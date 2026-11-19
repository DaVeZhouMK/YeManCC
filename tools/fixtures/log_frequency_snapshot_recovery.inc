static unsigned fixtureEventRows(const char* event){unsigned n=0;for(const auto& row:readRecoveryRows(fspath::path(g_testRoot)))n+=row.value("event",std::string{})==event;return n;}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    (void)ymcc::sleepfactlog::writerIdentity();
    DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    const auto finish=[&]{closeStreams();DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();
        std::cout<<json{{"selectedCase","native-sink-snapshots"},{"checks",checks},{"failures",failures},{"handlesBefore",handlesBefore},{"handlesAfter",after},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;};
    g_testRoot=(root/L"initial-missing").wstring();g_domainLogEnabled[1]=false;++g_powerGeneration;
    const json feedback={{"ok",true},{"fixture",1}};const auto initialEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    appendNativeLifecycleLog("gamepad.feedbackCaps",feedback);fspath::create_directories(g_testRoot);
    appendNativeLifecycleLog("log-export-result",{{"fixture","original-append-recovery-only"}});
    appendNativeLifecycleLog("gamepad.feedbackCaps",feedback);
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()>initialEpoch&&fixtureEventRows("gamepad.feedbackCaps")==1,"same feedback snapshot is resident after actual failed-open and original successful append");
#ifdef YMCC_SNAPSHOT_REARM_OLD_ONLY
    return finish();
#else
    struct Sample{const char* event;json value;};
    std::vector<Sample> samples={
        {"rog.hid-bound",{{"fixture",true}}},{"rog.hid-discovery-summary",{{"fixture",true}}},
        {"gamepad.feedbackCaps",feedback},{"desktop-hud-event",{{"hudEvent","desktopHud.ready"},{"fixture",true}}},
        {"oem-vendor-stack",{{"state","wake-guard-pass"},{"ok",true}}},
        {"gamepad-openxinput-slot-guard",{{"slot",0}}},{"gamepad-openxinput-assign-slot",{{"slot",0}}},
        {"oem-key-usage-query",{{"ok",true}}},{"input-host-player-start-work",{{"action","consuming"}}},
        {"input-host-switch-audit",{{"beat","throttled"},{"request",1}}},
        {"input-host-switch-audit",{{"beat","enqueue"},{"request",1}}},
        {"input-host-switch-audit",{{"beat","dequeue"},{"request",1},{"waitedMs",1},{"deferCount",1}}},
        {"input-host-switch-settle",{{"deferMs",1},{"settleMs",2}}},
        {"late-hide-enqueue",{{"outcome","coalesced"},{"coalesced",true}}},
        {"screen-touchpads-state",{{"callSite","screenTouchpads.get"},{"requestedPersona","steamdeck"},{"state",{{"enabled",true}}}}}};
    bool firstWitnesses=true,repeatedSuppressed=true,failuresDoNotRearm=true,allRearmed=true,onlyOnce=true;
    unsigned index=0;
    for(const auto& sample:samples){
        closeStreams();g_testRoot=(root/(L"case-"+std::to_wstring(index++))).wstring();fspath::create_directories(g_testRoot);++g_powerGeneration;
        firstWitnesses&=nativeLifecycleEventIsBase(sample.event)&&!nativeLifecycleRecordIsStreaming(sample.event,sample.value);
        repeatedSuppressed&=nativeLifecycleRecordIsStreaming(sample.event,sample.value);
        const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
        // The original stream opening failure is genuine; only its path is a private port.
        g_testRoot+=(L"\\not-yet-created");appendNativeLifecycleLog("log-export-result",{{"fixture","forced-open-failure"}});
        failuresDoNotRearm&=nativeLifecycleRecordIsStreaming(sample.event,sample.value)&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch;
        fspath::create_directories(g_testRoot);appendNativeLifecycleLog("log-export-result",{{"fixture","recovered-file"}});
        appendNativeLifecycleLog(sample.event,sample.value);allRearmed&=fixtureEventRows(sample.event)==1;
        appendNativeLifecycleLog(sample.event,sample.value);onlyOnce&=fixtureEventRows(sample.event)==1;
    }
    check(firstWitnesses&&samples.size()==15,"all15 bounded snapshot slots retain their original first resident selection");
    check(repeatedSuppressed,"all15 unchanged snapshot slots keep their existing detailed-or-suppressed route");
    check(failuresDoNotRearm,"an unresolved file failure cannot rearm snapshots before original successful append");
    check(allRearmed,"original successful append rearms all15 snapshot slots exactly within their existing classifier");
    check(onlyOnce,"each same snapshot stays suppressed again after its one recovery witness");
    const auto& stable=samples[2];bool unchanged=true;(void)nativeLifecycleRecordIsStreaming(stable.event,stable.value);
    ymcc::testing::allocationFault::failAfter(0);
    for(unsigned i=0;i<50000;++i)unchanged&=nativeLifecycleRecordIsStreaming(stable.event,stable.value);
    const bool unconsumed=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);
    check(unchanged&&unconsumed,"50000 stable classifier calls perform no C++ allocation after recovery");
    g_domainLogEnabled[1]=true;appendNativeLifecycleLog(stable.event,stable.value);g_nativeDetailLog.flush();
    std::ifstream detailed(fspath::path(g_testRoot)/L"native-detail.log");std::string detailLine;std::getline(detailed,detailLine);detailed.close();
    check(!detailLine.empty()&&json::parse(detailLine)["event"]==stable.event,"detail ON still retains repeated snapshot in the detailed sink");
    const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    ymcc::logging::nativeLogFailureEvidence().record(Failure::detailWrite,"fixture-detail");
    appendNativeLifecycleLog("log-export-result",{{"fixture","detail-only-fault"}});
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch&&nativeLifecycleRecordIsStreaming(stable.event,stable.value),"detail-only failure does not grant another resident snapshot");
    ++g_powerGeneration;check(!nativeLifecycleRecordIsStreaming(stable.event,stable.value),"original new-power first witness remains resident");
    const json write={{"callSite","screenTouchpads.set"},{"requestedPersona","steamdeck"},{"state",{{"enabled",true}}}};
    check(!nativeLifecycleRecordIsStreaming("screen-touchpads-state",write)&&!nativeLifecycleRecordIsStreaming("screen-touchpads-state",write),"explicit settings-write receipts still remain resident on every call");
    const auto evidence=exportEvidence()["nativeLogFailureEvidence"]["stats"]["eventAppendRecovery"];
    check(evidence["schemaVersion"]==7&&evidence["oldLostRowRestored"]==false&&evidence["currentFileHealthProven"]==false,"export describes schema7 rearm scope without replay or file-health proof");
    return finish();
#endif
}
