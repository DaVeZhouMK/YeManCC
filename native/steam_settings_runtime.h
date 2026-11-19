// Included by main.cpp after the existing overlay/mouse adapters. One write lane
// owns both legacy features and the new performance monitor. No idle polling.
static std::mutex g_steamSettingsMx;
static std::atomic<bool> g_steamSettingsQueued{false}, g_steamClientPending{false};
static std::atomic<bool> g_steamModuleProfilePreparationPending{false};
// Optional one-shot startup consumer; the existing write lane owns execution.
static std::atomic<void(*)()> g_steamSettingsStartupConsumer{nullptr};
static std::atomic<unsigned long long> g_steamSettingsSerial{0};
static unsigned long long g_steamOverlayPinnedSerial=0;
static std::string g_steamOverlayAccount;
static std::wstring g_steamOverlayPath;
// Persisted module fields and Live receipts are independent untrusted inputs.
// Detection is read-only: preserve the bad section until an explicit valid SET
// uses the existing root writer (which preserves its preimage before replacement).
static json steamMonitorUnknown(const std::string& reason) {
    return {{"ok",false},{"available",false},{"pending",false},{"restartRequired",false},
        {"liveConfirmed",false},{"applicationConfirmed",false},{"via","unknown"},{"reason",reason}};
}
static bool steamMonitorAccountValid(const std::string& account) {
    if(account.empty() || account.size()>10)return false;
    unsigned long long value=0;
    for(const auto c:account){if(c<'0'||c>'9')return false;value=value*10+static_cast<unsigned>(c-'0');}
    return value>0 && value<=0xffffffffULL;
}
static bool steamMonitorValuesValid(const json& values) {
    if(!values.is_object() || values.size()!=ymcc::steamsettings::fields.size())return false;
    for(const auto& f:ymcc::steamsettings::fields){
        const auto it=values.find(f.name);if(it==values.end()||!it->is_number())return false;
        const double v=it->get<double>();
        if(!std::isfinite(v)||v<f.min-1e-6||v>f.max+1e-6||(f.integer&&v!=std::round(v)))return false;
    }return true;
}
static bool steamMonitorSavedValid(const json& saved) {
    if(!saved.is_object())return false;
    if(saved.empty())return true; // No saved intent; never fabricate an identity.
    if(saved.contains("schemaVersion")&&(!saved["schemaVersion"].is_number_integer()||saved["schemaVersion"]!=1))return false;
    if(!saved.contains("pending")||!saved["pending"].is_boolean() ||
       !saved.contains("account")||!saved["account"].is_string()||!steamMonitorAccountValid(saved["account"].get<std::string>()) ||
       !saved.contains("path")||!saved["path"].is_string()||saved["path"].get_ref<const std::string&>().empty() ||
       !saved.contains("desired")||!ymcc::steamsettings::valid(saved["desired"]))return false;
    if(saved.contains("reason")&&!saved["reason"].is_string())return false;
    if(saved.contains("restartRequired")&&!saved["restartRequired"].is_boolean())return false;
    if(saved.contains("via")&&!saved["via"].is_string())return false;
    const bool pending=saved["pending"].get<bool>();const auto via=saved.value("via",std::string());
    if(via!=""&&via!="live"&&via!="file"&&via!="deferred"&&via!="error"&&via!="unknown")return false;
    if(saved.value("restartRequired",false)&&(!pending||via!="deferred"))return false;
    const bool cached=saved.contains("livePid")||saved.contains("liveValues");
    if(cached){
        if(!saved.contains("livePid")||!saved["livePid"].is_number_integer()||saved["livePid"]<=0||saved["livePid"]>0xffffffffULL ||
           !saved.contains("liveValues")||!steamMonitorValuesValid(saved["liveValues"]))return false;
    }
    return via!="live"||(!pending&&cached);
}
static bool steamMonitorLiveEnvelopeValid(const json& reply) {
    return reply.is_object()&&reply.contains("ok")&&reply["ok"].is_boolean() &&
        (!reply.contains("reason")||reply["reason"].is_string()) &&
        (!reply.contains("mutated")||reply["mutated"].is_boolean()) &&
        (!reply.contains("via")||(reply["via"].is_string()&&reply["via"]=="live")) &&
        (!reply.contains("values")||reply["values"].is_object());
}
static bool steamMonitorSetReceiptValid(const json& reply,const json& expected) {
    if(!steamMonitorLiveEnvelopeValid(reply))return false;
    if(!reply["ok"].get<bool>())return true;
    if(!reply.contains("values")||reply["values"].size()!=expected.size())return false;
    for(auto it=expected.begin();it!=expected.end();++it){
        const auto value=reply["values"].find(it.key());if(value==reply["values"].end())return false;
        if(it->is_boolean()){if(!value->is_boolean()||*value!=*it)return false;}
        else if(!value->is_number()||!std::isfinite(value->get<double>())||std::abs(value->get<double>()-it->get<double>())>1e-5)return false;
    }return true;
}
static json steamMonitorSavedSection() {
    // The generic section accessor intentionally folds scalar/array sections to
    // {}. Read the same root once, preserving the original module type so it
    // cannot masquerade as an absent section and receive a Live confirmation.
    const auto document=ymSettingsRead();
    if(!document.is_object())return nullptr;
    const auto saved=document.find("steamMonitor");
    return saved==document.end()?json::object():*saved;
}
static json steamMonitorStateLocked() {
    json result=steamMonitorUnknown("");
    if(g_aiFanMockSession.isolated){result["reason"]="isolated-session";return result;}
    try{
        const auto saved=steamMonitorSavedSection();
        if(!steamMonitorSavedValid(saved))return steamMonitorUnknown("invalid-steam-monitor-state");
        if(saved.value("reason",std::string())=="invalid-steam-monitor-receipt")return steamMonitorUnknown("invalid-steam-monitor-receipt");
        std::wstring path;std::string account;DWORD pid=0;
        if(!steamSettingsObservedLocalConfig(&path,&account,&pid)){result["reason"]="localconfig-not-found";return result;}
        result.update(ymcc::steamsettings::read(sgReadFile(path)));
        result["ok"]=true;result["available"]=true;
        const bool same=saved.value("account",std::string())==account && saved.value("path",std::string())==W2U(path);
        if(same){
            const bool pending=saved.value("pending",false);
            result["pending"]=pending;
            result["reason"]=saved.value("reason",std::string());
            result["restartRequired"]=pending && saved.value("restartRequired",false);
            if(pending && saved.contains("desired"))result.update(saved["desired"]);
            if(!pending && !saved.value("reason",std::string()).empty())result["ok"]=false;
            if(saved.contains("via"))result["via"]=saved["via"];
            if(!pending && saved.value("via",std::string())=="live" && pid!=0 && pid==saved.value("livePid",0u) && saved.contains("liveValues"))result.update(saved["liveValues"]);
        }else if(saved.value("pending",false)){result["reason"]="steam-account-changed";}
    }catch(const std::exception& error){return steamMonitorUnknown(error.what());}
    return result;
}
using SteamSettingsIdentity=decltype(g_steamSettingsObserver.identitySnapshot());
// Resolve only from one published identity. Callers retain epoch checks around
// this existing file selection; this helper grants no mutation or absence.
static bool steamSettingsIdentityTarget(const SteamSettingsIdentity& identity,std::wstring* path,std::string* account){
    if(!identity||identity->rootReadError!=ERROR_SUCCESS||identity->registryRoot.empty())return false;
    const auto candidate=identity->accountReadError==ERROR_SUCCESS?identity->session.account:DWORD{0};
    if(identity->presence!=0&&!candidate)return false;
    if(!sofFindLocalConfigAt(identity->registryRoot,candidate,path,account))return false;
    return identity->presence==0||(account&&*account==std::to_string(candidate));
}
static void steamClientSettingsAttemptLocked(json* monitorAck=nullptr,const SteamSettingsIdentity& expectedIdentity={});
static json steamMonitorGetLocked(){
    auto state=steamMonitorStateLocked();
    // GET never applies saved pending intent. Corruption cannot be promoted to
    // Live by a separate successful query; an explicit valid SET repairs it.
    state["liveConfirmed"]=false;
    const auto reason=state.value("reason",std::string());
    if(g_aiFanMockSession.isolated||reason=="invalid-steam-monitor-state"||reason=="invalid-steam-monitor-receipt")return state;
    try{
        const auto identity=g_steamSettingsObserver.identitySnapshot();
        const auto sameIdentity=[&]{return identity&&g_steamSettingsObserver.sameIdentityEpoch(identity);};
        if(!identity||!sameIdentity()||(identity->presence!=0&&identity->presence!=1)){
            g_steamSettingsObserver.requestIdentityRefresh();
            state["ok"]=false;state["available"]=false;state["via"]="unconfirmed";state["reason"]="steam-session-starting";return state;
        }
        if(identity->presence==1){
            state["ok"]=false;state["available"]=false;state["via"]="unconfirmed";
            if(!identity->session.pid||!identity->session.created||identity->session.root.empty()){
                state["reason"]="steam-session-starting";return state;
            }
            std::wstring path;std::string account;
            const auto candidate=identity->accountReadError==ERROR_SUCCESS?identity->session.account:DWORD{0};
            if(identity->rootReadError!=ERROR_SUCCESS||identity->registryRoot.empty()||
                !sofFindLocalConfigAt(identity->registryRoot,candidate,&path,&account)){state["reason"]="localconfig-not-found";return state;}
            if(!sameIdentity()){state["reason"]="steam-session-changed";return state;}
            if(!steamMonitorAccountValid(account))return steamMonitorUnknown("invalid-steam-account");
            json keys=json::array();for(const auto& f:ymcc::steamsettings::fields)keys.push_back(f.client);
            const auto live=steamLiveRequest(static_cast<DWORD>(std::stoul(account)),{{"operation","settings.get"},{"keys",keys}},sameIdentity,sameIdentity);
            if(!steamMonitorLiveEnvelopeValid(live))return steamMonitorUnknown("invalid-steam-monitor-receipt");
            const auto afterIdentity=g_steamSettingsObserver.identitySnapshot();
            if(!sameIdentity()){
                const bool sameProcess=afterIdentity&&afterIdentity->processEpoch==identity->processEpoch&&
                    afterIdentity->session.pid==identity->session.pid&&afterIdentity->session.created==identity->session.created&&
                    afterIdentity->session.root==identity->session.root;
                state["reason"]=sameProcess?"steam-account-changed":"steam-session-changed";return state;
            }
            std::wstring afterPath;std::string afterAccount;
            if(!sofFindLocalConfigAt(identity->registryRoot,candidate,&afterPath,&afterAccount)||afterPath!=path||afterAccount!=account){state["reason"]="steam-account-changed";return state;}
            if(!sameIdentity()){state["reason"]="steam-session-changed";return state;}
            if(live["ok"].get<bool>()&&live.contains("values")){
                json values=json::object();
                for(const auto& f:ymcc::steamsettings::fields)if(live["values"].contains(f.client))values[f.name]=live["values"][f.client];
                if(steamMonitorValuesValid(values)){state.update(values);state["ok"]=true;state["available"]=true;state["via"]="live";state["liveConfirmed"]=true;state["reason"]="";}
                else state["reason"]="live-readback-failed";
            }else state["reason"]=live.value("reason",std::string("live-readback-failed"));
        }
    }catch(const std::exception&){return steamMonitorUnknown("invalid-steam-monitor-receipt");}
    return state;
}
static json steamMonitorGet(){
    std::lock_guard<std::mutex> lock(g_steamSettingsMx);
    return steamMonitorGetLocked();
}
static json steamMonitorSet(const json& patch,bool awaitApplied=false){
    if(!ymcc::steamsettings::valid(patch))return {{"ok",false},{"reason","invalid-monitor-settings"}};
    {
        std::lock_guard<std::mutex> lock(g_steamSettingsMx);
        if(g_aiFanMockSession.isolated)return {{"ok",false},{"reason","isolated-session"}};
        const auto identity=g_steamSettingsObserver.identitySnapshot();
        const auto sameIdentity=[&]{return identity&&g_steamSettingsObserver.sameIdentityEpoch(identity);};
        if(!sameIdentity()){g_steamSettingsObserver.requestIdentityRefresh();return {{"ok",false},{"reason","steam-session-starting"}};}
        std::wstring path;std::string account;
        if(!steamSettingsIdentityTarget(identity,&path,&account))return {{"ok",false},{"reason","localconfig-not-found"}};
        if(!sameIdentity())return {{"ok",false},{"reason","steam-session-changed"}};
        try{(void)ymcc::steamsettings::read(sgReadFile(path));}catch(const std::exception& e){return {{"ok",false},{"reason",e.what()}};}
        // A matching GET from a restarted Steam cannot acknowledge this SET.
        const DWORD requestPid=awaitApplied&&identity->registeredPidReadError==ERROR_SUCCESS?identity->registeredPid:DWORD{0};
        auto saved=steamMonitorSavedSection();
        json desired=json::object();
        if(steamMonitorSavedValid(saved) && saved.value("pending",false) && saved.value("account",std::string())==account && saved.value("path",std::string())==W2U(path))desired=saved.value("desired",json::object());
        desired.update(patch);
        saved={{"pending",true},{"account",account},{"path",W2U(path)},{"desired",desired},{"reason",""},{"restartRequired",false}};
        if(!sameIdentity())return {{"ok",false},{"reason","steam-session-changed"}};
        if(!ymSettingsWriteSection("steamMonitor",saved))return {{"ok",false},{"reason","settings-write-failed"}};
        g_steamClientPending.store(true,std::memory_order_release);
        if(awaitApplied){
            // One lock covers this request's intent, write receipt and fresh GET.
            // A GET can confirm current state, never whether THIS SET succeeded.
            json receipt={{"ok",false},{"reason","no-application-receipt"}};
            steamClientSettingsAttemptLocked(&receipt,identity);
            auto state=steamMonitorGetLocked();
            std::wstring currentPath;std::string currentAccount;
            const bool same=steamSettingsIdentityTarget(identity,&currentPath,&currentAccount)&&currentPath==path&&currentAccount==account;
            const bool sameProcess=requestPid!=0&&sameIdentity();
            const bool applied=same && sameProcess && receipt.value("ok",false) && receipt.value("persisted",false) &&
                !receipt.value("pending",true) && receipt.value("via",std::string())=="live" &&
                state.value("liveConfirmed",false) && !state.value("pending",true) && ymcc::steamsettings::matches(state,desired);
            state["applicationConfirmed"]=applied;
            if(!applied){
                state["ok"]=false;
                auto reason=receipt.value("reason",std::string());
                if(!same)reason="steam-account-changed";
                else if(receipt.value("ok",false)&&receipt.value("persisted",false)&&receipt.value("via",std::string())=="live"&&!sameProcess)reason="steam-session-changed";
                if(reason.empty())reason=state.value("reason",std::string());
                state["reason"]=reason.empty()?"live-application-unconfirmed":reason;
            }
            return state;
        }
    }
    steamSettingsQueue("monitor-user");
    return steamMonitorGet(); // ordinary sliders retain coalescing / queue ACK
}
static void steamClientSettingsAttemptLocked(json* monitorAck,const SteamSettingsIdentity& expectedIdentity){
    // Abandoned sleep overlay requests never enter the shared monitor write lane.
    g_sofEnabled.store(false,std::memory_order_release);
    g_sofPending.store(false,std::memory_order_release);
    std::lock_guard<std::mutex> overlayLock(g_sofMx);
    const auto overlaySerial=g_sofRequestSerial.load(std::memory_order_acquire);
    const bool overlayPending=g_sofPending.load(std::memory_order_acquire);
    const int overlay=overlayPending?g_sofDesired.load(std::memory_order_acquire):-1;
    auto saved=steamMonitorSavedSection();
    if(!steamMonitorSavedValid(saved)){
        g_steamClientPending.store(false,std::memory_order_release);
        if(monitorAck)*monitorAck={{"ok",false},{"pending",false},{"via","unknown"},{"reason","invalid-steam-monitor-state"},{"persisted",false}};
        ipc_emit("steam.settings.updated",{{"monitor",steamMonitorUnknown("invalid-steam-monitor-state")}});
        return; // Preserve original; do not replay, normalize or write from retry.
    }
    const bool monitorPending=saved.value("pending",false);
    json persistedIntent=saved;
    if(!overlayPending && !monitorPending){g_steamClientPending.store(false);return;}
    json monitor=monitorPending?saved.value("desired",json::object()):json::object();
    const auto identity=expectedIdentity?expectedIdentity:g_steamSettingsObserver.identitySnapshot();
    const auto sameIdentity=[&]{return identity&&g_steamSettingsObserver.sameIdentityEpoch(identity);};
    bool enteredLive=false;
    auto finish=[&](bool ok,bool pending,const std::string& via,const std::string& reason){
        bool remainingPending=pending;
        if(monitorPending){
            if(monitorAck)*monitorAck={{"ok",ok},{"pending",pending},{"via",via},{"reason",reason},{"persisted",false}};
            saved["pending"]=pending;saved["reason"]=reason;saved["via"]=via;
            saved["restartRequired"]=pending && via=="deferred" && reason!="steam-session-starting";
            if(!ymSettingsWriteSection("steamMonitor",saved,[&](json&,json& root){
                const auto current=root.find("steamMonitor");
                return current!=root.end()&&*current==persistedIntent&&
                    (!enteredLive||(!pending&&!(ok&&via=="live"))||sameIdentity());
            })){
                remainingPending=false;saved["pending"]=false;saved["reason"]="settings-write-failed";
                if(monitorAck){(*monitorAck)["ok"]=false;(*monitorAck)["reason"]="settings-write-failed";}
                // Do not silently sign failed durable acknowledgement as success.
                ipc_emit("steam.settings.updated",{{"monitor",{{"ok",false},{"available",true},{"pending",false},{"reason","settings-write-failed"}}}});
            }else{
                if(monitorAck)(*monitorAck)["persisted"]=true;
                ipc_emit("steam.settings.updated",{{"monitor",steamMonitorStateLocked()}});
            }
        }
        if(overlayPending && overlaySerial==g_sofRequestSerial.load(std::memory_order_acquire)){
            g_sofPending.store(pending,std::memory_order_release);
            g_sofReceipt={{"ok",ok},{"via",via},{"value",overlay},{"reason",reason},{"restartRequired",pending&&via=="deferred"}};
            g_sofReceipt["persisted"]=ok && (via=="file" || (via=="live" && ymcc::steamsettings::overlayValue(sgReadFile(g_steamOverlayPath))==overlay));
            ipc_emit("steamOverlayFix.updated",steamOverlayFixStateLocked());
        }
        g_steamClientPending.store(remainingPending || g_sofPending.load(std::memory_order_acquire),std::memory_order_release);
    };
    if(!sameIdentity()){
        g_steamSettingsObserver.requestIdentityRefresh();
        finish(false,!expectedIdentity,expectedIdentity?"unknown":"deferred",expectedIdentity?"steam-session-changed":"steam-session-starting");return;
    }
    std::wstring path;std::string account;
    if(!steamSettingsIdentityTarget(identity,&path,&account)){finish(false,false,"error","localconfig-not-found");return;}
    if(!sameIdentity()){finish(false,false,"unknown","steam-session-changed");return;}
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
        if(!sameIdentity()){finish(false,false,"unknown","steam-session-changed");return;}
        if(identity->presence!=0&&identity->presence!=1){
            g_steamSettingsObserver.requestIdentityRefresh();finish(false,true,"deferred","steam-session-starting");return;
        }
        const bool running=identity->presence==1;
        if(running){
            if(!identity->session.pid||!identity->session.created||identity->session.root.empty()){
                g_steamSettingsObserver.requestIdentityRefresh();finish(false,true,"deferred","steam-session-starting");return;
            }
            if(monitorPending){
                // Durably consume replay authority before a possibly submitted
                // Live write. Failed terminal ACKs cannot resurrect this SET.
                auto consumed=saved;consumed["pending"]=false;consumed["via"]="unknown";
                consumed["reason"]="live-application-unconfirmed";consumed["restartRequired"]=false;
                if(!ymSettingsWriteSection("steamMonitor",consumed,[&](json&,json& root){
                    const auto current=root.find("steamMonitor");return current!=root.end()&&*current==persistedIntent;
                })){
                    if(monitorAck)*monitorAck={{"ok",false},{"pending",false},{"via","unknown"},{"reason","settings-write-failed"},{"persisted",false}};
                    g_steamClientPending.store(false,std::memory_order_release);return;
                }
                persistedIntent=std::move(consumed);
            }
            enteredLive=true;json result;
            try{result=steamLiveRequest(static_cast<DWORD>(std::stoul(account)),{{"operation","settings.set"},{"values",ymcc::steamsettings::clientPatch(monitor,overlay)}},sameIdentity,sameIdentity);}
            catch(const std::exception&){finish(false,false,"unknown","invalid-steam-monitor-receipt");return;}
            if(!sameIdentity()){finish(false,false,"unknown","steam-session-changed");return;}
            if(!steamMonitorSetReceiptValid(result,ymcc::steamsettings::clientPatch(monitor,overlay))){finish(false,false,"unknown","invalid-steam-monitor-receipt");return;}
            if(result.value("ok",false)){
                if(monitorPending){
                    if(identity->registeredPidReadError!=ERROR_SUCCESS||!identity->registeredPid){
                        saved.erase("liveValues");saved.erase("livePid");finish(false,false,"unknown","steam-session-starting");return;
                    }
                    auto accepted=ymcc::steamsettings::read(text);accepted.update(monitor);
                    saved["liveValues"]=accepted;
                    saved["livePid"]=identity->registeredPidReadError==ERROR_SUCCESS?identity->registeredPid:DWORD{0};
                }
                finish(true,false,"live","");return;
            }
            const bool defer=steamLiveCanDefer(result) && result.value("reason",std::string())!="steam-account-changed";
            finish(false,defer,defer?"deferred":"error",result.value("reason",std::string("live-readback-failed")));
            return; // Never patch Steam-owned files while its process is running.
        }
        if(sofSteamRunning()){finish(false,true,"deferred","steam-session-starting");return;}
        const auto output=ymcc::steamsettings::patch(text,monitor,overlay);
        if(sofSteamRunning()){finish(false,true,"deferred","steam-session-starting");return;}
        std::wstring current;std::string currentAccount;
        if(!sofFindLocalConfig(&current,&currentAccount) || current!=path || currentAccount!=account){finish(false,false,"error","steam-account-changed");return;}
        if(sgReadFile(path)!=text){finish(false,false,"error","localconfig-changed");return;}
        if(output!=text){
            if(!CopyFileW(path.c_str(),(path+L".ymcc-bak").c_str(),FALSE)){finish(false,false,"error","backup-failed");return;}
            if(sofSteamRunning()){finish(false,true,"deferred","steam-session-starting");return;}
            if(!sofFindLocalConfig(&current,&currentAccount) || current!=path || currentAccount!=account){finish(false,false,"error","steam-account-changed");return;}
            if(sgReadFile(path)!=text){finish(false,false,"error","localconfig-changed");return;}
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
static void steamSettingsQueue(const char* reason,bool prepareModuleProfile){
    if(g_aiFanMockSession.isolated)return;
    if(prepareModuleProfile)g_steamModuleProfilePreparationPending.store(true,std::memory_order_release);
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
        // This consumer takes the same original settings lock after prior writes release it.
        if(const auto consume=g_steamSettingsStartupConsumer.load(std::memory_order_acquire))consume();
        const bool prepareProfile=g_steamModuleProfilePreparationPending.exchange(false,std::memory_order_acq_rel);
        const bool clientPending=g_steamClientPending.load(),mousePending=g_sdmEventPending.load();
        // Release the original write lane before optional disk qualification.
        // Later real settings retain their original queue and lock ordering.
        g_sofQueued.store(false);g_steamSettingsQueued.store(false,std::memory_order_release);
        if(serial!=g_steamSettingsSerial.load(std::memory_order_acquire))steamSettingsQueue("user-followup");
        const char* preparation=prepareProfile?nativeSteamPrepareSessionModuleProfile():"not-attempted";
        if(prepareProfile)try{
            appendNativeLifecycleLog("steam-module-profile-preparation",{{"source",why},{"status",preparation},
                {"scope","session-module-profile-facts-only"}});
        }catch(...){} // Logging must not change the consumed request or its original queue.
        appendNativeLifecycleLog("steam-settings-batch",{{"source",why},{"clientPending",clientPending},{"mousePending",mousePending},
            {"moduleProfilePreparation",preparation}});
    })){g_sofQueued.store(false);g_steamSettingsQueued.store(false);}
}
static json steamSettingsGet(const json& args){
    if(!args.is_object()||(args.contains("scope")&&!args["scope"].is_string()))return steamMonitorUnknown("invalid-steam-scope");
    const auto scope=args.value("scope",std::string("monitor"));
    if(scope=="monitor")return steamMonitorGet();
    if(scope=="mouse")return steamDeckMouseGet();
    if(scope=="overlay")return steamOverlayFixGet();
    return {{"ok",false},{"reason","invalid-steam-scope"}};
}
static json steamMonitorApplyOwned(const json& patch,const std::function<bool()>& current){
    const auto requireOwner=[&]{if(!current||!current())throw std::runtime_error("BOOT_STEAM_MONITOR_REQUEST_EXPIRED");};
    requireOwner();
    if(!ymcc::steamsettings::valid(patch))return steamMonitorUnknown("invalid-monitor-settings");
    std::lock_guard<std::mutex> lock(g_steamSettingsMx);
    requireOwner();
    if(g_aiFanMockSession.isolated)return steamMonitorUnknown("isolated-session");
    const auto identity=g_steamSettingsObserver.identitySnapshot();
    const auto sameIdentity=[&]{return identity&&g_steamSettingsObserver.sameIdentityEpoch(identity);};
    if(!identity||!sameIdentity()||(identity->presence!=0&&identity->presence!=1)){
        g_steamSettingsObserver.requestIdentityRefresh();
        return steamMonitorUnknown("steam-session-starting");
    }
    if(identity->presence==0)return steamMonitorUnknown("steam-not-running");
    // Preserve the original readable, nonzero registered-PID admission. The
    // verified process can outlive a missing registry binding; that alone
    // must not broaden this boot settings writer's original permission.
    if(!identity->session.pid||!identity->session.created||identity->session.root.empty()||
       identity->registeredPidReadError!=ERROR_SUCCESS||!identity->registeredPid)return steamMonitorUnknown("steam-session-starting");
    const auto candidate=identity->accountReadError==ERROR_SUCCESS?identity->session.account:DWORD{0};
    std::wstring path;std::string account;
    if(identity->rootReadError!=ERROR_SUCCESS||identity->registryRoot.empty()||
       !sofFindLocalConfigAt(identity->registryRoot,candidate,&path,&account))return steamMonitorUnknown("localconfig-not-found");
    requireOwner();
    if(!sameIdentity())return steamMonitorUnknown("steam-session-changed");
    if(!steamMonitorAccountValid(account))return steamMonitorUnknown("invalid-steam-account");
    const auto sameSession=[&]{
        requireOwner();
        if(!sameIdentity())return false;
        std::wstring observedPath;std::string observedAccount;
        const bool found=sofFindLocalConfigAt(identity->registryRoot,candidate,&observedPath,&observedAccount);
        requireOwner();return found&&observedPath==path&&observedAccount==account&&sameIdentity();
    };
    const auto admit=[&]{return current&&current()&&sameSession();};
    const auto clientPatch=ymcc::steamsettings::clientPatch(patch,-1);
    json receipt;
    try{receipt=steamLiveRequest(static_cast<DWORD>(std::stoul(account)),{{"operation","settings.set"},{"values",clientPatch}},admit,sameIdentity);}
    catch(const std::exception&){requireOwner();return steamMonitorUnknown("invalid-steam-monitor-receipt");}
    requireOwner();
    if(!sameSession())return steamMonitorUnknown("steam-session-changed");
    if(!steamMonitorSetReceiptValid(receipt,clientPatch))return steamMonitorUnknown("invalid-steam-monitor-receipt");
    if(!receipt.value("ok",false))return steamMonitorUnknown(receipt.value("reason",std::string("live-application-unconfirmed")));
    json keys=json::array();for(const auto& field:ymcc::steamsettings::fields)keys.push_back(field.client);
    json observed;
    try{observed=steamLiveRequest(static_cast<DWORD>(std::stoul(account)),{{"operation","settings.get"},{"keys",keys}},admit,sameIdentity);}
    catch(const std::exception&){requireOwner();return steamMonitorUnknown("invalid-steam-monitor-receipt");}
    requireOwner();
    if(!sameSession())return steamMonitorUnknown("steam-session-changed");
    if(!steamMonitorLiveEnvelopeValid(observed)||!observed.value("ok",false)||!observed.contains("values"))return steamMonitorUnknown("live-readback-failed");
    json values=json::object();for(const auto& field:ymcc::steamsettings::fields)
        if(observed["values"].contains(field.client))values[field.name]=observed["values"][field.client];
    if(!steamMonitorValuesValid(values)||!ymcc::steamsettings::matches(values,patch))return steamMonitorUnknown("live-readback-failed");
    requireOwner();values.update({{"ok",true},{"available",true},{"pending",false},{"restartRequired",false},
        {"via","live"},{"liveConfirmed",true},{"applicationConfirmed",true}});
    return values;
}
static json steamSettingsSet(const json& args,const std::function<bool()>& current={}){
    if(!args.is_object())return steamMonitorUnknown("invalid-steam-settings");
    if(current){
        if(args.size()!=4||!args.contains("monitor")||args.value("awaitApplied",json())!=true||
           args.value("origin",json())!="boot-mode"||!args.contains("bootContext"))return steamMonitorUnknown("invalid-steam-settings");
        return steamMonitorApplyOwned(args.at("monitor"),current);
    }
    const bool awaited=args.contains("awaitApplied");
    if(awaited&&(!args.contains("monitor")||args.size()!=2||!args["awaitApplied"].is_boolean()))return {{"ok",false},{"reason","invalid-steam-settings"}};
    if(!awaited&&args.size()!=1)return {{"ok",false},{"reason","invalid-steam-settings"}};
    if(args.contains("overlayOffFix"))return ymcc::sleepprefs::retiredOverlayReply();
    if(args.contains("monitor"))return steamMonitorSet(args["monitor"],awaited&&args["awaitApplied"].get<bool>());
    if(args.contains("mousePercent"))return steamDeckMouseSet({{"percent",args["mousePercent"]}});
    return {{"ok",false},{"reason","invalid-steam-settings"}};
}
