#pragma once
// Included inside screenpads after getConfig(). UI submits clicks; existing serial
// worker completes actions. No virtual report, hook, timer, worker or held OS key.
enum class StandaloneAction { None, SteamMainMenu, SteamQuickAccess, MicrophoneMute };
enum class StandaloneStatus { Idle, Pending, Accepted, QueueUnavailable, PowerBlocked,
    SteamUnavailable, SteamUncertain, MicrophoneFailed, KeyboardFailed, ModifiersHeld };
inline constexpr UINT kStandaloneResultMessage=WM_APP+175;
inline std::mutex standaloneMutex;
inline std::atomic<UINT_PTR> standaloneGeneration{1},standalonePending{0};
inline std::atomic<StandaloneStatus> standaloneStatus{StandaloneStatus::Idle};
inline std::atomic<unsigned long long> standaloneRequests{0},standaloneCompleted{0};
inline bool (*standaloneSubmit)(StandaloneAction,UINT_PTR)=nullptr;
inline StandaloneAction standaloneAction(StandaloneSpecialMode mode,int index) {
    if(index!=1 && index!=2)return StandaloneAction::None;
    if(mode==StandaloneSpecialMode::SteamDeck)return index==1?StandaloneAction::SteamMainMenu:StandaloneAction::SteamQuickAccess;
    if(mode==StandaloneSpecialMode::PS5)return index==1?StandaloneAction::SteamMainMenu:StandaloneAction::MicrophoneMute;
    return StandaloneAction::None;
}
inline const char* standaloneStatusName(StandaloneStatus status) {
    switch(status) {
    case StandaloneStatus::Pending:return "pending";
    case StandaloneStatus::Accepted:return "accepted";
    case StandaloneStatus::QueueUnavailable:return "action-queue-unavailable";
    case StandaloneStatus::PowerBlocked:return "input-power-gate";
    case StandaloneStatus::SteamUnavailable:return "steam-menu-unavailable";
    case StandaloneStatus::SteamUncertain:return "steam-menu-result-uncertain";
    case StandaloneStatus::MicrophoneFailed:return "microphone-mute-failed";
    case StandaloneStatus::KeyboardFailed:return "keyboard-injection-failed";
    case StandaloneStatus::ModifiersHeld:return "physical-modifiers-held";
    default:return "idle";
    }
}
inline bool standaloneFailed(StandaloneStatus status) {
    return status!=StandaloneStatus::Idle && status!=StandaloneStatus::Pending && status!=StandaloneStatus::Accepted;
}
inline void standaloneCancel() {
    std::lock_guard<std::mutex> lock(standaloneMutex);
    standaloneGeneration.fetch_add(1,std::memory_order_acq_rel);
    standalonePending.store(0,std::memory_order_release);
    standaloneStatus.store(StandaloneStatus::Idle,std::memory_order_release);
}
inline bool standaloneCurrent(UINT_PTR generation) {
    return generation==standaloneGeneration.load(std::memory_order_acquire) && activeProfile.load()==0 && appliedProfile.load()==0;
}
inline void standaloneFinish(UINT_PTR generation,StandaloneStatus status) {
    std::lock_guard<std::mutex> lock(standaloneMutex);
    if(!standaloneCurrent(generation))return;
    UINT_PTR expected=generation;
    if(!standalonePending.compare_exchange_strong(expected,0))return;
    standaloneStatus.store(status,std::memory_order_release);
    if(status==StandaloneStatus::Accepted)standaloneCompleted.fetch_add(1);
    if(HWND parent=owner.load())PostMessageW(parent,kStandaloneResultMessage,generation,0);
}
inline void standaloneDispatch(const Config& cfg,int index) {
    const auto action=standaloneAction(cfg.standaloneSpecialMode,index);
    UINT_PTR generation=0;
    {
        std::lock_guard<std::mutex> lock(standaloneMutex);
        generation=standaloneGeneration.load(std::memory_order_acquire);
        if(action==StandaloneAction::None || !standaloneCurrent(generation))return;
        UINT_PTR expected=0;
        if(!standalonePending.compare_exchange_strong(expected,generation))return; // no queued toggle storm
        standaloneStatus.store(StandaloneStatus::Pending);
    }
    if(standaloneSubmit && standaloneSubmit(action,generation))standaloneRequests.fetch_add(1);
    else if(dryRun && !standaloneSubmit) {standaloneRequests.fetch_add(1);standaloneFinish(generation,StandaloneStatus::Accepted);}
    else standaloneFinish(generation,StandaloneStatus::QueueUnavailable);
}
