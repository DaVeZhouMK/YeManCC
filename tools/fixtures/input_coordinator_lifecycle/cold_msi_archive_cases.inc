// Current G3E two-obligation archived cold return. Original archive bytes are
// supplied explicitly and kept byte-exact. Only OS/process/HidHide terminals
// are controlled; real ColdEnvironment, durable Session, R3 queue and Host pipe run.
namespace coldMsiArchive {
namespace duty=ymcc::targetReturn;
namespace proof=duty::pnpAbsence;
static std::vector<std::string> hidden;
static unsigned visibilityCalls=0,unhideCalls=0,pathCalls=0;
static bool pathMissing=false,pathUnknown=false,visibilityKnown=true,loseUnhideAck=false;
static bool oldPhysicalPresent=false,unicodeId=false;
static std::vector<std::string> archivePaths(){std::vector<std::string> out;for(const auto& o:coldArchive::historical.obligations)for(const auto& p:o.paths)out.push_back(p.instanceId);return out;}
static const duty::Obligation* pathOwner(const std::string& path){for(const auto& o:coldArchive::historical.obligations)for(const auto& p:o.paths)if(p.instanceId==path)return &o;return nullptr;}
static void loadArchive(){
 wchar_t buffer[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_FIXTURE_COLD_RECORD",buffer,32768);if(!n||n>=32768)throw std::runtime_error("explicit original MSI archived record required");
 std::ifstream file{fspath::path(buffer),std::ios::binary};coldArchive::original.assign(std::istreambuf_iterator<char>(file),{});
 auto& record=coldArchive::historical;
 if(!duty::decode(coldArchive::original,record)||record.obligations.size()!=2)throw std::runtime_error("MSI archive must contain exactly two decoded obligations");
 unsigned physical=0,target=0;
 for(const auto& o:record.obligations){
  if(o.origin.persona!="steamdeck"||o.origin.cliWriterProtocol!=1||o.acceptedPsPin)throw std::runtime_error("MSI archive persona/writer contract mismatch");
  if(!o.targetMayExist&&o.paths.size()==2&&!o.origin.physicalRoot.empty())++physical;
  else if(o.targetMayExist&&o.paths.empty()&&o.origin.physicalRoot.empty())++target;
  else throw std::runtime_error("MSI archive physical/target duty structure mismatch");
 }
 const auto& a=record.obligations[0].origin;const auto& b=record.obligations[1].origin;
 if(physical!=1||target!=1||!duty::detail::sameProcess(a.parent,b.parent)||!duty::detail::sameProcess(a.host,b.host)||a.runId!=b.runId)
  throw std::runtime_error("MSI archive owner identities do not describe the same historical run");
}
struct Reader {
 DWORD lastError=0;unsigned enumerations=0,properties=0;
 HDEVINFO getClassDevs(const GUID*,PCWSTR,HWND,DWORD){return reinterpret_cast<HDEVINFO>(std::uintptr_t(0x22));}
 BOOL enumerate(HDEVINFO,DWORD i,PSP_DEVINFO_DATA data){++enumerations;if(i>=3){lastError=ERROR_NO_MORE_ITEMS;return FALSE;}if(coldArchive::censusState==proof::State::Unknown&&i==2){lastError=ERROR_ACCESS_DENIED;return FALSE;}data->DevInst=i+1;return TRUE;}
 BOOL destroy(HDEVINFO){return TRUE;}
 CONFIGRET deviceId(DEVINST node,PWCHAR out,ULONG chars,ULONG){
  std::wstring value=node==2?(unicodeId?L"SWD\\YMCC_READONLY\\\u8bbe\u5907:unrelated":L"SWD\\MMDEVAPI\\opaque:unrelated"):L"ROOT\\FIXTURE_MSI_COLD\\"+std::to_wstring(node);
  if(node==3&&oldPhysicalPresent)value=U2W(archivePaths().front());
  if(value.size()+1>chars)return CR_BUFFER_SMALL;std::copy(value.begin(),value.end(),out);out[value.size()]=0;return CR_SUCCESS;
 }
 CONFIGRET hardwareIds(DEVINST node,const DEVPROPKEY*,DEVPROPTYPE* type,PBYTE out,PULONG count,ULONG){
  ++properties;*type=DEVPROP_TYPE_STRING_LIST;std::wstring value;
  if(node==1)value.assign(1,L'\0');else {value=node==3&&coldArchive::censusState==proof::State::Present?L"ROOT\\HIDMaestroXUSB":L"PCI\\UNRELATED";value.push_back(0);value.push_back(0);}
  const auto needed=static_cast<ULONG>(value.size()*sizeof(wchar_t));if(*count<needed){*count=needed;return CR_BUFFER_SMALL;}std::memcpy(out,value.data(),needed);*count=needed;return CR_SUCCESS;
 }
 DWORD error(){return lastError;}
};
static void prepare(const std::string& name,const std::string& persona,bool initiallyHidden=true){
 fixtureColdMsiArchive=false;coldArchive::prepare("MSI/"+name,persona);fixtureColdMsiArchive=true;
 hidden=initiallyHidden?archivePaths():std::vector<std::string>{};visibilityCalls=unhideCalls=pathCalls=0;
 pathMissing=false;pathUnknown=false;visibilityKnown=true;loseUnhideAck=false;oldPhysicalPresent=false;unicodeId=false;
 cases.back()["archiveScope"]="original G3E two SteamDeck obligations; controlled OS observations; real cold owner/session/queue/Host pipe";
}
static void clean(){
 const auto loaded=fixtureTargetReturn::disk();
 check(loaded.record&&!coldArchive::oldOriginRemains(*loaded.record),"MSI both historical origins settle durably before a new target");
 check(inputHostTargetReturnHistorySettledLocked(),"MSI history gate opens only after both archived obligations settle");
 check(hidden.empty(),"MSI only historical owned hide keys have been returned");
 cases.back()["coldStats"]={{"censusCalls",coldArchive::censusCalls},{"visibilityCalls",visibilityCalls},{"unhideCalls",unhideCalls},{"pathCalls",pathCalls},{"oldPhysicalPresent",oldPhysicalPresent},{"unicodeId",unicodeId}};
}
static void completeOn(const std::string& persona,std::uint64_t ticket){personaLifecycle140::success(0,persona,ticket);personaLifecycle140::close(0,persona);fixtureColdMsiArchive=false;fixtureColdArchive=false;}
}
static ymcc::targetReturn::pnpAbsence::Result fixtureColdMsiAbsence(const std::vector<std::string>& ids){
 using namespace coldMsiArchive;++coldArchive::censusCalls;Reader reader;const auto result=proof::verify(ids,reader);
 cases.back()["coldCensus"].push_back({{"state",proof::stateName(result.state)},{"reason",proof::reasonName(result.reason)},{"scannedNodes",result.scannedNodes},{"clock",startProtectionClock},{"opaqueOsId",true},{"knownIds",ids}});
 return result;
}
static bool fixtureColdMsiHidden(std::vector<std::string>& result){using namespace coldMsiArchive;++visibilityCalls;if(!visibilityKnown)return false;result=hidden;return true;}
static ymcc::targetReturn::physicalPath::Result fixtureColdMsiPath(const std::string& root,const std::string& path){
 using namespace coldMsiArchive;namespace p=duty::physicalPath;++pathCalls;const auto* owner=pathOwner(path);
 if(!owner||owner->origin.physicalRoot!=root){++forbiddenIo;throw std::logic_error("unowned MSI physical path refused");}
 if(pathUnknown)return {p::State::Unknown,p::Reason::ParentQueryFailed,1,CR_ACCESS_DENIED};
 if(pathMissing)return {p::State::Unknown,p::Reason::TargetLocateFailed,0,CR_NO_SUCH_DEVNODE};
 return {p::State::Proven,p::Reason::RootMatched,2,CR_SUCCESS};
}
static bool fixtureColdMsiCli(const std::wstring& command,std::string*,DWORD* exit){
 using namespace coldMsiArchive;
 for(const auto& path:archivePaths())if(command==L"--dev-unhide \""+U2W(path)+L"\""){
  ++unhideCalls;const auto found=std::find(hidden.begin(),hidden.end(),path);if(found==hidden.end()){++forbiddenIo;throw std::logic_error("duplicate/unowned MSI unhide refused");}hidden.erase(found);
  if(exit)*exit=loseUnhideAck?ERROR_NO_DATA:ERROR_SUCCESS;return !loseUnhideAck;
 }
 ++forbiddenIo;throw std::logic_error("non-owned MSI cold CLI refused:"+W2U(command));
}
static void coldMsiArchiveCases(){
 using namespace coldMsiArchive;loadArchive();
 for(const auto& persona:{std::string("steamdeck"),std::string("elite"),std::string("dualsense-edge")}){
  prepare("saved-ON-"+persona,persona);const auto before=fullPipeSpawnCount;
  unicodeId=persona=="dualsense-edge";
  const bool started=inputHostStart();check(started,"MSI opaque OS ID permits durable two-duty cleanup before saved ON creation");
  if(!started){check(coldArchive::bytes()==coldArchive::original&&fullPipeSpawnCount==before,"failed census retains exact two-duty archive without creating a Host");fixtureColdMsiArchive=false;fixtureColdArchive=false;continue;}
  clean();check(unhideCalls==2&&pathCalls==2&&visibilityCalls>=4,"MSI two archived hidden paths each return once with readback");
  check(fullPipeSpawnCount==before+1&&coldArchive::createChecks==1,"MSI one new Host follows complete durable return");completeOn(persona,g_virtualStartProtection.view(startProtectionClock).requestId);
 }
 prepare("OFF-two-hidden-paths","disabled");const auto beforeOff=fullPipeSpawnCount;
 inputHostTargetReturnQueueColdStartup();fullPipeDrainQueuedLifecycle();
 if(inputHostTargetReturnHistorySettledLocked()){clean();coldArchive::finishOff();check(unhideCalls==2&&fullPipeSpawnCount==beforeOff,"MSI OFF returns both exact keys without any Host creation");}
 else check(false,"MSI OFF completes both historical duties through the real cold owner");
 fixtureColdMsiArchive=false;fixtureColdArchive=false;
 prepare("reserved-paths-already-visible","steamdeck",false);
 const bool visibleStarted=inputHostStart();check(visibleStarted,"MSI reserved but already-visible paths settle without inventing a hide write");
 if(visibleStarted){clean();check(unhideCalls==0&&pathCalls==0&&visibilityCalls==2,"MSI already-visible exact paths require readback but no ancestry write authority");completeOn("steamdeck",g_virtualStartProtection.view(startProtectionClock).requestId);}else{fixtureColdMsiArchive=false;fixtureColdArchive=false;}
 prepare("ack-loss-readback-proves-return","steamdeck");loseUnhideAck=true;
 const bool ackStarted=inputHostStart();check(ackStarted,"lost unhide ACK may settle only after exact readback proves both keys absent");
 if(ackStarted){clean();check(unhideCalls==2&&visibilityCalls>=4,"ACK-loss path performs owned writes and complete readback");completeOn("steamdeck",g_virtualStartProtection.view(startProtectionClock).requestId);}else{fixtureColdMsiArchive=false;fixtureColdArchive=false;}
 for(const auto& block:{std::string("target-present"),std::string("census-unknown"),std::string("writer-unknown"),std::string("ancestry-unknown"),std::string("visibility-unknown"),std::string("old-path-still-present")}){
  prepare("transient-"+block,"steamdeck");const auto before=fullPipeSpawnCount;
  if(block=="target-present")coldArchive::censusState=proof::State::Present;
  if(block=="census-unknown")coldArchive::censusState=proof::State::Unknown;
  if(block=="writer-unknown")coldArchive::writerState=duty::process::State::Unknown;
  if(block=="ancestry-unknown")pathUnknown=true;
  if(block=="visibility-unknown")visibilityKnown=false;
  if(block=="old-path-still-present"){pathMissing=true;oldPhysicalPresent=true;}
  check(!inputHostStart()&&fullPipeSpawnCount==before&&unhideCalls==0,"MSI unproven historical state refuses overlap and performs no unhide");
  const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
  check(coldArchive::bytes()==coldArchive::original&&g_inputHostSwitchExecutionReturnPending.load(),"MSI uncertain old state retains byte-exact two-duty archive and owned continuation");
  coldArchive::censusState=proof::State::Absent;coldArchive::writerState=duty::process::State::Dead;pathUnknown=false;visibilityKnown=true;oldPhysicalPresent=false;
  coldArchive::retry();
  if(g_inputHostProcess){clean();check(fullPipeSpawnCount==before+1&&g_virtualStartProtection.view(startProtectionClock).requestId==ticket,"MSI now-proven return resumes exactly once on the original serial request");completeOn("steamdeck",ticket);}
  else{check(false,"MSI transient proof recovery creates the requested Host within the original period");fixtureColdMsiArchive=false;fixtureColdArchive=false;}
 }
 prepare("reboot-missing-old-devnodes","steamdeck");pathMissing=true;
 const bool missingStarted=inputHostStart();check(missingStarted,"MSI missing old devnodes require complete absence before removing historical hide keys");
 if(missingStarted){clean();check(coldArchive::censusCalls>=3&&unhideCalls==2,"MSI each missing old path consumes a complete exact-ID absence census before unhide");completeOn("steamdeck",g_virtualStartProtection.view(startProtectionClock).requestId);}else{fixtureColdMsiArchive=false;fixtureColdArchive=false;}
 prepare("original-budget-then-explicit-OFF","steamdeck");const auto before=fullPipeSpawnCount;coldArchive::censusState=proof::State::Unknown;
 check(!inputHostStart(),"MSI persistent unknown census rejects target creation");const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
 coldArchive::retry();coldArchive::retry();coldArchive::retry();
 check(coldArchive::bytes()==coldArchive::original&&unhideCalls==0&&fullPipeSpawnCount==before,"MSI exhausted execution retries preserve both duties without writes or spawn");
 personaLifecycle140::advance(30000);fullPipeVerificationStep();fullPipeVerificationStep();
 check(!g_virtualStartProtection.view(startProtectionClock).locked&&personaLifecycle140::finalCount(ticket,"failure")==1&&personaLifecycle140::finalCount(ticket,"success")==0,"MSI original hard budget has one failure final, never simultaneous success");
 coldArchive::censusState=proof::State::Absent;const auto config=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE)),off=targetDocument("disabled",config["input"]["revision"].get<int>()+1);
 check(writeTarget(config,off).value("ok",false),"MSI subsequent user OFF is admitted after the failed period");inputCaptureRefreshRealStickSettings(true);fullPipeDrainQueuedLifecycle();
 if(inputHostTargetReturnHistorySettledLocked()){clean();coldArchive::finishOff();check(fullPipeSpawnCount==before,"MSI user OFF after recovery budget settles historical state without manual record reset");}else check(false,"MSI later OFF settles now-provable old responsibilities");
 fixtureColdMsiArchive=false;fixtureColdArchive=false;
}
