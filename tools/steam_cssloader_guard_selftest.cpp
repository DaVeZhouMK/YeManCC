#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "steam_cssloader_guard.h"
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ymcc::steamcss;
static unsigned tests = 0;
static void expect(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
    ++tests; std::cout << "PASS " << label << '\n';
}
template<class F> static bool until(F predicate, DWORD timeout = 2500) {
    const auto end = GetTickCount64() + timeout;
    while (!predicate()) { if (GetTickCount64() >= end) return false; Sleep(5); }
    return true;
}
static ULONGLONG cpu() {
    FILETIME b{}, e{}, k{}, u{}; GetProcessTimes(GetCurrentProcess(), &b, &e, &k, &u);
    return ((static_cast<ULONGLONG>(k.dwHighDateTime) << 32) | k.dwLowDateTime) + ((static_cast<ULONGLONG>(u.dwHighDateTime) << 32) | u.dwLowDateTime);
}
static void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc); stream << text; stream.close();
    if (!stream) throw std::runtime_error("fixture write");
}
static HANDLE duplicate(HANDLE handle) {
    HANDLE result = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &result, SYNCHRONIZE, FALSE, 0)) throw std::runtime_error("fixture duplicate");
    return result;
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) throw std::runtime_error("isolated output path required");
        const auto root = std::filesystem::path(argv[1]) / (L"profile 中文 with spaces-" + std::to_wstring(GetCurrentProcessId()));
        std::filesystem::create_directories(root);
        const auto link = root / L"PowerControl" / L"YeManSteam" / L"CssLoader-Standalone-Headless.txt";
        const std::string executable = "C:\\SOFT\\CSSLoader Desktop\\CssLoader-Standalone-Headless.exe";
        ModeWindow modeWindow;
        expect(modeWindow.timeout(0)==INFINITE,"mode observation has no timer before a real event");
        modeWindow.request(100);
        for(unsigned i=0;i<1000;++i)modeWindow.request(101+i);
        expect(modeWindow.timeout(699)==1&&modeWindow.take(700),"mode event storm coalesces without postponing the first read");
        expect(!modeWindow.finish(701,UiMode::Desktop),"desktop observation cannot trigger CSS");
        modeWindow.request(800);
        expect(modeWindow.timeout(5699)==1&&modeWindow.take(5700),"mode reads preserve a five-second gap despite new events");
        expect(modeWindow.finish(5701,UiMode::BigPicture),"desktop to bigpicture is one mode edge in the same process");
        modeWindow.request(6000);expect(modeWindow.take(10700),"later event can be confirmed once");
        expect(!modeWindow.finish(10701,UiMode::BigPicture),"same bigpicture mode never grants another CSS start");
        modeWindow.request(11000);expect(modeWindow.take(15700),"failed mode read consumes an event budget");
        expect(!modeWindow.finish(15701,UiMode::Unknown),"unknown cannot manufacture a mode edge");
        expect(modeWindow.take(20701),"one finite unknown retry");modeWindow.finish(20702,UiMode::Unknown);
        expect(modeWindow.take(25702),"last finite unknown retry");modeWindow.finish(25703,UiMode::Unknown);
        expect(modeWindow.timeout(999999)==INFINITE,"failed mode window exhausts to event-only wait");
        modeWindow.request(25800);expect(modeWindow.timeout(55702)==1,"failure recovery needs a new event and thirty-second cooldown");
        expect(modeWindow.take(55703)&&!modeWindow.finish(55704,UiMode::BigPicture),"unknown reads preserve last mode instead of duplicating CSS");
        modeWindow.reset();modeWindow.manualEntry(100);expect(modeWindow.take(700),"manual prewarm still schedules real mode confirmation");
        expect(!modeWindow.finish(701,UiMode::BigPicture),"confirming a manual prewarm does not start CSS twice");
        modeWindow.reset();modeWindow.request(0);modeWindow.take(600);
        expect(modeWindow.finish(601,{UiMode::BigPicture,"renderer:1"}),"first renderer entry establishes a mode epoch");
        modeWindow.request(700);modeWindow.take(5600);
        expect(modeWindow.finish(5601,{UiMode::BigPicture,"renderer:2"}),"callback epoch detects quick leave and re-entry even when both reads see bigpicture");
        modeWindow.request(5700);modeWindow.take(10600);
        expect(!modeWindow.finish(10601,{UiMode::BigPicture,"renderer:2"}),"repeated epoch never duplicates an appearance cycle");
        modeWindow.reset();modeWindow.manualEntry(100);modeWindow.take(700);modeWindow.finish(701,UiMode::Unknown);
        modeWindow.take(5701);modeWindow.finish(5702,UiMode::Unknown);modeWindow.take(10702);modeWindow.finish(10703,UiMode::Unknown);
        modeWindow.request(41000);modeWindow.take(41600);
        expect(modeWindow.finish(41601,{UiMode::BigPicture,"late:1"}),"a late real mode entry after the prewarm expires cannot be suppressed forever");
        expect(modeWindow.finish(42000,{UiMode::BigPicture,"new-document:1"}),"new CEF document requires one CSS reapply without granting Steam or Decky startup");
        expect(modeWindow.finish(42001,{UiMode::BigPicture,"new-document:2"}),"same-document higher entry counter retains fast leave/re-entry detection");
        for(const auto& invalid: {"new-document:2", "new-document:1", "new-document:02", "other-document:99", "new-document:-1", "new-document:2x", "new-document:18446744073709551616", "new-document:", "new-document:3:4"})
            expect(!ymcc::steamui::laterModeEntry("new-document:2",invalid),"invalid/reset/foreign epoch cannot authorize CSS");
        expect(ymcc::steamui::laterModeEntry("new-document:2","new-document:3"),"canonical monotonic counter authorizes one real same-document entry");
        expect(!modeWindow.finish(42002,{UiMode::BigPicture,"new-document:1"}),"regressing same-document counter does not manufacture an entry");
        expect(!modeWindow.finish(42003,{UiMode::BigPicture,"new-document:2"}),"regression cannot lower the baseline and replay the previous entry");
        expect(!ymcc::steamui::updateModeEpoch("page:2","page:1"),"same-document epoch baseline is monotonic");
        expect(!ymcc::steamui::updateModeEpoch("page:2","page:02"),"malformed epoch never replaces baseline");
        Budget budget;
        expect(budget.timeout(0) == INFINITE, "idle budget has no recurring deadline");
        budget.begin(100);
        for (const ULONGLONG due : {10100ULL, 40100ULL, 75100ULL}) {
            expect(budget.timeout(due - 1) == 1 && !budget.take(due - 1), "startup check cannot run before grace deadline");
            expect(budget.take(due), "one bounded startup check at 10/40/75 seconds");
        }
        expect(budget.used() == 3 && budget.timeout(90000) == INFINITE && !budget.take(90000), "failed startup budget exhausts rather than becoming polling");
        budget.resume(100000); expect(budget.timeout(100000) == INFINITE, "backend crash cannot replenish exhausted session budget");
        budget.begin(100); expect(budget.take(10100), "new explicit session replenishes bounded budget");
        budget.pause(); expect(budget.timeout(999999) == INFINITE, "healthy process waits indefinitely for exit event");
        budget.resume(50000); expect(budget.timeout(50000) == 30000, "crash waits before using only remaining retries");
        Budget delayed; delayed.begin(0); expect(delayed.take(100000) && delayed.used() == 1 && delayed.timeout(100000) == 30000, "standby late deadline is one check, never a catch-up burst");
        expect(!readLink(link.wstring()).enabled, "missing checkbox link means disabled, not implicit enable");
        write(link, "\xef\xbb\xbf  " + executable + "\r\n");
        expect(readLink(link.wstring()).enabled && readLink(link.wstring()).executable == L"C:\\SOFT\\CSSLoader Desktop\\CssLoader-Standalone-Headless.exe", "UTF-8 BOM, spaces and CRLF preserve linked executable path");
        for (const auto& malformed : {std::string{}, std::string("relative/CssLoader-Standalone-Headless.exe"), executable + "\nextra", std::string("C:\\SOFT\\other.exe"), std::string("C:\\SOFT\\CSSLoader Desktop\\CssLoader-Standalone-Headless.exe --arg"), std::string(33000, 'a'), std::string("\xff\xff")}) {
            write(link, malformed); expect(!readLink(link.wstring()).enabled, "malformed/oversized/wrong executable link cannot authorize launch");
        }
        write(link, executable + std::string(1, '\0')); expect(!readLink(link.wstring()).enabled, "embedded NUL cannot change executable interpretation");
        write(link, executable);
        Runtime runtime; std::atomic<unsigned> calls{0}; std::atomic<int> mode{0};
        ymcc::deckylaunch::Handle fixtureBackend; fixtureBackend.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::atomic<unsigned> lifecycleEvents{0};
        Config config{link.wstring(), [&](const Link&, const ymcc::steamsession::Session&, const std::function<bool()>& allowed) {
            ++calls; if (!allowed()) return Result{nullptr, 0, "canceled", "fixture-canceled"};
            if (mode == 2) return Result{nullptr, 0, "missing", {}, false};
            if (mode == 0) return Result{duplicate(fixtureBackend.value), 321, "running", {}, false};
            return Result{nullptr, 0, "blocked", "fixture-busy"};
        }, {120, 150, 170}};
        config.changed = [&](const State&) { ++lifecycleEvents; runtime.snapshot(); }; // Callback must run outside state lock.
        expect(runtime.initialize(config), "event-based CSS guard starts in isolated profile");
        expect(until([&] { return runtime.snapshot().phase == "waiting-steam"; }), "enabled link without Steam never schedules launch");
        Sleep(180); expect(calls == 0, "no Steam means no process enumeration/check callback");
        ymcc::steamsession::Session steam{L"c:\\isolated-steam", 111, 42, 100};
        runtime.observeSteam(steam);
        expect(until([&] { return runtime.snapshot().phase == "grace"; }), "shared session event schedules startup grace");
        Sleep(35); expect(calls == 0, "existing BAT has first chance before CSS guard check");
        expect(until([&] { return runtime.snapshot().phase == "running"; }), "already running backend is adopted, no duplicate launch");
        expect(calls == 1 && !runtime.snapshot().started, "existing backend adoption does not report a launch");
        expect(lifecycleEvents >= 2, "backend lifecycle publishes finite UI notifications without locking callbacks");
        const auto eventsBeforeIdle = lifecycleEvents.load();
        const auto idle = cpu(); Sleep(600); const auto idleUsed = cpu() - idle;
        std::cout << "HEALTHY_IDLE_CPU_MS " << idleUsed / 10000.0 << " over 600ms\n";
        expect(calls == 1 && lifecycleEvents == eventsBeforeIdle && idleUsed < 1000000, "healthy guard has no timer/check work and negligible idle CPU");
        for (unsigned i = 0; i < 30; ++i) write(link.parent_path() / L"unrelated.txt", std::to_string(i));
        Sleep(150); expect(calls == 1, "unrelated directory writes do not replenish/recheck backend");
        steam.account = 99; runtime.observeSteam(steam); Sleep(150);
        expect(calls == 1, "login/account changes do not reset process-start budget");
        mode = 1; SetEvent(fixtureBackend.value);
        expect(until([&] { return runtime.snapshot().reason == "loader-exit-not-abnormal"; }), "unknown nonprocess wait handle signals exit without a polling timer or crash permission");
        expect(until([&] { return runtime.snapshot().phase == "waiting-appearance"; }), "backend exit parks until a manual appearance use, never recovery");
        expect(calls == 1 && runtime.snapshot().attempts == 1, "backend exit does not even use the remaining recovery budget");
        const auto exhausted = cpu(); Sleep(450); const auto exhaustedUsed = cpu() - exhausted;
        std::cout << "EXHAUSTED_IDLE_CPU_MS " << exhaustedUsed / 10000.0 << " over 450ms\n";
        expect(calls == 1 && exhaustedUsed < 1000000, "exhausted guard returns to event-only idle");
        steam.created = 101; runtime.observeSteam(steam);
        expect(until([&] { return runtime.snapshot().phase == "grace" && runtime.snapshot().attempts == 0; }), "PID reuse with a new birth time opens a new finite session budget");
        runtime.observeSteam({});
        expect(until([&] { return runtime.snapshot().phase == "waiting-steam"; }), "Steam exit cancels deferred recovery");
        Sleep(500); expect(calls == 1, "Steam-exit cancellation prevents even a late startup callback");
        runtime.observeSteam(steam);
        expect(until([&] { return runtime.snapshot().phase == "grace"; }), "Steam restart arms deferred CSS check");
        std::filesystem::remove(link);
        expect(until([&] { return runtime.snapshot().phase == "disabled"; }), "checkbox file deletion cancels pending launch through directory event");
        Sleep(500); expect(calls == 1, "disabled checkbox cannot be secretly re-enabled by watchdog");
        ResetEvent(fixtureBackend.value); mode = 0; write(link, executable);
        expect(until([&] { return runtime.snapshot().phase == "running"; }), "live Steam plus checkbox enable starts a new grace window");
        expect(calls == 2, "explicit enable grants one fresh missing-only check");
        runtime.observeSteam({}); expect(until([&] { return runtime.snapshot().phase == "waiting-steam"; }), "Steam stop releases backend lease");
        expect(WaitForSingleObject(fixtureBackend.value, 0) == WAIT_TIMEOUT, "Steam stop never terminates/signals an already running CSS process");
        runtime.shutdown(); const auto stoppedCalls = calls.load(); write(link, executable);
        Sleep(180); expect(calls == stoppedCalls, "shutdown closes directory/process subscriptions and prevents callbacks");
        runtime.shutdown(); expect(true, "shutdown is idempotent");
        // A missing YeManSteam directory is recovered from its existing ancestor.
        const auto late = root / L"late PowerControl" / L"YeManSteam" / L"CssLoader-Standalone-Headless.txt";
        std::filesystem::create_directories(late.parent_path().parent_path());
        Config lateConfig = config; lateConfig.linkFile = late.wstring();
        expect(runtime.initialize(lateConfig), "guard can restart with a different profile"); runtime.observeSteam(steam);
        expect(until([&] { return runtime.snapshot().phase == "disabled"; }), "missing addon directory remains disabled without recurring scan");
        write(late, executable); expect(until([&] { return runtime.snapshot().phase == "running"; }), "late addon directory creation heals via file-system event");
        std::filesystem::remove(late);
        expect(until([&] { return runtime.snapshot().phase == "disabled"; }), "deleting live checkbox releases healthy backend wait lease");
        std::filesystem::remove(late.parent_path());
        Sleep(60);
        write(late, executable);
        expect(until([&] { return runtime.snapshot().phase == "running"; }), "deletion and recreation of addon directory repairs its event subscription");
        runtime.shutdown();
        // Launch completion racing Steam exit must not publish a stale healthy state.
        ymcc::deckylaunch::Handle entered, release;
        entered.value = CreateEventW(nullptr, TRUE, FALSE, nullptr); release.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        Config race = config;
        race.ensure = [&](const Link&, const ymcc::steamsession::Session&, const std::function<bool()>& allowed) {
            SetEvent(entered.value); WaitForSingleObject(release.value, 2000);
            return Result{duplicate(fixtureBackend.value), 222, "running", {}, allowed()};
        };
        runtime.initialize(race); runtime.observeSteam(steam);
        expect(WaitForSingleObject(entered.value, 2500) == WAIT_OBJECT_0, "isolated in-flight completion race reached backend check");
        runtime.observeSteam({}); SetEvent(release.value);
        expect(until([&] { return runtime.snapshot().phase == "waiting-steam"; }), "late completion after Steam exit cannot publish healthy/stale process");
        expect(WaitForSingleObject(fixtureBackend.value, 0) == WAIT_TIMEOUT, "canceled in-flight operation only closes wait lease, does not terminate backend");
        runtime.shutdown();
        Config fenced = config; fenced.mayLaunch = [] { return false; };
        runtime.initialize(fenced); runtime.observeSteam(steam);
        expect(until([&] { return runtime.snapshot().phase == "canceled"; }), "application exit fence prevents a late backend launch before shutdown joins");
        expect(runtime.snapshot().pid == 0 && !runtime.snapshot().started, "application exit fence cannot adopt/publish an in-flight backend as healthy");
        runtime.shutdown();
        DWORD handlesBefore = 0, handlesAfter = 0;
        GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
        for (unsigned i = 0; i < 5; ++i) { runtime.initialize(config); runtime.observeSteam(steam); Sleep(10); runtime.shutdown(); }
        GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
        expect(handlesAfter <= handlesBefore + 2, "repeated startup/shutdown does not leak event/directory/process handles");
        std::atomic<bool> parked{true}; std::atomic<unsigned> idleHelpers{0};
        Config idleConfig = config; mode=2; ResetEvent(fixtureBackend.value);calls=0;
        idleConfig.isIdle=[&](const auto&){return parked.load();};
        ymcc::deckylaunch::Handle helper;helper.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        idleConfig.confirmIdle=[&](const auto&,bool,bool){++idleHelpers;return duplicate(helper.value);};
        runtime.initialize(idleConfig);runtime.observeSteam(steam);
        expect(until([&]{return runtime.snapshot().phase=="idle";}),"intentional idle receipt skips startup recovery");
        runtime.requestManualRecovery();Sleep(250);
        expect(calls==2&&idleHelpers==1,"explicit manual bigpicture request bypasses a stale idle receipt once");
        runtime.shutdown();parked=false;mode=0;calls=0;idleHelpers=0;
        runtime.initialize(idleConfig);runtime.observeSteam(steam);
        expect(until([&]{return idleHelpers==1&&runtime.snapshot().phase=="applying";}),"healthy startup issues one bounded idle-confirmation helper");
        parked=true;SetEvent(fixtureBackend.value);SetEvent(helper.value);
        expect(until([&]{return runtime.snapshot().phase=="idle";}),"intentional backend exit is idle rather than crash recovery");
        const auto parkedCpu=cpu();Sleep(600);
        std::cout<<"PARKED_IDLE_CPU_MS "<<(cpu()-parkedCpu)/10000.0<<" over 600ms\n";
        expect(calls==1&&idleHelpers==1,"parked CSS has no extra readiness helper or restart attempt");
        runtime.shutdown();parked=false;calls=0;idleHelpers=0;ResetEvent(fixtureBackend.value);ResetEvent(helper.value);
        runtime.initialize(idleConfig);runtime.observeSteam(steam);
        expect(until([&]{return idleHelpers==1;}),"new session can confirm startup once");
        SetEvent(helper.value);Sleep(650);
        expect(calls==1&&idleHelpers==1&&runtime.snapshot().phase=="waiting-appearance","failed idle confirmation leaves applying state without a retry loop");
        runtime.shutdown();
        mode=2;calls=0;runtime.initialize(config);runtime.observeSteam(steam);
        expect(until([&]{return runtime.snapshot().phase=="exhausted";}),"missing startup loader stops after three finite observations");
        Sleep(500);expect(calls==3&&!runtime.snapshot().started,"missing loader remains absent; no auto-start/restart permission");runtime.shutdown();
        ResetEvent(helper.value);mode=2;calls=0;idleHelpers=0;parked=false;
        unsigned startupRequests=0;
        auto startupConfig=idleConfig;
        startupConfig.confirmIdle=[&](const auto&,bool start,bool){++idleHelpers;if(start)++startupRequests;return duplicate(helper.value);};
        runtime.initialize(startupConfig);runtime.observeSteam(steam);
        expect(until([&]{return idleHelpers==1;}),"external Steam session requests missing CSS exactly once");
        expect(startupRequests==1&&calls==1,"missing CSS uses the startup helper instead of observation-only exhaustion");
        SetEvent(helper.value);Sleep(600);
        expect(calls==1&&idleHelpers==1,"helper failure never becomes repeated CSS launch or discovery");runtime.shutdown();
        ResetEvent(helper.value);calls=0;idleHelpers=0;startupRequests=0;mode=0;
        runtime.initialize(startupConfig);runtime.observeSteam(steam);
        expect(until([&]{return idleHelpers==1;}),"BAT-provided CSS uses the same bounded hold path");
        expect(startupRequests==0,"existing CSS is adopted without a second startup helper");runtime.shutdown();
        ResetEvent(helper.value);calls=0;idleHelpers=0;startupRequests=0;
        auto lateStartup=startupConfig;
        lateStartup.ensure=[&](const auto&,const auto&,const auto&){
            ++calls;return calls<3?Result{nullptr,0,"blocked","fixture-starting"}:Result{nullptr,0,"missing",{}};
        };
        runtime.initialize(lateStartup);runtime.observeSteam(steam);
        expect(until([&]{return idleHelpers==1;}),"a last-budget probe that finally verifies missing CSS still starts once");
        expect(calls==3&&startupRequests==1,"transient blocked startup remains finite and does not lose its final eligible start");runtime.shutdown();
        // Real worker/event waits, mocked mode/backend only. One PID stays alive
        // while the user moves between Steam desktop and Big Picture.
        std::atomic<unsigned> modeReads{0},modeUses{0},modeStarts{0},renewedModes{0};
        std::atomic<UiMode> observedMode{UiMode::Desktop};
        ResetEvent(helper.value);calls=0;parked=true;
        auto modeConfig=startupConfig;modeConfig.delays={5000,5000,5000};modeConfig.modeDelays={15,80,120};
        modeConfig.ensure=[&](const auto&,const auto&,const auto&){++calls;return Result{nullptr,0,"missing",{}};};
        modeConfig.readMode=[&](const auto&){++modeReads;return observedMode.load();};
        modeConfig.confirmIdle=[&](const auto&,bool start,bool renew){++modeUses;if(start)++modeStarts;if(renew)++renewedModes;return duplicate(helper.value);};
        runtime.initialize(modeConfig);runtime.observeSteam(steam);
        expect(until([&]{return modeReads==1;}),"startup establishes a desktop baseline from the shared session");
        expect(modeUses==0,"desktop window events do not start a CSS mode cycle");
        observedMode=UiMode::BigPicture;runtime.requestUiRefresh();
        expect(until([&]{return modeUses==1;}),"external Steam desktop-to-bigpicture event starts appearance without YMCC UI");
        expect(modeStarts==1&&renewedModes==1,"new bigpicture entry bypasses the previous same-PID idle receipt");
        for(unsigned i=0;i<1000;++i)runtime.requestUiRefresh();
        Sleep(220);expect(modeUses==1&&modeReads<=3,"1000 repeated notifications cause bounded reads and no duplicate CSS use");
        SetEvent(helper.value);expect(until([&]{return runtime.snapshot().phase=="idle";}),"mode application parks with a confirmed receipt");
        const auto modeIdleCpu=cpu();const auto readsBeforeIdle=modeReads.load();Sleep(300);
        expect(modeReads==readsBeforeIdle&&cpu()-modeIdleCpu<1000000,"parked mode observer performs no recurring reads or CPU loop");
        observedMode=UiMode::Desktop;runtime.requestUiRefresh();const auto desktopReads=modeReads.load();
        expect(until([&]{return modeReads>desktopReads;}),"desktop return is remembered without changing Steam PID");
        ResetEvent(helper.value);observedMode=UiMode::BigPicture;runtime.requestUiRefresh();
        expect(until([&]{return modeUses==2;}),"second real entry receives its own CSS cycle despite old receipt and used startup budget");
        SetEvent(helper.value);expect(until([&]{return runtime.snapshot().phase=="idle";}),"second cycle also releases its wait handles");
        observedMode=UiMode::Desktop;const auto beforeDesktop=modeReads.load();runtime.requestUiRefresh();
        expect(until([&]{return modeReads>beforeDesktop;}),"manual prewarm fixture observes desktop first");
        ResetEvent(helper.value);runtime.requestManualRecovery();
        expect(until([&]{return modeUses==3;},300),"YMCC manual switch prewarms CSS immediately instead of waiting the ten-second startup grace");
        observedMode=UiMode::BigPicture;runtime.requestUiRefresh();Sleep(220);
        expect(modeUses==3,"observed manual switch completion does not launch a second CSS group");
        std::filesystem::remove(link);expect(until([&]{return runtime.snapshot().phase=="disabled";}),"disabled appearance cancels all pending mode reads");
        const auto disabledReads=modeReads.load();for(unsigned i=0;i<100;++i)runtime.requestUiRefresh();Sleep(220);
        expect(modeReads==disabledReads&&modeUses==3,"mode notifications cannot re-enable disabled appearance");runtime.shutdown();
        write(link,executable);
        // A mode response arriving after Steam exit is never permission to start.
        std::atomic<bool> modeEntered{false},modeRelease{false};
        auto staleMode=modeConfig;staleMode.readMode=[&](const auto&){modeEntered=true;while(!modeRelease)Sleep(1);return UiMode::BigPicture;};
        runtime.initialize(staleMode);runtime.observeSteam(steam);
        expect(until([&]{return modeEntered.load();}),"stale mode fixture reaches one in-flight read");
        runtime.observeSteam({});modeRelease=true;
        expect(until([&]{return !runtime.snapshot().steamRunning;}),"Steam exit cancels an in-flight bigpicture response");
        expect(modeUses==3,"stale response cannot start CSS for an exited Steam");runtime.shutdown();
        // Cached links must still be revalidated before a mode edge starts CSS.
        modeEntered=false;modeRelease=false;
        runtime.initialize(staleMode);runtime.observeSteam(steam);
        expect(until([&]{return modeEntered.load();}),"disable race reaches an in-flight mode read");
        std::filesystem::remove(link);modeRelease=true;
        expect(until([&]{return runtime.snapshot().phase=="disabled";}),"link deletion wins over a returning bigpicture mode edge");
        expect(modeUses==3,"cached link cannot authorize CSS after appearance is disabled");runtime.shutdown();
        ymcc::steamsession::Session badSteam{L"c:\\not-a-steam-install", GetCurrentProcessId(), 0, 1};
        expect(steamLease(badSteam) == nullptr, "unrelated/PID-reused executable cannot authorize production CSS launch");
        auto observed = probeBackend();
        std::cout << "READ_ONLY_REAL_BACKEND phase=" << observed.phase << " reason=" << observed.reason << " pid=" << observed.pid << '\n';
        if (observed.process) CloseHandle(observed.process);
        expect(true, "production backend process/port probe is read-only; no real Steam/CSS operations");
        std::cout << "STEAM_CSS_GUARD_OK tests=" << tests << " actualSteamOperations=0 actualLoaderLaunches=0\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAILED " << error.what() << '\n'; return 1; }
}
