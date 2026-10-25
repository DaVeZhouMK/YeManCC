#pragma once
#include <array>
#include <optional>
#include "controller_motion_session.h"

namespace ymcc::controller_motion {
// Two BUS-owned CPU slots. Output capture epochs retire samples independently
// of this lifetime, which must change on source loss/stop/power/raw changes.
class BankSessions {
    struct Scope {std::string identity;std::uint64_t power=0,raw=0,lifetime=0;};
    std::optional<Scope> scope_;
    std::array<std::optional<Session>,2> sessions_;
    std::array<std::optional<Key>,2> keys_;
    std::array<std::optional<double>,2> processedAt_;
    std::array<bool,2> advancedUnselected_{};
    struct FailedInitialization {Key key;Ops ops;ReadOps reads;};
    std::array<std::optional<FailedInitialization>,2> initializationFailures_;
    Session* selected_=nullptr;
    bool retainedOnActivation_=false;
    bool recent(unsigned index,double now,double window) const noexcept {
        return processedAt_[index]&&std::isfinite(now)&&std::isfinite(window)&&window>0&&
            now>=*processedAt_[index]&&now-*processedAt_[index]<=window;
    }
    Session* prepare(const Key& key,double now,bool keepClock,bool resetSameKey=true) {
        const auto index=key.sensorIndex;auto& session=sessions_[index];auto& old=keys_[index];
        if(!session)session.emplace();
        if(old&&!(*old==key)&&session->active()&&!session->reauthorize(*old,key,now,!keepClock))return nullptr;
        if(resetSameKey&&old&&*old==key&&session->active()&&!keepClock&&!session->resetTimeOrigin(key,now))return nullptr;
        old=key;return &*session;
    }
public:
    BankSessions()=default;
    BankSessions(const BankSessions&)=delete;
    BankSessions& operator=(const BankSessions&)=delete;
    explicit operator bool() const noexcept {return selected_!=nullptr;}
    Session* operator->() noexcept {return selected_;}
    const Session* operator->() const noexcept {return selected_;}
    void reset() {
        selected_=nullptr;retainedOnActivation_=false;
        for(auto& session:sessions_)session.reset();
        for(auto& key:keys_)key.reset();
        for(auto& time:processedAt_)time.reset();
        advancedUnselected_={};for(auto& failure:initializationFailures_)failure.reset();scope_.reset();
    }
    bool sameLifetime(const Key& key,std::uint64_t lifetime) const noexcept {
        return scope_&&key.valid()&&lifetime!=0&&scope_->identity==key.physicalIdentity&&
            scope_->power==key.power&&scope_->raw==key.rawGeneration&&scope_->lifetime==lifetime;
    }
    bool activate(const Key& key,std::uint64_t lifetime,double nowSeconds,double continuationWindowSeconds=0) {
        if(!key.valid()||key.sensorIndex>1||!lifetime||!std::isfinite(nowSeconds)){reset();return false;}
        if(!sameLifetime(key,lifetime)){
            reset();scope_=Scope{key.physicalIdentity,key.power,key.rawGeneration,lifetime};
        }
        const auto index=key.sensorIndex;
        retainedOnActivation_=sessions_[index]&&sessions_[index]->active();
        const bool continuous=advancedUnselected_[index]&&recent(index,nowSeconds,continuationWindowSeconds);
        auto* session=prepare(key,nowSeconds,continuous,false);
        if(!session){selected_=nullptr;retainedOnActivation_=false;return false;}
        selected_=session;advancedUnselected_[index]=false;return true;
    }
    // Caller proves this is not the currently selected physical bank. Initial
    // background-only scope is allowed, but never replaces a different scope.
    Session* prepareUnselected(const Key& key,std::uint64_t lifetime,double now,double window) {
        if(!key.valid()||key.sensorIndex>1||!lifetime||!std::isfinite(now))return nullptr;
        if(!scope_)scope_=Scope{key.physicalIdentity,key.power,key.rawGeneration,lifetime};
        if(!sameLifetime(key,lifetime))return nullptr;
        if(processedAt_[key.sensorIndex]&&now<*processedAt_[key.sensorIndex])return nullptr;
        return prepare(key,now,recent(key.sensorIndex,now,window));
    }
    bool bankMatches(const Key& key,std::uint64_t lifetime,const Ops& ops,const ReadOps& reads) const noexcept {
        return key.sensorIndex<2&&sameLifetime(key,lifetime)&&sessions_[key.sensorIndex]&&keys_[key.sensorIndex]&&
            *keys_[key.sensorIndex]==key&&sessions_[key.sensorIndex]->matchesSelection(key,ops,reads);
    }
    bool initializationRefused(const Key& key,const Ops& ops,const ReadOps& reads) const noexcept {
        if(key.sensorIndex>1)return true;const auto& failure=initializationFailures_[key.sensorIndex];
        return failure&&failure->key==key&&failure->ops==ops&&failure->reads==reads;
    }
    void refuseInitialization(const Key& key,const Ops& ops,const ReadOps& reads) {
        if(key.sensorIndex<2)initializationFailures_[key.sensorIndex]=FailedInitialization{key,ops,reads};
    }
    bool noteProcessed(const Key& key,std::uint64_t lifetime,double now,bool unselected) noexcept {
        if(key.sensorIndex>1||!sameLifetime(key,lifetime)||!sessions_[key.sensorIndex]||
            !sessions_[key.sensorIndex]->handleFor(key)||!std::isfinite(now)||
            (processedAt_[key.sensorIndex]&&now<*processedAt_[key.sensorIndex]))return false;
        processedAt_[key.sensorIndex]=now;advancedUnselected_[key.sensorIndex]=unselected;return true;
    }
    void pauseUnselected(unsigned index) noexcept {
        if(index>1)return;processedAt_[index].reset();advancedUnselected_[index]=false;
    }
    bool retainedOnActivation() const noexcept {return retainedOnActivation_;}
    unsigned liveSlots() const noexcept {
        unsigned count=0;for(const auto& session:sessions_)if(session&&session->active())++count;return count;
    }
};
} // namespace ymcc::controller_motion
