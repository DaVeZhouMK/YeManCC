#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace ymcc::radial {
class ReleaseGate {
    struct State {
        std::uint64_t power = 0;
        std::uint64_t deadline = 0;
    };
    static constexpr auto active = (std::numeric_limits<std::uint64_t>::max)();
    std::atomic<State> state{State{}};
public:
    static constexpr std::uint64_t returnMs = 750;
    void begin(std::uint64_t power, std::uint64_t now) noexcept {
        if (!power || now >= active - returnMs) return;
        state.store(State{power, active}, std::memory_order_release);
    }
    void end(std::uint64_t power, std::uint64_t now) noexcept {
        auto previous = state.load(std::memory_order_acquire);
        while (previous.power == power && previous.deadline == active) {
            const auto next = now < active - returnMs ? State{power, now + returnMs} : State{};
            if (state.compare_exchange_weak(previous, next, std::memory_order_acq_rel)) return;
        }
    }
    void invalidate() noexcept { state.store(State{}, std::memory_order_release); }
    bool owns(std::uint64_t power, std::uint64_t now) noexcept {
        auto previous = state.load(std::memory_order_acquire);
        while (previous.power == power && previous.deadline) {
            if (previous.deadline == active || (now < previous.deadline && previous.deadline - now <= returnMs)) return true;
            if (state.compare_exchange_weak(previous, State{}, std::memory_order_acq_rel)) return false;
        }
        return false;
    }
    void observe(std::uint64_t power, std::uint64_t now, bool neutral, bool fresh = true) noexcept {
        auto previous = state.load(std::memory_order_acquire);
        while (previous.power == power && previous.deadline && previous.deadline != active) {
            if (!(neutral && fresh) && now < previous.deadline && previous.deadline - now <= returnMs) return;
            if (state.compare_exchange_weak(previous, State{}, std::memory_order_acq_rel)) return;
        }
    }
};
}
