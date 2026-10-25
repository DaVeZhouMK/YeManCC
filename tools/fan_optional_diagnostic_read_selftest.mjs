// Actual Fan feature initialization, diagnostics and IPC. Settings storage,
// native replies, process and Host are inert; deadlines use a virtual clock.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createHash, webcrypto } from 'node:crypto';
import { build } from 'esbuild';

const root = path.resolve(import.meta.dirname, '..');
const output = path.resolve(process.argv[2]);
const diagnosticSource = process.argv[3] ? path.resolve(process.argv[3]) : path.join(root, 'src/bridge/fanDiagnostics.ts');
fs.mkdirSync(output, { recursive: true });
const digest = p => createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const bindingPaths = [diagnosticSource, ...[
  'src/bridge/fanFeature.ts', 'src/bridge/fanHost.ts', 'src/bridge/ipc.ts',
  'src/bridge/api.ts', 'tools/fixtures/fan_no_io_host.ts',
].map(p => path.join(root, p))];
const sourceBinding = bindingPaths.map(p => ({ path: p, sha256: digest(p) }));
const stubs = {
  './topmon': 'export const topMonitorData = {value:{tempC:50}};',
  './settingsRepository': `
    export async function updateSettings() { throw Error('Unexpected settings transaction in diagnostic initialization'); }
    export async function readSettingsSection(section) {
      if (section !== 'fan') throw Error('Unexpected settings read');
      const f = globalThis.__fanInitFixture; f.reads++; return structuredClone(f.saved);
    }
    export async function saveSettingsSection(section, value) {
      if (section !== 'fan') throw Error('Unexpected settings write');
      const f = globalThis.__fanInitFixture; f.saves.push(structuredClone(value));
      Object.assign(f.saved, structuredClone(value));
    }
  `,
};
const compiled = await build({
  stdin: {
    resolveDir: root, sourcefile: 'fan-optional-read-entry.ts',
    contents: "export * from './src/bridge/fanFeature';export * from './src/bridge/fanDiagnostics';export {FanHostLifecycle} from './src/bridge/fanHost';export {NoIoHost} from './tools/fixtures/fan_no_io_host';export {rejectAllPending} from './src/bridge/ipc';",
  },
  bundle: true, write: false, format: 'iife', globalName: 'FanTest',
  platform: 'browser', target: 'es2022', define: {'import.meta.env': '{}'},
  plugins: [{name: 'inert-settings-and-shared-monitor', setup(b) {
    b.onResolve({filter: /^\.\//}, a =>
      path.dirname(a.importer) === path.join(root, 'src/bridge') && a.path in stubs
        ? {path: a.path, namespace: 'fan-init-fixture'} : undefined);
    b.onLoad({filter: /.*/, namespace: 'fan-init-fixture'}, a => ({contents: stubs[a.path], loader: 'ts'}));
    b.onLoad({filter: /[\\/]src[\\/]bridge[\\/]fanDiagnostics\.ts$/}, () => ({
      contents: fs.readFileSync(diagnosticSource, 'utf8'), loader: 'ts', resolveDir: path.join(root, 'src/bridge'),
    }));
  }}],
});
const code = compiled.outputFiles[0].text;
const plain = value => JSON.parse(JSON.stringify(value));
const nodes = [{tempC:0,dutyPercent:10},{tempC:40,dutyPercent:35},{tempC:70,dutyPercent:75},{tempC:100,dutyPercent:100}];
function fixture(mode = 'missing', savedLogging = true) {
  let now = 0, nextId = 0, listener, held, host, life, starts = 0;
  const timers = new Map(), calls = [], data = {
    reads: 0, saves: [], saved: {featureEnabled:true, configured:true, deviceIdentity:{name:'remembered'},
      preset:'aggressive', motionEnabled:false, diagnosticLoggingEnabled:savedLogging, nodes:structuredClone(nodes)},
  };
  const deliver = (msg, result, error) => listener?.({data: error ? {id:msg.id,error} : {id:msg.id,result}});
  const win = new EventTarget();
  function setTimer(fn, ms, interval = false) {
    const id = ++nextId, delay = Math.max(1, Number(ms) || 1);
    timers.set(id, {fn, due:now+delay, period:interval?delay:0}); return id;
  }
  const clear = id => timers.delete(id);
  win.setTimeout = (fn, ms) => setTimer(fn, ms);
  win.clearTimeout = clear;
  win.setInterval = (fn, ms) => setTimer(fn, ms, true);
  win.clearInterval = clear;
  win.chrome = {webview: {
    addEventListener: (_type, fn) => listener = fn,
    postMessage(msg) {
      calls.push(msg);
      if (msg.cmd === 'fanLog.write') { queueMicrotask(() => deliver(msg, {ok:true})); return; }
      if (msg.cmd !== 'fanLog.setEnabled') throw Error('Unexpected IPC: '+msg.cmd);
      if (Object.hasOwn(msg.args ?? {}, 'enabled')) {
        queueMicrotask(() => deliver(msg, {enabled:msg.args.enabled})); return;
      }
      held = value => deliver(msg, {enabled:value});
      if (mode === 'missing') return;
      queueMicrotask(() => mode === 'reject'
        ? deliver(msg, undefined, 'unknown: fanLog.setEnabled') : deliver(msg, {enabled:mode==='on'}));
    },
  }};
  const context = vm.createContext({
    window:win, document:Object.assign(new EventTarget(), {hidden:false,visibilityState:'visible',
      createElement:()=>({style:{},content:{}}),querySelector:()=>null}),
    console, Event, EventTarget, CustomEvent:class extends Event {constructor(type, init){super(type);this.detail=init?.detail;}},
    setTimeout:win.setTimeout, clearTimeout:clear, setInterval:win.setInterval, clearInterval:clear,
    URL, TextEncoder, TextDecoder, crypto:webcrypto, structuredClone, queueMicrotask,
    performance:{now:()=>now}, __fanInitFixture:data,
  });
  vm.runInContext(code, context, {timeout:5000});
  const api = context.FanTest;
  const observe = promise => {
    const result = {status:'pending',value:null,error:null};
    void promise.then(value=>{result.status='fulfilled';result.value=value;}, error=>{result.status='rejected';result.error=String(error);});
    return result;
  };
  async function flush() { for (let i=0;i<80;i++) await Promise.resolve(); await new Promise(setImmediate); }
  async function advance(ms) {
    await flush(); const target=now+ms;
    for (let i=0;i<1000;i++) {
      const row=[...timers.entries()].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];
      if (!row) break;
      const [id,t]=row; now=t.due;
      if (t.period) t.due+=t.period; else timers.delete(id);
      t.fn(); await flush();
    }
    now=target; await flush();
  }
  async function createLifecycle() {
    host = new api.NoIoHost();
    life = new api.FanHostLifecycle({enabled:true,adapter:host,
      readNativeActivity:async()=>null,
      readNativePowerState:async()=>({generation:0,phase:'ready',resumeReady:true,hardwareWritesAllowed:true}),
      launcher:{start:async()=>({pid:111601+starts++,executable:'inert.exe'}),stop:async()=>{}},
    });
    await life.start(); return life;
  }
  async function done() {
    const closing=life?.close().catch(()=>{});
    api.rejectAllPending('fixture cleanup'); await flush();
    if (closing) await closing;
    timers.clear();
  }
  return {api,data,calls,timers,observe,flush,advance,createLifecycle,done,
    lateReply:value=>held?.(value),get starts(){return starts;},get host(){return host;}};
}
const rows=[];
async function check(name, run) {
  try { const facts=await run(); rows.push({name,ok:true,...(facts?{facts}:{})}); console.log('PASS '+name); }
  catch(error) { rows.push({name,ok:false,error:String(error)}); console.log('FAIL '+name+': '+error); }
}
const queryCalls = f => f.calls.filter(c=>c.cmd==='fanLog.setEnabled' && !Object.hasOwn(c.args??{},'enabled'));
const writeCalls = f => f.calls.filter(c=>c.cmd==='fanLog.setEnabled' && Object.hasOwn(c.args??{},'enabled'));
await check('optional diagnostic read settles as unknown within one second after a lost acknowledgement',async()=>{
  const f=fixture();
  try {
    const p=f.observe(f.api.readFanDiagnosticLogging());
    await f.advance(999); assert.equal(p.status,'pending');
    await f.advance(1); assert.equal(p.status,'fulfilled','optional logging read still owns initialization');
    assert.equal(p.value,null); assert.equal(queryCalls(f).length,1); assert.equal(writeCalls(f).length,0);
    assert.equal(f.timers.size,0);
  } finally {await f.done();}
});
await check('concurrent initialization shares one read and keeps saved curves/settings on missing diagnostic reply',async()=>{
  const f=fixture();
  try {
    const requests=Array.from({length:10},()=>f.observe(f.api.initializeFanFeature()));
    await f.advance(1000);
    assert(requests.every(p=>p.status==='fulfilled'),'Fan feature initialization never finished');
    assert.equal(f.data.reads,1); assert.equal(queryCalls(f).length,1); assert.equal(f.data.saves.length,0);
    assert.equal(writeCalls(f).length,0);
    for (const p of requests) {
      assert.equal(p.value.preset,'aggressive'); assert.equal(p.value.motionEnabled,false);
      assert.equal(p.value.diagnosticLoggingEnabled,true); assert.deepEqual(plain(p.value.nodes),nodes);
    }
    assert.notEqual(requests[0].value,requests[1].value);
    assert.equal(f.api.fanFeatureEnabled.value,false,'remembered identity must not grant a handshake');
  } finally {await f.done();}
});
await check('after missing diagnostic reply, actual handshake gate and public on/off/on remain reachable',async()=>{
  const f=fixture();
  try {
    const p=f.observe(f.api.initializeFanFeature()); await f.advance(1000);
    assert.equal(p.status,'fulfilled','optional logging reply blocks manual preparation');
    assert.equal(f.api.fanFeatureEnabled.value,false);
    const life=await f.createLifecycle();
    await f.api.recordFanHandshake(true);
    assert.equal(f.api.fanFeatureEnabled.value,true);
    await life.apply(nodes); assert.equal(life.controlReady,true);
    await life.disable(); assert.equal(life.hasControlIntent,false);
    await life.apply(nodes); assert.equal(life.controlReady,true);
    assert.equal(f.host.calls.filter(c=>c==='enable').length,2); assert.equal(f.starts,1);
    assert.equal(f.data.saves.length,0); assert.equal(writeCalls(f).length,0);
    return {hostStarts:f.starts,enableCalls:2,hardwareCalls:0};
  } finally {await f.done();}
});
for (const authoritative of [true,false]) await check('normal readonly authority '+authoritative+' is adopted without writing a default or granting control',async()=>{
  const f=fixture(authoritative?'on':'off',!authoritative);
  try {
    const p=f.observe(f.api.initializeFanFeature()); await f.flush();
    assert.equal(p.status,'fulfilled',p.error??'normal read is blocked');
    assert.equal(p.value.diagnosticLoggingEnabled,authoritative);
    assert.equal(f.api.fanDiagnosticLoggingEnabled.value,authoritative);
    assert.equal(f.api.fanFeatureEnabled.value,false);
    assert.equal(f.data.saves.length,0); assert.equal(writeCalls(f).length,0); assert.equal(f.timers.size,0);
  } finally {await f.done();}
});
await check('older shell rejection preserves saved settings and resolves initialization',async()=>{
  const f=fixture('reject');
  try {
    const p=f.observe(f.api.initializeFanFeature()); await f.flush();
    assert.equal(p.status,'fulfilled'); assert.equal(p.value.diagnosticLoggingEnabled,true);
    assert.deepEqual(plain(p.value.nodes),nodes); assert.equal(f.data.saves.length,0);
    assert.equal(writeCalls(f).length,0);
  } finally {await f.done();}
});
await check('late readonly reply cannot undo the later explicit logging choice or revive its observer',async()=>{
  const f=fixture();
  try {
    const p=f.observe(f.api.initializeFanFeature()); await f.advance(1000);
    assert.equal(p.status,'fulfilled','initialization did not release its diagnostic observer');
    await f.api.setFanDiagnosticLoggingEnabled(false); await f.flush();
    f.lateReply(true); await f.flush();
    assert.equal(f.api.fanDiagnosticLoggingEnabled.value,false);
    assert.equal(f.api.getFanFeatureSettings().diagnosticLoggingEnabled,false);
    assert.equal(f.data.saves.length,1); assert.equal(writeCalls(f).length,1);
    assert.equal(f.timers.size,0);
  } finally {await f.done();}
});
for (const b of sourceBinding) assert.equal(digest(b.path),b.sha256,'source changed during verification: '+b.path);
const report={ok:rows.every(r=>r.ok),checks:rows.length,failed:rows.filter(r=>!r.ok).length,
  hardwareCalls:0,cpuBenchmark:false,scope:'actual Fan feature/diagnostics/IPC/public lifecycle; inert settings, topmon, native, Host and process; no actual HC, device, native process or sleep',sourceBinding,rows};
fs.writeFileSync(path.join(output,'result.json'),JSON.stringify(report,null,2));
console.log(JSON.stringify({ok:report.ok,checks:report.checks,failed:report.failed}));
process.exitCode=report.ok?0:1;