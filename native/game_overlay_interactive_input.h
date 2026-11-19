#pragma once
// Bounded interactive UI window. No foreground acquisition, input hook, thread,
// capture manager, or device changes. Only the visible UI rectangle is hittable.
#include <windows.h>
namespace ymcc::gameoverlayinput {
inline constexpr LONG_PTR requiredEx=WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;
inline bool guardStyle(WPARAM which,LPARAM parameter){
    if(!parameter)return false;
    auto* change=reinterpret_cast<STYLESTRUCT*>(parameter);
    if(static_cast<int>(which)==GWL_EXSTYLE){change->styleNew|=static_cast<DWORD>(requiredEx);change->styleNew&=~WS_EX_TRANSPARENT;return true;}
    if(static_cast<int>(which)==GWL_STYLE){change->styleNew&=~WS_DISABLED;return true;}
    return false;
}
inline bool ready(HWND window){return window&&IsWindow(window)&&IsWindowEnabled(window)&&
    (GetWindowLongPtrW(window,GWL_EXSTYLE)&(requiredEx|WS_EX_TRANSPARENT))==requiredEx;}
inline bool ensure(HWND window){
    if(!window||!IsWindow(window))return false;
    const LONG_PTR before=GetWindowLongPtrW(window,GWL_EXSTYLE);
    const LONG_PTR desired=(before|requiredEx)&~WS_EX_TRANSPARENT;
    if(before!=desired){SetWindowLongPtrW(window,GWL_EXSTYLE,desired);if(GetWindowLongPtrW(window,GWL_EXSTYLE)!=desired)return false;}
    if(!IsWindowEnabled(window))EnableWindow(window,TRUE);
    BYTE alpha=0;DWORD flags=0;COLORREF key=0;
    if(!GetLayeredWindowAttributes(window,&key,&alpha,&flags)||alpha!=255||flags!=LWA_ALPHA)
        if(!SetLayeredWindowAttributes(window,0,255,LWA_ALPHA))return false;
    return ready(window);
}
}
