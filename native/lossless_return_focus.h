#pragma once

#include "sleep_power_observation.h"

// Included after main.cpp's common focus helpers. A one-shot return session
// owns only a precise game identity and the bounded common return timer.
// The fixture compiles this same consumer with Win32 boundaries replaced.
struct LosslessReturnFocusState {
    FocusTargetSnapshot target;
    ymcc::sleeppower::SharedPowerV1 power;
    unsigned long long identityEpoch = 0;
    ULONGLONG started = 0, deadline = 0, nextAttempt = 0;
    unsigned attempts = 0;
    bool returning = false;
};
static LosslessReturnFocusState g_losslessReturnFocus; // UI thread only
static std::uint64_t g_losslessReturnSequence = 0;
static constexpr ULONGLONG kLosslessReturnBudgetMs = 10000;
static constexpr unsigned kLosslessReturnMaxAttempts = 3;
static constexpr UINT kLosslessReturnTickMs = 100;

static void focusLosslessReturnCancel() {
    ++g_losslessReturnSequence;
    g_losslessReturnArmed.store(false, std::memory_order_release);
    if (g_losslessReturnFocus.returning) {
        if (g_hwnd) KillTimer(g_hwnd, RETURN_GAME_FOCUS_TIMER_ID);
        // A queued WM_TIMER must not fall back into the ordinary return path.
        g_focusSession.returning = false;
        g_focusSession.returnStarted = g_focusSession.returnDeadline = 0;
    }
    g_losslessReturnFocus = {};
}

static void focusLosslessReturnYieldToTaskView() {
    if (!g_losslessReturnFocus.returning) return;
    focusLosslessReturnCancel();
    g_focusSession.target = {};
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
        g_losslessReturnIdentityEpoch.load(std::memory_order_acquire) == g_losslessReturnFocus.identityEpoch &&
        !focusGameControlBlocked() && !focusPidIsManuallyPaused(g_losslessReturnFocus.target.pid);
}

static bool focusLosslessPowerReady(const ymcc::sleeppower::SharedPowerV1& power) noexcept {
    return power.phase == 0 && !(power.flags & ymcc::sleeppower::kNotificationPending);
}

static bool focusLosslessPowerRetired(const ymcc::sleeppower::SharedPowerV1& power) noexcept {
    const auto& entered = g_losslessReturnFocus.power;
    return power.phase != 0 || power.generation > entered.generation || power.suspendSerial > entered.suspendSerial;
}

static void focusLosslessReturnRetire() {
    if (g_losslessReturnFocus.returning) focusClearSession();
    else focusLosslessReturnCancel();
}

static bool focusLosslessSharedPowerChanged(const ymcc::sleeppower::SharedPowerV1& power) {
    if (!g_losslessReturnFocus.target.valid || !ymcc::sleeppower::powerObservationKnown(power) ||
        !focusLosslessPowerRetired(power)) return true;
    if (!ymcc::sleeppower::powerObservationCurrent(&readSharedSystemPower, power)) return false;
    focusLosslessReturnRetire();
    return true;
}

static bool focusLosslessMayAct(ULONGLONG now) {
    const auto& state = g_losslessReturnFocus;
    if (!focusLosslessContextMatches() || (state.returning && (now < state.started || now >= state.deadline))) {
        focusLosslessReturnRetire();
        return false;
    }
    ymcc::sleeppower::SharedPowerV1 power;
    if (!ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower, power)) return false;
    if (focusLosslessPowerRetired(power)) {
        (void)focusLosslessSharedPowerChanged(power);
        return false;
    }
    return focusLosslessPowerReady(power) && power.generation == state.power.generation &&
        power.suspendSerial == state.power.suspendSerial;
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
    const auto session = g_losslessReturnSequence;
    if (!focusLosslessMayAct(now)) return true;
    const bool live = focusLosslessLiveIdentity(g_losslessReturnFocus.target);
    if (session != g_losslessReturnSequence) return true;
    if (!live) { focusLosslessReturnRetire(); return true; }
    if (!g_losslessReturnFocus.returning) return true;
    auto& state = g_losslessReturnFocus;
    const auto foreground = focusLosslessForeground(state.target);
    if (session != g_losslessReturnSequence) return true;
    if (foreground == LosslessReturnForeground::OtherApplication) {
        focusClearSession();
        return true;
    }
    if (foreground == LosslessReturnForeground::Game || state.attempts >= kLosslessReturnMaxAttempts ||
        now < state.nextAttempt) return true;

    // Reacquire an HWND only within this exact live PID/birth/path. Neither
    // focusResolveTarget nor focusRememberedGameTarget is safe here: both rebind.
    if (!focusIsRestorableExactWindow(state.target.hwnd, state.target.pid)) {
        const auto target = state.target;
        const auto hwnd = focusFindWindowForPid(target.pid, &target);
        if (session != g_losslessReturnSequence) return true;
        state.target.hwnd = hwnd;
    }
    if (!focusIsRestorableExactWindow(state.target.hwnd, state.target.pid)) return true;
    if (!focusLosslessMayAct(GetTickCount64())) return true;
    const bool stillLive = focusLosslessLiveIdentity(state.target);
    if (session != g_losslessReturnSequence) return true;
    if (!stillLive) {
        focusClearSession();
        return true;
    }
    const auto finalForeground = focusLosslessForeground(state.target);
    if (session != g_losslessReturnSequence) return true;
    if (finalForeground == LosslessReturnForeground::OtherApplication) {
        focusClearSession();
        return true;
    }
    if (finalForeground == LosslessReturnForeground::Game) return true;
    const ULONGLONG requestNow = GetTickCount64();
    if (!focusIsRestorableExactWindow(state.target.hwnd, state.target.pid)) {
        focusClearSession();
        return true;
    }
    if (!focusLosslessMayAct(requestNow)) return true;
    // Count actual requests, including denied SetForegroundWindow. A success
    // retains unused budget until 10s so a later LS steal can be corrected.
    ++state.attempts;
    state.nextAttempt = (std::max)(state.started + (state.attempts == 1 ? 1000ULL : 3000ULL), requestNow + 1000ULL);
    ymcc::requestWindowForegroundAsync(state.target.hwnd);
    return true;
}

static bool focusLosslessReturnBegin(ULONGLONG now) {
    if (!g_losslessReturnFocus.target.valid) return false;
    if (taskViewFocusHandoffPaused()) return true;
    if (!focusLosslessMayAct(now)) return true;
    const auto session = g_losslessReturnSequence;
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
    if (session != g_losslessReturnSequence) return true;
    hideWindowAnimated(g_hwnd);
    if (session != g_losslessReturnSequence) return true;
    focusLosslessReturnTimerTick(now);
    if (session == g_losslessReturnSequence && g_losslessReturnFocus.returning &&
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
    ymcc::sleeppower::SharedPowerV1 power;
    if (!ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower, power) || !focusLosslessPowerReady(power) ||
        focusGameControlBlocked() || focusPidIsManuallyPaused(requested.pid) ||
        !focusLosslessLiveIdentity(requested))
        return {{"ok", false}, {"reason", "game-identity-unavailable"}};
    // The worker publishes only the current selection. Never wait on its game
    // enumeration mutex from the UI thread, and never run detection here.
    FocusTargetSnapshot remembered;
    unsigned long long epoch;
    {
        std::lock_guard<std::mutex> focusLock(g_rememberedGameTargetMx);
        if (!focusLosslessSameIdentity(requested, g_rememberedGameTarget) || GetTickCount64() > g_rememberedGameDeadline)
            return {{"ok", false}, {"reason", "game-not-currently-identified"}};
        remembered = g_rememberedGameTarget;
        epoch = g_losslessReturnIdentityEpoch.load(std::memory_order_acquire);
    }
    if (!ymcc::sleeppower::powerObservationCurrent(&readSharedSystemPower, power) ||
        !focusIsolationSharedPowerChanged(power) ||
        g_exitRequested.load(std::memory_order_acquire))
        return {{"ok", false}, {"reason", "game-not-currently-identified"}};
    if (g_focusSession.returning) focusClearSession();
    else focusLosslessReturnCancel();
    std::lock_guard<std::mutex> focusLock(g_rememberedGameTargetMx);
    if (epoch != g_losslessReturnIdentityEpoch.load(std::memory_order_acquire) ||
        !focusLosslessSameIdentity(remembered, g_rememberedGameTarget) || GetTickCount64() > g_rememberedGameDeadline ||
        !ymcc::sleeppower::powerObservationCurrent(&readSharedSystemPower, power))
        return {{"ok", false}, {"reason", "game-not-currently-identified"}};
    g_losslessReturnFocus.target = std::move(remembered);
    g_losslessReturnFocus.power = power;
    g_losslessReturnFocus.identityEpoch = epoch;
    g_losslessReturnArmed.store(true, std::memory_order_release);
    return {{"ok", true}, {"enabled", true}, {"pid", requested.pid},
        {"processCreated", std::to_string(requested.processCreated)},
        {"timeoutMs", kLosslessReturnBudgetMs}, {"maxAttempts", kLosslessReturnMaxAttempts}};
}
