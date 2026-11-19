#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <utility>
#include "sleep_power_state.h"
#include "sleep_power_observation.h"

namespace ymcc::sleeppower {
// Only cancellation mirrors of the existing Native owner. This class cannot
// change power generation/phase, permit IO, open a gate or revive an old event.
class RevocationMirrors {
    struct Node {
        HANDLE event=nullptr;
        Node* previous=nullptr;
        Node* next=nullptr;
        std::atomic<bool> revoked{false};
        explicit Node(HANDLE value) noexcept:event(value){}
        ~Node(){if(event)CloseHandle(event);}
    };
    struct State {
        std::mutex mutex;
        Node* first=nullptr;
        // Registration race watermark only; never a second power generation.
        std::atomic<std::uint64_t> revision{0};
        std::atomic<bool> closed{false};
    };
    const std::shared_ptr<State> state_;
    static std::shared_ptr<State> createState() noexcept {
        try{return std::make_shared<State>();}catch(...){return {};}
    }
    static void signal(Node& node) noexcept {
        // A prior revoker can be paused before SetEvent. Every completed
        // cancellation must finish its own idempotent event signal.
        node.revoked.store(true,std::memory_order_release);
        if(node.event)SetEvent(node.event);
    }
public:
    class Registration {
        friend class RevocationMirrors;
        std::shared_ptr<State> state_;
        std::unique_ptr<Node> node_;
        std::uint64_t revision_=0;
        Registration(std::shared_ptr<State> state,std::unique_ptr<Node> node,std::uint64_t revision) noexcept
            :state_(std::move(state)),node_(std::move(node)),revision_(revision){}
    public:
        Registration() noexcept=default;
        Registration(const Registration&)=delete;
        Registration& operator=(const Registration&)=delete;
        Registration(Registration&& other) noexcept
            :state_(std::move(other.state_)),node_(std::move(other.node_)),revision_(other.revision_){}
        Registration& operator=(Registration&& other) noexcept {
            if(this!=&other){reset();state_=std::move(other.state_);node_=std::move(other.node_);revision_=other.revision_;}
            return *this;
        }
        ~Registration(){reset();}
        // Borrowed handle; do not CloseHandle/ResetEvent. Consumers may duplicate
        // SYNCHRONIZE to an owned child and retain this registration through it.
        HANDLE event() const noexcept{return node_?node_->event:nullptr;}
        explicit operator bool() const noexcept{return node_!=nullptr;}
        // Sticky local cancellation. Keep this registration and its event alive
        // until every borrower/owned child has returned its resources. Like
        // move/reset/destruction, callers serialize changes to this object's
        // lifetime; Native registry revocation can run concurrently.
        void cancel() noexcept {if(node_)signal(*node_);}
        bool cancelled() const noexcept {
            return !state_||!node_||state_->closed.load(std::memory_order_acquire)||
                node_->revoked.load(std::memory_order_acquire)||
                state_->revision.load(std::memory_order_acquire)!=revision_;
        }
        void reset() noexcept {
            if(!node_)return;
            {
                std::lock_guard<std::mutex> lock(state_->mutex);
                signal(*node_); // Cancels any exported duplicate before close.
                if(node_->previous)node_->previous->next=node_->next;
                else state_->first=node_->next;
                if(node_->next)node_->next->previous=node_->previous;
            }
            node_.reset(); // Only owned close; outside the registry lock.
            state_.reset();
        }
    };
    RevocationMirrors() noexcept:state_(createState()){}
    RevocationMirrors(const RevocationMirrors&)=delete;
    RevocationMirrors& operator=(const RevocationMirrors&)=delete;
    ~RevocationMirrors(){close();}
    Registration observe(Reader read,const SharedPowerV1& captured) noexcept {
        const auto state=state_;
        if(!state||state->closed.load(std::memory_order_acquire))return {};
        const auto revision=state->revision.load(std::memory_order_acquire);
        // Reader/allocations/handle creation never run under a Dispatch or
        // registry lock. The revision closes both ABA and registration gaps.
        if(!powerCurrent(read,captured))return {};
        const HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!event)return {};
        std::unique_ptr<Node> node(new(std::nothrow) Node(event));
        if(!node){CloseHandle(event);return {};}
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(state->closed.load(std::memory_order_acquire)||state->revision.load(std::memory_order_acquire)!=revision){signal(*node);return {};}
            node->next=state->first;
            if(state->first)state->first->previous=node.get();
            state->first=node.get();
        }
        Registration result(state,std::move(node),revision);
        if(!powerCurrent(read,captured)||result.cancelled()){result.reset();return {};}
        return result;
    }
    // Explicit read-only observation registration on this same Native mirror.
    // It grants no Ready/hardware capability; observe() above stays unchanged.
    Registration observeRead(Reader read,const SharedPowerV1& captured) noexcept {
        const auto state=state_;
        if(!state||state->closed.load(std::memory_order_acquire))return {};
        const auto revision=state->revision.load(std::memory_order_acquire);
        // Reader/allocations/handle creation never run under a Dispatch or
        // registry lock. The revision closes both ABA and registration gaps.
        if(!powerObservationCurrent(read,captured))return {};
        const HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!event)return {};
        std::unique_ptr<Node> node(new(std::nothrow) Node(event));
        if(!node){CloseHandle(event);return {};}
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(state->closed.load(std::memory_order_acquire)||state->revision.load(std::memory_order_acquire)!=revision){signal(*node);return {};}
            node->next=state->first;
            if(state->first)state->first->previous=node.get();
            state->first=node.get();
        }
        Registration result(state,std::move(node),revision);
        if(!powerObservationCurrent(read,captured)||result.cancelled()){result.reset();return {};}
        return result;
    }
    // Takes ownership even on rejection. The trusted source creates this exact
    // manual-reset event; Registry remains its only cancellation authority.
    Registration observeReadWithEvent(Reader read,const SharedPowerV1& captured,HANDLE ownedEvent) noexcept {
        struct Owned {HANDLE value;~Owned(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}} incoming{ownedEvent};
        if(!ownedEvent||ownedEvent==INVALID_HANDLE_VALUE||WaitForSingleObject(ownedEvent,0)!=WAIT_TIMEOUT)return {};
        const auto state=state_;
        if(!state||state->closed.load(std::memory_order_acquire))return {};
        const auto revision=state->revision.load(std::memory_order_acquire);
        // Reader/allocations/handle creation never run under a Dispatch or
        // registry lock. The revision closes both ABA and registration gaps.
        if(!powerObservationCurrent(read,captured))return {};
        const HANDLE event=incoming.value;incoming.value=nullptr;
        if(!event)return {};
        std::unique_ptr<Node> node(new(std::nothrow) Node(event));
        if(!node){CloseHandle(event);return {};}
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(state->closed.load(std::memory_order_acquire)||state->revision.load(std::memory_order_acquire)!=revision){signal(*node);return {};}
            node->next=state->first;
            if(state->first)state->first->previous=node.get();
            state->first=node.get();
        }
        Registration result(state,std::move(node),revision);
        if(!powerObservationCurrent(read,captured)||result.cancelled()){result.reset();return {};}
        return result;
    }
    void revoke() noexcept {
        const auto state=state_;
        if(!state)return;
        state->revision.fetch_add(1,std::memory_order_acq_rel);
        std::lock_guard<std::mutex> lock(state->mutex);
        // Nodes and events stay owned through this short traversal. No Reader,
        // owner callback, allocation, device/Steam action or other lock here.
        for(auto* node=state->first;node;node=node->next)signal(*node);
    }
    void close() noexcept {
        const auto state=state_;
        if(!state)return;
        state->closed.store(true,std::memory_order_release);
        revoke();
    }
};
}
