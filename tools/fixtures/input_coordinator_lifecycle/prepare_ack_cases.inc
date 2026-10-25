// Actual PREPARE creates an owned managed target. Only its native wire read is
// lost; a bounded fake SDK output-stop gate keeps real cleanup/child pending.
static void fullPipePrepareAckLostCase(const std::string& family,bool offWhileHeld=false,bool unknownReturn=false,bool exitSweep=false){
 seed();fixtureFullPipe=true;fullPipeFault="prepare-ack-loss-held-exit";g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 g_machineIdentity.family=family=="ROG"?YmccFamily::AsusRogAllyX:YmccFamily::Unknown;g_machineIdentity.manufacturer=family=="ROG"?"ASUSTEK COMPUTER INC.":"FIXTURE-PC";g_machineIdentity.product=family=="ROG"?"ROG Ally X":"unregistered-desktop";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=1;if(family=="PC")fixturePcPowerCapabilities=true;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 cases.push_back({{"name","PREPARE-ACK-LOSS/"+family+(exitSweep?"/app-exit-sweep-while-owned-target-held":offWhileHeld?"/explicit-OFF-while-owned-target-held":unknownReturn?"/unknown-owned-root-negative-proof":"/actual-owned-target-held-cleanup")},{"controlledFamily",family}});
 const auto restoreCalls=[](){return static_cast<unsigned>(std::count_if(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),[](const std::string& entry){return entry.rfind("hidhide-restore:",0)==0;}));};
 const auto spawns=fullPipeSpawnCount,restores=restoreCalls();const auto on=targetDocument("dualsense-edge",0),off=targetDocument("disabled",1);
 check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"prepare ACK loss seeds a private real settings request");inputCaptureRefreshRealStickSettings(true);const auto initial=g_virtualStartProtection.view(startProtectionClock);
 fixturePrepareAckLoss=true;const bool started=inputHostStart();fixturePrepareAckLoss=false;
 check(!started&&fixturePrepareAckLossConsumed&&fullPipeSpawnCount==spawns+1,"actual owned managed PREPARE succeeds while only its native ACK read is lost");
 check(fixturePrepareAckOriginal.is_object()&&fixturePrepareAckOriginal.value("command","")=="PREPARE_TARGET"&&fixturePrepareAckOriginal.value("status","")=="target-prepared","captured actual PREPARE ACK proves the real child created its target before receipt loss");
 json hold;const auto holdPath=fullPipeReceipts.back()+L".hold.json";{std::ifstream input{fspath::path(holdPath)};if(input)input>>hold;}
 const auto childHandle=fullPipeChildObservers.back();const bool childAlive=WaitForSingleObject(childHandle,0)==WAIT_TIMEOUT;
 check(childAlive&&hold.is_object()&&hold.value("hasController",false)&&hold.value("hasContext",false)&&hold.value("lastCommand","")=="PREPARE_TARGET"&&hold.value("processId",0u)==GetProcessId(childHandle),"real owned child remains alive at its bounded fake SDK cleanup gate after native stop wait");
 const auto beforeGate=state();
 check(restoreCalls()==restores,"lost PREPARE ACK must not restore physical visibility while actual target cleanup is still unproven");
 check(g_inputHostProcess&&g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&!g_inputHostPrepared,"unaccepted creation remains quarantined with exact live child ownership and future Start blocked");
 const auto view=g_virtualStartProtection.view(startProtectionClock);check(view.requestId==initial.requestId&&view.locked&&view.candidateOutcome=="failure"&&view.fallbackAttempts==0,"unproven creation remains the original failure candidate without another player or fallback");
 cases.back()["heldCleanupProof"]={{"hostAlive",childAlive},{"actualPrepareReceipt",fixturePrepareAckOriginal},{"hold",hold},{"nativeState",beforeGate},{"seamTraceAtHold",g_startEntrySeam.trace},{"restoreCallsBefore",restores},{"restoreCallsAtHold",restoreCalls()}};
 if(exitSweep){
  fixtureExitSweep=true;hidHideSweepAllHiddenDevices("app-exit");fixtureExitSweep=false;
  check(WaitForSingleObject(childHandle,0)==WAIT_TIMEOUT&&g_psEndpointOwnership.retirementRequired(),"exit sweep test retains its exact live child and PS target return obligation");
  check(fixtureExitSweepLists==0&&fixtureExitSweepUnhides==0&&fixtureExitSweepCloakOffs==0,"final app-exit sweep cannot bypass live Host or unproven target ownership");
  cases.back()["exitSweepWhileHeld"]={{"lists",fixtureExitSweepLists},{"unhides",fixtureExitSweepUnhides},{"cloakOffs",fixtureExitSweepCloakOffs},{"nativeState",state()}};
 }
 std::uint64_t closingTicket=0;
 if(offWhileHeld){
  fullPipeWaitForPlayerLock(on,off);check(writeTarget(on,off).value("ok",false),"real explicit OFF is admitted after original ON lock while old cleanup is held");inputCaptureRefreshRealStickSettings(true);closingTicket=g_virtualStartProtection.view(startProtectionClock).requestId;
  const auto beforeExplicitRestore=restoreCalls();fullPipeDrainQueuedLifecycle();const bool stillAlive=WaitForSingleObject(childHandle,0)==WAIT_TIMEOUT;
  check(stillAlive,"explicit OFF is observed while exact old child is still at its bounded cleanup gate");
  check(restoreCalls()==beforeExplicitRestore&&g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&g_inputHostPrepareMayHaveReachedHost,"explicit OFF cannot override live unknown target return and restore physical visibility");
  fullPipeCheckStopCandidate(closingTicket,"failure");
  cases.back()["explicitOffWhileHeld"]={{"requestId",closingTicket},{"hostAlive",stillAlive},{"restoreCallsBefore",beforeExplicitRestore},{"restoreCallsAfter",restoreCalls()},{"nativeState",state()}};
 }
 const auto gatePath=fullPipeReceipts.back()+L".release-gate";{std::ofstream output{fspath::path(gatePath)};output<<"release";}
 const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;
 check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"releasing only the fake SDK gate permits actual managed cleanup and natural owned child exit");
 if(unknownReturn){
  check(g_psEndpointOwnership.retirementRequired()&&!g_psEndpointOwnership.retirement().target.valid,"lost accepted PREPARE identity retains an explicit unknown-root return obligation");fixturePsReturnManualTopology=true;g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;
  const auto beforeUnknownRestore=restoreCalls();
  for(unsigned phase=0;phase<4;++phase){
   fixturePsReadConfig(phase==1?std::vector<std::wstring>{}:phase==2?std::vector<std::wstring>{L"HID#VID_1234&PID_0001#PHYSICAL"}:std::vector<std::wstring>{L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OTHER-PROCESS-B"},phase==1?ERROR_ACCESS_DENIED:ERROR_NO_MORE_ITEMS);if(phase==2)fixturePsParentFailure=0;
   {FixturePsReadScope scope;fullPipeQuarantineProbe();}
   check(g_inputHostReleaseQuarantined&&g_psEndpointOwnership.retirementRequired()&&restoreCalls()==beforeUnknownRestore,"unknown old PS root cannot be discharged by profile presence or incomplete family census");
  }
  fixturePsReadConfig({L"HID#VID_1234&PID_0001#PHYSICAL"});{FixturePsReadScope scope;fullPipeQuarantineProbe();}
  check(!g_inputHostReleaseQuarantined&&!g_psEndpointOwnership.retirementRequired()&&restoreCalls()==beforeUnknownRestore+1,"complete profile-negative census retires unknown old PS root without claiming a peer as owned");
 }
 if(!offWhileHeld){fullPipeWaitForPlayerLock(on,off);check(writeTarget(on,off).value("ok",false),"unproven creation closes through a new actual admitted player OFF after original lock");inputCaptureRefreshRealStickSettings(true);closingTicket=g_virtualStartProtection.view(startProtectionClock).requestId;}
 // The controlled local physical endpoint is the same leaf as the existing
 // normal-return fixture. Only the actual quarantine consumer retires ownership.
 g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;fullPipeQuarantineProbe();fullPipeDrainQueuedLifecycle();
 if(!offWhileHeld)fullPipeCheckStopCandidate(closingTicket,"success");fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));
 check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&fullPipeSpawnCount==spawns+1,"verified deferred OFF retires the exact old child without an extra target or leaked visibility ownership");
 if(exitSweep){
  fixtureExitSweepLists=fixtureExitSweepUnhides=fixtureExitSweepCloakOffs=0;fixtureExitSweep=true;hidHideSweepAllHiddenDevices("app-exit");fixtureExitSweep=false;
  check(!g_hidHideVisibilityJournal.active&&ymcc::inputHost::targetReturnRuntime::state.physicals.empty(),"verified target return leaves no owned physical work for final app-exit cleanup");
  check(fixtureExitSweepLists==0&&fixtureExitSweepUnhides==0&&fixtureExitSweepCloakOffs==0,"final app-exit with zero owned physical work performs no global list unhide or cloak query");
  HANDLE acquired=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(acquired&&release,"private lifecycle lock contention events created");
  std::thread busy([&](){std::lock_guard<InputHostMutex> owner(g_inputHostMx);SetEvent(acquired);WaitForSingleObject(release,3000);});
  const bool locked=WaitForSingleObject(acquired,3000)==WAIT_OBJECT_0;
  fixtureExitSweepLists=fixtureExitSweepUnhides=fixtureExitSweepCloakOffs=0;fixtureExitSweep=true;hidHideSweepAllHiddenDevices("app-exit");fixtureExitSweep=false;
  SetEvent(release);busy.join();CloseHandle(acquired);CloseHandle(release);
  check(locked&&fixtureExitSweepLists==0&&fixtureExitSweepUnhides==0&&fixtureExitSweepCloakOffs==0,"contended exit lifecycle owner cannot be mistaken for a safe global sweep");
  cases.back()["exitSweepAfterReturn"]={{"lists",fixtureExitSweepLists},{"unhides",fixtureExitSweepUnhides},{"cloakOffs",fixtureExitSweepCloakOffs}};
 }
 cases.back()["nativeEvents"]=fixtureEvents;fixturePsReturnManualTopology=false;fixtureFullPipe=false;fullPipeFault="none";
}
