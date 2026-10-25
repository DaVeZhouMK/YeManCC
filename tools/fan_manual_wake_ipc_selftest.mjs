// Real Fan manual-wake IPC and lifecycle. Native power facts and Host/process
// operations are inert; all deadlines use a virtual monotonic clock.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { webcrypto, createHash } from 'node:crypto';
import { build } from 'esbuild';
const project=path.resolve(import.meta.dirname,'..'),out=path.resolve(process.argv[2]);
fs.mkdirSync(out,{recursive:true});
const beforeRoot=process.argv[3]?path.resolve(process.argv[3]):null;
const selected=p=>beforeRoot&&['api.ts','fanHost.ts'].includes(path.basename(p))?path.join(beforeRoot,path.basename(p).replace('.ts','.before.ts')):path.join(project,p);
const files=['src/bridge/api.ts','src/bridge/fanHost.ts','src/bridge/ipc.ts','tools/fixtures/fan_no_io_host.ts'];
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const binding=files.map(p=>({path:selected(p),sha256:hash(selected(p))}));
const bundle=await build({stdin:{contents:"export {powerLifecycle} from './src/bridge/api';export {FanHostLifecycle} from './src/bridge/fanHost';export {NoIoHost} from './tools/fixtures/fan_no_io_host';export {rejectAllPending} from './src/bridge/ipc';",resolveDir:project,sourcefile:'fan-manual-wake-ipc-entry.ts'},bundle:true,write:false,format:'iife',globalName:'FanTest',platform:'browser',target:'es2022',define:{'import.meta.env':'{}'},plugins:beforeRoot?[{name:'bound-before-image',setup(b){b.onLoad({filter:/[\\/]src[\\/]bridge[\\/](?:api|fanHost)\.ts$/},a=>({contents:fs.readFileSync(selected(path.relative(project,a.path)),'utf8'),loader:'ts',resolveDir:path.dirname(a.path)}));}}]:[]});
const code=bundle.outputFiles[0].text;
function fixture(mode='ack'){
 let now=0,id=0,listener,manuals=0,heldReply;
 const timers=new Map(),calls=[];
 let native={generation:3,phase:'ready',resumeReady:true,inputReady:true,hardwareWritesAllowed:true,hibernateAvailable:true};
 const timer=(fn,ms,interval=false)=>{const key=++id,delay=Math.max(1,Number(ms)||1);timers.set(key,{fn,due:now+delay,period:interval?delay:0});return key;};
 const clear=key=>timers.delete(key),win=new EventTarget();
 win.setTimeout=(fn,ms)=>timer(fn,ms);win.clearTimeout=clear;win.setInterval=(fn,ms)=>timer(fn,ms,true);win.clearInterval=clear;
 const deliver=(msg,result,error)=>listener?.({data:error?{id:msg.id,error}:{id:msg.id,result}});
 win.chrome={webview:{addEventListener:(_t,fn)=>listener=fn,postMessage(msg){calls.push(msg);
  if(msg.cmd==='fanLog.write'){queueMicrotask(()=>deliver(msg,{ok:true}));return;}
  if(msg.cmd==='power.fanLifecycle'){const result={...native};queueMicrotask(()=>deliver(msg,result));return;}
  if(msg.cmd==='power.fanManualWake'){
   manuals++;assert.equal(msg.args.generation,native.generation,'manual call must bind the current generation');
   const rejected=native.phase==='suspending'||mode==='reject';
   if(mode!=='missing-unexecuted'&&!rejected)native={...native,phase:'ready',resumeReady:true,hardwareWritesAllowed:true};
   const result={ok:!rejected,generation:native.generation,phase:native.phase,admitted:!rejected,reason:rejected?'suspend_in_progress':'manual_fan_wake'};
   if(mode.startsWith('missing')){heldReply=()=>deliver(msg,result);return;}
   queueMicrotask(()=>deliver(msg,result));return;
  }
  throw Error('unexpected IPC '+msg.cmd);
 }}};
 const context=vm.createContext({window:win,document:Object.assign(new EventTarget(),{hidden:false,visibilityState:'visible',createElement:()=>({style:{},content:{}}),querySelector:()=>null}),console,Event,EventTarget,
  CustomEvent:class extends Event{constructor(t,i){super(t);this.detail=i?.detail;}},setTimeout:win.setTimeout,clearTimeout:clear,setInterval:win.setInterval,clearInterval:clear,
  URL,TextEncoder,TextDecoder,crypto:webcrypto,performance:{now:()=>now},structuredClone,queueMicrotask});
 vm.runInContext(code,context,{timeout:5000});const api=context.FanTest;
 const observe=p=>{const result={status:'pending',value:undefined,error:null};void p.then(v=>{result.status='fulfilled';result.value=v;},e=>{result.status='rejected';result.error=String(e);});return result;};
 async function flush(){for(let n=0;n<80;n++)await Promise.resolve();await new Promise(setImmediate);}
 async function advance(ms){await flush();const target=now+ms;for(let n=0;n<1000;n++){const row=[...timers.entries()].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!row)break;const [key,t]=row;now=t.due;if(t.period)t.due+=t.period;else timers.delete(key);t.fn();await flush();}now=target;await flush();}
 let life,host,starts=0;
 const curve=[{tempC:0,dutyPercent:20},{tempC:40,dutyPercent:35},{tempC:70,dutyPercent:65},{tempC:100,dutyPercent:100}];
 async function boot(){host=new api.NoIoHost();host.generation=3;host.attempt=1;host.powerState='On';host.completeResume=true;
  life=new api.FanHostLifecycle({enabled:true,adapter:host,readNativeActivity:async()=>null,
   launcher:{start:async()=>({pid:111501+starts++,executable:'inert.exe'}),stop:async()=>{}},onControlDisplay(){}});
  life.setPowerGeneration(3);life.observePowerBoundary('resume-ready',3);await life.start();await life.apply(curve);
  native={...native,generation:4,phase:'suspended',resumeReady:false,hardwareWritesAllowed:false};
  life.setPowerGeneration(4);life.observePowerBoundary('suspending',4);await life.suspend();await host.suspend();host.generation=4;host.lease=null;host.calls=[];
 }
 async function begin(){const pending=observe(life.apply(curve));for(let n=0;n<20&&!calls.some(c=>c.cmd==='power.fanManualWake')&&pending.status==='pending';n++)await flush();return pending;}
 async function done(){const close=life?.close().catch(()=>{});api.rejectAllPending('fixture cleanup');await flush();if(close)await close;timers.clear();}
 return {api,calls,timers,flush,advance,observe,boot,begin,done,curve,get host(){return host;},get life(){return life;},get starts(){return starts;},get manuals(){return manuals;},get native(){return native;},setNative(v){native={...native,...v};},lateReply(){heldReply?.();}};
}
const results=[];
async function check(name,run){try{const facts=await run();results.push({name,ok:true,...(facts?{facts}:{})});console.log('PASS '+name);}catch(error){results.push({name,ok:false,error:String(error)});console.log('FAIL '+name+': '+error);}}
await check('manual-wake API retires a missing acknowledgement within three seconds',async()=>{const f=fixture('missing-committed');try{f.setNative({generation:4,phase:'suspended'});const p=f.observe(f.api.powerLifecycle.manualFanWake(4));await f.advance(2999);assert.equal(p.status,'pending');await f.advance(1);assert.equal(p.status,'rejected','missing manual wake acknowledgement never settled');assert.match(p.error,/IPC command timed out: power.fanManualWake/);assert.equal(f.manuals,1);assert.equal(f.timers.size,0);}finally{await f.done();}});
await check('same click observes a committed wake after lost reply and completes actual Open/Events/acquire/enable',async()=>{const f=fixture('missing-committed');try{await f.boot();const p=await f.begin();assert.equal(f.manuals,1);assert.equal(p.status,'pending');await f.advance(4001);assert.equal(p.status,'fulfilled',p.error??'same click remains blocked');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.host.calls.filter(c=>c==='acquire').length,1);assert.equal(f.manuals,1,'unknown native mutation is never blindly repeated');assert.equal(f.starts,1,'same exact Host owns the operation');f.lateReply();await f.flush();assert.equal(f.host.calls.filter(c=>c==='enable').length,1);return {manualRequests:f.manuals,starts:f.starts,trace:f.host.calls};}finally{await f.done();}});
await check('normal acknowledged rescue retains the same one-click path',async()=>{const f=fixture();try{await f.boot();const p=await f.begin();await f.flush();assert.equal(p.status,'fulfilled',p.error??'normal manual rescue pending');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.starts,1);}finally{await f.done();}});
await check('a lost unexecuted request cannot fabricate a ready/write fact',async()=>{const f=fixture('missing-unexecuted');try{await f.boot();const p=await f.begin();await f.advance(4001);const requestAt=f.calls.findIndex(c=>c.cmd==='power.fanManualWake');assert(requestAt>=0&&f.calls.slice(requestAt+1).some(c=>c.cmd==='power.fanLifecycle'),'native facts must be reread after the unknown result');assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'),'still-suspended native cannot grant writes');assert.equal(f.native.phase,'suspended');return {outcome:p.status,trace:f.host.calls};}finally{await f.done();}});
await check('OFF during a lost acknowledgement cancels the old write, and the next manual ON remains reachable',async()=>{const f=fixture('missing-committed');try{await f.boot();const p=await f.begin();const off=f.observe(f.life.disable());await f.advance(4001);assert.notEqual(p.status,'pending','old native observer still owns the queue');assert.equal(off.status,'fulfilled',off.error??'OFF still queued');assert(!f.host.calls.includes('enable'),'cancelled intent cannot write after timeout');f.lateReply();await f.flush();assert(!f.host.calls.includes('enable'),'late native success cannot revive OFF');const next=f.observe(f.life.apply(f.curve));await f.advance(4001);assert.equal(next.status,'fulfilled',next.error??'next manual control pending');assert.equal(f.host.calls.filter(c=>c==='enable').length,1);assert.equal(f.starts,1);}finally{await f.done();}});
await check('a new sleep generation retires the delayed old manual request',async()=>{const f=fixture('missing-committed');try{await f.boot();const p=await f.begin();f.setNative({generation:5,phase:'suspending',resumeReady:false,hardwareWritesAllowed:false});f.life.setPowerGeneration(5);f.life.observePowerBoundary('suspending',5);await f.advance(4001);assert.notEqual(p.status,'pending','old-generation IPC observer still owns the request');assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));f.lateReply();await f.flush();assert(!f.host.calls.includes('enable'));assert.equal(f.starts,1);}finally{await f.done();}});
await check('actual suspending native still rejects the same manual request with zero writes',async()=>{const f=fixture('reject');try{await f.boot();f.setNative({phase:'suspending'});const p=await f.begin();await f.advance(4001);assert(!f.host.calls.includes('acquire')&&!f.host.calls.includes('enable'));assert.equal(f.native.phase,'suspending');return {outcome:p.status,manualRequests:f.manuals};}finally{await f.done();}});
await check('idle HC Open admission rereads native after a missing promotion acknowledgement',async()=>{const f=fixture('missing-committed');try{await f.boot();f.life.observePowerBoundary('resume-ready',4);f.setNative({phase:'resuming',resumeReady:true});f.host.stateName='AwaitingControl';f.host.powerState='On';f.host.opened=false;f.host.events=false;f.host.lease=null;const probe={...f.host.snapshot(),powerOperationOutcome:'await-user'};const context=f.life.admissionContext('idle-open-ipc-check',f.life.controlIntentRevisionValue);context.manualRecovery=true;const p=f.observe(f.life.completeHcOpenForAdmission(probe,context));await f.flush();assert.equal(f.manuals,1);await f.advance(4001);assert.equal(p.status,'fulfilled',p.error??'idle HC Open still pending');assert.equal(f.host.calls.filter(c=>c==='open').length,1);assert.equal(f.host.calls.filter(c=>c==='events').length,1);assert(!f.host.calls.includes('enable')&&!f.host.calls.includes('acquire'),'Open admission alone cannot establish writes');}finally{await f.done();}});
await check('unbound session retirement reaches the original Close after a missing promotion acknowledgement',async()=>{const f=fixture('missing-committed');let release,previousClose,p;try{await f.boot();f.host.generation=0;f.host.attempt=0;f.host.stateName='Open';f.host.powerState='On';f.host.opened=true;f.host.events=false;f.host.lease=null;previousClose=f.host.close.bind(f.host);f.host.close=async()=>{f.host.calls.push('held-close-enter');await new Promise(r=>release=r);throw Error('fixture ends at real Close admission');};p=f.observe(f.life.restartUnboundHcSessionForManualControl({manualRecovery:true},true));await f.flush();assert.equal(f.manuals,1);await f.advance(4001);assert(f.host.calls.includes('held-close-enter'),p.error??'unbound retirement did not reach Close');assert.equal(f.starts,1,'no replacement until original owner has closed/exited');assert(!f.host.calls.includes('enable')&&!f.host.calls.includes('acquire'));}finally{if(previousClose)f.host.close=previousClose;if(release)release();if(p)await f.flush();await f.done();}});
for(const b of binding)assert.equal(hash(b.path),b.sha256,'source changed during gate: '+b.path);
const report={ok:results.every(r=>r.ok),checks:results.length,failed:results.filter(r=>!r.ok).length,hardwareCalls:0,cpuBenchmark:false,scope:'actual default Fan wake API/shared IPC/public lifecycle with inert native power facts, process and Host; no real native operation, HC, sleep or hardware',sourceBinding:binding,results};
fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({ok:report.ok,checks:report.checks,failed:report.failed,scope:report.scope}));process.exitCode=report.ok?0:1;
