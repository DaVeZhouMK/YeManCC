#pragma once
#include <cstdint>
#include <limits>

// User-selected elapsed-time fallback, not a physical high-power classifier.
// One deadline per original button intent; no polling, CPU threshold or DRIPS requirement.
namespace ymcc::sleeptimeout {
struct Window {
    static constexpr std::uint64_t delayMs=240000;
    static constexpr std::uint64_t unitsPerMs=10000;
    std::uint64_t origin=0, lastOrigin=0;
    bool waiting=false, requested=false;
    bool active() const noexcept { return waiting||requested; }
    bool arm(std::uint64_t intent,std::uint64_t now) noexcept {
        if(!intent||intent<=lastOrigin||now<intent||
           intent>(std::numeric_limits<std::uint64_t>::max)()-delayMs*unitsPerMs)return false;
        origin=lastOrigin=intent;waiting=true;requested=false;return true;
    }
    std::uint64_t remainingMs(std::uint64_t now) const noexcept {
        if(!waiting||now<origin)return 0;
        const auto elapsed=(now-origin)/unitsPerMs;
        return elapsed>=delayMs?0:delayMs-elapsed;
    }
    bool due(std::uint64_t now) const noexcept {
        return waiting&&now>=origin&&(now-origin)/unitsPerMs>=delayMs;
    }
    bool request(std::uint64_t now) noexcept {
        if(!due(now))return false;
        waiting=false;requested=true;return true;
    }
    void retire() noexcept {waiting=requested=false;}
};
}
