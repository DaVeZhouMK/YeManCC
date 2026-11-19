#pragma once
#include "steam_session_observer.h"
#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
namespace ymcc::steamsession {
// One process-lifetime attachment of the original main consumers. Stable
// dispatchers exist before subscriptions are created, so early facts and late
// consumers use the same worker, full WinEvent hooks and immutable publisher.
class ConsumerRelay final {
    struct Callbacks {
        std::function<void(bool)> changed;
        std::function<bool()> retry;
        std::function<void()> ui;
        IdentityPublished identity;
        IdentityPublished seed;
    };
    struct State {std::atomic<std::shared_ptr<const Callbacks>> callbacks;std::atomic<bool> seeded{false};};
    const std::shared_ptr<State> state_=std::make_shared<State>();
public:
    bool attach(std::function<void(bool)> changed,std::function<bool()> retry,
        std::function<void()> ui,IdentityPublished identity,IdentityPublished seed={}) noexcept {
        try{
            if(!changed||!retry||!ui||!identity)return false;
            if(state_->callbacks.load(std::memory_order_acquire))return true;
            auto next=std::make_shared<const Callbacks>(Callbacks{std::move(changed),std::move(retry),std::move(ui),std::move(identity),std::move(seed)});
            std::shared_ptr<const Callbacks> empty;
            state_->callbacks.compare_exchange_strong(empty,std::move(next),std::memory_order_release,std::memory_order_acquire);
            return true; // Existing first attachment is immutable and idempotent.
        }catch(...){return false;}
    }
    bool start(Observer& observer,StartupWindowFact onStartupWindowFact={}) {
        const auto state=state_;
        return observer.start([state](bool changed){
            const auto c=state->callbacks.load(std::memory_order_acquire);if(c)c->changed(changed);
        },[state]{
            const auto c=state->callbacks.load(std::memory_order_acquire);return c&&c->retry();
        },[state]{
            const auto c=state->callbacks.load(std::memory_order_acquire);if(c)c->ui();
        },[state](std::shared_ptr<const IdentitySnapshot> source){
            const auto c=state->callbacks.load(std::memory_order_acquire);
            // The original observer must not acknowledge a receipt that no
            // consumer received. Late attachment uses an existing refresh hint;
            // it does not invent Changed(true) or renew an observer retry window.
            if(!c)throw std::runtime_error("steam-consumers-not-attached");
            // Initial facts and every subsequent edge share this same original
            // worker ordering. Main must not write an older captured snapshot
            // after a newer changed callback has already reached consumers.
            if(source&&!state->seeded.load(std::memory_order_acquire)){
                if(c->seed)c->seed(source);
                state->seeded.store(true,std::memory_order_release);
            }
            c->identity(std::move(source));
        },std::move(onStartupWindowFact));
    }
};
}
