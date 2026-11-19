#pragma once
// Display-only HWND invariant. No hook, forwarding, synthetic input, or new timer.
#include <windows.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
namespace ymcc::hudinput {
inline constexpr LONG_PTR requiredExStyle = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
// Preserve the invariant BEFORE a style mutation becomes visible to hit testing.
// WM_STYLECHANGED remains the fallback for EnableWindow and other state changes.
inline bool guardStyle(WPARAM which, LPARAM parameter) {
    if (!parameter) return false;
    auto* change = reinterpret_cast<STYLESTRUCT*>(parameter);
    if (static_cast<int>(which) == GWL_EXSTYLE) change->styleNew |= static_cast<DWORD>(requiredExStyle);
    else if (static_cast<int>(which) == GWL_STYLE) change->styleNew |= WS_DISABLED;
    else return false;
    return true;
}
// Mouse hit tests and touch proximity targeting are separate Windows paths.
// An edge-origin drag may already belong to the game; a new tap needs this
// explicit rejection so the visible display rectangle cannot be a touch target.
inline bool registerTouchPassthrough(HWND hwnd) {
    return hwnd && RegisterTouchHitTestingWindow(hwnd, TOUCH_HIT_TESTING_CLIENT) != FALSE;
}
inline LRESULT rejectTouchHitTest(LPARAM parameter) {
    if (!parameter) return 0;
    const auto* input = reinterpret_cast<const TOUCH_HIT_TESTING_INPUT*>(parameter);
    const TOUCH_HIT_TESTING_PROXIMITY_EVALUATION rejected{TOUCH_HIT_TESTING_PROXIMITY_FARTHEST, input->point};
    return PackTouchHitTestingProximityEvaluation(input, &rejected);
}
// The crosshair renderer keeps screen coordinates, but its HWND must not
// retain a full-screen native region. Only the HUD pixels and the small,
// display-only crosshair glyph exist in the window region. Input remains on
// the separate panel-sized input HWND; this disabled display never owns it.
inline bool constrainDisplayRegion(HWND hwnd,const RECT& output,const RECT& panel,bool crosshair) {
    if (!hwnd || output.right <= output.left || output.bottom <= output.top) return false;
    if (!crosshair) return SetWindowRgn(hwnd, nullptr, FALSE) != 0;
    HRGN region = CreateRectRgn(panel.left-output.left, panel.top-output.top,
                               panel.right-output.left, panel.bottom-output.top);
    if (!region) return false;
    // The renderer is fixed to raw pixels / rasterization 1.0. Its 24px SVG
    // and outline fit inside this 32px region, including odd screen sizes.
    const LONG x=(output.right-output.left-32)/2,y=(output.bottom-output.top-32)/2;
    HRGN glyph = CreateRectRgn(x, y, x+32, y+32);
    const bool combined = glyph && CombineRgn(region, region, glyph, RGN_OR) != ERROR;
    if (glyph) DeleteObject(glyph);
    if (combined && SetWindowRgn(hwnd, region, FALSE)) return true; // Windows owns it.
    DeleteObject(region);
    return false;
}
inline bool inert(HWND hwnd) {
    return hwnd && IsWindow(hwnd) && !IsWindowEnabled(hwnd) &&
           (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & requiredExStyle) == requiredExStyle;
}
inline bool ensure(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    const auto before = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((before & requiredExStyle) != requiredExStyle) {
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(hwnd, GWL_EXSTYLE, before | requiredExStyle) && GetLastError() != ERROR_SUCCESS) return false;
    }
    // A disabled top-level display is excluded from normal mouse targeting.
    // HTTRANSPARENT alone is only a same-thread hit-test fallback, not the guarantee.
    if (IsWindowEnabled(hwnd)) EnableWindow(hwnd, FALSE);
    BYTE alpha = 0; DWORD flags = 0; COLORREF key = 0;
    if (!GetLayeredWindowAttributes(hwnd, &key, &alpha, &flags) || alpha != 255 || flags != LWA_ALPHA)
        if (!SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA)) return false;
    return inert(hwnd);
}
// Composition WebViews still create native helper children. Disabling the
// host does not set WS_DISABLED/WS_EX_TRANSPARENT on those HWNDs themselves.
// Keep the entire display tree inert, independently of the panel input HWND.
inline constexpr LONG_PTR displayChildExStyle = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
inline bool protectDisplayChild(HWND root, HWND child);
inline LRESULT CALLBACK displayChildProc(HWND child, UINT message, WPARAM w, LPARAM l,
                                         UINT_PTR id, DWORD_PTR rootValue) {
    const HWND root = reinterpret_cast<HWND>(rootValue);
    if (message == WM_NCDESTROY || !root || !IsWindow(root) || !IsChild(root, child)) {
        RemoveWindowSubclass(child, displayChildProc, id);
        return DefSubclassProc(child, message, w, l);
    }
    if (message == WM_STYLECHANGING && l) {
        auto* change = reinterpret_cast<STYLESTRUCT*>(l);
        if (static_cast<int>(w) == GWL_EXSTYLE) change->styleNew |= static_cast<DWORD>(displayChildExStyle);
        else if (static_cast<int>(w) == GWL_STYLE) change->styleNew |= WS_DISABLED;
    }
    if (message == WM_ENABLE && w) EnableWindow(child, FALSE);
    if (message == WM_PARENTNOTIFY && LOWORD(w) == WM_CREATE)
        protectDisplayChild(root, reinterpret_cast<HWND>(l));
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_TOUCHHITTESTING) return rejectTouchHitTest(l);
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_POINTERACTIVATE) return PA_NOACTIVATE;
    return DefSubclassProc(child, message, w, l);
}
inline bool protectDisplayChild(HWND root, HWND child) {
    if (!root || !child || !IsWindow(child) || !IsChild(root, child)) return false;
    DWORD pid = 0;
    const DWORD thread = GetWindowThreadProcessId(child, &pid);
    // Never synchronously send a style/enable mutation into another thread or
    // browser process. An unprotected foreign helper denies display admission.
    if (pid != GetCurrentProcessId() || thread != GetCurrentThreadId())
        return !IsWindowEnabled(child) &&
               (GetWindowLongPtrW(child, GWL_EXSTYLE) & displayChildExStyle) == displayChildExStyle;
    {
        DWORD_PTR existing = 0;
        constexpr UINT_PTR id = 0x594D4844;
        if (!GetWindowSubclass(child, displayChildProc, id, &existing)) {
            if (!SetWindowSubclass(child, displayChildProc, id, reinterpret_cast<DWORD_PTR>(root))) return false;
        } else if (existing != reinterpret_cast<DWORD_PTR>(root)) return false;
    }
    const auto style = GetWindowLongPtrW(child, GWL_EXSTYLE);
    if ((style & displayChildExStyle) != displayChildExStyle) {
        // Subclass callbacks may change last-error during a successful style
        // notification. Verify the actual flags instead of misreading old=0.
        SetWindowLongPtrW(child, GWL_EXSTYLE, style | displayChildExStyle);
        if ((GetWindowLongPtrW(child, GWL_EXSTYLE) & displayChildExStyle) != displayChildExStyle) return false;
    }
    if (IsWindowEnabled(child)) EnableWindow(child, FALSE);
    return !IsWindowEnabled(child) &&
           (GetWindowLongPtrW(child, GWL_EXSTYLE) & displayChildExStyle) == displayChildExStyle;
}
inline bool ensureDisplayTree(HWND root) {
    if (!ensure(root)) return false;
    struct Check { HWND root; bool ok = true; } check{root};
    EnumChildWindows(root, [](HWND child, LPARAM data) -> BOOL {
        auto& check = *reinterpret_cast<Check*>(data);
        if (!protectDisplayChild(check.root, child)) { check.ok = false; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&check));
    return check.ok;
}
} // namespace ymcc::hudinput
