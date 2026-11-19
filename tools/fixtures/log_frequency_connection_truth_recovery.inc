struct ConnectionPad {WORD wButtons=0;BYTE bLeftTrigger=0,bRightTrigger=0;SHORT sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
static constexpr BYTE XINPUT_GAMEPAD_TRIGGER_THRESHOLD=30;
static bool g_gamepadInputSampleInitialized=false,g_xinputPhysicalSourceLocked=false,g_xinputSpecialButton=false;
static constexpr bool kXInputPermissiveAnyConnectedInput=false;
static unsigned g_gamepadInputSampleMask=0;static ConnectionPad g_gamepadInputSamplePad;
static unsigned g_xinputConnectedMask=1,g_xinputVirtualEndpointMask=0,g_xinputPrimarySlot=0;
static bool g_inputHostPrepared=true,fixtureForeground=false;
static unsigned long long fixtureForegroundCalls=0;
static bool focusMainWindowIsForeground(){++fixtureForegroundCalls;return fixtureForeground;}
static void connectionTruthOffer(bool live,const ConnectionPad& pad,unsigned admittedMask,bool multipleActive){
    const bool rogRawInputSubstitution=false;const char* inputSource="fixture-canonical-cache";
@@DOMAIN_TRUTH_REGION@@
}
static json connectionRows(const fspath::path& root){auto all=readRecoveryRows(root);json rows=json::array();for(const auto& row:all)if(row.value("event","")=="domain-pad-truth")rows.push_back(row);return rows;}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;const fspath::path root(argv[1]);fspath::create_directories(root);g_testRoot=root.wstring();
    (void)ymcc::sleepfactlog::writerIdentity();DWORD before=0,after=0;if(!GetProcessHandleCount(GetCurrentProcess(),&before))return 3;
    ConnectionPad pad;connectionTruthOffer(true,pad,1,false);flushNativeLifecycleLogs(false);auto rows=connectionRows(root);
    check(rows.size()==1&&rows[0]["connectedMask"]==1&&rows[0]["guide"]==false,"original-first-connection-truth-is-resident");
    closeStreams();g_testRoot=(root/L"missing"/L"parent").wstring();g_xinputConnectedMask=3;
    const auto failuresBefore=count(Failure::eventOpen);connectionTruthOffer(true,pad,3,true);
    check(count(Failure::eventOpen)==failuresBefore+1&&ymcc::logging::nativeLogFailureEvidence().eventFailurePending.load(),"actual-resident-open-failure-loses-original-changed-connection-row");
    const auto callsBefore=fixtureForegroundCalls;ymcc::testing::allocationFault::failAfter(0);
    for(unsigned i=0;i<100000;++i)connectionTruthOffer(true,pad,3,true);
    const auto outageArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(outageArm==0&&count(Failure::eventOpen)==failuresBefore+1&&fixtureForegroundCalls==callsBefore,"100000-unchanged-outage-offers-no-allocation-retry-or-new-foreground-queries");
    g_testRoot=root.wstring();const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();appendNativeLifecycleLog("session-clean-exit",{{"fixtureRecovery",1}});
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch+1&&!ymcc::logging::nativeLogFailureEvidence().eventFailurePending.load(),"only-original-good-resident-append-confirms-log-recovery");
    pad.bLeftTrigger=1; // Analog change below original threshold must not refresh the digital sample cache.
    g_domainLogEnabled[1]=true;const auto detailBefore=g_nativeDetailWrites;const auto cachedBefore=g_gamepadInputSamplePad;static_cast<void>(detailBefore);static_cast<void>(cachedBefore);const auto preceding=connectionRows(root).size();connectionTruthOffer(true,pad,3,true);flushNativeLifecycleLogs(false);rows=connectionRows(root);g_domainLogEnabled[1]=false;
    bool restored=rows.size()==preceding+1&&rows.back()["connectedMask"]==3&&rows.back()["admittedMask"]==3;
#ifndef YMCC_CONNECTION_TRUTH_OLD_ONLY
    restored=restored&&rows.back()["diagnosticRecoveryReoffer"]==true&&rows.back()["observationScope"]=="current-connection-truth;not-input-edge-or-device-recovery"&&g_nativeDetailWrites==detailBefore&&g_gamepadInputSamplePad.wButtons==cachedBefore.wButtons&&g_gamepadInputSamplePad.bLeftTrigger==cachedBefore.bLeftTrigger&&cachedBefore.bLeftTrigger!=pad.bLeftTrigger;
#endif
    check(restored,"original-outer-gate-allows-only-current-truth-reoffer-not-extra-digital-trajectory-or-cache-update");
#ifndef YMCC_CONNECTION_TRUTH_OLD_ONLY
    const auto writes=g_nativeLifecycleFrameWrites;g_domainLogEnabled[1]=true;ymcc::testing::allocationFault::failAfter(0);
    for(unsigned i=0;i<100000;++i)connectionTruthOffer(true,pad,3,true);
    const auto healthyArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);g_domainLogEnabled[1]=false;
    check(healthyArm==0&&g_nativeLifecycleFrameWrites==writes&&g_nativeDetailWrites==detailBefore,"100000-healthy-unchanged-offers-remain-quiet-with-detail-enabled");
    pad.wButtons=1;connectionTruthOffer(true,pad,3,true);check(g_nativeLifecycleFrameWrites==writes,"ordinary-button-changes-still-do-not-create-resident-truth-edges");
    pad.wButtons=0x0400;connectionTruthOffer(true,pad,3,true);rows=connectionRows(root);check(rows.back()["guide"]==true&&rows.back()["diagnosticRecoveryReoffer"]==false,"original-guide-press-edge-stays-resident");
    pad.wButtons=0;connectionTruthOffer(true,pad,3,true);rows=connectionRows(root);check(rows.back()["guide"]==false&&rows.back()["diagnosticRecoveryReoffer"]==false,"original-guide-release-edge-stays-resident");
    fixtureForeground=true;const auto beforeFg=g_nativeLifecycleFrameWrites;connectionTruthOffer(true,pad,3,true);check(g_nativeLifecycleFrameWrites==beforeFg,"foreground-alone-remains-behind-original-digital-sample-gate");pad.wButtons=2;connectionTruthOffer(true,pad,3,true);rows=connectionRows(root);check(rows.back()["ymccForeground"]==true&&rows.back()["diagnosticRecoveryReoffer"]==false,"foreground-observation-on-original-next-digital-transition-stays-resident");
    const auto stableWrites=g_nativeLifecycleFrameWrites;++g_powerGeneration;connectionTruthOffer(true,pad,3,true);check(g_nativeLifecycleFrameWrites==stableWrites,"power-change-alone-does-not-add-an-originally-absent-truth-row");
    closeStreams();g_testRoot=(root/L"missing"/L"parent").wstring();g_xinputConnectedMask=1;connectionTruthOffer(true,pad,1,false);
    g_testRoot=root.wstring();appendNativeLifecycleLog("session-clean-exit",{{"fixtureRecovery",2}});const auto currentBefore=connectionRows(root).size();connectionTruthOffer(true,pad,1,false);rows=connectionRows(root);
    check(rows.size()==currentBefore+1&&rows.back()["connectedMask"]==1&&rows.back()["diagnosticRecoveryReoffer"]==true,"second-real-outage-epoch-reoffers-only-current-not-old-lost-connection-state");
    connectionTruthOffer(true,pad,1,false);check(connectionRows(root).size()==rows.size(),"second-recovery-offer-is-not-repeated");
#endif
    flushNativeLifecycleLogs(false);rows=connectionRows(root);closeStreams();GetProcessHandleCount(GetCurrentProcess(),&after);check(before==after,"all-owned-log-handles-return-before-exit");unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"phase","connection-truth"},{"checks",checks},{"failures",failed},{"handlesBefore",before},{"handlesAfter",after},{"rows",rows},{"originalForegroundPortCalls",fixtureForegroundCalls},{"actualOuterDigitalGateAndBothLogInitializersExecuted",true},{"fullReadOrTimerExecuted",false},{"productOrDeviceActions",0}}.dump(2)<<'\n';return failed?2:0;
}catch(const std::exception& e){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<e.what()<<'\n';return 3;}}
