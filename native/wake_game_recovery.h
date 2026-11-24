#pragma once
#include "input_host_shared_power.h"
// Included after the native sleep-lease helpers. All batch state is guarded by
// g_sgOpMtx; wake timing runs only on the existing sleep lifecycle worker.
struct SgWakeRecoveryEntry {
    std::wstring dir;
    DWORD pid = 0;
    ULONGLONG created = 0;
    std::string markerText;
    bool fallback = false, acceptedOnce = false, done = false;
    std::wstring fallbackName;
    // A retained sleep lease may belong to an earlier power generation.
    // Capture its owner independently from this recovery batch generation.
    unsigned long long sleepOwnerGeneration = 0;
    bool canceledLease = false;
    ULONGLONG canceledThrough = 0;
    uint64_t canceledBoundarySequence = 0;
    bool resumeBlockedByNewPause = false;
};
struct SgWakeRecoveryBatch {
    unsigned long long generation = 0;
    ULONGLONG started = 0;
    size_t nextAttempt = 0;
    std::string manualState;
    std::vector<SgWakeRecoveryEntry> entries;
    std::string requestId;
    bool focusOnComplete = true;
    ymcc::sleeppower::SharedPowerV1 focusPower;
    bool canceledLeasesOnly = false;
    unsigned long long autoConfigEpoch = 0;
    bool explicitWake = false;
    bool resumeDispatched = false;
    bool waitingForNextWake = false;
    uint64_t wakeSleepSequence = 0;
    uint64_t wakeSuspendSerial = 0;
    std::shared_ptr<ymcc::inputHost::sharedPower::Scope> userRepairPower;

    bool crossedSleepBoundary(uint64_t sleepSequence, uint64_t suspendSerial) const noexcept {
        return wakeSleepSequence != sleepSequence || wakeSuspendSerial != suspendSerial;
    }
};
static SgWakeRecoveryBatch g_sgWakeRecovery;
// The batch stays under g_sgOpMtx; public power changes can signal a parked worker.
static std::atomic<bool> g_sgWakeRecoveryParked{false};
static void sgNotifyParkedWakeGameRecovery() {
    if (g_sgWakeRecoveryParked.load(std::memory_order_acquire)) sgNotifyWork();
}
static bool sgWakeGameRecoveryHasWork() {
    return g_sgWakeRecoveryPending.load(std::memory_order_acquire) ||
        g_sgWakeRecoveryParked.load(std::memory_order_acquire);
}
static unsigned long long g_sgLastGameWakeGeneration = 0;

static std::wstring sgWakeRecoveryMarker(const SgWakeRecoveryEntry& e) {
    return e.dir + L"\\" + std::to_wstring(e.pid) + L".txt";
}
static void sgWakeRecoveryClearTarget(const SgWakeRecoveryEntry& e) {
    if (e.dir != SG_SLEEP_LEASE_DIR || !e.sleepOwnerGeneration) return;
    SgSleepTarget target;
    if (sgReadSleepTarget(target) && target.pid == e.pid && target.processCreated == e.created &&
        target.markerOwned && target.powerGeneration == e.sleepOwnerGeneration)
        sgClearSleepTargetIfMatches(target);
}
// A canceled sleep owns only its captured old PID lease, never a fresh scan.
// Caller holds g_sgOpMtx; the witness remains read-only at every native call.
static bool sgCanceledSleepRecoveryBoundaryCurrent(const SgWakeRecoveryEntry& e) {
    return !e.canceledLease || (e.canceledThrough &&
        g_sgLatestSleepEntryFileTime.load(std::memory_order_acquire)<=e.canceledThrough &&
        g_sleepPowerDispatch.observedSleepBoundarySequence()==e.canceledBoundarySequence);
}
static bool sgCanceledSleepRecoveryEntryCurrent(const SgWakeRecoveryEntry& e) {
    if (!e.canceledLease) return true;
    if (!sgCanceledSleepRecoveryBoundaryCurrent(e)) return false;
    SgSleepTarget owner;
    if (!sgReadSleepTarget(owner) || !owner.markerOwned || owner.pid!=e.pid ||
        owner.processCreated!=e.created || owner.powerGeneration!=e.sleepOwnerGeneration) return false;
    std::error_code ec;
    const bool manual=fspath::exists(SG_MANUAL_DIR+L"\\"+std::to_wstring(e.pid)+L".txt",ec);
    return !ec&&!manual && sgCanceledSleepRecoveryBoundaryCurrent(e);
}
static bool sgQueueCanceledSleepLeaseRecoveryUnlocked(const SgSleepTarget& target,
    unsigned long long generation,ULONGLONG canceledThrough,uint64_t boundarySequence,std::string retainedMarkerText) {
    if (!generation || generation!=currentPowerGeneration() || g_exitRequested.load(std::memory_order_acquire) ||
        g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready ||
        sgSleepRetryActive() || g_sleepPowerDispatch.pendingSuspend()) return false;
    SgWakeRecoveryEntry entry;
    entry.dir=SG_SLEEP_LEASE_DIR;entry.pid=target.pid;entry.created=target.processCreated;
    entry.sleepOwnerGeneration=target.powerGeneration;entry.canceledLease=true;
    entry.canceledThrough=canceledThrough;entry.canceledBoundarySequence=boundarySequence;
    if (!entry.pid || !entry.created || !entry.sleepOwnerGeneration || !sgCanceledSleepRecoveryEntryCurrent(entry)) return false;
    entry.markerText=std::move(retainedMarkerText);
    // A failed first checked read keeps only this held lease's exact witness.
    // The existing worker must validate its generation before capturing bytes.
    if (!sgCanceledSleepRecoveryEntryCurrent(entry)) return false;
    if (g_sgWakeRecoveryPending.load(std::memory_order_acquire) && g_sgWakeRecovery.generation==generation) {
        for (auto& current:g_sgWakeRecovery.entries) if (current.dir==entry.dir&&current.pid==entry.pid) {
            // Another creation or lease at this PID keeps its own original
            // checks. It cannot absorb the current held tuple; append below
            // preserves this batch's existing clock, attempt and capacity.
            if(current.created!=entry.created||current.sleepOwnerGeneration!=entry.sleepOwnerGeneration)continue;
            if(current.done ||
               (!current.markerText.empty()&&!entry.markerText.empty()&&current.markerText!=entry.markerText))return false;
            if(current.markerText.empty()&&!entry.markerText.empty())current.markerText=std::move(entry.markerText);
            current.canceledLease=true;current.canceledThrough=canceledThrough;current.canceledBoundarySequence=boundarySequence;
            sgNotifyWork();return true; // Keep the existing attempt/deadline, without rearming.
        }
        if(g_sgWakeRecovery.entries.size()>=64)return false;
        g_sgWakeRecovery.entries.push_back(std::move(entry)); // Existing batch keeps its original clock and attempt.
    } else {
        SgWakeRecoveryBatch batch;batch.generation=generation;batch.started=GetTickCount64();
        batch.focusOnComplete=false;batch.canceledLeasesOnly=true;batch.entries.push_back(std::move(entry));
        // A stale-generation batch has no current action permit. Drop only its
        // volatile plan; all of its exact marker/owner debt remains untouched.
        g_sgWakeRecovery=std::move(batch);
        g_sgWakeRecoveryParked.store(false,std::memory_order_release);
    }
    g_sgWakeRecoveryPending.store(true,std::memory_order_release);sgNotifyWork();return true;
}

static void sgReconcileSleepGameFlagsAfterRecoveryCancel(const char* reason) {
    // A confirmed-running old lease is no longer a sleep pause.  The previous
    // recovery implementation retired its exact marker/owner but left these
    // two in-memory flags set, so the next generation saw alreadyPaused=true,
    // skipped capture, and could never schedule a fresh wake recovery.
    SgSleepTarget target;
    const bool ownedSleepLease = sgReadSleepTarget(target) && target.markerOwned;
    const bool retainedSleepMarker = sgHasMarkerFiles(SG_SLEEP_LEASE_DIR);
    const bool shouldRemainSuspended = ownedSleepLease || retainedSleepMarker;
    const bool beforeInSuspend = g_sgInSuspend.load(std::memory_order_acquire);
    const bool beforeActuallySuspended = g_sgGameActuallySuspended.load(std::memory_order_acquire);
    g_sgInSuspend.store(shouldRemainSuspended, std::memory_order_release);
    g_sgGameActuallySuspended.store(shouldRemainSuspended, std::memory_order_release);
    if (beforeInSuspend != shouldRemainSuspended ||
        beforeActuallySuspended != shouldRemainSuspended) {
        try { sgRecordFact("sleep-game-lease-state-reconciled", {
            {"reason", reason ? reason : "recovery-cancel"},
            {"ownedSleepLease", ownedSleepLease},
            {"retainedSleepMarker", retainedSleepMarker},
            {"inSuspendBefore", beforeInSuspend},
            {"gameActuallySuspendedBefore", beforeActuallySuspended},
            {"inSuspendAfter", shouldRemainSuspended},
            {"gameActuallySuspendedAfter", shouldRemainSuspended}
        }); } catch (...) {}
    }
}
enum class SgWakeMarkerRetirement { Retained, Superseded, Retired };

static SgWakeMarkerRetirement sgRetireAcceptedWakeMarkerUnlocked(
    const SgWakeRecoveryEntry& e, const std::function<bool()>& requestCurrent) {
    using Retirement = SgWakeMarkerRetirement;
    const auto current = [&] { return !requestCurrent || requestCurrent(); };
    if (!e.pid || !e.created || e.markerText.empty() || !current()) return Retirement::Retained;
    const auto marker = sgWakeRecoveryMarker(e);
    auto files = sgAcquireResumeFiles(e.dir, e.pid, current, e.created);
    if (files.result == ymcc::sleepLegacyResume::Result::Cleaned) {
        sgWakeRecoveryClearTarget(e);
        return Retirement::Retired;
    }
    const auto stillCurrent = [&] { return files.current(current); };
    if (!stillCurrent()) return Retirement::Retained;
    std::string text;
    if (!sgReadResumeMarkerChecked(marker, text)) {
        std::error_code ec;
        return !fspath::exists(marker, ec) && !ec && stillCurrent()
            ? Retirement::Superseded : Retirement::Retained;
    }
    if (!stillCurrent()) return Retirement::Retained;
    if (text != e.markerText) return Retirement::Superseded;
    const ymcc::sleepLegacyResume::File process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.pid));
    if (!process || nativeProcessCreatedFromHandle(process.get()) != e.created || !stillCurrent() ||
        !WakeGameResume::queryThreads(e.pid).allRunning()) return Retirement::Retained;
    if (!sgReadResumeMarkerChecked(marker, text) || text != e.markerText || !stillCurrent())
        return Retirement::Retained;
    std::error_code ec;
    if (!fspath::remove(marker, ec) || ec) return Retirement::Retained;
    sgWakeRecoveryClearTarget(e);
    return Retirement::Retired;
}

static void sgCancelWakeGameRecoveryUnlocked(const char* reason,
    const std::function<bool()>& requestCurrent = std::function<bool()>()) {
    if (requestCurrent && !requestCurrent()) return;
    if (!g_sgWakeRecoveryPending.load(std::memory_order_acquire)) {
        if (g_sgWakeRecoveryParked.exchange(false,std::memory_order_acq_rel) ||
            g_sgWakeRecovery.waitingForNextWake) g_sgWakeRecovery = {};
        // Recovery may already have drained its batch while an old in-memory
        // flag still survives. Reconcile even on the no-pending path.
        sgReconcileSleepGameFlagsAfterRecoveryCancel(reason);
        return;
    }
    unsigned retired = 0;
    // Cancellation may win the race against the next suspend item. Retire
    // only already-running confirmations here as well, so an old marker can
    // never masquerade as the new sleep's successful freeze. No resume API.
    for (auto& e : g_sgWakeRecovery.entries) {
        if (requestCurrent && !requestCurrent()) return;
        if (e.done || !e.acceptedOnce) continue;
        if (sgRetireAcceptedWakeMarkerUnlocked(e, requestCurrent) == SgWakeMarkerRetirement::Retired)
            ++retired;
    }
    if (requestCurrent && !requestCurrent()) return;
    try { sgRecordFact("sleep-game-recovery-canceled", {
        {"generation", g_sgWakeRecovery.generation}, {"reason", reason ? reason : "unknown"},
        {"retiredConfirmedRunning", retired}, {"unconfirmedMarkersRetained", true}
    }); } catch (...) {}
    if (requestCurrent && !requestCurrent()) return;
    g_sgWakeRecovery = {};
    g_sgWakeRecoveryPending.store(false, std::memory_order_release);
    sgReconcileSleepGameFlagsAfterRecoveryCancel(reason);
}
static bool sgWakeGameRecoveryCanWaitForNextWakeUnlocked() {
    const auto& batch = g_sgWakeRecovery;
    return batch.explicitWake && !batch.autoConfigEpoch && !batch.resumeDispatched &&
        !g_exitRequested.load(std::memory_order_acquire) &&
        batch.generation == currentPowerGeneration();
}
static SgWakeRecoveryBatch sgWakeGameRecoveryTerminalBoundaryUnlocked() {
    SgWakeRecoveryBatch terminal;
    if (sgWakeGameRecoveryCanWaitForNextWakeUnlocked()) {
        terminal.generation = g_sgWakeRecovery.generation;
        terminal.explicitWake = true;
        terminal.wakeSleepSequence = g_sgWakeRecovery.wakeSleepSequence;
        terminal.wakeSuspendSerial = g_sgWakeRecovery.wakeSuspendSerial;
    }
    return terminal;
}
static void sgFailWakeGameRecoveryUnlocked() {
    // Keep the same dedup boundary as normal zero-dispatch completion. Never
    // replay a batch that made an NT call, or retry the fault on this wake.
    auto terminal = sgWakeGameRecoveryTerminalBoundaryUnlocked();
    try { sgCancelWakeGameRecoveryUnlocked("recovery-exception"); }
    catch (...) {
        // A failed retirement cannot release an unconfirmed marker. Close
        // the volatile work even if its marker or flag reconciliation fails.
        g_sgWakeRecovery = {};
        g_sgWakeRecoveryPending.store(false, std::memory_order_release);
        g_sgWakeRecoveryParked.store(false, std::memory_order_release);
        try { sgReconcileSleepGameFlagsAfterRecoveryCancel("recovery-exception"); } catch (...) {}
    }
    g_sgWakeRecovery = std::move(terminal);
}
static bool sgGameWakeBoundaryChanged() {
    return g_sgWakeRecovery.explicitWake &&
        g_sgWakeRecovery.crossedSleepBoundary(g_sleepPowerDispatch.observedSleepBoundarySequence(),
            g_sharedPowerSuspendSerial.load(std::memory_order_acquire));
}
static void sgInterruptWakeGameRecoveryUnlocked() {
    auto& batch = g_sgWakeRecovery;
    if (sgWakeGameRecoveryCanWaitForNextWakeUnlocked()) {
        // The worker has no runnable work until another explicit, admitted
        // wake. Retain only the original snapshot, including completed entries.
        batch.waitingForNextWake = true;
        g_sgWakeRecoveryParked.store(false,std::memory_order_release);
        g_sgWakeRecoveryPending.store(false, std::memory_order_release);
        try { sgRecordFact("sleep-game-recovery-awaiting-wake", {{"generation", batch.generation},
            {"reason", "suspend-or-retry-before-dispatch"}, {"targets", batch.entries.size()}}); } catch (...) {}
        sgReconcileSleepGameFlagsAfterRecoveryCancel("zero-dispatch-wake-interrupted");
        return;
    }
    sgCancelWakeGameRecoveryUnlocked("exit-new-sleep-or-retry");
}
static bool sgPrepareWakeRecoveryForPauseUnlocked(DWORD pid, const std::function<bool()>& requestCurrent) {
    if (requestCurrent && !requestCurrent()) return false;
    if (!sgWakeGameRecoveryHasWork() && !g_sgWakeRecovery.waitingForNextWake) return true;
    bool safe = true;
    for (auto& e : g_sgWakeRecovery.entries) {
        if (e.done || e.pid != pid) continue;
        // A NEW manual pause must not reuse an already-resumed confirmation
        // marker or be consumed by a delayed attempt from the old wake.
        if (e.acceptedOnce &&
            sgRetireAcceptedWakeMarkerUnlocked(e, requestCurrent) == SgWakeMarkerRetirement::Retained) {
            if (requestCurrent && !requestCurrent()) return false;
            // Keep the unresolved marker, but a later tick of this wake may
            // no longer resume this PID after a newer pause was requested.
            e.resumeBlockedByNewPause = true;
            safe = false;
            continue;
        }
        if (requestCurrent && !requestCurrent()) return false;
        e.done = true;
        try { sgRecordFact("sleep-game-recovery-superseded", {
            {"generation", g_sgWakeRecovery.generation}, {"pid", pid}, {"reason", "new-pause"}
        }); } catch (...) {}
    }
    sgReconcileSleepGameFlagsAfterRecoveryCancel("new-pause");
    return safe;
}
static bool sgPrepareWakeRecoveryForSleep(unsigned long long generation,
    const std::function<bool()>& requestCurrent = std::function<bool()>()) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    if (requestCurrent && !requestCurrent()) return false;
    if (!sgWakeGameRecoveryHasWork() ||
        g_sgWakeRecovery.generation == generation) {
        sgReconcileSleepGameFlagsAfterRecoveryCancel("new-sleep-check");
        return !requestCurrent || requestCurrent();
    }
    // Cancellation retires only confirmed-running markers before capture.
    // Keep this queue item's original veto through IO, thread probes and journals.
    sgCancelWakeGameRecoveryUnlocked("new-sleep-before-capture", requestCurrent);
    return !requestCurrent || requestCurrent();
}
static void sgWakeRecoverySnapshotDirectory(const std::wstring& dir,const SgWorkItem* delivery=nullptr) {
    std::error_code ec;
    if (!fspath::exists(dir, ec) || ec) return;
    for (const auto& file : fspath::directory_iterator(dir, ec)) {
        if (!file.is_regular_file(ec) || file.path().extension() != L".txt") continue;
        const auto stem = file.path().stem().wstring();
        if (stem.empty() || stem.find_first_not_of(L"0123456789") != std::wstring::npos) continue;
        DWORD pid = 0;
        try { const auto raw = std::stoull(stem); if (raw > MAXDWORD) continue; pid = static_cast<DWORD>(raw); }
        catch (...) { continue; }
        const auto created = sgMarkerCreated(dir, pid);
        if (!created || !sgMarkerIsSuspended(dir, pid)) continue;
        const auto text = sgReadFile(file.path().wstring());
        if (dir == SG_MANUAL_DIR && delivery) {
            const auto serialPos = text.find("|pauseSerial=");
            if (serialPos != std::string::npos &&
                text.find("|pauseRun=" + sgManualPauseRunToken() + "|pauseSerial=") != std::string::npos) {
                try {
                    if (std::stoull(text.substr(serialPos + 13)) >
                        delivery->wakeManualPauseFence) continue;
                } catch (...) { continue; }
            }
        }
        // Transient access denial is NOT evidence of exit or a stale PID.
        SgWakeRecoveryEntry entry{dir, pid, created, text};
        SgSleepTarget sleepOwner;
        if (dir == SG_SLEEP_LEASE_DIR && sgReadSleepTarget(sleepOwner) &&
            sleepOwner.markerOwned && sleepOwner.pid == pid &&
            sleepOwner.processCreated == created && sleepOwner.powerGeneration != 0 &&
            sleepOwner.powerGeneration == sgMarkerGeneration(dir, pid))
            entry.sleepOwnerGeneration = sleepOwner.powerGeneration;
        g_sgWakeRecovery.entries.push_back(std::move(entry));
    }
}
static bool sgWriteWakeRecoveryMarker(DWORD pid, ULONGLONG created, unsigned long long generation) {
    if (!pid || !created || !sgEnsureMarkerDir(SG_SLEEP_LEASE_DIR)) return false;
    // Never re-query a bare PID and accidentally persist the replacement's
    // creation time between discovery and marker publication.
    const auto path = SG_SLEEP_LEASE_DIR + L"\\" + std::to_wstring(pid) + L".txt";
    return sgWriteFileAtomic(path, "pid=" + std::to_string(pid) + "|created=" + std::to_string(created) +
        "|epoch=" + std::to_string(sgNowEpoch()) + "|state=suspended|generation=" +
        std::to_string(generation) + "|origin=explicit-wake-recovery");
}
static void sgWakeRecoveryDiscoverFallback() {
    if (!g_guardEnabled || !g_sgPauseResume) return;
    std::vector<std::wstring> excludes, whitelist, configured;
    {
        std::lock_guard<std::mutex> rulesLock(g_gameRulesMx);
        excludes = sgExcludes(); whitelist = sgGameWhitelist(); configured = nativeConfiguredGameExes();
    }
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return;
    // Reuse the same owning topology as nativeScanGame. Partial/unavailable
    // rows cannot authorize a recovery; do not invalidate or force a refresh.
    const auto topology = ymcc::processInventory::read();
    if (topology->status != ymcc::processInventory::Status::Complete) return;
    unsigned admitted = 0;
    for (const auto& row : topology->rows) {
        const DWORD pid = row.pid;
        const std::wstring name = sgBaseName(row.name);
        // Blacklist ALWAYS wins in broad recovery, including contradictory
        // whitelist/custom rules. Respect system and user lists together.
        if (nativeMonitorExcluded(name) || sgNameExcludedBy(name, excludes)) continue;
        // A manual marker not in our snapshot may have been created AFTER
        // event receipt. Never rediscover that new pause as an orphan.
        if (sgMarkerIsSuspended(SG_MANUAL_DIR, pid)) continue;
        if (std::any_of(g_sgWakeRecovery.entries.begin(), g_sgWakeRecovery.entries.end(),
            [pid](const auto& e) { return e.pid == pid; })) continue;
        NativeDetectedGame game;
        if (nativeScanGameOnePid(pid, name, excludes, whitelist, configured, &game) != NativeOnePidResult::Ok)
            continue;
        const auto floor = game.whitelisted ? SG_CAPTURE_MIN_WS : SG_MIN_WS;
        if (game.workingSet < floor || game.path.empty()) continue;
        const auto actualName = sgBaseName(game.path);
        if (nativeMonitorExcluded(actualName) || sgNameExcludedBy(actualName, excludes)) continue;
        ymcc::sleepLegacyResume::File process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SUSPEND_RESUME, FALSE, pid));
        if (!process) continue;
        const auto h = process.get();
        DWORD targetSession = 0; BOOL debugged = TRUE;
        const bool haveDebugger = CheckRemoteDebuggerPresent(h, &debugged) != FALSE;
        const auto threads = WakeGameResume::queryThreads(pid);
        const WakeGameResume::FallbackGate gate{
            true, ProcessIdToSessionId(pid, &targetSession) && targetSession == session,
            WakeGameResume::sameUser(h), false, false, game.workingSet >= floor,
            !haveDebugger || debugged != FALSE,
            nativeProcessCreatedFromHandle(h) == game.processCreated, threads.allSuspended()
        };
        process.reset();
        if (!WakeGameResume::allowFallback(gate)) continue;
        // Persist the newly authorized orphan identity before any side effect.
        if (!sgWriteWakeRecoveryMarker(pid, game.processCreated, g_sgWakeRecovery.generation) ||
            sgMarkerCreated(SG_SLEEP_LEASE_DIR, pid) != game.processCreated) continue;
        const auto text = sgReadFile(SG_SLEEP_LEASE_DIR + L"\\" + std::to_wstring(pid) + L".txt");
        g_sgWakeRecovery.entries.push_back({SG_SLEEP_LEASE_DIR, pid, game.processCreated, text, true, false, false, actualName});
        try { sgRecordFact("sleep-game-recovery-fallback", {
            {"generation", g_sgWakeRecovery.generation}, {"pid", pid},
            {"processCreated", std::to_string(game.processCreated)}, {"workingSet", game.workingSet},
            {"threads", threads.total}, {"systemAndUserBlacklistApplied", true}
        }); } catch (...) {}
        if (++admitted >= 64) {
            try { sgRecordFact("sleep-game-recovery-fallback-limit", {{"limit", 64}}); } catch (...) {}
            break;
        }
    }
}
static bool sgBeginWakeGameRecovery(unsigned long long generation,const SgWorkItem* delivery=nullptr) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    if (!generation || generation != currentPowerGeneration() || g_exitRequested.load() ||
        sgSleepRetryActive() || g_sleepPowerDispatch.pendingSuspend()) return false;
    const auto wakeSleepSequence = g_sleepPowerDispatch.observedSleepBoundarySequence();
    const auto wakeSuspendSerial = g_sharedPowerSuspendSerial.load(std::memory_order_acquire);
    if(delivery&&!delivery->wakeBoundaryMatches(wakeSleepSequence,wakeSuspendSerial))return false;
    const auto stillAdmitted = [&] {
        const auto phase = g_powerLifecycle.load(std::memory_order_acquire);
        return generation == currentPowerGeneration() && !g_exitRequested.load() &&
            phase != PowerLifecycle::Suspending && phase != PowerLifecycle::Suspended &&
            !sgSleepRetryActive() && !g_sleepPowerDispatch.pendingSuspend() &&
            wakeSleepSequence == g_sleepPowerDispatch.observedSleepBoundarySequence() &&
            wakeSuspendSerial == g_sharedPowerSuspendSerial.load(std::memory_order_acquire);
    };
    const auto captureFocusPower = [generation] {
        ymcc::sleeppower::SharedPowerV1 power;
        if (!ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower, power) ||
            power.generation != generation) return ymcc::sleeppower::SharedPowerV1{};
        return power;
    };
    const bool crossedSleepBoundary = g_sgWakeRecovery.crossedSleepBoundary(wakeSleepSequence, wakeSuspendSerial);
    if (g_sgLastGameWakeGeneration == generation && sgWakeGameRecoveryCanWaitForNextWakeUnlocked() &&
        crossedSleepBoundary &&
        std::none_of(g_sgWakeRecovery.entries.begin(), g_sgWakeRecovery.entries.end(),
            [](const auto& e) { return !e.done; })) {
        // A later sleep boundary retires a zero-dispatch snapshot even if a
        // more recent pause superseded its entries instead of leaving it empty.
        g_sgLastGameWakeGeneration = 0;
    }
    if (g_sgLastGameWakeGeneration == generation) {
        auto& batch = g_sgWakeRecovery;
        // A new explicit wake can reach the worker before its next recovery tick.
        const bool mustPark = sgWakeGameRecoveryHasWork() &&
            sgWakeGameRecoveryCanWaitForNextWakeUnlocked() && crossedSleepBoundary;
        if (!stillAdmitted()) return false;
        if ((!batch.waitingForNextWake && !mustPark) || batch.generation != generation) return true;
        const auto focusPower = captureFocusPower();
        if (!stillAdmitted()) return false;
        if (mustPark) {
            g_sgWakeRecoveryParked.store(false,std::memory_order_release);
            g_sgWakeRecoveryPending.store(false,std::memory_order_release);
            batch.waitingForNextWake = true;
        }
        batch.waitingForNextWake = false;
        batch.wakeSleepSequence = wakeSleepSequence;
        batch.wakeSuspendSerial = wakeSuspendSerial;
        if (std::none_of(batch.entries.begin(), batch.entries.end(), [](const auto& e) {return !e.done;})) {
            batch.entries.clear(); // Keep the boundary for the next admitted wake.
            return true;
        }
        batch.started = GetTickCount64();
        batch.nextAttempt = 0;
        batch.focusPower = focusPower;
        g_sgWakeRecoveryPending.store(true, std::memory_order_release);
        sgNotifyWork();
        try {
            sgRecordFact("sleep-game-recovery-continued", {{"generation", generation},
                {"reason", "explicit-wake-after-zero-dispatch"}, {"targets", batch.entries.size()}});
        } catch (...) {}
        return true;
    }
    sgCancelWakeGameRecoveryUnlocked("new-user-wake");
    g_sgWakeRecovery = {};
    g_sgWakeRecovery.generation = generation;
    g_sgWakeRecovery.explicitWake = true;
    g_sgWakeRecovery.wakeSleepSequence = wakeSleepSequence;
    g_sgWakeRecovery.wakeSuspendSerial = wakeSuspendSerial;
    g_sgWakeRecovery.focusPower = captureFocusPower();
    g_sgWakeRecovery.started = GetTickCount64();
    g_sgWakeRecovery.manualState = sgReadFile(SG_DIR + L"\\quickapp_suspended.json");
    SgSleepTarget target;
    const bool ownedSleepTarget = sgReadSleepTarget(target) && target.markerOwned;
    if (ownedSleepTarget && target.powerGeneration != 0 &&
        sgMarkerCreated(SG_SLEEP_LEASE_DIR, target.pid) == 0) {
        const ymcc::sleepLegacyResume::File process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid));
        if (process && nativeProcessCreatedFromHandle(process.get()) == target.processCreated)
            (void)sgWriteWakeRecoveryMarker(target.pid, target.processCreated, target.powerGeneration);
    }
    sgWakeRecoverySnapshotDirectory(SG_SLEEP_LEASE_DIR);
    sgWakeRecoverySnapshotDirectory(SG_MANUAL_DIR,delivery);
    // A manual/old-generation marker is still an exact recovery target.
    // Retained owner/marker evidence (including unavailable marker IO) must
    // not turn an access failure or a filtered new manual pause into a scan.
    if (g_sgWakeRecovery.entries.empty() && !ownedSleepTarget &&
        !sgHasMarkerFiles(SG_SLEEP_LEASE_DIR) && !sgHasMarkerFiles(SG_MANUAL_DIR))
        sgWakeRecoveryDiscoverFallback();
    // Power ingress is independent of the game-operation lock. A wake that
    // lost admission while preparing targets has not spent this generation.
    if (!stillAdmitted()) {
        g_sgWakeRecovery = {};
        return false;
    }
    // Publish the prepared batch before recording optional diagnostics.
    g_sgLastGameWakeGeneration = generation;
    g_sgWakeRecoveryPending.store(!g_sgWakeRecovery.entries.empty(), std::memory_order_release);
    sgNotifyWork();
    try {
        sgRecordFact("sleep-game-recovery-queued", {
            {"generation", generation}, {"targets", g_sgWakeRecovery.entries.size()},
            {"inheritedSleepTargets", std::count_if(g_sgWakeRecovery.entries.begin(),
                g_sgWakeRecovery.entries.end(), [generation](const auto& e) {
                    return e.sleepOwnerGeneration != 0 && e.sleepOwnerGeneration != generation;
                })},
            {"attemptOffsetsMs", std::vector<ULONGLONG>(WakeGameResume::attemptOffsetsMs,
                WakeGameResume::attemptOffsetsMs + WakeGameResume::attemptCount)}, {"includesPreWakeManual", true}
        });
    } catch (...) {}
    return true;
}
// Caller holds g_sgOpMtx and supplies only its native inspection snapshot.
// No manual-pause directory, orphan discovery, Steam restart or game launch.
static bool sgBeginUserSleepGameRecoveryUnlocked(unsigned long long generation,
    const std::vector<SgWakeRecoveryEntry>& expected,const std::string& requestId,unsigned long long autoEpoch,
    const std::shared_ptr<ymcc::inputHost::sharedPower::Scope>& admission) {
    const auto current=[&]{return admission&&admission->power().generation==generation&&admission->current();};
    if(!generation||generation!=currentPowerGeneration()||g_exitRequested.load()||
        g_powerLifecycle.load()!=PowerLifecycle::Ready||!g_inputReady.load()||sgSleepRetryActive()||
        g_sgWakeRecoveryPending.load()||expected.empty()||expected.size()>64||!current())return false;
    if(autoEpoch&&!sleepRecoveryAutoAllows(generation,autoEpoch))return false;
    auto entries=expected;
    for(auto& e:entries){
        if(e.dir!=SG_SLEEP_LEASE_DIR||e.fallback||!e.pid||!e.created||sgMarkerIsSuspended(SG_MANUAL_DIR,e.pid))return false;
        std::error_code ec;const bool exists=fspath::exists(sgWakeRecoveryMarker(e),ec);
        if(ec)return false;
        if(e.markerText.empty()){
            SgSleepTarget owner;
            if(exists||!e.sleepOwnerGeneration||!sgReadSleepTarget(owner)||!owner.markerOwned||
                owner.pid!=e.pid||owner.processCreated!=e.created||owner.powerGeneration!=e.sleepOwnerGeneration)return false;
            const ymcc::sleepLegacyResume::File process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.pid));
            if(!process||nativeProcessCreatedFromHandle(process.get())!=e.created)return false;
        }else{
            const auto bytes=fspath::file_size(sgWakeRecoveryMarker(e),ec);
            if(!exists||ec||bytes>8192||sgReadFile(sgWakeRecoveryMarker(e))!=e.markerText)return false;
        }
    }
    // Complete validation of EVERY inspected entry before restoring a lost marker.
    for(auto& e:entries)if(e.markerText.empty()){
        if(!current()||!sgWriteWakeRecoveryMarker(e.pid,e.created,e.sleepOwnerGeneration))return false;
        e.markerText=sgReadFile(sgWakeRecoveryMarker(e));
        if(e.markerText.empty())return false;
        try { sgRecordFact("sleep-game-user-marker-rebuilt", {{"requestId", requestId}, {"pid", e.pid},
            {"sleepOwnerGeneration", e.sleepOwnerGeneration}}); } catch (...) {}
    }
    if(!current())return false;
    g_sgWakeRecovery={};g_sgWakeRecovery.generation=generation;
    g_sgWakeRecoveryParked.store(false,std::memory_order_release);
    g_sgWakeRecovery.started=GetTickCount64();g_sgWakeRecovery.entries=std::move(entries);
    g_sgWakeRecovery.requestId=requestId;g_sgWakeRecovery.focusOnComplete=false;g_sgWakeRecovery.autoConfigEpoch=autoEpoch;
    g_sgWakeRecovery.userRepairPower=admission;
    g_sgWakeRecoveryPending.store(true,std::memory_order_release);
    sgNotifyWork();
    try { sgRecordFact("sleep-game-user-recovery-queued", {{"generation", generation}, {"requestId", requestId},
        {"targets", expected.size()}, {"scope", "sleep-markers-only"}, {"healthVerified", false}}); } catch (...) {}
    return true;
}
// The same five attempt offsets own scheduling. Readiness alone has a bounded
static bool sgParkedGameWakeInterrupted() {
    const auto phase = g_powerLifecycle.load(std::memory_order_acquire);
    return g_exitRequested.load(std::memory_order_acquire) ||
        g_sgWakeRecovery.generation != currentPowerGeneration() ||
        phase == PowerLifecycle::Suspending || phase == PowerLifecycle::Suspended || sgSleepRetryActive() ||
        g_sleepPowerDispatch.pendingSuspend() || sgGameWakeBoundaryChanged();
}
static bool sgCaptureGameWakeReady(unsigned long long generation, ymcc::sleeppower::SharedPowerV1& power) {
    return g_inputReady.load(std::memory_order_acquire) &&
        ymcc::sleeppower::capturePower(&readSharedSystemPower, power) &&
        power.generation == generation;
}
static bool sgGameWakeReady(unsigned long long generation) {
    ymcc::sleeppower::SharedPowerV1 power;
    return sgCaptureGameWakeReady(generation, power);
}
static bool sgParkGameWakeIfNotReady() {
    if (!sgWakeGameRecoveryCanWaitForNextWakeUnlocked() ||
        sgGameWakeReady(g_sgWakeRecovery.generation)) return false;
    if (sgParkedGameWakeInterrupted()) { sgInterruptWakeGameRecoveryUnlocked(); return true; }
    // Wait for a public Ready delivery without polling or consuming this wake.
    g_sgWakeRecoveryParked.store(true,std::memory_order_release);
    g_sgWakeRecoveryPending.store(false,std::memory_order_release);
    try { sgRecordFact("sleep-game-recovery-awaiting-ready", {{"generation", g_sgWakeRecovery.generation},
        {"targets", g_sgWakeRecovery.entries.size()}}); } catch (...) {}
    return true;
}

// 250ms fallback while this short wake batch exists; parked and idle work do not poll.
static ULONGLONG sgWakeGameRecoveryWaitMs(ULONGLONG now) {
    if (!sgWakeGameRecoveryHasWork()) return ~ULONGLONG{0};
    std::lock_guard<std::mutex> lock(g_sgOpMtx);
    if (g_sgWakeRecoveryParked.load(std::memory_order_acquire)) {
        ymcc::sleeppower::SharedPowerV1 power;
        return sgParkedGameWakeInterrupted() || sgCaptureGameWakeReady(g_sgWakeRecovery.generation, power) ? 0 : ~ULONGLONG{0};
    }
    if (!g_sgWakeRecoveryPending.load(std::memory_order_acquire)) return ~ULONGLONG{0};
    if (!g_sgWakeRecoveryPending.load(std::memory_order_acquire)) return ~ULONGLONG{0};
    const auto phase = g_powerLifecycle.load(std::memory_order_acquire);
    if (g_exitRequested.load() || g_sgWakeRecovery.generation != currentPowerGeneration() ||
        phase == PowerLifecycle::Suspending || phase == PowerLifecycle::Suspended || sgSleepRetryActive() ||
        g_sleepPowerDispatch.pendingSuspend() || sgGameWakeBoundaryChanged() ||
        (g_sgWakeRecovery.userRepairPower && !g_sgWakeRecovery.userRepairPower->current()) ||
        (g_sgWakeRecovery.autoConfigEpoch && !sleepRecoveryAutoAllows(g_sgWakeRecovery.generation,g_sgWakeRecovery.autoConfigEpoch)) ||
        now < g_sgWakeRecovery.started) return 0;
    if (std::any_of(g_sgWakeRecovery.entries.begin(),g_sgWakeRecovery.entries.end(),[](const auto& e){
        return !e.done && !sgCanceledSleepRecoveryBoundaryCurrent(e);
    })) return 0;
    const auto elapsed = now - g_sgWakeRecovery.started;
    const auto attempt = g_sgWakeRecovery.nextAttempt;
    if (attempt >= WakeGameResume::attemptCount) return 0;
    const auto due = WakeGameResume::attemptOffsetsMs[attempt];
    if (elapsed < due) return due - elapsed;
    const auto last = WakeGameResume::attemptOffsetsMs[WakeGameResume::attemptCount - 1];
    if (elapsed < last && !sgGameWakeReady(g_sgWakeRecovery.generation))
        return (std::min)(ULONGLONG{250}, last - elapsed);
    return 0;
}
static void sgWakeGameRecoveryTickUnlocked() {
    if (g_sgWakeRecoveryParked.load(std::memory_order_acquire)) {
        if (sgParkedGameWakeInterrupted()) { sgInterruptWakeGameRecoveryUnlocked(); return; }
        ymcc::sleeppower::SharedPowerV1 power;
        if (!sgCaptureGameWakeReady(g_sgWakeRecovery.generation, power) || sgParkedGameWakeInterrupted()) return;
        g_sgWakeRecovery.started = GetTickCount64();
        g_sgWakeRecovery.nextAttempt = 0;
        g_sgWakeRecovery.focusPower = power;
        g_sgWakeRecoveryPending.store(true, std::memory_order_release);
        g_sgWakeRecoveryParked.store(false,std::memory_order_release);
    }
    if (!g_sgWakeRecoveryPending.load(std::memory_order_acquire)) return;
    const auto generation = g_sgWakeRecovery.generation;
    const auto phase = g_powerLifecycle.load(std::memory_order_acquire);
    const auto userRepairPowerCurrent = [&] {
        return !g_sgWakeRecovery.userRepairPower || g_sgWakeRecovery.userRepairPower->current();
    };
    if (!userRepairPowerCurrent()) {
        sgCancelWakeGameRecoveryUnlocked("user-repair-power-revoked"); return;
    }
    if (g_exitRequested.load() || generation != currentPowerGeneration() ||
        phase == PowerLifecycle::Suspending || phase == PowerLifecycle::Suspended || sgSleepRetryActive() ||
        g_sleepPowerDispatch.pendingSuspend() || sgGameWakeBoundaryChanged()) {
        sgInterruptWakeGameRecoveryUnlocked(); return;
    }
    if(g_sgWakeRecovery.autoConfigEpoch&&!sleepRecoveryAutoAllows(generation,g_sgWakeRecovery.autoConfigEpoch)){
        sgCancelWakeGameRecoveryUnlocked("automatic-recovery-disabled");return;
    }
    // A newer sleep invalidates old cancellation authority even while the
    // public gate is closed. Finish the volatile work and leave its lease alone.
    bool hasLiveEntry=false;
    for (auto& e:g_sgWakeRecovery.entries) {
        if (!e.done && !sgCanceledSleepRecoveryBoundaryCurrent(e)) e.done=true;
        hasLiveEntry=hasLiveEntry || !e.done;
    }
    // The operation mutex protects this batch, not concurrent power/exit or
    // automatic-recovery admission. Revalidate native dispatch and the
    // observed marker ownership before retiring this batch's resources.
    const auto batchCurrent = [&] {
        const auto currentPhase = g_powerLifecycle.load(std::memory_order_acquire);
        return generation == currentPowerGeneration() && !g_exitRequested.load(std::memory_order_acquire) &&
            currentPhase != PowerLifecycle::Suspending && currentPhase != PowerLifecycle::Suspended &&
            !sgSleepRetryActive() && !g_sleepPowerDispatch.pendingSuspend() &&
            !sgGameWakeBoundaryChanged() && userRepairPowerCurrent() &&
            (!g_sgWakeRecovery.autoConfigEpoch ||
             sleepRecoveryAutoAllows(generation, g_sgWakeRecovery.autoConfigEpoch));
    };
    const auto cancelChangedBatch = [&] {
        const bool automaticDisabled = g_sgWakeRecovery.autoConfigEpoch &&
            !sleepRecoveryAutoAllows(generation, g_sgWakeRecovery.autoConfigEpoch);
        if (automaticDisabled) sgCancelWakeGameRecoveryUnlocked("automatic-recovery-disabled");
        else if (!userRepairPowerCurrent()) sgCancelWakeGameRecoveryUnlocked("user-repair-power-revoked");
        else sgInterruptWakeGameRecoveryUnlocked();
    };
    const auto elapsed = GetTickCount64() - g_sgWakeRecovery.started;
    const auto attempt = g_sgWakeRecovery.nextAttempt;
    if (hasLiveEntry && elapsed < WakeGameResume::attemptOffsetsMs[attempt]) return;
    const bool final = attempt + 1 == WakeGameResume::attemptCount ||
        elapsed >= WakeGameResume::attemptOffsetsMs[WakeGameResume::attemptCount - 1];
    const bool ready = sgGameWakeReady(generation);
    if (hasLiveEntry && !ready && !final) return;
    if (hasLiveEntry && !ready && sgParkGameWakeIfNotReady()) return;
    if (hasLiveEntry) sgInitNt();
    unsigned confirmed = 0;
    for (auto& e : g_sgWakeRecovery.entries) {
        if (e.done) continue;
        if (!sgCanceledSleepRecoveryEntryCurrent(e)) { e.done=true; continue; }
        if (e.resumeBlockedByNewPause) continue;
        // Only this boundary may retire the vector. The drain may only mark
        // that an NT call is about to run.
        if (!batchCurrent()) { cancelChangedBatch(); return; }
        // A user repair must never consume a manual pause introduced AFTER inspection.
        if(!g_sgWakeRecovery.requestId.empty()&&sgMarkerIsSuspended(SG_MANUAL_DIR,e.pid)){
            continue;
        }
        const auto fileWorkCurrent = [&] {
            return batchCurrent() && sgCanceledSleepRecoveryEntryCurrent(e) &&
                sgGameWakeReady(generation);
        };
        auto files = sgAcquireResumeFiles(e.dir, e.pid, fileWorkCurrent, e.created);
        const auto cancelAfterFileReturn = [&] {
            files = {}; // Cancellation can acquire the same PID's files for retirement.
            cancelChangedBatch();
        };
        if (files.result == ymcc::sleepLegacyResume::Result::Cleaned) {
            if (files.completedBirth == e.created) sgWakeRecoveryClearTarget(e);
            e.done = true;
            continue;
        }
        if (!files.current(fileWorkCurrent)) continue;
        const auto marker = sgWakeRecoveryMarker(e);
        if (e.canceledLease && e.markerText.empty()) {
            // Deferred capture grants no action by itself. It belongs only to
            // this exact held owner and the original cancellation boundary.
            if (!ready) continue;
            std::string captured;
            if (!sgReadResumeMarkerChecked(marker, captured)) continue;
            ULONGLONG capturedGeneration = 0, capturedCreated = 0;
            const auto generationPos = captured.find("|generation=");
            const auto createdPos = captured.find("created=");
            try {
                if (generationPos != std::string::npos)
                    capturedGeneration = std::stoull(captured.substr(generationPos + 12));
                if (createdPos != std::string::npos)
                    capturedCreated = std::stoull(captured.substr(createdPos + 8));
            } catch (const std::bad_alloc&) { continue; }
              catch (...) { e.done = true; continue; }
            const auto statePos = captured.find("|state=suspended");
            const auto stateEnd = statePos == std::string::npos ? 0 : statePos + 16;
            const bool suspended = statePos != std::string::npos &&
                (stateEnd == captured.size() || captured[stateEnd] == '|' ||
                 captured[stateEnd] == '\r' || captured[stateEnd] == '\n');
            if (capturedGeneration != e.sleepOwnerGeneration || capturedCreated != e.created || !suspended) {
                e.done = true; continue; // Keep replacement marker and owner untouched.
            }
            if (!nativeValveValidateOwnedSuspendedTarget(e.dir, e.pid, e.created) ||
                !fileWorkCurrent() || sgReadFile(marker) != captured || !fileWorkCurrent()) continue;
            e.markerText = std::move(captured); // Immutable after the first validated capture.
        }
        const auto entryCurrent = [&] {
            return files.current(fileWorkCurrent) &&
                (g_sgWakeRecovery.requestId.empty() || !sgMarkerIsSuspended(SG_MANUAL_DIR, e.pid)) &&
                sgReadFile(marker) == e.markerText && fileWorkCurrent();
        };
        const auto textNow = sgReadFile(marker);
        if (textNow != e.markerText) {
            std::error_code markerError;
            const bool exists = fspath::exists(marker, markerError);
            if (markerError || (exists && textNow.empty())) {
                // A temporary read failure cannot discard the recovery entry.
                continue;
            }
            if (exists) {
                // A new owner/marker supersedes the snapshot. Never replace it.
                e.done = true;
                try { sgRecordFact("sleep-game-recovery-superseded", {
                    {"generation", generation}, {"pid", e.pid}, {"reason", "marker-changed"}
                }); } catch (...) {}
                continue;
            }
            ULONGLONG currentCreated = 0;
            const bool sameIdentity = focusQueryProcessIdentity(e.pid, nullptr, &currentCreated) &&
                currentCreated == e.created;
            const bool alreadyRunning = sameIdentity && WakeGameResume::queryThreads(e.pid).allRunning();
            std::error_code currentMarkerError;
            const bool markerStillMissing = !fspath::exists(marker, currentMarkerError);
            if (alreadyRunning && !currentMarkerError && markerStillMissing && fileWorkCurrent()) {
                // Manual resume already completed. Retire ONLY our old owner.
                sgWakeRecoveryClearTarget(e); e.done = true;
                continue;
            }
            // Lost file during recovery: retain/reconstruct the captured exact
            // lease, including old identity. A reused PID will still fail the
            // opened-handle check below; no replacement identity is invented.
            if (!batchCurrent()) { cancelAfterFileReturn(); return; }
            if (currentMarkerError || !markerStillMissing) continue;
            if (!files.current(fileWorkCurrent) || !sgWriteFileAtomic(marker, e.markerText)) {
                continue;
            }
            try { sgRecordFact("sleep-game-recovery-marker-rebuilt", {{"generation", generation}, {"pid", e.pid}}); } catch (...) {}
        }
        const bool entryReady = fileWorkCurrent();
        ymcc::sleepLegacyResume::File process(entryReady ? OpenProcess(PROCESS_SUSPEND_RESUME |
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, e.pid) : nullptr);
        const auto h = process.get();
        const DWORD openError = h || !entryReady ? ERROR_SUCCESS : GetLastError();
        if (!batchCurrent()) {
            process.reset();
            cancelAfterFileReturn(); return;
        }
        const bool identityMatches = h && nativeProcessCreatedFromHandle(h) == e.created;
        const bool exited = h && WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
        if ((h && (!identityMatches || exited)) || openError == ERROR_INVALID_PARAMETER) {
            process.reset();
            if (!batchCurrent()) { cancelAfterFileReturn(); return; }
            if (sgReadFile(marker) != e.markerText || !files.current(fileWorkCurrent)) continue;
            std::error_code ec;
            const bool removed = fspath::remove(marker, ec);
            // Keep file failures in the original bounded cleanup window.
            if (!ec) { sgWakeRecoveryClearTarget(e); e.done = true; }
            try { sgRecordFact("sleep-game-recovery-stale", {
                {"generation", generation}, {"pid", e.pid}, {"markerRemoved", removed},
                {"markerRemovalError", ec.value()}, {"cleanupRetained", static_cast<bool>(ec)}
            }); } catch (...) {}
            continue;
        }
        DWORD targetSession = 0, ownSession = 0;
        const bool sameSession = h && ProcessIdToSessionId(e.pid, &targetSession) &&
            ProcessIdToSessionId(GetCurrentProcessId(), &ownSession) && targetSession == ownSession;
        bool fallbackStillAllowed = true;
        if (h && e.fallback) {
            BOOL debugged = TRUE;
            bool excluded = true;
            {
                std::lock_guard<std::mutex> rulesLock(g_gameRulesMx);
                excluded = nativeMonitorExcluded(e.fallbackName) || sgNameExcludedBy(e.fallbackName, sgExcludes());
            }
            fallbackStillAllowed = !excluded && WakeGameResume::sameUser(h) &&
                CheckRemoteDebuggerPresent(h, &debugged) && !debugged;
        }
        const auto dispatchCurrent = [&] {
            if (!entryCurrent()) return false;
            // The original drain calls NT immediately after this check. A
            // failed call still consumes the right to replay the whole wake.
            g_sgWakeRecovery.resumeDispatched = true;
            return true;
        };
        const bool accepted = identityMatches && sameSession && fallbackStillAllowed &&
            fnNtResume && entryCurrent() && sgDrainProcessSuspendCountIfCurrent(h, dispatchCurrent);
        e.acceptedOnce = e.acceptedOnce || accepted;
        const auto threads = identityMatches ? WakeGameResume::queryThreads(e.pid) : WakeGameResume::ThreadState{};
        process.reset();
        try { sgRecordFact("sleep-game-recovery-attempt", {
            {"generation", generation}, {"pid", e.pid}, {"processCreated", std::to_string(e.created)},
            {"owner", e.fallback ? "orphan-fallback" : e.dir == SG_MANUAL_DIR ? "manual" : "sleep"},
            {"sleepOwnerGeneration", e.sleepOwnerGeneration},
            {"attempt", attempt + 1}, {"elapsedMs", elapsed}, {"apiAccepted", accepted},
            {"ready", entryReady}, {"openError", openError}, {"sameSession", sameSession},
            {"fallbackStillAllowed", fallbackStillAllowed}, {"threadStateKnown", threads.known},
            {"threads", threads.total}, {"suspendedThreads", threads.suspended}, {"final", final}
        }); } catch (...) {}
        // The current call plus the complete, stable two-snapshot thread set
        // is sufficient; waiting for final would hold an already-running lease.
        // Never borrow acceptedOnce or treat this as rendering/input health.
        if (accepted && threads.allRunning()) {
            ULONGLONG createdNow = 0;
            if (focusQueryProcessIdentity(e.pid, nullptr, &createdNow) && createdNow == e.created &&
                fileWorkCurrent() &&
                (g_sgWakeRecovery.requestId.empty() || !sgMarkerIsSuspended(SG_MANUAL_DIR, e.pid)) &&
                sgReadFile(marker) == e.markerText && files.current(fileWorkCurrent)) {
                std::error_code ec;
                const bool removed = fspath::remove(marker, ec);
                if (removed && !ec) {
                    sgWakeRecoveryClearTarget(e); e.done = true; ++confirmed;
                    try { sgRecordFact("sleep-game-recovery-confirmed", {
                        {"generation", generation}, {"pid", e.pid}, {"processCreated", std::to_string(e.created)},
                        {"attempt", attempt + 1}, {"elapsedMs", elapsed}, {"final", final},
                        {"basis", "current-attempt-zero-suspend-counts"}, {"markerRemoved", true}
                    }); } catch (...) {}
                    if(g_sgWakeRecovery.focusOnComplete&&!e.canceledLease&&
                       ymcc::sleeppower::powerObservationKnown(g_sgWakeRecovery.focusPower))
                        focusSingleResumedGame({e.pid}, e.created, g_sgWakeRecovery.focusPower,
                            g_sgWakeRecovery.wakeSleepSequence);
                }
            }
        }
    }
    if (!batchCurrent() && (sgGameWakeBoundaryChanged() || sgWakeGameRecoveryCanWaitForNextWakeUnlocked())) {
        cancelChangedBatch(); return;
    }
    const auto pending = std::count_if(g_sgWakeRecovery.entries.begin(), g_sgWakeRecovery.entries.end(),
        [](const auto& e) { return !e.done; });
    if (final && pending != 0 && sgParkGameWakeIfNotReady()) return;
    if (!final && pending != 0) {
        // A late worker must never replay overdue attempts in a tight burst.
        ++g_sgWakeRecovery.nextAttempt;
        while (g_sgWakeRecovery.nextAttempt < WakeGameResume::attemptCount - 1 &&
               WakeGameResume::attemptOffsetsMs[g_sgWakeRecovery.nextAttempt] <= elapsed)
            ++g_sgWakeRecovery.nextAttempt;
    }
    try { sgRecordFact("sleep-game-recovery-progress", {
        {"generation", generation}, {"attempt", attempt + 1}, {"elapsedMs", elapsed},
        {"confirmedThisAttempt", confirmed}, {"pendingTargets", pending}, {"final", final}
    }); } catch (...) {}
    if (final || pending == 0) {
        const bool manualPending = sgHasMarkerFiles(SG_MANUAL_DIR);
        if (!manualPending&&!g_sgWakeRecovery.canceledLeasesOnly) {
            g_manualPausedPid.store(0, std::memory_order_release);
            const auto statePath = SG_DIR + L"\\quickapp_suspended.json";
            if (!g_sgWakeRecovery.manualState.empty() && sgReadFile(statePath) == g_sgWakeRecovery.manualState) {
                std::error_code ec; fspath::remove(statePath, ec);
            }
        }
        // A failed orphan recovery can retain a marker without a volatile
        // owner. Do not let the next sleep treat that lease as unowned.
        sgReconcileSleepGameFlagsAfterRecoveryCancel("wake-recovery-complete");
        auto requestId = std::move(g_sgWakeRecovery.requestId);
        // Hold only the dedup boundary after zero dispatch. A later explicit
        // wake in a new boundary can rediscover the still-owned exact leases.
        g_sgWakeRecovery = sgWakeGameRecoveryTerminalBoundaryUnlocked();
        g_sgWakeRecoveryPending.store(false, std::memory_order_release);
        try { sgRecordFact("sleep-game-recovery-complete", {
            {"generation", generation}, {"requestId",requestId}, {"elapsedMs", elapsed}, {"confirmedThisAttempt", confirmed},
            {"basis", pending == 0 ? "all-targets-done" : "attempt-window-exhausted"},
            {"attempt", attempt + 1}, {"pendingTargets", pending}, {"final", final},
            {"retainedTargets", pending}, {"manualPending", manualPending}, {"healthVerified", false}
        }); } catch (...) {}
        postWakeGameRecoveryStatus(generation, manualPending, requestId, attempt + 1, pending);
    }
}
static void sgWakeGameRecoveryTick() {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    // Retire the exact failed batch before another caller can replace it.
    try { sgWakeGameRecoveryTickUnlocked(); }
    catch (...) { sgFailWakeGameRecoveryUnlocked(); }
}
