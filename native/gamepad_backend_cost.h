#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

// Diagnostic counters only. No device reads, admission, retry or control state.
namespace ymcc::input::backendCost {
constexpr std::size_t kResultCodeCapacity = 8;
constexpr std::size_t kSourceCount = 4;
constexpr std::size_t kPurposeCount = 3;
constexpr std::size_t kXinputRouteCount = kSourceCount * kPurposeCount * 2;
struct ResultCount { std::uint32_t code{}; std::uint64_t n{}; };
struct Snapshot {
    std::uint64_t n{}, sumUs{}, maxUs{}, success{}, nonSuccess{};
    std::array<ResultCount, kResultCodeCapacity> resultCodes{};
    std::uint64_t otherCodeCount{};
    std::uint32_t lastOtherCode{};
};
class Aggregate {
    struct CodeBucket {
        // Lifetime assignment is retained across drains. code+1 represents all
        // uint32 results, including 0 and UINT32_MAX, without an empty collision.
        std::atomic<std::uint64_t> key{0}, n{0};
    };
    std::atomic<std::uint64_t> n_{0}, sumUs_{0}, maxUs_{0}, success_{0}, nonSuccess_{0};
    std::array<CodeBucket, kResultCodeCapacity> codes_{};
    std::atomic<std::uint64_t> otherCodeCount_{0};
    std::atomic<std::uint32_t> lastOtherCode_{0};
public:
    void record(std::uint64_t us, std::uint32_t code, bool success) noexcept {
        n_.fetch_add(1, std::memory_order_relaxed);
        sumUs_.fetch_add(us, std::memory_order_relaxed);
        auto mx = maxUs_.load(std::memory_order_relaxed);
        while (us > mx && !maxUs_.compare_exchange_weak(mx, us, std::memory_order_relaxed)) {}
        (success ? success_ : nonSuccess_).fetch_add(1, std::memory_order_relaxed);
        const std::uint64_t key = static_cast<std::uint64_t>(code) + 1;
        for (auto& bucket : codes_) {
            auto observed = bucket.key.load(std::memory_order_relaxed);
            if (observed == 0) {
                bucket.key.compare_exchange_strong(observed, key, std::memory_order_relaxed);
                if (observed == 0) observed = key;
            }
            if (observed == key) {
                // Publish the observed/initialized lifetime key to an acquiring
                // drain, including a first insertion racing that drain.
                bucket.n.fetch_add(1, std::memory_order_release);
                return;
            }
        }
        otherCodeCount_.fetch_add(1, std::memory_order_relaxed);
        lastOtherCode_.store(code, std::memory_order_relaxed);
    }
    // Independent atomic drains, not a coherent transaction. An in-flight
    // observation can straddle adjacent emissions; per-window sums may differ.
    Snapshot drain() noexcept {
        Snapshot s;
        s.n = n_.exchange(0, std::memory_order_acq_rel);
        s.sumUs = sumUs_.exchange(0, std::memory_order_acq_rel);
        s.maxUs = maxUs_.exchange(0, std::memory_order_acq_rel);
        s.success = success_.exchange(0, std::memory_order_acq_rel);
        s.nonSuccess = nonSuccess_.exchange(0, std::memory_order_acq_rel);
        for (std::size_t i = 0; i < codes_.size(); ++i) {
            // Read the lifetime key after draining n, so a first insertion
            // concurrent with this drain cannot label a counted code as empty.
            s.resultCodes[i].n = codes_[i].n.exchange(0, std::memory_order_acq_rel);
            s.resultCodes[i].code = static_cast<std::uint32_t>(
                codes_[i].key.load(std::memory_order_acquire) - 1);
        }
        s.otherCodeCount = otherCodeCount_.exchange(0, std::memory_order_acq_rel);
        s.lastOtherCode = lastOtherCode_.load(std::memory_order_relaxed);
        return s;
    }
};
struct Counters {
    std::array<Aggregate, kXinputRouteCount> xinput{};
    Aggregate system32, msiPoll, msiGetDeviceState;
    static constexpr std::size_t route(std::size_t source, std::size_t purpose, bool ordinal) noexcept {
        return (source * kPurposeCount + purpose) * 2 + (ordinal ? 1 : 0);
    }
};
template<class Clock = std::chrono::steady_clock> class Span {
    bool on_;
    typename Clock::time_point started_;
public:
    explicit Span(bool on) noexcept : on_(on), started_(on ? Clock::now() : typename Clock::time_point{}) {}
    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;
    template<class Gate> void finish(Aggregate& aggregate, std::uint32_t code, bool success, Gate&& gate) noexcept {
        if (!on_) return; // no clock, atomics, gate callback or allocation when off
        on_ = false;     // a span is recorded at most once
        if (!gate()) return; // no diagnostic accumulation after the gate closes
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started_).count();
        aggregate.record(elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0, code, success);
    }
};
} // namespace ymcc::input::backendCost
