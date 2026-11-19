// Real shared observer + real CSS guard workers with mock reader/backend only.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_cssloader_guard.h"
#include "steam_ui_mode_observer.h"
#include <atomic>
#include <fstream>
#include <iostream>
using namespace ymcc;
static std::vector<std::string> cases;
static void check(bool value,const char* name){if(!value)throw std::runtime_error(name);cases.push_back(name);std::cout<<"PASS "<<name<<'\n';}
template<class F> static bool until(F f,DWORD ms=2000){const auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
static HANDLE duplicate(HANDLE source){HANDLE out=nullptr;if(!DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&out,SYNCHRONIZE,FALSE,0))throw std::runtime_error("duplicate fixture event");return out;}

// Same-document account publication can retire an old use. The original
// process/helper/transport leaves below remain owned, inert event ports.
static int accountReadyCase(const std::filesystem::path& out){
 DWORD initial=0,final=0;if(!GetProcessHandleCount(GetCurrentProcess(),&initial))throw std::runtime_error("initial HANDLE count");
 const auto scenario=[&](const char* label,unsigned cancellations,bool expectRecovery,bool blockMode,bool disable){
  const auto dir=out/label;std::filesystem::create_directories(dir);const auto link=dir/L"backend.path";
  {std::ofstream file(link);file<<"X:\\inert-css\\CssLoader-Standalone-Headless.exe";}
  deckylaunch::Handle completion,backend;completion.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!completion.value||!backend.value)throw std::runtime_error("owned account event");
  std::atomic<unsigned> uses{0},ensures{0},reads{0};std::atomic<bool> confirmed{false},valid{true},allowed{true};
  steamui::ModeObserver shared;steamcss::Runtime css;
  struct Stop{steamui::ModeObserver& shared;steamcss::Runtime& css;~Stop(){css.shutdown();shared.shutdown();}}stop{shared,css};
  steamui::ModeObserverConfig mode;mode.debounceMs=2;mode.minimumGapMs=4;mode.failureGapMs=40;
  mode.read=[&](const auto& s){++reads;if(!valid)return steamui::ModeJson{{"known",false},{"running",true},{"mode","unknown"}};return steamui::ModeJson{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","bigpicture"},{"modeEpoch","same-document:1"}};};
  mode.changed=[&](const auto& who,const auto& value){css.observeMode(who,{value.value("mode",std::string{})=="bigpicture"?steamcss::UiMode::BigPicture:steamcss::UiMode::Unknown,value.value("modeEpoch",std::string{})});};
  if(!shared.initialize(mode))throw std::runtime_error("actual account mode worker");
  steamcss::Config guard;guard.linkFile=link.wstring();guard.delays={60000,60000,60000};guard.modeDelays={2,10,40};guard.mayLaunch=[&]{return allowed.load();};
  guard.changed=[&](const auto& state){shared.setDemand(steamui::ModeConsumer::Css,state.linked);};
  guard.readMode=[&](const auto& s){const auto state=shared.snapshot(s);return steamcss::ModeSample{state.value("mode",std::string{})=="bigpicture"?steamcss::UiMode::BigPicture:steamcss::UiMode::Unknown,state.value("modeEpoch",std::string{})};};
  guard.ensure=[&](const auto&,const auto& s,const auto& current){++ensures;const auto state=shared.snapshot(s);if(!current()||!steamcss::appearanceModeReady(s,state))return steamcss::Result{nullptr,0,"waiting-steam-ui","mode-unconfirmed"};return steamcss::Result{duplicate(backend.value),880,"running","inert-backend"};};
  guard.confirmIdle=[&](const auto& s,bool start,bool){if(start||!steamcss::appearanceModeReady(s,shared.snapshot(s)))throw std::runtime_error("current account mode required");++uses;confirmed=false;ResetEvent(completion.value);return duplicate(completion.value);};
  guard.probeExisting=[&]{return steamcss::Result{duplicate(backend.value),880,"running","inert-backend"};};guard.isResidentConfirmed=[&](const auto&){return confirmed.load();};
  if(!css.initialize(guard))throw std::runtime_error("actual account CSS worker");steamsession::Session session{L"X:\\inert-steam",123,0,100};
  css.observeSteam(session);shared.observeSteam(session);check(until([&]{return uses==1&&css.snapshot().phase=="applying";}),"account fixture starts one current helper");
  for(unsigned n=0;n<cancellations;++n){
   const auto old=session;++session.account;
   if(blockMode)valid=false;
   if(disable){std::filesystem::remove(link);check(until([&]{return !css.snapshot().linked;}),"OFF arrives before account completion");}
   css.observeSteam(session);shared.observeSteam(session);confirmed=false;SetEvent(completion.value);
   if(disable){Sleep(50);check(uses==1,"OFF account completion cannot reapply CSS");break;}
   if(blockMode){
    check(until([&]{return css.snapshot().reason=="appearance-unconfirmed"||css.snapshot().reason=="appearance-account-recheck";}),"account cancellation remains unconfirmed");
    css.observeMode(old,{steamcss::UiMode::BigPicture,"same-document:1"});Sleep(35);check(uses==1,"stale delivered account cannot bypass current shared mode");
    valid=true;shared.requestRefresh(session);
   }
   if(n<2){
    const bool recovered=until([&]{return uses==n+2;},500);
    if(expectRecovery&&!recovered)throw std::runtime_error("account successor must resume remaining CSS attempt without a new document");
    check(recovered==expectRecovery,"account-only retirement follows its original bounded continuation");
    if(!expectRecovery)break;
   }else{Sleep(60);check(uses==3&&css.snapshot().attempts==3,"account changes cannot refill three original startup attempts");}
  }
  if(expectRecovery&&!disable&&cancellations<3){confirmed=true;SetEvent(completion.value);check(until([&]{return css.snapshot().phase=="ready";}),"successor helper alone confirms current resident appearance");const auto beforeUses=uses.load(),beforeEnsures=ensures.load();++session.account;css.observeSteam(session);shared.observeSteam(session);Sleep(50);check(uses==beforeUses&&ensures==beforeEnsures,"healthy metadata publication does not reapply or refill budget");}
  const auto beforeUses=uses.load();const auto beforeEnsures=ensures.load();Sleep(50);check(uses==beforeUses&&ensures==beforeEnsures,"account scenario parks without repeated backend work");
  css.shutdown();shared.shutdown();std::filesystem::remove(link);check(std::filesystem::remove(dir),"owned account directory returned");
 };
 std::filesystem::create_directories(out);
 scenario("startup-account-ready",1,true,false,false);
 scenario("bounded-account-changes",3,true,false,false);
 scenario("pending-shared-account",1,true,true,false);
 scenario("disabled-account",1,true,false,true);
 check(GetProcessHandleCount(GetCurrentProcess(),&final)&&initial==final,"both real workers and all account scenario HANDLEs returned");
 std::cout<<nlohmann::json{{"ok",true},{"selection","account-ready"},{"passed",cases.size()},{"cases",cases},{"handlesBefore",initial},{"handlesAfter",final},{"realSteamOperations",0},{"realCssOperations",0}}.dump(2)<<'\n';return 0;
}

// Same-document account publication can retire an old use. The original
// process/helper/transport leaves below remain owned, inert event ports.
static int identityReadyCase(const std::filesystem::path& out){
 DWORD initial=0,final=0;if(!GetProcessHandleCount(GetCurrentProcess(),&initial))throw std::runtime_error("initial HANDLE count");
 const auto scenario=[&](const char* label,unsigned cancellations,bool expectRecovery,bool blockMode,bool disable){
  const auto dir=out/label;std::filesystem::create_directories(dir);const auto link=dir/L"backend.path";
  {std::ofstream file(link);file<<"X:\\inert-css\\CssLoader-Standalone-Headless.exe";}
  deckylaunch::Handle completion,backend;completion.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!completion.value||!backend.value)throw std::runtime_error("owned account event");
  std::atomic<uint64_t> identityEpoch{1};
  std::atomic<unsigned> uses{0},ensures{0},reads{0};std::atomic<bool> confirmed{false},valid{true},allowed{true};
  steamui::ModeObserver shared;steamcss::Runtime css;
  struct Stop{steamui::ModeObserver& shared;steamcss::Runtime& css;~Stop(){css.shutdown();shared.shutdown();}}stop{shared,css};
  steamui::ModeObserverConfig mode;mode.debounceMs=2;mode.minimumGapMs=4;mode.failureGapMs=40;
  mode.read=[&](const auto& s){++reads;if(!valid)return steamui::ModeJson{{"known",false},{"running",true},{"mode","unknown"}};return steamui::ModeJson{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","bigpicture"},{"modeEpoch","same-document:1"}};};
  mode.changed=[&](const auto& who,const auto& value){css.observeMode(who,{value.value("mode",std::string{})=="bigpicture"?steamcss::UiMode::BigPicture:steamcss::UiMode::Unknown,value.value("modeEpoch",std::string{})});};
  if(!shared.initialize(mode))throw std::runtime_error("actual account mode worker");
  steamcss::Config guard;guard.linkFile=link.wstring();guard.delays={60000,60000,60000};guard.modeDelays={2,10,40};guard.mayLaunch=[&]{return allowed.load();};
  guard.changed=[&](const auto& state){shared.setDemand(steamui::ModeConsumer::Css,state.linked);};
  guard.readMode=[&](const auto& s){const auto state=shared.snapshot(s);return steamcss::ModeSample{state.value("mode",std::string{})=="bigpicture"?steamcss::UiMode::BigPicture:steamcss::UiMode::Unknown,state.value("modeEpoch",std::string{})};};
  guard.ensure=[&](const auto&,const auto& s,const auto& current){++ensures;const auto state=shared.snapshot(s);if(!current()||!steamcss::appearanceModeReady(s,state))return steamcss::Result{nullptr,0,"waiting-steam-ui","mode-unconfirmed"};return steamcss::Result{duplicate(backend.value),880,"running","inert-backend"};};
  guard.confirmIdle=[&](const auto& s,bool start,bool){if(start||!steamcss::appearanceModeReady(s,shared.snapshot(s)))throw std::runtime_error("current account mode required");++uses;confirmed=false;ResetEvent(completion.value);return duplicate(completion.value);};
  guard.probeExisting=[&]{return steamcss::Result{duplicate(backend.value),880,"running","inert-backend"};};guard.isResidentConfirmed=[&](const auto&){return confirmed.load();};

  const auto bindIdentity=[&]<class T>(T& value){if constexpr(requires{value.confirmObserved;value.identityCurrent;}){
   using Observation=typename std::decay_t<decltype(value.confirmObserved)>::result_type;
   const auto legacy=value.confirmIdle;
   value.confirmObserved=[&,legacy](const auto& s,bool start,bool renew,const sleeppower::SharedPowerV1* = nullptr){Observation result;result.completion=legacy(s,start,renew);auto identity=std::make_shared<steamsession::IdentitySnapshot>();identity->session=s;identity->presence=1;identity->processEpoch=1;identity->accountEpoch=identityEpoch.load();result.identity=std::move(identity);return result;};
   value.identityCurrent=[&](const auto& expected){return expected&&expected->accountEpoch==identityEpoch.load();};
   value.refreshMode=[&](const auto& s){return shared.requestRefresh(s);};
  }};bindIdentity(guard);
  if(!css.initialize(guard))throw std::runtime_error("actual account CSS worker");steamsession::Session session{L"X:\\inert-steam",123,0,100};
  css.observeSteam(session);shared.observeSteam(session);check(until([&]{return uses==1&&css.snapshot().phase=="applying";}),"account fixture starts one current helper");
  for(unsigned n=0;n<cancellations;++n){
   const auto old=session;++identityEpoch;
   if(blockMode)valid=false;
   if(disable){std::filesystem::remove(link);check(until([&]{return !css.snapshot().linked;}),"OFF arrives before account completion");}
   css.observeSteam(session);shared.observeSteam(session);confirmed=false;SetEvent(completion.value);
   if(disable){Sleep(50);check(uses==1,"OFF account completion cannot reapply CSS");break;}
   if(blockMode){
    check(until([&]{return css.snapshot().reason=="appearance-unconfirmed"||css.snapshot().reason=="appearance-account-recheck";}),"account cancellation remains unconfirmed");
    css.observeMode(old,{steamcss::UiMode::BigPicture,"same-document:1"});Sleep(35);check(uses==1,"pre-refresh delivery cannot bypass current shared mode");
    valid=true;shared.requestRefresh(session);
   }
   if(n<2){
    const bool recovered=until([&]{return uses==n+2;},500);
    if(expectRecovery&&!recovered)throw std::runtime_error("account successor must resume remaining CSS attempt without a new document");
    check(recovered==expectRecovery,"account-only retirement follows its original bounded continuation");
    if(!expectRecovery)break;
   }else{Sleep(60);check(uses==3&&css.snapshot().attempts==3,"account changes cannot refill three original startup attempts");}
  }
  if(expectRecovery&&!disable&&cancellations<3){confirmed=true;SetEvent(completion.value);check(until([&]{return css.snapshot().phase=="ready";}),"successor helper alone confirms current resident appearance");const auto beforeUses=uses.load(),beforeEnsures=ensures.load();++identityEpoch;css.observeSteam(session);shared.observeSteam(session);Sleep(50);check(uses==beforeUses&&ensures==beforeEnsures,"healthy metadata publication does not reapply or refill budget");}
  const auto beforeUses=uses.load();const auto beforeEnsures=ensures.load();Sleep(50);check(uses==beforeUses&&ensures==beforeEnsures,"account scenario parks without repeated backend work");
  css.shutdown();shared.shutdown();std::filesystem::remove(link);check(std::filesystem::remove(dir),"owned account directory returned");
 };
 std::filesystem::create_directories(out);
 scenario("startup-account-ready",1,true,false,false);
 scenario("bounded-account-changes",3,true,false,false);
 scenario("pending-shared-account",1,true,true,false);
 scenario("disabled-account",1,true,false,true);
 check(GetProcessHandleCount(GetCurrentProcess(),&final)&&initial==final,"both real workers and all account scenario HANDLEs returned");
 std::cout<<nlohmann::json{{"ok",true},{"selection","identity-ready"},{"passed",cases.size()},{"cases",cases},{"handlesBefore",initial},{"handlesAfter",final},{"realSteamOperations",0},{"realCssOperations",0}}.dump(2)<<'\n';return 0;
}

static int accountObservationCase(const std::filesystem::path& out, bool delayedDelivery = false) {
 DWORD initial=0,final=0;
 check(GetProcessHandleCount(GetCurrentProcess(),&initial)!=FALSE,"account observation fixture handle baseline");
 for (const bool cached : {true,false}) {
  const auto dir=out/(cached?L"cached":L"in-flight");
  if(!std::filesystem::create_directories(dir))throw std::runtime_error("fresh account observation directory required");
  const auto link=dir/L"backend.path";
  {std::ofstream file(link);file<<"X:\\inert-css\\CssLoader-Standalone-Headless.exe";}
  deckylaunch::Handle completion,backend,releaseRead;
  completion.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);backend.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  releaseRead.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  if(!completion.value||!backend.value||!releaseRead.value)throw std::runtime_error("account observation event");
  std::atomic<unsigned> uses{0},reads{0},joins{0},forcedRefreshes{0};
  std::atomic<uint64_t> epoch{1};std::atomic<bool> confirmed{false};
  steamui::ModeObserver shared;steamcss::Runtime css;
  struct Stop{HANDLE release;steamui::ModeObserver& shared;steamcss::Runtime& css;~Stop(){SetEvent(release);css.shutdown();shared.shutdown();}}stop{releaseRead.value,shared,css};
  const steamsession::Session startup{L"X:\\inert-steam",123,0,100};
  auto replacement=startup;replacement.account=42;
  const auto validMode=[](const auto& s) {return steamui::ModeJson{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","bigpicture"},{"modeEpoch","same-document:1"}};};
  steamui::ModeObserverConfig mode;mode.debounceMs=3;mode.minimumGapMs=8;mode.failureGapMs=100;
  mode.read=[&](const auto& s){const auto read=++reads;if(!cached&&read==2&&WaitForSingleObject(releaseRead.value,4000)!=WAIT_OBJECT_0)throw std::runtime_error("held account read timed out");return validMode(s);};
  mode.changed=[&](const auto& s,const auto& value){css.observeMode(s,{steamcss::UiMode::BigPicture,value.value("modeEpoch",std::string{})});};
  if(!shared.initialize(mode))throw std::runtime_error("real mode observer unavailable");
  steamcss::Config guard;guard.linkFile=link.wstring();guard.delays={60000,60000,60000};guard.modeDelays={3,15,100};
  guard.changed=[&](const auto& state){shared.setDemand(steamui::ModeConsumer::Css,state.linked);};
  guard.readMode=[&](const auto& s){const auto v=shared.snapshot(s);return steamcss::ModeSample{steamcss::appearanceModeReady(s,v)?steamcss::UiMode::BigPicture:steamcss::UiMode::Unknown,v.value("modeEpoch",std::string{})};};
  guard.refreshMode=[&](const auto& s){++forcedRefreshes;return shared.requestRefresh(s);};
  [&]<class C,class O>(C& config,O& observer){if constexpr(requires{config.hasAccountModeObservation;observer.hasAccountObservation(startup);}){
   config.hasAccountModeObservation=[&](const auto& s){const auto has=observer.hasAccountObservation(s);if(has)++joins;return has;};
  }}(guard,shared);
  [&]<class C>(C& config){if constexpr(requires{config.accountPublicationAhead;}){
   config.accountPublicationAhead=[&](const auto& s){return epoch.load()>1&&steamcss::sameSession(s,replacement)&&s.account!=replacement.account;};
  }}(guard);
  guard.ensure=[&](const auto&,const auto& s,const auto& ready){if(!ready()||!steamcss::appearanceModeReady(s,shared.snapshot(s)))return steamcss::Result{nullptr,0,"waiting-steam-ui","mode-unconfirmed"};return steamcss::Result{duplicate(backend.value),880,"running","inert-backend"};};
  guard.confirmObserved=[&](const auto& s,bool start,bool,const sleeppower::SharedPowerV1* = nullptr){
   if(start||!steamcss::appearanceModeReady(s,shared.snapshot(s)))throw std::runtime_error("actual shared snapshot required");
   auto identity=std::make_shared<steamsession::IdentitySnapshot>();identity->session=s;identity->presence=1;identity->accountEpoch=epoch.load();
   ResetEvent(completion.value);++uses;
   return steamcss::ObservedUse{duplicate(completion.value),std::move(identity)};
  };
  guard.identityCurrent=[&](const auto& identity){return identity&&identity->accountEpoch==epoch.load();};
  guard.probeExisting=[&]{return steamcss::Result{duplicate(backend.value),880,"running","inert-backend"};};
  guard.isResidentConfirmed=[&](const auto&){return confirmed.load();};
  if(!css.initialize(guard))throw std::runtime_error("real CSS guard unavailable");
  css.observeSteam(startup);shared.observeSteam(startup);
  check(until([&]{return uses==1&&css.snapshot().phase=="applying";}),"one startup helper with account pending");
  ++epoch;if(!delayedDelivery)css.observeSteam(replacement);shared.observeSteam(replacement);
  check(until([&]{return cached?steamcss::appearanceModeReady(replacement,shared.snapshot(replacement)):reads==2;}),"replacement mode observation precedes old helper completion");
  SetEvent(completion.value);
  if(delayedDelivery){
   check(until([&]{const auto state=css.snapshot();return state.reason=="appearance-awaiting-account-observation"||state.reason=="appearance-identity-refresh-unavailable";}),"old helper closes before new account reaches guard");
   check(uses==1&&css.snapshot().attempts==1,"late delivery cannot spend a CSS attempt under the old account");
   css.observeSteam(replacement);
  }
  check(until([&]{return joins||forcedRefreshes;}),"guard handles retired helper");
  if(!cached)check(uses==1,"unfinished replacement read does not authorize CSS");
  SetEvent(releaseRead.value);
  check(until([&]{return uses==2&&css.snapshot().phase=="applying";}),delayedDelivery?"late account delivery must resume the parked CSS continuation":"shared result starts one successor helper");
  confirmed=true;SetEvent(completion.value);
  check(until([&]{return css.snapshot().phase=="ready";}),"successor confirms current resident appearance");
  check(reads==2&&joins==1&&forcedRefreshes==0,"replacement mode observation must not be invalidated or duplicated");
  check(css.snapshot().attempts==2,"same-document account update spends the remaining budget");
  css.shutdown();shared.shutdown();std::filesystem::remove(link);check(std::filesystem::remove(dir),"account observation directory returned");
 }
 check(GetProcessHandleCount(GetCurrentProcess(),&final)&&initial==final,"account observation workers and HANDLEs returned");
 std::cout<<nlohmann::json{{"ok",true},{"selection",delayedDelivery?"account-delivery":"account-observation"},{"cases",cases},{"passed",cases.size()}}.dump(2)<<'\n';return 0;
}

int wmain(int argc,wchar_t** argv){try{
 if(argc==4&&std::wstring(argv[1])==L"--case"&&std::wstring(argv[2])==L"account-observation")return accountObservationCase(std::filesystem::path(argv[3]));
 if(argc==4&&std::wstring(argv[1])==L"--case"&&std::wstring(argv[2])==L"account-delivery")return accountObservationCase(std::filesystem::path(argv[3]),true);
 if(argc==4&&std::wstring(argv[1])==L"--case"&&std::wstring(argv[2])==L"identity-ready")return identityReadyCase(std::filesystem::path(argv[3]));
 if(argc==4&&std::wstring(argv[1])==L"--case"){if(std::wstring(argv[2])!=L"account-ready"){std::cerr<<"UNKNOWN_SHARED_CHAIN_CASE\n";return 2;}return accountReadyCase(std::filesystem::path(argv[3]));}
 if(argc!=2)throw std::runtime_error("isolated output required");const auto out=std::filesystem::path(argv[1]);std::filesystem::create_directories(out);
 const auto link=out/L"link.txt";auto enable=[&]{std::ofstream f(link);f<<"X:\\inert-css\\CssLoader-Standalone-Headless.exe";};enable();
 const steamsession::Session session{L"X:\\inert-steam",123,0,100};
 deckylaunch::Handle completion;completion.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 std::atomic<unsigned> reads{0},edges{0},uses{0},renews{0},cssReads{0},entry{1};std::atomic<bool> big{false},malformed{false},throwRead{false};
 steamui::ModeObserver shared;steamcss::Runtime css;
 steamui::ModeObserverConfig mode;mode.debounceMs=4;mode.minimumGapMs=25;mode.failureGapMs=100;
 mode.read=[&](const auto& s){++reads;if(throwRead)throw std::runtime_error("fixture transport unavailable");steamui::ModeJson result{{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},
  {"mode",big?"bigpicture":"desktop"},{"modeEpoch","renderer:"+std::to_string(entry.load())}};if(malformed)result["pid"]=uint64_t{0x100000000ULL}+s.pid;return result;};
 mode.changed=[&](const auto& who,const auto& value){++edges;css.observeMode(who,{value.value("mode",std::string{})=="bigpicture"?steamcss::UiMode::BigPicture:steamcss::UiMode::Desktop,value.value("modeEpoch",std::string{})});};
 check(shared.initialize(mode),"shared mode worker initialized");
 steamcss::Config guard;guard.linkFile=link.wstring();guard.delays={5000,5000,5000};guard.modeDelays={3,15,100};
 guard.readMode=[&](const auto& s)->steamcss::ModeSample{++cssReads;const auto v=shared.snapshot(s);const auto m=v.value("mode",std::string{});return {m=="bigpicture"?steamcss::UiMode::BigPicture:m=="desktop"?steamcss::UiMode::Desktop:steamcss::UiMode::Unknown,v.value("modeEpoch",std::string{})};};
 guard.ensure=[](const auto&,const auto&,const auto&){return steamcss::Result{nullptr,0,"missing",{}};};
 guard.confirmIdle=[&](const auto&,bool start,bool renew){if(!start)throw std::runtime_error("expected missing mock backend");++uses;if(renew)++renews;return duplicate(completion.value);};
 guard.isIdle=[](const auto&){return true;};guard.changed=[&](const auto& s){shared.setDemand(steamui::ModeConsumer::Css,s.linked);};
 check(css.initialize(guard),"CSS worker initialized with the real shared-demand callback");shared.observeSteam(session);css.observeSteam(session);
 check(until([&]{return shared.snapshot(session).value("mode",std::string{})=="desktop"&&edges==1;}),"desktop baseline crosses the shared observer and CSS consumer");
 check(uses==0,"desktop detection cannot create a CSS mode-entry cycle");
 big=true;for(unsigned i=0;i<1000;++i)shared.requestRefresh(session);
 check(until([&]{return uses==1;}),"external bigpicture entry reaches CSS once through production workers");
 for(unsigned i=0;i<1000;++i)shared.requestRefresh(session);Sleep(120);
 check(uses==1&&renews==1,"same epoch event storms do not duplicate the CSS cycle");
 SetEvent(completion.value);check(until([&]{return css.snapshot().phase=="idle";}),"CSS completion parks the guard");
 // A rapid exit/re-entry between native reads is represented by a new renderer epoch.
 ResetEvent(completion.value);++entry;shared.requestRefresh(session);
 check(until([&]{return uses==2;}),"same PID and same bigpicture mode with a new epoch reapplies CSS once");
 SetEvent(completion.value);check(until([&]{return css.snapshot().phase=="idle";}),"second mode cycle also parks");
 std::filesystem::remove(link);check(until([&]{return css.snapshot().phase=="disabled";}),"unlink removes CSS demand through the directory event");
 Sleep(50);const auto disabledReads=reads.load();for(unsigned i=0;i<1000;++i)shared.requestRefresh(session);Sleep(80);
 check(reads==disabledReads&&uses==2,"no consumers means no CDP reader or CSS use after disable");
 // Decky can retain the shared reader independently, but cannot enable CSS.
 shared.setDemand(steamui::ModeConsumer::Decky,true);++entry;shared.requestRefresh(session);Sleep(100);
 check(reads>disabledReads&&uses==2,"Decky demand keeps detection alive without re-enabling CSS");
 malformed=true;++entry;shared.requestRefresh(session);
 check(until([&]{return shared.snapshot(session).value("mode",std::string{})=="unknown";}),"malformed refresh invalidates the previous verified mode before enable");
 enable();check(until([&]{return css.snapshot().linked;}),"explicit re-enable restores CSS consumer demand");Sleep(140);
 check(uses==2&&shared.snapshot(session).value("mode",std::string{})=="unknown","malformed common-interface identity never reaches CSS admission");
 malformed=false;throwRead=true;shared.requestRefresh(session);Sleep(200);
 check(uses==2,"transport exceptions cannot manufacture a bigpicture edge or launch CSS");
 throwRead=false;ResetEvent(completion.value);shared.requestRefresh(session);
 check(until([&]{return uses==3;}),"fresh valid event recovers enabled CSS after failures without restarting Steam");
 // Pending old read/session cannot survive Steam exit.
 shared.observeSteam({});css.observeSteam({});check(until([&]{return !css.snapshot().steamRunning;}),"Steam exit clears CSS session state");
 const auto stoppedReads=reads.load();for(unsigned i=0;i<1000;++i)shared.requestRefresh(session);Sleep(100);
 check(reads==stoppedReads&&uses==3,"late old-session hints cannot restart the reader or CSS");
 css.shutdown();shared.shutdown();
 nlohmann::json record{{"ok",true},{"passed",cases.size()},{"cases",cases},{"realSteamOperations",0},{"realCssOperations",0},{"cssUseCallbacks",uses.load()},{"modeReads",reads.load()}};
 std::ofstream f(out/L"chain.json");f<<record.dump(2);std::cout<<record.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
