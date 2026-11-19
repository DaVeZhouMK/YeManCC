#pragma once
#include "steam_game_tracker_observation.h"
#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

// PRIVATE / UNBUILT. Completion metadata only, never source/action readiness.
namespace ymcc::steamgamefacts {
class TrackerOwner;
namespace tracker_detail { struct TrackerFlight; struct CompletionWatch; }

class TrackerCompletionHint final {
    friend class TrackerOwner;
    friend class TrackerCompletionSubscription;
    std::weak_ptr<tracker_detail::CompletionWatch> watch_;
    TrackerObservationKey key_;
    TrackerFlightReceipt receipt_;
    explicit TrackerCompletionHint(const std::shared_ptr<tracker_detail::CompletionWatch>&);
public:
    TrackerObservationKey key()const noexcept{return key_;}
    const TrackerFlightReceipt& receipt()const noexcept{return receipt_;}
    // Cancellation/lifetime ONLY. Consumer must separately re-read/current-check.
    bool subscribed()const noexcept;
};

// Must ONLY copy a hint into a bounded queue / PostMessage and return admission.
// NO re-read, consumer callback/lock, waiting, retries, source IO or action gate.
// Caller can hold refreshMx. No callback may capture a subscription strongly.
using TrackerCompletionSink=std::function<bool(const TrackerCompletionHint&)>;
enum class TrackerCompletionDelivery { Waiting, Ready, Offering, Enqueued, Rejected, SinkFailed, Cancelled };
enum class TrackerCompletionWatchStatus { Watching, ReReadNow, InvalidWitness, Capacity };

namespace tracker_detail {
struct CompletionWatch final {
    const TrackerObservationKey key;
    const std::uint64_t sequence,factVersion;
    const TrackerCompletionSink sink;
    std::atomic<bool> cancelled{false},released{false};
    std::atomic<TrackerCompletionDelivery> delivery{TrackerCompletionDelivery::Waiting};
    TrackerFlightReceipt receipt; // Written once under owner mutex, published by released.
    CompletionWatch(TrackerObservationKey fact,std::uint64_t serial,std::uint64_t version,TrackerCompletionSink enqueue)
        :key(std::move(fact)),sequence(serial),factVersion(version),sink(std::move(enqueue)){}
};
}
inline TrackerCompletionHint::TrackerCompletionHint(const std::shared_ptr<tracker_detail::CompletionWatch>& watch)
    :watch_(watch),key_(watch->key),receipt_(watch->receipt){}
inline bool TrackerCompletionHint::subscribed()const noexcept {
    const auto watch=watch_.lock();return watch&&!watch->cancelled.load(std::memory_order_acquire);
}

class TrackerCompletionSubscription final {
    friend class TrackerOwner;
    std::shared_ptr<tracker_detail::CompletionWatch> watch_;
    explicit TrackerCompletionSubscription(std::shared_ptr<tracker_detail::CompletionWatch> watch):watch_(std::move(watch)){}
public:
    TrackerCompletionSubscription()=default;
    TrackerCompletionSubscription(const TrackerCompletionSubscription&)=delete;
    TrackerCompletionSubscription& operator=(const TrackerCompletionSubscription&)=delete;
    TrackerCompletionSubscription(TrackerCompletionSubscription&& other)noexcept:watch_(std::move(other.watch_)){}
    TrackerCompletionSubscription& operator=(TrackerCompletionSubscription&& other)noexcept {
        if(this!=&other){reset();watch_=std::move(other.watch_);}return *this;
    }
    ~TrackerCompletionSubscription(){reset();}
    explicit operator bool()const noexcept{return watch_&&!watch_->cancelled.load(std::memory_order_acquire);}
    // Nonblocking. An already entered sink can finish; queued hints become dead.
    void cancel()noexcept{if(watch_)watch_->cancelled.store(true,std::memory_order_release);}
    void reset()noexcept{cancel();watch_.reset();}
    TrackerCompletionDelivery delivery()const noexcept {
        if(!watch_||watch_->cancelled.load(std::memory_order_acquire))return TrackerCompletionDelivery::Cancelled;
        return watch_->delivery.load(std::memory_order_acquire);
    }
    std::optional<TrackerCompletionHint> hint()const noexcept {
        if(!watch_||!watch_->released.load(std::memory_order_acquire)||watch_->cancelled.load(std::memory_order_acquire))return {};
        return TrackerCompletionHint(watch_);
    }
};
struct TrackerCompletionIssue final {
    const TrackerCompletionWatchStatus status;
    TrackerCompletionSubscription subscription;
    explicit TrackerCompletionIssue(TrackerCompletionWatchStatus value,TrackerCompletionSubscription watch={})
        :status(value),subscription(std::move(watch)){}
};
}
