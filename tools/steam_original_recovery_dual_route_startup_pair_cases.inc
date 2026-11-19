    const auto startPair=[](const std::shared_ptr<Control>& c){
        const auto at=engineCalls;
        eventOwner(c);c->startupPending=true;
        SteamOriginalRecovery::runOne(c,{Trigger::Startup,0,1},config);
        check(engineCalls==at+2,"initial-pair-not-admitted-independently");
        check(requests[at]["trigger"]["kind"]=="startup"&&requests[at+1]["trigger"]["kind"]=="startup","initial-trigger");
    };
    test("startup-pair-new-game-uses-original-pair-budget",[&]{
        auto c=reset(true);startPair(c);++liveGame.processCreated;
        steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,9);drainOne(c);
        check(engineCalls==4,"new-game-route-count");
        check(requests[2]["trigger"]["kind"]=="startup"&&requests[3]["trigger"]["kind"]=="startup"&&
            requests[2]["recoveryAllowed"]==true&&requests[3]["recoveryAllowed"]==true,"new-game-startup-missed");
    });
#ifndef YMCC_OBSERVATION_OLD_COUNTEREXAMPLE
    test("startup-pair-new-steam-uses-original-pair-budget",[&]{
        auto c=reset(true);startPair(c);++SleepSteamRecovery::live.created;
        steamOriginalRecoverySteamChanged();drainOne(c);
        check(engineCalls==4&&requests[2]["trigger"]["kind"]=="startup"&&requests[3]["recoveryAllowed"]==true,"new-steam-startup-missed");
    });
    test("startup-pair-consumed-pair-remains-observation",[&]{
        auto c=reset(true);startPair(c);
        steamOriginalRecoveryBindingReady();drainOne(c);
        check(engineCalls==4&&!c->startupPending,"consumed-pair-observation-lost");
        check(requests[2]["trigger"]["kind"]=="target-changed"&&requests[2]["recoveryAllowed"]==false&&requests[3]["recoveryAllowed"]==false,"consumed-pair-refunded");
        steamOriginalRecoveryBindingReady();drainOne(c);check(engineCalls==4,"duplicate-observation-repeated");
    });
    test("startup-pair-game-unknown-and-return-never-refunds",[&]{
        auto c=reset(true);startPair(c);const auto original=liveGame;
        targetAvailable=false;steamOriginalRecoveryTargetChanged(0,0,L"",9);drainOne(c);
        check(engineCalls==2,"unknown-target-dispatched");targetAvailable=true;
        steamOriginalRecoveryTargetChanged(original.pid,original.processCreated,original.path,9);drainOne(c);
        check(engineCalls==4&&requests[2]["recoveryAllowed"]==false&&requests[3]["recoveryAllowed"]==false,"unknown-target-refunded-startup");
    });
    test("startup-pair-steam-aba-never-refunds",[&]{
        auto c=reset(true);startPair(c);const auto original=SleepSteamRecovery::live;
        ++SleepSteamRecovery::live.created;steamOriginalRecoverySteamChanged();drainOne(c);
        SleepSteamRecovery::live=original;steamOriginalRecoverySteamChanged();drainOne(c);
        check(engineCalls==6&&requests[4]["recoveryAllowed"]==false&&requests[5]["recoveryAllowed"]==false,"steam-aba-refunded-startup");
    });
    test("startup-pair-rules-and-touch-create-no-new-work",[&]{
        auto c=reset(true);startPair(c);c->targetObserved=true;
        c->observedTarget=SteamOriginalRecovery::identity(liveGame.pid,liveGame.processCreated,liveGame.path);c->observedRules=9;
        for(unsigned i=0;i<1000;++i)steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,10);
        check(c->pending.empty()&&engineCalls==2&&c->observedRules==10,"touch-created-new-startup");
    });
    test("startup-pair-new-game-nonready-resumes-original-ready-event",[&]{
        auto c=reset(true);startPair(c);++liveGame.processCreated;fixtureScopeAdmitted=false;g_powerLifecycle=PowerLifecycle::Resuming;
        steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,9);drainOne(c);
        check(engineCalls==2&&c->startupPending,"new-pair-lost-ready-hint");
        fixtureScopeAdmitted=true;g_powerLifecycle=PowerLifecycle::Ready;steamOriginalRecoveryPowerReady(7);drainOne(c);
        check(engineCalls==4&&requests[2]["recoveryAllowed"]==true&&requests[3]["recoveryAllowed"]==true&&!c->startupPending,"ready-did-not-use-original-pair-admission");
    });
    test("startup-pair-outer-unknown-keeps-original-ready-hint",[&]{
        auto c=reset(true);startPair(c);++liveGame.processCreated;fixtureScopeAvailable=false;
        steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,9);drainOne(c);
        check(engineCalls==2&&c->startupPending,"outer-unknown-erased-new-pair-hint");
        fixtureScopeAvailable=true;steamOriginalRecoveryPowerReady(7);drainOne(c);
        check(engineCalls==4&&requests[2]["recoveryAllowed"]==true,"outer-unknown-never-resumed");
    });
    test("startup-pair-independent-static-qualification-keeps-hint",[]{
        auto c=reset(true);eventOwner(c);privateAvailable=false;
        SteamOriginalRecovery::runOne(c,{Trigger::Startup,0,1},config);
        check(engineCalls==1&&c->startupPending,"dynamic-cleared-unqualified-static-pending");
        privateAvailable=true;steamOriginalRecoveryBindingReady();drainOne(c);
        check(engineCalls==3&&requests[1]["recoveryAllowed"]==false&&requests[2]["recoveryRoute"]=="semi-static"&&
            requests[2]["trigger"]["kind"]=="startup"&&requests[2]["recoveryAllowed"]==true&&!c->startupPending,"independent-static-startup-lost");
    });
    test("startup-pair-ready-hints-coalesce-without-budget-reset",[&]{
        auto c=reset(true);startPair(c);++liveGame.processCreated;privateAvailable=false;
        steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,9);drainOne(c);
        check(c->startupPending,"unqualified-static-not-pending");const auto before=engineCalls;
        for(unsigned i=0;i<1000;++i)steamOriginalRecoveryPowerReady(7);
        check(c->pending.size()==1,"ready-hints-not-coalesced");privateAvailable=true;drainOne(c);
        check(engineCalls==before+2&&requests[before]["recoveryAllowed"]==false&&requests[before+1]["recoveryAllowed"]==true,"ready-refunded-dynamic-route");
    });
    test("startup-pair-suspend-and-wake-triggers-retained",[&]{
        auto c=reset(true);eventOwner(c);c->startupPending=true;
        SteamOriginalRecovery::runOne(c,{Trigger::SuspendCapture,7,1},config);
        check(engineCalls==2&&c->startupPending&&requests[0]["trigger"]["kind"]=="suspend-capture"&&requests[0]["recoveryAllowed"]==false,"suspend-consumed-startup-hint");
        startPair(c); // The two new calls use Startup, not the suspend ledger.
    });
    test("startup-pair-original-wake-priority-retained",[&]{
        auto c=reset(true);startPair(c);++liveGame.processCreated;c->deferredWake=c->latestPower=7;
        steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,9);
        check(!c->pending.empty()&&c->pending.front().kind==Trigger::Wake,"new-pair-stole-deferred-wake");
        drainOne(c);check(requests[2]["trigger"]["kind"]=="wake"&&requests[3]["trigger"]["kind"]=="wake","wake-converted-to-startup");
    });
    test("startup-pair-licensed-type0-is-still-excluded",[]{
        auto c=reset(true);eventOwner(c);SteamGameLink::live.gameId="252490";SteamGameLink::live.binding.appid=252490;
        SteamOriginalRecovery::runOne(c,{Trigger::TargetChanged,0,1},config);
        check(engineCalls==0&&c->startupPending,"licensed-game-entered-recovery");
    });
    test("startup-pair-new-inner-scope-keeps-mutation-authority",[&]{
        auto c=reset(true);startPair(c);++liveGame.processCreated;std::shared_ptr<FixtureNativeReadScope> publisher;
        onEngineEnter=[&]{publisher=lastNativeReadScope.lock();check(bool(publisher)&&publisher->purpose==ymcc::steamnative::ReadPurpose::PrepareMutation&&
            ymcc::sleeppower::powerAdmitted(publisher->power()),"observation-scope-upgraded-to-writer");};
        steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,9);drainOne(c);
        check(engineCalls==4&&requests[2]["recoveryAllowed"]==true,"new-pair-lacks-fresh-writer-scope");
    });
    test("startup-pair-old-dispatch-cannot-clear-next-game",[&]{
        auto c=reset(true);eventOwner(c);bool changed=false;
        onEngineEnter=[&]{if(changed)return;changed=true;++liveGame.processCreated;
            steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,9);};
        SteamOriginalRecovery::runOne(c,{Trigger::Startup,0,1},config);onEngineEnter={};
        check(!c->pending.empty(),"new-game-event-lost");const auto at=engineCalls;drainOne(c);
        bool newMutation=false;for(auto i=at;i<engineCalls;++i)newMutation=newMutation||requests[i]["recoveryAllowed"]==true;
        check(newMutation,"old-dispatch-erased-new-game-startup");
    });
    test("startup-pair-terminal-old-pair-does-not-consume-new-pair",[]{
        auto c=reset(true);eventOwner(c);bool changed=false;
        onEngineEnter=[&]{if(changed)return;changed=true;++liveGame.processCreated;
            steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,9);};
        engineReply=[](const json&){return result("recovered-native",true);};
        SteamOriginalRecovery::runOne(c,{Trigger::Startup,0,1},config);onEngineEnter={};engineReply={};
        check(engineCalls==1&&!c->pending.empty(),"old-terminal-consumed-new-pair-static");
        drainOne(c);check(engineCalls==3&&requests[1]["recoveryAllowed"]==true&&requests[2]["recoveryAllowed"]==true,"new-pair-suppressed-by-old-terminal");
    });
#endif
