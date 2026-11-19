#pragma once
// TaskView action-only Windows adapter. Never polled by a background owner.
// The taskView async request supplies the immutable power/request context.
struct TaskViewKeyboardOps {
    const ULONGLONG power;
    const std::uint64_t focusToken=0;
    // Reuse the original decision reads; no extra key queries or key releases.
    // This is the first blocker of the latest check, not full key/source coverage.
    mutable WORD blockingVirtualKey=0;
    mutable UINT keyStateChecks=0;
    ULONGLONG now() const noexcept{return GetTickCount64();}
    bool current() const noexcept {
        return (!focusToken||g_taskViewFocusToken.load(std::memory_order_acquire)==focusToken) &&
            power==currentPowerGeneration() && !g_exitRequested.load(std::memory_order_acquire) &&
            g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready;
    }
    DWORD error()const noexcept{return GetLastError();}
    void setError(DWORD value)const noexcept{SetLastError(value);}
    bool interactive()const noexcept {
        if(!current())return false;
        DWORD session=0;
        if(!ProcessIdToSessionId(GetCurrentProcessId(),&session))return false;
        LPWSTR stateData=nullptr;DWORD stateBytes=0;
        const BOOL queried=WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,session,
            WTSConnectState,&stateData,&stateBytes);
        const bool active=queried&&stateData&&stateBytes==sizeof(WTS_CONNECTSTATE_CLASS)&&
            *reinterpret_cast<const WTS_CONNECTSTATE_CLASS*>(stateData)==WTSActive;
        if(stateData)WTSFreeMemory(stateData);
        if(!active||!current())return false;
        const HDESK input=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
        if(!input)return false;
        wchar_t desktop[64]{},thread[64]{},station[64]{};DWORD needed=0;BOOL receiving=FALSE;
        const bool normal=GetUserObjectInformationW(input,UOI_NAME,desktop,sizeof(desktop),&needed) &&
            GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,thread,sizeof(thread),&needed) &&
            GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station,sizeof(station),&needed) &&
            _wcsicmp(desktop,L"Default")==0 && _wcsicmp(thread,L"Default")==0 && _wcsicmp(station,L"WinSta0")==0 &&
            GetUserObjectInformationW(input,UOI_IO,&receiving,sizeof(receiving),&needed) && receiving;
        const auto failure=normal?ERROR_SUCCESS:GetLastError();CloseDesktop(input);SetLastError(failure);
        // Session/desktop/key sampling cannot make the later send atomic.
        return normal&&current();
    }
    bool keysClear()const noexcept {
        ++keyStateChecks;blockingVirtualKey=0;
        // Do not turn a user's Shift/Ctrl/Alt/Win into another shortcut or
        // synthesize releases for keys YMCC did not press.
        for(const int key:{VK_LCONTROL,VK_RCONTROL,VK_LSHIFT,VK_RSHIFT,VK_LMENU,VK_RMENU,VK_LWIN,VK_RWIN,VK_TAB})
            if((static_cast<USHORT>(GetAsyncKeyState(key))&0x8000u)!=0){blockingVirtualKey=static_cast<WORD>(key);return false;}
        return true;
    }
    UINT send(const INPUT* inputs,UINT count)const noexcept {
        return SendInput(count,const_cast<INPUT*>(inputs),sizeof(INPUT));
    }
    void observe(const INPUT* inputs,UINT requested,UINT inserted)const noexcept {
        keyboardModifierObserveSelfSend(ymcc::keyboardEvents::SourceId::TaskView,inputs,requested,inserted);
    }
    void pause(DWORD milliseconds)const noexcept{Sleep(milliseconds);}
};
