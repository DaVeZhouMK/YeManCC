#pragma once
#include <atomic>
#include <memory>
#include <utility>
#include "sleep_power_observation.h"
#include "sleep_power_revocation.h"

namespace ymcc::inputHost::sharedPower {
enum class Use {Input, Lifecycle};
// An input-use policy over the ONE Native snapshot, not a second power owner.
// Existing retained targets may write in Resuming. This never grants ordinary
// hardware/create admission, and pending notifications always revoke the use.
inline bool inputAdmitted(const sleeppower::SharedPowerV1& power,bool keepTarget) noexcept {
    return sleeppower::powerObservationKnown(power) && !(power.flags&sleeppower::kNotificationPending) &&
        (sleeppower::powerAdmitted(power) || (keepTarget&&power.phase==3));
}
inline bool capture(sleeppower::Reader reader,bool keepTarget,sleeppower::SharedPowerV1& power) noexcept {
    power={};
    sleeppower::SharedPowerV1 observed;
    if(!sleeppower::capturePowerObservation(reader,observed)||!inputAdmitted(observed,keepTarget))return false;
    power=observed;return true;
}
inline bool current(sleeppower::Reader reader,const sleeppower::SharedPowerV1& power,bool keepTarget) noexcept {
    return inputAdmitted(power,keepTarget)&&sleeppower::powerObservationCurrent(reader,power);
}

// Immutable public observation plus its original one-way cancellation mirror.
// The Native process provides the reader and registry. A pending operation
// retains this exact object; Ready or a newer capture cannot revive it.
class Scope final {
    const sleeppower::Reader reader_;
    const sleeppower::SharedPowerV1 power_;
    const bool keepTarget_;
    const Use use_;
    sleeppower::RevocationMirrors::Registration registration_;
    mutable std::atomic<bool> retired_{false};
    Scope(sleeppower::Reader reader,sleeppower::SharedPowerV1 power,bool keepTarget,Use use,
          sleeppower::RevocationMirrors::Registration registration) noexcept
        :reader_(reader),power_(power),keepTarget_(keepTarget),use_(use),registration_(std::move(registration)){}
public:
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    static std::shared_ptr<Scope> create(sleeppower::Reader reader,sleeppower::RevocationMirrors& mirrors,bool keepTarget,Use use=Use::Input) noexcept {
        try {
            sleeppower::SharedPowerV1 power;
            if(use==Use::Input){if(!capture(reader,keepTarget,power))return {};}
            else if(!sleeppower::capturePowerObservation(reader,power)||(power.flags&sleeppower::kNotificationPending))return {};
            auto registration=mirrors.observeRead(reader,power);
            if(!registration||registration.cancelled()||!sleeppower::powerObservationCurrent(reader,power))return {};
            return std::shared_ptr<Scope>(new Scope(reader,power,keepTarget,use,std::move(registration)));
        }catch(...){return {};}
    }
    const sleeppower::SharedPowerV1& power()const noexcept{return power_;}
    bool keepTarget()const noexcept{return keepTarget_;}
    Use use()const noexcept{return use_;}
    HANDLE event()const noexcept{return registration_.event();} // Borrowed; never reset/close.
    bool current()const noexcept {
        if(retired_.load(std::memory_order_acquire)||registration_.cancelled())return false;
        if((use_==Use::Lifecycle||inputAdmitted(power_,keepTarget_))&&
           sleeppower::powerObservationCurrent(reader_,power_))return true;
        retired_.store(true,std::memory_order_release);return false;
    }
    // Only the original serial request/Host owner calls cancel or releases its
    // cache. Pure current() may run at the final transport issue boundary.
    void cancel()noexcept{retired_.store(true,std::memory_order_release);registration_.cancel();}
};

// Caller holds the existing Host transaction mutex. Cache only one current
// derived observation; no timer, epoch, business retry or raw sleep consumer.
// Publication-only advances reuse the same event. Old operations keep their
// shared_ptr until actual terminal return, independently of this cache.
class Cache final {
    std::shared_ptr<Scope> scope_;
public:
    std::shared_ptr<Scope> acquire(sleeppower::Reader reader,sleeppower::RevocationMirrors& mirrors,bool keepTarget,Use use=Use::Input)noexcept{
        if(scope_&&scope_->keepTarget()==keepTarget&&scope_->use()==use&&scope_->current())return scope_;
        reset();scope_=Scope::create(reader,mirrors,keepTarget,use);return scope_;
    }
    void reset()noexcept{if(scope_)scope_->cancel();scope_.reset();}
};
}
