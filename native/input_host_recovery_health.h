#pragma once
#include <cstdint>
#include <limits>
#include "virtual_output_status.h"

namespace ymcc::input::recoveryHealth {
using Tick = std::uint64_t;
struct Context {
    Tick host = 0, power = 0, intent = 0;
    unsigned topology = 0, identity = 0;
    bool allowed = false;
    bool operator==(const Context&) const = default;
};
// Cached reader identity is health evidence, never device-write authority.
enum class Route : unsigned { Unspecified, XInput, MsiDinput };
inline const char* routeName(Route route) noexcept {
    switch (route) {
    case Route::XInput: return "xinput";
    case Route::MsiDinput: return "msi-dinput";
    case Route::Unspecified: return "unspecified";
    }
    return "unknown";
}
struct Source {
    Tick sequence = 0, started = 0, completed = 0, power = 0;
    unsigned topology = 0, identity = 0;
    bool live = false, stable = false;
    Tick continuity = 0;
    Route route = Route::Unspecified;
    Tick incarnation = 0;
};
inline bool sameSource(const Source& a, const Source& b) noexcept {
    return a.route == b.route && a.incarnation == b.incarnation;
}
// Reuse the canonical source-window age, not an ACK interval or packet change.
inline bool fresh(const Source& s, Tick now, Tick power, unsigned topology) noexcept {
    return ymcc::virtualOutputSourceCurrent({s.sequence,s.started,s.completed,s.power,s.topology,s.stable},
        now,power,topology);
}
// The canonical producer stamps continuity under its existing snapshot mutex.
// A reader stall is retained even if it resumes before maintenance next runs;
// healthy intermediate reads survive normal output deduplication.
inline Tick nextContinuity(const Source& previous, const Source& current, Tick now) noexcept {
    constexpr auto exhausted = std::numeric_limits<Tick>::max();
    const bool continuous = current.live && previous.live && current.identity == previous.identity &&
        sameSource(previous, current) &&
        fresh(current,now,current.power,current.topology) &&
        fresh(previous,current.started,current.power,current.topology);
    if (continuous && previous.continuity && previous.continuity != exhausted)
        return previous.continuity;
    return previous.continuity >= exhausted-1 ? exhausted : previous.continuity+1;
}
// Pure evidence window. The existing Host mutex owns it. Wall-clock maintenance
// cannot extend coverage; only an accepted receipt for a fresh read can do so.
class Window {
    Context context_{};
    Tick since_ = 0, through_ = 0, ackSequence_ = 0;
    Tick observedSequence_ = 0, observedCompleted_ = 0;
    bool haveContext_ = false;
    Tick continuity_ = 0;
    Route route_ = Route::Unspecified;
    Tick incarnation_ = 0;
    static bool valid(const Context& c, const Source& s, Tick now) noexcept {
        return c.allowed && c.host && c.power && s.continuity &&
            s.continuity != std::numeric_limits<Tick>::max() && fresh(s,now,c.power,c.topology) &&
            s.live && s.stable && s.completed >= s.started &&
            s.power == c.power && s.topology == c.topology && s.identity == c.identity;
    }
public:
    void clear() noexcept { *this = Window{}; }
    bool observe(const Context& c, const Source& s, Tick now) noexcept {
        if (!valid(c, s, now)) { clear(); return false; }
        if (!haveContext_ || !(c == context_) || s.continuity != continuity_ ||
            s.route != route_ || s.incarnation != incarnation_) {
            clear(); context_ = c; haveContext_ = true; continuity_ = s.continuity;
            route_ = s.route; incarnation_ = s.incarnation;
        }
        if (s.sequence < observedSequence_ || s.completed < observedCompleted_) {
            clear(); return false;
        }
        observedSequence_ = s.sequence; observedCompleted_ = s.completed;
        return true;
    }
    void accepted(const Context& request, const Source& read,
                  const Context& current, const Source& latest, Tick now) noexcept {
        if (!observe(current, latest, now)) return;
        if (!(request == current) || !valid(request, read, now) || !sameSource(read, latest) ||
            read.continuity != latest.continuity ||
            read.sequence > latest.sequence || read.completed > latest.completed) {
            clear(); return;
        }
        // Reusing a sample or deduplicating a frame is not a disconnect, but
        // cannot manufacture a later source/receipt coverage endpoint.
        if (read.sequence <= ackSequence_) return;
        if (read.completed < through_) { clear(); return; }
        if (!since_) since_ = read.completed;
        through_ = read.completed; ackSequence_ = read.sequence;
    }
    Route route() const noexcept { return route_; }
    Tick incarnation() const noexcept { return incarnation_; }
    Tick continuity() const noexcept { return continuity_; }
    Tick since() const noexcept { return since_; }
    Tick through() const noexcept { return through_; }
    bool complete(Tick duration) const noexcept {
        return since_ && through_ >= since_ && through_ - since_ >= duration;
    }
    void settled() noexcept { since_ = through_; }
};
} // namespace ymcc::input::recoveryHealth
