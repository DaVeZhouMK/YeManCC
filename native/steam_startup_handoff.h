#pragma once
#include "steam_game_tracker_observation.h"
#include "steam_process_parent_evidence.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace ymcc::steamgamefacts {
class TrackerOwner;

// PRIVATE, SOURCE ONLY. Owner-retained historical launch evidence, including
// when the captured launcher has exited. This does not establish a current
// account or publisher, native state, selected game, or action authority.
// Retains only this line's bytes and metadata, never the complete source buffer.
// TrackerOwner revokes retained_ when pruning a record within a live epoch.
class StartupLaunchEvidence final {
    friend class TrackerOwner;
    const TrackerSteamReceipt steam_;
    const std::shared_ptr<const std::atomic<bool>> epoch_;
    const std::shared_ptr<std::atomic<bool>> retained_;
    const std::shared_ptr<const ProcessObjectEvidence> launcher_;
    const std::uint64_t sourceRevision_;
    const std::uint32_t appid_;
    const std::uint64_t fullGameId_;
    const unsigned line_;
    const std::string at_;
    const std::string sourcePath_;
    const std::string exactLine_;

    StartupLaunchEvidence(TrackerSteamReceipt steam,
        std::shared_ptr<const std::atomic<bool>> epoch,
        std::shared_ptr<const ProcessObjectEvidence> launcher,
        std::uint64_t sourceRevision, std::uint32_t appid,
        std::uint64_t fullGameId, unsigned line, std::string at,
        std::string sourcePath, std::string exactLine)
        : steam_(std::move(steam)), epoch_(std::move(epoch)),
          retained_(std::make_shared<std::atomic<bool>>(true)),
          launcher_(std::move(launcher)), sourceRevision_(sourceRevision),
          appid_(appid), fullGameId_(fullGameId), line_(line),
          at_(std::move(at)), sourcePath_(std::move(sourcePath)),
          exactLine_(std::move(exactLine)) {}
public:
    StartupLaunchEvidence(const StartupLaunchEvidence&) = delete;
    StartupLaunchEvidence& operator=(const StartupLaunchEvidence&) = delete;

    const TrackerSteamReceipt& steam() const noexcept { return steam_; }
    const std::shared_ptr<const ProcessObjectEvidence>& launcher() const noexcept { return launcher_; }
    std::uint64_t sourceRevision() const noexcept { return sourceRevision_; }
    std::uint32_t appid() const noexcept { return appid_; }
    std::uint64_t fullGameId() const noexcept { return fullGameId_; }
    unsigned sourceLine() const noexcept { return line_; }
    const std::string& at() const noexcept { return at_; }
    const std::string& sourcePath() const noexcept { return sourcePath_; }
    const std::string& exactLine() const noexcept { return exactLine_; }
    bool sourceEpochRetained() const noexcept {
        return epoch_ && epoch_->load(std::memory_order_acquire) &&
            retained_ && retained_->load(std::memory_order_acquire);
    }
};

// Owner-issued association of a retained launch and an independently captured
// parent relation. TrackerOwner separately validates the actual snapshot epoch
// and current selected game before issuing or consuming this source fact.
class StartupHandoffEvidence final {
    friend class TrackerOwner;
    const std::shared_ptr<const StartupLaunchEvidence> launch_;
    const std::shared_ptr<const ProcessParentEvidence> relation_;
    const std::uint64_t freshSourceRevision_;
    const bool launcherRemoved_;

    StartupHandoffEvidence(std::shared_ptr<const StartupLaunchEvidence> launch,
        std::shared_ptr<const ProcessParentEvidence> relation,
        std::uint64_t freshSourceRevision, bool launcherRemoved) noexcept
        : launch_(std::move(launch)), relation_(std::move(relation)),
          freshSourceRevision_(freshSourceRevision), launcherRemoved_(launcherRemoved) {}
public:
    StartupHandoffEvidence(const StartupHandoffEvidence&) = delete;
    StartupHandoffEvidence& operator=(const StartupHandoffEvidence&) = delete;

    const std::shared_ptr<const StartupLaunchEvidence>& launch() const noexcept { return launch_; }
    const std::shared_ptr<const ProcessParentEvidence>& relation() const noexcept { return relation_; }
    std::uint64_t freshSourceRevision() const noexcept { return freshSourceRevision_; }
    bool launcherRemoved() const noexcept { return launcherRemoved_; }

    // Point check of launch retention and child liveness only. This does NOT
    // establish real publisher/account/native/action currentness.
    bool retainedChildCurrent() const noexcept {
        return launch_ && launch_->sourceEpochRetained() &&
            relation_ && relation_->childStillAlive();
    }
};

// Diagnostics only: neither counters nor status confer evidence or authority.
struct StartupLaunchCapture {
    enum class Status { Unavailable, Busy, Observed, ContextChanged, SourceIncomplete, Capacity };
    Status status = Status::Unavailable;
    std::uint64_t sourceRevision = 0;
    unsigned considered = 0, captured = 0, reused = 0, retained = 0;
};
} // namespace ymcc::steamgamefacts
