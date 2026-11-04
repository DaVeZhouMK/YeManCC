import assert from 'node:assert/strict';
import {createMonitorLiveEdits} from '../src/bridge/monitorLiveEdits';
import {createMonitorOutputCoordinator} from '../src/bridge/monitorOutputCoordinator';
import {normalizeMonitorOutput} from '../src/bridge/monitorHudModel';
let cases=0;
const tick=async()=>{await Promise.resolve();await Promise.resolve();};
function setup(){let prefs=normalizeMonitorOutput(),saves:any[]=[],applies:any[]=[],steam:any[]=[],rtss:any[]=[],errors:any[]=[],busy:boolean[]=[],timers=new Map<number,()=>void>(),id=0,failSave=false,block:Promise<void>|null=null;const c=createMonitorLiveEdits({snapshot:()=>structuredClone(prefs),save:async p=>{saves.push(p);if(failSave)throw Error('save failed');if(block)await block;},apply:async p=>{applies.push(p)},steam:async(p,s)=>{steam.push({p,s})},rtss:async p=>{rtss.push(p)},busy:v=>busy.push(v),failure:e=>errors.push(String(e)),timer:f=>{timers.set(++id,f);return id},clearTimer:h=>{timers.delete(h as number)}});return {c,get prefs(){return prefs},set prefs(p){prefs=p},saves,applies,steam,rtss,errors,busy,timers,setFailSave(){failSave=true},setBlock(p:Promise<void>){block=p},fire(){const all=[...timers.values()];timers.clear();for(const f of all)f();}}}
{const s=setup();assert.equal(s.timers.size,0);await tick();assert.equal(s.saves.length,0);assert.equal(s.applies.length,0);cases++;}
{const s=setup();for(let i=0;i<100;i++){s.prefs.ymcc.opacity=i/100;s.c.request({preferences:true});}assert.equal(s.timers.size,1);assert.equal(s.saves.length,0);s.fire();await s.c.flush();assert.equal(s.saves.length,1);assert.equal(s.saves[0].ymcc.opacity,.99);assert.equal(s.applies.length,0);assert.equal(s.timers.size,0);cases++;}
{const s=setup();s.prefs.output='steam';s.c.request({preferences:true,output:true,immediate:true});s.prefs.output='rtss';s.c.request({preferences:true,output:true,immediate:true});await s.c.flush();assert.equal(s.saves.length,1);assert.equal(s.applies.length,1);assert.equal(s.applies[0].output,'rtss');cases++;}
{const s=setup();s.c.request({steam:{scale:1.2}});s.c.request({steam:{opacity:.5}});await s.c.flush();assert.deepEqual(s.steam[0].p,{scale:1.2,opacity:.5});assert.equal(s.saves.length,0);assert.equal(s.applies.length,0);assert(!('position'in s.steam[0].p));cases++;}
{const s=setup();s.prefs.rtssScale=80;s.c.request({preferences:true,rtss:true});s.prefs.rtssScale=120;s.c.request({preferences:true,rtss:true,immediate:true});await s.c.flush();assert.equal(s.rtss.length,1);assert.equal(s.rtss[0].rtssScale,120);cases++;}
{const s=setup();s.setFailSave();s.c.request({preferences:true,output:true,immediate:true});await s.c.flush();assert.equal(s.applies.length,0);assert.equal(s.errors.length,1);assert.equal(s.busy.at(-1),false);cases++;}
{const s=setup();s.prefs.ymcc.opacity=.4;s.c.request({preferences:true});s.prefs.ymcc.opacity=.9;await s.c.dispose();assert.equal(s.saves[0].ymcc.opacity,.4);assert.equal(s.timers.size,0);s.c.request({preferences:true});await s.c.flush();assert.equal(s.saves.length,1);cases++;}
{const s=setup();let release:()=>void=()=>{};s.setBlock(new Promise<void>(r=>release=r));s.prefs.output='steam';s.c.request({preferences:true,output:true,immediate:true});await tick();s.prefs.output='rtss';s.c.request({preferences:true,output:true});s.prefs.output='ymcc';s.c.request({preferences:true,output:true});release();await s.c.flush();assert.deepEqual(s.applies.map(p=>p.output),['steam','ymcc']);assert.equal(s.timers.size,0);cases++;}
{const calls:string[]=[];let active=0,maximum=0;const coord=createMonitorOutputCoordinator({capabilities:async()=>({steam:true,ymcc:true}),ensureRtss:async()=>{calls.push('backend')},setSteam:async p=>{calls.push('steam:'+p);return {confirmed:true}},setRtss:async show=>{calls.push('rtss:'+show);return {confirmed:true}},setYmcc:async show=>{calls.push('ymcc:'+show);return {confirmed:true}}});let prefs=normalizeMonitorOutput();const c=createMonitorLiveEdits({snapshot:()=>structuredClone(prefs),save:async()=>{},apply:async p=>{active++;maximum=Math.max(maximum,active);try{const r=await coord.apply(p);assert(r.ok);}finally{active--}},steam:async()=>{},rtss:async()=>{},busy:()=>{},failure:e=>{throw e},timer:()=>0,clearTimer:()=>{}});prefs.output='rtss';c.request({preferences:true,output:true,immediate:true});await c.flush();assert.equal(maximum,1);assert(calls.indexOf('rtss:true')>calls.indexOf('steam:0'));assert(calls.indexOf('rtss:true')>calls.indexOf('ymcc:false'));cases++;}

const boundaryCases:any[]=[];
async function finalizationBoundary(mode:'success'|'failed-first'|'dispose') {
 const events:string[]=[],loads:string[]=[],errors:string[]=[],timers=new Map<number,()=>void>();let id=0,queued=false,prefs=normalizeMonitorOutput({enabled:true,output:'rtss',rtssTemplate:'W'}),queue:ReturnType<typeof createMonitorLiveEdits>;
 let release:()=>void=()=>{};const wait=new Promise<void>(r=>release=r);
 const coord=createMonitorOutputCoordinator({capabilities:async()=>({steam:true,ymcc:true}),ensureRtss:async()=>{},setSteam:async()=>({confirmed:true}),setYmcc:async()=>({confirmed:true}),setRtss:async(show,p)=>{
  if(!show)return {confirmed:true};loads.push(p.rtssTemplate);events.push('load:'+p.rtssTemplate);if(p.rtssTemplate==='L')await wait;
  return {confirmed:mode!=='failed-first'||p.rtssTemplate!=='W',reason:'controlled first-target refusal'};
 }});
 let finished=false,disposeFinished=false,disposePromise:Promise<void>|null=null;
 queue=createMonitorLiveEdits({snapshot:()=>normalizeMonitorOutput(prefs),save:async p=>{events.push('save:'+p.rtssTemplate)},apply:async p=>{const result=await coord.apply(p);events.push('apply:'+p.rtssTemplate+':'+result.ok);if(!result.ok)throw Error(result.reason);},steam:async()=>{},rtss:async()=>{},busy:value=>{
  events.push('busy:'+value);if(!value&&!queued){queued=true;queueMicrotask(()=>{prefs=normalizeMonitorOutput({...prefs,rtssTemplate:'L'});events.push('request:L');queue.request({preferences:true,output:true,immediate:true});if(mode==='dispose')disposePromise=queue.dispose().then(()=>{disposeFinished=true;events.push('dispose:finished');});});}
 },failure:e=>errors.push(String(e)),timer:fn=>{timers.set(++id,fn);return id},clearTimer:handle=>{timers.delete(handle as number)}});
 queue.request({preferences:true,output:true,immediate:true});const flushed=queue.flush().then(()=>{finished=true;events.push('flush:finished');});
 for(let n=0;n<100;n++)await Promise.resolve();
 const before={finished,disposeFinished,loads:[...loads],events:[...events],queue:queue.inspect()};
 if(mode==='dispose'){prefs=normalizeMonitorOutput({...prefs,rtssTemplate:'J'});queue.request({preferences:true,output:true,immediate:true});}
 release();await flushed;await queue.flush();if(disposePromise)await disposePromise;
 const after={finished,disposeFinished,loads:[...loads],events:[...events],queue:queue.inspect(),timers:timers.size,errors:[...errors]};
 const checks={latestReached:before.loads.join(',')==='W,L',flushWaits:!before.finished&&before.queue.running,disposeWaits:mode!=='dispose'||!before.disposeFinished,settled:after.finished&&!after.queue.running&&!after.queue.pending&&after.timers===0,onlyRequestedLoads:after.loads.join(',')==='W,L',failureRecovered:errors.length===(mode==='failed-first'?1:0),disposeCompleted:mode!=='dispose'||after.disposeFinished&&after.queue.disposed};
 return {mode,before,after,checks,ok:Object.values(checks).every(Boolean)};
}
for(const mode of ['success','failed-first','dispose'] as const){const result=await finalizationBoundary(mode);boundaryCases.push(result);if(result.ok)cases++;}
{
 let release:()=>void=()=>{};const wait=new Promise<void>(r=>release=r);let prefs='first',firstFinished=false;
 const q=createMonitorLiveEdits({snapshot:()=>prefs,save:async value=>{if(value==='future')await wait},apply:async()=>{},steam:async()=>{},rtss:async()=>{},busy:()=>{},failure:e=>{throw e},timer:()=>0,clearTimer:()=>{}});
 q.request({preferences:true,output:true,immediate:true});await q.flush().then(()=>firstFinished=true);prefs='future';q.request({preferences:true,output:true,immediate:true});for(let n=0;n<20;n++)await Promise.resolve();
 const before={firstFinished,running:q.inspect().running};release();await q.flush();const ok=before.firstFinished&&before.running&&!q.inspect().running;boundaryCases.push({mode:'future-event-independent',ok,before,after:q.inspect()});if(ok)cases++;
}
console.log(JSON.stringify({passed:cases,originalCases:9,boundaryCases,scope:'Pure event queue plus actual exclusive output coordinator; all native/Steam/storage ports controlled. No application or process mutation.'},null,2));
if(boundaryCases.some(c=>!c.ok))process.exitCode=2;
