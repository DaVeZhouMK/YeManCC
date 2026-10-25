#pragma once
// Visual-only layers may be disabled. Controlled Touch/Pen hosts MUST NOT use
// this policy: disabling those would remove the touch input they are keeping.
#include <windows.h>
namespace ymcc::screeninput {
inline constexpr LONG_PTR displayExStyle=WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;
inline bool displayInert(HWND window) noexcept {
    return window&&IsWindow(window)&&!IsWindowEnabled(window)&&
        (GetWindowLongPtrW(window,GWL_EXSTYLE)&displayExStyle)==displayExStyle;
}
inline bool ensureDisplay(HWND window) noexcept {
    if(!window||!IsWindow(window))return false;
    const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    if((style&displayExStyle)!=displayExStyle){
        SetLastError(ERROR_SUCCESS);
        if(!SetWindowLongPtrW(window,GWL_EXSTYLE,style|displayExStyle)&&GetLastError()!=ERROR_SUCCESS)return false;
    }
    if(IsWindowEnabled(window))EnableWindow(window,FALSE);
    // Preserve the uploaded per-pixel bitmap. SetLayeredWindowAttributes is NOT
    // interchangeable with UpdateLayeredWindow and must not be applied here.
    return displayInert(window);
}
} // namespace ymcc::screeninput
