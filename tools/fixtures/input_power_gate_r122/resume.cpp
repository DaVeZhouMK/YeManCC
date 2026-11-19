// Original input power consumer. All device/window/Host leaves are inert.
// Native progress, ROG guards/deferred owner, Dispatch and repair timer are source-extracted.
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "sleep_power_dispatch.h"
#include "sleep_power_state.h"
#include "sleep_wake_admission.h"
using json=nlohmann::json;
@@POWER_ENUM@@
@@MESSAGES@@
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_exitRequested{false},g_sleepPowerRedeliveryArmed{false},g_sharedPowerProjectionPending{false};
static std::atomic<unsigned long long> fixturePowerGeneration{100};
static unsigned long long sharedPowerGeneration=0;
static unsigned long long currentPowerGeneration(){return sharedPowerGeneration?sharedPowerGeneration:fixturePowerGeneration.load();}
static ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
static unsigned long long serial=1;
static bool sharedPowerKnown=true;

static std::atomic<uint64_t> g_sharedPowerPublicationRevision{1},g_sharedPowerSuspendSerial{1};
static bool sharedPowerAllowed=true;
static bool hardwareWriteAllowed(){if(!sharedPowerKnown)g_sharedPowerSuspendSerial.fetch_add(1);return sharedPowerAllowed&&!g_exitRequested.load()&&!g_sleepPowerDispatch.pendingSuspend()&&g_powerLifecycle.load()==PowerLifecycle::Ready;}
static unsigned rawReadCount=0,rawFailAt=0;static bool rawFailDenied=false,unknownAfterSend=false,failNextRaw=false;
@@RAW_READER@@
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* out,uint32_t bytes) noexcept {const bool fail=(++rawReadCount==rawFailAt)||failNextRaw;failNextRaw=false;const bool wasKnown=sharedPowerKnown,wasAllowed=sharedPowerAllowed;if(fail){if(rawFailDenied)sharedPowerAllowed=false;else sharedPowerKnown=false;}const auto result=fixtureProductionReadSharedSystemPower(out,bytes);sharedPowerKnown=wasKnown;sharedPowerAllowed=wasAllowed;return result;}
@@POWER_CAPTURE@@
@@POWER_CURRENT@@
static std::vector<std::string> calls,allCalls;
static std::vector<json> keepDiagnostics,stepFailures;
static std::string interruptAt,edge;
static bool changed=false,keepWriter=false,rawResult=true,observeReady=false;
static unsigned publications=0,repairArms=0,focusArms=0,binds=0;
static std::atomic<bool> waitEntered{false};
static std::atomic<unsigned> rogAttempts{0};
static LRESULT recoverMessage(WPARAM);
static void projectSharedPowerLifecycle(HWND);
static void boundary(const char*,bool core=true);
struct ReadyFlag {
 std::atomic<bool> value{true};
 bool load(std::memory_order order=std::memory_order_seq_cst)const{return value.load(order);}
 void store(bool v,std::memory_order order=std::memory_order_seq_cst){value.store(v,order);if(observeReady)boundary("input-ready",false);}
 ReadyFlag& operator=(bool v){store(v);return *this;}
 operator bool()const{return load();}
 void silent(bool v){value.store(v);}
}g_inputReady;
static std::recursive_mutex fixtureObservationMx;
static void boundary(const char* name,bool core){
 std::lock_guard<std::recursive_mutex> observation(fixtureObservationMx);
 if(core)calls.emplace_back(name);allCalls.emplace_back(name);
 if(changed||interruptAt!=name)return;
 changed=true;
 if(edge=="pending")g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
 else if(edge=="generation"){++fixturePowerGeneration;g_inputReady.silent(true);}
 else if(edge=="phase"){g_powerLifecycle=PowerLifecycle::Suspended;g_inputReady.silent(false);}
 else if(edge=="exit"){g_exitRequested=true;g_inputReady.silent(true);}
 else if(edge=="reentrant")recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));
 else if(edge=="reentrant-successor"){++fixturePowerGeneration;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));}
 else if(edge=="unknown")sharedPowerKnown=false;
 else if(edge=="throw")throw std::runtime_error("accepted test leaf failed");
}
static bool gamepadRegisterRawInput(){boundary("raw");return rawResult;}
static bool sgShouldKeepInputDuringStandby(){return keepWriter;}
static void gamepadResetNativeState(bool){boundary("reset");}
static void gamepadReadState(){boundary("read");}
static void inputHostRequestPowerRearm(unsigned long long,const char*){boundary("rearm");}
static void inputHostRequestWakeRescan(unsigned long long){boundary("rescan");}
static void inputHostQueueSubmitStatsJob(const char*){boundary("stats");}
static void publishSharedPowerLifecycle(const char*){++publications;boundary("publish",false);}
static void appendNativeLifecycleLog(const char* name,const json& data){
 const std::string n=name;if(n=="wake-input-chain-snapshot")boundary("snapshot",false);
 else if(n=="power-input-recover-light"){keepDiagnostics.push_back(data);boundary("keep-diagnostic",false);}
 else if(n=="power-input-step-failed")stepFailures.push_back(data);
}
static const char* powerLifecycleName(PowerLifecycle){return "fixture";}
template<class... Args>static void traceLog(const char*,Args...){boundary("trace",false);}
namespace ymcc::screenpads{static void ownerResumeReady(unsigned long long){boundary("screenpads",false);}}
static void msiVisibilityObserverWakeReady(unsigned long long){boundary("msi",false);}
static unsigned g_xinputConnectedMask=0,g_xinputPrimarySlot=0,g_xinputPhysicalSourceMask=0,g_inputCaptureBusTickGlobal=0;
static bool g_xinputPhysicalSourceLocked=false,g_inputHostPrepared=false,g_inputHostNeutralized=false,g_inputHostActive=false;
static void* g_inputHostProcess=nullptr;
static bool physicalReleaseWindowActive(){return false;}
static std::atomic<unsigned> g_physicalReleaseWindowStage{0},g_physicalReleaseWindowTransition{0},g_physicalReleaseWindowDeferredCleanup{0},g_physicalReleaseWindowStaleByPower{0},g_physicalReleaseWindowTokenId{0};
static bool inputHostExitFenceRaised(){return false;}
static std::atomic<bool> g_inputHostPowerAdmissionRevoked{true};
struct FocusTargetSnapshot {unsigned fixtureValue=0;};
static std::atomic<unsigned long long> g_wakeFocusNudgeGeneration{0};
static std::mutex g_rememberedGameTargetMx;
static FocusTargetSnapshot g_wakeFocusNudgeTarget,g_rememberedGameTarget;
static ULONGLONG g_rememberedGameDeadline=0;
static constexpr UINT_PTR WAKE_FOCUS_NUDGE_TIMER_ID=0x5912;
static constexpr UINT kWakeFocusNudgeDelayMs=5000;
static HWND g_hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(1));
static thread_local bool fixtureBoundaryHeld=false;
struct ObservedTimedMutex {
 std::timed_mutex value;
 bool try_lock(){const bool ok=value.try_lock();if(ok)fixtureBoundaryHeld=true;return ok;}
 template<class Duration>bool try_lock_for(Duration duration){waitEntered=true;const bool ok=value.try_lock_for(duration);if(ok)fixtureBoundaryHeld=true;return ok;}
 void unlock(){fixtureBoundaryHeld=false;value.unlock();}
 void lock(){value.lock();fixtureBoundaryHeld=true;}
};
static ObservedTimedMutex g_rogRemapBoundaryMx;
@@ROG_GUARDS@@
static struct {bool isolated=false;}g_aiFanMockSession;
static std::atomic<unsigned long long> g_rogRemapDevGen{1};
static std::atomic<int> g_entryTestRecoverBindDeferred{0},g_entryTestRecoverBindDropped{0};
static void rogRemapLog(const char*,const json&){}
static void rogBindInputHid(){
 if(!fixtureBoundaryHeld||!tls_rogRemapActive)throw std::runtime_error("bind escaped original ROG session boundary");
 ++binds;boundary("rog-bind",false);
}
@@ROG_PENDING@@
@@ROG_BIND@@
@@ROG_CONSUME@@
static bool rogRecoverBindSerialized(const char* why,unsigned long long generation=0,bool* dispatched=nullptr){
 ++rogAttempts;boundary("rog");
 @@ROG_INVOKE@@
}
static void rogEnsureXboxFaceEnabled(const char*){
 boundary("face");
 RogRemapGuardScope guard;RogRemapBoundaryScope owner;
 if(guard.acquired&&owner.entered)(void)rogRemapConsumeDeferredRecover();
}
@@GAMEPAD_PROGRESS@@
@@RECOVER@@
static bool failRepairTimer=false;
static UINT_PTR fixtureSetTimer(HWND,UINT_PTR id,UINT,TIMERPROC){if(id==kSleepPowerRedeliveryTimer){++repairArms;if(failRepairTimer)return 0;}else{++focusArms;boundary("focus-arm",false);}return 1;}
static BOOL fixtureKillTimer(HWND,UINT_PTR id){if(id==WAKE_FOCUS_NUDGE_TIMER_ID)boundary("focus-cancel",false);return TRUE;}
static LRESULT fixtureSendMessage(HWND,UINT message,WPARAM w,LPARAM l){
 if(message==WM_POWER_INPUT_RECOVER){const auto result=recoverMessage(w);if(unknownAfterSend){unknownAfterSend=false;failNextRaw=true;rawFailDenied=false;}return result;}
 if(message==kSleepPowerForwardMessage){const auto receipt=g_sleepPowerDispatch.forwarded(static_cast<unsigned long long>(l),static_cast<ULONG>(w),++serial);g_sleepPowerDispatch.complete(receipt,++serial);return 0;}
 if(message==kSharedPowerStateMessage){projectSharedPowerLifecycle(g_hwnd);return 0;}if(message==WM_POWER_RESUME_COMMIT)return 0;
 throw std::runtime_error("unexpected original repair message");
}
#define SetTimer fixtureSetTimer
#define KillTimer fixtureKillTimer
#define SendMessageW fixtureSendMessage
[[maybe_unused]] static void sleepPowerArmRedelivery(HWND);
static void inputCaptureSensorThreadRetryPowerWake(HWND){} // Sensor owner is exercised by the original sensor-power-only selector.
static void inputCaptureSensorThreadRecoverReturned(HWND){} // Actual STA recovery executes in sleep.gyro-power power-wake.
@@DELIVERY@@


static unsigned projectionMsiCalls=0,projectionOtherCalls=0;
static bool projectionThrow=false,projectionUnknownAtMsi=false;
static struct {void sharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){++projectionOtherCalls;}}g_gameOverlay;
static std::atomic<bool> g_sensorSuspendCallbackActive{false};
static void inputCaptureResumeSensorsForPower(){++projectionOtherCalls;}
static bool msiNativeForegroundSharedPowerReady(const ymcc::sleeppower::SharedPowerV1&){++projectionOtherCalls;return true;}
static bool focusIsolationSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){return true;}
static bool sharedSleepWakeClassified(uint64_t){return true;}
static bool sgSleepRetryActive(){return false;}
namespace SleepRecoveryAuto {static void powerReady(uint64_t){++projectionOtherCalls;}}
static void steamOriginalRecoveryPowerReady(uint64_t){++projectionOtherCalls;}
namespace ymcc::lossless {static struct Monitor{void powerChanged(){++projectionOtherCalls;}}fixtureMonitor;static auto& runtimeMonitor(){return fixtureMonitor;}}
namespace SteamControllerCursor {static void power(const ymcc::sleeppower::SharedPowerV1&){++projectionOtherCalls;}}
namespace ymcc::screenpads {static void ownerSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1&){++projectionOtherCalls;}}
static void monitorSharedPowerChanged(){++projectionOtherCalls;}
namespace ymcc::inputHost::msiPowerWork {static void readyDelivery(const ymcc::sleeppower::SharedPowerV1&){++projectionMsiCalls;if(projectionUnknownAtMsi){projectionUnknownAtMsi=false;sharedPowerKnown=false;}}}
enum class FanReadyDelivery { Idle, Settled, Deferred };
[[maybe_unused]] static uint64_t currentFanPowerGeneration(){return currentPowerGeneration();}
static FanReadyDelivery fanHostScheduleEmergencyResume(uint64_t){++projectionOtherCalls;return FanReadyDelivery::Settled;}
static void steamAppearancePublicReady(uint64_t){++projectionOtherCalls;}
static json sharedPowerLifecycleSnapshot(){
 if(projectionThrow)throw std::runtime_error("projection IO port");
 ymcc::sleeppower::SharedPowerV1 observed;
 const bool known=fixtureProductionReadSharedSystemPower(&observed,sizeof(observed))==1;
 return {{"known",known},{"hardwareWritesAllowed",known&&ymcc::sleeppower::powerAdmitted(observed)}};
}
static void sharedPowerProjectionConsumerFailed(const char*,const ymcc::sleeppower::SharedPowerV1* =nullptr,const uint64_t* =nullptr) noexcept{}
static void projectSharedPowerLifecycle(HWND);

namespace ymcc::batterycharge {static void notifySystemPower(){}}
namespace ymcc::sleeppower::task {static bool allowed(){return false;}}
static void notifySharedMediaDownloadsPower(){}
static void sgOfferStartupResume(const ymcc::sleeppower::SharedPowerV1&){}

struct DiagnosticQueuedMessage {UINT message;WPARAM w;LPARAM l;};
static std::vector<DiagnosticQueuedMessage> diagnosticMessages;
static unsigned diagnosticSuccessfulPosts=0;
static void beginPowerSourceRecovery(HWND){}
static void stopPowerResumeWatchdog(){}
static bool steamAppearanceResumeCommitted(unsigned long long){return true;}
static void deckySidebarResumeAfterIntent(unsigned long long,const char*){}
static void nativeAyaLightingResume(){}
static void ipc_emit(const char*,const json&){}
static BOOL diagnosticPostMessage(HWND,UINT message,WPARAM w,LPARAM l){
 if(message!=WM_POWER_INPUT_RECOVER&&message!=kSharedPowerStateMessage)throw std::runtime_error("unexpected post outside original input/projection fanout");
 diagnosticMessages.push_back({message,w,l});++diagnosticSuccessfulPosts;return TRUE;
}
#define PostMessageW diagnosticPostMessage
static LRESULT diagnosticOriginalCommit(WPARAM w,LPARAM l){[[maybe_unused]] HWND h=g_hwnd;
@@PUBLIC_COMMIT@@
}
@@PUBLIC_PUBLISHER@@
@@PUBLIC_PROJECTION@@
#undef PostMessageW
static LRESULT repairTimer();
static void diagnosticConsumeQueued(){
 if(diagnosticMessages.empty())throw std::runtime_error("no posted message to consume");
 const auto message=diagnosticMessages.front();diagnosticMessages.erase(diagnosticMessages.begin());
 if(message.message==kSharedPowerStateMessage){projectSharedPowerLifecycle(g_hwnd);return;}
 if(message.message!=WM_POWER_INPUT_RECOVER||message.l!=0)throw std::runtime_error("queued input identity changed");
 recoverMessage(message.w);
}
static void diagnosticPumpOnlyScheduled(){
 for(unsigned step=0;step!=8;++step){
  if(!diagnosticMessages.empty())diagnosticConsumeQueued();
  else if(g_sleepPowerRedeliveryArmed.load())repairTimer();
  else return;
 }
 throw std::runtime_error("unexpected unbounded original delivery activity");
}

static LRESULT recoverMessage(WPARAM w){[[maybe_unused]] HWND h=g_hwnd;
@@INPUT_CASE@@
}
static LRESULT repairTimer(){HWND h=g_hwnd;WPARAM w=kSleepPowerRedeliveryTimer;
@@TIMER@@
return 0;
}
#undef SetTimer
#undef KillTimer
#undef SendMessageW
static std::vector<std::string> cases,failures,registeredCases;
static unsigned assertions=0;
static void require(bool value,const char* message){++assertions;if(!value)failures.push_back(cases.back()+": "+message);}
static void settle(){const auto r=g_sleepPowerDispatch.window(PBT_APMQUERYSUSPENDFAILED,++serial);g_sleepPowerDispatch.complete(r,++serial);}
static void clearPending(){@@CLEAR_ROG_PENDING@@}
static bool hasPending(){@@HAS_ROG_PENDING@@}
static void reset(){
 observeReady=false;settle();fixturePowerGeneration+=10;g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;g_inputReady.silent(true);
 keepWriter=false;interruptAt.clear();edge.clear();changed=false;calls.clear();allCalls.clear();publications=repairArms=focusArms=binds=0;rawResult=true;waitEntered=false;rogAttempts=0;
 sharedPowerKnown=true;sharedPowerGeneration=0;sharedPowerAllowed=true;rawReadCount=rawFailAt=0;rawFailDenied=unknownAfterSend=failNextRaw=false;diagnosticMessages.clear();diagnosticSuccessfulPosts=0;failRepairTimer=false;projectionMsiCalls=projectionOtherCalls=0;projectionThrow=false;projectionUnknownAtMsi=false;g_sharedPowerProjectionPending=false;keepDiagnostics.clear();stepFailures.clear();
 g_inputHostPowerAdmissionRevoked=true;g_sleepPowerRedeliveryArmed=false;g_powerInputRecoverRedelivery=0;g_powerResumeCommitRedelivery=0;g_entryTestRecoverBindDeferred=0;g_entryTestRecoverBindDropped=0;clearPending();observeReady=true;
}
static void successor(){
 ++fixturePowerGeneration;g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=false;
 interruptAt.clear();edge.clear();calls.clear();publications=0;rawResult=true;
 recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));
 require(calls==std::vector<std::string>{"raw","rog","face","reset","read","rearm","rescan","stats"},"fresh valid cycle runs the original recovery chain once");
 require(g_inputReady&&publications==1,"fresh valid cycle publishes readiness without an old private latch");
}
static unsigned count(const std::string& value){return static_cast<unsigned>(std::count(allCalls.begin(),allCalls.end(),value));}
static void completeOnce(bool keep,bool rawOk=true){
 require(g_inputReady,"completed scoped recovery restores its software input readiness");
 require(count("screenpads")==1&&count("msi")==1&&count("snapshot")==1,"each actual pre-observer and snapshot is dispatched once");
 require(count("stats")==1&&count("publish")==1&&publications==1,"accepted final stats and publication are not replayed");
 if(keep){
  require(calls==std::vector<std::string>{"raw","stats"}&&count("raw")==1&&focusArms==1&&binds==0&&rogAttempts==0,"keep writer renews physical registration once without OEM or Host recovery");
  require(!g_inputHostPowerAdmissionRevoked,"accepted keep recovery releases the original input admission");
  const auto& diagnostic=keepDiagnostics.empty()?json::object():keepDiagnostics.back();
  require(diagnostic.value("generation",0ULL)==fixturePowerGeneration.load()&&diagnostic.value("rawRegistrationAttempted",false)&&diagnostic.contains("rawRegistrationSucceeded")&&diagnostic.value("rawRegistrationSucceeded",!rawOk)==rawOk&&!diagnostic.value("physicalReportRecoveryProven",true),"keep diagnostic reports actual registration outcome without claiming physical recovery");
 }
 else{require(count("raw")==1&&count("rog")<=2&&count("face")==1&&count("reset")==1&&count("read")==1,"same-generation partial progress does not replay completed raw/state work");require(count("rearm")==1&&count("rescan")==1&&binds==1,"only one actual scoped bind and Host demand complete");}
}
static bool waitFor(const std::function<bool()>& predicate){const auto until=GetTickCount64()+2000;while(!predicate()&&GetTickCount64()<until)Sleep(1);return predicate();}
static bool consumePending(){RogRemapGuardScope guard;RogRemapBoundaryScope boundary;return guard.acquired&&boundary.entered&&rogRemapConsumeDeferredRecover();}
int main(int argc,char** argv){
 const std::string selected=argc==2?argv[1]:"";
 const auto run=[&](const std::string& name,const std::function<void()>& body){
  registeredCases.push_back(name);
  const bool keepSelection=(selected=="unknown-delivery"&&name.rfind("unknown-delivery-",0)==0)||selected=="keep-resubscription"&&(name=="normal"||name=="registration-failure"||name=="fault-raw"||name.rfind("keep-",0)==0||name.rfind("fault-keep-",0)==0);
  if(!selected.empty()&&selected!=name&&!keepSelection&&(selected!="input-faults"||name.rfind("fault-",0)!=0))return;
  reset();cases.push_back(name);body();
 };
 run("normal",[]{successor();});
 run("registration-failure",[]{rawResult=false;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration));require(!g_inputReady&&publications==1,"registration failure remains truthful");require(calls==std::vector<std::string>{"raw","rog","face","reset","read","stats"},"registration failure grants no Host rearm or rescan");successor();});
 for(const std::string initial:{"generation","phase","exit"})run("initial-"+initial,[&]{const auto captured=fixturePowerGeneration.load();if(initial=="generation")++fixturePowerGeneration;if(initial=="phase")g_powerLifecycle=PowerLifecycle::Suspended;if(initial=="exit")g_exitRequested=true;recoverMessage(static_cast<WPARAM>(captured));require(calls.empty()&&publications==0,"invalid original message performs no recovery");successor();});
 const std::vector<std::string> stages={"raw","rog","face","reset","read","rearm","rescan","stats"};
 for(const auto& stage:stages)for(const std::string change:{"generation","phase","exit"})run(stage+"-"+change,[&]{
  interruptAt=stage;edge=change;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration));
  const auto position=std::find(stages.begin(),stages.end(),stage);
  const std::vector<std::string> expected(stages.begin(),position+1);
  require(calls==expected,"retired recovery stops at the interrupted owner boundary");
  require(publications==0,"retired recovery cannot publish a successor input receipt");
  require(g_inputReady.load()==(change!="phase"),"retired recovery preserves the current owner's input readiness");
  successor();
 });

 const std::vector<std::string> fullStages={"screenpads","msi","snapshot","raw","rog","face","reset","read","input-ready","rearm","rescan","stats","publish"};
 const std::vector<std::string> keepStages={"screenpads","msi","snapshot","raw","input-ready","keep-diagnostic","focus-cancel","focus-arm","stats","publish"};
 for(const bool keep:{false,true}){
  const std::string mode=keep?"keep":"full";
  run(mode+"-pending-before",[&]{keepWriter=keep;g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));require(allCalls.empty(),"actual pending before phase change blocks every input consumer");require(g_powerInputRecoverRedelivery==fixturePowerGeneration&&repairArms==1,"pending queued message retains same-generation input debt");for(unsigned i=0;i<20;i++)recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));require(allCalls.empty()&&repairArms==1,"pending storm cannot consume work or postpone repair");settle();repairTimer();completeOnce(keep);require(g_powerInputRecoverRedelivery==0&&!g_sleepPowerRedeliveryArmed,"completed input continuation retires debt and timer");});
  for(const auto& stage:keep?keepStages:fullStages)for(const std::string change:{"pending","generation","phase","exit","reentrant","reentrant-successor"})run(mode+"-"+stage+"-"+change,[&]{
   keepWriter=keep;interruptAt=stage;edge=change;const auto previous=fixturePowerGeneration.load();recoverMessage(static_cast<WPARAM>(previous));
   if(change=="pending"){require(changed&&g_sleepPowerDispatch.pendingSuspend(),"actual Dispatch receives the selected mid-call pending edge");require(allCalls.back()==stage,"pending edge stops the next independent input step");settle();repairTimer();completeOnce(keep);}
   else if(change=="reentrant"){completeOnce(keep);repairTimer();completeOnce(keep);}
   else if(change=="reentrant-successor"){require(changed,"new-generation reentrant event reached selected boundary");calls.clear();allCalls.clear();keepDiagnostics.clear();publications=focusArms=binds=0;rogAttempts=0;repairTimer();completeOnce(keep);require(fixturePowerGeneration>previous,"old active frame kept the successor generation intact");}
   else if(change=="phase"){require(allCalls.back()==stage,"non-Ready old frame stops at selected boundary");g_powerLifecycle=PowerLifecycle::Ready;recoverMessage(static_cast<WPARAM>(previous));completeOnce(keep);}
   else if(change=="generation"){require(allCalls.back()==stage,"old-generation frame cannot enter later input work");calls.clear();allCalls.clear();keepDiagnostics.clear();publications=focusArms=binds=0;rogAttempts=0;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));completeOnce(keep);}
   else require(allCalls.back()==stage,"exiting frame stops without later input work");
  });
  run(mode+"-duplicates",[&]{keepWriter=keep;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));for(unsigned i=0;i<20;i++)recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));completeOnce(keep);});
 }
 for(const std::string change:{"pending","generation","phase","exit","device"})run("actual-rog-wait-"+change,[&]{
  g_rogRemapBoundaryMx.lock();std::thread worker([&]{recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));});const bool entered=waitFor([]{return waitEntered.load();});
  require(entered,"actual bounded ROG waiter reaches the held real timed mutex");
  if(change=="pending")g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});else if(change=="generation")++fixturePowerGeneration;else if(change=="phase")g_powerLifecycle=PowerLifecycle::Suspended;else if(change=="exit")g_exitRequested=true;else ++g_rogRemapDevGen;
  g_rogRemapBoundaryMx.unlock();worker.join();require(binds==0,"invalid original ROG waiter cannot bind after the session lock becomes available");
  if(change=="pending"){require(g_powerInputRecoverRedelivery==fixturePowerGeneration,"pending lock waiter retains UI continuation");settle();repairTimer();completeOnce(false);}
  else if(change=="generation"){calls.clear();allCalls.clear();publications=binds=0;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));completeOnce(false);}
 });
 for(const std::string change:{"pending","generation","device","exit"})run("actual-rog-deferred-"+change,[&]{
  bool dispatched=false;g_rogRemapBoundaryMx.lock();std::thread waiter([&]{(void)rogRecoverBindSerialized("resume",fixturePowerGeneration.load(),&dispatched);});waiter.join();g_rogRemapBoundaryMx.unlock();
  require(dispatched&&hasPending()&&g_entryTestRecoverBindDeferred==1,"original800ms budget retains a coherent device and power pending owner");
  if(change=="pending"){g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});require(!consumePending()&&hasPending()&&binds==0,"pending same-generation deferred owner is held without binding or being lost");settle();require(consumePending()&&!hasPending()&&binds==1,"same owner consumes the deferred bind exactly once after cancellation");}
  else{if(change=="generation")++fixturePowerGeneration;else if(change=="device")++g_rogRemapDevGen;else g_exitRequested=true;require(!consumePending()&&!hasPending()&&binds==0&&g_entryTestRecoverBindDropped==1,"retired deferred owner cannot bind another power or device generation");}
 });

 for(const std::string stage:{"raw","rog","rog-bind","face","reset","read","screenpads","msi","snapshot","stats","publish"})run("fault-"+stage,[&]{
  interruptAt=stage;edge="throw";recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));
  require(changed,"selected actual input leaf fault is reached");
  require(!fixtureBoundaryHeld&&!tls_rogRemapActive,"fault releases original ROG boundary and thread-recursion owner");
  if(stage=="raw"||stage=="rog"||stage=="rog-bind"||stage=="face"||stage=="reset"||stage=="read")require(!g_inputReady&&count("rearm")==0,"uncertain device step cannot grant Host rearm");
  const auto before=calls;const auto beforeAll=allCalls;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));
  require(calls==before&&allCalls==beforeAll,"uncertain accepted input work is not automatically replayed");
  ++fixturePowerGeneration;interruptAt.clear();edge.clear();changed=false;calls.clear();allCalls.clear();publications=focusArms=binds=0;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));completeOnce(false);
 });
 for(const std::string stage:{"focus-arm","stats"})run("fault-keep-"+stage,[&]{
  keepWriter=true;interruptAt=stage;edge="throw";recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));require(changed,"selected keep-writer fault is reached");
  require(calls==std::vector<std::string>{"raw","stats"},"keep-writer exception does not start full device recovery");
  ++fixturePowerGeneration;interruptAt.clear();edge.clear();changed=false;calls.clear();allCalls.clear();publications=focusArms=binds=0;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));completeOnce(true);
 });
 for(const std::string outcome:{"false","throw"})run("keep-registration-"+outcome,[&]{
  keepWriter=true;rawResult=outcome!="false";
  if(outcome=="throw"){interruptAt="raw";edge="throw";}
  recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));completeOnce(true,false);
  require(g_inputReady&&!g_inputHostPowerAdmissionRevoked,"keep registration failure preserves original XInput fail-open admission");
  require(stepFailures.size()==(outcome=="throw"?1u:0u),"only a thrown registration records the original step failure");
  for(unsigned i=0;i<20;i++)recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));
  completeOnce(true,false);
 });
 for(const std::string initial:{"generation","phase","exit","unknown-shared","stale-shared"})run("keep-initial-"+initial,[&]{
  keepWriter=true;const auto captured=fixturePowerGeneration.load();
  if(initial=="generation")++fixturePowerGeneration;
  else if(initial=="phase")g_powerLifecycle=PowerLifecycle::Suspended;
  else if(initial=="exit")g_exitRequested=true;
  else if(initial=="unknown-shared")sharedPowerKnown=false;
  else sharedPowerGeneration=captured-1;
  recoverMessage(static_cast<WPARAM>(captured));
  require(calls.empty()&&allCalls.empty()&&publications==0,"invalid keep message cannot register or consume OEM or Host work");
 });
 run("keep-retained-host-state",[&]{
  keepWriter=true;auto* const previous=g_inputHostProcess;const bool active=g_inputHostActive,prepared=g_inputHostPrepared,neutralized=g_inputHostNeutralized;
  auto* const retained=reinterpret_cast<void*>(static_cast<UINT_PTR>(0x1234));g_inputHostProcess=retained;g_inputHostActive=true;g_inputHostPrepared=true;g_inputHostNeutralized=false;
  recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));completeOnce(true);
  require(g_inputHostProcess==retained&&g_inputHostActive&&g_inputHostPrepared&&!g_inputHostNeutralized,"retained in-memory Host facts are unchanged; no actual Host is started");
  g_inputHostProcess=previous;g_inputHostActive=active;g_inputHostPrepared=prepared;g_inputHostNeutralized=neutralized;
 });

 run("unknown-delivery-first-message",[]{
  keepWriter=true;diagnosticMessages.clear();diagnosticSuccessfulPosts=0;
  diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));
  require(diagnosticSuccessfulPosts==1&&diagnosticMessages.size()==1,"actual commit fanout successfully posts one input recovery message");
  sharedPowerKnown=false;diagnosticConsumeQueued();
  require(calls.empty(),"temporary UNKNOWN performs no input or Host operation");
  sharedPowerKnown=true;diagnosticPumpOnlyScheduled();
  require(count("raw")==1&&calls==std::vector<std::string>{"raw","stats"},"first successful post survives temporary UNKNOWN without a new commit");
 });
 run("unknown-delivery-armed-timer",[]{
  keepWriter=true;diagnosticMessages.clear();diagnosticSuccessfulPosts=0;
  diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));
  require(diagnosticSuccessfulPosts==1&&diagnosticMessages.size()==1,"actual commit posts input before a pending transition");
  g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});
  diagnosticConsumeQueued();
  require(g_powerInputRecoverRedelivery==fixturePowerGeneration.load()&&g_sleepPowerRedeliveryArmed.load()&&calls.empty(),"actual pending consumer retains same-generation debt and arms its original timer");
  settle();sharedPowerKnown=false;
  if(!g_sleepPowerRedeliveryArmed.load())throw std::runtime_error("refuse to invoke an unarmed timer");
  repairTimer();
  require(calls.empty(),"UNKNOWN original timer executes no input or Host action");
  sharedPowerKnown=true;diagnosticPumpOnlyScheduled();
  require(count("raw")==1&&calls==std::vector<std::string>{"raw","stats"},"previously armed input debt survives UNKNOWN timer and resumes without a new commit");
 });


 run("unknown-delivery-permanent-unknown-parks",[]{
  keepWriter=true;diagnosticMessages.clear();diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));sharedPowerKnown=false;diagnosticConsumeQueued();diagnosticPumpOnlyScheduled();
  require(calls.empty()&&publications==0,"permanent UNKNOWN never grants input or Host permission");require(!g_sleepPowerRedeliveryArmed&&repairArms<=3&&repairArms>0,"permanent UNKNOWN exhausts finite existing timer reservations and parks");require(g_powerInputRecoverRedelivery==fixturePowerGeneration,"parked UNKNOWN keeps the original notification obligation");
  sharedPowerKnown=true;productionPublishSharedPowerLifecycle("fixture-native-ready-after-unknown");diagnosticPumpOnlyScheduled();completeOnce(true);require(g_powerInputRecoverRedelivery==0,"actual Ready publisher/projection settles parked notification");
 });
 run("unknown-delivery-known-denied-parks",[]{
  keepWriter=true;diagnosticMessages.clear();sharedPowerAllowed=false;diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));diagnosticConsumeQueued();
  require(calls.empty()&&repairArms==0&&!g_sleepPowerRedeliveryArmed,"known-denied is parked without UNKNOWN polling");require(g_powerInputRecoverRedelivery==fixturePowerGeneration,"known-denied retains committed input duty");productionPublishSharedPowerLifecycle("fixture-known-denied");diagnosticPumpOnlyScheduled();require(calls.empty()&&projectionMsiCalls==0,"whole denied projection cannot grant Ready consumers");
  sharedPowerAllowed=true;productionPublishSharedPowerLifecycle("fixture-real-ready");diagnosticPumpOnlyScheduled();completeOnce(true);
 });
 run("unknown-delivery-nonready-parks",[]{
  keepWriter=true;diagnosticMessages.clear();diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));g_powerLifecycle=PowerLifecycle::Suspended;diagnosticConsumeQueued();require(calls.empty()&&!g_sleepPowerRedeliveryArmed&&g_powerInputRecoverRedelivery==fixturePowerGeneration,"same generation NonReady keeps duty without polling");g_powerLifecycle=PowerLifecycle::Ready;productionPublishSharedPowerLifecycle("fixture-native-ready");diagnosticPumpOnlyScheduled();completeOnce(true);
 });
 run("unknown-delivery-ready-bit256-refires",[]{
  keepWriter=true;diagnosticMessages.clear();diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));sharedPowerKnown=false;diagnosticConsumeQueued();sharedPowerKnown=true;
  projectionUnknownAtMsi=true;projectionThrow=true;productionPublishSharedPowerLifecycle("fixture-first-projection");diagnosticConsumeQueued();
  require(projectionMsiCalls==1&&g_sharedPowerProjectionPending&&calls.empty(),"first complete projection claims MSI256 and retains read-only projection retry");sharedPowerKnown=true;projectionThrow=false;
  projectSharedPowerLifecycle(g_hwnd);require(count("raw")==1&&projectionMsiCalls==1,"same-publication actual Ready projection refires debt without replaying MSI256");diagnosticPumpOnlyScheduled();completeOnce(true);
 });
 for(const bool keep:{false,true})for(const std::string point:{"screenpads","raw","input-ready","stats"})run(std::string("unknown-delivery-mid-")+(keep?"keep-":"full-")+point,[&]{
  keepWriter=keep;interruptAt=point;edge="unknown";recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));require(changed&&!allCalls.empty(),"actual selected stage reaches moving Native authority");require(g_powerInputRecoverRedelivery==fixturePowerGeneration,"mid-stage UNKNOWN retains notification");sharedPowerKnown=true;diagnosticPumpOnlyScheduled();completeOnce(keep);
 });
 run("unknown-delivery-retired-generation",[]{keepWriter=true;sharedPowerKnown=false;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));++fixturePowerGeneration;diagnosticPumpOnlyScheduled();require(calls.empty()&&g_powerInputRecoverRedelivery==0&&!g_sleepPowerRedeliveryArmed,"unknown sender retires earlier generation debt without actions or new retries");});
 run("unknown-delivery-exit",[]{keepWriter=true;sharedPowerKnown=false;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));g_exitRequested=true;diagnosticPumpOnlyScheduled();require(calls.empty()&&!g_sleepPowerRedeliveryArmed,"exit cancels timer delivery without input permission");});

 run("unknown-delivery-budget-then-posted-same-publication",[]{
  keepWriter=true;diagnosticMessages.clear();diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));sharedPowerKnown=false;diagnosticConsumeQueued();sharedPowerKnown=true;
  projectionUnknownAtMsi=true;productionPublishSharedPowerLifecycle("fixture-already-posted-native-ready");require(diagnosticMessages.size()==1,"actual Native publisher retains one real queued Ready message");
  repairTimer();require(projectionMsiCalls==1&&calls.empty(),"original armed timer consumes Ready projection256 before its posted copy");
  for(unsigned retry=0;retry<3&&g_sleepPowerRedeliveryArmed.load();++retry)repairTimer();
  require(!g_sleepPowerRedeliveryArmed&&repairArms<=3&&g_powerInputRecoverRedelivery==fixturePowerGeneration&&diagnosticMessages.size()==1,"unknown exhausts timer budget while original posted Ready copy remains queued");const auto revision=g_sharedPowerPublicationRevision.load();
  sharedPowerKnown=true;diagnosticConsumeQueued();require(g_sharedPowerPublicationRevision==revision&&count("raw")==1&&projectionMsiCalls==1,"same-publication actual posted Ready settles parked duty without MSI replay or new commit");completeOnce(true);
 });
 run("unknown-delivery-newer-debt-preserved",[]{g_powerInputRecoverRedelivery=fixturePowerGeneration.load()+1;sleepPowerRedeliverCommitted(g_hwnd);require(g_powerInputRecoverRedelivery==fixturePowerGeneration.load()+1&&calls.empty(),"current sender never clears or consumes newer debt");sharedPowerKnown=false;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));require(g_powerInputRecoverRedelivery==fixturePowerGeneration.load()+1&&!g_sleepPowerRedeliveryArmed,"old unknown input cannot overwrite newer debt or arm old retry");});
 run("unknown-delivery-timer-failure",[]{keepWriter=true;diagnosticMessages.clear();diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));failRepairTimer=true;sharedPowerKnown=false;diagnosticConsumeQueued();require(!g_sleepPowerRedeliveryArmed&&g_powerInputRecoverRedelivery==fixturePowerGeneration&&calls.empty(),"failed timer retains notification and grants no action");failRepairTimer=false;sharedPowerKnown=true;productionPublishSharedPowerLifecycle("fixture-native-ready-after-timer-failure");diagnosticPumpOnlyScheduled();completeOnce(true);});
 run("unknown-delivery-duplicates-dont-renew-budget",[]{keepWriter=true;sharedPowerKnown=false;recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));for(unsigned i=0;i<30;++i)recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));while(g_sleepPowerRedeliveryArmed.load())repairTimer();const auto used=repairArms;for(unsigned i=0;i<30;++i)recoverMessage(static_cast<WPARAM>(fixturePowerGeneration.load()));require(used<=3&&repairArms==used&&!g_sleepPowerRedeliveryArmed&&calls.empty(),"duplicate current notifications cannot renew exhausted UNKNOWN retry budget");});

 for(const unsigned at:{2u,3u})for(const bool denied:{false,true})run(std::string("unknown-delivery-sender-read-")+std::to_string(at)+(denied?"-denied":"-unknown"),[&]{
  keepWriter=true;diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});diagnosticConsumeQueued();settle();rawReadCount=0;rawFailAt=at;rawFailDenied=denied;repairTimer();
  require(calls.empty()&&g_powerInputRecoverRedelivery==fixturePowerGeneration,"timer freshness read failure retains notification without actions");require(g_sleepPowerRedeliveryArmed.load()!=denied,"only actual UNKNOWN freshness failure schedules bounded delivery retry");
  rawFailAt=0;if(denied)productionPublishSharedPowerLifecycle("fixture-real-ready-after-denied-freshness");diagnosticPumpOnlyScheduled();completeOnce(true);
 });
 run("unknown-delivery-sender-post-send-read",[]{
  keepWriter=true;diagnosticOriginalCommit(0,static_cast<LPARAM>(fixturePowerGeneration.load()));g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,++serial,[](const auto&){return false;});diagnosticConsumeQueued();settle();unknownAfterSend=true;repairTimer();
  require(count("raw")==1&&g_powerInputRecoverRedelivery==fixturePowerGeneration&&g_sleepPowerRedeliveryArmed,"post-send UNKNOWN keeps duty after completed consumer without losing the timer");diagnosticPumpOnlyScheduled();completeOnce(true);
 });

 run("fault-two-waiters",[&]{
  g_rogRemapBoundaryMx.lock();bool oldDispatched=false,newDispatched=false;const auto first=fixturePowerGeneration.load();
  std::thread older([&]{(void)rogRecoverBindSerialized("resume",first,&oldDispatched);});require(waitFor([]{return waitEntered.load();}),"older actual waiter starts before ownership changes");
  ++fixturePowerGeneration;waitEntered=false;const auto next=fixturePowerGeneration.load();std::thread newer([&]{(void)rogRecoverBindSerialized("resume",next,&newDispatched);});require(waitFor([]{return waitEntered.load();}),"new owner enters the same original device wait");
  older.join();newer.join();g_rogRemapBoundaryMx.unlock();
  require(!oldDispatched&&newDispatched&&g_entryTestRecoverBindDeferred==1,"only the current waiter may publish coherent deferred ownership");
  require(hasPending()&&consumePending()&&!hasPending()&&binds==1,"new request survives old waiter completion and consumes once");
 });
 if(cases.empty()){std::cerr<<"UNKNOWN_INPUT_RESUME_CASE\n";return 2;}
 std::cout<<json{{"cases",cases},{"registeredCases",registeredCases},{"selectedCase",selected},{"assertions",assertions},{"failures",failures},{"original29Retained",true},{"actualWholeInputCase",true},{"actualGamepadBody",true},{"actualRogWaitAndDeferredOwner",true},{"actualDispatchAndInputRedeliveryTimer",true},{"sharedPowerReaderIsMemoryPort",true},{"deviceHostScreenpadMsiFocusIoInert",true},{"physicalRecoveryAccepted",false}}.dump(2)<<'\n';return failures.empty()?0:1;
}
