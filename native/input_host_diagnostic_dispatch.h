#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <new>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

// Diagnostic data only. No Host/controller/pipe ownership or input recovery.
// The production submitter must be an asynchronous, zero-wait offer to an
// ALREADY running executor. It must never invoke the callback inline.
namespace ymcc::inputHost::diagnostics {
class Scope final {
    inline static thread_local unsigned depth_=0;
public:
    Scope() noexcept { ++depth_; }
    ~Scope() { --depth_; }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
    static bool active() noexcept { return depth_!=0; }
};

// Failure branch observation only; submit refusal does not identify its underlying cause.
enum class RejectedPost { Busy, Closed, Oversized, Cooldown, Full, SubmissionNotAccepted,
    SubmissionAllocationException, SubmissionStandardException, SubmissionUnknownException };

struct Snapshot {
    std::uint64_t offered=0,accepted=0,rejectedBusy=0,rejectedClosed=0;
    std::uint64_t rejectedFull=0,rejectedOversized=0,rejectedCooldown=0;
    std::uint64_t submitFailed=0,sinkReturned=0,sinkThrew=0,callbackAbandoned=0;
    std::uint64_t pendingDiscarded=0,pendingRows=0,payloadBytes=0;
    bool closed=false,sinkActive=false;
};

template<class Data, std::size_t Capacity=32, class Mutex=std::mutex> class Dispatcher final {
    static_assert(Capacity>0);
    static_assert(std::is_nothrow_move_constructible_v<Data>);
    struct Item { Data data; std::size_t charge; };
    struct Counters {
        std::atomic<std::uint64_t> offered{0},accepted{0},rejectedBusy{0},rejectedClosed{0};
        std::atomic<std::uint64_t> rejectedFull{0},rejectedOversized{0},rejectedCooldown{0};
        std::atomic<std::uint64_t> submitFailed{0},sinkReturned{0},sinkThrew{0},callbackAbandoned{0};
        std::atomic<std::uint64_t> pendingDiscarded{0},pendingRows{0},payloadBytes{0};
        std::atomic<bool> sinkActive{false};
    } counters_;
    const std::size_t recordLimit_,byteLimit_;
    std::function<bool(std::function<void()>)> submit_;
    std::function<void(const Data&)> sink_;
    void (*rejected_)(const Data&,RejectedPost) noexcept;
    void observeRejected(const Data& data,RejectedPost reason) const noexcept {
        if(rejected_)rejected_(data,reason);
    }
    std::atomic<bool> closed_{false};
    Mutex gate_;
    inline static thread_local Dispatcher* gateOwner_=nullptr;
    std::array<std::optional<Item>,Capacity> pending_;
    std::size_t head_=0,count_=0,bytes_=0;
    bool scheduled_=false,active_=false,failedSubmission_=false;
    std::uint64_t failedAt_=0;

    // A producer may be preempted immediately before unlock. Check Closed
    // again AFTER releasing the gate so that Close's failed try_lock cannot
    // strand queued data behind an unrelated hung sink.
    void discardClosedPending() noexcept {
        if(gateOwner_==this)return; // no recursive try_lock on a nonrecursive mutex
        std::unique_lock lock(gate_,std::try_to_lock);
        if(lock.owns_lock())discardPendingLocked();
    }
    struct GateGuard final {
        Dispatcher& owner;
        Dispatcher* prior=nullptr;
        std::unique_lock<Mutex> lock;
        explicit GateGuard(Dispatcher& value):owner(value),lock(value.gate_) {
            prior=gateOwner_;gateOwner_=&owner;
        }
        GateGuard(Dispatcher& value,std::try_to_lock_t):owner(value),lock(value.gate_,std::try_to_lock) {
            if(lock.owns_lock()){prior=gateOwner_;gateOwner_=&owner;}
        }
        ~GateGuard() {
            if(!lock.owns_lock())return;
            if(owner.closed_.load(std::memory_order_acquire))owner.discardPendingLocked();
            lock.unlock();gateOwner_=prior;
            if(owner.closed_.load(std::memory_order_acquire))owner.discardClosedPending();
        }
        bool owns_lock()const noexcept{return lock.owns_lock();}
    };

    // A dropped accepted callback seals this diagnostic owner. It cannot retire
    // a later job or launch a replacement worker: Closed never becomes Open.
    struct Callback final {
        Dispatcher* owner;
        std::atomic<bool> started{false},accepted{false};
        explicit Callback(Dispatcher* value) noexcept:owner(value){}
        ~Callback() {
            if(accepted.load(std::memory_order_acquire) && !started.load(std::memory_order_acquire)) {
                owner->counters_.callbackAbandoned.fetch_add(1,std::memory_order_relaxed);
                owner->close();
            }
        }
    };
    void publishCountsLocked() noexcept {
        counters_.pendingRows.store(count_,std::memory_order_relaxed);
        counters_.payloadBytes.store(bytes_,std::memory_order_relaxed);
        counters_.sinkActive.store(active_,std::memory_order_relaxed);
    }
    void discardPendingLocked() noexcept {
        counters_.pendingDiscarded.fetch_add(count_,std::memory_order_relaxed);
        while(count_) {
            bytes_-=pending_[head_]->charge;
            pending_[head_].reset();head_=(head_+1)%Capacity;--count_;
        }
        publishCountsLocked();
    }
    void drain() noexcept {
        for(;;) {
            std::optional<Item> item;
            {
                // Only the existing executor waits for this short data gate.
                // Producers/Close always use try_lock and never wait on sinks.
                GateGuard lock(*this);
                if(closed_.load(std::memory_order_acquire)) {
                    discardPendingLocked();scheduled_=false;return;
                }
                if(!count_) { scheduled_=false;return; }
                item.emplace(std::move(*pending_[head_]));pending_[head_].reset();
                head_=(head_+1)%Capacity;--count_;active_=true;publishCountsLocked();
            }
            try { sink_(item->data);counters_.sinkReturned.fetch_add(1,std::memory_order_relaxed); }
            catch(...) { counters_.sinkThrew.fetch_add(1,std::memory_order_relaxed); }
            const auto finishedCharge=item->charge;
            item.reset(); // Release retained data BEFORE returning its payload budget.
            {
                GateGuard lock(*this);
                bytes_-=finishedCharge;active_=false;publishCountsLocked();
            }
        }
    }
public:
    Dispatcher(std::function<bool(std::function<void()>)> submit,
               std::function<void(const Data&)> sink,
               std::size_t recordLimit=64*1024,std::size_t byteLimit=256*1024,
               void (*rejected)(const Data&,RejectedPost) noexcept=nullptr)
        :recordLimit_(recordLimit),byteLimit_(byteLimit),submit_(std::move(submit)),sink_(std::move(sink)),rejected_(rejected){}
    Dispatcher(const Dispatcher&)=delete;
    Dispatcher& operator=(const Dispatcher&)=delete;

    // charge is the caller's validated serialized payload+metadata byte charge,
    // NOT an assertion of exact heap usage. Active data remains charged too.
    bool tryPost(Data data,std::size_t charge,std::uint64_t nowTick) noexcept {
        counters_.offered.fetch_add(1,std::memory_order_relaxed);
        if(closed_.load(std::memory_order_acquire)) {
            observeRejected(data,RejectedPost::Closed);
            counters_.rejectedClosed.fetch_add(1,std::memory_order_relaxed);return false;
        }
        if(!charge || charge>recordLimit_ || charge>byteLimit_) {
            observeRejected(data,RejectedPost::Oversized);
            counters_.rejectedOversized.fetch_add(1,std::memory_order_relaxed);return false;
        }
        if(gateOwner_==this){observeRejected(data,RejectedPost::Busy);counters_.rejectedBusy.fetch_add(1,std::memory_order_relaxed);return false;}
        GateGuard lock(*this,std::try_to_lock);
        if(!lock.owns_lock()) { observeRejected(data,RejectedPost::Busy);counters_.rejectedBusy.fetch_add(1,std::memory_order_relaxed);return false; }
        if(closed_.load(std::memory_order_acquire)) {
            observeRejected(data,RejectedPost::Closed);
            discardPendingLocked();counters_.rejectedClosed.fetch_add(1,std::memory_order_relaxed);return false;
        }
        if(failedSubmission_ && (nowTick<failedAt_ || nowTick-failedAt_<1000)) {
            observeRejected(data,RejectedPost::Cooldown);
            counters_.rejectedCooldown.fetch_add(1,std::memory_order_relaxed);return false;
        }
        if(count_+std::size_t(active_)>=Capacity || charge>byteLimit_-bytes_) {
            observeRejected(data,RejectedPost::Full);
            counters_.rejectedFull.fetch_add(1,std::memory_order_relaxed);return false;
        }
        auto& posted=pending_[(head_+count_)%Capacity];
        posted.emplace(Item{std::move(data),charge});
        ++count_;bytes_+=charge;publishCountsLocked();
        if(!scheduled_) {
            scheduled_=true;bool accepted=false;
            auto rejection=RejectedPost::SubmissionNotAccepted;
            try {
                auto callback=std::make_shared<Callback>(this);
                accepted=submit_([this,callback] {
                    if(callback->started.exchange(true,std::memory_order_acq_rel))return;
                    drain();
                });
                callback->accepted.store(accepted,std::memory_order_release);
            } catch(const std::bad_alloc&) { accepted=false;rejection=RejectedPost::SubmissionAllocationException; }
            catch(const std::exception&) { accepted=false;rejection=RejectedPost::SubmissionStandardException; }
            catch(...) { accepted=false;rejection=RejectedPost::SubmissionUnknownException; }
            if(!accepted) {
                scheduled_=false;failedSubmission_=true;failedAt_=nowTick;
                counters_.submitFailed.fetch_add(1,std::memory_order_relaxed);
                observeRejected(posted->data,rejection);
                discardPendingLocked();return false;
            }
            failedSubmission_=false;
        }
        if(closed_.load(std::memory_order_acquire)) {
            observeRejected(posted->data,RejectedPost::Closed);
            discardPendingLocked();counters_.rejectedClosed.fetch_add(1,std::memory_order_relaxed);return false;
        }
        counters_.accepted.fetch_add(1,std::memory_order_relaxed);return true;
    }
    // Production calls this BEFORE the existing pool discards pending jobs.
    // An already active sink may remain blocked; no join or retry is performed.
    void close() noexcept {
        closed_.store(true,std::memory_order_release);
        discardClosedPending();
    }
    Snapshot snapshot() const noexcept {
        Snapshot s;
#define YMCC_DIAG_READ(name) s.name=counters_.name.load(std::memory_order_relaxed)
        YMCC_DIAG_READ(offered);YMCC_DIAG_READ(accepted);YMCC_DIAG_READ(rejectedBusy);YMCC_DIAG_READ(rejectedClosed);
        YMCC_DIAG_READ(rejectedFull);YMCC_DIAG_READ(rejectedOversized);YMCC_DIAG_READ(rejectedCooldown);
        YMCC_DIAG_READ(submitFailed);YMCC_DIAG_READ(sinkReturned);YMCC_DIAG_READ(sinkThrew);YMCC_DIAG_READ(callbackAbandoned);
        YMCC_DIAG_READ(pendingDiscarded);YMCC_DIAG_READ(pendingRows);YMCC_DIAG_READ(payloadBytes);YMCC_DIAG_READ(sinkActive);
#undef YMCC_DIAG_READ
        s.closed=closed_.load(std::memory_order_acquire);return s;
    }
};
} // namespace ymcc::inputHost::diagnostics
