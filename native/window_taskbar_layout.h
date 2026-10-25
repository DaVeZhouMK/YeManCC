#pragma once
#include <algorithm>
#include <cwchar>

// The host supplies Win32 declarations. This policy is also compiled with
// deterministic Win32 fixtures; it never changes the taskbar or foreground.
namespace ymcc::windowlayout {
struct TaskbarProbe {
    HMONITOR monitor;
    RECT monitorRect;
    RECT workArea;
    RECT safeArea;
    bool found = false;
    bool uncertain = false;
    bool visible = false;
};

inline BOOL CALLBACK collectTaskbar(HWND hwnd, LPARAM context) {
    wchar_t name[64]{};
    if (!GetClassNameW(hwnd, name, 64) ||
        (_wcsicmp(name, L"Shell_TrayWnd") != 0 &&
         _wcsicmp(name, L"Shell_SecondaryTrayWnd") != 0)) return TRUE;
    auto& probe = *reinterpret_cast<TaskbarProbe*>(context);
    if (MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != probe.monitor) return TRUE;
    probe.found = true;
    if (!IsWindowVisible(hwnd)) return TRUE;
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) {
        probe.uncertain = true;
        return TRUE;
    }
    const auto& screen = probe.monitorRect;
    const LONG left = (std::max)(rect.left, screen.left);
    const LONG top = (std::max)(rect.top, screen.top);
    const LONG right = (std::min)(rect.right, screen.right);
    const LONG bottom = (std::min)(rect.bottom, screen.bottom);
    // Auto-hide retains WS_VISIBLE and may leave a 1-2 physical-pixel
    // activation strip. The setting alone does NOT mean it is currently hidden.
    if (right <= left || bottom <= top) return TRUE;
    const bool horizontal = rect.right - rect.left >= rect.bottom - rect.top;
    if ((horizontal ? bottom - top : right - left) <= 2) return TRUE;
    probe.visible = true;
    // rcWork is authoritative when this edge already reserves a taskbar.
    // On Windows 11, Shell_TrayWnd can include transparent padding ABOVE the
    // visible bar (e.g. 144px window vs. 96px reservation at 200% DPI). Clipping
    // that rectangle again incorrectly leaves a 48px desktop gap below YMCC.
    // Use live bounds only for an unreserved/activation-strip edge, such as an
    // expanded auto-hide bar or Explorer before it publishes the work area.
    const auto& work = probe.workArea;
    if (horizontal) {
        if (top - screen.top <= screen.bottom - bottom) {
            if (work.top - screen.top <= 2)
                probe.safeArea.top = (std::max)(probe.safeArea.top, bottom);
        } else if (screen.bottom - work.bottom <= 2) {
            probe.safeArea.bottom = (std::min)(probe.safeArea.bottom, top);
        }
    } else {
        if (left - screen.left <= screen.right - right) {
            if (work.left - screen.left <= 2)
                probe.safeArea.left = (std::max)(probe.safeArea.left, right);
        } else if (screen.right - work.right <= 2) {
            probe.safeArea.right = (std::min)(probe.safeArea.right, left);
        }
    }
    return TRUE;
}

inline bool safeMonitorArea(HMONITOR targetMonitor, RECT& area) {
    MONITORINFO mi{sizeof(mi)};
    if (!targetMonitor || !GetMonitorInfoW(targetMonitor, &mi)) return false;
    TaskbarProbe probe{targetMonitor, mi.rcMonitor, mi.rcWork, mi.rcWork};
    const bool enumerated = EnumWindows(collectTaskbar, reinterpret_cast<LPARAM>(&probe)) != FALSE;
    // Unknown/failed observations retain the conservative work area. A saved
    // fullscreen game or the primary monitor's auto-hide flag cannot override
    // a visible taskbar on this monitor.
    if (enumerated && probe.found && !probe.visible && !probe.uncertain) {
        probe.safeArea.top = mi.rcMonitor.top;
        probe.safeArea.bottom = mi.rcMonitor.bottom;
    }
    area = probe.safeArea;
    return area.right > area.left && area.bottom > area.top;
}

inline bool sameRect(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

struct TaskbarRecheck {
    static constexpr UINT intervalMs = 200;
    static constexpr unsigned passes = 6;
    unsigned remaining = 0;
    void arm() { remaining = passes; }
    void cancel() { remaining = 0; }
    bool active() const { return remaining != 0; }
    bool finishPass() { return remaining != 0 && --remaining != 0; }
};
} // namespace ymcc::windowlayout
