// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_link_admission.h"
#include "steam_cssloader_guard.h"
#include "decky_sidebar_runtime.h"
#include "decky_update_recovery.h"
#include "decky_gaming_fullscreen.h"
#include "json.hpp"
#include <iostream>
#include <fstream>
using json=nlohmann::json;
using Session=ymcc::steamsession::Session;
static std::atomic<bool> g_exitRequested{false};
static struct {bool isolated=false;} g_aiFanMockSession;
static ymcc::steamlink::Admission g_steamLinkAdmission{100};
static ymcc::deckyupdate::Recovery g_deckyUpdateRecovery;
static std::wstring POWER_CONTROL_DIR=L"C:\\inert-fixture";
namespace ymcc::deckymirror {inline std::string frontendBuildId(const std::wstring&){return std::string(64,'b');}}
namespace ymcc::deckyidentity {inline const char* version="v3.3.0";}
static struct {Session session;Session sessionSnapshot(){return session;}} g_steamSettingsObserver;
static ymcc::deckygaming::Observation gamingFullscreenObservation;
static ymcc::deckygaming::Observation deckySidebarGamingFullscreenObservation(const Session&){return gamingFullscreenObservation;}
enum class PowerLifecycle{Suspending,Suspended,Resuming,Ready};
static std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned> probes{0},helpers{0},modeReads{0};
static unsigned failures=0,checks=0,consumerWakes=0;
static HANDLE helperLease=nullptr;
static struct { HANDLE confirmAndIdle(const Session&,bool,bool){++helpers;HANDLE duplicate=nullptr;DuplicateHandle(GetCurrentProcess(),helperLease,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS);return duplicate;} } g_steamCssUse;
static struct {std::mutex mutex;json state;void requestRefresh(const Session&){} void set(json value){std::lock_guard lock(mutex);state=std::move(value);}json current(){std::lock_guard lock(mutex);return state;}json snapshot(const Session&){++modeReads;return current();}} g_steamModeObserver;
static struct {ymcc::deckysidebar::State state;ymcc::deckysidebar::State snapshot(){return state;}bool requestLaunchAdmissionRefresh(){++consumerWakes;return true;}} g_deckySidebarRuntime;
static struct {ymcc::steamcss::Runtime* actual=nullptr;void observeMode(const Session& s,const ymcc::steamcss::ModeSample& m){if(actual)actual->observeMode(s,m);}} g_steamCssGuard;
static void deckySidebarDiagnostic(const char*,const json&,bool=false){}
static void deckySidebarPost(const char*,const json&){}
static void deckySidebarSessionDiagnostic(const Session&,const char*){}
namespace ymcc::steamcss {inline Result fixtureProbeBackend(){++probes;return {nullptr,0,"missing",{},false};}}
// Only external process IO is replaced. The four lambdas below are extracted
// exactly from production main.cpp, not rewritten as a parallel policy.
#define probeBackend fixtureProbeBackend
#include "production-startup-callbacks.h"
#undef probeBackend
static void check(bool value,const char* label){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';}
template<class F>static bool until(F condition){const auto end=GetTickCount64()+2500;while(!condition()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
static void signal(const Session& s,const char* mode,const char* epoch="document-a:1",bool loaded=true,bool update=false){
 json state={{"known",std::string(mode)!="unknown"},{"running",true},{"mode",mode},{"pid",s.pid},{"birth",std::to_string(s.created)},{"modeEpoch",epoch},{"deckyFrontendLoaded",loaded}};
 if(update)state["deckyUpdateResidue"]={{"protocol",1},{"active",true},{"bridgePresent",true},{"frontendBuildId",std::string(64,'a')}};
 g_steamModeObserver.set(state);productionModeChanged(s,state);
}
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("isolated artifact directory required");
 const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
 Session old{L"c:\\inert-steam",1,7,90},newer{L"c:\\inert-steam",2,7,110};
 helperLease=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!helperLease)throw std::runtime_error("fixture event unavailable");
 ymcc::steamcss::Config cssGuard;productionCssCallbacks(cssGuard);const auto valid=[] {return true;};
 g_steamSettingsObserver.session=old;g_deckySidebarRuntime.state.enabled=true;
 signal(old,"bigpicture");
 auto first=cssGuard.ensure({},old,valid);check(first.phase=="missing"&&probes==1,"already running Big Picture permits a CSS-only probe even without a manual Steam startup grant");
 auto ticket=cssGuard.confirmIdle(old,true,true);check(ticket&&helpers==1,"already running Big Picture can apply enabled appearance once");if(ticket)CloseHandle(ticket);
 auto sidebar=productionSidebarAdmission();check(!sidebar.allowed&&!sidebar.retryable,"ordinary startup still refuses unrequested Decky reinjection");
 // Field: Xbox Full Screen starts Steam before YMCC; system FSE is the
 // explicit independent admission, never process age or a generic saved ON.
 gamingFullscreenObservation={true,true,true,true};g_deckySidebarRuntime.state.enabled=true;
 signal(old,"bigpicture","xbox-document:1",false);
 check(productionSidebarAdmission().allowed,"Xbox full-screen admits missing Decky when Steam predates YMCC");
 for(unsigned i=0;i<4;++i){auto prior=gamingFullscreenObservation;if(i==0)gamingFullscreenObservation.apiAvailable=false;if(i==1)gamingFullscreenObservation.supported=false;if(i==2)gamingFullscreenObservation.active=false;if(i==3)gamingFullscreenObservation.sameWindowsSession=false;check(!productionSidebarAdmission().allowed,"missing/unsupported/inactive/foreign-session FSE never admits");gamingFullscreenObservation=prior;}
 g_deckySidebarRuntime.state.enabled=false;check(!productionSidebarAdmission().allowed,"Xbox FSE cannot override user or fault OFF");g_deckySidebarRuntime.state.enabled=true;
 g_powerLifecycle=PowerLifecycle::Suspended;check(!productionSidebarAdmission().allowed,"Xbox FSE waits for committed native Ready");g_powerLifecycle=PowerLifecycle::Ready;
 g_exitRequested=true;check(!productionSidebarAdmission().allowed,"Xbox FSE cannot revive exiting YMCC");g_exitRequested=false;
 for(const auto bad:{"pid","birth","known","running","mode"}){auto mode=g_steamModeObserver.current();const auto original=mode;if(std::string(bad)=="pid")mode[bad]=999u;if(std::string(bad)=="birth")mode[bad]="stale";if(std::string(bad)=="known"||std::string(bad)=="running")mode[bad]=false;if(std::string(bad)=="mode")mode[bad]="desktop";g_steamModeObserver.set(mode);check(!productionSidebarAdmission().allowed,"Xbox FSE rejects stale session and unconfirmed/non-BPM state");g_steamModeObserver.set(original);}
 {auto mode=g_steamModeObserver.current();mode["deckyFrontendLoaded"]=true;g_steamModeObserver.set(mode);check(!productionSidebarAdmission().allowed&&productionSidebarAdmission().reason=="decky-context-already-loaded","Xbox FSE does not reinject an already loaded page");mode.erase("deckyFrontendLoaded");g_steamModeObserver.set(mode);check(!productionSidebarAdmission().allowed,"Xbox FSE preserves unknown frontend state veto");}
 check(!g_steamLinkAdmission.allows(old),"FSE never grants shared CSS or Steam mode/start authority");
 gamingFullscreenObservation={};g_deckySidebarRuntime.state.enabled=true;
 signal(old,"bigpicture","document-a:1",true,true);
 check(g_deckyUpdateRecovery.allows(old),"field update mismatch reaches the actual Decky recovery grant");
 sidebar=productionSidebarAdmission();check(!sidebar.allowed&&!sidebar.retryable&&sidebar.reason=="decky-context-already-loaded","update recovery cannot start a second Loader on a loaded Steam document");
 g_steamLinkAdmission.manual(old);check(!productionSidebarAdmission().allowed,"manual Steam linkage grant cannot override the loaded-context no-restart veto");
 signal(old,"bigpicture","document-a:1",false);check(productionSidebarAdmission().allowed,"a confirmed fresh context retains a safe first Loader start");
 auto state=g_steamModeObserver.current();state.erase("deckyFrontendLoaded");g_steamModeObserver.set(state);check(!productionSidebarAdmission().allowed,"unknown context state never grants possibly destructive Loader injection");
 const unsigned priorProbes=probes.load(),priorHelpers=helpers.load();
 for(const auto mode:{"desktop","unknown"}){signal(old,mode);const auto result=cssGuard.ensure({},old,valid);auto lease=cssGuard.confirmIdle(old,true,true);if(lease)CloseHandle(lease);check(result.phase=="waiting-steam-ui"&&!lease,"desktop and unknown mode perform no CSS start or Steam mode action");}
 check(probes==priorProbes&&helpers==priorHelpers,"unconfirmed modes stop before any backend probe or helper work");
 signal(old,"bigpicture");auto currentAccount=old;++currentAccount.account;g_steamSettingsObserver.session=currentAccount;
 check(cssGuard.ensure({},old,valid).phase=="missing","Steam account metadata changes do not permanently block a process-bound CSS use");g_steamSettingsObserver.session=old;
 signal(old,"bigpicture");Session reused=old;++reused.created;auto stale=cssGuard.ensure({},reused,valid);check(stale.phase=="waiting-steam-ui","PID reuse or stale Steam identity cannot authorize appearance");
 g_exitRequested=true;check(!cssGuard.confirmIdle(old,true,true),"application exit cancels pending appearance use");g_exitRequested=false;
 g_aiFanMockSession.isolated=true;check(cssGuard.ensure({},old,valid).phase=="waiting-steam-ui","device isolation blocks appearance side effects");g_aiFanMockSession.isolated=false;
 // Run the production CSS worker with the exact production callback chain.
 Session existing{L"c:\\inert-steam",9,7,80};g_steamSettingsObserver.session=existing;g_steamLinkAdmission.observe(existing);
 const auto link=out/L"links"/L"loader.txt";std::filesystem::create_directories(link.parent_path());{std::ofstream f(link);f<<"C:\\isolated\\CssLoader-Standalone-Headless.exe";}
 ymcc::steamcss::Runtime runtime;g_steamCssGuard.actual=&runtime;cssGuard.linkFile=link.wstring();cssGuard.delays={10,20,30};cssGuard.modeDelays={1,5,20};
 cssGuard.readMode=[](const Session&){const auto s=g_steamModeObserver.current();const auto m=s.value("mode",std::string{});return ymcc::steamcss::ModeSample{m=="bigpicture"?ymcc::steamcss::UiMode::BigPicture:m=="desktop"?ymcc::steamcss::UiMode::Desktop:ymcc::steamcss::UiMode::Unknown,s.value("modeEpoch",std::string{})};};
 cssGuard.mayLaunch=[] {return !g_exitRequested.load();};
 signal(existing,"bigpicture");const auto baselineProbes=probes.load(),baselineHelpers=helpers.load();
 check(runtime.initialize(cssGuard),"actual CSS worker initializes in remembered-enabled existing Steam fixture");runtime.observeSteam(existing);signal(existing,"bigpicture");
 check(until([&]{return helpers==baselineHelpers+1;}),"actual worker automatically applies appearance to the first pre-existing Big Picture observation");
 for(unsigned i=0;i<1000;++i)signal(existing,"bigpicture");Sleep(60);
 check(helpers==baselineHelpers+1&&probes==baselineProbes+1,"1000 repeated mode/window observations do not repeat CSS helpers or discovery");
 check(runtime.snapshot().steamRunning&&runtime.snapshot().linked,"appearance state retains truthful existing Steam and enabled selections");
 signal(existing,"bigpicture","document-b:1");check(until([&]{return helpers==baselineHelpers+2;}),"a replaced document gets one CSS reapply while Steam PID and Big Picture mode remain unchanged");
 for(unsigned i=0;i<100;++i)signal(existing,"bigpicture","document-b:1");Sleep(40);check(helpers==baselineHelpers+2,"duplicate new-document notifications cannot repeat the CSS reapply");
 signal(existing,"desktop","document-b:1");Sleep(15);signal(existing,"bigpicture","document-b:2");
 check(until([&]{return helpers==baselineHelpers+3;}),"a real desktop-to-Big-Picture entry still reapplies enabled appearance once");
 std::filesystem::remove(link);check(until([&]{return runtime.snapshot().phase=="disabled";}),"appearance OFF is observed through the existing directory event");
 for(unsigned i=0;i<100;++i)signal(existing,"bigpicture","document-c:1");Sleep(30);check(helpers==baselineHelpers+3,"disabled appearance cannot be reenabled by a document or mode event");
 runtime.shutdown();{std::ofstream f(link);f<<"C:\\isolated\\CssLoader-Standalone-Headless.exe";}
 signal(existing,"desktop","manual-document:1");const auto beforeManual=helpers.load();
 check(runtime.initialize(cssGuard),"manual-transition fixture starts with actual desktop mode");runtime.observeSteam(existing);
 check(until([&]{return runtime.snapshot().phase=="waiting-steam-ui";}),"desktop startup parks without CSS or mode side effects");
 runtime.requestManualRecovery();check(until([&]{return runtime.snapshot().reason=="appearance-needs-confirmed-bigpicture";}),"manual transition retains the current-mode admission fence");
 signal(existing,"bigpicture","manual-document:2");
 check(until([&]{return helpers==beforeManual+1;}),"first confirmed BPM resumes a parked manual request instead of losing appearance application");
 for(unsigned i=0;i<100;++i)signal(existing,"bigpicture","manual-document:2");Sleep(40);
 check(helpers==beforeManual+1,"a completed manual transition does not schedule a second CSS use");
 g_steamCssGuard.actual=nullptr;runtime.shutdown();
 ymcc::steamlink::Admission restarted(120);restarted.observeMode(old,"bigpicture");check(!restarted.allows(old),"YMCC restart does not turn a CSS reconciliation into a Decky startup grant");
 signal(newer,"bigpicture","new-steam:1",true);g_steamSettingsObserver.session=newer;g_steamLinkAdmission.observe(newer);
 check(!productionSidebarAdmission().allowed,"a genuinely newer Steam process still cannot override an already loaded document");
 // Exact launch admission must wait for the same account identity that the
 // authenticated binding already requires, before resource/worker preparation.
 auto noAccount=newer;noAccount.account=0;g_steamSettingsObserver.session=noAccount;
 signal(noAccount,"bigpicture","new-steam:2",false);const auto readsBeforeAccountWait=modeReads.load();
 const auto accountWait=productionSidebarAdmission();
 check(!accountWait.allowed&&accountWait.retryable&&accountWait.reason=="steam-session-unconfirmed","account-pending startup waits before preparing resources or a binding worker");
 check(modeReads==readsBeforeAccountWait,"account-pending admission does not copy or query shared UI state");
 g_steamSettingsObserver.session=newer;signal(newer,"bigpicture","new-steam:2",false);
 check(productionSidebarAdmission().allowed,"real account readiness immediately restores existing permitted startup");
 CloseHandle(helperLease);helperLease=nullptr;
 const json receipt={{"ok",failures==0},{"checks",checks},{"failures",failures},{"actualSteamActions",0},{"actualCssLaunches",0},{"source","Four exact production callbacks plus actual CSS worker; only external IO replaced"}};
 std::ofstream(out/L"result.json")<<receipt.dump(2)<<'\n';std::cout<<receipt.dump()<<'\n';return failures?1:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
