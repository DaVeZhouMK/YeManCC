#pragma once
#include <cstdint>
#include <limits>
#include <mutex>
#include <memory>
#include <optional>
#include <utility>

namespace ymcc::fan_resume {
inline constexpr std::uint64_t kRequestNamespaceBit=std::uint64_t{1}<<63;
inline constexpr std::uint64_t kRequestWindowMs=15000;
inline constexpr unsigned kRequestSlotLimit=3;
// Copies of the sole Native Reader facts. This ledger never reads Windows,
// invents power, cancels resources, or grants device/Host authority.
struct PowerStamp {
    std::uint64_t generation=0,suspendSerial=0,notification=0,publication=0;
    std::uint32_t phase=0,flags=0;
    bool cohort(const PowerStamp& b) const noexcept {
        return generation==b.generation&&suspendSerial==b.suspendSerial;
    }
    bool same(const PowerStamp& b) const noexcept {
        return cohort(b)&&notification==b.notification&&phase==b.phase&&flags==b.flags;
    }
};
struct RequestTicket {
    std::uint64_t request=0,run=0,generation=0;
    explicit operator bool() const noexcept{return request&&run&&generation;}
};
struct PostTicket {RequestTicket owner;unsigned attempt=0;explicit operator bool()const noexcept{return owner&&attempt;}};
struct ContinuationTicket {RequestTicket owner;unsigned slot=0;explicit operator bool()const noexcept{return owner&&slot;}};
struct ObserveCounters {
    unsigned probes=0,loggedProbes=0,unreachableProbes=0;
    std::uint64_t lastLogTick=0;
};
// Retention only. Concrete held process belongs to the original Native owner;
// the ledger neither calls it nor treats it as power/SDK/resource proof.
struct RequestAttachment {virtual ~RequestAttachment()=default;};
struct RequestSnapshot {
    std::shared_ptr<RequestAttachment> attachment;
    RequestTicket ticket;
    PowerStamp power;
    std::uint64_t startedTick=0,deadlineTick=0,manualOperation=0,intentRevision=0;
    std::uint64_t writeRevision=0;
    bool sessionOnly=false;
    unsigned attempts=0,posts=0,continuations=0;
    std::uint64_t lastGrantRun=0;
    bool mayPost=true,postUnclassified=false,finished=false,awaitingGrant=false;
    bool launchRetried=false;
    ObserveCounters observations;
};
// One original recovery owner. Legacy and ticketed callers share its gate;
// ticketed work retains its first budget across a fresh Native observation.
class RequestOwner {
    mutable std::mutex mutex_;
    std::uint64_t owner_ = 0;
    std::uint64_t pending_ = 0;
    std::optional<RequestSnapshot> business_,pendingBusiness_;
    struct Admission { PowerStamp power; std::uint64_t writeRevision; };
    std::optional<Admission> pendingPower_;
    std::uint64_t requests_=0,runs_=0,lastManual_=0;
    bool closed_=false;
    static constexpr std::uint64_t windowMs_=kRequestWindowMs;
    static constexpr unsigned postLimit_=kRequestSlotLimit;
    static bool older(std::uint64_t fan,const PowerStamp& power,std::uint64_t intentRevision,const RequestSnapshot& b) noexcept {
        return intentRevision<b.intentRevision||fan<b.ticket.generation||power.generation<b.power.generation||
            (power.generation==b.power.generation&&power.suspendSerial<b.power.suspendSerial)||
            power.notification<b.power.notification||power.publication<b.power.publication;
    }
    static bool sameBusiness(std::uint64_t fan,const PowerStamp& power,std::uint64_t intentRevision,const RequestSnapshot& b) noexcept {
        // suspendSerial revokes old scope authority, including canceled queries;
        // it alone is not proof of a new business or a refundable deadline.
        return fan==b.ticket.generation&&power.generation==b.power.generation&&intentRevision==b.intentRevision;
    }
    static bool expired(const RequestSnapshot& b,std::uint64_t now) noexcept {
        return now<b.startedTick||now>=b.deadlineTick;
    }
    bool owns(const RequestTicket& t) const noexcept {
        return t&&owner_&&business_&&t.request==business_->ticket.request&&
            t.run==business_->ticket.run&&t.generation==owner_;
    }
    // The caller destroys retired bindings after unlocking: they can own a
    // remote offer whose destructor settles an unclaimed publication.
    RequestTicket start(std::uint64_t now,std::shared_ptr<RequestAttachment>& retired) noexcept {
        if(closed_||!business_||business_->finished||expired(*business_,now)||
           runs_==(std::numeric_limits<std::uint64_t>::max)()){
            if(business_){
                business_->finished=true;
                if(business_->attachment)retired=std::move(business_->attachment);
            }
            owner_=0;return {};
        }
        business_->ticket.run=++runs_;
        owner_=business_->ticket.generation;
        return business_->ticket;
    }
public:
    bool begin(std::uint64_t generation) {
        if (!generation) return false;
        std::lock_guard<std::mutex> lock(mutex_);
        if(closed_||business_)return false; // Legacy callers cannot bypass the business ledger.
        if (owner_) {
            if (generation > owner_ && generation > pending_) pending_ = generation;
            return false;
        }
        owner_ = generation;
        return true;
    }
    std::uint64_t finish(std::uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(business_)return 0; // A bare generation cannot release a ticketed run.
        if (!generation || owner_ != generation) return 0;
        owner_ = 0;
        auto next = pending_;
        pending_ = 0;
        return next > generation ? next : 0;
    }
    std::uint64_t pending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pending_;
    }
    // Called only after the original Native purpose/capture gate. Reserving
    // here includes dispatch/thread-start waiting in the FIRST deadline.
    RequestTicket offer(std::uint64_t generation,const PowerStamp& power,
                        std::uint64_t now,std::uint64_t manualOperation=0,std::uint64_t intentRevision=0,std::uint64_t writeRevision=0,bool sessionOnly=false) {
        std::shared_ptr<RequestAttachment> retired;
        std::lock_guard<std::mutex> lock(mutex_);
        if(closed_||!generation||!power.generation||power.phase>3||!(power.flags&1u)||(power.flags&4u)||
           (sessionOnly&&!manualOperation))return {};
        if(owner_&&!business_)return {}; // Do not queue ticketed work behind an unbound legacy caller.
        if(business_&&older(generation,power,intentRevision,*business_))return {};
        if(pendingBusiness_&&older(generation,power,intentRevision,*pendingBusiness_))return {};
        RequestSnapshot* same=nullptr;bool samePending=false;
        if(pendingBusiness_&&sameBusiness(generation,power,intentRevision,*pendingBusiness_)&&
           (!manualOperation||manualOperation==pendingBusiness_->manualOperation)){same=&*pendingBusiness_;samePending=true;}
        else if(business_&&sameBusiness(generation,power,intentRevision,*business_)&&
                (!manualOperation||manualOperation==business_->manualOperation))same=&*business_;
        if(manualOperation&&manualOperation<=lastManual_&&(!same||same->manualOperation!=manualOperation))return {};
        if(same){
            if(same->finished||expired(*same,now)){
                same->finished=true;
                if(!owner_)retired=std::move(same->attachment);
                return {};
            }
            if(writeRevision<same->writeRevision)return {};
            if(samePending){same->power=power;same->writeRevision=writeRevision;return {};}
            if(owner_){
                const auto& newest=pendingPower_?pendingPower_->power:same->power;
                if(power.notification<newest.notification||power.publication<newest.publication||
                   (pendingPower_&&writeRevision<pendingPower_->writeRevision))return {};
                if(pendingPower_||!same->power.same(power)){pendingPower_=Admission{power,writeRevision};pending_=generation;}
                else same->power.publication=power.publication;
                return {};
            }
            if(same->power.same(power))return {}; // publication/duplicate cannot reopen a run
            same->power=power;
            same->writeRevision=writeRevision;
            return start(now,retired);
        }
        if(manualOperation&&manualOperation<=lastManual_)return {};
        if(now>(std::numeric_limits<std::uint64_t>::max)()-windowMs_||
           requests_==(kRequestNamespaceBit-1))return {};
        RequestSnapshot fresh;
        fresh.ticket={++requests_,0,generation};fresh.power=power;fresh.writeRevision=writeRevision;fresh.sessionOnly=sessionOnly;
        fresh.startedTick=now;fresh.deadlineTick=now+windowMs_;fresh.manualOperation=manualOperation;fresh.intentRevision=intentRevision;
        if(manualOperation)lastManual_=manualOperation;
        if(owner_){pendingBusiness_=fresh;pendingPower_.reset();pending_=generation;return {};}
        if(business_)retired=std::move(business_->attachment);
        business_=fresh;pendingPower_.reset();pending_=0;
        return start(now,retired);
    }
    // Read-only command receipt. Registered is not Host acceptance, completion,
    // power Ready or resource return, including when the original worker ended.
    bool registered(std::uint64_t generation,std::uint64_t manualOperation,
                    std::uint64_t intentRevision,std::uint64_t writeRevision) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto matches=[&](const std::optional<RequestSnapshot>& b){return b&&b->ticket.generation==generation&&
            b->manualOperation==manualOperation&&b->intentRevision==intentRevision&&b->writeRevision==writeRevision;};
        return !closed_&&(matches(business_)||matches(pendingBusiness_));
    }
    // A first click can own a session before any curve is active. Locate only
    // that live request; a revoked write revision must not regain authority.
    std::uint64_t retainedSessionManual(std::uint64_t fan,std::uint64_t shared,
            std::uint64_t intent,std::uint64_t write,std::uint64_t now) {
        std::shared_ptr<RequestAttachment> retired;
        std::lock_guard<std::mutex> lock(mutex_);
        auto& latest=pendingBusiness_?pendingBusiness_:business_;
        if(latest&&!owner_&&expired(*latest,now)){
            latest->finished=true;
            retired=std::move(latest->attachment);
            return 0;
        }
        if(closed_||!write||!latest||latest->finished||expired(*latest,now)||!latest->sessionOnly||
           latest->ticket.generation!=fan||latest->power.generation!=shared||
           latest->intentRevision!=intent||latest->writeRevision!=write)return 0;
        return latest->manualOperation;
    }
    std::optional<RequestSnapshot> snapshot(const RequestTicket& ticket) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!owns(ticket))return {};
        return business_;
    }
    std::shared_ptr<RequestAttachment> retainAttachment(const RequestTicket& ticket,
        std::shared_ptr<RequestAttachment> proposed) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(closed_||!owns(ticket)||business_->finished)return {};
        if(!business_->attachment)business_->attachment=std::move(proposed);
        return business_->attachment;
    }
    // Mark uncertain BEFORE real IO. Only the exact original completion may
    // classify a confirmed unsubmitted/retryable response; no exception refund.
    PostTicket reservePost(const RequestTicket& ticket,std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(closed_||!owns(ticket)||business_->finished||expired(*business_,now)||
           !business_->mayPost||business_->attempts>=postLimit_)return {};
        business_->mayPost=false;business_->postUnclassified=true;
        ++business_->posts;business_->lastGrantRun=ticket.run;
        return {ticket,++business_->attempts};
    }
    bool completePost(const PostTicket& post,bool confirmedRetryable) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!owns(post.owner)||post.attempt!=business_->attempts||!business_->postUnclassified)return false;
        business_->postUnclassified=false;
        business_->mayPost=confirmedRetryable&&business_->attempts<postLimit_;
        return true;
    }
    // Only the original Native consumer may call this AFTER exact authenticated
    // Host business acceptance/public-only waiting proof has been decoded. It
    // records that proof's key; generic Ready, import or resource debt is not it.
    bool confirmAwaitingGrant(const RequestTicket& ticket,std::uint64_t keyLow,std::uint64_t keyHigh) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(closed_||!owns(ticket)||business_->finished||!business_->attempts||business_->mayPost||
           keyLow!=(kRequestNamespaceBit|ticket.request)||keyHigh!=business_->deadlineTick)return false;
        business_->awaitingGrant=true;return true;
    }
    // A fresh Native-signed parent consumes the SAME original three slots.
    // This is not another POST and never reopens an unknown POST for replay.
    ContinuationTicket reserveContinuation(const RequestTicket& ticket,std::uint64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(closed_||!owns(ticket)||business_->finished||expired(*business_,now)||business_->mayPost||
           !business_->awaitingGrant||business_->attempts>=postLimit_||business_->lastGrantRun==ticket.run)return {};
        business_->awaitingGrant=false;business_->lastGrantRun=ticket.run;
        ++business_->continuations;
        return {ticket,++business_->attempts};
    }
    bool checkpoint(const RequestTicket& ticket,const ObserveCounters& value,bool confirmedReachable=false) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!owns(ticket))return false;
        auto& saved=business_->observations;
        if(value.probes<saved.probes||value.loggedProbes<saved.loggedProbes||value.lastLogTick<saved.lastLogTick||
           (value.unreachableProbes<saved.unreachableProbes&&!(confirmedReachable&&value.unreachableProbes==0)))return false;
        saved=value;return true;
    }
    std::uint32_t phaseBudget(const RequestTicket& ticket,std::uint64_t now,std::uint32_t originalLimit=1950) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(closed_||!owns(ticket)||business_->finished||expired(*business_,now))return 0;
        const auto remaining=business_->deadlineTick-now;
        return remaining<originalLimit?static_cast<std::uint32_t>(remaining):originalLimit;
    }
    RequestTicket finishRequest(const RequestTicket& ticket,std::uint64_t now,bool terminal) {
        return releaseRun(ticket,now,terminal ? RunEnd::Terminal : RunEnd::Returned);
    }
    RequestTicket finishUnstartedRequest(const RequestTicket& ticket,std::uint64_t now) {
        return releaseRun(ticket,now,RunEnd::Unstarted);
    }
private:
    enum class RunEnd { Returned, Terminal, Unstarted };
    RequestTicket releaseRun(const RequestTicket& ticket,std::uint64_t now,RunEnd end) {
        std::shared_ptr<RequestAttachment> retired;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!owns(ticket))return {};
        const bool retryLaunch=end==RunEnd::Unstarted&&!business_->launchRetried;
        if(retryLaunch)business_->launchRetried=true;
        business_->finished=business_->finished||end==RunEnd::Terminal||
            (end==RunEnd::Unstarted&&!retryLaunch)||expired(*business_,now);
        owner_=0;pending_=0;
        if(pendingBusiness_){
            retired=std::move(business_->attachment);
            business_=pendingBusiness_;pendingBusiness_.reset();pendingPower_.reset();
            return start(now,retired);
        }
        if(pendingPower_){
            business_->power=pendingPower_->power;
            business_->writeRevision=pendingPower_->writeRevision;
            pendingPower_.reset();
            return start(now,retired);
        }
        if(retryLaunch&&!business_->finished)return start(now,retired);
        if(business_->finished)retired=std::move(business_->attachment);
        return {};
    }
public:
    void close() {
        std::shared_ptr<RequestAttachment> retired;
        std::lock_guard<std::mutex> lock(mutex_);closed_=true;
        if(business_){
            business_->finished=true;
            if(!owner_)retired=std::move(business_->attachment);
        }
        pendingBusiness_.reset();pendingPower_.reset();pending_=0;
    }
    // Existing authenticated descriptor.Request can carry this key; only the
    // formal Native factory may publish it. This getter grants no capability.
    std::optional<std::pair<std::uint64_t,std::uint64_t>> requestKey(const RequestTicket& ticket) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if(closed_||!owns(ticket)||business_->finished)return {};
        return std::pair{kRequestNamespaceBit|ticket.request,business_->deadlineTick};
    }
};
}
