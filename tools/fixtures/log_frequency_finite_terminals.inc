struct FiniteOnexValues {unsigned long long writeInFlightToken=17,writeInFlightGen=19,readInFlightGen=23;};
static void offerOriginalFinite(unsigned index) {
    const unsigned long long token=17,gen=19;const bool success=false;const DWORD bytes=3;
    const char* reason="fixture-original-terminal";FiniteOnexValues g_onexBack;
    const json warnings=json::array({{{"consumer","gesture"},{"kind","standard-exception"}}});
    const json refreshWarnings=warnings;const bool result=true;
@@PRODUCERS@@
}
static json finiteExpected(unsigned index) {
    const json warnings=json::array({{{"consumer","gesture"},{"kind","standard-exception"}}});
    if(index==0)return {{"token",17},{"gen",19},{"ok",false},{"bytes",3}};
    if(index==1)return {{"phase","drain"},{"token",17},{"gen",19}};
    if(index==2||index==4)return {{"reason","fixture-original-terminal"},{"gen",23}};
    if(index==3)return {{"gen",19},{"bytes",3}};
    return {{"consumers",warnings},{"committed",true}};
}
static const char* finiteEvents[]={"onex-back-write-late","onex-back-write-stuck","onex-back-read-stuck","onex-back-read-late","onex-back-read-fault","settings-reset-refresh-warning","settings-write-refresh-warning"};
static bool hasPayload(const json& row,const json& expected) {
    for(auto it=expected.begin();it!=expected.end();++it)if(!row.contains(it.key())||row[it.key()]!=it.value())return false;return true;
}
static unsigned detailedRows(){std::ifstream input(fspath::path(g_testRoot)/L"native-detail.log");unsigned result=0;std::string line;while(std::getline(input,line))if(!line.empty())++result;return result;}
static void originalStartupOutcome(bool stopPhase,bool succeeded,DWORD candidatePid) {
    const bool stopped=stopPhase?succeeded:true,recovered=succeeded;
    if(stopPhase){@@STARTUP_STOPPED@@}
    else {@@STARTUP_RECOVERED@@}
}
static int startupOutcomes(const fspath::path& root){try{
    if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;
    (void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    const char* events[]={"single-instance-windowless-terminated","single-instance-windowless-terminate-failed","single-instance-windowless-takeover-succeeded","single-instance-windowless-takeover-failed"};
    bool all=true;for(const auto* event:events)all=all&&nativeLifecycleEventIsBase(event);
    check(all,"all-four-dynamic-startup-final-outcomes-registered");
    for(unsigned i=0;i<4;++i)originalStartupOutcome(i<2,(i&1)==0,1097+i);
    auto rows=readRecoveryRows(root);bool exact=rows.size()==4;
    if(exact)for(unsigned i=0;i<4;++i)exact=exact&&rows[i]["event"]==events[i]&&rows[i]["existingPid"]==1097+i&&(i<2||rows[i]["stopped"]==true);
    check(exact&&!g_nativeDetailLog.is_open(),"actual-two-conditional-initializers-preserve-all-four-default-off-payloads");
#ifndef YMCC_STARTUP_OUTCOMES_OLD_ONLY
    for(unsigned i=0;i<4;++i)originalStartupOutcome(i<2,(i&1)==0,1097+i);
    check(readRecoveryRows(root).size()==8,"identical-separate-conflict-steps-keep-own-terminal-receipts");
    g_domainLogEnabled[1]=true;for(unsigned i=0;i<4;++i)originalStartupOutcome(i<2,(i&1)==0,2097+i);g_domainLogEnabled[1]=false;
    check(readRecoveryRows(root).size()==12&&!g_nativeDetailLog.is_open(),"detail-on-startup-finals-still-use-resident-lane");
    for(const auto* event:{"single-instance-windowless-candidates","single-instance-windowless-takeover-declined","single-instance-windowless-ambiguous"})appendNativeLifecycleLog(event,{{"existingPid",313},{"count",2}});
    check(readRecoveryRows(root).size()==15,"existing-candidate-decline-and-ambiguous-receipts-stay-resident");
    const auto writes=g_nativeLifecycleFrameWrites;
    for(const auto* event:events){std::string typo=std::string(event)+"-future";appendNativeLifecycleLog(typo.c_str(),{{"fixture",true}});}
    check(g_nativeLifecycleFrameWrites==writes,"unknown-outcome-suffixes-stay-default-suppressed");
    const json sample={{"snapshotReason","periodic"}};(void)nativeLifecycleEventIsBase("input-host-frame-evidence");
    ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog("input-host-frame-evidence",sample);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(arm==0&&g_nativeLifecycleFrameWrites==writes,"50000-periodic-frame-samples-remain-silent-and-allocation-free");
    ++g_powerGeneration;originalStartupOutcome(true,false,4197);rows=readRecoveryRows(root);
    check(rows.size()==16&&rows.back()["event"]=="single-instance-windowless-terminate-failed"&&rows.back()["existingPid"]==4197,"new-power-and-candidate-value-retains-original-failed-result");
    const auto oldFailure=count(Failure::eventWrite);const auto priorRows=rows.size();
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);originalStartupOutcome(false,false,5197);}
    originalStartupOutcome(false,false,5197);rows=readRecoveryRows(root);
    check(count(Failure::eventWrite)==oldFailure+1&&rows.size()==priorRows+1&&rows.back()["event"]=="single-instance-windowless-takeover-failed","original-file-failure-account-and-next-startup-result-offer-preserved");
#endif
    closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-log-handles-returned-without-process-or-window-action");
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"registeredEvents",events},{"handlesBefore",before},{"handlesAfter",after},{"actualConditionalInitializerCalls",2},{"processTerminationOrMutexTakeoverActions",0},{"hardwareActions",0}}.dump(2)<<'\n';return failed?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}

static std::atomic<bool> g_rogRemapLogQuiet{false};
@@ROG_LOG_WRAPPER@@
static void originalRogRecoveryOutcome(unsigned index) {
    const char* tag="fixture-existing-recovery";const unsigned long long deviceGeneration=19,powerGeneration=23;
    const struct {unsigned long long device,power;} pending{11,13};
@@ROG_RECOVERY_CALLS@@
}
static json expectedRogRecovery(unsigned index) {
    if(index==0)return {{"reason","fixture-existing-recovery"},{"devGen",19},{"producerPowerGeneration",23}};
    if(index==1)return {{"reason","fixture-existing-recovery"}};
    if(index==2)return {{"reason","retired-owner"},{"pendingGen",11},{"producerPowerGeneration",13},{"devGen",19}};
    return {{"devGen",19},{"producerPowerGeneration",13}};
}
static int rogRecoveryOutcomes(const fspath::path& root){try{
    if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;
    (void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    const char* events[]={"rog.recover-bind-deferred","rog.recover-bind-executed","rog.recover-bind-dropped","rog.recover-bind-consumed"};
    bool all=true;for(const auto* event:events)all=all&&nativeLifecycleEventIsBase(event);
    check(all,"four-existing-ROG-recovery-outcomes-registered");
    for(unsigned i=0;i<4;++i)originalRogRecoveryOutcome(i);auto rows=readRecoveryRows(root);bool exact=rows.size()==4;
    if(exact)for(unsigned i=0;i<4;++i)exact=exact&&rows[i]["event"]==events[i]&&hasPayload(rows[i],expectedRogRecovery(i));
    check(exact&&!g_nativeDetailLog.is_open(),"original-wrapper-and-four-original-initializers-retain-off-payloads");
#ifndef YMCC_ROG_RECOVERY_LOG_OLD_ONLY
    check(rows[0]["powerGeneration"]==1&&rows[0]["observedPowerGeneration"]==1&&rows[0]["producerPowerGeneration"]==23&&rows[2]["producerPowerGeneration"]==13,"source-power-and-current-writer-power-remain-distinct");
    for(unsigned i=0;i<4;++i)originalRogRecoveryOutcome(i);
    check(readRecoveryRows(root).size()==8,"separate-identical-recovery-steps-retain-own-receipts");
    g_domainLogEnabled[1]=true;for(unsigned i=0;i<4;++i)originalRogRecoveryOutcome(i);g_domainLogEnabled[1]=false;
    check(readRecoveryRows(root).size()==12&&!g_nativeDetailLog.is_open(),"detail-on-recovery-outcomes-stay-resident-without-extra-copy");
    g_rogRemapLogQuiet.store(true);for(unsigned i=0;i<4;++i)originalRogRecoveryOutcome(i);g_rogRemapLogQuiet.store(false);
    check(readRecoveryRows(root).size()==12,"original-selftest-quiet-wrapper-still-suppresses-synthetic-results");
    const auto writes=g_nativeLifecycleFrameWrites;for(const auto* event:events){std::string typo=std::string(event)+"-future";rogRemapLog(typo.c_str(),{{"fixture",true}});}
    check(g_nativeLifecycleFrameWrites==writes,"unknown-wrapper-outcome-suffixes-stay-suppressed");
    const json sample={{"snapshotReason","periodic"}};ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog("input-host-frame-evidence",sample);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(arm==0&&g_nativeLifecycleFrameWrites==writes,"50000-original-periodic-frames-remain-allocation-free-and-silent");
    ++g_powerGeneration;originalRogRecoveryOutcome(2);rows=readRecoveryRows(root);
    check(rows.size()==13&&rows.back()["powerGeneration"]==2&&rows.back()["producerPowerGeneration"]==13&&rows.back()["event"]==events[2],"new-observed-power-does-not-rewrite-retired-request-power");
    const auto oldFailure=count(Failure::eventWrite);const auto priorRows=rows.size();
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);originalRogRecoveryOutcome(0);}
    originalRogRecoveryOutcome(0);rows=readRecoveryRows(root);
    check(count(Failure::eventWrite)==oldFailure+1&&rows.size()==priorRows+1&&rows.back()["event"]==events[0],"real-file-failure-account-and-next-original-recovery-offer-preserved");
#endif
    closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"owned-log-handles-returned-without-real-ROG-or-device-recovery");
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"registeredEvents",events},{"handlesBefore",before},{"handlesAfter",after},{"originalInitializerCalls",4},{"actualRogLogWrapper",true},{"actualDeviceRecoveryActions",0},{"hardwareActions",0}}.dump(2)<<'\n';return failed?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}

#include "msi_target_intent_evidence.h"
using IntentObserver=ymcc::msi::intentEvidence::BoundedObserver;
static bool fixtureIntentMsi=false;
static struct {int family=1;std::string product="fixture-board";}g_machineIdentity;
static bool msiClawKnownBoardAuthority() noexcept{return fixtureIntentMsi;}
static const char* ymccFamilyName(int){return fixtureIntentMsi?"msi":"gpd";}
static unsigned long long inputHostEpochToken(){return 7;}
static unsigned long long currentPowerGeneration(){return g_powerGeneration.load();}
static IntentObserver g_msiTargetIntentObservation,g_msiTargetIntentWrites;
static std::atomic<unsigned long long> g_msiTargetIntentEvidenceFailures{0};
@@ACTUAL_INTENT_GATE@@
@@ACTUAL_INTENT_PRODUCERS@@
static unsigned long long intentResidentRows(const fspath::path& root){
    unsigned long long result=0;for(const auto& row:readRecoveryRows(root))
        if(row.value("event",std::string{})=="input-target-intent-evidence"||row.value("event",std::string{})=="msi-target-intent-evidence")++result;
    return result;
}
static void intentRecoverWriter(){appendNativeLifecycleLog("settings-write-refresh-warning",{{"fixtureExistingOffer",true}});}
static int intentFileDelivery(const fspath::path& root){try{
    if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();
    (void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    const json saved={{"revision",20},{"outputTarget",{{"persona","steamdeck"},{"buttonMappingEnabled",true},{"gyroEnabled",true}}}};
    auto requested=saved;requested["outputTarget"]["persona"]="disabled";
    const json args={{"content",json{{"input",requested}}.dump()},{"baseline",{{"input",saved}}}};
    const json committed={{"input",requested}};
    check(nativeLifecycleEventIsBase("input-target-intent-evidence")&&nativeLifecycleEventIsBase("msi-target-intent-evidence"),"both-original-family-aliases-use-resident-lane-behind-detail-gate");
    g_domainLogEnabled[1]=false;ymcc::testing::allocationFault::failAfter(0);
    msiTargetIntentObserveInput(saved,saved,false,false);msiTargetIntentDocumentWrite(args,committed,true,false,-1,20);
    const auto closedArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    g_domainLogEnabled[1]=true;msiTargetIntentObserveInput(saved,saved,false,false);
    const auto writeBefore=count(Failure::eventWrite);const auto epochBefore=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);msiTargetIntentObserveInput(saved,saved,true,false);}
    const bool refused=count(Failure::eventWrite)==writeBefore+1&&intentResidentRows(root)==1;
    intentRecoverWriter();const auto recoveredEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    check(closedArm==0&&g_msiTargetIntentWrites.statistics()["reservedRows"]==0&&refused&&recoveredEpoch==epochBefore+1,"actual-source-detail-gate-and-original-file-write-refusal-observed");
    const auto rowsBeforeReoffer=intentResidentRows(root);msiTargetIntentObserveInput(saved,saved,true,false);
    check(intentResidentRows(root)==rowsBeforeReoffer+1,"same-observation-reoffered-after-original-resident-writer-recovers");
#ifndef YMCC_INTENT_DELIVERY_OLD_ONLY
    const auto steadyRows=intentResidentRows(root);ymcc::testing::allocationFault::failAfter(0);
    for(unsigned i=0;i<50000;++i)msiTargetIntentObserveInput(saved,saved,true,false);
    const auto repeatArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(repeatArm==0&&intentResidentRows(root)==steadyRows&&g_msiTargetIntentEvidenceFailures.load()==0,"50000-recovered-identical-source-calls-stay-allocation-free-without-more-rows");

    const auto flushBefore=count(Failure::eventFlush);
    {ControlledBuffer buffer(ControlledBuffer::Mode::flushFail);BufferLease lease(g_nativeLifecycleLog,buffer);msiTargetIntentObserveInput(saved,saved,false,false);}
    const auto beforeFlushRecover=intentResidentRows(root);intentRecoverWriter();msiTargetIntentObserveInput(saved,saved,false,false);
    check(count(Failure::eventFlush)==flushBefore+1&&intentResidentRows(root)==beforeFlushRecover+1,"original-flush-refusal-reoffers-same-source-only-after-observed-recovery");

    const auto prepareBefore=count(Failure::preparationException);timestampAllocationFault=true;
    msiTargetIntentObserveInput(saved,saved,true,true);timestampAllocationFault=false;ymcc::testing::allocationFault::failAfter(-1);
    const auto beforePrepareRecover=intentResidentRows(root);intentRecoverWriter();msiTargetIntentObserveInput(saved,saved,true,true);
    check(count(Failure::preparationException)==prepareBefore+1&&intentResidentRows(root)==beforePrepareRecover+1,"resident-record-preparation-failure-reoffers-with-existing-recovery-epoch");

    const auto pathBefore=count(Failure::preparationException);pathAllocationFailureAfter=0;
    msiTargetIntentObserveInput(saved,saved,false,true);pathAllocationFailureAfter=-1;ymcc::testing::allocationFault::failAfter(-1);
    const auto beforePathRecover=intentResidentRows(root);intentRecoverWriter();msiTargetIntentObserveInput(saved,saved,false,true);
    check(count(Failure::preparationException)==pathBefore+1&&intentResidentRows(root)==beforePathRecover+1,"original-resident-path-allocation-refusal-reoffers-after-recovery");

    fixtureIntentMsi=true;const auto msiWriteBefore=count(Failure::eventWrite);
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);msiTargetIntentObserveInput(saved,saved,true,false);}
    const auto beforeMsiRecover=intentResidentRows(root);intentRecoverWriter();msiTargetIntentObserveInput(saved,saved,true,false);
    const auto msiRows=readRecoveryRows(root);
    check(count(Failure::eventWrite)==msiWriteBefore+1&&intentResidentRows(root)==beforeMsiRecover+1&&msiRows.back()["event"]=="msi-target-intent-evidence"&&!g_nativeDetailLog.is_open(),"actual-msi-alias-recovery-uses-same-resident-writer-and-preserves-family");

    const auto gateRows=intentResidentRows(root);
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);intentRecoverWriter();}
    intentRecoverWriter();g_domainLogEnabled[1]=false;msiTargetIntentObserveInput(saved,saved,true,false);
    const bool gateHeld=intentResidentRows(root)==gateRows;g_domainLogEnabled[1]=true;msiTargetIntentObserveInput(saved,saved,true,false);
    check(gateHeld&&intentResidentRows(root)==gateRows+1,"closed-source-gate-does-not-consume-recovery-observation");

    const auto documentRows=intentResidentRows(root);for(unsigned i=0;i<3;++i)msiTargetIntentDocumentWrite(args,committed,i!=1,i==1,-1,20);
    auto all=readRecoveryRows(root);unsigned documents=0;bool documentFields=true;
    for(const auto& row:all)if(row.value("stage",std::string{})=="ipc-document-write-result"){
        documentFields=documentFields&&row["receiptSequence"]==++documents&&row["humanGestureProven"]==false&&row["writesControls"]==false;
    }
    check(intentResidentRows(root)==documentRows+3&&documents==3&&documentFields&&g_msiTargetIntentWrites.statistics()["reservedRows"]==3,"each-original-document-operation-keeps-own-real-file-receipt-and-no-replay");

    const auto reservations=g_msiTargetIntentObservation.statistics()["reservedRows"].get<unsigned long long>();
    const auto capRows=intentResidentRows(root);for(unsigned i=0;i<200;++i)msiTargetIntentObserveInput(saved,saved,(i&1)!=0,false);
    auto cap=g_msiTargetIntentObservation.statistics();
    check(cap["reservedRows"]==128&&cap["capReached"]==true&&intentResidentRows(root)==capRows+128-reservations&&cap["droppedForCap"].get<unsigned long long>()>0,"failed-and-reoffered-reservations-share-original128-cap-without-refund");
    const auto capEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);intentRecoverWriter();}
    intentRecoverWriter();const auto capDrops=cap["droppedForCap"].get<unsigned long long>();const auto cappedRows=intentResidentRows(root);
    msiTargetIntentObserveInput(saved,saved,true,false);msiTargetIntentObserveInput(saved,saved,true,false);cap=g_msiTargetIntentObservation.statistics();
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==capEpoch+1&&cap["reservedRows"]==128&&cap["droppedForCap"]==capDrops+1&&intentResidentRows(root)==cappedRows,"recovery-at-cap-records-one-original-drop-without-restoring-budget");
    check(g_msiTargetIntentEvidenceFailures.load()==0&&g_msiTargetIntentWrites.statistics()["reservedRows"]==3,"logging-failure-recovery-never-replays-settings-write-or-creates-source-exception");
#endif
    closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"actual-intent-source-and-file-handles-return-without-device-or-settings-actions");
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failed},{"handlesBefore",before},{"handlesAfter",after},{"actualMainIntentProducers",2},{"actualFileWriter",true},{"actualHardwareActions",0},{"actualSettingsWrites",0},{"observerBytes",sizeof(IntentObserver)},{"observation",g_msiTargetIntentObservation.statistics()},{"documentWrites",g_msiTargetIntentWrites.statistics()}}.dump(2)<<'\n';return failed?2:0;
}catch(const std::exception& error){timestampAllocationFault=false;pathAllocationFailureAfter=-1;ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
int wmain(int argc,wchar_t** argv){try{
    if(argc==3 && std::wstring(argv[2])==L"--intent-delivery")return intentFileDelivery(fspath::path(argv[1]));
    if(argc==3 && std::wstring(argv[2])==L"--rog-recovery-outcomes")return rogRecoveryOutcomes(fspath::path(argv[1]));
    if(argc==3 && std::wstring(argv[2])==L"--startup-outcomes")return startupOutcomes(fspath::path(argv[1]));
    if(argc!=2)return 3;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);g_testRoot=root.wstring();g_domainLogEnabled[1]=false;
    (void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    bool all=true;for(const auto* event:finiteEvents)all=all&&nativeLifecycleEventIsBase(event);
    check(all,"seven-existing-finite-terminal-names-registered");
    for(unsigned i=0;i<7;++i)offerOriginalFinite(i);auto rows=readRecoveryRows(root);bool exact=rows.size()==7;
    if(exact)for(unsigned i=0;i<7;++i)exact=exact&&rows[i]["event"]==finiteEvents[i]&&hasPayload(rows[i],finiteExpected(i));
    check(exact&&!g_nativeDetailLog.is_open(),"actual-seven-original-initializers-survive-detail-off-with-fields");
#ifndef YMCC_FINITE_TERMINAL_OLD_ONLY
    for(unsigned i=0;i<7;++i)offerOriginalFinite(i);rows=readRecoveryRows(root);bool repeated=rows.size()==14;
    if(repeated)for(unsigned i=0;i<14;++i)repeated=repeated&&rows[i]["event"]==finiteEvents[i%7]&&hasPayload(rows[i],finiteExpected(i%7));
    check(repeated,"identical-new-operations-retain-every-terminal-receipt");
    g_domainLogEnabled[1]=true;for(unsigned i=0;i<7;++i)offerOriginalFinite(i);g_domainLogEnabled[1]=false;
    check(readRecoveryRows(root).size()==21&&!g_nativeDetailLog.is_open(),"detail-on-keeps-terminal-events-only-in-resident-lane");
    const auto writes=g_nativeLifecycleFrameWrites;
    for(const auto* event:finiteEvents){std::string typo=std::string(event)+"-future";appendNativeLifecycleLog(typo.c_str(),{{"fixture",true}});}
    check(g_nativeLifecycleFrameWrites==writes,"unknown-suffixes-do-not-gain-resident-permission");
    const json high={{"reason","buttons-change"}};(void)nativeLifecycleEventIsBase("system-xinput-readback");
    ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog("system-xinput-readback",high);const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(arm==0&&g_nativeLifecycleFrameWrites==writes,"50000-existing-high-frequency-samples-still-silent-allocation-free");
    g_domainLogEnabled[1]=true;appendNativeLifecycleLog("system-xinput-readback",high);g_nativeDetailLog.flush();g_domainLogEnabled[1]=false;
    check(detailedRows()==1&&readRecoveryRows(root).size()==21,"high-frequency-sample-still-routes-to-detail-when-enabled");
    appendNativeLifecycleLog("onex-back-read-fault",{{"reason","different-terminal"},{"gen",24},{"futureField",false}});
    appendNativeLifecycleLog("settings-write-refresh-warning",{{"consumers",json::array({"input-refresh"})},{"committed",false}});rows=readRecoveryRows(root);
    check(rows.size()==23&&rows[21]["futureField"]==false&&rows[22]["committed"]==false,"changed-errors-unknown-fields-and-uncommitted-warning-preserved");
    ++g_powerGeneration;for(unsigned i=0;i<7;++i)offerOriginalFinite(i);
    check(readRecoveryRows(root).size()==30,"new-power-generation-keeps-all-operation-receipts");
    const auto failures=count(Failure::eventWrite);const auto prior=readRecoveryRows(root).size();
    {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);offerOriginalFinite(4);}
    offerOriginalFinite(4);rows=readRecoveryRows(root);
    check(count(Failure::eventWrite)==failures+1&&rows.size()==prior+1&&rows.back()["event"]=="onex-back-read-fault","actual-file-write-failure-retains-failure-account-and-next-original-offer");
#endif
    closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all-owned-log-handles-returned-before-exit");
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"registeredEvents",finiteEvents},{"handlesBefore",before},{"handlesAfter",after},{"hardwareActions",0},{"actualInitializerStatements",7}}.dump(2)<<'\n';return failed?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
