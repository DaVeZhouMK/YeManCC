"""Offline logging test: compile extracted real native writers, no product/device calls.

Uses the existing MSVC toolchain. All fixture logs and binaries remain under
--output-dir. No execution-policy changes, privilege elevation or recursive cleanup.
"""
from __future__ import annotations
import argparse, datetime, hashlib, json, os, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

def extract(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    # Mask strings and comments without moving offsets; brackets in a comment or
    # JSON string must not end the extracted production function.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: " " * len(m.group(0)), source, flags=re.S)
    depth = 0
    for i in range(opening, len(masked)):
        if masked[i] == "{": depth += 1
        elif masked[i] == "}":
            depth -= 1
            if depth == 0: return source[start:i + 1]
    raise ValueError("Unclosed function: " + signature)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=pathlib.Path,
                        default=ROOT.parents[1] / "Build/Validation/LogFrequency")
    parser.add_argument("--gpu-windows-result", type=pathlib.Path, help="Production Windows offline transport responses for capability-log checks")
    parser.add_argument("--sdk-first-response",type=pathlib.Path,help="Actual-source fake-SDK reply for Native journal bridge replay")
    parser.add_argument("--review-existing-run",type=pathlib.Path,help="Read an immutable completed producer after a postcheck repair;never rebuild or rerun it")
    args = parser.parse_args()
    review_run=args.review_existing_run.resolve() if args.review_existing_run else None

    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = output / ("run-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + str(os.getpid()))
    run.mkdir(exist_ok=False)
    sdk_response_path=args.sdk_first_response.resolve() if args.sdk_first_response else None
    sdk_response_bytes=sdk_response_path.read_bytes() if sdk_response_path else None
    if sdk_response_bytes is not None:(run/"sdk-first-response.json").write_bytes(sdk_response_bytes)
    gpu_result_path = args.gpu_windows_result.resolve() if args.gpu_windows_result else None
    if gpu_result_path:
        gpu_result_bytes = gpu_result_path.read_bytes()
        gpu_result = json.loads(gpu_result_bytes)
        if not (gpu_result.get("ok") and gpu_result.get("productionReader") and gpu_result.get("productionApply")):
            raise ValueError("GPU Windows result is not a passing production transport fixture")
    else:
        gpu_result_bytes = None
    extra_headers = {name:(ROOT/"native"/name).read_bytes() for name in ("log_snapshot_state.h", "virtual_start_protection.h", "rog_fse_experiment_policy.h", "sleep_fact_log_record.h", "log_jsonl_boundary.h", "sleep_steam_recovery.h", "decky_sidebar_diagnostics.h", "model_adaptation_log.h", "virtual_adaptation_evidence.h", "virtual_adaptation_log_native.inc", "model_adaptation_log_native.inc", "../capabilities/HcOneXVendorInputContract.h", "../capabilities/HcOemInputCapabilityBinding.h", "../capabilities/hc-device-catalog.generated.h")}
    source_path = ROOT / "native/main.cpp"
    source_bytes = source_path.read_bytes()
    source = source_bytes.decode("utf-8-sig").replace("\r\n", "\n")
    identity_observer = extract(source, "static void hidHideObserveRawInputDevice(HANDLE rawDevice) {")
    header = ROOT / "native/log_frequency_policy.h"
    header_bytes = header.read_bytes()
    (run / "bound-main.cpp").write_bytes(source_bytes)
    (run / "bound-log-frequency-policy.h").write_bytes(header_bytes)
    functions = [extract(source, x) for x in (
        "static bool domainLogEnabled(const char* domain) {",
        "static bool sensorDetailLoggingGate() {",
        "static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes)",
        "static void mirrorLifecycleToDomains(const char* event, const json& detail) {",
        "static bool nativeLifecycleEventIsBase(const char* event) {",
        "static void flushNativeLifecycleLogs(bool closeDetail) {",
        "static bool nativeLifecycleRecordIsStreaming(const char* event, const json& detail) {",
        "static void appendNativeLifecycleLog(const char* event, const json& observed) {",
        "static void domainLogGateRecord(const char* source, bool enabled) {",
        "static void inputHostCostFlagSync() {",
        "template<class Record>\nstatic void inputCaptureAppendRecord(Record&& rec) {",
        "static void inputCaptureAppend(const json& rec) {",
        "static void inputCaptureAppendOwned(json&& rec) {",
        "static void inputCaptureQueueSample(json rec) {",
        "static void inputCaptureDrainQueuedSamples() {",
        "static void sgRecordFact(const char* event, const json& details) {",
        "static void record_gpu_dedicated_memory_capability(const json& result) {",
        "static void hidHideLogPhysicalIdentityObservation(const HidHidePhysicalIdentity& resolved, std::uint64_t sourceGeneration, bool selectedChanged = true) {",
    )]
    # rotateOneLogFile has a forward declaration with a default argument. Select
    # its definition explicitly to avoid extracting the next unrelated function.
    functions[2] = extract(source, "static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {")
    functions.append(extract(source,"static json inputHostRecoveryEpisodeLogRecord(const json& requestId,unsigned long long hostEpoch,"))
    fan = extract(source, 'ipc_on("fanLog.setEnabled", [](const json& a) -> json {')
    fan_body = fan[fan.index("{"):]
    family = extract(source, "        if (controllerMotion) {")
    family_log = extract(family, "if (g_sensorDetailLogging.load(std::memory_order_acquire)) {")
    inspection_source = (ROOT / "native/sleep_steam_recovery.h").read_text(encoding="utf-8-sig").replace("\r\n", "\n")
    inspection_log = extract(inspection_source, "static void recordInspection(unsigned long long generation, const char* purpose, const json& observed) noexcept {")
    decky_log = extract(source, 'static std::wstring deckySidebarLogPath() {') + "\n" + extract(source, 'static void deckySidebarDiagnostic(const char* event,const json& detail,bool force) {')
    screen_log = extract(source, 'static void logScreenTouchpadObservation(const char* site, const std::string& persona, const json& state) noexcept {')
    steam_log_projection = extract(source, "static json nativeSteamGameLinkLogDetail(const json& detail) {")
    prelude = r"""
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <map>
#include "decky_sidebar_diagnostics.h"
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
#include "log_snapshot_state.h"
#include "rog_fse_experiment_policy.h"
#include "sleep_fact_log_record.h"
#include "log_jsonl_boundary.h"
#include "virtual_adaptation_evidence.h"
#include "virtual_start_protection.h"
#include <Xinput.h>
using json = nlohmann::json;
// Sleep activity sampling is outside this isolated journal/writer fixture.
static void sgSleepActivityOnFact(const char*,const json&) {}
static void flushTdpObservationLogs(bool = false) {}
// Detailed monitor sink is tested by monitor_detail_bounds_selftest; this fixture owns only legacy writer lanes.
static void monitorDetailGateChanged(bool) {}
namespace fspath = std::filesystem;
static std::wstring g_testRoot, SG_FACT_LOG;
struct MachineIdentityFixture {
    std::string deviceClass="HandheldCompanion.Devices.ClawCG3EM",manufacturer="MSI",
        systemProduct="G3E fixture",processor="Intel fixture",product="fixture",systemVersion="fixture";
    int family=0;
    bool resolved=true,attempted=true;
};
struct VirtualEvidenceSourceFixture {
    std::string inputSource="xinput";XINPUT_GAMEPAD pad{};
    uint64_t readAttemptSequence=17,readStartedTick=100,readCompletedTick=101,readPowerGeneration=23,readTopologyGeneration=11,readContinuityGeneration=7;
    unsigned primarySlot=0;bool live=true,readContextStable=true;
};
static const VirtualEvidenceSourceFixture* t_commonFrameSource=nullptr;
static MachineIdentityFixture g_machineIdentity;
static const char* ymccFamilyName(int) { return "fixture-family"; }
static const unsigned SG_FACT_HISTORY_LIMIT = 512;
static std::mutex g_sgFactMx;
static std::deque<std::string> g_sgFacts;
static std::string sgFactTimestamp() { return "2000-01-01T00:00:00.000"; }
static std::wstring app_data_dir() { return g_testRoot; }
static std::wstring yemancc_data_dir() { return g_testRoot; }
static std::string W2U(const std::wstring& s) { return std::string(s.begin(), s.end()); }
static unsigned long long kLogMaxBytes = 256; // fixture threshold only; production stays 5 MiB
static std::atomic<bool> g_domainLogEnabled[2] = {false, false};
static std::atomic<bool> g_sensorDetailLogging{false};
static std::atomic<bool> g_fanLogEnabled{false};
static std::wstring g_fanLogFile;
static std::atomic<int> g_powerLifecycle{0};
static std::atomic<unsigned long long> g_powerGeneration{0};
static HWND g_hwnd = nullptr;
static const char* powerLifecycleName(int) { return "offline-fixture"; }
static std::string nativeLifecycleTimestamp() { return "2000-01-01T00:00:00.000"; }
struct HidHidePhysicalIdentity {
    bool resolved=true;
    std::string sourcePath,deviceInstanceId,baseContainerDeviceInstanceId;
    unsigned short vendorId=0x17ef,productId=0x61ed,usagePage=1,usage=5;
};
static std::mutex g_nativeLifecycleLogMx;
static std::ofstream g_nativeLifecycleLog;
static uint32_t g_nativeLifecycleFrameWrites = 0;
static std::ofstream g_nativeDetailLog;
static uint32_t g_nativeDetailWrites = 0;
static ULONGLONG g_nativeDetailLastFlushTick = 0;
static std::mutex g_domainLogGateTimelineMx;
static std::vector<std::string> g_domainLogGateTimeline;
static bool g_domainLogGateKnown = false;
static bool g_domainLogGateLast = false;
static unsigned configWrites = 0;
static unsigned domainRows = 0;
static std::mutex g_inputCaptureMx;
static std::wstring g_inputCapturePath;
static std::ofstream g_inputCaptureLog;
static uint64_t g_inputCaptureBytes = 0;
static uint32_t g_inputCaptureSampleWrites = 0;
static std::mutex g_inputCaptureSampleQueueMx;
static std::deque<json> g_inputCaptureSampleQueue;
static uint64_t g_inputCaptureSampleQueueDrops = 0;
static constexpr size_t kInputCaptureSampleQueueLimit = 256;
static std::wstring fan_host_state_dir() { return g_testRoot; }
static const std::wstring POWER_CONTROL_DIR=L"isolated-fixture-root";
static std::mutex g_domainLogGateWriteMx;
static void domainLogsSaveConfig() { ++configWrites; }
static void fanDiagnosticsApplyEnabled(bool on) { g_fanLogEnabled.store(on); }
static void appendDomainLog(const char*, const char*, const json&) { ++domainRows; }
static void flushDomainLogs(bool) {} // Real domain buffering has a separate R20 offline gate.
static void flushNativeLifecycleLogs(bool closeDetail = false);
static void rotateOneLogFile(const std::wstring&, unsigned long long maxBytes = kLogMaxBytes);
"""
    prelude += "\n" + extract(source,"static unsigned long long currentPowerGeneration() {")
    oem_constants = "\n".join(line for line in source.splitlines() if line.startswith("static constexpr uint64_t kOemBit"))
    catalog_fixture = '\n#include "../capabilities/HcOemInputCapabilityBinding.h"\n'
    for signature in ("enum class OemKeyForm : int {", "struct OemKeyDef {", "struct OemCatalogRef {", "static const OemKeyDef kOneXApexKeys[] = {"):
        catalog_fixture += extract(source,signature) + ";\n"
    catalog_fixture += r"""
static const ymcc::capabilities::oem::InputBinding<OemCatalogRef>& oemUnifiedInputBindingFor(int family,const std::string& cls){
    static ymcc::capabilities::oem::InputBindingCache<int,OemCatalogRef> cache;
    return cache.bind(family,cls,[](int,const std::string&){return 4;},[](int,const std::string& canonical){
        return canonical=="HandheldCompanion.Devices.OneXPlayerApex"?OemCatalogRef{kOneXApexKeys,static_cast<int>(std::size(kOneXApexKeys))}:OemCatalogRef{nullptr,0};});
}
"""
    prelude += "\n" + oem_constants + catalog_fixture + '\n#include "model_adaptation_log_native.inc"\n'
    rog_parser=extract(source,"static void rogOemKeyParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")
    rog_constants="\n".join(line for line in source.splitlines() if line.startswith("static constexpr BYTE kRogKeyEvent") or line.startswith("static constexpr ULONGLONG kRogKeyPulseMs"))
    onex_constants="\n".join(line for line in source.splitlines() if line.startswith("static constexpr BYTE kOnexBack"))
    onex_real_parser=extract(source,"static void onexBackParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")
    rog_fixture=r"""
enum class OemProducerId { RogClick, OneX };
static bool oemProducerCurrent(OemProducerId,unsigned long long generation){return generation==71;}
static struct {uint64_t pulseMask=0;ULONGLONG pulseUntil=0;} g_rogOemKey;
static std::atomic<uint64_t> g_oemKeyMaskRog{0};
static unsigned rogPublications=0;
static bool oemProducerPublishRogClick(unsigned long long, BYTE, uint64_t){++rogPublications;return true;}
""" + rog_constants + "\n" + rog_parser + r"""
static struct {ymcc::capabilities::oem::onex::Descriptor desc{true,true,ymcc::capabilities::oem::onex::InitProfile::Apex,true};
    bool feLatched=false;int state=0;uint64_t auxKeys=0,actionGeneration=71;} g_onexBack;
static std::atomic<uint64_t> g_oemKeyMaskHid{0};static std::atomic<int> g_oemBackState{0};
static unsigned onexReinit=0,onexPublications=0;static uint64_t onexObserved=0;
static void onexBackRequestReinit(){++onexReinit;}
static bool oemProducerPublish(OemProducerId,uint64_t,uint64_t,uint64_t observed){++onexPublications;onexObserved=observed;return true;}
""" + onex_constants + "\n" + onex_real_parser + "\n"
    tests = r"""
static json checks = json::array();
static void check(const char* name, bool pass) { checks.push_back({{"name", name}, {"pass", pass}}); }
static std::vector<json> readRows(const wchar_t* name) {
    flushNativeLifecycleLogs();
    std::ifstream f(fspath::path(g_testRoot) / name);
    std::vector<json> rows;
    for (std::string line; std::getline(f, line);) if (!line.empty()) rows.push_back(json::parse(line));
    return rows;
}
static void reset(const fspath::path& root, const wchar_t* name) {
    g_nativeLifecycleLog.close(); g_nativeLifecycleLog.clear();
    g_nativeDetailLog.close(); g_nativeDetailLog.clear();
    g_inputCaptureLog.close(); g_inputCaptureLog.clear();
    g_inputCaptureBytes = 0; g_inputCaptureSampleWrites = 0;
    g_inputCaptureSampleQueue.clear(); g_inputCaptureSampleQueueDrops = 0;
    kLogMaxBytes = 256;
    g_nativeLifecycleFrameWrites = 0; g_nativeDetailWrites = 0;
    g_nativeDetailLastFlushTick = GetTickCount64();
    g_domainLogEnabled[0] = false; g_domainLogEnabled[1] = false;
    g_domainLogGateKnown = false; g_domainLogGateTimeline.clear();
    g_sensorDetailLogging = false; g_fanLogEnabled = false; configWrites = 0; domainRows = 0;
    g_testRoot = (root / name).wstring(); fspath::create_directories(g_testRoot);
    SG_FACT_LOG = (fspath::path(g_testRoot) / L"sleep-facts.log").wstring(); g_sgFacts.clear();
    g_inputCapturePath = (fspath::path(g_testRoot) / L"input-capture.jsonl").wstring();
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 && argc != 3) return 2;
    const fspath::path root(argv[1]);
    {
    reset(root,L"virtual-adaptation");kLogMaxBytes=1024*1024;g_powerGeneration=23;
    appendNativeLifecycleLog("input-host-switch-recovery",{{"powerGeneration",22},{"phase","stale"},{"requestId",37},{"hostEpoch",91},{"runId","old-run"},{"stopInvoked",false}});
    auto rows=readRows(L"native-lifecycle.log");
    check("virtual-old-captured-power-survives-current-writer-wrapper",rows.size()==1&&rows[0]["powerGeneration"]==23&&rows[0]["producerPowerGeneration"]==22&&rows[0]["observedPowerGeneration"]==23&&rows[0]["stopInvoked"]==false);
    VirtualEvidenceSourceFixture source;source.pad.wButtons=4096;source.pad.bLeftTrigger=128;t_commonFrameSource=&source;
    ymcc::virtualAdaptation::FrameStamp stamp{91,23,7,11,300,20,1234,false,true,true,true};stamp.captureBinding("old-run","Host-before-call");
    const std::string wire=R"({"command":"SUBMIT_FRAME","buttons":4096,"raw":{"bLeftTrigger":128},"runId":"old-run","epoch":91,"sequence":300,"startupNonce":"must-not-retain-this"})";
    for(unsigned i=0;i<50000;++i){stamp.commandSequence=300+i;stamp.wireSample=20+i;
        virtualAdaptationFrameReceipt(stamp,json::object(),wire,"first-request",{{"ok",true},{"startupNonce","must-not-retain-this"},{"frameReplyBinding","must-not-retain-this"}});}
    stamp.neutral=true;virtualAdaptationFrameReceipt(stamp,json::object(),wire,"neutral",json::object());
    stamp.accepted=false;virtualAdaptationFrameReceipt(stamp,json::object(),wire,"rejected",json::object());
    rows=readRows(L"virtual-adaptation.log");
    check("virtual-50000-frames-three-first-outcome-samples",rows.size()==4&&virtualAdaptationSelector().selected()==3);
    const auto& sample=rows[1]["firstSample"];
    check("virtual-frame-sample-preserves-exact-source-and-request",sample["source"]["pad"]["buttons"]==4096&&sample["source"]["pad"]["ltByte"]==128&&sample["replayRequest"]["buttons"]==4096&&sample["commandSequence"]==300&&rows[1]["uniqueKey"]["identity"]["hostInstanceId"]=="Host-before-call");
    check("virtual-transport-auth-not-logged-and-external-proof-false",sample.dump().find("must-not-retain-this")==std::string::npos&&!sample["replayRequest"].contains("startupNonce")&&!sample["nativeReceiptJson"].contains("frameReplyBinding")&&sample["hostSdkDeliveryProven"]==false&&sample["steamConsumptionProven"]==false);
    check("virtual-stage-original-power-retained",rows[0]["firstSample"]["producer"]["powerGeneration"]==22);
    check("virtual-independent-budget-and-no-per-write-rescan",virtualAdaptationStatus()["maxBytes"]==8ull*1024*1024&&virtualAdaptationJournal().status()["journalReadScans"]<=1);
    ymcc::virtualAdaptation::FrameSelector bounded;unsigned selected=0;
    for(unsigned context=1;context<=64;++context)for(unsigned flags=0;flags<8;++flags){auto value=stamp;value.epoch=context;value.neutral=flags&1;value.accepted=flags&2;value.sourceAdmitted=flags&4;selected+=bounded.select(value);}
    stamp.epoch=65;bool overflow=!bounded.select(stamp);stamp.epoch=0;bool unknown=!bounded.select(stamp);
    check("virtual-context-and-outcome-cap-no-eviction",selected==512&&overflow&&unknown&&bounded.capacityDrops()==1&&bounded.unknownDrops()==1);
    if(fspath::exists(root.parent_path()/L"sdk-first-response.json")){
        std::ifstream actualReply(root.parent_path()/L"sdk-first-response.json");json reply;actualReply>>reply;
        auto sdkStamp=stamp;sdkStamp.neutral=false;sdkStamp.accepted=true;
        virtualAdaptationFrameReceipt(sdkStamp,json::object(),wire,"bridge-request",reply);
        auto bridged=readRows(L"virtual-adaptation.log");
        check("virtual-actual-Host-SDK-first-witness-survives-native-sampler",bridged.size()==5&&bridged.back()["category"]=="virtual-sdk-first-witness"&&bridged.back()["firstSample"]["nativeReceiptJson"]["sdkFirstWitnesses"]==reply["sdkFirstWitnesses"]&&bridged.back()["firstSample"]["hostSdkDeliveryProven"]==false);
        reply["sdkFirstWitnesses"]=json::array();reply["sdkFirstWitnessCoverage"]={{"dropped",1},{"emissionFailures",0}};
        virtualAdaptationFrameReceipt(sdkStamp,json::object(),wire,"gap-1",reply);
        reply["sdkFirstWitnessCoverage"]["dropped"]=2;
        virtualAdaptationFrameReceipt(sdkStamp,json::object(),wire,"gap-2",reply);
        auto gapRows=readRows(L"virtual-adaptation.log");
        check("virtual-SDK-gap-only-updates-retain-changing-loss-counts",gapRows.size()==7&&gapRows.back()["firstSample"]["nativeReceiptJson"]["sdkFirstWitnessCoverage"]["dropped"]==2);

    }
    t_commonFrameSource=nullptr;
    appendNativeLifecycleLog("input-host-wake-execution",{{"stage","duplicate"},{"wakeSerial",1},{"originalPlayerDeadline",4000}});
    appendNativeLifecycleLog("input-host-wake-execution",{{"stage","retired"},{"wakeSerial",1},{"powerGeneration","unknown"},{"reason","original-wake-budget-expired"}});
    rows=readRows(L"native-lifecycle.log");
    check("virtual-missing-generation-stays-missing-and-unknown-stays-unknown",!rows[1].contains("producerPowerGeneration")&&rows[2]["producerPowerGeneration"]=="unknown"&&rows[2]["powerGeneration"]==23);
    auto evidence=readRows(L"virtual-adaptation.log");
    check("virtual-wake-duplicate-retains-original-budget",evidence[evidence.size()-2]["firstSample"]["producer"]["originalPlayerDeadline"]==4000&&evidence.back()["firstSample"]["producer"]["reason"]=="original-wake-budget-expired");
    bool protectionUnchanged=true,unknownRequest=true,badViewExpires=true;
    for(unsigned verifying=0;verifying<2;++verifying){
        ymcc::VirtualStartProtection currentProtection,badProtection;
        const auto currentAdmission=currentProtection.admit("steamdeck",1000);
        const auto badAdmission=badProtection.admit("steamdeck",1000);
        if(verifying){
            currentProtection.beginVerification(currentAdmission.requestId,"success",1002);
            badProtection.beginVerification(badAdmission.requestId,"success",1002);
            currentProtection.reserveFallback(currentAdmission.requestId,11002);
            badProtection.reserveFallback(badAdmission.requestId,11002);
        }
        const auto authorizedNow=verifying?12002ULL:1001ULL;
        const auto before=currentProtection.view(authorizedNow);json recorded;
        episodeLogProbe(currentProtection,500000,false,recorded);
        const auto after=currentProtection.view(authorizedNow);
        protectionUnchanged&=(before==after)&&after.fallbackAttempts==(verifying?1u:0u)&&after.phase==(verifying?"verifying":"pending");
        unknownRequest&=recorded["requestId"].is_null()&&recorded["requestIdScope"]=="unobserved;logger-does-not-read-start-protection";
        episodeLogProbe(badProtection,500000,true,recorded);
        badViewExpires&=badProtection.view(authorizedNow).phase=="expired";
    }
    check("episode-actual-log-block-preserves-real-class-pending-verifying-candidate-and-budget",protectionUnchanged);
    check("episode-actual-log-block-retains-unknown-request-without-state-read",unknownRequest);
    check("episode-negative-view-with-boot-clock-reproduces-premature-expiry",badViewExpires);
    const auto priorEpisodeRows=readRows(L"virtual-adaptation.log").size();
    const auto episodeA=inputHostRecoveryEpisodeLogRecord(7,91,"run-a","host-a",23,4,8,100);
    const auto episodeB=inputHostRecoveryEpisodeLogRecord(8,92,"run-b","host-b",23,6,9,110);
    const auto episodeA2=inputHostRecoveryEpisodeLogRecord(7,91,"run-a","host-a",23,4,8,100);
    const auto episodeA3=inputHostRecoveryEpisodeLogRecord(7,91,"run-a","host-a",23,4,8,100);
    for(unsigned i=0;i<50000;++i)virtualAdaptationObserveRecord("input-host-recovery-episode-new",episodeA);
    for(const auto& row:{episodeB,episodeA2,episodeA3})virtualAdaptationObserveRecord("input-host-recovery-episode-new",row);
    virtualAdaptationFlush();auto episodeRows=readRows(L"virtual-adaptation.log");
    check("virtual-episode-four-real-resets-not-collapsed-even-reused-context",episodeRows.size()==priorEpisodeRows+4&&episodeA["episodeId"]!=episodeA2["episodeId"]&&episodeA2["episodeId"]!=episodeA3["episodeId"]);
    check("virtual-episode-50000-replays-of-same-observation-retain-one",virtualAdaptationJournal().status()["budgetOmissions"]==0&&episodeRows.size()==priorEpisodeRows+4);
    const auto& episodeKey=episodeRows[priorEpisodeRows]["uniqueKey"]["identity"];
    check("virtual-episode-existing-context-and-writer-scope-retained",episodeKey["requestId"]==7&&episodeKey["hostEpoch"]==91&&episodeKey["runId"]=="run-a"&&episodeKey["hostInstanceId"]=="host-a"&&episodeKey["powerGeneration"]==23&&episodeKey["intentRevision"]==4&&episodeKey["presentationGeneration"]==8&&episodeKey["writerInstanceId"]==episodeA["writerInstanceId"]);
    const auto& metadata=episodeRows.back();const auto& cachedWriter=ymcc::sleepfactlog::writerIdentity();
    check("adaptation-journal-writer-birth-and-instance-come-from-existing-cache",metadata["writerPid"]==cachedWriter.pid&&metadata["writerProcessCreation100ns"]==std::to_string(cachedWriter.creation100ns)&&metadata["writerInstanceId"]==episodeA["writerInstanceId"]);
    bool sequenceIncreasing=true;for(size_t i=1;i<episodeRows.size();++i)sequenceIncreasing&=episodeRows[i]["writerSequence"].get<unsigned long long>()>episodeRows[i-1]["writerSequence"].get<unsigned long long>();
    check("adaptation-journal-sequence-is-process-scoped-and-outside-hardware-key",sequenceIncreasing&&metadata["writerSequenceScope"]=="adaptation-journals-in-process;record-creation;gaps-allowed"&&!metadata["uniqueKey"].contains("writerSequence")&&!metadata["uniqueKey"].contains("writerProcessCreation100ns"));

    const auto beforeStartup=readRows(L"virtual-adaptation.log").size();
    json slot={{"slot",0},{"stage","target-mismatch"},{"stateResult",0},{"confirmResult",1167},{"mappedSlot",255},
        {"path","cached-a"},{"stateAttempted",true},{"pathAttempted",true},{"targetInterfaceAttempted",true},
        {"targetInterfaceMatched",false},{"indexAttempted",false},{"confirmAttempted",false}};
    json startup={{"phase","xinput-missing"},{"attempt",1},{"retry",false},{"elapsedMs",500},{"polls",10},
        {"targetHid","target-a"},{"targetRoot","root-a"},{"nodePresent",true},{"statusRead",true},{"nodeStatus",10},{"nodeProblem",0},
        {"slotReadback",json::array({slot})}};
    g_domainLogEnabled[1]=false;
    appendNativeLifecycleLog("input-host-xbox-startup",startup);
    auto nativeStartup=readRows(L"native-lifecycle.log").back();
    check("virtual-startup-six-observation-flags-preserved-in-detail-off-native",nativeStartup["slotReadback"]==startup["slotReadback"]&&nativeStartup["phase"]=="xinput-missing"&&!g_domainLogEnabled[1]);
    json indexed=startup;indexed["slotReadback"][0]["targetInterfaceMatched"]=true;
    indexed["slotReadback"][0]["indexAttempted"]=true;indexed["slotReadback"][0]["stage"]="index-mismatch";
    json confirmed=indexed;confirmed["slotReadback"][0]["mappedSlot"]=0;
    confirmed["slotReadback"][0]["confirmAttempted"]=true;confirmed["slotReadback"][0]["stage"]="readback-changed";
    const json shapes[]={startup,indexed,confirmed};
    for(unsigned i=0;i<50000;++i){auto observed=shapes[i%3];observed["elapsedMs"]=500+i;observed["polls"]=10+i;
        observed["slotReadback"][0]["path"]="cached-"+std::to_string(i);
        virtualAdaptationObserveRecord("input-host-xbox-startup",observed);}
    virtualAdaptationFlush();auto startupRows=readRows(L"virtual-adaptation.log");
    check("virtual-startup-50000-observations-three-check-shapes-only",startupRows.size()==beforeStartup+3);
    const auto& firstStartup=startupRows[beforeStartup];const auto& startupIdentity=firstStartup["uniqueKey"]["identity"];
    check("virtual-startup-unknown-operation-and-original-fields-retained",firstStartup["firstSample"]["producer"]==startup&&firstStartup["firstSample"]["operationContextKnown"]==false);
    const auto& startupSlot=startupIdentity["startupReadbackShape"]["slotReadback"][0];
    check("virtual-startup-unattempted-default-and-attempted-failure-distinct",startupSlot["confirmAttempted"]==false&&startupSlot["confirmResult"]==1167&&startupRows[beforeStartup+2]["uniqueKey"]["identity"]["startupReadbackShape"]["slotReadback"][0]["confirmAttempted"]==true&&startupRows[beforeStartup+2]["uniqueKey"]["identity"]["startupReadbackShape"]["slotReadback"][0]["confirmResult"]==1167);
    check("virtual-startup-key-excludes-clock-poll-and-path",!startupIdentity.contains("elapsedMs")&&!startupIdentity.contains("polls")&&!startupSlot.contains("path"));
    json legacy=startup;for(const auto* flag:{"stateAttempted","pathAttempted","targetInterfaceAttempted","targetInterfaceMatched","indexAttempted","confirmAttempted"})legacy["slotReadback"][0].erase(flag);
    virtualAdaptationObserveRecord("input-host-xbox-startup",legacy);virtualAdaptationFlush();startupRows=readRows(L"virtual-adaptation.log");
    check("virtual-startup-missing-attempt-flags-remain-unknown",startupRows.size()==beforeStartup+4&&startupRows.back()["uniqueKey"]["identity"]["startupReadbackShape"]["slotReadback"][0]["confirmAttempted"].is_null()&&!startupRows.back()["firstSample"]["producer"]["slotReadback"][0].contains("confirmAttempted"));
    for(unsigned request:{32,33}){auto observed=startup;observed["requestId"]=request;virtualAdaptationObserveRecord("input-host-xbox-startup",observed);}
    virtualAdaptationFlush();startupRows=readRows(L"virtual-adaptation.log");
    check("virtual-startup-explicit-request-context-preserved-when-provided",startupRows.size()==beforeStartup+6&&startupRows.back()["uniqueKey"]["identity"]["requestId"]==33&&startupRows.back()["firstSample"]["operationContextKnown"]==true);
    auto nodeChanged=startup;nodeChanged["nodeProblem"]=28;
    virtualAdaptationObserveRecord("input-host-xbox-startup",nodeChanged);virtualAdaptationFlush();startupRows=readRows(L"virtual-adaptation.log");
    check("virtual-startup-node-problem-changes-not-collapsed",startupRows.size()==beforeStartup+7&&startupRows.back()["uniqueKey"]["identity"]["startupReadbackShape"]["nodeProblem"]==28);


    }
    using ymcc::logging::NativeLane;
    using ymcc::logging::nativeLane;
    check("lane-policy-base-stays-event-on-and-off",
        nativeLane(true, false, false) == NativeLane::event && nativeLane(true, false, true) == NativeLane::event);
    check("lane-policy-details-off-suppressed-on-detailed",
        nativeLane(false, false, false) == NativeLane::suppressed && nativeLane(false, false, true) == NativeLane::detail);
    check("lane-policy-successful-frame-never-base",
        nativeLane(true, true, false) == NativeLane::suppressed && nativeLane(true, true, true) == NativeLane::detail);
    check("normal-build-compiles-ROG-FSE-adaptation-diagnostics-out",YMCC_ROG_FSE_DIAGNOSTICS==0);
    check("ROG-raw-input-window-cannot-be-resident",!nativeLifecycleEventIsBase("rog-raw-input-window"));
    for(bool detailed : {false,true}) {
        reset(root,detailed?L"r114-evidence-detail-on":L"r114-evidence-detail-off");
        g_domainLogEnabled[1]=detailed;
        check("R114 bounded diagnostics classified as base",nativeLifecycleEventIsBase("input-host.native-helper-diagnostic")&&nativeLifecycleEventIsBase("hidhide.owned-game-reconnect-plan"));
        appendNativeLifecycleLog("input-host.native-helper-diagnostic",{{"helperExitCode",8},{"helperDiagnostic",{{"state","record-accepted"}}}});
        appendNativeLifecycleLog("hidhide.owned-game-reconnect-plan",{{"discoveryAttempts",1},{"driverWrite",false}});
        flushNativeLifecycleLogs(true);
        const auto rows=readRows(L"native-lifecycle.log");
        check("R114 helper and owned-plan facts reach ordinary lifecycle file",rows.size()==2&&rows[0].value("event",std::string())=="input-host.native-helper-diagnostic"&&rows[1].value("event",std::string())=="hidhide.owned-game-reconnect-plan");
        check("R114 bounded events do not become a detailed stream",readRows(L"native-detail.log").empty());
    }
    for(bool detailed : {false,true}) {
        reset(root,detailed?L"onex-start-detail-on":L"onex-start-detail-off");
        g_domainLogEnabled[1]=detailed;
        check("OneX Start exact stage event is ordinary while forged names stay unknown",
            nativeLifecycleEventIsBase("input-host-onex-start-refresh") &&
            !nativeLifecycleEventIsBase("input-host-onex-start-refresh-forged"));
        const json proof={{"stage","completed"},{"requestId",17},{"persona","steamdeck"},{"runId","fixture-onex"},
            {"freshSameRootSource",true},{"driverOperationMayHaveIssued",true},
            {"externalSuppressionVerified",false},{"filterAttachmentProven",false},
            {"newTargetCreatedHere",false},{"sourceInterfacePathKnown",true},{"reverseBindingApiResolved",true},
            {"repairRevision","VC-ONEX-093203-R1"},{"locationPathCount",1},
            {"usbTopology",{{"desiredAccess",0},{"portKind",-1},{"reason","hop-unproven"},
                {"hops",json::array({{{"port",4},{"openError",5},{"reason","hub-open-failed"}}})}}}};
        appendNativeLifecycleLog("input-host-onex-start-refresh",proof);flushNativeLifecycleLogs(true);
        const auto rows=readRows(L"native-lifecycle.log");
        check("OneX Start actual writer retains the bound identity and limited evidence fields",
            rows.size()==1 && rows[0].value("event",std::string{})=="input-host-onex-start-refresh" &&
            rows[0].value("requestId",0)==17 && rows[0].value("runId",std::string{})=="fixture-onex" &&
            rows[0].value("persona",std::string{})=="steamdeck" && rows[0].value("freshSameRootSource",false) &&
            rows[0].value("driverOperationMayHaveIssued",false) &&
            !rows[0].value("externalSuppressionVerified",true) && !rows[0].value("filterAttachmentProven",true) &&
            !rows[0].value("newTargetCreatedHere",true) && rows[0].value("sourceInterfacePathKnown",false) &&
            rows[0].value("reverseBindingApiResolved",false));
        check("OneX093203 actual ordinary writer retains nested query failure evidence without asserting internal hardware",
            rows.size()==1 && rows[0].value("repairRevision",std::string{})=="VC-ONEX-093203-R1" &&
            rows[0].value("locationPathCount",0)==1 && rows[0]["usbTopology"].value("desiredAccess",999)==0 &&
            rows[0]["usbTopology"].value("portKind",0)==-1 && rows[0]["usbTopology"]["hops"][0].value("openError",0)==5 &&
            rows[0]["usbTopology"]["hops"][0].value("reason",std::string{})=="hub-open-failed");
        check("OneX bounded Start stages do not create a detailed stream",readRows(L"native-detail.log").empty());
    }
    for(bool detailed : {false,true}) {
        reset(root,detailed?L"r117-slot-detail-on":L"r117-slot-detail-off");
        g_domainLogEnabled[1]=detailed;++g_powerGeneration;
        const json assignment={{"path","fixture-original-source"},{"targetSlot",0},{"result",0},{"actualSlot",0},{"accepted",true}};
        const json guarded={{"primarySlot",0},{"probeSlot",1},{"assigned",true},{"accepted",true},{"intentRevisionAtQuery",4}};
        for(int n=0;n<8;++n){appendNativeLifecycleLog("gamepad-openxinput-assign-slot",assignment);appendNativeLifecycleLog("gamepad-openxinput-slot-guard",guarded);}
        flushNativeLifecycleLogs(true);
        check("R117 actual assignment and guard changes reach ordinary log",readRows(L"native-lifecycle.log").size()==2);
        check("R117 repeated results stay detail-only",readRows(L"native-detail.log").size()==(detailed?14u:0u));
        auto failure=assignment;failure["accepted"]=false;failure["result"]=1167;appendNativeLifecycleLog("gamepad-openxinput-assign-slot",failure);
        auto newIntent=guarded;newIntent["intentRevisionAtQuery"]=6;appendNativeLifecycleLog("gamepad-openxinput-slot-guard",newIntent);
        flushNativeLifecycleLogs(true);check("R117 outcome and intent transitions retained",readRows(L"native-lifecycle.log").size()==4);
    }
    reset(root, L"snapshot-routing"); kLogMaxBytes = 5 * 1024 * 1024;
    g_domainLogEnabled[1] = true;
    for (int i=0;i<296;++i) appendNativeLifecycleLog("rog.hid-bound", {{"matched", false}, {"bindingPathHash", 1}});
    check("repeated-rog-binding-first-event-rest-detailed", readRows(L"native-lifecycle.log").size()==1 && readRows(L"native-detail.log").size()==295);
    for (int i=0;i<64;++i) appendNativeLifecycleLog("rog.hid-discovery", {{"path", i}, {"report5A", true}});
    check("per-interface-rog-discovery-never-enters-low-frequency", readRows(L"native-lifecycle.log").size()==1 && readRows(L"native-detail.log").size()==359);
    appendNativeLifecycleLog("rog.hid-discovery-summary", {{"matches", 3}, {"enumerationComplete", true}});
    check("rog-discovery-summary-first-observation-is-low-frequency", readRows(L"native-lifecycle.log").size()==2);
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", true}, {"bindingPathHash", 1}});
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", true}, {"bindingPathHash", 2}});
    ++g_powerGeneration;
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", true}, {"bindingPathHash", 2}});
    check("binding-result-path-and-power-generation-edges-stay-event", readRows(L"native-lifecycle.log").size()==5);
    g_domainLogEnabled[1] = false;
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", true}, {"bindingPathHash", 2}});
    appendNativeLifecycleLog("rog.hid-bound", {{"matched", false}, {"bindingPathHash", 0}});
    check("snapshot-detail-off-does-not-hide-transitions", readRows(L"native-lifecycle.log").size()==6 && readRows(L"native-detail.log").size()==359);
    appendNativeLifecycleLog("gamepad.feedbackCaps", {{"ledAvailable", true}});
    appendNativeLifecycleLog("gamepad.feedbackCaps", {{"ledAvailable", true}});
    appendNativeLifecycleLog("gamepad.feedbackCaps", {{"ledAvailable", false}});
    check("feedback-capability-first-change-not-duplicate-stays-event", readRows(L"native-lifecycle.log").size()==8);
    appendNativeLifecycleLog("http-request-failed", {{"deadlineExpired", true}});
    appendNativeLifecycleLog("screen-standalone-special", {{"status", "completed"}});
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.closed"}});
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.painted"}});
    check("new-errors-actions-and-hud-boundaries-survive-default-off", readRows(L"native-lifecycle.log").size()==11);
    g_domainLogEnabled[1] = true;
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.painted"}, {"seq", 42}});
    check("hud-paint-is-detailed-with-original-sequence", readRows(L"native-detail.log").size()==360 && readRows(L"native-detail.log").back()["seq"]==42);
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.unavailable"}, {"hud", {{"reason", "content-too-large"}, {"seq", 1}}}});
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.unavailable"}, {"hud", {{"reason", "content-too-large"}, {"seq", 2}}}});
    appendNativeLifecycleLog("desktop-hud-event", {{"hudEvent", "desktopHud.unavailable"}, {"hud", {{"reason", "target-exited"}, {"seq", 3}}}});
    check("hud-repeated-unavailable-sequence-is-detailed-reason-edge-resident",readRows(L"native-lifecycle.log").size()==13 && readRows(L"native-detail.log").size()==361 && readRows(L"native-detail.log").back()["hud"]["seq"]==2);
    reset(root, L"sleep-device-routing"); kLogMaxBytes = 5 * 1024 * 1024;
    g_domainLogEnabled[1] = true;
    for (int i=0;i<373;++i) sgRecordFact("device-change", {{"code",7},{"userStandby",false},{"standbyAgeEligible",false},{"standbyAgeMs",i}});
    sgRecordFact("device-change", {{"code",32768},{"userStandby",false}});
    sgRecordFact("device-change", {{"code",32772},{"userStandby",false}});
    sgRecordFact("device-change", {{"code",7},{"userStandby",true}});
    sgRecordFact("device-change", {{"code",7},{"standbyAgeEligible",true}});
    sgRecordFact("power-broadcast", {{"code",7}});
    const auto treeRows=readRows(L"native-detail.log");
    check("generic-tree-notifications-detailed-no-row-or-field-loss",treeRows.size()==373 && treeRows.back()["details"]["standbyAgeMs"]==372 && treeRows.front()["factEvent"]=="device-change");
    check("arrival-removal-standby-and-power-boundaries-stay-sleep-facts",g_sgFacts.size()==5 && readRows(L"sleep-facts.log").size()==5);
    const auto sleepIdentityRows=readRows(L"sleep-facts.log");
    const auto& firstSleep=sleepIdentityRows.front();const auto& lastSleep=sleepIdentityRows.back();
    check("sleep-facts-explicit-writer-process-creation-not-subject-pid",firstSleep["writerPid"]==GetCurrentProcessId()&&firstSleep["writerInstanceId"].is_string()&&firstSleep["writerProcessCreation100ns"].is_string()&&firstSleep["writerIdentityStatus"]=="pid-and-process-creation-known");
    check("sleep-facts-local-legacy-clock-preserved-new-UTC-labeled",firstSleep["time"]==sgFactTimestamp()&&firstSleep["timestampUtc"].get<std::string>().back()=='Z'&&firstSleep["timestampLocal"].is_string()&&firstSleep["utcEpochMs"].is_number_unsigned()&&firstSleep["observedTickMs"].is_number_unsigned());
    check("sleep-fact-sequence-follows-actual-journal-order",lastSleep["writerSequence"].get<unsigned long long>()==firstSleep["writerSequence"].get<unsigned long long>()+4);
    check("sleep-fact-generation-observer-scope-is-explicit",firstSleep["writerPowerGeneration"]==currentPowerGeneration()&&firstSleep["factSchemaVersion"]==2&&firstSleep["observationScope"]=="journal-record-creation;not-kernel-event-time-or-action-completion");
    auto originalFacts=json{{"generation",999},{"writerPid",987654},{"time","original-kernel-time"},{"result",false}};
    sgRecordFact("sleep-action-failed",originalFacts);
    const auto failedFact=readRows(L"sleep-facts.log").back();
    check("sleep-fact-details-preserved-and-subject-identity-not-promoted",failedFact["details"]==originalFacts&&failedFact["writerPid"]==GetCurrentProcessId()&&failedFact["writerPowerGeneration"]==currentPowerGeneration());
    // Restore only this fixture's extra row; original production assertions remain unchanged.
    g_sgFacts.pop_back();{std::ofstream f(fspath::path(SG_FACT_LOG),std::ios::binary|std::ios::trunc);for(const auto& row:sleepIdentityRows)f<<row.dump()<<'\n';}

    g_domainLogEnabled[1]=false;
    sgRecordFact("device-change", {{"code",7},{"userStandby",true}});
    check("standby-relevant-device-event-survives-detail-off",g_sgFacts.size()==6);
    // REQUIRED default-log contract for sleep repair; no frequent log opt-in.
    reset(root, L"sleep-required-evidence-off");
    unsigned requiredSleepFacts=0;
    for(const char* event:{"user-initiated-sleep","abnormal-sleep-live-source-start","abnormal-sleep-live-source-gap",
        "abnormal-sleep-armed","abnormal-sleep-evidence-state","abnormal-sleep-deadline","abnormal-sleep-request",
        "abnormal-sleep-request-result","abnormal-sleep-owned-retry-result","abnormal-sleep-finished",
        "s0-low-power-residency","static-wake-delay-armed","static-wake-delay-canceled","static-wake-scan-start",
        "static-wake-scan-result","static-wake-restore-result"}) {
        sgRecordFact(event,{{"mandatoryEvidence",true},{"detailLogsRequired",false}});++requiredSleepFacts;
    }
    const auto mandatorySleepRows=readRows(L"sleep-facts.log");
    check("required-sleep-source-decision-action-cancel-facts-survive-default-detail-off",mandatorySleepRows.size()==requiredSleepFacts&&g_sgFacts.size()==requiredSleepFacts);
    check("required-sleep-evidence-preserves-event-and-fields",mandatorySleepRows.front()["event"]=="user-initiated-sleep"&&mandatorySleepRows.back()["event"]=="static-wake-restore-result"&&mandatorySleepRows.back()["details"]["detailLogsRequired"]==false);
    check("required-sleep-evidence-does-not-enable-high-frequency-stream",!fspath::exists(fspath::path(g_testRoot)/L"native-detail.log")&&!g_domainLogEnabled[1]);
    check("static-summary-retained-errors-are-not-labeled-fresh-failures",mandatorySleepRows.back()["payloadSemantics"]["currentItemReadbackVerified"].is_null()&&mandatorySleepRows.back()["payloadSemantics"]["actualWritesThisOperation"].is_null()&&mandatorySleepRows.back()["details"]["mandatoryEvidence"]==true);


    reset(root, L"off");
    appendNativeLifecycleLog("app-exit-ipc", {{"fixture", true}});
    for (int i=0; i<1000; ++i) appendNativeLifecycleLog("real-stick-frame", {{"index", i}});
    check("off-base-immediately-readable", readRows(L"native-lifecycle.log").size() == 1);
    check("off-details-no-file-no-domain-mirror", !fspath::exists(fspath::path(g_testRoot)/L"native-detail.log") && domainRows == 0);
    reset(root, L"on"); g_domainLogEnabled[0] = true; g_domainLogEnabled[1] = true;
    appendNativeLifecycleLog("gyro-calibration-request", {{"fixture", true}});
    for (int i=0; i<40; ++i) appendNativeLifecycleLog("real-stick-frame", {{"index", i}});
    check("on-low-file-only-base-events", readRows(L"native-lifecycle.log").size() == 1);
    const auto detailed = readRows(L"native-detail.log");
    check("on-detail-preserves-all-rows-and-order", detailed.size() == 40 && detailed.front()["index"] == 0 && detailed.back()["index"] == 39);
    check("on-domain-mirror-preserved", domainRows == 41);
    reset(root, L"frames-off");
    for(int i=0;i<1000;++i) appendNativeLifecycleLog("input-host-command-result", {{"command", "SUBMIT_FRAME"}, {"ok", true}, {"sequence", i}});
    check("successful-frame-off-neither-lane-created", !fspath::exists(fspath::path(g_testRoot)/L"native-lifecycle.log") && !fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"));
    appendNativeLifecycleLog("input-host-command-result", {{"command", "SUBMIT_FRAME"}, {"ok", false}, {"phase", "transport"}});
    check("frame-error-remains-visible-with-detail-off", readRows(L"native-lifecycle.log").size() == 1);
    reset(root, L"frames-on"); g_domainLogEnabled[1] = true;
    appendNativeLifecycleLog("input-host-command-result", {{"command", "SUBMIT_FRAME"}, {"ok", true}});
    check("successful-frame-on-detailed-not-base", readRows(L"native-detail.log").size() == 1 && !fspath::exists(fspath::path(g_testRoot)/L"native-lifecycle.log"));
    reset(root, L"rate");
    ymcc::logging::BusRateLogState rate;
    unsigned edges = 0;
    for(int i=0;i<500;++i) edges += rate.observe(0);
    const bool normalSilent = edges == 0;
    for(int i=0;i<500;++i) {
        if(rate.observe(1)) { ++edges; appendNativeLifecycleLog("input-capture-bus-rate", {{"rateFlags", 1}}); }
    }
    check("rate-normal-silent-persistent-anomaly-one-event", normalSilent && edges == 1 && readRows(L"native-lifecycle.log").size() == 1);
    edges += rate.observe(3); edges += rate.observe(2); edges += rate.observe(0);
    for(int i=0;i<500;++i) edges += rate.observe(0);
    check("rate-category-change-and-recovery-once", edges == 4);
    reset(root, L"catalog-off");
    appendNativeLifecycleLog("oem-key-catalog-query", {{"count", 0}});
    check("read-only-catalog-off-not-base", !fspath::exists(fspath::path(g_testRoot)/L"native-lifecycle.log") && !fspath::exists(fspath::path(g_testRoot)/L"native-detail.log"));
    reset(root, L"rotation");
    {std::ofstream f(fspath::path(g_testRoot)/L"native-lifecycle.log"); f << std::string(300, 'x') << '\n';}
    g_nativeLifecycleFrameWrites = 255;
    appendNativeLifecycleLog("app-exit-ipc", {{"rotation", true}});
    check("base-rotation-retains-backup-and-current-row", fspath::exists(fspath::path(g_testRoot)/L"native-lifecycle.log.1") && readRows(L"native-lifecycle.log").size() == 1);
    reset(root, L"blocked-backup");
    {std::ofstream f(fspath::path(g_testRoot)/L"native-lifecycle.log"); f << "retained-before\n" << std::string(300, 'x') << '\n';}
    const auto backup = fspath::path(g_testRoot)/L"native-lifecycle.log.1";
    HANDLE held = CreateFileW(backup.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    g_nativeLifecycleFrameWrites = 255;
    appendNativeLifecycleLog("app-exit-ipc", {{"rotation", true}});
    std::ifstream active(fspath::path(g_testRoot)/L"native-lifecycle.log"); std::stringstream contents; contents << active.rdbuf();
    check("blocked-backup-preserves-active-and-new-event", held != INVALID_HANDLE_VALUE && contents.str().find("retained-before") == 0 && contents.str().find("app-exit-ipc") != std::string::npos);
    active.close(); if(held != INVALID_HANDLE_VALUE) CloseHandle(held);
    reset(root, L"fan-toggle");
    const json enabled = fanToggle({{"enabled", true}});
    const auto costFlag = fspath::path(g_testRoot)/L"input-host-cost-enabled.flag";
    check("fan-entry-on-syncs-authoritative-and-derived-gates", enabled["enabled"] == true && g_domainLogEnabled[0] && g_domainLogEnabled[1] && g_sensorDetailLogging && fspath::exists(costFlag));
    appendNativeLifecycleLog("real-stick-frame", {{"partialBatch", true}});
    fanToggle({{"enabled", false}});
    const auto rowsAfterOff = readRows(L"native-detail.log");
    check("fan-entry-off-flushes-and-closes-detail-stream", rowsAfterOff.size() == 1 && !g_nativeDetailLog.is_open());
    check("fan-entry-off-clears-sensor-detail-and-cost-flag", !g_domainLogEnabled[0] && !g_domainLogEnabled[1] && !g_sensorDetailLogging && !fspath::exists(costFlag));
    appendNativeLifecycleLog("real-stick-frame", {{"afterOff", true}});
    check("off-does-not-reopen-detail-stream", !g_nativeDetailLog.is_open() && readRows(L"native-detail.log").size() == 1);
    const unsigned writesBeforeRead = configWrites; fanToggle(json::object());
    check("fan-entry-read-only-does-not-save-or-enable", configWrites == writesBeforeRead && !g_sensorDetailLogging && !fspath::exists(costFlag));
    reset(root, L"family-imu"); kLogMaxBytes = 5 * 1024 * 1024;
    for(int i=0;i<1000;++i) familyImuLog(1,2,3,4,5,6);
    check("family-imu-off-no-json-queue-or-file", g_inputCaptureSampleQueue.empty() && !fspath::exists(g_inputCapturePath));
    g_sensorDetailLogging = true;
    for(int i=0;i<16;++i) familyImuLog(i,2,3,4,5,6);
    check("family-imu-on-queues-without-bus-disk-io", g_inputCaptureSampleQueue.size() == 16 && !fspath::exists(g_inputCapturePath));
    inputCaptureDrainQueuedSamples();
    const auto imuRows = readRows(L"input-capture.jsonl");
    check("family-imu-writer-preserves-values-and-sample-batching", imuRows.size() == 16 && g_inputCaptureSampleWrites == 16 && imuRows.front()["dps"]["x"] == 0 && imuRows.back()["dps"]["x"] == 15);
    check("sample-writer-pid-preserves-sample-values", imuRows.front()["writerPid"] == GetCurrentProcessId());
    inputCaptureAppend({{"kind","identity-fixture"},{"pid",12345},{"writerPid",0},{"t",98765}});
    const auto identitySample = readRows(L"input-capture.jsonl").back();
    check("sample-writer-pid-does-not-relabel-business-pid-or-producer-time",
        identitySample["writerPid"] == GetCurrentProcessId() && identitySample["pid"] == 12345 && identitySample["t"] == 98765);
    check("sample-kind-policy-does-not-batch-lifecycle-events", ymcc::logging::inputSampleKind("sample") && ymcc::logging::inputSampleKind("controller-family-imu") && !ymcc::logging::inputSampleKind("stop") && !ymcc::logging::inputSampleKind("worker-exception"));
    reset(root, L"queue-bound"); g_sensorDetailLogging = true;
    for(int i=0;i<300;++i) familyImuLog(i,2,3,4,5,6);
    check("family-imu-queue-bound-and-drop-counter-retained", g_inputCaptureSampleQueue.size() == 256 && g_inputCaptureSampleQueueDrops == 44 && g_inputCaptureSampleQueue.front()["dps"]["x"] == 44 && !fspath::exists(g_inputCapturePath));
    reset(root, L"discovery-summary-routing"); g_domainLogEnabled[1] = true;
    ++g_powerGeneration;
    const json absent = {{"schema",2},{"matches",0},{"enumerationComplete",true},{"enumerationError",0},
        {"interfaceInspectionCoverage","not-measured"},{"physicalAbsenceProven",false}};
    // Replay the supplied archive's repeated-absence groups (43/22/42/21).
    for (int count : {43,22,42,21}) {
        ++g_powerGeneration;
        for (int i=0;i<count;++i) appendNativeLifecycleLog("rog.hid-discovery-summary", absent);
    }
    flushNativeLifecycleLogs(false);
    check("archive-128-summary-observations-keep-four-edges-and-124-detail-rows",
        readRows(L"native-lifecycle.log").size()==4 && readRows(L"native-detail.log").size()==124);
    g_domainLogEnabled[1] = false;
    for(int i=0;i<64;++i) {
        appendNativeLifecycleLog("rog.hid-discovery-summary", absent);
        appendNativeLifecycleLog("rog.hid-discovery", {{"path","fake-hid-path"}});
    }
    check("gate-off-no-repeated-summary-or-interface-detail-written",
        readRows(L"native-lifecycle.log").size()==4 && readRows(L"native-detail.log").size()==124);
    json enumerationFailure = absent; enumerationFailure["enumerationComplete"]=false; enumerationFailure["enumerationError"]=5;
    appendNativeLifecycleLog("rog.hid-discovery-summary", enumerationFailure);
    appendNativeLifecycleLog("rog.hid-discovery-summary", enumerationFailure);
    enumerationFailure["enumerationError"]=6; appendNativeLifecycleLog("rog.hid-discovery-summary", enumerationFailure);
    appendNativeLifecycleLog("rog.hid-discovery-summary", absent);
    ++g_powerGeneration; appendNativeLifecycleLog("rog.hid-discovery-summary", absent);
    check("gate-off-keeps-failure-change-recovery-and-new-power-generation",
        readRows(L"native-lifecycle.log").size()==8 && readRows(L"native-detail.log").size()==124);
    reset(root, L"rear-observation-routing"); g_domainLogEnabled[1] = false;
    appendNativeLifecycleLog("msi-rear-source-observation", {{"snapshotReason","context-change"},{"rearDemand",true}});
    for(int i=0;i<48;++i) appendNativeLifecycleLog("msi-rear-source-observation", {{"snapshotReason","raw-input-change"},{"dinputSamples",i}});
    check("rear-context-stays-low-but-activity-counters-off-stay-out",
        readRows(L"native-lifecycle.log").size()==1 && readRows(L"native-detail.log").empty());
    g_domainLogEnabled[1] = true;
    for(int i=0;i<48;++i) appendNativeLifecycleLog("msi-rear-source-observation", {{"snapshotReason","raw-input-change"},{"dinputSamples",i}});
    appendNativeLifecycleLog("msi-rear-source-observation", {{"snapshotReason","context-change"},{"rearDemand",false}});
    flushNativeLifecycleLogs(false);
    const auto activityRows = readRows(L"native-detail.log");
    check("rear-activity-on-retains-all-48-counter-receipts-in-detail",
        readRows(L"native-lifecycle.log").size()==2 && activityRows.size()==48 &&
        activityRows.front()["dinputSamples"]==0 && activityRows.back()["dinputSamples"]==47);
    reset(root, L"dedicated-memory-capability");
    json memoryFailed{{"schema",1},{"ok",false},{"available",false},{"reason","[adapter.binding] fixture unread"}};
    for(int i=0;i<500;++i)record_gpu_dedicated_memory_capability(memoryFailed);
    auto capabilityRows=readRows(L"native-lifecycle.log");
    check("gpu-capability-default-logging-retains-one-first-failed-read",
        !g_domainLogEnabled[1] && capabilityRows.size()==1 && readRows(L"native-detail.log").empty() &&
        capabilityRows[0]["event"]=="machine-capability" && capabilityRows[0]["capability"]=="gpu.dedicatedMemory");
    check("gpu-capability-failed-read-is-unconfirmed-with-model-and-reason-not-invented-zero",
        capabilityRows[0]["status"]=="unconfirmed" && capabilityRows[0]["available"]==false &&
        capabilityRows[0]["deviceClass"]=="HandheldCompanion.Devices.ClawCG3EM" &&
        capabilityRows[0]["reason"]==memoryFailed["reason"] && !capabilityRows[0].contains("dedicatedBytes"));
    json memoryRead{{"schema",1},{"ok",true},{"available",true},{"reason",""},{"provider","intel-gmm"},
        {"capacityRead",true},{"dedicatedBytes",4096ull*1024*1024},{"adapterName","Intel fixture"},
        {"driverVersion","fixture"},{"bindingSource","kernel-pnp"},{"selected","auto"},
        {"options",json::array({json{{"id","auto"}}})}};
    for(int i=0;i<500;++i)record_gpu_dedicated_memory_capability(memoryRead);
    capabilityRows=readRows(L"native-lifecycle.log");
    check("gpu-capability-recovery-retains-real-capacity-binding-and-options-once",
        capabilityRows.size()==2 && capabilityRows[1]["available"]==true &&
        capabilityRows[1]["dedicatedBytes"]==memoryRead["dedicatedBytes"] &&
        capabilityRows[1]["capacityRead"]==true && capabilityRows[1]["bindingSource"]=="kernel-pnp" &&
        capabilityRows[1]["options"]==memoryRead["options"]);
    memoryRead["dedicatedBytes"]=0;record_gpu_dedicated_memory_capability(memoryRead);
    capabilityRows=readRows(L"native-lifecycle.log");
    check("gpu-capability-confirmed-zero-remains-an-actual-zero",
        capabilityRows.size()==3 && capabilityRows[2]["available"]==true && capabilityRows[2]["dedicatedBytes"]==0);
    g_domainLogEnabled[1]=true;record_gpu_dedicated_memory_capability(memoryRead);g_domainLogEnabled[1]=false;
    check("gpu-capability-detail-toggle-does-not-duplicate-unchanged-capability",readRows(L"native-lifecycle.log").size()==3);
    record_gpu_dedicated_memory_capability(memoryFailed);capabilityRows=readRows(L"native-lifecycle.log");
    check("gpu-capability-new-read-failure-does-not-retain-stale-capacity",
        capabilityRows.size()==4 && capabilityRows[3]["available"]==false && !capabilityRows[3].contains("dedicatedBytes"));
    if(argc==3){
        reset(root,L"dedicated-memory-production-responses");kLogMaxBytes=5*1024*1024;
        std::ifstream input(fspath::path(argv[2]),std::ios::binary);const auto windows=json::parse(input);
        bool preserved=true,deduplicated=true;
        for(const auto& response:windows["responses"]){
            record_gpu_dedicated_memory_capability(response);const auto rows=readRows(L"native-lifecycle.log");
            if(rows.empty()){preserved=false;continue;}
            const auto& row=rows.back();const bool available=response.value("ok",false)&&response.value("available",false);
            preserved=preserved&&row["event"]=="machine-capability"&&row["available"]==available&&
                row["status"]==(available?"available":"unconfirmed")&&row["reason"]==response["reason"]&&
                row["deviceClass"]=="HandheldCompanion.Devices.ClawCG3EM";
            for(const auto* key:{"readerRevision","dedicatedBytes","capacityRead","bindingSource","classificationSource","selected","verification","bootId","savedBootId","bootComparison","pendingOption","options","readDiagnostics","writeDiagnostics"})
                preserved=preserved&&(row.contains(key)==response.contains(key))&&(!response.contains(key)||row[key]==response[key]);
            record_gpu_dedicated_memory_capability(response);
            deduplicated=deduplicated&&readRows(L"native-lifecycle.log").size()==rows.size();
        }
        check("gpu-capability-production-Windows-results-preserve-actual-readback-and-failure-with-detail-off",preserved&&readRows(L"native-detail.log").empty());
        check("gpu-capability-production-Windows-results-repeat-does-not-duplicate",deduplicated);
    }
    reset(root, L"oem-wake-pass-routing"); g_domainLogEnabled[1] = true;
    ++g_powerGeneration;
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-started"},{"generation",77}});
    for(int i=0;i<10;++i) appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},
        {"ok",true},{"requestedEpoch",i+3},{"durationMs",i*2},{"intervalMs",2000}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-ended"},{"generation",77}});
    flushNativeLifecycleLogs(false);
    auto wakeDetail = readRows(L"native-detail.log");
    check("wake-guard-ten-successes-keep-start-first-end-and-nine-detail",
        readRows(L"native-lifecycle.log").size()==3 && wakeDetail.size()==9 &&
        wakeDetail.front()["requestedEpoch"]==4 && wakeDetail.back()["durationMs"]==18);
    g_domainLogEnabled[1] = false;
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"requestedEpoch",90},{"durationMs",70},{"intervalMs",2000}});
    check("wake-guard-unchanged-success-off-no-extra-row",
        readRows(L"native-lifecycle.log").size()==3 && readRows(L"native-detail.log").size()==9);
    for(int i=0;i<2;++i) appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",false},{"requestedEpoch",91+i},{"intervalMs",2000}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"intervalMs",2000}});
    check("wake-guard-failures-and-recovery-resident-with-detail-off",readRows(L"native-lifecycle.log").size()==6);
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"intervalMs",2000}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok","unknown"},{"intervalMs",2000}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"intervalMs",2000}});
    ++g_powerGeneration;
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"intervalMs",2000}});
    check("wake-guard-unknown-and-new-generation-cannot-disappear",readRows(L"native-lifecycle.log").size()==10);
    for(const auto* state:{"task-disabled","process-stopped","process-stop-failed","retry-exhausted","applied"})
        for(int i=0;i<2;++i) appendNativeLifecycleLog("oem-vendor-stack",{{"state",state},{"requestedEpoch",i}});
    for(int i=0;i<4;++i) appendNativeLifecycleLog("late-hide-complete",{{"queueToken",i},{"outcome","skipped"},{"decision","refused-window"}});
    check("guard-action-and-late-hide-transaction-receipts-remain-resident",readRows(L"native-lifecycle.log").size()==24);
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-started"},{"generation",77}});
    appendNativeLifecycleLog("oem-vendor-stack", {{"state","wake-guard-pass"},{"ok",true},{"intervalMs",2000}});
    check("new-guard-start-retains-first-pass-even-in-same-power-generation",readRows(L"native-lifecycle.log").size()==26);

    reset(root, L"frame-observation-lanes"); g_domainLogEnabled[1] = true;
    appendNativeLifecycleLog("input-host-frame-evidence", {{"snapshotReason","state-change"},{"sequence",1}});
    appendNativeLifecycleLog("input-host-frame-evidence", {{"snapshotReason","periodic"},{"sequence",2}});
    appendNativeLifecycleLog("input-host-frame-evidence", {{"snapshotReason","unknown"},{"sequence",3}});
    flushNativeLifecycleLogs();
    check("frame-periodic-detail-state-change-and-unknown-retained", readRows(L"native-lifecycle.log").size()==2 &&
        readRows(L"native-detail.log").size()==1 && readRows(L"native-detail.log")[0]["sequence"]==2);
    g_domainLogEnabled[1] = false;
    appendNativeLifecycleLog("input-host-frame-evidence", {{"snapshotReason","periodic"},{"sequence",4}});
    flushNativeLifecycleLogs();
    check("frame-periodic-off-adds-no-row", readRows(L"native-lifecycle.log").size()==2 && readRows(L"native-detail.log").size()==1);

    reset(root, L"interrupted-tail");
    const auto tailPath=fspath::path(g_testRoot)/L"native-lifecycle.log";
    const std::string broken="{\"oldPid\":7,\"unfinished\":";
    {std::ofstream seed(tailPath,std::ios::binary);seed<<broken;}
    appendNativeLifecycleLog("app-exit-ipc", {{"nextPid",8}});
    std::ifstream tailInput(tailPath,std::ios::binary);std::string retained,valid;
    std::getline(tailInput,retained);std::getline(tailInput,valid);
    check("restart-retains-tail-bytes-and-keeps-next-row-parseable",retained==broken && json::parse(valid)["nextPid"]==8);
    tailInput.close();g_nativeLifecycleLog.close();g_nativeLifecycleLog.clear();
    appendNativeLifecycleLog("app-exit-ipc", {{"nextPid",9}});
    std::ifstream again(tailPath);unsigned tailLines=0;for(std::string line;std::getline(again,line);)++tailLines;
    check("clean-reopen-does-not-add-extra-boundary",tailLines==3);
    again.close();
    reset(root, L"valid-row-without-newline");
    {std::ofstream seed(fspath::path(g_testRoot)/L"native-lifecycle.log",std::ios::binary);seed<<"{\"oldPid\":7}";}
    appendNativeLifecycleLog("app-exit-ipc", {{"nextPid",8}});
    check("valid-old-row-without-newline-and-new-row-both-retained",readRows(L"native-lifecycle.log").size()==2);

    reset(root, L"invalid-destination"); g_testRoot += L"\\missing\\nested";
    bool survived = true;
    try { appendNativeLifecycleLog("app-exit-ipc", {{"fixture", true}}); } catch(...) { survived = false; }
    check("writer-failure-never-changes-caller-control-flow", survived && !g_nativeLifecycleLog.is_open());
    reset(root,L"screen-off-fast-producer");kLogMaxBytes=5*1024*1024;++g_powerGeneration;
    json borrowed={{"available",true},{"cursorFallbackQueries",1}};
    logScreenTouchpadObservation("screenTouchpads.get","steamdeck",borrowed);
    for(int i=0;i<100;++i){borrowed["cursorFallbackQueries"]=i;logScreenTouchpadObservation("screenTouchpads.get","steamdeck",borrowed);}
    check("screen-off-fast-producer-emits-only-first",readRows(L"native-lifecycle.log").size()==1);
    logScreenTouchpadObservation("screenTouchpads.set","steamdeck",borrowed);logScreenTouchpadObservation("screenTouchpads.set","steamdeck",borrowed);
    check("screen-fast-path-preserves-both-settings-actions",readRows(L"native-lifecycle.log").size()==3);

    for(bool detailed:{false,true}){
        reset(root,detailed?L"raw-identity-on":L"raw-identity-off");kLogMaxBytes=5*1024*1024;
        g_domainLogEnabled[1]=detailed;++g_powerGeneration;
        HidHidePhysicalIdentity first,second;
        first.sourcePath="collection-one";first.deviceInstanceId="instance-one";first.baseContainerDeviceInstanceId="shared-container";
        second=first;second.sourcePath="collection-two";second.deviceInstanceId="instance-two";
        for(int i=0;i<100;++i){hidHideLogPhysicalIdentityObservation(first,1);hidHideLogPhysicalIdentityObservation(second,1);}
        check("alternating-collections-are-two-identity-observations-not-200-resident-edges",readRows(L"native-lifecycle.log").size()==2);
        check("alternating-collection-trajectory-follows-detail-gate",readRows(L"native-detail.log").size()==(detailed?198u:0u));
        auto count=readRows(L"native-lifecycle.log").size();
        first.baseContainerDeviceInstanceId="new-container";hidHideLogPhysicalIdentityObservation(first,1);
        hidHideLogPhysicalIdentityObservation(first,2);++g_powerGeneration;hidHideLogPhysicalIdentityObservation(first,2);
        first.resolved=false;hidHideLogPhysicalIdentityObservation(first,2);hidHideLogPhysicalIdentityObservation(first,2);
        first.resolved=true;hidHideLogPhysicalIdentityObservation(first,2);
        appendNativeLifecycleLog("physical-controller-identity",{{"resolved",true},{"sourcePath","legacy-without-label"}});
        check("identity-fields-topology-power-failures-and-legacy-records-stay-resident",readRows(L"native-lifecycle.log").size()==count+7);
        const auto beforeStable=readRows(L"native-lifecycle.log").size(),beforeDetail=readRows(L"native-detail.log").size();
        for(int i=0;i<64;++i)hidHideLogPhysicalIdentityObservation(first,2,false);
        check("unchanged-selected-source-does-not-add-packet-level-diagnostics",readRows(L"native-lifecycle.log").size()==beforeStable&&readRows(L"native-detail.log").size()==beforeDetail);
        hidHideLogPhysicalIdentityObservation(first,3,false);
        check("new-topology-is-logged-even-without-selected-source-change",readRows(L"native-lifecycle.log").size()==beforeStable+1);

    }

        // Field traces interleave consuming/dequeue/countdown/coalesced receipts.
        // A dequeue is an observation of the same queued request, not a new one.
        for(bool detailed : {false,true}) {
            reset(root,detailed?L"queued-wait-detail-on":L"queued-wait-detail-off");kLogMaxBytes=5*1024*1024;
            g_domainLogEnabled[1]=detailed;++g_powerGeneration;
            json consuming={{"action","consuming"},{"requestId",3},{"originalDeadline",10000},{"persona","steamdeck"}};
            json dequeue={{"beat","dequeue"},{"reason","initial-target-admission"},{"requestedAtDequeue","steamdeck"},{"waitedMs",4000},{"deferCount",1}};
            json settle={{"reason","initial-target-admission"},{"policy","queue-not-reject-coalesce-on-next-round"},{"deferMs",1000},{"settleMs",1000}};
            json merged={{"outcome","coalesced"},{"coalesced",true},{"queueToken",4},{"runId","field-run"},{"hostGen",2},{"sourceGen",390},{"recordGen",7}};
            for(int i=0;i<32;++i){
                dequeue["waitedMs"]=4000+i*8;dequeue["deferCount"]=i+1;settle["deferMs"]=1000-i*8;
                appendNativeLifecycleLog("input-host-player-start-work",consuming);
                appendNativeLifecycleLog("input-host-switch-audit",dequeue);
                appendNativeLifecycleLog("input-host-switch-settle",settle);
                appendNativeLifecycleLog("late-hide-enqueue",merged);
            }
            flushNativeLifecycleLogs(true);
            check("interleaved-queue-wait-only-four-first-observations-resident",readRows(L"native-lifecycle.log").size()==4);
            check("interleaved-queue-wait-full-repeats-follow-detail-gate",readRows(L"native-detail.log").size()==(detailed?124u:0u));
            auto count=readRows(L"native-lifecycle.log").size();
            consuming["requestId"]=4;appendNativeLifecycleLog("input-host-player-start-work",consuming);
            merged["queueToken"]=5;appendNativeLifecycleLog("late-hide-enqueue",merged);
            dequeue["requestedAtDequeue"]="elite";appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            settle["deferMs"]=999;appendNativeLifecycleLog("input-host-switch-settle",settle);
            check("request-token-target-and-new-countdown-window-remain-resident",readRows(L"native-lifecycle.log").size()==count+4);
            appendNativeLifecycleLog("input-host-switch-audit",{{"beat","request"},{"requestId",5}});
            count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-player-start-work",consuming);
            appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            check("real-request-resets-wait-evidence",readRows(L"native-lifecycle.log").size()==count+3);
            ++g_powerGeneration;count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-player-start-work",consuming);
            appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            appendNativeLifecycleLog("late-hide-enqueue",merged);
            check("power-generation-resets-all-wait-evidence",readRows(L"native-lifecycle.log").size()==count+4);
            count=readRows(L"native-lifecycle.log").size();
            auto rejected=consuming;rejected["action"]="rejected";
            auto accepted=merged;accepted["outcome"]="accepted";accepted["coalesced"]=false;
            auto refused=merged;refused["outcome"]="refused";refused["coalesced"]=false;
            for(int i=0;i<2;++i){
                appendNativeLifecycleLog("input-host-player-start-work",rejected);
                appendNativeLifecycleLog("late-hide-enqueue",accepted);
                appendNativeLifecycleLog("late-hide-enqueue",refused);
                appendNativeLifecycleLog("input-host-switch-audit",{{"beat","settled"},{"result","ok"}});
                appendNativeLifecycleLog("late-hide-complete",{{"queueToken",5},{"outcome","completed"}});
            }
            check("rejection-acceptance-refusal-and-terminal-actions-always-resident",readRows(L"native-lifecycle.log").size()==count+10);
            settle["deferMs"]="unavailable";count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            check("invalid-countdown-never-suppressed-as-telemetry",readRows(L"native-lifecycle.log").size()==count+2);
            dequeue["waitedMs"]=-1;count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            appendNativeLifecycleLog("input-host-switch-audit",dequeue);
            check("invalid-dequeue-counter-never-suppressed-as-telemetry",readRows(L"native-lifecycle.log").size()==count+2);
            settle["deferMs"]=500;appendNativeLifecycleLog("input-host-switch-settle",settle);
            settle["futureUnknownField"]=true;count=readRows(L"native-lifecycle.log").size();
            appendNativeLifecycleLog("input-host-switch-settle",settle);
            check("unknown-wait-fields-remain-semantic",readRows(L"native-lifecycle.log").size()==count+1);
        }

    for (bool detailed : {false,true}) {
        reset(root,detailed?L"new-observation-on":L"new-observation-off");
        kLogMaxBytes=5*1024*1024; ++g_powerGeneration;
        g_domainLogEnabled[1]=detailed;
        const json usage={{"supported",true},{"runtime",{{"keyboardActive",true}}}};
        for(int i=0;i<64;++i) appendNativeLifecycleLog("oem-key-usage-query",usage);
        auto base=readRows(L"native-lifecycle.log"), detail=readRows(L"native-detail.log");
        check(detailed?"oem-repeats-retained-only-in-detail":"oem-off-only-first-resident",base.size()==1&&detail.size()==(detailed?63:0));
        check("writer-borrows-business-state-without-mutation",!usage.contains("time")&&!usage.contains("pid"));
        auto changed=usage;changed["runtime"]["keyboardActive"]=false;
        appendNativeLifecycleLog("oem-key-usage-query",changed);
        ++g_powerGeneration;appendNativeLifecycleLog("oem-key-usage-query",changed);
        check("oem-source-change-and-power-edge-survive-off",readRows(L"native-lifecycle.log").size()==3);
        json screen={{"callSite","screenTouchpads.get"},{"state",{{"available",true},{"cursorFallbackQueries",1},
            {"cursorDeckReportPulseTick",1},{"cursorTransitionTrace",{{"entries",json::array({{{"samples",1},{"lastTick",1}}})},{"placements",{{"unchanged",1}}}}}}}};
        appendNativeLifecycleLog("screen-touchpads-state",screen);
        screen["state"]["cursorFallbackQueries"]=2;screen["state"]["cursorDeckReportPulseTick"]=2;
        screen["state"]["cursorTransitionTrace"]["entries"][0]["samples"]=2;
        appendNativeLifecycleLog("screen-touchpads-state",screen);
        auto rowsBefore=readRows(L"native-lifecycle.log").size();
        check("screen-counter-and-trajectory-samples-not-resident",rowsBefore==4);
        screen["state"]["available"]=false;appendNativeLifecycleLog("screen-touchpads-state",screen);
        screen["state"]["futureUnknownState"]=1;appendNativeLifecycleLog("screen-touchpads-state",screen);
        check("screen-availability-and-unknown-fields-preserved",readRows(L"native-lifecycle.log").size()==rowsBefore+2);
        screen["callSite"]="screenTouchpads.set";
        appendNativeLifecycleLog("screen-touchpads-state",screen);appendNativeLifecycleLog("screen-touchpads-state",screen);
        check("identical-settings-actions-both-remain-resident",readRows(L"native-lifecycle.log").size()==rowsBefore+4);
        screen["callSite"]="screenTouchpads.get";
        screen["state"]["cursorFallbackQueries"]="unavailable";
        const auto beforeSchema=readRows(L"native-lifecycle.log").size();
        appendNativeLifecycleLog("screen-touchpads-state",screen);
        screen["state"].erase("cursorFallbackQueries");appendNativeLifecycleLog("screen-touchpads-state",screen);
        check("wrong-type-and-missing-counter-are-not-hidden",readRows(L"native-lifecycle.log").size()==beforeSchema+2);
        const json work={{"requestId",7},{"originalDeadline",1000},{"action","consuming"},{"persona","elite"}};
        const auto beforeWork=readRows(L"native-lifecycle.log").size();
        for(int i=0;i<59;++i)appendNativeLifecycleLog("input-host-player-start-work",work);
        check("player-consuming-burst-only-first-resident",readRows(L"native-lifecycle.log").size()==beforeWork+1);
        auto failure=work;failure["action"]="rejected";
        appendNativeLifecycleLog("input-host-player-start-work",failure);
        check("player-failure-is-not-consumption-noise",readRows(L"native-lifecycle.log").size()==beforeWork+2);
        json throttle={{"beat","throttled"},{"reason","initial-target-admission"},{"policy","one-admission-attempt-per-2s"},{"waitMs",1}};
        const auto beforeThrottle=readRows(L"native-lifecycle.log").size();
        for(int i=0;i<10;++i){throttle["waitMs"]=i;appendNativeLifecycleLog("input-host-switch-audit",throttle);}
        check("wait-ms-alone-does-not-create-resident-edge",readRows(L"native-lifecycle.log").size()==beforeThrottle+1);
        appendNativeLifecycleLog("input-host-switch-audit",{{"beat","request"},{"requestId",8}});
        appendNativeLifecycleLog("input-host-switch-audit",throttle);
        check("new-request-restores-first-throttle-evidence",readRows(L"native-lifecycle.log").size()==beforeThrottle+3);

        json facts={{"generation",static_cast<unsigned long long>(g_powerGeneration.load())},{"pid",27},{"status","current"},
            {"foregroundRole","original-game"},{"nativePrimary",{{"status","matched"},{"log",{{"copiedBytes",1},{"originalBytes",1}}}}}};
        recordInspection(g_powerGeneration.load(),"observation",facts);
        facts["nativePrimary"]["log"]["copiedBytes"]=2;facts["nativePrimary"]["log"]["originalBytes"]=2;
        for(int i=0;i<22;++i)recordInspection(g_powerGeneration.load(),"observation",facts);
        check("sleep-log-growth-not-a-resident-state-change",readRows(L"sleep-facts.log").size()==1);
        recordInspection(g_powerGeneration.load(),"input-preflight",facts);
        recordInspection(g_powerGeneration.load(),"input-postflight",facts);
        check("sleep-identical-action-before-after-always-retained",readRows(L"sleep-facts.log").size()==3);
        facts["nativePrimary"]["log"]["truncated"]=true;
        recordInspection(g_powerGeneration.load(),"observation",facts);
        check("sleep-read-coverage-loss-is-real-evidence",readRows(L"sleep-facts.log").size()==4);
        facts["foregroundRole"]="steam-ui";recordInspection(g_powerGeneration.load(),"observation",facts);
        check("sleep-focus-change-is-real-evidence",readRows(L"sleep-facts.log").size()==5);
        const auto beforeDecky=readRows(L"native-lifecycle.log").size();
        for(int i=0;i<20;++i)deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",static_cast<unsigned long long>(g_powerGeneration.load())},{"ready",true},{"revision",i}},false);
        check("decky-snapshot-revisions-are-detailed-not-state-edges",readRows(L"decky-sidebar.log").size()==1&&readRows(L"native-lifecycle.log").size()==beforeDecky);
        deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",static_cast<unsigned long long>(g_powerGeneration.load())},{"ready",false},{"revision",20}},false);
        deckySidebarDiagnostic("runtime",{{"phase","failed"},{"reason",detailed?"fixture-error-on":"fixture-error-off"}},false);
        check("decky-ready-transition-and-runtime-error-survive",readRows(L"decky-sidebar.log").size()==3);
        ++g_powerGeneration;
        deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",static_cast<unsigned long long>(g_powerGeneration.load())-1},{"ready",false},{"revision",20}},false);
        check("decky-new-native-power-edge-survives-identical-payload",readRows(L"decky-sidebar.log").size()==4);
        bool survived=true;try{recordInspection(g_powerGeneration.load(),"observation",json(5));}catch(...){survived=false;}
        check("bad-diagnostic-projection-does-not-escape",survived);
    }
    reset(root,L"decky-sink-recovery");g_testRoot+=L"\\missing";++g_powerGeneration;
    deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",999},{"ready",true},{"revision",1}},false);
    fspath::create_directories(g_testRoot);
    deckySidebarDiagnostic("mirror-snapshot-queued",{{"generation",999},{"ready",true},{"revision",1}},false);
    check("decky-failed-write-does-not-consume-first-evidence",readRows(L"decky-sidebar.log").size()==1);

    {
    reset(root,L"model-adaptation");kLogMaxBytes=1024*1024;
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerApex";
    g_machineIdentity.manufacturer="ONE-NETBOOK";
    g_machineIdentity.systemProduct="ONEXPLAYER APEX";
    const auto beforeSignals=modelAdaptationSignals().status()["knownTypes"].get<unsigned>();
    std::array<BYTE,64> raw{};raw[0]=0xB2;raw[1]=raw[62]=0x3F;
    LARGE_INTEGER perfFreq{},perfBegin{},perfEnd{};QueryPerformanceFrequency(&perfFreq);QueryPerformanceCounter(&perfBegin);
    for(unsigned i=0;i<50000;++i){raw[6]=static_cast<BYTE>(0x21+i%3);raw[12]=1;raw[17]=static_cast<BYTE>(i);
        modelAdaptationOnexReport(raw.data(),raw.size(),true);raw[12]=0;
        modelAdaptationOnexReport(raw.data(),raw.size(),true);}
    QueryPerformanceCounter(&perfEnd);
    const auto producerUs=(perfEnd.QuadPart-perfBegin.QuadPart)*1000000/perfFreq.QuadPart;
    check("adaptation-50000-press-release-three-types-no-count-key",modelAdaptationSignals().status()["knownTypes"]==beforeSignals+3);
    check("adaptation-producer-does-not-write-files",!fspath::exists(fspath::path(g_testRoot)/L"model-adaptation.log"));
    modelAdaptationFlush();auto adaptation=readRows(L"model-adaptation.log");
    check("adaptation-detail-off-exactly-three-persistent-types",adaptation.size()==3);
    check("adaptation-sample-and-save-clocks-have-explicit-scope",adaptation[0].contains("timestampUtc")&&adaptation[0]["timestampUtc"].get<std::string>().back()=='Z'&&adaptation[0]["firstSample"].contains("capturedTickMs")&&!adaptation[0].contains("firstObservedAt"));
    bool rawReplay=true;for(const auto& row:adaptation){const auto& sample=row["firstSample"];
        const std::string hex=sample["firstRawReportHex"];
        std::array<BYTE,64> recovered{};for(unsigned i=0;i<64;++i)recovered[i]=static_cast<BYTE>(std::stoul(hex.substr(i*2,2),nullptr,16));
        const auto* frame=ymcc::capabilities::oem::onex::inputFrame(recovered.data(),recovered.size());
        rawReplay=rawReplay&&frame&&frame[6]==row["uniqueKey"]["identity"]["code"]&&sample["consumerActionProven"]==false;
        onexObserved=0;onexBackParse(recovered.data(),recovered.size(),71);
        rawReplay=rawReplay&&onexObserved==row["uniqueKey"]["identity"]["canonicalMask"].get<uint64_t>();
    }
    check("adaptation-first-raw-sample-replays-through-pinned-decoder",rawReplay);
    raw[0]=0xB8;raw[6]=0x99;modelAdaptationOnexReport(raw.data(),raw.size(),true);
    raw[0]=0xB2;raw[62]=0;modelAdaptationOnexReport(raw.data(),raw.size(),true);modelAdaptationFlush();
    check("adaptation-status-and-malformed-not-button-types",readRows(L"model-adaptation.log").size()==3);
    raw[62]=0x3F;modelAdaptationOnexReport(raw.data(),raw.size(),true);modelAdaptationFlush();
    adaptation=readRows(L"model-adaptation.log");
    check("adaptation-unknown-code-retained-before-mapping",adaptation.size()==4&&adaptation.back()["firstSample"]["mappingFound"]==false&&adaptation.back()["uniqueKey"]["identity"]["code"]==0x99);
    {std::array<BYTE,256> wide{};wide[0]=7;wide[1]=0xB2;wide[2]=wide[254]=0x3F;wide[7]=0x77;
    modelAdaptationOnexReport(wide.data(),wide.size(),true);modelAdaptationFlush();auto wideRows=readRows(L"model-adaptation.log");
    check("adaptation-complete-non64-report-retains-report-id",wideRows.size()==5&&wideRows.back()["firstSample"]["rawBytes"]==256&&wideRows.back()["firstSample"]["firstRawReportHex"].get<std::string>().size()==512&&wideRows.back()["uniqueKey"]["identity"]["reportId"]==7);}
    ++g_powerGeneration;raw[6]=0x22;modelAdaptationOnexReport(raw.data(),raw.size(),true);modelAdaptationFlush();
    check("adaptation-power-generation-not-new-type",readRows(L"model-adaptation.log").size()==5);
    const auto path=fspath::path(g_testRoot)/L"model-adaptation.log";
    ymcc::adaptlog::Journal reopened;
    const auto& first=adaptation.front();
    check("adaptation-reopen-dedup-ignores-first-sample-and-time",reopened.record(path,first["uniqueKey"]["model"],"oem-signal-type",first["uniqueKey"]["identity"],{{"pressedAtFirstWitness",false}},"different-time")&&readRows(L"model-adaptation.log").size()==5);
    auto changedModel=first["uniqueKey"]["model"];changedModel["deviceClass"]="different-model";
    auto changedRule=first["uniqueKey"]["identity"];changedRule["canonicalMask"]=1;
    check("adaptation-new-model-and-rule-not-deduped",reopened.record(path,changedModel,"oem-signal-type",first["uniqueKey"]["identity"],json::object(),"time")&&reopened.record(path,first["uniqueKey"]["model"],"oem-signal-type",changedRule,json::object(),"time")&&readRows(L"model-adaptation.log").size()==7);
    json catalog={{"schemaVersion",1},{"familyId","onex"},{"deviceClass","APEX"},{"captureClass","actual-consumer"},{"bindingSource","current-total-catalog"},{"keys",json::array({{{"keyId","l4"},{"physicalKeyIds",json::array({"l4"})}}})},{"runtime",{{"keyboardActive",true}}}};
    const auto originalCatalog=catalog;
    for(unsigned i=0;i<5000;++i){catalog["generation"]=i;modelAdaptationObserveRecord("oem-key-usage-query",catalog);}
    modelAdaptationFlush();adaptation=readRows(L"model-adaptation.log");
    check("adaptation-catalog-5000-queries-one-stable-fact",adaptation.size()==8&&adaptation.back()["uniqueKey"]["identity"]["captureClass"]=="actual-consumer");
    catalog.erase("generation");check("adaptation-observer-does-not-mutate-business-payload",catalog==originalCatalog);
    auto binding=json{{"ok",true},{"initProfile",3},{"path","fixture-hid"},{"inputReportBytes",65},{"outputReportBytes",64}};
    modelAdaptationObserveRecord("onex-back-bind",binding);
    modelAdaptationObserveRecord("msi-wmi-source-capability",{{"availability","known-absent"},{"eventClass","ROOT\\WMI:MSI_Event"},{"beginHresult",-1},{"authorityGranted",false}});
    modelAdaptationFlush();adaptation=readRows(L"model-adaptation.log");
    check("adaptation-actual-bind-and-wmi-field-projections",adaptation.size()==10&&adaptation[8]["uniqueKey"]["identity"]["inputReportBytes"]==65&&adaptation[9]["uniqueKey"]["identity"]["availability"]=="known-absent");
    for(const bool detail:{false,true}){
        g_domainLogEnabled[1]=detail;
        appendNativeLifecycleLog("onex-back-buttons",{{"id",0x22},{"down",true}});
        appendNativeLifecycleLog("onex-oem-input-edge",{{"mask",kOemBitR4}});
        appendNativeLifecycleLog("rog-oem-key",{{"code",1}});
        appendNativeLifecycleLog("oem-producer-edge",{{"status","queued"},{"reason","state-transition"}});
        appendNativeLifecycleLog("msi-wmi-key",{{"reason","queued"},{"accepted",true},{"posted",true}});
        appendNativeLifecycleLog("oem-producer-edge",{{"status","refused"},{"reason","fixture-no-binding"}});
        appendNativeLifecycleLog("msi-wmi-key",{{"reason","post-failed"},{"accepted",true},{"posted",false}});
    }
    auto resident=readRows(L"native-lifecycle.log"),detailed=readRows(L"native-detail.log");
    check("adaptation-key-trajectories-never-resident",resident.size()==4&&detailed.size()==5);
    check("adaptation-rejection-and-post-failure-resident-on-and-off",resident[0]["status"]=="refused"&&resident[1]["posted"]==false);
    ymcc::adaptlog::Signals bounded;
    for(unsigned i=0;i<ymcc::adaptlog::signalCapacity+1;++i){ymcc::adaptlog::Signal value;value.type.code=i;bounded.offer(value);}
    check("adaptation-queue-cap-no-eviction-explicit-omission",bounded.status()["knownTypes"]==ymcc::adaptlog::signalCapacity&&bounded.status()["capacityDrops"]==1&&bounded.status()["eviction"]==false);
    ymcc::adaptlog::Journal ledger;
    const auto boundedPath=fspath::path(g_testRoot)/L"bounded.log";
    bool committed=true;for(unsigned i=0;i<ymcc::adaptlog::journalCapacity;++i)committed&=ledger.record(boundedPath,json::object(),"test",{{"code",i}},json::object(),"time");
    const auto boundedBytes=fspath::file_size(boundedPath);
    check("adaptation-ledger-cap-preserves-original-types",committed&&!ledger.record(boundedPath,json::object(),"test",{{"code",999}},json::object(),"time")&&ledger.status()["budgetFull"]==true&&fspath::file_size(boundedPath)==boundedBytes);
    ymcc::adaptlog::Journal limited(10000,4,1024);const auto poison=fspath::path(g_testRoot)/L"poison.log";
    check("adaptation-permanent-oversize-does-not-block-later-types",limited.recordResult(poison,json::object(),"test",{{"code",1}},{{"tooLarge",std::string(2048,'x')}},"time")==ymcc::adaptlog::RecordResult::omitted&&limited.record(poison,json::object(),"test",{{"code",2}},json::object(),"time")&&limited.status()["budgetOmissions"]==1);
    const auto broken=fspath::path(g_testRoot)/L"missing"/L"repaired.log";
    ymcc::adaptlog::Journal failedWriter;
    bool failedWrite=!failedWriter.record(broken,json::object(),"test",{{"code",1}},json::object(),"time");
    failedWriter.record(broken,json::object(),"test",{{"code",1}},json::object(),"time");
    check("adaptation-failed-sink-backoff-bounds-retry",failedWrite&&failedWriter.status()["writeFailures"]==1);
    fspath::create_directories(broken.parent_path());ymcc::adaptlog::Journal repaired;
    check("adaptation-write-failure-does-not-consume-type",repaired.record(broken,json::object(),"test",{{"code",1}},json::object(),"time"));
    check("adaptation-no-pending-idle-path-is-explicit",!modelAdaptationSignals().pending()&&!modelAdaptationFacts().pending());
    {ymcc::adaptlog::Facts facts;for(unsigned i=0;i<2000;++i)facts.offer({json::object(),{{"a",i%2}},json::object(),"two-types"});
    check("adaptation-alternating-facts-do-not-grow",facts.status()["knownFacts"]==2);}
    std::ofstream cost(fspath::path(g_testRoot)/L"producer-cost.json");cost<<json({{"calls",100000},{"uniqueTypes",3},{"wallMicroseconds",producerUs},{"scope","isolated-real-callback-POD-path;not-product-CPU-or-device-latency"}}).dump(2);cost.close();
    }
    {
    reset(root,L"rog-adaptation");kLogMaxBytes=1024*1024;
    g_machineIdentity.manufacturer="ASUSTeK";g_machineIdentity.deviceClass="HandheldCompanion.Devices.ROGAlly";
    const BYTE unknown[]={0x5A,0x77};const auto previousTypes=modelAdaptationSignals().status()["knownTypes"];
    rogOemKeyParse(unknown,sizeof(unknown),70);modelAdaptationFlush();
    check("adaptation-ROG-stale-source-not-promoted",modelAdaptationSignals().status()["knownTypes"]==previousTypes&&!fspath::exists(fspath::path(g_testRoot)/L"model-adaptation.log"));
    for(unsigned i=0;i<5000;++i)rogOemKeyParse(unknown,sizeof(unknown),71);
    modelAdaptationFlush();auto rows=readRows(L"model-adaptation.log");
    check("adaptation-ROG-unknown-one-raw-type-no-business-publication",rows.size()==1&&rows[0]["firstSample"]["mappingFound"]==false&&rogPublications==0&&rows[0]["firstSample"]["firstRawReportHex"]=="5a77");
    check("adaptation-ROG-unknown-no-resident-input-flood",readRows(L"native-lifecycle.log").empty());
    const BYTE known[]={0x5A,kRogKeyEventAc};rogOemKeyParse(known,sizeof(known),71);modelAdaptationFlush();
    rows=readRows(L"model-adaptation.log");
    check("adaptation-ROG-mapped-parser-retains-publication",rows.size()==2&&rogPublications==1&&g_oemKeyMaskRog.load()==kOemBitAc&&rows[1]["firstSample"]["mappingFound"]==true);
    }
    {
    reset(root,L"model-catalog-baseline");
    g_machineIdentity.deviceClass="HandheldCompanion.Devices.OneXPlayerApex";g_machineIdentity.manufacturer="ONE-NETBOOK";
    modelAdaptationCatalog();modelAdaptationCatalog();modelAdaptationFlush();auto rows=readRows(L"model-adaptation.log");
    check("adaptation-baseline-without-diagnostics-query",rows.size()==1&&rows[0]["category"]=="model-catalog-baseline"&&rows[0]["uniqueKey"]["identity"]["deviceId"]=="hc.device.OneXPlayerApex"&&rows[0]["uniqueKey"]["identity"]["keys"].size()==std::size(kOneXApexKeys)&&rows[0]["firstSample"]["consumerActionProven"]==false);
    g_machineIdentity.deviceClass="unknown-machine";g_machineIdentity.resolved=false;modelAdaptationCatalog();modelAdaptationFlush();rows=readRows(L"model-adaptation.log");
    check("adaptation-unknown-model-is-evidence-not-silenced",rows.size()==2&&rows[1]["uniqueKey"]["identity"]["catalogBound"]==false&&rows[1]["uniqueKey"]["model"]["resolved"]==false);
    }
    flushNativeLifecycleLogs(true);
    unsigned failed = 0; for(const auto& row:checks) if(!row["pass"].get<bool>()) ++failed;
    std::cout << json({{"checks", checks}, {"failures", failed}, {"scope", "EXTRACTED_REAL_WRITER_AND_GATE_HANDLER_ISOLATED_FILES_NO_PRODUCT_OR_HARDWARE"}}).dump(2) << std::endl;
    return failed ? 3 : 0;
}
"""
    maintenance=extract(source,"static void inputHostRecoveryMaintenanceTick() {")
    block_call=maintenance.index('appendNativeLifecycleLog("input-host-recovery-episode-new"')
    block_start=maintenance.rfind("            try {",0,block_call)
    block_end=maintenance.index("            } catch",block_call)
    block_end=maintenance.index(chr(10),block_end)
    episode_log_block=maintenance[block_start:block_end]
    assert block_start>=0 and "g_virtualStartProtection" not in episode_log_block and "GetTickCount64" not in episode_log_block
    unsafe_episode_block=episode_log_block.replace("json(nullptr),g_inputHostEpoch","g_virtualStartProtection.view(now).requestId,g_inputHostEpoch",1)
    assert unsafe_episode_block!=episode_log_block
    episode_log_probe=r"""
static void episodeLogProbe(ymcc::VirtualStartProtection& protection,unsigned long long observationTick,bool unsafe,json& recorded){
    auto& g_virtualStartProtection=protection;
    const auto now=observationTick;const unsigned long long g_inputHostEpoch=91;
    const std::string g_inputHostRunId="probe-run",g_inputHostHostInstanceId="probe-host";
    const std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{4},g_inputHostPresentationWorkGeneration{8};
    auto appendNativeLifecycleLog=[&](const char*,const json& row){recorded=row;};
    if(unsafe){
"""+unsafe_episode_block+r"""
    }else{
"""+episode_log_block+r"""
    }
}
"""
    code = prelude + steam_log_projection + "\n\n" + "\n\n".join(functions) + "\nstatic json fanToggle(const json& a) " + fan_body + "\nstatic void familyImuLog(double gx, double gy, double gz, double ax, double ay, double az) {\n" + "struct Context { unsigned long long power=1, rawGeneration=1; }; struct Sample { Context context; unsigned long long capturedAt=0; } value; const auto* controllerMotion=&value; const auto now=GetTickCount64();\n" + family_log + "\n}\n" + inspection_log + "\n" + decky_log + "\n" + screen_log + "\n" + rog_fixture + "\n" + tests
    code = code.replace("static json fanToggle",episode_log_probe+"static json fanToggle",1)
    cpp = run / "log-frequency-harness.cpp"
    if review_run is not None:
        if (review_run/"log-frequency-harness.cpp").read_text(encoding="utf-8-sig") != code:
            raise ValueError("Referenced generated producer differs;refusing implicit rebuild/re-execution")
        if (review_run/"bound-log-frequency-policy.h").read_bytes()!=header_bytes:
            raise ValueError("Referenced producer policy differs")
        if sdk_response_bytes is not None and (review_run/"sdk-first-response.json").read_bytes()!=sdk_response_bytes:
            raise ValueError("Referenced SDK bridge response differs")
    cpp.write_text(code, encoding="utf-8")
    exe = run / "log-frequency-harness.exe"
    vcvars = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat")
    if not vcvars.is_file(): raise RuntimeError("Existing MSVC toolchain unavailable; no installation attempted")
    command = 'call "' + str(vcvars) + '" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "' + str(cpp) + '" /I"' + str(ROOT / "deps/json") + '" /I"' + str(ROOT / "native") + '" /Fo"' + str(run / "harness.obj") + '" /Fe"' + str(exe) + '"'
    # A saved batch avoids Python's list-to-command-line quoting of /c payloads.
    # It only initializes the existing compiler environment and compiles this fixture.
    # Registry discovery can omit an SDK whose headers/libraries are installed.
    # This only supplies existing paths to the fixture compiler, never host setup.
    sdk_root = pathlib.Path(r"C:\Program Files (x86)\Windows Kits\10")
    installed = sorted((sdk_root / "Include").glob("10.*"), reverse=True)
    sdk = next((p for p in installed if (p/"um/Windows.h").is_file() and (p/"ucrt/stdio.h").is_file()), None)
    if sdk is not None:
        command += "".join(' /I"' + str(sdk / name) + '"' for name in ("um", "shared", "ucrt", "winrt"))
        command += ' /link' + "".join(' /LIBPATH:"' + str(sdk_root/"Lib"/sdk.name/name/"x64") + '"' for name in ("um", "ucrt"))
    build_cmd = run / "build-harness.cmd"
    build_cmd.write_text("@echo off\n" + command + "\n", encoding="utf-8")
    if review_run is None:
        build = subprocess.run(["cmd.exe", "/d", "/c", str(build_cmd)], cwd=run, capture_output=True, text=True, errors="replace")
        (run / "compile.log").write_text(build.stdout + build.stderr, encoding="utf-8")
        if build.returncode: print(build.stdout + build.stderr); return build.returncode
        run_args = [str(exe), str(run / "fixtures")]
        if gpu_result_path: run_args.append(str(gpu_result_path))
        result = subprocess.run(run_args, cwd=run, capture_output=True, text=True, errors="replace")
        (run / "result.json").write_text(result.stdout, encoding="utf-8")
        if result.stderr: (run / "stderr.log").write_text(result.stderr, encoding="utf-8")
        report = json.loads(result.stdout)
        producer_exit_code=result.returncode
        report["executedThisRun"]=True
    else:
        raw_result=(review_run/"result.json").read_bytes()
        report=json.loads(raw_result)
        if not isinstance(report.get("checks"),list):raise ValueError("Referenced raw checks missing")
        failures=sum(not item.get("pass",False) for item in report["checks"])
        if report.get("failures")!=failures:raise ValueError("Referenced failure summary disagrees with actual checks")
        producer_exit_code=3 if failures else 0
        references=[]
        for name in ("result.json","log-frequency-harness.cpp","log-frequency-harness.exe","bound-main.cpp","bound-log-frequency-policy.h","compile.log"):
            file=review_run/name;data=file.read_bytes()
            references.append({"path":str(file),"bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()})
        (run/"REFERENCED-PRODUCER.json").write_text(json.dumps({"executedThisRun":False,"generatedHarnessIdentical":True,"references":references},indent=2)+"\n",encoding="utf-8")
        report["executedThisRun"]=False
        report["producerScope"]="CACHED_REFERENCE;original actual producer;only postchecks executed in this review"
        report["referencedProducer"]=references
    wake = extract(source, "static void inputHostEmitWakeTruthSnapshot() {")
    mirror = functions[3]
    current_bytes = source_path.read_bytes()
    current_source = current_bytes.decode("utf-8-sig").replace("\r\n", "\n")
    stable_functions = all(extract(current_source, f[:f.index("{")].strip() + " {") == f for f in functions)
    stable_fan = extract(current_source, 'ipc_on("fanLog.setEnabled", [](const json& a) -> json {') == fan
    current_family = extract(current_source, "        if (controllerMotion) {")
    stable_family = extract(current_family, "if (g_sensorDetailLogging.load(std::memory_order_acquire)) {") == family_log
    static_checks = {
        "identity-observation-helper-called-after-selected-update": identity_observer.index("g_hidHidePhysicalIdentity = resolved;") < identity_observer.index("hidHideLogPhysicalIdentityObservation(resolved, resolvedGeneration, changed);"),
        "identity-observer-stable-during-selftest": extract(current_source, "static void hidHideObserveRawInputDevice(HANDLE rawDevice) {") == identity_observer,
        "identity-detail-off-gates-before-json": extract(source,"static void hidHideLogPhysicalIdentityObservation(").index('if (!changed && (!selectedChanged || !domainLogEnabled("virtual"))) return;') < extract(source,"static void hidHideLogPhysicalIdentityObservation(").index('appendNativeLifecycleLog('),
        "wake-truth-gated-before-json-construction": wake.index('if (!domainLogEnabled("virtual")) return;') < wake.index('appendDomainLog('),
        "mirror-gated-before-string-allocation": mirror.index('if (!domainLogEnabled("gyro") && !domainLogEnabled("virtual")) return;') < mirror.index('std::string name'),
        "detail-export-includes-active-and-rotated": 'addRotated(appDir + L"\\\\native-detail.log");' in source,
        "export-flushes-pending-detail-batch": 'flushNativeLifecycleLogs(); // export includes the latest partial detail batch' in source,
        "bus-transition-helper-used-in-production": 'if (busRateLogState.observe(evidenceRateFlags)) {' in source,
        "periodic-bus-samples-have-separate-name": 'appendNativeLifecycleLog("input-capture-bus-rate-sample", rateEvidence());' in source,
        "family-imu-source-gates-before-building-json": family_log.index("g_sensorDetailLogging.load") < family_log.index("inputCaptureQueueSample"),
        "family-imu-source-has-no-synchronous-write": "inputCaptureAppend(" not in family,
        "detail-writer-rechecks-gate-under-lock": 'if (!baseEvent && !g_domainLogEnabled[1].load(std::memory_order_acquire)) return;' in functions[7],
        "bound-logging-functions-stable-during-selftest": stable_functions and stable_fan and stable_family,
        "policy-header-stable-during-selftest": header.read_bytes() == header_bytes,
        "screen-off-fast-path-before-wrapper": screen_log.index('observationEqual(previous,state') < screen_log.index('appendNativeLifecycleLog('),
        "suppressed-writer-skips-deep-copy": functions[7].index("NativeLane::suppressed") < functions[7].index("json detail = *evidence"),
        "sleep-post-action-read-labels-preserved": all(x in (ROOT/"native/sleep_recovery_auto.h").read_text(encoding="utf-8-sig") for x in ('inspect(gen,"reconcile-postflight")','inspect(gen,"input-postflight")')),
        "sleep-pre-action-labels-preserved": all(x in inspection_source for x in ('"input-preflight"','"reconcile-preflight"','"native-return-preflight"','"suspend-baseline"')),
    }
    onex_parser=extract(source,"static void onexBackParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")
    static_checks.update({
        "virtual-sample-bound-before-actual-request-and-observed-after": source.index("virtualEvidenceStamp.captureBinding") < source.index('accepted = frameTextReady') < source.index("virtualAdaptationFrameReceipt(virtualEvidenceStamp"),
        "virtual-no-extra-submit-in-observer": "inputHostRequest" not in (ROOT/"native/virtual_adaptation_log_native.inc").read_text(encoding="utf-8"),
        "adaptation-baseline-called-by-existing-machine-resolver": "modelAdaptationCatalog();" in extract(source,"static void ymccResolveMachineIdentityOnce() {"),
        "episode-log-block-no-protection-view-or-new-clock": "g_virtualStartProtection" not in episode_log_block and "GetTickCount64" not in episode_log_block and "inputHostStartProtectionNow" not in episode_log_block,
        "episode-log-builder-has-no-new-os-query": "GetProcessTimes" not in extract(source,"static json inputHostRecoveryEpisodeLogRecord(") and "writerIdentity()" in extract(source,"static json inputHostRecoveryEpisodeLogRecord("),
        "episode-log-builder-only-called-for-accepted-reset": source.count("inputHostRecoveryEpisodeLogRecord(")==2 and extract(source,"static void inputHostRecoveryMaintenanceTick() {").index("g_inputHostRecoveryIntentReset.exchange") < extract(source,"static void inputHostRecoveryMaintenanceTick() {").index("inputHostRecoveryEpisodeLogRecord("),
        "adaptation-OneX-parser-bound-to-source": extract(current_source,"static void onexBackParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")==onex_real_parser,
        "adaptation-ROG-parser-bound-to-source": extract(current_source,"static void rogOemKeyParse(const BYTE* data, size_t len, uint64_t actionGeneration) {")==rog_parser,
        "adaptation-tap-after-current-source-guard": onex_parser.index("!oemProducerCurrent") < onex_parser.index("modelAdaptationOnexReport") < onex_parser.index("inputFrame"),
        "adaptation-export-file-and-status": 'addFile(dataDir + L"\\\\model-adaptation.log");' in source and 'availability["modelAdaptation"]=modelAdaptationStatus();' in source,
        "adaptation-observer-no-disk": "Journal().record" not in extract((ROOT/"native/model_adaptation_log_native.inc").read_text(encoding="utf-8"),"static void modelAdaptationObserveRecord(const char* event,const json& detail) noexcept {"),
    })
    static_checks.update({
        "R117-export-index-locates-physical-slot-results": 'physicalSlotGuardEvidenceFile=native-lifecycle.log(changes),native-detail.log(repeats)' in source,
        "R114-export-includes-rotated-native-lifecycle": 'addRotated(appDir + L"\\\\native-lifecycle.log");' in source,
        "R114-export-index-locates-helper-facts": 'inputHelperAuthorityEvidenceFile=native-lifecycle.log event=input-host.native-helper-diagnostic detailLoggingRequired=0' in source,
        "R114-export-index-locates-owned-game-plan": 'ownedGameReconnectEvidenceFile=native-lifecycle.log event=hidhide.owned-game-reconnect-plan detailLoggingRequired=0' in source,
    })
    if gpu_result_path:
        static_checks["gpu-Windows-result-stable-during-selftest"] = gpu_result_path.read_bytes() == gpu_result_bytes
        report["gpuWindowsResultPath"] = str(gpu_result_path)
        report["gpuWindowsResultSha256"] = hashlib.sha256(gpu_result_bytes).hexdigest().upper()
        report["gpuProductionResponseCount"] = len(gpu_result["responses"])
    static_checks["snapshot-and-boundary-headers-stable"] = all((ROOT/"native"/n).read_bytes()==b for n,b in extra_headers.items())
    report["extraHeaderSha256"] = {n:hashlib.sha256(b).hexdigest().upper() for n,b in extra_headers.items()}
    report["sourceChecks"] = static_checks
    report["wholeSourceStableDuringSelftest"] = current_bytes == source_bytes
    report["currentWholeSourceSha256"] = hashlib.sha256(current_bytes).hexdigest().upper()
    report["sourceDriftPolicy"] = "Unrelated shared-file edits are disclosed; any bound logging-function or policy-header drift fails the test."
    report["sourceSha256"] = hashlib.sha256(source_bytes).hexdigest().upper()
    report["policyHeaderSha256"] = hashlib.sha256(header.read_bytes()).hexdigest().upper()
    report["runDirectory"] = str(run)
    report["fixtureRotationThresholdBytes"] = 256
    report["productionOrHardwareExecuted"] = False
    report["sdkFirstBridgeExecuted"]=sdk_response_path is not None
    if sdk_response_path:report["sdkFirstResponse"]={"path":str(sdk_response_path),"sha256":hashlib.sha256(sdk_response_bytes).hexdigest(),"stable":sdk_response_path.read_bytes()==sdk_response_bytes}
    report["frequencyPolicyHeaderChanged"] = header.read_bytes() != header_bytes
    report["boundedR114EvidenceBaseEvents"] = ["input-host.native-helper-diagnostic", "hidhide.owned-game-reconnect-plan"]
    report["failures"] += sum(not v for v in static_checks.values())
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 3 if report["failures"] else producer_exit_code

if __name__ == "__main__":
    raise SystemExit(main())
