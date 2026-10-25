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
