"""Steam-only production parser/observer/rescope coordinator, deterministic OS/CDP seams.
Does not access live Steam, sleep Windows, resume games or create input devices.
"""
from pathlib import Path
import argparse,subprocess,re,json,uuid
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,default=Path('../../Build/Validation/SleepSteamOnly-20261011'))
p.add_argument('--coordinator-cost-only',action='store_true');p.add_argument('--expect-capture-budget-rejected',action='store_true');p.add_argument('--expect-native-primary-old-target',action='store_true');p.add_argument('--expect-native-predispatch-blocked',action='store_true');p.add_argument('--expect-delayed-window-blocked',action='store_true');args=p.parse_args()
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True);root=Path.cwd()
api=(root/'native/sleep_recovery_api.h').read_text('utf-8-sig')
tail=api[api.index('struct Tail {'):api.index('struct Leases {')]
main=(root/'native/main.cpp').read_text('utf-8-sig')
one_pid=main[main.index('enum class NativeOnePidResult {'):main.index('// C-D1（155 §3.1-2）')]
ws_constants='\n'.join(re.findall(r'^static const uint64_t\s+SG_(?:MIN_WS|CAPTURE_MIN_WS)\s*=.*?;',main,re.M))
assert len(ws_constants.splitlines())==2
live_wrapper=main[main.index('static bool sleepSteamLiveProcessMatches('):main.index('static bool steamLiveCanDefer(')]
field_predispatch=json.loads((root/'tools/fixtures/sleep_steam_msi_tmnt_r30_predispatch_160835.json').read_text('utf-8'))
assert field_predispatch['exactCancelPredicateKnown'] is False and field_predispatch['mutated'] is False
assert field_predispatch['jointBindingVerified'] and field_predispatch['nativeOverviewFirstWakeRead']==11
transport=(root/'native/steam_live_cdp.h').read_text('utf-8-sig')
cancel_guard=transport[transport.index('        if (admitAction && !admitAction()) return '):transport.index('        sent = true;')].strip()
link_source=(root/'native/steam_game_link_runtime.h').read_text('utf-8-sig')
link_dense_seam=(link_source[link_source.index('struct Handle {'):link_source.index('struct Control {')]
 +link_source[link_source.index('struct TrackerRead {'):link_source.index('static bool sameClientProcess(')]
 +link_source[link_source.index('static Game discoverGame('):link_source.index('static json base()')])
cpp=r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
#include <atomic>
#include <functional>
#include <set>
#include <condition_variable>
#include <iostream>
#include "json.hpp"
#include "steam_native_return_evidence.h"
#include "sleep_recovery_diagnostics.h"
#include "sleep_steam_tracking.h"
#include "monitor_identity.h"
#include "sleep_steam_preferences.h"
using json=nlohmann::json;namespace fspath=std::filesystem;
std::wstring SG_DIR,SG_SLEEP_LEASE_DIR,steamRoot;
std::atomic<ULONGLONG> g_sleepRecoveryAutoConfigEpoch{1};
std::atomic<bool> g_sleepRecoveryAutoEnabled{false},g_sleepRecoveryAutoPending{false},g_exitRequested{false};
enum class PowerLifecycle{Ready,Suspending,Suspended,Resuming};
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
ULONGLONG generation=7,tick=0;bool retry=false,writes=true,modernStandby=false;
bool sgModernStandbyRepairSupported(){return modernStandby;}
ULONGLONG fileTimeBase=134362144573220276ULL;
ULONGLONG sgNowFileTime(){return fileTimeBase+tick*10000;}
std::condition_variable g_sgWorkCv;
unsigned workNotifications=0;
void sgNotifyWork(){++workNotifications;g_sgWorkCv.notify_one();}
unsigned long long currentPowerGeneration(){return generation;}
ULONGLONG fakeTick(){return tick;}
#define GetTickCount64 fakeTick
bool sgSleepRetryActive(){return retry;}
bool hardwareWriteAllowed(){return writes&&g_powerLifecycle==PowerLifecycle::Ready;}
bool sleepRecoveryAutoAllows(ULONGLONG gen,ULONGLONG epoch){return g_sleepRecoveryAutoEnabled&&gen==generation&&epoch==g_sleepRecoveryAutoConfigEpoch&&!g_exitRequested;}
bool sleepSteamRecoveryAllows(ULONGLONG gen,ULONGLONG epoch){return sleepRecoveryAutoAllows(gen,epoch);}
std::string W2U(const std::wstring& s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string v(n,0);if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),v.data(),n,nullptr,nullptr);return v;}
std::wstring U2W(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);std::wstring v(n,0);if(n)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),v.data(),n);return v;}
std::wstring sgBaseName(const std::wstring& s){return nativeIdentityNormalizeName(s);}
double sgNowEpoch(){return tick/1000.0;}
bool sgEnsureMarkerDir(const std::wstring& dir){std::error_code e;fspath::create_directories(dir,e);return !e;}
bool sgWriteFileAtomic(const std::wstring& file,const std::string& text){std::ofstream f{fspath::path(file),std::ios::binary};f<<text;return bool(f);}
std::string readFile(const std::wstring& file){std::ifstream f{fspath::path(file),std::ios::binary};return {std::istreambuf_iterator<char>(f),{}};}
std::vector<std::pair<std::string,json>> facts,events;
void sgRecordFact(const char* name,json data=json::object()){facts.push_back({name,data});}
void ipc_emit(const std::string& name,json data){events.push_back({name,data});}
namespace SleepRecoveryApi {
__TAIL__
}
struct SgSleepTarget {DWORD pid=0;ULONGLONG processCreated=0;bool markerOwned=false;};
SgSleepTarget owner;
struct NativeDetectedGame {DWORD pid=12932;ULONGLONG processCreated=134361596976806475ULL;std::wstring path=L"E:\\GAME\\TMNT.exe";std::wstring name,whitelistRule;SIZE_T workingSet=0;bool whitelisted=false,customConfigured=false;};
NativeDetectedGame selected;
std::map<DWORD,NativeDetectedGame> captureProcesses;std::set<DWORD> captureWindowPids;
std::set<DWORD> captureForeignSessionPids,captureIdentityUnavailablePids;std::vector<std::wstring> captureExcludes,captureWhitelist;
std::function<void()> onCaptureRulesSnapshot;
std::function<void(DWORD)> onCaptureWindow,onOnePidMemory;bool captureSessionQueryFails=false;
std::vector<std::wstring> captureConfigured;std::map<DWORD,SIZE_T> captureWorkingSets;
std::set<DWORD> captureVmReadDeniedPids,captureImageFailsPids,captureMemoryFailsPids;
std::mutex g_gameRulesMx;std::atomic<unsigned long long> g_gameRulesEpoch{1};
void nativeRefreshGameRulesEpochFromDiskLocked(){if(onCaptureRulesSnapshot)onCaptureRulesSnapshot();}
std::vector<std::wstring> nativeConfiguredGameExes(){return captureConfigured;}
bool nativeIsConfiguredGameExe(const std::wstring& name,const std::vector<std::wstring>& configured){return std::find(configured.begin(),configured.end(),name)!=configured.end();}
std::wstring sgMatchingGameWhitelistRule(const std::wstring& name,const std::vector<std::wstring>& list){return std::find(list.begin(),list.end(),name)==list.end()?L"":name;}
bool tickCostEnabled(){return false;}
std::atomic<unsigned long long> g_gameValveOnePidChecks{0},g_gameValveDetailQueries{0},g_gameValveIdentityRejects{0},g_gameValveIdentityUnverified{0};
NativeNameIdentity nativeNameIdentityVerdict(const std::wstring& name,const std::wstring& path){return nativeNameIdentityVerdictNormalized(name,sgBaseName(path));}
__WS_CONSTANTS__
std::vector<std::wstring> sgExcludes(){return captureExcludes;}
std::vector<std::wstring> sgGameWhitelist(){return captureWhitelist;}
bool sgNameExcludedBy(const std::wstring& name,const std::vector<std::wstring>& list){return std::find(list.begin(),list.end(),name)!=list.end();}
bool nativeMonitorExcluded(const std::wstring& name){return name==L"redengineerrorreporter"||name==L"steamwebhelper";}
BOOL fakeSessionId(DWORD pid,DWORD* out){if(captureSessionQueryFails)return FALSE;*out=captureForeignSessionPids.count(pid)?2:1;return TRUE;}

bool gameExists=true,valveAvailable=true,accessDenied=false;
DWORD steamAccount=42,steamPid=4000,registrySteamPid=0,registryAccountOverride=0;ULONGLONG steamCreated=134361500000000000ULL;
int sessionDiscoveryCalls=0;bool sessionReadMalformed=false;
bool steamProcessAvailable=true,observerAvailable=true,registryAccountUnavailable=false,observerWrongRoot=false,observerWrongBirth=false;std::string steamReadFailure;
struct SteamObservedSession{std::wstring root;DWORD pid=0,account=0;ULONGLONG created=0;};
struct MockSteamObserver{SteamObservedSession sessionSnapshot(){return observerAvailable?SteamObservedSession{observerWrongRoot?steamRoot+L"-other":steamRoot,steamPid,registryAccountOverride?registryAccountOverride:steamAccount,steamCreated+(observerWrongBirth?1:0)}:SteamObservedSession{};}};
MockSteamObserver g_steamSettingsObserver;
DWORD extraForegroundPid=0;ULONGLONG extraForegroundCreated=134361500000000111ULL;
std::wstring extraForegroundPath;bool extraForegroundAvailable=true;

bool sgReadSleepTarget(SgSleepTarget& t){t=owner;return bool(owner.pid);}
bool nativeValveReadCurrentTarget(NativeDetectedGame* t){*t=selected;return valveAvailable&&selected.pid;}
bool focusQueryProcessIdentity(DWORD pid,std::wstring* path,ULONGLONG* created){if(captureIdentityUnavailablePids.count(pid))return false;if(captureProcesses.count(pid)){const auto& g=captureProcesses.at(pid);if(path)*path=g.path;if(created)*created=g.processCreated;return true;}if(extraForegroundPid&&pid==extraForegroundPid){if(!extraForegroundAvailable)return false;if(path)*path=extraForegroundPath;if(created)*created=extraForegroundCreated;return true;}if(pid==steamPid&&steamProcessAvailable){if(created)*created=steamCreated;if(path)*path=steamRoot+L"\\steam.exe";return true;}if(!gameExists||accessDenied||pid!=selected.pid)return false;if(path)*path=selected.path;if(created)*created=selected.processCreated;return true;}
HANDLE fakeOpen(DWORD access,BOOL,DWORD pid){if((access&PROCESS_VM_READ)&&captureVmReadDeniedPids.count(pid))return nullptr;if(captureProcesses.count(pid))return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid));if(pid==steamPid&&steamProcessAvailable)return reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid));return gameExists&&!accessDenied&&pid==selected.pid?reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid)):nullptr;}
BOOL fakeImagePath(HANDLE h,DWORD,wchar_t* image,DWORD* length){const auto pid=static_cast<DWORD>(reinterpret_cast<uintptr_t>(h));if(captureImageFailsPids.count(pid))return FALSE;std::wstring path;ULONGLONG birth=0;if(!focusQueryProcessIdentity(pid,&path,&birth)||path.size()>=*length)return FALSE;std::copy(path.begin(),path.end(),image);*length=static_cast<DWORD>(path.size());return TRUE;}
BOOL fakeMemory(HANDLE h,PROCESS_MEMORY_COUNTERS* memory,DWORD){const auto pid=static_cast<DWORD>(reinterpret_cast<uintptr_t>(h));if(onOnePidMemory)onOnePidMemory(pid);if(captureMemoryFailsPids.count(pid))return FALSE;memory->WorkingSetSize=captureWorkingSets.count(pid)?captureWorkingSets.at(pid):1024ULL*1024*1024;return TRUE;}

ULONGLONG nativeProcessCreatedFromHandle(HANDLE h){const auto pid=static_cast<DWORD>(reinterpret_cast<uintptr_t>(h));if(captureProcesses.count(pid))return captureProcesses.at(pid).processCreated;return reinterpret_cast<uintptr_t>(h)==steamPid?steamCreated:selected.processCreated;}
DWORD fakeWait(HANDLE h,DWORD){if(captureProcesses.count(static_cast<DWORD>(reinterpret_cast<uintptr_t>(h))))return WAIT_TIMEOUT;return reinterpret_cast<uintptr_t>(h)==steamPid?(steamProcessAvailable?WAIT_TIMEOUT:WAIT_OBJECT_0):(gameExists?WAIT_TIMEOUT:WAIT_OBJECT_0);}
BOOL fakeClose(HANDLE){return true;}
bool steamAvailable=true,running=false;int localStatus=4,steamReads=0,scopeCalls=0,inputCalls=0;std::string steamOverviewGameid="10307630032478011392";
bool inputContextAvailable=false,gamePaused=false,controllersObserved=true,raiseFocusesOriginal=true,deferForeground=false;DWORD steamFocusedApp=413080,foregroundPid=0,steamOverlayApp=0;unsigned foregroundWaitCalls=0;std::function<void()> onForegroundWait;
void fakeSleep(DWORD ms){tick+=ms;++foregroundWaitCalls;if(onForegroundWait)onForegroundWait();}
bool poolSubmit(std::function<void()> job);
bool focusPidIsManuallyPaused(DWORD){return gamePaused;}
std::vector<std::wstring> probeModules={L"gameoverlayrenderer64.dll",L"xinput1_3.dll"};size_t moduleIndex=0;bool moduleQueryFails=false;
HANDLE fakeModuleSnapshot(DWORD,DWORD){return moduleQueryFails?INVALID_HANDLE_VALUE:reinterpret_cast<HANDLE>(uintptr_t{200});}
BOOL fakeModuleFirst(HANDLE,MODULEENTRY32W* m){moduleIndex=0;if(probeModules.empty()){SetLastError(ERROR_NO_MORE_FILES);return false;}wcsncpy_s(m->szModule,probeModules[0].c_str(),_TRUNCATE);wcsncpy_s(m->szExePath,(steamRoot+L"\\"+probeModules[0]).c_str(),_TRUNCATE);return true;}
BOOL fakeModuleNext(HANDLE,MODULEENTRY32W* m){if(++moduleIndex>=probeModules.size()){SetLastError(ERROR_NO_MORE_FILES);return false;}wcsncpy_s(m->szModule,probeModules[moduleIndex].c_str(),_TRUNCATE);wcsncpy_s(m->szExePath,(steamRoot+L"\\"+probeModules[moduleIndex]).c_str(),_TRUNCATE);return true;}
BOOL fakeProcessToken(HANDLE,DWORD,HANDLE* out){*out=nullptr;SetLastError(ERROR_ACCESS_DENIED);return false;}
HWND fakeForeground(){return reinterpret_cast<HWND>(uintptr_t{100});}
bool pidWindowExists=false,pidWindowResponsive=true,pidApiAccepted=true,pidActivationFocusesOriginal=true,pidWindowMismatch=false,inputInteractiveIdle=true;
unsigned pidFindCalls=0,pidProbeCalls=0,pidActivateCalls=0;std::function<void()> onPidProbe,onPidActivation;
DWORD fakeWindowPid(HWND window,DWORD* out){if(reinterpret_cast<uintptr_t>(window)>=1000){if(out)*out=static_cast<DWORD>(reinterpret_cast<uintptr_t>(window)-1000);return 1;}if(out)*out=reinterpret_cast<uintptr_t>(window)==200?(pidWindowMismatch?6000:selected.pid):(foregroundPid?foregroundPid:selected.pid);return 1;}
HWND focusFindWindowForPid(DWORD pid){++pidFindCalls;if(onCaptureWindow)onCaptureWindow(pid);if(captureWindowPids.count(pid))return reinterpret_cast<HWND>(uintptr_t{1000}+pid);if(captureProcesses.count(pid)||pid!=selected.pid)return nullptr;return pidWindowExists?reinterpret_cast<HWND>(uintptr_t{200}):nullptr;}
bool focusIsRestorableExactWindow(HWND w,DWORD pid){if(reinterpret_cast<uintptr_t>(w)>=1000)return captureWindowPids.count(pid)&&reinterpret_cast<uintptr_t>(w)==1000+pid;return w&&pidWindowExists&&!pidWindowMismatch&&pid==selected.pid;}
LRESULT fakeSendTimeout(HWND,UINT,WPARAM,LPARAM,UINT,UINT,PDWORD_PTR){++pidProbeCalls;tick+=pidWindowResponsive?1:150;if(onPidProbe)onPidProbe();return pidWindowResponsive?1:0;}
DWORD fakeCurrentThread(){return 2;}
BOOL fakeVisible(HWND){return true;}BOOL fakeIconic(HWND){return false;}BOOL fakeShowAsync(HWND,int){return true;}
BOOL fakeZOrder(HWND,HWND,int,int,int,int,UINT){return true;}
BOOL fakeActivate(HWND w){assert(reinterpret_cast<uintptr_t>(w)==200);++pidActivateCalls;if(pidActivationFocusesOriginal)foregroundPid=selected.pid;if(onPidActivation)onPidActivation();return pidApiAccepted;}
std::function<void(json&)> contextOverride;
json inputContext(){json result={{"focus",{{"available",true},{"appid",steamFocusedApp},{"overlayAppid",steamOverlayApp}}},{"controllerListValid",true},{"controllers",controllersObserved?json::array({{{"index",0},{"type",2}}}):json::array()},
 {"editor",{{"available",true},{"editingAvailable",true},{"editingActive",false},{"dirty",false},{"previewActive",false},{"updating",false}}},
 {"interactive",{{"available",true},{"idle",inputInteractiveIdle},{"lockScreen",false},{"keyboard",false},{"overlayOrVrWindow",false},{"menuOpen",inputInteractiveIdle?0:1},{"menuSuppressed",false}}}};if(contextOverride)contextOverride(result);return result;}

json lastInputRequest,preflightShapeOverride,contactReconcileReceiptOverride;int preflightRejects=0,windowPrepCalls=0;bool postMutationGuardFailure=false;
bool uiReadbackStale=false,rejectListMutation=false,keepMissingList=false;std::string listRejectReason="steam-list-readback-unconfirmed";std::function<void()> onReturn;
std::function<void(const std::string&)> onSteamCall;
void sofRegDWORD(HKEY,const wchar_t*,const wchar_t* key,DWORD* out){*out=std::wstring(key)==L"pid"?(registrySteamPid?registrySteamPid:steamPid):(registryAccountUnavailable?0:registryAccountOverride?registryAccountOverride:steamAccount);}
std::wstring sofRegSZ(HKEY,const wchar_t*,const wchar_t*){return steamRoot;}
json privateNativeRecordReply,nativeReturnFixture;
std::string sha256File(const std::wstring&){return {};}// no host file hashing in deterministic OS fixture
json fakeSteamLiveRun(json request,const std::function<bool()>& admit={}) {
 const DWORD account=request.value("account",DWORD{0});
 const auto op=request.value("operation",std::string());if(onSteamCall)onSteamCall(op);
 if(!steamReadFailure.empty())return {{"ok",false},{"mutated",false},{"reason",steamReadFailure}};
 if(op=="sleep.session.inspect"){++sessionDiscoveryCalls;
   if(!steamAvailable||!steamAccount)return {{"ok",false},{"mutated",false},{"reason","steam-session-starting"}};
   return {{"ok",true},{"mutated",sessionReadMalformed},{"observedAccount",steamAccount}};}
 if(account!=steamAccount)return {{"ok",false},{"mutated",false},{"reason","steam-account-changed"}};if(!steamAvailable)return {{"ok",false},{"mutated",false},{"reason","steam-read-unavailable"}};
 if(op=="sleep.inspect"){++steamReads;json list=json::array(),overviews=json::array();if(running)list.push_back(2399932135U);
   for(const auto& id:request["appids"])overviews.push_back({{"appid",id},{"overviewAvailable",true},{"gameidString",steamOverviewGameid},{"appType",0x40000000},{"localRunning",localStatus==4||localStatus==1},{"localDisplayStatus",localStatus},{"selectedDisplayStatus",localStatus},{"inRunningList",running}});
   auto reply=json{{"ok",true},{"mutated",false},{"runningAppIDs",list},{"overviews",overviews},{"inputContext",inputContextAvailable?inputContext():json::object()},{"gameInput","unknown"}};
   if(!privateNativeRecordReply.is_null())reply["_ymccPrivateNativeRecord"]=privateNativeRecordReply;return reply;}
 if(op=="sleep.input-resume"){const auto& admitAction=admit;
 __PREDISPATCH_GUARD__
    if(preflightRejects>0){--preflightRejects;return preflightShapeOverride.is_object()?preflightShapeOverride:json{{"ok",false},{"mutated",false},{"stage","sleep-inspect"},{"reason","steam-interactive-ui-active"}};}
    ++inputCalls;const bool windowPreparation=request.value("preControllerOriginalWindow",false);if(windowPreparation)++windowPrepCalls;lastInputRequest=request;
   steamFocusedApp=uiReadbackStale?413080:2399932135U;foregroundPid=deferForeground?steamPid:raiseFocusesOriginal?request.value("originalTarget",json::object()).value("pid",selected.pid):13000;
   if(onReturn)onReturn();
   auto response=json{{"ok",!windowPreparation&&!uiReadbackStale&&!postMutationGuardFailure},{"mutated",true},{"stage","sleep-input-resume"},{"actionAcknowledged",true},{"verificationPending",!postMutationGuardFailure&&(windowPreparation||uiReadbackStale)},{"windowPreparation",windowPreparation},{"controllerReconnectPending",windowPreparation&&!controllersObserved},{"uiFocusMatched",!uiReadbackStale},
     {"runtimeAccepted",!windowPreparation&&!uiReadbackStale&&!postMutationGuardFailure},{"inputContext",inputContext()},{"after",{{"overviews",json::array({{{"appid",2399932135U},{"gameidString","10307630032478011392"},{"appType",0x40000000}}})}}},
     {"reason",postMutationGuardFailure?"steam-interactive-ui-active":windowPreparation?"steam-original-window-prepared-input-unverified":uiReadbackStale?"steam-input-ui-focus-readback-pending":"steam-input-context-refocused"}};
   if(!nativeReturnFixture.is_null())response["nativeReturn"]=nativeReturnFixture;return response;}
 assert(op=="sleep.reconcile");if(admit&&!admit())return {{"ok",false},{"reason","admission-denied"},{"mutated",false}};
 // Valve ScopeRunningApps derives membership from local Running state; it
 // cannot promote ReadyToLaunch by changing the test double's running flag.
 if(request.value("contactRecovery",false)&&localStatus==11){++scopeCalls;return {{"ok",false},{"mutated",true},{"runtimeAccepted",false},{"reason","steam-contact-rescope-not-confirmed"}};}
 ++scopeCalls;if(!keepMissingList)running=true;auto response=json{{"ok",!rejectListMutation},{"mutated",listRejectReason=="steam-rescope-not-confirmed"||!keepMissingList},{"runtimeAccepted",!rejectListMutation},{"reason",rejectListMutation?listRejectReason:"steam-association-current"}};
 if(request.value("contactRecovery",false))response["contactRecovery"]=contactReconcileReceiptOverride.is_null()?json{{"requested",true},{"proofAccepted",true},{"accepted",!rejectListMutation}}:contactReconcileReceiptOverride;
 return response;
}
#define GetCurrentThreadId fakeCurrentThread
#define SendMessageTimeoutW fakeSendTimeout
#define IsWindowVisible fakeVisible
#define IsIconic fakeIconic
#define ShowWindowAsync fakeShowAsync
#define SetWindowPos fakeZOrder
#define SetForegroundWindow fakeActivate
#define OpenProcessToken fakeProcessToken
#define CreateToolhelp32Snapshot fakeModuleSnapshot
#define Module32FirstW fakeModuleFirst
#define Module32NextW fakeModuleNext
#define Sleep fakeSleep
#define GetForegroundWindow fakeForeground
#define GetWindowThreadProcessId fakeWindowPid
#define ProcessIdToSessionId fakeSessionId
#define OpenProcess fakeOpen
#define WaitForSingleObject fakeWait
#define CloseHandle fakeClose
#define QueryFullProcessImageNameW fakeImagePath
#define GetProcessMemoryInfo fakeMemory
__ONE_PID__
struct MockIsolation {bool isolated=false;} g_aiFanMockSession;
namespace ymcc::steamlive {
std::wstring normalizedPath(std::wstring path){path=U2W(ymcc::sleepsteam::pathKey(W2U(path)));while(!path.empty()&&path.back()==L'\\')path.pop_back();return path;}
bool isRunningLinkRead(const json& request){const auto operation=request.find("operation"),flag=request.find("runningLinkRead");return operation!=request.end()&&operation->is_string()&&(operation->get<std::string>()=="sleep.inspect"||operation->get<std::string>()=="sleep.session.inspect")&&flag!=request.end()&&flag->is_boolean()&&flag->get<bool>();}
json run(const std::wstring&,const json& request,const std::function<bool()>& admit={},DWORD budget=0){return fakeSteamLiveRun(request,admit);}
}
__LIVE_WRAPPER__
#include "log_snapshot_state.h"
bool domainLogEnabled(const char*){return false;}
void appendNativeLifecycleLog(const char* name,const json& data){sgRecordFact(name,data);}
#include "sleep_steam_recovery.h"
#include "steam_game_link_state.h"
namespace DenseTrackerRuntimeSeam {
using namespace ymcc::sleepsteam;using namespace ymcc::steamlink;
using Game=SleepSteamRecovery::Game;using Client=SleepSteamRecovery::Session;
__LINK_DENSE_SEAM__
}
#undef QueryFullProcessImageNameW
#undef GetProcessMemoryInfo
#undef GetCurrentThreadId
#undef SendMessageTimeoutW
#undef IsWindowVisible
#undef IsIconic
#undef ShowWindowAsync
#undef SetWindowPos
#undef SetForegroundWindow
#undef OpenProcessToken
#undef CreateToolhelp32Snapshot
#undef Module32FirstW
#undef Module32NextW
#undef Sleep
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef ProcessIdToSessionId
#undef OpenProcess
#undef WaitForSingleObject
#undef CloseHandle
std::vector<std::function<void()>> jobs;bool queueAllowed=true,saveAllowed=true;
bool poolSubmit(std::function<void()> job){if(!queueAllowed)return false;jobs.push_back(std::move(job));return true;}
bool ymSettingsPatchSection(const char*,const json&){return saveAllowed;}
// Native original-instance owner is independently tested; this fixture runs
// the legacy coordinator against the production consumers below.
static void steamOriginalRecoverySuspend(unsigned long long){}
static void steamOriginalRecoveryWake(unsigned long long){}
static void steamOriginalRecoveryPowerReady(unsigned long long){}
static bool steamOriginalRecoveryOwnsRunning(DWORD,ULONGLONG){return false;}
#include "sleep_recovery_auto.h"
int passed=0,cases=0;std::wstring base;
void pass(const char* label){++passed;std::cout<<"PASS "<<label<<"\n";}
void reset(){
 privateNativeRecordReply=nullptr;nativeReturnFixture=nullptr;SleepSteamRecovery::nativeRecordPrefetch={};
 captureConfigured.clear();captureWorkingSets.clear();captureVmReadDeniedPids.clear();captureImageFailsPids.clear();captureMemoryFailsPids.clear();onOnePidMemory={};onCaptureRulesSnapshot={};g_gameRulesEpoch=1;
 captureProcesses.clear();captureWindowPids.clear();captureForeignSessionPids.clear();captureIdentityUnavailablePids.clear();captureExcludes.clear();captureWhitelist.clear();onCaptureWindow={};captureSessionQueryFails=false;
 extraForegroundPid=0;extraForegroundPath.clear();extraForegroundCreated=134361500000000111ULL;extraForegroundAvailable=true;
 contactReconcileReceiptOverride=nullptr;contextOverride={};pidWindowExists=false;pidWindowResponsive=pidApiAccepted=pidActivationFocusesOriginal=inputInteractiveIdle=true;pidWindowMismatch=false;
 pidFindCalls=pidProbeCalls=pidActivateCalls=0;onPidProbe={};onPidActivation={};steamOverviewGameid="10307630032478011392";
 probeModules={L"gameoverlayrenderer64.dll",L"xinput1_3.dll"};moduleQueryFails=false;
 registrySteamPid=registryAccountOverride=0;sessionDiscoveryCalls=0;sessionReadMalformed=false;SleepSteamRecovery::liveAccountSession={};g_aiFanMockSession.isolated=false;steamProcessAvailable=observerAvailable=true;registryAccountUnavailable=observerWrongRoot=observerWrongBirth=false;steamReadFailure.clear();
 SleepSteamRecovery::captured={};SleepSteamRecovery::cancelPreparedSuspend();inputContextAvailable=false;gamePaused=false;controllersObserved=true;raiseFocusesOriginal=true;steamFocusedApp=413080;foregroundPid=0;inputCalls=0;
 keepMissingList=false;listRejectReason="steam-list-readback-unconfirmed";preflightRejects=0;windowPrepCalls=0;preflightShapeOverride=nullptr;postMutationGuardFailure=false;lastInputRequest=nullptr;uiReadbackStale=rejectListMutation=false;onReturn={};deferForeground=false;steamOverlayApp=0;foregroundWaitCalls=0;onForegroundWait={};
 ++cases;SG_DIR=base+L"\\case-"+std::to_wstring(cases);steamRoot=SG_DIR+L"\\steam";
 SG_SLEEP_LEASE_DIR=SG_DIR+L"\\leases";assert(sgEnsureMarkerDir(SG_SLEEP_LEASE_DIR));
 assert(sgEnsureMarkerDir(steamRoot+L"\\logs"));SleepRecoveryAuto::state={};SleepRecoveryAuto::kernelWake={};SleepRecoveryAuto::kernelSleepEntry={};modernStandby=false;jobs.clear();facts.clear();events.clear();
 fileTimeBase=134362144573220276ULL;generation=7;tick=0;retry=false;writes=true;g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;
 g_sleepRecoveryAutoEnabled=false;g_sleepRecoveryAutoConfigEpoch=1;g_sleepRecoveryAutoPending=false;
 selected={};owner={};gameExists=valveAvailable=steamAvailable=true;accessDenied=false;steamAccount=42;steamPid=4000;steamCreated=134361500000000000ULL;running=false;localStatus=4;
 steamReads=scopeCalls=0;queueAllowed=saveAllowed=true;onSteamCall={};
 const auto at=SleepSteamRecovery::localSecond(selected.processCreated);
 assert(sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt","["+at+"] Client version: 1788652215\n["+at+"] AppID 10307630032478011392 adding PID 12932 as a tracked process \"\"E:\\GAME\\TMNT.exe\"\"\n"));
}
void job(){assert(!jobs.empty());auto fn=std::move(jobs.front());jobs.erase(jobs.begin());fn();}
void complete(bool simulateConnectedContext=true){
 if(simulateConnectedContext&&!inputContextAvailable){inputContextAvailable=true;controllersObserved=true;steamFocusedApp=2399932135U;}
 const auto limit=SleepRecoveryAuto::state.armed+61000;
  while(g_sleepRecoveryAutoPending&&tick<limit){tick+=1500;sleepRecoveryAutoTick();if(!jobs.empty())job();}
 assert(!g_sleepRecoveryAutoPending);
}
void begin(bool capture=true,ULONGLONG firstTick=1500){SleepRecoveryAuto::set({{"enabled",true}});if(capture){g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);if(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;steamReads=0;}sleepRecoveryAutoArm(7);tick=firstTick;sleepRecoveryAutoTick();}
int wmain(int argc,wchar_t** argv){std::cout<<std::unitbuf;assert(argc==9);base=fspath::absolute(argv[1]).wstring();assert(base.find(L"SleepSteamOnly-20261011")!=std::wstring::npos);assert(sgEnsureMarkerDir(base));
 using namespace ymcc::sleepsteam;
 std::uint32_t app=0;assert(appId("2399932135",app)&&app==2399932135U);assert(appId("-1895035161",app)&&app==2399932135U);assert(appId("10307630032478011392",app)&&app==2399932135U);assert(appId("945360",app)&&app==945360);
 pass("three exact TMNT shortcut identities normalize to one app without 64bit rounding");
 reset();begin(true,0);assert(jobs.size()==1);pass("confirmed ready wake inspects immediately without separate 1500ms grace");
 reset();assert(sleepRecoveryAutoWaitMs(tick)==~ULONGLONG{0});begin(true,0);
 assert(SleepRecoveryAuto::get()["phase"]=="inspecting"&&sleepRecoveryAutoWaitMs(tick)==45001);
 job();assert(SleepRecoveryAuto::get()["phase"]=="confirm-wait"&&sleepRecoveryAutoWaitMs(tick)==1500);
 tick=1499;assert(sleepRecoveryAutoWaitMs(tick)==1);tick=1500;assert(sleepRecoveryAutoWaitMs(tick)==0);
 sleepRecoveryAutoTick();assert(SleepRecoveryAuto::get()["phase"]=="verifying"&&sleepRecoveryAutoWaitMs(tick)>40000);
 pass("actual automatic owner sleeps to its observation or worker deadline instead of 50ms heartbeats");
 reset();running=true;begin(true,0);job();unsigned deadlineWakes=0;
 while(g_sleepRecoveryAutoPending){const auto delay=sleepRecoveryAutoWaitMs(tick);assert(delay!=~ULONGLONG{0});tick+=delay;sleepRecoveryAutoTick();while(!jobs.empty())job();assert(++deadlineWakes<=45);}
 std::cout<<"CPU_SCHEDULE_OBSERVED "<<json({{"deadlineWakes",deadlineWakes},{"tick",tick},{"scopeCalls",scopeCalls},{"inputCalls",inputCalls},{"notifications",workNotifications},{"phase",SleepRecoveryAuto::get()["phase"]}}).dump()<<"\n";
 assert(deadlineWakes==40&&tick==60000&&SleepRecoveryAuto::get()["phase"]=="blocked"&&scopeCalls==0&&inputCalls==0&&workNotifications>0);
 pass("actual sixty-second unchanged observation window uses at most forty scheduled wakes, formerly about twelve hundred");
 if(__COORDINATOR_COST_ONLY__){std::cout<<"Steam-only coordinator cost selected: "<<passed<<" groups PASS\n";return 0;}

 reset();begin();jobs.clear();SleepRecoveryAuto::state.phase="game-wait";
 SleepRecoveryAuto::state.due=tick+750;sleepRecoveryAutoGameReleased(8);
 assert(SleepRecoveryAuto::state.due==tick+750);sleepRecoveryAutoGameReleased(7);
 assert(SleepRecoveryAuto::state.due==tick);pass("game-release hint advances only the same generation and reuses ordinary inspection");

 for(auto bad:{"","0","-0","+1","1.0"," 1","-2147483649","18446744073709551616","10307630032478011393","4294967296","-999999999999999999999"})assert(!appId(bad,app));
 assert(appId("-2147483648",app)&&app==2147483648U);assert(appId("-1",app)&&app==4294967295U);pass("signed/unsigned boundaries, overflow and wrong shortcut GameID layouts fail closed");
 const std::string row="[2026-10-11 10:34:57] AppID 10307630032478011392 adding PID 12932 as a tracked process \"\"E:\\GAME\\TMNT.exe\"\"";
 auto binding=track({row},12932,"e:/GAME/TMNT.exe","2026-10-11 10:34:55","2026-10-11 10:35:00");assert(binding.status=="tracked"&&binding.appid==2399932135U&&binding.exactPath);pass("log-proven PID/path/creation-time window binds shortcut without user AppID entry");
 for(int mode=0;mode<4;++mode){auto v=track({row},mode==0?100:12932,mode==1?"E:\\GAME\\other.exe":"E:\\GAME\\TMNT.exe",mode==2?"2026-10-11 10:35:00":"2026-10-11 10:34:55",mode==3?"2026-10-11 10:34:56":"2026-10-11 10:35:00");assert(v.status!="tracked");}pass("wrong PID/path and reused PID creation window never bind target");
 assert(track({row,"[2026-10-11 10:36:00] AppID -1895035161 no longer tracking PID 12932, exit code 0"},12932,"E:\\GAME\\TMNT.exe","2026-10-11 10:34:55","2026-10-11 10:35:00").status=="removed");
 assert(track({row,"[2026-10-11 10:36:00] Remove 2399932135 from running list"},12932,"E:\\GAME\\TMNT.exe","2026-10-11 10:34:55","2026-10-11 10:35:00").status=="removed");
 assert(track({row,"[2026-10-11 10:36:00] Client version: 1"},12932,"E:\\GAME\\TMNT.exe","2026-10-11 10:34:55","2026-10-11 10:35:00").status=="unknown");pass("untracking/removal and Steam client restart invalidate stale tracking evidence");
 auto duplicate=row;duplicate.replace(duplicate.find("10307630032478011392"),20,"945360");assert(track({row,duplicate},12932,"E:\\GAME\\TMNT.exe","2026-10-11 10:34:55","2026-10-11 10:35:00").status=="ambiguous");
 assert(track(std::vector<std::string>(20001,row),12932,"E:\\GAME\\TMNT.exe","2026-10-11 10:34:55","2026-10-11 10:35:00").status=="incomplete");pass("ambiguous/multiple apps and bounded log scan cannot authorize repair");
 // Production native postflight must not discard bounded settled RETURN evidence.
 for(int mode=0;mode<4;++mode){reset();SleepRecoveryAuto::set({{"enabled",true}});const auto pinnedClient=SleepSteamRecovery::prepareSession();
  nativeReturnFixture={{"source","SteamClient.Apps.RaiseWindowForGame"},{"kind","uint32"},{"observed",true},{"code",3},{"booleanValue",nullptr},{"authority","diagnostic-only-no-admission"},{"nativeMeaningVerified",false},{"gameInputVerified",false},{"continueVerified",false},{"untrustedSecret","NEVER_EXPORT_NATIVE_PRIVATE"}};
  if(mode==2)nativeReturnFixture["code"]="NEVER_EXPORT_NATIVE_PRIVATE";
  if(mode==3)nativeReturnFixture=nullptr;
  onReturn=[mode]{if(mode==1)steamCreated++;else steamAccount=43;};
  const auto refused=SleepSteamRecovery::liveRequest(pinnedClient,{{"operation","sleep.input-resume"},{"appids",json::array({2399932135U})}});
  assert(!refused["ok"].get<bool>()&&refused["mutated"].get<bool>()&&!refused.contains("actionAcknowledged")&&!refused.contains("accepted")&&!refused.contains("verificationPending"));
  assert(refused["reason"]==(mode==1?"steam-session-changed":"steam-account-changed"));
  if(mode<2)assert(refused["nativeReturn"]["code"]==3&&refused["nativeReturnEvidenceVersion"]=="steam-native-return-evidence-v1");else assert(!refused.contains("nativeReturn"));
  assert(refused.dump().find("NEVER_EXPORT_NATIVE_PRIVATE")==std::string::npos&&inputCalls==1);
 }
 pass("native process/account postflight refusal keeps only bounded settled return, with no ACK or replay and no secret leakage");
 // Optional R50 diagnostic identity cannot leak or alter existing admission.
 reset();SleepRecoveryAuto::set({{"enabled",true}});
 privateNativeRecordReply={{"status","observed"},{"source","steam-ui-exact-native-record-identity"},{"steamid64","76561197960265770"},{"gameid64","10307630032478011392"},{"appid",2399932135U}};
 ymcc::steamrecordidentity::Read privateRead;
 auto publicReply=SleepSteamRecovery::liveRequest(SleepSteamRecovery::prepareSession(),{{"operation","sleep.inspect"},{"appids",json::array({2399932135U})}},{},&privateRead);
 assert(privateRead.available&&privateRead.gameId64==10307630032478011392ULL&&!publicReply.contains("_ymccPrivateNativeRecord")&&publicReply.dump().find("76561197960265770")==std::string::npos);
 publicReply=SleepSteamRecovery::liveRequest(SleepSteamRecovery::prepareSession(),{{"operation","sleep.inspect"},{"appids",json::array({2399932135U})}});
 assert(!publicReply.contains("_ymccPrivateNativeRecord"));pass("optional private identity is consumed before public export even when unrequested");
 reset();SleepRecoveryAuto::set({{"enabled",true}});tick=10000;
 const auto savedClient=SleepSteamRecovery::prepareSession();const auto savedGame=SleepSteamRecovery::currentGame();
 SleepSteamRecovery::Captured diagnosticCapture;diagnosticCapture.generation=diagnosticCapture.boundaryGeneration=7;diagnosticCapture.configEpoch=g_sleepRecoveryAutoConfigEpoch.load();diagnosticCapture.at=tick;diagnosticCapture.powerBoundaryFileTime=sgNowFileTime();diagnosticCapture.client=savedClient;diagnosticCapture.game=savedGame;
 diagnosticCapture.targetSelection={{"jointBindingVerified",true},{"selectedAppid",2399932135U}};
 auto& prefetch=SleepSteamRecovery::nativeRecordPrefetch;prefetch.client=savedClient;prefetch.game=savedGame;prefetch.identity={76561197960265770ULL,10307630032478011392ULL,2399932135U,1,true,"native-record-pre-capture-ui-bound"};
 prefetch.witness={7,diagnosticCapture.configEpoch,tick-1500,sgNowFileTime()-15000000};prefetch.nativeBefore={{"authority","diagnostic-only-no-admission"},{"recordState","not-observed"}};
 SleepSteamRecovery::bindNativeRecordIdentity(diagnosticCapture);assert(diagnosticCapture.nativeRecord.bound&&diagnosticCapture.inputBefore.is_object());
 assert(diagnosticCapture.inputBefore["nativeRecordPrefetchBefore"]["recordState"]=="not-observed");assert(diagnosticCapture.inputBefore.dump().find("76561197960265770")==std::string::npos);
 pass("pre-capture diagnostic cache binds exact identity with bounded earlier witness, no physical sleep or native presence proof");
 for(int mode=0;mode<7;++mode){auto denied=diagnosticCapture;denied.nativeRecord={};denied.inputBefore=nullptr;auto prior=prefetch;
  if(mode==0)prefetch.witness.observedTick=tick+1;if(mode==1)prefetch.witness.observedFileTime=sgNowFileTime()+1;if(mode==2)prefetch.witness.generation=8;
  if(mode==3)prefetch.witness.epoch++;if(mode==4)prefetch.game.created++;if(mode==5)prefetch.client.account++;if(mode==6)denied.targetSelection["jointBindingVerified"]=false;
  SleepSteamRecovery::bindNativeRecordIdentity(denied);assert(!denied.nativeRecord.bound);prefetch=prior;
 }
 pass("late, future, changed generation/account/game or rejected target never adopts diagnostic cache as original");
 reset();localStatus=11;begin();auto withoutDiagnostic=SleepSteamRecovery::inspect(7);auto& owned=SleepSteamRecovery::captured;
 owned.nativeRecord={76561197960265770ULL,10307630032478011392ULL,2399932135U,1,true,"native-record-pre-capture-ui-bound"};
 auto withDiagnostic=SleepSteamRecovery::inspect(7);assert(withDiagnostic["steamRecovery"]==withoutDiagnostic["steamRecovery"]&&withDiagnostic["steamInputRecovery"]==withoutDiagnostic["steamInputRecovery"]);
 assert(withDiagnostic["nativeRecordDiagnostic"]["status"]=="unavailable"&&withDiagnostic["nativeRecordDiagnostic"]["recordState"]=="unknown");
 assert(scopeCalls==0&&inputCalls==0);pass("native record diagnostic unavailability cannot modify association/input action admission or issue a repair");
 reset();auto observed=SleepSteamRecovery::inspect(7);assert(observed["tracker"]["status"]=="tracked"&&observed["steamRecovery"]["repairable"]);assert(observed["game"]["identityMatches"]&&scopeCalls==0);pass("real Steam-only observer derives bound target and stale UI from process/creation/path + tracker + backend");
 reset();running=true;observed=SleepSteamRecovery::inspect(7);assert(observed["steamRecovery"]["status"]=="current"&&observed["steamRecovery"]["associationVerified"]&&!observed["healthVerified"]);pass("already-current association is positively observed without a write or game-input claim");
  reset();localStatus=11;steamOverviewGameid="wrong";observed=SleepSteamRecovery::inspect(7);assert(observed["steamRecovery"]["status"]=="blocked"&&!observed["steamRecovery"]["repairable"]&&!observed["steamRecovery"]["contactRepairable"]);pass("backend ReadyToLaunch without exact contact proof remains blocked, not repaired by fabricating running state");
 reset();gameExists=false;observed=SleepSteamRecovery::inspect(7);assert(!observed["steamRecovery"]["repairable"]&&!observed["steamRecovery"]["associationVerified"]);reset();accessDenied=true;observed=SleepSteamRecovery::inspect(7);assert(!observed["steamRecovery"]["repairable"]);pass("exited/inaccessible original game remains unknown, no launch or PID substitution");
 reset();observed=SleepSteamRecovery::inspect(7);SleepRecoveryAuto::set({{"enabled",true}});auto receipt=SleepSteamRecovery::reconcile(observed,7,g_sleepRecoveryAutoConfigEpoch);assert(receipt["accepted"]&&scopeCalls==1&&running);pass("native caller rebinds fresh identities before a single admitted Valve UI rescope");
 for(int mode=0;mode<6;++mode){reset();observed=SleepSteamRecovery::inspect(7);SleepRecoveryAuto::set({{"enabled",true}});if(mode==0)selected.processCreated++;if(mode==1)steamAccount=43;if(mode==2)steamPid=4001;if(mode==3){localStatus=11;steamOverviewGameid="wrong";}if(mode==4)generation=8;if(mode==5)steamCreated++;
 receipt=SleepSteamRecovery::reconcile(observed,7,g_sleepRecoveryAutoConfigEpoch);assert(!receipt["accepted"]&&scopeCalls==0);}pass("changed game/Steam account/client PID/backend/generation refuses stale write");
 reset();observed=SleepSteamRecovery::inspect(7);SleepRecoveryAuto::set({{"enabled",true}});onSteamCall=[](const auto& op){if(op=="sleep.reconcile")gameExists=false;};receipt=SleepSteamRecovery::reconcile(observed,7,g_sleepRecoveryAutoConfigEpoch);assert(!receipt["accepted"]&&scopeCalls==0);pass("game death after fresh inspection but before CDP admission vetoes mutation");
 reset();SleepRecoveryAuto::set({{"enabled",true}});assert(!g_sleepRecoveryAutoPending&&jobs.empty()&&steamReads==0);pass("enable persists future-wake intent only, no startup/current-game repair");
 reset();saveAllowed=false;auto value=SleepRecoveryAuto::set({{"enabled",true}});assert(!value["ok"]&&!g_sleepRecoveryAutoEnabled);pass("failed preference save does not admit automatic repair");
 reset();begin();assert(jobs.size()==1);job();assert(scopeCalls==1&&SleepRecoveryAuto::get()["phase"]=="confirm-wait");tick=3000;sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending);complete();value=SleepRecoveryAuto::get();assert(value["phase"]=="recorded"&&value["scope"]=="steam"&&value["steamRecovery"]["associationVerified"]);
 const auto evidence=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(evidence["scope"]=="steam"&&!evidence["healthVerified"]&&!evidence["uploaded"]&&evidence.contains("after"));pass("Steam-only automatic wake performs guarded rescope then bounded native verification and local evidence");
  reset();running=true;begin();job();assert(g_sleepRecoveryAutoPending);complete();assert(scopeCalls==0&&SleepRecoveryAuto::get()["phase"]=="recorded"&&SleepRecoveryAuto::get()["steamRecovery"]["associationVerified"]);pass("already-running game gets explicit Steam-current verdict, not generic no-safe-action");
  for(int receiptMode=0;receiptMode<4;++receiptMode){
    reset();localStatus=11;begin();auto contactInspected=SleepSteamRecovery::inspect(7);
    if(receiptMode==1)contactReconcileReceiptOverride={{"requested",true},{"proofAccepted",false},{"accepted",true}};
    if(receiptMode==2)contactReconcileReceiptOverride=true;
    if(receiptMode==3)contactReconcileReceiptOverride={{"proofAccepted",true},{"accepted",true}};
    onSteamCall=[](const auto& op){if(op=="sleep.reconcile")localStatus=4;};
    const auto checkedContact=SleepSteamRecovery::reconcile(contactInspected,7,g_sleepRecoveryAutoConfigEpoch);
    assert(scopeCalls==1&&checkedContact["accepted"]==(receiptMode==0));
    if(receiptMode)assert(checkedContact["ok"]==false&&checkedContact["reason"]=="steam-contact-rescope-not-confirmed");
  }
  pass("R47 native requires a consistent positive contact receipt; missing/wrong-type/contradictory proof cannot be accepted");
  reset();localStatus=11;onSteamCall=[](const auto& op){if(op=="sleep.reconcile")localStatus=4;};begin();job();assert(scopeCalls==1&&SleepRecoveryAuto::get()["steamRecovery"]["contactConnected"]==true);complete(false);assert(SleepRecoveryAuto::get()["steamRecovery"]["associationVerified"]==true&&inputCalls==0);pass("R47 exact contact proof permits one Valve scope and requires Running readback before any input path");
  reset();localStatus=11;steamOverviewGameid="wrong";begin();job();assert(g_sleepRecoveryAutoPending);complete(false);assert(scopeCalls==0&&SleepRecoveryAuto::get()["phase"]=="blocked");tick=999999;sleepRecoveryAutoTick();assert(jobs.empty());pass("backend-not-running without exact contact proof is visible blocked, terminal, and never loops/restarts Steam");
 reset();steamAvailable=false;begin();job();complete(false);assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&scopeCalls==0);pass("missing Steam read channel remains unknown/blocked, never auto-enables CDP");
 reset();begin();SleepRecoveryAuto::set({{"enabled",false}});job();assert(scopeCalls==0&&steamReads==0);pass("disable cancels queued Steam-only worker before read/write");
 reset();begin();generation=8;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoTick();job();assert(!g_sleepRecoveryAutoPending&&scopeCalls==0);pass("new sleep cancels stale Steam repair without touching device owners");
 reset();SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoArm(7);g_powerLifecycle=PowerLifecycle::Resuming;tick=30000;sleepRecoveryAutoTick();assert(!g_sleepRecoveryAutoPending&&jobs.empty());pass("power-never-ready wait is bounded, not an input or fan readiness gate");
 reset();queueAllowed=false;begin();assert(SleepRecoveryAuto::get()["reason"]=="automatic-worker-queue-full"&&!g_sleepRecoveryAutoPending);pass("full worker queue records one terminal failure without extra threads");
 reset();begin();tick=50001;sleepRecoveryAutoTick();job();assert(scopeCalls==0&&steamReads==0);pass("late worker cannot repair after its stage timeout");
 reset();running=true;begin();job();complete();sleepRecoveryAutoArm(7);tick+=10000;sleepRecoveryAutoTick();assert(jobs.empty());generation=8;sleepRecoveryAutoArm(8);tick+=1500;sleepRecoveryAutoTick();assert(jobs.size()==1);pass("one Steam check per explicit generation, future legitimate wake remains eligible");


 // MSI 14:12 wake: cached app focus at +1.5s, controller returns at +21s.
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;controllersObserved=false;foregroundPid=steamPid;begin();job();
 assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="settle-wait"&&SleepRecoveryAuto::get()["steamInputRecovery"]["status"]=="waiting-controller"&&inputCalls==0);
 tick=21000;controllersObserved=true;steamFocusedApp=413080;sleepRecoveryAutoTick();job();assert(inputCalls==1&&scopeCalls==0&&g_sleepRecoveryAutoPending);
 complete(false);auto msiEvidence=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(msiEvidence["phase"]=="recorded"&&msiEvidence["settling"]["lastElapsedMs"]>=30000&&msiEvidence["observations"].size()>=3&&msiEvidence["attempts"]["input"]==1);
 pass("MSI empty-controller cached focus cannot finish early; +21s reconnect gets one original-game return and stable rechecks");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;controllersObserved=false;begin();job();complete(false);
 assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::get()["reason"]=="steam-controller-reconnect-not-observed"&&inputCalls==1&&windowPrepCalls==1&&scopeCalls==0);
 pass("no controller reconnect within 60 seconds is bounded failure, never falsely context-current");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;begin();job();assert(g_sleepRecoveryAutoPending);tick=28500;sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending);
 steamFocusedApp=413080;tick=30000;sleepRecoveryAutoTick();job();assert(inputCalls==1);complete(false);assert(SleepRecoveryAuto::get()["phase"]=="recorded");
 pass("already-current first sample is watched through late focus drift before completing; not an immediate library-only success");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;begin();job();controllersObserved=false;tick=28500;sleepRecoveryAutoTick();job();controllersObserved=true;tick=30000;sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending);
 tick=39000;sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending);complete(false);assert(SleepRecoveryAuto::get()["phase"]=="recorded");
 pass("a late controller disappearance resets the stable window even when cached focus never changes");
 reset();SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();selected.pid=13000;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);assert(SleepSteamRecovery::captured.game.pid==12932&&SleepSteamRecovery::captured.boundary=="power-suspend-notification");
 if(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoArm(7);tick=1500;sleepRecoveryAutoTick();job();assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==0&&scopeCalls==0);
 pass("early suspend notification pins old PID even when delayed queue sees a new game after wake; no replacement launch");
 reset();SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();generation=8;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(8);assert(SleepSteamRecovery::captured.game.pid==12932&&SleepSteamRecovery::captured.generation==8);
 pass("early suspend identity transfers only into the same or immediately next power generation");
 reset();SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();sleepRecoveryAutoCancelPreparedSuspend();g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);assert(SleepSteamRecovery::captured.boundary=="queued-suspend");
 pass("aborted suspend discards early identity receipt, not a stale original-game authority");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;begin();job();SleepRecoveryAuto::set({{"enabled",false}});tick=21000;sleepRecoveryAutoTick();assert(jobs.empty()&&!g_sleepRecoveryAutoPending&&inputCalls==0);
 pass("single switch off cancels settling checks and late reconnect actions");

 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;begin();job();
 auto continueReport=SleepSteamRecovery::inspect(7);assert(continueReport["continueRecovery"]["candidate"]&&!continueReport["continueRecovery"]["continueButtonVerified"].get<bool>());
 auto selectedDrift=continueReport["steam"];selectedDrift["overviews"][0]["selectedDisplayStatus"]=11;
 auto continueWaiting=continueVerdict(selectedDrift,continueReport["steamRecovery"]);assert(!continueWaiting["candidate"].get<bool>()&&continueWaiting["reason"]=="steam-selected-client-not-running"&&scopeCalls==0);
 selectedDrift["overviews"][0]["selectedDisplayStatus"]=nullptr;assert(!continueVerdict(selectedDrift,continueReport["steamRecovery"])["candidate"].get<bool>());
 pass("Continue diagnostics require selected and local Running, never turn association alone into a button proof or fake selected state");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;begin();job();tick=21000;controllersObserved=true;selected.processCreated++;sleepRecoveryAutoTick();job();assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==0&&scopeCalls==0);
 pass("same PID with a new creation time during late reconnect cannot recover a replacement game");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;begin();job();tick=21000;generation=8;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoTick();assert(!g_sleepRecoveryAutoPending&&jobs.empty());
 pass("a new sleep during settling cancels all old-generation reconnect checks");

 for(const auto delay:{0ULL,21000ULL,45000ULL}){
     reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;controllersObserved=false;begin();job();
     tick=std::max<ULONGLONG>(3000,delay);controllersObserved=true;sleepRecoveryAutoTick();job();complete(false);
     assert(SleepRecoveryAuto::get()["phase"]=="recorded"&&inputCalls==1&&windowPrepCalls==1&&scopeCalls==0);
 }
 pass("brand-neutral Steam observation tolerates immediate, 21-second and 45-second enumeration without device rebuilds");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;begin();job();auto overlaySample=SleepSteamRecovery::inspect(7);overlaySample["steam"]["inputContext"]["focus"]["overlayAppid"]=2399932135U;
 assert(settleReason(overlaySample)=="steam-overlay-focus-active-or-unknown");
 pass("cached original-game focus plus active overlay cannot satisfy the stable game-context window");

 reset();SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();selected.pid=13000;sleepRecoveryAutoPrepareSuspend();g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);
 assert(SleepSteamRecovery::captured.game.pid==12932);sleepRecoveryAutoPrepareSuspend();assert(!SleepSteamRecovery::preparedPending);
 pass("duplicate or delayed suspend broadcast never replaces the earliest original-game identity with a post-wake process");

 reset();running=true;inputContextAvailable=true;registrySteamPid=1736;begin();job();assert(inputCalls==1&&SleepSteamRecovery::captured.client.pid==4000);
 auto staleRegistry=SleepSteamRecovery::inspect(7);assert(staleRegistry["steamClient"]["registeredPid"]==1736&&staleRegistry["steamClient"]["pid"]==4000&&staleRegistry["steamClient"]["source"]=="verified-session-observer"&&staleRegistry["game"]["identityMatches"]==true);
 complete(false);assert(SleepRecoveryAuto::get()["phase"]=="recorded");
 pass("MSI dead registry PID 1736 resolves only to verified Steam observer PID/image/birth, never a game or stale PID");
 reset();running=true;inputContextAvailable=true;begin();steamReadFailure="steam-session-starting";owner={12932,selected.processCreated,true};job();
 assert(SleepRecoveryAuto::get()["phase"]=="game-wait"&&inputCalls==0&&scopeCalls==0);
 tick=10032;owner={};sleepRecoveryAutoTick();job();assert(SleepRecoveryAuto::get()["phase"]=="session-wait"&&inputCalls==0&&g_sleepRecoveryAutoPending);
 tick=13000;steamReadFailure.clear();sleepRecoveryAutoTick();job();assert(inputCalls==1&&scopeCalls==0);complete(false);assert(SleepRecoveryAuto::get()["phase"]=="recorded");
 pass("19:47 ordering: original lease release at +10s then same-account reconnect at +13s does not terminate at +1.5s");
 reset();running=true;inputContextAvailable=true;begin();steamProcessAvailable=false;job();
 assert(SleepRecoveryAuto::get()["phase"]=="session-wait"&&inputCalls==0);auto unknownClient=SleepSteamRecovery::inspect(7);
 assert(unknownClient["game"]["identityMatches"]==true&&unknownClient["game"]["recoveryBindingMatches"]==false&&unknownClient["steamSessionState"]["status"]=="waiting");
 tick=12000;steamProcessAvailable=true;sleepRecoveryAutoTick();job();assert(inputCalls==1);complete(false);
 pass("temporary unreadable Steam identity is separated from live original-game identity and can resume only on the same verified client");
 reset();running=true;inputContextAvailable=true;begin();steamReadFailure="steam-session-starting";job();steamAccount=43;steamReadFailure.clear();tick=13000;sleepRecoveryAutoTick();job();
 assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::get()["reason"]=="steam-original-session-changed"&&inputCalls==0&&scopeCalls==0);
 pass("a real account switch during bounded reconnect refuses original wake authority; no rebinding to another user");
 reset();running=true;inputContextAvailable=true;begin();steamReadFailure="steam-session-starting";job();steamPid++;steamCreated++;steamReadFailure.clear();tick=13000;sleepRecoveryAutoTick();job();
 assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==0&&scopeCalls==0);
 pass("Steam restart/PID-birth change during reconnect is not a temporary outage and cannot authorize an old wake");
 reset();running=true;inputContextAvailable=true;begin();steamReadFailure="steam-account-changed";job();assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::get()["reason"]=="steam-live-account-changed"&&inputCalls==0);
 pass("a nonzero live UI account mismatch still terminates; JS/native account checks are never bypassed");
 reset();running=true;inputContextAvailable=true;begin();steamReadFailure="steam-session-starting";job();complete(false);assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&tick<=61000&&inputCalls==0&&scopeCalls==0);
 pass("Steam reconnect never ready is bounded by the original 60s wake deadline without a write or replay");
 reset();running=true;inputContextAvailable=true;begin();steamReadFailure="steam-session-starting";job();SleepRecoveryAuto::set({{"enabled",false}});tick=13000;steamReadFailure.clear();sleepRecoveryAutoTick();assert(!g_sleepRecoveryAutoPending&&jobs.empty()&&inputCalls==0);
 pass("disable cancels pending session wait without deferred Steam mutation");
 reset();running=true;inputContextAvailable=true;begin();steamReadFailure="steam-session-starting";job();selected.processCreated++;tick=13000;steamReadFailure.clear();sleepRecoveryAutoTick();job();assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==0);
 pass("same PID with a new game birth cannot exploit a session-read retry to substitute the original instance");
 reset();registrySteamPid=selected.pid;observerAvailable=false;auto foreignRegistry=SleepSteamRecovery::session();assert(foreignRegistry.pid==0&&foreignRegistry.reason=="steam-process-unverified");
 pass("registry PID pointing at an actual game is rejected by exact steam.exe image proof, not just liveness");
 reset();registrySteamPid=1736;observerAvailable=false;running=true;inputContextAvailable=true;begin();job();assert(SleepRecoveryAuto::get()["reason"]=="steam-original-session-not-captured"&&inputCalls==0&&scopeCalls==0);
 pass("no verified client at suspend cannot acquire a new post-wake session as if it were the original");

 for(int mode=0;mode<2;++mode){reset();registrySteamPid=1736;if(mode==0)observerWrongRoot=true;else observerWrongBirth=true;
   auto wrongObserver=SleepSteamRecovery::session();assert(!SleepSteamRecovery::validSession(wrongObserver)&&wrongObserver.pid==0);
 }
 pass("observer fallback rejects another install root or stale cached process birth; no blind trust in cached PID");
 reset();running=true;inputContextAvailable=true;registrySteamPid=1736;begin();registryAccountUnavailable=true;steamReadFailure="steam-session-starting";job();
 assert(SleepRecoveryAuto::get()["phase"]=="session-wait"&&inputCalls==0);tick=13000;registryAccountUnavailable=false;steamReadFailure.clear();sleepRecoveryAutoTick();job();assert(inputCalls==1);complete(false);
 pass("cached account only selects a read target during registry outage; zero live account still blocks every write until exact user is ready");

 // Round15: 21:43 failure has a live tracked original but local UI non-running.
 reset();localStatus=11;inputContextAvailable=true;begin();job();
 assert(inputCalls==1&&scopeCalls==1&&SleepRecoveryAuto::get()["steamInputRecovery"]["contextMatched"]==true&&!SleepRecoveryAuto::get()["steamRecovery"]["associationVerified"].get<bool>());
 assert(lastInputRequest["originalTarget"]["pid"]==12932&&lastInputRequest["originalTarget"]["created"]==std::to_string(selected.processCreated)&&lastInputRequest["originalTarget"]["basis"]=="captured-original-tracker");
 complete(false);auto fallbackEvidence=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(fallbackEvidence["phase"]=="blocked"&&fallbackEvidence["attempts"]["input"]==1&&fallbackEvidence["attempts"]["list"]==1&&!fallbackEvidence["after"]["continueRecovery"]["candidate"].get<bool>()&&!fallbackEvidence["healthVerified"].get<bool>());
 pass("live captured original may get ONE native return while ReadyToLaunch; secondary remains blocked, never fabricated recorded");
 for(int status:{0,12,999}){reset();localStatus=status;inputContextAvailable=true;begin();job();assert(inputCalls==0&&scopeCalls==0);}
 pass("unknown/non-ReadyToLaunch overview states never get original-tracker return authority");
  reset();localStatus=11;inputContextAvailable=true;begin(false);job();assert(inputCalls==0&&SleepRecoveryAuto::get()["phase"]=="blocked");
 pass("ReadyToLaunch original-tracker path still requires a real suspend capture");
  for(int mode=0;mode<4;++mode){reset();localStatus=11;inputContextAvailable=true;begin();if(mode==0)selected.processCreated++;if(mode==1)steamPid++;if(mode==2)gamePaused=true;if(mode==3)foregroundPid=13000;job();assert(inputCalls==0&&scopeCalls==(mode==3?1:0));}
 pass("tracker fallback never substitutes game/client identity, resumes manual pause or steals another OS window");
 reset();selected.pid=0;begin();job();auto emptyCapture=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(emptyCapture["after"]["suspendCapture"]["available"]==false&&emptyCapture["after"]["game"]["identityMatches"]==false);
 pass("two zero-PID empty identities cannot be recorded as an available original suspend capture");
  reset();localStatus=11;inputContextAvailable=true;
 assert(sgWriteFileAtomic(SleepRecoveryAuto::reportPath(),json{{"schema",4},{"generation",6},{"configEpoch",1},{"phase","blocked"},{"reason","old-no-game"}}.dump()));
 begin();job();const int savedReads=steamReads,savedInput=inputCalls,savedScope=scopeCalls;const auto savedJobs=jobs.size();
 auto livePath=SleepRecoveryAuto::exportCurrentSnapshot();assert(!livePath.empty());auto liveSnapshot=json::parse(readFile(livePath));
 assert(liveSnapshot["generation"]==7&&liveSnapshot["reportState"]=="pending"&&liveSnapshot["lastCompletedReport"]["generation"]==6&&liveSnapshot["lastCompletedReport"]["sameGenerationAsCurrent"]==false);
 assert(liveSnapshot["after"]["steam"]["overviews"][0]["localDisplayStatus"]==11&&liveSnapshot["observations"][0]["overviews"][0]["localDisplayStatus"]==11);
 assert(liveSnapshot["repairTriggered"]==false&&liveSnapshot["readOnly"]==true&&liveSnapshot["healthVerified"]==false&&liveSnapshot["after"]["repairTicketReusable"]==false);
 assert(steamReads==savedReads&&inputCalls==savedInput&&scopeCalls==savedScope&&jobs.size()==savedJobs&&g_sleepRecoveryAutoPending);
 pass("export copies pending exact statuses and distinguishes old final generation, without inspect/repair/queue/state mutation");
 complete(false);liveSnapshot=json::parse(readFile(SleepRecoveryAuto::exportCurrentSnapshot()));assert(liveSnapshot["reportState"]=="terminal"&&liveSnapshot["lastCompletedReport"]["sameGenerationAsCurrent"]==true);
 pass("terminal copied export agrees with saved final generation while input success cannot imply Continue");
  reset();localStatus=11;inputContextAvailable=true;begin();job();generation=8;g_powerLifecycle=PowerLifecycle::Suspending;
 liveSnapshot=json::parse(readFile(SleepRecoveryAuto::exportCurrentSnapshot()));assert(liveSnapshot["reportState"]=="superseded"&&liveSnapshot["currentPowerGeneration"]==8&&liveSnapshot["generation"]==7&&inputCalls==1);
 sleepRecoveryAutoTick();sleepRecoveryAutoCaptureSuspend(8);if(!jobs.empty())job();running=false;steamFocusedApp=413080;foregroundPid=0;g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoArm(8);tick+=1500;sleepRecoveryAutoTick();job();
 liveSnapshot=json::parse(readFile(SleepRecoveryAuto::exportCurrentSnapshot()));assert(liveSnapshot["generation"]==8&&liveSnapshot["reportState"]=="pending"&&liveSnapshot["attempts"]["input"]==1&&inputCalls==2&&scopeCalls==2);
 pass("two nearby sleeps cancel stale generation then independently return exact same original once in new generation");
 reset();SleepRecoveryAuto::set({{"enabled",true}});const auto idleJobs=jobs.size();liveSnapshot=json::parse(readFile(SleepRecoveryAuto::exportCurrentSnapshot()));assert(liveSnapshot["reportState"]=="idle"&&steamReads==0&&inputCalls==0&&scopeCalls==0&&jobs.size()==idleJobs);
 pass("idle export is pure cached evidence, not an implicit repair or live Steam inspection");

 // Round16: cached AppID != actual original OS foreground. No double action.
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=steamPid;begin();job();
 assert(inputCalls==1&&lastInputRequest["originalWindowReturn"]==true&&lastInputRequest["originalTarget"]["basis"]=="current-association"&&SleepRecoveryAuto::get()["steamInputRecovery"]["contextMatched"]==true);
 complete(false);assert(SleepRecoveryAuto::get()["phase"]=="recorded"&&inputCalls==1);pass("cached original Steam focus with verified Steam foreground gets ONE actual original-window return instead of a no-op");
  reset();localStatus=11;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=steamPid;begin();job();
 assert(inputCalls==1&&lastInputRequest["originalWindowReturn"]==true&&lastInputRequest["originalTarget"]["basis"]=="captured-original-tracker"&&SleepRecoveryAuto::get()["steamInputRecovery"]["contextMatched"]==true);
 complete(false);assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==1&&scopeCalls==1);pass("cached original focus ReadyToLaunch can return original OS window but never changes Continue or counts as recorded");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=steamPid;deferForeground=true;begin();
 onForegroundWait=[](){if(foregroundWaitCalls==2)foregroundPid=selected.pid;};job();assert(inputCalls==1&&foregroundWaitCalls==2&&SleepRecoveryAuto::get()["phase"]=="confirm-wait");
 auto delayedWindow=json::parse(readFile(SleepRecoveryAuto::exportCurrentSnapshot()));assert(delayedWindow["receipt"]["inputResume"]["foregroundWaitMs"]==50&&delayedWindow["receipt"]["inputResume"]["originalWindowFocused"]==true);
 pass("cached JS focus readback waits for a genuinely delayed original OS foreground rather than accepting it early or replaying");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=steamPid;deferForeground=true;begin();job();
 assert(inputCalls==1&&foregroundWaitCalls==30&&g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.receipt["inputResume"]["reason"]=="steam-original-window-not-confirmed");complete(false);assert(inputCalls==1&&foregroundWaitCalls==30&&tick<=61000&&SleepRecoveryAuto::get()["phase"]=="blocked");
 pass("immediate original-window wait remains750ms/30 reads; remaining wake reads end bounded and cannot accept cached AppID or replay");
 for(int mode=0;mode<2;++mode){reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=mode==0?6000:GetCurrentProcessId();begin();job();
  assert(inputCalls==0&&SleepRecoveryAuto::get()["steamInputRecovery"]["repairable"]==false&&SleepRecoveryAuto::get()["steamInputRecovery"]["contextMatched"]==false);}
 pass("cached original AppID cannot steal another application or an active YeManCC settings window");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=steamPid;steamOverlayApp=2399932135U;begin();job();
 assert(inputCalls==0&&SleepRecoveryAuto::get()["steamInputRecovery"]["reason"]=="steam-overlay-focus-active-or-unknown");pass("matching cached AppID plus actual Steam foreground does not bypass active Overlay ownership");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=steamPid;deferForeground=true;begin();onForegroundWait=[](){foregroundPid=6000;};job();
 assert(inputCalls==1&&foregroundWaitCalls==1&&g_sleepRecoveryAutoPending);complete(false);assert(inputCalls==1&&foregroundWaitCalls==1&&foregroundPid==6000&&SleepRecoveryAuto::get()["phase"]=="blocked");pass("user focus change stops native window actions immediately; remaining original-game reads never steal focus or replay");
 for(int mode=0;mode<3;++mode){reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=steamPid;deferForeground=true;begin();
  onForegroundWait=[mode](){if(mode==0)gamePaused=true;if(mode==1){generation=8;g_powerLifecycle=PowerLifecycle::Suspending;}if(mode==2)selected.processCreated++;};
  auto cachedReport=SleepSteamRecovery::inspect(7);auto windowChanged=SleepSteamRecovery::resumeInputContext(cachedReport,7,g_sleepRecoveryAutoConfigEpoch.load());
  assert(inputCalls==1&&foregroundWaitCalls==1&&windowChanged["accepted"]==false&&windowChanged["reason"]=="steam-input-state-changed-after-request");}
 pass("manual pause/new sleep/original birth change during OS foreground confirmation revokes acceptance without releasing owners");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=steamPid;begin(false);job();assert(inputCalls==0);pass("cached-focus actual-window return cannot acquire authority without the original suspend capture");

 // Round32: the exact original native window can be observed in the fresh read
 // immediately after a zero-wait window miss. Retain the failed receipt and
 // the remaining bounded observations, never replay the already sent action.
 const auto delayedField=json::parse(readFile(argv[8]));
 assert(delayedField["implementation"]=="steam-wake-round31-native-predispatch-wait");
 assert(delayedField["inputResume"]["mutated"]==true&&delayedField["inputResume"]["actionAcknowledged"]==true);
 assert(delayedField["inputResume"]["accepted"]==false&&delayedField["inputResume"]["foregroundWaitMs"]==0);
 assert(delayedField["inputResume"]["originalWindowFocused"]==false&&delayedField["after"]["originalGameRecovery"]["originalWindowFocused"]==true);
 assert(delayedField["after"]["steam"]["overviews"][0]["localDisplayStatus"]==11&&!delayedField["actualInputVerified"].get<bool>());
 pass("R31 MSI023942 complete field receipt retains zero-wait miss and later exact original native foreground separately");
 const auto delayedBegin=[](){
   reset();running=false;localStatus=11;inputContextAvailable=true;uiReadbackStale=true;
   extraForegroundPid=6000;extraForegroundPath=L"C:\\Other\\foreign.exe";
   begin();onReturn=[]{foregroundPid=extraForegroundPid;};
   onSteamCall=[](const auto& op){if(op=="sleep.inspect"&&std::any_of(facts.begin(),facts.end(),[](const auto& f){return f.first=="sleep-steam-input-resume-result";}))foregroundPid=selected.pid;};
   job();onReturn={};onSteamCall={};
   assert(inputCalls==1&&scopeCalls==1&&pidActivateCalls==0);
   assert(SleepRecoveryAuto::state.receipt["inputResume"]["reason"]=="steam-original-window-not-confirmed");
   assert(SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false&&SleepRecoveryAuto::state.receipt["inputResume"]["foregroundWaitMs"]==0);
   assert(SleepRecoveryAuto::state.last["foreground"]["role"]=="original-game");
 };
 delayedBegin();
#if __DELAYED_WINDOW_BASELINE__
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="blocked");
 assert(SleepRecoveryAuto::state.timeline.empty());
 pass("baseline reproduces early owner termination despite the fresh read already matching the original window");
#else
 assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.postReturnDelayedWindowObserved);
 assert(SleepRecoveryAuto::state.timeline.size()==1&&SleepRecoveryAuto::state.inputAttempted);
 complete(false);auto delayedLost=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(delayedLost["phase"]=="blocked"&&delayedLost["recordEpoch"]==60.0);
 assert(delayedLost["settling"]["lastElapsedMs"]==58500&&delayedLost["settling"]["samples"]==39&&delayedLost["observations"].size()==39);
 assert(delayedLost["receipt"]["inputResume"]["accepted"]==false&&delayedLost["after"]["steam"]["overviews"][0]["localDisplayStatus"]==11);
 assert(delayedLost["postReturnObservation"]["delayedOriginalWindowObserved"]==true);
 assert(!delayedLost["healthVerified"].get<bool>()&&inputCalls==1&&scopeCalls==1&&pidActivateCalls==0);
 pass("delayed original readback retains native11 observations to the original deadline without promoting receipt or sending a second return");
 delayedBegin();tick=35000;localStatus=4;running=true;steamFocusedApp=2399932135U;sleepRecoveryAutoTick();job();complete(false);
 auto delayedHealthy=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(delayedHealthy["phase"]=="recorded"&&delayedHealthy["settling"]["lastElapsedMs"]>=58500);
 assert(delayedHealthy["receipt"]["inputResume"]["accepted"]==false&&!delayedHealthy["healthVerified"].get<bool>());
 assert(inputCalls==1&&scopeCalls==1&&pidActivateCalls==0);
 pass("later genuine synthetic original context convergence is observable while the failed action receipt stays unchanged and gameplay unverified");
 delayedBegin();foregroundPid=steamPid;localStatus=4;running=false;tick=3000;sleepRecoveryAutoTick();job();
 assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.postReturnForegroundWait&&scopeCalls==1&&inputCalls==1&&pidActivateCalls==0);
 assert(SleepRecoveryAuto::state.timeline.back()["stage"]=="post-return-foreground-wait");
 pass("foreground loss after delayed readback keeps reads and blocks rescope or PID activation from stealing the foreground");
 for(int mode=0;mode<6;++mode){delayedBegin();if(mode==0)SleepRecoveryAuto::set({{"enabled",false}});
   if(mode==1)generation++;if(mode==2)selected.processCreated++;if(mode==3)steamCreated++;if(mode==4)steamAccount++;if(mode==5)gamePaused=true;
   tick=3000;sleepRecoveryAutoTick();if(!jobs.empty())job();assert(!g_sleepRecoveryAutoPending&&inputCalls==1&&scopeCalls==1&&pidActivateCalls==0);}
 pass("off/new sleep/original PID birth/Steam birth/account/manual pause revoke delayed-readback observations with no replay");
 assert(SleepRecoveryAuto::delayedOriginalWindowReadback(delayedField["inputResume"],delayedField["before"],delayedField["after"]));
 for(int mode=0;mode<22;++mode){auto q=delayedField["inputResume"],b=delayedField["before"],a=delayedField["after"];
   if(mode==0)q["mutated"]=false;if(mode==1)q["actionAcknowledged"]=false;if(mode==2)q["accepted"]=true;
   if(mode==3)q["reason"]="live-readback-failed";if(mode==4)q["stage"]="sleep-inspect";if(mode==5)q["originalIdentityStable"]=false;
   if(mode==6)q["nativeAdmissionReason"]="steam-input-foreground-changed";if(mode==7)q["originalPid"]=10945;if(mode==8)q["originalCreated"]="134363901017334320";
   if(mode==9)a["game"]["identityMatches"]=false;if(mode==10)a["game"]["recoveryBindingMatches"]=false;
   if(mode==11)a["suspendCapture"]["available"]=false;if(mode==12)a["steamSessionState"]["ready"]=false;
   if(mode==13)a["gamePause"]["paused"]=true;if(mode==14)a["gamePause"]["available"]=false;
   if(mode==15)a["foreground"]["role"]="steam-ui";if(mode==16)a["foreground"]["pid"]=10945;if(mode==17)a["foreground"]["created"]="134363901017334320";
   if(mode==18)a["steamClient"]["created"]="134363898746040363";if(mode==19)a["originalGameRecovery"]["jointBindingVerified"]=false;
   if(mode==20)q["mutated"]="true";if(mode==21)q.erase("actionAcknowledged");
   assert(!SleepRecoveryAuto::delayedOriginalWindowReadback(q,b,a));}
 pass("field-bound delayed-readback predicate rejects wrong stage/type/identity/session/pause/foreground and missing native evidence");
 assert(std::none_of(facts.begin(),facts.end(),[](const auto& f){return f.first=="sleep-recovery-automatic-accepted"&&f.second.value("observationOnly",false)==false;}));
#endif
 // Round17: UI-focus cache uncertainty must not terminate exact YMCC PID postflight.
 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;foregroundPid=steamPid;begin();job();
 assert(inputCalls==1&&scopeCalls==0&&g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="confirm-wait");
 assert(SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false&&SleepRecoveryAuto::state.receipt["inputResume"]["verificationPending"]==true);
 assert(SleepRecoveryAuto::state.receipt["inputResume"]["windowReturnObserved"]==true&&SleepRecoveryAuto::state.last["foreground"]["pid"]==12932);
 assert(SleepRecoveryAuto::state.last["originalGameRecovery"]["originalInstanceMatched"]==true&&SleepRecoveryAuto::state.last["originalGameRecovery"]["steamUiInputFocusMatched"]==false);
 assert(lastInputRequest["originalTarget"]["basis"]=="current-association"&&lastInputRequest["originalTarget"]["pid"]==12932);
 assert(std::any_of(facts.begin(),facts.end(),[](const auto& e){return e.first=="sleep-recovery-automatic-observing";}));
 assert(std::none_of(facts.begin(),facts.end(),[](const auto& e){return e.first=="sleep-recovery-automatic-accepted";}));
 pass("exact original YMCC PID window survives stale Desktop UI readback; pending is NOT an input ACK and logs a fresh after");
 steamFocusedApp=2399932135U;complete(false);auto delayedUiEvidence=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(inputCalls==1&&delayedUiEvidence["phase"]=="recorded"&&delayedUiEvidence["receipt"]["inputResume"]["accepted"]==false&&delayedUiEvidence["attempts"]["input"]==1);
 assert(delayedUiEvidence["after"]["steamInputRecovery"]["contextMatched"]==true&&!delayedUiEvidence["healthVerified"].get<bool>());
 pass("late UI cache catch-up is observed without RETURN replay or rewriting the original uncertain receipt into accepted");
 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;begin();job();complete(false);
 assert(inputCalls==1&&scopeCalls==0&&SleepRecoveryAuto::get()["phase"]=="blocked"&&tick<=61000);
 assert(SleepRecoveryAuto::state.last["originalGameRecovery"]["originalWindowFocused"]==true&&SleepRecoveryAuto::state.last["steamInputRecovery"]["contextMatched"]==false);
 pass("UI cache never catches up: observe until the original 60s deadline, distinguish native window from unverified input, no success fabrication");
 reset();localStatus=11;inputContextAvailable=true;uiReadbackStale=true;foregroundPid=steamPid;begin();job();
 assert(g_sleepRecoveryAutoPending&&inputCalls==1&&scopeCalls==1);complete(false);auto s0Evidence=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(s0Evidence["phase"]=="blocked"&&s0Evidence["after"]["steam"]["overviews"][0]["localDisplayStatus"]==11&&s0Evidence["after"]["originalGameRecovery"]["originalInstanceMatched"]==true);
 assert(s0Evidence["after"]["foreground"]["pid"]==12932&&s0Evidence["before"]["foreground"]["pid"]==4000&&!s0Evidence["after"]["continueRecovery"]["candidate"].get<bool>());
 pass("23:06 S0 shape: stable original PID/window plus stale UI and status11 remains separate from genuine Continue recovery; after is not before");
 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;deferForeground=true;begin();job();
 assert(inputCalls==1&&foregroundWaitCalls==30&&g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false);complete(false);assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==1&&foregroundWaitCalls==30);
 pass("UI pending preserves only bounded reads and never bypasses native original-window confirmation");
 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;deferForeground=true;begin();
 onForegroundWait=[]{if(foregroundWaitCalls==3)foregroundPid=selected.pid;};job();assert(g_sleepRecoveryAutoPending&&inputCalls==1&&foregroundWaitCalls==3);
 pass("UI-cache miss still permits 750ms read-only original-PID foreground postflight, not a second RETURN");
 for(int mode=0;mode<5;++mode){reset();running=true;inputContextAvailable=true;uiReadbackStale=true;begin();
   onReturn=[mode]{if(mode==0)selected.processCreated++;if(mode==1)gameExists=false;if(mode==2)steamCreated++;if(mode==3)gamePaused=true;if(mode==4)foregroundPid=6000;};job();
   if(mode==4){assert(g_sleepRecoveryAutoPending);complete(false);}
   assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==1&&SleepRecoveryAuto::state.receipt["inputResume"]["verificationPending"]==false);
 }
 pass("post-RETURN game death/PID reuse, Steam restart, manual pause or user foreground change revokes pending authority");
 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;begin();job();SleepRecoveryAuto::set({{"enabled",false}});tick=13000;sleepRecoveryAutoTick();
 assert(!g_sleepRecoveryAutoPending&&jobs.empty()&&inputCalls==1);pass("explicit off cancels UI-cache observation without deferred action");
 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;begin();job();selected.processCreated++;tick=3000;sleepRecoveryAutoTick();job();
 assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==1&&SleepRecoveryAuto::state.last["game"]["identityMatches"]==false);
 pass("read-only pending observation refuses later PID-birth substitution using the same original game contract");
 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;begin();onReturn=[]{steamReadFailure="steam-read-unavailable";};job();
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.last["steamSessionState"]["ready"]==false&&SleepRecoveryAuto::state.last["foreground"]["pid"]==12932);
 pass("failed post-action read stays a fresh unavailable after, not a retained pre-action snapshot or a false PID/input success");
 reset();rejectListMutation=true;foregroundPid=steamPid;begin();job();auto failedListEvidence=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(scopeCalls==1&&failedListEvidence["phase"]=="blocked"&&failedListEvidence["before"]["steamRecovery"]["associationVerified"]==false&&failedListEvidence["after"]["steamRecovery"]["associationVerified"]==true);
 assert(failedListEvidence["receipt"]["accepted"]==false);pass("even rejected list receipts retain actual new after without converting rejected ACK into success");

 // Round18: an acknowledged Steam RETURN that did not restore the OS window
 // may use the SAME captured YMCC PID, once, with fresh positive UI guards.
 reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;writes=false;begin();job();
 assert(inputCalls==1&&scopeCalls==0&&pidActivateCalls==1&&pidProbeCalls==1&&foregroundWaitCalls==30&&g_sleepRecoveryAutoPending);
 auto directPidReceipt=SleepRecoveryAuto::state.receipt["inputResume"];
 assert(directPidReceipt["accepted"]==true&&directPidReceipt["pidWindowActivation"]["attempted"]==true&&directPidReceipt["originalWindowFocused"]==true);
 complete(false);auto directPidEvidence=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(directPidEvidence["phase"]=="recorded"&&directPidEvidence["attempts"]["input"]==1&&directPidEvidence["attempts"]["pidWindow"]==1&&!directPidEvidence["healthVerified"].get<bool>());
 assert(pidActivateCalls==1&&inputCalls==1&&directPidEvidence["after"]["originalGameRecovery"]["originalWindowFocused"]==true);
 pass("acknowledged Steam no-op gets one existing YMCC PID window handoff, no input-device gate or process thaw; stable readback never replays it");
 reset();localStatus=11;inputContextAvailable=true;uiReadbackStale=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;begin();job();
  assert(pidActivateCalls==1&&inputCalls==1&&scopeCalls==1&&g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false&&SleepRecoveryAuto::state.receipt["inputResume"]["verificationPending"]==true);
 complete(false);auto readyPidEvidence=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(readyPidEvidence["phase"]=="blocked"&&readyPidEvidence["attempts"]["pidWindow"]==1&&readyPidEvidence["after"]["steam"]["overviews"][0]["localDisplayStatus"]==11&&!readyPidEvidence["after"]["continueRecovery"]["candidate"].get<bool>());
 pass("ReadyToLaunch primary PID handoff can restore the original window without fabricating Running/Continue or an uncertain Steam input ACK");
 reset();running=true;inputContextAvailable=true;pidWindowExists=true;begin();job();assert(inputCalls==1&&pidActivateCalls==0&&pidFindCalls==0);complete(false);
 pass("original game already foreground after RETURN gets no duplicate PID activation/probe");
 for(int mode=0;mode<3;++mode){reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;
   if(mode==0)pidWindowExists=false;if(mode==1)pidWindowResponsive=false;if(mode==2)pidWindowMismatch=true;begin();job();if(g_sleepRecoveryAutoPending)complete(false);
   assert(pidActivateCalls==0&&inputCalls==1&&SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::state.receipt["inputResume"]["pidWindowActivation"]["attempted"]==false);
 }
 pass("missing/unresponsive/reused window cannot authorize a PID handoff or success, no fallback process discovery");
 for(int mode=0;mode<6;++mode){reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;begin();
   onPidProbe=[mode]{if(mode==0)selected.processCreated++;if(mode==1)gameExists=false;if(mode==2)steamCreated++;if(mode==3)gamePaused=true;if(mode==4)generation=8;if(mode==5)g_sleepRecoveryAutoEnabled=false;};job();
   assert(pidActivateCalls==0&&inputCalls==1);
 }
 pass("PID probe race: game birth/death, Steam restart, manual pause, new power generation or off all revoke the native action");
 reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;begin();onPidProbe=[]{inputInteractiveIdle=false;};job();if(g_sleepRecoveryAutoPending)complete(false);
 assert(pidActivateCalls==0&&SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::state.receipt["inputResume"]["pidWindowActivation"]["reason"]=="steam-interactive-ui-active-or-unknown");
 pass("menu opened during bounded WM_NULL probe is seen by a NEW Steam read and prevents direct native foreground activation");
 reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;begin();onPidProbe=[]{foregroundPid=GetCurrentProcessId();};job();if(g_sleepRecoveryAutoPending)complete(false);
 assert(pidActivateCalls==0&&SleepRecoveryAuto::get()["phase"]=="blocked");pass("user foreground change during native probe cannot borrow earlier idle guards");
 reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;begin();onReturn=[]{foregroundPid=GetCurrentProcessId();};job();
 assert(pidActivateCalls==0&&pidProbeCalls==0);pass("changing to another allowed YeManCC foreground AFTER Valve RETURN still cannot authorize stealing that window with the PID fallback");
 reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;begin();onPidProbe=[]{controllersObserved=false;};job();
 assert(pidActivateCalls==0&&inputCalls==1);pass("controller disappearance during PID probe denies native handoff instead of rebuilding or assuming a device");
 reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;pidApiAccepted=false;pidActivationFocusesOriginal=false;begin();job();if(g_sleepRecoveryAutoPending)complete(false);
 assert(pidActivateCalls==1&&foregroundWaitCalls==60&&SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::state.receipt["inputResume"]["pidWindowActivation"]["apiAccepted"]==false);
 tick=99999;sleepRecoveryAutoTick();assert(pidActivateCalls==1&&jobs.empty());pass("Windows foreground-lock refusal gets bounded after-checks, no Alt injection/AttachThreadInput/repeated focus or accepted gameplay claim");
 for(int mode=0;mode<3;++mode){reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;begin();
   onPidActivation=[mode]{if(mode==0)selected.processCreated++;if(mode==1)gamePaused=true;if(mode==2)steamCreated++;};job();
   assert(pidActivateCalls==1&&SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false&&SleepRecoveryAuto::state.receipt["inputResume"]["verificationPending"]==false);
 }
 pass("identity/session/pause change while Windows accepts a PID foreground request remains failed, never healthy input or retained Continue");
 reset();running=true;inputContextAvailable=true;auto guardSteam=json{{"ok",true},{"inputContext",inputContext()}};assert(pidWindowReturnBlock(guardSteam,2399932135U).empty());
 for(int mode=0;mode<14;++mode){auto denied=guardSteam;auto& c=denied["inputContext"];
   if(mode==0)c["focus"]["appid"]=-3;if(mode==1)c["focus"]["overlayAppid"]=2399932135U;if(mode==2)c["controllers"]=json::array();if(mode==3)c["controllers"][0]["index"]=0xffffffffU;
   if(mode==4)c["editor"]["dirty"]=true;if(mode==5)c["editor"]["updating"]=true;if(mode==6)c["interactive"].erase("available");if(mode==7)c["interactive"]["lockScreen"]=true;
   if(mode==8)c["interactive"]["menuOpen"]=1;if(mode==9)c["interactive"]["keyboard"]=true;
   if(mode==10)c["interactive"]="idle";if(mode==11)c["editor"]=nullptr;if(mode==12)c["focus"]=json::array();if(mode==13)c["controllers"]=json::array({nullptr});
   assert(!pidWindowReturnBlock(denied,2399932135U).empty());
 }
 pass("positive native PID guard rejects malformed/unknown UI, active overlay/editor/lock/menu/keyboard and invalid controller identity; a bare idle boolean is insufficient");

 reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;begin();onPidProbe=[]{steamOverviewGameid="14259554480910499840";};job();
 assert(pidActivateCalls==0&&inputCalls==1&&SleepRecoveryAuto::state.receipt["inputResume"]["pidWindowActivation"]["reason"]=="original-pid-return-binding-changed");
 pass("fresh native PID handoff cannot borrow another 64bit GameID even when the original process is still alive");
 reset();running=true;inputContextAvailable=true;deferForeground=true;foregroundPid=steamPid;pidWindowExists=true;begin();onPidProbe=[]{auto at=SleepSteamRecovery::localSecond(selected.processCreated);sgWriteFileAtomic((fspath::path(steamRoot)/L"logs"/L"gameprocess_log.txt").wstring(),"["+at+"] AppID 2399932135 no longer tracking PID 12932\n");};job();
 assert(pidActivateCalls==0&&inputCalls==1&&SleepRecoveryAuto::get()["phase"]=="blocked");
 pass("removing native gameprocess tracking during a PID probe revokes the Steam handoff instead of focusing an unrelated or unbound application");
 // Actual verified identity, not discovery metadata, owns Steam admission.
 for(int mode=0;mode<2;++mode){reset();running=true;inputContextAvailable=true;registrySteamPid=mode?steamPid:1736;begin();
   onSteamCall=[mode](const std::string& op){if(op=="sleep.inspect"&&steamReads==0)registrySteamPid=mode?1736:steamPid;};job();
   assert(inputCalls==1&&SleepRecoveryAuto::get()["phase"]=="confirm-wait");complete(false);assert(SleepRecoveryAuto::get()["phase"]=="recorded");
 }
 pass("registry PID/source recovery changes during initial read cannot veto a RETURN bound to the unchanged verified Steam process/birth/account/root");
 reset();registrySteamPid=1736;begin();onSteamCall=[](const std::string& op){if(op=="sleep.inspect"&&steamReads==0)registrySteamPid=steamPid;};job();
 assert(scopeCalls==1&&SleepRecoveryAuto::state.steamStatus["associationVerified"]==true);
 pass("the same provenance-only transition cannot falsely block a native list rescope either");
 reset();auto originalEvidence=SleepSteamRecovery::clientIdentity(SleepSteamRecovery::session());auto diagnosticChange=originalEvidence;
 diagnosticChange["source"]="verified-session-observer";diagnosticChange["reason"]="diagnostic-not-identity";diagnosticChange["registeredPid"]=1736;diagnosticChange["registeredPidMatchesVerified"]=false;diagnosticChange["integrityRid"]=nullptr;
 assert(SleepSteamRecovery::sameClientEvidence(originalEvidence,diagnosticChange));
 for(int mode=0;mode<9;++mode){auto changed=originalEvidence;
   if(mode==0)changed["pid"]=0;if(mode==1)changed["pid"]=4001;if(mode==2)changed["created"]="134361500000000001";if(mode==3)changed["identityToken"]="different-session";
   if(mode==4)changed["processVerified"]=false;if(mode==5)changed["accountKnown"]=false;if(mode==6)changed.erase("created");if(mode==7)changed["created"]="0";if(mode==8)changed["created"]=134361500000000000ULL;
   assert(!SleepSteamRecovery::sameClientEvidence(originalEvidence,changed));
 }
 pass("discovery/probe fields are diagnostic only; absent/unverified process/account, zero/reused PID, invalid birth or another real session still cannot pass native admission");
 auto normalizedSession=SleepSteamRecovery::session();auto spellingChange=normalizedSession;
 std::transform(spellingChange.root.begin(),spellingChange.root.end(),spellingChange.root.begin(),[](wchar_t ch){return ch==L'\\'?L'/':static_cast<wchar_t>(towlower(ch));});
 assert(SleepSteamRecovery::sameClientEvidence(SleepSteamRecovery::clientIdentity(normalizedSession),SleepSteamRecovery::clientIdentity(spellingChange)));
 spellingChange.root+=L"-other-install";assert(!SleepSteamRecovery::sameClientEvidence(SleepSteamRecovery::clientIdentity(normalizedSession),SleepSteamRecovery::clientIdentity(spellingChange)));
 pass("same verified Steam root path spelling/case is canonical identity; another install remains a different Steam session");
 auto anotherAccount=normalizedSession;anotherAccount.account++;
 assert(!SleepSteamRecovery::sameClientEvidence(SleepSteamRecovery::clientIdentity(normalizedSession),SleepSteamRecovery::clientIdentity(anotherAccount)));
 pass("same client PID/birth with a different actual account still produces a different session identity token");

 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;begin();job();auto stableIdentityReport=SleepSteamRecovery::inspect(7);SettleWindow provenanceWindow;bool provenanceComplete=false;
 for(unsigned i=1;i<=20;++i){auto sample=stableIdentityReport;sample["steamClient"]["registeredPid"]=i%2?1736:4000;sample["steamClient"]["source"]=i%2?"verified-session-observer":"verified-active-process";sample["steamClient"]["integrityRid"]=i%2?json():json(12288);provenanceComplete=provenanceWindow.observe(sample,i*1500);}
 assert(provenanceComplete&&provenanceWindow.stableSince==1500);pass("alternating discovery PID/source/probe metadata cannot perpetually reset the 9s stable window of the same verified Steam identity");
 for(const auto key:{"pid","created","identityToken"}){auto changed=stableIdentityReport;if(std::string(key)=="pid")changed["steamClient"][key]=4001;else changed["steamClient"][key]="134361500000000001";
   SettleWindow genuineChange;assert(!genuineChange.observe(stableIdentityReport,1500));assert(!genuineChange.observe(stableIdentityReport,15000));assert(genuineChange.observe(stableIdentityReport,30000));assert(!genuineChange.observe(changed,31500)&&genuineChange.stableSince==31500);}
 pass("actual Steam PID/birth/session-token changes still reset the stable identity observation; ignoring discovery metadata does not erase session changes");

 // Input-context recovery is a distinct production path, independent of list repair.
 auto captureOriginal=[](){SleepRecoveryAuto::set({{"enabled",true}});g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);assert(jobs.size()==1);job();g_powerLifecycle=PowerLifecycle::Ready;};
 reset();sleepRecoveryAutoCaptureSuspend(7);assert(SleepSteamRecovery::captured.generation==0&&jobs.empty());pass("disabled preference never captures or queues pre-sleep Steam work");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;captureOriginal();
 unsigned transientInspectCalls=0;onSteamCall=[&](const std::string& op){if(op=="sleep.inspect"){++transientInspectCalls;steamAccount=transientInspectCalls==1?99:42;}};
 auto transientSession=SleepSteamRecovery::inspect(7);
 assert(transientSession["steamSessionRetry"]["attempted"]==true&&transientSession["steamSessionRetry"]["attempts"]==2&&transientSession["steamSessionRetry"]["recovered"]==true);
 assert(transientSession["steamSessionState"]["status"]=="current"&&transientSession["steamRecovery"]["status"]=="current");
 pass("transient Steam ActiveUser/session read race gets two bounded read-only retries and recovers without mutating or changing the captured account");
 reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;captureOriginal();
 onSteamCall=[](const std::string& op){if(op=="sleep.inspect")steamAccount=99;};
 auto persistentSession=SleepSteamRecovery::inspect(7);
 assert(persistentSession["steamSessionRetry"]["attempted"]==true&&persistentSession["steamSessionRetry"]["attempts"]==1&&persistentSession["steamSessionRetry"]["recovered"]==false);
 assert(persistentSession["steamSessionState"]["status"]=="changed"&&!persistentSession["steamRecovery"]["repairable"].get<bool>());
 pass("persistent Steam account change remains fail-closed after bounded retries, with explicit retry/current-state evidence and no repair action");
reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;captureOriginal();
 assert(SleepSteamRecovery::captured.game.pid==12932&&SleepSteamRecovery::captured.inputBefore["steam"]["inputContext"]["focus"]["appid"]==2399932135U);
 assert(!SleepSteamRecovery::captured.inputBefore["physicalPreSleepVerified"].get<bool>());sleepRecoveryAutoCaptureSuspend(7);assert(jobs.empty());pass("sleep boundary captures original instance once plus paired read, without claiming physical pre-sleep proof");
 reset();running=true;inputContextAvailable=true;captureOriginal();steamFocusedApp=413080;begin();job();assert(inputCalls==1&&scopeCalls==0&&SleepRecoveryAuto::get()["phase"]=="confirm-wait");tick=3000;sleepRecoveryAutoTick();job();
 complete();auto inputSaved=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(inputSaved["schema"]==4&&inputSaved["after"]["steamInputRecovery"]["contextMatched"]==true&&!inputSaved["healthVerified"].get<bool>()&&inputSaved["after"]["steamInputRecovery"]["gameInputVerified"]==false);
 pass("automatic wake returns to exact original Steam input context even when running list was already current; actual input remains unverified");
 reset();running=true;inputContextAvailable=true;captureOriginal();running=false;begin();job();assert(inputCalls==1&&scopeCalls==1);tick=3000;sleepRecoveryAutoTick();job();assert(SleepRecoveryAuto::get()["steamInputRecovery"]["contextMatched"]==true);pass("UI rescope and original input return have separate bounded actions and one verification");
 reset();running=true;inputContextAvailable=true;begin(false);job();assert(inputCalls==0&&SleepRecoveryAuto::get()["steamInputRecovery"]["reason"]=="steam-original-target-not-captured-at-suspend");pass("no sleep-bound original identity means no speculative focus override");
 reset();running=true;inputContextAvailable=true;captureOriginal();foregroundPid=6000;auto userReport=SleepSteamRecovery::inspect(7);assert(!userReport["steamInputRecovery"]["repairable"].get<bool>());assert(userReport["foreground"]["role"]=="unknown"&&inputCalls==0);pass("foreground owned by another application is never stolen by automatic Steam return");
 reset();running=true;inputContextAvailable=true;captureOriginal();auto staleInput=SleepSteamRecovery::inspect(7);onSteamCall=[](const std::string& op){if(op=="sleep.input-resume")foregroundPid=6000;};auto deniedInput=SleepSteamRecovery::resumeInputContext(staleInput,7,g_sleepRecoveryAutoConfigEpoch.load());assert(!deniedInput["accepted"].get<bool>()&&inputCalls==0);pass("native admission rechecks foreground just before CDP and refuses intervening user focus");
 reset();running=true;inputContextAvailable=true;captureOriginal();auto pausedInput=SleepSteamRecovery::inspect(7);gamePaused=true;auto pausedReceipt=SleepSteamRecovery::resumeInputContext(pausedInput,7,g_sleepRecoveryAutoConfigEpoch.load());assert(!pausedReceipt["accepted"].get<bool>()&&inputCalls==0);gamePaused=false;sgWriteFileAtomic(SG_SLEEP_LEASE_DIR+L"\\12932.txt","owned lease");pausedReceipt=SleepSteamRecovery::resumeInputContext(pausedInput,7,g_sleepRecoveryAutoConfigEpoch.load());assert(!pausedReceipt["accepted"].get<bool>()&&inputCalls==0);pass("manual or sleep-pause ownership blocks input refocus without releasing another owner");
 reset();running=true;inputContextAvailable=true;captureOriginal();selected.pid=13000;auto replaced=SleepSteamRecovery::inspect(7);assert(replaced["game"]["pid"]==12932&&!replaced["game"]["identityMatches"].get<bool>()&&!replaced["steamInputRecovery"]["repairable"].get<bool>());pass("a new detected game after wake cannot replace the captured original Steam target");
 reset();running=true;inputContextAvailable=true;captureOriginal();steamPid++;auto newClient=SleepSteamRecovery::inspect(7);assert(newClient["suspendCapture"]["available"].get<bool>()&&!newClient["suspendCapture"]["clientMatched"].get<bool>()&&newClient["game"]["identityMatches"].get<bool>()&&newClient["steamSessionState"]["status"]=="changed"&&!newClient["steamInputRecovery"]["repairable"].get<bool>());pass("changed Steam client/session invalidates captured input-return authority");
 reset();running=true;inputContextAvailable=true;captureOriginal();auto liveInput=SleepSteamRecovery::inspect(7);onSteamCall=[](const std::string& op){if(op=="sleep.input-resume")gameExists=false;};auto deathInput=SleepSteamRecovery::resumeInputContext(liveInput,7,g_sleepRecoveryAutoConfigEpoch.load());assert(!deathInput["accepted"].get<bool>()&&inputCalls==0);pass("original game death during input admission never focuses or launches a substitute");
 reset();running=true;inputContextAvailable=true;captureOriginal();begin();job();assert(inputCalls==1);steamFocusedApp=413080;tick=3000;sleepRecoveryAutoTick();job();complete(false);assert(inputCalls==1&&SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::get()["reason"]=="steam-input-context-not-retained");pass("lost context after native return is a terminal failure, not a current-library success or a refocus loop");
 reset();running=true;inputContextAvailable=true;captureOriginal();controllersObserved=false;auto noDevice=SleepSteamRecovery::inspect(7);assert(!noDevice["steamInputRecovery"]["repairable"].get<bool>()&&!noDevice["steamInputRecovery"]["gameInputVerified"].get<bool>());pass("Steam cannot observe a controller: input remains unknown, no virtual-device recovery guessed");
  reset();running=true;inputContextAvailable=true;captureOriginal();localStatus=11;steamOverviewGameid="wrong";auto noBackend=SleepSteamRecovery::inspect(7);assert(noBackend["steamRecovery"]["status"]=="blocked"&&noBackend["steamInputRecovery"]["repairable"].get<bool>()&&noBackend["continueRecovery"]["candidate"]==false&&inputCalls==0);pass("ReadyToLaunch may authorize primary original-tracker RETURN without exact contact proof, but never list repair or a false Continue label");

 reset();begin(false);job();assert(scopeCalls==0&&inputCalls==0&&SleepRecoveryAuto::get()["reason"]=="steam-original-target-not-captured-at-suspend");pass("automatic list repair also refuses an unbound post-wake replacement target");
 reset();begin();onSteamCall=[](const std::string& op){if(op=="sleep.inspect"&&steamReads==1){tick=50001;sleepRecoveryAutoTick();}};job();assert(scopeCalls==0&&SleepRecoveryAuto::get()["reason"]=="automatic-worker-timeout");pass("stage timeout during fresh pre-write read revokes UI-rescope admission");
 reset();running=true;inputContextAvailable=true;begin();onSteamCall=[](const std::string& op){if(op=="sleep.inspect"&&steamReads==1){tick=50001;sleepRecoveryAutoTick();}};job();assert(inputCalls==0&&SleepRecoveryAuto::get()["reason"]=="automatic-worker-timeout");pass("stage timeout during fresh input read revokes native return admission");
 reset();auto apiProbe=SleepSteamRecovery::inspect(7)["gameApiProbe"];assert(apiProbe["available"].get<bool>()&&apiProbe["steamOverlayModulePresent"].get<bool>()&&apiProbe["overlayFromCurrentSteamRoot"].get<bool>()&&apiProbe["inputLibrariesObserved"][0]=="xinput1_3.dll"&&!apiProbe["hookHealthVerified"].get<bool>()&&!apiProbe["gameInputVerified"].get<bool>());pass("bound process probe observes Steam renderer/input DLLs without treating their presence as healthy input");
 reset();moduleQueryFails=true;apiProbe=SleepSteamRecovery::inspect(7)["gameApiProbe"];assert(!apiProbe["available"].get<bool>()&&apiProbe["steamOverlayModulePresent"].is_null());pass("module query denied/incomplete leaves overlay health unknown, never absent or recovered");
 reset();probeModules={L"dinput8.dll",L"private-custom.dll"};apiProbe=SleepSteamRecovery::inspect(7)["gameApiProbe"];assert(apiProbe["available"].get<bool>()&&!apiProbe["steamOverlayModulePresent"].get<bool>()&&apiProbe["inputLibrariesObserved"].size()==1);assert(apiProbe.dump().find("private-custom")==std::string::npos&&apiProbe.dump().find(W2U(steamRoot))==std::string::npos);pass("game probe exports only an input-API allowlist, no arbitrary module paths or source-device data");
 reset();probeModules.assign(513,L"xinput1_3.dll");probeModules.back()=L"gameoverlayrenderer64.dll";apiProbe=SleepSteamRecovery::inspect(7)["gameApiProbe"];assert(apiProbe["available"].get<bool>()&&apiProbe["moduleTruncated"].get<bool>()&&apiProbe["moduleCount"]==512&&apiProbe["steamOverlayModulePresent"].is_null());pass("bounded module truncation cannot falsely declare the Steam renderer absent");
 reset();running=true;inputContextAvailable=true;begin();owner={12932,selected.processCreated,true};job();assert(SleepRecoveryAuto::get()["phase"]=="game-wait"&&inputCalls==0&&scopeCalls==0);tick=7000;sleepRecoveryAutoTick();assert(jobs.empty());owner={};tick=10032;sleepRecoveryAutoTick();assert(jobs.size()==1);job();assert(inputCalls==1);tick=11532;sleepRecoveryAutoTick();job();assert(SleepRecoveryAuto::get()["steamInputRecovery"]["contextMatched"]==true);pass("provided 10:36 ordering: power Ready precedes sleep lease release; Steam waits then returns once after original lease is released");
 reset();running=true;inputContextAvailable=true;begin();sgWriteFileAtomic(SG_SLEEP_LEASE_DIR+L"\\12932.txt","owned sleep");job();assert(SleepRecoveryAuto::get()["phase"]=="game-wait");tick=30000;sleepRecoveryAutoTick();assert(SleepRecoveryAuto::get()["reason"]=="steam-original-game-sleep-release-timeout"&&inputCalls==0&&fspath::exists(SG_SLEEP_LEASE_DIR+L"\\12932.txt"));pass("original sleep ownership wait is bounded and never removes or resumes the sleep lease");
 reset();running=true;inputContextAvailable=true;begin();owner={12932,selected.processCreated,true};job();gamePaused=true;tick=2500;sleepRecoveryAutoTick();assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==0&&gamePaused&&owner.markerOwned);pass("manual pause inserted during original sleep wait vetoes Steam return and remains owned by the user");
 reset();running=true;inputContextAvailable=true;begin();gamePaused=true;job();assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==0&&scopeCalls==0&&gamePaused);pass("pre-existing manual pause is not converted into an automatic Steam wake repair");
 reset();running=true;inputContextAvailable=true;begin();owner={12932,selected.processCreated,true};job();SleepRecoveryAuto::set({{"enabled",false}});owner={};tick=10032;sleepRecoveryAutoTick();assert(jobs.empty()&&inputCalls==0&&!g_sleepRecoveryAutoPending);pass("disable while waiting for original game release cancels the pending Steam action");
 reset();running=true;inputContextAvailable=true;begin();raiseFocusesOriginal=false;job();assert(inputCalls==1&&g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.receipt["inputResume"]["reason"]=="steam-original-window-not-confirmed");complete(false);assert(inputCalls==1&&SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false);pass("same AppID acknowledgement cannot substitute for foreground confirmation of the original OS PID/creation instance");
 reset();running=true;inputContextAvailable=true;writes=false;begin();job();assert(inputCalls==1&&SleepRecoveryAuto::get()["phase"]=="confirm-wait");pass("Steam-only wake does not wait for unrelated hardware write/fan readiness gates");
 reset();sgWriteFileAtomic(steamRoot+L"\\logs\\controller_ui.txt","[2026-10-11 10:36:09] game focus\n");sgWriteFileAtomic(steamRoot+L"\\logs\\gameoverlay_renderer.txt","overlay trace\n");auto exportedSteam=SleepSteamRecovery::exportLogTails();assert(exportedSteam.size()==4&&inputCalls==0&&scopeCalls==0&&steamReads==0);auto exportManifest=json::parse(readFile(SG_DIR+L"\\steam-input-log-tails\\steam-input-export-manifest.json"));assert(exportManifest["files"].size()==7&&!exportManifest["gameInputVerified"].get<bool>()&&!exportManifest["automaticUpload"].get<bool>());assert(exportManifest.dump().find("localconfig")==std::string::npos);pass("existing exportAll gets bounded Steam focus/overlay/tracker evidence, missing-file metadata and no credentials or repair");
 reset();sgWriteFileAtomic(steamRoot+L"\\logs\\controller_ui.txt",std::string(3*1024*1024,'x'));exportedSteam=SleepSteamRecovery::exportLogTails();exportManifest=json::parse(readFile(SG_DIR+L"\\steam-input-log-tails\\steam-input-export-manifest.json"));assert(exportManifest["files"][2]["truncated"]==true);assert(readFile(SG_DIR+L"\\steam-input-log-tails\\steam-controller-ui.log").size()<=2*1024*1024);pass("Steam export files stay byte bounded, preserving truncation evidence instead of copying unlimited logs");
 reset();running=true;inputContextAvailable=true;begin();job();assert(inputCalls==1);gamePaused=true;tick=3000;sleepRecoveryAutoTick();job();assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==1&&gamePaused);pass("delayed input verification cannot claim context retained while a new manual pause owns the original game");
 reset();running=true;inputContextAvailable=true;begin();job();assert(inputCalls==1);foregroundPid=6000;tick=3000;sleepRecoveryAutoTick();job();
  assert(g_sleepRecoveryAutoPending&&inputCalls==1&&SleepRecoveryAuto::state.postReturnForegroundWait);complete(false);
  assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==1&&scopeCalls==0&&pidActivateCalls==0);
  pass("stale Steam focus cannot hide original foreground loss: bounded read-only wait, no focus theft or false readiness");
 std::vector<std::string> realLines;std::istringstream realSteam(readFile(argv[2]));std::string realLine;
 while(std::getline(realSteam,realLine))realLines.push_back(realLine);
 const auto add=std::find_if(realLines.begin(),realLines.end(),[](const auto& line){return line.find("adding PID 12932")!=std::string::npos;});assert(add!=realLines.end());
 const auto processMarker=add->find(" as a tracked process ");assert(processMarker!=std::string::npos);
 const auto actual=track(realLines,12932,commandExe(add->substr(processMarker+22)),"2026-10-11 10:34:55","2026-10-11 10:35:00");
 assert(actual.status=="tracked"&&actual.appid==2399932135U);pass("replay provided Steam log: exact TMNT PID/shortcut binding persists through 10:36 wake");
 const auto unavailable=verdict({{"ok",true},{"runningAppIDs",json::array()},{"overviews",json::array()}},actual,true);
 assert(unavailable["status"]=="unknown"&&!unavailable["repairable"]&&!unavailable["associationVerified"]);
 pass("provided empty overview snapshot cannot be promoted to Steam exited or recovery success");

 const auto realMsi=json::parse(readFile(argv[3]));Binding msiBinding;msiBinding.status="tracked";msiBinding.appid=3320061248U;
 auto msiAssociation=verdict(realMsi["steam"],msiBinding,true);
 auto msiInput=inputVerdict(realMsi["steam"],msiBinding,msiAssociation,true,true,true,true);
 assert(msiAssociation["associationVerified"].get<bool>()&&msiInput["status"]=="waiting-controller"&&!msiInput["contextMatched"].get<bool>());
 assert(continueVerdict(realMsi["steam"],msiAssociation)["candidate"].get<bool>());
 auto replayMsi=realMsi;replayMsi["steamInputRecovery"]=msiInput;replayMsi["continueRecovery"]=continueVerdict(realMsi["steam"],msiAssociation);
 SettleWindow msiWindow;assert(!msiWindow.observe(replayMsi,1500)&&msiWindow.reason=="steam-controller-reconnect-not-observed");
 pass("replay actual MSI 2077 snapshot: local/selected Running and cached focus with zero controllers never completes input recovery");


 const auto latestMsi=json::parse(readFile(argv[4]));const auto actualS4=latestMsi["s4"]["details"],actualS0=latestMsi["s0"]["details"];
 const auto trackedPid=static_cast<DWORD>(number(latestMsi["game"],"pid"));
 const auto trackerText=latestMsi.value("trackerLine",std::string());const auto trackerMarker=trackerText.find(" as a tracked process ");assert(trackerMarker!=std::string::npos);
 const auto latestBinding=track({trackerText},trackedPid,commandExe(trackerText.substr(trackerMarker+22)),"2026-10-11 23:00:46","2026-10-11 23:00:51");
 assert(latestBinding.status=="tracked"&&latestBinding.appid==3320061248U&&trackedPid==4284);
 json s4Steam={{"ok",true},{"overviews",json::array({actualS4["overview"]})},{"inputContext",{{"focus",actualS4["inputFocus"]},{"controllerListValid",true},{"controllers",json::array({{{"index",0}}})}}}};
 const auto s4Association=verdict(s4Steam,latestBinding,true),s4Input=inputVerdict(s4Steam,latestBinding,s4Association,true,false,true,true);
 assert(s4Association["associationVerified"]==true&&continueVerdict(s4Steam,s4Association)["candidate"]==true&&s4Input["contextMatched"]==false&&s4Input["repairable"]==false);
 pass("replay 23:03 actual S4: same original2077 PID and true Running4/4 retained, but absent capture prevents claiming automatic repair success");
 json s0Steam={{"ok",true},{"overviews",json::array({actualS0["overview"]})},{"inputContext",{{"focus",actualS0["inputFocus"]},{"controllerListValid",true},{"controllers",json::array({{{"index",0}}})}}}};
 const auto s0Association=verdict(s0Steam,latestBinding,true),s0Input=inputVerdict(s0Steam,latestBinding,s0Association,true,true,true,false);
 assert(s0Association["associationVerified"]==false&&s0Association["reason"]=="steam-backend-not-running"&&s0Input["repairable"]==false&&s0Input["returnBasis"]=="captured-original-tracker");
 assert(s0Input["status"]=="waiting-interaction"&&s0Input["reason"]=="steam-input-editor-busy-or-unknown");
  const auto contactGameId=(static_cast<std::uint64_t>(latestBinding.appid)<<32)|0x02000000ULL;
  json contactOverview={{"appid",latestBinding.appid},{"overviewAvailable",true},{"gameidString",std::to_string(contactGameId)},
      {"appType",0x40000000U},{"localDisplayStatus",11},{"localRunning",false},{"inRunningList",false}};
  const auto contactAssociation=verdict({{"ok",true},{"overviews",json::array({contactOverview})}},latestBinding,true,true);
  assert(contactAssociation["status"]=="contact-degraded"&&contactAssociation["reason"]=="steam-running-consumer-missing"&&
      contactAssociation["contactConnected"]==true&&contactAssociation["contactRepairable"]==true&&
      !contactAssociation["associationVerified"].get<bool>());
  const auto unboundContact=verdict({{"ok",true},{"overviews",json::array({contactOverview})}},latestBinding,true,false);
  assert(unboundContact["reason"]=="steam-backend-not-running"&&!unboundContact["contactRepairable"].get<bool>());
  pass("R47 contact proof distinguishes the same tracked original from ordinary backend loss before any Scope call");
  reset();json contactSteam={{"ok",true},{"overviews",json::array({contactOverview})},{"inputContext",inputContext()}};
  const auto contactInput=inputVerdict(contactSteam,latestBinding,contactAssociation,true,true,true,false);
  assert(contactInput["repairable"]==true&&contactInput["returnBasis"]=="captured-original-tracker"&&contactInput["associationVerified"]==false);
  assert(inputVerdict(contactSteam,latestBinding,contactAssociation,true,false,true,false)["repairable"]==false);
  for(const char* proof:{"contactConnected","contactRepairable"}){auto missingProof=contactAssociation;missingProof[proof]=false;
    assert(inputVerdict(contactSteam,latestBinding,missingProof,true,true,true,false)["repairable"]==false);}
  auto wrongContactSteam=contactSteam;wrongContactSteam["overviews"][0]["gameidString"]="wrong";
  assert(inputVerdict(wrongContactSteam,latestBinding,contactAssociation,true,true,true,false)["repairable"]==false);
  pass("R47 contact-degraded retains captured original RETURN independently of list readback, while missing capture/proof or wrong GameID cannot borrow it");

 // The historical row did not capture positive editor/UI guards. Supply them
 // ONLY in an explicitly synthetic policy probe; never promote the raw log.
 auto supplementedS0=s0Steam;supplementedS0["inputContext"]["editor"]=inputContext()["editor"];supplementedS0["inputContext"]["interactive"]=inputContext()["interactive"];
 const auto guardedS0=inputVerdict(supplementedS0,latestBinding,s0Association,true,true,true,false);
 assert(guardedS0["repairable"]==true&&guardedS0["returnBasis"]=="captured-original-tracker"&&continueVerdict(supplementedS0,s0Association)["candidate"]==false);
 pass("historical incomplete UI guards wait; an explicitly synthetic idle supplement admits the same original tracker basis without faking Running");
 assert(continueVerdict(s0Steam,s0Association)["candidate"]==false&&latestMsi["s0OldReceipt"]["originalWindowFocused"]==true&&latestMsi["s0OldReceipt"]["accepted"]==false);
 assert(latestMsi.value("s0NativeInputFocusLine",std::string()).find("AppID 3320061248")!=std::string::npos&&actualS0["inputFocus"]["appid"]==413080);
 pass("replay 23:06 actual S0: same PID tracker, ReadyToLaunch11/11 and native original-game focus disagree with UI Desktop; no fabricated Continue/input success");
 assert(ymcc::sleepprefs::kAutoWakeRecoveryDefaultEnabled&&ymcc::sleepprefs::autoWakePreference(json::object()));
 assert(ymcc::sleepprefs::autoWakePreference({{"steamOverlayOffFix",true}}));
 pass("fresh/missing auto preference defaults enabled independently of obsolete overlay intent");
 assert(!ymcc::sleepprefs::autoWakePreference({{"autoWakeRecovery",false}})&&ymcc::sleepprefs::autoWakePreference({{"autoWakeRecovery",true}}));
 pass("explicit saved off remains off; default-on does not overwrite the user's choice");
 for(const auto& malformed:{json(nullptr),json("true"),json(1)})assert(!ymcc::sleepprefs::autoWakePreference({{"autoWakeRecovery",malformed}}));
 pass("malformed saved auto preference cannot silently authorize a Steam write");
 const auto retired=ymcc::sleepprefs::retiredOverlayReply();assert(ymcc::sleepprefs::kOverlayOffFixDeprecated&&!ymcc::sleepprefs::kOverlayOffFixDefaultEnabled&&retired["deprecated"]==true&&retired["accepted"]==false&&retired["enabled"]==false&&retired["mutated"]==false&&retired["userReachable"]==false);
 pass("abandoned overlay strategy stays disabled and rejects compatibility activation without Steam mutation");

 // Replay 01:49 package: entering S0 produces PBT 18/7 before the final
 // Reason=1 exit. These are candidates, not authority or budget consumption.
 auto s0Capture=[](){modernStandby=true;SleepRecoveryAuto::set({{"enabled",true}});g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoPrepareSuspend();sleepRecoveryAutoCaptureSuspend(7);if(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;steamReads=0;};
 reset();running=true;inputContextAvailable=true;s0Capture();sleepRecoveryAutoArm(7);tick=2000;sleepRecoveryAutoTick();
 assert(!g_sleepRecoveryAutoPending&&jobs.empty()&&inputCalls==0&&scopeCalls==0&&steamReads==0&&SleepRecoveryAuto::state.seenGeneration==0);
 assert(SleepRecoveryAuto::get()["phase"]=="wake-proof-wait"&&SleepRecoveryAuto::state.armed==0);
 auto candidateSnapshot=json::parse(readFile(SleepRecoveryAuto::exportCurrentSnapshot()));
 assert(candidateSnapshot["reportState"]=="waiting-confirmed-wake"&&candidateSnapshot["attempts"]["input"]==0&&!candidateSnapshot["repairTriggered"].get<bool>());
 sleepRecoveryAutoArm(7);assert(facts.size()>0&&SleepRecoveryAuto::state.wakeCandidateAt==0);
 pass("S0 intermediate RESUMESUSPEND queues no CDP work and consumes neither RETURN, generation nor 60s budget; export is honestly waiting for proof");
 tick=23000;sleepRecoveryAutoRecordKernelWake(7,507,16777220,-1,sgNowFileTime());sleepRecoveryAutoArm(7);
 assert(!g_sleepRecoveryAutoPending&&inputCalls==0&&SleepRecoveryAuto::state.seenGeneration==0);
 sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());assert(!g_sleepRecoveryAutoPending&&jobs.empty());
 sleepRecoveryAutoArm(7);assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.armed==23000&&SleepRecoveryAuto::state.wakeAdmission["candidateWaitMs"]==23000);
 controllersObserved=false;foregroundPid=steamPid;tick=24500;sleepRecoveryAutoTick();job();assert(inputCalls==0&&scopeCalls==0&&g_sleepRecoveryAutoPending);
 controllersObserved=true;tick=26000;sleepRecoveryAutoTick();job();assert(inputCalls==1&&SleepRecoveryAuto::get()["phase"]=="confirm-wait");
 auto confirmedArmed=SleepRecoveryAuto::state.armed;sleepRecoveryAutoArm(7);assert(inputCalls==1&&SleepRecoveryAuto::state.armed==confirmedArmed);
 complete(false);auto finalWakeReport=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(finalWakeReport["phase"]=="recorded"&&finalWakeReport["wakeAdmission"]["source"]=="kernel-power-507-reason-1"&&finalWakeReport["settling"]["lastElapsedMs"]>=30000&&!finalWakeReport["healthVerified"].get<bool>());
 pass("01:48 replay: automatic exit does not arm; true user exit starts a fresh budget, empty controller list waits, then exactly one original-PID RETURN follows reconnect");
 for(int mode=0;mode<4;++mode){reset();running=true;inputContextAvailable=true;s0Capture();tick=1000;
   auto boundary=SleepSteamRecovery::captured.powerBoundaryFileTime;auto event=mode==0?0:mode==1?boundary-1:mode==2?boundary:sgNowFileTime()+10000;
   sleepRecoveryAutoRecordKernelWake(7,507,1,-1,event);sleepRecoveryAutoArm(7);assert(!g_sleepRecoveryAutoPending&&jobs.empty()&&inputCalls==0&&scopeCalls==0);}
 pass("missing, pre-boundary, equal-boundary and future Kernel wake timestamps cannot authorize Steam recovery");
 reset();running=true;inputContextAvailable=true;s0Capture();tick=2000;sleepRecoveryAutoRecordKernelWake(7,107,0,4,sgNowFileTime());sleepRecoveryAutoArm(7);assert(!g_sleepRecoveryAutoPending);
 sleepRecoveryAutoRecordKernelWake(7,107,0,5,sgNowFileTime());sleepRecoveryAutoArm(7);assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.wakeAdmission["source"]=="kernel-power-107-target-5");
 tick=3500;sleepRecoveryAutoTick();job();assert(inputCalls==1);pass("hibernate on an S0-capable machine accepts correlated Kernel107/Target5 without requiring a nonexistent S0 exit event");
 reset();running=true;inputContextAvailable=true;s0Capture();tick=2000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());
 g_powerLifecycle=PowerLifecycle::Suspended;sleepRecoveryAutoArm(7);tick=3500;sleepRecoveryAutoTick();assert(g_sleepRecoveryAutoPending&&jobs.empty()&&inputCalls==0);
 g_powerLifecycle=PowerLifecycle::Ready;tick=4000;sleepRecoveryAutoTick();job();assert(inputCalls==1);pass("a correlated final wake witness may arrive before shared Ready; only the Steam first-read waits, without modifying shared power or game leases");
 reset();running=true;inputContextAvailable=true;s0Capture();tick=2000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());generation=8;sleepRecoveryAutoArm(7);assert(!g_sleepRecoveryAutoPending&&inputCalls==0);
 g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(8);if(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoArm(8);assert(!g_sleepRecoveryAutoPending);
 pass("wake proof from a preceding generation cannot admit a newly captured sleep transaction");
 reset();running=true;inputContextAvailable=true;s0Capture();tick=2000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());SleepRecoveryAuto::set({{"enabled",false}});sleepRecoveryAutoArm(7);
 assert(!g_sleepRecoveryAutoPending&&inputCalls==0);SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoArm(7);assert(!g_sleepRecoveryAutoPending&&jobs.empty());
 pass("disable/re-enable invalidates old capture and Kernel proof epochs; toggling alone never launches a repair");
 reset();running=true;inputContextAvailable=true;begin();assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.wakeAdmission["source"]=="pbt-resume-suspend-s3");job();assert(inputCalls==1);
 pass("S3 machines retain their explicit RESUMESUSPEND path and do not wait for S0-only Kernel506/507 events");
 reset();running=true;inputContextAvailable=true;begin();assert(g_sleepRecoveryAutoPending&&jobs.size()==1);
 modernStandby=true;generation=8;tick=2000;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(8);
 while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoArm(8);
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="wake-proof-wait"&&SleepRecoveryAuto::state.seenGeneration==7&&SleepRecoveryAuto::state.armed==0&&inputCalls==0);
 tick=3000;sleepRecoveryAutoTick();assert(jobs.empty());
 sleepRecoveryAutoRecordKernelWake(8,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(8);assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.armed==3000&&SleepRecoveryAuto::state.seenGeneration==8);
 tick=4500;sleepRecoveryAutoTick();job();assert(inputCalls==1);pass("a new S0 candidate cancels the previous generation pending worker instead of inheriting its active flag or budget");
 reset();running=true;inputContextAvailable=true;s0Capture();tick=90000;sleepRecoveryAutoTick();assert(!g_sleepRecoveryAutoPending);
 sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(7);assert(SleepRecoveryAuto::state.armed==90000&&SleepRecoveryAuto::state.settling.samples==0);
 tick=91500;sleepRecoveryAutoTick();job();assert(inputCalls==1&&g_sleepRecoveryAutoPending);complete(false);
 auto longSleepReport=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(longSleepReport["phase"]=="recorded"&&longSleepReport["settling"]["lastElapsedMs"]>=30000&&longSleepReport["settling"]["lastElapsedMs"]<60000);
 pass("sleep longer than 60s does not exhaust observation: the bounded budget begins at admitted final wake, not intermediate entry broadcasts");


 // Protected windows delay admission, not the original PID identity or final
 // wake budget. Closing them may permit ONE RETURN, never an override.
  reset();running=true;inputContextAvailable=true;inputInteractiveIdle=false;begin();job();
 assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="settle-wait"&&inputCalls==0&&scopeCalls==0&&!SleepRecoveryAuto::state.inputAttempted);
 assert(SleepRecoveryAuto::get()["steamInputRecovery"]["status"]=="waiting-interaction");
 tick=18000;sleepRecoveryAutoTick();job();assert(inputCalls==0&&g_sleepRecoveryAutoPending);
 inputInteractiveIdle=true;tick=21000;sleepRecoveryAutoTick();job();assert(inputCalls==1&&scopeCalls==0);complete(false);
 auto interactiveReport=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(interactiveReport["phase"]=="recorded"&&interactiveReport["attempts"]["input"]==1&&!interactiveReport["healthVerified"].get<bool>());
 pass("a transient protected Steam UI waits without consuming RETURN; closing it permits one original-PID action within the same bounded wake");
 reset();running=true;inputContextAvailable=true;inputInteractiveIdle=false;begin();job();complete(false);
 auto protectedReport=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(protectedReport["phase"]=="blocked"&&protectedReport["reason"]=="steam-interactive-ui-active-or-unknown"&&inputCalls==0&&scopeCalls==0);
 assert(protectedReport["attempts"]["input"]==0&&protectedReport["observations"][0]["interactive"]["menuOpen"]==1&&!protectedReport["healthVerified"].get<bool>());
 pass("a real Steam menu remaining open for the full wake budget is untouched and reported, not silently dismissed or repeatedly RETURNed");
 for(int mode=0;mode<8;++mode){reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;foregroundPid=selected.pid;
   contextOverride=[mode](json& c){if(mode==0)c.erase("interactive");if(mode==1)c["interactive"]["available"]=false;if(mode==2)c["interactive"]["keyboard"]=true;
     if(mode==3)c["interactive"]["overlayOrVrWindow"]=true;if(mode==4)c["editor"]["dirty"]=true;if(mode==5)c["editor"]["previewActive"]=true;
     if(mode==6){c["editor"]["editingActive"]=true;c["editor"]["editingAppid"]=123;}if(mode==7)c["interactive"]["lockScreen"]=true;};
   begin();job();assert(inputCalls==0&&scopeCalls==0&&!SleepRecoveryAuto::state.inputAttempted&&g_sleepRecoveryAutoPending);
   assert(!SleepRecoveryAuto::get()["steamInputRecovery"]["contextMatched"].get<bool>());
   auto blockedObservation=SleepSteamRecovery::inspect(7);ymcc::sleepsteam::SettleWindow protectedStable;
   assert(!protectedStable.observe(blockedObservation,1500)&&!protectedStable.observe(blockedObservation,15000)&&!protectedStable.observe(blockedObservation,30000));
 }
 pass("unknown guards, keyboard/Overlay/lock, dirty/preview/foreign editor cannot satisfy readiness even when cached AppID and original OS window both match");
 reset();running=true;inputContextAvailable=true;contextOverride=[](json& c){c["editor"]["editingActive"]=true;c["editor"]["editingAppid"]=2399932135U;};begin();job();assert(inputCalls==1);complete(false);
 pass("a clean cached loaded configuration for the original AppID remains allowed; waiting does not confuse it with a visible controller configurator");
 for(int mode=0;mode<4;++mode){reset();running=true;inputContextAvailable=true;inputInteractiveIdle=false;begin();job();
   if(mode==0)selected.processCreated++;if(mode==1)steamAccount++;if(mode==2)generation++;if(mode==3)SleepRecoveryAuto::set({{"enabled",false}});
   inputInteractiveIdle=true;tick=21000;sleepRecoveryAutoTick();if(!jobs.empty())job();assert(inputCalls==0&&scopeCalls==0&&!g_sleepRecoveryAutoPending);
 }
 pass("waiting for protected UI does not renew authority after PID birth/account/generation change or saved switch off");
 reset();running=true;inputContextAvailable=true;inputInteractiveIdle=false;begin();job();
 assert(SleepRecoveryAuto::state.associationHistory["firstCurrentAssociationElapsedMs"]==1500);
 localStatus=11;running=false;tick=15000;sleepRecoveryAutoTick();job();assert(inputCalls==0&&scopeCalls==1&&g_sleepRecoveryAutoPending);
 assert(SleepRecoveryAuto::state.associationHistory["firstLossAfterCurrentElapsedMs"]==15000);
 auto lossFact=std::find_if(facts.begin(),facts.end(),[](const auto& f){return f.first=="sleep-steam-running-association-transition"&&f.second.value("lostAfterCurrent",false);});
 assert(lossFact!=facts.end()&&lossFact->second["returnMutationPreviouslyMarked"]==false&&lossFact->second["from"]["localDisplayStatus"]==4&&lossFact->second["to"]["localDisplayStatus"]==11);
 inputInteractiveIdle=true;tick=21000;sleepRecoveryAutoTick();job();assert(inputCalls==1&&scopeCalls==1&&localStatus==11&&!running);
 assert(SleepRecoveryAuto::state.receipt["inputResume"]["returnBasis"]=="captured-original-tracker");complete(false);
 auto delayedLossReport=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(delayedLossReport["phase"]=="blocked"&&delayedLossReport["associationHistory"]["firstLossAfterCurrentElapsedMs"]==15000&&!delayedLossReport["healthVerified"].get<bool>());
 pass("delayed native 4-to-11 loss is recorded before any RETURN; idle UI still permits primary original-PID handoff without fabricating the secondary Running state");
  reset();running=true;inputContextAvailable=true;begin();owner={selected.pid,selected.processCreated,true};job();assert(SleepRecoveryAuto::get()["phase"]=="game-wait");
 localStatus=11;running=false;owner={};tick=10032;sleepRecoveryAutoTick();job();assert(SleepRecoveryAuto::state.associationHistory["firstCurrentAssociationElapsedMs"]==1500&&SleepRecoveryAuto::state.associationHistory["firstLossAfterCurrentElapsedMs"]==10032&&inputCalls==1);
 pass("first native association evidence is retained while original sleep lease delays action, not overwritten by the first post-thaw snapshot");



 // Field R30: original TMNT identity survives; native live cancellation sends
 // zero Runtime.evaluate requests but the owner used to finish around 12 seconds.
 // Run the production CDP guard extracted above, with a late OS foreground race.
  reset();running=false;localStatus=11;inputContextAvailable=true;begin();foregroundPid=selected.pid;tick=__FIELD_PREDISPATCH_ELAPSED__;
 onSteamCall=[](const std::string& op){if(op=="sleep.input-resume")foregroundPid=steamPid;};job();
 assert(inputCalls==0&&scopeCalls==1&&SleepRecoveryAuto::state.inputDispatches==1);
 if(__PREDISPATCH_BASELINE__){
   assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::get()["reason"]=="live-action-cancelled"&&!g_sleepRecoveryAutoPending);
   pass("baseline reproduces terminal owner cancellation after exact original-PID foreground races the unsent CDP dispatch");
 }else{
   assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="interaction-wait"&&!SleepRecoveryAuto::state.inputAttempted);
   const auto receipt=SleepRecoveryAuto::state.preflightRejections.at(0);
   assert(receipt["nativeAdmissionReason"]=="steam-input-foreground-changed"&&receipt["nativePreDispatchCancelled"]==true&&receipt["requestDispatched"]==false&&receipt["mutated"]==false&&receipt["accepted"]==false);
   onSteamCall={};foregroundPid=selected.pid;tick=21000;sleepRecoveryAutoTick();job();
   assert(inputCalls==1&&scopeCalls==1&&localStatus==11&&!running&&SleepRecoveryAuto::state.inputDispatches==2);
   assert(SleepRecoveryAuto::state.receipt["inputResume"]["returnBasis"]=="captured-original-tracker");
   complete(false);assert(inputCalls==1&&!g_sleepRecoveryAutoPending&&!SleepRecoveryAuto::get()["healthVerified"].get<bool>());
   pass("unsent foreground-only cancellation retains original-PID authority for one fresh return; native11 stays11 and actual input remains unverified");
 }

 if(!__PREDISPATCH_BASELINE__){
    reset();running=false;localStatus=11;inputContextAvailable=true;begin();foregroundPid=selected.pid;
   onSteamCall=[](const std::string& op){if(op=="sleep.input-resume")foregroundPid=foregroundPid==steamPid?selected.pid:steamPid;};job();
   for(int retry=2;retry<=3;++retry){tick=18000+retry*1500;sleepRecoveryAutoTick();job();}
   assert(g_sleepRecoveryAutoPending&&inputCalls==0&&scopeCalls==1&&SleepRecoveryAuto::state.inputDispatches==3&&SleepRecoveryAuto::state.inputAttempted);
   assert(SleepRecoveryAuto::state.preflightRejections.size()==3);
   onSteamCall={};foregroundPid=selected.pid;complete(false);
   assert(inputCalls==0&&scopeCalls==1&&SleepRecoveryAuto::state.inputDispatches==3&&!g_sleepRecoveryAutoPending);
   pass("three proven zero-send foreground cancellations exhaust reservations but retain read-only observation to the original deadline; never dispatch a fourth action");

    reset();running=false;localStatus=11;inputContextAvailable=true;begin();foregroundPid=selected.pid;
   extraForegroundPid=9100;extraForegroundPath=L"C:\\Other\\Editor.exe";
   onSteamCall=[](const std::string& op){if(op=="sleep.input-resume")foregroundPid=extraForegroundPid;};job();
   assert(g_sleepRecoveryAutoPending&&inputCalls==0&&pidActivateCalls==0&&SleepRecoveryAuto::state.last["foreground"]["role"]=="unknown");
   onSteamCall={};tick=21000;sleepRecoveryAutoTick();job();assert(inputCalls==0&&SleepRecoveryAuto::state.inputDispatches==1&&g_sleepRecoveryAutoPending);
   foregroundPid=selected.pid;tick=23000;sleepRecoveryAutoTick();job();assert(inputCalls==1&&scopeCalls==1&&SleepRecoveryAuto::state.inputDispatches==2);
   pass("unrelated foreground after an unsent cancellation preserves original identity observations without input or PID-window focus theft until the original foreground returns");

   for(int mode=0;mode<6;++mode){
      reset();running=false;localStatus=11;inputContextAvailable=true;begin();foregroundPid=selected.pid;
     onSteamCall=[mode](const std::string& op){if(op!="sleep.input-resume")return;
       if(mode==0)selected.processCreated++;if(mode==1)steamCreated++;if(mode==2)steamAccount++;
       if(mode==3)generation++;if(mode==4)SleepRecoveryAuto::set({{"enabled",false}});if(mode==5)gamePaused=true;};
     job();onSteamCall={};tick=21000;sleepRecoveryAutoTick();if(!jobs.empty())job();
     assert(inputCalls==0&&scopeCalls==1&&SleepRecoveryAuto::state.preflightRejections.empty());
     assert(!g_sleepRecoveryAutoPending);
   }
   pass("PID birth, Steam instance/account, new generation, user off and pause ownership vetoes cannot be relabeled foreground-only or refund authority");

   for(int mode=0;mode<5;++mode){
      reset();running=false;localStatus=11;inputContextAvailable=true;preflightRejects=1;
     preflightShapeOverride={{"ok",false},{"reason","live-action-cancelled"},{"mutated",false},{"stage","native-live-pre-dispatch"},{"requestDispatched",false}};
     if(mode==0)preflightShapeOverride.erase("requestDispatched");if(mode==1)preflightShapeOverride["requestDispatched"]="false";
     if(mode==2)preflightShapeOverride["stage"]="sleep-input-resume";if(mode==3)preflightShapeOverride["reason"]="live-transport-uncertain";
     if(mode==4){preflightShapeOverride["nativeAdmissionReason"]="steam-input-foreground-changed";preflightShapeOverride["nativePreDispatchCancelled"]=true;}
     begin();job();assert(inputCalls==0&&SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::state.preflightRejections.empty());
     assert(SleepRecoveryAuto::state.receipt["inputResume"]["nativeAdmissionReason"]=="admitted");
     tick=21000;sleepRecoveryAutoTick();assert(jobs.empty());
   }
   pass("generic cancellation, malformed no-send proofs, timeout/post-action stages and forged JS evidence cannot refund a reservation without the local native foreground veto");
 }
 reset();running=true;inputContextAvailable=true;begin();onSteamCall=[](const std::string& op){if(op=="sleep.inspect"&&steamReads==1)inputInteractiveIdle=false;};job();
 assert(inputCalls==0&&SleepRecoveryAuto::get()["phase"]=="interaction-wait"&&!SleepRecoveryAuto::state.inputAttempted&&SleepRecoveryAuto::state.inputDispatches==1);
 assert(SleepRecoveryAuto::state.preflightRejections.size()==1&&SleepRecoveryAuto::state.preflightRejections[0]["accepted"]==false&&SleepRecoveryAuto::state.preflightRejections[0]["mutated"]==false);
 onSteamCall={};inputInteractiveIdle=true;tick=21000;sleepRecoveryAutoTick();job();assert(inputCalls==1&&SleepRecoveryAuto::state.inputDispatches==2);complete(false);
 auto nativeRaceReport=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(nativeRaceReport["phase"]=="recorded"&&nativeRaceReport["preflightRejections"][0]["accepted"]==false&&nativeRaceReport["attempts"]["inputDispatches"]==2);
 pass("UI opening between the coordinator read and fresh native admission defers without CDP/RETURN; exact idle authority is reissued later, original rejection retained");
 for(int refusals=1;refusals<=3;++refusals){reset();running=true;inputContextAvailable=true;preflightRejects=refusals;begin();job();
   while(g_sleepRecoveryAutoPending&&inputCalls==0){tick+=1500;sleepRecoveryAutoTick();if(!jobs.empty())job();}
   assert(SleepRecoveryAuto::state.inputDispatches<=3&&SleepRecoveryAuto::state.preflightRejections.size()==static_cast<size_t>(refusals));
   if(refusals<3){assert(inputCalls==1&&SleepRecoveryAuto::state.inputDispatches==static_cast<unsigned>(refusals+1));complete(false);assert(SleepRecoveryAuto::get()["phase"]=="recorded");}
   else assert(inputCalls==0&&SleepRecoveryAuto::get()["phase"]=="blocked");
 }
 pass("known JS guard races before mutation are bounded at three dispatches, never more than one executed RETURN; persistent races terminate honestly");
 for(int mode=0;mode<5;++mode){reset();running=true;inputContextAvailable=true;preflightRejects=1;preflightShapeOverride={{"ok",false},{"mutated",false},{"stage","sleep-inspect"},{"reason","steam-interactive-ui-active"}};
   if(mode==0)preflightShapeOverride.erase("mutated");if(mode==1)preflightShapeOverride["mutated"]="false";if(mode==2)preflightShapeOverride["mutated"]=true;
   if(mode==3)preflightShapeOverride["stage"]="sleep-input-resume";if(mode==4)preflightShapeOverride["reason"]="live-readback-failed";
   begin();job();assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::state.inputDispatches==1&&inputCalls==0);
   tick=21000;sleepRecoveryAutoTick();assert(jobs.empty()&&SleepRecoveryAuto::state.inputDispatches==1);
 }
 pass("missing/wrong-type/true mutated, post-action stage and unknown readback errors cannot be relabeled safe preflight or cause a repeated action");
 reset();running=true;inputContextAvailable=true;postMutationGuardFailure=true;begin();job();assert(inputCalls==1&&SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::state.preflightRejections.empty());
 auto postActionReport=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(postActionReport["receipt"]["inputResume"]["mutated"]==true&&postActionReport["receipt"]["inputResume"]["preflightDeferred"]==false);
 tick=21000;sleepRecoveryAutoTick();assert(jobs.empty()&&inputCalls==1);pass("the same protected-UI error after an actual RETURN keeps its rejected mutated receipt and is never replayed");
 for(int mode=0;mode<3;++mode){reset();running=true;inputContextAvailable=true;preflightRejects=1;begin();
   onSteamCall=[mode](const std::string& op){if(op=="sleep.inspect"&&steamReads==2){if(mode==0)selected.processCreated++;if(mode==1)steamAccount++;if(mode==2)gamePaused=true;}};
   job();assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==0&&SleepRecoveryAuto::state.inputDispatches==1&&!g_sleepRecoveryAutoPending);
 }
 pass("a pure UI preflight receipt cannot reacquire authority if the postflight PID/session or manual-pause owner changed");


 // List repair is SECONDARY, never the authority for the captured local-running
 // original-PID return. Preserve every rejected list receipt honestly.
 for(const char* error:{"steam-rescope-unavailable","steam-rescope-schema-changed","steam-rescope-not-confirmed"}){
   reset();running=false;inputContextAvailable=true;rejectListMutation=keepMissingList=true;listRejectReason=error;begin();job();
   assert(scopeCalls==1&&inputCalls==1&&!running&&localStatus==4&&g_sleepRecoveryAutoPending);
   assert(SleepRecoveryAuto::state.receipt["accepted"]==false&&SleepRecoveryAuto::state.secondaryRejection["accepted"]==false);
   assert(SleepRecoveryAuto::state.receipt["inputResume"]["returnBasis"]=="captured-original-local-running");
   assert(!SleepRecoveryAuto::get()["steamRecovery"]["associationVerified"].get<bool>());
   assert(std::any_of(facts.begin(),facts.end(),[](const auto& f){return f.first=="sleep-recovery-automatic-partial-observing";}));
   complete(false);auto partial=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(partial["phase"]=="blocked"&&partial["secondaryRejection"]["accepted"]==false&&partial["attempts"]["list"]==1&&partial["attempts"]["input"]==1);
   assert(scopeCalls==1&&inputCalls==1&&!partial["healthVerified"].get<bool>());
 }
 pass("known unavailable/schema/not-confirmed secondary list failures still allow one exact local-running original-PID RETURN, never fake membership or overall acceptance");
 reset();running=false;localStatus=1;inputContextAvailable=true;rejectListMutation=keepMissingList=true;listRejectReason="steam-rescope-unavailable";begin();job();assert(inputCalls==1&&scopeCalls==1&&!running&&localStatus==1);
 pass("Launching1 with a captured live original window uses its own current local evidence rather than requiring library membership");
 reset();running=false;inputContextAvailable=true;controllersObserved=false;rejectListMutation=keepMissingList=true;listRejectReason="steam-rescope-unavailable";begin();job();
 assert(scopeCalls==1&&inputCalls==0&&g_sleepRecoveryAutoPending);controllersObserved=true;tick=21000;sleepRecoveryAutoTick();job();assert(inputCalls==1&&scopeCalls==1);
 assert(facts.back().first!="sleep-recovery-automatic-accepted");assert(SleepRecoveryAuto::state.secondaryRejection["accepted"]==false);
 pass("secondary rejection stays partial through a later controller reconnect; it never consumes/replays list action or blocks primary return");
 reset();running=false;inputContextAvailable=true;inputInteractiveIdle=false;rejectListMutation=keepMissingList=true;listRejectReason="steam-rescope-not-confirmed";begin();job();assert(inputCalls==0&&scopeCalls==1&&g_sleepRecoveryAutoPending);
 inputInteractiveIdle=true;tick=21000;sleepRecoveryAutoTick();job();assert(inputCalls==1&&scopeCalls==1&&!running);
 pass("secondary-independent credential cannot bypass interactive UI protections; it waits for idle original authority then returns once");
 reset();running=false;inputContextAvailable=true;pidWindowExists=true;deferForeground=true;foregroundPid=steamPid;rejectListMutation=keepMissingList=true;listRejectReason="steam-rescope-unavailable";begin();job();
 assert(inputCalls==1&&scopeCalls==1&&pidActivateCalls==1&&SleepRecoveryAuto::state.receipt["inputResume"]["windowReturnObserved"]==true&&SleepRecoveryAuto::state.receipt["inputResume"]["returnBasis"]=="captured-original-local-running");
 pass("exact original-PID native window postflight accepts local-running basis without deriving authority from missing library membership");
 reset();running=false;inputContextAvailable=true;rejectListMutation=keepMissingList=true;listRejectReason="steam-rescope-not-confirmed";begin();job();running=true;complete(false);
 auto independentlyCurrent=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(independentlyCurrent["phase"]=="recorded"&&independentlyCurrent["after"]["steamRecovery"]["associationVerified"]==true);
 assert(independentlyCurrent["secondaryRejection"]["accepted"]==false&&independentlyCurrent["receipt"]["accepted"]==false&&scopeCalls==1&&inputCalls==1);
 pass("a later genuine synthetic backend observation may become current, but never promotes the rejected rescope receipt via GET/readback");
 for(int mode=0;mode<4;++mode){reset();running=false;inputContextAvailable=true;rejectListMutation=keepMissingList=true;listRejectReason="steam-rescope-unavailable";begin();
   onSteamCall=[mode](const std::string& op){if(op=="sleep.reconcile"){if(mode==0)selected.processCreated++;if(mode==1)steamAccount++;if(mode==2)generation++;if(mode==3)gamePaused=true;}};
   job();if(g_sleepRecoveryAutoPending){tick=3000;sleepRecoveryAutoTick();}assert(inputCalls==0);
 }
 pass("secondary recovery failure is never softened after actual original PID/account/generation/manual-pause authority changes");
 reset();running=false;inputContextAvailable=true;begin(false);job();assert(inputCalls==0&&SleepRecoveryAuto::get()["phase"]=="blocked");
 pass("local native Running without captured original identity cannot authorize the independent primary path");


 // Early mode is a WINDOW operation only. It never proves input and cannot
 // move the OS foreground away from a different application.
 reset();running=true;inputContextAvailable=true;controllersObserved=false;foregroundPid=selected.pid;begin();job();
 assert(inputCalls==1&&windowPrepCalls==1&&scopeCalls==0&&g_sleepRecoveryAutoPending);
 assert(lastInputRequest["preControllerOriginalWindow"]==true&&lastInputRequest["originalWindowReturn"]==true);
 assert(SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false&&SleepRecoveryAuto::state.receipt["inputResume"]["verificationPending"]==true&&SleepRecoveryAuto::state.receipt["inputResume"]["windowReturnObserved"]==true);
 assert(SleepRecoveryAuto::get()["steamInputRecovery"]["repairable"]==false&&!SleepRecoveryAuto::get()["steamInputRecovery"]["contextMatched"].get<bool>());
 tick=15000;sleepRecoveryAutoTick();job();assert(inputCalls==1&&g_sleepRecoveryAutoPending);controllersObserved=true;tick=21000;sleepRecoveryAutoTick();job();complete(false);
 auto earlyWindowReport=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(earlyWindowReport["phase"]=="recorded"&&earlyWindowReport["receipt"]["inputResume"]["accepted"]==false&&earlyWindowReport["attempts"]["input"]==1&&inputCalls==1&&!earlyWindowReport["healthVerified"].get<bool>());
 pass("current captured Running4 original window can be reasserted before controller enumeration; ACK remains rejected/input-unverified and later device arrival never replays RETURN");
  reset();running=true;inputContextAvailable=true;controllersObserved=false;foregroundPid=selected.pid;begin();owner={selected.pid,selected.processCreated,true};job();assert(inputCalls==0&&SleepRecoveryAuto::get()["phase"]=="game-wait");
 tick=10032;owner={};sleepRecoveryAutoTick();job();assert(inputCalls==1&&windowPrepCalls==1&&tick<15000&&!controllersObserved);
 localStatus=11;running=false;tick=15000;sleepRecoveryAutoTick();job();assert(inputCalls==1&&windowPrepCalls==1&&!running&&localStatus==11);
 assert(SleepRecoveryAuto::state.associationHistory["firstLossAfterCurrentElapsedMs"]==15000);controllersObserved=true;tick=21000;sleepRecoveryAutoTick();job();complete(false);
 assert(inputCalls==1&&scopeCalls==1&&SleepRecoveryAuto::get()["phase"]=="blocked");
 pass("window-only preparation can fit between actual lease release and the observed delayed 4-to-11 loss; it does not bypass the lease, fake preservation or repeat after real loss");
  for(int mode=0;mode<8;++mode){reset();running=true;inputContextAvailable=true;controllersObserved=false;foregroundPid=selected.pid;
   if(mode==0)foregroundPid=steamPid;if(mode==1)localStatus=1;if(mode==2){localStatus=11;running=false;}if(mode==3)steamOverlayApp=2399932135U;
   if(mode==4)inputInteractiveIdle=false;if(mode==5)contextOverride=[](json& c){c["controllerListValid"]=false;};if(mode==6)contextOverride=[](json& c){c["editor"]["dirty"]=true;};
   begin(mode!=7);job();assert(windowPrepCalls==0&&inputCalls==0);
 }
 pass("early window eligibility cannot come from Steam/user foreground, Launching/Ready11, active Overlay/menu, invalid controller list, dirty editor or missing original capture");
 reset();running=true;inputContextAvailable=true;controllersObserved=false;foregroundPid=selected.pid;begin();gamePaused=true;job();assert(windowPrepCalls==0&&inputCalls==0&&gamePaused);
 pass("pre-controller preparation never releases or ignores a manual pause owner");
 reset();running=true;inputContextAvailable=true;controllersObserved=false;foregroundPid=selected.pid;pidWindowExists=true;deferForeground=true;begin();job();
 assert(inputCalls==1&&windowPrepCalls==1&&pidActivateCalls==0&&g_sleepRecoveryAutoPending);complete(false);assert(inputCalls==1&&windowPrepCalls==1&&pidActivateCalls==0&&SleepRecoveryAuto::get()["phase"]=="blocked");
 pass("an early already-foreground window operation that loses original focus cannot borrow the later PID-activation fallback or fight the changed foreground");
 reset();running=true;inputContextAvailable=true;controllersObserved=false;foregroundPid=selected.pid;begin();onReturn=[]{controllersObserved=true;};job();
 assert(windowPrepCalls==1&&inputCalls==1&&SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false&&SleepRecoveryAuto::state.receipt["inputResume"]["verificationPending"]==true);complete(false);assert(inputCalls==1);
 pass("controller arrival during window-only operation remains observation-only and cannot promote the original window ACK into input success");
 reset();running=true;inputContextAvailable=true;controllersObserved=false;foregroundPid=selected.pid;begin();onReturn=[]{steamAccount++;};job();assert(inputCalls==1&&SleepRecoveryAuto::get()["phase"]=="blocked"&&pidActivateCalls==0);
 pass("early window request does not survive a real Steam account change during its native postflight");


 // Final wake/capture ordering is independent of source timestamp ordering.
 // The kernel callback can preserve a candidate, never create action authority.
 auto prepareBeforeCapture=[](){modernStandby=true;SleepRecoveryAuto::set({{"enabled",true}});g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoPrepareSuspend();g_powerLifecycle=PowerLifecycle::Ready;};
 reset();running=true;inputContextAvailable=true;prepareBeforeCapture();tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());
 assert(SleepSteamRecovery::captured.generation==0&&SleepRecoveryAuto::kernelWake.awaitedCapture&&!g_sleepRecoveryAutoPending&&jobs.empty());
 sleepRecoveryAutoArm(7);assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="wake-proof-wait"&&SleepRecoveryAuto::state.seenGeneration==0);
 tick=1200;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.armed==1200&&SleepRecoveryAuto::state.wakeAdmission["proofOriginallyAwaitedCapture"]==true);
 while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;tick=2700;sleepRecoveryAutoTick();job();assert(inputCalls==1&&scopeCalls==0);complete(false);
 pass("a true final Kernel507 before the original capture worker is preserved as candidate-only; prior explicit request and matched prepared boundary admit after capture, not at kernel callback");
 reset();running=true;inputContextAvailable=true;prepareBeforeCapture();tick=1000;sleepRecoveryAutoRecordKernelWake(7,107,0,5,sgNowFileTime());
 tick=1200;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;
 assert(!g_sleepRecoveryAutoPending&&inputCalls==0);sleepRecoveryAutoArm(7);assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.wakeAdmission["source"]=="kernel-power-107-target-5");
 pass("S4 final event before capture is retained too, but capture alone cannot arm without the explicit wake entry");
 reset();running=true;inputContextAvailable=true;prepareBeforeCapture();auto preparedTime=SleepSteamRecovery::prepared.powerBoundaryFileTime;
 tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(7);generation=8;tick=1200;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(8);
 assert(SleepSteamRecovery::captured.boundaryGeneration==7&&SleepSteamRecovery::captured.powerBoundaryFileTime==preparedTime&&g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.generation==8);
 assert(SleepRecoveryAuto::state.wakeAdmission["proofFollowedPreparedBoundary"]==true&&SleepRecoveryAuto::state.wakeAdmission["proofObservedGeneration"]==7);
 while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;tick=2700;sleepRecoveryAutoTick();job();assert(inputCalls==1);
 pass("a raw final wake may follow ONLY the identical prepared sleep boundary across its legitimate one-generation capture transfer");
 for(int mode=0;mode<4;++mode){reset();running=true;inputContextAvailable=true;prepareBeforeCapture();tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(7);
   if(mode==0)generation=9;else generation=8;
   if(mode==1){sleepRecoveryAutoCancelPreparedSuspend();tick=1200;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoPrepareSuspend();}
   if(mode==2){sleepRecoveryAutoCancelPreparedSuspend();}
   if(mode==3){SleepRecoveryAuto::set({{"enabled",false}});SleepRecoveryAuto::set({{"enabled",true}});}
   tick=1300;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(generation);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoArm(generation);
   assert(!g_sleepRecoveryAutoPending&&inputCalls==0&&scopeCalls==0);
 }
 pass("old raw proof cannot bind after skipped generation, another newly prepared boundary, aborted preparation or config epoch toggle");
 reset();running=true;inputContextAvailable=true;modernStandby=true;SleepRecoveryAuto::set({{"enabled",true}});tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(7);
 tick=1200;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;
 assert(!g_sleepRecoveryAutoPending&&inputCalls==0&&SleepRecoveryAuto::get()["phase"]=="wake-proof-wait");
 pass("a capture first created after the true exit without an earlier prepared boundary cannot retroactively authorize that wake candidate");
 reset();running=true;inputContextAvailable=true;begin();job();complete(false);assert(SleepRecoveryAuto::state.seenGeneration==7&&inputCalls==1);
 modernStandby=true;tick=70000;g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoPrepareSuspend();
 tick=71000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());assert(SleepRecoveryAuto::kernelWake.awaitedCapture&&SleepRecoveryAuto::kernelWake.boundaryFileTime==SleepSteamRecovery::prepared.powerBoundaryFileTime);
 sleepRecoveryAutoArm(7);assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::kernelWake.uiWakeRequested&&inputCalls==1);
 generation=8;tick=71200;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(8);assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.seenGeneration==8);
 while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;steamFocusedApp=413080;tick=72700;sleepRecoveryAutoTick();job();assert(inputCalls==2);
 sleepRecoveryAutoCaptureSuspend(8);sleepRecoveryAutoArm(8);assert(inputCalls==2);
 pass("a fresh prepared boundary outranks the completed prior capture still under pre-increment generation; its explicit request transfers once without replaying the prior cycle");
 reset();running=true;inputContextAvailable=true;prepareBeforeCapture();tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,0);sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime()+10000);
 assert(SleepRecoveryAuto::kernelWake.eventFileTime==0&&!g_sleepRecoveryAutoPending);tick=1100;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());auto rawWake=SleepRecoveryAuto::kernelWake.eventFileTime;
 sleepRecoveryAutoRecordKernelWake(7,507,1,-1,rawWake-100);assert(SleepRecoveryAuto::kernelWake.eventFileTime==rawWake);
 pass("missing/future pre-capture events are rejected and an older duplicate cannot overwrite the one bounded final-wake candidate");


 // Round24: a verified exact-PID RETURN remains a receipt, not an input proof.
 // Later transient foreground does not revoke the immutable capture or stop READS.
 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;begin();job();assert(inputCalls==1);
 foregroundPid=steamPid;steamFocusedApp=769;tick=3000;sleepRecoveryAutoTick();job();
 assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.postReturnForegroundWait&&inputCalls==1&&scopeCalls==0&&pidActivateCalls==0);
 assert(SleepRecoveryAuto::state.timeline.back()["stage"]=="post-return-foreground-wait"&&SleepRecoveryAuto::state.timeline.back()["game"]["pid"]==12932);
 tick=18000;sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.postReturnForegroundSamples==2);
 foregroundPid=selected.pid;steamFocusedApp=2399932135U;tick=21000;sleepRecoveryAutoTick();job();complete(false);
 auto foregroundRecovered=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(foregroundRecovered["phase"]=="recorded"&&foregroundRecovered["settling"]["lastElapsedMs"]>=30000&&inputCalls==1&&scopeCalls==0);
 assert(foregroundRecovered["receipt"]["inputResume"]["accepted"]==false&&foregroundRecovered["postReturnObservation"]["foregroundDeferredSamples"]==2);
 assert(foregroundRecovered["postReturnObservation"]["waitingOriginalForeground"]==false&&!foregroundRecovered["healthVerified"].get<bool>());
 pass("completed original-PID RETURN survives later Steam foreground until genuine context converges, without replay or promoting the uncertain receipt");

 reset();running=true;inputContextAvailable=true;begin();job();foregroundPid=6000;running=false;
 tick=3000;sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending&&scopeCalls==0&&inputCalls==1);
 auto blockedForUser=SleepRecoveryAuto::exportCurrentSnapshot();auto exportedUser=json::parse(readFile(blockedForUser));
 assert(exportedUser["reportState"]=="pending"&&exportedUser["postReturnObservation"]["waitingOriginalForeground"]==true);
 assert(exportedUser["after"]["foreground"]["pid"]==6000&&exportedUser["after"]["game"]["pid"]==12932&&exportedUser["attempts"]["list"]==0);
 complete(false);assert(foregroundPid==6000&&tick<=61000&&inputCalls==1&&scopeCalls==0&&pidActivateCalls==0);
 assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&!SleepRecoveryAuto::state.settling.ready);
 pass("foreign user foreground cannot authorize even an unused secondary rescope: retain bounded original-game reads and end unverified without stealing focus");

 // R49: a dispatched native RETURN may still be moving its window after its immediate postflight.
 reset();running=true;inputContextAvailable=true;deferForeground=true;pidApiAccepted=false;pidActivationFocusesOriginal=false;
 begin();job();assert(inputCalls==1&&g_sleepRecoveryAutoPending);
 auto asynchronousReturn=SleepRecoveryAuto::state.receipt["inputResume"];
 assert(SleepRecoveryAuto::state.postReturnObservationSummary()["waitingInitialOriginalWindow"]==true&&SleepRecoveryAuto::state.postReturnObservationSummary()["initialWindowReadbackSamples"]==1);
 assert(asynchronousReturn["reason"]=="steam-original-window-not-confirmed"&&asynchronousReturn["accepted"]==false&&asynchronousReturn["originalIdentityStable"]==true);
 const auto spentPidActivations=pidActivateCalls;
 foregroundPid=selected.pid;steamFocusedApp=2399932135U;tick+=3000;sleepRecoveryAutoTick();job();complete(false);
 auto lateOriginalWindow=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(lateOriginalWindow["phase"]=="recorded"&&lateOriginalWindow["receipt"]["inputResume"]["accepted"]==false&&inputCalls==1&&pidActivateCalls==spentPidActivations&&scopeCalls==0);
 assert(lateOriginalWindow["postReturnObservation"]["delayedOriginalWindowObserved"]==true&&lateOriginalWindow["postReturnObservation"]["waitingInitialOriginalWindow"]==false);
 assert(lateOriginalWindow["healthVerified"]==false&&tick<=61000);
 pass("late original window after dispatched native RETURN remains observable without any action replay or receipt promotion");

 reset();running=true;inputContextAvailable=true;deferForeground=true;pidApiAccepted=false;pidActivationFocusesOriginal=false;
 begin();job();assert(g_sleepRecoveryAutoPending&&inputCalls==1);const auto noWindowPidActions=pidActivateCalls;complete(false);
 auto neverWindow=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(neverWindow["phase"]=="blocked"&&neverWindow["receipt"]["inputResume"]["accepted"]==false&&neverWindow["postReturnObservation"]["initialWindowReadbackSamples"].get<unsigned>()>1);
 assert(neverWindow["postReturnObservation"]["delayedOriginalWindowObserved"]==false&&inputCalls==1&&pidActivateCalls==noWindowPidActions&&scopeCalls==0&&tick<=61000);
 foregroundPid=selected.pid;steamFocusedApp=2399932135U;tick=65000;sleepRecoveryAutoTick();assert(!g_sleepRecoveryAutoPending&&jobs.empty()&&inputCalls==1&&pidActivateCalls==noWindowPidActions);
 pass("initial original window never returns: remaining observations end at original wake deadline without replay or success");
 reset();running=true;inputContextAvailable=true;deferForeground=true;pidApiAccepted=false;pidActivationFocusesOriginal=false;
 begin();job();const auto late45PidActions=pidActivateCalls;
 while(tick<45000){tick+=1500;sleepRecoveryAutoTick();if(!jobs.empty())job();assert(g_sleepRecoveryAutoPending&&inputCalls==1&&scopeCalls==0&&pidActivateCalls==late45PidActions);}
 foregroundPid=selected.pid;steamFocusedApp=2399932135U;tick+=1500;sleepRecoveryAutoTick();job();complete(false);
 auto late45=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(late45["phase"]=="recorded"&&late45["receipt"]["inputResume"]["accepted"]==false&&inputCalls==1&&scopeCalls==0&&pidActivateCalls==late45PidActions&&tick<=61000);
 assert(late45["postReturnObservation"]["initialWindowReadbackSamples"].get<unsigned>()>20&&late45["postReturnObservation"]["delayedOriginalWindowObserved"]==true);
 pass("45-second original window convergence stays within the same wake deadline with one RETURN and no PID replay");
 reset();running=true;inputContextAvailable=true;deferForeground=true;pidApiAccepted=false;pidActivationFocusesOriginal=false;
 begin();job();const auto lateScopePidActions=pidActivateCalls;localStatus=11;running=false;tick+=3000;sleepRecoveryAutoTick();job();
 assert(g_sleepRecoveryAutoPending&&scopeCalls==0&&inputCalls==1&&pidActivateCalls==lateScopePidActions);
 foregroundPid=selected.pid;steamFocusedApp=2399932135U;tick+=3000;sleepRecoveryAutoTick();job();complete(false);
 auto lateScope=json::parse(readFile(SleepRecoveryAuto::reportPath()));assert(scopeCalls==1&&inputCalls==1&&pidActivateCalls==lateScopePidActions&&lateScope["receipt"]["inputResume"]["accepted"]==false&&lateScope["phase"]=="blocked"&&lateScope["after"]["steam"]["overviews"][0]["localDisplayStatus"]==11);
 pass("late original window does not suppress an unused independently guarded single scope; missing Running still ends unverified");

 for(int mode=0;mode<7;++mode){reset();running=true;inputContextAvailable=true;deferForeground=true;pidApiAccepted=false;pidActivationFocusesOriginal=false;
   begin();job();assert(g_sleepRecoveryAutoPending&&inputCalls==1);const auto spent=pidActivateCalls;
   if(mode==0)gameExists=false;if(mode==1)selected.processCreated++;if(mode==2)steamCreated++;if(mode==3)gamePaused=true;if(mode==4)generation++;if(mode==5)SleepRecoveryAuto::set({{"enabled",false}});if(mode==6)steamAccount++;
   tick+=3000;sleepRecoveryAutoTick();if(!jobs.empty())job();assert(!g_sleepRecoveryAutoPending&&inputCalls==1&&pidActivateCalls==spent&&scopeCalls==0);
 }
 pass("initial-window read-only wait stops on original death/PID reuse, Steam replacement/account change, pause, new sleep or user off");
 reset();running=true;inputContextAvailable=true;deferForeground=true;postMutationGuardFailure=true;begin();job();
 assert(inputCalls==1&&!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false);
 pass("unrelated post-action guard failures cannot borrow the exact original-window-miss observation path");

 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;bool switchedAfterPostflight=false;
 onSteamCall=[&](const std::string& op){if(op=="sleep.inspect"&&!switchedAfterPostflight&&std::any_of(facts.begin(),facts.end(),[](const auto& f){return f.first=="sleep-steam-input-resume-result";})){foregroundPid=steamPid;steamFocusedApp=769;switchedAfterPostflight=true;}};
 begin();job();assert(switchedAfterPostflight&&g_sleepRecoveryAutoPending&&inputCalls==1&&SleepRecoveryAuto::state.postReturnForegroundWait);
 assert(SleepRecoveryAuto::state.receipt["inputResume"]["windowReturnObserved"]==true&&SleepRecoveryAuto::state.last["originalGameRecovery"]["originalWindowFocused"]==false);
 onSteamCall={};foregroundPid=selected.pid;steamFocusedApp=2399932135U;complete(false);
 assert(SleepRecoveryAuto::get()["phase"]=="recorded"&&inputCalls==1&&SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false);
 pass("foreground change between successful native PID postflight and the NEW after read resets readiness instead of discarding a genuine pending receipt");

 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;begin();job();
 foregroundPid=steamPid;controllersObserved=false;steamFocusedApp=769;tick=3000;sleepRecoveryAutoTick();job();
 tick=18000;sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending&&inputCalls==1&&!SleepRecoveryAuto::state.settling.ready);
 controllersObserved=true;foregroundPid=selected.pid;steamFocusedApp=2399932135U;
 contextOverride=[](json& c){c["controllers"]=json::array({{{"index",15},{"type",4}}});};tick=21000;sleepRecoveryAutoTick();job();complete(false);
 auto reconnectObserved=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(reconnectObserved["phase"]=="recorded"&&reconnectObserved["after"]["steam"]["inputContext"]["controllers"][0]["index"]==15&&inputCalls==1);
 assert(reconnectObserved["observations"][1]["inputControllers"].empty()&&reconnectObserved["observations"].back()["inputControllers"][0]["type"]==4);
 pass("Steam-side late controller-index/type convergence is observed on the same captured game without controller operations or RETURN replay");

 for(int mode=0;mode<6;++mode){reset();running=true;inputContextAvailable=true;begin();job();foregroundPid=steamPid;tick=3000;sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending);
   if(mode==0)gameExists=false;if(mode==1)selected.processCreated++;if(mode==2)steamCreated++;if(mode==3)gamePaused=true;if(mode==4)generation++;if(mode==5)SleepRecoveryAuto::set({{"enabled",false}});
   tick=6000;sleepRecoveryAutoTick();if(!jobs.empty())job();assert(!g_sleepRecoveryAutoPending&&inputCalls==1&&scopeCalls==0&&pidActivateCalls==0);
 }
 pass("post-RETURN read-only foreground wait still stops on original death/PID reuse, Steam replacement, manual pause, new sleep generation or user off");

 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;begin();job();foregroundPid=steamPid;running=false;
 tick=3000;sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending&&scopeCalls==0&&inputCalls==1);
 foregroundPid=selected.pid;steamFocusedApp=2399932135U;tick=6000;sleepRecoveryAutoTick();job();
 assert(scopeCalls==1&&inputCalls==1&&SleepRecoveryAuto::state.last["steamRecovery"]["associationVerified"]==true);
 assert(SleepRecoveryAuto::state.receipt["inputResume"]["accepted"]==false);complete(false);
 assert(SleepRecoveryAuto::get()["phase"]=="recorded"&&scopeCalls==1&&inputCalls==1&&pidActivateCalls==0);
 pass("when original foreground genuinely returns, the unused positively guarded native list rescope still works once without replaying input or promoting its pending receipt");

 // The production game-name helper strips .exe. A filename-returning mock
 // hid Steam UI admission failures in all previous foreground tests.
 for(int mode=0;mode<3;++mode){reset();extraForegroundPid=5000;foregroundPid=extraForegroundPid;
   extraForegroundPath=steamRoot+(mode==0?L"\\bin\\cef\\cef.win7x64\\steamwebhelper.exe":mode==1?L"\\gameoverlayui.exe":L"/BIN/CEF/SteamWebHelper.Exe");
   const auto stem=sgBaseName(extraForegroundPath);assert(stem==L"steamwebhelper"||stem==L"gameoverlayui");
   auto ui=SleepSteamRecovery::foreground(SleepSteamRecovery::currentGame(),SleepSteamRecovery::session());
   assert(ui.role=="steam-ui"&&ui.pid==5000&&ui.created==extraForegroundCreated&&SleepSteamRecovery::allowedForeground(ui));
 }
 pass("real production name normalizer and exact verified install-path Steam UI executable agree for webhelper/overlay UI and mixed-case paths");
 for(int mode=0;mode<7;++mode){reset();extraForegroundPid=5000;foregroundPid=extraForegroundPid;
   if(mode==0)extraForegroundPath=steamRoot+L"-fake\\steamwebhelper.exe";
   if(mode==1)extraForegroundPath=L"D:\\OtherSteam\\steamwebhelper.exe";
   if(mode==2)extraForegroundPath=steamRoot+L"\\steamwebhelper.exe.bak";
   if(mode==3)extraForegroundPath=steamRoot+L"\\unrelated.exe";
   if(mode==4)extraForegroundPath=steamRoot+L"\\steamwebhelper";
   if(mode==5){extraForegroundPath=steamRoot+L"\\steamwebhelper.exe";extraForegroundAvailable=false;}
   if(mode==6){extraForegroundPath=steamRoot+L"\\steamwebhelper.exe";extraForegroundCreated=0;}
   auto other=SleepSteamRecovery::foreground(SleepSteamRecovery::currentGame(),SleepSteamRecovery::session());
   assert(other.role=="unknown"&&!SleepSteamRecovery::allowedForeground(other));
 }
 pass("similar root, other install, backup/extensionless executable, unrelated leaf or unverifiable/zero-birth image cannot borrow Steam UI foreground admission");
 reset();running=true;inputContextAvailable=true;extraForegroundPid=5000;extraForegroundPath=steamRoot+L"\\bin\\cef\\steamwebhelper.exe";foregroundPid=5000;
 begin();job();assert(inputCalls==1&&scopeCalls==0&&SleepRecoveryAuto::state.receipt["inputResume"]["originalPid"]==12932);
 assert(SleepRecoveryAuto::state.before["foreground"]["role"]=="steam-ui"&&SleepRecoveryAuto::state.last["game"]["pid"]==12932);complete(false);
 assert(SleepRecoveryAuto::get()["phase"]=="recorded"&&inputCalls==1);
 pass("verified real Big Picture foreground can dispatch one original captured-PID RETURN without using the UI process as the game target");
 reset();localStatus=11;inputContextAvailable=true;extraForegroundPid=5000;extraForegroundPath=steamRoot+L"\\bin\\cef\\steamwebhelper.exe";foregroundPid=5000;
 begin();job();assert(inputCalls==1&&scopeCalls==1&&SleepRecoveryAuto::state.receipt["inputResume"]["returnBasis"]=="captured-original-tracker");
 assert(SleepRecoveryAuto::state.last["originalGameRecovery"]["originalWindowFocused"]==true);complete(false);
 assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&localStatus==11&&!running&&inputCalls==1);
 pass("Big Picture classification no longer blocks primary original-game return after secondary Running loss, while no native state or Continue is fabricated");
 reset();running=true;inputContextAvailable=true;extraForegroundPid=5000;extraForegroundPath=steamRoot+L"\\gameoverlayui.exe";foregroundPid=5000;inputInteractiveIdle=false;
 begin();job();assert(inputCalls==0&&scopeCalls==0&&g_sleepRecoveryAutoPending);complete(false);
 assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==0&&pidActivateCalls==0);
 pass("correct overlay-UI executable classification is not authority to bypass a visible interactive overlay/editor/menu guard");

 // Real 07:31 log replay uses production verdicts, plus a deterministic coordinator
 // seam mapped onto the fixture PID. Any later recovery here is synthetic, not field proof.
 const auto fieldR23=json::parse(readFile(argv[5]));assert(fieldR23["implementation"]=="steam-wake-round23-ordered-wake-proof"&&fieldR23["game"]["pid"]==10100);
 Binding fieldBinding;fieldBinding.status="tracked";fieldBinding.appid=3320061248U;
 auto fieldAssociation=verdict(fieldR23["after"]["steam"],fieldBinding,true);
 assert(!fieldAssociation["associationVerified"].get<bool>()&&fieldR23["after"]["steam"]["overviews"][0]["localDisplayStatus"]==11);
 assert(fieldR23["after"]["game"]["identityMatches"]==true&&fieldR23["inputResume"]["windowReturnObserved"]==true&&fieldR23["inputResume"]["accepted"]==false);
 pass("replay actual 07:31 R23 report: acknowledged original-PID RETURN and live same instance are not evidence of retained Steam running association or gameplay");
 reset();running=true;inputContextAvailable=true;uiReadbackStale=true;begin();tick=fieldR23["observations"][0]["elapsedMs"].get<ULONGLONG>();job();
 localStatus=fieldR23["observations"][1]["overviews"][0]["localDisplayStatus"].get<int>();running=false;controllersObserved=false;steamFocusedApp=2399932135U;
 tick=fieldR23["observations"][1]["elapsedMs"].get<ULONGLONG>();sleepRecoveryAutoTick();job();assert(g_sleepRecoveryAutoPending);
 assert(SleepRecoveryAuto::state.associationHistory["firstLossAfterCurrentElapsedMs"]==12687&&SleepRecoveryAuto::state.last["game"]["identityMatches"]==true);
 foregroundPid=steamPid;steamFocusedApp=769;controllersObserved=true;
 contextOverride=[&](json& c){c["controllers"]=fieldR23["after"]["steam"]["inputContext"]["controllers"];};
 tick=fieldR23["associationHistory"]["lastReliableElapsedMs"].get<ULONGLONG>();sleepRecoveryAutoTick();job();
 assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.postReturnForegroundWait&&inputCalls==1&&scopeCalls==1);
 assert(SleepRecoveryAuto::state.timeline.back()["tracker"]["status"]=="tracked"&&SleepRecoveryAuto::state.timeline.back()["inputControllers"][0]["index"]==15);
 complete(false);auto persistentLoss=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(persistentLoss["phase"]=="blocked"&&tick>=60000&&tick<=61000&&inputCalls==1&&scopeCalls==1&&pidActivateCalls==0);
 assert(persistentLoss["after"]["steam"]["overviews"][0]["localDisplayStatus"]==11&&persistentLoss["associationHistory"]["firstLossAfterCurrentElapsedMs"]==12687);
 assert(persistentLoss["postReturnObservation"]["foregroundDeferredSamples"]>1&&!persistentLoss["healthVerified"].get<bool>());
 pass("07:30:06 association-loss/07:30:07 Steam-foreground shape keeps PID-bound evidence until the original deadline, never invents Running or retries the action");

 {
 // R25 capture inventory parses existing exact tracker rows; it grants no writes.
 const auto captureField=json::parse(readFile(argv[6]));std::vector<std::string> fieldLines;
 std::istringstream fieldStream(captureField["gameprocessLog"].get<std::string>());std::string fieldLine;while(std::getline(fieldStream,fieldLine))fieldLines.push_back(fieldLine);
 auto fieldInventory=trackedProcessInventory(fieldLines);
 assert(fieldInventory.complete&&std::any_of(fieldInventory.rows.begin(),fieldInventory.rows.end(),[](const auto& row){return row.pid==17060&&row.appid==3320061248U&&pathKey(row.path)==pathKey("E:\\GAME\\Cyberpunk 2077\\bin\\x64\\Cyberpunk2077.exe");}));
 assert(std::none_of(fieldInventory.rows.begin(),fieldInventory.rows.end(),[](const auto& row){return row.pid==14904||row.pid==6700||row.pid==5228;}));
 pass("actual 21:38 tracker rows discover Cyberpunk PID17060, never MSI14904 or already-untracked Wukong instances");
 auto logRow=[](DWORD pid,const std::wstring& path,std::uint32_t app,ULONGLONG birth){return "["+SleepSteamRecovery::localSecond(birth)+"] AppID "+std::to_string(app)+" adding PID "+std::to_string(pid)+" as a tracked process \"\""+W2U(path)+"\"\"\n";};
 const auto liveInventory=trackedProcessInventory({"[2026-10-12 21:32:33] AppID 14259554480910499840 adding PID 17060 as a tracked process \"\"E:\\GAME\\Cyberpunk2077.exe\"\"", "[2026-10-12 21:32:34] AppID 14259554480910499840 adding PID 2836 as a tracked process \"REDEngineErrorReporter.exe 17060\""});
 assert(liveInventory.complete&&liveInventory.rows.size()==2);
 for(int mode=0;mode<3;++mode){auto rows=std::vector<std::string>{"[2026-10-12 21:32:33] AppID 3320061248 adding PID 17060 as a tracked process \"\"E:\\GAME\\Cyberpunk2077.exe\"\""};
  rows.push_back(mode==0?"[2026-10-12 21:33:00] Remove -974906048 from running list":mode==1?"[2026-10-12 21:33:00] AppID 14259554480910499840 no longer tracking PID 17060, exit code 1":"[2026-10-12 21:33:00] Client version: 123");
  assert(trackedProcessInventory(rows).rows.empty());
 }
 assert(trackedProcessInventory(std::vector<std::string>(20001,"noise")).complete);
  assert(!trackedProcessInventory({std::string(trackerMaximumBytes+1,'x')}).complete);
 pass("capture discovery normalizes shortcut IDs and clears true drops/removals/client epochs; malformed or over-limit logs do not grant process authority");
 reset();running=true;inputContextAvailable=true;auto real=selected;captureProcesses[real.pid]=real;captureWindowPids.insert(real.pid);
 selected={14904,real.processCreated,L"C:\\Fixtures\\MSI Center M.exe"};owner={selected.pid,selected.processCreated,true};foregroundPid=steamPid;
 begin();job();assert(inputCalls==1&&scopeCalls==0&&SleepSteamRecovery::captured.game.pid==real.pid);
 assert(SleepSteamRecovery::captured.targetSelection["source"]=="unique-exact-tracked-own-window-before-suspend"&&SleepSteamRecovery::captured.targetSelection["preferredPid"]==14904);
 assert(lastInputRequest["originalTarget"]["pid"]==real.pid&&selected.pid==14904&&owner.pid==14904&&owner.markerOwned);
 assert(SleepSteamRecovery::captured.targetSelection["jointBindingVerified"]==true&&SleepSteamRecovery::captured.targetSelection["sharedCandidateMatched"]==false);
 assert(SleepSteamRecovery::captured.targetSelection["sharedCandidateStatus"]=="different-instance"&&SleepSteamRecovery::captured.targetSelection["ymccPidValidation"]["source"]=="nativeScanGameOnePid");
 assert(SleepSteamRecovery::captured.targetSelection["ymccSteamJoin"]["ymccValidatedPid"]==real.pid&&SleepSteamRecovery::captured.targetSelection["ymccSteamJoin"]["steamTrackedPid"]==real.pid);
 assert(!SleepSteamRecovery::capturedPauseState(7)["waitForSleepRelease"].get<bool>());complete(false);
 assert(SleepRecoveryAuto::get()["phase"]=="recorded"&&inputCalls==1&&SleepRecoveryAuto::state.last["game"]["pid"]==real.pid);
 pass("misclassified MSI current candidate cannot receive Steam repair: unique exact live tracked game is pinned, RETURN uses it, and shared game/lease owner remain untouched");
 reset();auto localReal=selected;captureProcesses[localReal.pid]=localReal;captureWindowPids.insert(localReal.pid);selected={14904,localReal.processCreated,L"C:\\Fixtures\\MSI Center M.exe"};
 SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();assert(SleepSteamRecovery::prepared.game.pid==localReal.pid&&steamReads==0&&inputCalls==0&&scopeCalls==0);
 selected={15000,localReal.processCreated,L"C:\\Fixtures\\Other.exe"};captureProcesses.erase(localReal.pid);generation=8;g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(8);
 assert(SleepSteamRecovery::captured.game.pid==localReal.pid&&SleepSteamRecovery::captured.boundaryGeneration==7&&SleepSteamRecovery::captured.targetSelection["preferredPid"]==14904);
 assert(SleepSteamRecovery::inspect(8)["game"]["identityMatches"]==false&&inputCalls==0);
 pass("Steam target selection is pre-boundary only: prepared identity transfers unchanged and a later exit/UI candidate cannot replace it");
 for(int mode=0;mode<12;++mode){reset();real=selected;captureProcesses[real.pid]=real;captureWindowPids.insert(real.pid);selected={14904,real.processCreated,L"C:\\Fixtures\\MSI Center M.exe"};
  if(mode==0)captureWindowPids.clear();
  if(mode==1)captureProcesses[real.pid].path=L"E:\\GAME\\Other.exe";
  if(mode==2)captureProcesses[real.pid].processCreated+=1000000000ULL;
  if(mode==3)captureProcesses[real.pid].processCreated=sgNowFileTime()+10000;
  if(mode==4)captureForeignSessionPids.insert(real.pid);
  if(mode==5)captureSessionQueryFails=true;
  if(mode==6)captureExcludes.push_back(L"tmnt");
  if(mode==7)onCaptureWindow=[](DWORD){steamCreated++;};
  if(mode==8)onCaptureWindow=[&](DWORD pid){captureProcesses[pid].processCreated++;};
  if(mode==9)onCaptureWindow=[](DWORD){tick+=1001;};
  if(mode==10)sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt","[2026-10-12 21:00:00] Client version: 123\n");
  if(mode==11)steamProcessAvailable=false;
  SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();assert(!SleepSteamRecovery::prepared.game.pid&&SleepSteamRecovery::prepared.targetSelection["status"]=="unbound"&&inputCalls==0&&scopeCalls==0);
 }
 pass("fallback capture fails closed on missing own window, wrong path/birth, post-boundary birth, another Windows session, excludes, death/session races, budget, missing tracker or Steam");
 reset();real=selected;captureProcesses[real.pid]=real;captureWindowPids.insert(real.pid);selected={14904,real.processCreated,L"C:\\Fixtures\\MSI Center M.exe"};
 NativeDetectedGame second{15000,real.processCreated,L"E:\\GAME\\Second.exe"};captureProcesses[second.pid]=second;captureWindowPids.insert(second.pid);
 auto log=readFile(steamRoot+L"\\logs\\gameprocess_log.txt");sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt",log+logRow(second.pid,second.path,2399932135U,second.processCreated));
 SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();assert(!SleepSteamRecovery::prepared.game.pid&&SleepSteamRecovery::prepared.targetSelection["reason"]=="steam-original-target-native-primary-unverifiable"&&SleepSteamRecovery::prepared.targetSelection["nativePrimaryRejectedCount"]==2);
 pass("two tracked members of one AppID without native primary proof remain unbound, never resolved by foreground/title heuristics");
 reset();real=selected;captureProcesses[real.pid]=real;captureWindowPids.insert(real.pid);selected={14904,real.processCreated,L"C:\\Fixtures\\MSI Center M.exe"};
 captureExcludes.push_back(L"tmnt");captureWhitelist.push_back(L"tmnt");SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();
 assert(SleepSteamRecovery::prepared.game.pid==real.pid&&selected.pid==14904);
 pass("Steam-only fallback uses existing explicit whitelist/exclude precedence without modifying shared game selection");
 reset();real=selected;captureProcesses[real.pid]=real;captureWindowPids.insert(real.pid);captureProcesses[real.pid].processCreated++;
 SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();assert(!SleepSteamRecovery::prepared.game.pid);
 pass("same PID/path with a changed birth cannot borrow pre-sleep fallback to replace a stale preferred instance within the log timestamp tolerance");
 reset();real=selected;captureProcesses[real.pid]=real;captureWindowPids.insert(real.pid);selected={14904,real.processCreated,L"C:\\Fixtures\\MSI Center M.exe"};
 sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt",std::string(2*1024*1024+1,'x')+readFile(steamRoot+L"\\logs\\gameprocess_log.txt"));
 SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();assert(!SleepSteamRecovery::prepared.game.pid&&SleepSteamRecovery::prepared.targetSelection["reason"]=="steam-capture-tracker-not-complete");
 pass("truncated tracker cannot establish uniqueness for capture or authorize a guessed original target");
 reset();NativeDetectedGame actual;actual.pid=17060;actual.path=U2W(captureField["actualSteamGame"]["path"].get<std::string>());
 SYSTEMTIME at{};at.wYear=2026;at.wMonth=10;at.wDay=12;at.wHour=21;at.wMinute=32;at.wSecond=33;SYSTEMTIME utc{};FILETIME ft{};
 assert(TzSpecificLocalTimeToSystemTime(nullptr,&at,&utc)&&SystemTimeToFileTime(&utc,&ft));actual.processCreated=(static_cast<ULONGLONG>(ft.dwHighDateTime)<<32)|ft.dwLowDateTime;
 fileTimeBase=std::stoull(captureField["wakeAdmission"]["boundaryFileTime"].get<std::string>());
 selected={14904,std::stoull(captureField["wrongCapturedGame"]["created"].get<std::string>()),L"C:\\Fixtures\\MSI Center M.exe"};
 captureProcesses[actual.pid]=actual;captureWindowPids.insert(actual.pid);sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt",captureField["gameprocessLog"].get<std::string>());
 sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt","["+SleepSteamRecovery::localSecond(actual.processCreated)+"] Game process added : AppID 3320061248 \"simulated-native-primary\", ProcID 17060, IP 0.0.0.0:0\n");
 SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();assert(SleepSteamRecovery::prepared.game.pid==17060&&SleepSteamRecovery::prepared.targetSelection["selectedAppid"]==3320061248U);
 assert(SleepSteamRecovery::prepared.targetSelection["preferredPid"]==14904&&inputCalls==0&&steamReads==0);
 pass("replay actual 21:38 tracker rows plus simulated native primary/PID/birth/window: pre-sleep binding chooses2077 notMSI; no real gameplay recovery claimed");

 }
 {
 for(int mode=0;mode<3;++mode){reset();auto real=selected;captureProcesses[real.pid]=real;captureWindowPids.insert(real.pid);selected={14904,real.processCreated,L"C:\\Fixtures\\MSI Center M.exe"};
  NativeDetectedGame second{15000,real.processCreated,L"E:\\GAME\\Second.exe"};captureProcesses[second.pid]=second;captureWindowPids.insert(second.pid);
  const auto at=SleepSteamRecovery::localSecond(second.processCreated);auto log=readFile(steamRoot+L"\\logs\\gameprocess_log.txt");
  log+="["+at+"] AppID 945360 adding PID 15000 as a tracked process \"\"E:\\GAME\\Second.exe\"\"\n";
  if(mode==0)captureIdentityUnavailablePids.insert(second.pid);
  if(mode==1)log+="["+at+"] AppID 2399932135 adding PID 15000 as a tracked process \"\"E:\\GAME\\Second.exe\"\"\n";
  if(mode==2)captureForeignSessionPids.insert(second.pid);
  sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt",log);SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();
  assert(!SleepSteamRecovery::prepared.game.pid&&SleepSteamRecovery::prepared.targetSelection["unresolvedOwnWindowCount"]>0&&inputCalls==0);
 }
 pass("another tracked own-window app with unknown identity, ambiguous AppID or foreign/unverifiable session prevents silently selecting the only accessible game");
 }

 // R25 joint-PID amendment consumes the actual shared one-PID implementation,
 // not a hand-written permissive surrogate for YMCC game recognition.
 {
 auto capture=[](){SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();};
 auto reversed=[](){auto game=selected;captureProcesses[game.pid]=game;captureWindowPids.insert(game.pid);selected={14904,game.processCreated,L"C:\\Fixtures\\MSI Center M.exe"};return game;};
 reset();capture();auto evidence=SleepSteamRecovery::prepared.targetSelection;
 assert(SleepSteamRecovery::prepared.game.pid==selected.pid&&evidence["jointBindingVerified"]==true&&evidence["sharedCandidateMatched"]==true);
 assert(evidence["ymccPidValidation"]["exactProcessIdentity"]==true&&evidence["ymccPidValidation"]["workingSetBytes"]==1024ULL*1024*1024);
 assert(evidence["ymccSteamJoin"]["gameInputVerified"]==false&&evidence["ymccSteamJoin"]["continueButtonVerified"]==false);
 pass("direct binding joins the actual YMCC one-PID detector, Steam tracked PID and original OS instance, never equates linkage to gameplay/Continue");
 reset();auto game=reversed();captureWorkingSets[game.pid]=SG_MIN_WS-1;capture();evidence=SleepSteamRecovery::prepared.targetSelection;
 assert(!SleepSteamRecovery::prepared.game.pid&&evidence["reason"]=="steam-original-target-ymcc-pid-not-admitted"&&evidence["jointBindingVerified"]==false);
 assert(evidence["ymccRejectedCandidates"][0]["status"]=="rejected"&&selected.pid==14904&&steamReads==0&&inputCalls==0);
 pass("Steam row plus own window is insufficient: reverse join applies YMCC ordinary-game working-set threshold without publishing a shared candidate");
 reset();game=reversed();captureWorkingSets[game.pid]=SG_CAPTURE_MIN_WS;captureWhitelist.push_back(L"tmnt");capture();evidence=SleepSteamRecovery::prepared.targetSelection;
 assert(SleepSteamRecovery::prepared.game.pid==game.pid&&evidence["jointBindingVerified"]==true&&evidence["sharedCandidateMatched"]==false&&evidence["ymccPidValidation"]["whitelisted"]==true);
 reset();game=reversed();captureWorkingSets[game.pid]=SG_CAPTURE_MIN_WS-1;captureWhitelist.push_back(L"tmnt");capture();assert(!SleepSteamRecovery::prepared.game.pid);
 pass("reverse join honors the same production YMCC whitelist threshold including its lower bound, not Steam presence alone");
 reset();game=reversed();captureWorkingSets[game.pid]=0;captureConfigured.push_back(L"tmnt");capture();evidence=SleepSteamRecovery::prepared.targetSelection;
 assert(SleepSteamRecovery::prepared.game.pid==game.pid&&evidence["ymccPidValidation"]["customConfigured"]==true&&evidence["jointBindingVerified"]==true);
 pass("configured-game exception is inherited from the actual YMCC one-PID implementation rather than silently replaced by Steam-only heuristics");
 for(int mode=0;mode<3;++mode){reset();game=reversed();if(mode==0)captureVmReadDeniedPids.insert(game.pid);if(mode==1)captureImageFailsPids.insert(game.pid);if(mode==2)captureMemoryFailsPids.insert(game.pid);
  capture();assert(!SleepSteamRecovery::prepared.game.pid&&SleepSteamRecovery::prepared.targetSelection["jointBindingVerified"]==false&&inputCalls==0&&scopeCalls==0);
 }
 pass("inaccessible detector process/name/working-set evidence fails the join even when Steam tracking and the separate OS identity reader succeed");
 reset();game=reversed();onOnePidMemory=[](DWORD){++g_gameRulesEpoch;};capture();
 assert(!SleepSteamRecovery::prepared.game.pid&&SleepSteamRecovery::prepared.targetSelection["reason"]=="steam-capture-ymcc-rules-changed");
 pass("game-rule epoch changes during the join cannot use mixed old/new YMCC recognition policy to authorize Steam repair");
 reset();game=reversed();onOnePidMemory=[game](DWORD pid){if(pid==game.pid)captureProcesses[pid].processCreated++;};capture();
 assert(!SleepSteamRecovery::prepared.game.pid&&SleepSteamRecovery::prepared.targetSelection["ymccRejectedCandidates"][0]["status"]=="identity-mismatch");
 pass("PID reuse between Steam/OS discovery and YMCC handle validation is refused despite identical executable path and PID");
 reset();game=reversed();onOnePidMemory=[game](DWORD pid){if(pid==game.pid)tick+=1001;};capture();
 assert(!SleepSteamRecovery::prepared.game.pid&&SleepSteamRecovery::prepared.targetSelection["reason"]=="steam-capture-read-budget-exceeded");
 pass("additional directed YMCC recognition stays inside the pre-sleep soft time budget and cannot authorize a late partial join");
 reset();captureWorkingSets[selected.pid]=SG_MIN_WS-1;capture();
 assert(!SleepSteamRecovery::prepared.game.pid&&SleepSteamRecovery::prepared.targetSelection["reason"]=="steam-original-target-ymcc-pid-not-admitted");
 pass("a stale permissive shared candidate does not bypass the common detector on the preferred-PID branch");
 }
 // R28: rejection diagnostics must never turn the follow-up observation
 // into admission or silently adopt a different live instance.
 {
 auto capture=[](){SleepRecoveryAuto::set({{"enabled",true}});sleepRecoveryAutoPrepareSuspend();};
 auto reversed=[](){auto game=selected;captureProcesses[game.pid]=game;captureWindowPids.insert(game.pid);selected.pid=0;return game;};
 const auto sample=[](){return SleepSteamRecovery::prepared.targetSelection["ymccRejectedCandidates"][0]["rejectionObservation"];};
 reset();auto game=reversed();captureWorkingSets[game.pid]=SG_MIN_WS-1;capture();auto observed=sample();
 assert(observed["reason"]=="working-set-below-current-rule-minimum"&&observed["workingSetBytes"]==SG_MIN_WS-1&&observed["minimumWorkingSetBytes"]==SG_MIN_WS);
 assert(observed["identityMatched"]==true&&observed["authoritative"]==false&&observed["admissionGranted"]==false&&!SleepSteamRecovery::prepared.game.pid);
 const auto rejected=SleepSteamRecovery::prepared.targetSelection["ymccRejectedCandidates"][0];
 assert(rejected["appid"]==2399932135U&&rejected["steamTrackerStatus"]=="tracked"&&rejected["ownWindowObserved"]==true&&rejected["sharedCandidateStatus"]=="not-available");
 pass("exact tracked own-window game denied by the actual shared detector records observed threshold and preserves zero repair admission");
 reset();game=reversed();captureMemoryFailsPids.insert(game.pid);capture();observed=sample();
 assert(observed["reason"]=="working-set-query-unavailable"&&observed["workingSetKnown"]==false&&observed["workingSetBytes"].is_null()&&!SleepSteamRecovery::prepared.game.pid);
 pass("unavailable working-set reads are reported separately from a measured zero or low working set");
 reset();game=reversed();captureVmReadDeniedPids.insert(game.pid);capture();observed=sample();
 assert(observed["reason"]=="process-query-denied-or-unavailable"&&!SleepSteamRecovery::prepared.game.pid&&inputCalls==0&&scopeCalls==0);
 pass("Steam diagnostic reports VM_READ denial without weakening directed YMCC validation or issuing repair writes");
 reset();game=reversed();captureImageFailsPids.insert(game.pid);capture();observed=sample();
 assert(observed["reason"]=="process-identity-unavailable"&&observed["workingSetBytes"].is_null());
 pass("unknown handle image cannot attribute even a readable working set to the original game");
 reset();game=reversed();captureWorkingSets[game.pid]=SG_MIN_WS-1;unsigned reads=0;
 onOnePidMemory=[&](DWORD pid){if(pid==game.pid&&++reads==2)captureWorkingSets[pid]=SG_MIN_WS;};capture();observed=sample();
 assert(observed["reason"]=="no-blocker-observed-in-followup"&&observed["admissionGranted"]==false&&!SleepSteamRecovery::prepared.game.pid&&inputCalls==0&&scopeCalls==0);
 pass("a later memory sample meeting the threshold cannot erase the real detector rejection or authorize recovery");
 reset();game=reversed();captureWorkingSets[game.pid]=SG_MIN_WS-1;reads=0;
 onOnePidMemory=[&](DWORD pid){if(pid==game.pid&&++reads==2)captureProcesses[pid].processCreated++;};capture();observed=sample();
 assert(observed["reason"]=="process-instance-changed-or-unavailable"&&observed["workingSetBytes"].is_null()&&!SleepSteamRecovery::prepared.game.pid);
 pass("PID reuse during rejection sampling never reports the replacement's memory as the original game");
 reset();game=reversed();captureWhitelist.push_back(L"tmnt");captureWorkingSets[game.pid]=SG_CAPTURE_MIN_WS-1;capture();observed=sample();
 assert(observed["whitelisted"]==true&&observed["minimumWorkingSetBytes"]==SG_CAPTURE_MIN_WS&&observed["reason"]=="working-set-below-current-rule-minimum");
 pass("rejection observation derives the whitelist working-set threshold from unchanged shared constants");
 reset();captureWorkingSets[selected.pid]=SG_MIN_WS-1;capture();
 const auto preferredEvidence=SleepSteamRecovery::prepared.targetSelection["ymccPidValidation"];
 assert(preferredEvidence["sharedCandidateIdentityMatched"]==true&&preferredEvidence["sharedCandidateStatus"]=="same-instance"&&preferredEvidence["rejectionObservation"]["admissionGranted"]==false);
 pass("preferred-PID rejection preserves shared-candidate agreement separately from successful joint admission");
 }
 // R29: early stability must not hide a later loss inside the existing
 // sixty-second wake budget. These exercise the production owner and guards.
 {
 auto stableToThirty=[](){
   reset();running=true;inputContextAvailable=true;steamFocusedApp=2399932135U;begin();job();
   for(tick=3000;tick<=30000;tick+=1500){sleepRecoveryAutoTick();if(!jobs.empty())job();}
   tick=30000;assert(g_sleepRecoveryAutoPending&&inputCalls==0&&scopeCalls==0);
 };
 auto sampleAt=[](ULONGLONG at){tick=at;sleepRecoveryAutoTick();assert(jobs.size()==1);job();};
 stableToThirty();complete(false);auto healthy=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(healthy["phase"]=="recorded"&&healthy["settling"]["lastElapsedMs"]>=58500&&healthy["settling"]["lastElapsedMs"]<60000&&inputCalls==0&&scopeCalls==0);
 running=false;tick=61000;sleepRecoveryAutoTick();assert(jobs.empty()&&inputCalls==0&&scopeCalls==0);
 pass("early healthy context is observed until the last poll interval before sixty seconds, with no writes when intact or new authority after the deadline");
 stableToThirty();steamFocusedApp=413080;sampleAt(35000);
 assert(inputCalls==1&&scopeCalls==0&&SleepSteamRecovery::captured.game.pid==12932&&g_sleepRecoveryAutoPending);
 complete(false);auto focusLoss=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(focusLoss["phase"]=="recorded"&&focusLoss["settling"]["lastElapsedMs"]>=58500&&inputCalls==1&&focusLoss["after"]["game"]["pid"]==12932);
 pass("Steam input focus drifting at thirty-five seconds still receives the existing one original-PID recovery instead of being ignored after early settlement");
 stableToThirty();running=false;sampleAt(48000);assert(scopeCalls==1&&g_sleepRecoveryAutoPending);
 complete(false);auto listLoss=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(listLoss["phase"]=="recorded"&&listLoss["associationHistory"]["firstLossAfterCurrentElapsedMs"]==48000&&listLoss["after"]["game"]["pid"]==12932&&scopeCalls==1);
 pass("a genuine running-list loss at forty-eight seconds is logged and reconciled to the same game before the unchanged deadline");
 stableToThirty();controllersObserved=false;sampleAt(35000);complete(false);auto controllerLoss=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(controllerLoss["phase"]=="blocked"&&controllerLoss["reason"]=="steam-controller-reconnect-not-observed"&&scopeCalls==0&&!controllerLoss["healthVerified"].get<bool>());
 pass("controller absence appearing after thirty seconds prevents a stale stable receipt from being reported as current");
 stableToThirty();selected.processCreated++;sampleAt(35000);
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="blocked"&&inputCalls==0&&scopeCalls==0);
 pass("late PID reuse is refused even after an initially stable original-instance observation");
 stableToThirty();steamCreated++;sampleAt(35000);
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="blocked"&&sessionDiscoveryCalls==0&&inputCalls==0&&scopeCalls==0);
 pass("a new Steam birth after early stability cannot be adopted or rebound during the extended observation");
 stableToThirty();tick=45000;SleepRecoveryAuto::set({{"enabled",false}});sleepRecoveryAutoTick();
 assert(!g_sleepRecoveryAutoPending&&jobs.empty()&&inputCalls==0&&scopeCalls==0);
 pass("user disabling the single setting at forty-five seconds cancels the late observation without actions");
 stableToThirty();generation=8;g_powerLifecycle=PowerLifecycle::Suspending;tick=40000;sleepRecoveryAutoTick();
 assert(!g_sleepRecoveryAutoPending&&jobs.empty()&&inputCalls==0&&scopeCalls==0);
 pass("a new sleep generation supersedes the late observation and never repairs the preceding wake");
 }
 // R25 real-time kernel freshness: the 21:38 Wukong cycle admitted a
 // 20:29:56 exit at20:45. Its borrowed proof cannot spend this wake's budget.
 {
 auto prepare=[](){modernStandby=true;SleepRecoveryAuto::set({{"enabled",true}});g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoPrepareSuspend();g_powerLifecycle=PowerLifecycle::Suspending;};
 reset();running=true;inputContextAvailable=true;prepare();tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(7);
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.seenGeneration==0);
 tick=16*60*1000;generation=8;sleepRecoveryAutoCaptureSuspend(8);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoArm(8);
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.seenGeneration==0&&SleepRecoveryAuto::get()["phase"]=="wake-proof-wait"&&inputCalls==0);
 assert(SleepRecoveryAuto::state.wakeAdmission["proofFresh"]==false&&SleepRecoveryAuto::state.wakeAdmission["proofMaximumAgeMs"]==60000);
 tick+=1000;sleepRecoveryAutoRecordKernelWake(8,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(8);assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.armed==tick);
 tick+=1500;sleepRecoveryAutoTick();job();assert(inputCalls==1);complete(false);
 pass("16-minute-old exit cannot follow prepared boundary into another capture or consume seen-generation; actual fresh final wake starts the real budget once");
 reset();running=true;inputContextAvailable=true;prepare();tick=1000;auto stale=sgNowFileTime();tick=62000;
 sleepRecoveryAutoRecordKernelWake(7,507,1,-1,stale);assert(SleepRecoveryAuto::kernelWake.eventFileTime==0&&!g_sleepRecoveryAutoPending);
 pass("already-stale kernel log delivery cannot create a raw wake candidate, irrespective of collection-time freshness");
 reset();running=true;inputContextAvailable=true;prepare();tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());
 tick=1200;sleepRecoveryAutoRecordKernelWake(7,506,1,-1,sgNowFileTime());generation=8;sleepRecoveryAutoCaptureSuspend(8);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoArm(8);
 assert(!g_sleepRecoveryAutoPending&&inputCalls==0&&SleepRecoveryAuto::state.wakeAdmission["proofFresh"]==false);
 tick=1500;sleepRecoveryAutoRecordKernelWake(8,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(8);assert(g_sleepRecoveryAutoPending);
 pass("a newer actual Kernel506 entry invalidates an earlier button-exit candidate even inside60s and under legitimate prepared-generation transfer");
 reset();running=true;inputContextAvailable=true;prepare();generation=8;tick=1200;sleepRecoveryAutoCaptureSuspend(8);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;
 tick=1500;sleepRecoveryAutoRecordKernelWake(8,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(8);assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.seenGeneration==8);
 tick=1700;sleepRecoveryAutoRecordKernelWake(8,506,1,-1,sgNowFileTime());sleepRecoveryAutoTick();
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.seenGeneration==0&&SleepRecoveryAuto::get()["phase"]=="wake-proof-wait"&&inputCalls==0&&scopeCalls==0);
 tick=2000;sleepRecoveryAutoRecordKernelWake(8,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(8);assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.armed==2000);
 tick=3500;sleepRecoveryAutoTick();job();assert(inputCalls==1);complete(false);
 pass("late entry event may withdraw a proof-only armed cycle with zero dispatches, restoring admission for the genuinely later final wake");
 reset();running=true;inputContextAvailable=true;prepare();generation=8;tick=1200;sleepRecoveryAutoCaptureSuspend(8);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;
 tick=1500;sleepRecoveryAutoRecordKernelWake(8,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(8);
 tick=3000;sleepRecoveryAutoTick();assert(!jobs.empty());tick=3100;sleepRecoveryAutoRecordKernelWake(8,506,1,-1,sgNowFileTime());job();assert(inputCalls==0);
 sleepRecoveryAutoTick();assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="wake-proof-wait");
 pass("a newer entry racing a queued worker revokes its current/admission guard before any Steam mutation without changing shared power state");
 reset();running=true;inputContextAvailable=true;prepare();generation=8;tick=1200;sleepRecoveryAutoCaptureSuspend(8);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;
 tick=1500;sleepRecoveryAutoRecordKernelWake(8,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(8);tick=3000;sleepRecoveryAutoTick();job();assert(inputCalls==1);
 tick=3100;sleepRecoveryAutoRecordKernelWake(8,506,1,-1,sgNowFileTime());sleepRecoveryAutoTick();
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["reason"]=="steam-new-entry-after-action-no-replay"&&SleepRecoveryAuto::state.seenGeneration==8);
 tick=4000;sleepRecoveryAutoRecordKernelWake(8,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(8);assert(!g_sleepRecoveryAutoPending&&inputCalls==1&&scopeCalls==0);
 pass("new entry after any completed RETURN never refunds seen-generation or replays an action under uncertain shared-generation ownership");
 reset();running=true;inputContextAvailable=true;prepare();tick=2000;const auto finalEvent=sgNowFileTime();sleepRecoveryAutoRecordKernelWake(7,507,1,-1,finalEvent);
 sleepRecoveryAutoRecordKernelWake(7,506,1,-1,finalEvent-1000000);generation=8;sleepRecoveryAutoCaptureSuspend(8);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;sleepRecoveryAutoArm(8);
 assert(g_sleepRecoveryAutoPending&&SleepRecoveryAuto::state.wakeAdmission["proofFresh"]==true);
 pass("late collection of an older Kernel506 cannot invalidate a genuinely later final wake: real event timestamp, not receipt order, is authoritative");
 reset();running=true;inputContextAvailable=true;prepare();tick=1000;sleepRecoveryAutoRecordKernelWake(7,506,1,-1,0);sleepRecoveryAutoRecordKernelWake(7,506,1,-1,sgNowFileTime()+10000);
 assert(SleepRecoveryAuto::kernelSleepEntry.eventFileTime==0&&SleepRecoveryAuto::state.seenGeneration==0&&!g_sleepRecoveryAutoPending);
 sleepRecoveryAutoRecordKernelWake(7,506,1,-1,sgNowFileTime());auto entry=SleepRecoveryAuto::kernelSleepEntry.eventFileTime;
 sleepRecoveryAutoRecordKernelWake(7,506,1,-1,entry-10000);assert(SleepRecoveryAuto::kernelSleepEntry.eventFileTime==entry&&jobs.empty()&&inputCalls==0);
 pass("missing/future/older duplicate entry observations cannot arm repair, move the final-wake budget or overwrite latest true entry");
 }


 // R26 ROG field shape: correctly joined target, final Kernel507 proof,
 // shared lifecycle stuck Resuming, zero automatic dispatches in R25.
 {
 const auto rog=json::parse(readFile(argv[7]));unsigned joins=0,blocked=0;
 for(const auto& fact:rog["facts"]){if(fact["event"]=="sleep-steam-original-captured"){
   const auto& join=fact["details"]["targetSelection"];assert(join["jointBindingVerified"]==true&&join["sharedCandidateMatched"]==true);++joins;
  }else if(fact["event"]=="sleep-recovery-automatic-result"){assert(fact["details"]["reason"]=="steam-power-not-ready");++blocked;}}
 assert(joins==3&&blocked==2&&rog["automaticAttempts"]["inputDispatches"]==0&&rog["automaticAttempts"]["list"]==0);
 assert(rog["powerObservations"].back()["powerLifecycle"]=="resuming"&&rog["fieldInputVerified"]==false);
 pass("actual ROG R25 field evidence proves joint-PID capture but two zero-dispatch shared-Ready timeouts, not an executed Steam repair failure");
 reset();modernStandby=true;running=true;inputContextAvailable=true;foregroundPid=steamPid;
 SleepRecoveryAuto::set({{"enabled",true}});g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);while(!jobs.empty())job();
 tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());g_powerLifecycle=PowerLifecycle::Resuming;sleepRecoveryAutoArm(7);
 tick=2500;sleepRecoveryAutoTick();assert(jobs.size()==1);job();assert(inputCalls==1&&scopeCalls==0&&g_powerLifecycle==PowerLifecycle::Resuming);
 complete(false);assert(SleepRecoveryAuto::get()["phase"]=="recorded"&&inputCalls==1&&g_powerLifecycle==PowerLifecycle::Resuming);
 pass("bound final Kernel wake authorizes Steam-only original-game repair while shared lifecycle remains Resuming, with no shared power write or action replay");
 reset();modernStandby=true;running=false;inputContextAvailable=true;foregroundPid=steamPid;
 SleepRecoveryAuto::set({{"enabled",true}});g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);while(!jobs.empty())job();
 tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());g_powerLifecycle=PowerLifecycle::Resuming;sleepRecoveryAutoArm(7);
 tick=2500;sleepRecoveryAutoTick();assert(jobs.size()==1);job();assert(scopeCalls==1&&inputCalls==1&&g_powerLifecycle==PowerLifecycle::Resuming);
 complete(false);auto rescopeDuringResume=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(rescopeDuringResume["attempts"]["list"]==1&&rescopeDuringResume["attempts"]["input"]==1&&rescopeDuringResume["wakeReadiness"]["sharedReady"]==false);
 assert(!rescopeDuringResume["healthVerified"].get<bool>()&&!rescopeDuringResume["postReturnObservation"]["continueButtonVerified"].get<bool>());
 assert(rescopeDuringResume["after"]["continueRecovery"]["candidate"]==true&&rescopeDuringResume["after"]["continueRecovery"]["status"]=="runtime-running-unverified");
 pass("non-Steam missing running-list membership can invoke Valve native rescope and one original-input return while shared lifecycle is Resuming, without claiming Continue");
 }

 // R26 negative/TOCTOU guards: alternative readiness is NOT a blanket
 // permission to act during Resuming or ignore original game/client/UI state.
 {
 auto prepare=[](){modernStandby=true;running=true;inputContextAvailable=true;foregroundPid=steamPid;
  SleepRecoveryAuto::set({{"enabled",true}});g_powerLifecycle=PowerLifecycle::Suspending;sleepRecoveryAutoCaptureSuspend(7);while(!jobs.empty())job();};
 auto admit=[](int id=507){tick=1000;sleepRecoveryAutoRecordKernelWake(7,id,1,id==107?5:-1,sgNowFileTime());g_powerLifecycle=PowerLifecycle::Resuming;sleepRecoveryAutoArm(7);};
 auto start=[](){tick+=1500;sleepRecoveryAutoTick();assert(jobs.size()==1);job();};
 reset();prepare();admit();assert(sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));start();complete(false);
 auto saved=json::parse(readFile(SleepRecoveryAuto::reportPath()));
 assert(saved["implementation"]=="steam-wake-round50-native-record-diagnostic"&&saved["wakeReadiness"]["basis"]=="bound-final-kernel-wake-during-resuming");
 assert(saved["wakeReadiness"]["sharedReady"]==false&&saved["wakeReadiness"]["sharedStateModified"]==false&&saved["wakeReadiness"]["inputDeviceGateUsed"]==false);
 assert(std::any_of(facts.begin(),facts.end(),[](const auto& f){return f.first=="sleep-recovery-automatic-result"&&f.second.contains("wakeReadiness")&&f.second.contains("attempts");}));
 assert(saved["after"]["game"]["pid"]==selected.pid&&saved["after"]["originalGameRecovery"]["jointBindingVerified"]==true&&saved["after"]["originalGameRecovery"]["gameInputVerified"]==false);
 assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load())&&g_powerLifecycle==PowerLifecycle::Resuming);
 SleepRecoveryAuto::exportCurrentSnapshot();auto exported=json::parse(readFile(SG_DIR+L"\\wake-recovery-current.json"));
 assert(exported["wakeReadiness"]["basis"]==saved["wakeReadiness"]["basis"]&&exported["attempts"]["input"]==1);
 pass("automatic result/current export retain true shared-Resuming/Steam-only wake basis and joint PID evidence without claiming device admission or gameplay success");
 reset();prepare();tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());g_powerLifecycle=PowerLifecycle::Resuming;
 assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load())&&!g_sleepRecoveryAutoPending&&jobs.empty());
 sleepRecoveryAutoArm(7);assert(sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));
 pass("raw Kernel witness cannot authorize Resuming operation without explicit correlated arm and its native UI-wake request");
 for(int reason:{7,8,28,16777220}){reset();prepare();tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,reason,-1,sgNowFileTime());g_powerLifecycle=PowerLifecycle::Resuming;sleepRecoveryAutoArm(7);
  tick=2500;sleepRecoveryAutoTick();assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load())&&!g_sleepRecoveryAutoPending&&jobs.empty()&&inputCalls==0);}
 pass("non-user/device/entry-failure exits never gain Steam Resuming readiness from generic PBT or unqualified Kernel exit");
 for(auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended}){reset();prepare();admit();g_powerLifecycle=phase;
  assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));tick=2500;sleepRecoveryAutoTick();assert(jobs.empty()&&inputCalls==0&&scopeCalls==0);}
 pass("valid final proof cannot bypass an actually Suspending/Suspended shared lifecycle");
 reset();prepare();admit();retry=true;assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));tick=2500;sleepRecoveryAutoTick();assert(jobs.empty()&&!g_sleepRecoveryAutoPending&&inputCalls==0);
 pass("active sleep retry revokes the Steam-only alternative without canceling or changing the shared retry transaction");
 for(int mode=0;mode<6;++mode){reset();prepare();admit();
  if(mode==0)SleepRecoveryAuto::kernelWake.configEpoch++;
  if(mode==1)SleepRecoveryAuto::kernelWake.eventFileTime++;
  if(mode==2)SleepRecoveryAuto::kernelWake.uiWakeRequested=false;
  if(mode==3)SleepRecoveryAuto::kernelWake.requestGeneration++;
  if(mode==4)SleepRecoveryAuto::kernelWake.boundaryFileTime++;
  if(mode==5)SleepRecoveryAuto::kernelWake.eventId=506;
  assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));tick=2500;sleepRecoveryAutoTick();assert(jobs.empty()&&inputCalls==0);
 }
 pass("exported admission booleans alone cannot substitute for the native witness: epoch/event/request/boundary/kind mismatch refuses Resuming operation");
 reset();prepare();admit();SleepRecoveryAuto::state.wakeAdmission["source"]="pbt-resume-suspend-s3";
 assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));tick=2500;sleepRecoveryAutoTick();assert(jobs.empty()&&inputCalls==0);
 reset();prepare();admit();SleepRecoveryAuto::state.wakeAdmission["boundaryFileTime"]="0";assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));
 pass("S3/PBT-only or missing boundary credentials do not borrow the stronger final-Kernel Resuming route");
 reset();prepare();admit();SleepRecoveryAuto::state.wakeAdmission["eventFileTime"]=SleepRecoveryAuto::state.wakeAdmission["boundaryFileTime"];
 assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));
 reset();prepare();admit();fileTimeBase+=61000ULL*10000;assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));
 reset();prepare();admit();tick=61000;assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));
 reset();prepare();admit();tick=999;assert(!sleepSteamWakeOperationAllows(7,g_sleepRecoveryAutoConfigEpoch.load()));
 pass("pre-boundary/equal/stale native clock proof or exhausted/rollback monotonic budget cannot authorize a Resuming action");
 reset();prepare();admit();tick=2500;sleepRecoveryAutoTick();assert(jobs.size()==1);generation=8;job();assert(inputCalls==0&&scopeCalls==0);
 reset();prepare();admit();tick=2500;sleepRecoveryAutoTick();SleepRecoveryAuto::set({{"enabled",false}});job();assert(inputCalls==0&&scopeCalls==0);
 pass("new power generation or user disable cancels a queued Resuming Steam worker before any mutation");
 reset();prepare();admit();tick=2500;sleepRecoveryAutoTick();auto reads=steamReads;
 tick=2600;sleepRecoveryAutoRecordKernelWake(7,506,1,-1,sgNowFileTime());job();assert(steamReads==reads&&inputCalls==0);
 sleepRecoveryAutoTick();assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["phase"]=="wake-proof-wait");
 pass("new native entry invalidates a queued Resuming worker before even its first CDP read, retaining the zero-dispatch fresh-wake refund");
 reset();prepare();admit();onSteamCall=[](const std::string& op){if(op=="sleep.input-resume"){++tick;sleepRecoveryAutoRecordKernelWake(7,506,1,-1,sgNowFileTime());}};
 start();assert(inputCalls==0&&SleepRecoveryAuto::state.inputAttempted);sleepRecoveryAutoTick();
 assert(!g_sleepRecoveryAutoPending&&SleepRecoveryAuto::get()["reason"]=="steam-new-entry-after-action-no-replay"&&SleepRecoveryAuto::state.seenGeneration==7);
 pass("new entry between original-game inspection and RETURN native preflight prevents the mutation and never refunds an already-reserved action");
 reset();prepare();admit();owner={selected.pid,selected.processCreated,true};start();
 assert(inputCalls==0&&scopeCalls==0&&SleepRecoveryAuto::get()["phase"]=="game-wait");
 tick+=1500;sleepRecoveryAutoTick();assert(jobs.empty()&&owner.markerOwned);
 owner={};gamePaused=false;tick+=1500;sleepRecoveryAutoTick();assert(jobs.size()==1);job();assert(inputCalls==1&&g_powerLifecycle==PowerLifecycle::Resuming);
 pass("bound final wake does not release the exact original game's sleep lease; Steam waits until its existing owner genuinely releases it");
 reset();prepare();admit();gamePaused=true;start();assert(inputCalls==0&&scopeCalls==0);
 pass("manual pause ownership stays protected even with positive alternative Steam wake readiness");
 reset();prepare();admit();inputInteractiveIdle=false;start();assert(inputCalls==0&&scopeCalls==0&&g_sleepRecoveryAutoPending);
 inputInteractiveIdle=true;tick+=1500;sleepRecoveryAutoTick();assert(jobs.size()==1);job();assert(inputCalls==1);
 pass("interactive Steam UI protection remains before the first Resuming RETURN and closing the menu permits only one normal original-instance action");
 reset();prepare();admit();onSteamCall=[](const std::string& op){if(op=="sleep.inspect")++steamAccount;};start();assert(inputCalls==0&&scopeCalls==0);
 assert(SleepRecoveryAuto::get()["phase"]=="blocked"&&SleepRecoveryAuto::get()["reason"]=="steam-original-session-changed"&&inputCalls==0&&scopeCalls==0);
 reset();prepare();admit();steamReadFailure="steam-account-changed";start();assert(inputCalls==0&&scopeCalls==0&&SleepRecoveryAuto::get()["reason"]=="steam-live-account-changed");
 pass("Steam account/session change remains a hard rejection in the Resuming path, including the ROG pre-sleep account-changed symptom");
 reset();prepare();admit();onSteamCall=[](const std::string& op){if(op=="sleep.input-resume")g_powerLifecycle=PowerLifecycle::Suspending;};start();assert(inputCalls==0&&scopeCalls==0);
 pass("actual new shared suspend racing the native RETURN preflight revokes the alternative immediately");
 reset();prepare();admit();onReturn=[](){g_powerLifecycle=PowerLifecycle::Suspended;};start();assert(inputCalls==1);
 sleepRecoveryAutoTick();assert(!g_sleepRecoveryAutoPending&&inputCalls==1&&scopeCalls==0);
 pass("new suspend immediately after a RETURN prevents confirmation and keeps the single-action reservation, never replaying on uncertainty");
 reset();prepare();admit(107);start();assert(inputCalls==1&&SleepRecoveryAuto::state.wakeAdmission["source"]=="kernel-power-107-target-5"&&g_powerLifecycle==PowerLifecycle::Resuming);
 pass("exact hibernate Kernel107/Target5 receipt supports the same bounded Resuming operation without a fictitious S0 Kernel507");
 reset();modernStandby=true;running=true;inputContextAvailable=true;foregroundPid=steamPid;SleepRecoveryAuto::set({{"enabled",true}});
 sleepRecoveryAutoPrepareSuspend();g_powerLifecycle=PowerLifecycle::Suspending;tick=1000;sleepRecoveryAutoRecordKernelWake(7,507,1,-1,sgNowFileTime());sleepRecoveryAutoArm(7);
 generation=8;tick=2000;sleepRecoveryAutoCaptureSuspend(8);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Resuming;
 assert(SleepRecoveryAuto::state.wakeAdmission["proofFollowedPreparedBoundary"]==true&&sleepSteamWakeOperationAllows(8,g_sleepRecoveryAutoConfigEpoch.load()));
 tick=3500;sleepRecoveryAutoTick();assert(jobs.size()==1);job();assert(inputCalls==1&&lastInputRequest["originalTarget"]["pid"]==selected.pid);
 pass("only the existing exact prepared-boundary gen+1 transfer can carry the original native proof into the Resuming route");
 reset();prepare();admit();tick=2500;sleepRecoveryAutoTick();tick=61001;job();sleepRecoveryAutoTick();assert(inputCalls==0&&scopeCalls==0&&!g_sleepRecoveryAutoPending);
 pass("queued worker delayed beyond the unchanged sixty-second final-wake observation budget performs no late CDP mutation");
 }

 // R27: execute the production main.cpp request wrapper, not a mock account gate.
 reset();running=true;inputContextAvailable=true;registrySteamPid=selected.pid;registryAccountOverride=99;
 begin();assert(SleepSteamRecovery::captured.client.account==42&&SleepSteamRecovery::captured.client.liveAccountPinned&&sessionDiscoveryCalls==1);
 job();assert(inputCalls==1&&scopeCalls==0&&lastInputRequest["sleepSessionProcess"]["pid"]==steamPid&&lastInputRequest["account"]==42);
 assert(SleepSteamRecovery::captured.client.foreignRegistryAccountIgnored&&!SleepRecoveryAuto::state.receipt.value("healthVerified",false));
 pass("foreign game registry PID/account and corrupt observer account bind only the verified Steam UI account; actual wrapper admits original-game resume");
 reset();registrySteamPid=selected.pid;registryAccountOverride=99;SleepRecoveryAuto::set({{"enabled",true}});
 SleepSteamRecovery::prepareSuspend();const auto pinned=SleepSteamRecovery::prepared.client;assert(pinned.account==42&&sessionDiscoveryCalls==1);
 steamAccount=43;SleepSteamRecovery::prepareSuspend();g_powerLifecycle=PowerLifecycle::Suspending;SleepSteamRecovery::captureSuspend(7);
 SleepSteamRecovery::captureSuspend(7);assert(sessionDiscoveryCalls==1&&SleepSteamRecovery::captured.client.account==42&&SleepSteamRecovery::liveAccountSession.account==42);
 while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Resuming;
 auto pinnedRead=SleepSteamRecovery::inspect(7);assert(!pinnedRead["steam"]["ok"].get<bool>()&&inputCalls==0&&scopeCalls==0&&sessionDiscoveryCalls==1);
 pass("duplicate preparation and queued capture preserve the earliest account; actual UI account change is rejected without wake discovery");
 for(int mode=0;mode<9;++mode){reset();registrySteamPid=selected.pid;registryAccountOverride=99;
   auto client=SleepSteamRecovery::prepareSession();assert(client.account==42&&sessionDiscoveryCalls==1);
   json witness={{"pid",client.pid},{"created",std::to_string(client.created)},{"root",W2U(client.root)}};
   if(mode==0)witness["pid"]=selected.pid;
   if(mode==1)witness["created"]=std::to_string(client.created+1);
   if(mode==2)witness["root"]=W2U(client.root+L"-wrong");
   if(mode==3)steamCreated++;
   if(mode==4)steamPid++;
   if(mode==5)steamAccount=43;
   if(mode==6)steamProcessAvailable=false;
   if(mode==7)witness["created"]="+"+std::to_string(client.created);
   if(mode==8)witness["pid"]=0U;
   auto denied=steamLiveRequest(42,{{"operation","sleep.inspect"},{"appids",json::array()},{"sleepSessionProcess",witness}});
   assert(!denied.value("ok",true)&&!denied.value("mutated",true)&&inputCalls==0&&scopeCalls==0&&sessionDiscoveryCalls==1);
 }
 pass("production wrapper rejects foreign PID, wrong birth/root, restart/account switch, exited Steam and malformed witness");
 for(int mode=0;mode<8;++mode){reset();registrySteamPid=selected.pid;registryAccountOverride=99;
   if(mode==0)steamAccount=0;
   if(mode==1)steamReadFailure="live-timeout";
   if(mode==2)sessionReadMalformed=true;
   if(mode==3)onSteamCall=[](const auto& op){if(op=="sleep.session.inspect")steamCreated++;};
   if(mode==4)onSteamCall=[](const auto& op){if(op=="sleep.session.inspect")registrySteamPid=steamPid;};
   if(mode==5)onSteamCall=[](const auto& op){if(op=="sleep.session.inspect")g_powerLifecycle=PowerLifecycle::Resuming;};
   if(mode==6)onSteamCall=[](const auto& op){if(op=="sleep.session.inspect")generation++;};
   if(mode==7)onSteamCall=[](const auto& op){if(op=="sleep.session.inspect")g_sleepRecoveryAutoConfigEpoch++;};
   auto failed=SleepSteamRecovery::prepareSession();assert(!SleepSteamRecovery::validSession(failed)&&!SleepSteamRecovery::liveAccountSession.liveAccountPinned);
 }
 pass("zero/mutating/unavailable UI discovery and process/registry/wake/generation/epoch swap never pin a replacement account");
 for(auto lifecycle:{PowerLifecycle::Resuming,PowerLifecycle::Suspended}){reset();registrySteamPid=selected.pid;registryAccountOverride=99;g_powerLifecycle=lifecycle;
   auto unbound=SleepSteamRecovery::prepareSession();assert(!SleepSteamRecovery::validSession(unbound)&&sessionDiscoveryCalls==0);
 }
 pass("unbound resumed or suspended client cannot discover or adopt an account");
 reset();registrySteamPid=selected.pid;registryAccountOverride=99;auto client=SleepSteamRecovery::prepareSession();g_powerLifecycle=PowerLifecycle::Resuming;
 assert(SleepSteamRecovery::prepareSession().account==42&&sessionDiscoveryCalls==1);registryAccountOverride=101;
 assert(SleepSteamRecovery::session().account==42&&SleepSteamRecovery::sameSession(client));
 auto ordinary=steamLiveRequest(42,{{"operation","overlay.get"},{"sleepSessionProcess",{{"pid",steamPid},{"created",std::to_string(steamCreated)},{"root",W2U(steamRoot)}}}});
 assert(!ordinary.value("ok",true)&&ordinary["reason"]=="steam-account-changed");
 assert(!steamLiveRequest(0,{{"operation","sleep.session.inspect"}}).value("ok",true));
 pass("pinned fallback account survives foreign registry noise; ordinary feature gates and required process witness remain enforced");
 reset();registrySteamPid=selected.pid;registryAccountOverride=99;SleepSteamRecovery::prepareSuspend();SleepSteamRecovery::captureSuspend(7);
 assert(sessionDiscoveryCalls==0&&inputCalls==0&&scopeCalls==0);pass("disabled owner performs no account discovery or recovery actions");
 reset();registrySteamPid=selected.pid;registryAccountOverride=99;auto readClient=SleepSteamRecovery::prepareSession();
 steamReadFailure="live-transport-uncertain";auto noWrite=SleepSteamRecovery::liveRequest(readClient,{{"operation","sleep.inspect"},{"appids",json::array()}});
 assert(!noWrite.value("ok",true)&&!noWrite.value("mutated",true)&&noWrite["reason"]=="live-read-unavailable");
 pass("bounded sleep account and inspection reads preserve fixed read-only transport uncertainty mapping");

 reset();running=false;inputContextAvailable=true;registrySteamPid=selected.pid;registryAccountOverride=99;SleepRecoveryAuto::set({{"enabled",true}});
 g_powerLifecycle=PowerLifecycle::Suspending;SleepSteamRecovery::captureSuspend(7);while(!jobs.empty())job();g_powerLifecycle=PowerLifecycle::Ready;
 auto boundInspection=SleepSteamRecovery::inspect(7);auto listReceipt=SleepSteamRecovery::reconcile(boundInspection,7,g_sleepRecoveryAutoConfigEpoch);
 assert(listReceipt.value("accepted",false)&&scopeCalls==1&&sessionDiscoveryCalls==1&&SleepSteamRecovery::captured.client.account==42);
 pass("foreign registry account also permits exact same-session native list rescope through the production wrapper without invented Running state");
 reset();json exactWitness={{"pid",steamPid},{"created",std::to_string(steamCreated)},{"root",W2U(steamRoot)+"/"}};
 auto normalizedRead=steamLiveRequest(42,{{"operation","sleep.inspect"},{"appids",json::array()},{"sleepSessionProcess",exactWitness}});
 assert(normalizedRead.value("ok",false)&&!normalizedRead.value("mutated",true));
 pass("normalized install-root separators keep the same verified Steam birth/image witness");

 {
  reset();std::ifstream pf{fspath::path(argv[2]).parent_path()/"sleep_steam_msi_tmnt_r31_primary_split_080143.json"};json field;pf>>field;
  selected.pid=field["sharedPreferredPid"].get<DWORD>();selected.path=U2W(field["path"].get<std::string>());selected.processCreated=std::stoull(field["sharedPreferredCreated"].get<std::string>());
  fileTimeBase=std::stoull(field["captureBoundary"].get<std::string>());
  auto sibling=selected;sibling.pid=field["nativePrimaryPid"].get<DWORD>();sibling.processCreated+=field["simulatedNewBirthOffset100ns"].get<ULONGLONG>();
  captureProcesses[selected.pid]=selected;captureProcesses[sibling.pid]=sibling;captureWindowPids.insert(sibling.pid);
  assert(sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt",field["gameprocessLog"].get<std::string>()));
  assert(sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",field["consoleLog"].get<std::string>()));
  const SleepSteamRecovery::Game preferred{selected.pid,selected.processCreated,selected.path};
  const auto choice=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,preferred);
  if(__NATIVE_PRIMARY_BASELINE__){assert(choice.game.pid==preferred.pid);pass("baseline reproduces stale tracked/shared PID surviving a newer native primary process update");}
  else{assert(choice.game.pid==sibling.pid&&choice.game.created==sibling.processCreated&&choice.evidence["jointBindingVerified"]==true&&choice.evidence["sharedCandidateMatched"]==false);pass("field-shaped primary split selects exact new native primary only after OS and YMCC admission, preserving shared disagreement");}
 }

 {
  std::ifstream pf{fspath::path(argv[2]).parent_path()/"sleep_steam_msi_tmnt_r31_primary_split_080143.json"};json field;pf>>field;
  auto setupField=[&]{
   reset();selected.pid=field["sharedPreferredPid"].get<DWORD>();selected.path=U2W(field["path"].get<std::string>());selected.processCreated=std::stoull(field["sharedPreferredCreated"].get<std::string>());
   fileTimeBase=std::stoull(field["captureBoundary"].get<std::string>());auto sibling=selected;sibling.pid=field["nativePrimaryPid"].get<DWORD>();sibling.processCreated+=field["simulatedNewBirthOffset100ns"].get<ULONGLONG>();
   captureProcesses[selected.pid]=selected;captureProcesses[sibling.pid]=sibling;captureWindowPids.insert(sibling.pid);
   assert(sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt",field["gameprocessLog"].get<std::string>()));assert(sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",field["consoleLog"].get<std::string>()));return sibling;
  };
  for(int mode=0;mode<7;++mode){auto sibling=setupField();
   if(mode==0)captureProcesses[sibling.pid].processCreated=fileTimeBase+1;
   if(mode==1)captureForeignSessionPids.insert(sibling.pid);
   if(mode==2)captureProcesses[sibling.pid].path=L"E:\\Different.exe";
   if(mode==3)captureWorkingSets[sibling.pid]=SG_MIN_WS-1;
   if(mode==4)captureVmReadDeniedPids.insert(sibling.pid);
   if(mode==5)captureIdentityUnavailablePids.insert(sibling.pid);
   if(mode==6)captureWindowPids.erase(sibling.pid);
   auto choice=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{selected.pid,selected.processCreated,selected.path});
   assert(!choice.game.pid&&!choice.evidence["jointBindingVerified"].get<bool>()&&selected.pid==11168&&inputCalls==0&&scopeCalls==0);
  }
  pass("native primary never bypasses post-boundary birth, Windows session, OS path, working set, process rights, unavailable identity or own-window checks");
  auto sibling=setupField();selected=captureProcesses[sibling.pid];
  auto agrees=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{selected.pid,selected.processCreated,selected.path});
  assert(agrees.game.pid==sibling.pid&&agrees.evidence["sharedCandidateMatched"]==true&&agrees.evidence["nativePrimary"]["status"]=="matched");
  pass("shared/native primary agreement is separately logged from the field-shaped old shared PID divergence");
  sibling=setupField();onOnePidMemory=[&](DWORD pid){if(pid==sibling.pid)sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt","["+SleepSteamRecovery::localSecond(fileTimeBase)+"] Game process updated : AppID 2399932135 \"race\", ProcID 11168, IP 0.0.0.0:0\n");};
  auto raced=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{selected.pid,selected.processCreated,selected.path});
  assert(!raced.game.pid&&raced.evidence["reason"]=="steam-capture-binding-changed");
  pass("primary update during one-PID validation invalidates final pre-sleep capture instead of publishing an outdated target");
  sibling=setupField();onOnePidMemory=[](DWORD){tick+=1001;};
  auto budget=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{selected.pid,selected.processCreated,selected.path});
  assert(!budget.game.pid&&budget.evidence["reason"]=="steam-capture-read-budget-exceeded"&&budget.evidence["captureReadBudget"]["maximumMs"]==(__CAPTURE_BUDGET_BASELINE__?150:1000)&&budget.evidence["captureReadBudget"]["elapsedMs"].get<unsigned>()>=1001);
  pass("capture budget remains bounded, reports elapsed time and refuses incomplete work beyond its configured maximum");
 }
 {
  const std::string t="[2026-10-11 10:34:57] ";
  auto added=t+"Game process added : AppID 2399932135 \"fixture\", ProcID 12932, IP 0.0.0.0:0";
  auto primary=nativePrimaryProcessInventory({added+"\r"},"2026-10-11 10:30:00","2026-10-11 10:40:00");assert(primary.complete&&primary.rows.at(2399932135U).local);
  pass("Windows CRLF native diagnostic preserves local primary evidence");
  for(auto bad:{t+"Game process updated : AppID 2399932135 \"x\", ProcID 0, IP 0.0.0.0:0",t+"Game process updated : AppID 2399932135 \"x\", ProcID 4294967296, IP 0.0.0.0:0",t+"Game process updated : AppID 2399932135 \"x\", ProcID 100",std::string("bad-time Game process updated : AppID 2399932135 \"x\", ProcID 100, IP 0.0.0.0:0")})assert(!nativePrimaryProcessInventory({bad},"2026-10-11 10:30:00","2026-10-11 10:40:00").complete);
  assert(!nativePrimaryProcessInventory({std::string(nativePrimaryMaximumBytes+1,'x')},"2026-10-11 10:30:00","2026-10-11 10:40:00").complete);
  assert(!nativePrimaryProcessInventoryText(std::string(nativePrimaryMaximumBytes+1,'x'),"2026-10-11 10:30:00","2026-10-11 10:40:00").complete);
  pass("malformed primary diagnostics, invalid PID and byte-budget overflow fail closed");
  auto dense=std::vector<std::string>(24696,"noise");dense.push_back(added);
  auto late=nativePrimaryProcessInventory(dense,"2026-10-11 10:30:00","2026-10-11 10:40:00");
  assert(late.complete&&late.rows.at(2399932135U).pid==12932&&late.rows.at(2399932135U).line==24697);
  pass("primary beyond 20000 unrelated lines is read with its original line number");
  dense.push_back(t+"Game process removed: AppID 2399932135 \"fixture\", ProcID 12932 ");
  late=nativePrimaryProcessInventory(dense,"2026-10-11 10:30:00","2026-10-11 10:40:00");assert(late.complete&&late.rows.at(2399932135U).removed);
  dense.back()=t+"Game process updated : AppID 2399932135 \"fixture\", ProcID 20000, IP 0.0.0.0:0";
  late=nativePrimaryProcessInventory(dense,"2026-10-11 10:30:00","2026-10-11 10:40:00");assert(late.complete&&late.rows.at(2399932135U).pid==20000);
  dense.back()="bad-time Game process updated : AppID 2399932135 \"fixture\", ProcID 12932, IP 0.0.0.0:0";
  assert(!nativePrimaryProcessInventory(dense,"2026-10-11 10:30:00","2026-10-11 10:40:00").complete);
  pass("late removal, replacement and malformed evidence cannot be skipped after a valid primary");
  const auto exactCap=std::string(nativePrimaryMaximumBytes-added.size()-1,'x')+"\n"+added;
  auto atCap=nativePrimaryProcessInventoryText(exactCap,"2026-10-11 10:30:00","2026-10-11 10:40:00");
  assert(atCap.complete&&atCap.rows.at(2399932135U).pid==12932);
  assert(!nativePrimaryProcessInventoryText(exactCap+"x","2026-10-11 10:30:00","2026-10-11 10:40:00").complete);
  pass("full 2MiB snapshot is accepted and one extra byte fails closed");
  reset();sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",std::string(24696,'\n')+"["+SleepSteamRecovery::localSecond(fileTimeBase)+"] Game process added : AppID 2399932135 \"fixture\", ProcID "+std::to_string(selected.pid)+", IP 0.0.0.0:0\r\n");
  auto fullRead=SleepSteamRecovery::readNativePrimary(SleepSteamRecovery::session(),fileTimeBase);
  assert(fullRead.inventory.complete&&fullRead.inventory.rows.at(2399932135U).pid==selected.pid&&fullRead.inventory.rows.at(2399932135U).line==24697);
  auto fullChoice=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{selected.pid,selected.processCreated,selected.path});
  assert(fullChoice.game.pid==selected.pid&&fullChoice.evidence["nativePrimary"]["status"]=="matched");
  pass("production read wrapper and exact pre-sleep join inspect a bounded log past the former row limit");
  assert(nativePrimaryProcessInventory({added},"2026-10-11 10:35:00","2026-10-11 10:40:00").rows.empty());
  assert(nativePrimaryProcessInventory({added},"2026-10-11 10:30:00","2026-10-11 10:34:56").rows.empty());
  pass("primary diagnostic outside verified Steam birth or snapshot boundary cannot bind the original");
  auto inventory=trackedProcessInventory({t+"AppID 10307630032478011392 adding PID 12932 as a tracked process \"\"E:\\GAME\\TMNT.exe\"\""});
  SleepSteamRecovery::NativePrimaryRead read;read.inventory=primary;
  assert(SleepSteamRecovery::nativePrimaryJoin(read,inventory,2399932135U,12932)["originalAllowed"]==true);
  for(int mode=0;mode<3;++mode){read.inventory=nativePrimaryProcessInventory({mode==0?t+"Game process removed: AppID 2399932135 \"fixture\", ProcID 12932 ":mode==1?t+"Game process updated : AppID 2399932135 \"fixture\", ProcID 12932, IP 192.168.1.2:1234":t+"Game process updated : AppID 2399932135 \"fixture\", ProcID 20000, IP 0.0.0.0:0"},"2026-10-11 10:30:00","2026-10-11 10:40:00");assert(SleepSteamRecovery::nativePrimaryJoin(read,inventory,2399932135U,12932)["originalAllowed"]==false);}
  read.inventory.complete=false;assert(SleepSteamRecovery::nativePrimaryJoin(read,inventory,2399932135U,12932)["originalAllowed"]==false);
  pass("removed, nonlocal, changed or incomplete primary proof blocks a unique surviving tracker member");
  reset();sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",std::string(2*1024*1024+1,'x'));
  auto choice=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{selected.pid,selected.processCreated,selected.path});assert(!choice.game.pid);
  pass("truncated native-primary file cannot use unique tracker fallback");
 }
 {
  auto diagnostic=[&](DWORD pid){return "["+SleepSteamRecovery::localSecond(fileTimeBase)+"] Game process updated : AppID 2399932135 \"fixture\", ProcID "+std::to_string(pid)+", IP 0.0.0.0:0\n";};
  for(int mode=0;mode<4;++mode){reset();running=true;inputContextAvailable=true;steamFocusedApp=769;sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",diagnostic(selected.pid));begin();
   const auto original=SleepSteamRecovery::captured.game;assert(original.pid==selected.pid);
   if(mode==0){sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",diagnostic(20000));auto changed=SleepSteamRecovery::inspect(7);assert(changed["steamRecovery"]["reason"]=="steam-native-primary-game-changed"&&changed["originalGameRecovery"]["jointBindingVerified"]==false);job();assert(inputCalls==0&&scopeCalls==0);}
   if(mode==1){onSteamCall=[&](const std::string& op){if(op=="sleep.input-resume")sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",diagnostic(20000));};auto inspected=SleepSteamRecovery::inspect(7);auto receipt=SleepSteamRecovery::resumeInputContext(inspected,7,g_sleepRecoveryAutoConfigEpoch);assert(inputCalls==0&&receipt["nativeAdmissionReason"]=="steam-native-primary-game-changed"&&receipt["nativePreDispatchCancelled"]==false);}
   if(mode==2){onReturn=[&]{sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",diagnostic(20000));};auto inspected=SleepSteamRecovery::inspect(7);auto receipt=SleepSteamRecovery::resumeInputContext(inspected,7,g_sleepRecoveryAutoConfigEpoch);assert(inputCalls==1&&receipt["mutated"]==true&&receipt["accepted"]==false);}
   if(mode==3){running=false;steamFocusedApp=2399932135U;onSteamCall=[&](const std::string& op){if(op=="sleep.reconcile")sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",diagnostic(20000));};job();assert(scopeCalls==0);}
   assert(SleepSteamRecovery::captured.game.pid==original.pid&&SleepSteamRecovery::captured.game.created==original.created&&selected.pid==original.pid);
  }
  pass("wake primary change blocks association, input predispatch, postflight acceptance and list dispatch without retargeting or refunding an identity veto");
 }
 {
  for(int mode=0;mode<2;++mode){reset();running=true;inputContextAvailable=true;steamFocusedApp=769;
   const auto file=steamRoot+L"\\logs\\console_log.txt";
   assert(sgWriteFileAtomic(file,"["+SleepSteamRecovery::localSecond(fileTimeBase)+"] Game process added : AppID 2399932135 \"fixture\", ProcID 12932, IP 0.0.0.0:0\n"));begin();
   assert(SleepSteamRecovery::captured.targetSelection["nativePrimary"]["nativePrimaryVerified"]==true);
   if(mode==0)assert(DeleteFileW(file.c_str()));else assert(sgWriteFileAtomic(file,"noise\n"));
   const auto lost=SleepSteamRecovery::inspect(7);assert(lost["nativePrimary"]["originalAllowed"]==false&&lost["nativePrimary"]["reason"]=="steam-native-primary-proof-lost");
   job();assert(inputCalls==0&&scopeCalls==0&&SleepSteamRecovery::captured.game.pid==12932);
  }
  pass("previously verified native primary cannot downgrade to a unique legacy tracker if the wake log disappears or loses its record");
 }
 {
  // The fixture delays a read-only seam by the FIELD-OBSERVED total172ms.
  // This tests admission, not that rules/mutex caused the measured delay.
  for(const ULONGLONG delay:{172ULL,500ULL,1000ULL}){
   reset();const auto original=selected;onCaptureRulesSnapshot=[delay](){tick+=delay;};
   auto choice=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{original.pid,original.processCreated,original.path});
   if(__CAPTURE_BUDGET_BASELINE__){assert(!choice.game.pid&&choice.evidence["reason"]=="steam-capture-read-budget-exceeded");}
   else{assert(choice.game.pid==original.pid&&choice.evidence["jointBindingVerified"]==true&&choice.evidence["captureReadBudget"]["maximumMs"]==1000&&choice.evidence["captureReadBudget"]["elapsedMs"]==delay);
    assert(choice.evidence["captureReadBudget"]["checkpoints"][0]["stage"]=="ymcc-rules-snapshot"&&choice.evidence["captureReadBudget"]["checkpoints"][0]["stageElapsedMs"]==delay);}
  }
  pass("field-measured172ms plus500ms and exact1000ms capture delay retain the same validated original within widened budget; baseline reproduces premature rejection");
  if(!__CAPTURE_BUDGET_BASELINE__){
   reset();onCaptureRulesSnapshot=[](){tick+=1001;};
   auto over=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{selected.pid,selected.processCreated,selected.path});
   assert(!over.game.pid&&over.evidence["reason"]=="steam-capture-read-budget-exceeded"&&over.evidence["captureReadBudget"]["stage"]=="ymcc-rules-snapshot"&&over.evidence["captureReadBudget"]["elapsedMs"]==1001&&inputCalls==0&&scopeCalls==0);
   pass("1001ms snapshot exceeds the whole-capture budget and grants no partial target or repair");
   reset();tick=10;onCaptureRulesSnapshot=[](){tick=9;};
   auto back=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{selected.pid,selected.processCreated,selected.path});
   assert(!back.game.pid&&back.evidence["captureReadBudget"]["clockRegressed"]==true);
   pass("a regressed capture clock remains invalid after widening the latency allowance");
   for(int mode=0;mode<4;++mode){
    reset();const auto original=selected;onCaptureRulesSnapshot=[](){tick+=172;};
    onOnePidMemory=[&,mode](DWORD pid){if(pid!=original.pid)return;if(mode==0)captureProcesses[pid]={pid,original.processCreated+1,original.path};if(mode==1)++g_gameRulesEpoch;if(mode==2)steamAccount=43;if(mode==3)steamCreated++;};
    auto invalid=SleepSteamRecovery::selectPreSleepSteamTarget(SleepSteamRecovery::session(),fileTimeBase,{original.pid,original.processCreated,original.path});
    assert(!invalid.game.pid&&invalid.evidence["jointBindingVerified"]==false&&inputCalls==0&&scopeCalls==0);
   }
   pass("172ms grace preserves PID birth, rule epoch, Steam account and Steam process birth refusal");
  }
 }

 {
  // Dense complete tracker snapshots: positive proof and late invalidation.
  const std::string prefix="[2026-10-11 10:34:57] unrelated observation";
  std::vector<std::string> dense(20001,prefix);dense.push_back(row);
  auto lookup=[&](const std::vector<std::string>& rows){return track(rows,12932,"E:\\GAME\\TMNT.exe","2026-10-11 10:34:55","2026-10-11 10:35:00");};
  auto found=lookup(dense);auto inventory=trackedProcessInventory(dense);
  assert(found.status=="tracked"&&found.line==20002&&found.exactPath&&inventory.complete&&inventory.rows.size()==1);
  pass("dense complete tracker below2MiB binds after20000 lines and retains exact original row number");
  for(int mode=0;mode<4;++mode){auto rejected=track(dense,mode==0?100:12932,mode==1?"E:\\GAME\\other.exe":"E:\\GAME\\TMNT.exe",mode==2?"2026-10-11 10:35:00":"2026-10-11 10:34:55",mode==3?"2026-10-11 10:34:56":"2026-10-11 10:35:00");assert(rejected.status!="tracked");}
  auto ambiguous=dense;ambiguous.push_back(duplicate);assert(lookup(ambiguous).status=="ambiguous");
  pass("dense tracker keeps wrong PID path birth-window and multiple-AppID refusal");
  for(const std::string end:{"[2026-10-11 10:36:00] AppID -1895035161 no longer tracking PID 12932, exit code 0","[2026-10-11 10:36:00] Remove 2399932135 from running list","[2026-10-11 10:36:00] Client version: 1"}){
   auto ended=dense;ended.push_back(end);assert(lookup(ended).status!="tracked");auto cleared=trackedProcessInventory(ended);assert(cleared.complete&&cleared.rows.empty());
  }
  pass("dense tracker consumes final untrack removal and client reset instead of trusting earlier add");
  std::vector<std::string> exact{std::string(trackerMaximumBytes-row.size()-1,'x'),row};
  assert(trackerLinesWithinBudget(exact)&&lookup(exact).status=="tracked"&&trackedProcessInventory(exact).complete);
  exact[0].push_back('x');assert(!trackerLinesWithinBudget(exact)&&lookup(exact).status=="incomplete"&&trackedProcessInventory(exact).reason=="steam-tracker-byte-limit");
  assert(trackerLinesWithinBudget({std::string(trackerMaximumBytes,'x')}));
  assert(!trackerLinesWithinBudget({std::string(trackerMaximumBytes,'x'),""}));
  assert(trackerLinesWithinBudget({std::string(trackerMaximumBytes-1,'x'),""}));
  assert(trackerLinesWithinBudget({}));
  pass("tracker exact2MiB accepted and2MiBplus1 rejected including empty-row separators");
  auto many=dense;for(unsigned pid=1;pid<=32;++pid)many.push_back("[2026-10-11 10:34:57] AppID 945360 adding PID "+std::to_string(pid)+" as a tracked process \"\"E:\\GAME\\other.exe\"\"");
  assert(!trackedProcessInventory(many).complete&&trackedProcessInventory(many).reason=="steam-tracker-live-process-limit");
  pass("dense tracker retains32-live-process discovery cap");
 }
 {
  // All five production wrappers, with filesystem/OS/CDP mocks only.
  for(int ending=0;ending<4;++ending){
   reset();running=true;inputContextAvailable=true;pidWindowExists=true;
   const auto original=selected;const auto at=SleepSteamRecovery::localSecond(original.processCreated);
   const std::string add="["+at+"] AppID 10307630032478011392 adding PID "+std::to_string(original.pid)+" as a tracked process \"\""+W2U(original.path)+"\"\"\n";
   std::string dense;for(unsigned i=0;i<20001;++i)dense+="["+at+"] unrelated observation\n";dense+=add;
   const std::string primary="["+SleepSteamRecovery::localSecond(fileTimeBase)+"] Game process updated : AppID 2399932135 \"fixture\", ProcID "+std::to_string(original.pid)+", IP 0.0.0.0:0\n";
   assert(dense.size()<trackerMaximumBytes&&sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt",dense));
   assert(sgWriteFileAtomic(steamRoot+L"\\logs\\console_log.txt",primary));
   const auto client=SleepSteamRecovery::session();const SleepSteamRecovery::Game game{original.pid,original.processCreated,original.path};
   assert(SleepSteamRecovery::nativePrimaryAllowsOriginal(client,game,2399932135U,true));
   auto choice=SleepSteamRecovery::selectPreSleepSteamTarget(client,fileTimeBase,game);assert(choice.game.pid==game.pid&&choice.evidence["jointBindingVerified"]==true);
   auto tracked=DenseTrackerRuntimeSeam::tracking(client,game);assert(tracked.complete&&tracked.binding.status=="tracked"&&tracked.binding.line==20002);
   json discovery;assert(DenseTrackerRuntimeSeam::discoverGame(client,1,discovery).pid==game.pid);
   begin();assert(SleepSteamRecovery::captured.game.pid==game.pid);
   if(ending){dense+="["+at+"] "+(ending==1?"AppID 2399932135 no longer tracking PID "+std::to_string(original.pid)+", exit code 0":ending==2?"Remove 2399932135 from running list":"Client version: 1")+"\n";assert(sgWriteFileAtomic(steamRoot+L"\\logs\\gameprocess_log.txt",dense));}
   const auto inspected=SleepSteamRecovery::inspect(7);
   assert(inspected["originalGameRecovery"]["jointBindingVerified"]==(ending==0));
   auto after=DenseTrackerRuntimeSeam::tracking(client,game);assert(after.complete&&(after.binding.status=="tracked")==!ending);
   assert(SleepSteamRecovery::nativePrimaryAllowsOriginal(client,game,2399932135U,true)==!ending);
   assert((DenseTrackerRuntimeSeam::discoverGame(client,1,discovery).pid==game.pid)==!ending);
   assert(inputCalls==0&&scopeCalls==0&&pidActivateCalls==0);
  }
  pass("all5 production tracker consumers read dense complete snapshots and late invalidation without app input scope or focus writes");
  for(int kind=0;kind<2;++kind){reset();const auto client=SleepSteamRecovery::session();const SleepSteamRecovery::Game game{selected.pid,selected.processCreated,selected.path};
   const auto file=steamRoot+L"\\logs\\gameprocess_log.txt";
   if(kind==0)assert(DeleteFileW(file.c_str()));else assert(sgWriteFileAtomic(file,std::string(trackerMaximumBytes+1,'x')));
   assert(!SleepSteamRecovery::nativePrimaryAllowsOriginal(client,game,2399932135U));
   assert(!SleepSteamRecovery::selectPreSleepSteamTarget(client,fileTimeBase,game).game.pid);
   assert(!DenseTrackerRuntimeSeam::tracking(client,game).complete);json evidence;assert(!DenseTrackerRuntimeSeam::discoverGame(client,1,evidence).pid);
   assert(inputCalls==0&&scopeCalls==0);
  }
  pass("dense fix preserves unavailable and file-truncated tracker refusal in production readers");
 }

 std::cout<<"Steam-only production-header selftest: "<<passed<<" groups PASS\n";
}
'''.replace('__COORDINATOR_COST_ONLY__','true' if args.coordinator_cost_only else 'false').replace('__LINK_DENSE_SEAM__',link_dense_seam).replace('__CAPTURE_BUDGET_BASELINE__','true' if args.expect_capture_budget_rejected else 'false').replace('__TAIL__',tail).replace('__ONE_PID__',one_pid).replace('__WS_CONSTANTS__',ws_constants).replace('__LIVE_WRAPPER__',live_wrapper).replace('__PREDISPATCH_GUARD__',cancel_guard).replace('__PREDISPATCH_BASELINE__','true' if args.expect_native_predispatch_blocked else 'false').replace('__FIELD_PREDISPATCH_ELAPSED__',str(field_predispatch['terminalElapsedMs'])).replace('__DELAYED_WINDOW_BASELINE__','1' if args.expect_delayed_window_blocked else '0').replace('__NATIVE_PRIMARY_BASELINE__','true' if args.expect_native_primary_old_target else 'false')
source=out/'sleep_steam_only_selftest.cpp';source.write_text(cpp,'utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat');exe=out/'sleep_steam_only_selftest.exe';fixtureDir=out/("owned-SleepSteamOnly-20261011-fixture-"+uuid.uuid4().hex[:12])
cmd=out/'run-steam-only.cmd'
toolchain_js='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain_prefix=subprocess.check_output(['node','--input-type=module','-e',toolchain_js,str(vs)],text=True,encoding='utf-8')
cmd.write_text(toolchain_prefix+f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{source}" /I"{root / "native"}" /I"{root / "deps/json"}" /Fo"{out / "sleep_steam_only_selftest.obj"}" /Fe"{exe}" /link advapi32.lib\r\nif errorlevel 1 exit /b 1\r\n"{exe}" "{fixtureDir}" "{root / "tools/fixtures/sleep_steam_tmnt_gameprocess_20261011.txt"}" "{root / "tools/fixtures/sleep_steam_msi_2077_wake_20261011.json"}" "{root / "tools/fixtures/sleep_steam_msi_2077_s4_s0_20261011.json"}" "{root / "tools/fixtures/sleep_steam_msi_2077_r23_073143_20261012.json"}" "{root / "tools/fixtures/sleep_steam_msi_wrong_target_213819.json"}" "{root / "tools/fixtures/sleep_steam_rog_r25_014614_20261013.json"}" "{root / "tools/fixtures/sleep_steam_msi_tmnt_r31_delayed_window_023942.json"}"\r\n',encoding='ascii')
res=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'sleep-steam-only-selftest.log').write_bytes(res.stdout);print(res.stdout.decode('utf-8',errors='replace'));print('nativeReturn',res.returncode)
raise SystemExit(res.returncode)
