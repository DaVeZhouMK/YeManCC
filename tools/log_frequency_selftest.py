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
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = output / ("run-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S") + "-" + str(os.getpid()))
    run.mkdir(exist_ok=False)
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
        "static void appendNativeLifecycleLog(const char* event, json detail) {",
        "static void domainLogGateRecord(const char* source, bool enabled) {",
        "static void inputHostCostFlagSync() {",
        "static void inputCaptureAppend(const json& rec) {",
        "static void inputCaptureQueueSample(const json& rec) {",
        "static void inputCaptureDrainQueuedSamples() {",
    )]
    # rotateOneLogFile has a forward declaration with a default argument. Select
    # its definition explicitly to avoid extracting the next unrelated function.
    functions[2] = extract(source, "static void rotateOneLogFile(const std::wstring& path, unsigned long long maxBytes) {")
    fan = extract(source, 'ipc_on("fanLog.setEnabled", [](const json& a) -> json {')
    fan_body = fan[fan.index("{"):]
    family = extract(source, "if (g_legionImuValid && g_machineIdentity.family == YmccFamily::LenovoLegionGo) {")
    family_log = extract(family, "if (g_sensorDetailLogging.load(std::memory_order_acquire)) {")
    prelude = r"""
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "json.hpp"
#include "log_frequency_policy.h"
using json = nlohmann::json;
namespace fspath = std::filesystem;
static std::wstring g_testRoot;
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
static void domainLogsSaveConfig() { ++configWrites; }
static void fanDiagnosticsApplyEnabled(bool on) { g_fanLogEnabled.store(on); }
static void appendDomainLog(const char*, const char*, const json&) { ++domainRows; }
static void flushDomainLogs(bool) {} // Real domain buffering has a separate R20 offline gate.
static void flushNativeLifecycleLogs(bool closeDetail = false);
static void rotateOneLogFile(const std::wstring&, unsigned long long maxBytes = kLogMaxBytes);
"""
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
    g_inputCapturePath = (fspath::path(g_testRoot) / L"input-capture.jsonl").wstring();
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const fspath::path root(argv[1]);
    using ymcc::logging::NativeLane;
    using ymcc::logging::nativeLane;
    check("lane-policy-base-stays-event-on-and-off",
        nativeLane(true, false, false) == NativeLane::event && nativeLane(true, false, true) == NativeLane::event);
    check("lane-policy-details-off-suppressed-on-detailed",
        nativeLane(false, false, false) == NativeLane::suppressed && nativeLane(false, false, true) == NativeLane::detail);
    check("lane-policy-successful-frame-never-base",
        nativeLane(true, true, false) == NativeLane::suppressed && nativeLane(true, true, true) == NativeLane::detail);
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
    check("sample-kind-policy-does-not-batch-lifecycle-events", ymcc::logging::inputSampleKind("sample") && ymcc::logging::inputSampleKind("controller-family-imu") && !ymcc::logging::inputSampleKind("stop") && !ymcc::logging::inputSampleKind("worker-exception"));
    reset(root, L"queue-bound"); g_sensorDetailLogging = true;
    for(int i=0;i<300;++i) familyImuLog(i,2,3,4,5,6);
    check("family-imu-queue-bound-and-drop-counter-retained", g_inputCaptureSampleQueue.size() == 256 && g_inputCaptureSampleQueueDrops == 44 && g_inputCaptureSampleQueue.front()["dps"]["x"] == 44 && !fspath::exists(g_inputCapturePath));
    reset(root, L"invalid-destination"); g_testRoot += L"\\missing\\nested";
    bool survived = true;
    try { appendNativeLifecycleLog("app-exit-ipc", {{"fixture", true}}); } catch(...) { survived = false; }
    check("writer-failure-never-changes-caller-control-flow", survived && !g_nativeLifecycleLog.is_open());
    flushNativeLifecycleLogs(true);
    unsigned failed = 0; for(const auto& row:checks) if(!row["pass"].get<bool>()) ++failed;
    std::cout << json({{"checks", checks}, {"failures", failed}, {"scope", "EXTRACTED_REAL_WRITER_AND_GATE_HANDLER_ISOLATED_FILES_NO_PRODUCT_OR_HARDWARE"}}).dump(2) << std::endl;
    return failed ? 3 : 0;
}
"""
    code = prelude + "\n\n".join(functions) + "\nstatic json fanToggle(const json& a) " + fan_body + "\nstatic void familyImuLog(double gx, double gy, double gz, double ax, double ay, double az) {\n" + family_log + "\n}\n" + tests
    cpp = run / "log-frequency-harness.cpp"
    cpp.write_text(code, encoding="utf-8")
    exe = run / "log-frequency-harness.exe"
    vcvars = pathlib.Path(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat")
    if not vcvars.is_file(): raise RuntimeError("Existing MSVC toolchain unavailable; no installation attempted")
    command = 'call "' + str(vcvars) + '" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 "' + str(cpp) + '" /I"' + str(ROOT / "deps/json") + '" /I"' + str(ROOT / "native") + '" /Fo"' + str(run / "harness.obj") + '" /Fe"' + str(exe) + '"'
    # A saved batch avoids Python's list-to-command-line quoting of /c payloads.
    # It only initializes the existing compiler environment and compiles this fixture.
    build_cmd = run / "build-harness.cmd"
    build_cmd.write_text("@echo off\n" + command + "\n", encoding="utf-8")
    build = subprocess.run(["cmd.exe", "/d", "/c", str(build_cmd)], cwd=run, capture_output=True, text=True, errors="replace")
    (run / "compile.log").write_text(build.stdout + build.stderr, encoding="utf-8")
    if build.returncode: print(build.stdout + build.stderr); return build.returncode
    result = subprocess.run([str(exe), str(run / "fixtures")], cwd=run, capture_output=True, text=True, errors="replace")
    (run / "result.json").write_text(result.stdout, encoding="utf-8")
    if result.stderr: (run / "stderr.log").write_text(result.stderr, encoding="utf-8")
    report = json.loads(result.stdout)
    wake = extract(source, "static void inputHostEmitWakeTruthSnapshot() {")
    mirror = functions[3]
    current_bytes = source_path.read_bytes()
    current_source = current_bytes.decode("utf-8-sig").replace("\r\n", "\n")
    stable_functions = all(extract(current_source, f[:f.index("{")].strip() + " {") == f for f in functions)
    stable_fan = extract(current_source, 'ipc_on("fanLog.setEnabled", [](const json& a) -> json {') == fan
    current_family = extract(current_source, "if (g_legionImuValid && g_machineIdentity.family == YmccFamily::LenovoLegionGo) {")
    stable_family = extract(current_family, "if (g_sensorDetailLogging.load(std::memory_order_acquire)) {") == family_log
    static_checks = {
        "wake-truth-gated-before-json-construction": wake.index('if (!domainLogEnabled("virtual")) return;') < wake.index('appendDomainLog('),
        "mirror-gated-before-string-allocation": mirror.index('if (!domainLogEnabled("gyro") && !domainLogEnabled("virtual")) return;') < mirror.index('std::string name'),
        "detail-export-includes-active-and-rotated": 'addRotated(appDir + L"\\\\native-detail.log");' in source,
        "export-flushes-pending-detail-batch": 'flushNativeLifecycleLogs(); // export includes the latest partial detail batch' in source,
        "bus-transition-helper-used-in-production": 'if (busRateLogState.observe(rateFlags)) {' in source,
        "periodic-bus-samples-have-separate-name": 'appendNativeLifecycleLog("input-capture-bus-rate-sample", rateEvidence());' in source,
        "family-imu-source-gates-before-building-json": family_log.index("g_sensorDetailLogging.load") < family_log.index("inputCaptureQueueSample"),
        "family-imu-source-has-no-synchronous-write": "inputCaptureAppend(" not in family,
        "detail-writer-rechecks-gate-under-lock": 'if (!baseEvent && !g_domainLogEnabled[1].load(std::memory_order_acquire)) return;' in functions[6],
        "bound-logging-functions-stable-during-selftest": stable_functions and stable_fan and stable_family,
        "policy-header-stable-during-selftest": header.read_bytes() == header_bytes,
    }
    report["sourceChecks"] = static_checks
    report["wholeSourceStableDuringSelftest"] = current_bytes == source_bytes
    report["currentWholeSourceSha256"] = hashlib.sha256(current_bytes).hexdigest().upper()
    report["sourceDriftPolicy"] = "Unrelated shared-file edits are disclosed; any bound logging-function or policy-header drift fails the test."
    report["sourceSha256"] = hashlib.sha256(source_bytes).hexdigest().upper()
    report["policyHeaderSha256"] = hashlib.sha256(header.read_bytes()).hexdigest().upper()
    report["runDirectory"] = str(run)
    report["fixtureRotationThresholdBytes"] = 256
    report["productionOrHardwareExecuted"] = False
    report["policyChanged"] = False
    report["failures"] += sum(not v for v in static_checks.values())
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 3 if report["failures"] else result.returncode

if __name__ == "__main__":
    raise SystemExit(main())
