// Task140: the existing full production TU and real private managed pipe only.
// SDK/OS leaves are the original fixture ends; no lifecycle completion is forged.
namespace personaLifecycle140 {
static void require(bool value,const char* assertion){check(value,assertion);if(!value)throw std::runtime_error(std::string("persona-lifecycle assertion: ")+assertion);}
static unsigned finalCount(std::uint64_t ticket,const char* outcome){unsigned n=0;for(const auto& e:fixtureEvents)if(e.value("event","")=="input-host-switch-verification"&&e["detail"].value("stage","")=="final"&&e["detail"].value("requestId",0ULL)==ticket&&e["detail"].value("outcome","")==outcome)++n;return n;}
static void advance(std::uint64_t ms){startProtectionClock+=ms;fixturePowerClockOffset+=ms;}
static InputCaptureGamepadSnapshot fresh(int family){
 if(fixtureOneXEnabled){gamepadReadState(false);
  // The real canonical reader can enqueue a normal wake hide even after the
  // first Start drain. Run that original worker, never clear its queue/busy bit.
  fullPipeDrainQueuedLifecycle();
  std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);return g_inputCaptureGamepadSnapshot;}
 auto sample=fullPipeReadNow(family);std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);
 const auto previous=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);
 sample.readContinuityGeneration=ymcc::input::recoveryHealth::nextContinuity(previous,inputHostRecoveryHealthSource(&sample,true),GetTickCount64());
 g_inputCaptureGamepadSnapshot=sample;return sample;
}
static void observeSource(int family,const std::string& persona){
 if(fixtureOneXEnabled && fixtureOneXInitialPending){cases.back()["sourceDiscoveries"].push_back({{"persona",persona},{"deferredUntilHello",true},{"sourceLiveAtOsLeaf",false}});return;}

 if(family){
  // Never set a selected-source mask/lock in this fixture. Both discovery scans
  // and classification/admission execute in the existing real consumer.
  cases.back()["sourceDiscoveries"].push_back({{"persona",persona},{"discovery",fullPipeReadPhysicalSourceForElite()}});
 } // MSI source eligibility is observed after actual Start arms its canonical DInput route.
}
// Fixture display labels: new families must not be reported as GPD.
static const char* familyName(int family){switch(family){case 0:return "MSI";case 1:return "ROG";case 2:return "GPD";case 3:return "OneXPlayer";case 4:return "Ayanero";case 5:return "Lenovo";default:throw std::logic_error("unknown persona fixture family");}}
struct ControlledDmi {const char* manufacturer;const char* product;const char* declaredDeviceClass;const char* supportPointer;const char* systemProduct=nullptr;const char* declaredDeviceId=nullptr;};
static void prepare(int family,const std::string& name,const ControlledDmi* dmi=nullptr,bool registerCase=true){
 if(family>=3&&family!=5)require(family<=4&&dmi,"new family variants require declared controlled DMI leaves");
 if(family==5)require(dmi,"Lenovo family requires its declared controlled DMI leaf");
 require(fullPipeChildObservers.empty()&&!g_inputHostProcess,"new independent lifecycle case starts with no residual owned child");
 seed();fixtureFullPipe=true;fullPipeFault="none";fixtureXinputSourceLive=true;fixturePsSameInstanceNames=false;fixturePsReturnManualTopology=false;
 g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_shortcutRecordingActive=false;g_inputCaptureStop=false;
 g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;
 if(family&&family!=5){g_machineIdentity.family=family==1?YmccFamily::AsusRogAllyX:family==3?YmccFamily::OneXPlayer:family==4?YmccFamily::Ayanero:YmccFamily::Gpd;g_machineIdentity.manufacturer=family==1?"ASUSTEK COMPUTER INC.":"GPD";g_machineIdentity.product=family==1?"ROG Ally X":"G1617";g_msiDinput.enabled=false;g_msiDinputSourceValid=false;}
 // Default 0/1/2 inputs remain MSI/ROG/G1617. Only explicit variants
 // replace DMI at this fixture leaf; no catalog/deviceClass resolution is forged.
 if(dmi){g_machineIdentity.manufacturer=dmi->manufacturer;g_machineIdentity.product=dmi->product;}
 const ymcc::capabilities::Device* catalogDevice=nullptr;
 if(family==5){
  require(dmi->systemProduct&&dmi->declaredDeviceId,"Lenovo controlled DMI declares a system model and expected catalog deviceId");
  g_msiDinput.enabled=false;g_msiDinputSourceValid=false;
  // product is the baseboard; only systemProduct supplies the Lenovo model.
  // Feed the actual resolver, then find its output in the existing total catalog.
  g_machineIdentity.systemProduct=dmi->systemProduct;ymccResolveFamily(g_machineIdentity);
  require(g_machineIdentity.resolved&&g_machineIdentity.family==YmccFamily::LenovoLegionGo,"actual DMI resolver admits this registered Lenovo system model");
  require(g_machineIdentity.deviceClass==dmi->declaredDeviceClass,"actual Lenovo DMI resolution matches the expected canonical consumer class");
  catalogDevice=ymcc::capabilities::find(g_machineIdentity.deviceClass);
  require(catalogDevice&&catalogDevice->factoryInstantiable&&catalogDevice->consumerClass==g_machineIdentity.deviceClass&&catalogDevice->deviceId==dmi->declaredDeviceId,"existing total catalog find binds the resolved Lenovo class to its registered concrete deviceId");
  require(ymccFamilyInterfacePolicy(g_machineIdentity.family)==YmccInterfacePolicy::None&&!g_msiDinputRouteDinputArmed.load(),"resolved Lenovo starts on the existing standard source without MSI mode authority");
  require(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Unsupported&&!g_msiNativeForegroundLease.active,"resolved Lenovo retains unsupported native-OFF foreground isolation and no native write lease");
 }
 if(family==3&&dmi&&dmi->systemProduct){
  g_machineIdentity.systemProduct=dmi->systemProduct;ymccResolveFamily(g_machineIdentity);
  require(g_machineIdentity.resolved&&g_machineIdentity.family==YmccFamily::OneXPlayer&&g_machineIdentity.deviceClass==dmi->declaredDeviceClass,"actual directed OneX DMI resolves its concrete class");
  const auto binding=ymcc::capabilities::onexVirtual::bind(g_machineIdentity.deviceClass);
  require(binding.known()&&ymcc::capabilities::onexVirtual::recognized(binding,g_machineIdentity.deviceClass),"directed OneX class binds the actual legacy or reviewed capability");
  fixtureOneXEnabled=true;fixtureOneXShape="flow";
  // Independent process-equivalent discovery state, not post-refresh proof.
  // Direct helper cases previously left a completed boundary in this process.
  g_xinputPreTargetSlots=0;g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;
  g_xinputSourceReconfirmMask=0;g_xinputReconfirmIdentity.clear();g_xinputReconfirmRejectLogged=false;
  g_xinputPreTargetBoundaryScanned=false;g_xinputBoundaryScanEpoch=0;
  g_xinputSlotFullScanOnce=false;g_xinputSlotFullScanEpochStart=0;
  g_xinputDiscoveryDirty=true;g_xinputDiscoveryLastTick=0;
  {std::lock_guard<std::mutex> identity(g_hidHidePhysicalIdentityMx);g_hidHidePhysicalIdentity=fixtureOneXSource();}
 }
 reg_fs();if(registerCase)cases.push_back({{"name",name},{"controlledFamily",familyName(family)},{"realManagedPipe",true},{"sourceDiscoveries",json::array()},{"periods",json::array()},{"managedReceipts",json::array()}});
 if(fixtureOneXEnabled){cases.back()["familySelection"]="onex";cases.back()["suite"]="family-lifecycle";cases.back()["managedCommandPath"]=true;
  cases.back()["filterAttached"]="UNKNOWN";cases.back()["externalSuppressionVerified"]=false;
  const auto binding=ymcc::capabilities::onexVirtual::bind(g_machineIdentity.deviceClass);
  cases.back()["controlledDmi"]={{"boundary","controlled DMI leaf; actual resolver and capability binding; not hardware acceptance"},
   {"manufacturer",g_machineIdentity.manufacturer},{"product",g_machineIdentity.product},{"systemProduct",g_machineIdentity.systemProduct},
   {"resolver","ymccResolveFamily"},{"runtimeDeviceClass",g_machineIdentity.deviceClass},{"deviceId",std::string(binding.deviceId())},{"canonicalClass",std::string(binding.canonicalClass())}};
 }
 if(dmi&&family!=5&&!fixtureOneXEnabled)cases.back()["controlledDmi"]={{"boundary","fixture-leaf; not catalog resolution or hardware acceptance"},{"manufacturer",dmi->manufacturer},{"product",dmi->product},{"declaredDeviceClass",dmi->declaredDeviceClass},{"runtimeDeviceClass",g_machineIdentity.deviceClass},{"source","capabilities/CONTROLLER-REPAIR-SUPPORT.json"},{"jsonPointer",dmi->supportPointer}};
 if(family==5){
  cases.back()["controlledDmi"]={{"boundary","controlled DMI leaf; actual family/catalog resolution; not hardware acceptance"},{"manufacturer",g_machineIdentity.manufacturer},{"product",g_machineIdentity.product},{"systemProduct",g_machineIdentity.systemProduct},{"declaredDeviceClass",dmi->declaredDeviceClass},{"runtimeDeviceClass",g_machineIdentity.deviceClass},{"canonicalClass",std::string(catalogDevice->consumerClass)},{"deviceId",std::string(catalogDevice->deviceId)},{"resolver","ymccResolveFamily"},{"source","deps/handheldcompanion-runtime/source/Devices/IDevice.cs"},{"catalog","capabilities/hc-device-catalog.generated.h"}};
  cases.back()["nativeForegroundUnsupported"]=true;
 }
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
 // The actual canonical reader emits normal wake/late-hide work, unlike
 // the synthetic legacy sample. Dispatch the original worker; never clear
 // its busy flag/queue to force a verification final.
 if(fixtureOneXEnabled){
  fullPipeDrainQueuedLifecycle();
  cases.back()["postStartWorkerDrain"]={{"workRequested",g_inputHostLifecycleWorkRequested.load()},
   {"workInFlight",g_inputHostLifecycleWorkInFlight.load()},{"sameHost",g_inputHostProcess!=nullptr}};
 }
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
static void first(int family,const std::string& persona,const ControlledDmi* dmi=nullptr){prepare(family,std::string(familyName(family))+(family==5&&dmi&&dmi->systemProduct?std::string("/")+dmi->systemProduct:std::string{})+"/persona-first/"+persona,dmi);const auto spawns=fullPipeSpawnCount;startFirst(family,persona);close(family,persona);require(fullPipeSpawnCount==spawns+1,"first-ON-to-OFF lifecycle has exactly one Host without fallback replacement");cases.back()["passed"]=true;fixtureFullPipe=false;}
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

// One canonical directed scope table; the existing JS consumer derives its
// expected names from these frozen bytes rather than keeping a second catalog.
namespace personaLifecycle140 {
struct OneXHelperCase {const char* name;int result;unsigned issues;};
static constexpr OneXHelperCase oneXHelperCases[]={
 {"onex/helper/catalog",0,0},
 {"onex/helper/ack-unknown",-1,1},
 {"onex/helper/source-missing",-1,1},
 // Historical IDs retained: these shapes vary connector metadata only, not selected-source ownership.
 {"onex/helper/external",1,1},
 {"onex/helper/unknown-port",1,1},
 {"onex/helper/unknown-root",0,0},
 {"onex/helper/unbound-slot",0,0},
 {"onex/helper/nonstandard-source",0,0},
 {"onex/helper/multiple-roots",0,0},
 {"onex/helper/configuration-unknown",-1,0},
 {"onex/helper/cloak-off",-1,0},
 {"onex/helper/app-missing",-1,0},
 {"onex/helper/inverse-unknown",-1,0},
 {"onex/helper/root-not-hidden",-1,0},
 {"onex/helper/locations-changed",-1,0},
 {"onex/helper/ticket-at-write",-1,0},
 {"onex/helper/off-at-write",-1,0},
 {"onex/helper/cancel-at-write",-1,0},
 {"onex/helper/power-at-write",-1,0},
 {"onex/helper/root-at-write",-1,0},
 {"onex/helper/mask-at-write",-1,0},
 {"onex/helper/deadline-at-write",-1,0},
 {"onex/helper/off-after-issue",-1,1},
 {"onex/helper/deadline-after-issue",-1,1},
 {"onex/helper/initial-context-expired",-1,0},
 {"onex/helper/rehide-switch-source",-1,1},
 {"onex/helper/rehide-context-changed",-1,1},
 {"onex/helper/fresh-positive",1,1},
 {"onex/helper/owned-prehidden",1,1},
 {"onex/helper/already-hidden-hid",0,0},
 {"onex/helper/already-hidden-root",0,0}
};
static bool oneXFamilyRequested(){
 const auto* family=std::getenv("YMCC_FIXTURE_FAMILY");
 if(!family || !*family)return false;
 require(std::strcmp(family,"onex")==0,"family-lifecycle rejects an unknown directed family");return true;
}
static void oneXCatalogCase(){
 using namespace ymcc::capabilities;using namespace ymcc::capabilities::onexVirtual;
 cases.back()["catalogBindings"]=json::array();unsigned concrete=0;
 const auto verify=[&](Binding binding,std::string_view cls,std::string_view id,const char* storage){
  require(binding.known()&&recognized(binding,cls)&&binding.canonicalClass()==cls&&binding.deviceId()==id,"actual OneX binding proves known/deviceId/canonicalClass for this concrete registry identity");
  require(gameSource(binding,0x045e,0x028e,1,4)&&gameSource(binding,0x045e,0x028e,1,5),"OneX concrete binding admits only observed standard game collections");
  require(standardGameCollection(binding,1,4)&&standardGameCollection(binding,1,5)&&!standardGameCollection(binding,0xff00,5)&&!standardGameCollection(binding,1,6),
   "family metadata classifies standard GAME only; no root, slot, internal or refresh authority is implied");
  require(!gameSource(binding,0x045e,0x028f,1,5)&&!gameSource(binding,0x045e,0x028e,0xff00,5)&&!gameSource(binding,0x045e,0x028e,1,6),"vendor and non-game collections grant no refresh capability");
  cases.back()["catalogBindings"].push_back({{"deviceId",std::string(binding.deviceId())},{"canonicalClass",std::string(binding.canonicalClass())},{"known",binding.known()},{"storage",storage}});++concrete;
 };
 for(const auto& row:devices){
  if(row.consumerClass.find("HandheldCompanion.Devices.OneXPlayer")!=0)continue;
  const auto binding=ymcc::capabilities::onexVirtual::bind(row.consumerClass);
  require(binding.known()==row.factoryInstantiable,"legacy abstract catalog rows remain unbound");
  if(row.factoryInstantiable){require(binding.model==&row&&!binding.source,"legacy OneX binding retains exact catalog membership");verify(binding,row.consumerClass,row.deviceId,"model-only");}
 }
 for(const auto& row:reviewedSourceIdentities){
  const auto binding=ymcc::capabilities::onexVirtual::bind(row.canonicalClass);
  require(!binding.model&&binding.source==&row&&!row.sourceSha256.empty()&&!row.sourcePin.empty(),"reviewed identities retain source membership and upstream pin without a fictitious HC factory");
  verify(binding,row.canonicalClass,row.deviceId,"reviewed-source-only");
 }
 require(concrete==17,"OneX directed immutable checks cover all seventeen concrete legacy plus reviewed identities");
 const auto legacy=ymcc::capabilities::onexVirtual::bind("HandheldCompanion.Devices.OneXPlayerApex");
 const auto reviewed=ymcc::capabilities::onexVirtual::bind("HandheldCompanion.Devices.OneXPlayer3");
 require(legacy.model&&reviewed.source,"membership negatives begin with actual registered rows");
 const auto copiedModel=*legacy.model;const auto copiedSource=*reviewed.source;
 const Binding foreignModel{&copiedModel,nullptr},foreignSource{nullptr,&copiedSource},mixed{legacy.model,reviewed.source};
 require(!foreignModel.known()&&!foreignSource.known()&&!mixed.known(),"copied and mixed rows cannot forge immutable registry membership");
 require(!gameSource(foreignModel,0x045e,0x028e,1,5)&&!gameSource(foreignSource,0x045e,0x028e,1,5)&&
  !recognized(mixed,"HandheldCompanion.Devices.OneXPlayer3"),"forged membership cannot authorize an observed root or recognized class");
 require(!recognized(legacy,"HandheldCompanion.Devices.OneXPlayer3")&&
  !ymcc::capabilities::onexVirtual::bind("HandheldCompanion.Devices.OneXAOKZOE").known(),"cross-model and AOKZOE source classes cannot borrow OneX authority");
 require(!ymcc::capabilities::onexVirtual::bind("unknown-onex-class").known()&&!gameSource({},0x045e,0x028e,1,5),"unknown classes and empty bindings remain unsupported");
 require(recognized(ymcc::capabilities::onexVirtual::bind("HandheldCompanion.Devices.OneXPlayer.OneXPlayerApex"),"HandheldCompanion.Devices.OneXPlayerApex"),"Apex catalog namespace alias keeps the exact canonical identity");
 cases.back()["concreteCatalogModels"]=concrete;
}
// Canonical DMI/persona parameters consumed by both the real cases and the
// existing runner's exact selection. No new runner or test identity is added.
struct OneXLifecycleModel {const char* name;const char* product;const char* deviceClass;};
static constexpr OneXLifecycleModel oneXLifecycleModels[]={
 {"Apex","ONEXPLAYER APEX","HandheldCompanion.Devices.OneXPlayerApex"},
 {"X2","ONEXPLAYER X2","HandheldCompanion.Devices.OneXPlayerX2"},
 {"OneX3","ONEXPLAYER 3","HandheldCompanion.Devices.OneXPlayer3"}
};
static constexpr const char* oneXLifecyclePersonas[]={"steamdeck","dualsense-edge","elite"};
// These names drive the original deadline case's subvariant loop directly.
static constexpr const char* oneXDeadlineVariants[]={"read-deadline","returned-child-stale","boundary-scan-not-consumed"};
static constexpr const char* oneXFreshVariants[]={"original-fresh","late-topology-after-readmission"};
static constexpr const char* oneXSelectedVariants[]={"original-selected","same-root-current-child-stale-selected"};
static constexpr const char* oneXSelectedInventoryVariants[]={"original-fresh-inventory","same-root-two-live-children","same-root-incomplete-inventory"};
static constexpr const char* oneXUnboundVariants[]={"original-unbound-slot","selected-A-owned-B-bound-slot-1","selected-A-owned-B-reused-slot-0"};
// The new alignment policy and original final write guard remain production
// bodies. OS leaves only supply first-hide RawInput and current child inventory.
static void oneXSelectedEvidence140(json& observation,bool stale){
 json initial,inventory,transport,alignment;unsigned hides=0,inventories=0,transports=0,alignments=0,issued=0;
 for(const auto& io:fixtureOneXIo){
  const auto leaf=io.value("leaf","");
  if(leaf=="first-hide-selected-child-input"){initial=io;++hides;}
  if(leaf=="selected-child-inventory"){inventory=io;++inventories;}
  if(leaf=="selected-child-before-cycle-transport"){transport=io;++transports;}
  if(leaf=="cycle-dispatch"&&io.value("issueReserved",false))++issued;
 }
 for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-onex-start-refresh"&&
  event["detail"].value("stage","")=="selected-source-aligned"){alignment=event["detail"];++alignments;}
 observation["firstHideSelectedInput"]=initial;observation["selectedChildInventory"]=inventory;
 observation["selectedBeforeCycleTransport"]=transport;observation["selectedAlignmentEvent"]=alignment;
 require(hides==1&&transports==1&&issued==1&&alignments==(stale?1u:0u)&&inventories==(stale?1u:0u),
  "Elite selected parameters reach one original final transport guard; stale selection alone requires one production alignment");
 const auto& selected=initial["selected"];const auto& owned=initial["journalPhysical"];
 require(initial.value("sourceLiveAtOsLeaf",false)&&initial.value("sourceLocked",false)&&initial.value("preTargetSlots",0u)!=0&&
  initial.value("primarySlot",4u)==0&&initial.value("sourceMask",0u)==1&&initial.value("connectedMask",0u)==1&&
  !initial.value("prepared",true)&&!initial.value("prepareMayHaveReachedHost",true)&&
  selected["root"]==owned["root"]&&selected["vendorId"]==owned["vendorId"]&&selected["productId"]==owned["productId"]&&
  owned.value("hid","").find("&IG_00")!=std::string::npos,
  "actual first hide preserves canonical source liveness and slot while the owned journal contains fresh IG_00");
 if(stale){
  require(selected.value("hid","").find("&IG_01")!=std::string::npos&&selected["identity"]!=owned["identity"]&&
   inventory.value("wrapper","")=="inputHostOneXResolveSelectedChildren"&&inventory.value("complete",false)&&
   inventory.value("count",0u)==1&&inventory["children"].size()==1&&inventory["children"][0]==owned&&
   inventory["selectedAtLeafReturn"]==selected&&!inventory.value("leafPublishesSelected",true),
   "independent uncollapsed inventory returns only current IG_00 and leaves the old same-root selected IG_01 untouched");
  const auto& evidence=alignment["selectedCacheAlignment"];
  require(alignment.value("reason","")=="current-same-root-child-before-cycle"&&
   evidence.value("reason","")=="current-same-root-child-published-before-cycle"&&evidence.value("inventoryComplete",false)&&
   evidence.value("inventoryCount",0u)==1&&evidence["selectedChildBefore"]==selected["hid"]&&evidence["ownedChild"]==owned["hid"]&&
   evidence.value("sameRoot",false)&&evidence.value("published",false)&&evidence.value("reverseSlot",4u)==0&&
   evidence["topologyEpoch"]==initial["topologyEpoch"]&&evidence["identityEpoch"]==initial["identityEpoch"]&&
   !evidence.value("newUsbCycle",true)&&!evidence.value("targetPrepared",true)&&
   !alignment.value("driverOperationMayHaveIssued",true),
   "actual inputHostOneXAlignSelectedBeforeCycle proves the unique child and publishes it before spending the original cycle");
 }else require(selected==owned,"original-selected Elite parameter keeps its exact first-hide selected identity");
 require(transport["selected"]==owned&&transport["journalPhysical"]==owned&&transport.value("originalSelectedExact",false)&&
  transport.value("sourceLocked",false)&&transport.value("candidateSlot",4u)==0&&transport.value("primarySlot",4u)==0&&
  transport.value("sourceMask",0u)==1&&transport.value("connectedMask",0u)==1&&
  transport["proofTopology"]==transport["topologyEpoch"]&&transport["topologyEpoch"]==initial["topologyEpoch"]&&
  transport["proofIdentity"]==transport["identityEpoch"]&&transport["identityEpoch"]==initial["identityEpoch"]&&
  transport["hostEpoch"]==initial["hostEpoch"]&&transport["runId"]==initial["runId"]&&transport["power"]==initial["power"]&&
  transport["requestId"]==initial["requestId"]&&transport["episodeDeadline"]==initial["originalDeadline"]&&
  transport.value("episodeDeadline",0ULL)==fixtureOneXDeadline&&transport.value("clock",0ULL)<fixtureOneXDeadline&&
  !transport.value("prepared",true)&&!transport.value("prepareMayHaveReachedHost",true),
  "exact OriginalSelectedSource and real final MayIssue reach the single cycle under unchanged ticket Host power epochs and deadline");
 observation["selectedAlignmentProven"]=stale;observation["helperIo"]=fixtureOneXIo;
}
// Observe the existing pending scan and its real owner; a request is never
// accepted as completion. No policy/source/sample/scan verdict is substituted.
static void oneXBoundaryEvidence140(json& observation,bool consumed){
 observation["helperIo"]=fixtureOneXIo;observation["refreshEvents"]=json::array();
 json arrival,scan,request,firstDeferred;unsigned arrivals=0,scans=0,requests=0,deferrals=0,completed=0;
 for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-onex-start-refresh"){
  const auto& detail=event["detail"];observation["refreshEvents"].push_back(detail);
  if(detail.value("reason","")=="current-topology-boundary-scan-requested"){request=detail;++requests;}
  if(detail.value("stage","")=="completed"){
   ++completed;require(detail.value("freshSameRootSource",false),"boundary completion retains the actual fresh-source production verdict");
   require(detail.contains("boundaryScan")&&detail["boundaryScan"].is_object()&&detail["boundaryScan"].value("requests",0u)==1,
    "completion retains one request for the late topology rather than replaying the scan or USB cycle");
  }
 }
 for(const auto& io:fixtureOneXIo){
  const auto leaf=io.value("leaf","");
  if(leaf=="late-topology-arrival"){arrival=io;++arrivals;}
  if(leaf=="boundary-consumed-by-canonical-owner"){scan=io;++scans;}
  if(leaf=="boundary-owner-deferred"){if(!deferrals)firstDeferred=io;++deferrals;
   require(io.value("pending",false)&&!io.value("prepared",true)&&!io.value("prepareMayHaveReachedHost",true)&&
    !io.value("hostLockOwned",true)&&!io.value("scopedWriteInherited",true)&&io.value("cycles",0u)==1&&
    io.value("preTargetSlots",0u)!=0&&io.value("sourceLocked",false)&&io.value("reconfirmMask",1u)==0,
    "pending boundary waits release the owner fences and keep the live admitted source unprepared without another cycle");
  }
 }
 require(arrivals==1&&requests==1&&deferrals>=2,"one actual post-readmission arrival requests one scan and survives repeated same-epoch owner waits");
 require(arrival.value("readmitted",false)&&arrival.value("sourceLiveAtOsLeaf",false)&&arrival.value("sourceLocked",false)&&
  arrival.value("preTargetSlots",0u)!=0&&arrival.value("sourceMask",0u)==arrival.value("connectedMask",1u)&&
  arrival.value("reconfirmMask",1u)==0&&!arrival.value("fullScanPending",true)&&
  !arrival.value("prepared",true)&&!arrival.value("prepareMayHaveReachedHost",true),
  "late arrival follows actual readmission without a lost source, empty preTargetSlots, pending scan or prepared target");
 require(arrival["oldBoundaryEpoch"]==arrival["topologyBefore"]&&arrival["boundaryAfterArrival"]==arrival["oldBoundaryEpoch"]&&
  arrival.value("currentEpoch",0u)==arrival.value("topologyBefore",0u)+1,
  "only the controlled OS dirty event advances topology and leaves the old completed boundary stale");
 require(arrival.value("freshSampleBeforeArrival",false),"late arrival follows the unchanged production FreshSample predicate on an actual owner sample");
 const auto& boundary=request["boundaryScan"];
 require(request.value("repairRevision","")=="VC-ONEX-FAMILY-BOUNDARY-20261105-R1"&&boundary.is_object()&&
  boundary.value("requests",0u)==1&&boundary["observedEpoch"]==arrival["oldBoundaryEpoch"]&&
  boundary["requestedTopologyEpoch"]==arrival["currentEpoch"]&&boundary.value("requestOnly",false)&&!boundary.value("newUsbCycle",true)&&
  request["requestId"]==arrival["requestId"]&&arrival.value("deadline",0ULL)==fixtureOneXDeadline,
  "the production boundaryScan note binds the old boundary and new request to the original Start without a new USB cycle");
 for(const auto& io:fixtureOneXIo)if(io.value("leaf","")=="boundary-owner-deferred")
  require(io["requestedEpoch"]==arrival["currentEpoch"]&&io["topologyEpoch"]==arrival["currentEpoch"]&&
   io["boundaryEpoch"]==arrival["oldBoundaryEpoch"]&&io["sequence"]==firstDeferred["sequence"]&&
   io["hostEpoch"]==arrival["hostEpoch"]&&io["runId"]==arrival["runId"],
   "same-epoch waits preserve the pending scan start, old boundary, Host identity and command sequence");
 if(!consumed){
  require(deferrals==3&&firstDeferred.value("clock",0ULL)<fixtureOneXDeadline,
   "unconsumed scan remains pending through repeated waits before its original deadline");
  bool pendingAtDeadline=false;for(const auto& io:fixtureOneXIo)if(io.value("leaf","")=="boundary-owner-deferred"&&
   io.value("clock",0ULL)==fixtureOneXDeadline)pendingAtDeadline=io.value("pending",false);
  require(pendingAtDeadline,"the last real wait reaches the original deadline with the scan still pending before normal Start cleanup");
 }
 require(scans==(consumed?1u:0u)&&completed==(consumed?1u:0u),"a pending flag alone cannot complete the refresh or replace the one actual canonical scan");
 if(consumed){
  require(scan["requestedEpoch"]==arrival["currentEpoch"]&&scan["boundaryBeforeRead"]==arrival["oldBoundaryEpoch"]&&
   scan["boundaryAfterFirstRead"]==arrival["currentEpoch"]&&!scan.value("pendingAfterFirstRead",true)&&
   scan["topologyEpoch"]==arrival["currentEpoch"]&&scan["sampleTopology"]==arrival["currentEpoch"]&&
   scan.value("sampleSequence",0ULL)>arrival.value("sampleSequence",0ULL)&&scan.value("sampleLive",false)&&
   scan.value("sampleContextStable",false)&&scan.value("sampleKnown",false)&&
   scan.value("clock",0ULL)>=scan.value("sampleCompleted",0ULL)&&scan.value("clock",0ULL)-scan.value("sampleCompleted",0ULL)<=200&&
   !scan.value("prepared",true)&&!scan.value("prepareMayHaveReachedHost",true)&&
   !scan.value("hostLockOwned",true)&&!scan.value("scopedWriteInherited",true),
   "real gamepadReadState consumes FullScanOnce and publishes a current fresh sample before PREPARE");
  require(scan["actualSlotPolls"].size()==4&&std::all_of(scan["actualSlotPolls"].begin(),scan["actualSlotPolls"].end(),[](const auto& polls){return polls.template get<unsigned>()>0;}),
   "the actual consuming owner polls all four controlled XInput OS slots");
 }
 observation["lateArrival"]=arrival;observation["boundaryRequest"]=request;observation["boundaryOwnerScan"]=scan;
 observation["sameEpochDeferredWaits"]=deferrals;observation["boundaryActuallyConsumed"]=consumed;
}
static void oneXManagedLifecycleCases140(){
 require(fspath::is_regular_file(fixtureManagedDll),"directed OneX requires the current owned SDK-free managed fixture");
 for(const bool initialPending:{false,true})for(const auto& model:oneXLifecycleModels)for(const auto* persona:oneXLifecyclePersonas){
  if(initialPending && std::strcmp(persona,"elite")==0)continue; // Existing Elite requires an admitted physical source before Host creation.
  const ControlledDmi dmi={"ONE-NETBOOK TECHNOLOGY CO., LTD.",model.product,model.deviceClass,nullptr,model.product,nullptr};
  const bool apex=!initialPending&&std::strcmp(model.name,"Apex")==0;
  const bool freshParameterized=apex&&std::strcmp(persona,"steamdeck")==0;
  const bool selectedParameterized=apex&&std::strcmp(persona,"elite")==0;
  const bool parameterized=freshParameterized||selectedParameterized;
  const auto variantCount=freshParameterized?sizeof(oneXFreshVariants)/sizeof(oneXFreshVariants[0]):
   selectedParameterized?sizeof(oneXSelectedVariants)/sizeof(oneXSelectedVariants[0]):1u;
  for(unsigned variant=0;variant<variantCount;++variant){
  prepare(3,std::string("onex/lifecycle/")+(initialPending?"pending/":"")+model.name+"/"+persona,&dmi,variant==0);
  if(parameterized){
   if(!variant)cases.back()["subvariants"]=json::array();
   const auto* name=freshParameterized?oneXFreshVariants[variant]:oneXSelectedVariants[variant];
   if(freshParameterized)fixtureOneXBoundaryVariant=name;else fixtureOneXSelectedVariant=name;
   cases.back()["subvariants"].push_back({{"variant",name},{"passed",false}});
  }
  cases.back()["passed"]=false;
  auto& observation=parameterized?cases.back()["subvariants"].back():cases.back();
  if(initialPending){fixtureOneXShape="initial-pending";fixtureOneXInitialPending=true;fixtureXinputSourceLive=false;}
  const auto spawns=fullPipeSpawnCount;
  try{startFirst(3,persona);}catch(...){
   // In particular, old R2 must retain its original-deadline timeout evidence.
   observation["helperIo"]=fixtureOneXIo;observation["originalDeadline"]=fixtureOneXDeadline;
   observation["clockAfterStart"]=startProtectionClock;observation["hostSpawnDelta"]=fullPipeSpawnCount-spawns;
   observation["refreshEvents"]=json::array();for(const auto& event:fixtureEvents)
    if(event.value("event","")=="input-host-onex-start-refresh")observation["refreshEvents"].push_back(event["detail"]);
   throw;
  }
  unsigned completed=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-onex-start-refresh"&&event["detail"].value("stage","")=="completed")++completed;
  require(completed==1&&fixtureOneXIssues==1&&fixtureOneXDispatchCalls==1&&fixtureOneXOwnerReads>0,"actual Start completes exactly one original helper refresh from actual canonical read owner before target PREPARE");
  if(initialPending){
   require(fixtureOneXInitialOwnerReads>0 && !fixtureOneXInitialPending,
    "OneX093203 pre-hide pending source reaches actual canonical read/admission before any target is prepared");
   bool initialReady=false;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-onex-start-refresh" &&
    event["detail"].value("reason","")=="selected-source-ready-before-hide")initialReady=true;
   require(initialReady,"OneX093203 pre-hide observation emits its own actual current-ticket source-ready stage");
  }
  observation["freshSourceReturnProven"]=true;observation["actualOwnerReads"]=fixtureOneXOwnerReads;
  observation["helperIo"]=fixtureOneXIo;observation["originalDeadline"]=fixtureOneXDeadline;
  const auto original=g_virtualStartProtection.view(startProtectionClock);
  observation["originalRequestId"]=original.requestId;observation["clockAfterSuccess"]=startProtectionClock;
  require(startProtectionClock<fixtureOneXDeadline&&finalCount(original.requestId,"success")==1&&finalCount(original.requestId,"failure")==0,
   "OneX lifecycle parameters succeed once within the same original player deadline");
  for(const auto& io:fixtureOneXIo)if(io.value("leaf","")=="cycle-dispatch")
   require(io.value("requestId",0ULL)==original.requestId&&io.value("episodeDeadline",0ULL)==fixtureOneXDeadline,
    "the single actual cycle keeps the original successful request and absolute deadline");
  if(selectedParameterized)oneXSelectedEvidence140(observation,variant!=0);
  if(freshParameterized&&variant)oneXBoundaryEvidence140(observation,true);
  if(freshParameterized&&!variant)require(!fixtureOneXLateArrivalApplied&&fixtureOneXBoundaryDeferrals==0,
   "original fresh parameter retains its undisturbed canonical path");
  close(3,persona);
  require(fullPipeSpawnCount==spawns+1,"OneX persona and OFF keep exactly one actual managed Host without fallback creation");
  std::ifstream raw{fspath::path(fullPipeReceipts.back())};json child;raw>>child;
  unsigned cycle=0,prepareTarget=0,position=0,cyclePosition=0,preparePosition=0;
  for(const auto& observation:child["observations"])if(observation.value("kind","")=="command-decoded"){
   ++position;const auto command=observation.value("command","");if(command=="HIDHIDE_CYCLE"){++cycle;cyclePosition=position;}if(command=="PREPARE_TARGET"){++prepareTarget;preparePosition=position;}
  }
  require(cycle==1&&prepareTarget==1&&cyclePosition<preparePosition,"actual managed command decode proves one cycle before one PREPARE_TARGET");
  observation["cycleCommands"]=cycle;observation["prepareCommands"]=prepareTarget;
  observation["hostSpawnDelta"]=fullPipeSpawnCount-spawns;observation["managedChild"]=child;observation["passed"]=true;
  if(parameterized&&!variant){
   cases.back()["freshSourceReturnProven"]=true;cases.back()["actualOwnerReads"]=fixtureOneXOwnerReads;
   cases.back()["helperIo"]=fixtureOneXIo;
  }
  fixtureOneXEnabled=false;fixtureFullPipe=false;
  }
  if(parameterized)require(cases.back()["subvariants"].size()==variantCount&&
   std::all_of(cases.back()["subvariants"].begin(),cases.back()["subvariants"].end(),[](const auto& row){return row.value("passed",false);}),
   "original Apex lifecycle case passes only after every source-declared parameter succeeds");
  cases.back()["passed"]=true;
 }
 // One actual Start uses the original player deadline through HELLO connect,
 // cycle write and cycle read. Time is supplied only after real kernel returns.
 const auto& model=oneXLifecycleModels[0];
 const ControlledDmi dmi={"ONE-NETBOOK TECHNOLOGY CO., LTD.",model.product,model.deviceClass,nullptr,model.product,nullptr};
 for(const auto* variant:oneXDeadlineVariants){
 const bool returnedStale=std::strcmp(variant,"returned-child-stale")==0;
 const bool boundaryUnconsumed=std::strcmp(variant,"boundary-scan-not-consumed")==0;
 const bool readDeadline=!returnedStale&&!boundaryUnconsumed;
 prepare(3,"onex/lifecycle/deadline-read-stage",&dmi,readDeadline);
 fixtureOneXShape=returnedStale?"returned-stale":boundaryUnconsumed?"flow":"deadline-read-stage";
 if(boundaryUnconsumed)fixtureOneXBoundaryVariant=variant;
 if(readDeadline)cases.back()["subvariants"]=json::array();
 cases.back()["passed"]=false;cases.back()["subvariants"].push_back({{"variant",variant},{"passed",false}});
 auto& observation=cases.back()["subvariants"].back();const auto failureBegin=failures.size();
 const auto originalSource=fixtureOneXSource();
 observation["sourceBefore"]={{"root",originalSource.baseContainerDeviceInstanceId},{"hid",originalSource.deviceInstanceId},{"identity",originalSource.physicalIdentity}};
 const auto doc=targetDocument("steamdeck",0);require(sgWriteFileAtomic(YM_SETTINGS_FILE,doc.dump()),"deadline case seeds its private persona request");
 inputCaptureRefreshRealStickSettings(true);observeSource(3,"steamdeck");
 const auto admission=g_virtualStartProtection.admit("steamdeck",startProtectionClock);
 require(admission.accepted&&admission.requestId,"deadline case actually admits its initial player ticket before consuming time");
 const auto request=g_virtualStartProtection.view(startProtectionClock);
 require(request.requestId==admission.requestId&&request.remainingMs==30000,"deadline case begins with the real thirty-second execution period");
 fixtureOneXDeadline=startProtectionClock+request.remainingMs;
 if(readDeadline)advance(27500);
 fixturePipeBudgetEnabled=fixturePipeBudgetActive=true;fixturePipeBudgetDeadline=fixtureOneXDeadline;fixturePipeBudgetCommand="initial-start";fixturePipeBudgetAdvancePhase.clear();
 const auto spawns=fullPipeSpawnCount;const bool started=inputHostStart();fixturePipeBudgetEnabled=false;
 observation["started"]=started;observation["clockAfterStart"]=startProtectionClock;observation["originalDeadline"]=fixtureOneXDeadline;
 observation["originalRequestId"]=request.requestId;observation["helperIo"]=fixtureOneXIo;
 {std::lock_guard<std::mutex> identity(g_hidHidePhysicalIdentityMx);const auto& source=g_hidHidePhysicalIdentity;
  observation["sourceAfterStart"]={{"root",source.baseContainerDeviceInstanceId},{"hid",source.deviceInstanceId},{"identity",source.physicalIdentity}};}
 observation["hostSpawnDelta"]=fullPipeSpawnCount-spawns;observation["issuedRefreshes"]=fixtureOneXIssues;
 observation["canonicalOwnerReads"]=fixtureOneXOwnerReads;observation["refreshEvents"]=json::array();
 for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-onex-start-refresh")observation["refreshEvents"].push_back(event["detail"]);
 if(readDeadline){
 require(!started&&fullPipeSpawnCount==spawns+1&&fixtureOneXIssues==1,"actual cycle read reaching the original deadline cannot PREPARE or create a target");
 bool read=false,connect=false;for(const auto& io:fixtureOneXIo)if(io.value("leaf","")=="actual-pipe-stage"){
  const auto before=io.value("clockBefore",0ULL);const auto timeout=io.value("timeoutMs",0u);
  require(before<fixtureOneXDeadline&&timeout<=fixtureOneXDeadline-before,"each actual connect/write/read borrows only the original absolute remainder");
  if(io.value("command","")=="HELLO"&&io.value("stage","")=="connect")connect=true;
  if(io.value("command","")=="HIDHIDE_CYCLE"&&io.value("stage","")=="read"){read=true;require(timeout==1000,"cycle read receives the original 1000ms remainder after preceding IO costs");}
 }
 require(read&&connect&&startProtectionClock==fixtureOneXDeadline,"deadline negative actually reaches connect and cycle read without renewing the ticket");
 }else if(returnedStale){
  observation["staleSamples"]=json::array();bool waited=false,failed=false,completed=false;
  for(const auto& io:fixtureOneXIo)if(io.value("leaf","")=="returned-sample-age")observation["staleSamples"].push_back(io);
  for(const auto& detail:observation["refreshEvents"]){waited|=detail.value("stage","")=="waiting-source";
   failed|=detail.value("stage","")=="failed"&&detail.value("reason","")=="same-root-source-return-unproven";
   completed|=detail.value("stage","")=="completed";}
  require(!started&&startProtectionClock==fixtureOneXDeadline&&fixtureOneXReturned&&fixtureOneXIssues==1&&fixtureOneXDispatchCalls==1&&
   fullPipeSpawnCount==spawns+1&&!g_inputHostPrepared&&!g_inputHostPrepareMayHaveReachedHost&&!g_inputHostActive,
   "returned stale source cannot PREPARE through the original thirty-second deadline after one real managed cycle and one Host");
  require(fixtureOneXOwnerReads>0&&observation["staleSamples"].size()>1&&waited&&failed&&!completed&&fixtureOneXRehideSelections==0,
   "returned stale negative reaches real source owner and repeated stale observations without refresh completion or rehide");
  for(const auto& sample:observation["staleSamples"]){
   require(sample.value("readmitted",false)&&sample.value("nonAgeProof",false)&&sample.value("sampleAgeMs",0ULL)>200&&
    sample.value("clock",0ULL)<fixtureOneXDeadline&&sample.value("requestId",0ULL)==request.requestId&&sample.value("deadline",0ULL)==fixtureOneXDeadline,
    "actual ymStartAdmitSource succeeds and all non-age sample facts hold while the OS clock makes each returned sample older than 200ms");
   require(sample.value("originalRoot","")==hidHideUpper(originalSource.baseContainerDeviceInstanceId)&&
    hidHideUpper(sample.value("returnedRoot",""))==hidHideUpper(originalSource.baseContainerDeviceInstanceId)&&
    hidHideUpper(sample.value("returnedHid",""))!=hidHideUpper(originalSource.deviceInstanceId)&&
    sample.value("sampleFromActualCanonicalOwner",false)&&observation["sourceAfterStart"]["identity"]==sample.value("returnedIdentity","")&&
    !sample.value("prepared",true)&&!sample.value("prepareMayHaveReachedHost",true),
    "production selects the returned new HID child of the original root before successful readmission without any target preparation");
  }
  observation["sourceReturnProven"]=true;observation["sourceReadmitted"]=true;observation["freshSourceReturnProven"]=false;
  observation["reachedStage"]="same-root-new-child/readmitted/sample-age-over-200/unprepared-until-original-deadline";
 }else{
  oneXBoundaryEvidence140(observation,false);
  fullPipeVerificationStep();fullPipeVerificationStep();const auto expired=g_virtualStartProtection.view(startProtectionClock);
  require(!started&&startProtectionClock==fixtureOneXDeadline&&expired.requestId==request.requestId&&expired.phase=="expired"&&
   !expired.locked&&finalCount(request.requestId,"failure")==1&&finalCount(request.requestId,"success")==0&&
   fullPipeSpawnCount==spawns+1&&fixtureOneXIssues==1&&fixtureOneXDispatchCalls==1&&
   fixtureXinputSourceLive&&
   !g_inputHostPrepared&&!g_inputHostPrepareMayHaveReachedHost&&!g_inputHostActive,
   "an unconsumed boundary request expires once at the original deadline without PREPARE, another cycle or another Host");
  observation["finalFailureCount"]=finalCount(request.requestId,"failure");observation["finalSuccessCount"]=finalCount(request.requestId,"success");
  observation["originalRequestId"]=request.requestId;observation["originalDeadline"]=fixtureOneXDeadline;
  observation["freshSourceReturnProven"]=false;observation["reachedStage"]="readmitted/late-topology/pending-scan-unconsumed/original-deadline";
 }
 inputHostStop("virtual-target-disabled");const auto child=fullPipeChildOutcome();
 // Program.RunPipeProtocol returns (5, read-failed) on the parent closing
 // its pipe at the original deadline. Settle preserves that source error
 // after successful release; it must not be relabelled as ordered exit 0.
 const auto terminal=child.value("terminalSettlement",json::object());
 observation["managedChild"]=child;observation["terminalSettlement"]=terminal;
 if(readDeadline){
 require(child.value("nativeObservedProcessExit",false)&&child.value("nativeObservedExitCode",999)==5&&
  child.value("pipeExitCode",999)==5&&child.value("creates",999)==0&&child.value("finalCleanupOk",false)&&
  !child.value("hasController",true)&&!child.value("hasContext",true)&&
  terminal.value("SourceExitCode",999)==5&&terminal.value("ExitCode",999)==5&&
  terminal.value("Reason",std::string{})=="read-failed"&&terminal.value("ManagedReleaseComplete",false)&&
  terminal.value("ResourceDisposition",std::string{})=="managed-release-complete"&&
  terminal.value("DirectReleaseAttempts",999)==1&&!terminal.value("Retried",true),
  "deadline failure preserves exact read-failed exit5 and naturally releases its real managed child with no target created");
 unsigned decoded=0;
 for(const auto& observation:child["observations"])if(observation.value("kind",std::string{})=="command-decoded"){
  require(observation.contains("command")&&observation["command"].is_string(),"actual decoded command carries its string identity");
  const auto command=observation["command"].get<std::string>();
  require(command!="PREPARE_TARGET","late read cannot proceed to PREPARE_TARGET");
  require(command==(decoded==0?"HELLO":"HIDHIDE_CYCLE"),"deadline trace decodes only HELLO then the single admitted refresh");++decoded;
 }
 require(decoded==2,"deadline trace contains the exact two original decoded commands and no target creation");
 }else{
  unsigned hello=0,cycle=0;
  for(const auto& command:child["observations"])if(command.value("kind","")=="command-decoded"){
   require(command.contains("command")&&command["command"].is_string(),"returned stale managed decode retains the real command identity");
   const auto name=command["command"].get<std::string>();hello+=name=="HELLO";cycle+=name=="HIDHIDE_CYCLE";
   require(name!="PREPARE_TARGET","returned stale source never decodes PREPARE_TARGET before or during real Stop");
  }
  require(hello==1&&cycle==1&&child.value("creates",999)==0&&child.value("actualProductionHostSessionConsumer",false),
   "returned stale proof consumes actual managed HELLO and a single cycle with no virtual target creation");
  require(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&
   !child.value("hasContext",true)&&terminal.value("ManagedReleaseComplete",false)&&
   terminal.value("ResourceDisposition",std::string{})=="managed-release-complete"&&terminal.value("DirectReleaseAttempts",999)==1&&!terminal.value("Retried",true)&&
   child.value("nativeObservedExitCode",999)==child.value("pipeExitCode",998)&&
   terminal.value("SourceExitCode",997)==child.value("pipeExitCode",998)&&terminal.value("ExitCode",996)==child.value("pipeExitCode",998),
   "returned stale child really exits and settles its own original result after managed cleanup without forged return or retry");
 }
 observation["helperIo"]=fixtureOneXIo;observation["realManagedPipe"]=true;
 if(readDeadline){cases.back()["helperIo"]=fixtureOneXIo;cases.back()["realManagedPipe"]=true;}
 fullPipeDrainQueuedLifecycle();fixtureOneXEnabled=false;fixtureFullPipe=false;
 require(fullPipeSpawnCount==spawns+1&&fullPipeChildObservers.empty()&&!g_inputHostProcess&&failures.size()==failureBegin,
  "each deadline subvariant returns its sole actual Host and retains every original assertion result");
 observation["passed"]=true;
 }
 require(cases.back()["subvariants"].size()==sizeof(oneXDeadlineVariants)/sizeof(oneXDeadlineVariants[0])&&
  std::all_of(cases.back()["subvariants"].begin(),cases.back()["subvariants"].end(),[](const auto& variant){return variant.value("passed",false);}),
  "original deadline case passes only after all declared independent subvariants pass");
 cases.back()["passed"]=true;
}
// Snapshot the exact resources of an independent original-ID subvariant before
// any helper assertion can throw. Paths are private fixture identities only.
static json oneXHelperResources140(){
 const auto paths=[](const std::vector<std::wstring>& values){json out=json::array();for(const auto& value:values)out.push_back(W2U(value));return out;};
 const auto source=[](const HidHidePhysicalIdentity& p){return json{{"resolved",p.resolved},{"sourcePath",p.sourcePath},
  {"hid",p.deviceInstanceId},{"root",p.baseContainerDeviceInstanceId},{"interface",p.baseContainerDevicePath},
  {"physicalIdentity",p.physicalIdentity},{"vendorId",p.vendorId},{"productId",p.productId},{"usagePage",p.usagePage},{"usage",p.usage}};};
 const auto visibility=[&](const HidHideVisibilitySnapshot& s){return json{{"valid",s.valid},{"cloaking",s.cloaking},
  {"hidden",paths(s.hidden)},{"applications",paths(s.applications)},{"cloakRaw",s.cloakRaw},{"hiddenRaw",s.hiddenRaw},
  {"applicationsRaw",s.applicationsRaw},{"inverseRaw",s.inverseRaw}};};
 const auto& j=g_hidHideVisibilityJournal;json sources=json::array();for(const auto& p:j.physicalSources)sources.push_back(source(p));
 std::lock_guard<std::mutex> identity(g_hidHidePhysicalIdentityMx);
 return {{"canonicalSelected",source(g_hidHidePhysicalIdentity)},{"journal",{{"active",j.active},{"transactionId",j.transactionId},
  {"physical",source(j.physical)},{"physicalSources",sources},{"hiddenByUs",paths(j.hiddenByUs)},{"before",visibility(j.before)},
  {"applicationAdded",j.applicationAdded},{"ownedApplications",paths(j.ownedApplications)},
  {"ownedApplicationResponsibilityUnknown",j.ownedApplicationResponsibilityUnknown},{"cloakChanged",j.cloakChanged},
  {"cycledBaseContainers",paths(j.cycledBaseContainers)},{"admittedTick",j.admittedTick},
  {"preReleaseCycleAttempted",j.preReleaseCycleAttempted},{"preReleaseCycleOk",j.preReleaseCycleOk}}},
  {"sourceLocked",static_cast<bool>(g_xinputPhysicalSourceLocked)},{"primarySlot",static_cast<unsigned>(g_xinputPrimarySlot)},
  {"sourceMask",static_cast<unsigned>(g_xinputPhysicalSourceMask)},{"connectedMask",static_cast<unsigned>(g_xinputConnectedMask)},
  {"unknownMask",static_cast<unsigned>(g_xinputSlotUnknownMask)},{"virtualMask",static_cast<unsigned>(g_xinputVirtualEndpointMask)},
  {"topology",g_xinputTopologyEpoch.load()},{"identityEpoch",g_xinputSlotIdentityEpoch.load()},{"power",currentPowerGeneration()},
  {"pipeSequence",g_inputHostSequence},{"prepared",g_inputHostPrepared},{"prepareMayHaveReachedHost",g_inputHostPrepareMayHaveReachedHost},
  {"active",g_inputHostActive},{"cycleInFlight",g_hidHideCycleInFlight.load()},{"forbiddenIo",forbiddenIo},
  {"ownerReads",fixtureOneXOwnerReads},{"rehideSelections",fixtureOneXRehideSelections}};
}
static void oneXFamilyHelperCases140(){
 const auto begin=cases.size();
 for(const auto& test:oneXHelperCases){
  require(fullPipeChildObservers.empty()&&!g_inputHostProcess,"OneX independent helper case starts without an owned Host");
  seed();fixtureFullPipe=false;fixtureOneXEnabled=true;
  fixtureOneXShape=std::string(test.name).substr(std::strlen("onex/helper/"));
  cases.push_back({{"name",test.name},{"suite","family-lifecycle"},{"familySelection","onex"},
   {"scope","production-helper/native-event-host"},{"realManagedPipe",false},{"managedCommandPath",false},
   {"filterAttached","UNKNOWN"},{"externalSuppressionVerified",false},{"freshSourceReturnProven",false}});
  if(fixtureOneXShape=="catalog"){cases.back()["scope"]="immutable-capability/catalog";oneXCatalogCase();cases.back()["passed"]=true;fixtureOneXEnabled=false;continue;}
  const bool unboundParameterized=fixtureOneXShape=="unbound-slot";
  const bool inventoryParameterized=fixtureOneXShape=="fresh-positive";
  const bool parameterized=unboundParameterized||inventoryParameterized;
  const auto variantCount=unboundParameterized?sizeof(oneXUnboundVariants)/sizeof(oneXUnboundVariants[0]):
   inventoryParameterized?sizeof(oneXSelectedInventoryVariants)/sizeof(oneXSelectedInventoryVariants[0]):1u;
  if(parameterized)cases.back()["subvariants"]=json::array();
  for(unsigned variant=0;variant<variantCount;++variant){
  if(variant){
   // The previous variant's event/TLS/journal owner has left scope before seed.
   require(fullPipeChildObservers.empty()&&!g_inputHostProcess,"OneX subvariant owns an independent native event Host");
   seed();fixtureFullPipe=false;fixtureOneXEnabled=true;fixtureOneXShape=unboundParameterized?"unbound-slot":"fresh-positive";
   fixtureOneXInitialForeign=unboundParameterized;fixtureOneXForeignSlot=variant==2?0:1;
  }
  if(inventoryParameterized)fixtureOneXSelectedInventoryVariant=oneXSelectedInventoryVariants[variant];
  cases.back()["passed"]=false;
  if(parameterized)cases.back()["subvariants"].push_back({{"variant",unboundParameterized?oneXUnboundVariants[variant]:oneXSelectedInventoryVariants[variant]},{"passed",false}});
  auto& observation=parameterized?cases.back()["subvariants"].back():cases.back();
  // Actual DMI resolver for Apex (the user's OP3). This context is explicitly
  // a native event-Host producer, never a forged managed owner or final success.
  g_machineIdentity={};g_machineIdentity.attempted=true;
  g_machineIdentity.manufacturer="ONE-NETBOOK TECHNOLOGY CO., LTD.";
  g_machineIdentity.product="ONEXPLAYER APEX";g_machineIdentity.systemProduct="ONEXPLAYER APEX";
  ymccResolveFamily(g_machineIdentity);
  require(g_machineIdentity.resolved&&g_machineIdentity.family==YmccFamily::OneXPlayer&&
   g_machineIdentity.deviceClass=="HandheldCompanion.Devices.OneXPlayerApex","actual OneX Apex DMI resolver supplies the directed helper class");
  const auto binding=ymcc::capabilities::onexVirtual::bind(g_machineIdentity.deviceClass);
  require(binding.known()&&ymcc::capabilities::onexVirtual::recognized(binding,g_machineIdentity.deviceClass),"Apex helper uses its real registered capability facade");
  g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;
  g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_shortcutRecordingActive=false;g_inputCaptureStop=false;
  g_msiDinput.enabled=false;g_msiDinputSourceValid=false;
  const auto request=g_virtualStartProtection.admit("steamdeck",startProtectionClock);
  require(request.accepted,"direct OneX producer admits one original player ticket");
  const auto before=g_virtualStartProtection.view(startProtectionClock);fixtureOneXDeadline=startProtectionClock+before.remainingMs;
  require(before.requestId==request.requestId&&before.remainingMs==30000,"OneX borrows the original thirty-second player deadline");
  g_inputHostProcess=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  require(g_inputHostProcess!=nullptr,"native-only helper producer owns an actual closeable event");
  struct EventOwner {HANDLE handle;~EventOwner(){fixtureOneXEnabled=false;t_inputHostOneXStartWrite=nullptr;
   if(g_inputHostProcess==handle)g_inputHostProcess=nullptr;CloseHandle(handle);g_inputHostHello=false;
   g_hidHideVisibilityJournal=HidHideVisibilityJournal{};}} eventOwner{g_inputHostProcess};
  g_inputHostEpoch=1;g_inputHostRunId="fixture-onex-direct-"+std::to_string(cases.size())+(parameterized?"-variant-"+std::to_string(variant):"");
  g_inputHostBoundPowerGeneration=currentPowerGeneration();g_inputHostHello=true;
  g_inputHostPrepared=g_inputHostPrepareMayHaveReachedHost=g_inputHostActive=false;
  auto& journal=g_hidHideVisibilityJournal;journal=HidHideVisibilityJournal{};
  journal.active=true;journal.transactionId=g_inputHostRunId;journal.physical=fixtureOneXSource();
  journal.physicalSources={journal.physical};journal.before.valid=true;
  journal.hiddenByUs={U2W(journal.physical.deviceInstanceId),U2W(journal.physical.baseContainerDeviceInstanceId)};
  if(fixtureOneXShape=="already-hidden-hid")journal.before.hidden={U2W(journal.physical.deviceInstanceId)};
  if(fixtureOneXShape=="already-hidden-root")journal.before.hidden={U2W(journal.physical.baseContainerDeviceInstanceId)};
  if(fixtureOneXShape=="already-hidden-hid")journal.hiddenByUs={U2W(journal.physical.baseContainerDeviceInstanceId)};
  if(fixtureOneXShape=="already-hidden-root")journal.hiddenByUs={U2W(journal.physical.deviceInstanceId)};
  if(fixtureOneXShape=="owned-prehidden")journal.before.hidden=journal.hiddenByUs;
  const auto ownedBefore=journal.hiddenByUs;
  {std::lock_guard<std::mutex> identity(g_hidHidePhysicalIdentityMx);
   g_hidHidePhysicalIdentity=inventoryParameterized&&variant?fixtureOneXStaleSelectedChild(journal.physical):journal.physical;}
  if(fixtureOneXShape=="nonstandard-source")journal.physical.vendorId=0x1234;
  if(fixtureOneXShape=="multiple-roots")journal.physicalSources.push_back(journal.physical);
  // Controlled pre-target environment for the direct guard proof. No read,
  // post-refresh source mask, continuity, boundary or final success is forged.
  g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputPrimarySlot=0;
  g_xinputConnectedMask=1;g_xinputVirtualEndpointMask=g_xinputSlotUnknownMask=0;
  if(fixtureOneXInitialForeign){
   auto selectedA=journal.physical;
   selectedA.baseContainerDeviceInstanceId="USB\\VID_045E&PID_028E\\ONEX-SELECTED-A";
   selectedA.deviceInstanceId="HID\\VID_045E&PID_028E&IG_00\\ONEX-SELECTED-A";
   selectedA.sourcePath=selectedA.deviceInstanceId;selectedA.physicalIdentity=selectedA.deviceInstanceId+"|"+selectedA.baseContainerDeviceInstanceId;
   {std::lock_guard<std::mutex> identity(g_hidHidePhysicalIdentityMx);g_hidHidePhysicalIdentity=selectedA;}
   // Both slots are connected; the selected source remains locked to A/slot 0.
   // Only B is in the owned hidden journal, and its OS binder succeeds at slot 1.
   g_xinputConnectedMask=fixtureOneXForeignSlot==1?3:1;
  }
  {std::lock_guard<std::mutex> sample(g_inputCaptureGamepadSnapshotMx);g_inputCaptureGamepadSnapshot={};}
  fixtureXinputSourceLive=fixtureOneXFreshMode();
  if(fixtureOneXShape=="initial-context-expired")advance(before.remainingMs);
  const auto resourcesBefore=parameterized?oneXHelperResources140():json{};
  if(parameterized){observation["sourceBefore"]=resourcesBefore;observation["originalRequestId"]=request.requestId;
   observation["originalDeadline"]=fixtureOneXDeadline;observation["expectedHelperResult"]=variant?-1:test.result;
   observation["expectedPossibleDispatches"]=inventoryParameterized&&!variant?1:0;observation["controlledDmi"]={{"product",g_machineIdentity.product},{"resolver","ymccResolveFamily"},{"deviceClass",g_machineIdentity.deviceClass}};}
  std::unique_lock<InputHostMutex> hostLock(g_inputHostMx);
  const int result=inputHostOneXPostHideBeforeTarget(request.requestId,hostLock);
  if(parameterized){
   json refresh=json::array();for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-onex-start-refresh")refresh.push_back(event["detail"]);
   observation["sourceAfter"]=oneXHelperResources140();observation["helperResult"]=result;observation["io"]=fixtureOneXIo;
   observation["refreshEvents"]=refresh;observation["issuedRefreshes"]=fixtureOneXIssues;
   observation["possibleDispatches"]=fixtureOneXIssues;observation["cycleLeafCalls"]=fixtureOneXDispatchCalls;
   observation["finalMayIssueCalls"]=fixtureOneXMayIssueCalls;
   observation["touchStages"]={{"locationReads",fixtureOneXLocationsReads},{"visibilityReads",fixtureOneXVisibilityReads},
    {"cycleLeafCalls",fixtureOneXDispatchCalls},{"finalMayIssueCalls",fixtureOneXMayIssueCalls},{"possibleDispatches",fixtureOneXIssues},
    {"canonicalOwnerReads",fixtureOneXOwnerReads},{"rehideSelections",fixtureOneXRehideSelections},
    {"pipeSequenceChanged",observation["sourceAfter"]["pipeSequence"]!=resourcesBefore["pipeSequence"]}};
   observation["reachedStage"]=fixtureOneXMayIssueCalls?"final-StartMayIssue-before-dispatch":"initial-root-slot-proof";
  }
  if(inventoryParameterized&&variant){
   json inventory,refusal;unsigned inventories=0,aligned=0;
   for(const auto& io:fixtureOneXIo)if(io.value("leaf","")=="selected-child-inventory"){inventory=io;++inventories;}
   for(const auto& detail:observation["refreshEvents"]){
    if(detail.value("stage","")=="selected-source-aligned")++aligned;
    if(detail.value("reason","")=="current-selected-child-inventory-unproven")refusal=detail;
   }
   observation["selectedChildInventory"]=inventory;observation["selectedAlignmentRefusal"]=refusal;
   observation["reachedStage"]="pre-cycle-selected-alignment/current-child-inventory-unproven";
   observation["freshSourceReturnProven"]=false;
   require(resourcesBefore["canonicalSelected"]["root"]==resourcesBefore["journal"]["physical"]["root"]&&
    resourcesBefore["canonicalSelected"]["physicalIdentity"]!=resourcesBefore["journal"]["physical"]["physicalIdentity"]&&
    resourcesBefore["canonicalSelected"]["vendorId"]==resourcesBefore["journal"]["physical"]["vendorId"]&&
    resourcesBefore["canonicalSelected"]["productId"]==resourcesBefore["journal"]["physical"]["productId"]&&
    inventories==1&&inventory.value("wrapper","")=="inputHostOneXResolveSelectedChildren"&&
    !inventory.value("leafPublishesSelected",true)&&
    inventory["selectedAtLeafReturn"]["identity"]==resourcesBefore["canonicalSelected"]["physicalIdentity"]&&
    inventory["children"][0]["identity"]==resourcesBefore["journal"]["physical"]["physicalIdentity"],
    "alignment negative consumes the distinct uncollapsed child inventory without publishing the returned descriptor");
   if(variant==1)require(inventory.value("complete",false)&&inventory.value("count",0u)==2&&inventory["children"].size()==2&&
    inventory["children"][0]["root"]==inventory["children"][1]["root"]&&
    inventory["children"][0]["hid"]!=inventory["children"][1]["hid"],
    "same-root two-live-child negative preserves both distinct current children before any root collapse");
   else require(!inventory.value("complete",true)&&inventory.value("count",0u)==1,
    "incomplete same-root inventory remains unknown despite returning the exact owned descriptor");
   const auto evidence=refusal.value("selectedCacheAlignment",json::object());
   require(result==-1&&aligned==0&&refusal.value("stage","")=="failed"&&
    evidence.value("reason","")=="current-selected-child-inventory-unproven"&&evidence.value("sameRoot",false)&&
    !evidence.value("published",true)&&evidence["inventoryComplete"]==inventory["complete"]&&evidence["inventoryCount"]==inventory["count"]&&
    !refusal.value("driverOperationMayHaveIssued",true),
    "production AlignSelectedBeforeCycle rejects ambiguous or incomplete inventory before any possible cycle");
   require(observation["sourceAfter"]==resourcesBefore&&hostLock.owns_lock()&&!t_inputHostOneXStartWrite&&
    fixtureOneXIssues==0&&fixtureOneXDispatchCalls==0&&fixtureOneXOwnerReads==0&&fixtureOneXRehideSelections==0&&
    !g_inputHostPrepared&&!g_inputHostPrepareMayHaveReachedHost&&!g_inputHostActive&&startProtectionClock<fixtureOneXDeadline&&
    finalCount(request.requestId,"success")==0&&finalCount(request.requestId,"failure")==0,
    "inventory refusal preserves stale selection and every captured owner resource without cycle target or fabricated final");
   observation["passed"]=true;continue;
  }
  if(fixtureOneXInitialForeign){
   // Store all observations before enforcing the boundary: a production bug
   // must leave its original failing result, never a manufactured optional skip.
   unsigned bound=0;bool initialBinding=false,preDispatchBinding=false;json finalGuard=nullptr;
   for(const auto& io:fixtureOneXIo){
    if(io.value("leaf","")=="reverse-binding"){
     if(fixtureOneXForeignSlot==1){
     require(io.value("bound",false)&&io.value("slot",4u)==1&&io.value("physicalIdentity","")==journal.physical.physicalIdentity,
      "initial foreign B really reverse-binds slot 1; failed binding cannot satisfy this negative");++bound;
     }else{require(io.value("bound",false)&&io.value("slot",4u)==0&&io.value("physicalIdentity","")==journal.physical.physicalIdentity,
      "same-slot foreign B really reverse-binds slot 0; reused numeric slot cannot substitute selected physical identity");++bound;}
     initialBinding|=io.value("stage","")=="initial-root-slot-proof";preDispatchBinding|=io.value("stage","")=="pre-dispatch-root-slot-proof";
    }else if(io.value("leaf","")=="cycle-dispatch")finalGuard=io;
   }
   observation["successfulReverseBindings"]=bound;observation["finalGuard"]=finalGuard;
   require(hostLock.owns_lock()&&!t_inputHostOneXStartWrite,"initial foreign helper restores its TLS scope and original Host lock");
   require(resourcesBefore["canonicalSelected"]["root"]!=resourcesBefore["journal"]["physical"]["root"]&&
    resourcesBefore["sourceLocked"]==true&&resourcesBefore["primarySlot"]==0&&resourcesBefore["sourceMask"]==1&&
    journal.physicalSources.size()==1&&journal.hiddenByUs.size()==2,"initial canonical selected A stays locked while the sole owned hidden journal is B");
   require(bound==2&&initialBinding&&preDispatchBinding&&fixtureOneXVisibilityReads==1&&fixtureOneXDispatchCalls==1&&fixtureOneXMayIssueCalls==1&&!finalGuard.is_null(),
    "bound foreign B passes initial and pre-dispatch source proofs and reaches the actual production final MayIssue");
   if(fixtureOneXForeignSlot==1){
   require(finalGuard.value("startCurrentBeforeMayIssue",false)&&finalGuard.value("candidateSlot",4u)==1&&finalGuard.value("primarySlot",4u)==0&&
    finalGuard.value("requestId",0ULL)==request.requestId&&finalGuard.value("episodeDeadline",0ULL)==fixtureOneXDeadline,
    "initial foreign refusal reaches final MayIssue under the unchanged current ticket and A/B slot mismatch");
   }else require(finalGuard.value("startCurrentBeforeMayIssue",false)&&finalGuard.value("candidateSlot",4u)==0&&finalGuard.value("primarySlot",4u)==0&&
    finalGuard.value("requestId",0ULL)==request.requestId&&finalGuard.value("episodeDeadline",0ULL)==fixtureOneXDeadline,
    "same-slot foreign refusal reaches final MayIssue with a current ticket; only actual physical identity distinguishes A and B");
   require(!finalGuard.value("admitted",true)&&!finalGuard.value("issueReserved",true)&&fixtureOneXIssues==0&&!fixtureOneXReplayAdmitted,
    "production StartMayIssue refuses initial foreign B before any possible dispatch");
   require(result==-1&&!observation["refreshEvents"].empty()&&
    observation["refreshEvents"].back().value("reason","")=="refresh-unissued-context-retired",
    "initial foreign helper preserves the unissued refusal instead of an unbound-slot optional skip");
   for(const auto& detail:observation["refreshEvents"])require(!detail.value("driverOperationMayHaveIssued",true)&&
    !detail.value("filterAttachmentProven",true)&&!detail.value("externalSuppressionVerified",true)&&detail.value("stage","")!="completed",
    "initial foreign source refusal cannot manufacture issue, refresh completion, filter or suppression proof");
   require(observation["sourceAfter"]==resourcesBefore&&!g_inputHostPrepared&&!g_inputHostPrepareMayHaveReachedHost&&!g_inputHostActive&&
    fixtureOneXOwnerReads==0&&fixtureOneXRehideSelections==0,"initial foreign refusal leaves A selection, B journal and all captured resources unchanged without target, owner read or rehide");
   require(finalCount(request.requestId,"success")==0&&finalCount(request.requestId,"failure")==0,
    "initial foreign helper cannot manufacture a lifecycle final");
   observation["passed"]=true;continue;
  }
  require(hostLock.owns_lock()&&!t_inputHostOneXStartWrite,"actual helper restores its TLS scope and original Host lock");
  require(result==test.result,"actual OneX helper returns the declared optional-skip or failure outcome");
  const unsigned expectedDispatches=test.issues||fixtureOneXShape.find("-at-write")!=std::string::npos?1:0;
  require(fixtureOneXIssues==test.issues&&fixtureOneXDispatchCalls==expectedDispatches&&!fixtureOneXReplayAdmitted,"actual final MayIssue spends at most once and executes the exact declared dispatch boundary");
  for(const auto& io:fixtureOneXIo)if(io.value("leaf","")=="cycle-dispatch")
   require(io.value("requestId",0ULL)==request.requestId&&io.value("episodeDeadline",0ULL)==fixtureOneXDeadline,"dispatch keeps the exact original ticket and absolute deadline");
  if(fixtureOneXShape=="ticket-at-write"){
   const auto newer=g_virtualStartProtection.view(startProtectionClock);
   require(newer.requestId!=request.requestId&&newer.locked&&newer.remainingMs==30000,"old helper cannot consume or age a newer admitted ticket");
  }
  require(!g_inputHostPrepared&&!g_inputHostPrepareMayHaveReachedHost&&!g_inputHostActive,"direct helper never creates or publishes a target");
  require(finalCount(request.requestId,"success")==0&&finalCount(request.requestId,"failure")==0,"direct helper proof cannot manufacture a lifecycle final");
  bool completed=false;json refresh=json::array();
  for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-onex-start-refresh"){
   const auto& detail=event["detail"];refresh.push_back(detail);completed|=detail.value("stage","")=="completed";
   require(!detail.value("filterAttachmentProven",true)&&!detail.value("externalSuppressionVerified",true),"unknown filter attachment or external suppression is never reported as proven");
  }
  require(!refresh.empty()&&completed==(test.result==1),"only the actual fresh-source positive may produce one completed refresh");
  if(fixtureOneXFreshMode())require(fixtureOneXOwnerReads>0&&fixtureOneXReturnedGuardProven,"fresh cases reach actual canonical owner and real ReturnedSourceCurrent");
  observation["oneXReadinessObservation"]={{"ownerReads",fixtureOneXOwnerReads},{"rehideSelections",fixtureOneXRehideSelections},
   {"returnedGuardProven",fixtureOneXReturnedGuardProven},{"externalSourceRejected",fixtureOneXExternalSourceRejected},
   {"externalTargetRejected",fixtureOneXExternalTargetRejected}};
  if(fixtureOneXShape=="rehide-switch-source")require(fixtureOneXRehideSelections==1&&fixtureOneXExternalSourceRejected&&fixtureOneXExternalTargetRejected,"source and actual hide-target hooks reject external B before any command");
  if(fixtureOneXShape=="rehide-context-changed")require(fixtureOneXVisibilityReads==3,"context negative reaches the readback after actual rehide");
  const char* reason="hidden-configuration-unproven";
  if(fixtureOneXShape=="initial-context-expired")reason="original-execution-not-current";
  else if(fixtureOneXShape=="already-hidden-hid"||fixtureOneXShape=="already-hidden-root")reason="already-hidden-source-no-new-refresh";
  else if(fixtureOneXShape=="unknown-root")reason="selected-source-location-unproven";
  else if(fixtureOneXShape=="nonstandard-source"||fixtureOneXShape=="multiple-roots")reason="unique-standard-game-root-not-proven";
  else if(fixtureOneXShape=="unbound-slot")reason="selected-source-root-slot-unproven";
  else if(fixtureOneXShape=="locations-changed")reason="root-location-or-source-changed-before-dispatch";
  else if(fixtureOneXShape=="fresh-positive"||fixtureOneXShape=="owned-prehidden"||fixtureOneXShape=="external"||fixtureOneXShape=="unknown-port")reason="fresh-same-root-source-before-target";
  else if(fixtureOneXShape=="rehide-switch-source"||fixtureOneXShape=="rehide-context-changed"||fixtureOneXShape=="source-missing")reason="same-root-source-return-unproven";
  else if(test.issues)reason="refresh-result-unproven-no-reissue";
  else if(expectedDispatches)reason="refresh-unissued-context-retired";
  require(refresh.back().value("reason","")==reason&&refresh.back().value("driverOperationMayHaveIssued",false)==(test.issues!=0),"OneX negative reaches its exact production refusal and actual issue reservation rather than an unrelated early skip");
  if(fixtureOneXShape=="already-hidden-hid"||fixtureOneXShape=="already-hidden-root")
   require(journal.hiddenByUs==ownedBefore&&!completed&&fixtureOneXIssues==0,"already-hidden optional skip adds no foreign path or new return responsibility");
  if(fixtureOneXShape=="owned-prehidden")require(completed&&fixtureOneXIssues==1,
   "OneX093203 own carried hidden pair does not masquerade as a third-party prehidden skip; original one-cycle and fresh-source guards execute");
   if(fixtureOneXShape=="external"||fixtureOneXShape=="unknown-port"){
    require(completed&&fixtureOneXIssues==1&&fixtureOneXOwnerReads>0,
     "OneX112441 HC selected-owned source refresh survives removable/unknown connector metadata without reclassifying the port or bypassing a fresh read");
    observation["connectorMetadataOnly"]=true;observation["portClassificationIsRefreshAuthority"]=false;
   }
  observation["freshSourceReturnProven"]=test.result==1;observation["actualOwnerReads"]=fixtureOneXOwnerReads;
  observation["helperResult"]=result;observation["issuedRefreshes"]=fixtureOneXIssues;
  observation["dispatchCalls"]=fixtureOneXDispatchCalls;observation["io"]=fixtureOneXIo;
  observation["refreshEvents"]=refresh;observation["originalRequestId"]=request.requestId;
  observation["originalDeadline"]=fixtureOneXDeadline;observation["controlledDmi"]={{"product",g_machineIdentity.product},{"resolver","ymccResolveFamily"},{"deviceClass",g_machineIdentity.deviceClass}};
  observation["passed"]=true;
  if(unboundParameterized){
   require(observation["sourceAfter"]==resourcesBefore,"original unbound-slot skip leaves its independently recorded resources unchanged");
   auto originalReceipt=observation;originalReceipt.erase("passed");
   cases.back().update(originalReceipt); // Keep the parent pending until all variants finish.
  }else if(inventoryParameterized&&!variant){
   auto originalReceipt=observation;originalReceipt.erase("passed");cases.back().update(originalReceipt);
  }
  }
  if(parameterized)require(cases.back()["subvariants"].size()==variantCount&&
   std::all_of(cases.back()["subvariants"].begin(),cases.back()["subvariants"].end(),[](const auto& row){return row.value("passed",false);}),
   "original helper passes only after every source-declared independent parameter passes");
  cases.back()["passed"]=true;
 }
 fixtureOneXEnabled=false;
 require(cases.size()-begin==sizeof(oneXHelperCases)/sizeof(oneXHelperCases[0]),"direct OneX selection executes exactly its declared canonical scope");
}
} // namespace personaLifecycle140

static void familyLifecycleCases140(){
 if(personaLifecycle140::oneXFamilyRequested()){personaLifecycle140::oneXFamilyHelperCases140();personaLifecycle140::oneXManagedLifecycleCases140();return;}
 using namespace personaLifecycle140;
 if(!fspath::is_regular_file(fixtureManagedDll))throw std::runtime_error("explicit managed fixture missing");
 // Original support-declaration leaves remain unbound; Lenovo below resolves DMI.
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
 // Seven HC SystemModel keys, four registered concrete classes, three existing
 // personas. Every row reuses first/actual Start/fresh source/Host pipe/final/OFF.
 static const ControlledDmi lenovoDmi[]={
 {"LENOVO","LNVNB161216","HandheldCompanion.Devices.LegionGoTablet",nullptr,"83E1","hc.device.LegionGoTablet"},
 {"LENOVO","LNVNB161216","HandheldCompanion.Devices.LegionGoTablet2",nullptr,"83N0","hc.device.LegionGoTablet2"},
 {"LENOVO","LNVNB161216","HandheldCompanion.Devices.LegionGoTablet2",nullptr,"83N1","hc.device.LegionGoTablet2"},
 {"LENOVO","LNVNB161216","HandheldCompanion.Devices.LegionGoSZ2",nullptr,"83L3","hc.device.LegionGoSZ2"},
 {"LENOVO","LNVNB161216","HandheldCompanion.Devices.LegionGoSZ1",nullptr,"83N6","hc.device.LegionGoSZ1"},
 {"LENOVO","LNVNB161216","HandheldCompanion.Devices.LegionGoSZ1",nullptr,"83Q2","hc.device.LegionGoSZ1"},
 {"LENOVO","LNVNB161216","HandheldCompanion.Devices.LegionGoSZ1",nullptr,"83Q3","hc.device.LegionGoSZ1"}
 };
 for(const auto& model:lenovoDmi)for(const std::string persona:{"steamdeck","dualsense-edge","elite"}){
  first(5,persona,&model);
  require(ymccFamilyInterfacePolicy(g_machineIdentity.family)==YmccInterfacePolicy::None&&!g_msiDinputRouteDinputArmed.load(),"Lenovo standard virtual lifecycle never acquires MSI mode authority");
  require(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Unsupported&&!g_msiNativeForegroundLease.active,"Lenovo virtual output and OFF return do not grant unsupported native foreground writes");
 }
 require(cases.size()==firstCase+30,"family selector retains nine original variants and adds exactly twenty-one Lenovo variants");
 require(fullPipeChildObservers.empty()&&!g_inputHostProcess,"original and Lenovo family variants leave no owned child or active native Host");
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
 require(cases.size()==firstCase+31,"family selector retains physical Deck refusal after thirty standard virtual lifecycles");
}
