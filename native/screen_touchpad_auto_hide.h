#pragma once
// Included inside screenpads after button/visual helpers. Owner-thread only.
// One deadline timer; no BUS work, per-MOVE rearm, animation or polling thread.
inline void autoHideStopTimer() noexcept {
    const HWND h=autoHide.timerWindow;const UINT_PTR id=autoHide.timer;
    autoHide.timerWindow=nullptr;autoHide.timer=0;
    if(h && id)KillTimer(h,id);
}
inline void autoHideStop() {
    autoHideStopTimer();autoHide.hidden=false;autoHide.visibleMask=0;autoHide.lastTouch=0;autoHide.specialContacts={};
    ++autoHide.revealSerial;autoHide.retryRevealMask=0;
    autoHide.revealErrors={};
}
inline bool autoHideContactHeld() {
    {std::lock_guard<std::mutex> lock(mutex);
        if(core.pads[0].down || core.pads[1].down || feedbackOnlyContacts.mask())return true;}
    return ymcc::customtouch::contactHeld() || buttonContacts[0]!=0 || autoHide.specialContacts[0]!=0 || autoHide.specialContacts[1]!=0 || buttonContacts[1]!=0 || buttonContacts[2]!=0;
}
inline unsigned autoHideVisibleMask(const Config& cfg) {
    unsigned mask=0;
    if(!cfg.masterEnabled)return mask;
    if(cfg.enabled)for(int side=0;side<2;++side) {
        if(cfg.layout==Layout::Single && side!=0)continue;
        if(permitted(surfaceMode(cfg,side)) && windows[side] && outlines[side] && IsWindowVisible(windows[side]))mask|=1u<<side;
    }
    for(int i=0;i<=2;++i)if(buttonPermitted(cfg,i) && buttonWindows[i] && buttonOutlines[i] && IsWindowVisible(buttonWindows[i]))mask|=i==0?16u:1u<<(i+1);
    if(ymcc::customtouch::visible())mask|=32u;
    return mask;
}
inline constexpr unsigned autoHideOutlineBits[]={1u,2u,16u,4u,8u}; // pads, summon, Steam, QAM
inline DWORD autoHideRevealError() noexcept {
    if(!initialized||inputTeardown||autoHide.hidden||autoHide.revealPadEpoch!=padRefreshEpoch||
       autoHide.revealButtonEpoch!=buttonRefreshEpoch)return 0;
    for(unsigned i=0;i<autoHide.revealErrors.size();++i)
        if(autoHide.visibleMask&autoHideOutlineBits[i] && autoHide.revealErrors[i])return autoHide.revealErrors[i];
    return 0;
}
template<class Current,class Render> inline bool autoHideUpdateOutline(const HWND& slot,bool wanted,const Current& revealCurrent,const Render& render,DWORD& submissionError,bool immediateRetry=true) {
    submissionError=0;
    const HWND visual=slot;if(!visual)return true;
    if(!wanted){ShowWindow(visual,SW_HIDE);return true;}
    const auto current=[&]{return slot==visual&&revealCurrent();};
    if(!current())return true;
    const auto submit=[&]{return ymcc::screeninput::ensureDisplay(visual)&&current()&&render(&submissionError);};
    bool ready=submit();
    // A missed first submission must not strand this outline in the hidden
    // state. Retry only this artwork once, without touching its input host.
    if(immediateRetry&&!ready&&current()){
        const DWORD firstError=submissionError;submissionError=0;
        ready=submit();
        if(!ready&&!submissionError)submissionError=firstError;
    }
    if(current())ShowWindow(visual,ready?SW_SHOWNOACTIVATE:SW_HIDE);
    return ready||!current();
}
inline void autoHideSetHidden(bool hidden) {
    if(displayTransitionFence.idleSuspended)return;
    if(presentationSuspended.load(std::memory_order_acquire)&&!hidden)return;
    if(autoHide.hidden==hidden)return;
    autoHide.hidden=hidden;
    const auto serial=++autoHide.revealSerial;
    autoHide.retryRevealMask=0;
    autoHide.revealErrors={};
    if(hidden)++autoHide.hides;else ++autoHide.wakes;
    const auto epoch=padRefreshEpoch;
    const auto buttonEpoch=buttonRefreshEpoch;
    const auto mask=autoHide.visibleMask;
    const auto current=[&]{return initialized&&!inputTeardown&&padRefreshEpoch==epoch&&
        buttonRefreshEpoch==buttonEpoch&&autoHide.revealSerial==serial&&
        autoHide.hidden==hidden&&autoHide.visibleMask==mask&&!displayTransitionFence.idleSuspended&&
        (hidden||!presentationSuspended.load(std::memory_order_acquire));};
    const Config cfg=getConfig();
    unsigned failed=0;
    std::array<DWORD,5> errors{};
    // Only visual HWNDs change visibility. Input surfaces retain Touch/Pen and
    // their existing mouse-pass policy, so the first touch still acts normally.
    for(int side=0;side<2;++side) {
        if(!current())return;
        const bool wanted=!hidden && (mask&(1u<<side));
        if(!autoHideUpdateOutline(outlines[side],wanted,current,[&](DWORD* error){return renderOutline(side,cfg.transparency,false,error);},errors[side]))failed|=1u<<side;
    }
    for(int i=0;i<=2;++i) {
        if(!current())return;
        const unsigned bit=autoHideOutlineBits[i+2];
        const bool wanted=!hidden && (mask&bit);
        if(!autoHideUpdateOutline(buttonOutlines[i],wanted,current,[&](DWORD* error){return renderButton(i,cfg.transparency,false,error);},errors[i+2]))failed|=bit;
    }
    if(!current())return;
    ymcc::customtouch::syncVisibility(hidden);if(!current())return;
    syncContactVisuals();
    if(current()&&failed){
        autoHide.revealPadEpoch=epoch;autoHide.revealButtonEpoch=buttonEpoch;
        autoHide.retryRevealMask=failed;autoHide.revealErrors=errors;
    }
}
inline bool autoHideRetryFailedReveal() {
    const auto failed=autoHide.retryRevealMask&autoHide.visibleMask;
    autoHide.retryRevealMask=0;
    const auto padEpoch=autoHide.revealPadEpoch,buttonEpoch=autoHide.revealButtonEpoch;
    if(!failed||padEpoch!=padRefreshEpoch||buttonEpoch!=buttonRefreshEpoch)return true;
    const auto serial=autoHide.revealSerial;
    const auto mask=autoHide.visibleMask;
    const auto current=[&]{return initialized&&!inputTeardown&&!autoHide.hidden&&autoHide.revealSerial==serial&&
        autoHide.visibleMask==mask&&padRefreshEpoch==padEpoch&&buttonRefreshEpoch==buttonEpoch&&
        !displayTransitionFence.idleSuspended&&sharedInputAllowed();};
    if(!current())return false;
    const Config cfg=getConfig();bool changed=false;
    const auto retry=[&](unsigned index,HWND input,const HWND& visual,const auto& render){
        if(!current()||!(failed&autoHideOutlineBits[index])||!input||!IsWindowVisible(input)||!visual)return;
        if(IsWindowVisible(visual)){autoHide.revealErrors[index]=0;return;}
        DWORD error=0;const bool ready=autoHideUpdateOutline(visual,true,current,render,error,false);
        if(!current())return;
        if(ready){autoHide.revealErrors[index]=0;changed=true;}
        else if(error)autoHide.revealErrors[index]=error;
    };
    // Consume the retry before drawing; a nested touch cannot repeat it.
    for(int i=0;i<2;++i)retry(i,windows[i],outlines[i],[&](DWORD* error){return renderOutline(i,cfg.transparency,true,error);});
    for(int i=0;i<=2;++i)retry(i+2,buttonWindows[i],buttonOutlines[i],[&](DWORD* error){return renderButton(i,cfg.transparency,true,error);});
    if(current()&&changed)syncContactVisuals();
    return current();
}
inline bool autoHideRevealForIdle() {
    const auto epoch=padRefreshEpoch;
    const auto buttonEpoch=buttonRefreshEpoch;
    const auto serial=autoHide.revealSerial+(autoHide.hidden?1:0);
    autoHideSetHidden(false);
    // A nested refresh or OFF selection owns its own idle deadline.
    return epoch==padRefreshEpoch&&buttonEpoch==buttonRefreshEpoch&&serial==autoHide.revealSerial&&
        initialized&&!inputTeardown&&!autoHide.hidden&&
        !displayTransitionFence.idleSuspended&&!presentationSuspended.load(std::memory_order_acquire)&&
        autoHide.visibleMask&&autoHide.seconds;
}
inline void CALLBACK autoHideTimerProc(HWND h,UINT,UINT_PTR id,DWORD) {
    // KillTimer does not remove an already queued WM_TIMER. A fresh ID on each
    // arm makes old callbacks harmless after configuration/restart/teardown.
    if(h!=autoHide.timerWindow || id!=autoHide.timer)return;
    autoHideStopTimer();
    try {autoHideSync();}catch(...){windowError=ERROR_GEN_FAILURE;}
}
inline void autoHideArm(ULONGLONG delay) {
    const HWND h=owner.load(std::memory_order_acquire);
    if(!h || !IsWindow(h))return;
    const UINT_PTR id=++autoHide.serial;
    const auto timer=SetTimer(h,id,(UINT)std::clamp<ULONGLONG>(delay,USER_TIMER_MINIMUM,120000),autoHideTimerProc);
    if(!timer){windowError=GetLastError();if(!windowError)windowError=ERROR_NOT_ENOUGH_MEMORY;return;}
    // With a non-null HWND, nIDEvent is the timer identity; the return value
    // only indicates success and is not the documented cancellation token.
    autoHide.timerWindow=h;autoHide.timer=id;
}
inline void autoHideSync(bool reset) {
    if(!initialized){autoHideStop();return;}
    if(ymcc::customtouch::editing.load()){autoHideStopTimer();autoHideSetHidden(false);return;}
    if(displayTransitionFence.idleSuspended){displayTransitionFence.resetIdle=displayTransitionFence.resetIdle||reset;return;}
    const Config cfg=getConfig();const unsigned mask=autoHideVisibleMask(cfg);
    const bool restart=reset || cfg.autoHideSeconds!=autoHide.seconds || (mask&~autoHide.visibleMask)!=0;
    autoHide.visibleMask=mask;autoHide.seconds=cfg.autoHideSeconds;
    for(int i=0;i<2;++i)if(!(mask&(1u<<(i+2))))autoHide.specialContacts[i]=0;
    if(!mask || !autoHide.seconds) {
        autoHideStopTimer();autoHide.lastTouch=GetTickCount64();autoHideSetHidden(false);return;
    }
    if(restart) {
        autoHideStopTimer();autoHide.lastTouch=GetTickCount64();
        if(!autoHideRevealForIdle())return;
    }
    if(autoHide.hidden){autoHideStopTimer();return;}
    // A stationary held contact is active use. Once this timer fires, wait for
    // next touch event instead of periodically checking stationary fingers.
    if(autoHideContactHeld())return;
    if(autoHide.timer)return;
    const ULONGLONG now=GetTickCount64(),timeout=(ULONGLONG)autoHide.seconds*1000;
    if(!autoHide.lastTouch)autoHide.lastTouch=now;
    const ULONGLONG elapsed=now>=autoHide.lastTouch?now-autoHide.lastTouch:0;
    if(elapsed>=timeout){autoHideSetHidden(true);return;}
    autoHideArm(timeout-elapsed);
}
inline void autoHideTouch(bool freshDown) {
    if(displayTransitionFence.idleSuspended)return;
    if(!initialized || !autoHide.visibleMask || !autoHide.seconds)return;
    autoHide.lastTouch=GetTickCount64();
    if(autoHide.hidden){if(!autoHideRevealForIdle())return;}
    else if(freshDown&&autoHide.retryRevealMask&&!autoHideRetryFailedReveal())return;
    // While a deadline is armed, MOVE only records uptime. It never queries contacts,
    // rearms that timer, allocates, serializes or repaints cached graphics.
    // Also cover foreground-suppressed key touches, which do not enter the
    // mapped contact arrays. They must still expire after waking the visuals.
    if(!autoHide.timer)autoHideArm((ULONGLONG)autoHide.seconds*1000);
}
