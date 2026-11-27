// One UI-owned classification witness in the existing Native sleep owner.
// It grants no sleep action, new generation, device ownership or retry budget.
// Workers only read its atomic generation; timers keep their first deadlines.
struct SgResumeClassificationState {
    std::atomic<unsigned long long> generation{0},observedGeneration{0};
    std::atomic<ULONGLONG> retrySuspendBoundary{0};
    ULONGLONG origin=0,attemptTick=0,firstReceiptTick=0,deadlineTick=0,interactiveReceiptTick=0,retrySuspendSequence=0;
    bool entryObservation=false; // UI-only, original short window after resource recovery
    ULONGLONG entryExitFileTime=0;
    int entryExitReason=-1;
};
static SgResumeClassificationState g_sgResumeClassification;
static std::atomic<unsigned long long> g_sgWakeClassificationBlockedGeneration{0};
static bool sharedSleepWakeClassified(unsigned long long generation) noexcept {
    // Pure atomic observation; callable while a Steam consumer holds its mutex.
    // Resuming with a precise final witness remains governed by that consumer.
    return generation && !g_exitRequested.load(std::memory_order_acquire) &&
        generation==g_powerGeneration.load(std::memory_order_acquire) &&
        g_sgResumeClassification.generation.load(std::memory_order_acquire)!=generation &&
        g_sgWakeClassificationBlockedGeneration.load(std::memory_order_acquire)!=generation &&
        generation==g_powerGeneration.load(std::memory_order_acquire);
}
static void sgBlockSharedWakeClassification(unsigned long long generation) {
    if(generation && g_sgWakeClassificationBlockedGeneration.exchange(generation,std::memory_order_acq_rel)!=generation)
        g_recoveryPowerBoundary.revoke();
}
static void sgHoldSharedWakeClassification(unsigned long long generation) {
    g_sgResumeClassification.generation.store(generation,std::memory_order_release);
    g_recoveryPowerBoundary.revoke();
}
static void sgCompleteSharedWakeClassification(unsigned long long generation) {
    if(!generation||generation!=currentPowerGeneration()||g_exitRequested.load(std::memory_order_acquire)||
       sgResumeClassificationOwned(generation))return;
    auto expected=generation;
    const bool completed=g_sgWakeClassificationBlockedGeneration.compare_exchange_strong(expected,0,std::memory_order_acq_rel);
    if(completed&&sharedSleepWakeClassified(generation))
        publishSharedPowerLifecycle("native-wake-classification-ready");
}

static bool sgResumeClassificationOwned(unsigned long long generation) {
    return generation && generation==currentPowerGeneration() &&
        g_sgResumeClassification.generation.load(std::memory_order_acquire)==generation;
}
static bool sgResumeClassificationObserved(unsigned long long generation) {
    return generation && g_sgResumeClassification.observedGeneration.load(std::memory_order_acquire)==generation;
}
static bool sgResumeClassificationHeld(unsigned long long generation) {
    if(!sgResumeClassificationOwned(generation)||g_exitRequested.load(std::memory_order_acquire)||
       !g_guardEnabled||(!g_sgRetryEntryFailure&&!g_sgRetrySleepTimeout)||
       g_sgTask.generation!=generation||!g_sgRepairEligible)return false;
    const auto origin=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    return origin && origin>g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire) &&
        !sgUserWakeCancelsSleepIntent(origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)) &&
        !sgSleepTimeoutSuccessAfter((std::max)(origin,g_sgResumeClassification.retrySuspendBoundary.load(std::memory_order_acquire)));
}
// A completed two-second classification returns resources immediately. Its
// original confirmed entry may still receive display/507 evidence in the same
// thirty-second window. This ticket cannot extend that window or authorize IO.
// A deferred UI operation, not a power state or an additional retry budget.
// Its immutable original/native source is rechecked after the old op returns.
struct SgRetryAdmissionHandoff {
    SgRetryKind kind=SgRetryKind::None;
    unsigned long long generation=0;
    ULONGLONG origin=0,latestEntry=0;
    ymcc::sleeprequest::CompletionWitness success;
    ymcc::sleeppower::SharedPowerV1 power;
};
static SgRetryAdmissionHandoff g_sgRetryAdmissionHandoff; // protected by g_sgWorkMx
static void sgCancelRetryAdmission(SgRetryKind only) {
    std::lock_guard<std::mutex> deliveryLock(g_sgWorkMx);
    if(only==SgRetryKind::None || g_sgRetryAdmissionHandoff.kind==only)
        g_sgRetryAdmissionHandoff={};
}
static bool sgRetryAdmissionHeld(SgRetryKind kind,unsigned long long generation,ULONGLONG origin) {
    std::lock_guard<std::mutex> deliveryLock(g_sgWorkMx);
    const auto& pending=g_sgRetryAdmissionHandoff;
    return kind!=SgRetryKind::None && pending.kind==kind && pending.generation==generation &&
        pending.origin==origin &&
        g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)<=pending.latestEntry &&
        !pending.success.superseded(sgSleepTimeoutSuccessfulExitTime(),sgSleepTimeoutSuccessAfter(origin));
}
static void sgDeferRetryAdmission(SgRetryKind kind,ULONGLONG origin,ymcc::sleeprequest::CompletionWitness success) {
    const auto generation=currentPowerGeneration();
    const auto latestEntry=g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire);
    ymcc::sleeppower::SharedPowerV1 power;
    if(!origin || !generation || g_exitRequested.load(std::memory_order_acquire) ||
       !ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower,power) ||
       power.generation!=generation || (power.flags&ymcc::sleeppower::kNotificationPending) ||
       success.superseded(sgSleepTimeoutSuccessfulExitTime(),sgSleepTimeoutSuccessAfter(origin)))return;
    bool first=false;
    {
        std::lock_guard<std::mutex> deliveryLock(g_sgWorkMx);
        const auto& previous=g_sgRetryAdmissionHandoff;
        if(previous.kind!=SgRetryKind::None){
            // First request for this episode retains its first source witness.
            if(previous.generation>generation ||
               (previous.generation==generation && previous.origin>=origin))return;
        }
        g_sgRetryAdmissionHandoff={kind,generation,origin,latestEntry,success,power};
        first=true;
    }
    sgRequestEntryObservationHandoff();
    if(first)try {sgRecordFact("sleep-retry-admission-deferred",{{"kind",sgRetryKindName(kind)},
        {"generation",generation},{"originalHumanIntent100ns",origin},{"polling",false},
        {"budgetReset",false},{"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
}
static void sgResumeRetryAdmission() {
    SgRetryAdmissionHandoff pending;
    {
        std::lock_guard<std::mutex> deliveryLock(g_sgWorkMx);
        pending=g_sgRetryAdmissionHandoff;
        g_sgRetryAdmissionHandoff={};
    }
    if(pending.kind==SgRetryKind::None)return;
    ymcc::sleeppower::SharedPowerV1 current;
    const auto origin=pending.kind==SgRetryKind::SleepTimeout ? sgSleepTimeoutOriginalIntent() :
        g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    if(g_exitRequested.load(std::memory_order_acquire) || pending.generation!=currentPowerGeneration() ||
       origin!=pending.origin || pending.origin<=g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire) ||
       g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)>pending.latestEntry ||
       sgUserWakeCancelsSleepIntent(origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)) ||
       pending.success.superseded(sgSleepTimeoutSuccessfulExitTime(),sgSleepTimeoutSuccessAfter(origin)) ||
       !ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower,current) ||
       current.generation!=pending.power.generation || current.suspendSerial!=pending.power.suspendSerial ||
       current.observedNotificationSequence!=pending.power.observedNotificationSequence ||
       current.publicationRevision<pending.power.publicationRevision ||
       (current.flags&ymcc::sleeppower::kNotificationPending))return;
    // Ready projection/publication alone does not replace the original source.
    sgStartSleepRetry(pending.kind,"operation-completed-original-retry",&pending.success);
}

static bool sgResumeEntryObservationPresent(unsigned long long generation) {
    if(!g_sgResumeClassification.entryObservation || !generation ||
       generation!=currentPowerGeneration() || !sgResumeClassificationObserved(generation) ||
       sgResumeClassificationOwned(generation) || !g_guardEnabled || !g_sgRetryEntryFailure ||
       g_exitRequested.load(std::memory_order_acquire) || g_sleepPowerDispatch.pendingSuspend())return false;
    const auto origin=g_sgResumeClassification.origin;
    const auto current=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    const auto now=sgNowFileTime();
    const auto phase=g_powerLifecycle.load(std::memory_order_acquire);
    // Resource cleanup may concurrently retire the old task. Observation
    // does not borrow those mutable fields; the actual claim checks the
    // task together under the operation mutex below.
    return !sgSleepRetryActive() &&
        (phase==PowerLifecycle::Ready||phase==PowerLifecycle::Resuming) &&
        g_sgResumeClassification.attemptTick && origin && current==origin &&
        origin>g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire) &&
        now>=origin && now-origin<=SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL &&
        !sgUserWakeCancelsSleepIntent(origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)) &&
        !sgSleepTimeoutSuccessAfter(origin);
}
static bool sgResumeEntryObservationHeld(unsigned long long generation) {
    const auto origin=g_sgResumeClassification.origin;
    return sgResumeEntryObservationPresent(generation) &&
        g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)==origin &&
        g_sgLastKernel506EventFileTime.load(std::memory_order_acquire)==origin;
}
static void sgResumeEntryObservationRememberExit(int reason,ULONGLONG at) {
    if(!sgResumeEntryObservationPresent(currentPowerGeneration()) || reason<0 || reason==1 || reason==20 ||
       !at || at<g_sgResumeClassification.origin || at>sgNowFileTime())return;
    if(at>g_sgResumeClassification.entryExitFileTime){
        g_sgResumeClassification.entryExitFileTime=at;
        g_sgResumeClassification.entryExitReason=reason;
    }
}
static void sgResumeEntryObservationInterrupt(int reason,ULONGLONG at,ULONGLONG attempt) {
    if(!sgSleepRetryActive() || g_sgRetryDueTick==0){
        g_entryFailureInterruptedAt=at;
        g_entryFailureExpanded=reason!=7 && reason!=8;
        g_entryFailureQuickDeadline=g_entryFailureExpanded && attempt
            ? attempt+SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL : 0;
    }
}
static void sgRevokeResumeEntryObservation() {
    {
        std::lock_guard<std::mutex> deliveryLock(g_sgWorkMx);
        if(g_sgRetryAdmissionHandoff.kind==SgRetryKind::None){
            g_sgEntryHandoffPending.store(false,std::memory_order_release);
            g_sgEntryHandoffReady.store(false,std::memory_order_release);
        }
    }
    g_sgResumeClassification.entryObservation=false;
    g_sgResumeClassification.entryExitFileTime=0;
    g_sgResumeClassification.entryExitReason=-1;
}
static bool sgJoinResumeEntryObservation() {
    const auto generation=currentPowerGeneration();
    std::unique_lock<std::mutex> opLock(g_sgOpMtx,std::try_to_lock);
    if(!opLock.owns_lock()) {
        sgRequestEntryObservationHandoff();
        return false;
    }
    if(!sgResumeEntryObservationHeld(generation))return false;
    const bool taskAbsent=g_sgTask.generation==0 && g_sgTask.mode==SgSleepMode::Unknown;
    const bool originalTask=g_sgTask.generation==generation && g_sgTask.mode==SgSleepMode::S3 &&
        g_sgRepairEligible && g_sgSleepTriggerTick==g_sgResumeClassification.attemptTick;
    if(!taskAbsent&&!originalTask)return false;
    const auto origin=g_sgResumeClassification.origin;
    sgRevokeResumeEntryObservation();
    g_sgTask.generation=generation;
    g_sgTask.mode=SgSleepMode::S3;
    g_sgSleepTriggerTick=g_sgResumeClassification.attemptTick;
    g_sgRepairEligible=true;
    // The task and the original retry claim become visible in one critical section.
    g_sgTask.retryKind=SgRetryKind::EntryFailure;
    g_sgTask.retryAttempt=0;
    g_sgRetryInProgress=true;
    sgBlockSharedWakeClassification(generation);
    opLock.unlock();
    try { sgRecordFact("entry-failure-rejoined-after-classification",{{"generation",generation},
        {"originalHumanIntent100ns",origin},
        {"originalDeadline100ns",origin+SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL},
        {"budgetReset",false},{"detailLogsRequired",false},{"mandatoryEvidence",true}}); } catch (...) {}
    return true;
}
static void sgResumeEntryObservationHandoffTick() {
    sgResumeRetryAdmission();
    const auto generation=currentPowerGeneration();
    if(!sgResumeEntryObservationHeld(generation))return;
    const auto origin=g_sgResumeClassification.origin;
    const auto at=g_sgResumeClassification.entryExitFileTime;
    const auto reason=g_sgResumeClassification.entryExitReason;
    if(at && at>=origin && at<=sgNowFileTime() &&
       sgS0EntryFailureEligible(reason,GetTickCount64())) {
        g_sgLastS0WakeReason=reason;
        sgResumeEntryObservationInterrupt(reason,at,origin);
        sgHandleModernStandbyWake(false,at);
        return;
    }
    // These fields were written only after the original display confirmation.
    // Ordinary worker cleanup preserves them; actual cancellation still clears them.
    const auto on=g_entryFailureDisplayOnFileTime;
    const auto now=sgNowFileTime();
    if(g_entryFailureExpanded && on && g_entryFailureInterruptedAt==on &&
       g_entryFailureDisplayOn && g_entryFailureDisplayOffFileTime>=origin &&
       now>=on && now-on>=SG_MONITOR_ENTRY_FAILURE_WINDOW_MS*10000ULL &&
       g_entryFailureQuickDeadline==origin+SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL &&
       g_entryFailureNewOffFileTime<=on && sgEntryFailureWindowEligible(GetTickCount64()))
        sgStartSleepRetry(SgRetryKind::EntryFailure,"display-relit-during-entry");
}
static void sgConsumeEntryObservationHandoffReady(UINT message) {
    if(!g_sgEntryHandoffReady.load(std::memory_order_acquire))return;
    // Power and shutdown facts are processed before any deferred action.
    if(message==WM_POWERBROADCAST || message==kSleepPowerForwardMessage ||
       message==kSharedPowerStateMessage || message==WM_CLOSE || message==WM_DESTROY ||
       message==WM_NCDESTROY || message==WM_QUERYENDSESSION || message==WM_ENDSESSION)return;
    {
        std::lock_guard<std::mutex> deliveryLock(g_sgWorkMx);
        if(!g_sgEntryHandoffReady.exchange(false,std::memory_order_acq_rel))return;
    }
    // Do not hold the queue lock across the nonblocking operation-lock claim.
    sgResumeEntryObservationHandoffTick();
}
static bool sgEntryFailureLifecycleAllowed() {
    const auto phase=g_powerLifecycle.load(std::memory_order_acquire);
    return phase==PowerLifecycle::Suspending||phase==PowerLifecycle::Suspended||
        ((phase==PowerLifecycle::Ready||phase==PowerLifecycle::Resuming)&&
         (sgResumeClassificationHeld(currentPowerGeneration())||
          sgResumeEntryObservationHeld(currentPowerGeneration())));
}
static void sgStopResumeClassificationWait(const char* reason) {
    sgRevokeResumeEntryObservation();
    const auto generation=g_sgResumeClassification.generation.exchange(0,std::memory_order_acq_rel);
    if(g_hwnd)KillTimer(g_hwnd,SG_RESUME_CLASSIFY_TIMER_ID);
    sgSleepTimeoutClassificationComplete(generation);
    if(generation)try{sgRecordFact("resume-classification-stopped",{{"generation",generation},
        {"reason",reason?reason:"unknown"},{"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
}
// Session-display edges are UI-thread observations, not another wake authority.
// A second generation change requires a continuing kernel session and a final exit.
static struct SgSleepDisplayEvidence {
    ULONGLONG off=0,on=0,offGeneration=0,onGeneration=0,reportedIntent=0;
    bool reportedExit=false;
} g_sgSleepDisplayEvidence;

static void sgReportSleepDisplayRelight() noexcept;

static void sgObserveSleepSessionDisplay(int previous,DWORD state) noexcept {
    if(state>2 || previous==static_cast<int>(state) || !sgModernStandbyRepairSupported() ||
       g_exitRequested.load(std::memory_order_acquire))return;
    auto& display=g_sgSleepDisplayEvidence;
    const auto generation=currentPowerGeneration();
    const auto now=sgNowFileTime();
    if(state==0){
        display={};
        if(previous>=0){display.off=now;display.offGeneration=generation;}
    }else if(state==1 && previous==0 && display.off && display.off<now && display.offGeneration &&
             display.offGeneration<=generation && generation-display.offGeneration<=2){
        display.on=now;
        display.onGeneration=generation;
        sgReportSleepDisplayRelight();
    }else display={};
}

static void sgFinishResumeClassification(const char* reason,ULONGLONG acceptedButtonWake) {
    const auto generation=g_sgResumeClassification.generation.load(std::memory_order_acquire);
    if(!generation){sgRevokeResumeEntryObservation();return;}
    const auto interactive=g_sgResumeClassification.interactiveReceiptTick;
    const auto origin=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    const auto buttonWake=g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire);
    const bool manual=sgUserWakeCancelsSleepIntent(origin,buttonWake);
    // Diagnostic only: accepted507 carries THIS message's original time,
    // including zero when unavailable. Collection time and a newer callback
    // cannot replace it; the existing manual/action predicates stay unchanged.
    const auto receiptOrigin=g_sgResumeClassification.origin;
    const auto originalButtonWake=g_sgOriginalPowerButtonWakeFileTime.load(std::memory_order_acquire);
    const bool acceptedButtonMessage=reason && strcmp(reason,"power-button-wake")==0;
    const auto receiptWake=acceptedButtonMessage ? acceptedButtonWake :
        (manual && originalButtonWake==buttonWake ? originalButtonWake : 0);
    const bool confirmedUserWake=receiptWake && receiptWake<=sgNowFileTime() &&
        (!origin || manual) && sgUserWakeCancelsSleepIntent(receiptOrigin,receiptWake);
    sgStopResumeClassificationWait(reason);
    if(generation!=currentPowerGeneration()||g_exitRequested.load(std::memory_order_acquire))return;
    if(sgSleepRetryActive()){
        if(manual)sgAbortSleepIntent(reason, receiptWake);
        return;
    }
    if(manual){g_sgAutomaticSleepBudget.confirmedUserWake();sgSleepTimeoutStop("classified-user-wake");}
    else if(sgSleepTimeoutSuccessAfter(origin))sgSleepTimeoutStop("classified-windows-success");
    g_sgModernStandbyActive=false;
    g_sgModernWakeClassified=true;
    g_sgModernWakeClassifiedTick=GetTickCount64();
    sgClearInternalSleepRequest(reason);
    const bool timeoutRetained=!manual&&!sgSleepTimeoutSuccessAfter(origin)&&sgSleepTimeoutSettleClassification(generation);
    if(!manual && reason && strcmp(reason,"no-entry-failure-evidence")==0) {
        g_sgResumeClassification.entryObservation=true;
        if(!sgResumeEntryObservationPresent(generation))sgRevokeResumeEntryObservation();
    }
    const bool entryRetained=sgResumeEntryObservationPresent(generation);
    // A successful original sleep can precede an unexpected wake. Only newer
    // evidence can supersede the separately admitted re-sleep.
    const bool retryAdmissionRetained=!manual &&
        sgNonUserWakeGuardEnabled() && sgRetryAdmissionHeld(SgRetryKind::UnexpectedWake,generation,origin);
    if(!retryAdmissionRetained)sgClearUnexpectedWake(reason,!timeoutRetained&&!entryRetained);
    sleepRecoveryAutoCloseSuspendEpisode();
    // Reuse the actual interactive receipt, if present; observation expiry
    // never manufactures a power-button witness or refunds an action budget.
    if(interactive)sleepRecoveryAutoInteractiveResume(generation,PBT_APMRESUMESUSPEND,interactive);
    sgRecordFact("resume-classification-complete",{{"generation",generation},{"reason",reason?reason:"unknown"},
        {"confirmedUserWake",confirmedUserWake},{"originalHumanIntent100ns",receiptOrigin},
        {"powerButtonWake100ns",receiptWake},{"observedPowerButtonWake100ns",buttonWake},
        {"timeoutObservationRetained",timeoutRetained},
        {"entryObservationRetained",entryRetained},{"retryAdmissionRetained",retryAdmissionRetained},{"powerActionDispatched",false},
        {"detailLogsRequired",false},{"mandatoryEvidence",true}});
    // Final resource recovery uses the original common owner and exact lease.
    handlePowerResumeNotification(SgWork::WakeSuspend,reason);
    // Input Ready may already have been de-duplicated. Notify the existing
    // deferred software consumer in this same generation, without a new budget.
    if(sharedSleepWakeClassified(generation))
        publishSharedPowerLifecycle("native-wake-classification-complete");
}
static bool sgDeferModernResume(ULONG type,ULONGLONG receiptTick) {
    if(type!=PBT_APMRESUMEAUTOMATIC&&type!=PBT_APMRESUMESUSPEND)return false;
    if(!sgModernStandbyRepairSupported()||sgAutomaticClassicSleepPending()||
       !g_guardEnabled||g_exitRequested.load(std::memory_order_acquire))return false;
    // Original506 may already be observed by its callback while GUI delivery
    // is queued. Refine the fallback before comparing the human veto.
    sgRefreshSleepTriggerOrigin();
    const auto generation=currentPowerGeneration();
    const auto original=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    // The first resume can trail an unissued timeout scheduler. It still
    // owns the original classification grace; accepted requests keep old handling.
    const bool unissuedTimeout=sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::SleepTimeout&&
        g_sgTask.retryAttempt==0&&g_sgRetryDueTick!=0;
    if(sgSleepRetryActive()&&!unissuedTimeout&&original&&
       !sgUserWakeCancelsSleepIntent(original,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))&&
       !sgSleepTimeoutSuccessAfter(original))return true; // the accepted retry owns classification
    if(sgResumeClassificationOwned(generation)){
        if(!sgResumeClassificationHeld(generation)){sgStopResumeClassificationWait("known-veto-or-retired");return false;}
        if(type==PBT_APMRESUMESUSPEND&&!g_sgResumeClassification.interactiveReceiptTick)
            g_sgResumeClassification.interactiveReceiptTick=receiptTick;
        return true;
    }
    if((sgResumeClassificationObserved(generation)&&g_sgResumeClassification.attemptTick==g_sgSleepTriggerTick)||(sgSleepRetryActive()&&!unissuedTimeout)||
       (!g_sgRetryEntryFailure&&!g_sgRetrySleepTimeout)||g_sgTask.generation!=generation||
       g_sgTask.mode!=SgSleepMode::S3||!g_sgRepairEligible||!g_sgSleepTriggerTick)return false;
    const auto origin=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    const auto now=sgNowFileTime();
    // A confirmed entry-failure retry can wake much later than the original
    // button window. A completion from before its Suspend cannot classify it.
    const auto suspend=g_sleepPowerDispatch.latestSleepBoundary();
    const auto& request=g_sgInternalSleepRequest;
    const auto retrySuspendBoundary=!sgSleepRetryActive() &&
        request.kind==SgRetryKind::EntryFailure && request.dispatchFileTime>origin &&
        request.dispatchTick && suspend && suspend.type==PBT_APMSUSPEND &&
        suspend.tick>=request.dispatchTick && suspend.tick<=receiptTick &&
        suspend.fileTime100ns>=request.dispatchFileTime && suspend.fileTime100ns<=now
        ? suspend.fileTime100ns : 0;
    if(!origin||now<origin||(now-origin>SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL&&!retrySuspendBoundary&&!sgSleepTimeoutObservationHeld(generation))||
       origin<=g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire)||
       sgUserWakeCancelsSleepIntent(origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))||
       sgSleepTimeoutSuccessAfter((std::max)(origin,retrySuspendBoundary)))return false;
    if(!receiptTick)return false; // Unknown ingress time cannot be rejuvenated.
    const auto first=receiptTick;
    g_sgResumeClassification.origin=origin;
    g_sgResumeClassification.attemptTick=g_sgSleepTriggerTick;
    g_sgResumeClassification.firstReceiptTick=first;
    g_sgResumeClassification.deadlineTick=first+SG_NON_USER_WAKE_CLASSIFY_MS;
    g_sgResumeClassification.interactiveReceiptTick=type==PBT_APMRESUMESUSPEND?receiptTick:0;
    g_sgResumeClassification.retrySuspendBoundary.store(retrySuspendBoundary,std::memory_order_release);
    g_sgResumeClassification.retrySuspendSequence=retrySuspendBoundary?suspend.sequence:0;
    g_sgResumeClassification.observedGeneration.store(generation,std::memory_order_release);
    sgHoldSharedWakeClassification(generation);
    const auto tick=GetTickCount64();
    const auto delay=tick<g_sgResumeClassification.deadlineTick?g_sgResumeClassification.deadlineTick-tick:1ULL;
    if(!g_hwnd||!SetTimer(g_hwnd,SG_RESUME_CLASSIFY_TIMER_ID,static_cast<UINT>(delay),nullptr)){
        sgStopResumeClassificationWait("timer-registration-failed");return false;
    }
    // A diagnostics failure must not unwind an already-registered classification.
    try{sgRecordFact("resume-classification-pending",{{"generation",generation},{"code",type},
        {"originalHumanIntent100ns",origin},{"firstReceiptTickMs",first},
        {"deadlineTickMs",g_sgResumeClassification.deadlineTick},{"observationOnly",true},
        {"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
    return true;
}
// Refine only this already-admitted original receipt. A late506 cannot
// allocate a generation, replay game pause or refund an action/deadline.
static bool sgJoinResumeEntryObservationOriginal(int reason,ULONGLONG original) {
    const auto generation=currentPowerGeneration();
    if((reason!=1&&reason!=3) || !original)return false;
    const auto observed=g_sgResumeClassification.origin;
    if(!sgResumeEntryObservationPresent(generation)){
        // Retired/disabled/expired observations cannot turn their own delayed
        // original506 into a new power cycle. The independent timeout keeps
        // its existing late-original join; a newer genuine entry still passes.
        if(sgResumeClassificationObserved(generation) && !sgResumeClassificationOwned(generation) &&
           !sgSleepRetryActive() && observed && original<=observed &&
           observed-original<=SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL &&
           !sgSleepTimeoutObservationHeld(generation)){
            sgRecordFact("s0-intent-ignored",{{"reason","retired-entry-observation-original"},
                {"generation",generation},{"eventFileTime100ns",original},
                {"observedOrigin100ns",observed},{"detailLogsRequired",false},{"mandatoryEvidence",true}});
            return true;
        }
        return false;
    }
    const auto now=sgNowFileTime();
    if(original>observed || observed-original>SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL ||
       now<original || now-original>SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL ||
       original<=g_sgClosedSleepIntentFileTime.load(std::memory_order_acquire) ||
       g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)!=original ||
       g_sgLastKernel506EventFileTime.load(std::memory_order_acquire)!=original ||
       sgUserWakeCancelsSleepIntent(original,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire)) ||
       sgSleepTimeoutSuccessAfter(original))return false;
    (void)sgSleepTimeoutJoinEntry(reason,original);
    g_sgResumeClassification.origin=original;
    g_sgLastPowerButtonSleepIntentFileTime.store(original,std::memory_order_release);
    sgSleepTimeoutArm(reason,original);
    try {sgRecordFact("entry-failure-observation-origin-refined",{{"generation",generation},
        {"originalHumanIntent100ns",original},{"originalDeadline100ns",original+SG_S0_ENTRY_FAILURE_WINDOW_MS*10000ULL},
        {"entryReason",reason},{"budgetReset",false},{"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
    sgArmMonitorEntryFailureTimer("late-original-entry-after-classification");
    const auto at=g_sgResumeClassification.entryExitFileTime;
    const int exitReason=g_sgResumeClassification.entryExitReason;
    if(at && at>=original && at<=now && sgS0EntryFailureEligible(exitReason,GetTickCount64())){
        g_sgLastS0WakeReason=exitReason;
        sgStopMonitorEntryFailureTimer("joined-original-failed-exit");
        sgResumeEntryObservationInterrupt(exitReason,at,original);
        sgHandleModernStandbyWake(false,at);
    }
    return true;
}
static bool sgJoinPendingResumeEntry(int reason,ULONGLONG original) {
    const auto generation=currentPowerGeneration();
    const bool waiting=sgResumeClassificationOwned(generation);
    const bool classifiedRetry=sgResumeClassificationObserved(generation)&&sgSleepRetryActive()&&
        g_sgTask.generation==generation;
    if((reason!=1&&reason!=3)||!original||(!waiting&&!classifiedRetry))return false;
    if(original>g_sgResumeClassification.origin){
        sgStopResumeClassificationWait("newer-user-entry");
        g_sgModernStandbyActive=false;return false;
    }
    g_sgLastPowerButtonSleepIntentFileTime.store(original,std::memory_order_release);
    g_sgResumeClassification.origin=original;
    if(classifiedRetry){
        // Original506 GUI may trail its own failed507 and the admitted retry.
        // Join its original identity without replacing kind/attempt or deadline.
        sgSleepTimeoutArm(reason,original);
        sgRecordFact("s0-intent-joined-classified-retry",{{"generation",generation},
            {"originalHumanIntent100ns",original},{"detailLogsRequired",false},{"mandatoryEvidence",true}});
        return true;
    }
    // Refinement is the same window, so retain its first display deadline.
    if(g_monitorEntryFailureTimerArmed&&g_monitorEntryFailureGeneration==generation)
        g_monitorEntryFailureIntent=original;
    g_sgModernStandbyActive=true;
    g_sgModernStandbyGeneration=generation;
    sgMarkUserStandby("kernel-power-506-pending-resume",reason,original);
    sgSleepTimeoutArm(reason,original);
    sgArmMonitorEntryFailureTimer("pending-resume-entry-refined");
    sgRecordFact("s0-intent-joined-pending-resume",{{"generation",generation},
        {"originalHumanIntent100ns",original},{"deadlineTickMs",g_sgResumeClassification.deadlineTick},
        {"detailLogsRequired",false},{"mandatoryEvidence",true}});
    return true;
}
static void sgResumeClassificationTick() {
    if(g_hwnd)KillTimer(g_hwnd,SG_RESUME_CLASSIFY_TIMER_ID);
    const auto generation=g_sgResumeClassification.generation.load(std::memory_order_acquire);
    if(!generation)return;
    if(generation!=currentPowerGeneration()||g_exitRequested.load(std::memory_order_acquire)){
        sgStopResumeClassificationWait("retired-generation-or-exit");return;
    }
    if(!sgResumeClassificationHeld(generation)){
        sgFinishResumeClassification("classification-veto-or-disabled");return;
    }
    const auto tick=GetTickCount64();
    if(tick<g_sgResumeClassification.deadlineTick){
        if(!SetTimer(g_hwnd,SG_RESUME_CLASSIFY_TIMER_ID,
            static_cast<UINT>(g_sgResumeClassification.deadlineTick-tick),nullptr))
            sgFinishResumeClassification("classification-timer-failed");
        return;
    }
    // A real OFF->ON window already owns five seconds of continuous display
    // evidence. Join its remaining time, never restart or shorten that window.
    sgArmMonitorEntryFailureTimer("resume-classification-display-handoff");
    const auto now=sgNowFileTime();
    if(g_monitorEntryFailureTimerArmed&&g_monitorEntryFailureGeneration==generation&&
       g_monitorEntryFailureIntent==g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire)){
        if(now>=g_monitorEntryFailureDeadline){sgMonitorEntryFailureTick();}
        else {
            const auto remaining=(g_monitorEntryFailureDeadline-now+9999ULL)/10000ULL;
            if(SetTimer(g_hwnd,SG_RESUME_CLASSIFY_TIMER_ID,static_cast<UINT>(remaining),nullptr))return;
        }
    }
    if(sgSleepRetryActive()){sgStopResumeClassificationWait("classified-entry-retry");return;}
    sgFinishResumeClassification("no-entry-failure-evidence");
}

static bool sgIgnorePowerReturnBeforeCurrentIntent(const ymcc::sleeppower::Dispatch::Receipt& receipt) {
    const bool queryRejected=receipt.type==PBT_APMQUERYSUSPENDFAILED||receipt.type==PBT_APMQUERYSTANDBYFAILED;
    if(!queryRejected&&receipt.type!=PBT_APMRESUMEAUTOMATIC&&receipt.type!=PBT_APMRESUMESUSPEND&&
       receipt.type!=PBT_APMRESUMECRITICAL)return false;
    const auto origin=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    if(!origin||g_sgTask.generation!=currentPowerGeneration()||!g_sgRepairEligible)return false;
    const auto entry=g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire);
    // A callback predating a newer ORIGINAL entry is stale. UI processing time
    // is not the sleep origin: a real wake can precede delayed suspend handling.
    const bool beforeOriginal=entry==origin&&receipt.fileTime100ns&&receipt.fileTime100ns<entry;
    // An ingress received before the current attempt was prepared cannot
    // describe that attempt. A later tick alone does not establish ownership.
    const bool beforeAttempt=queryRejected&&receipt.tick&&sgSleepRetryActive()&&
        g_sgInternalSleepRequest.kind==g_sgTask.retryKind&&receipt.tick<g_sgInternalSleepRequest.dispatchTick;
    const bool missingForwardTime=receipt.callback&&(!receipt.tick||(queryRejected&&!receipt.fileTime100ns));
    if(!beforeOriginal&&!beforeAttempt&&!missingForwardTime)return false;
    try{sgRecordFact(queryRejected?"sleep-query-rejection-ignored":"power-resume-ignored",{{"reason",
        beforeOriginal?"receipt-before-current-original-entry":beforeAttempt?"receipt-before-current-attempt":"forwarded-receipt-time-unknown"},
        {"generation",currentPowerGeneration()},{"receiptSequence",receipt.sequence},
        {"receiptFileTime100ns",receipt.fileTime100ns},{"originalHumanIntent100ns",origin},
        {"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
    return true;
}
