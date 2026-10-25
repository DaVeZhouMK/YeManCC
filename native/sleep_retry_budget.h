#pragma once

// One automatic repair chain survives suspend confirmations, new power
// generations and canceled/failed requests. Only a confirmed user wake grants
// a new budget. No OS calls, timers or background work live in this policy.
namespace ymcc::sleeprepair {
class Budget {
    const unsigned int limit_;
    unsigned int requests_ = 0;
public:
    explicit constexpr Budget(unsigned int limit) noexcept : limit_(limit) {}
    bool available() const noexcept { return requests_ < limit_; }
    bool consume() noexcept {
        if (!available()) return false;
        ++requests_;
        return true;
    }
    void confirmedUserWake() noexcept { requests_ = 0; }
    unsigned int requests() const noexcept { return requests_; }
    unsigned int limit() const noexcept { return limit_; }
};
}
