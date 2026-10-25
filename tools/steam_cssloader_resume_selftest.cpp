#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_cssloader_guard.h"
#include <json.hpp>
#include <atomic>
#include <fstream>
#include <iostream>
using namespace ymcc;using json=nlohmann::json;using Session=steamsession::Session;
static std::atomic<unsigned> checks=0;static void check(bool yes,const char*label){if(!yes)throw std::runtime_error(label);++checks;std::cout<<"PASS "<<label<<std::endl;}
template<class Work>static bool until(Work work,DWORD timeout=3000){auto end=GetTickCount64()+timeout;while(!work()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
static std::atomic<bool>g_exitRequested=false;static struct{bool isolated=false;}g_aiFanMockSession;
enum class PowerLifecycle{Ready,Resuming,Suspended};static std::atomic<PowerLifecycle>g_powerLifecycle=PowerLifecycle::Ready;static std::atomic<unsigned long long>powerGeneration=10;
static unsigned long long currentPowerGeneration(){return powerGeneration.load();}
static struct{Session value;Session sessionSnapshot(){return value;}}g_steamSettingsObserver;
static struct{steamcss::Runtime* actual=nullptr;bool requestResumeRecovery(const Session&s,ULONGLONG generation){return actual&&actual->requestResumeRecovery(s,generation);}}g_steamCssGuard;
static struct{std::atomic<unsigned> invalidations=0;bool valid=true;bool invalidateForResume(const Session&){++invalidations;return valid;}}g_steamCssUse;
static struct{std::function<bool(const Session&)> port;bool requestRefresh(const Session&s){return port&&port(s);}}g_steamModeObserver;
static void deckySidebarDiagnostic(const char*,const json&,bool){}
#include "production-css-resume.h"
static HANDLE copy(HANDLE handle){HANDLE result=nullptr;if(!DuplicateHandle(GetCurrentProcess(),handle,GetCurrentProcess(),&result,SYNCHRONIZE,FALSE,0))throw std::runtime_error("fixture wait handle");return result;}
static ULONGLONG cpu(){FILETIME b{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&b,&e,&k,&u);return(ULONGLONG(k.dwHighDateTime)<<32)+k.dwLowDateTime+(ULONGLONG(u.dwHighDateTime)<<32)+u.dwLowDateTime;}
int wmain(int argc,wchar_t**argv){try{
 check(argc==2,"owned evidence directory required");const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);const auto link=out/L"links"/L"backend.path";std::filesystem::create_directories(link.parent_path());{std::ofstream f(link);f<<"C:\\fixture-css\\CssLoader-Standalone-Headless.exe";}
 steamcss::Runtime runtime;g_steamCssGuard.actual=&runtime;Session session{L"C:\\fixture-steam",42,0,100};g_steamSettingsObserver.value=session;
 deckylaunch::Handle backend,helper;backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);helper.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(backend.value&&helper.value,"owned backend/helper wait events created");
 std::atomic<unsigned>modeReads=0,backendReads=0,helpers=0,refreshes=0,rebinds=0;std::atomic<bool>confirmed=false,refreshAccepted=true;std::atomic<steamcss::UiMode>mode=steamcss::UiMode::BigPicture;
 steamcss::Config config;config.linkFile=link.wstring();config.delays={2,5,10};config.modeDelays={2,10,20};
 config.mayLaunch=[] {return !g_exitRequested&&g_powerLifecycle==PowerLifecycle::Ready;};
 config.readMode=[&](const auto&){++modeReads;return steamcss::ModeSample{mode.load(),"same-document:1"};};
 g_steamModeObserver.port=[&](const auto&){++refreshes;mode=steamcss::UiMode::Unknown;return refreshAccepted.load();};
 config.refreshMode=productionCssResumeRefresh;
 config.ensure=[&](const auto&,const auto&,const auto&){if(mode!=steamcss::UiMode::BigPicture)return steamcss::Result{nullptr,0,"waiting-steam-ui","mode-unconfirmed"};++backendReads;return steamcss::Result{copy(backend.value),900,"running",{},false};};
 config.confirmIdle=[&](const auto&,bool start,bool renew){check(!start,"resume never creates a second backend when one already exists");++helpers;confirmed=false;ResetEvent(helper.value);return copy(helper.value);};
 config.isResidentConfirmed=[&](const auto&){return confirmed.load();};config.probeExisting=[&]{++rebinds;return steamcss::Result{copy(backend.value),900,"running",{},false};};
 struct ScopedRuntimeStop {steamcss::Runtime& value;~ScopedRuntimeStop(){g_steamCssGuard.actual=nullptr;value.shutdown();}} stopBeforePorts{runtime};
 check(runtime.initialize(config),"production CSS observer starts");runtime.observeSteam(session);
 check(until([&]{return helpers==1;}),"initial existing Big Picture gets one appearance use");confirmed=true;SetEvent(helper.value);check(until([&]{return runtime.snapshot().phase=="ready";}),"initial appearance is actually confirmed before resume");
 g_steamCssUse.valid=false;check(!productionCssResumeRefresh(session)&&refreshes==0,"actual resume refresh cannot bypass failed old-confirmation invalidation");g_steamCssUse.valid=true;
 g_powerLifecycle=PowerLifecycle::Resuming;check(!steamAppearanceResumeCommitted(10),"Resuming cannot authorize CSS correction before committed Ready");check(!productionCssResumeRefresh(session)&&refreshes==0,"actual refresh callback rejects uncommitted power state before shared mode IO");g_powerLifecycle=PowerLifecycle::Ready;
 check(!steamAppearanceResumeCommitted(0)&&!steamAppearanceResumeCommitted(9),"zero and stale power generations are rejected");g_aiFanMockSession.isolated=true;check(!steamAppearanceResumeCommitted(10),"device isolation blocks the actual production Ready callback");g_aiFanMockSession.isolated=false;
 check(steamAppearanceResumeCommitted(10),"actual committed Ready admits one CSS-only event");check(until([&]{return refreshes==1&&runtime.snapshot().phase=="waiting-steam-ui";}),"resume invalidates old shared mode before any CSS ensure can apply");
 for(unsigned i=0;i<50000;++i)if(steamAppearanceResumeCommitted(10))throw std::runtime_error("duplicate Ready admitted");
 Sleep(30);check(refreshes==1&&helpers==1&&backendReads==1,"50000 repeated Ready events create no extra reads, helper or backend");
 mode=steamcss::UiMode::BigPicture;runtime.observeMode(session,{mode.load(),"same-document:1"});
 check(until([&]{return helpers==2;}),"same Steam PID and same document receive one recovery after fresh Big Picture confirmation");confirmed=true;SetEvent(helper.value);check(until([&]{return runtime.snapshot().reason=="appearance-applied-resident";}),"resume reports ready only after the new helper confirmation");
 for(unsigned i=0;i<1000;++i)runtime.observeMode(session,{mode.load(),"same-document:1"});Sleep(50);check(helpers==2,"unchanged mode signals do not repeat the resumed appearance use");
 const auto idleCpu=cpu();const auto idleReads=backendReads.load(),idleModes=modeReads.load();Sleep(200);const auto idleDelta=cpu()-idleCpu;check(backendReads==idleReads&&modeReads==idleModes&&idleDelta<=312500,"healthy resumed observer parks without polling or repeated lookup");
 powerGeneration=11;check(steamAppearanceResumeCommitted(11),"a later power generation gets one new finite check");check(until([&]{return refreshes==2;}),"new generation requests fresh shared mode once");mode=steamcss::UiMode::Desktop;runtime.observeMode(session,{mode.load(),"same-document:1"});Sleep(60);check(helpers==2,"a desktop Steam resume never becomes an appearance launch or mode switch");
 auto stale=session;++stale.created;check(!runtime.requestResumeRecovery(stale,12),"stale Steam birth cannot enqueue a resume correction");
 powerGeneration=13;refreshAccepted=false;check(steamAppearanceResumeCommitted(13),"shared mode failure is handled on the worker");check(until([&]{return runtime.snapshot().reason=="resume-mode-refresh-unavailable";}),"failed mode refresh remains unconfirmed instead of using old mode");Sleep(40);check(helpers==2&&!steamAppearanceResumeCommitted(13),"failed resume does not replenish itself or launch a helper");refreshAccepted=true;
 std::filesystem::remove(link);check(until([&]{return runtime.snapshot().phase=="disabled";}),"appearance OFF reaches the actual event-driven guard");powerGeneration=14;check(!steamAppearanceResumeCommitted(14),"saved OFF cannot be revived by resume");Sleep(30);check(helpers==2,"OFF retains zero resume side effects");
 runtime.shutdown();powerGeneration=15;check(!steamAppearanceResumeCommitted(15),"shutdown rejects a late committed Ready callback");g_steamCssGuard.actual=nullptr;
 const json record={{"ok",true},{"checks",checks.load()},{"duplicateReadyEvents",50000},{"appearanceUses",helpers.load()},{"modeRefreshes",refreshes.load()},{"backendReads",backendReads.load()},{"idleSampleMs",200},{"idleCpuMs",idleDelta/10000.0},{"realSteamOperations",0},{"realCssOperations",0},{"physicalSleep",false},{"scope","Exact production Ready callback + actual CSS observer; shared mode and process/helper IO are owned inert boundaries"}};
 std::ofstream(out/L"result.json")<<record.dump(2);std::cout<<record.dump()<<std::endl;return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
