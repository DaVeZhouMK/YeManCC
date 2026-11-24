// Ordinary pause work owns the source receipt captured before Steam preparation.
// It grants no power or retry action. A later wake cannot be borrowed by this
// older queue item, even while the GUI has not consumed the callback yet.
static bool sgOrdinaryPauseCurrent(const SgWorkItem& item) {
    const auto phase=g_powerLifecycle.load(std::memory_order_acquire);
    const auto resume=g_sleepPowerDispatch.latestResumeBoundary();
    const auto now=sgNowFileTime();
    return item.kind==SgWork::Suspend && item.pauseOrdinaryWitness && item.generation &&
        item.generation==currentPowerGeneration() && !g_exitRequested.load(std::memory_order_acquire) &&
        (phase==PowerLifecycle::Suspending || phase==PowerLifecycle::Suspended) &&
        (!item.pauseTriggerFileTime || item.pauseTriggerFileTime<=now) &&
        g_sgOriginalPowerButtonWakeFileTime.load(std::memory_order_acquire)==item.pauseWakeFileTime &&
        (!item.pauseWakeFileTime || (item.pauseTriggerFileTime && item.pauseWakeFileTime<item.pauseTriggerFileTime)) &&
        resume.sequence==item.pauseResumeSequence &&
        (!resume || (item.pauseTriggerFileTime && resume.fileTime100ns && resume.fileTime100ns<item.pauseTriggerFileTime));
}

// An exact resource continuation in the existing SleepWork owner. It cannot
// replay Steam capture, change power generation, or request system sleep.
static bool sgReturnedPauseCurrent(const SgWorkItem& item) {
    const auto phase=g_powerLifecycle.load(std::memory_order_acquire);
    const auto entry=g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire);
    const auto resume=g_sleepPowerDispatch.latestResumeBoundary();
    return item.kind==SgWork::Suspend && item.generation && item.pauseEntryFileTime &&
        item.pausePid && item.pauseProcessCreated && !g_exitRequested.load(std::memory_order_acquire) &&
        item.generation==currentPowerGeneration() &&
        g_guardEnabled && g_sgPauseResume && g_sgRepairEligible &&
        (phase==PowerLifecycle::Suspending || phase==PowerLifecycle::Suspended) &&
        entry>=item.pauseEntryFileTime && entry<=sgNowFileTime() &&
        item.pauseWakeFileTime<item.pauseEntryFileTime &&
        g_sgOriginalPowerButtonWakeFileTime.load(std::memory_order_acquire)==item.pauseWakeFileTime &&
        resume.sequence==item.pauseResumeSequence &&
        (!resume || (resume.fileTime100ns && resume.fileTime100ns<item.pauseEntryFileTime));
}
// Private role-bound delivery candidate; unexpected capacity remains a failure.
static void sgCompletePauseWork(unsigned long long generation) noexcept {
    if(!generation||generation!=currentPowerGeneration())return;
    auto completed=g_sgPauseWorkCompletedGeneration.load(std::memory_order_acquire);
    while(completed<generation&&!g_sgPauseWorkCompletedGeneration.compare_exchange_weak(
        completed,generation,std::memory_order_acq_rel,std::memory_order_acquire)){}
}
static bool sgRetainedWorkDeliveryPendingLocked() noexcept {
    for(const auto& pending:g_sgRetainedWorkDelivery)if(pending.order)return true;
    return false;
}
static void sgPruneRetainedWorkDeliveryLocked() {
    const auto current=currentPowerGeneration();
    SgSleepTarget lease{};const bool captured=sgReadSleepTarget(lease)&&lease.pid&&lease.processCreated;
    // Retire obsolete roles before any same-pass merge can observe them.
    // Otherwise an earlier old receipt can swallow the new receipt, then clear.
    const auto wakeSequence=g_sleepPowerDispatch.observedSleepBoundarySequence();
    const auto wakeSerial=g_sharedPowerSuspendSerial.load(std::memory_order_acquire);
    for(auto& pending:g_sgRetainedWorkDelivery)if(pending.order&&
        (!pending.item.wakeBoundaryMatches(wakeSequence,wakeSerial)||
         (pending.item.generation!=current&&!(pending.item.kind==SgWork::WakeSuspend&&captured&&
          lease.powerGeneration==pending.item.generation))))pending={};
    for(auto& pending:g_sgWorkQ)if(pending.kind==SgWork::Suspend&&
        !pending.pauseCanceledDelivery&&
        ((pending.pauseOrdinaryWitness&&!sgOrdinaryPauseCurrent(pending))||
         (pending.pauseEntryFileTime&&!sgReturnedPauseCurrent(pending))))pending.pauseCanceledDelivery=true;
    for(size_t i=0;i<g_sgRetainedWorkDelivery.size();++i){
        auto& pending=g_sgRetainedWorkDelivery[i];if(!pending.order)continue;
        auto& item=pending.item;
        if(item.generation!=current){
            const bool oldWake=item.kind==SgWork::WakeSuspend&&captured&&
                lease.powerGeneration==item.generation;
            if(!oldWake){pending={};continue;}
            if(i!=4){if(g_sgRetainedWorkDelivery[4].order)throw std::bad_alloc();
                g_sgRetainedWorkDelivery[4]=pending;pending={};}continue;
        }
        if(item.kind!=SgWork::Suspend)continue;
        if(!item.pauseCanceledDelivery&&
           (!item.pauseOrdinaryWitness||sgOrdinaryPauseCurrent(item))&&
           (!item.pauseEntryFileTime||sgReturnedPauseCurrent(item)))continue;
        item.pauseCanceledDelivery=true;
        if(i==7)continue;
        auto& canceled=g_sgRetainedWorkDelivery[7];
        if(!canceled.order||pending.order<canceled.order)canceled=pending;
        pending={};
    }
}
static bool sgWorkDeliveryCovers(const SgWorkItem& pending,const SgWorkItem& item,bool returned) {
    if(pending.pauseCanceledDelivery||pending.kind!=item.kind||pending.generation!=item.generation)return false;
    if(!pending.wakeBoundaryMatches(item.wakeSleepSequence,item.wakeSuspendSerial))return false;
    if(item.kind!=SgWork::Suspend)return true;
    if((pending.pauseOrdinaryWitness&&!sgOrdinaryPauseCurrent(pending))||
       (pending.pauseEntryFileTime&&!sgReturnedPauseCurrent(pending)))return false;
    return !returned||!pending.pauseEntryFileTime||
        (pending.pausePid==item.pausePid&&pending.pauseProcessCreated==item.pauseProcessCreated&&
         pending.pauseWakeFileTime==item.pauseWakeFileTime);
}
static bool sgWorkDeliveryAlreadyQueuedLocked(const SgWorkItem& item,bool returned) {
    for(const auto& pending:g_sgWorkQ)if(sgWorkDeliveryCovers(pending,item,returned))return true;
    for(const auto& pending:g_sgRetainedWorkDelivery)if(pending.order&&sgWorkDeliveryCovers(pending.item,item,returned))return true;
    return false;
}
static bool sgRetainWorkDeliveryLocked(const SgWorkItem& item,bool returned) {
    const auto current=currentPowerGeneration();size_t slot=static_cast<size_t>(item.kind);
    if(returned){slot=!g_sgRetainedWorkDelivery[5].order?5:6;}
    else if(item.generation!=current){
        // A captured target may already have an issued pause while markerOwned
        // is still unpublished. Retain delivery; the original worker alone
        // decides whether that exact lease finally became owned.
        SgSleepTarget lease{};
        if(item.kind!=SgWork::WakeSuspend||!sgReadSleepTarget(lease)||!lease.pid||
           !lease.processCreated||lease.powerGeneration!=item.generation)return false;
        slot=4;
    }
    if(item.kind==SgWork::WakeSuspend&&slot<g_sgRetainedWorkDelivery.size()) {
        const auto sequence=g_sleepPowerDispatch.observedSleepBoundarySequence();
        const auto serial=g_sharedPowerSuspendSerial.load(std::memory_order_acquire);
        if(!item.wakeBoundaryMatches(sequence,serial))return false;
        auto& retained=g_sgRetainedWorkDelivery[slot];
        if(retained.order&&!retained.item.wakeBoundaryMatches(sequence,serial))retained={};
    }
    if(slot>=g_sgRetainedWorkDelivery.size()||g_sgRetainedWorkDelivery[slot].order||g_sgRetainedWorkOrder==~0ull)
        throw std::bad_alloc();
    g_sgRetainedWorkDelivery[slot]={item,g_sgWorkQ.size(),++g_sgRetainedWorkOrder};return true;
}
static bool sgAppendWorkDeliveryLocked(const SgWorkItem& item,bool returned) {
    if(returned)return sgRetainWorkDeliveryLocked(item,true);
    try{g_sgWorkQ.push_back(item);return true;}catch(const std::bad_alloc&){return sgRetainWorkDeliveryLocked(item,false);}
}
static bool sgTakeWorkDeliveryLocked(SgWorkItem& item) {
    for(;;) {
        sgPruneRetainedWorkDeliveryLocked();SgRetainedWorkDelivery* first=nullptr;
        for(auto& pending:g_sgRetainedWorkDelivery)
            if(pending.order&&(!first||pending.order<first->order))first=&pending;
        if(first&&first->ahead==0){item=first->item;*first={};}
        else {
            if(g_sgWorkQ.empty())return false;
            item=g_sgWorkQ.front();g_sgWorkQ.pop_front();
            for(auto& pending:g_sgRetainedWorkDelivery)if(pending.order&&pending.ahead)--pending.ahead;
        }
        if(item.wakeBoundaryMatches(g_sleepPowerDispatch.observedSleepBoundarySequence(),
            g_sharedPowerSuspendSerial.load(std::memory_order_acquire)))return true;
    }
}
static bool sgQueueReturnedPause(const SgWorkItem& item) {
    if(!sgReturnedPauseCurrent(item))return false;
    std::lock_guard<std::mutex> lock(g_sgWorkMx);
    if(g_sgWorkStop||!sgReturnedPauseCurrent(item))return false;
    sgPruneRetainedWorkDeliveryLocked();
    if(sgWorkDeliveryAlreadyQueuedLocked(item,true))return false;
    if(!sgAppendWorkDeliveryLocked(item,true))return false;
    ++g_sgWorkWakeRevision;return true;
}
static void sgNotifyReturnedPauseQueued(const SgWorkItem& item) {
    g_sgWorkCv.notify_one();
    try {sgRecordFact("sleep-game-returned-pause-queued",{
        {"generation",item.generation},{"originalEntry100ns",item.pauseEntryFileTime},
        {"pid",item.pausePid},{"processCreated",std::to_string(item.pauseProcessCreated)},
        {"systemSleepRequested",false},{"steamCaptureReplayed",false},{"polling",false},
        {"mandatoryEvidence",true},{"detailLogsRequired",false}});} catch(...) {}
}
