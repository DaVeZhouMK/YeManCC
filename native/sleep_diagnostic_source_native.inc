// Source configuration is prepared only on a saved repair preference transition/startup.
// The diagnostic journal and these low-frequency receipts are REQUIRED even with detailed logs OFF.
static std::mutex g_sgDiagnosticMx;
static std::atomic<int> g_sgDiagnosticWanted{-1};
static const std::wstring SG_DIAGNOSTIC_JOURNAL=SG_DIR+L"\\diagnostic-source-owned.json";
struct SgDiagnosticPorts {
    DWORD error=ERROR_SUCCESS;
    ymcc::sleepdiagnostic::Load load(ymcc::sleepdiagnostic::Journal& journal) {
        std::error_code ec;
        if(!fspath::exists(SG_DIAGNOSTIC_JOURNAL,ec))return ec?ymcc::sleepdiagnostic::Load::Invalid:ymcc::sleepdiagnostic::Load::Missing;
        const auto bytes=fspath::file_size(SG_DIAGNOSTIC_JOURNAL,ec);
        if(ec||bytes>16384)return ymcc::sleepdiagnostic::Load::Invalid;
        try {
            const auto value=json::parse(sgReadFile(SG_DIAGNOSTIC_JOURNAL));
            if(!value.is_object()||value.value("version",0)!=1||value.value("channel",std::string{})!="Microsoft-Windows-Kernel-Power/Diagnostic"||
                !value.contains("before")||!value["before"].is_boolean()||value["before"]!=false||
                !value.contains("expected")||!value["expected"].is_boolean()||value["expected"]!=true)
                return ymcc::sleepdiagnostic::Load::Invalid;
            journal.owned=true;return ymcc::sleepdiagnostic::Load::Valid;
        } catch(...) {return ymcc::sleepdiagnostic::Load::Invalid;}
    }
    bool save(const ymcc::sleepdiagnostic::Journal&) {
        std::error_code ec;fspath::create_directories(SG_DIR,ec);
        return !ec&&sgWriteFileAtomic(SG_DIAGNOSTIC_JOURNAL,json{{"version",1},{"channel","Microsoft-Windows-Kernel-Power/Diagnostic"},{"before",false},{"expected",true}}.dump());
    }
    bool clear() {std::error_code ec;fspath::remove(SG_DIAGNOSTIC_JOURNAL,ec);return !ec;}
    bool allowed(bool wanted) const {return g_sgDiagnosticWanted.load(std::memory_order_acquire)==(wanted?1:0)&&!g_exitRequested.load(std::memory_order_acquire);}
    bool read(bool& result) {
        const auto config=EvtOpenChannelConfig(nullptr,L"Microsoft-Windows-Kernel-Power/Diagnostic",0);
        if(!config){error=GetLastError();return false;}
        EVT_VARIANT enabled{};DWORD used=0;
        const bool ok=EvtGetChannelConfigProperty(config,EvtChannelConfigEnabled,0,sizeof(enabled),&enabled,&used)&&
            (enabled.Type&EVT_VARIANT_TYPE_MASK)==EvtVarTypeBoolean;
        if(!ok)error=GetLastError();else result=enabled.BooleanVal!=FALSE;
        EvtClose(config);return ok;
    }
    bool write(bool enabled) {
        const auto config=EvtOpenChannelConfig(nullptr,L"Microsoft-Windows-Kernel-Power/Diagnostic",0);
        if(!config){error=GetLastError();return false;}
        EVT_VARIANT value{};value.Type=EvtVarTypeBoolean;value.BooleanVal=enabled?TRUE:FALSE;
        const bool ok=EvtSetChannelConfigProperty(config,EvtChannelConfigEnabled,0,&value)&&EvtSaveChannelConfig(config,0);
        if(!ok)error=GetLastError();EvtClose(config);return ok;
    }
};
static void sgDiagnosticSync(bool wanted) {
    if(g_aiFanMockSession.isolated||g_exitRequested.load(std::memory_order_acquire))return;
    const int desired=wanted?1:0;
    if(g_sgDiagnosticWanted.exchange(desired,std::memory_order_acq_rel)==desired)return;
    if(!poolSubmit([wanted,desired]{
        std::lock_guard<std::mutex> lock(g_sgDiagnosticMx);
        if(g_sgDiagnosticWanted.load(std::memory_order_acquire)!=desired||g_exitRequested.load(std::memory_order_acquire))return;
        SgDiagnosticPorts ports;
        const auto result=ymcc::sleepdiagnostic::reconcile(ports,wanted);
        sgRecordFact("abnormal-sleep-source-preparation",{{"requested",wanted},{"ready",result.ready},{"ok",result.ok},
            {"owned",result.owned},{"error",result.error},{"win32",ports.error},{"detailLogsRequired",false},{"mandatoryEvidence",true}});
    })) {
        int expected=desired;g_sgDiagnosticWanted.compare_exchange_strong(expected,-1,std::memory_order_acq_rel);
        sgRecordFact("abnormal-sleep-source-preparation",{{"requested",wanted},{"ok",false},{"reason","worker-unavailable"},{"detailLogsRequired",false},{"mandatoryEvidence",true}});
    }
}
