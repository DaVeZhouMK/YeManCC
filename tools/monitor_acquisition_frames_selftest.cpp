#include <iostream>
#include <fstream>
#include "../deps/json/json.hpp"
using json=nlohmann::json;
#ifndef YMCC_FRAME_OUTPUT_HEADER
#define YMCC_FRAME_OUTPUT_HEADER "../native/monitor_frame_output.h"
#endif
#include YMCC_FRAME_OUTPUT_HEADER
using namespace ymcc::monitorframe;
int main(int argc,char**argv){
 if(argc!=2)return 2;json cases=json::array(),packets=json::object();
 auto check=[&](const char*name,bool ok){cases.push_back({{"name",name},{"ok",ok}});if(!ok)std::cerr<<name<<"\n";};
 constexpr std::uint64_t now=1700000000000ULL;
 Output rtss;rtss.bind(2077,99,now-10000);
 check("without either producer FPS family remains unavailable",rtss.packet(now).is_null());
 check("RTSS measurement alone is admitted through the existing output",rtss.observeRtss({true,8.3,120,now}));
 auto p=rtss.packet(now+1);
 check("RTSS-only packet has genuine FPS and source",p.is_object()&&p["fps"]==120&&p["fpsSource"]=="rtss");
 check("RTSS-only packet does not fabricate hardware 1-percent-low",p["fps1"]==-1&&!p.contains("fps1ObservedAt"));
 rtss.observeRtss({true,8.4,119,now+100});p=rtss.packet(now+101);
 check("RTSS-only curve uses actual observed durations",p["frameTimeSource"]=="rtss-current-frame"&&p["frameTimes"].size()==2&&p["frametime"]==8.4);
 packets["rtss-only"]=p;
 const auto deadline=p["fpsValidUntil"].get<std::uint64_t>();
 check("rereading the packet cannot extend the producer deadline",rtss.packet(deadline-1)["fpsValidUntil"]==deadline);
 check("RTSS-only family expires without fabricated continuity",rtss.packet(deadline).is_null());
 Output off;off.bind(2077,99,now-10000);off.hardware(now/1000,120,82,60,30,"fixture",now,8.3,3000);
 auto before=off.packet(now+1);check("prior valid hardware LOW is genuine",before["fps1"]==82&&before["fps1Source"]=="hwinfo-presented-1percent-low");
 const auto lowEnd=before["fps1ValidUntil"].get<std::uint64_t>();
 off.observeRtss({true,8.3,120,lowEnd+1});auto after=off.packet(lowEnd+2);
 check("missing hardware LOW cannot renew through RTSS",after["fps1"]==-1&&after["fps1ValidUntil"]==lowEnd&&after["fpsSource"]=="rtss");
 packets["hardware-missing"]=after;
 off.bind(88,100,lowEnd+10);check("new game does not inherit previous static producer proof",off.packet(lowEnd+11).is_null());
 Output mixed;mixed.bind(2077,99,now-10000);mixed.hardware(now/1000,180,101,70,30,"fixture",now,5.5,3000);
 mixed.observeRtss({true,11.1,90,now+100});mixed.observeRtss({true,11.2,89,now+200});
 auto priority=mixed.packet(now+201);
#ifdef YMCC_BEFORE_PRIORITY
 check("frozen baseline lets RTSS replace genuine presented FPS",priority["fps"]==89&&priority["fpsSource"]=="rtss");
#else
 check("genuine presented FPS including generated frames takes priority",priority["fps"]==180&&priority["fpsSource"]=="hwinfo-presented");
 check("prioritizing FPS does not discard fast real RTSS frame-time samples",priority["frameTimeSource"]=="rtss-current-frame"&&priority["frametime"]==11.2);
 check("prioritizing FPS retains genuine hardware LOW provenance",priority["fps1"]==101&&priority["fps1Source"]=="hwinfo-presented-1percent-low");
#endif
 packets["dual-source"]=priority;
 const auto expires=priority["fpsValidUntil"].get<std::uint64_t>();
 mixed.observeRtss({true,11.3,88,expires+1});auto rescue=mixed.packet(expires+2);
 check("RTSS rescues FPS after the hardware proof expires",rescue["fps"]==88&&rescue["fpsSource"]=="rtss");
 const auto resumedPoll=(expires+2000)/1000;
 mixed.hardware(resumedPoll,200,110,70,30,"fixture",resumedPoll*1000,5,3000);mixed.observeRtss({true,12.5,80,resumedPoll*1000+50});
 auto recovered=mixed.packet(resumedPoll*1000+51);
#ifdef YMCC_BEFORE_PRIORITY
 check("frozen baseline still prefers RTSS after hardware recovers",recovered["fps"]==80);
#else
 check("fresh hardware FPS automatically resumes primary ownership",recovered["fps"]==200&&recovered["fpsSource"]=="hwinfo-presented");
#endif
 Output zero;zero.bind(2077,99,now-10000);zero.hardware(now/1000,0,0,70,30,"fixture",now,0,3000);zero.observeRtss({true,8.3,120,now+100});
 check("below-one hardware FPS still permits genuine RTSS rescue",zero.packet(now+101)["fps"]==120);
 bool ok=true;for(const auto&c:cases)ok=ok&&c["ok"].get<bool>();
 std::ofstream(argv[1])<<json({{"ok",ok},{"checks",cases.size()},{"cases",cases},{"packets",packets},{"realSensorSampling",false}}).dump(2);return ok?0:1;
}
