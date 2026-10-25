#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <system_error>

namespace ymcc::inputHost {
namespace transactionDetail {
inline std::uint64_t reserveIdentity(std::atomic<std::uint64_t>& next) noexcept {
    auto value = next.load(std::memory_order_relaxed);
    // Never wrap or issue zero. Exhaustion permanently denies new identities.
    while (value != 0 && value != std::numeric_limits<std::uint64_t>::max()) {
        if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
            return value;
    }
    return 0;
}
struct ThreadIdentity {
    static std::uint64_t get() noexcept {
        static std::atomic<std::uint64_t> next{1};
        static thread_local const std::uint64_t identity = reserveIdentity(next);
        return identity;
    }
};
} // namespace transactionDetail
// Nonrecursive ownership observation. Identities never reuse a TLS address or
// OS TID, so an old relaxed owner value cannot identify a newly created thread.
template<class Mutex = std::mutex, class Identity = transactionDetail::ThreadIdentity>
class TransactionMutex final {
    Mutex mutex_;
    std::atomic<std::uint64_t> owner_{0};
public:
    TransactionMutex() = default;
    TransactionMutex(const TransactionMutex&) = delete;
    TransactionMutex& operator=(const TransactionMutex&) = delete;
    bool ownedByCurrentThread() const noexcept {
        const auto identity = Identity::get();
        return identity != 0 && owner_.load(std::memory_order_relaxed) == identity;
    }
    void lock() {
        const auto identity = Identity::get();
        if (identity == 0)
            throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
        mutex_.lock();
        owner_.store(identity, std::memory_order_relaxed);
    }
    bool try_lock() {
        const auto identity = Identity::get();
        if (identity == 0 || owner_.load(std::memory_order_relaxed) == identity) return false;
        if (!mutex_.try_lock()) return false;
        owner_.store(identity, std::memory_order_relaxed);
        return true;
    }
    void unlock() {
        owner_.store(0, std::memory_order_relaxed);
        mutex_.unlock();
    }
};
} // namespace ymcc::inputHost
