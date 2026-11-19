static json exportIpcEvidence(){json availability;@@ACTUAL_IPC_EXPORT@@ return availability;}
static json ipcFixtureRow(unsigned kind,int code=5){
    json value={{"childId",7},{"command","fixture"},{"messageKind","response"},{"requestId",1},{"requestIdPresent",true},
        {"eventName",nullptr},{"eventNamePresent",false},{"eventNameOmitted",false}};
    if(kind==0)value["hresult"]=-code;else if(kind==2)value["jsonError"]=code;return value;
}
static json ipcFixtureRows(){json rows=json::array();for(const auto& row:readRecoveryRows(fspath::path(g_testRoot)))if(ymcc::logging::IpcFailureLogState::eventIndex(row.value("event",std::string{}).c_str())>=0)rows.push_back(row);return rows;}
#if defined(YMCC_IPC_REOFFER_EVENT_GATES_ONLY)
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    (void)ymcc::sleepfactlog::writerIdentity();DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    g_domainLogEnabled[1]=false;g_testRoot=root.wstring();
    const char* events[]={"ipc-response-delivery-failed","ipc-response-delivery-exception","ipc-response-serialization-failed"};
    for(unsigned i=0;i<64;++i)appendNativeLifecycleLog(events[0],ipcFixtureRow(0,int(i+1)));
    const auto descriptors=exportIpcEvidence()["nativeIpcFailureEvidence"]["descriptors"];
    check(descriptors.size()==64&&ipcFixtureRows().size()==64,"actual logger fills exactly 64 original lifetime type descriptors");
    bool capacity=true,unknown=true,pending=true,stable=true;unsigned expectedRows=64;
    for(unsigned group=0;group<2;++group)for(unsigned kind=0;kind<3;++kind){
        const json row=group==0?ipcFixtureRow(kind,1000):json{{"fixtureUnknown",true}};
        appendNativeLifecycleLog(events[kind],row);auto rows=ipcFixtureRows();
        bool valid=rows.size()==++expectedRows&&rows.back()["failureLogSelection"]["typeIndex"].is_null()&&
            rows.back()["failureLogSelection"]["reason"]==(group==0?"capacity-first-witness":"unclassified-first-witness");
        const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
        {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);appendNativeLifecycleLog("log-export-result",{{"fixture","actual-event-gate-file-fault"}});}
        appendNativeLifecycleLog(events[kind],row);pending&=ipcFixtureRows().size()==expectedRows&&ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch;
        appendNativeLifecycleLog("log-export-result",{{"fixture","original-event-gate-file-recovery"}});appendNativeLifecycleLog(events[kind],row);rows=ipcFixtureRows();
        valid&=rows.size()==++expectedRows&&rows.back()["failureLogSelection"]["schema"]==2&&rows.back()["failureLogSelection"]["typeIndex"].is_null()&&
            rows.back()["failureLogSelection"]["reason"]==(group==0?"capacity-recovery-witness":"unclassified-recovery-witness")&&rows.back()["failureLogSelection"]["recoveryEpoch"]==epoch+1;
        if(group==0)capacity&=valid;else unknown&=valid;
        ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<10000;++i)appendNativeLifecycleLog(events[kind],row);
        stable&=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);stable&=ipcFixtureRows().size()==expectedRows;
    }
    check(capacity,"all three event kinds write capacity recovery markers through the original complete logger");
    check(unknown,"all three event kinds write unclassified recovery markers through the original complete logger");
    check(pending,"all six pending file failures keep event gates suppressed until original successful append");
    check(stable,"six times 10000 full repeated event gate calls allocate no C++ memory or extra resident row");
    const auto stats=exportIpcEvidence()["nativeIpcFailureEvidence"];
    check(stats["typesRetained"]==64&&stats["descriptors"]==descriptors,"actual capacity and unclassified reoffers never clear or replace lifetime descriptors");
    check(stats["fileRecoveryReoffer"]["selected"]==6&&stats["fileRecoveryReoffer"]["lastObservedEpoch"]==6,"actual six event gate recovery witnesses use six original append epochs");
    check(ipcFixtureRows().size()==76,"actual low file has only 64 first types and 12 bounded event gate witnesses");
    closeStreams();DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);check(after==handlesBefore,"event gate logger returns owned stream handles before exit");
    unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();
    std::cout<<json{{"selectedCase","native-sink-ipc-reoffer-event-gates"},{"checks",checks},{"failures",failures},{"handlesBefore",handlesBefore},{"handlesAfter",after},
        {"evidence",exportEvidence()},{"ipcEvidence",exportIpcEvidence()},{"retainedRows",readRecoveryRows(root)},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;
}

#else
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    (void)ymcc::sleepfactlog::writerIdentity();DWORD handlesBefore=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    const auto finish=[&]{closeStreams();DWORD after=0;GetProcessHandleCount(GetCurrentProcess(),&after);unsigned failures=0;for(const auto& item:checks)failures+=!item["pass"].get<bool>();
        std::cout<<json{{"selectedCase","native-sink-ipc-reoffer"},{"checks",checks},{"failures",failures},{"handlesBefore",handlesBefore},{"handlesAfter",after},
            {"evidence",exportEvidence()},{"ipcEvidence",exportIpcEvidence()},{"retainedRows",readRecoveryRows(fspath::path(g_testRoot))},{"hardwareActions",0}}.dump(2)<<'\n';return failures?2:0;};
    g_domainLogEnabled[1]=false;++g_powerGeneration;g_testRoot=(root/L"initial-missing").wstring();
    const char* events[]={"ipc-response-delivery-failed","ipc-response-delivery-exception","ipc-response-serialization-failed"};
    const json initial=ipcFixtureRow(0);const auto initialEpoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
    appendNativeLifecycleLog(events[0],initial);fspath::create_directories(g_testRoot);appendNativeLifecycleLog("log-export-result",{{"fixture","original-good-append"}});appendNativeLifecycleLog(events[0],initial);
    const auto recovered=ipcFixtureRows();
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()>initialEpoch&&recovered.size()==1&&recovered[0]["failureLogSelection"]["reason"]=="type-recovery-witness","same IPC failure type is retained after actual failed open and original append recovery");
#ifdef YMCC_IPC_REOFFER_OLD_ONLY
    return finish();
#else
    const auto firstStats=exportIpcEvidence()["nativeIpcFailureEvidence"];
    check(firstStats["typesRetained"]==1&&firstStats["offered"]==2&&firstStats["repeated"]==1&&firstStats["fileRecoveryReoffer"]["selected"]==1,"real recovery keeps original type index and lifetime counters instead of resetting table");
    bool first=true,repeated=true,pending=true,once=true,noAlloc=true;json last;
    for(unsigned kind=0;kind<3;++kind){
        closeStreams();g_testRoot=(root/(L"event-"+std::to_wstring(kind))).wstring();fspath::create_directories(g_testRoot);
        auto row=ipcFixtureRow(kind,316);row["command"]="event-"+std::to_string(kind);last=row;
        appendNativeLifecycleLog(events[kind],row);auto rows=ipcFixtureRows();first&=rows.size()==1&&rows[0]["failureLogSelection"]["reason"]=="first-type";
        appendNativeLifecycleLog(events[kind],row);repeated&=ipcFixtureRows().size()==1;
        const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();
        {ControlledBuffer buffer(ControlledBuffer::Mode::writeFail);BufferLease lease(g_nativeLifecycleLog,buffer);appendNativeLifecycleLog("log-export-result",{{"fixture","actual-write-fault"}});}
        appendNativeLifecycleLog(events[kind],row);pending&=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch&&ipcFixtureRows().size()==1;
        appendNativeLifecycleLog("log-export-result",{{"fixture","natural-good-write"}});appendNativeLifecycleLog(events[kind],row);appendNativeLifecycleLog(events[kind],row);
        rows=ipcFixtureRows();once&=rows.size()==2&&rows.back()["failureLogSelection"]["schema"]==2&&rows.back()["failureLogSelection"]["reason"]=="type-recovery-witness"&&
            rows.back()["failureLogSelection"]["typeIndex"]==rows.front()["failureLogSelection"]["typeIndex"]&&rows.back()["failureLogSelection"]["recoveryEpoch"]==epoch+1;
        ymcc::testing::allocationFault::failAfter(0);for(unsigned i=0;i<50000;++i)appendNativeLifecycleLog(events[kind],row);
        noAlloc&=ymcc::testing::allocationFault::countdown.load()==0;ymcc::testing::allocationFault::failAfter(-1);noAlloc&=ipcFixtureRows().size()==2;
    }
    check(first,"all three actual IPC failure events keep their original first type witness");
    check(repeated,"all three actual IPC failure events suppress unchanged same epoch repetitions");
    check(pending,"actual unresolved file write failure cannot grant an IPC reoffer");
    check(once,"all three failure events reuse type index for exactly one recovery witness");
    check(noAlloc,"three times 50000 full repeated IPC logging calls allocate no C++ memory or extra resident row");
    g_domainLogEnabled[1]=true;appendNativeLifecycleLog(events[2],last);g_nativeDetailLog.flush();
    std::ifstream detail(fspath::path(g_testRoot)/L"native-detail.log");std::string line;std::getline(detail,line);detail.close();g_domainLogEnabled[1]=false;
    check(!line.empty()&&json::parse(line)["failureLogSelection"]["reason"]=="repeat-type"&&json::parse(line)["failureLogSelection"]["schema"]==2,"detail ON still retains unchanged IPC repetitions with strict schema2 marker");
    const auto epoch=ymcc::logging::nativeLogFailureEvidence().recoveryEpoch();ymcc::logging::nativeLogFailureEvidence().record(Failure::detailWrite,"fixture-detail-only");
    appendNativeLifecycleLog("log-export-result",{{"fixture","detail-only-fault"}});appendNativeLifecycleLog(events[2],last);
    check(ymcc::logging::nativeLogFailureEvidence().recoveryEpoch()==epoch&&ipcFixtureRows().size()==2,"detail-only sink failure never grants an IPC recovery witness");
    using Table=ymcc::logging::IpcFailureLogState;Table table;std::vector<json> typed;
    bool sixtyFour=true;for(unsigned i=0;i<64;++i){typed.push_back(ipcFixtureRow(0,int(i+1)));const auto d=table.observe(events[0],typed.back(),7);sixtyFour&=d.selected&&d.index==i&&std::strcmp(d.reason,"first-type")==0;}
    const auto descriptors=table.snapshot()["descriptors"];const auto offered=table.offered;
    const auto d0=table.observe(events[0],typed[0],8);sixtyFour&=d0.selected&&d0.index==0&&std::strcmp(d0.reason,"type-recovery-witness")==0&&table.types==64&&table.offered==offered+1&&table.repeated==1;
    check(sixtyFour&&table.snapshot()["descriptors"]==descriptors,"all 64 retained type descriptors and original counters survive reoffer without new type admission");
    bool overflow=true;for(unsigned kind=0;kind<3;++kind){
        const auto row=ipcFixtureRow(kind,1000);const auto firstGate=table.observe(events[kind],row,8);const auto repeatGate=table.observe(events[kind],row,8);
        overflow&=firstGate.selected&&!repeatGate.selected&&firstGate.index==Table::Capacity&&std::strcmp(firstGate.reason,"capacity-first-witness")==0;
    }
    for(unsigned kind=0;kind<3;++kind){const auto row=ipcFixtureRow(kind,1000);const auto d=table.observe(events[kind],row,9);overflow&=d.selected&&std::strcmp(d.reason,"capacity-recovery-witness")==0&&!table.observe(events[kind],row,9).selected;}
    check(overflow&&table.types==64&&table.snapshot()["descriptors"]==descriptors,"overflow witnesses remain one per original event gate and epoch without clearing capacity");
    bool unsupported=true;const json unknown=json::object();for(unsigned kind=0;kind<3;++kind){
        const auto d=table.observe(events[kind],unknown,9);unsupported&=d.selected&&std::strcmp(d.reason,"unclassified-first-witness")==0&&!table.observe(events[kind],unknown,9).selected;
    }
    for(unsigned kind=0;kind<3;++kind){const auto d=table.observe(events[kind],unknown,10);unsupported&=d.selected&&std::strcmp(d.reason,"unclassified-recovery-witness")==0&&!table.observe(events[kind],unknown,10).selected;}
    check(unsupported&&table.types==64&&table.snapshot()["descriptors"]==descriptors,"unclassified witnesses keep three bounded event gates with one recovery witness each");
    unsigned selections=0;for(unsigned i=0;i<64;++i)selections+=table.observe(events[0],typed[i],11).selected;
    bool reofferOnce=selections==64;for(unsigned i=0;i<64;++i)reofferOnce&=!table.observe(events[0],typed[i],11).selected;
    check(reofferOnce&&table.types==64,"one new recovery epoch reoffers each existing type once and leaves the type table full");
    auto idOnly=typed[0];idOnly["requestId"]=999;idOnly["childId"]=999;const auto identity=table.observe(events[0],idOnly,11);
    check(!identity.selected&&identity.index==0&&table.snapshot()["descriptors"]==descriptors,"request and child IDs remain witnesses rather than failure type identities");
    const auto legacy=table.observe(events[0],typed[0]);const auto stale=table.observe(events[0],typed[0],10);
    check(!legacy.selected&&!stale.selected&&table.lastObservedRecoveryEpoch==11,"legacy two argument callers and stale epochs cannot rewind the original reoffer boundary");
    table.reoffered=table.offered=table.repeated=~0ULL;const auto saturated=table.observe(events[0],typed[0],~0ULL);const auto wrapped=table.observe(events[0],typed[0],0);
    check(saturated.selected&&!wrapped.selected&&table.lastObservedRecoveryEpoch==~0ULL&&table.reoffered==~0ULL&&table.offered==~0ULL&&table.repeated==~0ULL,"recovery epoch and observation counters saturate without wrap or repeated allowance");
    const auto stats=exportIpcEvidence()["nativeIpcFailureEvidence"];const auto boundary=stats["fileRecoveryReoffer"];
    check(stats["schema"]==2&&stats["capacity"]==64&&stats["firstSelectionMeansFileWrite"]==false&&boundary["schemaVersion"]==1&&boundary["typeCapacityReset"]==false&&boundary["counterReset"]==false&&boundary["deliveryProven"]==false,"actual IPC export describes reoffer selection separately from delivery and lifetime type capacity");
    return finish();
#endif
}

#endif
