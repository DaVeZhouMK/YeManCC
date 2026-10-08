// Generated runner inserts exact production helpers from native/main.cpp.
// Every Win32 probe is a fixture; this executable opens no real process/window.
#define NOMINMAX
#include <windows.h>
#include <xinput.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include "json.hpp"
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
static FixtureWindow* fixture(HWND window) { return reinterpret_cast<FixtureWindow*>(window); }
static BOOL fixtureIsWindow(HWND window) { return window && fixture(window)->valid; }
static BOOL fixtureIsVisible(HWND window) { return fixtureIsWindow(window) && fixture(window)->visible; }
static BOOL fixtureIsIconic(HWND window) { return fixtureIsWindow(window) && fixture(window)->iconic; }
static HWND fixtureForeground() { return foregroundWindow; }
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

struct { bool enabled = true, virtualTargetEnabled = true; } g_realStickTest;
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
//__PRODUCTION_OUTPUT_GATES__
//__PRODUCTION_FRAME_ASSEMBLY__

static std::vector<std::string> childActions, parentActions;
static void gamepadEmitUiAction(const char* action) {
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
    return frame;
}
static FocusPlan deckPlan() { FocusPlan plan; plan.steamDeckTarget = true; return plan; }
static FocusPlan ps5Plan() { FocusPlan plan; plan.ps5Target = true; return plan; }
static void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
static void reset() {
    mainWindow = {}; childWindow = {}; mainPopup = {}; otherWindow = {};
    mainWindow.root = mainHandle; childWindow.root = childHandle;
    mainPopup.root = mainHandle; otherWindow.root = otherHandle;
    g_hwnd = mainHandle; foregroundWindow = mainHandle;
    processDenied = processQueryFailed = physicalRelease = false;
    fixtureTick = 50000; g_realStickTest = {true, true};
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
    test("launch-before-child-keeps-existing-admission-gate", [] { g_customSteamLibraryInputPhase.store(1); check(!gamepadUiInputEligible() && !steamDeckUiOwnsInput(deckPlan()), "Launch gap admitted parent input"); });
    test("return-before-neutral-keeps-existing-admission-gate", [] { g_customSteamLibraryInputPhase.store(3); check(!gamepadUiInputEligible() && !steamDeckUiOwnsInput(deckPlan()), "Return gap admitted parent input"); });
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
    size_t failures = 0; for (const auto& result : cases) if (!result["passed"].get<bool>()) ++failures;
    std::cout << json{{"allPassed", failures == 0}, {"caseCount", cases.size()}, {"failedCount", failures}, {"cases", cases},
        {"productionHelpersExecuted", true}, {"win32AndActionsMocked", true}, {"realSteamFilesModified", false}, {"realDeviceIo", false}}.dump(2) << '\n';
    return failures ? 1 : 0;
}
