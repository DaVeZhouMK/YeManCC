// Include after pipe_cases.inc (and before the caller in fixture.cpp).
// Deliberately strict regressions for protected-cycle-other-recovery-audit37
// sections 2-4. Run only in the existing native/inert fixture, never full-pipe.
// No runner registration, independent lifecycle model, new clock, or real I/O.
// Expected on the audited production code: stale admission mutates recovery
// state; deferred work adopts a replacement epoch; already-owned Stop repeats;
// an ordinary current-tuple reopen bypasses the central inspection/reservation.
static void protectedTupleRecoveryCases() {
    if (fixtureFullPipe) throw std::logic_error("tuple cases require native owned-event fixture");

    const auto stopCount = []() {
        return static_cast<unsigned>(std::count_if(g_startEntrySeam.trace.begin(),
            g_startEntrySeam.trace.end(), [](const auto& s) { return s.rfind("stop:", 0) == 0; }));
    };
    const auto createCount = []() {
        return static_cast<unsigned>(std::count(g_startEntrySeam.trace.begin(),
            g_startEntrySeam.trace.end(), "create-process"));
    };
    const auto queueView = []() {
        std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);
        json result = json::array();
        for (const auto& reason : g_inputHostLifecycleWorkQueue) result.push_back(reason);
        return result;
    };
    const auto budgetView = []() {
        return json{{"episode", g_inputHostRecoveryEpisodeAttempts.load()},
            {"rolling0", g_inputHostRecoveryRolling[0].load()},
            {"rolling1", g_inputHostRecoveryRolling[1].load()},
            {"rollingNext", g_inputHostRecoveryRollingNext.load()},
            {"lastStart", g_inputHostRecoveryLastStartTick.load()},
            {"terminal", g_inputHostRecoveryTerminalLogged.load()},
            {"fallbackState", g_inputHostRecoveryFallbackState.load()}};
    };
    const auto recoveryView = [&]() {
        return json{{"budget", budgetView()},
            {"faultHold", g_inputHostFaultHoldsStart.load()},
            {"pending", g_inputHostRecoveryPending.load()},
            {"deferredUntil", g_inputHostRecoveryDeferredUntil.load()},
            {"tupleHeal", g_inputHostTupleHealPending.load()}};
    };
    const auto protectionView = []() {
        const auto v = g_virtualStartProtection.view(startProtectionClock);
        return json{{"requestId", v.requestId}, {"persona", v.persona},
            {"phase", v.phase}, {"locked", v.locked}, {"elapsedMs", v.elapsedMs},
            {"remainingMs", v.remainingMs}, {"candidateOutcome", v.candidateOutcome},
            {"verificationRemainingMs", v.verificationRemainingMs},
            {"fallbackAttempts", v.fallbackAttempts}};
    };
    const auto ownerView = []() {
        const auto& n = ymcc::inputHost::switchExecutionReturnRuntime::state;
        const auto& r = g_inputHostSwitchRecovery;
        return json{{"normal", {{"requestId", n.requestId}, {"power", n.power},
                {"oldEpoch", n.oldEpoch}, {"oldRunId", n.oldRunId}, {"persona", n.persona},
                {"pending", n.pending}, {"returned", n.returned}, {"queued", n.queued},
                {"atomicPending", g_inputHostSwitchExecutionReturnPending.load()}}},
            {"recovery", {{"requestId", r.requestId}, {"power", r.power},
                {"hostEpoch", r.hostEpoch}, {"deadline", r.deadline},
                {"stoppedRunId", r.stoppedRunId}, {"pending", r.pending},
                {"queued", r.queued}, {"spent", r.spent},
                {"stopInvoked", r.stopInvoked}, {"executionCleanup", r.executionCleanup},
                {"awaitingCleanup", r.awaitingCleanup},
                {"atomicPending", g_inputHostSwitchRecoveryPending.load()}}}};
    };
    const auto readCurrent = []() {
        auto sample = fullPipeReadNow(1);
        std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);
        const auto previous = inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot, true);
        sample.readContinuityGeneration = ymcc::input::recoveryHealth::nextContinuity(
            previous, inputHostRecoveryHealthSource(&sample, true), GetTickCount64());
        g_inputCaptureGamepadSnapshot = sample;
        return sample;
    };
    const auto drain = [&]() {
        unsigned turns = 0;
        while (g_inputHostLifecycleWorkRequested.load() && turns++ < 16) {
            // Same existing due boundary as fullPipeDrainQueuedLifecycle().
            // Do not let a preceding persona token strand the tuple behind it.
            g_inputHostBuildSettleUntilTick = 0;
            inputHostRunQueuedLifecycleWork();
        }
        check(!g_inputHostLifecycleWorkRequested.load() && !g_inputHostLifecycleWorkInFlight.load(),
            "tuple work completes on the serialized owner within sixteen dispatches");
    };
    const auto queueTuple = [&](unsigned long long epoch) {
        check(epoch != 0, "injected worker token names an actual nonzero Host epoch");
        std::string reason;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);reason=inputHostTupleRecoveryReason(inputHostTupleRecoveryTokenLocked(epoch,g_inputHostSequence));}
        check(inputHostQueueLifecycleWork(reason.c_str()), "actual lifecycle queue accepts tuple token");
        const auto queued = queueView();
        check(std::find(queued.begin(), queued.end(), json(reason)) != queued.end(),
            "exact epoch-bearing token is observed before actual dispatch");
    };
    const auto prepare = [&](const std::string& name) {
        cases.push_back({{"name", name}});
        seed();
        // Same existing source/process boundaries as switchRecoveryCases().
        g_machineIdentity.family = YmccFamily::AsusRogAllyX;
        g_machineIdentity.manufacturer = "ASUSTEK COMPUTER INC.";
        g_machineIdentity.product = "ROG Ally X";
        g_msiDinput.enabled = false; g_msiDinputSourceValid = false;
        g_inputReady = true; g_powerLifecycle = PowerLifecycle::Ready;
        g_inputHostPowerAdmissionRevoked = false; g_externalPriorityVirtualSuspended = false;
        g_shortcutRecordingActive = false; g_inputCaptureStop = false;
        g_inputHostRecoveryEpisodeAttempts = 0; g_inputHostRecoveryLastStartTick = 0;
        g_inputHostRecoveryRolling[0] = 0; g_inputHostRecoveryRolling[1] = 0;
        g_inputHostRecoveryRollingNext = 0; g_inputHostRecoveryTerminalLogged = false;
        g_inputHostRecoveryPending = false; g_inputHostRecoveryDeferredUntil = 0;
        g_inputHostRecoveryIntentReset = false; g_inputHostTupleHealPending = false;
        g_inputHostRecoveryFallbackState = 0; g_inputHostRecoveryHealth.clear();
        g_inputHostRecoveryHealthWasTracking = false; g_inputHostHealthySinceTick = 0;
        { std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);
            g_inputHostLifecycleWorkQueue.clear(); g_inputHostLifecycleWorkRequested = false; }
        check(!g_inputHostLifecycleWorkInFlight.load(), "fixture begins without another lifecycle owner");
        check(sgWriteFileAtomic(YM_SETTINGS_FILE, targetDocument("steamdeck", 1).dump()),
            "tuple case uses the existing private settings file");
        const auto admitted = g_virtualStartProtection.admit("steamdeck", startProtectionClock);
        check(admitted.accepted && admitted.requestId != 0, "one real protection request owns the fixture");
        fixtureXinputSourceLive = true; fixtureFrameFirstAck = false;
        g_xinputAdmittedMask = 1; g_xinputPhysicalSourceMask = 1; g_xinputConnectedMask = 1;
        readCurrent();
        check(inputHostStart(), "real Start creates the first native owned-event Host");
        check(g_inputHostProcess && g_inputHostEpoch && !g_inputHostRunId.empty() &&
                WaitForSingleObject(g_inputHostProcess, 0) == WAIT_TIMEOUT,
            "actual Host tuple is bound to a live unsignaled fixture-owned event");
        cases.back()["lifecycleVersion"] = ymcc::VirtualStartProtection::lifecycleVersion;
    };
    const auto replaceHost = [&]() {
        const auto oldEpoch = g_inputHostEpoch;
        const auto oldRun = g_inputHostRunId;
        inputHostStop("offline-owned-tuple-replacement");
        check(!g_inputHostProcess && !g_inputHostReleaseQuarantined && !g_inputHostStartBlocked,
            "real replacement Stop completes old ownership before another Start");
        readCurrent();
        check(inputHostStart(), "real replacement Start succeeds through current production gates");
        check(g_inputHostProcess && g_inputHostEpoch != oldEpoch && g_inputHostEpoch != 0 &&
                g_inputHostRunId != oldRun && WaitForSingleObject(g_inputHostProcess, 0) == WAIT_TIMEOUT,
            "replacement owns a distinct actual epoch and run with a live event");
        cases.back()["oldEpoch"] = oldEpoch;
        cases.back()["replacementEpoch"] = g_inputHostEpoch;
        return oldEpoch;
    };
    const auto beginInspection = [&]() {
        fixtureFrameFirstAck = true;
        for (int n = 0; n < kInputHostFreshNeutralFramesRequired + 1; ++n) {
            auto sample = readCurrent(); fullPipeSubmitSource(sample, true, false);
        }
        const auto v = g_virtualStartProtection.view(startProtectionClock);
        check(v.phase == "verifying" && v.locked && v.candidateOutcome == "success" &&
                v.verificationRemainingMs == ymcc::VirtualStartProtection::settleMs,
            "actual accepted frame candidate begins the original ten-second inspection");
        return v;
    };
    const auto finishCase = [&]() {
        cases.back()["observedRecovery"] = recoveryView();
        cases.back()["observedOwners"] = ownerView();
        cases.back()["observedProtection"] = protectionView();
        cases.back()["events"] = fixtureEvents;
        // Teardown observations only, after assertions: prove exit of our own
        // fake process event and let the real Stop/return code release it.
        g_inputHostPowerAdmissionRevoked = false; g_powerLifecycle = PowerLifecycle::Ready;
        g_inputReady = true; g_shortcutRecordingActive = false;
        g_startEntrySeam.hostExitWait = WAIT_OBJECT_0;
        g_startEntrySeam.msiSdReleaseAbsent = true; g_startEntrySeam.msiSdProofSucceeded = true;
        g_xinputPhysicalSourceLocked = true; g_xinputPhysicalSourceMask = 1; g_xinputConnectedMask = 1;
        if (g_inputHostProcess) check(SetEvent(g_inputHostProcess) != FALSE,
            "teardown signals only the fixture-owned Host event");
        cases.back()["cleanup"] = cleanup();
        check(!g_inputHostProcess, "teardown retires the owned event before the next seed");
        if (g_inputHostProcess) throw std::logic_error("tuple fixture retained owned event at teardown");
        fixtureFrameFirstAck = false; fixtureXinputSourceLive = false;
    };

    // 1. Staleness must be checked before budget, cooldown, or power branches.
    // Two real Starts supply E_old/E_new; no fabricated epoch counter is used.
    for (const std::string mode : {"episode-exhausted", "rolling-exhausted", "cooldown",
            "power-revoked", "power-not-ready", "power-revoked-existing-pending"}) {
        prepare("protected-tuple/stale-" + mode);
        const auto oldEpoch = replaceHost();
        const auto now = GetTickCount64();
        if (mode == "episode-exhausted") g_inputHostRecoveryEpisodeAttempts = kRecoveryEpisodeMax;
        if (mode == "rolling-exhausted") {
            g_inputHostRecoveryRolling[0] = now; g_inputHostRecoveryRolling[1] = now;
        }
        if (mode == "cooldown") g_inputHostRecoveryLastStartTick = now;
        if (mode.rfind("power-revoked", 0) == 0) g_inputHostPowerAdmissionRevoked = true;
        if (mode == "power-not-ready") g_powerLifecycle = PowerLifecycle::Suspending;
        if (mode == "power-revoked-existing-pending") {
            // Preserve another current pending item exactly, including its due tick.
            // It is established by the actual current-token consumer, not a boolean seed.
            queueTuple(g_inputHostEpoch); drain();
            check(g_inputHostRecoveryPending.load() || g_inputHostSwitchRecoveryPending.load(), "current revoked-power token creates pending work");
        }
        const auto host = g_inputHostProcess;
        const auto before = recoveryView(), local = state(), protection = protectionView();
        const auto stops = stopCount(), creates = createCount();
        cases.back()["beforeRecovery"] = before;
        queueTuple(oldEpoch); drain();
        check(recoveryView() == before,
            "stale epoch changes no current budget rolling cooldown terminal hold pending due tick or tuple heal");
        check(stopCount() == stops && createCount() == creates && g_inputHostProcess == host &&
                state() == local && WaitForSingleObject(host, 0) == WAIT_TIMEOUT,
            "stale epoch cannot Stop terminate clear or replace the current live target");
        check(protectionView() == protection && queueView().empty(),
            "stale epoch creates no physical-fallback job and does not alter the player cycle");
        finishCase();
    }

    // 2. First accept a VALID E_old deferral. Then genuinely replace the Host.
    // Advancing only the existing due inputs avoids sleeping or a shadow clock.
    for (const std::string mode : {"cooldown", "power-revoked", "power-not-ready"}) {
        prepare("protected-tuple/deferred-current-epoch-never-adopts-replacement/" + mode);
        const auto prior=beginInspection();startProtectionClock+=prior.verificationRemainingMs;readCurrent();
        {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
        check(!g_virtualStartProtection.view(startProtectionClock).locked,"generic deferred tuple starts after actual player final");
        const auto deferredEpoch = g_inputHostEpoch;
        if (mode == "cooldown") g_inputHostRecoveryLastStartTick = GetTickCount64();
        if (mode == "power-revoked") g_inputHostPowerAdmissionRevoked = true;
        if (mode == "power-not-ready") g_powerLifecycle = PowerLifecycle::Suspending;
        const auto firstBudget = budgetView(); const auto firstStops = stopCount();
        queueTuple(deferredEpoch); drain();
        check(g_inputHostRecoveryPending.load() && !g_inputHostTupleHealPending.load() &&
                budgetView() == firstBudget && stopCount() == firstStops,
            "valid current token is deferred without Stop or budget charge");
        cases.back()["deferredRecovery"] = recoveryView();
        g_inputHostPowerAdmissionRevoked = false; g_powerLifecycle = PowerLifecycle::Ready;
        replaceHost();
        check(g_inputHostEpoch != deferredEpoch, "pending token is now strictly older than the actual Host");
        g_inputHostRecoveryLastStartTick = 0; g_inputHostRecoveryDeferredUntil = 0;
        const auto host = g_inputHostProcess;
        const auto before = budgetView(), local = state(), protection = protectionView();
        const auto stops = stopCount(), creates = createCount();
        inputHostRecoveryMaintenanceTick();
        const auto maintenanceQueue = queueView();
        cases.back()["maintenanceQueue"] = maintenanceQueue;
        const auto forbidden = "tuple-mismatch-reopen:" + std::to_string(g_inputHostEpoch);
        check(std::none_of(maintenanceQueue.begin(), maintenanceQueue.end(), [&](const auto& item){const auto token=item.template get<std::string>();return token==forbidden || token.rfind(forbidden+":",0)==0;}),
            "maintenance never rewrites a valid old deferral into the replacement epoch");
        drain();
        inputHostRecoveryMaintenanceTick(); drain();
        check(budgetView() == before && !g_inputHostTupleHealPending.load() &&
                !g_inputHostFaultHoldsStart.load(),
            "retired deferral cannot charge or hold the replacement Host on repeated maintenance");
        check(stopCount() == stops && createCount() == creates && g_inputHostProcess == host &&
                state() == local && WaitForSingleObject(host, 0) == WAIT_TIMEOUT,
            "maintenance of a retired deferral never Stops or replaces the new actual target");
        check(protectionView() == protection, "retired deferral cannot change the current request or its deadlines");
        finishCase();
    }

    // 3a. Use R3's actual player transition and normal delayed-return Stop owner.
    {
        prepare("protected-tuple/normal-return-owner-refuses-duplicate-stop");
        const auto candidate = beginInspection();
        startProtectionClock += candidate.verificationRemainingMs;
        readCurrent();
        { std::lock_guard<InputHostMutex> lock(g_inputHostMx);
            inputHostSwitchVerificationMaintenanceLocked(); }
        check(g_virtualStartProtection.view(startProtectionClock).phase == "success" &&
                !g_virtualStartProtection.view(startProtectionClock).locked,
            "original actual target completes verification before admitting the normal player switch");
        reg_fs();
        const auto beforeSettings = json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
        check(writeTarget(beforeSettings, targetDocument("dualsense-edge", 2)).value("ok", false),
            "real settings admission owns the normal persona switch");
        inputCaptureRefreshRealStickSettings(true);
        const auto epoch = g_inputHostEpoch; const auto run = g_inputHostRunId;
        g_startEntrySeam.hostExitWait = WAIT_TIMEOUT;
        check(!inputHostStart(), "normal transition waits for actual old Host exit");
        const auto& n = ymcc::inputHost::switchExecutionReturnRuntime::state;
        check(n.pending && !n.returned && n.oldEpoch == epoch && n.oldRunId == run &&
                g_inputHostSwitchExecutionReturnPending.load() && g_inputHostProcess &&
                WaitForSingleObject(g_inputHostProcess, 0) == WAIT_TIMEOUT && stopCount() == 1,
            "real normal-return path already owns exactly one Stop of the retained old tuple");
        g_inputHostRecoveryLastStartTick = 0;
        const auto host = g_inputHostProcess;
        const auto before = recoveryView(), owners = ownerView(), local = state(), protection = protectionView();
        const auto stops = stopCount(), creates = createCount();
        queueTuple(epoch); drain();
        check(stopCount() == stops && createCount() == creates && g_inputHostProcess == host,
            "tuple token cannot repeat normal-return owner's Stop or start another Host");
        check(recoveryView() == before && ownerView() == owners && state() == local,
            "tuple duplicate preserves normal-return proof owner budget hold and pending work exactly");
        check(protectionView() == protection, "normal-return tuple duplicate cannot charge fallback or renew the cycle");
        finishCase();
    }

    // 3b. Cover both uncharged execution cleanup and an already-reserved fallback.
    for (const bool spent : {false, true}) {
        prepare(spent ? "protected-tuple/spent-switch-recovery-owner-refuses-duplicate-stop" :
            "protected-tuple/execution-cleanup-owner-refuses-duplicate-stop");
        const auto candidate = beginInspection();
        if (spent) startProtectionClock += candidate.verificationRemainingMs;
        else startProtectionClock += 500;
        const auto epoch = g_inputHostEpoch; const auto run = g_inputHostRunId;
        g_startEntrySeam.hostExitWait = WAIT_TIMEOUT;
        fixtureFrameFirstAck = false; g_startEntrySeam.pipeFailCommand = "SUBMIT_FRAME";
        fixtureStartupTransportError = ERROR_BROKEN_PIPE;
        auto sample = readCurrent(); sample.pad.wButtons = XINPUT_GAMEPAD_A;
        fullPipeSubmitSource(sample, true, false);
        check(g_inputHostTransportFault, "real Submit fault supplies the existing classified cleanup producer");
        g_startEntrySeam.pipeFailCommand.clear(); fixtureStartupTransportError = ERROR_SUCCESS;
        fixtureFrameFirstAck = true;
        drain();
        const auto& r = g_inputHostSwitchRecovery;
        check(r.pending && r.stopInvoked && r.awaitingCleanup && r.hostEpoch == epoch &&
                r.stoppedRunId == run && r.spent == spent && r.executionCleanup == !spent &&
                g_inputHostProcess && WaitForSingleObject(g_inputHostProcess, 0) == WAIT_TIMEOUT &&
                stopCount() == 1 && createCount() == 1,
            "actual fault dispatch retains the live old tuple under exactly one established Stop owner");
        check(g_virtualStartProtection.view(startProtectionClock).fallbackAttempts == (spent ? 1u : 0u) &&
                g_inputHostRecoveryEpisodeAttempts.load() == (spent ? 1 : 0),
            "actual owner has the exact expected central reservation and public charge");
        // Make only the existing cooldown due; do not clear ownership or charges.
        g_inputHostRecoveryLastStartTick = 0;
        const auto host = g_inputHostProcess;
        const auto before = recoveryView(), owners = ownerView(), local = state(), protection = protectionView();
        const auto stops = stopCount(), creates = createCount();
        queueTuple(epoch); drain();
        check(stopCount() == stops && createCount() == creates && g_inputHostProcess == host,
            "tuple cannot repeat Stop already owned by execution cleanup or switch recovery");
        check(recoveryView() == before && ownerView() == owners && state() == local,
            "tuple duplicate changes no charge rolling record hold pending or exact stopped-tuple proof");
        check(protectionView() == protection,
            "duplicate tuple preserves zero or one central reservation and the original request clock");
        finishCase();
    }

    // 4. A non-transport tuple fault is not permission for an early safety Stop.
    // Independent cases keep the 500ms and 9999ms boundaries observable even
    // when the first boundary fails against old production.
    for (const unsigned elapsed : {500u, 9999u}) {
        prepare("protected-tuple/current-fault-inspection-boundary/" + std::to_string(elapsed));
        const auto candidate = beginInspection();
        startProtectionClock += elapsed; readCurrent();
        const auto host = g_inputHostProcess; const auto epoch = g_inputHostEpoch;
        const auto before = recoveryView(); const auto stops = stopCount(), creates = createCount();
        queueTuple(epoch); drain();
        const auto v = g_virtualStartProtection.view(startProtectionClock);
        check(v.requestId == candidate.requestId && v.verificationRemainingMs ==
                ymcc::VirtualStartProtection::settleMs - elapsed && v.fallbackAttempts == 0,
            "current tuple fault before ten seconds preserves the first inspection deadline with no reservation");
        check(stopCount() == stops && createCount() == creates && g_inputHostProcess == host &&
                g_inputHostEpoch == epoch && WaitForSingleObject(host, 0) == WAIT_TIMEOUT,
            "current tuple fault at500 or9999 cannot Stop reopen or replace before inspection completes");
        check(budgetView() == before["budget"] && !g_inputHostTupleHealPending.load(),
            "pre-inspection tuple work cannot spend public recovery or claim a destructive heal");
        finishCase();
    }
    // Same actual accepted-report path must retire an unspent repair. Neither a
    // queue delay nor idle timer may turn an already repaired tuple into a Stop.
    for(const bool acceptBeforeWorker:{true,false}){
        prepare(acceptBeforeWorker?"protected-tuple/fresh-report-before-queued-worker":"protected-tuple/fresh-report-during-inspection");
        const auto candidate=beginInspection();startProtectionClock+=500;readCurrent();
        const auto epoch=g_inputHostEpoch;const auto stops=stopCount(),creates=createCount();
        std::string token;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);token=inputHostTupleRecoveryReason(inputHostTupleRecoveryTokenLocked(epoch,g_inputHostSequence));}
        check(inputHostQueueLifecycleWork(token.c_str()),"current observed tuple fault uses the exact captured production token");
        if(!acceptBeforeWorker){drain();check(g_inputHostSwitchRecoveryPending.load(),"actual tuple worker retains unspent classified fault through inspection");}
        auto fresh=readCurrent();const auto ack=fullPipeSubmitSource(fresh,true,false);
        check(ack.value("hostSubmission","")=="frame-accepted","natural tuple recovery uses an actual accepted current-source report");
        drain();
        check(!g_inputHostSwitchRecoveryPending.load()&&!g_inputHostSwitchRecovery.pending&&!g_inputHostTupleFault.epoch,"a newer accepted report cancels old unspent tuple work before or after worker admission");
        startProtectionClock+=g_virtualStartProtection.view(startProtectionClock).verificationRemainingMs;readCurrent();
        {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
        const auto final=g_virtualStartProtection.view(startProtectionClock);
        check(final.requestId==candidate.requestId&&final.phase=="success"&&!final.locked&&final.fallbackAttempts==0&&g_inputHostRecoveryEpisodeAttempts.load()==0&&stopCount()==stops&&createCount()==creates,"natural tuple return finalizes once without destructive repair or fallback charge");
        finishCase();
    }

    // Recovery can have been observed in ordinary steady state before the next
    // actual OFF/ON admission. The same Host may remain during blocked cleanup;
    // this old unprotected token cannot bypass the new player's protection.
    for(const bool deferred:{false,true}){
        prepare(deferred?"cross-cycle-tuple/steady-deferred-before-new-player":"cross-cycle-tuple/steady-queued-before-new-player");
        const auto candidate=beginInspection();startProtectionClock+=candidate.verificationRemainingMs;readCurrent();
        {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
        check(!g_virtualStartProtection.view(startProtectionClock).locked,"steady tuple capture follows a real successful original cycle");
        std::string oldToken;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);const auto token=inputHostTupleRecoveryTokenLocked(g_inputHostEpoch,g_inputHostSequence);check(!token.protectedCycle,"original tuple task is captured outside player protection");oldToken=inputHostTupleRecoveryReason(token);}
        if(deferred){g_inputHostPowerAdmissionRevoked=true;inputHostPerformTupleRecovery(oldToken);check(g_inputHostRecoveryPending.load(),"actual current steady-state worker retains its own power deferral");g_inputHostPowerAdmissionRevoked=false;}
        const auto on=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));const auto off=targetDocument("disabled",2),next=targetDocument("steamdeck",3);
        check(writeTarget(on,off).value("ok",false),"completed original cycle admits actual player OFF");inputCaptureRefreshRealStickSettings(true);
        const auto closing=g_virtualStartProtection.view(startProtectionClock);check(closing.persona=="disabled"&&closing.phase=="pending","actual OFF waits for its own completion before admitting a newer intent");
        startProtectionClock+=closing.remainingMs;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
        check(writeTarget(off,next).value("ok",false),"actual newer ON is admitted only after uncompleted OFF reaches original hard cap");inputCaptureRefreshRealStickSettings(true);
        const auto latest=g_virtualStartProtection.view(startProtectionClock);const auto before=recoveryView(),owners=ownerView(),local=state();const auto stops=stopCount(),creates=createCount();const auto host=g_inputHostProcess;
        check(latest.locked&&latest.requestId!=candidate.requestId&&g_inputHostEpoch&&host,"new player cycle deliberately retains the same still-live Host");
        if(deferred){std::lock_guard<InputHostMutex> lock(g_inputHostMx);std::string work;const bool queued=inputHostTupleRecoveryMaintenanceLocked(work);check(!queued,"old steady-state deferral cannot queue recovery into the newer player cycle");}
        else inputHostPerformTupleRecovery(oldToken);
        check(stopCount()==stops&&createCount()==creates&&g_inputHostProcess==host&&state()==local,"old unprotected tuple work cannot Stop or replace the newer player cycle's Host");
        check(budgetView()==before["budget"]&&ownerView()==owners&&g_virtualStartProtection.view(startProtectionClock).requestId==latest.requestId&&g_virtualStartProtection.view(startProtectionClock).remainingMs==latest.remainingMs,"old steady-state task cannot charge acquire return ownership or alter newer request deadlines");
        finishCase();
    }

}