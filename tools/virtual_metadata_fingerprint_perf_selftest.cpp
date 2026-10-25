// R23 standalone source-extracted metadata fingerprints and original delta builders.
#define NOMINMAX
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
#define NOINLINE __declspec(noinline)
struct Count {uint64_t calls=0,bytes=0;};
static thread_local bool counting=false;static thread_local Count count;
static void account(size_t n){if(counting){++count.calls;count.bytes+=n;}}
void* operator new(size_t n){auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();account(n);return p;}
void* operator new[](size_t n){return ::operator new(n);}void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}void operator delete(void*p,size_t)noexcept{std::free(p);}void operator delete[](void*p,size_t)noexcept{std::free(p);}
void* operator new(size_t n,std::align_val_t a){auto p=_aligned_malloc(n?n:1,size_t(a));if(!p)throw std::bad_alloc();account(n);return p;}
void* operator new[](size_t n,std::align_val_t a){return ::operator new(n,a);}void operator delete(void*p,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void*p,std::align_val_t)noexcept{_aligned_free(p);}void operator delete(void*p,size_t,std::align_val_t)noexcept{_aligned_free(p);}void operator delete[](void*p,size_t,std::align_val_t)noexcept{_aligned_free(p);}
struct Selector {
 std::string dmiManufacturer="Micro-Star International Co., Ltd.",dmiProduct="MS-1T91",dmiSystemProduct="Claw 8 AI+ fixture";
 std::string selectorId="fixture-cg3em-selector",deviceClass="HandheldCompanion.Devices.ClawCG3EM";
 std::string gyroMatrixId="fixture-gyro-matrix",accelMatrixId="fixture-accel-matrix",matrixSourceSha256=std::string(64,'a');bool resolved=true;
};
struct Config {bool standaloneTestLane=false,enabled=true,virtualTargetEnabled=true,targetGyroEnabled=true,motionActionEnabled=false,motionEnabled=true;std::string motionMode="player-space",motionTrigger="always",targetStick="right";};
struct Meta {std::string gyroFamily="sensorapi",accelFamily="sensorapi";bool gyroHcLegacyFields=true,accelHcLegacyFields=true;};
struct Machine {std::string deviceClass="HandheldCompanion.Devices.ClawCG3EM",product="MS-1T91",gyroMatrixId="fixture-gyro-matrix",accelMatrixId="fixture-accel-matrix";};
struct Context {Selector selector;Config config;Meta meta;Machine machine;std::string persona="steamdeck",gyroProvider="fixture-sensorapi-gyro",accelProvider="fixture-sensorapi-accel";};
struct State {std::string selector,config;};
static thread_local const Context* activeContext=nullptr;
static thread_local uint64_t personaReads=0,receiptReads=0;
struct Active {const Context* previous;explicit Active(const Context& c):previous(activeContext){activeContext=&c;}~Active(){activeContext=previous;}};
static std::string inputHostConfiguredPersona(){++personaReads;return activeContext->persona;}
#include "generated.hpp"
static uint64_t assertions=0,sink=0;
static void check(bool ok,const char* why){++assertions;if(!ok)throw std::runtime_error(why);}
static bool exact(const json&a,const json&b){if(a.type()!=b.type()||a.size()!=b.size())return false;if(a.is_number_float())return std::bit_cast<uint64_t>(a.get<double>())==std::bit_cast<uint64_t>(b.get<double>());if(a.is_object()||a.is_array()){auto x=a.begin(),y=b.begin();for(;x!=a.end();++x,++y)if((a.is_object()&&x.key()!=y.key())||!exact(x.value(),y.value()))return false;return true;}return a==b;}
static std::string wire(const json& r){try{return "ok:"+r.dump();}catch(const json::exception&e){return "error:"+std::to_string(e.id);}}
static void pair(const Context& c,State& a,State& b){
 const auto fp=baselineFingerprints(c),fc=candidateFingerprints(c);check(fp==fc,"fingerprint bytes changed");
 personaReads=receiptReads=0;auto x=baselineDelta(c,a);auto pr=personaReads,rr=receiptReads;
 personaReads=receiptReads=0;auto y=candidateDelta(c,b);
 check(pr==personaReads&&pr==1,"persona snapshot count changed");check(rr==receiptReads,"receipt call frequency changed");
 check(a.selector==b.selector&&a.config==b.config,"delta state changed");
 check(exact(x,y),"recursive delta value changed");check(wire(x)==wire(y),"delta output/serialization error changed");
}
static const std::array<std::string Selector::*,8> selectorFields{&Selector::dmiManufacturer,&Selector::dmiProduct,&Selector::dmiSystemProduct,&Selector::selectorId,&Selector::deviceClass,&Selector::gyroMatrixId,&Selector::accelMatrixId,&Selector::matrixSourceSha256};
static const std::array<std::string Config::*,3> configFields{&Config::motionMode,&Config::motionTrigger,&Config::targetStick};
static const std::array<bool Config::*,6> configFlags{&Config::standaloneTestLane,&Config::enabled,&Config::virtualTargetEnabled,&Config::targetGyroEnabled,&Config::motionActionEnabled,&Config::motionEnabled};
static void correctness(){
 Context base;State a,b;pair(base,a,b);pair(base,a,b);
 for(auto f:selectorFields){auto c=base;c.selector.*f+="changed";pair(c,a,b);pair(c,a,b);pair(base,a,b);}
 for(auto f:configFields){auto c=base;c.config.*f+="changed";pair(c,a,b);pair(c,a,b);pair(base,a,b);}
 for(auto f:configFlags){auto c=base;c.config.*f=!(c.config.*f);pair(c,a,b);pair(base,a,b);}
 std::vector<size_t> lengths{0,1,14,15,16,30,31,32,63,64,127,128,255,256,1024,4096};
 for(auto n:lengths){auto c=base;for(auto f:selectorFields)c.selector.*f=std::string(n,'S');for(auto f:configFields)c.config.*f=std::string(n,'C');c.persona=std::string(n,'P');c.gyroProvider=std::string(n,'G');c.accelProvider=std::string(n,'A');pair(c,a,b);pair(c,a,b);}
 // Delimiters and embedded NUL remain bytes, including preexisting separator collisions.
 for(auto text:std::vector<std::string>{"微星😀\n\t\"\\",std::string("x\0y",3),std::string(1,'\x1f'),std::string("\xc3\x28",2)}){
  auto c=base;for(auto f:selectorFields)c.selector.*f=text;for(auto f:configFields)c.config.*f=text;c.persona=text;c.gyroProvider=text;c.accelProvider=text;pair(c,a,b);pair(c,a,b);
 }
 auto one=base;one.selector.dmiManufacturer="A";one.selector.dmiProduct="B\x1f" "C";
 auto two=one;two.selector.dmiManufacturer="A\x1f" "B";two.selector.dmiProduct="C";
 pair(one,a,b);pair(two,a,b);pair(one,a,b);
 check(baselineFingerprints(one).first==baselineFingerprints(two).first,"fixture collision missing");
 // The existing recipe can change without a fingerprint change; preserve that omission.
 auto receiptOnly=base;pair(base,a,b);receiptOnly.meta.gyroFamily="winrt";pair(receiptOnly,a,b);
 for(unsigned n=0;n<64;++n){auto c=base;c.meta.gyroHcLegacyFields=(n&1)!=0;c.meta.accelHcLegacyFields=(n&2)!=0;c.selector.resolved=(n&4)!=0;c.meta.gyroFamily=n&8?"winrt":"sensorapi";c.meta.accelFamily=n&16?"winrt":"sensorapi";c.machine.product=n&32?"unknown":"MS-1T91";pair(c,a,b);}
 std::mt19937_64 rng(0x20261015);
 for(unsigned n=0;n<5000;++n){auto c=base;auto& selected=c.selector.*selectorFields[rng()%selectorFields.size()];selected+=std::to_string(rng());c.config.*configFlags[rng()%configFlags.size()]=(rng()&1)!=0;c.meta.gyroHcLegacyFields=(rng()&1)!=0;c.meta.accelHcLegacyFields=(rng()&1)!=0;c.persona=n%3?"steamdeck":"dualsense";pair(c,a,b);if(n%9==0)pair(c,a,b);if(n%31==0){a={};b={};pair(c,a,b);}}
}
using Delta=json(*)(const Context&,State&);using Fingerprint=std::pair<std::string,std::string>(*)(const Context&);
struct Measurement{double ns;Count allocation;};
static std::vector<Context> prepared(const Context& base,unsigned mode){
 std::vector<Context> v(32,base);
 for(unsigned n=0;n<v.size();++n){if(mode&1)v[n].selector.dmiProduct=(n&1)?"MS-1T91-fixture-A":"MS-1T91-fixture-B";if(mode&2)v[n].config.motionMode=(n&1)?"world-space":"player-space";}
 return v;
}
static Measurement measure(const Context& c,unsigned mode,bool candidate,bool fp,unsigned batches,bool alloc){
 double elapsed=0;Count total{};State state;Delta df=candidate?candidateDelta:baselineDelta;Fingerprint ff=candidate?candidateFingerprints:baselineFingerprints;
 {auto seed=df(c,state);sink+=seed.size();}
 for(unsigned i=0;i<batches;++i){auto rows=prepared(c,mode);count={};counting=alloc;auto start=std::chrono::steady_clock::now();
  for(const auto& row:rows){if(fp){auto pair=ff(row);sink+=pair.first.size()+pair.second.size();}else{auto rec=df(row,state);sink+=rec.size();}}
  elapsed+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count();counting=false;total.calls+=count.calls;total.bytes+=count.bytes;
 }
 return{elapsed/(32*batches),total};
}
static double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
static json formal(const Context& c,const char* name,unsigned mode,bool fp,int campaign){
 measure(c,mode,false,fp,6,false);measure(c,mode,true,fp,6,false);
 std::vector<double> before,after,paired;unsigned wins=0;
 for(unsigned n=0;n<11;++n){double x,y;if(((n+campaign)&1)==0){x=measure(c,mode,false,fp,24,false).ns;y=measure(c,mode,true,fp,24,false).ns;}else{y=measure(c,mode,true,fp,24,false).ns;x=measure(c,mode,false,fp,24,false).ns;}before.push_back(x);after.push_back(y);paired.push_back((x-y)/x);wins+=y<x;}
 auto ba=measure(c,mode,false,fp,2,true).allocation,ca=measure(c,mode,true,fp,2,true).allocation;
 double pm=median(paired),ratio=(median(before)-median(after))/median(before);bool lower=ca.calls<ba.calls&&ca.bytes<ba.bytes;
 return{{"name",name},{"mode",mode},{"scope",fp?"fingerprints":"full-metadata-delta"},{"rounds",11},{"fasterRounds",wins},{"pairedMedianReduction",pm},{"ratioOfMediansReduction",ratio},{"beforeMedianNs",median(before)},{"afterMedianNs",median(after)},{"beforeAllocationRequests",ba.calls/64.0},{"afterAllocationRequests",ca.calls/64.0},{"beforeRequestedBytes",ba.bytes/64.0},{"afterRequestedBytes",ca.bytes/64.0},{"allocationsLower",lower},{"beforeSamplesNs",before},{"afterSamplesNs",after},{"pass",wins>=10&&pm>=.05&&ratio>=.05&&lower}};
}
int main(int argc,char**argv){try{
 check(argc>=2,"mode missing");correctness();
 if(std::string(argv[1])=="verify"){std::cout<<json({{"pass",true},{"assertions",assertions},{"fixtureOnly",true},{"physicalOrProductExecuted",false}}).dump(2)<<'\n';return 0;}
 check(std::string(argv[1])=="benchmark"&&argc==3,"invalid benchmark args");int campaign=std::stoi(argv[2]);
 Context base,unicode;unicode=base;unicode.selector.dmiManufacturer="微星设备能力库-UTF8-fixture";unicode.selector.selectorId="selector-微星-😀-fixture";unicode.config.motionMode="模式-player-space-fixture";unicode.gyroProvider="sensorapi-微星-gyro-fixture";
 json rows=json::array();bool pass=true;
 for(auto shape:std::vector<std::pair<const char*,Context>>{{"synthetic-CG3EM-shape",base},{"synthetic-Unicode-shape",unicode}})for(unsigned mode=0;mode<4;++mode)for(bool fp:{true,false}){auto r=formal(shape.second,shape.first,mode,fp,campaign);pass=pass&&r.at("pass").get<bool>();rows.push_back(std::move(r));}
 std::cout<<json({{"pass",pass},{"campaign",campaign},{"correctnessAssertions",assertions},{"rows",rows},{"wholeProcessClaim",false}}).dump(2)<<'\n';return pass?0:2;
 }catch(const std::exception&e){counting=false;std::cerr<<e.what()<<'\n';return 1;}}
