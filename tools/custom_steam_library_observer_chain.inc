// Explicit two-phase private include; product headers use their one canonical path.
#ifdef YMCC_LIBRARY_OBSERVER_DECLARATIONS
#pragma push_macro("RegGetValueW")
#undef RegGetValueW
#pragma push_macro("CloseHandle")
#undef CloseHandle
#pragma push_macro("OpenProcess")
#undef OpenProcess
#pragma push_macro("OpenProcessToken")
#undef OpenProcessToken
#pragma push_macro("CreateEventW")
#undef CreateEventW
#pragma push_macro("OpenEventW")
#undef OpenEventW
#pragma push_macro("CreateFileW")
#undef CreateFileW
#pragma push_macro("CreateNamedPipeW")
#undef CreateNamedPipeW
#pragma push_macro("OpenFileMappingW")
#undef OpenFileMappingW
#pragma push_macro("CreateFileMappingW")
#undef CreateFileMappingW
#pragma push_macro("MapViewOfFile")
#undef MapViewOfFile
#pragma push_macro("UnmapViewOfFile")
#undef UnmapViewOfFile
#pragma push_macro("CreateWindowExW")
#undef CreateWindowExW
#pragma push_macro("DestroyWindow")
#undef DestroyWindow
#pragma push_macro("CreateProcessW")
#undef CreateProcessW
#pragma push_macro("ReadFile")
#undef ReadFile
#pragma push_macro("WriteFile")
#undef WriteFile
#pragma push_macro("FlushFileBuffers")
#undef FlushFileBuffers
#pragma push_macro("WaitForSingleObject")
#undef WaitForSingleObject
#pragma push_macro("CreateToolhelp32Snapshot")
#undef CreateToolhelp32Snapshot
#pragma push_macro("Process32FirstW")
#undef Process32FirstW
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
#include "../native/process_inventory.h"
#include "../native/steam_window_event_budget.h"
#include "custom_steam_library_observer_resources.inc"
#include "custom_steam_library_observer_census.inc"
#pragma push_macro("CreateEventW")
#undef CreateEventW
#define CreateEventW ::ObserverResources::createEventW
#pragma push_macro("OpenEventW")
#undef OpenEventW
#define OpenEventW ::ObserverResources::openEventW
#pragma push_macro("OpenProcess")
#undef OpenProcess
#define OpenProcess ::ObserverResources::openProcess
#pragma push_macro("CloseHandle")
#undef CloseHandle
#define CloseHandle ::ObserverResources::closeHandle
#pragma push_macro("RegOpenKeyExW")
#undef RegOpenKeyExW
#define RegOpenKeyExW ::ObserverResources::regOpenKeyExW
#pragma push_macro("RegCloseKey")
#undef RegCloseKey
#define RegCloseKey ::ObserverResources::regCloseKey
#pragma push_macro("SetWinEventHook")
#undef SetWinEventHook
#define SetWinEventHook ::ObserverResources::setWinEventHook
#pragma push_macro("UnhookWinEvent")
#undef UnhookWinEvent
#define UnhookWinEvent ::ObserverResources::unhookWinEvent
#endif
#include "../native/steam_session_observer.h"
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
#undef UnhookWinEvent
#pragma pop_macro("UnhookWinEvent")
#undef SetWinEventHook
#pragma pop_macro("SetWinEventHook")
#undef RegCloseKey
#pragma pop_macro("RegCloseKey")
#undef RegOpenKeyExW
#pragma pop_macro("RegOpenKeyExW")
#undef CloseHandle
#pragma pop_macro("CloseHandle")
#undef OpenProcess
#pragma pop_macro("OpenProcess")
#undef OpenEventW
#pragma pop_macro("OpenEventW")
#undef CreateEventW
#pragma pop_macro("CreateEventW")
#endif
#pragma pop_macro("Process32FirstW")
#pragma pop_macro("CreateToolhelp32Snapshot")
#pragma pop_macro("WaitForSingleObject")
#pragma pop_macro("FlushFileBuffers")
#pragma pop_macro("WriteFile")
#pragma pop_macro("ReadFile")
#pragma pop_macro("CreateProcessW")
#pragma pop_macro("DestroyWindow")
#pragma pop_macro("CreateWindowExW")
#pragma pop_macro("UnmapViewOfFile")
#pragma pop_macro("MapViewOfFile")
#pragma pop_macro("CreateFileMappingW")
#pragma pop_macro("OpenFileMappingW")
#pragma pop_macro("CreateNamedPipeW")
#pragma pop_macro("CreateFileW")
#pragma pop_macro("OpenEventW")
#pragma pop_macro("CreateEventW")
#pragma pop_macro("OpenProcessToken")
#pragma pop_macro("OpenProcess")
#pragma pop_macro("CloseHandle")
#pragma pop_macro("RegGetValueW")
namespace ChainObserver {
static const std::wstring registryPath=L"Software\\YeManCC-LibraryObserverSelftest\\"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
static bool registryCreated=false,registryDeleted=false,observerStarted=false,observerStopped=false,inPresenceCollect=false,powerRevoked=false,retirementObserved=false;
static unsigned collectCalls=0,presenceCollectCalls=0,presenceReads=0,modeReads=0,informationCalls=0,informationPublished=0;
static std::atomic<unsigned> changes{0},publications{0},retirements{0};
static std::function<void()> afterPresenceRead;
static std::function<void(std::uint32_t,const ymcc::steaminfo::Facts&,const json&)> onCollect;
static std::vector<json> informationReturns;
static json initialPublication,retiredPublication;
static LSTATUS WINAPI presenceRegistry(HKEY hive,LPCWSTR key,LPCWSTR value,DWORD flags,LPDWORD type,PVOID data,LPDWORD bytes){
 if(hive!=HKEY_CURRENT_USER||!key||!value||_wcsicmp(key,L"Software\\Valve\\Steam\\ActiveProcess")||_wcsicmp(value,L"pid")||flags!=RRF_RT_REG_DWORD){++factsIpcRegistryDenials;return ERROR_FILE_NOT_FOUND;}
 const auto path=registryPath+L"\\ActiveProcess";
 const auto status=::RegGetValueW(hive,path.c_str(),value,flags,type,data,bytes);const DWORD error=GetLastError();
 if(inPresenceCollect){++presenceReads;if(afterPresenceRead)afterPresenceRead();}SetLastError(error);return status;
}
static void waitRetired();
}
#else
namespace ChainObserver {
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class F> static bool until(F f){const auto end=GetTickCount64()+3000;while(!f()){if(GetTickCount64()>=end)return false;Sleep(5);}return true;}
static json publication(const std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>& v){
 if(!v)return nullptr;return {{"pid",v->session.pid},{"birth",std::to_string(v->session.created)},{"root",ymcc::steaminfo::utf8(v->session.root)},{"account",v->session.account},{"presence",v->presence},{"registeredPid",v->registeredPid},{"windowsSession",v->windowsSession},{"windowsSessionKnown",v->windowsSessionKnown},{"windowsSessionError",v->windowsSessionError},{"rootReadError",v->rootReadError},{"registeredPidReadError",v->registeredPidReadError},{"accountReadError",v->accountReadError},{"revision",v->revision},{"processEpoch",v->processEpoch},{"accountEpoch",v->accountEpoch}};
}
static void waitRetired(){
 need(until([]{const auto v=g_steamSettingsObserver.identitySnapshot();return v&&v->session.pid!=ChainTyped::targetPid&&v->session.created!=ChainTyped::targetBirth;}),"Actual Observer did not retire owned target");
 retiredPublication=publication(g_steamSettingsObserver.identitySnapshot());retirementObserved=true;
}
struct Lifetime {
 ~Lifetime()noexcept{try{finish();}catch(...){++LibraryFactsIpc::cleanupFailures;}}
 void start(const fs::path& root,DWORD pid,uint64_t birth){
  need(!registryCreated&&!observerStarted&&pid==ChainTyped::targetPid&&birth==ChainTyped::targetBirth,"Observer isolated identity");
  HKEY key=nullptr;DWORD disposition=0;
  need(RegCreateKeyExW(HKEY_CURRENT_USER,registryPath.c_str(),0,nullptr,0,KEY_SET_VALUE|KEY_CREATE_SUB_KEY,nullptr,&key,&disposition)==ERROR_SUCCESS,"Observer private registry create");
  if(disposition!=REG_CREATED_NEW_KEY){RegCloseKey(key);throw std::runtime_error("Observer registry key not fresh");}registryCreated=true;
  const auto path=root.wstring();const auto written=RegSetValueExW(key,L"SteamPath",0,REG_SZ,reinterpret_cast<const BYTE*>(path.c_str()),DWORD((path.size()+1)*sizeof(wchar_t)));RegCloseKey(key);need(written==ERROR_SUCCESS,"Observer private SteamPath");
  need(RegCreateKeyExW(HKEY_CURRENT_USER,(registryPath+L"\\ActiveProcess").c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS,"Observer private ActiveProcess");
  const DWORD account=42;const auto a=RegSetValueExW(key,L"ActiveUser",0,REG_DWORD,reinterpret_cast<const BYTE*>(&account),sizeof(account));const auto p=RegSetValueExW(key,L"pid",0,REG_DWORD,reinterpret_cast<const BYTE*>(&pid),sizeof(pid));RegCloseKey(key);need(a==ERROR_SUCCESS&&p==ERROR_SUCCESS,"Observer private account/pid");
  DWORD session=0;need(ProcessIdToSessionId(pid,&session)!=FALSE,"Observer actual Windows session");
  need(g_steamSettingsObserver.start([](bool changed){if(changed)++changes;},[]{return false;},{},[](std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> v){if(v)++publications;else ++retirements;}),"Actual Observer start");observerStarted=true;
  need(until([&]{const auto v=g_steamSettingsObserver.identitySnapshot();return v&&v->session.pid==pid&&v->session.created==birth&&_wcsicmp(v->session.root.c_str(),root.c_str())==0&&v->session.account==42&&v->presence==1&&v->registeredProcessMatches()&&v->currentRegistryAccountKnown()&&v->windowsSessionKnown&&v->windowsSession==session&&publications.load()>0;}),"Actual Observer live publication");
  initialPublication=publication(g_steamSettingsObserver.identitySnapshot());
 }
 void finish(){
  if(observerStarted&&!observerStopped){g_steamSettingsObserver.stop();observerStopped=true;need(!g_steamSettingsObserver.identitySnapshot()&&g_steamSettingsObserver.sessionSnapshot().pid==0&&g_steamSettingsObserver.steamPresence()==-1,"Observer stop retained identity");}
  afterPresenceRead={};onCollect={};
  if(registryCreated&&!registryDeleted){need(RegDeleteTreeW(HKEY_CURRENT_USER,registryPath.c_str())==ERROR_SUCCESS,"Observer private key delete");HKEY key=nullptr;const auto absent=RegOpenKeyExW(HKEY_CURRENT_USER,registryPath.c_str(),0,KEY_QUERY_VALUE,&key);if(key)RegCloseKey(key);need(absent==ERROR_FILE_NOT_FOUND,"Observer private key still exists");registryDeleted=true;}
 }
};
struct ModePort {ymcc::steaminfo::Json snapshot(const ymcc::steamsession::Session&){++modeReads;return ymcc::steaminfo::Json::object();}};
static void informationReturned(const COPYDATASTRUCT* data,LRESULT result,unsigned before){
 if(!data||data->dwData!=ymcc::steaminfo::messageTag)return;++informationCalls;if(result==ymcc::steaminfo::protocol)++informationPublished;
 std::uint32_t mask=0;if(data->cbData==sizeof(ymcc::steaminfo::Request)&&data->lpData)mask=static_cast<const ymcc::steaminfo::Request*>(data->lpData)->mask;
 informationReturns.push_back({{"mask",mask},{"result",static_cast<long long>(result)},{"protocol",ymcc::steaminfo::protocol},{"published",result==ymcc::steaminfo::protocol},{"collectCallsBefore",before},{"collectCallsAfter",collectCalls},{"powerRevoked",powerRevoked}});
}
static json receipt(){
 const bool revoke=ChainTyped::name=="chain-observer-power-revoked-preserves";unsigned acks=0,denials=0;
 for(const auto& r:informationReturns)if(r.at("mask").get<std::uint32_t>()&ymcc::steaminfo::Presence){if(r.value("published",false))++acks;else ++denials;}
 const bool passed=observerStarted&&observerStopped&&registryCreated&&registryDeleted&&retirementObserved&&publications.load()>0&&retirements.load()>0&&collectCalls>=2&&presenceCollectCalls>=2&&presenceReads==presenceCollectCalls&&modeReads==0&&powerRevoked==revoke&&(revoke?(presenceCollectCalls==2&&acks==1&&denials==1):(acks>=2&&denials==0));
 return {{"passed",passed},{"actualObserver",true},{"observerIsExplicitAdapter",false},{"actualGlobalObserverProducer",false},{"actualIsolatedObserverProducer",true},{"registryPath",ymcc::steaminfo::utf8(registryPath)},{"registryCreated",registryCreated},{"registryDeleted",registryDeleted},{"observerStarted",observerStarted},{"observerStopped",observerStopped},{"retirementObserved",retirementObserved},{"initialPublication",initialPublication},{"retiredPublication",retiredPublication},{"changes",changes.load()},{"publications",publications.load()},{"retirements",retirements.load()},{"collectCalls",collectCalls},{"presenceCollectCalls",presenceCollectCalls},{"presenceReads",presenceReads},{"modeReads",modeReads},{"modeIsExplicitPort",true},{"powerRevoked",powerRevoked},{"informationCalls",informationCalls},{"informationPublished",informationPublished},{"presencePublished",acks},{"presenceRejected",denials},{"informationReturns",informationReturns},{"originalMainInformationReader",true},{"originalCollect",true},{"ownedPresenceCensusOnly",true},{"wholeMachineAbsenceProven",false},{"observerThreadLedgerInstrumented",
#ifdef YMCC_LIBRARY_OBSERVER_RESOURCES
true
#else
false
#endif
},{"observerReturnedByOriginalStop",observerStopped},{"allKernelResourcesReturnedProven",false}};
}
}
static ChainObserver::ModePort g_steamModeObserver;
namespace ymcc::steaminfo {
static Facts fixtureLibraryCollect(std::uint32_t mask,steamsession::Observer& observer,const std::function<Json(const steamsession::Session&)>& mode,std::shared_ptr<const processInventory::Snapshot> table={},const std::function<StartupWindowFacts(const std::shared_ptr<const steamsession::IdentitySnapshot>&)>& windowReader={},const steamsession::SourceOwner::Read* borrowed=nullptr){
 using namespace ChainObserver;++collectCalls;if(mask&Presence)++presenceCollectCalls;
 need(&observer==&g_steamSettingsObserver&&observerStarted&&!observerStopped,"Collect must use actual Observer");need(table&&table->status==processInventory::Status::Complete,"Original ingress census incomplete");
 const auto* ingress=table.get();auto mutableOwned=std::make_shared<processInventory::Snapshot>();mutableOwned->status=table->status;mutableOwned->observedAt=table->observedAt;
 for(const auto& row:table->rows)if(row.pid==ChainTyped::targetPid)mutableOwned->rows.push_back(row);
 std::shared_ptr<const processInventory::Snapshot> owned=mutableOwned;const auto* passed=owned.get();const auto session=observer.sessionSnapshot();const auto identity=observer.identitySnapshot();
 ymcc::sleeppower::SharedPowerV1 power;const bool captured=captureSharedPowerLifecycle(power);const auto reads=presenceReads;need(!inPresenceCollect,"Nested collect");inPresenceCollect=bool(mask&Presence);
 struct Scope {~Scope(){inPresenceCollect=false;afterPresenceRead={};}} scope;
 if(inPresenceCollect&&ChainTyped::name=="chain-observer-power-revoked-preserves"&&presenceCollectCalls==2)afterPresenceRead=[]{need(!powerRevoked,"Duplicate revoke");const auto receipt=g_sleepPowerDispatch.window(PBT_APMSUSPEND,GetTickCount64());need(bool(receipt)&&g_sleepPowerDispatch.pendingSuspend(),"Original Dispatch revoke");powerRevoked=true;};
 auto facts=collect(mask,observer,mode,owned,windowReader,borrowed); // Actual original collector with the same source admission.
 const bool current=captured&&sharedPowerLifecycleCurrent(power);const auto after=observer.sessionSnapshot();
 json detail={{"actualOriginalCollect",true},{"ingressComplete",table->status==processInventory::Status::Complete},{"ingressRows",table->rows.size()},{"ingressPointer",std::to_string(reinterpret_cast<UINT_PTR>(ingress))},{"ingressSamePointerAfterCollect",table.get()==ingress},{"ownedRows",owned->rows.size()},{"ownedPointer",std::to_string(reinterpret_cast<UINT_PTR>(owned.get()))},{"collectSameOwnedPointer",owned.get()==passed},{"presenceReadCalls",presenceReads-reads},{"observerSessionPid",session.pid},{"observerSessionBirth",std::to_string(session.created)},{"observerSessionRoot",utf8(session.root)},{"observerSessionAccount",session.account},{"observerSessionSameAfterCollect",session==after},{"observerPublication",publication(identity)},{"powerCaptured",captured},{"powerGeneration",power.generation},{"powerSequence",power.observedNotificationSequence},{"powerSerial",power.suspendSerial},{"powerCurrentAfterCollect",current},{"powerRevoked",powerRevoked},{"collectReturnedAvailable",facts.available}};
 if(onCollect)onCollect(mask,facts,detail);return facts;
}
}
#define collect fixtureLibraryCollect
#include "typed-shutdown-extracted/main-information-reader.inc"
#undef collect
#endif
