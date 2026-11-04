#pragma once
#include "json.hpp"
#include "windows_motion_submission.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>

namespace ymcc::sensors::virtual_link {
// One finite check after an admitted Host/configuration, on the existing STA.
// This owns neither sensors nor the output switch and never requests PnP/rebind.
class Wake final {
public:
    struct Ticket {
        std::uint64_t id=0, config=0, host=0, power=0, bindingGeneration=0, due=0, expires=0;
        std::weak_ptr<const std::atomic<std::uint64_t>> binding;
        bool verification=false;
    };
    static constexpr std::uint32_t settleMs=750, verificationMs=1500, freshMs=250, lifetimeMs=5000;
private:
    mutable std::mutex mx_;
    nlohmann::json context_;
    std::atomic<std::uint64_t> config_{0}, seenConfig_{0}, seenHost_{0}, seenPower_{0};
    std::atomic<bool> enabled_{false};
    std::uint64_t serial_=0, committed_=0;
    std::optional<Ticket> pending_;
    bool currentLocked(const Ticket& t,std::uint64_t host,std::uint64_t power) const {
        return enabled_.load(std::memory_order_acquire) && t.id==serial_ &&
            t.config==config_.load(std::memory_order_acquire) && t.host==host && t.power==power && host && power;
    }
    static bool bindingCurrent(const Ticket& t) {
        const auto binding=t.binding.lock();
        return binding && t.bindingGeneration && binding->load(std::memory_order_acquire)==t.bindingGeneration;
    }
public:
    void configure(const nlohmann::json& context,bool enabled) {
        std::lock_guard<std::mutex> lock(mx_);
        if(context_==context && enabled_.load(std::memory_order_relaxed)==enabled)return;
        context_=context; pending_.reset(); ++serial_;
        config_.fetch_add(1,std::memory_order_acq_rel);
        enabled_.store(enabled,std::memory_order_release);
    }
    std::uint64_t configuration() const noexcept {return enabled_.load(std::memory_order_acquire)?config_.load(std::memory_order_acquire):0;}
    bool needsReady(std::uint64_t host,std::uint64_t power) const noexcept {
        return host && power && enabled_.load(std::memory_order_acquire) &&
            (seenConfig_.load(std::memory_order_acquire)!=config_.load(std::memory_order_acquire) ||
             seenHost_.load(std::memory_order_acquire)!=host || seenPower_.load(std::memory_order_acquire)!=power);
    }
    bool accepted(std::uint64_t host,std::uint64_t power,const windows_motion::Stamp& binding,std::uint64_t now,std::uint64_t expectedConfig) {
        if(!expectedConfig || !needsReady(host,power))return false;
        std::lock_guard<std::mutex> lock(mx_);
        if(!needsReady(host,power) || expectedConfig!=config_.load(std::memory_order_acquire) || !windows_motion::current(binding))return false;
        const auto config=config_.load(std::memory_order_acquire);
        pending_=Ticket{++serial_,config,host,power,binding.bindingGeneration,now+settleMs,now+lifetimeMs,binding.activeBinding,false};
        seenHost_.store(host,std::memory_order_release); seenPower_.store(power,std::memory_order_release);
        seenConfig_.store(config,std::memory_order_release);
        return true;
    }
    std::uint32_t waitMs(std::uint64_t now,std::uint32_t fallback) const {
        std::lock_guard<std::mutex> lock(mx_);
        if(!pending_)return fallback;
        return static_cast<std::uint32_t>((std::min)(std::uint64_t(fallback),pending_->due>now?pending_->due-now:0));
    }
    std::optional<Ticket> take(std::uint64_t now,std::uint64_t host,std::uint64_t power) {
        std::lock_guard<std::mutex> lock(mx_);
        if(!pending_)return {};
        if(now>=pending_->expires || !currentLocked(*pending_,host,power) || !bindingCurrent(*pending_)){pending_.reset();return {};}
        if(now<pending_->due)return {};
        auto result=pending_;pending_.reset();return result;
    }
    bool current(const Ticket& t,std::uint64_t host,std::uint64_t power) const {
        std::lock_guard<std::mutex> lock(mx_);
        return currentLocked(t,host,power) && bindingCurrent(t);
    }
    template<class StillAllowed>
    bool commit(const Ticket& t,std::uint64_t host,std::uint64_t power,std::uint64_t now,StillAllowed&& allowed) {
        std::lock_guard<std::mutex> lock(mx_);
        if(t.verification || now>=t.expires || committed_==t.id || !currentLocked(t,host,power) ||
            !bindingCurrent(t) || !allowed())return false;
        // Cancellation linearizes here: before this point there is no sensor effect.
        // After this point the existing STA-owned same-object transaction must finish
        // its cleanup; OFF still revokes output and all later checks immediately.
        committed_=t.id;return true;
    }
    bool verifyAfter(const Ticket& t,const windows_motion::Stamp& binding,std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mx_);
        // Only our completed same-object resubscription may advance this ticket.
        if(t.verification || committed_!=t.id || now>=t.expires || !currentLocked(t,t.host,t.power) || !windows_motion::current(binding) ||
            binding.bindingGeneration==t.bindingGeneration || !windows_motion::sameOwner(t.binding.lock(),binding.activeBinding))return false;
        auto verify=t;verify.binding=binding.activeBinding;verify.bindingGeneration=binding.bindingGeneration;
        verify.verification=true;verify.due=now+verificationMs;pending_=std::move(verify);return true;
    }
    void cancelPending() {std::lock_guard<std::mutex> lock(mx_);pending_.reset();}
};
} // namespace ymcc::sensors::virtual_link
