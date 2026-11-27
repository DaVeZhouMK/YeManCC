#pragma once
#include <windows.h>

// The admitted gesture's window, not the foreground at worker dispatch.
struct NativeSteamQamOrigin {
    enum class Source { Foreground, YmccGame, YmccIdle, YmccUnknown };
    HWND window=nullptr;
    DWORD windowPid=0, gamePid=0;
    ULONGLONG edgeTime=0, gameBirth=0, selection=0;
    ULONGLONG rules=0, power=0, noGameEpoch=0;
    Source source=Source::Foreground;
    const char* capture="foreground-window";
    HWND touchButton=nullptr;
    DWORD touchButtonPid=0;
    ULONGLONG touchButtonActivated=0,touchButtonLimit=0;
    ULONGLONG pageQuickAccessTick=0;

    const char* sourceName() const noexcept {
        switch(source){
        case Source::YmccGame:return "ymcc-selected-game";
        case Source::YmccIdle:return "ymcc-confirmed-idle";
        case Source::YmccUnknown:return "ymcc-target-unconfirmed";
        default:return "foreground-window";
        }
    }

    bool current() const noexcept {
        if(!window||!windowPid)return false;
        const HWND foreground=GetForegroundWindow();
        if(foreground!=window){
            DWORD buttonPid=0;
            const auto now=GetTickCount64();
            if(!touchButton||foreground!=touchButton||now<touchButtonActivated||
                now-touchButtonActivated>touchButtonLimit||
                !GetWindowThreadProcessId(touchButton,&buttonPid)||buttonPid!=touchButtonPid)return false;
        }
        DWORD pid=0;
        return GetWindowThreadProcessId(window,&pid)!=0&&pid==windowPid;
    }
};
