// Actual before/current FPS admission gate, virtual clock and callback lifecycle.
// No hardware, timers in the running app, game input or CPU timing.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {pathToFileURL} from 'node:url';
const root=process.cwd(),task=path.resolve(process.env.YMCC_FPS_REENTRANT_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/monitor-fps-reentrant-20261016'),out=path.join(task,'tests');fs.mkdirSync(out,{recursive:true});
const {build}=createRequire(path.join(root,'package.json'))('esbuild'),pins={};
async function load(side){const source=path.join(side==='before'?path.join(task,'before'):root,'src/bridge/monitorHudFpsVisibility.ts');pins[side]=crypto.createHash('sha256').update(fs.readFileSync(source)).digest('hex');const b=await build({entryPoints:[source],bundle:true,write:false,platform:'node',format:'esm'}),file=path.join(out,side+'.mjs');fs.writeFileSync(file,b.outputFiles[0].text);return import(pathToFileURL(file).href+'?'+Date.now());}
const before=await load('before'),after=await load('after');
const data=(at,target='A',patch={})=>({source:'ymcc-uniform',stale:false,values:{fps:120},coreUsage:[],coreLabels:[],coreClocks:[],frameTimes:[],fpsObservedAt:at,fpsTarget:'session|2077|'+target,freshUntil:{fps:at+5000},...patch});
function harness(m){let now=100000,id=0,sets=0,clears=0,maxTimers=0,hook=null;const timers=new Map(),callbacks=[],events=[];const gate=m.createHudFpsVisibilityGate({now:()=>now,emit:ready=>{events.push({at:now,ready});hook?.(ready);},setTimer(fn,ms){++sets;callbacks.push(fn);timers.set(++id,{at:now+ms,fn});maxTimers=Math.max(maxTimers,timers.size);return id;},clearTimer(id){++clears;timers.delete(id);}});return {gate,timers,callbacks,events,setNow(v){now=v;},setHook(v){hook=v;},advance(v){now=v;for(const [id,t]of [...timers])if(t.at<=now){timers.delete(id);t.fn();}},state:()=>({ready:gate.ready(),pending:gate.pending(),timers:timers.size,deadlines:[...timers.values()].map(t=>t.at),sets,clears,maxTimers,events:[...events]})};}
function scenario(m,kind){
 const h=harness(m);h.gate.accept(data(100000));
 const phase=kind.startsWith('entry-')?'entry':'change',action=kind.slice(phase.length+1);let triggered=false;
 if(phase==='change'){h.setNow(103000);h.gate.accept(data(103000));assert(h.gate.ready());}
 h.setHook(ready=>{if(triggered||ready!==(phase==='entry'))return;triggered=true;
  if(action==='dispose')h.gate.dispose();
  else if(action==='reset')h.gate.reset();
  else if(action==='zero')h.gate.accept(data(phase==='entry'?103000:103100,'C',{values:{fps:0}}));
  else if(action==='new-game'){const at=phase==='entry'?103000:103100;h.gate.accept(data(at,'C',{freshUntil:{fps:at+6000}}));}
  else throw Error('Unknown scenario');
 });
 const at=phase==='entry'?103000:103100;h.setNow(at);h.gate.accept(data(at,phase==='entry'?'A':'B'));
 assert(triggered);const post=h.state();
 if(action==='new-game'){h.setNow(at+3000);h.gate.accept(data(at+3000,'C'));}
 const qualified=h.state();h.gate.dispose();const closed=h.state();for(const fn of h.callbacks)fn();assert.deepEqual(h.state(),closed,'cancelled callback resurrected work');
 return {post,qualified,closed};
}
const names=['entry-dispose','entry-reset','entry-zero','entry-new-game','change-dispose','change-reset','change-zero','change-new-game'];
const failures={};
function correct(name,r){if(name.endsWith('new-game'))return r.post.deadlines[0]===(name.startsWith('entry')?109000:109100)&&r.qualified.ready&&r.post.sets===2;return r.post.timers===0&&!r.post.pending;}
for(const name of names){const r=scenario(before,name);assert.equal(correct(name,r),false,'Pre-change negative no longer reproduces: '+name);failures[name]=r;}
fs.writeFileSync(path.join(task,'BEFORE-REENTRANT-FAILURES.json'),JSON.stringify({reproduced:names.length,scenarios:failures,sourceSha256:pins.before},null,2));
if(process.argv.includes('--probe-before')){console.log(JSON.stringify({ok:true,reproduced:names.length,scope:'Pre-change counterexamples only'}));process.exit(0);}
const checks=[],traces={};
function check(name,fn){fn();checks.push(name);}
for(const name of names)check(name,()=>{const r=scenario(after,name);traces[name]=r;assert(correct(name,r),JSON.stringify(r));assert.equal(r.closed.timers,0);assert.equal(r.closed.pending,false);assert(r.post.maxTimers<=1);});
check('expiry notification can dispose, reset or accept a new game without losing its timer ownership',()=>{for(const action of ['dispose','reset','new-game']){const h=harness(after);h.gate.accept(data(100000));h.setNow(103000);h.gate.accept(data(103000));let once=false;h.setHook(ready=>{if(ready||once)return;once=true;if(action==='dispose')h.gate.dispose();else if(action==='reset')h.gate.reset();else h.gate.accept(data(108000,'C'));});h.advance(105000);h.advance(108000);assert(once);assert.equal(h.timers.size,action==='new-game'?1:0);h.gate.dispose();assert.equal(h.timers.size,0);}});
check('fixture preview notification can release the gate immediately without a timer',()=>{for(const action of ['dispose','reset']){const h=harness(after);h.setHook(ready=>{if(ready)h.gate[action]();});h.gate.accept(data(100000,'fixture',{source:'fixture'}));assert.equal(h.timers.size,0);h.gate.dispose();}});
check('normal 3s entrance, 5s genuine deadline and one-timer coalescing retain exact baseline behavior',()=>{
 const left=harness(before),right=harness(after);for(const dt of [0,1000,2000,2999,3000,3500]){for(const h of [left,right]){h.setNow(100000+dt);h.gate.accept(data(100000+dt));}assert.deepEqual(right.state(),left.state());assert.equal(right.gate.ready(),dt>=3000);}
 for(const at of [105000,108499,108500]){left.advance(at);right.advance(at);assert.deepEqual(right.state(),left.state());}assert.equal(right.gate.ready(),false);left.gate.dispose();right.gate.dispose();assert.equal(right.timers.size,0);
});
check('2000 ordinary mixed publications/clocks/invalid proofs preserve full baseline notifications and timer work',()=>{
 const a=harness(before),b=harness(after);let now=100000,seed=0x161027;const random=()=>{seed^=seed<<13;seed^=seed>>>17;seed^=seed<<5;return seed>>>0;};
 for(let i=0;i<2000;i++){now+=random()%1500;if(i%29===0)now-=3000;const d=data(now,String(random()%3));const n=random()%12;
  if(n===0)d.values.fps=0;if(n===1)d.values.fps=.9;if(n===2)d.values.fps=null;if(n===3)d.stale=true;if(n===4)d.fpsObservedAt=now+1;if(n===5)d.freshUntil.fps=now+7000;if(n===6)d.fpsTarget='';if(n===7)d.source='fixture';if(n===8)d.fpsObservedAt=now-500;
  const advance=i%3===0;for(const h of [a,b]){if(advance)h.advance(now);else h.setNow(now);h.gate.accept(d);}assert.deepEqual(b.state(),a.state(),'mixed publication '+i);assert(b.timers.size<=1);
 }
 a.gate.dispose();b.gate.dispose();assert.equal(a.timers.size,0);assert.equal(b.timers.size,0);for(const fn of b.callbacks)fn();assert.equal(b.timers.size,0);
});
assert.equal(crypto.createHash('sha256').update(fs.readFileSync('src/bridge/monitorHudFpsVisibility.ts')).digest('hex'),pins.after,'Production changed during verification');
const result={ok:true,cases:checks.length,checks,reproducedBefore:names.length,traces,sourcePins:pins,scope:'Actual frozen pre-change/current FPS gate, virtual clocks and synthetic display data. Callback timer-work/lifecycle counts only; no CPU percentage measurement, running app, hardware or game.'};fs.writeFileSync(path.join(task,'FPS-REENTRANT-RESULT.json'),JSON.stringify(result,null,2));console.log(JSON.stringify({...result,traces:undefined},null,2));
