#pragma once

// Included after main.cpp's common focus helpers. A one-shot return session
// owns only a precise game identity and the bounded common return timer.
// The fixture compiles this same consumer with Win32 boundaries replaced.
struct LosslessReturnFocusState {
    FocusTargetSnapshot target;
    unsigned long long powerGeneration = 0;
    unsigned long long identityEpoch = 0;
    ULONGLONG started = 0, deadline = 0, nextAttempt = 0;
    unsigned attempts = 0;
    bool returning = false;
};
static LosslessReturnFocusState g_losslessReturnFocus; // UI thread only
static constexpr ULONGLONG kLosslessReturnBudgetMs = 10000;
static constexpr unsigned kLosslessReturnMaxAttempts = 3;
static constexpr UINT kLosslessReturnTickMs = 100;

static void focusLosslessReturnCancel() {
    g_losslessReturnArmed.store(false, std::memory_order_release);
    if (g_losslessReturnFocus.returning) {
        if (g_hwnd) KillTimer(g_hwnd, RETURN_GAME_FOCUS_TIMER_ID);
        // A queued WM_TIMER must not fall back into the ordinary return path.
        g_focusSession.returning = false;
        g_focusSession.returnStarted = g_focusSession.returnDeadline = 0;
    }
    g_losslessReturnFocus = {};
}

// Called under the existing game/remembered locks, including from workers.
// Posting only invalidates on the UI thread; it never starts a timer/session.
static void focusLosslessReturnInvalidate() {
    g_losslessReturnIdentityEpoch.fetch_add(1, std::memory_order_acq_rel);
    if (g_losslessReturnArmed.load(std::memory_order_acquire) && g_hwnd)
        PostMessageW(g_hwnd, WM_TIMER, RETURN_GAME_FOCUS_TIMER_ID, 0);
}

static bool focusLosslessSameIdentity(const FocusTargetSnapshot& a,
                                     const FocusTargetSnapshot& b) {
    return a.valid && b.valid && a.pid && a.pid == b.pid &&
        a.processCreated && a.processCreated == b.processCreated &&
        !a.path.empty() && !b.path.empty() &&
        _wcsicmp(a.path.c_str(), b.path.c_str()) == 0;
}

static bool focusLosslessLiveIdentity(const FocusTargetSnapshot& target) {
    if (!target.valid || !target.pid || !target.processCreated || target.path.empty()) return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, target.pid);
    if (!process) return false;
    wchar_t image[32768]{};
    DWORD length = static_cast<DWORD>(std::size(image));
    FILETIME created{}, exited{}, kernel{}, user{};
    const bool matches = WaitForSingleObject(process, 0) == WAIT_TIMEOUT &&
        QueryFullProcessImageNameW(process, 0, image, &length) &&
        GetProcessTimes(process, &created, &exited, &kernel, &user) &&
        focusFileTimeTicks(created) == target.processCreated &&
        _wcsicmp(target.path.c_str(), image) == 0 &&
        WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return matches; // Access denied/unknown identity always fails closed here.
}

static bool focusLosslessContextMatches() {
    return g_losslessReturnFocus.target.valid &&
        !g_exitRequested.load(std::memory_order_acquire) &&
        g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready &&
        currentPowerGeneration() == g_losslessReturnFocus.powerGeneration &&
        g_losslessReturnIdentityEpoch.load(std::memory_order_acquire) == g_losslessReturnFocus.identityEpoch &&
        !focusGameControlBlocked() && !focusPidIsManuallyPaused(g_losslessReturnFocus.target.pid);
}

enum class LosslessReturnForeground { Game, Transient, OtherApplication };
static LosslessReturnForeground focusLosslessForeground(const FocusTargetSnapshot& target) {
    HWND foreground = GetForegroundWindow();
    if (!foreground) return LosslessReturnForeground::Transient;
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (pid == target.pid) return LosslessReturnForeground::Game;
    if (pid == GetCurrentProcessId()) return LosslessReturnForeground::Transient;
    const auto cls = focusWindowClass(foreground);
    if (_wcsicmp(cls.c_str(), L"Progman") == 0 || _wcsicmp(cls.c_str(), L"WorkerW") == 0 ||
        _wcsicmp(cls.c_str(), L"Shell_TrayWnd") == 0)
        return LosslessReturnForeground::Transient;
    // Only LS is allowed to steal focus during this session. Other overlays
    // and normal applications terminate it, respecting an explicit user switch.
    return focusWindowProcessName(foreground) == L"losslessscaling"
        ? LosslessReturnForeground::Transient : LosslessReturnForeground::OtherApplication;
}

// True means the message belongs to this consumer, including a posted identity
// invalidation while merely armed. It cannot fall through to the LS recall.
static bool focusLosslessReturnTimerTick(ULONGLONG now) {
    if (!g_losslessReturnFocus.target.valid) return false;
    if (!focusLosslessContextMatches() || !focusLosslessLiveIdentity(g_losslessReturnFocus.target)) {
        if (g_losslessReturnFocus.returning) focusClearSession();
        else focusLosslessReturnCancel();
        return true;
    }
    if (!g_losslessReturnFocus.returning) return true;
    if (now < g_losslessReturnFocus.started || now >= g_losslessReturnFocus.deadline) {
        focusClearSession();
        return true;
    }
    auto& state = g_losslessReturnFocus;
    const auto foreground = focusLosslessForeground(state.target);
    if (foreground == LosslessReturnForeground::OtherApplication) {
        focusClearSession();
        return true;
    }
    if (foreground == LosslessReturnForeground::Game || state.attempts >= kLosslessReturnMaxAttempts ||
        now < state.nextAttempt) return true;

    // Reacquire an HWND only within this exact live PID/birth/path. Neither
    // focusResolveTarget nor focusRememberedGameTarget is safe here: both rebind.
    if (!focusIsRestorableExactWindow(state.target.hwnd, state.target.pid))
        state.target.hwnd = focusFindWindowForPid(state.target.pid, &state.target);
    if (!focusIsRestorableExactWindow(state.target.hwnd, state.target.pid)) return true;
    if (!focusLosslessContextMatches() || !focusLosslessLiveIdentity(state.target)) {
        focusClearSession();
        return true;
    }
    const auto finalForeground = focusLosslessForeground(state.target);
    if (finalForeground == LosslessReturnForeground::OtherApplication) {
        focusClearSession();
        return true;
    }
    if (finalForeground == LosslessReturnForeground::Game) return true;
    const ULONGLONG requestNow = GetTickCount64();
    if (requestNow < state.started || requestNow >= state.deadline ||
        !focusLosslessContextMatches() ||
        !focusIsRestorableExactWindow(state.target.hwnd, state.target.pid)) {
        focusClearSession();
        return true;
    }
    // Count actual requests, including denied SetForegroundWindow. A success
    // retains unused budget until 10s so a later LS steal can be corrected.
    ++state.attempts;
    state.nextAttempt = (std::max)(state.started + (state.attempts == 1 ? 1000ULL : 3000ULL), requestNow + 1000ULL);
    ymcc::requestWindowForegroundAsync(state.target.hwnd);
    return true;
}

static bool focusLosslessReturnBegin(ULONGLONG now) {
    if (!g_losslessReturnFocus.target.valid) return false;
    auto& state = g_losslessReturnFocus;
    if (!state.returning) {
        state.returning = true;
        state.started = now;
        state.deadline = now + kLosslessReturnBudgetMs;
        state.nextAttempt = now;
    }
    g_focusSession.target = state.target; // Override a captured LS HWND.
    g_focusSession.returning = true;
    g_focusSession.returnStarted = state.started;
    g_focusSession.returnDeadline = state.deadline;
    focusReleaseOwnedTopmost();
    hideWindowAnimated(g_hwnd);
    focusLosslessReturnTimerTick(now);
    if (g_losslessReturnFocus.returning &&
        !SetTimer(g_hwnd, RETURN_GAME_FOCUS_TIMER_ID, kLosslessReturnTickMs, nullptr))
        focusClearSession();
    return true;
}

// Decimal strings preserve the full FILETIME through JavaScript. Integer JSON
// values are accepted too; floats, signs and overflow are never truncated.
static bool focusLosslessReadUnsigned(const json& value, ULONGLONG& out) {
    out = 0;
    if (value.is_number_unsigned()) out = value.get<ULONGLONG>();
    else if (value.is_number_integer()) {
        const auto number = value.get<long long>();
        if (number <= 0) return false;
        out = static_cast<ULONGLONG>(number);
    } else if (value.is_string()) {
        const auto text = value.get<std::string>();
        if (text.empty()) return false;
        for (const char c : text) {
            if (c < '0' || c > '9' || out > ((std::numeric_limits<ULONGLONG>::max)() - (c - '0')) / 10)
                return false;
            out = out * 10 + (c - '0');
        }
    } else return false;
    return out != 0;
}

// game.detect runs on a worker. Publish its window snapshot only while the
// same selection is still authoritative, so an older detection cannot undo a
// concurrent game switch. The UI registration needs only the short focus lock.
static void focusLosslessRememberDetectedGame(DWORD pid, ULONGLONG processCreated,
    const std::wstring& path, const FocusTargetSnapshot& remembered) {
    std::lock_guard<std::mutex> valveLock(g_gameValveMx);
    std::lock_guard<std::mutex> focusLock(g_rememberedGameTargetMx);
    const auto& game = g_gameValveTarget.game;
    if (!remembered.valid || !pid || !processCreated || path.empty() ||
        remembered.pid != pid || remembered.processCreated != processCreated ||
        _wcsicmp(remembered.path.c_str(), path.c_str()) != 0 ||
        game.pid != pid || game.processCreated != processCreated ||
        _wcsicmp(game.path.c_str(), path.c_str()) != 0) return;
    g_rememberedGameTarget = remembered;
    g_rememberedGameDeadline = GetTickCount64() + 6ULL * 60ULL * 60ULL * 1000ULL;
}

static json focusLosslessReturnTarget(const json& args) {
    FocusTargetSnapshot requested;
    ULONGLONG pid = 0;
    if (!args.is_object() || !args.contains("pid") || !args.contains("processCreated") ||
        !args.contains("path") || !args["path"].is_string() ||
        (args.contains("enabled") && !args["enabled"].is_boolean()) ||
        !focusLosslessReadUnsigned(args["pid"], pid) || pid > (std::numeric_limits<DWORD>::max)() ||
        !focusLosslessReadUnsigned(args["processCreated"], requested.processCreated))
        return {{"ok", false}, {"reason", "invalid-game-identity"}};
    requested.pid = static_cast<DWORD>(pid);
    requested.path = U2W(args["path"].get<std::string>());
    requested.valid = !requested.path.empty() && requested.path.find(L'\0') == std::wstring::npos;
    const bool enabled = args.value("enabled", true);
    if (!enabled) {
        if (!g_losslessReturnFocus.target.valid) return {{"ok", true}, {"enabled", false}};
        // An old game's OFF cannot clear another game's pending/active session.
        if (!focusLosslessSameIdentity(requested, g_losslessReturnFocus.target))
            return {{"ok", false}, {"reason", "return-target-mismatch"}};
        if (g_losslessReturnFocus.returning) focusClearSession();
        else focusLosslessReturnCancel();
        return {{"ok", true}, {"enabled", false}};
    }
    const auto generation = currentPowerGeneration();
    if (g_exitRequested.load(std::memory_order_acquire) ||
        g_powerLifecycle.load(std::memory_order_acquire) != PowerLifecycle::Ready ||
        focusGameControlBlocked() || focusPidIsManuallyPaused(requested.pid) ||
        !focusLosslessLiveIdentity(requested))
        return {{"ok", false}, {"reason", "game-identity-unavailable"}};
    // The worker publishes only the current selection. Never wait on its game
    // enumeration mutex from the UI thread, and never run detection here.
    std::lock_guard<std::mutex> focusLock(g_rememberedGameTargetMx);
    if (!focusLosslessSameIdentity(requested, g_rememberedGameTarget) ||
        GetTickCount64() > g_rememberedGameDeadline ||
        generation != currentPowerGeneration() ||
        g_powerLifecycle.load(std::memory_order_acquire) != PowerLifecycle::Ready ||
        g_exitRequested.load(std::memory_order_acquire))
        return {{"ok", false}, {"reason", "game-not-currently-identified"}};
    if (g_focusSession.returning) focusClearSession();
    else focusLosslessReturnCancel();
    g_losslessReturnFocus.target = g_rememberedGameTarget;
    g_losslessReturnFocus.powerGeneration = generation;
    g_losslessReturnFocus.identityEpoch = g_losslessReturnIdentityEpoch.load(std::memory_order_acquire);
    g_losslessReturnArmed.store(true, std::memory_order_release);
    return {{"ok", true}, {"enabled", true}, {"pid", requested.pid},
        {"processCreated", std::to_string(requested.processCreated)},
        {"timeoutMs", kLosslessReturnBudgetMs}, {"maxAttempts", kLosslessReturnMaxAttempts}};
}
