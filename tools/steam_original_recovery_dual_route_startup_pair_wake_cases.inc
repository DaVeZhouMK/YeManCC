    test("startup-pair-wake-consumed-never-creates-startup-replay",[]{
        auto c=reset(false);eventOwner(c);privateAvailable=false;
        engineReply=[](const json&){return result("blocked",true);};
        steamOriginalRecoveryWake(7);drainOne(c);check(engineCalls==1&&c->deferredWake==0,"wake-not-consumed-as-before");
        privateAvailable=true;steamOriginalRecoveryBindingReady();drainOne(c);
        for(std::size_t i=1;i<requests.size();++i)check(requests[i]["recoveryAllowed"]==false,"wake-created-startup-replay");
    });
#ifndef YMCC_OBSERVATION_OLD_COUNTEREXAMPLE
    test("startup-pair-wake-old-generation-still-covers-same-pair",[]{
        auto c=reset(false);eventOwner(c);engineReply=[](const json&){return result("blocked",true);};
        steamOriginalRecoveryWake(7);drainOne(c);const auto at=engineCalls;fixtureGeneration=8;c->latestPower=8;
        steamOriginalRecoverySteamChanged();drainOne(c);
        for(auto i=at;i<engineCalls;++i)check(requests[i]["recoveryAllowed"]==false,"power-generation-created-startup-replay");
    });
    test("startup-pair-wake-new-game-still-gets-startup",[]{
        auto c=reset(false);eventOwner(c);engineReply=[](const json&){return result("blocked",true);};
        steamOriginalRecoveryWake(7);drainOne(c);const auto at=engineCalls;engineReply={};++liveGame.processCreated;
        steamOriginalRecoveryTargetChanged(liveGame.pid,liveGame.processCreated,liveGame.path,9);drainOne(c);
        check(engineCalls==at+2&&requests[at]["trigger"]["kind"]=="startup"&&requests[at]["recoveryAllowed"]==true&&requests[at+1]["recoveryAllowed"]==true,"old-pair-wake-blocked-new-game");
    });
    test("startup-pair-wake-new-steam-still-gets-startup",[]{
        auto c=reset(false);eventOwner(c);engineReply=[](const json&){return result("blocked",true);};
        steamOriginalRecoveryWake(7);drainOne(c);const auto at=engineCalls;engineReply={};++SleepSteamRecovery::live.created;
        steamOriginalRecoverySteamChanged();drainOne(c);
        check(engineCalls==at+2&&requests[at]["trigger"]["kind"]=="startup"&&requests[at]["recoveryAllowed"]==true,"old-pair-wake-blocked-new-steam");
    });
    test("startup-pair-wake-unqualified-event-grants-no-consumption",[]{
        auto c=reset(false);eventOwner(c);SteamGameLink::live.complete=false;run(c);
        check(engineCalls==0,"unqualified-wake-entered-engine");SteamGameLink::live.complete=true;
        SteamOriginalRecovery::runOne(c,{Trigger::TargetChanged,0,2},config);
        check(engineCalls==2&&requests[0]["trigger"]["kind"]=="startup"&&requests[1]["recoveryAllowed"]==true,"unqualified-wake-erased-startup");
    });
    test("startup-pair-wake-query-is-readonly-and-pair-scoped",[]{
        using namespace ymcc::steamOriginal;
        Admission admission;Scope scope{{1,1,11,L"parent"},{2,1,22,L"game"},{3,1,33,L"steam"},1};
        check(!admission.observedPair(scope,Trigger::Wake),"query-invented-wake");
        check(admission.admit(scope,{Trigger::Wake,7,1},true).accepted,"wake-policy-changed");
        const auto prior=admission.preview(scope,{Trigger::Startup,0,2},true);
        for(unsigned i=0;i<1000;++i)check(admission.observedPair(scope,Trigger::Wake),"query-lost-wake");
        const auto after=admission.preview(scope,{Trigger::Startup,0,3},true);
        check(prior.accepted==after.accepted&&prior.recoveryAllowed==after.recoveryAllowed,"query-mutated-original-policy");
        scope.rulesEpoch=2;scope.parent.birth=12;check(admission.observedPair(scope,Trigger::Wake),"parent-or-rules-refunded-pair");
        ++scope.game.birth;check(!admission.observedPair(scope,Trigger::Wake),"new-game-shares-old-wake");--scope.game.birth;
        ++scope.steam.birth;check(!admission.observedPair(scope,Trigger::Wake),"new-steam-shares-old-wake");
    });
    test("startup-pair-wake-explicit-startup-does-not-replay",[]{
        auto c=reset(false);eventOwner(c);engineReply=[](const json&){return result("blocked",true);};
        steamOriginalRecoveryWake(7);drainOne(c);const auto at=engineCalls;
        SteamOriginalRecovery::runOne(c,{Trigger::Startup,0,3},config);
        for(auto i=at;i<engineCalls;++i)check(requests[i]["trigger"]["kind"]!="startup"&&requests[i]["recoveryAllowed"]==false,"explicit-startup-replayed-wake");
    });
#endif
