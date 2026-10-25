// Included after full production TU and existing fixture observations.
// Real owned child/process/pipe ends; original SDK/PnP boundaries stay fake.
static std::vector<HANDLE> fullPipeChildObservers;
static std::vector<std::wstring> fullPipeReceipts;
static bool fixturePsAutoReturnTopology(){
 if(fullPipeReceipts.empty())return false;std::ifstream file{fspath::path(fullPipeReceipts.back())};if(!file)return false;
 const auto receipt=json::parse(file,nullptr,false);if(!receipt.is_object()||!receipt.value("finalCleanupOk",false)||receipt.value("hasController",true)||receipt.value("hasContext",true))return false;
 const auto& owner=g_psEndpointOwnership.retirement().owner;if(owner.runId.empty()||!owner.epoch)return false;
 const auto cleanup=receipt.find("stateAfterFinalCleanup");if(cleanup==receipt.end()||!cleanup->is_object())return false;
 const auto tuple=cleanup->find("tuple");if(tuple==cleanup->end()||!tuple->is_object())return false;
 const auto run=tuple->find("RunId"),epoch=tuple->find("Epoch");
 if(run==tuple->end()||!run->is_string()||run->get<std::string>()!=owner.runId||epoch==tuple->end()||!epoch->is_number_integer()||*epoch!=owner.epoch)return false;
 // An absent, malformed or foreign final-cleanup tuple leaves return unknown.
 // Default fake PnP end models complete removal ONLY after the actual owned
 // child wrote successful local disposal. Delayed/unknown cases override it.
 fixturePsPaths.clear();fixturePsOpenFails=false;fixturePsEndError=ERROR_NO_MORE_ITEMS;fixturePsSizeFailure=fixturePsDetailFailure=fixturePsParentFailure=fixturePsIdFailure=-1;fixturePsParentCycle=fixturePsDepthLimit=false;return true;
}
static std::string fullPipeFault="none";
static unsigned fullPipeSpawnCount=0;
static std::wstring fullPipeQuote(const std::wstring& value){if(value.find_first_of(L"\"\r\n")!=std::wstring::npos)throw std::runtime_error("invalid-owned-path");return L"\""+value+L"\"";}
static BOOL fixtureCreateManagedProcess(STARTUPINFOW& si,PROCESS_INFORMATION& pi){
 if(fixtureManagedDll.empty())throw std::runtime_error("explicit-managed-fixture-required");
 const auto receipt=fspath::path(g_yemanccDataDir)/(L"full-pipe-child-"+std::to_wstring(++fullPipeSpawnCount)+L".json");
 const std::wstring dotnet=L"C:\\Program Files\\dotnet\\dotnet.exe";
 std::wstring command=fullPipeQuote(dotnet)+L" "+fullPipeQuote(fixtureManagedDll)+L" --pipe "+fullPipeQuote(g_inputHostPipeName)+L" --parent-pid "+std::to_wstring(GetCurrentProcessId())+L" --receipt "+fullPipeQuote(receipt.wstring())+L" --nonce "+fullPipeQuote(U2W(g_inputHostStartupNonce))+L" --fault "+U2W(fullPipeFault);
 if(!CreateProcessW(dotnet.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return FALSE;
 HANDLE observation=nullptr;if(!DuplicateHandle(GetCurrentProcess(),pi.hProcess,GetCurrentProcess(),&observation,0,FALSE,DUPLICATE_SAME_ACCESS))throw std::runtime_error("private-child-observer-duplicate");
 fullPipeChildObservers.push_back(observation);fullPipeReceipts.push_back(receipt.wstring());return TRUE;
}
static json fullPipeChildOutcome(HANDLE exactChild=nullptr,const std::wstring& exactReceipt={}){
 if(fullPipeChildObservers.empty()||fullPipeReceipts.empty())throw std::logic_error("no-owned-managed-child-to-observe");
 const auto observation=exactChild?exactChild:fullPipeChildObservers.back();check(std::find(fullPipeChildObservers.begin(),fullPipeChildObservers.end(),observation)!=fullPipeChildObservers.end(),"child receipt observes an exact fixture-owned process handle");const auto wait=WaitForSingleObject(observation,8000);check(wait==WAIT_OBJECT_0,"owned managed Host fixture exits naturally after real coordinator Stop");
 DWORD exit=999;if(wait==WAIT_OBJECT_0)GetExitCodeProcess(observation,&exit);
 json child;std::ifstream file(exactReceipt.empty()?fullPipeReceipts.back():exactReceipt);if(file)file>>child;check(child.is_object()&&child.value("status","")=="OBSERVED"&&child["failure"].is_null(),"managed final receipt binds actual loop/session execution and source identities");
 if(child.is_object()){child["nativeObservedProcessExit"]=wait==WAIT_OBJECT_0;child["nativeObservedExitCode"]=exit;}
 CloseHandle(observation);fullPipeChildObservers.erase(std::find(fullPipeChildObservers.begin(),fullPipeChildObservers.end(),observation));return child;
}
// Poll and dispatch actual queued quarantine work. Only the clock becoming due
// is a fixture input; no quarantine/start-blocked state is cleared by the fixture.
static void fullPipeDrainQueuedLifecycle(){
 unsigned runs=0;while(g_inputHostLifecycleWorkRequested.load()&&runs++<32){g_inputHostBuildSettleUntilTick=0;inputHostRunQueuedLifecycleWork();}
 check(!g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load(),"actual queued lifecycle work drains without stranding ownership");
}
static void fullPipeQuarantineProbe(){
 g_inputHostBuildSettleUntilTick=0;g_inputHostLastQuarantineProbeTick=0;
 inputHostReconcileQuarantine();fullPipeDrainQueuedLifecycle();
}
static unsigned fullPipeRestoreCount(){return static_cast<unsigned>(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-restore:quarantine-endpoint-absent"));}
// Execute the current production submit consumer; the sample is a controlled
// physical-read endpoint, while serialization, receipt and Host transport are real.
static InputCaptureGamepadSnapshot fullPipeReadNow(int family){static std::uint64_t attempt=0;InputCaptureGamepadSnapshot v;v.live=true;v.inputSource=family?"xinput":"msi-dinput";v.connectedMask=family?1:0;v.readAttemptSequence=++attempt;v.readStartedTick=v.readCompletedTick=GetTickCount64();v.readPowerGeneration=currentPowerGeneration();v.readTopologyGeneration=g_xinputTopologyEpoch.load();v.readIdentityGeneration=g_xinputSlotIdentityEpoch.load();v.readMsiSourceRevision=g_msiMainSourceRevision.load();v.readMsiModeGeneration=g_msiFeedbackModeGeneration.load();v.readContextStable=true;return v;}
static json fullPipeSubmitSource(InputCaptureGamepadSnapshot& v,bool admitted,bool neutral,bool boundSource=true){CommonFrameSourceScope scope(boundSource?&v:nullptr);return inputHostSubmitPad(admitted,v.pad,0,0,0,true,true,MotionContribution{},"right",0,0,v.pad,false,0,0,0,0,0,1,1,0,0,0,neutral,0);}
// Repeated downstream requests must be idempotent even if an upstream caller
// bypasses its UI busy state. This executes actual Start, transport ownership
// and the current admission ticket; it never supplies a terminal receipt.
static void fullPipeDuplicateStartBurst(const char* phase){
 const auto process=GetProcessId(g_inputHostProcess);const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto spawns=fullPipeSpawnCount;const auto sequence=g_inputHostSequence;const auto queued=g_inputHostLifecycleWorkQueue.size();const auto before=g_virtualStartProtection.view(startProtectionClock);
 bool accepted=true;for(int i=0;i<128;++i)accepted=inputHostStart()&&accepted;
 const auto after=g_virtualStartProtection.view(startProtectionClock);
 check(accepted,"duplicate downstream Start reuses healthy prepared target");
 check(g_inputHostProcess&&GetProcessId(g_inputHostProcess)==process&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&fullPipeSpawnCount==spawns,"128 duplicate Start calls create no process or replacement tuple");
 check(g_inputHostSequence==sequence&&g_inputHostLifecycleWorkQueue.size()==queued,"duplicate Start calls send no target command and add no lifecycle work");
 check(after.requestId==before.requestId&&after.phase==before.phase&&after.remainingMs==before.remainingMs&&after.verificationRemainingMs==before.verificationRemainingMs&&after.candidateOutcome==before.candidateOutcome,"duplicate Start calls neither finish nor renew startup protection");
 if(!cases.back().contains("duplicateStartBursts"))cases.back()["duplicateStartBursts"]=json::array();
 cases.back()["duplicateStartBursts"].push_back({{"phase",phase},{"requests",128},{"requestIdBefore",before.requestId},{"requestIdAfter",after.requestId},{"remainingBeforeMs",before.remainingMs},{"remainingAfterMs",after.remainingMs},{"commandsAdded",g_inputHostSequence-sequence},{"processesAdded",fullPipeSpawnCount-spawns}});
}
// Player requests share one lock, including OFF. These helpers only observe
// real IPC/consumer state, advance the fixture clock and invoke the actual
// verification maintenance. They never manufacture candidates/terminals, mutate
// intent, or dispatch queued work to force admission.
static constexpr std::uint64_t fullPipeUserSettleMs=10000,fullPipeUserMaxMs=30000;
static json fullPipeProtectionState(){const auto v=g_virtualStartProtection.view(startProtectionClock);return {{"requestId",v.requestId},{"persona",v.persona},{"phase",v.phase},{"locked",v.locked},{"elapsedMs",v.elapsedMs},{"remainingMs",v.remainingMs},{"verificationPending",v.verificationPending},{"verificationRemainingMs",v.verificationRemainingMs},{"candidateOutcome",v.candidateOutcome}};}
static json fullPipePlayerState(){
 json queue;{std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);queue=g_inputHostLifecycleWorkQueue;}
 return {{"settings",ymSettingsReadRaw(YM_SETTINGS_FILE)},{"queue",queue},{"workRequested",g_inputHostLifecycleWorkRequested.load()},{"workInFlight",g_inputHostLifecycleWorkInFlight.load()},{"processId",g_inputHostProcess?GetProcessId(g_inputHostProcess):0},{"spawns",fullPipeSpawnCount},{"epoch",g_inputHostEpoch},{"runId",g_inputHostRunId},{"sequence",g_inputHostSequence},{"configuredPersona",inputHostConfiguredPersona()},{"effectiveEnabled",g_realStickTest.virtualTargetEnabled},{"intentRevision",g_inputHostVirtualIntentRevision.load()},{"localState",state()}};
}
static void fullPipeRejectLockedPlayerWrite(const json& current,const json& next){
 const auto protection=fullPipeProtectionState(),before=fullPipePlayerState();const auto process=g_inputHostProcess;
 check(protection.value("locked",false),"rejected player transition is tested while the original protection is locked");
 const auto result=writeTarget(current,next);
 check(!result.value("ok",true),"all explicit player transitions including OFF are rejected during protection");
 check(fullPipePlayerState()==before&&g_inputHostProcess==process,"rejected explicit player request changes no settings queue process or effective intent");
 check(fullPipeProtectionState()==protection,"rejected player request cannot finish replace or renew the original protection");
 if(!cases.back().contains("lockedPlayerRequests"))cases.back()["lockedPlayerRequests"]=json::array();
 cases.back()["lockedPlayerRequests"].push_back({{"requestedPersona",next["input"]["outputTarget"].value("persona",std::string{})},{"protection",protection},{"result",result},{"sideEffectsUnchanged",fullPipePlayerState()==before}});
}
static void fullPipeVerificationStep(){
 if(g_inputHostMx.ownedByCurrentThread())inputHostSwitchVerificationMaintenanceLocked();
 else {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
}
static void fullPipeWaitForPlayerLock(const json& current,const json& next){
 const auto before=fullPipeProtectionState();const auto v=g_virtualStartProtection.view(startProtectionClock);json verified=nullptr;
 if(v.locked){
  fullPipeRejectLockedPlayerWrite(current,next);
  // Ten elapsed seconds only make verification eligible; the real consumer
  // still owns final success/failure. A late candidate cannot extend the cap.
  if(v.verificationPending&&v.verificationRemainingMs<v.remainingMs){
   if(v.verificationRemainingMs){if(v.verificationRemainingMs>1)startProtectionClock+=v.verificationRemainingMs-1;fullPipeRejectLockedPlayerWrite(current,next);++startProtectionClock;}
   const auto due=g_virtualStartProtection.view(startProtectionClock);
   check(due.requestId==v.requestId&&due.locked&&due.phase=="verifying"&&due.verificationRemainingMs==0,"ten-second candidate countdown alone never unlocks a player request");
   fullPipeRejectLockedPlayerWrite(current,next);fullPipeVerificationStep();verified=fullPipeProtectionState();
  }
  const auto held=g_virtualStartProtection.view(startProtectionClock);
  if(held.locked){
   check(held.requestId==v.requestId&&held.remainingMs==fullPipeUserMaxMs-held.elapsedMs,"missing proof or in-flight recovery retains only the original thirty-second budget");
   fullPipeRejectLockedPlayerWrite(current,next);if(held.remainingMs>1)startProtectionClock+=held.remainingMs-1;fullPipeRejectLockedPlayerWrite(current,next);++startProtectionClock;
   fullPipeVerificationStep();
  }
 }
 const auto after=g_virtualStartProtection.view(startProtectionClock);
 check(!after.locked&&after.requestId==v.requestId&&(after.phase=="success"||after.phase=="failure"||after.phase=="expired"),"player admission opens only after actual verification final or original thirty-second expiry");
 if(!cases.back().contains("playerLockWaits"))cases.back()["playerLockWaits"]=json::array();
 cases.back()["playerLockWaits"].push_back({{"before",before},{"afterVerification",verified},{"after",fullPipeProtectionState()}});
}
static void fullPipeCheckStopCandidate(std::uint64_t request,const char* outcome){
 const auto v=g_virtualStartProtection.view(startProtectionClock);const auto remaining=v.elapsedMs<fullPipeUserMaxMs?fullPipeUserMaxMs-v.elapsedMs:std::uint64_t{0};
 check(v.requestId==request&&v.persona=="disabled"&&v.phase=="verifying"&&v.candidateOutcome==outcome&&v.verificationPending&&v.locked&&v.remainingMs==remaining&&v.verificationRemainingMs==fullPipeUserSettleMs,"actual stop reports only a candidate with its first ten-second verification inside the original thirty-second cap");
 if(!cases.back().contains("stopProtectionReceipts"))cases.back()["stopProtectionReceipts"]=json::array();
 cases.back()["stopProtectionReceipts"].push_back(fullPipeProtectionState());
}
#include "off_lifecycle_cases.inc"

static void fullPipeCurrentFrameCases(int family){
 const auto request=g_virtualStartProtection.view(startProtectionClock).requestId;json observations=json::array();
 check(request&&g_virtualStartProtection.view(startProtectionClock).phase=="pending","HELLO PREPARE and startup neutral alone leave startup protection pending");
 auto sample=[&](){return fullPipeReadNow(family);};
 auto submit=[&](const char* phase,InputCaptureGamepadSnapshot& v,bool admitted,bool neutral){const auto result=fullPipeSubmitSource(v,admitted,neutral);observations.push_back({{"phase",phase},{"result",result},{"protectionPhase",g_virtualStartProtection.view(startProtectionClock).phase},{"hostSequence",g_inputHostSequence}});return result;};
 auto old=sample();old.readPowerGeneration=currentPowerGeneration()+1;const auto stale=submit("previous-power-generation",old,true,false);
 check(stale.value("focusIsolationSemanticNeutral",false)&&stale.value("sourceContextReason","")=="canonical-read-context-retired","old physical read is converted to safe neutral by actual source generation gate");check(g_virtualStartProtection.view(startProtectionClock).phase=="pending","accepted cleanup from old source cannot settle startup success");
 auto missing=sample();missing.live=false;const auto absent=submit("source-disconnected",missing,false,false);check(absent.value("focusIsolationSemanticNeutral",false)&&g_virtualStartProtection.view(startProtectionClock).phase=="pending","source loss can clean prior input without fabricating startup success");
 auto neutralSource=sample();const auto neutral=submit("explicit-semantic-neutral",neutralSource,true,true);const bool neutralExpected=family?(neutral.value("hostSubmission","")=="publication-suppressed"&&neutral.value("publicationReason","")=="opening-fresh-neutral"):neutral.value("hostSubmission","")=="frame-accepted";check(neutralExpected&&g_virtualStartProtection.view(startProtectionClock).phase=="pending","pre-admission neutral preserves existing MSI/common source gate and cannot finish pending startup");
 auto held=sample();const auto before=g_inputHostSequence;g_inputHostPowerAdmissionRevoked=true;const auto sleeping=submit("power-not-admitted",held,true,false);g_inputHostPowerAdmissionRevoked=false;check(sleeping.value("hostSubmission","")=="not-admitted-or-failed"&&g_inputHostSequence==before&&g_virtualStartProtection.view(startProtectionClock).phase=="pending","revoked power admits no pipe frame or startup completion");
 json current;for(int i=0;i<kInputHostFreshNeutralFramesRequired;++i){auto fresh=sample();current=submit("fresh-neutral-admission",fresh,true,false);}
 check(g_inputHostFreshNeutralAdmitted,"actual three fresh neutral reads establish original source admission");check(current.value("hostSubmission","")=="frame-accepted"&&!current.value("focusIsolationSemanticNeutral",true)&&g_inputHostActive,"fresh same-generation input crosses real native serializer and managed Host receipt");
 const auto candidate=g_virtualStartProtection.view(startProtectionClock);check(candidate.requestId==request&&candidate.phase=="verifying"&&candidate.candidateOutcome=="success"&&candidate.verificationPending&&candidate.locked&&candidate.remainingMs==fullPipeUserMaxMs-candidate.elapsedMs&&candidate.verificationRemainingMs==fullPipeUserSettleMs,"actual current frame creates only a success candidate and starts the first ten-second verification");
 auto active=sample();active.pad.wButtons=XINPUT_GAMEPAD_A;active.pad.sThumbRX=1234;const auto changed=submit("live-buttons-and-axis",active,true,false);check(changed.value("hostSubmission","")=="frame-accepted"&&!changed.value("focusIsolationSemanticNeutral",true),"live button and axis frame remains accepted while candidate verification is pending");
 startProtectionClock+=100;auto release=sample();const auto released=submit("post-admission-semantic-neutral",release,true,true);check(released.value("hostSubmission","")=="frame-accepted"&&released.value("focusIsolationSemanticNeutral",false)&&g_virtualStartProtection.view(startProtectionClock).phase=="verifying"&&g_virtualStartProtection.view(startProtectionClock).candidateOutcome=="success"&&g_virtualStartProtection.view(startProtectionClock).remainingMs==candidate.remainingMs-100&&g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs==fullPipeUserSettleMs-100,"post-admission neutral preserves the original candidate and both verification and admission deadlines");
 check(!g_virtualStartProtection.admit("elite",startProtectionClock).accepted,"new persona request cannot enter while the real-frame success candidate awaits verification");cases.back()["frameInputChain"]=observations;
}
// Only wake hide/discovery diagnostic ends are controlled here. Actual wake
// request/consume/queued-worker policy stays in the compiled production TU.
struct FullPipeWakeIoScope{bool prior=fixtureWakeScenario;FullPipeWakeIoScope(){fixtureWakeScenario=true;}~FullPipeWakeIoScope(){fixtureWakeScenario=prior;}};
static json fullPipeWakeKeptCase(unsigned long long generation,bool hideResult){
 const auto spawns=fullPipeSpawnCount,hideBefore=fixtureWakeHideCalls,excludedBefore=fixtureWakeVisibilityExclusions;const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto process=GetProcessId(g_inputHostProcess);fixtureWakeHideResult=hideResult;
 {FullPipeWakeIoScope io;const auto stale=inputHostWakeWorkReason(epoch+1,generation);check(inputHostQueueLifecycleWork(stale.c_str()),"stale epoch wake item enters actual owned lifecycle queue for rejection");fullPipeDrainQueuedLifecycle();}
 check(fixtureWakeHideCalls==hideBefore&&g_inputHostEpoch==epoch&&fullPipeSpawnCount==spawns,"stale queued Host epoch wake cannot hide or create another target");
 const bool msiNoSlotHandshake=msiClawKnownBoardAuthority()&&g_xinputConnectedMask==0&&g_xinputAdmittedMask==0&&inputHostWakePhysicalHandshakeReady();if(msiClawKnownBoardAuthority())check(msiNoSlotHandshake,"MSI DInput wake handshake succeeds with zero XInput slots as observed in real logs");
 inputHostRequestWakeRescan(generation);inputHostConsumeWakeRescan();check(g_inputHostLifecycleWorkQueue.size()==1&&!g_wakeRescanRequested.load(),"actual current wake producer and consumer enqueue exactly one hot-replace item");
 const auto eventBegin=fixtureEvents.size();{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
 json kept=nullptr;for(size_t i=eventBegin;i<fixtureEvents.size();++i){const auto& e=fixtureEvents[i];if(e.value("event","")=="input-host-wake-hot-replace"&&e["detail"].value("action","")=="kept-target")kept=e["detail"];}
 check(g_inputHostProcess&&GetProcessId(g_inputHostProcess)==process&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&fullPipeSpawnCount==spawns&&!g_wakeHoldPhysicalUnhide.load()&&!g_wakeRescanRequested.load()&&g_wakeKeptTargetGeneration.load()==generation,"actual handheld wake branch retains original Host and clears its own wake hold without rebuilding");
 check(fixtureWakeHideCalls==hideBefore+1&&fixtureWakeVisibilityExclusions==excludedBefore+1,"wake executes one explicit hide-refresh end and marks diagnostic readback excluded");
 check(kept.is_object()&&kept.value("hidRefreshed",!hideResult)==hideResult,"actual kept-target receipt preserves controlled hide success or failure instead of fabricating visibility proof");
 fixtureWakeHideResult=true;return {{"generation",generation},{"msiNoSlotHandshake",msiNoSlotHandshake},{"controlledHideResult",hideResult},{"keptReceipt",kept},{"hideCalls",fixtureWakeHideCalls-hideBefore},{"diagnosticReadbackExcluded",true},{"targetAfterWake",state()},{"keptGeneration",g_wakeKeptTargetGeneration.load()}};
}
// Controlled power facts enter the actual request producers and BUS consumer.
// No OS sleep/callback or sensor STA is executed; capture-ready stays false.
static void fullPipePowerCycles(int family){
 if(g_inputCaptureReady)throw std::logic_error("sensor-STA-not-admitted-in-offline-power-fixture");
 fullPipeDrainQueuedLifecycle();const auto process=GetProcessId(g_inputHostProcess);const auto epoch=g_inputHostEpoch;const auto spawns=fullPipeSpawnCount;const auto run=g_inputHostRunId;const auto request=g_virtualStartProtection.view(startProtectionClock);json cycles=json::array();
 for(int cycle=0;cycle<2;++cycle){
  auto oldRead=fullPipeReadNow(family);const auto previous=currentPowerGeneration(),generation=previous+1;g_powerGeneration=generation;
  inputHostRequestPowerRelease(previous,"offline-stale-release");inputHostConsumePowerRequests();const bool staleReleaseIgnored=!g_inputHostPowerAdmissionRevoked.load()&&g_inputHostLifecycleWorkQueue.empty();check(staleReleaseIgnored,"stale generation release cannot revoke current output or enqueue work");
  g_powerLifecycle=PowerLifecycle::Suspended;g_inputReady=false;for(int i=0;i<1000;++i)inputHostRequestPowerRelease(generation,"offline-controlled-suspend");inputHostConsumePowerRequests();check(g_inputHostLifecycleWorkQueue.size()==1,"one thousand pending same-generation suspend notifications coalesce into one actual queue item");
  const auto suspendSequence=g_inputHostSequence;fullPipeDrainQueuedLifecycle();check(g_inputHostPowerAdmissionRevoked.load()&&g_wakeHoldPhysicalUnhide.load()&&g_inputHostProcess&&GetProcessId(g_inputHostProcess)==process&&g_inputHostPrepared&&g_inputHostEpoch==epoch&&fullPipeSpawnCount==spawns&&g_inputHostSequence==suspendSequence,"actual suspend work retains the current target and sends no pipe command");
  const auto suspended=fullPipeSubmitSource(oldRead,true,false);check(suspended.value("hostSubmission","")=="not-admitted-or-failed"&&g_inputHostSequence==suspendSequence,"real submit consumer cannot write while actual power consumer has revoked admission");
  inputHostRequestPowerRearm(previous,"offline-stale-resume");inputHostConsumePowerRequests();const bool staleRearmIgnored=g_inputHostPowerAdmissionRevoked.load()&&g_inputHostLifecycleWorkQueue.empty();check(staleRearmIgnored,"stale generation resume cannot reopen writer or enqueue rearm");
  g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;inputHostRequestPowerRearm(generation,"offline-controlled-resume");inputHostConsumePowerRequests();check(!g_inputHostPowerAdmissionRevoked.load()&&g_inputHostLifecycleWorkQueue.size()==1,"matching committed Ready generation restores admission and queues one rearm");fullPipeDrainQueuedLifecycle();
  check(g_inputHostProcess&&GetProcessId(g_inputHostProcess)==process&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&fullPipeSpawnCount==spawns,"actual power rearm reuses retained Host instead of creating a second target");
  const auto wake=fullPipeWakeKeptCase(generation,cycle==0);
  const auto retired=fullPipeSubmitSource(oldRead,true,false);check(retired.value("hostSubmission","")=="frame-accepted"&&retired.value("focusIsolationSemanticNeutral",false)&&retired.value("sourceContextReason","")=="canonical-read-context-retired","pre-suspend physical read cannot become live input after committed resume");auto fresh=fullPipeReadNow(family);const auto resumed=fullPipeSubmitSource(fresh,true,false);check(resumed.value("hostSubmission","")=="frame-accepted"&&!resumed.value("focusIsolationSemanticNeutral",true),"fresh current-generation input resumes through actual existing Host pipe");
  const auto after=g_virtualStartProtection.view(startProtectionClock);check(after.requestId==request.requestId&&after.phase==request.phase&&after.remainingMs==request.remainingMs&&after.verificationRemainingMs==request.verificationRemainingMs&&after.candidateOutcome==request.candidateOutcome,"operational suspend and resume do not renew or replace user startup protection");cycles.push_back({{"generation",generation},{"wake",wake},{"staleReleaseIgnored",staleReleaseIgnored},{"staleRearmIgnored",staleRearmIgnored},{"suspendSequence",suspendSequence},{"suspended",suspended},{"retiredRead",retired},{"resumed",resumed},{"afterRearm",state()},{"retainedProcessId",process},{"protocolPowerGeneration",g_inputHostBoundPowerGeneration}});
 }
 // Queue the final Ready rearm while ON, then let the caller commit a real OFF
 // and stop before dispatch. The queued rearm must not resurrect that intent.
 const auto generation=currentPowerGeneration()+1;g_powerGeneration=generation;g_powerLifecycle=PowerLifecycle::Suspended;g_inputReady=false;inputHostRequestPowerRelease(generation,"offline-before-user-off");inputHostConsumePowerRequests();fullPipeDrainQueuedLifecycle();g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;inputHostRequestPowerRearm(generation,"offline-ready-before-user-off");inputHostConsumePowerRequests();check(!g_inputHostPowerAdmissionRevoked.load()&&g_inputHostLifecycleWorkQueue.size()==1,"real Ready rearm is queued while original intent is still ON before user cancellation");fullPipeDrainQueuedLifecycle();inputHostRequestWakeRescan(generation);inputHostConsumeWakeRescan();inputHostRequestPowerRearm(generation,"offline-duplicate-ready-before-user-off");inputHostConsumePowerRequests();check(g_inputHostLifecycleWorkQueue.size()==2,"actual wake item and duplicate Ready rearm are both queued before user OFF");cases.back()["wakeQueuedBeforeOff"]=true;cases.back()["hideCallsBeforeOffDispatch"]=fixtureWakeHideCalls;cases.back()["powerCycles"]=cycles;cases.back()["powerRearmQueuedBeforeOff"]=true;
}
static void fullPipeFinishOffRearm(unsigned spawns){{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}check(cases.back().contains("hideCallsBeforeOffDispatch")&&cases.back()["hideCallsBeforeOffDispatch"].is_number_unsigned()&&fixtureWakeHideCalls==cases.back()["hideCallsBeforeOffDispatch"].get<unsigned>(),"queued wake item dispatched after real OFF does not reach hide-refresh boundary");check(!g_realStickTest.virtualTargetEnabled&&!g_inputHostProcess&&!g_inputHostPrepared&&fullPipeSpawnCount==spawns,"queued power rearm dispatched after actual OFF and Stop cannot resurrect target");cases.back()["afterLateRearmOff"]=state();}
static unsigned fullPipeRecreateCommands(){unsigned count=0;for(const auto& e:fixtureEvents)if(e.value("event","")=="input-host-command-result"&&e["detail"].value("command","")=="RECREATE_TARGET")++count;return count;}
static void fullPipePsWakeStep(){g_psWakeRearmRetryAfterTick=0;inputHostConsumePsWakeRearm();FixturePsReadScope read;fullPipeDrainQueuedLifecycle();}
// The real settings IPC and effective consumer run while the actual PS reader
// is inside its controlled SetupAPI leaf. No direct OFF/session-state mutation.
// Actual four-slot discovery and classification under the existing SDK leaf
// fixture. No physical-source lock, admitted mask or completed-scan flag is set
// by this test: the original loop and original source consumer establish them.
static json fullPipeReadPhysicalSourceForElite(){
 for(auto& count:fixtureXinputLivePolls)count=0;
 const auto scan=[](){
  const ULONGLONG probeNow=GetTickCount64();const int probeSrc=kGpReadBus;const bool probeCostOn=false;
  const auto diagnosticReadPower=currentPowerGeneration();const auto diagnosticReadTopology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
  #include "xinput_discovery.inc"
 };
 scan();
 check(!inputHostAdmitSelectedPhysicalSource()&&g_xinputSlotFullScanOnce.load(),"first raw discovery cannot admit Elite source before actual consumer requests a complete boundary scan");
 scan();
 check(fixtureXinputLivePolls[0]==2&&fixtureXinputLivePolls[1]==2&&fixtureXinputLivePolls[2]==2&&fixtureXinputLivePolls[3]==2,"actual raw discovery and requested boundary scan each read all four controlled XInput slots once");
 const bool admitted=inputHostAdmitSelectedPhysicalSource();
 check(admitted&&g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==1&&xinputPhysicalConnectedMask()==1&&g_xinputAdmittedMask==1,"actual physical-source classifier and selected-source consumer admit controlled slot before Elite creation");
 return {{"actualDiscoveryLoop",true},{"actualPhysicalAdmissionConsumer",true},{"physicalMask",xinputPhysicalConnectedMask()},{"selectedSourceMask",g_xinputPhysicalSourceMask},{"publishedSourceMask",g_xinputAdmittedMask},{"completedBoundary",g_xinputPreTargetBoundaryScanned.load()},{"boundaryEpoch",g_xinputBoundaryScanEpoch.load()},{"topologyEpoch",g_xinputTopologyEpoch.load()},{"slotReads",{fixtureXinputLivePolls[0],fixtureXinputLivePolls[1],fixtureXinputLivePolls[2],fixtureXinputLivePolls[3]}},{"deviceIo",false}};
}
static void fullPipePsCancelDuringProbe(const std::string& kind){
 fullPipeDrainQueuedLifecycle();const auto before=fullPipeRecreateCommands();const auto spawns=fullPipeSpawnCount;const auto opens=fixturePsOpenCalls;const auto requestBefore=g_virtualStartProtection.view(startProtectionClock).requestId;const auto publicationRevisionBefore=g_inputHostVirtualIntentRevision.load();bool callback=false,directRecreate=false;json committed,frameAfterCancellation,unboundFrameAfterOff;unsigned long long frameCommandsAdded=0,unboundCommandsAdded=0;bool newerRequestPreserved=false,newerRequestCompleted=false;
 const auto bindingBefore=g_psEndpointOwnership.snapshot();const auto eventStart=fixtureEvents.size();
 if(kind=="refresh"){fullPipeRejectLockedPlayerWrite(json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE)),targetDocument("disabled",99));startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;}
 const auto generation=currentPowerGeneration()+1;g_powerGeneration=generation;fixturePsReadConfig({});
 fixturePsBeforeRead=[&]{
  callback=true;
  if(kind=="off"){
   const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));const auto off=targetDocument("disabled",current["input"]["revision"].get<int>()+1);
   fullPipeWaitForPlayerLock(current,off);committed=writeTarget(current,off);check(committed.value("ok",false),"actual settings OFF commits while PS endpoint query is in flight");inputCaptureRefreshRealStickSettings(true);
   check(!g_realStickTest.virtualTargetEnabled&&!psWakePolicyActive(),"real OFF consumer revokes PS policy during endpoint query");
  }else if(kind=="exit"){
   inputHostRaiseExitFence("offline-exit-during-query");check(inputHostExitFenceRaised(),"actual exit-fence producer raises terminal fence during endpoint query");
  }else if(kind=="power"){
   g_powerLifecycle=PowerLifecycle::Suspended;g_inputReady=false;inputHostRequestPowerRelease(generation,"offline-suspend-during-query");inputHostConsumePowerRequests();
   check(g_inputHostPowerAdmissionRevoked.load()&&!sgVirtualWriterAllowedNow(),"actual power request consumer revokes publication during endpoint query");
  }else if(kind=="persona"){
   const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));const auto next=targetDocument("elite",current["input"]["revision"].get<int>()+1);fullPipeWaitForPlayerLock(current,next);
   committed=writeTarget(current,next);check(committed.value("ok",false),"actual settings switch to another persona commits after original verification final or hard cap");inputCaptureRefreshRealStickSettings(true);
   check(g_realStickTest.virtualTargetEnabled&&inputHostConfiguredPersona()=="elite"&&inputHostBoundPersonaValue()=="dualsense-edge"&&!g_inputHostRequestedPersonaMatchesBound.load(),"actual consumer requests new persona while old owned Edge target awaits lifecycle transition");
  }else if(kind=="reentry"){
   const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));const auto off=targetDocument("disabled",current["input"]["revision"].get<int>()+1);
   fullPipeWaitForPlayerLock(current,off);check(writeTarget(current,off).value("ok",false),"actual OFF commits during old PS query before same-persona reentry");inputCaptureRefreshRealStickSettings(true);
   const auto closing=g_virtualStartProtection.view(startProtectionClock);const auto next=targetDocument("dualsense-edge",off["input"]["revision"].get<int>()+1);
   check(closing.persona=="disabled"&&closing.phase=="pending"&&closing.remainingMs==fullPipeUserMaxMs,"OFF settings commit waits for real stop instead of fabricating a closing terminal receipt");
   // Preserve the same owned Host for this request-identity negative. The old
   // query delays Stop, so OFF must use its 30s hard cap, never a fake success.
   startProtectionClock+=fullPipeUserSettleMs;fullPipeRejectLockedPlayerWrite(off,next);
   check(g_virtualStartProtection.view(startProtectionClock).phase=="pending","ten seconds after OFF admission cannot substitute for an actual stop result");
   fullPipeWaitForPlayerLock(off,next);
   check(g_virtualStartProtection.view(startProtectionClock).requestId==closing.requestId&&g_virtualStartProtection.view(startProtectionClock).phase=="expired"&&g_virtualStartProtection.view(startProtectionClock).elapsedMs==fullPipeUserMaxMs,"uncompleted OFF releases only at its own original thirty-second hard cap");
   committed=writeTarget(off,next);check(committed.value("ok",false),"fresh same-persona request is admitted after pending OFF reaches its hard cap");inputCaptureRefreshRealStickSettings(true);
   check(g_realStickTest.virtualTargetEnabled&&psWakePolicyActive(),"same persona and power do not alone identify the original recovery request");
  }else if(kind=="target"){
   directRecreate=inputHostRecreatePreparedTarget();const auto newer=g_psEndpointOwnership.snapshot();
   fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"});
   check(directRecreate&&newer.valid&&newer.generation>bindingBefore.generation&&newer.rootInstanceId==bindingBefore.rootInstanceId,"actual SDK replacement may reuse instance names while advancing its target generation");
  }else if(kind=="refresh"){
   const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId,revision=g_inputHostVirtualIntentRevision.load();
   inputCaptureRefreshRealStickSettings(true);
   check(g_virtualStartProtection.view(startProtectionClock).requestId==ticket&&g_inputHostVirtualIntentRevision.load()!=revision,"same settings refresh changes publication revision but preserves actual admitted user request");
  }else if(kind=="generation"){
   g_powerGeneration=generation+1;inputHostRequestPsWakeRearm(generation+1,"offline-new-wake-during-old-query");
   check(g_psWakeRearmRequested.load()&&g_psWakeRearmGeneration.load()==generation+1,"actual PS producer arms a newer generation during older endpoint query");
  }else throw std::logic_error("unknown-query-cancellation");
  if(kind=="generation"||kind=="reentry"||kind=="refresh"||kind=="target")return;
  auto fresh=fullPipeReadNow(1);fresh.pad.wButtons=XINPUT_GAMEPAD_A;const auto sequence=g_inputHostSequence;frameAfterCancellation=fullPipeSubmitSource(fresh,true,false);frameCommandsAdded=g_inputHostSequence-sequence;
  check(frameAfterCancellation.value("hostSubmission","")!="frame-accepted"&&frameCommandsAdded==0,"fresh normal input after cancellation cannot reach Host frame terminal");
  if(kind=="off"||kind=="persona"){
   const auto unboundBefore=g_inputHostSequence;unboundFrameAfterOff=fullPipeSubmitSource(fresh,true,false,false);unboundCommandsAdded=g_inputHostSequence-unboundBefore;
   check(unboundFrameAfterOff.value("hostSubmission","")!="frame-accepted"&&unboundCommandsAdded==0,"normal input without capture TLS cannot bypass actual user OFF or changed persona");
  }
  directRecreate=inputHostRecreatePreparedTarget();check(!directRecreate,"actual RECREATE primitive refuses changed admission at send boundary");
 };
 inputHostRequestPsWakeRearm(generation,"offline-cancellation-during-query");fullPipePsWakeStep();
 check(callback&&!fixturePsBeforeRead&&fixturePsOpenCalls==opens+1,"one actual endpoint query consumed the controlled cancellation interleaving");
 if(kind=="off"){
  // The real dispatcher drains the OFF consumer's queued stop after the slow
  // query returns. Observe that first terminal, before final duplicate cleanup.
  check(!g_inputHostProcess&&!g_inputHostPrepared,"actual queued OFF stop completes before the PS query worker returns");
  fullPipeCheckStopCandidate(g_virtualStartProtection.view(startProtectionClock).requestId,"success");
  check(g_virtualStartProtection.view(startProtectionClock).requestId!=requestBefore,"queued OFF stop reports a candidate for its own request rather than the retired ON intent");
 }
 if(kind=="target"){
  const bool retiredDeferred=g_psWakeRearmRequested.load()&&g_psWakeRebuiltGeneration.load()!=generation&&fullPipeRecreateCommands()==before+1;
  bool oldReady=false;for(size_t i=eventStart;i<fixtureEvents.size();++i)oldReady=oldReady||fixtureEvents[i].value("event",std::string{})=="ps-wake-hid-ready";
  check(retiredDeferred&&!oldReady,"new target generation invalidates a slow observation even when instance names are reused");
  fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"});const auto freshQueries=fixturePsOpenCalls;fullPipePsWakeStep();
  check(!g_psWakeRearmRequested.load()&&fixturePsOpenCalls==freshQueries+1&&fullPipeRecreateCommands()==before+1,"new target requires its own fresh ownership query and no second recreate");
  auto fresh=fullPipeReadNow(1);const auto accepted=fullPipeSubmitSource(fresh,true,false);check(accepted.value("hostSubmission","")=="frame-accepted","same-name new target completes current-source submission after fresh proof");
  const auto boundAfter=g_psEndpointOwnership.snapshot();cases.back()["psWakeBindingGeneration"]={{"sameRootInstanceId",boundAfter.rootInstanceId==bindingBefore.rootInstanceId},{"beforeGeneration",bindingBefore.generation},{"afterGeneration",boundAfter.generation},{"retiredQueryDeferred",retiredDeferred},{"oldQueryMarkedReady",oldReady},{"currentFrame",accepted}};
 }else if(kind=="refresh"){
  check(fullPipeRecreateCommands()==before+1&&g_psWakeRebuiltGeneration.load()==generation&&!g_psWakeRearmRequested.load(),"same-request settings refresh does not cancel valid complete-absence recovery");
  auto fresh=fullPipeReadNow(1);const auto resumed=fullPipeSubmitSource(fresh,true,false);check(resumed.value("hostSubmission","")=="frame-accepted","current request resumes input after valid recovery despite an ordinary settings refresh");
 }else check(fullPipeRecreateCommands()==before,"cancellation during actual endpoint query cannot send RECREATE_TARGET");
 if(kind=="generation"){
  newerRequestPreserved=g_psWakeRearmRequested.load()&&g_psWakeRearmGeneration.load()==generation+1&&g_psWakeRebuiltGeneration.load()!=generation;
  check(newerRequestPreserved,"older endpoint query cannot clear or rebuild a newer pending PS request");
  fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"});fullPipePsWakeStep();
  newerRequestCompleted=!g_psWakeRearmRequested.load()&&fullPipeRecreateCommands()==before;
  check(newerRequestCompleted&&fixturePsOpenCalls==opens+2,"newer PS request requires its own fresh query before reusing existing target");
 }else if(kind!="refresh"&&kind!="target")check(!g_psWakeRearmRequested.load()&&g_psWakeRebuiltGeneration.load()!=generation,"cancelled query cannot mark rebuilt or rearm its retired request");
 check(fullPipeSpawnCount==spawns,"late PS query cannot spawn another Host after cancellation");
 cases.back()["psWakeCancellation"]={{"kind",kind},{"requestIdBefore",requestBefore},{"requestIdAfter",g_virtualStartProtection.view(startProtectionClock).requestId},{"publicationRevisionBefore",publicationRevisionBefore},{"requestedMatchesBound",g_inputHostRequestedPersonaMatchesBound.load()},{"requestedPersonaAfter",inputHostConfiguredPersona()},{"boundPersonaAfter",inputHostBoundPersonaValue()},{"intentRevisionAfter",g_inputHostVirtualIntentRevision.load()},{"unboundFrameAfterOff",unboundFrameAfterOff},{"unboundCommandsAdded",unboundCommandsAdded},{"newerRequestPreserved",newerRequestPreserved},{"newerRequestCompleted",newerRequestCompleted},{"normalFrameResult",frameAfterCancellation},{"frameCommandsAdded",frameCommandsAdded},{"actualSettingsOffDuringQuery",kind=="off"&&committed.value("ok",false)},{"callbackExecuted",callback},{"directRecreateReturned",directRecreate},{"generation",generation},{"probeCalls",fixturePsOpenCalls-opens},{"recreateCommandsAdded",fullPipeRecreateCommands()-before},{"processesAdded",fullPipeSpawnCount-spawns},{"effectiveTargetEnabled",g_realStickTest.virtualTargetEnabled},{"rearmRequested",g_psWakeRearmRequested.load()},{"markedRebuilt",g_psWakeRebuiltGeneration.load()==generation},{"afterWorker",state()}};
 if(kind=="reentry"){
  const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;check(inputHostStart()&&fullPipeSpawnCount==spawns,"fresh same-persona intent reuses current owned Host after old query is rejected");
  auto fresh=fullPipeReadNow(1);const auto frame=fullPipeSubmitSource(fresh,true,false);const auto protectedNow=g_virtualStartProtection.view(startProtectionClock);
  check(frame.value("hostSubmission","")=="frame-accepted"&&protectedNow.requestId==ticket&&protectedNow.phase=="verifying"&&protectedNow.candidateOutcome=="success"&&protectedNow.verificationPending&&protectedNow.locked,"fresh same-persona request reaches actual accepted frame and its own success candidate without final release");
  cases.back()["psWakeNewIntentFollowup"]={{"kind",kind},{"newProcesses",fullPipeSpawnCount-spawns},{"frame",frame},{"requestId",protectedNow.requestId},{"phase",protectedNow.phase}};
 }else if(kind=="persona"){
  // Complete the originally requested type transfer through actual production
  // Stop then Start. This is the new user's intent, after the old query retired.
  inputHostStop("persona-transition");const auto old=fullPipeChildOutcome();check(old.value("nativeObservedExitCode",999)==0&&old.value("finalCleanupOk",false)&&old.value("creates",0)==1,"old Edge Host closes naturally without stale-query recreation before requested persona replacement starts");
  struct SourceScope{bool previous=fixtureXinputSourceLive;SourceScope(){fixtureXinputSourceLive=true;}~SourceScope(){fixtureXinputSourceLive=previous;}}sourceScope;
  const auto physicalSource=fullPipeReadPhysicalSourceForElite();
  check(inputHostStart()&&fullPipeSpawnCount==spawns+1&&inputHostBoundPersonaValue()=="elite"&&g_inputHostRequestedPersonaMatchesBound.load(),"actual native Start prepares the newly requested Elite target after old Edge release");
  json frames=json::array();for(int i=0;i<3;++i){auto fresh=fullPipeReadNow(1);frames.push_back(fullPipeSubmitSource(fresh,true,false));}
  check(frames.back().value("hostSubmission","")=="frame-accepted"&&frames.back().value("persona","")=="elite","newly bound requested target passes fresh neutral admission and receives current input");
  auto live=fullPipeReadNow(1);live.pad.wButtons=XINPUT_GAMEPAD_A;const auto frame=fullPipeSubmitSource(live,true,false);
  check(frame.value("hostSubmission","")=="frame-accepted"&&!frame.value("focusIsolationSemanticNeutral",true)&&g_virtualStartProtection.view(startProtectionClock).phase=="verifying"&&g_virtualStartProtection.view(startProtectionClock).candidateOutcome=="success"&&g_virtualStartProtection.view(startProtectionClock).locked,"new requested persona resumes live buttons and produces only its own success candidate");
  cases.back()["psWakeNewIntentFollowup"]={{"kind",kind},{"newProcesses",fullPipeSpawnCount-spawns},{"oldManagedReceipt",old},{"physicalSource",physicalSource},{"frames",frames},{"frame",frame},{"boundPersona",inputHostBoundPersonaValue()},{"phase",g_virtualStartProtection.view(startProtectionClock).phase}};
 }
}

static unsigned fullPipeOwnershipQueryCommands(){unsigned n=0;for(const auto& e:fixtureEvents)if(e.value("event",std::string{})=="input-host-command-result"&&e["detail"].value("command",std::string{})=="QUERY_TARGET_OWNERSHIP")++n;return n;}
static void fullPipeLateOwnershipCaptureCase(){
 const auto before=g_psEndpointOwnership.snapshot();const auto beforeQuery=fullPipeOwnershipQueryCommands(),beforeRecreate=fullPipeRecreateCommands(),beforeSpawns=fullPipeSpawnCount;const auto ticket=g_virtualStartProtection.view(startProtectionClock);
 check(before.capable&&!before.valid&&before.generation==1&&g_inputHostPrepared,"optional first ownership read failure does not reject prepared virtual target");
 auto sample=fullPipeReadNow(1);const auto liveBefore=fullPipeSubmitSource(sample,true,false);check(liveBefore.value("hostSubmission",std::string{})=="frame-accepted","current virtual input remains usable before optional ownership recovery");
 const auto generation=currentPowerGeneration()+1;g_powerGeneration=generation;fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"});inputHostRequestPsWakeRearm(generation,"offline-late-target-ownership");fullPipePsWakeStep();const auto after=g_psEndpointOwnership.snapshot();
 check(after.valid&&after.generation==before.generation&&fullPipeOwnershipQueryCommands()==beforeQuery+1,"actual native query obtains late SDK subtree evidence without a new target generation");
 check(!g_psWakeRearmRequested.load()&&fullPipeRecreateCommands()==beforeRecreate&&fullPipeSpawnCount==beforeSpawns,"late ownership recovery reuses current target without recreating or spawning");
 sample=fullPipeReadNow(1);const auto liveAfter=fullPipeSubmitSource(sample,true,false);check(liveAfter.value("hostSubmission",std::string{})=="frame-accepted","current-source input completes after actual read-only ownership recovery");
 const auto settled=g_virtualStartProtection.view(startProtectionClock);check(settled.requestId==ticket.requestId&&settled.remainingMs==ticket.remainingMs&&settled.verificationRemainingMs==ticket.verificationRemainingMs&&settled.candidateOutcome==ticket.candidateOutcome,"late metadata recovery cannot renew user startup protection");
 cases.back()["lateOwnershipRecovery"]={{"beforeComplete",before.valid},{"afterComplete",after.valid},{"bindingGeneration",after.generation},{"queriesAdded",fullPipeOwnershipQueryCommands()-beforeQuery},{"recreatesAdded",fullPipeRecreateCommands()-beforeRecreate},{"processesAdded",fullPipeSpawnCount-beforeSpawns},{"beforeFrame",liveBefore},{"afterFrame",liveAfter}};
}

static void fullPipePsWakeConsumerCases(){
 const auto protectionClockBefore=startProtectionClock;
 fullPipeDrainQueuedLifecycle();
 const auto process=GetProcessId(g_inputHostProcess);const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto spawns=fullPipeSpawnCount;const auto initialRecreates=fullPipeRecreateCommands();const auto intent=g_virtualStartProtection.view(startProtectionClock);json stages=json::array();
 check(psWakePolicyActive()&&!g_msiSteamDeckReleaseFence.required(),"actual Edge policy is selected without opting ROG into MSI SteamDeck pin contract");
 auto generation=currentPowerGeneration()+1;g_powerGeneration=generation;fixturePsReadConfig({},ERROR_ACCESS_DENIED);const auto opens=fixturePsOpenCalls;inputHostRequestPsWakeRearm(generation,"offline-unavailable-query");
 for(int i=0;i<kPsWakeRearmMaxAttempts+1;++i)fullPipePsWakeStep();
 check(g_psWakeExhaustedGeneration.load()==generation&&!g_psWakeRearmRequested.load()&&fixturePsOpenCalls==opens+kPsWakeRearmMaxAttempts,"actual PS request consumer and worker stop unknown-read retries at existing attempt cap");check(fullPipeRecreateCommands()==initialRecreates&&fullPipeSpawnCount==spawns,"unknown endpoint read cannot send RECREATE or start another Host");stages.push_back({{"phase","unknown-budget-exhausted"},{"generation",generation},{"queryCalls",fixturePsOpenCalls-opens},{"recreateCommands",fullPipeRecreateCommands()-initialRecreates}});
 // Controlled endpoint ground truth: these are a peer process and a retired
 // target. Their profile matches; the unchanged current Host must not claim them.
 // The terminal labels are not production authority and no owner flag is forged.
 for(const auto peer: {L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OTHER-PROCESS-B",L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OLD-TARGET-GENERATION"}){
  generation=currentPowerGeneration()+1;g_powerGeneration=generation;fixturePsReadConfig({peer});const auto eventStart=fixtureEvents.size();
  inputHostRequestPsWakeRearm(generation,"offline-same-profile-not-current-target");fullPipePsWakeStep();
  bool ready=false;for(size_t i=eventStart;i<fixtureEvents.size();++i)ready=ready||fixtureEvents[i].value("event",std::string{})=="ps-wake-hid-ready";
  check(!ready,"same-profile peer or retired endpoint cannot prove current owned PS readiness");
  check(g_psWakeRearmRequested.load()&&fullPipeRecreateCommands()==initialRecreates,"same-profile unowned observation defers without clearing recovery or rebuilding beside a peer");
  stages.push_back({{"phase","same-profile-not-current-target"},{"injectedPath",W2U(peer)},{"profileOnlyTruth","controlled-terminal-not-native-ownership-proof"},{"generation",generation},{"actualReady",ready},{"rearmRequested",g_psWakeRearmRequested.load()},{"hostEpochUnchanged",g_inputHostEpoch==epoch},{"requestIdUnchanged",g_virtualStartProtection.view(startProtectionClock).requestId==intent.requestId},{"recreateCommands",fullPipeRecreateCommands()-initialRecreates}});
 }
 generation=currentPowerGeneration()+1;g_powerGeneration=generation;fixturePsReadConfig({L"HID#VID_28DE&PID_1205#HM-CTL-FOREIGN"});inputHostRequestPsWakeRearm(generation,"offline-foreign-endpoint");fullPipePsWakeStep();check(g_psWakeRearmRequested.load()&&g_psWakeRebuiltGeneration.load()!=generation&&fullPipeRecreateCommands()==initialRecreates,"actual PS worker defers foreign virtual endpoint without rebuilding");
 fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"});fullPipePsWakeStep();check(!g_psWakeRearmRequested.load()&&fullPipeRecreateCommands()==initialRecreates,"actual current own-endpoint observation reuses existing Edge target");stages.push_back({{"phase","foreign-then-own-reused"},{"generation",generation},{"recreateCommands",fullPipeRecreateCommands()-initialRecreates}});
 // Check every rejected player transition while the original inspection is still locked.
 fullPipeRejectLockedPlayerWrite(json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE)),targetDocument("disabled",99));
 // Actual replacement follows the original central execution inspection.
 startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;
 generation=currentPowerGeneration()+1;g_powerGeneration=generation;fixturePsReadConfig({});inputHostRequestPsWakeRearm(generation,"offline-confirmed-endpoint-absent");fullPipePsWakeStep();check(g_psWakeRebuiltGeneration.load()==generation&&!g_psWakeRearmRequested.load()&&fullPipeRecreateCommands()==initialRecreates+1&&g_inputHostPrepared&&g_inputHostNeutralized,"complete endpoint absence permits exactly one actual RECREATE and neutral on current Host");
 check(g_inputHostProcess&&GetProcessId(g_inputHostProcess)==process&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&fullPipeSpawnCount==spawns,"Edge recovery recreates in place without changing Host process or tuple");auto fresh=fullPipeReadNow(1);const auto frame=fullPipeSubmitSource(fresh,true,false);check(frame.value("hostSubmission","")=="frame-accepted"&&!frame.value("focusIsolationSemanticNeutral",true),"actual replacement Edge target resumes current-source frame through managed pipe");stages.push_back({{"phase","complete-absence-rebuilt"},{"generation",generation},{"recreateCommands",fullPipeRecreateCommands()-initialRecreates},{"resumedFrame",frame}});
 inputHostRequestPsWakeRearm(generation,"offline-same-wake-repeat");fullPipePsWakeStep();check(g_psWakeRearmRequested.load()&&fullPipeRecreateCommands()==initialRecreates+1,"same generation cannot recreate Edge target a second time after one rebuild");const auto after=g_virtualStartProtection.view(startProtectionClock);check(after.requestId==intent.requestId&&after.phase==intent.phase&&after.elapsedMs==intent.elapsedMs+(startProtectionClock-protectionClockBefore)&&after.remainingMs==intent.remainingMs-(startProtectionClock-protectionClockBefore)&&after.verificationRemainingMs==0&&after.fallbackAttempts==1&&after.candidateOutcome==intent.candidateOutcome,"bounded PS endpoint recovery does not replace or renew user start protection");
 g_psWakeRearmRetryAfterTick=0;inputHostConsumePsWakeRearm();check(g_inputHostLifecycleWorkQueue.size()==1,"actual PS rearm work is queued before caller commits user OFF");cases.back()["psWakeRecovery"]=stages;cases.back()["psWakeQueuedBeforeOff"]=true;cases.back()["psProbeCallsBeforeOff"]=fixturePsOpenCalls;
}
// Valid new SDK/PnP identities and an invalid-identity provider exercise the
// actual native RECREATE consumer; no response or SDK disposal ACK is forged.
static void fullPipeRecreateOwnedCase(bool foreign,bool unbound){
 const auto before=g_msiSteamDeckReleaseFence.ownedTargets();const auto process=GetProcessId(g_inputHostProcess);const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto spawns=fullPipeSpawnCount;const auto eventBegin=fixtureEvents.size();const auto sequence=g_inputHostSequence;
 check(g_msiSteamDeckReleaseFence.required()&&g_msiSteamDeckReleaseFence.pinned()&&before.count("USB\\VID_28DE&PID_1205\\OWNED-1"),"initial actual prepared target has its exact first owned root pinned");
 const auto boundHost=g_inputHostHostInstanceId;if(unbound)g_inputHostHostInstanceId="offline-stale-host-instance";
 if(!foreign&&!unbound)throwLogEvent="msi-sd-release-fence";
 const bool recreated=inputHostRecreatePreparedTarget();if(unbound)g_inputHostHostInstanceId=boundHost;if(!foreign&&!unbound)check(throwLogEvent==nullptr,"recreate diagnostic loss cannot override accepted new owned pin or target state");const auto after=g_msiSteamDeckReleaseFence.ownedTargets();json wire=nullptr;for(size_t i=eventBegin;i<fixtureEvents.size();++i){const auto& e=fixtureEvents[i];if(e.value("event","")=="input-host-command-result"&&e["detail"].value("command","")=="RECREATE_TARGET"&&e["detail"].contains("msiSteamDeckReleaseEvidence"))wire=e["detail"]["msiSteamDeckReleaseEvidence"];}
 cases.back()["recreateOwnedPin"]={{"foreign",foreign},{"unboundAck",unbound},{"before",before},{"after",after},{"returned",recreated},{"wireEvidence",wire},{"afterRecreate",state()},{"pinnedAfter",g_msiSteamDeckReleaseFence.pinned()},{"sequenceBefore",sequence},{"sequenceAfter",g_inputHostSequence}};
 check(g_inputHostProcess&&GetProcessId(g_inputHostProcess)==process&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&fullPipeSpawnCount==spawns,"in-process actual RECREATE keeps original Host ownership tuple and process");
 if(!unbound)check(wire.is_object()&&wire.value("required",false),"actual RECREATE response carries current owned endpoint evidence for opted-in MSI SD session");
 if(unbound){check(!recreated&&!g_msiSteamDeckReleaseFence.pinned()&&g_inputHostPrepared&&!g_inputHostNeutralized&&!g_inputHostActive,"unbound recreate ACK cannot leave old endpoint identity trusted");auto proof=g_msiSteamDeckReleaseFence;const std::set<std::string> newPresent={"USB\\VID_28DE&PID_1205\\OWNED-2","HID\\VID_28DE&PID_1205&MI_00\\OWNED-2-CHILD0","HID\\VID_28DE&PID_1205&MI_01\\OWNED-2-CHILD1"};const bool absent=proof.acceptOwnedPnpAbsence(true,newPresent);cases.back()["recreateOwnedPin"]["oldIdsAbsentWhileNewTargetPresentWouldPass"]=absent;check(!absent,"old endpoint absence cannot prove replacement removal after an unbound RECREATE ACK");check(g_inputHostSequence==sequence,"unbound ACK preserves native command sequence until cleanup reconciliation");
 }else if(!foreign){
  check(recreated&&g_inputHostPrepared&&g_inputHostNeutralized,"valid actual RECREATE and neutral complete");check(g_msiSteamDeckReleaseFence.pinned()&&after.count("USB\\VID_28DE&PID_1205\\OWNED-2")&&!after.count("USB\\VID_28DE&PID_1205\\OWNED-1"),"native release fence switches to recreated owned endpoints");
  check(wire.is_object()&&wire.contains("ownedTargets")&&wire["ownedTargets"].get<std::set<std::string>>()==after,"native owned endpoint set equals actual current managed RECREATE receipt");auto sample=fullPipeReadNow(0);const auto frame=fullPipeSubmitSource(sample,true,false);check(frame.value("hostSubmission","")=="frame-accepted"&&!frame.value("focusIsolationSemanticNeutral",true),"replacement target accepts new current-source frame through actual pipe");
 }else{
  check(!recreated&&!g_msiSteamDeckReleaseFence.pinned()&&g_inputHostPrepared&&!g_inputHostNeutralized&&!g_inputHostActive,"untrusted recreate pin retains possible target ownership but blocks publication");check(g_inputHostSequence==sequence+1,"untrusted RECREATE evidence does not send subsequent neutral command");auto sample=fullPipeReadNow(0);const auto beforeFrame=g_inputHostSequence;const auto frame=fullPipeSubmitSource(sample,true,false);check(frame.value("hostSubmission","")!="frame-accepted"&&g_inputHostSequence==beforeFrame,"fresh physical source cannot publish through unproven replacement pin");
 }
}
// A real managed child must accept the returning lineage before the common
// verifier can release ON. This complements controlled-event native proofs.
static void fullPipeVerifiedSourceLineageCase(){
 seed();fixtureFullPipe=true;fullPipeFault="none";g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="ROG Ally X";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_xinputAdmittedMask=1;g_msiSteamDeckReleaseDebt.clear();g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 cases.push_back({{"name","VERIFICATION/current-source-lineage-real-managed-pipe"},{"requestedPersona","steamdeck"},{"controlledFamily","ROG"}});
 const auto oldSpawns=fullPipeSpawnCount;const auto on=targetDocument("steamdeck",0),off=targetDocument("disabled",1);
 check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"lineage integration seeds its private ON document");inputCaptureRefreshRealStickSettings(true);
 check(inputHostStart()&&fullPipeSpawnCount==oldSpawns+1,"lineage integration creates one real owned managed Host");
 const auto publish=[](){auto sample=fullPipeReadNow(1);std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);
  const auto old=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);sample.readContinuityGeneration=ymcc::input::recoveryHealth::nextContinuity(old,inputHostRecoveryHealthSource(&sample,true),GetTickCount64());g_inputCaptureGamepadSnapshot=sample;return sample;};
 fullPipeDrainQueuedLifecycle();inputHostRecoveryMaintenanceTick();
 json firstAck;for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=publish();firstAck=fullPipeSubmitSource(sample,true,false);}
 const auto candidate=fullPipeProtectionState();const auto epoch=g_inputHostEpoch;const auto sequence=g_inputHostSequence;
 check(firstAck.value("hostSubmission",std::string{})=="frame-accepted"&&candidate.value("phase",std::string{})=="verifying","real current source and managed receipt form the provisional ON candidate");
 const auto before=ymcc::inputHost::switchVerificationRuntime::acceptedSource;
 {std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);g_inputCaptureGamepadSnapshot.live=false;}
 auto returned=publish();const auto newContinuity=returned.readContinuityGeneration;
 check(newContinuity!=before.source.continuity,"real canonical lineage advances after physical-read disappearance");
 startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;fullPipeVerificationStep();
 const auto withoutAck=fullPipeProtectionState();
 check(withoutAck.value("locked",false)&&withoutAck.value("phase",std::string{})=="verifying","real managed integration cannot finalize the returning source before its new report receipt");
 check(g_inputHostSequence==sequence&&g_inputHostEpoch==epoch&&fullPipeSpawnCount==oldSpawns+1,"reading a new source and verifying it emits no frame or replacement process");
 returned.pad.wButtons=XINPUT_GAMEPAD_A;const auto currentAck=fullPipeSubmitSource(returned,true,false);
 check(currentAck.value("hostSubmission",std::string{})=="frame-accepted","returning source crosses actual native serialization and real managed acceptance");
 fullPipeWaitForPlayerLock(on,off);const auto verified=fullPipeProtectionState();
 check(verified.value("phase",std::string{})=="success"&&!verified.value("locked",true)&&verified["requestId"]==candidate["requestId"]&&verified.value("elapsedMs",0ULL)==10000,"current managed receipt is the unique successful ON release at the original ten-second deadline");
 cases.back()["lineageProof"]={{"candidate",candidate},{"withoutAck",withoutAck},{"verified",verified},{"oldContinuity",before.source.continuity},{"newContinuity",newContinuity},{"firstAck",firstAck},{"newAck",currentAck},{"commandsBeforeReturnedAck",sequence},{"hostEpoch",epoch},{"processes",fullPipeSpawnCount-oldSpawns}};
 check(writeTarget(on,off).value("ok",false),"verified ON admits one actual user OFF");inputCaptureRefreshRealStickSettings(true);
 const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,"success");
 cases.back()["managedReceipt"]=fullPipeChildOutcome();cases.back()["afterStop"]=state();
 const auto next=targetDocument("steamdeck",2);fullPipeWaitForPlayerLock(off,next);
 check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"lineage integration ends with real clean child exit and no target or hidden-device debt");
 cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;
}
// Same-power PS absence is an observed owned-target fault, not idle/focus or
// missing source evidence. Execute the normal Start/report chain before asking
// the real PS consumer/worker to obey the central inspection and shared budget.
// Real first-open Start, not a manually supplied recovery TLS context.
static void fullPipeInitialBudgetCase(const std::string& family,bool expires,bool expiredBeforeEntry=false,bool longPrepare=false){
 seed();fixtureFullPipe=true;fullPipeFault="none";g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 if(family!="MSI"){g_machineIdentity.family=family=="ROG"?YmccFamily::AsusRogAllyX:family=="GPD"?YmccFamily::Gpd:YmccFamily::Unknown;g_machineIdentity.manufacturer=family;g_machineIdentity.product=family=="ROG"?"ROG Ally X":family=="GPD"?"G1617":"unregistered-desktop";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=1;}
 if(family=="PC")fixturePcPowerCapabilities=true;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 cases.push_back({{"name","INITIAL-PIPE-BUDGET/"+family+"/"+(expiredBeforeEntry?"expired-before-entry":expires?"expiry":"remaining")+"-real-managed-pipe"},{"controlledFamily",family}});
 const auto spawns=fullPipeSpawnCount;const auto on=targetDocument("dualsense-edge",0),off=targetDocument("disabled",1);
 check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"initial budget case seeds only its private real settings document");inputCaptureRefreshRealStickSettings(true);
 const auto admission=g_virtualStartProtection.view(startProtectionClock);const auto deadline=startProtectionClock+admission.remainingMs;
 check(admission.locked&&admission.remainingMs==fullPipeUserMaxMs,"normal settings consumer creates original30s player request before actual first Start");
 if(expiredBeforeEntry){
  check(inputHostQueueLifecycleWork("initial-target-admission"),"actual initial target work is queued within its original player period");
  startProtectionClock+=admission.remainingMs;fixturePowerClockOffset+=admission.remainingMs;
  check(g_virtualStartProtection.view(startProtectionClock).phase=="expired","original initial request expires before its actual queued consumer enters Start");
  fullPipeDrainQueuedLifecycle();fullPipeVerificationStep();
  const auto after=g_virtualStartProtection.view(startProtectionClock);const bool created=fullPipeSpawnCount!=spawns;
  check(!created&&!g_inputHostProcess&&!g_inputHostPrepared&&!g_hidHideVisibilityJournal.active,"expired queued first Start cannot create a Host or target after original thirty-second budget");
  check(after.requestId==admission.requestId&&after.phase=="expired"&&!after.locked&&after.fallbackAttempts==0,"late initial work cannot renew the failed original request or reserve fallback");
  unsigned success=0,failure=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-switch-verification"&&event["detail"].value("stage","")=="final"&&event["detail"].value("requestId",0ULL)==admission.requestId){success+=event["detail"].value("outcome","")=="success";failure+=event["detail"].value("outcome","")=="failure";}
  check(success==0&&failure==1,"expired initial request publishes exactly one failure and never a competing late success");
  check(writeTarget(on,off).value("ok",false),"expired initial request releases admission for actual player OFF");inputCaptureRefreshRealStickSettings(true);
  const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,"success");
  if(created){cases.back()["managedReceipt"]=fullPipeChildOutcome();}
  fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));
  check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"late initial work leaves real ownership fully returned after actual OFF");
  cases.back()["initialEntryProof"]={{"originalRequest",admission.requestId},{"originalDeadline",deadline},{"clockAfter",startProtectionClock},{"unexpectedHostCreated",created},{"finalSuccess",success},{"finalFailure",failure}};
  cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;return;
 }

 fixturePipeBudgetEnabled=fixturePipeBudgetActive=true;fixturePipeBudgetAdvanced=false;fixturePipeBudgetStages.clear();fixturePipeBudgetDeadline=deadline;fixturePipeBudgetCommand="initial-start";fixturePipeBudgetCurrentCommand="HELLO";
 fixturePipeBudgetAdvancePhase=longPrepare?"PREPARE_TARGET:read":"HELLO:read";fixturePipeBudgetAdvanceMs=longPrepare?19170:expires?fullPipeUserMaxMs+1:fullPipeUserMaxMs-1000;
 const bool started=inputHostStart();fixturePipeBudgetEnabled=false;
 const auto stages=fixturePipeBudgetStages;bool bounded=true,attached=true;unsigned prepareWrites=0;
 for(const auto& stage:stages){bounded=bounded&&stage.value("timeoutMs",0u)<=stage.value("remainingMs",0ULL);attached=attached&&stage.value("activeExecutionDeadline",0ULL)==deadline;prepareWrites+=stage.value("command","")=="PREPARE_TARGET"&&stage.value("stage","")=="write";}
 check(fixturePipeBudgetAdvanced&&fullPipeSpawnCount==spawns+1,"actual first Start performs real HELLO before supplied elapsed time on its owned child");
 check(attached,"normal first Start binds every actual lifecycle pipe stage to the original player execution period");
 if(longPrepare){unsigned budget=0;for(const auto& stage:stages)if(stage.value("command","")=="PREPARE_TARGET"&&stage.value("stage","")=="read")budget=stage.value("timeoutMs",0u);check(budget>=19170&&budget<=20000,"actual prepare receipt budget covers observed rejected-create cleanup within original player cap");}
 check(bounded,"normal first Start never gives a later lifecycle stage more than original remaining30s budget");
 if(expires){check(!started&&prepareWrites==0,"HELLO consuming original30s prevents first target PREPARE rather than creating a target after expiry");
  bool falseClaim=false;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-stop-request")falseClaim=falseClaim||event["detail"].value("prepareCreationPossible",false);
  check(!g_inputHostPrepareMayHaveReachedHost&&!falseClaim,"unissued PREPARE cannot create a cleanup claim or target ownership");
 }
 else check(started&&prepareWrites==1&&g_inputHostPrepared&&g_inputHostNeutralized,"first Start retains actual Prepare and Neutral success under remaining original budget");
 check(g_virtualStartProtection.view(startProtectionClock).requestId==admission.requestId&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"normal Start budget attaches without creating another player or fallback round");
 cases.back()["initialPipeBudget"]={{"started",started},{"originalDeadline",deadline},{"clockAfter",startProtectionClock},{"stages",stages},{"expires",expires}};
 fullPipeVerificationStep();fullPipeWaitForPlayerLock(on,off);check(writeTarget(on,off).value("ok",false),"initial budget case admits actual player OFF only after original period finishes");inputCaptureRefreshRealStickSettings(true);
 const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,family=="MSI"?"failure":"success");
 const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;
 if(family=="MSI")fullPipeMsiObserveReleasedPhysicalReturn(child,"initial-budget-normal-OFF-released-MSI-XInput");
 check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"first-open budget case observes actual owned child exit and SDK fixture resource disposal");
 fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));if(family=="MSI")fullPipeCheckUniqueOffFinal(closing.requestId,true);
 check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"first-open deadline retains normal owned cleanup without hidden debt");
 cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;
}
// Actual queue admission/dispatch on an unregistered PC; only external clocks,
// power generation and SDK leaves are supplied. Never erase queued work/debt.
// The shared drain clears settle, so these cases make no settle-requeue claim.
static void fullPipeQueuedInitialAuthorityCase(const char* reason,bool replaceRequest){
 seed();fixtureFullPipe=true;fullPipeFault="none";g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="PC";g_machineIdentity.product="unregistered-desktop";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=1;fixturePcPowerCapabilities=true;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 cases.push_back({{"name",std::string("INITIAL-QUEUED-AUTHORITY/PC/")+(replaceRequest?"expired-request-new-persona/":"power-generation-changed/")+reason},{"queueReason",reason}});
 const auto spawns=fullPipeSpawnCount;const auto observers=fullPipeChildObservers.size();
 const auto queueDepth=[](){std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);return g_inputHostLifecycleWorkQueue.size();};
 const auto base=targetDocument("disabled",0),on=targetDocument("dualsense-edge",1);
 check(queueDepth()==0&&!g_inputHostProcess&&!g_inputHostPrepared,"queued authority case begins after prior owned work has actually drained");
 check(sgWriteFileAtomic(YM_SETTINGS_FILE,base.dump()),"queued authority case seeds only its private initial OFF document");
 check(writeTarget(base,on).value("ok",false),"original ON enters through actual player settings admission");inputCaptureRefreshRealStickSettings(true);
 const auto original=g_virtualStartProtection.view(startProtectionClock);const auto originalDeadline=startProtectionClock+original.remainingMs;const auto originalPower=currentPowerGeneration();
 check(original.requestId&&original.locked&&original.remainingMs==fullPipeUserMaxMs&&original.persona=="dualsense-edge","actual settings consumer establishes original immutable thirty-second request");
 check(inputHostQueueLifecycleWork(reason)&&queueDepth()==1,"actual lifecycle queue contains exactly one old admitted work item");
 json current=on;
 if(replaceRequest){
  startProtectionClock+=original.remainingMs;fixturePowerClockOffset+=original.remainingMs;fullPipeVerificationStep();
  const auto expired=g_virtualStartProtection.view(startProtectionClock);
  check(expired.requestId==original.requestId&&expired.phase=="expired"&&!expired.locked,"old request reaches its original hard deadline while old work remains queued");
  current=targetDocument("steamdeck",2);
  check(writeTarget(on,current).value("ok",false),"different new ON persona is committed through real writeTarget after original expiry");inputCaptureRefreshRealStickSettings(true);
 }else{
  ++startProtectionClock;++fixturePowerClockOffset;g_powerGeneration=originalPower+1;
 }
 const auto currentRequest=g_virtualStartProtection.view(startProtectionClock);const auto currentDeadline=startProtectionClock+currentRequest.remainingMs;const auto beforeStale=fullPipeProtectionState();
 check(currentRequest.locked&&currentRequest.remainingMs>0&&(replaceRequest?currentRequest.requestId!=original.requestId&&currentRequest.persona=="steamdeck":currentRequest.requestId==original.requestId&&currentPowerGeneration()!=originalPower),"old queue item is tested against a live newer request or changed power generation");
 check(queueDepth()==1,"new settings consumption does not add fresh work before stale-item rejection is observed");
 fullPipeDrainQueuedLifecycle();
 const bool staleCreated=fullPipeSpawnCount!=spawns;
 check(!staleCreated&&!g_inputHostProcess&&!g_inputHostPrepared&&!g_hidHideVisibilityJournal.active,"old queued authority cannot borrow current request or power to create any Host or target");
 check(fullPipeProtectionState()==beforeStale&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"stale dispatch neither settles nor renews current request and never spends fallback");
 cases.back()["staleDispatchProof"]={{"originalRequest",original.requestId},{"originalDeadline",originalDeadline},{"originalPower",originalPower},{"currentRequest",currentRequest.requestId},{"currentDeadline",currentDeadline},{"currentPower",currentPowerGeneration()},{"unexpectedHostCreated",staleCreated},{"protectionBefore",beforeStale},{"protectionAfter",fullPipeProtectionState()}};
 check(inputHostQueueLifecycleWork(reason)&&queueDepth()==1,"fresh work enters the same actual queue with current request and power authority");
 startProtectionClock+=17;fixturePowerClockOffset+=17;fullPipeDrainQueuedLifecycle();
 check(fullPipeSpawnCount==spawns+1&&g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized&&g_inputHostBoundPersona==(replaceRequest?"steamdeck":"dualsense-edge"),"fresh admitted work alone creates the intended real managed Host and prepared target");
 const auto afterFresh=g_virtualStartProtection.view(startProtectionClock);
 check(afterFresh.requestId==currentRequest.requestId&&startProtectionClock+afterFresh.remainingMs==currentDeadline&&afterFresh.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"fresh dispatch retains current original deadline without another player or fallback round");
 const auto off=targetDocument("disabled",current["input"]["revision"].get<int>()+1);fullPipeWaitForPlayerLock(current,off);
 check(writeTarget(current,off).value("ok",false),"queued authority case cleans up through actual admitted player OFF");inputCaptureRefreshRealStickSettings(true);
 const auto closing=g_virtualStartProtection.view(startProtectionClock);fullPipeDrainQueuedLifecycle();fullPipeCheckStopCandidate(closing.requestId,"success");
 cases.back()["managedReceipts"]=json::array();
 for(unsigned n=0;n<2&&fullPipeChildObservers.size()>observers;++n){const auto child=fullPipeChildOutcome();cases.back()["managedReceipts"].push_back(child);check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"actual serialized OFF returns its owned managed child and SDK resources");}
 check(fullPipeChildObservers.size()==observers,"all children created by this bounded case have an actual owned exit receipt");
 fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",off["input"]["revision"].get<int>()+1));fullPipeDrainQueuedLifecycle();
 check(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"queued authority case leaves ownership returned through real OFF and dispatcher cleanup");
 cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;
}
#include "prepare_ack_cases.inc"
#include "ps_return_cases.inc"

static void fullPipePsProtectedCycleCase(const std::string& variant="central",const std::string& family="ROG"){
 seed();fixtureFullPipe=true;fixtureWireCapture=variant.rfind("receipt-",0)==0;fixturePsSameInstanceNames=false;fullPipeFault=(variant=="wake-pc-retry-transient"||variant=="wake-pc-retry-source-timeout")?"recreate-output-stop-twice":variant.rfind("wake-pc-retry-",0)==0?"recreate-output-stop-always":"none";
 g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 const int controlledFamily=family=="MSI"?0:(family=="GPD"||family=="PC")?2:1;
 if(controlledFamily){g_machineIdentity.family=controlledFamily==1?YmccFamily::AsusRogAllyX:YmccFamily::Gpd;g_machineIdentity.manufacturer=controlledFamily==1?"ASUSTEK COMPUTER INC.":"GPD";g_machineIdentity.product=controlledFamily==1?"ROG Ally X":"G1617";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;}
 if(family=="PC"){fixturePcPowerCapabilities=true;g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="FIXTURE-PC";g_machineIdentity.product="unregistered-desktop";}
 if(variant.rfind("wake-",0)==0){g_wakeRescanRequested=false;g_wakeRescanGeneration=0;g_wakeRescanArmTick=0;g_wakeHotReplaceRetryAfterTick=0;g_wakeKeptTargetGeneration=0;g_msiVirtualWakeGeneration=0;g_msiVirtualWakeArmTick=0;g_msiVirtualWakeStartAttempts=0;g_msiVirtualWakeRearmedGeneration=0;}
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_xinputAdmittedMask=controlledFamily?1:0;
 g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 cases.push_back({{"name",variant.rfind("pipe-budget-",0)==0?"PIPE-BUDGET/"+family+"/"+variant+"-real-managed-pipe":variant=="central"?"PS-WAKE/central-protection-same-power-real-managed-pipe":variant.rfind("receipt-",0)==0?"RECEIPT/"+variant+"-real-managed-pipe":variant.rfind("wake-",0)==0?"WAKE/"+family+"/"+variant+"-real-managed-pipe":"PS-WAKE/protected-boundary/"+variant},{"requestedPersona","dualsense-edge"},{"controlledFamily",family}});
 const auto oldSpawns=fullPipeSpawnCount;const auto on=targetDocument("dualsense-edge",0),off=targetDocument("disabled",1);
 check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"protected PS cycle seeds its private ON document");inputCaptureRefreshRealStickSettings(true);
 const bool started=inputHostStart();check(started&&fullPipeSpawnCount==oldSpawns+1,"protected PS cycle executes actual Start with one real managed child");
 if(!started){cases.back()["afterStart"]=state();fixtureFullPipe=false;return;}
 const auto process=g_inputHostProcess;const auto pid=GetProcessId(process);const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto host=g_inputHostHostInstanceId;
 const auto generation=currentPowerGeneration();const auto boundPower=g_inputHostBoundPowerGeneration;
 ULONG client=0;check(pid!=GetCurrentProcessId()&&GetNamedPipeClientProcessId(g_inputHostServerPipe,&client)&&client==pid,"protected PS cycle uses the real pipe connected to its owned child");
 check(generation&&boundPower==generation&&psWakePolicyActive()&&!g_msiSteamDeckReleaseFence.required(),"protected PS cycle starts with the original Host power binding and Edge policy");
 const auto publish=[&](){auto sample=fullPipeReadNow(controlledFamily);std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);
  const auto prior=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);
  sample.readContinuityGeneration=ymcc::input::recoveryHealth::nextContinuity(prior,inputHostRecoveryHealthSource(&sample,true),GetTickCount64());
  g_inputCaptureGamepadSnapshot=sample;return sample;};
 const auto sameHost=[&](){return g_inputHostProcess==process&&GetProcessId(g_inputHostProcess)==pid&&g_inputHostEpoch==epoch&&g_inputHostRunId==run&&g_inputHostHostInstanceId==host&&fullPipeSpawnCount==oldSpawns+1&&currentPowerGeneration()==generation&&g_inputHostBoundPowerGeneration==boundPower;};
 const auto finals=[&](){unsigned success=0,failure=0;const auto ticket=cases.back().value("onRequestId",0ULL);
  for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-switch-verification"&&event["detail"].value("stage","")=="final"&&event["detail"].value("requestId",0ULL)==ticket){success+=event["detail"].value("outcome","")=="success";failure+=event["detail"].value("outcome","")=="failure";}
  return std::pair<unsigned,unsigned>{success,failure};};
 const auto record=[&](const char* phase){const auto v=g_virtualStartProtection.view(startProtectionClock);
  cases.back()["psProtectionTimeline"].push_back({{"phase",phase},{"clock",startProtectionClock},{"protection",fullPipeProtectionState()},{"fallbackAttempts",v.fallbackAttempts},{"episodeAttempts",g_inputHostRecoveryEpisodeAttempts.load()},{"inspectionStartedElapsedMs",v.inspectionStartedElapsedMs},{"inspectionDeadlineElapsedMs",v.inspectionDeadlineElapsedMs},{"recreateCommands",fullPipeRecreateCommands()},{"queryCalls",fixturePsOpenCalls},{"hostSequence",g_inputHostSequence},{"processId",pid},{"hostEpoch",g_inputHostEpoch},{"powerGeneration",currentPowerGeneration()},{"boundPowerGeneration",g_inputHostBoundPowerGeneration},{"targetRevision",g_psEndpointOwnership.revision()},{"rearmRequested",g_psWakeRearmRequested.load()},{"rearmArmTick",g_psWakeRearmArmTick.load()}});};
 fullPipeDrainQueuedLifecycle();inputHostRecoveryMaintenanceTick();
 json firstAck;for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=publish();firstAck=fullPipeSubmitSource(sample,true,false);}
 const auto candidate=g_virtualStartProtection.view(startProtectionClock);cases.back()["onRequestId"]=candidate.requestId;cases.back()["firstCurrentAck"]=firstAck;
 const auto originalOwnership=g_psEndpointOwnership.snapshot();const auto accepted=ymcc::inputHost::switchVerificationRuntime::acceptedSource;
 check(firstAck.value("hostSubmission","")=="frame-accepted"&&!firstAck.value("focusIsolationSemanticNeutral",true)&&g_inputHostFreshNeutralAdmitted&&g_inputHostActive,"normal current-source execution reaches actual managed acceptance before PS recovery");
 check(candidate.requestId&&candidate.phase=="verifying"&&candidate.candidateOutcome=="success"&&candidate.locked&&candidate.verificationPending&&candidate.verificationRemainingMs==fullPipeUserSettleMs&&candidate.remainingMs==fullPipeUserMaxMs,"protected PS normal execution starts one full ten-second inspection inside the original player deadline");
 check(originalOwnership.valid&&originalOwnership.capable&&!originalOwnership.rootInstanceId.empty()&&accepted.requestId==candidate.requestId&&accepted.targetRevision==g_psEndpointOwnership.revision()&&accepted.source.sequence,"real managed PREPARE and current report establish owned PS target and accepted source authority");
 fullPipeVerificationStep();
 check(std::string(ymcc::inputHost::switchVerificationRuntime::observation.lastEvidence)=="current-host-and-source-proven"&&sameHost(),"protected PS baseline proves current source and original power binding before injecting absence");
 check(candidate.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&finals()==std::make_pair(0u,0u),"normal execution precedes self-healing without reserving fallback or reporting a final");record("normal-execution-inspection-10000");
 if(variant.rfind("wake-dead-host",0)==0){
  const bool paused=variant=="wake-dead-host-not-ready";
  startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;publish();fullPipeVerificationStep();
  check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&finals()==std::make_pair(1u,0u),"dead-Host wake starts from a real completed original request");
  const auto oldObserver=fullPipeChildObservers.back();const auto oldReceipt=fullPipeReceipts.back();
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);check(DisconnectNamedPipe(g_inputHostServerPipe)!=FALSE,"test disconnects only the real private owned native pipe endpoint");}
  const auto retired=fullPipeChildOutcome(oldObserver,oldReceipt);cases.back()["retiredManagedReceipt"]=retired;
  check(retired.value("nativeObservedProcessExit",false)&&retired.value("finalCleanupOk",false)&&!retired.value("hasController",true)&&!retired.value("hasContext",true),"retired Host actually exits and disposes controller and context before wake may create a replacement");
  check(g_inputHostProcess==process&&WaitForSingleObject(process,0)==WAIT_OBJECT_0&&g_inputHostEpoch==epoch&&!inputHostProcessAliveSnapshot(),"real signaled old process handle remains to exercise actual stale-Host return owner");
  // The known slot0 physical source is supplied only after the owned child
  // has disposed its target. Do not clear quarantine or mark return proven.
  // PS return stays unknown independently of the XInput source lock.
  fixturePsReturnManualTopology=true;fixturePsReadConfig({},ERROR_ACCESS_DENIED);
  g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;g_xinputConnectedMask=0;
  const auto wakeGeneration=generation+1;g_powerGeneration=wakeGeneration;inputHostRequestWakeRescan(wakeGeneration);
  fixturePowerClockOffset+=kWakeHandshakeWaitMs+1;startProtectionClock+=kWakeHandshakeWaitMs+1;
  inputHostConsumeWakeRescan();
  check(g_inputHostLifecycleWorkRequested.load(),"actual wake consumer queues repair of the truly exited Host");
  if(paused){g_powerLifecycle=PowerLifecycle::Suspended;g_inputReady=false;g_inputHostPowerAdmissionRevoked=true;}
  const auto hideBefore=fixtureWakeHideCalls;{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
  if(paused){
   check(fullPipeSpawnCount==oldSpawns+1&&fixtureWakeHideCalls==hideBefore&&g_wakeRescanRequested.load(),"queued dead-Host wake retains its original duty while expected power pause prevents every new endpoint write");
   g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;
   const auto now=GetTickCount64(),due=g_wakeHotReplaceRetryAfterTick.load();if(due>now){fixturePowerClockOffset+=due-now;startProtectionClock+=due-now;}
   inputHostConsumeWakeRescan();{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
  }
  fullPipeQuarantineProbe();
  check(g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&fullPipeSpawnCount==oldSpawns+1&&ymcc::inputHost::wakeExecutionRuntime::returnWait.pending,"actual unknown endpoint topology keeps old return debt and forbids replacement Host");
  const bool returnNewPlayer=variant=="wake-dead-host-new-player",returnNewPower=variant=="wake-dead-host-new-power",returnExpired=variant=="wake-dead-host-expired",startBudget=variant=="wake-dead-host-start-budget";
  if(returnNewPlayer||returnNewPower||returnExpired||startBudget){
   namespace wake=ymcc::inputHost::wakeExecutionRuntime;
   const auto original=wake::returnWait.work;const auto oldWork=wake::encode(original);const auto eventStart=fixtureEvents.size();const auto restores=fullPipeRestoreCount(),recreates=fullPipeRecreateCommands(),returnHides=fixtureWakeHideCalls;
   auto current=on;const char* retirement=returnNewPlayer?"player-request-retired":returnNewPower?"power-generation-retired":"original-wake-budget-expired";
   check(original.serial&&original.requestId==candidate.requestId&&original.generation==wakeGeneration&&original.epoch==epoch&&original.armedAt==inputHostWakeArmTick()&&!wake::episode.completed,"pending dead-Host return belongs to the exact original wake request power and Host");
   if(returnNewPlayer){
    current=targetDocument("elite",1);check(writeTarget(on,current).value("ok",false),"pending dead-Host return admits a new player persona only through actual settings IPC");inputCaptureRefreshRealStickSettings(true);
    check(g_virtualStartProtection.view(startProtectionClock).requestId!=original.requestId&&inputHostConfiguredPersona()=="elite"&&g_realStickTest.virtualTargetEnabled,"actual refresh publishes the newer player request while old return remains unproven");
   }else if(returnNewPower){
    // A changed fixture power fact must reach the old return consumer BEFORE
    // a new explicit arm, which would replace the very debt under test.
    g_powerGeneration=wakeGeneration+1;
   }else if(returnExpired){
    const auto now=GetTickCount64(),deadline=original.armedAt+original.maxAgeMs;
    check(now<deadline,"pending dead-Host return reaches expiry from within its original wake budget");
    if(deadline>now){fixturePowerClockOffset+=deadline-now;startProtectionClock+=deadline-now;}
    check(GetTickCount64()-original.armedAt>=original.maxAgeMs,"pending dead-Host return expires at the original fake-clock wake boundary");
   }
   const auto ownerBefore=fullPipeProtectionState();
   fullPipeQuarantineProbe();
   // A new explicit player intent may clear its own rearm latch; quarantine
   // must still retain the old ownership and block every replacement.
   cases.back()["unknownReturnProof"]={{"quarantined",g_inputHostReleaseQuarantined},{"startBlocked",g_inputHostStartBlocked},{"newPlayer",returnNewPlayer},{"prepared",g_inputHostPrepared},{"returnPending",wake::returnWait.pending}};
   check(g_inputHostReleaseQuarantined&&(returnNewPlayer||g_inputHostStartBlocked)&&g_inputHostPrepared&&wake::returnWait.pending&&wake::returnWait.host==epoch&&wake::returnWait.run==run&&wake::returnWait.work.serial==original.serial&&fullPipeRestoreCount()==restores&&fullPipeSpawnCount==oldSpawns+1,"unknown topology retains the exact return debt even after its wake authority becomes stale");
   // Only this existing lifecycle owner may consume endpoint absence, restore
   // visibility and decide whether the original wake is still authorized.
   fixturePsReadConfig({});
   g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;fullPipeQuarantineProbe();
   bool exactReturn=false,returnPreserved=false,returnProven=false,retiredReason=false,exactRetired=false;
   for(size_t i=eventStart;i<fixtureEvents.size();++i){const auto& event=fixtureEvents[i];const auto& d=event["detail"];
    if(event.value("event","")=="input-host-quarantine-cleared"&&d.value("retiredHostEpoch",0ULL)==epoch&&d.value("retiredRunId","")==run){exactReturn=true;returnPreserved=d.value("wakeExecutionReturnPreserved",false);}
    if(event.value("event","")=="input-host-wake-execution"){
     returnProven=returnProven||d.value("stage","")=="return-proven";
     if(d.value("stage","")=="retired"&&d.value("reason","")==retirement){retiredReason=true;exactRetired=d.value("wakeSerial",0ULL)==original.serial&&d.value("requestId",0ULL)==original.requestId&&d.value("powerGeneration",0ULL)==original.generation&&d.value("hostEpoch",0ULL)==epoch;cases.back()["returnRetirement"]=d;}
    }
   }
   check(exactReturn&&!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_inputHostPrepared&&!g_inputHostNeutralized&&!g_hidHideVisibilityJournal.active&&g_inputHostRunId.empty()&&!wake::returnWait.pending&&fullPipeRestoreCount()==restores+1,"actual return owner alone consumes the proven old endpoint and visibility debt");
   if(startBudget){
    const auto starts=[](){unsigned count=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-wake-hot-replace"&&event["detail"].value("action","")=="dead-host-start")++count;return count;};
    const auto cleanupStarts=starts(),assetChecks=static_cast<unsigned>(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"assets"));
    check(cleanupStarts==1&&returnPreserved&&returnProven&&!g_inputHostStartBlocked&&g_wakeRescanRequested.load(),"old-Host cleanup Start is excluded before counting proven-return no-Host creation attempts");
    check(ymStartSeamActive(),"real managed pipe fixture retains the existing asset-availability test boundary");g_startEntrySeam.assetsPresent=false;
    const auto playerStarted=startProtectionClock-g_virtualStartProtection.view(startProtectionClock).elapsedMs;
    for(unsigned n=0;n<static_cast<unsigned>(kWakeHotReplaceMaxAttempts)+1;++n){
     const auto now=GetTickCount64(),due=g_wakeHotReplaceRetryAfterTick.load();if(due>now){fixturePowerClockOffset+=due-now;startProtectionClock+=due-now;}
     check(GetTickCount64()-original.armedAt<original.maxAgeMs,"fourth no-Host dispatcher opportunity remains inside the original wake budget");
     inputHostConsumeWakeRescan();{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
     const auto attempts=starts()-cleanupStarts;const auto v=g_virtualStartProtection.view(startProtectionClock);
     cases.back()["deadHostStartBudget"].push_back({{"dispatch",n+1},{"actualStartAttempts",attempts},{"assetChecks",static_cast<unsigned>(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"assets"))-assetChecks},{"wakePending",g_wakeRescanRequested.load()},{"state",state()}});
     check(attempts==(std::min)(n+1,static_cast<unsigned>(kWakeHotReplaceMaxAttempts)),"PC dead-Host wake permits at most three actual no-Host Start attempts after proven return");
     check(wake::episode.serial==original.serial&&wake::episode.requestId==original.requestId&&wake::episode.generation==original.generation&&wake::episode.armedAt==original.armedAt&&wake::episode.maxAgeMs==original.maxAgeMs&&wake::episode.playerDeadline==original.playerDeadline&&inputHostWakeArmTick()==original.armedAt&&startProtectionClock-v.elapsedMs==playerStarted&&v.requestId==candidate.requestId&&v.phase=="success","failed no-Host Starts never renew the original wake arm request or player period");
    }
    const auto attempts=starts()-cleanupStarts;
    check(attempts==static_cast<unsigned>(kWakeHotReplaceMaxAttempts)&&static_cast<unsigned>(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"assets"))-assetChecks==attempts,"no-Host budget counts actual asset-gated Start failures and excludes waiting for old return");
    check(wake::episode.completed&&!g_wakeRescanRequested.load(),"PC no-Host Start budget exhaustion retires its original wake without another retry");
    inputHostRequestWakeRescan(wakeGeneration);const auto now=GetTickCount64(),due=g_wakeHotReplaceRetryAfterTick.load();if(due>now){fixturePowerClockOffset+=due-now;startProtectionClock+=due-now;}
    inputHostConsumeWakeRescan();{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
    check(starts()-cleanupStarts==static_cast<unsigned>(kWakeHotReplaceMaxAttempts)&&wake::episode.armedAt==original.armedAt&&wake::episode.serial==original.serial&&!g_wakeRescanRequested.load(),"duplicate wake notification cannot revive an exhausted no-Host Start budget");
   }else{
    check(!returnPreserved&&!returnProven&&retiredReason&&wake::episode.completed&&g_inputHostStartBlocked&&!g_wakeRescanRequested.load(),"stale dead-Host return is retired without authorizing a new target");
    // Assert before a replay can emit a second, correctly copied Work log.
    // retireLocked(debt.work) must log this identity before clearing its alias.
    check(exactRetired,"rejected dead-Host return retirement logs exact original wakeSerial requestId powerGeneration and hostEpoch");
    check(inputHostQueueLifecycleWork(oldWork.c_str()),"retired dead-Host wake token enters the actual dispatcher for replay rejection");
    {FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}inputHostConsumeWakeRescan();
    check(fullPipeProtectionState()==ownerBefore&&wake::episode.serial==original.serial&&wake::episode.armedAt==original.armedAt&&wake::episode.maxAgeMs==original.maxAgeMs&&wake::episode.requestId==original.requestId&&wake::episode.generation==original.generation&&inputHostWakeArmTick()==original.armedAt&&g_inputHostStartBlocked&&!g_wakeRescanRequested.load(),"retired return and late wake replay cannot renew or authorize the newer player power or expired period");
   }
   check(fullPipeSpawnCount==oldSpawns+1&&!g_inputHostProcess&&!g_inputHostPrepared&&fullPipeRecreateCommands()==recreates&&(startBudget||fixtureWakeHideCalls==returnHides)&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&finals()==std::make_pair(1u,0u),"rejected or exhausted dead-Host wake never resurrects a target charges fallback or duplicates the original player final");
   const auto finalOff=targetDocument("disabled",current["input"]["revision"].get<int>()+1);fullPipeWaitForPlayerLock(current,finalOff);check(writeTarget(current,finalOff).value("ok",false),"dead-Host boundary exits through an admitted actual player OFF");inputCaptureRefreshRealStickSettings(true);
   // Retired wake work still blocks a new Start, but its actually returned PC
   // resources permit a successful OFF candidate. StartBlocked is not return debt.
   const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,"success");
   fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(finalOff,targetDocument("dualsense-edge",finalOff["input"]["revision"].get<int>()+1));
   check(fullPipeSpawnCount==oldSpawns+1&&!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_inputHostPrepared&&!g_hidHideVisibilityJournal.active&&!wake::returnWait.pending,"dead-Host boundary leaves all actual endpoint process and visibility ownership returned");
   cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;return;
  }
  fixturePsReadConfig({});
  g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;
  // If the existing return owner needs an endpoint read, execute it, retaining
  // any unresolved proof. Never mutate its debt/start-blocked ownership flags.
  for(unsigned n=0;n<4&&(!g_inputHostProcess||!inputHostProcessAliveSnapshot());++n){
   fullPipeQuarantineProbe();inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
   const auto now=GetTickCount64(),due=g_wakeHotReplaceRetryAfterTick.load();if(due>now){fixturePowerClockOffset+=due-now;startProtectionClock+=due-now;}
   inputHostConsumeWakeRescan();{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
  }
  const bool restarted=g_inputHostProcess&&inputHostProcessAliveSnapshot()&&g_inputHostEpoch!=epoch;
  check(restarted&&fullPipeSpawnCount==oldSpawns+2,"dead-Host wake consumes proven old return then creates exactly one new owned Host");
  check(!g_wakeRescanRequested.load()&&!g_inputHostReleaseQuarantined&&g_inputHostPrepared&&g_inputHostNeutralized,"newly prepared actual Host completes only its own wake episode without leaving a pending retry or return debt");
  bool exactReturn=false,exactCompletion=false;
  for(const auto& event:fixtureEvents){
   if(event.value("event","")=="input-host-quarantine-cleared")exactReturn=event["detail"].value("wakeExecutionReturnPreserved",false)&&event["detail"].value("retiredHostEpoch",0ULL)==epoch&&event["detail"].value("retiredRunId","")==run;
   if(event.value("event","")=="input-host-wake-execution"&&event["detail"].value("stage","")=="complete")exactCompletion=event["detail"].value("hostEpoch",0ULL)==g_inputHostEpoch&&g_inputHostEpoch!=epoch;
  }
  check(exactReturn&&exactCompletion,"dead-Host wake return proof names exact retired tuple and completion names actual new Host");
  json fresh;if(restarted){for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=publish();fresh=fullPipeSubmitSource(sample,true,false);}}
  check(restarted&&fresh.value("hostSubmission","")=="frame-accepted"&&!fresh.value("focusIsolationSemanticNeutral",true),"replacement Host admits actual fresh current-power input after old child disposal");
  check(g_virtualStartProtection.view(startProtectionClock).requestId==candidate.requestId&&finals()==std::make_pair(1u,0u)&&g_inputHostRecoveryEpisodeAttempts.load()==0,"normal dead-Host wake never creates a second player result or charges fallback");
  check(writeTarget(on,off).value("ok",false),"dead-Host wake closes through actual new player OFF");inputCaptureRefreshRealStickSettings(true);
  const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,family=="MSI"?"failure":"success");
  if(fullPipeSpawnCount==oldSpawns+2){const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;if(family=="MSI")fullPipeMsiObserveReleasedPhysicalReturn(child,"normal-PS-OFF-released-MSI-XInput");check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false),"replacement Host also exits naturally with actual owned cleanup");}
  fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));if(family=="MSI")fullPipeCheckUniqueOffFinal(closing.requestId,true);
  check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"dead-Host wake case ends without target visibility or process ownership debt");
  cases.back()["currentPowerAck"]=fresh;cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;return;
 }
 if(variant.rfind("release-budget-",0)==0){
  startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;publish();fullPipeVerificationStep();
  check(g_virtualStartProtection.view(startProtectionClock).phase=="success","release budget begins after original ON inspection finishes");
  check(writeTarget(on,off).value("ok",false),"release budget uses an actual new admitted OFF");inputCaptureRefreshRealStickSettings(true);
  const auto closing=g_virtualStartProtection.view(startProtectionClock);
  const auto originalDeadline=startProtectionClock+closing.remainingMs;
  if(variant=="release-budget-near-deadline"){startProtectionClock+=25000;fixturePowerClockOffset+=25000;}
  fixturePipeBudgetEnabled=true;fixturePipeBudgetActive=false;fixturePipeBudgetAdvanced=false;fixturePipeBudgetStages.clear();
  fixturePipeBudgetCommand="RELEASE_TARGET";fixturePipeBudgetDeadline=originalDeadline;
  fixturePipeBudgetAdvancePhase=variant=="release-budget-operation"?"write":"read";
  fixturePipeBudgetAdvanceMs=variant=="release-budget-long"?10742:variant=="release-budget-operation"?600:0;
  inputHostStop("virtual-target-disabled");fixturePipeBudgetEnabled=false;
  const auto stages=fixturePipeBudgetStages;bool bounded=!stages.empty();unsigned reads=0,writes=0;DWORD readBudget=0,writeBudget=0;
  for(const auto& stage:stages){bounded=bounded&&stage.value("activeExecutionDeadline",0ULL)==originalDeadline&&stage.value("timeoutMs",0u)<=stage.value("remainingMs",0ULL);if(stage.value("stage","")=="read"){++reads;readBudget=stage.value("timeoutMs",0u);}if(stage.value("stage","")=="write"){++writes;writeBudget=stage.value("timeoutMs",0u);}}
  check(bounded&&reads==1&&writes==1,"actual OFF release pipe keeps the original thirty-second player deadline at every stage");
  if(variant=="release-budget-long")check(readBudget>=10742&&readBudget<=20000,"actual release receipt budget admits observed 10.742-second SDK cleanup within one command ceiling");
  if(variant=="release-budget-operation")check(writeBudget>=600&&readBudget<=writeBudget-600,"actual release read cannot renew time consumed by its write stage");
  if(variant=="release-budget-near-deadline")check(writeBudget<=5000&&readBudget<=5000,"late OFF release cannot acquire a fresh twenty-second wait beyond original hard cap");
  const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;
  check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"budgeted actual release observes orderly owned Host resource disposal");
  check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&fullPipeSpawnCount==oldSpawns+1,"budget correction never creates a second Host or leaves false release quarantine");
  check(g_inputHostSwitchRecoveryStartDeadline==0,"OFF borrowed execution deadline is restored after actual Stop");
  fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));
  cases.back()["releaseBudgetStages"]=stages;cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;return;
 }
 if(variant.rfind("pipe-budget-",0)==0){
  const bool connect=variant.find("connect")!=std::string::npos,expires=variant.find("expiry")!=std::string::npos;
  const auto originalDeadline=startProtectionClock+g_virtualStartProtection.view(startProtectionClock).remainingMs;
  const auto advance=originalDeadline-startProtectionClock-1000;startProtectionClock+=advance;fixturePowerClockOffset+=advance;
  fixturePipeBudgetEnabled=fixturePipeBudgetActive=true;fixturePipeBudgetAdvanced=false;fixturePipeBudgetStages.clear();fixturePipeBudgetDeadline=originalDeadline;
  fixturePipeBudgetAdvancePhase=connect?"connect":"write";fixturePipeBudgetAdvanceMs=expires?1001:600;
  if(connect)g_inputHostPipeConnected=false; // Cache unknown; actual owned Windows pipe/client remains connected.
  bool knownNegative=true,ok=false;
  {
   struct OriginalRequestScope{std::uint64_t deadline=g_inputHostSwitchRecoveryStartDeadline,request=g_inputHostSwitchRecoveryStartRequestId,power=g_inputHostSwitchRecoveryStartPower;
    OriginalRequestScope(std::uint64_t d,std::uint64_t r,std::uint64_t p){g_inputHostSwitchRecoveryStartDeadline=d;g_inputHostSwitchRecoveryStartRequestId=r;g_inputHostSwitchRecoveryStartPower=p;}
    ~OriginalRequestScope(){g_inputHostSwitchRecoveryStartDeadline=deadline;g_inputHostSwitchRecoveryStartRequestId=request;g_inputHostSwitchRecoveryStartPower=power;}
   } original(originalDeadline,candidate.requestId,generation);
   ok=inputHostRecreatePreparedTarget(&knownNegative);
  }
  fixturePipeBudgetEnabled=false;const auto stages=fixturePipeBudgetStages;
  check(fixturePipeBudgetAdvanced&&!stages.empty(),"actual owned pipe stage completes before controlled elapsed time is supplied");
  bool within=true;unsigned reads=0,writes=0;for(const auto& stage:stages){within=within&&stage.value("timeoutMs",0u)<=stage.value("remainingMs",0ULL);reads+=stage.value("stage","")=="read";writes+=stage.value("stage","")=="write";}
  check(within,"every actual connect write and read receives only the original player's remaining deadline budget");
  if(expires){check(!ok&&!knownNegative,"original deadline expiry is not fabricated as an owned SDK negative receipt");check(connect?writes==0:reads==0,"expired preceding stage cannot issue another pipe operation after original hard deadline");}
  else{check(ok&&!knownNegative&&reads==1&&writes==1,"remaining-budget scenario retains real Host RECREATE neutral and accepted ownership on one actual command");}
  check(g_virtualStartProtection.view(startProtectionClock).requestId==candidate.requestId&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"pipe stage budget neither renews player identity nor charges fallback");
  cases.back()["pipeBudget"]={{"variant",variant},{"family",family},{"originalDeadline",originalDeadline},{"clockAfter",startProtectionClock},{"stages",stages},{"ok",ok},{"knownNegative",knownNegative}};
  fullPipeVerificationStep();fullPipeWaitForPlayerLock(on,off);check(writeTarget(on,off).value("ok",false),"pipe budget case exits through actual admitted player OFF");inputCaptureRefreshRealStickSettings(true);
  const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,family=="MSI"?"failure":"success");
  const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;if(family=="MSI")fullPipeMsiObserveReleasedPhysicalReturn(child,"normal-PS-OFF-released-MSI-XInput");
  check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"pipe budget case observes real owned child exit and actual resource disposal");
  fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));if(family=="MSI")fullPipeCheckUniqueOffFinal(closing.requestId,true);
  check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"pipe budget case retains normal owned cleanup and no visibility debt");
  cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;return;
 }
 if(variant.rfind("wake-pc-retry-",0)==0){
  const bool sourceTimeout=variant=="wake-pc-retry-source-timeout";
  const bool transient=variant=="wake-pc-retry-transient"||sourceTimeout,expires=variant=="wake-pc-retry-deadline";
  check(!sgShouldKeepInputDuringStandby(),"retry case consumes actual non-AoAc PC policy");
  const auto wakeGeneration=generation+1;g_powerGeneration=wakeGeneration;inputHostRequestWakeRescan(wakeGeneration);
  const auto originalArm=inputHostWakeArmTick();fixturePowerClockOffset+=kWakeHandshakeWaitMs+1;startProtectionClock+=kWakeHandshakeWaitMs+1;
  const auto recreates=fullPipeRecreateCommands(),sleeps=fixtureWakeBlockingSleeps;
  inputHostConsumeWakeRescan();{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
  check(fullPipeRecreateCommands()==recreates+1,"one wake dispatch executes at most one actual RECREATE even after a negative SDK release receipt");
  check(fixtureWakeBlockingSleeps==sleeps,"wake retry never blocks lifecycle ownership in an OS Sleep");
  check(g_virtualStartProtection.view(startProtectionClock).candidateOutcome=="failure"&&finals()==std::make_pair(0u,0u),"actual negative recreate receipt invalidates provisional success without creating a second final");
  if(expires){
   const auto remaining=g_virtualStartProtection.view(startProtectionClock).remainingMs;startProtectionClock+=remaining;
   const auto before=fullPipeRecreateCommands();inputHostConsumeWakeRescan();{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}fullPipeVerificationStep();
   check(fullPipeRecreateCommands()==before&&!g_wakeRescanRequested.load(),"original30s expiry cancels a scheduled recreate retry without another command");
   check(finals()==std::make_pair(0u,1u),"expired wake retry publishes one original-request failure only");
  }else{
   unsigned turns=0;while(g_wakeRescanRequested.load()&&turns++<8){
    const auto before=fullPipeRecreateCommands();const auto now=GetTickCount64();const auto due=g_wakeHotReplaceRetryAfterTick.load();
    if(due>now){inputHostConsumeWakeRescan();check(fullPipeRecreateCommands()==before&&!g_inputHostLifecycleWorkRequested.load(),"retry backoff returns ownership without queuing another command before it is due");const auto advance=due-now;fixturePowerClockOffset+=advance;startProtectionClock+=advance;}
    inputHostRequestWakeRescan(wakeGeneration);check(inputHostWakeArmTick()==originalArm,"duplicate wake notification retains original retry episode and deadline");
    inputHostConsumeWakeRescan();{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
    check(fullPipeRecreateCommands()<=before+1,"later queue consumption cannot reset and replay the three-attempt batch");
   }
   check(fullPipeRecreateCommands()==recreates+3&&!g_wakeRescanRequested.load(),"whole wake episode has one three-attempt ceiling across every retry dispatcher");
   check(g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"ordinary wake retries never charge the central or shared self-healing budget");
   if(transient){
    fullPipeVerificationStep();
    check(finals()==std::make_pair(0u,0u)&&g_virtualStartProtection.view(startProtectionClock).locked,"recreate success after negative receipts waits for its first actual current-source ACK before finalizing");
    if(sourceTimeout){const auto before=fullPipeRecreateCommands();startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).remainingMs;fullPipeVerificationStep();
     check(finals()==std::make_pair(0u,1u)&&g_virtualStartProtection.view(startProtectionClock).phase=="expired"&&fullPipeRecreateCommands()==before,"successful endpoint without fresh input expires at original30s with one failure and no extra recreate");
    }else{json fresh;for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=publish();fresh=fullPipeSubmitSource(sample,true,false);}cases.back()["currentPowerAck"]=fresh;
    check(fresh.value("hostSubmission","")=="frame-accepted"&&!fresh.value("focusIsolationSemanticNeutral",true),"two real failed release receipts followed by actual recreation restore usable current input");
    startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;publish();fullPipeVerificationStep();
    check(finals()==std::make_pair(1u,0u)&&g_virtualStartProtection.view(startProtectionClock).phase=="success","transient ordinary retry completes original inspected request once as success");}
   }else{startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;fullPipeVerificationStep();
    check(finals()==std::make_pair(0u,1u)&&g_virtualStartProtection.view(startProtectionClock).phase=="failure","known retry exhaustion finalizes once after original active inspection rather than waiting for hard expiry");}
  }
  check(fixtureWakeBlockingSleeps==sleeps,"all retry dispatches remain nonblocking at the OS wait boundary");
  unsigned loggedAttempts=0;bool truthful=true;
  for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-wake-execution"&&event["detail"].value("stage","")=="recreate-attempt"){
   ++loggedAttempts;truthful=truthful&&event["detail"].value("hostEpoch",0ULL)==epoch&&event["detail"].value("recreateAttempts",0u)==loggedAttempts;
  }
  check(truthful&&loggedAttempts==fullPipeRecreateCommands()-recreates,"wake attempt logs preserve actual queued Host identity and exact whole-episode attempt count");
  for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-wake-execution"&&event["detail"].value("stage","")=="complete")
   check(event["detail"].value("recreateAttempts",0u)==loggedAttempts,"completed wake log includes the final reserved attempt rather than a stale queue snapshot");
  check(g_inputHostProcess==process&&g_inputHostEpoch==epoch&&fullPipeSpawnCount==oldSpawns+1&&g_inputHostBoundPowerGeneration==boundPower,"normal recreate retry preserves the original single Host and pipe binding");
  fullPipeWaitForPlayerLock(on,off);check(writeTarget(on,off).value("ok",false),"retry case exits through actual player OFF");inputCaptureRefreshRealStickSettings(true);
  const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,family=="MSI"?"failure":"success");
  const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;if(family=="MSI")fullPipeMsiObserveReleasedPhysicalReturn(child,"normal-PS-OFF-released-MSI-XInput");
  check(child.value("faultConsumed",false)&&child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"retry failure evidence comes from real Host handling and natural owned resource cleanup");
  fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));if(family=="MSI")fullPipeCheckUniqueOffFinal(closing.requestId,true);
  check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"retry case leaves no target or visibility ownership debt");
  cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;return;
 }
 if(variant=="wake-pc-recreate"){
  check(fixturePcPowerCapabilities&&!sgShouldKeepInputDuringStandby()&&g_machineIdentity.family==YmccFamily::Unknown,"unregistered PC consumes explicit non-AoAc capability facts through actual OS-policy reader");
  const auto wakeGeneration=generation+1;g_powerGeneration=wakeGeneration;
  inputHostRequestWakeRescan(wakeGeneration);
  fixturePowerClockOffset+=kWakeHandshakeWaitMs+1;startProtectionClock+=kWakeHandshakeWaitMs+1;
  const auto recreates=fullPipeRecreateCommands();inputHostConsumeWakeRescan();
  check(g_inputHostLifecycleWorkRequested.load()&&!g_wakeRescanRequested.load(),"ordinary PC wake queues original RECREATE work after real clock and handshake budget");
  {FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
  check(fullPipeRecreateCommands()==recreates+1&&g_inputHostProcess==process&&g_inputHostEpoch==epoch&&fullPipeSpawnCount==oldSpawns+1&&g_inputHostPrepared&&g_inputHostNeutralized&&g_psEndpointOwnership.revision()>originalOwnership.revision,"ordinary PC wake executes one real managed RECREATE with new owned target revision on original Host");
  check(!g_wakeRescanRequested.load()&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"successful ordinary PC wake completes execution without charging self-healing");
  json fresh;for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto sample=publish();fresh=fullPipeSubmitSource(sample,true,false);}
  check(fresh.value("hostSubmission","")=="frame-accepted"&&!fresh.value("focusIsolationSemanticNeutral",true)&&ymcc::inputHost::switchVerificationRuntime::acceptedSource.context.power==wakeGeneration,"fresh current-power PC input remains admitted after actual same-Host target recreation");
  const auto previousHardwareGate=g_hardwareWriteGate.exchange(true);
  const auto apiProof=SleepRecoveryApi::inputSnapshot();
  check(apiProof.value("powerBindingProofGeneration",0ULL)==wakeGeneration&&SleepRecoveryApi::inputAllowed(apiProof,wakeGeneration,epoch),"actual input-repair API consumes the same current Host power proof without changing protocol binding");
  g_hardwareWriteGate=previousHardwareGate;
  startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;publish();fullPipeVerificationStep();
  check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&finals()==std::make_pair(1u,0u),"ordinary PC wake preserves the original execution inspection and finishes with one success");
  check(inputHostRecreatePreparedTarget()&&g_inputHostRecreatedWakePowerProof.generation==0&&inputHostWakeBindingProofLocked()!=wakeGeneration,"later real target replacement invalidates the old wake recreation proof before new input verification");
  fullPipeWaitForPlayerLock(on,off);check(writeTarget(on,off).value("ok",false),"ordinary PC wake exits through admitted player OFF");inputCaptureRefreshRealStickSettings(true);
  const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,family=="MSI"?"failure":"success");
  const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;if(family=="MSI")fullPipeMsiObserveReleasedPhysicalReturn(child,"normal-PS-OFF-released-MSI-XInput");cases.back()["currentPowerAck"]=fresh;
  check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"ordinary PC wake child exits with controller and context disposed");
  fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));if(family=="MSI")fullPipeCheckUniqueOffFinal(closing.requestId,true);
  check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&!g_inputHostRecreatedWakePowerProof.epoch&&!g_inputHostRecreatedWakePowerProof.generation,"ordinary PC wake leaves no virtual or physical visibility debt");
  cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;return;
 }
 if(variant.rfind("wake-",0)==0){
  if(family=="MSI"&&variant=="wake-old-generation")check(nativeLifecycleEventIsBase("input-host-wake-execution")&&nativeLifecycleEventIsBase("input-host-tuple-recovery")&&nativeLifecycleEventIsBase("input-host-switch-execution"),"shared execution and recovery transitions are retained in ordinary exported logs");
  json current=on;fixtureWakeHideResult=true;
  if(variant=="wake-new-player"){startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;publish();fullPipeVerificationStep();check(g_virtualStartProtection.view(startProtectionClock).phase=="success","original ON truly finishes before a steady-state wake duty is armed");}
  inputHostRequestWakeRescan(generation);
  if(variant=="wake-inspection-pending"){startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;publish();fullPipeVerificationStep();check(g_virtualStartProtection.view(startProtectionClock).locked&&g_virtualStartProtection.view(startProtectionClock).phase=="verifying"&&finals()==std::make_pair(0u,0u),"current protected wake execution pending before queueing cannot release the player final");}
  const auto originalArm=inputHostWakeArmTick();
  if(variant=="wake-duplicate-arm"){
   const auto waitUntil=GetTickCount64()+100;while(GetTickCount64()==originalArm&&GetTickCount64()<waitUntil)Sleep(1);
   inputHostRequestWakeRescan(generation);
   check(inputHostWakeArmTick()==originalArm,"duplicate wake notification retains the original effective operation deadline");
  }
  inputHostConsumeWakeRescan();std::string work;
  {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);for(const auto& reason:g_inputHostLifecycleWorkQueue)if(reason.rfind("wake-hot-replace:",0)==0)work=reason;}
  check(!work.empty(),"real wake request and consumer queue the current owned work before the tested context change");
  auto nextGeneration=generation;
  if(variant=="wake-old-generation"){
   nextGeneration=generation+1;g_powerGeneration=nextGeneration;inputHostRequestWakeRescan(nextGeneration);
  }else if(variant=="wake-power-not-ready"){
   g_powerLifecycle=PowerLifecycle::Suspended;g_inputReady=false;g_inputHostPowerAdmissionRevoked=true;fixtureWakeHideResult=false;
  }else if(variant=="wake-player-expired"){
   startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).remainingMs;fullPipeVerificationStep();check(g_virtualStartProtection.view(startProtectionClock).phase=="expired"&&finals()==std::make_pair(0u,1u),"queued protected wake reaches only the original player thirty-second hard cap");
  }else if(variant=="wake-new-player"){
   startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;publish();fullPipeVerificationStep();
   check(writeTarget(on,off).value("ok",false),"actual completed ON admits OFF before wake work is dispatched");inputCaptureRefreshRealStickSettings(true);
   const auto closing=g_virtualStartProtection.view(startProtectionClock);startProtectionClock+=closing.remainingMs;fullPipeVerificationStep();
   current=targetDocument("dualsense-edge",2);check(writeTarget(off,current).value("ok",false),"actual newer same-persona ON follows uncompleted OFF original hard cap");inputCaptureRefreshRealStickSettings(true);
   check(g_virtualStartProtection.view(startProtectionClock).requestId!=candidate.requestId&&sameHost(),"new player intent deliberately retains the same Host and power as the queued old wake duty");
  }else if(variant=="wake-power-during-hide"){
   fixtureWakeBeforeHideReturn=[&]{nextGeneration=generation+1;g_powerGeneration=nextGeneration;g_powerLifecycle=PowerLifecycle::Suspended;g_inputReady=false;g_inputHostPowerAdmissionRevoked=true;inputHostRequestWakeRescan(nextGeneration);};
  }
  const auto hideBefore=fixtureWakeHideCalls;const auto commandsBefore=g_inputHostSequence;const auto ownerBefore=fullPipeProtectionState();
  {FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
  const bool currentWork=variant=="wake-duplicate-arm"||variant=="wake-inspection-pending",enteredBeforeChange=variant=="wake-power-during-hide";
  check(fixtureWakeHideCalls==hideBefore+((currentWork||enteredBeforeChange)?1:0),"old generation paused or retired-player wake duty cannot reach a new hide operation");
  check(g_inputHostSequence==commandsBefore&&g_inputHostEpoch==epoch&&fullPipeSpawnCount==oldSpawns+1&&fullPipeProtectionState()==ownerBefore,"wake context rejection changes no pipe target process or player protection state");
  if(currentWork)check(g_wakeKeptTargetGeneration.load()==generation&&!g_wakeRescanRequested.load(),"one current wake duty retains the existing target and closes its own arm");
  else check(g_wakeKeptTargetGeneration.load()!=nextGeneration,"an old or paused wake duty cannot manufacture current-generation kept-target proof");
  if(variant=="wake-old-generation"||enteredBeforeChange)check(g_wakeRescanGeneration.load()==nextGeneration&&g_wakeRescanRequested.load(),"old wake completion cannot clear the newer power generation's pending arm");
  g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;fixtureWakeHideResult=true;
  if(variant=="wake-old-generation"||variant=="wake-power-not-ready"||enteredBeforeChange){
   g_wakeHotReplaceRetryAfterTick=0;const auto retryHides=fixtureWakeHideCalls;inputHostConsumeWakeRescan();{FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
   check(fixtureWakeHideCalls==retryHides+1&&g_wakeKeptTargetGeneration.load()==nextGeneration&&!g_wakeRescanRequested.load(),"the retained current Ready wake executes exactly once after stale or paused work is rejected");
  }
  json freshAck;for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto fresh=publish();freshAck=fullPipeSubmitSource(fresh,true,false);}
  check(freshAck.value("hostSubmission","")=="frame-accepted"&&!freshAck.value("focusIsolationSemanticNeutral",true),"actual fresh current-power report remains usable after the wake boundary");
  if(variant=="wake-inspection-pending"){fullPipeVerificationStep();check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&g_virtualStartProtection.view(startProtectionClock).elapsedMs==fullPipeUserSettleMs,"current wake completion releases the original inspected request without another window");}
  if(variant=="wake-new-player")check(g_virtualStartProtection.view(startProtectionClock).phase=="verifying"&&g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs==fullPipeUserSettleMs,"the newer player's actual accepted frame starts its own inspection rather than borrowing the old duty");
  const auto finalOff=targetDocument("disabled",current["input"]["revision"].get<int>()+1);fullPipeWaitForPlayerLock(current,finalOff);
  check(writeTarget(current,finalOff).value("ok",false),"wake boundary exits through actual admitted player OFF");inputCaptureRefreshRealStickSettings(true);
  const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,family=="MSI"?"failure":"success");
  const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;if(family=="MSI")fullPipeMsiObserveReleasedPhysicalReturn(child,"normal-PS-OFF-released-MSI-XInput");
  check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"wake boundary child actually exits with all local resources released");
  fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(finalOff,targetDocument("dualsense-edge",finalOff["input"]["revision"].get<int>()+1));if(family=="MSI")fullPipeCheckUniqueOffFinal(closing.requestId,true);
  check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"wake boundary leaves no target or hidden-device ownership debt");
  cases.back()["wakeBoundary"]={{"kind",variant},{"family",family},{"generation",generation},{"nextGeneration",nextGeneration},{"work",work},{"effectiveArm",originalArm},{"before",ownerBefore},{"after",fullPipeProtectionState()}};
  cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;return;
 }
 if(variant.rfind("receipt-",0)==0){
  const bool trusted=variant=="receipt-owned-replay";fixtureWireFault=trusted?"replay":variant=="receipt-untrusted-asset"?"asset":"identity";
  const auto oldObserver=fullPipeChildObservers.back();const auto oldReceiptPath=fullPipeReceipts.back();bool oldObserved=false;
  check(!fixtureWirePrevious.empty(),"wire fault reuses an actually observed prior managed frame receipt");
  fixturePsLastAuthority=originalOwnership;fixturePsLastSdkSerial=1;
  startProtectionClock+=100;auto sample=publish();const auto rejected=fullPipeSubmitSource(sample,true,false);
  check(fixtureWireFaultConsumed&&rejected.value("hostSubmission","")!="frame-accepted"&&!g_inputHostTransportFault,"actual reply parser rejects the controlled receipt without inventing a pipe transport error");
  const auto prefix="tuple-mismatch-reopen:"+std::to_string(epoch)+":"+std::to_string(generation)+":"+std::to_string(candidate.requestId)+":";
  bool tupleQueued=false,boundQueued=false;
  {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);for(const auto& token:g_inputHostLifecycleWorkQueue)if(token.rfind("tuple-mismatch-reopen:",0)==0){tupleQueued=true;boundQueued=boundQueued||(token.rfind(prefix,0)==0&&std::count(token.begin(),token.end(),':')==6);}}
  check(tupleQueued==trusted,"only a receipt with current owned identity and verified assets may queue automatic tuple recovery");
  check(boundQueued==trusted,"actual tuple-mismatch producer captures the original complete request Host power and report token");
  fullPipeDrainQueuedLifecycle();const auto inspected=g_virtualStartProtection.view(startProtectionClock);
  check(inspected.requestId==candidate.requestId&&inspected.locked&&inspected.verificationRemainingMs==9900&&inspected.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_inputHostProcess==process,"nontransport receipt fault retains the current Host through original inspection without early Stop or fallback");
  if(trusted)check(g_inputHostSwitchRecoveryPending.load()&&g_inputHostSwitchRecovery.tupleFault&&!g_inputHostSwitchRecovery.stopInvoked,"trusted tuple receipt fault waits for the central post-inspection fallback");
  else check(!g_inputHostSwitchRecoveryPending.load()&&!g_inputHostSwitchRecovery.pending,"identity or asset refusal cannot schedule an automatic fallback");
  // Withhold PS return before the post-inspection Stop can reconcile it.
  if(trusted){fixturePsReturnManualTopology=true;fixturePsReadConfig({},ERROR_ACCESS_DENIED);}
  startProtectionClock+=inspected.verificationRemainingMs;publish();inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  if(trusted){
   if(g_inputHostSwitchRecovery.stopInvoked){
    cases.back()["retiredManagedReceipt"]=fullPipeChildOutcome(oldObserver,oldReceiptPath);oldObserved=true;
    fullPipeQuarantineProbe();
    check(g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&fullPipeSpawnCount==oldSpawns+1&&g_inputHostRecoveryEpisodeAttempts.load()==1,"unknown endpoint topology retains old return proof and the one spent fallback without another Host");
    // Existing controlled XInput source is physical slot0. Observe that known
    // topology only after actual old-child disposal; no quarantine/debt flags
    // are written here. The real return owner must consume this endpoint fact.
    fixturePsReadConfig({});
    g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;
    fullPipeQuarantineProbe();publish();inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
   }
   check(g_inputHostProcess&&fullPipeSpawnCount==oldSpawns+2&&g_inputHostEpoch!=epoch&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==1&&g_inputHostRecoveryEpisodeAttempts.load()==1,"trusted receipt mismatch uses one central/shared fallback and exactly one replacement Host after inspection");
   if(g_inputHostProcess&&g_inputHostEpoch!=epoch){for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){auto fresh=publish();fullPipeSubmitSource(fresh,true,false);}fullPipeVerificationStep();fixturePsLastAuthority=g_psEndpointOwnership.snapshot();}
   check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&finals()==std::make_pair(1u,0u),"actual current replacement report uniquely completes the trusted receipt recovery");
  }else{
   fullPipeVerificationStep();check(g_virtualStartProtection.view(startProtectionClock).phase=="failure"&&finals()==std::make_pair(0u,1u)&&fullPipeSpawnCount==oldSpawns+1&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_inputHostProcess==process,"untrusted receipt ends once as honest failure with no automatic Stop or recreation");
  }
  fullPipeWaitForPlayerLock(on,off);check(writeTarget(on,off).value("ok",false),"receipt recovery case exits through real admitted OFF");inputCaptureRefreshRealStickSettings(true);
  const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");
  const auto stopped=g_virtualStartProtection.view(startProtectionClock);check(stopped.requestId==closing.requestId&&stopped.phase=="verifying"&&stopped.locked,"actual OFF cleanup supplies one provisional candidate while return proof is observed");
  if(!oldObserved){cases.back()["retiredManagedReceipt"]=fullPipeChildOutcome(oldObserver,oldReceiptPath);oldObserved=true;}
  if(!fullPipeChildObservers.empty())cases.back()["managedReceipt"]=fullPipeChildOutcome();else cases.back()["managedReceipt"]=cases.back()["retiredManagedReceipt"];
  fullPipeQuarantineProbe();fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(off,targetDocument("dualsense-edge",2));
  check(!g_inputHostProcess&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&fullPipeChildObservers.empty()&&g_virtualStartProtection.view(startProtectionClock).phase=="success","receipt fault/retry finishes actual OFF proof with no live child or physical visibility debt");
  cases.back()["actualOriginalReply"]=fixtureWireOriginal;cases.back()["injectedWireReply"]=fixtureWireInjected;cases.back()["rejectedAck"]=rejected;cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;return;
 }
 if(variant!="central"){
  const auto recreates=fullPipeRecreateCommands(),queries=fixturePsOpenCalls;
  json current=on;bool natural=variant=="natural-return";
  if(variant=="unprotected-task-new-ticket"){startProtectionClock+=candidate.verificationRemainingMs;publish();fullPipeVerificationStep();check(g_virtualStartProtection.view(startProtectionClock).phase=="success","steady PS request is captured after the real original cycle succeeds");}
  inputHostRequestPsWakeRearm(generation,"offline-protected-boundary");
  if(natural){
   startProtectionClock+=100;fixturePsReadConfig({});fullPipePsWakeStep();
   check(g_inputHostPsEndpointFault.requestId==candidate.requestId&&g_psWakeRearmRequested.load()&&fullPipeRecreateCommands()==recreates,"complete current-owned PS absence is retained during the original inspection");
   startProtectionClock+=400;publish();fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"});fullPipePsWakeStep();
   check(!g_inputHostPsEndpointFault.requestId&&!g_psWakeRearmRequested.load()&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0,"natural current owned endpoint return clears the known fault without fallback or RECREATE");
   startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;publish();fullPipeVerificationStep();
   check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&finals()==std::make_pair(1u,0u),"natural owned endpoint return releases the original request with one final success");
  }else{
   inputHostConsumePsWakeRearm();std::string oldToken;
   {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);for(const auto& token:g_inputHostLifecycleWorkQueue)if(token.rfind("ps-wake-rearm:",0)==0)oldToken=token;}
   check(!oldToken.empty(),"actual PS consumer queues the original protected worker token before expiry");
   if(variant!="unprotected-task-new-ticket"){startProtectionClock+=candidate.remainingMs;fullPipeVerificationStep();
   check(g_virtualStartProtection.view(startProtectionClock).phase=="expired"&&finals()==std::make_pair(0u,1u),"original PS request expires once at its own thirty-second cap");}
   if(variant=="new-same-persona-ticket"||variant=="unprotected-task-new-ticket"){
    check(writeTarget(on,off).value("ok",false),"expired PS request admits actual OFF for the new-ticket boundary");inputCaptureRefreshRealStickSettings(true);
    const auto closing=g_virtualStartProtection.view(startProtectionClock);startProtectionClock+=closing.remainingMs;fullPipeVerificationStep();
    current=targetDocument("dualsense-edge",2);check(writeTarget(off,current).value("ok",false),"a new same-persona ticket is admitted after uncompleted OFF reaches its own hard cap");inputCaptureRefreshRealStickSettings(true);
    if(variant=="new-same-persona-ticket"){inputHostRequestPsWakeRearm(generation,"offline-new-same-persona-ticket");
    check(g_psWakeProtectedRequestId.load()==g_virtualStartProtection.view(startProtectionClock).requestId&&g_psWakeProtectedRequestId.load()!=candidate.requestId&&sameHost(),"new request deliberately reuses the same actual Host persona and power while replacing only request identity");}
    else check(!g_psWakeProtectedRequestId.load()&&sameHost()&&g_virtualStartProtection.view(startProtectionClock).requestId!=candidate.requestId,"old steady PS task has zero protected ticket while the same Host now belongs to a new player request");
   }
   const auto before=fullPipeProtectionState();const auto requested=g_psWakeRearmRequested.load();
   fixturePsReadConfig({});{FixturePsReadScope read;inputHostPerformPsWakeRearm(oldToken);}
   check(fixturePsOpenCalls==queries&&fullPipeRecreateCommands()==recreates&&g_inputHostRecoveryEpisodeAttempts.load()==0,"expired queued PS token cannot probe rebuild or charge a current or newer request");
   check(fullPipeProtectionState()==before&&g_psWakeRearmRequested.load()==requested&&sameHost(),"old queued PS token preserves current request clocks flags and the unchanged live Host");
  }
  // Exit only through real player OFF admission and real owned child cleanup.
  if(g_virtualStartProtection.view(startProtectionClock).locked){startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).remainingMs;fullPipeVerificationStep();}
  const auto finalOff=targetDocument("disabled",current["input"]["revision"].get<int>()+1);
  check(writeTarget(current,finalOff).value("ok",false),"boundary case exits through actual admitted player OFF");inputCaptureRefreshRealStickSettings(true);
  const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,family=="MSI"?"failure":"success");
  const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;if(family=="MSI")fullPipeMsiObserveReleasedPhysicalReturn(child,"normal-PS-OFF-released-MSI-XInput");
  check(child.value("nativeObservedProcessExit",false)&&child.value("nativeObservedExitCode",999)==0&&child.value("finalCleanupOk",false),"boundary case releases the actual owned managed child naturally");
  fullPipeDrainQueuedLifecycle();fullPipeWaitForPlayerLock(finalOff,targetDocument("dualsense-edge",finalOff["input"]["revision"].get<int>()+1));if(family=="MSI")fullPipeCheckUniqueOffFinal(closing.requestId,true);
  check(fullPipeRecreateCommands()==recreates&&fullPipeSpawnCount==oldSpawns+1&&!g_inputHostProcess&&!g_inputHostReleaseQuarantined,"boundary and all late queued work leave no replacement or ownership debt");
  cases.back()["nativeEvents"]=fixtureEvents;cases.back()["afterStop"]=state();fixtureFullPipe=false;return;
 }
 const auto recreates=fullPipeRecreateCommands();const auto sequence=g_inputHostSequence;
 startProtectionClock+=100;
 fixturePsReadConfig({});inputHostRequestPsWakeRearm(currentPowerGeneration(),"offline-protected-owned-endpoint-absent");
 const auto armTick=g_psWakeRearmArmTick.load();
 const auto inspectAbsent=[&](const char* phase){const auto opens=fixturePsOpenCalls,destroys=fixturePsDestroyCalls;const auto eventBegin=fixtureEvents.size();fixturePsEndSeen=false;
  fullPipePsWakeStep();json observation=nullptr;
  for(size_t i=eventBegin;i<fixtureEvents.size();++i)if(fixtureEvents[i].value("event","")=="ps-wake-rearm-attempt")observation=fixtureEvents[i]["detail"];
  check(fixturePsOpenCalls==opens+1&&fixturePsDestroyCalls==destroys+1&&fixturePsEndSeen,"protected PS worker actually completes and closes one read-only endpoint enumeration");
  check(observation.is_object()&&observation.value("probeKind","")=="absent"&&observation.value("probeComplete",false)&&observation.value("ownershipComplete",false)&&observation.value("probeReadError",1UL)==ERROR_SUCCESS&&observation.value("foreignVirtual",1)==0&&observation.value("queryRequestId",0ULL)==candidate.requestId&&observation.value("queryHostEpoch",0ULL)==epoch,"protected PS worker observes complete current-owned absence rather than unknown foreign or unbound power evidence");
  cases.back()["psAbsenceObservations"].push_back({{"phase",phase},{"probe",observation}});record(phase);};
 const auto unchangedCycle=[&](std::uint64_t elapsed){const auto v=g_virtualStartProtection.view(startProtectionClock);
  return v.requestId==candidate.requestId&&v.persona==candidate.persona&&v.phase=="verifying"&&v.candidateOutcome=="success"&&v.locked&&v.verificationPending&&v.elapsedMs==candidate.elapsedMs+elapsed&&v.remainingMs==candidate.remainingMs-elapsed&&v.inspectionStartedElapsedMs==candidate.inspectionStartedElapsedMs&&v.inspectionDeadlineElapsedMs==candidate.inspectionDeadlineElapsedMs;};
 const auto noEarlyRebuild=[&](std::uint64_t elapsed,std::uint64_t remaining){const auto v=g_virtualStartProtection.view(startProtectionClock);
  check(unchangedCycle(elapsed)&&v.verificationRemainingMs==remaining&&g_psWakeRearmArmTick.load()==armTick,"early PS observation preserves the original request admission and inspection clocks");
  check(fullPipeRecreateCommands()==recreates&&g_inputHostSequence==sequence&&g_psEndpointOwnership.revision()==accepted.targetRevision,"complete owned absence before ten seconds cannot send RECREATE or mutate the current target");
  check(v.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&g_psWakeRebuiltGeneration.load()!=generation,"early owned absence reserves neither central fallback nor a shared episode attempt");
  check(g_psWakeRearmRequested.load()&&g_psWakeRearmGeneration.load()==generation&&sameHost()&&finals()==std::make_pair(0u,0u),"early PS observation retains the same request for later consumption without Host replacement or final");};
 inspectAbsent("owned-absence-inspection-9900");noEarlyRebuild(100,9900);
 startProtectionClock+=9899;publish();inspectAbsent("owned-absence-inspection-1");noEarlyRebuild(9999,1);
 ++startProtectionClock;publish();
 check(unchangedCycle(fullPipeUserSettleMs)&&g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs==0,"ten seconds only makes the same PS inspection eligible without renewing it");
 // Consume the retained request, not a new wake/power generation or admission.
 fullPipeVerificationStep();
 check(g_virtualStartProtection.view(startProtectionClock).locked&&finals()==std::make_pair(0u,0u),"known current-owned PS absence prevents a premature successful final at the original inspection deadline");
 inspectAbsent("owned-absence-inspection-complete");
 const auto rebuilt=g_virtualStartProtection.view(startProtectionClock);const auto newOwnership=g_psEndpointOwnership.snapshot();
 check(fullPipeRecreateCommands()==recreates+1&&newOwnership.valid&&newOwnership.generation>originalOwnership.generation&&g_inputHostPrepared&&g_inputHostNeutralized,"after ten seconds the actual PS worker performs exactly one accepted managed RECREATE and neutral");
 check(rebuilt.fallbackAttempts==1&&g_inputHostRecoveryEpisodeAttempts.load()==1,"actual PS RECREATE reserves exactly one central fallback and one shared episode attempt");
 check(unchangedCycle(fullPipeUserSettleMs)&&g_psWakeRearmArmTick.load()==armTick&&g_psWakeRebuiltGeneration.load()==generation&&!g_psWakeRearmRequested.load()&&sameHost(),"PS fallback retains original deadlines request Host PID and power binding");
 publish();fullPipeVerificationStep();const auto withoutAck=g_virtualStartProtection.view(startProtectionClock);
 check(withoutAck.requestId==candidate.requestId&&withoutAck.locked&&withoutAck.phase=="verifying"&&withoutAck.verificationRemainingMs==0&&withoutAck.remainingMs==candidate.remainingMs-fullPipeUserSettleMs&&finals()==std::make_pair(0u,0u),"old accepted target report cannot finalize recreated PS target before an actual current managed report");record("recreated-awaiting-current-managed-report");
 auto current=publish();current.pad.wButtons=XINPUT_GAMEPAD_A;const auto currentAck=fullPipeSubmitSource(current,true,false);
 const auto repairedSource=ymcc::inputHost::switchVerificationRuntime::acceptedSource;
 check(currentAck.value("hostSubmission","")=="frame-accepted"&&!currentAck.value("focusIsolationSemanticNeutral",true)&&repairedSource.requestId==candidate.requestId&&repairedSource.targetRevision==g_psEndpointOwnership.revision()&&repairedSource.source.sequence==current.readAttemptSequence,"recreated PS target accepts this current source report through the real native and managed consumers");
 fullPipeVerificationStep();const auto verified=g_virtualStartProtection.view(startProtectionClock);
 check(verified.requestId==candidate.requestId&&verified.phase=="success"&&!verified.locked&&verified.elapsedMs==candidate.elapsedMs+fullPipeUserSettleMs&&verified.inspectionStartedElapsedMs==candidate.inspectionStartedElapsedMs&&verified.inspectionDeadlineElapsedMs==candidate.inspectionDeadlineElapsedMs&&verified.fallbackAttempts==1&&g_inputHostRecoveryEpisodeAttempts.load()==1&&sameHost(),"only post-recreate managed acceptance releases the original PS request through central final success without another inspection");
 check(finals()==std::make_pair(1u,0u),"protected PS cycle emits exactly one final success and no final failure");cases.back()["repairedCurrentAck"]=currentAck;record("unique-final-success");
 for(int n=0;n<2;++n){inputHostRequestPsWakeRearm(currentPowerGeneration(),"offline-protected-same-wake-repeat");fullPipePsWakeStep();fullPipeVerificationStep();}
 const auto repeated=g_virtualStartProtection.view(startProtectionClock);
 check(fullPipeRecreateCommands()==recreates+1&&repeated.requestId==verified.requestId&&repeated.phase=="success"&&!repeated.locked&&repeated.elapsedMs==verified.elapsedMs&&repeated.fallbackAttempts==1&&g_inputHostRecoveryEpisodeAttempts.load()==1&&sameHost()&&finals()==std::make_pair(1u,0u),"duplicate same-generation PS requests cannot rebuild charge renew or publish another final");record("duplicate-requests-no-rebuild-or-charge");
 // Preserve accepted ownership for the existing final PS reader group.
 fixturePsLastAuthority=g_psEndpointOwnership.snapshot();fixturePsLastSdkSerial=fullPipeRecreateCommands()+1;
 g_psWakeRearmRetryAfterTick=0;inputHostConsumePsWakeRearm();
 check(writeTarget(on,off).value("ok",false),"protected PS final success admits actual user OFF");inputCaptureRefreshRealStickSettings(true);
 const auto closing=g_virtualStartProtection.view(startProtectionClock);inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(closing.requestId,family=="MSI"?"failure":"success");
 const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;if(family=="MSI")fullPipeMsiObserveReleasedPhysicalReturn(child,"normal-PS-OFF-released-MSI-XInput");
 check(child.value("nativeObservedProcessExit",false)&&child.value("nativeObservedExitCode",999)==0&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"protected PS actual OFF releases managed resources and the owned child exits naturally");
 const auto queries=fixturePsOpenCalls;{FixturePsReadScope read;fullPipeDrainQueuedLifecycle();}
 check(fixturePsOpenCalls==queries&&fullPipeRecreateCommands()==recreates+1&&fullPipeSpawnCount==oldSpawns+1,"late PS work after actual OFF cannot probe rebuild or resurrect the child");
 const auto next=targetDocument("dualsense-edge",2);fullPipeWaitForPlayerLock(off,next);if(family=="MSI")fullPipeCheckUniqueOffFinal(closing.requestId,true);
 check(!g_realStickTest.virtualTargetEnabled&&!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostNeutralized&&!g_inputHostReleaseQuarantined&&!g_inputHostStartBlocked&&!g_hidHideVisibilityJournal.active&&!g_msiSteamDeckReleaseDebt.required()&&fullPipeChildObservers.empty()&&!g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load()&&g_inputHostLifecycleWorkQueue.empty(),"protected PS final observer is clean after actual OFF verification and natural child exit");
 check(finals()==std::make_pair(1u,0u),"protected PS cleanup cannot add an opposite or duplicate final for the ON request");
 cases.back()["afterStop"]=state();cases.back()["nativeEvents"]=fixtureEvents;fixtureFullPipe=false;
}
static void fullPipeCases(){
 if(!fspath::is_regular_file(fixtureManagedDll))throw std::runtime_error("managed-fixture-not-file");
 const struct{const char* fault;int family;const char* persona="steamdeck";} scenarios[]={{"none",0},{"controller-dispose-once",0},{"context-dispose-once",0},{"none",1},{"none",2},{"recreate-owned-ids-rotate",0},{"recreate-owned-ids-foreign",0},{"recreate-owned-ids-unbound-ack",0},{"none",1,"dualsense-edge"},{"off-during-ps-probe",1,"dualsense-edge"},{"exit-during-ps-probe",1,"dualsense-edge"},{"power-during-ps-probe",1,"dualsense-edge"},{"generation-during-ps-probe",1,"dualsense-edge"},{"persona-during-ps-probe",1,"dualsense-edge"},{"reentry-during-ps-probe",1,"dualsense-edge"},{"refresh-during-ps-probe",1,"dualsense-edge"},{"target-during-ps-probe",1,"dualsense-edge"},{"ps-late-ownership-read",1,"dualsense-edge"}};
 for(const auto scenario:scenarios){const std::string fault=scenario.fault;const int family=scenario.family;const std::string persona=scenario.persona;const bool edge=persona=="dualsense-edge";const bool cancelProbe=fault.find("-during-ps-probe")!=std::string::npos;const bool rotate=fault=="recreate-owned-ids-rotate",foreign=fault=="recreate-owned-ids-foreign",unbound=fault=="recreate-owned-ids-unbound-ack";
  seed();fixtureFullPipe=true;fixturePsSameInstanceNames=fault=="target-during-ps-probe";fullPipeFault=fixturePsSameInstanceNames?"ps-same-instance-ids":cancelProbe?"none":fault;g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
  if(family){g_machineIdentity.family=family==1?YmccFamily::AsusRogAllyX:YmccFamily::Gpd;g_machineIdentity.manufacturer=family==1?"ASUSTEK COMPUTER INC.":"GPD";g_machineIdentity.product=family==1?"ROG Ally X":"G1617";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;}
  g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_xinputAdmittedMask=family?1:0;
  cases.push_back({{"name",std::string(family==1?"ROG/":family==2?"GPD/":"")+"full-coordinator-real-managed-pipe/"+fault+(edge?"/dualsense-edge":"")},{"requestedPersona",persona},{"controlledFamily",family==1?"ROG":family==2?"GPD":"MSI"}});const auto oldSpawns=fullPipeSpawnCount;
  // Independent-case initialization, never used as recovery completion evidence.
  g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
  const auto on=targetDocument(persona,0);auto off=targetDocument("disabled",1);check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"private ON document exists for real settings consumer");inputCaptureRefreshRealStickSettings(true);
  const bool started=inputHostStart();cases.back()["afterStart"]=state();check(started,"actual inputHostStart creates owned process and completes real HELLO PREPARE NEUTRAL");
  check(fullPipeSpawnCount==oldSpawns+1&&g_inputHostProcess&&GetProcessId(g_inputHostProcess)!=GetCurrentProcessId(),"real process factory is consumed exactly once by actual startup");
  if(started){ULONG client=0;check(GetNamedPipeClientProcessId(g_inputHostServerPipe,&client)&&client==GetProcessId(g_inputHostProcess),"production server is connected to this owned managed child");check(!g_inputHostHostInstanceId.empty()&&g_inputHostPrepared&&g_inputHostNeutralized,"real managed receipt establishes bound Host and admitted target");}
  if(started){if(fault=="none"&&!edge)fullPipeDuplicateStartBurst("pending-before-first-frame");fullPipeCurrentFrameCases(family);if(fault=="none"&&!edge){fullPipeDuplicateStartBurst("current-frame-success-candidate");fullPipePowerCycles(family);}if(edge){if(fault=="ps-late-ownership-read")fullPipeLateOwnershipCaptureCase();if(cancelProbe)fullPipePsCancelDuringProbe(fault.substr(0,fault.find("-during-ps-probe")));else fullPipePsWakeConsumerCases();}if(rotate||foreign||unbound)fullPipeRecreateOwnedCase(foreign,unbound);}
  if(edge){const auto pin=g_psEndpointOwnership.snapshot();if(pin.valid){fixturePsLastAuthority=pin;fixturePsLastSdkSerial=fixturePsSameInstanceNames?1:fullPipeRecreateCommands()+1;}}
  const auto beforeStop=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));off=targetDocument("disabled",beforeStop["input"]["revision"].get<int>()+1);
  const bool alreadyOff=inputHostTargetKey(beforeStop["input"])=="disabled";const auto beforeOffProtection=fullPipeProtectionState();
  if(!alreadyOff)fullPipeWaitForPlayerLock(beforeStop,off);
  check(writeTarget(beforeStop,off).value("ok",false),"real settings IPC accepts OFF after the prior lock or as an unchanged target");inputCaptureRefreshRealStickSettings(true);check(!g_realStickTest.virtualTargetEnabled,"actual effective settings consumer observes user OFF before Stop");
  const auto closing=g_virtualStartProtection.view(startProtectionClock);
  if(!alreadyOff)check(closing.persona=="disabled"&&closing.phase=="pending"&&closing.locked,"accepted OFF remains pending until actual stop reaches a terminal result");
  else check(fullPipeProtectionState()==beforeOffProtection&&closing.persona=="disabled"&&closing.phase=="verifying"&&closing.candidateOutcome=="success"&&closing.verificationPending&&closing.locked&&!g_inputHostProcess,"unchanged OFF preserves the actual queued-stop candidate without renewing either deadline");
  const auto blockedOn=targetDocument(persona,off["input"]["revision"].get<int>()+1);fullPipeRejectLockedPlayerWrite(off,blockedOn);
  const auto blockedPersona=targetDocument(persona=="elite"?"steamdeck":"elite",off["input"]["revision"].get<int>()+1);fullPipeRejectLockedPlayerWrite(off,blockedPersona);
  // A delayed real candidate must retain the original 30s cap. The 10s
  // verification starts at that candidate, never at the OFF click.
  startProtectionClock+=(family==2||fault=="context-dispose-once")?27000:1000;
  if(!alreadyOff)check(g_virtualStartProtection.view(startProtectionClock).phase=="pending","waiting for actual stop cannot start the closing settle interval");
  else check(g_virtualStartProtection.view(startProtectionClock).requestId==closing.requestId&&g_virtualStartProtection.view(startProtectionClock).phase==closing.phase&&g_virtualStartProtection.view(startProtectionClock).remainingMs==closing.remainingMs-1000&&g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs==closing.verificationRemainingMs-1000,"queued-stop verification and original admission budgets continue while duplicate cleanup waits");
  const auto beforeStopProtection=fullPipeProtectionState();inputHostStop("virtual-target-disabled");
  // MODE1 is only a write receipt: MSI still owes physical return at Stop. The
  // explicit normal observation below can promote only the final verification.
  if(!alreadyOff)fullPipeCheckStopCandidate(closing.requestId,(fault=="controller-dispose-once"||fault=="context-dispose-once"||foreign||unbound||family==0)?"failure":"success");
  else check(fullPipeProtectionState()==beforeStopProtection,"duplicate actual stop cannot renew an OFF candidate or its ten-second verification");
  cases.back()["afterStop"]=state();const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;
  // No physical-return observation is supplied to dispose/identity failures.
  if(family==0&&(fault=="none"||rotate))fullPipeMsiObserveReleasedPhysicalReturn(child,"normal-MSI-pipe-OFF-released-physical-XInput");
  cases.back()["nativeEvents"]=fixtureEvents;
  check(!g_inputHostProcess,"actual stop only retires the process handle after child exit confirmation");check(child.value("creates",0)==((rotate||foreign||unbound||(edge&&!cancelProbe)||fault=="refresh-during-ps-probe"||fault=="target-during-ps-probe")?2:1),"actual coordinated target count matches single prepare or explicit in-process replacement");
  if(fault=="none"||fault=="ps-late-ownership-read"||rotate||cancelProbe){
   if(fault=="none"&&!edge)fullPipeFinishOffRearm(fullPipeSpawnCount);else {const auto queries=fixturePsOpenCalls;{FixturePsReadScope read;fullPipeDrainQueuedLifecycle();}if(edge)check(fixturePsOpenCalls==queries&&!g_inputHostProcess&&!g_inputHostPrepared,"queued PS recovery after real OFF and Stop cannot probe or recreate target");}
   check(child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"normal actual Stop closes managed target and context through accepted command chain");
   check(!g_inputHostPrepared&&!g_inputHostNeutralized&&!g_inputHostReleaseQuarantined,"normal complete real pipe lifecycle leaves no local pending target");
  }else if(foreign||unbound){
   check(child.value("faultConsumed",false)&&child.value("finalCleanupOk",false),"invalid source-identity control still executes actual SDK disposal and natural child cleanup");check(g_inputHostReleaseQuarantined&&g_inputHostStartBlocked&&!g_msiSteamDeckReleaseDebt.pinned(),"unproven replacement identity remains quarantined after child exit without trusting old pins");const auto count=fullPipeSpawnCount;check(!inputHostStart()&&fullPipeSpawnCount==count,"unproven replacement cannot spawn a second Host after Stop");
  }else{
   check(child.value("faultConsumed",false)&&child.value("finalCleanupOk",true)==false&&child.value("hasContext",false),"SDK disposal exception remains unresolved through pipe EOF and final managed cleanup");
   if(fault=="controller-dispose-once")check(child.value("hasController",false),"failed SDK controller remains owned until its process exits");
   check(g_inputHostReleaseQuarantined&&g_inputHostStartBlocked,"real failed release retains native quarantine despite child process exit");
   const auto count=fullPipeSpawnCount;
   const auto heldOn=targetDocument("steamdeck",2),heldOff=targetDocument("disabled",3);fullPipeWaitForPlayerLock(off,heldOn);
   check(writeTarget(off,heldOn).value("ok",false),"expired cooldown admits a fresh explicit ON while old release is unproven");inputCaptureRefreshRealStickSettings(true);
   check(g_realStickTest.virtualTargetEnabled&&g_inputHostReleaseQuarantined,"fresh effective ON does not erase old release quarantine");
   check(!inputHostStart()&&fullPipeSpawnCount==count,"expired user cooldown and fresh ON cannot bypass unproven native release after owned child exits");
   fullPipeWaitForPlayerLock(heldOn,heldOff);check(writeTarget(heldOn,heldOff).value("ok",false),"explicit OFF waits for the fresh intent lock while old release remains quarantined");inputCaptureRefreshRealStickSettings(true);check(!g_realStickTest.virtualTargetEnabled,"real consumer preserves OFF throughout subsequent quarantine probes");
   const auto failedClosing=g_virtualStartProtection.view(startProtectionClock);check(failedClosing.phase=="pending"&&failedClosing.persona=="disabled","quarantined OFF remains pending before actual failed stop");
   inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(failedClosing.requestId,"failure");
   const auto targets=g_msiSteamDeckReleaseDebt.ownedTargets();check(g_msiSteamDeckReleaseDebt.required()&&g_msiSteamDeckReleaseDebt.pinned()&&!targets.empty(),"actual failed release retains this Host's exact prepared SD endpoint identities");
   const auto restores=fullPipeRestoreCount();json probes=json::array();
   fullPipeQuarantineProbe();probes.push_back({{"phase","unknown-PnP"},{"state",state()}});check(g_inputHostReleaseQuarantined&&fullPipeRestoreCount()==restores,"unknown PnP cannot restore physical visibility or clear quarantine");
   g_msiSdOwnedPnpSnapshotForTest=[](){return std::pair{false,std::set<std::string>{}};};fullPipeQuarantineProbe();probes.push_back({{"phase","incomplete-PnP"},{"state",state()}});check(g_inputHostReleaseQuarantined&&fullPipeRestoreCount()==restores,"incomplete empty PnP cannot prove owned endpoint absence");
   g_msiSdOwnedPnpSnapshotForTest=[targets](){return std::pair{true,targets};};fullPipeQuarantineProbe();probes.push_back({{"phase","owned-endpoint-present"},{"state",state()}});check(g_inputHostReleaseQuarantined&&fullPipeRestoreCount()==restores,"complete PnP still containing pinned endpoint prevents physical restore");
   g_msiSdOwnedPnpSnapshotForTest=[](){return std::pair{true,std::set<std::string>{}};};g_wakeHoldPhysicalUnhide=true;fullPipeQuarantineProbe();probes.push_back({{"phase","absent-wake-hold"},{"state",state()}});check(g_inputHostReleaseQuarantined&&fullPipeRestoreCount()==restores,"proven absence cannot bypass current wake hold");
   g_wakeHoldPhysicalUnhide=false;g_startEntrySeam.hidHideRestoreResult=false;fullPipeQuarantineProbe();probes.push_back({{"phase","physical-restore-failed"},{"state",state()}});check(g_inputHostReleaseQuarantined&&g_inputHostPrepared&&fullPipeRestoreCount()==restores+1,"failed physical restore retains quarantined session for later retry");
   g_startEntrySeam.hidHideRestoreResult=true;fullPipeQuarantineProbe();probes.push_back({{"phase","verified-restore"},{"state",state()}});check(!g_inputHostReleaseQuarantined&&!g_inputHostPrepared&&!g_inputHostNeutralized&&g_inputHostRunId.empty()&&!g_msiSteamDeckReleaseDebt.required(),"actual lifecycle owner clears dead tuple and owned removal debt only after verified restore");check(fullPipeSpawnCount==count&&!g_realStickTest.virtualTargetEnabled,"quarantine reconciliation while OFF creates no replacement Host");cases.back()["recoveryProbes"]=probes;
   const auto newer=targetDocument("steamdeck",4);fullPipeWaitForPlayerLock(heldOff,newer);check(writeTarget(heldOff,newer).value("ok",false),"fresh explicit ON is admitted only after actual verification final or the original hard cap");inputCaptureRefreshRealStickSettings(true);check(g_realStickTest.virtualTargetEnabled,"real consumer restores newly requested effective target");fullPipeFault="none";
   check(inputHostStart()&&fullPipeSpawnCount==count+1,"fresh explicit ON after verified return creates exactly one new owned managed Host");cases.back()["afterExplicitRestart"]=state();
   const auto finalOff=targetDocument("disabled",5);fullPipeWaitForPlayerLock(newer,finalOff);check(writeTarget(newer,finalOff).value("ok",false),"second real OFF waits for the replacement startup lock");inputCaptureRefreshRealStickSettings(true);const auto finalClosing=g_virtualStartProtection.view(startProtectionClock);check(finalClosing.phase=="pending"&&finalClosing.persona=="disabled","replacement OFF waits for actual stop result");inputHostStop("virtual-target-disabled");fullPipeCheckStopCandidate(finalClosing.requestId,family==0?"failure":"success");const auto restarted=fullPipeChildOutcome();cases.back()["restartManagedReceipt"]=restarted;if(family==0)fullPipeMsiObserveReleasedPhysicalReturn(restarted,"explicit-new-ON-clean-OFF-released-MSI-XInput");check(restarted.value("nativeObservedExitCode",999)==0&&restarted.value("finalCleanupOk",false)&&!g_inputHostReleaseQuarantined&&!g_inputHostPrepared,"replacement Host follows full clean stop through actual pipe and child exit");
   // settings refresh also queued this OFF duty. Its real owner must consume
   // it before verification can finish; the physical debt was qualified above.
   fullPipeDrainQueuedLifecycle();
  }
  const auto stoppedSettings=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));const auto afterStopOn=targetDocument(persona,stoppedSettings["input"]["revision"].get<int>()+1);
  fullPipeWaitForPlayerLock(stoppedSettings,afterStopOn);
  if(family==0&&!foreign&&!unbound)fullPipeCheckUniqueOffFinal(g_virtualStartProtection.view(startProtectionClock).requestId,true);
  check(!g_inputHostProcess,"actual verification final or hard expiry only reopens admission and never creates a replacement process");
  cases.back()["nativeEvents"]=fixtureEvents;g_msiSdOwnedPnpSnapshotForTest={};fixtureFullPipe=false;
 }
 fullPipeVerifiedSourceLineageCase();
 fullPipePsProtectedCycleCase();
 for(const std::string variant:{"natural-return","expired-queued-task","new-same-persona-ticket","unprotected-task-new-ticket","receipt-owned-replay","receipt-untrusted-asset","receipt-untrusted-identity"})fullPipePsProtectedCycleCase(variant);
 for(const std::string family:{"MSI","ROG","GPD"})for(const std::string variant:{"wake-old-generation","wake-power-not-ready","wake-new-player","wake-duplicate-arm","wake-power-during-hide","wake-inspection-pending","wake-player-expired"})fullPipePsProtectedCycleCase(variant,family);
 fullPipePsProtectedCycleCase("wake-pc-recreate","PC");
 for(const std::string variant:{"wake-pc-retry-transient","wake-pc-retry-exhausted","wake-pc-retry-deadline","wake-pc-retry-source-timeout"})fullPipePsProtectedCycleCase(variant,"PC");
 for(const std::string variant:{"pipe-budget-write-remaining","pipe-budget-write-expiry","pipe-budget-connect-remaining","pipe-budget-connect-expiry"})fullPipePsProtectedCycleCase(variant,"PC");
 for(const std::string variant:{"release-budget-long","release-budget-operation","release-budget-near-deadline"})fullPipePsProtectedCycleCase(variant,"PC");
 for(const std::string family:{"MSI","ROG","GPD"})fullPipePsProtectedCycleCase("pipe-budget-write-remaining",family);
 for(const std::string family:{"MSI","ROG","GPD","PC"})fullPipeInitialBudgetCase(family,false);
 fullPipeInitialBudgetCase("PC",true);
 fullPipeInitialBudgetCase("PC",false,false,true);
 fullPipeInitialBudgetCase("PC",false,true);
 fullPipeQueuedInitialAuthorityCase("initial-target-admission",true);
 fullPipeQueuedInitialAuthorityCase("persona-transition",true);
 fullPipeQueuedInitialAuthorityCase("initial-target-admission",false);
 for(const std::string family:{"MSI","ROG","GPD","PC"})fullPipePsProtectedCycleCase("wake-dead-host",family);
 fullPipePsProtectedCycleCase("wake-dead-host-not-ready","PC");
 for(const std::string variant:{"wake-dead-host-new-player","wake-dead-host-new-power","wake-dead-host-expired","wake-dead-host-start-budget"})fullPipePsProtectedCycleCase(variant,"PC");
 psEndpointProbeCases();
 fullPipePrepareAckLostCase("PC");
 fullPipePrepareAckLostCase("ROG");
 fullPipePrepareAckLostCase("ROG",true);
 fullPipePrepareAckLostCase("ROG",false,false,true);
 fullPipePsReturnEvidenceCases();
 fullPipeOffLifecycleCases();
}
