struct FocusPlan {const char* virtualUiPersona="";unsigned long long virtualUiHostEpoch=0;};
static bool fixtureAuthority=true;
static unsigned long long fixtureAuthorityCalls=0,fixtureMsiBusinessCalls=0;
static bool fixtureMsiUiOwned=false;
static bool msiClawKnownBoardAuthority(){++fixtureAuthorityCalls;return fixtureAuthority;}
static void msiForegroundIsolationSetUiOwned(bool value){++fixtureMsiBusinessCalls;fixtureMsiUiOwned=value;}
@@PRODUCERS@@
static void outputOffer(const FocusPlan& plan,bool neutral,bool submit,const json& frame,const std::string& source){
    dsuUiOutputNote(neutral,submit);
    virtualUiOutputNote(plan,neutral,submit,frame,true,source);
    steamDeckUiOutputNote(neutral,submit,frame);
    msiUiOutputNote(neutral,submit,frame,true,source);
    ps5UiOutputNote(neutral,submit,frame,true,source,2,4,19);
}
static bool hasPhase(const json& rows,const char* event,const char* phase){
    for(const auto& row:rows)if(row.at("event")==event&&row.at("phase")==phase)return true;
    return false;
}
static const char* outputEvents[]={"dsu-ui-output","virtual-ui-output","steamdeck-ui-output","msi-ui-output","ps5-ui-output"};
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;const fspath::path root(argv[1]);fspath::create_directories(root);g_testRoot=root.wstring();
    (void)ymcc::sleepfactlog::writerIdentity();
    DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;
    const json pending={{"hostSubmission","frame-pending"},{"focusIsolationSemanticNeutral",false}};
    const json accepted={{"hostSubmission","frame-accepted"},{"focusIsolationSemanticNeutral",true}};
    const std::string source="fixture-source";FocusPlan plan;
    outputOffer(plan,false,false,pending,source);flushNativeLifecycleLogs(false);
    auto rows=readRecoveryRows(root);
    check(rows.size()==1&&rows[0]["event"]=="dsu-ui-output"&&fixtureMsiBusinessCalls==1&&!fixtureMsiUiOwned,"original-initial-passthrough-quiet-and-msi-business-call-preserved");
    closeStreams();g_testRoot=(root/L"unavailable"/L"parent").wstring();
    const auto failedBefore=count(Failure::eventOpen);outputOffer(plan,true,true,pending,source);
    check(count(Failure::eventOpen)==failedBefore+5&&ymcc::logging::nativeLogFailureEvidence().eventFailurePending.load(),"five-original-note-functions-reach-actual-unavailable-file-exits");
    const auto msiBefore=fixtureMsiBusinessCalls;ymcc::testing::allocationFault::failAfter(0);
    for(unsigned i=0;i<100000;++i)outputOffer(plan,true,true,pending,source);
    const auto armed=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(armed==0&&count(Failure::eventOpen)==failedBefore+5&&fixtureMsiBusinessCalls==msiBefore+100000&&fixtureMsiUiOwned,"100000-unchanged-offers-during-outage-no-allocation-or-retry-and-msi-business-kept");
    g_testRoot=root.wstring();const auto oldEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    appendNativeLifecycleLog("session-clean-exit",{{"fixtureRecovery",1}});
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==oldEpoch+1&&!ymcc::logging::nativeLogFailureEvidence().eventFailurePending.load(),"recovery-epoch-requires-original-successful-resident-append");
    const auto preceding=readRecoveryRows(root).size();outputOffer(plan,true,true,pending,source);flushNativeLifecycleLogs(false);rows=readRecoveryRows(root);
    bool restored=rows.size()==preceding+5;
    for(const char* event:outputEvents)restored=restored&&hasPhase(rows,event,!std::strcmp(event,"dsu-ui-output")?"neutral":"current-output-after-log-recovery");
#ifndef YMCC_UI_OUTPUT_OLD_ONLY
    for(size_t i=preceding;i<rows.size();++i)restored=restored&&rows[i]["diagnosticRecoveryReoffer"]==true&&rows[i]["uiOwnsInput"]==true;
#endif
    check(restored,"same-current-output-reoffered-once-after-confirmed-resident-file-recovery");
#ifndef YMCC_UI_OUTPUT_OLD_ONLY
    const auto writesBefore=g_nativeLifecycleFrameWrites;g_domainLogEnabled[1]=true;ymcc::testing::allocationFault::failAfter(0);
    for(unsigned i=0;i<100000;++i)outputOffer(plan,true,true,pending,source);
    const auto healthyArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);g_domainLogEnabled[1]=false;
    check(healthyArm==0&&g_nativeLifecycleFrameWrites==writesBefore&&!g_nativeDetailLog.is_open(),"100000-healthy-unchanged-source-offers-stay-quiet-even-with-detail-on");
    const auto beforeAck=readRecoveryRows(root).size();outputOffer(plan,true,true,accepted,source);flushNativeLifecycleLogs(false);rows=readRecoveryRows(root);
    bool acks=rows.size()==beforeAck+4;
    for(const char* event:outputEvents)if(std::strcmp(event,"dsu-ui-output"))acks=acks&&hasPhase(rows,event,"neutral-accepted");
    check(acks,"four-original-awaited-neutral-acceptance-receipts-remain-distinct");
    outputOffer(plan,true,true,accepted,source);check(readRecoveryRows(root).size()==rows.size(),"accepted-neutral-repeat-stays-quiet");
    const auto beforeReturn=rows.size();outputOffer(plan,false,false,accepted,source);flushNativeLifecycleLogs(false);rows=readRecoveryRows(root);
    bool returned=rows.size()==beforeReturn+5;
    for(size_t i=beforeReturn;i<rows.size();++i)returned=returned&&rows[i]["diagnosticRecoveryReoffer"]==false&&rows[i]["uiOwnsInput"]==false;
    check(returned&&!fixtureMsiUiOwned,"original-passthrough-transition-and-msi-business-state-preserved");
    outputOffer(plan,true,true,pending,source);closeStreams();g_testRoot=(root/L"unavailable"/L"parent").wstring();
    const auto ackFailures=count(Failure::eventOpen);outputOffer(plan,true,true,accepted,source);
    check(count(Failure::eventOpen)==ackFailures+4,"lost-acceptance-receipts-reproduced-at-four-real-sink-exits");
    ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<10000;++i)outputOffer(plan,true,true,accepted,source);
    const auto ackArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(ackArm==0&&count(Failure::eventOpen)==ackFailures+4,"lost-acceptance-does-not-cause-failed-write-storm");
    g_testRoot=root.wstring();appendNativeLifecycleLog("session-clean-exit",{{"fixtureRecovery",2}});const auto beforeRecoveredAck=readRecoveryRows(root).size();
    outputOffer(plan,true,true,accepted,source);rows=readRecoveryRows(root);
    bool currentAck=rows.size()==beforeRecoveredAck+5;
    for(size_t i=beforeRecoveredAck;i<rows.size();++i){const auto& row=rows[i];currentAck=currentAck&&row["diagnosticRecoveryReoffer"]==true;if(row["event"]!="dsu-ui-output")currentAck=currentAck&&row["phase"]=="current-output-after-log-recovery"&&row["neutralAccepted"]==true;}
    check(currentAck,"recovered-current-accepted-state-is-not-relabelled-as-a-new-acceptance-operation");
    const auto beforeIdentity=rows.size();plan.virtualUiPersona="fixture-other-persona";plan.virtualUiHostEpoch=7;virtualUiOutputNote(plan,true,true,accepted,true,source);rows=readRecoveryRows(root);
    check(rows.size()==beforeIdentity+1&&rows.back()["phase"]=="neutral-requested"&&rows.back()["hostEpoch"]==7&&rows.back()["persona"]=="fixture-other-persona","original-persona-and-host-epoch-transitions-retained");
    closeStreams();g_testRoot=(root/L"unavailable"/L"parent").wstring();appendNativeLifecycleLog("session-clean-exit",{{"fixtureFailure",3}});g_testRoot=root.wstring();appendNativeLifecycleLog("session-clean-exit",{{"fixtureRecovery",3}});
    const auto beforeAuthority=readRecoveryRows(root).size(),businessBeforeAuthority=fixtureMsiBusinessCalls;fixtureAuthority=false;msiUiOutputNote(true,true,accepted,true,source);
    check(readRecoveryRows(root).size()==beforeAuthority&&fixtureMsiBusinessCalls==businessBeforeAuthority,"msi-board-authority-rejection-retains-original-no-business-no-log-semantics");
    fixtureAuthority=true;msiUiOutputNote(true,true,accepted,true,source);rows=readRecoveryRows(root);
    check(rows.size()==beforeAuthority+1&&rows.back()["event"]=="msi-ui-output"&&rows.back()["diagnosticRecoveryReoffer"]==true&&fixtureMsiBusinessCalls==businessBeforeAuthority+1,"authority-rejection-does-not-consume-later-valid-recovery-offer");
    auto fieldRow=rows.back();check(fieldRow["sourceLive"]==true&&fieldRow["inputSource"]==source&&fieldRow["physicalReaderChanged"]==false&&fieldRow["externalPhysicalSuppression"]=="unverified","existing-msi-source-and-external-suppression-boundaries-preserved");
    const auto psBefore=rows.size();ps5UiOutputNote(true,true,accepted,true,source,2,4,19);rows=readRecoveryRows(root);
    check(rows.size()==psBefore+1&&rows.back()["sourceSlot"]==2&&rows.back()["sourceAdmittedMask"]==4&&rows.back()["sourceButtons"]==19,"original-ps5-source-snapshot-values-survive-recovery-offer");
#endif
    closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all-owned-log-handles-returned-before-exit");unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",failed},{"handlesBefore",before},{"handlesAfter",after},{"originalNoteFunctions",5},{"msiBusinessPortCalls",fixtureMsiBusinessCalls},{"boardAuthorityPortCalls",fixtureAuthorityCalls},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<'\n';return failed?2:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<'\n';return 3;}}
