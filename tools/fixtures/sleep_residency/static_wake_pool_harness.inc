#if !defined(YMCC_STATIC_POOL_CASES_ONLY)
static std::condition_variable g_poolCv;
static auto& g_poolQ=jobs;
static constexpr size_t IPC_POOL_QUEUE_LIMIT=32;
static unsigned schedulePoolStarts=0;
static std::vector<std::thread> g_poolThreads;
static bool g_poolStarted=false,g_poolStopping=false;
static std::atomic<bool> g_poolCancel{false};
static int g_poolSize=3;
static unsigned fixturePoolDiagnosticsStarts=0,fixturePoolDiagnosticsCloses=0,fixturePoolJoins=0,fixturePoolJoinFailures=0;
static bool fixturePoolEmergencyStop=false,fixturePoolStartedAfterFault=false;
static size_t fixturePoolThreadsAfterFault=0;
#if defined(YMCC_STATIC_WAKE_REAL_DIAGNOSTIC)
static void reset();
#include "production_static_pool_try.inc"
static std::atomic<bool>* fixtureDiagRelease=nullptr;
static std::atomic<bool> fixtureDiagBlock{false};
static std::atomic<unsigned> fixtureDiagEntered{0},fixtureDiagReturned{0};
static std::mutex fixtureDiagWriteMx;
static std::vector<json> fixtureDiagWrites;
static void fixtureDiagnosticWrite(const char*,const char*,const json&,const std::string*);
static std::string nativeLifecycleTimestamp(){return "owned-producer-time";}
static void appendNativeLifecyclePrepared(const char* name,const json& detail,bool,const std::string* encoded){fixtureDiagnosticWrite("native",name,detail,encoded);}
static void mirrorLifecycleToDomains(const char* name,const json& detail){fixtureDiagnosticWrite("mirror",name,detail,nullptr);}
static void appendDomainLog(const char*,const char* name,const json& detail){fixtureDiagnosticWrite("domain",name,detail,nullptr);}
static void writeLog(const std::string& name,const std::string& text){fixtureDiagnosticWrite("app",name.c_str(),json{{"text",text}},nullptr);}
#include "input_host_diagnostic_runtime.h"
#else
static void inputHostCriticalDiagnosticsStartup() noexcept {++fixturePoolDiagnosticsStarts;}
static void inputHostCriticalDiagnosticsClose() noexcept {++fixturePoolDiagnosticsCloses;}
#endif
#if defined(YMCC_STATIC_WAKE_REAL_EXIT_JOIN)
#include "production_static_trace_state.inc"
#include "production_static_trace_locked.inc"
#include "production_static_trace_log.inc"
#include "production_static_trace_try.inc"
#include "production_static_exit_join.inc"
#else
static bool joinThreadBoundedForExit(std::thread& thread,DWORD timeoutMs,const char*){
 if(!thread.joinable())return true;
 if(WaitForSingleObject(thread.native_handle(),timeoutMs)!=WAIT_OBJECT_0){++fixturePoolJoinFailures;return false;}
 thread.join();++fixturePoolJoins;return true;
}
#endif
#include "production_static_pool_start.inc"
#include "production_static_schedule_pool.inc"
#include "production_static_pool_stop.inc"
#if defined(YMCC_STATIC_WAKE_REAL_EXIT_JOIN)
[[maybe_unused]] static int realPoolExitCase(const std::string&);
#include "static_wake_pool_exit_harness.inc"
#if defined(YMCC_STATIC_WAKE_REAL_CRT)
static int realPoolCrtCase(const std::string&);
#endif
#endif
[[maybe_unused]] static int realPoolCase(const std::string&);
#else
namespace static_wake_schedule_runtime {
template<class Predicate> static bool fixturePoolWait(Predicate predicate){
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
 while(!predicate()){
  if(std::chrono::steady_clock::now()>=deadline)return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 return true;
}
struct FixturePoolCleanup {
 ~FixturePoolCleanup(){
  if(!g_poolThreads.empty()){
   if(!g_poolStarted){fixturePoolEmergencyStop=true;g_poolStarted=true;}
   poolStop(2000);
  }
 }
};
static int realPoolCase(const std::string& selected){
 unsigned checks=0;
 const auto expect=[&](bool ok,const char* name){++checks;if(!ok)throw std::runtime_error(name);};
 try{
  if(!(selected=="schedule-pool-normal"||selected=="schedule-pool-reserve-failure"||selected=="schedule-pool-first-thread-failure"||selected=="schedule-pool-partial-thread-failure"||selected=="schedule-pool-partial-stop"||selected=="schedule-pool-one-worker"||selected=="schedule-pool-sixteen-workers"||selected=="schedule-pool-worker-exception"||selected=="schedule-pool-stop-drops-pending"||selected=="schedule-pool-cold-off-reenable"))throw std::runtime_error("unknown exact cold pool scenario");
  reset();schedulePoolFaultHits=0;fixturePoolThreadEntries=0;fixturePoolJoins=0;fixturePoolJoinFailures=0;fixturePoolEmergencyStop=false;
  g_poolSize=selected=="schedule-pool-one-worker"||selected=="schedule-pool-stop-drops-pending"?1:selected=="schedule-pool-sixteen-workers"?16:3;
  FixturePoolCleanup cleanup;
  sgStaticScanPreference(true,true);pump();expect(timerDue==61000,"original static sleep task retains first one-minute deadline");
  const bool reserve=selected=="schedule-pool-reserve-failure"||selected=="schedule-pool-cold-off-reenable";
  const bool first=selected=="schedule-pool-first-thread-failure";
  const bool partial=selected=="schedule-pool-partial-thread-failure"||selected=="schedule-pool-partial-stop";
  const bool fault=reserve||first||partial;
  if(first||partial)g_poolThreads.reserve(static_cast<size_t>(g_poolSize));
  if(fault)schedulePoolFaultAfter=partial?1:0;
  tick=61000;sgStaticScanTimer();schedulePoolFaultAfter=-1;
  if(fault){
   fixturePoolStartedAfterFault=g_poolStarted;fixturePoolThreadsAfterFault=g_poolThreads.size();
   expect(schedulePoolFaultHits==1,"real vector or thread allocation fails exactly once in original cold owner");
   expect(!g_sgStaticScanRunning&&jobs.empty()&&scans==0,"failed cold task retains no callback and releases scan running state");
   if(reserve||first)expect(!g_poolStarted&&g_poolThreads.empty(),"cold refusal must not publish pool ready with zero workers");
   if(partial)expect(!g_poolStarted&&g_poolThreads.size()==1,"partial cold admission cannot publish pool ready");
   if(selected=="schedule-pool-partial-stop"){
    poolStop(2000);
    expect(g_poolThreads.empty()&&!g_poolStarted&&fixturePoolJoins==1&&fixturePoolJoinFailures==0,"original stop must join retained partial workers before fully-started publication");
    const auto entries=fixturePoolThreadEntries.load();
    expect(!poolSubmit([]{})&&g_poolThreads.empty()&&fixturePoolThreadEntries==entries,"stopped partial pool cannot revive through later submission");
    std::cout<<json{{"scenario",selected},{"assertions",checks},{"actualPoolSubmit",true},{"realColdPool",true},{"faultHits",schedulePoolFaultHits.load()},{"faultBytes",schedulePoolLastFaultBytes.load()},{"startedAfterFault",fixturePoolStartedAfterFault},{"threadsAfterFault",fixturePoolThreadsAfterFault},{"workerEntries",fixturePoolThreadEntries.load()},{"joined",fixturePoolJoins},{"joinFailures",fixturePoolJoinFailures},{"emergencyCleanup",fixturePoolEmergencyStop},{"remainingThreads",g_poolThreads.size()},{"jobs",jobs.size()},{"realSleep",false},{"hardwareCalls",0}}.dump()<<'\n';return 0;
   }
   if(selected=="schedule-pool-cold-off-reenable"){sgStaticScanPreference(false);pump();expect(!g_sgStaticScanDelay.pending&&jobs.empty(),"OFF after real cold allocation refusal owns no pending or replayed callback");sgStaticScanPreference(true);pump();expect(timerDue==121000&&g_sgStaticScanDelay.pending,"explicit enable keeps one new delayed demand after cold refusal");}
   tick=70000;++testScheduleGeneration;sgStaticScanSchedule(false);pump();
   expect(timerDue==130000,"later original wake has one new absolute minute without retry replay");
   tick=130000;sgStaticScanTimer();
  }
  expect(fixturePoolWait([&]{return !g_sgStaticScanRunning.load(std::memory_order_acquire);}),"real cold worker completes original queued sleep task within finite fixture wait");
  expect(g_poolStarted&&g_poolThreads.size()==static_cast<size_t>(g_poolSize),"pool ready publishes only the complete requested retained worker set");
  expect(scans==1&&scanWrites==2&&jobs.empty(),"only later admitted sleep task executes once with original scan body");
  expect(fixturePoolWait([&]{return fixturePoolThreadEntries==static_cast<unsigned>(g_poolSize);}),"all original workers start without duplicates after partial retry");
  if(selected=="schedule-pool-worker-exception"){
   std::atomic<unsigned> calls{0};
   expect(poolSubmit([&]{calls.fetch_add(1);throw std::runtime_error("owned fixture callback");}),"original pool accepts throwing callback");
   expect(fixturePoolWait([&]{return calls==1;}),"original worker observes callback exception");
   expect(poolSubmit([&]{calls.fetch_add(1);}),"same original worker pool accepts later callback");
   expect(fixturePoolWait([&]{return calls==2;}),"callback exception cannot kill the real original pool loop");
  }
  if(selected=="schedule-pool-stop-drops-pending"){
   std::atomic<bool> entered{false},release{false};std::atomic<unsigned> lateCalls{0};
   expect(poolSubmit([&]{entered=true;while(!release.load(std::memory_order_acquire))std::this_thread::sleep_for(std::chrono::milliseconds(1));}),"owned finite blocker enters existing pool");
   expect(fixturePoolWait([&]{return entered.load();}),"owned blocker actually running before pending callback");
   expect(poolSubmit([&]{lateCalls.fetch_add(1);}),"second original callback is pending before stop");
   struct Stopper {std::atomic<bool>& release;std::thread worker;~Stopper(){release=true;if(worker.joinable())worker.join();}} stopper{release,std::thread([]{poolStop(2000);})};
   const bool cancelled=fixturePoolWait([&]{std::lock_guard<std::mutex> lock(g_poolMx);return g_poolStopping&&g_poolQ.empty();});release=true;stopper.worker.join();
   expect(cancelled&&lateCalls==0,"original stop drops pending callback without replay while joining current owner");
  }else poolStop(2000);
  expect(g_poolThreads.empty()&&!g_poolStarted&&jobs.empty()&&g_poolCancel&&g_poolStopping,"original pool stop returns full owned thread set and pending work");
  expect(fixturePoolJoins==static_cast<unsigned>(g_poolSize)&&fixturePoolJoinFailures==0,"every actual created worker is signaled and joined once");
  expect(!poolSubmit([]{}),"closed pool admission cannot revive stopped sleep tasks");
  expect(!fixturePoolEmergencyStop,"candidate cleanup completes through original public stop only");
  std::cout<<json{{"scenario",selected},{"assertions",checks},{"actualPoolSubmit",true},{"realColdPool",true},{"faultHits",schedulePoolFaultHits.load()},{"faultBytes",schedulePoolLastFaultBytes.load()},{"startedAfterFault",fixturePoolStartedAfterFault},{"threadsAfterFault",fixturePoolThreadsAfterFault},{"requestedWorkers",g_poolSize},{"workerEntries",fixturePoolThreadEntries.load()},{"joined",fixturePoolJoins},{"joinFailures",fixturePoolJoinFailures},{"emergencyCleanup",fixturePoolEmergencyStop},{"remainingThreads",g_poolThreads.size()},{"jobs",jobs.size()},{"scans",scans},{"realSleep",false},{"hardwareCalls",0}}.dump()<<'\n';return 0;
 }catch(const std::exception& e){schedulePoolFaultAfter=-1;std::cout<<json{{"scenario",selected},{"assertions",checks},{"failed",e.what()},{"faultHits",schedulePoolFaultHits.load()},{"faultBytes",schedulePoolLastFaultBytes.load()},{"startedAfterFault",fixturePoolStartedAfterFault},{"threadsAfterFault",fixturePoolThreadsAfterFault},{"workerEntries",fixturePoolThreadEntries.load()},{"joined",fixturePoolJoins},{"joinFailures",fixturePoolJoinFailures},{"emergencyCleanup",fixturePoolEmergencyStop},{"remainingThreads",g_poolThreads.size()},{"jobs",jobs.size()},{"realSleep",false},{"hardwareCalls",0}}.dump()<<'\n';return 1;}
}
}
#endif
