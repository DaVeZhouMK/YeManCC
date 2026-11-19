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
static const char* familyName(int family){switch(family){case 0:return "MSI";case 1:return "ROG";case 2:return "GPD";case 3:return "OneXPlayer";case 4:return "Ayanero";case 5:return "Lenovo";case 6:return "AOKZOE";default:throw std::logic_error("unknown persona fixture family");}}
struct ControlledDmi {const char* manufacturer;const char* product;const char* declaredDeviceClass;const char* supportPointer;const char* systemProduct=nullptr;const char* declaredDeviceId=nullptr;};
static void prepare(int family,const std::string& name,const ControlledDmi* dmi=nullptr,bool registerCase=true){
 if(family>=3&&family!=5)require((family<=4||family==6)&&dmi,"new family variants require declared controlled DMI leaves");
 if(family==5)require(dmi,"Lenovo family requires its declared controlled DMI leaf");
 require(fullPipeChildObservers.empty()&&!g_inputHostProcess,"new independent lifecycle case starts with no residual owned child");
 fixtureMsiOwnedMetadata=false;fixtureMsiRuntimeProofEdges={};
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
 if((family==3||family==6)&&dmi&&dmi->systemProduct){
  g_machineIdentity.systemProduct=dmi->systemProduct;ymccResolveFamily(g_machineIdentity);
  require(g_machineIdentity.resolved&&g_machineIdentity.family==(family==6?YmccFamily::Aokzoe:YmccFamily::OneXPlayer)&&g_machineIdentity.deviceClass==dmi->declaredDeviceClass,"actual directed family DMI resolves its concrete class");
  const auto binding=ymcc::capabilities::onexVirtual::bind(g_machineIdentity.deviceClass);
  require(binding.known()&&ymcc::capabilities::onexVirtual::recognized(binding,g_machineIdentity.deviceClass),"directed OneX class binds the actual legacy or reviewed capability");
  require(inputHostOneXMachineEligible(),"resolved family agrees with its exact immutable capability");
   require(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Unsupported&&!g_msiNativeForegroundLease.active&&!g_msiDinputRouteDinputArmed.load(),"shared ordinary Start grants no OEM native-OFF isolation or MSI mode authority");
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
 if(fixtureOneXEnabled){cases.back()["familySelection"]=family==6?"aokzoe":"onex";cases.back()["suite"]="family-lifecycle";cases.back()["managedCommandPath"]=true;
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
static json receipt(HANDLE child,const std::wstring& file,const std::string& persona,unsigned expectedCreates=1,bool allowClassifiedReadFailure=false){
 const auto result=fullPipeChildOutcome(child,file);
 const auto terminal=result.value("terminalSettlement",json::object());
 const bool expectedExit=result.value("nativeObservedExitCode",999)==0 ||
  (allowClassifiedReadFailure && result.value("nativeObservedExitCode",999)==5 &&
   terminal.value("Reason",std::string{})=="read-failed" && terminal.value("ManagedReleaseComplete",false));
 require(result.value("nativeObservedProcessExit",false)&&expectedExit&&result.value("finalCleanupOk",false)&&!result.value("hasController",true)&&!result.value("hasContext",true),"exact old managed child controller and context release naturally before the next create");
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
static void close(int family,const std::string& persona,unsigned expectedCreates=1,bool allowClassifiedReadFailure=false){
 const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE)),off=targetDocument("disabled",current["input"]["revision"].get<int>()+1);const auto spawns=fullPipeSpawnCount;
 const auto child=fullPipeChildObservers.back();const auto file=fullPipeReceipts.back();require(writeTarget(current,off).value("ok",false),"real settings IPC admits OFF only after persona success final");inputCaptureRefreshRealStickSettings(true);const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
 fullPipeRejectLockedPlayerWrite(off,targetDocument(persona,off["input"]["revision"].get<int>()+1));
 require(!inputHostStart()&&fullPipeSpawnCount==spawns,"unfinished OFF cannot create or reopen a Host");
 inputHostStop("virtual-target-disabled");const auto result=receipt(child,file,persona,expectedCreates,allowClassifiedReadFailure);
 if(family==0&&g_msiDinputReturnOwed.load())fullPipeMsiObserveReleasedPhysicalReturn(result,"persona140-normal-OFF-released-local-interface");
 fullPipeDrainQueuedLifecycle();
 require(!g_inputHostProcess&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active,"actual OFF has released the exact old Host target and visibility ownership");
 if(family==1)fullPipeRogObserveReturnedSource(ticket,true);
 auto candidate=g_virtualStartProtection.view(startProtectionClock);require(candidate.requestId==ticket&&candidate.persona=="disabled"&&candidate.phase=="verifying"&&candidate.locked,"actual OFF reports only its closing candidate before common verification");
 if(candidate.verificationRemainingMs){advance(candidate.verificationRemainingMs-1);fullPipeVerificationStep();require(g_virtualStartProtection.view(startProtectionClock).locked&&finalCount(ticket,"success")==0,"closing result remains protected until its original inspection completes");advance(1);}
 if(family==1)(void)fresh(1);
 fullPipeVerificationStep();fullPipeVerificationStep();const auto done=g_virtualStartProtection.view(startProtectionClock);
 require(done.phase=="success"&&!done.locked&&done.requestId==ticket&&finalCount(ticket,"success")==1&&finalCount(ticket,"failure")==0,"real OFF inspection publishes exactly one successful closing result");
 fullPipeDrainQueuedLifecycle();require(fullPipeSpawnCount==spawns&&fullPipeChildObservers.empty()&&!g_inputHostProcess,"queued late work cannot resurrect OFF and no fixture child remains");
 cases.back()["periods"].push_back({{"requestId",ticket},{"persona","disabled"},{"outcome",done.phase},{"finalCount",finalCount(ticket,"success")}});
}
#ifdef YMCC_FIXTURE_OEM_AFTER_OFF
static LRESULT CALLBACK oemPrivateWindow(HWND window,UINT message,WPARAM w,LPARAM l){
 if(message==WM_OEM_SHORTCUT_EDGE){nativeKeyboardShortcutHandleOemEdge(w,l);return 0;}
 if(message==WM_OEM_HOOK_RECONCILE){oemBackMappingReconcileOnUi();return 0;}
 return DefWindowProcW(window,message,w,l);
}
static void oemAfterOffCases(){
 struct Input {int family;const char* name;ControlledDmi dmi;const char* key;};
 const Input inputs[]={
  {4,"OEM-AFTER-OFF/AYA-AIRPro",{"AYANEO","AIR PRO","HandheldCompanion.Devices.AYANEOAIRPro","/ayaAirProOemField127"},"ckbig"},
  {2,"OEM-AFTER-OFF/GPD-Win5",{"GPD","G1618-05","HandheldCompanion.Devices.GPDWin5","/physicalModels/0"},"l4"}};
 for(const auto& input:inputs){
  require(!g_gamepadSerialThread.joinable(),"prior OEM scenario returned its actual serial worker");
  {std::lock_guard<std::mutex> lock(g_gamepadSerialMx);g_gamepadSerialStopping=false;}
  prepare(input.family,input.name,&input.dmi);ymccResolveFamily(g_machineIdentity);
  require(g_machineIdentity.resolved&&g_machineIdentity.deviceClass==input.dmi.declaredDeviceClass,"controlled DMI uses actual total-library identity resolution");
  cases.back()["resolvedDeviceClass"]=g_machineIdentity.deviceClass;
  struct ReturnOwner {
   ~ReturnOwner(){
    try{if(g_inputHostProcess)inputHostStop("offline-owned-cleanup");}catch(...){}
    try{gamepadSerialStop(2000);}catch(...){}
    g_exitRequested=true;try{oemBackMappingReconcileOnUi();}catch(...){}
    if(g_hwnd){DestroyWindow(g_hwnd);g_hwnd=nullptr;}
   }
  } returnOwner;
  const auto journalRequests=fixtureOemJournalMutexRequests.load();
  const auto spawns=fullPipeSpawnCount;startFirst(input.family,"elite");close(input.family,"elite");
  require(fullPipeSpawnCount==spawns+1&&fullPipeChildObservers.empty()&&!g_inputHostProcess&&!g_inputHostPrepared,
   "real native Start and OFF returned the private managed child before fresh OEM input");
  fixtureOemKeyboardActions=fixtureOemDesktopActions=fixtureOemActionTid=0;fixtureOemHookInstalls=fixtureOemHookRemoves=0;
  const auto title=L"YMCC.OemAfterOff."+std::to_wstring(GetCurrentProcessId());
  WNDCLASSW cls{};cls.lpfnWndProc=oemPrivateWindow;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=title.c_str();
  const auto atom=RegisterClassW(&cls);require(atom||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"private message-only OEM class exists");
  g_hwnd=CreateWindowExW(0,title.c_str(),L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
  require(g_hwnd!=nullptr,"fresh OEM events use a private message-only window");
  // The keyboard source's interface readiness is an explicit transport leaf,
  // not a hardware/firmware query. Matcher, gate, queue and release remain real.
  if(input.family==2){g_gpdBackIfaceReady=true;g_gpdBackIfaceGen.fetch_add(1);}
  const auto& binding=oemUnifiedInputBindingFor(g_machineIdentity.family,g_machineIdentity.deviceClass);
  const auto& profile=oemChordProfileFor(binding.profileId);uint64_t mask=0;
  require(nativeKeyboardShortcutMaskBit(input.key,mask),"existing OEM catalog ID resolves without a new mapping");
  const OemChordRule* chord=nullptr;
  for(int i=0;i<profile.count;++i)if(profile.rules[i].bit==mask&&!profile.rules[i].mouseChord){chord=&profile.rules[i];break;}
  require(chord!=nullptr,"source chord comes from the actual model-local profile");
  const auto settle=[&](){
   std::promise<void> done;auto ready=done.get_future();
   require(gamepadSerialSubmit([&]{done.set_value();}),"actual serial observer is admitted after the action");
   require(ready.wait_for(std::chrono::seconds(2))==std::future_status::ready,"actual serial action and observer return within the existing bound");
  };
  const auto pump=[&](){MSG message{};unsigned count=0;
   while(PeekMessageW(&message,g_hwnd,0,0,PM_REMOVE)){require(++count<=64,"private OEM message queue is bounded");DispatchMessageW(&message);}
   settle();
  };
  const auto bind=[&](const char* action){
   const auto before=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));auto next=before;
   next["input"]["revision"]=before["input"]["revision"].get<int>()+1;
   next["input"]["buttonMapping"]["rules"]["shortcutRules"]=json::array({{{"id","oem-after-off"},{"enabled",true},
    {"actionId",action},{"params",json::object()},{"inputs",json::array({{{"source","oem"},{"code",input.key}}})},
    {"trigger","press"},{"holdMs",500},{"doubleWindowMs",500},{"intervalMs",150}}});
   require(writeTarget(before,next).value("ok",false),"ordinary settings IPC publishes the native OEM action rule after real OFF");
   inputCaptureRefreshRealStickSettings(true);Sleep(NATIVE_KEYBOARD_SHORTCUT_REFRESH_MS+1);
   nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},GetTickCount64());pump();
   require(GamepadSerialEvent::RulesApplied.load()==GamepadSerialEvent::RulesRequired.load(),"actual refresh consumes the published rules generation");
  };
  const auto pulse=[&](bool injected){
   uint32_t remaining=chord->keys;std::vector<DWORD> keys;unsigned consumed=0;
   for(DWORD vk=1;vk<256;++vk){const auto bit=oemBackHookVkBit(vk);if(bit&&(remaining&bit)){keys.push_back(vk);remaining&=~bit;}}
   require(!remaining&&!keys.empty(),"OEM source uses current VK decoding");
   for(const auto vk:keys){KBDLLHOOKSTRUCT key{};key.vkCode=vk;key.flags=injected?LLKHF_INJECTED:0;consumed+=oemBackHookProc(HC_ACTION,WM_KEYDOWN,reinterpret_cast<LPARAM>(&key))!=0;}
   for(auto it=keys.rbegin();it!=keys.rend();++it){KBDLLHOOKSTRUCT key{};key.vkCode=*it;key.flags=LLKHF_UP|(injected?LLKHF_INJECTED:0);consumed+=oemBackHookProc(HC_ACTION,WM_KEYUP,reinterpret_cast<LPARAM>(&key))!=0;}
   if(!injected)require(consumed==2*keys.size()&&!g_oemKeyboardConsume.replayCount&&!oemKeyboardConsumeBusy(),"fresh native OEM down/up pair is consumed and released without replay");
   else require(consumed==0,"injected source is not promoted to physical OEM input");
  };
  bind("os.openTouchKeyboard");pulse(false);pump();
  require(fixtureOemKeyboardActions==1&&fixtureOemActionTid!=GetCurrentThreadId(),"fresh post-OFF OEM input executes the configured action on the real serial worker");
  pulse(true);pump();require(fixtureOemKeyboardActions==1,"injected post-OFF sequence executes no extra action");
  pulse(false);g_oemShortcutEdgeEpoch.fetch_add(1);pump();
  require(fixtureOemKeyboardActions==1,"actual retained delivery rejects a retired source epoch after OFF");
  nativeKeyboardShortcutsEvaluate(0,XINPUT_GAMEPAD{},GetTickCount64());pulse(false);pump();
  require(fixtureOemKeyboardActions==2,"next fresh native source remains usable after stale delivery was refused");
  bind("os.returnDesktop");pulse(false);pump();
  require(fixtureOemDesktopActions==1,"actual external-injection action route also reaches its controlled OS leaf without Host");
  require(fullPipeSpawnCount==spawns+1&&!g_inputHostProcess&&!g_inputHostPrepared&&!g_realStickTest.virtualTargetEnabled,
   "OEM actions create no replacement virtual Host or output intent");
  require(gamepadSerialStop(2000)&&!g_gamepadSerialThread.joinable(),"actual OEM action worker is joined before fixture completion");
  g_exitRequested=true;oemBackMappingReconcileOnUi();
  require(!g_oemBackHook&&!g_oemMouseHook&&fixtureOemHookInstalls==2&&fixtureOemHookRemoves==2&&!oemKeyboardConsumeBusy(),
   "actual hook owner returns both controlled hook handles and paired-key ledger");
  DestroyWindow(g_hwnd);g_hwnd=nullptr;
  require(fixtureOemJournalMutexRequests.load()>journalRequests,"actual model journal uses only its private named mutex boundary");
  cases.back()["postOffOem"]={{"keyboardActions",fixtureOemKeyboardActions.load()},{"desktopActions",fixtureOemDesktopActions.load()},
   {"privateModelJournalMutexRequests",fixtureOemJournalMutexRequests.load()-journalRequests},
   {"realSerialWorkerJoined",true},{"controlledHookHandlesReturned",true},{"realManagedOffBeforeInput",true},
   {"physicalInputAndOSActionEffectVerified",false},{"gpdInterfaceReadinessControlled",input.family==2}};
  cases.back()["passed"]=true;fixtureFullPipe=false;
 }
}
#endif
static void first(int family,const std::string& persona,const ControlledDmi* dmi=nullptr){prepare(family,std::string(familyName(family))+(family==5&&dmi&&dmi->systemProduct?std::string("/")+dmi->systemProduct:std::string{})+"/persona-first/"+persona,dmi);const auto spawns=fullPipeSpawnCount;startFirst(family,persona);close(family,persona);require(fullPipeSpawnCount==spawns+1,"first-ON-to-OFF lifecycle has exactly one Host without fallback replacement");cases.back()["passed"]=true;fixtureFullPipe=false;}
// Explicit parameters only: the original ROG six directions and family suite
// retain their existing selections. The JS selector reads this same table.
static constexpr const char* gpdLiveSdTransferPersonas[]={"dualsense-edge","elite"};
static void observeGpdLiveSdNativeWork(){
 require(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Gpd &&
  g_inputHostProcess&&g_inputHostActive&&g_hidHideVisibilityJournal.active&&
  inputHostBoundPersonaValue()=="steamdeck"&&!g_msiNativeForegroundLease.active&&
  !g_postHideCycleOwned.load()&&!inputHostNativeVisibilityHistoryPending()&&
  !ymcc::inputHost::switchExecutionReturnRuntime::state.pending&&!g_inputHostSwitchExecutionReturnPending,
  "GPD live SD observation begins with actual live ownership and no historical native duty");
 const auto host=g_inputHostProcess;const auto epoch=g_inputHostEpoch;
 const auto request=g_virtualStartProtection.view(startProtectionClock);
 unsigned reads=0,writes=0,refreshes=0,queues=0;
 // Only foreground/physical effect leaves are local. In particular coldReturn
 // stays EMPTY: the production cold entry/current guard must decide itself.
 // Restore all effects before the serialized persona worker, including on FAIL.
 struct Effects {
  MsiNativeForegroundTestIo saved=g_msiNativeForegroundTestIo;
  bool observer=g_msiNativeForegroundObserverActive.exchange(true);
  bool isolation=g_controllerFrontendIsolationEnabled.exchange(true);
  ~Effects(){g_msiNativeForegroundTestIo=std::move(saved);
   g_msiNativeForegroundObserverActive=observer;g_controllerFrontendIsolationEnabled=isolation;}
 };
 const auto persistenceBefore=g_msiNativeForegroundTestIo.nativeVisibilityPersistence;
 const bool coldOverrideBefore=static_cast<bool>(g_msiNativeForegroundTestIo.coldReturn);
 {
  Effects effects;g_msiNativeForegroundTestIo={};auto& io=g_msiNativeForegroundTestIo;
  io.nativeVisibilityPersistence=true;io.ui=[](){return true;};
  io.snapshot=[&](HidHideVisibilitySnapshot&){++reads;return false;};
  io.candidates=[&](){++reads;return std::vector<HidHidePhysicalIdentity>{};};
  io.inventory=[&](const HidHidePhysicalIdentity&){++reads;return std::vector<HidHideInterfaceRecord>{};};
  io.xusbInventory=[&](const HidHidePhysicalIdentity&){++reads;return MsiNativeForegroundXusbInventory{};};
  io.inventoryComplete=[&](){++reads;return false;};
  io.visibility=[&](const std::wstring&,bool){++writes;return false;};
  io.cloak=[&](bool){++writes;return false;};
  io.durableHide=[&](const auto&,const auto&,const auto&){++writes;return false;};
  io.durableReturn=[&](const auto&){++writes;};
  io.readerExemption=[&](){++writes;return false;};
  io.readerInverseOff=[&](){++reads;return false;};
  io.cycle=[&](const auto&,const auto&,bool){++refreshes;return 2;};
  io.gameRefresh=[&](const auto&,const auto&){++refreshes;return 2;};
  io.xusbRefresh=[&](const auto&,const auto&){++refreshes;return 2;};
  io.poll=[&](){++refreshes;return -1;};
  io.queue=[&](){++queues;};io.virtualQueue=[&](const auto&){++queues;};
  {
   std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto facts=msiNativeForegroundFacts();
   require(facts.knownBoard&&facts.virtualEnabled&&!facts.nativeChoice&&facts.uiOwned&&
    facts.powerReady&&!facts.hostAbsent&&!facts.exitFence&&io.nativeVisibilityPersistence&&!io.coldReturn,
    "GPD live SD reaches actual foreground facts with persistence enabled and no cold override");
  }
  msiNativeForegroundHandleWork();
  const bool nativePending=inputHostNativeVisibilityHistoryPending();
  const bool coldPending=ymcc::inputHost::switchExecutionReturnRuntime::state.pending||g_inputHostSwitchExecutionReturnPending;
  cases.back()["nativeUiObservation"]={{"handleWorkCalls",1},{"nativeVisibilityPersistence",true},
   {"coldReturnOverride",false},{"deviceReads",reads},{"deviceWrites",writes},{"refreshes",refreshes},{"queues",queues},
   {"nativePending",nativePending},{"coldPending",coldPending},
   {"boundary","actual HandleWork/cold admission; only UI fact and fail-closed physical effect leaves; no SDK/device or cold cleanup acceptance"}};
  require(reads+writes+refreshes+queues==0,"GPD live SD foreground work performs zero device IO or replacement queue effects");
  require(!nativePending&&!coldPending&&g_inputHostProcess==host&&g_inputHostEpoch==epoch&&g_inputHostActive&&
   g_hidHideVisibilityJournal.active&&g_virtualStartProtection.view(startProtectionClock).requestId==request.requestId&&
   g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,
   "GPD live SD foreground work cannot manufacture native pending or cold continuation");
 }
 require(g_msiNativeForegroundTestIo.nativeVisibilityPersistence==persistenceBefore&&
  static_cast<bool>(g_msiNativeForegroundTestIo.coldReturn)==coldOverrideBefore,
  "GPD live SD local effects are restored before serialized persona reconciliation");
 cases.back()["nativeUiObservation"]["effectsRestoredBeforeDispatch"]=true;
}
static void transfer(const std::string& from,const std::string& to,int family=1,const ControlledDmi* dmi=nullptr){
 const bool serialNativeUi=family==2;
 if(family!=1)require(serialNativeUi&&dmi&&from=="steamdeck"&&
  std::any_of(std::begin(gpdLiveSdTransferPersonas),std::end(gpdLiveSdTransferPersonas),[&](const auto* value){return to==value;}),
  "directed native UI transfer uses only the two declared GPD SD successors");
 prepare(family,std::string(familyName(family))+"/persona-transfer/"+from+"-to-"+to,dmi);
 if(serialNativeUi){
  ymccResolveFamily(g_machineIdentity);
  require(g_machineIdentity.resolved&&g_machineIdentity.family==YmccFamily::Gpd&&
   g_machineIdentity.deviceClass==dmi->declaredDeviceClass&&nativeForegroundProfile()==ymcc::nativeForeground::Profile::Gpd,
   "directed GPD transfer resolves its actual DMI class and native foreground admission");
  cases.back()["controlledDmi"]["runtimeDeviceClass"]=g_machineIdentity.deviceClass;
  cases.back()["controlledDmi"]["boundary"]="controlled DMI leaf; actual resolver/native profile; no hardware acceptance";
 }
 const auto caseSpawns=fullPipeSpawnCount;startFirst(family,from);
 if(serialNativeUi)observeGpdLiveSdNativeWork();
 auto oldSource=fresh(family);oldSource.pad.wButtons=XINPUT_GAMEPAD_B;oldSource.pad.sThumbLX=29000;
 const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE)),next=targetDocument(to,current["input"]["revision"].get<int>()+1);const auto oldChild=fullPipeChildObservers.back();const auto oldReceipt=fullPipeReceipts.back();const auto oldPid=GetProcessId(oldChild);const auto oldEpoch=g_inputHostEpoch;
 require(writeTarget(current,next).value("ok",false),"real settings IPC admits a different persona only after original success");
 // The BUS can complete an old Host report after settings admission but before
 // the new runtime intent is reconciled. That receipt belongs to the OLD persona.
 const auto admitted=g_virtualStartProtection.view(startProtectionClock);
 auto gapSource=fresh(family);const auto gapFrame=fullPipeSubmitSource(gapSource,true,false);
 require(g_virtualStartProtection.view(startProtectionClock).requestId==admitted.requestId && g_virtualStartProtection.view(startProtectionClock).phase=="pending",
     "old persona ACK in the settings-to-runtime gap cannot start new persona inspection");
 cases.back()["admissionGap"]={{"requestId",admitted.requestId},{"boundBeforeReconcile",inputHostBoundPersonaValue()},{"frame",gapFrame}};
 inputCaptureRefreshRealStickSettings(true);const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
 const auto late=fullPipeSubmitSource(oldSource,true,false);require(late.value("hostSubmission",std::string{})!="frame-accepted"||late.value("focusIsolationSemanticNeutral",false),"changed intent cannot replay cached old buttons through the retiring persona");
 json released;
 if(serialNativeUi){
  const auto eventBegin=fixtureEvents.size();
  bool reread=false;
  if(to=="elite")fixtureGpdTransferSourceAtEnsure=[&](){
   require(!g_inputHostProcess&&!g_inputHostPrepared,"Elite controlled source read starts only after old Host retirement");
   cases.back()["sourceDiscoveries"].push_back({{"persona",to},{"afterOldHostRetired",true},
    {"discovery",fullPipeReadPhysicalSourceForElite(false,true)}});reread=true;
  };
  require(inputHostQueueLifecycleWork("persona-transition"),"GPD transfer admits its original request to the actual serialized worker");
  fullPipeDrainQueuedLifecycle();
  require(to!="elite"||reread,"Elite consumes an actual controlled physical read before source-required creation");
  require(g_inputHostProcess&&g_inputHostProcess!=oldChild&&inputHostBoundPersonaValue()==to&&
   fullPipeSpawnCount==caseSpawns+2&&WaitForSingleObject(oldChild,0)==WAIT_OBJECT_0,
   "GPD serialized transfer observes the old child exited and exactly one replacement Host");
  released=receipt(oldChild,oldReceipt,from);
  bool dispatched=false;auto stopEvent=fixtureEvents.size(),createEvent=fixtureEvents.size();
  for(auto i=eventBegin;i<fixtureEvents.size();++i){const auto& event=fixtureEvents[i];
   if(event.value("event","")=="input-host-lifecycle-work"&&event["detail"].value("action","")=="start-or-persona-reconcile"&&
    event["detail"].value("reason","")=="persona-transition")dispatched=true;
   if(event.value("event","")=="input-host-stop-result"&&event["detail"].value("epoch",uint64_t{})==oldEpoch&&
    event["detail"].value("hostPid",DWORD{})==oldPid&&event["detail"].value("hostExitConfirmed",false))stopEvent=i;
   if(event.value("event","")=="input-host-primary-thread-retirement"&&event["detail"].value("phase","")=="created"&&
    event["detail"].value("epoch",uint64_t{})==g_inputHostEpoch)createEvent=i;
  }
  // The real-process port returns before the old synthetic create-process
  // trace. Observe actual Stop/creation receipts for the exact old/new tuples.
  require(dispatched&&stopEvent<createEvent&&createEvent<fixtureEvents.size(),
   "GPD transfer uses production BeforeVirtualWork dispatcher with Stop ordered before new creation");
  require(GetProcessId(g_inputHostProcess)!=oldPid&&g_inputHostEpoch!=oldEpoch&&
   !inputHostNativeVisibilityHistoryPending()&&!ymcc::inputHost::switchExecutionReturnRuntime::state.pending&&!g_inputHostSwitchExecutionReturnPending,
   "new persona creates exactly one new Host after old child/controller/context exit proof");
  cases.back()["serializedTransfer"]={{"queueReason","persona-transition"},{"actualDispatcher",true},
   {"beforeVirtualWorkOnProductionPath",true},{"stopBeforeCreate",true},{"oldExited",true},{"replacementHosts",1},
   {"stopEventIndex",stopEvent},{"creationEventIndex",createEvent},{"observation","actual-native-tuples-not-synthetic-create-trace"}};
 }else{
  inputHostStop("persona-transition");released=receipt(oldChild,oldReceipt,from);require(!g_inputHostProcess&&fullPipeSpawnCount==caseSpawns+1,"old managed child naturally releases before any new persona Host creation");
  observeSource(family,to);const auto spawns=fullPipeSpawnCount;require(inputHostStart()&&fullPipeSpawnCount==spawns+1&&GetProcessId(g_inputHostProcess)!=oldPid&&g_inputHostEpoch!=oldEpoch,"new persona creates exactly one new Host after old child/controller/context exit proof");
 }
 const auto stale=fullPipeSubmitSource(oldSource,true,false);require((stale.value("hostSubmission",std::string{})!="frame-accepted"||stale.value("focusIsolationSemanticNeutral",false))&&g_virtualStartProtection.view(startProtectionClock).phase=="pending","old sample cannot become a live frame or successful candidate on the newly bound persona");
 cases.back()["transferProof"]={{"from",from},{"to",to},{"oldPid",oldPid},{"oldEpoch",oldEpoch},{"oldNaturalExit",released.value("nativeObservedProcessExit",false)},{"newPid",GetProcessId(g_inputHostProcess)},{"newEpoch",g_inputHostEpoch},{"oldFrameAfterIntent",late},{"oldFrameAfterNewHost",stale}};
 success(family,to,ticket);close(family,to);require(fullPipeSpawnCount==caseSpawns+2,"directed persona transfer owns only one Host per requested persona");cases.back()["passed"]=true;fixtureFullPipe=false;
}
static void gpdLiveSdTransferCases(){
 const ControlledDmi dmi={"GPD","G1618-05","HandheldCompanion.Devices.GPDWin5","/physicalModels/0"};
 const auto begin=cases.size();
 for(const auto* persona:gpdLiveSdTransferPersonas)transfer("steamdeck",persona,2,&dmi);
 require(cases.size()==begin+std::size(gpdLiveSdTransferPersonas)&&fullPipeChildObservers.empty()&&!g_inputHostProcess,
  "explicit GPD live SD selection executes exactly two canonical transfers and returns all owned children");
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

// Exact original worker path: a current protected tuple classification, then a
// one-shot transport OS error queues the real frame-fault cleanup token. The
// runtime consumer must decline it before switch cleanup can inspect it.
static const char* classifiedFaultCleanupPersonas[]={"dualsense-edge","elite"};
static void classifiedFaultCleanupCases140(const std::string& onlyPersona={}){
 using namespace personaLifecycle140;
 const auto faultReceipt=[&](HANDLE child,const std::wstring& file,const std::string& persona){
  const auto result=fullPipeChildOutcome(child,file);const auto code=result.value("nativeObservedExitCode",999);
  const auto terminal=result.value("terminalSettlement",json::object());
  const bool expectedExit=code==0||(code==5&&terminal.value("Reason",std::string{})=="read-failed"&&terminal.value("ManagedReleaseComplete",false));
  require(result.value("nativeObservedProcessExit",false)&&expectedExit&&result.value("finalCleanupOk",false)&&!result.value("hasController",true)&&!result.value("hasContext",true),
   "faulted exact managed child exits naturally and fully releases; controlled transport EOF may retain exit5");
  require(result.value("creates",0)==1&&result.value("actualProductionHostSessionConsumer",false)&&result.value("actualProductionPipeLoop",false)&&result["stateAfterFinalCleanup"]["tuple"].value("Persona",std::string{})==persona,
   "fault receipt preserves exact persona and actual production loop with one released old target");
  cases.back()["managedReceipts"].push_back({{"path",W2U(file)},{"processId",result.value("processId",0u)},{"persona",persona},{"creates",result.value("creates",0)},
   {"controllerReleased",!result.value("hasController",true)},{"contextReleased",!result.value("hasContext",true)},{"naturalExit",result.value("nativeObservedProcessExit",false)},
   {"nativeExitCode",code},{"terminalReason",terminal.value("Reason",std::string{})},{"tuple",result["stateAfterFinalCleanup"]["tuple"]},{"faultScope","one controlled transport OS failure; not normal OFF exit-code exemption"}});
  return result;
 };
 const auto stops=[](){return std::count_if(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),[](const auto& s){return s.rfind("stop:",0)==0;});};
 for(const char* rawPersona:classifiedFaultCleanupPersonas){
  const std::string persona=rawPersona;
  if(!onlyPersona.empty()&&persona!=onlyPersona)continue;
  require(!g_inputHostReleaseQuarantined&&!g_inputHostEpoch&&!g_psEndpointOwnership.retirementRequired()&&!g_hidHideVisibilityJournal.active,
   "independent fault case begins only after the preceding actual return owner fully closed");
  prepare(1,"classified-fault-cleanup/"+persona);
  fixtureClassifiedReturnReleasedEpoch=0;fixtureRuntimeRecoveryOsEnabled=true;
  const auto doc=targetDocument(persona,0);
  require(sgWriteFileAtomic(YM_SETTINGS_FILE,doc.dump()),"classified cleanup seeds only its explicit private persona");
  inputCaptureRefreshRealStickSettings(true);observeSource(1,persona);
  const auto creates=fullPipeSpawnCount;
  require(inputHostStart()&&fullPipeSpawnCount==creates+1,"classified cleanup starts one actual managed Host through production Start");
  const auto oldChild=fullPipeChildObservers.back();const auto oldFile=fullPipeReceipts.back();
  const auto oldEpoch=g_inputHostEpoch;const auto oldRun=g_inputHostRunId;
  const auto oldInstance=g_inputHostHostInstanceId;const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
  HANDLE oldObservation=nullptr;
  require(DuplicateHandle(GetCurrentProcess(),oldChild,GetCurrentProcess(),&oldObservation,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,
   "fault return owns an independent exact old process observer until native settlement completes");
  ymcc::targetReturn::fileDetail::Handle oldObserver(oldObservation);
  fixtureRuntimeRecoveryOldHost=oldObservation;fixtureRuntimeRecoveryOldOwner=inputHostTargetReturnCaptureOwner(g_inputHostProcess);
  require(fixtureRuntimeRecoveryOldOwner.has_value(),"real fullPipe old Host supplies its actual PID and birth for writer proof");
  unsigned reads=0;
  for(;reads<32;++reads){advance(8);auto sample=fresh(1);fullPipeSubmitSource(sample,true,false);const auto v=g_virtualStartProtection.view(startProtectionClock);if(v.phase=="verifying"&&v.candidateOutcome=="success")break;}
  require(reads<32&&g_inputHostActive,"classified cleanup has an actual current-source accepted candidate");
  const auto candidate=g_virtualStartProtection.view(startProtectionClock);
  require(candidate.requestId==ticket&&candidate.locked&&candidate.verificationRemainingMs==10000&&candidate.fallbackAttempts==0,
   "classified cleanup retains original ten-second inspection before any fallback");
  const auto originalDeadline=startProtectionClock+candidate.remainingMs;
  std::string tupleToken;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
   inputHostSwitchRecoveryOfferLocked(ticket,currentPowerGeneration(),ERROR_SEM_TIMEOUT);
   check(!g_inputHostSwitchRecovery.pending&&!g_inputHostSwitchRecoveryPending.load()&&!g_inputHostSwitchRecovery.requestId,
    "ordinary unclassified non-SD transport cannot acquire broader classified recovery permission");
   tupleToken=inputHostTupleRecoveryReason(inputHostTupleRecoveryTokenLocked(oldEpoch,g_inputHostSequence));}
  advance(500);(void)fresh(1);
  require(inputHostQueueLifecycleWork(tupleToken.c_str()),"classified cleanup queues its exact original current tuple token");
  fullPipeDrainQueuedLifecycle();
  require(g_inputHostSwitchRecovery.pending&&g_inputHostSwitchRecovery.tupleFault&&!g_inputHostSwitchRecovery.spent&&!g_inputHostSwitchRecovery.stopInvoked,
   "actual tuple worker offers a current unspent classified request without early Stop");
  advance(g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs);
  auto faultSource=fresh(1);
  require(ymcc::inputHost::switchRecoveryRuntime::executionInspectionCompleteLocked(startProtectionClock)&&
   g_virtualStartProtection.view(startProtectionClock).locked&&finalCount(ticket,"success")==0,
   "fault token arrives at completed inspection while the original period remains protected");
  const auto runtimeBefore=ymcc::inputHost::runtimeRecovery::state;
  require(runtimeBefore.hostEpoch!=oldEpoch&&!runtimeBefore.pending&&!runtimeBefore.stopInvoked,
   "runtime-consumer OR branch has no matching old epoch authority before actual dispatch");
  const auto stopBefore=stops();const auto budgetBefore=g_inputHostRecoveryEpisodeAttempts.load();
  if(persona=="dualsense-edge"){
   fixturePsReturnManualTopology=true;
   fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"},ERROR_ACCESS_DENIED);
  }
  fixtureRuntimeRecoveryCensus="unknown";
  fixtureRuntimeRecoveryFaultEnabled=true;fixtureRuntimeRecoveryFaultConsumed=false;fixtureStartupTransportError=ERROR_SEM_TIMEOUT;
  const auto failedFrame=fullPipeSubmitSource(faultSource,true,false);
  require(fixtureRuntimeRecoveryFaultConsumed&&fixtureRuntimeRecoveryFaultCalls==1&&g_inputHostTransportFault,
   "actual request classifier consumes exactly one transport OS failure in the real managed pipe path");
  fixtureRuntimeRecoveryFaultEnabled=false;fixtureStartupTransportError=ERROR_SUCCESS;
  const auto faultToken="fault-stop:frame-receipt-fault:"+std::to_string(oldEpoch);
  json work;
  {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);work=g_inputHostLifecycleWorkQueue;}
  require(std::count(work.begin(),work.end(),json(faultToken))==1&&
    std::none_of(work.begin(),work.end(),[](const auto& row){return row.template get<std::string>().rfind("switch-recovery:",0)==0;}),
   "real frame producer queues exactly its fault token without an alternative recovery dispatcher");
  require(!ymcc::inputHost::runtimeRecovery::state.pending&&ymcc::inputHost::runtimeRecovery::state.hostEpoch!=oldEpoch,
   "protected failure is not adopted by the post-final runtime consumer");
  cases.back()["faultInput"]={{"tupleToken",tupleToken},{"faultToken",faultToken},{"queuedWork",work},{"frame",failedFrame},
   {"runtimeHostEpoch",runtimeBefore.hostEpoch},{"runtimePending",runtimeBefore.pending},{"runtimeStopInvoked",runtimeBefore.stopInvoked},
   {"ticket",ticket},{"oldEpoch",oldEpoch},{"oldRun",oldRun},{"oldInstance",oldInstance}};
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
   namespace recovery=ymcc::inputHost::switchRecoveryRuntime;
   const auto& request=g_inputHostSwitchRecovery;
   const bool held=recovery::stopHeldLocked(GetTickCount64()),budget=recovery::budgetBlockedLocked(GetTickCount64());
   const bool ordinary=recovery::recoverableOwnedStateLocked(request.power);
   const bool classified=recovery::recoverableOwnedStateLocked(request.power,request.tupleFault,request.localFault!=InputHostSwitchLocalFault::None);
   cases.back()["cleanupGateInputs"]={{"held",held},{"budgetBlocked",budget},{"ordinaryOwnedPredicate",ordinary},{"classifiedOwnedPredicate",classified}};
   require(!held&&!budget&&!ordinary&&classified,"only lost fault classification distinguishes cleanup admission at the actual worker boundary");
  }
  fullPipeDrainQueuedLifecycle();
  const auto repaired=g_virtualStartProtection.view(startProtectionClock);
  const bool owned=g_inputHostSwitchRecovery.requestId==ticket&&g_inputHostSwitchRecovery.stopInvoked&&g_inputHostSwitchRecovery.spent&&
   g_inputHostSwitchRecovery.awaitingCleanup&&g_inputHostSwitchRecovery.pending&&g_inputHostSwitchRecovery.deadline==originalDeadline&&
   repaired.requestId==ticket&&repaired.fallbackAttempts==1&&stops()==stopBefore+1&&
   g_inputHostRecoveryEpisodeAttempts.load()==budgetBefore+1&&fullPipeSpawnCount==creates+1&&!g_inputHostProcess&&
   g_inputHostEpoch==oldEpoch&&g_inputHostRunId==oldRun&&g_inputHostReleaseQuarantined;
  require(owned,"classified current non-SD fault token owns one cleanup and waits for the actual old return proof");
  cases.back()["afterFault"]={{"owned",owned},{"requestId",g_inputHostSwitchRecovery.requestId},{"stopInvoked",g_inputHostSwitchRecovery.stopInvoked},
   {"spent",g_inputHostSwitchRecovery.spent},{"fallbackAttempts",repaired.fallbackAttempts},{"stopDelta",stops()-stopBefore},
   {"budgetDelta",g_inputHostRecoveryEpisodeAttempts.load()-budgetBefore},{"createDelta",fullPipeSpawnCount-creates},{"state",state()},{"events",fixtureEvents}};
  const auto released=faultReceipt(oldChild,oldFile,persona);
  require(released.value("nativeObservedProcessExit",false)&&released["terminalSettlement"].value("ManagedReleaseComplete",false),
   "classified fallback observes exact old managed child natural exit before continuing");
  require(WaitForSingleObject(oldObservation,0)==WAIT_OBJECT_0&&fullPipeSpawnCount==creates+1,
   "exact old process and SDK resources are returned before any replacement create");
  fixtureClassifiedReturnReleasedEpoch=oldEpoch;
  fixtureRuntimeRecoveryCensus="absent";
  if(persona=="dualsense-edge")fixturePsReadConfig({},ERROR_NO_MORE_ITEMS);
  // Feed a new controlled OS slot observation through the original canonical
  // discovery loop. No connected/physical/admitted mask is assigned here.
  {const ULONGLONG probeNow=GetTickCount64();const int probeSrc=kGpReadBus;const bool probeCostOn=false;
   const auto diagnosticReadPower=currentPowerGeneration();const auto diagnosticReadTopology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
   #include "xinput_discovery.inc"
  }
  (void)fresh(1);
  if(persona=="dualsense-edge"){FixturePsReadScope scope;fullPipeQuarantineProbe();}
  else fullPipeQuarantineProbe();
  bool settled=false;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);settled=inputHostTargetReturnRuntimeSettledLocked(oldRun,oldEpoch);}
  cases.back()["beforeRestart"]={{"settled",settled},{"state",state()},{"creates",fullPipeSpawnCount-creates},
   {"writerProofCalls",fixtureRuntimeRecoveryWriterCalls},{"censusCalls",fixtureRuntimeRecoveryCensusCalls},
   {"oldProcessWait",WaitForSingleObject(oldObservation,0)},{"deadline",g_inputHostSwitchRecovery.deadline},{"originalDeadline",originalDeadline}};
  require(settled&&!g_inputHostEpoch&&!g_inputHostReleaseQuarantined&&!g_hidHideVisibilityJournal.active&&
   !g_psEndpointOwnership.retirementRequired()&&fullPipeSpawnCount==creates+1,
   "original quarantine owner settles exact target physical and durable duty before any replacement");
  // Production Clear retires old source admission. Re-run the original
  // discovery+admission consumer, never seed masks/guards or reuse old proof.
  cases.back()["postReturnSourceAdmission"]=fullPipeReadPhysicalSourceForElite(false);
  (void)fresh(1);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  require(g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized&&g_inputHostEpoch!=oldEpoch&&
   g_inputHostRunId!=oldRun&&g_inputHostBoundPersona==persona&&fullPipeSpawnCount==creates+2&&
   stops()==stopBefore+1&&g_inputHostRecoveryEpisodeAttempts.load()==budgetBefore+1,
   "only original proven-return continuation creates exactly one same-persona replacement without a second charge");
  const auto newEpoch=g_inputHostEpoch;const auto newRun=g_inputHostRunId;const auto replacement=g_inputHostProcess;
  for(unsigned n=0;n<8;++n)require(inputHostQueueLifecycleWork(faultToken.c_str()),"late old fault token is queued through original worker");
  fullPipeDrainQueuedLifecycle();
  require(g_inputHostProcess==replacement&&g_inputHostEpoch==newEpoch&&g_inputHostRunId==newRun&&stops()==stopBefore+1&&
   fullPipeSpawnCount==creates+2&&g_inputHostRecoveryEpisodeAttempts.load()==budgetBefore+1,
   "old fault token cannot Stop successor or reserve another fallback charge");
  reads=0;
  for(;reads<32;++reads){advance(8);auto sample=fresh(1);const auto accepted=fullPipeSubmitSource(sample,true,false);const auto v=g_virtualStartProtection.view(startProtectionClock);if(accepted.value("hostSubmission",std::string{})=="frame-accepted"&&!accepted.value("focusIsolationSemanticNeutral",true)&&g_inputHostActive&&v.phase=="verifying"&&v.candidateOutcome=="success"){cases.back()["replacementAcceptedFrame"]=accepted;break;}}
  require(reads<32,"same-persona replacement reaches actual accepted-source inspection");
  const auto inspected=g_virtualStartProtection.view(startProtectionClock);
  require(inspected.requestId==ticket&&inspected.fallbackAttempts==1&&inspected.remainingMs<=ymcc::VirtualStartProtection::maxMs&&startProtectionClock+inspected.remainingMs==originalDeadline,
   "fallback retains the original request and thirty-second hard ceiling");
  advance(inspected.verificationRemainingMs);(void)fresh(1);fullPipeVerificationStep();fullPipeVerificationStep();
  const auto done=g_virtualStartProtection.view(startProtectionClock);
  require(done.requestId==ticket&&done.phase=="success"&&!done.locked&&done.fallbackAttempts==1&&
   finalCount(ticket,"success")==1&&finalCount(ticket,"failure")==0&&done.elapsedMs==startProtectionClock-(originalDeadline-ymcc::VirtualStartProtection::maxMs),
   "classified fallback publishes exactly one success and no contradictory failure");
  cases.back()["periods"].push_back({{"requestId",ticket},{"persona",persona},{"outcome","success"},{"fallbackAttempts",done.fallbackAttempts},{"elapsedMs",done.elapsedMs}});
  close(1,persona);
  require(fullPipeSpawnCount==creates+2&&fullPipeChildObservers.empty()&&!g_inputHostProcess,
   "classified fallback and actual OFF leave no owned child or target");
  require(!g_inputHostEpoch&&!g_inputHostReleaseQuarantined&&!g_psEndpointOwnership.retirementRequired()&&!g_hidHideVisibilityJournal.active,
   "successful final OFF fully closes ownership before next independent fault case");
  require(oldObserver.close(),"extra exact old-process observation handle is checked closed");
  fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureRuntimeRecoveryOsEnabled=false;
  fixtureClassifiedReturnReleasedEpoch=0;
  cases.back()["passed"]=true;fixtureFullPipe=false;
 }
}


static const char* runtimeFamilyTokens140[]={"msi","rog","gpd","onex","aya","lenovo"};
static const char* runtimePersonaTokens140[]={"steamdeck","dualsense-edge","elite"};
static const char* runtimeCancellationEdges140[]={"off-before-stop","off-after-stop","power-before-stop","power-after-stop","exit-before-stop","exit-after-stop"};
static void runtimeFamilyTransportCase140(const std::string& familyToken,const std::string& persona,const std::string& boundary={}){
 using namespace personaLifecycle140;
 const auto familyIt=std::find(std::begin(runtimeFamilyTokens140),std::end(runtimeFamilyTokens140),familyToken);
 require(familyIt!=std::end(runtimeFamilyTokens140)&&std::find(std::begin(runtimePersonaTokens140),std::end(runtimePersonaTokens140),persona)!=std::end(runtimePersonaTokens140),"post-final request matches source-declared family and persona");
 const int family=static_cast<int>(familyIt-std::begin(runtimeFamilyTokens140));
 const ControlledDmi gpd={"GPD","G1618-05","HandheldCompanion.Devices.GPDWin5","/physicalModels/0"};
 const ControlledDmi onex={"ONE-NETBOOK TECHNOLOGY CO., LTD.","ONEXPLAYER X2","HandheldCompanion.Devices.OneXPlayerX2","/physicalModels/2"};
 const ControlledDmi aya={"AYADEVICE","AYANEO 3","HandheldCompanion.Devices.AYANEO3","/physicalModels/5"};
 const ControlledDmi lenovo={"LENOVO","LNVNB161216","HandheldCompanion.Devices.LegionGoTablet",nullptr,"83E1","hc.device.LegionGoTablet"};
 const ControlledDmi* dmi=family==2?&gpd:family==3?&onex:family==4?&aya:family==5?&lenovo:nullptr;
 const bool terminalFallback=boundary=="terminal-exhausted-physical-return";
 const bool delayedSuccessor=boundary=="delayed-same-period-successor"||terminalFallback;
 const unsigned wallGapMs=boundary=="wall-gap-5s"?5000u:boundary=="wall-gap-10s"?10000u:0u;
 const bool failedRestart=boundary=="persistent-start-failure"||boundary=="persistent-prepared-failure";
 const bool repeatIncident=boundary=="persistent-repeat-before-observer"||boundary=="persistent-repeat-after-observer";
 const bool persistent=boundary=="persistent-budget"||boundary=="persistent-off"||failedRestart||repeatIncident;
 const unsigned replacementCount=(failedRestart||delayedSuccessor)?2u:1u;
 if(persistent)require(familyToken=="gpd"&&persona=="elite","persistent cases use exact existing GPD Elite managed consumer");
 if(!boundary.empty()&&!persistent&&!delayedSuccessor&&!wallGapMs)require(((familyToken=="rog"&&persona=="steamdeck")||(familyToken=="gpd"&&persona=="elite"))&&
  std::find(std::begin(runtimeCancellationEdges140),std::end(runtimeCancellationEdges140),boundary)!=std::end(runtimeCancellationEdges140),"runtime cancellation uses one source-declared parameter of two actual route representatives");
 prepare(family,"runtime-after-final/"+familyToken+"/"+persona+(boundary.empty()?"":"/"+boundary),dmi);
 struct RestoreControlledPhysicalIdentity {
  std::wstring original=fixturePhysicalPath;
  ~RestoreControlledPhysicalIdentity(){fixturePhysicalPath=std::move(original);}
 } restoreControlledPhysicalIdentity;
 fixtureRuntimeRecoveryOsEnabled=true;fixtureClassifiedReturnReleasedEpoch=0;
 if(family==0){
  // Supply the actual retained MODE2 owner's corresponding returned HID leaf.
  // Other families keep the original generic path; no product proof is forged.
  fixturePhysicalPath=L"\\\\?\\HID#VID_0DB0&PID_1901#OFFLINE_MSI_RETURN";
  // Complete controlled DMI leaf; the original seed omits two fields and
  // cannot satisfy the unchanged four-field durable board-key schema.
  g_machineIdentity.systemProduct="CLAW A8"; // Same controlled DMI as original t91 resolver case.
  ymccResolveFamily(g_machineIdentity);
  require(g_machineIdentity.resolved&&g_machineIdentity.family==YmccFamily::MsiClaw&&
   g_machineIdentity.deviceClass=="HandheldCompanion.Devices.ClawCG3EM"&&msiRecovery::validBoardKey(msiCrashBoardKey()),
   "actual MSI board resolver supplies canonical class and complete durable board identity before metadata consumer");
  fixtureMsiOwnedMetadata=true;
  g_msiForegroundTestIo.locations=[](const std::string& root){
   if(root=="USB\\VID_0DB0&PID_1902\\7&OWNED&0&3"||root=="USB\\VID_0DB0&PID_1901\\7&OWNED&0&3")
    return std::vector<std::wstring>{L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(3)"};
   return std::vector<std::wstring>{};
  };
  require(!g_msiCrashLoaded&&!g_msiCrashStoreInvalid&&!g_msiDinputReturnOwed.load(),"MSI metadata setup starts without preexisting durable loader or in-memory mode obligation");
  {struct RestoreSeam{bool prior;~RestoreSeam(){g_startEntrySeam.active=prior;}} restore{g_startEntrySeam.active};
   g_startEntrySeam.active=false;msiCrashRecoveryInitialize();
   MsiClawHidDiag command;command.interfacePath="\\\\?\\HID#VID_0DB0&PID_1902#OFFLINE_MSI_COMMAND";command.productId=0x1902;
   require(msiCrashRecoveryBeforeModeWrite(2,command),"actual pre-MODE2 durable consumer commits same-Native root and location before simulated mode request");
  }
  require(ymStartSeamActive()&&g_msiCrashLoaded&&!g_msiCrashStoreInvalid&&g_msiCrashRecord.modeReturnPending&&
   g_msiCrashRecord.ownerPid==GetCurrentProcessId()&&g_msiCrashRecord.ownerCreationTime==msiCrashProcessCreation(GetCurrentProcess()),
   "MSI private metadata scope restores hardware seam and retains actual process-bound persistent ownership");
  cases.back()["msiOwnedMode2Input"]={{"root",g_msiCrashRecord.rootInstance},{"locations",g_msiCrashRecord.locationPaths},
   {"ownerPid",g_msiCrashRecord.ownerPid},{"ownerBirth",g_msiCrashRecord.ownerCreationTime},{"modeReturnPending",g_msiCrashRecord.modeReturnPending},
   {"inputKind","actual-before-mode2-write-ahead-consumer;not-a-return-proof"}};
 }
 if(terminalFallback&&family!=0){
  // Existing enumeration leaf only. The original consumer, not the fixture,
  // publishes selected identity from one complete same-family candidate.
  fixtureRogInitialIdentityEnabled=true;fixtureRogInitialDiscoveryComplete=true;
  std::vector<HidHidePhysicalIdentity> expected;
  if(family==2)expected={fixtureGpdPostHideSource()};
  else {bool complete=false;require(fixtureRogInitialDiscovery(expected,&complete)&&complete,"terminal fixture enumeration is complete");}
  std::vector<HidHidePhysicalIdentity> consumed;
  require(expected.size()==1&&hidHideGetPhysicalIdentities(consumed)&&consumed.size()==1,
    "original selected identity consumer accepts one complete same-family enumeration before Start");
  std::lock_guard<std::mutex> selected(g_hidHidePhysicalIdentityMx);
  require(g_hidHidePhysicalIdentity.resolved&&g_hidHidePhysicalIdentity.deviceInstanceId==expected.front().deviceInstanceId&&
    g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId==expected.front().baseContainerDeviceInstanceId&&
    g_hidHidePhysicalIdentity.physicalIdentity==expected.front().physicalIdentity,
    "selected identity is published by actual consumer and exactly matches unique same-family source");
  cases.back()["terminalSelectedBeforeStart"]={{"consumer","hidHideGetPhysicalIdentities"},{"directGlobalAssignment",false},
    {"key",g_hidHidePhysicalIdentity.physicalIdentity},{"root",g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId}};
 }
 startFirst(family,persona);
 const auto oldTicket=g_virtualStartProtection.view(startProtectionClock).requestId,oldEpoch=g_inputHostEpoch;
 const auto oldRun=g_inputHostRunId,oldInstance=g_inputHostHostInstanceId;
 const auto oldChild=fullPipeChildObservers.back();const auto oldFile=fullPipeReceipts.back();
 const auto creates=fullPipeSpawnCount;const auto oldSuccesses=finalCount(oldTicket,"success");
 require(oldSuccesses==1&&!g_virtualStartProtection.view(startProtectionClock).locked,"runtime incident starts after an actual unique successful player final");
 advance(480001);(void)fresh(family);
 HANDLE observed=nullptr;require(DuplicateHandle(GetCurrentProcess(),oldChild,GetCurrentProcess(),&observed,0,FALSE,DUPLICATE_SAME_ACCESS)!=FALSE,"runtime retains an independent exact real old process observer");
 ymcc::targetReturn::fileDetail::Handle oldObserver(observed);
 fixtureRuntimeRecoveryOldHost=observed;fixtureRuntimeRecoveryOldOwner=inputHostTargetReturnCaptureOwner(g_inputHostProcess);
 require(fixtureRuntimeRecoveryOldOwner.has_value(),"runtime captures actual old PID and creation birth");
 if(persona=="dualsense-edge"){fixturePsReturnManualTopology=true;fixturePsReadConfig({L"HID#VID_054C&PID_0DF2#HIDMAESTRO-OWN"},ERROR_ACCESS_DENIED);}
 fixtureRuntimeRecoveryCensus="unknown";fixtureRuntimeRecoveryFaultEnabled=true;fixtureRuntimeRecoveryFaultConsumed=false;fixtureStartupTransportError=ERROR_SEM_TIMEOUT;
 auto faulty=fresh(family);const auto faultReply=fullPipeSubmitSource(faulty,true,false);
 fixtureRuntimeRecoveryFaultEnabled=false;fixtureStartupTransportError=ERROR_SUCCESS;
 auto& incident=ymcc::inputHost::runtimeRecovery::state;
 require(fixtureRuntimeRecoveryFaultConsumed&&fixtureRuntimeRecoveryFaultCalls==1&&incident.pending&&incident.hostEpoch==oldEpoch&&incident.runId==oldRun&&incident.hostInstanceId==oldInstance&&incident.requestId==oldTicket&&incident.persona==persona,"real post-final transport classifier offers exactly the faulted old owner");
 const auto serial=incident.serial;const auto beforeBudget=g_inputHostRecoveryEpisodeAttempts.load();
 if(wallGapMs)g_shortcutRecordingActive=true; // Explicit existing expected-pause leaf keeps Start unissued while exact old return completes.

 if(delayedSuccessor){
  require(terminalFallback?((familyToken=="rog"&&persona=="steamdeck")||(familyToken=="gpd"&&persona=="elite")||(familyToken=="msi"&&persona=="steamdeck")):(familyToken=="rog"&&persona=="steamdeck"),"manual successor/terminal route is one exact source-declared family representative");
  fixtureRuntimeRecoveryFirstHelloOnly=true;fixtureRuntimeRecoveryFaultEnabled=true;
  fixtureRuntimeRecoveryFaultConsumed=false;fixtureStartupTransportError=ERROR_SEM_TIMEOUT;
 }

 if(!boundary.empty()&&!persistent&&!delayedSuccessor&&!wallGapMs){
  const bool afterStop=boundary.ends_with("after-stop"),isOff=boundary.starts_with("off-"),isPower=boundary.starts_with("power-");
  if(afterStop){
   inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
   require(incident.pending&&incident.stopInvoked&&!incident.recoveryRequestId&&g_inputHostReleaseQuarantined,
    "late cancellation actually reaches the original safety Stop and unproven return quarantine");
  }else require(!incident.stopInvoked,"early cancellation changes authority before original runtime safety Stop");
  const auto oldPower=currentPowerGeneration();std::uint64_t offTicket=0;
  const auto actualOff=[&](){
   const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));const auto off=targetDocument("disabled",current["input"]["revision"].get<int>()+1);
   require(writeTarget(current,off).value("ok",false),"post-final cancellation OFF is admitted through the real settings IPC");
   inputCaptureRefreshRealStickSettings(true);offTicket=g_virtualStartProtection.view(startProtectionClock).requestId;
   require(offTicket!=oldTicket&&g_virtualStartProtection.view(startProtectionClock).persona=="disabled",
    "cancelled old incident does not own the distinct explicit OFF period");
  };
  if(isOff)actualOff();
  else if(isPower){g_powerGeneration=oldPower+1;g_powerLifecycle=PowerLifecycle::Suspending;g_inputHostPowerAdmissionRevoked=true;}
  else g_exitRequested=true;
  inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  require(!incident.pending&&incident.spent&&!incident.recoveryRequestId&&!g_inputHostRuntimeRecoveryPending.load()&&
   fullPipeSpawnCount==creates&&g_inputHostRecoveryEpisodeAttempts.load()==beforeBudget,
   "persistent authority cancellation retires the actual queued runtime incident without a restart ticket charge or Host");
  const auto retirement=fixtureEvents.size();
  fixtureClassifiedReturnReleasedEpoch=oldEpoch;fixtureRuntimeRecoveryCensus="absent";
  inputHostStop(isOff?"virtual-target-disabled":isPower?"suspend":"app-exit");
  const auto child=fullPipeChildOutcome(oldChild,oldFile);const auto terminal=child.value("terminalSettlement",json::object());
  require(child.value("nativeObservedProcessExit",false)&&child.value("nativeObservedExitCode",999)==5&&
   terminal.value("Reason",std::string{})=="read-failed"&&terminal.value("ManagedReleaseComplete",false)&&
   child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),
   "cancelled incident still naturally releases its exact old managed fault Host controller and context");
  cases.back()["managedReceipts"].push_back({{"path",W2U(oldFile)},{"processId",child.value("processId",0u)},
   {"persona",persona},{"creates",child.value("creates",0)},{"naturalExit",true},{"nativeExitCode",5},
   {"controllerReleased",true},{"contextReleased",true},{"faultScope","cancelled actual post-final transport incident"}});
  if(isPower){g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;}
  for(unsigned pass=0;pass<3;++pass){advance(1001);gamepadReadState(false);xinputSlotsEnsurePreTargetBoundaryDiscovery();fullPipeQuarantineProbe();inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();}
  if(!isOff&&!g_exitRequested.load())actualOff();
  if(offTicket){
   inputHostStop("virtual-target-disabled");fullPipeQuarantineProbe();fullPipeDrainQueuedLifecycle();
   if(family==1)fullPipeRogObserveReturnedSource(offTicket,true);
   const auto view=g_virtualStartProtection.view(startProtectionClock);if(view.locked&&view.verificationRemainingMs)advance(view.verificationRemainingMs);
   if(family==1)(void)fresh(family);fullPipeVerificationStep();fullPipeVerificationStep();
   require(finalCount(offTicket,"success")==1&&finalCount(offTicket,"failure")==0,
    "explicit cancellation OFF reaches one successful closing final after actual return");
  }
  for(unsigned late=0;late<8;++late){inputHostPerformRuntimeRecovery(serial);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();}
  bool settled=false;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);settled=inputHostTargetReturnRuntimeSettledLocked(oldRun,oldEpoch);}
  require(!incident.pending&&incident.spent&&!incident.recoveryRequestId&&fullPipeSpawnCount==creates&&
   g_inputHostRecoveryEpisodeAttempts.load()==beforeBudget&&finalCount(oldTicket,"success")==1&&finalCount(oldTicket,"failure")==0,
   "eight late old worker dispatches after real cancellation cannot resurrect output spend quota or contradict the original success");
  require(!g_inputHostProcess&&!g_inputHostEpoch&&!g_inputHostPrepared&&!g_inputHostReleaseQuarantined&&
   !g_hidHideVisibilityJournal.active&&fullPipeChildObservers.empty()&&settled,
   "cancelled post-final lifecycle releases actual old target Host visibility and return obligations");
  require(oldObserver.close(),"cancelled incident closes its extra exact old-process observer");
  cases.back()["runtimeCancellation"]={{"boundary",boundary},{"phaseAtCancellation",afterStop?"old-stop-return-pending":"offered-before-stop"},
   {"oldPower",oldPower},{"newPower",currentPowerGeneration()},{"powerAdvanced",isPower},{"exitRemainsRequested",g_exitRequested.load()},
   {"originalRequest",oldTicket},{"offRequest",offTicket},{"restartTicket",incident.recoveryRequestId},{"budgetDelta",g_inputHostRecoveryEpisodeAttempts.load()-beforeBudget},
   {"oldFinalSuccess",finalCount(oldTicket,"success")},{"oldFinalFailure",finalCount(oldTicket,"failure")},{"settled",settled},{"lateDispatches",8},
   {"retirementEventCount",retirement},{"authorityRestoredBeforeRetirement",false}};
  fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureRuntimeRecoveryOsEnabled=false;fixtureClassifiedReturnReleasedEpoch=0;
  cases.back()["passed"]=true;fixtureFullPipe=false;return;
 }

 if(family==0)fixtureMsiRuntimeProofEdges=[&](){
  json edges=json::array();
  for(const std::string edge:{"mode","intent","snapshot-publication","source-event","identity","topology","power","exit","hardware"}){
   const auto mode=g_msiFeedbackModeGeneration.load(),intent=g_inputHostVirtualIntentRevision.load(),sourceEvent=g_msiOwnedReturnSourceEvent.load();
   const auto identity=g_xinputSlotIdentityEpoch.load(),topology=g_xinputTopologyEpoch.load();const auto power=currentPowerGeneration();
   const auto exit=g_exitRequested.load(),hardware=g_hardwareWriteGate.load();const bool targetIntent=g_realStickTest.virtualTargetEnabled;unsigned clockReads=0;
   fixtureMonotonicInterleave=[&](){if(++clockReads!=2)return;
    if(edge=="mode")g_msiFeedbackModeGeneration=mode+1;
    else if(edge=="intent")g_realStickTest.virtualTargetEnabled=false;
    else if(edge=="snapshot-publication")g_inputHostVirtualIntentRevision=intent+1;
    else if(edge=="source-event")g_msiOwnedReturnSourceEvent=sourceEvent+1;
    else if(edge=="identity")g_xinputSlotIdentityEpoch=identity+1;
    else if(edge=="topology")g_xinputTopologyEpoch=topology+1;
    else if(edge=="power")g_powerGeneration=power+1;
    else if(edge=="exit")g_exitRequested=true;
    else if(edge=="hardware")g_hardwareWriteGate=false;
   };
   const bool accepted=inputHostRuntimeMsiReturnProofCurrentLocked();fixtureMonotonicInterleave={};
   // Independent controlled read-only interleavings, not a player lifecycle:
   // restore the input after the predicate returns, before the positive flow.
   g_msiFeedbackModeGeneration=mode;g_inputHostVirtualIntentRevision=intent;g_msiOwnedReturnSourceEvent=sourceEvent;
   g_xinputSlotIdentityEpoch=identity;g_xinputTopologyEpoch=topology;g_powerGeneration=power;g_exitRequested=exit;g_hardwareWriteGate=hardware;g_realStickTest.virtualTargetEnabled=targetIntent;
   const bool expectedAcceptance=edge=="snapshot-publication";
   require(clockReads>=2&&accepted==expectedAcceptance,expectedAcceptance?
    "unchanged effective intent publication retains the exact cached runtime return proof":
    "actual cached runtime proof rejects a late effective intent cancellation or generation edge after sample copy");
   require(incident.pending&&!incident.spent&&!incident.recoveryRequestId&&fullPipeSpawnCount==creates&&g_inputHostRecoveryEpisodeAttempts.load()==beforeBudget,
    "read-only rejected proof edge cannot mint a recovery ticket charge or replacement");
   edges.push_back({{"edge",edge},{"actualClockReads",clockReads},{"accepted",accepted},{"expectedAcceptance",expectedAcceptance},{"oldTicket",oldTicket},{"newTicket",incident.recoveryRequestId}});
  }
  cases.back()["runtimeProofLateRejectEdges"]=edges;
 };

 inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
 require(incident.pending&&incident.stopInvoked&&!incident.recoveryRequestId&&fullPipeSpawnCount==creates&&g_inputHostRecoveryEpisodeAttempts.load()==beforeBudget,"post-final safety Stop waits for real old return without spending a restart or changing old final");
 const auto child=fullPipeChildOutcome(oldChild,oldFile);const auto terminal=child.value("terminalSettlement",json::object());
 require(child.value("nativeObservedProcessExit",false)&&child.value("nativeObservedExitCode",999)==5&&terminal.value("Reason",std::string{})=="read-failed"&&terminal.value("ManagedReleaseComplete",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"actual faulted old child naturally exits and releases before any replacement");
 cases.back()["managedReceipts"].push_back({{"path",W2U(oldFile)},{"processId",child.value("processId",0u)},{"persona",persona},{"creates",child.value("creates",0)},{"naturalExit",true},{"nativeExitCode",5},{"controllerReleased",true},{"contextReleased",true},{"faultScope","controlled transport EOF"}});
 fixtureClassifiedReturnReleasedEpoch=oldEpoch;fixtureRuntimeRecoveryCensus="absent";
 if(persona=="dualsense-edge")fixturePsReadConfig({},ERROR_NO_MORE_ITEMS);
 if(family==0)g_msiSdOwnedPnpSnapshotForTest=[](){return std::pair{true,std::set<std::string>{}};}; // Controlled PnP only; original runtime Start owns MSI route admission, never normal-OFF debt helper.
 {const ULONGLONG probeNow=GetTickCount64();const int probeSrc=kGpReadBus;const bool probeCostOn=false;
  const auto diagnosticReadPower=currentPowerGeneration();const auto diagnosticReadTopology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
  #include "xinput_discovery.inc"
 }
 if(family==0){
  auto observeMsiReturn=[&](const char* stage){
   InputCaptureGamepadSnapshot sample;{std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);sample=g_inputCaptureGamepadSnapshot;}
   json slots=json::array();for(unsigned slot=0;slot<4;++slot)slots.push_back({{"slot",slot},{"resolvedEpoch",g_xinputSlotIdentityResolvedEpoch[slot]},
     {"path",W2U(g_msiOwnedReturnSlotPaths[slot])},{"feedbackValid",g_xinputFeedbackIdentity[slot].capture(g_xinputSlotIdentityEpoch.load(),currentPowerGeneration()).valid}});
   if(!cases.back().contains("msiCanonicalReturn"))cases.back()["msiCanonicalReturn"]=json::array();
   cases.back()["msiCanonicalReturn"].push_back({{"stage",stage},{"tick",GetTickCount64()},{"rawConnected",g_xinputConnectedMask},
    {"virtualMask",g_xinputVirtualEndpointMask},{"unknownMask",g_xinputSlotUnknownMask},{"physicalMask",xinputPhysicalConnectedMask()},
    {"selectedMask",g_xinputPhysicalSourceMask},{"locked",static_cast<bool>(g_xinputPhysicalSourceLocked)},
    {"preTargetSlots",g_xinputPreTargetSlots},{"fullScanRequested",g_xinputSlotFullScanOnce.load()},
    {"faultHold",g_inputHostFaultHoldsStart.load()},{"runtimePending",incident.pending},{"runtimeStopInvoked",incident.stopInvoked},
    {"runtimeNewTicket",incident.recoveryRequestId},{"runtimeSpent",incident.spent},
    {"identityEpoch",g_xinputSlotIdentityEpoch.load()},{"topology",g_xinputTopologyEpoch.load()},
    {"boundaryScanned",g_xinputPreTargetBoundaryScanned.load()},{"boundaryEpoch",g_xinputBoundaryScanEpoch.load()},
    {"modeDebt",g_msiDinputReturnOwed.load()},{"modeEpisode",g_msiDinputReturnDebtEpisode.load()},
    {"repairScope",msiControllerRepairScope()},{"dinputEligible",msiDinputMainSourceEligible()},
    {"sample",{{"live",sample.live},{"source",sample.inputSource},{"connected",sample.connectedMask},{"sequence",sample.readAttemptSequence},
      {"power",sample.readPowerGeneration},{"topology",sample.readTopologyGeneration},{"identity",sample.readIdentityGeneration},
      {"started",sample.readStartedTick},{"completed",sample.readCompletedTick},{"stable",sample.readContextStable}}},{"slots",slots}});
  };
  {
   std::lock_guard<std::mutex> durable(g_msiCrashMx);
   cases.back()["msiDurableOwnerBeforeReturn"]={{"loaded",g_msiCrashLoaded},{"invalid",g_msiCrashStoreInvalid},
    {"root",g_msiCrashRecord.rootInstance},{"locations",g_msiCrashRecord.locationPaths},
    {"modeReturnPending",g_msiCrashRecord.modeReturnPending},{"ownerPid",g_msiCrashRecord.ownerPid},
    {"ownerBirth",g_msiCrashRecord.ownerCreationTime},{"hardwareSeamActive",ymStartSeamActive()},
    {"scope","cached private durable ownership only; no device operation or proof mutation"}};
  }
  {
   std::wstring returnedRoot;const bool resolved=deviceInterfaceUsbInstance(fixturePhysicalPath.c_str(),returnedRoot);
   const auto locations=resolved?msiCrashLocations(W2U(returnedRoot)):std::vector<std::string>{};
   const bool sameRoot=resolved&&msiRecovery::sameDeviceRoot(g_msiCrashRecord,W2U(returnedRoot),locations);
   require(sameRoot,"controlled PID1901 metadata matches the actual retained MODE2 same-root/location identity");
   cases.back()["msiReturnedSourceIdentity"]={{"resolved",resolved},{"root",W2U(returnedRoot)},{"locations",locations},{"sameOriginalRootLocation",sameRoot},
    {"scope","read-only original root matcher; no matched token, operational proof or discharge publication"}};
  }
  observeMsiReturn("after-existing-partial-discovery");
  for(unsigned pass=0;pass<3;++pass){advance(1001);gamepadReadState(false);xinputSlotsEnsurePreTargetBoundaryDiscovery();fullPipeQuarantineProbe();observeMsiReturn(("after-actual-canonical-read-"+std::to_string(pass)).c_str());}
 }
 if(persona=="dualsense-edge"){FixturePsReadScope scope;fullPipeQuarantineProbe();}else fullPipeQuarantineProbe();
 bool settled=false;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);settled=inputHostTargetReturnRuntimeSettledLocked(oldRun,oldEpoch);}
 const bool resumedDuringDrain=g_inputHostEpoch&&g_inputHostEpoch!=oldEpoch&&incident.recoveryRequestId&&g_inputHostBoundPersona==persona;
 require(settled&&!g_inputHostReleaseQuarantined&&(resumedDuringDrain?fullPipeSpawnCount==creates+1:(!g_inputHostEpoch&&!g_hidHideVisibilityJournal.active&&fullPipeSpawnCount==creates)),"original runtime return owner settles exact old duties before any directly continued replacement");
 if(resumedDuringDrain){
  size_t settledAt=fixtureEvents.size(),createdAt=fixtureEvents.size();
  for(size_t n=0;n<fixtureEvents.size();++n){const auto& event=fixtureEvents[n];const auto& detail=event["detail"];
   if(event.value("event",std::string{})=="input-host-target-return-record"&&detail.value("phase",std::string{})=="target-settled"&&detail.value("runId",std::string{})==oldRun&&detail.value("epoch",0ULL)==oldEpoch)settledAt=(std::min)(settledAt,n);
   if(event.value("event",std::string{})=="input-host-primary-thread-retirement"&&detail.value("phase",std::string{})=="created"&&detail.value("epoch",0ULL)==g_inputHostEpoch)createdAt=(std::min)(createdAt,n);
  }
  require(settledAt<createdAt&&createdAt<fixtureEvents.size(),"exact old run epoch settlement event precedes actual successor process creation");
  cases.back()["directReturnContinuation"]={{"oldSettlementEvent",settledAt},{"successorCreateEvent",createdAt},{"newEpoch",g_inputHostEpoch}};
 }
 if(persona=="elite"&&family!=0&&!resumedDuringDrain){
  require(incident.pending&&!incident.spent&&!incident.recoveryRequestId&&g_inputHostRecoveryEpisodeAttempts.load()==beforeBudget&&g_virtualStartProtection.view(startProtectionClock).requestId==oldTicket&&finalCount(oldTicket,"success")==oldSuccesses,
   "source-pending runtime return keeps original successful period and spends no replacement ticket or retry budget");
  cases.back()["sourcePendingBeforeRestart"]={{"pending",incident.pending},{"spent",incident.spent},{"newRequest",incident.recoveryRequestId},{"budget",g_inputHostRecoveryEpisodeAttempts.load()},{"oldTicket",oldTicket}};
 }
 if(persistent){
  g_inputHostRecoveryEpisodeAttempts=2;
  // Exhausted episode with an already empty rolling window: isolate the 30s wave.
  g_inputHostRecoveryRolling[0]=0;g_inputHostRecoveryRolling[1]=0;
  g_inputHostRecoveryTerminalLogged=true;
  for(unsigned n=0;n<25;++n){advance(100);(void)fresh(family);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();}
  require(incident.pending&&!incident.spent&&incident.retryAt&&fullPipeSpawnCount==creates&&g_inputHostRecoveryEpisodeAttempts.load()==2,
   "exhausted episode retains demand without ticket, refund or 100ms hot queue");
  const auto retryAt=incident.retryAt;const auto eventCount=fixtureEvents.size();
  for(unsigned n=0;n<25;++n){advance(100);(void)fresh(family);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();}
  require(incident.retryAt==retryAt&&fullPipeSpawnCount==creates&&fixtureEvents.size()<eventCount+100,
   "maintenance does not renew wave deadline or repeatedly enqueue a cooled-down action");
  if(boundary=="persistent-off"){
   const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
   require(writeTarget(current,targetDocument("disabled",current["input"]["revision"].get<int>()+1)).value("ok",false),"real OFF supersedes deferred runtime demand");
   inputCaptureRefreshRealStickSettings(true);
   const auto offTicket=g_virtualStartProtection.view(startProtectionClock).requestId;
   require(offTicket!=oldTicket&&g_virtualStartProtection.view(startProtectionClock).persona=="disabled","persistent OFF owns its distinct real closing request");
   inputHostStop("virtual-target-disabled");fullPipeQuarantineProbe();fullPipeDrainQueuedLifecycle();
   const auto offView=g_virtualStartProtection.view(startProtectionClock);
   if(offView.locked&&offView.verificationRemainingMs)advance(offView.verificationRemainingMs);
   fullPipeVerificationStep();fullPipeVerificationStep();
   require(finalCount(offTicket,"success")==1&&finalCount(offTicket,"failure")==0&&!g_virtualStartProtection.view(startProtectionClock).locked,"persistent real OFF publishes exactly one successful closing final");
   cases.back()["periods"].push_back({{"requestId",offTicket},{"persona","disabled"},{"outcome","success"},{"finalCount",finalCount(offTicket,"success")}});
   advance(130001);inputHostRecoveryMaintenanceTick();
   inputHostPerformRuntimeRecovery(serial);fullPipeDrainQueuedLifecycle();
   require(!incident.pending&&!incident.monitoring&&fullPipeSpawnCount==creates&&!g_inputHostProcess,
    "OFF then elapsed backoff and old token cannot resurrect a Host");
   require(finalCount(oldTicket,"success")==1&&finalCount(oldTicket,"failure")==0,"cancelled persistence never rewrites old successful final");
   require(oldObserver.close(),"cancelled persistence closes exact old observer");
   fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureRuntimeRecoveryOsEnabled=false;
   cases.back()["passed"]=true;fixtureFullPipe=false;return;
  }
  // Time alone does not invent source freshness or refund the old episode.
  const auto now=GetTickCount64();require(retryAt>now,"persistent first wave is still inside its 30s backoff");
  advance(retryAt-now-1);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  require(fullPipeSpawnCount==creates&&incident.pending&&g_inputHostRecoveryEpisodeAttempts.load()==2,"persistent wave cannot restart at backoff deadline minus one millisecond");
  advance(1);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  require(fullPipeSpawnCount==creates&&incident.pending&&g_inputHostRecoveryEpisodeAttempts.load()==2,
   "elapsed cooldown with stale source does not create Host or refund attempts");
  cases.back()["persistentWave"]={{"retryAt",retryAt},{"attemptsBeforeNewWave",2},{"oldFinalImmutable",true}};
  for(const unsigned fallback:{1u,3u}){
   const auto saved=g_inputHostRecoveryFallbackState.load();g_inputHostRecoveryFallbackState=fallback;
   bool ready=false;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);ready=ymcc::inputHost::runtimeRecovery::waveReady(GetTickCount64());}
   g_inputHostRecoveryFallbackState=saved;
   require(!ready,"persistent wave never exempts fallback 1 or unproven-return fallback 3");
  }
 }
 cases.back()["oldRuntimeReturn"]={{"settled",settled},{"oldTicket",oldTicket},{"oldEpoch",oldEpoch},{"oldRun",oldRun},{"oldInstance",oldInstance},{"oldProcessWait",WaitForSingleObject(observed,0)},{"createsBeforeRestart",fullPipeSpawnCount-creates}};
 const auto admitReturnedSource=[&](){
 if(family!=0){
  bool admitted=false;json reads=json::array();
  for(unsigned pass=0;pass<3&&!admitted;++pass){
   {const ULONGLONG probeNow=GetTickCount64();const int probeSrc=kGpReadBus;const bool probeCostOn=false;
    const auto diagnosticReadPower=currentPowerGeneration();const auto diagnosticReadTopology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
    #include "xinput_discovery.inc"
   }
   admitted=inputHostAdmitSelectedPhysicalSource();reads.push_back({{"pass",pass},{"admitted",admitted},{"pendingFullGuard",g_xinputSlotFullScanOnce.load()},{"completedBoundary",g_xinputPreTargetBoundaryScanned.load()},{"physicalMask",xinputPhysicalConnectedMask()},{"selectedSourceMask",g_xinputPhysicalSourceMask}});
  }
  require(admitted&&g_xinputPhysicalSourceLocked&&g_xinputPhysicalSourceMask==1&&xinputPhysicalConnectedMask()==1,"runtime continuation receives fresh canonical physical source admission without forced masks");
  cases.back()["postReturnSourceAdmission"]={{"actualDiscoveryLoop",true},{"actualPhysicalAdmissionConsumer",true},{"passes",reads}};
 }
 else cases.back()["postReturnSourceAdmission"]={{"scope","original MSI Start arms and admits DInput; no forced XInput source masks"},{"sourceModeBeforeStart",msiDinputMainSourceEligible()?"msi-dinput":"physical-xinput"}};

 };
 if(wallGapMs){
  bool gapPipeReturned=false;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);gapPipeReturned=inputHostPipeRetirementProvenLocked();}
  require(!g_inputHostProcess&&!g_inputHostEpoch&&g_inputHostRunId.empty()&&!g_inputHostServerPipe&&
    gapPipeReturned&&settled&&incident.pending&&!incident.recoveryRequestId,
    "real old managed Host pipe and target have returned before actual no-Host interval");
  const auto gapPower=currentPowerGeneration(),gapDemand=g_inputHostRecoveryIntent.revision();
  const auto gapPeriod=g_virtualStartProtection.view(startProtectionClock);const auto gapAttempts=g_inputHostRecoveryEpisodeAttempts.load();
  const auto wallBegin=std::chrono::steady_clock::now();
  ::SleepEx(wallGapMs,FALSE); // Actual Windows wait; no whole-machine CPU load.
  const auto actualGap=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-wallBegin).count());
  startProtectionClock+=actualGap; // Guard clock consumes real elapsed; uptime already advanced, no offset refund/double charge.
  require(actualGap>=wallGapMs&&actualGap<wallGapMs+5000,"actual5or10second no-Host window is measured and bounded");
  inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  require(!g_inputHostProcess&&!g_inputHostEpoch&&fullPipeSpawnCount==creates&&incident.pending&&
    g_inputHostRecoveryEpisodeAttempts.load()==gapAttempts&&currentPowerGeneration()==gapPower&&
    g_inputHostRecoveryIntent.revision()==gapDemand&&g_virtualStartProtection.view(startProtectionClock).requestId==gapPeriod.requestId&&
    g_realStickTest.virtualTargetEnabled&&inputHostConfiguredPersona()==persona,
    "actual wait retains same ON demand public power original request and unchanged recovery quota");
  cases.back()["actualNoHostGap"]={{"requestedMs",wallGapMs},{"actualElapsedMs",actualGap},
    {"guardClockChargedMs",actualGap},{"realOsWait",true},{"oldProcessAndPipeAlreadyReturned",true},
    {"expectedPauseLeaf","original shortcut recording pause; no product settings"},
    {"episodeAttemptsBefore",gapAttempts},{"episodeAttemptsAfter",g_inputHostRecoveryEpisodeAttempts.load()},
    {"periodAtGap",gapPeriod.requestId},{"powerAtGap",gapPower},{"notWholeMachineCpuLoad",true}};
  g_shortcutRecordingActive=false;
 }
 admitReturnedSource();
 // All runtime cases consume the same production one-second observation tick.
 advance(1001);
 (void)fresh(family==0&&!msiDinputMainSourceEligible()?1:family);
 if(failedRestart){
  // One existing OS pipe error or one existing real PREPARE ACK-loss port.
  // The actual managed process, Start failure, cleanup and verifier run intact.
  fixtureRuntimeRecoveryFaultConsumed=false;
  fixtureStartupTransportError=ERROR_SEM_TIMEOUT;
  fixtureRuntimeRecoveryFaultEnabled=boundary=="persistent-start-failure";
  fixturePrepareAckLoss=boundary=="persistent-prepared-failure";
  fixturePrepareAckLossConsumed=false;
 }
 inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();

 if(delayedSuccessor){
  fixtureRuntimeRecoveryFirstHelloOnly=false;fixtureRuntimeRecoveryFaultEnabled=false;fixtureStartupTransportError=ERROR_SUCCESS;
  auto failedRequest=incident.recoveryRequestId;const auto failedEpoch=incident.hostEpoch;
  require(fixtureRuntimeRecoveryFaultConsumed&&failedRequest&&failedRequest!=oldTicket&&incident.monitoring&&
    fullPipeSpawnCount==creates+1&&failedEpoch!=oldEpoch,
    "actual recovery Start creates exact tuple before first HELLO connect failure");
  const auto failedHost=fullPipeChildObservers.back();const auto failedReceipt=fullPipeReceipts.back();
  const auto failedChild=fullPipeChildOutcome(failedHost,failedReceipt);
  require(failedChild.value("nativeObservedProcessExit",false)&&failedChild.value("finalCleanupOk",false)&&
    !failedChild.value("hasController",true)&&!failedChild.value("hasContext",true),
    "failed first HELLO child naturally returns before same-request switch fallback");
  cases.back()["managedReceipts"].push_back({{"path",W2U(failedReceipt)},{"processId",failedChild.value("processId",0u)},
    {"naturalExit",true},{"controllerReleased",true},{"contextReleased",true},{"faultScope","first-recovery-HELLO-connect-error"}});
  const auto canonicalMsiReturn=[&](const char* stage){
   if(family!=0)return;
   // Existing fake physical HID/slot leaves, real complete reader and original
   // return/reconcile owners. No debt/mask/selected identity is seeded here.
   for(unsigned pass=0;pass<3;++pass){
    advance(1001);gamepadReadState(false);xinputSlotsEnsurePreTargetBoundaryDiscovery();fullPipeQuarantineProbe();
   }
   cases.back()["terminalMsiReturnObservations"].push_back({{"stage",stage},
     {"owed",g_msiDinputReturnOwed.load()},{"episode",g_msiDinputReturnDebtEpisode.load()},
     {"request",g_virtualStartProtection.view(startProtectionClock).requestId},
     {"switchPending",g_inputHostSwitchRecovery.pending},{"hostPresent",g_inputHostProcess!=nullptr},
     {"scope","original-canonical-reader-and-return-owner;not-forced-proof"}});
  };
  if(terminalFallback&&family==0)canonicalMsiReturn("first-failed-HELLO-return");
  const auto inspection=g_virtualStartProtection.view(startProtectionClock);
  const bool samePeriodSwitch=terminalFallback&&g_inputHostSwitchRecovery.pending&&
    g_inputHostSwitchRecovery.requestId==failedRequest;
  if(terminalFallback)cases.back()["terminalRecoveryRoute"]={{"samePeriodSwitch",samePeriodSwitch},
    {"request",failedRequest},{"family",familyToken},{"source","actual-pending-request-not-family-label"}};
  require(inspection.requestId==failedRequest&&inspection.locked&&inspection.candidateOutcome=="failure"&&
    ((terminalFallback&&family!=1)||(g_inputHostSwitchRecovery.pending&&g_inputHostSwitchRecovery.requestId==failedRequest)),
    "current failed recovery inspection retains its request; only ROG requires its original switch fallback");
  if(inspection.verificationRemainingMs)advance(inspection.verificationRemainingMs);
  admitReturnedSource();
  if(terminalFallback&&family==0)gamepadReadState(false);else (void)fresh(family);
  if(terminalFallback&&!samePeriodSwitch){
   fullPipeVerificationStep();fullPipeVerificationStep();
   require(finalCount(failedRequest,"failure")==1&&finalCount(failedRequest,"success")==0,
     "non-ROG failed recovery keeps its actual failure before a distinct runtime wave");
   inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
   const auto retryAt=incident.retryAt,now=GetTickCount64();
   require(retryAt>now,"non-ROG second attempt retains original runtime backoff deadline");
   advance(retryAt-now+1001);admitReturnedSource();(void)fresh(family);
  }
  if(terminalFallback){
   fixtureRuntimeRecoveryFirstHelloOnly=true;fixtureRuntimeRecoveryFaultEnabled=true;
   fixtureRuntimeRecoveryFaultConsumed=false;fixtureStartupTransportError=ERROR_SEM_TIMEOUT;
  }
  for(unsigned pass=0;pass<5&&fullPipeSpawnCount<creates+2;++pass){
   inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
   if(fullPipeSpawnCount<creates+2){advance(1001);admitReturnedSource();
    if(terminalFallback&&family==0)gamepadReadState(false);else (void)fresh(family);fullPipeQuarantineProbe();}
  }

  if(terminalFallback){
   if(!samePeriodSwitch){require(incident.recoveryRequestId&&incident.recoveryRequestId!=failedRequest,
     "non-ROG second actual runtime wave owns a distinct bounded request");failedRequest=incident.recoveryRequestId;}
   fixtureRuntimeRecoveryFirstHelloOnly=false;fixtureRuntimeRecoveryFaultEnabled=false;fixtureStartupTransportError=ERROR_SUCCESS;
   require(fixtureRuntimeRecoveryFaultConsumed&&fullPipeSpawnCount==creates+2&&g_inputHostRecoveryEpisodeAttempts.load()==beforeBudget+2,
     "two actual failed HELLO Starts exhaust exactly the shared runtime and switch fallback attempts");
   const auto secondHost=fullPipeChildObservers.back();const auto secondReceipt=fullPipeReceipts.back();
   const auto second=fullPipeChildOutcome(secondHost,secondReceipt);
   require(second.value("nativeObservedProcessExit",false)&&second.value("finalCleanupOk",false)&&
     !second.value("hasController",true)&&!second.value("hasContext",true),
     "actual second failed recovery child returns its own process controller and context");
   cases.back()["managedReceipts"].push_back({{"path",W2U(secondReceipt)},{"processId",second.value("processId",0u)},
     {"naturalExit",true},{"controllerReleased",true},{"contextReleased",true},{"faultScope","second-failed-HELLO-shared-budget"}});
   if(family==0)canonicalMsiReturn("second-failed-HELLO-return");
   if(!samePeriodSwitch){
    const auto secondInspection=g_virtualStartProtection.view(startProtectionClock);
    require(secondInspection.requestId==failedRequest&&secondInspection.candidateOutcome=="failure",
      "second failed actual runtime Start retains its own normal inspection");
    if(secondInspection.verificationRemainingMs)advance(secondInspection.verificationRemainingMs);
   }
   for(unsigned pass=0;pass<6&&!g_inputHostRecoveryTerminalLogged.load();++pass){
    if(terminalFallback&&family==0)gamepadReadState(false);else (void)fresh(family);
    fullPipeVerificationStep();inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
    if(!g_inputHostRecoveryTerminalLogged.load()){advance(1001);fullPipeQuarantineProbe();}
   }
   require(finalCount(failedRequest,"failure")==1&&finalCount(failedRequest,"success")==0&&
     g_inputHostRecoveryTerminalLogged.load()&&!incident.pending&&!incident.monitoring&&incident.spent,
     "failed current recovery final hands once to actual shared terminal and retires old runtime waves");
   namespace physicalFallback=ymcc::inputHost::recoveryFallback;
   const auto fallbackSerial=physicalFallback::request.serial;
   require(fallbackSerial&&physicalFallback::request.request==failedRequest&&physicalFallback::request.persona==persona,
     "actual shared fallback captures exact failed player request and selected persona");
   for(unsigned pass=0;pass<6;++pass){
    advance(1001);
    // Original discovery/classification and canonical read follow the actual
    // Stop. Never seed a mask, clear a debt, or claim a different physical root.
    if(physicalFallback::request.stopInvoked){
     admitReturnedSource();
     // The original SDK-free snapshot OS leaf must supply a readable
     // configuration after the real Stop has returned. A foreign hidden
     // path is deliberately retained; no product journal/debt is cleared.
     require(!g_hidHideVisibilityJournal.active,"post-Stop CLI observation follows actual journal release");
     fixtureOwnedCliBoundary=true;
     fixtureOwnedCliHidden={L"HID\\VID_F00D&PID_0001\\FOREIGN_UNRELATED"};
     if(family!=0)gamepadReadState(false); // Original complete canonical reader.
     else {gamepadReadState(false);xinputSlotsEnsurePreTargetBoundaryDiscovery();}
    }else (void)fresh(family);
    inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();fullPipeQuarantineProbe();
    if(g_inputHostRecoveryFallbackState.load()==2)break;
   }
   const auto stopCount=std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"stop:recovery-exhausted-physical-fallback");
   unsigned terminalEvents=0;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-recovery-exhausted")++terminalEvents;
   require(stopCount==1&&terminalEvents==1&&physicalFallback::request.stopInvoked,
     "one shared terminal produces one actual serialized fallback Stop");
   require(!g_inputHostProcess&&!g_inputHostEpoch&&!g_inputHostHello&&!g_inputHostPrepared&&
     g_inputHostRecoveryEpisodeAttempts.load()==beforeBudget+2&&fullPipeSpawnCount==creates+2,
     "final physical fallback stops virtual output and does not create a replacement or refund attempts");
   const auto outcome=g_inputHostRecoveryFallbackState.load();
   require(outcome==2||outcome==3,"physical result is proven native source or explicitly unknown, never inferred from empty hidden list");
   if(family!=0)require(outcome==2,"ROG and GPD actual Stop must complete owned return and post-Stop canonical same-selected physical read");
   if(outcome==2){
    bool proven=false;for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-recovery-fallback"){
     const auto& detail=event["detail"];
     if(detail.value("state","")=="owned-return-proven")proven=detail.value("ownedCleanupProven",false)&&detail.value("ownedVisibilityRestored",false)&&detail.value("freshPhysicalRead",false);
    }
    require(proven,"physical success requires actual cleanup visibility and fresh same-selected source receipt");
   }
   const auto selectedBefore=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
   require(g_realStickTest.virtualTargetEnabled&&inputHostConfiguredPersona()==persona&&selectedBefore["input"]["outputTarget"]["persona"]==persona,
     "physical fallback preserves player selection and configured ON intent");
   for(unsigned stale=0;stale<8;++stale){inputHostPerformRuntimeRecovery(serial);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();}
   require(fullPipeSpawnCount==creates+2&&g_inputHostRecoveryEpisodeAttempts.load()==beforeBudget+2,
     "eight old runtime tokens cannot restart after shared final handoff");
   inputHostRecoveryPhysicalFallback();inputHostRecoveryPhysicalFallback(fallbackSerial+1);inputHostRecoveryPhysicalFallback(fallbackSerial);
   require(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"stop:recovery-exhausted-physical-fallback")==1&&
     fullPipeSpawnCount==creates+2&&!g_inputHostProcess,
     "bare wrong and completed fallback tokens cannot repeat Stop or create a new Host");
   cases.back()["terminalFallback"]={{"request",failedRequest},{"serial",fallbackSerial},{"persona",persona},
     {"resultState",outcome},{"terminalEvents",terminalEvents},{"fallbackStopCount",stopCount},{"episodeAttempts",g_inputHostRecoveryEpisodeAttempts.load()},
     {"nativeSourceProven",outcome==2},{"externalGameInputProven",false},{"sourceUnknownIsNotSuccess",true}};
   const auto off=targetDocument("disabled",selectedBefore["input"]["revision"].get<int>()+1);
   require(writeTarget(selectedBefore,off).value("ok",false),"explicit new OFF can follow failed terminal fallback without rewriting its result");
   fixtureOwnedCliBoundary=false;
   inputCaptureRefreshRealStickSettings(true);inputHostStop("virtual-target-disabled");fullPipeQuarantineProbe();fullPipeDrainQueuedLifecycle();
   require(!g_inputHostProcess&&fullPipeChildObservers.empty(),"terminal case returns all actual owned child observers");
   require(oldObserver.close(),"terminal case closes its exact extra old Host observer");
   fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureRuntimeRecoveryOsEnabled=false;fixtureClassifiedReturnReleasedEpoch=0;
   fixtureRogInitialIdentityEnabled=false;
   cases.back()["passed"]=true;fixtureFullPipe=false;return;
  }
  require(fullPipeSpawnCount==creates+2&&g_inputHostProcess&&g_inputHostHello&&g_inputHostPrepared&&
    incident.recoveryRequestId==failedRequest&&incident.monitoring&&incident.hostEpoch==g_inputHostEpoch&&
    incident.hostEpoch!=failedEpoch&&incident.runId==g_inputHostRunId&&incident.hostInstanceId==g_inputHostHostInstanceId,
    "real switch fallback Start and authenticated HELLO bind their actual same-request successor to runtime");
  require(g_inputHostRecoveryEpisodeAttempts.load()==beforeBudget+2,
    "runtime and original switch fallback charge exactly their existing attempts without refund");
  cases.back()["delayedSuccessor"]={{"request",failedRequest},{"firstFailedEpoch",failedEpoch},
    {"successorEpoch",g_inputHostEpoch},{"successorRun",g_inputHostRunId},{"successorInstance",g_inputHostHostInstanceId},
    {"actualOriginalStart",true},{"actualSwitchFallback",true},{"realManagedProcessAndPipe",true},
    {"connectFailureLeafControlled",true},{"sdkAndPhysicalLeavesFake",true}};
 }
 if(failedRestart){
  fixtureRuntimeRecoveryFaultEnabled=false;fixtureStartupTransportError=ERROR_SUCCESS;fixturePrepareAckLoss=false;
  const auto rejectedTicket=incident.recoveryRequestId;
  require(rejectedTicket&&rejectedTicket!=oldTicket&&incident.monitoring&&fullPipeSpawnCount==creates+1,
   "failed recovery Start owns a distinct real period and exactly one real managed child");
  require(boundary=="persistent-start-failure"?fixtureRuntimeRecoveryFaultConsumed:fixturePrepareAckLossConsumed,
   "requested existing OS failure or actual prepared ACK-loss port really fired");
  const auto failedChild=fullPipeChildObservers.back();const auto failedFile=fullPipeReceipts.back();
  const auto rejection=g_virtualStartProtection.view(startProtectionClock);
  require(rejection.candidateOutcome=="failure"||rejection.phase=="failure"||rejection.phase=="expired",
   "failed recovery uses its actual normal failure result instead of an invented final");
  if(rejection.locked&&rejection.verificationRemainingMs)advance(rejection.verificationRemainingMs);
  fullPipeVerificationStep();fullPipeVerificationStep();
  // Let the same native return owner consume all possible failed-target duties.
  for(unsigned pass=0;pass<4;++pass){advance(1001);(void)fresh(family);fullPipeQuarantineProbe();inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();}
  const auto rejected=fullPipeChildOutcome(failedChild,failedFile);
  require(rejected.value("nativeObservedProcessExit",false)&&rejected.value("finalCleanupOk",false)&&
   !rejected.value("hasController",true)&&!rejected.value("hasContext",true),
   "failed successor naturally releases its actual managed controller and context before another attempt");
  cases.back()["managedReceipts"].push_back({{"path",W2U(failedFile)},{"processId",rejected.value("processId",0u)},
   {"persona",persona},{"naturalExit",true},{"controllerReleased",true},{"contextReleased",true},{"faultScope",boundary}});
  require(finalCount(rejectedTicket,"failure")==1&&finalCount(rejectedTicket,"success")==0&&
   incident.pending&&!incident.monitoring&&incident.retryAt&&incident.serial!=serial&&fullPipeSpawnCount==creates+1,
   "one failed recovery final retains ON demand with a fresh dispatch identity and a deferred next attempt");
  const auto nextRetry=incident.retryAt;
  for(unsigned staleToken=0;staleToken<8;++staleToken)inputHostPerformRuntimeRecovery(serial);
  require(incident.retryAt==nextRetry&&fullPipeSpawnCount==creates+1,
   "eight stale prior-attempt tokens cannot trigger or renew the failed successor's next wave");
  const auto now=GetTickCount64();require(nextRetry>now,"failed successor keeps its original slow retry deadline");
  advance(nextRetry-now-1);inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  require(fullPipeSpawnCount==creates+1,"failed successor does not retry before the next wave deadline");
  advance(1002);admitReturnedSource();(void)fresh(family);
  inputHostRecoveryMaintenanceTick();fullPipeDrainQueuedLifecycle();
  require(incident.recoveryRequestId!=rejectedTicket&&incident.recoveryRequestId!=oldTicket,
   "failed successor continues only under a third distinct original bounded period");
  cases.back()["failedRecovery"]={{"requestId",rejectedTicket},{"failureFinals",1},{"successFinals",0},{"retryAt",nextRetry},
    {"nextRequest",incident.recoveryRequestId},{"oldSerial",serial},{"newSerial",incident.serial},{"preparedAckLoss",fixturePrepareAckLossConsumed}};
 }
 const auto newTicket=incident.recoveryRequestId;const auto newEpoch=g_inputHostEpoch;const auto newRun=g_inputHostRunId;const auto newProcess=g_inputHostProcess;
 require(newTicket&&newTicket!=oldTicket&&incident.spent&&!incident.pending&&fullPipeSpawnCount==creates+replacementCount&&newProcess&&newEpoch!=oldEpoch&&newRun!=oldRun&&g_inputHostBoundPersona==persona&&g_inputHostRecoveryEpisodeAttempts.load()==(persistent?2+replacementCount:beforeBudget+(delayedSuccessor?2:1)),"proven post-final return creates exactly one charged same-persona recovery period with a distinct ticket");
 const auto runtimeStops=[](){return std::count_if(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),[](const auto& s){return s.rfind("stop:",0)==0;});};
 const auto oldSerialStops=runtimeStops();
 for(unsigned n=0;n<8;++n)require(inputHostQueueLifecycleWork(("runtime-transport-recovery:"+std::to_string(serial)).c_str()),"old runtime serial can reach the actual serialized worker after its replacement");
 fullPipeDrainQueuedLifecycle();
 require(g_inputHostProcess==newProcess&&g_inputHostEpoch==newEpoch&&g_inputHostRunId==newRun&&fullPipeSpawnCount==creates+replacementCount&&g_inputHostRecoveryEpisodeAttempts.load()==(persistent?2+replacementCount:beforeBudget+(delayedSuccessor?2:1)),"spent runtime token cannot Stop successor or create another charged replacement");
 require(runtimeStops()==oldSerialStops,"duplicate old serial emits no second actual Stop");

 if(family==0){
  require(cases.back().contains("runtimeProofLateRejectEdges")&&cases.back()["runtimeProofLateRejectEdges"].size()==9,
   "all nine declared late proof edges distinguish real cancellation from harmless snapshot publication");
  json settlements=json::array(),proofs=json::array();
  for(const auto& event:fixtureEvents){const auto& detail=event["detail"];
   if(event.value("event","")=="msi-claw-return-debt"&&detail.value("action","")=="discharged"&&detail.value("where","")=="runtime-same-owned-MSI-two-fresh-windows")settlements.push_back(detail);
   if(event.value("event","")=="input-host-runtime-recovery"&&detail.value("stage","")=="msi-return-proven")proofs.push_back(detail);
  }
  require(settlements.size()==1&&proofs.size()==1,"exact old MSI mode obligation commits once before its unique runtime return proof");
  cases.back()["runtimeMsiModeSettlement"]=settlements;cases.back()["runtimeMsiReturnProof"]=proofs;
 }

 if(delayedSuccessor){
  // Original fixture.cpp failed-start healthy-final consumer: a failure
  // candidate stays provisional after the original inspection is complete.
  // Actual accepted source + the unchanged verifier decide the only final.
  fixtureFrameFirstAck=true;json last;
  for(int n=0;n<kInputHostFreshNeutralFramesRequired+1;++n){advance(8);auto sample=fresh(family);last=fullPipeSubmitSource(sample,true,false);}
  require(last.value("hostSubmission",std::string{})=="frame-accepted"&&g_inputHostActive,
    "actual recovered successor consumes current source over its real managed pipe");
  fullPipeVerificationStep();fullPipeVerificationStep();
  const auto final=g_virtualStartProtection.view(startProtectionClock);
  require(final.requestId==newTicket&&final.phase=="success"&&!final.locked&&final.fallbackAttempts==1&&
    finalCount(newTicket,"success")==1&&finalCount(newTicket,"failure")==0,
    "original verifier resolves provisional failure to exactly one actual same-period fallback success");
  cases.back()["periods"].push_back({{"requestId",newTicket},{"persona",persona},{"outcome","success"},{"fallbackAttempts",1},{"finalCount",1}});
 }else success(family,persona,newTicket);

 require(finalCount(oldTicket,"success")==oldSuccesses&&finalCount(oldTicket,"failure")==0&&finalCount(newTicket,"success")==1&&finalCount(newTicket,"failure")==0,"runtime recovery keeps original player final immutable and publishes exactly one separate recovery success");
 if(delayedSuccessor){inputHostRecoveryMaintenanceTick();require(!incident.pending&&!incident.monitoring&&!g_inputHostRuntimeRecoveryPending.load()&&incident.recoveryRequestId==newTicket,"runtime observes one actual successful same-period fallback terminal without retirement");}

 cases.back()["runtimeRecovery"]={{"serial",serial},{"oldRequest",oldTicket},{"newRequest",newTicket},{"oldEpoch",oldEpoch},{"newEpoch",newEpoch},{"newRun",newRun},{"budgetDelta",g_inputHostRecoveryEpisodeAttempts.load()-beforeBudget},{"oldFinalCount",oldSuccesses},{"faultReply",faultReply}};
 if(repeatIncident){
  if(boundary=="persistent-repeat-after-observer")inputHostRecoveryMaintenanceTick();
  require(incident.monitoring==(boundary=="persistent-repeat-before-observer")&&
   !incident.pending&&incident.recoveryRequestId==newTicket&&g_inputHostEpoch==newEpoch,
   "repeat fault starts on the real successful replacement at its selected observer boundary");
  const auto priorSerial=incident.serial;const auto faultCalls=fixtureRuntimeRecoveryFaultCalls;
  fixtureRuntimeRecoveryFaultEnabled=true;fixtureRuntimeRecoveryFaultConsumed=false;fixtureStartupTransportError=ERROR_SEM_TIMEOUT;
  auto repeatSource=fresh(family);const auto repeatReply=fullPipeSubmitSource(repeatSource,true,false);
  fixtureRuntimeRecoveryFaultEnabled=false;fixtureStartupTransportError=ERROR_SUCCESS;
  require(fixtureRuntimeRecoveryFaultConsumed&&fixtureRuntimeRecoveryFaultCalls==faultCalls+1&&
   incident.pending&&incident.serial!=priorSerial&&!incident.recoveryRequestId&&
   incident.requestId==newTicket&&incident.hostEpoch==newEpoch&&incident.runId==newRun,
   "actual second post-final pipe failure offers a new incident instead of deduplicating the recovered Host forever");
  inputHostPerformRuntimeRecovery(priorSerial);
  require(g_inputHostEpoch==newEpoch&&fullPipeSpawnCount==creates+replacementCount&&!incident.stopInvoked,
   "the prior recovery token cannot stop the newly faulted successor incident");
  cases.back()["repeatedIncident"]={{"boundary",boundary},{"newSerial",incident.serial},{"priorSerial",priorSerial},
   {"request",newTicket},{"hostEpoch",newEpoch},{"reply",repeatReply},{"newAttemptStarted",false}};
  fixtureClassifiedReturnReleasedEpoch=newEpoch;
 }
 close(family,persona,1,repeatIncident);
 if(repeatIncident)inputHostRecoveryMaintenanceTick();
 require(fullPipeSpawnCount==creates+replacementCount&&fullPipeChildObservers.empty()&&!g_inputHostProcess&&!g_inputHostEpoch&&!g_inputHostReleaseQuarantined,"runtime recovery complete OFF leaves no owned child epoch or quarantine");
 require(oldObserver.close(),"runtime closes its extra exact old-process observation handle");
 fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureRuntimeRecoveryOsEnabled=false;fixtureClassifiedReturnReleasedEpoch=0;
 cases.back()["passed"]=true;fixtureFullPipe=false;
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
 require(std::strcmp(family,"onex")==0||std::strcmp(family,"aokzoe")==0,"family-lifecycle rejects an unknown directed family");return true;
}
static bool aokzoeFamilyRequested(){const auto* family=std::getenv("YMCC_FIXTURE_FAMILY");return family&&std::strcmp(family,"aokzoe")==0;}
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
  const auto savedIdentity=g_machineIdentity;
  unsigned aokzoeCount=0;
  for(const auto& row:devices){
   if(!ymcc::capabilities::onexVirtual::detail::aokzoeClass(row.consumerClass))continue;
   const auto binding=ymcc::capabilities::onexVirtual::bind(row.consumerClass);
   require(row.factoryInstantiable&&binding.model==&row&&!binding.source&&isAokzoe(binding),"AOKZOE exact factory binding preserves its catalog identity");
   verify(binding,row.consumerClass,row.deviceId,"aokzoe-model-only");++aokzoeCount;
   const auto copied=*binding.model;require(!Binding{&copied,nullptr}.known(),"copied AOKZOE row cannot forge catalog membership");
   g_machineIdentity.resolved=true;g_machineIdentity.deviceClass=std::string(row.consumerClass);g_machineIdentity.family=YmccFamily::Aokzoe;
   require(inputHostOneXMachineEligible(),"AOKZOE family and exact concrete class admit shared ordinary Start");
   g_machineIdentity.family=YmccFamily::OneXPlayer;require(!inputHostOneXMachineEligible(),"AOKZOE class cannot borrow OneX family identity");
  }
  require(aokzoeCount==4,"exactly four existing concrete AOKZOE factories are admitted");
  g_machineIdentity.family=YmccFamily::Aokzoe;g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerApex";
  require(!inputHostOneXMachineEligible(),"OneX class cannot borrow AOKZOE identity");
  for(const char* cls:{"HandheldCompanion.Devices.OneXAOKZOE","HandheldCompanion.Devices.AOKZOEA9","HandheldCompanion.Devices.AOKZOEA1Unknown"}){
   g_machineIdentity.deviceClass=cls;require(!inputHostOneXMachineEligible(),"abstract, unknown and prefix-only AOKZOE classes remain unsupported");
  }
  g_machineIdentity=savedIdentity;cases.back()["aokzoeConcreteModels"]=aokzoeCount;
}
// Canonical DMI/persona parameters consumed by both the real cases and the
// existing runner's exact selection. No new runner or test identity is added.
struct OneXLifecycleModel {const char* name;const char* product;const char* deviceClass;};
static constexpr OneXLifecycleModel oneXLifecycleModels[]={
 {"Apex","ONEXPLAYER APEX","HandheldCompanion.Devices.OneXPlayerApex"},
 {"X2","ONEXPLAYER X2","HandheldCompanion.Devices.OneXPlayerX2"},
 {"OneX3","ONEXPLAYER 3","HandheldCompanion.Devices.OneXPlayer3"}
};
static constexpr OneXLifecycleModel aokzoeLifecycleModels[]={
 {"A1","AOKZOE A1 AR07","HandheldCompanion.Devices.AOKZOEA1"},
 {"A1Pro","AOKZOE A1 PRO","HandheldCompanion.Devices.AOKZOEA1Pro"},
 {"A1X","AOKZOE A1X","HandheldCompanion.Devices.AOKZOEA1X"},
 {"A2","AOKZOE A2 PRO","HandheldCompanion.Devices.AOKZOEA2"}
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
 require(request.value("repairRevision","")=="VC-ONEX-SELECTED-CHILD-20261105-R1"&&boundary.is_object()&&
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
static void oneXManagedLifecycleCases140(bool aokzoe=false,bool parameterizedOnly=false){
  const int family=aokzoe?6:3;const std::string prefix=aokzoe?"aokzoe":"onex";
  const auto* models=aokzoe?aokzoeLifecycleModels:oneXLifecycleModels;
  const auto modelCount=aokzoe?std::size(aokzoeLifecycleModels):std::size(oneXLifecycleModels);
  const char* manufacturer=aokzoe?"AOKZOE":"ONE-NETBOOK TECHNOLOGY CO., LTD.";
 require(fspath::is_regular_file(fixtureManagedDll),"directed OneX requires the current owned SDK-free managed fixture");
 for(const bool initialPending:{false,true})for(size_t modelIndex=0;modelIndex<modelCount;++modelIndex)for(const auto* persona:oneXLifecyclePersonas){
   const auto& model=models[modelIndex];
  if(initialPending && std::strcmp(persona,"elite")==0)continue; // Existing Elite requires an admitted physical source before Host creation.
  const ControlledDmi dmi={manufacturer,model.product,model.deviceClass,nullptr,model.product,nullptr};
  const bool apex=!initialPending&&modelIndex==0;
  const bool freshParameterized=apex&&std::strcmp(persona,"steamdeck")==0;
  const bool selectedParameterized=apex&&std::strcmp(persona,"elite")==0;
  const bool parameterized=freshParameterized||selectedParameterized;
  if(parameterizedOnly&&!parameterized)continue;
  const auto variantCount=freshParameterized?sizeof(oneXFreshVariants)/sizeof(oneXFreshVariants[0]):
   selectedParameterized?sizeof(oneXSelectedVariants)/sizeof(oneXSelectedVariants[0]):1u;
  for(unsigned variant=0;variant<variantCount;++variant){
  prepare(family,prefix+"/lifecycle/"+(initialPending?"pending/":"")+model.name+"/"+persona,&dmi,variant==0);
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
  try{startFirst(family,persona);}catch(...){
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
  close(family,persona);
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
 const auto& model=models[0];
 const ControlledDmi dmi={manufacturer,model.product,model.deviceClass,nullptr,model.product,nullptr};
 for(const auto* variant:oneXDeadlineVariants){
 const bool returnedStale=std::strcmp(variant,"returned-child-stale")==0;
 const bool boundaryUnconsumed=std::strcmp(variant,"boundary-scan-not-consumed")==0;
 const bool readDeadline=!returnedStale&&!boundaryUnconsumed;
 prepare(family,prefix+"/lifecycle/deadline-read-stage",&dmi,readDeadline);
 fixtureOneXShape=returnedStale?"returned-stale":boundaryUnconsumed?"flow":"deadline-read-stage";
 if(boundaryUnconsumed)fixtureOneXBoundaryVariant=variant;
 if(readDeadline)cases.back()["subvariants"]=json::array();
 cases.back()["passed"]=false;cases.back()["subvariants"].push_back({{"variant",variant},{"passed",false}});
 auto& observation=cases.back()["subvariants"].back();const auto failureBegin=failures.size();
 const auto originalSource=fixtureOneXSource();
 observation["sourceBefore"]={{"root",originalSource.baseContainerDeviceInstanceId},{"hid",originalSource.deviceInstanceId},{"identity",originalSource.physicalIdentity}};
 const auto doc=targetDocument("steamdeck",0);require(sgWriteFileAtomic(YM_SETTINGS_FILE,doc.dump()),"deadline case seeds its private persona request");
 inputCaptureRefreshRealStickSettings(true);observeSource(family,"steamdeck");
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
static void oneXFamilyHelperCases140(bool aokzoe=false,bool parameterizedOnly=false){
 const char* family=aokzoe?"aokzoe":"onex";
 const auto begin=cases.size();
 const auto selected=[parameterizedOnly](const OneXHelperCase& test){
  return !parameterizedOnly||std::strcmp(test.name,"onex/helper/unbound-slot")==0||std::strcmp(test.name,"onex/helper/fresh-positive")==0;
 };
 const auto expected=std::count_if(std::begin(oneXHelperCases),std::end(oneXHelperCases),selected);
 for(const auto& test:oneXHelperCases){
  if(!selected(test))continue;
  require(fullPipeChildObservers.empty()&&!g_inputHostProcess,"OneX independent helper case starts without an owned Host");
  seed();fixtureFullPipe=false;fixtureOneXEnabled=true;
  fixtureOneXShape=std::string(test.name).substr(std::strlen("onex/helper/"));
  cases.push_back({{"name",std::string(family)+std::string(test.name).substr(4)},{"suite","family-lifecycle"},{"familySelection",family},
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
  g_machineIdentity.manufacturer=aokzoe?"AOKZOE":"ONE-NETBOOK TECHNOLOGY CO., LTD.";
  g_machineIdentity.product=aokzoe?"AOKZOE A1 AR07":"ONEXPLAYER APEX";g_machineIdentity.systemProduct=g_machineIdentity.product;
  ymccResolveFamily(g_machineIdentity);
  require(g_machineIdentity.resolved&&g_machineIdentity.family==(aokzoe?YmccFamily::Aokzoe:YmccFamily::OneXPlayer)&&
   g_machineIdentity.deviceClass==(aokzoe?"HandheldCompanion.Devices.AOKZOEA1":"HandheldCompanion.Devices.OneXPlayerApex"),"actual family DMI resolver supplies the directed helper class");
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
 require(cases.size()-begin==static_cast<size_t>(expected),"direct OneX selection executes exactly its declared canonical scope");
}
} // namespace personaLifecycle140

static void familyLifecycleCases140(bool oneXParameterizedOnly=false){
 if(oneXParameterizedOnly){const auto* family=std::getenv("YMCC_FIXTURE_FAMILY");if(!family||std::strcmp(family,"onex")!=0)throw std::invalid_argument("onex-parameterized requires exact onex family");}
 if(personaLifecycle140::oneXFamilyRequested()){const bool aokzoe=personaLifecycle140::aokzoeFamilyRequested();personaLifecycle140::oneXFamilyHelperCases140(aokzoe,oneXParameterizedOnly);personaLifecycle140::oneXManagedLifecycleCases140(aokzoe,oneXParameterizedOnly);return;}
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
