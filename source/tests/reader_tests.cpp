#include "bruker_reader.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
void check(bool good,const std::string& msg){if(!good)throw std::runtime_error(msg);}
std::vector<unsigned char> bytes(const fs::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void write(const fs::path& p,const std::vector<unsigned char>& b){std::ofstream o(p,std::ios::binary);o.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));}
void writeText(const fs::path& p,const std::string& t){std::ofstream o(p,std::ios::binary);o<<t;}
std::uint32_t u32(const std::vector<unsigned char>& b,std::size_t p){return std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24);}
void put32(std::vector<unsigned char>& b,std::size_t p,std::uint32_t v){for(int i=0;i<4;++i)b[p+i]=static_cast<unsigned char>(v>>(8*i));}
void mustReject(const fs::path& p){bool rejected=false;try{(void)xrr::readBruker(p);}catch(const std::exception&){rejected=true;}check(rejected,"Malformed input was accepted: "+p.string());}
int main(int argc,char** argv){
 if(argc!=3){std::cerr<<"reader_tests sample.raw sample.txt\n";return 1;}
 fs::path temp=fs::temp_directory_path()/("xrr_reader_tests_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 fs::create_directories(temp);
 try{
  const auto a=xrr::readBruker(argv[1]),b=xrr::readBruker(argv[2]);
  check(a.size()==1&&b.size()==1,"Expected one sample range");
  for(const auto* s:{&a[0],&b[0]}){
   check(s->records==4601&&s->unmeasured==997&&s->points.size()==3604,"Sample point counts differ");
   check(s->nonpositive==0&&s->nonfinite==0,"Unexpected invalid sample points");
   check(std::abs(s->points.front().twoTheta-0.8)<1e-12,"Start angle differs");
   check(std::abs(s->points.back().twoTheta-8.006)<1e-12,"End angle differs");
  }
  double relative=0;
  for(std::size_t i=0;i<a[0].points.size();++i){
   const auto& x=a[0].points[i];const auto& y=b[0].points[i];
   check(std::abs(x.twoTheta-y.twoTheta)<1e-12,"RAW/TXT x coordinate mismatch");
   relative=std::max(relative,std::abs(x.intensity-y.intensity)/std::max(1.0,std::abs(x.intensity)));
  }
  check(relative<5e-6,"RAW/TXT intensity mismatch exceeds TXT rounding");
  check(a[0].secondaryRatio==0,"RAW reader invented an unsupported second wavelength");
  check(std::abs(b[0].secondaryWavelength-1.54439)<1e-8&&std::abs(b[0].secondaryRatio-.5)<1e-12,
        "TXT Cu Kalpha2 wavelength/intensity ratio was not preserved");
  std::cout<<"Sample agreement: 3604 points; max relative intensity error "<<relative<<'\n';
  const auto txt=temp/"gaps.txt";
  writeText(txt,"\xEF\xBB\xBF;RAW4.00\r\n[RangeHeader]\r\nSteps=6\r\nScanType=Unlocked Coupled\r\n[Data]\r\nAngle,Det1Digita,\r\n0.8,100,\r\n0.802,-9999,\r\n0.804,50,\r\n0.806,0,\r\n0.808,-2,\r\n0.810,nan,\r\n");
  const auto t=xrr::readBruker(txt)[0];
  check(t.records==6&&t.points.size()==4&&t.unmeasured==1&&t.nonfinite==1&&t.nonpositive==2,"TXT missing/nonpositive/nonfinite accounting");
  check(t.points[1].gapBefore&&std::abs(t.points[1].twoTheta-0.804)<1e-12,"TXT gap or original angle lost");
  auto raw=bytes(argv[1]);std::size_t start=61;
  while(u32(raw,start)!=0&&u32(raw,start)!=160)start+=u32(raw,start+4);
  const auto data=start+160+u32(raw,start+140);
  float sentinel=-9999;std::uint32_t bits;std::memcpy(&bits,&sentinel,4);
  auto changed=raw;put32(changed,data+4,bits);write(temp/"gap.raw",changed);
  const auto g=xrr::readBruker(temp/"gap.raw")[0];
  check(g.points.size()==3603&&g.unmeasured==998&&g.points[1].gapBefore,"RAW internal missing point not skipped");
  check(std::abs(g.points[1].twoTheta-0.804)<1e-12,"RAW angle incorrectly compacted after missing point");
  changed=raw;changed.resize(raw.size()-1);write(temp/"truncated.raw",changed);mustReject(temp/"truncated.raw");
  changed=raw;put32(changed,65,0);write(temp/"bad_segment.raw",changed);mustReject(temp/"bad_segment.raw");
  changed=raw;put32(changed,start+136,8);write(temp/"bad_record.raw",changed);mustReject(temp/"bad_record.raw");
  changed=raw;put32(changed,start+88,0xffffffff);write(temp/"huge_count.raw",changed);mustReject(temp/"huge_count.raw");
  changed=raw;std::fill(changed.begin()+start+32,changed.begin()+start+56,0);
  std::memcpy(changed.data()+start+32,"Rocking Curve",13);write(temp/"rocking.raw",changed);mustReject(temp/"rocking.raw");
  changed=raw;changed.insert(changed.end(),raw.begin()+start,raw.end());put32(changed,36,2);put32(changed,40,2);write(temp/"two_ranges.raw",changed);
  check(xrr::readBruker(temp/"two_ranges.raw").size()==2,"Multiple RAW ranges not read");
  writeText(txt,"[RangeHeader]\nSteps=3\n[Data]\nAngle,Intensity,\n1,10,\n2,20,\n");mustReject(txt);
  writeText(txt,"[RangeHeader]\nSteps=2\n[Data]\nAngle,Intensity,\n1,10,\n2,broken,\n");mustReject(txt);
  writeText(txt,"[Data]\nTheta,Intensity,\n1,10,\n");mustReject(txt);
  writeText(txt,"[RangeHeader]\nSteps=1\n[Data]\nAngle,Intensity,\n1,-9999,\n");
  const auto empty=xrr::readBruker(txt)[0];check(empty.points.empty()&&empty.unmeasured==1,"All-unmeasured range should be empty");
  writeText(temp/"old.raw","RAW1.01");mustReject(temp/"old.raw");
  writeText(txt,"[RangeHeader]\nSteps=1\n[Data]\nAngle,Intensity,\n1,10,\n[RangeHeader]\nSteps=1\n[Data]\nAngle,Intensity,\n2,20,\n");
  check(xrr::readBruker(txt).size()==2,"Multiple TXT ranges not read");
  fs::remove_all(temp);std::cout<<"All parser tests passed.\n";return 0;
 }catch(const std::exception& e){fs::remove_all(temp);std::cerr<<e.what()<<'\n';return 1;}
}
