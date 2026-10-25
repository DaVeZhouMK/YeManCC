#pragma once
#include <windows.h>
#include <utility>
#include <type_traits>

// Caller serializes the sole owning HANDLE (production: g_inputHostMx).
// This retires a process HANDLE only. It proves no Host protocol completion,
// endpoint absence, physical return or permission to reuse an old session tuple.
namespace ymcc::inputHost::processRetirement {
struct WaitResult {
    DWORD state = WAIT_FAILED;
    DWORD error = ERROR_SUCCESS;
};
struct CloseResult {
    bool closed = false;
    DWORD error = ERROR_SUCCESS;
};
struct Receipt {
    bool hadHandle = false;
    DWORD waitState = WAIT_FAILED;
    DWORD waitError = ERROR_SUCCESS;
    bool exitConfirmed = false;
    bool closeAttempted = false;
    bool handleClosed = false;
    DWORD closeError = ERROR_SUCCESS;
    // Deliberately no bool conversion or whole-Host completion predicate.
};
inline WaitResult waitOnce(HANDLE process, DWORD timeoutMs) noexcept {
    const DWORD state = WaitForSingleObject(process, timeoutMs);
    return {state, state == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS};
}
inline CloseResult closeOnce(HANDLE process) noexcept {
    const bool closed = CloseHandle(process) != FALSE;
    return {closed, closed ? ERROR_SUCCESS : GetLastError()};
}
// Explicit ports allow deterministic OS failures without symbolic Host/SDKs.
// observe must wait on the supplied same process; close must leave it owned on
// failure. Neither port may rebind owner. No kill, loop, allocation or new budget.
template<class Observe, class Close>
Receipt retire(HANDLE& owner, Observe&& observe, Close&& close) {
    Receipt receipt;
    if (!owner) return receipt;
    receipt.hadHandle = true;
    const auto waited = std::forward<Observe>(observe)(owner);
    receipt.waitState = waited.state;
    receipt.waitError = waited.error;
    receipt.exitConfirmed = waited.state == WAIT_OBJECT_0;
    if (!receipt.exitConfirmed) return receipt;
    receipt.closeAttempted = true;
    const auto closed = std::forward<Close>(close)(owner);
    receipt.handleClosed = closed.closed;
    receipt.closeError = closed.error;
    if (receipt.handleClosed) owner = nullptr;
    return receipt;
}
template<class Observe, std::enable_if_t<std::is_invocable_v<Observe, HANDLE>, int> = 0>
Receipt retire(HANDLE& owner, Observe&& observe) {
    return retire(owner, std::forward<Observe>(observe), closeOnce);
}
inline Receipt retire(HANDLE& owner, DWORD timeoutMs = 0) {
    return retire(owner, [timeoutMs](HANDLE process) { return waitOnce(process, timeoutMs); });
}
} // namespace ymcc::inputHost::processRetirement
