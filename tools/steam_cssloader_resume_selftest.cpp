#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_cssloader_guard.h"
#include <json.hpp>
#include "decky_sidebar_diagnostics.h"
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

// Reuse the real guard with inert wait handles. Long in-flight work must not
// spend three recovery slots without performing one check, or create a busy wait.
static void verifyBusyRecovery(const std::filesystem::path& root,bool oldHelper) {
 const auto dir=root/(oldHelper?L"old-helper-pending":L"current-helper-pending");std::filesystem::create_directories(dir);
 const auto link=dir/L"backend.path";{std::ofstream out(link);out<<"C:\\fixture-css\\CssLoader-Standalone-Headless.exe";}
 Session session{L"C:\\fixture-steam",42,123,100};
 deckylaunch::Handle backend,helper;backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);helper.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 std::atomic<unsigned> uses=0,refreshes=0;std::atomic<bool> confirmed=false;
 std::atomic<steamcss::UiMode> mode=steamcss::UiMode::BigPicture;std::atomic<unsigned> doc=1;
 steamcss::Config c;c.linkFile=link.wstring();c.delays={2,5,10};c.modeDelays={2,10,20};c.resumeDelays={25,40,50};
 c.readMode=[&](const auto&){return steamcss::ModeSample{mode.load(),"busy-document:"+std::to_string(doc.load())};};
 c.refreshMode=[&](const auto&){++refreshes;confirmed=false;mode=steamcss::UiMode::Unknown;return true;};
 c.ensure=[&](const auto&,const auto&,const auto&){return mode==steamcss::UiMode::BigPicture?steamcss::Result{copy(backend.value),900,"running",{},false}:steamcss::Result{nullptr,0,"waiting-steam-ui","mode-unconfirmed"};};
 c.confirmIdle=[&](const auto&,bool start,bool){if(start)throw std::runtime_error("busy fixture forbids backend creation");++uses;confirmed=false;ResetEvent(helper.value);return copy(helper.value);};
 c.isResidentConfirmed=[&](const auto&){return confirmed.load();};c.probeExisting=[&]{return steamcss::Result{copy(backend.value),900,"running",{},false};};
 c.confirmExisting=[&](const auto&){confirmed=false;ResetEvent(helper.value);return copy(helper.value);};
 steamcss::Runtime guard;struct Stop {steamcss::Runtime& guard;~Stop(){guard.shutdown();}} stop{guard};
 check(guard.initialize(c),"busy recovery uses the actual guard worker");guard.observeSteam(session);
 check(until([&]{return uses==1;}),"busy scenario establishes one initial helper");confirmed=true;SetEvent(helper.value);
 check(until([&]{return guard.snapshot().phase=="ready";}),"busy scenario starts from a verified resident appearance");
 if(oldHelper){doc=2;guard.observeMode(session,{mode.load(),"busy-document:2"});check(until([&]{return uses==2;}),"a pre-resume document helper remains in flight");}
 check(guard.requestResumeRecovery(session,1),"busy scenario admits one fresh wake episode");
 check(until([&]{return refreshes==1;},500),"old in-flight helper cannot consume every wake slot without a refresh");
 mode=steamcss::UiMode::BigPicture;guard.observeMode(session,{mode.load(),"busy-document:"+std::to_string(doc.load())});
 const unsigned expectedUses=oldHelper?3:2;check(until([&]{return uses==expectedUses;}),"fresh shared mode owns one replacement DOM helper");
 const auto idleCpu=cpu();Sleep(220);const auto idleDelta=cpu()-idleCpu;
 check(refreshes==1&&uses==expectedUses&&idleDelta<=312500,"a current helper parks remaining wake slots without polling or overlap");
 confirmed=false;SetEvent(helper.value);check(until([&]{return guard.snapshot().reason=="appearance-unconfirmed";}),"DOM failure is retained instead of counted as ready");
 check(until([&]{return refreshes==2;}),"a slow failed DOM helper retains its next finite recovery slot");
 mode=steamcss::UiMode::BigPicture;guard.observeMode(session,{mode.load(),"busy-document:"+std::to_string(doc.load())});
 check(until([&]{return uses==expectedUses+1;}),"DOM retry starts only after the previous helper completes");confirmed=true;SetEvent(helper.value);
 check(until([&]{return guard.snapshot().phase=="ready";}),"later DOM confirmation closes the busy recovery episode");
 Sleep(160);check(refreshes==2&&uses==expectedUses+1,"busy recovery leaves no recurring checks after confirmation");
 if(!oldHelper){
  check(guard.requestResumeRecovery(session,2),"later Ready owns one pending settle check before a user edit");
  check(until([&]{return guard.snapshot().reason=="resume-css-settle-delay";}),"manual priority starts inside the real delayed wake episode");
  check(guard.requestReadOnlyRebind(session,1),"a completed user edit hands off one read-only verification");
  check(until([&]{return guard.snapshot().phase=="verifying";}),"read-only manual completion supersedes delayed wake ownership");
  confirmed=true;SetEvent(helper.value);check(until([&]{return guard.snapshot().phase=="ready";}),"current manual DOM verification may close the wake episode");
  Sleep(180);check(refreshes==2&&uses==expectedUses+1,"wake checks cannot invalidate or reapply over a later user edit");
 }
}


static void verifyHelperAdmission(const std::filesystem::path& root) {
 const auto dir=root/L"helper-admission";std::filesystem::create_directories(dir);const auto link=dir/L"backend.path";
 {std::ofstream f(link);f<<"C:\\fixture-css\\CssLoader-Standalone-Headless.exe";}
 Session session{L"C:\\fixture-steam",42,123,100};deckylaunch::Handle backend,helper;
 backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);helper.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 std::atomic<unsigned> attempts=0;std::atomic<bool> accept=false,confirmed=false,allowed=true;
 steamcss::Config c;c.linkFile=link.wstring();c.delays={10,30,60};c.modeDelays={2,10,20};
 c.mayLaunch=[&]{return allowed.load();};c.readMode=[](const auto&){return steamcss::ModeSample{steamcss::UiMode::BigPicture,"admission:1"};};
 c.ensure=[&](const auto&,const auto&,const auto&){return steamcss::Result{copy(backend.value),900,"running",{},false};};
 c.confirmIdle=[&](const auto&,bool,bool)->HANDLE{++attempts;if(!accept){if(attempts==1)throw std::runtime_error("owned temporary helper refusal");return nullptr;}ResetEvent(helper.value);return copy(helper.value);};
 c.isResidentConfirmed=[&](const auto&){return confirmed.load();};c.probeExisting=[&]{return steamcss::Result{copy(backend.value),900,"running",{},false};};
 steamcss::Runtime guard;struct Stop {steamcss::Runtime& value;~Stop(){value.shutdown();}} stop{guard};
 check(guard.initialize(c),"helper admission uses the actual worker");guard.observeSteam(session);
 check(until([&]{return attempts>=1;}),"temporary helper refusal reaches the original appearance boundary");
 check(until([&]{return attempts==3;}),"refused helper consumes only the original three finite startup attempts");
 Sleep(120);check(attempts==3&&guard.snapshot().phase!="failed","helper refusal parks without throwing out the worker or retrying forever");
 accept=true;guard.requestManualRecovery();check(until([&]{return attempts==4;}),"a later manual request can recover after the finite helper refusal");
 allowed=false;confirmed=true;SetEvent(helper.value);
 check(until([&]{return guard.snapshot().reason=="helper-completion-lifecycle-changed";}),"late helper confirmation cannot certify a suspended lifecycle");
 check(guard.snapshot().phase!="ready","lifecycle cancellation retains unconfirmed appearance state");
}

int wmain(int argc,wchar_t**argv){try{
 const auto projected=deckydiag::record("steam-css-resume",{{"steamPid",42},{"steamBirth","100"},{"token","DO_NOT_EXPORT"}});
 check(projected.value("steamPid",0)==42&&projected.value("steamBirth",std::string{})=="100"&&!projected.contains("token"),"real resume log filter retains exact Steam identity and drops secrets");
 steamcss::Config defaultPolicy;
 check(defaultPolicy.resumeDelays==std::array<DWORD,3>{10000,20000,30000},"production wake uses ten-second settle and finite twenty/thirty-second follow-ups");
 steamcss::Budget exactWake(defaultPolicy.resumeDelays);exactWake.begin(1000);
 check(!exactWake.take(10999)&&exactWake.take(11000)&&!exactWake.take(30999)&&exactWake.take(31000)&&!exactWake.take(60999)&&exactWake.take(61000)&&exactWake.timeout(100000)==INFINITE,"unblocked production wake slots are exactly Ready plus 10/30/60 seconds and then park");
 check(argc==2,"owned evidence directory required");const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);const auto link=out/L"links"/L"backend.path";std::filesystem::create_directories(link.parent_path());{std::ofstream f(link);f<<"C:\\fixture-css\\CssLoader-Standalone-Headless.exe";}
 steamcss::Runtime runtime;g_steamCssGuard.actual=&runtime;Session session{L"C:\\fixture-steam",42,0,100};g_steamSettingsObserver.value=session;
 deckylaunch::Handle backend,helper;backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);helper.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(backend.value&&helper.value,"owned backend/helper wait events created");
 std::atomic<bool>nextDocument=false;
 std::atomic<unsigned>modeReads=0,backendReads=0,helpers=0,refreshes=0,rebinds=0;std::atomic<bool>confirmed=false,refreshAccepted=true;std::atomic<steamcss::UiMode>mode=steamcss::UiMode::BigPicture;
 steamcss::Config config;config.linkFile=link.wstring();config.delays={2,5,10};config.modeDelays={2,10,20};config.resumeDelays={40,160,220};config.powerGeneration=[] {return currentPowerGeneration();};
 config.mayLaunch=[] {return !g_exitRequested&&g_powerLifecycle==PowerLifecycle::Ready;};
 config.readMode=[&](const auto&){++modeReads;return steamcss::ModeSample{mode.load(),nextDocument.load()?"next-document:2":"same-document:1"};};
 g_steamModeObserver.port=[&](const auto& queried){if(queried.account!=g_steamSettingsObserver.value.account)return false;++refreshes;mode=steamcss::UiMode::Unknown;return refreshAccepted.load();};
 config.refreshMode=productionCssResumeRefresh;
 config.ensure=[&](const auto&,const auto&,const auto&){if(mode!=steamcss::UiMode::BigPicture)return steamcss::Result{nullptr,0,"waiting-steam-ui","mode-unconfirmed"};++backendReads;return steamcss::Result{copy(backend.value),900,"running",{},false};};
 config.confirmIdle=[&](const auto&,bool start,bool renew){check(!start,"resume never creates a second backend when one already exists");++helpers;confirmed=false;ResetEvent(helper.value);return copy(helper.value);};
 config.isResidentConfirmed=[&](const auto&){return confirmed.load();};config.probeExisting=[&]{++rebinds;return steamcss::Result{copy(backend.value),900,"running",{},false};};
 struct ScopedRuntimeStop {steamcss::Runtime& value;~ScopedRuntimeStop(){g_steamCssGuard.actual=nullptr;value.shutdown();}} stopBeforePorts{runtime};
 check(runtime.initialize(config),"production CSS observer starts");runtime.observeSteam(session);
 check(until([&]{return helpers==1;}),"initial existing Big Picture gets one appearance use");confirmed=true;SetEvent(helper.value);check(until([&]{return runtime.snapshot().phase=="ready";}),"initial appearance is actually confirmed before resume");
 session.account=123;g_steamSettingsObserver.value=session;runtime.observeSteam(session);
 const auto beforeAccountReads=backendReads.load();Sleep(30);
 check(helpers==1&&backendReads==beforeAccountReads,"learning the Steam account updates metadata without refilling the startup budget");
 g_steamCssUse.valid=false;check(!productionCssResumeRefresh(session)&&refreshes==0,"actual resume refresh cannot bypass failed old-confirmation invalidation");g_steamCssUse.valid=true;
 g_powerLifecycle=PowerLifecycle::Resuming;check(!steamAppearanceResumeCommitted(10),"Resuming cannot authorize CSS correction before committed Ready");check(!productionCssResumeRefresh(session)&&refreshes==0,"actual refresh callback rejects uncommitted power state before shared mode IO");g_powerLifecycle=PowerLifecycle::Ready;
 check(!steamAppearanceResumeCommitted(0)&&!steamAppearanceResumeCommitted(9),"zero and stale power generations are rejected");g_aiFanMockSession.isolated=true;check(!steamAppearanceResumeCommitted(10),"device isolation blocks the actual production Ready callback");g_aiFanMockSession.isolated=false;
 check(steamAppearanceResumeCommitted(10),"actual committed Ready admits one CSS-only event");check(until([&]{return refreshes==1&&runtime.snapshot().phase=="waiting-steam-ui";}),"resume invalidates old shared mode before any CSS ensure can apply");
 const auto readyInvalidations=g_steamCssUse.invalidations.load();
 for(unsigned i=0;i<50000;++i)if(steamAppearanceResumeCommitted(10))throw std::runtime_error("duplicate Ready admitted");
 check(g_steamCssUse.invalidations==readyInvalidations,"duplicate Ready cannot repeatedly revoke current helper work");
 Sleep(30);check(refreshes==1&&helpers==1&&backendReads==1,"50000 repeated Ready events create no extra reads, helper or backend");
 mode=steamcss::UiMode::BigPicture;runtime.observeMode(session,{mode.load(),"same-document:1"});
 check(until([&]{return helpers==2;}),"same Steam PID and same document receive one recovery after fresh Big Picture confirmation");confirmed=true;SetEvent(helper.value);check(until([&]{return runtime.snapshot().reason=="appearance-applied-resident";}),"resume reports ready only after the new helper confirmation");
 for(unsigned i=0;i<1000;++i)runtime.observeMode(session,{mode.load(),"same-document:1"});Sleep(50);check(helpers==2,"unchanged mode signals do not repeat the resumed appearance use");
 const auto idleCpu=cpu();const auto idleReads=backendReads.load(),idleModes=modeReads.load();Sleep(200);const auto idleDelta=cpu()-idleCpu;check(backendReads==idleReads&&modeReads==idleModes&&idleDelta<=312500,"healthy resumed observer parks without polling or repeated lookup");
 powerGeneration=11;check(steamAppearanceResumeCommitted(11),"a later power generation gets one new finite check");check(until([&]{return refreshes==2;}),"new generation requests fresh shared mode once");mode=steamcss::UiMode::Desktop;runtime.observeMode(session,{mode.load(),"same-document:1"});Sleep(60);check(helpers==2,"a desktop Steam resume never becomes an appearance launch or mode switch");
 auto stale=session;++stale.created;check(!runtime.requestResumeRecovery(stale,12),"stale Steam birth cannot enqueue a resume correction");
 powerGeneration=13;refreshAccepted=false;const auto beforeFailure=refreshes.load();check(steamAppearanceResumeCommitted(13),"shared mode failure is handled on the worker");check(until([&]{return runtime.snapshot().reason=="resume-mode-refresh-unavailable";}),"failed mode refresh remains unconfirmed instead of using old mode");Sleep(20);check(helpers==2&&!steamAppearanceResumeCommitted(13),"failed resume does not replenish itself or launch a helper");refreshAccepted=true;
 check(until([&]{return refreshes>=beforeFailure+2;}),"transient mode-refresh failure is retried in the same finite wake episode");mode=steamcss::UiMode::BigPicture;runtime.observeMode(session,{mode.load(),"same-document:1"});
 check(until([&]{return helpers==3;}),"fresh same-document mode after delayed retry gets exactly one correction");confirmed=true;SetEvent(helper.value);check(until([&]{return runtime.snapshot().phase=="ready";}),"retry stops only on actual resident DOM confirmation");
 const auto settledRefreshes=refreshes.load();Sleep(250);check(refreshes==settledRefreshes&&helpers==3,"successful wake cancels all remaining recovery slots");
 powerGeneration=14;refreshAccepted=false;check(steamAppearanceResumeCommitted(14),"persistent unknown gets a finite recovery episode");const auto persistentBefore=refreshes.load();
 check(until([&]{return refreshes==persistentBefore+3;}),"persistent refresh failure consumes exactly three shared checks");Sleep(250);check(refreshes==persistentBefore+3&&helpers==3,"exhausted wake parks without scanning or creating a helper");
 nextDocument=true;refreshAccepted=true;mode=steamcss::UiMode::BigPicture;runtime.observeMode(session,{mode.load(),"next-document:2"});
 check(until([&]{return helpers==4;}),"a later genuine document event remains eligible after exhausted wake checks");
 powerGeneration=15;check(steamAppearanceResumeCommitted(15),"new Ready is queued while a previous helper still owns its old DOM use");
 check(until([&]{return runtime.snapshot().reason=="resume-css-settle-delay";}),"new wake has its own settle phase");
 const auto lateRebinds=rebinds.load(),lateRefreshes=refreshes.load();confirmed=true;SetEvent(helper.value);
 check(until([&]{return rebinds>lateRebinds;}),"pre-resume helper completion is consumed once");
 check(until([&]{return refreshes>lateRefreshes;}),"late pre-resume confirmation cannot cancel the fresh wake check");
 mode=steamcss::UiMode::BigPicture;runtime.observeMode(session,{mode.load(),"next-document:2"});
 check(until([&]{return helpers==5;}),"fresh post-resume mode receives one new DOM use after late old completion");
 confirmed=true;SetEvent(helper.value);check(until([&]{return runtime.snapshot().phase=="ready";}),"only the post-resume helper can complete the new wake episode");
 powerGeneration=16;check(steamAppearanceResumeCommitted(16),"old-generation settle can be queued");g_powerLifecycle=PowerLifecycle::Suspended;Sleep(70);check(helpers==5,"new suspension cancels pending wake work");g_powerLifecycle=PowerLifecycle::Ready;
 std::filesystem::remove(link);check(until([&]{return runtime.snapshot().phase=="disabled";}),"appearance OFF reaches the actual event-driven guard");powerGeneration=17;check(!steamAppearanceResumeCommitted(17),"saved OFF cannot be revived by resume");Sleep(30);check(helpers==5,"OFF retains zero resume side effects");
 runtime.shutdown();powerGeneration=18;check(!steamAppearanceResumeCommitted(18),"shutdown rejects a late committed Ready callback");g_steamCssGuard.actual=nullptr;
 verifyBusyRecovery(out,true);verifyBusyRecovery(out,false);verifyHelperAdmission(out);
 const json record={{"ok",true},{"checks",checks.load()},{"duplicateReadyEvents",50000},{"appearanceUses",helpers.load()},{"modeRefreshes",refreshes.load()},{"backendReads",backendReads.load()},{"idleSampleMs",200},{"idleCpuMs",idleDelta/10000.0},{"realSteamOperations",0},{"realCssOperations",0},{"physicalSleep",false},{"scope","Exact production Ready callback + actual CSS observer; shared mode and process/helper IO are owned inert boundaries"}};
 std::ofstream(out/L"result.json")<<record.dump(2);std::cout<<record.dump()<<std::endl;return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
