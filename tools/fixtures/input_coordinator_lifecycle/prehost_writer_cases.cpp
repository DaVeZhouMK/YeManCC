// Supplemental to controller.coordinator-lifecycle; NOT a replacement for its
// full Native/managed consumer. Actual production runtime header and admission
// class execute against explicit cached-ownership/source/Start effect ports.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>
#include "json.hpp"
#include "virtual_start_protection.h"
#include "virtual_recovery_intent.h"
using json=nlohmann::json;
static std::uint64_t tick=1000;
static std::uint64_t fixtureTick(){return tick;}
#define GetTickCount64 fixtureTick
struct InputHostMutex:std::mutex { bool ownedByCurrentThread()const{return true;} };
static InputHostMutex g_inputHostMx;
static std::mutex g_msiNativeForegroundMx;
struct FakeWriter { struct Attempt{std::string attemptId="exact-fixture-native-writer";} value; const Attempt* attempt()const{return &value;} };
struct NativeLease { bool active=false,postHideCycle=false;HANDLE cycleProcess=nullptr;std::unique_ptr<FakeWriter> nativeRefreshWriter; };
static NativeLease g_msiNativeForegroundLease;
static std::atomic<bool> g_msiNativeForegroundCyclePending{false},g_postHideCycleOwned{false};
static std::atomic<bool> g_exitRequested{false},g_inputCaptureStop{false},g_inputHostPowerAdmissionRevoked{false};
static std::atomic<bool> g_inputHostRuntimeRecoveryPending{false},g_inputHostVirtualIntentWriters{false};
static std::atomic<bool> g_inputHostRecoveryTerminalLogged{false},g_externalPriorityVirtualSuspended{false},g_inputHostFaultHoldsStart{false};
static std::atomic<int> g_inputHostRecoveryEpisodeAttempts{0},g_inputHostRecoveryFallbackState{0};
static std::atomic<std::uint64_t> g_inputHostVirtualIntentRevision{7};
static ymcc::inputHost::VirtualRecoveryIntent g_inputHostRecoveryIntent;
static json demandInput;
static const std::string demandSession="fixture-owner-session";
enum class PowerLifecycle{Ready,Suspending};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::uint64_t power=50;
static bool exitFence=false;
static std::uint64_t currentPowerGeneration(){return power;}
static bool inputHostExitFenceRaised(){return exitFence;}
struct Settings{bool enabled=true,virtualTargetEnabled=true;};static Settings g_realStickTest;
static std::string configured="steamdeck";
static std::string inputHostConfiguredPersona(){return configured;}
static ymcc::VirtualStartProtection g_virtualStartProtection;
static std::uint64_t protectionTickOffset=0;
static std::uint64_t inputHostStartProtectionNow(){return tick+protectionTickOffset;}
static std::uint64_t g_inputHostEpoch=0;
static std::string g_inputHostRunId,g_inputHostHostInstanceId,g_inputHostBoundPersona;
static HANDLE g_inputHostProcess=nullptr,g_inputHostPrimaryThread=nullptr,g_inputHostServerPipe=nullptr;
static bool g_inputHostHello=false,g_inputHostPrepared=false,g_inputHostNeutralized=false,g_inputHostActive=false;
static bool g_inputHostReleaseQuarantined=false,g_inputHostStartBlocked=false,g_inputHostTransportFault=false;
struct VisibilityJournal{bool active=false;};static VisibilityJournal g_hidHideVisibilityJournal;
static bool cleanupProven=true,pipeReturned=true,exactTargetReturned=true,sourceHeld=false,startResult=true;
static bool inputHostOwnedCleanupProvenLocked(){return cleanupProven;}
static bool inputHostPipeRetirementProvenLocked(){return pipeReturned;}
static bool inputHostTargetReturnRuntimeSettledLocked(const std::string&,std::uint64_t){return exactTargetReturned;}
namespace ymcc::inputHost::targetReturnRuntime {
struct Record{std::vector<int> obligations;};
struct Session{Record value;const Record* record()const{return &value;}};
struct Ledger{std::shared_ptr<Session> session=std::make_shared<Session>();std::vector<int> targets,proven;};
static Ledger state;
}
static unsigned rolling=0;
static int inputHostRecoveryRollingCountLocked(std::uint64_t){return static_cast<int>(rolling);}
static bool inputHostRecoveryBudgetExhaustedLocked(std::uint64_t){return rolling>=2||g_inputHostRecoveryEpisodeAttempts>=2;}
namespace ymcc::inputHost::switchRecoveryRuntime {
static bool heldLocked(std::uint64_t){return sourceHeld||g_externalPriorityVirtualSuspended.load();}
}
namespace ymcc::inputHost::queuedPlayerStart {
struct Work{std::uint64_t request=0,power=0,deadline=0;std::string persona,reason;bool bound=false;};
static bool startReason(const std::string& reason){return reason=="initial-target-admission"||reason=="persona-transition";}
static const char* stale(const Work& work){
 const auto view=g_virtualStartProtection.view(tick);
 if(view.requestId!=work.request||view.persona!=work.persona||configured!=work.persona||power!=work.power||tick>=work.deadline||!view.locked)return "retired-port";
 return nullptr;
}
} // namespace ymcc::inputHost::queuedPlayerStart
static std::vector<json> logs;
static void appendNativeLifecycleLog(const char* event,const json& detail){logs.push_back({{"event",event},{"detail",detail}});}
static std::vector<std::string> queue;
static bool inputHostQueueLifecycleWork(const char* work){queue.emplace_back(work);return true;}
static void inputHostRuntimeMsiReturnCaptureLocked(){}
static unsigned stopCalls=0,startCalls=0,charges=0,oldTimeoutObservations=0;
static void inputHostStopLocked(const char*){++stopCalls;}
static bool msiClawKnownBoardAuthority(){return false;}
static bool inputHostCanCreateTargetWithoutPhysicalSource(){return false;}
static std::atomic<unsigned> g_xinputTopologyEpoch{3},g_xinputBoundaryScanEpoch{3},g_xinputSlotFullScanEpochStart{0};
static std::atomic<bool> g_xinputPreTargetBoundaryScanned{true},g_xinputSlotFullScanOnce{false};
static unsigned sourceMask=1;
static unsigned xinputPhysicalConnectedMask(){return sourceMask;}
static ymcc::VirtualStartProtection::Admission inputHostStartProtectionConsume(const std::string& persona){return g_virtualStartProtection.consume(persona,tick);}
static std::uint64_t advanceDuringTimeoutRecord=0;
static void inputHostRecordSwitchTimeout(const ymcc::VirtualStartProtection::View&){
 ++oldTimeoutObservations;if(advanceDuringTimeoutRecord)tick=advanceDuringTimeoutRecord;
}
static int inputHostRecoveryChargeAttemptLocked(std::uint64_t){++charges;++rolling;return ++g_inputHostRecoveryEpisodeAttempts;}
static void inputHostStartProtectionLog(const char*,const char*){}
static std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0,g_inputHostSwitchRecoveryStartRequestId=0,g_inputHostSwitchRecoveryStartPower=0;
static void inputHostStartProtectionFinish(std::uint64_t ticket,const char* outcome,const char*){(void)g_virtualStartProtection.beginVerification(ticket,outcome,tick);}
static bool inputHostStart(){++startCalls;return startResult;}

static unsigned terminalCalls=0,terminalActions=0;
static bool terminalCaptureRefused=false;
static void inputHostRecoveryTerminalLocked(const char*) {
 ++terminalCalls;
 if(terminalCaptureRefused)return;
 if(g_inputHostRecoveryTerminalLogged.exchange(true))return;
 ++terminalActions;g_inputHostFaultHoldsStart=true;
}

#include "virtual_runtime_recovery_runtime.h"
#ifdef YMCC_FIXTURE_OLD_SUCCESSOR_BINDING
static void inputHostRuntimeRecoveryBindStartTupleLocked(std::uint64_t,std::uint64_t,const std::string&,bool) noexcept {}
#endif


static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void maintain(){std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryMaintenanceLocked();}
static void drain(){auto pending=std::move(queue);queue.clear();for(const auto& token:pending){const auto colon=token.find(':');inputHostPerformRuntimeRecovery(std::stoull(token.substr(colon+1)));}}
static ymcc::inputHost::queuedPlayerStart::Work prepare(const std::string& persona="steamdeck"){
 terminalCalls=terminalActions=0;terminalCaptureRefused=false;
 tick=1000;power=50;configured=persona;g_realStickTest={};exitFence=false;protectionTickOffset=0;
 g_virtualStartProtection.~VirtualStartProtection();new(&g_virtualStartProtection)ymcc::VirtualStartProtection;
 g_exitRequested=false;g_inputCaptureStop=false;g_inputHostPowerAdmissionRevoked=false;g_powerLifecycle=PowerLifecycle::Ready;
 g_inputHostRuntimeRecoveryPending=false;g_inputHostVirtualIntentWriters=false;g_inputHostVirtualIntentRevision=7;
 g_inputHostRecoveryTerminalLogged=false;g_externalPriorityVirtualSuspended=false;g_inputHostFaultHoldsStart=false;
 g_inputHostRecoveryEpisodeAttempts=0;g_inputHostRecoveryFallbackState=0;
 g_inputHostEpoch=0;g_inputHostRunId.clear();g_inputHostHostInstanceId.clear();g_inputHostBoundPersona.clear();
 g_inputHostProcess=nullptr;g_inputHostPrimaryThread=nullptr;g_inputHostServerPipe=nullptr;
 g_inputHostHello=false;g_inputHostPrepared=false;g_inputHostNeutralized=false;g_inputHostActive=false;
 g_inputHostReleaseQuarantined=false;g_inputHostStartBlocked=false;g_inputHostTransportFault=false;g_hidHideVisibilityJournal.active=false;
 cleanupProven=pipeReturned=exactTargetReturned=true;sourceHeld=false;startResult=true;
 rolling=0;charges=stopCalls=startCalls=oldTimeoutObservations=0;queue.clear();logs.clear();advanceDuringTimeoutRecord=0;
 g_xinputTopologyEpoch=3;g_xinputBoundaryScanEpoch=3;g_xinputPreTargetBoundaryScanned=true;g_xinputSlotFullScanOnce=false;sourceMask=1;
 g_inputHostSwitchRecoveryStartDeadline=g_inputHostSwitchRecoveryStartRequestId=g_inputHostSwitchRecoveryStartPower=0;
 ymcc::inputHost::targetReturnRuntime::state={};ymcc::inputHost::runtimeRecovery::state={};
 g_msiNativeForegroundLease={};g_msiNativeForegroundLease.active=true;g_msiNativeForegroundLease.cycleProcess=reinterpret_cast<HANDLE>(1);
 g_msiNativeForegroundLease.nativeRefreshWriter=std::make_unique<FakeWriter>();g_msiNativeForegroundCyclePending=true;g_postHideCycleOwned=false;
 g_inputHostRecoveryIntent.~VirtualRecoveryIntent();new(&g_inputHostRecoveryIntent)ymcc::inputHost::VirtualRecoveryIntent;
 demandInput={{"outputTarget",{{"persona",persona},{"buttonMappingEnabled",true},{"gyroEnabled",false},{"revision",1}}},{"gyroMotion",{{"enabled",false},{"revision",1}}}};
 g_inputHostRecoveryIntent.configure(demandInput,demandSession,true,true,persona);
 const auto a=g_virtualStartProtection.admit(persona,tick);
 return {a.requestId,power,tick+30000,persona,"initial-target-admission",true};
}
static void returned(){g_msiNativeForegroundLease={};g_msiNativeForegroundCyclePending=false;}
static void offered(const ymcc::inputHost::queuedPlayerStart::Work& work){inputHostRuntimeRecoveryObserveNativeWait(work);require(ymcc::inputHost::runtimeRecovery::state.pending,"real native writer defer is recorded");}
static unsigned cases=0;
static void done(const char* name){++cases;std::cout<<"PASS "<<name<<'\n';}

static void lateTerminalTransportCases() {
 for(const auto* persona:{"steamdeck","dualsense-edge","elite"}) {
  for(const auto* finalPhase:{"success","failure","expired"}) {
   const auto w=prepare(persona);returned();
   tick=std::strcmp(finalPhase,"expired")==0?26000:2000;
   require(g_virtualStartProtection.beginVerification(w.request,finalPhase==std::string("failure")?"failure":"success",tick),"original execution has a candidate");
   if(std::strcmp(finalPhase,"expired")!=0) {
    tick+=10000;
    require(g_virtualStartProtection.finishVerification(w.request,finalPhase,tick),"original terminal completes without changing its request");
   }
   tick=33000;const auto original=g_virtualStartProtection.view(tick);
   require(original.phase==finalPhase&&!original.locked,"original settled phase is retained before real runtime fault");
   g_inputHostEpoch=7;g_inputHostRunId="late-working-host";g_inputHostHostInstanceId="host-instance-7";
   g_inputHostBoundPersona=persona;g_inputHostProcess=reinterpret_cast<HANDLE>(1);
   g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;
   g_inputHostTransportFault=true;
   {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryOfferLocked("SUBMIT_FRAME",42,ERROR_BROKEN_PIPE);}
   require(g_inputHostRuntimeRecoveryPending&&ymcc::inputHost::runtimeRecovery::state.requestId==w.request,
    "later real transport fault must not depend on earlier player success classification");
   maintain();drain();
   require(stopCalls==1&&startCalls==0&&charges==0,"safety Stop precedes real returned-resource proof");
   // Cached ports model the existing owner completing that exact Stop. No
   // product return predicate or production device action is replaced here.
   g_inputHostEpoch=0;g_inputHostRunId.clear();g_inputHostHostInstanceId.clear();g_inputHostProcess=nullptr;
   g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=false;
   tick+=1001;maintain();drain();const auto recovery=g_virtualStartProtection.view(tick);
   require(startCalls==1&&charges==1&&recovery.requestId!=w.request&&recovery.persona==persona,
    "returned current demand gets a distinct bounded recovery without reviving the expired ticket");
   require(oldTimeoutObservations==(std::strcmp(finalPhase,"expired")==0?1u:0u),"old timeout observation is preserved exactly when applicable");
   require(recovery.locked&&recovery.remainingMs==30000,"recovery uses its own original thirty-second period");
   tick+=1001;maintain();drain();require(startCalls==1&&stopCalls==1&&charges==1,"duplicate observation cannot repeat Stop or Start");
   if(std::strcmp(finalPhase,"success")!=0) {
    const auto failedRecovery=recovery.requestId;
    ++tick;
    require(g_virtualStartProtection.beginVerification(failedRecovery,finalPhase==std::string("failure")?"failure":"success",tick),"recovery candidate belongs to its distinct ticket");
    if(std::strcmp(finalPhase,"failure")==0) {
     tick+=10000;
     require(g_virtualStartProtection.finishVerification(failedRecovery,"failure",tick),"failed recovery has its own immutable final");
    } else tick+=31000;
    const auto failed=g_virtualStartProtection.view(tick);
    require(failed.requestId==failedRecovery&&failed.phase==finalPhase,"later recovery terminal is distinct from the initial incident ticket");
    maintain();drain();
    require(startCalls==1&&charges==1&&g_inputHostRuntimeRecoveryPending,"failed recovery retains ON demand during its original backoff");
    const auto retryAt=ymcc::inputHost::runtimeRecovery::state.retryAt;
    require(retryAt>tick,"failure schedules a bounded action backoff instead of immediate repeated Start");
    tick=retryAt+1;
    // Existing rolling window is30s; advancing past the60s backoff expires
    // that cached rolling port. The episode count is deliberately NOT reset.
    rolling=0;maintain();drain();
    const auto successor=g_virtualStartProtection.view(tick);
    require(startCalls==2&&charges==2&&stopCalls==1&&g_inputHostRecoveryEpisodeAttempts==2,
     "failed recovery must continue under shared counters without refund or duplicate Stop");
    require(successor.requestId!=failedRecovery&&successor.requestId!=w.request&&successor.persona==persona&&successor.remainingMs==30000,
     "persistent wave admission must bind the current recovery ticket rather than the initial fault ticket");
   }
  }
 }
 done("three-personas-success-failure-expired-later-real-transport-and-failed-recovery-next-wave");
 for(unsigned edge=0;edge<3;++edge) {
  const auto w=prepare();returned();tick=2000;
  if(edge==0)require(g_virtualStartProtection.cancel(tick),"cancel original pending period");
  if(edge==1)require(g_virtualStartProtection.beginVerification(w.request,"success",tick),"unfinished verification stays owned by execution");
  if(edge==0)tick=33000;
  g_inputHostEpoch=7;g_inputHostRunId="working-host";g_inputHostHostInstanceId="host-instance-7";
  g_inputHostBoundPersona="steamdeck";g_inputHostProcess=reinterpret_cast<HANDLE>(1);
  g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=true;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryOfferLocked("SUBMIT_FRAME",42,ERROR_BROKEN_PIPE);}
  require(!g_inputHostRuntimeRecoveryPending&&queue.empty()&&stopCalls==0&&charges==0,
   "cancelled and unfinished periods cannot become operational runtime incidents");
 }
 done("cancelled-pending-verifying-stay-with-original-owner");
 for(unsigned edge=0;edge<4;++edge) {
  const auto w=prepare();returned();tick=33000;
  require(g_virtualStartProtection.view(tick).phase=="expired","cancellation test starts from an expired old period");
  g_inputHostEpoch=7;g_inputHostRunId="working-host";g_inputHostHostInstanceId="host-instance-7";
  g_inputHostBoundPersona="steamdeck";g_inputHostProcess=reinterpret_cast<HANDLE>(1);
  g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=true;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryOfferLocked("SUBMIT_FRAME",42,ERROR_BROKEN_PIPE);}
  require(g_inputHostRuntimeRecoveryPending,"real fault was observed before cancellation");
  if(edge==0){configured="disabled";g_realStickTest.virtualTargetEnabled=false;}
  if(edge==1){++g_inputHostVirtualIntentRevision;g_inputHostRecoveryIntent.invalidate();}
  if(edge==2)++power;
  if(edge==3)require(g_virtualStartProtection.admit("elite",tick).accepted,"new player ticket supersedes the old terminal");
  maintain();drain();
  require(!g_inputHostRuntimeRecoveryPending&&stopCalls==0&&startCalls==0&&charges==0,
   "OFF revision power and new player ticket cancel the late transport incident before effects");
  (void)w;
 }
 done("late-transport-cancellation-before-effects");
}


static void publishDemand(){g_inputHostRecoveryIntent.configure(demandInput,demandSession,g_realStickTest.enabled,g_realStickTest.virtualTargetEnabled,configured);}
static void snapshotRefresh(){g_inputHostVirtualIntentWriters=true;++g_inputHostVirtualIntentRevision;publishDemand();++g_inputHostVirtualIntentRevision;g_inputHostVirtualIntentWriters=false;}
static void setDedicated(const std::string& owner){demandInput["gameOverride"]={{"sessionId",demandSession},{"identity",owner},{"outputTarget",demandInput["outputTarget"]},{"gyroMotion",demandInput["gyroMotion"]}};publishDemand();}

static void demandBadFieldCases(){
 const std::vector<json> bad={nullptr,json::array(),json::array({1,"tail"}),"legacy-string",true,17};
 for(const auto& output:bad)for(const auto& motion:bad){
  prepare();demandInput["outputTarget"]=output;demandInput["gyroMotion"]=motion;const auto raw=demandInput.dump();
  publishDemand();const auto generation=g_inputHostRecoveryIntent.revision();snapshotRefresh();
  require(generation!=0&&generation==g_inputHostRecoveryIntent.revision(),"malformed legacy output and motion preserve same demand without throwing");
  require(demandInput.dump()==raw,"bad field publisher cannot rewrite or repair persisted raw settings");
  demandInput["outputTarget"]=json::object();publishDemand();require(g_inputHostRecoveryIntent.revision()!=generation,"bad output shape becoming object is a true demand change");
 }
 prepare();demandInput.erase("outputTarget");demandInput.erase("gyroMotion");publishDemand();const auto missing=g_inputHostRecoveryIntent.revision();publishDemand();
 require(missing&&missing==g_inputHostRecoveryIntent.revision()&&!demandInput.contains("outputTarget")&&!demandInput.contains("gyroMotion"),"missing legacy fields remain unmodified and stable");
 done("demand-malformed-legacy-fields-preserve-raw-and-do-not-throw");
}

static void demandStabilityCases(){
 demandBadFieldCases();
 namespace runtime=ymcc::inputHost::runtimeRecovery;
 for(const auto* persona:{"steamdeck","dualsense-edge","elite"}){
  const auto w=prepare(persona);setDedicated("game-A");offered(w);const auto original=runtime::state;
  for(unsigned n=0;n<30;++n){tick+=16;demandInput["revision"]=n+100;demandInput["hostAcknowledgedRevision"]=n+99;demandInput["pendingConfigHash"]="sha256:bookkeeping";
   demandInput["outputTarget"]["revision"]=n+1;demandInput["gyroMotion"]["revision"]=n+1;demandInput["unrelated"]={{"diagnostics",n}};snapshotRefresh();maintain();
   require(g_inputHostRuntimeRecoveryPending&&runtime::state.serial==original.serial&&runtime::state.observationUntil==original.observationUntil&&runtime::state.demandRevision==original.demandRevision,
    "same semantic settings refresh must retain original pre-host observation serial demand and deadline");
   require(startCalls==0&&stopCalls==0&&charges==0,"same settings refresh cannot create effects or consume attempts");
  }
  require(g_inputHostVirtualIntentRevision==67,"original snapshot revision continues advancing independently");
  returned();tick=32000;maintain();drain();const auto recovery=g_virtualStartProtection.view(tick);
  require(startCalls==1&&stopCalls==0&&charges==1&&recovery.requestId!=w.request&&runtime::state.serial==original.serial,
   "same-demand writer return admits one bounded recovery through original runtime");
  require(recovery.remainingMs==30000&&runtime::state.observationUntil==original.observationUntil,"new recovery does not extend original background observation");
 }
 done("demand-three-personas-same-refresh-ack-and-unrelated-save");
 for(unsigned mode=0;mode<2;++mode){
  const auto w=prepare();offered(w);returned();tick=32000;const auto original=runtime::state;
  if(mode==1){maintain();require(queue.size()==1,"original recovery token queues before writer starts");}
  g_inputHostVirtualIntentWriters=true;++g_inputHostVirtualIntentRevision;
  if(mode==0)maintain();else drain();
  require(g_inputHostRuntimeRecoveryPending&&runtime::state.serial==original.serial&&runtime::state.observationUntil==original.observationUntil&&startCalls==0&&stopCalls==0&&charges==0,
   "active snapshot writer must pause maintenance and queued execution without retiring current demand");
  publishDemand();++g_inputHostVirtualIntentRevision;g_inputHostVirtualIntentWriters=false;tick+=1001;maintain();drain();
  require(startCalls==1&&charges==1&&stopCalls==0,"quiescent same-demand writer resumes original one permitted recovery");
 }
 done("demand-active-writer-pauses-maintenance-and-executor");
 for(unsigned edge=0;edge<10;++edge){
  auto w=prepare();setDedicated("game-A");offered(w);returned();tick=32000;const auto old=runtime::state.demandRevision;
  if(edge==0){g_realStickTest.virtualTargetEnabled=false;publishDemand();g_realStickTest.virtualTargetEnabled=true;publishDemand();}
  if(edge==1){configured="elite";publishDemand();}
  if(edge==2)setDedicated("game-B");
  if(edge==3){demandInput.erase("gameOverride");publishDemand();}
  if(edge==4){g_realStickTest.enabled=false;publishDemand();g_realStickTest.enabled=true;publishDemand();}
  if(edge==5){demandInput["gyroMotion"]["enabled"]=true;publishDemand();}
  if(edge==6){demandInput["outputTarget"]["backMapping"]=true;publishDemand();}
  if(edge==7)g_inputHostRecoveryIntent.invalidate();
  if(edge==8){demandInput["gameOverride"]["sessionId"]="different-session";publishDemand();}
  if(edge==9){demandInput["gyroMotion"]["motionTrigger"]="rb";publishDemand();}
  require(g_inputHostRecoveryIntent.revision()!=old,"true config owner lane OFF ABA persona or invalidation changes demand generation");
  maintain();drain();require(!g_inputHostRuntimeRecoveryPending&&startCalls==0&&stopCalls==0&&charges==0,
   "true semantic change cancels stale pre-host demand before every effect");
 }
 done("demand-ten-real-change-and-aba-cancellations");
 {
  const auto w=prepare();offered(w);const auto original=runtime::state;
  for(unsigned n=0;n<20;++n){tick+=100;demandInput["revision"]=n;snapshotRefresh();inputHostRuntimeRecoveryObserveNativeWait(w);maintain();
   require(runtime::state.serial==original.serial&&runtime::state.observationUntil==original.observationUntil,"duplicate same-demand observations cannot renew serial or deadline");}
  returned();tick=original.observationUntil;g_inputHostVirtualIntentWriters=true;maintain();
  require(!g_inputHostRuntimeRecoveryPending&&startCalls==0&&charges==0,"original 180-second observation expiry still cancels during active writer");
  g_inputHostVirtualIntentWriters=false;snapshotRefresh();maintain();drain();require(startCalls==0&&charges==0,"same refresh cannot revive an expired observation");
 }
 done("demand-duplicate-refresh-does-not-renew-observation-or-expiry");
 for(unsigned edge=0;edge<2;++edge){
  const auto w=prepare();returned();g_virtualStartProtection.beginVerification(w.request,"success",tick);tick+=10000;g_virtualStartProtection.finishVerification(w.request,"success",tick);tick=33000;
  g_inputHostEpoch=7;g_inputHostRunId="current-operational-host";g_inputHostHostInstanceId="host-instance-7";g_inputHostBoundPersona="steamdeck";
  g_inputHostProcess=reinterpret_cast<HANDLE>(1);g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;g_inputHostTransportFault=true;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryOfferLocked("SUBMIT_FRAME",42,ERROR_BROKEN_PIPE);}
  const auto original=runtime::state;require(original.pending,"settled operational transport fault is offered");
  if(edge==0){snapshotRefresh();maintain();require(stopCalls==0,"maintenance only queues original Stop");g_inputHostVirtualIntentWriters=true;drain();
   require(stopCalls==0&&charges==0&&g_inputHostRuntimeRecoveryPending,"queued operational Stop also pauses during an active snapshot writer");
   publishDemand();g_inputHostVirtualIntentWriters=false;tick+=1001;maintain();drain();require(stopCalls==1&&charges==0&&runtime::state.serial==original.serial,"same-demand quiescence permits exactly one original safety Stop");}
  else {demandInput["outputTarget"]["gyroEnabled"]=true;publishDemand();maintain();drain();require(stopCalls==0&&charges==0&&!g_inputHostRuntimeRecoveryPending,"changed operational demand cancels old safety owner");}
 }
 done("demand-post-final-same-refresh-and-queued-writer-fences");
 {
  const auto w=prepare();offered(w);returned();tick=32000;maintain();const auto token=runtime::state.serial;
  g_realStickTest.virtualTargetEnabled=false;publishDemand();g_realStickTest.virtualTargetEnabled=true;publishDemand();drain();inputHostPerformRuntimeRecovery(token);
  require(startCalls==0&&charges==0&&!g_inputHostRuntimeRecoveryPending,"queued OFF-ON ABA cannot reuse prior equal persona token");
 }
 done("demand-queued-aba-token-remains-cancelled");
}

// Field225328: production runtime/admission with the existing cached effect ports.
// The observed CM call duration advances only this fixture clock; no CM/Host IO.
static void field225328LongWriterCases(){
 namespace runtime=ymcc::inputHost::runtimeRecovery;
 for(const std::uint64_t duration:{65370ULL,65378ULL}){
  const auto work=prepare();offered(work);const auto observed=runtime::state;
  const auto owner=g_msiNativeForegroundLease.cycleProcess;
  const auto* writer=g_msiNativeForegroundLease.nativeRefreshWriter.get();
  for(const auto elapsed:{29999ULL,30000ULL,duration-1}){
   tick=1000+elapsed;maintain();drain();
   require(startCalls==0&&charges==0&&queue.empty(),"live native writer cannot authorize recovery");
   require(g_msiNativeForegroundLease.cycleProcess==owner&&g_msiNativeForegroundLease.nativeRefreshWriter.get()==writer,
    "long current writer identity and ownership must remain retained");
   require(runtime::state.serial==observed.serial&&runtime::state.observationUntil==observed.observationUntil,
    "long writer observations cannot refresh the original180second window");
   if(elapsed>=30000)require(g_virtualStartProtection.view(tick).requestId==work.request&&g_virtualStartProtection.view(tick).phase=="expired",
    "field long writer cannot widen the old30second player deadline");
  }
  g_msiNativeForegroundCyclePending=false;tick=1000+duration;maintain();drain();
  require(startCalls==0&&charges==0&&g_msiNativeForegroundLease.active,
   "pending flag alone cannot prove long writer return");
  g_msiNativeForegroundLease.nativeRefreshWriter.reset();tick+=1001;maintain();drain();
  require(startCalls==0&&charges==0&&g_msiNativeForegroundLease.cycleProcess==owner,
   "missing writer object with live cached process remains ownership debt");
  const auto oldFinal=g_virtualStartProtection.view(tick);returned();tick+=1001;maintain();drain();
  const auto next=g_virtualStartProtection.view(tick);
  require(oldFinal.requestId==work.request&&oldFinal.phase=="expired"&&next.requestId!=work.request&&next.remainingMs==30000,
   "returned long writer gets one distinct original30second recovery period");
  require(startCalls==1&&charges==1&&stopCalls==0,"long writer return starts and charges exactly once");
  const auto recoverySerial=runtime::state.serial;
  for(unsigned repeat=0;repeat<3;++repeat){inputHostPerformRuntimeRecovery(recoverySerial);maintain();drain();}
  require(startCalls==1&&charges==1&&runtime::state.observationUntil==observed.observationUntil,
   "duplicate long writer terminal does not refresh time or refund attempts");
  require(g_virtualStartProtection.beginVerification(next.requestId,"success",tick),"field recovery uses original normal verification");
  tick+=ymcc::VirtualStartProtection::executionInspectionMs;
  require(g_virtualStartProtection.finishVerification(next.requestId,"success",tick),"field recovery completes original10second inspection");
  maintain();drain();require(!g_inputHostRuntimeRecoveryPending&&startCalls==1&&charges==1,
   "verified long writer recovery stays final without another wave");
 }
 for(unsigned edge=0;edge<3;++edge){
  const auto work=prepare();offered(work);const auto serial=runtime::state.serial;
  const auto owner=g_msiNativeForegroundLease.cycleProcess;
  const auto* writer=g_msiNativeForegroundLease.nativeRefreshWriter.get();
  tick=edge==2?32000:15000;
  if(edge==0||edge==2){g_realStickTest.virtualTargetEnabled=false;configured="disabled";g_inputHostRecoveryIntent.invalidate();}
  else require(g_virtualStartProtection.cancel(tick),"explicit cancel retires the original pending player ticket");
  maintain();drain();require(!g_inputHostRuntimeRecoveryPending&&startCalls==0&&charges==0,
   "OFF or cancel while writer live immediately retires only old recovery observation");
  require(g_msiNativeForegroundLease.active&&g_msiNativeForegroundLease.cycleProcess==owner&&g_msiNativeForegroundLease.nativeRefreshWriter.get()==writer,
   "OFF does not erase unresolved writer ownership");
  tick=66378;returned();maintain();drain();inputHostPerformRuntimeRecovery(serial);maintain();drain();
  require(startCalls==0&&charges==0&&!g_inputHostRuntimeRecoveryPending,
   "late65second terminal and duplicate old token cannot reopen OFF or cancelled demand");
  (void)work;
 }
 {
  const auto oldWork=prepare();offered(oldWork);const auto oldSerial=runtime::state.serial;
  tick=15000;g_realStickTest.virtualTargetEnabled=false;configured="disabled";g_inputHostRecoveryIntent.invalidate();
  require(g_virtualStartProtection.cancel(tick),"OFF cancels old ticket before a new player ON");maintain();drain();
  require(!g_inputHostRuntimeRecoveryPending&&startCalls==0&&charges==0&&g_msiNativeForegroundLease.active,
   "OFF retires observation while preserving the same unresolved native writer");
  tick=25001;g_realStickTest.virtualTargetEnabled=true;configured="steamdeck";publishDemand();
  const auto player=g_virtualStartProtection.admit(configured,tick);require(player.accepted&&player.requestId!=oldWork.request,
   "new ON must acquire its own player intent");
  const ymcc::inputHost::queuedPlayerStart::Work newWork{player.requestId,power,tick+30000,configured,"initial-target-admission",true};
  offered(newWork);const auto current=runtime::state;require(current.serial!=oldSerial&&current.requestId==player.requestId,
   "new ON binds its own observation serial instead of reviving old OFF");
  tick=newWork.deadline;maintain();drain();require(startCalls==0&&charges==0&&g_virtualStartProtection.view(tick).phase=="expired",
   "new ON also keeps its own original deadline while old exact writer is live");
  tick=66378;returned();tick+=1001;maintain();drain();const auto next=g_virtualStartProtection.view(tick);
  require(next.requestId!=oldWork.request&&next.requestId!=player.requestId&&startCalls==1&&charges==1&&next.remainingMs==30000,
   "exact old writer return permits only one independent recovery for the current ON");
  inputHostPerformRuntimeRecovery(oldSerial);inputHostRuntimeRecoveryObserveNativeWait(oldWork);maintain();drain();
  require(startCalls==1&&charges==1&&runtime::state.serial==current.serial&&runtime::state.observationUntil==current.observationUntil,
   "old OFF serial and old deferred request cannot affect successor recovery or180second window");
 }
 std::cout<<"FIELD_225328 original-group=live durations=65370,65378 off-or-cancel=3 off-on=1 hardwareIo=false wholeNativeConsumer=false\n";
}


static void successorContinuationCases() {
 namespace runtime=ymcc::inputHost::runtimeRecovery;
 for(const auto* persona:{"steamdeck","dualsense-edge","elite"}) {
  for(const auto* edge:{"continue","foreign","off","power","new-ticket","deadline","return-debt","wrong-scope","hello-foreign"}) {
   const auto w=prepare(persona);returned();tick=33000;
   require(g_virtualStartProtection.view(tick).phase=="expired","original final is settled before actual transport fault");
   g_inputHostEpoch=7;g_inputHostRunId="original-host";g_inputHostHostInstanceId="original-instance";
   g_inputHostBoundPersona=persona;g_inputHostProcess=reinterpret_cast<HANDLE>(1);
   g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;g_inputHostTransportFault=true;
   {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryOfferLocked("SUBMIT_FRAME",42,ERROR_BROKEN_PIPE);}
   maintain();drain();require(stopCalls==1&&charges==0,"actual transport incident invokes one owned safety Stop");
   g_inputHostEpoch=0;g_inputHostRunId.clear();g_inputHostHostInstanceId.clear();g_inputHostProcess=nullptr;
   g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=false;
   startResult=false;tick+=1001;maintain();drain();
   auto& s=runtime::state;const auto request=s.recoveryRequestId,serial=s.serial;
   require(request&&request!=w.request&&s.monitoring&&charges==1&&startCalls==1,"first rejected Start retains exact recovery period for normal inner fallback");
   const auto deadline=tick+g_virtualStartProtection.view(tick).remainingMs;
   tick+=1001;
   g_inputHostEpoch=9;g_inputHostRunId="same-request-later-host";g_inputHostHostInstanceId.clear();
   g_inputHostSwitchRecoveryStartRequestId=request;g_inputHostSwitchRecoveryStartPower=power;g_inputHostSwitchRecoveryStartDeadline=deadline;
   if(std::strcmp(edge,"off")==0){configured="disabled";g_realStickTest.virtualTargetEnabled=false;}
   if(std::strcmp(edge,"power")==0)++power;
   if(std::strcmp(edge,"new-ticket")==0){tick+=10000;require(g_virtualStartProtection.finishVerification(request,"failure",tick),"old recovery completes its original inspection before new explicit request");require(g_virtualStartProtection.admit("elite",tick).accepted,"new explicit period supersedes old recovery");}
   if(std::strcmp(edge,"deadline")==0)g_inputHostSwitchRecoveryStartDeadline=tick;
   if(std::strcmp(edge,"return-debt")==0)ymcc::inputHost::targetReturnRuntime::state.session->value.obligations.push_back(1);
   if(std::strcmp(edge,"wrong-scope")==0)++g_inputHostSwitchRecoveryStartRequestId;
   if(std::strcmp(edge,"foreign")!=0){std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryBindStartTupleLocked(request,s.power,persona,true);}
   const bool positive=std::strcmp(edge,"continue")==0||std::strcmp(edge,"hello-foreign")==0;
   require((s.hostEpoch==9)==positive,"only actual authorized same-period creation binds successor");
   if(!positive){maintain();drain();require(!s.pending&&!s.monitoring&&s.spent&&charges==1&&startCalls==1&&stopCalls==1,"foreign cancellation deadline and debt cannot be adopted or create effects");continue;}
   g_inputHostProcess=reinterpret_cast<HANDLE>(2);g_inputHostHostInstanceId="validated-instance-9";
   if(std::strcmp(edge,"hello-foreign")==0){g_inputHostEpoch=10;g_inputHostRunId="foreign-hello";}
   {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryBindStartTupleLocked(request,s.power,persona,false);}
   if(std::strcmp(edge,"hello-foreign")==0){require(s.hostEpoch==9&&s.hostInstanceId.empty(),"HELLO observation cannot adopt a foreign epoch");maintain();require(!s.monitoring,"foreign HELLO owner still cancels recovery");continue;}
   g_inputHostSwitchRecoveryStartRequestId=g_inputHostSwitchRecoveryStartPower=g_inputHostSwitchRecoveryStartDeadline=0;
   maintain();require(s.monitoring&&s.hostEpoch==9&&s.hostInstanceId=="validated-instance-9","later same-request Host survives observer after Start scope returned");
   // Model normal fallback's actual failure final. The admission class and
   // runtime header are real; process/pipe/target returns remain cached ports.
   tick+=10000;require(g_virtualStartProtection.finishVerification(request,"failure",tick),"normal failed recovery keeps a single failed final");
   maintain();drain();require(s.pending&&!s.monitoring&&s.serial!=serial&&s.retryAt>tick,"same-period failed successor retains ON for original bounded next wave");
   const auto retry=s.retryAt;inputHostPerformRuntimeRecovery(serial);
   require(startCalls==1&&charges==1&&s.retryAt==retry,"old recovery serial cannot act on successor");
   g_inputHostEpoch=0;g_inputHostRunId.clear();g_inputHostHostInstanceId.clear();g_inputHostProcess=nullptr;
   for(unsigned debt=0;debt<3;++debt){cleanupProven=debt!=0;pipeReturned=debt!=1;exactTargetReturned=debt!=2;tick=retry+1001;maintain();drain();require(startCalls==1&&charges==1,"next wave waits exact cleanup pipe and target return");}
   cleanupProven=pipeReturned=exactTargetReturned=true;rolling=0;tick+=1001;startResult=true;maintain();drain();
   require(startCalls==2&&charges==2&&g_inputHostRecoveryEpisodeAttempts==2&&g_virtualStartProtection.view(tick).remainingMs==30000,"returned successor continues one distinct bounded period without refund");
   require(g_virtualStartProtection.view(tick).requestId!=request,"later ON recovery never rewrites failed period");
  }
 }
 done("same-period-delayed-start-successor-three-personas-and-cancellation-return-debt-controls");
}


static void healthyIdleMinuteCases() {
 namespace runtime=ymcc::inputHost::runtimeRecovery;
 for(const auto* persona:{"steamdeck","dualsense-edge","elite"}) {
  const auto w=prepare(persona);returned();tick=2000;
  require(g_virtualStartProtection.beginVerification(w.request,"success",tick),"healthy original candidate");
  tick+=10000;require(g_virtualStartProtection.finishVerification(w.request,"success",tick),"healthy original10s inspection");
  g_inputHostEpoch=7;g_inputHostRunId="healthy-steady-host";g_inputHostHostInstanceId="healthy-instance";
  g_inputHostBoundPersona=persona;g_inputHostProcess=reinterpret_cast<HANDLE>(1);
  g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;
  g_inputHostTransportFault=false;sourceHeld=true;
  const auto started=tick;const auto before=runtime::state.serial;
  for(unsigned pass=0;pass<60;++pass){
   tick+=1000;maintain();drain();
   require(g_inputHostEpoch==7&&g_inputHostRunId=="healthy-steady-host"&&g_inputHostHostInstanceId=="healthy-instance"&&g_inputHostProcess==reinterpret_cast<HANDLE>(1),"one minute without input/fault cannot detach the same healthy Host");
   require(stopCalls==0&&startCalls==0&&charges==0&&rolling==0&&g_inputHostRecoveryEpisodeAttempts==0&&queue.empty()&&!g_inputHostRuntimeRecoveryPending&&runtime::state.serial==before,"idle ticks alone cannot offer recovery Stop Start quota or old-key replay");
   const auto view=g_virtualStartProtection.view(tick);require(view.requestId==w.request&&view.phase=="success"&&!view.locked,"idle cannot reopen original terminal or lock UI");
  }
  require(tick-started==60000,"exact60s virtual observation");
 }
 done("healthy-same-on-power-host-sixty-seconds-no-input-no-fault-no-recovery-effects");
}


static void terminalBudgetCases(bool idleOnly=false,bool finalOnly=false,bool admissionOnly=false,bool coreOnly=false) {
 namespace runtime=ymcc::inputHost::runtimeRecovery;
 const auto beginFault=[](const char* persona){
  const auto work=prepare(persona);returned();tick=2000;
  require(g_virtualStartProtection.beginVerification(work.request,"success",tick),"original candidate");
  tick+=10000;require(g_virtualStartProtection.finishVerification(work.request,"success",tick),"original settled10s success");
  g_inputHostEpoch=7;g_inputHostRunId="faulted-original";g_inputHostHostInstanceId="original-instance";
  g_inputHostBoundPersona=persona;g_inputHostProcess=reinterpret_cast<HANDLE>(1);
  g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;g_inputHostTransportFault=true;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryOfferLocked("SUBMIT_FRAME",42,ERROR_BROKEN_PIPE);}
  maintain();drain();require(stopCalls==1&&charges==0,"one real incident safety Stop precedes return");
  g_inputHostEpoch=0;g_inputHostRunId.clear();g_inputHostHostInstanceId.clear();g_inputHostProcess=nullptr;
  g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=false;
  startResult=false;tick+=1001;maintain();drain();
  require(startCalls==1&&charges==1&&runtime::state.monitoring&&runtime::state.recoveryRequestId,"real failed Start owns one distinct bounded recovery");
  return work;
 };
 if(!idleOnly&&!finalOnly&&!admissionOnly){
 for(const auto* persona:{"steamdeck","dualsense-edge","elite"}) {
  for(const auto* edge:{"episode","rolling","already-terminal","not-exhausted","off","power","persona","new-ticket","foreign-host","prehost","unknown-command","nonretryable","unfinished"}) {
   const auto old=beginFault(persona);auto& s=runtime::state;
   const auto request=s.recoveryRequestId,serial=s.serial;
   const auto afterFirst=tick;
   if(std::strcmp(edge,"unfinished")!=0){tick+=10000;require(g_virtualStartProtection.finishVerification(request,"failure",tick),"failed recovery final remains real failure");}
   if(std::strcmp(edge,"rolling")==0)rolling=2;
   else if(std::strcmp(edge,"not-exhausted")!=0)g_inputHostRecoveryEpisodeAttempts=2;
   if(std::strcmp(edge,"already-terminal")==0){g_inputHostRecoveryTerminalLogged=true;g_inputHostFaultHoldsStart=true;}
   if(std::strcmp(edge,"off")==0){g_realStickTest.virtualTargetEnabled=false;configured="disabled";}
   if(std::strcmp(edge,"power")==0)++power;
   if(std::strcmp(edge,"persona")==0)configured=persona==std::string("elite")?"steamdeck":"elite";
   if(std::strcmp(edge,"new-ticket")==0)require(g_virtualStartProtection.admit("elite",tick).accepted,"new player ticket is distinct");
   if(std::strcmp(edge,"foreign-host")==0){g_inputHostEpoch=9;g_inputHostRunId="foreign";g_inputHostHostInstanceId="foreign";g_inputHostProcess=reinterpret_cast<HANDLE>(2);}
   if(std::strcmp(edge,"prehost")==0)s.preHostWriterWait=true;
   if(std::strcmp(edge,"unknown-command")==0)s.command="NO_SOURCE";
   if(std::strcmp(edge,"nonretryable")==0)s.error=ERROR_ACCESS_DENIED;
   const auto charged=g_inputHostRecoveryEpisodeAttempts.load();maintain();
   const bool shouldTerminal=std::strcmp(edge,"episode")==0||std::strcmp(edge,"rolling")==0||std::strcmp(edge,"already-terminal")==0;
   require(terminalCalls==(shouldTerminal?1u:0u),"failed classified recovery with exhausted shared budget hands off exactly once");
   if(shouldTerminal){
    require(!s.pending&&!s.monitoring&&s.spent&&!g_inputHostRuntimeRecoveryPending&&g_inputHostFaultHoldsStart,"shared terminal takes ownership before any new runtime wave");
    require(terminalActions==(std::strcmp(edge,"already-terminal")==0?0u:1u),"existing shared terminal action is idempotent");
    const auto failed=g_virtualStartProtection.view(tick);require(failed.requestId==request&&failed.phase=="failure"&&!failed.locked,"terminal handoff never rewrites prior failure");
    for(unsigned n=0;n<8;++n){tick+=120001;maintain();inputHostPerformRuntimeRecovery(serial);}
    require(terminalCalls==1&&startCalls==1&&stopCalls==1&&charges==1&&g_inputHostRecoveryEpisodeAttempts.load()==charged,"late waves and tokens cannot restart spend refund or repeat terminal");
    require(g_realStickTest.virtualTargetEnabled&&configured==persona,"shared final fallback preserves selected persona and ON intent");
   }else if(std::strcmp(edge,"not-exhausted")==0){
    require(s.pending&&!s.monitoring&&s.retryAt>tick&&!g_inputHostRecoveryTerminalLogged,"unspent shared budget keeps legal retry");
    tick=s.retryAt+1;rolling=0;startResult=true;maintain();drain();
    require(startCalls==2&&charges==2&&terminalCalls==0&&g_virtualStartProtection.view(tick).remainingMs==30000,"legal second attempt remains bounded with no terminal or refund");
   }else{
    inputHostPerformRuntimeRecovery(serial);require(terminalCalls==0&&startCalls==1&&charges==1,"cancelled unknown preHost or unfinished work cannot dispatch terminal effects");
   }
   (void)old;(void)afterFirst;
  }
 }
 done("terminal-failed-recovery-three-personas-exhaustion-and-revocation-guards");
 for(unsigned edge=0;edge<3;++edge){
  prepare();returned();g_inputHostRecoveryEpisodeAttempts=2;rolling=2;
  auto& s=runtime::state;s.wavePermit=true;s.retryAt=1;s.wave=4;
  if(edge==1){g_inputHostRecoveryEpisodeAttempts=0;g_inputHostRecoveryTerminalLogged=true;}
  if(edge==2){g_inputHostRecoveryEpisodeAttempts=0;g_inputHostRecoveryTerminalLogged=false;}
  require(!runtime::waveReady(tick),"runtime wave permit cannot bypass shared episode rolling or terminal");
 }
 done("wave-permit-never-overrides-shared-terminal-or-budget");
 }
 if(!finalOnly&&!admissionOnly&&!coreOnly){
 for(unsigned edge=0;edge<3;++edge){
  const auto w=prepare();if(edge!=2)returned();g_inputHostRecoveryEpisodeAttempts=2;rolling=2;sourceHeld=true;
  if(edge==1){g_inputHostEpoch=7;g_inputHostRunId="healthy-idle";g_inputHostHostInstanceId="healthy";g_inputHostProcess=reinterpret_cast<HANDLE>(1);}
  if(edge==2){const auto work=w;offered(work);}
  for(unsigned n=0;n<60;++n){tick+=1000;maintain();drain();}
  require(terminalCalls==0&&terminalActions==0&&stopCalls==0&&startCalls==0&&charges==0,"idle missing source or preHost uncertainty cannot manufacture terminal fallback");
 }
 done("sixty-second-idle-source-unknown-prehost-never-terminal");
 }
 if(finalOnly){
  for(const auto* persona:{"steamdeck","dualsense-edge","elite"})for(const bool success:{false,true}){
   beginFault(persona);auto& s=runtime::state;const auto request=s.recoveryRequestId,serial=s.serial;
   if(success){tick+=10000;require(g_virtualStartProtection.finishVerification(request,"success",tick),"current accepted-success fixture terminal is finalized by actual admission class");}
   else tick+=30001;
   g_inputHostRecoveryEpisodeAttempts=2;maintain();
   const auto view=g_virtualStartProtection.view(tick);
   require(view.requestId==request&&view.phase==(success?"success":"expired")&&!view.locked,"original success/expiry remains unchanged at exhausted budget");
   require(terminalCalls==(success?0u:1u)&&!s.pending&&!s.monitoring&&s.spent,"success never falls back solely for exhausted counters; expired proven fault does");
   inputHostPerformRuntimeRecovery(serial);tick+=120001;maintain();
   require(terminalCalls==(success?0u:1u)&&startCalls==1&&charges==1,"late token cannot change success/expired terminal result");
  }
  done("three-personas-success-with-exhausted-counters-stays-and-expired-fault-hands-off");
 }
 if(admissionOnly){
  for(unsigned edge=0;edge<5;++edge){
   beginFault("steamdeck");auto& s=runtime::state;const auto serial=s.serial,request=s.recoveryRequestId;
   tick+=10000;require(g_virtualStartProtection.finishVerification(request,"failure",tick),"admission test has real failed period");
   g_inputHostRecoveryEpisodeAttempts=2;terminalCaptureRefused=true;
   if(edge==4)g_externalPriorityVirtualSuspended=true;
   maintain();require(s.monitoring&&!s.pending&&s.serial==serial&&s.recoveryRequestId==request&&!g_inputHostRecoveryTerminalLogged,
     "refused shared terminal capture cannot retire exact failed runtime incident");
   require(terminalCalls==(edge==4?0u:1u)&&terminalActions==0,"external priority pauses capture and failed Scope grants no action");
   const auto originalProbe=s.nextProbe;
   for(unsigned n=0;n<8;++n){maintain();inputHostPerformRuntimeRecovery(serial);}
   require(terminalCalls==(edge==4?0u:1u)&&s.nextProbe==originalProbe&&startCalls==1&&charges==1,"same failed observation keeps1s cadence and no wave or quota refund");
   terminalCaptureRefused=false;g_externalPriorityVirtualSuspended=false;
   if(edge==1){g_realStickTest.virtualTargetEnabled=false;configured="disabled";}
   if(edge==2)++power;
   if(edge==3){require(g_virtualStartProtection.admit("elite",tick).accepted,"new request revokes refused capture");}
   tick+=1001;maintain();
   const bool action=edge==0||edge==4;
   require(terminalActions==(action?1u:0u)&&!s.monitoring&&!s.pending&&s.spent,
     "retry captures terminal once or cancels immediately with OFF power or new request");
   inputHostPerformRuntimeRecovery(serial);maintain();require(startCalls==1&&charges==1,"late serial after terminal admission cannot rebuild");
  }
  done("shared-terminal-refusal-keeps-exact-observation-and-revocation");
 }


}


static void freshFaultExhaustionCases() {
 namespace runtime=ymcc::inputHost::runtimeRecovery;
 for(const auto* persona:{"steamdeck","dualsense-edge","elite"})for(const auto* edge:{"success-fault","failure-fault","expired-fault","healthy-no-fault","off-after-offer","power-after-offer","foreign-host","shared-capture-refused"}){
  const auto work=prepare(persona);returned();tick=2000;
  const bool expired=std::strcmp(edge,"expired-fault")==0,failed=std::strcmp(edge,"failure-fault")==0;
  require(g_virtualStartProtection.beginVerification(work.request,failed?"failure":"success",tick),"original operational candidate");
  tick+=expired?31000:10000;
  if(!expired)require(g_virtualStartProtection.finishVerification(work.request,failed?"failure":"success",tick),"actual original operational final");
  const auto original=g_virtualStartProtection.view(tick);
  require(runtime::operationalTerminal(original),"new fault starts after actual operational final");
  g_inputHostEpoch=7;g_inputHostRunId="newly-faulted-live-host";g_inputHostHostInstanceId="instance7";
  g_inputHostBoundPersona=persona;g_inputHostProcess=reinterpret_cast<HANDLE>(1);
  g_inputHostHello=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;
  g_inputHostRecoveryEpisodeAttempts=2;
  const bool healthy=std::strcmp(edge,"healthy-no-fault")==0;
  if(!healthy){
   g_inputHostTransportFault=true;
   {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryOfferLocked("SUBMIT_FRAME",57,ERROR_BROKEN_PIPE);}
   require(runtime::state.pending&&!runtime::state.recoveryRequestId&&!runtime::state.spent,"new real post-final fault has no recovery ticket yet");
  }
  const auto serial=runtime::state.serial;
  if(std::strcmp(edge,"off-after-offer")==0){configured="disabled";g_realStickTest.virtualTargetEnabled=false;}
  if(std::strcmp(edge,"power-after-offer")==0)++power;
  if(std::strcmp(edge,"foreign-host")==0){g_inputHostEpoch=9;g_inputHostRunId="foreign-new";g_inputHostHostInstanceId="foreign";}
  if(std::strcmp(edge,"shared-capture-refused")==0)terminalCaptureRefused=true;
  maintain();drain();
  const bool cancel=healthy||std::strcmp(edge,"off-after-offer")==0||std::strcmp(edge,"power-after-offer")==0||std::strcmp(edge,"foreign-host")==0;
  const bool refused=std::strcmp(edge,"shared-capture-refused")==0;
  require(terminalCalls==(cancel?0u:1u),"new classified fault with exhausted prior budget must reach shared terminal without a recovery ticket");
  if(refused){
   require(runtime::state.pending&&!runtime::state.spent&&!g_inputHostRecoveryTerminalLogged,"new fault preserves observation when shared capture is refused");
   terminalCaptureRefused=false;tick+=1001;maintain();drain();
   require(terminalCalls==2&&terminalActions==1&&!runtime::state.pending,"same new fault later captures terminal exactly once");
  }
  require(startCalls==0&&charges==0&&g_inputHostRecoveryEpisodeAttempts==2,"exhausted fresh incident never charges or creates a recovery target");
  const auto final=g_virtualStartProtection.view(tick);
  require(final.requestId==original.requestId&&final.phase==original.phase&&!final.locked,"new fault fallback leaves original successful or failed final immutable");
  for(unsigned late=0;late<8;++late){inputHostPerformRuntimeRecovery(serial);maintain();drain();}
  require(startCalls==0&&charges==0&&terminalActions==(cancel?0u:1u),"late fresh-fault tokens cannot duplicate terminal action or retry");
 }
 done("fresh-post-final-fault-with-exhausted-budget-three-personas-eight-boundaries");
}

int main(int argc,char** argv){
 try{
  require(argc==1||argc==2,"expected zero or one selector argument");
  const std::string selection=argc>1?argv[1]:"all";
  if(selection=="demand-stability"){demandStabilityCases();std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  if(selection=="terminal-budget-idle"){terminalBudgetCases(true);std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  if(selection=="terminal-budget-final"){terminalBudgetCases(false,true);std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  if(selection=="terminal-budget-admission"){terminalBudgetCases(false,false,true);std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  if(selection=="terminal-budget-core"){terminalBudgetCases(false,false,false,true);std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  if(selection=="terminal-budget-fresh-fault"){freshFaultExhaustionCases();std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  if(selection=="terminal-budget"){terminalBudgetCases();std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  if(selection=="healthy-idle-minute"){healthyIdleMinuteCases();std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  if(selection=="late-terminal-successor"){successorContinuationCases();std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  if(selection=="late-terminal"){lateTerminalTransportCases();std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;}
  // The origin's observation clock is authoritative at actual admission,
  // even when Maintenance queued the token earlier. Cached I/O ports only.
  if(selection=="all"||selection=="late-queue"){
   const auto w=prepare();offered(w);const auto until=ymcc::inputHost::runtimeRecovery::state.observationUntil;
   returned();tick=until-1;maintain();require(queue.size()==1,"recovery may queue just before observation deadline");
   tick=until;drain();require(startCalls==0&&charges==0&&!g_inputHostRuntimeRecoveryPending,
    "queued recovery must not admit after original observation deadline");
   require(g_virtualStartProtection.view(tick).requestId==w.request,"late queue preserves original failed period");
   done("queued-before-deadline-executes-after-deadline");
  }
  if(selection=="all"||selection=="admission-deadline"){
   const auto w=prepare();offered(w);const auto until=ymcc::inputHost::runtimeRecovery::state.observationUntil;
   returned();tick=until-1;maintain();advanceDuringTimeoutRecord=until;drain();
   require(oldTimeoutObservations==1&&startCalls==0&&charges==0&&!g_inputHostRuntimeRecoveryPending,
    "timeout observation cannot carry a stale clock into recovery admission");
   require(g_virtualStartProtection.view(tick).requestId==w.request,"deadline crossing cannot mint a distinct ticket");
   done("deadline-crossed-during-final-admission-prefix");
  }
  if(selection=="all"){
   const auto w=prepare();offered(w);const auto until=ymcc::inputHost::runtimeRecovery::state.observationUntil;
   returned();tick=until-1;maintain();drain();const auto next=g_virtualStartProtection.view(tick);
   require(next.requestId!=w.request&&startCalls==1&&charges==1,"admission before observation limit retains one new bounded period");
   require(g_virtualStartProtection.beginVerification(next.requestId,"success",tick),"new period starts normal verification");
   tick+=ymcc::VirtualStartProtection::executionInspectionMs;
   require(g_virtualStartProtection.finishVerification(next.requestId,"success",tick),"already-admitted new period may finish after observation deadline");
   maintain();drain();require(!g_inputHostRuntimeRecoveryPending&&startCalls==1&&charges==1,
    "old observation deadline must not cancel or repeat an admitted recovery period");
   done("admitted-recovery-retains-its-own-thirty-second-period");
  }
  if(selection=="all"){
   const auto w=prepare();offered(w);returned();tick=67000;protectionTickOffset=1000000;
   maintain();drain();const auto next=g_virtualStartProtection.view(inputHostStartProtectionNow());
   require(next.requestId!=w.request&&startCalls==1&&charges==1,
    "R3 clock offset must not be compared with boot observation deadline");
   done("admission-clock-offset-keeps-boot-deadline-independent");
  }
  if(selection=="all"||selection=="live"){
   auto w=prepare();offered(w);tick=67000;maintain();drain();
   require(startCalls==0&&charges==0&&queue.empty(),"live native writer cannot authorize recovery");
   g_msiNativeForegroundCyclePending=false;tick+=1001;maintain();drain();
   require(startCalls==0&&charges==0,"uncleared native lease remains ownership debt");field225328LongWriterCases();done("live-writer-and-uncleared-lease");
  }
  if(selection=="all"||selection=="duplicate"){
   for(const auto* persona:{"steamdeck","dualsense-edge","elite"}){
    const auto w=prepare(persona);offered(w);const auto until=ymcc::inputHost::runtimeRecovery::state.observationUntil;
    tick+=100;inputHostRuntimeRecoveryObserveNativeWait(w);require(ymcc::inputHost::runtimeRecovery::state.observationUntil==until,"duplicate defer cannot renew observation");
    tick=32000;require(g_virtualStartProtection.view(tick).phase=="expired","old player period really expires");
    require(!g_virtualStartProtection.consume(persona,tick).created,"ordinary consume still preserves expired old ticket");
    require(!g_virtualStartProtection.admitRecoveryAfterTerminal(w.request+1,persona,tick).accepted,"wrong old terminal owner is refused atomically");
    require(!g_virtualStartProtection.admitRecoveryAfterTerminal(w.request,"disabled",tick).accepted,"OFF persona cannot borrow terminal recovery admission");
    require(!g_virtualStartProtection.admitRecoveryAfterTerminal(w.request,persona==std::string("elite")?"steamdeck":"elite",tick).accepted,"different persona cannot borrow old terminal owner");
    returned();maintain();drain();const auto next=g_virtualStartProtection.view(tick);
    require(next.requestId!=w.request&&next.locked&&startCalls==1&&charges==1&&stopCalls==0,"one distinct recovery ticket only after returned writer");
    require(oldTimeoutObservations==1,"old timeout is observed before new period");
    for(unsigned n=0;n<6;++n){tick+=1001;maintain();drain();}
    require(startCalls==1&&charges==1,"repeated maintenance cannot duplicate pre-host recovery");
    tick+=10000;g_virtualStartProtection.finish(next.requestId,"failure",tick);tick+=10001;maintain();drain();
    require(!g_inputHostRuntimeRecoveryPending&&startCalls==1&&charges==1,"failed pre-host recovery does not borrow persistent transport waves");
   }done("three-personas-one-new-period-no-wave-refund");
  }
  if(selection=="all"){
   for(unsigned edge=0;edge<7;++edge){auto w=prepare();offered(w);tick=67000;returned();
    if(edge==0){g_realStickTest.virtualTargetEnabled=false;configured="disabled";}
    if(edge==1){++g_inputHostVirtualIntentRevision;g_inputHostRecoveryIntent.invalidate();}
    if(edge==2)++power;
    if(edge==3)g_exitRequested=true;
    if(edge==4)g_powerLifecycle=PowerLifecycle::Suspending;
    if(edge==5)(void)g_virtualStartProtection.admit("elite",tick);
    if(edge==6){g_inputHostEpoch=7;g_inputHostRunId="original-start-already-built-host";g_inputHostProcess=reinterpret_cast<HANDLE>(1);}
    maintain();drain();require(startCalls==0&&charges==0&&!g_inputHostRuntimeRecoveryPending,"OFF/intent/power/exit/new ticket cancels pre-host observation");
   }done("seven-cancellation-edges-including-original-host-created");
   for(unsigned debt=0;debt<6;++debt){auto w=prepare();offered(w);tick=67000;returned();
    if(debt==0)cleanupProven=false;if(debt==1)pipeReturned=false;
    if(debt==2)ymcc::inputHost::targetReturnRuntime::state.session->value.obligations.push_back(1);
    if(debt==3)ymcc::inputHost::targetReturnRuntime::state.proven.push_back(1);
    if(debt==4)g_hidHideVisibilityJournal.active=true;if(debt==5)g_postHideCycleOwned=true;
    maintain();drain();require(startCalls==0&&charges==0,"unproven target/pipe/config/native duty cannot start recovery");
   }done("six-return-debts");
   for(unsigned b=0;b<3;++b){auto w=prepare();offered(w);tick=67000;returned();
    if(b==0)g_inputHostRecoveryEpisodeAttempts=2;if(b==1)rolling=2;if(b==2)g_inputHostRecoveryTerminalLogged=true;
    maintain();drain();require(startCalls==0&&charges==0&&!g_inputHostRuntimeRecoveryPending,"exhausted shared budget cannot mint a pre-host wave");
   }done("three-shared-budget-rejections");
   {auto w=prepare();offered(w);tick=181001;returned();maintain();drain();require(!g_inputHostRuntimeRecoveryPending&&startCalls==0,"background observation is bounded without a new UI lock");done("observation-ceiling");}
   {auto w=prepare();w.bound=false;inputHostRuntimeRecoveryObserveNativeWait(w);require(!g_inputHostRuntimeRecoveryPending,"unbound policy work cannot invent a pre-host incident");done("unbound-refusal");}
   {auto w=prepare();offered(w);returned();tick=2000;maintain();drain();require(startCalls==0&&g_virtualStartProtection.view(tick).requestId==w.request,"returned writer cannot preempt the still-running original period");done("original-period-first");}
   {auto w=prepare();offered(w);returned();tick=32000;sourceHeld=true;maintain();drain();require(startCalls==0&&charges==0,"unknown source holds without consuming an attempt");sourceHeld=false;g_xinputPreTargetBoundaryScanned=false;tick+=1001;maintain();drain();require(startCalls==0&&charges==0&&g_xinputSlotFullScanOnce,"canonical boundary scan is requested before recovery admission");done("source-and-boundary-unknown");}
   {auto w=prepare();offered(w);returned();tick=32000;maintain();const auto serial=ymcc::inputHost::runtimeRecovery::state.serial;configured="disabled";g_realStickTest.virtualTargetEnabled=false;drain();require(startCalls==0&&charges==0,"late queued recovery cannot cross OFF");inputHostPerformRuntimeRecovery(serial);require(startCalls==0,"duplicate stale serial cannot revive OFF");done("queued-off-and-duplicate-token");}
  }
  require(cases>0,"unknown or empty selector");
  std::cout<<json{{"caseGroups",cases},{"selection",selection},{"hardwareIo",false},{"wholeNativeConsumer",false}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"ASSERT "<<e.what()<<'\n';return 2;}
}
