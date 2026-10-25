// Actual Vue reactivity and the production topmon setter/projection. No real IO.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
const root=process.cwd(), out=path.resolve(process.env.YMCC_TOP_SHALLOW_REF_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/internal-top-shallow-ref-20261017');
fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(root+'/package.json')('esbuild');
const before=fs.readFileSync('tools/fixtures/monitor_top_shallow_ref_before_20261017.ts','utf8');
const current=fs.readFileSync(process.argv.includes('--probe')?out+'/proposed-topmon.ts':'src/bridge/topmon.ts','utf8');
const now=2_000_000, sourcePins={}, results={}, tracking=!process.argv.includes('--production');
globalThis.__topShallowRef={};
const require=createRequire(root+'/package.json'), ts=require('typescript');
const appText=fs.readFileSync('src/App.vue','utf8');
const appScript=require('vue/compiler-sfc').parse(appText).descriptor.scriptSetup.content;
const ast=ts.createSourceFile('App.ts',appScript,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
const functionNames=['classifyVideoPower','updateLowBatteryStatic','observeVideoPower'];
const appFunctions=functionNames.map(name=>{const node=ast.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text===name);assert(node,name);return node.getText(ast)}).join('\n');
const powerWatcher=ast.statements.find(n=>ts.isExpressionStatement(n)&&ts.isCallExpression(n.expression)&&n.expression.expression.getText(ast)==='watch'&&n.expression.arguments[0]?.getText(ast)==='topMonitorData');
assert(powerWatcher,'production App monitor watcher');
sourcePins.App=crypto.createHash('sha256').update(appText).digest('hex');
sourcePins.backgroundPolicy=crypto.createHash('sha256').update(fs.readFileSync('src/bridge/backgroundPolicy.ts')).digest('hex');
function appHooks(ref){return `
import {ref,watch} from 'vue';
import {topMonitorData} from './src/bridge/topmon';
import {nextLowBatteryStatic} from './src/bridge/backgroundPolicy';
type VideoPowerState='unknown'|'desktop'|'charging'|'discharging';
const videoPowerState=ref<VideoPowerState>('unknown'),lowBatteryStatic=ref(false),backgroundVideoAutoPause=ref(true);
let lastVideoPowerTs=0,pendingVideoPowerState:Exclude<VideoPowerState,'unknown'>|null=null,pendingVideoPowerSamples=0,videoPowerEventSettleUntil=0;
function reconcileBackgroundLifecycle(){${ref}.appLifecycle+=1;}
${appFunctions.replaceAll('Date.now()',ref+'.now')}
${powerWatcher.getText(ast)}
export function __appPowerInfo(){return {power:videoPowerState.value,static:lowBatteryStatic.value,lastTs:lastVideoPowerTs,pending:pendingVideoPowerState,samples:pendingVideoPowerSamples,reconciles:${ref}.appLifecycle};}
export function __resetAppPower(){videoPowerState.value='unknown';lowBatteryStatic.value=false;lastVideoPowerTs=0;pendingVideoPowerState=null;pendingVideoPowerSamples=0;videoPowerEventSettleUntil=0;${ref}.appLifecycle=0;}
`;}
async function load(side){
 const h=globalThis.__topShallowRef[side]={calls:[],now,visible:true,callbacks:new Set(),reply:null,appLifecycle:0};
 const ref='globalThis.__topShallowRef['+JSON.stringify(side)+']';
 const plugin={name:'isolated-native-boundary',setup(b){
  b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]topmon\.ts$/},()=>{let source=side==='before'?before:current;sourcePins[side]=crypto.createHash('sha256').update(source).digest('hex');return {contents:source.replaceAll('Date.now()',ref+'.now'),loader:'ts',resolveDir:path.join(root,'src/bridge')}});
  b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]powerSource\.ts$/},()=>({contents:"import {ref} from 'vue';export const powerSourceMode=ref(null);",loader:'ts',resolveDir:root}));
  b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]monitorData\.ts$/},()=>({contents:`const h=${ref};export function acquireMonitorDemand(owner,need){h.calls.push({kind:'start',owner,need});return ()=>h.calls.push({kind:'stop',owner});}export async function readMonitorSnapshot(){h.calls.push({kind:'read'});return h.reply;}`,loader:'ts'}));
  b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]uiLifecycle\.ts$/},()=>({contents:`const h=${ref};export const isUiVisible=()=>h.visible;export function onUiVisibilityChange(fn){h.callbacks.add(fn);fn(h.visible);return ()=>h.callbacks.delete(fn);}`,loader:'ts'}));
 }};
 const code=await build({stdin:{contents:`export * from './src/bridge/topmon';export {monitorCpuTemperature} from './src/bridge/monitorSnapshotSignals';export {powerSourceMode} from './src/bridge/powerSource';export {effect,stop,watch,computed,isReactive,toRaw,nextTick} from 'vue';`+appHooks(ref),loader:'ts',resolveDir:root},bundle:true,write:false,platform:'node',format:'esm',plugins:[plugin],define:{'process.env.NODE_ENV':JSON.stringify(tracking?'development':'production'),__VUE_OPTIONS_API__:'true',__VUE_PROD_DEVTOOLS__:'false',__VUE_PROD_HYDRATION_MISMATCH_DETAILS__:'false'}});
 const file=out+'/'+side+'.mjs';fs.writeFileSync(file,code.outputFiles[0].text);
 return {h,m:await import(pathToFileURL(file).href+'?'+Date.now())};
}
function top(patch={}){return {ts:now,cpuPowerValid:true,cpuClockValid:true,cpuTempValid:true,tdpW:30,freqMhz:4000,tempC:65,ac:1,hasBattery:true,batteryPercent:60,chargeW:-25,batteryPowerValid:true,batteryPowerEstimated:false,batteryPowerTs:now,batteryStatusTs:now,batteryTimeValid:true,batteryTimeEstimated:true,batteryTimeTs:now,remainMin:70,cpuUsage:25,cpuUsageValid:true,cpuUsageTs:now,gpuPowerW:40,gpuClockMhz:1800,thermalThrottleFound:false,thermalThrottleMax:0,virtualMemoryCommittedFound:true,virtualMemoryCommittedMb:10000,virtualMemoryLoadFound:true,virtualMemoryLoadPct:40,hwDown:false,...patch};}
function snapshot(t,patch={}){return {schema:1,session:1,relayEpoch:0,generation:1,seq:1,collectedAt:now,heartbeatAt:now,hwinfoOkAt:now,top:t,fps:null,topActive:true,fpsActive:false,hwDown:false,source:'snapshot',...patch};}
const plain=x=>JSON.parse(JSON.stringify(x));
for(const side of ['before','after']){
 const {h,m}=await load(side),cases=[],trace=[],watchTrace=[],track=[],io=()=>h.calls.length;
 let runs=0;
 const temperature=m.computed(()=>m.monitorCpuTemperature(m.topMonitorData.value));
 const stopWatch=m.watch(()=>m.topMonitorData.value?.batteryPercent??null,value=>watchTrace.push(value),{flush:'sync'});
 const runner=m.effect(()=>{
  ++runs;const d=m.topMonitorData.value;
  trace.push(d?{ts:d.ts,power:d.tdpW,temp:d.tempC,clock:d.freqMhz,ac:d.ac,battery:d.batteryPercent,charge:d.chargeW,time:d.remainMin,cpu:d.cpuUsage,gpu:d.gpuPowerW,gpuClock:d.gpuClockMhz,hwDown:d.hwDown,cpuValid:d.cpuTempValid,estimated:d.batteryPowerEstimated}:null);
 },{onTrack:e=>track.push(String(e.key))});
 const check=(name,fn)=>{fn();cases.push(name)};
 check('initial missing value is unavailable',()=>{assert.equal(m.topMonitorData.value,null);assert.equal(temperature.value,null)});
 m.powerSourceMode.value='ac';track.length=0;
 const input=top({ac:0});m.setTopMonitorData(input);
 const trackedKeys=[...track],reactive=m.isReactive(m.topMonitorData.value);
 check('AC override retains detached scalar shell',()=>{assert.equal(m.topMonitorData.value.ac,1);assert.notEqual(m.toRaw(m.topMonitorData.value),input);input.tempC=120;assert.equal(m.topMonitorData.value.tempC,65);assert.equal(temperature.value,65)});
 check('DC edge updates all existing consumers synchronously',()=>{m.powerSourceMode.value='dc';assert.equal(m.topMonitorData.value.ac,0);assert.equal(trace.at(-1).ac,0)});
 check('healthy temperature replacement updates computed consumer',()=>{m.setTopMonitorData(top({tempC:91,batteryPercent:29}));assert.equal(temperature.value,91);assert.equal(watchTrace.at(-1),29)});
 check('same scalar values in a new snapshot retain replacement delivery',()=>{const count=runs;m.setTopMonitorData(top({tempC:91,batteryPercent:29}));assert.equal(runs,count+1)});
 check('missing temperature remains unavailable',()=>{m.setTopMonitorData(top({cpuTempValid:false,tempC:0}));assert.equal(temperature.value,null)});
 check('hardware loss does not become live zero temperature',()=>{m.setTopMonitorData(top({hwDown:true,tempC:0}));assert.equal(temperature.value,null)});
 check('real idle zero temperature retains explicit proof',()=>{m.setTopMonitorData(top({tempC:0}));assert.equal(temperature.value,0)});
 check('missing and recovered battery replace the whole source',()=>{m.setTopMonitorData(top({hasBattery:false,batteryPercent:-1}));assert.equal(watchTrace.at(-1),-1);m.setTopMonitorData(top({batteryPercent:80}));assert.equal(watchTrace.at(-1),80)});
 check('frozen producer arrays remain shared and frozen',()=>{const cores=Object.freeze([1,null,99]),labels=Object.freeze(['CPU1','CPU2','CPU3']);m.setTopMonitorData(top({coreUsage:cores,coreLabels:labels}));assert.equal(m.topMonitorData.value.coreUsage,cores);assert.equal(m.topMonitorData.value.coreLabels,labels);assert(Object.isFrozen(cores));assert.throws(()=>{m.topMonitorData.value.coreUsage[0]=50},TypeError)});
 check('clear and recovery notify existing subscribers',()=>{m.setTopMonitorData(null);assert.equal(trace.at(-1),null);assert.equal(temperature.value,null);m.setTopMonitorData(top({tempC:55}));assert.equal(temperature.value,55)});
 check('null power mode preserves established pass-through behavior',()=>{m.powerSourceMode.value=null;const t=top({ac:0});m.setTopMonitorData(t);assert.equal(m.toRaw(m.topMonitorData.value),t);assert.equal(m.topMonitorData.value.ac,0)});
 check('AC restoration updates the same shared ref',()=>{m.powerSourceMode.value='ac';assert.equal(m.topMonitorData.value.ac,1)});
 const allValues=plain(m.topMonitorData.value);
 const projections=[];
 for(const [label,t,patch] of [
  ['healthy',top(),{}],['hw-down',top(),{hwDown:true}],['stale',top({ts:now-6000}),{}],['future',top({ts:now+1}),{}],
  ['cpu-proof-loss',top({cpuUsageValid:false}),{}],['battery-loss',top({batteryPowerValid:false,batteryTimeValid:false}),{}],
  ['independent-cpu',top({ts:now-7000,cpuUsageTs:now}),{}],['empty',null,{}]
 ]){
  h.reply=snapshot(t,patch);const beforeIo=io();const projected=await m.readTopMonitor();assert.equal(io(),beforeIo+1);projections.push({label,value:plain(projected)});m.setTopMonitorData(projected);
 }
 cases.push('actual top projection handles eight validity/loss scenarios');
 const demandStart=h.calls.length;
 h.visible=false;await m.startTopMonitor();await m.startTopMonitor();assert.equal(h.calls.length,demandStart);
 h.visible=true;for(const fn of h.callbacks)fn(true);assert.equal(h.calls.length,demandStart+1);
 await m.stopTopMonitor();assert.equal(h.calls.length,demandStart+1);
 h.visible=false;for(const fn of h.callbacks)fn(false);assert.equal(h.calls.length,demandStart+2);
 h.visible=true;for(const fn of h.callbacks)fn(true);assert.equal(h.calls.length,demandStart+3);
 await m.stopTopMonitor();assert.equal(h.calls.length,demandStart+4);assert.equal(h.callbacks.size,0);cases.push('hidden/visible and overlapping mount demand ownership retained');
 m.setTopMonitorData(null);await m.nextTick();m.powerSourceMode.value=null;m.__resetAppPower();
 const appPolicyTrace=[];let appTs=now;
 async function policy(name,patch,expected){m.setTopMonitorData(patch===null?null:top({ts:++appTs,...patch}));await m.nextTick();const state=m.__appPowerInfo();for(const [key,value] of Object.entries(expected))assert.equal(state[key],value,name+'/'+key);appPolicyTrace.push({name,state});}
 await policy('first discharge sample',{ac:0,batteryPercent:50},{power:'unknown',static:false,samples:1});
 await policy('confirmed discharge',{ac:0,batteryPercent:50},{power:'discharging',static:false,samples:0});
 await policy('low battery enters static',{ac:0,batteryPercent:20},{power:'discharging',static:true});
 await policy('hysteresis keeps static',{ac:0,batteryPercent:23},{static:true});
 await policy('unknown battery preserves state',{ac:0,batteryPercent:-1},{static:true});
 await policy('battery recovery resumes',{ac:0,batteryPercent:25},{static:false});
 await policy('first charge sample',{ac:1,batteryPercent:20,chargeW:5},{power:'discharging',static:false,samples:1});
 await policy('confirmed charge',{ac:1,batteryPercent:20,chargeW:5},{power:'charging',static:false,samples:0});
 await policy('missing snapshot preserves power policy',null,{power:'charging',static:false});
 cases.push('actual App monitor watcher and power/battery policy retain nine transitions');
 stopWatch();m.stop(runner);
 assert.equal(reactive,side==='before');
 assert.equal(trackedKeys.filter(k=>k!=='value').length,tracking&&side==='before'?14:0);
 results[side]={cases,trace,watchTrace,runs,allValues,projections,appPolicyTrace,calls:h.calls,reactive,trackedKeys};
}
for(const key of ['cases','trace','watchTrace','runs','allValues','projections','appPolicyTrace'])assert.deepEqual(results.after[key],results.before[key],key+' changed');
// A subsequent unified-resource optimization explicitly opts internal topbar
// demand out of filesystem sampling. Keep its ownership/call sequence equal,
// and assert this intentional hint separately instead of hiding it.
const withoutHint=events=>events.map(event=>event.kind==='start'?{...event,need:{top:event.need.top}}:event);
assert.deepEqual(withoutHint(results.after.calls),withoutHint(results.before.calls));
assert(results.after.calls.filter(event=>event.kind==='start').every(event=>event.need.fileSystem===false));
const result={ok:true,vueBuild:tracking?'development-with-dependency-instrumentation':'production',scope:'Actual production topmon module and actual Vue; native boundaries inert. Whole-snapshot API and current consumer behavior only. Direct in-place consumer mutation is not used by current production and is not claimed equivalent.',cases:results.after.cases.length,sourcePins,work:{before:{rootIsReactive:results.before.reactive,dependencyRegistrations:results.before.trackedKeys.length,scalarRegistrations:results.before.trackedKeys.filter(k=>k!=='value').length},after:{rootIsReactive:results.after.reactive,dependencyRegistrations:results.after.trackedKeys.length,scalarRegistrations:results.after.trackedKeys.filter(k=>k!=='value').length}},results,wholeProcessCpuMeasured:false,hardwareAccessed:false};
fs.writeFileSync(out+'/RESULT.json',JSON.stringify(result,null,2));console.log(JSON.stringify({ok:true,cases:result.cases,work:result.work,sourcePins},null,2));
