#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include "../native/decky_sidebar_recovery.h"
#include "../native/decky_sidebar_manual_repair.h"
#include "../native/decky_sidebar_diagnostics.h"
// Explicit optional parameter only: compile with /DYMCC_DECKY_HEALTH_REPORT_MEMORY_FIXTURE.
#ifdef YMCC_DECKY_HEALTH_REPORT_MEMORY_FIXTURE
#include "fixtures/decky_binding_constructor_probe.inc"
#include <cstdio>
#include <cstring>
#include <exception>
namespace healthReportMemoryProbe {
inline std::atomic<unsigned> target{0},hits{0};inline std::atomic<bool> remaining{true},reasonBuffer{false};
// Arm after the original health callback has built its return/exception.
// On unwind skip its original reason buffer; the next 32B JSON object is the optional log.
// The old/new Runtime pair proves that only the new optional-log catch isolates this fault.
struct Scope {
 unsigned phase;explicit Scope(unsigned currentPhase) noexcept:phase(currentPhase){}
 ~Scope() noexcept {
  const unsigned edge=phase==1?1u:phase>=2?2u:0u;if(!edge||target.load()!=edge||!remaining.exchange(false))return;
  std::set_terminate([]{
   const char* kind="unknown";try{if(const auto error=std::current_exception())std::rethrow_exception(error);}catch(const std::bad_alloc&){kind="bad-alloc";}catch(...){}
   std::fprintf(stderr,"HEALTH_REPORT_MEMORY_TERMINAL kind=%s edge=%u hits=%u failures=%u bytes=%zu",kind,target.load(),hits.load(),ymcc::deckybinding::fixture::constructionprobe::failures.load(),ymcc::deckybinding::fixture::constructionprobe::failedBytes.load());
   std::fflush(stderr);std::_Exit(std::strcmp(kind,"bad-alloc")==0?207:208);
  });
  ++hits;ymcc::deckybinding::fixture::constructionprobe::failures=0;ymcc::deckybinding::fixture::constructionprobe::failedBytes=0;
  ymcc::deckybinding::fixture::constructionprobe::skipAllocations=edge==1&&!reasonBuffer.load()?1u:0u;
  ymcc::deckybinding::fixture::constructionprobe::failNext=true;
 }
};
template<class Check> inline void verifyEdges(const nlohmann::json& edges,const Check& check){
 check(hits==1&&ymcc::deckybinding::fixture::constructionprobe::failures==1&&ymcc::deckybinding::fixture::constructionprobe::failedBytes==32,"exact-one-original-health-report-allocation-fails");
 if(reasonBuffer.load()){
  check(edges.size()==2,"reason-buffer-fault-preserves-both-original-health-edges");
  check(edges.at(0).value("bootstrapStage",std::string{})=="health-read-unavailable"&&edges.at(1).value("bootstrapStage",std::string{})=="health-read-returned","reason-buffer-fault-retains-health-read-return-observation");return;
 }
 check(edges.size()==1,"only-selected-optional-health-log-may-be-absent");
 const char* expected=target==1?"health-read-returned":"health-read-unavailable";
 check(edges.at(0).value("bootstrapStage",std::string{})==expected&&!edges.at(0).value("attempted",true),"remaining-health-edge-is-original-observation-without-authority");
}
} // Explicit fixture port only, absent from the product.
#endif
#include "../native/decky_sidebar_runtime.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <vector>
#include <cassert>
#include <iostream>
#ifdef YMCC_DECKY_BUDGET_SOURCE
#include "fixtures/decky_recovery_budget_cases.h"
#endif
static int healthObservationFaultCase(){
 using namespace ymcc::deckysidebar;using Json=nlohmann::json;
 Runtime runtime;std::mutex mutex;std::condition_variable changed;
 std::atomic<unsigned> phase{0},reads{0},completedRead{0},admissionReads{0},preflights{0},uiSteps{0};
 Json records=Json::array();
 Config config;config.executable=L"NEVER-LAUNCH.exe";config.home=L"NEVER-CREATE-DIRECTORY";
 config.preflight=[&]{++preflights;return std::string("fixture-no-product-launch");};
 // Pending observation keeps the original read window; a terminal refusal intentionally fast-skips recovery rungs.
 config.launchAdmission=[&]{++admissionReads;return LaunchAdmission{false,"fixture-observation-only",true};};
 config.connectionHealth=[&]()->ConnectionHealth{
#ifdef YMCC_DECKY_HEALTH_REPORT_MEMORY_FIXTURE
  healthReportMemoryProbe::Scope optionalLogMemory(phase.load());
#endif
  ++reads;if(phase.load()==1)throw std::runtime_error("untrusted-health-exception-private-text");
  ConnectionHealth health;health.required=true;health.connected=phase.load()==3;health.identity="fixture-steam:account:ready1";health.reason=health.connected?"fixture-connected":"fixture-disconnected";return health;
 };
 config.connectionStep=[&](auto){++uiSteps;return Json{{"ok",false},{"mutated",false}};};
 config.connectionRecovery=[&]{{std::lock_guard lock(mutex);completedRead=reads.load();}changed.notify_all();};
 config.bindingDiagnostic=[&](const Json& row){records.push_back(ymcc::deckydiag::record("bootstrap-watch",row));};
 runtime.initialize(std::move(config));runtime.observeSteam(1,true);
 const auto check=[](bool ok,const char* name){if(!ok)throw std::runtime_error(name);};
 const auto awaitRead=[&](unsigned expected){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::seconds(2),[&]{return completedRead.load()>=expected;});};
 check(runtime.setEnabled(true),"real observation Runtime enabled");check(awaitRead(1),"real first observation completed on Runtime worker");
 phase=1;
 for(unsigned i=0;i<16;++i){const auto expected=reads.load()+1;check(runtime.requestTransportRecovery(),"explicit observer wake accepted");check(awaitRead(expected),"same-source throwing observation completed on real worker");}
 phase=2;{const auto expected=reads.load()+1;check(runtime.requestTransportRecovery()&&awaitRead(expected),"same-source missing health returned");}
 phase=3;{const auto expected=reads.load()+1;check(runtime.requestTransportRecovery()&&awaitRead(expected),"same-source connected health returned");}
 runtime.shutdown();const auto stopped=runtime.snapshot();const auto readsAfterStop=reads.load();
 check(!runtime.requestTransportRecovery(),"OFF rejects late health wake");
 Json reconnectEpisodes=Json::array(),stoppedEpisodes=Json::array(),recoveredEpisodes=Json::array(),healthEdges=Json::array();
 for(const auto& row:records){
  if(row.value("scope",std::string{})=="connection-health")healthEdges.push_back(row);
  if(row.value("scope",std::string{})!="global-connection")continue;
  const auto state=row.value("recoveryPhase",std::string{});
  if(row.value("stage",std::string{})=="reconnect")reconnectEpisodes.push_back(row.at("recoveryEpisode"));
  if(state=="stopped")stoppedEpisodes.push_back(row.at("recoveryEpisode"));
  if(state=="connected")recoveredEpisodes.push_back(row.at("recoveryEpisode"));
 }
 const Json evidence{{"kind","actual-production-runtime-health-observation"},{"readFaults",16},{"reconnectEpisodes",reconnectEpisodes},{"stoppedEpisodes",stoppedEpisodes},{"recoveredEpisodes",recoveredEpisodes},{"healthEdges",healthEdges},{"reads",reads.load()},{"admissionReads",admissionReads.load()},{"preflights",preflights.load()},{"uiSteps",uiSteps.load()},{"pidAfterStop",stopped.pid},{"enabledAfterStop",stopped.enabled},{"lateWakeReads",reads.load()-readsAfterStop},{"realSteamActions",0},{"legacyMacroDefined",false}};
 std::cout<<"HEALTH_OBSERVATION_EVIDENCE "<<evidence.dump()<<'\n';
 check(reconnectEpisodes==Json::array({1}),"health read exception restarted original recovery episode");
 check(stoppedEpisodes.empty()&&recoveredEpisodes==Json::array({1}),"unknown read must park the same episode until real health returns");
#ifdef YMCC_DECKY_HEALTH_REPORT_MEMORY_FIXTURE
 if(healthReportMemoryProbe::target.load())healthReportMemoryProbe::verifyEdges(healthEdges,check);
 else
#endif
 {
 check(healthEdges.size()==2,"repeated health read faults need exactly failure and return log edges");
 check(healthEdges[0].value("bootstrapStage",std::string{})=="health-read-unavailable"&&healthEdges[1].value("bootstrapStage",std::string{})=="health-read-returned","read return must not claim overall connection recovery");
 check(healthEdges[0].value("attempted",true)==false&&healthEdges[1].value("attempted",true)==false,"observation edges authorize no recovery action");
 }
 check(records.dump().find("untrusted-health-exception-private-text")==std::string::npos,"private exception text must stay outside diagnostics");
 check(preflights==0&&uiSteps==0&&stopped.pid==0&&!stopped.enabled&&reads.load()==readsAfterStop,"health observations and OFF create no process, UI action or late read");
 std::cout<<"Runtime health observation fault tests passed: 7; realSteam=0; loader=0; hardware=0\n";return 0;
}

static int retiredHealthReplyCase(){
 using namespace ymcc::deckysidebar;using Json=nlohmann::json;
 const auto check=[](bool pass,const char* why){if(!pass)throw std::runtime_error(why);};
 unsigned failures=0;
 for(const bool exitFence:{false,true}){
  std::mutex mutex;std::condition_variable changed;
  bool entered=false,released=false,terminal=false;
  std::atomic<unsigned> phase{0},reads{0},completed{0},preflights{0},uiSteps{0};
  Json sequence=Json::array();
  DWORD beforeHandles=0,afterHandles=0;
  check(GetProcessHandleCount(GetCurrentProcess(),&beforeHandles)!=FALSE,"initial handle count");
  {
   Runtime runtime;
   Config config;config.executable=L"NEVER-LAUNCH.exe";config.home=L"NEVER-CREATE-DIRECTORY";
   config.preflight=[&]{++preflights;return std::string("fixture-blocked");};
   config.launchAdmission=[] {return LaunchAdmission{false,"fixture-observation-only",true};};
   config.connectionHealth=[&]()->ConnectionHealth{
    const auto p=phase.load();++reads;
    if(p==1){
     std::unique_lock lock(mutex);entered=true;changed.notify_all();
     if(!changed.wait_for(lock,std::chrono::seconds(3),[&]{return released;}))throw std::runtime_error("fixture read release deadline");
    }
    return {true,p==1||p==3,p==1?"retired-ready-reply":p==3?"current-ready-reply":"waiting",false,p<2?"old-session":"new-session"};
   };
   config.connectionRecovery=[&]{{std::lock_guard lock(mutex);completed=reads.load();}changed.notify_all();};
   config.connectionStep=[&](auto){++uiSteps;return Json{{"ok",false},{"mutated",false}};};
   config.bindingDiagnostic=[&](const Json& row){
    if(row.value("scope",std::string{})!="global-connection")return;
    std::lock_guard lock(mutex);
    const auto action=row.value("recoveryPhase",std::string{});const auto reason=row.value("reason",std::string{});
    sequence.push_back({action,row.at("recoveryEpisode"),reason});
    if(action=="connected"||action=="stopped"){terminal=true;changed.notify_all();}
   };
   const auto wait=[&](auto&& ready){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::seconds(2),ready);};
   ymcc::steamsession::Session original;original.root=L"fixture-root";original.pid=1;original.created=10;original.account=23;
   runtime.initialize(std::move(config));runtime.observeSteam(1,original);
   check(runtime.setEnabled(true)&&wait([&]{return completed>=1;}),"old recovery active");
   phase=1;check(runtime.requestTransportRecovery()&&wait([&]{return entered;}),"old health returned only after lifecycle edge");
   if(exitFence)runtime.requestExitFence();
   else {auto successor=original;successor.pid=2;successor.created=11;runtime.observeSteam(1,successor);}
   phase=2;{std::lock_guard lock(mutex);released=true;}changed.notify_all();
   if(exitFence)check(wait([&]{return terminal;}),"exit must terminate old recovery");
   else {
    check(wait([&]{return completed>=3;}),"successor gets original immediate observation");
    phase=3;check(runtime.requestTransportRecovery()&&wait([&]{return completed>=4;}),"successor may confirm its own recovery");
   }
   runtime.shutdown();
   check(!runtime.requestTransportRecovery()&&!runtime.snapshot().enabled,"Runtime exits and rejects late work");
  }
  check(GetProcessHandleCount(GetCurrentProcess(),&afterHandles)!=FALSE,"final handle count");
  const auto expected=exitFence?Json::array({{"recovering",1,"waiting"},{"stopped",1,"decky-stopped"}}):
      Json::array({{"recovering",1,"waiting"},{"stopped",1,"steam-session-changed"},{"recovering",2,"waiting"},{"connected",2,"current-ready-reply"}});
  const bool pass=sequence==expected&&beforeHandles==afterHandles&&preflights==0&&uiSteps==0;
  const Json row={{"name",exitFence?"exit-fence":"verified-session-change"},{"sequence",sequence},{"expected",expected},
      {"healthReads",reads.load()},{"beforeHandles",beforeHandles},{"afterHandles",afterHandles},{"preflights",preflights.load()},
      {"realSteamActions",0},{"pageActions",uiSteps.load()},{"passed",pass}};
  std::cout<<"RETIRED_HEALTH_REPLY "<<row.dump()<<'\n';
  if(!pass)++failures;
 }
 return failures?1:0;
}

// Explicit timing diagnostic only. The original default ladder suite is not
// lengthened; this uses its real evidence cadence without a manual recovery wake.
static int healthObservationTimerCase(unsigned startupReadTarget=1){
 using namespace ymcc::deckysidebar;using Json=nlohmann::json;
 std::mutex mutex;std::condition_variable changed;
 bool setupReleased=false;
 std::atomic<unsigned> phase{0},reads{0},completedRead{0},loops{0},preflights{0},uiSteps{0};
 std::atomic<bool> connected{false};Json records=Json::array();
 struct ThreadObservation {HANDLE handle=nullptr;DWORD id=0;~ThreadObservation(){if(handle)CloseHandle(handle);}} thread;
 Runtime runtime; // Destroy before captured callback storage, including on failure.
 Config config;config.executable=L"NEVER-LAUNCH.exe";config.home=L"NEVER-CREATE-DIRECTORY";
 config.preflight=[&]{++preflights;return std::string("fixture-no-launch");};
 config.launchAdmission=[](){return LaunchAdmission{false,"fixture-observation-only",false};};
 config.connectionHealth=[&]()->ConnectionHealth{
#ifdef YMCC_DECKY_HEALTH_REPORT_MEMORY_FIXTURE
  healthReportMemoryProbe::Scope optionalLogMemory(phase.load());
#endif
  if(!thread.handle){
   if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&thread.handle,THREAD_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0))throw std::runtime_error("fixture-worker-observation-failed");
   thread.id=GetCurrentThreadId();
  }
  ++reads;if(phase.load()==1)throw std::runtime_error("private-timed-read-fault");
  ConnectionHealth health;health.required=true;health.connected=phase.load()==3;health.identity="fixture-steam:account:ready1";health.reason=health.connected?"fixture-connected":"fixture-disconnected";return health;
 };
 config.connectionStep=[&](auto){++uiSteps;return Json{{"ok",false},{"mutated",false}};};
 config.connectionRecovery=[&]{
  std::unique_lock lock(mutex);++loops;completedRead=reads.load();changed.notify_all();
  // Do not race a due-now startup retry against the test's explicit fault setup.
  if(completedRead==startupReadTarget)changed.wait_for(lock,std::chrono::seconds(3),[&]{return setupReleased;});
 };
 config.bindingDiagnostic=[&](const Json& value){
  records.push_back(ymcc::deckydiag::record("bootstrap-watch",value));
  if(value.value("scope",std::string{})=="global-connection"&&value.value("recoveryPhase",std::string{})=="connected"){
   {std::lock_guard lock(mutex);connected=true;}changed.notify_all();
  }
 };
 const auto check=[](bool ok,const char* name){if(!ok)throw std::runtime_error(name);};
 const auto awaitRead=[&](unsigned count,DWORD window){std::unique_lock lock(mutex);return changed.wait_for(lock,std::chrono::milliseconds(window),[&]{return completedRead.load()>=count;});};
 const auto cpu=[&](){FILETIME birth{},exit{},kernel{},user{};check(thread.handle&&GetThreadTimes(thread.handle,&birth,&exit,&kernel,&user),"exact Runtime worker CPU counter available");const auto value=[](const FILETIME& x){return (ULONGLONG(x.dwHighDateTime)<<32)|x.dwLowDateTime;};return value(kernel)+value(user);};
 runtime.initialize(std::move(config));runtime.observeSteam(1,true);check(runtime.setEnabled(true)&&awaitRead(startupReadTarget,2000),"actual Runtime startup observation completed");
 phase=1;const auto startupReads=reads.load(),firstFault=startupReads+1;const bool wake=runtime.requestTransportRecovery();
 {std::lock_guard lock(mutex);setupReleased=true;}changed.notify_all();
 check(wake&&awaitRead(firstFault,2000),"single explicit fault setup reaches original worker");
 // No requestTransportRecovery, requestConnectionCheck or observer notification
 // occurs from this point until shutdown. Only the production wait may retry.
 const auto measuredAt=GetTickCount64(),cpuBefore=cpu();const auto loopsBefore=loops.load();
 const DWORD window=ConnectionRecoveryWindow::evidenceIntervalMs+1500;
 const bool automaticFaultRead=awaitRead(firstFault+1,window);const auto retryAt=GetTickCount64();
 bool automaticConnected=false;
 if(automaticFaultRead){phase=3;std::unique_lock lock(mutex);automaticConnected=changed.wait_for(lock,std::chrono::milliseconds(window),[&]{return connected.load();});}
 const auto measuredUntil=GetTickCount64(),cpuAfter=cpu();runtime.shutdown();
 const bool workerExited=WaitForSingleObject(thread.handle,0)==WAIT_OBJECT_0;const auto stopped=runtime.snapshot();
 Json reconnects=Json::array(),recovered=Json::array(),edges=Json::array();
 for(const auto& row:records){
  if(row.value("scope",std::string{})=="connection-health")edges.push_back(row);
  if(row.value("scope",std::string{})=="global-connection"&&row.value("stage",std::string{})=="reconnect")reconnects.push_back(row.at("recoveryEpisode"));
  if(row.value("scope",std::string{})=="global-connection"&&row.value("recoveryPhase",std::string{})=="connected")recovered.push_back(row.at("recoveryEpisode"));
 }
 const Json evidence{{"kind","actual-runtime-automatic-observation-timer"},{"automaticFaultRead",automaticFaultRead},{"automaticConnected",automaticConnected},{"firstAutomaticReadMs",retryAt-measuredAt},{"measurementMs",measuredUntil-measuredAt},{"originalEvidenceIntervalMs",ConnectionRecoveryWindow::evidenceIntervalMs},{"readObservationWindowMs",window},{"workerThreadId",thread.id},{"workerCpu100ns",cpuAfter-cpuBefore},{"workerCpuMs",double(cpuAfter-cpuBefore)/10000.0},{"cpuZeroMayReflectCounterGranularity",true},{"loopsDuringMeasurement",loops.load()-loopsBefore},{"totalLoops",loops.load()},{"reads",reads.load()},{"reconnectEpisodes",reconnects},{"recoveredEpisodes",recovered},{"healthEdges",edges},{"workerExitObservedOnHeldHandle",workerExited},{"pidAfterStop",stopped.pid},{"enabledAfterStop",stopped.enabled},{"preflights",preflights.load()},{"uiSteps",uiSteps.load()},{"manualWakeAfterFaultSetup",false},{"realSteamActions",0}};
 std::cout<<"HEALTH_TIMER_EVIDENCE "<<evidence.dump()<<'\n';
 std::cout<<"HEALTH_TIMER_STARTUP_READS "<<startupReads<<'\n';
 check(automaticFaultRead,"health observation did not retry on original evidence cadence");
 check(automaticConnected,"actual connected health did not resume without a manual wake");
 check(reconnects==Json::array({1})&&recovered==Json::array({1}),"automatic observation must preserve original recovery episode");
 check(loops.load()-loopsBefore<=3&&reads.load()<=4,"existing timed observation must wait rather than spin");
#ifdef YMCC_DECKY_HEALTH_REPORT_MEMORY_FIXTURE
 if(healthReportMemoryProbe::target.load())healthReportMemoryProbe::verifyEdges(edges,check);
 else
#endif
 {
 check(edges.size()==2&&edges[0].value("bootstrapStage",std::string{})=="health-read-unavailable"&&edges[1].value("bootstrapStage",std::string{})=="health-read-returned","repeated automatic read fault coalesces to two diagnostic edges");
 }
 check(workerExited&&stopped.pid==0&&!stopped.enabled&&preflights==0&&uiSteps==0,"normal stop returns actual worker without granting an action");
 std::cout<<"Runtime automatic health observation tests passed: 6; realSteam=0; loader=0; hardware=0\n";return 0;
}

static int rebuildChildWindowCase(){
 using namespace ymcc::deckysidebar;
 using Window=ConnectionRecoveryWindow;using Action=Window::Action;
 const ConnectionHealth missing{true,false,"authenticated-peer-missing",false,"same-session-and-power"};
 const auto need=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
 constexpr ULONGLONG confirmed=1000,child=confirmed+10467,oldReopen=confirmed+Window::settleGraceMs,firstProbe=confirmed+10458+12000;
 const auto start=[&](Window& w,bool acknowledge=true){
  need(w.observe(confirmed-3,missing)==Action::reconnect,"start reconnect");w.failed(Action::reconnect,confirmed-2);
  need(w.observe(confirmed-2,missing)==Action::recycle,"start recycle");w.failed(Action::recycle,confirmed-1);
  need(w.observe(confirmed-1,missing)==Action::rebuild,"start rebuild");if(acknowledge)w.started(Action::rebuild,confirmed);
 };
 auto owned=missing;owned.coldLoaderStarted=child;
 Window win5;start(win5);unsigned reservations=0;
 const auto reserve=[&](Action action){need(action==Action::reopen,"unexpected page action");++reservations;return true;};
 need(win5.observe(confirmed+15372,owned,reserve)==Action::none,"socket observation is not health");
 const auto atOld=win5.observe(oldReopen,owned,reserve);
 std::cout<<"REBUILD_CHILD_WINDOW oldReopenAction="<<(atOld==Action::reopen?"reopen":"none")<<" reservations="<<reservations
          <<" remainingMs="<<win5.remaining(oldReopen)<<std::endl;
 need(atOld==Action::none&&reservations==0,"live child lost first-load window to Big Picture reopen");
 need(win5.observe(firstProbe,owned,reserve)==Action::none&&win5.attempts()==3,"original frontend probe has time to run");
 auto healthy=owned;healthy.connected=true;healthy.transportConnected=true;
 need(win5.observe(firstProbe+1,healthy,reserve)==Action::recovered&&reservations==0,"health must retire the episode without a visible action");

 Window dead;start(dead);
 need(dead.observe(child,owned)==Action::none,"child starts before expiry");
 auto duplicate=owned;duplicate.coldLoaderStarted=child+1000;
 need(dead.observe(child+1000,duplicate)==Action::none,"duplicate receipt is only an observation");
 need(dead.remaining(child+Window::settleGraceMs-1)==1,"duplicate receipt must not refill window");
 need(dead.observe(child+Window::settleGraceMs,duplicate,reserve)==Action::reopen&&reservations==1,"unhealthy child still escalates once");

 Window clamped;start(clamped);auto lastMoment=owned;lastMoment.coldLoaderStarted=oldReopen-1;
 need(clamped.observe(oldReopen,lastMoment)==Action::none,"on-time start may align once even at due observation");
 need(clamped.remaining(confirmed+Window::coldStartGraceMs-1)==1,"rebuild hard limit uses existing cold cap");
 need(clamped.observe(confirmed+Window::coldStartGraceMs,lastMoment)==Action::reopen,"hard cap remains terminal for the lower step");
 for(const auto invalid:{ULONGLONG{0},confirmed-1,oldReopen,oldReopen+1}){
  Window trial;start(trial);auto health=missing;health.coldLoaderStarted=invalid;
  need(trial.observe(oldReopen,health)==Action::reopen,"missing, stale or after-deadline loader restores no time");
 }
 Window unknown;start(unknown,false);
 need(unknown.observe(oldReopen-1,owned)==Action::reopen,"unconfirmed rebuild grants no handoff");
 Window failed;start(failed);failed.failed(Action::rebuild,child);
 need(failed.observe(child,owned)==Action::reopen,"definitive rebuild failure still escalates");
 Window paused;start(paused);auto suspended=owned;suspended.paused=true;
 need(paused.observe(oldReopen,suspended)==Action::none,"suspended observation never issues a page action");
 need(paused.observe(oldReopen+1,owned)==Action::reopen,"resuming past the deadline cannot retroactively grant time");
 Window off;start(off);need(off.observe(child,owned)==Action::none,"live before OFF");
 need(off.observe(child+1,{false,false,"off"})==Action::stopped&&off.remaining(child+1)==0,"OFF retires the aligned window");
 // The Runtime already owns the new child before the deadline. A delayed
 // health read must not confuse the observation time with the start time.
 Window delayed;start(delayed);delayed.serviceStarted(child);unsigned delayedReservations=0;
 const auto lateReserve=[&](Action action){need(action==Action::reopen,"unexpected delayed action");++delayedReservations;return true;};
 const auto delayedAction=delayed.observe(oldReopen+1,owned,lateReserve);
 std::cout<<"REBUILD_CHILD_RECEIPT lateAction="<<(delayedAction==Action::reopen?"reopen":"none")
          <<" reservations="<<delayedReservations<<" remainingMs="<<delayed.remaining(oldReopen+1)<<std::endl;
 need(delayedAction==Action::none&&delayedReservations==0,"on-time owned Loader receipt lost to a delayed health read");
 need(delayed.remaining(oldReopen+1)==10466,"delayed read keeps the original start tick and deadline");
 need(delayed.observe(firstProbe+1,healthy,lateReserve)==Action::recovered&&delayedReservations==0,"late observation can settle with no page action");
 for(const auto rejected:{missing,duplicate}){
  Window vanished;start(vanished);vanished.serviceStarted(child);
  need(vanished.observe(oldReopen+1,rejected)==Action::reopen,"missing or different live child cannot spend the old receipt");
 }
 Window repeat;start(repeat);repeat.serviceStarted(child);repeat.serviceStarted(child+1000);
 need(repeat.observe(oldReopen+1,owned)==Action::none,"duplicate launch does not replace the first receipt");
 need(repeat.observe(child+Window::settleGraceMs,owned)==Action::reopen,"unhealthy child still reaches the original aligned deadline");
 Window late;start(late);late.serviceStarted(oldReopen);auto lateChild=owned;lateChild.coldLoaderStarted=oldReopen;
 need(late.observe(oldReopen+1,lateChild)==Action::reopen,"launch at the expired deadline grants no handoff");
 Window switched;start(switched);switched.serviceStarted(child);auto successor=owned;successor.identity="next-power-generation";
 need(switched.observe(oldReopen+1,successor)==Action::reconnect,"replacement identity must not inherit old page step");
 std::cout<<"REBUILD_CHILD_RECEIPT_OK boundaries=9 modeCommands=0\n";
 std::cout<<"REBUILD_CHILD_WINDOW_OK fieldOffsets=10467,15372,20000 modeCommands=0\n";
 return 0;
}
static void busyCommandRecoveryCase(){
 using namespace ymcc::deckysidebar;
 using Window=ConnectionRecoveryWindow;using Action=Window::Action;
 const ConnectionHealth missing{true,false,"fixture-disconnected",false,"steam-a:account-a:ready1"};
 auto pending=missing;pending.paused=true;
 auto healthy=missing;healthy.connected=true;
 auto successor=missing;successor.identity="steam-a:account-a:ready2";
 const ULONGLONG at=103,interval=Window::evidenceIntervalMs;
 const auto need=[](bool pass,const char* why){if(!pass)throw std::runtime_error(why);};
 const auto qualifyRebuild=[&](Window& window){
    need(window.observe(at-3,missing)==Action::reconnect,"busy case enters reconnect");
    window.failed(Action::reconnect,at-2);
    need(window.observe(at-2,missing)==Action::recycle,"busy case enters recycle");
    window.failed(Action::recycle,at-1);
 };
 Window window;qualifyRebuild(window);
 unsigned reservations=0;bool free=false;Action expected=Action::rebuild;
 const auto reserve=[&](Action step){need(step==expected,"busy command cannot skip its rung");++reservations;return free;};
 need(window.observe(at,missing,reserve)==Action::none&&window.timeout(at)==interval,"busy command schedules original evidence interval");
 for(unsigned i=1;i<=100;++i)need(window.observe(at+i,missing,reserve)==Action::none,"repeated wake must not run a busy page command");
 std::cout<<"BUSY_PAGE_SLOT_STORM reservations="<<reservations<<" wakes=100\n";
 need(reservations==1,"repeated wakes must not re-request the same busy page slot");
 need(window.observe(at+interval-1,missing,reserve)==Action::none&&window.timeout(at+interval-1)==1,"wakes never postpone the scheduled attempt");
 need(window.observe(at+interval,missing,reserve)==Action::none&&reservations==2&&window.timeout(at+interval)==interval,"persistent contention keeps one scheduled attempt");
 free=true;
 need(window.observe(at+2*interval,pending,reserve)==Action::none&&reservations==2,"unknown proof cannot spend the page step");
 const auto rebuiltAt=at+2*interval+1;
 need(window.observe(rebuiltAt,missing,reserve)==Action::rebuild&&reservations==3&&window.attempts()==3,"fresh evidence can use the available slot without resetting the original stage count");
 expected=Action::reopen;free=false;window.failed(Action::rebuild,rebuiltAt+1);
 need(window.observe(rebuiltAt+1,missing,reserve)==Action::none&&reservations==4,"reopen shares the same reservation throttle");
 need(window.observe(rebuiltAt+2,missing,reserve)==Action::none&&reservations==4,"busy reopen cannot re-request early");
 need(window.observe(rebuiltAt+3,healthy,reserve)==Action::recovered&&reservations==4,"complete health immediately ends even a busy episode");
 for(const auto& end:{ConnectionHealth{false,false,"off"},successor}){
    Window reset;qualifyRebuild(reset);expected=Action::rebuild;const auto count=reservations;
    need(reset.observe(at,missing,reserve)==Action::none&&reservations==count+1,"retirement control starts with a refused slot");
    need(reset.observe(at+1,end,reserve)==(end.required?Action::reconnect:Action::stopped)&&reservations==count+1,"OFF or new identity ends the old reservation wait");
    if(!end.required)need(reset.observe(at+2,missing,reserve)==Action::reconnect,"explicit next episode starts normally");
    const auto& current=end.required?end:missing;
    reset.failed(Action::reconnect,at+3);need(reset.observe(at+3,current,reserve)==Action::recycle,"new episode can use its original recycle rung");
    reset.failed(Action::recycle,at+4);free=true;
    need(reset.observe(at+4,current,reserve)==Action::rebuild&&reservations==count+2,"old busy deadline cannot block a new episode");
    free=false;
 }
 // A shorter paused shared-read deadline can land between the slot refusal
 // and its retry. Resuming must wait for the remainder, not another interval.
 for(const bool unstarted:{false,true}){
    Window resumed;qualifyRebuild(resumed);
    const auto retryAt=at+interval,proofAt=at+interval/2;
    unsigned requests=0;
    const auto ask=[&](Action step){need(step==Action::rebuild,"same page rung after temporary proof loss");++requests;return unstarted||requests>1;};
    need(resumed.observe(at,missing,ask)==(unstarted?Action::rebuild:Action::none),"retry control reaches the selected page rung");
    if(unstarted)resumed.parkUnstartedPage(Action::rebuild,at);
    auto readPending=pending;readPending.pausedReadDue=proofAt;
    need(resumed.observe(at+1,readPending,ask)==Action::none&&resumed.timeout(at+1)==proofAt-at-1,"paused proof uses the shared read and does not request the slot");
    need(resumed.observe(proofAt,missing,ask)==Action::none&&requests==1,"fresh proof does not allow an early page request");
    const auto remaining=resumed.timeout(proofAt);
    std::cout<<"BUSY_PAGE_RESUME_BACKOFF unstarted="<<unstarted<<" remainingMs="<<remaining<<" expectedMs="<<(retryAt-proofAt)<<std::endl;
    need(remaining==retryAt-proofAt,"proof return must not postpone page slot retry");
    need(resumed.observe(retryAt-1,missing,ask)==Action::none&&resumed.timeout(retryAt-1)==1&&requests==1,"remaining slot observation never runs early");
    need(resumed.timeout(retryAt)==0&&resumed.observe(retryAt,missing,ask)==Action::rebuild&&requests==2&&resumed.attempts()==3,"timed retry returns to the original rung and count");
    need(resumed.timeout(retryAt)>0,"successful slot selection clears the due retry");
 }
 std::cout<<"BUSY_PAGE_SLOT_OK retryMs="<<interval<<" realSteamActions=0\n";
}
static void pageAttemptCooldownCase(){
 using namespace ymcc::deckyrepair;
 using Window=ymcc::deckysidebar::ConnectionRecoveryWindow;using Action=Window::Action;
 const auto need=[](bool pass,const char* message){if(!pass)throw std::runtime_error(message);};
 const ymcc::deckysidebar::ConnectionHealth missing{true,false,"official-backend-unavailable",false,"fixture-account",true};
 const Json noStart={{"ok",false},{"mutated",false},{"reason","decky-cleanup-account-unconfirmed"},{"awaitCurrentIdentity",true},{"cleanupNotStarted",true}};
 const Json success={{"ok",true},{"mutated",true},{"reason","fixture-dispatched"}};
 ManualRepair repair;Dependencies deps;ULONGLONG clock=1000;
 ymcc::steamsession::Session session{L"C:\\fixture-steam",412u,91u,987654ULL};
 const Json state={{"enabled",true},{"pid",0u},{"phase","frontend-attached"},{"reason","retained-frontend-connected"},{"revision",7u}};
 deps.session=[&]{return session;};deps.runtime=[&]{return state;};deps.now=[&]{return clock;};
 deps.exiting=[]{return false;};deps.consent=[](const auto&){return true;};deps.cleanupAllowed=deps.consent;
 const auto request=[&](Origin origin){
  const auto name=origin==Origin::FinalCleanup?"finalCleanup":"reopenBigPicture";
  return repair.request({{name,true},{"pid",session.pid},{"birth",std::to_string(session.created)},
   {"revision",state.at("revision")},{"modeEpoch","fixture-document"}},deps,origin);
 };
 unsigned calls=0;bool throwReply=false,checkReentry=false;Origin currentOrigin=Origin::FinalCleanup;Json reply=noStart;
 deps.cleanupWorker=[&](const auto&,const auto&,const auto& current)->Json{
  ++calls;need(current(),"private cleanup worker receives fresh current action");
  if(std::exchange(checkReentry,false))need(request(currentOrigin).value("reason",std::string{})=="decky-repair-wait","no concurrent cleanup while proof is checked");
  if(throwReply)throw std::runtime_error("unknown dispatch result");
  return reply;
 };
 for(const auto rung:{Action::rebuild,Action::reopen}){
  currentOrigin=rung==Action::rebuild?Origin::FinalCleanup:Origin::ReopenBigPicture;
  Window window;
  need(window.observe(clock,missing)==Action::recycle,"retained mirror enters recycle rung");
  window.failed(Action::recycle,++clock);
  auto step=window.observe(clock,missing);
  if(rung==Action::reopen){window.failed(Action::rebuild,++clock);step=window.observe(clock,missing);}
  need(step==rung,"page rung is available before the proof gap");
  reply=noStart;calls=0;checkReentry=true;
  need(request(currentOrigin)==noStart&&calls==1,"trusted pre-child refusal returns its original flags");
  window.parkUnstartedPage(rung,clock);
  ++clock;need(window.observe(clock,missing)==Action::none,"no early retry after pre-child failure");
  clock+=Window::evidenceIntervalMs-1;
  need(window.observe(clock,missing)==rung,"original retry returns to the same rung at five seconds");
  reply=success;const auto result=request(currentOrigin);
  std::cout<<"PAGE_ATTEMPT_COOLDOWN "<<(rung==Action::rebuild?"rebuild":"reopen")
      <<" atMs=5000 calls="<<calls<<" reason="<<result.at("reason").get<std::string>()<<std::endl;
  need(result==success&&calls==2,"typed unstarted page proof must not spend the repair cooldown");
  ++clock;need(request(currentOrigin).value("reason",std::string{})=="decky-repair-wait"&&calls==2,"real dispatch still spends its origin cooldown");
 }
 for(unsigned fault=0;fault<8;++fault){
  ++session.created;reply=noStart;calls=0;throwReply=fault==7;
  switch(fault){
   case 0:reply["uncertain"]=true;break;
   case 1:reply.erase("awaitCurrentIdentity");break;
   case 2:reply.erase("cleanupNotStarted");break;
   case 3:reply["ok"]=0;break;
   case 4:reply["reason"]="different-refusal";break;
   case 5:reply["mutated"]=true;break;
   case 6:reply=success;break;
  }
  const auto result=request(Origin::FinalCleanup);
  need(calls==1&&(!throwReply||result.value("uncertain",false)),"non-exempt response reaches sole cleanup attempt");
  clock+=Window::evidenceIntervalMs;
  need(request(Origin::FinalCleanup).value("reason",std::string{})=="decky-repair-wait"&&calls==1,"uncertain, untyped and consumed responses keep cooldown");
 }
 throwReply=false;reply=noStart;++session.created;
 need(request(Origin::FinalCleanup)==noStart,"new session pre-child refusal is distinct from old consumed cooldown");
 --session.created;
 need(request(Origin::FinalCleanup).value("reason",std::string{})=="decky-repair-wait","unstarted successor does not erase the prior spent cooldown");
 std::cout<<"PAGE_ATTEMPT_COOLDOWN_CONTROLS 9 PASS\n";
}
int main(int argc,char** argv){
if(argc==3&&std::string_view(argv[1])=="--health-observation-timer"&&std::string_view(argv[2])=="--control-extra-startup-read"){
 try{return healthObservationTimerCase(2);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
if(argc==3&&std::string_view(argv[1])=="--health-observation-fault"&&std::string_view(argv[2])=="--retired-reply"){
 try{return retiredHealthReplyCase();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
if(argc==2&&std::string_view(argv[1])=="--rebuild-child-window"){try{return rebuildChildWindowCase();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
if(argc==1){
 using Json=nlohmann::json;
 constexpr auto unknown="decky-recovery-unconfirmed", acknowledged="decky-recovery-action-acknowledged";
 const Json receipt={{"stage","rebuild-ui-document"},{"ok",true},{"mutated",true},{"uncertain",false},{"reason",unknown}};
 unsigned checked=0,failed=0;
 const auto check=[&](const char* label,const std::string& event,const Json& detail,const char* want){
  const auto logged=ymcc::deckydiag::record(event,detail);
  const bool pass=logged.value("reason",std::string{})==want&&!logged.contains("connected");
  ++checked;if(!pass)++failed;
  std::cout<<"RECOVERY_ACTION_DIAGNOSTIC "<<label<<" "<<(pass?"PASS":"FAIL")<<"\n";
 };
 check("confirmed-page-command","connection-recovery",receipt,acknowledged);
 auto row=receipt;row["stage"]="reopen-bigpicture";
 check("confirmed-big-picture-command","connection-recovery",row,acknowledged);
 row["reason"]="decky-bigpicture-reopened";
 check("specific-receipt-reason","connection-recovery",row,"decky-bigpicture-reopened");
 row=receipt;row["ok"]=false;row["mutated"]=false;row["reason"]="decky-cleanup-account-unconfirmed";
 check("no-effect-refusal","connection-recovery",row,"decky-cleanup-account-unconfirmed");
 row=receipt;row["mutated"]=false;
 check("no-mutation","connection-recovery",row,unknown);
 row=receipt;row["uncertain"]=true;
 check("uncertain-command","connection-recovery",row,unknown);
 row=receipt;row.erase("uncertain");
 check("missing-certainty","connection-recovery",row,unknown);
 row=receipt;row["ok"]="true";row["uncertain"]=0;
 check("nonboolean-receipt","connection-recovery",row,unknown);
 row=receipt;row["stage"]="reconnect";
 check("different-recovery-step","connection-recovery",row,unknown);
 check("different-log-event","runtime",receipt,unknown);
 std::cout<<"RECOVERY_ACTION_DIAGNOSTIC_RESULT "<<checked<<" "<<failed<<"\n";
 using ymcc::steamfailure::Operation;using ymcc::steamfailure::Phase;
 ymcc::steamfailure::FailureContext context;context.phase=Phase::Finished;context.operation=Operation::RebuildDocument;
 const Json waiting={{"ok",false},{"mutated",false},{"reason","decky-cleanup-account-unconfirmed"},
  {"awaitCurrentIdentity",true},{"cleanupNotStarted",true},{"sessionCheck","shared-process-receipt"}};
 const auto traceCheck=[&](const char* label,const Json& detail,bool want,const char* stage=nullptr){
  const auto logged=ymcc::deckydiag::failureRecord(detail,context);
  const bool pass=(want?logged.value("cleanupUnstartedIdentityWait",Json{})==true:!logged.contains("cleanupUnstartedIdentityWait"))&&
   (!stage||logged.value("cleanupAccountStage",Json{})==stage)&&!logged.contains("sessionCheck")&&
   !logged.contains("awaitCurrentIdentity")&&!logged.contains("cleanupNotStarted");
  if(!pass)++failed;
  std::cout<<"RECOVERY_IDENTITY_DIAGNOSTIC "<<label<<" "<<(pass?"PASS":"FAIL")<<"\n";
 };
 traceCheck("page-parent-receipt",waiting,true,"shared-process-receipt");
 context.operation=Operation::ReopenBigPicture;row=waiting;row["sessionCheck"]="shared-process-delegation";
 traceCheck("bpm-parent-delegation",row,true,"shared-process-delegation");
 row["sessionCheck"]="child-account-proof";traceCheck("child-refusal",row,false,"child-account-proof");
 row["sessionCheck"]="parent-account-source";row.erase("cleanupNotStarted");traceCheck("parent-account-missing",row,false,"parent-account-source");
 row["sessionCheck"]="private-value";traceCheck("unknown-stage",row,false);
 row=waiting;row.erase("awaitCurrentIdentity");traceCheck("identity-wait-missing",row,false);
 row=waiting;row.erase("cleanupNotStarted");traceCheck("unstarted-proof-missing",row,false);
 row=waiting;row["cleanupNotStarted"]="true";traceCheck("unstarted-proof-malformed",row,false);
 row=waiting;row["uncertain"]=true;traceCheck("uncertain",row,false);
 row=waiting;row["mutated"]=true;traceCheck("effect-possible",row,false);
 row=waiting;row["ok"]=true;traceCheck("success",row,false);
 row=waiting;row["reason"]="decky-repair-wait";traceCheck("different-reason",row,false);
 context.phase=Phase::Started;traceCheck("nonterminal",waiting,false);
 context.phase=Phase::Finished;context.operation=Operation::Binding;traceCheck("different-operation",waiting,false);
 if(failed)return 1;
}
// Same original cases; the extra memory argument never selects the default parent.
if(argc==3&&(std::string_view(argv[2]).starts_with("--optional-log-memory=")||std::string_view(argv[2])=="--health-reason-memory")){
 const std::string_view selection=argv[1],parameter=argv[2];
 if(selection!="--health-observation-fault"&&selection!="--health-observation-timer")return 2;
 if(parameter!="--optional-log-memory=unavailable"&&parameter!="--optional-log-memory=returned"&&parameter!="--health-reason-memory")return 2;
#ifndef YMCC_DECKY_HEALTH_REPORT_MEMORY_FIXTURE
 std::cerr<<"Explicit health report allocation fixture build required"<<std::endl;return 64;
#else
 healthReportMemoryProbe::target=parameter=="--optional-log-memory=returned"?2u:1u;
 healthReportMemoryProbe::reasonBuffer=parameter=="--health-reason-memory";
 DWORD beforeHandles=0,afterHandles=0;if(!GetProcessHandleCount(GetCurrentProcess(),&beforeHandles))return 2;
 int result=0;try{result=selection=="--health-observation-timer"?healthObservationTimerCase():healthObservationFaultCase();}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}
 if(!GetProcessHandleCount(GetCurrentProcess(),&afterHandles))return 2;
 const nlohmann::json fault={{"edge",healthReportMemoryProbe::target.load()},{"reasonBufferTarget",healthReportMemoryProbe::reasonBuffer.load()},{"hits",healthReportMemoryProbe::hits.load()},{"failures",ymcc::deckybinding::fixture::constructionprobe::failures.load()},
  {"bytes",ymcc::deckybinding::fixture::constructionprobe::failedBytes.load()},{"beforeHandles",beforeHandles},{"afterHandles",afterHandles},{"originalCasePassed",result==0},{"realSteamActions",0}};
 std::cout<<"HEALTH_REPORT_MEMORY_EVIDENCE "<<fault.dump()<<std::endl;return result==0&&beforeHandles==afterHandles?0:1;
#endif
}

#ifdef YMCC_DECKY_BUDGET_SOURCE
if(argc==2&&std::string(argv[1])=="--budget-final-readback-only"){try{return recoveryBudgetReservationCase(true);}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
if(argc==2&&std::string(argv[1])=="--budget-reservation"){try{return recoveryBudgetReservationCase();}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
#endif
if(argc==2&&std::string(argv[1])=="--health-observation-timer"){try{return healthObservationTimerCase();}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}if(argc==2&&std::string(argv[1])=="--health-observation-fault"){try{return healthObservationFaultCase();}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}if(argc!=1){std::cerr<<"Unknown recovery selftest selection\n";return 2;}using ymcc::deckysidebar::ConnectionRetryWindow;
 try{if(const int result=rebuildChildWindowCase())return result;busyCommandRecoveryCase();pageAttemptCooldownCase();}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
 assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-async-error"));assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-deadline"));
 assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-ws-closed"));assert(ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-ws-receive-failed"));assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-ws-invalid"));
 assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-owner-mismatch"));assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-readback-failed"));assert(!ymcc::deckysidebar::recoverableWatchFailure("mirror-bootstrap-canceled"));
 ConnectionRetryWindow window;assert(window.timeout(100)==INFINITE);
 ULONGLONG now=100;constexpr DWORD delays[]{250,1000,3000,10000,30000,30000,30000,30000};
 for(auto delay:delays){window.begin(now);assert(window.timeout(now)==delay);window.begin(now+10);assert(window.timeout(now)==delay);assert(!window.take(now+delay-1));assert(window.take(now+delay));assert(window.timeout(now+delay)==INFINITE);now+=delay;}
 window.begin(now);window.cancel();assert(window.timeout(now)==INFINITE);window.begin(now);assert(window.timeout(now)==30000);window.reset();window.begin(now);assert(window.timeout(now)==250);window.reset();assert(window.timeout(now)==INFINITE);
 using Fallback=ymcc::deckysidebar::ConnectionRecoveryWindow;using Action=Fallback::Action;
 const ymcc::deckysidebar::ConnectionHealth missing{true,false,"unknown-future-error",false,"steam-a:account-a:ready1"};
 Fallback ladder;now=100;
 assert(ladder.observe(now,missing)==Action::reconnect);
 for(unsigned i=0;i<10000;++i)assert(ladder.observe(now+1,missing)==Action::none);
 assert(ladder.timeout(now+1)==4999&&ladder.remaining(now+1)==6999);
 assert(ladder.observe(now+6999,missing)==Action::none);
 assert(ladder.observe(now+7000,missing)==Action::recycle);
 ladder.started(Action::recycle,now+7100); // Actual child creation, not the earlier restart request.
 ladder.started(Action::recycle,now+9000); // Duplicate receipt cannot refund20s.
 assert(ladder.observe(now+27099,missing)==Action::none);
 assert(ladder.observe(now+27100,missing)==Action::rebuild);
 ladder.started(Action::rebuild,now+27300); // Actual positive dispatch receipt.
 assert(ladder.observe(now+47299,missing)==Action::none);
 assert(ladder.observe(now+47300,missing)==Action::reopen);
 ladder.failed(Action::reopen,now+47301);
 assert(ladder.observe(now+47301,missing)==Action::exhausted);
 assert(ladder.observe(now+999999,missing)==Action::none); // No whole-Steam or second visible restart.
 assert(ladder.observe(now+1000000,{true,true,"actual-three-channel-ready",false,missing.identity})==Action::recovered);
 assert(ladder.remaining(now+1000000)==0&&ladder.stageBudget()==0);
 assert(ladder.observe(now+1000001,missing)==Action::reconnect);
 Fallback failed;assert(failed.observe(now,missing)==Action::reconnect);
 failed.failed(Action::reconnect,now+1);assert(failed.observe(now+1,missing)==Action::recycle);
 failed.failed(Action::reconnect,now+2);assert(failed.observe(now+2,missing)==Action::none); // Late old-stage failure.
 failed.failed(Action::recycle,now+3);assert(failed.observe(now+3,missing)==Action::rebuild); // No owned service skips dead wait.
 failed.failed(Action::rebuild,now+4);assert(failed.observe(now+4,missing)==Action::reopen);
 assert(failed.attempts()==4);
 auto officialOnly=missing;officialOnly.transportConnected=true;
 Fallback backend;assert(backend.observe(now,officialOnly)==Action::recycle); // Rebinding a healthy mirror cannot fix dead official WS.
 Fallback pendingAfterIdle;assert(pendingAfterIdle.observe(now,{false,false,"session-initializing"})==Action::none);
 assert(pendingAfterIdle.timeout(now)==30000);auto initialPending=missing;initialPending.paused=true;
 assert(pendingAfterIdle.observe(now+1,initialPending)==Action::none&&pendingAfterIdle.timeout(now+1)==5000); // Idle30s cannot hide newly pending proof.
 Fallback paused;assert(paused.observe(now,missing)==Action::reconnect);
 auto pending=missing;pending.required=false;pending.paused=true;
 assert(paused.observe(now+20000,pending)==Action::none&&paused.timeout(now+20000)>0);
 assert(paused.observe(now+20001,missing)==Action::recycle&&paused.attempts()==2);
 auto nextReady=missing;nextReady.identity="steam-a:account-a:ready2";
 assert(paused.observe(now+20002,nextReady)==Action::reconnect&&paused.attempts()==1);
 assert(paused.observe(now+20003,{false,false,"off"})==Action::stopped);
 Fallback stall;assert(stall.observe(now,missing)==Action::reconnect);
 assert(stall.observe(now+86400000,missing)==Action::recycle&&stall.attempts()==2);
 assert(stall.observe(now+86400001,missing)==Action::none); // One next step, no catch-up burst.
 Fallback cold;auto firstBoot=missing;firstBoot.coldLoaderStarted=now;
 assert(cold.observe(now+1000,firstBoot)==Action::reconnect);
 assert(!cold.serviceStarted(now+10000));cold.started(Action::reconnect,now+11000);
 assert(cold.observe(now+36999,firstBoot)==Action::none&&cold.remaining(now+36999)==1);
 assert(cold.observe(now+37000,firstBoot)==Action::recycle); // Initial31.371s cold case survives, deadline is not refilled.
 // Recorded transport-only samples; backend/hooks are independent inert ports
 // assumed already healthy here, NOT inferred from historical mirror records.
 // Calibration episodes SHA256: 884252b4137d2ef10f7c251ed2cc60bb69672da80120d202902425ee3bbe1456
 constexpr DWORD recordedRebindMs[]{32,30,910,107,121,66,123,26,34,73,27,94,116,54,1064,351,1303,56,90,77,3645,749,650,95,828,4065087,4432,264,1286,121,167,125,78,132,140,141,148,98,108,77,187,109,3688,109,1127,3658,122,151,385,174,5106,2421,2009,1406,123,231,155,122,218,92,2323,203,2737,1707,95,1799,4987,3985,4369,3198,2832,85,2364,4083,2509,3743,2866,107,1862,1315,2446,4682,105,2994,4075,4665,4763,2128,2182,2465,1336,184,4682,2434,123,260,114,3270,96,2937,2932,4270,106,122,2974,2433,4981,1262,2321,4959,2530,3270,4113,4740,4748,3761,1582,2067,2843,2632,2574,4674,184,4343,92,172,121,388,4628,4399,4077,90,89,579,1964,525,152,513,3739,5043,1109,593,82,2518,4131,1042,592,959,4613,4613,91,2295,1131,2519,124,201,3447,4995,4932,4034,1193,1824,1596,5128,1470,116,5008,2635,770,4698,4256,1916,92,3345,1118,2469,2164,229,82,1100,609,327,73,483,1770,91,84,121,108,124};
 constexpr DWORD recordedWakeMs[]{172,19,20,27,25,34,26,1924,1066,1333,843,1247,698,68,143,119,208,1916,166,1941,194,1730,1255,1656,273,1942,160,1926,259,1610,130,87,940,171,1693,2804,1813,1303,719,1944,1988,1753,1913,1994,1603,1024};
 constexpr DWORD recordedColdMs[]{30946,16195,31371,1625,16498,27440,29780,1015949,4323,2330,2387,8515,438,29,3,2681,7911,5686,539,3515,2309,2448,8673,2129,2271,2193,2122,2343,2957,2369,3050,2911,2855,2913,2274,7660,7281,2764,2485,2338,2913,2182,2293,2367,2300,7799,2713,2473,3054,2292,2412,3950,3164,2565,2261,2213,2302,1573,3527,3451,3497,2429,2376,2263,2848,2121,2492,2393,2145,3589,3931,97,2037,16,2,2050,2802,1944,1873,2099,1952,8180,4546,7711,7164,5481,8242,4301,6070,5052,4650,3898,1964,14191,13765,12020,11671,2311,13398,2614,3623,4494,3609,2857,2568,2127,2316,3800,1950,2338,3125,7474,2458,2640,3705,2957,1960,2968,2770,6876,2196,5412,4591,4828,3319,4402,4017,3998,3433,3853,2838,5551,4915,2617,2169,4779,2986,4157,2968,5037,2980,3206,2659,2169,3054,2199,3276,3360,3051,1960,4435,2000,3860,3641,6331,5505,5919,2188,2847,3730,3282,2352,4250,2356,2857,4840,4259,3236,3067,3183,4318,3063,2169,4412,2854,3793,2730,2911,4949,2600,5216,5720,3279,3294,2745,2774,3418,1969,3495,2637,2507,2630,2838,2848,2938,3031,3145,2684,2916,2981,2017,2139,7148,3469,3732,4321,2661,2281,3545,2441,2638,2636,3345,2691,2052,2104,2250,4349,2366,1858,2101,2077,4120,4561,12619,13099,2425,2355,4723,12018};
 constexpr DWORD recordedContextMs[]{13777,13707};
 unsigned historicalTimely=0,historicalLate=0;
 for(const auto elapsed:recordedRebindMs){Fallback trial;assert(trial.observe(now,missing)==Action::reconnect);
   if(elapsed<=Fallback::transportGraceMs){assert(trial.observe(now+elapsed-1,missing)==Action::none);assert(trial.observe(now+elapsed,{true,true,"fixture-ui-healthy-and-recorded-mirror"})==Action::recovered);++historicalTimely;}
   else {assert(trial.observe(now+Fallback::transportGraceMs,missing)==Action::recycle);++historicalLate;}}
 assert(historicalTimely==189&&historicalLate==1);
 for(const auto elapsed:recordedWakeMs){Fallback trial;assert(trial.observe(now,missing)==Action::reconnect);
   assert(trial.observe(now+elapsed-1,missing)==Action::none);assert(trial.observe(now+elapsed,{true,true,"fixture-ui-healthy-and-recorded-mirror"})==Action::recovered);}
 for(const auto elapsed:recordedColdMs){Fallback trial;auto h=missing;h.coldLoaderStarted=now;assert(trial.observe(now,h)==Action::reconnect);
   if(elapsed<Fallback::coldStartGraceMs){assert(trial.observe(now+elapsed-1,h)==Action::none);assert(trial.observe(now+elapsed,{true,true,"fixture-ui-healthy-and-recorded-mirror"})==Action::recovered);}
   else assert(trial.observe(now+Fallback::coldStartGraceMs,h)==Action::recycle);}
 for(const auto elapsed:recordedContextMs){Fallback trial;assert(trial.observe(now,missing)==Action::reconnect);trial.failed(Action::reconnect,now+1);assert(trial.observe(now+1,missing)==Action::recycle);
   trial.failed(Action::recycle,now+2);assert(trial.observe(now+2,missing)==Action::rebuild);trial.started(Action::rebuild,now+3);
   assert(trial.observe(now+3+elapsed-1,missing)==Action::none);assert(trial.observe(now+3+elapsed,{true,true,"fixture-ui-healthy-and-recorded-mirror"})==Action::recovered);}
 std::cout<<"Historical timing samples: rebind190 (189 timely/1 late kept), wake46, token-verified cold230, context2; transport-only evidence, inert UI ports\n";
 // OP3 2026-11-05: repeated proven elevated-shell refusal must park the
 // generic UI ladder, while ordinary outages retain all existing stages above.
 using Policy=ymcc::deckysidebar::LaunchPolicyStop;Policy policy;unsigned probes=0;
 assert(!policy.failed("create-process",ERROR_ACCESS_DENIED,100));
 assert(!policy.failed("shell-token-elevated",ERROR_INVALID_PARAMETER,100));
 assert(!policy.blocked()&&policy.timeout(100)==INFINITE);
 assert(policy.failed("shell-token-elevated",ERROR_ACCESS_DENIED,100));
 for(unsigned i=0;i<10000;++i){assert(policy.failed("shell-token-elevated",ERROR_ACCESS_DENIED,101));assert(!policy.recheck(101,[&]{++probes;return true;}));}
 assert(probes==0&&policy.timeout(101)==29999); // Storms cannot move the deadline or invoke extra reads.
 assert(!policy.recheck(30100,[&]{++probes;return false;}));assert(probes==1&&policy.blocked());
 assert(!policy.recheck(60100,[&]()->bool{++probes;throw std::runtime_error("unknown token");}));assert(policy.blocked());
 assert(policy.recheck(90100,[&]{++probes;return true;}));assert(probes==3&&!policy.blocked()&&policy.timeout(90100)==INFINITE);
 policy.failed("shell-token-elevated",ERROR_ACCESS_DENIED,90200);policy.reset();assert(!policy.blocked());
 Fallback parked;assert(parked.observe(100,missing)==Action::reconnect);
 assert(parked.observe(101,{false,false,"launch-policy-blocked"})==Action::stopped);
 for(unsigned i=0;i<10000;++i)assert(parked.observe(102+i,{false,false,"launch-policy-blocked"})==Action::none);
 assert(parked.observe(90100,missing)==Action::reconnect); // Environment recovery uses normal authority again.
 std::cout<<"Elevated-shell classification PASS: wrong errors remain recoverable, bounded read-only probes, storms, unknown, restored and explicit reset\n";
 using Json=nlohmann::json;ymcc::steamsession::Session session;session.pid=77;session.created=88;session.root=L"C:\\inert";session.account=99;
 unsigned cycles=0;
 for(const std::string scenario:{"success","close-no-transition","close-rejected","open-no-transition","open-rejected","off-after-close","account-after-close","identity-missing","throws"}){
   ++cycles;std::vector<std::string>sent;unsigned long long tick=100;bool current=true;unsigned reads=0;
   const auto read=[&]()->Json{
     ++reads;
     if(scenario=="throws"&&!sent.empty())throw std::runtime_error("inert");
     std::string mode=sent.empty()?"bigpicture":sent.back();
     if(scenario=="close-no-transition"&&!sent.empty())mode="bigpicture";
     if(scenario=="open-no-transition"&&sent.size()==2)mode="desktop";
     return {{"known",true},{"running",true},{"pid",session.pid},{"birth",std::to_string(session.created)},
       {"mode",mode},{"observedAccount",scenario=="account-after-close"&&!sent.empty()?100:99},
       {"deckyAccountWatchAvailable",scenario!="identity-missing"}};
   };
   const auto result=ymcc::deckyrepair::bigpicture::run(session,[&]{return current;},read,[&](const std::string& mode){
     sent.push_back(mode);if(scenario=="off-after-close")current=false;
     return !(scenario=="close-rejected"||scenario=="open-rejected"&&mode=="bigpicture");
   },[&]{return tick;},[&]{tick+=500;});
   assert(result.value("ok",false)==(scenario=="success"));
   const auto expectedStage=scenario=="success"?"complete":scenario=="identity-missing"?"initial-read":
     scenario=="close-rejected"?"close-submit":scenario=="open-rejected"?"open-submit":
     scenario=="open-no-transition"?"open-readback":"close-readback";
   const auto expectedMode=scenario=="success"?"bigpicture":
     scenario=="identity-missing"||scenario=="account-after-close"||scenario=="throws"?"unknown":
     scenario=="open-rejected"||scenario=="open-no-transition"?"desktop":"bigpicture";
   assert(result.value("launchStage",std::string{})==expectedStage);
   assert(result.value("launchMode",std::string{})==expectedMode);
   const unsigned expectedReads=scenario=="success"?3:scenario=="close-no-transition"?25:
     scenario=="open-no-transition"?50:scenario=="open-rejected"||scenario=="account-after-close"||scenario=="throws"?2:1;
   assert(reads==expectedReads); // Diagnostics add no mode or identity reads.
   auto detail=result;detail["elapsedMs"]=tick-100;detail["token"]="must-not-log";
   const auto logged=ymcc::deckydiag::record("connection-recovery",detail);
   assert(logged.at("launchStage")==result.at("launchStage"));
   assert(logged.at("launchMode")==result.at("launchMode"));
   assert(logged.at("elapsedMs")==tick-100&&!logged.contains("token"));
   std::cout<<"BigPicture diagnostic "<<scenario<<" "<<expectedStage<<" "<<expectedMode<<" reads="<<reads<<"\n";
   if(scenario=="identity-missing"){assert(sent.empty()&&!result.value("mutated",true));}
   else {assert(result.value("mutated",false));assert(result.value("uncertain",false)==(scenario!="success"));}
   assert(sent.size()<=2);if(!sent.empty())assert(sent[0]=="desktop");
   if(sent.size()==2)assert(sent[1]=="bigpicture");
   if(scenario=="off-after-close"||scenario=="account-after-close"||scenario=="close-no-transition")assert(sent.size()==1);
 }
 Json runtime={{"enabled",true},{"pid",0u},{"revision",1},{"phase","waiting-manual-steam"},{"reason","decky-context-already-loaded"}};
 Json args={{"reopenBigPicture",true},{"pid",session.pid},{"birth",std::to_string(session.created)},{"revision",1},{"modeEpoch","doc:1"}};
 assert(ymcc::deckyrepair::requestMatches(args,session,runtime,ymcc::deckyrepair::Origin::ReopenBigPicture));
 assert(!ymcc::deckyrepair::requestMatches(args,session,runtime,ymcc::deckyrepair::Origin::FinalCleanup));
 unsigned reservedCount=0,reopenCount=0,preflightCount=0;bool consent=true;
 Json mode={{"known",true},{"running",true},{"pid",session.pid},{"birth",std::to_string(session.created)},
   {"mode","bigpicture"},{"modeEpoch","doc:1"},{"observedAccount",session.account},{"deckyAccountWatchAvailable",true},
   {"deckyFrontendLoaded",true},{"deckyEmptyFrontend",{{"protocol",1},{"frontendBuildId",std::string(64,'a')},{"loaderVersion","v3.3.1"}}}};
 ymcc::deckyrepair::Dependencies deps;deps.session=[&]{return session;};deps.runtime=[&]{return runtime;};
 deps.mode=deps.freshMode=[&](const auto&){return mode;};deps.installed=[]{return std::string(64,'a');};deps.loader="v3.3.1";
 deps.now=[]{return 100ull;};deps.exiting=[] {return false;};deps.consent=[&](const auto&){return consent;};deps.cleanupAllowed=deps.consent;
 deps.preflight=[&]{++preflightCount;return std::string{};};deps.reserveCleanup=[&](const auto&){++reservedCount;return true;};
 deps.reopenBigPicture=[&](const auto& current){++reopenCount;assert(current());return Json{{"ok",true},{"mutated",true}};};
 ymcc::deckyrepair::ManualRepair repair;
 assert(repair.request(args,deps,ymcc::deckyrepair::Origin::ReopenBigPicture).value("ok",false));
 assert(reservedCount==1&&reopenCount==1&&preflightCount==2); // One admission recheck, no hashing in mode waits.
 assert(!repair.request(args,deps,ymcc::deckyrepair::Origin::ReopenBigPicture).value("ok",false));
 assert(reservedCount==1&&reopenCount==1);
 // Original higher-tier consumer: a dead/missing old plugin must not stop
 // the native close/open route before its first mode command.
 const auto goodMode=mode;
 unsigned missingResidueCycles=0,refusedInvalidModes=0;
 for(unsigned variant=0;variant<3;++variant){
   mode=goodMode;mode.erase("deckyEmptyFrontend");mode.erase("deckyUpdateResidue");
   if(variant==1)mode["deckyFrontendLoaded"]=false;
   if(variant==2)mode["deckyUpdateResidue"]={{"loaderVersion","unavailable"}};
   unsigned reservations=0;std::vector<std::string> commands;unsigned long long manualRouteTick=100;
   deps.reserveCleanup=[&](const auto&){++reservations;return true;};
   deps.reopenBigPicture=[&](const auto& current){
     return ymcc::deckyrepair::bigpicture::run(session,current,[&]{auto observed=mode;
       if(!commands.empty())observed["mode"]=commands.back();return observed;},
       [&](const std::string& value){commands.push_back(value);return true;},[&]{return manualRouteTick;},[&]{manualRouteTick+=500;});
   };
   ymcc::deckyrepair::ManualRepair high;
   const auto reopened=high.request(args,deps,ymcc::deckyrepair::Origin::ReopenBigPicture);
   assert(reopened.value("ok",false)&&reopened.value("reason",std::string{})=="decky-bigpicture-reopened");
   assert(reservations==1&&commands==std::vector<std::string>({"desktop","bigpicture"}));
   assert(!high.request(args,deps,ymcc::deckyrepair::Origin::ReopenBigPicture).value("ok",true)&&commands.size()==2);
   // The lower JS rebuild still needs its original exact plugin target.
   auto page=args;page.erase("reopenBigPicture");page["finalCleanup"]=true;
   ymcc::deckyrepair::ManualRepair lower;
   assert(lower.request(page,deps,ymcc::deckyrepair::Origin::FinalCleanup).value("reason",std::string{})=="decky-repair-context-changed");
   assert(reservations==1&&commands.size()==2);++missingResidueCycles;
 }
 for(unsigned variant=0;variant<6;++variant){
   mode=goodMode;mode.erase("deckyEmptyFrontend");mode.erase("deckyUpdateResidue");
   if(variant==0)mode["mode"]="desktop";
   if(variant==1)mode["pid"]=session.pid+1;
   if(variant==2)mode["birth"]=std::to_string(session.created+1);
   if(variant==3)mode["deckyAccountWatchAvailable"]=false;
   if(variant==4)mode["modeEpoch"]="different-document:1";
   if(variant==5)mode["known"]=false;
   unsigned reservations=0,commands=0;
   deps.reserveCleanup=[&](const auto&){++reservations;return true;};
   deps.reopenBigPicture=[&](const auto&){++commands;return Json{{"ok",true},{"mutated",true}};};
   ymcc::deckyrepair::ManualRepair refused;
   assert(!refused.request(args,deps,ymcc::deckyrepair::Origin::ReopenBigPicture).value("ok",true));
   assert(reservations==0&&commands==0);++refusedInvalidModes;
 }
 mode=goodMode;
 std::cout<<"Missing plugin BPM entry: recovered="<<missingResidueCycles<<" invalidRefused="<<refusedInvalidModes<<" realSteamActions=0\n";
 // Field 2026-11-12: empty v3.3.1 Loader keeps the old bridge after an upgrade.
 mode=goodMode;mode["deckyEmptyFrontend"]["frontendBuildId"]=std::string(64,'c');
 Json serializedEmptyAction;unsigned emptyReservations=0,emptyDispatches=0;
 deps.reserveCleanup=[&](const auto&){++emptyReservations;return true;};
 deps.dispatch=[&](const auto&,const Json& action,const auto& admit){assert(admit());serializedEmptyAction=action;++emptyDispatches;return Json{{"ok",true},{"mutated",true}};};
 auto emptyArgs=args;emptyArgs.erase("reopenBigPicture");emptyArgs["finalCleanup"]=true;
 ymcc::deckyrepair::ManualRepair emptyRepair;const auto emptyResult=emptyRepair.request(emptyArgs,deps,ymcc::deckyrepair::Origin::FinalCleanup);
 assert(emptyResult.value("ok",false)&&emptyReservations==1&&emptyDispatches==1);
 assert(serializedEmptyAction.at("installedFrontendBuild")==std::string(64,'a')&&serializedEmptyAction.at("expectedFrontendBuild").is_null()&&serializedEmptyAction.at("expectedEmptyBridgeBuild")==std::string(64,'c'));
 std::cout<<"EMPTY_BRIDGE_ACTION "<<serializedEmptyAction.dump()<<"\n";
 mode=goodMode;
 Json reserved={{"schemaVersion",1},{"steamSession",ymcc::deckyupdate::ticket(session)}};
 Json document={{"deckyBigPictureRecovery",reserved}};
 assert(!ymcc::deckyrepair::cleanupDocumentAvailable(document,session,{},"deckyBigPictureRecovery"));
 assert(ymcc::deckyrepair::cleanupDocumentAvailable(document,session)); // Independent lower-tier fuse.
 std::cout<<"Decky ladder + observed Big Picture cycle PASS: nine bounded scenarios; no Steam/OS mode commands\n";
 healthObservationFaultCase();
 std::cout<<"Decky outage recovery PASS: capped retry, coalescing, cancel, health reset; no real process IO\n";
}
