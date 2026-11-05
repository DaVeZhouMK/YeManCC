#pragma once
// Display-only HWND invariant. No hook, forwarding, synthetic input, or new timer.
#include <windows.h>
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
} // namespace ymcc::hudinput
