// Frozen-header tests: every query is a scalar macro fake. No live User32 calls.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include "json.hpp"

namespace allocation_probe {
inline bool forbidden = false;
inline unsigned attempts = 0;
}
void* operator new(std::size_t size) {
    if (allocation_probe::forbidden) { ++allocation_probe::attempts; throw std::bad_alloc(); }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace fake_os {
DWORD error() noexcept; void setError(DWORD) noexcept;
ULONGLONG tick(); DWORD currentThread(); HWND foreground();
DWORD windowThread(HWND, LPDWORD); BOOL gui(DWORD, PGUITHREADINFO);
}
// No definitions: any accidental activation/input/privacy call fails to link.
namespace forbidden_os {
BOOL allowForeground(DWORD); BOOL setForeground(HWND); HWND setFocus(HWND);
HWND setActive(HWND); BOOL bringToTop(HWND); BOOL attachThread(DWORD, DWORD, BOOL);
UINT sendInput(UINT, LPINPUT, int); void keyEvent(BYTE, BYTE, DWORD, ULONG_PTR);
void mouseEvent(DWORD, DWORD, DWORD, DWORD, ULONG_PTR);
int title(HWND, LPWSTR, int); int className(HWND, LPWSTR, int);
UINT moduleName(HWND, LPWSTR, UINT); HANDLE openProcess(DWORD, BOOL, DWORD);
BOOL imageName(HANDLE, DWORD, LPWSTR, PDWORD); BOOL visible(HWND);
SHORT asyncKey(int); BOOL keyboard(PBYTE); BOOL lastInput(PLASTINPUTINFO);
void sleep(DWORD); DWORD sleepEx(DWORD, BOOL);
UINT_PTR timer(HWND, UINT_PTR, UINT, TIMERPROC);
}
#define GetLastError fake_os::error
#define SetLastError fake_os::setError
#define GetTickCount64 fake_os::tick
#define GetCurrentThreadId fake_os::currentThread
#define GetForegroundWindow fake_os::foreground
#define GetWindowThreadProcessId fake_os::windowThread
#define GetGUIThreadInfo fake_os::gui
#define AllowSetForegroundWindow forbidden_os::allowForeground
#define SetForegroundWindow forbidden_os::setForeground
#define SetFocus forbidden_os::setFocus
#define SetActiveWindow forbidden_os::setActive
#define BringWindowToTop forbidden_os::bringToTop
#define AttachThreadInput forbidden_os::attachThread
#define SendInput forbidden_os::sendInput
#define keybd_event forbidden_os::keyEvent
#define mouse_event forbidden_os::mouseEvent
#define GetWindowTextW forbidden_os::title
#define GetClassNameW forbidden_os::className
#define GetWindowModuleFileNameW forbidden_os::moduleName
#define OpenProcess forbidden_os::openProcess
#define QueryFullProcessImageNameW forbidden_os::imageName
#define IsWindowVisible forbidden_os::visible
#define GetAsyncKeyState forbidden_os::asyncKey
#define GetKeyboardState forbidden_os::keyboard
#define GetLastInputInfo forbidden_os::lastInput
#define Sleep forbidden_os::sleep
#define SleepEx forbidden_os::sleepEx
#define SetTimer forbidden_os::timer
#include "../native/task_view_foreground_evidence.h"

using Json = nlohmann::json;
namespace evidence = ymcc::taskViewForeground;
static_assert(noexcept(evidence::capture(nullptr)));
static_assert(std::is_trivially_copyable_v<evidence::Snapshot>);
static_assert(std::is_standard_layout_v<evidence::Snapshot>);
static_assert(sizeof(std::uintptr_t) == 8, "Exercise full x64 HWND JSON values.");

namespace fake_os {
struct Injected {};
struct WindowReply { HWND hwnd; DWORD tid, pid, failure = 0; };
struct GuiReply { DWORD tid; bool ok = true; DWORD failure = 0; GUITHREADINFO data{}; };
struct State {
    DWORD lastError = 0x1234, workerTid = 10284;
    HWND caller = reinterpret_cast<HWND>(std::uintptr_t{0x2222});
    std::array<HWND, 2> foregrounds{};
    std::array<WindowReply, 3> windows{};
    std::array<GuiReply, 2> guis{};
    std::array<const char*, 16> calls{};
    unsigned callCount = 0, ticks = 0, foregroundReads = 0, windowReads = 0, guiReads = 0;
    unsigned zeroGuiQueries = 0, violations = 0;
    int throwAt = -1;
};
State state;
HWND handle(std::uint64_t value) noexcept { return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value)); }
void record(const char* name) {
    if (state.callCount >= state.calls.size()) { ++state.violations; throw Injected{}; }
    const auto at = state.callCount++;
    state.calls[at] = name;
    state.lastError = 0xBAD;
    if (static_cast<int>(at) == state.throwAt) throw Injected{};
}
DWORD error() noexcept { return state.lastError; }
void setError(DWORD value) noexcept { state.lastError = value; }
ULONGLONG tick() {
    const ULONGLONG value = 100 + 10 * state.ticks++;
    record("tick"); return value;
}
DWORD currentThread() { record("currentThread"); return state.workerTid; }
HWND foreground() {
    record("foreground");
    if (state.foregroundReads >= state.foregrounds.size()) { ++state.violations; throw Injected{}; }
    return state.foregrounds[state.foregroundReads++];
}
DWORD windowThread(HWND hwnd, LPDWORD pid) {
    record("windowThread");
    if (!hwnd || state.windowReads >= state.windows.size()) { ++state.violations; throw Injected{}; }
    const auto& reply = state.windows[state.windowReads++];
    if (reply.hwnd != hwnd) ++state.violations;
    // Dirty outputs on failure must never become observed process identity.
    *pid = reply.pid;
    if (!reply.tid) { state.lastError = reply.failure; return 0; }
    return reply.tid;
}
BOOL gui(DWORD tid, PGUITHREADINFO out) {
    record("gui");
    if (!tid) { ++state.zeroGuiQueries; ++state.violations; }
    if (state.guiReads >= state.guis.size()) { ++state.violations; throw Injected{}; }
    const auto& reply = state.guis[state.guiReads++];
    if (tid != reply.tid || out->cbSize != sizeof(GUITHREADINFO)) ++state.violations;
    // Even a failed fake fills the structure to test unknown/partial-data handling.
    *out = reply.data;
    if (!reply.ok) { state.lastError = reply.failure; return FALSE; }
    return TRUE;
}
void reset() {
    state = State{};
    const HWND foregroundWindow = handle(0x1111);
    state.foregrounds = {foregroundWindow, foregroundWindow};
    state.windows = {{{foregroundWindow, 9992, 9988}, {state.caller, 9992, 9988}, {foregroundWindow, 9992, 9988}}};
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui); gui.flags = GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_CARETBLINKING;
    gui.hwndActive = handle(0x123456789ABCDEF0ULL); gui.hwndFocus = handle(0x3333);
    gui.hwndMenuOwner = handle(0x4444); gui.hwndCapture = handle(0x5555); gui.hwndCaret = handle(0x6666);
    state.guis = {{{9992, true, 0, gui}, {9992, true, 0, gui}}};
}
} // namespace fake_os

namespace fixture {
unsigned checks = 0;
Json cases = Json::array();
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
evidence::Snapshot capture() {
    const auto oldAttempts = allocation_probe::attempts;
    const DWORD oldError = fake_os::state.lastError;
    allocation_probe::forbidden = true;
    const auto snapshot = evidence::capture(fake_os::state.caller);
    allocation_probe::forbidden = false;
    check(allocation_probe::attempts == oldAttempts, "capture allocated");
    check(fake_os::state.lastError == oldError, "capture changed caller LastError");
    check(fake_os::state.zeroGuiQueries == 0, "queried unknown GUI TID as zero");
    check(fake_os::state.violations == 0, "fake call contract violation");
    return snapshot;
}
void scopes(const Json& row) {
    for (const char* key : {"crossQueryAtomicSnapshot", "continuousForegroundStabilityProven",
         "foregroundGrantProven", "foregroundLockStateProven", "taskViewVisibleProven", "windowHandlesValidated"}) {
        check(row.at(key).is_boolean() && !row.at(key).get<bool>(), "evidence overclaims authority/stability/visibility");
    }
}
void guiUnknown(const Json& gui) {
    check(!gui.at("ok").get<bool>(), "failed GUI query reported success");
    for (const char* key : {"flags", "hwndActive", "hwndFocus", "hwndMenuOwner", "hwndCapture", "hwndCaret"})
        check(gui.at(key).is_null(), "failed/absent GUI query published data");
}
void noPrivateValues(const Json& row) {
    if (row.is_object()) {
        for (auto it = row.begin(); it != row.end(); ++it) {
            const auto& key = it.key();
            check(key.find("title") == std::string::npos && key.find("Title") == std::string::npos &&
                key.find("path") == std::string::npos && key.find("Path") == std::string::npos &&
                key.find("class") == std::string::npos && key.find("Class") == std::string::npos,
                "privacy-sensitive key");
            if (key == "scope") check(it.value().is_string(), "scope metadata must be static text");
            else noPrivateValues(it.value());
        }
    } else check(!row.is_string() && !row.is_array(), "captured nonnumeric/nonboolean private value");
}
void normal() {
    fake_os::reset(); const auto snapshot = capture(); const auto row = evidence::toJson(snapshot);
    check(snapshot.captureComplete && !snapshot.captureException, "normal capture incomplete");
    check(snapshot.beginTick == 100 && snapshot.endTick == 110, "capture not bracketed");
    check(snapshot.callingThreadId == 10284 && snapshot.callerWindow.threadId == 9992, "worker/UI identity conflated");
    check(snapshot.foregroundStableKnown && snapshot.foregroundEndpointsStable, "matching endpoints not recognized");
    check(row.at("foregroundBefore").at("hwnd") == 0x1111 && row.at("foregroundBefore").at("processId") == 9988, "foreground identity missing");
    check(row.at("callerWindow").at("hwnd") == 0x2222 && row.at("callerWindow").at("threadId") == 9992, "caller UI identity missing");
    check(fake_os::state.foregroundReads == 2 && fake_os::state.windowReads == 3 && fake_os::state.guiReads == 2, "unbounded/omitted query");
    for (const char* key : {"foregroundThreadGui", "callerUiThreadGui"}) {
        const auto& gui = row.at(key);
        check(gui.at("attempted") == true && gui.at("ok") == true && gui.at("error").is_null(), "GUI success status wrong");
        check(gui.at("flags") == (GUI_INMENUMODE | GUI_POPUPMENUMODE | GUI_CARETBLINKING), "GUI flags lost");
        check(gui.at("hwndActive").is_number_unsigned() && gui.at("hwndActive").get<std::uint64_t>() == 0x123456789ABCDEF0ULL, "HWND narrowed/stringified");
        check(gui.at("hwndFocus") == 0x3333 && gui.at("hwndMenuOwner") == 0x4444 &&
            gui.at("hwndCapture") == 0x5555 && gui.at("hwndCaret") == 0x6666, "GUI handle lost");
    }
    scopes(row); noPrivateValues(row);
    check(Json::parse(row.dump()) == row, "JSON numeric roundtrip failed");
    cases.push_back("stable-endpoints-worker-ui-privacy");
}
void changed() {
    for (unsigned kind = 0; kind < 3; ++kind) {
        fake_os::reset();
        if (kind == 0) { fake_os::state.foregrounds[1] = fake_os::handle(0x7777); fake_os::state.windows[2].hwnd = fake_os::handle(0x7777); }
        if (kind == 1) fake_os::state.windows[2].pid = 8036;
        if (kind == 2) fake_os::state.windows[2].tid = 5556;
        const auto snapshot = capture(); const auto row = evidence::toJson(snapshot);
        check(snapshot.foregroundStableKnown && !snapshot.foregroundEndpointsStable, "changed HWND/PID/TID reported stable");
        check(row.at("foregroundEndpointsStable") == false, "known change became unknown");
    }
    cases.push_back("changed-hwnd-pid-tid");
}
void guiFailures() {
    for (unsigned mask = 1; mask < 4; ++mask) {
        fake_os::reset();
        for (unsigned i = 0; i < 2; ++i) if (mask & (1u << i)) { fake_os::state.guis[i].ok = false; fake_os::state.guis[i].failure = 1400 + i; }
        const auto snapshot = capture(); const auto row = evidence::toJson(snapshot);
        check(snapshot.captureComplete && snapshot.foregroundEndpointsStable, "GUI failure corrupted endpoint observation");
        unsigned i = 0;
        for (const char* key : {"foregroundThreadGui", "callerUiThreadGui"}) {
            const auto& gui = row.at(key);
            if (mask & (1u << i)) { guiUnknown(gui); check(gui.at("attempted") == true && gui.at("error") == 1400 + i, "GUI error overwritten"); }
            else check(gui.at("ok") == true, "independent GUI query lost");
            ++i;
        }
    }
    fake_os::reset(); fake_os::state.guis[0].ok = false; fake_os::state.guis[0].failure = 0;
    const auto row = evidence::toJson(capture());
    check(row.at("foregroundThreadGui").at("error") == 0 && row.at("foregroundThreadGui").at("ok") == false, "zero-error failure became success/unknown status");
    fake_os::reset(); fake_os::state.guis[0].data = GUITHREADINFO{};
    const auto zeros = evidence::toJson(capture()).at("foregroundThreadGui");
    check(zeros.at("ok") == true && zeros.at("flags") == 0 && zeros.at("hwndFocus") == 0, "observed zero confused with failed query");
    cases.push_back("gui-failures-errors-dirty-output-observed-zero");
}
void identityFailures() {
    for (unsigned which = 0; which < 3; ++which) {
        fake_os::reset();
        fake_os::state.windows[which].tid = 0; fake_os::state.windows[which].pid = 0xDEAD; fake_os::state.windows[which].failure = 6 + which;
        if (which == 0) fake_os::state.guis[0] = fake_os::state.guis[1];
        const auto snapshot = capture(); const auto row = evidence::toJson(snapshot);
        const char* key = which == 0 ? "foregroundBefore" : which == 1 ? "callerWindow" : "foregroundAfter";
        const auto& window = row.at(key);
        check(window.at("identityAttempted") == true && window.at("identityOk") == false, "failed identity status wrong");
        check(window.at("error") == 6 + which && window.at("processId").is_null() && window.at("threadId").is_null(), "dirty identity output leaked/error lost");
        if (which != 1) check(!snapshot.foregroundStableKnown && row.at("foregroundEndpointsStable").is_null(), "unknown identity became stable/change proof");
        else check(snapshot.foregroundStableKnown && snapshot.foregroundEndpointsStable, "caller failure corrupted independent foreground");
        if (which < 2) {
            const auto& gui = row.at(which == 0 ? "foregroundThreadGui" : "callerUiThreadGui");
            guiUnknown(gui); check(gui.at("attempted") == false && gui.at("threadId").is_null() && gui.at("error").is_null(), "unknown thread queried");
            check(fake_os::state.guiReads == 1, "skipped GUI identity still queried");
        }
    }
    fake_os::reset(); fake_os::state.windows[0].pid = 0;
    const auto partial = evidence::toJson(capture());
    check(partial.at("foregroundBefore").at("processId").is_null() && partial.at("foregroundBefore").at("threadId") == 9992, "missing PID mislabeled");
    check(partial.at("foregroundEndpointsStable").is_null(), "missing PID became stable proof");
    cases.push_back("identity-failures-unknown-gui-no-zero-tid");
}
void absentWindows() {
    fake_os::reset(); fake_os::state.foregrounds = {nullptr, nullptr};
    fake_os::state.windows[0] = fake_os::state.windows[1];
    auto row = evidence::toJson(capture());
    check(row.at("foregroundBefore").at("sampled") == true && row.at("foregroundBefore").at("hwnd") == 0, "null foreground not observed");
    check(row.at("foregroundEndpointsStable").is_null(), "two absent foregrounds reported stable");
    guiUnknown(row.at("foregroundThreadGui"));
    check(fake_os::state.windowReads == 1 && fake_os::state.guiReads == 1, "null foreground still queried");
    fake_os::reset(); fake_os::state.foregrounds = {nullptr, nullptr}; fake_os::state.caller = nullptr;
    row = evidence::toJson(capture());
    check(fake_os::state.windowReads == 0 && fake_os::state.guiReads == 0, "absent windows reached identity/GUI API");
    guiUnknown(row.at("callerUiThreadGui")); scopes(row); noPrivateValues(row);
    fake_os::reset(); fake_os::state.caller = nullptr; fake_os::state.windows[1] = fake_os::state.windows[2];
    const auto noCaller = capture();
    check(noCaller.foregroundEndpointsStable && !noCaller.callerUiThreadGui.attempted && fake_os::state.guiReads == 1, "absent caller corrupted foreground");
    fake_os::reset(); fake_os::state.foregrounds[0] = nullptr;
    fake_os::state.windows[0] = fake_os::state.windows[1]; fake_os::state.windows[1] = fake_os::state.windows[2];
    row = evidence::toJson(capture());
    check(row.at("foregroundBefore").at("hwnd") == 0 && row.at("foregroundAfter").at("hwnd") == 0x1111 && row.at("foregroundEndpointsStable").is_null(), "appearing foreground misclassified");
    fake_os::reset(); fake_os::state.foregrounds[1] = nullptr;
    row = evidence::toJson(capture());
    check(row.at("foregroundAfter").at("hwnd") == 0 && row.at("foregroundEndpointsStable").is_null(), "disappearing foreground misclassified");
    cases.push_back("no-foreground-no-caller-appearing-disappearing");
}
void exceptions() {
    // Fail every fixed query boundary, including both tick reads.
    for (int step = 0; step < 10; ++step) {
        fake_os::reset(); fake_os::state.throwAt = step;
        const auto snapshot = capture(); const auto row = evidence::toJson(snapshot);
        check(snapshot.captureException && !snapshot.captureComplete, "capture exception escaped/reported complete");
        check(!snapshot.foregroundStableKnown && row.at("foregroundEndpointsStable").is_null(), "partial capture claimed stability");
        if (step == 9) check(!snapshot.endTickKnown && row.at("captureEndTick").is_null(), "failed end tick published");
        else check(snapshot.endTickKnown, "partial capture lacks end tick");
        if (step == 0) check(!snapshot.beginTickKnown && row.at("captureBeginTick").is_null(), "failed begin tick published");
        scopes(row); noPrivateValues(row);
    }
    cases.push_back("all-query-boundaries-caught-last-error-restored");
}
void serializationFailure() {
    fake_os::reset(); const auto snapshot = capture(); bool caught = false;
    allocation_probe::forbidden = true;
    try { const auto row = evidence::toJson(snapshot); (void)row; }
    catch (const std::bad_alloc&) { caught = true; }
    catch (...) { allocation_probe::forbidden = false; throw; }
    allocation_probe::forbidden = false;
    check(caught, "toJson allocation failure must reach diagnostic caller");
    check(evidence::toJson(snapshot).at("captureComplete") == true, "serialization failure mutated scalar snapshot");
    cases.push_back("serialization-allocation-failure-caller-owned");
}
} // namespace fixture

int main() {
    try {
        fixture::normal(); fixture::changed(); fixture::guiFailures(); fixture::identityFailures();
        fixture::absentWindows(); fixture::exceptions(); fixture::serializationFailure();
        std::cout << Json{{"pass", true}, {"checks", fixture::checks}, {"cases", fixture::cases},
            {"captureAllocationAttempts", allocation_probe::attempts - 1},
            {"realUser32Executed", false}, {"realShellOrInputExecuted", false},
            {"fieldAcceptance", false}}.dump() << '\n';
        return 0;
    } catch (const std::exception& error) {
        allocation_probe::forbidden = false;
        std::cerr << error.what() << '\n'; return 1;
    }
}
