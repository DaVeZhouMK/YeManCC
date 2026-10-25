#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <vector>
#include "__ROOT__/deps/json/json.hpp"
using json=nlohmann::json;
#define Output LegacyOutput
#include "__BROKEN__"
#undef Output
#define Output BeforeOutput
#include "__BEFORE__"
#undef Output
#include "__AFTER__"
using ymcc::monitorframe::LegacyOutput;
using ymcc::monitorframe::BeforeOutput;
using ymcc::monitorframe::Output;
static const char* phase="init";
static bool counting=false;
static std::size_t allocations=0,bytes=0,checks=0,comparisons=0;
void* operator new(std::size_t n){if(counting){++allocations;bytes+=n;}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
static void check(bool ok,const char* message){++checks;if(!ok){std::cerr<<"FAIL "<<message<<" comparison="<<comparisons<<'\n';std::exit(1);}}
static bool typed(const json&a,const json&b){
 if(a.type()!=b.type())return false;
 if(a.is_object()){if(a.size()!=b.size())return false;for(auto i=a.begin();i!=a.end();++i){auto j=b.find(i.key());if(j==b.end()||!typed(i.value(),*j))return false;}return true;}
 if(a.is_array()){if(a.size()!=b.size())return false;for(std::size_t i=0;i<a.size();++i)if(!typed(a[i],b[i]))return false;return true;}
 if(a.is_number_float()&&std::isnan(a.get<double>())&&std::isnan(b.get<double>()))return true;
 return a==b;
}
static constexpr std::uint64_t epoch=1700000000000ULL;
struct Pair {
 BeforeOutput before;Output after;
 void bind(std::uint64_t pid=2077,std::uint64_t birth=99,std::uint64_t at=epoch-10000){before.bind(pid,birth,at);after.bind(pid,birth,at);}
 void reset(){before.reset();after.reset();}
 void hardware(std::uint64_t poll,double fps=180,double low=99,double gpu=75,double power=30,const std::string&game="fixture",std::uint64_t now=epoch,double ms=5.5,std::uint64_t period=3000){phase="before.hardware";before.hardware(poll,fps,low,gpu,power,game,now,ms,period);phase="after.hardware";after.hardware(poll,fps,low,gpu,power,game,now,ms,period);}
 void metrics(std::uint64_t poll,double gpu,double power,const std::string&game,std::uint64_t now,std::uint64_t period=3000){phase="before.metrics";before.hardwareMetrics(poll,gpu,power,game,now,period);phase="after.metrics";after.hardwareMetrics(poll,gpu,power,game,now,period);}
 void invalidate(){before.invalidateHardwareMetrics();after.invalidateHardwareMetrics();}
 void rtss(std::uint64_t at,double fps=120,double ms=8.3,bool valid=true){const ymcc::monitorframe::RtssFrame frame{valid,ms,fps,at};check(before.observeRtss(frame)==after.observeRtss(frame),"RTSS observation admission matches");}
 void compare(std::uint64_t now){
  ++comparisons;phase="before.packetView";const auto a=before.packetView(now);phase="after.packetView";const auto b=after.packetView(now);check(typed(a,b),"full recursive values and JSON types match");
  bool threwA=false,threwB=false;int codeA=0,codeB=0;std::string x,y;
  try{x=a.dump();}catch(const json::exception&e){threwA=true;codeA=e.id;}
  try{y=b.dump();}catch(const json::exception&e){threwB=true;codeB=e.id;}
  check(threwA==threwB&&codeA==codeB&&x==y,"strict serialized bytes and exception identity match");
  threwA=threwB=false;x.clear();y.clear();
  try{x=before.packetTextView(now);}catch(const json::exception&e){threwA=true;codeA=e.id;}
  try{y=after.packetTextView(now);}catch(const json::exception&e){threwB=true;codeB=e.id;}
  check(threwA==threwB&&codeA==codeB&&x==y,"actual reusable serialization stays equivalent");
  check(before.needsRtssRescue(now)==after.needsRtssRescue(now),"rescue scheduling matches");
 }
};
template<class T>static json measure(const char*lane,bool fast,bool changing){
 T output;output.bind(2077,99,epoch-10000);std::size_t totalBytes=0;
 const std::string title="Cyberpunk 2077 / \xe4\xb8\xad\xe6\x96\x87 / fixed application title",changedTitle=title+" changed";
 auto step=[&](std::size_t i){const auto now=epoch+i*(fast?100ULL:1000ULL),poll=(epoch+(i*(fast?100ULL:1000ULL)/3000)*3000)/1000;
  if(!fast||i%10==0)output.hardware(poll,180,99,75,30,changing&&i%2?changedTitle:title,now,5.5,3000);
  if(fast)output.observeRtss({true,8.3,120,now});
  totalBytes+=output.packetTextView(now).size();
 };
 for(std::size_t i=0;i<4096;++i)step(i);
 allocations=bytes=0;counting=true;for(std::size_t i=4096;i<5096;++i)step(i);counting=false;
 return {{"lane",lane},{"iterations",1000},{"allocations",allocations},{"requestedBytes",bytes},{"outputBytes",totalBytes}};
}
int main(int argc,char**argv){std::set_terminate([](){try{if(auto e=std::current_exception())std::rethrow_exception(e);}catch(const std::exception&e){std::cerr<<"UNHANDLED "<<phase<<" "<<e.what()<<"\n";}std::_Exit(93);});check(argc==2,"result output path required");std::vector<std::string>cases;
 LegacyOutput brokenClock;Output fixedClock;brokenClock.bind(2077,99,epoch-10000);fixedClock.bind(2077,99,epoch-10000);
 brokenClock.hardware(epoch/1000,180,99,75,30,"fixture",epoch,5.5,3000);fixedClock.hardware(epoch/1000,180,99,75,30,"fixture",epoch,5.5,3000);
 const auto clockInitial=brokenClock.packet(epoch);check(typed(clockInitial,fixedClock.packet(epoch)),"clock reproduction begins with identical healthy packets");
 check(brokenClock.packet(epoch-1000).is_null()&&fixedClock.packet(epoch-1000).is_null(),"clock rollback correctly withdraws a future observation");
 int clockBeforeError=0;try{brokenClock.packet(epoch);}catch(const json::exception&e){clockBeforeError=e.id;}
 check(clockBeforeError==306,"frozen production class reproduces null value() failure after clock catches up");
 const auto clockRecovered=fixedClock.packet(epoch);check(typed(clockInitial,clockRecovered),"recovery rebuilds exact packet with original proofs");
 check(clockRecovered.at("fpsObservedAt")==epoch&&clockRecovered.at("fpsValidUntil")==epoch+5000,"cache recovery never renews source freshness");
 check(fixedClock.packet(epoch+5000).is_null(),"recovered cache still expires at original deadline");
 cases.push_back("original clock rollback/catch-up null-cache exception reproduced; exact valid packet restored without renewing proofs");
 Pair p;p.compare(epoch);p.bind();
 const std::string unicode="Cyberpunk 2077 / \xe4\xb8\xad\xe6\x96\x87 / \xf0\x9f\x90\x88 / \"\\\n";
 p.hardware(epoch/1000,180,99,75,30,unicode);p.compare(epoch);
 for(std::uint64_t age:{1ULL,4999ULL,5000ULL,5001ULL,6000ULL})p.compare(epoch+age);
 cases.push_back("Presented FPS proof, complete curve and exact expiry boundaries");
 p.reset();p.bind();p.rtss(epoch+100);p.compare(epoch+100);p.compare(epoch+200);
 p.metrics(epoch/1000,75,30,unicode,epoch+300);p.compare(epoch+300);
 p.hardware(epoch/1000,180,99,75,30,unicode,epoch+400);p.compare(epoch+400);
 p.rtss(epoch+500);p.compare(epoch+500);p.compare(epoch+5100);p.compare(epoch+5500);p.compare(epoch+5600);
 cases.push_back("empty-base rescue, hardware metrics arrival, RTSS/Presented transitions and rescue expiry");
 p.reset();p.bind();for(int i=0;i<20;++i){const auto now=epoch+i*1000ULL;p.hardware(now/1000,180+i,99-i,75+i%2,30+i%2,i%3?unicode:"title change",now,5+i%3,i%2?1000:3000);p.compare(now);p.invalidate();p.compare(now+1);p.metrics(now/1000,0,0,unicode,now+2);p.compare(now+2);p.metrics(now/1000,-1,-1,unicode,now+3);p.compare(now+3);}
 cases.push_back("same-poll corrections, integer/null/float transitions, metric invalidation, period and game-title changes");
 p.reset();p.bind();p.hardware(epoch/1000,180,-0.0,75,-0.0,unicode);p.compare(epoch);p.hardware((epoch+1000)/1000,180,0.0,75,0.0,unicode,epoch+1000);p.compare(epoch+1000);p.hardware((epoch+2000)/1000,180,-0.0,75,-0.0,unicode,epoch+2000);p.compare(epoch+2000);
 cases.push_back("signed zero is preserved through otherwise equal floating values");
 p.reset();p.bind();p.hardware(epoch/1000);p.rtss(epoch+100);p.compare(epoch+2000);p.compare(epoch+1);p.hardware((epoch-1000)/1000,180,99,75,30,unicode,epoch-900);p.compare(epoch-800);
 cases.push_back("wall-clock rollback and producer timeline rollback");
 p.bind(2077,100,epoch+200);p.compare(epoch+300);p.hardware(epoch/1000);p.compare(epoch+400);p.hardware((epoch+1000)/1000,100,80,0,0,unicode,epoch+1000);p.compare(epoch+1000);
 p.bind(88,116444736000000000ULL+(epoch+2000)*10000ULL,0);p.hardware((epoch+1000)/1000);p.compare(epoch+2000);p.hardware((epoch+3000)/1000,100,80,0,0,unicode,epoch+3000);p.compare(epoch+3000);
 cases.push_back("PID reuse, FILETIME birth fences and new target binding");
 for(double fps:{0.,.5,1.,999.,1000000.,1000001.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){p.reset();p.bind();p.hardware(epoch/1000,fps,0,0,0,unicode,epoch,0);p.compare(epoch);p.rtss(epoch+1,120);p.compare(epoch+1);}
 cases.push_back("zero/sub-one/large/invalid FPS, invalid frame time and genuine RTSS rescue");
 for(const auto&title:std::vector<std::string>{unicode,std::string("embedded\0null",13),std::string(30000,'x'),std::string("invalid\xff UTF-8",14)}){p.reset();p.bind();p.hardware(epoch/1000,180,99,75,30,title);p.compare(epoch);p.hardware((epoch+1000)/1000,180,99,75,30,"recovered",epoch+1000);p.compare(epoch+1000);}
 cases.push_back("Unicode, embedded NUL, oversized names, strict invalid UTF-8 and recovery");
 std::mt19937_64 random(0x594d4343ULL);p.reset();p.bind();std::uint64_t now=epoch;
 for(std::size_t i=0;i<18000;++i){const auto op=random()%12;now+=random()%350;if(op==0){p.reset();p.bind(2077,99,now>10000?now-10000:0);}else if(op==1)p.bind(2077,99+(random()%4),now);else if(op<6){const auto poll=(now-(random()%12000))/1000;const double fps=random()%5?double(random()%350):0.;p.hardware(poll,fps,random()%4?double(random()%120):-1.,random()%5?75:-1,random()%5?30:-1,i%7?unicode:"title",now,random()%4?8.3:-1.,random()%2?1000:3000);}else if(op<9)p.rtss(now-random()%1000,double(random()%200),random()%5?8.3:0.,random()%6!=0);else if(op==9)p.invalidate();else if(op==10)p.metrics(now/1000,random()%4?75:-1,random()%4?30:-1,unicode,now);else if(now>5000)now-=random()%5000;
  p.compare(now);if(i%17==0)p.compare(now+6001);
 }
 cases.push_back("18000 seeded mixed receipt/reset/identity/clock transitions with full packet comparison");
 json work=json::array();for(const auto&entry:std::vector<std::pair<bool,bool>>{{false,false},{true,false},{false,true}}){const char*lane=entry.first?"100ms RTSS with 1Hz hardware":entry.second?"1Hz changing title":"1Hz stable title";const auto a=measure<BeforeOutput>(lane,entry.first,entry.second),b=measure<Output>(lane,entry.first,entry.second);check(a.at("outputBytes")==b.at("outputBytes"),"bench emits identical output size");check(b.at("allocations").get<std::size_t>()<a.at("allocations").get<std::size_t>(),"reusing base nodes reduces allocations");check(b.at("requestedBytes").get<std::size_t>()<a.at("requestedBytes").get<std::size_t>(),"reusing base nodes reduces allocation bytes");work.push_back({{"before",a},{"after",b}});}
 check(sizeof(BeforeOutput)==sizeof(Output),"node reuse adds no per-object storage");const json result={{"ok",true},{"outputObjectBytes",sizeof(Output)},{"checks",checks},{"comparisons",comparisons},{"clockBeforeErrorId",clockBeforeError},{"cases",cases},{"work",work},{"scope","Actual frozen/current Output with inert receipts. Allocation requests are not total CPU percentage or resident memory savings; no hardware, native IPC, game, deployment or scheduling changes."}};
 std::ofstream(argv[1])<<result.dump(2);std::cout<<json({{"ok",true},{"checks",checks},{"comparisons",comparisons},{"work",work}}).dump(2)<<'\n';
}