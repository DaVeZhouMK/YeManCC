// The verifier extracts the actual production adapters. Every Windows/input
// API is fake; this test must not link USER32 or run the application/Host.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
static DWORD fakeError=777; static ULONGLONG fakeNow=1000;
static std::array<SHORT,256> asyncStates{};
static bool fakeQueueOk=true,logging=true,msi=true,rog=false,throwLog=false;
static unsigned asyncReads=0,queueReads=0;
static DWORD fakeGetLastError(){return fakeError;}
static void fakeSetLastError(DWORD x){fakeError=x;}
static ULONGLONG fakeTick(){fakeError=111;return fakeNow;}
static DWORD fakeThread(){return 42;}
static HWND fakeForeground(){return reinterpret_cast<HWND>(1);}
static DWORD fakePid(HWND,DWORD* pid){*pid=987;return 12;}
static SHORT fakeAsync(int key){++asyncReads;return asyncStates[key];}
static BOOL fakeQueue(PBYTE keys){++queueReads;if(!fakeQueueOk){fakeError=5;return FALSE;}for(unsigned i=0;i<256;++i)keys[i]=0;return TRUE;}
#define GetLastError fakeGetLastError
#define SetLastError fakeSetLastError
#define GetTickCount64 fakeTick
#define GetCurrentThreadId fakeThread
#define GetForegroundWindow fakeForeground
#define GetWindowThreadProcessId fakePid
#define GetAsyncKeyState fakeAsync
#define GetKeyboardState fakeQueue
#include "../native/keyboard_modifier_snapshot.h"
#include "../native/input_stop_signal_evidence.h"
static bool domainLogEnabled(const char*){return logging;}
static bool msiClawKnownBoardAuthority(){return msi;}
static bool ymccIsRogFamily(){return rog;}
struct {bool resolved=true;} g_machineIdentity;
static std::string g_inputHostRunId="old-run",g_inputHostLifecycle="host-active";
static unsigned long long g_inputHostEpoch=7,g_inputHostSequence=40,g_inputHostSampleSequence=99;
static bool g_inputHostActive=true,g_inputHostPrepared=true;
static HANDLE g_inputHostProcess=reinterpret_cast<HANDLE>(1);
static std::atomic<unsigned long long> g_powerGeneration{2};
static std::vector<json> emitted;
static void appendDomainLog(const char* domain,const char* event,const json& row){
    if(throwLog)throw std::runtime_error("fake log unavailable");
    if(std::string(domain)!="virtual"||std::string(event)!="input-stop-signal-evidence")std::abort();
    emitted.push_back(row);
}
#include "actual_input_stop_signal_adapter.inc"
static unsigned checks=0;
static void check(bool ok,const char* name){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
int main(){
    auto id=inputStopSignalBeginLocked("virtual-target-disabled","steamdeck",321);
    check(id && fakeError==777,"begin preserves caller error");
    asyncStates[VK_LCONTROL]=SHORT(0x8000);
    inputStopSignalPhaseLocked(id,"before-publication-revoke");
    check(fakeError==777&&emitted.back()["keyboardModifiers"]["asyncDownMask"]==1,"Ctrl observed and LastError retained");
    check(emitted.back()["stopRunId"]=="old-run"&&emitted.back()["stopEpoch"]==7,"original identity bound");
    check(!emitted.back()["consumerReleaseProven"].get<bool>(),"no release claim from snapshot");
    json idle=json::object();inputStopSignalAppendRecentLocked(idle,fakeNow);
    check(!idle.contains("recentStopSignalContext"),"incomplete stop not reported complete");
    g_inputHostActive=false;fakeNow+=10;
    inputStopSignalPhaseLocked(id,"after-quiesce","QUIESCE",1);
    check(emitted.back()["commandAccepted"]==true&&!emitted.back()["hostActive"].get<bool>(),"phase observes actual local state");
    fakeQueueOk=false;
    inputStopSignalPhaseLocked(id,"after-neutral","SUBMIT_NEUTRAL",-1);
    check(emitted.back()["commandAccepted"].is_null(),"skipped command stays unknown");
    check(emitted.back()["keyboardModifiers"]["callingThreadQueueDownMask"].is_null(),"failed queue not fabricated neutral");
    fakeQueueOk=true;
    inputStopSignalPhaseLocked(id,"after-target-release","RELEASE_TARGET",0);
    check(emitted.back()["commandAccepted"]==false,"failed release not success");
    fakeNow=2000;inputStopSignalPhaseLocked(id,"after-physical-return-attempt",nullptr,-1,true);
    check(emitted.back()["localCleanupFinished"]==true&&!emitted.back()["consumerReleaseProven"].get<bool>(),"cleanup finished is not consumer success");
    g_inputHostRunId.clear();g_inputHostEpoch=0;g_inputHostPrepared=false;g_inputHostProcess=nullptr;
    idle=json::object();inputStopSignalAppendRecentLocked(idle,3000);
    auto c=idle.at("recentStopSignalContext");
    check(c["stopRunId"]=="old-run"&&c["stopEpoch"]==7&&c["currentHostEpoch"]==0,"identity survives Host clear");
    check(c["sameStoppedSessionStillBound"]==false&&c["elapsedSinceCleanupMs"]==1000,"no cleared host conflation");
    g_inputHostRunId="new-run";g_inputHostEpoch=7;
    idle=json::object();inputStopSignalAppendRecentLocked(idle,4000);
    check(!idle["recentStopSignalContext"]["sameStoppedSessionStillBound"].get<bool>(),"same epoch new run remains distinct");
    idle=json::object();inputStopSignalAppendRecentLocked(idle,12000);check(idle.contains("recentStopSignalContext"),"10s inclusive");
    idle=json::object();inputStopSignalAppendRecentLocked(idle,12001);check(!idle.contains("recentStopSignalContext"),"bounded followup expires");
    idle=json::object();inputStopSignalAppendRecentLocked(idle,1999);check(!idle.contains("recentStopSignalContext"),"clock reversal not underflow");
    const auto readCount=asyncReads;logging=false;fakeNow=5000;
    auto off=inputStopSignalBeginLocked("disabled","steamdeck",321);inputStopSignalPhaseLocked(off,"none");
    check(off==0&&asyncReads==readCount,"disabled diagnostics do not sample");
    logging=true;msi=false;rog=false;
    check(inputStopSignalBeginLocked("other-family","xbox360",321)==0,"unreported families unchanged");
    rog=true;g_machineIdentity.resolved=false;
    check(inputStopSignalBeginLocked("unresolved","xbox360",321)==0,"unresolved ROG excluded");
    g_machineIdentity.resolved=true;
    auto next=inputStopSignalBeginLocked("new-stop","xbox360",654);
    check(next>id,"stop serial cannot collide");
    const auto oldSize=emitted.size();inputStopSignalPhaseLocked(id,"late-old-completion",nullptr,-1,true);
    check(emitted.size()==oldSize,"old completion cannot finish newer stop");
    auto failures=g_inputStopSignalEvidenceFailures.load();throwLog=true;
    inputStopSignalPhaseLocked(next,"log-throws",nullptr,-1,true);
    check(g_inputStopSignalEvidenceFailures.load()==failures+1&&fakeError==777,"diagnostic exception cannot escape control flow");
    throwLog=false;idle=json::object();inputStopSignalAppendRecentLocked(idle,5001);
    check(idle.contains("recentStopSignalContext"),"completion persists even if emission fails");
    check(idle["stopSignalDiagnosticFailures"]==failures+1,"logging loss visible later");
    // Production stop I/O itself is deliberately not executed; the verifier
    // separately checks only diagnostic insertions altered that production path.
    std::printf("INPUT_STOP_SIGNAL_PASS checks=%u liveIO=false actualAdapters=true\n",checks);
    return 0;
}
