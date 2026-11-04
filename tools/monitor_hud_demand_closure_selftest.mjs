// Real unified demand hub + real shared sample source; virtual IPC/native flags.
// No device reads, new timers or CPU percentage benchmark.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import crypto from 'node:crypto';import {createRequire} from 'node:module';import {pathToFileURL} from 'node:url';
if (process.argv.includes('--push-relay-only')) {
  await runPushRelayOnly();
} else {
const root=process.cwd(),task=path.resolve(process.env.YMCC_HUD_DEMAND_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/hud-demand-closure-20261016');fs.mkdirSync(task,{recursive:true});const {build}=createRequire(root+'/package.json')('esbuild');
const mocks={'./ipc':'export const isNativeRuntime=false;export const invoke=()=>{throw Error("no IO")};export const on=()=>()=>{};','./api':'export const fs={};export const windowApi={};','./monitorAppliedTdp':'export const advanceMonitorTdpDisplay=()=>{};export const subscribeMonitorTdpState=()=>()=>{};'};
const modules={};for(const side of ['before','after']){const b=await build({stdin:{resolveDir:root,loader:'ts',contents:['monitorData','monitorHudModel','monitorHudSampleSource'].map(n=>'export * from '+JSON.stringify('./src/bridge/'+n+'.ts')+';').join('\n')},bundle:true,platform:'node',format:'esm',write:false,plugins:[{name:'inert',setup(b){b.onResolve({filter:/.*/},a=>mocks[a.path]?{path:a.path,namespace:'mock'}:undefined);b.onLoad({filter:/.*/,namespace:'mock'},a=>({contents:mocks[a.path],loader:'ts'}));if(side==='before')b.onLoad({filter:/monitorHud(?:Model|SampleSource)\.ts$/},a=>({contents:fs.readFileSync(task+'/before/src/bridge/'+path.basename(a.path),'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));}}]});const file=task+'/'+side+'.mjs';fs.writeFileSync(file,b.outputFiles[0].text);modules[side]=await import(pathToFileURL(file));}
const now=1700000000000,checks=[],drain=async()=>{for(let i=0;i<60;i++)await Promise.resolve();},noIO=()=>{throw Error('unexpected file, timer or hardware access');};function check(name,fn){fn();checks.push(name);}
const empty=m=>Object.fromEntries(m.HUD_METRICS.map(v=>[v.id,false]));const fields=(m,...ids)=>({...empty(m),...Object.fromEntries(ids.map(id=>[id,true]))});
const after=modules.after;
const fpsIds=['fps','fps1','frametime','frametimeGraph','fpsLimit'];
for(const metric of after.HUD_METRICS)check(metric.id+' has the correct native channel dependency',()=>{const f=fields(after,metric.id),expected={top:metric.ready&&(!fpsIds.includes(metric.id)||metric.id==='fpsLimit')&&!['clock','tdpLimit'].includes(metric.id),fps:metric.ready&&fpsIds.includes(metric.id),fileSystem:metric.ready&&['diskRead','diskWrite'].includes(metric.id)};assert.deepEqual(after.hudNativeDemand(f),expected);});
check('unknown preferences preserve legacy demand while empty selection needs no native sampling',()=>{assert.deepEqual(after.hudNativeDemand(undefined),{top:true,fps:true});assert.deepEqual(after.hudNativeDemand(empty(after)),{top:false,fps:false,fileSystem:false});});
function setup(m){let seq=0,acquires=0,releases=0,reads=0,subscriptions=0,tdpSubscriptions=0,snapshotCb=null,timerId=0;const timers=new Map(),demandStates=[];const calls=[],actual={top:false,fps:false},states=[];
 const hub=m.createMonitorDataHub({now:()=>now,setTimer(fn,ms){const id=++timerId;timers.set(id,{fn,ms});return id;},clearTimer(id){timers.delete(id);},readTextFile:noIO,invoke:async(cmd,args)=>{calls.push({cmd,args});if(cmd==='monitor.start'||cmd==='monitor.stop'){if(args.top)actual.top=cmd==='monitor.start';if(args.fps)actual.fps=cmd==='monitor.start';states.push({...actual});return {ok:true};}if(cmd==='monitor.snapshot')return {schema:1,available:false,session:1,generation:1,relayEpoch:0,seq:++seq,topActive:actual.top,fpsActive:actual.fps};if(cmd==='monitor.takeover')return {ok:true,cleaned:true};if(cmd==='monitor.release')return {ok:true};throw Error(cmd);}});
 const source=m.createHudSampleSource({now:()=>now,subscribeSnapshot(fn){subscriptions++;snapshotCb=fn;fn(null);return()=>{subscriptions--;snapshotCb=null;};},subscribeTdp(fn){tdpSubscriptions++;fn({mode:'ac',status:'applied',watts:30,proof:'write-ack',revision:1,confirmedAt:now});return()=>{tdpSubscriptions--;};},readOnce(){reads++;return hub.read();},acquire(need){acquires++;const close=hub.acquire('product-HUD',need??{top:true,fps:true});demandStates.push(hub.desired());return()=>{releases++;close();demandStates.push(hub.desired());};}});
 return {hub,source,calls,states,demandStates,timers,actual,select(f){source.setDemand?.(m.hudNativeDemand(f));},stats:()=>({acquires,releases,reads,subscriptions,tdpSubscriptions}),publish(v){snapshotCb?.(v);}};
}
const work={};for(const [side,m]of Object.entries(modules)){const s=setup(m);s.select(fields(m,'cpuTemp'));const off1=s.source.acquire(),off2=s.source.acquire();await drain();work[side]={cpuOnly:{desired:s.hub.desired(),actual:{...s.actual},...s.stats()},calls:[]};assert.equal(s.source.users(),2);assert.equal(s.stats().acquires,1);off1();await drain();assert.equal(s.source.users(),1);off2();await drain();assert.equal(s.stats().subscriptions,0);assert.equal(s.stats().tdpSubscriptions,0);assert.deepEqual(s.hub.desired(),{top:false,fps:false});work[side].calls=s.calls;}
check('a CPU-only HUD stops requesting FPS without adding a reader or lease',()=>{assert.deepEqual(work.before.cpuOnly.desired,{top:true,fps:true});assert.deepEqual(work.after.cpuOnly.desired,{top:true,fps:false});assert.equal(work.before.cpuOnly.reads,work.after.cpuOnly.reads);assert.equal(work.after.cpuOnly.acquires,1);});
{const s=setup(after);s.select(fields(after,'cpuTemp'));const off=s.source.acquire();await drain();const before=s.stats(),calls=s.calls.length;for(let i=0;i<200;i++){s.select(fields(after,'cpuTemp'));s.source.sample();}await drain();check('200 unchanged settings/samples add no acquisition, snapshot read or native command',()=>{assert.deepEqual(s.stats(),before);assert.equal(s.calls.length,calls);});off();await drain();}
{const s=setup(after);s.select(fields(after,'cpuTemp'));const off=s.source.acquire();await drain();const marker=s.demandStates.length,nativeMarker=s.states.length;s.select(fields(after,'fps'));await drain();check('top-to-FPS replacement preserves logical ownership and converges through the existing stop-first native protocol',()=>{assert.deepEqual(s.hub.desired(),{top:false,fps:true});assert.deepEqual(s.actual,{top:false,fps:true});assert(!s.demandStates.slice(marker).some(v=>!v.top&&!v.fps));assert.deepEqual(s.states.slice(nativeMarker),[{top:false,fps:false},{top:false,fps:true}]);assert.equal(s.timers.size,0);assert.equal(s.stats().reads,1);assert.equal(s.stats().subscriptions,1);assert.equal(s.source.users(),1);});s.select(fields(after,'fpsLimit'));await drain();check('frame limit retains both value and FPS-family visibility dependencies',()=>{assert.deepEqual(s.hub.desired(),{top:true,fps:true});});off();await drain();check('final disposer closes the replacement token and all subscriptions',()=>{assert.deepEqual(s.hub.desired(),{top:false,fps:false});assert.equal(s.stats().acquires,s.stats().releases);assert.equal(s.stats().tdpSubscriptions,0);});}
{const s=setup(after),auto=s.hub.acquire('autofloat',{fps:true}),bar=s.hub.acquire('topbar',{top:true});s.select(fields(after,'cpuTemp'));const off=s.source.acquire();await drain();s.select(fields(after,'clock','tdpLimit'));await drain();check('software-only HUD cannot release autofloat or topbar collection',()=>{assert.deepEqual(s.hub.desired(),{top:true,fps:true});assert.deepEqual(s.actual,{top:true,fps:true});});bar();await drain();check('releasing topbar leaves the independent autofloat FPS channel alive',()=>{assert.deepEqual(s.hub.desired(),{top:false,fps:true});});off();await drain();check('closing the HUD leaves autofloat ownership intact',()=>{assert.deepEqual(s.hub.desired(),{top:false,fps:true});});auto();await drain();assert.deepEqual(s.hub.desired(),{top:false,fps:false});}
{const s=setup(after);s.select(fields(after,'clock','tdpLimit'));const off=s.source.acquire();await drain();const d=s.source.sample();check('TDP/clock/image-only output retains independent values with no start command',()=>{assert.deepEqual(s.hub.desired(),{top:false,fps:false});assert(!s.calls.some(c=>c.cmd==='monitor.start'));assert.equal(d.values.tdpLimit,30);assert.equal(typeof d.values.clock,'string');});off();await drain();}
{const s=setup(after);s.select(fields(after,'cpuTemp'));const off=s.source.acquire();await drain();s.publish(null);for(let i=0;i<200;i++)s.source.sample();await drain();check('missing/hidden data never removes its own selected collection demand',()=>{assert.deepEqual(s.hub.desired(),{top:true,fps:false});assert.equal(s.stats().acquires,1);});off();await drain();}
{let fail=false,acquired=0,released=0;const s=after.createHudSampleSource({now:()=>now,subscribeSnapshot:()=>()=>{},subscribeTdp:()=>()=>{},readOnce:async()=>null,acquire(){if(fail)throw Error('lease rejected');acquired++;return()=>{released++;};}});s.setDemand({top:true,fps:false});const off=s.acquire();await drain();fail=true;check('failed replacement keeps its existing owned lease',()=>{assert.throws(()=>s.setDemand({top:false,fps:true}));assert.equal(released,0);assert.equal(s.users(),1);});fail=false;s.setDemand({top:false,fps:true});check('replacement can recover without leaking the old lease',()=>{assert.equal(acquired,2);assert.equal(released,1);});off();assert.equal(acquired,released);}
const host=fs.readFileSync('src/bridge/monitorHudHost.ts','utf8');check('production host wires initial show, preference change, apply and ready into field demand',()=>{assert(host.includes("acquireMonitorDemand('ymcc-product-hud',need)"));assert(host.includes('if(show)source.setDemand(hudNativeDemand(prefs.ymcc.fields))'));assert(host.includes("if(prefs.output==='ymcc')source.setDemand(hudNativeDemand(prefs.ymcc.fields))"));assert(host.includes('source.setDemand(hudNativeDemand(activePreferences.ymcc.fields))'));assert(host.includes('source.setDemand(hudNativeDemand(activePreferences?.ymcc.fields))'));});
const result={ok:true,checks:checks.length,cases:checks,work,hardwareReads:false,newTimers:false,registryWrites:false,upstreamProcessStopped:false,sourceSha256:Object.fromEntries(['src/bridge/monitorHudModel.ts','src/bridge/monitorHudSampleSource.ts','src/bridge/monitorHudHost.ts','src/bridge/monitorData.ts'].map(f=>[f,crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')])),scope:'Actual frozen/current model/sample source and current unified hub with inert IPC; individual field dependencies, lease replacement, other consumers and no-work invariants. Host routing assertions are static, not full application acceptance.'};fs.writeFileSync(task+'/DEMAND-RESULT.json',JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({ok:true,checks:checks.length,before:work.before.cpuOnly,after:work.after.cpuOnly},null,2));

}

// Explicit, bounded production-singleton/public-API consumer. The legacy default
// above is retained byte-for-byte; this branch does not run its frozen comparison.
async function runPushRelayOnly() {
  const root = path.resolve(import.meta.dirname, '..');
  const evidenceRoot = path.resolve(root, '../../../Testing/YMCC/Evidence');
  const canonicalSource = path.join(root, 'src/bridge/monitorData.ts');
  const args = process.argv.slice(2);
  let sourcePath = canonicalSource, evidenceDir = process.env.YMCC_HUD_DEMAND_OUT;
  for (let i = 0; i < args.length; ++i) {
    if (args[i] === '--push-relay-only') continue;
    if (!['--source', '--evidence-dir'].includes(args[i]) || !args[i + 1] || args[i + 1].startsWith('--'))
      throw Error('Usage: --push-relay-only [--source <exact TS>] [--evidence-dir <Testing/YMCC/Evidence/...>]');
    const option = args[i++], value = args[i];
    if (option === '--source') sourcePath = path.resolve(value);
    else evidenceDir = value;
  }
  evidenceDir = path.resolve(evidenceDir || path.join(evidenceRoot,
    'BackgroundIdleOptimization-20261101/MainlineImport/MonitorTakeoverFromPush/PushRelay'));
  const contained = (base, value) => {
    const relative = path.relative(base, value);
    return relative !== '' && !relative.startsWith('..' + path.sep) && relative !== '..' && !path.isAbsolute(relative);
  };
  assert(contained(evidenceRoot, evidenceDir), 'Evidence output must be a child of Testing/YMCC/Evidence');
  let existing = evidenceDir;
  while (!fs.existsSync(existing)) existing = path.dirname(existing);
  const realRoot = fs.realpathSync(evidenceRoot), realExisting = fs.realpathSync(existing);
  assert(realExisting === realRoot || contained(realRoot, realExisting), 'Evidence output ancestor escapes through a link');
  fs.mkdirSync(evidenceDir, { recursive: true });
  assert(contained(realRoot, fs.realpathSync(evidenceDir)), 'Resolved output must remain under Evidence');
  const resultPath = path.join(evidenceDir, 'PUSH-RELAY-RESULT.json');
  const bundlePath = path.join(evidenceDir, 'monitorData.push-relay.bundle.mjs');
  assert(!fs.existsSync(resultPath) && !fs.existsSync(bundlePath), 'Preserve existing evidence: choose an unused evidence directory');
  const hash = value => crypto.createHash('sha256').update(value).digest('hex');
  const sourceBytes = fs.readFileSync(sourcePath), toolBytes = fs.readFileSync(new URL(import.meta.url));
  const dependencies = new Map(), cases = [], realNow = Date.now;
  const startedAt = realNow(), { build } = createRequire(path.join(root, 'package.json'))('esbuild');
  const mockSlot = '__ymccHudPushRelayOnly';
  const previousSlot = Object.getOwnPropertyDescriptor(globalThis, mockSlot);
  const originalTimers = { setTimeout: globalThis.setTimeout, clearTimeout: globalThis.clearTimeout,
    setInterval: globalThis.setInterval, clearInterval: globalThis.clearInterval };
  let activeFixture = null, caseSerial = 0, prepareError = null, bundleHash = null;
  const drain = async () => { for (let i = 0; i < 100; ++i) await Promise.resolve(); };
  // Observe/delegate real timers; never synthesize timer callbacks or virtualize
  // performance.now. Only Date.now and inert native IPC state are virtual.
  for (const name of ['setTimeout', 'setInterval']) {
    globalThis[name] = (fn, ms, ...rest) => {
      const owner = activeFixture;
      const handle = originalTimers[name](fn, ms, ...rest);
      if (owner) { owner.timerEvents.push({ kind: name, ms, at: owner.now }); owner.liveTimers.set(handle, name); }
      return handle;
    };
  }
  for (const name of ['clearTimeout', 'clearInterval']) {
    globalThis[name] = handle => {
      activeFixture?.liveTimers.delete(handle);
      return originalTimers[name](handle);
    };
  }
  Date.now = () => activeFixture ? activeFixture.now : realNow();
  const mocks = {
    './ipc': `const h=globalThis.${mockSlot};export const isNativeRuntime=false;export const invoke=(...a)=>h.invoke(...a);export const on=()=>()=>{};`,
    './api': `const h=globalThis.${mockSlot};export const fs={readTextFile:async()=>{h.fileReads++;throw Error('Unexpected compatibility-file read')}};export const windowApi={};`,
    './monitorAppliedTdp': 'export const advanceMonitorTdpDisplay=()=>{};export const subscribeMonitorTdpState=()=>()=>{};',
  };
  function fixture(options = {}) {
    let sequence = 0;
    const f = {
      now: 1700000000000, session: 1, generation: 1, relayEpoch: 0, relayOff: false,
      calls: [], timerEvents: [], liveTimers: new Map(), unexpected: [], fileReads: 0,
      disposers: [], deferred: [], actual: { top: false, fps: false, fileSystem: false },
      packet(overrides = {}) {
        return { schema: 1, available: true, session: f.session, generation: f.generation,
          relayEpoch: f.relayEpoch, seq: ++sequence, collectedAt: f.now, heartbeatAt: f.now, hwinfoOkAt: f.now,
          topActive: f.actual.top, fpsActive: f.actual.fps, fileSystemActive: f.actual.fileSystem, hwDown: false,
          top: JSON.stringify({ ts: f.now, hwDown: false, cpuUsage: 20, cpuUsageValid: true,
            cpuUsageTs: f.now, coreUsage: [10, 20], coreUsageTs: f.now, hasBattery: false, gpuSelectionPresent: false }),
          fps: null, ...overrides };
      },
      switchSession(session) {
        f.session = session; sequence = 0; f.generation = 1; f.relayEpoch = 0; f.relayOff = false;
        f.actual = { top: false, fps: false, fileSystem: false };
      },
      advance(ms = 1000) { f.now += ms; },
      takeovers() { return f.calls.filter(call => call.cmd === 'monitor.takeover'); },
      reads() { return f.calls.filter(call => call.cmd === 'monitor.snapshot').length; },
      hold() {
        let resolve;
        const promise = new Promise(done => { resolve = done; });
        const held = { promise, resolve }; f.deferred.push(held); return held;
      },
      async invoke(cmd, args = {}, ipcOptions) {
        const call = { cmd, args: structuredClone(args), at: f.now, session: f.session, options: ipcOptions };
        f.calls.push(call);
        if (cmd === 'monitor.start' || cmd === 'monitor.stop') {
          for (const key of ['top', 'fps']) if (args[key]) f.actual[key] = cmd === 'monitor.start';
          if (args.top) f.actual.fileSystem = cmd === 'monitor.start' && args.fileSystem !== false;
          return { ok: true };
        }
        if (cmd === 'monitor.snapshot') return f.packet({ available: optionsForSnapshot() });
        if (cmd === 'monitor.takeover') {
          const receipt = options.takeover ? await options.takeover(f, call) : { ok: true, cleaned: true };
          if (receipt?.ok === true && receipt.cleaned === true && call.session === f.session) f.relayOff = true;
          return receipt;
        }
        if (cmd === 'monitor.release') return { ok: true };
        f.unexpected.push(cmd); throw Error('Unexpected native command: ' + cmd);
      },
    };
    // Do not shadow the fixture options with invoke's IPC options argument.
    const optionsForSnapshot = () => options.initialAvailable === true;
    const invokeNative = f.invoke;
    f.invoke = (cmd, args, ipcOptions) => invokeNative.call(f, cmd, args, ipcOptions);
    return f;
  }
  async function runCase(name, options, test) {
    const f = fixture(options), start = realNow(); activeFixture = f;
    globalThis[mockSlot] = f;
    const record = { name, ok: false, executedThisRun: true };
    try {
      const m = await import(pathToFileURL(bundlePath).href + '?case=' + (++caseSerial));
      const own = (need = { top: true, fps: false, fileSystem: false }) => {
        const release = m.acquireMonitorDemand('push-relay-case', need); f.disposers.push(release); return release;
      };
      const push = async (overrides = {}, advance = 1000) => {
        f.advance(advance); m.acceptPublishedMonitorSnapshot(f.packet(overrides)); await drain();
      };
      await test({ m, f, own, push });
      assert.equal(f.fileReads, 0, 'No compatibility-file reads');
      assert.deepEqual(f.unexpected, [], 'No unexpected IPC'); record.ok = true;
    } catch (error) { record.error = { name: error.name, message: error.message, stack: error.stack }; }
    finally {
      for (const off of f.disposers.reverse()) { try { off(); } catch (error) { record.ok = false; record.cleanupError = String(error); } }
      for (const held of f.deferred) held.resolve({ ok: false, cleaned: false });
      await drain();
      record.calls = f.calls; record.snapshotReads = f.reads(); record.takeoverCalls = f.takeovers().length;
      record.fileReads = f.fileReads; record.unexpected = f.unexpected;
      record.timerEvents = f.timerEvents; record.durationMs = realNow() - start;
      // Resource cleanup only, never fire pending real callbacks to make a case pass.
      record.outstandingTimersAtCleanup = f.liveTimers.size;
      for (const [handle, kind] of f.liveTimers) originalTimers[kind === 'setInterval' ? 'clearInterval' : 'clearTimeout'](handle);
      f.liveTimers.clear(); cases.push(record); activeFixture = null;
    }
  }
  try {
    const built = await build({ stdin: { resolveDir: root, loader: 'ts', contents: 'export * from "./src/bridge/monitorData.ts";' },
      bundle: true, platform: 'node', format: 'esm', write: false, metafile: true,
      plugins: [{ name: 'push-relay-production-boundary', setup(b) {
        b.onResolve({ filter: /.*/ }, a => mocks[a.path] ? { path: a.path, namespace: 'mock' } : undefined);
        b.onLoad({ filter: /.*/, namespace: 'mock' }, a => ({ contents: mocks[a.path], loader: 'ts' }));
        b.onLoad({ filter: /\.tsx?$/, namespace: 'file' }, a => {
          const exact = path.resolve(a.path).toLowerCase() === canonicalSource.toLowerCase();
          const bytes = exact ? sourceBytes : fs.readFileSync(a.path);
          dependencies.set(a.path, { path: a.path, bytes: bytes.length, sha256: hash(bytes), override: exact,
            ...(exact ? { actualInputPath: sourcePath } : {}) });
          return { contents: bytes.toString('utf8'), loader: 'ts', resolveDir: path.dirname(a.path) };
        });
      } }] });
    const bundle = built.outputFiles[0].text; bundleHash = hash(bundle);
    fs.writeFileSync(bundlePath, bundle, { flag: 'wx' });
    assert(dependencies.get(canonicalSource)?.override || [...dependencies.values()].some(item => item.override),
      'The real production monitorData path must be loaded through the exact-source override');
    await runCase('cold unavailable read then valid native push takes over once without another read', {}, async ({ m, f, own, push }) => {
      own(); await drain(); assert.equal(await m.readMonitorSnapshot(), null); await drain();
      assert.equal(f.takeovers().length, 0); let displays = 0;
      const off = m.subscribeMonitorHudSnapshot(value => { if (value?.top) ++displays; }); f.disposers.push(off);
      await push(); assert(displays >= 1, 'A valid cold push must reach a display listener'); off();
      assert.equal(f.takeovers().length, 1); assert.equal(m.monitorTakeoverState(), 'done');
      assert.equal(f.relayOff, true); assert.equal(f.reads(), 1);
      const timers = f.timerEvents.length;
      for (let i = 0; i < 20; ++i) await push();
      assert.equal(f.takeovers().length, 1, 'Success must not resend'); assert.equal(f.reads(), 1);
      assert.equal(f.timerEvents.length, timers, 'Native pushes add no timers without display subscribers');
    });
    await runCase('warm available read remains a single takeover', { initialAvailable: true }, async ({ m, f, own, push }) => {
      own(); await drain(); assert((await m.readMonitorSnapshot())?.top); await drain();
      for (let i = 0; i < 20; ++i) await push();
      assert.equal(f.takeovers().length, 1); assert.equal(f.reads(), 1); assert.equal(m.monitorTakeoverState(), 'done');
    });
    for (const [name, need] of [['no owner', null], ['clock-only owner', { top: false, fps: false, fileSystem: false }]]) {
      await runCase(name + ' receives pushes without control commands', {}, async ({ m, f, own, push }) => {
        if (need) own(need); await drain(); await m.readMonitorSnapshot(); await drain();
        const calls = f.calls.length, timers = f.timerEvents.length;
        for (let i = 0; i < 20; ++i) await push();
        assert.equal(f.calls.length, calls); assert.equal(f.timerEvents.length, timers);
        assert.equal(f.takeovers().length, 0); assert.equal(f.reads(), 1);
      });
    }
    await runCase('public hub admit is data-only even with an active owner', {}, async ({ m, f }) => {
      const hub = m.createMonitorDataHub({ invoke: f.invoke, now: () => f.now,
        readTextFile: async () => { ++f.fileReads; throw Error('Unexpected read'); } });
      const off = hub.acquire('public-only', { top: true, fps: false, fileSystem: false }); f.disposers.push(off);
      await drain(); await hub.read(); await drain(); const calls = f.calls.length, timers = f.timerEvents.length;
      for (let i = 0; i < 20; ++i) { f.advance(); assert(hub.admitDisplayPublication(f.packet())?.top); await drain(); }
      assert.equal(f.calls.length, calls); assert.equal(f.timerEvents.length, timers); assert.equal(hub.attempts().takeover, 0);
    });
    const invalid = [
      ['bad schema', f => ({ schema: 99 })], ['bad inner JSON', f => ({ top: '{' })],
      ['future collectedAt', f => ({ collectedAt: f.now + 1 })],
      ['older generation', f => ({ generation: 0 })], ['older sequence', f => ({ seq: 0 })],
      ['missing relay epoch', f => ({ relayEpoch: undefined })], ['string relay epoch', f => ({ relayEpoch: '0' })],
      ['string session', f => ({ session: '1' })], ['expired transport envelope', f => ({ collectedAt: f.now - 6000 })],
      ['negative relay epoch', f => ({ relayEpoch: -1 })], ['fractional relay epoch', f => ({ relayEpoch: 0.5 })],
      ['unsafe relay epoch', f => ({ relayEpoch: Number.MAX_SAFE_INTEGER + 1 })],
      ['missing session', f => ({ session: undefined })], ['zero session', f => ({ session: 0 })],
      ['fractional session', f => ({ session: 1.5 })], ['unsafe session', f => ({ session: Number.MAX_SAFE_INTEGER + 1 })],
    ];
    for (const [name, change] of invalid) await runCase(name + ' grants no takeover', {}, async ({ m, f, own }) => {
      own(); await drain(); await m.readMonitorSnapshot(); await drain();
      const calls = f.calls.length; f.advance(); m.acceptPublishedMonitorSnapshot(f.packet(change(f))); await drain();
      assert.equal(f.takeovers().length, 0); assert.equal(f.calls.length, calls); assert.equal(f.reads(), 1);
    });
    await runCase('new session without its own relay epoch cannot inherit the old cached epoch', {}, async ({ m, f, own }) => {
      own(); await drain(); await m.readMonitorSnapshot(); await drain();
      f.switchSession(2); f.advance(); m.acceptPublishedMonitorSnapshot(f.packet({ available: false, relayEpoch: undefined })); await drain();
      const calls = f.calls.length; f.advance(); m.acceptPublishedMonitorSnapshot(f.packet({ relayEpoch: undefined })); await drain();
      assert.equal(f.takeovers().length, 0); assert.equal(f.calls.length, calls); assert.equal(f.reads(), 1);
    });
    await runCase('retired native session grants no takeover', {}, async ({ m, f, own }) => {
      own(); await drain(); await m.readMonitorSnapshot(); await drain(); f.switchSession(2); f.advance();
      m.acceptPublishedMonitorSnapshot(f.packet({ available: false })); await drain();
      const calls = f.calls.length;
      m.acceptPublishedMonitorSnapshot(f.packet({ session: 1 })); await drain();
      assert.equal(f.takeovers().length, 0); assert.equal(f.calls.length, calls); assert.equal(f.reads(), 1);
    });
    for (const subscribe of ['subscribeMonitorSnapshot', 'subscribeMonitorHudSnapshot']) {
      await runCase(subscribe + ' synchronous demand release prevents takeover', {}, async ({ m, f, own, push }) => {
        const release = own(); await drain(); await m.readMonitorSnapshot(); await drain(); let released = false;
        const off = m[subscribe](value => { if (value?.top) { released = true; release(); } }); f.disposers.push(off);
        await push(); assert(released, 'Publication callback must run synchronously before takeover');
        assert.equal(f.takeovers().length, 0); assert.equal(f.reads(), 1);
      });
    }
    for (const failure of ['pending receipt', 'cleaned false', 'exception']) {
      await runCase(failure + ' shares read/push budget five and 1s minimum dispatch spacing', {
        initialAvailable: true,
        takeover: async () => { if (failure === 'exception') throw Error('virtual native failure');
          return { ok: true, cleaned: false, ...(failure === 'pending receipt' ? { pending: true } : {}) }; },
      }, async ({ m, f, own, push }) => {
        own(); await drain(); await m.readMonitorSnapshot(); await drain(); assert.equal(f.takeovers().length, 1);
        const timers = f.timerEvents.length;
        for (let i = 0; i < 30; ++i) await push({}, 5);
        assert.equal(f.takeovers().length, 1, 'Fast pushes must respect the dispatch gate');
        for (let i = 0; i < 30; ++i) await push();
        const calls = f.takeovers(); assert.equal(calls.length, 5, 'Existing read and push paths share exactly five attempts');
        for (let i = 1; i < calls.length; ++i) assert(calls[i].at - calls[i - 1].at >= 1000);
        assert.equal(m.monitorTakeoverState(), 'pending'); assert.equal(f.relayOff, false);
        assert.equal(f.reads(), 1); assert.equal(f.timerEvents.length, timers, 'Takeover retries must not install timers');
      });
    }
    await runCase('in-flight takeover collapses fast pushes without a timer', {
      takeover: f => f.hold().promise,
    }, async ({ m, f, own, push }) => {
      own(); await drain(); await m.readMonitorSnapshot(); await drain(); await push();
      assert.equal(f.takeovers().length, 1); const timers = f.timerEvents.length;
      for (let i = 0; i < 50; ++i) await push({}, 10);
      assert.equal(f.takeovers().length, 1); assert.equal(f.timerEvents.length, timers); assert.equal(f.reads(), 1);
      f.deferred[0].resolve({ ok: true, cleaned: true }); await drain();
      assert.equal(m.monitorTakeoverState(), 'done');
    });
    await runCase('new session receives a fresh bounded budget after the old five are exhausted', {
      takeover: async () => ({ ok: true, cleaned: false }),
    }, async ({ m, f, own, push }) => {
      own(); await drain(); await m.readMonitorSnapshot(); await drain();
      for (let i = 0; i < 15; ++i) await push();
      assert.equal(f.takeovers().length, 5); f.switchSession(2); await push();
      assert.equal(f.takeovers().filter(call => call.session === 2).length, 1);
      for (let i = 0; i < 20; ++i) await push();
      assert.equal(f.takeovers().length, 10);
      assert.equal(f.takeovers().filter(call => call.session === 2).length, 5); assert.equal(f.reads(), 1);
    });
    await runCase('old-session late successful acknowledgement cannot mark the new session done', {
      takeover: (f, call) => call.session === 1 ? f.hold().promise : Promise.resolve({ ok: true, cleaned: true }),
    }, async ({ m, f, own, push }) => {
      own(); await drain(); await m.readMonitorSnapshot(); await drain(); await push();
      assert.equal(f.takeovers().length, 1); f.switchSession(2); await push();
      f.deferred[0].resolve({ ok: true, cleaned: true }); await drain();
      assert.notEqual(m.monitorTakeoverState(), 'done', 'Old native session cannot complete the new owner');
      await push(); assert.equal(f.takeovers().filter(call => call.session === 2).length, 1);
      assert.equal(m.monitorTakeoverState(), 'done'); assert.equal(f.reads(), 1);
    });
  } catch (error) { prepareError = { name: error.name, message: error.message, stack: error.stack }; }
  finally {
    Date.now = realNow;
    for (const [name, original] of Object.entries(originalTimers)) globalThis[name] = original;
    if (previousSlot) Object.defineProperty(globalThis, mockSlot, previousSlot); else delete globalThis[mockSlot];
  }
  const ok = !prepareError && cases.length > 0 && cases.every(item => item.ok);
  const result = { mode: 'push-relay-only', status: prepareError ? 'PREPARATION_FAILED' : ok ? 'PASS' : 'FAIL', ok,
    executedThisRun: true, startedAtUtc: new Date(startedAt).toISOString(), durationMs: realNow() - startedAt,
    source: { canonicalPath: canonicalSource, actualInputPath: sourcePath, bytes: sourceBytes.length, sha256: hash(sourceBytes) },
    tool: { path: import.meta.filename, sha256: hash(toolBytes) }, bundleSha256: bundleHash,
    dependencies: [...dependencies.values()], mocks, esbuildVersion: createRequire(path.join(root, 'package.json'))('esbuild').version,
    checks: cases.length, passed: cases.filter(item => item.ok).length, cases, prepareError,
    scope: 'Actual bundled monitorData production singleton and public APIs. Only native IPC state and Date.now virtual; real timers observed/delegated, no synthetic timer firing. Not real UI/hardware, installed-build acceptance or a benchmark.',
    productSourceWrites: 0, testsAddedToRegistry: 0, realApplicationsStarted: 0, hardwareReads: false, collectorStarted: false,
  };
  fs.writeFileSync(resultPath, JSON.stringify(result, null, 2) + '\n', { flag: 'wx' });
  console.log(JSON.stringify({ status: result.status, checks: result.checks, passed: result.passed, resultPath, source: result.source }, null, 2));
  if (!ok) process.exitCode = 1;
}
