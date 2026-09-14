#include "bruker_reader.hpp"
#include "project.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs=std::filesystem;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void reject(const fs::path& p){bool yes=false;try{(void)xrr::loadProject(p);}catch(const std::exception&){yes=true;}check(yes,"Malformed project was accepted");}
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"project_tests sample.raw\n";return 1;}
    const auto temp=fs::temp_directory_path()/("xrr_project_tests_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temp);
    try {
        xrr::Project p;check(p.layers.rows.size()==1&&p.layers.rows.back().substrate,"Default stack must contain only the substrate");
        auto i=p.layers.insertBefore(0);check(i==0&&p.layers.rows.size()==2&&p.layers.rows.back().substrate,"Film insertion failed");
        p.layers.rows[0]={false,"Ru","12.37","8.0","0.35"};
        i=p.layers.insertAfter(0);p.layers.rows[i]={false,"Co80Tb20","8.6","10.0","0.45"};
        p.layers.rows.back().material="Si / SiO2";p.layers.rows.back().density="2.33";p.layers.rows.back().roughness="0.25";
        p.layers.rows[0].fitDensity=true;p.layers.rows[1].fitThickness=true;p.layers.rows.back().fitRoughness=true;
        bool belowSubstrateRejected=false;
        try{(void)p.layers.insertAfter(p.layers.rows.size()-1);}catch(const std::exception&){belowSubstrateRejected=true;}
        check(belowSubstrateRejected,"A film was inserted below Substrate");
        check(!p.layers.move(0,-1)&&p.layers.move(0,1),"Layer movement boundaries failed");
        check(p.layers.rows[0].material=="Co80Tb20"&&p.layers.rows.back().substrate,"Layer ordering or substrate invariant failed");
        check(!p.layers.erase(p.layers.rows.size()-1),"Substrate was deletable");
        check(p.layers.validationError().empty(),"Valid layer stack rejected");
        p.layers.rows.back().thickness="500";check(!p.layers.validationError().empty(),"Substrate thickness was accepted");
        p.layers.rows.back().thickness.clear();
        p.layers.rows[0].density="-1";check(!p.layers.validationError().empty(),"Negative density accepted");
        p.layers.rows[0].density="8.6";p.layers.rows[0].roughness="nan";check(!p.layers.validationError().empty(),"Nonfinite roughness accepted");
        p.layers.rows[0].roughness="0.45";check(p.layers.validationError().empty(),"Restored layer stack invalid");
        p.scans=xrr::readBruker(argv[1]);p.activeScan=0;p.view=xrr::autoView(&p.scans[0]);
        check(p.view.xmin==p.scans[0].points.front().twoTheta&&p.view.xmax==p.scans[0].points.back().twoTheta,
              "Automatic x view adds whitespace outside measured endpoints");
        p.poisson={true,0.04,2.7,1.0e-5,0.018};
        p.scans[0].secondaryWavelength=1.54439;p.scans[0].secondaryRatio=.5;
        const auto file=temp/"roundtrip.xrrproj";xrr::saveProject(file,p);const auto q=xrr::loadProject(file);
        check(q.layers.rows.size()==3&&q.layers.rows.back().substrate,"Layer project round trip failed");
        check(q.layers.rows[0].material=="Co80Tb20"&&q.layers.rows[1].material=="Ru","Layer values/order changed");
        check(q.layers.rows[0].fitThickness&&q.layers.rows[1].fitDensity&&q.layers.rows.back().fitRoughness,"Fit selections were not preserved");
        check(q.poisson.enabled&&std::abs(q.poisson.diffuseStrength-.04)<1e-12&&std::abs(q.poisson.detectorFraction-1e-5)<1e-12
              &&std::abs(q.poisson.resolutionFwhmDegrees-.018)<1e-12,
              "Poisson background model was not preserved");
        check(std::abs(q.scans[0].secondaryWavelength-1.54439)<1e-12&&std::abs(q.scans[0].secondaryRatio-.5)<1e-12,
              "Secondary wavelength metadata was not preserved");
        check(q.scans.size()==1&&q.scans[0].points.size()==3604&&q.scans[0].unmeasured==997,"Scan project round trip failed");
        check(std::abs(q.scans[0].points[100].intensity-p.scans[0].points[100].intensity)<1e-12,"Intensity changed in project");
        check(q.view.xmin==p.view.xmin&&q.view.ymax==p.view.ymax,"View changed in project");
        auto model=q.scans[0];for(auto& point:model.points)point.intensity*=.5;
        xrr::exportLayersCsv(temp/"layers.csv",q.layers);xrr::exportDataCsv(temp/"data.csv",q.scans[0],&model);
        std::ifstream layers(temp/"layers.csv",std::ios::binary),data(temp/"data.csv",std::ios::binary);
        const std::string layerText{std::istreambuf_iterator<char>(layers),{}},dataText{std::istreambuf_iterator<char>(data),{}};
        check(layerText.find("Substrate")!=std::string::npos&&layerText.find("Co80Tb20")!=std::string::npos,"Layer CSV lost values");
        check(dataText.find("-9999")==std::string::npos,"Data CSV exported unmeasured points");
        check(dataText.find("two_theta_deg,experimental_intensity,fitted_intensity\r\n")!=std::string::npos,
              "Plot-data CSV header is not the required three-column format");
        const auto firstLine=dataText.find("\r\n"),secondLine=dataText.find("\r\n",firstLine+2);
        const auto firstRecord=dataText.substr(firstLine+2,secondLine-firstLine-2);
        check(firstLine!=std::string::npos&&secondLine!=std::string::npos&&
              std::count(firstRecord.begin(),firstRecord.end(),',')==2&&firstRecord.back()!=',',
              "Plot-data CSV is not exactly three populated columns");
        bool mismatchedModelRejected=false;model.points.pop_back();
        try{xrr::exportDataCsv(temp/"bad_grid.csv",q.scans[0],&model);}catch(const std::exception&){mismatchedModelRejected=true;}
        check(mismatchedModelRejected,"A mismatched model grid was exported");
        xrr::exportDataCsv(temp/"no_model.csv",q.scans[0]);std::ifstream noModel(temp/"no_model.csv",std::ios::binary);
        const std::string noModelText{std::istreambuf_iterator<char>(noModel),{}};const auto noModelFirst=noModelText.find("\r\n");
        const auto noModelSecond=noModelText.find("\r\n",noModelFirst+2);
        const auto noModelRecord=noModelText.substr(noModelFirst+2,noModelSecond-noModelFirst-2);
        check(noModelFirst!=std::string::npos&&noModelSecond!=std::string::npos&&
              std::count(noModelRecord.begin(),noModelRecord.end(),',')==2&&noModelRecord.back()==',',
              "Unavailable model data did not leave the third CSV column blank");
        std::ofstream bad(temp/"bad.xrrproj");bad<<"XRR_STUDIO_PROJECT 1\nACTIVE 0\nVIEW 0 1 0 1\nLAYERS 1\nLAYER 0 \"bad\" \"\" \"\" \"\"\nSCANS 0\nEND_PROJECT\n";bad.close();reject(temp/"bad.xrrproj");
        std::ofstream legacy(temp/"legacy.xrrproj");legacy<<"XRR_STUDIO_PROJECT 1\nACTIVE 0\nVIEW 0 10 0 8\nLAYERS 1\nLAYER 1 \"Si\" \"2.33\" \"500\" \"0.2\"\nSCANS 0\nEND_PROJECT\n";legacy.close();
        check(xrr::loadProject(temp/"legacy.xrrproj").layers.rows.back().thickness.empty(),"Legacy substrate thickness was not cleared");
        auto badFit=q.layers;badFit.rows.back().fitThickness=true;check(!badFit.validationError().empty(),"Substrate thickness was selectable for fitting");
        std::ofstream tail(temp/"tail.xrrproj",std::ios::binary);tail<<std::ifstream(file,std::ios::binary).rdbuf()<<"garbage";tail.close();reject(temp/"tail.xrrproj");
        fs::remove_all(temp);std::cout<<"Layer-stack and project round-trip tests passed.\n";return 0;
    }catch(const std::exception& e){fs::remove_all(temp);std::cerr<<e.what()<<'\n';return 1;}
}
