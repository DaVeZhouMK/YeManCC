#pragma once
#include <cstdint>
namespace ymcc {
struct ShortcutWindowVisibility {
    bool visible;
    bool iconic;
    bool cloakKnown = false;
    unsigned cloak = 0;
    bool restoreMaximized = false;

    bool presentable() const noexcept { return visible && !iconic && !(cloakKnown && cloak); }
};

// Never treat a hidden, minimized or known cloaked window as open. An open
// maximized/full-height window still toggles off without a foreground requirement.
inline bool summonPressHides(const ShortcutWindowVisibility& state, bool zoomed, bool fullHeight) {
    return state.presentable() && (zoomed || fullHeight);
}

inline constexpr std::uint64_t kDefaultSummonHoldMs = 250;
inline constexpr std::uint64_t kDefaultSummonRetryMs = 1000;
inline constexpr std::uint64_t kSummonIntentLifetimeMs = 3000;
// One chord owns one request. A false dispatch must not have shown or hidden
// anything; accepted requests keep their own presentation retries.
class DefaultSummonHold {
    using Tick = std::uint64_t;
public:
    enum class Result { None, Waiting, Resumed, Expired };
    struct Context {
        Tick power, source, rules, presentation;
        long long revision;
        bool operator==(const Context&) const = default;
    };
    void cancelWhileHeld() noexcept { phase = Spent; }
    void release() noexcept { phase = Idle; }
    template<class Dispatch>
    Result update(Tick now, const Context& current, Dispatch dispatch) {
        if (phase == Spent) return Result::None;
        if (phase == Idle) {
            phase = Holding;
            context = current;
            pressed = now;
            next = now + kDefaultSummonHoldMs;
            return Result::None;
        }
        if (!(context == current)) { cancelWhileHeld(); return Result::None; }
        if (now < next) return Result::None;
        const bool wasWaiting = phase == Waiting;
        const Tick elapsed = now - pressed;
        const Tick limit = kDefaultSummonHoldMs + kDefaultSummonRetryMs;
        // Consume before calling out to code that may pump window messages.
        cancelWhileHeld();
        // A late first tick for a still-current chord gets one attempt.
        if (elapsed > (wasWaiting ? limit : kSummonIntentLifetimeMs)) return Result::Expired;
        if (dispatch()) return wasWaiting ? Result::Resumed : Result::None;
        if (elapsed >= limit) return Result::Expired;
        phase = Waiting;
        const Tick deadline = pressed + limit;
        next = now + 50 < deadline ? now + 50 : deadline;
        return wasWaiting ? Result::None : Result::Waiting;
    }
private:
    enum Phase { Idle, Holding, Waiting, Spent } phase = Idle;
    Context context{};
    Tick pressed = 0, next = 0;
};
}
