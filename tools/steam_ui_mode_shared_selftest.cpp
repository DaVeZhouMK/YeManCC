// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
// Production shared observer + Decky runtime; inert data, rejected preflight.
// No real Steam, plugin, configuration or device is touched.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "steam_ui_mode_observer.h"
#include "decky_sidebar_runtime.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
using namespace ymcc;
static unsigned passed=0;
static void expect(bool ok,const char* label){if(!ok)throw std::runtime_error(label);++passed;std::cout<<"PASS "<<label<<'\n';}
template<class F> bool until(F f,DWORD ms=1500){const auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(2);}return true;}
static steamui::ModeJson sample(const steamsession::Session& s,const char* mode="desktop",const char* epoch="renderer:1"){
    return {{"known",true},{"running",true},{"pid",s.pid},{"birth",std::to_string(s.created)},{"mode",mode},{"modeEpoch",epoch}};
}
int main(){try{
    steamui::ModeReadWindow window;const auto base=ULONGLONG{10000};
    expect(window.timeout(base)==INFINITE,"Idle observer has no timer");expect(window.request(base),"First hint owns one wake");
    expect(window.timeout(base)==600,"Window hints coalesce for 600ms");
    unsigned redundantWakes=0;for(unsigned i=0;i<10000;++i)redundantWakes+=window.request(base+500);
    expect(redundantWakes==0,"10000 pending hints do not wake the worker again");
    expect(window.timeout(base+500)==100,"Window storms cannot postpone the first read");
    expect(!window.take(base+599)&&window.take(base+600),"No early read");window.finish(base+600,true);
    expect(window.timeout(base+601)==INFINITE,"Known result returns to event wait");
    window.request(base+601);expect(window.timeout(base+601)==4999,"All consumers share the 5s minimum gap");
    expect(window.take(base+5600),"Only the next eligible read starts");window.finish(base+5600,false);
    expect(window.timeout(base+5600)==5000,"Unknown result has finite 5s retry");
    expect(window.take(base+10600),"Second unknown check");window.finish(base+10600,false);
    expect(window.take(base+15600),"Third unknown check");window.finish(base+15600,false);
    expect(window.timeout(base+15600)==INFINITE,"Three failures park without polling");
    window.request(base+15601);expect(window.timeout(base+15601)==29999,"New hints cannot bypass 30s failure cooldown");
    window.cancel();expect(window.timeout(base+16000)==INFINITE,"Withdrawn demand cancels deadlines");
    window.request(base+16000);expect(window.timeout(base+16000)==29600,"Disable/re-enable preserves cooldown");
    window.reset();window.request(base);expect(window.take(base+900000),"Late IO admits one read, not a catch-up burst");
    window.finish(base+900000,true);expect(window.timeout(base+900001)==INFINITE,"Long stall does not introduce recurring polling");

    steamsession::Session session{L"X:\\InertSteam",123,0,100};
    steamui::ModeObserver observer;std::atomic<unsigned> reads{0},notifications{0};std::atomic<bool> big{false},reentry{false};
    steamui::ModeObserverConfig config;config.debounceMs=25;config.minimumGapMs=80;config.failureGapMs=180;
    config.read=[&](const auto& s){++reads;return sample(s,big?"bigpicture":"desktop",reentry?"renderer:2":"renderer:1");};
    config.changed=[&](const auto& s,const auto&){++notifications;observer.snapshot(s);};
    expect(observer.initialize(config),"Shared worker initializes");observer.observeSteam(session);Sleep(60);
    expect(reads==0,"No enabled consumers means no reads");
    expect(observer.passiveSnapshot(session).is_null(),"No observer demand leaves legacy UI observation available");
    observer.setDemand(steamui::ModeConsumer::Decky,true);observer.setDemand(steamui::ModeConsumer::Css,true);
    expect(until([&]{return observer.snapshot(session).value("mode",std::string{})=="desktop";}),"Both consumers receive a verified initial mode");
    expect(reads==1&&notifications==1,"CSS and Decky demand starts one shared read");
    bool passiveCorrect=true;for(unsigned i=0;i<10000;++i)passiveCorrect&=observer.passiveSnapshot(session).value("mode",std::string{})=="desktop";
    expect(passiveCorrect,"Automatic page consumes shared cached mode");
    expect(reads==1,"10000 passive frontend reads add no CDP reads");
    auto mismatched=session;++mismatched.created;
    expect(observer.passiveSnapshot(mismatched).is_null(),"Old-session cache never replaces current frontend identity");
    for(unsigned i=0;i<10000;++i){observer.snapshot(session);}
    Sleep(120);expect(reads==1,"Snapshot consumers and idle time create no CDP calls");
    auto reused=session;reused.created++;
    expect(observer.snapshot(reused).value("mode",std::string{})=="unknown","PID reuse cannot consume the old birth snapshot");
    auto otherRoot=session;otherRoot.root=L"Y:\\OtherSteam";
    expect(!observer.requestRefresh(otherRoot),"Another root cannot request a refresh for this session");
    big=true;for(unsigned i=0;i<10000;++i)observer.requestRefresh(session);
    expect(until([&]{return observer.snapshot(session).value("mode",std::string{})=="bigpicture";}),"A genuine bigpicture confirmation is delivered");
    Sleep(100);expect(reads==2&&notifications==2,"An event storm starts one read and one mode notification");
    observer.requestRefresh(session);expect(until([&]{return reads==3;}),"Explicit recovery can refresh the shared snapshot");Sleep(30);
    expect(notifications==2,"Unchanged mode/epoch does not wake consumers again");
    reentry=true;observer.requestRefresh(session);expect(until([&]{return reads==4&&notifications==3;}),"Valve modeEpoch re-entry wakes both consumers even when current mode remains bigpicture");
    observer.setDemand(steamui::ModeConsumer::Decky,false);observer.requestRefresh(session);
    expect(until([&]{return reads==5;}),"Disabling Decky retains CSS demand");Sleep(30);
    observer.setDemand(steamui::ModeConsumer::Css,false);const auto disabled=reads.load();
    expect(!observer.requestRefresh(session),"No consumers rejects new hints");
    expect(observer.passiveSnapshot(session).is_null(),"Disabling all consumers restores legacy read admission");Sleep(220);expect(reads==disabled,"Disabled shared worker stays idle");observer.shutdown();

    // Real Decky admission worker wakes on shared completion, ahead of its 1s/30s timers.
    steamui::ModeObserver signals;deckysidebar::Runtime decky;std::atomic<unsigned> preflight{0},signalReads{0};
    deckysidebar::Config deckyConfig;deckyConfig.executable=L"NeverLaunch.exe";
    deckyConfig.preflight=[&]{++preflight;return std::string("inert-preflight-stop");};
    deckyConfig.launchAdmission=[&]{const auto state=signals.snapshot(session);const bool ready=state.value("mode",std::string{})=="bigpicture";return deckysidebar::LaunchAdmission{ready,"steam-ui-state-pending",true};};
    decky.initialize(deckyConfig);decky.observeSteam(1,true);decky.setEnabled(true);
    expect(until([&]{return decky.snapshot().phase=="waiting-manual-steam";}),"Decky waits for the shared true mode");
    config.read=[&](const auto& s){++signalReads;return sample(s,"bigpicture");};
    config.changed=[&](const auto&,const auto&){decky.requestLaunchAdmissionRefresh();};
    const auto started=GetTickCount64();signals.initialize(config);signals.setDemand(steamui::ModeConsumer::Decky,true);signals.setDemand(steamui::ModeConsumer::Css,true);signals.observeSteam(session);
    expect(until([&]{return preflight==1;}),"Shared bigpicture signal immediately resumes real Decky admission");
    expect(GetTickCount64()-started<800,"Confirmed mode does not wait for 1s or 30s recovery timer");
    expect(signalReads==1,"Decky wake consumes the same snapshot without another reader");
    expect(until([&]{return decky.snapshot().phase=="unavailable";}),"The original preflight still guards all launch paths");
    expect(decky.snapshot().pid==0,"No loader or real plugin was launched by the fixture");decky.shutdown();signals.shutdown();

    // A session change during blocked IO must reject the old delivery.
    steamui::ModeObserver stale;std::atomic<bool> entered{false},release{false};std::atomic<unsigned> oldNotifies{0};
    config.read=[&](const auto& s){if(s.created==session.created){entered=true;while(!release)Sleep(2);}return sample(s,"bigpicture");};
    config.changed=[&](const auto& s,const auto&){if(s.created==session.created)++oldNotifies;};
    stale.initialize(config);stale.setDemand(steamui::ModeConsumer::Decky,true);stale.observeSteam(session);
    expect(until([&]{return entered.load();}),"Old session fixture is in flight");stale.observeSteam(reused);release=true;
    expect(until([&]{return stale.snapshot(reused).value("mode",std::string{})=="bigpicture";}),"New birth gets its own verified mode");
    expect(oldNotifies==0&&stale.snapshot(session).value("mode",std::string{})=="unknown","Old IO cannot notify or populate a new session");stale.shutdown();

    // An event in flight invalidates that read and schedules just one following read.
    steamui::ModeObserver inFlight;entered=false;release=false;std::atomic<unsigned> flightReads{0},flightNotifies{0};
    config.read=[&](const auto& s){if(++flightReads==1){entered=true;while(!release)Sleep(2);}return sample(s,"bigpicture");};
    config.changed=[&](const auto&,const auto&){++flightNotifies;};
    inFlight.initialize(config);inFlight.setDemand(steamui::ModeConsumer::Css,true);inFlight.observeSteam(session);
    expect(until([&]{return entered.load();}),"Mode event fixture is in flight");
    expect(inFlight.passiveSnapshot(session).value("reason",std::string{})=="shared-mode-pending", "Pending shared read is returned without a competing frontend read");
    for(unsigned i=0;i<1000;++i)inFlight.requestRefresh(session);release=true;
    expect(until([&]{return flightNotifies==1;}),"Event arriving during IO is checked by a following read");Sleep(120);
    expect(flightReads==2&&flightNotifies==1,"In-flight event storm produces exactly one fresh delivery");inFlight.shutdown();

    steamui::ModeObserver malformed;std::atomic<unsigned> badReads{0};
    config.read=[&](const auto&){++badReads;return steamui::ModeJson{{"known","invalid"},{"reason",5}};};config.changed={};
    malformed.initialize(config);malformed.setDemand(steamui::ModeConsumer::Decky,true);malformed.observeSteam(session);
    expect(until([&]{return badReads==3;}),"Malformed reads use only three finite attempts");Sleep(200);
    expect(badReads==3&&malformed.snapshot(session).value("mode",std::string{})=="unknown","Malformed data cannot manufacture a mode or crash the worker");
    malformed.requestRefresh(session);expect(until([&]{return badReads>3;}),"A later explicit recovery renews parked observation");malformed.shutdown();
    std::cout<<"SHARED_STEAM_MODE_TEST_OK passed="<<passed<<" realSteamOperations=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
