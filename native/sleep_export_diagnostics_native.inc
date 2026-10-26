// REQUIRED ordinary export evidence for recorded standby without software DRIPS.
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
static std::vector<std::string> sgSleepExportFactCandidates() {
    std::vector<std::string> facts;
    {std::lock_guard<std::mutex> lock(g_sgFactMx);facts.assign(g_sgFacts.begin(),g_sgFacts.end());}
    if(!ymcc::sleepexport::selectAnomaly(facts).is_null())return facts;
    // Existing ordinary log only, bounded tail at explicit export. A partial
    // concurrently appended record is ignored by the strict selector.
    try {
        std::ifstream in(fspath::path(SG_FACT_LOG),std::ios::binary);
        if(!in)return facts;
        in.seekg(0,std::ios::end);const auto end=in.tellg();
        if(end<=0)return facts;
        constexpr std::streamoff cap=1024*1024;
        const auto offset=(std::max)(std::streamoff{0},static_cast<std::streamoff>(end)-cap);
        in.seekg(offset);
        std::string bytes(static_cast<std::size_t>(static_cast<std::streamoff>(end)-offset),'\0');
        in.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(in.gcount()));
        std::istringstream bounded(bytes);std::string line;
        if(offset)std::getline(bounded,line);
        std::deque<std::string> tail;
        while(std::getline(bounded,line)) {
            if(line.size()>ymcc::sleepexport::maxFactBytes)continue;
            tail.push_back(std::move(line));
            if(tail.size()>ymcc::sleepexport::maxFacts)tail.pop_front();
        }
        std::vector<std::string> historical(tail.begin(),tail.end());
        if(!ymcc::sleepexport::selectAnomaly(historical).is_null())return historical;
    }catch(...){/* Missing historical evidence never grants a power action. */}
    return facts;
}
static SgWindowsSleepExport sgExportWindowsSleepEvidence(const std::wstring& dataDir) {
    SgWindowsSleepExport result;
    const auto generation=currentPowerGeneration();
    const auto current=[generation] {
        return !g_exitRequested.load(std::memory_order_acquire)&&
            g_powerLifecycle.load(std::memory_order_acquire)==PowerLifecycle::Ready&&
            currentPowerGeneration()==generation;
    };
    fspath::path reportPath,receiptPath;
    std::size_t commandOutputBytes=0;
    const auto query=[&](std::uint32_t timeout)->ymcc::sleepexport::QueryResult {
        ymcc::sleepexport::QueryResult q;
        const auto started=GetTickCount64();
        try {
            const fspath::path root(dataDir);std::error_code ec;
            if(!root.is_absolute()||!fspath::is_directory(root,ec)||ec) {q.readError="diagnostic-directory-unavailable";return q;}
            GUID id{};wchar_t nonce[64]{};
            if(FAILED(CoCreateGuid(&id))||!StringFromGUID2(id,nonce,64)) {q.readError="diagnostic-identity-unavailable";return q;}
            const std::wstring stem=std::wstring(L"windows-sleepstudy-")+nonce;
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
            const auto cmd=quote_windows_arg(exe)+L" /sleepstudy /xml /duration 1 /output "+quote_windows_arg(reportPath.wstring());
            const auto run=runCapture(cmd,timeout,[&]{return !current();});
            q.ran=run.ran;q.exitCode=run.exitCode;q.completion=run.completion;q.win32Error=run.win32Error;
            commandOutputBytes=run.out.size();
            if(run.ran&&run.exitCode==0&&q.completion=="child-exit"&&current()) {
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
            }
        }catch(...){q.xml.clear();q.readError="diagnostic-query-exception";}
        q.elapsedMs=GetTickCount64()-started;return q;
    };
    try {
        result.receipt=ymcc::sleepexport::collect(sgSleepExportFactCandidates(),current,query);
        result.receipt["commandOutputBytes"]=commandOutputBytes;
        result.receipt["historyDays"]=1;
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
    return result;
}
