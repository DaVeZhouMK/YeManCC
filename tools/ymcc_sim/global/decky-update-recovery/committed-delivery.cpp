// Original committed-ready consumer, selected separately from the Decky parent.
// Dispatch and source-extracted sender/consumer/timer are real; task/Win32 leaves are inert.
#define NOMINMAX
#include <Windows.h>
#include <atomic>
#include "sleep_power_state.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "sleep_power_dispatch.h"
#include "recovery_resume_commit.h"
using json=nlohmann::json;
@@POWER_ENUM@@
@@MESSAGES@@
static ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
[[maybe_unused]] static ymcc::sleeppower::RecoveryResumeCommit g_recoveryResumeCommit;
static std::atomic<bool> g_exitRequested{false},g_inputReady{true};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_sleepPowerRedeliveryArmed{false},g_sharedPowerProjectionPending{false};
static unsigned long long fixturePowerGeneration=100,serial=1;
static unsigned long long fixtureSuspendSerial=0;
static const struct {
 uint64_t load(std::memory_order) const noexcept {return fixtureSuspendSerial?fixtureSuspendSerial:fixturePowerGeneration;}
} g_sharedPowerSuspendSerial;
static HWND g_hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
static unsigned long long currentPowerGeneration(){return fixturePowerGeneration;}
// Explicit memory-only shared-power Reader port. Production sender and admission predicate stay complete.
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* output,std::uint32_t bytes) noexcept {
 if(!output||bytes<sizeof(*output))return 0;
 *output={};output->generation=fixturePowerGeneration;output->suspendSerial=g_sharedPowerSuspendSerial.load(std::memory_order_acquire);output->publicationRevision=1;
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
static bool changed=false,failCommit=false,failInput=false,throwLog=false,failTimer=false;
static unsigned arms=0,inputDeliveries=0,commitPosts=0,projectionDeliveries=0;
static json resumed;
static LRESULT productionWakeCommit(WPARAM,LPARAM);
static LRESULT productionPowerBroadcast(UINT,WPARAM,LPARAM);
static std::vector<ymcc::sleeppower::Dispatch::Receipt> powerDeliveries;
static std::vector<std::pair<WPARAM,LPARAM>> rawWindowPosts;
static unsigned rawWindowAttempts=0;
static bool failRawWindowPost=false;
static std::vector<std::pair<WPARAM,LPARAM>> committedWindowPosts;
static unsigned committedWindowAttempts=0;
static bool failCommittedWindowPost=false;
static struct {void handled(ULONG,ULONGLONG) const noexcept {}} g_sleepNotificationEvidence;
static std::function<void()> onIgnoredSuspend;
static void sgRecordFact(const char* event,const json&){
 if(onIgnoredSuspend&&std::string_view(event)=="power-suspend-ignored"){auto callback=std::move(onIgnoredSuspend);onIgnoredSuspend=nullptr;callback();}
}
static unsigned long long sgNowFileTime(){return serial;}
static void publishSharedPowerLifecycle(const char*){}
static ULONGLONG g_lastSuspendReceiptTick=0,g_lastSuspendNotifyTick=0,g_sgSleepQueryGeneration=0;
static bool g_sgModernStandbyActive=false,g_sgSleepQueryOwnsGeneration=false,g_sgSleepQueryGateClosed=false;
[[maybe_unused]] static std::atomic<unsigned long long> g_sgConfirmedSuspendGeneration{0};
static struct {unsigned long long generation=0;} g_sgTask;
static bool sgSleepRetryActive(){return false;}
// This fixture begins after Ready is committed. Worker-ready tickets run in the Native fixture.
[[maybe_unused]] static void redeliverPowerResumeReady(HWND){}
static const char* powerLifecycleName(PowerLifecycle){return "fixture";}
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
static void stopPowerResumeWatchdog(){boundary("resume-watchdog",fixturePowerGeneration);}
static bool steamAppearanceResumeCommitted(unsigned long long generation){boundary("steam-css",generation);return true;}
#ifdef YMCC_FIXTURE_LEGACY_DECKY_COMMIT
static bool deckySidebarResumeAfterIntent(unsigned long long generation,const char*){boundary("decky",generation);return true;}
#endif
static void ipc_emit(const char* name,const json& value){if(std::string(name)!="power.resumed")throw std::runtime_error("unexpected event");resumed=value;boundary("power-resumed",value.at("generation").get<unsigned long long>());}
[[maybe_unused]] static void appendNativeLifecycleLog(const char* name,const json& value){if(throwLog)throw std::runtime_error("diagnostic failure");if(std::string(name)!="power-resume-consumer-failed")throw std::runtime_error("unexpected error log");errors.push_back(value.at("consumer").get<std::string>());}
static UINT_PTR fixtureSetTimer(HWND,UINT_PTR,UINT,TIMERPROC){++arms;return failTimer?0:1;}
static BOOL fixtureKillTimer(HWND,UINT_PTR){return TRUE;}
static BOOL fixturePostMessage(HWND window,UINT message,WPARAM w,LPARAM l){
 if(message==kSleepPowerForwardMessage){
  if(window!=g_hwnd||!ymcc::sleeppower::Dispatch::transition(static_cast<ULONG>(w))||!l)throw std::runtime_error("invalid original receipt window post");
  ++rawWindowAttempts;if(failRawWindowPost)return FALSE;rawWindowPosts.emplace_back(w,l);return TRUE;
 }
 if(message==WM_POWER_RESUME_COMMIT){
  if(static_cast<unsigned long long>(l)!=fixturePowerGeneration||w==1)throw std::runtime_error("commit identity changed");
  if(w>=2){++committedWindowAttempts;if(failCommittedWindowPost)return FALSE;committedWindowPosts.emplace_back(w,l);return TRUE;}
  if(failCommit)return FALSE;++commitPosts;return TRUE;
 }
 if(message!=WM_POWER_INPUT_RECOVER||static_cast<unsigned long long>(w)!=fixturePowerGeneration||l)throw std::runtime_error("input identity changed");
 boundary("input-message",static_cast<unsigned long long>(w));return failInput?FALSE:TRUE;
}
static LRESULT fixtureSendMessage(HWND,UINT message,WPARAM w,LPARAM l){
 if(message==WM_POWER_RESUME_COMMIT)return productionWakeCommit(w,l);
 if(message==WM_POWER_INPUT_RECOVER){if(static_cast<unsigned long long>(w)!=fixturePowerGeneration||l)throw std::runtime_error("input repair relabeled");++inputDeliveries;return 0;}
 if(message==kSharedPowerStateMessage){++projectionDeliveries;return 0;}
 if(message==kSleepPowerForwardMessage)return productionPowerBroadcast(message,w,l);
 throw std::runtime_error("unexpected repair message");
}
#define PostMessageW fixturePostMessage
#define SendMessageW fixtureSendMessage
#define SetTimer fixtureSetTimer
#define KillTimer fixtureKillTimer
#define GetTickCount64() (serial)
static void inputCaptureSensorThreadRetryPowerWake(HWND){} // Sensor owner is exercised by the original sensor-power-only selector.
static void inputCaptureSensorThreadRecoverReturned(HWND){} // Actual STA recovery executes in sleep.gyro-power power-wake.
[[maybe_unused]] static void sgProbeMissingKernelWake() noexcept {} // Event-log readback runs in its source fixture.
@@DELIVERY@@
@@OWNER_EVENT_REPLAY@@
@@SUSPEND_ADMISSION@@
static LRESULT productionPowerBroadcast(UINT m,WPARAM w,LPARAM l){HWND h=g_hwnd;
@@POWER_BROADCAST@@
@@SUSPEND_HANDOFF@@
 if(powerReceipt)powerDeliveries.push_back(powerReceipt);
 return TRUE;
}
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
#undef GetTickCount64
static std::vector<std::string> cases,failures;
static unsigned assertions=0;
static const std::vector<std::string> stages={"resume-watchdog","steam-css",
#ifdef YMCC_FIXTURE_LEGACY_DECKY_COMMIT
 "decky",
#endif
 "power-resumed","input-message"};
static void require(bool value,const char* message){++assertions;if(!value)failures.push_back(cases.back()+": "+message);}
static std::vector<std::string> names(){std::vector<std::string> out;for(const auto& call:calls)out.push_back(call.stage);return out;}
static void settle(){const auto receipt=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPENDFAILED,++serial);g_sleepPowerDispatch.complete(receipt,++serial);}
static void reset(){settle();fixturePowerGeneration+=10;g_exitRequested=false;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_sleepPowerRedeliveryArmed=false;g_powerResumeCommitRedelivery=0;g_powerInputRecoverRedelivery=0;g_sharedPowerProjectionPending=false;calls.clear();errors.clear();resumed=json::object();interruptAt.clear();edge.clear();changed=false;failCommit=failInput=throwLog=failTimer=false;arms=inputDeliveries=commitPosts=projectionDeliveries=0;rawWindowPosts.clear();rawWindowAttempts=0;failRawWindowPost=false;}
static void normal(){productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(names()==stages,"one committed generation dispatches the original seven calls once");require(resumed.value("generation",0ull)==fixturePowerGeneration,"Ready event retains its captured generation");}
int main(int argc,char** argv){
 const std::string selected=argc==2?argv[1]:"";
 const auto run=[&](const std::string& name,const std::function<void()>& body){if(!selected.empty()&&selected!=name)return;reset();cases.push_back(name);try{body();}catch(const std::exception& e){failures.push_back(name+": unexpected exception "+e.what());}};
 for(const bool inputOnly:{false,true})for(const bool refused:{false,true}){
  if(inputOnly&&refused)continue;
  run(inputOnly?"committed-window-fallback-input-only":refused?"committed-window-fallback-all-refused":"committed-window-fallback-after-ready",[&]{
   committedWindowPosts.clear();committedWindowAttempts=0;failCommittedWindowPost=refused;
   g_sleepPowerDispatch.closeReceiptWake();
   const auto gen=fixturePowerGeneration;
   g_recoveryResumeCommit.record({gen,gen,++serial});
   failCommit=failInput=failTimer=true;
   if(inputOnly)normal();else sleepPowerPostResumeCommit(g_hwnd,gen);
   require((inputOnly?g_powerInputRecoverRedelivery.load():g_powerResumeCommitRedelivery.load())==gen&&
       inputDeliveries==0&&!g_sleepPowerRedeliveryArmed,"failed normal routes retain already committed work");
   require(committedWindowAttempts==1&&committedWindowPosts.size()==(refused?0u:1u),"the original window gets one fallback for the committed scope");
   if(refused){
    for(unsigned i=0;i<12;++i)sleepPowerPostResumeCommit(g_hwnd,gen);
    require(committedWindowAttempts==1&&calls.empty()&&g_powerResumeCommitRedelivery==gen,"window refusal parks debt without repeated fallback posts");
    failTimer=false;sleepPowerArmRedelivery(g_hwnd);productionTimer();
   }else if(!committedWindowPosts.empty()){
    const auto [w,l]=committedWindowPosts.front();committedWindowPosts.clear();
    require(w==gen+2&&static_cast<unsigned long long>(l)==gen,"fallback preserves the exact generation and suspend serial");
    productionWakeCommit(w,l);productionWakeCommit(w,l);
   }
   require(names()==stages&&inputDeliveries==1&&g_powerInputRecoverRedelivery==0&&g_powerResumeCommitRedelivery==0,"the current debt drains once without replaying other consumers");
   require(committedWindowPosts.empty()&&committedWindowAttempts==1,"delivery itself creates no more window fallbacks");
   failCommittedWindowPost=false;
  });
 }
 run("committed-window-fallback-retired-by-next-suspend",[]{
  committedWindowPosts.clear();committedWindowAttempts=0;g_sleepPowerDispatch.closeReceiptWake();
  const auto gen=fixturePowerGeneration;
  g_recoveryResumeCommit.record({gen,gen,++serial});
  failCommit=failInput=failTimer=true;sleepPowerPostResumeCommit(g_hwnd,gen);
  require(committedWindowPosts.size()==1,"old scope queued its one fallback");
  if(committedWindowPosts.empty())return;
  const auto [oldW,oldL]=committedWindowPosts.front();committedWindowPosts.clear();
  fixtureSuspendSerial=gen+1;
  g_recoveryResumeCommit.record({gen,fixtureSuspendSerial,++serial});
  sleepPowerPostResumeCommit(g_hwnd,gen);
  productionWakeCommit(oldW,oldL);
  require(calls.empty()&&inputDeliveries==0&&g_powerResumeCommitRedelivery==gen,"old fallback cannot drain the newer same-generation commit");
  require(committedWindowAttempts==2&&committedWindowPosts.size()==1,"a strictly newer committed suspend scope gets its own fallback");
  if(!committedWindowPosts.empty()){
   const auto [w,l]=committedWindowPosts.front();committedWindowPosts.clear();productionWakeCommit(w,l);
  }
  require(names()==stages&&inputDeliveries==1&&g_powerResumeCommitRedelivery==0&&g_powerInputRecoverRedelivery==0,"new commit drains its own debt once");
  fixtureSuspendSerial=0;
 });
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
 for(const auto& stage:stages)if(stage=="steam-css"||stage=="decky"||stage=="power-resumed")run(stage+"-exception",[&]{interruptAt=stage;edge="throw";normal();require(errors==std::vector<std::string>{stage},"consumer failure is recorded without blocking independent fanout");productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(names()==stages,"uncertain accepted work is not automatically replayed");});
 run("diagnostic-exception",[]{interruptAt="steam-css";edge="throw";throwLog=true;normal();++fixturePowerGeneration;calls.clear();normal();});
 run("failed-commit-post",[]{failCommit=true;sleepPowerPostResumeCommit(g_hwnd,fixturePowerGeneration);require(g_powerResumeCommitRedelivery==fixturePowerGeneration&&calls.empty(),"original failed commit post retains delivery debt");failCommit=false;productionTimer();require(names()==stages&&g_powerResumeCommitRedelivery==0,"original timer recovers failed commit without inventing a generation");});
 run("failed-input-post",[]{failInput=true;normal();require(g_powerInputRecoverRedelivery==fixturePowerGeneration,"original failed input post retains only input transport debt");failInput=false;productionTimer();require(inputDeliveries==1&&names()==stages&&g_powerInputRecoverRedelivery==0,"input retry does not replay completed committed fanout");productionTimer();require(inputDeliveries==1,"completed input delivery is not replayed");});
 run("refused-commit-post-and-timer",[]{
  if(!g_sleepPowerDispatch.openReceiptWake())throw std::runtime_error("could not open original owner event");
  const auto wake=g_sleepPowerDispatch.receiptWakeHandle();
  failCommit=failInput=failTimer=true;
  sleepPowerPostResumeCommit(g_hwnd,fixturePowerGeneration);
  require(arms==1&&!g_sleepPowerRedeliveryArmed&&calls.empty()&&g_powerResumeCommitRedelivery==fixturePowerGeneration,"refused post and timer leave committed debt without raw broadcasts");
  const bool delivered=WaitForSingleObject(wake,0)==WAIT_OBJECT_0;
  require(delivered,"existing owner event must wake for committed debt without a new publication");
  if(delivered)sleepPowerReplayWindowDelivery(g_hwnd);
  require(names()==stages&&inputDeliveries==1&&g_powerResumeCommitRedelivery==0&&g_powerInputRecoverRedelivery==0,"owner event finishes committed fanout and input despite both refused posts");
  require(WaitForSingleObject(wake,0)==WAIT_TIMEOUT,"fallback does not signal again from its own delivery");
  g_sleepPowerDispatch.closeReceiptWake();
 });
 run("refused-input-post-and-timer",[]{
  if(!g_sleepPowerDispatch.openReceiptWake())throw std::runtime_error("could not open original owner event");
  const auto wake=g_sleepPowerDispatch.receiptWakeHandle();
  failInput=failTimer=true;normal();
  require(g_powerInputRecoverRedelivery==fixturePowerGeneration&&inputDeliveries==0&&!g_sleepPowerRedeliveryArmed,"only input remains owed after fanout");
  const bool delivered=WaitForSingleObject(wake,0)==WAIT_OBJECT_0;
  require(delivered,"owner event wakes for an isolated input delivery failure");
  if(delivered)sleepPowerReplayWindowDelivery(g_hwnd);
  require(inputDeliveries==1&&names()==stages&&g_powerInputRecoverRedelivery==0,"input fallback does not replay completed fanout");
  require(WaitForSingleObject(wake,0)==WAIT_TIMEOUT,"input fallback returns the event to idle");
  g_sleepPowerDispatch.closeReceiptWake();
 });
 run("owner-event-renews-for-next-suspend-in-same-generation",[]{
  if(!g_sleepPowerDispatch.openReceiptWake())throw std::runtime_error("could not open original owner event");
  const auto wake=g_sleepPowerDispatch.receiptWakeHandle();
  const auto generation=fixturePowerGeneration;
  g_recoveryResumeCommit.record({generation,1,++serial});
  failCommit=failTimer=true;
  sleepPowerPostResumeCommit(g_hwnd,generation);
  require(WaitForSingleObject(wake,0)==WAIT_OBJECT_0,"first committed scope owns one fallback");
  sleepPowerReplayWindowDelivery(g_hwnd);
  require(names()==stages&&inputDeliveries==1,"first Ready fanout and input complete once");
  sleepPowerPostResumeCommit(g_hwnd,generation);
  require(WaitForSingleObject(wake,0)==WAIT_TIMEOUT,"duplicate in the same scope cannot signal again");
  g_recoveryResumeCommit.record({generation,2,++serial});
  sleepPowerPostResumeCommit(g_hwnd,generation);
  const bool delivered=WaitForSingleObject(wake,0)==WAIT_OBJECT_0;
  require(delivered,"second committed Suspend in the same generation still owns one fallback");
  if(delivered)sleepPowerReplayWindowDelivery(g_hwnd);
  require(inputDeliveries==2&&g_powerResumeCommitRedelivery==0&&g_powerInputRecoverRedelivery==0,
      "second scope returns both delivery debts and recovers input exactly once");
  const auto second=names();
  require(second.size()==stages.size()+1&&std::equal(stages.begin(),stages.end(),second.begin())&&
      second.back()=="input-message",
      "same-generation repair does not redo other consumers");
  sleepPowerPostResumeCommit(g_hwnd,generation);
  require(WaitForSingleObject(wake,0)==WAIT_TIMEOUT,"second scope cannot spin the pump");
  g_sleepPowerDispatch.closeReceiptWake();
 });
 run("isolated-input-owner-event-renews-for-next-suspend",[]{
  if(!g_sleepPowerDispatch.openReceiptWake())throw std::runtime_error("could not open original owner event");
  const auto wake=g_sleepPowerDispatch.receiptWakeHandle();
  const auto generation=fixturePowerGeneration;
  failInput=failTimer=true;
  for(unsigned serialInGeneration=1;serialInGeneration<=2;++serialInGeneration){
   g_recoveryResumeCommit.record({generation,serialInGeneration,++serial});
   productionWakeCommit(0,static_cast<LPARAM>(generation));
   const bool delivered=WaitForSingleObject(wake,0)==WAIT_OBJECT_0;
   require(delivered,"each committed suspend can wake an isolated input fallback");
   if(delivered)sleepPowerReplayWindowDelivery(g_hwnd);
   require(inputDeliveries==serialInGeneration&&g_powerInputRecoverRedelivery==0,"isolated input debt is settled exactly once per commit");
   productionWakeCommit(0,static_cast<LPARAM>(generation));
   require(WaitForSingleObject(wake,0)==WAIT_TIMEOUT,"duplicate input dispatch cannot rearm the fallback");
  }
  g_sleepPowerDispatch.closeReceiptWake();
 });
 run("refused-commit-owner-event-stale",[]{
  if(!g_sleepPowerDispatch.openReceiptWake())throw std::runtime_error("could not open original owner event");
  const auto wake=g_sleepPowerDispatch.receiptWakeHandle();
  failCommit=failTimer=true;sleepPowerPostResumeCommit(g_hwnd,fixturePowerGeneration);
  ++fixturePowerGeneration;
  require(WaitForSingleObject(wake,0)==WAIT_OBJECT_0,"an outstanding event may outlive its generation");
  sleepPowerReplayWindowDelivery(g_hwnd);
  require(calls.empty()&&g_powerResumeCommitRedelivery==0&&inputDeliveries==0,"owner event never relabels stale debt");
  sleepPowerPostResumeCommit(g_hwnd,fixturePowerGeneration);
  require(WaitForSingleObject(wake,0)==WAIT_OBJECT_0,"the successor still owns one event fallback");
  sleepPowerReplayWindowDelivery(g_hwnd);
  require(names()==stages&&inputDeliveries==1&&g_powerResumeCommitRedelivery==0&&g_powerInputRecoverRedelivery==0,"successor completes only its own work");
  require(WaitForSingleObject(wake,0)==WAIT_TIMEOUT,"successor returns the event to idle");
  g_sleepPowerDispatch.closeReceiptWake();
 });
 run("stale-or-exiting-debt",[]{failCommit=true;sleepPowerPostResumeCommit(g_hwnd,fixturePowerGeneration);++fixturePowerGeneration;productionTimer();require(calls.empty()&&g_powerResumeCommitRedelivery==0,"successor retires old committed debt");failCommit=false;g_exitRequested=true;productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(calls.empty(),"exit rejects a late committed message");});
 for(const auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Resuming})run("initial-phase-"+std::to_string(static_cast<unsigned>(phase)),[&]{g_powerLifecycle=phase;productionWakeCommit(0,static_cast<LPARAM>(fixturePowerGeneration));require(calls.empty(),"uncommitted phases reject all fanout");g_powerLifecycle=PowerLifecycle::Ready;normal();});
 for(const bool replaceFailed:{true,false})run(replaceFailed?"resume-ahead-of-replacement-suspend":"resume-ahead-of-posted-suspend",[&]{
  using Receipt=ymcc::sleeppower::Dispatch::Receipt;
  Receipt prior,suspend,resume;powerDeliveries.clear();
  if(replaceFailed){
   require(!g_sleepPowerDispatch.callback(PBT_APMSUSPEND,++serial,[&](auto r){prior=r;return false;}),"older Suspend post fails");
   sleepPowerArmRedelivery(g_hwnd);
  }
  const auto suspendTick=++serial;
  require(g_sleepPowerDispatch.callback(PBT_APMSUSPEND,suspendTick,[&](auto r){suspend=r;return true;},700),"new Suspend post succeeds but is still unclaimed");
  require(g_sleepPowerDispatch.callback(PBT_APMRESUMECRITICAL,++serial,[&](auto r){resume=r;return true;},701),"following Resume post succeeds");
  const auto deliver=[](const Receipt& r){productionPowerBroadcast(kSleepPowerForwardMessage,r.type,static_cast<LPARAM>(r.sequence));};
  deliver(resume);
  require(powerDeliveries.empty()&&g_sleepPowerDispatch.pendingSuspend()&&g_sleepPowerRedeliveryArmed,"Resume overtook an unclaimed successfully posted Suspend");
  productionTimer();
  require(powerDeliveries.size()==1&&powerDeliveries[0].sequence==suspend.sequence&&powerDeliveries[0].fileTime100ns==700&&powerDeliveries[0].tick==suspendTick,"first timer consumes the current Suspend with its original receipt");
  require(g_sleepPowerDispatch.pendingSuspend()&&g_sleepPowerRedeliveryArmed,"Suspend completion preserves the fence and rearms the original timer");
  productionTimer();
  require(powerDeliveries.size()==2&&powerDeliveries[1].sequence==resume.sequence&&powerDeliveries[1].fileTime100ns==701,"next timer delivers the waiting Resume once");
  require(!g_sleepPowerDispatch.pendingSuspend()&&!g_sleepPowerDispatch.hasPending()&&!g_sleepPowerRedeliveryArmed,"completed pair clears delivery debt and the suspend fence");
  if(prior)deliver(prior);
  deliver(suspend);deliver(resume);productionTimer();
  require(powerDeliveries.size()==2&&!g_sleepPowerRedeliveryArmed,"superseded and late duplicate posts do not replay completed work");
 });
 run("deferred-critical-resume-before-query-failure",[]{
  using Receipt=ymcc::sleeppower::Dispatch::Receipt;
  Receipt suspend,critical,automatic,queryFailed;powerDeliveries.clear();
  const auto post=[](ULONG type,Receipt& output){const auto tick=++serial;g_sleepPowerDispatch.callback(type,tick,[&](auto r){output=r;return true;},tick);};
  const auto deliver=[](const Receipt& r){productionPowerBroadcast(kSleepPowerForwardMessage,r.type,static_cast<LPARAM>(r.sequence));};
  post(PBT_APMSUSPEND,suspend);post(PBT_APMRESUMECRITICAL,critical);
  deliver(critical);
  require(powerDeliveries.empty()&&g_sleepPowerRedeliveryArmed,"first critical Resume is deferred behind Suspend");
  post(PBT_APMRESUMEAUTOMATIC,automatic);post(PBT_APMQUERYSUSPENDFAILED,queryFailed);
  deliver(automatic);deliver(queryFailed);productionTimer();
  require(powerDeliveries.size()==1&&powerDeliveries[0].sequence==suspend.sequence,"Suspend is delivered first despite later traffic");
  productionTimer();
  require(powerDeliveries.size()==2&&powerDeliveries[1].sequence==critical.sequence,"later traffic displaced the deferred critical Resume");
  productionTimer();
  require(powerDeliveries.size()==3&&powerDeliveries[2].sequence==queryFailed.sequence,"latest query failure also finishes after the required boundary pair");
  require(!g_sleepPowerDispatch.hasPending()&&!g_sleepPowerDispatch.pendingSuspend()&&!g_sleepPowerRedeliveryArmed,"later traffic cannot strand the suspend fence or repair timer");
 });
 run("new-suspend-supersedes-waiting-resume",[]{
  using Receipt=ymcc::sleeppower::Dispatch::Receipt;
  Receipt first,oldWake,second,currentWake;powerDeliveries.clear();
  const auto post=[](ULONG type,Receipt& output){g_sleepPowerDispatch.callback(type,++serial,[&](auto r){output=r;return true;});};
  const auto deliver=[](const Receipt& r){productionPowerBroadcast(kSleepPowerForwardMessage,r.type,static_cast<LPARAM>(r.sequence));};
  post(PBT_APMSUSPEND,first);post(PBT_APMRESUMECRITICAL,oldWake);deliver(oldWake);
  post(PBT_APMSUSPEND,second);post(PBT_APMRESUMESUSPEND,currentWake);deliver(currentWake);
  productionTimer();productionTimer();deliver(first);deliver(oldWake);
  require(powerDeliveries.size()==2&&powerDeliveries[0].sequence==second.sequence&&powerDeliveries[1].sequence==currentWake.sequence,"new Suspend retires previous wake replay without consuming the current wake");
  require(!g_sleepPowerDispatch.hasPending()&&!g_sleepPowerDispatch.pendingSuspend()&&!g_sleepPowerRedeliveryArmed,"successor boundary pair leaves no delivery debt");
 });
 for(const bool query:{false,true})run(query?"query-resuming-suspend-preserves-posted-wake":"duplicate-suspend-preserves-failed-wake",[&]{
  using Receipt=ymcc::sleeppower::Dispatch::Receipt;
  Receipt wake,late;powerDeliveries.clear();
  g_lastSuspendReceiptTick=++serial;g_lastSuspendNotifyTick=serial;
  g_powerLifecycle=query?PowerLifecycle::Resuming:PowerLifecycle::Suspended;
  g_sgTask.generation=fixturePowerGeneration;
  g_sgSleepQueryOwnsGeneration=g_sgSleepQueryGateClosed=query;
  g_sgSleepQueryGeneration=query?fixturePowerGeneration:0;
  const auto wakeTick=++serial;
  require(g_sleepPowerDispatch.callback(PBT_APMRESUMESUSPEND,wakeTick,[&](auto r){wake=r;return query;},731)==query,"original wake carries the selected post result");
  if(!query)sleepPowerArmRedelivery(g_hwnd);
  g_sleepPowerDispatch.callback(PBT_APMSUSPEND,++serial,[&](auto r){late=r;return true;},732);
  const auto deliver=[](const Receipt& r){productionPowerBroadcast(kSleepPowerForwardMessage,r.type,static_cast<LPARAM>(r.sequence));};
  deliver(late);
  require(powerDeliveries.empty()&&!g_sgSleepQueryOwnsGeneration&&!g_sgSleepQueryGateClosed,"production suspend admission rejected the late receipt and retired any query gate");
  require(g_sleepPowerDispatch.pendingSuspend()&&g_sleepPowerRedeliveryArmed,"retained wake keeps the ingress fence and rearms delivery after ignored suspend");
  productionTimer();
  require(powerDeliveries.size()==1&&powerDeliveries.front().sequence==wake.sequence&&powerDeliveries.front().tick==wakeTick&&powerDeliveries.front().fileTime100ns==731,"ignored suspend must allow exactly one delivery of the original wake and its clocks");
  deliver(wake);deliver(late);productionTimer();
  require(powerDeliveries.size()==1&&!g_sleepPowerDispatch.hasPending()&&!g_sleepPowerDispatch.pendingSuspend()&&!g_sleepPowerRedeliveryArmed,"late posts cannot replay the wake or strand the ingress fence");
  g_lastSuspendReceiptTick=g_lastSuspendNotifyTick=g_sgSleepQueryGeneration=g_sgTask.generation=0;
 });
 for(const bool successor:{false,true})run(successor?"ignored-suspend-reentrant-successor":"ignored-suspend-reentrant-automatic-wake",[&]{
  using Receipt=ymcc::sleeppower::Dispatch::Receipt;
  Receipt first,late,next,wake;powerDeliveries.clear();
  const auto post=[](ULONG type,Receipt& output){const auto tick=++serial;g_sleepPowerDispatch.callback(type,tick,[&](auto r){output=r;return true;},tick+1);};
  const auto deliver=[](const Receipt& r){productionPowerBroadcast(kSleepPowerForwardMessage,r.type,static_cast<LPARAM>(r.sequence));};
  g_powerLifecycle=PowerLifecycle::Suspended;g_sgTask.generation=fixturePowerGeneration;
  post(PBT_APMRESUMECRITICAL,first);post(PBT_APMSUSPEND,late);
  deliver(first);
  require(powerDeliveries.empty(),"the older wake post waits for the late suspend decision");
  onIgnoredSuspend=[&]{
   if(successor){serial+=2000;post(PBT_APMSUSPEND,next);post(PBT_APMRESUMESUSPEND,wake);deliver(next);}
   else {post(PBT_APMRESUMEAUTOMATIC,wake);deliver(wake);}
  };
  deliver(late);
  require(!onIgnoredSuspend&&powerDeliveries.empty()&&g_sleepPowerDispatch.pendingSuspend(),"reentrant ingress cannot deliver before ignored suspend completion");
  g_powerLifecycle=PowerLifecycle::Ready;g_sgTask.generation=0;
  productionTimer();productionTimer();
  require(powerDeliveries.size()==2&&powerDeliveries[0].sequence==(successor?next.sequence:first.sequence)&&powerDeliveries[1].sequence==wake.sequence,"a genuine successor drops the old wake, while a reentrant automatic wake cannot displace the retained critical wake");
  deliver(first);deliver(late);deliver(wake);productionTimer();
  require(powerDeliveries.size()==2&&!g_sleepPowerDispatch.hasPending()&&!g_sleepPowerDispatch.pendingSuspend()&&!g_sleepPowerRedeliveryArmed,"reentrant delivery completes once and leaves no power debt");
 });
 for(const bool refuseWindow:{false,true})run(refuseWindow?"raw-receipt-all-retry-paths-refused":"raw-receipt-owner-window-fallback",[&]{
  using Receipt=ymcc::sleeppower::Dispatch::Receipt;
  Receipt suspend,resume;
  powerDeliveries.clear();g_sleepPowerDispatch.closeReceiptWake();failTimer=true;failRawWindowPost=refuseWindow;
  const auto observe=[](ULONG type,Receipt& result,bool posted,unsigned long long fileTime){
   return g_sleepPowerDispatch.callback(type,++serial,[&](auto r){result=r;return posted;},fileTime);
  };
  require(!observe(PBT_APMSUSPEND,suspend,false,880),"the original suspend post is refused");
  for(unsigned i=0;i<12;++i)sleepPowerArmRedelivery(g_hwnd);
  require(rawWindowAttempts==1&&rawWindowPosts.size()==(refuseWindow?0u:1u),"only one original-window fallback is attempted per pending head and observed scope");
  require(powerDeliveries.empty()&&g_sleepPowerDispatch.pendingSuspend(),"retry never runs a consumer in the callback or drops its suspend fence");
  if(refuseWindow)require(g_sleepPowerDispatch.hasPending()&&!g_sleepPowerRedeliveryArmed,"all paths refused leaves the original debt parked");
  failRawWindowPost=false;
  require(observe(PBT_APMRESUMECRITICAL,resume,true,881),"later authentic Resume reaches the owner before its suspend debt");
  productionPowerBroadcast(kSleepPowerForwardMessage,resume.type,static_cast<LPARAM>(resume.sequence));
  require(powerDeliveries.empty()&&g_sleepPowerDispatch.pendingSuspend(),"an early Resume cannot pass the queued pending Suspend");
  require(!rawWindowPosts.empty(),"the existing owner window has a route to settle the now-runnable debt");
  for(unsigned i=0;i<6&&!rawWindowPosts.empty();++i){
   const auto [type,sequence]=rawWindowPosts.front();rawWindowPosts.erase(rawWindowPosts.begin());
   productionPowerBroadcast(kSleepPowerForwardMessage,type,sequence);
  }
  require(powerDeliveries.size()==2&&powerDeliveries[0].sequence==suspend.sequence&&powerDeliveries[0].fileTime100ns==880&&
      powerDeliveries[1].sequence==resume.sequence&&powerDeliveries[1].fileTime100ns==881,"the exact Suspend and Resume are delivered once in original order");
  require(rawWindowPosts.empty()&&!g_sleepPowerDispatch.hasPending()&&!g_sleepPowerDispatch.pendingSuspend()&&!g_sleepPowerRedeliveryArmed,"completed pair leaves no queued fallback or suspend fence");
  const auto attempts=rawWindowAttempts;
  productionPowerBroadcast(kSleepPowerForwardMessage,resume.type,static_cast<LPARAM>(resume.sequence));
  sleepPowerArmRedelivery(g_hwnd);
  require(rawWindowAttempts==attempts&&powerDeliveries.size()==2,"a late duplicate cannot replay work or rearm the fallback");
 });
 run("raw-receipt-window-fallback-replaced-by-new-suspend",[]{
  using Receipt=ymcc::sleeppower::Dispatch::Receipt;
  Receipt previous,suspend,resume;
  powerDeliveries.clear();g_sleepPowerDispatch.closeReceiptWake();failTimer=true;
  const auto observe=[](ULONG type,Receipt& result,bool posted,unsigned long long fileTime){
   return g_sleepPowerDispatch.callback(type,++serial,[&](auto r){result=r;return posted;},fileTime);
  };
  require(!observe(PBT_APMSUSPEND,previous,false,890),"older Suspend needs fallback");
  sleepPowerArmRedelivery(g_hwnd);
  require(rawWindowPosts.size()==1&&static_cast<unsigned long long>(rawWindowPosts.front().second)==previous.sequence,"older Suspend is queued before its replacement");
  require(observe(PBT_APMSUSPEND,suspend,true,891)&&observe(PBT_APMRESUMESUSPEND,resume,true,892),"genuine replacement pair enters the shared dispatcher");
  if(!rawWindowPosts.empty()){
   const auto [type,sequence]=rawWindowPosts.front();rawWindowPosts.erase(rawWindowPosts.begin());
   productionPowerBroadcast(kSleepPowerForwardMessage,type,sequence);
  }
  require(powerDeliveries.empty()&&g_sleepPowerDispatch.pendingSuspend(),"the queued fallback cannot apply the superseded Suspend");
  for(unsigned i=0;i<6&&!rawWindowPosts.empty();++i){
   const auto [type,sequence]=rawWindowPosts.front();rawWindowPosts.erase(rawWindowPosts.begin());
   productionPowerBroadcast(kSleepPowerForwardMessage,type,sequence);
  }
  require(powerDeliveries.size()==2&&powerDeliveries[0].sequence==suspend.sequence&&powerDeliveries[1].sequence==resume.sequence,
      "the affected window consumes only the new pair in order");
  require(!g_sleepPowerDispatch.hasPending()&&!g_sleepPowerDispatch.pendingSuspend()&&rawWindowPosts.empty(),"new pair settles the original dispatch fence");
 });
 run("raw-receipt-event-takes-precedence-over-window-fallback",[]{
  using Receipt=ymcc::sleeppower::Dispatch::Receipt;
  if(!g_sleepPowerDispatch.openReceiptWake())throw std::runtime_error("could not open original owner event");
  const auto wake=g_sleepPowerDispatch.receiptWakeHandle();
  failTimer=true;powerDeliveries.clear();Receipt resume;
  require(!g_sleepPowerDispatch.callback(PBT_APMRESUMECRITICAL,++serial,[&](auto r){resume=r;return false;},893),"original Resume window post is refused");
  sleepPowerArmRedelivery(g_hwnd);
  const bool signalled=WaitForSingleObject(wake,0)==WAIT_OBJECT_0;
  require(signalled&&rawWindowAttempts==0,"working owner event prevents an extra window post");
  if(signalled)sleepPowerReplayWindowDelivery(g_hwnd);
  require(powerDeliveries.size()==1&&powerDeliveries[0].sequence==resume.sequence&&!g_sleepPowerDispatch.hasPending(),"existing owner event settles the exact receipt");
  g_sleepPowerDispatch.closeReceiptWake();
 });
 if(cases.empty()){std::cerr<<"UNKNOWN_COMMITTED_DELIVERY_CASE\n";return 2;}
 std::cout<<json{{"cases",cases},{"assertions",assertions},{"failures",failures},{"actualDispatch",true},{"actualCommittedMessageAndSenderAndTimer",true},{"nativeConsumerAlgorithmsInert",true},{"deviceOrSteamOperations",0}}.dump(2)<<'\n';return failures.empty()?0:1;
}
