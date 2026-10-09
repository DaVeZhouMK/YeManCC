// Included by main.cpp after the existing overlay/mouse adapters. One write lane
// owns both legacy features and the new performance monitor. No idle polling.
static std::mutex g_steamSettingsMx;
static std::atomic<bool> g_steamSettingsQueued{false}, g_steamClientPending{false};
static std::atomic<unsigned long long> g_steamSettingsSerial{0};
static unsigned long long g_steamOverlayPinnedSerial=0;
static std::string g_steamOverlayAccount;
static std::wstring g_steamOverlayPath;
static json steamMonitorStateLocked() {
    json result={{"ok",false},{"available",false},{"pending",false},{"restartRequired",false}};
    if(g_aiFanMockSession.isolated){result["reason"]="isolated-session";return result;}
    try{
        std::wstring path;std::string account;
        if(!sofFindLocalConfig(&path,&account)){result["reason"]="localconfig-not-found";return result;}
        result.update(ymcc::steamsettings::read(sgReadFile(path)));
        result["ok"]=true;result["available"]=true;
        const auto saved=ymSettingsSection("steamMonitor");
        const bool same=saved.value("account",std::string())==account && saved.value("path",std::string())==W2U(path);
        if(same){
            const bool pending=saved.value("pending",false);
            result["pending"]=pending;
            result["reason"]=saved.value("reason",std::string());
            result["restartRequired"]=pending && saved.value("restartRequired",false);
            if(pending && saved.contains("desired"))result.update(saved["desired"]);
            if(!pending && !saved.value("reason",std::string()).empty())result["ok"]=false;
            if(saved.contains("via"))result["via"]=saved["via"];
            DWORD pid=0;sofRegDWORD(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"pid",&pid);
            if(!pending && saved.value("via",std::string())=="live" && pid!=0 && pid==saved.value("livePid",0u) && saved.contains("liveValues"))result.update(saved["liveValues"]);
        }else if(saved.value("pending",false)){result["reason"]="steam-account-changed";}
    }catch(const std::exception& error){result["reason"]=error.what();}
    return result;
}
static json steamMonitorGet(){
    std::lock_guard<std::mutex> lock(g_steamSettingsMx);
    auto state=steamMonitorStateLocked();
    if(state.value("available",false) && !state.value("pending",false) && sofSteamRunning()){
        std::wstring path;std::string account;
        if(sofFindLocalConfig(&path,&account)){
            json keys=json::array();for(const auto& f:ymcc::steamsettings::fields)keys.push_back(f.client);
            const auto live=steamLiveRequest(static_cast<DWORD>(std::stoul(account)),{{"operation","settings.get"},{"keys",keys}});
            if(live.value("ok",false) && live.contains("values")){
                json values=json::object();for(const auto& f:ymcc::steamsettings::fields)if(live["values"].contains(f.client))values[f.name]=live["values"][f.client];
                if(values.size()==5){state.update(values);state["ok"]=true;state["via"]="live";state["reason"]="";}
            }
        }
    }return state;
}
static json steamMonitorSet(const json& patch){
    if(!ymcc::steamsettings::valid(patch))return {{"ok",false},{"reason","invalid-monitor-settings"}};
    {
        std::lock_guard<std::mutex> lock(g_steamSettingsMx);
        if(g_aiFanMockSession.isolated)return {{"ok",false},{"reason","isolated-session"}};
        std::wstring path;std::string account;
        if(!sofFindLocalConfig(&path,&account))return {{"ok",false},{"reason","localconfig-not-found"}};
        try{(void)ymcc::steamsettings::read(sgReadFile(path));}catch(const std::exception& e){return {{"ok",false},{"reason",e.what()}};}
        auto saved=ymSettingsSection("steamMonitor");
        json desired=json::object();
        if(saved.value("pending",false) && saved.value("account",std::string())==account && saved.value("path",std::string())==W2U(path))desired=saved.value("desired",json::object());
        desired.update(patch);
        saved={{"pending",true},{"account",account},{"path",W2U(path)},{"desired",desired},{"reason",""},{"restartRequired",false}};
        if(!ymSettingsWriteSection("steamMonitor",saved))return {{"ok",false},{"reason","settings-write-failed"}};
        g_steamClientPending.store(true,std::memory_order_release);
    }
    steamSettingsQueue("monitor-user");
    return steamMonitorGet();
}
static void steamClientSettingsAttemptLocked(){
    std::lock_guard<std::mutex> overlayLock(g_sofMx);
    const auto overlaySerial=g_sofRequestSerial.load(std::memory_order_acquire);
    const bool overlayPending=g_sofPending.load(std::memory_order_acquire);
    const int overlay=overlayPending?g_sofDesired.load(std::memory_order_acquire):-1;
    auto saved=ymSettingsSection("steamMonitor");
    const bool monitorPending=saved.value("pending",false);
    if(!overlayPending && !monitorPending){g_steamClientPending.store(false);return;}
    json monitor=monitorPending?saved.value("desired",json::object()):json::object();
    auto finish=[&](bool ok,bool pending,const std::string& via,const std::string& reason){
        if(monitorPending){
            saved["pending"]=pending;saved["reason"]=reason;saved["via"]=via;
            saved["restartRequired"]=pending && via=="deferred";
            if(!ymSettingsWriteSection("steamMonitor",saved)){
                saved["pending"]=false;saved["reason"]="settings-write-failed";
                // Do not silently sign failed durable acknowledgement as success.
                ipc_emit("steam.settings.updated",{{"monitor",{{"ok",false},{"available",true},{"pending",false},{"reason","settings-write-failed"}}}});
            }else ipc_emit("steam.settings.updated",{{"monitor",steamMonitorStateLocked()}});
        }
        if(overlayPending && overlaySerial==g_sofRequestSerial.load(std::memory_order_acquire)){
            g_sofPending.store(pending,std::memory_order_release);
            g_sofReceipt={{"ok",ok},{"via",via},{"value",overlay},{"reason",reason},{"restartRequired",pending&&via=="deferred"}};
            g_sofReceipt["persisted"]=ok && (via=="file" || (via=="live" && ymcc::steamsettings::overlayValue(sgReadFile(g_steamOverlayPath))==overlay));
            ipc_emit("steamOverlayFix.updated",steamOverlayFixStateLocked());
        }
        g_steamClientPending.store(pending || g_sofPending.load(std::memory_order_acquire),std::memory_order_release);
    };
    std::wstring path;std::string account;
    if(!sofFindLocalConfig(&path,&account)){finish(false,false,"error","localconfig-not-found");return;}
    // Explicit monitor requests are bound to one account/file. Never migrate a
    // previous user's settings to whichever account signs in after restart.
    if(monitorPending && (saved.value("account",std::string())!=account || saved.value("path",std::string())!=W2U(path))){
        finish(false,false,"error","steam-account-changed");return;
    }
    if(overlayPending){
        if(g_steamOverlayPinnedSerial!=overlaySerial){g_steamOverlayAccount.clear();g_steamOverlayPath.clear();g_steamOverlayPinnedSerial=overlaySerial;}
        if(g_steamOverlayAccount.empty()){g_steamOverlayAccount=account;g_steamOverlayPath=path;}
        else if(g_steamOverlayAccount!=account || g_steamOverlayPath!=path){finish(false,false,"error","steam-account-changed");return;}
    }
    try{
        const auto text=sgReadFile(path);
        if(text.empty()){finish(false,false,"error","read-failed");return;}
        const bool running=sofSteamRunning();
        if(running){
            const auto result=steamLiveRequest(static_cast<DWORD>(std::stoul(account)),{{"operation","settings.set"},{"values",ymcc::steamsettings::clientPatch(monitor,overlay)}});
            if(result.value("ok",false)){
                if(monitorPending){
                    auto accepted=ymcc::steamsettings::read(text);accepted.update(monitor);
                    saved["liveValues"]=accepted;
                    DWORD pid=0;sofRegDWORD(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"pid",&pid);saved["livePid"]=pid;
                }
                finish(true,false,"live","");return;
            }
            const bool defer=steamLiveCanDefer(result) && result.value("reason",std::string())!="steam-account-changed";
            finish(false,defer,defer?"deferred":"error",result.value("reason",std::string("live-readback-failed")));
            return; // Never patch Steam-owned files while its process is running.
        }
        const auto output=ymcc::steamsettings::patch(text,monitor,overlay);
        if(sofSteamRunning()){finish(false,true,"deferred","steam-session-starting");return;}
        std::wstring current;std::string currentAccount;
        if(!sofFindLocalConfig(&current,&currentAccount) || current!=path || currentAccount!=account){finish(false,false,"error","steam-account-changed");return;}
        if(sgReadFile(path)!=text){finish(false,false,"error","localconfig-changed");return;}
        if(output!=text){
            if(!CopyFileW(path.c_str(),(path+L".ymcc-bak").c_str(),FALSE)){finish(false,false,"error","backup-failed");return;}
            if(sofSteamRunning()){finish(false,true,"deferred","steam-session-starting");return;}
            if(!sgWriteFileAtomic(path,output)){finish(false,false,"error","write-failed");return;}
        }
        const auto readback=sgReadFile(path);
        if(!ymcc::steamsettings::matches(ymcc::steamsettings::read(readback),monitor) ||
            (overlay>=0 && ymcc::steamsettings::overlayValue(readback)!=overlay)){finish(false,false,"error","readback-failed");return;}
        // Monitor + EnableGameOverlay share ONE backup/atomic localconfig write.
        finish(true,false,"file","");
    }catch(const std::exception& e){finish(false,false,"error",e.what());}
    if(!g_sofPending.load()){g_steamOverlayAccount.clear();g_steamOverlayPath.clear();}
}
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
static json steamSettingsGet(const json& args){
    const auto scope=args.value("scope",std::string("monitor"));
    if(scope=="monitor")return steamMonitorGet();
    if(scope=="mouse")return steamDeckMouseGet();
    if(scope=="overlay")return steamOverlayFixGet();
    return {{"ok",false},{"reason","invalid-steam-scope"}};
}
static json steamSettingsSet(const json& args){
    if(args.size()!=1)return {{"ok",false},{"reason","invalid-steam-settings"}};
    if(args.contains("overlayOffFix")){
        if(!args["overlayOffFix"].is_boolean())return {{"ok",false},{"reason","invalid-overlay-value"}};
        if(g_aiFanMockSession.isolated)return {{"ok",false},{"reason","isolated-session"}};
        const bool enabled=args["overlayOffFix"].get<bool>();
        if(!ymSettingsPatchSection("sleep",{{"steamOverlayOffFix",enabled}}))return {{"ok",false},{"reason","settings-write-failed"}};
        g_sofEnabled.store(enabled,std::memory_order_release);steamOverlayFixKick("setting");
        return {{"ok",true},{"accepted",true},{"pending",true}}; // queue ACK, not application ACK
    }
    if(args.contains("monitor"))return steamMonitorSet(args["monitor"]);
    if(args.contains("mousePercent"))return steamDeckMouseSet({{"percent",args["mousePercent"]}});
    return {{"ok",false},{"reason","invalid-steam-settings"}};
}
