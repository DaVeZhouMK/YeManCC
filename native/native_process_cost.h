#pragma once
#include <cstdint>
#include <limits>
#include "json.hpp"

// OS process counters observed by the existing virtual diagnostic 1 Hz lane.
// No input/controller APIs, timers, process handles or control decisions here.
namespace ymcc::nativeCost {
struct Counters {
    std::uint64_t tick = 0, processId = 0, creation100ns = 0;
    bool cpuKnown = false, memoryKnown = false;
    std::uint64_t kernel100ns = 0, user100ns = 0;
    std::uint64_t workingSetBytes = 0, privateCommitBytes = 0;
    std::uint32_t cpuError = 0, memoryError = 0;
};
struct Observation {
    Counters current{};
    bool rateKnown = false;
    const char* status = "cpu-unavailable";
    std::uint64_t beginTick = 0, intervalMs = 0;
    std::uint64_t kernelDelta100ns = 0, userDelta100ns = 0;
};
class Window {
    bool havePrevious_ = false;
    Counters previous_{};
public:
    static constexpr std::uint64_t maxContinuousGapMs = 3000;
    void reset() noexcept { havePrevious_ = false; }
    Observation observe(const Counters& current) noexcept {
        Observation row; row.current = current;
        if (!current.cpuKnown) { reset(); return row; }
        if (!havePrevious_) {
            previous_ = current; havePrevious_ = true;
            row.status = "baseline"; return row;
        }
        const auto previous = previous_; previous_ = current;
        row.beginTick = previous.tick;
        if (current.processId != previous.processId || current.creation100ns != previous.creation100ns) {
            row.status = "process-identity-changed"; return row;
        }
        if (current.tick <= previous.tick) {
            row.status = current.tick < previous.tick ? "clock-regressed" : "interval-not-positive";
            return row;
        }
        row.intervalMs = current.tick - previous.tick;
        if (row.intervalMs > maxContinuousGapMs) { row.status = "observation-gap"; return row; }
        if (current.kernel100ns < previous.kernel100ns || current.user100ns < previous.user100ns) {
            row.status = "counter-regressed"; return row;
        }
        row.kernelDelta100ns = current.kernel100ns - previous.kernel100ns;
        row.userDelta100ns = current.user100ns - previous.user100ns;
        row.rateKnown = true; row.status = "observed"; return row;
    }
};
inline nlohmann::json toJson(const Observation& row) {
    const auto& c = row.current;
    nlohmann::json result = {{"schemaVersion",1},{"pid",c.processId},{"sampleTick",c.tick},
        {"cpuCounterSource","GetProcessTimes"},{"cpuCounterUnit","100ns"},
        {"cpuCounterAvailable",c.cpuKnown},{"cpuRateAvailable",row.rateKnown},{"cpuStatus",row.status},
        {"memorySource","GetProcessMemoryInfo"},{"memoryAvailable",c.memoryKnown},
        {"scope","Whole Native process, including all its threads; not virtual-input-only, not Host or WebView child processes"}};
    if (c.cpuKnown) {
        result["processCreation100ns"] = c.creation100ns;
        result["kernelTotal100ns"] = c.kernel100ns;
        result["userTotal100ns"] = c.user100ns;
    } else result["cpuError"] = c.cpuError;
    if (row.rateKnown) {
        result["intervalBeginTick"] = row.beginTick;
        result["intervalMs"] = row.intervalMs;
        result["kernelDelta100ns"] = row.kernelDelta100ns;
        result["userDelta100ns"] = row.userDelta100ns;
        const double denominator = 10.0 * static_cast<double>(row.intervalMs);
        const double kernel = static_cast<double>(row.kernelDelta100ns) / denominator;
        const double user = static_cast<double>(row.userDelta100ns) / denominator;
        result["kernelCpuMsPerSec"] = kernel;
        result["userCpuMsPerSec"] = user;
        result["cpuMsPerSec"] = kernel + user; // Multi-threaded CPU can exceed 1000 ms/s.
    }
    if (c.memoryKnown) {
        result["workingSetBytes"] = c.workingSetBytes;
        result["privateCommitBytes"] = c.privateCommitBytes;
    } else result["memoryError"] = c.memoryError;
    return result;
}
} // namespace ymcc::nativeCost
