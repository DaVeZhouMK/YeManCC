#pragma once
// Real pending/completion/queue primitives and enabled actual lifecycle drain.
// Legacy successful hide is simulated with the actual completion primitive;
// no physical hide, mode write, Host process, or device enumeration is run.
static bool msiLateHidePendingRegression(){
    using namespace ymcc::msi::latehide;unsigned cases=0,failed=0;std::ostringstream report;
    auto check=[&](const char* name,bool ok){++cases;if(!ok)++failed;report<<(ok?"PASS ":"FAIL ")<<name<<"\n";};
    for(bool enabled:{false,true})for(bool managed:{false,true})for(bool pending:{false,true}){
        const bool scope=required(enabled,managed);check("scope is build plus registered managed MSI only",scope==(enabled&&managed));
        check("empty pending denied only in MSI fix scope",std::string(decision(scope,pending,"apply"))==(scope&&!pending?"refused-msi-missing-pending":"apply"));
        check("empty expiry cannot overwrite completion in MSI scope",mayComplete(scope,pending)==(!scope||pending));
    }
    for(const char* refusal:{"skipped-expired","refused-window","refused-host","refused-source","refused-stale-token"}){
        check("real pending preserves old failure priorities",std::string(decision(true,true,refusal))==refusal);
        check("ROG/common preserves original no-pending decision",std::string(decision(false,false,refusal))==refusal);
    }
    if(g_inputCaptureThread||g_inputHostProcess){report<<"FAIL not an offline entry\n";return false;}
    const auto oldState=inputHostLateHideStateSnapshotForTest();const auto oldMachine=g_machineIdentity;
    const auto oldConfig=g_realStickTest;const auto oldApp=g_appDataDir;const auto oldSeam=g_startEntrySeam;
    const bool oldStop=g_inputCaptureStop.load(),oldFence=g_inputHostExitFence.load(),oldArm=g_msiDinputRouteDinputArmed.load();
    const auto oldEpoch=g_inputHostEpoch,oldEpochMirror=g_inputHostEpochMirror.load();const auto oldRun=g_inputHostRunId;
    const auto oldSettle=g_inputHostBuildSettleUntilTick;const bool oldRequested=g_inputHostLifecycleWorkRequested.load(),oldInFlight=g_inputHostLifecycleWorkInFlight.load();
    decltype(g_inputHostLifecycleWorkQueue) oldQueue;{std::lock_guard<std::mutex> l(g_inputHostLifecycleWorkMx);oldQueue=g_inputHostLifecycleWorkQueue;g_inputHostLifecycleWorkQueue.clear();}
    ULONGLONG oldEnqueuedTick=0;long oldEnqueuedCount=0;{std::lock_guard<std::mutex> l(g_switchAudit.mx);oldEnqueuedTick=g_switchAudit.enqueuedTick;oldEnqueuedCount=g_switchAudit.enqueuedCount;}
    const auto logDir=fspath::path(g_yemanccDataDir)/(L"late-hide-pending-native-"+std::to_wstring(GetTickCount64()));fspath::create_directories(logDir);
    g_nativeLifecycleLog.close();g_nativeLifecycleLog.clear();g_appDataDir=logDir.wstring();
    g_inputCaptureStop=false;g_inputHostExitFence=false;g_inputHostLifecycleWorkRequested=false;g_inputHostLifecycleWorkInFlight=false;
    g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";
    g_msiDinputRouteDinputArmed=true;g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=buildEnabled;g_realStickTest.virtualTargetPersona="elite";
    g_inputHostEpoch=7;g_inputHostEpochMirror=7;g_inputHostRunId="offline-E-epoch7";g_inputHostBuildSettleUntilTick=0;
    g_startEntrySeam=YmccStartEntrySeam{};g_startEntrySeam.active=true;g_startEntrySeam.hidHideApplyResult=true;
    const bool actualScope=inputHostLateHideManagedMsiRouteType();check("actual known MSI managed scope recognized",actualScope);
    InputHostLateHideEpisodeKey ep;ep.runId=g_inputHostRunId;ep.hostGen=7;ep.sourceGen=712;ep.managedMsiRoute=true;ep.recordGen=11;
    auto seed=[&](){inputHostLateHideStateRestoreForTest(InputHostLateHideStateSnapshot{});const auto token=inputHostLateHidePendingCommit(ep,"",false);inputHostLateHideMarkEpisodeCompleted(ep);check("valid completed pending can be cleared",inputHostLateHidePendingClearIfToken(token));return token;};
    auto token=seed();InputHostLateHidePendingView empty;check("real read reports cleared pending",!inputHostLateHidePendingRead(&empty));
    check("cleared view retains numeric token but loses owner/type/generations",empty.token==token&&empty.token>0&&empty.episode.runId.empty()&&empty.episode.hostGen==0&&!empty.episode.managedMsiRoute&&empty.episode.recordGen==0);
    const char* legacy=inputHostLateHideExecDecisionFor(false,false,false,empty.episode.runId,empty.episode.hostGen,ep.runId,7,empty.episode.managedMsiRoute,true,0,11,false);
    check("actual legacy predicate wrongly admits the cleared view",std::string(legacy)=="apply");
    const char* fixed=decision(required(buildEnabled,actualScope),empty.hasPending,legacy);
    check("actual cleared-view decision has exact flag differential",std::string(fixed)==(buildEnabled?"refused-msi-missing-pending":"apply"));
    if(std::string(fixed)=="apply")inputHostLateHideMarkEpisodeCompleted(empty.episode); // simulated successful hide, NO I/O
    check("empty completion overwrite reproduced only without the MSI gate",inputHostLateHideEpisodeAlreadyCompleted(ep)==buildEnabled);
    unsigned long long newToken=0;bool coalesced=false;
    const auto outcome=inputHostLateHideEnqueueEpisode(ep,"",false,&newToken,&coalesced);
    check("actual enqueue dedupe retains completed MSI episode with fix",outcome==(buildEnabled?InputHostLateHideEnqueueOutcome::AlreadyCompleted:InputHostLateHideEnqueueOutcome::Enqueued));
    check("fixed replay does not queue a replacement job",g_inputHostLifecycleWorkRequested.load()==!buildEnabled);
    {std::lock_guard<std::mutex> l(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();}g_inputHostLifecycleWorkRequested=false;
    // Actual full drain is safe in enabled mode because the production gate
    // refuses before Hide/snapshot I/O. Flag-off uses virtual-off to avoid I/O;
    // its unsafe successful-completion effect was reproduced above explicitly.
    if(actualScope){seed();inputHostQueueLifecycleWork("late-hide-after-source-return");inputHostRunQueuedLifecycleWork();
        check("actual lifecycle drain performs no hide",std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-apply")==0);
        check("actual enabled lifecycle drain preserves completed episode",inputHostLateHideEpisodeAlreadyCompleted(ep));
        check("actual lifecycle drain clears requested/in-flight flags",!g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load());
    }
    for(const auto family:{YmccFamily::AsusRogAlly,YmccFamily::AsusRogAllyX,YmccFamily::Unknown}){
        g_machineIdentity.family=family;const bool scope=required(buildEnabled,inputHostLateHideManagedMsiRouteType());
        check("actual ROG/common managed scope remains false",!scope&&std::string(decision(scope,false,"apply"))=="apply");
    }
    g_machineIdentity.family=YmccFamily::MsiClaw;
    for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}){
        g_machineIdentity.product=board;check("actual registered MSI scope has flag differential",required(buildEnabled,inputHostLateHideManagedMsiRouteType())==buildEnabled);
    }
    g_machineIdentity.product="unregistered";check("unregistered MSI excluded from pending fix",!required(buildEnabled,inputHostLateHideManagedMsiRouteType()));
    g_machineIdentity.product="MS-1T91";g_msiDinputRouteDinputArmed=false;check("disarmed MSI stays on original non-managed path",!required(buildEnabled,inputHostLateHideManagedMsiRouteType()));g_msiDinputRouteDinputArmed=true;
    const auto fixture=fspath::path(g_yemanccDataDir)/L"msi-e-empty-pending-real-fixture.json";
    if(fspath::exists(fixture)){std::ifstream in(fixture);json j;in>>j;
        check("exact same E PID and source return identified",j["nativePid"]==12544&&j["emptyPendingJobs"]==34&&j["cycleEvents"].empty());
        for(const auto& e:j["emptyJobs"]){const auto& q=e["dequeue"];check("actual return reports absent pending with legacy apply",q["hasPending"]==false&&q["hostGen"]==0&&e["decision"]["decision"]=="apply");
            check("each actual empty return receives scoped refusal with fix",std::string(decision(required(buildEnabled,true),q["hasPending"],"apply"))==(buildEnabled?"refused-msi-missing-pending":"apply"));}
    }
    inputHostLateHideStateRestoreForTest(oldState);g_machineIdentity=oldMachine;g_realStickTest=oldConfig;g_startEntrySeam=oldSeam;
    g_inputCaptureStop=oldStop;g_inputHostExitFence=oldFence;g_msiDinputRouteDinputArmed=oldArm;g_inputHostEpoch=oldEpoch;g_inputHostEpochMirror=oldEpochMirror;g_inputHostRunId=oldRun;g_inputHostBuildSettleUntilTick=oldSettle;
    {std::lock_guard<std::mutex> l(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue=oldQueue;}g_inputHostLifecycleWorkRequested=oldRequested;g_inputHostLifecycleWorkInFlight=oldInFlight;
    {std::lock_guard<std::mutex> l(g_switchAudit.mx);g_switchAudit.enqueuedTick=oldEnqueuedTick;g_switchAudit.enqueuedCount=oldEnqueuedCount;}
    g_nativeLifecycleLog.close();g_nativeLifecycleLog.clear();g_appDataDir=oldApp;
    report<<"cases="<<cases<<" failed="<<failed<<"\n";std::ofstream out(fspath::path(g_yemanccDataDir)/L"msi-late-hide-pending-native-entry.txt");out<<report.str();return failed==0&&out.good();
}

#ifdef YMCC_LATE_HIDE_PENDING_FULL_TU
// The full-TU harness owns seed/check/cases and its existing inert external ports.
// All queue, pending, dedupe, decision, completion and worker bodies are production
// functions checked byte-for-byte by input_coordinator_lifecycle_selftest.mjs.
// Completion is produced ONLY by the worker: no direct MarkEpisodeCompleted here.
static void msiLateHidePendingConsumerCases(){
    using Outcome=InputHostLateHideEnqueueOutcome;
    const auto prepare=[](){
        seed();
        fixtureWakeScenario=true; // existing visibility-snapshot/Sleep I/O ports
        g_inputCaptureStop=false;g_inputHostExitFence=false;
        g_startupInputState=ymcc::StartupInputState::Ready;
        g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;
        g_inputHostPowerAdmissionRevoked=false;g_sleepRecoveryOutputWanted=true;
        g_externalPriorityVirtualSuspended=false;g_inputHostBuildSettleUntilTick=0;
        g_realStickTest.virtualTargetPersona="elite";
        g_inputHostEpoch=7;g_inputHostEpochMirror=7;g_inputHostRunId="late-hide-offline-host-7";
        g_msiDinputRouteDinputArmed=true;
        g_physicalReleaseWindowStage=0;g_physicalReleaseWindowDeadline=0;
        {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);
            g_msiMainSource=HidHideMsiMainSourceRecord{};
            g_msiMainSource.valid=true;g_msiMainSource.generation=11;
            g_msiMainSource.interfacePath="offline-msi-main-source";
        }
        inputHostLateHideStateRestoreForTest(InputHostLateHideStateSnapshot{});
        {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);
            g_inputHostLifecycleWorkQueue.clear();
            g_inputHostLifecycleWorkRequested=false;g_inputHostLifecycleWorkInFlight=false;
        }
        g_hidHideVisibilityJournal=HidHideVisibilityJournal{};
        g_startEntrySeam.trace.clear();fixtureEvents.clear();
    };
    const auto episode=[](){
        InputHostLateHideEpisodeKey ep;ep.runId=g_inputHostRunId;
        ep.hostGen=7;ep.sourceGen=712;
        HidHideMsiMainSourceRecord source;const bool active=msiDinputManagedMainSourceActive(source);
        inputHostLateHideApplyEpisodeMetadata(&ep,inputHostLateHideManagedMsiRouteType(),active,source.generation);
        return ep;
    };
    const auto enqueue=[](const InputHostLateHideEpisodeKey& ep){
        unsigned long long token=0;bool coalesced=false;
        return inputHostLateHideEnqueueEpisode(ep,"",false,&token,&coalesced);
    };
    const auto hides=[](){return std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-apply");};
    const auto decision=[](){
        for(auto i=fixtureEvents.rbegin();i!=fixtureEvents.rend();++i)
            if(i->value("event","")=="late-hide-exec-decision")return (*i)["detail"].value("decision","");
        return std::string("missing-worker-decision");
    };
    const auto drained=[](){return !g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load();};
    const bool enabled=ymcc::msi::latehide::buildEnabled;
    prepare();cases.push_back({{"name","actual-worker-real-empty-same-real"},{"buildEnabled",enabled}});
    const auto ep=episode();
    check(inputHostLateHideManagedMsiRouteType()&&ep.managedMsiRoute&&ep.recordGen==11,"actual registered armed MSI source is in scope");
    check(enqueue(ep)==Outcome::Enqueued,"first real episode enters actual enqueue");
    inputHostRunQueuedLifecycleWork();
    check(hides()==1&&inputHostLateHideEpisodeAlreadyCompleted(ep),"real worker hides once and records actual episode completion");
    InputHostLateHidePendingView cleared;inputHostLateHidePendingRead(&cleared);
    check(!cleared.hasPending&&cleared.episode.runId.empty()&&cleared.episode.hostGen==0&&cleared.token>0,"real completion clears pending owner while retaining queue token");
    cases.back()["afterReal"]={{"hideCalls",hides()},{"completed",inputHostLateHideEpisodeAlreadyCompleted(ep)}};
    check(inputHostQueueLifecycleWork("late-hide-after-source-return"),"residual empty work enters actual lifecycle queue");
    inputHostRunQueuedLifecycleWork();
    check(decision()==(enabled?"refused-msi-missing-pending":"apply"),"actual empty worker decision matches compiled product policy");
    check(hides()==1,"empty leftover invokes no second hide");
    check(inputHostLateHideEpisodeAlreadyCompleted(ep),"empty leftover preserves the completed real episode");
    cases.back()["afterEmpty"]={{"hideCalls",hides()},{"completed",inputHostLateHideEpisodeAlreadyCompleted(ep)},{"decision",decision()}};
    const auto repeat=enqueue(ep);
    check(repeat==Outcome::AlreadyCompleted,"completed real episode enqueue is refused");
    inputHostRunQueuedLifecycleWork();
    unsigned sameRealApplied=0;
    for(const auto& e:fixtureEvents)if(e.value("event","")=="late-hide-complete"&&
        e["detail"].value("outcome","")=="applied"&&e["detail"].value("runId","")==ep.runId)++sameRealApplied;
    check(sameRealApplied==1,"same real episode is never hidden twice");
    check(drained(),"real empty real chain releases requested and in-flight flags");
    cases.back()["afterRepeat"]={{"hideCalls",hides()},{"sameRealApplied",sameRealApplied},{"enqueueOutcome",inputHostLateHideEnqueueOutcomeName(repeat)}};
    cases.back()["events"]=fixtureEvents;
    // Compatibility decisions use the same real empty-work consumer in both builds.
    struct ScopeCase{const char* name;YmccFamily family;const char* product;bool armed;};
    // Parameterize the existing compatibility cases; no new case/test IDs.
    // Real outer event producer + actual worker, only the existing hide/log ports inert.
    // Named field counterexample: 114820, ROG non-MSI epochs 3/4, initial
    // source-pending skip leaves no journal; reconfirm/connect both hit the old
    // no-hide-journal-and-no-wake-hold gate. Archive SHA256:
    // 08205ba9d182c981789a1a0765d2d518dd4e8c5a332fcaee84e32406c20ed43d
    // Native lines 517/518/799/801; Steam 39687-39724 and 39780-39803.
    // Attribution is archived field evidence; all execution here is inert replay.
    // No archived file/path is a runtime prerequisite. OFF remains zero-hide.
    const auto deferredSourceReturn=[&](const ScopeCase& row){
        const auto savedPower=currentPowerGeneration();
        const bool rog=row.family==YmccFamily::AsusRogAlly||row.family==YmccFamily::AsusRogAllyX;
        const auto pending=[](){InputHostLateHidePendingView p;inputHostLateHidePendingRead(&p);return p;};
        const auto depth=[](){std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);return g_inputHostLifecycleWorkQueue.size();};
        const auto detail=[](const char* name){for(auto i=fixtureEvents.rbegin();i!=fixtureEvents.rend();++i)
            if(i->value("event","")==name)return (*i)["detail"];return json::object();};
        const auto current=[](){return inputHostDeferredPhysicalHideCurrent(g_inputHostEpoch,g_inputHostRunId,
            currentPowerGeneration(),inputHostConfiguredPersona(),inputHostStartProtectionNow());};
        const auto prep=[&](bool arm,unsigned long long hostGen=7,bool refreshRequired=true){
            g_powerGeneration.store(savedPower,std::memory_order_release);prepare();
            fixtureLateHideDuringDecision={};fixtureLateHideAfterEmptyDecision={};
            g_machineIdentity.family=row.family;g_machineIdentity.product=row.product;
            if(rog){g_machineIdentity.manufacturer="ASUSTeK COMPUTER INC.";
                g_machineIdentity.product=row.family==YmccFamily::AsusRogAllyX?"RC72LA":"RC71L";}
            else if(row.family==YmccFamily::Gpd){g_machineIdentity.manufacturer="GPD";g_machineIdentity.product="G1617-01";}
            {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);g_msiMainSource=HidHideMsiMainSourceRecord{};}
            g_realStickTest.virtualTargetPersona="steamdeck";g_msiDinputRouteDinputArmed=row.armed;
            g_inputHostEpoch=hostGen;g_inputHostEpochMirror=hostGen;g_inputHostRunId="late-hide-offline-host-"+std::to_string(hostGen);
            g_physicalReleaseWindowNeutral=false;
            fixtureRogDeferredJournalSeed=fixtureRogDeferredCycleEnabled=rog;
            g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;
            const auto request=g_virtualStartProtection.admit("steamdeck",startProtectionClock);
            const auto deadline=startProtectionClock+g_virtualStartProtection.view(startProtectionClock).remainingMs;
            inputHostDeferredPhysicalHideArm(arm&&request.accepted,g_inputHostRunId,g_inputHostEpoch,
                currentPowerGeneration(),inputHostConfiguredPersona(),deadline,request.requestId,refreshRequired);
            return deadline;
        };
        const auto observe=[&](const std::string& variation){const auto p=pending();
            cases.back()["deferredStartHide"].push_back({{"variation",variation},{"hideCalls",hides()},
                {"hasPending",p.hasPending},{"token",p.token},{"pendingDeferred",p.episode.deferredStartHide},
                {"debtCurrent",current()},{"clock",startProtectionClock},{"events",fixtureEvents},
                {"hidePort",fixtureRogDeferredHideRecords},{"cycles",fixtureRogDeferredCycles},
                {"portQueries",fixtureRogDeferredPortQueries},{"trace",g_startEntrySeam.trace}});
        };
        if(row.family==YmccFamily::AsusRogAllyX)cases.back()["fieldCounterexample"]={
            {"name","114820-epoch3-4-ROG-source-pending-return-without-journal"},
            {"archiveSha256","08205ba9d182c981789a1a0765d2d518dd4e8c5a332fcaee84e32406c20ed43d"},
            {"nativeLines",json::array({517,518,799,801})},
            {"steamLineRanges",json::array({json::array({39687,39724}),json::array({39780,39803})})},
            {"oldGate","no-hide-journal-and-no-wake-hold"},{"offGuard","after-worker-decision/OFF"},
            {"actualStartArmCovered",false}};
        if(row.family==YmccFamily::AsusRogAllyX)cases.back()["refreshFieldCounterexample"]={
            {"name","131128-PID11164-epoch1-2-hide-readback-success-Steam-still-double"},
            {"archiveSha256","dc22b8e3448eb7409699f6812a2e20f13e56b464c2d43d1f0f5c71a25d4b3e0b"},
            {"consumerValidated",false},{"hardwareValidated",false},{"seamlessClaimed",false}};
        for(const char* reason:{"source-return-reconfirmed","xinput-connect"}){
            prep(false,rog?(std::strcmp(reason,"source-return-reconfirmed")==0?3ULL:4ULL):7ULL);
            // Same late-arrival ordering as the field failure, with no Start debt.
            g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;g_xinputConnectedMask=0;
            g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;
            inputHostQueueWakePhysicalHide(reason);
            check(!pending().hasPending&&depth()==0&&drained()&&
                detail("input-host-late-hide-decision").value("gate","")=="no-hide-journal-and-no-wake-hold",
                "real confirmed-source producer without Start debt or MSI authority cannot bypass no-journal gate");
        }
        inputHostRunQueuedLifecycleWork();check(hides()==0&&fixtureRogDeferredCycles.empty()&&drained(),"no-debt family source events dispatch no actual worker hide");
        observe("no-debt/both-confirmed-reasons");
        if(!rog)return;
        for(const char* reason:{"source-return-reconfirmed","xinput-connect"}){
            const auto deadline=prep(true,std::strcmp(reason,"source-return-reconfirmed")==0?3ULL:4ULL);
            g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;g_xinputConnectedMask=0;
            inputHostQueueWakePhysicalHide(reason);
            check(!pending().hasPending&&depth()==0&&hides()==0&&current(),
                "initial source-pending event cannot spend ROG Start debt before the exact source is live and locked");
            g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;
            inputHostQueueWakePhysicalHide(reason);const auto first=pending();
            check(nativeForegroundProfile()==ymcc::nativeForeground::Profile::Rog&&first.hasPending&&first.token>0&&
                first.episode.deferredStartHide&&!first.episode.managedMsiRoute&&first.episode.recordGen==0&&
                first.episode.hostGen==g_inputHostEpoch&&first.episode.runId==g_inputHostRunId&&depth()==1&&
                !g_hidHideVisibilityJournal.active&&!g_wakeHoldPhysicalUnhide.load()&&current(),
                "real ROG event producer commits exact deferred Host episode without journal or MSI authority");
            ++startProtectionClock;inputHostQueueWakePhysicalHide("xinput-connect");const auto repeated=pending();
            check(repeated.hasPending&&repeated.token==first.token&&repeated.firstTick==first.firstTick&&depth()==1&&
                !inputHostDeferredPhysicalHideCurrent(g_inputHostEpoch,g_inputHostRunId,currentPowerGeneration(),inputHostConfiguredPersona(),deadline),
                "duplicate confirmed source preserves one token first tick and original absolute deadline");
            inputHostRunQueuedLifecycleWork();
            check(hides()==1&&inputHostLateHideEpisodeAlreadyCompleted(first.episode)&&!pending().hasPending&&!current()&&drained(),
                "actual ROG worker hides late source once completes original episode and clears Start debt");
            const auto cycleCount=fixtureRogDeferredCycles.size();
            const auto hideAt=std::find(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-apply");
            const auto cycleAt=std::find(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-cycle:deferred-start-hide");
            check(cycleCount==1&&hideAt<cycleAt&&cycleAt!=g_startEntrySeam.trace.end()&&
                fixtureRogDeferredCycles[0].value("root","")==fixtureRogDeferredRoot()&&
                fixtureRogDeferredCycles[0].value("phase","")=="deferred-start-hide"&&
                fixtureRogDeferredCycles[0].value("budgetMs",0)==5000&&
                fixtureRogDeferredCycles[0].value("executionDeadline",0ULL)==deadline&&
                fixtureRogDeferredCycles[0].value("executionRequestId",0ULL)>0&&
                fixtureRogDeferredCycles[0].value("executionRequestId",0ULL)==fixtureRogDeferredCycles[0].value("requestId",0ULL)&&
                fixtureRogDeferredCycles[0].value("executionPower",0ULL)==currentPowerGeneration()&&
                fixtureRogDeferredHideRecords.size()==1&&!fixtureRogDeferredHideRecords[0].value("powerCycle",true),
                (std::string("ROG131128 first hide(false) precedes exactly one bounded owned-root cycle: ")+reason).c_str());
            check(detail("late-hide-complete").value("deferredRefreshOutcome","")=="owned-root-refreshed"&&
                detail("hidhide.p-hid-late-attempt").value("powerCycle",false)&&
                !detail("hidhide.p-hid-late-attempt").value("hideTransactionPowerCycle",true),
                (std::string("ROG131128 applied receipt requires refresh ACK not only hide readback: ")+reason).c_str());
            inputHostQueueWakePhysicalHide(reason);inputHostRunQueuedLifecycleWork();
            check(hides()==2&&fixtureRogDeferredCycles.size()==cycleCount&&depth()==0&&drained(),
                "consumed Start debt leaves ordinary journal reconnect hide without another cycle");
            observe(std::string("confirmed-return/")+reason);
        }
        if(row.family==YmccFamily::AsusRogAllyX){
            enum class End{Off,CaptureOff,Host,Run,Persona,Power,Deadline,Release,Exit,Unlocked,Absent,Cleared,PowerRevoked};
            struct Boundary{const char* name;End value;};
            const Boundary boundaries[]={{"OFF",End::Off},{"capture-OFF",End::CaptureOff},{"Host",End::Host},
                {"run",End::Run},{"persona",End::Persona},{"power",End::Power},{"original-deadline",End::Deadline},
                {"release",End::Release},{"exit",End::Exit},{"unlocked-source",End::Unlocked},
                {"absent-source",End::Absent},{"cleared-debt",End::Cleared},{"power-revoked",End::PowerRevoked}};
            const auto invalidate=[](End e,unsigned long long deadline){switch(e){
                case End::Off:g_realStickTest.virtualTargetEnabled=false;break;
                case End::CaptureOff:g_realStickTest.enabled=false;break;
                case End::Host:++g_inputHostEpoch;g_inputHostEpochMirror=g_inputHostEpoch;break;
                case End::Run:g_inputHostRunId+="-replacement";break;
                case End::Persona:g_realStickTest.virtualTargetPersona="dualsense-edge";break;
                case End::Power:g_powerGeneration.fetch_add(1,std::memory_order_acq_rel);break;
                case End::Deadline:startProtectionClock=deadline;break;
                case End::Release:g_physicalReleaseWindowNeutral=true;break;
                case End::Exit:g_inputHostExitFence=true;break;
                case End::Unlocked:g_xinputPhysicalSourceLocked=false;break;
                case End::Absent:g_xinputConnectedMask=0;break;
                case End::Cleared:inputHostDeferredPhysicalHideClear();break;
                case End::PowerRevoked:g_inputHostPowerAdmissionRevoked=true;break;
            }};
            for(const auto& boundary:boundaries){
                // Power revocation is an execution guard, not a new promise
                // that the existing event producer suppresses its notification.
                if(boundary.value!=End::PowerRevoked){
                    const auto deadline=prep(true);invalidate(boundary.value,deadline);
                    inputHostQueueWakePhysicalHide("source-return-reconfirmed");inputHostRunQueuedLifecycleWork();
                    check(!pending().hasPending&&depth()==0&&hides()==0&&fixtureRogDeferredCycles.empty()&&drained(),
                        (std::string("real deferred producer rejects invalid context before publication: ")+boundary.name).c_str());
                    observe(std::string("before-producer/")+boundary.name);
                }
                const auto deadline=prep(true);inputHostQueueWakePhysicalHide("source-return-reconfirmed");const auto queued=pending();
                check(queued.hasPending&&queued.episode.deferredStartHide&&depth()==1,
                    (std::string("actual producer queues debt before final context invalidation: ")+boundary.name).c_str());
                bool injected=false;fixtureLateHideDuringDecision=[&](){injected=true;invalidate(boundary.value,deadline);};
                inputHostRunQueuedLifecycleWork();const auto completed=detail("late-hide-complete");
                check(injected&&hides()==0&&fixtureRogDeferredCycles.empty()&&!inputHostLateHideEpisodeAlreadyCompleted(queued.episode)&&drained()&&
                    (boundary.value==End::Off?
                        completed.value("outcome","")=="skipped"&&completed.value("decision","")=="skipped-gate"&&
                        completed.value("gate","")=="virtual-target-not-enabled":
                        completed.value("outcome","")=="refused-deferred-start-context"&&
                        !completed.value("deferredStartContextCurrent",true)),
                    (std::string("actual worker final gate refuses stale deferred context without hide or completion: ")+boundary.name).c_str());
                observe(std::string("after-worker-decision/")+boundary.name);
            }
            prep(true);inputHostQueueWakePhysicalHide("wake-physical-return");inputHostRunQueuedLifecycleWork();
            check(!pending().hasPending&&depth()==0&&hides()==0&&current()&&drained(),
                "unknown source reason cannot consume an otherwise current deferred Start responsibility");
            observe("unknown-source-reason");
            // Transaction-stage delay counterexamples: execute the actual
            // durable-context and pipe/CLI admission predicates after injecting
            // invalidation. No real slow query, reservePath, pipe or CLI runs.
            for(const char* stage:{"after-query","after-durable-reservation"})
                for(const auto end:{End::Off,End::Host,End::Power,End::Persona,End::Deadline}) {
                    const auto deadline=prep(true);
                    InputHostDeferredPhysicalHide debt;
                    {std::lock_guard<std::mutex> guard(g_inputHostDeferredPhysicalHideMx);debt=g_inputHostDeferredPhysicalHide;}
                    const auto priorScope=t_inputHostDeferredPhysicalHideWrite;
                    const auto priorDeadline=g_inputHostSwitchRecoveryStartDeadline;
                    t_inputHostDeferredPhysicalHideWrite=&debt;
                    g_inputHostSwitchRecoveryStartDeadline=deadline;
                    check(inputHostDeferredPhysicalHideWriteCurrent() && inputHostFrameSubmissionMayIssue(nullptr) &&
                        ymcc::inputHost::targetReturnRuntime::contextCurrent() &&
                        inputHostRequestHasExecutionDeadline("HIDHIDE_QUERY") &&
                        inputHostRequestHasExecutionDeadline("HIDHIDE_HIDE") &&
                        inputHostRequestHasExecutionDeadline("HIDHIDE_CYCLE"),
                        "same original debt reaches transaction predicates with original query/write deadline");
                    invalidate(end,deadline);
                    check(!ymcc::inputHost::targetReturnRuntime::contextCurrent(),
                        "delayed query or durable commit cannot reserve/adopt hide with retired debt");
                    json response;
                    check(!inputHostFrameSubmissionRequestCurrent("HIDHIDE_HIDE",&response,"delayed-hide-write") &&
                        response.value("status","")=="visibility-context-retired" &&
                        !response.value("deviceWriteAttempted",true),
                        "actual final pipe admission rejects retired debt before issuing hide");
                    json cycleResponse,unhideResponse;
                    check(!inputHostFrameSubmissionRequestCurrent("HIDHIDE_CYCLE",&cycleResponse,"delayed-cycle-write") &&
                        cycleResponse.value("status","")=="visibility-context-retired" &&
                        !cycleResponse.value("deviceWriteAttempted",true) &&
                        inputHostFrameSubmissionRequestCurrent("HIDHIDE_UNHIDE",&unhideResponse,"owned-unhide") &&
                        fixtureRogDeferredCycles.empty(),
                        "actual CYCLE RequestCurrent rejects retired debt while owned UNHIDE remains permitted");
                    check(!inputHostFrameSubmissionMayIssue(nullptr),
                        "actual transport issue guard rejects retired debt after buffer and event preparation");
                    check(!inputHostHidHideApplyViaPipeImpl(L"hide",L"HID_TEST_OWNED",false),
                        "actual pipe dispatch entry cannot pass retired debt to its transport");
                    check(!inputHostDeferredPhysicalHideCliCurrent(L"--dev-hide HID_TEST_OWNED") &&
                        !inputHostDeferredPhysicalHideCliCurrent(L"--cloak-on") &&
                        !inputHostDeferredPhysicalHideCliCurrent(L"--app-reg offline-reader") &&
                        inputHostDeferredPhysicalHideCliCurrent(L"--dev-unhide HID_TEST_OWNED"),
                        "shared actual CLI admission forbids fallback hide but permits owned cleanup direction");
                    check(g_inputHostSwitchRecoveryStartDeadline==deadline && hides()==0 && forbiddenIo==0,
                        "delay invalidation never renews original deadline or issues external I/O");
                    t_inputHostDeferredPhysicalHideWrite=priorScope;
                    g_inputHostSwitchRecoveryStartDeadline=priorDeadline;
                    observe(std::string("transaction-stage/")+stage+"/"+std::to_string(static_cast<int>(end)));
                }
            prep(true);g_startEntrySeam.hidHideApplyResult=false;
            inputHostQueueWakePhysicalHide("source-return-reconfirmed");const auto failed=pending();inputHostRunQueuedLifecycleWork();
            check(failed.hasPending&&failed.episode.deferredStartHide&&hides()==1&&
                !inputHostLateHideEpisodeAlreadyCompleted(failed.episode)&&current()&&drained()&&
                !g_hidHideVisibilityJournal.active&&fixtureRogDeferredCycles.empty(),
                "failed actual deferred hide retains debt and never claims completion");
            g_startEntrySeam.hidHideApplyResult=true;inputHostQueueWakePhysicalHide("xinput-connect");const auto retry=pending();inputHostRunQueuedLifecycleWork();
            check(retry.hasPending&&retry.token>failed.token&&inputHostLateHideEpisodeEqual(retry.episode,failed.episode)&&
                hides()==2&&inputHostLateHideEpisodeAlreadyCompleted(retry.episode)&&!current()&&drained(),
                "next confirmed source retries failed deferred hide and clears debt only after actual success");
            observe("failed-hide-retry");
            check(fixtureRogDeferredCycles.size()==1,
                "ROG131128 failed hide seeds no journal and only successful hide retry refreshes");

        }

        // ROG131128 additions stay in the original ROG/ROG-X compatibility rows.
        // These exercise the actual worker/helper, never call helper directly.
        const auto attempted=[](){std::lock_guard<std::mutex> guard(g_inputHostDeferredPhysicalHideMx);
            return g_inputHostDeferredPhysicalHide.refreshAttempted;};
        const auto debtRetained=[](){std::lock_guard<std::mutex> guard(g_inputHostDeferredPhysicalHideMx);
            return !g_inputHostDeferredPhysicalHide.runId.empty();};
        const auto refreshCheck=[&](bool ok,const char* assertion,const std::string& variation){
            check(ok,(std::string("ROG131128 ")+assertion+": "+variation).c_str());};
        const auto dispatch=[&](){inputHostQueueWakePhysicalHide("source-return-reconfirmed");
            const auto queued=pending();inputHostRunQueuedLifecycleWork();return queued;};
        // A retained journal belongs to an earlier initial hide/refresh, not a
        // fresh permission to cycle again after Arm(...,refreshRequired=false).
        prep(true,7,false);fixtureRogDeferredSeedJournal(g_inputHostRunId);
        const auto retained=dispatch();
        refreshCheck(retained.hasPending&&hides()==1&&fixtureRogDeferredCycles.empty()&&
            inputHostLateHideEpisodeAlreadyCompleted(retained.episode)&&!debtRetained()&&
            detail("late-hide-complete").value("deferredRefreshOutcome","")=="not-required",
            "retained journal refreshRequired=false adds no cycle","retained-journal");
        observe("retained-journal/no-new-refresh");

        prep(true);bool sourceDropped=false;
        fixtureRogDeferredCycleCallback=[&](){sourceDropped=true;g_xinputPhysicalSourceLocked=false;
            g_xinputConnectedMask=g_xinputPhysicalSourceMask=0;};
        const auto dropped=dispatch();
        refreshCheck(sourceDropped&&fixtureRogDeferredCycles.size()==1&&
            !g_xinputPhysicalSourceLocked&&g_xinputConnectedMask==0&&
            inputHostLateHideEpisodeAlreadyCompleted(dropped.episode)&&!debtRetained()&&
            detail("late-hide-complete").value("outcome","")=="applied"&&
            detail("late-hide-complete").value("deferredRefreshOutcome","")=="owned-root-refreshed",
            "successful cycle ACK may drop raw source and still complete current intent","source-drop");
        inputHostQueueWakePhysicalHide("xinput-connect");inputHostRunQueuedLifecycleWork();
        refreshCheck(fixtureRogDeferredCycles.size()==1&&!debtRetained()&&drained(),
            "offline raw notification cannot repeat cycle","source-drop");
        g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=g_xinputConnectedMask=1;
        inputHostQueueWakePhysicalHide("source-return-reconfirmed");inputHostRunQueuedLifecycleWork();
        refreshCheck(fixtureRogDeferredCycles.size()==1&&!debtRetained()&&drained(),
            "ordinary reconnect after cycle adds no cycle","source-drop");
        observe("cycle-ACK/source-drop-and-reconnect");

        for(const unsigned remaining:{4999u,1u}){
            const auto deadline=prep(true);startProtectionClock=deadline-remaining;
            const auto tight=dispatch();
            refreshCheck(fixtureRogDeferredCycles.size()==1&&
                fixtureRogDeferredCycles[0].value("budgetMs",0u)==remaining&&
                fixtureRogDeferredCycles[0].value("deadline",0ULL)==deadline&&
                fixtureRogDeferredCycles[0].value("executionDeadline",0ULL)==deadline&&
                inputHostLateHideEpisodeAlreadyCompleted(tight.episode)&&!debtRetained(),
                "cycle consumes only original remaining deadline",std::to_string(remaining));
            observe("remaining-budget/"+std::to_string(remaining));
        }
        for(const char* result:{"failure","unknown"}){
            prep(true);fixtureRogDeferredCycleResult=result;const auto first=dispatch();
            refreshCheck(first.hasPending&&fixtureRogDeferredCycles.size()==1&&attempted()&&debtRetained()&&
                !inputHostLateHideEpisodeAlreadyCompleted(first.episode)&&
                detail("late-hide-complete").value("applied",true)==false&&
                detail("late-hide-complete").value("deferredRefreshOutcome","")=="refresh-failed",
                "failed or unknown cycle retains debt without completion",result);
            inputHostQueueWakePhysicalHide("xinput-connect");inputHostRunQueuedLifecycleWork();
            refreshCheck(fixtureRogDeferredCycles.size()==1&&attempted()&&debtRetained()&&
                !inputHostLateHideEpisodeAlreadyCompleted(first.episode)&&
                detail("late-hide-complete").value("deferredRefreshOutcome","")=="refresh-unproven-no-reissue",
                "failed or unknown cycle cannot reissue on second event",result);
            observe(std::string("cycle-result/")+result);
        }
        prep(true);g_hidHideCycleInFlight=true;g_hidHideCycleBusySinceTick=GetTickCount64();
        const auto busy=dispatch();
        refreshCheck(fixtureRogDeferredCycles.empty()&&!attempted()&&debtRetained()&&
            !inputHostLateHideEpisodeAlreadyCompleted(busy.episode)&&
            detail("late-hide-complete").value("deferredRefreshOutcome","")=="refresh-busy-deferred",
            "busy refresh preserves unspent single attempt","busy");
        g_hidHideCycleInFlight=false;g_hidHideCycleBusySinceTick=0;
        inputHostQueueWakePhysicalHide("xinput-connect");inputHostRunQueuedLifecycleWork();
        refreshCheck(fixtureRogDeferredCycles.size()==1&&inputHostLateHideEpisodeAlreadyCompleted(busy.episode)&&!debtRetained(),
            "cleared busy permits first actual cycle","busy");
        observe("busy/no-attempt-until-available");

        for(const char* shape:{"missing-journal","unresolved","empty-root","unowned-root","wrong-transaction",
                "mixed-roots","unresolved-source","wrong-source-vendor"}){
            prep(true);fixtureRogDeferredAfterHide=[&](){auto& j=g_hidHideVisibilityJournal;
                const std::string value=shape;
                if(value=="missing-journal")j.active=false;
                else if(value=="unresolved")j.physical.resolved=false;
                else if(value=="empty-root")j.physical.baseContainerDeviceInstanceId.clear();
                else if(value=="unowned-root")j.hiddenByUs.clear();
                else if(value=="wrong-transaction")j.transactionId+="-other-owner";
                else if(value=="mixed-roots"){auto other=j.physical;other.baseContainerDeviceInstanceId+="_OTHER";j.physicalSources.push_back(other);}
                else if(value=="unresolved-source")j.physicalSources[0].resolved=false;
                else j.physicalSources[0].vendorId=0x045e;
            };
            const auto invalid=dispatch();
            refreshCheck(fixtureRogDeferredCycles.empty()&&fixtureRogDeferredPortQueries.empty()&&!attempted()&&
                debtRetained()&&!inputHostLateHideEpisodeAlreadyCompleted(invalid.episode)&&
                !detail("late-hide-complete").value("applied",true),
                "unproven exact journal forbids cycle and completion",shape);
            observe(std::string("journal/")+shape);
        }
        for(const char* shape:{"same-VID-other-PID","wrong-root","virtual-game","wrong-usage","045E-policy"}){
            prep(true);fixtureRogDeferredAfterHide=[&](){auto& p=g_hidHideVisibilityJournal.physical;
                const std::string value=shape;
                if(value=="same-VID-other-PID")p.productId=0xffff;
                else if(value=="wrong-root")p.baseContainerDeviceInstanceId="HID\\VID_0B05&PID_1B4C\\WRONG_ROOT";
                else if(value=="virtual-game")p.deviceInstanceId="ROOT\\VIGEMBUS\\0000";
                else if(value=="wrong-usage")p.usage=2;
                else{p.vendorId=0x045e;p.productId=0x028e;p.baseContainerDeviceInstanceId="USB\\VID_045E&PID_028E\\POLICY";}
            };
            const auto nonTarget=dispatch();
            refreshCheck(fixtureRogDeferredCycles.empty()&&fixtureRogDeferredPortQueries.empty()&&
                inputHostLateHideEpisodeAlreadyCompleted(nonTarget.episode)&&!debtRetained()&&
                detail("late-hide-complete").value("deferredRefreshOutcome","")=="not-rog-game-root",
                "non-target game identity preserves no-cycle policy",shape);
            observe(std::string("non-target/")+shape);
        }
        for(const int portKind:{1,-1}){
            prep(true);fixtureRogDeferredPortKind=portKind;const auto external=dispatch();
            refreshCheck(fixtureRogDeferredPortQueries.size()==1&&fixtureRogDeferredCycles.empty()&&!attempted()&&
                debtRetained()&&!inputHostLateHideEpisodeAlreadyCompleted(external.episode)&&
                detail("late-hide-complete").value("deferredRefreshOutcome","")==
                    (portKind>0?"external-port-no-refresh":"internal-port-unproven"),
                "external or unknown topology forbids cycle and completion",std::to_string(portKind));
            observe("port-kind/"+std::to_string(portKind));
        }
        // Invalidate at real external port boundaries, not inside the helper.
        for(const char* stage:{"after-hide","after-port-query"})
            for(const char* boundary:{"OFF","Host","power","deadline","exit","release"}){
                const auto deadline=prep(true);
                const auto retire=[&](){const std::string name=boundary;
                    if(name=="OFF")g_realStickTest.virtualTargetEnabled=false;
                    else if(name=="Host"){++g_inputHostEpoch;g_inputHostEpochMirror=g_inputHostEpoch;}
                    else if(name=="power")g_powerGeneration.fetch_add(1,std::memory_order_acq_rel);
                    else if(name=="deadline")startProtectionClock=deadline;
                    else if(name=="exit")g_inputHostExitFence=true;
                    else g_physicalReleaseWindowNeutral=true;
                };
                if(std::strcmp(stage,"after-hide")==0)fixtureRogDeferredAfterHide=retire;
                else fixtureRogDeferredPortCallback=retire;
                const auto retired=dispatch();
                const auto complete=detail("late-hide-complete");
                refreshCheck(fixtureRogDeferredCycles.empty()&&!attempted()&&debtRetained()&&
                    !inputHostLateHideEpisodeAlreadyCompleted(retired.episode)&&
                    complete.value("outcome","")=="refused-deferred-start-context"&&
                    complete.value("deferredContextPhase","")=="transaction-or-refresh"&&
                    !complete.value("deferredStartContextReason","").empty()&&
                    complete.value("deferredRefreshOutcome","")==
                        (std::strcmp(stage,"after-hide")==0?"context-retired":"context-retired-after-port-query"),
                    "retired original intent at port boundary forbids cycle",std::string(stage)+"/"+boundary);
                observe(std::string(stage)+"/"+boundary);
            }
        for(const char* boundary:{"OFF","Host","power","deadline"}){
            const auto deadline=prep(true);const auto host=g_inputHostEpoch;
            fixtureRogDeferredCycleCallback=[&](){const std::string name=boundary;
                if(name=="OFF")g_realStickTest.virtualTargetEnabled=false;
                else if(name=="Host"){++g_inputHostEpoch;g_inputHostEpochMirror=g_inputHostEpoch;}
                else if(name=="power")g_powerGeneration.fetch_add(1,std::memory_order_acq_rel);
                else startProtectionClock=deadline;
            };
            const auto retired=dispatch();const auto complete=detail("late-hide-complete");
            refreshCheck(fixtureRogDeferredCycles.size()==1&&attempted()&&debtRetained()&&
                !inputHostLateHideEpisodeAlreadyCompleted(retired.episode)&&
                complete.value("outcome","")=="refused-deferred-start-context"&&
                complete.value("deferredRefreshOutcome","")=="refresh-completed-context-retired"&&
                complete.value("deferredStartContextReason","")==
                    (std::strcmp(boundary,"OFF")==0?"virtual-target-disabled":
                     std::strcmp(boundary,"Host")==0?"host-generation-changed":
                     std::strcmp(boundary,"power")==0?"power-generation-changed":"original-deadline-expired"),
                "cycle ACK cannot complete retired original intent",boundary);
            // Restore observations only, never Arm/replenish the original debt.
            g_realStickTest.virtualTargetEnabled=true;g_inputHostEpoch=host;g_inputHostEpochMirror=host;
            g_powerGeneration.store(savedPower,std::memory_order_release);startProtectionClock=deadline-1;
            inputHostQueueWakePhysicalHide("xinput-connect");inputHostRunQueuedLifecycleWork();
            refreshCheck(fixtureRogDeferredCycles.size()==1&&attempted()&&debtRetained()&&
                !inputHostLateHideEpisodeAlreadyCompleted(retired.episode)&&
                detail("late-hide-complete").value("deferredRefreshOutcome","")=="refresh-unproven-no-reissue",
                "retired cycle ACK consumes single attempt without reissue",boundary);
            observe(std::string("cycle-ACK-retired/")+boundary);
        }
        // 151907: run the original HELLO-only execution repair, not a new
        // reconfirm policy. Both old empty identities must still be refused.
        struct InitialHostEvent {
            HANDLE handle=CreateEventW(nullptr,TRUE,FALSE,nullptr);
            ~InitialHostEvent(){if(g_inputHostProcess==handle)g_inputHostProcess=nullptr;if(handle)CloseHandle(handle);}
        } initialHost;
        if(!initialHost.handle)throw std::runtime_error("initial-ROG-fixture-event");
        const auto initialPrep=[&](){
            prep(false);fixtureRogInitialIdentityEnabled=true;
            g_inputHostProcess=initialHost.handle;g_inputHostHello=true;g_inputHostPrepared=false;
            g_inputHostPrepareMayHaveReachedHost=false;g_inputHostBoundPowerGeneration=currentPowerGeneration();
            g_inputHostBoundPersona="steamdeck";g_inputHostBuildPersona="steamdeck";
            g_hidHideVisibilityJournal={};g_hidHidePhysicalIdentity={};
            g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=0;g_xinputConnectedMask=1;
            g_xinputPreTargetSlots=1;g_xinputSourceReconfirmMask=1;g_xinputReconfirmIdentity.clear();
            g_xinputSlotUnknownMask=g_xinputVirtualEndpointMask=0;
            g_xinputPreTargetBoundaryScanned=true;g_xinputBoundaryScanEpoch=g_xinputTopologyEpoch.load();
            g_xinputSlotReprobeMask=0;g_xinputSlotIdentityResolvedEpoch[0]=g_xinputSlotIdentityEpoch.load();
            return g_virtualStartProtection.view(startProtectionClock).requestId;
        };
        const auto initialCheck=[&](bool ok,const std::string& name){check(ok,("ROG151907 "+name).c_str());};
        auto initialTicket=initialPrep();const auto initialDeadline=g_virtualStartProtection.view(startProtectionClock).remainingMs+startProtectionClock;
        initialCheck(ymStartRecoverRogPendingIdentity(initialTicket)&&g_xinputPhysicalSourceLocked&&
            g_xinputPhysicalSourceMask==1&&g_xinputSourceReconfirmMask.load()==0&&
            g_hidHidePhysicalIdentity.physicalIdentity=="initial-ROG-physical-identity"&&
            fixtureRogInitialDiscoveryReads==1&&traced("admit:true"),
            "fresh unique internal identity re-enters original physical admission before target");
        initialCheck(!g_inputHostPrepared&&!g_inputHostPrepareMayHaveReachedHost&&fixtureRogDeferredCycles.empty()&&
            g_virtualStartProtection.view(startProtectionClock).remainingMs+startProtectionClock==initialDeadline&&
            g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0&&
            nativeLifecycleEventIsBase("input-host-rog-source-before-target"),
            "execution recovery creates no target cycle fallback or renewed budget and logs by default");
        initialCheck(!ymStartRecoverRogPendingIdentity(initialTicket)&&fixtureRogInitialDiscoveryReads==1,
            "same source success never triggers another identity scan");
        for(const char* invalid:{"known-identity","saved-identity","prepared","prepare-issued","journal","multiple-slots",
                "unknown-slot","virtual-slot","stale-scan","reprobe-pending","slot-epoch-stale","OFF","expired","wrong-ticket","release","MSI","GPD"}){
            initialTicket=initialPrep();const std::string kind=invalid;
            if(kind=="known-identity")g_hidHidePhysicalIdentity.resolved=true;
            else if(kind=="saved-identity")g_xinputReconfirmIdentity="known-original";
            else if(kind=="prepared")g_inputHostPrepared=true;
            else if(kind=="prepare-issued")g_inputHostPrepareMayHaveReachedHost=true;
            else if(kind=="journal")g_hidHideVisibilityJournal.active=true;
            else if(kind=="multiple-slots")g_xinputConnectedMask=3;
            else if(kind=="unknown-slot")g_xinputSlotUnknownMask=1;
            else if(kind=="virtual-slot")g_xinputVirtualEndpointMask=1;
            else if(kind=="stale-scan")g_xinputBoundaryScanEpoch=0;
            else if(kind=="reprobe-pending")g_xinputSlotReprobeMask=1;
            else if(kind=="slot-epoch-stale")g_xinputSlotIdentityResolvedEpoch[0]=0;
            else if(kind=="OFF")g_realStickTest.virtualTargetEnabled=false;
            else if(kind=="expired")startProtectionClock+=30000;
            else if(kind=="wrong-ticket")++initialTicket;
            else if(kind=="release")g_physicalReleaseWindowNeutral=true;
            else if(kind=="MSI"){g_machineIdentity.family=YmccFamily::MsiClaw;g_machineIdentity.manufacturer="MICRO-STAR INTERNATIONAL CO., LTD.";g_machineIdentity.product="MS-1T91";}
            else {g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.manufacturer="GPD";g_machineIdentity.product="G1617-01";}
            initialCheck(!ymStartRecoverRogPendingIdentity(initialTicket)&&fixtureRogInitialDiscoveryReads==0&&
                g_xinputPhysicalSourceMask==0&&g_xinputSourceReconfirmMask.load()==1,
                std::string("pre-query boundary preserves original pending: ")+invalid);
        }
        for(const char* shape:{"incomplete","empty","multiple","other-PID","empty-identity","virtual","external","unknown-port"}){
            initialTicket=initialPrep();const std::string kind=shape;
            if(kind=="incomplete")fixtureRogInitialDiscoveryComplete=false;
            else if(kind=="external")fixtureRogDeferredPortKind=1;
            else if(kind=="unknown-port")fixtureRogDeferredPortKind=-1;
            else fixtureRogInitialShape=kind;
            initialCheck(!ymStartRecoverRogPendingIdentity(initialTicket)&&fixtureRogInitialDiscoveryReads==1&&
                !g_hidHidePhysicalIdentity.resolved&&g_xinputPhysicalSourceMask==0&&fixtureRogDeferredCycles.empty(),
                std::string("unproven current physical source stays pending: ")+shape);
        }
        for(const char* stage:{"PnP-read","port-read"})for(const char* boundary:{"OFF","Host","power","deadline","topology","cache","known-source","prepared","reprobe","slot-epoch"}){
            initialTicket=initialPrep();const auto deadline=startProtectionClock+30000;
            const auto retire=[&](){const std::string kind=boundary;
                if(kind=="OFF")g_realStickTest.virtualTargetEnabled=false;
                else if(kind=="Host")++g_inputHostEpoch;
                else if(kind=="power")g_powerGeneration.fetch_add(1);
                else if(kind=="deadline")startProtectionClock=deadline;
                else if(kind=="topology")g_xinputTopologyEpoch.fetch_add(1);
                else if(kind=="cache")g_hidResolveCacheGeneration.fetch_add(1);
                else if(kind=="known-source")g_hidHidePhysicalIdentity.resolved=true;
                else if(kind=="reprobe")g_xinputSlotReprobeMask=1;
                else if(kind=="slot-epoch")g_xinputSlotIdentityResolvedEpoch[0]=0;
                else g_inputHostPrepared=true;
            };
            if(std::strcmp(stage,"PnP-read")==0)fixtureRogInitialDiscoveryCallback=retire;
            else fixtureRogDeferredPortCallback=retire;
            initialCheck(!ymStartRecoverRogPendingIdentity(initialTicket)&&g_xinputPhysicalSourceMask==0&&
                g_xinputSourceReconfirmMask.load()==1&&fixtureRogDeferredCycles.empty(),
                std::string("query cannot adopt retired observation: ")+stage+"/"+boundary);
        }
        // Also consume the real production Start insertion, so deleting its
        // call while retaining the helper cannot leave this regression green.
        initialTicket=initialPrep();
        g_inputHostProcess=nullptr;g_inputHostHello=false;g_inputHostEpoch=0;g_inputHostEpochMirror=0;
        g_inputHostRunId.clear();g_inputHostBoundPersona.clear();g_inputHostBuildPersona.clear();
        const auto previousTerminate=std::set_terminate(fixtureOffTerminate);
        const auto previousInvalid=_set_invalid_parameter_handler(fixtureOffInvalidParameter);
        fixturePersistOffCheckpoints=true;fixtureOffCheckpoint("ROG-initial-Start-before");
        const bool completeStart=inputHostStart();
        fixtureOffCheckpoint("ROG-initial-Start-after");

        const auto hideAt=std::find(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"hidhide-apply");
        const auto prepareAt=std::find(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"pipe:PREPARE_TARGET");
        initialCheck(completeStart&&fixtureRogInitialDiscoveryReads==1&&g_xinputPhysicalSourceMask==1&&
            g_xinputSourceReconfirmMask.load()==0&&g_inputHostPrepared&&
            detail("input-host-started").value("physicalSourcePending",true)==false,
            "actual Start repairs empty initial identity before target instead of publishing source-pending");
        initialCheck(hideAt!=g_startEntrySeam.trace.end()&&prepareAt!=g_startEntrySeam.trace.end()&&hideAt<prepareAt&&
            fixtureRogDeferredCycles.empty()&&g_virtualStartProtection.view(startProtectionClock).requestId==initialTicket,
            "actual Start preserves original hide before PREPARE target and original player request");
        inputHostStop("offline-owned-cleanup");
        fixtureOffCheckpoint("ROG-initial-Stop-after");fixturePersistOffCheckpoints=false;
        std::set_terminate(previousTerminate);_set_invalid_parameter_handler(previousInvalid);
        cases.back()["initialIdentityFieldCounterexample"]={{"nativePid",10440},{"hostEpochs",json::array({1,2})},
            {"fieldStartTimes",json::array({"2026-11-03T14:43:19.865","2026-11-03T14:48:28.595"})},
            {"savedIdentity",""},{"currentIdentity",""},{"oldResult","source remains locked/pending until PS5 switch"},
            {"actualCompleteStartArmCovered",false},{"physicalDiscoveryAndTopologyInert",true}};
        // ROG164155 PID11292: the child hide returned, the original root
        // reservation survived but dispatch did not. No initial refresh ran.
        // This is one original execution completion, not Stop/Start recovery.
        InitialHostEvent partialHost;
        if(!partialHost.handle)throw std::runtime_error("partial-ROG-fixture-event");
        const auto partialPrep=[&](const std::string& shape="valid"){
            const auto deadline=prep(true);fixtureRogPartialEnabled=true;fixtureRogPartialShape=shape;
            g_inputHostProcess=partialHost.handle;g_inputHostHello=true;g_inputHostPrepared=true;
            g_inputHostNeutralized=true;g_inputHostActive=true;g_inputHostHostInstanceId="ROG164155-private-instance";
            fixtureXinputSourceLive=true;g_xinputAdmittedMask=g_xinputPreTargetSlots=1;
            g_xinputSlotUnknownMask=g_xinputVirtualEndpointMask=0;g_xinputSlotReprobeMask=0;
            g_xinputSlotIdentityResolvedEpoch[0]=g_xinputSlotIdentityEpoch.load();
            g_xinputPreTargetBoundaryScanned=true;g_xinputBoundaryScanEpoch=g_xinputTopologyEpoch.load();
            g_inputHostBoundPowerGeneration=currentPowerGeneration();g_inputHostBoundPersona="steamdeck";
            return deadline;
        };
        const auto partialAttempted=[](){std::lock_guard<std::mutex> guard(g_inputHostDeferredPhysicalHideMx);
            return g_inputHostDeferredPhysicalHide.executionContinuationAttempted;};
        const auto partialCheck=[&](bool ok,const std::string& label){check(ok,("ROG164155 "+label).c_str());};
        for(const char* shape:{"valid","root-only"}){
            const auto deadline=partialPrep(shape);const auto request=g_virtualStartProtection.view(startProtectionClock).requestId;
            const auto run=g_inputHostRunId;const auto host=g_inputHostEpoch;const auto beforePrepares=std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"pipe:PREPARE_TARGET");
            const auto beforePartialState=state();const auto beforeForbidden=forbiddenIo;
            const auto queued=dispatch();
            cases.back()["partialRuns"].push_back({{"shape",shape},{"before",beforePartialState},{"after",state()},
                {"hideCalls",hides()},{"snapshots",fixtureRogPartialSnapshotReads},{"forbiddenBefore",beforeForbidden},
                {"forbiddenAfter",forbiddenIo},{"events",fixtureEvents},{"trace",g_startEntrySeam.trace}});
            partialCheck(hides()==2&&fixtureRogPartialSnapshotReads==2&&fixtureRogDeferredCycles.size()==1&&
                inputHostLateHideEpisodeAlreadyCompleted(queued.episode)&&!debtRetained(),
                std::string("real worker completes one partial original hide before its sole refresh: ")+shape);
            partialCheck(g_virtualStartProtection.view(startProtectionClock).requestId==request&&g_inputHostRunId==run&&
                g_inputHostEpoch==host&&g_inputHostProcess==partialHost.handle&&
                std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"pipe:PREPARE_TARGET")==beforePrepares&&
                fixtureRogDeferredCycles.size()==1&&fixtureRogDeferredCycles[0].value("deadline",0ULL)==deadline&&
                detail("input-host-rog-hide-continuation").value("configurationCompleted",false),
                std::string("completion keeps original request target and absolute budget: ")+shape);
        }
        for(const char* shape:{"unknown","invalid","none-hidden","already-complete","cloak-off","app-missing","inverse-unknown","inverse-on"}){
            partialPrep(shape);const auto queued=dispatch();
            partialCheck(hides()==1&&fixtureRogDeferredCycles.empty()&&!partialAttempted()&&debtRetained()&&
                !inputHostLateHideEpisodeAlreadyCompleted(queued.episode),
                std::string("unknown or non-partial configuration cannot authorize another write: ")+shape);
        }
        partialPrep();{
            std::ofstream invalidWriter(fspath::path(fixtureTargetReturnRecordPath()+L".rog-cli"));invalidWriter<<"invalid-writer-state";
        }
        const auto unknownWriter=dispatch();
        partialCheck(hides()==1&&fixtureRogDeferredCycles.empty()&&!partialAttempted()&&debtRetained()&&
            !inputHostLateHideEpisodeAlreadyCompleted(unknownWriter.episode),
            "unreadable original writer journal withholds continuation rather than assuming quiet");
        for(const int port:{-1,1}){
            partialPrep();fixtureRogDeferredPortKind=port;const auto queued=dispatch();
            partialCheck(hides()==1&&fixtureRogDeferredCycles.empty()&&!partialAttempted()&&debtRetained()&&
                !inputHostLateHideEpisodeAlreadyCompleted(queued.episode),
                "external or unknown root never gains continuation "+std::to_string(port));
        }
        for(const char* fault:{"transport","retired-pipe","wrong-run","empty-identity","unowned-root","mixed-roots","cycle-spent"}){
            partialPrep();fixtureRogPartialAfterFirst=[&](){auto& j=g_hidHideVisibilityJournal;const std::string value=fault;
                if(value=="transport")g_inputHostTransportFault=true;
                else if(value=="retired-pipe")g_inputHostPipeRetired=true;
                else if(value=="wrong-run")j.transactionId+="-other";
                else if(value=="empty-identity")j.physical.physicalIdentity.clear();
                else if(value=="unowned-root")j.hiddenByUs={U2W(j.physical.deviceInstanceId)};
                else if(value=="mixed-roots"){auto other=j.physical;other.baseContainerDeviceInstanceId+="_OTHER";j.physicalSources.push_back(other);}
                else{std::lock_guard<std::mutex> lock(g_inputHostDeferredPhysicalHideMx);g_inputHostDeferredPhysicalHide.refreshAttempted=true;}
            };const auto queued=dispatch();
            partialCheck(hides()==1&&fixtureRogDeferredCycles.empty()&&!partialAttempted()&&debtRetained()&&
                !inputHostLateHideEpisodeAlreadyCompleted(queued.episode),
                std::string("unproven original owner or spent cycle cannot continue: ")+fault);
        }
        for(const char* shape:{"retry-failed","after-unknown","after-still-missing","after-cloak-off","after-app-missing"}){
            partialPrep(shape);const auto queued=dispatch();
            partialCheck(hides()==2&&fixtureRogDeferredCycles.empty()&&partialAttempted()&&debtRetained()&&
                !inputHostLateHideEpisodeAlreadyCompleted(queued.episode)&&
                !detail("input-host-rog-hide-continuation").value("configurationCompleted",true),
                std::string("accepted writes need a complete readback before any refresh: ")+shape);
        }
        partialPrep("retry-failed");const auto failedPartial=dispatch();
        inputHostQueueWakePhysicalHide("xinput-connect");inputHostRunQueuedLifecycleWork();
        partialCheck(hides()==3&&fixtureRogDeferredCycles.empty()&&partialAttempted()&&debtRetained()&&
            !inputHostLateHideEpisodeAlreadyCompleted(failedPartial.episode),
            "another source notification cannot replenish the original completion attempt");
        for(const unsigned readAt:{1u,2u})for(const char* boundary:{"OFF","Host","power","deadline","identity","transport"}){
            const auto deadline=partialPrep();bool boundarySeen=false;fixtureRogPartialDuringSnapshot=[&](unsigned read){if(read!=readAt)return;boundarySeen=true;
                const std::string value=boundary;
                if(value=="transport")g_inputHostTransportFault=true;
                else if(value=="OFF")g_realStickTest.virtualTargetEnabled=false;
                else if(value=="Host"){++g_inputHostEpoch;g_inputHostEpochMirror=g_inputHostEpoch;}
                else if(value=="power")g_powerGeneration.fetch_add(1,std::memory_order_acq_rel);
                else if(value=="deadline")startProtectionClock=deadline;
                else g_hidHideVisibilityJournal.physicalSources.front().deviceInstanceId+="_OTHER";
            };const auto queued=dispatch();
            partialCheck(boundarySeen&&hides()==(readAt==1?1:2)&&fixtureRogDeferredCycles.empty()&&debtRetained()&&
                !inputHostLateHideEpisodeAlreadyCompleted(queued.episode),
                "readback cannot complete retired scope "+std::to_string(readAt)+"/"+boundary);
        }
        partialPrep();bool sourceGuard=false,targetGuard=false;
        fixtureRogPartialDuringRetry=[&](){auto changed=g_hidHideVisibilityJournal.physicalSources;
            changed.front().deviceInstanceId+="_OTHER";sourceGuard=!inputHostRogPartialHideSourcesCurrent(changed);
            const auto& physical=g_hidHideVisibilityJournal.physical;
            targetGuard=!inputHostRogPartialHideTargetsCurrent({physical.deviceInstanceId,physical.baseContainerDeviceInstanceId,"HID_EXTRA"});
            fixtureRogPartialShape="retry-failed";
        };dispatch();partialCheck(sourceGuard&&targetGuard&&fixtureRogDeferredCycles.empty(),
            "Refresh scope rejects re-resolved source or additional target under the active original completion");
        cases.back()["partialHideFieldCounterexample"]={{"nativePid",11292},{"start","2026-11-03T16:38:49.745"},
            {"originalHideFailed","2026-11-03T16:38:51.948"},{"childHidden",true},{"rootHidden",false},
            {"originalRefreshOutcome","not-requested"},{"oldFinal","success"},
            {"hideAndSnapshotPortsInert",true},{"actualWorkerAndCompletionHelper",true}};
        g_inputHostProcess=nullptr;g_inputHostHello=false;g_inputHostPrepared=false;g_inputHostPrepareMayHaveReachedHost=false;
        fixtureRogInitialIdentityEnabled=false;
        fixtureRogDeferredJournalSeed=fixtureRogDeferredCycleEnabled=false;
        fixtureLateHideDuringDecision={};inputHostDeferredPhysicalHideClear();
        g_physicalReleaseWindowNeutral=false;g_powerGeneration.store(savedPower,std::memory_order_release);
    };
    for(const auto& row:{ScopeCase{"ROG",YmccFamily::AsusRogAlly,"ROG Ally",true},
        ScopeCase{"ROG-X",YmccFamily::AsusRogAllyX,"ROG Ally X",true},
        ScopeCase{"GPD",YmccFamily::Gpd,"G1617",true},
        ScopeCase{"Unknown",YmccFamily::Unknown,"unknown",true},
        ScopeCase{"unregistered-MSI",YmccFamily::MsiClaw,"unregistered",true},
        ScopeCase{"disarmed-MSI",YmccFamily::MsiClaw,"MS-1T91",false}}){
        prepare();g_machineIdentity.family=row.family;g_machineIdentity.product=row.product;
        g_msiDinputRouteDinputArmed=row.armed;
        cases.push_back({{"name",std::string("compatibility/")+row.name}});
        check(!inputHostLateHideManagedMsiRouteType(),"compatibility route stays outside pending-required scope");
        check(inputHostQueueLifecycleWork("late-hide-after-source-return"),"compatibility empty work enters real queue");
        inputHostRunQueuedLifecycleWork();
        check(decision()=="apply"&&hides()==1&&drained(),"compatibility empty-work decision and hide remain original");
        cases.back()["events"]=fixtureEvents;
        deferredSourceReturn(row);
    }
    for(const char* board:{"MS-1T41","MS-1T42","MS-1T52","MS-1T8K","MS-1T91"}){
        prepare();g_machineIdentity.product=board;
        cases.push_back({{"name",std::string("registered-board/")+board}});
        check(inputHostLateHideManagedMsiRouteType(),"registered armed MSI board is recognized by actual authority");
        check(inputHostQueueLifecycleWork("late-hide-after-source-return"),"registered-board empty work enters real queue");
        inputHostRunQueuedLifecycleWork();
        check(decision()==(enabled?"refused-msi-missing-pending":"apply")&&hides()==(enabled?0:1)&&drained(),"registered-board consumer follows only the compiled pending policy");
        cases.back()["events"]=fixtureEvents;
    }
    prepare();cases.push_back({{"name","failed-hide-retries-through-real-worker"}});
    const auto failedEp=episode();g_startEntrySeam.hidHideApplyResult=false;
    check(enqueue(failedEp)==Outcome::Enqueued,"failed-hide episode is admitted");inputHostRunQueuedLifecycleWork();
    check(hides()==1&&!inputHostLateHideEpisodeAlreadyCompleted(failedEp),"failed actual hide does not complete episode");
    g_startEntrySeam.hidHideApplyResult=true;
    check(enqueue(failedEp)==Outcome::Enqueued,"same episode retry remains admissible after failed hide");inputHostRunQueuedLifecycleWork();
    check(hides()==2&&inputHostLateHideEpisodeAlreadyCompleted(failedEp)&&drained(),"real worker successfully completes legal failed-hide retry");
    cases.back()["events"]=fixtureEvents;
    prepare();cases.push_back({{"name","new-source-generation-is-new-real-episode"}});
    const auto oldEp=episode();check(enqueue(oldEp)==Outcome::Enqueued,"old source episode admitted");inputHostRunQueuedLifecycleWork();
    check(inputHostLateHideEpisodeAlreadyCompleted(oldEp),"old source episode actually completed");
    {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);g_msiMainSource.generation=12;}
    auto newEp=episode();++newEp.sourceGen;
    check(!inputHostLateHideEpisodeAlreadyCompleted(newEp)&&enqueue(newEp)==Outcome::Enqueued,"new source and record generations legally enter actual queue");inputHostRunQueuedLifecycleWork();
    check(hides()==2&&inputHostLateHideEpisodeAlreadyCompleted(newEp)&&drained(),"new generation is hidden and completed by actual worker");
    cases.back()["events"]=fixtureEvents;
    prepare();cases.push_back({{"name","source-refusal-is-reversible-before-completion"}});
    const auto retryEp=episode();check(enqueue(retryEp)==Outcome::Enqueued,"source-refusal episode admitted");
    {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);g_msiMainSource.generation=12;}
    inputHostRunQueuedLifecycleWork();
    check(decision()=="refused-source"&&hides()==0&&!inputHostLateHideEpisodeAlreadyCompleted(retryEp),"real worker refuses stale record without completing episode");
    {std::lock_guard<std::mutex> lock(g_msiMainSourceMx);g_msiMainSource.generation=11;}
    check(enqueue(retryEp)==Outcome::Enqueued,"restored valid source permits same-episode retry");inputHostRunQueuedLifecycleWork();
    check(hides()==1&&inputHostLateHideEpisodeAlreadyCompleted(retryEp)&&drained(),"actual worker completes retry after valid source return");
    cases.back()["events"]=fixtureEvents;
    prepare();cases.push_back({{"name","exit-refuses-enqueue-and-already-queued-hide"}});
    const auto exitEp=episode();g_inputHostExitFence=true;
    check(enqueue(exitEp)==Outcome::Refused,"exit fence refuses real late-hide enqueue");
    g_inputHostExitFence=false;check(enqueue(exitEp)==Outcome::Enqueued,"late-hide can enqueue before exit");
    g_inputHostExitFence=true;inputHostRunQueuedLifecycleWork();
    check(hides()==0&&!inputHostLateHideEpisodeAlreadyCompleted(exitEp)&&drained(),"exit consumer refuses already queued hide and releases claim");
    cases.back()["events"]=fixtureEvents;

    // SOURCE-DIRECTED ONLY. Field empty jobs carried nonzero tokens; this case
    // injects first-token0 ordering and makes no claim it occurred on hardware.
    prepare();cases.push_back({{"name","source-directed-empty-token0-new-token1"},{"fieldAttribution",false}});
    const auto cornerEp=episode();unsigned long long injectedToken=0;bool hookFired=false;
    fixtureLateHideAfterEmptyDecision=[&](){
        hookFired=true;
        check(enqueue(cornerEp)==Outcome::Enqueued,"log boundary submits real new episode after empty token0 decision");
        InputHostLateHidePendingView injected;inputHostLateHidePendingRead(&injected);injectedToken=injected.token;
        check(injected.hasPending&&injectedToken==1,"actual producer commits token1 at empty decision boundary");
    };
    check(inputHostQueueLifecycleWork("late-hide-after-source-return"),"first residual job is queued with initial token0");
    inputHostRunQueuedLifecycleWork();
    InputHostLateHidePendingView afterCorner;inputHostLateHidePendingRead(&afterCorner);
    check(hookFired&&decision()==(enabled?"refused-msi-missing-pending":"apply")&&hides()==(enabled?0:1),"empty token0 worker follows compiled policy while actual producer runs at log port");
    check(afterCorner.hasPending&&afterCorner.token==injectedToken&&inputHostLateHideEpisodeEqual(afterCorner.episode,cornerEp),"empty-token0 worker preserves newly enqueued token1 pending");
    check(g_inputHostLifecycleWorkRequested.load()&&!g_inputHostLifecycleWorkInFlight.load(),"new producer work remains queued after empty worker releases claim");
    cases.back()["afterEmpty"]={{"hasPending",afterCorner.hasPending},{"token",afterCorner.token},{"runId",afterCorner.episode.runId},{"hideCalls",hides()}};
    inputHostRunQueuedLifecycleWork();
    check(hides()==1&&inputHostLateHideEpisodeAlreadyCompleted(cornerEp)&&drained(),"new token1 episode is consumed and completed exactly once");
    cases.back()["afterNewReal"]={{"hideCalls",hides()},{"completed",inputHostLateHideEpisodeAlreadyCompleted(cornerEp)}};
    cases.back()["events"]=fixtureEvents;
    // The original clear/apply behavior outside managed MSI stays unchanged.
    for(const auto family:{YmccFamily::AsusRogAllyX,YmccFamily::Gpd}){
        prepare();g_machineIdentity.family=family;
        cases.push_back({{"name",family==YmccFamily::Gpd?"source-directed-token0-legacy-GPD":"source-directed-token0-legacy-ROG"},{"fieldAttribution",false}});
        const auto commonEp=episode();bool commonHook=false;
        fixtureLateHideAfterEmptyDecision=[&](){commonHook=true;check(enqueue(commonEp)==Outcome::Enqueued,"legacy boundary producer uses real enqueue");};
        check(inputHostQueueLifecycleWork("late-hide-after-source-return"),"legacy token0 work is queued");inputHostRunQueuedLifecycleWork();
        InputHostLateHidePendingView common;inputHostLateHidePendingRead(&common);
        check(commonHook&&!inputHostLateHideManagedMsiRouteType()&&!common.hasPending&&common.token==1&&hides()==1,"common token0 worker retains original clear and apply decisions");
        inputHostRunQueuedLifecycleWork();check(drained()&&hides()==2,"legacy queued followup keeps original consumer behavior");
        cases.back()["events"]=fixtureEvents;
    }
    fixtureLateHideAfterEmptyDecision={};

    check(forbiddenIo==0,"bounded late-hide suite executes no forbidden external I/O");
    fixtureWakeScenario=false;g_inputHostExitFence=false;
}
#endif
