// Include after pipe_cases.inc (and before the caller in fixture.cpp).
// Deliberately strict regressions for protected-cycle-other-recovery-audit37
// sections 2-4. Run only in the existing native/inert fixture, never full-pipe.
// No runner registration, independent lifecycle model, new clock, or real I/O.
// Expected on the audited production code: stale admission mutates recovery
// state; deferred work adopts a replacement epoch; already-owned Stop repeats;
// an ordinary current-tuple reopen bypasses the central inspection/reservation.
static constexpr const char* postFinalTupleVariants[]={"fresh-queued","fresh-deferred","read-only-queued","read-only-deferred","neutral-queued","neutral-deferred","idle"};
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
    const auto prepare = [&](const std::string& name, bool runtimeIncident = false) {
        cases.push_back({{"name", name}});
        seed();
        fixtureRuntimeRecoveryOsEnabled = runtimeIncident;
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
        if (runtimeIncident) {
            // Controlled physical-reader input, matching the existing return
            // cases: this selected slot is still the admitted physical source.
            // Without it the original XInput-plane gate correctly stays UNKNOWN
            // and never dispatches the target-return proof under test.
            g_xinputPhysicalSourceLocked = true;
            // Native event ACKs do not execute a real managed PREPARE wire.
            // Establish this explicit controlled old-target input through the
            // unchanged production write-ahead hook and real private Session.
            // This tests runtime settlement, not managed-wire reservation.
            std::lock_guard<InputHostMutex> lock(g_inputHostMx);
            check(inputHostTargetReturnBeforeCreateLocked("PREPARE_TARGET", g_inputHostSequence + 1),
                "runtime fixture establishes its old target through the actual write-ahead hook");
            cases.back()["oldTargetInput"] = "native owned event; real production reservation/Session; no managed-wire claim";
        }
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


    // Real final -> captured real report -> existing queued/deferred consumer.
    // Reads and intentional neutral output cannot retire an unresolved tuple.
    for(const char* raw:postFinalTupleVariants){
        const std::string variant=raw;const bool idle=variant=="idle";
        const bool deferred=variant.find("deferred")!=std::string::npos;
        const bool accepted=variant.rfind("fresh-",0)==0,neutral=variant.rfind("neutral-",0)==0;
        prepare("post-final-tuple/"+variant);
        const auto candidate=beginInspection();startProtectionClock+=candidate.verificationRemainingMs;readCurrent();
        {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
        check(g_virtualStartProtection.view(startProtectionClock).phase=="success" &&
            !g_virtualStartProtection.view(startProtectionClock).locked,"steady tuple starts after actual final success");
        const auto epoch=g_inputHostEpoch;const auto run=g_inputHostRunId;const auto host=g_inputHostProcess;
        const auto originalProtection=protectionView();const auto beforeAccepted=g_inputHostTupleAccepted.acceptedSerial;
        if(idle){
            const auto budget=budgetView();const auto stops=stopCount();auto sample=readCurrent();sample.pad.wButtons=XINPUT_GAMEPAD_A;
            const auto ack=fullPipeSubmitSource(sample,true,false);
            check(ack.value("hostSubmission","")=="frame-accepted" &&
                g_inputHostTupleAccepted.acceptedSerial==beforeAccepted && budgetView()==budget && stopCount()==stops &&
                !g_inputHostRecoveryPending.load() && !g_inputHostLifecycleWorkRequested.load(),
                "healthy ordinary ACK without unresolved tuple does not start evidence tracking work or recovery");
            finishCase();continue;
        }
        if(deferred)g_inputHostRecoveryLastStartTick=GetTickCount64();queueTuple(epoch);
        if(deferred){drain();check(g_inputHostRecoveryPending.load()&&!g_inputHostTupleHealPending.load(),
            "actual post-final token is deferred without spending or Stop");}
        auto sample=readCurrent();
        if(accepted || neutral){
            sample.pad.wButtons=accepted?XINPUT_GAMEPAD_A:0;
            const auto reply=fullPipeSubmitSource(sample,true,neutral);
            check(reply.value("hostSubmission","")=="frame-accepted","post-final test observes an actual Host accepted frame");
            check(accepted?g_inputHostTupleAccepted.acceptedSerial>beforeAccepted:
                    g_inputHostTupleAccepted.acceptedSerial==beforeAccepted,
                accepted?"post-final proof comes from an actual current-source accepted report":
                    "intentional neutral ACK cannot supply current-source tuple resolution proof");
        }else check(g_inputHostTupleAccepted.acceptedSerial==beforeAccepted,
            "fresh source read without Host acceptance does not resolve a tuple fault");
        g_inputHostRecoveryLastStartTick=0;g_inputHostRecoveryDeferredUntil=0;
        const auto budget=budgetView();const auto stops=stopCount(),creates=createCount();inputHostRecoveryMaintenanceTick();drain();
        if(accepted){
            check(stopCount()==stops && createCount()==creates && g_inputHostProcess==host &&
                g_inputHostEpoch==epoch && g_inputHostRunId==run && g_inputHostPrepared && g_inputHostNeutralized,
                "fresh post-final acceptance cancels only unspent repair without stopping the healthy target");
            check(budgetView()==budget && !g_inputHostRecoveryPending.load() && !g_inputHostTupleHealPending.load(),
                "resolved post-final tuple spends no shared budget and leaves no pending repair");
            const auto resolved=[&](){return std::count_if(fixtureEvents.begin(),fixtureEvents.end(),[](const auto& e){return
                e.value("event","")=="input-host-tuple-recovery" && e["detail"].value("stage","")=="resolved";});};
            check(resolved()==1,"actual tuple consumer logs one exact unspent resolution edge");
            inputHostRecoveryMaintenanceTick();drain();
            check(stopCount()==stops && budgetView()==budget && protectionView()==originalProtection && resolved()==1,
                "resolved tuple cannot reappear rewrite the completed final or repeat its edge log");
        }else check(stopCount()==stops+1 && g_inputHostRecoveryEpisodeAttempts.load()==budget["episode"].get<int>()+1,
                "read-only or neutral ACK preserves the original bounded repair and its one charge");
        finishCase();
    }

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

    // The original tuple consumer also owns these bounded post-final transport
    // cases. No second runner, recovery model, clock or direct settlement stub.
    struct RuntimeParameter { const char* command; const char* condition; bool hostFirst; };
    const std::vector<RuntimeParameter> runtimeParameters = {
        {"HIDHIDE_QUERY_STATE","return-order",true}, {"HIDHIDE_QUERY_STATE","return-order",false},
        {"SUBMIT_FRAME","return-order",true}, {"SUBMIT_FRAME","return-order",false},
        {"SUBMIT_FRAME","target-present",true}, {"SUBMIT_FRAME","target-unknown",true},
        {"SUBMIT_FRAME","commit-denied",true},
        {"SUBMIT_FRAME","cancel-off",true}, {"SUBMIT_FRAME","cancel-exit",true},
        {"SUBMIT_FRAME","cancel-power",true}, {"SUBMIT_FRAME","cancel-intent",true},
        {"SUBMIT_FRAME","cancel-new-request",true},
        {"SUBMIT_FRAME","episode-exhausted",true}, {"SUBMIT_FRAME","rolling-exhausted",true},
        {"SUBMIT_FRAME","cooldown",true}, {"HIDHIDE_QUERY_STATE","nonretryable",true},
        {"HIDHIDE_QUERY_STATE","verification-period",true}, {"SUBMIT_FRAME","recording-pause",true},
        {"SUBMIT_FRAME","quiescence-power",true}, {"SUBMIT_FRAME","quiescence-intent",true}
    };
    const auto runtimeView = []() {
        const auto& r=ymcc::inputHost::runtimeRecovery::state;
        return json{{"serial",r.serial},{"oldRequestId",r.requestId},{"hostEpoch",r.hostEpoch},
            {"runId",r.runId},{"hostInstanceId",r.hostInstanceId},{"persona",r.persona},
            {"intentRevision",r.intentRevision},{"power",r.power},{"sequence",r.sequence},
            {"command",r.command},{"error",r.error},{"pending",r.pending},{"queued",r.queued},
            {"stopInvoked",r.stopInvoked},{"spent",r.spent},{"recoveryRequestId",r.recoveryRequestId},
            {"atomicPending",g_inputHostRuntimeRecoveryPending.load()}};
    };
    const auto runtimeQueued = [&]() {
        const auto queue=queueView();unsigned count=0;
        for(const auto& reason:queue)if(reason.get<std::string>().rfind("runtime-transport-recovery:",0)==0)++count;
        return count;
    };
    const auto oldFinals = [](std::uint64_t ticket) {
        json result=json::array();
        for(const auto& event:fixtureEvents)if(event.value("event","")=="input-host-switch-verification" &&
            event["detail"].value("stage","")=="final" && event["detail"].value("requestId",0ULL)==ticket)
            result.push_back(event);
        return result;
    };
    const auto loadRuntimeDisk = []() {
        return ymcc::targetReturn::load(fixtureTargetReturnRecordPath(),inputHostTargetReturnMachineBinding());
    };
    const auto runtimeBusinessSnapshot = []() {
        std::lock_guard<InputHostMutex> lock(g_inputHostMx);
        const auto& ledger=ymcc::inputHost::targetReturnRuntime::state;
        return json{{"hostInstanceId",g_inputHostHostInstanceId},{"runId",g_inputHostRunId},{"epoch",g_inputHostEpoch},
            {"hostPresent",g_inputHostProcess!=nullptr},{"primaryThread",g_inputHostPrimaryThread!=nullptr},
            {"hello",g_inputHostHello},{"prepared",g_inputHostPrepared},{"active",g_inputHostActive},
            {"journalActive",g_hidHideVisibilityJournal.active},{"quarantined",g_inputHostReleaseQuarantined},
            {"pipeRetirement",inputHostPipeRetirementProvenLocked()},
            {"sessionReady",ledger.session && ledger.session->ready()},{"targets",ledger.targets.size()},
            {"proven",ledger.proven.size()},{"physicals",ledger.physicals.size()},
            {"hidHideRestoreResult",g_startEntrySeam.hidHideRestoreResult}};
    };
    const auto runtimeTick = [&]() { inputHostRecoveryMaintenanceTick(); drain(); };
    const auto revokeRecoveryDemand = []() {
        std::lock_guard<std::mutex> personaLock(g_inputHostPersonaMx);
        g_inputHostRecoveryIntent.invalidate();
    };
    // Simulate one already-scheduled production observation after exact return.
    // Do not roll nextProbe, request/cooldown deadlines, owner state or quotas.
    const auto observeNextRuntimeProbe = [&]() {
        const auto& r=ymcc::inputHost::runtimeRecovery::state;
        if(!r.pending || r.queued || r.spent)return;
        const auto now=GetTickCount64();
        if(!r.nextProbe || r.nextProbe<=now)return;
        const auto delta=r.nextProbe-now;
        if(delta>1000)throw std::logic_error("runtime-fixture-probe-outside-original-cadence");
        cases.back()["nextScheduledProbe"]={{"beforeTick",now},{"originalNextProbe",r.nextProbe},
            {"advanceMs",delta},{"oldRequestId",r.requestId},{"recoveryRequestBefore",r.recoveryRequestId}};
        fixturePowerClockOffset+=delta;startProtectionClock+=delta;
        // Existing verified source reader supplies the sample at the new time;
        // this does not create a frame receipt, health credit or device proof.
        readCurrent();runtimeTick();
        cases.back()["nextScheduledProbe"]["pendingAfter"]=r.pending;
        cases.back()["nextScheduledProbe"]["recoveryRequestAfter"]=r.recoveryRequestId;
    };
    for(const auto& parameter:runtimeParameters) {
        const std::string condition=parameter.condition;
        prepare(std::string("runtime-after-final/")+parameter.command+"/"+condition+
            (condition=="return-order"?(parameter.hostFirst?"/host-first":"/target-first"):""),true);
        const auto candidate=beginInspection();
        if(condition!="verification-period") {
            startProtectionClock+=candidate.verificationRemainingMs;readCurrent();
            {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
            check(g_virtualStartProtection.view(startProtectionClock).phase=="success" &&
                !g_virtualStartProtection.view(startProtectionClock).locked,
                "runtime input starts only after the original actual successful final");
            startProtectionClock+=480001;fixturePowerClockOffset+=480001;readCurrent();
        }
        const auto oldTicket=candidate.requestId,oldEpoch=g_inputHostEpoch,oldPower=currentPowerGeneration();
        const auto oldRun=g_inputHostRunId,oldInstance=g_inputHostHostInstanceId,oldPersona=g_inputHostBoundPersona;
        const auto oldIntent=g_inputHostVirtualIntentRevision.load();const auto finalBefore=oldFinals(oldTicket);
        check(condition=="verification-period"?finalBefore.empty():finalBefore.size()==1,
            "original player final scope is observed before the runtime error");
        HANDLE observation=nullptr;
        if(!DuplicateHandle(GetCurrentProcess(),g_inputHostProcess,GetCurrentProcess(),&observation,0,FALSE,DUPLICATE_SAME_ACCESS))
            throw std::runtime_error("runtime-owned-event-observer");
        ymcc::targetReturn::fileDetail::Handle oldHost(observation),denyReplace(INVALID_HANDLE_VALUE);
        fixtureRuntimeRecoveryOldHost=observation;
        fixtureRuntimeRecoveryOldOwner=inputHostTargetReturnCaptureOwner(g_inputHostProcess);
        check(fixtureRuntimeRecoveryOldOwner.has_value(),"runtime old Host has the exact controlled PID and birth identity");
        const auto running=loadRuntimeDisk();std::vector<ymcc::targetReturn::Origin> oldOrigins;
        if(running.record)for(const auto& duty:running.record->obligations)
            if(duty.origin.runId==oldRun && duty.origin.epoch==oldEpoch)oldOrigins.push_back(duty.origin);
        check(running.state==ymcc::targetReturn::LoadState::Valid && !oldOrigins.empty(),
            "runtime fault begins with a real persisted nonempty exact old-target obligation");
        if(oldOrigins.empty())throw std::runtime_error("runtime-old-target-reservation-missing");
        const auto oldDutyRemains = [&]() {
            const auto disk=loadRuntimeDisk();
            if(disk.state!=ymcc::targetReturn::LoadState::Valid || !disk.record)return true;
            for(const auto& origin:oldOrigins)if(std::any_of(disk.record->obligations.begin(),disk.record->obligations.end(),
                [&](const auto& duty){return ymcc::targetReturn::sameOrigin(duty.origin,origin);}))return true;
            return false;
        };
        const auto runtimeSettled = [&]() {
            std::lock_guard<InputHostMutex> lock(g_inputHostMx);
            return inputHostTargetReturnRuntimeSettledLocked(oldRun,oldEpoch);
        };
        if(condition=="commit-denied") {
            denyReplace.value=CreateFileW(fixtureTargetReturnRecordPath().c_str(),GENERIC_READ,FILE_SHARE_READ,
                nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
            if(!denyReplace.valid())throw std::runtime_error("runtime-private-commit-denial");
        }
        const auto oldEpochCounter=g_inputHostEpochCounter.load();
        const auto terminalSerialBefore=ymcc::inputHost::recoveryFallback::nextSerial;
        const auto now=GetTickCount64();
        if(condition=="episode-exhausted")g_inputHostRecoveryEpisodeAttempts=kRecoveryEpisodeMax;
        if(condition=="rolling-exhausted"){g_inputHostRecoveryRolling[0]=now;g_inputHostRecoveryRolling[1]=now;}
        if(condition=="cooldown")g_inputHostRecoveryLastStartTick=now;
        const auto budgetBefore=budgetView();
        const bool sharedTerminalCase=std::strcmp(parameter.command,"SUBMIT_FRAME")==0 &&
            (condition=="episode-exhausted" || condition=="rolling-exhausted");
        auto expectedBudget=budgetBefore;
        if(sharedTerminalCase){expectedBudget["terminal"]=true;expectedBudget["fallbackState"]=3;}
        g_startEntrySeam.hostExitWait=WAIT_TIMEOUT;fixtureRuntimeRecoveryCensus="unknown";
        fixtureFrameFirstAck=false;g_startEntrySeam.pipeFailCommand=parameter.command;
        fixtureStartupTransportError=condition=="nonretryable"?ERROR_ACCESS_DENIED:ERROR_SEM_TIMEOUT;
        fixtureRuntimeRecoveryFaultEnabled=true;fixtureRuntimeRecoveryFaultConsumed=false;
        if(std::strcmp(parameter.command,"HIDHIDE_QUERY_STATE")==0) {
            std::lock_guard<InputHostMutex> lock(g_inputHostMx);HidHidePeriodicState queried;
            check(!hidHidePeriodicStateViaPipe(queried),"actual periodic Host query receives the controlled OS transport failure");
        } else {
            auto sample=readCurrent();sample.pad.wButtons=XINPUT_GAMEPAD_A;
            const auto receipt=fullPipeSubmitSource(sample,true,false);
            check(receipt.value("hostSubmission","")!="frame-accepted","actual frame consumer receives the controlled OS transport failure");
        }
        check(fixtureRuntimeRecoveryFaultConsumed && fixtureRuntimeRecoveryFaultCalls==1 && g_inputHostTransportFault,
            "one real request classifier consumes exactly one injected transport OS error");
        fixtureRuntimeRecoveryFaultEnabled=false;g_startEntrySeam.pipeFailCommand.clear();
        fixtureStartupTransportError=ERROR_SUCCESS;fixtureFrameFirstAck=true;
        if(condition=="nonretryable" || condition=="verification-period") {
            check(!g_inputHostRuntimeRecoveryPending.load() && !ymcc::inputHost::runtimeRecovery::state.pending &&
                !ymcc::inputHost::runtimeRecovery::state.serial && !runtimeQueued() && stopCount()==0 &&
                createCount()==1 && budgetView()==budgetBefore,
                "nonretryable error or unfinished player verification cannot offer a runtime incident");
        } else {
            auto& runtime=ymcc::inputHost::runtimeRecovery::state;const auto serial=runtime.serial;
            check(serial && runtime.pending && runtime.requestId==oldTicket && runtime.hostEpoch==oldEpoch &&
                runtime.runId==oldRun && runtime.hostInstanceId==oldInstance && runtime.persona==oldPersona &&
                runtime.intentRevision==oldIntent && runtime.power==oldPower && runtime.command==parameter.command &&
                runtime.error==ERROR_SEM_TIMEOUT && !runtime.recoveryRequestId,
                "actual runtime offer captures the complete faulted old owner without reusing its final ticket");
            const auto terminalFactSnapshot=[&]() {
                std::lock_guard<InputHostMutex> lock(g_inputHostMx);
                const auto& f=ymcc::inputHost::recoveryFallback::request;
                return json{{"serial",f.serial},{"nextSerial",ymcc::inputHost::recoveryFallback::nextSerial},
                    {"request",f.request},{"power",f.power},{"demand",f.demand},{"persona",f.persona},
                    {"epoch",f.epoch},{"epochCounter",f.epochCounter},{"run",f.run},{"instance",f.instance},
                    {"stopInvoked",f.stopInvoked},{"deadline",f.deadline},{"stoppedAt",f.stoppedAt}};
            };
            const auto terminalStoppedForOldOwner=[&]() {
                std::lock_guard<InputHostMutex> lock(g_inputHostMx);
                const auto& f=ymcc::inputHost::recoveryFallback::request;
                return f.serial==terminalSerialBefore+1 && f.serial &&
                    ymcc::inputHost::recoveryFallback::nextSerial==f.serial &&
                    f.request==oldTicket && f.power==oldPower && f.demand==runtime.demandRevision &&
                    f.persona==oldPersona && f.epoch==oldEpoch && f.epochCounter==oldEpochCounter &&
                    g_inputHostEpochCounter.load()==oldEpochCounter && f.run==oldRun &&
                    f.instance==oldInstance && f.stopInvoked && runtime.serial==serial &&
                    !runtime.pending && !runtime.queued && runtime.spent && !runtime.stopInvoked &&
                    !g_inputHostRuntimeRecoveryPending.load();
            };
            cases.back()["offeredRuntime"]=runtimeView();
            runtimeTick();
            const auto terminalWaiting=sharedTerminalCase?terminalFactSnapshot():json();
            check(stopCount()==1 && traced(sharedTerminalCase?"stop:recovery-exhausted-physical-fallback":
                "stop:runtime-transport-recovery") && createCount()==1 &&
                (sharedTerminalCase?terminalStoppedForOldOwner():(runtime.stopInvoked && !runtime.spent)) &&
                !runtime.recoveryRequestId && budgetView()==expectedBudget,
                "original serialized owner performs exactly one uncharged runtime safety Stop");
            check(g_inputHostReleaseQuarantined && g_inputHostPrepared && g_inputHostHello &&
                g_inputHostEpoch==oldEpoch && g_inputHostRunId==oldRun && oldDutyRemains() && !runtimeSettled(),
                "unknown return retains prepared hello and the exact old persistent duty before Clear");
            const auto waiting=runtimeView();const auto pnpBefore=fixtureRuntimeRecoveryCensusCalls;
            const auto writerBefore=fixtureRuntimeRecoveryWriterCalls;
            for(unsigned duplicate=0;duplicate<32;++duplicate) {
                {std::lock_guard<InputHostMutex> lock(g_inputHostMx);
                    inputHostRuntimeRecoveryOfferLocked(parameter.command,runtime.sequence,ERROR_SEM_TIMEOUT);
                    inputHostRuntimeRecoveryMaintenanceLocked();}
                inputHostPerformRuntimeRecovery(serial);
            }
            check(runtimeView()==waiting && stopCount()==1 && createCount()==1 && budgetView()==expectedBudget &&
                (!sharedTerminalCase || (terminalStoppedForOldOwner() && terminalFactSnapshot()==terminalWaiting)) &&
                fixtureRuntimeRecoveryCensusCalls==pnpBefore && fixtureRuntimeRecoveryWriterCalls==writerBefore,
                "duplicate fault and maintenance cannot repeat Stop proof IO ticket or shared charge");
            const auto cancel = condition.rfind("cancel-",0)==0;
            if(cancel) {
                if(condition=="cancel-off" || condition=="cancel-new-request") {
                    reg_fs();const auto before=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
                    check(writeTarget(before,targetDocument(condition=="cancel-off"?"disabled":"elite",2)).value("ok",false),
                        "runtime cancellation enters through the original player settings admission");
                    inputCaptureRefreshRealStickSettings(true);
                }
                if(condition=="cancel-exit")g_exitRequested=true;
                if(condition=="cancel-power")g_powerGeneration=oldPower+1;
                if(condition=="cancel-intent"){g_inputHostVirtualIntentRevision.fetch_add(1);revokeRecoveryDemand();}
                const auto playerBefore=protectionView();
                {std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostRuntimeRecoveryMaintenanceLocked();}
                inputHostPerformRuntimeRecovery(serial);inputHostConsumeRuntimeFaultCleanup(oldEpoch);
                check(!runtime.pending && !g_inputHostRuntimeRecoveryPending.load() && !runtime.recoveryRequestId &&
                    stopCount()==1 && createCount()==1 && budgetView()==budgetBefore && protectionView()==playerBefore,
                    "OFF exit power intent or newer player request retires only the old runtime serial");
            } else {
                if(!parameter.hostFirst) {
                    fixtureRuntimeRecoveryCensus="absent";fullPipeQuarantineProbe();runtimeTick();
                    check(createCount()==1 && !runtime.recoveryRequestId && oldDutyRemains() &&
                        WaitForSingleObject(observation,0)==WAIT_TIMEOUT && budgetView()==budgetBefore,
                        "complete target absence cannot substitute for the still-live exact old Host");
                }
                check(SetEvent(observation)!=FALSE,"only the exact fixture-owned old Host event is signaled exited");
                g_startEntrySeam.hostExitWait=WAIT_OBJECT_0;
                if(parameter.hostFirst) {
                    const auto restoreCallsBeforeUnprovedTarget=fullPipeRestoreCount();
                    fixtureRuntimeRecoveryCensus=condition=="target-present"?"present":"unknown";
                    fullPipeQuarantineProbe();
                    check(fullPipeRestoreCount()==restoreCallsBeforeUnprovedTarget,
                        "present or unknown old target forbids physical restoration before complete absence proof");
                    check(createCount()==1 && !runtime.recoveryRequestId && oldDutyRemains() &&
                        !g_inputHostProcess && g_inputHostPrepared && g_inputHostHello &&
                        g_inputHostEpoch==oldEpoch && g_inputHostRunId==oldRun,
                        "dead exact Host with present or unknown PnP retains the real prepared hello quarantine tuple");
                }
                if(condition=="recording-pause")g_shortcutRecordingActive=true;
                const bool quiescenceBoundary=condition=="quiescence-power" || condition=="quiescence-intent";
                if(quiescenceBoundary) {
                    fixtureRuntimeRecoveryWriterPathCallback=[&]() {
                        if(condition=="quiescence-power")g_powerGeneration=oldPower+1;
                        else {g_inputHostVirtualIntentRevision.fetch_add(1);revokeRecoveryDemand();}
                    };
                }
                fixtureRuntimeRecoveryCensus="absent";readCurrent();fullPipeQuarantineProbe();
                cases.back()["runtimeReturnBusinessSnapshot"]=runtimeBusinessSnapshot();
                if(quiescenceBoundary) {
                    check(fixtureRuntimeRecoveryWriterPathCallbackCalls==1 && !fixtureRuntimeRecoveryWriterPathCallback &&
                        (condition=="quiescence-power"?currentPowerGeneration()==oldPower+1:
                            g_inputHostVirtualIntentRevision.load()==oldIntent+1),
                        "last real quiescence writer-path IO fires the armed post-commit power or intent change exactly once");
                    check(!oldDutyRemains() && runtimeSettled(),
                        "late quiescence invalidation preserves the already committed exact old-origin settlement");
                    check(!g_inputHostProcess && g_inputHostReleaseQuarantined && g_inputHostPrepared && g_inputHostHello &&
                        g_inputHostEpoch==oldEpoch && g_inputHostRunId==oldRun && g_inputHostHostInstanceId==oldInstance &&
                        !runtime.recoveryRequestId && createCount()==1 && stopCount()==1 && budgetView()==budgetBefore,
                        "post-commit quiescence IO invalidation cannot return success Clear the retained tuple or create a new ticket Host or charge");
                    runtimeTick();inputHostPerformRuntimeRecovery(serial);
                    check(!runtime.pending && !g_inputHostRuntimeRecoveryPending.load() && !runtime.recoveryRequestId &&
                        g_inputHostEpoch==oldEpoch && g_inputHostRunId==oldRun && g_inputHostHostInstanceId==oldInstance &&
                        createCount()==1 && stopCount()==1 && budgetView()==budgetBefore &&
                        fixtureRuntimeRecoveryWriterPathCallbackCalls==1,
                        "original runtime maintenance retires the late power or intent serial without reviving its settled old request");
                } else {
                if(condition=="commit-denied") {
                    bool proven=false;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);
                        const auto& ledger=ymcc::inputHost::targetReturnRuntime::state;
                        for(const auto& origin:oldOrigins)proven=proven || std::any_of(ledger.proven.begin(),ledger.proven.end(),
                            [&](const auto& item){return ymcc::targetReturn::sameOrigin(item,origin);});}
                    check(proven && oldDutyRemains() && g_inputHostPrepared && g_inputHostHello &&
                        !runtime.recoveryRequestId && createCount()==1 && budgetView()==budgetBefore,
                        "real atomic commit failure retains proven old duty and cannot mint a recovery ticket");
                    const auto provedCensusCalls=fixtureRuntimeRecoveryCensusCalls;
                    fixtureRuntimeRecoveryCensus="unknown"; // later OS observation must not erase the exact earlier completed proof
                    fullPipeQuarantineProbe();
                    check(oldDutyRemains() && !runtimeSettled() && createCount()==1 && budgetView()==budgetBefore,
                        "retained proof cannot erase duty or restart while the original atomic write is still denied");
                    check(fixtureRuntimeRecoveryCensusCalls==provedCensusCalls,
                        "already-proven exact return retries its denied durable commit without another PnP census");
                    check(denyReplace.close(),"only the private commit-denial handle is released");
                    fullPipeQuarantineProbe();
                    check(fixtureRuntimeRecoveryCensusCalls==provedCensusCalls,
                        "later unknown census cannot block completion of the already-proven old return");
                    cases.back()["runtimeReturnBusinessSnapshotAfterCommitRetry"]=runtimeBusinessSnapshot();
                }
                check(!oldDutyRemains() && runtimeSettled() && fixtureRuntimeRecoveryWriterCalls>0 && fixtureRuntimeRecoveryCensusCalls>0,
                    "production exact-host death PnP parser and Session durably settle the old target before restart");
                runtimeTick();
                if(condition=="episode-exhausted" || condition=="rolling-exhausted") {
                    check(!runtime.recoveryRequestId && createCount()==1 && stopCount()==1 && budgetView()==expectedBudget &&
                        (!sharedTerminalCase || (terminalStoppedForOldOwner() && terminalFactSnapshot()==terminalWaiting)),
                        "original shared episode or rolling exhaustion permits safety return but no replacement charge or ticket");
                } else {
                    if(condition=="cooldown" || condition=="recording-pause") {
                        check(runtime.pending && !runtime.recoveryRequestId && createCount()==1 && budgetView()==budgetBefore,
                            "expected pause or original cooldown waits without fabricating another fault or charge");
                        if(condition=="cooldown"){fixturePowerClockOffset+=1001;startProtectionClock+=1001;}
                        g_shortcutRecordingActive=false;readCurrent();runtimeTick();
                    }
                    observeNextRuntimeProbe();
                    const auto newTicket=runtime.recoveryRequestId;
                    check(newTicket && newTicket!=oldTicket && runtime.spent && !runtime.pending &&
                        g_inputHostRuntimeRecoveryPending.load() && runtime.monitoring && createCount()==2 && stopCount()==1 &&
                        g_inputHostProcess && g_inputHostPrepared && g_inputHostNeutralized && g_inputHostHello &&
                        g_inputHostEpoch!=oldEpoch && g_inputHostRunId!=oldRun && g_inputHostBoundPersona==oldPersona &&
                        g_inputHostRecoveryEpisodeAttempts.load()==budgetBefore["episode"].get<int>()+1,
                        "proven return creates one distinct internally charged same-persona ordinary Start");
                    const auto charged=budgetView();const auto newHost=g_inputHostProcess;
                    const auto newEpoch=g_inputHostEpoch;const auto newRun=g_inputHostRunId;
                    for(unsigned duplicate=0;duplicate<32;++duplicate) {
                        inputHostPerformRuntimeRecovery(serial);inputHostConsumeRuntimeFaultCleanup(oldEpoch);runtimeTick();
                    }
                    check(runtime.recoveryRequestId==newTicket && g_inputHostProcess==newHost &&
                        g_inputHostEpoch==newEpoch && g_inputHostRunId==newRun && createCount()==2 &&
                        stopCount()==1 && budgetView()==charged,
                        "spent serial and old fault token cannot Stop successor reissue a ticket or charge again");
                    for(int frame=0;frame<kInputHostFreshNeutralFramesRequired+1;++frame) {
                        auto sample=readCurrent();fullPipeSubmitSource(sample,true,false);
                    }
                    const auto inspection=g_virtualStartProtection.view(startProtectionClock);
                    check(inspection.requestId==newTicket && inspection.phase=="verifying" &&
                        inspection.verificationRemainingMs==ymcc::VirtualStartProtection::settleMs &&
                        inspection.fallbackAttempts==0 && inspection.remainingMs<=ymcc::VirtualStartProtection::maxMs,
                        "new internal ticket retains the original ten-second inspection thirty-second cap and unspent one fallback");
                    startProtectionClock+=inspection.verificationRemainingMs;readCurrent();runtimeTick();
                    check(g_virtualStartProtection.view(startProtectionClock).requestId==newTicket &&
                        g_virtualStartProtection.view(startProtectionClock).phase=="success" && oldFinals(newTicket).size()==1,
                        "ordinary verifier emits exactly one new-ticket successful final");
                }
                }
            }
            cases.back()["observedRuntime"]=runtimeView();
            if(sharedTerminalCase){cases.back()["terminalBeforeDuplicates"]=terminalWaiting;cases.back()["observedTerminal"]=terminalFactSnapshot();cases.back()["expectedTerminalBudget"]=expectedBudget;}
        }
        check(oldFinals(oldTicket)==finalBefore,"runtime processing neither changes nor republishes the completed original player final");
        cases.back()["runtimeOs"]=fixtureRuntimeRecoveryOsObservations;
        cases.back()["oldRequestId"]=oldTicket;cases.back()["oldFinalBefore"]=finalBefore;
        cases.back()["originalBudget"]=budgetBefore;
        // Observations above are final. Restore only fixture inputs for owned
        // teardown; never erase the ledger, proven vector or pending incident.
        fixtureRuntimeRecoveryWriterPathCallback={};
        g_exitRequested=false;g_inputHostExitFence=false;g_inputCaptureStop=false;
        g_powerGeneration=oldPower;g_shortcutRecordingActive=false;
        fixtureRuntimeRecoveryCensus="absent";
        check(SetEvent(observation)!=FALSE,"runtime teardown signals its own old Host observer");
        denyReplace.close();finishCase();
        check(oldHost.close(),"runtime exact old Host observation handle is closed");
        fixtureRuntimeRecoveryOldHost=nullptr;fixtureRuntimeRecoveryOldOwner.reset();fixtureRuntimeRecoveryOsEnabled=false;
    }
}
