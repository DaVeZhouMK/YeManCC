#pragma once
#include <cstdint>
#include <string>

// Passive failure evidence only. No I/O, retries, timing changes or lifecycle
// decisions. The caller supplies facts already collected by the transport.
namespace ymcc::msi::hostTransportEvidence {
struct Facts {
    bool msiFamily = false;
    bool pipeAvailable = false, connected = false, written = false, read = false;
    bool hostAlive = false;
    std::uint32_t error = 0, timeoutMs = 0, responseBytes = 0;
    std::uint64_t sequence = 0, epoch = 0, writeElapsedUs = 0, readElapsedUs = 0;
    std::string command, requestId, runId, hostInstanceId;
};
inline const char* boundary(const Facts& f) noexcept {
    if ((f.connected && !f.pipeAvailable) || (f.written && !f.connected) ||
        (f.read && !f.written)) return "inconsistent-transport-facts";
    if (f.read) return "none";
    if (!f.pipeAvailable) return "pipe-unavailable";
    if (!f.connected) return "client-connect";
    if (!f.written) return "request-write";
    return "response-read";
}
template<class Json> Json make(const Facts& f) {
    // A successful response and other families produce no additional event.
    if (!f.msiFamily || f.read) return Json();
    return Json{{"version", 1}, {"command", f.command}, {"requestId", f.requestId},
        {"runId", f.runId}, {"epoch", f.epoch}, {"sequence", f.sequence},
        {"hostInstanceId", f.hostInstanceId}, {"hostAlive", f.hostAlive},
        {"failureBoundary", boundary(f)}, {"pipeAvailable", f.pipeAvailable},
        {"connected", f.connected}, {"requestWritten", f.written},
        {"responseReadAttempted", f.written}, {"responseReadSucceeded", f.read},
        {"responseBytes", f.responseBytes}, {"error", f.error},
        {"errorKnown", f.error != 0}, {"timeoutMsPerOperation", f.timeoutMs},
        {"writeElapsedUs", f.writeElapsedUs}, {"readElapsedUs", f.readElapsedUs},
        {"hostExecutionStage", "unobserved"}, {"steamConsumption", "unobserved"},
        {"evidenceScope", "native-pipe-boundary-only"}};
}
} // namespace ymcc::msi::hostTransportEvidence
