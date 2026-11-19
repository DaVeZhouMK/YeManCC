#pragma once
// Observe only the original selected process under the existing public owners.
// The same public two-source/group observation is used for healthy references;
// this facade grants no mutation or replacement selection.
static SteamGameLink::TrackerRead nativeSteamNativeObservationTracking(
    const SleepSteamRecovery::Session& client,const SleepSteamRecovery::Game& game,const std::function<bool()>& active){
    const auto allowed=[&](){try{return active&&active();}catch(...){return false;}};
    if(!allowed())return {};
    auto held=ymcc::steamgamefacts::processOwner().acquire(
        g_steamSettingsObserver,g_steamSettingsObserver.identitySnapshot(),{game.pid,game.created,game.path});
    if(!held||!held->processCurrent()||!allowed())return {};
    return SteamGameLink::trackingOriginal(client,game,held,allowed);
}
