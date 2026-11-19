// Directed extension of the original fixture. No new canonical ID.
// Counterexample selects exactly the same first business case and old runtime/factory.
    test("producer-suspend-capture-queues-reference-only",[]{
        auto c=reset(false);eventOwner(c);c->startupPending=true;
        g_powerLifecycle=PowerLifecycle::Suspending;fixtureScopeAdmitted=false;operationAllowed=false;
        sleepRecoveryAutoCaptureSuspend(7);
        check(c->pending.size()==1&&c->pending.front().kind==Trigger::SuspendCapture,"formal-capture-not-queued");
        drainOne(c);
        check(engineCalls==2,"formal-capture-did-not-reach-both-routes");
        for(const auto& req:requests)check(req["recoveryAllowed"]==false&&req["trigger"]["kind"]=="suspend-capture","capture-granted-recovery");
        for(const auto& guard:engineGuards)check(guard["current"]==true&&guard["observation"]==true,"suspend-original-guard-refused");
#ifndef YMCC_OBSERVATION_OLD_COUNTEREXAMPLE
        check(fixturePurposes==std::vector<ymcc::steamnative::ReadPurpose>(3,ymcc::steamnative::ReadPurpose::ObserveOriginal),"suspend-purpose-sequence");
        check(observationTrackingCalls==2&&observationTrackingAccepted==2,"suspend-tracker-facade-not-used");
#endif
    });
#ifndef YMCC_OBSERVATION_OLD_COUNTEREXAMPLE
    test("native-read-observation-target-steam-changes",[]{
        for(const auto kind:{Trigger::TargetChanged,Trigger::SteamChanged}){
            auto c=reset(false);c->startupPending=false;g_powerLifecycle=PowerLifecycle::Suspending;fixtureScopeAdmitted=false;operationAllowed=false;
            SteamOriginalRecovery::runOne(c,{kind,7,1},config);
            check(engineCalls==2&&observationTrackingAccepted==2,"nonready-change-did-not-observe-both-routes");
            for(const auto& req:requests)check(req["recoveryAllowed"]==false,"observation-granted-recovery");
            check(fixturePurposes==std::vector<ymcc::steamnative::ReadPurpose>(3,ymcc::steamnative::ReadPurpose::ObserveOriginal),"change-purpose-sequence");
        }
    });
    test("native-read-observation-startup-wake-power-denied",[]{
        for(const auto kind:{Trigger::Startup,Trigger::Wake}){
            auto c=reset(true);fixtureScopeAdmitted=false;
            SteamOriginalRecovery::runOne(c,{kind,7,1},config);
            check(engineCalls==0&&privateReads==0,"power-denied-mutation-entered-engine");
            check(fixturePurposes==std::vector<ymcc::steamnative::ReadPurpose>{ymcc::steamnative::ReadPurpose::PrepareMutation},"mutation-operation-purpose");
        }
        auto c=reset(true);c->startupPending=true;fixtureScopeAdmitted=false;
        SteamOriginalRecovery::runOne(c,{Trigger::TargetChanged,7,1},config);
        check(engineCalls==0&&fixturePurposes==std::vector<ymcc::steamnative::ReadPurpose>{ymcc::steamnative::ReadPurpose::PrepareMutation},"startup-conversion-after-purpose");
    });
    test("native-read-observation-account-revokes",[]{
        for(const auto r:{Route::LegacyDynamic,Route::SemiStatic}){
            auto c=reset(false);g_powerLifecycle=PowerLifecycle::Suspending;fixtureScopeAdmitted=false;
            onEngineEnter=[]{auto held=lastNativeReadScope.lock();held->identityValid=false;};
            SteamOriginalRecovery::runRoute(c,{Trigger::SuspendCapture,7,1},config,r,true);
            check(engineCalls==1&&engineGuards[0]["current"]==false&&engineGuards[0]["observation"]==false,"account-revocation-bypassed");
            check(!c->ownsRunning,"revoked-account-retained-owner");
        }
    });
    test("native-read-observation-cannot-upgrade-startup",[]{
        auto c=reset(false);fixtureScopeAdmitted=false;g_powerLifecycle=PowerLifecycle::Suspending;
        auto held=nativeSteamNativeReadScope(SleepSteamRecovery::session(),ymcc::steamnative::ReadPurpose::ObserveOriginal);
        check(held&&held->current()&&!ymcc::sleeppower::powerAdmitted(held->power())&&held->powerCancellationEvent()!=nullptr,"observation-minted-power-ticket");
        g_powerLifecycle=PowerLifecycle::Ready;
        SteamOriginalRecovery::runRoute(c,{Trigger::Startup,7,1},config,Route::LegacyDynamic,true,held);
        check(engineCalls==0&&privateReads==0,"observation-upgraded-to-startup-mutation");
        check(fixturePurposes==std::vector<ymcc::steamnative::ReadPurpose>{ymcc::steamnative::ReadPurpose::ObserveOriginal,ymcc::steamnative::ReadPurpose::PrepareMutation},"startup-route-did-not-reacquire-strict-scope");
    });
    test("native-read-observation-original-cancellation",[]{
        for(const auto r:{Route::LegacyDynamic,Route::SemiStatic})for(int mode=0;mode<4;++mode){
            auto c=reset(false);fixtureScopeAdmitted=false;g_powerLifecycle=PowerLifecycle::Suspending;
            onEngineEnter=[c,mode]{if(mode==0)c->latestPower=8;if(mode==1)++g_sleepRecoveryAutoConfigEpoch;if(mode==2){++liveGame.processCreated;std::lock_guard<std::mutex> lock(g_gameValveMx);++g_gameValveTarget.game.processCreated;}if(mode==3)c->stop=true;};
            SteamOriginalRecovery::runRoute(c,{Trigger::SuspendCapture,7,1},config,r,true);
            check(engineCalls==1&&engineGuards[0]["current"]==false&&engineGuards[0]["observation"]==false,"original-cancellation-bypassed");
            check(!c->ownsRunning,"cancelled-observation-retained-owner");
        }
    });
    test("native-read-observation-operation-policy-sequence",[]{
        using P=ymcc::steamnative::ReadPurpose;
        for(const auto kind:{Trigger::Startup,Trigger::Wake,Trigger::SuspendCapture,Trigger::TargetChanged,Trigger::SteamChanged}){
            auto c=reset(true);engineReply=[](const json&){return result("healthy");};
            SteamOriginalRecovery::runOne(c,{kind,7,1},config);
            const bool mutation=kind==Trigger::Startup||kind==Trigger::Wake;
            check(engineCalls==2,"policy-dual-route-count");
            check(fixturePurposes==std::vector<P>{mutation?P::PrepareMutation:P::ObserveOriginal,mutation?P::PrepareMutation:P::ObserveOriginal,P::ObserveOriginal},"operation-route-purpose-sequence");
            check(requests[0]["recoveryAllowed"]==mutation&&requests[1]["recoveryAllowed"]==false,"terminal-route-mutation-policy");
            check(observationTrackingCalls==(mutation?1U:2U),"purpose-tracker-selection");
        }
    });
    test("native-read-observation-tracker-active-required",[]{
        for(const auto r:{Route::LegacyDynamic,Route::SemiStatic}){
            auto c=reset(false);fixtureScopeAdmitted=false;g_powerLifecycle=PowerLifecycle::Suspending;
            onTracking=[]{auto held=lastNativeReadScope.lock();held->identityValid=false;};
            SteamOriginalRecovery::runRoute(c,{Trigger::SuspendCapture,7,1},config,r,true);
            check(observationTrackingCalls==1&&observationTrackingAccepted==0&&engineCalls==0&&privateReads==0,"tracker-active-scope-revocation-bypassed");
        }
    });
#endif
