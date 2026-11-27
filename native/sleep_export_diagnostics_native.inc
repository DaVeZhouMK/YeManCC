// REQUIRED explicit-export Windows transition diagnostics, independent of auxiliary DRIPS.
// Only explicit log export calls this. No resident worker, power action or retry.
struct SgWindowsSleepExport {
    json receipt;
    std::vector<std::wstring> files;
    std::vector<fspath::path> owned;
    SgWindowsSleepExport()=default;
    SgWindowsSleepExport(const SgWindowsSleepExport&)=delete;
    SgWindowsSleepExport& operator=(const SgWindowsSleepExport&)=delete;
    SgWindowsSleepExport(SgWindowsSleepExport&& other) noexcept
        :receipt(std::move(other.receipt)),files(std::move(other.files)),owned(std::move(other.owned)) {other.owned.clear();}
    ~SgWindowsSleepExport() {
        for(const auto& path:owned) {
            std::error_code ec;fspath::remove(path,ec);
            if(ec)try{appendNativeLifecycleLog("sleep-export-temp-cleanup-failed",{{"file",W2U(path.filename().wstring())},{"error",ec.value()}});}catch(...){}
        }
    }
};
static json sgSleepExportSelectAnomaly(std::uint64_t exportFileTime) {
    std::vector<std::string> facts;
    {std::lock_guard<std::mutex> lock(g_sgFactMx);facts.assign(g_sgFacts.begin(),g_sgFacts.end());}
    auto selected=ymcc::sleepexport::selectAnomaly(facts);
    if(!selected.is_null())return selected;
    const auto selectFile=[&](const fspath::path& path,bool rotated)->json {
        try {
            if(rotated) {
                std::error_code ec;
                if(fspath::symlink_status(path,ec).type()!=fspath::file_type::regular||ec)return nullptr;
            }
            std::ifstream in(path,std::ios::binary);
            if(!in)return nullptr;
            in.seekg(0,std::ios::end);const auto end=static_cast<std::streamoff>(in.tellg());
            if(end<=0)return nullptr;
            constexpr std::streamoff cap=1024*1024;
            // The writer rotates after the threshold, so allow one extra bounded row.
            constexpr std::streamoff rotatedCap=cap+ymcc::sleepexport::maxFactBytes+1;
            const auto offset=rotated?0:(std::max)(std::streamoff{0},end-cap);
            const auto size=rotated?(std::min)(end,rotatedCap):end-offset;
            in.seekg(offset);
            std::string bytes(static_cast<std::size_t>(size),'\0');
            in.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
            bytes.resize(static_cast<std::size_t>(in.gcount()));
            if(rotated)return ymcc::sleepexport::selectRotatedPairedAnomaly(bytes,exportFileTime);
            return ymcc::sleepexport::selectAnomalyFromFileTail(bytes,offset!=0);
        }catch(...){return nullptr;}
    };
    selected=selectFile(fspath::path(SG_FACT_LOG),false);
    if(!selected.is_null())return selected;
    return selectFile(fspath::path(SG_FACT_LOG+L".previous"),true);
}
static SgWindowsSleepExport sgExportWindowsSleepEvidence(const std::wstring& dataDir,const std::string& exportId={},
        unsigned long long generation=currentPowerGeneration()) {
    SgWindowsSleepExport result;
    // logs.exportAll owns the existing export mutex for this whole call.
    static ymcc::sleepexport::DeadlineCooldown recentDeadline;
    const auto contextBlocker=[generation]() -> const char* {
        if(g_exitRequested.load(std::memory_order_acquire))return "native-exit-requested";
        if(g_powerLifecycle.load(std::memory_order_acquire)!=PowerLifecycle::Ready)return "power-not-ready";
        return currentPowerGeneration()!=generation?"power-generation-changed":nullptr;
    };
    const auto current=[&]{return contextBlocker()==nullptr;};
    fspath::path reportPath,receiptPath;
    std::size_t commandOutputBytes=0;
    ymcc::sleepexport::QueryFailureOutputBytes commandFailureOutput;
    ymcc::sleepexport::CaptureFinalization commandFinalization;
    ymcc::sleepexport::CaptureLoopTiming captureLoopTiming;
    bool commandTimedOut=false;
    unsigned historyDays=ymcc::sleepexport::defaultHistoryDays;
    std::optional<std::uint64_t> preparationMs,captureCallMs,reportFileReadMs;
    const auto runQuery=[&](std::uint32_t timeout)->ymcc::sleepexport::QueryResult {
        ymcc::sleepexport::QueryResult q;
        const auto started=GetTickCount64();
        try {
            const fspath::path root(dataDir);std::error_code ec;
            if(!root.is_absolute()||!fspath::is_directory(root,ec)||ec) {q.readError="diagnostic-directory-unavailable";return q;}
            GUID id{};wchar_t nonce[64]{};
            if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,nonce,64)) {q.readError="diagnostic-identity-unavailable";return q;}
            const std::wstring stem=std::wstring(L"windows-systempowerreport-")+nonce;
            reportPath=root/(stem+L".xml");receiptPath=root/(stem+L".json");
            for(const auto& path:{reportPath,receiptPath}) {
                ec.clear();const auto state=fspath::symlink_status(path,ec);
                if(state.type()!=fspath::file_type::not_found || (ec&&ec!=std::errc::no_such_file_or_directory)) {
                    reportPath.clear();receiptPath.clear();q.readError="diagnostic-path-not-fresh";return q;
                }
            }
            // Only newly minted, absent, direct children of this export's data
            // directory are owned. Never overwrite or clean an existing file.
            result.owned.push_back(reportPath);result.owned.push_back(receiptPath);
            wchar_t system[32768]{};const auto count=GetSystemDirectoryW(system,32768);
            if(!count||count>=32768) {q.readError="absolute-System32-unavailable";return q;}
            const auto exe=std::wstring(system,count)+L"\\powercfg.exe";
            if(exe.size()<3||exe[1]!=L':'||exe[2]!=L'\\') {q.readError="System32-path-not-absolute";return q;}
            const auto cmd=quote_windows_arg(exe)+L" /systempowerreport /xml /duration "+std::to_wstring(historyDays)+
                L" /output "+quote_windows_arg(reportPath.wstring());
            const auto commandStarted=GetTickCount64();
            const auto run=runCapture(cmd,timeout,[&]{return !current();},nullptr,nullptr,nullptr,&captureLoopTiming);
            captureCallMs=GetTickCount64()-commandStarted;
            preparationMs=commandStarted-started;
            q.ran=run.ran;q.exitCode=run.exitCode;q.completion=run.completion;q.win32Error=run.win32Error;
            commandTimedOut=q.ran&&q.exitCode==ERROR_TIMEOUT&&q.win32Error==ERROR_SUCCESS&&q.completion=="deadline";
            commandOutputBytes=run.out.size();commandFinalization=run.captureFinalization;
            if(!q.ran||q.exitCode!=0||q.win32Error!=0||q.completion!="child-exit")
                commandFailureOutput.observe(run.out);
            if(run.ran&&run.exitCode==0&&q.completion=="child-exit"&&current()) {
                const auto readStarted=GetTickCount64();
                ec.clear();const auto state=fspath::symlink_status(reportPath,ec);
                if(ec||state.type()!=fspath::file_type::regular)q.readError="current-report-not-regular";
                else {
                    const auto bytes=fspath::file_size(reportPath,ec);
                    if(ec||bytes==0||bytes>ymcc::sleepexport::maxReportBytes)q.readError="current-report-size-unavailable-or-out-of-bounds";
                    else {
                        std::ifstream in(reportPath,std::ios::binary);q.xml.resize(static_cast<std::size_t>(bytes));
                        in.read(q.xml.data(),static_cast<std::streamsize>(bytes));
                        if(in.gcount()!=static_cast<std::streamsize>(bytes)||in.peek()!=std::char_traits<char>::eof()) {q.xml.clear();q.readError="current-report-changed-or-unreadable";}
                    }
                }
                reportFileReadMs=GetTickCount64()-readStarted;
            }
        }catch(...){q.xml.clear();q.readError="diagnostic-query-exception";}
        q.elapsedMs=GetTickCount64()-started;
        return q;
    };
    bool deadlineRecorded=false;
    const auto query=[&](std::uint32_t timeout){
        auto result=runQuery(timeout);
        if(current())try {
            recentDeadline.observe(generation,GetTickCount64(),result,exportId);
            deadlineRecorded=recentDeadline.known;
        }catch(...){recentDeadline.known=false;}
        return result;
    };
    try {
        const auto select=[&]{
            const auto exportFileTime=sgNowFileTime();
            auto selected=sgSleepExportSelectAnomaly(exportFileTime);
            historyDays=ymcc::sleepexport::reportHistoryDays(selected,exportFileTime);
            return selected;
        };
        const char* receiptBlocker=nullptr;
        const auto receiptCurrent=[&]{receiptBlocker=contextBlocker();return receiptBlocker==nullptr;};
        result.receipt=ymcc::sleepexport::collectWithAnomaly(select,receiptCurrent,query,
            &recentDeadline,generation,GetTickCount64());
        if(receiptBlocker)try {
            const auto reason=result.receipt.value("reason","");
            if(reason=="export-context-not-current"||reason=="export-context-changed")
                result.receipt["queryContextGate"]={{"schema",1},{"blocker",receiptBlocker}};
        }catch(...){}
        if(deadlineRecorded)try {
            if(result.receipt.contains("query") && result.receipt.value("reason","")=="query-not-completed-successfully")
                result.receipt["deadlineRetryPolicy"]=recentDeadline.retryPolicy();
        }catch(...){}
        const auto& deadlineJob=captureLoopTiming.deadlineJob;
        if(commandTimedOut&&result.receipt.contains("query")&&deadlineJob.observed&&current())try {
            result.receipt["deadlineJobAccounting"]={{"schema",1},{"scope","owned-command-job-before-deadline-termination"},
                {"available",deadlineJob.available},{"elapsedMs",deadlineJob.elapsedMs},
                {"win32Error",deadlineJob.available?json(nullptr):json(deadlineJob.win32Error)},
                {"totalProcesses",deadlineJob.available?json(deadlineJob.totalProcesses):json(nullptr)},
                {"activeProcesses",deadlineJob.available?json(deadlineJob.activeProcesses):json(nullptr)},
                {"userTime100ns",deadlineJob.available?json(deadlineJob.userTime100ns):json(nullptr)},
                {"kernelTime100ns",deadlineJob.available?json(deadlineJob.kernelTime100ns):json(nullptr)}};
        }catch(...){}
        const auto& deadlineRoot=captureLoopTiming.deadlineRoot;
        if(commandTimedOut&&result.receipt.contains("query")&&deadlineRoot.observed&&current())try {
            result.receipt["deadlineRootAccounting"]={{"schema",1},{"scope","owned-command-root-before-deadline-termination"},
                {"available",deadlineRoot.available},{"elapsedMs",deadlineRoot.elapsedMs},
                {"win32Error",deadlineRoot.available?json(nullptr):json(deadlineRoot.win32Error)},
                {"userTime100ns",deadlineRoot.available?json(deadlineRoot.userTime100ns):json(nullptr)},
                {"kernelTime100ns",deadlineRoot.available?json(deadlineRoot.kernelTime100ns):json(nullptr)}};
        }catch(...){}
        // Inspect the owned output name once after capture, without consuming a failed report.
        if(commandTimedOut&&result.receipt.contains("query")&&!reportPath.empty()&&current())try {
            WIN32_FILE_ATTRIBUTE_DATA info{};
            const auto started=GetTickCount64();
            const bool found=GetFileAttributesExW(reportPath.c_str(),GetFileExInfoStandard,&info)!=FALSE;
            const DWORD error=found?ERROR_SUCCESS:GetLastError();
            const auto elapsed=GetTickCount64()-started;
            if(current()) {
                const bool regular=found&&!(info.dwFileAttributes&
                    (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE));
                const auto size=(static_cast<std::uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
                result.receipt["deadlineReportPath"]={{"schema",1},{"scope","owned-report-path-after-current-capture-deadline;metadata-only"},
                    {"state",regular?"regular-file-metadata":found?"non-regular-path":
                        (error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)?"path-not-found":"attribute-query-failed"},
                    {"bytes",regular?json(size):json(nullptr)},{"win32Error",found?json(nullptr):json(error)},
                    {"elapsedMs",elapsed}};
            }
        }catch(...){}
        unsigned finalizationEvidenceBuildFailures=0;
        if(!result.receipt.contains("query"))commandFinalization.entered=false;
        try {
            auto evidence=ymcc::sleepexport::captureFinalizationEvidence(commandFinalization);
            if(evidence.is_null())++finalizationEvidenceBuildFailures;
            result.receipt["commandCaptureFinalization"]=std::move(evidence);
        }catch(...){++finalizationEvidenceBuildFailures;}
        try{result.receipt["commandCaptureFinalizationBuildFailures"]=finalizationEvidenceBuildFailures;}catch(...){}
        if(result.receipt.contains("query")&&captureCallMs&&preparationMs)try {
            json phases={{"preparation",*preparationMs},{"captureCall",*captureCallMs}};
            if(captureLoopTiming.observed) {
                phases["setupThroughResume"]=captureLoopTiming.setupThroughResumeElapsedMs;
                phases["captureLoop"]=captureLoopTiming.loopElapsedMs;
            }
            if(reportFileReadMs)phases["reportFileRead"]=*reportFileReadMs;
            result.receipt["queryPhaseElapsedMs"]=std::move(phases);
        }catch(...){}
        unsigned outputEvidenceBuildFailures=0;
        if(!result.receipt.contains("query"))commandFailureOutput.observed=false;
        // Final drainage after job termination can see pipe closure that did not occur in the capture loop.
        const auto& pipe=captureLoopTiming.pipe;
        if(commandFailureOutput.observed&&captureLoopTiming.observed&&pipe.observed())try {
            result.receipt["commandCapturePipe"]={{"schema",1},{"scope","current-failed-query-capture-loop-before-finalization"},
                {"emptyPeekObserved",pipe.emptyPeekObserved},{"nonemptyPeekObserved",pipe.nonemptyPeekObserved},
                {"emptyReadObserved",pipe.emptyReadObserved},{"bytesReadObserved",pipe.bytesReadObserved},
                {"firstPeekError",pipe.firstPeekError?json(*pipe.firstPeekError):json(nullptr)},
                {"firstReadError",pipe.firstReadError?json(*pipe.firstReadError):json(nullptr)}};
        }catch(...){}
        try {
            auto bytes=ymcc::sleepexport::queryFailureOutputBytesEvidence(commandFailureOutput);
            if(bytes.is_null())++outputEvidenceBuildFailures;
            result.receipt["commandFailureOutputBytes"]=std::move(bytes);
        }catch(...){++outputEvidenceBuildFailures;}
        try{result.receipt["commandFailureOutputEvidenceBuildFailures"]=outputEvidenceBuildFailures;}catch(...){}
        result.receipt["commandOutputBytes"]=commandOutputBytes;
        result.receipt["historyDays"]=historyDays;
        result.receipt["correlation"]="Windows report history; match original event timestamp; not live retry authority";
        result.receipt["temporaryFiles"]="unique-export-owned; removed after archive attempt";
        if(result.receipt.value("reportAvailable",false)&&!reportPath.empty()) {
            result.receipt["reportFile"]=W2U(reportPath.filename().wstring());result.files.push_back(reportPath.wstring());
        }
        if(!receiptPath.empty()) {
            std::ofstream receipt(receiptPath,std::ios::binary|std::ios::trunc);
            receipt<<result.receipt.dump(2)<<'\n';receipt.close();
            if(receipt)result.files.push_back(receiptPath.wstring());
            else result.receipt["receiptWriteError"]="ordinary-export-receipt-unavailable";
        }
    }catch(...) {
        result.files.clear();result.receipt={{"status","UNKNOWN"},{"reportAvailable",false},
            {"reason","diagnostic-export-exception"},{"automaticRepairAuthorized",false},{"mandatoryEvidence",true}};
    }
    // Keep cleanup with the local owner until this explicit handoff.
    return SgWindowsSleepExport(std::move(result));
}
