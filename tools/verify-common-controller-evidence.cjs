#!/usr/bin/env node
'use strict';
// Extract the actual diagnostic helper and compile an offline MSVC fixture.
// Usage: node tools/verify-common-controller-evidence.cjs --output-dir <NEW directory>
// Optional: --main <main.cpp snapshot> --vcvars <vcvars64.bat>
// Never compiles main.cpp, invokes build_native.bat, starts a product/Host, or
// links input, HID, SDK, service or registry APIs. Only the named new directory
// receives generated sources, compiler outputs, receipts and observations.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');
const repo = path.resolve(__dirname, '..');
const sha256 = value => crypto.createHash('sha256').update(value).digest('hex');
function options(argv) {
  const result = {};
  for (let i = 0; i < argv.length; ++i) {
    const name = argv[i];
    if (name === '--help' || name === '-h') return { help: true };
    if (!['--output-dir', '--main', '--vcvars'].includes(name) || !argv[i + 1] || argv[i + 1].startsWith('--'))
      throw Error('Expected --output-dir <NEW directory> [--main <main.cpp>] [--vcvars <vcvars64.bat>]');
    if (result[name.slice(2)] !== undefined) throw Error('Duplicate option: ' + name);
    result[name.slice(2)] = argv[++i];
  }
  if (!result['output-dir']) throw Error('--output-dir is required; the output directory must not already exist.');
  return result;
}
function braceEnd(text, start) {
  let depth = 0, mode = 'code';
  for (let i = start; i < text.length; ++i) {
    const c = text[i], next = text[i + 1];
    if (mode === 'line') { if (c === '\n') mode = 'code'; continue; }
    if (mode === 'block') { if (c === '*' && next === '/') { mode = 'code'; ++i; } continue; }
    if (mode === 'string' || mode === 'char') {
      if (c === '\\') { ++i; continue; }
      if (c === (mode === 'string' ? '"' : "'")) mode = 'code';
      continue;
    }
    if (c === '/' && next === '/') { mode = 'line'; ++i; continue; }
    if (c === '/' && next === '*') { mode = 'block'; ++i; continue; }
    if (c === 'R' && next === '"') {
      const open = text.indexOf('(', i + 2), delimiter = text.slice(i + 2, open);
      if (open >= 0 && delimiter.length <= 16 && !/[\s()\\]/.test(delimiter)) {
        const close = text.indexOf(')' + delimiter + '"', open + 1);
        if (close < 0) throw Error('Unclosed C++ raw string.');
        i = close + delimiter.length + 1; continue;
      }
    }
    if (c === '"' || c === "'") { mode = c === '"' ? 'string' : 'char'; continue; }
    if (c === '{') ++depth;
    else if (c === '}' && --depth === 0) return i + 1;
  }
  throw Error('Unclosed production definition.');
}
function extract(text, name, kind = 'function') {
  const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const re = kind === 'struct'
    ? new RegExp('^struct\\s+' + escaped + '\\s*\\{', 'mg')
    : new RegExp('^static\\b[^\\n;{]*\\b' + escaped + '\\s*\\(', 'mg');
  const found = [];
  for (let match; (match = re.exec(text));) {
    const start = match.index, brace = text.indexOf('{', start), semi = text.indexOf(';', start);
    if (brace < 0 || (semi >= 0 && semi < brace)) continue;
    let end = braceEnd(text, brace);
    if (kind === 'struct') {
      const suffix = /^\s*;/.exec(text.slice(end));
      if (!suffix) throw Error('Missing struct terminator: ' + name);
      end += suffix[0].length;
    }
    const body = text.slice(start, end);
    found.push({ name, kind, line: text.slice(0, start).split('\n').length, body, sha256: sha256(body) });
    re.lastIndex = end;
  }
  if (found.length !== 1) throw Error(`Expected exactly one ${name} definition; found ${found.length}.`);
  return found[0];
}
function writeJson(out, name, value) { fs.writeFileSync(path.join(out, name), JSON.stringify(value, null, 2) + '\n'); }
function commandPath(value) {
  if (/["%!\r\n\0]/.test(value)) throw Error('Unsafe batch path: ' + value);
  return value;
}
const prelude = String.raw`
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
using ULONGLONG = unsigned long long;
using DWORD = std::uint32_t;
using USHORT = std::uint16_t;
using USAGE = std::uint16_t;
// Inert portable layout only; no XInput header, library, handle or function.
struct XINPUT_GAMEPAD {
    std::uint16_t wButtons = 0;
    std::uint8_t bLeftTrigger = 0, bRightTrigger = 0;
    std::int16_t sThumbLX = 0, sThumbLY = 0, sThumbRX = 0, sThumbRY = 0;
};
`;
const fakes = String.raw`
static thread_local const InputCaptureGamepadSnapshot* t_commonFrameSource = nullptr;
static std::atomic<unsigned long long> g_commonFrameEvidenceRows{0}, g_commonFrameEvidenceFailures{0};
static std::atomic<unsigned> g_xinputTopologyEpoch{7};
// Synthetic cache. std::mutex and the helper's try_lock remain real; the test
// controls availability by holding this mutex on a DIFFERENT fixture thread.
static std::mutex g_hidHidePhysicalIdentityMx;
static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
enum class FixtureFamily { Rog, Msi, Gpd };
static struct { FixtureFamily family = FixtureFamily::Rog; std::string product = "fixture-ROG-board"; } g_machineIdentity;
static const char* ymccFamilyName(FixtureFamily value) {
    return value == FixtureFamily::Rog ? "fixture-rog" : value == FixtureFamily::Msi ? "fixture-msi" : "fixture-gpd";
}
static bool gate = true, sinkThrows = false, sinkDiscards = false;
static ULONGLONG fakeTick = 10000;
static unsigned clockCalls = 0, gateCalls = 0, sinkCalls = 0;
static std::vector<json> logs;
static bool domainLogEnabled(const char* domain) {
    ++gateCalls;
    if (std::string(domain) != "virtual") throw std::runtime_error("unexpected log domain");
    return gate;
}
static ULONGLONG GetTickCount64() { ++clockCalls; return fakeTick; }
static void appendNativeLifecycleLog(const char* event, json row) {
    ++sinkCalls;
    if (sinkThrows) throw std::runtime_error("fixture log sink failure");
    if (sinkDiscards) return; // Observational probe: void sink gives no write ACK.
    row["event"] = event;
    logs.push_back(std::move(row));
}
`;
const tests = String.raw`
static unsigned assertions = 0;
static json cases = json::array(), findings = json::array();
static InputCaptureGamepadSnapshot source;
static json frame;
static_assert(noexcept(inputCaptureCommonFrameEvidence(std::declval<const json&>(), false)), "production helper must stay noexcept");
static void check(bool condition, const std::string& message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
static void flags(const json& row) {
    check(row.at("physicalIdentitySampleBindingProven") == false, "cached identity falsely bound to this sample");
    check(row.at("steamConsumptionProven") == false, "Steam consumption falsely proven");
    check(row.at("source").at("hardwarePacketAgeKnown") == false, "hardware-internal packet age falsely known");
    check(row.at("source").at("steamConsumptionProven") == false, "source claims Steam consumption");
}
static void seed() {
    gate = true; sinkThrows = false; sinkDiscards = false;
    fakeTick = 10000; clockCalls = 0; gateCalls = 0; sinkCalls = 0;
    logs.clear(); g_commonFrameEvidenceRows = 0; g_commonFrameEvidenceFailures = 0;
    g_xinputTopologyEpoch = 7;
    g_machineIdentity.family = FixtureFamily::Rog; g_machineIdentity.product = "fixture-ROG-board";
    source = InputCaptureGamepadSnapshot{};
    source.live = true; source.inputSource = "xinput"; source.primarySlot = 1; source.connectedMask = 2;
    source.readAttemptSequence = 101; source.readPowerGeneration = 3; source.readTopologyGeneration = 7;
    source.readStartedTick = 9992; source.readCompletedTick = 9998; source.readContextStable = true;
    g_hidHidePhysicalIdentity = HidHidePhysicalIdentity{};
    g_hidHidePhysicalIdentity.resolved = true;
    g_hidHidePhysicalIdentity.sourcePath = "fixture/source-A";
    g_hidHidePhysicalIdentity.deviceInstanceId = "FIXTURE/PHYSICAL-A";
    g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId = "FIXTURE/ROOT-A";
    g_hidHidePhysicalIdentity.physicalIdentity = "fixture-physical-A";
    g_hidHidePhysicalIdentity.vendorId = 0x0b05; g_hidHidePhysicalIdentity.productId = 0x1abe;
    g_hidHidePhysicalIdentity.usagePage = 1; g_hidHidePhysicalIdentity.usage = 5;
    frame = {{"hostSubmission", "frame-accepted"}, {"frameProof", {
        {"sampleSequence", 101ULL}, {"motionPairProofId", "fixture-safe-zero"},
        {"hostReceipt", {{"runId", "run-A"}, {"epoch", 41ULL}, {"hostInstanceId", "host-A"},
            {"sampleSequence", 101ULL}, {"commandSequence", 501ULL}, {"persona", "xbox360"},
            {"tupleCapturedBeforeRequest", true}, {"hostAckAccepted", true},
            {"receiptCompletedTick", 9999ULL}, {"steamConsumptionProven", false}}}}}};
}
static void emit(bool neutral = false) { inputCaptureCommonFrameEvidence(frame, neutral); }
static void one(const std::string& name, const std::function<void()>& test, bool cacheBusy = false) {
    seed();
    const unsigned before = assertions;
    std::unique_lock<std::mutex> holder(g_hidHidePhysicalIdentityMx, std::defer_lock);
    if (cacheBusy) holder.lock();
    std::exception_ptr error;
    // Each scenario gets new thread_local de-duplication state WITHOUT replacing
    // or resetting any statement of the extracted production helper.
    std::thread worker([&] {
        t_commonFrameSource = &source;
        try { test(); } catch (...) { error = std::current_exception(); }
        t_commonFrameSource = nullptr;
    });
    worker.join();
    if (holder.owns_lock()) holder.unlock();
    try { for (const auto& row : logs) flags(row); } catch (...) { if (!error) error = std::current_exception(); }
    json result = {{"name", name}, {"passed", error == nullptr}, {"assertions", assertions - before},
        {"emittedRows", logs.size()}, {"helperRowCounter", g_commonFrameEvidenceRows.load()},
        {"diagnosticFailures", g_commonFrameEvidenceFailures.load()}, {"clockCalls", clockCalls},
        {"sinkCalls", sinkCalls}, {"observations", logs}};
    if (error) {
        try { std::rethrow_exception(error); }
        catch (const std::exception& e) { result["failure"] = e.what(); }
        catch (...) { result["failure"] = "non-standard exception"; }
    }
    cases.push_back(std::move(result));
}
static void transition(const std::string& name, const std::function<void()>& change, bool secondNeutral = false) {
    one(name, [&] {
        emit(); check(logs.size() == 1, "first row missing");
        fakeTick += 1; change(); emit(secondNeutral);
        check(logs.size() == 2, "state transition suppressed below 1 second");
        check(logs.back().at("observedTick") == fakeTick, "transition time lost");
    });
}
int main() {
    one("gate-off-zero-output-and-no-clock-or-json-parse", [] {
        gate = false;
        inputCaptureCommonFrameEvidence(json{{"frameProof", 7}}, false);
        inputCaptureCommonFrameEvidence(json(nullptr), false);
        check(logs.empty() && sinkCalls == 0, "gate off writes logs");
        check(clockCalls == 0, "gate off reads the clock");
        check(g_commonFrameEvidenceRows == 0 && g_commonFrameEvidenceFailures == 0, "gate off changes counters");
        gate = true; emit(); check(logs.size() == 1, "gate-off call consumed first-frame state");
    });
    one("first-frame-real-json-and-read-call-age", [] {
        emit(); check(logs.size() == 1 && g_commonFrameEvidenceRows == 1, "first row count wrong");
        const auto& row = logs.front();
        check(row.at("event") == "input-host-frame-evidence", "wrong event name");
        check(row.at("source").at("snapshotKnown") == true, "known snapshot lost");
        check(row.at("source").at("readCompletionAgeMs") == 2, "read-call completion age wrong");
        check(row.at("source").at("readStartedTick") == 9992 && row.at("source").at("readCompletedTick") == 9998, "read window lost");
        check(row.at("source").at("freshnessScope") == "read-call-window;not-backend-cached-sample-age", "freshness boundary lost");
        check(row.at("source").at("identityBindingScope").get<std::string>().find("existing-visibility-events") != std::string::npos, "visibility join boundary missing");
        check(row.at("physicalIdentityCache").at("deviceInstanceId") == "FIXTURE/PHYSICAL-A", "cache identity lost");
        check(row == json::parse(row.dump()), "real JSON round trip differs");
    });
    one("same-state-under-1s-suppressed-even-with-new-sequences", [] {
        emit(); fakeTick += 1;
        source.readAttemptSequence += 1; frame["frameProof"]["sampleSequence"] = 102ULL;
        frame["frameProof"]["hostReceipt"]["sampleSequence"] = 102ULL;
        frame["frameProof"]["hostReceipt"]["commandSequence"] = 502ULL;
        emit(); fakeTick = 10999; emit();
        check(logs.size() == 1 && sinkCalls == 1 && g_commonFrameEvidenceRows == 1, "unchanged state emitted below 1s");
    });
    one("one-second-heartbeat-preserves-current-proof", [] {
        emit(); fakeTick = 11000; source.readAttemptSequence = 222;
        frame["frameProof"]["hostReceipt"]["sampleSequence"] = 222ULL;
        frame["frameProof"]["hostReceipt"]["commandSequence"] = 622ULL;
        emit(); check(logs.size() == 2, "exact 1s heartbeat missing");
        check(logs.back().at("source").at("readAttemptSequence") == 222, "heartbeat has old source sequence");
        check(logs.back().at("frameProof").at("hostReceipt").at("commandSequence") == 622, "heartbeat has old receipt");
        fakeTick = 11999; emit(); check(logs.size() == 2, "heartbeat window did not restart");
    });
    transition("epoch-transition", [] { frame["frameProof"]["hostReceipt"]["epoch"] = 42ULL; });
    transition("topology-transition", [] { source.readTopologyGeneration = 8; g_xinputTopologyEpoch = 8; });
    transition("source-transition", [] { source.inputSource = "rog-hid"; });
    transition("neutral-transition", [] {}, true);
    transition("outcome-transition", [] { frame["hostSubmission"] = "not-admitted-or-failed"; });
    transition("runId-transition", [] { frame["frameProof"]["hostReceipt"]["runId"] = "run-B"; });
    transition("hostInstanceId-transition", [] { frame["frameProof"]["hostReceipt"]["hostInstanceId"] = "host-B"; });
    transition("live-transition", [] { source.live = false; });
    transition("readContextStable-transition", [] { source.readContextStable = false; });
    transition("power-generation-transition", [] { source.readPowerGeneration = 4; });
    transition("primary-slot-transition", [] { source.primarySlot = 2; });
    transition("connected-mask-transition", [] { source.connectedMask = 6; });
    one("unknown-snapshot-does-not-claim-freshness", [] {
        t_commonFrameSource = nullptr; emit(); check(logs.size() == 1, "unknown snapshot row missing");
        const auto& value = logs.front().at("source");
        check(value.at("snapshotKnown") == false, "unknown snapshot marked known");
        for (const auto* key : {"readCompletionAgeMs", "readCompletedTick", "readStartedTick", "readAttemptSequence", "readContextStable"})
            check(!value.contains(key) || value.at(key).is_null(), std::string("unknown source fabricates ") + key);
        check(logs.front().at("frameProof") == frame.at("frameProof"), "unknown source corrupts independent ACK proof");
    });
    one("missing-completion-tick-is-unknown", [] {
        source.readCompletedTick = 0; emit();
        check(logs.front().at("source").at("readCompletionAgeMs").is_null(), "missing tick reported as fresh zero age");
    });
    one("future-completion-tick-is-unknown", [] {
        source.readCompletedTick = fakeTick + 1; emit();
        check(logs.front().at("source").at("readCompletionAgeMs").is_null(), "future tick gives an invented age");
    });
    one("old-read-and-drift-remain-qualified-observations", [] {
        source.readCompletedTick = 1000; source.readContextStable = false; emit();
        check(logs.front().at("source").at("readCompletionAgeMs") == 9000, "old read age hidden");
        check(logs.front().at("source").at("readContextStable") == false, "read drift erased");
    });
    one("identity-cache-try-lock-busy-is-null-not-bound", [] {
        emit(); check(logs.size() == 1, "busy identity lock blocks whole row");
        check(logs.front().at("physicalIdentityCache").is_null(), "busy lock fabricated identity");
        check(g_commonFrameEvidenceFailures == 0, "ordinary try_lock failure treated as diagnostic exception");
    }, true);
    one("identity-cache-unresolved-is-null-not-bound", [] {
        g_hidHidePhysicalIdentity.resolved = false; emit();
        check(logs.front().at("physicalIdentityCache").is_null(), "unresolved identity fabricated");
    });
    one("host-ack-proof-preserved-exactly", [] {
        // Use values beyond JS's safe-integer limit to exercise real C++ JSON,
        // and full-object equality to avoid a weakened field-by-field mock.
        frame["frameProof"]["hostReceipt"]["sampleSequence"] = 9007199254741001ULL;
        frame["frameProof"]["hostReceipt"]["commandSequence"] = 9007199254741013ULL;
        const json expected = frame.at("frameProof"); emit();
        const auto& proof = logs.front().at("frameProof");
        check(proof == expected, "Host proof changed or fields dropped");
        for (const auto* key : {"runId", "epoch", "hostInstanceId", "sampleSequence", "commandSequence", "hostAckAccepted"})
            check(proof.at("hostReceipt").at(key) == expected.at("hostReceipt").at(key), std::string("ACK proof lost ") + key);
    });
    one("sink-throw-contained-only-failure-counter-increases", [] {
        sinkThrows = true; bool callerContinued = false; emit(); callerContinued = true;
        check(callerContinued, "sink exception escaped");
        check(logs.empty() && g_commonFrameEvidenceRows == 0, "failed sink counted a row");
        check(g_commonFrameEvidenceFailures == 1 && sinkCalls == 1, "diagnostic failure count wrong");
        sinkThrows = false; fakeTick += 1; emit();
        if (logs.empty()) findings.push_back({{"id", "dedup-state-committed-before-sink"},
            {"severity", "observation"}, {"probe", "same state at +1ms after throwing sink"},
            {"effect", "Immediate retry suppressed; the first failed transition can remain absent until the 1s heartbeat or another state change."},
            {"minimumSuggestion", "Commit previous/lastTick after the sink returns; no device or lifecycle change."}});
        fakeTick = 11000; emit(); check(logs.size() >= 1, "logging cannot recover at heartbeat after sink failure");
        check(g_commonFrameEvidenceFailures == 1, "recovery increments failure counter");
    });
    one("malformed-real-json-contained-as-diagnostic-failure", [] {
        inputCaptureCommonFrameEvidence(json{{"frameProof", 7}}, false);
        check(g_commonFrameEvidenceFailures == 1 && logs.empty(), "malformed proof escaped or was treated as valid");
    });
    one("row-counter-is-not-durable-log-ack-probe", [] {
        sinkDiscards = true; emit(); check(logs.empty(), "silent sink probe unexpectedly retains row");
        if (g_commonFrameEvidenceRows == 1) findings.push_back({{"id", "row-counter-is-helper-sink-return-count"},
            {"severity", "scope"}, {"effect", "A void sink returning normally increments rows even when nothing is retained; rows is not a durable-write receipt."},
            {"minimumSuggestion", "Describe the coverage counter as helper emission attempts/sink returns; use the staged export bytes for retained-file evidence."}});
    });
    one("clock-regression-does-not-underflow-throttle", [] {
        emit(); fakeTick = 9999; emit(); check(logs.size() == 2, "regressed clock suppresses recovery indefinitely");
    });
    unsigned failed = 0;
    for (const auto& value : cases) if (!value.at("passed").get<bool>()) ++failed;
    std::cout << json{{"schemaVersion", 1}, {"passed", failed == 0}, {"scenarios", cases.size()},
        {"failedScenarios", failed}, {"assertions", assertions}, {"cases", cases}, {"findings", findings},
        {"productionEntrypointExecuted", false}, {"hostExecuted", false}, {"deviceOrRegistryApisUsed", false},
        {"scope", "Extracted helper, source structs and identity serializer; real nlohmann JSON and std try_lock; synthetic gate, clock, machine, identity cache and log sink. Not production integration, runtime build, firmware recovery or Steam acceptance."}}.dump(2) << '\n';
    return failed ? 1 : 0;
}
`;
function main() {
  const args = options(process.argv.slice(2));
  if (args.help) {
    console.log('node tools/verify-common-controller-evidence.cjs --output-dir <NEW directory> [--main <main.cpp>] [--vcvars <vcvars64.bat>]');
    return;
  }
  if (process.platform !== 'win32') throw Error('This verification uses the existing Windows MSVC toolchain.');
  const out = path.resolve(args['output-dir']);
  const relative = path.relative(repo, out);
  if (relative === '' || (!relative.startsWith('..' + path.sep) && !path.isAbsolute(relative)))
    throw Error('Output must be a new directory outside the production source checkout.');
  if (fs.existsSync(out)) throw Error('Refusing an existing output directory: ' + out);
  const sourcePath = path.resolve(args.main || path.join(repo, 'native', 'main.cpp'));
  const raw = fs.readFileSync(sourcePath), text = raw.toString('utf8');
  const definitions = [extract(text, 'InputCaptureGamepadSnapshot', 'struct'),
    extract(text, 'HidHidePhysicalIdentity', 'struct'), extract(text, 'hidHideIdentityJson'),
    extract(text, 'inputCaptureCommonFrameEvidence')];
  const helper = definitions[3];
  // Snapshot the header too: concurrent mainline work cannot change fixture bytes.
  const jsonPath = path.join(repo, 'deps', 'json', 'json.hpp');
  const jsonHeader = fs.readFileSync(jsonPath);
  const recipePath = path.join(repo, 'native', 'build_native.bat');
  const recipe = fs.readFileSync(recipePath, 'utf8');
  const recipeVcvars = /call\s+"([^"]*vcvars64\.bat)"/i.exec(recipe);
  const vcvars = path.resolve(args.vcvars || (recipeVcvars && recipeVcvars[1]) || '');
  if (!args.vcvars && !recipeVcvars) throw Error('No vcvars64.bat path found in build_native.bat; provide --vcvars.');
  if (!fs.existsSync(vcvars)) throw Error('Missing existing MSVC environment script: ' + vcvars);
  commandPath(vcvars);
  const fixture = prelude + '\n' + definitions[0].body + '\n' + definitions[1].body + '\n' + fakes + '\n' + definitions[2].body + '\n' + helper.body + '\n' + tests;
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.mkdirSync(out); // exclusive: never reuse another task's outputs
  fs.writeFileSync(path.join(out, 'json.hpp'), jsonHeader);
  fs.writeFileSync(path.join(out, 'common-controller-evidence.cpp'), fixture);
  fs.writeFileSync(path.join(out, 'inputCaptureCommonFrameEvidence.extracted.cpp'), helper.body);
  const binding = { createdAt: new Date().toISOString(), sourcePath, sourceSha256: sha256(raw),
    functions: definitions.map(({ body, ...metadata }) => metadata), jsonHeaderPath: jsonPath,
    jsonHeaderSha256: sha256(jsonHeader), fixtureSha256: sha256(fixture), toolSha256: sha256(fs.readFileSync(__filename)),
    vcvars, recipePath, recipeSha256: sha256(recipe),
    extraction: 'Verbatim definitions from one read of main.cpp; no production entrypoint or SDK code compiled.',
    fakes: ['domain gate', 'clock', 'machine identity', 'synthetic physical cache with controlled real std::mutex try_lock', 'log sink'],
    limitations: 'Host ACK proof is synthetic input to the actual helper. This verifies exact retention, not ACK generation or native/Host integration. No physical hardware, registry, services, firmware or Steam acceptance.' };
  writeJson(out, 'source-binding.json', binding);
  const batch = '@echo off\r\nwhere cl.exe >nul 2>nul\r\n' +
    `if errorlevel 1 call "${vcvars}" >nul\r\n` +
    'if errorlevel 1 exit /b %errorlevel%\r\n' +
    'cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 common-controller-evidence.cpp /Focommon-controller-evidence.obj /Fecommon-controller-evidence.exe /Fdcommon-controller-evidence.pdb\r\n' +
    'exit /b %errorlevel%\r\n';
  fs.writeFileSync(path.join(out, 'compile-fixture.cmd'), batch);
  console.log('Compiling offline extracted helper into ' + out);
  const compiler = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/c', 'compile-fixture.cmd'],
    { cwd: out, windowsHide: true, encoding: 'utf8', timeout: 180000, maxBuffer: 8 * 1024 * 1024 });
  fs.writeFileSync(path.join(out, 'compiler.log'), (compiler.stdout || '') + (compiler.stderr || ''));
  writeJson(out, 'compiler-result.json', { status: compiler.status, signal: compiler.signal, error: compiler.error?.message || null });
  if (compiler.error || compiler.status !== 0) throw Error('Offline fixture compile failed; inspect ' + path.join(out, 'compiler.log'));
  const run = spawnSync(path.join(out, 'common-controller-evidence.exe'), [],
    { cwd: out, windowsHide: true, encoding: 'utf8', timeout: 30000, maxBuffer: 16 * 1024 * 1024 });
  fs.writeFileSync(path.join(out, 'fixture.stdout.json'), run.stdout || '');
  fs.writeFileSync(path.join(out, 'fixture.stderr.log'), run.stderr || '');
  let result;
  try { result = JSON.parse(run.stdout || ''); }
  catch { throw Error('Offline fixture returned no valid JSON; inspect fixture stdout/stderr.'); }
  const current = fs.readFileSync(sourcePath);
  binding.sourceSha256After = sha256(current);
  binding.sourceFileChangedDuringVerification = binding.sourceSha256 !== binding.sourceSha256After;
  try { binding.helperSha256After = extract(current.toString('utf8'), 'inputCaptureCommonFrameEvidence').sha256; }
  catch (error) { binding.helperRecheckError = error.message; }
  binding.verifiedHelperStillCurrent = binding.helperSha256After === helper.sha256;
  writeJson(out, 'source-binding.json', binding);
  result.compilerExitCode = compiler.status; result.fixtureExitCode = run.status;
  result.fixtureProcessError = run.error?.message || null;
  result.sourceBindingFile = 'source-binding.json'; result.verifiedHelperSha256 = helper.sha256;
  result.verifiedHelperStillCurrent = binding.verifiedHelperStillCurrent;
  result.passed = result.passed === true && !run.error && run.status === 0;
  // Do not round-trip the C++ observations through JS Number: uint64 proof
  // sequences can exceed 2^53. Preserve their exact serialized numeric tokens.
  const metadata = { compilerExitCode: result.compilerExitCode, fixtureExitCode: result.fixtureExitCode,
    fixtureProcessError: result.fixtureProcessError, sourceBindingFile: result.sourceBindingFile,
    verifiedHelperSha256: result.verifiedHelperSha256, verifiedHelperStillCurrent: result.verifiedHelperStillCurrent,
    toolPassed: result.passed };
  const retained = (run.stdout || '').trimEnd();
  fs.writeFileSync(path.join(out, 'verification-result.json'), retained.slice(0, -1) + ',\n' + JSON.stringify(metadata, null, 2).slice(1) + '\n');
  const summary = { passed: result.passed, scenarios: result.scenarios, assertions: result.assertions,
    failedScenarios: result.failedScenarios, findings: result.findings, outputDirectory: out,
    verifiedHelperSha256: helper.sha256, verifiedHelperStillCurrent: binding.verifiedHelperStillCurrent };
  console.log(JSON.stringify(summary, null, 2));
  if (!result.passed) process.exitCode = 1;
  else if (!binding.verifiedHelperStillCurrent) { console.error('Helper changed during verification; this receipt covers the extracted snapshot only.'); process.exitCode = 2; }
}
try { main(); }
catch (error) { console.error(error.stack || error.message); process.exitCode = 1; }