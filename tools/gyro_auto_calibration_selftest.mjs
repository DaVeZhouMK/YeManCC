import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

// Compile the actual production calibration state machine against private fake
// motion/clock/persistence/event APIs. No production binary or DLL is executed.
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.resolve(process.argv[2] || path.join(root, '../../Build/Validation/GyroAutoCalibration'));
fs.mkdirSync(out, { recursive: true });
const sourceFile = path.join(root, 'native/main.cpp');
const source = fs.readFileSync(sourceFile, 'utf8');
const sha = (s) => crypto.createHash('sha256').update(s).digest('hex');
const witness = [];
function fragment(startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start + startMarker.length);
  if (start < 0 || end < 0 || source.indexOf(startMarker, start + 1) >= 0) throw new Error(`Source anchor changed: ${startMarker}`);
  const text = source.slice(start, end).trimEnd();
  witness.push({ file: sourceFile, line: source.slice(0, start).split('\n').length, marker: startMarker, sha256: sha(text), unchanged: true });
  return text;
}
const tick = fragment('static void inputCaptureGyroCalibrationTick(', 'static void inputCaptureLoadGamepadMotion() {');
const declarations = fragment('enum class GyroCalibStage : int {', '// 7.44 修订（2026-09-11）：T2b ResetGamepadMotion');
const scalarNames = ['g_gyroCalibDeadline', 'g_gyroCalibConfidence', 'g_gyroCalibOx'];
const scalars = scalarNames.map((name) => {
  const line = source.split(/\r?\n/).find((l) => l.startsWith('static ') && l.includes(` ${name} =`));
  if (!line) throw new Error(`Missing source state ${name}`);
  return line;
}).join('\n');
const replayInput = process.argv[3] ? path.resolve(process.argv[3]) : null;
const replayWitness = [];
let replayTests = '';
if (replayInput) {
  const bytes = fs.readFileSync(replayInput);
  replayWitness.push({ file: replayInput, sha256: sha(bytes), limitation: 'Recorded samples only; each sample is held for the dwell to check the magnitude/start gate. Not a continuous calibration/DLL replay or verified physical stillness.' });
  const groups = JSON.parse(bytes.toString('utf8'));
  for (const [index, group] of groups.entries()) {
    if (!Array.isArray(group.rows) || (group.oneHzOnly !== true && group.gateOnly !== true)) throw new Error('Replay fixture shape changed');
    const msi = group.family === 'msi';
    const previousThresholdDps = msi ? 3 : 2;
    const expectedThresholdDps = msi ? 4 : 2.5;
    const familySetup = msi ? 'g_machineIdentity.family=YmccFamily::MsiClaw;' : '';
    const rows = group.rows.map(({ gyro }) => {
      const axes = ['x', 'y', 'z'].map((k) => Math.fround(gyro?.[k]));
      if (!axes.every(Number.isFinite)) throw new Error('Recorded fixture contains nonfinite gyro');
      const l1 = Math.fround(Math.fround(Math.abs(axes[0]) + Math.abs(axes[1])) + Math.abs(axes[2]));
      return `{${axes.map((v) => `(float)(${v})`).join(',')},${l1 < expectedThresholdDps ? 1 : 0}}`;
    });
    if (!rows.length) throw new Error('Empty replay group');
    replayTests += `\n    { static const float samples${index}[][4] = {${rows.join(',')}};\n` +
      `      unsigned oldAccepted=0,newAccepted=0,gained=0; bool agree=true;\n` +
      `      for (const auto& row : samples${index}) { reset(); ${familySetup} fakeX=row[0]; fakeY=row[1]; fakeZ=row[2]; runAt(1000); runAt(4000);\n` +
      `        const bool old=fabsf(row[0])+fabsf(row[1])+fabsf(row[2])<(float)(${previousThresholdDps}); const bool current=started();\n` +
      `        oldAccepted+=old;newAccepted+=current;gained+=!old&&current;agree&=current==(row[3]!=0); }\n` +
      `      check("recorded-sample-start-gate-replay-${index}",agree);tests.back()["loggedRows"]=std::size(samples${index});\n` +
      `      tests.back()["family"]=${JSON.stringify(msi ? 'msi' : 'other')};
      tests.back()["oneHzOnly"]=${group.oneHzOnly === true};
      tests.back()["previousThresholdDps"]=${previousThresholdDps};tests.back()["expectedThresholdDps"]=${expectedThresholdDps};
      tests.back()["oldGateAccepted"]=oldAccepted;tests.back()["newGateAccepted"]=newAccepted;tests.back()["newLowMagnitudeBand"]=gained;\n` +
      `    }\n`;
  }
}
const cpp = String.raw`
#include <atomic>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "json.hpp"
#include "gyro_calibration_policy.h"
using UINT64 = std::uint64_t;
enum class YmccFamily { Other, MsiClaw };
static struct { YmccFamily family=YmccFamily::Other; } g_machineIdentity;
using json = nlohmann::json;
using ULONGLONG = std::uint64_t;
static ULONGLONG clockTick = 1000;
static ULONGLONG GetTickCount64() { return clockTick; }
static int writes = 0, stores = 0, resets = 0, modes = 0, resubscribe = 0;
static float fakeX = 0, fakeY = 0, fakeZ = 0, fakeConfidence = 0;
static bool fakeSteady = false;
static std::vector<json> events;
struct InputCaptureSensorMeta { bool gyroPresent = true; std::string gyroDeviceId = "FAKE-MSI"; };
static InputCaptureSensorMeta meta;
static InputCaptureSensorMeta inputCaptureSensorMetaSnapshot() { return meta; }
static struct { bool autoCalibrate = true; } g_realStickTest;
static std::atomic<bool> g_gmSessionResetRequested{false};
static std::atomic<const char*> g_gmSessionResetReason{"synthetic"};
static void ymccMadgwickResetOrientation() {}
static void fakeReset(void*) { ++resets; }
static void fakeSetMode(void*, int) { ++modes; }
static float fakeGetConfidence(void*) { return fakeConfidence; }
static void fakeGetOffset(void*, float* x, float* y, float* z) { *x = .2f; *y = -.4f; *z = .9f; }
static void fakeSetOffset(void*, float, float, float, int) { ++writes; }
static void fakeGetCal(void*, float* x, float* y, float* z) { *x = fakeX; *y = fakeY; *z = fakeZ; }
static bool fakeIsSteady(void*) { return fakeSteady; }
static void* g_gm = reinterpret_cast<void*>(1);
static auto g_gmResetCal = &fakeReset;
static auto g_gmResetMotionApi = &fakeReset;
static auto g_gmSetMode = &fakeSetMode;
static auto g_gmGetConf = &fakeGetConfidence;
static auto g_gmGetOffset = &fakeGetOffset;
static auto g_gmSetOffset = &fakeSetOffset;
static auto g_gmGetCal = &fakeGetCal;
static auto g_gmIsSteady = &fakeIsSteady;
static void inputCaptureStoreCalibration(const std::string&, float, float, float, int, float) { ++stores; }
enum class SensorThreadRequest { Resubscribe };
static void inputCaptureSensorThreadPost(SensorThreadRequest) { ++resubscribe; }
static void appendNativeLifecycleLog(const char* event, json detail) { detail["event"] = event; events.push_back(std::move(detail)); }
static void gamepadSerialPostEvent(const char* event, json detail) { detail["event"] = event; events.push_back(std::move(detail)); }
__DECLARATIONS__
__SCALARS__
// This fixture exercises the existing Other/MSI path. Reviewed Windows models
// have a separate real-DLL/source-owner transaction fixture.
namespace ymcc::windows_motion { struct Stamp {}; }
static struct { struct { bool active=false; } transaction; } g_reviewedCalibrationContext;
static const void* inputCaptureReviewedCalibrationRecipe() { return nullptr; }
static const void* inputCaptureSourceQualifiedCalibrationRecipe() { return nullptr; }
static void inputCaptureReviewedCalibrationObserve(const ymcc::windows_motion::Stamp*, const InputCaptureSensorMeta*, const InputCaptureSensorMeta&) { throw std::runtime_error("Legacy entered reviewed observation"); }
static void inputCaptureReviewedCalibrationTick(const ymcc::windows_motion::Stamp*, const InputCaptureSensorMeta*, const InputCaptureSensorMeta&) { throw std::runtime_error("Legacy entered reviewed transaction"); }
__TICK__
static json tests = json::array();
static void reset() {
    clockTick = 1000; fakeX = 0; fakeY = 0; fakeZ = 0; fakeConfidence = 0; fakeSteady = false;
    writes = 0; stores = 0; resets = 0; modes = 0; resubscribe = 0; events.clear();
    meta = {}; g_realStickTest.autoCalibrate = true;
    g_gm = reinterpret_cast<void*>(1); g_gmResetCal = &fakeReset; g_gmResetMotionApi = &fakeReset;
    g_gmSetMode = &fakeSetMode; g_gmGetConf = &fakeGetConfidence; g_gmGetOffset = &fakeGetOffset;
    g_gmSetOffset = &fakeSetOffset; g_gmGetCal = &fakeGetCal; g_gmIsSteady = &fakeIsSteady;
    g_gmSessionResetRequested = false; g_gyroCalibStageAtomic = GyroCalibStage::None;
    g_gyroCalibRequested = false; g_calibSourceAuto = false; g_autoCalibTrusted = false;
    g_autoCalibFinished = false; g_autoCalibAttempts = 0; g_calibUiStatus = 0;
    g_autoCalibWindowStart = 0; g_autoStillSince = 0; g_autoStillLastSteadyTick = 0;
    g_machineIdentity.family=YmccFamily::Other;
    g_gyroCalibFreshPairThisTick=true;g_gyroCalibBindingThisTick=2;g_gyroCalibSessionBinding=0;
    g_gyroCalibSessionDeviceId.clear();g_gyroCalibPollWindowMs=5000;
    g_gyroCalibProcessedTicks=0;g_gyroCalibMsiSession=false;
    g_gmProcessedThisTick = true; g_gyroCalibDeadline = 0; g_gyroCalibConfidence = 0;
    g_gyroCalibOx = g_gyroCalibOy = g_gyroCalibOz = 0;
}
static void runAt(ULONGLONG t) { clockTick = t; inputCaptureGyroCalibrationTick(); }
static bool eventExists(const char* s) { for (const auto& e : events) if (e.value("event", "") == s) return true; return false; }
static bool started() { return eventExists("gyro-auto-calibrate-start"); }
static void check(const std::string& name, bool pass) {
    tests.push_back({{"name", name}, {"passed", pass}, {"offsetWrites", writes}, {"stores", stores}, {"attempts", g_autoCalibAttempts.load()}});
    if (!pass) throw std::runtime_error(name);
}
static void collectAndFinish(float confidence) {
    reset(); fakeX = .7f; fakeY = .8f; fakeZ = .9f;
    runAt(1000); runAt(4000); runAt(4008);
    fakeConfidence = confidence;
    runAt(4016); runAt(9008); runAt(9016); runAt(9024);
}
int main(int argc, char** argv) {
 try {
    if (argc != 2) throw std::runtime_error("One output path is required");
    check("limits-unchanged-except-start-magnitude", kAutoCalibStillMagnitudeDps == 2.5f &&
        kAutoCalibStillMs == 3000 && kAutoCalibStillGraceMs == 250 && kAutoCalibWindowMs == 300000 &&
        kAutoCalibMaxAttempts == 3 && kCalibTrustedConfidence == .99f);
    reset(); fakeX = .5f; runAt(1000); runAt(3999); check("rog-small-signal-requires-full-3s", !started());
    runAt(4000); check("rog-small-signal-still-starts", started() && resets == 1 && g_autoCalibAttempts == 1);
    reset(); fakeX = .7f; fakeY = .8f; fakeZ = .9f; runAt(1000); runAt(4000);
    check("msi-2.4-dps-band-now-starts", started() && g_autoCalibAttempts == 1);
    reset(); fakeX = std::nextafter(2.5f, 0.f); runAt(1000); runAt(4000);
    check("threshold-immediately-below-2.5-is-included", started());
    reset(); fakeX = 2.5f; runAt(1000); runAt(9000); check("exact-2.5-still-rejected", !started());
    reset(); fakeX = 2.5001f; runAt(1000); runAt(9000); check("above-2.5-rejected", !started());
    reset(); fakeX = 20; runAt(1000); runAt(9000); check("moving-20-dps-never-starts", !started() && g_autoCalibAttempts == 0);
    reset(); fakeX = -.7f; fakeY = -.8f; fakeZ = -.9f; runAt(1000); runAt(4000); check("negative-axes-use-absolute-sum", started());
    reset(); fakeX = -1; fakeY = 1; fakeZ = 1; runAt(1000); runAt(9000); check("opposite-signs-cannot-cancel-motion", !started());
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        reset(); fakeY = invalid; runAt(1000); runAt(9000); check("nonfinite-axis-rejected-" + std::to_string(tests.size()), !started());
    }
    reset(); fakeX = 2.1f; runAt(1000); runAt(3750); fakeX = 2.7f; runAt(4000);
    check("250ms-noise-grace-does-not-start-on-bad-frame", !started() && g_autoStillSince == 1000);
    fakeX = 2.1f; runAt(4008); check("steady-return-after-grace-starts", started());
    reset(); fakeX = 2.1f; runAt(1000); runAt(3750); fakeX = 2.7f; runAt(4001);
    check("251ms-instability-resets-start-clock", !started() && g_autoStillSince == 0);
    fakeX = 2.1f; runAt(4010); runAt(7009); check("fresh-3s-needed-after-reset", !started());
    runAt(7010); check("fresh-3s-completes-after-reset", started());
    reset(); g_realStickTest.autoCalibrate = false; runAt(1000); runAt(9000); check("user-auto-off-no-start", !started());
    reset(); meta.gyroPresent = false; runAt(1000); runAt(9000); check("missing-gyro-no-start", !started());
    reset(); g_gmProcessedThisTick = false; runAt(1000); runAt(9000); check("unprocessed-frame-no-start", !started());
    reset(); g_autoCalibAttempts = 3; runAt(1000); runAt(4000); check("3-attempt-limit-preserved", !started());
    runAt(301001); runAt(304001); check("5min-budget-rolls-not-permanent-failure", started() && g_autoCalibAttempts == 1);
    reset(); g_autoCalibTrusted = true; runAt(1000); runAt(9000); check("trusted-calibration-does-not-restart", !started());
    reset(); g_autoCalibFinished = true; runAt(1000); runAt(9000); check("finished-calibration-does-not-restart", !started());
    reset(); g_gmGetCal = nullptr; runAt(1000); runAt(9000); check("no-magnitude-api-does-not-fabricate-steady", !started());
    reset(); g_gmGetConf = nullptr; runAt(1000); runAt(9000); check("missing-required-api-no-start", !started());
    reset(); g_gmProcessedThisTick = false; g_gyroCalibRequested = true; runAt(1000);
    check("manual-request-still-bypasses-auto-magnitude-only", g_gyroCalibStageAtomic == GyroCalibStage::EnterFusion);
    collectAndFinish(0.f); check("recorded-confidence0-never-writes-offset", stores == 0 && writes == 0 && !g_autoCalibTrusted);
    collectAndFinish(.584137f); check("msi-confidence58percent-never-saved", stores == 0 && writes == 0 && !g_autoCalibTrusted);
    collectAndFinish(.9846224f); check("rog-confidence98.46percent-never-saved", stores == 0 && writes == 0 && !g_autoCalibTrusted);
    collectAndFinish(.99f); check("exact99percent-still-not-saved", stores == 0 && writes == 0 && !g_autoCalibTrusted);
    collectAndFinish(.99213463f); check("msi15-58-success-remains-trusted", stores == 1 && writes == 1 && g_autoCalibTrusted && resubscribe == 1);
    collectAndFinish(.9925161f); check("msi19-49-success-remains-trusted", stores == 1 && writes == 1 && g_autoCalibTrusted && resubscribe == 1);
    collectAndFinish(.99182516f); check("rog-success-remains-trusted", stores == 1 && writes == 1 && g_autoCalibTrusted && resubscribe == 1);
    reset();g_machineIdentity.family=YmccFamily::MsiClaw;fakeX=2.8f;runAt(1000);runAt(4000);
    check("msi-2.8-noise-band-starts-after-3s",started());runAt(4008);
    check("msi-session-window-is-15s",g_gyroCalibDeadline==19008 && g_gyroCalibMsiSession);
    fakeConfidence=.36f;runAt(9008);
    check("msi-low-confidence-at-5s-keeps-sampling",g_gyroCalibStageAtomic==GyroCalibStage::PollWait && writes==0 && stores==0);
    fakeConfidence=1.f;runAt(13008);runAt(13016);runAt(13024);
    check("msi-trusted-before-15s-finishes-early",stores==1 && writes==1 && g_autoCalibTrusted && resubscribe==1);
    for(float magnitude:{3.0f,3.2f,3.5f,3.9f,std::nextafter(4.f,0.f),4.f,4.01f,20.f}) {
        reset();g_machineIdentity.family=YmccFamily::MsiClaw;fakeX=magnitude;runAt(1000);runAt(4000);
        check("msi-4dps-start-boundary-"+std::to_string(magnitude),started()==(magnitude<4.f));
    }
    for(int invalidation=0;invalidation<4;++invalidation) {
        reset();g_machineIdentity.family=YmccFamily::MsiClaw;g_gyroCalibRequested=true;runAt(1000);runAt(1008);fakeConfidence=1.f;
        if(invalidation==0)g_gyroCalibFreshPairThisTick=false;
        if(invalidation==1)g_gyroCalibBindingThisTick=3;
        if(invalidation==2)meta.gyroDeviceId="OTHER-MSI";
        if(invalidation==3)g_gmProcessedThisTick=false;
        runAt(16008);runAt(16016);runAt(16024);
        check("msi-invalid-sensor-pair-cannot-use-stale-confidence-"+std::to_string(invalidation),stores==0 && writes==0 && !g_autoCalibTrusted);
    }
    reset();g_machineIdentity.family=YmccFamily::MsiClaw;g_gyroCalibRequested=true;runAt(1000);runAt(1008);fakeConfidence=0.f;
    runAt(16008);runAt(16016);runAt(16024);
    check("msi-15s-timeout-confidence0-still-not-saved",stores==0 && writes==0 && g_gyroCalibStageAtomic==GyroCalibStage::None);
    const auto admits=[](bool msi,bool api,bool wanted,bool gyro,bool accel,bool provider,bool selector,
        UINT64 gg,UINT64 ag,UINT64 active,bool fresh){return ymcc::gyro_calibration::sensorCalibrationAdmitted(msi,api,wanted,gyro,accel,provider,selector,gg,ag,active,fresh);};
    check("proven-msi-sensors-admit-without-controller",admits(true,true,true,true,true,true,true,2,2,2,true));
    for(int gate=0;gate<8;++gate) {
        bool bits[]={true,true,true,true,true,true,true,true};bits[gate]=false;
        check("sensor-only-admission-gate-"+std::to_string(gate),!admits(bits[0],bits[1],bits[2],bits[3],bits[4],bits[5],bits[6],2,2,2,bits[7]));
    }
    check("stale-binding-rejected",!admits(true,true,true,true,true,true,true,2,2,3,true));
    check("different-bindings-rejected",!admits(true,true,true,true,true,true,true,2,3,2,true));
    check("zero-binding-rejected",!admits(true,true,true,true,true,true,true,0,0,0,true));
    check("350ms-receipt-boundary-kept",ymcc::gyro_calibration::freshPair(1000,650,650,1,1));
    check("351ms-old-receipt-rejected",!ymcc::gyro_calibration::freshPair(1000,649,650,1,1));
    check("future-receipt-rejected",!ymcc::gyro_calibration::freshPair(1000,1001,650,1,1));
    check("missing-sequence-rejected",!ymcc::gyro_calibration::freshPair(1000,900,900,0,1));
__REPLAY_TESTS__
    std::ofstream result(argv[1]); result << json{{"passed", tests.size()}, {"total", tests.size()}, {"tests", tests},
        {"productionFunctionExtracted", true}, {"realHardwareAccess", false}, {"oemDllExecuted", false},
        {"syntheticConfidenceNotDllReplay", true}}.dump(2);
    std::cout << tests.size() << "/" << tests.size() << " calibration tests passed\n"; return 0;
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
`.replace('__DECLARATIONS__', declarations).replace('__SCALARS__', scalars).replace('__TICK__', tick).replace('__REPLAY_TESTS__', replayTests);
if (/(?:DllImport|LoadLibrary|GetProcAddress|RegSet|CreateProcess|TerminateProcess|\bSleep\s*\()/.test(cpp)) throw new Error('Fixture contains real OS/OEM entry points');
const fixture = path.join(out, 'fixture.cpp');
fs.writeFileSync(fixture, cpp);
fs.writeFileSync(path.join(out, 'source-witnesses.json'), JSON.stringify({ sourceSHA256: sha(source), witness, replayWitness, fixtureSHA256: sha(cpp), fakeIoOnly: true }, null, 2));
const vcvars = process.env.YMCC_VCVARS64 || String.raw`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat`;
if (!fs.existsSync(vcvars)) throw new Error(`Build environment not found: ${vcvars}`);
const command = `call "${vcvars}" >nul && cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /I"${path.join(root, 'deps/json')}" /I"${path.join(root, 'native')}" "${fixture}" /Fo"${path.join(out, 'fixture.obj')}" /Fe"${path.join(out, 'fixture.exe')}"`;
const built = spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/s', '/c', command], { encoding: 'utf8', cwd: out, windowsHide: true, windowsVerbatimArguments: true });
fs.writeFileSync(path.join(out, 'build.log'), `${built.stdout || ''}${built.stderr || ''}`);
if (built.status !== 0) throw new Error(`Calibration fixture compilation failed: ${built.status}\n${built.stdout}\n${built.stderr}`);
const run = spawnSync(path.join(out, 'fixture.exe'), [path.join(out, 'results.json')], { encoding: 'utf8', cwd: out, windowsHide: true });
fs.writeFileSync(path.join(out, 'run.log'), `${run.stdout || ''}${run.stderr || ''}`);
if (run.status !== 0) throw new Error(`Calibration fixture failed: ${run.stdout}\n${run.stderr}`);
console.log(run.stdout.trim());
console.log(`Evidence: ${path.join(out, 'results.json')}`);
