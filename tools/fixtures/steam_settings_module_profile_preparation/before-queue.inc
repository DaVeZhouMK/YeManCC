static void steamSettingsQueue(const char* reason){
    if(g_aiFanMockSession.isolated)return;
    ++g_steamSettingsSerial;
    if(g_steamSettingsQueued.exchange(true,std::memory_order_acq_rel))return;
    g_sofQueued.store(g_sofPending.load());
    if(!poolSubmit([why=std::string(reason?reason:"user")] {
        // One bounded coalescing delay per USER burst, not an idle timer/poller.
        // Three page adapters all enter this lane; mouse layout remains its
        // own Steam-native file/API, never repurposed as localconfig.
        std::this_thread::sleep_for(std::chrono::milliseconds(160));
        const auto serial=g_steamSettingsSerial.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lock(g_steamSettingsMx);
            try{steamClientSettingsAttemptLocked();}catch(...){g_steamClientPending.store(false);}
            std::lock_guard<std::mutex> mouseLock(g_sdmMx);
            try{
                const bool refresh=g_sdmRefreshRequested.exchange(false);
                if(g_sdmApplyRequested.exchange(false) || g_sdmEventPending.load() || refresh)sdmApplyLocked();
                if(refresh)ipc_emit("steamDeckMouse.updated",sdmStateLocked(true));
            }catch(...){g_sdmEventPending.store(false);}
        }
        g_sofQueued.store(false);g_steamSettingsQueued.store(false,std::memory_order_release);
        if(serial!=g_steamSettingsSerial.load(std::memory_order_acquire))steamSettingsQueue("user-followup");
        appendNativeLifecycleLog("steam-settings-batch",{{"source",why},{"clientPending",g_steamClientPending.load()},{"mousePending",g_sdmEventPending.load()}});
    })){g_sofQueued.store(false);g_steamSettingsQueued.store(false);}
}
