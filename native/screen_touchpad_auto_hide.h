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
}
inline bool autoHideContactHeld() {
    {std::lock_guard<std::mutex> lock(mutex);
        if(core.pads[0].down || core.pads[1].down || feedbackOnlyContacts.mask())return true;}
    return autoHide.specialContacts[0]!=0 || autoHide.specialContacts[1]!=0 || buttonContacts[1]!=0 || buttonContacts[2]!=0;
}
inline unsigned autoHideVisibleMask(const Config& cfg) {
    unsigned mask=0;
    if(!cfg.masterEnabled)return mask;
    if(cfg.enabled)for(int side=0;side<2;++side) {
        if(cfg.layout==Layout::Single && side!=0)continue;
        if(permitted(surfaceMode(cfg,side)) && windows[side] && outlines[side] && IsWindowVisible(windows[side]))mask|=1u<<side;
    }
    for(int i=1;i<=2;++i)if(buttonPermitted(cfg,i) && buttonWindows[i] && buttonOutlines[i] && IsWindowVisible(buttonWindows[i]))mask|=1u<<(i+1);
    return mask;
}
inline void autoHideSetHidden(bool hidden) {
    if(autoHide.hidden==hidden)return;
    autoHide.hidden=hidden;
    if(hidden)++autoHide.hides;else ++autoHide.wakes;
    const Config cfg=getConfig();
    // Only visual HWNDs change visibility. Input surfaces retain Touch/Pen and
    // their existing mouse-pass policy, so the first touch still acts normally.
    for(int side=0;side<2;++side)if(outlines[side]) {
        const bool wanted=!hidden && (autoHide.visibleMask&(1u<<side));
        const bool ready=wanted && ymcc::screeninput::ensureDisplay(outlines[side]) && renderOutline(side,cfg.transparency);
        ShowWindow(outlines[side],ready?SW_SHOWNOACTIVATE:SW_HIDE);
    }
    for(int i=1;i<=2;++i)if(buttonOutlines[i]) {
        const bool wanted=!hidden && (autoHide.visibleMask&(1u<<(i+1)));
        const bool ready=wanted && ymcc::screeninput::ensureDisplay(buttonOutlines[i]) && renderButton(i,cfg.transparency);
        ShowWindow(buttonOutlines[i],ready?SW_SHOWNOACTIVATE:SW_HIDE);
    }
    syncContactVisuals();
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
    const Config cfg=getConfig();const unsigned mask=autoHideVisibleMask(cfg);
    const bool restart=reset || cfg.autoHideSeconds!=autoHide.seconds || (mask&~autoHide.visibleMask)!=0;
    autoHide.visibleMask=mask;autoHide.seconds=cfg.autoHideSeconds;
    for(int i=0;i<2;++i)if(!(mask&(1u<<(i+2))))autoHide.specialContacts[i]=0;
    if(!mask || !autoHide.seconds) {
        autoHideStopTimer();autoHide.lastTouch=GetTickCount64();autoHideSetHidden(false);return;
    }
    if(restart) {
        autoHideStopTimer();autoHide.lastTouch=GetTickCount64();autoHideSetHidden(false);
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
inline void autoHideTouch() {
    if(!initialized || !autoHide.visibleMask || !autoHide.seconds)return;
    autoHide.lastTouch=GetTickCount64();
    if(autoHide.hidden)autoHideSetHidden(false);
    // While a deadline is armed, MOVE only records uptime. It never queries contacts,
    // rearms that timer, allocates, serializes or repaints cached graphics.
    // Also cover foreground-suppressed key touches, which do not enter the
    // mapped contact arrays. They must still expire after waking the visuals.
    if(!autoHide.timer)autoHideArm((ULONGLONG)autoHide.seconds*1000);
}