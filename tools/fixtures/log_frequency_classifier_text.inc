struct ClassifierTextProbe {const char* event;const char* key;json row;};
static std::vector<ClassifierTextProbe> classifierTextProbes(){
    const std::string longText(192,'x');
    return {
        {"gamepad-vibrate-request","requestSource",{{"requestSource",longText}}},
        {"system-xinput-readback","reason",{{"reason",longText}}},
        {"oem-producer-edge","status",{{"status",longText},{"reason","state-transition"}}},
        {"oem-producer-edge","reason",{{"status","queued"},{"reason",longText}}},
        {"msi-wmi-key","reason",{{"reason",longText},{"accepted",true},{"posted",true}}},
        {"physical-controller-identity","snapshotReason",{{"snapshotReason",longText}}},
        {"input-host-frame-evidence","snapshotReason",{{"snapshotReason",longText}}},
        {"input-host-command-result","command",{{"command",longText},{"ok",true}}},
        {"desktop-hud-event","hudEvent",{{"hudEvent",longText},{"width",800}}},
        {"msi-rear-source-observation","snapshotReason",{{"snapshotReason",longText}}},
        {"oem-vendor-stack","state",{{"state",longText},{"ok",true}}},
        {"input-host-player-start-work","action",{{"action",longText},{"serial",1}}},
        {"input-host-switch-audit","beat",{{"beat",longText},{"serial",1}}},
        {"late-hide-enqueue","outcome",{{"outcome",longText},{"coalesced",true}}},
    };
}

// CLASSIFIER_REFERENCE

static int classifierResult(bool before,const char* event,const json& row){
    try{
#ifdef YMCC_CLASSIFIER_REFERENCE
        if(before)return nativeLifecycleRecordIsStreamingBefore(event,row)?1:0;
#else
        (void)before;
#endif
        return nativeLifecycleRecordIsStreaming(event,row)?1:0;
    }catch(const json::type_error&){return 2;}catch(...){return 3;}
}

int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 3;
    const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    g_testRoot=root.wstring();g_domainLogEnabled[1]=false;(void)ymcc::sleepfactlog::writerIdentity();
    DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    const auto probes=classifierTextProbes();
    const auto& silent=probes.front();appendNativeLifecycleLog(silent.event,silent.row);
    const auto preparationBefore=count(Failure::preparationException);
    ymcc::testing::allocationFault::failAfter(0);appendNativeLifecycleLog(silent.event,silent.row);
    const auto firstArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(firstArm==0&&count(Failure::preparationException)==preparationBefore&&!g_nativeLifecycleLog.is_open()&&!g_nativeDetailLog.is_open(),
        "silent actual append borrows long classification text without consuming allocation fault or creating preparation failure");
#ifndef YMCC_CLASSIFIER_OLD_ONLY
    ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog(silent.event,silent.row);
    const auto burstArm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    check(burstArm==0&&count(Failure::preparationException)==preparationBefore&&!fspath::exists(root/L"native-lifecycle.log")&&!fspath::exists(root/L"native-detail.log"),
        "50000 original suppressed append calls allocate no text and open no Native file");
    json probeResults=json::array();
    for(const auto& probe:probes){
        (void)classifierResult(false,probe.event,probe.row); // Prime a changed snapshot before comparing steady reads.
        const auto expected=classifierResult(false,probe.event,probe.row);
        bool equal=expected<2;ymcc::testing::allocationFault::failAfter(0);
        for(unsigned i=0;i<5000;++i)equal&=classifierResult(false,probe.event,probe.row)==expected;
        const auto arm=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
        probeResults.push_back({{"event",probe.event},{"key",probe.key},{"iterations",5000},{"allocationFaultUnconsumed",arm==0},{"sameDecision",equal}});
    }
    bool probesPass=true;for(const auto& value:probeResults)probesPass&=value["allocationFaultUnconsumed"].get<bool>()&&value["sameDecision"].get<bool>();
    check(probesPass&&probeResults.size()==14,"fourteen long text branch inputs retain decisions for 70000 allocation-free classifier calls");
    const auto badBefore=count(Failure::preparationException);unsigned malformedRows=0;
    for(const auto& probe:probes){auto row=probe.row;row[probe.key]=false;appendNativeLifecycleLog(probe.event,row);++malformedRows;}
    check(count(Failure::preparationException)==badBefore+malformedRows&&malformedRows==14&&!fspath::exists(root/L"native-lifecycle.log"),
        "malformed existing text fields still enter original preparation exception bucket rather than silently using fallback");
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==0&&!ymcc::logging::nativeLogFailureEvidence().residentRecordPreparationFailurePending.load(),
        "before-selection type errors grant no selected resident preparation or recovery epoch");
    unsigned differentialCases=0;bool differentialExact=true;
#ifdef YMCC_CLASSIFIER_REFERENCE
    const std::vector<json> values={nullptr,false,true,0,-1,~0ULL,0.5,json::array({1}),json{{"x",1}},"","queued","state-transition","manual-test","repeat",std::string(192,'x'),std::string("A\0B",3)};
    for(unsigned power=0;power<4;++power){
        g_powerGeneration.store(20+power);
        for(const auto& probe:probes){
            auto missing=probe.row;missing.erase(probe.key);
            differentialExact&=classifierResult(true,probe.event,missing)==classifierResult(false,probe.event,missing);++differentialCases;
            for(const auto& value:values){auto row=probe.row;row[probe.key]=value;
                for(unsigned repeat=0;repeat<2;++repeat){
                    differentialExact&=classifierResult(true,probe.event,row)==classifierResult(false,probe.event,row);++differentialCases;
                }
            }
        }
    }
    check(differentialExact&&differentialCases==1848,"original complete classifier ordered differential preserves text types missing values generation and repeats");
#endif
    // Real rows still reach the original sinks after classification, including
    // explicit action receipts and detailed trajectories.
    appendNativeLifecycleLog("gamepad-vibrate-request",{{"requestSource","manual-test"}});
    g_domainLogEnabled[1]=true;appendNativeLifecycleLog(silent.event,silent.row);g_domainLogEnabled[1]=false;
    closeStreams();const auto resident=readRecoveryRows(root);std::ifstream detail(root/L"native-detail.log");std::string detailLine;std::getline(detail,detailLine);detail.close();
    check(resident.size()==1&&resident[0]["requestSource"]=="manual-test"&&resident[0]["event"]=="gamepad-vibrate-request"&&
        json::parse(detailLine)["requestSource"]==silent.row["requestSource"],
        "manual action receipt remains resident and enabled detailed trajectory preserves its original long text");
    const auto unknownBefore=count(Failure::preparationException);appendNativeLifecycleLog("gamepad-vibrate-request",json::array({1}));
    check(count(Failure::preparationException)==unknownBefore,"original append still normalizes nonobject input before classifier text access");
#endif
    closeStreams();DWORD handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);
    check(handlesBefore==handlesAfter,"all owned Native file and writer query handles returned before exit");
    unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();
    json result={{"selectedCase","native-sink-classifier-text"},{"checks",checks},{"failures",failures},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"evidence",exportEvidence()},{"hardwareActions",0}};
#ifndef YMCC_CLASSIFIER_OLD_ONLY
    result["probeResults"]=std::move(probeResults);result["differentialCases"]=differentialCases;result["differentialExact"]=differentialExact;
#endif
    std::cout<<result.dump(2)<<'\n';return failures?2:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 3;}}
