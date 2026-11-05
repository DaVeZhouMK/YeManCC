#pragma once

struct SteamLightGamepadIntent {
    unsigned long long serial = 0;
    unsigned long long session = 0;
    ULONGLONG deadline = 0;
    long long expiresAt = 0;
    bool enabled = false;
    std::shared_ptr<const FocusLightIsolationTicket> proof;
};
static std::atomic<std::shared_ptr<const SteamLightGamepadIntent>> g_steamLightGamepadIntent;
static std::atomic<bool> g_steamLightGamepadQueued{false};
static std::atomic<unsigned long long> g_steamLightGamepadSettledSerial{0};
static std::atomic<ULONGLONG> g_steamLightGamepadReleaseUntil{0};
static unsigned long long g_steamLightGamepadSerial = 0;
static ULONGLONG g_steamLightGamepadNextRenewal = 0;
static constexpr ULONGLONG kSteamLightGamepadLeaseMs = 3000;
static constexpr ULONGLONG kSteamLightGamepadRenewalMs = 750;

static bool steamLightGamepadIntentCurrent(const std::shared_ptr<const SteamLightGamepadIntent>& intent) {
    if (g_steamLightGamepadIntent.load(std::memory_order_acquire) != intent) return false;
    if (!intent->enabled) return true;
    return !g_exitRequested.load(std::memory_order_acquire) && GetTickCount64() < intent->deadline &&
        g_focusLightIsolationTicket.load(std::memory_order_acquire) == intent->proof &&
        focusLightIsolationCurrent() && gamepadUiOutputIsolationEligible() &&
        !radialHasInputOwner() && !customSteamLibraryInputSuppressed();
}

static void steamLightGamepadQueue() {
    if (g_steamLightGamepadQueued.exchange(true, std::memory_order_acq_rel)) return;
    if (!poolSubmit([] {
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
                if (intent->enabled) {
                    DWORD pid = 0, account = 0;
                    sofRegDWORD(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"pid", &pid);
                    sofRegDWORD(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", &account);
                    const auto root = sofRegSZ(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
                    ULONGLONG created = 0;
                    std::wstring image;
                    if (pid && account && focusQueryProcessIdentity(pid, &image, &created) && created &&
                        ymcc::steamlive::normalizedPath(image) == ymcc::steamlive::normalizedPath(root) + L"\\steam.exe") {
                        binding = {{"pid", pid}, {"created", std::to_string(created)}, {"root", W2U(root)}};
                        boundAccount = account;
                    } else result = {{"ok", false}, {"enabled", false}, {"reason", "steam-instance-unavailable"}};
                }
                if (!binding.is_null() && (!intent->enabled || result.value("ok", false))) {
                    result = steamLiveRequest(boundAccount, {
                        {"operation", "input.gamepadLease"}, {"owner", owner}, {"serial", intent->serial},
                        {"enabled", intent->enabled}, {"expiresAt", intent->expiresAt}, {"lightSessionProcess", binding}
                    }, [intent] { return steamLightGamepadIntentCurrent(intent); });
                }
                const bool releasing = result.value("ok", false) && result.value("releasing", false);
                if (!intent->enabled && releasing && steamLightGamepadIntentCurrent(intent))
                    g_steamLightGamepadReleaseUntil.store(GetTickCount64() + kSteamLightGamepadLeaseMs, std::memory_order_release);
                if (!intent->enabled && result.value("ok", false) && !result.value("enabled", true) && !releasing) {
                    binding = nullptr; boundAccount = 0;
                    g_steamLightGamepadSettledSerial.store(intent->serial, std::memory_order_release);
                }
                const bool enabled = result.value("ok", false) && result.value("enabled", false) && steamLightGamepadIntentCurrent(intent);
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
            appendNativeLifecycleLog("summon-light-gamepad", {{"enabled", false}, {"reason", "lease-worker-failed"},
                {"scope", "steam-gamepad-navigation"}, {"deviceVisibilityChanged", false}});
        }
        g_steamLightGamepadQueued.store(false, std::memory_order_release);
        if (g_steamLightGamepadIntent.load(std::memory_order_acquire) != intent) steamLightGamepadQueue();
    })) g_steamLightGamepadQueued.store(false, std::memory_order_release);
}

static void steamLightGamepadUiChanged() {
    const bool enabled = !g_exitRequested.load(std::memory_order_acquire) &&
        focusLightIsolationCurrent() && gamepadUiOutputIsolationEligible() &&
        !radialHasInputOwner() && !customSteamLibraryInputSuppressed();
    const auto previous = g_steamLightGamepadIntent.load(std::memory_order_acquire);
    const auto now = GetTickCount64();
    if (!previous && !enabled) return;
    const auto proof = g_focusLightIsolationTicket.load(std::memory_order_acquire);
    const bool releasePending = previous && !previous->enabled && now < g_steamLightGamepadReleaseUntil.load(std::memory_order_acquire) &&
        previous->serial != g_steamLightGamepadSettledSerial.load(std::memory_order_acquire);
    if (previous && previous->enabled == enabled && previous->proof == proof &&
        ((!enabled && !releasePending) || now < g_steamLightGamepadNextRenewal)) return;
    auto intent = std::make_shared<SteamLightGamepadIntent>();
    intent->serial = ++g_steamLightGamepadSerial;
    intent->session = g_focusSessionSequence;
    intent->enabled = enabled;
    intent->proof = proof;
    intent->deadline = now + kSteamLightGamepadLeaseMs;
    if (enabled) g_steamLightGamepadReleaseUntil.store(intent->deadline, std::memory_order_release);
    intent->expiresAt = static_cast<long long>(sgNowEpoch() * 1000.0) + kSteamLightGamepadLeaseMs;
    g_steamLightGamepadNextRenewal = now + kSteamLightGamepadRenewalMs;
    g_steamLightGamepadIntent.store(intent, std::memory_order_release);
    steamLightGamepadQueue();
}
