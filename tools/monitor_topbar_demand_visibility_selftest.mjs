// Real topmon, unified hub and UI lifecycle, in a headless browser. Only native
// transport/settings/power-source IO is inert; never drives the installed YMCC.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { createServer } from 'node:http';
const root = process.cwd();
const out = path.resolve(process.env.YMCC_TOPBAR_VISIBILITY_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/unified-internal-data-20261017/visibility');
fs.mkdirSync(out, { recursive: true });
const { build } = createRequire(root + '/package.json')('esbuild');
const runtime = 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules';
const { chromium } = createRequire(path.join(runtime, '_monitor-demand.cjs'))('playwright');
const before = fs.readFileSync('tools/fixtures/unified_internal_topmon_before_20261017.ts', 'utf8');
const floatSource = fs.readFileSync('src/bridge/autofloat.ts', 'utf8');
const beforeFloatSource = fs.readFileSync('tools/fixtures/unified_internal_autofloat_before_20261017.ts', 'utf8');
const floatImports = floatSource.match(/import \{([\s\S]*?)\} from '\.\/yeman';/)[1].split('import {').pop().split(',').map(x => x.trim()).filter(x => x && !x.startsWith('type '));
const floatProbe = `
export async function __unifiedDataTickProbe(){
  lastFloatActiveTs=Date.now();hwinfoPollTs=0;continuousOwner=null;
  await tick();await Promise.resolve();await Promise.resolve();return getFloatInfo();
}
export async function __unifiedDataContinuousProbe(){
  const bank=legacyContinuousBank('bal','small',30,'ac'),cpu=automaticCpuControls(bank);
  const state={watts:Math.floor(30*(1-SAVING_RULES.small.ratio)),frequencyMHz:cpu.maxMHz,aggressiveness:cpu.aggressiveness,stable:1,lastDown:0,recoverUntil:0};
  continuousOwner={bank,side:'ac',current:async()=>true,applyCore:async()=>{throw Error('Unexpected TDP actuation')},state,targetPid:2077};
  lastFloatActiveTs=Date.now();hwinfoPollTs=0;curTarget=60;
  await tick();await Promise.resolve();await Promise.resolve();
  const result={info:getFloatInfo(),state:{...continuousOwner.state},initial:{...state}};continuousOwner=null;return result;
}`;
const mocks = {
  './ipc': `export const isNativeRuntime=true;export function on(){return()=>{}};export const invoke=(name,args)=>window.__native.invoke(name,args);`,
  './api': `export const windowApi={};export const fs={readTextFile:async()=>{throw Error('Unexpected legacy file read')},writeTextFile:async(...a)=>{window.__native.blockedIo.push(['file-write',a]);throw Error('No file write permitted')}};`,
  './yeman': floatImports.map(name => name === 'PW' ? 'export const PW={YEMAN:"fixture"};' : name === 'getTdpHardwareMinimum' ? 'export const getTdpHardwareMinimum=()=>6;' : `export function ${name}(...a){window.__native.blockedIo.push([${JSON.stringify(name)},a]);throw Error('No power control permitted')}`).join('\n'),
  './settingsRepository': `export const getSettingsGeneration=()=>1;export function assertSettingsGeneration(){};export async function readSettingsSection(){return {}};export async function saveSettingsSection(){throw Error('No settings write permitted')}`,
  './gamePolicyTarget': `export function getPolicyGame(){return {pid:2077}}`,
  './powerSource': `import {ref} from 'vue';export const powerSourceMode=ref('ac');`,
  './monitorAppliedTdp': `export function advanceMonitorTdpDisplay(){};export function subscribeMonitorTdpState(){return()=>{}};`,
};
const scripts = {}, sourcePins = {};
for (const side of ['before', 'after']) {
  const result = await build({ stdin: { contents: `import * as top from './src/bridge/topmon';import * as data from './src/bridge/monitorData';import * as life from './src/bridge/uiLifecycle';import {readStatus,startMonitor,stopMonitor,__unifiedDataTickProbe,__unifiedDataContinuousProbe} from './src/bridge/autofloat';window.__app={...top,...data,...life,readStatus,startMonitor,stopMonitor,__unifiedDataTickProbe,__unifiedDataContinuousProbe};`, resolveDir: root, loader: 'ts' }, bundle: true, platform: 'browser', format: 'iife', write: false, metafile: true,
    plugins: [{ name: 'inert-native', setup(b) {
      b.onResolve({ filter: /.*/ }, a => mocks[a.path] && a.importer.replaceAll('\\','/').includes('/src/bridge/') ? { path: a.path, namespace: 'mock' } : null);
      b.onLoad({ filter: /.*/, namespace: 'mock' }, a => ({ contents: mocks[a.path], loader: 'ts', resolveDir: root }));
      b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]autofloat\.ts$/}, a=>{
        const raw=side==='before'?beforeFloatSource:floatSource;
        const signature='async function tickContinuous(owner: ContinuousFloatOwner, fps: number, low: number, pid: number, cpuBusy = 0): Promise<void> {';
        assert(raw.includes(signature));
        return {contents:raw.replace(signature,signature+'\n  window.__native.cpuObservations.push(cpuBusy);')+floatProbe,loader:'ts',resolveDir:path.dirname(a.path)};
      });
      if (side === 'before') b.onLoad({ filter: /[\\/]src[\\/]bridge[\\/]topmon\.ts$/ }, a => ({ contents: before, loader: 'ts', resolveDir: path.dirname(a.path) }));
    } }] });
  scripts[side] = result.outputFiles[0].text;
  fs.writeFileSync(path.join(out, side + '.js'), scripts[side]);
  sourcePins[side] = {};
  for (const input of Object.keys(result.metafile.inputs)) if (fs.existsSync(input)) {
    const rel=input.replaceAll('\\','/');
    const raw=side==='before'&&rel==='src/bridge/topmon.ts'?before:side==='before'&&rel==='src/bridge/autofloat.ts'?beforeFloatSource:fs.readFileSync(input);
    sourcePins[side][input]=crypto.createHash('sha256').update(raw).digest('hex');
  }
}
const init = String.raw`
let wall=1710000000000;Date.now=()=>++wall;
window.__native={flags:{top:false,fps:false},calls:[],holds:{},pending:[],blockedIo:[],cpuObservations:[],data:false,seq:0,
  async invoke(name,args){
    this.calls.push({name,args});
    if(name==='monitor.start'||name==='monitor.stop'){
      if(this.holds[name])await new Promise(resolve=>this.pending.push({name,resolve}));
      for(const flag of ['top','fps'])if(args?.[flag]===true)this.flags[flag]=name==='monitor.start';
      return {ok:true,...this.flags};
    }
    if(name==='monitor.snapshot'){
      const now=Date.now();return {schema:1,available:this.data,session:17,relayEpoch:0,generation:1,seq:++this.seq,collectedAt:now,heartbeatAt:now,hwinfoOkAt:now,hwDown:false,topActive:this.flags.top,fpsActive:this.flags.fps,
        top:this.flags.top?JSON.stringify({ts:now,tdpW:30,tempC:65,freqMhz:4000,hasBattery:false,cpuUsage:90,cpuUsageValid:true,cpuUsageTs:now}):null,
        fps:this.flags.fps?JSON.stringify({ts:now,fps:117,fps1:85,gpu:80,packagePower:30,pid:2077,game:'fixture'}):null};
    }
    if(name==='monitor.takeover')return {ok:true,cleaned:true,session:17,relayEpoch:0};
    throw Error('Unexpected native operation '+name);
  }
};
window.__releases={};window.__visibility=true;
Object.defineProperty(document,'visibilityState',{get:()=>window.__visibility?'visible':'hidden'});
window.__doc=visible=>{window.__visibility=visible;document.dispatchEvent(new Event('visibilitychange'))};
window.__emit=name=>window.dispatchEvent(new CustomEvent(name));
window.__drain=async()=>{for(let i=0;i<100;++i)await Promise.resolve()};
window.__releasePending=name=>{const pending=window.__native.pending.filter(p=>p.name===name);window.__native.pending=window.__native.pending.filter(p=>p.name!==name);for(const p of pending)p.resolve()};
window.__uiListeners=new Set();
const add=window.addEventListener.bind(window),remove=window.removeEventListener.bind(window);
window.addEventListener=(name,listener,...rest)=>{if(name==='ui:visibility')window.__uiListeners.add(listener);return add(name,listener,...rest)};
window.removeEventListener=(name,listener,...rest)=>{if(name==='ui:visibility')window.__uiListeners.delete(listener);return remove(name,listener,...rest)};
`;
const server = createServer((req, res) => { res.setHeader('Content-Type', 'text/html;charset=utf-8'); res.end('<!doctype html><meta charset="utf-8"><title>Unified demand lifecycle test</title>'); });
await new Promise(r => server.listen(0, '127.0.0.1', r));
let browser;
const cases = [], errors = [], traces = {};
try {
  browser = await chromium.launch({ headless: true, executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe' });
  for (const side of ['before', 'after']) {
    const page = await browser.newPage();
    page.on('pageerror', e => errors.push(side + ': ' + String(e)));
    await page.goto('http://127.0.0.1:' + server.address().port + '/');
    await page.addScriptTag({ content: init }); await page.addScriptTag({ content: scripts[side] });
    const run = code => page.evaluate(async source => { const value = await (new Function('return (async()=>{' + source + '})()'))(); await window.__drain(); return value; }, code);
    const observe = async name => {
      const value = await page.evaluate(() => ({ flags: { ...window.__native.flags }, confirmed: window.__app.monitorDemandConfirmed(), calls: structuredClone(window.__native.calls), listeners: window.__uiListeners.size, pending: window.__native.pending.length }));
      (traces[side] ??= {})[name] = value; return value;
    };
    const equalFlags = (row, top, fps, label) => assert.deepStrictEqual(row.flags, { top, fps }, side + ': ' + label);
    await run('await __app.startTopMonitor();');
    equalFlags(await observe('mounted'), true, false, 'visible mount starts top');
    await run("__emit('ipc:window.hidden');");
    equalFlags(await observe('hidden'), side === 'before', false, 'hidden top owner releases only after fix');
    await run("__emit('ipc:window.shown');"); equalFlags(await observe('shown'), true, false, 'shown resumes');
    await run('await __app.startTopMonitor();await __app.stopTopMonitor();'); equalFlags(await observe('overlap'), true, false, 'one overlapping mount survives');
    await run('await __app.stopTopMonitor();'); equalFlags(await observe('unmounted'), false, false, 'last mount releases');
    await run("__emit('ipc:window.hidden');__emit('ipc:window.shown');");
    const stopped = await observe('unmounted-events'); equalFlags(stopped, false, false, 'unmounted events cannot restart'); assert.equal(stopped.listeners, 0);
    await run("__emit('ipc:window.hidden');await __app.startTopMonitor();"); equalFlags(await observe('hidden-first-mount'), side === 'before', false, 'hidden initial mount');
    await run("__emit('ipc:window.shown');__releases.policy=__app.acquireMonitorDemand('test-policy',{fps:true});");
    equalFlags(await observe('policy-active'), true, true, 'policy is independent');
    await run("__emit('ipc:window.hidden');");
    const policyHidden = await observe('policy-hidden'); equalFlags(policyHidden, side === 'before', true, 'hidden top never stops FPS policy'); assert.equal(policyHidden.confirmed.fps, true);
    const status = await run("__native.data=true;return await __app.readStatus();");
    assert.equal(status.fps,117);assert.equal(status.fps1,85);assert.equal(status.pid,2077);
    const tickReadCount = await run("const before=__native.calls.filter(c=>c.name==='monitor.snapshot').length;const info=await __app.__unifiedDataTickProbe();return {reads:__native.calls.filter(c=>c.name==='monitor.snapshot').length-before,info,blockedIo:__native.blockedIo};");
    assert.equal(tickReadCount.reads,1,'current internal tick and health must share one read');assert.equal(tickReadCount.info.status.fps,117);assert.equal(tickReadCount.info.hwinfoDown,false);assert.deepStrictEqual(tickReadCount.blockedIo,[]);
    traces[side]['current-internal-consumer']={status,tick:tickReadCount};
    await run("__releases.hud=__app.acquireMonitorDemand('test-hud',{top:true,fps:true});"); equalFlags(await observe('hud-hidden'), true, true, 'HUD retains top behind hidden window');
    await run("__releases.preview=__app.acquireMonitorDemand('test-preview',{top:true});__releases.hud();"); equalFlags(await observe('preview-hidden'), true, true, 'separate preview still retains top');
    await run('__releases.preview();'); equalFlags(await observe('preview-released'), side === 'before', true, 'last hidden top owner releases without FPS');
    await run('await __app.startMonitor();');
    equalFlags(await observe('actual-controller-demand'),true,true,'actual autofloat owns CPU data independently of topbar visibility');
    const continuous=await run('const output=await __app.__unifiedDataContinuousProbe();return {output,cpuObservations:__native.cpuObservations,blockedIo:__native.blockedIo};');
    assert.deepStrictEqual(continuous.cpuObservations,[90],'real tick must pass fresh CPU pressure to actual continuous controller');
    assert.deepStrictEqual(continuous.blockedIo,[],'busy CPU with settled watt floor must not downshift');
    assert.equal(continuous.output.state.frequencyMHz,continuous.output.initial.frequencyMHz);assert.equal(continuous.output.state.aggressiveness,continuous.output.initial.aggressiveness);
    traces[side]['current-continuous-consumer']=continuous;
    await run('await __app.stopMonitor();');
    equalFlags(await observe('actual-controller-release'),side==='before',true,'controller releases only its own combined demand');
    await run("__emit('ipc:window.shown');await __app.stopTopMonitor();__releases.policy();"); equalFlags(await observe('all-released'), false, false, 'all owners release');
    await run("await __app.startTopMonitor();__emit('ipc:power.suspending');"); equalFlags(await observe('suspending'), side === 'before', false, 'power gate releases UI owner');
    await run("__emit('ipc:power.resuming');"); equalFlags(await observe('resuming'), side === 'before', false, 'no premature power resume');
    await run("__emit('ipc:power.resumed');"); equalFlags(await observe('resumed'), true, false, 'power ready resumes');
    await run("__doc(false);__emit('ipc:power.resumed');"); equalFlags(await observe('doc-hidden'), side === 'before', false, 'document visibility also owns demand');
    await run('__doc(true);'); const stable = await observe('doc-visible'); equalFlags(stable, true, false, 'document show resumes');
    await run("for(let i=0;i<200;++i){__emit('ipc:window.shown');__emit('ipc:power.resumed');__doc(true)}");
    const repeated = await observe('repeated-events'); assert.equal(repeated.calls.length, stable.calls.length, 'identical events must not churn native commands');
    await run('await __app.stopTopMonitor();await __app.stopTopMonitor();'); equalFlags(await observe('extra-stop'), false, false, 'extra unmount remains bounded');
    // Hold real hub commands at their native boundary to test rapid UI changes.
    await run("__native.holds['monitor.start']=true;await __app.startTopMonitor();__emit('ipc:window.hidden');");
    assert.equal((await observe('held-start')).pending, 1);
    await run("__native.holds['monitor.start']=false;__releasePending('monitor.start');"); equalFlags(await observe('held-start-finished'), side === 'before', false, 'late start cannot outlive hidden owner');
    await run("__emit('ipc:window.shown');"); equalFlags(await observe('held-start-reshown'), true, false, 'new visible intent starts');
    if (side === 'after') {
      await run("__native.holds['monitor.stop']=true;__emit('ipc:window.hidden');"); assert.equal((await observe('held-stop')).pending, 1);
      await run("__emit('ipc:window.shown');__native.holds['monitor.stop']=false;__releasePending('monitor.stop');"); equalFlags(await observe('held-stop-finished'), true, false, 'show during pending stop reconciles back to active');
    }
    await run('await __app.stopTopMonitor();'); const final = await observe('final'); equalFlags(final, false, false, 'final state idle'); assert.equal(final.listeners, 0); assert.equal(final.pending, 0);
    cases.push(side + ': full lifecycle/multi-consumer/late-command trace passed');
    await page.close();
  }
  assert.deepStrictEqual(errors, []);
  const result = { ok: true, cases, observations: Object.values(traces).reduce((n, v) => n + Object.keys(v).length, 0), beforeReproduced: traces.before.hidden.flags.top && traces.before['hidden-first-mount'].flags.top, afterReleases: !traces.after.hidden.flags.top && !traces.after['hidden-first-mount'].flags.top, traces, sourcePins, pageErrors: errors, scope: 'Production topmon, monitor hub and UI lifecycle in headless Edge. Native operations are simulated cache/command receipts; no installed app, actual sleep, game, hardware or process CPU measurement.' };
  fs.writeFileSync(path.join(out, 'RESULT.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify({ ok: true, observations: result.observations, beforeReproduced: result.beforeReproduced, afterReleases: result.afterReleases, pageErrors: errors }, null, 2));
} finally { if (browser) await browser.close(); await new Promise(r => server.close(r)); }
