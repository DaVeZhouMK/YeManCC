#pragma once
#include <windows.h>
#include <cstdint>
#include "json.hpp"

// Sparse request-boundary diagnostics only. No authorization, activation, input,
// hooks, polling, title/path reads, or visibility test. The owner supplies its HWND.
// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getguithreadinfo
// https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid
namespace ymcc::taskViewForeground {
struct WindowSnapshot {
    HWND hwnd = nullptr;
    DWORD processId = 0, threadId = 0, error = 0;
    bool sampled = false, identityAttempted = false, identityOk = false, errorKnown = false;
};
struct GuiSnapshot {
    DWORD threadId = 0, error = 0, flags = 0;
    HWND hwndActive = nullptr, hwndFocus = nullptr, hwndMenuOwner = nullptr;
    HWND hwndCapture = nullptr, hwndCaret = nullptr;
    bool attempted = false, ok = false, errorKnown = false;
};
struct Snapshot {
    ULONGLONG beginTick = 0, endTick = 0;
    DWORD callingThreadId = 0;
    WindowSnapshot foregroundBefore, foregroundAfter, callerWindow;
    GuiSnapshot foregroundThreadGui, callerUiThreadGui;
    bool beginTickKnown = false, endTickKnown = false;
    bool captureComplete = false, captureException = false;
    bool foregroundStableKnown = false, foregroundEndpointsStable = false;
};
namespace detail {
// Helpers may propagate a C++ exception from a test adapter into capture's guard.
// Only scalar members are written; failed APIs never publish partially filled data.
inline void readWindow(HWND hwnd, WindowSnapshot& out) {
    out.hwnd = hwnd;
    out.sampled = true;
    if (!hwnd) return;
    out.identityAttempted = true;
    DWORD pid = 0;
    SetLastError(ERROR_SUCCESS);
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    if (!tid) {
        out.error = GetLastError();
        out.errorKnown = true;
        return;
    }
    out.threadId = tid;
    out.processId = pid;
    out.identityOk = true;
}
inline void readGui(const WindowSnapshot& window, GuiSnapshot& out) {
    if (!window.identityOk || !window.threadId) return;
    out.threadId = window.threadId;
    out.attempted = true;
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    SetLastError(ERROR_SUCCESS);
    // idThread=0 means "foreground thread" in User32. Never substitute it for
    // an unknown caller/foreground TID or silently change the query's subject.
    if (!GetGUIThreadInfo(out.threadId, &gui)) {
        out.error = GetLastError();
        out.errorKnown = true;
        return;
    }
    out.flags = gui.flags;
    out.hwndActive = gui.hwndActive;
    out.hwndFocus = gui.hwndFocus;
    out.hwndMenuOwner = gui.hwndMenuOwner;
    out.hwndCapture = gui.hwndCapture;
    out.hwndCaret = gui.hwndCaret;
    out.ok = true;
}
inline std::uint64_t hwndNumber(HWND hwnd) noexcept {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hwnd));
}
inline nlohmann::json windowJson(const WindowSnapshot& window) {
    using Json = nlohmann::json;
    return {{"sampled", window.sampled},
        {"hwnd", window.sampled ? Json(hwndNumber(window.hwnd)) : Json(nullptr)},
        {"identityAttempted", window.identityAttempted}, {"identityOk", window.identityOk},
        {"error", window.errorKnown ? Json(window.error) : Json(nullptr)},
        {"processId", window.identityOk && window.processId ? Json(window.processId) : Json(nullptr)},
        {"threadId", window.identityOk && window.threadId ? Json(window.threadId) : Json(nullptr)}};
}
inline nlohmann::json guiJson(const GuiSnapshot& gui) {
    using Json = nlohmann::json;
    return {{"threadId", gui.threadId ? Json(gui.threadId) : Json(nullptr)},
        {"attempted", gui.attempted}, {"ok", gui.ok},
        {"error", gui.errorKnown ? Json(gui.error) : Json(nullptr)},
        {"flags", gui.ok ? Json(gui.flags) : Json(nullptr)},
        {"hwndActive", gui.ok ? Json(hwndNumber(gui.hwndActive)) : Json(nullptr)},
        {"hwndFocus", gui.ok ? Json(hwndNumber(gui.hwndFocus)) : Json(nullptr)},
        {"hwndMenuOwner", gui.ok ? Json(hwndNumber(gui.hwndMenuOwner)) : Json(nullptr)},
        {"hwndCapture", gui.ok ? Json(hwndNumber(gui.hwndCapture)) : Json(nullptr)},
        {"hwndCaret", gui.ok ? Json(hwndNumber(gui.hwndCaret)) : Json(nullptr)}};
}
} // namespace detail

inline Snapshot capture(HWND callerWindow) noexcept {
    const DWORD savedError = GetLastError();
    Snapshot snapshot;
    try {
        snapshot.beginTick = GetTickCount64();
        snapshot.beginTickKnown = true;
        snapshot.callingThreadId = GetCurrentThreadId();
        detail::readWindow(GetForegroundWindow(), snapshot.foregroundBefore);
        detail::readWindow(callerWindow, snapshot.callerWindow);
        detail::readGui(snapshot.foregroundBefore, snapshot.foregroundThreadGui);
        detail::readGui(snapshot.callerWindow, snapshot.callerUiThreadGui);
        detail::readWindow(GetForegroundWindow(), snapshot.foregroundAfter);
        snapshot.captureComplete = true;
    } catch (...) {
        snapshot.captureException = true;
    }
    // Also bracket a partial/exceptional capture, without retrying any failed query.
    try {
        snapshot.endTick = GetTickCount64();
        snapshot.endTickKnown = true;
    } catch (...) {
        snapshot.captureException = true;
    }
    snapshot.captureComplete = snapshot.captureComplete && snapshot.endTickKnown;
    const auto& before = snapshot.foregroundBefore;
    const auto& after = snapshot.foregroundAfter;
    snapshot.foregroundStableKnown = snapshot.captureComplete && before.hwnd && after.hwnd &&
        before.identityOk && after.identityOk && before.processId && after.processId;
    snapshot.foregroundEndpointsStable = snapshot.foregroundStableKnown && before.hwnd == after.hwnd &&
        before.processId == after.processId && before.threadId == after.threadId;
    SetLastError(savedError);
    return snapshot;
}

// Allocation/serialization may throw; the existing diagnostic caller owns catch.
inline nlohmann::json toJson(const Snapshot& snapshot) {
    using Json = nlohmann::json;
    return {{"schemaVersion", 1},
        {"captureBeginTick", snapshot.beginTickKnown ? Json(snapshot.beginTick) : Json(nullptr)},
        {"captureEndTick", snapshot.endTickKnown ? Json(snapshot.endTick) : Json(nullptr)},
        {"callingThreadId", snapshot.callingThreadId ? Json(snapshot.callingThreadId) : Json(nullptr)},
        {"captureComplete", snapshot.captureComplete}, {"captureException", snapshot.captureException},
        {"foregroundBefore", detail::windowJson(snapshot.foregroundBefore)},
        {"foregroundAfter", detail::windowJson(snapshot.foregroundAfter)},
        {"callerWindow", detail::windowJson(snapshot.callerWindow)},
        {"foregroundThreadGui", detail::guiJson(snapshot.foregroundThreadGui)},
        {"callerUiThreadGui", detail::guiJson(snapshot.callerUiThreadGui)},
        {"foregroundStableKnown", snapshot.foregroundStableKnown},
        {"foregroundEndpointsStable", snapshot.foregroundStableKnown ? Json(snapshot.foregroundEndpointsStable) : Json(nullptr)},
        {"crossQueryAtomicSnapshot", false}, {"continuousForegroundStabilityProven", false},
        {"foregroundGrantProven", false}, {"foregroundLockStateProven", false},
        {"taskViewVisibleProven", false}, {"windowHandlesValidated", false},
        {"scope", "Sequential numeric query observations only. Matching foreground endpoints do not prove continuous stability. GUI flags and handles prove neither foreground authorization/lock state nor TaskView visibility; handles may become stale. Unknown TIDs are never queried as zero."}};
}
} // namespace ymcc::taskViewForeground
