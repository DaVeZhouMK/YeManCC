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
static std::atomic<std::uint64_t> powerSequence{1},powerRevision{1};
static std::atomic<bool> powerPending{false};
static int __cdecl readReadyFixture(sleeppower::SharedPowerV1* value,std::uint32_t size){
 if(!value||size!=sizeof(*value))return 0;
 *value={};value->generation=currentPowerGeneration();value->suspendSerial=1;
 value->observedNotificationSequence=powerSequence;value->publicationRevision=powerRevision;
 value->phase=static_cast<std::uint32_t>(g_powerLifecycle.load());
 value->flags=sleeppower::kKnown|(value->phase==0?sleeppower::kHardwareAllowed:0u)|(powerPending?sleeppower::kNotificationPending:0u);
 return 1;
}
static bool captureSharedPowerLifecycle(sleeppower::SharedPowerV1& value){return sleeppower::capturePower(&readReadyFixture,value);}
static bool sharedPowerLifecycleCurrent(const sleeppower::SharedPowerV1& value){return sleeppower::powerCurrent(&readReadyFixture,value);}
static struct{Session value;Session sessionSnapshot(){return value;}}g_steamSettingsObserver;
static struct{steamcss::Runtime* actual=nullptr;unsigned publicAccepted=0;bool requestPublicResumeRecovery(const Session&s,const sleeppower::SharedPowerV1& power){const bool accepted=actual&&actual->requestPublicResumeRecovery(s,power);if(accepted)++publicAccepted;return accepted;}
#ifdef FIXTURE_BASELINE_ROUTE
 bool requestResumeRecovery(const Session&s,ULONGLONG generation){return actual&&actual->requestResumeRecovery(s,generation);}
#endif
 void observeMode(const Session&s,const steamcss::ModeSample& mode,bool waitingOnly){if(actual)actual->observeMode(s,mode,waitingOnly);}}g_steamCssGuard;
static struct{std::atomic<unsigned> invalidations=0;bool valid=true;bool invalidateForResume(const Session&){++invalidations;return valid;}}g_steamCssUse;
static struct{std::function<bool(const Session&)> port;std::function<json(const Session&)> facts;bool requestRefresh(const Session&s){return port&&port(s);}json snapshot(const Session&s){return facts?facts(s):json();}}g_steamModeObserver;
static void steamWallpaperForegroundWake(){}
static void deckySidebarResumeManagedMode(const Session&){}
static struct{bool requestLaunchAdmissionRefresh(){return false;}bool requestConnectionCheck(){return false;}}g_deckySidebarRuntime;
static void deckySidebarDiagnostic(const char*,const json&,bool){}
static void traceLog(const char*){}
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


static void verifyReadyFirst(const std::filesystem::path& root) {
 const auto dir=root/L"ready-first";std::filesystem::create_directories(dir);const auto link=dir/L"backend.path";
 {std::ofstream out(link);out<<"C:\\fixture-css\\CssLoader-Standalone-Headless.exe";}
 Session session{L"C:\\fixture-steam",42,123,100};g_steamSettingsObserver.value=session;
 deckylaunch::Handle backend,helper;backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);helper.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 check(backend.value&&helper.value,"ready-first owns inert backend/helper handles");
 std::atomic<unsigned> refreshes=0,uses=0;std::atomic<bool> confirmed=false;
 std::atomic<steamcss::UiMode> mode=steamcss::UiMode::BigPicture;
 steamcss::Runtime guard;g_steamCssGuard.actual=&guard;
 struct Stop {steamcss::Runtime& value;~Stop(){value.shutdown();g_steamCssGuard.actual=nullptr;}} stop{guard};
 steamcss::Config c;c.linkFile=link.wstring();c.delays={2,5,10};c.modeDelays={2,10,20};
 c.powerGeneration=[] {return currentPowerGeneration();};c.powerReader=&readReadyFixture;c.mayLaunch=[] {return g_powerLifecycle==PowerLifecycle::Ready&&!g_exitRequested;};
 c.readMode=[&](const auto&){return steamcss::ModeSample{mode.load(),"ready-first:1"};};
 g_steamModeObserver.port=[&](const auto& s){if(!(s==session))return false;++refreshes;mode=steamcss::UiMode::Unknown;return true;};
 c.refreshMode=productionCssResumeRefresh;
 c.ensure=[&](const auto&,const auto&,const auto&){return mode==steamcss::UiMode::BigPicture?steamcss::Result{copy(backend.value),900,"running",{},false}:steamcss::Result{nullptr,0,"waiting-steam-ui","mode-unconfirmed"};};
 c.confirmIdle=[&](const auto&,bool start,bool){check(!start,"ready-first reuses the existing backend");++uses;confirmed=false;ResetEvent(helper.value);return copy(helper.value);};
 c.isResidentConfirmed=[&](const auto&){return confirmed.load();};c.probeExisting=[&]{return steamcss::Result{copy(backend.value),900,"running",{},false};};
 check(guard.initialize(c),"ready-first uses the actual event-driven guard");guard.observeSteam(session);
 check(until([&]{return uses==1;}),"ready-first establishes one original appearance use");confirmed=true;SetEvent(helper.value);
 check(until([&]{return guard.snapshot().phase=="ready";}),"ready-first establishes original DOM confirmation");
 g_steamCssUse.invalidations=0;powerGeneration=80;
 const auto begin=GetTickCount64();check(steamAppearanceResumeCommitted(80),"ready-first committed Ready is admitted once");
 check(until([&]{return refreshes==1;},500),"ready-first requests public mode within500ms instead of fixed10s");
 const auto refreshMs=GetTickCount64()-begin;
 check(g_steamCssUse.invalidations==1,"only the worker invalidates before its one public mode request");
 const auto accepted=g_steamCssGuard.publicAccepted;
 steamAppearancePublicReady(80);
 check(g_steamCssGuard.publicAccepted==accepted,"committed Ready followed by public projection must not admit a second CSS wave");
 bool duplicatesRejected=true;for(unsigned i=0;i<1000;++i)if(steamAppearanceResumeCommitted(80))duplicatesRejected=false;check(duplicatesRejected,"ready-first duplicate Ready is rejected");
 Sleep(40);check(uses==1&&refreshes==1,"accepted refresh with unknown mode cannot apply CSS or replenish recovery");
 // The shared observer emits stableBigPicture after a fresh unchanged read.
 // Exercise the exact main callback after all short cache reads have parked.
 g_steamModeObserver.facts=[&](const Session&s){return json{{"known",true},{"running",true},{"mode",mode==steamcss::UiMode::BigPicture?"bigpicture":"unknown"},{"pid",s.pid},{"birth",std::to_string(s.created)},{"modeEpoch","ready-first:1"}};};
 productionCssStableMode(session);Sleep(20);check(uses==1,"stable callback cannot authorize unconfirmed cached mode");
 mode=steamcss::UiMode::BigPicture;productionCssStableMode(session);
 check(until([&]{return uses==2;},500),"fresh unchanged mode releases waiting CSS through the exact stable callback");
 const auto useMs=GetTickCount64()-begin;confirmed=true;SetEvent(helper.value);
 check(until([&]{return guard.snapshot().phase=="ready";}),"ready-first requires actual new helper confirmation");
 const auto before=refreshes.load();for(unsigned i=0;i<1000;++i)productionCssStableMode(session);Sleep(80);
 check(uses==2&&refreshes==before,"healthy ready-first completion parks without repeated backend or mode work");
 powerPending=true;++powerSequence;++powerRevision;
 check(!steamAppearanceResumeCommitted(80),"pending notification does not authorize the window route");
 steamAppearancePublicReady(80);
 check(g_steamCssGuard.publicAccepted==accepted,"pending notification does not authorize the projection route");
 powerPending=false;++powerRevision;
 steamAppearancePublicReady(80);
 check(g_steamCssGuard.publicAccepted==accepted+1&&!steamAppearanceResumeCommitted(80),"true new same-generation scope admits projection first and deduplicates the window route");
 check(until([&]{return refreshes==before+1;}),"second distinct scope reaches exactly one worker refresh");
 ++powerRevision;steamAppearancePublicReady(80);
 check(!steamAppearanceResumeCommitted(80)&&g_steamCssGuard.publicAccepted==accepted+1,"revision-only metadata cannot create another CSS wave");
 powerGeneration=81;g_powerLifecycle=PowerLifecycle::Suspended;
 check(!steamAppearanceResumeCommitted(81),"ready-first rejects nonReady before scheduling");g_powerLifecycle=PowerLifecycle::Ready;
 auto other=session;++other.created;check(!guard.requestResumeRecovery(other,81),"ready-first rejects changed Steam birth");
 std::filesystem::remove(link);check(until([&]{return guard.snapshot().phase=="disabled";}),"ready-first OFF reaches the actual guard");
 powerGeneration=82;check(!steamAppearanceResumeCommitted(82),"ready-first OFF cannot be revived by Ready");
 const json timing={{"publicRefreshAfterReadyMs",refreshMs},{"cssUseAfterReadyMs",useMs},{"refreshes",refreshes.load()},{"uses",uses.load()},{"realSteamOperations",0},{"realCssOperations",0},{"sameDocumentStableCallback",true}};
 std::ofstream(root/L"ready-first-timing.json")<<timing.dump(2);
}


static void verifyStableDuringEnsure(const std::filesystem::path& root,bool cancelAfterSignal) {
 const auto dir=root/(cancelAfterSignal?L"cancel-after-signal":L"signal-during-ensure");std::filesystem::create_directories(dir);const auto link=dir/L"backend.path";
 {std::ofstream out(link);out<<"C:\\fixture-css\\CssLoader-Standalone-Headless.exe";}
 Session session{L"C:\\fixture-steam",42,123,100};g_steamSettingsObserver.value=session;
 deckylaunch::Handle backend,helper,entered,release;
 backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);helper.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 entered.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);release.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 check(backend.value&&helper.value&&entered.value&&release.value,"inflight stable owns four inert event handles");
 std::atomic<unsigned> refreshes=0,uses=0,modeReads=0;std::atomic<bool> confirmed=false,holdEnsure=false;
 std::atomic<steamcss::UiMode> mode=steamcss::UiMode::BigPicture;
 steamcss::Runtime guard;g_steamCssGuard.actual=&guard;
 struct Stop {steamcss::Runtime& value;HANDLE release;~Stop(){SetEvent(release);value.shutdown();g_steamCssGuard.actual=nullptr;}} stop{guard,release.value};
 steamcss::Config c;c.linkFile=link.wstring();c.delays={0,30000,30000};
 // No cache timer can rescue a missed completion inside the test deadline.
 c.modeDelays={60000,60000,60000};c.powerGeneration=[] {return currentPowerGeneration();};c.powerReader=&readReadyFixture;
 c.mayLaunch=[] {return g_powerLifecycle==PowerLifecycle::Ready&&!g_exitRequested;};
 c.readMode=[&](const auto&){++modeReads;return steamcss::ModeSample{mode.load(),"stable-inflight:1"};};
 g_steamModeObserver.port=[&](const auto&s){if(!(s==session))return false;++refreshes;mode=steamcss::UiMode::Unknown;return true;};
 g_steamModeObserver.facts=[&](const Session&s){return json{{"known",true},{"running",true},{"mode",mode==steamcss::UiMode::BigPicture?"bigpicture":"unknown"},{"pid",s.pid},{"birth",std::to_string(s.created)},{"modeEpoch","stable-inflight:1"}};};
 c.refreshMode=productionCssResumeRefresh;
 c.ensure=[&](const auto&,const auto&,const auto&){
  const auto sampled=mode.load();
  if(holdEnsure){SetEvent(entered.value);if(WaitForSingleObject(release.value,3000)!=WAIT_OBJECT_0)throw std::runtime_error("inflight stable release unavailable");}
  return sampled==steamcss::UiMode::BigPicture?steamcss::Result{copy(backend.value),900,"running",{},false}:steamcss::Result{nullptr,0,"waiting-steam-ui","mode-unconfirmed"};
 };
 c.confirmIdle=[&](const auto&,bool start,bool){check(!start,"inflight stable always reuses the backend");++uses;confirmed=false;ResetEvent(helper.value);return copy(helper.value);};
 c.isResidentConfirmed=[&](const auto&){return confirmed.load();};c.probeExisting=[&]{return steamcss::Result{copy(backend.value),900,"running",{},false};};
 check(guard.initialize(c),"inflight stable uses the actual guard");guard.observeSteam(session);
 check(until([&]{return uses==1;}),"inflight stable establishes the initial use");confirmed=true;SetEvent(helper.value);
 check(until([&]{return guard.snapshot().phase=="ready";}),"inflight stable establishes initial DOM confirmation");
 g_steamCssUse.invalidations=0;holdEnsure=true;const auto generation=cancelAfterSignal?92ULL:91ULL;powerGeneration=generation;
 check(steamAppearanceResumeCommitted(generation),"inflight stable admits the current committed Ready");
 check(WaitForSingleObject(entered.value,1000)==WAIT_OBJECT_0,"inflight stable stops at the real ensure return boundary");
 const auto state=guard.snapshot();check(state.phase=="applying"&&state.reason=="resume-css-recheck","stable notification overlaps the precise wake recheck phase");
 productionCssStableMode(session);check(uses==1,"unknown stable snapshot cannot start a helper while ensure is held");
 mode=steamcss::UiMode::BigPicture;
 auto stale=session;++stale.created;productionCssStableMode(stale);check(uses==1,"stale Steam stable notification cannot start a helper");
 const auto sent=GetTickCount64();for(unsigned i=0;i<1000;++i)productionCssStableMode(session);
 if(cancelAfterSignal)g_powerLifecycle=PowerLifecycle::Suspended;
 holdEnsure=false;SetEvent(release.value);
 if(cancelAfterSignal){
  check(until([&]{return guard.snapshot().reason=="lifecycle-changed";}),"queued stable result is cancelled by a newer nonReady lifecycle");
  Sleep(80);check(uses==1&&refreshes==1,"cancelled stable mailbox creates no helper or recovery refill");g_powerLifecycle=PowerLifecycle::Ready;
 }else{
  check(until([&]{return uses==2;},500),"stable result arriving during ensure starts exactly one use without cache fallback");
  const auto accepted=GetTickCount64()-sent;
  const auto beforeReads=modeReads.load();for(unsigned i=0;i<1000;++i)productionCssStableMode(session);Sleep(30);
  check(uses==2&&refreshes==1&&modeReads==beforeReads,"inflight helper ignores repeated stable completions without new mode reads");
  confirmed=true;SetEvent(helper.value);check(until([&]{return guard.snapshot().phase=="ready";}),"inflight stable still requires a fresh DOM helper result");
  for(unsigned i=0;i<1000;++i)productionCssStableMode(session);Sleep(30);
  check(uses==2&&refreshes==1&&modeReads==beforeReads,"healthy stable completions do not replay or poll CSS");
  check(g_steamCssUse.invalidations==1,"inflight stable keeps the original one-worker invalidation");
  std::ofstream(dir/L"timing.json")<<json{{"signalToUseMs",accepted},{"modeCacheReads",modeReads.load()},{"modeRefreshes",refreshes.load()},{"uses",uses.load()},{"realSteamOperations",0},{"realCssOperations",0}}.dump(2);
 }
}

int wmain(int argc,wchar_t**argv){try{
 if(argc==3&&std::wstring(argv[1])==L"--stable-inflight") {const std::filesystem::path selected=argv[2];std::filesystem::create_directories(selected);verifyStableDuringEnsure(selected,false);verifyStableDuringEnsure(selected,true);const json record={{"ok",true},{"selection","stable-inflight"},{"checks",checks.load()},{"realSteamOperations",0},{"realCssOperations",0}};std::ofstream(selected/L"result.json")<<record.dump(2);std::cout<<record.dump()<<std::endl;return 0;}

 if(argc==3&&std::wstring(argv[1])==L"--ready-first") {const std::filesystem::path selected=argv[2];std::filesystem::create_directories(selected);verifyReadyFirst(selected);const json record={{"ok",true},{"selection","ready-first"},{"checks",checks.load()},{"realSteamOperations",0},{"realCssOperations",0}};std::ofstream(selected/L"result.json")<<record.dump(2);std::cout<<record.dump()<<std::endl;return 0;}
 if(argc!=2)throw std::runtime_error("Unknown or empty CSS resume selection");
 const auto projected=deckydiag::record("steam-css-resume",{{"steamPid",42},{"steamBirth","100"},{"token","DO_NOT_EXPORT"}});
 check(projected.value("steamPid",0)==42&&projected.value("steamBirth",std::string{})=="100"&&!projected.contains("token"),"real resume log filter retains exact Steam identity and drops secrets");
 steamcss::Config defaultPolicy;
 check(defaultPolicy.resumeDelays==std::array<DWORD,3>{0,30000,30000},"production wake requests fresh shared mode immediately with two finite thirty-second follow-ups");
 steamcss::Budget exactWake(defaultPolicy.resumeDelays);exactWake.begin(1000);
 check(exactWake.take(1000)&&!exactWake.take(30999)&&exactWake.take(31000)&&!exactWake.take(60999)&&exactWake.take(61000)&&exactWake.timeout(100000)==INFINITE,"unblocked production wake slots are exactly Ready now/plus30/plus60 seconds and then park");
 check(argc==2,"owned evidence directory required");const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);const auto link=out/L"links"/L"backend.path";std::filesystem::create_directories(link.parent_path());{std::ofstream f(link);f<<"C:\\fixture-css\\CssLoader-Standalone-Headless.exe";}
 steamcss::Runtime runtime;g_steamCssGuard.actual=&runtime;Session session{L"C:\\fixture-steam",42,0,100};g_steamSettingsObserver.value=session;
 deckylaunch::Handle backend,helper;backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);helper.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);check(backend.value&&helper.value,"owned backend/helper wait events created");
 std::atomic<bool>nextDocument=false;
 std::atomic<unsigned>modeReads=0,backendReads=0,helpers=0,refreshes=0,rebinds=0;std::atomic<bool>confirmed=false,refreshAccepted=true;std::atomic<steamcss::UiMode>mode=steamcss::UiMode::BigPicture;
 steamcss::Config config;config.linkFile=link.wstring();config.delays={2,5,10};config.modeDelays={2,10,20};config.resumeDelays={40,160,220};config.powerGeneration=[] {return currentPowerGeneration();};config.powerReader=&readReadyFixture;
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
