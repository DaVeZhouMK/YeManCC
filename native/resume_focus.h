#pragma once
#include <windows.h>
#include <cstdint>

namespace ymcc {
// Window focus is optional after thaw. It owns neither a process resume nor a
// gameplay authorization; the sleep/manual owners remain in main.cpp.
struct ResumeFocusRequest {
    std::uint64_t ticket = 0;
    DWORD pid = 0;
    ULONGLONG processCreated = 0;
    std::uint64_t powerGeneration = 0;
    ULONGLONG notBefore = 0;
    ULONGLONG deadline = 0;
};
enum class ResumeFocusGate { Ready, Deferred, Invalid, Superseded, StalePower, Exiting, Expired };
inline ResumeFocusGate resumeFocusGate(const ResumeFocusRequest& r, ULONGLONG now,
    std::uint64_t latestTicket, std::uint64_t generation, bool exiting,
    bool powerReady, bool inputReady) {
    if (!r.ticket || !r.pid || !r.processCreated || !r.powerGeneration)
        return ResumeFocusGate::Invalid;
    if (exiting) return ResumeFocusGate::Exiting;
    if (r.ticket != latestTicket) return ResumeFocusGate::Superseded;
    if (r.powerGeneration != generation) return ResumeFocusGate::StalePower;
    if (now >= r.deadline) return ResumeFocusGate::Expired;
    if (now < r.notBefore || !powerReady || !inputReady) return ResumeFocusGate::Deferred;
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
inline bool requestWindowForegroundAsync(HWND target) {
    // No shared input queue, synchronous restore, or cross-thread SetFocus /
    // SetActiveWindow. Foreign z-order/show requests are explicitly async.
    if (!IsWindowVisible(target)) ShowWindowAsync(target, SW_SHOW);
    if (IsIconic(target)) ShowWindowAsync(target, SW_RESTORE);
    SetWindowPos(target, HWND_TOP, 0, 0, 0, 0,
        SWP_ASYNCWINDOWPOS | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return SetForegroundWindow(target) != FALSE;
}
} // namespace ymcc
