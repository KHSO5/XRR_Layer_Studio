#include "fit.hpp"
#include "simulation.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <exception>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace xrr {
namespace {
constexpr double electronRadiusAngstrom=2.8179403262e-5;

double number(const std::string& text,const char* field,bool blankAsZero=false) {
    if(blankAsZero&&text.find_first_not_of(" \t\r\n")==std::string::npos)return 0;
    std::istringstream in(text);in.imbue(std::locale::classic());double value=0;
    if(!(in>>value)||!std::isfinite(value))throw std::runtime_error(std::string("enter ")+field);
    in>>std::ws;if(!in.eof())throw std::runtime_error(std::string("enter a valid ")+field);
    return value;
}

std::string numericText(double value) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(10)<<value;return out.str();
}

std::string layerName(const LayerStack& stack,std::size_t row) {
    return stack.rows[row].substrate?"Substrate":"Film "+std::to_string(row+1);
}

bool selectedInLayer(const Layer& layer,FitParameter parameter) {
    if(parameter==FitParameter::Density)return layer.fitDensity;
    if(parameter==FitParameter::Thickness)return layer.fitThickness;
    return layer.fitRoughness;
}

double layerValue(const Layer& layer,FitParameter parameter) {
    if(parameter==FitParameter::Density)return number(layer.density,"density");
    if(parameter==FitParameter::Thickness)return number(layer.thickness,"thickness");
    return number(layer.roughness,"roughness",true);
}

void setLayerValue(Layer& layer,FitParameter parameter,double value) {
    auto text=numericText(value);
    if(parameter==FitParameter::Density)layer.density=std::move(text);
    else if(parameter==FitParameter::Thickness)layer.thickness=std::move(text);
    else layer.roughness=std::move(text);
}

struct PreparedLayer {
    double sldPerDensity=0,density=0,thicknessAngstrom=0,roughnessAngstrom=0;
};

struct Workspace {
    std::vector<OpticalLayer> layers;
    std::vector<double> reflectivity,secondary,expected,resolution;
    std::vector<std::complex<double>> kz;
};

struct SampledData {
    Scan scan;
    std::vector<std::uint8_t> objective;
    std::size_t pointsInRange=0;
    double firstAngle=0,lastAngle=0;
};

bool insideRange(double angle,const std::optional<double>& minimum,const std::optional<double>& maximum) {
    return (!minimum||angle>=*minimum)&&(!maximum||angle<=*maximum);
}

SampledData sampledScan(const Scan& measured,std::size_t maximum,const std::optional<double>& minimum,
                        const std::optional<double>& maximumAngle) {
    if((minimum&&!std::isfinite(*minimum))||(maximumAngle&&!std::isfinite(*maximumAngle)))
        throw std::runtime_error("the 2theta fitting range must contain finite numbers");
    if(minimum&&maximumAngle&&!(*maximumAngle>*minimum))
        throw std::runtime_error("the 2theta fitting maximum must be greater than the minimum");
    std::vector<std::size_t> usable;usable.reserve(measured.points.size());
    std::size_t maximumIndex=0,minimumAngleIndex=0;double maximumValue=-1,minimumPositiveAngle=std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i<measured.points.size();++i)if(std::isfinite(measured.points[i].intensity)&&measured.points[i].intensity>=0) {
        const auto& point=measured.points[i];
        if(point.intensity>maximumValue){maximumValue=point.intensity;maximumIndex=i;}
        if(point.twoTheta>0&&point.twoTheta<minimumPositiveAngle){minimumPositiveAngle=point.twoTheta;minimumAngleIndex=i;}
        if(insideRange(point.twoTheta,minimum,maximumAngle))usable.push_back(i);
    }
    if(!(maximumValue>0))throw std::runtime_error("the active scan has no nonnegative count data");
    if(usable.size()<8)throw std::runtime_error("the selected 2theta range contains fewer than 8 usable data points");
    if(maximum<64)maximum=64;
    std::vector<std::size_t> chosen;
    if(usable.size()<=maximum)chosen=usable;
    else {
        chosen.reserve(maximum+2);
        for(std::size_t i=0;i<maximum;++i)chosen.push_back(usable[i*(usable.size()-1)/(maximum-1)]);
    }
    // Keep the full-scan intensity maximum and lowest positive angle as
    // normalization anchors even when they are outside the objective range.
    chosen.push_back(maximumIndex);if(std::isfinite(minimumPositiveAngle))chosen.push_back(minimumAngleIndex);
    std::sort(chosen.begin(),chosen.end());chosen.erase(std::unique(chosen.begin(),chosen.end()),chosen.end());
    SampledData result;result.scan=measured;result.scan.points.clear();result.scan.points.reserve(chosen.size());
    result.objective.reserve(chosen.size());result.pointsInRange=usable.size();
    result.firstAngle=measured.points[usable.front()].twoTheta;result.lastAngle=result.firstAngle;
    for(const auto index:usable){result.firstAngle=std::min(result.firstAngle,measured.points[index].twoTheta);result.lastAngle=std::max(result.lastAngle,measured.points[index].twoTheta);}
    for(const auto index:chosen) {
        result.scan.points.push_back(measured.points[index]);
        result.objective.push_back(insideRange(measured.points[index].twoTheta,minimum,maximumAngle)?1u:0u);
    }
    std::vector<double> spacing;spacing.reserve(result.scan.points.size());
    for(std::size_t i=1;i<result.scan.points.size();++i)if(!result.scan.points[i].gapBefore) {
        const double difference=std::abs(result.scan.points[i].twoTheta-result.scan.points[i-1].twoTheta);
        if(std::isfinite(difference)&&difference>0)spacing.push_back(difference);
    }
    if(!spacing.empty()) {
        const auto middle=spacing.begin()+static_cast<std::ptrdiff_t>(spacing.size()/2);
        std::nth_element(spacing.begin(),middle,spacing.end());result.scan.step=*middle;
    }
    result.scan.records=result.scan.points.size();result.scan.unmeasured=result.scan.nonfinite=result.scan.nonpositive=0;return result;
}

double poissonDevianceTerm(double y,double mu) {
    mu=std::max(1.0e-12,mu);
    if(!(y>0))return 2.0*mu;
    const double relative=(mu-y)/y;
    const double value=std::abs(relative)<1.0e-3?2.0*y*(relative-std::log1p(relative))
                                                      :2.0*(mu-y+y*std::log(y/mu));
    return std::max(0.0,value);
}

double objectiveTerm(double y,double mu,FitObjective objective) {
    const double value=poissonDevianceTerm(y,mu);
    // Cauchy loss applied to the signed Poisson deviance residual. It remains
    // count-aware close to the optimum but prevents a misspecified high-count
    // region from suppressing several decades of weak XRR fringes.
    return objective==FitObjective::RobustPoisson?4.0*std::log1p(value/4.0):value;
}

double metric(const Scan& measured,const Scan& predicted,FitObjective objective,
              const std::optional<double>& minimum={},const std::optional<double>& maximum={}) {
    if(measured.points.size()!=predicted.points.size())throw std::runtime_error("fit curve grid mismatch");
    long double sum=0;std::size_t used=0;
    for(std::size_t i=0;i<measured.points.size();++i) {
        if(!insideRange(measured.points[i].twoTheta,minimum,maximum))continue;
        const double y=measured.points[i].intensity;
        if(!std::isfinite(y)||y<0)continue;
        const double mu=std::max(1.0e-12,predicted.points[i].intensity);
        sum+=objectiveTerm(y,mu,objective);++used;
    }
    return used?static_cast<double>(sum/used):std::numeric_limits<double>::infinity();
}

double logRmse(const Scan& measured,const Scan& predicted,double minimum,double maximum) {
    if(measured.points.size()!=predicted.points.size())throw std::runtime_error("fit curve grid mismatch");
    long double sum=0;std::size_t used=0;
    for(std::size_t i=0;i<measured.points.size();++i) {
        const double angle=measured.points[i].twoTheta,y=measured.points[i].intensity,mu=predicted.points[i].intensity;
        if(angle<minimum||angle>maximum||!std::isfinite(y)||y<0||!std::isfinite(mu)||mu<0)continue;
        const double difference=std::log10(std::max(1.0e-12,mu))-std::log10(std::max(1.0e-12,y));
        sum+=difference*difference;++used;
    }
    return used?std::sqrt(static_cast<double>(sum/used)):std::numeric_limits<double>::infinity();
}

struct Problem {
    Scan measured;
    std::vector<double> angles;
    std::vector<std::uint8_t> objective;
    std::vector<PreparedLayer> base;
    std::vector<FitVariable> variables;
    FitObjective objectiveMode=FitObjective::RobustPoisson;

    std::size_t dimensions()const{return variables.size()+4;}
    PoissonModel poisson(const double* genes)const {
        const std::size_t offset=variables.size();
        PoissonModel p;p.enabled=true;
        p.diffuseStrength=2.0*genes[offset]*genes[offset]*genes[offset];
        p.diffuseExponent=0.5+2.5*genes[offset+1];
        const double detectorSquared=genes[offset+2]*genes[offset+2];
        p.detectorFraction=0.2*detectorSquared*detectorSquared;
        p.resolutionFwhmDegrees=0.05*genes[offset+3]*genes[offset+3];
        return p;
    }
    double physical(const FitVariable& variable,double gene)const {
        return variable.lower+(variable.upper-variable.lower)*gene;
    }
    double evaluate(const double* genes,Workspace& work,double* scaleOut=nullptr)const {
        work.layers.resize(base.size());
        for(std::size_t i=0;i<base.size();++i)
            work.layers[i]={base[i].sldPerDensity*base[i].density,base[i].thicknessAngstrom,base[i].roughnessAngstrom};
        for(std::size_t i=0;i<variables.size();++i) {
            const auto& v=variables[i];const double value=physical(v,genes[i]);auto& layer=work.layers[v.row];
            if(v.parameter==FitParameter::Density)layer.sld=base[v.row].sldPerDensity*value;
            else if(v.parameter==FitParameter::Thickness)layer.thicknessAngstrom=value*10.0;
            else layer.roughnessAngstrom=value*10.0;
        }
        calculateSpectrumReflectivity(angles,measured.wavelength,measured.secondaryWavelength,measured.secondaryRatio,
                                     work.layers,work.reflectivity,work.secondary,work.kz);
        const double scale=composeExpectedIntensity(measured,work.reflectivity,poisson(genes),work.expected,&work.resolution);
        if(scaleOut)*scaleOut=scale;
        long double loss=0;std::size_t used=0;
        for(std::size_t i=0;i<measured.points.size();++i) {
            if(!objective[i])continue;
            const double y=measured.points[i].intensity;if(!std::isfinite(y)||y<0)continue;
            const double mu=std::max(1.0e-12,work.expected[i]);
            loss+=objectiveTerm(y,mu,objectiveMode);++used;
        }
        return used?static_cast<double>(loss/used):std::numeric_limits<double>::infinity();
    }
};

unsigned workerCount(unsigned requested,std::size_t jobs) {
    unsigned count=requested?requested:std::thread::hardware_concurrency();if(!count)count=1;
    return std::max(1u,std::min<unsigned>(count,static_cast<unsigned>(jobs)));
}

template<class Cancel>
bool evaluatePopulation(const Problem& problem,const std::vector<double>& population,std::vector<double>& fitness,
                        std::size_t populationSize,std::size_t dimensions,unsigned workers,std::atomic_size_t& evaluations,
                        const Cancel& cancelled) {
    std::atomic_size_t next{0};std::atomic_bool stopped{false};std::exception_ptr error;std::mutex errorMutex;
    std::vector<std::thread> pool;pool.reserve(workers);
    for(unsigned worker=0;worker<workers;++worker)pool.emplace_back([&]{
        Workspace workspace;
        while(!stopped.load(std::memory_order_relaxed)) {
            const auto index=next.fetch_add(1,std::memory_order_relaxed);if(index>=populationSize)break;
            if(cancelled()){stopped.store(true,std::memory_order_relaxed);break;}
            try {
                fitness[index]=problem.evaluate(population.data()+index*dimensions,workspace);
                evaluations.fetch_add(1,std::memory_order_relaxed);
            }catch(...) {
                {std::lock_guard<std::mutex> lock(errorMutex);if(!error)error=std::current_exception();}
                stopped.store(true,std::memory_order_relaxed);break;
            }
        }
    });
    for(auto& thread:pool)thread.join();
    if(error)std::rethrow_exception(error);
    return !stopped.load(std::memory_order_relaxed);
}

std::size_t tournament(const std::vector<double>& fitness,std::mt19937_64& random) {
    std::uniform_int_distribution<std::size_t> pick(0,fitness.size()-1);auto best=pick(random);
    for(int i=1;i<4;++i){const auto candidate=pick(random);if(fitness[candidate]<fitness[best])best=candidate;}return best;
}

double clampGene(double value){return std::clamp(value,0.0,1.0);}
}

std::vector<FitVariable> availableFitVariables(const LayerStack& stack) {
    stack.validateStructure();std::vector<FitVariable> result;result.reserve(stack.rows.size()*3);
    for(std::size_t row=0;row<stack.rows.size();++row) {
        const auto& layer=stack.rows[row];
        for(const auto parameter:{FitParameter::Density,FitParameter::Thickness,FitParameter::Roughness}) {
            if(layer.substrate&&parameter==FitParameter::Thickness)continue;
            const double initial=layerValue(layer,parameter);double lower=initial*0.8,upper=initial*1.2;
            if(parameter!=FitParameter::Density)lower=std::max(0.0,lower);
            if(!(upper>lower))upper=lower+(parameter==FitParameter::Density?0.1:0.2);
            result.push_back({row,parameter,initial,lower,upper,initial,selectedInLayer(layer,parameter)});
        }
    }
    return result;
}

std::string fitVariableName(const LayerStack& stack,const FitVariable& variable) {
    if(variable.row>=stack.rows.size())return "Invalid variable";
    const char* field=variable.parameter==FitParameter::Density?"density":variable.parameter==FitParameter::Thickness?"thickness":"roughness";
    return layerName(stack,variable.row)+" "+field;
}

FitResult fitXrrGenetic(const Scan& measured,const LayerStack& initialStack,const std::vector<FitVariable>& allVariables,
                        const PoissonModel& initialPoisson,const FitOptions& options,FitProgress progress,FitCancelled cancelled) {
    const auto started=std::chrono::steady_clock::now();FitResult result;result.fittedLayers=initialStack;
    initialStack.validateStructure();const auto stackError=initialStack.validationError();if(!stackError.empty())throw std::runtime_error(stackError);
    auto sample=sampledScan(measured,options.maxSamplePoints,options.twoThetaMinimum,options.twoThetaMaximum);
    Problem problem;problem.measured=std::move(sample.scan);problem.objective=std::move(sample.objective);problem.objectiveMode=options.objective;
    problem.angles.reserve(problem.measured.points.size());result.objective=options.objective;
    if(!std::isfinite(problem.measured.wavelength)||problem.measured.wavelength<=0)problem.measured.wavelength=1.5406;
    for(const auto& point:problem.measured.points)problem.angles.push_back(point.twoTheta);
    problem.base.reserve(initialStack.rows.size());
    for(std::size_t row=0;row<initialStack.rows.size();++row) {
        const auto& layer=initialStack.rows[row];PreparedLayer prepared;
        prepared.density=number(layer.density,"density");if(prepared.density<=0)throw std::runtime_error(layerName(initialStack,row)+": density must be greater than zero");
        prepared.sldPerDensity=electronRadiusAngstrom*electronDensityAngstrom3(layer.material,1.0);
        if(!layer.substrate){const double t=number(layer.thickness,"thickness");if(t<0)throw std::runtime_error(layerName(initialStack,row)+": thickness must be nonnegative");prepared.thicknessAngstrom=t*10.0;}
        const double roughness=number(layer.roughness,"roughness",true);if(roughness<0)throw std::runtime_error(layerName(initialStack,row)+": roughness must be nonnegative");prepared.roughnessAngstrom=roughness*10.0;
        problem.base.push_back(prepared);
    }
    std::unordered_set<std::size_t> unique;
    for(const auto& variable:allVariables)if(variable.selected) {
        if(variable.row>=initialStack.rows.size())throw std::runtime_error("fit variable refers to a missing layer");
        if(initialStack.rows[variable.row].substrate&&variable.parameter==FitParameter::Thickness)throw std::runtime_error("Substrate thickness cannot be fitted");
        if(!std::isfinite(variable.lower)||!std::isfinite(variable.upper)||!(variable.upper>variable.lower))throw std::runtime_error(fitVariableName(initialStack,variable)+": lower bound must be less than upper bound");
        if((variable.parameter==FitParameter::Density&&variable.lower<=0)||(variable.parameter!=FitParameter::Density&&variable.lower<0))throw std::runtime_error(fitVariableName(initialStack,variable)+": invalid lower bound");
        const double current=layerValue(initialStack.rows[variable.row],variable.parameter);
        if(current<variable.lower||current>variable.upper)throw std::runtime_error(fitVariableName(initialStack,variable)+": current value must lie inside its bounds");
        const auto key=variable.row*3+static_cast<std::size_t>(variable.parameter);if(!unique.insert(key).second)throw std::runtime_error("duplicate fit variable");
        auto copy=variable;copy.initial=current;copy.fitted=current;problem.variables.push_back(copy);
    }
    if(problem.variables.empty())throw std::runtime_error("select at least one structural parameter to fit");
    if(problem.variables.size()>24)throw std::runtime_error("select no more than 24 structural parameters in one fit");

    const std::size_t dimensions=problem.dimensions();
    const std::size_t populationSize=options.populationSize?options.populationSize:std::clamp<std::size_t>(dimensions*8,64,160);
    const std::size_t generations=options.generations?options.generations:std::clamp<std::size_t>(110+dimensions*3,120,180);
    if(populationSize<8||populationSize>4096||generations<1||generations>10000)throw std::runtime_error("invalid Genetic Algorithm settings");
    result.workerThreads=workerCount(options.workerThreads,populationSize);
    result.sampledPoints=static_cast<std::size_t>(std::count(problem.objective.begin(),problem.objective.end(),std::uint8_t{1}));
    result.pointsInFitRange=sample.pointsInRange;result.fitTwoThetaMinimum=sample.firstAngle;result.fitTwoThetaMaximum=sample.lastAngle;

    std::vector<double> initialGenes(dimensions,0.5);
    for(std::size_t i=0;i<problem.variables.size();++i)initialGenes[i]=clampGene((problem.variables[i].initial-problem.variables[i].lower)/(problem.variables[i].upper-problem.variables[i].lower));
    const std::size_t nuisance=problem.variables.size();double detectorEstimate=0,maximum=0;
    for(const auto& point:problem.measured.points)maximum=std::max(maximum,point.intensity);
    std::vector<double> tail;const auto start=problem.measured.points.size()*9/10;
    for(std::size_t i=start;i<problem.measured.points.size();++i)if(problem.measured.points[i].intensity>=0)tail.push_back(problem.measured.points[i].intensity);
    if(!tail.empty()){const auto middle=tail.begin()+static_cast<std::ptrdiff_t>(tail.size()/2);std::nth_element(tail.begin(),middle,tail.end());detectorEstimate=*middle/std::max(1.0,maximum);}
    const double diffuseInitial=initialPoisson.enabled?initialPoisson.diffuseStrength:0;
    const double exponentInitial=initialPoisson.enabled?initialPoisson.diffuseExponent:1;
    const double detectorInitial=initialPoisson.enabled?initialPoisson.detectorFraction:std::clamp(detectorEstimate,0.0,0.2);
    initialGenes[nuisance]=std::cbrt(std::clamp(diffuseInitial/2.0,0.0,1.0));
    initialGenes[nuisance+1]=clampGene((exponentInitial-0.5)/2.5);
    initialGenes[nuisance+2]=std::sqrt(std::sqrt(std::clamp(detectorInitial/0.2,0.0,1.0)));
    const double resolutionInitial=initialPoisson.enabled?initialPoisson.resolutionFwhmDegrees:0;
    initialGenes[nuisance+3]=std::sqrt(std::clamp(resolutionInitial/0.05,0.0,1.0));

    std::mt19937_64 random(options.randomSeed);std::uniform_real_distribution<double> uniform(0,1);std::normal_distribution<double> near(0,0.12);
    std::vector<double> population(populationSize*dimensions),next(population.size()),fitness(populationSize),nextFitness(populationSize);
    std::copy(initialGenes.begin(),initialGenes.end(),population.begin());
    for(std::size_t member=1;member<populationSize;++member)for(std::size_t gene=0;gene<dimensions;++gene)
        population[member*dimensions+gene]=member<populationSize/2?clampGene(initialGenes[gene]+near(random)):uniform(random);
    std::atomic_size_t evaluations{0};const auto isCancelled=[&]{return cancelled&&cancelled();};
    if(progress)progress(0);
    if(!evaluatePopulation(problem,population,fitness,populationSize,dimensions,result.workerThreads,evaluations,isCancelled))result.cancelled=true;
    if(result.cancelled){result.evaluations=evaluations.load();result.elapsedMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return result;}
    result.initialPoissonDeviance=fitness[0];double globalBest=*std::min_element(fitness.begin(),fitness.end());std::size_t stagnant=0;
    std::vector<std::size_t> order(populationSize);std::iota(order.begin(),order.end(),0);
    for(std::size_t generation=0;generation<generations;++generation) {
        if(isCancelled()){result.cancelled=true;break;}
        std::sort(order.begin(),order.end(),[&](auto a,auto b){return fitness[a]<fitness[b];});
        const std::size_t elites=std::max<std::size_t>(2,populationSize/20);
        for(std::size_t e=0;e<elites;++e)std::copy_n(population.data()+order[e]*dimensions,dimensions,next.data()+e*dimensions);
        const double fraction=static_cast<double>(generation)/static_cast<double>(std::max<std::size_t>(1,generations-1));
        const double sigma=0.12*(1-fraction)+0.012*fraction;std::normal_distribution<double> mutation(0,sigma);
        const double mutationRate=std::max(0.08,1.0/static_cast<double>(dimensions));std::uniform_real_distribution<double> blend(-0.15,1.15);
        for(std::size_t member=elites;member<populationSize;++member) {
            const auto first=tournament(fitness,random),second=tournament(fitness,random);
            for(std::size_t gene=0;gene<dimensions;++gene) {
                const double mix=blend(random);double value=mix*population[first*dimensions+gene]+(1-mix)*population[second*dimensions+gene];
                if(uniform(random)<mutationRate)value+=mutation(random);
                if(uniform(random)<0.006)value=uniform(random);
                next[member*dimensions+gene]=clampGene(value);
            }
        }
        if(stagnant>0&&stagnant%18==0)for(std::size_t member=populationSize-populationSize/10;member<populationSize;++member)
            for(std::size_t gene=0;gene<dimensions;++gene)next[member*dimensions+gene]=uniform(random);
        if(!evaluatePopulation(problem,next,nextFitness,populationSize,dimensions,result.workerThreads,evaluations,isCancelled)){result.cancelled=true;break;}
        population.swap(next);fitness.swap(nextFitness);const double best=*std::min_element(fitness.begin(),fitness.end());
        if(best<globalBest*(1-1.0e-8)){globalBest=best;stagnant=0;}else ++stagnant;
        result.generationsCompleted=generation+1;if(progress)progress(0.9*static_cast<double>(generation+1)/static_cast<double>(generations));
    }
    if(result.cancelled){result.evaluations=evaluations.load();result.elapsedMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return result;}
    const auto bestIndex=static_cast<std::size_t>(std::min_element(fitness.begin(),fitness.end())-fitness.begin());
    std::vector<double> best(dimensions);std::copy_n(population.data()+bestIndex*dimensions,dimensions,best.data());double bestFitness=fitness[bestIndex];
    Workspace polishWorkspace;double step=0.05;
    for(int pass=0;pass<8&&step>0.0005;++pass) {
        bool improved=false;
        for(std::size_t gene=0;gene<dimensions;++gene)for(const double direction:{-1.0,1.0}) {
            auto candidate=best;candidate[gene]=clampGene(candidate[gene]+direction*step);if(candidate[gene]==best[gene])continue;
            const double value=problem.evaluate(candidate.data(),polishWorkspace);evaluations.fetch_add(1);
            if(value<bestFitness){bestFitness=value;best=std::move(candidate);improved=true;}
            if(isCancelled()){result.cancelled=true;break;}
        }
        if(result.cancelled)break;
        if(!improved)step*=0.5;
        if(progress)progress(0.9+0.1*static_cast<double>(pass+1)/8);
    }
    if(result.cancelled){result.evaluations=evaluations.load();result.elapsedMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();return result;}

    result.variables=problem.variables;
    for(std::size_t i=0;i<result.variables.size();++i) {
        auto& variable=result.variables[i];variable.fitted=problem.physical(variable,best[i]);
        setLayerValue(result.fittedLayers.rows[variable.row],variable.parameter,variable.fitted);
    }
    result.poisson=problem.poisson(best.data());result.fittedCurve=simulateXrr(measured,result.fittedLayers,result.poisson);
    PoissonModel startingPoisson=problem.poisson(initialGenes.data());const auto initialCurve=simulateXrr(measured,initialStack,startingPoisson);
    result.initialPoissonDeviance=metric(measured,initialCurve,FitObjective::PurePoisson,options.twoThetaMinimum,options.twoThetaMaximum);
    result.finalPoissonDeviance=metric(measured,result.fittedCurve,FitObjective::PurePoisson,options.twoThetaMinimum,options.twoThetaMaximum);
    result.initialObjective=metric(measured,initialCurve,options.objective,options.twoThetaMinimum,options.twoThetaMaximum);
    result.finalObjective=metric(measured,result.fittedCurve,options.objective,options.twoThetaMinimum,options.twoThetaMaximum);
    result.highAngleStart=result.fitTwoThetaMinimum+(result.fitTwoThetaMaximum-result.fitTwoThetaMinimum)*2.0/3.0;
    result.initialHighAngleLogRmse=logRmse(measured,initialCurve,result.highAngleStart,result.fitTwoThetaMaximum);
    result.finalHighAngleLogRmse=logRmse(measured,result.fittedCurve,result.highAngleStart,result.fitTwoThetaMaximum);
    Workspace finalWorkspace;double scale=0;problem.evaluate(best.data(),finalWorkspace,&scale);evaluations.fetch_add(1);
    result.diffuseStrength=result.poisson.diffuseStrength;result.detectorMeanCounts=scale*result.poisson.detectorFraction;
    result.evaluations=evaluations.load();result.elapsedMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    if(progress)progress(1);
    return result;
}
}
