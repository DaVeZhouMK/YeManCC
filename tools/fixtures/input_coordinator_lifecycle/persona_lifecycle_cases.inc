// Task140: the existing full production TU and real private managed pipe only.
// SDK/OS leaves are the original fixture ends; no lifecycle completion is forged.
namespace personaLifecycle140 {
static void require(bool value,const char* assertion){check(value,assertion);if(!value)throw std::runtime_error(std::string("persona-lifecycle assertion: ")+assertion);}
static unsigned finalCount(std::uint64_t ticket,const char* outcome){unsigned n=0;for(const auto& e:fixtureEvents)if(e.value("event","")=="input-host-switch-verification"&&e["detail"].value("stage","")=="final"&&e["detail"].value("requestId",0ULL)==ticket&&e["detail"].value("outcome","")==outcome)++n;return n;}
static void advance(std::uint64_t ms){startProtectionClock+=ms;fixturePowerClockOffset+=ms;}
static InputCaptureGamepadSnapshot fresh(int family){
 auto sample=fullPipeReadNow(family);std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);
 const auto previous=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);
 sample.readContinuityGeneration=ymcc::input::recoveryHealth::nextContinuity(previous,inputHostRecoveryHealthSource(&sample,true),GetTickCount64());
 g_inputCaptureGamepadSnapshot=sample;return sample;
}
static void observeSource(int family,const std::string& persona){
 if(family){
  // Never set a selected-source mask/lock in this fixture. Both discovery scans
  // and classification/admission execute in the existing real consumer.
  cases.back()["sourceDiscoveries"].push_back({{"persona",persona},{"discovery",fullPipeReadPhysicalSourceForElite()}});
 } // MSI source eligibility is observed after actual Start arms its canonical DInput route.
}
// Fixture display labels: new families must not be reported as GPD.
static const char* familyName(int family){switch(family){case 0:return "MSI";case 1:return "ROG";case 2:return "GPD";case 3:return "OneXPlayer";case 4:return "Ayanero";default:throw std::logic_error("unknown persona fixture family");}}
struct ControlledDmi {const char* manufacturer;const char* product;const char* declaredDeviceClass;const char* supportPointer;};
static void prepare(int family,const std::string& name,const ControlledDmi* dmi=nullptr){
 if(family>=3)require(family<=4&&dmi,"new family variants require declared controlled DMI leaves");
 require(fullPipeChildObservers.empty()&&!g_inputHostProcess,"new independent lifecycle case starts with no residual owned child");
 seed();fixtureFullPipe=true;fullPipeFault="none";fixtureXinputSourceLive=true;fixturePsSameInstanceNames=false;fixturePsReturnManualTopology=false;
 g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_shortcutRecordingActive=false;g_inputCaptureStop=false;
 g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;
 if(family){g_machineIdentity.family=family==1?YmccFamily::AsusRogAllyX:family==3?YmccFamily::OneXPlayer:family==4?YmccFamily::Ayanero:YmccFamily::Gpd;g_machineIdentity.manufacturer=family==1?"ASUSTEK COMPUTER INC.":"GPD";g_machineIdentity.product=family==1?"ROG Ally X":"G1617";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;}
 // Default 0/1/2 inputs remain MSI/ROG/G1617. Only explicit variants
 // replace DMI at this fixture leaf; no catalog/deviceClass resolution is forged.
 if(dmi){g_machineIdentity.manufacturer=dmi->manufacturer;g_machineIdentity.product=dmi->product;}
 reg_fs();cases.push_back({{"name",name},{"controlledFamily",familyName(family)},{"realManagedPipe",true},{"sourceDiscoveries",json::array()},{"periods",json::array()},{"managedReceipts",json::array()}});
 if(dmi)cases.back()["controlledDmi"]={{"boundary","fixture-leaf; not catalog resolution or hardware acceptance"},{"manufacturer",dmi->manufacturer},{"product",dmi->product},{"declaredDeviceClass",dmi->declaredDeviceClass},{"runtimeDeviceClass",g_machineIdentity.deviceClass},{"source","capabilities/CONTROLLER-REPAIR-SUPPORT.json"},{"jsonPointer",dmi->supportPointer}};
}
static json receipt(HANDLE child,const std::wstring& file,const std::string& persona,unsigned expectedCreates=1){
 const auto result=fullPipeChildOutcome(child,file);
 require(result.value("nativeObservedProcessExit",false)&&result.value("nativeObservedExitCode",999)==0&&result.value("finalCleanupOk",false)&&!result.value("hasController",true)&&!result.value("hasContext",true),"exact old managed child controller and context release naturally before the next create");
 require(result.value("creates",0)==expectedCreates&&result.value("actualProductionHostSessionConsumer",false)&&result.value("actualProductionPipeLoop",false),"actual HostSession creates exactly the requested target count through the real private production pipe");
 const auto& final=result["stateAfterFinalCleanup"];require(final["tuple"].value("Persona",std::string{})==persona,"managed final tuple retains this lifecycle's actual requested persona");
 bool prepared=false;for(const auto& row:result["observations"])if(row.value("kind",std::string{})=="command-decoded"&&row.value("command",std::string{})=="PREPARE_TARGET")prepared=true;
 // The raw receipt is retained by the existing factory in the evidence root.
 cases.back()["managedReceipts"].push_back({{"path",W2U(file)},{"processId",result.value("processId",0u)},{"persona",persona},{"creates",result.value("creates",0)},{"controllerReleased",!result.value("hasController",true)},{"contextReleased",!result.value("hasContext",true)},{"naturalExit",result.value("nativeObservedProcessExit",false)},{"tuple",final["tuple"]},{"productionHostSession",result.value("actualProductionHostSessionConsumer",false)},{"prepareObservation",prepared}});
 return result;
}
static void success(int family,const std::string& persona,std::uint64_t ticket){
 if(family==0)require(msiDinputMainSourceEligible(),"MSI actual Start arms the original confirmed canonical DInput source rather than forged XInput masks");
 require(g_inputHostPrepared&&g_inputHostNeutralized&&inputHostBoundPersonaValue()==persona,"actual Start binds the requested persona and accepted prepared neutral target");
 if(persona=="elite"){
  bool ready=false;for(const auto& e:fixtureEvents)if(e.value("event","")=="input-host-xbox-startup"&&e["detail"].value("phase","")=="xinput-ready")ready=true;
  require(ready,"Elite creation executes original Xbox state/path/index/identity/readback confirmation consumer");
 }
 json last;unsigned reads=0;for(;reads<32;++reads){advance(8);auto sample=fresh(family);last=fullPipeSubmitSource(sample,true,false);const auto view=g_virtualStartProtection.view(startProtectionClock);if(last.value("hostSubmission",std::string{})=="frame-accepted"&&!last.value("focusIsolationSemanticNeutral",true)&&view.phase=="verifying"&&view.candidateOutcome=="success")break;}
 cases.back()["lastSourceReceipt"]=last;require(reads<32&&g_inputHostActive,"valid current source reaches actual managed frame acceptance through existing sample/admission gates");
 auto candidate=g_virtualStartProtection.view(startProtectionClock);require(candidate.requestId==ticket&&candidate.verificationPending&&candidate.candidateOutcome=="success"&&candidate.verificationRemainingMs==10000&&candidate.locked&&finalCount(ticket,"success")==0,"current source creates one candidate with original ten-second inspection and no early final");
 advance(9999);auto before=fresh(family);fullPipeSubmitSource(before,true,false);fullPipeVerificationStep();require(g_virtualStartProtection.view(startProtectionClock).locked&&finalCount(ticket,"success")==0,"persona success cannot finish at candidate plus9999 milliseconds");
 advance(1);auto current=fresh(family);fullPipeSubmitSource(current,true,false);fullPipeVerificationStep();fullPipeVerificationStep();const auto done=g_virtualStartProtection.view(startProtectionClock);
 require(done.requestId==ticket&&done.phase=="success"&&!done.locked&&done.fallbackAttempts==0&&finalCount(ticket,"success")==1&&finalCount(ticket,"failure")==0,"actual current-source inspection emits exactly one success final for the original persona request");
 cases.back()["periods"].push_back({{"requestId",ticket},{"persona",persona},{"outcome","success"},{"finalCount",finalCount(ticket,"success")},{"elapsedMs",done.elapsedMs},{"remainingMs",done.remainingMs},{"freshReads",reads+1}});
}
static void startFirst(int family,const std::string& persona){
 const auto doc=targetDocument(persona,0);require(sgWriteFileAtomic(YM_SETTINGS_FILE,doc.dump()),"independent case seeds only its explicit private logical persona request");inputCaptureRefreshRealStickSettings(true);observeSource(family,persona);
 const auto spawns=fullPipeSpawnCount;require(inputHostStart()&&fullPipeSpawnCount==spawns+1,"first persona Start creates exactly one actual private managed Host");
 success(family,persona,g_virtualStartProtection.view(startProtectionClock).requestId);
}
static void close(int family,const std::string& persona,unsigned expectedCreates=1){
 const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE)),off=targetDocument("disabled",current["input"]["revision"].get<int>()+1);const auto spawns=fullPipeSpawnCount;
 const auto child=fullPipeChildObservers.back();const auto file=fullPipeReceipts.back();require(writeTarget(current,off).value("ok",false),"real settings IPC admits OFF only after persona success final");inputCaptureRefreshRealStickSettings(true);const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
 fullPipeRejectLockedPlayerWrite(off,targetDocument(persona,off["input"]["revision"].get<int>()+1));
 require(!inputHostStart()&&fullPipeSpawnCount==spawns,"unfinished OFF cannot create or reopen a Host");
 inputHostStop("virtual-target-disabled");const auto result=receipt(child,file,persona,expectedCreates);
 if(family==0&&g_msiDinputReturnOwed.load())fullPipeMsiObserveReleasedPhysicalReturn(result,"persona140-normal-OFF-released-local-interface");
 fullPipeDrainQueuedLifecycle();
 require(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"actual OFF has released the exact old Host target and visibility ownership");
 auto candidate=g_virtualStartProtection.view(startProtectionClock);require(candidate.requestId==ticket&&candidate.persona=="disabled"&&candidate.phase=="verifying"&&candidate.locked,"actual OFF reports only its closing candidate before common verification");
 if(candidate.verificationRemainingMs){advance(candidate.verificationRemainingMs-1);fullPipeVerificationStep();require(g_virtualStartProtection.view(startProtectionClock).locked&&finalCount(ticket,"success")==0,"closing result remains protected until its original inspection completes");advance(1);}
 fullPipeVerificationStep();fullPipeVerificationStep();const auto done=g_virtualStartProtection.view(startProtectionClock);
 require(done.phase=="success"&&!done.locked&&done.requestId==ticket&&finalCount(ticket,"success")==1&&finalCount(ticket,"failure")==0,"real OFF inspection publishes exactly one successful closing result");
 fullPipeDrainQueuedLifecycle();require(fullPipeSpawnCount==spawns&&fullPipeChildObservers.empty()&&!g_inputHostProcess,"queued late work cannot resurrect OFF and no fixture child remains");
 cases.back()["periods"].push_back({{"requestId",ticket},{"persona","disabled"},{"outcome",done.phase},{"finalCount",finalCount(ticket,"success")}});
}
static void first(int family,const std::string& persona,const ControlledDmi* dmi=nullptr){prepare(family,std::string(familyName(family))+"/persona-first/"+persona,dmi);const auto spawns=fullPipeSpawnCount;startFirst(family,persona);close(family,persona);require(fullPipeSpawnCount==spawns+1,"first-ON-to-OFF lifecycle has exactly one Host without fallback replacement");cases.back()["passed"]=true;fixtureFullPipe=false;}
static void transfer(const std::string& from,const std::string& to){
 prepare(1,"ROG/persona-transfer/"+from+"-to-"+to);const auto caseSpawns=fullPipeSpawnCount;startFirst(1,from);
 auto oldSource=fresh(1);oldSource.pad.wButtons=XINPUT_GAMEPAD_B;oldSource.pad.sThumbLX=29000;
 const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE)),next=targetDocument(to,current["input"]["revision"].get<int>()+1);const auto oldChild=fullPipeChildObservers.back();const auto oldReceipt=fullPipeReceipts.back();const auto oldPid=GetProcessId(oldChild);const auto oldEpoch=g_inputHostEpoch;
 require(writeTarget(current,next).value("ok",false),"real settings IPC admits a different persona only after original success");
 // The BUS can complete an old Host report after settings admission but before
 // the new runtime intent is reconciled. That receipt belongs to the OLD persona.
 const auto admitted=g_virtualStartProtection.view(startProtectionClock);
 auto gapSource=fresh(1);const auto gapFrame=fullPipeSubmitSource(gapSource,true,false);
 require(g_virtualStartProtection.view(startProtectionClock).requestId==admitted.requestId && g_virtualStartProtection.view(startProtectionClock).phase=="pending",
     "old persona ACK in the settings-to-runtime gap cannot start new persona inspection");
 cases.back()["admissionGap"]={{"requestId",admitted.requestId},{"boundBeforeReconcile",inputHostBoundPersonaValue()},{"frame",gapFrame}};
 inputCaptureRefreshRealStickSettings(true);const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
 const auto late=fullPipeSubmitSource(oldSource,true,false);require(late.value("hostSubmission",std::string{})!="frame-accepted"||late.value("focusIsolationSemanticNeutral",false),"changed intent cannot replay cached old buttons through the retiring persona");
 inputHostStop("persona-transition");const auto released=receipt(oldChild,oldReceipt,from);require(!g_inputHostProcess&&fullPipeSpawnCount==caseSpawns+1,"old managed child naturally releases before any new persona Host creation");
 observeSource(1,to);const auto spawns=fullPipeSpawnCount;require(inputHostStart()&&fullPipeSpawnCount==spawns+1&&GetProcessId(g_inputHostProcess)!=oldPid&&g_inputHostEpoch!=oldEpoch,"new persona creates exactly one new Host after old child/controller/context exit proof");
 const auto stale=fullPipeSubmitSource(oldSource,true,false);require((stale.value("hostSubmission",std::string{})!="frame-accepted"||stale.value("focusIsolationSemanticNeutral",false))&&g_virtualStartProtection.view(startProtectionClock).phase=="pending","old sample cannot become a live frame or successful candidate on the newly bound persona");
 cases.back()["transferProof"]={{"from",from},{"to",to},{"oldPid",oldPid},{"oldEpoch",oldEpoch},{"oldNaturalExit",released.value("nativeObservedProcessExit",false)},{"newPid",GetProcessId(g_inputHostProcess)},{"newEpoch",g_inputHostEpoch},{"oldFrameAfterIntent",late},{"oldFrameAfterNewHost",stale}};
 success(1,to,ticket);close(1,to);require(fullPipeSpawnCount==caseSpawns+2,"directed persona transfer owns only one Host per requested persona");cases.back()["passed"]=true;fixtureFullPipe=false;
}
static void recreateDurability(){
 prepare(0,"MSI/steamdeck/same-Host-twenty-recreates-durable-current-only");fullPipeFault="recreate-owned-ids-rotate";startFirst(0,"steamdeck");
 const auto child=g_inputHostProcess;const auto pid=GetProcessId(child);const auto run=g_inputHostRunId;const auto epoch=g_inputHostEpoch;const auto spawns=fullPipeSpawnCount;
 cases.back()["recreateDurability"]=json::array();unsigned succeeded=0;
 for(unsigned attempt=1;attempt<=20;++attempt){
  const auto before=fixtureTargetReturn::disk();const auto eventBegin=fixtureEvents.size();const auto ownedBefore=g_msiSteamDeckReleaseFence.ownedTargets();
  const bool ok=inputHostRecreatePreparedTarget();const auto loaded=fixtureTargetReturn::disk();const auto ownedAfter=g_msiSteamDeckReleaseFence.ownedTargets();json wire=nullptr;
  for(std::size_t i=eventBegin;i<fixtureEvents.size();++i){const auto& e=fixtureEvents[i];if(e.value("event","")=="input-host-command-result"&&e["detail"].value("command","")=="RECREATE_TARGET"&&e["detail"].contains("msiSteamDeckReleaseEvidence"))wire=e["detail"]["msiSteamDeckReleaseEvidence"];}
  const bool soleCurrent=loaded.state==ymcc::targetReturn::LoadState::Valid&&loaded.record&&loaded.record->obligations.size()==1&&fixtureTargetReturn::currentCount(loaded)==1&&loaded.record->obligations.front().targetMayExist;
  cases.back()["recreateDurability"].push_back({{"attempt",attempt},{"returned",ok},{"before",fixtureTargetReturn::summary(before)},{"after",fixtureTargetReturn::summary(loaded)},{"soleCurrentDurableTarget",soleCurrent},{"ownedBefore",ownedBefore},{"ownedAfter",ownedAfter},{"wireEvidence",wire}});
  check(g_inputHostProcess==child&&GetProcessId(g_inputHostProcess)==pid&&g_inputHostRunId==run&&g_inputHostEpoch==epoch&&fullPipeSpawnCount==spawns,"twenty in-Host recreates keep the original exact child and ownership tuple without replacement Host");
  check(ok&&g_inputHostPrepared&&g_inputHostNeutralized,"each of twenty actual RECREATE_TARGET calls succeeds with an accepted neutral replacement");
  if(!ok)break;
  ++succeeded;check(soleCurrent,"successful in-Host RECREATE keeps only the current live target duty instead of accumulating disposed origins");
  check(g_msiSteamDeckReleaseFence.pinned()&&wire.is_object()&&wire.contains("ownedTargets")&&wire["ownedTargets"].get<std::set<std::string>>()==ownedAfter&&ownedAfter!=ownedBefore,"each recreate pins the actual newly accepted managed endpoint evidence without fabricated identity or ACK");
  auto sample=fresh(0);const auto frame=fullPipeSubmitSource(sample,true,false);check(frame.value("hostSubmission",std::string{})=="frame-accepted"&&!frame.value("focusIsolationSemanticNeutral",true),"each accepted recreate consumes a fresh current source through the actual managed pipe");
 }
 cases.back()["successfulRecreates"]=succeeded;check(succeeded==20,"one actual managed Host completes all twenty successful RECREATE_TARGET operations beyond maxOrigins sixteen");
 close(0,"steamdeck",succeeded+1);require(fullPipeSpawnCount==spawns&&fullPipeChildObservers.empty(),"durability diagnostic cleanup naturally closes its sole managed child");cases.back()["passed"]=succeeded==20&&std::all_of(cases.back()["recreateDurability"].begin(),cases.back()["recreateDurability"].end(),[](const auto& row){return row.value("soleCurrentDurableTarget",false);});fixtureFullPipe=false;
}
}
// 2026-10-21 G3E: an admitted new ON must inherit completed/in-flight old
// quarantine return, rather than silently drain its player token behind a hold.
static void msiLateExplicitReturnCase(bool beforeReturn,bool returnBeforeQueue=false){
 using namespace personaLifecycle140;
 prepare(0,returnBeforeQueue?"MSI/explicit-ON-return-before-queue":beforeReturn?"MSI/explicit-ON-before-old-return":"MSI/explicit-ON-after-old-return");
 fullPipeFault="output-stop-once";startFirst(0,"dualsense-edge");
 const auto on=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));const auto spawns=fullPipeSpawnCount;
 const auto pin=g_psEndpointOwnership.snapshot();require(pin.valid,"late explicit return begins with real managed PS target ownership");
 fixturePsReturnManualTopology=true;fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"},ERROR_ACCESS_DENIED);
 inputHostStop("fault-stop:late-explicit-return");const auto old=fullPipeChildOutcome();
 require(old.value("nativeObservedProcessExit",false)&&!g_inputHostProcess&&g_inputHostReleaseQuarantined&&g_inputHostStartBlocked,
     "actual failed PS release retains old quarantine after exact child exit");
 const auto probeAbsent=[&](){fixturePsReadConfig({},ERROR_NO_MORE_ITEMS);FixturePsReadScope read;fullPipeQuarantineProbe();};
 if(!beforeReturn){probeAbsent();require(!g_inputHostReleaseQuarantined&&g_inputHostStartBlocked,"complete old return keeps old admission hold before new request");}
 fullPipeFault="none"; // only the OLD child faults; replacement cleanup is healthy.
 const auto next=targetDocument("steamdeck",on["input"]["revision"].get<int>()+1);
 require(writeTarget(on,next).value("ok",false),"new explicit SD intent is admitted after prior successful period");inputCaptureRefreshRealStickSettings(true);
 const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
 if(returnBeforeQueue)probeAbsent();
 if(beforeReturn&&!returnBeforeQueue){
  const auto power=currentPowerGeneration();g_powerGeneration=power+1;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);check(!inputHostSwitchExecutionPreserveMsiIntentLocked(ticket,g_inputHostEpoch,g_inputHostRunId,false),"old-power explicit setting cannot preserve a new Start across unrelated wake generation");}
  g_powerGeneration=power; // adversarial observation only; original hardware tuple was never changed.
 }
 require(inputHostQueueLifecycleWork("initial-target-admission"),"new SD uses actual immutable player queue provenance");
 {FixturePsReadScope read;fullPipeDrainQueuedLifecycle();}
 if(beforeReturn&&!returnBeforeQueue){check(!g_inputHostProcess&&fullPipeSpawnCount==spawns&&g_inputHostReleaseQuarantined,"new intent cannot create while old PS endpoint absence remains unproven");probeAbsent();}
 fullPipeFault="none";inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
 const bool restarted=g_inputHostProcess&&g_inputHostPrepared&&inputHostBoundPersonaValue()=="steamdeck"&&fullPipeSpawnCount==spawns+1;
 check(restarted,"new admitted MSI intent resumes exactly one SD Start after old return proof instead of silently waiting thirty seconds");
 if(!restarted){cases.back()["nativeEvents"]=fixtureEvents;fixturePsReturnManualTopology=false;fixtureFullPipe=false;return;}
 check(g_virtualStartProtection.view(startProtectionClock).requestId==ticket&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,
     "late explicit return retains original request budget and consumes no fallback");
 fixturePsReturnManualTopology=false;success(0,"steamdeck",ticket);close(0,"steamdeck");cases.back()["passed"]=true;fixtureFullPipe=false;
}

static void personaLifecycleCases140(){
 if(!fspath::is_regular_file(fixtureManagedDll))throw std::runtime_error("explicit managed fixture missing");
 for(const std::string persona:{"elite","steamdeck","dualsense-edge"})personaLifecycle140::first(1,persona);
 for(const std::string from:{"steamdeck","dualsense-edge","elite"})for(const std::string to:{"steamdeck","dualsense-edge","elite"})if(from!=to)personaLifecycle140::transfer(from,to);
 for(const std::string persona:{"steamdeck","dualsense-edge","elite"})personaLifecycle140::first(0,persona);
 personaLifecycle140::first(2,"elite");
 personaLifecycle140::recreateDurability();
 msiLateExplicitReturnCase(false);msiLateExplicitReturnCase(true);msiLateExplicitReturnCase(true,true);
 personaLifecycle140::require(fullPipeChildObservers.empty()&&!g_inputHostProcess,"complete persona matrix leaves no residual fixture child or active native Host handle");
}
// Explicit parameters of the original first/Start/success/close consumer.
// No transfer/recreate/wake matrix, registry ID or new parent gate.
static void familyLifecycleCases140(){
 using namespace personaLifecycle140;
 if(!fspath::is_regular_file(fixtureManagedDll))throw std::runtime_error("explicit managed fixture missing");
 // Exact support declarations used as DMI leaves; deviceClass stays unbound.
 static const ControlledDmi dmi[]={
 {"GPD","G1618-05","HandheldCompanion.Devices.GPDWin5","/physicalModels/0"},
 {"ONE-NETBOOK TECHNOLOGY CO., LTD.","ONEXPLAYER X2","HandheldCompanion.Devices.OneXPlayerX2","/physicalModels/2"},
 {"AYADEVICE","AYANEO 3","HandheldCompanion.Devices.AYANEO3","/physicalModels/5"}
 };
 const auto firstCase=cases.size();
 for(int family:{2,3,4})for(const std::string persona:{"steamdeck","dualsense-edge","elite"}){
  first(family,persona,&dmi[family-2]);
  require(ymccFamilyInterfacePolicy(g_machineIdentity.family)==YmccInterfacePolicy::None&&!g_msiDinputRouteDinputArmed.load(),"generic family uses observed standard input without acquiring MSI mode authority");
  if(family>=3)require(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Unsupported&&!g_msiNativeForegroundLease.active,"OneX and AYA standard virtual output does not grant unimplemented native foreground writes");
 }
 require(cases.size()==firstCase+9,"family selector executes exactly nine original first variants");
 require(fullPipeChildObservers.empty()&&!g_inputHostProcess,"nine family variants leave no owned child or active native Host");
 // Negative: only the controlled physical-family enum, no Deck DMI claim.
 // Both existing settings admission and actual Start execute the real policy.
 prepare(2,"SteamDeck/physical-self-output-blocked");
 g_machineIdentity.family=YmccFamily::ValveSteamDeck;g_machineIdentity.manufacturer.clear();g_machineIdentity.product.clear();
 cases.back()["controlledFamily"]="SteamDeck";cases.back()["identityBoundary"]="fixture family enum only; no physical DMI or hardware acceptance";
 const auto doc=targetDocument("steamdeck",0);require(sgWriteFileAtomic(YM_SETTINGS_FILE,doc.dump()),"physical Deck boundary seeds only its private explicit SD request");
 inputCaptureRefreshRealStickSettings(true);
 require(inputHostConfiguredPersona()=="steamdeck"&&!g_realStickTest.virtualTargetEnabled,"actual settings consumer preserves SD request and refuses physical Deck self output");
 const auto spawns=fullPipeSpawnCount;const auto receipts=fullPipeReceipts.size();const auto eventBegin=fixtureEvents.size();
 require(!inputHostStart(),"actual Start refuses a physical SteamDeck emulating SD");
 bool rejected=false;for(auto i=eventBegin;i<fixtureEvents.size();++i){const auto& e=fixtureEvents[i];if(e.value("event","")=="input-host-start-blocked"&&e["detail"].value("reason","")=="physical-steamdeck-self-output-blocked"&&e["detail"].value("requestedPersona","")=="steamdeck")rejected=true;}
 require(rejected,"actual Start reports the original physical SteamDeck self-output boundary");
 require(fullPipeSpawnCount==spawns&&fullPipeReceipts.size()==receipts&&fullPipeChildObservers.empty()&&!g_inputHostProcess&&!g_inputHostPrepared,"physical Deck negative creates no managed Host or target");
 cases.back()["passed"]=true;fixtureFullPipe=false;
}
