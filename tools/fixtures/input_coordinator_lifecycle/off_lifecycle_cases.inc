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
 fixtureOffCheckpoint(("original-off-case-entry/"+variant).c_str());
 seed();fixtureOffCheckpoint("original-off-seed-returned");fixtureFullPipe=true;fullPipeFault="none";g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 if(!msi){g_machineIdentity.family=stale?YmccFamily::Unknown:YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer=stale?"PC":"ASUSTEK COMPUTER INC.";g_machineIdentity.product=stale?"unregistered-desktop":"ROG Ally X";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=1;}if(stale)fixturePcPowerCapabilities=true;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 cases.push_back({{"name","OFF-LIFECYCLE/"+variant},{"controlledFamily",msi?"MSI":stale?"PC":"ROG"}});
 const auto spawns=fullPipeSpawnCount,observers=static_cast<unsigned>(fullPipeChildObservers.size());
 const auto queueDepth=[](){std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);return g_inputHostLifecycleWorkQueue.size();};
 const auto advance=[](std::uint64_t ms){startProtectionClock+=ms;fixturePowerClockOffset+=ms;};
 const auto base=targetDocument("disabled",0),on=targetDocument(msi?"steamdeck":"dualsense-edge",1),off=targetDocument("disabled",2);
 check(queueDepth()==0&&!g_inputHostProcess,"OFF case starts only after earlier owned work has drained");check(sgWriteFileAtomic(YM_SETTINGS_FILE,base.dump()),"OFF case initializes only its private starting settings document");
 check(writeTarget(base,on).value("ok",false),"OFF case enters original ON through real settings admission");fixtureOffCheckpoint("before-original-ON-refresh");inputCaptureRefreshRealStickSettings(true);fixtureOffCheckpoint("after-original-ON-refresh");
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
   fullPipeRogObserveReturnedSource(closing.requestId);
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

#if defined(YMCC_FIXTURE_SD_OFF_SOURCE_LOST)

// Opt-in parameters of the ORIGINAL SD OFF/target-return consumer. Real private
// Host/pipe, BeforeHide reservations, file CAS and quarantine lifecycle execute.
// Only the physical inventory, HidHide end and shared-Reader event inputs below
// are controlled. In particular sourceMask0 is an incident input, not a claim
// that this case ran physical-source reconfirmation or real hardware sleep.
namespace fixtureSdOffSourceLost {
static bool active=false,readbackKnown=true,fired=false;
static unsigned settled=0,interruptAfter=0;
static std::string edge,variant;
static std::vector<std::wstring> inventory,hidden;
static std::map<std::wstring,unsigned> unhideCalls;
static json observations=json::array();
static constexpr const char* root="USB\\VID_045E&PID_028E\\FIXTURE-SD-OFF-111700";
}
static bool fixtureSdOffSourceLostActive(){return fixtureSdOffSourceLost::active;}
// Defined by the existing runner's shared Native Reader adapter, never by a new
// power clock/provider here. Unknown event names must fail, not become Ready.
static void fixtureSdOffSharedPowerEdge(const std::string&);
static bool fixtureSdOffSourceLostSnapshot(HidHideVisibilitySnapshot& snapshot){
 using namespace fixtureSdOffSourceLost;snapshot={};snapshot.valid=readbackKnown;snapshot.cloaking=true;
 snapshot.hidden=hidden;return readbackKnown;
}
static bool fixtureSdOffSourceLostCli(const std::wstring& command,std::string* output,DWORD* code){
 using namespace fixtureSdOffSourceLost;if(output)output->clear();if(code)*code=ERROR_SUCCESS;
 const std::wstring prefix=L"--dev-unhide \"";
 if(command.starts_with(prefix)&&command.ends_with(L"\"")){
  const auto target=command.substr(prefix.size(),command.size()-prefix.size()-1);
  if(std::find(inventory.begin(),inventory.end(),target)==inventory.end())throw std::logic_error("sd-off-CLI-not-exact-owned-path");
  ++unhideCalls[target];hidden.erase(std::remove(hidden.begin(),hidden.end(),target),hidden.end());
  observations.push_back({{"leaf","exact-private-HidHide-unhide"},{"path",W2U(target)}});return true;
 }
 ++forbiddenIo;throw std::logic_error("sd-off-unexpected-CLI-command:"+W2U(command));
}
static ymcc::targetReturn::physicalPath::Result fixtureSdOffSourceLostPath(const std::string& expected,const std::string& target){
 using namespace fixtureSdOffSourceLost;
 struct Reader {
  std::wstring rootId,child;bool known=false;
  CONFIGRET locate(PDEVINST out,DEVINSTID_W id,ULONG){
   if(!id){*out=3;return CR_SUCCESS;}if(rootId==id){*out=1;return CR_SUCCESS;}
   if(known&&child==id){*out=2;return CR_SUCCESS;}return CR_NO_SUCH_DEVNODE;
  }
  CONFIGRET deviceId(DEVINST node,PWCHAR out,ULONG count,ULONG){
   const std::wstring tree=L"HTREE\\ROOT\\PRIVATE-SD-OFF";
   const auto* value=node==1?&rootId:node==2&&known?&child:node==3?&tree:nullptr;
   if(!value)return CR_NO_SUCH_DEVNODE;if(count<=value->size())return CR_BUFFER_SMALL;
   std::copy(value->begin(),value->end(),out);out[value->size()]=0;return CR_SUCCESS;
  }
  CONFIGRET parent(PDEVINST out,DEVINST node,ULONG){
   if(node==2){*out=1;return CR_SUCCESS;}if(node==1){*out=3;return CR_SUCCESS;}return CR_NO_SUCH_DEVNODE;
  }
 } reader{U2W(root),U2W(target),std::find(inventory.begin(),inventory.end(),U2W(target))!=inventory.end()};
 const auto result=ymcc::targetReturn::physicalPath::verify(expected,target,reader);
 observations.push_back({{"leaf","actual-private-CM-ancestry-verifier"},{"target",target},
  {"state",ymcc::targetReturn::physicalPath::stateName(result.state)}});return result;
}
static void fixtureSdOffSourceLostObserve(const char* event,const json& detail){
 using namespace fixtureSdOffSourceLost;if(!active||!event||!detail.is_object())return;
 if(std::strcmp(event,"input-host-target-return-record")==0&&detail.value("phase","")=="physical-path-settled"){
  ++settled;observations.push_back({{"stage","actual-path-CAS-returned"},{"ordinal",settled},
   {"path",detail.value("pathInstanceId","")},{"revision",detail.value("revision",0ULL)}});
  if(!fired&&interruptAfter&&settled==interruptAfter){
   fired=true;
   if(edge=="intent")g_inputHostVirtualIntentRevision.fetch_add(1,std::memory_order_acq_rel);
   else fixtureSdOffSharedPowerEdge(edge);
  }
 }
 if(!fired&&variant.starts_with("post-log-")&&std::strcmp(event,"hidhide.p-hid-restore")==0&&
    detail.value("ownedSdPostDurableCurrent",false)){
  fired=true;if(edge=="intent")g_inputHostVirtualIntentRevision.fetch_add(1,std::memory_order_acq_rel);
  else fixtureSdOffSharedPowerEdge(edge);
 }
}
static void fullPipeSdOffSourceLostCase(const std::string& selected){
 namespace f=fixtureSdOffSourceLost;namespace duty=ymcc::targetReturn;
 seed();fixtureFullPipe=true;fullPipeFault="none";fixturePcPowerCapabilities=true;
 g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="FIXTURE-PC";g_machineIdentity.product="SD-OFF-common-consumer";
 g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;
 g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 fixtureSdOffSharedPowerEdge("ready");
 f::active=false;f::readbackKnown=true;f::fired=false;f::settled=0;f::interruptAfter=0;f::edge.clear();f::variant=selected;
 f::observations=json::array();f::unhideCalls.clear();
 f::inventory={U2W(f::root),L"HID\\VID_045E&PID_028E&IG_00\\PRIVATE-SD-OFF-0",
  L"HID\\VID_045E&PID_028E&IG_01\\PRIVATE-SD-OFF-1",L"HID\\VID_045E&PID_028E&IG_02\\PRIVATE-SD-OFF-2"};
 f::hidden.clear();
 cases.push_back({{"name","OFF-LIFECYCLE/SD-source-lost/"+selected},{"subvariantOf","OFF-LIFECYCLE"},
  {"controlledFamily","common SD OFF consumer with GPD incident device shape"},{"realManagedChild",true},
  {"physicalDeviceIo",false},{"sourceReconfirmExecuted",false},{"realSleepExecuted",false}});
 const auto spawns=fullPipeSpawnCount;const auto observers=fullPipeChildObservers.size();
 const auto base=targetDocument("disabled",0),on=targetDocument("steamdeck",1),off=targetDocument("disabled",2);
 check(sgWriteFileAtomic(YM_SETTINGS_FILE,base.dump())&&writeTarget(base,on).value("ok",false),"SD OFF case uses original private settings IPC ON admission");
 inputCaptureRefreshRealStickSettings(true);check(inputHostQueueLifecycleWork("initial-target-admission"),"SD OFF queues original target lifecycle");fullPipeDrainQueuedLifecycle();
 check(fullPipeSpawnCount==spawns+1&&g_inputHostProcess&&g_inputHostPrepared,"SD OFF owns one actual prepared managed SD Host");
 if(!g_inputHostProcess)throw std::logic_error("sd-off-real-managed-Host-setup-failed");
 HANDLE oldObserver=nullptr;check(DuplicateHandle(GetCurrentProcess(),g_inputHostProcess,GetCurrentProcess(),&oldObserver,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,"SD OFF retains exact old process observation through return");
 if(!oldObserver)throw std::logic_error("sd-off-old-observer-duplicate-failed");
 fixtureRuntimeRecoveryOldHost=oldObserver;fixtureRuntimeRecoveryOldOwner=inputHostTargetReturnCaptureOwner(g_inputHostProcess);
 fixtureRuntimeRecoveryOsEnabled=true;fixtureRuntimeRecoveryCensus="absent";fixtureRuntimeRecoveryCensusCalls=0;
 struct Scope {
  HANDLE observer,denial=nullptr;~Scope(){fixtureSdOffSourceLost::active=false;fixtureRuntimeRecoveryOldHost=nullptr;
   fixtureRuntimeRecoveryOldOwner.reset();fixtureRuntimeRecoveryOsEnabled=false;if(denial)CloseHandle(denial);if(observer)CloseHandle(observer);}
 } scope{oldObserver};
 const auto originalEpoch=g_inputHostEpoch;const auto originalRun=g_inputHostRunId;
 // The actual reservation algorithm records every reached fake hide. No state
 // session, origin or persisted obligation is fabricated by this scenario.
 HidHidePhysicalIdentity physical;physical.resolved=true;physical.sourcePath="fixture://sd-off-physical";
 physical.deviceInstanceId=W2U(f::inventory[1]);physical.baseContainerDeviceInstanceId=f::root;
 physical.vendorId=0x045e;physical.productId=0x028e;physical.usagePage=1;physical.usage=5;
 physical.physicalIdentity=physical.deviceInstanceId+"|"+physical.baseContainerDeviceInstanceId+"|vid_1118|pid_654";
 f::active=true;
 {std::lock_guard<InputHostMutex> host(g_inputHostMx);
  HidHideVisibilitySnapshot baseline;baseline.valid=true;
  g_hidHideVisibilityJournal={};auto& journal=g_hidHideVisibilityJournal;
  journal.transactionId=originalRun;journal.physical=physical;journal.physicalSources={physical};journal.before=baseline;journal.active=true;
  for(const auto& target:f::inventory){bool reserved=false;
   const bool admitted=inputHostTargetReturnBeforeHideLocked({physical},baseline,baseline,W2U(target),originalRun,&reserved);
   check(admitted&&reserved,"real BeforeHide reserves one exact owned physical path in the private Session before fake hide");
   if(!admitted||!reserved)throw std::logic_error("sd-off-real-physical-reservation-rejected");
   journal.hiddenByUs.push_back(target);f::hidden.push_back(target);
  }
 }
 const auto pathCount=[](){std::size_t count=0;const auto& session=ymcc::inputHost::targetReturnRuntime::state.session;
  if(!session||!session->record())throw std::logic_error("sd-off-no-actual-record");
  for(const auto& group:session->record()->obligations)count+=group.paths.size();return count;};
 check(pathCount()==4,"four actual durable owned-path reservations exist before incident OFF");
 f::active=false;fullPipeWaitForPlayerLock(on,off);
 // Recorded incident: physical slot is online while its admission was lost.
 g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=0;g_xinputConnectedMask=1;g_xinputAdmittedMask=0;
 g_startEntrySeam.hidHideRestoreResult=false;
 check(writeTarget(on,off).value("ok",false),"original IPC admits player OFF after original ON lock");inputCaptureRefreshRealStickSettings(true);
 const auto close=g_virtualStartProtection.view(startProtectionClock);fullPipeDrainQueuedLifecycle();
 const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;
 check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!g_inputHostProcess&&
  g_inputHostReleaseQuarantined&&g_hidHideVisibilityJournal.active&&pathCount()==4,"actual Stop exits its private Host and retains unreturned physical duty and quarantine");
 f::active=true;g_startEntrySeam.active=false;
 fixtureRuntimeRecoveryCensus=selected=="PnP-present"?"present":selected=="PnP-unknown"?"unknown":"absent";
 f::readbackKnown=selected!="readback-unknown";
 if(selected=="publication"){f::interruptAfter=1;f::edge="publication";}
 if(selected.starts_with("CAS")){
  if(selected.size()<7||selected[3]!='-'||selected[4]<'1'||selected[4]>'4')throw std::logic_error("sd-off-invalid-CAS-selector");
  f::interruptAfter=static_cast<unsigned>(selected[4]-'0');f::edge=selected.substr(6);
 }
 if(selected.starts_with("post-log-"))f::edge=selected.substr(9);
 if(selected=="record-write-denied"){
  scope.denial=CreateFileW(fixtureTargetReturnRecordPath().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  check(scope.denial!=INVALID_HANDLE_VALUE,"actual private record no-delete-sharing handle provides a real durable-commit denial");
  if(scope.denial==INVALID_HANDLE_VALUE){scope.denial=nullptr;throw std::logic_error("sd-off-private-file-denial-setup-failed");}
 }
 fullPipeQuarantineProbe();
 const bool negative=selected!="absent"&&selected!="publication";
 if(negative){
  check(g_inputHostReleaseQuarantined&&g_hidHideVisibilityJournal.active&&g_inputHostEpoch==originalEpoch&&
   g_inputHostRunId==originalRun,"absent proof, readback or final scope failure retains the exact old return owner");
  if(f::interruptAfter||selected.starts_with("post-log-")){
   check(f::fired,"requested interruption was reached at actual path-CAS/log boundary");
   check(f::settled==(f::interruptAfter?f::interruptAfter:4u)&&pathCount()==4-f::settled,
    "real committed paths survive interruption and only remaining duties stay on disk");
  }else check(f::settled==0&&pathCount()==4,"unproven census/readback cannot consume any real path duty");
  const auto alreadySettled=f::settled;const auto callsBefore=f::unhideCalls;
  if(scope.denial){check(CloseHandle(scope.denial)!=FALSE,"private commit denial is returned before original retry");scope.denial=nullptr;}
  fixtureRuntimeRecoveryCensus="absent";f::readbackKnown=true;fixtureSdOffSharedPowerEdge("ready");
  // New intent is a controlled publisher edge, not a UI ON request. OFF remains
  // the configured target and must never be silently reopened by cleanup.
  fullPipeQuarantineProbe();
  for(const auto& row:f::observations)if(row.value("stage","")=="actual-path-CAS-returned"&&row.value("ordinal",0u)<=alreadySettled){
   const auto target=U2W(row.value("path",std::string{}));
   check(f::unhideCalls[target]==callsBefore.at(target),"already durably settled paths do not receive another unhide on continuation");
  }
 }
 check(!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&!g_inputHostProcess&&
  !g_inputHostPrepared&&!g_inputHostNeutralized&&pathCount()==0&&f::settled==4,"original lifecycle finishes actual four-path durable return and clears the old tuple only after proof");
 check(fullPipeSpawnCount==spawns+1&&fullPipeChildObservers.size()==observers&&inputHostConfiguredPersona()=="disabled",
  "OFF continuation creates no Host and preserves the player's disabled target");
 check(fixtureRuntimeRecoveryCensusCalls>0,"stale-mask OFF reaches the actual complete target census, not an absence flag");
 if(selected=="publication")check(f::fired,"pure diagnostic publication advanced at the actual first path-CAS boundary");
 startProtectionClock+=11000;fixturePowerClockOffset+=11000;fullPipeVerificationStep();
 check(fullPipeOffFinalCounts(close.requestId)==std::make_pair(1u,0u),
  "original OFF publishes exactly one success after actual cleanup without renewing the request deadline");
 cases.back()["pathObservations"]=f::observations;cases.back()["censusCalls"]=fixtureRuntimeRecoveryCensusCalls;
 cases.back()["unhideCalls"]=json::object();for(const auto& item:f::unhideCalls)cases.back()["unhideCalls"][W2U(item.first)]=item.second;
 cases.back()["finalProtection"]=fullPipeProtectionState();cases.back()["nativeEvents"]=fixtureEvents;
 fixtureFullPipe=false;g_startEntrySeam.active=true;
}
static std::vector<std::string> fixtureSdOffSourceLostVariantNames(){
 std::vector<std::string> variants={"absent","PnP-present","PnP-unknown","readback-unknown","record-write-denied","publication"};
 for(unsigned path=1;path<=4;++path)for(const auto* event:{"notification","generation","intent"})
  variants.push_back("CAS-"+std::to_string(path)+"-"+event);
 for(const auto* event:{"notification","generation","intent"})variants.push_back(std::string("post-log-")+event);
 return variants;
}
static void fullPipeSdOffSourceLostCases(const std::string& only={}){
 const auto variants=fixtureSdOffSourceLostVariantNames();
 if(!only.empty()&&std::find(variants.begin(),variants.end(),only)==variants.end())throw std::invalid_argument("unknown-sd-off-source-lost-variant");
 for(const auto& selected:variants){if(!only.empty()&&selected!=only)continue;
  fullPipeSdOffSourceLostCase(selected);if(!failures.empty())return;
 }
}

#endif // explicit optional SD OFF source-loss consumer only
