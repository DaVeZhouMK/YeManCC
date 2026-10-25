#pragma once

#include <cstdint>
#include <optional>

namespace ymcc::legion_motion_clock {

// Caller supplies a common uptime-ms domain: now64 and the unsigned 32 bits of
// the OS queue's message time. This is not a physical/firmware sample timestamp.
// Only the most recent compatible tick (age modulo 2^32) is considered. A short
// maxAgeMs rejects ordinary future/stale values; no window proves uniqueness
// against an event from an earlier complete 2^32-ms cycle. Windows/API-free.
inline constexpr std::optional<std::uint64_t> messageReceiptTick(
    std::uint64_t now64, std::uint32_t messageTime32, std::uint64_t maxAgeMs) {
    const std::uint32_t age32 = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(now64) - messageTime32);
    const std::uint64_t age = age32;
    if (age > now64 || age > maxAgeMs) return std::nullopt;
    return now64 - age;
}

// Zero receivedAt is never an epoch witness; epochStart itself may be zero.
// Equal uptime ticks cannot order messages on opposite sides of a cutover.
// Reject that uncertain tick; the next provably newer sample can resume input.
inline constexpr bool belongsToEpoch(std::uint64_t receivedAt, std::uint64_t epochStart) {
    return receivedAt != 0 && receivedAt > epochStart;
}

} // namespace ymcc::legion_motion_clock
