inline Json read(mode_producer_port::IdentityObserver& observer, bool force = false, const std::function<bool()>& ownerCurrent = {},
    steamlive::ModeTransportWait modeWait = steamlive::ModeTransportWait::Immediate,
    steamlive::ModeTransportObservation* modeObservation = nullptr) {
    if(modeObservation)*modeObservation={};
    const auto identity = observer.identitySnapshot();
    const auto session = identity ? identity->session : steamsession::Session{};
    const int presence = identity ? identity->presence : -1;
    Json::object_t initial;
    initial.emplace("known", presence >= 0);
    initial.emplace("running", presence == 1);
    initial.emplace("mode", presence == 0 ? "stopped" : "unknown");
    initial.emplace("pid", session.pid);
    initial.emplace("birth", std::to_string(session.created));
    initial.emplace("reason", "");
    Json state(std::move(initial));
    const auto ownerAlive = [&]() noexcept { try { return !ownerCurrent || ownerCurrent(); } catch (...) { return false; } };
    const auto cancelled = [&] { state["known"] = false; state["reason"] = "shared-mode-read-cancelled"; return state; };
    if (!ownerAlive()) return cancelled();
    if (presence != 1 || !session.pid || session.root.empty()) return state;
    static std::mutex readMutex;
    static ReadBudget budget; static steamsession::Session cachedSession; static Json cached;
    static std::uint64_t cachedProcessEpoch=0,cachedAccountEpoch=0;
    std::unique_lock<std::mutex> readGuard(readMutex, std::try_to_lock);
    if (!readGuard.owns_lock()) { state["reason"] = "live-busy"; state["localReadBusy"] = true; return state; }
    if (!ownerAlive()) return cancelled();
    if (cachedSession.pid != session.pid || cachedSession.created != session.created || cachedSession.root != session.root) {
        cachedSession = session; cached = Json{}; budget.reset();
    }
    // Producer/account invalidation discards old UI proof without refunding
    // the existing automatic read budget. Only a real process/session change
    // above or the original explicit force request resets that allowance.
    if (cachedProcessEpoch!=identity->processEpoch || cachedAccountEpoch!=identity->accountEpoch) {
        cached=Json{};cachedProcessEpoch=identity->processEpoch;cachedAccountEpoch=identity->accountEpoch;
    }
    if (!budget.begin(GetTickCount64(), force)) {
        if (cached.is_object()) {
            // A cached UI proof still belongs to the original request and identity.
            // Refusal spends no new read slot and cannot trigger discovery.
            if (!ownerAlive()) return cancelled();
            if (!observer.sameIdentityEpoch(identity)) {
                state["known"] = false; state["reason"] = "steam-session-changed"; return state;
            }
            return cached;
        }
        state["reason"] = "ui-auto-query-paused"; return state;
    }
    const auto stillCurrent = [&] { return ownerAlive() && observer.sameIdentityEpoch(identity); };
    const auto result = steamlive::runOwned(observer,session.root, {{"operation", "ui.mode.get"}}, {}, 4000, stillCurrent, &session,
        false, nullptr, modeWait, modeObservation);
    // Preserve only this read's existing safe error before a late identity
    // invalidation returns. The state retains its original PID/birth; facts
    // never turn a failure into mode proof or alter the read budget.
    try{const auto failure=result.find("steamFailure");
        if(!result.value("ok",false)&&failure!=result.end()&&failure->is_object())
            state["steamFailure"]=steamfailure::normalize(*failure);
    }catch(...){} // Diagnostics must not affect the original read outcome.
    if (!ownerAlive()) { budget.finish(false); cached = Json{}; return cancelled(); }
    if (!stillCurrent()) { state["known"] = false; state["reason"] = "steam-session-changed"; budget.finish(false); cached = state; return state; }
    if (result.value("ok", false)) {
        const auto mode = result.value("mode", std::string("unknown"));
        if (mode == "desktop" || mode == "bigpicture") {
            state["mode"] = mode;
            const auto copyBoolean = [&](const char* field) {
                const auto value = result.find(field);
                if (value != result.end() && value->is_boolean()) state[field] = *value;
            };
            // Private read-only proof for Decky offline startup; no account is
            // adopted into the shared observer or any controller/settings owner.
            const auto account=result.find("observedAccount");
            if(account!=result.end()&&account->is_number_integer()&&
               (account->is_number_unsigned()?account->get<uint64_t>()>0&&account->get<uint64_t>()<=MAXDWORD:
                account->get<int64_t>()>0&&account->get<int64_t>()<=MAXDWORD))state["observedAccount"]=*account;
            copyBoolean("deckyAccountWatchAvailable");
            const auto epoch = result.value("modeEpoch", std::string{});
            if (epoch.size() <= 96) state["modeEpoch"] = epoch;
            copyBoolean("deckyFrontendLoaded");
            for(const char* field:{"deckyBackendConnected","deckyHooksInstalled","deckyClientConnected","deckyClientChecking","deckyClientVerified"})
                copyBoolean(field);
            const auto backendState=result.find("deckyBackendState");
            if(backendState!=result.end()&&backendState->is_string()&&deckyfrontend::backendState(backendState->get_ref<const std::string&>()))state["deckyBackendState"]=*backendState;
            const auto clientState=result.find("deckyClientState");
            if(clientState!=result.end()&&clientState->is_string()&&deckyfrontend::clientState(clientState->get_ref<const std::string&>()))state["deckyClientState"]=*clientState;
            const auto residue=ymcc::deckyupdate::residue(result.value("deckyUpdateResidue",Json()));
            if(!residue.empty())state["deckyUpdateResidue"]=residue;
            const auto empty=result.value("deckyEmptyFrontend",Json());
            if(empty.is_object()&&empty.size()==3&&empty.value("protocol",0)==1&&
                ymcc::deckyupdate::fingerprint(empty.value("frontendBuildId",std::string{}))&&
                ymcc::deckyupdate::loaderVersion(empty.value("loaderVersion",std::string{})))state["deckyEmptyFrontend"]=empty;
        }
        else state["reason"] = "steam-ui-mode-unsupported";
    } else state["reason"] = result.value("reason", std::string("live-read-unavailable"));
    budget.finish(state.value("known", false) && (state.value("mode", std::string{}) == "desktop" || state.value("mode", std::string{}) == "bigpicture"));
    cached = state; return state;
}
