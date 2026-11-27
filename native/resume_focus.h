#pragma once
#include <windows.h>
#include <cstdint>
#include <optional>
#include "sleep_power_observation.h"

namespace ymcc {
// Window focus is optional after thaw. It owns neither a process resume nor a
// gameplay authorization; the sleep/manual owners remain in main.cpp.
struct ResumeFocusRequest {
    std::uint64_t ticket = 0;
    DWORD pid = 0;
    ULONGLONG processCreated = 0;
    sleeppower::SharedPowerV1 power;
    ULONGLONG notBefore = 0;
    ULONGLONG deadline = 0;
    std::optional<std::uint64_t> wakeSleepBoundary;
};
enum class ResumeFocusGate { Ready, Deferred, Invalid, Superseded, StalePower, Exiting, Expired };
// Only the confirmed thaw handoff may follow a later notice, and only while
// the caller still holds the same observed sleep boundary before the first probe.
inline bool sameResumeFocusSleep(const sleeppower::SharedPowerV1& origin,
                                 const sleeppower::SharedPowerV1& now) noexcept {
    return sleeppower::powerObservationKnown(origin) && sleeppower::powerObservationKnown(now) &&
        !((origin.flags | now.flags) & sleeppower::kNotificationPending) &&
        origin.generation == now.generation && origin.suspendSerial == now.suspendSerial &&
        origin.observedNotificationSequence <= now.observedNotificationSequence &&
        origin.publicationRevision <= now.publicationRevision;
}
inline bool sameResumeFocusWake(const sleeppower::SharedPowerV1& origin,
                                const sleeppower::SharedPowerV1& now) noexcept {
    return origin.observedNotificationSequence == now.observedNotificationSequence && sameResumeFocusSleep(origin, now);
}
inline ResumeFocusGate resumeFocusGate(const ResumeFocusRequest& r, ULONGLONG now,
    std::uint64_t latestTicket, const sleeppower::SharedPowerV1& power, bool exiting, bool inputReady,
    bool followWake = false) {
    if (!r.ticket || !r.pid || !r.processCreated || !sleeppower::powerObservationKnown(r.power))
        return ResumeFocusGate::Invalid;
    if (exiting) return ResumeFocusGate::Exiting;
    if (r.ticket != latestTicket) return ResumeFocusGate::Superseded;
    if (!(followWake ? sameResumeFocusSleep(r.power, power) : sameResumeFocusWake(r.power, power)))
        return ResumeFocusGate::StalePower;
    if (now >= r.deadline) return ResumeFocusGate::Expired;
    if (now < r.notBefore || !sleeppower::powerAdmitted(power) || !inputReady) return ResumeFocusGate::Deferred;
    return ResumeFocusGate::Ready;
}
inline const char* resumeFocusGateName(ResumeFocusGate gate) {
    switch (gate) {
    case ResumeFocusGate::Ready: return "ready";
    case ResumeFocusGate::Deferred: return "waiting-power-input-settle";
    case ResumeFocusGate::Invalid: return "invalid-identity";
    case ResumeFocusGate::Superseded: return "superseded";
    case ResumeFocusGate::StalePower: return "stale-power-generation";
    case ResumeFocusGate::Exiting: return "exiting";
    default: return "deadline-expired";
    }
}
inline constexpr UINT kResumeFocusProbeMs = 150;
inline bool resumeWindowResponds(HWND target) {
    SetLastError(ERROR_SUCCESS); // zero failure may be generic, not proof of a hang
    const DWORD thread = GetWindowThreadProcessId(target, nullptr);
    // SendMessageTimeout cannot enforce a timeout against the caller's own
    // input queue. Production never attaches this worker or the UI to games.
    if (!thread || thread == GetCurrentThreadId()) return false;
    DWORD_PTR reply = 0;
    return SendMessageTimeoutW(target, WM_NULL, 0, 0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT,
        kResumeFocusProbeMs, &reply) != 0;
}
template<class Current> inline bool requestWindowForegroundAsync(HWND target, const Current& current) {
    // No shared input queue, synchronous restore, or cross-thread SetFocus /
    // SetActiveWindow. Foreign z-order/show requests are explicitly async.
    if (!IsWindowVisible(target)) {
        if (!current()) return false;
        ShowWindowAsync(target, SW_SHOW);
    }
    if (IsIconic(target)) {
        if (!current()) return false;
        ShowWindowAsync(target, SW_RESTORE);
    }
    if (!current()) return false;
    SetWindowPos(target, HWND_TOP, 0, 0, 0, 0,
        SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return current() && SetForegroundWindow(target) != FALSE;
}
inline bool requestWindowForegroundAsync(HWND target) {
    return requestWindowForegroundAsync(target, [] { return true; });
}
} // namespace ymcc
