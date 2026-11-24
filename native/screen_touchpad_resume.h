#pragma once
#include "sleep_power_state.h"
// Included by the touchpad owner. A finite post-Ready check of our HWND/source
// set; no device/Steam work, shared compositor reset or idle polling.
namespace ymcc::screenpads {
inline constexpr unsigned kOwnerResumeMaxRepairs=3;
inline constexpr ULONGLONG kOwnerResumeBudgetMs=5000;
struct OwnerResumeControlStamp {
    UINT64 pad=0,button=0,custom=0,autoHideReveal=0;
    bool operator==(const OwnerResumeControlStamp&) const=default;
};
struct OwnerResumeRecovery {
    bool observed=false,pending=false,awaitingPower=false;
    UINT_PTR epoch=0,consumedEpoch=0,timer=0,serial=0x59550000;
    HWND window=nullptr;int profile=-1;
    unsigned long long powerGeneration=0;
    ymcc::sleeppower::SharedPowerV1 powerScope;
    ULONGLONG deadline=0;
    ULONGLONG retryOnMessageAt=0;
    unsigned checks=0,repairs=0,clean=0,runtimeRebuilds=0,channelResets=0;
    OwnerResumeControlStamp cleanControls;
    DWORD lastError=0;const char* status="idle";
    // Fixed first-operation evidence, not a new recovery authority or history.
    UINT64 failureSequenceAtStart=0;NativeInputFailure firstInputFailure;
};
inline OwnerResumeRecovery ownerResumeRecovery;
inline bool (*ownerPowerCapture)(ymcc::sleeppower::SharedPowerV1&)=nullptr;
inline bool (*ownerPowerCurrent)(const ymcc::sleeppower::SharedPowerV1&)=nullptr;
struct OwnerFaultBudget {
    unsigned long long generation=0,suspendSerial=0;
    UINT64 settledFailure=0;unsigned repairs=0,episodes=0,runtimeRebuilds=0,channelResets=0;
};
inline OwnerFaultBudget ownerFaultBudget;
inline void syncOwnerFaultBudget(const ymcc::sleeppower::SharedPowerV1& power) {
    if(ownerFaultBudget.generation!=power.generation||ownerFaultBudget.suspendSerial!=power.suspendSerial)
        ownerFaultBudget={power.generation,power.suspendSerial,nativeInputFailure.sequence,0,0};
}
inline void (*ownerResumeRecoveryObserver)(const Json&)=nullptr;
inline Json ownerResumeRecoveryState() {
    const auto& r=ownerResumeRecovery;
    Json state={{"version",1},{"status",r.status},{"pending",r.pending},{"timerArmed",r.timer!=0},
        {"timerDeliveryDeferred",r.retryOnMessageAt!=0},
        {"powerGeneration",r.powerGeneration},{"checks",r.checks},{"repairs",r.repairs},{"clean",r.clean},{"runtimeRebuilds",r.runtimeRebuilds},{"inputChannelResets",r.channelResets},{"lastError",r.lastError}};
    state["faultBudget"]={{"generation",ownerFaultBudget.generation},{"suspendSerial",ownerFaultBudget.suspendSerial},
        {"repairs",ownerFaultBudget.repairs},{"episodes",ownerFaultBudget.episodes},{"runtimeRebuilds",ownerFaultBudget.runtimeRebuilds},{"inputChannelResets",ownerFaultBudget.channelResets},{"maximumRepairs",kOwnerResumeMaxRepairs},{"maximumEpisodes",kOwnerResumeMaxRepairs}};
    if(r.lastError&&nativeInputFailure.sequence&&nativeInputFailure.error==r.lastError)state["nativeInputFailure"]=nativeInputFailureState();
    if(r.firstInputFailure.sequence){try{const auto& first=r.firstInputFailure;
        Json::object_t value;value.emplace("version",1);value.emplace("sequence",first.sequence);
        value.emplace("surface",first.extension?"custom":first.button?"button":"pad");value.emplace("index",first.index);
        value.emplace("error",first.error);value.emplace("stage",first.stage);
        state.emplace("firstInputFailure",Json(std::move(value)));
    }catch(...){}} // Optional evidence must not discard the original receipt.

    return state;
}
inline void stopOwnerResumeTimer() {
    auto& r=ownerResumeRecovery;const auto timer=r.timer;const auto window=r.window;r.timer=0;r.retryOnMessageAt=0;
    if(window&&timer)KillTimer(window,timer);
}
inline void cancelOwnerResumeRecovery(const char* status) {
    stopOwnerResumeTimer();ownerResumeRecovery.pending=false;ownerResumeRecovery.awaitingPower=false;ownerResumeRecovery.status=status;
}
inline bool ownerResumeBindingCurrent() {
    const auto& r=ownerResumeRecovery;
    return initialized&&!inputTeardown&&nativeInputCandidateRequested&&
        r.epoch==ownerLifecycleEpoch&&r.window==owner.load()&&onOwnerThread(r.window)&&
        r.profile==activeProfile.load()&&r.profile==appliedProfile.load();
}
inline bool ownerResumeCurrent() {
    const auto& r=ownerResumeRecovery;
    return r.pending&&ownerResumeBindingCurrent()&&ownerPowerCurrent&&ownerPowerCurrent(r.powerScope);
}
inline bool ownerResumeLocallyBlocked() noexcept {
    return presentationSuspended.load(std::memory_order_acquire)||ymcc::customtouch::editing.load(std::memory_order_acquire);
}
inline void loseOwnerResumeContext() {
    auto& r=ownerResumeRecovery;
    const bool awaitPower=r.pending&&ownerResumeBindingCurrent()&&ownerPowerCurrent&&GetTickCount64()<r.deadline;
    cancelOwnerResumeRecovery(awaitPower?"waiting-for-power":"context-changed");
    if(awaitPower){r.awaitingPower=true;r.clean=0;}
}
inline void finishOwnerResumeRecovery(const char* status) {
    if(ownerFaultBudget.generation==ownerResumeRecovery.powerGeneration&&ownerFaultBudget.suspendSerial==ownerResumeRecovery.powerScope.suspendSerial)
        ownerFaultBudget.settledFailure=nativeInputFailure.sequence;
    cancelOwnerResumeRecovery(status);
    // One compact terminal receipt. A logging/UI failure cannot restart work.
    try {if(ownerResumeRecoveryObserver)ownerResumeRecoveryObserver(ownerResumeRecoveryState());}catch(...){}
}
inline bool ownerResumeControlsHealthy() {
    if(windowError)return false;
    const Config cfg=getConfig();
    if(padsNeedRefresh(cfg))return false;
    for(int side=0;side<2;++side)if(cfg.masterEnabled&&cfg.enabled&&
        (cfg.layout!=Layout::Single||side==0)&&permitted(surfaceMode(cfg,side))) {
        if(!windows[side]||!IsWindow(windows[side])||!IsWindowVisible(windows[side])||
            !outlines[side]||!IsWindow(outlines[side])||(!autoHide.hidden&&!IsWindowVisible(outlines[side])))return false;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(nativeInputSurfaces[side].root()!=windows[side]||!nativeInputSurfaces[side].active()||!nativeInputSurfaces[side].ensureMousePass())return false;
#endif
        if(!ymcc::screeninput::ensureDisplay(outlines[side]))return false;
    }
    if(buttonsNeedRefresh(cfg))return false;
    if(ymcc::customtouch::needsRefresh())return false;
    for(int index=0;index<7;++index)if(buttonPermitted(cfg,index)) {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(nativeButtonSurfaces[index].root()!=buttonWindows[index]||!nativeButtonSurfaces[index].ensureMousePass())return false;
#endif
        if(!ymcc::screeninput::ensureDisplay(buttonOutlines[index]))return false;
    }
    for(const auto& item:ymcc::customtouch::items)if(ymcc::customtouch::itemWanted(*item)){
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(nativeInputCandidateRequested&&!item->surface.ensureMousePass())return false;
#endif
        if(!ymcc::screeninput::ensureDisplay(item->outline))return false;
    }
    return true;
}
// Probe only inside the existing bounded verifier, never on MOVE or idle.
// HWND visibility is not proof that the independent dispatcher still works.
template<class Current> inline bool ownerResumeProbeControls(bool submitArtwork,const Current& current) {
    const auto cfg=getConfig();
    for(int side=0;side<2;++side)if(cfg.masterEnabled&&cfg.enabled&&
        (cfg.layout!=Layout::Single||side==0)&&permitted(surfaceMode(cfg,side))) {
        if(!current())return false;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(nativeInputCandidateRequested){
            const bool ready=nativeInputSurfaces[side].sync();
            if(!current())return false;
            if(!ready){windowError=(DWORD)nativeInputSurfaces[side].error;recordNativeInputFailure(false,side,nativeInputSurfaces[side]);return false;}
        }
#endif
        if(submitArtwork&&!autoHide.hidden&&!renderOutline(side,cfg.transparency,true))return false;
        if(!current())return false;
    }
    for(int index=0;index<7;++index)if(buttonPermitted(cfg,index)) {
        if(!current())return false;
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
        if(nativeInputCandidateRequested){
            const bool ready=nativeButtonSurfaces[index].sync();
            if(!current())return false;
            if(!ready){windowError=(DWORD)nativeButtonSurfaces[index].error;recordNativeInputFailure(true,index,nativeButtonSurfaces[index]);return false;}
        }
#endif
        if(submitArtwork&&(!autoHide.hidden||index>2)&&!renderButton(index,cfg.transparency,true))return false;
        if(!current())return false;
    }
    return current()&&ymcc::customtouch::probeControls(current,submitArtwork)&&ownerResumeControlsHealthy();
}
inline void CALLBACK ownerResumeTimerProc(HWND,UINT,UINT_PTR,DWORD);
template<class Current> inline bool rebuildOwnedInputResources(Current&& current) {
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    if(!current())return false;
    // Withdraw every controlled HWND before COM calls/cancel sinks can reenter.
    for(auto& surface:nativeInputSurfaces){surface.quiesce();if(!current())return false;}
    for(auto& surface:nativeButtonSurfaces){surface.quiesce();if(!current())return false;}
    const auto withdraw=[&](HWND h,bool disable){
        if(h&&IsWindow(h)){
            ShowWindow(h,SW_HIDE);if(!current())return false;
            if(disable&&IsWindow(h))EnableWindow(h,FALSE);
        }
        return current();
    };
    for(HWND h:windows)if(!withdraw(h,true))return false;
    for(HWND h:buttonWindows)if(!withdraw(h,true))return false;
    for(HWND h:outlines)if(!withdraw(h,false))return false;
    for(HWND h:buttonOutlines)if(!withdraw(h,false))return false;
    release();if(!current())return false;
    if(!ymcc::customtouch::retireOwnedResources(current)||!current())return false;
    for(auto& surface:nativeInputSurfaces){surface.close();if(!current())return false;}
    for(auto& surface:nativeButtonSurfaces){surface.close();if(!current())return false;}
    for(const auto& surface:nativeInputSurfaces)if(!surface.terminalReleased()){windowError=ERROR_NOT_READY;return false;}
    for(const auto& surface:nativeButtonSurfaces)if(!surface.terminalReleased()){windowError=ERROR_NOT_READY;return false;}
    if(sentKeys||sentClick||screenButtonMask.load()||ymcc::customtouch::heldKeys()){windowError=ERROR_NOT_READY;return false;}
    const auto destroy=[&](HWND& handle){
        if(!current())return false;const HWND retired=handle;
        if(retired&&IsWindow(retired)&&!DestroyWindow(retired)){
            const auto error=GetLastError();if(current())windowError=error;return false;
        }
        if(handle==retired)handle=nullptr;return current();
    };
    for(auto& h:outlines)if(!destroy(h))return false;
    for(auto& h:buttonOutlines)if(!destroy(h))return false;
    for(auto& h:windows)if(!destroy(h))return false;
    for(auto& h:buttonWindows)if(!destroy(h))return false;
    outlineUploads={};buttonUploads={};shutdownContactVisuals();
    if(!current())return false;
    // All visuals/targets/controllers belonging to this compositor are gone.
    // Keep the thread's dispatcher queue: monitor/radial may borrow that queue.
    nativePadInputRuntime.compositor=nullptr;nativePadInputRuntime.error=S_OK;
    return true;
#else
    return current();
#endif
}
inline void armOwnerResumeCheck(unsigned delay) {
    if(!ownerResumeCurrent()){loseOwnerResumeContext();return;}
    auto& r=ownerResumeRecovery;const auto now=GetTickCount64();
    if(now>=r.deadline){finishOwnerResumeRecovery("expired");return;}
    const UINT_PTR id=++r.serial;
    const UINT timeout=static_cast<UINT>(std::min<ULONGLONG>(delay,r.deadline-now));
    const auto interval=std::max<UINT>(USER_TIMER_MINIMUM,timeout);
    if(!SetTimer(r.window,id,interval,ownerResumeTimerProc)) {
        r.lastError=GetLastError();r.retryOnMessageAt=now+interval;return;
    }
    r.timer=id;
}
inline void runOwnerResumeCheck(UINT_PTR id) {
    auto& r=ownerResumeRecovery;
    stopOwnerResumeTimer();
    if(!ownerResumeCurrent()){loseOwnerResumeContext();return;}
    const auto epoch=r.epoch;const auto generation=r.powerGeneration;
    const auto sameAttempt=[&]{return r.epoch==epoch&&r.powerGeneration==generation&&r.serial==id;};
    const auto current=[&]{
        if(!sameAttempt())return false; // A nested Ready can replace the timer in the same generation.
        if(ownerResumeCurrent())return true;
        if(r.pending)loseOwnerResumeContext();return false;
    };
    const auto expired=[&] {return GetTickCount64()>=r.deadline;};
    if(!getConfig().masterEnabled){finishOwnerResumeRecovery("disabled");return;}
    if(expired()){finishOwnerResumeRecovery("expired");return;}
    // Hidden or edited controls cannot be probed or repaired by the wake verifier.
    const auto displayBusy=[] {return ownerDisplayRefresh.pending||displayTransitionFence.idleSuspended;};
    if(!sharedInputAllowed()){r.clean=0;armOwnerResumeCheck(250);return;}
    if(ownerDisplayReflowInterrupted&&displayTransitionFence.idleSuspended){
        queueOwnerDisplayRefresh(true,true);
        if(!current())return;
    }
    if(displayBusy()){r.clean=0;armOwnerResumeCheck(100);return;}
    // Auto-hide changes the artwork the probe must verify without rebinding input.
    const auto controlStamp=[] {return OwnerResumeControlStamp{padRefreshEpoch,buttonRefreshEpoch,ymcc::customtouch::refreshEpoch,autoHide.revealSerial};};
    const auto stamp=controlStamp();
    const auto controlsCurrent=[&] {return stamp==controlStamp()&&!ownerResumeLocallyBlocked()&&!displayBusy();};
    const auto probeCurrent=[&] {return sameAttempt()&&ownerResumeCurrent()&&controlsCurrent()&&!expired();};
    if(r.clean&&r.cleanControls!=stamp)r.clean=0;
    try {
        ++r.checks;
        const bool healthy=ownerResumeControlsHealthy()&&probeCurrent()&&ownerResumeProbeControls(r.clean==0,probeCurrent);
        if(!current())return;
        // A dispatcher callback can outlast the budget. Do not certify or
        // begin a repair late; an already admitted repair must finish cleanup.
        if(expired()){finishOwnerResumeRecovery("expired");return;}
        // ProcessEvents can run configuration or display work on this stack.
        // A superseded observation neither proves health nor spends a repair.
        if(!controlsCurrent()){r.clean=0;armOwnerResumeCheck(displayBusy()?100:250);return;}
        if(healthy) {
            r.cleanControls=stamp;
            if(++r.clean==2){finishOwnerResumeRecovery(r.repairs?"recovered":"healthy");return;}
            armOwnerResumeCheck(250);return;
        }
        r.clean=0;
        if(windowError)r.lastError=windowError;
        if(r.repairs==kOwnerResumeMaxRepairs||ownerFaultBudget.repairs==kOwnerResumeMaxRepairs){finishOwnerResumeRecovery("failed");return;}
        ++r.repairs;++ownerFaultBudget.repairs;
        const auto sameSources=[&]{return stamp.pad==padRefreshEpoch&&stamp.button==buttonRefreshEpoch&&
            stamp.custom==ymcc::customtouch::refreshEpoch&&!ownerResumeLocallyBlocked()&&!displayBusy();};
        const auto sourceCurrent=[&]{return current()&&sameSources();};
        const auto retainAfterReplacement=[&]{
            if(!current())return true;
            if(sameSources())return false;
            armOwnerResumeCheck(displayBusy()?100:250);return true;
        };
        // Release only this owner's pads/keys. Recreate even E_UNEXPECTED:
        // the previous one-shot resume could hide everything permanently.
        // A short successful episode cannot reset the escalation stage. The
        // second actual repair in this power scope rebuilds our resources even
        // when the two failures arrived in separate episodes. At most once.
        if(ownerFaultBudget.repairs==kOwnerResumeMaxRepairs&&!ownerFaultBudget.channelResets){
            if(!rebuildOwnedInputResources(sourceCurrent)){
                if(!retainAfterReplacement()){
                    r.lastError=windowError?windowError:ERROR_NOT_READY;finishOwnerResumeRecovery("channel-reset-failed");
                }
                return;
            }
            ++r.channelResets;++ownerFaultBudget.channelResets;
            if(!nativePadInputRuntime.useFreshInputChannel()){
                if(current()){r.lastError=windowError?windowError:(DWORD)nativePadInputRuntime.error;finishOwnerResumeRecovery("channel-reset-failed");}return;
            }
        }else if(ownerFaultBudget.repairs>=2&&!ownerFaultBudget.runtimeRebuilds) {
            ++r.runtimeRebuilds;++ownerFaultBudget.runtimeRebuilds;
            if(!rebuildOwnedInputResources(sourceCurrent)) {
                if(!retainAfterReplacement()){r.lastError=windowError?windowError:ERROR_NOT_READY;finishOwnerResumeRecovery("release-failed");}
                return;
            }
        } else {
            const auto detachSources=[&]{
                release();if(!sourceCurrent())return false;
                for(int side=0;side<2;++side){nativeInputClose(side);if(!sourceCurrent())return false;}
                return buttonsNativeInputDisableCurrent(sourceCurrent)&&ymcc::customtouch::detachInputsCurrent(sourceCurrent);
            };
            if(!detachSources()){
                // A nested reflow owns the replacement HWNDs. A failed close
                // in the current layout instead needs bounded escalation.
                if(retainAfterReplacement())return;
                if(!windowError)windowError=ERROR_NOT_READY;r.lastError=windowError;
                armOwnerResumeCheck(500);
                return;
            }
        }
        if(!current())return;
        refresh(true);
        if(!current())return;
        if(windowError)r.lastError=windowError;
        // Back off from the actual attempt; never catch up missed timers.
        armOwnerResumeCheck(r.repairs==1?500:1000);
    }catch(...){if(current()){r.lastError=ERROR_GEN_FAILURE;finishOwnerResumeRecovery("exception");}}
}
inline void CALLBACK ownerResumeTimerProc(HWND window,UINT,UINT_PTR id,DWORD) {
    const auto& r=ownerResumeRecovery;
    // KillTimer does not remove a queued callback. Only the current arm runs.
    if(r.timer&&window==r.window&&id==r.timer)runOwnerResumeCheck(id);
}
inline void ownerResumeContinueOnWindowMessage(HWND window,UINT message) {
    const auto& r=ownerResumeRecovery;
    if(!r.retryOnMessageAt||window!=r.window)return;
    // Let the original lifecycle handlers commit their boundaries first.
    if(message!=WM_NULL&&message!=WM_PAINT&&message!=WM_INPUT&&message!=WM_MOUSEMOVE&&message!=WM_SETCURSOR)return;
    if(GetTickCount64()>=r.retryOnMessageAt)runOwnerResumeCheck(r.serial);
}
inline bool ownerResumeReady(unsigned long long powerGeneration) {
    auto& r=ownerResumeRecovery;const auto window=owner.load();
    if(!powerGeneration||!r.observed||!initialized||inputTeardown||!nativeInputCandidateRequested||!onOwnerThread(window))return false;
    ymcc::sleeppower::SharedPowerV1 power;
    if(r.consumedEpoch==ownerLifecycleEpoch){
        // A coalesced power notification may revoke the exact Ready scope
        // without a new suspend. Continue only the original live budget.
        if((!r.pending&&!r.awaitingPower)||powerGeneration!=r.powerGeneration||!ownerResumeBindingCurrent()||
            !ownerPowerCapture||!ownerPowerCurrent)return r.pending;
        if(r.pending&&ownerPowerCurrent(r.powerScope))return true;
        if(!ownerPowerCapture(power)||power.generation!=r.powerGeneration||power.suspendSerial!=r.powerScope.suspendSerial||
            !ownerPowerCurrent(power))return r.pending;
        if(GetTickCount64()>=r.deadline){finishOwnerResumeRecovery("expired");return false;}
        stopOwnerResumeTimer();r.powerScope=power;r.awaitingPower=false;r.clean=0;r.status="checking";r.pending=true;
        armOwnerResumeCheck(250);return r.pending;
    }
    if(!ownerPowerCapture||!ownerPowerCurrent||!ownerPowerCapture(power)||power.generation!=powerGeneration)return false;
    // A notification/query can close and reopen admission without a new sleep.
    // It may withdraw resources, but cannot refund this episode's repair limit.
    if(r.powerGeneration==power.generation&&r.powerScope.suspendSerial==power.suspendSerial)return r.pending;
    stopOwnerResumeTimer();r.consumedEpoch=r.epoch=ownerLifecycleEpoch;r.window=window;r.profile=activeProfile.load();
    syncOwnerFaultBudget(power);
    r.powerGeneration=powerGeneration;r.powerScope=power;r.deadline=GetTickCount64()+kOwnerResumeBudgetMs;
    r.failureSequenceAtStart=nativeInputFailure.sequence;r.firstInputFailure={};
    r.checks=r.repairs=r.clean=r.runtimeRebuilds=r.channelResets=0;r.lastError=0;r.status="checking";r.pending=true;r.awaitingPower=false;
    armOwnerResumeCheck(250);return r.pending;
}
inline bool ownerNativeInputFailed() {
    auto& r=ownerResumeRecovery;
    if(r.pending){
        if(!r.firstInputFailure.sequence&&nativeInputFailure.sequence>r.failureSequenceAtStart&&nativeInputFailure.error&&
           getConfig().masterEnabled&&ownerResumeCurrent())r.firstInputFailure=nativeInputFailure;
        return false;
    }
    if(!initialized||inputTeardown||!nativeInputCandidateRequested||!onOwnerThread(owner.load())||
        !getConfig().masterEnabled||!ownerPowerCapture||!ownerPowerCurrent)return false;
    ymcc::sleeppower::SharedPowerV1 power;
    if(!ownerPowerCapture(power))return false;
    const auto generation=power.generation;
    if(!generation||!ownerPowerCurrent(power))return false;
    const bool sameBudget=ownerFaultBudget.generation==generation&&ownerFaultBudget.suspendSerial==power.suspendSerial;
    // Healthy wake verification consumes no fault attempt. A later real failed
    // operation may use the remaining allowance; repeated calls or an exhausted
    // generation cannot refund it. Query/notification/config changes do not reset.
    if(sameBudget&&(nativeInputFailure.sequence<=ownerFaultBudget.settledFailure||
        ownerFaultBudget.repairs>=kOwnerResumeMaxRepairs||ownerFaultBudget.episodes>=kOwnerResumeMaxRepairs))return false;
    syncOwnerFaultBudget(power);++ownerFaultBudget.episodes;
    ownerFaultBudget.settledFailure=nativeInputFailure.sequence;
    stopOwnerResumeTimer();r.epoch=ownerLifecycleEpoch;r.window=owner.load();r.profile=activeProfile.load();
    r.powerGeneration=generation;r.powerScope=power;r.deadline=GetTickCount64()+kOwnerResumeBudgetMs;
    r.failureSequenceAtStart=nativeInputFailure.sequence;r.firstInputFailure=nativeInputFailure;
    r.checks=r.repairs=r.clean=r.runtimeRebuilds=r.channelResets=0;r.lastError=0;r.status="checking";r.pending=true;r.awaitingPower=false;
    armOwnerResumeCheck(250);return r.pending;
}
} // namespace ymcc::screenpads
