static unsigned diagnosticEventRows(const char* event){unsigned n=0;for(const auto& row:readRecoveryRows(fspath::path(g_testRoot)))n+=row.value("event",std::string{})==event;return n;}
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    (void)ymcc::sleepfactlog::writerIdentity();DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    const auto finish=[&]{closeStreams();DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();
        std::cout<<json{{"selectedCase","native-sink-diagnostic-memos"},{"checks",checks},{"failures",failures},{"handlesBefore",handlesBefore},{"handlesAfter",after},
            {"evidence",exportEvidence()},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;};
    g_domainLogEnabled[1]=false;++g_powerGeneration;g_testRoot=(root/L"initial-missing").wstring();
    const json publication={{"consumer","lifecycle-publication"},{"reason","fixture-failure"}};
    const auto initialEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    appendNativeLifecycleLog("power-projection-consumer-failed",publication);fspath::create_directories(g_testRoot);
    appendNativeLifecycleLog("log-export-result",{{"fixture","original-successful-append"}});appendNativeLifecycleLog("power-projection-consumer-failed",publication);
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()>initialEpoch&&diagnosticEventRows("power-projection-consumer-failed")==1,"same publication failure is retained after actual failed open and original file recovery");
#ifdef YMCC_DIAGNOSTIC_MEMO_OLD_ONLY
    return finish();
#else
    struct Sample{const char* event;json value;};
    const std::vector<Sample> samples={
        {"power-projection-consumer-failed",publication},
        {"input-host-recovery-fallback",{{"state","queue-refused"},{"serial",1},{"requestId",1}}},
        {"input-host-quarantine-endpoint-probe",{{"verdict","waiting"}}},
        {"input-host-quarantine-endpoint-probe",{{"owner","capture-lifecycle-thread"},{"verdict","waiting"}}},
        {"input-host-quarantine-restore-held",{{"reason","waiting"}}}};
    bool first=true,repeated=true,pendingHeld=true,rearmed=true,once=true,stableNoAlloc=true;unsigned index=0;
    for(const auto& sample:samples){
        closeStreams();++g_powerGeneration;g_testRoot=(root/(L"case-"+std::to_wstring(index++))/L"missing").wstring();
        first&=nativeLifecycleEventIsBase(sample.event)&&!nativeLifecycleRecordIsStreaming(sample.event,sample.value);
        repeated&=nativeLifecycleRecordIsStreaming(sample.event,sample.value);
        const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
        appendNativeLifecycleLog("log-export-result",{{"fixture","actual-open-failure"}});
        pendingHeld&=nativeLifecycleRecordIsStreaming(sample.event,sample.value)&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch;
        fspath::create_directories(g_testRoot);appendNativeLifecycleLog("log-export-result",{{"fixture","original-append-recovered"}});
        appendNativeLifecycleLog(sample.event,sample.value);rearmed&=diagnosticEventRows(sample.event)==1;
        appendNativeLifecycleLog(sample.event,sample.value);once&=diagnosticEventRows(sample.event)==1;
        ymcc::testing::allocationFault::failAfter(0);bool unchanged=true;
        for(unsigned i=0;i<50000;++i)unchanged&=nativeLifecycleRecordIsStreaming(sample.event,sample.value);
        stableNoAlloc&=unchanged&&ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);
    }
    check(first&&samples.size()==5,"all five diagnostic memo slots retain original first resident selection");
    check(repeated,"all five unchanged diagnostic observations keep their original repeat route");
    check(pendingHeld,"unresolved original file failure never rearms diagnostic memo slots");
    check(rearmed,"original successful append permits one current witness in each diagnostic slot");
    check(once,"same diagnostic observations return to suppression after their recovery witness");
    check(stableNoAlloc,"five times 50000 stable classifier calls consume no C++ allocation");
    ++g_powerGeneration;bool independent=true;
    for(unsigned i=0;i<2;++i){
        const auto& bus=samples[2];const auto& owner=samples[3];
        independent&=nativeLifecycleRecordIsStreaming(bus.event,bus.value)==(i!=0);
        independent&=nativeLifecycleRecordIsStreaming(owner.event,owner.value)==(i!=0);
    }
    check(independent,"BUS and lifecycle endpoint observations keep independent memo slots");
    const json cleared={{"reason","fixture-cleared"}};
    bool clear=!nativeLifecycleRecordIsStreaming("input-host-quarantine-cleared",cleared)&&!nativeLifecycleRecordIsStreaming("input-host-quarantine-cleared",cleared);
    for(unsigned i=2;i<5;++i)clear&=!nativeLifecycleRecordIsStreaming(samples[i].event,samples[i].value);
    check(clear,"original explicit cleared receipt stays resident and rearms all three quarantine slots");
    const json invalidRefusal={{"state","queue-refused"},{"serial",0},{"requestId",1}},unknownOwner={{"owner","unknown-owner"}},unknownBus={{"ownedSdReturnCandidate",false}};
    bool unknown=true;for(unsigned i=0;i<10;++i){
        unknown&=!nativeLifecycleRecordIsStreaming(samples[1].event,invalidRefusal);
        unknown&=!nativeLifecycleRecordIsStreaming(samples[2].event,unknownOwner)&&!nativeLifecycleRecordIsStreaming(samples[2].event,unknownBus);
    }
    check(unknown,"invalid queue context and unfamiliar quarantine producers retain original resident treatment");
    const auto& stable=samples[0];(void)nativeLifecycleRecordIsStreaming(stable.event,stable.value);
    g_domainLogEnabled[1]=true;appendNativeLifecycleLog(stable.event,stable.value);g_nativeDetailLog.flush();
    std::ifstream detail(fspath::path(g_testRoot)/L"native-detail.log");std::string line;std::getline(detail,line);detail.close();g_domainLogEnabled[1]=false;
    check(!line.empty()&&json::parse(line)["event"]==stable.event,"detail ON still retains repeated diagnostic failure in detailed file");
    const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();ymcc::logging::nativeLogFailureEvidence().record(Failure::detailWrite,"fixture-detail-only");
    appendNativeLifecycleLog("log-export-result",{{"fixture","detail-only-failure"}});
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch&&nativeLifecycleRecordIsStreaming(stable.event,stable.value),"detail-only failure does not grant another resident diagnostic witness");
    auto changed=stable.value;changed["reason"]="new-error";
    bool semantics=!nativeLifecycleRecordIsStreaming(stable.event,changed);++g_powerGeneration;semantics&=!nativeLifecycleRecordIsStreaming(stable.event,changed);
    check(semantics,"semantic error change and original power generation still retain first evidence");
    const auto recovery=exportEvidence()["nativeLogFailureEvidence"]["stats"]["eventAppendRecovery"];
    check(recovery["schemaVersion"]==7&&recovery["oldLostRowRestored"]==false&&recovery["currentFileHealthProven"]==false,"schema7 describes diagnostic memo rearm without claiming delivery or device health");
    return finish();
#endif
}
