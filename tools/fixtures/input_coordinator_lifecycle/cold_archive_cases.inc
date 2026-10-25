// Archived G3E stuck-state startup replay. The archive bytes and binding are
// supplied by the runner, never hardcoded as current OS/process facts.
// Reboot/process death and PnP observations are controlled inputs. The native
// cold owner, queue, real private persistent Session and managed pipe are real.
namespace coldArchive {
namespace duty=ymcc::targetReturn;
namespace proof=duty::pnpAbsence;
static std::string original;
static duty::Record historical;
static duty::process::State writerState=duty::process::State::Dead;
static proof::State censusState=proof::State::Absent;
static unsigned censusCalls=0,createChecks=0;
static std::string bytes(){std::ifstream f{fspath::path(fixtureTargetReturnRecordPath()),std::ios::binary};return {(std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()};}
static bool oldOriginRemains(const duty::Record& record){
 return std::any_of(record.obligations.begin(),record.obligations.end(),[](const auto& a){return std::any_of(historical.obligations.begin(),historical.obligations.end(),[&](const auto& b){return duty::sameOrigin(a.origin,b.origin);});});
}
// Real parser sees a Windows single-NUL empty STRING_LIST before another
// unrelated node, then either a supported target or a complete normal end.
struct Reader {
 DWORD lastError=0;unsigned enumerations=0,properties=0;
 HDEVINFO getClassDevs(const GUID*,PCWSTR,HWND,DWORD){return reinterpret_cast<HDEVINFO>(std::uintptr_t(0x21));}
 BOOL enumerate(HDEVINFO,DWORD i,PSP_DEVINFO_DATA data){
  ++enumerations;if(i>=3){lastError=ERROR_NO_MORE_ITEMS;return FALSE;}
  if(censusState==proof::State::Unknown&&i==2){lastError=ERROR_ACCESS_DENIED;return FALSE;}
  data->DevInst=i+1;return TRUE;
 }
 BOOL destroy(HDEVINFO){return TRUE;}
 CONFIGRET deviceId(DEVINST node,PWCHAR out,ULONG chars,ULONG){
  const auto value=L"ROOT\\FIXTURE_COLD_NODE\\"+std::to_wstring(node);
  if(value.size()+1>chars)return CR_BUFFER_SMALL;std::copy(value.begin(),value.end(),out);out[value.size()]=0;return CR_SUCCESS;
 }
 CONFIGRET hardwareIds(DEVINST node,const DEVPROPKEY*,DEVPROPTYPE* type,PBYTE out,PULONG count,ULONG){
  ++properties;*type=DEVPROP_TYPE_STRING_LIST;
  std::wstring value;
  if(node==1)value.assign(1,L'\0');
  else {value=node==3&&censusState==proof::State::Present?L"ROOT\\HIDMaestroXUSB":L"PCI\\UNRELATED";value.push_back(0);value.push_back(0);}
  const auto needed=static_cast<ULONG>(value.size()*sizeof(wchar_t));if(*count<needed){*count=needed;return CR_BUFFER_SMALL;}
  std::memcpy(out,value.data(),needed);*count=needed;return CR_SUCCESS;
 }
 DWORD error(){return lastError;}
};
static void prepare(const std::string& name,const std::string& persona){
 fixtureColdArchive=false;personaLifecycle140::prepare(0,"COLD-ARCHIVE/"+name);
 fixtureColdArchive=true;fixtureColdMachine=historical.machineBinding;
 writerState=duty::process::State::Dead;censusState=proof::State::Absent;censusCalls=createChecks=0;
 {std::ofstream out{fspath::path(fixtureTargetReturnRecordPath()),std::ios::binary};out<<original;}
 check(bytes()==original,"startup is seeded with byte-exact archived fault record, never an empty substitute");
 const auto doc=targetDocument(persona,0);check(sgWriteFileAtomic(YM_SETTINGS_FILE,doc.dump()),"private startup settings retain requested virtual persona");inputCaptureRefreshRealStickSettings(true);
 cases.back()["initialArchivedRecord"]=fixtureTargetReturn::summary(fixtureTargetReturn::disk());
 cases.back()["controlledRebootObservation"]="exact archived native and Host identity probes; no live process access";
}
static void retry(){personaLifecycle140::advance(1000);{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}fullPipeDrainQueuedLifecycle();}
static void finishOff(){
 auto view=g_virtualStartProtection.view(startProtectionClock);const auto ticket=view.requestId;
 if(view.verificationRemainingMs)personaLifecycle140::advance(view.verificationRemainingMs);
 fullPipeVerificationStep();fullPipeVerificationStep();view=g_virtualStartProtection.view(startProtectionClock);
 check(view.phase=="success"&&!view.locked&&personaLifecycle140::finalCount(ticket,"success")==1&&personaLifecycle140::finalCount(ticket,"failure")==0,"OFF cold cleanup reaches exactly one success final on the original owner");
 cases.back()["finalPeriod"]={{"requestId",ticket},{"outcome",view.phase},{"fallbackAttempts",view.fallbackAttempts},{"elapsedMs",view.elapsedMs}};
}
static void observeClean(){
 const auto loaded=fixtureTargetReturn::disk();check(loaded.record&&!oldOriginRemains(*loaded.record),"real Session durably settles only the archived old origin");
 check(inputHostTargetReturnHistorySettledLocked(),"cold history gate automatically opens after durable settlement");
 check(censusCalls>0,"real ColdEnvironment consumes a complete PnP parser result");
 check(!traced("hidhide-restore:quarantine-endpoint-absent"),"target-only archived duty invents no physical unhide responsibility");
 cases.back()["afterColdRecord"]=fixtureTargetReturn::summary(loaded);
}
static void loadArchive(){
 wchar_t buffer[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_FIXTURE_COLD_RECORD",buffer,32768);if(!n||n>=32768)throw std::runtime_error("explicit private archived record required");
 std::ifstream file{fspath::path(buffer),std::ios::binary};original.assign(std::istreambuf_iterator<char>(file),{});
 if(!duty::decode(original,historical)||historical.obligations.size()!=1||historical.obligations[0].origin.persona!="elite"||
    !historical.obligations[0].targetMayExist||!historical.obligations[0].paths.empty()||historical.obligations[0].acceptedPsPin)
  throw std::runtime_error("this regression requires the actual single Elite target-only archived fault");
}
}
static ymcc::targetReturn::process::Probe fixtureColdWriter(const ymcc::targetReturn::ProcessIdentity& id){
 using namespace coldArchive;
 bool matched=false;for(const auto& item:historical.obligations)matched=matched||duty::detail::sameProcess(id,item.origin.parent)||duty::detail::sameProcess(id,item.origin.host);
 if(!matched){++forbiddenIo;throw std::logic_error("non-archived process probe refused");}
 return {writerState,duty::process::Reason::MatchingExited,ERROR_SUCCESS};
}
static ymcc::targetReturn::pnpAbsence::Result fixtureColdAbsence(const std::vector<std::string>& ids){
 if(fixtureColdMsiArchive)return fixtureColdMsiAbsence(ids);
 using namespace coldArchive;++censusCalls;Reader reader;const auto result=proof::verify(ids,reader);
 check(reader.enumerations>1&&reader.properties>=1,"single-NUL empty node does not terminate the real device census");
 if(censusState==proof::State::Absent)check(result.absent()&&result.scannedNodes==3&&reader.enumerations==4,"valid empty property permits complete absence proof including all later nodes");
 if(censusState==proof::State::Present)check(result.state==proof::State::Present,"later supported virtual target still vetoes absence after a valid empty property");
 if(censusState==proof::State::Unknown)check(result.state==proof::State::Unknown,"later enumeration error retains unknown instead of claiming absence");
 cases.back()["coldCensus"].push_back({{"state",proof::stateName(result.state)},{"reason",proof::reasonName(result.reason)},{"scannedNodes",result.scannedNodes},{"clock",startProtectionClock}});
 return result;
}
static void fixtureColdBeforeCreate(){
 using namespace coldArchive;const auto loaded=fixtureTargetReturn::disk();++createChecks;
 check(loaded.record&&!oldOriginRemains(*loaded.record),"actual create wire cannot run before archived duty settlement is durable");
 if(!loaded.record||oldOriginRemains(*loaded.record))throw std::runtime_error("create crossed unfinished old return");
}
static void coldArchiveCases(){
 using namespace coldArchive;loadArchive();
 for(const bool owed:{true,false}){
  prepare(owed?"OFF-current-mode-return-owned":"OFF-current-mode-route-armed","disabled");
  // Controlled native ownership facts; this case grants no mode/device I/O.
  g_msiDinputReturnOwed=owed;g_msiDinputRouteDinputArmed=!owed;
  {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
   check(ymcc::inputHost::targetReturnRuntime::open()&&!inputHostTargetReturnHistorySettledLocked(),
       "mode-ownership exclusion is exercised after actual historical Session load, not the unopened fast path");}
  const auto spawns=fullPipeSpawnCount;const auto before=g_virtualStartProtection.view(startProtectionClock);
  const bool handled=inputHostPerformColdStartupReturn();
  check(!handled,"cold-only OFF refuses existing MSI mode ownership so normal Stop cannot be skipped");
  check(bytes()==original&&censusCalls==0&&fullPipeSpawnCount==spawns,"mode ownership exclusion changes no historical record or target and performs no census");
  check(g_virtualStartProtection.view(startProtectionClock).requestId==before.requestId,"cold-only exclusion grants no extra lifecycle period");
  // End independent controlled ownership case, never recovery/return proof.
  g_msiDinputReturnOwed=false;g_msiDinputRouteDinputArmed=false;fixtureColdArchive=false;
 }
 prepare("OFF-automatic-clean-and-idempotent-restart","disabled");
 const auto spawns=fullPipeSpawnCount;inputHostTargetReturnQueueColdStartup();fullPipeDrainQueuedLifecycle();observeClean();finishOff();
 check(fullPipeSpawnCount==spawns&&fullPipeChildObservers.empty()&&!g_inputHostProcess,"OFF startup clears old fault without creating any virtual Host");
 const auto settled=bytes();ymcc::inputHost::targetReturnRuntime::state={};inputHostTargetReturnQueueColdStartup();fullPipeDrainQueuedLifecycle();
 check(bytes()==settled&&fullPipeSpawnCount==spawns&&!g_inputHostLifecycleWorkRequested.load(),"second clean startup is idempotent and does not recreate an old return task");
 fixtureColdArchive=false;
 for(const auto& persona:{std::string("steamdeck"),std::string("elite"),std::string("dualsense-edge")}){
  prepare("saved-ON-"+persona,persona);const auto before=fullPipeSpawnCount;
  inputHostTargetReturnQueueColdStartup();fullPipeDrainQueuedLifecycle();
  check(fullPipeSpawnCount==before,"startup cleanup queue never invents ON creation before normal Start");
  check(inputHostStart(),"saved ON automatically cleans archived return and starts actual managed Host");observeClean();
  check(fullPipeSpawnCount==before+1&&createChecks==1,"exactly one create follows the durable archived settlement");
  personaLifecycle140::success(0,persona,g_virtualStartProtection.view(startProtectionClock).requestId);
  personaLifecycle140::close(0,persona);fixtureColdArchive=false;
 }
 for(const auto& block:{std::string("target-present"),std::string("writer-unknown"),std::string("census-unknown")}){
  prepare("transient-"+block,"steamdeck");const auto before=fullPipeSpawnCount;
  if(block=="writer-unknown")writerState=duty::process::State::Unknown;else censusState=block=="target-present"?proof::State::Present:proof::State::Unknown;
  check(!inputHostStart()&&fullPipeSpawnCount==before,"unproven old state starts no second Host");
  const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
  check(bytes()==original&&g_inputHostSwitchExecutionReturnPending.load(),"transient unproven return retains exact fault record and original queued continuation");
  writerState=duty::process::State::Dead;censusState=proof::State::Absent;retry();observeClean();
  check(fullPipeSpawnCount==before+1&&createChecks==1&&g_virtualStartProtection.view(startProtectionClock).requestId==ticket,"same serialized original request resumes once after observations become provable");
  personaLifecycle140::success(0,"steamdeck",ticket);personaLifecycle140::close(0,"steamdeck");fixtureColdArchive=false;
 }
 for(const auto& block:{std::string("target-present"),std::string("census-unknown"),std::string("writer-alive")}){
  prepare("bounded-unproven-"+block,"steamdeck");const auto before=fullPipeSpawnCount;
  if(block=="writer-alive")writerState=duty::process::State::Alive;else censusState=block=="target-present"?proof::State::Present:proof::State::Unknown;
  check(!inputHostStart(),"persistent unproven return rejects new target creation");const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
  retry();retry();retry();
  check(bytes()==original&&fullPipeSpawnCount==before&&!g_inputHostProcess,"bounded failed recovery neither erases debt nor spawns overlapping virtual targets");
  check(!g_inputHostSwitchExecutionReturnPending.load(),"three observation passes terminate the owned continuation");
  personaLifecycle140::advance(30000);fullPipeVerificationStep();fullPipeVerificationStep();const auto view=g_virtualStartProtection.view(startProtectionClock);
  check(!view.locked&&personaLifecycle140::finalCount(ticket,"success")==0&&personaLifecycle140::finalCount(ticket,"failure")==1,"hard budget always releases switch lock with exactly one failure, never success and failure together");
  cases.back()["finalPeriod"]={{"requestId",ticket},{"outcome",view.phase},{"elapsedMs",view.elapsedMs}};
  // The driver/old writer later becomes provably quiet. A normally admitted
  // OFF must finish its real return duties without app restart or file reset.
  writerState=duty::process::State::Dead;censusState=proof::State::Absent;
  const auto config=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
  const auto off=targetDocument("disabled",config["input"]["revision"].get<int>()+1);
  check(writeTarget(config,off).value("ok",false),"new player OFF after terminal failure is admitted through real settings IPC");
  inputCaptureRefreshRealStickSettings(true);const auto closing=g_virtualStartProtection.view(startProtectionClock);
  fullPipeDrainQueuedLifecycle();const auto after=fixtureTargetReturn::disk();
  const bool settled=after.record&&!oldOriginRemains(*after.record)&&inputHostTargetReturnHistorySettledLocked();
  check(settled,"normal OFF after exhausted cold startup automatically settles now-provable historical return without restarting or resetting records");
  check(fullPipeSpawnCount==before&&!g_inputHostProcess&&closing.persona=="disabled","closing historical duty creates no virtual Host");
  if(settled)finishOff();
  cases.back()["afterExplicitOff"]=fixtureTargetReturn::summary(after);
  fixtureColdArchive=false;
 }
}
