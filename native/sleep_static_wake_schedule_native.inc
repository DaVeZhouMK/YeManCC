#include "sleep_static_wake_schedule.h"
#include <mutex>
// REQUIRED ordinary lifecycle receipts. Runs only after boot/confirmed wake or an
// explicit user scan; no repeating inventory, no high-frequency logging dependency.
static ymcc::staticwake::ScanDelay g_sgStaticScanDelay;
static std::atomic<int> g_sgStaticScanPreference{-1};
static std::atomic<ULONGLONG> g_sgStaticScanRevision{1};
static ULONGLONG g_sgStaticScanArmedRevision=0;
static ymcc::sleeppower::SharedPowerV1 g_sgStaticScanArmedPower;
static std::atomic<bool> g_sgStaticScanRunning{false};
static constexpr WPARAM kStaticWakeStatusChanged=4;
struct SgStaticScanArmMessage {
    LPARAM token=0;
    WPARAM cause=0;
    ULONGLONG revision=0;
    ymcc::sleeppower::SharedPowerV1 power;
};
static std::mutex g_sgStaticScanArmMutex;
static SgStaticScanArmMessage g_sgStaticScanQueuedArm;
static LPARAM g_sgStaticScanArmToken=0;
// The worker publishes only an invalidation; the window owns WebView delivery.
static void sgPostStaticWakeStatusChanged() noexcept {
    if(g_exitRequested.load(std::memory_order_acquire))return;
    const HWND window=g_hwnd;
    if(window&&!PostMessageW(window,WM_SG_STATIC_SCAN,kStaticWakeStatusChanged,0)) {
        const DWORD error=GetLastError();
        try {sgRecordFact("static-wake-status-notification-failed",{{"win32Error",error}});} catch(...) {}
    }
}
static ULONGLONG sgStaticScanInvalidate(const char*) {
    const auto revision=g_sgStaticScanRevision.fetch_add(1,std::memory_order_acq_rel)+1;
    if(g_hwnd)PostMessageW(g_hwnd,WM_SG_STATIC_SCAN,0,static_cast<LPARAM>(revision));
    return revision; // Our own ticket, even if a newer invalidation races the post.
}
static void sgStaticScanCancel(const char* reason) {(void)sgStaticScanInvalidate(reason);}
static void sgPostStaticScanArm(WPARAM cause) {
    const HWND window=g_hwnd;
    const auto revision=g_sgStaticScanRevision.load(std::memory_order_acquire);
    ymcc::sleeppower::SharedPowerV1 power;
    if(!window||g_sgStaticScanPreference.load(std::memory_order_acquire)!=1||!captureSharedPowerLifecycle(power))return;
    std::lock_guard lock(g_sgStaticScanArmMutex);
    if(g_sgStaticScanRevision.load(std::memory_order_acquire)!=revision||
       g_sgStaticScanPreference.load(std::memory_order_acquire)!=1||!sharedPowerLifecycleCurrent(power)||
       g_sgStaticScanArmToken==(std::numeric_limits<LPARAM>::max)())return;
    const auto& queued=g_sgStaticScanQueuedArm;
    // Keep the first valid start for this revision. A new public scope can
    // replace a retired start without letting its old message take the slot.
    if(queued.token&&queued.revision==revision&&ymcc::sleeppower::samePowerRevocation(queued.power,power))return;
    const auto token=++g_sgStaticScanArmToken;
    if(PostMessageW(window,WM_SG_STATIC_SCAN,cause,token))g_sgStaticScanQueuedArm={token,cause,revision,power};
}
static void sgStaticScanSchedule(bool startup) {
    sgPostStaticScanArm(startup?1:2);
}
// Migrate the former shared preference without changing a saved OFF choice.
static bool sgStaticScanEnabled(const json& sleep) {
    return sleep.value("staticWakeAutoScanEnabled",sleep.value("sleepPowerPlanOptimizationEnabled",true));
}
static void sgStaticScanPreference(bool enabled,bool startup=false) {
    const auto previous=g_sgStaticScanPreference.exchange(enabled?1:0,std::memory_order_acq_rel);
    if(!enabled){if(previous!=0)sgStaticScanCancel("setting-off");}
    else if(startup)sgStaticScanSchedule(true);
    else if(previous!=1)sgPostStaticScanArm(3);
}
static bool sgStaticWakeCanceled(ULONGLONG revision,const ymcc::sleeppower::SharedPowerV1& power,bool restore) {
    return !sharedPowerLifecycleCurrent(power)||g_sgStaticScanRevision.load(std::memory_order_acquire)!=revision||
        (!restore&&g_sgStaticScanPreference.load(std::memory_order_acquire)!=1);
}
static json sgRunStaticWakeOperation(bool restore,ULONGLONG revision,const ymcc::sleeppower::SharedPowerV1& power) {
    auto revocation=sgStaticWakeCanceled(revision,power,restore)?
        ymcc::sleeppower::RevocationMirrors::Registration{}:observeSharedPowerRevocation(power);
    const auto canceled=[&]{return revocation.cancelled()||sgStaticWakeCanceled(revision,power,restore);};
    return restore?swRestore(canceled):swScan(canceled);
}
static json sgStaticWakeNow(bool restore,const json& request) {
    const auto metric=[&](const char* field)->std::optional<std::uint64_t>{
        const auto it=request.find(field);
        if(it==request.end()||(!it->is_number_unsigned()&&(!it->is_number_integer()||it->get<std::int64_t>()<0)))return {};
        const auto value=it->get<std::uint64_t>();
        return value<=9007199254740991ull?std::optional<std::uint64_t>{value}:std::nullopt;
    };
    const auto generation=metric("expectedPowerGeneration");
    const auto sequence=metric("expectedPowerNotificationSequence");
    auto revision=g_sgStaticScanRevision.load(std::memory_order_acquire);
    ymcc::sleeppower::SharedPowerV1 power;
    // Claim only the scan ticket that existed before checking the request's power scope.
    if(!generation||!*generation||!sequence||!captureSharedPowerLifecycle(power)||
        power.generation!=*generation||power.observedNotificationSequence!=*sequence||
        revision==(std::numeric_limits<ULONGLONG>::max)()||!sharedPowerLifecycleCurrent(power)||
        !g_sgStaticScanRevision.compare_exchange_strong(revision,revision+1,std::memory_order_acq_rel)) {
        auto status=swGetStatus();status["lastError"]="power-admission-retired";
        return {{"ok",false},{"status",std::move(status)}};
    }
    ++revision;
    if(g_hwnd)PostMessageW(g_hwnd,WM_SG_STATIC_SCAN,0,static_cast<LPARAM>(revision));
    return sgRunStaticWakeOperation(restore,revision,power);
}
static void sgStaticScanTimer() {
    if(g_hwnd)KillTimer(g_hwnd,SG_STATIC_SCAN_TIMER_ID);
    if(g_sgStaticScanDelay.pending && g_sgStaticScanArmedRevision!=g_sgStaticScanRevision.load(std::memory_order_acquire)) {
        g_sgStaticScanDelay.cancel();sgRecordFact("static-wake-delay-canceled",{{"reason","superseded-before-message"},{"mandatoryEvidence",true}});return;
    }
    if(!g_sgStaticScanDelay.pending)return;
    const auto revision=g_sgStaticScanArmedRevision;
    const auto power=g_sgStaticScanArmedPower;
    const auto now=GetTickCount64();
    if(sgStaticWakeCanceled(revision,power,false)) {
        g_sgStaticScanDelay.cancel();sgRecordFact("static-wake-delay-canceled",{{"reason","admission-retired"},{"mandatoryEvidence",true}});return;
    }
    const auto decision=g_sgStaticScanDelay.poll(now,true,true);
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
    bool submitted=false;
    try { submitted=poolSubmit([revision,power]{
        struct Release {~Release(){g_sgStaticScanRunning.store(false,std::memory_order_release);}} release;
        if(sgStaticWakeCanceled(revision,power,false))return;
        sgRecordFact("static-wake-scan-start",{{"trigger","delayed-boot-or-confirmed-wake"},{"delayMs",60000},
            {"generation",power.generation},{"mandatoryEvidence",true},{"detailLogsRequired",false}});
        const auto result=sgRunStaticWakeOperation(false,revision,power);
        sgRecordFact("static-wake-scan-result",result);
        sgPostStaticWakeStatusChanged();
    }); } catch(...) {}
    if(!submitted) {
        g_sgStaticScanRunning.store(false,std::memory_order_release);
        try { sgRecordFact("static-wake-auto-skipped",{{"reason","worker-unavailable"},{"retry",false},{"mandatoryEvidence",true}}); } catch(...) {}
    }
}
static void sgStaticScanMessage(WPARAM cause,LPARAM ticket) {
    if(cause==kStaticWakeStatusChanged) {
        if(!g_exitRequested.load(std::memory_order_acquire)) {
            try {ipc_emit("sleep.staticWake.updated",json::object());} catch(...) {}
        }
        return;
    }
    if(cause==0) {
        if(static_cast<ULONGLONG>(ticket)!=g_sgStaticScanRevision.load(std::memory_order_acquire))return;
        const bool pending=g_sgStaticScanDelay.pending;
        g_sgStaticScanDelay.cancel();if(g_hwnd)KillTimer(g_hwnd,SG_STATIC_SCAN_TIMER_ID);
        if(pending)sgRecordFact("static-wake-delay-canceled",{{"reason","sleep-setting-off-or-exit"},{"mandatoryEvidence",true}});
        return;
    }
    SgStaticScanArmMessage queued;
    {
        std::lock_guard lock(g_sgStaticScanArmMutex);
        if(!ticket||g_sgStaticScanQueuedArm.token!=ticket||g_sgStaticScanQueuedArm.cause!=cause)return;
        queued=g_sgStaticScanQueuedArm;
        g_sgStaticScanQueuedArm={};
    }
    const auto revision=queued.revision;
    // A queued start cannot acquire the revision created by a later sleep/OFF.
    if(static_cast<ULONGLONG>(revision)!=g_sgStaticScanRevision.load(std::memory_order_acquire))return;
    const auto& power=queued.power;
    if(g_sgStaticScanPreference.load()!=1||!sharedPowerLifecycleCurrent(power))return;
    auto next=g_sgStaticScanDelay;
    if(!next.arm(GetTickCount64(),cause==1,cause==2?power.generation:0,power.suspendSerial)||
       !sharedPowerLifecycleCurrent(power))return;
    auto expected=static_cast<ULONGLONG>(revision);
    if(expected==(std::numeric_limits<ULONGLONG>::max)())return;
    const auto armed=expected+1;
    // Commit only the revision carried by this message. A newer power/OFF event
    // must not be hidden by an unconditional increment after the initial check.
    if(!g_sgStaticScanRevision.compare_exchange_strong(expected,armed,std::memory_order_acq_rel))return;
    g_sgStaticScanDelay=std::move(next);g_sgStaticScanArmedRevision=armed;g_sgStaticScanArmedPower=power;
    if(!SetTimer(g_hwnd,SG_STATIC_SCAN_TIMER_ID,60000,nullptr)) {
        g_sgStaticScanDelay.cancel();sgRecordFact("static-wake-delay-canceled",{{"reason","timer-unavailable"},{"mandatoryEvidence",true}});return;
    }
    sgRecordFact("static-wake-delay-armed",{{"trigger",cause==1?"startup":"confirmed-wake-or-enable"},
        {"delayMs",60000},{"generation",power.generation},{"suspendSerial",power.suspendSerial},
        {"periodic",false},{"mandatoryEvidence",true}});
}
