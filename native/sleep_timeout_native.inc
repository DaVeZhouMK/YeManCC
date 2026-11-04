// Included by main.cpp after the existing sleep retry lifecycle. All state
// mutations and timers are UI-thread owned; the completed507 witness is atomic.
static ymcc::sleeptimeout::Window g_sgSleepTimeoutWindow;
static std::atomic<ULONGLONG> g_sgSleepTimeoutSuccessfulExit{0};

static void sgSleepTimeoutStop(const char* reason) noexcept {
    const bool active=g_sgSleepTimeoutWindow.active();
    const auto origin=g_sgSleepTimeoutWindow.origin;
    g_sgSleepTimeoutWindow.retire();
    if(g_hwnd)KillTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID);
    if(active)try {sgRecordFact("sleep-timeout-stopped",{{"reason",reason?reason:"unknown"},
        {"originalHumanIntent100ns",origin},{"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
}
static ULONGLONG sgSleepTimeoutOriginalIntent() {
    return g_sgSleepTimeoutWindow.active()?g_sgSleepTimeoutWindow.origin:0;
}
static bool sgSleepTimeoutCurrent() {
    const auto origin=g_sgSleepTimeoutWindow.origin;
    const auto inherited=g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire);
    return g_sgSleepTimeoutWindow.active()&&origin&&g_guardEnabled&&g_sgRetrySleepTimeout&&
        !g_exitRequested.load(std::memory_order_acquire)&&
        // The independent old wake guard may retire its own marker after an
        // early automatic wake. It cannot erase this separately enabled deadline.
        (inherited==0||inherited==origin)&&
        !sgUserWakeCancelsSleepIntent(origin,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))&&
        g_sgSleepTimeoutSuccessfulExit.load(std::memory_order_acquire)<origin;
}
static bool sgSleepTimeoutDispatchPermitted() {
    // Read the applicable AC/DC action only at arm/deadline/actual request.
    // Recheck callback-side veto after that OS read and immediately before dispatch.
    return g_sgSleepTimeoutWindow.requested&&sgSleepTimeoutCurrent()&&
        sgPowerButtonSleepMode()==SgSleepMode::S3&&sgSleepTimeoutCurrent();
}
static void sgSleepTimeoutArm(int reason,ULONGLONG original) {
    // Only original physical-button506 Reason1 can arm this independent feature.
    // Programmatic506, lid/idle, screen-off, S4 and unknown actions cannot arm it.
    if(reason!=1||!original||!g_guardEnabled||!g_sgRetrySleepTimeout||
        g_exitRequested.load(std::memory_order_acquire)||
        g_sgLastPowerButtonSleepIntentFileTime.load(std::memory_order_acquire)!=original||
        g_sgLatestUserSleepEntryFileTime.load(std::memory_order_acquire)!=original||
        sgPowerButtonSleepMode()!=SgSleepMode::S3)return;
    const auto now=sgNowFileTime();
    if(sgUserWakeCancelsSleepIntent(original,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire))||
       g_sgSleepTimeoutSuccessfulExit.load(std::memory_order_acquire)>=original)return;
    if(!g_sgSleepTimeoutWindow.arm(original,now))return;
    const auto wait=(std::max)(1ULL,static_cast<ULONGLONG>(g_sgSleepTimeoutWindow.remainingMs(now)));
    if(!g_hwnd||!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(wait),nullptr)) {
        sgSleepTimeoutStop("timer-registration-failed");return;
    }
    // REQUIRED ordinary evidence: do not move behind detailed/high-frequency logging.
    // Disk failure cannot grant, repeat or block a power action.
    try {sgRecordFact("sleep-timeout-armed",{{"originalHumanIntent100ns",original},
        {"deadlineMs",ymcc::sleeptimeout::Window::delayMs},{"remainingMs",wait},
        {"policy","user-selected-elapsed-time-fallback"},{"physicalPowerAssessment","UNKNOWN"},
        {"detailLogsRequired",false},{"mandatoryEvidence",true},{"polling",false}});}catch(...){}
}
static void sgSleepTimeoutRecordSystemSuccess(ULONGLONG at) noexcept {
    if(!at)return;
    auto previous=g_sgSleepTimeoutSuccessfulExit.load(std::memory_order_acquire);
    while(at>previous&&!g_sgSleepTimeoutSuccessfulExit.compare_exchange_weak(previous,at,std::memory_order_acq_rel)){}
}
static void sgSleepTimeoutObserveCompleted(const json& evidence,ULONGLONG at) noexcept {
    // Reuse already-decoded ordinary507 data as a success veto only. Zero/missing
    // DRIPS never means failure and is never required to arm the elapsed policy.
    try {
        if(!at||evidence.value("windowsSleepStage",std::string{})!="entered")return;
        const auto& values=evidence.at("values");
        if(!values.contains("DurationInUs"))return;
        const auto duration=values.at("DurationInUs").get<ULONGLONG>();
        const auto sw=values.value("DripsResidencyInUs",0ULL),hw=values.value("HwDripsResidencyInUs",0ULL);
        if(!duration||sw>duration||hw>duration||(!sw&&!hw))return;
        sgSleepTimeoutRecordSystemSuccess(at);
    }catch(...){}
}
static void sgSleepTimeoutPowerBroadcast(ULONG code) {
    if(!g_sgSleepTimeoutWindow.active())return;
    if(code==PBT_APMRESUMESUSPEND||code==PBT_APMRESUMECRITICAL)
        sgSleepTimeoutStop("interactive-or-critical-resume");
    else if(code==PBT_APMSUSPEND&&(g_sgSleepTimeoutWindow.requested||!sgModernStandbyRepairSupported()))
        sgSleepTimeoutStop("windows-suspend-confirmed");
}
static void sgSleepTimeoutTick() {
    if(g_hwnd)KillTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID);
    if(!g_sgSleepTimeoutWindow.waiting)return;
    if(!sgSleepTimeoutCurrent()||sgPowerButtonSleepMode()!=SgSleepMode::S3||!sgSleepTimeoutCurrent()) {
        sgSleepTimeoutStop("intent-setting-wake-or-success-veto");return;
    }
    const auto now=sgNowFileTime();
    if(now<g_sgSleepTimeoutWindow.origin){sgSleepTimeoutStop("clock-rollback");return;}
    if(!g_sgSleepTimeoutWindow.due(now)) {
        const auto wait=(std::max)(1ULL,static_cast<ULONGLONG>(g_sgSleepTimeoutWindow.remainingMs(now)));
        if(!g_hwnd||!SetTimer(g_hwnd,SG_SLEEP_TIMEOUT_TIMER_ID,static_cast<UINT>(wait),nullptr))
            sgSleepTimeoutStop("timer-registration-failed");
        return;
    }
    if(!g_sgSleepTimeoutWindow.request(now))return;
    try {sgRecordFact("sleep-timeout-due",{{"originalHumanIntent100ns",g_sgSleepTimeoutWindow.origin},
        {"elapsedMs",(now-g_sgSleepTimeoutWindow.origin)/10000},
        {"api","SetSuspendState"},{"arguments",json::array({false,false,false})},
        {"physicalPowerAssessment","UNKNOWN"},{"detailLogsRequired",false},{"mandatoryEvidence",true}});}catch(...){}
    if(!sgSleepTimeoutDispatchPermitted()){sgSleepTimeoutStop("final-deadline-veto");return;}
    // One shared transaction and cumulative budget; no duplicate sender, no
    // unconditional burst, no reset when an older accepted retry was stalled.
    if(sgSleepRetryActive()) {
        g_sgTask.retryKind=SgRetryKind::SleepTimeout;
        sgAdvanceSleepRetry("sleep-timeout-240s");
    } else sgStartSleepRetry(SgRetryKind::SleepTimeout,"sleep-timeout-240s");
}
