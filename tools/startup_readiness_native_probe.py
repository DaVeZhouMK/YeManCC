from pathlib import Path
import re,sys
root=Path(__file__).resolve().parent.parent
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
source=(root/'native/main.cpp').read_text(encoding='utf-8-sig')
names=['effectiveRuntimeInputSettings','publishStartupInputState','skipDisabledStartupInputGate','initializeStartupInputGate','recordStartupInputAttempt','observeStartupInputGate']
parts=[]
for name in names:
 m=re.search(r'^static (?:void|json) '+name+r'\([^\n]*\) \{\n.*?^\}\n',source,re.M|re.S)
 if not m:raise RuntimeError('Function not found: '+name)
 parts.append(m.group())
head=r'''
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include "@ROOT@/deps/json/json.hpp"
#include "@ROOT@/native/startup_readiness.h"
#include "@ROOT@/native/game_input_override.h"
#include "@ROOT@/native/input_shortcut_toggle.h"
using json = nlohmann::json;
using ULONGLONG = unsigned long long;
using S = ymcc::StartupInputState;
static ULONGLONG tick = 100;
static ULONGLONG GetTickCount64() { return tick; }
static int g_hwnd = 1, messages = 0;
static constexpr int WM_STARTUP_INPUT_SETTLED = 25;
static void PostMessageW(int, int, int, int) { ++messages; }
static std::vector<json> logs;
static void appendNativeLifecycleLog(const char*, const json& data) { logs.push_back(data); }
static json settings = json::object();
static std::mutex g_inputShortcutRuntimeMx;
static ymcc::InputShortcutRuntime g_inputShortcutRuntime;
// Isolated owner-session boundary only; real override resolver comes from the header.
static const std::string& gameInputOwnerSession() { static const std::string fixture; return fixture; }
static json ymSettingsSection(const char*) { return settings; }
static std::string trim_ascii(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b-a+1);
}
static std::string ascii_lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));}); return s;
}
static std::atomic<S> g_startupInputState{S::Initializing};
static ULONGLONG g_startupInputSince = 0;
static bool g_startupInputAttempted = false, g_startupInputStartSucceeded = false;
static std::atomic<bool> g_inputCaptureStop{false}, g_inputHostFaultHoldsStart{false};
static std::atomic<bool> g_inputHostLifecycleWorkRequested{false}, g_inputHostLifecycleWorkInFlight{false};
static std::atomic<bool> g_ymccSlotHandoffPlanActive{false}, g_inputHostRecoveryPending{false};
static std::atomic<bool> g_inputHostRecoveryTerminalLogged{false};
static std::mutex g_inputCaptureBusTickMx, g_inputHostMx, g_inputHostPersonaMx;
static bool alive = false, g_inputHostHello = false, g_inputHostPrepared = false, g_inputHostTransportFault = false;
static bool inputHostProcessAliveLocked() { return alive; }
static std::string g_inputHostAttemptPersona;
static struct Config { bool enabled = true, virtualTargetEnabled = true; std::string persona = "elite"; } g_realStickTest;
static std::string inputHostConfiguredPersona() { std::lock_guard<std::mutex> lock(g_inputHostPersonaMx); return g_realStickTest.persona; }
namespace ymcc {
class GamepadDriverSetup {
public:
    enum State { NotStarted=0, Pending=1, Ready=2, Failed=3, RebootRequired=4, Slow=5, InstallerMissing=6, InstallerInvalid=7 };
    static constexpr bool HasWarning(State s) { return s!=NotStarted && s!=Ready; }
    State state() const { return state_; }
    bool warnsTargets() const { return HasWarning(state_); }
    void setState(State s) { state_ = s; }
private:
    State state_ = NotStarted;
};
}
static ymcc::GamepadDriverSetup g_gamepadDriverSetup;
'''.replace('@ROOT@',root.as_posix())
foot=r'''
static int checks = 0;
static void check(bool ok, const char* why) { ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); std::exit(1); } }
static void reset() {
    g_startupInputState = S::Initializing; g_startupInputSince = 0;
    g_startupInputAttempted = g_startupInputStartSucceeded = false;
    g_inputCaptureStop = g_inputHostFaultHoldsStart = false;
    g_inputHostLifecycleWorkRequested = g_inputHostLifecycleWorkInFlight = false;
    g_ymccSlotHandoffPlanActive = g_inputHostRecoveryPending = g_inputHostRecoveryTerminalLogged = false;
    alive = g_inputHostHello = g_inputHostPrepared = g_inputHostTransportFault = false;
    g_realStickTest = Config{}; g_inputHostAttemptPersona.clear(); settings = json::object();
    g_inputShortcutRuntime.clear();
    messages = 0; logs.clear(); tick = 100;
}
int main() {
    // Exercise the actual extracted startup observer for every preparation state.
    for (auto state : {ymcc::GamepadDriverSetup::NotStarted, ymcc::GamepadDriverSetup::Pending,
            ymcc::GamepadDriverSetup::Ready, ymcc::GamepadDriverSetup::Failed,
            ymcc::GamepadDriverSetup::RebootRequired, ymcc::GamepadDriverSetup::Slow,
            ymcc::GamepadDriverSetup::InstallerMissing, ymcc::GamepadDriverSetup::InstallerInvalid}) {
        reset(); g_gamepadDriverSetup.setState(state); initializeStartupInputGate();
        recordStartupInputAttempt(true); alive = g_inputHostHello = g_inputHostPrepared = true;
        observeStartupInputGate();
        check(g_startupInputState == S::Ready,"preparation warning cannot override actual ready backend");
        check(g_gamepadDriverSetup.warnsTargets() == (state != ymcc::GamepadDriverSetup::NotStarted && state != ymcc::GamepadDriverSetup::Ready),"preparation state preserved for warning");
    }
    g_gamepadDriverSetup.setState(ymcc::GamepadDriverSetup::NotStarted);
    reset(); skipDisabledStartupInputGate();
    check(g_startupInputState == S::Skipped && messages == 1,"default settings skip before worker delay");
    initializeStartupInputGate(); recordStartupInputAttempt(false); observeStartupInputGate();
    check(g_startupInputState == S::Skipped && !g_inputHostFaultHoldsStart && messages == 1,"disabled terminal never regates or faults later intents");
    for (const char* persona : {"elite","dualshock4","dualsense-edge","steamdeck","xbox360","dualsense"}) {
        reset(); settings = {{"outputTarget",{{"persona",persona},{"buttonMappingEnabled",true}}}};
        skipDisabledStartupInputGate(); check(g_startupInputState == S::Initializing,"all requested personas await authoritative initialization");
    }
    reset(); settings = {{"outputTarget",{{"persona"," ELITE "},{"gyroEnabled",true}}}}; skipDisabledStartupInputGate();
    check(g_startupInputState == S::Initializing,"gyro-only output also waits");
    reset(); settings = {{"outputTarget",{{"persona","disabled"},{"gyroEnabled",true}}}}; skipDisabledStartupInputGate();
    check(g_startupInputState == S::Skipped,"disabled persona ignores retained switches");
    reset(); settings = json::array(); skipDisabledStartupInputGate();
    check(g_startupInputState == S::Initializing,"unreadable config is not interpreted as disabled");
    reset(); initializeStartupInputGate(); tick += 14999; observeStartupInputGate();
    check(g_startupInputState == S::Waiting && messages == 0,"source wait is not early failure");
    tick++; observeStartupInputGate();
    check(g_startupInputState == S::Failed && g_inputHostFaultHoldsStart && g_inputHostAttemptPersona == "elite","missing source freezes exact startup intent before fail-open UI");
    const auto failedLogs = logs.size(); observeStartupInputGate(); publishStartupInputState(S::Ready,"late");
    check(messages == 1 && logs.size() == failedLogs && g_startupInputState == S::Failed,"failure terminal is event-level and monotonic");
    for (auto* busy : {&g_inputHostLifecycleWorkRequested,&g_inputHostLifecycleWorkInFlight,&g_ymccSlotHandoffPlanActive,&g_inputHostRecoveryPending}) {
        reset(); initializeStartupInputGate(); alive=g_inputHostHello=g_inputHostPrepared=true; recordStartupInputAttempt(true);
        *busy=true; tick += 90000; observeStartupInputGate();
        check(g_startupInputState == S::Waiting && messages == 0,"no queued/inflight/handoff/recovery timeout bypass");
        *busy=false; observeStartupInputGate();
        check(g_startupInputState == S::Ready && messages == 1,"success only after real lifecycle queues drain");
    }
    reset(); initializeStartupInputGate(); recordStartupInputAttempt(false); g_inputHostLifecycleWorkRequested=true;
    observeStartupInputGate(); check(g_startupInputState == S::Waiting,"failed attempt waits cleanup queue");
    g_inputHostLifecycleWorkRequested=false; observeStartupInputGate();
    check(g_startupInputState == S::Failed && messages == 1,"failed attempt settles after cleanup");
    reset(); initializeStartupInputGate(); recordStartupInputAttempt(false);
    g_inputHostRecoveryPending=true; g_inputHostRecoveryTerminalLogged=true; observeStartupInputGate();
    check(g_startupInputState == S::Failed,"spent recovery flag cannot keep spinner forever");
    reset(); initializeStartupInputGate(); g_realStickTest.virtualTargetEnabled=false; observeStartupInputGate();
    check(g_startupInputState == S::Skipped,"cancelled virtual intent settles without target");
    reset(); initializeStartupInputGate(); recordStartupInputAttempt(true); tick += 90000;
    observeStartupInputGate(); check(g_startupInputState == S::Failed,"successful start receipt with dead target is not ready");
    reset(); initializeStartupInputGate(); recordStartupInputAttempt(true); g_inputCaptureStop=true; observeStartupInputGate();
    check(g_startupInputState == S::Waiting,"stopping worker cannot signal before its cleanup");
    publishStartupInputState(S::Failed,"worker-stopped");
    check(g_startupInputState == S::Failed,"worker termination explicit failure closes init path");
    std::printf("STARTUP_NATIVE_OBSERVER_PASS checks=%d hardwareOperations=0\n",checks);
}
'''
(out/'startup_native_observer_probe.cpp').write_text(head+'\n'.join(parts)+foot,encoding='utf-8')
print(out/'startup_native_observer_probe.cpp')
