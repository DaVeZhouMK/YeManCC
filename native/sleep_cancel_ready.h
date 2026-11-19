#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
namespace ymcc::sleeppower {
// A deny-only witness for one existing Native generation's cancellation Ready.
// It never grants power, changes generation, handles delivery or starts work.
class CancellationReady {
    std::atomic<std::uint64_t> revision_{0},generation_{0},through_{0},boundary_{0};
public:
    bool bind(std::uint64_t generation,std::uint64_t through,std::uint64_t boundary) noexcept {
        if(!generation)return false;
        auto revision=revision_.load(std::memory_order_acquire);
        if((revision&1)||revision>std::numeric_limits<std::uint64_t>::max()-2||
           !revision_.compare_exchange_strong(revision,revision+1,std::memory_order_acq_rel))return false;
        generation_.store(generation,std::memory_order_release);
        through_.store(through,std::memory_order_release);
        boundary_.store(boundary,std::memory_order_release);
        revision_.store(revision+2,std::memory_order_release);
        return true;
    }
    bool boundTo(std::uint64_t generation) const noexcept {
        const auto revision=revision_.load(std::memory_order_acquire);
        return revision && !(revision&1) && generation_.load(std::memory_order_acquire)==generation &&
            revision==revision_.load(std::memory_order_acquire);
    }
    // Caller must first admit a real final wake and its original time. This
    // replaces only this generation's previous deny, never ordinary power.
    bool renew(std::uint64_t generation,std::uint64_t through,std::uint64_t boundary) noexcept {
        return !boundTo(generation) || bind(generation,through,boundary);
    }
    bool allows(std::uint64_t generation,std::uint64_t entry,std::uint64_t boundary) const noexcept {
        const auto before=revision_.load(std::memory_order_acquire);
        if(!before)return true;
        if(before&1)return false;
        const auto boundGeneration=generation_.load(std::memory_order_acquire);
        const auto through=through_.load(std::memory_order_acquire);
        const auto boundBoundary=boundary_.load(std::memory_order_acquire);
        if(before!=revision_.load(std::memory_order_acquire))return false;
        return generation!=boundGeneration||(entry<=through&&boundary==boundBoundary);
    }
};
}
