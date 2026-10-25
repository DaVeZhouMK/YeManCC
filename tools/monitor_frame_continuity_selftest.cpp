#include <windows.h>
#include <cassert>
#include <fstream>
#include <vector>
#include <cstring>
#include "../deps/json/json.hpp"
using json=nlohmann::json;
#include "../native/monitor_frame_output.h"
#include "C:/Program Files (x86)/RivaTuner Statistics Server/SDK/Include/RTSSSharedMemory.h"
using namespace ymcc::monitorframe;
using SdkApp=RTSS_SHARED_MEMORY::RTSS_SHARED_MEMORY_APP_ENTRY;
static_assert(offsetof(SdkApp,dwFrameTime)==offsetof(RtssAppPrefix,frameTimeUs));
static_assert(offsetof(SdkApp,dwTime0)==offsetof(RtssAppPrefix,time0));
static_assert(offsetof(SdkApp,dwStatFrames)==offsetof(RtssAppPrefix,statFrames));
static_assert(offsetof(SdkApp,dwStatFrameTimeBuf)==offsetof(RtssAppPrefix,frameBuffer));
static_assert(offsetof(SdkApp,dwStatFrameTimeBufPos)==offsetof(RtssAppPrefix,frameBufferPos));
static_assert(offsetof(SdkApp,dwStatFrameTimeBufFramerate)==offsetof(RtssAppPrefix,frameBufferFps));
static_assert(offsetof(RTSS_SHARED_MEMORY,dwAppEntrySize)==offsetof(RtssHeader,appEntrySize));
static_assert(offsetof(RTSS_SHARED_MEMORY,dwBusy)==offsetof(RtssHeader,busy));
int main(int argc,char**argv){
 assert(argc==2);json checks=json::array();auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"ok",ok}});};
 constexpr uint64_t now=1700000000000ULL;Output o;
 o.bind(77,99);o.hardware(now/1000,120,90,60,30,"game",now,8.0);
 o.hardware(now/1000+1,120,90,60,30,"game",now+1000,8.5);const auto good=o.packet(now+1000);
 check("positive hardware packet supplies genuine observation proof",good["fpsSource"]=="hwinfo-presented"&&good["fpsObservedAt"]==now+1000&&good["frameTimes"].size()==2);
 for(uint64_t gap:{1000ULL,2000ULL}){auto held=o.packet(now+1000+gap);check("two missing reads retain exact history without freshening measurement",held["frameTimes"]==good["frameTimes"]&&held["frameTimeTs"]==good["frameTimeTs"]&&held["fpsObservedAt"]==good["fpsObservedAt"]);}
 Output missing;missing.bind(77,99);missing.hardware(now/1000+1,120,90,60,30,"game",now+1000,8.5);
 check("three seconds of complete missing FPS clears once",missing.packet(now+4000).is_null());
 o.hardware(now/1000+3,121,90,60,30,"game",now+3000,8.8);auto recovered=o.packet(now+3000);
 check("recovery before expiry preserves history",recovered["frameTimes"].size()==3);
 o.observeRtss({true,5.25,120,now+3100});auto first=o.packet(now+3100);check("source warmup keeps existing usable curve",first["frameTimeSource"]=="hwinfo-presented-avg");
 o.observeRtss({true,5.5,120,now+3200});auto fast=o.packet(now+3200);check("two real RTSS samples switch to current-frame source",fast["frameTimeSource"]=="rtss-current-frame"&&fast["frametime"]==5.5&&fast["frameTimes"]==json::array({5.25,5.5}));
 for(uint64_t gap:{100ULL,1000ULL,2999ULL}){auto held=o.packet(now+3200+gap);check("RTSS misses never append fabricated points or refresh expiry",held["frameTimes"]==fast["frameTimes"]&&held["frameTimeTs"]==now+3200&&held["fpsObservedAt"]==now+3200);}
 check("stopped RTSS cache expires at three seconds",o.packet(now+6200).is_null());
 o.bind(78,100);check("process switch immediately clears both sources",o.packet(now+3300).is_null());
 o.hardware(now/1000+4,0,-1,60,30,"game",now+4000,8.0);auto zero=o.packet(now+4000);check("a valid zero FPS retains zero and suppresses curve",zero["fps"]==0&&!zero.contains("frameTimes"));
 o.bind(78,101);check("same PID reused with another creation identity clears",o.packet(now+4000).is_null());o.bind(0,0);check("explicit no game immediately clears",o.packet(now+4000).is_null());
 History h;h.observeAt(77,99,now,8);h.observeAt(77,99,now+100,9);h.observeAt(77,99,now+100,9);check("duplicate observation produces no repeated point",h.size()==2);h.observeAt(77,99,now+200,-1);check("invalid frame retains previous curve during grace",h.size()==2&&h.timestamp()==now+100);check("history expires without observing a replacement",!h.retain(77,99,now+3100)&&h.size()==0);
 const auto bytes=sizeof(RtssHeader)+sizeof(RtssAppPrefix)*2;RtssHeader hdr{0x52545353,0x20015,sizeof(RtssAppPrefix),sizeof(RtssHeader),2,0,0,0,0,0};
 check("SDK v2.5-plus layout and declared offsets accepted",rtssHeaderValid(hdr,bytes));
 for(int mode=0;mode<8;++mode){auto bad=hdr;if(mode==0)bad.signature=0xDEAD;if(mode==1)bad.version=0x30000;if(mode==2)bad.version=0x20004;if(mode==3)bad.appEntrySize=4;if(mode==4)bad.appCount=0xffffffff;if(mode==5)bad.appOffset=0xfffffffc;if(mode==6)bad.busy=1;if(mode==7)bad.appOffset+=1;check("corrupt, truncated, busy, unaligned or unsupported RTSS layout rejected",!rtssHeaderValid(bad,bytes));}
 check("one truncated byte is rejected",!rtssHeaderValid(hdr,bytes-1));
 // Unique fixture mapping, never the live RTSS mapping. Run the actual read-only
 // Windows adapter and exact process creation binding, including tick wrap.
 const auto name=L"Local\\YMCC_FrameFixture_"+std::to_wstring(GetCurrentProcessId());
 HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,static_cast<DWORD>(bytes),name.c_str());assert(mapping);auto mem=static_cast<unsigned char*>(MapViewOfFile(mapping,FILE_MAP_WRITE,0,0,bytes));assert(mem);std::memset(mem,0,bytes);std::memcpy(mem,&hdr,sizeof(hdr));auto app=reinterpret_cast<RtssAppPrefix*>(mem+hdr.appOffset);app[0].pid=99999;app[1].pid=GetCurrentProcessId();auto& current=app[1];current.time0=10000;current.time1=11000;current.frames=120;current.frameTimeUs=8333;current.frameBufferPos=10;
 FILETIME c{},e{},k{},u{};assert(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u));const auto birth=(uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
 RtssReader reader(name.c_str());const auto tick=GetTickCount64();check("first stale mapping read cannot prove a current frame",!reader.sample(GetCurrentProcessId(),birth,tick,now).valid);
 current.frameBufferPos++;current.frameTimeUs=8123;const auto actual=reader.sample(GetCurrentProcessId(),birth,tick+100,now+100);check("actual adapter selects bound PID and converts microseconds",actual.valid&&actual.milliseconds==8.123&&actual.fps==120&&actual.observedAt==now+100);
 check("unchanged producer adds no duplicate sample",!reader.sample(GetCurrentProcessId(),birth,tick+200,now+200).valid);
 reinterpret_cast<RtssHeader*>(mem)->busy=1;current.frameBufferPos++;check("busy writer is a transient miss",!reader.sample(GetCurrentProcessId(),birth,tick+300,now+300).valid);reinterpret_cast<RtssHeader*>(mem)->busy=0;
 current.time0=0xfffffff0;current.time1=0x54;current.frames=12;const auto wrapped=reader.sample(GetCurrentProcessId(),birth,tick+400,now+400);check("Windows unsigned tick wrap preserves genuine period FPS",wrapped.valid&&wrapped.fps==120);
 current.frameTimeUs=0;current.frameBufferPos++;check("zero current frame cannot fabricate milliseconds",!reader.sample(GetCurrentProcessId(),birth,tick+500,now+500).valid);
 RtssReader reused(name.c_str());check("PID reuse rejects mismatched process creation time",!reused.sample(GetCurrentProcessId(),birth+1,tick,now).valid);
 reader.reset();UnmapViewOfFile(mem);CloseHandle(mapping);
 json packets={{"hardwareGood",good},{"hardwareRecovery",recovered},{"rtssGood",fast}};
 bool ok=true;for(const auto& item:checks)ok=ok&&item["ok"].get<bool>();std::ofstream(argv[1])<<json{{"ok",ok},{"cases",checks},{"packets",packets},{"scope","Actual production Output/History/RTSS adapter with unique fixture mapping; no live game acceptance"}}.dump(2);return ok?0:1;
}
