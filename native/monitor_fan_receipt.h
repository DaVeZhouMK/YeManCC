#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include "../deps/json/json.hpp"
namespace ymcc::monitorfan {
struct Receipt { bool enabled = false; bool valid = false; double percent = 0; long long ts = 0; };
// One passive cache, written only from existing authenticated Fan HTTP responses.
// No device APIs, HTTP, locks, retry loops or allocation on the monitor read path.
class Cache {
    std::atomic<std::uint64_t> epoch_{1}, packet_{0}, packetEpoch_{0};
    std::atomic_flag writing_ = ATOMIC_FLAG_INIT;
public:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "monitor fan cache must be lock-free");
    std::uint64_t epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }
    std::uint64_t invalidate() noexcept { return epoch_.fetch_add(1, std::memory_order_acq_rel) + 1; }
    Receipt sample(long long now) const noexcept {
        const auto generation = epoch();
        const auto version = packetEpoch_.load(std::memory_order_acquire);
        const auto bits = packet_.load(std::memory_order_acquire);
        if (version != generation || epoch() != generation || !bits) return {};
        const auto ts = static_cast<long long>(bits >> 10);
        const auto duty = static_cast<unsigned>(bits & 1023);
        if (ts <= 0 || ts > now || now - ts >= 6000 || duty > 1000) return {};
        return {true, true, duty / 10.0, ts};
    }
    void observe(std::uint64_t generation, unsigned status, const nlohmann::json& response, long long now) noexcept {
        if (generation != epoch()) return; // A close/sleep/new intent retired this request.
        try {
            if (status < 200 || status >= 300 || !response.is_object() || !response.contains("ok") || response["ok"] != true) { invalidate(); return; }
            const nlohmann::json* value = nullptr;
            if (response.contains("monitorFan")) value = &response["monitorFan"];
            else if (response.contains("state") && response["state"].is_object() && response["state"].contains("monitorFan")) value = &response["state"]["monitorFan"];
            // Lease-only responses from acquire-control cannot invent enabled telemetry.
            if (!value) return;
            if (!value->is_object() || !value->contains("enabled") || (*value)["enabled"] != true ||
                !value->contains("source") || (*value)["source"] != "ymcc-fan-target" ||
                !value->contains("percent") || !(*value)["percent"].is_number() ||
                !value->contains("sampledAt") || !(*value)["sampledAt"].is_number_integer()) { invalidate(); return; }
            const auto ts = (*value)["sampledAt"].get<long long>();
            const auto percent = (*value)["percent"].get<double>();
            if (!std::isfinite(percent) || percent < 0 || percent > 100 || ts <= 0 || ts > now || now - ts >= 6000 || ts >= (1LL << 48)) { invalidate(); return; }
            if (writing_.test_and_set(std::memory_order_acquire)) return; // bounded contention: drop, never wait
            if (generation == epoch()) {
                const auto prior = packet_.load(std::memory_order_relaxed);
                if (packetEpoch_.load(std::memory_order_relaxed) != generation || static_cast<long long>(prior >> 10) <= ts) {
                    packet_.store((static_cast<std::uint64_t>(ts) << 10) | static_cast<unsigned>(std::round(percent * 10)), std::memory_order_release);
                    packetEpoch_.store(generation, std::memory_order_release);
                }
            }
            writing_.clear(std::memory_order_release);
        } catch (...) { invalidate(); }
    }
};
inline bool isMutation(const std::string& path) noexcept {
    return path == "/api/enable" || path == "/api/preset" || path == "/api/disable" || path == "/api/cancel-control-intent" ||
        path == "/api/release-control" || path == "/api/restore" || path == "/api/close" || path == "/api/shutdown" ||
        path == "/api/parent-exit" || path == "/api/suspend" || path == "/api/resume" || path == "/api/open" || path == "/api/open-events";
}
} // namespace ymcc::monitorfan
