static unsigned g_assertions=0;
static std::unique_ptr<InputHostVirtualIntentMutation> g_heldIntent;
static void check(bool condition,const char* what) { ++g_assertions;if(!condition)throw std::runtime_error(what); }
static unsigned countAction(const char* action) {
    unsigned result=0;for(const auto& row:g_rows)if(row.value("action",std::string{})==action)++result;return result;
}
static bool durablePending() { return json::parse(g_durableBytes).at("modeReturnPending").get<bool>(); }
static void reset() {
    g_heldIntent.reset();g_storeHook={};g_signalHook={};g_rows=json::array();g_lockProbe=json::object();
    g_storeCalls=g_signalCalls=g_retryCalls=0;g_storeSuccess=true;g_stableAtStore=false;
    g_realStickTest.virtualTargetEnabled=false;g_persona="disabled";g_authorized=true;g_seam=false;g_hardware=true;g_foregroundBusy=false;
    g_inputHostProcess=g_inputHostPrepared=g_inputHostReleaseQuarantined=g_inputHostActive=false;
    g_msiNativeForegroundCyclePending=false;g_msiNativeForegroundPowerHold=false;g_msiDinputRouteDinputArmed=false;g_inputHostExitFence=false;
    g_power=11;g_msiFeedbackModeGeneration=12;g_xinputTopologyEpoch=13;g_msiOwnedReturnSourceEvent=14;g_msiMainSourceRevision=15;
    g_hostEpoch=16;g_msiDinputReturnDebtEpisode=17;g_inputHostVirtualIntentRevision=18;g_inputHostVirtualIntentWriters=0;
    g_msiOwnedReturnMatchedEpisode=0;g_msiOwnedReturnMatchedPower=0;g_msiOwnedReturnMatchedTopology=0;
    g_msiOwnedReturnSeenTopology=0;g_msiOwnedReturnSeenPower=0;g_msiOwnedReturnSeenSourceEvent=0;g_msiOwnedReturnObserveAfter=0;
    g_msiDinputReturnOwed=true;g_msiDinputReturnDebtPendingLogTick=0;g_msiCrashLoaded=true;g_msiCrashStoreInvalid=false;
    g_msiCrashRecord={};g_msiCrashRecord.boardKey="MICRO-STAR INTERNATIONAL CO., LTD.|MS-1T91|Claw|handheld";
    g_msiCrashRecord.ownerPid=42;g_msiCrashRecord.ownerCreationTime="1234";g_msiCrashRecord.transactionId="r120-settlement-owned";
    g_msiCrashRecord.modeReturnPending=true;g_msiCrashRecord.modeAttemptOwner="42:1234";
    g_msiCrashRecord.rootInstance="USB\\VID_0DB0&PID_1902\\R120";
    g_msiCrashRecord.locationPaths={"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(3)"};
    check(msiRecovery::validRecord(g_msiCrashRecord),"fixture starts with a real schema-valid journal");
    g_msiOwnedReturnExpected=g_msiCrashRecord;g_durableBytes=msiRecovery::encodeRecord(g_msiCrashRecord);
    g_msiOwnedReturnRecovery={};check(g_msiOwnedReturnRecovery.begin(9000,17,true),"begin real recovery policy");
    g_msiOwnedReturnRetrySafe=true;g_now=10000;g_signalSequence=1;msiOwnedReturnPublish();
}
static void firstWindow() {
    msiOwnedReturnMaintenance(g_now);
    check(g_signalCalls==1 && g_storeCalls==0,"first matching signal cannot settle");
    check(g_msiOwnedReturnRecovery.pending() && g_msiDinputReturnOwed && durablePending(),"first signal retains all debt");
    g_now=11000;g_signalSequence=2;
}
static void retained() {
    check(g_msiDinputReturnOwed && g_msiCrashRecord.modeReturnPending && durablePending(),"persistent and memory duty retained");
    check(g_msiOwnedReturnRecovery.pending() && !g_msiOwnedReturnRecovery.completed(),"observer remains pending");
    check(g_msiOwnedReturnMatchedEpisode==0 && g_msiOwnedReturnMatchedPower==0 && g_msiOwnedReturnMatchedTopology==0,"no matched publication");
    check(countAction("physical-signal-confirmed")==0 && countAction("discharged")==0,"no success receipt");
}
static void settled() {
    check(!g_msiDinputReturnOwed && !g_msiCrashRecord.modeReturnPending && !durablePending(),"actual durable and memory debt cleared");
    check(g_msiOwnedReturnRecovery.completed() && !g_msiOwnedReturnPending && !g_msiOwnedReturnWorking,"actual observer completed");
    check(g_msiOwnedReturnMatchedEpisode==17 && g_msiOwnedReturnMatchedPower==11 && g_msiOwnedReturnMatchedTopology==13,"captured matched tuple published");
    check(countAction("physical-signal-confirmed")==1 && countAction("discharged")==1,"one actual physical success and discharge receipt");
}
static json state() {
    return {{"contextCurrent",fixtureCapturedContextCurrent()},{"owed",g_msiDinputReturnOwed.load()},
        {"recordPending",g_msiCrashRecord.modeReturnPending},{"durablePending",durablePending()},
        {"observerPending",g_msiOwnedReturnRecovery.pending()},{"observerCompleted",g_msiOwnedReturnRecovery.completed()},
        {"mirrorPending",g_msiOwnedReturnPending.load()},{"matchedEpisode",g_msiOwnedReturnMatchedEpisode.load()},
        {"matchedPower",g_msiOwnedReturnMatchedPower.load()},{"matchedTopology",g_msiOwnedReturnMatchedTopology.load()},
        {"currentPower",g_power.load()},{"currentTopology",g_xinputTopologyEpoch.load()},{"currentSourceEvent",g_msiOwnedReturnSourceEvent.load()},
        {"currentMainSourceRevision",g_msiMainSourceRevision.load()},{"currentIntentRevision",g_inputHostVirtualIntentRevision.load()},
        {"activeIntentWriters",g_inputHostVirtualIntentWriters.load()},{"exitFence",g_inputHostExitFence.load()},
        {"storeCalls",g_storeCalls},{"signalCalls",g_signalCalls},{"retryWrites",g_retryCalls},{"heldLocksAtInjection",g_lockProbe}};
}
int main() {
    json cases=json::array();unsigned failures=0,observed=0;
    const auto run=[&](const std::string& name,bool duringStoreObservation,const std::function<void()>& body) {
        json row={{"name",name},{"duringStoreObservation",duringStoreObservation}};const auto before=g_assertions;
        try { reset();body();check(g_retryCalls==0,"no retry/device writes in any settlement case");row["passed"]=true;if(duringStoreObservation)++observed; }
        catch(const std::exception& error) { row["passed"]=false;row["error"]=error.what();++failures; }
        row["assertions"]=g_assertions-before;row["state"]=state();row["receipts"]=g_rows;cases.push_back(std::move(row));
    };
    run("control/stable-success",false,[&] { firstWindow();msiOwnedReturnMaintenance(g_now);settled();check(g_stableAtStore,"qualified context reaches durable boundary"); });
    run("control/stable-write-failure-then-fresh-retry",false,[&] {
        firstWindow();g_storeSuccess=false;msiOwnedReturnMaintenance(g_now);retained();
        check(countAction("settlement-pending")==1 && g_storeCalls==1,"failed actual writer recorded as pending");
        g_storeSuccess=true;g_now=12000;g_signalSequence=3;msiOwnedReturnMaintenance(g_now);settled();
        check(g_storeCalls==2,"next fresh window can retry settlement without mode write");
    });
    run("control/no-physical-proof",false,[&] { check(!msiDinputTryDischargeReturnDebt(false,false,1,"r120-no-proof"),"no boolean proof denied");retained();check(g_storeCalls==0,"no durable write without proof"); });
    run("control/invalid-store",false,[&] { firstWindow();g_msiCrashStoreInvalid=true;msiOwnedReturnMaintenance(g_now);retained();check(g_storeCalls==0,"invalid store prevents settlement"); });
    run("control/unloaded-real-discharge-gate",false,[&] { g_msiCrashLoaded=false;check(!msiDinputTryDischargeReturnDebt(false,true,1,"r120-unloaded"),"unloaded store denies actual discharge");retained();check(g_storeCalls==0,"unloaded gate avoids write"); });
    run("control/invalid-real-discharge-gate",false,[&] { g_msiCrashStoreInvalid=true;check(!msiDinputTryDischargeReturnDebt(false,true,1,"r120-invalid"),"invalid store denies actual discharge");retained(); });
    run("control/unqualified-record-schema",false,[&] { g_msiCrashRecord.ownerPid=0;check(!msiDinputTryDischargeReturnDebt(false,true,1,"r120-schema"),"actual store validates schema");check(g_msiDinputReturnOwed && g_storeCalls==0,"invalid schema retains owed without writer"); });
    run("control/no-authority",false,[&] { g_authorized=false;check(!msiDinputTryDischargeReturnDebt(false,true,1,"r120-authority"),"no authority denies durable discharge");retained(); });
    run("control/start-seam",false,[&] { g_seam=true;check(!msiDinputTryDischargeReturnDebt(false,true,1,"r120-seam"),"start seam denies durable discharge");retained(); });
    const std::vector<std::pair<std::string,std::function<void()>>> events={
        {"exit",[]{g_inputHostExitFence.exchange(true,std::memory_order_acq_rel);}},
        {"active-new-intent",[]{g_heldIntent=std::make_unique<InputHostVirtualIntentMutation>();}},
        {"completed-intent-revision",[]{const InputHostVirtualIntentMutation mutation;}},
        {"source-event",[]{g_msiOwnedReturnSourceEvent.fetch_add(1,std::memory_order_release);}},
        {"main-source-revision",[]{g_msiMainSourceRevision.fetch_add(1,std::memory_order_acq_rel);}},
        {"topology",[]{g_xinputTopologyEpoch.fetch_add(1,std::memory_order_acq_rel);}},
        {"power-generation",[]{g_power.fetch_add(1);}},
        {"mode-generation",[]{g_msiFeedbackModeGeneration.fetch_add(1);}},
        {"host-epoch",[]{g_hostEpoch.fetch_add(1);}}
    };
    for(const auto& event:events) {
        run("observation/durable-success/"+event.first,true,[&] {
            firstWindow();g_storeHook=event.second;msiOwnedReturnMaintenance(g_now);
            check(g_stableAtStore,"change happens after all pre-IO gates");check(!fixtureCapturedContextCurrent(),"event changes captured token values; no contract conclusion");
            for(const auto& held:g_lockProbe.items())check(held.value().get<bool>(),"injection runs while all production settlement locks held");
            settled();check(g_storeCalls==1,"single durable commit completes the prior proven duty");
            const auto signals=g_signalCalls;g_storeHook={};g_now=13000;g_signalSequence=3;msiOwnedReturnMaintenance(g_now);
            check(g_signalCalls==signals && !g_msiOwnedReturnPending,"later maintenance cannot resume cleared duty");
        });
        run("control/durable-failure/"+event.first,false,[&] {
            firstWindow();g_storeSuccess=false;g_storeHook=event.second;msiOwnedReturnMaintenance(g_now);
            check(g_stableAtStore && !fixtureCapturedContextCurrent(),"same during-IO change with failed writer");retained();
            check(g_storeCalls==1 && countAction("settlement-pending")==1,"writer failure keeps settlement pending");
        });
    }
    for(const auto& event:events) {
        if(event.first=="active-new-intent" || event.first=="completed-intent-revision" || event.first=="main-source-revision" || event.first=="host-epoch")continue;
        run("control/before-settlement/"+event.first,false,[&] {
            firstWindow();g_signalHook=event.second;msiOwnedReturnMaintenance(g_now);retained();
            check(g_storeCalls==0,"existing post-query gate rejects same change before durable write");
        });
    }
    // A new durable owner cannot replace the record under g_msiCrashMx. Do not
    // synthesize a new episode/journal mutation under locks to claim its loss.
    // Already-settled record behavior is a direct low-level gate contract only.
    run("gate/already-durable-settled",false,[&] {
        g_msiCrashRecord.modeReturnPending=false;g_durableBytes=msiRecovery::encodeRecord(g_msiCrashRecord);
        check(msiDinputTryDischargeReturnDebt(false,true,1,"r120-already-settled"),"already-settled valid store is accepted");
        check(!g_msiDinputReturnOwed && g_storeCalls==0,"memory debt follows already-settled durable state without rewrite");
    });
    json report={{"harnessPassed",failures==0},{"verdict",failures?"HARNESS_OR_SOURCE_CHANGED":"OBSERVED_PRIOR_DEBT_SETTLEMENT_DURING_STORE_EVENTS"},
        {"settlementSafetyPassed",nullptr},{"duringStoreObservations",observed},{"caseCount",cases.size()},{"assertions",g_assertions},
        {"failures",failures},{"cases",cases}};
    std::cout<<report.dump(2)<<'\n';return failures?1:0;
}

