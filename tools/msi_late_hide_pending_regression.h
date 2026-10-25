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
