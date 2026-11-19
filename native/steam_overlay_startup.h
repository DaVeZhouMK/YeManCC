#pragma once
// Explicit boot preference. Shares the existing Steam settings/file lane, but
// never reactivates the retired sleep overlay-off strategy or persistent retry.
static std::atomic<bool> g_steamOverlayStartupPending{false};
static bool steamOverlayStartupIdentityCurrent(const SteamSettingsIdentity& identity) noexcept {
    try {
        if(!identity)return false;
        const auto now=g_steamSettingsObserver.identitySnapshot();
        return now&&now->processEpoch==identity->processEpoch&&now->accountEpoch==identity->accountEpoch&&
            now->session==identity->session&&now->presence==identity->presence&&
            now->registryRoot==identity->registryRoot&&now->rootReadError==identity->rootReadError&&
            now->accountReadError==identity->accountReadError&&now->registeredPidReadError==identity->registeredPidReadError&&
            now->registeredPid==identity->registeredPid;
    }catch(...){return false;}
}
static bool steamOverlayStartupIdentityReady(const SteamSettingsIdentity& identity) noexcept {
    if(!identity||(identity->presence!=0&&identity->presence!=1))return false;
    if(identity->presence==0)return true; // File path still performs all original fresh absence checks.
    return identity->session.pid&&identity->session.created&&!identity->session.root.empty()&&
        identity->rootReadError==ERROR_SUCCESS&&!identity->registryRoot.empty()&&
        identity->accountReadError==ERROR_SUCCESS&&identity->session.account&&
        identity->registeredPidReadError==ERROR_SUCCESS&&identity->registeredPid==identity->session.pid;
}
static bool steamOverlayStartupEnabled(const json& startup) {
    if(!startup.is_object())return false;
    const auto value=startup.find("steamOverlayOnBoot");
    return value==startup.end() || *value==true; // Missing preference uses the product default; explicit OFF wins.
}
static bool steamOverlayStartupAttempt(bool beforeSteamStart,const SteamSettingsIdentity& expectedIdentity={}) {
    if(g_aiFanMockSession.isolated || g_exitRequested.load())return true;
    const auto boot=bootModeStartupIdentity();
    if(boot.empty())return true;
    if(beforeSteamStart && sofSteamRunning())return true;
    const auto identity=beforeSteamStart?SteamSettingsIdentity{}:expectedIdentity?expectedIdentity:g_steamSettingsObserver.identitySnapshot();
    const auto identityCurrent=[&]{return steamOverlayStartupIdentityCurrent(identity);};
    if(!beforeSteamStart&&(!steamOverlayStartupIdentityReady(identity)||!identityCurrent()))return false;
    // Preserve the original fresh absence boundary before spending this boot
    // opportunity when the publisher's negative fact may precede a new Steam.
    if(!beforeSteamStart&&identity->presence==0&&sofSteamRunning())return false;
    bool identityLostBeforeClaim=false;
    bool claimed=false,fileFallback=false;
    json retryTarget;
    const bool saved=ymSettingsWriteSection("startupDesired",json::object(),[&](json& next,json& all){
        if(!beforeSteamStart&&!identityCurrent()){identityLostBeforeClaim=true;return false;}
        const auto current=all.find("startupDesired");
        const auto prefs=current==all.end()?json::object():*current;
        if(!steamOverlayStartupEnabled(prefs))return false;
        if(prefs.value("steamOverlayAppliedBoot",json())==boot){
            // An explicitly unmutated unavailable Live request may use ONE
            // later pre-launch opportunity. Unknown/lost replies never replay.
            const auto previous=prefs.value("steamOverlayStartupResult",json());
            if(!beforeSteamStart || !previous.is_object() || previous.value("boot",json())!=boot ||
               previous.value("safeFileRetry",json())!=true ||
               prefs.value("steamOverlayFileFallbackBoot",json())==boot)return false;
            retryTarget=previous;fileFallback=true;
        }
        next=prefs;next["steamOverlayAppliedBoot"]=boot;
        if(fileFallback)next["steamOverlayFileFallbackBoot"]=boot;
        claimed=true;return true;
    });
    if(!saved || !claimed)return !identityLostBeforeClaim; // Unknown ownership preserves the initial opportunity; claimed work never replays.
    const auto current=[&]{
        if(g_exitRequested.load() || g_aiFanMockSession.isolated || (!beforeSteamStart&&!identityCurrent()))return false;
        const auto prefs=ymSettingsSection("startupDesired");
        return steamOverlayStartupEnabled(prefs) && prefs.value("steamOverlayAppliedBoot",json())==boot &&
            (prefs.value("steamOverlayFileFallbackBoot",json())==boot)==fileFallback;
    };
    auto result=json{{"ok",false},{"via","none"},{"reason","startup-cancelled"},{"pending",false},{"boot",boot},
        {"safeFileRetry",false},{"fileFallback",fileFallback},{"liveConfirmed",false},{"persisted",false},
        {"scope","client-setting"},{"gameOverlayVerified",false}};
    try {
        std::lock_guard<std::mutex> lane(g_steamSettingsMx);
        std::wstring path;std::string account;
        if(!current())return true;
        const bool running=beforeSteamStart?sofSteamRunning():identity->presence==1;
        const auto resolveTarget=[&](std::wstring* output,std::string* user){return running&&!beforeSteamStart?steamSettingsIdentityTarget(identity,output,user):sofFindLocalConfig(output,user);};
        if(!resolveTarget(&path,&account) || !steamMonitorAccountValid(account))result["reason"]="localconfig-not-found";
        else if(fileFallback && (retryTarget.value("targetAccount",json())!=account || retryTarget.value("targetPath",json())!=W2U(path)))
            result["reason"]="steam-account-changed";
        else {
            result["targetAccount"]=account;result["targetPath"]=W2U(path);
            const auto sameTarget=[&]{
                std::wstring observedPath;std::string observedAccount;
                return current() && resolveTarget(&observedPath,&observedAccount) && observedPath==path && observedAccount==account;
            };
            if(running && beforeSteamStart)result["reason"]="steam-session-starting";
            else if(running){
                const auto activeUser=identity->session.account;
                if(!identityCurrent()||!steamOverlayStartupIdentityReady(identity)||std::to_string(activeUser)!=account)result["reason"]="steam-account-unconfirmed";
                else {
                    const auto admit=[&]{return sameTarget()&&identityCurrent();};
                    const auto pureOwner=[&]{return !g_exitRequested.load()&&!g_aiFanMockSession.isolated&&identityCurrent();};
                    const json values={{"enable_overlay",true}};
                    const auto reply=steamLiveRequest(activeUser,{{"operation","settings.set"},{"values",values}},admit,pureOwner);
                    const bool valid=steamMonitorSetReceiptValid(reply,values);
                    if(admit() && valid && reply.value("ok",false)){
                        // Read the setting independently: a SET ACK alone does
                        // not prove the client kept the value, nor game injection.
                        const auto observed=steamLiveRequest(activeUser,{{"operation","settings.get"},{"keys",json::array({"enable_overlay"})}},admit,pureOwner);
                        const bool confirmed=admit() && steamMonitorSetReceiptValid(observed,values) && observed.value("ok",false);
                        result.update({{"ok",confirmed},{"via","live"},{"liveConfirmed",confirmed},
                            {"reason",confirmed?"":"live-readback-failed"}});
                        if(confirmed){
                            try{result["persisted"]=admit() && ymcc::steamsettings::overlayValue(sgReadFile(path))==1;}catch(...){}
                        }
                    }else{
                        result.update({{"via","live"},{"reason",valid?reply.value("reason",std::string("live-readback-failed")):"invalid-steam-monitor-receipt"}});
                        const auto why=valid?reply.value("reason",std::string()):std::string();
                        const bool unavailable=why=="live-debug-unavailable" || why=="live-context-unavailable" ||
                            why=="live-api-unavailable" || why=="live-read-unavailable" || why=="live-busy" || why=="steam-session-starting";
                        result["safeFileRetry"]=!fileFallback && admit() && valid && reply.value("ok",json())==false &&
                            reply.value("mutated",json())==false && unavailable;
                    }
                }
            } else {
                const auto text=sgReadFile(path);
                if(text.empty())result["reason"]="read-failed";
                else {
                    const auto output=ymcc::steamsettings::patch(text,json::object(),1);
                    const auto writable=[&]{return sameTarget() && !sofSteamRunning() && sgReadFile(path)==text;};
                    if(!writable())result["reason"]="startup-context-changed";
                    else if(output!=text && !CopyFileW(path.c_str(),(path+L".ymcc-bak").c_str(),FALSE))result["reason"]="backup-failed";
                    else if(!writable())result["reason"]="startup-context-changed";
                    else if(output!=text && !sgWriteFileAtomic(path,output))result["reason"]="write-failed";
                    else {
                        const auto readback=sgReadFile(path);
                        const bool stable=sameTarget() && !sofSteamRunning();
                        const bool confirmed=stable && readback==output && ymcc::steamsettings::overlayValue(readback)==1;
                        result.update({{"ok",confirmed},{"via","file"},{"persisted",confirmed},
                            {"reason",confirmed?"":stable?"readback-failed":"startup-context-changed"}});
                    }
                }
            }
        }
    } catch(const std::exception&){result.update({{"ok",false},{"reason","startup-application-unconfirmed"}});}
    (void)ymSettingsWriteSection("startupDesired",json::object(),[&](json& next,json& all){
        const auto prefs=all.find("startupDesired");
        if(prefs==all.end() || !prefs->is_object() || prefs->value("steamOverlayAppliedBoot",json())!=boot ||
           (prefs->value("steamOverlayFileFallbackBoot",json())==boot)!=fileFallback)return false;
        next=*prefs;next["steamOverlayStartupResult"]=result;return true;
    });
    auto diagnostic=result;diagnostic.erase("targetPath");diagnostic.erase("targetAccount");
    appendNativeLifecycleLog("steam-overlay-startup",diagnostic);
    return true;
}
[[maybe_unused]] static void steamOverlayStartupBeforeSteam() {
    try {steamOverlayStartupAttempt(true);}catch(...) {} // Never block a separately authorized Steam launch.
}
// The original shared settings worker is the sole consumer. Source callbacks
// only wake that existing queue; initial Unknown never consumes the boot claim.
static void steamOverlayStartupConsumeQueued() noexcept {
    if(!g_steamOverlayStartupPending.load(std::memory_order_acquire))return;
    try {
        const auto identity=g_steamSettingsObserver.identitySnapshot();
        if(!steamOverlayStartupIdentityReady(identity)||!steamOverlayStartupIdentityCurrent(identity))return;
        if(!steamOverlayStartupAttempt(false,identity))return;
    }catch(...){} // An uncertain claimed mutation is never replayed.
    g_steamOverlayStartupPending.store(false,std::memory_order_release);
    g_steamSettingsStartupConsumer.store(nullptr,std::memory_order_release);
}
static void steamOverlayStartupIdentityPublished() {
    if(!g_steamOverlayStartupPending.load(std::memory_order_acquire)||g_exitRequested.load()||g_aiFanMockSession.isolated)return;
    const auto identity=g_steamSettingsObserver.identitySnapshot();
    if(steamOverlayStartupIdentityReady(identity)&&steamOverlayStartupIdentityCurrent(identity))steamSettingsQueue("overlay-startup-source");
}
static void steamOverlayStartupInitialize() {
    if(g_aiFanMockSession.isolated || g_exitRequested.load())return;
    const auto preference=ymSettingsField("startupDesired","steamOverlayOnBoot");
    if(!preference.is_null() && preference!=true)return;
    g_steamSettingsStartupConsumer.store(steamOverlayStartupConsumeQueued,std::memory_order_release);
    g_steamOverlayStartupPending.store(true,std::memory_order_release);
    steamOverlayStartupIdentityPublished();
}
