#pragma once
#include <atomic>
#include <exception>

namespace ymcc::inputHost {
// A claimed lifecycle queue item must never strand the serial owner when a
// snapshot, diagnostic or completion observer throws. This grants no device
// authority and does not replay an attempted operation.
template<class Flag>
class LifecycleClaimGuard final {
    Flag& requested_;
    Flag& inFlight_;
    int exceptions_;
public:
    LifecycleClaimGuard(Flag& requested,Flag& inFlight) noexcept
        : requested_(requested),inFlight_(inFlight),exceptions_(std::uncaught_exceptions()) {}
    LifecycleClaimGuard(const LifecycleClaimGuard&)=delete;
    LifecycleClaimGuard& operator=(const LifecycleClaimGuard&)=delete;
    ~LifecycleClaimGuard() noexcept {
        if(std::uncaught_exceptions()>exceptions_)
            requested_.store(true,std::memory_order_release);
        inFlight_.store(false,std::memory_order_release);
    }
};
} // namespace ymcc::inputHost
