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
#include "../native/fan_response_contract.h"
#include <algorithm>
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
#ifdef YMCC_FAN_SHARED_INPUT_ADMISSION
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../native/input_host_shared_power.h"
#include "../native/input_frame_submission.h"
// This fixture keeps its existing inert execution-state values and timer ABI.
#undef ES_CONTINUOUS
#undef ES_SYSTEM_REQUIRED
#undef CALLBACK
#endif
namespace ymcc::sleeprequest {enum class Mode {None};}
namespace native_fixture {
using Admission=decltype(::std::declval<Observation&>().admitClose(0));
using json=::nlohmann::json;
enum class PowerLifecycle : ::std::uint8_t { Ready, Suspending, Suspended, Resuming };
Observation g_fanPhysicalWake;
::std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
::std::atomic<bool> g_exitRequested{false},g_fanSleepCleanupInFlight{false};
::std::atomic<unsigned long long> g_fanSleepCleanupGeneration{0};
::std::atomic<::std::uint64_t> g_resumeReadyGeneration{2};
::std::atomic<bool> g_inputReady{false};
::std::uint64_t g_resumeReadyTick=0,fakeTick=1000,lastCommitGeneration=0;
bool hardwareGate=true;
bool sharedPowerKnown=true,sharedPowerPending=false,sharedPowerAlwaysMoves=false,sharedDiagnosticPublication=false;
::std::uint64_t sharedSuspendSerial=3,sharedNotificationSequence=4;
unsigned sharedPowerReads=0, sharedPowerPendingFromRead=0;
::std::uint64_t sharedPowerEntryOnSecondRead=0;
int commitCalls=0;
bool denyReadyCommit=false;
unsigned committedReadyPublications=0;
static struct {
 unsigned attempted=0;
 bool fanKnown=false;
 unsigned long long fanGeneration=0;
 ::ymcc::sleeppower::SharedPowerV1 fanRecovery{};
} fixtureFanProjection;
using WPARAM=::std::uintptr_t;
using LPARAM=::std::intptr_t;
constexpr unsigned ES_CONTINUOUS=1,ES_SYSTEM_REQUIRED=2,WM_FAN_PHYSICAL_WAKE_READY=3;
int g_hwnd=1,disarms=0,workerStarts=0,readyPosts=0,readyLogs=0,wakeLogs=0,sharedWakeOffers=0;
bool active=false,failThread=false,sleepRetry=false,throwSuspend=false;
bool failSuspendNotification=false;
bool failCloseTrace=false;
unsigned closeTraceFailures=0,writeCancels=0;
bool dropFanWakeEvents=false,failFanWakeTimer=false;
unsigned fanWakeDrops=0;
using HWND=int;
using UINT=unsigned;
using UINT_PTR=::std::uintptr_t;
using DWORD=unsigned long;
using LRESULT=long;
constexpr UINT WM_SG_S0_INTENT=13,WM_SG_S0_WAKE=14,WM_SG_S4_WAKE=15,WM_FAN_KERNEL_POWER_FACT=16;
static bool rejectPublicWakePost=false,clearRetryDuringPolicy=false;
static bool fixtureAcceptUserS0=false;
static constexpr unsigned long long kFanResumeObserveWindowMs=15000;
static unsigned publicWakePosts=0,publicWakeDispatches=0,publicWakeSignals=0;
#define CALLBACK
using FanFixtureTimer=void (*)(HWND,UINT,UINT_PTR,DWORD);
static FanFixtureTimer fanWakeTimer=nullptr;
static UINT_PTR fanWakeTimerId=0;
static ::std::uint64_t fanWakeTimerDue=0,fanWakeIntentRevision=1;
[[maybe_unused]] static UINT_PTR SetTimer(HWND,UINT_PTR id,UINT delay,FanFixtureTimer callback){
 if(failFanWakeTimer)return 0;
 fanWakeTimer=callback;fanWakeTimerId=id;fanWakeTimerDue=fakeTick+delay;return id;
}
[[maybe_unused]] static bool KillTimer(HWND,UINT_PTR id){
 if(id!=fanWakeTimerId)return false;
 fanWakeTimer=nullptr;return true;
}
static void fireFanWakeTimer(){if(const auto callback=fanWakeTimer){fakeTick=fanWakeTimerDue;callback(g_hwnd,0,fanWakeTimerId,0);}}
const char* failWakeLog=nullptr;
unsigned failedWakeLogs=0;
using ULONGLONG=unsigned long long;
enum class SgWork { Suspend, WakeAutomatic, WakeSuspend, WakeHibernate };
using ::std::uint64_t;
struct SgWorkItem;
ULONGLONG g_sgAcceptedS4WakeFileTime=0;
::std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{0},g_sgLatestUserSleepEntryFileTime{0};
::std::atomic<ULONGLONG> g_sgLatestSleepEntryFileTime{0};
::std::atomic<ULONGLONG> g_sgClosedSleepIntentFileTime{0};
#ifndef PBT_APMSUSPEND
constexpr unsigned PBT_APMSUSPEND=4;
#endif
struct S4SleepBoundary {
 ULONGLONG fileTime100ns=0,sequence=0;
  unsigned type=PBT_APMSUSPEND;
 explicit operator bool() const {return sequence!=0;}
};
static struct {
 S4SleepBoundary boundary;
 S4SleepBoundary latestSleepBoundary() const {return boundary;}
 ULONGLONG observedSleepBoundarySequence() const {return boundary.sequence;}
 bool pendingSuspend() const {return sharedPowerPending;}
 bool signalOwnerWake() const {++publicWakeSignals;return true;}
} g_sleepPowerDispatch;
static ::std::atomic<uint64_t> g_sharedPowerSuspendSerial{0};
static struct { ULONGLONG dispatchFileTime=0; } g_sgInternalSleepRequest;
static struct { bool active() const {return false;} void shutdown(){} } g_radialOverlay;
::std::atomic<::ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget;
bool g_sgModernStandbyActive=false,renewS4=true;
unsigned s4CommonWake=0;
static S4SleepBoundary s4IngressDuringAbort;
static ULONGLONG sgNowFileTime(){return fakeTick;}
static void sgRecordFact(const char*,const json&){}
static bool sgRenewCancellationReady(ULONGLONG){return renewS4;}
static void radialInputIsolationInvalidate(){}
static void sgStopResumeClassificationWait(const char*){}
static void sgCompleteSharedWakeClassification(ULONGLONG){}
static void sgSleepTimeoutStop(const char*){}
static void sgClearInternalSleepRequest(const char*){g_sgInternalSleepRequest={};}
static void sgClearUnexpectedWake(const char*,bool){}
static void sgAbortSleepIntent(const char*,ULONGLONG){
 sleepRetry=false;
 if(s4IngressDuringAbort)g_sleepPowerDispatch.boundary=s4IngressDuringAbort;
}
static void handlePowerResumeNotification(SgWork,const char*,SgWorkItem* = nullptr){++s4CommonWake;}
struct FanCriticalReceipt { ULONGLONG sequence,fileTime100ns; };
template<class F> static void powerConsumer(const char*,F&& f){f();}
static void sleepRecoveryAutoArm(ULONGLONG){}
unsigned executionThread=0;
::std::vector<unsigned> executionStates{0};
bool executionRequestsOwned=true;
unsigned long long sharedGeneration=2;
::std::vector<::std::string> events;
::std::vector<json> eventDetails;
::std::vector<json> projectedPowerEvents;
::std::vector<unsigned long long> closes,resumes;
::std::deque<::std::function<void()>> workers;
static unsigned long long currentPowerGeneration(){return sharedGeneration;}
static unsigned long long currentFanPowerGeneration(){return g_fanPhysicalWake.generation(sharedGeneration);}
static bool fanActivityIntentPresent(){return active;}
[[maybe_unused]] static bool fanResumeActivityRevision(::std::uint64_t& revision){revision=fanWakeIntentRevision;return active;}
static bool fanManualActivityRevision(::std::uint64_t& revision){revision=fanWakeIntentRevision;return true;}
static bool sgSleepRetryActive(){return sleepRetry;}
static bool sharedSleepWakeClassified(ULONGLONG g){return g==sharedGeneration&&!sleepRetry;}
static void publishSharedPowerLifecycle(const char*){++sharedWakeOffers;}
// The provider owns its registry; these caller tests observe no real consumers.
static void revokeSharedPowerMirrors() noexcept {}
// Only external policy/clock state is fake; Fan physical/write/resume gates are
// extracted below. No input, power API, Steam or hardware side effects execute.
static bool hardwareWriteAllowed(){return hardwareGate&&g_powerLifecycle.load()==PowerLifecycle::Ready;}
static int __cdecl readSharedSystemPower(::ymcc::sleeppower::SharedPowerV1* output,::std::uint32_t size){
 ++sharedPowerReads;if(!sharedPowerKnown||g_exitRequested.load()||size<sizeof(*output))return 0;
 if(sharedPowerReads==2&&sharedPowerEntryOnSecondRead)g_sgLatestSleepEntryFileTime=sharedPowerEntryOnSecondRead;
 *output={};output->generation=sharedGeneration;output->suspendSerial=sharedSuspendSerial;
 output->observedNotificationSequence=sharedPowerAlwaysMoves?sharedPowerReads:sharedNotificationSequence;
 output->publicationRevision=sharedDiagnosticPublication?sharedPowerReads:9;
 output->phase=static_cast<::std::uint32_t>(g_powerLifecycle.load());
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
#include "fan_physical_wake_native_ready.inc"
static bool commitPowerResume(unsigned long long g,const char*,uint64_t ticket){
 ++commitCalls;lastCommitGeneration=g;
 if(denyReadyCommit||!g||g!=currentPowerGeneration())return false;
 if(g_powerLifecycle.load()==PowerLifecycle::Ready)return true;
 if(g_powerLifecycle.load()!=PowerLifecycle::Resuming||!readPowerResumeReady(g,ticket).ticket)return false;
 g_powerLifecycle.store(PowerLifecycle::Ready);hardwareGate=true;++committedReadyPublications;return true;
}
static unsigned long long g_resumeProbeGeneration=0,g_resumeProbeTicket=0;
static unsigned g_resumeProbeAttempts=0;
static bool g_resumeProbeInFlight=false,g_resumeProbeForcedReset=false;
static void armPowerResumeWatchdog(unsigned long long){}
static void nudgeWebViewAfterResume(bool){}
static void schedulePowerResumeProbe(unsigned long long,uint64_t,unsigned){}
static bool sgShouldKeepInputDuringStandby(){return true;}
static ::std::mutex g_fanLeaseKeepaliveMx;
static ::std::string g_fanLeaseId;
static ::std::atomic<bool> g_fanLeaseKeepaliveArmed{false};
static ::std::atomic<unsigned> g_fanKeepaliveRendererEpoch{0};
static unsigned g_fanKeepaliveRendererGraceUntil=0,g_fanKeepaliveRendererEpochSnapshot=0;
static int g_fanLeaseKeepaliveWake=0;
static struct { void invalidate() noexcept {++disarms;} } g_monitorFanDisplay;
static void fanWritePowerCancel() noexcept {++writeCancels;}
static void SetEvent(int) {}
static void ipc_emit(const char* event,const json& detail){
 if(dropFanWakeEvents&&(::std::strcmp(event,"power.lifecycleChanged")==0||::std::strcmp(event,"fan.resume-ready")==0)){
  ++fanWakeDrops;return; // Both posts return normally, but neither reaches the observer.
 }
 if(::std::strcmp(event,"power.lifecycleChanged")==0){projectedPowerEvents.push_back(detail);return;}
 if(failSuspendNotification&&::std::strcmp(event,"fan.suspending")==0)throw ::std::runtime_error("fixture-suspend-notification-failed");
 events.emplace_back(event);eventDetails.push_back(detail);
}
static void appendFanLifecycleLog(const char* event,const json&){
 if(failWakeLog&&::std::strcmp(event,failWakeLog)==0){++failedWakeLogs;throw ::std::bad_alloc{};}
 if(::std::strcmp(event,"fan-physical-wake-ready")==0)++readyLogs;
 else if(::std::strcmp(event,"fan-physical-wake-observed")==0)++wakeLogs;
}
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
static bool PostMessageW(int,UINT message,WPARAM,LPARAM){
 if(message==WM_FAN_PHYSICAL_WAKE_READY){++readyPosts;return true;}
 ++publicWakePosts;return !rejectPublicWakePost;
}
static DWORD GetWindowThreadProcessId(HWND,DWORD* process){*process=1;return 1;}
static DWORD GetCurrentProcessId(){return 1;}
static DWORD GetCurrentThreadId(){return 1;}
static bool SendNotifyMessageW(HWND,UINT,WPARAM,LPARAM){throw ::std::logic_error("unexpected second replay path");}
static LRESULT SendMessageW(HWND,UINT,WPARAM,LPARAM);
static LRESULT fixtureSharedS0Policy(WPARAM,LPARAM){
 ++publicWakeDispatches;
 if(clearRetryDuringPolicy)sleepRetry=false;
 return 0;
}
static bool failThisTrace(const char* format) noexcept {
 if(!failCloseTrace||::std::strncmp(format,"fan Host async sleep",20)!=0)return false;
 ++closeTraceFailures;return true;
}
static void traceLog(const char* format,...){if(failThisTrace(format))throw ::std::bad_alloc{};}
static void traceLogTry(const char* format,...) noexcept {(void)failThisTrace(format);}
// Resolve only the extracted functions' std::thread to this deterministic
// scheduler. detach retains the real worker body; drain explicitly opens its
// Close waiting gate. The original concurrent header test uses real std::thread.
namespace controlled_std {
using ::std::atomic;
using ::std::strcmp;
using ::std::move;
using ::std::string;
using ::std::to_string;
using ::std::unique_ptr;
using ::std::mutex;
using ::std::lock_guard;
using ::std::memory_order_acq_rel;
using ::std::memory_order_acquire;
using ::std::memory_order_release;
using ::std::uint64_t;
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
[[maybe_unused]] static unsigned long long g_fanKeepaliveLogLeaseSequence=0;
[[maybe_unused]] static bool fanLeaseKeepaliveMayRun(){return fanHostPowerMayWrite();}
struct FanHttpPowerAdmission {
 bool current() const{return fanHostPowerMayWrite();}
};
#include "fan_physical_wake_native_queue.inc"
#include "fan_physical_wake_native_shared_snapshot.inc"
#include "fan_physical_wake_native_publish.inc"
static long fanKernelPowerFact(LPARAM l){
#include "fan_physical_wake_native_fact.inc"
}
static LRESULT SendMessageW(HWND,UINT message,WPARAM w,LPARAM l){
 if(message==WM_FAN_KERNEL_POWER_FACT)return fanKernelPowerFact(l);
 return message==WM_SG_S0_WAKE?fanSharedS0(w,l):fanSharedS4(w,l);
}
// Legacy leaf scenarios recheck the same wake explicitly. The worker-to-
// coordinator transport is exercised separately by testCoordinatorCloseHandoff.
static int fanCloseWake(WPARAM g){
 if(fanHostPublishPhysicalWake(g)==FanReadyDelivery::Deferred)
  publishSharedPowerLifecycle("fixture-Fan-leaf-readmission");
 return 0;
}
static json fanLifecycleSnapshot(const json&){
#include "fan_physical_wake_native_lifecycle.inc"
}
static json fanManualWake(const json& a){
#include "fan_physical_wake_native_manual.inc"
}
static void dispatchFact(int eventId,unsigned long long at){
 if(eventId==506)fanKernelPowerFact(static_cast<LPARAM>(at));
 else fanHostObserveKernelExit(currentPowerGeneration(),at);
}
static void reset(){
 denyReadyCommit=false;committedReadyPublications=0;fixtureFanProjection={};
 sharedPowerKnown=true;sharedPowerPending=false;sharedPowerAlwaysMoves=false;sharedDiagnosticPublication=false;sharedPowerReads=sharedPowerPendingFromRead=0;
 sharedPowerEntryOnSecondRead=0;
 sharedSuspendSerial=3;sharedNotificationSequence=4;g_sharedPowerSuspendSerial=sharedSuspendSerial;
 check("native caller case leaves no waiting worker",workers.empty());
 workers.clear();g_fanPhysicalWake.~Observation();new (&g_fanPhysicalWake) Observation;
 g_powerLifecycle.store(PowerLifecycle::Ready);g_exitRequested.store(false);
 g_fanSleepCleanupInFlight.store(false);g_fanSleepCleanupGeneration.store(0);
  disarms=workerStarts=readyPosts=readyLogs=wakeLogs=sharedWakeOffers=0;active=failThread=sleepRetry=throwSuspend=failSuspendNotification=false;sharedGeneration=2;
  failWakeLog=nullptr;failedWakeLogs=0;
  failCloseTrace=false;closeTraceFailures=writeCancels=0;g_fanLeaseId.clear();g_fanLeaseKeepaliveArmed=false;
  executionThread=0;executionStates.assign(1,0);executionRequestsOwned=true;
 events.clear();eventDetails.clear();projectedPowerEvents.clear();closes.clear();resumes.clear();
 dropFanWakeEvents=failFanWakeTimer=false;fanWakeDrops=0;fanWakeTimer=nullptr;fanWakeTimerId=0;fanWakeTimerDue=0;++fanWakeIntentRevision;
 clearPowerResumeReady();g_inputReady.store(false);g_resumeReadyTick=0;fakeTick=1000;lastCommitGeneration=0;hardwareGate=true;commitCalls=0;
 g_sgAcceptedS4WakeFileTime=0;g_sleepPowerDispatch.boundary={};g_sgLastPowerButtonSleepIntentFileTime=0;
 g_sgLatestUserSleepEntryFileTime=0;g_sgLatestSleepEntryFileTime=0;g_sgInternalSleepRequest={};renewS4=true;s4CommonWake=0;
 s4IngressDuringAbort={};
 g_sgFailedS4Delivery={};g_sgOriginalHibernateIngress={};publicWakePosts=publicWakeDispatches=publicWakeSignals=0;
 rejectPublicWakePost=clearRetryDuringPolicy=false;
 fixtureAcceptUserS0=false;
 g_powerLifecycle=PowerLifecycle::Resuming;
 (void)recordPowerResumeReady(sharedGeneration,capturePowerResumeBoundary(),true);
 g_powerLifecycle=PowerLifecycle::Ready;
}
static void drain(){
  while(!workers.empty()){auto work=::std::move(workers.front());workers.pop_front();const auto caller=executionThread;executionThread=static_cast<unsigned>(executionStates.size());executionStates.push_back(0);try{work();}catch(...){executionThread=caller;throw;}executionThread=caller;}
}
static void testWakeDiagnostics(){
 for(const auto* stage:{"fan-physical-wake-observed","fan-physical-wake-ready"}){
  reset();active=true;dispatchFact(506,100);drain();
  const auto generation=currentFanPowerGeneration();
  failWakeLog=stage;
  bool escaped=false;
  try{dispatchFact(507,110);fanSharedReadyProjection();}catch(const ::std::bad_alloc&){escaped=true;}
  const auto readyCount=[](){return ::std::count(events.begin(),events.end(),"fan.resume-ready");};
  const auto recoveryDelivered=[&](){return resumes.size()==1&&resumes.front()==generation&&readyCount()==1;};
  check(stage,failedWakeLogs==1&&!escaped&&recoveryDelivered());
  const auto offers=resumes.size();const auto notices=readyCount();
  failWakeLog=nullptr;
  dispatchFact(507,110);
  fixtureFanProjection.attempted=0;fanSharedReadyProjection();
  check("wake diagnostics do not duplicate a completed physical wake",
      resumes.size()==offers&&readyCount()==notices&&closes.size()==1&&commitCalls==0);
 }
}
static void testColdStartupGrant(){
 const auto setup=[] {
  reset();sharedGeneration=4;hardwareGate=false;g_powerLifecycle=PowerLifecycle::Suspended;
  g_sgLatestSleepEntryFileTime=100;dispatchFact(506,100);
 };
 const auto candidate=[] {return sharedPowerLifecycleSnapshot().value("fanStartupWakeGeneration",0ULL);};
 const auto ready=[] {return sharedPowerLifecycleSnapshot().value("fanWakeGeneration",0ULL);};
 const auto claim=[](unsigned long long fan){return fanClaimStartupWake(json{{"generation",fan},
  {"sharedGeneration",sharedGeneration},{"suspendSerial",sharedSuspendSerial},
  {"notificationSequence",sharedNotificationSequence}}).value("ok",false);};
 const auto record=[](const char* stage){
  ::std::cout<<"COLD_STARTUP_FLOW "<<json{{"stage",stage},{"shared",sharedPowerLifecycleSnapshot()},
   {"native",fanLifecycleSnapshot({})},{"events",projectedPowerEvents},{"closes",closes.size()},
   {"resumes",resumes.size()},{"commits",commitCalls}}.dump()<<"\n";
 };
 setup();const auto fan=currentFanPowerGeneration();
 check("entry alone has no startup offer",!candidate()&&!claim(fan)&&workers.empty());
 (void)fanSharedS0(12,110);
 check("non-user S0 cannot authorize a cold startup",!candidate()&&!claim(fan)&&workers.empty());
 record("filtered");
 fixtureAcceptUserS0=true;(void)fanSharedS0(1,120);
 check("classified user wake offers only cold metadata",candidate()==fan&&!ready()&&!fanHostPowerMayWrite()&&workers.empty());
 record("offer");
 sharedPowerPending=true;
 check("pending input blocks a cold claim",!candidate()&&!claim(fan)&&workers.empty());
 sharedPowerPending=false;
 check("cold claim has no private Close or public write gate",claim(fan)&&!claim(fan)&&workers.empty()&&
  !candidate()&&!ready()&&!fanHostPowerMayWrite()&&resumes.empty());
 sharedPowerPending=true;fanSharedReadyProjection();
 check("accepted cold claim survives pending without starting Close",workers.empty()&&!ready());
 sharedPowerPending=false;fixtureFanProjection.attempted=0;fanSharedReadyProjection();
 check("clearing pending assigns one Close to the public consumer",workers.size()==1&&!ready());
 record("close-in-flight");
 fixtureFanProjection.attempted=0;
 fanSharedReadyProjection();
 check("even an extra public projection cannot bypass Close",!ready()&&commitCalls==0&&resumes.empty());
 drain();fixtureFanProjection.attempted=0;fanSharedReadyProjection();
 check("one completed close publishes Fan-only cold readiness",ready()==fan&&closes.size()==1&&resumes.size()==1&&
  projectedPowerEvents.size()==1&&commitCalls==0&&g_powerLifecycle==PowerLifecycle::Suspended);
 record("published");
 fixtureFanProjection.attempted=0;fanSharedReadyProjection();
 check("duplicate public projections do not repeat cold session resume",resumes.size()==1&&projectedPowerEvents.size()==1);
 fireFanWakeTimer();
 check("the original delivery timer can resend a cold grant without Host work",resumes.size()==1&&projectedPowerEvents.size()==2);
 ++fanWakeIntentRevision;fireFanWakeTimer();
 check("a new OFF receipt retires the cold grant and its redelivery",!ready()&&!fanLifecycleSnapshot({}).value("resumeReady",true)&&
  projectedPowerEvents.size()==2&&resumes.size()==1);

 setup();fixtureAcceptUserS0=true;(void)fanSharedS0(1,120);
 check("new-entry case starts with cold offer",claim(currentFanPowerGeneration()));
 fanSharedReadyProjection();
 fixtureFanProjection.attempted=0;
 g_sgLatestSleepEntryFileTime=140;drain();fanSharedReadyProjection();
 check("new raw entry revokes a cold claim during Close",!ready()&&resumes.empty()&&projectedPowerEvents.empty());
 setup();fixtureAcceptUserS0=true;(void)fanSharedS0(1,120);
 check("deadline case starts with cold offer",claim(currentFanPowerGeneration()));
 fanSharedReadyProjection();
 fixtureFanProjection.attempted=0;
 fakeTick+=kFanResumeObserveWindowMs;drain();fanSharedReadyProjection();
 check("deadline cannot be reset by Close or repeated 507",!ready()&&!candidate()&&resumes.empty()&&projectedPowerEvents.empty());

 reset();hardwareGate=false;g_powerLifecycle=PowerLifecycle::Suspended;
 const auto waiting=g_fanPhysicalWake.confirmedSuspend(sharedGeneration,90);
 fixtureAcceptUserS0=true;(void)fanSharedS0(1,120);
 check("reordered first 507 cannot offer startup before its 506",!candidate()&&workers.empty());
 g_sgLatestSleepEntryFileTime=100;dispatchFact(506,100);
 check("late 506 pairs the original user exit without restarting its deadline",candidate()==waiting&&sharedWakeOffers==1&&workers.empty());
 check("the reordered cold claim stays bound to the current suspend serial",claim(waiting));
 ++sharedSuspendSerial;fanSharedReadyProjection();
 check("a superseding public suspend serial retires the cold claim",!ready()&&workers.empty()&&resumes.empty());

 setup();fixtureAcceptUserS0=true;(void)fanSharedS0(1,120);
 const auto sealed=currentFanPowerGeneration();
 bool withdraw=false;
 const auto seal=g_fanPhysicalWake.observeStartupOff({1,1,1},130,sealed,100,fakeTick,withdraw);
 check("a sealed OFF blocks automatic cold startup without starting Close",
   seal==ymcc::fan_wake::Observation::OffStatus::Sealed&&!withdraw&&!candidate()&&!claim(sealed)&&workers.empty());
 const auto explicitRequest=fanManualWake(json{{"generation",sealed}});
 check("the actual manual IPC still accepts one click with current evidence after sealed OFF",
   explicitRequest.value("ok",false)&&explicitRequest.at("resumeDisposition")=="awaiting-power"&&workers.size()==1&&resumes.empty());
 drain();
 const auto explicitContinue=fanManualWake(json{{"generation",sealed},{"continueReadyOnly",true}});
 check("the manual click can finish after Close without granting public Ready",
   explicitContinue.value("ok",false)&&explicitContinue.value("resumeScheduled",false)&&
   closes.size()==1&&resumes.size()==1&&commitCalls==0&&g_powerLifecycle==PowerLifecycle::Suspended);
}

static void testSharedFanProjection(){
 testColdStartupGrant();
 reset();sharedGeneration=1;active=true;
 const auto initial=currentFanPowerGeneration();
 check("normal first control is permitted without a prior OS sleep",initial==1&&
       fanHostPowerMayWrite()&&fanHostPowerMayResume());
 // The common ingress has the first 506, but its Fan window callback has not run.
 g_sgLatestSleepEntryFileTime=100;
 const auto ingress=fanLifecycleSnapshot({});
 check("first raw 506 revokes cold Ready before its Fan notification",
       !fanHostPowerMayWrite()&&!fanHostPowerMayResume()&&ingress.at("phase")=="suspended"&&
       !ingress.value("hardwareWritesAllowed",true)&&!ingress.value("resumeReady",true));
 const auto initialClick=fanManualWake(json{{"generation",initial}});
 fanHostScheduleEmergencyResume(sharedGeneration);
 check("cold stale Ready can neither accept a click nor recover from a public projection",
       !initialClick.value("admitted",true)&&commitCalls==0&&workers.empty()&&resumes.empty());
 dispatchFact(507,110);drain();fanCloseWake(currentFanPowerGeneration());
 check("real 507 catches up the first 506 then resumes exactly once",
       closes.size()==1&&resumes==closes&&resumes.back()!=initial&&fanHostPowerMayWrite());
 ++sharedGeneration;
 check("an already handled 506 cannot fence a later public generation",
       fanHostPowerMayWrite()&&fanHostPowerMayResume());

 reset();dispatchFact(507,110);g_sgLatestSleepEntryFileTime=100;
 check("an older late first 506 below an unpaired exit is not a new sleep",
       fanHostPowerMayWrite()&&fanHostPowerMayResume()&&workers.empty()&&resumes.empty());
 dispatchFact(506,100);++sharedGeneration;
 check("retired 506 stays retired when the public generation changes",
       fanHostPowerMayWrite()&&fanHostPowerMayResume()&&workers.empty()&&resumes.empty());

 reset();active=true;
 dispatchFact(506,100);dispatchFact(507,110);
 const auto wake=currentFanPowerGeneration(),parent=currentPowerGeneration();
 const auto projected=[] {return sharedPowerLifecycleSnapshot().value("fanWakeGeneration",0ULL);};
 check("shared snapshot hides Fan wake while Close owns it",projected()==0);
 drain();
 const auto offers=sharedWakeOffers;
 fanCloseWake(wake);
 unsigned projections=0;
 for(const auto& event:projectedPowerEvents){
  projections+=event.value("fanWakeOnly",false)&&event.value("fanWakeGeneration",0ULL)==wake&&
               event.value("generation",0ULL)==parent;
 }
 check("closed Fan wake projects once without replaying shared Native consumers",
       projections==1&&projectedPowerEvents.size()==1&&sharedWakeOffers==offers&&wake!=parent&&projected()==wake);
 const auto posted=events.size();fanCloseWake(wake);
 check("duplicate Close completion does not publish another Fan wake",events.size()==posted&&projectedPowerEvents.size()==1);
 g_powerLifecycle.store(PowerLifecycle::Suspending);
 check("shared Suspending hides the old Fan wake even after its ingress receipt is consumed",projected()==0);
 g_powerLifecycle.store(PowerLifecycle::Suspended);
 check("a matched Fan wake can still project while public Suspended",projected()==wake);
 sharedPowerReads=0;sharedPowerPendingFromRead=2;
 check("shared ingress moving inside the Fan projection read cannot expose the old wake",projected()==0);
 sharedPowerPendingFromRead=0;g_powerLifecycle.store(PowerLifecycle::Ready);
 sharedPowerPending=true;
 check("pending shared ingress hides the previously projected Fan wake",projected()==0);
 sharedPowerPending=false;++sharedGeneration;
 check("wrong shared generation hides the old Fan wake",projected()==0);
 --sharedGeneration;dispatchFact(506,120);drain();
 check("new physical sleep retires the old projected Fan wake",projected()==0);

 reset();active=true;
 dispatchFact(506,100);drain();
 const auto privateSleep=currentFanPowerGeneration();
 fanHostScheduleEmergencyResume(sharedGeneration);
 check("Win5 unpaired 506 cannot use a pre-existing public Ready as a new wake",
       g_fanPhysicalWake.awaitingWake(privateSleep)&&!fanHostPowerMayWrite()&&projected()==0&&resumes.empty());

 reset();active=true;
 g_fanPhysicalWake.confirmedSuspend(sharedGeneration);
 const auto sharedSleep=currentFanPowerGeneration();
 fanHostQueueSuspend("fixture-public-suspend",sharedSleep);
 dispatchFact(506,100);drain();
 // An admitted public wake can outlive the optional Fan notification.
 const auto delivery=fanHostScheduleEmergencyResume(sharedGeneration);
 check("confirmed public wake without 507 publishes durable Fan readiness",
       delivery==FanReadyDelivery::Settled&&resumes==::std::vector<unsigned long long>{sharedSleep}&&
       !g_fanPhysicalWake.physical(sharedSleep)&&projected()==sharedSleep&&projectedPowerEvents.size()==1);
 g_sgLatestSleepEntryFileTime=100;
 check("the original 506 of a completed public wake does not cancel Fan readiness",
       projected()==sharedSleep&&fanHostPowerMayWrite()&&fanHostPowerMayResume());
 for(const bool resumeGate:{false,true}){
  g_sgLatestSleepEntryFileTime=100;sharedPowerReads=0;sharedPowerEntryOnSecondRead=120;
  check(resumeGate?"a 506 arriving during observation validation revokes resume":"a 506 arriving during observation validation revokes writes",
       !(resumeGate?fanHostPowerMayResume():fanHostPowerMayWrite())&&g_sgLatestSleepEntryFileTime.load()==120);
 }
 sharedPowerEntryOnSecondRead=0;
 g_sgLatestSleepEntryFileTime=120;
 const auto publicWakeRetired=fanLifecycleSnapshot({});
 check("a newer 506 retires an ordinary public wake without waiting for the Fan message",
       projected()==0&&!fanHostPowerMayWrite()&&!fanHostPowerMayResume()&&
       publicWakeRetired.at("phase")=="suspended"&&!publicWakeRetired.value("resumeReady",true));
 const auto oldPublicClick=fanManualWake(json{{"generation",sharedSleep}});
 check("a click from the superseded ordinary public wake cannot schedule another resume",
       !oldPublicClick.value("admitted",true)&&resumes.size()==1&&workers.empty()&&commitCalls==0);
 dispatchFact(506,120);drain();
 check("a subsequent private sleep retires the projected public Fan wake",
       currentFanPowerGeneration()>sharedSleep&&projected()==0&&!fanHostPowerMayWrite());
 dispatchFact(507,130);fanCloseWake(currentFanPowerGeneration());
 check("a real 507 can still recover the successor of the ordinary public wake",
       resumes.size()==2&&resumes.back()==currentFanPowerGeneration()&&resumes.back()!=sharedSleep&&
       projected()==resumes.back()&&fanHostPowerMayWrite()&&closes.size()==2);

 reset();active=true;
 g_sgLatestSleepEntryFileTime=100;
 dispatchFact(506,100);dispatchFact(507,110);drain();
 const auto pair=currentFanPowerGeneration();fanCloseWake(pair);
 check("paired recovery is eligible before a new original 506",
       projected()==pair&&fanHostPowerMayWrite()&&fanHostPowerMayResume());
 // The kernel ingress advances the high-water before its Fan window message.
 g_sgLatestSleepEntryFileTime=105;
 check("a delayed older 506 does not revoke a newer paired 507",
       projected()==pair&&fanHostPowerMayWrite()&&fanHostPowerMayResume());
 g_sgLatestSleepEntryFileTime=120;
 check("new original 506 revokes the previous paired 507 before its Fan message",
       projected()==0&&!fanHostPowerMayWrite()&&!fanHostPowerMayResume());
 const auto invalidated=fanLifecycleSnapshot({});
 check("a newer 506 also retires the old paired wake in the Fan status read",
       invalidated.value("known",false)&&invalidated.value("generation",0ULL)==pair&&
       invalidated.value("phase","")=="suspended"&&!invalidated.value("resumeReady",true)&&
       !invalidated.value("physicalWakeObserved",true)&&!invalidated.value("hardwareWritesAllowed",true));
 g_powerLifecycle.store(PowerLifecycle::Suspended);hardwareGate=false;g_resumeReadyGeneration.store(0);
 const auto staleClick=fanManualWake(json{{"generation",pair}});
 check("the superseded Fan click cannot submit global Ready or start new work",
       !staleClick.value("admitted",true)&&staleClick.at("reason")=="power_suspending"&&
       commitCalls==0&&g_powerLifecycle.load()==PowerLifecycle::Suspended&&
       closes.size()==1&&resumes.size()==1&&workers.empty());
 g_sgLatestSleepEntryFileTime=120;
 dispatchFact(507,130);drain();fanCloseWake(currentFanPowerGeneration());
 check("a valid new paired wake resumes once without requiring another click",
       closes.size()==2&&resumes.size()==2&&resumes.back()==currentFanPowerGeneration()&&
       resumes.back()!=pair&&commitCalls==0&&fanHostPowerMayWrite());
}
static void testFanSharedReadyOrdering(){
 for(const bool failCommit:{false,true}){
  reset();active=true;
  const auto g=g_fanPhysicalWake.confirmedSuspend(sharedGeneration,90);
  g_sgLatestSleepEntryFileTime=100;dispatchFact(506,100);
  drain();
  g_powerLifecycle.store(PowerLifecycle::Resuming);
  clearPowerResumeReady();
  const auto ticket=recordPowerResumeReady(sharedGeneration,capturePowerResumeBoundary(),true);
  g_powerResumeReadyRedelivery=ticket;
  denyReadyCommit=failCommit;
  fanSharedReadyDispatch(sharedGeneration,ticket);
  check(failCommit?"rejected public commit cannot register Fan recovery or notify ready":
                   "Ready window waits for the public Fan consumer to register recovery",
        ticket&&commitCalls==1&&resumes.empty()&&projectedPowerEvents.empty()&&
        g_fanPhysicalWake.awaitingWake(g,100));
  check("Ready window publishes only a successful public commit",committedReadyPublications==!failCommit);
  check("only a successful Ready commit retires the matching replay ticket",
        g_powerResumeReadyRedelivery.load()==(failCommit?ticket:0));
  if(!failCommit){
   fanSharedReadyProjection();
   fixtureFanProjection.attempted=0; // Retry the exact same delivery without a new authorization.
   fanSharedReadyProjection();
   check("committed public projection recovers Fan once without a second Close",
         resumes==closes&&closes.size()==1&&fanHostPowerMayWrite()&&
         sharedPowerLifecycleSnapshot().value("fanWakeGeneration",0ULL)==g);
  }else{
   fanSharedReadyProjection();
   check("non-admitted projection cannot resume an ordinary unpaired sleep",resumes.empty()&&
         !fanHostPowerMayWrite()&&g_fanPhysicalWake.awaitingWake(g,100));
  }
  g_powerResumeReadyRedelivery=0;
 }
}
static void testFanCanceledSleepDelivery(){
 for(bool wantFan:{true,false}){
  reset();active=wantFan;
  const auto g=g_fanPhysicalWake.confirmedSuspend(sharedGeneration,90);
  g_sgLatestSleepEntryFileTime=100;dispatchFact(506,100);
  const auto notices=[] {return ::std::count(events.begin(),events.end(),"fan.resume-ready");};
  fanCancelReadyTail(sharedGeneration);
  check("cancel publishes public power, not premature Fan readiness",sharedWakeOffers==1&&resumes.empty()&&notices()==0&&
        ::std::count(events.begin(),events.end(),"gameMedia.resume-ready")==1);
  fanSharedReadyProjection();
  check("cancel projection cannot report Fan ready while Close owns the session",resumes.empty()&&notices()==0);
  if(wantFan){
   drain();fixtureFanProjection.attempted=0;fanSharedReadyProjection();
   fixtureFanProjection.attempted=0;fanSharedReadyProjection();
   check("cancel projection and Close handoff recover active Fan exactly once",resumes==closes&&closes.size()==1&&notices()==1&&
         sharedPowerLifecycleSnapshot().value("fanWakeGeneration",0ULL)==g&&commitCalls==0);
  }else{
   check("cancel projection leaves an OFF Fan without a worker",workers.empty()&&closes.empty()&&resumes.empty()&&notices()==0);
  }
 }
}
static void testCoordinatorCloseHandoff(){
 for(bool scoped:{false,true})for(unsigned invalidation:{0u,1u,2u}){
  reset();active=true;
  g_powerLifecycle.store(PowerLifecycle::Suspended);
  g_sgLatestSleepEntryFileTime=100;
  if(!scoped)g_fanPhysicalWake.confirmedSuspend(sharedGeneration,90);
  dispatchFact(506,100);
  const auto g=currentFanPowerGeneration();
  if(scoped)dispatchFact(507,110);
  else g_powerLifecycle.store(PowerLifecycle::Ready);
  fanSharedReadyProjection();
  check("public wake cannot overtake the old Fan Close",workerStarts==1&&resumes.empty()&&closes.empty());
  const auto before=sharedWakeOffers;
  drain(); // A successful private Post is never consumed in this scenario.
  check("Close returns through the public mailbox without a private completion post",
       closes==::std::vector<ULONGLONG>{g}&&sharedWakeOffers==before+1&&readyPosts==0);
  if(invalidation==1)g_sgLatestSleepEntryFileTime=120;
  if(invalidation==2)active=false;
  if(sharedWakeOffers!=before){
   fixtureFanProjection.attempted=0;
   fanSharedReadyProjection();
   fixtureFanProjection.attempted=0;
   fanSharedReadyProjection();
  }
  check(invalidation==1?"new 506 retires the retained Close notification before public delivery":
        invalidation==2?"OFF before public delivery cannot restart Fan from a retained Close notification":
                   "public Close notification resumes Fan once without an additional wake or click",
       (invalidation?resumes.empty():resumes==closes)&&commitCalls==0);
 }
}
static void testPairedExitCatchesUpPublicEntry(){
 reset();active=true;
 g_sgLatestSleepEntryFileTime=100;
 dispatchFact(506,100);dispatchFact(507,110);drain();
 const auto first=currentFanPowerGeneration();fanCloseWake(first);
 check("existing public 506 and completed 507 produce the initial Fan recovery",
       resumes==::std::vector<ULONGLONG>{first}&&fanHostPowerMayWrite());

 // Ingress has the second 506, but first deliver only the second 507 to Fan.
 g_sgLatestSleepEntryFileTime=200;
 dispatchFact(507,210);
 const auto second=currentFanPowerGeneration();
 check("completed 507 joins the new public 506 before the Fan 506 message",
       second>first&&g_fanPhysicalWake.needsClose(second)&&!fanHostPowerMayWrite()&&resumes.size()==1);
 drain();fanCloseWake(second);
 dispatchFact(506,200);drain();fanCloseWake(second);
 check("the delayed Fan 506 cannot repeat Close or block the new recovery",
       second>first&&resumes==::std::vector<ULONGLONG>{first,second}&&
       closes==::std::vector<ULONGLONG>{first,second}&&fanHostPowerMayWrite()&&
       sharedPowerLifecycleSnapshot().value("fanWakeGeneration",0ULL)==second);

 reset();active=true;dispatchFact(506,100);drain();
 g_sgLatestSleepEntryFileTime=200;
 dispatchFact(507,190);
 check("a completed 507 older than the observed public 506 never resumes Fan",
       resumes.empty()&&!fanHostPowerMayWrite()&&!fanHostPowerMayResume());
}
static void testSharedFanProjectionRedelivery(){
 const auto loseBoth=[] {
  active=dropFanWakeEvents=true;
  dispatchFact(506,100);dispatchFact(507,110);drain();
  const auto wake=currentFanPowerGeneration();
  fanCloseWake(wake);
  check("both Fan wake messages can be lost after one real claim",fanWakeDrops==2&&projectedPowerEvents.empty()&&
      ::std::count(events.begin(),events.end(),"fan.resume-ready")==0&&closes.size()==1&&resumes.size()==1);
  dropFanWakeEvents=false;
  return wake;
 };
 reset();
 const auto shared=currentPowerGeneration(),wake=loseBoth();
 const auto offers=sharedWakeOffers;
 fireFanWakeTimer();
 check("no new hint or focus: coordinator redelivers the retained Fan-only fact",projectedPowerEvents.size()==1&&
     projectedPowerEvents.front().value("fanWakeOnly",false)&&projectedPowerEvents.front().value("generation",0ULL)==shared&&
     projectedPowerEvents.front().value("fanWakeGeneration",0ULL)==wake&&wake!=shared);
 fireFanWakeTimer();fireFanWakeTimer();fireFanWakeTimer();
 check("Fan redelivery is bounded and does not repeat Close, resume, or public consumers",
     projectedPowerEvents.size()==3&&!fanWakeTimer&&closes.size()==1&&resumes.size()==1&&sharedWakeOffers==offers&&commitCalls==0);
 reset();loseBoth();sharedPowerKnown=false;fireFanWakeTimer();sharedPowerKnown=true;fireFanWakeTimer();
 check("temporarily unavailable shared snapshot is not emitted and the next tick can deliver",projectedPowerEvents.size()==1);
 for(const auto* edge:{"off","off-on","new-sleep","new-shared-generation","exit","timer-refused"}){
  reset();
  if(::std::strcmp(edge,"timer-refused")==0)failFanWakeTimer=true;
  loseBoth();
  if(::std::strcmp(edge,"off")==0)active=false;
  if(::std::strcmp(edge,"off-on")==0)++fanWakeIntentRevision;
  if(::std::strcmp(edge,"new-sleep")==0){dispatchFact(506,120);drain();}
  if(::std::strcmp(edge,"new-shared-generation")==0)++sharedGeneration;
  if(::std::strcmp(edge,"exit")==0)g_exitRequested=true;
  fireFanWakeTimer();
  check(edge,projectedPowerEvents.empty()&&!fanWakeTimer&&resumes.size()==1);
 }
 reset();loseBoth();sharedPowerPending=true;fireFanWakeTimer();
 check("pending shared ingress cannot redeliver a prior Fan claim",projectedPowerEvents.empty());
 reset();const auto suspendedWake=loseBoth();g_powerLifecycle.store(PowerLifecycle::Suspending);fireFanWakeTimer();
 check("resend cannot publish an old Fan wake during shared Suspending",projectedPowerEvents.empty()&&resumes.size()==1);
 dispatchFact(506,120);drain();fireFanWakeTimer();
 check("the old resend cannot restart after the next confirmed sleep",projectedPowerEvents.empty()&&!fanWakeTimer&&resumes.size()==1);
 g_powerLifecycle.store(PowerLifecycle::Suspended);dispatchFact(507,130);fanCloseWake(currentFanPowerGeneration());
 check("a genuinely new Fan wake still reaches the background projection once",projectedPowerEvents.size()==1&&resumes.size()==2&&
      resumes.back()!=suspendedWake&&projectedPowerEvents.front().value("fanWakeGeneration",0ULL)==resumes.back());
}
static void testCoordinatorFanEntryDelivery(){
 for(bool enabled:{true,false}){
  reset();active=enabled;rejectPublicWakePost=true;
  const auto shared=currentPowerGeneration();
  g_sgLatestSleepEntryFileTime=100;
  sgPostKernelWake(g_hwnd,shared,100,WM_FAN_KERNEL_POWER_FACT,0);
  check("a refused Fan 506 post retains its original receipt without device work",publicWakeSignals==1&&workers.empty());
  g_sgLatestSleepEntryFileTime=120;
  sgPostKernelWake(g_hwnd,shared,120,WM_FAN_KERNEL_POWER_FACT,0);
  sgPostKernelWake(g_hwnd,shared,100,WM_FAN_KERNEL_POWER_FACT,0);
  sgRedeliverS4Wake(g_hwnd);sgRedeliverS4Wake(g_hwnd);
  const auto fan=currentFanPowerGeneration();
  check("common replay of a lost Fan 506 closes only the latest active cycle",
        g_fanPhysicalWake.awaitingWake(fan,120)&&workerStarts==static_cast<unsigned>(enabled)&&
        resumes.empty()&&currentPowerGeneration()==shared);
  drain();
  check("Fan entry replay alone never grants readiness or repeats Close",
        !fanHostPowerMayWrite()&&!fanHostPowerMayResume()&&closes.size()==static_cast<unsigned>(enabled)&&resumes.empty());
 }
}
static void testCoordinatorS0FanDelivery(){
 for(bool reversed:{false,true})for(unsigned retirement:{0u,1u,2u}){
  reset();active=true;g_powerLifecycle.store(PowerLifecycle::Suspended);
  const auto shared=currentPowerGeneration();
  if(reversed){
   const auto cycle=g_fanPhysicalWake.confirmedSuspend(shared,90);
   fanHostQueueSuspend("suspend-broadcast",cycle);drain();
   (void)fanSharedS0(12,110);
   g_sgLatestSleepEntryFileTime=100;
  }else{
   g_sgLatestSleepEntryFileTime=100;
   fanKernelPowerFact(100);drain();
  }
  const auto generation=currentFanPowerGeneration();
  const auto offers=sharedWakeOffers;
  if(reversed)fanKernelPowerFact(100);
  else (void)fanSharedS0(12,110);
  check(reversed?"late entry hands its matched wake to public delivery":"ordinary exit hands its matched wake to public delivery",
       g_fanPhysicalWake.physical(generation)&&sharedWakeOffers==offers+1&&resumes.empty()&&readyLogs==0);
  const auto read=fanLifecycleSnapshot({});
  check("undelivered pair is visible as a fact but not an automatic wake",
        read.value("physicalWakeObserved",false)&&!read.value("resumeReady",true));
  const auto compensation=fanManualWake(json{{"generation",generation},{"observedWakeOnly",true}});
  check("read-side compensation cannot schedule before coordinator delivery",
        compensation.value("reason",::std::string())=="wake_not_ready"&&resumes.empty());
  if(retirement==1)active=false;
  if(retirement==2)g_sgLatestSleepEntryFileTime=120;
  if(sharedWakeOffers>offers)fanSharedReadyProjection();
  fixtureFanProjection.attempted=0;fanSharedReadyProjection();
  check(retirement==1?"OFF before the public wake produces no Fan resume":
        retirement==2?"new raw 506 before the public wake produces no Fan resume":
                       "the public wake alone resumes the same session exactly once",
       closes==::std::vector<ULONGLONG>{generation}&&resumes.size()==(retirement?0u:1u)&&commitCalls==0);
  if(!retirement)check("coordinator delivery makes the same pair visible to a recreated renderer",
        fanLifecycleSnapshot({}).value("resumeReady",false));
 }
 reset();active=true;g_sgLatestSleepEntryFileTime=100;
 dispatchFact(506,100);drain();dispatchFact(507,110);
 const auto retainedFan=currentFanPowerGeneration(),retainedShared=currentPowerGeneration();
 fanSharedReadyProjection();
 const auto granted=fanLifecycleSnapshot({});
 g_fanPhysicalWake.retryBoundary(retainedShared,120);++sharedNotificationSequence;
 const auto revokedPublic=sharedPowerLifecycleSnapshot(),revoked=fanLifecycleSnapshot({});
 dispatchFact(507,110);dispatchFact(507,130);++sharedNotificationSequence;
 const auto waitingPublic=sharedPowerLifecycleSnapshot(),waiting=fanLifecycleSnapshot({});
 fixtureFanProjection.attempted=0;fanSharedReadyProjection();
 const auto restoredPublic=sharedPowerLifecycleSnapshot(),restored=fanLifecycleSnapshot({});
 check("same Fan generation reads Ready without resumeReady until coordinator republication",
       granted.value("resumeReady",false)&&!revoked.value("resumeReady",true)&&
       revokedPublic.value("hardwareWritesAllowed",false)&&revokedPublic.value("fanWakeGeneration",0ULL)==0&&
       waiting.at("phase")=="ready"&&waiting.value("hardwareWritesAllowed",false)&&
       waiting.value("physicalWakeObserved",false)&&!waiting.value("resumeReady",true)&&
       waitingPublic.value("fanWakeGeneration",0ULL)==0&&
       restored.value("resumeReady",false)&&restoredPublic.value("fanWakeGeneration",0ULL)==retainedFan&&
       granted.at("generation")==retainedFan&&revoked.at("generation")==retainedFan&&
       waiting.at("generation")==retainedFan&&restored.at("generation")==retainedFan&&
       currentPowerGeneration()==retainedShared&&closes.size()==1&&resumes.size()==2&&commitCalls==0);
 ::std::cout<<"FAN_SAME_GENERATION_READBACK "<<json{{"granted",granted},{"revoked",revoked},
     {"revokedPublic",revokedPublic},{"waitingForPublication",waiting},{"waitingPublic",waitingPublic},
     {"restored",restored},{"restoredPublic",restoredPublic}}.dump()<<'\n';
 reset();active=true;++sharedGeneration;
 dispatchFact(506,100);drain();dispatchFact(507,110);
 const auto paired=currentFanPowerGeneration();
 const auto stale=fanHostScheduleEmergencyResume(currentPowerGeneration()-1);
 check("old shared projection cannot deliver the current paired wake",
       stale==FanReadyDelivery::Idle&&resumes.empty()&&readyLogs==0&&g_fanPhysicalWake.readyUnpublished(paired));
 fanSharedReadyProjection();
 check("current shared projection delivers the retained pair once",resumes==::std::vector<ULONGLONG>{paired}&&readyLogs==1);

 reset();active=true;rejectPublicWakePost=true;
 const auto shared=currentPowerGeneration();
 g_sgLatestSleepEntryFileTime=100;
 // Model a missed 506 UI delivery; only the coordinator ingress fact survived.
 sgPostKernelWake(g_hwnd,shared,110,WM_SG_S0_WAKE,12);
 check("failed public 507 retains original receipt without pre-dispatch Fan work",
       publicWakePosts==1&&publicWakeSignals==1&&publicWakeDispatches==0&&workers.empty()&&resumes.empty());
 sgRedeliverS4Wake(g_hwnd);
 const auto generation=currentFanPowerGeneration();
 check("coordinator replay reaches Fan in unchanged public generation and waits for Close",
       publicWakeDispatches==1&&currentPowerGeneration()==shared&&generation!=shared&&
       g_fanPhysicalWake.physical(generation)&&!fanHostPowerMayWrite()&&resumes.empty());
 drain();fanCloseWake(generation);
 (void)fanSharedS0(12,110);sgRedeliverS4Wake(g_hwnd);
 check("replayed and duplicate normal 507 yield one Close and one background resume",
       closes==::std::vector<ULONGLONG>{generation}&&resumes==::std::vector<ULONGLONG>{generation}&&commitCalls==0);

 reset();active=true;rejectPublicWakePost=true;g_sgLatestSleepEntryFileTime=100;
 sgPostKernelWake(g_hwnd,currentPowerGeneration(),110,WM_SG_S0_WAKE,12);
 g_sgLatestSleepEntryFileTime=120;
 sgRedeliverS4Wake(g_hwnd);
 check("new original 506 retires the buffered prior exit without opening Fan",workers.empty()&&resumes.empty());
 (void)fanSharedS0(12,130);drain();fanCloseWake(currentFanPowerGeneration());
 check("new 506 pairs with its own coordinator 507 and recovers without focus",
       closes.size()==1&&resumes.size()==1&&commitCalls==0);

 reset();active=true;g_sgLatestSleepEntryFileTime=100;
 sgPostKernelWake(g_hwnd,currentPowerGeneration(),110,WM_SG_S0_WAKE,12,true);
 check("readback uses the same coordinator mailbox without a second direct post",
       publicWakePosts==0&&publicWakeSignals==1&&publicWakeDispatches==0&&workers.empty());
 sgRedeliverS4Wake(g_hwnd);drain();fanCloseWake(currentFanPowerGeneration());
 (void)fanSharedS0(12,110);
 check("readback departure and late matching live notice cannot duplicate Fan recovery",
       closes.size()==1&&resumes.size()==1&&commitCalls==0);

 reset();active=true;rejectPublicWakePost=true;g_sgLatestSleepEntryFileTime=100;
 sgPostKernelWake(g_hwnd,currentPowerGeneration(),110,WM_SG_S0_WAKE,1);
 g_sgLatestSleepEntryFileTime=120;
 sgPostKernelWake(g_hwnd,currentPowerGeneration(),130,WM_SG_S0_WAKE,12);
 sgRedeliverS4Wake(g_hwnd);drain();fanCloseWake(currentFanPowerGeneration());
 check("stale queued power-button exit cannot discard the next sleep's completed 507",
       publicWakeDispatches==1&&closes.size()==1&&resumes.size()==1);

 reset();active=true;dispatchFact(506,100);drain();
 (void)fanSharedS0(20,110);
 check("507 hibernate handoff cannot resume Fan",resumes.empty()&&!fanHostPowerMayWrite());
 g_sgAcceptedS4WakeFileTime=130;
 (void)fanSharedS0(12,120);
 check("already accepted 107 fences an older delivered 507",resumes.empty()&&!fanHostPowerMayWrite());

 reset();active=true;dispatchFact(506,100);drain();sleepRetry=true;
 (void)fanSharedS0(12,110);
 check("active public sleep retry suppresses Fan 507",resumes.empty()&&!fanHostPowerMayWrite());
 clearRetryDuringPolicy=true;(void)fanSharedS0(12,110);
 fanSharedReadyProjection();
 check("Fan consumes original receipt only after the public policy releases retry",resumes.size()==1);
}
static void testCoordinatorReadbackS4FanDelivery(){
 reset();active=true;dispatchFact(506,100);drain();
 const auto owningGeneration=currentPowerGeneration();
 g_sgOriginalHibernateIngress={owningGeneration,120,WM_SG_S4_WAKE,owningGeneration};
 (void)fanSharedS0(12,110);fanSharedReadyProjection();
 check("newer same-generation original 107 fences the direct 507 before public policy and Fan",
       publicWakeDispatches==0&&!fanHostPowerMayWrite()&&resumes.empty());
 reset();active=true;g_powerLifecycle=PowerLifecycle::Suspended;
 dispatchFact(506,100);drain();
 const auto nextOwner=currentPowerGeneration();
 g_sgOriginalHibernateIngress={nextOwner-1,120,WM_SG_S4_WAKE,nextOwner-1};
 (void)fanSharedS0(12,110);fanSharedReadyProjection();
 check("retired generation 107 cannot veto a current direct 507",
       publicWakeDispatches==1&&fanHostPowerMayWrite()&&resumes.size()==1);

 reset();active=true;sharedGeneration=3;
 const auto shared=currentPowerGeneration();
 const auto beforeS4=sharedPowerLifecycleSnapshot();
 g_sgLatestSleepEntryFileTime=100;
 g_sleepPowerDispatch.boundary={100,3};
 sgPostKernelWake(g_hwnd,shared,110,WM_SG_S0_WAKE,12,true);
 sgPostKernelWake(g_hwnd,shared,120,WM_SG_S4_WAKE,shared,true);
 sgRedeliverS4Wake(g_hwnd);
 const auto generation=currentFanPowerGeneration();
 using Wake=Observation::SharedWake;
 check("the recovered 107 supersedes the older 507 before the Fan can write",
       publicWakePosts==0&&publicWakeDispatches==0&&s4CommonWake==1&&g_sgAcceptedS4WakeFileTime==120&&
       g_fanPhysicalWake.sharedFinalWake(generation)==Wake::Hibernate&&!fanHostPowerMayWrite()&&resumes.empty());
 const auto heldShared=sharedPowerLifecycleSnapshot(),heldFan=fanLifecycleSnapshot({});
 // The common resume worker is an inert port here. Supply its current
 // receipt through the production recorder before the final Fan projection.
 g_powerLifecycle=PowerLifecycle::Resuming;
 const auto ticket=recordPowerResumeReady(shared,capturePowerResumeBoundary(120),true);
 g_powerLifecycle=PowerLifecycle::Ready;
 drain();(void)fanHostScheduleEmergencyResume(shared);
 sgRedeliverS4Wake(g_hwnd);(void)fanSharedS4(shared,120);(void)fanSharedS0(12,110);
 check("recovered hibernate schedules one background resume after Close with no independent admission",
       s4CommonWake==1&&closes==::std::vector<ULONGLONG>{generation}&&resumes==::std::vector<ULONGLONG>{generation}&&
       commitCalls==0);
 check("recovered hibernate emits exactly one Fan resume receipt with the original source",
       ::std::count(events.begin(),events.end(),"fan.resume-ready")==1&&
       eventDetails.back().value("source","")=="kernel-s4-wake");
 check("recovered hibernate exports the original shared Fan notification after Close",
       heldShared.value("fanWakeGeneration",0ULL)==0&&!heldFan.value("hardwareWritesAllowed",true)&&
       projectedPowerEvents.size()==1&&projectedPowerEvents.front().value("fanWakeGeneration",0ULL)==generation);
 if(projectedPowerEvents.size()==1){
  const auto readyShared=sharedPowerLifecycleSnapshot(),readyFan=fanLifecycleSnapshot({});
  check("current common hibernate receipt is readable by Fan",ticket&&readyFan.value("resumeReady",false));
  const auto delivered=projectedPowerEvents.front();
  g_sleepPowerDispatch.boundary={140,4};g_sgLatestSleepEntryFileTime=140;
  ++g_sharedPowerSuspendSerial;++sharedSuspendSerial;++sharedNotificationSequence;
  g_powerLifecycle=PowerLifecycle::Suspending;hardwareGate=false;sharedPowerPending=true;
  dispatchFact(506,140);drain();
  const auto nextShared=sharedPowerLifecycleSnapshot(),nextFan=fanLifecycleSnapshot({});
  check("newer Pending Suspend withdraws the exported hibernate authorization",
        nextShared.value("fanWakeGeneration",0ULL)==0&&nextShared.value("notificationPending",false)&&
        !nextFan.value("hardwareWritesAllowed",true));
  ::std::cout<<"S4_RETAINED_FAN_WIRE "<<json{{"source","kernel-s4-wake"},{"originalWakeFileTime100ns",120},
      {"prior",beforeS4},{"closeInFlight",heldShared},{"closeInFlightFan",heldFan},
      {"ready",readyShared},{"readyFan",readyFan},{"delivered",delivered},
      {"nextSuspend",nextShared},{"nextSuspendFan",nextFan}}.dump()<<'\n';
 }

 for(const bool hibernate:{false,true}){
  reset();active=true;g_sgLatestSleepEntryFileTime=100;g_sleepPowerDispatch.boundary={100,3};
  sgPostKernelWake(g_hwnd,currentPowerGeneration(),120,hibernate?WM_SG_S4_WAKE:WM_SG_S0_WAKE,
                   hibernate?currentPowerGeneration():12,true);
  active=false; // The retained receipt describes power, not a new user intent.
  sgRedeliverS4Wake(g_hwnd);drain();(void)fanHostScheduleEmergencyResume(currentPowerGeneration());
  check(hibernate?"OFF before recovered 107 does not start Fan":"OFF before recovered 507 does not start Fan",
        (hibernate?s4CommonWake:publicWakeDispatches)==1&&workerStarts==0&&resumes.empty()&&events.empty()&&commitCalls==0);
 }
}
static void testSharedPower(){
 testPairedExitCatchesUpPublicEntry();
 testCoordinatorS0FanDelivery();
 testCoordinatorReadbackS4FanDelivery();
 testSharedFanProjection();
 testSharedFanProjectionRedelivery();
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
#ifdef YMCC_FAN_SHARED_INPUT_ADMISSION
#include "fan_shared_input_admission_cases.h"
#endif
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
 // Public suspend owns the same cycle. Run the reversed pair on both sides
 // of Close return, with public delivery after the outstanding Close.
 for(bool closeReturned : {false,true}) {
  reset();active=true;g_powerLifecycle.store(PowerLifecycle::Suspended);
  const auto cycle=g_fanPhysicalWake.confirmedSuspend(sharedGeneration,90);
  fanHostQueueSuspend("suspend-broadcast",cycle);
  if(closeReturned)drain();
  dispatchFact(507,200);dispatchFact(506,80);
  check("public suspend rejects a 506 from before its boundary",g_fanPhysicalWake.awaitingWake(cycle)&&resumes.empty());
  if(closeReturned)sharedPowerPending=true;
  dispatchFact(506,100);
  check("reverse pair keeps its public generation and waits for Close or admission",currentFanPowerGeneration()==cycle&&
      g_fanPhysicalWake.physical(cycle)&&!fanHostPowerMayWrite()&&resumes.empty()&&workerStarts==1);
   const auto waitingClick=fanManualWake(json{{"generation",cycle}});
   check("first click retains matched Fan wake while Close or public admission is pending",
       waitingClick.value("admitted",false)&&!waitingClick.value("resumeScheduled",true)&&
       waitingClick.value("resumeDisposition","")=="awaiting-power"&&commitCalls==0&&
       g_powerLifecycle.load()==PowerLifecycle::Suspended&&resumes.empty()&&workerStarts==1);
  if(closeReturned)sharedPowerPending=false;
  else drain();
  check("reverse pair requests public delivery after Close or pending admission",sharedWakeOffers==1&&closes.size()==1);
  if(sharedWakeOffers)fanHostPublishPhysicalWake(cycle,true);
  dispatchFact(506,100);dispatchFact(507,200);fanCloseWake(cycle);
  check("reverse pair resumes once after public readmission with no shared commit",resumes==::std::vector<unsigned long long>{cycle}&&
      closes==resumes&&readyLogs==1&&fanHostPowerMayWrite()&&commitCalls==0&&g_powerLifecycle.load()==PowerLifecycle::Suspended);
  const auto click=fanManualWake(json{{"generation",cycle},{"continueReadyOnly",true}});
  check("first manual click can join the reordered wake",click.value("resumeScheduled",false)&&commitCalls==0&&closes.size()==1);
   const auto firstClick=fanManualWake(json{{"generation",cycle}});
   check("ordinary first click reuses active Fan wake without a global Ready commit",
       firstClick.value("admitted",false)&&firstClick.value("resumeScheduled",false)&&commitCalls==0&&
       g_powerLifecycle.load()==PowerLifecycle::Suspended&&closes==::std::vector<ULONGLONG>{cycle});
 }
 reset();active=true;g_powerLifecycle.store(PowerLifecycle::Suspended);
 const auto retryCycle=g_fanPhysicalWake.confirmedSuspend(sharedGeneration,90);
 fanHostQueueSuspend("suspend-broadcast",retryCycle);drain();dispatchFact(507,200);
 g_fanPhysicalWake.retryBoundary(sharedGeneration,210);dispatchFact(506,100);
 check("retry cutoff prevents pairing a retired early 507",g_fanPhysicalWake.awaitingWake(retryCycle)&&
      !g_fanPhysicalWake.physical(retryCycle)&&!fanHostPowerMayWrite()&&resumes.empty());
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
 sharedPowerKnown=false;fanSharedReadyProjection();
 check("Close completion with unknown admission asks the same public owner and keeps the ready marker",sharedWakeOffers==1&&resumes.empty()&&readyLogs==0&&g_fanPhysicalWake.readyUnpublished(deferredClose));
 sharedPowerKnown=true;fixtureFanProjection.attempted=0;fanSharedReadyProjection();
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
  active=true;hardwareGate=false;clearPowerResumeReady();
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
  check("direct Fan write and resume gates cannot cross shared Suspending with a previous pair",
      !fanHostPowerMayWrite()&&!fanHostPowerMayResume());
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
 for(const bool withReceipt:{false,true}){
  reset();dispatchFact(506,100);hardwareGate=false;clearPowerResumeReady();
  g_powerLifecycle.store(withReceipt?PowerLifecycle::Resuming:PowerLifecycle::Suspended);
  const auto pendingGeneration=currentFanPowerGeneration();
  const auto receipt=withReceipt?recordPowerResumeReady(sharedGeneration,capturePowerResumeBoundary(110),true):0;
  const auto click=fanManualWake(json{{"generation",pendingGeneration}});
  check(withReceipt?"explicit click leaves the real same-generation 107 receipt in charge":"explicit click without evidence retains intent without any Close, Host work or shared Ready",
      click.value("ok",false)&&click.value("reason","")=="awaiting_wake_evidence"&&
      click.value("resumeDisposition","")=="awaiting-power"&&!click.value("resumeScheduled",true)&&
      !hardwareGate&&commitCalls==0&&workers.empty()&&resumes.empty()&&
      g_powerLifecycle.load()==(withReceipt?PowerLifecycle::Resuming:PowerLifecycle::Suspended)&&
      readPowerResumeReady(sharedGeneration).ticket==receipt&&
      (!withReceipt||readPowerResumeReady(sharedGeneration,receipt).boundary.originalWakeFileTime==110));
  drain();
 }
 reset();dispatchFact(506,100);
 const auto suspendedFan=currentFanPowerGeneration();
 g_powerLifecycle.store(PowerLifecycle::Suspended);hardwareGate=false;clearPowerResumeReady();
 const auto waiting=fanManualWake(json{{"generation",suspendedFan}});
 check("first click owns no device work while the current cycle has no wake evidence",
      waiting.value("reason","")=="awaiting_wake_evidence"&&closes.empty()&&resumes.empty()&&commitCalls==0);
 // Deliver a genuine later system wake and its public Ready. This is the same
 // continuation of the click after a renderer read of the new wake witness.
 fanSharedS4(sharedGeneration,110);
 g_powerLifecycle.store(PowerLifecycle::Resuming);
 const auto systemTicket=recordPowerResumeReady(sharedGeneration,capturePowerResumeBoundary(110),true);
 const bool systemCommit=commitPowerResume(sharedGeneration,"native.resume-ready",systemTicket);
 (void)fanHostScheduleEmergencyResume(sharedGeneration);
 const auto accepted=fanManualWake(json{{"generation",suspendedFan}});
 check("one click can start its previously unstarted Close after genuine shared Ready",
      systemCommit&&accepted.value("ok",false)&&accepted.value("resumeDisposition","")=="awaiting-power"&&
      workerStarts==1&&resumes.empty()&&commitCalls==1&&readPowerResumeReady(sharedGeneration).ticket==systemTicket);
 drain();
 const auto continued=fanManualWake(json{{"generation",suspendedFan},{"continueReadyOnly",true}});
 check("genuine Ready continuation reuses the first click after one Close",
      continued.value("resumeScheduled",false)&&workerStarts==1&&closes==::std::vector<unsigned long long>{suspendedFan}&&
      resumes==::std::vector<unsigned long long>{suspendedFan}&&commitCalls==1);
 // A cold first click has no confirmed control record to lend to the power gate.
 for(const bool wakeBeforeClick:{false,true}){
  reset();g_powerLifecycle.store(PowerLifecycle::Suspended);hardwareGate=false;
  dispatchFact(506,100);if(wakeBeforeClick)dispatchFact(507,110);
  const auto cold=currentFanPowerGeneration();
  const json click{{"generation",cold}}, continuation{{"generation",cold},{"continueReadyOnly",true}};
  check("cold sleep/wake alone performs no Fan work",!active&&!workerStarts&&closes.empty()&&resumes.empty());
  if(!wakeBeforeClick){
   check("cold click without wake evidence retains the intent only",
       fanManualWake(click).at("reason")=="awaiting_wake_evidence"&&workers.empty()&&resumes.empty());
   dispatchFact(507,110);
  }
  const auto witness=fanLifecycleSnapshot({});
  check("cold paired fact is visible without write permission or an active record",
      witness.at("phase")=="resuming"&&witness.value("physicalWakeObserved",false)&&!witness.value("resumeReady",true)&&
      !witness.value("hardwareWritesAllowed",true)&&workers.empty()&&!active);
  sharedPowerPending=true;
  check("cold pending notification cannot start Close for a click",
      fanManualWake(click).at("reason")=="awaiting_wake_evidence"&&workers.empty());
  sharedPowerPending=false;
  const auto acceptedCold=fanManualWake(click);
  const auto duplicateCold=fanManualWake(click);
  const auto early=fanManualWake(continuation);
  check("cold same click enters one serialized Close and cannot write before it returns",
      acceptedCold.at("reason")=="already_ready"&&acceptedCold.at("resumeDisposition")=="awaiting-power"&&
      duplicateCold.at("resumeDisposition")=="awaiting-power"&&!early.value("admitted",true)&&
      workerStarts==1&&resumes.empty()&&closes.empty()&&!fanHostPowerMayWrite());
  drain();fanHostPublishPhysicalWake(cold);
  check("cold Close return cannot autonomously recover an inactive Fan",
      !active&&resumes.empty()&&readyLogs==0&&commitCalls==0&&g_powerLifecycle.load()==PowerLifecycle::Suspended);
  const auto ready=fanLifecycleSnapshot({});
  const auto completedCold=fanManualWake(continuation);
  check("cold first click continues after one Close without shared Ready or a fake activity record",
      ready.at("phase")=="ready"&&ready.value("hardwareWritesAllowed",false)&&!active&&commitCalls==0&&
      completedCold.value("resumeScheduled",false)&&closes==::std::vector<unsigned long long>{cold}&&
      resumes==::std::vector<unsigned long long>{cold}&&g_powerLifecycle.load()==PowerLifecycle::Suspended);
  g_sgLatestSleepEntryFileTime=200;
  check("new raw 506 revokes the cold continuation before the Fan window message",
      !fanHostPowerMayWrite()&&!fanManualWake(continuation).value("admitted",true)&&resumes.size()==1);
 }
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
 check("real native worker completes exactly its one admitted Close",closes==::std::vector<unsigned long long>{g}&&sharedWakeOffers==1&&readyPosts==0&&!g_fanSleepCleanupInFlight.load()&&g_fanPhysicalWake.ready(g));
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
 check("real native one worker drains old and newest Close in order",closes==::std::vector<unsigned long long>{old,next}&&workerStarts==1&&sharedWakeOffers==1&&readyPosts==0&&g_fanPhysicalWake.ready(next));
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
 fanSharedReadyProjection();
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
 fanSharedReadyProjection();
 const auto retryWake=currentFanPowerGeneration();
 g_fanPhysicalWake.retryBoundary(sharedGeneration,250);sleepRetry=true;
 dispatchFact(507,225);dispatchFact(507,275);
 check("public retry boundary revokes the old wake and suppresses work through newer 507",
       !g_fanPhysicalWake.physical(retryWake)&&!g_fanPhysicalWake.ready(retryWake)&&closes.size()==1&&resumes.size()==1&&wakeLogs==1&&readyLogs==1);
 sleepRetry=false;dispatchFact(507,225);dispatchFact(507,300);dispatchFact(507,300);
 check("fresh 507 after retry requests delivery without resuming early",sharedWakeOffers==2&&resumes.size()==1);
 // The public snapshot and Fan generation are unchanged; only the wake fact is new.
 fixtureFanProjection.attempted=0;fanSharedReadyProjection();
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
 g_fanLeaseId="lease-before-sleep";g_fanLeaseKeepaliveArmed=true;
 failWakeLog="native-lease-keepalive-stopped";bool disarmLogEscaped=false;
 try{dispatchFact(506,100);}catch(...){disarmLogEscaped=true;}
 const auto afterDisarmLog=currentFanPowerGeneration();
 check("disarm journal failure still cancels the old lease and installs its Close worker",
       !disarmLogEscaped&&failedWakeLogs==1&&writeCancels==1&&g_fanLeaseId.empty()&&
       !g_fanLeaseKeepaliveArmed&&workers.size()==1&&!g_fanPhysicalWake.ready(afterDisarmLog));
 dispatchFact(507,200);drain();fanHostPublishPhysicalWake(afterDisarmLog);fanHostPublishPhysicalWake(afterDisarmLog);
 check("wake after disarm journal failure receives the original Close and resumes once",
       closes==::std::vector<unsigned long long>{afterDisarmLog}&&resumes==closes&&workers.empty()&&
       !g_fanSleepCleanupInFlight.load()&&commitCalls==0);
 reset();active=true;g_powerLifecycle.store(PowerLifecycle::Suspended);failCloseTrace=true;
 dispatchFact(506,100);const auto afterTrace=currentFanPowerGeneration();
 dispatchFact(507,200);bool closeTraceEscaped=false;
 try{drain();}catch(...){closeTraceEscaped=true;}
 fanHostPublishPhysicalWake(afterTrace);
 check("worker trace failure cannot strand the Close owner or block the wake",
       !closeTraceEscaped&&closeTraceFailures==1&&closes==::std::vector<unsigned long long>{afterTrace}&&
       resumes==closes&&workers.empty()&&!g_fanSleepCleanupInFlight.load()&&commitCalls==0);
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
  drain();
  check("completed Close asks the public owner to continue the S4 wake",readyPosts==0&&
      sharedWakeOffers==s4Offers+1&&closes.size()==1&&resumes.empty());
  if(sharedWakeOffers>s4Offers)fanHostScheduleEmergencyResume(sharedGeneration);
  check("S4 resumes after public Close delivery without another wake or click",resumes.size()==1&&
      resumes.back()==closePostCycle&&eventDetails.back().value("source","")=="kernel-s4-wake"&&commitCalls==0);
  if(!resumes.empty())fanCloseWake(closePostCycle);
  check("a late duplicate Close completion cannot repeat the S4 recovery",resumes.size()<=1&&closes.size()==1);

  reset();active=true;dispatchFact(506,100);const auto criticalCycle=currentFanPowerGeneration();
  g_sgLatestSleepEntryFileTime=100;g_sleepPowerDispatch.boundary={100,3};
  fanHostScheduleEmergencyResume(sharedGeneration);
  fanCriticalResume({2,200});fanCriticalResume({4,90});fanCriticalResume({4,0});
  check("ordinary Ready and stale critical receipts leave the Fan parked",
      resumes.empty()&&sharedWakeOffers==0&&g_fanPhysicalWake.awaitingWake(criticalCycle));
  g_powerLifecycle=PowerLifecycle::Resuming;
  fanCriticalResume({4,200});fanCriticalResume({4,200});
  check("critical broadcast without 507 or 107 retains one wake through the public gate",
      sharedWakeOffers==1&&fanHostScheduleEmergencyResume(sharedGeneration)==FanReadyDelivery::Deferred&&
      !fanHostPowerMayResume()&&resumes.empty());
  g_powerLifecycle=PowerLifecycle::Ready;
  check("public critical recovery waits for the original Fan Close",
      fanHostScheduleEmergencyResume(sharedGeneration)==FanReadyDelivery::Idle&&closes.empty()&&resumes.empty());
  drain();
  check("completed Close retains the critical wake for public redelivery",
      closes==::std::vector<ULONGLONG>{criticalCycle}&&resumes.empty()&&sharedWakeOffers==2);
  fanHostScheduleEmergencyResume(sharedGeneration);fanCloseWake(criticalCycle);
  check("accepted critical receipt recovers Fan once without any 507 or 107",
      resumes==::std::vector<ULONGLONG>{criticalCycle}&&fanHostPowerMayWrite()&&
      eventDetails.back().value("source","")=="public-critical-resume"&&commitCalls==0);
  g_sgLatestSleepEntryFileTime=300;
  check("newer 506 ingress revokes critical Fan writes before the queued Fan handler",
      !fanHostPowerMayWrite()&&!fanHostPowerMayResume()&&
      sharedPowerLifecycleSnapshot().value("fanWakeGeneration",0ULL)==0);
  dispatchFact(506,300);drain();
  check("later 506 creates its own Close and cannot reuse a critical receipt",
      !fanHostPowerMayWrite()&&closes.size()==2&&resumes.size()==1);

  reset();active=true;dispatchFact(506,100);drain();const auto retryCritical=currentFanPowerGeneration();
  g_fanPhysicalWake.retryBoundary(sharedGeneration,250);sleepRetry=true;
  fanCriticalResume({4,200});fanHostScheduleEmergencyResume(sharedGeneration);
  check("older critical broadcast during retry stays zero action",
      sharedWakeOffers==0&&resumes.empty()&&!fanHostPowerMayWrite());
  fanCriticalResume({5,300});fanHostScheduleEmergencyResume(sharedGeneration);
  check("fresh critical receipt remains parked while public classification is held",
      sharedWakeOffers==1&&resumes.empty()&&!fanHostPowerMayWrite());
  sleepRetry=false;fanHostScheduleEmergencyResume(sharedGeneration);
  check("fresh critical receipt after retry resumes without another Close",
      resumes==::std::vector<ULONGLONG>{retryCritical}&&closes.size()==1);

  reset();dispatchFact(506,100);fanCriticalResume({4,200});fanHostScheduleEmergencyResume(sharedGeneration);
  check("critical broadcast with no Fan intent does not start Host or Close",
      workers.empty()&&closes.empty()&&resumes.empty()&&events.empty());

  reset();active=true;dispatchFact(506,100);const auto retiredClose=currentFanPowerGeneration();
  fanCriticalResume({4,200});
  ++sharedGeneration; // Public resume without PBT suspend allocates a generation.
  const auto currentCloseSuccessor=currentFanPowerGeneration();
  check("new public Ready waits while an older Fan Close owns cleanup",
      currentCloseSuccessor>retiredClose&&fanHostScheduleEmergencyResume(sharedGeneration)==FanReadyDelivery::Idle&&resumes.empty());
  const auto offersBeforeReturn=sharedWakeOffers;
  drain();
  check("older Close return redelivers the new public Fan generation",
      closes==::std::vector<ULONGLONG>{retiredClose}&&sharedWakeOffers==offersBeforeReturn+1&&resumes.empty());
  if(sharedWakeOffers>offersBeforeReturn)fanHostScheduleEmergencyResume(sharedGeneration);
  check("new public generation resumes after the older Close without another wake",
      resumes==::std::vector<ULONGLONG>{currentCloseSuccessor}&&fanHostPowerMayWrite());

  for(const bool hibernate:{false,true}){
   reset();active=true;dispatchFact(506,100);drain();
   const auto first=currentFanPowerGeneration();
   g_sgLatestSleepEntryFileTime=100;
   fanCriticalResume({4,200});fanHostScheduleEmergencyResume(sharedGeneration);
   check("first public wake restores Fan before the next sleep",resumes==::std::vector<ULONGLONG>{first});
   // The Event Log callback sees 506 before Fan's window message is delivered.
   g_sgLatestSleepEntryFileTime=300;
   check("original 506 revokes old Fan Ready before its message",!fanHostPowerMayWrite());
   if(hibernate)fanSharedS4(sharedGeneration,400);
   else fanCriticalResume({5,400});
   const auto second=currentFanPowerGeneration();
   fanHostScheduleEmergencyResume(sharedGeneration);
   check("the next final wake picks up the unprocessed 506 and waits for its own Close",
       second>first&&closes.size()==1&&resumes.size()==1&&!fanHostPowerMayWrite());
   drain();fanHostScheduleEmergencyResume(sharedGeneration);
   check("the next final wake restores Fan when that Close returns",
       closes==::std::vector<ULONGLONG>{first,second}&&resumes==::std::vector<ULONGLONG>{first,second});
   const auto publications=sharedWakeOffers;
   dispatchFact(506,300);drain();
   check("the delayed 506 message cannot repeat Close or recovery",
       closes.size()==2&&resumes.size()==2&&sharedWakeOffers==publications&&fanHostPowerMayWrite());
  }

  reset();active=true;dispatchFact(506,100);drain();
  g_sleepPowerDispatch.boundary={100,3};renewS4=false;
  fanSharedS4(sharedGeneration,200);
  renewS4=true;fanSharedS4(sharedGeneration,200);fanSharedS4(sharedGeneration,200);
  check("same S4 can join Fan recovery once after a denied renewal",
      s4CommonWake==1&&sharedWakeOffers==1&&g_sgAcceptedS4WakeFileTime==200);

  reset();active=true;dispatchFact(506,100);drain();
  g_sleepPowerDispatch.boundary={100,3};sleepRetry=true;s4IngressDuringAbort={300,4};
  fanSharedS4(sharedGeneration,200);
  check("new suspend during retry cancellation vetoes old S4 Fan and common recovery",
      s4CommonWake==0&&sharedWakeOffers==0&&resumes.empty());

  reset();active=true;dispatchFact(506,100);drain();
  check("Close without an observed wake does not request another Ready delivery",
      readyPosts==0&&sharedWakeOffers==0&&resumes.empty()&&closes.size()==1);
}
}
#endif
int main(int argc,char** argv){
 const bool callersOnly=argc==2&&::std::strcmp(argv[1],"--callers-only")==0;
 const bool sharedPowerOnly=argc==2&&::std::strcmp(argv[1],"--shared-power-only")==0;
 const bool wakeDiagnosticsOnly=argc==2&&::std::strcmp(argv[1],"--wake-diagnostics-only")==0;
 const bool s4WireOnly=argc==2&&::std::strcmp(argv[1],"--s4-wire-only")==0;
 const bool joint=argc==2&&::std::strcmp(argv[1],"--shared-input-admission")==0;
 const bool jointNegative=argc==2&&::std::strcmp(argv[1],"--shared-input-admission-control")==0;
 if(argc>2||(argc==2&&!callersOnly&&!sharedPowerOnly&&!wakeDiagnosticsOnly&&!s4WireOnly&&!joint&&!jointNegative)){
  ::std::cerr<<"ERROR unknown or extra arguments; selected_suite=invalid checks=0 failures=1\n";
  return 2;
 }
#ifdef YMCC_FAN_SHARED_INPUT_ADMISSION
 if(joint||jointNegative){
  native_fixture::testSharedInputAdmission(jointNegative);
  ::std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite="<<(joint?"shared-input-admission":"shared-input-admission-control")<<"\n";
  return failures?1:0;
 }
#else
 if(joint||jointNegative)return 2;
#endif
#ifdef YMCC_FAN_PHYSICAL_WAKE_NATIVE_CALLERS
 if(s4WireOnly){native_fixture::testCoordinatorReadbackS4FanDelivery();::std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite=s4-wire-only scope=coordinator-hibernate-to-Fan-no-hardware\n";return failures?1:0;}
 if(sharedPowerOnly){native_fixture::testSharedPower();::std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite=shared-power-only scope=original-Fan-native-public-power-consumer-no-hardware\n";return failures?1:0;}
 if(wakeDiagnosticsOnly){native_fixture::testWakeDiagnostics();::std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite=wake-diagnostics-only scope=production-Fan-physical-wake-diagnostics-no-hardware\n";return failures?1:0;}
 if(callersOnly){
  native_fixture::testCallers();
  native_fixture::testWakeDiagnostics();
  ::std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite=callers-only scope=selected-original-native-callers-no-hardware\n";
  return failures?1:0;
 }
#else
 if(callersOnly||sharedPowerOnly||wakeDiagnosticsOnly||s4WireOnly){
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
 native_fixture::testSharedFanProjection();
 native_fixture::testFanSharedReadyOrdering();
 native_fixture::testFanCanceledSleepDelivery();
 native_fixture::testCoordinatorCloseHandoff();
 native_fixture::testPairedExitCatchesUpPublicEntry();
 native_fixture::testCoordinatorFanEntryDelivery();
 native_fixture::testCoordinatorS0FanDelivery();
 native_fixture::testCoordinatorReadbackS4FanDelivery();
 native_fixture::testSharedFanProjectionRedelivery();
 native_fixture::testWakeDiagnostics();
#else
 check("production native caller extraction must be supplied by original runner",false);
#endif
 std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite=all scope=production-Fan-cycle-metadata-and-extracted-native-callers-no-hardware\n";
 return failures?1:0;
}
