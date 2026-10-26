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
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = output / ("run-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + str(os.getpid()))
    run.mkdir(exist_ok=False)
    gpu_result_path = args.gpu_windows_result.resolve() if args.gpu_windows_result else None
    if gpu_result_path:
        gpu_result_bytes = gpu_result_path.read_bytes()
        gpu_result = json.loads(gpu_result_bytes)
        if not (gpu_result.get("ok") and gpu_result.get("productionReader") and gpu_result.get("productionApply")):
            raise ValueError("GPU Windows result is not a passing production transport fixture")
    else:
        gpu_result_bytes = None
    extra_headers = {name:(ROOT/"native"/name).read_bytes() for name in ("log_snapshot_state.h", "sleep_fact_log_record.h", "log_jsonl_boundary.h", "sleep_steam_recovery.h", "decky_sidebar_diagnostics.h")}
    source_path = ROOT / "native/main.cpp"
    source_bytes = source_path.read_bytes()
    source = source_bytes.decode("utf-8-sig").replace("\r\n", "\n")
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
    )]
    # rotateOneLogFile has a forward declaration with a default argument. Select
    # its definition explicitly to avoid extracting the next unrelated function.
    functions[2] = extract(source, "static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {")
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
#include "sleep_fact_log_record.h"
#include "log_jsonl_boundary.h"
using json = nlohmann::json;
static void flushTdpObservationLogs(bool = false) {}
// Detailed monitor sink is tested by monitor_detail_bounds_selftest; this fixture owns only legacy writer lanes.
static void monitorDetailGateChanged(bool) {}
namespace fspath = std::filesystem;
static std::wstring g_testRoot, SG_FACT_LOG;
struct MachineIdentityFixture {
    std::string deviceClass="HandheldCompanion.Devices.ClawCG3EM",manufacturer="MSI",
        systemProduct="G3E fixture",processor="Intel fixture";
    bool resolved=true;
};
static MachineIdentityFixture g_machineIdentity;
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
    using ymcc::logging::NativeLane;
    using ymcc::logging::nativeLane;
    check("lane-policy-base-stays-event-on-and-off",
        nativeLane(true, false, false) == NativeLane::event && nativeLane(true, false, true) == NativeLane::event);
    check("lane-policy-details-off-suppressed-on-detailed",
        nativeLane(false, false, false) == NativeLane::suppressed && nativeLane(false, false, true) == NativeLane::detail);
    check("lane-policy-successful-frame-never-base",
        nativeLane(true, true, false) == NativeLane::suppressed && nativeLane(true, true, true) == NativeLane::detail);
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
    flushNativeLifecycleLogs(true);
    unsigned failed = 0; for(const auto& row:checks) if(!row["pass"].get<bool>()) ++failed;
    std::cout << json({{"checks", checks}, {"failures", failed}, {"scope", "EXTRACTED_REAL_WRITER_AND_GATE_HANDLER_ISOLATED_FILES_NO_PRODUCT_OR_HARDWARE"}}).dump(2) << std::endl;
    return failed ? 3 : 0;
}
"""
    code = prelude + steam_log_projection + "\n\n" + "\n\n".join(functions) + "\nstatic json fanToggle(const json& a) " + fan_body + "\nstatic void familyImuLog(double gx, double gy, double gz, double ax, double ay, double az) {\n" + "struct Context { unsigned long long power=1, rawGeneration=1; }; struct Sample { Context context; unsigned long long capturedAt=0; } value; const auto* controllerMotion=&value; const auto now=GetTickCount64();\n" + family_log + "\n}\n" + inspection_log + "\n" + decky_log + "\n" + screen_log + "\n" + tests
    cpp = run / "log-frequency-harness.cpp"
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
    build = subprocess.run(["cmd.exe", "/d", "/c", str(build_cmd)], cwd=run, capture_output=True, text=True, errors="replace")
    (run / "compile.log").write_text(build.stdout + build.stderr, encoding="utf-8")
    if build.returncode: print(build.stdout + build.stderr); return build.returncode
    run_args = [str(exe), str(run / "fixtures")]
    if gpu_result_path: run_args.append(str(gpu_result_path))
    result = subprocess.run(run_args, cwd=run, capture_output=True, text=True, errors="replace")
    (run / "result.json").write_text(result.stdout, encoding="utf-8")
    if result.stderr: (run / "stderr.log").write_text(result.stderr, encoding="utf-8")
    report = json.loads(result.stdout)
    wake = extract(source, "static void inputHostEmitWakeTruthSnapshot() {")
    mirror = functions[3]
    current_bytes = source_path.read_bytes()
    current_source = current_bytes.decode("utf-8-sig").replace("\r\n", "\n")
    stable_functions = all(extract(current_source, f[:f.index("{")].strip() + " {") == f for f in functions)
    stable_fan = extract(current_source, 'ipc_on("fanLog.setEnabled", [](const json& a) -> json {') == fan
    current_family = extract(current_source, "        if (controllerMotion) {")
    stable_family = extract(current_family, "if (g_sensorDetailLogging.load(std::memory_order_acquire)) {") == family_log
    static_checks = {
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
    report["frequencyPolicyHeaderChanged"] = header.read_bytes() != header_bytes
    report["boundedR114EvidenceBaseEvents"] = ["input-host.native-helper-diagnostic", "hidhide.owned-game-reconnect-plan"]
    report["failures"] += sum(not v for v in static_checks.values())
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 3 if report["failures"] else result.returncode

if __name__ == "__main__":
    raise SystemExit(main())
