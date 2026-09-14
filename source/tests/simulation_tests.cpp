#include "bruker_reader.hpp"
#include "project.hpp"
#include "simulation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double maximum(const xrr::Scan& scan){double v=0;for(const auto& p:scan.points)v=std::max(v,p.intensity);return v;}

int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"simulation_tests sample.raw\n";return 1;}
    try {
        auto measured=xrr::readBruker(argv[1]).at(0);
        xrr::LayerStack stack;stack.rows={
            {false,"Ru cap","12.37","8.0","0.35"},
            {false,"Co80Tb20","8.60","10.0","0.45"},
            {false,"Ru","12.37","5.0","0.30"},
            {true,"Si / native oxide","2.33","","0.25"}
        };
        const auto simulated=xrr::simulateXrr(measured,stack);
        check(simulated.points.size()==measured.points.size(),"Simulation did not preserve the imported point grid");
        for(std::size_t i=0;i<measured.points.size();++i) {
            check(simulated.points[i].twoTheta==measured.points[i].twoTheta,"Simulation changed a 2Theta coordinate");
            check(simulated.points[i].gapBefore==measured.points[i].gapBefore,"Simulation changed a missing-data gap");
            check(std::isfinite(simulated.points[i].intensity)&&simulated.points[i].intensity>=0,"Invalid simulated intensity");
        }
        check(std::abs(maximum(simulated)-maximum(measured))/maximum(measured)<1e-12,
              "Simulation maximum was not matched to measured data");
        const auto poisson=xrr::simulateXrr(measured,stack,{true,.03,1.4,1.0e-6});
        check(std::abs(maximum(poisson)-maximum(measured))/maximum(measured)<1e-12,
              "Poisson diffuse/detector model changed maximum normalization");
        double poissonDifference=0;
        for(std::size_t i=0;i<simulated.points.size();++i)poissonDifference+=std::abs(poisson.points[i].intensity-simulated.points[i].intensity);
        check(poissonDifference>maximum(measured)*1.0e-6,"Poisson diffuse/detector model had no effect");
        auto spectralMeasured=measured;spectralMeasured.secondaryWavelength=1.54439;spectralMeasured.secondaryRatio=.5;
        const auto doublet=xrr::simulateXrr(spectralMeasured,stack);
        double doubletDifference=0;
        for(std::size_t i=0;i<simulated.points.size();++i)doubletDifference+=std::abs(doublet.points[i].intensity-simulated.points[i].intensity);
        check(doubletDifference>maximum(measured)*1.0e-7,"Kalpha doublet metadata had no effect");
        const auto broadened=xrr::simulateXrr(spectralMeasured,stack,{true,0,1,0,.02});
        double resolutionDifference=0;
        for(std::size_t i=0;i<doublet.points.size();++i)resolutionDifference+=std::abs(broadened.points[i].intensity-doublet.points[i].intensity);
        check(resolutionDifference>maximum(measured)*1.0e-7,"Instrument resolution broadening had no effect");
        const auto original=simulated.points;
        stack.rows[1].thickness="13.0";const auto changed=xrr::simulateXrr(measured,stack);
        double difference=0;
        for(std::size_t i=0;i<original.size();++i)difference+=std::abs(original[i].intensity-changed.points[i].intensity);
        check(difference>maximum(measured),"Changing film thickness did not change the simulated curve");
        const auto ru=xrr::electronDensityAngstrom3("Ru",12.37);
        const auto silica=xrr::electronDensityAngstrom3("SiO2 note",2.20);
        check(ru>3.2&&ru<3.3&&silica>0.65&&silica<0.67,"Chemical-formula electron density is implausible");
        bool rejected=false;
        try{(void)xrr::electronDensityAngstrom3("NotAMaterial",1);}catch(const std::exception&){rejected=true;}
        check(rejected,"Invalid chemical formula was accepted");
        xrr::LayerStack maximumStack;maximumStack.rows.clear();
        for(int i=0;i<256;++i)maximumStack.rows.push_back({false,"Ru","12.37","0.5","0.1"});
        maximumStack.rows.push_back({true,"Si","2.33","","0.2"});
        const auto started=std::chrono::steady_clock::now();
        const auto maximumLayerResult=xrr::simulateXrr(measured,maximumStack);
        const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        check(maximumLayerResult.points.size()==measured.points.size(),"Maximum-size stack simulation failed");
        std::cout<<"Parratt simulation tests passed: "<<simulated.points.size()
                 <<" coordinates; maximum "<<maximum(simulated)<<"; 256-film benchmark "<<milliseconds<<" ms\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
