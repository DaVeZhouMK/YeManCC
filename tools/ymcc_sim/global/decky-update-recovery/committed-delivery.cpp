// Original committed-ready consumer, selected separately from the Decky parent.
// Dispatch and source-extracted sender/consumer/timer are real; task/Win32 leaves are inert.
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include "sleep_power_state.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "sleep_power_dispatch.h"
using json=nlohmann::json;
@@POWER_ENUM@@
@@MESSAGES@@
static ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
static std::atomic<bool> g_exitRequested{false},g_inputReady{true};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_sleepPowerRedeliveryArmed{false},g_sharedPowerProjectionPending{false};
static unsigned long long fixturePowerGeneration=100,serial=1;
static HWND g_hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
static unsigned long long currentPowerGeneration(){return fixturePowerGeneration;}
// Explicit memory-only shared-power Reader port. Production sender and admission predicate stay complete.
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* output,std::uint32_t bytes) noexcept {
 if(!output||bytes<sizeof(*output))return 0;
 *output={};output->generation=fixturePowerGeneration;output->suspendSerial=fixturePowerGeneration;output->publicationRevision=1;
 output->phase=g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready?0u:3u;
 output->flags=ymcc::sleeppower::kKnown;
 if(output->phase==0&&!g_exitRequested.load(std::memory_order_acquire))output->flags|=ymcc::sleeppower::kHardwareAllowed;
 if(g_sleepPowerDispatch.pendingSuspend())output->flags|=ymcc::sleeppower::kNotificationPending;
 return 1;
}

struct Call{std::string stage;unsigned long long generation;};
static std::vector<Call> calls;
static std::vector<std::string> errors;
static std::string interruptAt,edge;
static bool changed=false,failCommit=false,failInput=false,throwLog=false;
static unsigned arms=0,inputDeliveries=0,commitPosts=0,projectionDeliveries=0;
static json resumed;
static LRESULT productionWakeCommit(WPARAM,LPARAM);
static void boundary(const char* stage,unsigned long long generation){
 calls.push_back({stage,generation});
 if(changed||interruptAt!=stage)return;
 changed=true;
 if(edge=="pending")g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
 else if(edge=="generation")++fixturePowerGeneration;
 else if(edge=="phase")g_powerLifecycle=PowerLifecycle::Resuming;
 else if(edge=="exit")g_exitRequested=true;
 else if(edge=="reentrant")productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));
 else if(edge=="reentrant-successor"){++fixturePowerGeneration;productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));}
 else if(edge=="throw")throw std::runtime_error("consumer accepted boundary then failed");
}
static void beginPowerSourceRecovery(HWND){boundary("power-source",fixturePowerGeneration);}
static void stopPowerResumeWatchdog(){boundary("resume-watchdog",fixturePowerGeneration);}
static bool steamAppearanceResumeCommitted(unsigned long long generation){boundary("steam-css",generation);return true;}
static bool deckySidebarResumeAfterIntent(unsigned long long generation,const char*){boundary("decky",generation);return true;}
static void nativeAyaLightingResume(){boundary("aya-lighting",fixturePowerGeneration);}
static void ipc_emit(const char* name,const json& value){if(std::string(name)!="power.resumed")throw std::runtime_error("unexpected event");resumed=value;boundary("power-resumed",value.at("generation").get<unsigned long long>());}
[[maybe_unused]] static void appendNativeLifecycleLog(const char* name,const json& value){if(throwLog)throw std::runtime_error("diagnostic failure");if(std::string(name)!="power-resume-consumer-failed")throw std::runtime_error("unexpected error log");errors.push_back(value.at("consumer").get<std::string>());}
static UINT_PTR fixtureSetTimer(HWND,UINT_PTR,UINT,TIMERPROC){++arms;return 1;}
static BOOL fixtureKillTimer(HWND,UINT_PTR){return TRUE;}
static BOOL fixturePostMessage(HWND,UINT message,WPARAM w,LPARAM l){
 if(message==WM_POWER_RESUME_COMMIT){if(w||static_cast<unsigned long long>(l)!=fixturePowerGeneration)throw std::runtime_error("commit identity changed");if(failCommit)return FALSE;++commitPosts;return TRUE;}
 if(message!=WM_POWER_INPUT_RECOVER||static_cast<unsigned long long>(w)!=fixturePowerGeneration||l)throw std::runtime_error("input identity changed");
 boundary("input-message",static_cast<unsigned long long>(w));return failInput?FALSE:TRUE;
}
static LRESULT fixtureSendMessage(HWND,UINT message,WPARAM w,LPARAM l){
 if(message==WM_POWER_RESUME_COMMIT)return productionWakeCommit(w,l);
 if(message==WM_POWER_INPUT_RECOVER){if(static_cast<unsigned long long>(w)!=fixturePowerGeneration||l)throw std::runtime_error("input repair relabeled");++inputDeliveries;return 0;}
 if(message==kSharedPowerStateMessage){++projectionDeliveries;return 0;}
 if(message==kSleepPowerForwardMessage){const auto receipt=g_sleepPowerDispatch.forwarded(static_cast<unsigned long long>(l),static_cast<ULONG>(w),++serial);g_sleepPowerDispatch.complete(receipt,++serial);return 0;}
 throw std::runtime_error("unexpected repair message");
}
#define PostMessageW fixturePostMessage
#define SendMessageW fixtureSendMessage
#define SetTimer fixtureSetTimer
#define KillTimer fixtureKillTimer
static void inputCaptureSensorThreadRetryPowerWake(HWND){} // Sensor owner is exercised by the original sensor-power-only selector.
static void inputCaptureSensorThreadRecoverReturned(HWND){} // Actual STA recovery executes in sleep.gyro-power power-wake.
@@DELIVERY@@
static LRESULT productionWakeCommit(WPARAM w,LPARAM l){HWND h=g_hwnd;
@@COMMIT@@
}
static LRESULT productionTimer(){HWND h=g_hwnd;WPARAM w=kSleepPowerRedeliveryTimer;
@@TIMER@@
return 0;
}
#undef PostMessageW
#undef SendMessageW
#undef SetTimer
#undef KillTimer
static std::vector<std::string> cases,failures;
static unsigned assertions=0;
static const std::vector<std::string> stages={"power-source","resume-watchdog","steam-css","decky","aya-lighting","power-resumed","input-message"};
static void require(bool value,const char* message){++assertions;if(!value)failures.push_back(cases.back()+": "+message);}
static std::vector<std::string> names(){std::vector<std::string> out;for(const auto& call:calls)out.push_back(call.stage);return out;}
static void settle(){const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPENDFAILED,++serial);g_sleepPowerDispatch.complete(receipt,++serial);}
static void reset(){settle();fixturePowerGeneration+=10;g_exitRequested=false;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_sleepPowerRedeliveryArmed=false;g_powerResumeCommitRedelivery=0;g_powerInputRecoverRedelivery=0;g_sharedPowerProjectionPending=false;calls.clear();errors.clear();resumed=json::object();interruptAt.clear();edge.clear();changed=false;failCommit=failInput=throwLog=false;arms=inputDeliveries=commitPosts=projectionDeliveries=0;}
static void normal(){productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(names()==stages,"one committed generation dispatches the original seven calls once");require(resumed.value("generation",0ull)==fixturePowerGeneration,"Ready event retains its captured generation");}
int main(int argc,char** argv){
 const std::string selected=argc==2?argv[1]:"";
 const auto run=[&](const std::string& name,const std::function<void()>& body){if(!selected.empty()&&selected!=name)return;reset();cases.push_back(name);try{body();}catch(const std::exception& e){failures.push_back(name+": unexpected exception "+e.what());}};
 run("normal-and-duplicates",[]{normal();for(unsigned i=0;i<20;++i)productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(names()==stages,"duplicate committed messages do not replay dispatched consumers");});
 run("pending-before",[]{g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});require(g_sleepPowerDispatch.pendingSuspend()&&g_powerLifecycle==PowerLifecycle::Ready,"actual Dispatch pending arrives before phase transition");productionWakeCommit(1,static_cast<LPARAM>(fixturePowerGeneration));require(calls.empty(),"pending ingress blocks committed fanout before the first consumer");require(g_powerResumeCommitRedelivery==fixturePowerGeneration&&g_sleepPowerRedeliveryArmed&&arms==1,"pending ingress retains its original committed delivery debt");for(unsigned i=0;i<20;++i)productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(calls.empty()&&arms==1,"pending storms neither dispatch nor postpone original repair");settle();productionTimer();require(names()==stages,"same-generation query failure resumes original committed debt");require(resumed.value("degraded",false),"degraded metadata survives an interrupted same-generation continuation");require(g_powerResumeCommitRedelivery==0&&!g_sleepPowerRedeliveryArmed,"completed continuation retires debt and repair timer");});
 for(const auto& stage:stages)for(const std::string change:{"pending","generation","phase","exit","reentrant","reentrant-successor"})run(stage+"-"+change,[&]{
  interruptAt=stage;edge=change;const auto old=fixturePowerGeneration;productionWakeCommit(0,static_cast<LPARAM>(old));const auto position=std::find(stages.begin(),stages.end(),stage);const std::vector<std::string> prefix(stages.begin(),position+1);
  if(change=="reentrant"){
   require(names()==stages,"reentrant duplicate cannot enter or duplicate active fanout");productionTimer();require(names()==stages,"deferred duplicate does not replay completed stages");
  }else{
   require(names()==prefix,"interrupted committed call cannot continue later consumers");
   if(change=="pending"){
    require(g_sleepPowerDispatch.pendingSuspend()&&g_powerLifecycle==PowerLifecycle::Ready,"mid-call actual pending retains original native phase");settle();productionTimer();
    require(names()==stages,"same-generation repair resumes only the undispatched suffix");
   }else if(change=="phase"){
    g_powerLifecycle=PowerLifecycle::Ready;productionWakeCommit(0,static_cast<LPARAM>(old));require(names()==stages,"same generation recommit resumes an interrupted suffix");
   }else if(change=="reentrant-successor"){
    const auto oldCalls=calls;calls.clear();productionTimer();require(names()==stages,"new generation retained behind active old callback is delivered once");require(std::all_of(calls.begin(),calls.end(),[](const auto& call){return call.generation==fixturePowerGeneration;}),"new delivery owns only its captured successor generation");require(std::all_of(oldCalls.begin(),oldCalls.end(),[&](const auto& call){return call.generation==old;}),"old callback never relabels its suffix as a new generation");
   }else if(change=="generation"){
    require(std::all_of(calls.begin(),calls.end(),[&](const auto& call){return call.generation==old;}),"retired callback never dispatches a successor's work");calls.clear();normal();
   }else {require(!g_sleepPowerRedeliveryArmed,"exit stops further delivery without a repair loop");}
  }
 });
 for(const std::string stage:{"power-source","steam-css","decky","aya-lighting","power-resumed"})run(stage+"-exception",[&]{interruptAt=stage;edge="throw";normal();require(errors==std::vector<std::string>{stage},"consumer failure is recorded without blocking independent fanout");productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(names()==stages,"uncertain accepted work is not automatically replayed");});
 run("diagnostic-exception",[]{interruptAt="steam-css";edge="throw";throwLog=true;normal();++fixturePowerGeneration;calls.clear();normal();});
 run("failed-commit-post",[]{failCommit=true;sleepPowerPostResumeCommit(g_hwnd,fixturePowerGeneration);require(g_powerResumeCommitRedelivery==fixturePowerGeneration&&calls.empty(),"original failed commit post retains delivery debt");failCommit=false;productionTimer();require(names()==stages&&g_powerResumeCommitRedelivery==0,"original timer recovers failed commit without inventing a generation");});
 run("failed-input-post",[]{failInput=true;normal();require(g_powerInputRecoverRedelivery==fixturePowerGeneration,"original failed input post retains only input transport debt");failInput=false;productionTimer();require(inputDeliveries==1&&names()==stages&&g_powerInputRecoverRedelivery==0,"input retry does not replay completed committed fanout");productionTimer();require(inputDeliveries==1,"completed input delivery is not replayed");});
 run("stale-or-exiting-debt",[]{failCommit=true;sleepPowerPostResumeCommit(g_hwnd,fixturePowerGeneration);++fixturePowerGeneration;productionTimer();require(calls.empty()&&g_powerResumeCommitRedelivery==0,"successor retires old committed debt");failCommit=false;g_exitRequested=true;productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(calls.empty(),"exit rejects a late committed message");});
 for(const auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Resuming})run("initial-phase-"+std::to_string(static_cast<unsigned>(phase)),[&]{g_powerLifecycle=phase;productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(calls.empty(),"uncommitted phases reject all fanout");g_powerLifecycle=PowerLifecycle::Ready;normal();});
 if(cases.empty()){std::cerr<<"UNKNOWN_COMMITTED_DELIVERY_CASE\n";return 2;}
 std::cout<<json{{"cases",cases},{"assertions",assertions},{"failures",failures},{"actualDispatch",true},{"actualCommittedMessageAndSenderAndTimer",true},{"nativeConsumerAlgorithmsInert",true},{"deviceOrSteamOperations",0}}.dump(2)<<'\n';return failures.empty()?0:1;
}
