#pragma once
#include <cstdint>
#include <string>
namespace ymcc::lossless {
struct Identity {
    std::uint32_t pid = 0;
    std::uint64_t created = 0;
    std::wstring path;
};
enum class ScanState { Absent, Exact, Unknown, Conflict };
struct Scan {
    ScanState state = ScanState::Unknown;
    Identity identity;
    std::uint32_t error = 0;
};
struct Result {
    bool ok = false, started = false, reused = false;
    Identity identity;
    std::string reason;
    std::uint32_t error = 0;
};
// Ports are bounded OS operations. No window activation, hotkeys, settings
// writes, delayed work or retries belong to this consumer.
template<class Ports> Result ensureRunning(Ports& ports) {
    Result result;
    if (!ports.ready()) { result.reason = "context-unavailable"; return result; }
    if (!ports.validate()) { result.reason = "invalid-executable"; result.error = ports.error(); return result; }
    if (!ports.lock()) { result.reason = "launch-busy"; result.error = ports.error(); return result; }
    struct Unlock { Ports& p; ~Unlock() { p.unlock(); } } unlock{ports};
    // A second snapshot catches an external arrival before creation. An
    // external launcher does not share our mutex, so its final race remains.
    for (unsigned observation = 0; observation != 2; ++observation) {
        if (!ports.ready()) { result.reason = "context-unavailable"; return result; }
        const auto scan = ports.scan();
        if (scan.state == ScanState::Unknown || scan.state == ScanState::Conflict) {
            result.reason = scan.state == ScanState::Unknown ? "process-identity-unknown" : "other-instance-present";
            result.error = scan.error;
            return result;
        }
        if (scan.state == ScanState::Exact) {
            if (!ports.ready() || !ports.matches(scan.identity)) { result.reason = "process-identity-changed"; return result; }
            result.ok = true; result.reused = true; result.identity = scan.identity;
            return result;
        }
    }
    if (!ports.ready()) { result.reason = "context-unavailable"; return result; }
    result = ports.start(); // Exactly one fixed-argument, non-activating request.
    if (result.ok && !ports.ready()) { result.ok = false; result.reason = "context-changed-after-start"; }
    return result;
}
} // namespace ymcc::lossless
