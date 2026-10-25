#pragma once
// Event-driven display-stack maintenance shared in spirit with the monitor HUD.
// No process scanning, data cadence, periodic timer, input forwarding or focus
// manipulation. Every operation runs on the existing HWND owner's thread.
namespace ymcc::screenpads {
inline constexpr UINT kOwnerVisibilityMessage=WM_APP+177;
inline std::array<HWINEVENTHOOK,5> ownerVisibilityHooks{};
inline UINT_PTR ownerVisibilityEpoch=1;
inline bool ownerVisibilityQueued=false;
inline unsigned long long ownerVisibilityEvents=0,ownerVisibilityChecks=0,ownerVisibilityRaises=0;
inline bool ownerVisibilityActive(){return ownerVisibilityHooks[0]!=nullptr;}
inline bool visibleScreenControls(){
    for(HWND h:windows)if(h&&IsWindowVisible(h))return true;
    return buttonsVisible();
}
inline void stopOwnerVisibility(){
    ++ownerVisibilityEpoch;ownerVisibilityQueued=false;
    for(auto& hook:ownerVisibilityHooks){if(hook)UnhookWinEvent(hook);hook=nullptr;}
}
inline bool queueOwnerVisibility(){
    const HWND h=owner.load();
    if(!initialized||!ownerVisibilityActive()||!onOwnerThread(h)||!visibleScreenControls())return false;
    if(ownerVisibilityQueued)return true;
    ownerVisibilityQueued=true;
    if(PostMessageW(h,kOwnerVisibilityMessage,ownerVisibilityEpoch,0))return true;
    ownerVisibilityQueued=false;return false;
}
inline void CALLBACK screenVisibilityObserved(HWINEVENTHOOK,DWORD event,HWND h,LONG object,LONG child,DWORD,DWORD){
    if(!h||child!=CHILDID_SELF||!initialized||!ownerVisibilityActive())return;
    // Actual top-level reorders are also delivered on the desktop CLIENT
    // object, not just OBJID_WINDOW on the game. Do not discard that event.
    const bool desktopOrder=event==EVENT_OBJECT_REORDER&&(h==GetDesktopWindow()||h==GetShellWindow())&&
        (object==OBJID_WINDOW||object==OBJID_CLIENT);
    if(!desktopOrder&&object!=OBJID_WINDOW)return;
    const HWND foreground=GetForegroundWindow(),root=foreground?GetAncestor(foreground,GA_ROOT):nullptr;
    if(h!=foreground&&h!=root&&!desktopOrder)return;
    ++ownerVisibilityEvents;queueOwnerVisibility();
}
inline void syncOwnerVisibility(){
    if(!initialized||!onOwnerThread(owner.load())||!visibleScreenControls()){if(ownerVisibilityActive())stopOwnerVisibility();return;}
    if(ownerVisibilityActive())return;
    constexpr std::array<DWORD,5> events={EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_REORDER,EVENT_OBJECT_LOCATIONCHANGE,EVENT_SYSTEM_MINIMIZEEND};
    for(size_t i=0;i<events.size();++i){
        ownerVisibilityHooks[i]=SetWinEventHook(events[i],events[i],nullptr,screenVisibilityObserved,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        if(!ownerVisibilityHooks[i]){const DWORD error=GetLastError();stopOwnerVisibility();windowError=error?error:ERROR_GEN_FAILURE;return;}
    }
    queueOwnerVisibility();
}
inline bool screenLayerOccluded(HWND layer,HWND foreground){
    if(!layer||!IsWindowVisible(layer)||!foreground||!IsWindowVisible(foreground)||IsIconic(foreground)||
        !(GetWindowLongPtrW(foreground,GWL_EXSTYLE)&WS_EX_TOPMOST))return false;
    RECT a{},b{},intersection{};
    if(!GetWindowRect(layer,&a)||!GetWindowRect(foreground,&b)||!IntersectRect(&intersection,&a,&b))return false;
    // Bounded inspection only after an external foreground/show/order event.
    HWND before=GetWindow(layer,GW_HWNDPREV);
    for(int i=0;before&&i<128;++i,before=GetWindow(before,GW_HWNDPREV))if(before==foreground)return true;
    return false;
}
inline bool screenPairMouseSafe(HWND input,HWND outline){
    bool safe=!outline||ymcc::screeninput::ensureDisplay(outline);
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    if(input&&nativeInputCandidateRequested){
        bool found=false;
        for(int i=0;i<2;++i)if(windows[i]==input){found=true;safe=nativeInputSurfaces[i].ensureMousePass()&&safe;}
        for(int i=0;i<7;++i)if(buttonWindows[i]==input){found=true;safe=nativeButtonSurfaces[i].ensureMousePass()&&safe;}
        safe=safe&&found;
    }
#endif
    if(!safe){if(input)ShowWindow(input,SW_HIDE);if(outline)ShowWindow(outline,SW_HIDE);release();}
    return safe;
}
inline void restoreScreenLayerPair(HWND input,HWND outline,HWND foreground){
    if(!input||!IsWindowVisible(input)||!screenPairMouseSafe(input,outline))return;
    if(!screenLayerOccluded(input,foreground)&&!screenLayerOccluded(outline,foreground))return;
    for(HWND layer:{input,outline})if(layer&&IsWindowVisible(layer)){
        if(SetWindowPos(layer,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE))++ownerVisibilityRaises;
        else windowError=GetLastError();
    }
}
inline void restoreScreenFeedbackOrder(){
    // Raising an input window also raises its owned visual windows; raising
    // its outline afterwards can then put that outline above the contact.
    // Finish the same external-visibility recovery as one ordered stack.
    // These are display-only, already-visible windows: never reshow a hidden
    // contact/cursor, rebuild a bitmap, move input or touch the active contact.
    for(auto& visual:contactVisuals){
        if(!visual.window||!visual.visible||!IsWindowVisible(visual.window))continue;
        if(!ymcc::screeninput::ensureDisplay(visual.window)){
            const DWORD error=GetLastError();windowError=error?error:ERROR_ACCESS_DENIED;hideContactVisual(visual);continue;
        }
        if(visual.visible&&IsWindowVisible(visual.window)){
            if(SetWindowPos(visual.window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE))++ownerVisibilityRaises;
            else windowError=GetLastError();
        }
    }
    if(cursorWindow&&cursorVisible&&IsWindowVisible(cursorWindow)){
        if(!ymcc::screeninput::ensureDisplay(cursorWindow)){
            const DWORD error=GetLastError();cursorError=error?error:ERROR_ACCESS_DENIED;cursorHideVisual();return;
        }
        if(cursorVisible&&IsWindowVisible(cursorWindow)){
            if(SetWindowPos(cursorWindow,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)){
                ++ownerVisibilityRaises;++cursorStackRaises;
            }else{cursorError=GetLastError();++cursorStackRaiseFailures;}
        }
    }
}
inline bool consumeOwnerVisibility(UINT_PTR epoch){
    if(epoch!=ownerVisibilityEpoch||!ownerVisibilityQueued||!initialized||!onOwnerThread(owner.load()))return false;
    ownerVisibilityQueued=false;
    const HWND foreground=GetForegroundWindow(),root=foreground?GetAncestor(foreground,GA_ROOT):nullptr;
    DWORD pid=0;if(!root||!GetWindowThreadProcessId(root,&pid)||pid==GetCurrentProcessId())return false;
    ++ownerVisibilityChecks;const auto raisesBefore=ownerVisibilityRaises;
    for(int i=0;i<2;++i)restoreScreenLayerPair(windows[i],outlines[i],root);
    for(int i=0;i<7;++i)restoreScreenLayerPair(buttonWindows[i],buttonOutlines[i],root);
    if(ownerVisibilityRaises!=raisesBefore)restoreScreenFeedbackOrder();
    return true;
}
} // namespace ymcc::screenpads
