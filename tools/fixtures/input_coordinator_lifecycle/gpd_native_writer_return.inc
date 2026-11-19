// Included only for the original full-pipe case's explicit native-writer option.
// Device responses are the existing foreground IO leaves. Writer/Session/Job,
// process exit, retirement, settings, recovery and managed transport are real.
#ifdef YMCC_FIXTURE_GPD_NATIVE_WRITER_RETURN
struct FullPipeGpdNativeWriterReturn {
 const MsiNativeForegroundTestIo savedIo=g_msiNativeForegroundTestIo;
 const decltype(g_msiForegroundTestIo.locations) savedLocations=g_msiForegroundTestIo.locations;
 const bool savedIsolation=g_controllerFrontendIsolationEnabled.load();
 const bool savedObserver=g_msiNativeForegroundObserverActive.load();
 std::vector<std::wstring> hidden{L"HID\\FOREIGN-PREHIDDEN\\OTHER-PORT"};
 std::vector<std::string> hidCalls,xusbCalls;
 bool cloak=false,transferred=false,resumed=false,zero=false,finished=false;
 HANDLE root=nullptr,primary=nullptr,observer=nullptr,job=nullptr;
 std::unique_ptr<ymcc::nativeRefreshWriter::LaunchScope> preparing;
 std::wstring path;
 std::string attempt;
 std::uint64_t startedAt=0,firstRequest=0,currentRequest=0,observationSerial=0;
 unsigned hostSpawns=0;
 json retirement;
 const std::string machine=inputHostTargetReturnMachineBinding();
 void need(bool value,const char* message){check(value,message);if(!value)throw std::runtime_error(message);}
 json& row(){return cases.back()["nativeWriterReturn"];}
 void advance(std::uint64_t ms){startProtectionClock+=ms;fixturePowerClockOffset+=ms;}
 void maintain(){msiNativeForegroundMaintenance();std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryMaintenanceLocked();}
 void begin(){
  row()={{"realWriter",false},{"syntheticCycle",false},{"writerSessionReturned",false},
   {"rootProcessCloseVerified",false},{"observerAndJobCloseVerified",false},{"newRecoveryRequest",0},
   {"attempt",""},{"lateElapsedMs",0},{"hardwareSuccessProven",false}};
  need(!g_msiNativeForegroundLease.active&&!g_msiNativeForegroundLease.cycleProcess&&
   !g_msiNativeForegroundLease.nativeRefreshWriter&&!g_msiNativeForegroundCyclePending,
   "GPD combined begins with no inherited native writer duty");
  inputCaptureRefreshRealStickSettings(true); // consume the real disabled document
  need(inputHostConfiguredPersona()=="disabled"&&!g_realStickTest.virtualTargetEnabled,
   "GPD combined native entry uses actual disabled configuration");
  g_controllerFrontendIsolationEnabled=true;g_msiNativeForegroundObserverActive=true;
  g_msiForegroundTestIo.locations=[](const std::string& candidate){return candidate==fixtureGpdPostHideRoot()?
   std::vector<std::wstring>{fixtureGpdPostHideLocation()}:std::vector<std::wstring>{};};
  auto& io=g_msiNativeForegroundTestIo;io={};
  io.ui=[](){return true;};io.queue=[](){};
  io.clock=[](){return GetTickCount64();};
  io.candidates=[](){return std::vector<HidHidePhysicalIdentity>{fixtureGpdPostHideSource()};};
  io.inventoryComplete=[](){return true;};
  io.inventory=[](const HidHidePhysicalIdentity&){HidHideInterfaceRecord r;
   r.instanceId=fixtureGpdPostHideGame();r.baseContainerDeviceInstanceId=fixtureGpdPostHideRoot();
   r.vendorId=0x045e;r.productId=0x028e;r.usagePage=1;r.usage=5;return std::vector<HidHideInterfaceRecord>{r};};
  io.xusbInventory=[](const HidHidePhysicalIdentity& source){MsiNativeForegroundXusbInventory result;
   result.complete=true;ymcc::msi::NativeForegroundXusbInterface endpoint;
   endpoint.activeInterface=endpoint.ancestorMatched=true;endpoint.instanceId=fixtureGpdPostHideRoot();
   endpoint.ancestorRoot=source.baseContainerDeviceInstanceId;result.interfaces={endpoint};return result;};
  io.snapshot=[this](HidHideVisibilitySnapshot& s){s={};s.valid=true;s.cloaking=cloak;s.inverseRaw="off";
   s.hidden=hidden;s.applications={hidHideCurrentModulePath()};return true;};
  io.visibility=[this](const std::wstring& p,bool hide){if(hide){if(!hidHideContainsPath(hidden,W2U(p)))hidden.push_back(p);}
   else hidden.erase(std::remove_if(hidden.begin(),hidden.end(),[&](const auto& v){return hidHideContainsPath({v},W2U(p));}),hidden.end());return true;};
  io.cloak=[this](bool value){cloak=value;return true;};
  io.gameRefresh=[this](const HidHidePhysicalIdentity& v,const std::wstring& location){hidCalls.push_back(v.deviceInstanceId);
   need(v.deviceInstanceId==fixtureGpdPostHideGame()&&v.baseContainerDeviceInstanceId==fixtureGpdPostHideRoot()&&
    location==fixtureGpdPostHideLocation(),"GPD combined HID leaf receives the exact physical identity");return 0;};
  io.xusbRefresh=[this](const HidHidePhysicalIdentity& v,const std::wstring& location){xusbCalls.push_back(v.deviceInstanceId);
   need(v.deviceInstanceId==fixtureGpdPostHideRoot()&&location==fixtureGpdPostHideLocation(),
    "GPD combined XUSB leaf receives the exact root and location");return 1;};
  io.poll=[](){throw std::logic_error("combined-real-writer-must-not-use-synthetic-poll");return -1;};
  msiNativeForegroundUiEvent(true);msiNativeForegroundHandleWork();
  auto& lease=g_msiNativeForegroundLease;
  need(lease.active&&lease.profile==ymcc::nativeForeground::Profile::Gpd&&lease.syntheticCycle&&
   g_msiNativeForegroundCyclePending&&hidCalls==std::vector<std::string>{fixtureGpdPostHideGame()}&&
   xusbCalls==std::vector<std::string>{fixtureGpdPostHideRoot()},
   "GPD combined real foreground consumer acknowledges HID and parks its final XUSB leaf");
  path=nativeRefreshWriterRecordPath();preparing=std::make_unique<ymcc::nativeRefreshWriter::LaunchScope>(
   path,machine,fixtureGpdPostHideRoot(),fixtureGpdPostHideRoot(),std::vector<std::string>{W2U(fixtureGpdPostHideLocation())});
  STARTUPINFOW si{};si.cb=sizeof(si);
  need(preparing->ready()&&preparing->job().prepareTracked(si,preparing->jobName(),{preparing->inheritedLease()})&&preparing->reserve(),
   "GPD combined reserves the production LaunchScope Session and exact tracked Job");
  attempt=preparing->attempt()->attemptId;row()["attempt"]=attempt;
  wchar_t self[32768]{};const auto length=GetModuleFileNameW(nullptr,self,32768);
  need(length&&length<32768,"GPD combined inert leaf binds the current fixture image");
  std::wstring command=L"\""+std::wstring(self)+L"\" --gpd-terminal-inert-leaf";PROCESS_INFORMATION child{};
  need(CreateProcessW(self,command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW|preparing->job().flags(),
   nullptr,nullptr,preparing->job().startup(),&child)!=FALSE,"GPD combined creates its exact inert child suspended");
  root=child.hProcess;primary=child.hThread;
  need(DuplicateHandle(GetCurrentProcess(),root,GetCurrentProcess(),&observer,
   PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0)!=FALSE,"GPD combined duplicates only its exact child observer");
  job=OpenJobObjectW(JOB_OBJECT_QUERY,FALSE,preparing->jobName().c_str());BOOL member=FALSE;
  need(job&&IsProcessInJob(root,job,&member)&&member,"GPD combined positively proves exact child Job membership");
  need(preparing->creationReturned(root),"GPD combined production scope records the actual created child identity");
  // The consumer above created this lease. Replace only its pending IO leaf,
  // exactly as preHostWriterCase; never construct a substitute native owner.
  lease.cycleProcess=root;lease.nativeRefreshWriter=std::move(preparing);lease.syntheticCycle=false;transferred=true;
  startedAt=GetTickCount64();hostSpawns=fullPipeSpawnCount;row()["realWriter"]=true;
  auto blocked=ymcc::nativeRefreshWriter::Session::open(path,machine);
  need(!blocked.ready(),"GPD combined live writer actually excludes a second Session");
 }
 void observe(std::uint64_t ticket){
  namespace runtime=ymcc::inputHost::runtimeRecovery;
  const auto wire=ymcc::inputHost::queuedPlayerStart::bind("initial-target-admission");
  ymcc::inputHost::queuedPlayerStart::Work work;
  need(ymcc::inputHost::queuedPlayerStart::decode(wire,work)&&work.bound&&work.request==ticket,
   "GPD combined actual queued Work binds the current settings request");
  const bool deferred=!msiNativeForegroundBeforeVirtualWork(wire);
  if(deferred)inputHostRuntimeRecoveryObserveNativeWait(work);
  need(deferred&&runtime::state.pending&&runtime::state.preHostWriterWait&&runtime::state.requestId==ticket&&
   runtime::state.nativeWriterAttempt==attempt&&g_msiNativeForegroundLease.configurationReturned&&
   hidden==std::vector<std::wstring>{L"HID\\FOREIGN-PREHIDDEN\\OTHER-PORT"},
   "GPD combined actual defer observes the same writer after verified configuration return");
  observationSerial=runtime::state.serial;
  const auto until=runtime::state.observationUntil;inputHostRuntimeRecoveryObserveNativeWait(work);
  need(runtime::state.serial==observationSerial&&runtime::state.observationUntil==until,
   "GPD combined duplicate wait cannot renew the original180second observation");
 }
 bool completeChild() noexcept {
  if(zero)return true;if(!root)return true;
  if(!resumed){if(!primary||ResumeThread(primary)==DWORD(-1))return false;resumed=true;}
  HANDLE observed=observer?observer:root;
  if(WaitForSingleObject(observed,2000)!=WAIT_OBJECT_0)return false;
  DWORD code=0;if(!GetExitCodeProcess(observed,&code)||code!=6||!job)return false;
  for(unsigned n=0;n<1000;++n){JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
   if(!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr))return false;
   if(!info.ActiveProcesses){zero=true;break;}fixtureOneXRealSleep(1);}
  return zero;
 }
 bool closeObserved(HANDLE& value) noexcept {if(!value)return true;if(!CloseHandle(value))return false;value=nullptr;return true;}
 ymcc::VirtualStartProtection::View prepareAndStart(const std::string& terminal,const json& base,json& on,json& off,bool& sourceObserved){
  namespace runtime=ymcc::inputHost::runtimeRecovery;
  begin();need(writeTarget(base,on).value("ok",false),"GPD combined first ON uses actual settings writer");
  inputCaptureRefreshRealStickSettings(true);auto first=g_virtualStartProtection.view(startProtectionClock);
  firstRequest=first.requestId;need(firstRequest&&first.phase=="pending"&&first.remainingMs==fullPipeUserMaxMs,
   "GPD combined first ON retains original thirty-second protection");observe(firstRequest);
  if(terminal=="exit"){
   need(g_virtualStartProtection.cancel(startProtectionClock),"GPD combined exit variant explicitly cancels the live first request before OFF");
   maintain();need(!runtime::state.pending&&!runtime::state.recoveryRequestId,
    "GPD combined real cancel retires only its observed recovery demand");
   // Cancel retains its original ten-second terminal protection. Do not
   // bypass the real settings writer or change the product's 30+10 policy.
   advance(g_virtualStartProtection.view(startProtectionClock).remainingMs+1);
  }else advance(first.remainingMs+1);
  need(writeTarget(on,off).value("ok",false),"GPD combined live OFF commits through the actual settings writer");
  inputCaptureRefreshRealStickSettings(true);(void)msiNativeForegroundBeforeVirtualWork("virtual-target-disabled");maintain();
  need(WaitForSingleObject(observer,0)==WAIT_TIMEOUT&&g_msiNativeForegroundLease.nativeRefreshWriter&&
   g_msiNativeForegroundLease.cycleProcess==root&&g_msiNativeForegroundCyclePending&&!runtime::state.pending&&
   !runtime::state.recoveryRequestId&&!g_inputHostProcess&&fullPipeSpawnCount==hostSpawns&&hidCalls.size()==1&&xusbCalls.size()==1,
   "GPD combined live OFF preserves exact writer resources without new Host or inverse writer");
  const auto closing=g_virtualStartProtection.view(startProtectionClock);advance(closing.remainingMs+1);
  need(!g_virtualStartProtection.view(startProtectionClock).locked,
   "GPD combined new ON waits for the real closing protection to end");
  const auto next=targetDocument("elite",3);need(writeTarget(off,next).value("ok",false),
   "GPD combined later explicit ON is admitted by the actual settings writer");on=next;off=targetDocument("disabled",4);
  inputCaptureRefreshRealStickSettings(true);auto current=g_virtualStartProtection.view(startProtectionClock);currentRequest=current.requestId;
  need(currentRequest&&currentRequest!=firstRequest,"GPD combined later ON owns a distinct actual player ticket");observe(currentRequest);
  advance(current.remainingMs+1);const auto elapsed=GetTickCount64()-startedAt;if(elapsed<=65000)advance(65001-elapsed);
  maintain();msiNativeForegroundHandleWork();current=g_virtualStartProtection.view(startProtectionClock);
  row()["firstRequest"]=firstRequest;row()["currentRequest"]=currentRequest;row()["lateElapsedMs"]=GetTickCount64()-startedAt;
  need(current.requestId==currentRequest&&current.phase=="expired"&&!current.locked&&
   WaitForSingleObject(observer,0)==WAIT_TIMEOUT&&g_msiNativeForegroundLease.nativeRefreshWriter&&
   g_msiNativeForegroundLease.cycleProcess==root&&g_msiNativeForegroundCyclePending&&!runtime::state.recoveryRequestId&&
   !g_inputHostProcess&&fullPipeSpawnCount==hostSpawns&&hidCalls.size()==1&&xusbCalls.size()==1,
   "GPD combined beyond65seconds cannot release a live writer or create Host or inverse work");
  need(completeChild(),"GPD combined actual child exits6 and exact assigned Job becomes zero before retirement");
  msiNativeForegroundMaintenance();
  // Independent immediate oracle: product Poll does not check CloseHandle's
  // return. A cleared pointer alone cannot establish successful root closure.
  DWORD flags=0;SetLastError(ERROR_SUCCESS);const bool rootGone=!GetHandleInformation(root,&flags)&&GetLastError()==ERROR_INVALID_HANDLE;
  row()["rootProcessCloseVerified"]=rootGone;
  auto& lease=g_msiNativeForegroundLease;retirement=lease.nativeRefreshWriterDiagnostic;row()["retirement"]=retirement;
  need(rootGone&&!lease.nativeRefreshWriter&&!lease.cycleProcess&&!g_msiNativeForegroundCyclePending&&lease.helperExitCode==6&&
   retirement.value("phase",std::string{})=="durably-settled"&&retirement.value("attemptId",std::string{})==attempt&&
   retirement.value("settled",false),"GPD combined actual Poll retires exact scope and independently closes the original root handle");
  root=nullptr;row()["childExitCode"]=6;row()["assignedJobActiveProcessesBeforeClose"]=0;
  // Existing diagnostic IO input only; never a hardware-success or retirement
  // proof. It describes the same Disable23 boundary as preHostWriterCase.
  lease.helperDiagnostic={{"transport","delete-on-close-stdout"},{"state","record-accepted"},
   {"scope","helper-self-reported-existing-call-facts"},{"hardwareSuccessProven",false},
   {"reportedFacts",{{"Schema",1},{"Kind","ymcc.gameRefresh.failure"},{"Result",6},{"Stage","disable"},
    {"Xusb",lease.helperXusbTarget},{"PrepareLocateResult",0U},{"CurrentLocateResult",0U},{"SameNode",true},
    {"DisableCallEntered",true},{"DisableResult",23U},{"EnableCallEntered",false},{"EnableResult",nullptr},
    {"CompensationChecked",false},{"CompensationIdentityMatched",nullptr},{"CompensationLocateResult",nullptr},
    {"CompensationSameNode",nullptr},{"CompensationAttempted",false},{"CompensationEnableResult",nullptr},
    {"CompensationExceptionType",nullptr},{"CompensationExceptionHResult",nullptr},
    {"ExceptionType","System.InvalidOperationException"},{"ExceptionHResult",-2146233079}}}};
  msiNativeForegroundHandleWork();
  need(!lease.active&&!lease.cycleProcess&&!lease.nativeRefreshWriter&&!g_msiNativeForegroundCyclePending&&
   hidCalls.size()==1&&xusbCalls.size()==1,"GPD combined original return consumer clears only the retired writer without inverse IO");
  {auto returned=ymcc::nativeRefreshWriter::Session::open(path,machine);row()["writerSessionReturned"]=returned.ready();
   need(returned.ready(),"GPD combined writer Session can actually be reacquired after durable retirement");}
  need(closeObserved(primary)&&closeObserved(observer)&&closeObserved(job),
   "GPD combined checks primary observer and exact Job CloseHandle returns");
  row()["observerAndJobCloseVerified"]=true;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
   need(ymcc::inputHost::targetReturnRuntime::open()&&ymcc::inputHost::targetReturnRuntime::state.session->record()->obligations.empty(),
    "GPD combined actual target history opens with no prior obligations before recovery");}
  const auto probeNow=GetTickCount64();if(runtime::state.nextProbe>probeNow)advance(runtime::state.nextProbe-probeNow);
  fixtureXinputSourceLive=true;row()["returnedSourceDiscovery"]=fullPipeReadPhysicalSourceForElite(false,true);
  (void)fullPipePublishCanonicalRead(2);
  fixtureGpdTransferSourceAtEnsure=[&sourceObserved](){sourceObserved=true;
   cases.back()["initialSourceDiscovery"]=fullPipeReadPhysicalSourceForElite(false,true);};
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryMaintenanceLocked();}
  need(runtime::state.queued&&runtime::state.serial==observationSerial,
   "GPD combined original runtime queues recovery only after actual resource and fresh source return");
  {FullPipeWakeIoScope io;fullPipeDrainQueuedLifecycle();}
  row()["newRecoveryRequest"]=runtime::state.recoveryRequestId;
  need(runtime::state.requestId==currentRequest&&runtime::state.nativeWriterAttempt==attempt&&runtime::state.recoveryRequestId&&
   runtime::state.recoveryRequestId!=currentRequest&&g_virtualStartProtection.view(startProtectionClock).requestId==runtime::state.recoveryRequestId&&
   g_inputHostRecoveryEpisodeAttempts.load()==1,"GPD combined actual recovery admits one distinct current ticket within original shared budget");
  return current;
 }
 void finish(){need(row().value("rootProcessCloseVerified",false)&&row().value("observerAndJobCloseVerified",false)&&
  row().value("writerSessionReturned",false)&&!root&&!primary&&!observer&&!job,
  "GPD combined all independent native resource closure observations are complete");
  need(!g_msiNativeForegroundLease.active&&!g_msiNativeForegroundLease.nativeRefreshWriter&&
   !g_msiNativeForegroundLease.cycleProcess&&!g_msiNativeForegroundCyclePending,
   "GPD combined final managed cleanup leaves no native writer owner or pending helper");
  {auto returned=ymcc::nativeRefreshWriter::Session::open(path,machine);
   need(returned.ready(),"GPD combined final managed cleanup independently returns the actual writer Session");}
  finished=true;}
 ~FullPipeGpdNativeWriterReturn() noexcept {
  // Exceptional cleanup is bounded and never contributes a PASS. Keep the
  // original lease when production cannot prove retirement; do not zero it.
  try{if(!finished&&(root||primary||observer||job)){
   check(completeChild(),"GPD combined exceptional inert child and exact Job cleanup is proven");
   if(transferred&&root&&g_msiNativeForegroundLease.cycleProcess==root)msiNativeForegroundMaintenance();
   if(!transferred&&root)check(closeObserved(root),"GPD combined untransferred root CloseHandle succeeds");
   check(closeObserved(primary),"GPD combined exceptional primary CloseHandle succeeds");
   check(closeObserved(observer),"GPD combined exceptional observer CloseHandle succeeds");
   check(closeObserved(job),"GPD combined exceptional Job observer CloseHandle succeeds");
  }}catch(...){try{check(false,"GPD combined exceptional cleanup remains unproven");}catch(...){}}
  g_msiNativeForegroundTestIo=savedIo;g_msiForegroundTestIo.locations=savedLocations;
  g_controllerFrontendIsolationEnabled=savedIsolation;g_msiNativeForegroundObserverActive=savedObserver;
 }
};
#endif
