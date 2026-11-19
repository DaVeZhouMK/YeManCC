#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <utility>

// A single explicit TaskView action. Only the dispatcher-owned, ready worker
// may receive the grant. The UI message carries a scalar token, never a pointer
// whose lifetime could outlive SendMessageTimeout. No input injection or retry.
namespace ymcc::taskView::foreground {
inline constexpr UINT message = WM_APP + 831;
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
    HANDLE duplicate(HANDLE process) const noexcept {
        HANDLE copy = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(),process,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS)) return nullptr;
        return copy;
    }
    DWORD childId(HANDLE process) const noexcept { return GetProcessId(process); }
    void close(HANDLE process) const noexcept {
        const DWORD savedError = GetLastError();
        CloseHandle(process);
        SetLastError(savedError);
    }
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
// Pin the process object returned by CreateProcess, never reopen a PID. The
// deleter owns a copy of Ops, so completion may outlive the sender's stack.
template<class Ops> std::shared_ptr<void> pinProcess(Ops ops, HANDLE process, DWORD child) noexcept {
    if (!process || process == INVALID_HANDLE_VALUE || !child || child == ASFW_ANY) {
        ops.setError(ERROR_INVALID_HANDLE); return {};
    }
    const HANDLE copy = ops.duplicate(process);
    if (!copy) return {};
    if (ops.childId(copy) != child) {
        ops.close(copy); ops.setError(ERROR_INVALID_PARAMETER); return {};
    }
    try {
        // shared_ptr invokes this deleter itself if allocation fails.
        return std::shared_ptr<void>(copy,[ops](void* handle) noexcept { ops.close(handle); });
    } catch (...) { ops.setError(ERROR_NOT_ENOUGH_MEMORY); return {}; }
}
class Broker {
    enum class Phase { Pending, Delivering, Cancelled, Complete };
    struct Delivery {
        const UINT_PTR token;
        const HWND owner;
        const DWORD child;
        const ULONGLONG deadline;
        const std::shared_ptr<void> identity;
        std::atomic<Phase> phase{Phase::Pending};
        std::atomic<bool> cancelled{false};
        Delivery(UINT_PTR value, HWND window, DWORD pid, ULONGLONG until, std::shared_ptr<void> pin) noexcept
            : token(value), owner(window), child(pid), deadline(until), identity(std::move(pin)) {}
    };
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    std::atomic<UINT_PTR> serial_{0};
    std::atomic<std::shared_ptr<Delivery>> active_;
    void clear(const std::shared_ptr<Delivery>& state) noexcept {
        auto expected = state;
        active_.compare_exchange_strong(expected,{},std::memory_order_acq_rel);
    }
    struct Lease {
        Broker& broker;
        std::shared_ptr<Delivery> state;
        ~Lease() noexcept {
            if (state) {
                state->cancelled.store(true,std::memory_order_release);
                auto expected = Phase::Pending;
                if (state->phase.compare_exchange_strong(expected,Phase::Cancelled,std::memory_order_acq_rel) || expected == Phase::Complete)
                    broker.clear(state);
                // An entered delivery keeps the single slot and its exact process
                // pin until its own completion. No wait/poll or OS call is retried.
            }
            broker.busy_.clear(std::memory_order_release);
        }
    };
    struct Completion {
        Broker& broker;
        std::shared_ptr<Delivery> state;
        ~Completion() noexcept {
            state->phase.store(Phase::Complete,std::memory_order_release);
            broker.clear(state);
        }
    };
public:
    template<class Ops> DWORD deliver(Ops& ops, HWND window, WPARAM token, DeliveryReceipt* receipt = nullptr) noexcept {
        if (receipt) *receipt = {DeliveryStage::InvalidToken, ERROR_CANCELLED};
        const auto finish = [receipt](DeliveryStage stage, DWORD error) noexcept {
            if (receipt) { receipt->stage = stage; receipt->error = error; }
            return error;
        };
        // Capture once. Every check and the OS call use this immutable request,
        // even if a timed-out sender later admits another token.
        const auto state = active_.load(std::memory_order_acquire);
        if (!state || !token || state->token != token || state->owner != window)
            return finish(DeliveryStage::InvalidToken, ERROR_CANCELLED);
        DWORD ownerPid = 0;
        const auto ownerThread = ops.windowThread(window, ownerPid);
        const auto grantThread = ops.threadId();
        if (!ownerThread || ownerThread != grantThread || ownerPid != ops.processId())
            return finish(DeliveryStage::WrongOwnerThread, ERROR_ACCESS_DENIED);
        if (ops.now() >= state->deadline) return finish(DeliveryStage::Expired, ERROR_TIMEOUT);
        if (!state->child || state->child == ASFW_ANY) return finish(DeliveryStage::InvalidChild, ERROR_INVALID_PARAMETER);
        auto expected = Phase::Pending;
        if (!state->phase.compare_exchange_strong(expected,Phase::Delivering,std::memory_order_acq_rel))
            return finish(DeliveryStage::Cancelled, ERROR_CANCELLED);
        Completion completion{*this,state};
        ops.setError(ERROR_SUCCESS);
        if (state->cancelled.load(std::memory_order_acquire)) return finish(DeliveryStage::Cancelled, ERROR_CANCELLED);
        if (ops.now() >= state->deadline) return finish(DeliveryStage::Expired, ERROR_TIMEOUT);
        // Cancellation after entering the real API has an unknown OS outcome.
        // The sender cannot release/replay on a failed transport; this snapshot
        // keeps the original process object pinned through the entire call.
        if (ops.allow(state->child)) return finish(DeliveryStage::ApiAccepted, ERROR_SUCCESS);
        const auto error = ops.error();
        return finish(error ? DeliveryStage::ApiRejected : DeliveryStage::ApiRejectedNoError, error ? error : ERROR_ACCESS_DENIED);
    }
    template<class Ops> bool request(Ops& ops, HWND window, DWORD child, DeliveryReceipt* receipt = nullptr,
            std::shared_ptr<void> identity = {}) noexcept {
        DeliveryReceipt ignored;
        auto& result = receipt ? *receipt : ignored;
        result = {};
        if (!window || !child || child == ASFW_ANY) { result.error = ERROR_INVALID_PARAMETER; ops.setError(result.error); return false; }
        if (busy_.test_and_set(std::memory_order_acquire)) { result.error = ERROR_BUSY; ops.setError(result.error); return false; }
        Lease lease{*this,{}};
        if (active_.load(std::memory_order_acquire)) { result.error = ERROR_BUSY; ops.setError(result.error); return false; }
        DWORD ownerPid = 0;
        const auto ownerThread = ops.windowThread(window, ownerPid);
        if (!ownerThread || ownerPid != ops.processId()) { result.error = ERROR_ACCESS_DENIED; ops.setError(result.error); return false; }
        const auto token = serial_.fetch_add(1, std::memory_order_relaxed) + 1;
        if (!token) { result.error = ERROR_INVALID_DATA; ops.setError(result.error); return false; }
        try { lease.state = std::make_shared<Delivery>(token,window,child,ops.now()+maximumWaitMs,std::move(identity)); }
        catch (...) { result.error = ERROR_NOT_ENOUGH_MEMORY; ops.setError(result.error); return false; }
        active_.store(lease.state,std::memory_order_release);
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
inline bool request(HWND window, DWORD child, DeliveryReceipt* receipt, HANDLE process) noexcept {
    WindowsOps ops;
    auto identity = pinProcess(ops,process,child);
    if (!identity) {
        const DWORD error = ops.error() ? ops.error() : ERROR_INVALID_HANDLE;
        if (receipt) *receipt = {DeliveryStage::InvalidChild,error};
        ops.setError(error); return false;
    }
    return broker.request(ops,window,child,receipt,std::move(identity));
}
inline LRESULT deliver(HWND window, WPARAM token) noexcept {
    WindowsOps ops; DeliveryReceipt receipt;
    broker.deliver(ops, window, token, &receipt);
    return encodeReply(receipt);
}
} // namespace ymcc::taskView::foreground
