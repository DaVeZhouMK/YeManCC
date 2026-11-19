#pragma once
#include <cstdint>

namespace ymcc::steamlink {
// This hint is for the active link's diagnostic snapshot, never for recovery.
template<class Window>
class DiagnosticWindowObservation {
    static constexpr std::uint64_t rescanMs=30000;
    Window window_{};
    std::uint64_t scanAt_=0, powerGeneration_=0;
public:
    void invalidate() noexcept { window_={}; }

    template<class Find, class Valid>
    Window observe(std::uint64_t now, std::uint64_t power, bool notified, Find find, Valid valid) {
        if(!notified && window_ && power==powerGeneration_ && now>=scanAt_ && now-scanAt_<rescanMs && valid(window_))
            return window_;
        invalidate();
        const auto found=find();
        if(found && valid(found)){
            window_=found;
            scanAt_=now;
            powerGeneration_=power;
        }
        return window_;
    }
};
} // namespace ymcc::steamlink
