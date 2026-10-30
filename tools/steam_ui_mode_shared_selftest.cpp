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

    // Virtual-clock policy tests: event storms/account updates cannot refill
    // startup credit, and absent APIs keep the original slow three-try budget.
    {
        steamui::ModeReadWindow cold(10,5000,30000);ULONGLONG now=10000;
        cold.beginStartup(now);
        expect(cold.timeout(now)==INFINITE,"Opening startup credit alone creates no timer");
        for(unsigned i=0;i<6;++i){
            cold.request(now);now+=cold.timeout(now);
            expect(cold.take(now),"A requested startup read spends one finite credit");cold.finish(now,true);
            expect(cold.timeout(now)==INFINITE,"A known startup result returns to event wait");
            if(i<5){cold.request(now);expect(cold.timeout(now)==500,"Cold startup keeps a 500ms minimum gap");}
        }
        cold.reset(true);cold.request(now);
        expect(cold.timeout(now)==5000,"Account-only observation preserves exhausted credit and its 5s cooldown");
        cold.reset();cold.beginStartup(30000);cold.request(30000);cold.take(30010);cold.finish(30010,false);
        expect(cold.timeout(30010)==5000,"Unknown startup IO does not spend slow-PC retries faster");
        cold.take(35010);cold.finish(35010,false);cold.take(40010);cold.finish(40010,false);
        expect(cold.timeout(40010)==INFINITE,"Unknown startup IO still parks after exactly three attempts");
        cold.request(40011);expect(cold.timeout(40011)>=29999,"New hints cannot bypass failure cooldown");
        cold.reset();cold.beginStartup(10000);cold.request(20001);cold.take(20011);cold.finish(20011,true);cold.request(20012);
        expect(cold.timeout(20012)==4999,"Expired startup allowance restores the ordinary 5s gap");
    }

    {
        steamui::ModeReadWindow debt;debt.beginStartup(10000);debt.request(10000);debt.take(10600);debt.finish(10600,false);
        debt.cancel();debt.request(10601);
        expect(debt.timeout(10601)>=4999,"Unknown slow retry debt survives demand cancellation and reopen");
        debt.reset();debt.beginStartup(10000);debt.request(10000);debt.take(10600);debt.finish(10600,false);
        debt.reset(true);debt.request(10601);
        expect(debt.timeout(10601)>=4999,"Unknown slow retry debt survives same-process account metadata update");
    }

    {
        steamui::ModeReadWindow budget(10,5000,30000);ULONGLONG now=10000;
        budget.beginStartup(now);
        for(unsigned i=0;i<3;++i){
            budget.request(now,true);now+=budget.timeout(now);expect(budget.take(now),"Preserved retry budget admits its scheduled read");budget.finish(now,false);
            if(i<2){if(i==0)budget.cancel(true);else budget.reset(true);budget.request(now+1,true);expect(budget.timeout(now+1)>=4999,"Pause/account update cannot shorten unknown retry spacing");}
        }
        expect(budget.timeout(now)==INFINITE,"Unknown retries still park after three reads across demand/account changes");
        budget.reset(true);budget.request(now+1,true);expect(budget.timeout(now+1)>=29999,"Account update cannot discard exhausted failure cooldown");
    }

    // Cold start: a real confirmed mode invalidated by a startup window hint
    // must get a finite fresh read without waiting for the healthy 5s cadence.
    {
        steamui::ModeObserver startup;std::atomic<unsigned> startupReads{0},startupNotifies{0};
        std::atomic<bool> firstInFlight{false},completeFirst{false};
        steamui::ModeObserverConfig cold;cold.debounceMs=15;
        cold.read=[&](const auto& s){if(++startupReads==1){firstInFlight=true;while(!completeFirst)Sleep(1);}return sample(s,"bigpicture");};
        cold.changed=[&](const auto&,const auto&){++startupNotifies;};
        startup.initialize(cold);startup.setDemand(steamui::ModeConsumer::Decky,true);startup.observeSteam(session);
        const bool coldEntered=until([&]{return firstInFlight.load();});
        for(unsigned i=0;i<10000;++i)startup.requestRefresh(session);
        const auto freshAt=GetTickCount64();completeFirst=true;
        const bool coldReady=until([&]{return startupNotifies==1;},1400);
        const auto coldElapsed=GetTickCount64()-freshAt;
        startup.shutdown();
        expect(coldEntered&&coldReady&&coldElapsed<1400,"Cold startup hint rechecks a verified result before the healthy 5s interval");
        expect(startupReads==2&&startupNotifies==1,"10000 startup hints share two bounded reads and one delivery");
    }

    // A local try-lock miss performs no Steam IO and must not impose the
    // genuine read-failure delay during the finite initial catch-up window.
    {
        steamui::ModeObserver contention;std::atomic<unsigned> reads{0},notifications{0};
        steamui::ModeObserverConfig config;config.debounceMs=15;
        config.read=[&](const auto& s){auto value=sample(s,"bigpicture");if(++reads==1){value["mode"]="unknown";value["reason"]="live-busy";}return value;};
        config.changed=[&](const auto&,const auto&){++notifications;};
        const auto at=GetTickCount64();contention.initialize(config);contention.setDemand(steamui::ModeConsumer::Decky,true);contention.observeSteam(session);
        const bool ready=until([&]{return notifications==1;},1400);contention.shutdown();
        expect(ready&&GetTickCount64()-at<1400&&reads==2,"Cold local live-busy contention retries within startup credit without accepting unknown mode");
    }
    {
        steamui::ModeObserver contention;std::atomic<unsigned> reads{0},notifications{0};
        steamui::ModeObserverConfig config;config.debounceMs=15;
        config.read=[&](const auto& s){++reads;auto value=sample(s,"unknown");value["reason"]="live-busy";return value;};
        config.changed=[&](const auto&,const auto&){++notifications;};
        contention.initialize(config);contention.setDemand(steamui::ModeConsumer::Decky,true);contention.observeSteam(session);
        const bool six=until([&]{return reads==6;},3300);Sleep(550);contention.shutdown();
        expect(six&&reads==6&&notifications==0,"Persistent startup live-busy exhausts finite short slots then returns to slow retry without a mode notification");
    }

    {
        steamui::ModeReadWindow local(10,5000,30000);ULONGLONG now=10000;
        local.beginStartup(now);local.request(now);
        for(unsigned i=0;i<6;++i){now+=local.timeout(now);expect(local.take(now),"Busy catch-up consumes only a finite initial read slot");local.finish(now,false,true,true);}
        expect(local.timeout(now)==5000,"Exhausted busy catch-up resumes five-second spacing");
        now+=local.timeout(now);local.take(now);local.finish(now,false,true,true);
        now+=local.timeout(now);local.take(now);local.finish(now,false,true,true);
        expect(local.timeout(now)==INFINITE,"Persistent busy still parks after three ordinary slow attempts");
        local.request(now+1,true);expect(local.timeout(now+1)>=29999,"Persistent busy cannot bypass the ordinary failure cooldown");
    }

    {
        steamui::ModeObserver contention;std::atomic<unsigned> reads{0},notifications{0};
        steamui::ModeObserverConfig config;config.debounceMs=15;
        config.read=[&](const auto& s){auto value=sample(s,"bigpicture");if(++reads<=3){value["mode"]="unknown";value["reason"]="live-busy";}return value;};
        config.changed=[&](const auto&,const auto&){++notifications;};
        const auto at=GetTickCount64();contention.initialize(config);contention.setDemand(steamui::ModeConsumer::Decky,true);contention.observeSteam(session);
        const bool ready=until([&]{return notifications==1;},2400);contention.shutdown();
        expect(ready&&GetTickCount64()-at<2400&&reads==4,"Three initial local lock misses recover without entering thirty-second failure cooldown");
    }

    std::cout<<"SHARED_STEAM_MODE_TEST_OK passed="<<passed<<" realSteamOperations=0\n";return 0;

}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

