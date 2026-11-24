#pragma once

struct SteamLightGamepadLeaseState {
    std::shared_ptr<const FocusLightIsolationTicket> proof;
    unsigned long long session = 0;
    ULONGLONG initialDeadline = 0;
    bool nativeOnly = false;
    std::atomic<ULONGLONG> acknowledgedDeadline{0};
    std::atomic<bool> fallback{false};
};
static std::atomic<std::shared_ptr<SteamLightGamepadLeaseState>> g_steamLightGamepadLeaseState;

struct SteamLightGamepadIntent {
    unsigned long long serial = 0;
    unsigned long long session = 0;
    ULONGLONG deadline = 0;
    long long expiresAt = 0;
    bool enabled = false;
    std::shared_ptr<const FocusLightIsolationTicket> proof;
    std::shared_ptr<SteamLightGamepadLeaseState> lease;
};
static std::atomic<std::shared_ptr<const SteamLightGamepadIntent>> g_steamLightGamepadIntent;
static std::atomic<bool> g_steamLightGamepadQueued{false};
static std::atomic<unsigned long long> g_steamLightGamepadSettledSerial{0};
static std::atomic<ULONGLONG> g_steamLightGamepadReleaseUntil{0};
static unsigned long long g_steamLightGamepadSerial = 0;
static ULONGLONG g_steamLightGamepadNextRenewal = 0;
static constexpr ULONGLONG kSteamLightGamepadLeaseMs = 3000;
static constexpr ULONGLONG kSteamLightGamepadRenewalMs = 750;

static void steamLightGamepadRequireFallback(const std::shared_ptr<SteamLightGamepadLeaseState>& lease, const char* reason) {
    if (!lease || g_steamLightGamepadLeaseState.load(std::memory_order_acquire) != lease) return;
    const auto acknowledged = lease->acknowledgedDeadline.load(std::memory_order_acquire);
    if (inputHostConfiguredPersona() == "disabled" &&
        GetTickCount64() < (std::max)(acknowledged, lease->initialDeadline)) return;
    if (lease->fallback.exchange(true, std::memory_order_acq_rel)) return;
    appendNativeLifecycleLog("summon-light-fallback", {{"session", lease->session}, {"reason", reason},
        {"mode", "original-gamepad"}, {"virtualPriorityPreserved", true}, {"attemptLimitMs", kSteamLightGamepadLeaseMs},
        {"gameOpen", g_focusIsolationGameOpen.load(std::memory_order_acquire)}, {"gameInputSuppressionProven", false}});
    if (g_hwnd) PostMessageW(g_hwnd, WM_LIGHT_ISOLATION_RECHECK, 0, 0);
}

static bool steamLightGamepadFallbackRequired() {
    const auto lease = g_steamLightGamepadLeaseState.load(std::memory_order_acquire);
    if (!lease) return false;
    if (lease->fallback.load(std::memory_order_acquire)) return true;
    if (lease->proof->power != currentPowerGeneration()) return false;
    const auto acknowledged = lease->acknowledgedDeadline.load(std::memory_order_acquire);
    const auto deadline = inputHostConfiguredPersona() == "disabled" ? (std::max)(acknowledged, lease->initialDeadline) : acknowledged ? acknowledged : lease->initialDeadline;
    if (GetTickCount64() >= deadline)
        steamLightGamepadRequireFallback(lease, acknowledged ? "steam-lease-expired" : "steam-lease-unconfirmed");
    return lease->fallback.load(std::memory_order_acquire);
}

static bool steamLightGamepadAttemptActive() {
    const auto lease = g_steamLightGamepadLeaseState.load(std::memory_order_acquire);
    return lease && lease->proof->power == currentPowerGeneration() &&
        g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) == lease->proof->intent &&
        !g_focusIsolationGameOpen.load(std::memory_order_acquire) && inputHostConfiguredPersona() == "disabled" &&
        g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready &&
        !g_exitRequested.load(std::memory_order_acquire) && gamepadUiOutputIsolationEligible() &&
        focusLightNoGameQualified(GetTickCount64(), &lease->proof->sharedPower) &&
        lease->proof->intent == g_inputHostVirtualIntentRevision.load(std::memory_order_acquire) &&
        g_steamLightGamepadLeaseState.load(std::memory_order_acquire) == lease;
}

static void steamLightGamepadEndIsolation() {
    g_steamLightGamepadLeaseState.store({}, std::memory_order_release);
}

static bool steamLightGamepadIntentCurrent(const std::shared_ptr<const SteamLightGamepadIntent>& intent) {
    if (!intent || g_steamLightGamepadIntent.load(std::memory_order_acquire) != intent) return false;
    if (!intent->enabled) return true;
    return !g_exitRequested.load(std::memory_order_acquire) &&
        g_controllerFrontendIsolationEnabled.load(std::memory_order_acquire) && GetTickCount64() < intent->deadline &&
        g_focusLightIsolationTicket.load(std::memory_order_acquire) == intent->proof &&
        intent->lease && intent->lease->nativeOnly == (inputHostConfiguredPersona() == "disabled") &&
        focusLightIsolationCurrent() && gamepadUiOutputIsolationEligible() &&
        !radialHasInputOwner() && !customSteamLibraryInputSuppressed() &&
        g_focusLightIsolationTicket.load(std::memory_order_acquire) == intent->proof &&
        g_steamLightGamepadIntent.load(std::memory_order_acquire) == intent;
}

static void steamLightGamepadQueue() {
    if (g_steamLightGamepadQueued.exchange(true, std::memory_order_acq_rel)) return;
    struct QueueResponsibility {
        std::atomic<bool> pending{true};
        void retire() noexcept {
            if (pending.exchange(false, std::memory_order_acq_rel))
                g_steamLightGamepadQueued.store(false, std::memory_order_release);
        }
        ~QueueResponsibility() { retire(); }
    };
    std::shared_ptr<QueueResponsibility> responsibility;
    bool submitted = false;
    try {
        responsibility = std::make_shared<QueueResponsibility>();
        submitted = poolSubmit([responsibility] {
        const auto intent = g_steamLightGamepadIntent.load(std::memory_order_acquire);
        static std::string owner;
        static json binding;
        static DWORD boundAccount = 0;
        static bool lastEnabled = false;
        static bool lastReleasing = false;
        static std::string lastReason;
        try {
            if (owner.empty()) owner = ymcc::deckymirror::randomHex(16);
            if (intent && steamLightGamepadIntentCurrent(intent)) {
                json result = {{"ok", true}, {"enabled", false}, {"reason", "no-steam-lease"}};
                const auto session = g_steamSettingsObserver.sessionSnapshot();
                if (intent->enabled) {
                    const json candidate = {{"pid", session.pid}, {"created", std::to_string(session.created)}, {"root", W2U(session.root)}};
                    if (session.pid && session.account && session.created &&
                        sleepSteamLiveProcessMatches(candidate, session.root)) {
                        binding = candidate;
                        boundAccount = session.account;
                    } else result = {{"ok", false}, {"enabled", false}, {"reason", "steam-instance-unavailable"}};
                }
                if (!binding.is_null() && (!intent->enabled || result.value("ok", false))) {
                    result = steamLiveRequest(boundAccount, {
                        {"operation", "input.gamepadLease"}, {"owner", owner}, {"serial", intent->serial},
                        {"enabled", intent->enabled}, {"expiresAt", intent->expiresAt}, {"lightSessionProcess", binding}
                    }, [intent, session] { return steamLightGamepadIntentCurrent(intent) &&
                        (!intent->enabled || g_steamSettingsObserver.sessionSnapshot() == session); });
                }
                const bool releasing = result.value("ok", false) && result.value("releasing", false);
                if (!intent->enabled && result.value("ok", false) && !result.value("enabled", true) && !releasing &&
                    steamLightGamepadIntentCurrent(intent)) {
                    binding = nullptr; boundAccount = 0;
                    g_steamLightGamepadSettledSerial.store(intent->serial, std::memory_order_release);
                }
                const bool current = steamLightGamepadIntentCurrent(intent) &&
                    (!intent->enabled || g_steamSettingsObserver.sessionSnapshot() == session);
                bool enabled = result.value("ok", false) && result.value("enabled", false) && current;
                if (intent->enabled && steamLightGamepadIntentCurrent(intent)) {
                    const auto remaining = result.value("remainingMs", 0);
                    if (enabled && !steamLightGamepadFallbackRequired() && result.value("serial", 0ULL) == intent->serial && remaining > 0 &&
                        remaining <= static_cast<int>(kSteamLightGamepadLeaseMs) &&
                        result.value("scope", std::string()) == "steam-gamepad-navigation") {
                        intent->lease->acknowledgedDeadline.store(
                            (std::min)(intent->deadline, GetTickCount64() + static_cast<ULONGLONG>(remaining)), std::memory_order_release);
                    } else steamLightGamepadRequireFallback(intent->lease, "steam-lease-not-established");
                    enabled = enabled && !intent->lease->fallback.load(std::memory_order_acquire);
                }
                const auto reason = result.value("reason", enabled ? std::string("navigation-suppressed") : std::string("navigation-released"));
                if (enabled != lastEnabled || releasing != lastReleasing || reason != lastReason) {
                    appendNativeLifecycleLog("summon-light-gamepad", {
                        {"session", intent->session}, {"serial", intent->serial}, {"requested", intent->enabled},
                        {"enabled", enabled}, {"reason", reason}, {"scope", "steam-gamepad-navigation"},
                        {"acknowledged", result.value("ok", false)}, {"current", steamLightGamepadIntentCurrent(intent)},
                        {"releasing", releasing}, {"pendingReleaseCount", result.value("pendingReleaseCount", 0)},
                        {"result", result.value("ok", false) ? "acknowledged" : "unconfirmed"},
                        {"remainingMs", result.value("remainingMs", 0)}, {"deviceVisibilityChanged", false},
                        {"leaseMs", kSteamLightGamepadLeaseMs}, {"renewalMs", kSteamLightGamepadRenewalMs}
                    });
                    lastEnabled = enabled; lastReleasing = releasing; lastReason = reason;
                }
            }
        } catch (...) {
            if (intent && intent->enabled && steamLightGamepadIntentCurrent(intent))
                steamLightGamepadRequireFallback(intent->lease, "steam-lease-worker-failed");
            appendNativeLifecycleLog("summon-light-gamepad", {{"enabled", false}, {"reason", "lease-worker-failed"},
                {"scope", "steam-gamepad-navigation"}, {"deviceVisibilityChanged", false}});
        }
        responsibility->retire();
        if (g_steamLightGamepadIntent.load(std::memory_order_acquire) != intent) steamLightGamepadQueue();
        });
    } catch (...) {}
    if (!submitted) {
        if (responsibility) responsibility->retire();
        else g_steamLightGamepadQueued.store(false, std::memory_order_release);
        const auto intent = g_steamLightGamepadIntent.load(std::memory_order_acquire);
        if (intent && intent->enabled && steamLightGamepadIntentCurrent(intent))
            steamLightGamepadRequireFallback(intent->lease, "steam-lease-queue-unavailable");
    }
}

static void steamLightGamepadUiChanged() {
    const auto checkedProof = g_focusLightIsolationTicket.load(std::memory_order_acquire);
    if (!checkedProof) {
        const auto previous = g_steamLightGamepadIntent.load(std::memory_order_acquire);
        const bool settled = previous && !previous->enabled && !previous->proof &&
            previous->session == g_focusSessionSequence &&
            previous->serial == g_steamLightGamepadSettledSerial.load(std::memory_order_acquire);
        // A fresh proof is required to acquire. Only skip the UI probe when
        // there is no lease state to maintain and no release left to settle.
        if ((!previous || settled) && !g_steamLightGamepadLeaseState.load(std::memory_order_acquire) &&
            steamLightGamepadUiProbeCanIdle()) return;
    }
    const auto now = GetTickCount64();
    const bool proofQualified = checkedProof && focusLightIsolationCurrent();
    const bool demand = !g_exitRequested.load(std::memory_order_acquire) &&
        g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready &&
        gamepadUiOutputIsolationEligible() && !radialHasInputOwner() && !customSteamLibraryInputSuppressed();
    if (!demand) steamLightGamepadEndIsolation();
    const auto previous = g_steamLightGamepadIntent.load(std::memory_order_acquire);
    const auto proof = g_focusLightIsolationTicket.load(std::memory_order_acquire);
    bool enabled = demand && proofQualified && proof == checkedProof;
    auto lease = g_steamLightGamepadLeaseState.load(std::memory_order_acquire);
    if (enabled) {
        const bool nativeOnly = inputHostConfiguredPersona() == "disabled";
        if (!lease || lease->nativeOnly != nativeOnly || (lease->proof != checkedProof &&
            (!lease->fallback.load(std::memory_order_acquire) || lease->proof->power != checkedProof->power))) {
            const auto deadline = lease && lease->nativeOnly == nativeOnly && lease->proof->power == checkedProof->power ?
                (std::max)(lease->initialDeadline, lease->acknowledgedDeadline.load(std::memory_order_acquire)) : now + kSteamLightGamepadLeaseMs;
            lease = std::make_shared<SteamLightGamepadLeaseState>();
            lease->proof = checkedProof;
            lease->session = g_focusSessionSequence;
            lease->initialDeadline = deadline;
            lease->nativeOnly = nativeOnly;
            g_steamLightGamepadLeaseState.store(lease, std::memory_order_release);
        }
        enabled = !steamLightGamepadFallbackRequired();
    }
    if (!previous && !enabled) return;
    const bool releasePending = previous && !previous->enabled && now < g_steamLightGamepadReleaseUntil.load(std::memory_order_acquire) &&
        previous->serial != g_steamLightGamepadSettledSerial.load(std::memory_order_acquire);
    if (previous && previous->session == g_focusSessionSequence && previous->enabled == enabled && previous->proof == proof &&
        ((!enabled && !releasePending) || now < g_steamLightGamepadNextRenewal ||
         (enabled && previous->lease == lease && now < previous->deadline && g_steamLightGamepadQueued.load(std::memory_order_acquire)))) return;
    auto intent = std::make_shared<SteamLightGamepadIntent>();
    intent->serial = ++g_steamLightGamepadSerial;
    intent->session = g_focusSessionSequence;
    intent->enabled = enabled;
    intent->proof = enabled ? checkedProof : proof;
    intent->lease = lease;
    intent->deadline = enabled ? now + kSteamLightGamepadLeaseMs : g_steamLightGamepadReleaseUntil.load(std::memory_order_acquire);
    if (enabled) g_steamLightGamepadReleaseUntil.store(intent->deadline, std::memory_order_release);
    intent->expiresAt = static_cast<long long>(sgNowEpoch() * 1000.0) +
        static_cast<long long>(intent->deadline > now ? intent->deadline - now : 0);
    g_steamLightGamepadNextRenewal = now + kSteamLightGamepadRenewalMs;
    g_steamLightGamepadIntent.store(intent, std::memory_order_release);
    steamLightGamepadQueue();
}
