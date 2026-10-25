// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
// Filled with exact main.cpp callback bodies by run.mjs; external IO is inert only.
// Fixture-only observation seam. The wrapper calls the original unmodified
// normal creator once and returns its exact Result and PROCESS_INFORMATION.
#include "decky_sidebar_launch.h"
#include "json.hpp"
#include <atomic>
#include <mutex>
#include <fstream>
namespace fixtureStage {
using Json=nlohmann::json;
static std::mutex mutex;
static std::atomic<bool> active{false};
static std::atomic<unsigned> attempt{0},dropped{0};
static Json events=Json::array();
static std::filesystem::path destination,ownedDirectory;
static void record(const char* stage,Json detail=Json::object()) noexcept {
    if(!active.load())return;
    const DWORD saved=GetLastError();
    try { std::lock_guard lock(mutex);if(events.size()<2048)
        events.push_back({{"sequence",events.size()+1},{"monotonicMs",GetTickCount64()},
            {"threadId",GetCurrentThreadId()},{"preflightOrdinal",attempt.load()},
            {"stage",stage},{"detail",std::move(detail)}});
        else ++dropped;
    }catch(...){++dropped;}
    SetLastError(saved);
}
template<class Callback>static void safely(Callback&& callback) noexcept {
    const DWORD saved=GetLastError();try{callback();}catch(...){++dropped;}SetLastError(saved);
}
static void begin(const std::filesystem::path& directory,unsigned oldLaunches,unsigned oldPreflights){
    {std::lock_guard lock(mutex);events=Json::array();destination=directory/L"native-stage-observation.json";ownedDirectory=directory;}
    attempt=0;dropped=0;active=true;
    record("retained-scenario.begin",{{"oldLaunches",oldLaunches},{"oldPreflights",oldPreflights}});
}
static bool flush(const char* checkpoint) noexcept {
    if(!active.load())return false;
    const DWORD saved=GetLastError();bool ok=false;
    try { Json copy;{std::lock_guard lock(mutex);copy={{"schemaVersion",1},{"observationOnly",true},
        {"ownedDirectory",ownedDirectory.string()},{"scenario","retained-normal-create"},
        {"checkpoint",checkpoint},{"ordinalMeaning","observed preflight count, not a request or generation identity; earlier admission is unbound"},{"snapshotAtomicAcrossThreads",false},{"pollBudgetMs",4000},{"nativeBudgetMs",20000},
        {"eventCount",events.size()},{"dropped",dropped.load()},{"events",events}};}
        std::ofstream output(destination,std::ios::binary|std::ios::trunc);output<<copy.dump(2)<<'\n';output.flush();ok=output.good();
    }catch(...){}
    SetLastError(saved);return ok;
}
}
// Build diagnostic fields inside the exception boundary, including after a
// successful create: observation failure cannot strand its returned handles.
#define FIXTURE_STAGE(...) fixtureStage::safely([&]{fixtureStage::record(__VA_ARGS__);})
namespace ymcc::fixtureStageLaunch {
inline ymcc::deckylaunch::Result create(const std::wstring& executable,std::wstring& command,wchar_t* environment,
        const std::wstring& directory,STARTUPINFOW& startup,PROCESS_INFORMATION& child){
    if(!fixtureStage::active.load())return ymcc::deckylaunch::create(executable,command,environment,directory,startup,child);
    FIXTURE_STAGE("owned-normal-create.begin");
    const auto creatorStartedAtMs=GetTickCount64();
    auto result=ymcc::deckylaunch::create(executable,command,environment,directory,startup,child);
    const DWORD saved=GetLastError();const auto creatorReturnedAtMs=GetTickCount64();FILETIME born{},exited{},kernel{},user{};
    const bool hasBirth=result.created&&child.hProcess&&GetProcessTimes(child.hProcess,&born,&exited,&kernel,&user);
    FIXTURE_STAGE("owned-normal-create.return",{{"creatorStartedAtMs",creatorStartedAtMs},{"creatorReturnedAtMs",creatorReturnedAtMs},{"creatorElapsedMs",creatorReturnedAtMs-creatorStartedAtMs},{"created",result.created},{"error",result.error},
        {"launchStage",result.stage},{"launchMode",result.mode},{"pid",result.created?child.dwProcessId:0},
        {"birthKnown",hasBirth},{"birth",hasBirth?std::to_string((static_cast<unsigned long long>(born.dwHighDateTime)<<32)|born.dwLowDateTime):std::string{}}});
    SetLastError(saved);return result;
}
}
// decky_sidebar_launch.h is already included; only Runtime's qualified call is
// redirected through the observer. Launch policy and token checks are unchanged;
// extra identity queries use only the returned owned child handle. Timing overhead
// is diagnostic and is never treated as proof that the old failure is repaired.
#define deckylaunch fixtureStageLaunch
#include "decky_sidebar_runtime.h"
#undef deckylaunch
#include "decky_sidebar_setup.h"
#include "decky_sidebar_broker.h"
#include "decky_sidebar_bootstrap.h"
#include "steam_ui_mode_observer.h"
#include "steam_link_admission.h"
#include "decky_update_recovery.h"
#include "decky_gaming_fullscreen.h"
#include "decky_sidebar_manual_repair.h"
#include "decky_sidebar_automatic_repair.h"
#include <fstream>
#include <iostream>
#include <condition_variable>
using json=nlohmann::json;
using Session=ymcc::steamsession::Session;
static Session steam{L"C:\\FixtureSteam",45640,0,134368675855384222ULL};
static std::atomic<int> fixtureSteamPresence{1};
struct SessionFixture{Session sessionSnapshot(){return steam;}int steamPresence(){return fixtureSteamPresence.load();}}g_steamSettingsObserver;
static ymcc::deckygaming::Observation gamingFullscreenObservation;
static std::atomic<bool> gamingFullscreenLive{false};
static ymcc::deckygaming::Observation deckySidebarGamingFullscreenObservation(const Session&){auto observation=gamingFullscreenObservation;observation.active=gamingFullscreenLive.load();return observation;}
namespace ymcc::steamcss {enum class UiMode {BigPicture,Desktop};struct Mode{UiMode value;std::string epoch;};}
struct CssFixture{void observeMode(const Session&,const ymcc::steamcss::Mode&){} }g_steamCssGuard;
static std::atomic<bool>g_exitRequested{false};
static std::wstring POWER_CONTROL_DIR;
static ymcc::steamlink::Admission g_steamLinkAdmission{134369000000000000ULL};
static ymcc::deckyupdate::Recovery g_deckyUpdateRecovery;
static ymcc::deckysidebar::Runtime g_deckySidebarRuntime;
static ymcc::steamui::ModeObserver g_steamModeObserver;
static std::atomic<unsigned> launches{0},signals{0},preflights{0};
enum class PowerLifecycle{Suspending,Suspended,Resuming,Ready};
static std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool>g_inputReady{false};
static unsigned long long fixtureWakeGeneration=0;
static unsigned fixtureWakeStops=0,fixtureWakeInputPosts=0;
static json fixtureWakeEvents=json::array(),fixtureWakeDiagnostics=json::array();
static unsigned long long currentPowerGeneration(){return fixtureWakeGeneration;}
// The CSS consumer of this shared Ready body is an explicit inert external
// port here. Its exact production callback and real worker run separately in
// steam_cssloader_resume_selftest; this Decky fixture never claims CSS recovery.
static std::atomic<unsigned> fixtureCssReadyHints{0};
static bool steamAppearanceResumeCommitted(unsigned long long){++fixtureCssReadyHints;return false;}
static void stopPowerResumeWatchdog(){++fixtureWakeStops;}
static void ipc_emit(const std::string& event,const json& data){fixtureWakeEvents.push_back({{"event",event},{"data",data}});}
static std::atomic<unsigned> fixturePreparations{0},fixturePrepared{0};
static std::atomic<bool> fixtureResourceValid{true};
static void deckySidebarDiagnostic(const char* event,const json& data,bool){++signals;if(std::string(event)=="wake-connection-check")fixtureWakeDiagnostics.push_back(data);if(std::string(event)=="prewarm"){++fixturePreparations;if(data.value("ready",false))++fixturePrepared;}}
static json deckySidebarEnvironment(bool prepare,const std::function<bool()>& mayPrepare={}) {
    const auto root=std::filesystem::path(POWER_CONTROL_DIR),steamRoot=root/L"fixture-steam",home=root/L"decky";
    const auto digest=[](const std::wstring&){return fixtureResourceValid.load()?std::string(ymcc::deckysetup::loaderSha):std::string(64,'0');};
    return prepare?ymcc::deckysetup::prepare(steamRoot,home,digest,fixtureSteamPresence.load()!=0,mayPrepare):ymcc::deckysetup::inspect(steamRoot,home,digest);
}
/*__PREPARE_ENVIRONMENT__*/
/*__WAKE_MESSAGE__*/
static BOOL fixtureWakePost(HWND,UINT message,WPARAM generation,LPARAM){if(message!=WM_POWER_INPUT_RECOVER||generation!=fixtureWakeGeneration)throw std::runtime_error("wrong wake follow-up boundary");++fixtureWakeInputPosts;return TRUE;}
#define PostMessageW fixtureWakePost
static LRESULT productionWakeCommit(WPARAM w){HWND h=nullptr;
/*__WAKE_COMMIT__*/
}
#undef PostMessageW
static void deckySidebarPost(const std::string&,const json&){}
/*__GAMING_CONSENT__*/
static ymcc::deckysidebar::LaunchAdmission productionAdmission(){
/*__ADMISSION__*/
}
static void productionModeChanged(const Session& session,const json& state){
/*__MODE_CHANGED__*/
}
int wmain(int argc,wchar_t** argv){
 if(argc==1){Sleep(15000);return 0;} // Only this exact inert test image can be launched.
 unsigned checks=0;const auto check=[&](bool ok,const char* s){if(!ok){if(fixtureStage::active.load()){
    const auto state=g_deckySidebarRuntime.snapshot();
    FIXTURE_STAGE("assert.failure.before-cleanup",{{"assertion",s},{"phase",state.phase},{"reason",state.reason},
        {"error",state.error},{"pid",state.pid},{"revision",state.revision},{"launches",launches.load()},
        {"preflights",preflights.load()},{"allows",g_deckyUpdateRecovery.allows(steam)}});
    fixtureStage::flush("ASSERTION_FAILED_BEFORE_CLEANUP");}
    throw std::runtime_error(s);}++checks;};
 const auto until=[](auto fn,unsigned ms=4000){const auto end=GetTickCount64()+ms;while(!fn()){if(GetTickCount64()>=end)return false;Sleep(3);}return true;};
 try{
  const auto scope=std::filesystem::absolute(argv[1]).lexically_normal();wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);
  check(scope.filename()==L"recovery-fixture"&&scope.parent_path()==std::filesystem::path(image).parent_path(),"task-only fixture owned directory");
  POWER_CONTROL_DIR=scope.wstring();std::filesystem::create_directories(scope/L"home");std::filesystem::create_directories(scope/L"decky/plugins/ymcc-sidebar/dist");
  std::ifstream fieldIn(argv[2]);json field;fieldIn>>field;const auto oldId=field["observed"]["oldFrontendBuildId"].get<std::string>(),newId=field["observed"]["installedFrontendBuildId"].get<std::string>();
  std::ofstream(scope/L"decky/plugins/ymcc-sidebar/dist/index.js")<<"// YMCC_DECKY_FRONTEND_BUILD_ID="<<newId<<"\n";
  const json residue={{"protocol",1},{"active",true},{"bridgePresent",true},{"frontendBuildId",oldId}};
  const json healthyMode={{"known",true},{"running",true},{"pid",steam.pid},{"birth",std::to_string(steam.created)},{"mode","bigpicture"},{"modeEpoch","fixture-document:1"},{"deckyFrontendLoaded",true},{"deckyUpdateResidue",residue}};
  // 2026-10-22: the loaded module outlived its dead official backend. Replay
  // the exact product orchestrator; only process/transport IO is inert.
  {
    auto session=steam;auto mode=healthyMode;mode["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
    json state={{"enabled",true},{"pid",0},{"phase","waiting-manual-steam"},{"reason","decky-context-already-loaded"},{"revision",3u}};
    json args={{"userInitiated",true},{"pid",session.pid},{"birth",std::to_string(session.created)},{"revision",3u}};
    unsigned dispatches=0,refreshes=0,consents=0;unsigned long long now=100;std::string installed=newId,preflight;bool exiting=false;
    ymcc::deckyupdate::Recovery policy;
    ymcc::deckyrepair::Dependencies deps{false,[&]{return session;},[&]{return state;},[&](const auto&){return mode;},[&]{return installed;},ymcc::deckyidentity::version,
        [&]{return preflight;},[&]{return exiting;},[&](const auto& s){++consents;return policy.allowManualRepair(s);},
        [&](const auto&,const auto& action,const auto& admit){++dispatches;check(action.at("operation")=="decky.repairConnection","only repair operation dispatched");
            if(!admit())return json{{"ok",false},{"reason","live-action-cancelled"},{"mutated",false}};
            ++refreshes;return json{{"ok",true},{"requestDispatched",true},{"mutated",true}};},[&]{return now;}};
    for(const auto bad:{"missing-consent","stale-pid","stale-birth","stale-revision","extra-arg","OFF","healthy","loaded-pid","foreign-mode","inactive-plugin","different-loader","port-in-use","hash-failed","isolated","exiting"}){
      auto request=args,savedState=state,savedMode=mode;const std::string kind=bad;
      if(kind=="missing-consent")request.erase("userInitiated");if(kind=="stale-pid")request["pid"]=1u;if(kind=="stale-birth")request["birth"]="different";
      if(kind=="stale-revision")request["revision"]=2u;if(kind=="extra-arg")request["force"]=true;if(kind=="OFF")state["enabled"]=false;
      if(kind=="healthy")state["phase"]="loader-started";if(kind=="loaded-pid")state["pid"]=7u;if(kind=="foreign-mode")mode["mode"]="desktop";
      if(kind=="inactive-plugin")mode["deckyUpdateResidue"]["active"]=false;if(kind=="different-loader")mode["deckyUpdateResidue"]["loaderVersion"]="v3.2.10";
      if(kind=="port-in-use")preflight="loader-port-in-use";if(kind=="hash-failed")preflight="resource-hash-mismatch";
      if(kind=="isolated")deps.isolated=true;if(kind=="exiting")exiting=true;
      ymcc::deckyrepair::ManualRepair repair;check(!repair.request(request,deps).value("ok",true)&&dispatches==0&&refreshes==0&&consents==0,bad);
      state=savedState;mode=savedMode;preflight.clear();deps.isolated=false;exiting=false;
    }
    ymcc::deckyrepair::ManualRepair repair;
    check(repair.request(args,deps).value("ok",false)&&dispatches==1&&refreshes==1&&policy.allows(steam),"explicit repair resumes only the current Steam document");
    check(!repair.request(args,deps).value("ok",true)&&dispatches==1,"double click does not dispatch a second context refresh");
    now+=30000;policy.veto();check(!repair.request(args,deps).value("ok",true)&&refreshes==1,"repair never resets a concurrent fault OFF veto");
    policy.userEnable(steam);now+=30000;
    const auto originalDispatch=deps.dispatch;
    deps.dispatch=[&](const auto& s,const auto& action,const auto& admit){state["enabled"]=false;return originalDispatch(s,action,admit);};
    check(!repair.request(args,deps).value("ok",true)&&refreshes==1,"OFF after discovery vetoes final dispatch");state["enabled"]=true;
    now+=30000;deps.dispatch=[&](const auto& s,const auto& action,const auto& admit){++session.created;return originalDispatch(s,action,admit);};
    check(!repair.request(args,deps).value("ok",true)&&refreshes==1,"Steam changed after discovery vetoes final dispatch");--session.created;
    now+=30000;deps.dispatch=[&](const auto& s,const auto& action,const auto& admit){installed=oldId;return originalDispatch(s,action,admit);};
    check(!repair.request(args,deps).value("ok",true)&&refreshes==1,"installed module changed after discovery vetoes final dispatch");installed=newId;
    now+=30000;deps.dispatch=[&](const auto&,const auto&,const auto& admit){check(admit(),"uncertain test dispatch admitted");return json{{"ok",false},{"mutated",true},{"reason","live-transport-uncertain"}};};
    check(repair.request(args,deps).value("mutated",false),"uncertain dispatch remains visible");const auto spent=consents;
    check(!repair.request(args,deps).value("ok",true)&&consents==spent,"uncertain reply never refunds or replays repair");
    state["phase"]="frontend-attached";state["reason"]="retained-frontend-connected";
    ymcc::deckyrepair::ManualRepair retainedRepair;deps.dispatch=originalDispatch;
    const auto beforeRetained=refreshes;
    check(retainedRepair.request(args,deps).value("ok",false)&&refreshes==beforeRetained+1,"retained bridge with absent owned backend permits explicit repair");
    for(const auto bad:{"foreign-reason","wrong-phase","owned-loader","OFF"}){
      const auto saved=state;const std::string kind=bad;
      if(kind=="foreign-reason")state["reason"]="different";if(kind=="wrong-phase")state["phase"]="loader-started";
      if(kind=="owned-loader")state["pid"]=7u;if(kind=="OFF")state["enabled"]=false;
      ymcc::deckyrepair::ManualRepair rejected;const auto dispatched=dispatches;
      check(!rejected.request(args,deps).value("ok",true)&&dispatches==dispatched,"retained repair keeps phase reason ownership and OFF boundaries");state=saved;
    }
    unsigned thrownDispatches=0;const auto beforeThrow=refreshes;
    deps.dispatch=[&](const auto&,const auto&,const auto& admit)->json{
      ++thrownDispatches;check(admit(),"throwing dispatch admitted");++refreshes;throw std::runtime_error("reply lost after refresh");};
    ymcc::deckyrepair::ManualRepair throwingRepair;const auto thrown=throwingRepair.request(args,deps);
    check(!thrown.value("ok",true)&&thrown.value("mutated",false)&&thrown.value("uncertain",false)&&refreshes==beforeThrow+1,"exception after dispatch preserves uncertain mutation");
    check(!throwingRepair.request(args,deps).value("ok",true)&&thrownDispatches==1,"exception after dispatch never refunds or repeats repair");
    const auto originalMode=deps.mode;deps.mode=[](const auto&)->json{throw std::runtime_error("read failed before dispatch");};
    ymcc::deckyrepair::ManualRepair beforeDispatch;const auto refused=beforeDispatch.request(args,deps);
    check(!refused.value("ok",true)&&!refused.value("mutated",true)&&!refused.value("uncertain",false)&&thrownDispatches==1,"read exception before transport remains non-mutating");
    deps.mode=originalMode;deps.dispatch=originalDispatch;
    ymcc::deckyrepair::AttemptBudget budget;check(budget.reserve(steam,100)&&!budget.reserve(steam,40000),"in-flight repair cannot overlap after cooldown");budget.finish();
    check(!budget.reserve(steam,99)&&!budget.reserve(steam,101),"clock reversal and immediate retry remain spent");
  }
  // Field 2026-10-22: automatic retained repair must never rebuild Steam UI.
  {
    auto session=steam;auto mode=healthyMode;mode["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
    json state={{"enabled",true},{"pid",0},{"phase","frontend-attached"},{"reason","retained-frontend-connected"},{"revision",7u}};
    unsigned dispatches=0,preflightReads=0,events=0,consents=0;unsigned long long now=100;bool sleeping=false,failPort=false;std::string installed=newId;
    ymcc::deckyrepair::Dependencies deps{false,[&]{return session;},[&]{return state;},[&](const auto&){return mode;},[&]{return installed;},ymcc::deckyidentity::version,
      [&]{++preflightReads;return failPort?std::string("loader-port-in-use"):std::string{};},[&]{return sleeping;},[&](const auto&){++consents;return true;},
      [&](const auto&,const auto&,const auto&)->json{++dispatches;return json{{"ok",true},{"mutated",true}};},[&]{return now;}};
    ymcc::deckyrepair::ManualRepair shared;ymcc::deckyrepair::AutomaticRepair automatic;
    sleeping=true;automatic.observe(shared,deps);check(dispatches==0,"raw wake while native power not ready performs no repair");sleeping=false;
    failPort=true;automatic.observe(shared,deps);check(dispatches==0,"live or foreign backend is never refreshed automatically");failPort=false;now+=30000;
    automatic.observe(shared,deps,[&]{++events;});
    check(dispatches==0&&consents==0&&events==2&&automatic.snapshot().value("phase",std::string{})=="manual-required"&&automatic.snapshot().value("reason",std::string{})=="steam-ui-refresh-requires-user","automatic retained repair cannot rebuild Steam UI or consume manual launch consent");
    check(!automatic.snapshot().value("attempted",true),"blocked automatic UI refresh is not reported as an actual dispatched attempt");
    const auto reads=preflightReads;
    for(unsigned i=0;i<100;++i){now+=30000;automatic.observe(shared,deps);}
    check(dispatches==0&&preflightReads==reads,"one automatic attempt per document survives duplicate events");
    json forged={{"automatic",true},{"pid",session.pid},{"birth",std::to_string(session.created)},{"revision",7u}};
    check(!shared.request(forged,deps).value("ok",true),"public manual consumer cannot accept forged automatic arguments");
    for(const auto document:{"new-document:1","after-wake-document:1","third-document:1"}){mode["modeEpoch"]=document;now+=30000;automatic.observe(shared,deps);}
    check(dispatches==0&&consents==0,"new documents and wake episodes cannot authorize automatic Steam UI rebuilds");
    automatic.confirmed(session);mode["modeEpoch"]="after-confirmed-document:1";now+=30000;automatic.observe(shared,deps);
    check(dispatches==0,"a prior healthy confirmation never authorizes the next automatic UI rebuild");
    ++session.created;mode["birth"]=std::to_string(session.created);now+=30000;automatic.observe(shared,deps);
    check(dispatches==0,"replacement Steam identity does not grant automatic UI refresh");
    state["pid"]=88u;const auto healthyReads=preflightReads;automatic.observe(shared,deps);check(dispatches==0&&preflightReads==healthyReads,"healthy owned Loader has no automatic polling or repair");
  }
  check(!ymcc::deckyupdate::handoffTicket(steam,true,"loader-started",123,true).empty(),"owned active Loader carries update even while panel has no peer");
  check(ymcc::deckyupdate::handoffTicket(steam,false,"loader-started",123,true).empty()&&ymcc::deckyupdate::handoffTicket(steam,true,"waiting-manual-steam",0,true).empty()&&ymcc::deckyupdate::handoffTicket(steam,true,"loader-started",123,false).empty(),"off/waiting/inactive broker cannot manufacture updater continuation");
  // Policy rejection matrix uses the exact production class, never a parallel model.
  for(const auto bad:{"disabled","same-build","foreign-pid","foreign-birth","desktop","unknown","inactive","missing-bridge","malformed"}){
    ymcc::deckyupdate::Recovery policy;auto state=healthyMode;bool enabled=true;std::string installed=newId;
    if(std::string(bad)=="disabled")enabled=false;if(std::string(bad)=="same-build")installed=oldId;if(std::string(bad)=="foreign-pid")state["pid"]=1;if(std::string(bad)=="foreign-birth")state["birth"]="different";
    if(std::string(bad)=="desktop")state["mode"]="desktop";if(std::string(bad)=="unknown")state["known"]=false;if(std::string(bad)=="inactive")state["deckyUpdateResidue"]["active"]=false;if(std::string(bad)=="missing-bridge")state["deckyUpdateResidue"]["bridgePresent"]=false;if(std::string(bad)=="malformed")state["deckyUpdateResidue"]["frontendBuildId"]="x";
    check(policy.consider(steam,state,installed,enabled).empty()&&!policy.allows(steam),bad);
  }
  for(const auto bad:{"1:0","0:1","1:2:3","1;2","01:2","1:02","4294967296:2","1:18446744073709551616",""})check(!ymcc::deckyupdate::validTicket(bad),"invalid updater ticket rejected");
  {ymcc::deckyupdate::Recovery policy;policy.configure(ymcc::deckyupdate::ticket(steam));auto same=healthyMode;same.erase("deckyUpdateResidue");check(policy.consider(steam,same,newId,true)=="updater-decky-handoff","official updater continues same module version using explicit handoff");check(policy.allows(steam),"scoped updater session admitted");policy.veto();check(!policy.allows(steam)&&policy.consider(steam,healthyMode,newId,true).empty(),"manual/fatal off veto outranks handoff/residue");}
  {ymcc::deckyupdate::Recovery policy;auto wrong=steam;++wrong.created;policy.configure(ymcc::deckyupdate::ticket(wrong));auto same=healthyMode;same.erase("deckyUpdateResidue");check(policy.consider(steam,same,newId,true).empty(),"stale updater process birth cannot continue");}
  {ymcc::deckyupdate::Recovery policy;check(policy.consider(steam,healthyMode,newId,true)=="updated-decky-frontend-pending","exact field mismatch admits one Decky continuation");check(policy.consider(steam,healthyMode,newId,true).empty(),"duplicate mode read cannot initiate another recovery");auto wrong=steam;wrong.root=L"C:\\OtherSteam";check(!policy.allows(wrong),"root-bound continuation");auto desktop=healthyMode;desktop["mode"]="desktop";policy.consider(steam,desktop,newId,true);check(!policy.allows(steam),"desktop exit revokes recovery");}
  {ymcc::deckyupdate::Recovery policy;auto loaderOnly=healthyMode;loaderOnly["deckyUpdateResidue"]["frontendBuildId"]=newId;loaderOnly["deckyUpdateResidue"]["loaderVersion"]="v3.2.10";
   check(policy.consider(steam,loaderOnly,newId,true,"v3.3.0")=="updated-decky-loader-pending","official Loader upgrade alone resumes even if plugin module is unchanged");}
  // Full production launch-admission and shared-reader callback chain.
  g_deckySidebarRuntime.initialize({image,(scope/L"home").wstring(),[]{++preflights;return std::string{};},[](const auto& state){if(state.phase=="loader-started")++launches;}, {}, {}, {}, {}, {}, productionAdmission});
  g_deckySidebarRuntime.observeSteam(1);g_deckySidebarRuntime.setEnabled(true);
  check(until([]{return g_deckySidebarRuntime.snapshot().reason=="ymcc-startup-observation-only";}),"old scene first reproduces observation-only wait");
  check(preflights==0&&launches==0,"ordinary startup cannot prepare/inject/launch");
  ymcc::steamui::ModeObserverConfig config;config.debounceMs=1;config.minimumGapMs=1;config.failureGapMs=1;std::atomic<bool> residueReady{false},freshContext{false};config.read=[&](const Session&){auto state=healthyMode;if(freshContext){state["deckyFrontendLoaded"]=false;state["modeEpoch"]="new-document:1";}if(!residueReady)state.erase("deckyUpdateResidue");return state;};config.changed=productionModeChanged;
  g_steamModeObserver.initialize(config);g_steamModeObserver.observeSteam(steam);g_steamModeObserver.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
  check(until([]{return signals>0;}),"initial mode read completes without update evidence");check(launches==0,"first matching Big Picture baseline alone cannot start Decky");
  residueReady=true;g_steamModeObserver.requestRefresh(steam);
  check(until([]{return g_deckySidebarRuntime.snapshot().reason=="decky-context-already-loaded";}),"field regression: recovery grant cannot reinject the loaded Steam context");
  check(launches==0&&preflights==0,"update continuation performs zero loaded-context injection or Loader startup");
  freshContext=true;g_steamModeObserver.requestRefresh(steam);
  check(until([]{return launches==1;}),"field regression: shared update evidence wakes real runtime and starts owned child only after a fresh context");
  check(!g_steamLinkAdmission.allows(steam),"recovery did not authorize shared CSS or Steam startup");
  check(g_deckySidebarRuntime.snapshot().pid!=0,"actual inert child owned by normal Job");
  const auto beforeSignals=signals.load();for(unsigned i=0;i<100;++i)productionModeChanged(steam,healthyMode);
  check(launches==1&&preflights==1,"100 duplicate mode events never launch or reprepare healthy loader");
  g_deckyUpdateRecovery.veto();g_deckySidebarRuntime.setEnabled(false,"decky-render-failed");
  check(g_deckySidebarRuntime.snapshot().pid==0&&!g_deckySidebarRuntime.snapshot().enabled,"safety close remains normal off");productionModeChanged(steam,healthyMode);
  check(!g_deckyUpdateRecovery.allows(steam)&&launches==1,"fault-off cannot be undone by stale update evidence");
  g_deckyUpdateRecovery.userEnable(steam);check(g_deckyUpdateRecovery.allows(steam),"only fresh explicit user ON can clear veto");
  g_steamModeObserver.shutdown();g_deckySidebarRuntime.shutdown();
  // Same Steam/document after owner restart: exact active frontend, new broker,
  // no Loader authority and no CSS/Steam startup grant. This is the grey-panel field case.
  ++steam.pid;++steam.created;
  auto retainedState=healthyMode;retainedState["pid"]=steam.pid;retainedState["birth"]=std::to_string(steam.created);
  retainedState["deckyUpdateResidue"]["frontendBuildId"]=newId;
  retainedState["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
  {ymcc::deckyupdate::Recovery policy;
   check(policy.allowsRetainedFrontend(steam,retainedState,newId,ymcc::deckyidentity::version),"exact active frontend permits transport only without update consent");
   for(const auto bad:{"foreign-pid","foreign-birth","desktop","unknown","not-loaded","inactive","missing-bridge","different-module","missing-version","different-loader"}){
    auto state=retainedState;const std::string kind=bad;
    if(kind=="foreign-pid")state["pid"]=1;if(kind=="foreign-birth")state["birth"]="different";
    if(kind=="desktop")state["mode"]="desktop";if(kind=="unknown")state["known"]=false;
    if(kind=="not-loaded")state["deckyFrontendLoaded"]=false;if(kind=="inactive")state["deckyUpdateResidue"]["active"]=false;
    if(kind=="missing-bridge")state["deckyUpdateResidue"]["bridgePresent"]=false;
    if(kind=="different-module")state["deckyUpdateResidue"]["frontendBuildId"]=oldId;
    if(kind=="missing-version")state["deckyUpdateResidue"].erase("loaderVersion");
    if(kind=="different-loader")state["deckyUpdateResidue"]["loaderVersion"]="v3.2.10";
    check(!policy.allowsRetainedFrontend(steam,state,newId,ymcc::deckyidentity::version),bad);
   }
   policy.veto();check(!policy.allowsRetainedFrontend(steam,retainedState,newId,ymcc::deckyidentity::version),"OFF veto rejects retained transport too");
  }
  std::atomic<unsigned> retainedAdmissionReads{0};
  std::atomic<unsigned> retainedBinds{0};const auto oldLaunches=launches.load(),oldPreflights=preflights.load();
  fixtureStage::begin(scope.parent_path(),oldLaunches,oldPreflights);
  g_deckySidebarRuntime.initialize({image,(scope/L"home").wstring(),[]{
    ++fixtureStage::attempt;FIXTURE_STAGE("preflight.begin",{{"preflightsBefore",preflights.load()}});
    ++preflights;FIXTURE_STAGE("preflight.return",{{"result",""},{"preflightsAfter",preflights.load()}});return std::string{};
  },[](const auto& state){
    FIXTURE_STAGE("runtime-state-callback.begin",{{"phase",state.phase},{"reason",state.reason},{"pid",state.pid},{"error",state.error},{"revision",state.revision}});
    if(state.phase=="loader-started")++launches;
    FIXTURE_STAGE("runtime-state-callback.end",{{"phase",state.phase},{"pid",state.pid},{"revision",state.revision},{"launches",launches.load()}});
  },{},{},{},{},{},[&]{
    ++retainedAdmissionReads;FIXTURE_STAGE("launch-admission.begin");const auto admission=productionAdmission();
    FIXTURE_STAGE("launch-admission.return",{{"allowed",admission.allowed},{"bindingOnly",admission.bindingOnly},{"reason",admission.reason}});return admission;
  },[&]{const auto count=++retainedBinds;if(count<=2){g_deckySidebarRuntime.requestTransportRecovery();return std::string("mirror-bootstrap-ws-closed");}return std::string{};}});
  ymcc::steamui::ModeObserverConfig retainedConfig;retainedConfig.debounceMs=1;retainedConfig.minimumGapMs=1;
  std::mutex retainedModeMutex;
  retainedConfig.read=[&](const Session&){std::lock_guard lock(retainedModeMutex);return retainedState;};retainedConfig.changed=productionModeChanged;
  g_steamModeObserver.initialize(retainedConfig);g_steamModeObserver.observeSteam(steam);g_steamModeObserver.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
  const auto retainedStarted=GetTickCount64();
  g_deckySidebarRuntime.observeSteam(1,true);g_deckySidebarRuntime.setEnabled(true);
  check(until([]{return g_deckySidebarRuntime.snapshot().phase=="frontend-attached";}),"retained frontend must restore broker without official Loader");
  check(retainedBinds==3&&launches==oldLaunches&&preflights==oldPreflights&&g_deckySidebarRuntime.snapshot().pid==0,"retained path never performs Loader preflight or owned child creation");
  check(GetTickCount64()-retainedStarted>=1100,"reentrant watcher failures preserve increasing connection backoff");
  check(!g_steamLinkAdmission.allows(steam)&&!g_deckyUpdateRecovery.allows(steam),"retained transport does not grant Loader or CSS startup");
  for(unsigned i=0;i<100;++i)productionModeChanged(steam,retainedState);
  check(retainedBinds==3&&launches==oldLaunches,"100 duplicate mode reads do not rebind healthy retained frontend");
  check(g_deckySidebarRuntime.requestTransportRecovery(),"existing transport failure event admitted");
  check(until([&]{return retainedBinds==4;}),"retained transport recovers through existing owner event");
  check(launches==oldLaunches&&g_deckySidebarRuntime.snapshot().pid==0,"transport recovery never creates Loader");
  // The official backend remains absent until an explicit repair and a fresh
  // verified document. Exercise the real ManualRepair + Runtime + mode callback.
  const auto retainedDocument=retainedState;
  {std::lock_guard lock(retainedModeMutex);retainedState["deckyFrontendLoaded"]=false;retainedState.erase("deckyUpdateResidue");retainedState["modeEpoch"]="fixture-new-document:1";}
  g_steamModeObserver.requestRefresh(steam);check(until([&]{return g_steamModeObserver.snapshot(steam).value("modeEpoch",std::string{})=="fixture-new-document:1";}),"fresh document observed without consent");
  {const auto reads=retainedAdmissionReads.load();g_deckySidebarRuntime.requestLaunchAdmissionRefresh();
   check(until([&]{return retainedAdmissionReads>reads;}),"no-consent admission observed by owner worker");
   check(launches==oldLaunches,"fresh document without explicit consent cannot launch retained backend");}
  {std::lock_guard lock(retainedModeMutex);retainedState=retainedDocument;}
  g_steamModeObserver.requestRefresh(steam);check(until([&]{return g_steamModeObserver.snapshot(steam).value("deckyFrontendLoaded",false);}),"retained document restored before explicit repair");
  check(until([]{return g_deckySidebarRuntime.snapshot().phase=="frontend-attached";}),"retained binding settles before explicit repair captures revision");
  const auto liveState=[](){const auto s=g_deckySidebarRuntime.snapshot();return json{{"enabled",s.enabled},{"pid",s.pid},{"phase",s.phase},{"reason",s.reason},{"revision",s.revision}};};
  const auto beforeRepair=liveState();json repairArgs={{"userInitiated",true},{"pid",steam.pid},{"birth",std::to_string(steam.created)},{"revision",beforeRepair.at("revision")}};
  unsigned contextRefreshes=0;ymcc::deckyrepair::ManualRepair realRepair;
  ymcc::deckyrepair::Dependencies liveDeps{false,[]{return steam;},liveState,[](const auto& s){return g_steamModeObserver.snapshot(s);},[&]{return newId;},ymcc::deckyidentity::version,
    []{return std::string{};},[]{return false;},[](const auto& s){return g_deckyUpdateRecovery.allowManualRepair(s);},
    [&](const auto&,const auto&,const auto& admit){if(!admit())return json{{"ok",false},{"mutated",false}};++contextRefreshes;return json{{"ok",true},{"mutated",true},{"requestDispatched",true}};},[]{return GetTickCount64();}};
  check(realRepair.request(repairArgs,liveDeps).value("ok",false)&&contextRefreshes==1,"explicit retained repair dispatches exactly once");
  {const auto reads=retainedAdmissionReads.load();g_deckySidebarRuntime.requestLaunchAdmissionRefresh();
   check(until([&]{return retainedAdmissionReads>reads;}),"retained-document admission observed by owner worker");
   check(launches==oldLaunches,"explicit consent still cannot launch into retained document");}
  {std::lock_guard lock(retainedModeMutex);retainedState["deckyFrontendLoaded"]=false;retainedState.erase("deckyUpdateResidue");retainedState["modeEpoch"]="fixture-repaired-document:1";}
  FIXTURE_STAGE("fresh-document.requested",{{"modeEpoch","fixture-repaired-document:1"}});
  g_steamModeObserver.requestRefresh(steam);
  FIXTURE_STAGE("assert-wait.begin",{{"assertion","explicit retained repair starts owned backend only after fresh document"},
    {"oldLaunches",oldLaunches},{"oldPreflights",oldPreflights},{"originalDeadlineMs",4000}});
  const bool retainedNormalCreated=until([&]{return launches==oldLaunches+1&&g_deckySidebarRuntime.snapshot().pid!=0;});
  {const auto state=g_deckySidebarRuntime.snapshot();FIXTURE_STAGE("assert-wait.end-before-cleanup",{{"predicateResultAtWaitEnd",retainedNormalCreated},{"snapshotSampledAfterWait",true},
    {"oldLaunches",oldLaunches},{"oldPreflights",oldPreflights},{"launches",launches.load()},{"preflights",preflights.load()},
    {"launchPredicateAtSnapshot",launches==oldLaunches+1},{"pidPredicateAtSnapshot",state.pid!=0},{"phase",state.phase},{"reason",state.reason},
    {"pid",state.pid},{"revision",state.revision},{"error",state.error},{"allows",g_deckyUpdateRecovery.allows(steam)}});
    fixtureStage::flush("RETAINED_WAIT_OBSERVED");}
  check(retainedNormalCreated,"explicit retained repair starts owned backend only after fresh document");
  check(preflights==oldPreflights+1&&contextRefreshes==1&&!g_steamLinkAdmission.allows(steam),"retained repair preserves one normal preflight and no CSS or Steam startup grant");
  for(unsigned i=0;i<100;++i)productionModeChanged(steam,g_steamModeObserver.snapshot(steam));
  check(launches==oldLaunches+1&&contextRefreshes==1,"fresh-context duplicate events never replay retained repair");
  // Existing committed wake renews credentials once; no second Loader.
  const auto beforeWakePid=g_deckySidebarRuntime.snapshot().pid;const auto beforeWakePreflights=preflights.load();
  check(g_deckySidebarRuntime.requestBindingRefresh(77),"committed wake requests one transport check");
  check(!g_deckySidebarRuntime.requestBindingRefresh(77)&&!g_deckySidebarRuntime.requestBindingRefresh(76),"duplicate or older wake generation cannot rebind twice");
  check(until([&]{return g_deckySidebarRuntime.snapshot().phase=="loader-started";}),"wake transport returns to the existing owned runtime");
  check(g_deckySidebarRuntime.snapshot().pid==beforeWakePid&&preflights==beforeWakePreflights,"wake transport check does not create a second Loader");
  const auto beforeFaultBinds=retainedBinds.load();
  g_deckyUpdateRecovery.veto();g_deckySidebarRuntime.setEnabled(false,"decky-render-failed");productionModeChanged(steam,retainedState);
  check(!g_deckySidebarRuntime.snapshot().enabled&&retainedBinds==beforeFaultBinds&&!g_deckySidebarRuntime.snapshot().pid,"late retained mode events cannot reopen fault OFF");
  g_steamModeObserver.shutdown();g_deckySidebarRuntime.shutdown();
  FIXTURE_STAGE("retained-runtime.shutdown.complete");fixtureStage::flush("RETAINED_PHASE_COMPLETED");fixtureStage::active=false;
  // Automatic recovery exercises the actual Runtime worker all the way to
  // owned backend startup. There is no manual request or fake enabled toggle.
  ++steam.pid;++steam.created;
  auto autoMode=retainedState;autoMode["pid"]=steam.pid;autoMode["birth"]=std::to_string(steam.created);
  autoMode["modeEpoch"]="automatic-field-document:1";autoMode["deckyFrontendLoaded"]=true;
  autoMode["deckyUpdateResidue"]={{"protocol",1},{"active",true},{"bridgePresent",true},{"frontendBuildId",newId},{"loaderVersion",ymcc::deckyidentity::version}};
  std::mutex autoModeMutex;std::atomic<unsigned> automaticDispatches{0};const auto previousLaunches=launches.load();
  ymcc::deckyrepair::ManualRepair automaticShared;ymcc::deckyrepair::AutomaticRepair automaticOwner;
  const auto automaticState=[](){const auto s=g_deckySidebarRuntime.snapshot();return json{{"enabled",s.enabled},{"pid",s.pid},{"phase",s.phase},{"reason",s.reason},{"revision",s.revision}};};
  ymcc::deckyrepair::Dependencies automaticDeps{false,[]{return steam;},automaticState,[](const auto& s){return g_steamModeObserver.snapshot(s);},[&]{return newId;},ymcc::deckyidentity::version,
    []{return std::string{};},[]{return false;},[](const auto& s){return g_deckyUpdateRecovery.allowManualRepair(s);},
    [&](const auto&,const auto&,const auto& admit)->json{if(!admit())return json{{"ok",false},{"mutated",false}};++automaticDispatches;
      {std::lock_guard lock(autoModeMutex);autoMode["deckyFrontendLoaded"]=false;autoMode.erase("deckyUpdateResidue");autoMode["modeEpoch"]="automatic-repaired-document:1";}
      g_steamModeObserver.requestRefresh(steam);return json{{"ok",true},{"mutated",true}};},[]{return GetTickCount64();}};
  // Prior safety OFF persists; only the existing explicit ON clears that veto.
  g_deckyUpdateRecovery.userEnable(steam);
  g_deckySidebarRuntime.initialize({image,(scope/L"home").wstring(),[]{++preflights;return std::string{};},[](const auto& state){if(state.phase=="loader-started")++launches;},{},{},{},{},{},productionAdmission,[]{return std::string{};},[&]{automaticOwner.observe(automaticShared,automaticDeps);}});
  ymcc::steamui::ModeObserverConfig automaticConfig;automaticConfig.debounceMs=1;automaticConfig.minimumGapMs=1;
  automaticConfig.read=[&](const Session&){std::lock_guard lock(autoModeMutex);return autoMode;};automaticConfig.changed=productionModeChanged;
  g_steamModeObserver.initialize(automaticConfig);g_steamModeObserver.observeSteam(steam);g_steamModeObserver.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
  g_deckySidebarRuntime.observeSteam(1,true);g_deckySidebarRuntime.setEnabled(true);
  check(until([&]{return automaticOwner.snapshot().value("phase",std::string{})=="manual-required";}),"actual startup with missing backend reports manual repair instead of refreshing Steam UI");
  check(automaticDispatches==0&&g_deckySidebarRuntime.snapshot().pid==0&&launches==previousLaunches,"ordinary YMCC startup cannot rebuild or relaunch the existing Steam Big Picture context");
  // Only a subsequent explicit user repair can consume the existing UI rebuild path.
  const auto pendingState=automaticState();const json explicitArgs={{"userInitiated",true},{"pid",steam.pid},{"birth",std::to_string(steam.created)},{"revision",pendingState.at("revision")}};
  const auto explicitResult=automaticShared.request(explicitArgs,automaticDeps);
  check(explicitResult.value("ok",false),"explicit repair still admits the current retained context after automatic refusal");
  check(until([&]{return automaticDispatches==1&&g_deckySidebarRuntime.snapshot().pid!=0&&launches==previousLaunches+1;}),"explicit missing-backend repair reaches owned startup only after the user request");
  for(unsigned i=0;i<100;++i)g_deckySidebarRuntime.requestConnectionCheck();
  check(automaticDispatches==1,"healthy automatic recovery cannot refresh the live document again");
  {
    const auto ownedPid=g_deckySidebarRuntime.snapshot().pid;const auto ownedLaunches=launches.load();
    check(ownedPid!=0,"owned repair fixture keeps actual inert backend alive");
    auto manualMode=healthyMode;manualMode["pid"]=steam.pid;manualMode["birth"]=std::to_string(steam.created);
    manualMode["deckyUpdateResidue"]["loaderVersion"]=ymcc::deckyidentity::version;
    unsigned manualRefreshes=0,portChecks=0,freshReads=0;
    auto manualDeps=automaticDeps;
    manualDeps.mode=[&](const auto&){auto stale=manualMode;stale["deckyFrontendLoaded"]=false;stale.erase("deckyUpdateResidue");return stale;};
    manualDeps.freshMode=[&](const auto&){++freshReads;return manualMode;};
    manualDeps.preflight=[&]{return ymcc::deckyrepair::repairPreflight(json{{"ready",true}},automaticState(),[&]{++portChecks;return std::string("port-1337-in-use");});};
    manualDeps.dispatch=[&](const auto&,const auto& action,const auto& admit){check(action.at("operation")=="decky.repairConnection","owned runtime only accepts explicit repair operation");if(!admit())return json{{"ok",false},{"mutated",false}};++manualRefreshes;return json{{"ok",true},{"mutated",true}};};
    const auto ownedState=automaticState();const json manualArgs={{"userInitiated",true},{"pid",steam.pid},{"birth",std::to_string(steam.created)},{"revision",ownedState.at("revision")}};
    ymcc::deckyrepair::ManualRepair explicitOwned;const auto result=explicitOwned.request(manualArgs,manualDeps);
    check(result.value("ok",false)&&manualRefreshes==1&&portChecks==0,"explicit owned repair must read current frontend instead of stale pre-load cache");
    check(freshReads==1,"explicit owned repair bypasses stale pre-load mode cache with one current read");
    check(g_deckySidebarRuntime.snapshot().pid==ownedPid&&launches==ownedLaunches,"manual UI refresh keeps same actual owned backend and launch count");
    auto forged=manualArgs;forged.erase("userInitiated");forged["automatic"]=true;
    ymcc::deckyrepair::ManualRepair noOwnedAutomatic;
    check(!noOwnedAutomatic.request(forged,manualDeps,ymcc::deckyrepair::Origin::Automatic).value("ok",true)&&manualRefreshes==1,"automatic origin cannot refresh an owned backend");
    ymcc::deckyrepair::AutomaticRepair healthyOwned;healthyOwned.observe(noOwnedAutomatic,manualDeps);
    check(manualRefreshes==1&&portChecks==0&&freshReads==1,"healthy owned runtime never consumes automatic refresh budget or forces a context read");
    check(ymcc::deckyrepair::repairPreflight(json{{"ready",false},{"reason","loader-hash-mismatch"}},ownedState,[]{return std::string{};})=="loader-hash-mismatch","resource identity failure still rejects owned manual repair");
    auto absent=ownedState;absent["pid"]=0u;absent["phase"]="waiting-manual-steam";absent["reason"]="decky-context-already-loaded";
    check(ymcc::deckyrepair::repairPreflight(json{{"ready",true}},absent,[&]{++portChecks;return std::string("port-1337-in-use");})=="port-1337-in-use"&&portChecks==1,"absent backend retains foreign port refusal");
  }
  g_steamModeObserver.shutdown();g_deckySidebarRuntime.shutdown();
  {
    auto emptyMode=healthyMode;emptyMode["pid"]=steam.pid;emptyMode["birth"]=std::to_string(steam.created);emptyMode.erase("deckyUpdateResidue");
    emptyMode["deckyEmptyFrontend"]={{"protocol",1},{"frontendBuildId",newId},{"loaderVersion",ymcc::deckyidentity::version}};
    const auto action=ymcc::deckyrepair::target(steam,emptyMode,newId,ymcc::deckyidentity::version);
    check(action.is_object()&&action.at("expectedFrontendBuild").is_null(),"own empty frontend has explicit absence contract");
    emptyMode["deckyEmptyFrontend"]["frontendBuildId"]=oldId;
    check(ymcc::deckyrepair::target(steam,emptyMode,newId,ymcc::deckyidentity::version).is_null(),"foreign empty frontend build refused");
    emptyMode["deckyEmptyFrontend"]["frontendBuildId"]=newId;emptyMode["deckyEmptyFrontend"]["loaderVersion"]="v3.2.10";
    check(ymcc::deckyrepair::target(steam,emptyMode,newId,ymcc::deckyidentity::version).is_null(),"wrong empty official frontend refused");
  }
  {
    ymcc::deckymirror::Broker transport;check(transport.start({}),"production broker starts task-only ephemeral loopback");
    const auto boot=transport.bootstrap(),initial=transport.status();const auto endpoint=boot.at("endpoint").get<std::string>();
    const auto port=static_cast<unsigned short>(std::stoi(endpoint.substr(endpoint.rfind(':')+1)));
    SOCKET peer=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);check(peer!=INVALID_SOCKET,"task-only transport client opens");
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(port);
    check(::connect(peer,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"task-only client connects to exact ephemeral broker");
    const std::string header="GET /mirror?token="+boot.at("token").get<std::string>()+" HTTP/1.1\r\nHost: 127.0.0.1:"+std::to_string(port)+"\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\nOrigin: https://steamloopback.host\r\n\r\n";
    check(::send(peer,header.data(),static_cast<int>(header.size()),0)==static_cast<int>(header.size()),"task-only client sends authenticated upgrade");
    const bool accepted=until([&]{return transport.status().value("connected",false);});const auto attached=transport.status();
    ::shutdown(peer,SD_BOTH);::closesocket(peer);
    check(accepted&&attached.value("connectionRevision",0ull)>initial.value("connectionRevision",0ull),"authenticated attach increases actual transport revision");
    check(until([&]{return !transport.status().value("connected",true);}),"real peer close is observed without polling installed processes");
    const auto detached=transport.status();check(detached.value("connectionRevision",0ull)>attached.value("connectionRevision",0ull),"disconnect increases actual transport revision");
    transport.stop();check(transport.status().value("connectionRevision",0ull)>detached.value("connectionRevision",0ull),"shutdown preserves monotonic transport revision");
  }
  {
    // Source-extracted WM_POWER_RESUME_COMMIT against the REAL Runtime. The
    // Windows IPC/PostMessage boundary and inert self-child are task-only IO.
    std::atomic<unsigned> wakeBindings{0},wakePreflights{0};
    g_deckySidebarRuntime.initialize({image,(scope/L"home").wstring(),[&]{++wakePreflights;return std::string{};},{},[&]{++wakeBindings;return std::string{};}});
    g_deckySidebarRuntime.observeSteam(1,true);g_deckySidebarRuntime.setEnabled(true);
    check(until([]{return g_deckySidebarRuntime.snapshot().pid!=0;}),"wake chain starts only an exact owned inert backend");
    const auto owner=g_deckySidebarRuntime.snapshot().pid;
    fixtureWakeEvents=json::array();fixtureWakeDiagnostics=json::array();fixtureWakeStops=fixtureWakeInputPosts=0;fixtureWakeGeneration=177;
    for(const auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Resuming}){
        g_powerLifecycle.store(phase);productionWakeCommit(0);
        check(fixtureWakeEvents.empty()&&fixtureWakeDiagnostics.empty()&&wakeBindings==0&&fixtureWakeInputPosts==0,"uncommitted power phase cannot emit Ready or renew Decky binding");
    }
    g_powerLifecycle.store(PowerLifecycle::Ready);productionWakeCommit(0);
    check(until([&]{return wakeBindings==1;}),"actual committed wake reaches production Runtime binding refresh");
    check(fixtureWakeEvents.size()==1&&fixtureWakeEvents[0]["event"]=="power.resumed"&&fixtureWakeEvents[0]["data"]["generation"]==177&&fixtureWakeEvents[0]["data"]["degraded"]==false,"actual committed wake emits current generation Ready event");
    check(fixtureWakeDiagnostics.size()==1&&fixtureWakeDiagnostics[0].value("bindingRequested",false),"native owner reports the accepted binding demand");
    check(g_deckySidebarRuntime.snapshot().pid==owner&&wakePreflights==1,"committed wake preserves original owned backend without preflight or relaunch");
    for(unsigned i=0;i<20;++i)productionWakeCommit(0);
    check(wakeBindings==1&&fixtureWakeDiagnostics.size()==21&&std::all_of(fixtureWakeDiagnostics.begin()+1,fixtureWakeDiagnostics.end(),[](const auto& d){return !d.value("bindingRequested",true);}),"duplicate committed wake notifications cannot bind again");
    fixtureWakeGeneration=178;productionWakeCommit(1);
    check(until([&]{return wakeBindings==2;}),"next committed wake generation renews transport once");
    check(fixtureWakeEvents.back()["data"]["generation"]==178&&fixtureWakeEvents.back()["data"]["degraded"]==true,"degraded committed wake keeps truthful original power event");
    check(g_deckySidebarRuntime.snapshot().pid==owner&&wakePreflights==1,"second committed wake keeps same actual backend");
    fixtureWakeGeneration=177;productionWakeCommit(0);
    check(!fixtureWakeDiagnostics.back().value("bindingRequested",true)&&wakeBindings==2,"older committed event cannot rebind the newer generation");
    const auto eventsForHost=fixtureWakeEvents;
    g_deckySidebarRuntime.setEnabled(false);fixtureWakeGeneration=179;productionWakeCommit(0);
    check(!g_deckySidebarRuntime.snapshot().enabled&&!g_deckySidebarRuntime.snapshot().pid&&wakeBindings==2&&!fixtureWakeDiagnostics.back().value("bindingRequested",true),"OFF remains closed after a late actual Ready event");
    const json wakeReport={{"schemaVersion",1},{"passed",true},{"productionBlockExtracted",true},{"rawResumeCannotCommit",true},{"actualWindowsSleep",false},{"actualSteamActions",0},{"ownedBackendPreserved",true},{"bindingCalls",wakeBindings.load()},{"preflights",wakePreflights.load()},{"events",eventsForHost}};
    std::ofstream(scope.parent_path()/L"native-wake-commit.json")<<wakeReport.dump(2);
    g_deckySidebarRuntime.shutdown();
  }
  // Same-process/old Steam Xbox FSE: production admission -> real Runtime ->
  // owned inert child. Only the Windows system flag is a memory observation.
  {
    steam.pid+=2000;steam.created-=100;g_steamLinkAdmission.observe(steam);
    json mode={{"known",true},{"running",true},{"pid",steam.pid},{"birth",std::to_string(steam.created)},{"mode","bigpicture"},{"modeEpoch","xbox-fse-document:1"},{"deckyFrontendLoaded",false}};
    gamingFullscreenObservation={true,true,true,true};gamingFullscreenLive=false;
    const auto beforeLaunch=launches.load(),beforePreflight=preflights.load();
    g_powerLifecycle=PowerLifecycle::Ready;
    ymcc::steamui::ModeObserverConfig fseConfig;fseConfig.debounceMs=1;fseConfig.minimumGapMs=1;fseConfig.failureGapMs=1;fseConfig.read=[&](const Session&){return mode;};fseConfig.changed=productionModeChanged;
    g_steamModeObserver.initialize(fseConfig);g_steamModeObserver.observeSteam(steam);g_steamModeObserver.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
    g_deckySidebarRuntime.initialize({image,(scope/L"home").wstring(),[]{++preflights;return std::string{};},[](const auto& state){if(state.phase=="loader-started")++launches;},{},{},{},{},{},productionAdmission});
    g_deckySidebarRuntime.observeSteam(1,true);g_deckySidebarRuntime.setEnabled(true);
    check(until([]{return g_deckySidebarRuntime.snapshot().reason=="ymcc-startup-observation-only";}),"Xbox inactive waits without launching on saved ON alone");
    gamingFullscreenLive=true;g_deckySidebarRuntime.requestLaunchAdmissionRefresh();
    check(until([&]{return launches==beforeLaunch+1&&g_deckySidebarRuntime.snapshot().pid>0;}),"Xbox full-screen field path reaches actual owned Loader consumer");
    check(preflights==beforePreflight+1&&!g_steamLinkAdmission.allows(steam),"Xbox FSE prepares once without granting shared Steam or CSS authority");
    const auto childPid=g_deckySidebarRuntime.snapshot().pid;HANDLE child=OpenProcess(SYNCHRONIZE,FALSE,childPid);check(child!=nullptr,"Xbox test holds exact owned child for observed exit");
    for(unsigned i=0;i<100;++i)g_deckySidebarRuntime.requestLaunchAdmissionRefresh();Sleep(40);
    check(launches==beforeLaunch+1&&preflights==beforePreflight+1,"duplicate FSE/mode facts cannot restart healthy backend");
    const auto stopAt=GetTickCount64();g_deckySidebarRuntime.setEnabled(false);
    check(WaitForSingleObject(child,2000)==WAIT_OBJECT_0&&GetTickCount64()-stopAt<3000,"Xbox OFF destroys actual owned child within a bound");CloseHandle(child);
    for(unsigned i=0;i<10;++i){productionModeChanged(steam,mode);g_deckySidebarRuntime.requestLaunchAdmissionRefresh();}Sleep(40);
    check(!g_deckySidebarRuntime.snapshot().enabled&&g_deckySidebarRuntime.snapshot().pid==0&&launches==beforeLaunch+1,"late FSE events cannot resurrect explicit OFF");
    g_steamModeObserver.shutdown();g_deckySidebarRuntime.shutdown();gamingFullscreenLive=false;gamingFullscreenObservation={};
    std::ofstream(scope.parent_path()/L"xbox-fullscreen-admission.json")<<json{{"ok",true},{"actualOwnedChildLaunches",1},{"preflights",1},{"lateEvents",10},{"zeroSteamStarts",true},{"zeroSteamModeRequests",true},{"ownedChildExitObserved",true},{"physicalFseTest",false}}.dump(2);
  }
  // Native prewarm lives in the same Runtime for every supported startup order.
  // Steam state/mode/digest are explicit fixture ports; all children are this inert executable.
  {
    const auto originalSteam=steam;const auto originalControl=POWER_CONTROL_DIR;
    json startupResults=json::array();
    for(unsigned route=0;route<3;++route){
      const auto caseRoot=scope/(L"prewarm-"+std::to_wstring(GetCurrentProcessId())+L"-route-"+std::to_wstring(route));
      POWER_CONTROL_DIR=caseRoot.wstring();
      const auto fakeSteam=caseRoot/L"fixture-steam",decky=caseRoot/L"decky",home=caseRoot/L"home";
      std::filesystem::create_directories(fakeSteam);std::filesystem::create_directories(decky/L"plugins/ymcc-sidebar/dist");std::filesystem::create_directories(home);
      std::ofstream(fakeSteam/L"steam.exe")<<"inert";std::ofstream(decky/L"PluginLoader_noconsole.exe")<<"inert";
      std::ofstream(decky/L"plugins/ymcc-sidebar/plugin.json")<<"{}";std::ofstream(decky/L"plugins/ymcc-sidebar/dist/index.js")<<"// fixture";
      const auto marker=fakeSteam/L".cef-enable-remote-debugging";
      const bool alreadyRunning=route==2,directBigPicture=route==1;
      if(alreadyRunning)std::ofstream(marker)<<"PRESERVE";
      const Session next{LR"(C:\FixtureSteam)",static_cast<DWORD>(50000+route),0,
        route==2?134368675855380000ULL+route:134369000000000100ULL+route};
      steam=alreadyRunning?next:Session{};fixtureSteamPresence=alreadyRunning?1:0;
      g_steamLinkAdmission.observe(steam);g_powerLifecycle=PowerLifecycle::Ready;g_exitRequested=false;fixtureResourceValid=true;
      std::mutex modeMutex;json routeMode={{"known",true},{"running",true},{"pid",next.pid},{"birth",std::to_string(next.created)},
        {"mode",directBigPicture?"bigpicture":"desktop"},{"modeEpoch","prewarm-route-"+std::to_string(route)+":0"},{"deckyFrontendLoaded",false}};
      ymcc::steamui::ModeObserverConfig routeConfig;routeConfig.debounceMs=1;routeConfig.minimumGapMs=1;routeConfig.failureGapMs=1;
      routeConfig.read=[&](const Session&){std::lock_guard lock(modeMutex);return routeMode;};routeConfig.changed=productionModeChanged;
      g_steamModeObserver.initialize(routeConfig);g_steamModeObserver.observeSteam(steam);g_steamModeObserver.setDemand(ymcc::steamui::ModeConsumer::Decky,true);
      std::atomic<unsigned> routeStarts{0},routeChecks{0};const auto preparedBefore=fixturePrepared.load();
      ymcc::deckysidebar::Config config{image,home.wstring(),[&]{++routeChecks;const auto state=deckySidebarEnvironment(false);return state.value("ready",false)?std::string{}:state.value("reason",std::string("fixture-prepare-failed"));},
        [&](const auto& state){if(state.phase=="loader-started")++routeStarts;},{},{},{},{},{},productionAdmission};
      config.prepareEnvironment=deckySidebarPrepareEnvironment;
      g_deckySidebarRuntime.initialize(std::move(config));g_deckySidebarRuntime.observeSteam(alreadyRunning?1:0,true);g_deckySidebarRuntime.setEnabled(true);
      check(until([&]{return fixturePrepared>preparedBefore;}),"native Runtime prewarms resources without requiring a running Steam process");
      check(std::filesystem::is_regular_file(marker),"native setup prepares debug marker before launch admission");
      check(routeChecks==0&&routeStarts==0&&g_deckySidebarRuntime.snapshot().pid==0,"resource prewarm alone grants no Loader or page binding authority");
      if(!alreadyRunning){steam=next;fixtureSteamPresence=1;g_steamLinkAdmission.observe(steam);g_steamModeObserver.observeSteam(steam);g_deckySidebarRuntime.observeSteam(1,true);}
      if(!directBigPicture){
        check(until([&]{return g_deckySidebarRuntime.snapshot().reason=="steam-bigpicture-required";}),"normal desktop Steam waits for actual user Big Picture entry");
        check(routeChecks==0&&routeStarts==0,"desktop mode cannot start official Loader");
        {std::lock_guard lock(modeMutex);routeMode["mode"]="bigpicture";routeMode["modeEpoch"]="prewarm-route-"+std::to_string(route)+":1";}
        g_steamModeObserver.requestRefresh(steam);
      }
      check(until([&]{return routeStarts==1&&g_deckySidebarRuntime.snapshot().pid>0;}),"verified manual or automatic Big Picture consumes the already prepared native startup");
      check(routeChecks==1,"one final resource preflight remains before the owned Loader launch");
      const auto readyPreparations=fixturePreparations.load();for(unsigned i=0;i<80;++i)g_deckySidebarRuntime.requestLaunchAdmissionRefresh();Sleep(30);
      check(routeStarts==1&&routeChecks==1&&fixturePreparations==readyPreparations,"healthy Loader ignores repeated preparation and mode wakes");
      const auto pid=g_deckySidebarRuntime.snapshot().pid;HANDLE child=OpenProcess(SYNCHRONIZE,FALSE,pid);check(child!=nullptr,"startup matrix holds exact owned inert child for exit verification");
      g_deckySidebarRuntime.setEnabled(false);check(WaitForSingleObject(child,1200)==WAIT_OBJECT_0,"startup matrix OFF destroys the owned Loader child");CloseHandle(child);
      check(!g_deckySidebarRuntime.requestPreparationRefresh(700+route),"OFF rejects late prewarm demand without recreating a worker");
      g_steamModeObserver.shutdown();g_deckySidebarRuntime.shutdown();
      startupResults.push_back({{"route",route==0?"YMCC-desktopSteam-manualBPM":route==1?"YMCC-Steam-own-BPM-setting":"desktopSteam-before-YMCC-manualBPM"},{"prewarmed",true},{"ownedStarts",routeStarts.load()},{"finalPreflights",routeChecks.load()},{"offExitObserved",true}});
    }
    // A transient missing-resource result is retried on committed Ready, without Steam or a launch-budget grant.
    const auto retryRoot=scope/(L"prewarm-resume-"+std::to_wstring(GetCurrentProcessId()));POWER_CONTROL_DIR=retryRoot.wstring();steam={};fixtureSteamPresence=0;
    std::filesystem::create_directories(retryRoot/L"fixture-steam");std::filesystem::create_directories(retryRoot/L"decky/plugins/ymcc-sidebar/dist");
    std::ofstream(retryRoot/L"fixture-steam/steam.exe")<<"inert";std::ofstream(retryRoot/L"decky/PluginLoader_noconsole.exe")<<"inert";
    std::ofstream(retryRoot/L"decky/plugins/ymcc-sidebar/plugin.json")<<"{}";std::ofstream(retryRoot/L"decky/plugins/ymcc-sidebar/dist/index.js")<<"// fixture";
    fixtureResourceValid=false;const auto failedBefore=fixturePreparations.load(),readyBefore=fixturePrepared.load();
    ymcc::deckysidebar::Config retryConfig{image,(retryRoot/L"home").wstring(),[]{return std::string("NEVER-LAUNCH");}};retryConfig.prepareEnvironment=deckySidebarPrepareEnvironment;
    g_deckySidebarRuntime.initialize(std::move(retryConfig));g_deckySidebarRuntime.observeSteam(0,true);g_deckySidebarRuntime.setEnabled(true);
    check(until([&]{return fixturePreparations>failedBefore;}),"transient native prewarm failure is observed before Steam exists");
    check(fixturePrepared==readyBefore&&!std::filesystem::exists(retryRoot/L"fixture-steam/.cef-enable-remote-debugging"),"bad resource bytes cannot create a debug marker or claim ready");
    fixtureResourceValid=true;fixtureWakeGeneration=9901;g_powerLifecycle=PowerLifecycle::Suspended;
    productionWakeCommit(0);Sleep(20);check(fixturePrepared==readyBefore,"uncommitted sleep cannot authorize native prewarm retry");
    g_powerLifecycle=PowerLifecycle::Ready;productionWakeCommit(0);
    check(until([&]{return fixturePrepared>readyBefore;}),"actual committed Ready retries native prewarm while Steam remains absent");
    check(!g_deckySidebarRuntime.requestPreparationRefresh(9901),"duplicate Ready generation cannot renew preparation demand");
    check(g_deckySidebarRuntime.snapshot().pid==0&&!g_deckySidebarRuntime.snapshot().steamRunning,"resume resource recovery grants no Steam or Loader process authority");
    g_deckySidebarRuntime.setEnabled(false);check(!g_deckySidebarRuntime.requestPreparationRefresh(9902),"late resume demand respects saved OFF");
    g_deckySidebarRuntime.shutdown();fixtureResourceValid=true;steam=originalSteam;POWER_CONTROL_DIR=originalControl;fixtureSteamPresence=1;
    std::ofstream(scope.parent_path()/L"native-prewarm-startup-matrix.json")<<json{{"passed",true},{"startupRoutes",startupResults},{"committedReadyWithoutSteam",true},{"realSteamActions",0}}.dump(2);
  }
  std::cout<<"DECKY_UPDATE_RECOVERY_OK checks="<<checks<<" actualOwnedChildLaunches="<<launches<<" mainCallbacks=source-extracted realSteamActions=0\n";return 0;
 }catch(const std::exception& e){const auto failedState=g_deckySidebarRuntime.snapshot();const auto failedLaunches=launches.load(),failedPreflights=preflights.load();const auto failedAllows=g_deckyUpdateRecovery.allows(steam);g_steamModeObserver.shutdown();g_deckySidebarRuntime.shutdown();std::cerr<<"state="<<failedState.phase<<":"<<failedState.reason<<" error="<<failedState.error<<" launches="<<failedLaunches<<" preflights="<<failedPreflights<<" allows="<<failedAllows<<"\\nDECKY_UPDATE_RECOVERY_FAILED "<<e.what()<<'\n';return 1;}
}
