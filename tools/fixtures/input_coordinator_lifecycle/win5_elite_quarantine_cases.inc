static constexpr char win5EliteQuarantineCase[] = "GPD/Win5/elite-quarantine-then-explicit-on/exit";

static void fullPipeWin5EliteQuarantineCase(){
 using namespace personaLifecycle140;
 namespace queued=ymcc::inputHost::queuedPlayerStart;
 const ControlledDmi dmi={"GPD","G1618-05","HandheldCompanion.Devices.GPDWin5","/physicalModels/0"};
 prepare(2,win5EliteQuarantineCase,&dmi);
 g_machineIdentity.deviceClass=dmi.declaredDeviceClass;
 g_startupInputState=ymcc::StartupInputState::Ready;
 g_hardwareWriteGate=true;
 const auto birthSpawns=fullPipeSpawnCount;
 fullPipeFault="output-stop-once";
 startFirst(2,"elite");
 fixtureWin5OriginalOnEdge();
 const auto oldTicket=g_virtualStartProtection.view(startProtectionClock).requestId;
 const auto oldEpoch=g_inputHostEpoch;
 const auto oldRun=g_inputHostRunId;
 const auto oldOwner=inputHostTargetReturnCaptureOwner(g_inputHostProcess);
 HANDLE oldHandle=nullptr;
 require(oldOwner.has_value()&&oldEpoch&&!oldRun.empty()&&fullPipeSpawnCount==birthSpawns+1&&
  DuplicateHandle(GetCurrentProcess(),g_inputHostProcess,GetCurrentProcess(),&oldHandle,0,FALSE,DUPLICATE_SAME_ACCESS),
  "Win5 captures the exact old private Elite child before creating any return debt");
 ymcc::targetReturn::fileDetail::Handle oldObserver(oldHandle);
 struct RestoreProbe {
  ~RestoreProbe(){fixtureRuntimeRecoveryOsEnabled=false;fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureClassifiedReturnReleasedEpoch=0;}
 } restoreProbe;
 const auto oldSettled=[&](){std::lock_guard<InputHostMutex> lock(g_inputHostMx);return inputHostTargetReturnRuntimeSettledLocked(oldRun,oldEpoch);};
 require(!oldSettled(),"old Elite owns an actual unsettled target origin before OFF");
 fixtureRuntimeRecoveryOsEnabled=true;
 fixtureRuntimeRecoveryOldHost=oldHandle;
 fixtureRuntimeRecoveryOldOwner=oldOwner;
 fixtureRuntimeRecoveryCensus="unknown";
 const auto playerStart=[&](){return g_virtualStartProtection.view(inputHostStartProtectionNow());};
 const auto document=[](){return json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));};
 const auto revision=[](const json& current){return current["input"]["revision"].get<int>()+1;};
 const auto commit=[&](const json& before,const json& next){
  require(writeTarget(before,next).value("ok",false),"Win5 user intent is admitted by the original settings writer");
  inputCaptureRefreshRealStickSettings(true);
  fixtureWin5OriginalOnEdge();
 };

 const auto oldOn=document(),off=targetDocument("disabled",revision(oldOn));
 commit(oldOn,off);
 const auto offTicket=playerStart().requestId;
 require(offTicket&&offTicket!=oldTicket&&g_inputHostLifecycleWorkRequested.load(),"explicit OFF owns an independent original queued cleanup");
 fullPipeDrainQueuedLifecycle();
 const auto oldChild=fullPipeChildOutcome();
 cases.back()["oldHost"]={{"pid",oldOwner->pid},{"birth",oldOwner->creationTime},{"epoch",oldEpoch},
  {"runId",oldRun},{"requestId",oldTicket},{"receipt",oldChild}};
 require(oldChild.value("processId",0U)==oldOwner->pid&&oldChild.value("nativeObservedProcessExit",false)&&
  oldChild.value("nativeObserverCloseSucceeded",false)&&oldChild.value("faultConsumed",false)&&
  !g_inputHostProcess&&g_inputHostReleaseQuarantined&&!oldSettled(),
  "actual OFF loses confirmed old return through controlled leaves; natural exit alone is not settlement");
 const auto probeCalls=fixtureRuntimeRecoveryCensusCalls;
 fullPipeQuarantineProbe();
 require(fixtureRuntimeRecoveryCensusCalls>probeCalls&&g_inputHostReleaseQuarantined&&!oldSettled(),
  "incomplete readback reaches the real census and cannot settle the exact old origin");

 const auto failedOn=targetDocument("elite",revision(off));
 fullPipeWaitForPlayerLock(off,failedOn);
 commit(off,failedOn);
 const auto admitted=playerStart();
 require(admitted.requestId&&admitted.requestId!=offTicket&&admitted.requestId!=oldTicket&&
  admitted.phase=="pending"&&admitted.locked&&admitted.remainingMs==fullPipeUserMaxMs&&
  !g_inputHostStartBlocked&&g_inputHostReleaseQuarantined&&!oldSettled(),
  "actual OFF to ON rearm clears only the start hold, not the exact old return obligation");
 const auto queueOn=[&](const ymcc::VirtualStartProtection::View& view){
  (void)fullPipeReadPhysicalSourceForElite(false,true);
  fixtureGpdOriginalBusAdmission();
  std::string wire;
  {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);
   if(g_inputHostLifecycleWorkQueue.size()==1)wire=fixtureLifecycleWorkReason(g_inputHostLifecycleWorkQueue.front());}
  queued::Work work;
  require(queued::decode(wire,work)&&work.bound&&work.reason=="initial-target-admission"&&work.persona=="elite"&&
   work.request==view.requestId&&work.power==currentPowerGeneration()&&
   work.deadline==inputHostStartProtectionNow()+view.remainingMs,"actual BUS binds only the original admitted Elite ticket, power and deadline");
  return work;
 };
 const auto spawns=fullPipeSpawnCount;
 const auto sequence=g_inputHostSequence;
 const auto eventBegin=fixtureEvents.size();
 const auto rejectedWork=queueOn(admitted);
 fullPipeFault="none";
 {FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
 std::size_t consumeAt=fixtureEvents.size(),rejectAt=fixtureEvents.size();
 unsigned consumes=0,rejections=0,started=0,prepares=0,ready=0;
 for(auto i=eventBegin;i<fixtureEvents.size();++i){
  const auto& e=fixtureEvents[i];const auto name=e.value("event",std::string{});const auto& d=e.at("detail");
  if(name=="input-host-player-start-work"&&d.value("action",std::string{})=="consuming"&&
     d.value("requestId",0ULL)==rejectedWork.request&&d.value("powerGeneration",0ULL)==rejectedWork.power&&
     d.value("originalDeadline",0ULL)==rejectedWork.deadline){++consumes;consumeAt=i;}
  if(name=="input-host-start-blocked"&&d.value("reason",std::string{})=="release-quarantine"){++rejections;rejectAt=i;}
  if(name=="input-host-started")++started;
  if(name=="input-host-xbox-startup"&&d.value("phase",std::string{})=="xinput-ready")++ready;
  if(name=="input-host-command-result"&&d.value("command",std::string{})=="PREPARE_TARGET")++prepares;
 }
 cases.back()["blocked"]={{"requestId",rejectedWork.request},{"power",rejectedWork.power},{"deadline",rejectedWork.deadline},
  {"consumes",consumes},{"releaseQuarantine",rejections},{"newHosts",fullPipeSpawnCount-spawns},{"preparedCommands",prepares},{"ready",ready},
  {"protection",fullPipeProtectionState()},{"transportFault",g_inputHostTransportFault}};
 cases.back()["blocked"]["protection"]["fallbackAttempts"]=playerStart().fallbackAttempts;
 require(consumes==1&&rejections==1&&consumeAt<rejectAt&&!started&&!prepares&&!ready&&fullPipeSpawnCount==spawns&&
  g_inputHostSequence==sequence&&!g_inputHostProcess&&g_inputHostReleaseQuarantined&&!oldSettled(),
  "consumed request reaches actual release-quarantine branch before any Host or pipe command");
 const auto candidate=playerStart();
 require(candidate.requestId==rejectedWork.request&&candidate.locked&&candidate.phase=="verifying"&&
  candidate.candidateOutcome=="failure"&&candidate.verificationRemainingMs==10000&&!candidate.fallbackAttempts&&
  inputHostStartProtectionNow()+candidate.remainingMs==rejectedWork.deadline,
  "actual refusal forms a failure candidate within the original ticket, without renewing or falling back");
 advance(9999);fullPipeVerificationStep();
 require(playerStart().locked&&!finalCount(rejectedWork.request,"failure"),"Win5 refusal cannot unlock before its original inspection window");
 advance(1);fullPipeVerificationStep();fullPipeVerificationStep();
 const auto failed=playerStart();
 require(failed.requestId==rejectedWork.request&&failed.phase=="failure"&&!failed.locked&&!failed.fallbackAttempts&&
  finalCount(rejectedWork.request,"failure")==1&&!finalCount(rejectedWork.request,"success")&&fullPipeSpawnCount==spawns,
  "Win5 refusal publishes exactly one failed final with no Ready or Host recreation");
 cases.back()["failedFinal"]={{"protection",fullPipeProtectionState()},
  {"failures",finalCount(rejectedWork.request,"failure")},{"successes",finalCount(rejectedWork.request,"success")}};
 cases.back()["failedFinal"]["protection"]["fallbackAttempts"]=failed.fallbackAttempts;

 // Hold OFF before allowing absence proof, so no policy ON may inherit the failed request.
 const auto between=targetDocument("disabled",revision(failedOn));
 commit(failedOn,between);fullPipeDrainQueuedLifecycle();
 require(!oldSettled()&&!g_inputHostProcess&&g_inputHostReleaseQuarantined&&fullPipeSpawnCount==spawns,
  "cleanup-only OFF does not pretend the unknown old endpoint is absent");
 fixtureClassifiedReturnReleasedEpoch=oldEpoch;
 fixtureRuntimeRecoveryCensus="absent";
 const auto priorMask=g_xinputConnectedMask;
 {const ULONGLONG probeNow=GetTickCount64();const int probeSrc=kGpReadBus;const bool probeCostOn=false;
  const auto diagnosticReadPower=currentPowerGeneration();const auto diagnosticReadTopology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
  #include "xinput_discovery.inc"
 }
 (void)fresh(2);
 cases.back()["oldReleaseDiscovery"]={{"priorMask",priorMask},{"freshMask",g_xinputConnectedMask},{"sourceMask",g_xinputPhysicalSourceMask}};
 const auto absentCensusBefore=fixtureRuntimeRecoveryCensusCalls;
 fullPipeQuarantineProbe();
 require(fixtureRuntimeRecoveryCensusCalls>absentCensusBefore&&oldSettled()&&!g_inputHostReleaseQuarantined&&!g_inputHostProcess&&
  !g_hidHideVisibilityJournal.active&&fullPipeSpawnCount==spawns,
  "fresh complete old-target census settles actual old duty without a replacement Start");
 require(oldObserver.close(),"exact old writer's additional process observer closes after settlement");
 cases.back()["oldReturn"]={{"settled",oldSettled()},{"epoch",oldEpoch},{"runId",oldRun},{"newHosts",fullPipeSpawnCount-spawns}};
 fixtureRuntimeRecoveryOsEnabled=false;fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureClassifiedReturnReleasedEpoch=0;
 const auto nextOn=targetDocument("elite",revision(between));
 fullPipeWaitForPlayerLock(between,nextOn);
 commit(between,nextOn);
 const auto next=queueOn(playerStart());
 require(next.request!=rejectedWork.request&&next.request!=oldTicket,"post-return ON has a distinct explicit player ticket");
 {FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
 const auto nextEpoch=g_inputHostEpoch;
 const auto nextRun=g_inputHostRunId;
 const auto nextOwner=inputHostTargetReturnCaptureOwner(g_inputHostProcess);
 ULONG client=0;
 require(nextOwner.has_value()&&nextEpoch!=oldEpoch&&nextRun!=oldRun&&fullPipeSpawnCount==spawns+1&&
  fullPipeChildObservers.size()==1&&GetNamedPipeClientProcessId(g_inputHostServerPipe,&client)&&client==nextOwner->pid,
  "only the new request starts one new exact Host through the actual private pipe");
 success(2,"elite",next.request);
 const auto done=playerStart();
 require(done.requestId==next.request&&done.phase=="success"&&!done.locked&&
  g_inputHostRunId==nextRun&&g_inputHostEpoch==nextEpoch&&GetProcessId(g_inputHostProcess)==client,
  "current-frame acceptance settles the new ticket on the same replacement Host");
 inputHostRaiseExitFence("fixture-win5-elite-quarantine-exit");g_exitRequested=true;g_inputCaptureStop=true;
 inputHostStop("app-exit");
 const auto nextChild=fullPipeChildOutcome();
 fullPipeDrainQueuedLifecycle();
 const auto finalTuple=nextChild.value("stateAfterFinalCleanup",json::object()).value("tuple",json::object());
 cases.back()["replacement"]={{"requestId",next.request},{"pid",client},{"epoch",nextEpoch},{"runId",nextRun},
  {"successes",finalCount(next.request,"success")},{"failures",finalCount(next.request,"failure")},{"receipt",nextChild}};
 require(nextChild.value("processId",0U)==client&&nextChild.value("nativeObservedProcessExit",false)&&
  nextChild.value("nativeObservedExitCode",99)==0&&nextChild.value("nativeObserverCloseSucceeded",false)&&
  nextChild.value("finalCleanupOk",false)&&!nextChild.value("hasController",true)&&!nextChild.value("hasContext",true)&&
  finalTuple.value("RunId",std::string{})==nextRun&&finalTuple.value("Epoch",0ULL)==nextEpoch&&
  !g_inputHostProcess&&!g_inputHostServerPipe&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined&&
  !g_hidHideVisibilityJournal.active&&fullPipeChildObservers.empty()&&fullPipeSpawnCount==spawns+1&&forbiddenIo==0,
  "actual exit fences and destroys the exact replacement Host and returns native ownership");
 cases.back()["passed"]=true;
 fixtureFullPipe=false;
}
