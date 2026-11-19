static json snapshotWorkSnapshots=json::array();
static unsigned long long workCalls(const json& work,const char* group){return work[group]["enteredCalls"].get<unsigned long long>();}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 2;const fspath::path root(argv[1]);if(fspath::exists(root))return 3;fspath::create_directories(root);
 const auto input=root/L"native-lifecycle.log";put(input,"{\"event\":\"snapshot-work-owned\",\"time\":\"t\"}\n");
 const auto h1=sha256File(input.wstring()),h2=sha256File(input.wstring());const auto c1=productionRunCapture(L"\"C:\\Windows\\System32\\tar.exe\" --version",10000),c2=productionRunCapture(L"\"C:\\Windows\\System32\\tar.exe\" --version",10000);
 if(h1.size()!=64||h1!=h2||!c1.ran||c1.exitCode||!c2.ran||c2.exitCode)throw std::runtime_error("owned hash/tar initialization failed");DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
 const auto manifest=root/L"manifest"/L"export-manifest.txt";put(manifest,"owned-manifest\n");
 ymcc::logexport::SteamLogs steam;steam.root=root/L"owned-steam";steam.status="discovered";steam.source="fixture-owned-path";
 const auto steamInput=steam.root/L"logs"/L"steam-source.log";put(steamInput,"owned-steam-bytes\n");steam.files={steamInput};
 const auto initialized=createTarArchiveWithRetry((root/L"same-path-initialization.zip").wstring(),{input.wstring()},manifest.parent_path().wstring(),manifest.filename().wstring(),&steam);
 if(!initialized.ok)throw std::runtime_error("same-path owned archive initialization failed");const auto coldBeforeSamePath=before;GetProcessHandleCount(GetCurrentProcess(),&before);
 snapshotAttemptObserver=[&](const wchar_t* directory){std::ifstream f(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json");json v;f>>v;snapshotWorkSnapshots.push_back(v);};
 snapshotWorkDelay=true;const auto packed=createTarArchiveWithRetry((root/L"success.zip").wstring(),{input.wstring()},manifest.parent_path().wstring(),manifest.filename().wstring(),&steam);snapshotWorkDelay=false;snapshotAttemptObserver={};
 const auto timing=archiveAttemptTimingEvidence(packed);const auto& row=timing["attempts"][0];const auto& work=row["snapshotWork"];
 check("original-archive-success-and-source-bytes-preserved",packed.ok&&packed.attempts==1&&sha256File(input.wstring())==h1);
 check("four-original-source-snapshot-groups-observed",work.is_object()&&workCalls(work,"rootCopy")==1&&workCalls(work,"rootInspection")==1&&workCalls(work,"manifestCopy")==1&&workCalls(work,"steamCopy")==1);
#ifdef YMCC_SNAPSHOT_WORK_OLD_ONLY
 GetProcessHandleCount(GetCurrentProcess(),&after);check("owned-original-handles-returned",before==after);unsigned failed=0;for(const auto& v:checks)failed+=!v["pass"].get<bool>();std::cout<<json{{"checks",checks},{"failures",failed},{"handlesBefore",before},{"handlesAfter",after},{"successTiming",timing}}.dump(2)<<'\n';return failed?2:0;
#else
 const auto& leaders=work["rootInspectionLeaders"];
 check("actual archive first inspection rank matches its staged source ordinal",leaders["limit"]==8&&leaders["entries"].size()==1&&
     leaders["entries"][0]["sourceOrdinal"]==work["rootInspection"]["maxSourceOrdinal"]&&
     leaders["entries"][0]["elapsedMs"]==work["rootInspection"]["maxElapsedMs"]);
 ymcc::logexport::SnapshotInspectionLeaders ordered;
 evidenceCount=0;evidenceTrack=true;
 for(unsigned i=0;i<12;++i)ordered.add(i%2?5:0,i);
 ordered.add(8,12);
 evidenceTrack=false;
 bool orderedBounded=ordered.count==8&&ordered.entries[0].sourceOrdinal==12&&ordered.entries[7].sourceOrdinal==0;
 for(size_t i=1;i<7;++i)orderedBounded=orderedBounded&&ordered.entries[i].sourceOrdinal==2*i-1&&ordered.entries[i].elapsedMs==5;
 check("rank stays bounded, keeps first ties, measures zero and uses no heap",orderedBounded&&evidenceCount==0);
 check("controlled-delays-stay-with-the-original-copy-and-inspection-group",work["rootCopy"]["elapsedMs"].get<unsigned long long>()>=16&&work["rootInspection"]["elapsedMs"].get<unsigned long long>()>=32&&work["manifestCopy"]["elapsedMs"].get<unsigned long long>()>=48&&work["steamCopy"]["elapsedMs"].get<unsigned long long>()>=64);
 unsigned long long sum=0;for(const auto* name:{"rootCopy","rootInspection","manifestCopy","steamCopy"})sum+=work[name]["elapsedMs"].get<unsigned long long>();
 check("measured-call-groups-fit-source-snapshot-containing-step",sum<=row["completedStepElapsedMs"]["sourceSnapshot"].get<unsigned long long>());
 const auto& snap=snapshotWorkSnapshots[0]["archiveAttemptTiming"]["attempts"][0];check("same-attempt-snapshot-retains-finished-copy-work-before-current-index",snapshotWorkSnapshots.size()==1&&snap["snapshotWork"]==work&&snap["attemptBodyReturned"]==false&&snap["indexWork"]["indexEntered"]==false&&snap["totalElapsedMs"].is_null());
 check("original-index-second-inspection-remains-separate",row["indexWork"]["inspection"]["enteredCalls"].get<unsigned long long>()>1&&workCalls(work,"rootInspection")==1);
 const auto early=createTarArchiveWithRetry((root/L"missing.zip").wstring(),{(root/L"absent.log").wstring()});const auto earlyTiming=archiveAttemptTimingEvidence(early);bool earlyWork=!early.ok&&early.attempts==2;
 for(const auto& attempt:earlyTiming["attempts"]){const auto& w=attempt["snapshotWork"];earlyWork=earlyWork&&workCalls(w,"rootCopy")==1&&w["rootCopy"]["returnedCalls"]==1&&workCalls(w,"rootInspection")==0&&w["rootInspection"]["elapsedMs"].is_null()&&workCalls(w,"manifestCopy")==0&&workCalls(w,"steamCopy")==0;}
 check("failed-copy-return-is-observed-with-unentered-inspection-null",earlyWork);
 fspath::create_directories(root/L"missing-manifest");
 const auto manifestFail=createTarArchiveWithRetry((root/L"manifest-fail.zip").wstring(),{input.wstring()},(root/L"missing-manifest").wstring(),L"export-manifest.txt");const auto manifestTiming=archiveAttemptTimingEvidence(manifestFail);
 check("actual-manifest-copy-failure-keeps-original-error",!manifestFail.ok&&manifestFail.reason=="snapshot-manifest-copy-failed"&&manifestFail.win32Error==ERROR_FILE_NOT_FOUND&&workCalls(manifestTiming["attempts"][0]["snapshotWork"],"manifestCopy")==1);
 steam.files={steamInput,steam.root/L"logs"/L"missing.log",root/L"outside-steam.log"};const auto partial=createTarArchiveWithRetry((root/L"partial-steam.zip").wstring(),{input.wstring()},{},{},&steam);const auto partialTiming=archiveAttemptTimingEvidence(partial);const auto& p=partialTiming["attempts"][0]["snapshotWork"];
 check("optional-steam-false-return-and-unsafe-path-retain-original-selection",partial.ok&&partial.steamCopied==1&&partial.steamSkipped==2&&workCalls(p,"steamCopy")==2&&p["steamCopy"]["returnedCalls"]==2&&workCalls(p,"manifestCopy")==0&&p["manifestCopy"]["maxSourceOrdinal"].is_null());
 failTarChildren=1;const auto retry=createTarArchiveWithRetry((root/L"retry.zip").wstring(),{input.wstring()});const auto retryTiming=archiveAttemptTimingEvidence(retry);
 check("original-two-attempt-work-slots-do-not-accumulate-across-retry",retry.ok&&retry.attempts==2&&workCalls(retryTiming["attempts"][0]["snapshotWork"],"rootCopy")==1&&workCalls(retryTiming["attempts"][1]["snapshotWork"],"rootInspection")==1);
 const auto emptyTiming=ymcc::logexport::snapshotWorkEvidence(ymcc::logexport::SnapshotWorkEvidence{});bool emptyWork=true;for(const auto* group:{"rootCopy","rootInspection","manifestCopy","steamCopy"})emptyWork=emptyWork&&workCalls(emptyTiming,group)==0&&emptyTiming[group]["elapsedMs"].is_null();check("unentered-work-groups-never-invent-zero-duration",emptyWork);
 ymcc::logexport::IndexCallWork scalar;SetLastError(71);{ymcc::logexport::SnapshotCallWorkSpan span(&scalar,9);check("span-entry-preserves-original-last-error",GetLastError()==71);SetLastError(83);span.returned();check("span-return-preserves-original-api-last-error",GetLastError()==83);}check("span-completion-and-source-ordinal-exact",scalar.enteredCalls==1&&scalar.returnedCalls==1&&scalar.exceptionalCalls==0&&scalar.maxAppendOrdinal==9);
 try{ymcc::logexport::SnapshotCallWorkSpan span(&scalar,10);SetLastError(91);throw 1;}catch(int){check("exceptional-span-preserves-error-and-does-not-report-return",GetLastError()==91&&scalar.enteredCalls==2&&scalar.returnedCalls==1&&scalar.exceptionalCalls==1);}
 evidenceTrack=true;evidenceCount=0;evidenceFailAt=0;{ymcc::logexport::IndexCallWork local;ymcc::logexport::SnapshotCallWorkSpan span(&local,0);span.returned();}{ymcc::logexport::SnapshotCallWorkSpan span(nullptr,0);span.returned();}evidenceTrack=false;check("fixed-observer-and-null-observer-have-no-new-allocation",evidenceCount==0);
 evidenceTrack=true;evidenceCount=0;evidenceFailAt=0;auto measured=archiveAttemptTimingEvidence(packed);evidenceTrack=false;const auto census=evidenceCount;bool safe=measured==timing&&census>0&&census<2048;
 for(unsigned long long i=1;i<=census;++i){evidenceCount=0;evidenceFailAt=i;evidenceFault=false;evidenceTrack=true;auto omitted=archiveAttemptTimingEvidence(packed);evidenceTrack=false;evidenceFailAt=0;safe=safe&&evidenceFault&&omitted.is_null();}
 check("every-optional-timing-allocation-failure-keeps-original-result",safe&&packed.ok);
 const auto active=root/L"owned-monitor"/L"monitor-detail.log", alias=active.parent_path()/L"monitor-detail.log.1", unrelated=root/L"other-monitor"/active.filename();
 put(active,"{}\n{");put(alias,"{}\n");put(unrelated,"{\"other\":true}\n");
 unsigned boundaryCalls=0;const std::vector<ArchiveWriterSource> monitor{{active.wstring(),[&](const auto& capture){++boundaryCalls;capture();return true;},"monitorWriterBoundary"}};
 json monitorReceipt;
 snapshotAttemptObserver=[&](const wchar_t* directory){std::ifstream f(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json");f>>monitorReceipt;};
 const auto monitorArchive=createTarArchiveWithRetry((root/L"monitor-boundary.zip").wstring(),{unrelated.wstring(),alias.wstring(),active.wstring()},{},{},&steam,nullptr,&monitor);
 snapshotAttemptObserver={};
 bool exactMonitor=monitorArchive.ok&&monitorArchive.attempts==1&&boundaryCalls==1&&monitorReceipt.contains("files")&&monitorReceipt["files"].size()==3;
 if(exactMonitor){const auto& files=monitorReceipt["files"];exactMonitor=!files[0]["copyObservation"].contains("monitorWriterBoundary")&&!files[1]["copyObservation"].contains("monitorWriterBoundary")&&files[2]["copyObservation"]["monitorWriterBoundary"]=="captured-under-writer-lock"&&files[2]["partialOrInvalidTrailingRecord"]==true;}
 check("ordinary archive synchronizes only the exact active path and preserves an earlier partial tail",exactMonitor);
 const std::vector<ArchiveWriterSource> missingSource{{(root/L"missing"/active.filename()).wstring(),monitor.front().lockAndFlush,"monitorWriterBoundary"}};
 const auto missingMonitor=createTarArchiveWithRetry((root/L"missing-monitor.zip").wstring(),{missingSource.front().path},{},{},nullptr,nullptr,&missingSource);
 const auto missingEvidence=archiveSnapshotCopyFailureEvidence(missingMonitor);const auto& failures=missingEvidence["failures"];
 check("active monitor open failure preserves both bounded retry receipts",!missingMonitor.ok&&missingMonitor.attempts==2&&boundaryCalls==3&&failures.size()==2&&failures[0]["copyObservation"]["monitorWriterBoundary"]=="source-open-failed"&&failures[1]["copyObservation"]["monitorWriterBoundary"]=="source-open-failed");
 const auto tails=root/L"tails";const auto tailsManifest=tails/L"steam-input-export-manifest.json";
 json tailRows=json::array();std::vector<std::wstring> tailPaths{tailsManifest.wstring()};
 ymcc::logexport::SteamLogs mirrorSteam;mirrorSteam.root=root/L"mirror";mirrorSteam.status="discovered";
 const auto addPair=[&](const wchar_t* tail,const wchar_t* source,const char* left,const char* right,bool truncated=false){
    const auto a=tails/tail,b=mirrorSteam.root/L"logs"/source;put(a,left);if(right)put(b,right);
    tailPaths.push_back(a.wstring());mirrorSteam.files.push_back(b);
    tailRows.push_back({{"file",W2U(tail)},{"sourceFile",W2U(source)},{"available",true},{"saved",true},
       {"copiedBytes",std::string_view(left).size()},{"originalBytes",std::string_view(left).size()+truncated},
       {"limitBytes",2097152},{"truncated",truncated}});
 };
 addPair(L"steam-gameprocess.log",L"gameprocess_log.txt","original\0","original\0");
 addPair(L"steam-controller.log",L"controller.txt","other","delta");
 addPair(L"steam-controller-ui.log",L"controller_ui.txt","clip","clip",true);
 addPair(L"steam-gameoverlay-renderer.log",L"gameoverlay_renderer.txt","fallback",nullptr);
 put(tailsManifest,json{{"schema",1},{"scope","steam"},{"maxBytesPerFile",2097152},{"files",tailRows}}.dump());
 json aliasSnapshot;
 snapshotAttemptObserver=[&](const wchar_t* directory){std::ifstream f(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json");f>>aliasSnapshot;};
 const auto exactAlias=createTarArchiveWithRetry((root/L"steam-alias.zip").wstring(),tailPaths,{},{},&mirrorSteam,nullptr,nullptr,&tailPaths);
 snapshotAttemptObserver={};
 const auto& retainedAliases=aliasSnapshot["steamTailAliases"]["entries"];
 check("ordinary Steam archive omits only the exact full staged tail and records its digest",exactAlias.ok&&exactAlias.attempts==1&&exactAlias.steamCopied==3&&exactAlias.steamSkipped==1&&retainedAliases.size()==1&&retainedAliases[0]["tail"]=="steam-gameprocess.log"&&retainedAliases[0]["sha256"]==sha256File((tails/L"steam-gameprocess.log").wstring()));
 const json batchOperation={{"schema",1},{"id","37-42-3"},{"writerPid",37},{"writerProcessCreation100ns","9007199254740993"},
     {"ordinal","3"},{"startedTickMs","42"},{"scope","admitted-ordinary-log-export;diagnostic-correlation-only"}};
 std::vector<std::wstring> batchFiles;
 std::string largeLines;for(unsigned i=0;i<17000;++i)largeLines+="{\"time\":\"fixed-clock\"}\n";
 for(unsigned i=0;i<9;++i){
    const auto file=root/L"batch"/std::to_wstring(i)/(i<2?L"native-lifecycle.log":L"other.log");
    if(i<2)put(file,largeLines+std::to_string(i));else put(file,std::to_string(i));
    batchFiles.push_back(file.wstring());
 }
 json batchReceipt;snapshotAttemptObserver=[&](const wchar_t* directory){
     std::ifstream f(fspath::path(directory)/L"snapshot-evidence"/L"log-export-snapshot.json");f>>batchReceipt;};
 ymcc::logexport::SteamLogs batchSteam;
 const auto batchArchive=createTarArchiveWithRetry((root/L"batch.zip").wstring(),batchFiles,{},{},&batchSteam,&batchOperation);
 snapshotAttemptObserver={};
 const auto batchTiming=archiveAttemptTimingEvidence(batchArchive);
 const auto& batchWork=batchTiming.at("attempts").at(0).at("snapshotWork");
 bool batchOrder=batchArchive.ok&&batchArchive.attempts==1&&batchReceipt.at("files").size()==batchFiles.size();
 for(size_t i=0;batchOrder&&i<batchFiles.size();++i){
     const auto& row=batchReceipt["files"][i];batchOrder=row["listIndex"]==i&&row["entry"]==W2U(fspath::path(batchFiles[i]).filename().wstring());
 }
 check("actual parallel batch keeps root order, duplicate names and pinned index reuse",batchOrder&&
     batchWork.at("rootInspectionBatch").at("mode")=="two-workers"&&workCalls(batchWork,"rootInspection")==9&&
     batchTiming.at("attempts").at(0).at("indexWork").at("reusedRootInspections")==2&&
     batchReceipt.at("archiveAttemptTiming").at("attempts").at(0).at("snapshotWork")==batchWork);
 batchFiles[4]=(root/L"missing-batch.log").wstring();
 const auto interrupted=createTarArchiveWithRetry((root/L"interrupted-batch.zip").wstring(),batchFiles,{},{},nullptr,&batchOperation);
 const auto interruptedTiming=archiveAttemptTimingEvidence(interrupted);
 bool copyFailureRetained=!interrupted.ok&&interrupted.attempts==2&&interrupted.reason=="snapshot-copy-failed"&&interrupted.win32Error==ERROR_FILE_NOT_FOUND;
 for(const auto& attempt:interruptedTiming["attempts"]){const auto& partial=attempt["snapshotWork"];
     copyFailureRetained=copyFailureRetained&&partial["rootInspectionBatch"]["mode"]=="inline-batch"&&
         workCalls(partial,"rootCopy")==5&&workCalls(partial,"rootInspection")==4&&!attempt["completedStepElapsedMs"].contains("tarProcess");}
 check("copy failure checks only completed files and keeps original error for both attempts",copyFailureRetained);
 GetProcessHandleCount(GetCurrentProcess(),&after);check("owned-original-files-hash-and-tar-handles-return-to-warm-baseline",before==after);unsigned failed=0;for(const auto& v:checks)failed+=!v["pass"].get<bool>();
 std::cout<<json{{"checks",checks},{"failures",failed},{"coldBeforeSamePathHandles",coldBeforeSamePath},{"handlesBefore",before},{"handlesAfter",after},{"successTiming",timing},{"earlyTiming",earlyTiming},{"manifestTiming",manifestTiming},{"partialTiming",partialTiming},{"retryTiming",retryTiming},{"emptyTiming",emptyTiming},{"snapshots",snapshotWorkSnapshots},{"monitorReceipt",monitorReceipt},{"monitorFailureReceipt",missingEvidence},{"optionalAllocationCensus",census},{"workBytes",sizeof(ymcc::logexport::SnapshotWorkEvidence)},{"actualOriginalArchiveAndTar",true},{"onlyOwnedInputs",true}}.dump(2)<<'\n';return failed?2:0;
#endif
}catch(const std::exception& e){evidenceTrack=false;evidenceFailAt=0;std::cerr<<e.what()<<'\n';return 3;}}
