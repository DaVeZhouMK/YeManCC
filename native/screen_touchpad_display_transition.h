#pragma once
// Display notifications can arrive in a nested fullscreen/DPI transition.
// Withdraw our interaction immediately, then coalesce one owner-thread reflow.
// The timer exists only for that transition; events cannot extend its deadline.
namespace ymcc::screenpads {
struct OwnerDisplayRefresh {
    HWND window=nullptr;UINT_PTR timer=0,serial=0x59560000,epoch=0;
    bool pending=false;
    UINT64 requests=0,merged=0,completed=0;DWORD lastError=0;
};
inline OwnerDisplayRefresh ownerDisplayRefresh;
enum class DisplayExportPhase : unsigned {Inactive,Settled,TimerPending,Refreshing,WaitingForPowerReady,TimerRejected,WaitingForEditorExit};
// logs.exportAll runs off the HWND thread. Publish the phase and its error
// together without exposing the owner's mutable counters or window handles.
inline std::atomic<UINT64> ownerDisplayExportWord{0};
inline std::atomic<DWORD> ownerDisplayLastTimerError{0};
inline void publishOwnerDisplayExport(DisplayExportPhase phase,DWORD error=0) noexcept {
    ownerDisplayExportWord.store((static_cast<UINT64>(error)<<32)|static_cast<unsigned>(phase),std::memory_order_relaxed);
}
inline Json ownerDisplayExportState() {
    const auto word=ownerDisplayExportWord.load(std::memory_order_relaxed);
    constexpr const char* phases[]={"inactive","settled","timer-pending","refreshing","waiting-for-power-ready","timer-rejected","waiting-for-editor-exit"};
    return {{"version",1},{"phase",phases[static_cast<unsigned>(word)]},{"phaseError",static_cast<DWORD>(word>>32)},
        {"lastTimerError",ownerDisplayLastTimerError.load(std::memory_order_relaxed)},
        {"scope","last owner-published reflow phase; timer error retained separately; not window or pixel proof"}};
}
inline void cancelOwnerDisplayRefresh(bool preserveIdle=false) noexcept {
    auto& work=ownerDisplayRefresh;const auto timer=work.timer;const auto window=work.window;
    work.pending=false;work.timer=0;work.window=nullptr;work.epoch=0;
    if(preserveIdle&&displayTransitionFence.idleSuspended){
        displayTransitionFence.pending=false;displayTransitionFence.forceVisuals=false;
    }else displayTransitionFence={};
    if(window&&timer)KillTimer(window,timer);
}
template<class Current> inline void withdrawOwnerDisplayLayers(const Current& current) {
    for(HWND h:windows)if(current()&&h)ShowWindow(h,SW_HIDE);
    for(HWND h:buttonWindows)if(current()&&h)ShowWindow(h,SW_HIDE);
    for(HWND h:outlines)if(current()&&h)ShowWindow(h,SW_HIDE);
    for(HWND h:buttonOutlines)if(current()&&h)ShowWindow(h,SW_HIDE);
    if(current())ymcc::customtouch::hideForTransition();
    for(auto& visual:contactVisuals)if(current())hideContactVisual(visual);
    if(current())cursorHideVisual();
}
inline void completeOwnerDisplayRefresh(HWND window,UINT_PTR epoch,UINT_PTR id,bool force) {
    auto& work=ownerDisplayRefresh;
    const auto current=[&]{return initialized&&!inputTeardown&&epoch==ownerLifecycleEpoch&&window==owner.load()&&work.serial==id;};
    if(!current()||!onOwnerThread(window))return;
    const auto admissionPhase=[] {return ymcc::customtouch::editing.load()?DisplayExportPhase::WaitingForEditorExit:DisplayExportPhase::WaitingForPowerReady;};
    if(!sharedInputAllowed()){
        // The windows are already withdrawn. Only an admission edge may restore them.
        ownerDisplayReflowInterrupted=true;publishOwnerDisplayExport(admissionPhase());return;
    }
    publishOwnerDisplayExport(DisplayExportPhase::Refreshing);
    bool failed=false;
    try{refresh(force);}catch(...){failed=true;}
    // Sync may dispatch a newer transition, or admission may close without a
    // corresponding owner message. Only the first still owns this withdrawal.
    if(!current())return;
    if(!sharedInputAllowed()){
        ownerDisplayReflowInterrupted=true;publishOwnerDisplayExport(admissionPhase());cancelOwnerVisibilityWork();
        withdrawOwnerDisplayLayers(current);
        if(current()){
            try{release();}catch(...){if(current())work.lastError=windowError=ERROR_GEN_FAILURE;}
            withdrawOwnerDisplayLayers(current);
        }
        return;
    }
    if(failed)work.lastError=windowError=ERROR_GEN_FAILURE;
    else{++work.completed;work.lastError=windowError;}
    publishOwnerDisplayExport(DisplayExportPhase::Settled,work.lastError);
    const bool resetIdle=displayTransitionFence.resetIdle;displayTransitionFence={};
    if(initialized&&!inputTeardown&&epoch==ownerLifecycleEpoch&&window==owner.load())autoHideSync(resetIdle);
}
inline void CALLBACK ownerDisplayRefreshTimerProc(HWND window,UINT,UINT_PTR id,DWORD) {
    auto& work=ownerDisplayRefresh;
    if(!work.pending||!work.timer||id!=work.timer||window!=work.window)return;
    const auto epoch=work.epoch;const bool force=displayTransitionFence.forceVisuals;
    // Allow this one refresh while retaining idle state until all HWNDs return.
    cancelOwnerDisplayRefresh(true);
    completeOwnerDisplayRefresh(window,epoch,id,force);
}
inline bool queueOwnerDisplayRefresh(bool forceVisuals,bool resumeInterrupted=false) {
    const HWND window=owner.load();const auto epoch=ownerLifecycleEpoch;
    if(!initialized||inputTeardown||!onOwnerThread(window))return false;
    if(ymcc::customtouch::editing.load())ymcc::customtouch::finish(false,false);
    if(!initialized||inputTeardown||epoch!=ownerLifecycleEpoch||window!=owner.load())return false;
    auto& work=ownerDisplayRefresh;
    // A settled master OFF has no layout to move. Preserve deferred cleanup
    // and let a visible input remnant or previous fault take the normal path.
    if(!work.pending&&!displayTransitionFence.idleSuspended&&!windowError&&
       !getConfig().masterEnabled&&!visibleScreenControls())return false;
    if(!sharedInputAllowed())return false;
    ++work.requests;
    if(work.pending&&work.window==window&&work.epoch==epoch){displayTransitionFence.forceVisuals=displayTransitionFence.forceVisuals||forceVisuals;++work.merged;return true;}
    const bool resetIdle=displayTransitionFence.resetIdle;
    const auto id=work.serial+1;
    // Do not supersede a live refresh unless its replacement can run.
    if(!SetTimer(window,id,100,ownerDisplayRefreshTimerProc)){
        const auto error=GetLastError();work.lastError=error?error:ERROR_GEN_FAILURE;
        ownerDisplayLastTimerError.store(work.lastError,std::memory_order_relaxed);
        // Synchronous fallback only for an interrupted reflow whose admission
        // reopened. The windows are still withdrawn and no timer owns their return.
        if(resumeInterrupted&&ownerDisplayReflowInterrupted&&displayTransitionFence.idleSuspended&&!work.pending){
            work.serial=id;ownerDisplayReflowInterrupted=false;
            completeOwnerDisplayRefresh(window,epoch,id,forceVisuals);
            return initialized&&!inputTeardown&&epoch==ownerLifecycleEpoch&&window==owner.load()&&!ownerDisplayReflowInterrupted;
        }
        if(!displayTransitionFence.idleSuspended){
            windowError=work.lastError;publishOwnerDisplayExport(DisplayExportPhase::TimerRejected,work.lastError);
        }
        return false;
    }
    cancelOwnerDisplayRefresh();work.serial=id;
    work.window=window;work.timer=id;work.epoch=epoch;work.pending=true;displayTransitionFence={true,forceVisuals,true,resetIdle};
    ownerDisplayReflowInterrupted=false;
    publishOwnerDisplayExport(DisplayExportPhase::TimerPending);
    ++padRefreshEpoch;++buttonRefreshEpoch; // A nested older refresh cannot reshow.
    ++ymcc::customtouch::refreshEpoch;
    cancelOwnerVisibilityWork(); // Reflow will publish new visibility work after the layers return.
    const auto current=[&]{return initialized&&!inputTeardown&&ownerLifecycleEpoch==epoch&&owner.load()==window&&work.pending&&work.timer==id;};
    withdrawOwnerDisplayLayers(current);if(!current())return false;
    // No bind, geometry sync or ProcessEvents on the UI input source here.
    // Cancellation/release is owner-scoped and precedes the deferred reflow.
    release();if(!current())return false;withdrawOwnerDisplayLayers(current);return current();
}
inline bool resumeOwnerDisplayAfterEditor(){
    if(!initialized||inputTeardown||!onOwnerThread(owner.load())||
       !ownerDisplayReflowInterrupted||!displayTransitionFence.idleSuspended)return false;
    if(sharedInputAllowed())queueOwnerDisplayRefresh(true,true);
    else publishOwnerDisplayExport(DisplayExportPhase::WaitingForPowerReady);
    return true; // No ordinary refresh of the withdrawn windows before admission.
}
inline Json ownerDisplayRefreshState() {
    const auto& work=ownerDisplayRefresh;
    return {{"version",1},{"pending",work.pending},{"timerArmed",work.timer!=0},
        {"requests",work.requests},{"merged",work.merged},{"completed",work.completed},{"lastError",work.lastError},
        {"scope","touchpad-owned display transition;not-exclusive-fullscreen-or-pixel-proof"}};
}
} // namespace ymcc::screenpads
