#pragma once

// Process-start only. Power/resume and runtime controller ownership remain
// with their existing lifecycle; this gate is just a reveal prerequisite.
namespace ymcc {
enum class StartupInputState { Initializing, Waiting, Skipped, Ready, Failed };
inline bool startupInputSettled(StartupInputState state) {
    return state == StartupInputState::Skipped || state == StartupInputState::Ready ||
           state == StartupInputState::Failed;
}
inline const char* startupInputStateName(StartupInputState state) {
    switch (state) {
    case StartupInputState::Initializing: return "initializing";
    case StartupInputState::Waiting: return "waiting";
    case StartupInputState::Skipped: return "disabled";
    case StartupInputState::Ready: return "ready";
    case StartupInputState::Failed: return "failed";
    }
    return "unknown";
}
inline StartupInputState startupInputDecision(bool enabled, bool busy,
    bool attempted, bool startSucceeded, bool healthy, bool admissionExpired) {
    if (busy) return StartupInputState::Waiting;
    if (!enabled) return StartupInputState::Skipped;
    if (!attempted) return admissionExpired ? StartupInputState::Failed : StartupInputState::Waiting;
    return startSucceeded && healthy ? StartupInputState::Ready : StartupInputState::Failed;
}
inline bool startupFrontendUsable(bool routeReady, bool degraded, bool fallbackVisible) {
    return routeReady || (degraded && fallbackVisible);
}
inline bool startupMayReveal(bool navigationReady, bool frontendUsable, StartupInputState input) {
    return navigationReady && frontendUsable && startupInputSettled(input);
}
}
