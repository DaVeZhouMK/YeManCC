// Real production frame packets and native serialized bytes; no reader IO.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include "../deps/json/json.hpp"
using json=nlohmann::json;
static bool tracking=false;
static std::uint64_t allocations=0,allocatedBytes=0;
void* operator new(std::size_t n){if(tracking){++allocations;allocatedBytes+=n;}auto* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#include YMCC_FRAME_OUTPUT_HEADER
using namespace ymcc::monitorframe;
int main(int argc,char** argv){
 if(argc!=2)return 2;
 constexpr std::uint64_t now=1700000000000ULL;
 json cases=json::array(),trace=json::array(),work=json::array();
 const std::string title="Cyberpunk 2077 / \xe4\xb8\xad\xe6\x96\x87 / \"\\\n";
 auto check=[&](const char* name,bool ok){cases.push_back({{"name",name},{"ok",ok}});if(!ok)std::cerr<<name<<'\n';};
 auto record=[&](const char* label,const json& p){trace.push_back({{"label",label},{"packetBytes",p.dump()}});};
 auto bind=[&](Output& o){o.bind(2077,99,now-10000);};
 const std::size_t lengths[]={0,1,2,3,4,5,7,8,9,15,16,17,31,32,33,63,64,65,119,120};
 for(unsigned mode=0;mode<3;++mode)for(const auto n:lengths){
  Output o;bind(o);std::uint64_t at=now;
  if(n==0&&mode!=1)o.hardware(now/1000,180,101,70,30,title,now,-1,3000);
  for(std::size_t i=0;i<n;++i){
   at=now+i*(mode==0?1000:100);
   if(mode==0||mode==2&&i%10==0)o.hardware(at/1000,180,101,70,30,title,at,5.5,3000);
   if(mode!=0)o.observeRtss({true,8.333+(i%7)*0.125,120,at});
  }
  auto p=o.packet(at);record("growth-boundary",p);
  check("history count matches real observations at growth boundaries",n==0?(p.is_null()||!p.contains("frameTimes")):p.is_object()&&p["frameTimes"].size()==n);
  if(n>0){
   check("all original history samples retain order and exact durations",p["frameTimes"][0]==(mode==0?5.5:8.333)&&p["frameTimes"].back()==p["frametime"]);
   check("primary FPS stays independent of array sizing",p["fps"]==(mode==1?120:180)&&p["fpsSource"]==(mode==1?"rtss":"hwinfo-presented"));
  }
 }
 for(unsigned mode=0;mode<3;++mode){
  Output o;bind(o);allocations=allocatedBytes=0;bool values=true,bounded=true,ordering=true;
  for(std::uint64_t i=0;i<300;++i){
   const auto at=now+i*(mode==0?1000:100);
   if(mode==0||mode==2&&i%10==0)o.hardware(at/1000,180,101,70,30,title,at,5.5+(mode==0?i%17:0)*0.125,3000);
   if(mode!=0)o.observeRtss({true,8.333+(i%17)*0.125,120,at});
   tracking=true;auto p=o.packet(at);tracking=false;
   const auto count=(std::min)(i+1,std::uint64_t(120));
   bounded=bounded&&p["frameTimes"].size()==count;
   const auto first=i+1-count;
   const double expectedFirst=(mode==0?5.5:8.333)+(first%17)*0.125;
   ordering=ordering&&p["frameTimes"][0]==expectedFirst&&p["frameTimes"].back()==p["frametime"];
   values=values&&p["fps"]==(mode==1?120:180)&&p["fpsSource"]==(mode==1?"rtss":"hwinfo-presented");
   record("ordinary-or-fast-cadence",p);
  }
  check("300 current frame packets retain every real sample inside the 120 point cap",bounded);
  check("history ring rollover preserves chronological order",ordering);
  check("300 packets preserve FPS selection and do not substitute frame duration",values);
  work.push_back({{"mode",mode==0?"hardware-ordinary-1hz":mode==1?"rtss-fast-100ms":"hardware-primary-rtss-fast-100ms"},{"packets",300},{"allocations",allocations},{"allocatedBytes",allocatedBytes},{"outputObjectBytes",sizeof(o)}});
 }
 Output edge;bind(edge);
 edge.hardware(now/1000,180,101,70,30,title,now,5.5,3000);
 edge.observeRtss({true,8.333,120,now+100});edge.observeRtss({true,8.5,119,now+200});
 auto first=edge.packet(now+201);record("edge-original",first);
 auto held=edge.packet(now+1200);record("edge-missing-observation",held);
 check("missing observations do not append curve samples or renew frame deadline",held["frameTimes"]==first["frameTimes"]&&held["frameTimeTs"]==first["frameTimeTs"]&&held["frameTimeValidUntil"]==first["frameTimeValidUntil"]);
 edge.observeRtss({true,8.5,119,now+200});auto duplicate=edge.packet(now+1201);record("edge-duplicate",duplicate);
 check("duplicate observation retains exact history count",duplicate["frameTimes"].size()==2);
 edge.observeRtss({false,1,999,now+1300});auto invalid=edge.packet(now+1301);record("edge-invalid",invalid);
 check("invalid observation cannot create a new point",invalid["frameTimes"]==duplicate["frameTimes"]);
 auto expired=edge.packet(now+6000);record("edge-expired",expired);check("expired sources still remove the family",expired.is_null());
 edge.observeRtss({true,9.25,108,now+6100});auto rescued=edge.packet(now+6101);record("edge-rescued",rescued);
 check("rescue after expiry contains only the new true point",rescued["frameTimes"].size()==1&&rescued["frameTimes"][0]==9.25&&rescued["fpsSource"]=="rtss");
 check("RTSS rescue cannot renew expired hardware LOW",rescued["fps1"]==-1&&rescued["fps1ValidUntil"]==now+5000);
 edge.bind(2077,100,now+7000);record("edge-pid-reuse",edge.packet(now+7000));check("same PID new creation identity clears old history",edge.packet(now+7001).is_null());
 edge.hardware((now+8000)/1000,240,150,70,30,title,now+8000,4.166,3000);auto recovered=edge.packet(now+8001);record("edge-new-process",recovered);
 check("new process starts with one genuine hardware duration",recovered["frameTimes"].size()==1&&recovered["frametime"]==4.166&&recovered["fps"]==240&&recovered["processCreated"]=="100");
 bool ok=true;for(const auto& c:cases)ok=ok&&c["ok"].get<bool>();
 std::ofstream(argv[1])<<json({{"ok",ok},{"checks",cases.size()},{"cases",cases},{"packetComparisons",trace.size()},{"trace",trace},{"work",work},{"hardwareOrRtssReads",false},{"newTimers",false},{"timingOrCpuPercentageMeasured",false}}).dump(2)<<'\n';
 return ok?0:1;
}
