#if defined(YMCC_DECKY_FAILURE_COUNTERS_ONLY)
static unsigned long long failureCount(size_t index){return ymcc::logging::deckyLogFailureEvidence().snapshot()["buckets"][index]["observedFailures"].get<unsigned long long>();}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    using Failure=ymcc::logging::DeckyLogFailureEvidence;
    DWORD handlesBefore=0,handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    const auto nativeBefore=ymcc::logging::nativeLogFailureEvidence().snapshot();
    reset(root,L"preparation");const json queued={{"generation",1ull},{"ready",true},{"revision",1u}};
    fixtureFault=FaultSite::path;deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);fixtureFault=FaultSite::none;ymcc::testing::allocationFault::failAfter(-1);
    check("actual-path-allocation-exception-is-counted-without-escaping",failureCount(Failure::preparationException)==1&&readRows(L"decky-sidebar.log").empty());
#ifndef YMCC_DECKY_FAILURE_OLD_ONLY
    deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);
    check("preparation-failure-keeps-next-original-queue-offer",readRows(L"decky-sidebar.log").size()==1);
    for(unsigned i=0;i<2;++i){
        reset(root,i?L"open-exception":L"open-failure");const auto path=fspath::path(g_testRoot)/L"decky-sidebar.log";
        {std::ofstream seed(path);seed<<"{\"old\":true}\n";}
        HANDLE blocker=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        HANDLE probe=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);const auto error=GetLastError();
        const bool realRefusal=blocker!=INVALID_HANDLE_VALUE&&probe==INVALID_HANDLE_VALUE&&error==ERROR_SHARING_VIOLATION;
        if(probe!=INVALID_HANDLE_VALUE)CloseHandle(probe);outputFault=i?OutputFault::openException:OutputFault::none;
        deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);outputFault=OutputFault::none;if(blocker!=INVALID_HANDLE_VALUE)CloseHandle(blocker);
        deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);
        check(i?"actual-open-ios-exception-has-own-bucket-and-next-original-offer":"real-sharing-refusal-has-open-boundary-bucket-and-next-offer",
            realRefusal&&failureCount(i?Failure::openException:Failure::openFailure)==1&&readRows(L"decky-sidebar.log").size()==2);
    }
    const OutputFault modes[]={OutputFault::writeFailure,OutputFault::writeException,OutputFault::flushFailure,OutputFault::flushException};
    const Failure::Kind kinds[]={Failure::writeFailure,Failure::writeException,Failure::flushFailure,Failure::flushException};
    const char* names[]={"write-bad-state-counted-once-without-double-flush-failure","write-ios-exception-counted-in-write-stage","flush-bad-state-counted-in-flush-stage","flush-ios-exception-counted-in-flush-stage"};
    for(unsigned i=0;i<4;++i){
        reset(root,(L"stream-"+std::to_wstring(i)).c_str());outputFault=modes[i];
        const auto before=ymcc::logging::deckyLogFailureEvidence().snapshot();deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);outputFault=OutputFault::none;
        const auto after=ymcc::logging::deckyLogFailureEvidence().snapshot();bool exact=true;
        for(size_t k=0;k<Failure::Capacity;++k)exact=exact&&after["buckets"][k]["observedFailures"].get<unsigned long long>()==before["buckets"][k]["observedFailures"].get<unsigned long long>()+(k==kinds[i]?1:0);
        const auto calls=failurePathQueries;deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);
        check(names[i],exact&&failurePathQueries==calls+1);
    }
    const auto saved=ymcc::logging::deckyLogFailureEvidence().snapshot();const auto calls=failurePathQueries;
    for(unsigned i=0;i<5000;++i)deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);
    check("5000-healthy-repeats-add-no-failure-or-file-path-query",failurePathQueries==calls&&ymcc::logging::deckyLogFailureEvidence().snapshot()==saved);
    check("sidecar-failures-do-not-advance-Native-recovery-or-counters",ymcc::logging::nativeLogFailureEvidence().snapshot()==nativeBefore);
    const auto exported=failureAvailability();
    check("actual-optional-export-publishes-separate-seven-stage-receipt",exported.contains("deckyLogFailureEvidence")&&exported["deckyLogFailureEvidence"]["stats"]==saved&&exported["deckyLogFailureEvidence"]["writerPid"]==GetCurrentProcessId());
    bool noEscape=true;json failedExport=json::object();ymcc::testing::allocationFault::failAfter(0);
    try{appendFailureAvailability(failedExport);}catch(...){noEscape=false;}ymcc::testing::allocationFault::failAfter(-1);
    check("optional-export-allocation-failure-does-not-escape-or-fabricate-zero",noEscape&&(!failedExport.contains("deckyLogFailureEvidence")||failedExport["deckyLogFailureEvidence"].is_null()));
    Failure local;std::string longEvent(120,'x');longEvent[2]='\n';longEvent[3]=char(-1);
    ymcc::testing::allocationFault::failAfter(0);local.record(Failure::writeFailure,longEvent.c_str());const auto armed=ymcc::testing::allocationFault::countdown.load();ymcc::testing::allocationFault::failAfter(-1);
    const auto prefix=local.snapshot()["buckets"][Failure::writeFailure];
    check("failure-bookkeeping-is-allocation-free-and-prefix-is-bounded",armed==0&&prefix["firstEvent"].get<std::string>().size()==95&&prefix["firstEventTruncated"]==true&&prefix["firstEventAsciiSanitized"]==true&&prefix["firstEvent"].get<std::string>().substr(2,2)=="??");
    local.record(Failure::preparationException,nullptr);local.record(Failure::preparationException,"later-name");
    check("unknown-first-event-remains-null-after-later-known-event",local.snapshot()["buckets"][Failure::preparationException]["firstEvent"].is_null());
    {std::lock_guard lock(local.mutex);local.buckets[Failure::writeFailure].count=~0ULL-1;}
    local.record(Failure::writeFailure,"next");local.record(Failure::writeFailure,"next");
    check("uint64-failure-counter-saturates-with-original-prefix",local.snapshot()["buckets"][Failure::writeFailure]["observedFailures"]==~0ULL&&local.snapshot()["buckets"][Failure::writeFailure]["counterLimitReached"]==true);
    std::vector<std::thread> threads;for(unsigned i=0;i<4;++i)threads.emplace_back([&]{for(unsigned j=0;j<5000;++j)local.record(Failure::openFailure,"concurrent");});for(auto& thread:threads)thread.join();
    check("concurrent-failure-observations-keep-exact-bounded-count",local.snapshot()["buckets"][Failure::openFailure]["observedFailures"]==20000);
#else
    const auto exported=failureAvailability();
    check("actual-optional-export-publishes-separate-seven-stage-receipt",exported.contains("deckyLogFailureEvidence"));
#endif
    GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);check("all-owned-files-sharing-handles-and-threads-return",handlesBefore==handlesAfter);
    unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();
    std::cout<<json{{"selectedCase","decky-mode-health"},{"selectedPhase","failure-counters"},{"checks",checks},{"failures",failures},{"availability",exported},
        {"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"trackerBytes",sizeof(Failure)},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<std::endl;return failures?3:0;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<std::endl;return 4;}}
#elif defined(YMCC_DECKY_JSONL_BOUNDARY_ONLY)
#include <iterator>
static std::string boundaryRead(const fspath::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
static fspath::path boundaryPath(){return fspath::path(g_testRoot)/L"decky-sidebar.log";}
static void boundarySeed(const std::string& bytes){std::ofstream file(boundaryPath(),std::ios::binary|std::ios::trunc);file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.flush();if(!file.good())throw std::runtime_error("own seed write failed");}
static json boundaryLast(const std::string& bytes){
    const auto end=bytes.find_last_not_of('\n');if(end==std::string::npos)return json(json::value_t::discarded);
    const auto split=bytes.rfind('\n',end);const auto start=split==std::string::npos?0:split+1;
    return json::parse(bytes.substr(start,end-start+1),nullptr,false);
}
static void boundaryOffer(){deckySidebarDiagnostic("runtime",{{"phase","starting"}},true);}
static bool boundarySeparated(const std::string& bytes,const std::string& seed,const char* event="runtime"){const auto row=boundaryLast(bytes);return bytes.size()>seed.size()+1&&bytes.compare(0,seed.size(),seed)==0&&bytes[seed.size()]=='\n'&&!row.is_discarded()&&row.value("event",std::string{})==event&&row["diagnosticSchema"]==2;}
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
    DWORD handlesBefore=0,handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    reset(root,L"interrupted-tail");const std::string broken="{\"event\":\"old-incomplete\",\"partial\":";boundarySeed(broken);boundaryOffer();
    check("interrupted-old-bytes-survive-with-next-whole-row-separated",boundarySeparated(boundaryRead(boundaryPath()),broken));
    reset(root,L"complete-without-lf");const std::string complete="{\"event\":\"old-complete\",\"fixture\":true}";boundarySeed(complete);boundaryOffer();
    check("valid-legacy-row-without-newline-and-current-row-remain-independent",boundarySeparated(boundaryRead(boundaryPath()),complete));
#ifndef YMCC_DECKY_JSONL_OLD_ONLY
    bool empty=true;for(const bool exists:{false,true}){reset(root,exists?L"empty-file":L"missing-file");if(exists)boundarySeed("");boundaryOffer();const auto bytes=boundaryRead(boundaryPath());empty=empty&&!bytes.empty()&&bytes.front()=='{'&&!boundaryLast(bytes).is_discarded()&&std::count(bytes.begin(),bytes.end(),'\n')==1;}
    check("missing-and-empty-files-do-not-gain-leading-blank-line",empty);
    reset(root,L"existing-lf");const auto withLf=complete+"\n";boundarySeed(withLf);boundaryOffer();auto bytes=boundaryRead(boundaryPath());
    check("existing-lf-boundary-is-preserved-without-extra-empty-row",bytes.compare(0,withLf.size(),withLf)==0&&bytes[withLf.size()]=='{'&&std::count(bytes.begin(),bytes.end(),'\n')==2);
    reset(root,L"existing-cr");const auto withCr=complete+"\r";boundarySeed(withCr);boundaryOffer();bytes=boundaryRead(boundaryPath());
    check("bare-cr-tail-keeps-bytes-and-gains-only-required-lf",boundarySeparated(bytes,withCr)&&bytes.compare(0,withCr.size()+1,withCr+"\n")==0);
    reset(root,L"binary-tail");const std::string binary={char(-1),char(0),'{','"','x','"',':'};boundarySeed(binary);boundaryOffer();
    check("nul-and-invalid-utf8-old-tail-remain-exact-with-readable-new-row",boundarySeparated(boundaryRead(boundaryPath()),binary));

    const json queued={{"generation",1ull},{"ready",true},{"revision",1u}};
    reset(root,L"read-sharing-refusal");boundarySeed(broken);auto path=boundaryPath();
    HANDLE reader=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE probe=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);const auto readError=GetLastError();
    const bool refusedRead=reader!=INVALID_HANDLE_VALUE&&probe==INVALID_HANDLE_VALUE&&readError==ERROR_SHARING_VIOLATION;if(probe!=INVALID_HANDLE_VALUE)CloseHandle(probe);
    bool writeAdmitted=false;{std::ofstream permitted(path,std::ios::binary|std::ios::app);writeAdmitted=permitted.good();}
    deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);if(reader!=INVALID_HANDLE_VALUE)CloseHandle(reader);
    const auto afterReadFailure=boundaryRead(path);deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);
    check("real-tail-read-sharing-refusal-preserves-bytes-and-reoffers-memo",refusedRead&&writeAdmitted&&afterReadFailure==broken&&boundarySeparated(boundaryRead(path),broken,"mirror-snapshot-queued"));

    reset(root,L"write-sharing-refusal");boundarySeed(broken);path=boundaryPath();
    HANDLE blocker=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    probe=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);const auto writeError=GetLastError();
    const bool refusedWrite=blocker!=INVALID_HANDLE_VALUE&&probe==INVALID_HANDLE_VALUE&&writeError==ERROR_SHARING_VIOLATION;if(probe!=INVALID_HANDLE_VALUE)CloseHandle(probe);
    deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);if(blocker!=INVALID_HANDLE_VALUE)CloseHandle(blocker);
    const auto afterWriteFailure=boundaryRead(path);deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);
    check("real-append-sharing-refusal-keeps-old-tail-and-next-original-call",refusedWrite&&afterWriteFailure==broken&&boundarySeparated(boundaryRead(path),broken,"mirror-snapshot-queued"));

    reset(root,L"repeat-no-path");deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);const auto pathCalls=boundaryPathQueries;
    for(unsigned i=0;i<5000;++i)deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);
    g_detail=true;deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);g_detail=false;
    check("5000-unchanged-queue-calls-do-not-reopen-or-query-path",boundaryPathQueries==pathCalls&&readRows(L"decky-sidebar.log").size()==1&&detailRows==1);

    reset(root,L"forced-receipts");boundarySeed(complete);ymcc::steamfailure::FailureContext context;context.actionId=31;context.operation=ymcc::steamfailure::Operation::Reconnect;
    const json trace={{"ok",false},{"steamFailure",ymcc::steamfailure::native(ymcc::steamfailure::Domain::WinHttp,12002,ymcc::steamfailure::SourceStage::Receive)}};
    deckySidebarDiagnostic("steam-recovery-trace",trace,true,&context);deckySidebarDiagnostic("steam-recovery-trace",trace,true,&context);const auto receipts=readRows(L"decky-sidebar.log");
    check("each-forced-failure-operation-stays-whole-after-legacy-no-lf",receipts.size()==3&&receipts[1]["actionId"]=="31"&&receipts[2]["actionId"]=="31");

    reset(root,L"rotation");const std::string oversized(static_cast<size_t>(ymcc::deckydiag::maxBytes)+1,'X');boundarySeed(oversized);boundaryOffer();
    check("original-rotation-preserves-backup-and-new-active-starts-clean",boundaryRead(fspath::path(boundaryPath().wstring()+L".1"))==oversized&&readRows(L"decky-sidebar.log").size()==1&&boundaryRead(boundaryPath()).front()=='{');
    reset(root,L"中文路径");const std::string unicode="{\"保留\":\"原始\"}";boundarySeed(unicode);boundaryOffer();
    check("unicode-path-and-original-utf8-history-remain-byte-exact",boundarySeparated(boundaryRead(boundaryPath()),unicode));
#endif
    GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);check("all-owned-readers-writers-and-sharing-handles-return",handlesBefore==handlesAfter);
    unsigned failed=0;for(const auto& row:checks)failed+=!row["pass"].get<bool>();
    std::cout<<json{{"selectedCase","decky-mode-health"},{"selectedPhase","jsonl-boundary"},{"checks",checks},{"failures",failed},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"originalPathCalls",boundaryPathQueries},{"actualFullWriter",true},{"actualBoundaryHelper",true},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<std::endl;return failed?3:0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 4;}}
#elif defined(YMCC_DECKY_LAST_COMMIT_ONLY)
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;
    fspath::create_directories(root);DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    static_assert(noexcept(std::declval<std::string&>()=std::declval<std::string&&>()));
    reset(root,L"last-signature-commit");const json detail={{"generation",1ull},{"ready",true},{"revision",11u}};
    deckySidebarDiagnostic("mirror-snapshot-queued",detail,false);
    const auto firstArm=fixtureArmAtDestruction;ymcc::testing::allocationFault::failAfter(-1);
    const auto first=readRows(L"decky-sidebar.log").size();
    deckySidebarDiagnostic("mirror-snapshot-queued",detail,false);ymcc::testing::allocationFault::failAfter(-1);
    const auto second=readRows(L"decky-sidebar.log").size();
    GetProcessHandleCount(GetCurrentProcess(),&after);
    check("successful-real-file-row-keeps-memo-without-allocating-second-signature",first==1&&second==1&&fixtureSuccessfulFlushes==1&&firstArm==0&&before==after);
    const bool pass=checks[0]["pass"].get<bool>();
    std::cout<<json{{"checks",checks},{"failures",pass?0:1},{"actualRowsBeforeRepeat",first},{"actualRowsAfterRepeat",second},{"actualSuccessfulFlushes",fixtureSuccessfulFlushes},{"firstPostFlushAllocationArm",firstArm},{"handlesBefore",before},{"handlesAfter",after},{"actualOfstreamBaseFlush",true},{"hardwareActions",0},{"productOrSteamActions",0}}.dump(2)<<std::endl;
    return pass?0:3;
}catch(const std::exception& error){ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<std::endl;return 4;}}
#else
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;
    fspath::create_directories(root);DWORD handlesBefore=0,handlesAfter=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
    const json queued={{"generation",1ull},{"ready",true},{"revision",11u}};
    const json healthy={{"elapsedMs",0u},{"reason",""},{"launchMode","bigpicture"},{"modeKnown",true},{"modeRunning",true},{"modeIdentityConfirmed",true},
        {"deckyBackendKnown",true},{"deckyBackendConnected",true},{"deckyClientKnown",true},{"deckyClientConnected",true},{"deckyHooksKnown",true},{"deckyHooksInstalled",true},
        {"deckyClientCheckingKnown",true},{"deckyClientChecking",false},{"steamPid",37u},{"steamBirth","400"},{"proofStage","registry-account"}};
    const auto invoke=[&](bool mode){if(mode)deckySidebarDiagnostic("steam-mode-read",healthy,true);else deckySidebarDiagnostic("mirror-snapshot-queued",queued,false);};
    const wchar_t* folders[]={L"queue-path",L"queue-time",L"mode-path",L"mode-time"};
    const char* labels[]={"queued-path-exception-does-not-consume-original-first-row","queued-timestamp-exception-does-not-consume-original-first-row","healthy-path-exception-does-not-consume-original-first-row","healthy-timestamp-exception-does-not-consume-original-first-row"};
    for(unsigned i=0;i<4;++i){
        reset(root,folders[i]);fixtureFault=(i&1)?FaultSite::timestamp:FaultSite::path;const auto before=fixtureFaults;
        invoke(i>=2);fixtureFault=FaultSite::none;ymcc::testing::allocationFault::failAfter(-1);
        const bool failedEmpty=readRows(L"decky-sidebar.log").empty();invoke(i>=2);invoke(i>=2);
        check(labels[i],fixtureFaults==before+1&&failedEmpty&&readRows(L"decky-sidebar.log").size()==1);
    }
#ifndef YMCC_DECKY_MEMO_EXCEPTION_OLD_ONLY
    reset(root,L"two-consecutive-exceptions");const auto faultsBefore=fixtureFaults;
    for(unsigned i=0;i<2;++i){fixtureFault=FaultSite::timestamp;invoke(false);fixtureFault=FaultSite::none;ymcc::testing::allocationFault::failAfter(-1);}
    const bool twiceEmpty=readRows(L"decky-sidebar.log").empty();invoke(false);invoke(false);
    check("two-original-failed-calls-rearm-then-next-success-restores-one-row",twiceEmpty&&fixtureFaults==faultsBefore+2&&readRows(L"decky-sidebar.log").size()==1);

    bool ordinary=true;for(const bool mode:{false,true}){
        reset(root,mode?L"normal-mode-open-failure":L"normal-queue-open-failure");g_testRoot+=L"\\missing";invoke(mode);
        ordinary=ordinary&&readRows(L"decky-sidebar.log").empty();fspath::create_directories(g_testRoot);invoke(mode);invoke(mode);
        ordinary=ordinary&&readRows(L"decky-sidebar.log").size()==1;
    }
    check("both-original-nonexception-file-failure-paths-still-reoffer-once",ordinary);

    reset(root,L"queue-repeat-detail");invoke(false);for(unsigned i=0;i<1000;++i)invoke(false);
    const bool residentOnce=readRows(L"decky-sidebar.log").size()==1&&detailRows==0;
    g_detail=true;invoke(false);invoke(false);g_detail=false;
    check("queued-repeats-remain-one-resident-and-original-on-only-detail",residentOnce&&readRows(L"decky-sidebar.log").size()==1&&detailRows==2);

    reset(root,L"healthy-repeat");for(unsigned i=0;i<1000;++i)invoke(true);
    check("1000-original-healthy-cache-observations-still-produce-one-row",readRows(L"decky-sidebar.log").size()==1);

    reset(root,L"other-event-exception");fixtureFault=FaultSite::timestamp;const auto otherBefore=fixtureFaults;
    deckySidebarDiagnostic("runtime",{{"phase","starting"}},false);fixtureFault=FaultSite::none;ymcc::testing::allocationFault::failAfter(-1);
    deckySidebarDiagnostic("runtime",{{"phase","starting"}},false);deckySidebarDiagnostic("runtime",{{"phase","starting"}},false);
    check("original-nonmemo-text-dedup-commits-only-successful-row",fixtureFaults==otherBefore+1&&readRows(L"decky-sidebar.log").size()==1);

    reset(root,L"failure-operation-receipts");ymcc::steamfailure::FailureContext context;context.actionId=31;context.operation=ymcc::steamfailure::Operation::Reconnect;
    const json trace={{"ok",false},{"steamFailure",ymcc::steamfailure::native(ymcc::steamfailure::Domain::WinHttp,12002,ymcc::steamfailure::SourceStage::Receive)}};
    deckySidebarDiagnostic("steam-recovery-trace",trace,true,&context);deckySidebarDiagnostic("steam-recovery-trace",trace,true,&context);
    const auto traces=readRows(L"decky-sidebar.log");
    check("explicit-original-failure-operation-receipts-are-not-deduplicated",traces.size()==2&&traces[0]["actionId"]=="31"&&traces[1]["actionId"]=="31");

    reset(root,L"power-edge");invoke(true);++g_powerGeneration;invoke(true);invoke(true);
    check("original-power-generation-keeps-new-healthy-boundary-once",readRows(L"decky-sidebar.log").size()==2);

    reset(root,L"unknown-health");auto unknown=healthy;unknown["deckyHooksKnown"]="unknown";
    deckySidebarDiagnostic("steam-mode-read",unknown,true);deckySidebarDiagnostic("steam-mode-read",unknown,true);
    check("unknown-health-still-keeps-every-original-forced-receipt",readRows(L"decky-sidebar.log").size()==2);

    reset(root,L"parallel-after-fault");fixtureFault=FaultSite::timestamp;invoke(false);fixtureFault=FaultSite::none;ymcc::testing::allocationFault::failAfter(-1);
    std::vector<std::thread> threads;for(unsigned i=0;i<4;++i)threads.emplace_back([&]{for(unsigned j=0;j<50;++j)invoke(false);});for(auto& thread:threads)thread.join();
    check("existing-log-mutex-serializes-four-callers-after-exception",readRows(L"decky-sidebar.log").size()==1);
#endif
    GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);check("all-own-log-files-and-test-threads-return-without-product-actions",handlesBefore==handlesAfter);
    unsigned failures=0;for(const auto& row:checks)failures+=!row["pass"].get<bool>();
    std::cout<<json{{"selectedCase","decky-mode-health"},{"selectedPhase","memo-exception"},{"checks",checks},{"failures",failures},{"observedFixtureAllocationRefusals",fixtureFaults},{"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"actualFullDeckyWriter",true},{"actualRotationAndProjector",true},{"productOrSteamActions",0},{"hardwareActions",0}}.dump(2)<<std::endl;
    return failures?3:0;
}catch(const std::exception& error){fixtureFault=FaultSite::none;ymcc::testing::allocationFault::failAfter(-1);std::cerr<<error.what()<<std::endl;return 4;}}

#endif
