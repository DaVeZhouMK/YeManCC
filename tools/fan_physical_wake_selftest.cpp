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
int commitCalls=0;
using WPARAM=::std::uintptr_t;
using LPARAM=::std::intptr_t;
struct FanKernelPowerFact {int eventId;unsigned long long fileTime;};
constexpr unsigned ES_CONTINUOUS=1,ES_SYSTEM_REQUIRED=2,WM_FAN_PHYSICAL_WAKE_READY=3;
int g_hwnd=1,disarms=0,workerStarts=0,readyPosts=0,readyLogs=0,wakeLogs=0;
bool active=false,failThread=false,sleepRetry=false;
unsigned long long sharedGeneration=2;
::std::vector<::std::string> events;
::std::vector<unsigned long long> closes,resumes;
::std::deque<::std::function<void()>> workers;
static unsigned long long currentPowerGeneration(){return sharedGeneration;}
static unsigned long long currentFanPowerGeneration(){return g_fanPhysicalWake.generation(sharedGeneration);}
static bool fanActivityIntentPresent(){return active;}
static bool sgSleepRetryActive(){return sleepRetry;}
// Only external policy/clock state is fake; Fan physical/write/resume gates are
// extracted below. No input, power API, Steam or hardware side effects execute.
static bool hardwareWriteAllowed(){return hardwareGate&&g_powerLifecycle.load()==PowerLifecycle::Ready;}
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
static void ipc_emit(const char* event,const json&){events.emplace_back(event);}
static void appendFanLifecycleLog(const char* event,const json&){if(::std::string(event)=="fan-physical-wake-ready")++readyLogs;else if(::std::string(event)=="fan-physical-wake-observed")++wakeLogs;}
static void fanHostScheduleFanResume(unsigned long long g){resumes.push_back(g);}
static bool fanHostEmergencySuspend(const char*,unsigned long long g){closes.push_back(g);return true;}
static unsigned SetThreadExecutionState(unsigned state){return state;}
static bool PostMessageW(int,unsigned,WPARAM,int){++readyPosts;return true;}
static void traceLog(const char*,...){}
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
 check("native caller case leaves no waiting worker",workers.empty());
 workers.clear();g_fanPhysicalWake.~Observation();new (&g_fanPhysicalWake) Observation;
 g_powerLifecycle.store(PowerLifecycle::Ready);g_exitRequested.store(false);
 g_fanSleepCleanupInFlight.store(false);g_fanSleepCleanupGeneration.store(0);
 disarms=workerStarts=readyPosts=readyLogs=wakeLogs=0;active=failThread=sleepRetry=false;sharedGeneration=2;
 events.clear();closes.clear();resumes.clear();
 g_resumeReadyGeneration.store(sharedGeneration);g_inputReady.store(false);g_resumeReadyTick=0;fakeTick=1000;lastCommitGeneration=0;hardwareGate=true;commitCalls=0;
}
static void drain(){
 while(!workers.empty()){auto work=::std::move(workers.front());workers.pop_front();work();}
}
static void testCallers(){
 // Actual IPC/snapshot closure: wall-clock passage never creates a new OS fact.
 reset();dispatchFact(506,100);dispatchFact(507,110);
 const auto manualGeneration=currentFanPowerGeneration();
 fakeTick+=20ULL*60ULL*1000ULL;
 const auto aged=fanLifecycleSnapshot(json::object());
 check("real Fan snapshot after inactive wake and twenty minutes is resuming without writes",aged.at("phase")=="resuming"&&!aged.at("hardwareWritesAllowed").get<bool>()&&aged.at("generation").get<::std::uint64_t>()==manualGeneration&&g_fanPhysicalWake.physical(manualGeneration)&&workers.empty()&&events.empty());
 const auto manual=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(manualGeneration)}});
 const auto pending=fanLifecycleSnapshot(json::object());
 check("one actual manual IPC accepts current Fan generation without claiming finished Close",manual.at("ok").get<bool>()&&manual.at("reason")=="already_ready"&&pending.at("phase")=="resuming"&&!pending.at("hardwareWritesAllowed").get<bool>()&&workerStarts==1&&disarms==1&&events==::std::vector<::std::string>{"fan.suspending"}&&commitCalls==0);
 drain();
 const auto completed=fanLifecycleSnapshot(json::object());
 check("one manual Close completion makes actual Fan snapshot ready without input readiness",completed.at("phase")=="ready"&&completed.at("hardwareWritesAllowed").get<bool>()&&!completed.at("inputReady").get<bool>()&&closes==::std::vector<unsigned long long>{manualGeneration});
 const auto duplicateReady=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(manualGeneration)}});
 check("actual ready duplicate manual IPC preserves ready without extra invalidation",duplicateReady.at("ok").get<bool>()&&fanLifecycleSnapshot(json::object()).at("phase")=="ready"&&workerStarts==1&&disarms==1&&events.size()==1);
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
 reset();dispatchFact(506,100);dispatchFact(507,110);
 const auto suspendedFan=currentFanPowerGeneration();
 g_powerLifecycle.store(PowerLifecycle::Suspended);hardwareGate=false;g_resumeReadyGeneration.store(0);
 const auto promoted=fanManualWake(json{{"generation",static_cast<::std::uint64_t>(suspendedFan)}});
 check("actual manual missed-edge IPC commits shared generation instead of private Fan generation",suspendedFan!=sharedGeneration&&promoted.at("ok").get<bool>()&&commitCalls==1&&lastCommitGeneration==sharedGeneration&&g_resumeReadyGeneration.load()==sharedGeneration&&g_resumeReadyTick==fakeTick&&g_powerLifecycle.load()==PowerLifecycle::Ready&&!fanLifecycleSnapshot(json::object()).at("hardwareWritesAllowed").get<bool>());
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
 fanHostQueueSuspend("manual-fan-wake-close",g);
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
 reset();active=true;failThread=true;
 const auto failed=g_fanPhysicalWake.confirmedSuspend(sharedGeneration);
 fanHostQueueSuspend("schedule-failure",failed);
 check("real native schedule failure retains blocked Close obligation",workers.empty()&&g_fanPhysicalWake.needsClose(failed)&&!g_fanPhysicalWake.ready(failed)&&!g_fanSleepCleanupInFlight.load());
 failThread=false;fanHostQueueSuspend("schedule-retry",failed);drain();
 check("real native scheduling retry uses one successful worker",workerStarts==1&&closes==::std::vector<unsigned long long>{failed}&&!g_fanPhysicalWake.needsClose(failed));
 check("real native caller fixtures leave no worker pending",workers.empty());
}
}
#endif
int main(int argc,char** argv){
 const bool callersOnly=argc==2&&::std::strcmp(argv[1],"--callers-only")==0;
 if(argc>2||(argc==2&&!callersOnly)){
  ::std::cerr<<"ERROR unknown or extra arguments; expected no arguments or --callers-only; selected_suite=invalid checks=0 failures=1\n";
  return 2;
 }
#ifdef YMCC_FAN_PHYSICAL_WAKE_NATIVE_CALLERS
 if(callersOnly){
  native_fixture::testCallers();
  ::std::cout<<"checks="<<checks<<" failures="<<failures<<" selected_suite=callers-only scope=selected-original-native-callers-no-hardware\n";
  return failures?1:0;
 }
#else
 if(callersOnly){
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
