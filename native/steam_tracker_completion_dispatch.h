#pragma once
#include "ipc_result_mailbox.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

// Completion delivery only. It neither reads a source nor grants readiness.
namespace ymcc::steamcompletion {
class Dispatcher final {
    struct Counter {std::atomic<unsigned> retained{0};};
    struct Ticket final {
        std::shared_ptr<Counter> counter;
        explicit Ticket(std::shared_ptr<Counter> value) noexcept:counter(std::move(value)){}
        Ticket(const Ticket&)=delete;
        Ticket& operator=(const Ticket&)=delete;
        Ticket(Ticket&& other) noexcept:counter(std::move(other.counter)){}
        Ticket& operator=(Ticket&&)=delete;
        ~Ticket(){if(counter)counter->retained.fetch_sub(1,std::memory_order_acq_rel);}
    };
    struct Notice final {
        Ticket ticket; // Return the slot after callback captures are released.
        std::function<void()> callback;
        Notice(Ticket value,std::function<void()> consume) noexcept
            :ticket(std::move(value)),callback(std::move(consume)){}
    };
    const std::shared_ptr<Counter> counter_=std::make_shared<Counter>();
    ipc::ResultMailbox<std::shared_ptr<Notice>> mailbox_;
    std::optional<Ticket> acquire() noexcept {
        auto observed=counter_->retained.load(std::memory_order_acquire);
        // Bounded contention too; a failed offer stays pending with its owner.
        for(unsigned attempt=0;attempt<capacity;++attempt){
            if(observed>=capacity)return {};
            if(counter_->retained.compare_exchange_weak(observed,observed+1,std::memory_order_acq_rel))
                return Ticket(counter_);
        }
        return {};
    }
public:
    static constexpr unsigned capacity=32;
    enum class Drain {Missing,Invoked,ConsumerFailed};
    Dispatcher()=default;
    Dispatcher(const Dispatcher&)=delete;
    Dispatcher& operator=(const Dispatcher&)=delete;
    ~Dispatcher(){retire();}
    unsigned retained()const noexcept{return counter_->retained.load(std::memory_order_acquire);}
    // send must only perform a nonblocking asynchronous PostMessage. The source
    // caller may still hold consumer locks; callback is never invoked here.
    template<class Post> bool post(std::function<void()> callback,Post send) noexcept {
        try{
            if(!callback)return false;
            auto ticket=acquire();if(!ticket)return false;
            auto notice=std::make_shared<Notice>(std::move(*ticket),std::move(callback));
            // Keep the whole Notice alive until post returns. A failed send may
            // erase its envelope under the original mailbox lock; captured
            // destructors and the slot ticket must be released outside that lock.
            auto envelope=std::make_unique<std::shared_ptr<Notice>>(notice);
            return mailbox_.post(std::move(envelope),std::move(send));
        }catch(...){return false;}
    }
    // take releases the original mailbox mutex before entering any consumer.
    Drain drain(std::uintptr_t token) noexcept {
        auto envelope=mailbox_.take(token);if(!envelope||!*envelope)return Drain::Missing;
        auto notice=std::move(*envelope);
        try{notice->callback();return Drain::Invoked;}
        catch(...){return Drain::ConsumerFailed;}
    }
    // The original process exit owns retirement. It never reopens on Ready.
    void retire() noexcept{mailbox_.retire();}
};
}
