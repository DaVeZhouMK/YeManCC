#pragma once
#include "gesture_summon_core.h"
#include "screen_touchpad_input_surface.h"
#include <atomic>

namespace ymcc::gesturesummon {
// Inactive: edge touch input can interfere with Windows functionality.
// Retain the implementation for future evaluation; saved settings cannot enable it.
inline constexpr bool kFeatureEnabled = false;
inline constexpr const char* kDisabledReason = "影响 Windows 功能";
inline constexpr UINT kRefreshMessage = WM_APP + 181;
inline constexpr UINT kSummonMessage = WM_APP + 182;
// UI-thread owned. The narrow alpha-zero surface consumes Touch/Pen only;
// the existing controlled-input bridge passes ordinary mouse input through.
class EdgeSurface {
    HWND owner_ = nullptr, window_ = nullptr;
    screeninput::Runtime runtime_;
    screeninput::Surface input_;
    Recognizer recognizer_;
    RECT bounds_{};
    bool left_ = false;
    UINT dpi_ = 96;
    UINT_PTR generation_ = 0;
    std::atomic<bool> enabled_{false}, available_{true}, active_{false};
    std::atomic<HRESULT> error_{S_OK};
    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        auto* self = reinterpret_cast<EdgeSurface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<EdgeSurface*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (message == WM_POINTERACTIVATE) return PA_NOACTIVATE;
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) { PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); return 0; }
        if (message == WM_CANCELMODE && self) self->cancel();
        return DefWindowProcW(hwnd, message, w, l);
    }
    void cancel() noexcept { recognizer_.reset(left_, dpi_ / 96.0f); }
    static void sink(HWND, const screeninput::Event& event, void* context) {
        static_cast<EdgeSurface*>(context)->consume(event);
    }
    void consume(const screeninput::Event& event) noexcept {
        if (!active_.load() || !owner_ || ownerInForeground()) { cancel(); return; }
        const auto phase = event.cancelled ? Phase::Cancel : event.phase == screeninput::Phase::Down ? Phase::Down :
            event.phase == screeninput::Phase::Move ? Phase::Move : event.phase == screeninput::Phase::Up ? Phase::Up : Phase::Cancel;
        // Touch retains implicit contact capture beyond the narrow child.
        // Explicit CorePointer capture would also intercept ordinary mouse input.
        const bool summon = recognizer_.event(event.id, phase, event.x, event.y,
            event.timeKnown ? event.sourceTime : GetTickCount(), event.type == PT_TOUCH);
        if (phase == Phase::Cancel) { if (!recognizer_.hasContacts()) cancel(); return; }
        if (phase == Phase::Up) {
            if (!recognizer_.hasContacts()) cancel();
        }
        if (summon && owner_ && enabled_.load()) PostMessageW(owner_, kSummonMessage, generation_, 0);
    }
    void closeSurface() noexcept {
        ++generation_; active_ = false;
        cancel();
        if (window_) ShowWindow(window_, SW_HIDE);
        input_.close();
        if (window_) DestroyWindow(window_);
        window_ = nullptr; bounds_ = {};
    }
public:
    bool ownerInForeground() const noexcept {
        if (!owner_ || !IsWindowVisible(owner_) || IsIconic(owner_)) return false;
        const HWND foreground = GetForegroundWindow();
        return foreground && (foreground == owner_ || GetAncestor(foreground, GA_ROOTOWNER) == owner_);
    }
    bool left() const noexcept { return left_; }
    bool accepts(UINT_PTR generation) const noexcept { return kFeatureEnabled && generation == generation_ && active_.load() && enabled_.load(); }
    // Preserve the status field for older frontends; Windows gestures stay enabled.
    bool edgeHintApplied() const noexcept { return false; }
    bool enabled() const noexcept { return enabled_.load(); }
    bool available() const noexcept { return available_.load(); }
    bool active() const noexcept { return active_.load(); }
    HRESULT error() const noexcept { return error_.load(); }
    void shutdown() noexcept { enabled_ = false; closeSurface(); owner_ = nullptr; }
    bool refresh(HWND owner, bool enabled, bool left, bool force = false) noexcept {
        owner_ = owner;
        enabled = enabled && kFeatureEnabled;
        const bool wasEnabled = enabled_.exchange(enabled);
        if (!enabled || !wasEnabled) { available_ = true; error_ = S_OK; }
        if (!enabled || !owner || ownerInForeground()) {
            closeSurface(); return true;
        }
        MONITORINFO monitor{sizeof(monitor)};
        if (!GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitor)) {
            closeSurface(); available_ = false; error_ = HRESULT_FROM_WIN32(GetLastError()); return false;
        }
        const UINT dpi = GetDpiForWindow(owner) ? GetDpiForWindow(owner) : 96;
        const int width = MulDiv(12, dpi, 96);
        const auto strip=centeredEdge(monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right,monitor.rcMonitor.bottom,width,left);
        const RECT desired{strip.left,strip.top,strip.right,strip.bottom};
        if (!force && available_.load() && window_ && input_.active() && left == left_ && dpi == dpi_ && EqualRect(&desired, &bounds_)) return true;
        closeSurface(); left_ = left; dpi_ = dpi; bounds_ = desired; recognizer_.reset(left_, dpi / 96.0f);
        WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"YeManGestureSummonEdge"; cls.lpfnWndProc = proc;
        if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            available_ = false; error_ = HRESULT_FROM_WIN32(GetLastError()); return false;
        }
        // Keep both root and input child inside the narrow edge strip.
        // Do not request shell edge-gesture suppression or a fullscreen root.
        // An unowned tool window stays visible when the main window is hidden.
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
            cls.lpszClassName, L"", WS_POPUP, desired.left, desired.top,
            desired.right - desired.left, desired.bottom - desired.top, nullptr, nullptr, cls.hInstance, this);
        if (!window_) { available_ = false; error_ = HRESULT_FROM_WIN32(GetLastError()); return false; }
        if (!input_.attach(runtime_, window_, 0, sink, this)) {
            const HRESULT failure = input_.error; closeSurface(); available_ = false; error_ = failure; return false;
        }
        active_ = true; available_ = true; error_ = S_OK;
        ShowWindow(window_, SW_SHOWNOACTIVATE);
        SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        try {
            // Finish the initial input binding after showing the narrow root.
            // Drain only this input channel once; never poll while idle.
            auto channel=input_.source();
            channel.Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
        } catch (const winrt::hresult_error& e) {
            const HRESULT failure=e.code(); closeSurface(); available_=false; error_=failure; return false;
        } catch (...) { closeSurface(); available_=false; error_=E_FAIL; return false; }
        return true;
    }
};
} // namespace ymcc::gesturesummon
