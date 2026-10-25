#pragma once
#include <string>
#include <string_view>

// Pure log classification only. This policy never admits, retries or stops a device.
namespace ymcc::logging {
enum class NativeLane { suppressed, event, detail };

constexpr NativeLane nativeLane(bool baseEvent, bool streamingRecord, bool detailedEnabled) noexcept {
    if (baseEvent && !streamingRecord) return NativeLane::event;
    return detailedEnabled ? NativeLane::detail : NativeLane::suppressed;
}

// Both records are streaming samples; neither may force a flush per BUS frame.
constexpr bool inputSampleKind(std::string_view kind) noexcept {
    return kind == "sample" || kind == "controller-family-imu";
}

// Repeated snapshots are detailed samples; first/change/new power generation
// are event evidence. Fixed instances only: no unbounded per-device key map.
struct SnapshotLogState {
    std::string signature;
    unsigned long long generation = 0;
    bool known = false;
    bool observe(std::string_view next, unsigned long long nextGeneration) {
        if (known && generation == nextGeneration && signature == next) return false;
        signature.assign(next); generation = nextGeneration; known = true;
        return true;
    }
};

// Generic device-tree notifications are samples unless they carry sleep intent.
// Arrival/removal messages stay factual events regardless of their frequency.
constexpr bool sleepDeviceTreeSample(unsigned long long code, bool userStandby, bool ageEligible) noexcept {
    return code == 7 /* DBT_DEVNODES_CHANGED */ && !userStandby && !ageEligible;
}

// Markers are explicit opt-ins, not mere file existence. No locale/config side effects.
constexpr bool explicitLogFlag(std::string_view value) noexcept {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; };
    while (!value.empty() && space(value.front())) value.remove_prefix(1);
    while (!value.empty() && space(value.back())) value.remove_suffix(1);
    if (value == "1") return true;
    if (value.size() != 7) return false;
    constexpr std::string_view enabled = "enabled";
    for (size_t i = 0; i < enabled.size(); ++i) {
        const char c = value[i] >= 'A' && value[i] <= 'Z' ? static_cast<char>(value[i] + ('a' - 'A')) : value[i];
        if (c != enabled[i]) return false;
    }
    return true;
}

constexpr bool detailNeedsImmediateFlush(std::string_view event, bool explicitFailure) noexcept {
    return explicitFailure || event.find("fault") != std::string_view::npos ||
        event.find("fail") != std::string_view::npos || event.find("error") != std::string_view::npos ||
        event.find("reject") != std::string_view::npos || event.find("timeout") != std::string_view::npos;
}

// Only category edges belong to the always-on lane; window values stay detailed.
struct BusRateLogState {
    unsigned flags = 0;
    bool observe(unsigned next) noexcept {
        if (flags == next) return false;
        flags = next;
        return true;
    }
};
}
