#include "simulation.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <locale>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace xrr {
namespace {
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double avogadro = 6.02214076e23;
constexpr double electronRadiusAngstrom = 2.8179403262e-5;

struct Element { const char* symbol; int electrons; double atomicMass; };
constexpr std::array<Element,118> elements{{
    {"H",1,1.008},{"He",2,4.002602},{"Li",3,6.94},{"Be",4,9.0121831},{"B",5,10.81},
    {"C",6,12.011},{"N",7,14.007},{"O",8,15.999},{"F",9,18.998403163},{"Ne",10,20.1797},
    {"Na",11,22.98976928},{"Mg",12,24.305},{"Al",13,26.9815385},{"Si",14,28.085},{"P",15,30.973761998},
    {"S",16,32.06},{"Cl",17,35.45},{"Ar",18,39.948},{"K",19,39.0983},{"Ca",20,40.078},
    {"Sc",21,44.955908},{"Ti",22,47.867},{"V",23,50.9415},{"Cr",24,51.9961},{"Mn",25,54.938044},
    {"Fe",26,55.845},{"Co",27,58.933194},{"Ni",28,58.6934},{"Cu",29,63.546},{"Zn",30,65.38},
    {"Ga",31,69.723},{"Ge",32,72.630},{"As",33,74.921595},{"Se",34,78.971},{"Br",35,79.904},
    {"Kr",36,83.798},{"Rb",37,85.4678},{"Sr",38,87.62},{"Y",39,88.90584},{"Zr",40,91.224},
    {"Nb",41,92.90637},{"Mo",42,95.95},{"Tc",43,98.0},{"Ru",44,101.07},{"Rh",45,102.90550},
    {"Pd",46,106.42},{"Ag",47,107.8682},{"Cd",48,112.414},{"In",49,114.818},{"Sn",50,118.710},
    {"Sb",51,121.760},{"Te",52,127.60},{"I",53,126.90447},{"Xe",54,131.293},{"Cs",55,132.90545196},
    {"Ba",56,137.327},{"La",57,138.90547},{"Ce",58,140.116},{"Pr",59,140.90766},{"Nd",60,144.242},
    {"Pm",61,145.0},{"Sm",62,150.36},{"Eu",63,151.964},{"Gd",64,157.25},{"Tb",65,158.92535},
    {"Dy",66,162.500},{"Ho",67,164.93033},{"Er",68,167.259},{"Tm",69,168.93422},{"Yb",70,173.045},
    {"Lu",71,174.9668},{"Hf",72,178.49},{"Ta",73,180.94788},{"W",74,183.84},{"Re",75,186.207},
    {"Os",76,190.23},{"Ir",77,192.217},{"Pt",78,195.084},{"Au",79,196.966569},{"Hg",80,200.592},
    {"Tl",81,204.38},{"Pb",82,207.2},{"Bi",83,208.98040},{"Po",84,209.0},{"At",85,210.0},
    {"Rn",86,222.0},{"Fr",87,223.0},{"Ra",88,226.0},{"Ac",89,227.0},{"Th",90,232.0377},
    {"Pa",91,231.03588},{"U",92,238.02891},{"Np",93,237.0},{"Pu",94,244.0},{"Am",95,243.0},
    {"Cm",96,247.0},{"Bk",97,247.0},{"Cf",98,251.0},{"Es",99,252.0},{"Fm",100,257.0},
    {"Md",101,258.0},{"No",102,259.0},{"Lr",103,266.0},{"Rf",104,267.0},{"Db",105,268.0},
    {"Sg",106,269.0},{"Bh",107,270.0},{"Hs",108,277.0},{"Mt",109,278.0},{"Ds",110,281.0},
    {"Rg",111,282.0},{"Cn",112,285.0},{"Nh",113,286.0},{"Fl",114,289.0},{"Mc",115,290.0},
    {"Lv",116,293.0},{"Ts",117,294.0},{"Og",118,294.0}
}};

std::string trim(std::string text) {
    const auto first=std::find_if_not(text.begin(),text.end(),[](unsigned char c){return std::isspace(c)!=0;});
    const auto last=std::find_if_not(text.rbegin(),text.rend(),[](unsigned char c){return std::isspace(c)!=0;}).base();
    return first<last?std::string(first,last):std::string{};
}

std::string asciiSubscripts(std::string text) {
    std::string out;out.reserve(text.size());
    for(std::size_t i=0;i<text.size();) {
        const auto a=static_cast<unsigned char>(text[i]);
        if(i+2<text.size()&&a==0xE2&&static_cast<unsigned char>(text[i+1])==0x82) {
            const auto c=static_cast<unsigned char>(text[i+2]);
            if(c>=0x80&&c<=0x89){out+=static_cast<char>('0'+c-0x80);i+=3;continue;}
        }
        out+=text[i++];
    }
    return out;
}

std::string formulaToken(const std::string& label) {
    const auto text=trim(asciiSubscripts(label));std::size_t count=0;
    while(count<text.size()) {
        const auto c=static_cast<unsigned char>(text[count]);
        if(!(std::isalnum(c)||c=='.'))break;
        ++count;
    }
    if(!count)throw std::runtime_error("enter a chemical formula at the start of Material / note");
    return text.substr(0,count);
}

const Element& element(std::string_view symbol) {
    const auto found=std::find_if(elements.begin(),elements.end(),[&](const Element& e){return symbol==e.symbol;});
    if(found==elements.end())throw std::runtime_error("unknown element symbol '"+std::string(symbol)+"'");
    return *found;
}

double strictNumber(const std::string& text,const std::string& field) {
    std::istringstream in(text);in.imbue(std::locale::classic());double value=0;
    if(!(in>>value)||!std::isfinite(value))throw std::runtime_error("enter "+field);
    in>>std::ws;if(!in.eof())throw std::runtime_error("enter a valid "+field);
    return value;
}

double optionalNonnegative(const std::string& text,const std::string& field) {
    if(trim(text).empty())return 0;
    const auto value=strictNumber(text,field);
    if(value<0)throw std::runtime_error(field+" must be zero or greater");
    return value;
}

std::string rowName(std::size_t row,const Layer& layer) {
    return layer.substrate?"Substrate":"Film "+std::to_string(row+1);
}
}

double electronDensityAngstrom3(const std::string& material,double densityGcm3) {
    if(!std::isfinite(densityGcm3)||densityGcm3<=0)throw std::runtime_error("density must be greater than zero");
    const auto formula=formulaToken(material);double electrons=0,molarMass=0;
    for(std::size_t i=0;i<formula.size();) {
        if(!std::isupper(static_cast<unsigned char>(formula[i])))throw std::runtime_error("invalid formula '"+formula+"'");
        const std::size_t begin=i++;
        if(i<formula.size()&&std::islower(static_cast<unsigned char>(formula[i])))++i;
        const auto& e=element(std::string_view(formula).substr(begin,i-begin));double amount=1;
        if(i<formula.size()&&(std::isdigit(static_cast<unsigned char>(formula[i]))||formula[i]=='.')) {
            char* end=nullptr;amount=std::strtod(formula.c_str()+i,&end);
            if(end==formula.c_str()+i||!std::isfinite(amount)||amount<=0)
                throw std::runtime_error("invalid stoichiometry in '"+formula+"'");
            i=static_cast<std::size_t>(end-formula.c_str());
        }
        electrons+=amount*e.electrons;molarMass+=amount*e.atomicMass;
    }
    if(!std::isfinite(electrons)||!std::isfinite(molarMass)||electrons<=0||molarMass<=0)
        throw std::runtime_error("invalid formula '"+formula+"'");
    const double value=densityGcm3/molarMass*avogadro*electrons/1.0e24;
    if(!std::isfinite(value)||value<=0)throw std::runtime_error("electron-density conversion failed");
    return value;
}

void calculateParrattReflectivity(const std::vector<double>& twoTheta,double wavelength,
                                  const std::vector<OpticalLayer>& layers,std::vector<double>& reflectivity,
                                  std::vector<std::complex<double>>& kz) {
    if(layers.empty())throw std::runtime_error("the optical stack has no substrate");
    if(!std::isfinite(wavelength)||wavelength<=0)throw std::runtime_error("invalid X-ray wavelength");
    const std::size_t count=layers.size()+1;
    kz.resize(count);reflectivity.clear();reflectivity.reserve(twoTheta.size());
    for(const auto twoThetaValue:twoTheta) {
        const double theta=twoThetaValue*pi/360.0;
        const double kzAmbient=2.0*pi/wavelength*std::sin(theta);
        kz[0]=std::complex<double>(kzAmbient,0);
        for(std::size_t j=1;j<count;++j)
            kz[j]=std::sqrt(std::complex<double>(kzAmbient*kzAmbient-4.0*pi*layers[j-1].sld,1.0e-24));
        std::complex<double> amplitude=0;
        for(std::size_t upper=count-1;upper-->0;) {
            const auto denominator=kz[upper]+kz[upper+1];
            if(std::abs(denominator)<1.0e-30)continue;
            auto fresnel=(kz[upper]-kz[upper+1])/denominator;
            const double sigma=layers[upper].roughnessAngstrom;
            auto exponent=-2.0*kz[upper]*kz[upper+1]*sigma*sigma;
            if(exponent.real()>50)exponent.real(50);
            if(exponent.real()<-700)exponent.real(-700);
            fresnel*=std::exp(exponent);
            const double lowerThickness=layers[upper].thicknessAngstrom;
            const auto phase=std::exp(std::complex<double>(0,2)*kz[upper+1]*lowerThickness);
            const auto recursiveDenominator=1.0+fresnel*amplitude*phase;
            amplitude=std::abs(recursiveDenominator)<1.0e-30?fresnel:(fresnel+amplitude*phase)/recursiveDenominator;
        }
        double value=std::norm(amplitude);if(!std::isfinite(value))value=1;
        reflectivity.push_back(std::clamp(value,0.0,1.0));
    }
}

void calculateSpectrumReflectivity(const std::vector<double>& twoTheta,double primaryWavelength,
                                   double secondaryWavelength,double secondaryRatio,
                                   const std::vector<OpticalLayer>& layers,std::vector<double>& reflectivity,
                                   std::vector<double>& secondaryWorkspace,std::vector<std::complex<double>>& kz) {
    calculateParrattReflectivity(twoTheta,primaryWavelength,layers,reflectivity,kz);
    if(!(std::isfinite(secondaryWavelength)&&secondaryWavelength>0&&std::isfinite(secondaryRatio)&&secondaryRatio>0))return;
    calculateParrattReflectivity(twoTheta,secondaryWavelength,layers,secondaryWorkspace,kz);
    const double inverse=1.0/(1.0+secondaryRatio);
    for(std::size_t i=0;i<reflectivity.size();++i)
        reflectivity[i]=(reflectivity[i]+secondaryRatio*secondaryWorkspace[i])*inverse;
}

namespace {
void broadenResolution(const Scan& measured,const std::vector<double>& source,double fwhm,
                       std::vector<double>& destination,std::vector<double>& scratch) {
    destination=source;
    if(!(std::isfinite(fwhm)&&fwhm>0)||source.size()<3)return;
    double spacing=std::abs(measured.step);
    if(!(std::isfinite(spacing)&&spacing>0))return;
    const double sigma=fwhm/2.3548200450309493;
    const double variance=(sigma/spacing)*(sigma/spacing);
    if(variance<1.0e-4)return;
    const std::size_t passes=std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(variance/0.9)),1,64);
    const double alpha=std::min(0.45,variance/(2.0*static_cast<double>(passes)));
    scratch.resize(source.size());
    auto* current=&destination;auto* next=&scratch;
    const double connectionLimit=spacing*2.25;
    for(std::size_t pass=0;pass<passes;++pass) {
        for(std::size_t i=0;i<source.size();++i) {
            const bool left=i>0&&!measured.points[i].gapBefore
                &&std::abs(measured.points[i].twoTheta-measured.points[i-1].twoTheta)<=connectionLimit;
            const bool right=i+1<source.size()&&!measured.points[i+1].gapBefore
                &&std::abs(measured.points[i+1].twoTheta-measured.points[i].twoTheta)<=connectionLimit;
            double value=(*current)[i];
            if(left)value+=alpha*((*current)[i-1]-(*current)[i]);
            if(right)value+=alpha*((*current)[i+1]-(*current)[i]);
            (*next)[i]=std::max(0.0,value);
        }
        std::swap(current,next);
    }
    if(current!=&destination)destination.swap(scratch);
}
}

double composeExpectedIntensity(const Scan& measured,const std::vector<double>& reflectivity,
                                const PoissonModel& poisson,std::vector<double>& expected,
                                std::vector<double>* resolutionWorkspace) {
    if(reflectivity.size()!=measured.points.size())throw std::runtime_error("reflectivity grid does not match measurement");
    double measuredMaximum=0,qzMinimum=std::numeric_limits<double>::infinity();
    const double wavelength=(std::isfinite(measured.wavelength)&&measured.wavelength>0)?measured.wavelength:1.5406;
    for(const auto& p:measured.points) {
        if(std::isfinite(p.intensity)&&p.intensity>0)measuredMaximum=std::max(measuredMaximum,p.intensity);
        const double qz=4.0*pi*std::sin(p.twoTheta*pi/360.0)/wavelength;
        if(std::isfinite(qz)&&qz>0)qzMinimum=std::min(qzMinimum,qz);
    }
    if(!(measuredMaximum>0))throw std::runtime_error("the active scan has no positive intensity");
    if(!std::isfinite(qzMinimum))qzMinimum=1;
    if(poisson.enabled&&(!std::isfinite(poisson.diffuseStrength)||poisson.diffuseStrength<0||poisson.diffuseStrength>5
       ||!std::isfinite(poisson.diffuseExponent)||poisson.diffuseExponent<0.1||poisson.diffuseExponent>10
       ||!std::isfinite(poisson.detectorFraction)||poisson.detectorFraction<0||poisson.detectorFraction>1
       ||!std::isfinite(poisson.resolutionFwhmDegrees)||poisson.resolutionFwhmDegrees<0||poisson.resolutionFwhmDegrees>0.2))
        throw std::runtime_error("invalid Poisson background model");
    std::vector<double> localWorkspace;
    auto& workspace=resolutionWorkspace?*resolutionWorkspace:localWorkspace;
    broadenResolution(measured,reflectivity,poisson.enabled?poisson.resolutionFwhmDegrees:0,expected,workspace);
    double modelMaximum=0;
    for(std::size_t i=0;i<reflectivity.size();++i) {
        double value=expected[i];
        if(poisson.enabled) {
            const double qz=std::max(qzMinimum,4.0*pi*std::sin(measured.points[i].twoTheta*pi/360.0)/wavelength);
            const double reducedQ=std::max(0.0,qz/qzMinimum-1.0);
            value*=std::exp(-poisson.diffuseStrength*std::pow(reducedQ,poisson.diffuseExponent));
            value+=poisson.detectorFraction;
        }
        if(!std::isfinite(value)||value<0)value=0;
        expected[i]=value;modelMaximum=std::max(modelMaximum,value);
    }
    if(!(modelMaximum>0))throw std::runtime_error("calculated reflectivity is zero");
    const double scale=measuredMaximum/modelMaximum;
    for(auto& value:expected)value*=scale;
    return scale;
}

Scan simulateXrr(const Scan& measured,const LayerStack& stack,const PoissonModel& poisson) {
    stack.validateStructure();const auto validity=stack.validationError();
    if(!validity.empty())throw std::runtime_error(validity);
    if(measured.points.empty())throw std::runtime_error("the active scan has no measured points");

    // Contiguous arrays are faster than a linked list here: the bottom-up
    // recurrence is linear, cache-friendly, and needs no node allocations.
    std::vector<OpticalLayer> optical;optical.reserve(stack.rows.size());
    for(std::size_t row=0;row<stack.rows.size();++row) {
        const auto& layer=stack.rows[row];const auto label=rowName(row,layer);
        if(trim(layer.material).empty())throw std::runtime_error(label+": enter Material / note as a chemical formula");
        if(trim(layer.density).empty())throw std::runtime_error(label+": enter density");
        const double density=strictNumber(layer.density,"density");
        if(density<=0)throw std::runtime_error(label+": density must be greater than zero");
        OpticalLayer value;
        try{value.sld=electronRadiusAngstrom*electronDensityAngstrom3(layer.material,density);}
        catch(const std::exception& e){throw std::runtime_error(label+": "+e.what());}
        if(!layer.substrate) {
            if(trim(layer.thickness).empty())throw std::runtime_error(label+": enter expected thickness");
            const double nm=strictNumber(layer.thickness,"expected thickness");
            if(nm<0)throw std::runtime_error(label+": thickness must be zero or greater");
            value.thicknessAngstrom=nm*10.0;
        }
        value.roughnessAngstrom=optionalNonnegative(layer.roughness,"roughness")*10.0;
        optical.push_back(value);
    }

    const double wavelength=(std::isfinite(measured.wavelength)&&measured.wavelength>0)?measured.wavelength:1.5406;
    std::vector<double> angles;angles.reserve(measured.points.size());for(const auto& p:measured.points)angles.push_back(p.twoTheta);
    std::vector<double> reflectivity,secondary,expected,resolution;std::vector<std::complex<double>> kz;
    calculateSpectrumReflectivity(angles,wavelength,measured.secondaryWavelength,measured.secondaryRatio,
                                  optical,reflectivity,secondary,kz);
    composeExpectedIntensity(measured,reflectivity,poisson,expected,&resolution);

    Scan result;result.name="Layer-stack simulation";
    result.format=poisson.enabled?"Parratt + Poisson nuisance + resolution":"Parratt + Nevot-Croce";
    if(measured.secondaryRatio>0)result.format+=" + Kalpha doublet";
    result.scanType=measured.scanType;result.records=measured.records;result.unmeasured=measured.unmeasured;
    result.nominalStart=measured.nominalStart;result.nominalEnd=measured.nominalEnd;result.step=measured.step;result.wavelength=wavelength;
    result.secondaryWavelength=measured.secondaryWavelength;result.secondaryRatio=measured.secondaryRatio;
    result.points.reserve(measured.points.size());
    for(std::size_t i=0;i<measured.points.size();++i)
        result.points.push_back({measured.points[i].twoTheta,expected[i],measured.points[i].gapBefore});
    return result;
}
}
