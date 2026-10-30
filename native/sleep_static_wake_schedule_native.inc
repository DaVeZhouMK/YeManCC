#include "sleep_static_wake_schedule.h"
// REQUIRED ordinary lifecycle receipts. Runs only after boot/confirmed wake or an
// explicit user scan; no repeating inventory, no high-frequency logging dependency.
static ymcc::staticwake::ScanDelay g_sgStaticScanDelay;
static std::atomic<int> g_sgStaticScanPreference{-1};
static std::atomic<ULONGLONG> g_sgStaticScanRevision{1};
static ULONGLONG g_sgStaticScanArmedRevision=0;
static std::atomic<bool> g_sgStaticScanRunning{false};
static ULONGLONG sgStaticScanInvalidate(const char*) {
    const auto revision=g_sgStaticScanRevision.fetch_add(1,std::memory_order_acq_rel)+1;
    if(g_hwnd)PostMessageW(g_hwnd,WM_SG_STATIC_SCAN,0,static_cast<LPARAM>(revision));
    return revision; // Our own ticket, even if a newer invalidation races the post.
}
static void sgStaticScanCancel(const char* reason) {(void)sgStaticScanInvalidate(reason);}
static void sgStaticScanSchedule(bool startup) {
    if(g_hwnd&&!g_exitRequested.load(std::memory_order_acquire))
        PostMessageW(g_hwnd,WM_SG_STATIC_SCAN,startup?1:2,static_cast<LPARAM>(g_sgStaticScanRevision.load(std::memory_order_acquire)));
}
// Migrate the former shared preference without changing a saved OFF choice.
static bool sgStaticScanEnabled(const json& sleep) {
    return sleep.value("staticWakeAutoScanEnabled",sleep.value("sleepPowerPlanOptimizationEnabled",true));
}
static void sgStaticScanPreference(bool enabled,bool startup=false) {
    const auto previous=g_sgStaticScanPreference.exchange(enabled?1:0,std::memory_order_acq_rel);
    if(!enabled){if(previous!=0)sgStaticScanCancel("setting-off");}
    else if(startup)sgStaticScanSchedule(true);
    else if(previous!=1&&g_hwnd)PostMessageW(g_hwnd,WM_SG_STATIC_SCAN,3,static_cast<LPARAM>(g_sgStaticScanRevision.load(std::memory_order_acquire)));
}
static bool sgStaticScanCanceled(ULONGLONG revision,unsigned long long generation) {
    return g_sgStaticScanRevision.load(std::memory_order_acquire)!=revision||
        g_sgStaticScanPreference.load(std::memory_order_acquire)!=1||g_exitRequested.load(std::memory_order_acquire)||
        currentPowerGeneration()!=generation||g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready||!hardwareWriteAllowed();
}
static json sgStaticScanNow() {
    const auto revision=sgStaticScanInvalidate("explicit-scan");
    const auto generation=currentPowerGeneration();
    return swScan([revision,generation]{return sgStaticScanCanceled(revision,generation);});
}
// Restoring our changes is valid after the preference is OFF. It still belongs
// to this power/operation generation and must stop on sleep, exit or supersession.
static bool sgStaticRestoreCanceled(ULONGLONG revision,unsigned long long generation) {
    return g_sgStaticScanRevision.load(std::memory_order_acquire)!=revision||
        g_exitRequested.load(std::memory_order_acquire)||currentPowerGeneration()!=generation||
        g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready||!hardwareWriteAllowed();
}
static json sgStaticRestoreNow() {
    const auto revision=sgStaticScanInvalidate("explicit-restore");
    const auto generation=currentPowerGeneration();
    return swRestore([revision,generation]{return sgStaticRestoreCanceled(revision,generation);});
}
static void sgStaticScanTimer() {
    if(g_hwnd)KillTimer(g_hwnd,SG_STATIC_SCAN_TIMER_ID);
    if(g_sgStaticScanDelay.pending && g_sgStaticScanArmedRevision!=g_sgStaticScanRevision.load(std::memory_order_acquire)) {
        g_sgStaticScanDelay.cancel();sgRecordFact("static-wake-delay-canceled",{{"reason","superseded-before-message"},{"mandatoryEvidence",true}});return;
    }
    const auto decision=g_sgStaticScanDelay.poll(GetTickCount64(),g_sgStaticScanPreference.load()==1,
        !g_exitRequested.load()&&g_powerLifecycle.load()==PowerLifecycle::Ready&&hardwareWriteAllowed());
    if(decision.remaining) {
        if(!SetTimer(g_hwnd,SG_STATIC_SCAN_TIMER_ID,static_cast<UINT>((std::min)(decision.remaining,static_cast<ULONGLONG>(USER_TIMER_MAXIMUM))),nullptr)) {
            g_sgStaticScanDelay.cancel();sgRecordFact("static-wake-delay-canceled",{{"reason","timer-unavailable"},{"mandatoryEvidence",true}});
        }
        return;
    }
    if(!decision.dispatch)return;
    if(g_sgStaticScanRunning.exchange(true)) {
        sgRecordFact("static-wake-auto-skipped",{{"reason","scan-already-running"},{"retry",false}});return;
    }
    const auto revision=g_sgStaticScanArmedRevision; // Never adopt a cancellation that raced poll/dispatch.
    const auto generation=currentPowerGeneration();
    if(!poolSubmit([revision,generation]{
        struct Release {~Release(){g_sgStaticScanRunning.store(false,std::memory_order_release);}} release;
        if(sgStaticScanCanceled(revision,generation))return;
        sgRecordFact("static-wake-scan-start",{{"trigger","delayed-boot-or-confirmed-wake"},{"delayMs",60000},
            {"generation",generation},{"mandatoryEvidence",true},{"detailLogsRequired",false}});
        const auto result=swScan([revision,generation]{return sgStaticScanCanceled(revision,generation);});
        sgRecordFact("static-wake-scan-result",result);
        ipc_emit("sleep.staticWake.updated",result.at("status"));
    })) {
        g_sgStaticScanRunning.store(false,std::memory_order_release);
        sgRecordFact("static-wake-auto-skipped",{{"reason","worker-unavailable"},{"retry",false},{"mandatoryEvidence",true}});
    }
}
static void sgStaticScanMessage(WPARAM cause,LPARAM revision) {
    if(cause==0) {
        if(static_cast<ULONGLONG>(revision)!=g_sgStaticScanRevision.load(std::memory_order_acquire))return;
        const bool pending=g_sgStaticScanDelay.pending;
        g_sgStaticScanDelay.cancel();if(g_hwnd)KillTimer(g_hwnd,SG_STATIC_SCAN_TIMER_ID);
        if(pending)sgRecordFact("static-wake-delay-canceled",{{"reason","sleep-setting-off-or-exit"},{"mandatoryEvidence",true}});
        return;
    }
    // A queued start cannot acquire the revision created by a later sleep/OFF.
    if(static_cast<ULONGLONG>(revision)!=g_sgStaticScanRevision.load(std::memory_order_acquire))return;
    if(g_sgStaticScanPreference.load()!=1||g_exitRequested.load()||
        g_powerLifecycle.load()!=PowerLifecycle::Ready||!hardwareWriteAllowed())return;
    const auto generation=currentPowerGeneration();
    auto next=g_sgStaticScanDelay;
    if(!next.arm(GetTickCount64(),cause==1,cause==2?generation:0))return;
    auto expected=static_cast<ULONGLONG>(revision);
    if(expected==(std::numeric_limits<ULONGLONG>::max)())return;
    const auto armed=expected+1;
    // Commit only the revision carried by this message. A newer power/OFF event
    // must not be hidden by an unconditional increment after the initial check.
    if(!g_sgStaticScanRevision.compare_exchange_strong(expected,armed,std::memory_order_acq_rel))return;
    g_sgStaticScanDelay=std::move(next);g_sgStaticScanArmedRevision=armed;
    if(!SetTimer(g_hwnd,SG_STATIC_SCAN_TIMER_ID,60000,nullptr)) {
        g_sgStaticScanDelay.cancel();sgRecordFact("static-wake-delay-canceled",{{"reason","timer-unavailable"},{"mandatoryEvidence",true}});return;
    }
    sgRecordFact("static-wake-delay-armed",{{"trigger",cause==1?"startup":"confirmed-wake-or-enable"},
        {"delayMs",60000},{"generation",generation},{"periodic",false},{"mandatoryEvidence",true}});
}
