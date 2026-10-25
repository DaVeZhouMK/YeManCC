#pragma once
#include <string>
#include <cstdint>

namespace ymcc {
// Three-state readback of the actual transaction targets, not every diagnostic container.
inline int virtualOutputSuppressionVerdict(bool readback, bool targetsRequested,
        bool targetsPresent, bool cloakOn, bool appsRequired, bool appsPresent,
        const std::string& inverse) {
    if (!readback || !targetsRequested) return 0;
    if (!targetsPresent || !cloakOn || (appsRequired && !appsPresent) || inverse == "on") return 2;
    if (appsRequired && appsPresent && inverse == "off") return 1;
    return 0;
}
// Read-only evidence from an existing canonical read; never authorizes input.
struct VirtualOutputSourceWindow {
    std::uint64_t readAttemptSequence = 0;
    std::uint64_t readStartedTick = 0, readCompletedTick = 0, readPowerGeneration = 0;
    unsigned readTopologyGeneration = 0;
    bool readContextStable = false;
};
// A freshness bound only, matching the existing owned-return observation age.
// No timer is started and no packet-number change is required for a stationary pad.
inline constexpr std::uint64_t kVirtualOutputSourceWindowMaxAgeMs = 1500;
inline bool virtualOutputSourceCurrent(const VirtualOutputSourceWindow& source,
        std::uint64_t now, std::uint64_t powerGeneration, unsigned topologyGeneration) noexcept {
    return source.readAttemptSequence != 0 && source.readContextStable && powerGeneration != 0 &&
        source.readPowerGeneration == powerGeneration && source.readTopologyGeneration == topologyGeneration &&
        source.readStartedTick != 0 && source.readCompletedTick >= source.readStartedTick &&
        source.readCompletedTick <= now && now - source.readStartedTick <= kVirtualOutputSourceWindowMaxAgeMs;
}
// Read-only presentation. These facts never grant or revoke input admission.
struct VirtualOutputFacts {
    bool enabled = false;
    bool observed = false;
    bool hostAlive = false;
    bool prepared = false;
    bool neutralized = false;
    bool frameAccepted = false;
    bool sourceLive = false;
    bool sourceCurrent = false; // canonical read-window evidence, independent of source admission
    bool personaMatches = false;
    bool transportFault = false;
    bool faultHold = false;
    bool fallback = false;
    bool lifecycleBusy = false;
    bool recovering = false;
    bool suppressionDegraded = false;
    // Actual backend transaction work, including an old host or bound generation.
    // A terminal fault/fallback must still win over this switch signal.
    bool transitionPending = false;
    // MSI virtual-OFF mode-return debt only; unrelated recovery is not a return.
    bool returnPending = false;
    bool returnFailed = false;
};
struct VirtualOutputVerdict {
    const char* state;
    const char* reason;
};
inline VirtualOutputVerdict virtualOutputVerdict(const VirtualOutputFacts& f) {
    if (!f.enabled) {
        // Active return work wins over a stale failure; clearing pending alone
        // cannot hide an unconfirmed return. The producer clears failure on match.
        if (f.returnPending) return {"recovering", "physical-return-pending"};
        if (f.returnFailed) return {"suspected-failed", "physical-return-unconfirmed"};
        return {"disabled", "not-requested"};
    }
    if (f.faultHold) return {"suspected-failed", "backend-fault-hold"};
    if (f.fallback) return {"suspected-failed", "physical-fallback"};
    if (f.recovering) return {"recovering", "recovery-in-progress"};
    if (f.transitionPending) return {"starting", "transition-pending"};
    if (f.lifecycleBusy) return {"starting", "lifecycle-in-progress"};
    if (!f.observed) return {"unknown", "runtime-unconfirmed"};
    if (f.transportFault) return {"suspected-failed", "transport-fault"};
    if (!f.hostAlive) return {"suspected-failed", "host-not-alive"};
    if (!f.personaMatches) return {"suspected-failed", "persona-mismatch"};
    if (!f.prepared || !f.neutralized) return {"suspected-failed", "target-not-admitted"};
    if (f.suppressionDegraded) return {"suspected-failed", "suppression-degraded"};
    if (!f.sourceCurrent) return {"unknown", "physical-source-unconfirmed"};
    if (!f.sourceLive) return {"suspected-failed", "physical-source-unavailable"};
    if (!f.frameAccepted) return {"starting", "awaiting-first-frame"};
    return {"active", "current-session-frame-accepted"};
}
} // namespace ymcc
