// UI-owned, event-driven scheduler accounting. No timers, threads, ETW,
// powercfg, hardware reads or power requests. Do not gate ordinary evidence on
// detail logging: the whole-cycle deltas are needed even if no event ran inside
// the black-screen interval. Three intermediate records at most, >=30s apart.
static ymcc::sleepactivity::Window g_sleepActivityWindow;
static ULONGLONG g_sleepActivityPendingIntent=0;
static ULONGLONG sgSleepActivityFileTime(const FILETIME& f) {
    return (static_cast<ULONGLONG>(f.dwHighDateTime)<<32)|f.dwLowDateTime;
}
static ymcc::sleepactivity::Sample sgSleepActivityCapture() {
    ymcc::sleepactivity::Sample s;
    s.tickMs=GetTickCount64();s.fileTime=sgNowFileTime();
    FILETIME idle{},kernel{},user{},created{},exited{},pk{},pu{};
    s.systemKnown=GetSystemTimes(&idle,&kernel,&user)!=FALSE;
    if(s.systemKnown){s.idle100ns=sgSleepActivityFileTime(idle);s.kernel100ns=sgSleepActivityFileTime(kernel);s.user100ns=sgSleepActivityFileTime(user);}
    else s.systemError=GetLastError();
    s.processKnown=GetProcessTimes(GetCurrentProcess(),&created,&exited,&pk,&pu)!=FALSE;
    if(s.processKnown){s.processCreated100ns=sgSleepActivityFileTime(created);s.processKernel100ns=sgSleepActivityFileTime(pk);s.processUser100ns=sgSleepActivityFileTime(pu);}
    else s.processError=GetLastError();
    ULONGLONG unbiased=0;s.unbiasedKnown=QueryUnbiasedInterruptTime(&unbiased)!=FALSE;
    if(s.unbiasedKnown)s.unbiased100ns=unbiased;else s.unbiasedError=GetLastError();
    s.processorGroups=GetActiveProcessorGroupCount();
    s.logicalProcessors=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    const auto end=GetTickCount64();s.captureCostMs=end>=s.tickMs?end-s.tickMs:0;
    return s;
}
static void sgSleepActivityRecord(const char* boundary,const char* source,ULONGLONG eventTime,int reason,
                                 const ymcc::sleepactivity::Sample& s,const ymcc::sleepactivity::Sample* previous=nullptr) {
    const auto& window=g_sleepActivityWindow;
    json detail={{"schemaVersion",1},{"boundary",boundary},{"source",source?source:"unknown"},
        {"originalHumanIntent",sgFormatFactFileTime(window.intent)},{"originalHumanIntent100ns",window.intent},
        {"generation",window.generation},{"currentGeneration",currentPowerGeneration()},
        {"eventFileTime",eventTime?json(sgFormatFactFileTime(eventTime)):json(nullptr)},
        {"eventReason",reason},{"observed",ymcc::sleepactivity::raw(s)},
        {"cachedDisplay",{{"sessionState",g_idleSessionDisplayState.load(std::memory_order_acquire)},
            {"legacyMonitorState",g_idleLegacyMonitorState.load(std::memory_order_acquire)},
            {"scope","cached-Windows-notifications;freshness-unverified;not-a-sleep-witness"}}},
        {"powerNotifications",g_sleepNotificationEvidence.snapshot()},
        {"sinceBegin",ymcc::sleepactivity::interval(window.first,s)},
        {"sincePrevious",ymcc::sleepactivity::interval(previous?*previous:window.last,s)},
        {"observationsSeen",window.seen},{"suppressedObservations",window.suppressed},
        {"checkpoints",window.checkpoints},
        {"maxRecordsPerCycle",ymcc::sleepactivity::Window::maxRecords},
        {"sampling","existing-UI-power-events-only;no-polling-or-wake-timer"},
        {"eventDeliveryAgeMs",eventTime&&s.fileTime>=eventTime?json((s.fileTime-eventTime)/10000):json(nullptr)},
        {"observationMayIncludeResumeWork",true},{"unbiasedTimeIsS0SleepWitness",false},
        {"eventTimeAssociation","507-window checkpoints use latest callback high-water; not exact window-message identity"},
        {"physicalPowerAssessment","UNKNOWN"},{"powerActionAuthorized",false},
        {"detailLogsRequired",false},{"mandatoryEvidence",true}};
    sgRecordFact("sleep-activity-evidence",detail);
}
static void sgSleepActivityFinishCaptured(const char* source,ULONGLONG eventTime,int reason,
                                         const ymcc::sleepactivity::Sample& s) {
    if(!g_sleepActivityWindow.active())return;
    try {sgSleepActivityRecord("end",source,eventTime,reason,s);} catch (...) {}
    g_sleepActivityWindow.retire();
    g_sleepActivityPendingIntent=0;
}
static void sgSleepActivityEnd(const char* source,ULONGLONG eventTime=0,int reason=-1) noexcept {
    g_sleepActivityPendingIntent=0;
    if(!g_sleepActivityWindow.active())return;
    try {sgSleepActivityFinishCaptured(source,eventTime,reason,sgSleepActivityCapture());}
    catch (...) {g_sleepActivityWindow.retire();}
}
static void sgSleepActivityBegin(int reason,ULONGLONG eventTime) {
    if(reason!=1||!g_guardEnabled||g_exitRequested.load(std::memory_order_acquire)||
        !sgModernStandbyRepairSupported())return;
    auto& window=g_sleepActivityWindow;
    const auto gen=currentPowerGeneration();
    if(window.active()&&window.generation!=gen)sgSleepActivityEnd("generation-superseded");
    const auto wake=g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire);
    if(!window.admits(eventTime,gen,wake,sgNowFileTime()))return;
    const auto s=sgSleepActivityCapture();
    // A callback may confirm the user's wake during these cached OS reads.
    if(!g_guardEnabled||g_exitRequested.load(std::memory_order_acquire)||gen!=currentPowerGeneration()||
        !window.admits(eventTime,gen,g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire),sgNowFileTime()))return;
    window.begin(eventTime,gen,s);
    try {sgSleepActivityRecord("begin","power-button-506",eventTime,reason,s);}
    catch (...) {window.retire();}
}
static void sgSleepActivityObserve(const char* source,ULONGLONG eventTime=0,int reason=-1) {
    auto& window=g_sleepActivityWindow;
    if(!window.active())return;
    const auto wake=g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire);
    if(wake>=window.intent){sgSleepActivityEnd("power-button-wake",wake,1);return;}
    if(!g_guardEnabled||g_exitRequested.load(std::memory_order_acquire)||window.generation!=currentPowerGeneration()) {
        sgSleepActivityEnd("context-ended",eventTime,reason);return;
    }
    if(!window.observe(eventTime,GetTickCount64()))return;
    const auto s=sgSleepActivityCapture();
    const auto finalWake=g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire);
    if(finalWake>=window.intent){sgSleepActivityFinishCaptured("power-button-wake",finalWake,1,s);return;}
    if(!g_guardEnabled||g_exitRequested.load(std::memory_order_acquire)||window.generation!=currentPowerGeneration()) {
        sgSleepActivityFinishCaptured("context-ended",eventTime,reason,s);return;
    }
    // A stale original Windows event cannot claim a current-cycle sample.
    const auto previous=window.last;
    // Reserve the bounded slot before logging: a disk/allocation failure must
    // not turn a repeated notification into unbounded sampling attempts.
    window.checkpoint(s);
    try {sgSleepActivityRecord("checkpoint",source,eventTime,reason,s,&previous);} catch (...) {}
}
[[maybe_unused]] static json sgSleepActivityStatus() {
    const auto& w=g_sleepActivityWindow;
    return {{"schemaVersion",1},{"supported",true},{"active",w.active()},{"generation",w.generation},
        {"originalHumanIntent100ns",w.intent},{"observationsSeen",w.seen},{"suppressedObservations",w.suppressed},
        {"checkpoints",w.checkpoints},{"maxRecordsPerCycle",ymcc::sleepactivity::Window::maxRecords},
        {"minimumEventSpacingMs",ymcc::sleepactivity::Window::minimumEventSpacingMs},
        {"sampling","existing-UI-power-events-only;no-polling-or-wake-timer"},
        {"detailLogsRequired",false},{"mandatoryEvidence",true},{"powerActionAuthorized",false},
        {"physicalPowerAssessment","UNKNOWN"}};
}
// These facts are emitted on the UI power path after its existing admission.
// Pending identity is diagnostic-only: no OS reads until that path accepts the
// intent and resolves its generation. No observation feeds a sleep decision.
static void sgSleepActivityOnFact(const char* event,const json& details) {
    if(!event||!details.is_object())return;
    if(std::strcmp(event,"user-initiated-sleep")==0) {
        g_sleepActivityPendingIntent=details.value("reason",-1)==1?details.value("fileTime100ns",0ULL):0;
        return;
    }
    if(std::strcmp(event,"s0-intent-accepted")==0||std::strcmp(event,"s0-intent-joined-existing-suspend")==0||
       std::strcmp(event,"s0-intent-joined-delayed-resume")==0) {
        const auto original=g_sleepActivityPendingIntent;g_sleepActivityPendingIntent=0;
        if(original)sgSleepActivityBegin(1,original);
        return;
    }
    if(std::strcmp(event,"s0-intent-ignored")==0){g_sleepActivityPendingIntent=0;return;}
    if(std::strcmp(event,"s0-wake-message")==0) {
        if(details.value("acceptedUserWake",false)) {
            g_sleepActivityPendingIntent=0;
            const auto wake=g_sgLastPowerButtonWakeEventFileTime.load(std::memory_order_acquire);
            if(g_sleepActivityWindow.active()&&wake>=g_sleepActivityWindow.intent)sgSleepActivityEnd("power-button-wake",wake,1);
        } else sgSleepActivityObserve("kernel-power-507-window-message",details.value("latestKernel507FileTime100ns",0ULL),details.value("reason",-1));
        return;
    }
    if(std::strcmp(event,"s4-wake-message")==0){sgSleepActivityEnd("kernel-s4-wake",0,-1);return;}
    if(std::strcmp(event,"power-broadcast")==0) {
        const auto code=details.value("code",0ULL);
        if(code==2||code==3||code==6){sgSleepActivityEnd("power-transition-ended",0,static_cast<int>(code));return;}
        if(code==4||code==7||code==18)sgSleepActivityObserve("power-broadcast",0,static_cast<int>(code));
    }
}
