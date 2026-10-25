// Compare actual native snapshot serves and HUD pulse functions. Hardware is inert.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';

const root = process.cwd().replaceAll('\\', '/');
const sourceRoot = path.resolve(process.env.YMCC_SNAPSHOT_ENVELOPE_SOURCE || root).replaceAll('\\', '/');
const out = path.resolve(process.env.YMCC_SNAPSHOT_ENVELOPE_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/snapshot-envelope-direct-selftest').replaceAll('\\', '/');
const baseline = path.resolve(process.env.YMCC_SNAPSHOT_ENVELOPE_BEFORE || root + '/tools/fixtures/monitor_snapshot_envelope_before_20261017.inc');
fs.mkdirSync(out, { recursive: true });
const main = fs.readFileSync(sourceRoot + '/native/main.cpp', 'utf8');
function extract(begin, end) {
    const a = main.indexOf(begin), b = main.indexOf(end, a);
    assert(a >= 0 && b > a, begin);
    return main.slice(a, b);
}
const before = fs.readFileSync(baseline, 'utf8');
const after = extract('static json monitorSnapshotServeFn()', 'static bool nativeMonitorOutputsExist();');
const state = extract('struct MonitorSnapshotState {', '\n};') + '\n};';
const pulse = extract('static void monitorHudPulseTick()', 'static ymcc::desktophud::Carrier g_desktopMonitorHud;');
const declarations = `
static std::mutex g_monitorSnapshotMx, g_nativeMonitorMx;
static MonitorSnapshotState g_monitorSnapshot;
static bool g_nativeMonitorTop = false, g_nativeMonitorFps = false;
static std::atomic<unsigned long long> g_monitorRelayEpoch{0}, g_monitorSnapshotServes{0}, g_monitorSnapshotOversizeDrops{0};
static unsigned long long session = 1;
static unsigned long long nativeMonitorSessionId() { return session; }
static std::string g_monitorHudPulseNonce;
static ULONGLONG g_monitorHudPulseDeadline = 0;
static UINT g_monitorHudPulseInterval = 1000;
static std::atomic<bool> g_monitorFastFrames{false};
static HWND g_hwnd = nullptr;
static constexpr UINT_PTR MONITOR_HUD_PULSE_TIMER_ID = 0x594d;
`;
const side = (name, serve) => `namespace ${name} { ${declarations}\n${serve}\n${pulse}\n}`;
const code = String.raw`
#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <new>
#include <random>
#include <string>
#include <string_view>
#include <vector>
#include "${root}/deps/json/json.hpp"
#include "${sourceRoot}/native/monitor_snapshot_contract.h"
using json = nlohmann::json;
static bool tracking = false;
static std::size_t allocations = 0, allocationBytes = 0;
void* operator new(std::size_t n) { if (tracking) { ++allocations; allocationBytes += n; } if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
static std::size_t assertions = 0, serveComparisons = 0, pulseComparisons = 0;
static void check(bool ok, const char* label) { ++assertions; if (!ok) { std::cerr << "FAIL " << label << '\n'; std::exit(1); } }
static ULONGLONG fakeTick = 5000;
static constexpr long long nowMs = 1700000000000LL;
static double sgNowEpoch() { return double(nowMs) / 1000.0; }
struct Effects { unsigned kill = 0, set = 0; UINT requestedInterval = 0; std::string eventName; json event; };
static Effects* effects = nullptr;
static unsigned sink = 0;
static BOOL fakeKillTimer(HWND, UINT_PTR) { if (effects) ++effects->kill; return TRUE; }
static UINT_PTR fakeSetTimer(HWND, UINT_PTR id, UINT interval, TIMERPROC) { if (effects) { ++effects->set; effects->requestedInterval = interval; } return id; }
static void ipc_emit(const char* name, const json& event) { if (effects) { effects->eventName = name; effects->event = event; } sink += unsigned(event.size()); }
// The transport itself is covered by monitor_pulse_transport_selftest; this
// fixture captures only event values and cadence for the unchanged pulse logic.
static void ipc_emit_monitor_hud_pulse(json event) { ipc_emit("monitor.hudPulse", event); }
#define GetTickCount64() fakeTick
#define KillTimer fakeKillTimer
#define SetTimer fakeSetTimer
${state}
${side('before', before)}
${side('after', after)}
#undef GetTickCount64
#undef KillTimer
#undef SetTimer

// JSON equality alone permits signed/unsigned number equality. Check types recursively too.
static bool sameTypedValue(const json& a, const json& b) {
    if (a.type() != b.type()) return false;
    if (a.is_object()) {
        if (a.size() != b.size()) return false;
        for (auto it = a.begin(); it != a.end(); ++it) {
            const auto other = b.find(it.key());
            if (other == b.end() || !sameTypedValue(it.value(), *other)) return false;
        }
        return true;
    }
    if (a.is_array()) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) if (!sameTypedValue(a[i], b[i])) return false;
        return true;
    }
    return a == b;
}
static MonitorSnapshotState seed() {
    MonitorSnapshotState s;
    s.valid = true; s.generation = 17; s.seq = 32;
    s.collectedAt = nowMs - 100; s.heartbeatAt = nowMs - 200; s.hwinfoOkAt = nowMs - 300; s.hwDown = false;
    s.topText = "{\"cpuUsage\":25,\"gpuName\":\"GPU\",\"note\":\"\\u4e2d\\u6587 / \\\" / \\\\ / \\n\"}";
    s.fpsText = "{\"fps\":117,\"frameTimeSource\":\"rtss-current-frame\",\"frameMs\":8.54}";
    return s;
}
static std::string payload(std::size_t size, bool curve = false) {
    const std::string prefix = curve ? "{\"frameTimeSource\":\"rtss-current-frame\",\"padding\":\"" : "{\"padding\":\"";
    if (size < prefix.size() + 2) return std::string(size, 'x');
    return prefix + std::string(size - prefix.size() - 2, 'x') + "\"}";
}
static bool sameCache(const MonitorSnapshotState& a, const MonitorSnapshotState& b) {
    return a.valid == b.valid && a.generation == b.generation && a.seq == b.seq && a.collectedAt == b.collectedAt &&
        a.heartbeatAt == b.heartbeatAt && a.hwinfoOkAt == b.hwinfoOkAt && a.hwDown == b.hwDown && a.topText == b.topText && a.fpsText == b.fpsText;
}
static void install(const MonitorSnapshotState& s, bool top, bool fps, unsigned long long session = 42, unsigned long long relay = 8) {
    before::g_monitorSnapshot = after::g_monitorSnapshot = s;
    before::g_nativeMonitorTop = after::g_nativeMonitorTop = top; before::g_nativeMonitorFps = after::g_nativeMonitorFps = fps;
    before::session = after::session = session; before::g_monitorRelayEpoch = after::g_monitorRelayEpoch = relay;
}
static void compareServe(const MonitorSnapshotState& s, bool top, bool fps, unsigned long long session = 42, unsigned long long relay = 8) {
    install(s, top, fps, session, relay);
    const auto servesA = before::g_monitorSnapshotServes.load(), servesB = after::g_monitorSnapshotServes.load();
    const auto dropsA = before::g_monitorSnapshotOversizeDrops.load(), dropsB = after::g_monitorSnapshotOversizeDrops.load();
    auto a = before::monitorSnapshotServeFn(), b = after::monitorSnapshotServeFn();
    check(sameTypedValue(a, b) && a.dump() == b.dump(), "complete response values, numeric types and serialized bytes match");
    check(before::g_monitorSnapshotServes == servesA + 1 && after::g_monitorSnapshotServes == servesB + 1, "exactly one serve count per call");
    check(before::g_monitorSnapshotOversizeDrops == dropsA && after::g_monitorSnapshotOversizeDrops == dropsB, "reader never increments producer drop count");
    check(sameCache(s, before::g_monitorSnapshot) && sameCache(s, after::g_monitorSnapshot), "source cache including timestamps remains unchanged");
    check(b["schema"] == 1 && b["available"] == s.valid && b["generation"] == s.generation && b["seq"] == s.seq &&
        b["session"] == session && b["relayEpoch"] == relay && b["topActive"] == top && b["fpsActive"] == fps, "response uses current mode, session and relay, retaining stored generation");
    if (!s.valid) {
        check(b["hwDown"] == true && !b.contains("top") && !b.contains("fps") && !b.contains("collectedAt") && !b.contains("snapshotOversize"), "unavailable response never leaks old data or collection timestamp");
    } else {
        check(b["collectedAt"] == s.collectedAt && b["heartbeatAt"] == s.heartbeatAt && b["hwinfoOkAt"] == s.hwinfoOkAt && b["hwDown"] == s.hwDown, "serve never renews freshness");
        const bool topFits = s.topText.size() <= 16384, fpsFits = s.fpsText.size() <= 16384;
        const bool combined = (topFits ? s.topText.size() : 0) + (fpsFits ? s.fpsText.size() : 0) <= 24576;
        check(b.contains("snapshotOversize") == (!topFits || !fpsFits || !combined), "16 KiB document and 24 KiB combined boundary is enforced");
        check(b.contains("top") == (topFits && combined && !s.topText.empty()) && b.contains("fps") == (fpsFits && !s.fpsText.empty()), "oversize skips bounded documents as whole; combined overflow retains FPS");
        if (b.contains("top")) check(b["top"] == s.topText, "top opaque bytes preserved");
        if (b.contains("fps")) check(b["fps"] == s.fpsText, "FPS opaque bytes preserved");
    }
    b["top"] = "edited by consumer"; b["fps"] = "edited by consumer";
    check(sameCache(s, after::g_monitorSnapshot), "consumer edits cannot mutate the publisher cache");
    ++serveComparisons;
}
static json capturePulse(bool current, bool fast, ULONGLONG deadline, const std::string& nonce, UINT interval) {
    Effects capture; effects = &capture;
    json result;
    if (current) {
        after::g_monitorHudPulseNonce = nonce; after::g_monitorHudPulseDeadline = deadline; after::g_monitorFastFrames = fast; after::g_monitorHudPulseInterval = interval;
        after::monitorHudPulseTick();
        effects = nullptr; result["nonce"] = after::g_monitorHudPulseNonce; result["fast"] = after::g_monitorFastFrames.load(); result["interval"] = after::g_monitorHudPulseInterval;
    } else {
        before::g_monitorHudPulseNonce = nonce; before::g_monitorHudPulseDeadline = deadline; before::g_monitorFastFrames = fast; before::g_monitorHudPulseInterval = interval;
        before::monitorHudPulseTick();
        effects = nullptr; result["nonce"] = before::g_monitorHudPulseNonce; result["fast"] = before::g_monitorFastFrames.load(); result["interval"] = before::g_monitorHudPulseInterval;
    }
    result["kill"] = capture.kill; result["set"] = capture.set; result["requestedInterval"] = capture.requestedInterval;
    result["eventName"] = std::move(capture.eventName); result["event"] = std::move(capture.event);
    return result;
}
static void comparePulse(const MonitorSnapshotState& s, bool top, bool fps, bool fast, ULONGLONG deadline, const std::string& nonce, UINT interval) {
    install(s, top, fps);
    const auto countA = before::g_monitorSnapshotServes.load(), countB = after::g_monitorSnapshotServes.load();
    auto a = capturePulse(false, fast, deadline, nonce, interval), b = capturePulse(true, fast, deadline, nonce, interval);
    check(sameTypedValue(a, b) && a.dump() == b.dump(), "complete emitted event and lease/timer effects match");
    check(sameCache(s, before::g_monitorSnapshot) && sameCache(s, after::g_monitorSnapshot), "pulse never moves or rewrites shared cache");
    const bool alive = !nonce.empty() && fakeTick < deadline;
    check(before::g_monitorSnapshotServes == countA + (alive ? 1 : 0) && after::g_monitorSnapshotServes == countB + (alive ? 1 : 0), "expired lease does not serve a snapshot");
    if (!alive) {
        check(b["kill"] == 1 && b["set"] == 0 && b["nonce"] == "" && b["fast"] == false && b["event"].is_null(), "lease expiry silently disables pulse and fast request");
    } else {
        const bool curve = s.valid && s.fpsText.size() <= 16384 && s.fpsText.find("\"frameTimeSource\":\"rtss-current-frame\"") != std::string::npos;
        const bool fresh = fast && curve && fps && s.collectedAt > 0 && s.collectedAt <= nowMs && nowMs - s.collectedAt < 6000;
        const UINT expected = fresh ? 200 : 1000;
        check(b["kill"] == 0 && b["interval"] == expected && b["set"] == (interval == expected ? 0 : 1), "200 ms/1000 ms cadence and timer rearming remain exact");
        check(b["nonce"] == nonce && b["fast"] == fast && b["eventName"] == "monitor.hudPulse" && b["event"]["nonce"] == nonce, "live event retains the current lease owner");
        if (s.valid) check(b["event"]["snapshot"]["collectedAt"] == s.collectedAt, "pulse cannot renew an old producer timestamp");
    }
    ++pulseComparisons;
}
template<class F> static json measure(F fn) {
    tracking = false; for (unsigned i = 0; i < 20; ++i) fn();
    allocations = allocationBytes = 0; tracking = true;
    for (unsigned i = 0; i < 2500; ++i) fn();
    tracking = false;
    return {{"calls", 2500}, {"allocations", allocations}, {"bytes", allocationBytes}};
}
int main(int argc, char** argv) {
    check(argc == 2 || argc == 3, "output path and optional replay supplied");
    std::vector<MonitorSnapshotState> packetCases;
    const std::array<std::size_t, 10> lengths{0, 1, 15, 16, 1024, 8192, 8193, 16383, 16384, 16385};
    for (bool valid : {false, true}) for (auto top : lengths) for (auto fps : lengths) {
        auto s = seed(); s.valid = valid; s.topText = payload(top); s.fpsText = payload(fps, true); packetCases.push_back(std::move(s));
    }
    for (auto& s : packetCases) for (bool top : {false, true}) for (bool fps : {false, true}) compareServe(s, top, fps);
    // Special combined-boundary and string-ownership inputs.
    for (const auto sizes : {std::pair{16384U, 8192U}, std::pair{16384U, 8193U}, std::pair{16385U, 8192U}, std::pair{8192U, 16385U}}) {
        auto s = seed(); s.topText = payload(sizes.first); s.fpsText = payload(sizes.second, true); compareServe(s, true, true);
    }
    { auto s = seed(); s.topText.push_back('\0'); s.topText += "\"\\embedded"; s.fpsText.push_back('\0'); compareServe(s, true, true); }
    std::mt19937_64 rng(0x594d4343ULL);
    for (unsigned i = 0; i < 3500; ++i) {
        auto s = seed(); s.valid = rng() % 2; s.generation = rng(); s.seq = rng(); s.hwDown = rng() % 2;
        s.collectedAt = static_cast<long long>(rng()); s.heartbeatAt = static_cast<long long>(rng()); s.hwinfoOkAt = static_cast<long long>(rng());
        s.topText = payload(lengths[rng() % lengths.size()]); s.fpsText = payload(lengths[rng() % lengths.size()], rng() % 2);
        compareServe(s, rng() % 2, rng() % 2, rng(), rng());
    }
    // Freshness boundary, future/invalid timestamps, marker presence/size and mode changes.
    std::vector<MonitorSnapshotState> pulseCases;
    for (long long age : {-1LL, 0LL, 1LL, 5999LL, 6000LL, 6001LL, 63000LL}) { auto s = seed(); s.collectedAt = nowMs - age; pulseCases.push_back(s); }
    { auto s = seed(); s.collectedAt = 0; pulseCases.push_back(s); s.collectedAt = -1; pulseCases.push_back(s); s.valid = false; pulseCases.push_back(s); }
    for (auto len : {0U, 15U, 16384U, 16385U}) { auto s = seed(); s.fpsText = payload(len, true); pulseCases.push_back(s); }
    { auto s = seed(); s.fpsText = "{\"fps\":120,\"frameTimeSource\":\"hwinfo-presented\"}"; pulseCases.push_back(s); s.topText = payload(16384); s.fpsText = payload(16384, true); pulseCases.push_back(s); }
    for (const auto& s : pulseCases) for (bool top : {false, true}) for (bool fps : {false, true}) for (bool fast : {false, true})
        for (auto deadline : {4999ULL, 5000ULL, 5001ULL, 12000ULL}) for (const auto& nonce : {std::string{}, std::string{"snapshot-envelope-lease"}}) for (UINT interval : {200U, 1000U})
            comparePulse(s, top, fps, fast, deadline, nonce, interval);
    json replay = json::array();
    if (argc == 3) {
        std::ifstream input(argv[2]); json traces; input >> traces;
        for (const auto& trace : traces) {
            unsigned count = 0;
            for (const auto& packet : trace.at("packets")) {
                MonitorSnapshotState s;
                s.valid = packet.value("available", false); s.generation = packet.value("generation", 0ULL); s.seq = packet.value("seq", 0ULL);
                s.collectedAt = packet.value("collectedAt", 0LL); s.heartbeatAt = packet.value("heartbeatAt", 0LL); s.hwinfoOkAt = packet.value("hwinfoOkAt", 0LL); s.hwDown = packet.value("hwDown", true);
                if (packet.contains("top") && packet["top"].is_string()) s.topText = packet["top"].get<std::string>();
                if (packet.contains("fps") && packet["fps"].is_string()) s.fpsText = packet["fps"].get<std::string>();
                const bool top = packet.value("topActive", false), fps = packet.value("fpsActive", false);
                const auto session = packet.value("session", 0ULL), relay = packet.value("relayEpoch", 0ULL);
                compareServe(s, top, fps, session, relay);
                // Real source epochs remain untouched; current fake wall clock may correctly downgrade old/future captures.
                comparePulse(s, top, fps, false, 12000, "snapshot-envelope-replay", 1000);
                comparePulse(s, top, fps, true, 12000, "snapshot-envelope-replay", 200);
                ++count;
            }
            replay.push_back({{"name", trace.at("name")}, {"packets", count}});
        }
    }
    json work = json::array();
    for (const auto* name : {"unavailable", "small", "near-cap", "combined-overflow", "oversize"}) {
        auto s = seed(); const std::string kind(name);
        if (kind == "unavailable") s.valid = false;
        if (kind == "near-cap") { s.topText = payload(16384); s.fpsText = payload(8192, true); }
        if (kind == "combined-overflow") { s.topText = payload(16384); s.fpsText = payload(8193, true); }
        if (kind == "oversize") { s.topText = payload(16385); s.fpsText = payload(16385, true); }
        install(s, true, true);
        const auto serveA = measure([] { auto response = before::monitorSnapshotServeFn(); sink += unsigned(response.size()); });
        const auto serveB = measure([] { auto response = after::monitorSnapshotServeFn(); sink += unsigned(response.size()); });
        before::g_monitorHudPulseNonce = after::g_monitorHudPulseNonce = "snapshot-envelope-lease";
        before::g_monitorHudPulseDeadline = after::g_monitorHudPulseDeadline = 12000;
        before::g_monitorHudPulseInterval = after::g_monitorHudPulseInterval = 200;
        before::g_monitorFastFrames = after::g_monitorFastFrames = true;
        effects = nullptr;
        const auto pulseA = measure([] { before::monitorHudPulseTick(); });
        const auto pulseB = measure([] { after::monitorHudPulseTick(); });
        check(serveB["allocations"] < serveA["allocations"] && serveB["bytes"] < serveA["bytes"], "actual serve has strictly lower requested allocation work");
        check(pulseB["allocations"] < pulseA["allocations"] && pulseB["bytes"] < pulseA["bytes"], "actual pulse with inert event sink has strictly lower requested allocation work");
        work.push_back({{"case", kind}, {"serve", {{"before", serveA}, {"after", serveB}}}, {"pulse", {{"before", pulseA}, {"after", pulseB}}}});
    }
    json result = {{"ok", true}, {"assertions", assertions}, {"serveComparisons", serveComparisons}, {"pulseComparisons", pulseComparisons}, {"replay", replay}, {"operations", work},
        {"scope", "Actual complete before/current snapshot serve and actual current native HUD pulse with inert lease/clock/timer/event hooks. Exact output bytes, ownership, caps, counters and cadence. Allocation work includes temporary/result destruction; no hardware APIs, IPC serialization/transport cost, process CPU percentage or timing benchmark."}};
    std::ofstream(argv[1]) << result.dump(2);
    std::cout << result.dump(2) << '\n';
}
`;
fs.writeFileSync(out + '/snapshot-envelope.cpp', code);
const batch = out + '/compile-run.bat';
fs.writeFileSync(batch, [
    '@echo off',
    'call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul',
    'if errorlevel 1 exit /b %ERRORLEVEL%',
    `cl /nologo /std:c++20 /utf-8 /EHsc /W4 /MT /O2 "${out}/snapshot-envelope.cpp" /Fo"${out}/snapshot-envelope.obj" /Fe"${out}/snapshot-envelope.exe"`,
    'if errorlevel 1 exit /b %ERRORLEVEL%',
    `"${out}/snapshot-envelope.exe" "${out}/RESULT.json"${process.env.YMCC_SNAPSHOT_ENVELOPE_REPLAY ? ' "' + path.resolve(process.env.YMCC_SNAPSHOT_ENVELOPE_REPLAY).replaceAll('\\', '/') + '"' : ''}`,
    'exit /b %ERRORLEVEL%', ''
].join('\r\n'));
const run = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/s', '/c', '""' + batch + '""'], {
    windowsHide: true, windowsVerbatimArguments: true, encoding: 'utf8', timeout: 60000, maxBuffer: 4e6
});
fs.writeFileSync(out + '/COMPILE-RUN.log', (run.stdout || '') + (run.stderr || ''));
assert.equal(run.status, 0, (run.stdout || '') + (run.stderr || ''));
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
fs.writeFileSync(out + '/SOURCE-PINS.json', JSON.stringify({
    root: sourceRoot, baseline, before: sha(before), after: sha(after), state: sha(state), pulse: sha(pulse),
    snapshotContract: sha(fs.readFileSync(sourceRoot + '/native/monitor_snapshot_contract.h')),
    test: sha(fs.readFileSync(new URL(import.meta.url)))
}, null, 2));
const result = JSON.parse(fs.readFileSync(out + '/RESULT.json', 'utf8'));
assert(result.ok);
console.log(JSON.stringify({ ok: result.ok, assertions: result.assertions, serveComparisons: result.serveComparisons, pulseComparisons: result.pulseComparisons, replay: result.replay, operations: result.operations }, null, 2));