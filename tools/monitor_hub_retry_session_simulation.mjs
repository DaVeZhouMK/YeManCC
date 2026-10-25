import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';

// Execute the production hub with deterministic IPC and timers. No native/file
// access, hardware collection, application settings or installed process changes.
const root=process.cwd(),require=createRequire(path.join(root,'package.json'));
const {build}=require('esbuild');
const out=path.resolve(process.env.YMCC_MONITOR_HUB_SIM_OUT||'../../../Build/Tasks/YMCC-Steam-HUD-Probe/cpu-data-audit-20261015/hub-retry-session');
fs.mkdirSync(out,{recursive:true});
const baseline=process.env.YMCC_MONITOR_HUB_SIM_BASELINE;
async function load(file){
 const plugins=file?[{name:'captured-hub',setup(b){b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]monitorData\.ts$/},()=>({contents:fs.readFileSync(file,'utf8'),loader:'ts',resolveDir:path.join(root,'src/bridge')}));}}]:[];
 const bundle=await build({stdin:{resolveDir:root,loader:'ts',contents:"export {createMonitorDataHub} from './src/bridge/monitorData.ts'"},bundle:true,write:false,platform:'node',format:'esm',plugins});
 return (await import('data:text/javascript;base64,'+Buffer.from(bundle.outputFiles[0].text).toString('base64'))).createMonitorDataHub;
}
const drain=async()=>{for(let i=0;i<40;i++)await Promise.resolve();};
const packet=(session,seq=1)=>({schema:1,available:false,session,generation:1,seq,topActive:false,fpsActive:false});
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}
function fixture(make){
 let now=100000,id=0;const calls=[],timers=new Map();let handler=async()=>({ok:false});
 const hub=make({now:()=>now,invoke:async(cmd,args)=>{calls.push({cmd,args,at:now});return handler(cmd,args);},readTextFile:async()=>{throw Error('Unexpected file fallback');},
  setTimer:(fn,ms)=>{const own=++id;timers.set(own,{fn,at:now+ms});return own;},clearTimer:key=>timers.delete(key)});
 return {hub,calls,timers,setHandler:fn=>handler=fn,now:()=>now,count:cmd=>calls.filter(c=>c.cmd===cmd).length,
  async advance(ms){const end=now+ms;for(let steps=0;steps<1000;steps++){const next=[...timers.entries()].sort((a,b)=>a[1].at-b[1].at)[0];if(!next||next[1].at>end){now=end;await drain();return;}now=next[1].at;timers.delete(next[0]);next[1].fn();await drain();}throw Error('Unbounded retry loop');}};
}
async function reproduce(make){
 const repeated=fixture(make);repeated.hub.acquire('top',{top:true});await drain();
 for(let i=0;i<1000;i++){repeated.hub.acquire('top',{top:true});await drain();}
 const replacement=fixture(make);replacement.hub.admitDisplayPublication(packet(1));const old=deferred();let starts=0;
 replacement.setHandler(cmd=>cmd==='monitor.start'&&++starts===1?old.promise:Promise.resolve({ok:false}));
 replacement.hub.acquire('top',{top:true});await drain();replacement.hub.admitDisplayPublication(packet(2));old.resolve({ok:true});await drain();
 return {repeatStartsAtSameTime:repeated.count('monitor.start'),repeatTimerCount:repeated.timers.size,
  replacementStarts:replacement.count('monitor.start'),replacementConfirmed:replacement.hub.applied().top};
}
const make=await load(),checks=[];
function check(label,fn){fn();checks.push(label);}
const after=await reproduce(make);
check('1000 repeated registrations keep one failed command and one retry timer',()=>{assert.equal(after.repeatStartsAtSameTime,1);assert.equal(after.repeatTimerCount,1);});
check('old publisher success cannot confirm a replacement whose command failed',()=>{assert.equal(after.replacementStarts,2);assert.equal(after.replacementConfirmed,false);});
let before=null;
if(baseline){before=await reproduce(await load(path.resolve(baseline)));check('captured source reproduces both historical failures',()=>{assert.equal(before.repeatStartsAtSameTime,6);assert.equal(before.replacementStarts,1);assert.equal(before.replacementConfirmed,true);});}
{
 const f=fixture(make),oldOff=f.hub.acquire('top',{top:true});await drain();const newOff=f.hub.acquire('top',{top:true});oldOff();await drain();
 check('old disposer cannot release replacement token',()=>{assert.equal(f.hub.desired().top,true);assert.equal(f.count('monitor.stop'),0);});
 await f.advance(999);assert.equal(f.count('monitor.start'),1);await f.advance(1);assert.equal(f.count('monitor.start'),2);
 for(let i=0;i<100;i++){f.hub.acquire('top',{top:true});await drain();}
 await f.advance(1999);assert.equal(f.count('monitor.start'),2);await f.advance(1);assert.equal(f.count('monitor.start'),3);
 checks.push('retry deadlines remain 1000ms then 2000ms despite repeated subscriptions');newOff();
}
for(const reply of ['success','refusal','exception'])for(const kind of ['start','stop']){
 const f=fixture(make);f.hub.admitDisplayPublication(packet(10));f.setHandler(async()=>({ok:true}));
 let release;
 if(kind==='stop'){release=f.hub.acquire('top',{top:true});await drain();}
 const old=deferred();let captured=false;
 f.setHandler(cmd=>{if(cmd==='monitor.'+kind&&!captured){captured=true;return old.promise;}return Promise.resolve({ok:true});});
 if(kind==='start')release=f.hub.acquire('top',{top:true});else release();await drain();
 f.hub.admitDisplayPublication(packet(11));f.hub.admitDisplayPublication(packet(12));
 f.setHandler(async()=>({ok:false}));
 if(kind==='stop')f.hub.acquire('top',{top:true});
 if(reply==='exception')old.reject(Error('old IPC timeout'));else old.resolve({ok:reply==='success'});
 await drain();
 check(`${kind} ${reply} from two sessions ago cannot confirm, suppress or delay new startup`,()=>{
  assert.equal(f.hub.session(),12);assert.equal(f.hub.applied().top,false);
  const starts=f.calls.filter(c=>c.cmd==='monitor.start');assert.equal(starts.length,2);assert.equal(starts.at(-1).at,100000);
  assert.equal(f.timers.size,1);assert.equal(f.hub.desired().top,true);
 });
}
{
 const f=fixture(make),old=deferred();f.setHandler(cmd=>cmd==='monitor.start'?old.promise:Promise.resolve({ok:true}));
 f.hub.acquire('top',{top:true});f.hub.admitDisplayPublication(packet(20));old.resolve({ok:true});await drain();
 check('first identity discovery preserves the initial in-flight command',()=>{assert.equal(f.count('monitor.start'),1);assert.equal(f.hub.applied().top,true);assert.equal(f.timers.size,0);});
}
{
 const f=fixture(make);f.setHandler(async()=>({ok:true}));f.hub.admitDisplayPublication(packet(30));
 const top=f.hub.acquire('top',{top:true});await drain();const fps=f.hub.acquire('fps',{fps:true});await drain();
 top();await drain();check('releasing top leaves independent FPS demand running',()=>{assert.deepEqual(f.hub.desired(),{top:false,fps:true});assert.deepEqual(f.hub.applied(),{top:false,fps:true});assert.deepEqual(f.calls.filter(c=>c.cmd==='monitor.stop').at(-1).args,{top:true,fps:false});});
 fps();await drain();check('last release converges and cancels the retry timer',()=>{assert.deepEqual(f.hub.applied(),{top:false,fps:false});assert.equal(f.timers.size,0);});
}
{
 const f=fixture(make);f.hub.acquire('top',{top:true});await drain();await f.advance(60000);
 check('start failures retain bounded six-command retry budget',()=>{assert.equal(f.count('monitor.start'),6);assert.equal(f.timers.size,0);});
 for(let i=0;i<200;i++){f.hub.acquire('top',{top:true});await drain();}
 assert.equal(f.count('monitor.start'),6);
 f.hub.acquire('top',{top:true,fps:true});await drain();
 check('only a changed requirement replenishes the exhausted budget',()=>{assert.equal(f.count('monitor.start'),7);assert.deepEqual(f.calls.at(-1).args,{top:true,fps:true});});
}
const result={ok:true,checks,before,after,scope:'actual production TypeScript factory with fake IPC/timers; operation counts, not CPU percentage or native-device verification'};
fs.writeFileSync(path.join(out,'hub-simulation-results.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));
