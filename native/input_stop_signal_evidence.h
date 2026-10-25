#pragma once
#include <cstdint>
#include <string>
#include "json.hpp"

// Diagnostic state only. The caller owns g_inputHostMx. No clocks, input APIs,
// timers or control decisions; a new stop replaces the old bounded observation.
namespace ymcc::stopSignalEvidence {
class Tracker {
    std::uint64_t serial_ = 0, id_ = 0, started_ = 0, completed_ = 0;
    bool complete_ = false;
    nlohmann::json identity_;
public:
    static constexpr std::uint64_t followupMs = 10000;
    void reset() noexcept { id_ = 0; complete_ = false; }
    std::uint64_t begin(std::uint64_t now, const std::string& run, std::uint64_t epoch,
                        std::uint64_t hostPid, const std::string& persona,
                        const std::string& reason, std::uint64_t powerGeneration) {
        reset();
        // Construct before publishing id, so allocation failure cannot reuse an old stop.
        identity_ = {{"stopRunId",run},{"stopEpoch",epoch},{"stopHostPid",hostPid},
            {"stopPersona",persona},{"stopReason",reason},{"stopPowerGeneration",powerGeneration}};
        if (++serial_ == 0) ++serial_;
        id_ = serial_; started_ = now; completed_ = 0;
        return id_;
    }
    void finish(std::uint64_t id, std::uint64_t now) noexcept {
        if (!id || id != id_) return;
        if (now < started_) { reset(); return; }
        completed_ = now; complete_ = true;
    }
    nlohmann::json context(std::uint64_t id, std::uint64_t now) const {
        if (!id || id != id_ || now < started_) return nullptr;
        auto row = identity_;
        row.update({{"stopSerial",id_},{"stopBeginTick",started_},{"observationTick",now},
            {"elapsedSinceStopBeginMs",now-started_},{"localCleanupFinished",complete_},
            {"localCleanupFinishedTick",complete_ ? nlohmann::json(completed_) : nlohmann::json(nullptr)},
            {"elapsedSinceCleanupMs",complete_ && now>=completed_ ? nlohmann::json(now-completed_) : nlohmann::json(nullptr)},
            {"consumerReleaseProven",false}});
        return row;
    }
    nlohmann::json recent(std::uint64_t now, const std::string& currentRun,
                          std::uint64_t currentEpoch) const {
        if (!id_ || !complete_ || now<completed_ || now-completed_>followupMs) return nullptr;
        auto row = context(id_,now);
        row["sameStoppedSessionStillBound"] = !currentRun.empty() &&
            currentRun == identity_.at("stopRunId").get<std::string>() &&
            currentEpoch == identity_.at("stopEpoch").get<std::uint64_t>();
        row["currentHostRunId"] = currentRun; row["currentHostEpoch"] = currentEpoch;
        row["followupLimitMs"] = followupMs;
        row["scope"] = "Previous local stop context only; current input may belong to a new session, a physical device or another producer";
        return row;
    }
};
} // namespace ymcc::stopSignalEvidence
