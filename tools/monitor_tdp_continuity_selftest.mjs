// Production passive TDP display continuity; inert receipts and fake clock only.
// No settings/hardware writes, IPC, sensors, or CPU measurement.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
const root=process.cwd(), req=createRequire(path.join(root,'package.json'));
const out=path.resolve(process.env.YMCC_TDP_CONTINUITY_OUT || '../../../Build/Tasks/YMCC-Steam-HUD-Probe/live-jitter-20261015');
fs.mkdirSync(out,{recursive:true});
const {build}=req('esbuild');
await build({stdin:{contents:"export {createTdpRuntimeStore} from './src/bridge/tdpRuntimeState';export {createAppliedTdpFeed} from './src/bridge/monitorAppliedTdp';",resolveDir:root,loader:'ts'},bundle:true,platform:'node',format:'cjs',outfile:path.join(out,'tdp-continuity.cjs'),plugins:process.env.YMCC_MONITOR_TDP_SOURCE?[{name:'explicit-recheck-baseline',setup(build){build.onLoad({filter:/monitorAppliedTdp\.ts$/},a=>path.resolve(a.path)===path.resolve(root,'src/bridge/monitorAppliedTdp.ts')?{contents:fs.readFileSync(process.env.YMCC_MONITOR_TDP_SOURCE,'utf8'),loader:'ts',resolveDir:root+'/src/bridge'}:undefined)}}]:[]});
const {createTdpRuntimeStore,createAppliedTdpFeed}=req(path.join(out,'tdp-continuity.cjs'));
const checks=[];
function harness(){
 let now=0,id=0,bindings=0;const timers=new Map(),store=createTdpRuntimeStore(()=>1700000000000+now);
 const feed=createAppliedTdpFeed({initial:store.get(),observe:cb=>{bindings++;const off=store.observe(cb);return()=>{bindings--;off()}},now:()=>now,setTimer:(fn,ms)=>{const key=++id;timers.set(key,{fn,at:now+ms});return key},clearTimer:key=>timers.delete(key)});
 const values=[],off=feed.subscribe(v=>values.push(v));
 function at(next,fire=true){assert(next>=now);if(fire){for(let i=0;i<100;i++){const entry=[...timers].filter(([,t])=>t.at<=next).sort((a,b)=>a[1].at-b[1].at)[0];if(!entry)break;timers.delete(entry[0]);now=entry[1].at;entry[1].fn();}}now=next;feed.advance();}
 const apply=w=>store.finish(store.begin('ac',w),true);
 const last=()=>values.at(-1),ready=w=>{store.powerMode('ac');apply(w);at(now+1000);assert.equal(last().watts,w)};
 return {store,feed,values,off,timers,at,apply,last,ready,get now(){return now},get bindings(){return bindings}};
}
{
 const h=harness();h.ready(30);const baseline=h.values.length;
 for(let n=0;n<20;n++){
  const beginAt=h.now,receipt=h.store.begin('ac',50);
  assert.equal(h.store.get().status,'pending');assert.equal(h.store.get().watts,null);
  assert.equal(h.last().status,'applied');assert.equal(h.last().watts,n?35:30);
  h.at(beginAt+250);h.store.finish(receipt,true,35);h.at(beginAt+1000);
  assert.equal(h.last().watts,35);assert.equal(h.timers.size,0);
 }
 assert(h.values.slice(baseline).every(s=>s.status==='applied'&&s.watts===35));
 h.off();assert.equal(h.bindings,0);assert.equal(h.timers.size,0);
 checks.push('20 repeated in-flight writes preserve last real ACK; accepted 35W replaces requested 50W; raw store remains pending/null');
}
{
 const h=harness();h.ready(30);h.store.begin('ac',40);assert.equal(h.timers.size,1);
 h.at(3000);h.store.begin('ac',45);assert.equal(h.timers.size,1);
 h.at(5999);assert.equal(h.last().watts,30);
 h.at(6000);assert.equal(h.last().status,'pending');assert.equal(h.last().watts,null);assert.equal(h.timers.size,0);
 h.store.begin('ac',50);assert.equal(h.last().watts,null);h.off();
 checks.push('A pending chain expires exactly at 5 seconds; repeated requests cannot renew the old ACK');
}
for(const boundary of ['failed','unknown','power','invalidate','suspend']){
 const h=harness();h.ready(30);const receipt=h.store.begin('ac',45);
 if(boundary==='failed')h.store.finish(receipt,false);
 if(boundary==='unknown')h.store.finishUnknown(receipt);
 if(boundary==='power')h.store.powerMode('dc');
 if(boundary==='invalidate')h.store.invalidate();
 if(boundary==='suspend')h.store.invalidatePower();
 assert.equal(h.last().watts,null);assert.equal(h.timers.size,0);
 if(boundary==='power')assert.equal(h.last().mode,'dc');
 if(boundary==='suspend')assert.equal(h.last().mode,null);
 h.store.finish(receipt,true);assert.equal(h.last().watts,null);h.off();
 checks.push(boundary+': clear immediately and never replay the previous ACK');
}
{
 const h=harness();h.store.powerMode('ac');h.store.begin('ac',99);assert.equal(h.last().watts,null);h.at(5000);assert.equal(h.last().watts,null);h.off();
 checks.push('Startup pending without a successful write never exposes the requested target');
}
{
 const h=harness();h.ready(30);h.store.begin('ac',35);h.at(6000,false);assert.equal(h.last().watts,null);h.off();
 checks.push('Native display advance enforces the 5-second bound even when browser timers are throttled');
}
{
 const h=harness();h.ready(30);const second=[];const off2=h.feed.subscribe(v=>second.push(v));assert.equal(h.bindings,1);
 h.store.begin('ac',40);h.off();assert.equal(h.bindings,1);assert.equal(h.timers.size,1);
 off2();off2();assert.equal(h.bindings,0);assert.equal(h.timers.size,0);
 let reopen;const off3=h.feed.subscribe(v=>reopen=v);assert.equal(reopen.status,'pending');assert.equal(reopen.watts,null);off3();
 checks.push('Multiple monitors share one source and one grace timer; last detach releases both and reopening does not revive old watts');
}
const result={ok:true,checks,scope:'Production common display feed and raw runtime store, deterministic receipts; no real hardware/installed modifications.'};
fs.writeFileSync(path.join(out,'TDP-CONTINUITY-RESULT.json'),JSON.stringify(result,null,2));
console.log('TDP continuity PASS',checks.length);
