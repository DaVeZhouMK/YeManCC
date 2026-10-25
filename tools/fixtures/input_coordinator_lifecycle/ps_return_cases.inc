// Cross the actual accepted PS ownership, real failed child release and the
// existing quarantine consumer. The retained pin below is an observation witness,
// never injected back into production ownership or used to clear a duty.
static void fullPipePsReturnEvidenceCase(DWORD connected,PsWakeProbe expected,bool blockedStore=false){
 seed();fixtureFullPipe=true;fullPipeFault="output-stop-once";g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="ROG Ally X";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=1;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 const std::string verdict=expected==PsWakeProbe::Present?"present":expected==PsWakeProbe::Unknown?"unknown":expected==PsWakeProbe::Foreign?"foreign-peer":"absent";
 cases.push_back({{"name","PS-RETURN/ROG/owned-HID-"+verdict+"/xinput-mask-"+std::to_string(connected)+(blockedStore?"/durable-store-blocked-once":"")},{"controlledFamily","ROG"}});
 const auto spawns=fullPipeSpawnCount;const auto on=targetDocument("dualsense-edge",0),off=targetDocument("disabled",1);check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"PS return witness starts with a private real settings request");inputCaptureRefreshRealStickSettings(true);
 check(inputHostStart()&&fullPipeSpawnCount==spawns+1,"PS return witness creates exactly one actual managed target");const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto pin=g_psEndpointOwnership.snapshot();
 check(pin.capable&&pin.valid&&!pin.rootInstanceId.empty()&&!pin.hidInstanceIds.empty(),"actual accepted managed PREPARE supplies the exact old PS ownership witness");
 const auto currentDuty=[&](){const auto loaded=ymcc::targetReturn::load(fixtureTargetReturnRecordPath(),"fixture-machine");
  return loaded.record&&std::any_of(loaded.record->obligations.begin(),loaded.record->obligations.end(),
   [&](const auto& item){return item.origin.runId==run&&item.origin.epoch==epoch&&item.targetMayExist;});};
 check(currentDuty(),"actual PS target has a committed current target-return duty before failed release");

 // Hold an incomplete PnP observation through the initial Stop, before the
 // later scenario leaf. Successful SDK disposal alone is not that leaf.
 fixturePsReturnManualTopology=true;fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"},ERROR_ACCESS_DENIED);
 inputHostStop("fault-stop:ps-return-ownership-test");const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;
 check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"actual old child naturally exits and disposes SDK resources before delayed-PnP evidence is supplied");
 check(g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&!g_psEndpointOwnership.snapshot().valid,"real failed release retains quarantine while runtime PS binding is revoked");
 const auto configure=[&](PsWakeProbe kind){fixturePsReadConfig(kind==PsWakeProbe::Absent?std::vector<std::wstring>{}:kind==PsWakeProbe::Foreign?std::vector<std::wstring>{L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OTHER-PROCESS-B"}:std::vector<std::wstring>{L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"},kind==PsWakeProbe::Unknown?ERROR_ACCESS_DENIED:ERROR_NO_MORE_ITEMS);};
 configure(expected);PsWakeEndpointProbe witness;{FixturePsReadScope scope;witness=psWakeProbeOwnEndpoint(pin);}check(witness.kind==expected,"actual owned-PS reader establishes the independent old-target witness before quarantine dispatch");
 g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=connected;const auto restores=fullPipeRestoreCount();const auto before=state();const auto queries=fixturePsOpenCalls;
 HANDLE heldRecord=INVALID_HANDLE_VALUE;
 if(blockedStore){heldRecord=CreateFileW(fixtureTargetReturnRecordPath().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  check(heldRecord!=INVALID_HANDLE_VALUE,"private target record handle blocks atomic replacement without changing stored bytes");}
 {FixturePsReadScope scope;fullPipeQuarantineProbe();}
 if(blockedStore){
  check(currentDuty()&&g_inputHostReleaseQuarantined&&g_psEndpointOwnership.retirementRequired()&&g_inputHostRunId==run&&g_inputHostEpoch==epoch,
   "failed durable PS settlement retains old target duty quarantine and exact retirement witness");
  if(heldRecord!=INVALID_HANDLE_VALUE)CloseHandle(heldRecord);
 }
 const bool settled=!g_inputHostReleaseQuarantined&&g_inputHostEpoch==0&&g_inputHostRunId.empty();
 if(blockedStore)check(!settled&&fullPipeRestoreCount()==restores+1,"physical return alone cannot clear a failed durable target settlement");
 else if(expected==PsWakeProbe::Absent||expected==PsWakeProbe::Foreign)check(settled&&fullPipeRestoreCount()==restores+1,"complete owned-PS absence allows the actual deferred restore and exact old tuple retirement");
 else check(!settled&&g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&fullPipeRestoreCount()==restores,"old PS owned HID present or unknown must veto XInput-only physical restoration and tuple retirement");
 if(expected==PsWakeProbe::Present||expected==PsWakeProbe::Unknown){
  fixtureExitSweepLists=fixtureExitSweepUnhides=fixtureExitSweepCloakOffs=0;fixtureExitSweep=true;hidHideSweepAllHiddenDevices("app-exit");fixtureExitSweep=false;
  check(!g_inputHostProcess&&g_psEndpointOwnership.retirementRequired()&&fixtureExitSweepLists==0&&fixtureExitSweepUnhides==0&&fixtureExitSweepCloakOffs==0,"final app-exit sweep retains PS PnP return debt even after the exact old child exited");
 }
 if(expected==PsWakeProbe::Present||expected==PsWakeProbe::Unknown||blockedStore)
  check(currentDuty(),"unproved endpoint or uncommitted settlement preserves the exact durable PS origin");
 else check(!currentDuty(),"qualified PS quarantine return consumes its exact durable target duty before tuple clear");
 cases.back()["ownedReturnWitness"]={{"verdict",verdict},{"oldRoot",pin.rootInstanceId},{"oldGeneration",pin.generation},{"queryComplete",witness.enumerationComplete},{"queryError",witness.queryError},{"ownedMatches",witness.ownMatches},{"connectedMask",connected},{"sourceMask",1},{"stateBefore",before},{"stateAfter",state()},{"restoreCallsBefore",restores},{"restoreCallsAfter",fullPipeRestoreCount()},{"psQueriesDuringConsumer",fixturePsOpenCalls-queries}};
 // A later complete observation is a new leaf fact; only actual quarantine code
 // may retire ownership. Never clear quarantine/debt/pins to make cleanup pass.
 unsigned expectedRestoreAttempts=blockedStore?2:1;
 if(expected==PsWakeProbe::Present&&connected==0){
  configure(PsWakeProbe::Absent);g_startEntrySeam.hidHideRestoreResult=false;{FixturePsReadScope scope;fullPipeQuarantineProbe();}
  check(g_inputHostReleaseQuarantined&&g_psEndpointOwnership.retirementRequired()&&fullPipeRestoreCount()==restores+1,"failed physical restore retains the exact PS endpoint return identity for the next probe");
  configure(PsWakeProbe::Present);g_startEntrySeam.hidHideRestoreResult=true;{FixturePsReadScope scope;fullPipeQuarantineProbe();}
  check(g_inputHostReleaseQuarantined&&g_psEndpointOwnership.retirementRequired()&&fullPipeRestoreCount()==restores+1,"renewed owned-HID presence after failed restore cannot bypass retained PS return proof");expectedRestoreAttempts=2;
 }
 configure(PsWakeProbe::Absent);{FixturePsReadScope scope;const auto absent=psWakeProbeOwnEndpoint(pin);check(absent.kind==PsWakeProbe::Absent&&absent.enumerationComplete,"later complete owned-PS absence is independently observed before final cleanup");fullPipeQuarantineProbe();}
 check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&g_inputHostEpoch==0&&g_inputHostRunId.empty()&&fullPipeRestoreCount()==restores+expectedRestoreAttempts,"one actual restore settles the exact retired PS session after qualified absence");
 check(!currentDuty(),"final qualified PS return leaves no stale current target obligation in the real record");
 fullPipeWaitForPlayerLock(on,off);check(writeTarget(on,off).value("ok",false),"PS return witness closes through an actual admitted player OFF");inputCaptureRefreshRealStickSettings(true);const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;fullPipeDrainQueuedLifecycle();fullPipeCheckStopCandidate(ticket,"success");fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));
 check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_hidHideVisibilityJournal.active&&fullPipeSpawnCount==spawns+1,"owned-PS return witness leaves no new target or retained local resources");cases.back()["nativeEvents"]=fixtureEvents;fixturePsReturnManualTopology=false;fixtureFullPipe=false;fullPipeFault="none";
}
static void fullPipePsReturnEvidenceCases(){fullPipePrepareAckLostCase("PC",false,true);for(const DWORD connected:{DWORD{0},DWORD{1}})for(const auto kind:{PsWakeProbe::Present,PsWakeProbe::Unknown,PsWakeProbe::Absent,PsWakeProbe::Foreign})fullPipePsReturnEvidenceCase(connected,kind);fullPipePsReturnEvidenceCase(0,PsWakeProbe::Absent,true);}
