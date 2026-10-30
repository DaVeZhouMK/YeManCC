// Adversarial responses and IO/event ordering against the production observer.
// No Steam/CDP/process operations: reader results and events are inert fixtures.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_ui_mode_observer.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ymcc;
using Json=steamui::ModeJson;
static std::vector<std::string> passed,failed;
static void check(bool ok,const std::string& name){(ok?passed:failed).push_back(name);std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';}
template<class F> bool until(F f,DWORD timeout=1500){const auto end=GetTickCount64()+timeout;while(!f()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
static Json sample(const steamsession::Session& s){return {{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode","bigpicture"},{"modeEpoch","renderer:1"}};}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)throw std::runtime_error("isolated output required");std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
 const steamsession::Session session{L"X:\\inert-steam",123,0,100};
 // Coercible numeric values must never pass a process identity boundary.
 for(const auto& [name,pid]:std::vector<std::pair<std::string,Json>>{{"fractional pid",123.75},{"float integral pid",123.0},{"uint32 wraparound pid",uint64_t{0x100000000ULL+123}},{"negative wraparound pid",int64_t{-4294967173LL}},{"boolean pid",true},{"string pid","123"}}){
  steamui::ModeObserver observer;std::atomic<unsigned> reads{0},notifies{0};steamui::ModeObserverConfig cfg;cfg.debounceMs=1;cfg.minimumGapMs=5;cfg.failureGapMs=80;
  cfg.read=[&](const auto& s){++reads;auto v=sample(s);v["pid"]=pid;return v;};cfg.changed=[&](const auto&,const auto&){++notifies;};
  observer.initialize(cfg);observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);
  until([&]{return reads>=3||notifies>0;});Sleep(10);const auto result=observer.snapshot(session);observer.shutdown();
  check(notifies==0&&result.value("mode",std::string{})=="unknown",name+" is rejected without mode delivery");
 }
 // Related vocabulary: missing/null/wrong type/oversized/stale fields.
 for(const auto& [field,value]:std::vector<std::pair<std::string,Json>>{{"known",1},{"known",nullptr},{"running","true"},{"running",false},{"birth",100},{"birth","0100"},{"birth","101"},{"mode","stopped"},{"mode",nullptr},{"modeEpoch",Json::array()},{"modeEpoch",std::string(97,'x')}}){
  steamui::ModeObserver observer;std::atomic<unsigned> reads{0},notifies{0};steamui::ModeObserverConfig cfg;cfg.debounceMs=1;cfg.minimumGapMs=5;cfg.failureGapMs=80;
  cfg.read=[&](const auto& who){++reads;auto v=sample(who);v[field]=value;return v;};cfg.changed=[&](const auto&,const auto&){++notifies;};
  observer.initialize(cfg);observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);
  until([&]{return reads>=3||notifies>0;});Sleep(10);observer.shutdown();
  check(notifies==0,field+"="+value.dump()+" cannot publish a verified mode");
 }
 for(bool unsignedPid:{false,true}){
  steamui::ModeObserver observer;std::atomic<unsigned> deliveries{0};steamui::ModeObserverConfig cfg;cfg.debounceMs=1;
  cfg.read=[&](const auto& who){auto v=sample(who);v["pid"]=unsignedPid?Json(uint64_t{123}):Json(int64_t{123});return v;};cfg.changed=[&](const auto&,const auto&){++deliveries;};
  observer.initialize(cfg);observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);
  const bool accepted=until([&]{return deliveries==1;});observer.shutdown();check(accepted,unsignedPid?"valid unsigned integer PID remains accepted":"valid signed positive integer PID remains accepted");
 }
 // Every read is invalidated by a new window event while its IO is active.
 // This must consume the same three-attempt failure budget, not restart it.
 {
  steamui::ModeObserver observer;std::atomic<unsigned> reads{0},notifies{0};std::atomic<bool> interrupt{true};steamui::ModeObserverConfig cfg;
  cfg.debounceMs=1;cfg.minimumGapMs=12;cfg.failureGapMs=150;
  cfg.read=[&](const auto& s){++reads;if(interrupt)for(unsigned i=0;i<200;++i)observer.requestRefresh(s);return sample(s);};
  cfg.changed=[&](const auto&,const auto&){++notifies;};observer.initialize(cfg);observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);
  until([&]{return reads>=3;});Sleep(100);const auto exhausted=reads.load();check(exhausted==3&&notifies==0,"in-flight notification storms consume three attempts then park");
  interrupt=false;observer.requestRefresh(session);Sleep(15);check(reads==exhausted,"new event after exhaustion cannot bypass failure cooldown");
  check(until([&]{return notifies==1;}),"one later event recovers the parked reader after cooldown");
  Sleep(25);check(reads==exhausted+1&&notifies==1,"recovery makes one read without duplicate mode delivery");observer.shutdown();
 }
 // A lifecycle change invalidates old IO but cannot consume the NEW session's budget.
 {
  steamui::ModeObserver observer;std::atomic<bool> entered{false},release{false};std::atomic<unsigned> currentReads{0},oldDelivery{0};auto current=session;++current.created;
  steamui::ModeObserverConfig cfg;cfg.debounceMs=1;cfg.minimumGapMs=8;cfg.failureGapMs=100;
  cfg.read=[&](const auto& s){if(s==session){entered=true;while(!release)Sleep(1);return sample(s);}++currentReads;return Json{{"known",false}};};
  cfg.changed=[&](const auto& s,const auto&){if(s==session)++oldDelivery;};observer.initialize(cfg);observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);
  const bool reached=until([&]{return entered.load();});observer.observeSteam(current);release=true;until([&]{return currentReads>=3;});Sleep(30);observer.shutdown();
  check(reached&&currentReads==3&&oldDelivery==0,"session replacement owns a fresh independent three-read budget");
 }
 // A page can observe pending while a slow refresh of the SAME mode runs.
 // Its pending display must receive one completion notification without a CSS edge.
 {
  steamui::ModeObserver observer;std::atomic<unsigned> reads{0},edges{0},pageEvents{0};std::atomic<bool> entered{false},release{false};
  steamui::ModeObserverConfig cfg;cfg.debounceMs=1;cfg.minimumGapMs=8;cfg.failureGapMs=100;
  cfg.read=[&](const auto& s){if(++reads==2){entered=true;while(!release)Sleep(1);}return sample(s);};
  cfg.changed=[&](const auto&,const auto&){++edges;++pageEvents;};
#ifndef YMCC_BASELINE_METRICS
  cfg.settled=[&](const auto& s){observer.passiveSnapshot(s);++pageEvents;};
#endif
  observer.initialize(cfg);observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);
  until([&]{return edges==1;});observer.requestRefresh(session);const bool blocked=until([&]{return entered.load();});
  const auto pending=observer.passiveSnapshot(session);release=true;
  until([&]{return observer.snapshot(session).value("mode",std::string{})=="bigpicture";});Sleep(20);observer.shutdown();
  check(blocked&&pending.value("mode",std::string{})=="unknown"&&pageEvents==2&&edges==1,"same-mode completion repairs pending UI exactly once without another CSS/Decky edge");
 }

#ifndef YMCC_BASELINE_METRICS
 // Disable/re-enable while IO is blocked is a lifecycle change, not a hint.
 {
  steamui::ModeObserver observer;std::atomic<bool> entered{false},release{false};std::atomic<unsigned> reads{0},delivered{0};
  steamui::ModeObserverConfig cfg;cfg.debounceMs=1;cfg.minimumGapMs=8;cfg.failureGapMs=100;
  cfg.read=[&](const auto& who){if(++reads==1){entered=true;while(!release)Sleep(1);}return sample(who);};cfg.changed=[&](const auto&,const auto&){++delivered;};
  observer.initialize(cfg);observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);
  const bool blocked=until([&]{return entered.load();});observer.setDemand(steamui::ModeConsumer::Css,false);observer.setDemand(steamui::ModeConsumer::Css,true);release=true;
  const bool fresh=until([&]{return delivered==1;});Sleep(20);observer.shutdown();
  check(blocked&&fresh&&reads==2&&delivered==1,"off/on during IO drops the old result and preserves the new demand");
 }
 // A pending observer gets one failure settlement. Callback failure is isolated.
 {
  steamui::ModeObserver observer;std::atomic<bool> entered{false},release{false},healthy{false};std::atomic<unsigned> reads{0},settlements{0},deliveries{0};
  steamui::ModeObserverConfig cfg;cfg.debounceMs=1;cfg.minimumGapMs=8;cfg.failureGapMs=80;
  cfg.read=[&](const auto& who){if(++reads==1){entered=true;while(!release)Sleep(1);}if(!healthy)throw std::runtime_error("transport failed");return sample(who);};
  cfg.changed=[&](const auto&,const auto&){++deliveries;};cfg.settled=[&](const auto& who){observer.passiveSnapshot(who);++settlements;throw std::runtime_error("page callback failed");};
  observer.initialize(cfg);observer.setDemand(steamui::ModeConsumer::Css,true);observer.observeSteam(session);until([&]{return entered.load();});
  observer.passiveSnapshot(session);release=true;until([&]{return reads>=3;});Sleep(15);
  check(settlements==1&&deliveries==0&&reads==3,"pending failure is settled once and a throwing page callback cannot restart retries");
  healthy=true;observer.requestRefresh(session);const bool recovered=until([&]{return deliveries==1;});observer.shutdown();
  check(recovered,"reader recovers after page callback failure using a later valid event");
 }
#endif
 // Real observer callers retain failed-read attempts across same-process
 // account changes and demand pause/reopen, not just the policy unit API.
 for(bool accountChange:{false,true}){
  steamui::ModeObserver observer;std::atomic<unsigned> reads{0};steamui::ModeObserverConfig cfg;
  cfg.debounceMs=1;cfg.minimumGapMs=25;cfg.failureGapMs=150;
  cfg.read=[&](const auto&){++reads;return Json{{"known",false},{"reason","budget-fixture-unknown"}};};
  auto active=session;observer.initialize(cfg);observer.setDemand(steamui::ModeConsumer::Decky,true);observer.observeSteam(active);
  bool observed=true;
  for(unsigned i=1;i<=3;++i){
   observed&=until([&]{return reads>=i&&observer.snapshot(active).value("reason",std::string{})=="budget-fixture-unknown";});
   if(i<3){if(accountChange){++active.account;observer.observeSteam(active);}else{observer.setDemand(steamui::ModeConsumer::Decky,false);observer.setDemand(steamui::ModeConsumer::Decky,true);}}
  }
  Sleep(80);const auto count=reads.load();observer.shutdown();
  check(observed&&count==3,accountChange?"same-process account changes cannot refill unknown read attempts":"demand pause/reopen cannot refill unknown read attempts");
 }
 Json result{{"ok",failed.empty()},{"passed",passed.size()},{"cases",passed},{"failed",failed},{"realSteamOperations",0},{"realCssOperations",0}};
 std::ofstream f(out/L"adversarial.json");f<<result.dump(2);std::cout<<result.dump(2)<<'\n';return failed.empty()?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
