static void startupSeed(){
    reset();g_msiCrashLoaded=true;g_msiCrashInheritedPending=true;g_msiCrashStartupAttempted=false;
    g_msiCrashStartupTransactionId.clear();g_msiCrashStartupHiddenPaths.clear();g_msiCrashNextTick=0;
    g_msiCrashUnhideAttempts=0;g_msiCrashSettlementChecks=0;g_msiCrashLastDecision.clear();g_msiCrashDecisionRows=0;
    g_msiCrashRecord.ownerPid=30000;g_msiCrashRecord.ownerCreationTime="133800000000000001";
    g_msiCrashRecord.modeAttemptOwner="30000:133800000000000001";
    g_msiDinputReturnDebtEpisode=0;g_msiDinputReturnOwed=false;g_msiMode1TerminalAttempted=false;
    g_msiDinputSourceValid=true;g_msiDinputRouteConfirmed=false;
    g_msiFeedbackSamplePower=g_power;g_msiFeedbackSampleMode=g_msiFeedbackModeGeneration.load();g_msiFeedbackSampleTopology=g_xinputTopologyEpoch.load();
    g_powerLifecycle=PowerLifecycle::Ready;g_hidHideVisibilityJournal={};
    g_fakeOldOwnerDead=g_fakeStoreOk=g_fakeResolveOk=g_fakeVisibilityOk=g_fakeUnhideOk=g_fakeLocalClearOk=true;
    g_fakeInitialWrites=g_fakeStoreCalls=g_fakeUnhides=g_fakeSchedules=0;g_fakeStoreHook=g_fakeIdentityHook=g_fakePhysicalQueryHook={};
    g_fakeSource={};g_fakeSource.usbInstance=kOwnedRoot;g_fakeSource.interfacePath=W2U(kCommandPath);
    g_fakePhysical.clear();g_fakeHidden={U2W(kOwnedRoot)};g_fakeInitialOutcome=successful();
    g_fakeDurableText=msiRecovery::encodeRecord(g_msiCrashRecord);
}
static void lifecycle(ULONGLONG tick){g_tick=tick;msiCrashRecoveryMaintenance(tick);msiOwnedReturnMaintenance(tick);}
static void returned(unsigned long long sequence,ULONGLONG tick){
    physical();sample(sequence,tick);g_fakePhysical={{true,0x0db0,0x1901,kReturnedRoot}};
}
int main(){
    Checks c;
    c.run("startup/old-owner-transfer-to-real-initial-return-and-two-fresh-signals",[&]{
        startupSeed();lifecycle(10000);
        c.require(g_fakeInitialWrites==1,"the actual startup called the actual initial restore once");
        c.require(g_msiCrashRecord.ownerPid==GetCurrentProcessId(),"durable admission transfers the dead owner's same device");
        c.require(g_msiDinputReturnDebtEpisode.load()!=0 && g_msiOwnedReturnPending,"startup duty has a nonzero observed identity");
        returned(1,11000);lifecycle(11000);
        c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"one matching signal cannot settle ahead of the observer");
        returned(2,12000);lifecycle(12000);
        c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending && !g_msiOwnedReturnPending,"two real fresh signal windows settle both debts");
        lifecycle(13000);
        c.require(!g_msiCrashInheritedPending && !g_hidHideVisibilityJournal.active,"startup journal retires only after owned configuration and physical return");
        c.require(g_fakeInitialWrites==1 && g_count.writes==0,"matching input requires no second mode write");
    });

    for(const std::string outcome:{"error433","no-device"})c.run("startup/retry/"+outcome,[&]{
        startupSeed();g_fakeInitialOutcome=disconnected();if(outcome=="no-device")g_commandAvailable=false;
        lifecycle(10000);
        c.require(g_msiOwnedReturnPending && g_msiDinputReturnDebtEpisode==1,"failed initial still observes the exact adopted duty");
        g_commandAvailable=true;lifecycle(12000);
        c.require(g_count.writes==1 && g_count.physicalWriteAttempts==1 && g_msiOwnedReturnRecovery.retryUsed(),"one actual same-device retry is available");
        for(ULONGLONG t=13000;t<19000;t+=1000)lifecycle(t);
        c.require(g_count.writes==1 && g_msiDinputReturnDebtEpisode==1,"legacy and new maintenance cannot duplicate the retry or episode");
        returned(1,20000);lifecycle(20000);returned(2,21000);lifecycle(21000);lifecycle(22000);
        c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending && !g_msiCrashInheritedPending,"retry settles only after physical return and journal retirement");
    });
    for(const std::string outcome:{"partial","timeout","unknown-start-failure"})c.run("startup/ambiguous/"+outcome,[&]{
        startupSeed();g_fakeInitialOutcome=disconnected();
        if(outcome=="partial")g_fakeInitialOutcome.write.written=3;
        if(outcome=="timeout")g_fakeInitialOutcome.write.timedOut=true;
        if(outcome=="unknown-start-failure")g_fakeInitialOutcome.write.lastError=55;
        for(ULONGLONG t=10000;t<40000;t+=1000)lifecycle(t);
        c.require(g_fakeInitialWrites==1 && g_count.writes==0,"ambiguous old I/O cannot authorize a mode retry");
        c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"ambiguous result retains both responsibilities");
        returned(1,40000);++g_msiOwnedReturnSourceEvent;lifecycle(40000);returned(2,41000);lifecycle(41000);
        c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending,"a later trusted signal can settle even when writing is forbidden");
    });
    c.run("startup/already-XInput-does-not-write-or-settle-on-one-window",[&]{
        startupSeed();g_msiDinputSourceValid=false;returned(1,10000);lifecycle(10000);
        c.require(g_fakeInitialWrites==0 && g_count.writes==0,"already XInput never sends an initial mode command");
        c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending && g_msiOwnedReturnPending,"existing input enters the same stable observer");
        returned(2,11000);lifecycle(11000);lifecycle(12000);
        c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending && !g_msiCrashInheritedPending,"two same-device windows settle startup without writes");
    });
    const std::vector<std::pair<std::string,std::function<void()>>> readRejects={
        {"missing-start",[]{g_inputCaptureGamepadSnapshot.readStartedTick=0;}},
        {"start-after-completion",[]{g_inputCaptureGamepadSnapshot.readStartedTick=10001;}},
        {"future-completion",[]{g_inputCaptureGamepadSnapshot.readCompletedTick=10001;}},
        {"long-read-fresh-completion",[]{g_inputCaptureGamepadSnapshot.readStartedTick=8000;}},
        {"retired-read-identity",[]{--g_inputCaptureGamepadSnapshot.readIdentityGeneration;}},
        {"stale-cache",[]{g_tick=12000;}},
    };
    for(const auto& denial:readRejects)c.run("R121/startup-reject/"+denial.first,[&]{
        startupSeed();g_msiDinputSourceValid=false;returned(101,10000);denial.second();lifecycle(g_tick);
        c.require(!g_msiCrashStartupAttempted && g_msiDinputReturnDebtEpisode==0,"old or retired canonical XInput cannot adopt startup duty");
        c.require(g_fakeUnhides==0 && g_fakeInitialWrites==0 && g_count.writes==0,"unqualified XInput cannot unhide or trigger a mode command");
        c.require(g_msiCrashInheritedPending && g_msiCrashRecord.modeReturnPending && g_msiCrashRecord.ownerPid==30000,"unproven read retains original inherited owner responsibility");
    });
    for(const std::string change:{"identity","read-age"})c.run("R121/startup-query-recheck/"+change,[&]{
        startupSeed();g_msiDinputSourceValid=false;returned(101,10000);g_fakePhysicalQueryHook=[change]{if(change=="identity")++g_xinputSlotIdentityEpoch;else g_tick+=1600;};lifecycle(10000);
        c.require(!g_msiCrashStartupAttempted && g_msiDinputReturnDebtEpisode==0,"property query cannot carry retired or stale read into adoption");
        c.require(g_fakeUnhides==0 && g_fakeInitialWrites==0 && g_msiCrashRecord.ownerPid==30000,"query-time read retirement causes no driver or owner mutation");
    });
    c.run("R121/startup-fresh-read-age1500-inclusive",[&]{
        startupSeed();g_msiDinputSourceValid=false;returned(101,8500);g_inputCaptureGamepadSnapshot.readStartedTick=8500;lifecycle(10000);
        c.require(g_msiCrashStartupAttempted && g_msiDinputReturnDebtEpisode==1 && g_msiOwnedReturnPending,"canonical start-age exactly1500ms remains eligible for owned adoption");
        c.require(g_fakeInitialWrites==0 && g_count.writes==0,"valid XInput start boundary needs no mode write");
    });
    c.run("R121/startup-stale-read-later-fresh-rejoins",[&]{
        startupSeed();g_msiDinputSourceValid=false;returned(101,8000);lifecycle(10000);
        c.require(!g_msiCrashStartupAttempted && g_msiCrashInheritedPending,"stale startup wait does not consume adoption");
        returned(102,11000);lifecycle(11000);c.require(g_msiCrashStartupAttempted && g_msiDinputReturnDebtEpisode==1 && g_msiOwnedReturnPending,"new real read resumes original duty once");
        returned(103,12000);lifecycle(12000);lifecycle(13000);
        c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending && !g_msiCrashInheritedPending,"later two fresh windows fully settle retained obligation");
        c.require(g_fakeInitialWrites==0 && g_count.writes==0,"late fresh XInput recovery never switches mode");
    });
    c.run("R121/startup-adoption-store-cannot-use-aged-read-for-unhide",[&]{
        startupSeed();g_msiDinputSourceValid=false;returned(101,10000);g_fakeStoreHook=[]{g_tick+=1600;};lifecycle(10000);
        c.require(g_msiCrashStartupAttempted && g_msiDinputReturnDebtEpisode==1,"durable adoption is retained after a delayed store");
        c.require(g_fakeUnhides==0 && g_fakeInitialWrites==0 && g_count.writes==0,"expired read after durable store cannot authorize unhide or mode write");
        c.require(g_msiCrashInheritedPending && g_msiCrashRecord.modeReturnPending,"delayed persistence does not erase return responsibility");
    });
    std::vector<std::pair<std::string,std::function<void()>>> rejects={
        {"old-owner-live-or-unqueryable",[]{g_fakeOldOwnerDead=false;}},
        {"old-owner-is-current-live-process",[]{g_msiCrashRecord.ownerPid=GetCurrentProcessId();g_msiCrashRecord.ownerCreationTime=msiCrashProcessCreation(GetCurrentProcess());}},
        {"invalid-store",[]{g_msiCrashStoreInvalid=true;}},
        {"store-write-fails",[]{g_fakeStoreOk=false;}},
        {"ROG-family",[]{g_board="rog";}},{"GPD-family",[]{g_board="gpd";}},
        {"board-record-mismatch",[]{g_msiCrashRecord.boardKey="ROG|Ally|Ally|handheld";}},
        {"different-root-and-location",[]{g_fakeSource.usbInstance="USB\\VID_0DB0&PID_1902\\OTHER";}},
        {"changed-instance-and-location",[]{g_fakeSource.usbInstance="USB\\VID_0DB0&PID_1902\\OTHER";g_parents[kCommandPath]={U2W(g_fakeSource.usbInstance),{"PCIROOT(OTHER)"}};}},
        {"PnP-resolution-fails",[]{g_fakeResolveOk=false;}},
        {"source-absent",[]{g_msiDinputSourceValid=false;}},
        {"stale-source-power",[]{g_msiFeedbackSamplePower=99;}},
        {"stale-source-mode",[]{g_msiFeedbackSampleMode=99;}},
        {"stale-source-topology",[]{g_msiFeedbackSampleTopology=99;}},
        {"exit-fence",[]{g_exitFence=true;}},
        {"Host-present",[]{g_inputHostProcess=100;}},
        {"Host-prepared",[]{g_inputHostPrepared=true;}},
        {"virtual-request-ON",[]{g_realStickTest.virtualTargetEnabled=true;}},
        {"persona-request-ON",[]{g_persona="steamdeck";}},
        {"hardware-gate-closed",[]{g_hardwareGate=false;}},
        {"power-not-ready",[]{g_powerLifecycle=PowerLifecycle::Suspended;}},
        {"foreground-cycle",[]{g_msiNativeForegroundCyclePending=true;}},
        {"foreground-power-hold",[]{g_msiNativeForegroundPowerHold=true;}},
        {"foreground-borrow",[]{g_foregroundIsolationBorrowBusy=true;}},
    };
    for(const auto& denied:rejects)c.run("startup/reject/"+denied.first,[&]{
        startupSeed();denied.second();for(ULONGLONG t=10000;t<14000;t+=1000)lifecycle(t);
        c.require(g_msiDinputReturnDebtEpisode==0 && !g_msiOwnedReturnPending,"unproven startup cannot allocate an observed duty");
        c.require(g_fakeInitialWrites==0 && g_count.writes==0 && g_fakeUnhides==0,"unproven startup cannot write a mode or unhide hardware");
        c.require(g_msiCrashRecord.modeReturnPending,"original responsibility stays pending");
    });
    c.run("startup/rejected-store-can-later-adopt-once",[&]{
        startupSeed();g_fakeStoreOk=false;lifecycle(10000);g_fakeStoreOk=true;lifecycle(11000);
        c.require(g_msiDinputReturnDebtEpisode==1 && g_fakeInitialWrites==1 && g_msiOwnedReturnPending,"a failed durable adoption does not strand or duplicate its later valid duty");
    });
    c.run("startup/context-drift-during-source-resolution-does-not-adopt",[&]{
        startupSeed();g_fakeIdentityHook=[]{++g_power;};lifecycle(10000);
        c.require(g_msiDinputReturnDebtEpisode==0 && g_fakeInitialWrites==0 && g_fakeUnhides==0,"blocking source identity query cannot carry stale context to adoption");
    });
    c.run("startup/context-drift-after-adoption-observes-without-initial-write",[&]{
        startupSeed();g_fakeStoreHook=[]{++g_power;};lifecycle(10000);
        c.require(g_msiDinputReturnDebtEpisode==1 && g_fakeInitialWrites==0 && g_msiOwnedReturnPending,"adopted durable duty survives a changed context without initial write");
        lifecycle(12000);
        c.require(g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"only the guarded observer may retry with the newer valid context");
    });
    c.run("startup/deferred-local-target-facts-remain-observable",[&]{
        startupSeed();g_fakeLocalClearOk=false;g_targetAbsent=false;lifecycle(10000);
        c.require(g_fakeInitialWrites==0 && g_msiOwnedReturnPending,"no-write terminal gate must still attach the adopted observer");
        lifecycle(12000);c.require(g_count.writes==0,"unproven local target absence cannot authorize a retry");
        g_targetAbsent=true;lifecycle(13000);
        c.require(g_count.writes==1 && g_msiOwnedReturnRecovery.retryUsed(),"a later qualified context can consume one retry");
    });
    c.run("startup/no-signal-goes-dormant-and-source-event-can-settle",[&]{
        startupSeed();for(ULONGLONG t=10000;t<40000;t+=1000)lifecycle(t);
        c.require(g_msiOwnedReturnFailed && g_msiOwnedReturnPending,"bounded observation exhaustion retains the unconfirmed duty");
        const auto opens=g_count.opens,roots=g_count.rootQueries,stores=g_fakeStoreCalls;
        for(ULONGLONG t=40000;t<140000;t+=1000)lifecycle(t);
        c.require(g_count.opens==opens && g_count.rootQueries==roots && g_fakeStoreCalls==stores,"dormant lifecycle does no command-open, PnP-root resolution or durable write");
        returned(1,140000);++g_msiOwnedReturnSourceEvent;lifecycle(140000);returned(2,141000);lifecycle(141000);lifecycle(142000);
        c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending && !g_msiCrashInheritedPending,"late physical identity event settles the retained duty");
        c.require(g_fakeInitialWrites==1 && g_count.writes==1 && g_msiDinputReturnDebtEpisode==1,"late signals never refund a retry or allocate a second startup episode");
    });
    c.run("startup/foreign-XInput-never-clears-either-debt",[&]{
        startupSeed();g_fakeInitialOutcome=disconnected();g_fakeInitialOutcome.write.written=2;lifecycle(10000);
        for(ULONGLONG t=11000;t<16000;t+=1000){
            physical(0,"USB\\VID_0DB0&PID_1901\\OTHER",{"PCIROOT(OTHER)"});sample(t,t);
            g_fakePhysical={{true,0x0db0,0x1901,"USB\\VID_0DB0&PID_1901\\OTHER"}};lifecycle(t);
        }
        c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending && g_count.durableDischarges==0,"external or same-model-peer input is not this device's return");
    });
    c.run("startup/reused-sample-sequence-is-not-second-proof",[&]{
        startupSeed();lifecycle(10000);returned(7,11000);lifecycle(11000);returned(7,12000);lifecycle(12000);
        c.require(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"two timestamps with one read-attempt sequence cannot settle");
        returned(8,13000);lifecycle(13000);
        c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending,"stationary gamepad data with a new read sequence is valid proof");
    });

    c.run("startup/deferred-first-return-then-ambiguous-actual-result-reduces-retry",[&]{
        startupSeed();g_fakeLocalClearOk=false;g_targetAbsent=false;lifecycle(10000);
        c.require(g_fakeInitialWrites==0 && g_msiOwnedReturnPending && g_msiOwnedReturnRetrySafe,"a no-write deferral creates observable retry-safe duty");
        g_targetAbsent=true;g_fakeInitialOutcome=disconnected();g_fakeInitialOutcome.write.timedOut=true;
        g_tick=11000;msiClawRestoreXInputMode("input-host-stop");
        c.require(g_fakeInitialWrites==1,"later terminal call performs the first actual mode command");
        c.require(!g_msiOwnedReturnRetrySafe,"later ambiguous actual result must reduce this same duty's retry permission");
        lifecycle(12000);
        c.require(g_count.writes==0 && !g_msiOwnedReturnRecovery.retryUsed(),"no automatic write can follow the later ambiguous result");
        c.require(g_msiDinputReturnDebtEpisode==1 && g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending,"outcome refresh cannot renew the duty identity or clear its debt");
    });

    c.run("startup/durable-settlement-failure-retains-observer-and-later-converges",[&]{
        startupSeed();lifecycle(10000);returned(1,11000);lifecycle(11000);
        g_fakeStoreOk=false;returned(2,12000);lifecycle(12000);
        c.require(g_msiDinputReturnOwed && g_msiOwnedReturnPending && !g_msiOwnedReturnRecovery.completed(),"failed settlement retains both in-memory duty and its observer");
        c.require(g_msiCrashRecord.modeReturnPending && msiRecovery::decodeRecord(g_fakeDurableText).record.modeReturnPending,"failed settlement leaves actual record and inert durable bytes pending");
        c.require(g_msiOwnedReturnMatchedEpisode==0,"failed durable settlement cannot publish a completed BUS certificate");
        g_fakeStoreOk=true;returned(3,13000);lifecycle(13000);lifecycle(14000);
        c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending && !msiRecovery::decodeRecord(g_fakeDurableText).record.modeReturnPending,"new fresh physical window completes in-memory and durable settlement together");
        c.require(!g_msiOwnedReturnPending && !g_msiCrashInheritedPending,"the retained observer and startup journal both retire after commit");
        c.require(g_fakeInitialWrites==1 && g_count.writes==0,"settlement retry never resends a hardware mode");
    });
    c.run("startup/persistent-settlement-failure-is-bounded-until-source-event",[&]{
        startupSeed();lifecycle(10000);returned(1,11000);lifecycle(11000);g_fakeStoreOk=false;
        for(ULONGLONG t=12000;t<40000;t+=1000){returned(t,t);lifecycle(t);}
        c.require(g_msiDinputReturnOwed && g_msiOwnedReturnPending && g_msiOwnedReturnFailed,"persistent store failure remains owned after the bounded window");
        const auto stores=g_fakeStoreCalls;
        for(ULONGLONG t=40000;t<60000;t+=1000){returned(t,t);lifecycle(t);}
        c.require(g_fakeStoreCalls==stores,"unchanged dormant event does not poll persistent writes");
        g_fakeStoreOk=true;++g_msiOwnedReturnSourceEvent;returned(100,60000);lifecycle(60000);returned(101,61000);lifecycle(61000);lifecycle(62000);
        c.require(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending && !g_msiCrashInheritedPending,"a later real identity event can settle the original durable duty");
        c.require(g_fakeInitialWrites==1 && g_count.writes==0,"persistent store failure never creates a hardware retry loop");
    });
    std::vector<std::pair<std::string,std::function<void()>>> postCommitDrift={
        {"power-and-write-gate",[]{++g_power;g_hardwareGate=false;g_powerLifecycle=PowerLifecycle::Suspended;}},
        {"power",[]{++g_power;}},
        {"mode",[]{++g_msiFeedbackModeGeneration;}},
        {"topology",[]{++g_xinputTopologyEpoch;}},
        {"source-generation",[]{++g_fakeSource.generation;}},
        {"source-interface",[]{g_fakeSource.interfacePath+="changed";}},
        {"source-revoked",[]{g_msiDinputSourceValid=false;}},
        {"foreground-cycle",[]{g_msiNativeForegroundCyclePending=true;}},
        {"foreground-borrow",[]{g_foregroundIsolationBorrowBusy=true;}},
        {"new-virtual-ON",[]{g_realStickTest.virtualTargetEnabled=true;g_persona="steamdeck";}},
    };
    for(const auto& drift:postCommitDrift)c.run("startup/post-adoption-recheck/"+drift.first,[&]{
        startupSeed();g_fakeStoreHook=drift.second;lifecycle(10000);
        c.require(g_msiDinputReturnDebtEpisode==1 && g_msiCrashRecord.ownerPid==GetCurrentProcessId(),"post-commit drift preserves the durably adopted responsibility");
        c.require(g_fakeUnhides==0 && g_fakeInitialWrites==0 && g_count.writes==0,"post-adoption stale context cannot unhide or issue a mode command");
        c.require(g_msiCrashRecord.modeReturnPending && msiRecovery::decodeRecord(g_fakeDurableText).record.modeReturnPending,"post-commit drift cannot lose durable ownership");
    });
    const json report={{"passed",c.failures.empty()},{"tests",c.cases.size()},{"assertions",c.assertions},{"failures",c.failures},{"cases",c.cases}};
    std::cout<<report.dump(2)<<'\n';return c.failures.empty()?0:1;
}
