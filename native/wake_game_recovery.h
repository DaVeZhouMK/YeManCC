#pragma once
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
};
struct SgWakeRecoveryBatch {
    unsigned long long generation = 0;
    ULONGLONG started = 0;
    size_t nextAttempt = 0;
    std::string manualState;
    std::vector<SgWakeRecoveryEntry> entries;
    std::string requestId;
    bool focusOnComplete = true;
    unsigned long long autoConfigEpoch = 0;
};
static SgWakeRecoveryBatch g_sgWakeRecovery;
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
        sgRecordFact("sleep-game-lease-state-reconciled", {
            {"reason", reason ? reason : "recovery-cancel"},
            {"ownedSleepLease", ownedSleepLease},
            {"retainedSleepMarker", retainedSleepMarker},
            {"inSuspendBefore", beforeInSuspend},
            {"gameActuallySuspendedBefore", beforeActuallySuspended},
            {"inSuspendAfter", shouldRemainSuspended},
            {"gameActuallySuspendedAfter", shouldRemainSuspended}
        });
    }
}
static void sgCancelWakeGameRecoveryUnlocked(const char* reason) {
    if (!g_sgWakeRecoveryPending.load(std::memory_order_acquire)) {
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
        if (e.done || !e.acceptedOnce || sgReadFile(sgWakeRecoveryMarker(e)) != e.markerText) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.pid);
        const bool matches = h && nativeProcessCreatedFromHandle(h) == e.created;
        if (h) CloseHandle(h);
        if (!matches || !WakeGameResume::queryThreads(e.pid).allRunning()) continue;
        std::error_code ec; fspath::remove(sgWakeRecoveryMarker(e), ec);
        if (!ec) { sgWakeRecoveryClearTarget(e); ++retired; }
    }
    sgRecordFact("sleep-game-recovery-canceled", {
        {"generation", g_sgWakeRecovery.generation}, {"reason", reason ? reason : "unknown"},
        {"retiredConfirmedRunning", retired}, {"unconfirmedMarkersRetained", true}
    });
    g_sgWakeRecovery = {};
    g_sgWakeRecoveryPending.store(false, std::memory_order_release);
    sgReconcileSleepGameFlagsAfterRecoveryCancel(reason);
}
static bool sgPrepareWakeRecoveryForPauseUnlocked(DWORD pid) {
    if (!g_sgWakeRecoveryPending.load(std::memory_order_acquire)) return true;
    bool safe = true;
    for (auto& e : g_sgWakeRecovery.entries) {
        if (e.done || e.pid != pid) continue;
        // A NEW manual pause must not reuse an already-resumed confirmation
        // marker or be consumed by a delayed attempt from the old wake.
        if (e.acceptedOnce && sgReadFile(sgWakeRecoveryMarker(e)) == e.markerText) {
            if (WakeGameResume::queryThreads(pid).allRunning()) {
                std::error_code ec; fspath::remove(sgWakeRecoveryMarker(e), ec);
                if (!ec) sgWakeRecoveryClearTarget(e);
                else safe = false;
            } else safe = false; // don't report a new pause that was a no-op
        }
        e.done = true;
        sgRecordFact("sleep-game-recovery-superseded", {
            {"generation", g_sgWakeRecovery.generation}, {"pid", pid}, {"reason", "new-pause"}
        });
    }
    sgReconcileSleepGameFlagsAfterRecoveryCancel("new-pause");
    return safe;
}
static void sgPrepareWakeRecoveryForSleep(unsigned long long generation) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    if (!g_sgWakeRecoveryPending.load(std::memory_order_acquire) ||
        g_sgWakeRecovery.generation == generation) {
        sgReconcileSleepGameFlagsAfterRecoveryCancel("new-sleep-check");
        return;
    }
    // Cancellation retires only confirmed-running markers before capture.
    sgCancelWakeGameRecoveryUnlocked("new-sleep-before-capture");
}
static void sgWakeRecoverySnapshotDirectory(const std::wstring& dir) {
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
        if (dir == SG_MANUAL_DIR &&
            g_sgExplicitGameWakeGeneration.load(std::memory_order_acquire) == g_sgWakeRecovery.generation) {
            const auto serialPos = text.find("|pauseSerial=");
            if (serialPos != std::string::npos &&
                text.find("|pauseRun=" + sgManualPauseRunToken() + "|pauseSerial=") != std::string::npos) {
                try {
                    if (std::stoull(text.substr(serialPos + 13)) >
                        g_sgExplicitGameWakePauseFence.load(std::memory_order_acquire)) continue;
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
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W process{}; process.dwSize = sizeof(process);
    unsigned admitted = 0;
    if (Process32FirstW(snap, &process)) do {
        const DWORD pid = process.th32ProcessID;
        const std::wstring name = sgBaseName(process.szExeFile);
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
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SUSPEND_RESUME, FALSE, pid);
        if (!h) continue;
        DWORD targetSession = 0; BOOL debugged = TRUE;
        const bool haveDebugger = CheckRemoteDebuggerPresent(h, &debugged) != FALSE;
        const auto threads = WakeGameResume::queryThreads(pid);
        const WakeGameResume::FallbackGate gate{
            true, ProcessIdToSessionId(pid, &targetSession) && targetSession == session,
            WakeGameResume::sameUser(h), false, false, game.workingSet >= floor,
            !haveDebugger || debugged != FALSE,
            nativeProcessCreatedFromHandle(h) == game.processCreated, threads.allSuspended()
        };
        CloseHandle(h);
        if (!WakeGameResume::allowFallback(gate)) continue;
        // Persist the newly authorized orphan identity before any side effect.
        if (!sgWriteWakeRecoveryMarker(pid, game.processCreated, g_sgWakeRecovery.generation) ||
            sgMarkerCreated(SG_SLEEP_LEASE_DIR, pid) != game.processCreated) continue;
        const auto text = sgReadFile(SG_SLEEP_LEASE_DIR + L"\\" + std::to_wstring(pid) + L".txt");
        g_sgWakeRecovery.entries.push_back({SG_SLEEP_LEASE_DIR, pid, game.processCreated, text, true, false, false, actualName});
        sgRecordFact("sleep-game-recovery-fallback", {
            {"generation", g_sgWakeRecovery.generation}, {"pid", pid},
            {"processCreated", std::to_string(game.processCreated)}, {"workingSet", game.workingSet},
            {"threads", threads.total}, {"systemAndUserBlacklistApplied", true}
        });
        if (++admitted >= 64) { sgRecordFact("sleep-game-recovery-fallback-limit", {{"limit", 64}}); break; }
    } while (Process32NextW(snap, &process));
    CloseHandle(snap);
}
static void sgBeginWakeGameRecovery(unsigned long long generation) {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    if (!generation || generation != currentPowerGeneration() || g_exitRequested.load() ||
        g_sgLastGameWakeGeneration == generation) return;
    sgCancelWakeGameRecoveryUnlocked("new-user-wake");
    g_sgLastGameWakeGeneration = generation;
    g_sgWakeRecovery = {};
    g_sgWakeRecovery.generation = generation;
    g_sgWakeRecovery.started = GetTickCount64();
    g_sgWakeRecovery.manualState = sgReadFile(SG_DIR + L"\\quickapp_suspended.json");
    SgSleepTarget target;
    if (sgReadSleepTarget(target) && target.markerOwned && target.powerGeneration == generation &&
        sgMarkerCreated(SG_SLEEP_LEASE_DIR, target.pid) == 0) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
        if (h && nativeProcessCreatedFromHandle(h) == target.processCreated)
            (void)sgWriteWakeRecoveryMarker(target.pid, target.processCreated, generation);
        if (h) CloseHandle(h);
    }
    sgWakeRecoverySnapshotDirectory(SG_SLEEP_LEASE_DIR);
    sgWakeRecoverySnapshotDirectory(SG_MANUAL_DIR);
    sgWakeRecoveryDiscoverFallback();
    g_sgWakeRecoveryPending.store(!g_sgWakeRecovery.entries.empty(), std::memory_order_release);
    sgRecordFact("sleep-game-recovery-queued", {
        {"generation", generation}, {"targets", g_sgWakeRecovery.entries.size()},
        {"inheritedSleepTargets", std::count_if(g_sgWakeRecovery.entries.begin(),
            g_sgWakeRecovery.entries.end(), [generation](const auto& e) {
                return e.sleepOwnerGeneration != 0 && e.sleepOwnerGeneration != generation;
            })},
        {"attemptOffsetsMs", std::vector<ULONGLONG>(WakeGameResume::attemptOffsetsMs,
            WakeGameResume::attemptOffsetsMs + WakeGameResume::attemptCount)}, {"includesPreWakeManual", true}
    });
    g_sgWorkCv.notify_one();
}
// Caller holds g_sgOpMtx and supplies only its native inspection snapshot.
// No manual-pause directory, orphan discovery, Steam restart or game launch.
static bool sgBeginUserSleepGameRecoveryUnlocked(unsigned long long generation,
    const std::vector<SgWakeRecoveryEntry>& expected,const std::string& requestId,unsigned long long autoEpoch=0) {
    if(!generation||generation!=currentPowerGeneration()||g_exitRequested.load()||
        g_powerLifecycle.load()!=PowerLifecycle::Ready||!g_inputReady.load()||sgSleepRetryActive()||
        g_sgWakeRecoveryPending.load()||expected.empty()||expected.size()>64)return false;
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
            HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.pid);
            const bool same=h&&nativeProcessCreatedFromHandle(h)==e.created;if(h)CloseHandle(h);
            if(!same)return false;
        }else{
            const auto bytes=fspath::file_size(sgWakeRecoveryMarker(e),ec);
            if(!exists||ec||bytes>8192||sgReadFile(sgWakeRecoveryMarker(e))!=e.markerText)return false;
        }
    }
    // Complete validation of EVERY inspected entry before restoring a lost marker.
    for(auto& e:entries)if(e.markerText.empty()){
        if(!sgWriteWakeRecoveryMarker(e.pid,e.created,e.sleepOwnerGeneration))return false;
        e.markerText=sgReadFile(sgWakeRecoveryMarker(e));
        if(e.markerText.empty())return false;
        sgRecordFact("sleep-game-user-marker-rebuilt",{{"requestId",requestId},{"pid",e.pid},{"sleepOwnerGeneration",e.sleepOwnerGeneration}});
    }
    g_sgWakeRecovery={};g_sgWakeRecovery.generation=generation;
    g_sgWakeRecovery.started=GetTickCount64();g_sgWakeRecovery.entries=std::move(entries);
    g_sgWakeRecovery.requestId=requestId;g_sgWakeRecovery.focusOnComplete=false;g_sgWakeRecovery.autoConfigEpoch=autoEpoch;
    g_sgWakeRecoveryPending.store(true,std::memory_order_release);
    sgRecordFact("sleep-game-user-recovery-queued",{{"generation",generation},{"requestId",requestId},
        {"targets",expected.size()},{"scope","sleep-markers-only"},{"healthVerified",false}});
    g_sgWorkCv.notify_one();return true;
}
static void sgWakeGameRecoveryTick() {
    std::lock_guard<std::mutex> opLock(g_sgOpMtx);
    if (!g_sgWakeRecoveryPending.load(std::memory_order_acquire)) return;
    const auto generation = g_sgWakeRecovery.generation;
    const auto phase = g_powerLifecycle.load(std::memory_order_acquire);
    if (g_exitRequested.load() || generation != currentPowerGeneration() ||
        phase == PowerLifecycle::Suspending || phase == PowerLifecycle::Suspended || sgSleepRetryActive()) {
        sgCancelWakeGameRecoveryUnlocked("exit-new-sleep-or-retry"); return;
    }
    if(g_sgWakeRecovery.autoConfigEpoch&&!sleepRecoveryAutoAllows(generation,g_sgWakeRecovery.autoConfigEpoch)){
        sgCancelWakeGameRecoveryUnlocked("automatic-recovery-disabled");return;
    }
    const auto elapsed = GetTickCount64() - g_sgWakeRecovery.started;
    const auto attempt = g_sgWakeRecovery.nextAttempt;
    if (elapsed < WakeGameResume::attemptOffsetsMs[attempt]) return;
    const bool final = attempt + 1 == WakeGameResume::attemptCount ||
        elapsed >= WakeGameResume::attemptOffsetsMs[WakeGameResume::attemptCount - 1];
    const bool ready = phase == PowerLifecycle::Ready && g_inputReady.load(std::memory_order_acquire);
    if (!ready && !final) return;
    sgInitNt();
    unsigned confirmed = 0;
    for (auto& e : g_sgWakeRecovery.entries) {
        if (e.done) continue;
        // A user repair must never consume a manual pause introduced AFTER inspection.
        if(!g_sgWakeRecovery.requestId.empty()&&sgMarkerIsSuspended(SG_MANUAL_DIR,e.pid)){
            continue;
        }
        const auto marker = sgWakeRecoveryMarker(e);
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
                sgRecordFact("sleep-game-recovery-superseded", {
                    {"generation", generation}, {"pid", e.pid}, {"reason", "marker-changed"}
                });
                continue;
            }
            ULONGLONG currentCreated = 0;
            const bool sameIdentity = focusQueryProcessIdentity(e.pid, nullptr, &currentCreated) &&
                currentCreated == e.created;
            if (sameIdentity && WakeGameResume::queryThreads(e.pid).allRunning()) {
                // Manual resume already completed. Retire ONLY our old owner.
                sgWakeRecoveryClearTarget(e); e.done = true;
                continue;
            }
            // Lost file during recovery: retain/reconstruct the captured exact
            // lease, including old identity. A reused PID will still fail the
            // opened-handle check below; no replacement identity is invented.
            if (!sgWriteFileAtomic(marker, e.markerText)) {
                continue;
            }
            sgRecordFact("sleep-game-recovery-marker-rebuilt", {{"generation", generation}, {"pid", e.pid}});
        }
        HANDLE h = ready ? OpenProcess(PROCESS_SUSPEND_RESUME | PROCESS_QUERY_LIMITED_INFORMATION |
                                      SYNCHRONIZE, FALSE, e.pid) : nullptr;
        const DWORD openError = h || !ready ? ERROR_SUCCESS : GetLastError();
        const bool identityMatches = h && nativeProcessCreatedFromHandle(h) == e.created;
        const bool exited = h && WaitForSingleObject(h, 0) == WAIT_OBJECT_0;
        if ((h && (!identityMatches || exited)) || openError == ERROR_INVALID_PARAMETER) {
            if (h) CloseHandle(h);
            std::error_code ec; fspath::remove(marker, ec);
            sgWakeRecoveryClearTarget(e); e.done = true;
            sgRecordFact("sleep-game-recovery-stale", {{"generation", generation}, {"pid", e.pid}});
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
        const bool accepted = identityMatches && sameSession && fallbackStillAllowed &&
            fnNtResume && sgDrainProcessSuspendCount(h);
        e.acceptedOnce = e.acceptedOnce || accepted;
        const auto threads = identityMatches ? WakeGameResume::queryThreads(e.pid) : WakeGameResume::ThreadState{};
        if (h) CloseHandle(h);
        sgRecordFact("sleep-game-recovery-attempt", {
            {"generation", generation}, {"pid", e.pid}, {"processCreated", std::to_string(e.created)},
            {"owner", e.fallback ? "orphan-fallback" : e.dir == SG_MANUAL_DIR ? "manual" : "sleep"},
            {"sleepOwnerGeneration", e.sleepOwnerGeneration},
            {"attempt", attempt + 1}, {"elapsedMs", elapsed}, {"apiAccepted", accepted},
            {"ready", ready}, {"openError", openError}, {"sameSession", sameSession},
            {"fallbackStillAllowed", fallbackStillAllowed}, {"threadStateKnown", threads.known},
            {"threads", threads.total}, {"suspendedThreads", threads.suspended}, {"final", final}
        });
        // The current call plus the complete, stable two-snapshot thread set
        // is sufficient; waiting for final would hold an already-running lease.
        // Never borrow acceptedOnce or treat this as rendering/input health.
        if (accepted && threads.allRunning()) {
            ULONGLONG createdNow = 0;
            if (focusQueryProcessIdentity(e.pid, nullptr, &createdNow) && createdNow == e.created &&
                generation == currentPowerGeneration() && !g_exitRequested.load() && !sgSleepRetryActive() &&
                g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready &&
                g_inputReady.load(std::memory_order_acquire) &&
                (g_sgWakeRecovery.requestId.empty() || !sgMarkerIsSuspended(SG_MANUAL_DIR, e.pid)) &&
                sgReadFile(marker) == e.markerText) {
                std::error_code ec;
                const bool removed = fspath::remove(marker, ec);
                if (removed && !ec) {
                    sgWakeRecoveryClearTarget(e); e.done = true; ++confirmed;
                    sgRecordFact("sleep-game-recovery-confirmed", {
                        {"generation", generation}, {"pid", e.pid}, {"processCreated", std::to_string(e.created)},
                        {"attempt", attempt + 1}, {"elapsedMs", elapsed}, {"final", final},
                        {"basis", "current-attempt-zero-suspend-counts"}, {"markerRemoved", true}
                    });
                    if(g_sgWakeRecovery.focusOnComplete) focusSingleResumedGame({e.pid}, e.created, generation);
                }
            }
        }
    }
    // A late worker must never replay overdue attempts in a tight burst.
    ++g_sgWakeRecovery.nextAttempt;
    while (g_sgWakeRecovery.nextAttempt < WakeGameResume::attemptCount - 1 &&
           WakeGameResume::attemptOffsetsMs[g_sgWakeRecovery.nextAttempt] <= elapsed)
        ++g_sgWakeRecovery.nextAttempt;
    const auto pending = std::count_if(g_sgWakeRecovery.entries.begin(), g_sgWakeRecovery.entries.end(),
        [](const auto& e) { return !e.done; });
    sgRecordFact("sleep-game-recovery-progress", {
        {"generation", generation}, {"attempt", attempt + 1}, {"elapsedMs", elapsed},
        {"confirmedThisAttempt", confirmed}, {"pendingTargets", pending}, {"final", final}
    });
    if (final || pending == 0) {
        const bool manualPending = sgHasMarkerFiles(SG_MANUAL_DIR);
        if (!manualPending) {
            g_manualPausedPid.store(0, std::memory_order_release);
            const auto statePath = SG_DIR + L"\\quickapp_suspended.json";
            if (!g_sgWakeRecovery.manualState.empty() && sgReadFile(statePath) == g_sgWakeRecovery.manualState) {
                std::error_code ec; fspath::remove(statePath, ec);
            }
        }
        // A failed orphan recovery can retain a marker without a volatile
        // owner. Do not let the next sleep treat that lease as unowned.
        sgReconcileSleepGameFlagsAfterRecoveryCancel("wake-recovery-complete");
        sgRecordFact("sleep-game-recovery-complete", {
            {"generation", generation}, {"requestId",g_sgWakeRecovery.requestId}, {"elapsedMs", elapsed}, {"confirmedThisAttempt", confirmed},
            {"basis", pending == 0 ? "all-targets-done" : "attempt-window-exhausted"},
            {"attempt", attempt + 1}, {"pendingTargets", pending}, {"final", final},
            {"retainedTargets", pending}, {"manualPending", manualPending}, {"healthVerified", false}
        });
        ipc_emit("game.wake-recovery", {{"generation", generation}, {"manualPending", manualPending},
            {"requestId", g_sgWakeRecovery.requestId}, {"attempt", attempt + 1}, {"pendingTargets", pending},
            {"basis", pending == 0 ? "all-targets-done" : "attempt-window-exhausted"}, {"healthVerified", false}});
        g_sgWakeRecovery = {};
        g_sgWakeRecoveryPending.store(false, std::memory_order_release);
    }
}
