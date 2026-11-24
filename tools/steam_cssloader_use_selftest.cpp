#ifdef YMCC_CSS_SOURCE_OWNERSHIP_ONLY
#include "fixtures/steam_css_inventory_source_cases.inc"
#else
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_EARLY_MACRO_BEGIN
#ifdef YMCC_CSS_SHARED_IDENTITY_ONLY
#define YMCC_CSS_FINITE_DOCUMENT_FIXTURE 1
#endif
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_EARLY_MACRO_END
#include "fixtures/steam_css_inventory_source_fixture.inc"

#ifdef YMCC_CSS_COMPLETION_SOURCE_ONLY
#include <atomic>
static std::atomic<bool> cssProbeArm{false};static HANDLE cssProbeReached=nullptr,cssProbeRelease=nullptr;
static DWORD WINAPI cssFixtureWait(DWORD count,const HANDLE* handles,BOOL all,DWORD timeout){
 if(count>=6&&cssProbeArm.load()){
  const auto immediate=::WaitForMultipleObjects(count,handles,all,0);
  if(immediate!=WAIT_TIMEOUT)return immediate; // Deliver original pending events unchanged.
  if(cssProbeArm.exchange(false)){SetEvent(cssProbeReached);if(WaitForSingleObject(cssProbeRelease,3000)!=WAIT_OBJECT_0){SetLastError(ERROR_TIMEOUT);return WAIT_FAILED;}}
 }
 return ::WaitForMultipleObjects(count,handles,all,timeout);
}
#define WaitForMultipleObjects cssFixtureWait
#endif
#include "steam_cssloader_use.h"
#ifdef YMCC_CSS_COMPLETION_SOURCE_ONLY
#undef WaitForMultipleObjects
#include "steam_ui_mode_observer.h"
#endif
#include <fstream>
#include <iostream>
#include <atomic>

#ifdef YMCC_CSS_SHARED_IDENTITY_ONLY
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_INCLUDE_BEGIN
#include "decky_binding_worker_entry.h"
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_INCLUDE_END
#include "steam_document_read.h"
#include "steam_session_observer.h"
#include "decky_sidebar_diagnostics.h"
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_ADAPTER_GUARD_BEGIN
#ifndef YMCC_CSS_FINITE_DOCUMENT_SOURCE_ADAPTED
#error Shared finite document fixture requires the generated inert Observer header
#endif
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_ADAPTER_GUARD_END
using json=nlohmann::json;
static ymcc::steamsession::Observer g_steamSettingsObserver;
static std::atomic<bool> g_exitRequested=false;
static struct {bool isolated=false;}g_aiFanMockSession;
enum class PowerLifecycle {Ready,Suspended};
static std::atomic<PowerLifecycle>g_powerLifecycle=PowerLifecycle::Ready;
static ULONGLONG currentPowerGeneration(){return 1;}
static std::string W2U(const std::wstring& value){const auto count=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string result(count,'\0');WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr);return result;}
static json cssReadRecords=json::array();
static bool cssReadLogThrows=false;
static void deckySidebarDiagnostic(const std::string& event,const json& detail,bool){
 if(cssReadLogThrows)throw std::runtime_error("fixture-log-unavailable");
 cssReadRecords.push_back(ymcc::deckydiag::record(event,detail));
}
#ifdef YMCC_CSS_TARGET_SOURCE_ONLY
static json cssTargetSourceReply;static unsigned cssTargetSourceReads=0;
static json cssTargetSourceRead(const ymcc::steamsession::Session&,const ymcc::steamdocuments::StylePlan&,const std::function<bool()>&,DWORD){++cssTargetSourceReads;return cssTargetSourceReply;}
#endif
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_SOURCE_BEGIN
struct CssFiniteDocumentSourceFixture final {
 bool finished=false;
 CssFiniteDocumentSourceFixture(){
  try {
   if(!g_steamSettingsObserver.start({},[]{return false;}))throw std::runtime_error("finite fixture actual observer start");
   const auto reading=g_steamSettingsObserver.sourceRead();
   if(!g_steamSettingsObserver.ownsSource()||!g_steamSettingsObserver.sourceActive()||!reading||!reading.current()||!reading.view().current())
    throw std::runtime_error("finite fixture actual source admission");
  }catch(...){g_steamSettingsObserver.finishFactReadsForExit();finished=true;throw;}
 }
 void finish(){
  if(finished)return;
  g_steamSettingsObserver.finishFactReadsForExit();finished=true;
  if(g_steamSettingsObserver.sourceActive()||g_steamSettingsObserver.ownsSource()||g_steamSettingsObserver.sourceRead())
   throw std::runtime_error("finite fixture source was not returned");
 }
 ~CssFiniteDocumentSourceFixture(){if(!finished)g_steamSettingsObserver.finishFactReadsForExit();}
};
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_SOURCE_END
static std::string cssReadCostFault;
static unsigned cssReadCostThreadCalls=0,cssReadCostHandleCalls=0;
static BOOL cssReadCostThreadTimes(HANDLE h,LPFILETIME born,LPFILETIME exit,LPFILETIME kernel,LPFILETIME user){
 const auto call=++cssReadCostThreadCalls;
 if((cssReadCostFault=="thread-first"&&call==1)||(cssReadCostFault=="thread-last"&&call==2)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 const auto result=::GetThreadTimes(h,born,exit,kernel,user);
 if(result&&cssReadCostFault=="thread-birth"&&call==2)born->dwHighDateTime^=1u;
 if(result&&cssReadCostFault=="thread-backward"){*kernel={0u,call==1?1u:0u};*user={0u,0u};}
 return result;
}
static BOOL cssReadCostProcessHandles(HANDLE h,PDWORD value){
 const auto call=++cssReadCostHandleCalls;
 if((cssReadCostFault=="handles-first"&&call==1)||(cssReadCostFault=="handles-last"&&call==2)){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 return ::GetProcessHandleCount(h,value);
}
#define GetThreadTimes cssReadCostThreadTimes
#define GetProcessHandleCount cssReadCostProcessHandles
#include "shared-steam-read-main.inc"
#undef GetProcessHandleCount
#undef GetThreadTimes
#endif

using namespace ymcc;
static unsigned checks=0;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);++checks;std::cout<<"PASS "<<text<<std::endl;}
static bool running(HANDLE p){return p&&WaitForSingleObject(p,0)==WAIT_TIMEOUT;}
static std::wstring self(){wchar_t s[32768]{};GetModuleFileNameW(nullptr,s,32768);return s;}
struct Child {HANDLE value=nullptr;~Child(){if(value){TerminateProcess(value,0);WaitForSingleObject(value,2000);CloseHandle(value);}}};
static HANDLE launch(const std::wstring& exe, const std::wstring& args,steamcssuse::Group* group=nullptr){
 auto command=L"\""+exe+L"\" "+args;STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
 if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&si,&pi))throw std::runtime_error("fixture launch");
 if(group&&!group->assign(pi.hProcess)){TerminateProcess(pi.hProcess,1);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);throw std::runtime_error("fixture job");}
 ResumeThread(pi.hThread);CloseHandle(pi.hThread);return pi.hProcess;
}

#ifdef YMCC_CSS_COMPLETION_SOURCE_ONLY
static std::atomic<bool> g_exitRequested{false};static struct{bool isolated=false;}g_aiFanMockSession;
enum class PowerLifecycle{Ready,Suspended};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static struct{ymcc::steamsession::Session value;std::function<std::shared_ptr<const ymcc::steamsession::IdentitySnapshot>()> read;auto sessionSnapshot(){return value;}auto identitySnapshot(){return read?read():nullptr;}}g_steamSettingsObserver;
static constexpr sleeppower::SharedPowerV1 completionReady = [] {
    sleeppower::SharedPowerV1 value;
    value.generation = value.publicationRevision = 1;
    value.flags = sleeppower::kKnown | sleeppower::kHardwareAllowed;
    return value;
}();
static std::atomic<bool> completionPowerReady{true};
static int __cdecl completionPower(sleeppower::SharedPowerV1* out, std::uint32_t bytes) {
    if (!out || bytes != sizeof(*out)) return 0;
    *out = completionReady;
    if (!completionPowerReady) out->flags = sleeppower::kKnown;
    return 1;
}
static struct{bool big=true;ymcc::steamui::ModeObserver* actual=nullptr;auto snapshot(const ymcc::steamsession::Session& s,const sleeppower::SharedPowerV1& power){if(actual)return actual->snapshot(s,power);return nlohmann::json{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode",big?"bigpicture":"desktop"}};}}g_steamModeObserver;
static struct{ymcc::steamcssuse::Runtime* value=nullptr;std::atomic<bool> lastNotify{false};template<class Identity>bool notifyIdentityPublished(const Identity& source){const bool accepted=notify(*value,source);lastNotify=accepted;return accepted;}template<class T,class Identity>static bool notify(T& value,const Identity& source){if constexpr(requires{value.notifyIdentityPublished(source);})return value.notifyIdentityPublished(source);else return false;}template<class Identity>HANDLE confirmAndIdle(const ymcc::steamsession::Session& s,bool start,bool renew,Identity* identity,const sleeppower::SharedPowerV1* power){return value->confirmAndIdle(s,start,renew,identity,power);}}g_steamCssUse;
template<class T,class=void>struct MainObservedShape{struct Type{HANDLE completion=nullptr;std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> identity;};};
template<class T>struct MainObservedShape<T,std::void_t<decltype(std::declval<T>().confirmObserved)>>{using Type=typename std::decay_t<decltype(std::declval<T>().confirmObserved)>::result_type;};
using MainObserved=typename MainObservedShape<ymcc::steamcss::Config>::Type;
static void actualMainIdentityPublished(std::shared_ptr<const ymcc::steamsession::IdentitySnapshot> source){
        if(g_exitRequested.load(std::memory_order_acquire))return;
        if(source==g_steamSettingsObserver.identitySnapshot())g_steamCssUse.notifyIdentityPublished(source);
}
static MainObserved actualMainObserved(const ymcc::steamsession::Session& session,bool startBackend,bool renewForMode,
                                      const sleeppower::SharedPowerV1* power = &completionReady){

        const auto current = g_steamSettingsObserver.sessionSnapshot();
        if (g_exitRequested.load(std::memory_order_acquire) || g_aiFanMockSession.isolated || g_powerLifecycle.load(std::memory_order_acquire) != PowerLifecycle::Ready ||
            !power || !ymcc::steamcss::sameSession(session, current) ||
            !ymcc::steamcss::appearanceModeReady(current, g_steamModeObserver.snapshot(current, *power))) return {};
        MainObserved observed;
        observed.completion = g_steamCssUse.confirmAndIdle(session, startBackend, renewForMode, &observed.identity, power);
        return observed;

}
#endif

#ifdef YMCC_CSS_COMPLETION_SOURCE_ONLY


static int identityWakeCase(const std::filesystem::path& root,bool joined){
 if(!std::filesystem::create_directory(root))throw std::runtime_error("fresh identity-wake root required");
 const auto steamImage=root/L"steam.exe",backend=root/L"CssLoader-Standalone-Headless.exe";std::filesystem::copy_file(self(),steamImage);std::filesystem::copy_file(self(),backend);
 Child steam,css;steam.value=launch(steamImage.wstring(),L"--dummy");css.value=launch(backend.wstring(),L"--dummy");FILETIME born{},exit{},kernel{},user{};check(GetProcessTimes(steam.value,&born,&exit,&kernel,&user),"wake source holds an exact owned Steam birth");
 const steamsession::Session session{root.wstring(),GetProcessId(steam.value),42,(ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime};
 auto first=std::make_shared<steamsession::IdentitySnapshot>();first->session=session;first->presence=1;first->rootReadError=0;first->registeredPid=session.pid;first->registeredPidReadError=0;first->accountReadError=0;first->windowsSessionKnown=true;first->windowsSession=17;first->processEpoch=1;first->accountEpoch=1;
 std::atomic<std::shared_ptr<const steamsession::IdentitySnapshot>> current(first);
 const auto same=[&](const auto& expected){const auto value=current.load();return expected&&value&&expected->processEpoch==value->processEpoch&&expected->accountEpoch==value->accountEpoch&&expected->session==value->session&&expected->presence==value->presence;};
 const auto script=root/L"steam-appearance.ps1",link=root/L"backend.path";std::filesystem::create_directory(root/L"steam-appearance");
 {std::ofstream file(script);file<<"param($Action,$SessionPid,$SessionBirth,$BackendPath,$ProcessQuery,$ReadyEvent,$IdleEvent,$GateTimeoutMs,[switch]$StartBackend,[switch]$VerifyOnly)\n# SharedProcessQueryProtocol=1 SharedSteamReadProtocol=1 ResidentBackendPolicy=3 AutomaticResultProtocol=2 ManualObservationProtocol=1\nStart-Sleep -Seconds 15\n";}
 std::ofstream(root/L"steam-appearance/resident.ps1")<<"AppearanceProcessQueryProtocol=3 function Start-ResidentAppearanceBackend";std::ofstream(root/L"steam-appearance/idle.ps1")<<"AppearanceRenderProtocol=4 AppearanceSharedSteamProtocol=1 function Get-AppearanceVariableDefaults";std::ofstream(link)<<backend.string();CssSourceFixture::start();
 deckylaunch::Handle reached,release;reached.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);release.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!reached.value||!release.value)throw std::runtime_error("owned wake barrier events");cssProbeReached=reached.value;cssProbeRelease=release.value;cssProbeArm=true;
 std::atomic<unsigned> launches{0},modeReads{0};std::shared_ptr<const steamsession::IdentitySnapshot> firstObserved,lastObserved;std::mutex observedMutex;
 steamcssuse::Runtime use(false,35821,[]{return steamcsseditor::State::Absent;});steamcss::Runtime guard;steamui::ModeObserver mode;
 struct Stop{steamcssuse::Runtime& use;steamcss::Runtime& guard;steamui::ModeObserver& mode;HANDLE release;~Stop(){cssProbeArm=false;SetEvent(release);guard.shutdown();mode.shutdown();use.shutdown();g_steamModeObserver.actual=nullptr;g_steamSettingsObserver.read={};}}stop{use,guard,mode,release.value};
 g_steamSettingsObserver.value=session;g_steamSettingsObserver.read=[&]{return current.load();};g_steamCssUse.value=&use;
 check(use.initialize(script.wstring(),link.wstring(),180000,4000,[&](const auto& stage,const auto&,const auto&,const auto&){if(stage=="automatic-helper-start")++launches;},0,[]{return true;},[]{return 1;},[&]{return current.load();},same,{},CssSourceFixture::defaultRead,&completionPower),"real Use worker initializes for event-only revocation");use.observeSteam(session);
 deckylaunch::Handle completion;
 if(joined){
  g_steamModeObserver.actual=&mode;steamui::ModeObserverConfig cfg;cfg.debounceMs=2;cfg.minimumGapMs=4;cfg.failureGapMs=40;cfg.admissionPowerReader=&completionPower;cfg.read=[&](const auto& s){++modeReads;return nlohmann::json{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","bigpicture"},{"modeEpoch","joint-document:1"}};};
  cfg.changed=[&](const auto& s,const auto& value){guard.observeMode(s,{steamcss::UiMode::BigPicture,value.value("modeEpoch",std::string{})});};
  check(mode.initialize(cfg),"joint uses the real shared ModeObserver worker");
  steamcss::Config c;c.linkFile=link.wstring();c.delays={60000,60000,60000};c.modeDelays={2,10,40};c.mayLaunch=[]{return !g_exitRequested&&g_powerLifecycle==PowerLifecycle::Ready;};
  c.changed=[&](const auto& state){mode.setDemand(steamui::ModeConsumer::Css,state.linked);};c.readMode=[&](const auto& s){const auto value=mode.snapshot(s);return steamcss::ModeSample{value.value("mode",std::string{})=="bigpicture"?steamcss::UiMode::BigPicture:steamcss::UiMode::Unknown,value.value("modeEpoch",std::string{})};};
  c.ensure=[&](const auto&,const auto& s,const auto& allowed){if(!allowed()||!steamcss::appearanceModeReady(s,mode.snapshot(s)))return steamcss::Result{nullptr,0,"waiting-steam-ui","mode-pending"};HANDLE copy=nullptr;DuplicateHandle(GetCurrentProcess(),css.value,GetCurrentProcess(),&copy,SYNCHRONIZE,FALSE,0);return steamcss::Result{copy,GetProcessId(css.value),"running","owned-backend"};};
  c.probeExisting=[&]{HANDLE copy=nullptr;DuplicateHandle(GetCurrentProcess(),css.value,GetCurrentProcess(),&copy,SYNCHRONIZE,FALSE,0);return steamcss::Result{copy,GetProcessId(css.value),"running","owned-backend"};};c.isResidentConfirmed=[&](const auto& s){return use.residentConfirmed(s);};
  c.powerReader=&completionPower;c.confirmObserved=[&](const auto& s,bool start,bool renew,const sleeppower::SharedPowerV1* power){auto result=actualMainObserved(s,start,renew,power);std::lock_guard lock(observedMutex);if(!firstObserved)firstObserved=result.identity;lastObserved=result.identity;return result;};c.identityCurrent=same;c.refreshMode=[&](const auto& s){return use.invalidateForResume(s)&&mode.requestRefresh(s);};
  check(guard.initialize(c),"joint starts actual Guard with the main observed-confirmation body");guard.observeSteam(session);mode.observeSteam(session);
 }else{auto observed=actualMainObserved(session,false,false);completion.value=observed.completion;firstObserved=lastObserved=observed.identity;check(completion.value&&firstObserved==first,"idle helper returns its exact source before notification");}
 check(WaitForSingleObject(reached.value,3000)==WAIT_OBJECT_0,"actual Use reaches a wait with no original pending event");
 check(launches==1,"one original helper is parked without IPC work");
 auto identical=std::make_shared<steamsession::IdentitySnapshot>(*first);++identical->revision;current=identical;unsigned identicalAccepted=0;for(unsigned i=0;i<16;++i){g_steamCssUse.lastNotify=false;actualMainIdentityPublished(identical);if(g_steamCssUse.lastNotify)++identicalAccepted;}check(!identicalAccepted,"same epochs and metadata-only revision publish no worker wake");
 auto successor=std::make_shared<steamsession::IdentitySnapshot>(*identical);successor->accountEpoch=2;successor->registeredPidReadError=ERROR_ACCESS_DENIED;current=successor;
 g_steamCssUse.lastNotify=false;actualMainIdentityPublished(first);check(!g_steamCssUse.lastNotify,"stale publication is rejected by actual main current-source guard");
 unsigned accepted=0;for(unsigned i=0;i<16;++i){g_steamCssUse.lastNotify=false;actualMainIdentityPublished(successor);if(g_steamCssUse.lastNotify)++accepted;}
 SetEvent(release.value);
 bool continued=false;if(joined){const auto until=GetTickCount64()+1500;while(launches<2&&GetTickCount64()<until)Sleep(2);continued=launches==2;}else continued=WaitForSingleObject(completion.value,600)==WAIT_OBJECT_0;
 std::ofstream(root/L"wake-observation.json")<<nlohmann::json{{"joined",joined},{"acceptedWhileParked",accepted},{"continued",continued},{"helperLaunches",launches.load()},{"modeReads",modeReads.load()},{"ownedSteamStillAlive",running(steam.value)},{"ownedBackendStillAlive",running(css.value)},{"installedSteamCssOperations",0}}.dump(2);
 check(continued,joined?"public identity event must connect actual Use retirement to one Guard successor helper":"public identity event must wake the parked Use without helper IPC or a timer");
 check(accepted==1,"repeated successor publications coalesce into one original wake");
 {std::lock_guard lock(observedMutex);check(firstObserved==first,"old helper source remains immutable across same-account successor");if(joined)check(lastObserved&&lastObserved->accountEpoch==2,"joint successor helper captures the new exact source");}
 const auto beforeLaunches=launches.load(),beforeReads=modeReads.load();Sleep(80);check(launches==beforeLaunches&&modeReads==beforeReads,"pending current helper parks without repeated discovery or mode reads");
 guard.shutdown();mode.shutdown();use.shutdown();check(running(steam.value)&&running(css.value),"event retirement and joint shutdown retain independent owned backend and Steam");
 std::ofstream(root/L"result.json")<<nlohmann::json{{"ok",true},{"selection",joined?"identity-joint":"identity-wake"},{"passed",checks},{"acceptedWhileParked",accepted},{"actualUseGuardJoint",joined},{"actualModeWorker",joined},{"realOwnedPowerShellHelpers",launches.load()},{"installedSteamCssOperations",0}}.dump(2);return 0;
}

static int completionSourceCase(const std::filesystem::path& root){
 if(!std::filesystem::create_directory(root))throw std::runtime_error("fresh completion-source root required");
 const auto steamImage=root/L"steam.exe",backend=root/L"CssLoader-Standalone-Headless.exe";
 std::filesystem::copy_file(self(),steamImage);std::filesystem::copy_file(self(),backend);
 Child steam,css;steam.value=launch(steamImage.wstring(),L"--dummy");css.value=launch(backend.wstring(),L"--dummy");
 FILETIME born{},exit{},kernel{},user{};check(GetProcessTimes(steam.value,&born,&exit,&kernel,&user),"completion source uses an exact owned Steam birth");
 const steamsession::Session session{root.wstring(),GetProcessId(steam.value),42,(ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime};
 auto first=std::make_shared<steamsession::IdentitySnapshot>();first->session=session;first->presence=1;first->rootReadError=0;first->registeredPid=session.pid;first->registeredPidReadError=0;first->accountReadError=0;first->windowsSessionKnown=true;first->windowsSession=17;first->processEpoch=1;first->accountEpoch=1;
 std::atomic<std::shared_ptr<const steamsession::IdentitySnapshot>> current(first);
 const auto script=root/L"steam-appearance.ps1",link=root/L"backend.path";std::filesystem::create_directory(root/L"steam-appearance");
 {std::ofstream file(script);file<<"param($Action,$SessionPid,$SessionBirth,$BackendPath,$ProcessQuery,$ReadyEvent,$IdleEvent,$GateTimeoutMs,[switch]$StartBackend,[switch]$VerifyOnly)\n# SharedProcessQueryProtocol=1 SharedSteamReadProtocol=1 ResidentBackendPolicy=3 AutomaticResultProtocol=2 ManualObservationProtocol=1\nStart-Sleep -Seconds 15\n";}
 std::ofstream(root/L"steam-appearance/resident.ps1")<<"AppearanceProcessQueryProtocol=3 function Start-ResidentAppearanceBackend";
 std::ofstream(root/L"steam-appearance/idle.ps1")<<"AppearanceRenderProtocol=4 AppearanceSharedSteamProtocol=1 function Get-AppearanceVariableDefaults";
 std::ofstream(link)<<backend.string();CssSourceFixture::start();
 steamcssuse::Runtime use(false,35821,[]{return steamcsseditor::State::Absent;});
 struct Stop{steamcssuse::Runtime& use;~Stop(){use.shutdown();}}stop{use};
 check(use.initialize(script.wstring(),link.wstring(),180000,4000,{},0,[]{return true;},[]{return 1;},[&]{return current.load();},[&](const auto& expected){return expected&&expected==current.load();},{},CssSourceFixture::defaultRead,&completionPower),"real Use initializes with explicit memory identity and original owned inventory source");
 use.observeSteam(session);
 g_steamSettingsObserver.value=session;g_steamCssUse.value=&use;
 const auto observe=[](auto&,const auto& s,auto& identity,bool renew=false){auto result=actualMainObserved(s,false,renew);identity=std::move(result.identity);return result.completion;};
 deckylaunch::Handle completion;std::shared_ptr<const steamsession::IdentitySnapshot> observed;
 const auto readyPolicy=use.readPolicy(session);
 check(readyPolicy.ready&&!readyPolicy.processQuery.empty()&&!readyPolicy.automaticRepairActive,"idle policy reports no repair owner before the automatic use");
 completion.value=observe(use,session,observed);
 check(completion.value&&observed==first,"returned wait binds the exact identity captured inside Use");
 const auto activePolicy=use.readPolicy(session);
 check(activePolicy.automaticRepairActive&&activePolicy.processQuery==readyPolicy.processQuery,"live same-session automatic apply is visible on the existing policy read");
 auto other=session;++other.created;
 check(!use.readPolicy(other).automaticRepairActive,"another Steam birth cannot borrow the automatic repair owner");
 completionPowerReady=false;
 check(!use.readPolicy(session).automaticRepairActive,"public power revocation removes the repair owner immediately");
 completionPowerReady=true;
 auto successor=std::make_shared<steamsession::IdentitySnapshot>(*first);successor->accountEpoch=2;successor->registeredPidReadError=ERROR_ACCESS_DENIED;current=successor;
 check(!use.readPolicy(session).automaticRepairActive,"same-PID identity revocation refuses the previous helper before cancellation");
 use.observeSteam(session);check(WaitForSingleObject(completion.value,2000)==WAIT_OBJECT_0,"same numeric account metadata retirement completes the actual old helper wait");
 check(observed==first&&observed!=current.load(),"old completed wait retains its own immutable source instead of borrowing latest facts");
 CloseHandle(completion.value);completion.value=nullptr;
 completion.value=observe(use,session,observed);check(completion.value&&observed==successor,"successor use captures its own public source");
 deckylaunch::Handle renewed;std::shared_ptr<const steamsession::IdentitySnapshot> renewedSource;
 renewed.value=observe(use,session,renewedSource,true);check(renewed.value&&renewedSource==observed,"renewed same use returns the same source while holding the original mutex");
 use.invalidateForResume(session,false);check(WaitForSingleObject(completion.value,2000)==WAIT_OBJECT_0,"source-bearing wait retains original cancellation semantics");
 check(!use.readPolicy(session).automaticRepairActive,"retired helper leaves no repair owner");
 CloseHandle(completion.value);completion.value=nullptr;CloseHandle(renewed.value);renewed.value=nullptr;
 completion.value=use.confirmExisting(session);
 check(completion.value&&!use.readPolicy(session).automaticRepairActive,"VerifyOnly never owns configuration repair");
 use.invalidateForResume(session,false);
 CloseHandle(completion.value);completion.value=nullptr;
 const auto manual=use.begin(session);observed=first;completion.value=observe(use,session,observed);
 check(!completion.value&&!observed,"manual ownership refusal clears an old output source without launching another helper");
 check(!use.readPolicy(session).automaticRepairActive,"manual hold does not impersonate an automatic helper");use.release(manual.token);
 g_exitRequested=true;check(!actualMainObserved(session,false,false).completion,"actual main observed callback rejects exit");g_exitRequested=false;
 g_powerLifecycle=PowerLifecycle::Suspended;check(!actualMainObserved(session,false,false).completion,"actual main observed callback rejects non-Ready");g_powerLifecycle=PowerLifecycle::Ready;
 g_aiFanMockSession.isolated=true;check(!actualMainObserved(session,false,false).completion,"actual main observed callback rejects isolation");g_aiFanMockSession.isolated=false;
 g_steamModeObserver.big=false;check(!actualMainObserved(session,false,false).completion,"actual main observed callback rejects desktop mode");g_steamModeObserver.big=true;
 auto stale=session;++stale.created;check(!actualMainObserved(stale,false,false).completion,"actual main observed callback rejects stale birth");
 use.shutdown();check(running(steam.value)&&running(css.value),"source handoff and shutdown retain independent owned Steam/backend processes");
 std::ofstream(root/L"result.json")<<nlohmann::json{{"ok",true},{"selection","completion-source"},{"passed",checks},{"actualUseRuntime",true},{"realOwnedPowerShellHelper",true},{"identityPort","explicit atomic immutable snapshot"},{"installedSteamCssOperations",0}}.dump(2);return 0;
}

#endif
int wmain(int argc,wchar_t**argv){try{
#ifdef YMCC_CSS_COMPLETION_SOURCE_ONLY
 if(argc==3&&std::wstring(argv[1])==L"--identity-wake-only")return identityWakeCase(std::filesystem::absolute(argv[2]),false);
 if(argc==3&&std::wstring(argv[1])==L"--identity-joint-only")return identityWakeCase(std::filesystem::absolute(argv[2]),true);
 if(argc==3&&std::wstring(argv[1])==L"--completion-source-only")return completionSourceCase(std::filesystem::absolute(argv[2]));
#endif
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_ENTRY_BEGIN
#ifdef YMCC_CSS_SHARED_IDENTITY_ONLY
 int finiteWorkerExit=0;
 if(ymcc::deckybinding::entry::tryRunCommandLine(GetCommandLineW(),finiteWorkerExit))return finiteWorkerExit;
#endif
// YMCC_FORMAL_DOCUMENT_CONSUMER_R214_ENTRY_END
 if(argc>1&&std::wstring(argv[1])==L"--dummy"){Sleep(INFINITE);return 0;}
#ifdef YMCC_CSS_SHARED_IDENTITY_ONLY
#ifdef YMCC_CSS_TARGET_SOURCE_ONLY
 if(argc==5&&std::wstring(argv[1])==L"--shared-identity-only"&&std::wstring(argv[3])==L"--target-source-diagnostics"){
  const auto root=std::filesystem::absolute(argv[2]);if(!std::filesystem::create_directory(root))return 2;
  const auto output=root/L"result.json";const HANDLE rawHandle=CreateFileW(output.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(rawHandle==INVALID_HANDLE_VALUE)return 2;
  struct RawOwner{HANDLE h;~RawOwner(){if(h!=INVALID_HANDLE_VALUE)::CloseHandle(h);}}rawOwner{rawHandle};
  std::ifstream input{std::filesystem::path(argv[4])};json original;input>>original;cssTargetSourceReply=original.at("cases").at(0).at("receipt");
  check(!cssTargetSourceReply.value("ok",true)&&cssTargetSourceReply.value("reason",std::string{})=="document-source-unconfirmed"&&cssTargetSourceReply.contains("targetSource"),"actual-producer-rejection-input");
  DWORD before=0;check(GetProcessHandleCount(GetCurrentProcess(),&before),"target-source-initial-handles");FILETIME born{},end{},kernel{},user{};check(GetProcessTimes(GetCurrentProcess(),&born,&end,&kernel,&user),"target-source-held-self-birth");
  steamsession::IdentitySnapshot facts;facts.session={std::filesystem::path(self()).parent_path().wstring(),GetCurrentProcessId(),0,(ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime};facts.presence=1;facts.registeredPid=facts.session.pid;facts.rootReadError=facts.registeredPidReadError=facts.accountReadError=ERROR_SUCCESS;facts.windowsSessionKnown=true;facts.windowsSession=17;facts.windowsSessionError=0;steamsession::IdentityPublicationFixture::publish(g_steamSettingsObserver,facts);
  auto expected=steamAppearanceSharedSteamRead({{"operation","identity"}},[]{return true;});for(const auto* key:{"ok","protocol","presence"})expected.erase(key);
  const json fields={{"properties",json::array({"--fixture"})},{"fileCounts",json::array({"--fixture"})}};const json request={{"operation","documents"},{"identity",expected},{"plans",{{"SP",fields},{"MainMenu",fields},{"QuickAccess",fields}}}};
  const auto returned=steamAppearanceSharedSteamRead(request,[]{return true;});check(returned==cssTargetSourceReply&&cssTargetSourceReads==1,"actual-main-preserves-original-rejection-once");check(cssReadRecords.size()==1&&cssReadRecords.back().at("targetSource")==returned.at("targetSource"),"actual-main-and-log-filter-carry-producer-facts");
  cssReadLogThrows=true;const auto failedLog=steamAppearanceSharedSteamRead(request,[]{return true;});cssReadLogThrows=false;check(failedLog==returned&&cssTargetSourceReads==2&&cssReadRecords.size()==1,"logger-failure-does-not-change-result-or-retry");
  const json good={{"readOperation","documents"},{"readReason","document-source-unconfirmed"},{"readOk",false},{"readComplete",false},{"targetSource",returned.at("targetSource")}};
  unsigned refused=0;for(const auto* field:{"schema","phase","kind","urlClass","urlLength","hasQuery","hasFragment"}){auto poison=good;poison["targetSource"][field]="SECRET";check(!deckydiag::record("steam-css-read",poison).contains("targetSource"),"malformed-source-field-refused");++refused;}
  for(const auto& poisonValue:std::vector<json>{-1,4098,true}){auto poison=good;poison["targetSource"]["urlLength"]=poisonValue;check(!deckydiag::record("steam-css-read",poison).contains("targetSource"),"source-length-boundary-refused");++refused;}
  auto extra=good;extra["targetSource"]["url"]="SECRET";check(!deckydiag::record("steam-css-read",extra).contains("targetSource"),"extra-private-field-refused");
  for(const auto* field:{"readOperation","readReason","readOk","readComplete"}){auto poison=good;poison[field]="SECRET";check(!deckydiag::record("steam-css-read",poison).contains("targetSource"),"source-facts-only-for-exact-document-refusal");++refused;}
  DWORD after=0;check(GetProcessHandleCount(GetCurrentProcess(),&after)&&after==before,"target-source-no-helper-and-balanced-resources");
  const auto bytes=json{{"ok",true},{"selection","shared-identity-target-source-diagnostics"},{"passed",checks},{"producerCasesConsumed",1},{"actualMainConsumer",true},{"controlledDocumentReadLeaf",true},{"readCalls",cssTargetSourceReads},{"invalidInputsRefused",refused},{"records",cssReadRecords},{"beforeHandles",before},{"afterHandles",after},{"helperLaunches",0},{"realSteamOperations",0}}.dump(2);DWORD written=0;if(!WriteFile(rawOwner.h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)||written!=bytes.size()||!FlushFileBuffers(rawOwner.h))return 3;if(!CloseHandle(rawOwner.h))return 3;rawOwner.h=INVALID_HANDLE_VALUE;return 0;
 }
 return 2;
#endif
 if(argc==4&&std::wstring(argv[1])==L"--shared-identity-only"&&std::wstring(argv[3])==L"--read-diagnostics"){
  const auto root=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(root);
  DWORD initialHandles=0;check(GetProcessHandleCount(GetCurrentProcess(),&initialHandles),"read diagnostic initial handles");
  FILETIME born{},ended{},kernel{},user{};check(GetProcessTimes(GetCurrentProcess(),&born,&ended,&kernel,&user),"read diagnostic current process birth");
  steamsession::IdentitySnapshot facts;facts.session={std::filesystem::path(self()).parent_path().wstring(),GetCurrentProcessId(),0,(ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime};
  facts.presence=1;facts.registeredPid=facts.session.pid;facts.rootReadError=facts.registeredPidReadError=facts.accountReadError=ERROR_SUCCESS;
  facts.windowsSessionKnown=true;facts.windowsSession=17;facts.windowsSessionError=0;
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_START_BEGIN
  CssFiniteDocumentSourceFixture finiteSource;
  steamsession::IdentityPublicationFixture::publish(g_steamSettingsObserver,facts);
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_START_END
  const auto current=[] {return true;};
  const auto identity=steamAppearanceSharedSteamRead({{"operation","identity"}},current);
  check(identity.value("ok",false)&&cssReadRecords.empty(),"successful cached identity adds no routine diagnostic");
  check(cssReadCostThreadCalls==0&&cssReadCostHandleCalls==0,"routine identity adds no cost sampling API calls");
  json expected=identity;for(const auto* key:{"ok","protocol","presence"})expected.erase(key);
  const json fields={{"properties",json::array({"--fixture"})},{"fileCounts",json::array({"--fixture"})}};
  json request={{"operation","documents"},{"identity",expected},{"plans",{{"SP",fields},{"MainMenu",fields},{"QuickAccess",fields}}}};
  {
   std::unique_lock transport(steamlive::transportMutex);
   const auto result=steamAppearanceSharedSteamRead(request,current);
   check(result.value("reason",std::string{})=="live-busy","original document read keeps its real busy result");
   check(cssReadRecords.size()==1&&cssReadRecords.back().value("readReason",std::string{})=="live-busy"&&cssReadRecords.back().value("readOperation",std::string{})=="documents","actual shared read exposes its bounded native failure classification");
   check(cssReadRecords.back().value("readThreadCpuKnown",false)&&cssReadRecords.back().value("readProcessHandlesKnown",false),"actual request exposes both bounded native cost facts");
   check(cssReadCostThreadCalls==2&&cssReadCostHandleCalls==2&&cssReadRecords.back().at("readThreadId")==GetCurrentThreadId()&&cssReadRecords.back().at("readThreadCpu100ns").is_number_unsigned(),"one document request takes two exact-thread and process samples");
   FILETIME observedBorn{},observedExit{},observedKernel{},observedUser{};check(::GetThreadTimes(GetCurrentThread(),&observedBorn,&observedExit,&observedKernel,&observedUser)!=FALSE,"actual self thread birth available");
   const auto observedBirth=(ULONGLONG(observedBorn.dwHighDateTime)<<32)|observedBorn.dwLowDateTime;
   check(cssReadRecords.back().at("readThreadBirth")==std::to_string(observedBirth),"cost facts bind the actual same thread birth");
   for(const std::string fault:{"thread-first","thread-last","thread-birth","thread-backward","handles-first","handles-last"}){
    cssReadCostFault=fault;cssReadCostThreadCalls=cssReadCostHandleCalls=0;
    const auto beforeRecords=cssReadRecords.size();const auto failedSample=steamAppearanceSharedSteamRead(request,current);
    const auto& record=cssReadRecords.back();const bool threadFailure=fault.rfind("thread-",0)==0;
    check(failedSample==result&&cssReadRecords.size()==beforeRecords+1&&cssReadCostThreadCalls==2&&cssReadCostHandleCalls==2,"sample failure preserves original result and never retries");
    check(record.at("readThreadCpuKnown")==!threadFailure&&record.at("readProcessHandlesKnown")==threadFailure,"sample unknowns stay independent");
    if(threadFailure)check(!record.contains("readThreadId")&&!record.contains("readThreadBirth")&&!record.contains("readThreadCpu100ns"),"unknown thread sample exposes no fabricated numeric fact");
    else check(!record.contains("readProcessHandlesBefore")&&!record.contains("readProcessHandlesAfter"),"unknown process sample exposes no fabricated counts");
   }
   cssReadCostFault.clear();cssReadCostThreadCalls=cssReadCostHandleCalls=0;
   const auto count=cssReadRecords.size();cssReadLogThrows=true;
   const auto logFailure=steamAppearanceSharedSteamRead(request,current);cssReadLogThrows=false;
   check(logFailure==result&&cssReadRecords.size()==count,"throwing logger preserves exact document result without retry");
  }
  auto stale=request;stale["identity"]["processEpoch"]="0";
  const auto expired=steamAppearanceSharedSteamRead(stale,current);
  check(expired.value("reason",std::string{})=="shared-steam-request-expired"&&cssReadRecords.back().at("readReason")=="shared-steam-request-expired","expired same-process epoch retains original refusal and classification");
  g_powerLifecycle=PowerLifecycle::Suspended;
  const auto suspended=steamAppearanceSharedSteamRead({{"operation","identity"}},current);g_powerLifecycle=PowerLifecycle::Ready;
  check(suspended.value("reason",std::string{})=="shared-steam-identity-unknown"&&cssReadRecords.back().at("readOperation")=="identity","power refusal has an identity diagnostic without a document read");
  const auto unavailable=steamAppearanceSharedSteamRead(request,[]{return false;});
  check(unavailable.value("reason",std::string{})=="shared-steam-identity-unknown","diagnostics cannot override request cancellation");
  const auto poisoned=deckydiag::record("steam-css-read",{{"readOperation","documents"},{"readReason","document-private-url-token"},{"readOk",false},{"readComplete","true"},{"documentCount",13},{"mainDocuments",-1},{"menuDocuments",true},{"elapsedMs",99999999ull},{"reason","SECRET"},{"url","SECRET"},{"documents",json::array({{{"value","SECRET"}}})},{"steamPid",123u},{"steamBirth","100"}});
  check(poisoned.at("readReason")=="unknown"&&!poisoned.contains("reason")&&!poisoned.contains("url")&&!poisoned.contains("documents")&&!poisoned.contains("documentCount")&&!poisoned.contains("mainDocuments")&&!poisoned.contains("menuDocuments")&&!poisoned.contains("readComplete")&&poisoned.at("elapsedMs")==3600000ull,"read log rejects private text, nested payloads and invalid counts/types");
  const auto success=deckydiag::record("steam-css-read",{{"readOperation","documents"},{"readOk",true},{"readComplete",false},{"documentCount",3u},{"mainDocuments",1u},{"menuDocuments",1u},{"qamDocuments",1u},{"elapsedMs",42ull}});
  check(success.at("readReason")=="none"&&!success.at("readComplete").get<bool>()&&success.at("documentCount")==3&&success.at("qamDocuments")==1,"partial documents stay diagnostic incomplete rather than applied permission");
  const json cost={{"readOperation","documents"},{"readThreadCpuKnown",true},{"readThreadId",12u},{"readThreadBirth","134391875965228520"},{"readThreadCpu100ns",0ull},{"readProcessHandlesKnown",true},{"readProcessHandlesBefore",100u},{"readProcessHandlesAfter",102u}};
  const auto validCost=deckydiag::record("steam-css-read",cost);check(validCost.at("readThreadCpuKnown")==true&&validCost.at("readThreadCpu100ns")==0ull&&validCost.at("readProcessHandlesKnown")==true,"zero CPU counter increment is a known observation, not a zero-cost assertion");
  for(const auto* field:{"readThreadId","readThreadCpu100ns","readProcessHandlesBefore","readProcessHandlesAfter"})for(const auto& value:std::vector<json>{-1,true,"123",nullptr}){
   auto bad=cost;bad[field]=value;const auto filtered=deckydiag::record("steam-css-read",bad);const bool cpu=std::string(field).rfind("readThread",0)==0;
   check(filtered.at(cpu?"readThreadCpuKnown":"readProcessHandlesKnown")==false&&!filtered.contains(field),"bad cost type refuses the entire related sample");
  }
  for(const auto& value:std::vector<json>{"", "0", "SECRET", "123456789012345678901"}){auto bad=cost;bad["readThreadBirth"]=value;check(deckydiag::record("steam-css-read",bad).at("readThreadCpuKnown")==false,"invalid thread birth cannot confirm a CPU sample");}
  for(const auto* field:{"readThreadCpuKnown","readProcessHandlesKnown"}){auto bad=cost;bad[field]="true";check(!deckydiag::record("steam-css-read",bad).contains(field),"sample known flags require real booleans");}
  for(const auto& pair:std::vector<std::pair<std::string,json>>{{"readThreadId",0u},{"readThreadId",4294967296ull},{"readThreadCpu100ns",36000000001ull},{"readThreadBirth","18446744073709551616"},{"readProcessHandlesBefore",4294967296ull},{"readProcessHandlesAfter",4294967296ull}}){
   auto bad=cost;bad[pair.first]=pair.second;const auto filtered=deckydiag::record("steam-css-read",bad);const bool cpu=pair.first.rfind("readThread",0)==0;
   check(filtered.at(cpu?"readThreadCpuKnown":"readProcessHandlesKnown")==false&&!filtered.contains(pair.first),"out-of-range cost fact cannot confirm a sample");
  }
  auto identityCost=cost;identityCost["readOperation"]="identity";const auto identityFiltered=deckydiag::record("steam-css-read",identityCost);check(!identityFiltered.contains("readThreadCpuKnown")&&!identityFiltered.contains("readProcessHandlesKnown"),"other events cannot carry document cost facts");
  unsigned finiteReadReasonsChecked=0;
  for(const auto* reason:{"document-worker-deadline","document-worker-reply-invalid","document-worker-cleanup-incomplete"}){
   const auto record=deckydiag::record("steam-css-read",{{"readOperation","documents"},{"readOk",false},{"readReason",reason}});
   if(record.at("readReason")!=reason||record.at("readOk")!=false)throw std::runtime_error("finite refusal lost its closed diagnostic reason");
   ++finiteReadReasonsChecked;
  }
  cssReadRecords.push_back(success);cssReadRecords.push_back(poisoned);
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_FINISH_BEGIN
  finiteSource.finish();
  // YMCC_FORMAL_DOCUMENT_CONSUMER_R214_FINISH_END
  DWORD finalHandles=0;check(GetProcessHandleCount(GetCurrentProcess(),&finalHandles)&&finalHandles==initialHandles,"read diagnostics return all temporary resources without launching helpers");
  std::ofstream(root/L"result.json")<<json{{"ok",true},{"passed",checks},{"selection","shared-identity-read-diagnostics"},{"actualMainConsumer",true},{"actualSharedPublication",true},{"actualDocumentBusyResult",true},{"finiteReadReasonsChecked",finiteReadReasonsChecked},{"records",cssReadRecords},{"beforeHandles",initialHandles},{"afterHandles",finalHandles},{"helperLaunches",0},{"realSteamOperations",0},{"realCssOperations",0}}.dump(2);
  return 0;
 }
 if(argc!=3||std::wstring(argv[1])!=L"--shared-identity-only")throw std::runtime_error("shared identity selection and fresh evidence root required");
 const auto root=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(root);
 const auto steamImage=root/L"steam.exe",backend=root/L"CssLoader-Standalone-Headless.exe";
 std::filesystem::copy_file(self(),steamImage);std::filesystem::copy_file(self(),backend);
 Child steam,css;steam.value=launch(steamImage.wstring(),L"--dummy");css.value=launch(backend.wstring(),L"--dummy");
 FILETIME born{},exit{},kernel{},user{};check(GetProcessTimes(steam.value,&born,&exit,&kernel,&user),"held inert Steam has a real birth");
 steamsession::IdentitySnapshot facts;facts.session={root.wstring(),GetProcessId(steam.value),0,(ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime};
 facts.presence=1;facts.registeredPid=facts.session.pid;facts.rootReadError=facts.registeredPidReadError=ERROR_SUCCESS;facts.accountReadError=ERROR_ACCESS_DENIED;facts.windowsSessionKnown=true;facts.windowsSession=17;facts.windowsSessionError=0;
 auto& shared=g_steamSettingsObserver;steamsession::IdentityPublicationFixture::publish(shared,facts);
 const auto entry=root/L"steam-appearance.ps1",link=root/L"backend.path";std::ofstream(entry)<<"inert fixture";std::ofstream(link)<<backend.string();
 std::atomic<unsigned> reads{0},currentChecks{0},launches{0};std::atomic<ULONGLONG> power=1;
 steamcssuse::Runtime use(false,35821,[]{return steamcsseditor::State::Absent;});
 check(use.initialize(entry.wstring(),link.wstring(),180000,4000,[&](const auto&stage,const auto&,const auto&,const auto&){if(stage=="ensure-helper-start"||stage=="automatic-helper-start")++launches;},0,[]{return true;},[&]{return power.load();},[&]{++reads;return shared.identitySnapshot();},[&](const auto&expected){++currentChecks;return shared.sameIdentityEpoch(expected);},steamAppearanceSharedSteamRead,CssSourceFixture::defaultRead),"CSS consumes public identity callbacks without an additional observer");

 const auto session=shared.identitySnapshot()->session;use.observeSteam(session);
 {
  const auto repo=std::filesystem::path(__FILE__).parent_path().parent_path();wchar_t system[32768]{};GetSystemDirectoryW(system,32768);
  Child client;client.value=launch((std::filesystem::path(system)/L"WindowsPowerShell/v1.0/powershell.exe").wstring(),L"-NoProfile -ExecutionPolicy Bypass -File \""+(repo/L"tools/steam_appearance_process_identity_selftest.ps1").wstring()+L"\" -Out \""+(root/L"shared-channel").wstring()+L"\" -ProcessQuery \""+use.processQuery()+L"\" -ExpectedPid "+std::to_wstring(session.pid)+L" -SharedChannel");
  check(WaitForSingleObject(client.value,20000)==WAIT_OBJECT_0,"actual PowerShell shared Steam client finishes within its owned lifetime");DWORD code=1;GetExitCodeProcess(client.value,&code);check(code==0,"real worker and C# mapping consume only public Steam identity and reject malformed requests");
 }

 auto token=use.begin(session);check(token.token!=0,"shared process-only facts admit CSS while account candidate is Unknown");
 facts.session.account=42;facts.accountReadError=ERROR_SUCCESS;steamsession::IdentityPublicationFixture::publish(shared,facts);
 bool rejected=false;try{use.ensure(token.token);}catch(...){rejected=true;}
 check(rejected&&!launches,"a public account epoch change rejects an old Ensure before any helper starts");
 check(use.release(token.token).stale,"Ensure revokes expired use before a later release can claim valid manual completion");
 const auto current=shared.identitySnapshot()->session;use.observeSteam(current);token=use.begin(current);
 check(token.token!=0,"a new request can consume the successor public epoch");
 facts.registeredPidReadError=ERROR_ACCESS_DENIED;steamsession::IdentityPublicationFixture::publish(shared,facts);
 use.observeSteam(current);check(use.release(token.token).stale,"shared metadata invalidation cancels an active use even when Session fields are unchanged");
 facts.presence=-1;steamsession::IdentityPublicationFixture::publish(shared,facts);rejected=false;try{use.begin(current);}catch(...){rejected=true;}
 check(rejected&&!launches,"Unknown public presence cannot be turned into CSS launch permission");
 facts.presence=1;facts.registeredPidReadError=0;steamsession::IdentityPublicationFixture::publish(shared,facts);use.observeSteam(current);token=use.begin(current);
 shared.stop();rejected=false;try{use.ensure(token.token);}catch(...){rejected=true;}
 check(rejected&&!launches,"public producer teardown revokes a retained CSS request");
 use.release(token.token);use.shutdown();check(running(css.value)&&running(steam.value),"identity revocation and runtime shutdown retain both independent inert processes");
 check(reads>0&&currentChecks>reads,"CSS performs repeatable memory-only checks instead of re-reading Steam registration");
 std::ofstream(root/L"result.json")<<"{\"ok\":true,\"passed\":"<<checks<<",\"selection\":\"shared-identity-only\",\"publicIdentityReads\":"<<reads.load()<<",\"epochChecks\":"<<currentChecks.load()<<",\"helperLaunches\":"<<launches.load()<<",\"realSteamOperations\":0,\"realCssOperations\":0}";
 return 0;
#else
 if(argc!=2)throw std::runtime_error("evidence root required");
 const auto root=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(root);
 const auto backend=root/L"CssLoader-Standalone-Headless.exe";std::filesystem::copy_file(self(),backend,std::filesystem::copy_options::overwrite_existing);
 std::filesystem::create_directories(root/L"steam-appearance");
 auto entry=root/L"steam-appearance.ps1",module=root/L"steam-appearance/resident.ps1",renderer=root/L"steam-appearance/idle.ps1";
 std::ofstream(renderer)<<"AppearanceRenderProtocol=4 AppearanceSharedSteamProtocol=1 function Get-AppearanceVariableDefaults";
 std::ofstream(entry)<<"SharedProcessQueryProtocol=1 SharedSteamReadProtocol=1 ResidentBackendPolicy=3 AutomaticResultProtocol=2 ManualObservationProtocol=1";std::ofstream(module)<<"AppearanceProcessQueryProtocol=3 function Start-ResidentAppearanceBackend";
 steamcssuse::verifyResidentControl(entry.wstring());check(true,"matching resident script and module pass native preflight");
 std::ofstream(entry)<<"AutomaticResultProtocol=1";bool refused=false;try{steamcssuse::verifyResidentControl(entry.wstring());}catch(...){refused=true;}check(refused,"old script is refused before it can create job-owned CSS");
 std::ofstream(entry)<<"SharedProcessQueryProtocol=1 SharedSteamReadProtocol=1 ResidentBackendPolicy=3 AutomaticResultProtocol=2 ManualObservationProtocol=1";std::filesystem::remove(module);refused=false;try{steamcssuse::verifyResidentControl(entry.wstring());}catch(...){refused=true;}check(refused,"missing resident module is refused before helper creation");
 std::ofstream(module)<<"AppearanceProcessQueryProtocol=3 function Start-ResidentAppearanceBackend";std::ofstream(renderer)<<"AppearanceRenderProtocol=2";
 refused=false;try{steamcssuse::verifyResidentControl(entry.wstring());}catch(...){refused=true;}check(refused,"old static-only render component is refused before helper creation");
 std::filesystem::remove(renderer);refused=false;try{steamcssuse::verifyResidentControl(entry.wstring());}catch(...){refused=true;}check(refused,"missing render component is refused before helper creation");
 std::ofstream(renderer)<<"AppearanceRenderProtocol=4 AppearanceSharedSteamProtocol=1 function Get-AppearanceVariableDefaults";
 Child css;css.value=launch(backend.wstring(),L"--dummy");
 // The existing identity consumer also exercises the actual cross-process
 // channel, with only owned inert helper/backend/editor processes.
 {
  const auto desktop=root/L"CSSLoader Desktop.exe";std::filesystem::copy_file(self(),desktop,std::filesystem::copy_options::overwrite_existing);
  Child editor;editor.value=launch(desktop.wstring(),L"--dummy");
  std::atomic<unsigned> served{0};
  steamcssuse::Runtime inventoryHost(false,35821,[]{return steamcsseditor::State::Absent;});
  check(inventoryHost.initialize(entry.wstring(),(root/L"unused-backend.path").wstring(),180000,4000,{},0,[&]{return ++served!=2;},{}, {}, {}, {},CssSourceFixture::defaultRead),"actual CSS-use worker starts the protected inventory channel");
  FILETIME birth{},exit{},kernel{},user{};GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user);
  steamsession::Session session{L"owned-fixture",GetCurrentProcessId(),0,(ULONGLONG(birth.dwHighDateTime)<<32)|birth.dwLowDateTime};
  inventoryHost.observeSteam(session);
  const auto repo=std::filesystem::path(__FILE__).parent_path().parent_path();
  wchar_t system[32768]{};GetSystemDirectoryW(system,32768);
  const auto ps=std::filesystem::path(system)/L"WindowsPowerShell/v1.0/powershell.exe";
  Child client;client.value=launch(ps.wstring(),L"-NoProfile -ExecutionPolicy Bypass -File \""+(repo/L"tools/steam_appearance_process_identity_selftest.ps1").wstring()+L"\" -Out \""+(root/L"bridge").wstring()+L"\" -ProcessQuery \""+inventoryHost.processQuery()+L"\" -ExpectedPid "+std::to_wstring(GetCurrentProcessId())+L" -BackendPid "+std::to_wstring(GetProcessId(css.value))+L" -EditorPid "+std::to_wstring(GetProcessId(editor.value)));
  check(WaitForSingleObject(client.value,20000)==WAIT_OBJECT_0,"cross-process client stays inside its owned bounded lifetime");
  DWORD code=1;GetExitCodeProcess(client.value,&code);inventoryHost.shutdown();
  check(code==0&&served==4,"actual CSS worker serves real PS identities and rejects Unknown without private scans");
  check(running(css.value)&&running(editor.value),"closing the inventory host retains backend and editor processes");
 }


 {
  const auto steamImage=root/L"steam.exe";std::filesystem::copy_file(self(),steamImage,std::filesystem::copy_options::overwrite_existing);
  Child steam;steam.value=launch(steamImage.wstring(),L"--dummy");FILETIME born{},exit{},kernel{},user{};
  check(GetProcessTimes(steam.value,&born,&exit,&kernel,&user),"owned Steam identity has an exact birth receipt");
  steamsession::Session session{root.wstring(),GetProcessId(steam.value),123,(ULONGLONG(born.dwHighDateTime)<<32)|born.dwLowDateTime};
  const auto link=root/L"lifecycle-backend.path";{std::ofstream out(link);out<<backend.string();}
  std::atomic<bool> allowed=false;std::atomic<ULONGLONG> power=1;unsigned launches=0;
  steamcssuse::Runtime use(false,35821,[]{return steamcsseditor::State::Absent;});
  check(use.initialize(entry.wstring(),link.wstring(),180000,4000,[&](const auto&stage,const auto&,const auto&,const auto&){if(stage=="ensure-helper-start"||stage=="automatic-helper-start")++launches;},0,[&]{return allowed.load();},[&]{return power.load();}),"actual use worker initializes with the shared lifecycle predicate");
  use.observeSteam(session);bool rejected=false;try{use.begin(session);}catch(...){rejected=true;}
  check(rejected&&launches==0,"non-Ready cannot acquire a CSS use or create a helper");
  allowed=true;const auto ticket=use.begin(session);check(ticket.token!=0,"Ready uses the existing exact Steam and resident backend");
  allowed=false;rejected=false;try{use.ensure(ticket.token);}catch(...){rejected=true;}
  check(rejected&&launches==0,"a lease acquired before suspension cannot launch Ensure afterwards");
  power=2;allowed=true;check(use.invalidateForResume(session),"committed Ready revokes the old bounded use while retaining CSS");
  rejected=false;try{use.ensure(ticket.token);}catch(...){rejected=true;}
  check(rejected&&launches==0&&running(css.value),"a stale pre-sleep ticket cannot survive Ready revocation");
  const auto currentTicket=use.begin(session);
  check(!use.invalidateForResume(session),"automatic recovery cannot revoke a new user edit in the current power generation");
  check(use.release(currentTicket.token).ok(),"the current user edit retains its valid completion ticket");
  const auto offTicket=use.begin(session);
  check(use.invalidateForResume(session,false)&&use.release(offTicket.token).stale,"explicit OFF or backend loss revokes even a current edit without touching CSS");
  use.shutdown();check(running(css.value)&&running(steam.value),"lifecycle cleanup closes only owned work and leaves both resident processes alive");
 }
 DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 for(unsigned i=0;i<10;++i){steamcssuse::Group group({},[]{return steamcsseditor::State::Absent;});group.open();group.adopt(backend.wstring());auto result=group.close();check(result.verified==1&&result.ok()&&running(css.value),"verified existing backend survives request release");}
 {steamcssuse::Group group({},[]{return steamcsseditor::State::Absent;});group.open();Child helper;helper.value=launch(self(),L"--dummy",&group);group.adopt(backend.wstring());group.close();check(WaitForSingleObject(helper.value,2000)==WAIT_OBJECT_0,"helper job still destroys its own pending work");check(running(css.value),"helper job never owns the adopted resident CSS");}
 for(auto state:{steamcsseditor::State::Active,steamcsseditor::State::Unknown}){steamcssuse::Group group({},[state]{return state;});group.open();Child helper;helper.value=launch(self(),L"--dummy",&group);auto result=group.close();check(result.editorProtected&&WaitForSingleObject(helper.value,2000)==WAIT_OBJECT_0&&running(css.value),"editor takeover retains CSS while bounding helper lifetime");}
 GetProcessHandleCount(GetCurrentProcess(),&after);check(after<=before+1,"repeated use releases all observation and job handles");
 for(const bool confirmed:{true,false}) {
  auto link=root/(confirmed?L"resident-ready.txt":L"resident-unconfirmed.txt");std::ofstream(link)<<backend.string();
  std::atomic<unsigned> probes{0},rebinds{0},uses{0};steamcss::Runtime observer;steamcss::Config config;
  config.linkFile=link.wstring();config.delays={1,2,3};config.modeDelays={1,2,3};
  auto existing=[&]{HANDLE copy=nullptr;DuplicateHandle(GetCurrentProcess(),css.value,GetCurrentProcess(),&copy,SYNCHRONIZE,FALSE,0);return steamcss::Result{copy,GetProcessId(css.value),"running",{},false};};
  config.ensure=[&](const auto&,const auto&,const auto&){++probes;return existing();};
  config.probeExisting=[&]{++rebinds;return existing();};
  config.confirmIdle=[&](const auto&,bool,bool){++uses;return CreateEventW(nullptr,TRUE,TRUE,nullptr);};
  config.isResidentConfirmed=[&](const auto&){return confirmed;};config.readMode=[](const auto&){return steamcss::UiMode::BigPicture;};
  check(observer.initialize(config),"shared CSS monitor initializes without a real Steam probe");
  observer.observeSteam({root.wstring(),42,0,100});auto deadline=GetTickCount64()+3000;
  while(observer.snapshot().reason!=(confirmed?"appearance-applied-resident":"appearance-unconfirmed")&&GetTickCount64()<deadline)Sleep(5);
  check(observer.snapshot().phase==(confirmed?"ready":"waiting-appearance"),"shared monitor publishes readiness only for an actually confirmed resident use");
  Sleep(100);check(rebinds==1&&probes==1&&uses==1,"helper completion rebinds the exit handle exactly once without another search loop");
  observer.shutdown();check(running(css.value),"shared monitor shutdown closes handles and retains the resident backend");
 }
 DWORD values[8]{0x594D4353,2,8,0,31,1,0,0};steamcssuse::Cleanup valid;check(steamcssuse::readAutomaticResult(values,valid),"resident result protocol accepted");valid.helperExitKnown=true;steamcssuse::requireAutomaticConfirmation(valid,true);check(!valid.error,"resident completion requires all identity and DOM flags");
 values[1]=1;steamcssuse::Cleanup old;check(!steamcssuse::readAutomaticResult(values,old),"old backend-exit protocol cannot masquerade as resident confirmation");
 values[1]=2;values[4]=3;steamcssuse::Cleanup partial;steamcssuse::readAutomaticResult(values,partial);partial.helperExitKnown=true;steamcssuse::requireAutomaticConfirmation(partial,true);check(partial.error&&partial.helperFailureCode==12,"partial confirmation is never reported as applied");
 std::ofstream(root/L"result.json")<<"{\"ok\":true,\"passed\":"<<checks<<",\"realSteamOperations\":0,\"realCssOperations\":0}";
 return 0;
#endif
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}

#endif
