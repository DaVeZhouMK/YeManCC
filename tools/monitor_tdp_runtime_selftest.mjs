// Offline only: production runtime store/feed/model and actual setTdp body.
// Hardware/saved settings are inert dependencies. Never invokes the real bridge.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
const root=process.cwd(),req=createRequire(path.join(root,'package.json')),{build}=req('esbuild');
const out=path.resolve(root,'../../../Build/Tasks/YMCC-Steam-HUD-Probe/templates-tdp-content-20261010');fs.mkdirSync(out,{recursive:true});
const source=fs.readFileSync('src/bridge/yeman.ts','utf8');
const setTdp=source.slice(source.indexOf('export async function setTdp('),source.indexOf('export async function stopTdpDaemon('));
assert(setTdp.includes('tdpRuntimeStore.finish(receipt, true, acceptedW)'));
const targetResolver=source.slice(source.indexOf('export function resolveTdpTarget('),source.indexOf('export function formatTdpTarget('));
assert(targetResolver.includes('effectiveW'));
const defaultCapability={provider:'default',selected:false,ready:true,minPL1:2,maxPL1:200};
const contents=`
export * from './src/bridge/tdpRuntimeState.ts';export * from './src/bridge/monitorAppliedTdp.ts';export * from './src/bridge/monitorHudModel.ts';export * from './src/bridge/monitorBarPacking.ts';
import {tdpRuntimeStore} from './src/bridge/tdpRuntimeState.ts';
import {beginTdpObservation,tdpErrorText} from './src/bridge/tdpStaticDiagnostics.ts';
async function getTdpExecutionCapability(){return memory.capability;}
import {runTdpHardwareWrite,resetTdpCapabilityState} from './src/bridge/tdpCircuitBreaker.ts';
export const memory={capability:${JSON.stringify(defaultCapability)} as any,saved:null as number|null,saveCount:0,failSave:false,blocked:false,float:false,writes:[] as any[],exitCode:0,pending:null as null|Promise<void>,oemCeiling:35,oemReply:null as any};
export function reset(){resetTdpCapabilityState();tdpRuntimeStore.invalidate();Object.assign(memory,{capability:${JSON.stringify(defaultCapability)},saved:null,saveCount:0,failSave:false,blocked:false,float:false,writes:[],exitCode:0,pending:null});}
type SetTdpOpts={save?:boolean,apply?:boolean,vendor?:'amd'|'intel'|'unknown'};
const TDP_MIN=2,TDP_MAX=200;
type TdpExecutionCapability=any;type TdpTargetResolution=any;
let tdpExecutionCapability:any=null;
const clampTdp=(w:number)=>Math.max(2,Math.min(200,Math.round(w)));
${targetResolver}
async function saveTdp(_mode:string,w:number){if(memory.failSave)throw Error('inert save failed');memory.saved=w;memory.saveCount++;}
async function assertHardwareWriteAllowed(){if(memory.blocked)throw Error('inert power boundary');return {generation:1};}
async function invoke(cmd:string,args:any){
 if(cmd!=='tdpMsi.set')throw Error('Unexpected inert IPC '+cmd);
 memory.writes.push(['oem',args.watts]);
 if(memory.oemReply)return memory.oemReply;
 const acceptedPL1W=Math.min(args.watts,memory.oemCeiling);
 return {ok:true,rc:0,acceptedVerified:true,acceptedPL1W,readbackMatched:acceptedPL1W===args.watts,outcome:acceptedPL1W===args.watts?'applied':'oem-adjusted'};
}
async function detectVendor(){return 'amd';}async function floatTdpActive(){return memory.float;}
async function ensureTdpDaemon(){return true;}let tdpDaemonUp=false;
const TDPCTL_EXE=()=> 'INERT-NO-PROCESS';
async function tdpDaemonSet(w:number){memory.writes.push(['daemon',w]);await memory.pending;if(memory.exitCode)throw Error('inert daemon failure');}
const shell={async run(_cmd:string,args:string[]){memory.writes.push(['direct',Number(args[1])]);await memory.pending;return {exitCode:memory.exitCode,stderr:'inert write failed',stdout:''};}};
${setTdp}
`;
const bundle=await build({stdin:{contents,resolveDir:root,loader:'ts'},bundle:true,platform:'node',format:'esm',write:false,alias:{'@':path.join(root,'src')}});
const file=path.join(out,'tdp-runtime-under-test.mjs');fs.writeFileSync(file,bundle.outputFiles[0].text);const m=await import('file:///'+file.replaceAll('\\','/')+'?'+Date.now());
const cases=[],check=(name,fn)=>{fn();cases.push(name)};
{
 const s=m.createTdpRuntimeStore(()=>12345);
 check('Startup unknown: no saved-config, physical readback or AC assumption',()=>assert.deepEqual(s.get(),{mode:null,status:'unknown',watts:null,proof:null,revision:0,confirmedAt:null}));
 s.powerMode('ac');const r=s.begin('ac',45);
 check('Pending hardware operation clears effective value',()=>{assert.equal(s.get().status,'pending');assert.equal(s.get().watts,null)});
 s.finish(r,true);
 check('AC receipt records the confirmed write with explicit write-ack provenance',()=>{assert.equal(s.get().mode,'ac');assert.equal(s.get().watts,45);assert.equal(s.get().confirmedAt,12345);assert.equal(s.get().proof,'write-ack')});
 s.powerMode('dc');check('AC to DC invalidates: never takes a saved/cached DC number',()=>{assert.equal(s.get().status,'unknown');assert.equal(s.get().watts,null)});
 const dc=s.begin('dc',18);s.finish(dc,true);check('DC becomes valid only after a new confirmed DC transaction',()=>assert.equal(s.get().watts,18));
 const late=s.begin('dc',22);s.powerMode('ac');s.finish(late,true);check('Previous-side late ACK cannot validate the new AC epoch',()=>assert.equal(s.get().watts,null));
 const ac=s.begin('ac',48);s.powerMode('dc');s.powerMode('ac');s.finish(ac,true);check('AC to DC to AC does not resurrect a matching-side old ACK',()=>assert.equal(s.get().status,'unknown'));
 const first=s.begin('ac',20),second=s.begin('ac',32);s.finish(first,true);assert.equal(s.get().watts,null);s.finish(second,true);
 check('Newest transaction owns state: a queued newer write excludes older success',()=>assert.equal(s.get().watts,32));
 s.powerMode('ac');check('Duplicate same-side power notifications do not invalidate a good receipt',()=>assert.equal(s.get().watts,32));
 const failure=s.begin('ac',40);s.finish(failure,false);check('Failed/skipped hardware operation never exposes requested watts',()=>{assert.equal(s.get().status,'failed');assert.equal(s.get().watts,null)});
 const beforeSleep=s.begin('ac',50);s.invalidate();s.finish(beforeSleep,true);check('Sleep/settings-context invalidation rejects in-flight pre-boundary ACK',()=>assert.equal(s.get().watts,null));
 const unknown=m.createTdpRuntimeStore();unknown.finish(unknown.begin('dc',28),true);check('Unknown current power side cannot be inferred from caller mode',()=>assert.equal(unknown.get().watts,null));
 const bad=s.begin('ac',Infinity);s.finish(bad,true);check('Non-finite/out-of-range receipt is unknown, not fabricated',()=>assert.equal(s.get().watts,null));
 let calls=0;const off=s.observe(()=>{calls++;throw Error('display-failed')});s.begin('ac',12);off();const n=calls;s.invalidate();check('Observer faults and cleanup cannot disrupt TDP control',()=>assert.equal(calls,n));
}
{
 m.reset();m.tdpRuntimeStore.powerMode('ac');await m.setTdp('ac',45,{apply:true,save:true});
 check('Actual setTdp body publishes successful manual direct write, not just saved config',()=>{assert.equal(m.memory.saved,45);assert.equal(m.tdpRuntimeStore.get().watts,45);assert.deepEqual(m.memory.writes,[['direct',45]])});
 const revision=m.tdpRuntimeStore.get().revision;await m.setTdp('dc',80,{apply:false,save:true});
 check('Config-only save does not change effective AC runtime or issue a write',()=>{assert.equal(m.memory.saved,80);assert.equal(m.tdpRuntimeStore.get().watts,45);assert.equal(m.tdpRuntimeStore.get().revision,revision);assert.equal(m.memory.writes.length,1)});
 m.memory.float=true;await m.setTdp('ac',23,{apply:true,save:false,vendor:'amd'});
 check('Floating temporary target uses same receipt source and does not overwrite configured ceiling',()=>{assert.equal(m.memory.saved,80);assert.equal(m.tdpRuntimeStore.get().watts,23);assert.deepEqual(m.memory.writes.at(-1),['daemon',23])});
 m.memory.float=false;m.memory.exitCode=1;await assert.rejects(m.setTdp('ac',70,{apply:true,save:true}));
 check('Config saved before failed actual write is never shown as effective TDP',()=>{assert.equal(m.memory.saved,70);assert.equal(m.tdpRuntimeStore.get().watts,null);assert.equal(m.tdpRuntimeStore.get().status,'failed')});
 await assert.rejects(m.setTdp('ac',80,{apply:true,save:false}));check('Circuit-breaker skipped/backoff write cannot confirm a value',()=>assert.equal(m.tdpRuntimeStore.get().watts,null));
 m.reset();m.tdpRuntimeStore.powerMode('ac');let resolve;m.memory.pending=new Promise(r=>resolve=r);
 const late=m.setTdp('ac',60,{apply:true,save:false});await new Promise(r=>setTimeout(r,0));m.tdpRuntimeStore.powerMode('dc');resolve();await late;
 check('Actual delayed hardware success after AC/DC change cannot display stale AC watts',()=>assert.equal(m.tdpRuntimeStore.get().watts,null));
 m.memory.pending=null;await m.setTdp('dc',20,{apply:true,save:false});check('Actual next DC write establishes new effective side/value',()=>{assert.equal(m.tdpRuntimeStore.get().mode,'dc');assert.equal(m.tdpRuntimeStore.get().watts,20)});
 await m.setTdp('ac',31,{apply:true,save:false});
 check('Legacy global AC policy key is not mistaken for physical power mode: successful write on DC is DC runtime',()=>{assert.equal(m.tdpRuntimeStore.get().mode,'dc');assert.equal(m.tdpRuntimeStore.get().watts,31)});
 m.tdpRuntimeStore.invalidatePower();await m.setTdp('ac',33,{apply:true,save:false});
 check('Wake without renewed power-side observation remains unknown despite successful write',()=>{assert.equal(m.tdpRuntimeStore.get().mode,null);assert.equal(m.tdpRuntimeStore.get().watts,null)});
 m.reset();m.memory.blocked=true;await assert.rejects(m.setTdp('dc',30,{apply:true,save:false}));check('Native lifecycle admission denial publishes no effective value',()=>{assert.equal(m.memory.writes.length,0);assert.equal(m.tdpRuntimeStore.get().watts,null)});
}
{
 m.reset();m.tdpRuntimeStore.powerMode('ac');
 for(const ceiling of [30,35]){
  m.memory.capability={provider:'default',selected:false,ready:true,minPL1:8,maxPL1:ceiling};
  await m.setTdp('ac',120,{apply:true,save:true});
  check('User target 120W resolves to accepted '+ceiling+'W, without false failure or saved-intent migration',()=>{
   assert.equal(m.memory.saved,120);assert.equal(m.tdpRuntimeStore.get().watts,ceiling);assert.deepEqual(m.memory.writes.at(-1),['direct',ceiling]);
  });
 }
 m.reset();m.tdpRuntimeStore.powerMode('ac');
 m.memory.capability={provider:'msi-g3e-wmi',selected:true,ready:true,minPL1:8,maxPL1:200,generation:1,deadlineTick:8000,machineMaximumKnown:false};
 for(const accepted of [30,45,65]){
  m.memory.oemCeiling=accepted;
  await m.setTdp('ac',120,{apply:true,save:true});
  check('OEM receives user 120W once and its '+accepted+'W verified setting owns the runtime value, not a CPU/model cap',()=>{
   assert.equal(m.memory.saved,120);assert.equal(m.tdpRuntimeStore.get().watts,accepted);assert.deepEqual(m.memory.writes.at(-1),['oem',120]);
  });
 }
 for(const outcome of ['unverified','oem-rejected']){
  m.memory.oemReply={ok:false,rc:0,readbackMatched:false,outcome};
  assert.equal(await m.setTdp('ac',120,{apply:true,save:false}),false);
  check('OEM '+outcome+' is a non-error unknown receipt, not fabricated success',()=>{assert.equal(m.tdpRuntimeStore.get().status,'unknown');assert.equal(m.tdpRuntimeStore.get().watts,null);});
 }
 m.memory.oemReply=null;
 m.reset();m.tdpRuntimeStore.powerMode('ac');
 m.memory.capability={provider:'default',selected:false,ready:false,minPL1:8,maxPL1:30};
 await assert.rejects(m.setTdp('ac',120,{apply:true,save:true}));
 check('Unknown/unready capability is a real denial, not an automatic success',()=>{assert.equal(m.memory.saveCount,0);assert.equal(m.memory.writes.length,0);});
 m.reset();m.tdpRuntimeStore.powerMode('ac');
 m.memory.capability={provider:'default',selected:false,ready:true,minPL1:8,maxPL1:30};m.memory.exitCode=1;
 await assert.rejects(m.setTdp('ac',120,{apply:true,save:true}));
 check('Accepted target still reports genuine hardware transport failure',()=>{assert.equal(m.memory.saved,120);assert.equal(m.tdpRuntimeStore.get().status,'failed');assert.equal(m.tdpRuntimeStore.get().watts,null);});
}
{
 const store=m.createTdpRuntimeStore(()=>1);store.powerMode('ac');store.finish(store.begin('ac',45),true);
 const fresh={top:{ts:9900,hwDown:false,tdpW:28.4},fps:{ts:9900,fps:100},hwDown:false};
 const a=m.hudFromUniformSnapshot(fresh,10000,store.get());
 check('TDP ceiling and actual CPU package watts stay separate',()=>{assert.equal(a.values.tdpLimit,45);assert.equal(a.values.cpuPower,28.4)});
 check('Without runtime receipt, package watts/config are never substituted',()=>{assert.equal(m.hudFromUniformSnapshot(fresh,10000).values.tdpLimit,null);assert.equal(m.hudFromUniformSnapshot(fresh,10000,45).values.tdpLimit,null)});
 check('Hardware collector down does not turn an independently valid YMCC runtime receipt into package watts',()=>assert.equal(m.hudValue(m.hudFromUniformSnapshot(null,10000,store.get()),'tdpLimit'),'45'));
 for(const status of ['unknown','pending','failed'])check('HUD rejects '+status+' runtime even with a numeric requested value',()=>assert.equal(m.hudFromUniformSnapshot(fresh,10000,{...store.get(),status,watts:99}).values.tdpLimit,null));
 check('HUD does not accept unproven numeric state',()=>assert.equal(m.hudFromUniformSnapshot(fresh,10000,{...store.get(),proof:null}).values.tdpLimit,null));
}
{
 let now=0,id=0,observations=0;const timers=new Map(),store=m.createTdpRuntimeStore(()=>now);
 const f=m.createAppliedTdpFeed({initial:store.get(),observe:cb=>{observations++;const off=store.observe(cb);return()=>{observations--;off()}},now:()=>now,setTimer:(fn,ms)=>{timers.set(++id,{fn,ms});return id},clearTimer:id=>timers.delete(id)});
 const values=[],values2=[],off=f.subscribe(v=>values.push(v)),off2=f.subscribe(v=>values2.push(v));
 check('All monitors share one passive TDP binding and zero initial reads/timers',()=>{assert.equal(observations,1);assert.equal(timers.size,0)});
 store.powerMode('ac');store.finish(store.begin('ac',20),true);const good=values.filter(v=>v.status==='applied').length;
 for(let i=21;i<=40;i++)store.finish(store.begin('ac',i),true);
 check('Burst runtime writes clear uncertainty immediately but successful values coalesce at 1Hz',()=>{assert.equal(values.filter(v=>v.status==='applied').length,good);assert.equal(timers.size,1)});
 now=1000;const t=[...timers][0];timers.delete(t[0]);t[1].fn();
 check('At 1000ms every subscriber receives only latest confirmed watts',()=>{assert.equal(values.at(-1).watts,40);assert.equal(values2.at(-1).watts,40);assert.equal(timers.size,0)});
 store.powerMode('dc');check('Power boundary clears all displays immediately, not after the gate',()=>{assert.equal(values.at(-1).watts,null);assert.equal(values.at(-1).mode,'dc')});
 store.finish(store.begin('dc',12),true);off();check('One display closing cannot detach another consumer',()=>assert.equal(observations,1));off2();off2();
 check('Last unsubscribe cancels own passive binding and pending timer',()=>{assert.equal(observations,0);assert.equal(timers.size,0)});
 now=2000;store.powerMode('ac');store.finish(store.begin('ac',55),true);let current;const again=f.subscribe(v=>current=v);assert.equal(current.watts,55);again();
 check('Reopen receives current runtime rather than previous-side display cache',()=>assert.equal(current.mode,'ac'));
}
check('Content packing selects actual longest row instead of full cap width',()=>{const w=m.packedBarWidth([112,312,312,130],703,15.6,26);assert(Math.abs(w-483.6)<.01);assert.equal(m.packedBarWidth([112],703,15.6,26),138)});
check('Packing is bounded and rejects invalid inputs',()=>{assert.equal(m.packedBarWidth([1000],703,15.6,26),703);assert.equal(m.packedBarWidth([NaN],703,15.6,26),0);assert.equal(m.packedBarWidth([112],0,1,0),0)});
for(const file of ['src/bridge/tdpRuntimeState.ts','src/bridge/monitorAppliedTdp.ts'])check(file+' has no hardware/config polling',()=>{const s=fs.readFileSync(file,'utf8');assert(!/setInterval|readSettingsSection|shell\.run|tdpDaemon\.request|monitor\.read/.test(s))});
const power=fs.readFileSync('src/bridge/powerSource.ts','utf8'),app=fs.readFileSync('src/App.vue','utf8');
check('Existing central power and suspend/resume paths invalidate runtime without new hardware probes',()=>{assert(power.includes('tdpRuntimeStore.powerMode(mode)'));for(const name of ['onPowerSuspending','onPowerResuming'])assert(new RegExp('function '+name+'[^\\{]*\\{\\s*tdpRuntimeStore.invalidatePower\\(\\)').test(app))});
const report={suite:'Real YMCC applied TDP state / AC-DC / content packing',passed:cases.length,cases,physicalHardwareReadback:false,actualHardwareTest:false,scope:'Actual setTdp function body, production state/feed/model and packing; inert saved-config/direct/daemon APIs; no settings reads, desktop control or hardware I/O.'};
fs.writeFileSync(path.join(out,'TDP-RUNTIME-SELFTEST.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));
