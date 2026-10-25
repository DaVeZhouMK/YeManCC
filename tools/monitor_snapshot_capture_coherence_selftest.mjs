// Actual production capture/mode/publication functions, instrumented mutexes
// and deterministic thread barriers. No collector, device, app or Steam IO.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
const root = process.cwd().replaceAll('\\', '/');
const task = path.resolve(process.env.YMCC_CAPTURE_COHERENCE_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/snapshot-mode-coherence-20261017').replaceAll('\\', '/');
const probe = process.argv.includes('--probe'), out = task + (probe ? '/probe' : '/accepted');
fs.mkdirSync(out, { recursive: true });
const source = fs.readFileSync('native/main.cpp', 'utf8');
function extract(signature) {
  const start = source.indexOf(signature); assert(start >= 0, signature);
  const tail = source.slice(start), mask = tail.replace(/\/\/[^\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, m => ' '.repeat(m.length));
  let depth = 0;
  for (let i = mask.indexOf('{'); i < mask.length; ++i) { if (mask[i] === '{') ++depth; if (mask[i] === '}' && !--depth) return tail.slice(0, i + 1); }
  throw Error('Unterminated ' + signature);
}
const state = source.match(/struct MonitorSnapshotState \{[\s\S]*?\n\};/)[0];
const captureState = source.match(/struct MonitorDetailSnapshotCapture \{[\s\S]*?\n\};/)[0];
const modeChange = extract('static void monitorSnapshotOnModeChange('), setMode = extract('static void nativeMonitorSetMode('), publish = extract('static void monitorSnapshotPublish(');
const before = {}, after = {};
for (const [name, signature] of Object.entries({ serve: 'static json monitorSnapshotServeFn()', detail: 'static MonitorDetailSnapshotCapture monitorSnapshotRecordForDetail(' })) {
  before[name] = fs.readFileSync('tools/fixtures/monitor_snapshot_' + name + '_coherence_before_20261017.inc', 'utf8');
  after[name] = probe ? fs.readFileSync(task + '/proposed-' + name + '.inc', 'utf8') : extract(signature);
}
const cpp = fs.readFileSync('tools/monitor_snapshot_capture_coherence_selftest.cpp', 'utf8');
const declarations = String.raw`
static AuditMutex g_nativeMonitorMx{false},g_monitorSnapshotMx{true};
static MonitorSnapshotState g_monitorSnapshot;
static bool g_nativeMonitorTop=false,g_nativeMonitorFps=false,g_nativeMonitorStop=false;
static bool g_nativeMonitorFileSystem=true,g_nativeMonitorFileSystemExplicit=false;
static std::atomic<unsigned long long> g_monitorRelayEpoch{4},g_monitorSnapshotServes{0},g_monitorSnapshotOversizeDrops{0};
struct IdleThread { bool joinable() const{return true;} IdleThread& operator=(std::thread&& value){if(value.joinable())value.join();return *this;} };
static IdleThread g_nativeMonitorThread;
struct Notify { void notify_all(){} };static Notify g_nativeMonitorCv;
static void nativeMonitorLoop(){}
static unsigned long long nativeMonitorSessionId(){return 77;}
static void monitorTestBarrierPause(int){}
`;
const instrument = text => text.replaceAll('std::mutex', 'AuditMutex');
const body = side => 'namespace ' + side + ' {\n' + declarations + '\n' + [modeChange, setMode, publish, side === 'before' ? before.serve : after.serve, side === 'before' ? before.detail : after.detail].map(instrument).join('\n') + '\n}\n';
const code = cpp.replaceAll('__ROOT__', root).replace('__STATE__', state + '\n' + captureState).replace('__FUNCTIONS__', body('before') + body('after'));
fs.writeFileSync(out + '/capture-coherence.cpp', code);
const batch = ['@echo off','call "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat" >nul','if errorlevel 1 exit /b %errorlevel%',
  `cl /nologo /std:c++20 /utf-8 /EHsc /O2 /W4 /MT "${out}/capture-coherence.cpp" /Fo"${out}/capture-coherence.obj" /Fe"${out}/capture-coherence.exe"`,
  'if errorlevel 1 exit /b %errorlevel%',`"${out}/capture-coherence.exe" "${out}/RESULT.json"`,'exit /b %errorlevel%',''].join('\r\n');
fs.writeFileSync(out + '/run.cmd', batch);
const run = spawnSync('cmd.exe', ['/d','/c',out + '/run.cmd'], { encoding: 'utf8', windowsHide: true, timeout: 60000, maxBuffer: 4e6 });
fs.writeFileSync(out + '/COMPILE-RUN.log', (run.stdout || '') + (run.stderr || ''));
assert.equal(run.status, 0, (run.stdout || '') + (run.stderr || ''));
const result = JSON.parse(fs.readFileSync(out + '/RESULT.json', 'utf8'));
const sha = text => crypto.createHash('sha256').update(text).digest('hex');
result.proposedOnly = probe;
result.sourcePins = { state: sha(state), captureState: sha(captureState), modeChange: sha(modeChange), setMode: sha(setMode), publish: sha(publish), before: Object.fromEntries(Object.entries(before).map(([k,v])=>[k,sha(v)])), after: Object.fromEntries(Object.entries(after).map(([k,v])=>[k,sha(v)])), contract: sha(fs.readFileSync('native/monitor_snapshot_contract.h')), detailHeader: sha(fs.readFileSync('native/monitor_detail_log.h')) };
fs.writeFileSync(out + '/RESULT.json', JSON.stringify(result, null, 2));
console.log(JSON.stringify({ ok: result.ok, proposedOnly: probe, cases: result.cases, beforeMixed: result.beforeMixed, afterMixed: result.afterMixed, stress: result.stress }, null, 2));
