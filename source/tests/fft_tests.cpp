#include "bruker_reader.hpp"
#include "fft.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}

double nearestPeak(const xrr::ThicknessFftResult& result,double expected) {
    if(result.peaks.empty())return 1e300;
    const auto peak=std::min_element(result.peaks.begin(),result.peaks.end(),[&](const auto& left,const auto& right){
        return std::abs(left.thicknessNm-expected)<std::abs(right.thicknessNm-expected);
    });
    return peak->thicknessNm;
}

int main(int argc,char** argv) {
    if(argc!=3){std::cerr<<"fft_tests sample.raw sample.txt\n";return 1;}
    try {
        xrr::Scan synthetic;synthetic.name="known 42 nm period";synthetic.wavelength=1.5406;synthetic.step=.004;
        constexpr double trueThicknessNm=42.0,pi=3.14159265358979323846;
        for(int i=0;i<1800;++i) {
            const double q=.065+static_cast<double>(i)*.00032;
            const double twoTheta=360.0/pi*std::asin(q*synthetic.wavelength/(4.0*pi));
            const double logIntensity=7.0-7.5*q+0.36*std::cos(q*trueThicknessNm*10.0)+0.08*std::cos(q*13.0*10.0);
            synthetic.points.push_back({twoTheta,std::pow(10.0,logIntensity),false});
        }
        synthetic.records=synthetic.points.size();
        auto known=xrr::estimateThicknessFft(synthetic,{std::nullopt,std::nullopt,120,6});
        check(!known.spectrum.empty()&&!known.peaks.empty(),"Synthetic FFT returned no spectrum or peaks");
        check(std::abs(nearestPeak(known,trueThicknessNm)-trueThicknessNm)<.4,"Synthetic FFT did not recover the known 42 nm period");
        check(std::abs(nearestPeak(known,13.0)-13.0)<.5,"Synthetic FFT did not recover the secondary 13 nm period");
        const double firstQ=.12,secondQ=firstQ+2.0*pi/(trueThicknessNm*10.0);
        const auto angle=[&](double q){return 360.0/pi*std::asin(q*synthetic.wavelength/(4.0*pi));};
        const auto twoPoint=xrr::thicknessFromFringePoints(angle(firstQ),angle(secondQ),synthetic.wavelength);
        check(std::abs(twoPoint.thicknessNm-trueThicknessNm)<1.0e-10,"Two-point qz thickness conversion is inaccurate");
        check(twoPoint.twoThetaDifferenceDegrees>0&&twoPoint.qDifferenceInverseAngstrom>0,"Two-point result omitted coordinate differences");
        xrr::ThicknessFftOptions ranged;ranged.twoThetaMinimum=synthetic.points[250].twoTheta;
        ranged.twoThetaMaximum=synthetic.points[1500].twoTheta;ranged.maximumThicknessNm=100;
        const auto limited=xrr::estimateThicknessFft(synthetic,ranged);
        check(limited.selectedPoints==1251&&limited.twoThetaMinimum>=*ranged.twoThetaMinimum&&limited.twoThetaMaximum<=*ranged.twoThetaMaximum,
              "Custom FFT 2theta range was not applied");

        auto withGap=synthetic;withGap.points.erase(withGap.points.begin()+850,withGap.points.begin()+950);withGap.points[850].gapBefore=true;
        const auto gapped=xrr::estimateThicknessFft(withGap,{std::nullopt,std::nullopt,100,6});
        check(gapped.missingGridPoints>0,"FFT interpolated across an explicitly unmeasured gap");
        check(std::abs(nearestPeak(gapped,trueThicknessNm)-trueThicknessNm)<.6,"A preserved measurement gap destroyed the principal FFT peak");

        const auto raw=xrr::estimateThicknessFft(xrr::readBruker(argv[1]).at(0),{std::nullopt,std::nullopt,150,8});
        const auto text=xrr::estimateThicknessFft(xrr::readBruker(argv[2]).at(0),{std::nullopt,std::nullopt,150,8});
        check(raw.selectedPoints==3604&&text.selectedPoints==3604,"Uploaded sample FFT did not use all measured points");
        check(std::abs(nearestPeak(raw,8.423)-8.423)<2.0,"Uploaded RAW FFT did not identify the thin Ru scale");
        check(std::abs(nearestPeak(raw,58.851)-58.851)<3.0,"Uploaded RAW FFT did not identify the total film-stack scale");
        check(raw.peaks.size()==text.peaks.size(),"RAW/TXT FFT peak counts differ");
        for(std::size_t i=0;i<raw.peaks.size();++i)
            check(std::abs(raw.peaks[i].thicknessNm-text.peaks[i].thicknessNm)<.05,"RAW/TXT FFT peak positions differ");

        bool reversedRejected=false;
        try{xrr::ThicknessFftOptions invalid;invalid.twoThetaMinimum=5;invalid.twoThetaMaximum=2;(void)xrr::estimateThicknessFft(synthetic,invalid);}
        catch(const std::exception&){reversedRejected=true;}
        check(reversedRejected,"Reversed FFT range was accepted");
        bool duplicateRejected=false;
        try{(void)xrr::thicknessFromFringePoints(2.0,2.0,synthetic.wavelength);}catch(const std::exception&){duplicateRejected=true;}
        check(duplicateRejected,"Identical selected points produced a thickness");

        std::cout<<"FFT tests passed. Synthetic peak "<<nearestPeak(known,trueThicknessNm)<<" nm; uploaded RAW peaks:";
        for(const auto& peak:raw.peaks)std::cout<<' '<<peak.thicknessNm;
        std::cout<<" nm\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
