// Included after the existing full-pipe Stop/protection helpers. Only these
// explicit normal-return call sites supply a NEW physical observation. Neither
// Stop, receipt collection nor the global candidate checker discharges MSI debt.
static std::pair<unsigned,unsigned> fullPipeOffFinalCounts(std::uint64_t request){
 unsigned success=0,failure=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-switch-verification"&&event["detail"].value("stage","")=="final"&&event["detail"].value("requestId",0ULL)==request){success+=event["detail"].value("outcome","")=="success";failure+=event["detail"].value("outcome","")=="failure";}return {success,failure};
}
static json fullPipeOffDebtRows(std::uint64_t episode,const char* action){
 json rows=json::array();for(const auto& event:fixtureEvents)if(event.value("event","")=="msi-claw-return-debt"&&event["detail"].value("returnDebtEpisodeId",0ULL)==episode&&event["detail"].value("action","")==action)rows.push_back(event["detail"]);return rows;
}
static void fullPipeMsiObserveReleasedPhysicalReturn(const json& child,const char* where){
 const auto episode=g_msiDinputReturnDebtEpisode.load();const auto request=g_virtualStartProtection.view(startProtectionClock).requestId;
 const bool returned=msiClawKnownBoardAuthority()&&child.value("nativeObservedProcessExit",false)&&child.value("nativeObservedExitCode",999)==0&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true)&&!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&!g_msiSteamDeckReleaseDebt.required()&&msiDinputDeviceTargetAbsent()&&!g_msiDinputRouteDinputArmed.load();
 check(returned,"MSI physical return observation requires exact old child exit and released local DInput/virtual/visibility ownership");
 check(episode&&g_msiDinputReturnOwed.load(),"normal MSI Stop retains its real mode-return obligation until qualified physical observation");
 if(!returned||!episode||!g_msiDinputReturnOwed.load())return;
 const auto before=fullPipeProtectionState();check(before.value("persona","")=="disabled"&&before.value("phase","")=="verifying"&&before.value("candidateOutcome","")=="failure"&&fullPipeOffFinalCounts(request)==std::make_pair(0u,0u),"MODE1 receipt creates only an unproven OFF candidate before physical return is observed");
 // Controlled leaf: the JUST RELEASED local MSI interface has reappeared as
 // physical slot0 AFTER its exact old Host exited. This is not an arbitrary pad,
 // a cached pre-Stop sample, a MODE1 write acknowledgement, or a matched-token edit.
 g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;
 auto sample=fullPipeReadNow(1);{std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);const auto old=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);sample.readContinuityGeneration=ymcc::input::recoveryHealth::nextContinuity(old,inputHostRecoveryHealthSource(&sample,true),GetTickCount64());g_inputCaptureGamepadSnapshot=sample;}
 const auto physical=xinputPhysicalConnectedMask();const auto admitted=physical&g_xinputPhysicalSourceMask;g_xinputAdmittedMask=admitted;
 const bool qualified=msiXInputReAdmissionQualified(!g_msiDinputRouteDinputArmed.load(),msiClawKnownBoardAuthority(),physical!=0)&&msiCrashPhysicalXinputSnapshotEligible(currentPowerGeneration(),g_xinputTopologyEpoch.load());
 check(qualified&&admitted==1&&sample.readPowerGeneration==currentPowerGeneration()&&sample.readTopologyGeneration==g_xinputTopologyEpoch.load(),"fresh released-local-interface observation passes original MSI route/board/physical-XInput qualification");
 bool discharged=false;if(qualified&&admitted==1){
  // The hardware seam skips durable bookkeeping during MODE2. Temporarily
  // expose ONLY the real private-file initializer and discharge consumer; no
  // device operation runs here. Unknown/invalid persistence still fails closed.
  struct RestoreSeam{bool active;~RestoreSeam(){g_startEntrySeam.active=active;}} restore{g_startEntrySeam.active};g_startEntrySeam.active=false;
  msiCrashRecoveryInitialize();discharged=msiDinputTryDischargeReturnDebt(false,qualified,admitted,where);
 }
 const auto rows=fullPipeOffDebtRows(episode,"discharged");
 check(discharged&&!g_msiDinputReturnOwed.load()&&rows.size()==1&&rows[0].value("basis","")=="xinput-readmitted"&&rows[0].value("where","")==where,"sole production discharge consumer logs exactly one qualified XInput settlement for this real mode-return episode");
 check(fullPipeProtectionState()==before&&fullPipeOffFinalCounts(request)==std::make_pair(0u,0u),"qualified physical return does not rewrite the failure candidate or publish an early final result");
 if(!cases.back().contains("msiPhysicalReturnObservations"))cases.back()["msiPhysicalReturnObservations"]=json::array();
 cases.back()["msiPhysicalReturnObservations"].push_back({{"where",where},{"returnDebtEpisodeId",episode},{"requestId",request},{"oldHostExited",child.value("nativeObservedProcessExit",false)},{"managedInterfaceReleased",returned},{"newPhysicalReadSequence",sample.readAttemptSequence},{"readPower",sample.readPowerGeneration},{"readTopology",sample.readTopologyGeneration},{"qualified",qualified},{"discharged",discharged},{"retainedDebtRows",fullPipeOffDebtRows(episode,"retained")},{"dischargedDebtRows",rows}});
}
static void fullPipeCheckUniqueOffFinal(std::uint64_t request,bool success){
 const auto view=g_virtualStartProtection.view(startProtectionClock);const auto expected=success?std::make_pair(1u,0u):std::make_pair(0u,1u);
 check(view.requestId==request&&view.persona=="disabled"&&view.phase==(success?"success":"failure")&&!view.locked&&fullPipeOffFinalCounts(request)==expected,"actual OFF verifier publishes exactly one expected final and no contradictory outcome");
 const auto before=fullPipeProtectionState();inputHostStop("virtual-target-disabled");fullPipeVerificationStep();
 check(fullPipeProtectionState()==before&&fullPipeOffFinalCounts(request)==expected,"repeated actual Stop and verification cannot renew or contradict the unique OFF final");
}
static void fullPipeOffLifecycleCase(const std::string& variant){
 const bool msi=variant=="MSI-return-unproven",stale=variant=="expired-OFF-after-expired-new-ON";
 seed();fixtureFullPipe=true;fullPipeFault="none";g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 if(!msi){g_machineIdentity.family=stale?YmccFamily::Unknown:YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer=stale?"PC":"ASUSTEK COMPUTER INC.";g_machineIdentity.product=stale?"unregistered-desktop":"ROG Ally X";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=1;}if(stale)fixturePcPowerCapabilities=true;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 cases.push_back({{"name","OFF-LIFECYCLE/"+variant},{"controlledFamily",msi?"MSI":stale?"PC":"ROG"}});
 const auto spawns=fullPipeSpawnCount,observers=static_cast<unsigned>(fullPipeChildObservers.size());
 const auto queueDepth=[](){std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);return g_inputHostLifecycleWorkQueue.size();};
 const auto advance=[](std::uint64_t ms){startProtectionClock+=ms;fixturePowerClockOffset+=ms;};
 const auto base=targetDocument("disabled",0),on=targetDocument(msi?"steamdeck":"dualsense-edge",1),off=targetDocument("disabled",2);
 check(queueDepth()==0&&!g_inputHostProcess,"OFF case starts only after earlier owned work has drained");check(sgWriteFileAtomic(YM_SETTINGS_FILE,base.dump()),"OFF case initializes only its private starting settings document");
 check(writeTarget(base,on).value("ok",false),"OFF case enters original ON through real settings admission");inputCaptureRefreshRealStickSettings(true);
 if(!stale){check(inputHostQueueLifecycleWork("initial-target-admission"),"OFF case queues real initial target work");fullPipeDrainQueuedLifecycle();check(fullPipeSpawnCount==spawns+1&&g_inputHostProcess&&g_inputHostPrepared,"OFF case owns an actual prepared managed Host before exercising return");}
 fullPipeWaitForPlayerLock(on,off);if(!msi&&!stale)g_startEntrySeam.hidHideRestoreResult=false;
 check(writeTarget(on,off).value("ok",false),"real player OFF is admitted only after original ON lock finishes");inputCaptureRefreshRealStickSettings(true);
 const auto closing=g_virtualStartProtection.view(startProtectionClock);const auto closeAt=startProtectionClock;check(closing.persona=="disabled"&&closing.locked&&queueDepth()==1,"actual OFF consumer queues one original closing duty");
 if(stale){
  advance(closing.remainingMs);fullPipeVerificationStep();check(g_virtualStartProtection.view(startProtectionClock).phase=="expired"&&queueDepth()==1,"original OFF expires without consuming its queued cleanup work");
  const auto newer=targetDocument("steamdeck",3);check(writeTarget(off,newer).value("ok",false),"actual writeTarget admits newer ON after old OFF hard expiry");inputCaptureRefreshRealStickSettings(true);
  const auto replacement=g_virtualStartProtection.view(startProtectionClock);advance(replacement.remainingMs);fullPipeVerificationStep();const auto before=fullPipeProtectionState();
  check(replacement.requestId!=closing.requestId&&before.value("phase","")=="expired"&&queueDepth()==1,"new ON also passes its own thirty-second deadline before OLD OFF dispatch");
  fullPipeDrainQueuedLifecycle();
  bool started=false;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-lifecycle-work")started=started||event["detail"].value("action","")=="start-or-persona-reconcile";
  check(!started&&fullPipeSpawnCount==spawns&&!g_inputHostProcess&&!g_inputHostPrepared&&fullPipeProtectionState()==before,"expired old OFF cannot take generic Start through an also-expired newer ON");
  check(!started&&fullPipeOffFinalCounts(closing.requestId)==std::make_pair(0u,1u)&&fullPipeOffFinalCounts(replacement.requestId)==std::make_pair(0u,1u),"old OFF logs no Start dispatch and cannot add success or duplicate either expired request final");
  const auto finalOff=targetDocument("disabled",4);check(writeTarget(newer,finalOff).value("ok",false),"stale OFF case finishes through a fresh actual player OFF");inputCaptureRefreshRealStickSettings(true);const auto finalRequest=g_virtualStartProtection.view(startProtectionClock).requestId;fullPipeDrainQueuedLifecycle();fullPipeCheckStopCandidate(finalRequest,"success");
  for(unsigned n=0;n<2&&fullPipeChildObservers.size()>observers;++n)cases.back()["unexpectedManagedCleanup"]=fullPipeChildOutcome();
  advance(11000);fullPipeVerificationStep();fullPipeCheckUniqueOffFinal(finalRequest,true);
  check(fullPipeSpawnCount==spawns&&!g_inputHostProcess&&!g_inputHostPrepared&&fullPipeChildObservers.size()==observers,"fresh OFF fully drains cleanup and never creates a Host");
  cases.back()["staleOffProof"]={{"originalOffRequest",closing.requestId},{"newOnRequest",replacement.requestId},{"beforeOldDispatch",before},{"oldOffFinals",fullPipeOffFinalCounts(closing.requestId)},{"newOnFinals",fullPipeOffFinalCounts(replacement.requestId)}};
 }else{
  fullPipeDrainQueuedLifecycle();fullPipeCheckStopCandidate(closing.requestId,"failure");const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;
  check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true)&&!g_inputHostProcess,"OFF obligation is tested after actual old child exit and SDK resource disposal");
  if(msi){
   const auto episode=g_msiDinputReturnDebtEpisode.load();check(episode&&g_msiDinputReturnOwed.load()&&!g_msiDinputRouteDinputArmed.load()&&msiDinputDeviceTargetAbsent(),"real MSI Stop releases the managed interface but MODE1 acknowledgement retains its mode-return debt");
   bool receiptRetained=false;for(const auto& event:fixtureEvents)if(event.value("event","")=="msi-claw-gamepad-mode"&&event["detail"].value("returnDebtEpisodeId",0ULL)==episode&&event["detail"].value("action","")=="restore-xinput")receiptRetained=receiptRetained||event["detail"].value("returnDebtRetained",false);
   check(receiptRetained,"actual MODE1 restore log explicitly retains the unproven return obligation");
   advance(1001);const auto physical=xinputPhysicalConnectedMask();const bool qualified=msiXInputReAdmissionQualified(!g_msiDinputRouteDinputArmed.load(),msiClawKnownBoardAuthority(),physical!=0);
   check(!qualified&&physical==0&&!msiDinputTryDischargeReturnDebt(false,qualified,0,"fixture-OFF-no-physical-return-observation"),"missing physical observation fails real MSI qualification and the sole discharge consumer retains debt");
   const auto retained=fullPipeOffDebtRows(episode,"retained");check(!retained.empty()&&retained.back().value("returnDebtRetained",false)&&retained.back().value("basis","")=="release-unproven"&&fullPipeOffDebtRows(episode,"discharged").empty(),"same-episode native debt log remains release-unproven with no discharge evidence");
   advance(9999);fullPipeVerificationStep();
   check(g_virtualStartProtection.view(startProtectionClock).phase=="failure"&&fullPipeOffFinalCounts(closing.requestId)==std::make_pair(0u,1u),"unproven MSI return debt cannot finalize OFF success after eleven seconds");fullPipeCheckUniqueOffFinal(closing.requestId,false);
   advance(19001);fullPipeVerificationStep();fullPipeCheckUniqueOffFinal(closing.requestId,false);
   check(g_msiDinputReturnOwed.load()&&fullPipeOffDebtRows(episode,"discharged").empty()&&fullPipeSpawnCount==spawns+1,"elapsed inspection and original hard deadline never forgive unproven MSI debt or create another Host");
   cases.back()["retainedDebtProof"]={{"returnDebtEpisodeId",episode},{"retained",fullPipeOffDebtRows(episode,"retained")},{"discharged",fullPipeOffDebtRows(episode,"discharged")}};
  }else{
   check(g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&g_inputHostPrepared,"actual failed HidHide restore retains quarantine and StartBlocked after child exit");const auto restores=fullPipeRestoreCount();
   // NEW physical endpoint leaf after old Host exit; no ownership/debt flag edit.
   g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;g_startEntrySeam.hidHideRestoreResult=true;fullPipeQuarantineProbe();
   check(!g_inputHostReleaseQuarantined&&!g_inputHostPrepared&&!g_inputHostNeutralized&&!g_hidHideVisibilityJournal.active&&!g_msiSteamDeckReleaseDebt.required()&&g_inputHostStartBlocked&&fullPipeRestoreCount()==restores+1,"official quarantine retry fully returns ownership while retaining its real StartBlocked fence");
   bool restored=false;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-quarantine-cleared")restored=restored||event["detail"].value("status","")=="physical-restore-readback-confirmed";check(restored,"real quarantine log proves physical restore and retired ownership before OFF success");
   advance(11000);fullPipeVerificationStep();fullPipeCheckUniqueOffFinal(closing.requestId,true);
   check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&fullPipeOffFinalCounts(closing.requestId)==std::make_pair(1u,0u)&&g_inputHostStartBlocked&&fullPipeSpawnCount==spawns+1&&!g_inputHostProcess,"fully returned ROG OFF succeeds with StartBlocked retained and no automatic Start");
  }
  check(startProtectionClock>=closeAt&&fullPipeChildObservers.size()==observers&&!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"OFF case leaves actual child and virtual/visibility ownership returned without clearing mode-return debt");
 }
 cases.back()["finalProtection"]=fullPipeProtectionState();cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;
}
static void fullPipeOffLifecycleCases(){
 fullPipeOffLifecycleCase("ROG-returned-with-StartBlocked");
 fullPipeOffLifecycleCase("expired-OFF-after-expired-new-ON");
 // Keep the deliberately unresolved real MSI obligation at the end; no next
 // process-equivalent seed or broad cleanup can erase its failure evidence.
 fullPipeOffLifecycleCase("MSI-return-unproven");
}
