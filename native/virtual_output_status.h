#pragma once
#include <string>

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
// Read-only presentation. These facts never grant or revoke input admission.
struct VirtualOutputFacts {
    bool enabled = false;
    bool observed = false;
    bool hostAlive = false;
    bool prepared = false;
    bool neutralized = false;
    bool frameAccepted = false;
    bool sourceLive = false;
    bool personaMatches = false;
    bool transportFault = false;
    bool faultHold = false;
    bool fallback = false;
    bool lifecycleBusy = false;
    bool recovering = false;
    bool suppressionDegraded = false;
};
struct VirtualOutputVerdict {
    const char* state;
    const char* reason;
};
inline VirtualOutputVerdict virtualOutputVerdict(const VirtualOutputFacts& f) {
    if (!f.enabled) return {"disabled", "not-requested"};
    if (f.faultHold) return {"suspected-failed", "backend-fault-hold"};
    if (f.fallback) return {"suspected-failed", "physical-fallback"};
    if (f.recovering) return {"recovering", "recovery-in-progress"};
    if (f.lifecycleBusy) return {"starting", "lifecycle-in-progress"};
    if (!f.observed) return {"unknown", "runtime-unconfirmed"};
    if (f.transportFault) return {"suspected-failed", "transport-fault"};
    if (!f.hostAlive) return {"suspected-failed", "host-not-alive"};
    if (!f.personaMatches) return {"suspected-failed", "persona-mismatch"};
    if (!f.prepared || !f.neutralized) return {"suspected-failed", "target-not-admitted"};
    if (f.suppressionDegraded) return {"suspected-failed", "suppression-degraded"};
    if (!f.sourceLive) return {"suspected-failed", "physical-source-unavailable"};
    if (!f.frameAccepted) return {"starting", "awaiting-first-frame"};
    return {"active", "current-session-frame-accepted"};
}
} // namespace ymcc
