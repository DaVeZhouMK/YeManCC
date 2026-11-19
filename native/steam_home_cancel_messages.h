#pragma once
#include <windows.h>
namespace ymcc::steamaction {
// The activation's UI thread consumes only its own cancellation messages.
// It does not run unrelated queued commands or a nested general message loop.
inline bool homeCancelPending(HWND splash)noexcept{
    MSG message{};bool cancelled=false;
    if(splash)while(PeekMessageW(&message,splash,WM_CLOSE,WM_CLOSE,PM_REMOVE))cancelled=true;
    while(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE))cancelled=true;
    return cancelled;
}
}
