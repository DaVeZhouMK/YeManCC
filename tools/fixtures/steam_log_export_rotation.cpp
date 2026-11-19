// Concatenated after the original export PRELUDE and exact main archive helper.
#include "input_capture_log_evidence.h"
#include <thread>
static std::mutex g_domainLogMx;
static ymcc::logging::BufferedDomainLog g_domainLogStreams[4];
/* ACTUAL_DOMAIN_LOG_SOURCE_BOUNDARY */
static std::mutex g_inputCaptureMx;
static std::ofstream g_inputCaptureLog;
static ymcc::logging::CaptureLogFailureEvidence g_inputCaptureLogFailureEvidence;
/* ACTUAL_INPUT_CAPTURE_SOURCE_BOUNDARY */
static std::mutex g_tdpObservationMx;
static ymcc::logging::BufferedDomainLog g_tdpObservationStreams[2];
/* ACTUAL_TDP_SOURCE_BOUNDARY */

int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;const fspath::path root(argv[1]);
    if(fspath::exists(root))return 3;fspath::create_directories(root);
    const auto active=root/L"input-capture.jsonl";
    put(active,"{\"event\":\"retained-current\"}\n");
    std::vector<std::wstring> discovered{active.wstring()};
    for(const wchar_t* suffix:{L".1",L".prev",L".previous"}) {
        const auto alias=active.wstring()+suffix;put(alias,"rotated-before-discovery\n");
        discovered.push_back(alias);
        check("fixture-discovered-rotation-removed-before-source-open",fspath::remove(alias));
    }
    const auto packed=createTarArchiveWithRetry((root/L"rotation.zip").wstring(),discovered);
    check("vanished-discovered-history-preserves-current-and-one-archive-attempt",
        packed.ok&&packed.attempts==1&&packed.snapshotBoundaryWarnings==3&&tarCalls==1&&packed.cleanupOk);
#ifdef YMCC_ROTATION_OLD_CONTROL
    unsigned failures=0;for(const auto& row:checks)if(!row["pass"].get<bool>())++failures;
    std::cout<<json({{"checks",checks},{"failures",failures},{"necessaryOldFailure",true},
        {"result",{{"ok",packed.ok},{"attempts",packed.attempts},{"reason",packed.reason},{"error",packed.win32Error}}}}).dump(2)<<'\n';
    return failures?2:0;
#else
    using namespace ymcc::logexport;
    SnapshotCopyEvidence observation;observation.stage="source-open";
    check("only-same-directory-discovered-base-makes-history-optional",
        !disappearedRotatedSnapshot((root/L"other"/L"input-capture.jsonl.prev").wstring(),discovered,2,observation));
    for(const DWORD error:{DWORD{ERROR_ACCESS_DENIED},DWORD{ERROR_SHARING_VIOLATION},DWORD{ERROR_OPERATION_ABORTED}})
        check("permission-sharing-and-cancel-errors-are-not-missing-history",
            !disappearedRotatedSnapshot(discovered[1],discovered,error,observation));
    observation.stage="source-read";
    check("missing-error-after-open-does-not-hide-read-failure",!disappearedRotatedSnapshot(discovered[1],discovered,2,observation));
    observation.stage="source-open";observation.destinationCreated=true;
    check("destination-created-is-not-a-before-snapshot-disappearance",!disappearedRotatedSnapshot(discovered[1],discovered,2,observation));
    observation.destinationCreated=false;observation.sourceSizeKnown=true;
    check("observed-source-size-prevents-missing-alias-shortcut",!disappearedRotatedSnapshot(discovered[1],discovered,2,observation));
    observation.sourceSizeKnown=false;
    check("missing-current-log-remains-mandatory",!disappearedRotatedSnapshot(active.wstring(),discovered,2,observation));
    check("missing-unknown-suffix-remains-mandatory",!disappearedRotatedSnapshot(active.wstring()+L".backup",discovered,2,observation));
    const auto mandatory=root/L"required.log";
    const auto failed=createTarArchiveWithRetry((root/L"mandatory.zip").wstring(),{mandatory.wstring()});
    check("real-missing-current-source-still-fails-two-attempts-with-original-error",
        !failed.ok&&failed.attempts==2&&failed.reason=="snapshot-copy-failed"&&failed.win32Error==2&&
        archiveSnapshotCopyFailureEvidence(failed)["retainedFailures"]==2&&!fspath::exists(root/L"mandatory.zip"));
    const auto locked=active.wstring()+L".prev";put(locked,"locked-history\n");
    HANDLE held=CreateFileW(locked.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check("private-rotation-sharing-control-handle-opened",held!=INVALID_HANDLE_VALUE);
    const auto denied=createTarArchiveWithRetry((root/L"denied.zip").wstring(),{active.wstring(),locked});
    if(held!=INVALID_HANDLE_VALUE)check("private-sharing-control-handle-returned",CloseHandle(held)!=FALSE);
    check("real-rotation-sharing-denial-still-fails-and-retains-copy-observation",
        !denied.ok&&denied.attempts==2&&denied.win32Error==ERROR_SHARING_VIOLATION&&
        archiveSnapshotCopyFailureEvidence(denied)["retainedFailures"]==2&&!fspath::exists(root/L"denied.zip"));
    const auto withoutBase=createTarArchiveWithRetry((root/L"unpaired.zip").wstring(),{(root/L"unpaired.log.prev").wstring()});
    check("real-unpaired-history-source-still-fails",!withoutBase.ok&&withoutBase.attempts==2&&withoutBase.win32Error==2);

    // Inject rotation after the real source boundary releases the writer lock.
    const auto gamepad=root/L"virtual-gamepad.log", previous=root/L"virtual-gamepad.log.1";
    const auto archivedWriter=root/L"writer-rotated.log", monitor=root/L"monitor-detail.log";
    const std::string firstRow=R"({"time":"2026-11-18T15:59:00.000","event":"input-capture-tick-cost"})";
    const std::string pendingRow=R"({"time":"2026-11-18T15:59:00.001","event":"input-idle-window"})";
    const std::string historicalTail="{\"prior-partial\":";
    put(previous,historicalTail);put(monitor,"{}\n");
    auto& stream=g_domainLogStreams[1];
    const auto noRotate=[](const std::wstring&,unsigned long long){};
    check("real-virtual-writer-has-an-unflushed-row",stream.append(gamepad.wstring(),firstRow,2000,false,512000,noRotate)&&
        stream.append(gamepad.wstring(),pendingRow,2001,false,512000,noRotate)&&stream.pendingRows()==1);
    const ymcc::logexport::SnapshotSourceSync gamepadBoundary=/* ACTUAL_VIRTUAL_GAMEPAD_SOURCE_BOUNDARY */;
    const auto captureDir=root/L"capture";
    fspath::create_directory(captureDir);
    const auto captureLog=captureDir/L"input-capture.jsonl";
    const auto captureHistory=captureDir/L"input-capture.jsonl.prev";
    const auto captureRotated=captureDir/L"capture-rotated.jsonl";
    const std::string captureRow="{\"kind\":\"sample\",\"t\":17}\n";
    put(captureHistory,historicalTail);
    g_inputCaptureLog.open(captureLog,std::ios::binary);
    g_inputCaptureLog<<captureRow;
    check("real-capture-stream-has-buffered-bytes-before-the-boundary",
        g_inputCaptureLog.good()&&fspath::file_size(captureLog)==0);
    const auto tdpLog=root/L"tdp.log",tdpDetailLog=root/L"tdp-detail.log";
    const auto tdpHistory=root/L"tdp-detail.log.1",tdpRotated=root/L"tdp-rotated.log";
    const std::string tdpSeed=R"({"event":"tdp.lifecycle"})";
    const std::string tdpResident=R"({"event":"tdp.failed"})";
    const std::string tdpDetail=R"({"event":"tdp.backend-detail"})";
    auto& residentStream=g_tdpObservationStreams[0];
    auto& detailStream=g_tdpObservationStreams[1];
    check("both-tdp-streams-start-with-pending-data",
        residentStream.append(tdpLog.wstring(),tdpSeed,2000,false,512000,noRotate)&&
        residentStream.append(tdpLog.wstring(),tdpResident,2001,false,512000,noRotate)&&
        detailStream.append(tdpDetailLog.wstring(),tdpSeed,2000,false,512000,noRotate)&&
        detailStream.append(tdpDetailLog.wstring(),tdpDetail,2001,false,512000,noRotate)&&
        residentStream.pendingRows()==1&&detailStream.pendingRows()==1);
    put(tdpHistory,historicalTail);
    unsigned gamepadCalls=0,monitorCalls=0,captureCalls=0;
    unsigned tdpCalls=0,tdpDetailCalls=0;
    bool writerUnlocked=false,captureWriterUnlocked=false,tdpOnlyResident=false,tdpWriterUnlocked=false;
    const std::vector<ArchiveWriterSource> writers{
        {monitor.wstring(),[&](const auto& capture){++monitorCalls;capture();return true;},"monitorWriterBoundary"},
        {gamepad.wstring(),[&](const auto& capture){
            ++gamepadCalls;const bool flushed=gamepadBoundary(capture);
            std::unique_lock<std::mutex> lock(g_domainLogMx,std::try_to_lock);
            if(lock){
                writerUnlocked=true;stream.close();fspath::rename(gamepad,archivedWriter);
                put(gamepad,"{\"later-rotation\":true}\n");
            }
            return flushed;
        },"virtualGamepadWriterBoundary"},
        {captureLog.wstring(),[&](const auto& capture){
            ++captureCalls;
            const bool flushed=inputCaptureExportSourceBoundary(capture);
            std::thread rotate([&]{
                std::unique_lock<std::mutex> lock(g_inputCaptureMx,std::try_to_lock);
                if(!lock)return;
                captureWriterUnlocked=true;
                g_inputCaptureLog.close();fspath::rename(captureLog,captureRotated);
                put(captureLog,"{\"later-capture\":true}\n");
            });
            rotate.join();
            return flushed;
        },"inputCaptureWriterBoundary"},
        {tdpLog.wstring(),[&](const auto& capture){
            ++tdpCalls;
            const bool flushed=tdpObservationExportSourceBoundary(false,capture);
            std::lock_guard<std::mutex> lock(g_tdpObservationMx);
            tdpOnlyResident=residentStream.pendingRows()==0&&detailStream.pendingRows()==1;
            return flushed;
        },"tdpWriterBoundary"},
        {tdpDetailLog.wstring(),[&](const auto& capture){
            ++tdpDetailCalls;
            const bool flushed=tdpObservationExportSourceBoundary(true,capture);
            std::thread rotate([&]{
                std::unique_lock<std::mutex> lock(g_tdpObservationMx,std::try_to_lock);
                if(!lock)return;
                detailStream.close();
                std::error_code ec;
                fspath::rename(tdpDetailLog,tdpRotated,ec);
                if(ec)return;
                put(tdpDetailLog,"{\"later-tdp-detail\":true}\n");
                tdpWriterUnlocked=true;
            });
            rotate.join();
            return flushed;
        },"tdpDetailWriterBoundary"},
    };
    put(root/L"expected-virtual.log",firstRow+'\n'+pendingRow+'\n');
    put(root/L"expected-capture.jsonl",captureRow);
    put(root/L"expected-tdp.log",tdpSeed+'\n'+tdpResident+'\n');
    put(root/L"expected-tdp-detail.log",tdpSeed+'\n'+tdpDetail+'\n');
    const auto boundaryZip=root/L"writer-boundary.zip";
    const auto boundaryArchive=createTarArchiveWithRetry(boundaryZip.wstring(),
        {gamepad.wstring(),previous.wstring(),monitor.wstring(),captureLog.wstring(),captureHistory.wstring(),
         tdpLog.wstring(),tdpDetailLog.wstring(),tdpHistory.wstring()}, {}, {}, nullptr, nullptr, &writers);
    check("active-callbacks-ran-once-and-virtual-writer-unlocked-before-copy",writerUnlocked&&gamepadCalls==1&&monitorCalls==1);
    check("rotating-virtual-source-and-partial-history-still-produce-one-archive",
        boundaryArchive.ok&&boundaryArchive.attempts==1&&boundaryArchive.cleanupOk&&boundaryArchive.snapshotBoundaryWarnings==3);
    check("tdp-lanes-flush-separately-and-history-does-not-run-the-local-writer-boundary",
        tdpOnlyResident&&tdpWriterUnlocked&&tdpCalls==1&&tdpDetailCalls==1);
    check("active-capture-alone-runs-the-boundary-and-unlocks-before-rotation",
        captureWriterUnlocked&&captureCalls==1&&fspath::file_size(captureRotated)==captureRow.size());
    const SnapshotSourceSync failedFlush=inputCaptureExportSourceBoundary;
    g_inputCaptureLog.open(captureLog,std::ios::binary|std::ios::app);
    g_inputCaptureLog.setstate(std::ios::failbit);
    DWORD copyError=0;
    const auto preserved=root/L"capture-flush-failed.jsonl";
    const bool copied=snapshotSharedLog(captureLog,preserved,copyError,0,&observation,&failedFlush,"inputCaptureWriterBoundary");
    const auto failure=g_inputCaptureLogFailureEvidence.snapshot(g_inputCaptureLog.is_open(),g_inputCaptureLog.good());
    g_inputCaptureLog.clear();g_inputCaptureLog.close();
    check("capture-flush-failure-retains-available-source-bytes-and-original-failure-stage",
        copied&&copyError==0&&observation.copyComplete&&snapshotCopyEvidence(observation)["inputCaptureWriterBoundary"]=="flush-failed"&&
        failure.count==1&&failure.first.stage==ymcc::logging::CaptureLogFailureStage::ExportFlush);

    const auto focus=root/L"focus-trace.log", focusHistory=root/L"focus-trace.log.1";
    const auto focusRotated=root/L"focus-rotated.log", focusZip=root/L"focus-boundary.zip";
    const std::string focusFirst=R"({"event":"focus-state-change"})", focusPending=R"({"event":"input-edge-window"})";
    auto& focusStream=g_domainLogStreams[domainLogStreamIndex("focus")];
    put(focusHistory,historicalTail);
    check("focus-and-virtual-streams-have-distinct-pending-records",
        focusStream.append(focus.wstring(),focusFirst,4000,false,512000,noRotate)&&
        focusStream.append(focus.wstring(),focusPending,4001,false,512000,noRotate)&&
        stream.append(gamepad.wstring(),firstRow,4000,false,512000,noRotate)&&
        stream.append(gamepad.wstring(),pendingRow,4001,false,512000,noRotate)&&
        focusStream.pendingRows()==1&&stream.pendingRows()==1);
    const SnapshotSourceSync focusBoundary=/* ACTUAL_FOCUS_TRACE_SOURCE_BOUNDARY */;
    bool focusUnlocked=false, onlyFocusFlushed=false;
    unsigned focusCalls=0;
    const std::vector<ArchiveWriterSource> focusWriters{
        {focus.wstring(),[&](const auto& capture){
            ++focusCalls;
            const bool flushed=focusBoundary(capture);
            std::thread rotate([&]{
                std::unique_lock<std::mutex> lock(g_domainLogMx,std::try_to_lock);
                if(!lock)return;
                onlyFocusFlushed=focusStream.pendingRows()==0&&stream.pendingRows()==1;
                focusStream.close();
                std::error_code ec;
                fspath::rename(focus,focusRotated,ec);
                if(ec)return;
                put(focus,"{\"later-focus\":true}\n");
                focusUnlocked=true;
            });
            rotate.join();
            return flushed;
        },"focusTraceWriterBoundary"},
    };
    const auto focusPack=createTarArchiveWithRetry(focusZip.wstring(),{focus.wstring(),focusHistory.wstring()},
        {},{},nullptr,nullptr,&focusWriters);
    check("focus-boundary-flushes-only-the-active-focus-stream-and-unlocks-before-copy",
        focusCalls==1&&focusUnlocked&&onlyFocusFlushed);
    check("focus-boundary-produces-one-archive-and-reports-the-existing-history-tail",
        focusPack.ok&&focusPack.attempts==1&&focusPack.cleanupOk&&focusPack.snapshotBoundaryWarnings==1);
    put(root/L"expected-focus.log",focusFirst+'\n'+focusPending+'\n');
    put(root/L"expected-focus-history.log",historicalTail);

    const auto missingDir=root/L"missing-focus";
    fspath::create_directory(missingDir);
    const auto missingFocus=missingDir/L"focus-trace.log";
    const std::vector<ArchiveWriterSource> missingFocusWriters{{missingFocus.wstring(),focusBoundary,"focusTraceWriterBoundary"}};
    const auto missingFocusPack=createTarArchiveWithRetry((root/L"missing-focus.zip").wstring(),{missingFocus.wstring()},
        {},{},nullptr,nullptr,&missingFocusWriters);
    const auto focusFailures=archiveSnapshotCopyFailureEvidence(missingFocusPack);
    json carried=json::object();
    addPriorSnapshotCopyFailure(carried,{{"ok",false},{"attempts",2},{"snapshotCopyFailureEvidence",focusFailures}});
    check("missing-active-focus-retains-the-source-open-failure-on-both-attempts",
        !missingFocusPack.ok&&missingFocusPack.attempts==2&&missingFocusPack.win32Error==ERROR_FILE_NOT_FOUND&&
        focusFailures["failures"].size()==2&&
        focusFailures["failures"][0]["copyObservation"]["focusTraceWriterBoundary"]=="source-open-failed");
    check("previous-focus-copy-failure-preserves-the-existing-bounded-payload",
        carried.contains("snapshotCopyFailureEvidence")&&carried["snapshotCopyFailureEvidence"]==focusFailures);
    auto wrongFocus=focusFailures;wrongFocus["failures"][0]["entry"]="focus-trace.log.1";
    json rejected=json::object();
    addPriorSnapshotCopyFailure(rejected,{{"ok",false},{"attempts",2},{"snapshotCopyFailureEvidence",wrongFocus}});
    check("previous-copy-failure-rejects-a-focus-boundary-on-rotated-history",rejected.empty());

    bool cleanup=true;
    for(const auto& entry:fspath::directory_iterator(root)) {
        const auto name=entry.path().filename().wstring();
        if(name.find(L".staging-")!=std::wstring::npos||name.find(L".attempt-")!=std::wstring::npos)cleanup=false;
    }
    check("all-owned-attempt-and-staging-files-returned",cleanup&&fspath::exists(active)&&fspath::exists(locked));
    unsigned failures=0;for(const auto& row:checks)if(!row["pass"].get<bool>())++failures;
    std::cout<<json({{"selection","rotation"},{"checks",checks},{"failures",failures},
        {"realApplicationSteamOrPowerActions",false},{"archive",W2U((root/L"rotation.zip").wstring())},
        {"writerBoundaryArchive",W2U(boundaryZip.wstring())},{"focusBoundaryArchive",W2U(focusZip.wstring())},
        {"focusCopyFailures",carried}}).dump(2)<<'\n';
    return failures?2:0;
#endif
}
