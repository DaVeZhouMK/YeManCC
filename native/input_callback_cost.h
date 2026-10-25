#pragma once
#include <cstdint>

// Diagnostic ownership only. No timers, locks, allocation or controller state.
namespace ymcc::input::callbackCost {
using Value = std::uint64_t;
enum class Source : unsigned { multimedia, waitableFallback, absent, count };
enum class Reason : unsigned {
    valid, noScope, startDisabled, endDisabled, wrongThread, backwardsElapsed,
    startCyclesUnavailable, endCyclesUnavailable, backwardsCycles, count
};
inline constexpr const char* sourceNames[] = {"multimedia", "waitable-fallback", "absent"};
inline constexpr const char* reasonNames[] = {
    "valid", "no-scope", "start-disabled", "end-disabled", "wrong-thread",
    "backwards-elapsed", "start-cycles-unavailable", "end-cycles-unavailable", "backwards-cycles"
};
struct CycleSample { bool known = false; Value value = 0; };
struct Context {
    bool enabled = false;
    Source source = Source::absent;
    std::uint32_t threadId = 0;
    Value elapsedStartUs = 0;
    CycleSample cyclesStart{};
};
inline thread_local const Context* activeContext = nullptr;

class Scope final {
    Context context_{};
    const Context* previous_;
public:
    template<class ReadElapsed, class ReadCycles, class ReadThread>
    Scope(bool enabled, Source source, ReadElapsed readElapsed,
          ReadCycles readCycles, ReadThread readThread)
        : previous_(activeContext) {
        context_.enabled = enabled;
        context_.source = source;
        if (enabled) {
            context_.elapsedStartUs = readElapsed();
            context_.cyclesStart = readCycles();
            context_.threadId = readThread();
        }
        // Install a disabled scope too: an enabled outer span must never leak in.
        activeContext = &context_;
    }
    ~Scope() { activeContext = previous_; }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    Scope(Scope&&) = delete;
    Scope& operator=(Scope&&) = delete;
};
struct Measurement {
    Source source = Source::absent;
    Reason elapsedReason = Reason::noScope;
    Reason cyclesReason = Reason::noScope;
    Value elapsedUs = 0;
    Value cycles = 0;
};
inline Reason classifyCycles(CycleSample start, CycleSample end) {
    if (!start.known) return Reason::startCyclesUnavailable;
    if (!end.known) return Reason::endCyclesUnavailable;
    if (end.value < start.value) return Reason::backwardsCycles;
    return Reason::valid;
}
inline Measurement measure(const Context* context, bool endEnabled,
                           std::uint32_t endThreadId, Value endUs, CycleSample endCycles) {
    Measurement m;
    if (context) m.source = context->source;
    const auto rejectBoth = [&m](Reason reason) {
        m.elapsedReason = reason;
        m.cyclesReason = reason;
    };
    if (!endEnabled) { rejectBoth(Reason::endDisabled); return m; }
    if (!context) return m;
    if (!context->enabled) { rejectBoth(Reason::startDisabled); return m; }
    if (context->threadId != endThreadId) { rejectBoth(Reason::wrongThread); return m; }
    if (endUs < context->elapsedStartUs) m.elapsedReason = Reason::backwardsElapsed;
    else { m.elapsedReason = Reason::valid; m.elapsedUs = endUs - context->elapsedStartUs; }
    if (!context->cyclesStart.known) m.cyclesReason = Reason::startCyclesUnavailable;
    else if (!endCycles.known) m.cyclesReason = Reason::endCyclesUnavailable;
    else if (endCycles.value < context->cyclesStart.value) m.cyclesReason = Reason::backwardsCycles;
    else { m.cyclesReason = Reason::valid; m.cycles = endCycles.value - context->cyclesStart.value; }
    return m;
}
} // namespace ymcc::input::callbackCost
