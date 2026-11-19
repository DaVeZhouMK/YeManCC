#pragma once
#include "steam_startup_handoff.h"
#include "steam_historical_startup_handoff.h"
#include <functional>

// In-process recovery consumption only. This source fact never supplies native
// record state, a saved healthy reference, or mutation authority.
namespace ymcc::steamOriginalRecovery {
struct StartupHandoffTarget {
    steamgamefacts::ProcessWitness steam, game;
    std::uint32_t windowsSession = 0;
    std::uint64_t fullGameId = 0;
    std::uint32_t appid = 0;
};
inline bool startupHandoffMatches(
    const std::shared_ptr<const steamgamefacts::StartupHandoffEvidence>& evidence,
    const StartupHandoffTarget& target) noexcept {
    try {
        if(!evidence || !evidence->launch() || !evidence->relation() ||
            !evidence->relation()->child()) return false;
        const auto& launch=*evidence->launch();
        const auto& steam=launch.steam();
        const auto& child=*evidence->relation()->child();
        const auto& game=child.witness();
        return steam.present && steam.rootKnown && steam.windowsSessionKnown &&
            steam.pid==target.steam.pid && steam.birth==target.steam.created &&
            steam.windowsSession==target.windowsSession &&
            steamgamefacts::samePath(steam.root+L"\\steam.exe",target.steam.image) &&
            game.pid==target.game.pid && game.created==target.game.created &&
            steamgamefacts::samePath(game.image,target.game.image) &&
            child.windowsSession()==target.windowsSession &&
            target.fullGameId && target.appid &&
            ((target.fullGameId>>24)&255)==2 &&
            static_cast<std::uint32_t>(target.fullGameId>>32)==target.appid &&
            launch.fullGameId()==target.fullGameId && launch.appid()==target.appid;
    } catch(...) { return false; }
}
inline bool startupHandoffOwnerCurrent(
    const std::shared_ptr<const steamgamefacts::StartupHandoffEvidence>& evidence,
    const std::function<bool()>& ownerCurrent) noexcept {
    if(!evidence) return !ownerCurrent;
    try { return ownerCurrent && evidence->retainedChildCurrent() && ownerCurrent(); }
    catch(...) { return false; }
}
// Historical launch facts remain a separate type; they supply neither live
// group roles, a saved healthy reference, nor publication authority.
inline bool historicalStartupHandoffMatches(
    const std::shared_ptr<const steamgamefacts::HistoricalStartupHandoffEvidence>& evidence,
    const StartupHandoffTarget& target) noexcept {
    try {
        if(!evidence || !evidence->historicalParent() ||
            !evidence->historicalParent()->child()) return false;
        const auto& steam=evidence->steam();
        const auto& child=*evidence->historicalParent()->child();
        const auto& game=child.witness();
        return steam.present && steam.rootKnown && steam.windowsSessionKnown &&
            steam.pid==target.steam.pid && steam.birth==target.steam.created &&
            steam.windowsSession==target.windowsSession &&
            steamgamefacts::samePath(steam.root+L"\\steam.exe",target.steam.image) &&
            game.pid==target.game.pid && game.created==target.game.created &&
            steamgamefacts::samePath(game.image,target.game.image) &&
            child.windowsSession()==target.windowsSession &&
            target.fullGameId && target.appid &&
            static_cast<std::uint32_t>(target.fullGameId)==0x02000000u &&
            static_cast<std::uint32_t>(target.fullGameId>>32)==target.appid &&
            evidence->fullGameId()==target.fullGameId && evidence->appid()==target.appid;
    } catch(...) { return false; }
}
inline bool historicalStartupHandoffOwnerCurrent(
    const std::shared_ptr<const steamgamefacts::HistoricalStartupHandoffEvidence>& evidence,
    const std::function<bool()>& ownerCurrent) noexcept {
    if(!evidence) return !ownerCurrent;
    try { return ownerCurrent && ownerCurrent() && evidence->retainedChildCurrent() && ownerCurrent(); }
    catch(...) { return false; }
}
} // namespace ymcc::steamOriginalRecovery
