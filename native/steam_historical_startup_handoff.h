#pragma once
#include "steam_game_tracker_observation.h"
#include "steam_historical_parent_evidence.h"
#include <atomic>
namespace ymcc::steamgamefacts {
class TrackerOwner;
// Separate historical source fact. It cannot be converted to live launch/group
// evidence, current tracked state, SavedHealthyProof or a publisher ticket.
class HistoricalStartupHandoffEvidence final {
    friend class TrackerOwner;
    const std::shared_ptr<const HistoricalExitedParentEvidence> historical_;
    const TrackerSteamReceipt steam_;
    const TrackerOwnerCurrent retainedScopeCurrent_;
    const std::shared_ptr<const std::atomic<bool>> epoch_,retained_;
    const std::uint64_t sourceRevision_,fullGameId_;
    const std::uint32_t appid_;
    const unsigned sourceLine_;
    const std::string sourcePath_,exactLine_;
    const bool launcherRemoved_;
    HistoricalStartupHandoffEvidence(std::shared_ptr<const HistoricalExitedParentEvidence> historical,
        TrackerSteamReceipt steam,TrackerOwnerCurrent retainedCurrent,std::shared_ptr<const std::atomic<bool>> epoch,
        std::shared_ptr<const std::atomic<bool>> retained,std::uint64_t revision,std::uint64_t full,
        std::uint32_t app,unsigned line,std::string path,std::string exact,bool removed)
        :historical_(std::move(historical)),steam_(std::move(steam)),retainedScopeCurrent_(std::move(retainedCurrent)),epoch_(std::move(epoch)),retained_(std::move(retained)),
        sourceRevision_(revision),fullGameId_(full),appid_(app),sourceLine_(line),sourcePath_(std::move(path)),exactLine_(std::move(exact)),launcherRemoved_(removed){}
public:
    HistoricalStartupHandoffEvidence(const HistoricalStartupHandoffEvidence&)=delete;
    HistoricalStartupHandoffEvidence& operator=(const HistoricalStartupHandoffEvidence&)=delete;
    const std::shared_ptr<const HistoricalExitedParentEvidence>& historicalParent()const noexcept{return historical_;}
    const TrackerSteamReceipt& steam()const noexcept{return steam_;}
    std::uint64_t fullGameId()const noexcept{return fullGameId_;}
    std::uint32_t appid()const noexcept{return appid_;}
    std::uint64_t sourceRevision()const noexcept{return sourceRevision_;}
    unsigned sourceLine()const noexcept{return sourceLine_;}
    const std::string& sourcePath()const noexcept{return sourcePath_;}
    const std::string& exactLine()const noexcept{return exactLine_;}
    bool launcherRemoved()const noexcept{return launcherRemoved_;}
    // Only retained epoch and exact historical objects. Caller/source current
    // remains the public TrackerOwner current method, never an action grant.
    bool retainedChildCurrent()const noexcept{try{return retainedScopeCurrent_&&retainedScopeCurrent_()&&epoch_&&epoch_->load(std::memory_order_acquire)&&retained_&&retained_->load(std::memory_order_acquire)&&historical_&&historical_->relationCurrent();}catch(...){return false;}}
};
}
