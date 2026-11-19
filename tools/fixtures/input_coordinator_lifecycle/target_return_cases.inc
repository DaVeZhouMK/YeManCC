// Actual target-return production hooks run in main.inert.cpp. These functions
// only observe private record I/O and drive the existing real coordinator path.
namespace fixtureTargetReturn {
static void fixtureLegacyPerformSwitchExecutionReturn(std::uint64_t ticket) {
    using namespace ymcc::inputHost::switchExecutionReturnRuntime;
    std::uint64_t power=0,deadline=0;
    {
        std::lock_guard<InputHostMutex> lock(g_inputHostMx);
        if(!ymcc::inputHost::switchExecutionReturnRuntime::state.pending || !ymcc::inputHost::switchExecutionReturnRuntime::state.queued || ymcc::inputHost::switchExecutionReturnRuntime::state.requestId!=ticket)return;
        ymcc::inputHost::switchExecutionReturnRuntime::state.queued=false;
        if(!currentLocked()){retireLocked("queued-request-retired");return;}
        if(!cleanLocked() || pausedLocked() || (!ymcc::inputHost::switchExecutionReturnRuntime::state.cold && !ymcc::inputHost::switchExecutionReturnRuntime::state.returned))return;
        if(ymcc::inputHost::switchExecutionReturnRuntime::state.cold && !coldPredecessorsReturnedLocked())return;
        if(ymcc::inputHost::switchExecutionReturnRuntime::state.cold){
            if(ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses>=3){retireLocked("return-observations-already-exhausted");return;}
            const bool hasCold=!inputHostTargetReturnHistorySettledLocked();
            if(hasCold) {
                const auto environment=ymcc::inputHost::targetReturnRuntime::ColdEnvironment{ticket,ymcc::inputHost::switchExecutionReturnRuntime::state.power,ymcc::inputHost::switchExecutionReturnRuntime::state.persona};
                if(!environment.current())return;
            }else if(currentProofPausedLocked())return;
            // Both phases share the original deadline and existing three passes.
            const auto oldDeadline=g_inputHostSwitchRecoveryStartDeadline;
            const auto oldRequest=g_inputHostSwitchRecoveryStartRequestId,oldPower=g_inputHostSwitchRecoveryStartPower;
            struct RestoreCurrentReturn {
                std::uint64_t deadline,request,power;
                ~RestoreCurrentReturn(){g_inputHostSwitchRecoveryStartDeadline=deadline;
                    g_inputHostSwitchRecoveryStartRequestId=request;g_inputHostSwitchRecoveryStartPower=power;}
            } restore{oldDeadline,oldRequest,oldPower};
            g_inputHostSwitchRecoveryStartDeadline=oldDeadline && oldDeadline<ymcc::inputHost::switchExecutionReturnRuntime::state.deadline?oldDeadline:ymcc::inputHost::switchExecutionReturnRuntime::state.deadline;
            g_inputHostSwitchRecoveryStartRequestId=ticket;g_inputHostSwitchRecoveryStartPower=ymcc::inputHost::switchExecutionReturnRuntime::state.power;
            const auto ioDeadline=g_inputHostSwitchRecoveryStartDeadline;
            const bool flushed=inputHostTargetReturnFlushCurrentLocked(ticket,ymcc::inputHost::switchExecutionReturnRuntime::state.power,ymcc::inputHost::switchExecutionReturnRuntime::state.persona,ioDeadline);
            if(!currentLocked() || inputHostStartProtectionNow()>=ioDeadline){retireLocked("current-store-context-retired");return;}
            if(pausedLocked() || currentProofPausedLocked())return;
            auto result=ymcc::targetReturn::coldExecution::Result{
                ymcc::targetReturn::coldExecution::State::Clean,ymcc::targetReturn::coldExecution::Reason::Complete};
            if(!flushed)result={ymcc::targetReturn::coldExecution::State::Deferred,
                ymcc::targetReturn::coldExecution::Reason::OriginCommitFailed};
            else if(hasCold)result=inputHostTargetReturnColdPassLocked(ticket,ymcc::inputHost::switchExecutionReturnRuntime::state.power,ymcc::inputHost::switchExecutionReturnRuntime::state.persona);
            if(!currentLocked() || inputHostStartProtectionNow()>=ioDeadline){retireLocked("cold-observation-context-retired");return;}
            if(pausedLocked() || currentProofPausedLocked())return;
            if(!result.clean()){
                if(result.reason!=ymcc::targetReturn::coldExecution::Reason::ContextRetired)++ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses;
                if(ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses>=3){
                    retireLocked("cold-return-bounded-observations-exhausted");
                    g_inputHostFaultHoldsStart.store(true,std::memory_order_release);
                    inputHostStartProtectionFinish(ticket,"failure","cold-return-still-unproven");
                }else ymcc::inputHost::switchExecutionReturnRuntime::state.nextObservation=inputHostStartProtectionNow()+1000;
                return;
            }
            ymcc::inputHost::switchExecutionReturnRuntime::state.returned=true;
            if(ymcc::inputHost::switchExecutionReturnRuntime::state.persona=="disabled"){
                retireLocked("cold-owned-return-execution-complete");
                inputHostStartProtectionFinish(ticket,"success","cold-owned-return-execution-complete");
                return;
            }
        }
        const auto now=inputHostStartProtectionNow();
        const auto view=g_virtualStartProtection.view(now);
        if(view.remainingMs>(std::numeric_limits<std::uint64_t>::max)()-now)return;
        power=ymcc::inputHost::switchExecutionReturnRuntime::state.power;deadline=now+view.remainingMs;
        // Exact return was proven. Claim only this remaining Start, once, on the
        // serialized owner; neither a second Stop nor fallback reservation occurs.
        ymcc::inputHost::switchExecutionReturnRuntime::state.pending=false;ymcc::inputHost::switchExecutionReturnRuntime::state.queued=false;
        g_inputHostSwitchExecutionReturnPending.store(false,std::memory_order_release);
        if(!ymcc::inputHost::switchExecutionReturnRuntime::state.cold){g_inputHostStartBlocked=false;g_inputHostFaultHoldsStart.store(false,std::memory_order_release);}
        logLocked("return-start","remaining-normal-start-claimed");
    }
    // Reuse the protected Start deadline context; it grants no fallback quota.
    const auto oldDeadline=g_inputHostSwitchRecoveryStartDeadline;
    const auto oldRequest=g_inputHostSwitchRecoveryStartRequestId,oldPower=g_inputHostSwitchRecoveryStartPower;
    struct Restore {std::uint64_t deadline,request,power;~Restore(){g_inputHostSwitchRecoveryStartDeadline=deadline;
        g_inputHostSwitchRecoveryStartRequestId=request;g_inputHostSwitchRecoveryStartPower=power;}}
        restore{oldDeadline,oldRequest,oldPower};
    g_inputHostSwitchRecoveryStartDeadline=oldDeadline && oldDeadline<deadline?oldDeadline:deadline;
    g_inputHostSwitchRecoveryStartRequestId=ticket;g_inputHostSwitchRecoveryStartPower=power;
    const bool started=inputHostStart();
    recordStartupInputAttempt(started);
    if(started){ymccSlotHandoffSchedule();inputHostStampBuildSettle();}
    std::lock_guard<InputHostMutex> lock(g_inputHostMx);
    if(ymcc::inputHost::switchExecutionReturnRuntime::state.requestId==ticket)logLocked("return-start-result",started?"prepared-awaiting-current-source":"normal-start-failed",started);
}

static bool fixtureOriginalOwnedCleanupProvenLocked() {
    const bool msiReturnProven = !msiClawKnownBoardAuthority() ||
        !g_msiDinputReturnOwed.load(std::memory_order_acquire);
    return !g_inputHostProcess && !g_inputHostPrimaryThread && !g_inputHostServerPipe && inputHostPipeRetirementProvenLocked() && !g_inputHostHello &&
        !g_inputHostPrepared && !g_inputHostPrepareMayHaveReachedHost && !g_inputHostNeutralized && !g_inputHostActive &&
        !g_hidHideVisibilityJournal.active && !g_inputHostReleaseQuarantined &&
        !g_msiSteamDeckReleaseDebt.required() && !g_psEndpointOwnership.retirementRequired() && msiReturnProven &&
        inputHostTargetReturnHistorySettledLocked();
}

namespace duty=ymcc::targetReturn;
static duty::LoadResult disk(){return duty::load(fixtureTargetReturnRecordPath(),inputHostTargetReturnMachineBinding());}
static json summary(const duty::LoadResult& loaded){
 json out={{"loadState",static_cast<unsigned>(loaded.state)},{"reason",loaded.reason},{"win32Error",loaded.win32Error}};
 if(loaded.record){out["revision"]=loaded.record->revision;out["origins"]=json::array();for(const auto& item:loaded.record->obligations)out["origins"].push_back({{"transactionId",item.origin.transactionId},{"parentPid",item.origin.parent.pid},{"parentBirth",item.origin.parent.creationTime},{"hostPid",item.origin.host.pid},{"hostBirth",item.origin.host.creationTime},{"runId",item.origin.runId},{"epoch",item.origin.epoch},{"targetId",item.origin.targetId},{"persona",item.origin.persona},{"physicalRoot",item.origin.physicalRoot},{"targetMayExist",item.targetMayExist},{"hasAcceptedPin",item.acceptedPsPin.has_value()},{"paths",item.paths.size()}});}
 return out;
}
static bool current(const duty::Obligation& item){
 const auto parent=duty::process::capture(GetCurrentProcess());
 const auto host=duty::process::capture(g_inputHostProcess);
 return parent&&host&&duty::detail::sameProcess(item.origin.parent,*parent)&&duty::detail::sameProcess(item.origin.host,*host)&&
  item.origin.hostInstanceId==g_inputHostHostInstanceId&&item.origin.runId==g_inputHostRunId&&item.origin.epoch==g_inputHostEpoch&&
  item.origin.targetId==kInputHostTargetId&&item.origin.persona==inputHostBoundOrCurrentPersona();
}
static std::size_t currentCount(const duty::LoadResult& loaded){if(!loaded.record)return 0;return static_cast<std::size_t>(std::count_if(loaded.record->obligations.begin(),loaded.record->obligations.end(),[](const auto& item){return current(item);}));}
static unsigned commandCount(const json& child,const std::string& command){unsigned count=0;const auto requests=child.find("requests");if(requests!=child.end()&&requests->is_array())for(const auto& request:*requests)if(request.is_object()&&request.value("command",std::string{})==command)++count;return count;}
static void preparePcCase(const std::string& name){
 seed();fixtureFullPipe=true;fullPipeFault="none";fixturePcPowerCapabilities=true;
 g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="FIXTURE-PC";g_machineIdentity.product="unregistered-desktop";
 g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=0;g_xinputPhysicalSourceMask=0;g_xinputConnectedMask=0;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};
 g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 cases.push_back({{"name","TARGET-RETURN/PC/"+name},{"controlledFamily","unregistered ordinary PC"},{"realManagedChild",true},{"sourcePresent",false}});
 const auto on=targetDocument("dualsense-edge",0);check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"target return PC seeds an actual private settings request");inputCaptureRefreshRealStickSettings(true);
}
static duty::Record historical(){
 duty::Record record;record.machineBinding="fixture-machine";
 duty::Origin origin;origin.transactionId="historical-origin-108";origin.parent={600001,"132000000000000101"};origin.host={600002,"132000000000000102"};
 origin.hostInstanceId="historical-host-108";origin.runId="historical-run-108";origin.targetId=kInputHostTargetId;origin.persona="dualsense-edge";origin.epoch=77;
 duty::Record target,pinned;
 if(!duty::reserveTargetCreation(record,record.revision,origin,{true,true},target))throw std::runtime_error("fixture historical target transform failed");
 const duty::AcceptedPsPin pin{"USB\\VID_054C&PID_0DF2\\HISTORICAL108",{"HID\\VID_054C&PID_0DF2\\HISTORICAL108"},108};
 if(!duty::attachAcceptedPsPin(target,target.revision,origin,pin,pinned))throw std::runtime_error("fixture historical pin transform failed");
 return pinned;
}
static std::string encoded(const duty::Record& record){std::string text;if(!duty::encode(record,text))throw std::runtime_error("fixture record encode failed");return text;}

// Fixture observation reader: count one decoded command, not each observation
// around that command. Missing schema is a fixture failure, never zero traffic.
static unsigned decodedCommandCount(const json& child,const char* command) {
 const auto rows=child.find("observations");
 if(rows==child.end()||!rows->is_array())throw std::runtime_error("managed-command-observations-missing");
 return static_cast<unsigned>(std::count_if(rows->begin(),rows->end(),[&](const auto& row){
  return row.is_object()&&row.value("kind",std::string{})=="command-decoded"&&
   row.value("command",std::string{})==command;
 }));
}
// Only a fixture-simulated external repair may replace deliberate corrupt
// bytes. Product store must continue refusing unconfirmed destination history.
// Publish a known nonempty legacy duty through the existing private OS leaf.
static bool externalRepairKnownHistory(const duty::Record& history) {
 const auto file=fixtureTargetReturnRecordPath();
 const auto base=(fspath::path(g_yemanccDataDir)/L"target-return-private").wstring();
 if(file.rfind(base+L"\\",0)!=0&&file.rfind(base+L"/",0)!=0)
  throw std::runtime_error("external-repair-outside-private-case");
 if(history.obligations.empty())throw std::runtime_error("external-repair-must-retain-history");
 return sgWriteFileAtomic(file,encoded(history));
}

// Actual-consumer extension of TARGET-RETURN/PC/source-free-normal-complete-stop.
// Validated with real private-file failures and owned managed Host sessions.
// Uses original settings IPC, lifecycle queue, real managed child and real store.
// The only fault is a no-delete-sharing read handle on this case's private file.
static void commitFailureOffCase(bool persistent,const std::string& edge={}) {
 const std::string variant=std::string(persistent?"commit-denial-persistent":"commit-denial-transient")+(edge.empty()?"":"/"+edge);
 preparePcCase(std::string("source-free-normal-complete-stop/")+variant);
 cases.back()["subvariantOf"]="TARGET-RETURN/PC/source-free-normal-complete-stop";
 cases.back()["commitFaultKind"]="private-destination-no-delete-sharing";
 const auto spawns=fullPipeSpawnCount;
 const bool started=inputHostStart();
 check(started&&fullPipeSpawnCount==spawns+1&&g_inputHostProcess,
  "commit fault owns exactly one actual source-free managed target before fault injection");
 if(!started||!g_inputHostProcess)throw std::runtime_error("commit fault setup did not create owned Host");
 const auto running=disk();
 check(running.state==duty::LoadState::Valid&&running.record&&currentCount(running)==1,
  "commit fault starts with one actual persisted current target duty");
 if(!running.record)throw std::runtime_error("commit fault setup has no readable record");
 std::vector<duty::Origin> oldCurrent;
 for(const auto& item:running.record->obligations)if(current(item))oldCurrent.push_back(item.origin);
 const auto runningBytes=encoded(*running.record);

 // Source-free PREPARE deliberately has no current input receipt. Reuse the
 // original player-lock wait; do not forge a success or directly toggle OFF.
 const auto on=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
 const auto off=targetDocument("disabled",on["input"]["revision"].get<int>()+1);
 fullPipeWaitForPlayerLock(on,off);
 check(writeTarget(on,off).value("ok",false),"commit fault OFF uses the actual settings admission");
 inputCaptureRefreshRealStickSettings(true);
 const auto admitted=g_virtualStartProtection.view(startProtectionClock);
 check(admitted.persona=="disabled"&&admitted.locked&&admitted.remainingMs,
  "commit fault retains the real closing request and original absolute deadline");
 if(admitted.persona!="disabled"||!admitted.locked||!admitted.remainingMs)
  throw std::runtime_error("commit fault did not obtain real OFF ticket");
 const auto deadline=startProtectionClock+admitted.remainingMs;

 const auto recordPath=fixtureTargetReturnRecordPath();
 check(recordPath.find((fspath::path(g_yemanccDataDir)/L"target-return-private").wstring())==0,
  "commit fault handle is confined to the existing private target-return record");
 duty::fileDetail::Handle denyReplace(CreateFileW(recordPath.c_str(),GENERIC_READ,
  FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 check(denyReplace.valid(),"commit fault privately holds a read handle that denies atomic replacement");
 if(!denyReplace.valid())throw std::runtime_error("commit fault private handle unavailable");

 // Do not call Stop directly or synthesize its result: consume the actual OFF
 // work queued by settings refresh while the private destination is held.
 fullPipeDrainQueuedLifecycle();
 const auto child=fullPipeChildOutcome();
 cases.back()["managedReceipt"]=child;
 check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&
   !child.value("hasController",true)&&!child.value("hasContext",true),
  "commit fault requires actual managed cleanup and exact old process exit");
 check(decodedCommandCount(child,"RELEASE_TARGET")==1&&decodedCommandCount(child,"SHUTDOWN")==1,
  "commit fault does not manufacture an extra release or shutdown attempt");

 const auto blocked=disk();
 bool proofRetained=false,cleanupProven=false;std::string storeFailure;
 {
  std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  auto& ledger=ymcc::inputHost::targetReturnRuntime::state;
  proofRetained=!oldCurrent.empty()&&std::all_of(oldCurrent.begin(),oldCurrent.end(),[&](const auto& origin){
   return std::any_of(ledger.proven.begin(),ledger.proven.end(),[&](const auto& proof){return duty::sameOrigin(proof,origin);});
  });
  storeFailure=ledger.session?ledger.session->error().reason:"no-session";
  const bool legacyProven=fixtureOriginalOwnedCleanupProvenLocked();
  check(legacyProven,"saved original production predicate incorrectly admits pending exact commit in this actual state");
  cases.back()["legacyCleanupProven"]=legacyProven;
  cleanupProven=inputHostOwnedCleanupProvenLocked();
 }
 check(blocked.state==duty::LoadState::Valid&&blocked.record&&encoded(*blocked.record)==runningBytes,
  "failed actual atomic replacement preserves the exact previous disk record");
 check(storeFailure=="atomic-replace-failed"&&proofRetained,
  "failed actual target settlement preserves exact current proof and reports its real store failure");
 // This assertion is intended to reject the current incomplete history-only
 // predicate. A failure here must remain a recorded semantic counterexample.
 check(!cleanupProven,"pending exact target commit vetoes complete owned cleanup");
 const auto awaiting=g_virtualStartProtection.view(startProtectionClock);
 check(awaiting.requestId==admitted.requestId&&awaiting.locked&&
   fullPipeOffFinalCounts(admitted.requestId)==std::make_pair(0u,0u),
  "uncommitted OFF cannot publish either final before its original verification owner");
 cases.back()["commitBlockedObservation"]={{"disk",summary(blocked)},{"proofRetained",proofRetained},
  {"cleanupProven",cleanupProven},{"storeFailure",storeFailure},{"protection",fullPipeProtectionState()}};

 if(!persistent)check(denyReplace.close(),"transient commit fault releases only its own private read handle");

 if(edge=="recording"||edge=="external-priority"||edge=="native-lease") {
  const auto beforeDisk=encoded(*disk().record);
  const auto beforePasses=ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses;
  const bool recording=g_shortcutRecordingActive.load(),external=g_externalPriorityVirtualSuspended.load();
  bool nativeLease=false;
  {std::lock_guard<std::mutex> lease(g_msiNativeForegroundMx);nativeLease=g_msiNativeForegroundLease.active;if(edge=="native-lease")g_msiNativeForegroundLease.active=true;}
  if(edge=="recording")g_shortcutRecordingActive=true;
  if(edge=="external-priority")g_externalPriorityVirtualSuspended=true;
  startProtectionClock+=1000;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}
  fullPipeDrainQueuedLifecycle();fullPipeVerificationStep();
  check(encoded(*disk().record)==beforeDisk&&ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses==beforePasses&&
    ymcc::inputHost::switchExecutionReturnRuntime::state.pending,
   "known pause does not commit current proof, spend observations or discard original OFF");
  check(g_virtualStartProtection.view(startProtectionClock).requestId==admitted.requestId&&
    startProtectionClock+g_virtualStartProtection.view(startProtectionClock).remainingMs==deadline&&
    fullPipeOffFinalCounts(admitted.requestId)==std::make_pair(0u,0u),
   "known pause preserves the original deadline and cannot publish a final");
  g_shortcutRecordingActive=recording;g_externalPriorityVirtualSuspended=external;
  {std::lock_guard<std::mutex> lease(g_msiNativeForegroundMx);g_msiNativeForegroundLease.active=nativeLease;}
  cases.back()["knownPause"]={{"kind",edge},{"remainingMs",g_virtualStartProtection.view(startProtectionClock).remainingMs}};
 }
 if(edge=="io-deadline"||edge=="io-power") {
  auto previous=std::move(fixtureMonotonicInterleave);bool interleaved=false;
  struct RestoreObserver {std::function<void()> previous;~RestoreObserver(){fixtureMonotonicInterleave=std::move(previous);}} restore{std::move(previous)};
  const auto powerBefore=currentPowerGeneration();
  fixtureMonotonicInterleave=[&](){
   if(interleaved||g_inputHostSwitchRecoveryStartRequestId!=admitted.requestId||
      ymcc::inputHost::targetReturnRuntime::state.proven.empty())return;
   interleaved=true;
   if(edge=="io-deadline")startProtectionClock=deadline;
   else g_powerGeneration=powerBefore+1;
  };
  startProtectionClock+=1000;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}
  fullPipeDrainQueuedLifecycle();
  check(interleaved,"current proof persistence reaches a real file IO clock boundary before controlled retirement");
  const auto committed=disk();
  check(committed.state==duty::LoadState::Valid&&committed.record&&committed.record->obligations.empty(),
   "storage completion remains a durable fact after deadline or power retirement in IO");
  check(!ymcc::inputHost::switchExecutionReturnRuntime::state.pending&&
    fullPipeSpawnCount==spawns+1&&!g_inputHostProcess&&
    fullPipeOffFinalCounts(admitted.requestId)==std::make_pair(0u,0u),
   "retired IO continuation cannot publish a result or dispatch a remaining Start");
  inputHostPerformSwitchExecutionReturn(admitted.requestId);
  check(fullPipeOffFinalCounts(admitted.requestId)==std::make_pair(0u,0u),
   "late work after IO retirement cannot publish an old OFF completion");
  startProtectionClock=deadline;fullPipeVerificationStep();
  check(g_virtualStartProtection.view(startProtectionClock).phase=="expired"&&
    fullPipeOffFinalCounts(admitted.requestId)==std::make_pair(0u,1u),
   "original verifier alone emits one timeout failure after IO context retires");
  cases.back()["retiredDuringIo"]={{"kind",edge},{"interleaved",interleaved},{"disk",summary(committed)},
   {"powerBefore",powerBefore},{"powerAfter",currentPowerGeneration()},{"protection",fullPipeProtectionState()}};
  fixtureFullPipe=false;return;
 }

 if(edge=="post-io-recording") {
  auto previous=std::move(fixtureMonotonicInterleave);unsigned actualIo=0;
  struct RestorePauseObserver {std::function<void()> previous;~RestorePauseObserver(){fixtureMonotonicInterleave=std::move(previous);g_shortcutRecordingActive=false;}} restore{std::move(previous)};
  fixtureMonotonicInterleave=[&](){
   if(g_inputHostSwitchRecoveryStartRequestId!=admitted.requestId||g_shortcutRecordingActive.load())return;
   ++actualIo;g_shortcutRecordingActive=true;
  };
  // Execute the saved pre-fix production continuation once, with an exact
  // queued request and real denied store IO. Its failure stays a negative
  // control; it is not counted as a production retry of the fixed consumer.
  startProtectionClock+=1000;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}
  fixtureLegacyPerformSwitchExecutionReturn(admitted.requestId);fullPipeDrainQueuedLifecycle();
  check(actualIo==1&&g_shortcutRecordingActive.load()&&
    ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses==0,
   "saved pre-fix continuation loses a real failed store observation when pause starts during IO");
  g_shortcutRecordingActive=false;
  for(unsigned expected=1;expected<=3;++expected){
   startProtectionClock+=1000;
   {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}
   fullPipeDrainQueuedLifecycle();
   const auto& period=ymcc::inputHost::switchExecutionReturnRuntime::state;
   check(actualIo==expected+1&&g_shortcutRecordingActive.load()&&period.coldPasses==expected&&
     period.pending&&period.nextObservation==startProtectionClock+1000,
    "actual failed IO is charged exactly once before its later known pause");
   const auto noIo=actualIo;
   startProtectionClock+=1;
   {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}
   fullPipeDrainQueuedLifecycle();
   check(actualIo==noIo&&period.coldPasses==expected,
    "entry already paused cannot execute or refund another storage observation");
   g_shortcutRecordingActive=false;
  }
  startProtectionClock+=1000;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}
  fullPipeDrainQueuedLifecycle();
  check(actualIo==4&&!ymcc::inputHost::switchExecutionReturnRuntime::state.pending&&
    ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses==3,
   "resume after third failed current commit retires original owner without a fourth fixed-consumer IO");
  cases.back()["postIoPauseBudget"]={{"oldControlIo",1},{"fixedConsumerIo",actualIo-1},
   {"chargedFailures",ymcc::inputHost::switchExecutionReturnRuntime::state.coldPasses}};
 }

 json observations=json::array();
 // Drive only the existing continuation and its original queue. No admission,
 // Stop, Start, device operation, pending flag edit, or fallback is injected.
 for(unsigned pass=0;pass<4;++pass){
  auto view=g_virtualStartProtection.view(startProtectionClock);
  if(!view.locked||startProtectionClock>=deadline)break;
  startProtectionClock+=(std::min)(std::uint64_t{1000},deadline-startProtectionClock);
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}
  fullPipeDrainQueuedLifecycle();fullPipeVerificationStep();
  view=g_virtualStartProtection.view(startProtectionClock);
  check(view.requestId==admitted.requestId&&fullPipeSpawnCount==spawns+1&&
    view.fallbackAttempts==admitted.fallbackAttempts,
   "commit continuation preserves exact ticket, original target count and fallback quota");
  check(!view.locked||startProtectionClock+view.remainingMs==deadline,
   "commit continuation never renews the original player deadline");
  observations.push_back({{"pass",pass+1},{"protection",fullPipeProtectionState()},{"disk",summary(disk())}});
 }
 auto inspected=g_virtualStartProtection.view(startProtectionClock);
 if(inspected.locked&&inspected.verificationRemainingMs){
  startProtectionClock+=(std::min)(inspected.verificationRemainingMs,deadline-startProtectionClock);
 }
 fullPipeVerificationStep();
 inspected=g_virtualStartProtection.view(startProtectionClock);
 if(inspected.locked){startProtectionClock=deadline;fullPipeVerificationStep();}
 const auto final=g_virtualStartProtection.view(startProtectionClock);
 check(final.requestId==admitted.requestId&&!final.locked,
  "commit fault closes at one actual final or the original hard deadline without indefinite waiting");
 const auto counts=fullPipeOffFinalCounts(admitted.requestId);
 check(persistent?counts==std::make_pair(0u,1u):counts==std::make_pair(1u,0u),
  "commit fault has exactly one expected final and never both success and failure");
 const auto settled=disk();
 bool currentDutyRemains=false;
 if(settled.record)for(const auto& origin:oldCurrent)
  currentDutyRemains=currentDutyRemains||std::any_of(settled.record->obligations.begin(),settled.record->obligations.end(),
   [&](const auto& item){return duty::sameOrigin(item.origin,origin);});
 check(settled.state==duty::LoadState::Valid&&settled.record&&currentDutyRemains==persistent,
  "transient fault commits exact old duty; persistent fault preserves it without claiming success");
 check(!persistent||(settled.record&&encoded(*settled.record)==runningBytes),
  "persistent fault preserves original retained bytes through bounded failure");
 if(persistent)check(denyReplace.close(),"persistent fault releases only its private handle after failed closing evidence");
 const auto beforeLate=fullPipeProtectionState();
 inputHostPerformSwitchExecutionReturn(admitted.requestId);fullPipeVerificationStep();
 check(fullPipeProtectionState()==beforeLate&&fullPipeOffFinalCounts(admitted.requestId)==counts&&
   fullPipeSpawnCount==spawns+1&&!g_inputHostProcess,
  "late old continuation cannot resurrect OFF or contradict its unique final");
 cases.back()["commitObservations"]=observations;
 const auto& continuation=ymcc::inputHost::switchExecutionReturnRuntime::state;
 check(continuation.coldPasses<=(persistent?3u:1u),"all current-proof retries share the original bounded observation counter");
 check(!persistent||continuation.coldPasses==3,"persistent current-proof denial exhausts exactly three original continuation observations");
 cases.back()["commitFinal"]={{"protection",fullPipeProtectionState()},{"disk",summary(settled)},
  {"currentProofRemaining",!ymcc::inputHost::targetReturnRuntime::state.proven.empty()},
  {"continuationPending",continuation.pending},{"continuationPasses",continuation.coldPasses},
  {"successFinals",counts.first},{"failureFinals",counts.second},{"deadline",deadline}};
 fixtureFullPipe=false;
}
}
static void fixtureTargetReturnLiveRecreateCase(){
 using namespace fixtureTargetReturn;
 preparePcCase("live-recreate-predecessor-commit-denied");
 const auto spawns=fullPipeSpawnCount;check(inputHostStart(),"live predecessor case creates its real private Host");
 const auto host=g_inputHostProcess;const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;
 duty::fileDetail::Handle denyReplace(INVALID_HANDLE_VALUE);unsigned injected=0;
 fixtureTargetReturnAfterReservation=[&](const json& request){
  if(request.value("command",std::string{})!="RECREATE_TARGET"||injected)return;
  ++injected;denyReplace.value=CreateFileW(fixtureTargetReturnRecordPath().c_str(),GENERIC_READ,FILE_SHARE_READ,
   nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(!denyReplace.valid())throw std::runtime_error("live-recreate-private-denial-handle");
 };
 struct ClearObserver {~ClearObserver(){fixtureTargetReturnAfterReservation={};}} clear;
 const bool recreated=inputHostRecreatePreparedTarget();fixtureTargetReturnAfterReservation={};
 check(recreated&&injected==1&&g_inputHostPrepared&&g_inputHostNeutralized,
  "actual RECREATE succeeds before predecessor persistence alone is denied");
 const auto before=disk();
 check(before.record&&before.record->obligations.size()==2&&
   ymcc::inputHost::targetReturnRuntime::state.proven.size()==1,
  "live replacement retains one exact predecessor proof and both durable target duties");
 const auto createsBefore=fullPipeRecreateCommands();
 check(inputHostStart()&&g_inputHostProcess==host&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&
   fullPipeSpawnCount==spawns+1&&fullPipeRecreateCommands()==createsBefore,
  "healthy live duplicate Start remains idempotent with predecessor commit pending");
 json refused;bool created=false;const auto sequence=g_inputHostSequence;
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
  created=inputHostRequestLocked("RECREATE_TARGET",json::object(),"target-prepared",3000,&refused,true,nullptr);
 }
 check(!created&&refused.value("status",std::string{})=="target-return-reservation-deferred"&&
   g_inputHostSequence==sequence&&encoded(*disk().record)==encoded(*before.record),
  "next actual target command is refused before wire dispatch while exact commit remains denied");
 check(denyReplace.close(),"live predecessor case releases only its own private denial handle");
 inputHostStop("offline-live-predecessor-cleanup");const auto child=fullPipeChildOutcome();
 check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&
   decodedCommandCount(child,"RECREATE_TARGET")==1&&decodedCommandCount(child,"RELEASE_TARGET")==1,
  "actual live replacement cleanup releases once and dispatches no second RECREATE");
 const auto after=disk();check(after.record&&after.record->obligations.empty()&&
  ymcc::inputHost::targetReturnRuntime::state.proven.empty(),
  "same original Stop settles exact predecessor and replacement duties after private denial lifts");
 cases.back()["liveRecreate"]={{"before",summary(before)},{"after",summary(after)},{"refused",refused},{"managedReceipt",child}};
 fixtureFullPipe=false;
}
static void fixtureTargetReturnPhysicalHistoryCase(){
 using namespace fixtureTargetReturn;
 preparePcCase("unrelated-physical-history-retained");check(inputHostStart(),"physical history case starts one actual current target");
 auto history=historical();auto old=history.obligations.front();old.origin.transactionId="historical-physical-origin";
 old.origin.physicalRoot="USB\\VID_054C&PID_0DF2\\HISTORICAL108";
 const std::string ownedPath="HID\\VID_054C&PID_0DF2\\HISTORICAL108";
 {std::lock_guard<InputHostMutex> lock(g_inputHostMx);auto& session=ymcc::inputHost::targetReturnRuntime::state.session;
  check(session&&session->reserveTarget(session->record()->revision,old.origin,{true,true}),"independent historical target duty reserves through actual Session");
  check(session&&session->attachAcceptedPsPin(session->record()->revision,old.origin,*old.acceptedPsPin),"independent historical PS pin persists through actual Session");
  check(session&&session->reservePath(session->record()->revision,old.origin,ownedPath,{true,true,true}),"independent historical physical path persists through actual Session");
 }
 const auto before=disk();inputHostStop("offline-unrelated-physical-history-cleanup");const auto child=fullPipeChildOutcome();const auto after=disk();
 check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false),"physical history case has exact actual managed cleanup and natural exit");
 check(before.record&&before.record->obligations.size()==2&&after.record&&after.record->obligations.size()==1&&
   duty::sameOrigin(after.record->obligations.front().origin,old.origin)&&after.record->obligations.front().paths.size()==1&&
   after.record->obligations.front().paths.front().instanceId==ownedPath&&after.record->obligations.front().acceptedPsPin&&
   duty::detail::samePin(*after.record->obligations.front().acceptedPsPin,*old.acceptedPsPin),
  "exact current target settlement cannot erase unrelated origin pin or physical path");
 cases.back()["physicalHistory"]={{"before",summary(before)},{"after",summary(after)},{"managedReceipt",child}};fixtureFullPipe=false;
}

static void fixtureTargetReturnWireObserve(void* buffer,DWORD size){
 const auto request=json::parse(std::string(static_cast<char*>(buffer),size),nullptr,false);
 if(!request.is_object())return;const auto command=request.value("command",std::string{});
 if(command!="PREPARE_TARGET"&&command!="RECREATE_TARGET")return;
 if(fixtureColdArchive)fixtureColdBeforeCreate();
 const auto loaded=fixtureTargetReturn::disk();
 const bool exact=loaded.state==ymcc::targetReturn::LoadState::Valid&&loaded.record&&
  std::any_of(loaded.record->obligations.begin(),loaded.record->obligations.end(),[](const auto& item){return item.targetMayExist&&fixtureTargetReturn::current(item);});
 check(exact,"actual PREPARE or RECREATE wire is preceded by persisted targetMayExist for exact current owner");
 check(request.value("runId",std::string{})==g_inputHostRunId&&request.value("epoch",0ULL)==g_inputHostEpoch,"write-ahead observation binds actual serialized request tuple");
 fixtureTargetReturnObservations.push_back({{"stage","before-actual-wire-write"},{"command",command},{"caseSerial",fixtureTargetReturnCaseSerial},{"captureBasis","real107-process-handle"},{"request",request},{"disk",fixtureTargetReturn::summary(loaded)}});
 if(fixtureTargetReturnAfterReservation)fixtureTargetReturnAfterReservation(request);
}
static void fixtureTargetReturnAckLostObserve(const json& receipt){
 const auto loaded=fixtureTargetReturn::disk();
 check(loaded.state==ymcc::targetReturn::LoadState::Valid&&fixtureTargetReturn::currentCount(loaded)>0,"lost actual PREPARE ACK retains the persisted current target duty before cleanup");
 fixtureTargetReturnObservations.push_back({{"stage","actual-prepare-ack-lost"},{"caseSerial",fixtureTargetReturnCaseSerial},{"receipt",receipt},{"disk",fixtureTargetReturn::summary(loaded)}});
}
static void fixtureTargetReturnLifecycleObserve(const char* event,const json& detail){
 if(!event||std::strcmp(event,"input-host-stop-result")!=0||!detail.is_object()||
    !detail.value("targetMayExist",false)||detail.value("hostExitConfirmed",false))return;
 const auto loaded=fixtureTargetReturn::disk();const auto run=detail.value("runId",std::string{});const auto epoch=detail.value("epoch",0ULL);const auto host=detail.value("hostPid",0u);
 const bool retained=loaded.state==ymcc::targetReturn::LoadState::Valid&&loaded.record&&std::any_of(loaded.record->obligations.begin(),loaded.record->obligations.end(),[&](const auto& item){return item.targetMayExist&&item.origin.runId==run&&item.origin.epoch==epoch&&item.origin.host.pid==host;});
 check(retained,"unproven actual Host exit retains persisted target duty for exact old run epoch and PID");
 fixtureTargetReturnObservations.push_back({{"stage","stop-host-exit-unproven"},{"caseSerial",fixtureTargetReturnCaseSerial},{"stopDetail",detail},{"disk",fixtureTargetReturn::summary(loaded)}});
}
static void fixtureTargetReturnNativeCases(){
 namespace duty=ymcc::targetReturn;
 seed();cases.push_back({{"name","TARGET-RETURN/native-only/private-path-and-historical-pin"},{"captureBasis","explicit event-host identity model; no OS process claim"}});
 const auto path=fixtureTargetReturnRecordPath();check(path.find((fspath::path(g_yemanccDataDir)/L"target-return-private").wstring())==0,"target return record path stays inside explicit private fixture root");
 const auto record=fixtureTargetReturn::historical();check(duty::store(path,record).committed(),"historical pin written by actual production store");
 const auto loaded=duty::load(path,"fixture-machine");check(loaded.state==duty::LoadState::Valid&&loaded.record&&loaded.record->obligations.size()==1&&loaded.record->obligations.front().acceptedPsPin.has_value(),"actual store load preserves historical accepted pin as data");
 check(!g_psEndpointOwnership.snapshot().valid&&!g_inputHostActive&&!g_inputHostPrepared,"loading retained disk pin does not accept a current PS endpoint or output");
 cases.back()["disk"]=fixtureTargetReturn::summary(loaded);
}
static void fixtureTargetReturnFullPipeCases(){
 namespace duty=ymcc::targetReturn;
 // Ordinary PC, no physical source: target construction is permitted, while
 // historical pins and creation alone must not manufacture live input proof.
 for(const bool retainHistory:{false,true}){
  fixtureTargetReturn::preparePcCase(retainHistory?"retained-old-pin-exact-current-settlement":"source-free-normal-complete-stop");
  const auto history=fixtureTargetReturn::historical();
  check(!g_psEndpointOwnership.snapshot().valid,"historical file is not fed into current in-memory State.accept");
  const auto spawns=fullPipeSpawnCount;check(inputHostStart()&&fullPipeSpawnCount==spawns+1,"ordinary source-free PC creates exactly one owned managed target without model rejection");
  // Cold history must block a new Start. To test unrelated Stop settlement,
  // add this independent legacy origin only after the real current Start.
  if(retainHistory){
   std::lock_guard<InputHostMutex> lock(g_inputHostMx);
   auto& session=ymcc::inputHost::targetReturnRuntime::state.session;
   const auto& old=history.obligations[0];bool reserved=false,pinned=false;
   if(session&&session->ready()){
    reserved=session->reserveTarget(session->record()->revision,old.origin,{true,true});
    if(reserved)pinned=session->attachAcceptedPsPin(session->record()->revision,old.origin,*old.acceptedPsPin);
   }
   check(reserved&&pinned,"independent legacy responsibility is added through actual locked Session after current Start");
   check(old.origin.cliWriterProtocol==0,"retained independent history stays legacy unknown without producer promotion");
  }
  const auto running=fixtureTargetReturn::disk();
  check(running.state==duty::LoadState::Valid&&running.record&&fixtureTargetReturn::currentCount(running)==1,"accepted current target has exactly one persisted current-owner obligation");
  const auto pin=g_psEndpointOwnership.snapshot();
  const auto own=running.record?std::find_if(running.record->obligations.begin(),running.record->obligations.end(),[](const auto& item){return fixtureTargetReturn::current(item);}):std::vector<duty::Obligation>::const_iterator{};
  check(running.record&&own!=running.record->obligations.end()&&own->acceptedPsPin&&pin.valid&&own->acceptedPsPin->sdkRootInstanceId==pin.rootInstanceId&&own->acceptedPsPin->hidInstanceIds==pin.hidInstanceIds&&own->acceptedPsPin->bindingGeneration==pin.generation,"accepted real PS State pin is persisted after current acceptance");
  const auto ticket=g_virtualStartProtection.view(startProtectionClock);
  check(!g_inputHostActive&&ticket.phase=="pending","retained old pin and target creation cannot replace missing fresh physical input or startup success");
  std::vector<duty::Origin> oldCurrent;if(running.record)for(const auto& item:running.record->obligations)if(fixtureTargetReturn::current(item))oldCurrent.push_back(item.origin);
  inputHostStop("virtual-target-disabled");const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;
  check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"normal target settlement is paired with actual managed cleanup and natural process exit");
  const auto stopped=fixtureTargetReturn::disk();bool removed=stopped.state==duty::LoadState::Valid&&stopped.record.has_value();
  if(stopped.record)for(const auto& origin:oldCurrent)if(std::any_of(stopped.record->obligations.begin(),stopped.record->obligations.end(),[&](const auto& item){return duty::sameOrigin(item.origin,origin);}))removed=false;
  check(removed&&!oldCurrent.empty(),"complete Stop consumes exactly the settled old current target-only origins");
  check(stopped.record&&stopped.record->obligations.size()==(retainHistory?1u:0u),"normal settlement never consumes retained historical origins");
  if(retainHistory)check(stopped.record&&stopped.record->obligations.size()==1&&duty::sameOrigin(stopped.record->obligations[0].origin,history.obligations[0].origin)&&stopped.record->obligations[0].acceptedPsPin&&duty::detail::samePin(*stopped.record->obligations[0].acceptedPsPin,*history.obligations[0].acceptedPsPin),"retained historical origin and pin remain byte-equivalent after unrelated current Stop");
  cases.back()["runningDisk"]=fixtureTargetReturn::summary(running);cases.back()["stoppedDisk"]=fixtureTargetReturn::summary(stopped);fixtureFullPipe=false;
 }
 for(const std::string variant:{"corrupt-file","exclusive-owner-conflict"}){
  fixtureTargetReturn::preparePcCase(variant);const auto path=fixtureTargetReturnRecordPath();
  HANDLE owner=INVALID_HANDLE_VALUE;
  if(variant=="corrupt-file"){std::ofstream output{fspath::path(path),std::ios::binary};output<<"{broken-108";output.close();check(bool(output),"actual corrupt record file seeded privately");}
  else {owner=CreateFileW((path+L".owner").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);check(owner!=INVALID_HANDLE_VALUE,"actual exclusive private owner handle acquired");}
  const auto beforeObservations=fixtureTargetReturnObservations.size();const auto spawns=fullPipeSpawnCount;const auto observerCount=fullPipeChildObservers.size(),receiptCount=fullPipeReceipts.size();
  const auto wireSequence=g_inputHostSequence;const auto externalTrace=g_startEntrySeam.trace;
  const bool started=inputHostStart();
  check(!started&&fullPipeSpawnCount==spawns&&fullPipeChildObservers.size()==observerCount&&fullPipeReceipts.size()==receiptCount,"unreadable cold record blocks before any owned child spawn or HELLO");
  bool creationDispatched=false;for(std::size_t i=beforeObservations;i<fixtureTargetReturnObservations.size();++i)if(fixtureTargetReturnObservations[i].value("stage",std::string{})=="before-actual-wire-write")creationDispatched=true;
  check(!creationDispatched&&g_inputHostSequence==wireSequence,"corrupt record or exclusive-owner conflict dispatches zero HELLO PREPARE or RECREATE wire commands");
  check(!g_inputHostPrepareMayHaveReachedHost&&!g_inputHostProcess&&!g_hidHideVisibilityJournal.active&&g_startEntrySeam.trace==externalTrace,"cold denial makes no target-issued claim or device-mode/visibility side effect");
  const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
  if(owner!=INVALID_HANDLE_VALUE){CloseHandle(owner);owner=INVALID_HANDLE_VALUE;}
  if(variant=="corrupt-file"){
   {std::ifstream input{fspath::path(path),std::ios::binary};const std::string bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());check(bytes=="{broken-108","failed admission never overwrites corrupt historical bytes with an empty record");cases.back()["deniedOriginalBytes"]=bytes;}
   // Explicit fixture repair restores known retained duty, never an empty file.
   const auto history=fixtureTargetReturn::historical();check(fixtureTargetReturn::externalRepairKnownHistory(history),"explicit external fixture repair restores known legacy duty without clearing responsibility");
   const auto repaired=fixtureTargetReturn::encoded(history);
   startProtectionClock+=1000;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}fullPipeDrainQueuedLifecycle();
   const auto loaded=fixtureTargetReturn::disk();
   check(ymcc::inputHost::targetReturnRuntime::state.session&&ymcc::inputHost::targetReturnRuntime::state.session->ready()&&loaded.record&&fixtureTargetReturn::encoded(*loaded.record)==repaired,"actual continuation reopens repaired file and retains exact unrelated legacy responsibility");
   check(fullPipeSpawnCount==spawns&&fullPipeChildObservers.size()==observerCount&&!g_inputHostProcess&&!g_inputHostPrepareMayHaveReachedHost,"readable legacy history still blocks new child and target dispatch after recovery reentry");
  }else{
   startProtectionClock+=1000;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}fullPipeDrainQueuedLifecycle();
   check(fullPipeSpawnCount==spawns+1&&g_inputHostPrepared&&g_virtualStartProtection.view(startProtectionClock).requestId==ticket,"released owner permits exact original cold continuation to resume one actual Start without readmission");
   inputHostStop("offline-target-return-record-reentry-cleanup");
   if(fullPipeChildObservers.size()>observerCount){const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false),"successful recovery reentry has its own exact managed cleanup and natural exit receipt");}
   else check(false,"successful recovery reentry must own a child receipt before observation");
  }
  cases.back()["afterRecordReentry"]=fixtureTargetReturn::summary(fixtureTargetReturn::disk());fixtureFullPipe=false;
 }
 // Actual OFF and real private-file commit-denial regression variants.
 fixtureTargetReturn::commitFailureOffCase(false);
 fixtureTargetReturn::commitFailureOffCase(true);
  for(const std::string edge:{"io-deadline","io-power","recording","external-priority","native-lease"})
  fixtureTargetReturn::commitFailureOffCase(false,edge);
 fixtureTargetReturn::commitFailureOffCase(true,"post-io-recording");
 fixtureTargetReturnLiveRecreateCase();
 fixtureTargetReturnPhysicalHistoryCase();


 // Existing actual pipe ACK-loss + held SDK-stop case is re-used. The new
 // read-only ACK observer asserts durable debt while exit is not yet proven.
 const auto before=fixtureTargetReturnObservations.size();fullPipePrepareAckLostCase("PC");
 bool observed=false;for(std::size_t i=before;i<fixtureTargetReturnObservations.size();++i)if(fixtureTargetReturnObservations[i].value("stage",std::string{})=="actual-prepare-ack-lost")observed=true;
 check(observed,"target return regression executes actual lost PREPARE ACK with held child cleanup and retained debt");
}
