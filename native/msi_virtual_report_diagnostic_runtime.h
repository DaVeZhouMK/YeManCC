#pragma once
// Included inside main.cpp after Host globals. MSI opt-in diagnostic glue ONLY.
// No service/registry/HID/mode writes or controller-policy decisions. Native
// requests ONE session only when explicitly launched with an ephemeral token.
// All APIs below are called with g_inputHostMx held, except thread-local scope.
static ymcc::msi::virtualReportDiag::Session g_msiVirtualReportDiagnostic;
static thread_local const std::string* g_msiVirtualReportFrameMetadata = nullptr;
#ifdef YMCC_MSI_REPORT_CHAIN_TEST
static std::function<void(const char*,const json&)> g_msiVirtualReportTestLog;
#endif
static void inputHostMsiReportDiagnosticLog(const char* event,const json& detail) {
#ifdef YMCC_MSI_REPORT_CHAIN_TEST
    if (g_msiVirtualReportTestLog) g_msiVirtualReportTestLog(event,detail);
#else
    appendDomainLog("virtual",event,detail);
#endif
}

struct MsiVirtualReportFrameMetadataScope {
    const std::string* previous = nullptr;
    explicit MsiVirtualReportFrameMetadataScope(const std::string& value) noexcept
        : previous(g_msiVirtualReportFrameMetadata) { g_msiVirtualReportFrameMetadata = &value; }
    ~MsiVirtualReportFrameMetadataScope() { g_msiVirtualReportFrameMetadata = previous; }
};
static ymcc::msi::virtualReportDiag::Binding inputHostMsiReportDiagnosticBinding() {
    return {GetCurrentProcessId(),g_inputHostRunId,g_inputHostEpoch,inputHostProtocolPowerGenerationLocked()};
}
static ymcc::msi::virtualReportDiag::Gates inputHostMsiReportDiagnosticGates() {
    ymcc::msi::virtualReportDiag::Gates gates;
    gates.knownMsiBoard = msiClawKnownBoardAuthority();
#ifdef YMCC_MSI_VISIBILITY_DIAGNOSTIC
    gates.visibilityDiagnosticBuild = true;
#endif
    gates.virtualDetailLogEnabled = domainLogEnabled("virtual");
    gates.exitFence = g_exitRequested.load() || g_inputHostExitFence.load();
    gates.powerReady = g_inputReady.load(std::memory_order_acquire) &&
        g_powerLifecycle.load(std::memory_order_acquire) == PowerLifecycle::Ready &&
        !g_inputHostPowerAdmissionRevoked.load(std::memory_order_acquire);
    return gates;
}
static void inputHostMsiReportDiagnosticReadExplicitRequest() noexcept {
#if !defined(YMCC_MSI_REPORT_CHAIN_TEST)
    static bool read = false;
    if (read) return;
    read = true;
    try {
        // Read-only, process-local, opt-in. Never creates a flag or changes an
        // environment variable, setting or ordinary virtual toggle behavior.
        wchar_t token[128]{};
        const DWORD length = GetEnvironmentVariableW(L"YMCC_MSI_REPORT_CHAIN_SESSION", token, 128);
        if (!length || length >= 128) return;
        const std::string id = W2U(std::wstring(token,length));
        (void)g_msiVirtualReportDiagnostic.Request(id,10000,128);
    } catch (...) { g_msiVirtualReportDiagnostic.Close(); }
#endif
}
static void inputHostMsiReportDiagnosticObserveHello(const json& receipt) noexcept {
    unsigned version = 0;
    try {
        const auto it = receipt.find("msiReportChainDiagnosticVersion");
        if (it != receipt.end() && it->is_number_integer() && *it == 1) version = 1;
    } catch (...) { version = 0; }
    g_msiVirtualReportDiagnostic.ObserveHello(version);
}
static bool inputHostMsiReportDiagnosticAppendOptional(std::string& text,const char* key,const json& value) noexcept {
    try {
        if (text.empty() || text.back() != '}') return false;
        const std::string encodedKey = json(key).dump();
        if (text.find(encodedKey) != std::string::npos) return false;
        const std::string addition = "," + encodedKey + ":" + value.dump();
        // Preserve EVERY original control byte; no DOM re-encoding, sample
        // counters, hashes or sequence advancement. Allocation faults leave
        // the original text untouched. Fast and slow paths use this SAME glue.
        std::string candidate = text;
        candidate.insert(candidate.size()-1,addition);
        text.swap(candidate);
        return true;
    } catch (...) { return false; }
}
static void inputHostMsiReportDiagnosticExtendRequest(const char* command,std::string& text) noexcept {
    try {
        if (std::strcmp(command,"RELEASE_TARGET") == 0 || std::strcmp(command,"RECREATE_TARGET") == 0 ||
            std::strcmp(command,"QUIESCE") == 0) {
            g_msiVirtualReportDiagnostic.Close(); return;
        }
        if (std::strcmp(command,"PREPARE_TARGET") == 0) {
            const auto gates = inputHostMsiReportDiagnosticGates();
            if (!gates.knownMsiBoard || !gates.visibilityDiagnosticBuild || !gates.virtualDetailLogEnabled ||
                gates.exitFence || !gates.powerReady || g_msiVirtualReportDiagnostic.version() != 1) return;
            inputHostMsiReportDiagnosticReadExplicitRequest();
            const auto authorization = g_msiVirtualReportDiagnostic.OfferPrepare(inputHostMsiReportDiagnosticBinding(),gates,GetTickCount64());
            if (!authorization) return;
            const auto& a = *authorization;
            const json metadata = {{"schemaVersion",1},{"enabled",true},{"msiClawKnownBoardAuthority",true},
                {"visibilityDiagnosticBuild",true},{"virtualDetailLogEnabled",true},{"explicitSessionRequested",true},
                {"nativePid",a.binding.nativePid},{"runId",a.binding.runId},{"epoch",a.binding.epoch},
                {"powerGeneration",a.binding.powerGeneration},{"diagnosticSessionId",a.sessionId},
                {"durationMs",a.durationMs},{"maxMeaningfulEdges",a.maxMeaningfulEdges}};
            if (!inputHostMsiReportDiagnosticAppendOptional(text,"msiReportChainDiagnostic",metadata))
                g_msiVirtualReportDiagnostic.Close();
        } else if (std::strcmp(command,"SUBMIT_FRAME") == 0 && g_msiVirtualReportFrameMetadata &&
                   !g_msiVirtualReportFrameMetadata->empty() && g_msiVirtualReportDiagnostic.active()) {
            const json metadata = json::parse(*g_msiVirtualReportFrameMetadata,nullptr,false);
            if (metadata.is_discarded() || !inputHostMsiReportDiagnosticAppendOptional(text,"msiReportChainSource",metadata))
                g_msiVirtualReportDiagnostic.Close();
        }
    } catch (...) { g_msiVirtualReportDiagnostic.Close(); }
}
static void inputHostMsiReportDiagnosticAcceptPrepare(bool prepared,const json& receipt) noexcept {
    try {
        bool armed = false; std::string id;
        const auto bit = receipt.find("msiReportChainDiagnosticArmed");
        const auto session = receipt.find("msiReportChainDiagnosticSessionId");
        if (prepared && bit != receipt.end() && bit->is_boolean() && bit->get<bool>() &&
            session != receipt.end() && session->is_string()) {armed=true;id=session->get<std::string>();}
        const auto gates = inputHostMsiReportDiagnosticGates();
        if (!gates.knownMsiBoard || !gates.visibilityDiagnosticBuild || !gates.virtualDetailLogEnabled || gates.exitFence || !gates.powerReady) {
            g_msiVirtualReportDiagnostic.Close(); return;
        }
        const bool accepted = g_msiVirtualReportDiagnostic.AcceptPrepare(inputHostMsiReportDiagnosticBinding(),armed,id,GetTickCount64());
        if (accepted) inputHostMsiReportDiagnosticLog("msi-report-chain-diagnostic-armed",{
            {"diagnosticSessionId",id},{"runId",g_inputHostRunId},{"hostEpoch",g_inputHostEpoch},
            {"powerGeneration",inputHostProtocolPowerGenerationLocked()},{"durationMs",10000},{"maxMeaningfulRecords",128},
            {"scope","explicit-msi-virtual-report-diagnostic-only;no-control-policy-change"}});
    } catch (...) { g_msiVirtualReportDiagnostic.Close(); }
}
static std::string inputHostMsiReportDiagnosticFrameMetadata(const ymcc::msi::virtualReportDiag::SourceStamp& source,
    bool semanticNeutral,uint32_t submittedButtons) noexcept {
    (void)semanticNeutral;(void)submittedButtons;
    try {
        const auto metadata = g_msiVirtualReportDiagnostic.SourceForFrame(inputHostMsiReportDiagnosticBinding(),
            inputHostMsiReportDiagnosticGates(),source,GetTickCount64());
        if (!metadata) return {};
        const auto& m = *metadata;
        return json{{"schemaVersion",1},{"diagnosticSessionId",m.sessionId},
            {"sourceFrameSequence",m.source.sequence},{"sourceGeneration",m.source.generation}}.dump();
    } catch (...) { g_msiVirtualReportDiagnostic.Close(); return {}; }
}
// After the EXISTING native transport attempt only; no backend call/retry or
// control decision is introduced. Host separately logs the actual SDK boundary.
static void inputHostMsiReportDiagnosticRecordSourceAfterAttempt(
    const ymcc::msi::virtualReportDiag::SourceStamp& source,bool semanticNeutral,uint32_t submittedButtons,
    bool nativeRequestAccepted) noexcept {
    try {
        const auto now = GetTickCount64();
        const auto metadata = g_msiVirtualReportDiagnostic.SourceForFrame(inputHostMsiReportDiagnosticBinding(),
            inputHostMsiReportDiagnosticGates(),source,now);
        if (!metadata) return;
        const auto& m = *metadata;
        if (g_msiVirtualReportDiagnostic.ShouldRecordSource(m,semanticNeutral,now,submittedButtons)) {
            const auto& s = m.source;
            inputHostMsiReportDiagnosticLog("msi-report-chain-physical-source",{{"diagnosticSessionId",m.sessionId},
                {"runId",g_inputHostRunId},{"hostEpoch",g_inputHostEpoch},{"powerGeneration",inputHostProtocolPowerGenerationLocked()},
                {"sourceFrameSequence",s.sequence},{"sourceGeneration",s.generation},{"sourceCapturedTick",s.capturedTick},
                {"sourceAgeMs",now-s.capturedTick},{"rawXinputButtons",s.buttons},{"rawLeftTrigger",s.leftTrigger},
                {"rawRightTrigger",s.rightTrigger},{"rawLX",s.lx},{"rawLY",s.ly},{"rawRX",s.rx},{"rawRY",s.ry},
                {"semanticNeutral",semanticNeutral},{"submittedCanonicalButtons",submittedButtons},
                {"nativeRequestAccepted",nativeRequestAccepted},
                {"sequenceScope","same-seqlock-canonical-publish-ordinal;not-USB-packet"},
                {"generationScope","same-seqlock-verified-DInput-bindCount;not-Host-epoch"},
                {"deliveryScope","physical-canonical-sample-and-native-transport-result-only;not-SDK-return-or-Steam-action"}});
        }
    } catch (...) { g_msiVirtualReportDiagnostic.Close(); }
}
