// Actual target-return production hooks run in main.inert.cpp. These functions
// only observe private record I/O and drive the existing real coordinator path.
namespace fixtureTargetReturn {
namespace duty=ymcc::targetReturn;
static duty::LoadResult disk(){return duty::load(fixtureTargetReturnRecordPath(),inputHostTargetReturnMachineBinding());}
static json summary(const duty::LoadResult& loaded){
 json out={{"loadState",static_cast<unsigned>(loaded.state)},{"reason",loaded.reason},{"win32Error",loaded.win32Error}};
 if(loaded.record){out["revision"]=loaded.record->revision;out["origins"]=json::array();for(const auto& item:loaded.record->obligations)out["origins"].push_back({{"transactionId",item.origin.transactionId},{"parentPid",item.origin.parent.pid},{"parentBirth",item.origin.parent.creationTime},{"hostPid",item.origin.host.pid},{"hostBirth",item.origin.host.creationTime},{"runId",item.origin.runId},{"epoch",item.origin.epoch},{"targetId",item.origin.targetId},{"persona",item.origin.persona},{"physicalRoot",item.origin.physicalRoot},{"targetMayExist",item.targetMayExist},{"hasAcceptedPin",item.acceptedPsPin.has_value()},{"paths",item.paths.size()}});}
 return out;
}
static bool current(const duty::Obligation& item){
 const auto parent=duty::process::capture(GetCurrentProcess());
 const auto host=duty::process::capture(g_inputHostProcess);
 return parent&&host&&duty::detail::sameProcess(item.origin.parent,*parent)&&duty::detail::sameProcess(item.origin.host,*host)&&
  item.origin.hostInstanceId==g_inputHostHostInstanceId&&item.origin.runId==g_inputHostRunId&&item.origin.epoch==g_inputHostEpoch&&
  item.origin.targetId==kInputHostTargetId&&item.origin.persona==inputHostBoundOrCurrentPersona();
}
static std::size_t currentCount(const duty::LoadResult& loaded){if(!loaded.record)return 0;return static_cast<std::size_t>(std::count_if(loaded.record->obligations.begin(),loaded.record->obligations.end(),[](const auto& item){return current(item);}));}
static unsigned commandCount(const json& child,const std::string& command){unsigned count=0;const auto requests=child.find("requests");if(requests!=child.end()&&requests->is_array())for(const auto& request:*requests)if(request.is_object()&&request.value("command",std::string{})==command)++count;return count;}
static void preparePcCase(const std::string& name){
 seed();fixtureFullPipe=true;fullPipeFault="none";fixturePcPowerCapabilities=true;
 g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
 g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="FIXTURE-PC";g_machineIdentity.product="unregistered-desktop";
 g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_xinputAdmittedMask=0;g_xinputPhysicalSourceMask=0;g_xinputConnectedMask=0;
 g_msiMainSourceRevision=1;g_msiFeedbackModeGeneration=1;g_msiSteamDeckReleaseDebt.clear();g_msiSdOwnedPnpSnapshotForTest={};
 g_inputShortcutRuntime.clear();g_externalPriorityVirtualSuspended=false;g_inputCaptureStop=false;reg_fs();
 cases.push_back({{"name","TARGET-RETURN/PC/"+name},{"controlledFamily","unregistered ordinary PC"},{"realManagedChild",true},{"sourcePresent",false}});
 const auto on=targetDocument("dualsense-edge",0);check(sgWriteFileAtomic(YM_SETTINGS_FILE,on.dump()),"target return PC seeds an actual private settings request");inputCaptureRefreshRealStickSettings(true);
}
static duty::Record historical(){
 duty::Record record;record.machineBinding="fixture-machine";
 duty::Origin origin;origin.transactionId="historical-origin-108";origin.parent={600001,"132000000000000101"};origin.host={600002,"132000000000000102"};
 origin.hostInstanceId="historical-host-108";origin.runId="historical-run-108";origin.targetId=kInputHostTargetId;origin.persona="dualsense-edge";origin.epoch=77;
 duty::Record target,pinned;
 if(!duty::reserveTargetCreation(record,record.revision,origin,{true,true},target))throw std::runtime_error("fixture historical target transform failed");
 const duty::AcceptedPsPin pin{"USB\\VID_054C&PID_0DF2\\HISTORICAL108",{"HID\\VID_054C&PID_0DF2\\HISTORICAL108"},108};
 if(!duty::attachAcceptedPsPin(target,target.revision,origin,pin,pinned))throw std::runtime_error("fixture historical pin transform failed");
 return pinned;
}
static std::string encoded(const duty::Record& record){std::string text;if(!duty::encode(record,text))throw std::runtime_error("fixture record encode failed");return text;}
}
static void fixtureTargetReturnWireObserve(void* buffer,DWORD size){
 const auto request=json::parse(std::string(static_cast<char*>(buffer),size),nullptr,false);
 if(!request.is_object())return;const auto command=request.value("command",std::string{});
 if(command!="PREPARE_TARGET"&&command!="RECREATE_TARGET")return;
 if(fixtureColdArchive)fixtureColdBeforeCreate();
 const auto loaded=fixtureTargetReturn::disk();
 const bool exact=loaded.state==ymcc::targetReturn::LoadState::Valid&&loaded.record&&
  std::any_of(loaded.record->obligations.begin(),loaded.record->obligations.end(),[](const auto& item){return item.targetMayExist&&fixtureTargetReturn::current(item);});
 check(exact,"actual PREPARE or RECREATE wire is preceded by persisted targetMayExist for exact current owner");
 check(request.value("runId",std::string{})==g_inputHostRunId&&request.value("epoch",0ULL)==g_inputHostEpoch,"write-ahead observation binds actual serialized request tuple");
 fixtureTargetReturnObservations.push_back({{"stage","before-actual-wire-write"},{"command",command},{"caseSerial",fixtureTargetReturnCaseSerial},{"captureBasis","real107-process-handle"},{"request",request},{"disk",fixtureTargetReturn::summary(loaded)}});
}
static void fixtureTargetReturnAckLostObserve(const json& receipt){
 const auto loaded=fixtureTargetReturn::disk();
 check(loaded.state==ymcc::targetReturn::LoadState::Valid&&fixtureTargetReturn::currentCount(loaded)>0,"lost actual PREPARE ACK retains the persisted current target duty before cleanup");
 fixtureTargetReturnObservations.push_back({{"stage","actual-prepare-ack-lost"},{"caseSerial",fixtureTargetReturnCaseSerial},{"receipt",receipt},{"disk",fixtureTargetReturn::summary(loaded)}});
}
static void fixtureTargetReturnLifecycleObserve(const char* event,const json& detail){
 if(!event||std::strcmp(event,"input-host-stop-result")!=0||!detail.is_object()||
    !detail.value("targetMayExist",false)||detail.value("hostExitConfirmed",false))return;
 const auto loaded=fixtureTargetReturn::disk();const auto run=detail.value("runId",std::string{});const auto epoch=detail.value("epoch",0ULL);const auto host=detail.value("hostPid",0u);
 const bool retained=loaded.state==ymcc::targetReturn::LoadState::Valid&&loaded.record&&std::any_of(loaded.record->obligations.begin(),loaded.record->obligations.end(),[&](const auto& item){return item.targetMayExist&&item.origin.runId==run&&item.origin.epoch==epoch&&item.origin.host.pid==host;});
 check(retained,"unproven actual Host exit retains persisted target duty for exact old run epoch and PID");
 fixtureTargetReturnObservations.push_back({{"stage","stop-host-exit-unproven"},{"caseSerial",fixtureTargetReturnCaseSerial},{"stopDetail",detail},{"disk",fixtureTargetReturn::summary(loaded)}});
}
static void fixtureTargetReturnNativeCases(){
 namespace duty=ymcc::targetReturn;
 seed();cases.push_back({{"name","TARGET-RETURN/native-only/private-path-and-historical-pin"},{"captureBasis","explicit event-host identity model; no OS process claim"}});
 const auto path=fixtureTargetReturnRecordPath();check(path.find((fspath::path(g_yemanccDataDir)/L"target-return-private").wstring())==0,"target return record path stays inside explicit private fixture root");
 const auto record=fixtureTargetReturn::historical();check(duty::store(path,record).committed(),"historical pin written by actual production store");
 const auto loaded=duty::load(path,"fixture-machine");check(loaded.state==duty::LoadState::Valid&&loaded.record&&loaded.record->obligations.size()==1&&loaded.record->obligations.front().acceptedPsPin.has_value(),"actual store load preserves historical accepted pin as data");
 check(!g_psEndpointOwnership.snapshot().valid&&!g_inputHostActive&&!g_inputHostPrepared,"loading retained disk pin does not accept a current PS endpoint or output");
 cases.back()["disk"]=fixtureTargetReturn::summary(loaded);
}
static void fixtureTargetReturnFullPipeCases(){
 namespace duty=ymcc::targetReturn;
 // Ordinary PC, no physical source: target construction is permitted, while
 // historical pins and creation alone must not manufacture live input proof.
 for(const bool retainHistory:{false,true}){
  fixtureTargetReturn::preparePcCase(retainHistory?"retained-old-pin-exact-current-settlement":"source-free-normal-complete-stop");
  const auto history=fixtureTargetReturn::historical();
  check(!g_psEndpointOwnership.snapshot().valid,"historical file is not fed into current in-memory State.accept");
  const auto spawns=fullPipeSpawnCount;check(inputHostStart()&&fullPipeSpawnCount==spawns+1,"ordinary source-free PC creates exactly one owned managed target without model rejection");
  // Cold history must block a new Start. To test unrelated Stop settlement,
  // add this independent legacy origin only after the real current Start.
  if(retainHistory){
   std::lock_guard<InputHostMutex> lock(g_inputHostMx);
   auto& session=ymcc::inputHost::targetReturnRuntime::state.session;
   const auto& old=history.obligations[0];bool reserved=false,pinned=false;
   if(session&&session->ready()){
    reserved=session->reserveTarget(session->record()->revision,old.origin,{true,true});
    if(reserved)pinned=session->attachAcceptedPsPin(session->record()->revision,old.origin,*old.acceptedPsPin);
   }
   check(reserved&&pinned,"independent legacy responsibility is added through actual locked Session after current Start");
   check(old.origin.cliWriterProtocol==0,"retained independent history stays legacy unknown without producer promotion");
  }
  const auto running=fixtureTargetReturn::disk();
  check(running.state==duty::LoadState::Valid&&running.record&&fixtureTargetReturn::currentCount(running)==1,"accepted current target has exactly one persisted current-owner obligation");
  const auto pin=g_psEndpointOwnership.snapshot();
  const auto own=running.record?std::find_if(running.record->obligations.begin(),running.record->obligations.end(),[](const auto& item){return fixtureTargetReturn::current(item);}):std::vector<duty::Obligation>::const_iterator{};
  check(running.record&&own!=running.record->obligations.end()&&own->acceptedPsPin&&pin.valid&&own->acceptedPsPin->sdkRootInstanceId==pin.rootInstanceId&&own->acceptedPsPin->hidInstanceIds==pin.hidInstanceIds&&own->acceptedPsPin->bindingGeneration==pin.generation,"accepted real PS State pin is persisted after current acceptance");
  const auto ticket=g_virtualStartProtection.view(startProtectionClock);
  check(!g_inputHostActive&&ticket.phase=="pending","retained old pin and target creation cannot replace missing fresh physical input or startup success");
  std::vector<duty::Origin> oldCurrent;if(running.record)for(const auto& item:running.record->obligations)if(fixtureTargetReturn::current(item))oldCurrent.push_back(item.origin);
  inputHostStop("virtual-target-disabled");const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;
  check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false)&&!child.value("hasController",true)&&!child.value("hasContext",true),"normal target settlement is paired with actual managed cleanup and natural process exit");
  const auto stopped=fixtureTargetReturn::disk();bool removed=stopped.state==duty::LoadState::Valid&&stopped.record.has_value();
  if(stopped.record)for(const auto& origin:oldCurrent)if(std::any_of(stopped.record->obligations.begin(),stopped.record->obligations.end(),[&](const auto& item){return duty::sameOrigin(item.origin,origin);}))removed=false;
  check(removed&&!oldCurrent.empty(),"complete Stop consumes exactly the settled old current target-only origins");
  check(stopped.record&&stopped.record->obligations.size()==(retainHistory?1u:0u),"normal settlement never consumes retained historical origins");
  if(retainHistory)check(stopped.record&&stopped.record->obligations.size()==1&&duty::sameOrigin(stopped.record->obligations[0].origin,history.obligations[0].origin)&&stopped.record->obligations[0].acceptedPsPin&&duty::detail::samePin(*stopped.record->obligations[0].acceptedPsPin,*history.obligations[0].acceptedPsPin),"retained historical origin and pin remain byte-equivalent after unrelated current Stop");
  cases.back()["runningDisk"]=fixtureTargetReturn::summary(running);cases.back()["stoppedDisk"]=fixtureTargetReturn::summary(stopped);fixtureFullPipe=false;
 }
 for(const std::string variant:{"corrupt-file","exclusive-owner-conflict"}){
  fixtureTargetReturn::preparePcCase(variant);const auto path=fixtureTargetReturnRecordPath();
  HANDLE owner=INVALID_HANDLE_VALUE;
  if(variant=="corrupt-file"){std::ofstream output{fspath::path(path),std::ios::binary};output<<"{broken-108";output.close();check(bool(output),"actual corrupt record file seeded privately");}
  else {owner=CreateFileW((path+L".owner").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);check(owner!=INVALID_HANDLE_VALUE,"actual exclusive private owner handle acquired");}
  const auto beforeObservations=fixtureTargetReturnObservations.size();const auto spawns=fullPipeSpawnCount;const auto observerCount=fullPipeChildObservers.size(),receiptCount=fullPipeReceipts.size();
  const auto wireSequence=g_inputHostSequence;const auto externalTrace=g_startEntrySeam.trace;
  const bool started=inputHostStart();
  check(!started&&fullPipeSpawnCount==spawns&&fullPipeChildObservers.size()==observerCount&&fullPipeReceipts.size()==receiptCount,"unreadable cold record blocks before any owned child spawn or HELLO");
  bool creationDispatched=false;for(std::size_t i=beforeObservations;i<fixtureTargetReturnObservations.size();++i)if(fixtureTargetReturnObservations[i].value("stage",std::string{})=="before-actual-wire-write")creationDispatched=true;
  check(!creationDispatched&&g_inputHostSequence==wireSequence,"corrupt record or exclusive-owner conflict dispatches zero HELLO PREPARE or RECREATE wire commands");
  check(!g_inputHostPrepareMayHaveReachedHost&&!g_inputHostProcess&&!g_hidHideVisibilityJournal.active&&g_startEntrySeam.trace==externalTrace,"cold denial makes no target-issued claim or device-mode/visibility side effect");
  const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
  if(owner!=INVALID_HANDLE_VALUE){CloseHandle(owner);owner=INVALID_HANDLE_VALUE;}
  if(variant=="corrupt-file"){
   {std::ifstream input{fspath::path(path),std::ios::binary};const std::string bytes((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());check(bytes=="{broken-108","failed admission never overwrites corrupt historical bytes with an empty record");cases.back()["deniedOriginalBytes"]=bytes;}
   // Explicit fixture repair restores known retained duty, never an empty file.
   const auto history=fixtureTargetReturn::historical();check(duty::store(path,history).committed(),"explicit external fixture repair restores known legacy duty without clearing responsibility");
   const auto repaired=fixtureTargetReturn::encoded(history);
   startProtectionClock+=1000;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}fullPipeDrainQueuedLifecycle();
   const auto loaded=fixtureTargetReturn::disk();
   check(ymcc::inputHost::targetReturnRuntime::state.session&&ymcc::inputHost::targetReturnRuntime::state.session->ready()&&loaded.record&&fixtureTargetReturn::encoded(*loaded.record)==repaired,"actual continuation reopens repaired file and retains exact unrelated legacy responsibility");
   check(fullPipeSpawnCount==spawns&&fullPipeChildObservers.size()==observerCount&&!g_inputHostProcess&&!g_inputHostPrepareMayHaveReachedHost,"readable legacy history still blocks new child and target dispatch after recovery reentry");
  }else{
   startProtectionClock+=1000;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}fullPipeDrainQueuedLifecycle();
   check(fullPipeSpawnCount==spawns+1&&g_inputHostPrepared&&g_virtualStartProtection.view(startProtectionClock).requestId==ticket,"released owner permits exact original cold continuation to resume one actual Start without readmission");
   inputHostStop("offline-target-return-record-reentry-cleanup");
   if(fullPipeChildObservers.size()>observerCount){const auto child=fullPipeChildOutcome();cases.back()["managedReceipt"]=child;check(child.value("nativeObservedProcessExit",false)&&child.value("finalCleanupOk",false),"successful recovery reentry has its own exact managed cleanup and natural exit receipt");}
   else check(false,"successful recovery reentry must own a child receipt before observation");
  }
  cases.back()["afterRecordReentry"]=fixtureTargetReturn::summary(fixtureTargetReturn::disk());fixtureFullPipe=false;
 }
 // Existing actual pipe ACK-loss + held SDK-stop case is re-used. The new
 // read-only ACK observer asserts durable debt while exit is not yet proven.
 const auto before=fixtureTargetReturnObservations.size();fullPipePrepareAckLostCase("PC");
 bool observed=false;for(std::size_t i=before;i<fixtureTargetReturnObservations.size();++i)if(fixtureTargetReturnObservations[i].value("stage",std::string{})=="actual-prepare-ack-lost")observed=true;
 check(observed,"target return regression executes actual lost PREPARE ACK with held child cleanup and retained debt");
}
