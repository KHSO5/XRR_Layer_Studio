#include "fft.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace xrr {
namespace {
constexpr double pi=3.141592653589793238462643383279502884;
constexpr std::size_t maximumResampledPoints=65536,maximumFftSize=262144;

struct Sample {
    double twoTheta=0,q=0,logIntensity=0;
    bool gapBefore=false;
};

double median(std::vector<double> values) {
    if(values.empty())return 0;
    const auto middle=values.begin()+static_cast<std::ptrdiff_t>(values.size()/2);
    std::nth_element(values.begin(),middle,values.end());const double upper=*middle;
    if(values.size()%2)return upper;
    const auto lower=std::max_element(values.begin(),middle);return (*lower+upper)*.5;
}

std::array<double,3> quadraticFit(const std::vector<double>& values,const std::vector<unsigned char>& valid) {
    double matrix[3][4]{};
    const double denominator=static_cast<double>(values.size()-1);
    for(std::size_t i=0;i<values.size();++i)if(valid[i]) {
        const double x=2.0*static_cast<double>(i)/denominator-1.0,basis[3]{1.0,x,x*x};
        for(int row=0;row<3;++row) {
            for(int column=0;column<3;++column)matrix[row][column]+=basis[row]*basis[column];
            matrix[row][3]+=basis[row]*values[i];
        }
    }
    for(int column=0;column<3;++column) {
        int pivot=column;
        for(int row=column+1;row<3;++row)if(std::abs(matrix[row][column])>std::abs(matrix[pivot][column]))pivot=row;
        if(std::abs(matrix[pivot][column])<1e-18)throw std::runtime_error("FFT detrending failed because the selected range is degenerate");
        if(pivot!=column)for(int item=column;item<4;++item)std::swap(matrix[pivot][item],matrix[column][item]);
        const double divisor=matrix[column][column];for(int item=column;item<4;++item)matrix[column][item]/=divisor;
        for(int row=0;row<3;++row)if(row!=column) {
            const double factor=matrix[row][column];for(int item=column;item<4;++item)matrix[row][item]-=factor*matrix[column][item];
        }
    }
    return {matrix[0][3],matrix[1][3],matrix[2][3]};
}

void transform(std::vector<std::complex<double>>& data) {
    const std::size_t count=data.size();
    for(std::size_t i=1,j=0;i<count;++i) {
        std::size_t bit=count>>1;
        for(;j&bit;bit>>=1)j^=bit;
        j^=bit;if(i<j)std::swap(data[i],data[j]);
    }
    for(std::size_t length=2;length<=count;length<<=1) {
        const std::complex<double> root=std::polar(1.0,-2.0*pi/static_cast<double>(length));
        for(std::size_t offset=0;offset<count;offset+=length) {
            std::complex<double> phase=1.0;
            for(std::size_t j=0;j<length/2;++j) {
                const auto even=data[offset+j],odd=data[offset+j+length/2]*phase;
                data[offset+j]=even+odd;data[offset+j+length/2]=even-odd;phase*=root;
            }
        }
    }
}

std::size_t nextPowerOfTwo(std::size_t value) {
    std::size_t result=1;
    while(result<value) {
        if(result>maximumFftSize/2)return maximumFftSize;
        result<<=1;
    }
    return result;
}

double refinedPeakIndex(const std::vector<double>& amplitude,std::size_t index) {
    if(index==0||index+1>=amplitude.size())return static_cast<double>(index);
    const double left=amplitude[index-1],center=amplitude[index],right=amplitude[index+1];
    const double denominator=left-2.0*center+right;
    if(std::abs(denominator)<1e-30)return static_cast<double>(index);
    return static_cast<double>(index)+std::clamp(.5*(left-right)/denominator,-.5,.5);
}
}

FringeSpacingThickness thicknessFromFringePoints(double firstTwoThetaDegrees,
                                                 double secondTwoThetaDegrees,
                                                 double wavelengthAngstrom) {
    if(!std::isfinite(firstTwoThetaDegrees)||!std::isfinite(secondTwoThetaDegrees))
        throw std::runtime_error("selected 2theta coordinates must be finite");
    if(!std::isfinite(wavelengthAngstrom)||wavelengthAngstrom<=0)
        throw std::runtime_error("a positive X-ray wavelength is required for thickness calculation");
    if(firstTwoThetaDegrees<0||firstTwoThetaDegrees>=180||secondTwoThetaDegrees<0||secondTwoThetaDegrees>=180)
        throw std::runtime_error("selected 2theta coordinates must be between 0 and 180 degrees");
    const auto q=[&](double twoTheta) {
        return 4.0*pi/wavelengthAngstrom*std::sin(twoTheta*pi/360.0);
    };
    const double deltaTwoTheta=std::abs(secondTwoThetaDegrees-firstTwoThetaDegrees);
    const double deltaQ=std::abs(q(secondTwoThetaDegrees)-q(firstTwoThetaDegrees));
    if(!(deltaQ>1.0e-15)||!std::isfinite(deltaQ))
        throw std::runtime_error("select two different 2theta points");
    const double thicknessNm=2.0*pi/deltaQ/10.0;
    if(!std::isfinite(thicknessNm)||thicknessNm<=0)
        throw std::runtime_error("selected points do not define a finite thickness");
    return {deltaTwoTheta,deltaQ,thicknessNm};
}

ThicknessFftResult estimateThicknessFft(const Scan& scan,const ThicknessFftOptions& options) {
    if(!std::isfinite(scan.wavelength)||scan.wavelength<=0)throw std::runtime_error("FFT requires a positive X-ray wavelength");
    if(options.twoThetaMinimum&&(!std::isfinite(*options.twoThetaMinimum)))throw std::runtime_error("FFT 2theta minimum must be finite");
    if(options.twoThetaMaximum&&(!std::isfinite(*options.twoThetaMaximum)))throw std::runtime_error("FFT 2theta maximum must be finite");
    if(options.twoThetaMinimum&&options.twoThetaMaximum&&!(*options.twoThetaMaximum>*options.twoThetaMinimum))
        throw std::runtime_error("FFT 2theta maximum must be greater than the minimum");
    if(!std::isfinite(options.maximumThicknessNm)||options.maximumThicknessNm<=0)
        throw std::runtime_error("FFT maximum displayed thickness must be positive");

    std::vector<Sample> selected;selected.reserve(scan.points.size());
    for(const auto& point:scan.points) {
        if(!std::isfinite(point.twoTheta)||!std::isfinite(point.intensity)||point.intensity<=0)continue;
        if(options.twoThetaMinimum&&point.twoTheta<*options.twoThetaMinimum)continue;
        if(options.twoThetaMaximum&&point.twoTheta>*options.twoThetaMaximum)continue;
        const double thetaRadians=point.twoTheta*pi/360.0;
        const double q=4.0*pi/scan.wavelength*std::sin(thetaRadians);
        if(std::isfinite(q))selected.push_back({point.twoTheta,q,std::log10(point.intensity),point.gapBefore});
    }
    if(selected.size()<32)throw std::runtime_error("FFT needs at least 32 positive measured points in the selected 2theta range");
    std::stable_sort(selected.begin(),selected.end(),[](const Sample& left,const Sample& right){return left.q<right.q;});
    const double qMinimum=selected.front().q,qMaximum=selected.back().q,qSpan=qMaximum-qMinimum;
    if(!(qSpan>0))throw std::runtime_error("FFT qz range is degenerate");

    std::vector<double> spacings;spacings.reserve(selected.size()-1);
    for(std::size_t i=1;i<selected.size();++i) {
        const double spacing=selected[i].q-selected[i-1].q;
        if(!selected[i].gapBefore&&spacing>0&&std::isfinite(spacing))spacings.push_back(spacing);
    }
    double qStep=median(std::move(spacings));
    if(!(qStep>0))qStep=qSpan/static_cast<double>(selected.size()-1);
    std::size_t gridCount=static_cast<std::size_t>(std::llround(qSpan/qStep))+1;
    gridCount=std::clamp(gridCount,std::size_t{32},maximumResampledPoints);
    qStep=qSpan/static_cast<double>(gridCount-1);

    std::vector<double> uniform(gridCount,0);std::vector<unsigned char> valid(gridCount,0);
    std::size_t segmentStart=0;
    const auto fillSegment=[&](std::size_t first,std::size_t last) {
        if(last<=first)return;
        const std::size_t gridFirst=static_cast<std::size_t>(std::max(0.0,std::ceil((selected[first].q-qMinimum)/qStep-1e-9)));
        const std::size_t gridLast=std::min(gridCount-1,static_cast<std::size_t>(std::floor((selected[last].q-qMinimum)/qStep+1e-9)));
        std::size_t left=first;
        for(std::size_t grid=gridFirst;grid<=gridLast;++grid) {
            const double q=qMinimum+static_cast<double>(grid)*qStep;
            while(left+1<last&&selected[left+1].q<q)++left;
            const std::size_t right=std::min(left+1,last);const double span=selected[right].q-selected[left].q;
            const double fraction=span>0?std::clamp((q-selected[left].q)/span,0.0,1.0):0.0;
            uniform[grid]=selected[left].logIntensity+(selected[right].logIntensity-selected[left].logIntensity)*fraction;valid[grid]=1;
        }
    };
    for(std::size_t i=1;i<=selected.size();++i) {
        const bool end=i==selected.size();
        const bool newSegment=!end&&(selected[i].gapBefore||selected[i].q-selected[i-1].q>3.0*qStep);
        if(end||newSegment){fillSegment(segmentStart,i-1);segmentStart=i;}
    }
    const std::size_t validGrid=static_cast<std::size_t>(std::count(valid.begin(),valid.end(),static_cast<unsigned char>(1)));
    if(validGrid<32)throw std::runtime_error("FFT has fewer than 32 continuous resampled points after preserving unmeasured gaps");

    const auto coefficients=quadraticFit(uniform,valid);std::vector<double> residuals;residuals.reserve(validGrid);
    const double denominator=static_cast<double>(gridCount-1);
    for(std::size_t i=0;i<gridCount;++i)if(valid[i]) {
        const double x=2.0*static_cast<double>(i)/denominator-1.0;
        residuals.push_back(uniform[i]-(coefficients[0]+coefficients[1]*x+coefficients[2]*x*x));
    }
    const double residualMedian=median(residuals);std::vector<double> deviations;deviations.reserve(residuals.size());
    for(double value:residuals)deviations.push_back(std::abs(value-residualMedian));
    const double robustScale=std::max(1e-9,1.4826*median(std::move(deviations))),clip=6.0*robustScale;

    std::size_t fftSize=nextPowerOfTwo(gridCount);
    while(fftSize<=maximumFftSize/2&&fftSize<gridCount*4)fftSize<<=1;
    std::vector<std::complex<double>> values(fftSize);
    for(std::size_t i=0;i<gridCount;++i)if(valid[i]) {
        const double x=2.0*static_cast<double>(i)/denominator-1.0;
        double residual=uniform[i]-(coefficients[0]+coefficients[1]*x+coefficients[2]*x*x);
        residual=std::clamp(residual,residualMedian-clip,residualMedian+clip);
        const double window=.5-.5*std::cos(2.0*pi*static_cast<double>(i)/denominator);
        values[i]=std::complex<double>((residual-residualMedian)*window,0);
    }
    transform(values);

    const double thicknessBin=2.0*pi/(static_cast<double>(fftSize)*qStep)/10.0;
    const double resolution=2.0*pi/qSpan/10.0,minimumThickness=std::max(1.0,2.0*resolution);
    const double nyquist=pi/qStep/10.0,displayMaximum=std::min(options.maximumThicknessNm,nyquist);
    if(!(displayMaximum>minimumThickness))throw std::runtime_error("FFT thickness display maximum is below the reliable resolution of this 2theta range");
    const std::size_t firstBin=std::max<std::size_t>(1,static_cast<std::size_t>(std::ceil(minimumThickness/thicknessBin)));
    const std::size_t lastBin=std::min(fftSize/2,static_cast<std::size_t>(std::floor(displayMaximum/thicknessBin)));
    if(lastBin<=firstBin+2)throw std::runtime_error("FFT selected range is too short for the requested thickness interval");
    std::vector<double> amplitude(fftSize/2+1);
    for(std::size_t i=1;i<amplitude.size();++i)amplitude[i]=std::abs(values[i]);
    const double maximum=*std::max_element(amplitude.begin()+static_cast<std::ptrdiff_t>(firstBin),
                                           amplitude.begin()+static_cast<std::ptrdiff_t>(lastBin+1));
    if(!(maximum>0)||!std::isfinite(maximum))throw std::runtime_error("FFT signal is constant after detrending");

    ThicknessFftResult result;result.twoThetaMinimum=selected.front().twoTheta;result.twoThetaMaximum=selected.back().twoTheta;
    result.qMinimumInverseAngstrom=qMinimum;result.qMaximumInverseAngstrom=qMaximum;result.qStepInverseAngstrom=qStep;
    result.minimumReliableThicknessNm=minimumThickness;result.thicknessResolutionNm=resolution;result.nyquistThicknessNm=nyquist;
    result.displayedMaximumThicknessNm=displayMaximum;result.selectedPoints=selected.size();result.resampledPoints=gridCount;
    result.missingGridPoints=gridCount-validGrid;result.fftSize=fftSize;
    result.spectrum.reserve(lastBin-firstBin+1);
    for(std::size_t i=firstBin;i<=lastBin;++i)result.spectrum.push_back({static_cast<double>(i)*thicknessBin,amplitude[i]/maximum});

    std::vector<std::size_t> candidates;
    for(std::size_t i=firstBin+1;i<lastBin;++i)
        if(amplitude[i]>=amplitude[i-1]&&amplitude[i]>amplitude[i+1]&&amplitude[i]>=maximum*.04)candidates.push_back(i);
    if(candidates.empty())candidates.push_back(static_cast<std::size_t>(std::distance(amplitude.begin(),
        std::max_element(amplitude.begin()+static_cast<std::ptrdiff_t>(firstBin),amplitude.begin()+static_cast<std::ptrdiff_t>(lastBin+1)))));
    std::stable_sort(candidates.begin(),candidates.end(),[&](std::size_t left,std::size_t right){return amplitude[left]>amplitude[right];});
    const double separation=std::max(1.0,2.0*resolution);
    for(std::size_t index:candidates) {
        const double thickness=refinedPeakIndex(amplitude,index)*thicknessBin;
        const bool separated=std::all_of(result.peaks.begin(),result.peaks.end(),[&](const ThicknessPeak& peak){return std::abs(peak.thicknessNm-thickness)>=separation;});
        if(separated)result.peaks.push_back({thickness,amplitude[index]/maximum});
        if(result.peaks.size()>=options.maximumPeaks)break;
    }
    return result;
}

}
