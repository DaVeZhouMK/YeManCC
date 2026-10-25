#pragma once
#include "touch_haptic_dispatch.h"
#include <cstdint>
namespace ymcc::touchhaptic {
// Serialized by the existing feedback owner. Ops owns platform resources.
// Exactly one stable-generation resource or one retired in-flight resource.
// No resource reuse across power/source identity and no unbounded retirement list.
template<class Ops> class FeedbackLease {
public:
    using Resource=typename Ops::Resource;
    using Spec=typename Ops::Spec;
    struct Stats {std::uint64_t opens=0,openAttempts=0,reuses=0,writes=0,closes=0,retired=0;} stats;
    FeedbackLease()=default;
    FeedbackLease(const FeedbackLease&)=delete;
    FeedbackLease& operator=(const FeedbackLease&)=delete;
    ~FeedbackLease(){if(!invalidate()&&resource_)Ops::quarantine(resource_);}
    bool invalidate()noexcept {blocked_=false;retryMs_=250;return retire();}
private:
    bool retire()noexcept {
        if(!resource_)return true;
        retiring_=true;
        if(Ops::pending(*resource_)){Ops::cancel(*resource_);if(!Ops::reap(*resource_))return false;}
        Ops::close(resource_);resource_=nullptr;key_={};++stats.closes;retiring_=false;return true;
    }
    void block(const SourceToken& key,Reason reason,std::uint32_t error)noexcept {
        blocked_=true;blockedKey_=key;blockedReason_=reason;blockedError_=error;nextRetry_=Ops::now()+retryMs_;
        retryMs_=retryMs_<2500?retryMs_*2:5000;
    }
public:
    Resource* ensure(const SourceToken& key,const Spec& spec,Reason& reason,std::uint32_t& error) {
        if(!key.ready()){reason=Reason::SourcePending;return nullptr;}
        if(resource_&&!retiring_&&key_==key){++stats.reuses;return resource_;}
        if(resource_&&retiring_&&!retire()){reason=Reason::IoPending;return nullptr;}
        if(blocked_&&blockedKey_==key&&Ops::now()<nextRetry_){reason=blockedReason_;error=blockedError_;return nullptr;}
        if(!retire()){reason=Reason::IoPending;return nullptr;}
        ++stats.openAttempts;resource_=Ops::open(spec,error);
        if(!resource_){reason=Reason::OpenFailed;block(key,reason,error);return nullptr;}
        ++stats.opens;key_=key;retiring_=false;blocked_=false;retryMs_=250;return resource_;
    }
    template<class Allowed> Receipt write(const SourceToken& key,const Spec& spec,unsigned smallMotor,unsigned largeMotor,Allowed allowed) {
        Receipt result;result.backend=key.backend;
        if(!smallMotor&&!largeMotor)blocked_=false; // safety stops bypass retry delay, never the ownership/reap gate
        auto* resource=ensure(key,spec,result.reason,result.error);if(!resource)return result;
        if(!allowed()){result.reason=Reason::SourceChanged;retire();return result;}
        ++stats.writes;result=Ops::write(*resource,smallMotor,largeMotor);result.backend=key.backend;
        if(!result.ok){retiring_=true;if(Ops::pending(*resource)){++stats.retired;}retire();block(key,result.reason,result.error);}
        return result;
    }
    Resource* resource()noexcept{return resource_;}
    const SourceToken& key()const noexcept{return key_;}
private:
    Resource* resource_=nullptr;SourceToken key_{},blockedKey_{};bool retiring_=false,blocked_=false;
    Reason blockedReason_=Reason::None;std::uint32_t blockedError_=0;std::uint64_t nextRetry_=0,retryMs_=250;
};
} // namespace ymcc::touchhaptic
