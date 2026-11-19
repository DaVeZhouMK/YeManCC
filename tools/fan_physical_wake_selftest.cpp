#include "../native/fan_physical_wake.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <cstring>
using ymcc::fan_wake::Observation;
int checks=0,failures=0;
void check(const char* name,bool ok){++checks;if(!ok)++failures;std::cout<<(ok?"PASS ":"FAIL ")<<name<<"\n";}
// The original runner supplies unmodified production function bodies from main.cpp.
// Only their external dependencies are stubs: no Windows, Host, network or hardware I/O.
#ifdef YMCC_FAN_PHYSICAL_WAKE_NATIVE_CALLERS
#include <cstdint>
#include "../native/sleep_power_observation.h"
#include <deque>
#include <functional>
#include <initializer_list>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
namespace ymcc::sleeprequest {enum class Mode {None};}
namespace native_fixture {
using Admission=decltype(::std::declval<Observation&>().admitClose(0));
using json=::nlohmann::json;
enum class PowerLifecycle { Ready, Suspended, Resuming, Suspending };
Observation g_fanPhysicalWake;
::std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
::std::atomic<bool> g_exitRequested{false},g_fanSleepCleanupInFlight{false};
::std::atomic<unsigned long long> g_fanSleepCleanupGeneration{0};
::std::atomic<::std::uint64_t> g_resumeReadyGeneration{2};
::std::atomic<bool> g_inputReady{false};
::std::uint64_t g_resumeReadyTick=0,fakeTick=1000,lastCommitGeneration=0;
bool hardwareGate=true;
bool sharedPowerKnown=true,sharedPowerPending=false,sharedPowerAlwaysMoves=false,sharedDiagnosticPublication=false;
unsigned sharedPowerReads=0, sharedPowerPendingFromRead=0;
int commitCalls=0;
using WPARAM=::std::uintptr_t;
using LPARAM=::std::intptr_t;
struct FanKernelPowerFact {int eventId;unsigned long long fileTime;};
constexpr unsigned ES_CONTINUOUS=1,ES_SYSTEM_REQUIRED=2,WM_FAN_PHYSICAL_WAKE_READY=3;
int g_hwnd=1,disarms=0,workerStarts=0,readyPosts=0,readyLogs=0,wakeLogs=0,sharedWakeOffers=0;
bool active=false,failThread=false,sleepRetry=false,throwSuspend=false;
bool failSuspendNotification=false,failClosePost=false;
using ULONGLONG=unsigned long long;
enum class SgWork { WakeHibernate };
ULONGLONG g_sgAcceptedS4WakeFileTime=0;
::std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{0},g_sgLatestUserSleepEntryFileTime{0};
struct S4SleepBoundary {
 ULONGLONG fileTime100ns=0,sequence=0;
 explicit operator bool() const {return sequence!=0;}
};
static struct { S4SleepBoundary boundary; S4SleepBoundary latestSleepBoundary() const {return boundary;} } g_sleepPowerDispatch;
static struct { ULONGLONG dispatchFileTime=0; } g_sgInternalSleepRequest;
static struct { bool active() const {return false;} void shutdown(){} } g_radialOverlay;
::std::atomic<::ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget;
bool g_sgModernStandbyActive=false,renewS4=true;
unsigned s4CommonWake=0;
static ULONGLONG sgNowFileTime(){return fakeTick;}
static void sgRecordFact(const char*,const json&){}
static bool sgRenewCancellationReady(ULONGLONG){return renewS4;}
static void radialInputIsolationInvalidate(){}
static void sgStopResumeClassificationWait(const char*){}
static void sgCompleteSharedWakeClassification(ULONGLONG){}
static void sgSleepTimeoutStop(const char*){}
static void sgClearInternalSleepRequest(const char*){g_sgInternalSleepRequest={};}
static void sgClearUnexpectedWake(const char*,bool){}
static void sgAbortSleepIntent(const char*,ULONGLONG){sleepRetry=false;}
static void handlePowerResumeNotification(SgWork,const char*){++s4CommonWake;}
static void sleepRecoveryAutoArm(ULONGLONG){}
unsigned executionThread=0;
::std::vector<unsigned> executionStates{0};
bool executionRequestsOwned=true;
unsigned long long sharedGeneration=2;
::std::vector<::std::string> events;
::std::vector<json> eventDetails;
::std::vector<unsigned long long> closes,resumes;
::std::deque<::std::function<void()>> workers;
static unsigned long long currentPowerGeneration(){return sharedGeneration;}
static unsigned long long currentFanPowerGeneration(){return g_fanPhysicalWake.generation(sharedGeneration);}
static bool fanActivityIntentPresent(){return active;}
static bool sgSleepRetryActive(){return sleepRetry;}
static void publishSharedPowerLifecycle(const char*){++sharedWakeOffers;}
// The provider owns its registry; these caller tests observe no real consumers.
static void revokeSharedPowerMirrors() noexcept {}
// Only external policy/clock state is fake; Fan physical/write/resume gates are
// extracted below. No input, power API, Steam or hardware side effects execute.
static bool hardwareWriteAllowed(){return hardwareGate&&g_powerLifecycle.load()==PowerLifecycle::Ready;}
static int __cdecl readSharedSystemPower(::ymcc::sleeppower::SharedPowerV1* output,::std::uint32_t size){
 ++sharedPowerReads;if(!sharedPowerKnown||g_exitRequested.load()||size<sizeof(*output))return 0;
 *output={};output->generation=sharedGeneration;output->suspendSerial=3;
 output->observedNotificationSequence=sharedPowerAlwaysMoves?sharedPowerReads:4;
 output->publicationRevision=sharedDiagnosticPublication?sharedPowerReads:9;
 const auto phase=g_powerLifecycle.load();output->phase=phase==PowerLifecycle::Ready?0:phase==PowerLifecycle::Suspending?1:phase==PowerLifecycle::Suspended?2:3;
 const bool pending=sharedPowerPending||(sharedPowerPendingFromRead&&sharedPowerReads>=sharedPowerPendingFromRead);
 output->flags=::ymcc::sleeppower::kKnown|(hardwareWriteAllowed()?::ymcc::sleeppower::kHardwareAllowed:0u)|(pending?::ymcc::sleeppower::kNotificationPending:0u);return 1;
}
static bool captureSharedPowerLifecycle(::ymcc::sleeppower::SharedPowerV1& power){
 return ::ymcc::sleeppower::capturePowerObservation(readSharedSystemPower,power);
}
static bool sgHibernateAvailable(){return false;}
static ::std::uint64_t GetTickCount64(){return fakeTick;}
static const char* powerLifecycleName(PowerLifecycle p){switch(p){case PowerLifecycle::Ready:return "ready";case PowerLifecycle::Suspended:return "suspended";case PowerLifecycle::Resuming:return "resuming";default:return "suspending";}}
// Boundary-only commit substitute: observe the shared generation requested by
// the real manual IPC, without calling the product's unrelated power resources.
static bool commitPowerResume(unsigned long long g,const char*){
 ++commitCalls;lastCommitGeneration=g;
 if(!g||g!=currentPowerGeneration())return false;
 if(g_powerLifecycle.load()==PowerLifecycle::Ready)return true;
 if(g_powerLifecycle.load()!=PowerLifecycle::Resuming||g_resumeReadyGeneration.load()!=g)return false;
 g_powerLifecycle.store(PowerLifecycle::Ready);hardwareGate=true;return true;
}
static void fanLeaseKeepaliveDisarm(const char*){++disarms;}
static void ipc_emit(const char* event,const json& detail){
 if(failSuspendNotification&&::std::strcmp(event,"fan.suspending")==0)throw ::std::runtime_error("fixture-suspend-notification-failed");
 events.emplace_back(event);eventDetails.push_back(detail);
}
static void appendFanLifecycleLog(const char* event,const json&){if(::std::string(event)=="fan-physical-wake-ready")++readyLogs;else if(::std::string(event)=="fan-physical-wake-observed")++wakeLogs;}
struct FanResumeScheduleResult {
 bool registered=false;unsigned long long manualOperation=0;bool awaitingPower=false;
 const char* disposition() const noexcept {return registered?"registered":awaitingPower?"awaiting-power":"not-registered";}
};
static bool fanHostPowerMayResume();
static FanResumeScheduleResult fanHostScheduleFanResume(unsigned long long g,bool manual=false,unsigned long long continuing=0){
 if(g!=currentFanPowerGeneration()||(!manual&&!active)||g_exitRequested.load())return {};
 if(!fanHostPowerMayResume())return {false,0,!continuing};
 if(!continuing)resumes.push_back(g);
 return {true,manual?(continuing?continuing:1ULL):0ULL};
}
static bool fanHostEmergencySuspend(const char*,unsigned long long g){closes.push_back(g);executionRequestsOwned=executionRequestsOwned&&executionThread!=0&&executionStates[0]==0&&(executionStates.at(executionThread)&ES_SYSTEM_REQUIRED)!=0;if(throwSuspend)throw ::std::runtime_error("fixture-suspend-failed");return true;}
static unsigned SetThreadExecutionState(unsigned state){const auto previous=executionStates.at(executionThread);executionStates.at(executionThread)=state&ES_SYSTEM_REQUIRED;return previous;}
static bool PostMessageW(int,unsigned,WPARAM,int){++readyPosts;return !failClosePost;}
static void traceLog(const char*,...){}
static void traceLogTry(const char*,...) noexcept {}
// Resolve only the extracted functions' std::thread to this deterministic
// scheduler. detach retains the real worker body; drain explicitly opens its
// Close waiting gate. The original concurrent header test uses real std::thread.
namespace controlled_std {
using ::std::string;
using ::std::unique_ptr;
using ::std::memory_order_acquire;
using ::std::memory_order_release;
class thread {
 ::std::function<void()> work_;
public:
 template<class F> explicit thread(F&& work):work_(::std::forward<F>(work)){
  if(failThread)throw ::std::runtime_error("fixture-thread-schedule-failed");
 }
 void detach(){workers.push_back(::std::move(work_));++workerStarts;}
};
}
namespace std=controlled_std;
#include "fan_physical_wake_native_gates.inc"
#include "fan_physical_wake_native_queue.inc"
#include "fan_physical_wake_native_publish.inc"
static long fanKernelPowerFact(LPARAM l){
#include "fan_physical_wake_native_fact.inc"
}
static json fanLifecycleSnapshot(const json&){
#include "fan_physical_wake_native_lifecycle.inc"
}
static json fanManualWake(const json& a){
#include "fan_physical_wake_native_manual.inc"
}
static void dispatchFact(int eventId,unsigned long long at){
 fanKernelPowerFact(reinterpret_cast<LPARAM>(new FanKernelPowerFact{eventId,at}));
}
static void reset(){
 sharedPowerKnown=true;sharedPowerPending=false;sharedPowerAlwaysMoves=false;sharedDiagnosticPublication=false;sharedPowerReads=sharedPowerPendingFromRead=0;
 check("native caller case leaves no waiting worker",workers.empty());
 workers.clear();g_fanPhysicalWake.~Observation();new (&g_fanPhysicalWake) Observation;
 g_powerLifecycle.store(PowerLifecycle::Ready);g_exitRequested.store(false);
 g_fanSleepCleanupInFlight.store(false);g_fanSleepCleanupGeneration.store(0);
  disarms=workerStarts=readyPosts=readyLogs=wakeLogs=sharedWakeOffers=0;active=failThread=sleepRetry=throwSuspend=failSuspendNotification=failClosePost=false;sharedGeneration=2;
  executionThread=0;executionStates.assign(1,0);executionRequestsOwned=true;
 events.clear();eventDetails.clear();closes.clear();resumes.clear();
 g_resumeReadyGeneration.store(sharedGeneration);g_inputReady.store(false);g_resumeReadyTick=0;fakeTick=1000;lastCommitGeneration=0;hardwareGate=true;commitCalls=0;
 g_sgAcceptedS4WakeFileTime=0;g_sleepPowerDispatch.boundary={};g_sgLastPowerButtonSleepIntentFileTime=0;
 g_sgLatestUserSleepEntryFileTime=0;g_sgInternalSleepRequest={};renewS4=true;s4CommonWake=0;
}
static void drain(){
  while(!workers.empty()){auto work=::std::move(workers.front());workers.pop_front();const auto caller=executionThread;executionThread=static_cast<unsigned>(executionStates.size());executionStates.push_back(0);try{work();}catch(...){executionThread=caller;throw;}executionThread=caller;}
}
static void testSharedPower(){
 reset();auto value=fanLifecycleSnapshot({});
 check("shared coherent cold snapshot is known",value.value("known",false)&&value.value("hardwareWritesAllowed",false));
 check("shared current metadata retained",value.value("observedNotificationSequence",0u)==4&&value.value("suspendSerial",0u)==3&&value.value("sharedGeneration",0ULL)==sharedGeneration);
 reset();sharedPowerKnown=false;value=fanLifecycleSnapshot({});
 check("shared UNKNOWN cannot permit Fan writes",!value.value("known",true)&&!value.value("hardwareWritesAllowed",true)&&!value.value("resumeReady",true));
 check("shared UNKNOWN rejects direct write and resume gates",!fanHostPowerMayWrite()&&!fanHostPowerMayResume());
 reset();sharedPowerPending=true;value=fanLifecycleSnapshot({});
 check("shared pending cannot permit Fan writes",value.value("notificationPending",false)&&!value.value("hardwareWritesAllowed",true)&&!value.value("resumeReady",true));
 check("shared pending rejects direct write and resume gates",!fanHostPowerMayWrite()&&!fanHostPowerMayResume());
 reset();sharedPowerAlwaysMoves=true;value=fanLifecycleSnapshot({});
 check("moving notification cannot assemble a stale Fan Ready",!value.value("known",true)&&!value.value("hardwareWritesAllowed",true));
 check("moving notification rejects direct gates",!fanHostPowerMayWrite()&&!fanHostPowerMayResume());
 reset();sharedDiagnosticPublication=true;value=fanLifecycleSnapshot({});
 check("diagnostic publication alone preserves healthy Fan control",value.value("known",false)&&value.value("hardwareWritesAllowed",false));
 reset();g_inputReady.store(false);value=fanLifecycleSnapshot({});
 check("input owner readiness is not a Fan write dependency",value.value("known",false)&&value.value("hardwareWritesAllowed",false));
 reset();active=true;dispatchFact(506,100);dispatchFact(507,110);drain();
 value=fanLifecycleSnapshot({});
 check("coherent Fan physical recovery keeps existing scoped admission",value.value("known",false)&&value.value("hardwareWritesAllowed",false)&&fanHostPowerMayResume());
 sharedPowerPending=true;value=fanLifecycleSnapshot({});
 check("new shared pending revokes old physical Fan admission",value.value("notificationPending",false)&&!value.value("hardwareWritesAllowed",true)&&!fanHostPowerMayWrite()&&!fanHostPowerMayResume());
 reset();g_exitRequested.store(true);value=fanLifecycleSnapshot({});
 check("shared provider exit remains unknown and unwritable",!value.value("known",true)&&!value.value("hardwareWritesAllowed",true));
 reset();check("shared power checks leave no native worker",workers.empty());
}
static void testCallers(){
 // Reverse OS delivery: an unpaired 507 is ordering evidence only. It must
 // fence an older 506 without inventing a physical-wake or Close receipt.
 reset();active=true;const auto beforeEarlyWake=currentFanPowerGeneration();
 dispatchFact(507,200);dispatchFact(507,150);dispatchFact(507,200);
 check("early507 is ordering only and creates no physical or recovery authority",g_fanPhysicalWake.ready(beforeEarlyWake)&&!g_fanPhysicalWake.physical(beforeEarlyWake)&&workers.empty()&&events.empty()&&resumes.empty()&&wakeLogs==0);
 dispatchFact(506,100);
 check("late506 below early507 cannot revoke already Ready Fan",currentFanPowerGeneration()==beforeEarlyWake&&g_fanPhysicalWake.ready(beforeEarlyWake)&&!g_fanPhysicalWake.awaitingWake(beforeEarlyWake)&&workers.empty()&&events.empty()&&disarms==0);
 drain();fanHostPublishPhysicalWake(currentFanPowerGeneration());
 check("late506 after early507 creates no Close or recovery",closes.empty()&&resumes.empty()&&readyLogs==0&&events.empty()&&executionStates[0]==0);
 reset();active=true;const auto beforeNewSleep=currentFanPowerGeneration();
 dispatchFact(507,200);dispatchFact(506,300);const auto actualNewSleep=currentFanPowerGeneration();
 check("genuinely newer506 after early507 still admits one Close",actualNewSleep>beforeNewSleep&&!g_fanPhysicalWake.ready(actualNewSleep)&&g_fanPhysicalWake.awaitingWake(actualNewSleep)&&workerStarts==1&&disarms==1&&workers.size()==1);
 dispatchFact(507,200);
 check("older507 cannot wake genuinely newer506",!g_fanPhysicalWake.physical(actualNewSleep)&&resumes.empty());
 dispatchFact(507,400);
 check("new paired507 still waits for its serialized Close",g_fanPhysicalWake.physical(actualNewSleep)&&!g_fanPhysicalWake.ready(actualNewSleep)&&resumes.empty());
 drain();fanHostPublishPhysicalWake(actualNewSleep);fanHostPublishPhysicalWake(actualNewSleep);
 check("newer506 paired507 completes one Close and one recovery with execution request returned",g_fanPhysicalWake.ready(actualNewSleep)&&closes==::std::vector<unsigned long long>{actualNewSleep}&&resumes==::std::vector<unsigned long long>{actualNewSleep}&&readyLogs==1&&workers.empty()&&!g_fanSleepCleanupInFlight.load()&&executionRequestsOwned&&executionStates[0]==0&&executionStates.back()==0);
 reset();active=true;dispatchFact(506,100);drain();const auto deferredFact=currentFanPowerGeneration();
 sharedPowerKnown=false;dispatchFact(507,200);
 check("507 with unknown admission asks the public owner to deliver and keeps the ready marker",sharedWakeOffers==1&&resumes.empty()&&readyLogs==0&&g_fanPhysicalWake.readyUnpublished(deferredFact));
 sharedPowerKnown=true;fanHostPublishPhysicalWake(deferredFact,true);
 check("public readmission can publish the retained 507 without another kernel fact",resumes==::std::vector<unsigned long long>{deferredFact}&&readyLogs==1&&!g_fanPhysicalWake.readyUnpublished(deferredFact));
 reset();active=true;dispatchFact(506,100);dispatchFact(507,200);drain();const auto deferredClose=currentFanPowerGeneration();
 sharedPowerKnown=false;fanCloseWake(static_cast<WPARAM>(deferredClose));
 check("Close completion with unknown admission asks the same public owner and keeps the ready marker",sharedWakeOffers==1&&resumes.empty()&&readyLogs==0&&g_fanPhysicalWake.readyUnpublished(deferredClose));
 sharedPowerKnown=true;fanHostPublishPhysicalWake(deferredClose,true);
 check("readmission can publish the paired wake without repeating Close",resumes==::std::vector<unsigned long long>{deferredClose}&&readyLogs==1&&closes.size()==1);
 {
  Observation orphan;const auto current=orphan.generation(2);
  check("current shared unpaired wake carries no physical authority",!orphan.recordWake(2,200,false)&&orphan.ready(current)&&!orphan.physical(current));
  check("old shared unpaired wake cannot poison active shared watermark",!orphan.recordWake(1,900,false));
  const auto actual=orphan.recordSleep(2,300);
  check("old shared watermark cannot block a genuine current-shared cycle",actual>current&&orphan.beginClose(actual)&&orphan.recordWake(2,400,false)&&!orphan.ready(actual)&&orphan.completeClose(actual)==0&&orphan.ready(actual));
 }
 // A notification can arrive after the observed-only IPC checks Ready and
 // before the recovery scheduler reads the same power source again.
 reset();active=true;
 const auto observedGeneration=currentFanPowerGeneration();
 const json observedOnly{{"generation",observedGeneration},{"observedWakeOnly",true}};
 sharedPowerPendingFromRead=3;
 const auto interrupted=fanManualWake(observedOnly);
 check("observed wake with zero registered work reports retryable rejection",
     !interrupted.value("ok",true)&&!interrupted.value("admitted",true)&&
     interrupted.value("reason",::std::string())=="wake_not_ready"&&
     interrupted.value("generation",0ULL)==observedGeneration&&sharedPowerReads==3&&resumes.empty());
 sharedPowerPendingFromRead=0;
 const auto observed=fanManualWake(observedOnly);
 check("same observed wake can register after Ready settles without a manual wake",
     observed.value("ok",false)&&observed.value("admitted",false)&&observed.value("resumeScheduled",false)&&
     resumes==::std::vector<unsigned long long>{observedGeneration}&&commitCalls==0&&workerStarts==0);
 // Actual IPC/snapshot closure: wall-clock passage never creates a new OS fact.
 reset();dispatchFact(506,100);dispatchFact(507,110);
 const auto manualGeneration=currentFanPowerGeneration();
 fakeTick+=20ULL*60ULL*1000ULL;
 const auto aged=fanLifecycleSnapshot(json::object());
 check("real Fan snapshot after inactive wake and twenty minutes is resuming without writes",aged.at("phase")=="resuming"&&!aged.at("hardwareWritesAllowed").get<bool>()&&aged.at("generation").get<::std::uint64_t>()==manualGeneration&&g_fanPhysicalWake.physical(manualGeneration)&&workers.empty()&&events.empty());
 const auto manual=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(manualGeneration)}});
 const auto pending=fanLifecycleSnapshot(json::object());
 check("one actual manual IPC accepts current Fan generation without claiming finished Close",manual.at("ok").get<bool>()&&manual.at("reason")=="already_ready"&&pending.at("phase")=="resuming"&&!pending.at("hardwareWritesAllowed").get<bool>()&&workerStarts==1&&disarms==1&&events==::std::vector<::std::string>{"fan.suspending"}&&commitCalls==0);
 check("actual manual IPC distinguishes waiting for Close from registering Host work",manual.at("resumeDisposition")=="awaiting-power"&&!manual.at("resumeScheduled").get<bool>()&&resumes.empty());
 const auto beforeClose=fanManualWake(json{{"generation",manualGeneration},{"continueReadyOnly",true}});
 check("Ready only continuation cannot start Host work while Close is pending",!beforeClose.at("ok").get<bool>()&&beforeClose.at("reason")=="wake_not_ready"&&workerStarts==1&&resumes.empty());
 drain();
 const auto completed=fanLifecycleSnapshot(json::object());
 check("one manual Close completion makes actual Fan snapshot ready without input readiness",completed.at("phase")=="ready"&&completed.at("hardwareWritesAllowed").get<bool>()&&!completed.at("inputReady").get<bool>()&&closes==::std::vector<unsigned long long>{manualGeneration});
 check("actual manual Close catchup notification preserves the initiating wake intent marker",eventDetails.size()==1&&eventDetails[0].at("scope")=="fan-only"&&eventDetails[0].value("resumeCatchup",false));
 const auto duplicateReady=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(manualGeneration)},{"continueReadyOnly",true}});
 check("actual ready duplicate manual IPC preserves ready without extra invalidation",duplicateReady.at("ok").get<bool>()&&fanLifecycleSnapshot(json::object()).at("phase")=="ready"&&workerStarts==1&&disarms==1&&events.size()==1);
 check("actual continuation after Close reports its Native request registered",duplicateReady.at("resumeDisposition")=="registered"&&duplicateReady.at("resumeScheduled").get<bool>()&&resumes==::std::vector<unsigned long long>{manualGeneration});
 for (const auto sharedPhase : {PowerLifecycle::Resuming, PowerLifecycle::Suspended}) {
  reset();dispatchFact(506,100);dispatchFact(507,110);
  const auto scopedGeneration=currentFanPowerGeneration();
  const json continuation{{"generation",scopedGeneration},{"continueReadyOnly",true}};
  const auto first=fanManualWake(json{{"generation",scopedGeneration}});
  drain();
  active=true;hardwareGate=false;g_resumeReadyGeneration.store(0);
  g_powerLifecycle.store(sharedPhase);
  sharedPowerPending=true;
  const auto pending=fanManualWake(continuation);
  check("Fan-only continuation cannot ignore a pending shared notification",
      first.at("resumeDisposition")=="awaiting-power"&&!pending.value("admitted",true)&&
      pending.at("reason")=="wake_not_ready"&&resumes.empty()&&commitCalls==0);
  sharedPowerPending=false;
  const auto scoped=fanLifecycleSnapshot({});
  check("matched Fan wake and completed Close project Ready before shared Ready",
      scoped.at("phase")=="ready"&&scoped.value("hardwareWritesAllowed",false)&&
      scoped.value("physicalWakeObserved",false)&&!scoped.value("sharedPowerCommitted",true)&&
      scoped.value("generation",0ULL)==scopedGeneration&&closes==::std::vector<unsigned long long>{scopedGeneration});
  g_powerLifecycle.store(PowerLifecycle::Suspending);
  const auto enteringSleep=fanManualWake(continuation);
  check("Fan-only continuation cannot cross active shared Suspending",
      !enteringSleep.value("admitted",true)&&enteringSleep.at("reason")=="power_suspending"&&
      resumes.empty()&&commitCalls==0);
  g_powerLifecycle.store(sharedPhase);
  const auto continued=fanManualWake(continuation);
  check(sharedPhase==PowerLifecycle::Resuming ? "Fan-only Ready continues during shared Resuming" : "Fan-only Ready continues during shared Suspended",
      continued.value("ok",false)&&continued.value("admitted",false)&&continued.value("resumeScheduled",false)&&
      continued.value("generation",0ULL)==scopedGeneration&&resumes==::std::vector<unsigned long long>{scopedGeneration}&&
      workerStarts==1&&disarms==1&&events.size()==1&&commitCalls==0&&g_resumeReadyGeneration.load()==0&&
      g_powerLifecycle.load()==sharedPhase);
 }
 reset();dispatchFact(506,100);dispatchFact(507,110);
 const auto resumingGeneration=currentFanPowerGeneration();
 g_powerLifecycle.store(PowerLifecycle::Resuming);
 const auto duringResume=fanManualWake(json{{"generation",resumingGeneration},{"continueReadyOnly",true}});
 check("actual Ready-only continuation in shared Resuming rejects before offering work",
     !duringResume.value("ok",true)&&!duringResume.value("admitted",true)&&
     duringResume.at("phase")=="resuming"&&duringResume.at("reason")=="wake_not_ready"&&
     duringResume.value("generation",0ULL)==resumingGeneration&&resumes.empty()&&workerStarts==0&&commitCalls==0);
 reset();dispatchFact(506,100);dispatchFact(507,110);
 const auto pendingGeneration=currentFanPowerGeneration();
 fanManualWake(json{{"generation",static_cast<::std::uint64_t>(pendingGeneration)}});
 const auto duplicatePending=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(pendingGeneration)}});
 check("actual pending duplicate manual IPC does not repeat invalidation",duplicatePending.at("ok").get<bool>()&&workerStarts==1&&disarms==1&&events.size()==1);
 drain();
 reset();dispatchFact(506,100);
 const auto blockedGeneration=currentFanPowerGeneration();
 g_powerLifecycle.store(PowerLifecycle::Suspending);
 const auto blocked=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(blockedGeneration)}});
 check("actual Suspending manual IPC rejects before mutating sleep or Close",!blocked.at("ok").get<bool>()&&blocked.at("reason")=="power_suspending"&&g_fanPhysicalWake.awaitingWake(blockedGeneration)&&!g_fanPhysicalWake.physical(blockedGeneration)&&g_fanPhysicalWake.needsClose(blockedGeneration)&&workers.empty()&&events.empty()&&disarms==0&&resumes.empty()&&commitCalls==0&&g_powerLifecycle.load()==PowerLifecycle::Suspending);
 g_powerLifecycle.store(PowerLifecycle::Ready);
 const auto stale=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(blockedGeneration-1)}});
 check("actual stale-generation manual IPC rejects before mutating sleep or Close",!stale.at("ok").get<bool>()&&stale.at("reason")=="stale_generation"&&g_fanPhysicalWake.awaitingWake(blockedGeneration)&&g_fanPhysicalWake.needsClose(blockedGeneration)&&workers.empty()&&events.empty()&&disarms==0&&resumes.empty()&&commitCalls==0);
 g_powerLifecycle.store(PowerLifecycle::Suspended);
 const auto retired=fanManualWake(json{{"generation",blockedGeneration},{"continueReadyOnly",true}});
 check("Ready only continuation cannot promote sleep even at the same Fan generation",!retired.at("ok").get<bool>()&&retired.at("reason")=="wake_not_ready"&&g_fanPhysicalWake.awaitingWake(blockedGeneration)&&g_fanPhysicalWake.needsClose(blockedGeneration)&&workers.empty()&&resumes.empty()&&commitCalls==0);
 reset();dispatchFact(506,100);dispatchFact(507,110);
 const auto suspendedFan=currentFanPowerGeneration();
 g_powerLifecycle.store(PowerLifecycle::Suspended);hardwareGate=false;g_resumeReadyGeneration.store(0);
 const auto promoted=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(suspendedFan)}});
 check("actual manual missed-edge IPC commits shared generation instead of private Fan generation",suspendedFan!=sharedGeneration&&promoted.at("ok").get<bool>()&&commitCalls==1&&lastCommitGeneration==sharedGeneration&&g_resumeReadyGeneration.load()==sharedGeneration&&g_resumeReadyTick==fakeTick&&g_powerLifecycle.load()==PowerLifecycle::Ready&&!fanLifecycleSnapshot(json::object()).at("hardwareWritesAllowed").get<bool>());
 check("committing shared Ready cannot claim Host work while private Close is pending",promoted.at("resumeDisposition")=="awaiting-power"&&!promoted.at("resumeScheduled").get<bool>()&&resumes.empty());
 drain();
 check("actual missed-edge manual recovery reaches ready only after its Close",fanLifecycleSnapshot(json::object()).at("phase")=="ready"&&fanLifecycleSnapshot(json::object()).at("hardwareWritesAllowed").get<bool>()&&workerStarts==1);
 reset();dispatchFact(506,100);
 const auto abandonedOld=currentFanPowerGeneration();
 check("abandon setup has one old owner",g_fanPhysicalWake.admitClose(abandonedOld)==Admission::StartWorker);
 dispatchFact(506,200);const auto abandonedNew=currentFanPowerGeneration();
 check("abandon setup retains newer queued generation",g_fanPhysicalWake.admitClose(abandonedNew)==Admission::Queued);
 g_fanPhysicalWake.abandonClose(abandonedOld);dispatchFact(507,300);
 check("abandon never turns newer wake into completed Close proof",g_fanPhysicalWake.needsClose(abandonedNew)&&!g_fanPhysicalWake.ready(abandonedNew)&&g_fanPhysicalWake.completeClose(abandonedOld)==0);
 const auto recovered=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(abandonedNew)}});drain();
 check("actual manual IPC recovers newer generation after old schedule abandonment",recovered.at("ok").get<bool>()&&fanLifecycleSnapshot(json::object()).at("phase")=="ready"&&fanLifecycleSnapshot(json::object()).at("hardwareWritesAllowed").get<bool>()&&workerStarts==1&&disarms==1&&closes==::std::vector<unsigned long long>{abandonedNew});
 // Consume the actual production WM_FAN_KERNEL_POWER_FACT body. An inactive
 // control session records both OS facts but does not start cleanup or recovery.
 reset();dispatchFact(506,100);
 const auto factGeneration=currentFanPowerGeneration();
 check("real native inactive 506 records sleep without starting Host work",g_fanPhysicalWake.awaitingWake(factGeneration)&&workerStarts==0&&disarms==0&&events.empty()&&resumes.empty());
 dispatchFact(507,110);
 check("real native inactive 507 retains physical wake instead of stale sleeping",g_fanPhysicalWake.physical(factGeneration)&&!g_fanPhysicalWake.awaitingWake(factGeneration)&&wakeLogs==1);
 check("real native inactive fact pair does not manufacture Close or hardware ready",!g_fanPhysicalWake.ready(factGeneration)&&g_fanPhysicalWake.needsClose(factGeneration)&&workerStarts==0&&disarms==0&&events.empty()&&resumes.empty()&&readyLogs==0);
 active=true;fanHostQueueSuspend("explicit-after-inactive-fact-pair",factGeneration);drain();fanHostPublishPhysicalWake(factGeneration);
 check("real native retained inactive wake supports one later explicit Close and publish",g_fanPhysicalWake.ready(factGeneration)&&workerStarts==1&&closes==::std::vector<unsigned long long>{factGeneration}&&resumes==::std::vector<unsigned long long>{factGeneration}&&events==::std::vector<::std::string>{"fan.suspending","fan.resume-ready"}&&readyLogs==1);
 reset();active=true;
 const auto g=g_fanPhysicalWake.confirmedSuspend(sharedGeneration);
 fanHostQueueSuspend("kernel-power-506-physical",g);
 check("actual physical sleep notification cannot claim wake Close catchup",eventDetails.size()==1&&!eventDetails[0].value("resumeCatchup",true));
 check("real native first Close invalidates once and schedules one worker",disarms==1&&events==::std::vector<::std::string>{"fan.suspending"}&&workerStarts==1);
 fanHostQueueSuspend("manual-fan-wake-close",g);
 check("real native same-owner duplicate neither disarms nor emits nor schedules",disarms==1&&events.size()==1&&workerStarts==1);
 fanHostQueueSuspend("stale",g-1);fanHostQueueSuspend("zero",0);
 check("real native stale/zero request has no invalidation side effects",disarms==1&&events.size()==1&&workerStarts==1);
 check("real native paired fact retained while Close waits",g_fanPhysicalWake.recordSleep(sharedGeneration,100)==g&&g_fanPhysicalWake.recordWake(sharedGeneration,110,false));
 const bool staleNeedsClose=g_fanPhysicalWake.needsClose(g);
 fanHostPublishPhysicalWake(g);
 check("real native publisher cannot overtake pending Close",staleNeedsClose&&!g_fanPhysicalWake.ready(g)&&resumes.empty()&&readyLogs==0&&events.size()==1);
 drain();
  check("temporary Close execution request is worker-owned and cleared",executionRequestsOwned&&executionStates.size()==2&&executionStates[0]==0&&executionStates[1]==0);
 check("real native worker completes exactly its one admitted Close",closes==::std::vector<unsigned long long>{g}&&readyPosts==1&&!g_fanSleepCleanupInFlight.load()&&g_fanPhysicalWake.ready(g));
 fanHostPublishPhysicalWake(g);
 check("real native ready publisher schedules and emits once",resumes==::std::vector<unsigned long long>{g}&&readyLogs==1&&events==::std::vector<::std::string>{"fan.suspending","fan.resume-ready"});
 // The caller retained true needsClose before the worker completed: this is the
 // exact stale-read/queue interleaving, without a timing-dependent race.
 fanHostQueueSuspend("manual-fan-wake-close",g);fanHostPublishPhysicalWake(g);
 check("real native closed/TOCTOU duplicate preserves published ready",staleNeedsClose&&disarms==1&&events.size()==2&&workerStarts==1&&resumes.size()==1&&g_fanPhysicalWake.ready(g));
 reset();active=true;
 const auto old=g_fanPhysicalWake.recordSleep(sharedGeneration,100);
 fanHostQueueSuspend("kernel-power-506-physical",old);
 const auto next=g_fanPhysicalWake.recordSleep(sharedGeneration,200);
 fanHostQueueSuspend("kernel-power-506-physical",next);
 check("real native new generation queued behind owner invalidates immediately",next>old&&disarms==2&&events==::std::vector<::std::string>{"fan.suspending","fan.suspending"}&&workerStarts==1&&workers.size()==1);
 fanHostQueueSuspend("duplicate-new-pending",next);
 check("real native same pending generation invalidates only once",disarms==2&&events.size()==2&&workerStarts==1);
 check("real native newer wake cannot bypass either serialized Close",g_fanPhysicalWake.recordWake(sharedGeneration,300,false)&&!g_fanPhysicalWake.ready(next));
 fanHostPublishPhysicalWake(next);
 check("real native queued generation cannot publish early",resumes.empty()&&readyLogs==0&&events.size()==2);
 drain();
 check("real native one worker drains old and newest Close in order",closes==::std::vector<unsigned long long>{old,next}&&workerStarts==1&&readyPosts==1&&g_fanPhysicalWake.ready(next));
 fanHostPublishPhysicalWake(next);
 check("real native newest completion permits one current ready publication",resumes==::std::vector<unsigned long long>{next}&&readyLogs==1&&events.back()=="fan.resume-ready");
 reset();
 const auto inactive=g_fanPhysicalWake.recordSleep(sharedGeneration,100);
 check("inactive metadata cycle obtains only explicit fixture Close proof",g_fanPhysicalWake.admitClose(inactive)==Admission::StartWorker&&g_fanPhysicalWake.completeClose(inactive)==0);
 check("inactive OS wake metadata is retained independently of control intent",g_fanPhysicalWake.recordWake(sharedGeneration,110,false)&&g_fanPhysicalWake.physical(inactive)&&!g_fanPhysicalWake.awaitingWake(inactive)&&g_fanPhysicalWake.ready(inactive));
 fanHostPublishPhysicalWake(inactive);
 check("real native inactive publisher does not emit/log/schedule/claim ready",events.empty()&&readyLogs==0&&resumes.empty()&&workerStarts==0);
 active=true;fanHostPublishPhysicalWake(inactive);fanHostPublishPhysicalWake(inactive);
 check("inactive publish left ready claim available for later active intent once",events==::std::vector<::std::string>{"fan.resume-ready"}&&readyLogs==1&&resumes==::std::vector<unsigned long long>{inactive});
 reset();
 const auto retry=g_fanPhysicalWake.recordSleep(sharedGeneration,100);
 g_fanPhysicalWake.beginClose(retry);g_fanPhysicalWake.completeClose(retry);
 check("inactive metadata still rejects wake while sleep retry is active",!g_fanPhysicalWake.recordWake(sharedGeneration,110,true)&&!g_fanPhysicalWake.physical(retry));
 active=true;fanHostPublishPhysicalWake(retry);
 check("real native retry boundary cannot publish even with later active intent",events.empty()&&resumes.empty()&&readyLogs==0);
 // Observe the retry flag before its boundary is published. A suppressed
 // 507 is not itself a sleep boundary, even when that retry later ends.
 reset();active=true;g_powerLifecycle.store(PowerLifecycle::Suspended);
 dispatchFact(506,100);drain();dispatchFact(507,200);
 const auto retainedWake=currentFanPowerGeneration();
 check("retry overlap starts with one published physical wake and returned Close",
       g_fanPhysicalWake.physical(retainedWake)&&closes.size()==1&&resumes.size()==1&&readyLogs==1);
 sleepRetry=true;dispatchFact(507,150);sleepRetry=false;
 check("suppressed earlier 507 preserves the current physical wake and Ready claim",
       g_fanPhysicalWake.physical(retainedWake)&&!g_fanPhysicalWake.readyUnpublished(retainedWake)&&
       currentFanPowerGeneration()==retainedWake&&closes.size()==1&&resumes.size()==1&&wakeLogs==1&&readyLogs==1);
 const auto afterSuppressed=fanManualWake(json{{"generation",retainedWake},{"continueReadyOnly",true}});
 check("first click can join the retained wake after a zero-work retry ends",
       afterSuppressed.value("resumeScheduled",false)&&resumes.size()==2&&closes.size()==1&&commitCalls==0);
 reset();active=true;g_powerLifecycle.store(PowerLifecycle::Suspended);
 dispatchFact(506,100);drain();dispatchFact(507,200);
 const auto retryWake=currentFanPowerGeneration();
 g_fanPhysicalWake.retryBoundary(sharedGeneration,250);sleepRetry=true;
 dispatchFact(507,225);dispatchFact(507,275);
 check("public retry boundary revokes the old wake and suppresses work through newer 507",
       !g_fanPhysicalWake.physical(retryWake)&&!g_fanPhysicalWake.ready(retryWake)&&closes.size()==1&&resumes.size()==1&&wakeLogs==1&&readyLogs==1);
 sleepRetry=false;dispatchFact(507,225);dispatchFact(507,300);dispatchFact(507,300);
 check("first fresh 507 after public retry resumes once and keeps the original Close",
       g_fanPhysicalWake.physical(retryWake)&&g_fanPhysicalWake.ready(retryWake)&&closes.size()==1&&resumes.size()==2&&wakeLogs==2&&readyLogs==2&&commitCalls==0);
 reset();active=true;g_powerLifecycle.store(PowerLifecycle::Suspended);failSuspendNotification=true;
 bool firstNotifyEscaped=false;
 try{dispatchFact(506,100);}catch(...){firstNotifyEscaped=true;}
 const auto firstAfterLoss=currentFanPowerGeneration();
 failSuspendNotification=false;dispatchFact(507,200);
 check("failed first suspend notification still starts one Close and blocks wake until it returns",
       !firstNotifyEscaped&&disarms==1&&workerStarts==1&&workers.size()==1&&resumes.empty()&&!g_fanPhysicalWake.ready(firstAfterLoss));
 drain();fanHostPublishPhysicalWake(firstAfterLoss);fanHostPublishPhysicalWake(firstAfterLoss);
 check("first lost suspend notification resumes once after its original Close",
       closes==::std::vector<unsigned long long>{firstAfterLoss}&&resumes==closes&&readyLogs==1&&commitCalls==0);
 reset();active=true;g_powerLifecycle.store(PowerLifecycle::Suspended);
 dispatchFact(506,100);const auto priorClose=currentFanPowerGeneration();
 failSuspendNotification=true;bool queuedNotifyEscaped=false;
 try{dispatchFact(506,300);}catch(...){queuedNotifyEscaped=true;}
 const auto queuedAfterLoss=currentFanPowerGeneration();
 failSuspendNotification=false;dispatchFact(507,400);
 check("failed queued suspend notification cannot escape or start a second Close worker",
       !queuedNotifyEscaped&&queuedAfterLoss>priorClose&&workerStarts==1&&disarms==2&&resumes.empty()&&!g_fanPhysicalWake.ready(queuedAfterLoss));
 drain();fanHostPublishPhysicalWake(queuedAfterLoss);
 check("lost queued suspend notification keeps both Close returns in order and resumes only the newest wake",
       closes==::std::vector<unsigned long long>{priorClose,queuedAfterLoss}&&resumes==::std::vector<unsigned long long>{queuedAfterLoss}&&readyLogs==1&&commitCalls==0);
 reset();active=true;failThread=true;
 const auto failed=g_fanPhysicalWake.confirmedSuspend(sharedGeneration);
 fanHostQueueSuspend("schedule-failure",failed);
 check("real native schedule failure retains blocked Close obligation",workers.empty()&&g_fanPhysicalWake.needsClose(failed)&&!g_fanPhysicalWake.ready(failed)&&!g_fanSleepCleanupInFlight.load());
 failThread=false;fanHostQueueSuspend("schedule-retry",failed);drain();
 check("real native scheduling retry uses one successful worker",workerStarts==1&&closes==::std::vector<unsigned long long>{failed}&&!g_fanPhysicalWake.needsClose(failed));
  check("failed scheduling leaves no caller execution request",executionStates[0]==0);
 reset();active=true;g_powerLifecycle.store(PowerLifecycle::Suspended);failThread=true;
 dispatchFact(506,100);const auto withLostStart=currentFanPowerGeneration();
 failThread=false;sleepRetry=true;dispatchFact(507,200);
 active=false;sleepRetry=false;dispatchFact(507,250);
 g_exitRequested.store(true);active=true;dispatchFact(507,300);
 check("retry, off and exit cannot restart an unstarted Close",
       workers.empty()&&closes.empty()&&resumes.empty()&&g_fanPhysicalWake.needsClose(withLostStart));
 g_exitRequested.store(false);dispatchFact(507,400);
 fanHostScheduleEmergencyResume(sharedGeneration);dispatchFact(507,400);
 check("current 507 restarts only the unstarted Close and still blocks Resume",
       workerStarts==1&&workers.size()==1&&closes.empty()&&resumes.empty()&&!fanHostPowerMayWrite());
 drain();fanHostPublishPhysicalWake(withLostStart);fanHostPublishPhysicalWake(withLostStart);
 check("current 507 resumes once after the restarted Close returns",
       closes==::std::vector<unsigned long long>{withLostStart}&&resumes==closes&&readyLogs==1&&commitCalls==0);
 reset();active=true;failThread=true;
 const auto lostOnShared=g_fanPhysicalWake.confirmedSuspend(sharedGeneration);
 fanHostQueueSuspend("schedule-failure",lostOnShared);
 fanHostScheduleEmergencyResume(sharedGeneration);
 check("repeated constructor failure leaves no Close receipt or premature Resume",
       workers.empty()&&closes.empty()&&resumes.empty()&&!fanHostPowerMayWrite());
 failThread=false;fanHostScheduleEmergencyResume(sharedGeneration-1);
 check("stale public wake cannot restart the current Close",workers.empty()&&resumes.empty());
 fanHostScheduleEmergencyResume(sharedGeneration);fanHostScheduleEmergencyResume(sharedGeneration);
 check("current public Ready without 507 restarts the same Close only once",
       workerStarts==1&&workers.size()==1&&closes.empty()&&resumes.empty()&&!fanHostPowerMayWrite());
 drain();fanHostPublishPhysicalWake(lostOnShared);
 check("current public Ready resumes after Close without inventing a physical wake",
       closes==::std::vector<unsigned long long>{lostOnShared}&&resumes==closes&&!g_fanPhysicalWake.physical(lostOnShared)&&commitCalls==0);
 reset();active=true;failThread=true;
 dispatchFact(506,100);const auto obsolete=currentFanPowerGeneration();
 ++sharedGeneration;const auto newer=g_fanPhysicalWake.confirmedSuspend(sharedGeneration);
 failThread=false;fanHostPublishPhysicalWake(obsolete);fanHostScheduleEmergencyResume(sharedGeneration);
 check("unstarted Close marker cannot cross a generation or authorize an unregistered Close",
       newer>obsolete&&workers.empty()&&closes.empty()&&resumes.empty()&&!fanHostPowerMayWrite());
  reset();active=true;throwSuspend=true;
  const auto exceptionCycle=g_fanPhysicalWake.confirmedSuspend(sharedGeneration);
  fanHostQueueSuspend("suspend-exception",exceptionCycle);
  bool escaped=false;try{drain();}catch(...){escaped=true;}
  check("Close boundary exception cannot escape worker or retain execution request",!escaped&&executionRequestsOwned&&executionStates.size()==2&&executionStates[0]==0&&executionStates[1]==0&&!g_fanSleepCleanupInFlight.load());
 check("real native caller fixtures leave no worker pending",workers.empty());
 // A second raw sleep can advance Fan without changing the public generation.
 // The prior Ready and a rejected 107 must not unlock it. The accepted S4
 // handler then records the only wake; this scenario never provides its 507.
 reset();active=true;dispatchFact(506,100);drain();dispatchFact(507,200);
 const auto firstCycle=currentFanPowerGeneration();
 dispatchFact(506,300);const auto hibernateCycle=currentFanPowerGeneration();
 g_sleepPowerDispatch.boundary={300,3};
 const auto oldOffers=sharedWakeOffers;
 const auto oldResumeCount=resumes.size(),oldEventCount=events.size();
 fanHostScheduleEmergencyResume(sharedGeneration);
 fanSharedS4(sharedGeneration-1,400);fanSharedS4(sharedGeneration,250);fanSharedS4(sharedGeneration,1200);
 renewS4=false;fanSharedS4(sharedGeneration,390);renewS4=true;
 check("older, retired, future and unrenewed S4 retain the newer Fan sleep",hibernateCycle>firstCycle&&
     g_fanPhysicalWake.awaitingWake(hibernateCycle)&&!fanHostPowerMayWrite()&&s4CommonWake==0&&sharedWakeOffers==oldOffers&&resumes.size()==oldResumeCount);
 sleepRetry=true;fanSharedS4(sharedGeneration,400);
 check("accepted S4 ends the retry and republishes an already-Ready public generation",!sleepRetry&&
     s4CommonWake==1&&sharedWakeOffers==oldOffers+1&&currentFanPowerGeneration()==hibernateCycle);
 fanHostScheduleEmergencyResume(sharedGeneration);
 check("S4 cannot overtake the exact pending Close",!fanHostPowerMayWrite()&&resumes.size()==oldResumeCount&&events.size()==oldEventCount);
 drain();sharedPowerPending=true;
 check("S4 still obeys the public notification gate after Close",fanCloseWake(hibernateCycle)==0&&
     !fanHostPowerMayWrite()&&resumes.size()==oldResumeCount&&events.size()==oldEventCount);
 sharedPowerPending=false;fanHostScheduleEmergencyResume(sharedGeneration);
 check("accepted S4 without 507 joins original auto recovery after Close",fanHostPowerMayWrite()&&
     closes.size()==2&&closes.back()==hibernateCycle&&resumes.size()==oldResumeCount+1&&resumes.back()==hibernateCycle&&
     events.size()==oldEventCount+1&&eventDetails.back().value("source","")=="kernel-s4-wake"&&
     !g_fanPhysicalWake.physical(hibernateCycle)&&commitCalls==0);
 fanSharedS4(sharedGeneration,400);fanCloseWake(hibernateCycle);
 check("duplicate S4 and Close delivery cannot redispatch the completed wake",s4CommonWake==1&&events.size()==oldEventCount+1&&resumes.size()==oldResumeCount+1);
 const auto click=fanManualWake(json{{"generation",hibernateCycle},{"continueReadyOnly",true}});
 check("first manual click can join the accepted S4 after Close",click.value("resumeScheduled",false)&&
     closes.size()==2&&commitCalls==0);
 dispatchFact(506,500);drain();const auto thirdCycle=currentFanPowerGeneration();
 g_sleepPowerDispatch.boundary={500,4};fanSharedS4(sharedGeneration,450);fanHostScheduleEmergencyResume(sharedGeneration);
 check("newer sleep discards the S4 proof before the next manual or automatic action",thirdCycle>hibernateCycle&&
     g_fanPhysicalWake.awaitingWake(thirdCycle)&&!fanHostPowerMayWrite()&&s4CommonWake==1&&closes.size()==3);
 reset();active=true;g_fanPhysicalWake.confirmedSuspend(sharedGeneration);
 fanSharedS4(sharedGeneration,400);fanHostScheduleEmergencyResume(sharedGeneration);
 const auto sharedFan=currentFanPowerGeneration();
 dispatchFact(506,300);
 check("accepted S4 fences a late raw 506 before it can start a new Close",s4CommonWake==1&&
     sharedFan==currentFanPowerGeneration()&&workers.empty()&&closes.empty()&&events.empty());
 drain();dispatchFact(506,500);drain();
 check("a newer raw 506 still starts one Close after the accepted S4",!fanHostPowerMayWrite()&&
     currentFanPowerGeneration()>sharedFan&&closes.size()==1);

  reset();active=true;dispatchFact(506,100);const auto closePostCycle=currentFanPowerGeneration();
  g_sleepPowerDispatch.boundary={100,3};fanSharedS4(sharedGeneration,200);
  const auto s4Offers=sharedWakeOffers;
  check("Close still owns the accepted S4 when public Ready first arrives",s4Offers==1&&
      fanHostScheduleEmergencyResume(sharedGeneration)==FanReadyDelivery::Idle&&resumes.empty());
  failClosePost=true;drain();
  check("failed Close completion post asks the public owner to continue the S4 wake",readyPosts==1&&
      sharedWakeOffers==s4Offers+1&&closes.size()==1&&resumes.empty());
  if(sharedWakeOffers>s4Offers)fanHostScheduleEmergencyResume(sharedGeneration);
  check("S4 resumes after a lost Close completion post without another wake or click",resumes.size()==1&&
      resumes.back()==closePostCycle&&eventDetails.back().value("source","")=="kernel-s4-wake"&&commitCalls==0);
  if(!resumes.empty())fanCloseWake(closePostCycle);
  check("a late duplicate Close completion cannot repeat the S4 recovery",resumes.size()<=1&&closes.size()==1);

  reset();active=true;dispatchFact(506,100);failClosePost=true;drain();
  check("failed Close post without an observed wake does not request another Ready delivery",
      readyPosts==1&&sharedWakeOffers==0&&resumes.empty()&&closes.size()==1);
}
}
#endif
int main(int argc,char** argv){
 const bool callersOnly=argc==2&&::std::strcmp(argv[1],"--callers-only")==0;
 const bool sharedPowerOnly=argc==2&&::std::strcmp(argv[1],"--shared-power-only")==0;
 if(argc>2||(argc==2&&!callersOnly&&!sharedPowerOnly)){
  ::std::cerr<<"ERROR unknown or extra arguments; expected no arguments or --callers-only; selected_suite=invalid checks=0 failures=1\n";
  return 2;
 }
#ifdef YMCC_FAN_PHYSICAL_WAKE_NATIVE_CALLERS
 if(sharedPowerOnly){native_fixture::testSharedPower();::std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite=shared-power-only scope=original-Fan-native-public-power-consumer-no-hardware\n";return failures?1:0;}
 if(callersOnly){
  native_fixture::testCallers();
  ::std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite=callers-only scope=selected-original-native-callers-no-hardware\n";
  return failures?1:0;
 }
#else
 if(callersOnly||sharedPowerOnly){
  ::std::cerr<<"ERROR callers-only requires production source extraction; selected_suite=callers-only checks=0 failures=1\n";
  return 2;
 }
#endif
 Observation o;
 check("initial awake generation unchanged",o.generation(1)==1 && o.ready(1));
 check("wake without sleep rejected",!o.recordWake(1,100,false));
 const auto g=o.confirmedSuspend(2);
 check("normal shared F4 binds Fan generation",g==2);
 check("suspend blocks native Fan writes",!o.ready(g) && o.awaitingWake(g));
 check("only one Close owner",o.beginClose(g) && !o.beginClose(g));
 check("duplicate confirmed F4 is stable",o.confirmedSuspend(2)==g);
 check("paired raw 506 does not mint duplicate generation",o.recordSleep(2,100)==g);
 check("equal/earlier timestamp wake rejected",!o.recordWake(2,100,false) && !o.recordWake(2,99,false));
 check("wrong shared generation wake rejected",!o.recordWake(1,101,false));
 check("filtered real wake is retained",o.recordWake(2,196,false));
 check("real wake cannot overtake unfinished Close",o.physical(g) && !o.ready(g) && !o.claimReady(g));
 check("serialized Close finishes",o.completeClose(g)==0);
 check("filtered wake is now admitted for Fan",o.ready(g) && o.claimReady(g));
 check("publication is once per cycle",!o.claimReady(g));
 // Real repeated sleep while shared policy is STILL Suspended/same generation.
 const auto g2=o.recordSleep(2,300);
 check("same shared generation real new sleep gets newer Fan generation",g2>g && o.generation(2)==g2);
 check("new physical sleep immediately fences prior write",!o.ready(g) && !o.ready(g2));
 check("new private Close is accepted instead of Host stale same-generation",o.beginClose(g2));
 check("old/duplicate raw facts cannot undo new sleep",!o.recordWake(2,196,false) && !o.recordSleep(2,100));
 check("old shared F5 cannot acknowledge new private cycle",o.sharedResume(2)==0);
 check("new paired wake retained",o.recordWake(2,400,false));
 check("new cycle waits for its own Close",!o.ready(g2) && !o.claimReady(g2));
 check("second serialized Close completed",o.completeClose(g2)==0 && o.ready(g2));
 check("new cycle can publish",o.claimReady(g2));
 // Overlap: first Close is still running when another sleep arrives.
 const auto g3=o.recordSleep(2,500);check("third Close starts",o.beginClose(g3));
 check("third wake retained",o.recordWake(2,600,false));
 const auto g4=o.recordSleep(2,700);
 check("newer Close coalesced while owner busy",g4>g3 && !o.beginClose(g4));
 check("newest wake retained",o.recordWake(2,800,false));
 check("older Close cannot release latest write gate",o.completeClose(g3)==g4 && !o.ready(g4));
 check("only latest Close completion releases new gate",o.completeClose(g4)==0 && o.ready(g4));
 check("old completion cannot drain twice",o.completeClose(g3)==0 && o.ready(g4));
 o.retryBoundary(2,900);
 check("accepted re-sleep boundary invalidates physical wake",!o.ready(g4) && !o.physical(g4));
 check("late old wake below retry cutoff rejected",!o.recordWake(2,850,false));
 check("active retry cannot admit new wake",!o.recordWake(2,1000,true) && !o.ready(g4));
 const auto g5=o.recordSleep(2,1100);o.beginClose(g5);o.completeClose(g5);
 check("new cycle after retry needs fresh wake",!o.ready(g5));
 check("new matched wake after retry accepted",o.recordWake(2,1200,false) && o.ready(g5));
 const auto g6=o.confirmedSuspend(3);
 check("next public generation preserves monotonic Fan generation",g6>g5 && o.generation(3)==g6);
 check("stale shared suspend cannot roll back Fan",o.confirmedSuspend(2)==0 && o.generation(3)==g6);
 o.beginClose(g6);o.abandonClose(g6);
 check("failed thread schedule does not manufacture Close proof",o.needsClose(g6) && !o.ready(g6));
 check("manual retry may schedule Close again",o.explicitResume(g6) && o.beginClose(g6));
 check("manual still waits for Close worker boundary",!o.ready(g6));
 check("manual completion can resume",o.completeClose(g6)==0 && o.ready(g6));
 Observation concurrent;const auto cg=concurrent.confirmedSuspend(2);std::atomic<int> owners{0};std::thread threads[8];
 for(auto& t:threads)t=std::thread([&]{if(concurrent.beginClose(cg))++owners;});for(auto& t:threads)t.join();
 check("concurrent callbacks still have one Close owner",owners==1);
 // Admission outcomes, not bool beginClose, distinguish invalidation from worker ownership.
 using Admission=decltype(o.admitClose(0));
 Observation admission;const auto ag=admission.confirmedSuspend(2);
 admission.recordSleep(2,50); // consume the raw506 belonging to confirmedSuspend before testing a new cycle.
 check("first Close admission starts worker",admission.admitClose(ag)==Admission::StartWorker);
 check("same Close owner admission is ignored",admission.admitClose(ag)==Admission::Ignored);
 check("zero/stale Close admission is ignored",admission.admitClose(0)==Admission::Ignored&&admission.admitClose(ag-1)==Admission::Ignored);
 const bool beforeCompletion=admission.needsClose(ag);
 check("Close completes after needsClose snapshot",beforeCompletion&&admission.completeClose(ag)==0);
 check("closed/TOCTOU admission is ignored",admission.admitClose(ag)==Admission::Ignored);
 const auto ao=admission.recordSleep(2,100);
 check("new private cycle starts another Close owner",ao>ag&&admission.admitClose(ao)==Admission::StartWorker);
 const auto aq=admission.recordSleep(2,200);
 check("newest generation queues once behind old Close",aq>ao&&admission.admitClose(aq)==Admission::Queued);
 check("duplicate queued generation is ignored",admission.admitClose(aq)==Admission::Ignored);
 check("metadata-only wake retained with pending Close",admission.recordWake(2,300,false)&&admission.physical(aq)&&!admission.ready(aq)&&!admission.claimReady(aq));
 check("older completion transfers owner without opening new gate",admission.completeClose(ao)==aq&&!admission.ready(aq));
 check("pending owner duplicate remains ignored",admission.admitClose(aq)==Admission::Ignored);
 check("only pending completion releases its wake gate",admission.completeClose(aq)==0&&admission.ready(aq));
 check("completed pending generation cannot invalidate again",admission.admitClose(aq)==Admission::Ignored);
 Observation metadata;const auto mg=metadata.recordSleep(2,100);
 check("inactive control is absent from OS metadata admission",metadata.recordWake(2,110,false)&&metadata.physical(mg)&&!metadata.awaitingWake(mg));
 check("metadata wake alone cannot manufacture Close proof",!metadata.ready(mg)&&!metadata.claimReady(mg));
 check("metadata-only path retains generation and monotonic checks",!metadata.recordWake(1,120,false)&&!metadata.recordWake(2,110,false)&&!metadata.recordSleep(2,100));
 metadata.retryBoundary(2,130);
 check("metadata-only path retains retry and cutoff rejection",!metadata.recordWake(2,120,false)&&!metadata.recordWake(2,140,true)&&!metadata.physical(mg)&&!metadata.ready(mg));
#ifdef YMCC_FAN_PHYSICAL_WAKE_NATIVE_CALLERS
 native_fixture::testCallers();
#else
 check("production native caller extraction must be supplied by original runner",false);
#endif
 std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite=all scope=production-Fan-cycle-metadata-and-extracted-native-callers-no-hardware\n";
 return failures?1:0;
}
