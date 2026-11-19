#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
static DWORD fakeError=777;static unsigned asyncCalls=0,queueCalls=0,foregroundCalls=0;
static bool queueOk=true,foregroundChanges=false;static DWORD queueFailure=5;
static std::array<SHORT,256> asyncStates{};static std::array<BYTE,256> queueStates{};
static DWORD fakeGetLastError(){return fakeError;}
static void fakeSetLastError(DWORD v){fakeError=v;}
static ULONGLONG fakeTick(){fakeError=101;return 1000+foregroundCalls;}
static DWORD fakeThread(){fakeError=102;return 42;}
static HWND fakeForeground(){++foregroundCalls;fakeError=103;return reinterpret_cast<HWND>(std::uintptr_t(foregroundChanges&&foregroundCalls==2?2:1));}
static DWORD fakePid(HWND h,DWORD* pid){*pid=h==reinterpret_cast<HWND>(1)?123:456;fakeError=104;return 55;}
static SHORT fakeAsync(int key){++asyncCalls;fakeError=105;return asyncStates[key];}
static BOOL fakeQueue(PBYTE out){++queueCalls;if(!queueOk){fakeError=queueFailure;return FALSE;}for(unsigned i=0;i<256;++i)out[i]=queueStates[i];fakeError=106;return TRUE;}
#define GetLastError fakeGetLastError
#define SetLastError fakeSetLastError
#define GetTickCount64 fakeTick
#define GetCurrentThreadId fakeThread
#define GetForegroundWindow fakeForeground
#define GetWindowThreadProcessId fakePid
#define GetAsyncKeyState fakeAsync
#define GetKeyboardState fakeQueue
#include "../native/keyboard_modifier_snapshot.h"
static unsigned checks=0,cases=0;
static void check(bool v,const char* label){++checks;if(!v){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
static void reset(){fakeError=777;asyncCalls=queueCalls=foregroundCalls=0;queueOk=true;foregroundChanges=false;asyncStates.fill(0);queueStates.fill(0);}
int main(int argc,char** argv){
 using namespace ymcc::keyboardEvidence;
 if(argc==2&&std::string(argv[1])=="--replay"){
    for(std::string line;std::getline(std::cin,line);){
        const auto old=nlohmann::json::parse(line);
        Snapshot sample;
        sample.beginTick=old.at("captureBeginTick").get<ULONGLONG>();sample.endTick=old.at("captureEndTick").get<ULONGLONG>();
        sample.threadId=old.at("callingThreadId").get<DWORD>();
        sample.foregroundPidBefore=old.at("foregroundPidBefore").get<DWORD>();sample.foregroundPidAfter=old.at("foregroundPidAfter").get<DWORD>();
        sample.foregroundAfter=old.at("foregroundStableDuringCapture").get<bool>()?nullptr:reinterpret_cast<HWND>(1);
        sample.asyncDownMask=old.at("asyncDownMask").get<std::uint16_t>();
        sample.queueReadOk=old.at("callingThreadQueueReadOk").get<bool>();
        if(sample.queueReadOk)sample.callingThreadQueueDownMask=old.at("callingThreadQueueDownMask").get<std::uint16_t>();
        else sample.queueError=old.at("callingThreadQueueError").get<DWORD>();
        const auto compact=toPeriodicJson(sample);
        check(standaloneFromIdleSnapshot(compact)==old,"historical snapshot round trip");
        std::cout<<compact.dump()<<'\n';
    }
    return std::cin.bad()||!std::cout?1:0;
 }
 if(argc!=1)return 2;
 reset();auto s=read();auto j=toJson(s);++cases;
 check(asyncCalls==8&&queueCalls==1,"bounded existing-tick reads");check(fakeError==777,"Win32 error preserved");
 check(s.asyncDownMask==0&&s.queueReadOk,"all-up samples");check(j["asyncZeroIsAbsenceProof"]==false&&j["eventEdgesCaptured"]==false,"absence and edge limits");
 reset();asyncStates[VK_LCONTROL]=-32767;s=read();j=toJson(s);++cases;
 check(s.asyncDownMask==1&&j["modifiers"]["leftCtrl"]["asyncDownBit"]==true,"left Ctrl down");check(j["modifiers"]["rightCtrl"]["asyncDownBit"]==false,"side distinction");
 reset();asyncStates[VK_RCONTROL]=1;s=read();++cases;check(s.asyncDownMask==0,"low bit ignored");
 reset();asyncStates[VK_LCONTROL]=-32768;queueStates[VK_RCONTROL]=0x80;s=read();j=toJson(s);++cases;
 check(s.asyncDownMask==1&&s.callingThreadQueueDownMask==2,"queue and async not merged");check(j["sourceAttribution"]=="unobserved","no injected/physical inference");
 reset();queueOk=false;s=read();j=toJson(s);++cases;
 check(!s.queueReadOk&&s.queueError==5,"queue failure recorded");check(j["callingThreadQueueDownMask"].is_null()&&j["modifiers"]["leftCtrl"]["callingThreadQueueDown"].is_null(),"unknown queue is not zero");check(fakeError==777,"error preserved on failure");
 reset();foregroundChanges=true;s=read();j=toJson(s);++cases;
 check(j["foregroundStableDuringCapture"]==false&&s.foregroundPidBefore==123&&s.foregroundPidAfter==456,"foreground change preserved");check(j["crossKeyAtomicSnapshot"]==false,"no atomic claim");
 reset();for(const int key:modifierKeys){asyncStates[key]=-32768;queueStates[key]=0x80;}s=read();j=toJson(s);++cases;
 check(s.asyncDownMask==255&&s.callingThreadQueueDownMask==255,"all eight modifiers distinct");check(j["modifiers"].size()==8,"modifier-only schema");check(j["gameOrSteamKeyboardReceipt"]=="unobserved","no consumer inference");
 ++cases;
 for(unsigned bit=0;bit<modifierKeys.size();++bit){
    reset();
    asyncStates[modifierKeys[bit]]=-32768;queueStates[modifierKeys[7-bit]]=0x80;
    s=read();const auto compact=toPeriodicJson(s);
    check(compact.at("schemaVersion")==2&&compact.size()==11&&!compact.contains("modifiers"),"periodic compact field set");
    check(compact.at("asyncDownMask")==1u<<bit&&compact.at("callingThreadQueueDownMask")==1u<<(7-bit),"side and API bit order");
    check(standaloneFromIdleSnapshot(compact)==toJson(s),"selected receipt recovers v1 from same capture");
    check(asyncCalls==8&&queueCalls==1&&foregroundCalls==2,"serialization never reads the keyboard or foreground again");
 }
 ++cases;
 reset();queueOk=false;queueFailure=0;foregroundChanges=true;s=read();const auto unavailable=toPeriodicJson(s);
 check(unavailable.at("callingThreadQueueReadOk")==false&&unavailable.at("callingThreadQueueError")==0&&unavailable.at("callingThreadQueueDownMask").is_null(),"failed read with no Win32 code is not a neutral queue");
 check(unavailable.at("captureBeginTick")==1000&&unavailable.at("captureEndTick")==1002&&unavailable.at("callingThreadId")==42,"per-sample clock and thread retained");
 check(unavailable.at("foregroundStableDuringCapture")==false&&unavailable.at("foregroundPidBefore")==123&&unavailable.at("foregroundPidAfter")==456,"per-sample foreground context retained");
 check(standaloneFromIdleSnapshot(unavailable)==toJson(s),"selected receipt preserves unavailable queue");
 s.foregroundPidAfter=s.foregroundPidBefore;
 check(toPeriodicJson(s).at("foregroundStableDuringCapture")==false,"same PID does not imply same foreground window");
 reset();s=read();const auto neutral=toPeriodicJson(s);
 check(neutral.at("callingThreadQueueReadOk")==true&&neutral.at("callingThreadQueueDownMask")==0&&neutral.at("callingThreadQueueError").is_null(),"observed zero queue is not unavailable");
 check(standaloneFromIdleSnapshot(toJson(s))==toJson(s),"legacy standalone context remains readable");
 std::printf("KEYBOARD_MODIFIER_SNAPSHOT_PASS cases=%u checks=%u fakeWindowsOnly=true liveKeysRead=false\n",cases,checks);return 0;
}
