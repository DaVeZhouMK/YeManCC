#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <memory>
#include <string>
#include <utility>

// A write owner, not a capability table or device writer. Admission stays in
// the existing total library; callers supply its current family/model context.
namespace ymcc::ayaLighting {
struct Context { bool managed=false, ready=false; std::uint64_t power=0; std::string model; };
class WriteOwner {
    struct State {std::mutex mutex;std::atomic<std::uint64_t> sequence{0};};
    std::shared_ptr<State> state_=std::make_shared<State>();
public:
    struct Lease {
        // Declared before lock: the mutex unlocks before its state can retire.
        std::shared_ptr<void> retainedOwner;
        std::unique_lock<std::mutex> lock;
        bool accepted=true;
        const char* reason="ok";
        Lease()=default;
        Lease(Lease&&)=default;
        Lease& operator=(Lease&&)=delete;
        Lease(std::shared_ptr<void> owner,std::unique_lock<std::mutex> held,bool ok,const char* why):retainedOwner(std::move(owner)),lock(std::move(held)),accepted(ok),reason(why){}
        Lease(std::unique_lock<std::mutex> held,bool ok,const char* why):lock(std::move(held)),accepted(ok),reason(why){}
    };
    std::uint64_t sequence() const noexcept { return state_->sequence.load(std::memory_order_acquire); }
    void invalidate() noexcept { state_->sequence.fetch_add(1,std::memory_order_acq_rel); }
    template<class Current> Lease acquire(const Context& initial,bool restore,std::uint64_t expectedSequence,
                                         std::uint64_t expectedPower,Current current) {
        if(!initial.managed)return {};
        const auto state=state_; // Also pins a caller already waiting when its owner retires.
        const auto ticket=restore?expectedSequence:state->sequence.fetch_add(1,std::memory_order_acq_rel)+1;
        std::unique_lock<std::mutex> lock(state->mutex);
        const auto live=current();
        if(!live.managed||live.model!=initial.model)return {state,std::move(lock),false,"aya-lighting-model-changed"};
        if(!live.ready||live.power!=initial.power||(restore&&live.power!=expectedPower))
            return {state,std::move(lock),false,"power-transition"};
        if(ticket!=state->sequence.load(std::memory_order_acquire))return {state,std::move(lock),false,"aya-lighting-request-superseded"};
        return {state,std::move(lock),true,"ok"};
    }
};
}
