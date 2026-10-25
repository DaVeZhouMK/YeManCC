// Historical in-process callback regression; not the owned binding production acceptance.
#define YMCC_DECKY_RUNTIME_LEGACY_FIXTURE 1
// Decky startup regression: inert admission gates and rejected preflight only.
// Never loads a third-party executable, reads real Steam, or writes user settings.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "decky_sidebar_runtime.h"
#include "decky_sidebar_diagnostics.h"
#include "steam_ui_read_budget.h"
#include <atomic>
#include <filesystem>
#include <iostream>
static unsigned passed=0,failed=0;
static void check(bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(ok)++passed;else ++failed;}
template<class F> bool until(F f,unsigned ms=2500){const auto end=GetTickCount64()+ms;while(!f()){if(GetTickCount64()>=end)return false;Sleep(5);}return true;}
using ymcc::deckysidebar::Runtime;
using ymcc::deckysidebar::Config;
using ymcc::deckysidebar::LaunchAdmission;
using ymcc::deckysidebar::AdmissionWindow;
int main(int argc,char** argv){
    if(argc!=2)return 2;
    const auto dir=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(dir);
    std::atomic<unsigned> preflight{0},binding{0},gateCalls{0};std::atomic<bool> ready{false};
    Config config;config.home=dir.wstring();config.executable=(dir/L"NeverLaunch.exe").wstring();
    config.preflight=[&]{++preflight;return std::string("inert-preflight-stop");};
    config.prepareBinding=[&]{++binding;return std::string{};};
    config.launchAdmission=[&]{++gateCalls;const bool allowed=ready.load();return LaunchAdmission{allowed,allowed?"":"steam-ui-state-pending",!allowed,"live-busy"};};
    Runtime runtime;runtime.initialize(config);runtime.observeSteam(1,true);runtime.setEnabled(true);
    check(until([&]{return runtime.snapshot().phase=="waiting-manual-steam";}),"Startup waits for a genuine mode confirmation without loading anything");
    const auto initial=runtime.snapshot();
    check(initial.admissionRetry&&initial.admissionAttempts==1&&initial.admissionDetail=="live-busy","Pending state carries its true read reason and scheduled admission recovery");
    check(preflight==0&&binding==0&&initial.pid==0,"Unknown early UI never reaches preflight, injection or loader launch");
    ready=true;
    check(until([&]{return preflight==1;}),"Late confirmed bigpicture is rechecked within the finite window even when no new UI event arrives");
    check(until([&]{return runtime.snapshot().phase=="unavailable";}),"Recovered admission publishes the actual preflight result");
    check(binding==0&&runtime.snapshot().pid==0,"The recovered admission still respects original preflight failure");
    const auto resumed=runtime.snapshot();check(!resumed.admissionRetry&&resumed.admissionAttempts==0&&resumed.admissionDetail.empty(),"A completed admission clears obsolete checking presentation");
    runtime.shutdown();const auto disabled=gateCalls.load();Sleep(1100);
    check(gateCalls==disabled,"Disabling leaves no running admission poll");

    preflight=0;gateCalls=0;
    Runtime manual;auto manualConfig=config;
    manualConfig.launchAdmission=[&]{++gateCalls;return LaunchAdmission{false,"ymcc-restart-observation-only",false};};
    manual.initialize(manualConfig);manual.observeSteam(1,true);manual.setEnabled(true);
    check(until([&]{return manual.snapshot().phase=="waiting-manual-steam";}),"Saved preference retains observation-only startup policy");
    auto calls=gateCalls.load();Sleep(1200);
    check(gateCalls==calls&&preflight==0&&!manual.snapshot().admissionRetry,"Manual session admission is never bypassed or polled");manual.shutdown();

    ready=false;gateCalls=0;
    Runtime events;events.initialize(config);events.observeSteam(1,true);events.setEnabled(true);
    check(until([&]{return events.snapshot().admissionRetry;}),"A fresh session opens its own finite confirmation window");
    for(unsigned i=0;i<4;++i){Sleep(100);events.requestLaunchAdmissionRefresh();}
    check(until([&]{return events.snapshot().admissionAttempts>=2;},1500),"Window-event wakes do not postpone or replenish the original automatic check budget");
    check(preflight==0&&binding==0,"Repeated UI events cannot bypass an unconfirmed mode");
    events.observeSteam(0,true);
    check(until([&]{return events.snapshot().phase=="waiting-steam";}),"Steam absence cancels pending admission");
    calls=gateCalls.load();Sleep(1100);
    check(gateCalls==calls&&!events.snapshot().admissionRetry,"Absent Steam has no mode-read timer or background poll");events.shutdown();

    const auto base=ULONGLONG{10000};AdmissionWindow window;
    window.blocked(1,base,true);check(window.timeout(base)==1000,"Admission window starts on the original 1-second offset");
    window.blocked(1,base+500,true);check(window.timeout(base+500)==500,"Same-cycle events leave the first deadline unchanged");
    for(const auto offset:{1000ULL,3000ULL,8000ULL,20000ULL}){
        check(window.take(base+offset),"Each original bounded offset admits one recheck");window.blocked(1,base+offset,true);
    }
    check(window.attempts()==5&&window.timeout(base+21000)==29000&&window.recovering(),"After four fast rechecks a slow recovery remains scheduled");
    window.blocked(1,base+22000,true);check(window.timeout(base+22000)==28000,"An unrelated wake cannot postpone slow recovery");
    check(window.take(base+50000)&&window.attempts()==6&&window.timeout(base+50000)==30000,"An unchanged Steam session is rechecked after the old 20-second cutoff");
    check(window.take(base+900000)&&window.attempts()==7&&window.timeout(base+900000)==30000,"A 15-minute scheduling stall causes one read and no catch-up burst");
    check(!window.take(base+900000)&&window.elapsed(base+900000)==900000,"Slow recovery remains capped with accurate elapsed telemetry");
    window.blocked(2,base+23000,true);check(window.timeout(base+23000)==1000,"A genuine new launch intent opens one new bounded window");
    window.stop();window.blocked(2,base+23100,true);check(window.timeout(base+23100)==INFINITE,"Success or source stop clears the timer without allowing an event to refill it");
    window.blocked(3,base+24000,true);check(window.take(base+40000)&&window.attempts()==2&&window.timeout(base+40000)==4000,"Delayed IO skips elapsed offsets without a retry burst");
    window.blocked(3,base+40100,false);check(window.timeout(base+40100)==INFINITE,"Revoked/non-retryable admission immediately cancels remaining checks");

    window.completed();window.blocked(3,base+50000,true);
    check(window.timeout(base+50000)==1000&&window.attempts()==1,"A new outage after completed admission re-arms within the same Steam session");
    window.stop();

    // A failed shared read must be renewed only by Decky outage demand.
    ymcc::steamui::ReadBudget reads;for(unsigned i=0;i<3;++i){check(reads.begin(base+i*5000),"Shared mode read admits its original failure budget");reads.finish(false);}
    check(!reads.begin(base+50000),"Shared mode read really pauses after three failures");
    ymcc::deckysidebar::AdmissionReadRefresh refresh;
    check(!refresh.take(base+40000,false),"Fast admission does not force a shared read");
    check(refresh.take(base+50000,true)&&reads.begin(base+50000,true),"Slow Decky admission renews the original paused read");
    bool throttled=true;for(unsigned i=1;i<30000;i+=100)throttled=(!refresh.take(base+50000+i,true))&&throttled;
    check(throttled,"Window event storm cannot create extra forced reads");
    check(refresh.take(base+80000,true),"A fresh original read is allowed after 30 seconds");
    refresh.reset();check(refresh.take(base+80001,true),"A genuine new Steam session clears obsolete read throttling");

    // Real Runtime timers, no window wake: make UI ready after the old maximum
    // and let the first slow timer recover it. Never launch a third-party child.
    ready=false;preflight=0;gateCalls=0;
    Runtime slow;slow.initialize(config);slow.observeSteam(1,true);slow.setEnabled(true);
    check(until([&]{return slow.snapshot().admissionRecovery;},24000),"Runtime reaches slow recovery after the original startup offsets");
    const auto stalled=slow.snapshot();check(stalled.admissionAttempts==5&&stalled.admissionElapsedMs>=20000&&stalled.admissionNextRetryMs<=30000&&stalled.admissionRetry,"A slow machine has visible wait duration and a scheduled next read");
    ready=true;check(until([&]{return preflight==1;},34000),"UI becoming ready after 20 seconds recovers without a mode event or another toggle");
    check(binding==0&&slow.snapshot().pid==0,"Late recovery still obeys original preflight and never bypasses launch protection");
    slow.shutdown();

    Runtime invalid;auto bad=config;
    bad.launchAdmission=[]()->LaunchAdmission{throw std::runtime_error("inert read failure");};
    invalid.initialize(bad);invalid.observeSteam(1,true);invalid.setEnabled(true);
    check(until([&]{return invalid.snapshot().reason=="steam-ui-state-pending";}),"Thrown mode read is a visible pending state, not an unhandled worker failure");
    check(invalid.snapshot().admissionDetail=="admission-read-failed"&&invalid.snapshot().admissionRetry,"Read exceptions expose a bounded safe diagnostic");invalid.shutdown();

    const auto diagnostic=ymcc::deckydiag::record("runtime",{{"admissionRetry",true},{"admissionAttempts",2},{"admissionDetail","live-busy"},{"admissionElapsedMs",20000},{"admissionNextRetryMs",30000},{"admissionRecovery",true},{"reason","steam-ui-state-pending"},{"bootstrapToken","inert-forbidden"},{"args","inert-forbidden"},{"endpoint","inert-forbidden"}});
    check(diagnostic.value("admissionRetry",false)&&diagnostic.value("admissionAttempts",0)==2&&diagnostic.value("admissionDetail",std::string{})=="live-busy","Native log allowlist preserves actual admission stage and progress");
    check(diagnostic.value("admissionElapsedMs",0)==20000&&diagnostic.value("admissionNextRetryMs",0)==30000&&diagnostic.value("admissionRecovery",false),"Timing diagnostics survive the original redacting allowlist");
    check(!diagnostic.contains("bootstrapToken")&&!diagnostic.contains("args")&&!diagnostic.contains("endpoint"),"Admission diagnostics retain existing credential/request redaction");
    std::cout<<"ADMISSION_RESULT passed="<<passed<<" failed="<<failed<<" realSteam=0 realLoader=0 configWrites=0\n";return failed?1:0;
}
