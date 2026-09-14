#include "bruker_reader.hpp"
#include "fit.hpp"
#include "simulation.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double maximum(const xrr::Scan& scan){double value=0;for(const auto& point:scan.points)value=std::max(value,point.intensity);return value;}

int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"fit_tests sample.raw\n";return 1;}
    try {
        xrr::LayerStack truth;truth.rows={
            {false,"Ru","12.379","8.423","0.230"},
            {false,"Co80Tb20","8.397","50.428","0.491"},
            {true,"SiO2","2.650","","0.481"}
        };
        auto available=xrr::availableFitVariables(truth);check(available.size()==8,"Unexpected fit-variable count");
        check(std::abs(available[0].lower-available[0].initial*.8)<1e-12&&std::abs(available[0].upper-available[0].initial*1.2)<1e-12,
              "Default fitting bounds are not +/-20 percent");
        truth.rows[0].roughness.clear();available=xrr::availableFitVariables(truth);
        const auto zero=std::find_if(available.begin(),available.end(),[](const auto& v){return v.row==0&&v.parameter==xrr::FitParameter::Roughness;});
        check(zero!=available.end()&&zero->lower==0&&zero->upper==.2,"Zero roughness did not receive a usable 0-0.2 nm bound");
        truth.rows[0].roughness="0.230";

        xrr::Scan synthetic;synthetic.name="synthetic";synthetic.format="test";synthetic.scanType="coupled";
        synthetic.nominalStart=.7;synthetic.nominalEnd=5;synthetic.step=.015;synthetic.wavelength=1.5406;
        for(double angle=.7;angle<=5.0001;angle+=synthetic.step)synthetic.points.push_back({angle,1, false});
        synthetic.points.front().intensity=2.0e6;synthetic.records=synthetic.points.size();
        xrr::PoissonModel trueNoise{true,.035,2.7,2.0e-5};const auto mean=xrr::simulateXrr(synthetic,truth,trueNoise);
        std::mt19937 random(7321);
        for(std::size_t i=0;i<synthetic.points.size();++i) {
            std::poisson_distribution<int> draw(std::min(2.0e6,mean.points[i].intensity));
            synthetic.points[i].intensity=draw(random);
        }

        auto initial=truth;initial.rows[0].thickness="7.8";initial.rows[1].thickness="48.0";
        available=xrr::availableFitVariables(initial);
        for(auto& variable:available)variable.selected=variable.parameter==xrr::FitParameter::Thickness;
        xrr::FitOptions options;options.populationSize=64;options.generations=75;options.maxSamplePoints=500;options.workerThreads=4;
        const auto fitted=xrr::fitXrrGenetic(synthetic,initial,available,{},options);
        check(!fitted.cancelled&&fitted.variables.size()==2,"Synthetic Genetic Algorithm fit failed");
        check(fitted.objective==xrr::FitObjective::RobustPoisson&&fitted.finalObjective<fitted.initialObjective,
              "Robust Poisson objective was not used or did not improve");
        check(fitted.finalPoissonDeviance<fitted.initialPoissonDeviance*.35,"Poisson deviance did not improve enough");
        check(std::abs(fitted.variables[0].fitted-8.423)<.45&&std::abs(fitted.variables[1].fitted-50.428)<1.5,
              "Synthetic thickness recovery is outside tolerance");
        check(std::abs(maximum(fitted.fittedCurve)-maximum(synthetic))/maximum(synthetic)<1e-12,
              "Fitted curve maximum no longer matches the imported maximum");
        const auto cancelled=xrr::fitXrrGenetic(synthetic,initial,available,{},options,{},[]{return true;});
        check(cancelled.cancelled,"Genetic Algorithm cancellation was ignored");

        xrr::FitOptions rangeOptions;rangeOptions.populationSize=16;rangeOptions.generations=2;rangeOptions.maxSamplePoints=64;
        rangeOptions.workerThreads=2;rangeOptions.twoThetaMinimum=1.1;rangeOptions.twoThetaMaximum=1.3;
        rangeOptions.objective=xrr::FitObjective::PurePoisson;
        const auto ranged=xrr::fitXrrGenetic(synthetic,initial,available,{},rangeOptions);
        const auto expectedRangePoints=static_cast<std::size_t>(std::count_if(synthetic.points.begin(),synthetic.points.end(),[](const auto& point){
            return point.intensity>=0&&point.twoTheta>=1.1&&point.twoTheta<=1.3;
        }));
        check(ranged.pointsInFitRange==expectedRangePoints&&ranged.sampledPoints==expectedRangePoints,
              "The selected 2theta range did not control the fitting objective");
        check(ranged.fittedCurve.points.size()==synthetic.points.size(),"A ranged fit did not return a full-grid curve");
        check(ranged.fitTwoThetaMinimum>=1.1&&ranged.fitTwoThetaMaximum<=1.3,"The reported fitting range is incorrect");
        check(ranged.objective==xrr::FitObjective::PurePoisson,"Pure Poisson compatibility mode was ignored");
        bool badRangeRejected=false;rangeOptions.twoThetaMinimum=2.0;rangeOptions.twoThetaMaximum=1.0;
        try{(void)xrr::fitXrrGenetic(synthetic,initial,available,{},rangeOptions);}catch(const std::exception&){badRangeRejected=true;}
        check(badRangeRejected,"An inverted 2theta fitting range was accepted");

        const auto measured=xrr::readBruker(argv[1]).at(0);auto referenceVariables=xrr::availableFitVariables(truth);
        for(auto& variable:referenceVariables)variable.selected=variable.parameter==xrr::FitParameter::Thickness;
        xrr::FitOptions referenceOptions;referenceOptions.populationSize=64;referenceOptions.generations=45;referenceOptions.maxSamplePoints=1500;
        const auto reference=xrr::fitXrrGenetic(measured,truth,referenceVariables,{},referenceOptions);
        check(!reference.cancelled&&reference.finalPoissonDeviance<reference.initialPoissonDeviance,"Uploaded RAW reference fit did not improve");
        check(reference.variables[0].fitted>=reference.variables[0].lower&&reference.variables[0].fitted<=reference.variables[0].upper
           &&reference.variables[1].fitted>=reference.variables[1].lower&&reference.variables[1].fitted<=reference.variables[1].upper,
              "Uploaded RAW reference result escaped user bounds");
        std::cout<<"Genetic/Poisson fitting tests passed. Synthetic deviance "<<fitted.initialPoissonDeviance<<" -> "
                 <<fitted.finalPoissonDeviance<<"; uploaded RAW benchmark "<<reference.elapsedMilliseconds<<" ms, "
                 <<reference.workerThreads<<" threads.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
