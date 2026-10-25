#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <algorithm>

// A single explicit TaskView action. Only the dispatcher-owned, ready worker
// may receive the grant. The UI message carries a scalar token, never a pointer
// whose lifetime could outlive SendMessageTimeout. No input injection or retry.
namespace ymcc::taskView::foreground {
inline constexpr UINT message = WM_APP + 825;
inline constexpr DWORD maximumWaitMs = 250;
struct WindowsOps {
    ULONGLONG now() const noexcept { return GetTickCount64(); }
    DWORD threadId() const noexcept { return GetCurrentThreadId(); }
    DWORD processId() const noexcept { return GetCurrentProcessId(); }
    DWORD windowThread(HWND window, DWORD& pid) const noexcept { return GetWindowThreadProcessId(window, &pid); }
    HWND foregroundWindow() const noexcept { return GetForegroundWindow(); }
    DWORD error() const noexcept { return GetLastError(); }
    void setError(DWORD value) const noexcept { SetLastError(value); }
    bool allow(DWORD pid) const noexcept { return AllowSetForegroundWindow(pid) != FALSE; }
    bool send(HWND window, WPARAM token, DWORD wait, DWORD_PTR& result) const noexcept {
        return SendMessageTimeoutW(window, message, token, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, wait, &result) != 0;
    }
};
enum class DeliveryStage : DWORD { NotDelivered, InvalidToken, WrongOwnerThread, Expired, ForeignForeground,
    InvalidChild, Cancelled, ApiAccepted, ApiRejected, ApiRejectedNoError, TransportFailed, TransportNoError, InvalidReply };
inline const char* stageName(DeliveryStage stage) noexcept {
    switch (stage) {
    case DeliveryStage::NotDelivered: return "not-delivered";
    case DeliveryStage::TransportFailed: return "ui-message-failed-delivery-unknown";
    case DeliveryStage::TransportNoError: return "ui-message-failed-no-extended-error-delivery-unknown";
    case DeliveryStage::InvalidReply: return "invalid-ui-reply";
    case DeliveryStage::InvalidToken: return "invalid-token";
    case DeliveryStage::WrongOwnerThread: return "wrong-owner-thread";
    case DeliveryStage::Expired: return "expired-before-grant";
    case DeliveryStage::ForeignForeground: return "foreign-foreground";
    case DeliveryStage::InvalidChild: return "invalid-owned-worker";
    case DeliveryStage::Cancelled: return "cancelled-before-grant";
    case DeliveryStage::ApiAccepted: return "windows-api-accepted";
    case DeliveryStage::ApiRejected: return "windows-api-rejected";
    case DeliveryStage::ApiRejectedNoError: return "windows-api-rejected-no-extended-error";
    }
    return "unknown";
}
// Filled only on the delivery stack. It is never stored in the broker/message;
// a timed-out sender cannot leave a pointer to expired request storage here.
struct DeliveryReceipt {
    DeliveryStage stage = DeliveryStage::NotDelivered;
    DWORD error = ERROR_SUCCESS;
    bool deliveryKnown() const noexcept { return stage != DeliveryStage::TransportFailed && stage != DeliveryStage::TransportNoError && stage != DeliveryStage::InvalidReply; }
    bool apiAttempted() const noexcept { return stage == DeliveryStage::ApiAccepted || stage == DeliveryStage::ApiRejected || stage == DeliveryStage::ApiRejectedNoError; }
    bool extendedErrorKnown() const noexcept { return apiAttempted() || stage == DeliveryStage::TransportFailed || stage == DeliveryStage::TransportNoError; }
    DWORD extendedError() const noexcept { return stage == DeliveryStage::ApiRejectedNoError || stage == DeliveryStage::TransportNoError ? 0 : error; }
    bool apiAccepted() const noexcept { return stage == DeliveryStage::ApiAccepted; }
};
// YMCC's Windows shell is x64. Return a scalar receipt, never a stack pointer or
// shared mutable request buffer. No logging/I/O may delay the grant-to-execute gap.
static_assert(sizeof(DWORD_PTR) >= sizeof(std::uint64_t));
inline LRESULT encodeReply(const DeliveryReceipt& receipt) noexcept {
    return static_cast<LRESULT>((static_cast<std::uint64_t>(receipt.stage) << 32) | receipt.error);
}
inline bool decodeReply(DWORD_PTR reply, DeliveryReceipt& receipt) noexcept {
    const auto stage = static_cast<DeliveryStage>(static_cast<std::uint64_t>(reply) >> 32);
    const auto error = static_cast<DWORD>(reply);
    if (stage < DeliveryStage::InvalidToken || stage > DeliveryStage::ApiRejectedNoError ||
        (stage == DeliveryStage::ApiRejectedNoError && error != ERROR_ACCESS_DENIED) ||
        ((stage == DeliveryStage::ApiAccepted) != (error == ERROR_SUCCESS))) {
        receipt = {DeliveryStage::InvalidReply, ERROR_INVALID_DATA};
        return false;
    }
    receipt = {stage, error};
    return true;
}
class Broker {
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    std::atomic<UINT_PTR> serial_{0}, pending_{0};
    std::atomic<DWORD> childPid_{0};
    std::atomic<HWND> owner_{nullptr};
    std::atomic<ULONGLONG> deadline_{0};
    struct Lease { Broker& broker; ~Lease() noexcept { broker.pending_.store(0, std::memory_order_release); broker.busy_.clear(std::memory_order_release); } };
public:
    template<class Ops> DWORD deliver(Ops& ops, HWND window, WPARAM token, DeliveryReceipt* receipt = nullptr) noexcept {
        if (receipt) *receipt = {DeliveryStage::InvalidToken, ERROR_CANCELLED};
        const auto finish = [receipt](DeliveryStage stage, DWORD error) noexcept {
            if (receipt) { receipt->stage = stage; receipt->error = error; }
            return error;
        };
        if (!token || pending_.load(std::memory_order_acquire) != token || owner_.load() != window)
            return finish(DeliveryStage::InvalidToken, ERROR_CANCELLED);
        DWORD ownerPid = 0;
        const auto ownerThread = ops.windowThread(window, ownerPid);
        const auto grantThread = ops.threadId();
        if (!ownerThread || ownerThread != grantThread || ownerPid != ops.processId())
            return finish(DeliveryStage::WrongOwnerThread, ERROR_ACCESS_DENIED);
        if (ops.now() >= deadline_.load()) return finish(DeliveryStage::Expired, ERROR_TIMEOUT);
        DWORD foregroundPid = 0;
        const auto foregroundThread = ops.windowThread(ops.foregroundWindow(), foregroundPid);
        if (!foregroundThread || foregroundPid != ownerPid)
            return finish(DeliveryStage::ForeignForeground, ERROR_ACCESS_DENIED);
        const auto child = childPid_.load();
        if (!child || child == ASFW_ANY) return finish(DeliveryStage::InvalidChild, ERROR_INVALID_PARAMETER);
        UINT_PTR expected = static_cast<UINT_PTR>(token);
        if (!pending_.compare_exchange_strong(expected, 0, std::memory_order_acq_rel))
            return finish(DeliveryStage::Cancelled, ERROR_CANCELLED);
        ops.setError(ERROR_SUCCESS);
        if (ops.allow(child)) {
            return finish(DeliveryStage::ApiAccepted, ERROR_SUCCESS);
        }
        const auto error = ops.error();
        return finish(error ? DeliveryStage::ApiRejected : DeliveryStage::ApiRejectedNoError, error ? error : ERROR_ACCESS_DENIED);
    }
    template<class Ops> bool request(Ops& ops, HWND window, DWORD child, DeliveryReceipt* receipt = nullptr) noexcept {
        DeliveryReceipt ignored;
        auto& result = receipt ? *receipt : ignored;
        result = {};
        if (!window || !child || child == ASFW_ANY) { result.error = ERROR_INVALID_PARAMETER; ops.setError(result.error); return false; }
        if (busy_.test_and_set(std::memory_order_acquire)) { result.error = ERROR_BUSY; ops.setError(result.error); return false; }
        Lease lease{*this};
        DWORD ownerPid = 0;
        const auto ownerThread = ops.windowThread(window, ownerPid);
        if (!ownerThread || ownerPid != ops.processId()) { result.error = ERROR_ACCESS_DENIED; ops.setError(result.error); return false; }
        const auto token = serial_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!token) { result.error = ERROR_INVALID_DATA; ops.setError(result.error); return false; }
        owner_.store(window); childPid_.store(child); deadline_.store(ops.now() + maximumWaitMs);
        pending_.store(token, std::memory_order_release);
        if (ownerThread == ops.threadId()) deliver(ops, window, token, &result);
        else {
            DWORD_PTR reply = 0;
            ops.setError(ERROR_SUCCESS); // timeout APIs may fail without assigning a new error
            if (!ops.send(window, token, maximumWaitMs, reply)) {
                const auto error = ops.error();
                result = {error ? DeliveryStage::TransportFailed : DeliveryStage::TransportNoError, error ? error : ERROR_GEN_FAILURE};
            } else decodeReply(reply, result);
        }
        ops.setError(result.error);
        return result.apiAccepted() && result.error == ERROR_SUCCESS;
    }
};
inline Broker broker;
inline bool request(HWND window, DWORD child, DeliveryReceipt* receipt = nullptr) noexcept { WindowsOps ops; return broker.request(ops, window, child, receipt); }
inline LRESULT deliver(HWND window, WPARAM token) noexcept {
    WindowsOps ops; DeliveryReceipt receipt;
    broker.deliver(ops, window, token, &receipt);
    return encodeReply(receipt);
}
} // namespace ymcc::taskView::foreground
