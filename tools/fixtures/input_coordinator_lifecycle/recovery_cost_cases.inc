// Complete unmodified production recovery-maintenance function, isolated inputs.
// Timed regions contain only an indirect function call loop plus the original
// function. State setup, warmup, JSON and assertions are outside those regions.
// Monotonic OS clock and TransactionMutex remain the existing real fixture ones.
// The protection policy clock is deliberately fixed for the active inspection
// input, so this measures work per inspection, not real-time deadline behavior.
namespace fixtureRecoveryCost {
constexpr unsigned trials=15, calls=30000, warmupCalls=128;
using Function=void(*)();
__declspec(noinline) static void emptyLoopTarget(){std::atomic_signal_fence(std::memory_order_seq_cst);}
static unsigned long long fileTime(FILETIME value){return (static_cast<unsigned long long>(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
static unsigned long long cpuTime(){
 FILETIME created{},exited{},kernel{},user{};
 if(!GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user))throw std::runtime_error("recovery-cost GetThreadTimes unavailable");
 return fileTime(kernel)+fileTime(user);
}
struct Sample {LONGLONG wallTicks=0,qpcFrequency=0;unsigned long long threadCycles=0,threadCpu100ns=0;double wallNs=0;};
static Sample sample(Function function,unsigned iterations=calls){
 if(iterations==0||iterations>calls)throw std::runtime_error("CPU sample call budget invalid");
 LARGE_INTEGER frequency{},begin{},end{};unsigned long long cyclesBegin=0,cyclesEnd=0;
 if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)throw std::runtime_error("recovery-cost QPC frequency unavailable");
 Function volatile target=function;
 const auto cpuBefore=cpuTime();
 if(!QueryPerformanceCounter(&begin)||!QueryThreadCycleTime(GetCurrentThread(),&cyclesBegin))throw std::runtime_error("recovery-cost begin counters unavailable");
 std::atomic_signal_fence(std::memory_order_seq_cst);
 for(unsigned i=0;i<iterations;++i)target();
 std::atomic_signal_fence(std::memory_order_seq_cst);
 if(!QueryThreadCycleTime(GetCurrentThread(),&cyclesEnd)||!QueryPerformanceCounter(&end))throw std::runtime_error("recovery-cost end counters unavailable");
 const auto cpuAfter=cpuTime();
 if(end.QuadPart<=begin.QuadPart||cyclesEnd<cyclesBegin||cpuAfter<cpuBefore)throw std::runtime_error("recovery-cost nonmonotonic counters");
 Sample result;result.wallTicks=end.QuadPart-begin.QuadPart;result.qpcFrequency=frequency.QuadPart;
 result.wallNs=static_cast<double>(result.wallTicks)*1.0e9/static_cast<double>(result.qpcFrequency);
 result.threadCycles=cyclesEnd-cyclesBegin;result.threadCpu100ns=cpuAfter-cpuBefore;return result;
}
static json flags(){
 const auto v=g_virtualStartProtection.view(startProtectionClock);
 std::size_t queueDepth=0;{std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);queueDepth=g_inputHostLifecycleWorkQueue.size();}
 return {{"verificationPending",g_inputHostSwitchVerificationPending.load()},
 {"executionReturnPending",g_inputHostSwitchExecutionReturnPending.load()},
 {"switchRecoveryPending",g_inputHostSwitchRecoveryPending.load()},
 {"recoveryPending",g_inputHostRecoveryPending.load()},
 {"attempts",g_inputHostRecoveryEpisodeAttempts.load()},{"terminal",g_inputHostRecoveryTerminalLogged.load()},
 {"intentReset",g_inputHostRecoveryIntentReset.load()},{"fallback",g_inputHostRecoveryFallbackState.load()},
 {"tuplePending",g_inputHostTupleHealPending.load()},{"queueDepth",queueDepth},
 {"workRequested",g_inputHostLifecycleWorkRequested.load()},{"workInFlight",g_inputHostLifecycleWorkInFlight.load()},
 {"hostPresent",g_inputHostProcess!=nullptr},{"prepared",g_inputHostPrepared},
 {"journalActive",g_hidHideVisibilityJournal.active},{"quarantined",g_inputHostReleaseQuarantined},
 {"requestId",v.requestId},{"phase",v.phase},{"remainingMs",v.remainingMs},{"verificationRemainingMs",v.verificationRemainingMs},
 {"externalPolicyPause",g_externalPriorityVirtualSuspended.load()},{"healthTracking",g_inputHostRecoveryHealthWasTracking}};
}
static bool prepare(const std::string& state){
 seed();reg_fs();g_inputCaptureStop=false;g_inputHostExitFence=false;g_exitRequested=false;
 g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;
 g_inputHostRequestedPersonaMatchesBound=true;g_shortcutRecordingActive=false;
 g_externalPriorityVirtualSuspended=false;g_physicalReleaseWindowStage=0;g_physicalReleaseWindowDeadline=0;
 {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;g_inputHostLifecycleWorkInFlight=false;}
 if(state=="terminal-only"||state=="terminal-and-attempts")g_inputHostRecoveryTerminalLogged=true;
 if(state=="attempts-only"||state=="terminal-and-attempts")g_inputHostRecoveryEpisodeAttempts=1;
 if(state=="fallback-available")g_inputHostRecoveryFallbackState=2;
 if(state=="fallback-unproven")g_inputHostRecoveryFallbackState=3;
 if(state=="active-verification-paused"){
  const auto admission=g_virtualStartProtection.admit("steamdeck",startProtectionClock);
  if(!admission.accepted||!admission.requestId)return false;
  g_inputHostStartProtectionBoundTicket=admission.requestId;g_externalPriorityVirtualSuspended=true;
  inputHostStartProtectionFinish(admission.requestId,"success","offline-recovery-cost-active-inspection");
  const auto v=g_virtualStartProtection.view(startProtectionClock);
  if(v.phase!="verifying"||!v.verificationPending||g_inputHostSwitchVerificationPending.load()!=admission.requestId)return false;
 }
 return g_inputHostProcess==nullptr&&!g_inputHostPrepared&&!g_hidHideVisibilityJournal.active&&!g_inputHostReleaseQuarantined&&
  !g_inputHostSwitchExecutionReturnPending.load()&&!g_inputHostSwitchRecoveryPending.load()&&!g_inputHostRecoveryPending.load()&&
  !g_inputHostTupleHealPending.load()&&!g_inputHostLifecycleWorkRequested.load();
}
} // namespace fixtureRecoveryCost
static void recoveryMaintenanceCostCases(){
 using namespace fixtureRecoveryCost;
 const std::vector<std::string> states={"loop-baseline","all-clear","terminal-only","attempts-only","terminal-and-attempts","fallback-available","fallback-unproven","active-verification-paused"};
 for(unsigned trial=0;trial<trials;++trial)for(unsigned step=0;step<states.size();++step){
  const auto& selected=states[(trial*3+step)%states.size()];
  cases.push_back({{"name","recovery-cost/"+selected+"/trial-"+std::to_string(trial)},
   {"kind","controlled-recovery-cpu-sample"},{"state",selected},{"trial",trial},{"calls",calls},{"warmupCalls",warmupCalls},
   {"timedFunction",selected=="loop-baseline"?"emptyLoopTarget":"inputHostRecoveryMaintenanceTick"},
   {"realUninstrumentedTransactionMutex",true},{"lockWaitIndividuallyMeasured",false},{"protectionClockFixed",true}});
  const bool initialized=prepare(selected);check(initialized,"CPU input state established without an owned target or queued return duty");
  if(!initialized)throw std::runtime_error("recovery-cost input establishment failed");
  const Function target=selected=="loop-baseline"?emptyLoopTarget:inputHostRecoveryMaintenanceTick;
  for(unsigned n=0;n<warmupCalls;++n)target();
  const auto before=flags();const auto eventsBefore=fixtureEvents.size(),traceBefore=g_startEntrySeam.trace.size();
  const auto ioBefore=forbiddenIo,xinputBefore=inertXinputReads;
  const auto inspectionsBefore=ymcc::inputHost::switchVerificationRuntime::observation.checks;
  const auto measured=sample(target);
  const auto inspectionsAfter=ymcc::inputHost::switchVerificationRuntime::observation.checks;
  const auto after=flags();
  const bool noIo=forbiddenIo==ioBefore&&inertXinputReads==xinputBefore&&fixtureEvents.size()==eventsBefore&&g_startEntrySeam.trace.size()==traceBefore;
  const bool stable=before==after;const auto inspectionDelta=inspectionsAfter-inspectionsBefore;
  check(measured.wallTicks>0&&measured.qpcFrequency>0&&measured.wallNs>0,"CPU counters cover the complete bounded function-call loop");
  check(noIo,"CPU steady sample performs no device IO, new log, queue, Stop or Start transaction");
  check(stable&&inspectionDelta==(selected=="active-verification-paused"?calls:0),"CPU sample keeps its declared state and executes each required active inspection");
  cases.back()["sample"]={{"wallTicks",measured.wallTicks},{"qpcFrequency",measured.qpcFrequency},{"wallNs",measured.wallNs},
   {"threadCycles",measured.threadCycles},{"threadCpu100ns",measured.threadCpu100ns}};
  cases.back()["before"]=before;cases.back()["after"]=after;cases.back()["noIo"]=noIo;cases.back()["stateStable"]=stable;cases.back()["inspectionDelta"]=inspectionDelta;
 }
 g_externalPriorityVirtualSuspended=false;
}