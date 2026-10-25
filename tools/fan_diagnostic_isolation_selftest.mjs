// Real Fan diagnostics + shared IPC + public lifecycle; virtual timers and inert
// webview/Host boundaries. No native executable, network, HC or hardware.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { webcrypto, createHash } from 'node:crypto';
import { build } from 'esbuild';

const project = path.resolve(import.meta.dirname, '..');
const out = path.resolve(process.argv[2] ?? '../../../_scratch/fan-diagnostic-isolation');
const diagnosticSource = process.argv[3] ? path.resolve(process.argv[3]) : path.join(project, 'src/bridge/fanDiagnostics.ts');
fs.mkdirSync(out, { recursive: true });
const sourceFiles = ['src/bridge/fanHost.ts', 'src/bridge/ipc.ts', 'tools/fixtures/fan_no_io_host.ts'].map(p => path.join(project, p)).concat(diagnosticSource);
const hash = p => createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const bindings = sourceFiles.map(p => ({ path: p, sha256: hash(p) }));
const bundle = await build({
  stdin: { contents: "export * from './src/bridge/fanDiagnostics';export {FanHostLifecycle} from './src/bridge/fanHost';export {NoIoHost} from './tools/fixtures/fan_no_io_host';export {setLogSink,rejectAllPending} from './src/bridge/ipc';", resolveDir: project, sourcefile: 'fan-diagnostic-isolation-entry.ts' },
  bundle: true, write: false, format: 'iife', globalName: 'FanTest', platform: 'browser', target: 'es2022', define: { 'import.meta.env': '{}' },
  plugins: [{ name: 'bind-diagnostic-before-image', setup(b) {
    b.onLoad({ filter: /[\\/]src[\\/]bridge[\\/]fanDiagnostics\.ts$/ }, () => ({ contents: fs.readFileSync(diagnosticSource, 'utf8'), loader: 'ts', resolveDir: path.join(project, 'src/bridge') }));
  } }],
});
const code = bundle.outputFiles[0].text;
function fixture({ native = true, replies = 'ack' } = {}) {
  let now = 0, seq = 0, listener;
  const timers = new Map(), calls = [], settled = [];
  const setTimer = (fn, ms, interval = false) => { const id = ++seq; const delay = Math.max(1, Number(ms) || 1); timers.set(id, { fn, due: now + delay, period: interval ? delay : 0 }); return id; };
  const clearTimer = id => timers.delete(id);
  const win = new EventTarget();
  const deliver = (id, error) => listener?.({ data: error ? { id, error } : { id, result: { ok: true } } });
  win.setTimeout = (fn, ms) => setTimer(fn, ms); win.clearTimeout = clearTimer;
  win.setInterval = (fn, ms) => setTimer(fn, ms, true); win.clearInterval = clearTimer;
  if (native) win.chrome = { webview: { addEventListener: (_type, callback) => { listener = callback; }, postMessage(message) {
    calls.push(message);
    assert.equal(message.cmd, 'fanLog.write', 'only inert diagnostic IPC is allowed');
    if (replies === 'throw') throw new Error('fixture post failure');
    if (replies === 'missing') return;
    queueMicrotask(() => deliver(message.id, replies === 'reject' ? 'fixture native rejection' : undefined));
  } } };
  const context = vm.createContext({ window: win, document: Object.assign(new EventTarget(), { hidden: false, visibilityState: 'visible', createElement: () => ({ style: {}, content: {} }), querySelector: () => null }), console, Event, EventTarget,
    CustomEvent: class extends Event { constructor(type, init) { super(type); this.detail = init?.detail; } },
    setTimeout: win.setTimeout, clearTimeout: clearTimer, setInterval: win.setInterval, clearInterval: clearTimer,
    TextEncoder, TextDecoder, URL, crypto: webcrypto, performance: { now: () => now }, structuredClone, queueMicrotask });
  vm.runInContext(`globalThis.__fanRegexUses=[];{
    const original=RegExp.prototype.test;
    RegExp.prototype.test=function(value){globalThis.__fanRegexUses.push({source:this.source,flags:this.flags,regex:this});return original.call(this,value);};
  }`,context);
  vm.runInContext(code, context, { timeout: 5000 });
  const api = context.FanTest;
  api.setLogSink(entry => settled.push(entry));
  async function flush() { for (let i = 0; i < 40; i++) await Promise.resolve(); await new Promise(setImmediate); }
  async function advance(ms) {
    const until = now + ms; await flush();
    for (let i = 0; i < 1000; i++) {
      const next = [...timers.entries()].filter(([, t]) => t.due <= until).sort((a, b) => a[1].due - b[1].due)[0];
      if (!next) break;
      const [id, t] = next; now = t.due; if (t.period) t.due += t.period; else timers.delete(id);
      t.fn(); await flush();
    }
    now = until; await flush();
  }
  return { api, calls, settled, timers, advance, flush, deliver, regexUses: () => context.__fanRegexUses, async done() { api.rejectAllPending('fixture cleanup'); await flush(); timers.clear(); } };
}
const results = []; const grammarObservations = [];
async function check(name, body) { try { const evidence = await body(); results.push({ name, ok: true, ...(evidence ? { evidence } : {}) }); console.log('PASS ' + name); } catch (e) { results.push({ name, ok: false, error: String(e) }); console.log('FAIL ' + name + ': ' + e); } }
await check('disabled details stay lazy; responsibility evidence remains enabled and sanitized', async () => {
  const f = fixture(); try {
    let factoryCalls = 0;
    f.api.fanDiagnosticLog('ignored', { secret: 'not-emitted' });
    f.api.fanDiagnosticLogLazy('ignored', () => { factoryCalls++; throw Error('must stay lazy'); });
    assert.equal(factoryCalls, 0); assert.equal(f.calls.length, 0);
    f.api.fanLifecycleEvidence('lifecycle.resume-wait-started', { leaseId: 'fixture-private', password: 'never-export', generation: 3 });
    await f.flush(); assert.equal(f.calls.length, 1); const d = f.calls[0].args.details;
    assert.equal(d.caller, 'fanHost-lifecycle'); assert.equal(d.leaseId, '[redacted]'); assert.equal(d.password, '[redacted]'); assert.equal(d.generation, 3); assert.equal(f.timers.size, 0);
  } finally { await f.done(); }
});
await check('preview mode constructs no lazy details and sends no evidence IPC', async () => {
  const f = fixture({ native: false }); try { f.api.adoptFanDiagnosticLoggingState(true); f.api.fanDiagnosticLogLazy('preview', () => { throw Error('preview factory executed'); }); f.api.fanLifecycleEvidence('lifecycle.resume-wait-started', {}); assert.equal(f.calls.length, 0); assert.equal(f.timers.size, 0); } finally { await f.done(); }
});
await check('enabled lazy details use current state once per emitted event and preserve redaction', async () => {
  const f = fixture(); try {
    f.api.adoptFanDiagnosticLoggingState(true); let state = 'ready', builds = 0;
    const factory = () => { builds++; return { state, sessionToken: 'fixture-private', tokenPresent: true }; };
    f.api.fanDiagnosticLogLazy('lifecycle.heartbeat-success', factory); state = 'suspended'; f.api.fanDiagnosticLogLazy('lifecycle.heartbeat-success', factory);
    await f.flush(); assert.equal(builds, 2); assert.equal(f.calls[0].args.details.state, 'ready'); assert.equal(f.calls[1].args.details.state, 'suspended'); assert.equal(f.calls[0].args.details.sessionToken, '[redacted]'); assert.equal(f.calls[0].args.details.tokenPresent, true); assert.equal(f.settled.length, 2); assert.equal(f.timers.size, 0);
  } finally { await f.done(); }
});
for (const entry of ['ordinary', 'lazy', 'responsibility']) await check(entry + ' missing native acknowledgement retires within five seconds without replay', async () => {
  const f = fixture({ replies: 'missing' }); try {
    f.api.adoptFanDiagnosticLoggingState(true);
    for (let wave = 0; wave < 3; wave++) {
      for (let i = 0; i < 24; i++) {
        const event = entry === 'responsibility' ? 'lifecycle.resume-wait-started' : 'lifecycle.heartbeat-success';
        if (entry === 'lazy') f.api.fanDiagnosticLogLazy(event, () => ({ state: 'ready' }));
        else if (entry === 'responsibility') f.api.fanLifecycleEvidence(event, { generation: 3 });
        else f.api.fanDiagnosticLog(event, { state: 'ready' });
      }
      await f.advance(4999); assert.equal(f.settled.length, wave * 24, 'must retain the declared wait budget');
      await f.advance(1); assert.equal(f.settled.length, (wave + 1) * 24, 'lost replies must not retain IPC waiters indefinitely'); assert.equal(f.timers.size, 0);
    }
    const count = f.settled.length; for (const request of f.calls) f.deliver(request.id); await f.flush();
    assert.equal(f.settled.length, count, 'late diagnostic replies cannot settle twice'); assert.equal(f.calls.length, 72, 'diagnostics are never retransmitted');
    assert(f.settled.every(row => row.ok === false && /IPC command timed out: fanLog.write/.test(row.error)));
    return { sent: f.calls.length, retired: count, replayed: 0 };
  } finally { await f.done(); }
});
for (const mode of ['reject', 'throw']) await check('diagnostic ' + mode + ' cannot throw or leak an IPC timer', async () => {
  const f = fixture({ replies: mode }); try { f.api.adoptFanDiagnosticLoggingState(true); f.api.fanDiagnosticLog('test', { state: 'ready' }); f.api.fanDiagnosticLogLazy('test', () => ({ state: 'ready' })); f.api.fanLifecycleEvidence('lifecycle.resume-wait-started', { generation: 3 }); await f.flush(); assert.equal(f.settled.length, 3); assert(f.settled.every(r => !r.ok)); assert.equal(f.timers.size, 0); } finally { await f.done(); }
});
for (const entry of ['ordinary', 'lazy', 'responsibility']) await check(entry + ' synchronous detail failure remains diagnostic-only', async () => {
  const f = fixture(); try {
    f.api.adoptFanDiagnosticLoggingState(true); const detail = {}; Object.defineProperty(detail, 'broken', { enumerable: true, get() { throw Error('fixture getter failure'); } });
    if (entry === 'lazy') f.api.fanDiagnosticLogLazy('test', () => { throw Error('fixture factory failure'); });
    else if (entry === 'responsibility') f.api.fanLifecycleEvidence('lifecycle.resume-wait-started', detail);
    else f.api.fanDiagnosticLog('test', detail);
    assert.equal(f.calls.length, 0, 'invalid diagnostic payload is not emitted'); assert.equal(f.timers.size, 0);
    f.api.fanDiagnosticLog('next-valid-event', { state: 'ready' }); await f.flush(); assert.equal(f.calls.length, 1, 'failure cannot poison later valid logging');
  } finally { await f.done(); }
});
await check('a failed heartbeat log cannot reject a renewed lease or block next manual control', async () => {
  const f = fixture({ replies: 'missing' }); let life;
  try {
    const host = new f.api.NoIoHost(); host.factoryType = 'HandheldCompanion.Devices.MSI.ClawCG3EM'; host.generation = 3; host.attempt = 1; host.powerState = 'On'; host.completeResume = true;
    let starts = 0, stops = 0;
    life = new f.api.FanHostLifecycle({ enabled: true, adapter: host, heartbeatIntervalMs: 5000,
      readNativeActivity: async () => null, readNativePowerState: async () => ({ generation: 3, phase: 'ready', resumeReady: true, inputReady: true, hardwareWritesAllowed: true, hibernateAvailable: true }),
      launcher: { start: async () => { starts++; return { pid: 111401, executable: 'inert.exe' }; }, stop: async () => { stops++; } }, onControlDisplay() {} });
    life.setPowerGeneration(3); life.observePowerBoundary('resume-ready', 3);
    const curve = [{ tempC: 0, dutyPercent: 20 }, { tempC: 40, dutyPercent: 35 }, { tempC: 70, dutyPercent: 65 }, { tempC: 100, dutyPercent: 100 }];
    f.api.adoptFanDiagnosticLoggingState(true); await life.start(); await life.apply(curve);
    assert.equal(host.calls.filter(v => v === 'enable').length, 1);
    const lease = life.currentLease; life.heartbeatSuccessDetails = () => { throw Error('fixture lazy diagnostic failure after renewal'); };
    await life.heartbeat(); assert.equal(life.currentLease.leaseId, lease.leaseId, 'renewal survives diagnostic failure');
    await life.disable(); await life.apply(curve); assert.equal(host.calls.filter(v => v === 'enable').length, 2); assert.equal(starts, 1); assert.equal(stops, 0); assert.equal(life.state, 'ready');
    return { handshake: host.calls.filter(v => v === 'handshake').length, enables: 2, starts, hardwareCalls: 0 };
  } finally { if (life) await life.close(); await f.done(); }
});
// Reuse grammar only: repeated event values still undergo the same redaction.
function emit(f, entry, details) {
  if (entry === 'ordinary') f.api.fanDiagnosticLog('lifecycle.heartbeat-success', details);
  else if (entry === 'lazy') f.api.fanDiagnosticLogLazy('lifecycle.heartbeat-success', () => details);
  else f.api.fanLifecycleEvidence('lifecycle.resume-wait-started', details);
}
for (const entry of ['ordinary','lazy','responsibility']) await check(entry+' current nested values remain checked across credential changes', async () => {
  const f=fixture();try{
    f.api.adoptFanDiagnosticLoggingState(true);
    for(const valid of [true,false,true]){
      const details={tokenPresent:valid,tokenFingerprint:valid?'1234abcd':'not-a-fingerprint',credentialVersionId:valid?'absent':'not-a-version',
        sessionTokenPath:valid?'C:/inert/session-token.txt':'C:/inert/'+'f'.repeat(32)+'.txt',
        sessionToken:'fixture-never-emit',leaseId:'fixture-never-emit',Password:'fixture-never-emit',
        nested:[{authorization:'fixture-never-emit',plain:'kept',count:7},null,true]};
      emit(f,entry,details);await f.flush();const d=f.calls.at(-1).args.details;
      assert.equal(d.tokenPresent,valid);assert.equal(d.tokenFingerprint,valid?'1234abcd':'[redacted]');assert.equal(d.credentialVersionId,valid?'absent':'[redacted]');
      assert.equal(d.sessionTokenPath,valid?'C:/inert/session-token.txt':'[redacted]');
      for(const key of ['sessionToken','leaseId','Password'])assert.equal(d[key],'[redacted]');assert.equal(d.nested[0].authorization,'[redacted]');assert.equal(d.nested[0].plain,'kept');
      assert(!JSON.stringify(d).includes('fixture-never-emit'));assert.equal(details.sessionToken,'fixture-never-emit','sanitization mutated its input');
    }
    assert.equal(f.calls.length,3);assert.equal(f.timers.size,0);
  }finally{await f.done();}
});
for (const entry of ['ordinary','lazy','responsibility']) await check(entry+' inline replacement still redacts every occurrence in repeated strings', async () => {
  const f=fixture();try{
    f.api.adoptFanDiagnosticLoggingState(true);
    for(let i=0;i<3;i++){
      emit(f,entry,{message:'X-YeMan-Fan-Session=fixture-one, sessionToken:fixture-two leaseId=fixture-three'});await f.flush();
      assert.equal(f.calls.at(-1).args.details.message,'X-YeMan-Fan-Session=[redacted], sessionToken=[redacted] leaseId=[redacted]');
    }
    assert.equal(f.calls.length,3);
  }finally{await f.done();}
});
for (const entry of ['ordinary','lazy','responsibility']) await check(entry+' nonsecret whitelist keeps exact string rules and existing nonstring fallback', async () => {
  const f=fixture();try{
    f.api.adoptFanDiagnosticLoggingState(true);
    for(const value of [true,false,null,0,{},[]]){
      emit(f,entry,{tokenPresent:value,tokenFingerprint:value,credentialVersionId:value,sessionTokenPath:value});await f.flush();const d=f.calls.at(-1).args.details;
      assert.equal(d.tokenPresent,typeof value==='boolean'?value:'[redacted]');for(const key of ['tokenFingerprint','sessionTokenPath'])assert.equal(d[key],'[redacted]');assert.equal(JSON.stringify(d.credentialVersionId),JSON.stringify(value),'preserve the current nonstring fallback; this is not a broader redaction-policy change');
    }
    for(const [key,value,expected]of [['tokenFingerprint','',''],['tokenFingerprint','abcdef12','abcdef12'],['tokenFingerprint','ABCDEF12','[redacted]'],['credentialVersionId','1234abcd','1234abcd'],['credentialVersionId','absent','absent'],['credentialVersionId','','[redacted]'],['sessionTokenPath','C:/fixture-safe-path','C:/fixture-safe-path'],['sessionTokenPath','A'.repeat(32),'[redacted]']]){
      emit(f,entry,{[key]:value});await f.flush();assert.equal(f.calls.at(-1).args.details[key],expected);
    }
  }finally{await f.done();}
});
await check('four fixed non-global grammars reuse their matcher identities without caching values', async () => {
  const f=fixture();try{
    f.api.adoptFanDiagnosticLoggingState(true);
    for(const entry of ['ordinary','lazy','responsibility'])for(let i=0;i<3;i++)emit(f,entry,{state:'ready',tokenFingerprint:'1234abcd',credentialVersionId:'absent',sessionTokenPath:'C:/safe.txt',nested:{value:i}});
    await f.flush();const identities=[];
    for(const [source,flags]of [['^([0-9a-f]{8})?$',''],['^([0-9a-f]{8}|absent)$',''],['[0-9a-f]{32}','i'],['(token|lease.?id|authorization|session|password|secret)','i']]){
      const uses=Array.from(f.regexUses()).filter(v=>v.source===source&&v.flags===flags);assert(uses.length>=9,'the real public log path never exercised '+source);
      const count=new Set(uses.map(v=>v.regex)).size;identities.push({source,uses:uses.length,identities:count});
    }
    grammarObservations.push(...identities);for(const item of identities)assert.equal(item.identities,1,'fixed grammar was reconstructed for each field/event: '+item.source);
    return{scope:'RegExp object identity only; no CPU, time, memory or allocation benchmark',identities};
  }finally{await f.done();}
});
await check('disabled ordinary and lazy events do no grammar work while responsibility stays checked', async () => {
  const f=fixture();try{
    const before=f.regexUses().length;let calls=0;
    f.api.fanDiagnosticLog('ignored',{secret:'fixture-never-emit'});f.api.fanDiagnosticLogLazy('ignored',()=>{calls++;return{secret:'fixture-never-emit'};});await f.flush();
    assert.equal(f.regexUses().length,before);assert.equal(calls,0);assert.equal(f.calls.length,0);
    f.api.fanLifecycleEvidence('lifecycle.resume-wait-started',{secret:'fixture-never-emit'});await f.flush();assert(f.regexUses().length>before);assert.equal(f.calls[0].args.details.secret,'[redacted]');
  }finally{await f.done();}
});
for (const binding of bindings) assert.equal(hash(binding.path), binding.sha256, 'source changed during checks: ' + binding.path);
const report = { ok: results.every(r => r.ok), checks: results.length, failed: results.filter(r => !r.ok).length, scope: 'actual Fan diagnostic entry points/shared IPC/public lifecycle; virtual deadlines; inert webview and Host adapters; no hardware or CPU benchmark', sourceBinding: bindings, grammarObservations, results };
fs.writeFileSync(path.join(out, 'result.json'), JSON.stringify(report, null, 2)); console.log(JSON.stringify({ ok: report.ok, checks: report.checks, failed: report.failed, scope: report.scope })); process.exitCode = report.ok ? 0 : 1;
