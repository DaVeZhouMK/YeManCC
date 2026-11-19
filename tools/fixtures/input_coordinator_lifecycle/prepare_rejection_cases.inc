static void fullPipePrepareRejectionColdCase(){
 using namespace personaLifecycle140;
 prepare(1,"PREPARE-REJECT/ROG/actual-cold-create-exception");
 const auto from=targetDocument("disabled",0),requested=targetDocument("elite",1);
 require(sgWriteFileAtomic(YM_SETTINGS_FILE,from.dump())&&writeTarget(from,requested).value("ok",false),
  "explicit player ON uses original private settings admission");
 inputCaptureRefreshRealStickSettings(true);fixturePrepareOriginalOnEdge();observeSource(1,"elite");
 const auto initial=g_virtualStartProtection.view(startProtectionClock);
 const auto beforeSpawns=fullPipeSpawnCount;const auto originalDeadline=startProtectionClock+initial.remainingMs;
 require(initial.requestId!=0&&initial.locked,"actual player start has a protected request");
 fullPipeFault="prepare-create-exception-once";
 fixturePrepareRejectionWire=json::array();fixturePrepareRejectionCapture=true;
 const bool started=inputHostStart();
 fixturePrepareRejectionCapture=false;fullPipeFault="none";
 cases.back()["actualWire"]=fixturePrepareRejectionWire;
 cases.back()["afterStart"]=state();cases.back()["events"]=fixtureEvents;
 require(!started&&fullPipeSpawnCount==beforeSpawns+1&&!g_inputHostPrepared&&!g_inputHostNeutralized&&!g_inputHostActive,
  "one original Start fails without publishing a prepared, neutralized, or active target");
 require(fullPipeChildObservers.size()==1&&fullPipePrivateChildren.size()==1,
  "one real private child PID and birth were captured before the refusal");
 json outgoing,reply;unsigned sent=0,received=0,neutral=0;
 for(const auto& row:fixturePrepareRejectionWire){
  const auto& message=row.at("message");const auto command=message.value("command",std::string{});
  if(row.at("write")==true&&command=="SUBMIT_NEUTRAL")++neutral;
  if(command!="PREPARE_TARGET")continue;
  if(row.at("write")==true){outgoing=message;++sent;}else{reply=message;++received;}
 }
 require(sent==1&&received==1&&neutral==0&&reply.at("ok")==false&&reply.at("status")=="prepare-rejected"&&reply.at("assetVerified")==true,
  "a single delivered semantic refusal, not timeout, broken pipe, or lost successful acknowledgement");
 for(const char* key:{"requestId","runId","epoch","sequence","powerGeneration","startupNonce","persona","profileIdentity","targetId","ownerIdentity","configRevision","configHash"})
  require(outgoing.contains(key)&&reply.contains(key)&&outgoing.at(key)==reply.at(key),"real rejection echoes the exact sent owner tuple");
 require(reply.at("persona")=="elite"&&reply.at("profileIdentity")=="xbox-elite-v2"&&g_inputHostFaultHoldsStart.load(),
  "original coordinator consumes the proper persona rejection and holds reopen");
 const auto failed=g_virtualStartProtection.view(startProtectionClock);
 require(failed.requestId==initial.requestId&&failed.fallbackAttempts==0&&failed.candidateOutcome!="success",
  "failure retains original player request without fallback or success publication");
 const auto child=fullPipeChildObservers.back();const auto childFile=fullPipeReceipts.back();const auto identity=fullPipePrivateChildren.back().host;
 HANDLE oldHandle=nullptr;
 require(DuplicateHandle(GetCurrentProcess(),child,GetCurrentProcess(),&oldHandle,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,
  "hold an exact independent process observation until old durable duty has retired");
 ymcc::targetReturn::fileDetail::Handle heldOld(oldHandle);
 const auto managed=fullPipeChildOutcome(child,childFile);
 cases.back()["managedReceipt"]={{"path",W2U(childFile)},{"pid",identity.pid},{"birth",identity.creationTime},{"result",managed}};
 require(managed.value("processId",0u)==identity.pid&&managed.at("startupNonce")==reply.at("startupNonce")&&
  managed.value("faultConsumed",false)&&managed.value("creates",0u)==1&&managed.value("finalCleanupOk",false)&&
  !managed.value("hasController",true)&&!managed.value("hasContext",true)&&managed.value("nativeObservedProcessExit",false)&&managed.value("nativeObserverCloseSucceeded",false),
  "exact faulted Managed child exits and returns its locally owned controller and context");
 unsigned disposals=0,creates=0;for(const auto& event:managed.at("events")){
  if(event=="context-dispose")++disposals;if(event=="controller-dispose")++creates;
 }
 require(disposals==1&&creates==0,"the failed fake factory disposes its context once without a created controller to dispose");
 fullPipeDrainQueuedLifecycle();
 const bool locallyRetired=!g_inputHostProcess&&!g_inputHostReleaseQuarantined;
 const bool quarantined=g_inputHostReleaseQuarantined&&g_inputHostStartBlocked;
 require(!locallyRetired&&quarantined&&fullPipeSpawnCount==beforeSpawns+1,
  "a rejected PREPARE has possible creation duty and remains quarantined without fresh target proof");
 cases.back()["terminal"]={{"locallyRetired",locallyRetired},{"quarantined",quarantined},{"automaticSpawns",fullPipeSpawnCount-beforeSpawns-1},{"state",state()}};
 const auto oldRun=outgoing.at("runId").get<std::string>();
 const auto oldEpoch=outgoing.at("epoch").get<std::uint64_t>();
 const auto oldSettled=[&](){std::lock_guard<InputHostMutex> lock(g_inputHostMx);return inputHostTargetReturnRuntimeSettledLocked(oldRun,oldEpoch);};
 require(!oldSettled()&&g_inputHostRunId==oldRun&&g_inputHostEpoch==oldEpoch,
  "the exact old creation duty still exists after the Host has exited");
 // Only the original fixture's raw PnP inputs change; the real process probe,
 // ledger, old tuple, Start guard and native return consumer are not replaced.
 fixtureRuntimeRecoveryOsEnabled=true;fixtureRuntimeRecoveryOldHost=oldHandle;fixtureRuntimeRecoveryOldOwner=identity;
 for(const std::string census:{"present","unknown"}){
  fixtureRuntimeRecoveryCensus=census;const auto calls=fixtureRuntimeRecoveryCensusCalls;
  fullPipeQuarantineProbe();inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  cases.back()["returnProbes"].push_back({{"census",census},{"calls",fixtureRuntimeRecoveryCensusCalls-calls},{"state",state()},{"settled",oldSettled()}});
  require(fixtureRuntimeRecoveryCensusCalls>calls&&!oldSettled()&&!g_inputHostProcess&&g_inputHostReleaseQuarantined&&fullPipeSpawnCount==beforeSpawns+1,
   "live or incomplete PnP observation consumes no old duty and allows no new writer");
 }
 fixtureRuntimeRecoveryCensus="absent";const auto absentCalls=fixtureRuntimeRecoveryCensusCalls;
 fullPipeQuarantineProbe();inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
 cases.back()["returnProbes"].push_back({{"census","absent"},{"calls",fixtureRuntimeRecoveryCensusCalls-absentCalls},{"state",state()},{"settled",oldSettled()}});
 cases.back()["returnObservations"]=fixtureRuntimeRecoveryOsObservations;
 require(fixtureRuntimeRecoveryCensusCalls>absentCalls&&oldSettled()&&!g_inputHostProcess&&!g_inputHostPrimaryThread&&
    !g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&g_inputHostStartBlocked&&fullPipeSpawnCount==beforeSpawns+1,
  "fresh complete target absence retires the old duty without granting another Start");
 fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureRuntimeRecoveryOsEnabled=false;
 auto failedNow=g_virtualStartProtection.view(startProtectionClock);
 require(failedNow.requestId==initial.requestId&&failedNow.fallbackAttempts==0&&
    (!failedNow.locked||startProtectionClock+failedNow.remainingMs==originalDeadline),
  "old request cannot renew its original protected deadline while awaiting return");
 if(failedNow.locked){advance(failedNow.remainingMs+1);fullPipeVerificationStep();}
 inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
 const auto expired=g_virtualStartProtection.view(startProtectionClock);
 require(expired.requestId==initial.requestId&&!expired.locked&&expired.fallbackAttempts==0&&fullPipeSpawnCount==beforeSpawns+1,
  "retirement and expiry cannot automatically retry the rejected request");
 const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
 const auto off=targetDocument("disabled",current["input"]["revision"].get<int>()+1);
 require(writeTarget(current,off).value("ok",false),"explicit OFF is admitted after the old request ends");
 inputCaptureRefreshRealStickSettings(true);fixturePrepareOriginalOnEdge();inputHostStop("virtual-target-disabled");fullPipeDrainQueuedLifecycle();
 const auto offTicket=g_virtualStartProtection.view(startProtectionClock).requestId;
 fullPipeRogObserveReturnedSource(offTicket,true);
 const auto offResult=g_virtualStartProtection.view(startProtectionClock);
 if(offResult.verificationRemainingMs)advance(offResult.verificationRemainingMs);
 (void)fresh(1);fullPipeVerificationStep();fullPipeVerificationStep();
 require(g_virtualStartProtection.view(startProtectionClock).requestId==offTicket&&
    !g_virtualStartProtection.view(startProtectionClock).locked&&fullPipeSpawnCount==beforeSpawns+1,
  "the OFF observation retires without a hidden restart");
 const auto next=targetDocument("elite",off["input"]["revision"].get<int>()+1);
 require(writeTarget(off,next).value("ok",false),"second ON uses the real settings consumer, not forced rearm");
 const bool faultHeldBeforeOn=g_inputHostFaultHoldsStart.load();
 inputCaptureRefreshRealStickSettings(true);fixturePrepareOriginalOnEdge();observeSource(1,"elite");
 require(faultHeldBeforeOn&&!g_inputHostFaultHoldsStart.load(),"the original gamepad callback ON-edge admits the new intent but not the retired one");
 const auto nextTicket=g_virtualStartProtection.view(startProtectionClock).requestId;
 require(nextTicket!=initial.requestId&&nextTicket!=offTicket&&inputHostStart()&&fullPipeSpawnCount==beforeSpawns+2&&
    g_inputHostRunId!=oldRun&&g_inputHostEpoch!=oldEpoch,
  "only an independently admitted ON can start exactly one new private Host");
 cases.back()["successor"]={{"originalRequestId",initial.requestId},{"originalDeadline",originalDeadline},{"nextRequestId",nextTicket},
    {"oldRunId",oldRun},{"oldEpoch",oldEpoch},{"newRunId",g_inputHostRunId},{"newEpoch",g_inputHostEpoch}};
 const auto successorObservation=fullPipeChildObservers.back();
 const auto successorReceipt=fullPipeReceipts.back();
 try {
  const auto currentEpoch=g_inputHostEpoch;
  const auto currentRun=g_inputHostRunId;
  const auto currentProcess=g_inputHostProcess;
  require(g_inputHostRecoveryIntentReset.load(),"the explicit ON leaves an episode reset for the original maintenance owner");
  inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  require(!g_inputHostRecoveryIntentReset.load()&&g_inputHostEpoch==currentEpoch&&g_inputHostRunId==currentRun&&
     g_inputHostProcess==currentProcess&&fullPipeSpawnCount==beforeSpawns+2&&g_virtualStartProtection.view(startProtectionClock).requestId==nextTicket,
   "original recovery maintenance consumes the rearm without losing or replacing the explicit successor");
  success(1,"elite",nextTicket);close(1,"elite");
 } catch (...) {
  auto& cleanup=cases.back()["failureCleanup"];
  cleanup={{"attempted",true},{"before",state()}};
  try {
   if(std::find(fullPipeChildObservers.begin(),fullPipeChildObservers.end(),successorObservation)!=fullPipeChildObservers.end()) {
    inputHostStop("offline-owned-cleanup");
    cleanup["receipt"]=fullPipeChildOutcome(successorObservation,successorReceipt);
    fullPipeDrainQueuedLifecycle();
   }
   cleanup["after"]=state();cleanup["observerCount"]=fullPipeChildObservers.size();
  } catch(const std::exception& e){cleanup["error"]=e.what();}
  throw;
 }
 require(fullPipeChildObservers.empty()&&!g_inputHostProcess&&fullPipeSpawnCount==beforeSpawns+2,
  "the successor's independent OFF leaves both Hosts returned");
 cases.back()["passed"]=true;fixtureFullPipe=false;
}
