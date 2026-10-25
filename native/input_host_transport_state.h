#pragma once
namespace ymcc::input::transport {
struct Facts {
    bool hostProcessPresent = false;
    bool transportFault = false;
    bool pipeRetired = false;
    bool hello = false;
    bool pipeConnected = false;
    bool pipeNamePresent = false;
    bool runIdPresent = false;
    bool startupNoncePresent = false;
};
inline const char* unavailabilityReason(const Facts& f) noexcept {
    if (!f.hostProcessPresent) return "host-process-missing";
    if (f.transportFault) return "transport-fault-latched";
    if (f.pipeRetired) return "pipe-retired";
    if (!f.hello) return "hello-missing";
    if (!f.pipeNamePresent) return "pipe-name-missing";
    if (!f.runIdPresent) return "run-id-missing";
    if (!f.startupNoncePresent) return "startup-nonce-missing";
    if (!f.pipeConnected) return "pipe-not-connected";
    return "unknown";
}
}
