#include "project.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace xrr {
namespace {
bool numeric(const std::string& s, double& value) {
    std::istringstream in(s); in.imbue(std::locale::classic());
    if (!(in>>value) || !std::isfinite(value)) return false;
    in>>std::ws; return in.eof();
}
bool blank(const std::string& s) { return s.find_first_not_of(" \t\r\n")==std::string::npos; }
void need(bool ok, const char* text) { if(!ok) throw std::runtime_error(text); }
void tag(std::istream& in, const std::string& expected) {
    std::string token; in>>token;
    if(!in || token!=expected) throw std::runtime_error("Invalid project field; expected "+expected);
}
void finiteView(const View& v) {
    need(std::isfinite(v.xmin)&&std::isfinite(v.xmax)&&std::isfinite(v.ymin)&&std::isfinite(v.ymax)
        &&v.xmax>v.xmin&&v.ymax>v.ymin&&v.ymin>=-300&&v.ymax<=300,"Invalid plot limits in project");
}
std::string quoteCsv(const std::string& value) {
    std::string r="\""; for(char c:value){if(c=='"')r+='"';r+=c;}return r+'"';
}
void endWrite(std::ofstream& out) { out.close();need(bool(out),"Could not finish writing the output file"); }
}
void LayerStack::validateStructure() const {
    need(!rows.empty()&&rows.size()<=257,"A layer stack must contain one substrate and at most 256 films");
    need(rows.back().substrate,"Substrate must be the final row");
    for(std::size_t i=0;i+1<rows.size();++i) need(!rows[i].substrate,"Only the final row may be Substrate");
}
std::size_t LayerStack::insertBefore(std::size_t selected) {
    validateStructure(); need(rows.size()<257,"Maximum of 256 film layers reached");
    selected=std::min(selected,rows.size()-1);
    rows.insert(rows.begin()+static_cast<std::ptrdiff_t>(selected),Layer{});
    return selected;
}
std::size_t LayerStack::insertAfter(std::size_t selected) {
    validateStructure();selected=std::min(selected,rows.size()-1);
    need(selected<rows.size()-1,"A film cannot be inserted below Substrate");
    return insertBefore(selected+1);
}
bool LayerStack::erase(std::size_t index) {
    validateStructure();if(index>=rows.size()-1)return false;
    rows.erase(rows.begin()+static_cast<std::ptrdiff_t>(index));return true;
}
bool LayerStack::move(std::size_t index,int direction) {
    validateStructure();
    if(index>=rows.size()-1 || (direction!=-1&&direction!=1))return false;
    if(direction<0&&index==0)return false;
    const auto next=static_cast<std::size_t>(static_cast<std::ptrdiff_t>(index)+direction);
    if(next>=rows.size()-1)return false;
    std::swap(rows[index],rows[next]);return true;
}
std::string LayerStack::cellError(std::size_t row,int column) const {
    if(row>=rows.size()||column<2||column>4)return {};
    const auto& r=rows[row];
    const std::string& text=column==2?r.density:column==3?r.thickness:r.roughness;
    if(r.substrate&&column==3&&!blank(text))return "Substrate thickness must be blank.";
    if(blank(text))return {};
    double n=0;
    if(!numeric(text,n))return "Enter a finite number, using a decimal point.";
    if(column==2&&n<=0)return "Density must be greater than zero.";
    if(column!=2&&n<0)return "Thickness and roughness must be zero or greater.";
    return {};
}
std::string LayerStack::validationError() const {
    validateStructure();
    for(std::size_t r=0;r<rows.size();++r) {
        const auto& l=rows[r];
        if(l.material.size()>4096 || l.density.size()>128 || l.thickness.size()>128 || l.roughness.size()>128)
            return "A layer entry is too long.";
        for(int c=2;c<=4;++c) {
            const auto error=cellError(r,c);
            if(!error.empty())return "Row "+std::to_string(r+1)+": "+error;
        }
        if(l.substrate&&l.fitThickness)return "Substrate thickness cannot be selected for fitting.";
    }
    return {};
}
View autoView(const Scan* s,bool nominal) {
    if(!s)return {};
    double xmin=std::numeric_limits<double>::infinity(),xmax=-xmin,ymin=xmin,ymax=-xmin;
    for(const auto& p:s->points)if(p.intensity>0) {
        xmin=std::min(xmin,p.twoTheta);xmax=std::max(xmax,p.twoTheta);
        ymin=std::min(ymin,std::log10(p.intensity));ymax=std::max(ymax,std::log10(p.intensity));
    }
    if(nominal){xmin=std::min({xmin,s->nominalStart,s->nominalEnd});xmax=std::max({xmax,s->nominalStart,s->nominalEnd});}
    if(!std::isfinite(ymin)) {
        if(std::isfinite(xmin)&&xmax>xmin)return {xmin,xmax,0,8};
        return {};
    }
    if(xmax==xmin){xmin-=0.1;xmax+=0.1;}
    ymin=std::floor(ymin);ymax=std::ceil(ymax);if(ymax==ymin){ymin-=0.5;ymax+=0.5;}
    return {xmin,xmax,ymin,ymax};
}
void saveProject(const std::filesystem::path& path,const Project& p) {
    p.layers.validateStructure();
    const auto error=p.layers.validationError();
    if(!error.empty())throw std::runtime_error(error);
    finiteView(p.view);
    need(p.scans.size()<=128,"Too many scans in one project");
    need(p.scans.empty()?p.activeScan==0:p.activeScan<p.scans.size(),"Invalid active scan");
    need(std::isfinite(p.poisson.diffuseStrength)&&p.poisson.diffuseStrength>=0&&p.poisson.diffuseStrength<=5
        &&std::isfinite(p.poisson.diffuseExponent)&&p.poisson.diffuseExponent>=0.1&&p.poisson.diffuseExponent<=10
        &&std::isfinite(p.poisson.detectorFraction)&&p.poisson.detectorFraction>=0&&p.poisson.detectorFraction<=1
        &&std::isfinite(p.poisson.resolutionFwhmDegrees)&&p.poisson.resolutionFwhmDegrees>=0&&p.poisson.resolutionFwhmDegrees<=0.2,
        "Invalid Poisson background model");
    std::ofstream out(path,std::ios::binary);need(bool(out),"Cannot create project file");
    out.imbue(std::locale::classic());out<<std::setprecision(17);
    out<<"XRR_STUDIO_PROJECT 3\nACTIVE "<<p.activeScan<<"\nVIEW "<<p.view.xmin<<' '<<p.view.xmax<<' '<<p.view.ymin<<' '<<p.view.ymax
       <<"\nPOISSON "<<(p.poisson.enabled?1:0)<<' '<<p.poisson.diffuseStrength<<' '<<p.poisson.diffuseExponent<<' '
       <<p.poisson.detectorFraction<<' '<<p.poisson.resolutionFwhmDegrees<<"\nLAYERS "<<p.layers.rows.size()<<'\n';
    for(const auto& l:p.layers.rows)
        out<<"LAYER "<<(l.substrate?1:0)<<' '<<std::quoted(l.material)<<' '<<std::quoted(l.density)<<' '
           <<std::quoted(l.thickness)<<' '<<std::quoted(l.roughness)<<' '<<(l.fitDensity?1:0)<<' '
           <<(l.fitThickness?1:0)<<' '<<(l.fitRoughness?1:0)<<'\n';
    out<<"SCANS "<<p.scans.size()<<'\n';
    for(const auto& s:p.scans) {
        out<<"SCAN "<<std::quoted(s.name)<<' '<<std::quoted(s.format)<<' '<<std::quoted(s.scanType)<<'\n';
        out<<"COUNTS "<<s.records<<' '<<s.unmeasured<<' '<<s.nonfinite<<' '<<s.nonpositive<<'\n';
        out<<"GEOMETRY "<<s.nominalStart<<' '<<s.nominalEnd<<' '<<s.step<<' '<<s.wavelength<<' '
           <<s.secondaryWavelength<<' '<<s.secondaryRatio<<'\n';
        out<<"POINTS "<<s.points.size()<<'\n';
        for(const auto& q:s.points)out<<q.twoTheta<<' '<<q.intensity<<' '<<(q.gapBefore?1:0)<<'\n';
        out<<"END_SCAN\n";
    }
    out<<"END_PROJECT\n";endWrite(out);
}
Project loadProject(const std::filesystem::path& path) {
    need(std::filesystem::file_size(path)<=256u*1024u*1024u,"Project exceeds the 256 MiB limit");
    std::ifstream in(path,std::ios::binary);need(bool(in),"Cannot open project");
    in.imbue(std::locale::classic());Project p;std::size_t count=0,totalPoints=0;unsigned version=0;
    tag(in,"XRR_STUDIO_PROJECT");in>>version;need(version>=1&&version<=3,"Unsupported project version");
    tag(in,"ACTIVE");in>>p.activeScan;
    tag(in,"VIEW");in>>p.view.xmin>>p.view.xmax>>p.view.ymin>>p.view.ymax;need(bool(in),"Invalid view");finiteView(p.view);
    if(version>=2) {
        int enabled=0;tag(in,"POISSON");in>>enabled>>p.poisson.diffuseStrength>>p.poisson.diffuseExponent>>p.poisson.detectorFraction;
        if(version>=3)in>>p.poisson.resolutionFwhmDegrees;
        need(bool(in)&&(enabled==0||enabled==1)&&std::isfinite(p.poisson.diffuseStrength)&&p.poisson.diffuseStrength>=0&&p.poisson.diffuseStrength<=5
            &&std::isfinite(p.poisson.diffuseExponent)&&p.poisson.diffuseExponent>=0.1&&p.poisson.diffuseExponent<=10
            &&std::isfinite(p.poisson.detectorFraction)&&p.poisson.detectorFraction>=0&&p.poisson.detectorFraction<=1
            &&std::isfinite(p.poisson.resolutionFwhmDegrees)&&p.poisson.resolutionFwhmDegrees>=0&&p.poisson.resolutionFwhmDegrees<=0.2,
            "Invalid Poisson background model");
        p.poisson.enabled=enabled==1;
    }
    tag(in,"LAYERS");in>>count;need(bool(in)&&count>0&&count<=257,"Invalid layer count");
    p.layers.rows.clear();
    for(std::size_t i=0;i<count;++i) {
        tag(in,"LAYER");Layer l;int substrate=0;
        in>>substrate>>std::quoted(l.material)>>std::quoted(l.density)>>std::quoted(l.thickness)>>std::quoted(l.roughness);
        if(version>=2) {
            int density=0,thickness=0,roughness=0;in>>density>>thickness>>roughness;
            need(bool(in)&&(density==0||density==1)&&(thickness==0||thickness==1)&&(roughness==0||roughness==1),"Invalid fit selection");
            l.fitDensity=density==1;l.fitThickness=thickness==1;l.fitRoughness=roughness==1;
        }
        need(bool(in)&&(substrate==0||substrate==1),"Invalid layer record");l.substrate=substrate==1;
        p.layers.rows.push_back(std::move(l));
    }
    p.layers.validateStructure();
    // v2.0 projects allowed this cell to be edited. Substrate is semi-infinite
    // in the model, so discard a legacy value rather than rejecting the file.
    p.layers.rows.back().thickness.clear();
    p.layers.rows.back().fitThickness=false;
    const auto error=p.layers.validationError();if(!error.empty())throw std::runtime_error(error);
    tag(in,"SCANS");in>>count;need(bool(in)&&count<=128,"Invalid scan count");
    for(std::size_t i=0;i<count;++i) {
        Scan s;tag(in,"SCAN");in>>std::quoted(s.name)>>std::quoted(s.format)>>std::quoted(s.scanType);
        need(bool(in)&&s.name.size()<65536&&s.format.size()<256&&s.scanType.size()<256,"Invalid scan metadata");
        tag(in,"COUNTS");in>>s.records>>s.unmeasured>>s.nonfinite>>s.nonpositive;
        need(bool(in)&&s.records<=8000000,"Invalid record counts");
        tag(in,"GEOMETRY");in>>s.nominalStart>>s.nominalEnd>>s.step>>s.wavelength;
        if(version>=3)in>>s.secondaryWavelength>>s.secondaryRatio;
        need(bool(in)&&std::isfinite(s.nominalStart)&&std::isfinite(s.nominalEnd)&&std::isfinite(s.step)&&std::isfinite(s.wavelength)
             &&std::isfinite(s.secondaryWavelength)&&s.secondaryWavelength>=0&&std::isfinite(s.secondaryRatio)&&s.secondaryRatio>=0
             &&s.secondaryRatio<=10&&(s.secondaryRatio==0||s.secondaryWavelength>0),
             "Invalid scan geometry");
        std::size_t n=0;tag(in,"POINTS");in>>n;
        need(bool(in)&&n<=8000000&&totalPoints+n<=8000000,"Project contains too many points");
        totalPoints+=n;s.points.reserve(n);
        std::size_t nonpositive=0;
        for(std::size_t j=0;j<n;++j) {
            Point q;int gap=0;in>>q.twoTheta>>q.intensity>>gap;
            need(bool(in)&&std::isfinite(q.twoTheta)&&std::isfinite(q.intensity)&&q.intensity!=-9999
                 &&(gap==0||gap==1),"Invalid measured point in project");
            q.gapBefore=gap==1;if(q.intensity<=0)++nonpositive;s.points.push_back(q);
        }
        need(nonpositive==s.nonpositive&&n+s.unmeasured+s.nonfinite==s.records,"Project record counts disagree");
        tag(in,"END_SCAN");p.scans.push_back(std::move(s));
    }
    tag(in,"END_PROJECT");in>>std::ws;need(in.eof(),"Unexpected data after project end");
    need(p.scans.empty()?p.activeScan==0:p.activeScan<p.scans.size(),"Invalid active scan");
    return p;
}
void exportLayersCsv(const std::filesystem::path& path,const LayerStack& stack) {
    const auto error=stack.validationError();if(!error.empty())throw std::runtime_error(error);
    std::ofstream out(path,std::ios::binary);need(bool(out),"Cannot create layer CSV");
    out<<"\xEF\xBB\xBF"<<"layer,material,density_g_cm3,fit_density,expected_thickness_nm,fit_thickness,roughness_nm,fit_roughness\r\n";
    for(std::size_t i=0;i<stack.rows.size();++i) {
        const auto& l=stack.rows[i];out<<quoteCsv(l.substrate?"Substrate":"Layer "+std::to_string(i+1))<<','
          <<quoteCsv(l.material)<<','<<quoteCsv(l.density)<<','<<(l.fitDensity?1:0)<<','<<quoteCsv(l.thickness)<<','
          <<(l.fitThickness?1:0)<<','<<quoteCsv(l.roughness)<<','<<(l.fitRoughness?1:0)<<"\r\n";
    }
    endWrite(out);
}
void exportDataCsv(const std::filesystem::path& path,const Scan& s,const Scan* model) {
    if(model) {
        need(model->points.size()==s.points.size(),"Model curve does not match the measured point grid");
        for(std::size_t i=0;i<s.points.size();++i)
            need(std::isfinite(model->points[i].twoTheta)&&std::abs(model->points[i].twoTheta-s.points[i].twoTheta)<=1.0e-10,
                 "Model curve does not match the measured 2theta coordinates");
    }
    std::ofstream out(path,std::ios::binary);need(bool(out),"Cannot create data CSV");
    out.imbue(std::locale::classic());out<<"\xEF\xBB\xBF"<<"two_theta_deg,experimental_intensity,fitted_intensity\r\n"<<std::setprecision(17);
    for(std::size_t i=0;i<s.points.size();++i) {
        const auto& p=s.points[i];out<<p.twoTheta<<','<<p.intensity<<',';
        if(model)out<<model->points[i].intensity;
        out<<"\r\n";
    }
    endWrite(out);
}
std::string xmlEscape(const std::string& text) {
    std::string out;
    for(char c:text)switch(c){
    case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;
    case '"':out+="&quot;";break;case '\'':out+="&apos;";break;default:out+=c;
    }
    return out;
}
}
