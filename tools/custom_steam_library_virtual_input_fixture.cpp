// Generated runner inserts exact production helpers from native/main.cpp.
// Every Win32 probe is a fixture; this executable opens no real process/window.
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
#include "dsu_frame.h"
#include "controller_persona.h"
using json = nlohmann::json;

struct FixtureWindow {
    bool valid = true, visible = true, iconic = false, present = false;
    DWORD pid = 77;
    ULONG_PTR owner = 2, parentPid = 42;
    std::wstring image = L"G:\\Fixture\\CustomSteamLibrary.exe";
    HWND root = nullptr;
};
static FixtureWindow mainWindow, childWindow, mainPopup, otherWindow;
static HWND mainHandle = reinterpret_cast<HWND>(&mainWindow);
static HWND childHandle = reinterpret_cast<HWND>(&childWindow);
static HWND popupHandle = reinterpret_cast<HWND>(&mainPopup);
static HWND otherHandle = reinterpret_cast<HWND>(&otherWindow);
static HWND g_hwnd = mainHandle, foregroundWindow = mainHandle;
static ULONGLONG fixtureTick = 50000;
static bool processDenied = false, processQueryFailed = false;
static bool g_webviewReady = true, directFocusAllowed = true, altFocusAllowed = true, altInserted = false, altInsertionSucceeds = true;
static unsigned foregroundAttempts = 0, altAttempts = 0, windowRestores = 0;
static unsigned foregroundReads = 0;
struct FixtureWebViewController {
    unsigned moves = 0;
    HRESULT MoveFocus(int) { ++moves; return S_OK; }
};
static FixtureWebViewController fixtureController;
static FixtureWebViewController* g_ctrl = &fixtureController;
static constexpr int COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC = 0;
static FixtureWindow* fixture(HWND window) { return reinterpret_cast<FixtureWindow*>(window); }
static BOOL fixtureIsWindow(HWND window) { return window && fixture(window)->valid; }
static BOOL fixtureIsVisible(HWND window) { return fixtureIsWindow(window) && fixture(window)->visible; }
static BOOL fixtureIsIconic(HWND window) { return fixtureIsWindow(window) && fixture(window)->iconic; }
static HWND fixtureForeground() { ++foregroundReads; return foregroundWindow; }
static HWND fixtureAncestor(HWND window, UINT) { return window ? fixture(window)->root : nullptr; }
static HWND fixtureFindWindow(const wchar_t* name, const wchar_t*) {
    return childWindow.present && std::wcscmp(name, L"YeManSteamLibraryWorkspace") == 0 ? childHandle : nullptr;
}
static HANDLE fixtureGetProp(HWND window, const wchar_t* name) {
    if (!window) return nullptr;
    return reinterpret_cast<HANDLE>(std::wcscmp(name, L"YeManSteamLibrary.InputOwner") == 0
        ? fixture(window)->owner : fixture(window)->parentPid);
}
static DWORD fixtureWindowPid(HWND window, DWORD* pid) { if (pid) *pid = fixture(window)->pid; return 1; }
static DWORD fixtureCurrentPid() { return 42; }
static HANDLE fixtureOpenProcess(DWORD, BOOL, DWORD pid) {
    return processDenied || pid != childWindow.pid ? nullptr : reinterpret_cast<HANDLE>(childHandle);
}
static BOOL fixtureQueryProcess(HANDLE, DWORD, wchar_t* buffer, DWORD* length) {
    if (processQueryFailed || childWindow.image.size() >= *length) return FALSE;
    std::wcscpy(buffer, childWindow.image.c_str()); *length = static_cast<DWORD>(childWindow.image.size()); return TRUE;
}
static BOOL fixtureCloseHandle(HANDLE) { return TRUE; }
static ULONGLONG fixtureGetTickCount64() { return fixtureTick; }
static BOOL fixtureBringWindowToTop(HWND) { return TRUE; }
static BOOL fixtureSetForeground(HWND window) {
    ++foregroundAttempts;
    if (!directFocusAllowed && !(altInserted && altFocusAllowed)) return FALSE;
    foregroundWindow = window;
    return TRUE;
}
static HWND fixtureSetActive(HWND window) { return window; }
static HWND fixtureSetFocus(HWND window) { return window; }
static BOOL fixtureShowWindow(HWND window, int command) {
    if (command == SW_RESTORE) { fixture(window)->visible = true; fixture(window)->iconic = false; ++windowRestores; }
    return TRUE;
}
#define IsWindow fixtureIsWindow
#define IsWindowVisible fixtureIsVisible
#define IsIconic fixtureIsIconic
#define GetForegroundWindow fixtureForeground
#define GetAncestor fixtureAncestor
#define FindWindowW fixtureFindWindow
#define GetPropW fixtureGetProp
#define GetWindowThreadProcessId fixtureWindowPid
#define GetCurrentProcessId fixtureCurrentPid
#define OpenProcess fixtureOpenProcess
#define QueryFullProcessImageNameW fixtureQueryProcess
#define CloseHandle fixtureCloseHandle
#define GetTickCount64 fixtureGetTickCount64
#define BringWindowToTop fixtureBringWindowToTop
#define SetForegroundWindow fixtureSetForeground
#define SetActiveWindow fixtureSetActive
#define SetFocus fixtureSetFocus
#define ShowWindow fixtureShowWindow

namespace ymcc::keyboardEvents { enum class SourceId { FocusAltFallback }; }
static UINT keyboardSelfSendWithEvidence(ymcc::keyboardEvents::SourceId, UINT requested, INPUT*) {
    ++altAttempts;
    altInserted = altInsertionSucceeds;
    return altInserted ? requested : 0;
}

struct { bool enabled = true, virtualTargetEnabled = true; std::string virtualTargetPersona; bool invertHorizontal=false,invertVertical=false; } g_realStickTest;
static XINPUT_GAMEPAD g_curPad{};
static WORD g_prevW = 0;
// Only the screen-pad type dependency is mocked; neutral assembly never
// consults a real screen pad or sends mouse/keyboard input.
namespace ymcc::screenpads {
struct Snapshot { bool present = false; };
static json snapshotJson(const Snapshot&) { return json::object(); }
}
//__PRODUCTION_OWNER_DECLARATIONS__
//__PRODUCTION_WINDOW_HELPERS__
//__PRODUCTION_UI_GLOBALS__
static bool fixtureKnownMsi = false;
static bool msiClawKnownBoardAuthority() { return fixtureKnownMsi; }
using InputHostMutex = std::mutex;
static InputHostMutex g_inputHostMx;
static HANDLE g_inputHostProcess = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
static bool g_inputHostPrepared = true, g_inputHostNeutralized = true, g_inputHostTransportFault = false;
static std::string g_inputHostBoundPersona;
static unsigned long long g_inputHostEpoch = 1;
static std::atomic<bool> g_inputHostFaultHoldsStart{false}, g_inputReady{true}, g_inputHostPowerAdmissionRevoked{false};
static std::atomic<unsigned long long> g_focusIsolationSubmitSkipped{0}, g_focusIsolationNeutralFrames{0};
static bool fixtureHostLifecycleBusy = false, fixtureVirtualWriterAllowed = true;
static bool inputHostLifecycleBusy() { return fixtureHostLifecycleBusy; }
static bool sgVirtualWriterAllowedNow() { return fixtureVirtualWriterAllowed; }
static std::vector<json> outputReceipts;
static void appendNativeLifecycleLog(const char* event, const json& receipt) {
    auto row = receipt;
    row["event"] = event;
    outputReceipts.push_back(row);
}
//__PRODUCTION_OUTPUT_GATES__
//__PRODUCTION_FRAME_ASSEMBLY__

static std::vector<std::string> childActions, parentActions;
static void gamepadEmitUiAction(const char* action, bool repeat = false, ULONGLONG heldMs = 0) {
    if (!gamepadUiInputEligible()) return;
    if (customSteamLibraryChildForeground()) {
        if (const auto semantic = customSteamLibrarySemanticAction(action)) childActions.push_back(semantic);
    } else parentActions.push_back(action);
}
//__PRODUCTION_UI_PROCESS__
static bool physicalRelease = false;
static bool physicalReleaseWindowActive() { return physicalRelease; }
static json outputFrame(const FocusPlan& focusPlan, const FocusFrameContent& frameContent) {
//__PRODUCTION_FINAL_OUTPUT_DECISION__
    const bool semanticNeutral = submitNeutral;
//__PRODUCTION_EFFECTIVE_FRAME__
    // Invoke the actual production lazy JSON oracle for comparison.
    return materializeFrame();
}
extern "C" {
void dsu_publish(const ymcc::dsu::Frame& frame);
size_t dsu_encode_pad_data_for_test(uint8_t* out, size_t outLen, int slot, uint64_t timestampMs);
}
static ymcc::dsu::Frame lastDsuFrame;
static void fixtureDsuPublish(const ymcc::dsu::Frame& frame) {lastDsuFrame=frame;dsu_publish(frame);}
// Source metadata is a controlled seam in this focus-only regression. Actual
// source retirement and producer admission run in dsu_motion_selftest.mjs.
static uint64_t currentPowerGeneration(){return 1;}
static ymcc::input::frameSubmission::Sample inputHostFrameSubmissionSample(const void*){return {};}
static ymcc::dsu::Admission inputCaptureDsuFrameCurrent(const ymcc::dsu::Frame& f,uint64_t) noexcept {return {f.live,f.live&&f.motionReady};}
struct FixtureDsuSensor {std::atomic<uint64_t> activeBindingGeneration{1};};
static const auto windowsMotionSensorOwner=std::make_shared<FixtureDsuSensor>();
static const uint64_t gyroBindingGeneration=1;
static const struct { ymcc::windows_motion::Stamp submission{}; } windowsSensorView;
static const void* t_commonFrameSource=nullptr;
static constexpr int kTickCostPreHostDsu = 0;
static struct { long long dsuUs = 0; } g_tickPreHostSub;
struct TickPreHostSubScope { TickPreHostSubScope(int, long long*) {} };
//__PRODUCTION_DSU_NEUTRAL__
#define dsu_publish fixtureDsuPublish
static std::array<uint8_t, 100> dsuOutput(const FocusPlan& focusPlan, bool live = true) {
    const auto canonicalPad = g_curPad;
    const auto focusMaskedButtons = canonicalPad.wButtons;
    const double mgx = 11, mgy = 22, mgz = 33, maxa = .2, may = .3, maz = 1;
    const bool gamepadMotionPlaneProven=live,frameSteeringYaw=false,controllerMotionRoute=false;
    const std::optional<ymcc::legion_motion::Sample> controllerMotion;
//__PRODUCTION_DSU_DECISION__
//__PRODUCTION_DSU_FEED__
    std::array<uint8_t, 100> packet{};
    if (dsu_encode_pad_data_for_test(packet.data(), packet.size(), 0, 1234) != packet.size())
        throw std::runtime_error("Actual DSU packet encoder failed");
    return packet;
}
#undef dsu_publish
static FocusPlan nativeDsuPlan() {
    g_realStickTest.enabled = g_realStickTest.virtualTargetEnabled = false;
    g_inputHostProcess = nullptr;
    g_inputHostBoundPersona.clear();
    return focusIsolationPlanTick(true);
}
static FocusPlan deckPlan() { FocusPlan plan; plan.steamDeckTarget = true; return plan; }
static FocusPlan ps5Plan() { FocusPlan plan; plan.ps5Target = true; return plan; }
static FocusPlan virtualPlan(const std::string& persona) {
    g_inputHostBoundPersona = persona;
    return focusIsolationPlanTick(true);
}
static void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
static void dsuLiveSample() {
    g_curPad.wButtons = 0xffff;
    g_curPad.sThumbLX = -32768; g_curPad.sThumbLY = 32767;
    g_curPad.sThumbRX = 16384; g_curPad.sThumbRY = -8192;
    g_curPad.bLeftTrigger = 230; g_curPad.bRightTrigger = 204;
}
static void checkDsuNeutral(const std::array<uint8_t, 100>& packet) {
    check(lastDsuFrame.semanticNeutral, "DSU producer did not retain semantic neutral intent");
    check(std::memcmp(packet.data(), "DSUS", 4) == 0 && packet[22] == 2,
        "Neutral DSU output lost connected protocol envelope");
    check(std::all_of(packet.begin() + 36, packet.begin() + 40, [](uint8_t value) { return value == 0; }) &&
        std::all_of(packet.begin() + 44, packet.begin() + 68, [](uint8_t value) { return value == 0; }) &&
        packet[40]==128 && packet[41]==127 && packet[42]==128 && packet[43]==127,
        "Encoded DSU buttons/triggers/touch retained input");
    for (size_t offset = 76; offset < 100; offset += sizeof(float)) {
        float value = 1;
        std::memcpy(&value, packet.data() + offset, sizeof(value));
        check(value == 0, "Encoded DSU motion retained input");
    }
}
static void checkDsuLive(const std::array<uint8_t, 100>& packet) {
    check(lastDsuFrame.motionReady && !lastDsuFrame.semanticNeutral && lastDsuFrame.buttons == 0xffff && lastDsuFrame.lt == 230 && lastDsuFrame.rt == 204,
        "Background DSU feed lost live sample");
    check(lastDsuFrame.lx == -32768 && lastDsuFrame.ly == 32767 && lastDsuFrame.rx == 16384 && lastDsuFrame.ry == -8192,
        "Background DSU axes changed");
    check(packet[36] == 255 && packet[37] == 252 && packet[54] == 204 && packet[55] == 230,
        "Background DSU wire lost buttons/triggers");
    float gyro = 0;
    std::memcpy(&gyro, packet.data() + 88, sizeof(gyro));
    check(gyro == 11, "Background DSU wire lost motion");
}
static void reset() {
    fixtureKnownMsi = false;
    g_inputReleaseRequired.store(false);
    mainWindow = {}; childWindow = {}; mainPopup = {}; otherWindow = {};
    mainWindow.root = mainHandle; childWindow.root = childHandle;
    mainPopup.root = mainHandle; otherWindow.root = otherHandle;
    g_hwnd = mainHandle; foregroundWindow = mainHandle;
    processDenied = processQueryFailed = physicalRelease = false;
    g_webviewReady = directFocusAllowed = altFocusAllowed = altInsertionSucceeds = true;
    altInserted = false; foregroundAttempts = altAttempts = windowRestores = 0;
    foregroundReads = 0;
    fixtureController.moves = 0; g_ctrl = &fixtureController;
    g_inputHostProcess = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
    g_inputHostPrepared = g_inputHostNeutralized = fixtureVirtualWriterAllowed = true;
    g_inputHostTransportFault = fixtureHostLifecycleBusy = false;
    g_inputHostBoundPersona.clear(); g_inputHostEpoch = 1;
    g_inputHostFaultHoldsStart = g_inputHostPowerAdmissionRevoked = false; g_inputReady = true;
    g_focusIsolationSubmitSkipped = g_focusIsolationNeutralFrames = 0;
    fixtureTick = 50000; g_realStickTest = {true, true};
    g_radialUiOwnsInput=false;g_radialReturning=false;
    g_customSteamLibraryInputPhase.store(static_cast<int>(CustomSteamLibraryInputPhase::disabled));
    g_customSteamLibraryInputDeadline.store(fixtureTick + kCustomSteamLibraryLaunchTimeoutMs);
    g_prevW = 0; g_curPad = {}; childActions.clear(); parentActions.clear();
    g_uiShoulderPending = 0; g_uiSummonShoulderHeld = false; g_uiShoulderSuppressUntil = 0;
    g_uiNavX = g_uiNavY = 0; g_uiNavLastEmitTick = 0;
    g_uiSliderTriggerDirection = 0; g_uiSliderTriggerLastEmitTick = 0;
    g_uiStartPending = g_uiStartUsedAsModifier = g_uiFanNodeEditActive = false;
    g_gamepadAxisNeutralAdmissionRequired = false;
}
static void useChild() {
    mainWindow.visible = false; childWindow.present = true; foregroundWindow = childHandle;
    g_customSteamLibraryInputPhase.store(static_cast<int>(CustomSteamLibraryInputPhase::active));
}
static void sample(WORD buttons, ULONGLONG now) {
    g_curPad.wButtons = buttons;
    gamepadProcessUiInput(buttons, now); g_prevW = buttons;
}
static FocusFrameContent liveFrame() {
    FocusFrameContent frame; frame.buttons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_X;
    frame.lxShort = -32768; frame.lyShort = 32767; frame.rxShort = 16384; frame.ryShort = -8192;
    frame.lt = .75f; frame.rt = .5f; frame.ltRaw = .9f; frame.rtRaw = .8f;
    frame.rawLtByte = 230; frame.rawRtByte = 204; frame.backButtons = 3;
    frame.imuAdmitted = true; frame.gyroX = 11; frame.gyroY = 22; frame.gyroZ = 33;
    frame.accelX = .2; frame.accelY = .3; frame.accelZ = 1; frame.qw = .8; frame.qx = .2;
    frame.screenButtons = 4; frame.touchpads.present = true;
    return frame;
}
int main() {
    json cases = json::array();
    const auto test = [&](const char* name, const std::function<void()>& action) {
        reset();
        try { action(); cases.push_back({{"name", name}, {"passed", true}}); }
        catch (const std::exception& error) { cases.push_back({{"name", name}, {"passed", false}, {"error", error.what()}}); }
    };
    test("headless-process-without-window-never-suppresses-parent", [] {
        mainWindow.visible=false;foregroundWindow=otherHandle;
        for(int phase=0;phase<4;++phase){g_customSteamLibraryInputPhase.store(phase);check(!customSteamLibraryInputSuppressed(),"Headless process phase suppressed input");}
    });
    test("background-visible-child-never-suppresses-parent-shortcuts", [] {
        useChild();foregroundWindow=otherHandle;
        check(!customSteamLibraryInputSuppressed()&&!customSteamLibraryChildForeground(),"Background visible child retained parent suppression");
    });
    test("hidden-child-never-suppresses-parent-shortcuts", [] {
        useChild();childWindow.visible=false;check(!customSteamLibraryInputSuppressed(),"Hidden child suppressed shortcuts");
    });
    test("minimized-child-never-suppresses-parent-shortcuts", [] {
        useChild();childWindow.iconic=true;check(!customSteamLibraryInputSuppressed(),"Minimized child suppressed shortcuts");
    });
    test("foreground-loss-releases-library-immediately-and-refocus-reacquires", [] {
        useChild();check(customSteamLibraryInputSuppressed(),"Foreground library was not admitted");
        foregroundWindow=otherHandle;check(!customSteamLibraryInputSuppressed(),"Focus loss left a return barrier");
        foregroundWindow=childHandle;check(customSteamLibraryInputSuppressed(),"Foreground library could not reacquire");
    });
    test("background-bridge-enable-and-disable-never-arm-release-barrier", [] {
        useChild();foregroundWindow=otherHandle;customSteamLibrarySetIntegrationSession(true);
        check(!customSteamLibraryInputSuppressed(),"Background enable muted input");
        customSteamLibrarySetIntegrationSession(false);
        check(!g_inputReleaseRequired.load()&&!customSteamLibraryInputSuppressed(),"Background close armed held-input suppression");
    });
    for (int stalePhase=0;stalePhase<4;++stalePhase) {
        test((std::string("background-child-main-ui-not-blocked-phase-")+std::to_string(stalePhase)).c_str(), [&] {
            useChild();mainWindow.visible=true;foregroundWindow=mainHandle;
            g_customSteamLibraryInputPhase.store(stalePhase);
            check(!customSteamLibraryInputSuppressed()&&gamepadUiInputEligible(),"Background library retained parent UI ownership");
            sample(XINPUT_GAMEPAD_A,60000);sample(0,60010);
            check(parentActions==std::vector<std::string>{"confirm"}&&childActions.empty(),"Background library diverted parent confirmation");
            check(!g_inputReleaseRequired.load(),"Background observation created a held-input barrier");
        });
        test((std::string("background-child-external-output-live-phase-")+std::to_string(stalePhase)).c_str(), [&] {
            useChild();foregroundWindow=otherHandle;
            g_customSteamLibraryInputPhase.store(stalePhase);
            const auto live=liveFrame();const auto assembled=focusIsolationAssembleFrame(live);
            for(const char* persona:{"steamdeck","dualsense-edge","elite"})
                check(outputFrame(virtualPlan(persona),live)==assembled,"Background library muted the external virtual report");
            const auto plan=nativeDsuPlan();dsuLiveSample();checkDsuLive(dsuOutput(plan));
            check(!customSteamLibraryInputSuppressed()&&!g_inputReleaseRequired.load(),"Background game retained a child release gate");
        });
    }
    test("hidden-child-cannot-divert-main-confirmation", [] {
        useChild();mainWindow.visible=true;foregroundWindow=mainHandle;childWindow.visible=false;
        sample(XINPUT_GAMEPAD_A,60000);sample(0,60010);
        check(parentActions==std::vector<std::string>{"confirm"}&&childActions.empty(),"Hidden child diverted main input");
    });
    test("minimized-child-cannot-divert-main-confirmation", [] {
        useChild();mainWindow.visible=true;foregroundWindow=mainHandle;childWindow.iconic=true;
        sample(XINPUT_GAMEPAD_A,60000);sample(0,60010);
        check(parentActions==std::vector<std::string>{"confirm"}&&childActions.empty(),"Minimized child diverted main input");
    });
    test("background-bridge-repetition-cannot-stick-input-gate", [] {
        useChild();foregroundWindow=otherHandle;
        for(int i=0;i<100;++i){customSteamLibrarySetIntegrationSession(true);check(!customSteamLibraryInputSuppressed(),"Repeated background enable acquired input");customSteamLibrarySetIntegrationSession(false);}
        check(!g_inputReleaseRequired.load()&&!customSteamLibraryInputSuppressed(),"Repeated background bridge close retained release barrier");
    });
    test("foreground-close-still-protects-final-physical-release", [] {
        useChild();customSteamLibrarySetIntegrationSession(false);
        check(g_inputReleaseRequired.load(),"Genuine foreground close lost final-button protection");
        foregroundWindow=otherHandle;
        check(!customSteamLibraryInputSuppressed(),"Foreground close left a persistent library ownership gate");
    });
    test("deck-main-foreground-still-neutral", [] { check(steamDeckUiOwnsInput(deckPlan()), "Main UI lost Deck neutral output"); });
    test("deck-main-root-owner-popup-still-neutral", [] { foregroundWindow = popupHandle; check(steamDeckUiOwnsInput(deckPlan()), "Main root-owner foreground behavior changed"); });
    test("deck-parent-owned-library-foreground-neutral", [] { useChild(); check(steamDeckUiOwnsInput(deckPlan()), "Library missed Deck output neutralization"); });
    test("deck-child-recovered-without-renderer-callback", [] { useChild(); g_customSteamLibraryInputPhase.store(0); check(steamDeckUiOwnsInput(deckPlan()), "Recovered native child did not own output"); });
    test("deck-child-during-native-launch-promotion-neutral", [] { useChild(); g_customSteamLibraryInputPhase.store(1); check(steamDeckUiOwnsInput(deckPlan()), "Launching child did not reuse native promotion"); });
    test("deck-no-target-does-not-neutral", [] { useChild(); g_realStickTest.virtualTargetEnabled = false; check(!steamDeckUiOwnsInput(deckPlan()), "Disabled target was suppressed"); });
    test("deck-test-disabled-does-not-neutral", [] { useChild(); g_realStickTest.enabled = false; check(!steamDeckUiOwnsInput(deckPlan()), "Disabled source was suppressed"); });
    test("requested-deck-cannot-mute-other-bound-persona", [] { useChild(); check(!steamDeckUiOwnsInput(FocusPlan{}), "Requested instead of bound persona used"); });
    test("deck-child-hidden-is-not-eligible", [] { useChild(); childWindow.visible = false; check(!steamDeckUiOwnsInput(deckPlan()) && !gamepadUiInputEligible(), "Hidden child owned input"); });
    test("deck-child-iconic-is-not-eligible", [] { useChild(); childWindow.iconic = true; check(!steamDeckUiOwnsInput(deckPlan()), "Minimized child owned output"); });
    test("deck-child-background-leaves-external-output-active", [] { useChild(); foregroundWindow = otherHandle; check(!steamDeckUiOwnsInput(deckPlan()), "Background child muted external input"); });
    test("deck-main-hidden-does-not-neutral", [] { mainWindow.visible = false; check(!steamDeckUiOwnsInput(deckPlan()), "Hidden main muted output"); });
    test("deck-main-iconic-does-not-neutral", [] { mainWindow.iconic = true; check(!steamDeckUiOwnsInput(deckPlan()), "Minimized main muted output"); });
    test("deck-unrelated-window-does-not-neutral", [] { foregroundWindow = otherHandle; check(!steamDeckUiOwnsInput(deckPlan()), "Foreign foreground muted output"); });
    test("deck-invalid-main-window-does-not-neutral", [] { mainWindow.valid = false; check(!steamDeckUiOwnsInput(deckPlan()), "Invalid main owned output"); });
    test("host-owned-standalone-library-is-not-parent-neutralized", [] { useChild(); childWindow.owner = 1; g_customSteamLibraryInputPhase.store(0); check(!steamDeckUiOwnsInput(deckPlan()) && !gamepadUiInputEligible(), "Standalone child was claimed by parent"); });
    test("foreign-parent-pid-cannot-claim-output", [] { useChild(); childWindow.parentPid = 99; check(!steamDeckUiOwnsInput(deckPlan()), "Foreign parent child trusted"); });
    test("wrong-process-image-cannot-claim-output", [] { useChild(); childWindow.image = L"G:\\Fixture\\Unrelated.exe"; check(!steamDeckUiOwnsInput(deckPlan()), "Wrong process was trusted"); });
    test("child-process-query-denied-fails-closed", [] { useChild(); processDenied = true; check(!steamDeckUiOwnsInput(deckPlan()) && !gamepadUiInputEligible(), "Unverified process consumed input"); });
    test("child-process-image-query-failed-fails-closed", [] { useChild(); processQueryFailed = true; check(!steamDeckUiOwnsInput(deckPlan()), "Image query failure trusted"); });
    test("legacy-supported-child-name-still-neutral", [] { useChild(); childWindow.image = L"G:\\Fixture\\SteamLibraryWorkspace.exe"; check(steamDeckUiOwnsInput(deckPlan()), "Existing supported executable name rejected"); });
    test("launch-without-foreground-child-keeps-main-input", [] { g_customSteamLibraryInputPhase.store(1); check(!customSteamLibraryInputSuppressed() && gamepadUiInputEligible() && steamDeckUiOwnsInput(deckPlan()), "Background launch blocked the actual main foreground"); });
    test("old-return-phase-does-not-block-main-foreground", [] { g_customSteamLibraryInputPhase.store(3); check(!customSteamLibraryInputSuppressed() && gamepadUiInputEligible() && steamDeckUiOwnsInput(deckPlan()), "Stale library return phase suppressed main input"); });
    test("ps5-main-foreground-still-neutral", [] { check(ps5UiOwnsInput(ps5Plan()), "PS5 main behavior changed"); });
    test("ps5-child-reuses-same-owner-boundary", [] { useChild(); check(ps5UiOwnsInput(ps5Plan()), "PS5 child missed shared owner boundary"); });
    test("ps5-disabled-target-does-not-neutral", [] { useChild(); g_realStickTest.virtualTargetEnabled = false; check(!ps5UiOwnsInput(ps5Plan()), "PS5 disabled target suppressed"); });
    test("xbox-passthrough-not-expanded-by-this-fix", [] { useChild(); check(!steamDeckUiOwnsInput(FocusPlan{}) && !ps5UiOwnsInput(FocusPlan{}), "Other personas changed"); });
    test("child-X-held-produces-one-edit-and-no-parent-action", [] { useChild(); for (int i = 0; i != 30; ++i) sample(XINPUT_GAMEPAD_X, 1000 + i * 8); check(childActions == std::vector<std::string>{"edit"} && parentActions.empty(), "One physical X duplicated or escaped child"); check(steamDeckUiOwnsInput(deckPlan()), "Semantic X left Deck output active"); });
    test("child-two-X-presses-remain-two-legitimate-edits", [] { useChild(); sample(XINPUT_GAMEPAD_X, 1000); sample(0, 1020); sample(XINPUT_GAMEPAD_X, 1040); check(childActions == std::vector<std::string>{"edit", "edit"}, "Legitimate second X was swallowed"); });
    test("child-A-held-produces-one-accept", [] { useChild(); for (int i = 0; i != 30; ++i) sample(XINPUT_GAMEPAD_A, 1000 + i * 8); check(childActions == std::vector<std::string>{"accept"}, "A repeated without release"); });
    test("child-B-held-produces-one-back", [] { useChild(); for (int i = 0; i != 30; ++i) sample(XINPUT_GAMEPAD_B, 1000 + i * 8); check(childActions == std::vector<std::string>{"back"} && parentActions.empty(), "B leaked to parent or doubled"); });
    test("child-direction-retains-main-repeat-cadence", [] { useChild(); for (int i = 0; i <= 56; ++i) sample(XINPUT_GAMEPAD_DPAD_DOWN, 1000 + i * 8); check(childActions == std::vector<std::string>{"navigate-down", "navigate-down", "navigate-down"}, "Direction duplicated outside existing repeat cadence"); });
    test("child-back-X-chord-does-not-become-edit", [] { useChild(); sample(XINPUT_GAMEPAD_BACK | XINPUT_GAMEPAD_X, 1000); check(childActions.empty() && parentActions.empty(), "Existing modifier ownership lost"); });
    test("held-X-on-focus-entry-does-not-become-new-edge", [] { foregroundWindow = otherHandle; sample(XINPUT_GAMEPAD_X, 1000); useChild(); sample(XINPUT_GAMEPAD_X, 1010); check(childActions.empty(), "Focus entry replayed held X"); });
    test("main-X-remains-reserved-and-not-keyboard-action", [] { sample(XINPUT_GAMEPAD_X, 1000); check(parentActions.empty() && childActions.empty(), "Main X mapping changed"); });
    test("main-Y-editor-mapping-remains-intact", [] { sample(XINPUT_GAMEPAD_Y, 1000); check(parentActions == std::vector<std::string>{"edit-game"} && childActions.empty(), "Main Y editor mapping changed"); });
    test("child-Y-does-not-create-second-edit-path", [] { useChild(); sample(XINPUT_GAMEPAD_Y, 1000); check(parentActions.empty() && childActions.empty(), "Child Y duplicated existing X mapping"); });
    test("child-frame-is-fully-neutral-not-just-buttons", [] { useChild(); const auto frame = outputFrame(deckPlan(), liveFrame()); const auto neutral = focusIsolationAssembleFrame(focusIsolationNeutralContent()); check(frame == neutral, "Axes/triggers/IMU/raw/back/touch state survived neutral output"); });
    test("child-output-gate-does-not-modify-physical-sample", [] { useChild(); g_curPad = {XINPUT_GAMEPAD_X, 12, 34, -32768, 32767, 100, -200}; const auto before = g_curPad; const auto live = liveFrame(); outputFrame(deckPlan(), live); check(std::memcmp(&before, &g_curPad, sizeof before) == 0 && live.buttons != 0 && live.imuAdmitted, "Virtual neutral gate changed physical UI input"); });
    test("foreign-foreground-frame-stays-identical", [] { useChild(); foregroundWindow = otherHandle; const auto live = liveFrame(); check(outputFrame(deckPlan(), live) == focusIsolationAssembleFrame(live), "External foreground frame changed"); });
    test("disabled-virtual-target-frame-stays-identical", [] { useChild(); g_realStickTest.virtualTargetEnabled = false; const auto live = liveFrame(); check(outputFrame(deckPlan(), live) == focusIsolationAssembleFrame(live), "Disabled virtual target affected frame"); });
    test("existing-source-neutral-cleanup-still-wins", [] { foregroundWindow = otherHandle; auto plan = deckPlan(); plan.neutralCleanup = true; check(outputFrame(plan, liveFrame()) == focusIsolationAssembleFrame(focusIsolationNeutralContent()), "Source neutral cleanup lost"); });
    test("existing-physical-release-window-still-wins", [] { foregroundWindow = otherHandle; physicalRelease = true; check(outputFrame(deckPlan(), liveFrame()) == focusIsolationAssembleFrame(focusIsolationNeutralContent()), "Physical release boundary lost"); });
    for (const std::string persona : {"steamdeck", "dualsense-edge", "dualshock4", "elite"}) {
        test(("msi-" + persona + "-bound-foreground-neutral").c_str(), [&] {
            // This historical row now verifies the explicit retired-persona boundary.
            // The original neutral-output assertions below still execute for every current persona.
            if(!ymcc::controllerPersona::currentOutput(persona)){check(!msiUiBoundTargetAdmitted(persona)&&!virtualUiBoundPersona(persona)[0],"Retired persona regained current virtual-output admission");return;}
            fixtureKnownMsi = true; FocusPlan plan; plan.msiTarget = msiUiBoundTargetAdmitted(persona);
            check(plan.msiTarget && msiUiOwnsInput(plan), "MSI bound persona missed UI output isolation");
            const auto physical = liveFrame(); const auto before = physical;
            check(outputFrame(plan, physical) == focusIsolationAssembleFrame(focusIsolationNeutralContent()), "MSI virtual report not fully neutral");
            check(std::memcmp(&physical, &before, sizeof physical) == 0, "MSI UI source sample was changed");
            sample(XINPUT_GAMEPAD_A, 60000); sample(0, 60010);
            check(!parentActions.empty(), "MSI physical UI action was suppressed with virtual output");
        });
        test(("msi-" + persona + "-background-passthrough").c_str(), [&] {
            fixtureKnownMsi = true; FocusPlan plan; plan.msiTarget = msiUiBoundTargetAdmitted(persona); foregroundWindow = otherHandle;
            const auto physical = liveFrame(); check(outputFrame(plan, physical) == focusIsolationAssembleFrame(physical), "MSI background game report changed");
        });
        test(("msi-" + persona + "-parent-child-neutral").c_str(), [&] {
            // This historical row now verifies the explicit retired-persona boundary.
            // The original neutral-output assertions below still execute for every current persona.
            if(!ymcc::controllerPersona::currentOutput(persona)){check(!msiUiBoundTargetAdmitted(persona)&&!virtualUiBoundPersona(persona)[0],"Retired persona regained current virtual-output admission");return;}
            fixtureKnownMsi = true; FocusPlan plan; plan.msiTarget = msiUiBoundTargetAdmitted(persona); useChild();
            check(outputFrame(plan, liveFrame()) == focusIsolationAssembleFrame(focusIsolationNeutralContent()), "MSI owned child missed same UI isolation");
        });
        test(("other-family-" + persona + "-msi-policy-inactive").c_str(), [&] {
            FocusPlan plan; plan.msiTarget = msiUiBoundTargetAdmitted(persona); const auto physical = liveFrame();
            check(!plan.msiTarget && !msiUiOwnsInput(plan), "MSI-only policy leaked to another family");
            check(outputFrame(plan, physical) == focusIsolationAssembleFrame(physical), "MSI-only report change leaked to another family");
        });
    }
    test("msi-unbound-requested-persona-cannot-mute", [] { fixtureKnownMsi = true; FocusPlan plan; plan.msiTarget = msiUiBoundTargetAdmitted(""); check(!msiUiOwnsInput(plan), "Unbound requested persona gained isolation"); });
    test("msi-unknown-bound-persona-cannot-mute", [] { fixtureKnownMsi = true; check(!msiUiBoundTargetAdmitted("unknown") && !msiUiBoundTargetAdmitted("disabled"), "Unknown/off persona gained isolation"); });
    test("msi-virtual-off-physical-ui-stays-eligible", [] { fixtureKnownMsi = true; FocusPlan plan; plan.msiTarget = msiUiBoundTargetAdmitted("elite"); g_realStickTest.virtualTargetEnabled = false; check(!msiUiOwnsInput(plan) && gamepadUiInputEligible(), "Virtual off disabled physical UI route"); });
    test("msi-hidden-or-minimized-ui-does-not-own-output", [] { fixtureKnownMsi = true; FocusPlan plan; plan.msiTarget = msiUiBoundTargetAdmitted("elite"); mainWindow.visible = false; check(!msiUiOwnsInput(plan), "Hidden UI owned MSI output"); mainWindow.visible = true; mainWindow.iconic = true; check(!msiUiOwnsInput(plan), "Minimized UI owned MSI output"); });
    test("msi-launch-return-barrier-still-wins", [] { fixtureKnownMsi = true; FocusPlan plan; plan.msiTarget = msiUiBoundTargetAdmitted("elite"); g_customSteamLibraryInputPhase.store(static_cast<int>(CustomSteamLibraryInputPhase::returning)); check(msiUiOwnsInput(plan), "Stale return phase blocked actual main foreground input"); });

    for (const std::string persona : {"steamdeck", "dualsense-edge", "dualshock4", "elite"}) {
        test(("virtual-" + persona + "-main-foreground-neutral").c_str(), [&] {
            // This historical row now verifies the explicit retired-persona boundary.
            // The original neutral-output assertions below still execute for every current persona.
            if(!ymcc::controllerPersona::currentOutput(persona)){check(!msiUiBoundTargetAdmitted(persona)&&!virtualUiBoundPersona(persona)[0],"Retired persona regained current virtual-output admission");return;}
            check(gamepadUiInputEligible(), "Main UI must retain physical input");
            check(outputFrame(virtualPlan(persona), liveFrame()) == focusIsolationAssembleFrame(focusIsolationNeutralContent()), "Foreground virtual output was not fully neutral");
        });
        test(("virtual-" + persona + "-child-foreground-neutral").c_str(), [&] {
            // This historical row now verifies the explicit retired-persona boundary.
            // The original neutral-output assertions below still execute for every current persona.
            if(!ymcc::controllerPersona::currentOutput(persona)){check(!msiUiBoundTargetAdmitted(persona)&&!virtualUiBoundPersona(persona)[0],"Retired persona regained current virtual-output admission");return;}
            useChild();
            check(gamepadUiInputEligible(), "Owned child must retain physical input");
            check(outputFrame(virtualPlan(persona), liveFrame()) == focusIsolationAssembleFrame(focusIsolationNeutralContent()), "Child virtual output was not fully neutral");
        });
        test(("virtual-" + persona + "-background-passthrough").c_str(), [&] {
            foregroundWindow = otherHandle;
            const auto physical = liveFrame();
            check(outputFrame(virtualPlan(persona), physical) == focusIsolationAssembleFrame(physical), "Background output did not resume");
        });
        test(("virtual-" + persona + "-hidden-and-minimized-passthrough").c_str(), [&] {
            const auto physical = liveFrame();
            mainWindow.visible = false;
            check(outputFrame(virtualPlan(persona), physical) == focusIsolationAssembleFrame(physical), "Hidden UI muted output");
            mainWindow.visible = true; mainWindow.iconic = true;
            check(outputFrame(virtualPlan(persona), physical) == focusIsolationAssembleFrame(physical), "Minimized UI muted output");
        });
        test(("virtual-" + persona + "-disabled-keeps-native-ui").c_str(), [&] {
            g_realStickTest.virtualTargetEnabled = false;
            check(!virtualUiOwnsInput(virtualPlan(persona)) && gamepadUiInputEligible(), "Virtual-off changed native UI input");
        });
        test(("virtual-" + persona + "-launch-return-barrier").c_str(), [&] {
            g_customSteamLibraryInputPhase.store(static_cast<int>(CustomSteamLibraryInputPhase::launching));
            check(virtualUiOwnsInput(virtualPlan(persona)) == ymcc::controllerPersona::currentOutput(persona), "Launch hint changed actual foreground/persona admission");
            g_customSteamLibraryInputPhase.store(static_cast<int>(CustomSteamLibraryInputPhase::returning));
            check(virtualUiOwnsInput(virtualPlan(persona)) == ymcc::controllerPersona::currentOutput(persona), "Stale return phase changed actual foreground/persona admission");
        });
        test(("virtual-" + persona + "-reuses-same-tick-foreground-observation").c_str(), [&] {
            check(gamepadUiInputEligible(), "Fixture UI must be foreground eligible");
            const auto singleObservationReads = foregroundReads;
            foregroundReads = 0;
            (void)outputFrame(virtualPlan(persona), liveFrame());
            check(foregroundReads == singleObservationReads, "Generic output added duplicate foreground observations");
        });
    }
    test("retired-personas-cannot-claim-current-output", [] {
        fixtureKnownMsi=true;
        for(const std::string persona:{"xbox360","dualshock4","dualsense"}){const auto plan=virtualPlan(persona);check(!msiUiBoundTargetAdmitted(persona)&&!virtualUiBoundPersona(persona)[0]&&!plan.msiTarget&&!plan.steamDeckTarget&&!plan.ps5Target&&!virtualUiOwnsInput(plan),"Retired persona bypassed actual catalog or bound-output gates");}
    });
    test("virtual-unbound-disabled-unknown-cannot-mute", [] {
        check(!virtualUiOwnsInput(virtualPlan("")) && !virtualUiOwnsInput(virtualPlan("disabled")) &&
            !virtualUiOwnsInput(virtualPlan("unknown")), "Unbound, native or unknown target gained output ownership");
    });
    test("virtual-source-disabled-cannot-mute", [] {
        g_realStickTest.enabled = false;
        check(!virtualUiOwnsInput(virtualPlan("elite")), "Disabled source gained output ownership");
    });

    test("virtual-output-receipts-follow-transition-and-first-acceptance", [] {
        outputReceipts.clear();
        const auto xbox = virtualPlan("elite"), edge = virtualPlan("dualsense-edge");
        const json pending = {{"hostSubmission", "publication-suppressed"}};
        const json active = {{"hostSubmission", "frame-accepted"}, {"focusIsolationSemanticNeutral", false}};
        const json neutral = {{"hostSubmission", "frame-accepted"}, {"focusIsolationSemanticNeutral", true}};
        virtualUiOutputNote(xbox, true, true, pending, true, "xinput");
        check(outputReceipts.size() == 1 && !outputReceipts.back()["neutralAccepted"].get<bool>(), "Pending submission claimed neutral acceptance");
        virtualUiOutputNote(xbox, true, true, pending, true, "xinput");
        virtualUiOutputNote(xbox, true, true, active, true, "xinput");
        check(outputReceipts.size() == 1, "Pending or non-neutral frames produced repeated acceptance logs");
        virtualUiOutputNote(xbox, true, true, neutral, true, "xinput");
        check(outputReceipts.size() == 2 && outputReceipts.back()["phase"] == "neutral-accepted", "First actual neutral acceptance was not logged");
        for (int frame = 0; frame < 10; ++frame) virtualUiOutputNote(xbox, true, true, neutral, true, "xinput");
        check(outputReceipts.size() == 2, "Steady output wrote per-frame receipts");
        virtualUiOutputNote(edge, true, true, neutral, true, "xinput");
        check(outputReceipts.size() == 3 && outputReceipts.back()["persona"] == "dualsense-edge", "Bound persona change lost its receipt");
        virtualUiOutputNote(edge, false, false, active, true, "xinput");
        check(outputReceipts.size() == 4 && outputReceipts.back()["phase"] == "passthrough-requested", "UI release was not logged");
        virtualUiOutputNote(edge, false, false, active, true, "xinput");
        check(outputReceipts.size() == 4 && outputReceipts.back()["verificationScope"] == "final-virtual-output-only", "Background receipt repeated or claimed physical isolation");
        auto replacedHost = edge;
        replacedHost.virtualUiHostEpoch = 2;
        virtualUiOutputNote(replacedHost, true, true, neutral, true, "xinput");
        replacedHost.virtualUiHostEpoch = 3;
        virtualUiOutputNote(replacedHost, true, true, neutral, true, "xinput");
        check(outputReceipts.size() == 6 && outputReceipts.back()["hostEpoch"] == 3 &&
            outputReceipts.back()["neutralAccepted"].get<bool>(), "Same-persona Host replacement lost neutral acceptance evidence");
    });

    test("native-focus-direct-handoff-preserves-virtual-off-ui", [] {
        foregroundWindow = otherHandle;
        g_realStickTest.virtualTargetEnabled = false;
        check(refocusWebView(true) && gamepadUiInputEligible(), "Native mode lost main foreground handoff");
        check(foregroundAttempts == 1 && altAttempts == 0 && fixtureController.moves == 1, "Successful direct handoff sent unnecessary fallback input");
    });
    test("native-focus-alt-fallback-after-denied-handoff", [] {
        foregroundWindow = otherHandle; directFocusAllowed = false;
        check(refocusWebView(true) && gamepadUiInputEligible(), "Bounded Alt fallback did not recover foreground");
        check(foregroundAttempts == 2 && altAttempts == 1 && fixtureController.moves == 1, "Fallback did not use one retry and one UI focus move");
    });
    test("native-focus-without-fallback-does-not-claim-success", [] {
        foregroundWindow = otherHandle; directFocusAllowed = false;
        check(!refocusWebView(false) && !gamepadUiInputEligible(), "Denied foreground handoff claimed native ownership");
        check(foregroundAttempts == 1 && altAttempts == 0 && fixtureController.moves == 0, "Unrequested fallback or UI focus was sent");
    });
    test("native-focus-failed-alt-does-not-claim-success", [] {
        foregroundWindow = otherHandle; directFocusAllowed = false; altInsertionSucceeds = false;
        check(!refocusWebView(true) && fixtureController.moves == 0, "Failed Alt fallback claimed native focus");
    });
    test("native-focus-cold-webview-retains-first-frame-gate", [] {
        g_webviewReady = false; foregroundWindow = otherHandle;
        check(!refocusWebView(true) && foregroundAttempts == 0 && altAttempts == 0, "Cold shell bypassed first-frame admission");
    });
    test("native-focus-minimized-window-restores-once", [] {
        mainWindow.iconic = true; foregroundWindow = otherHandle;
        check(refocusWebView(true) && windowRestores == 1 && gamepadUiInputEligible(), "Native focus failed to restore minimized main window");
    });
    test("native-focus-invalid-window-does-no-io", [] {
        g_hwnd = nullptr;
        check(!refocusWebView(true) && foregroundAttempts == 0 && altAttempts == 0, "Invalid main HWND caused focus/input operations");
    });

    test("virtual-plan-uses-bound-target-and-captured-host-generation", [] {
        g_realStickTest.virtualTargetPersona = "elite";
        const auto unbound = focusIsolationPlanTick(true);
        check(!virtualUiOwnsInput(unbound), "Requested target gained ownership before binding");
        g_inputHostBoundPersona = "elite"; g_inputHostEpoch = 91;
        g_realStickTest.virtualTargetPersona = "disabled";
        const auto bound = focusIsolationPlanTick(true);
        check(virtualUiOwnsInput(bound) && std::strcmp(bound.virtualUiPersona, "elite") == 0 &&
            bound.virtualUiHostEpoch == 91, "Output did not capture actual bound target and generation");
    });
    test("virtual-plan-host-and-power-publication-guards-remain-authoritative", [] {
        g_inputHostBoundPersona = "elite";
        g_inputHostProcess = nullptr;
        check(focusIsolationPlanTick(true).decision == FocusDecision::SkipHostUnavailable, "Missing Host lost publication guard");
        g_inputHostProcess = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(1));
        g_inputHostPrepared = false;
        check(focusIsolationPlanTick(true).decision == FocusDecision::SkipHostUnavailable, "Unprepared Host gained admission");
        g_inputHostPrepared = true; g_inputHostNeutralized = false;
        check(focusIsolationPlanTick(true).decision == FocusDecision::SkipHostUnavailable, "Host without first neutral gained admission");
        g_inputHostNeutralized = true; g_inputHostTransportFault = true;
        check(focusIsolationPlanTick(true).decision == FocusDecision::SkipHostUnavailable, "Transport fault lost publication guard");
        g_inputHostTransportFault = false; fixtureHostLifecycleBusy = true;
        check(focusIsolationPlanTick(true).decision == FocusDecision::SkipHostUnavailable, "Lifecycle handoff gained admission");
        fixtureHostLifecycleBusy = false; g_inputHostPowerAdmissionRevoked = true;
        check(focusIsolationPlanTick(true).decision == FocusDecision::SkipHostUnavailable, "Power revocation lost publication guard");
        g_inputHostPowerAdmissionRevoked = false; fixtureVirtualWriterAllowed = false;
        check(focusIsolationPlanTick(true).decision == FocusDecision::SkipHostUnavailable, "Closed virtual writer gained admission");
    });
    test("virtual-plan-source-loss-retains-complete-neutral-cleanup", [] {
        g_inputHostBoundPersona = "elite"; foregroundWindow = otherHandle;
        const auto plan = focusIsolationPlanTick(false);
        check(plan.decision == FocusDecision::Neutral && plan.neutralCleanup &&
            outputFrame(plan, liveFrame()) == focusIsolationAssembleFrame(focusIsolationNeutralContent()), "Background source loss retained live output");
    });

    test("dsu-native-main-foreground-neutral-wire", [] {
        const auto plan = nativeDsuPlan(); dsuLiveSample();
        const auto physical = g_curPad;
        checkDsuNeutral(dsuOutput(plan));
        check(std::memcmp(&physical, &g_curPad, sizeof(physical)) == 0, "DSU shielding changed physical UI sample");
    });
    test("dsu-native-child-foreground-neutral-wire", [] {
        const auto plan = nativeDsuPlan(); useChild(); dsuLiveSample();
        checkDsuNeutral(dsuOutput(plan));
    });
    test("dsu-native-root-owner-foreground-neutral-wire", [] {
        const auto plan = nativeDsuPlan(); foregroundWindow = popupHandle; dsuLiveSample();
        checkDsuNeutral(dsuOutput(plan));
    });
    test("dsu-native-background-preserves-real-wire", [] {
        const auto plan = nativeDsuPlan(); foregroundWindow = otherHandle; dsuLiveSample();
        checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-focus-roundtrip-restores-identical-wire", [] {
        const auto plan = nativeDsuPlan(); dsuLiveSample(); foregroundWindow = otherHandle;
        const auto baseline = dsuOutput(plan); checkDsuLive(baseline);
        foregroundWindow = mainHandle; checkDsuNeutral(dsuOutput(plan));
        foregroundWindow = otherHandle;
        check(dsuOutput(plan) == baseline, "DSU pass-through did not restore original wire payload");
    });
    test("dsu-native-no-host-still-neutral", [] {
        const auto plan = nativeDsuPlan(); dsuLiveSample();
        check(plan.decision == FocusDecision::SkipHostUnavailable && !plan.virtualUiPersona[0], "Native test accidentally acquired virtual output");
        checkDsuNeutral(dsuOutput(plan));
    });
    test("dsu-native-child-hidden-passthrough", [] {
        const auto plan = nativeDsuPlan(); useChild(); childWindow.visible = false; dsuLiveSample();
        checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-child-iconic-passthrough", [] {
        const auto plan = nativeDsuPlan(); useChild(); childWindow.iconic = true; dsuLiveSample();
        checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-child-background-passthrough", [] {
        const auto plan = nativeDsuPlan(); useChild(); foregroundWindow = otherHandle; dsuLiveSample();
        checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-unowned-child-passthrough", [] {
        const auto plan = nativeDsuPlan(); useChild(); childWindow.owner = 0; dsuLiveSample();
        checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-launch-without-child-passthrough", [] {
        const auto plan = nativeDsuPlan(); foregroundWindow = otherHandle;
        g_customSteamLibraryInputPhase.store(static_cast<int>(CustomSteamLibraryInputPhase::launching));
        dsuLiveSample(); checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-return-without-child-passthrough", [] {
        const auto plan = nativeDsuPlan(); foregroundWindow = otherHandle;
        g_customSteamLibraryInputPhase.store(static_cast<int>(CustomSteamLibraryInputPhase::returning));
        dsuLiveSample(); checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-main-hidden-passthrough", [] {
        const auto plan = nativeDsuPlan(); mainWindow.visible = false; dsuLiveSample();
        checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-main-iconic-passthrough", [] {
        const auto plan = nativeDsuPlan(); mainWindow.iconic = true; dsuLiveSample();
        checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-invalid-main-passthrough", [] {
        const auto plan = nativeDsuPlan(); g_hwnd = nullptr; dsuLiveSample();
        checkDsuLive(dsuOutput(plan));
    });
    test("dsu-native-physical-release-keeps-neutral-cleanup", [] {
        const auto plan = nativeDsuPlan(); foregroundWindow = otherHandle; physicalRelease = true; dsuLiveSample();
        checkDsuNeutral(dsuOutput(plan));
    });
    test("dsu-background-source-loss-keeps-neutral-cleanup", [] {
        auto plan = nativeDsuPlan(); foregroundWindow = otherHandle; plan.neutralCleanup = true; dsuLiveSample();
        checkDsuNeutral(dsuOutput(plan, false));
    });
    for (const auto& persona : {"elite", "dualshock4", "steamdeck", "dualsense-edge"}) {
        test((std::string("dsu-virtual-") + persona + "-foreground-neutral-wire").c_str(), [&] {
            dsuLiveSample(); checkDsuNeutral(dsuOutput(virtualPlan(persona)));
        });
        test((std::string("dsu-virtual-") + persona + "-background-live-wire").c_str(), [&] {
            foregroundWindow = otherHandle; dsuLiveSample(); checkDsuLive(dsuOutput(virtualPlan(persona)));
        });
    }
    test("dsu-native-disabled-target-with-stale-binding-neutral", [] {
        const auto plan = virtualPlan("elite"); g_realStickTest.virtualTargetEnabled = false; dsuLiveSample();
        checkDsuNeutral(dsuOutput(plan));
    });
    test("dsu-native-disabled-test-with-stale-binding-neutral", [] {
        const auto plan = virtualPlan("elite"); g_realStickTest.enabled = false; dsuLiveSample();
        checkDsuNeutral(dsuOutput(plan));
    });
    test("dsu-receipts-only-on-actual-output-transitions", [] {
        const auto plan = nativeDsuPlan(); dsuLiveSample(); foregroundWindow = otherHandle;
        dsuOutput(plan); outputReceipts.clear(); foregroundWindow = mainHandle;
        for (unsigned frame = 0; frame < 5; ++frame) dsuOutput(plan);
        check(outputReceipts.size() == 1 && outputReceipts[0].value("event", "") == "dsu-ui-output" &&
            outputReceipts[0].value("uiOwnsInput", false) && outputReceipts[0].value("submitNeutral", false) &&
            outputReceipts[0].value("verificationScope", "") == "dsu-feed-only", "DSU transition receipt missing or repeated per tick");
        foregroundWindow = otherHandle; dsuOutput(plan);
        check(outputReceipts.size() == 2 && outputReceipts[1].value("phase", "") == "passthrough", "DSU return receipt missing");
    });

    size_t failures = 0; for (const auto& result : cases) if (!result["passed"].get<bool>()) ++failures;
    std::cout << json{{"allPassed", failures == 0}, {"caseCount", cases.size()}, {"failedCount", failures}, {"cases", cases},
        {"productionHelpersExecuted", true}, {"win32AndActionsMocked", true}, {"realSteamFilesModified", false}, {"realDeviceIo", false}}.dump(2) << '\n';
    return failures ? 1 : 0;
}
