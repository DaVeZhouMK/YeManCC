// Regression audit: exact production modules, deterministic memory IO only.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {root,task} from './fixtures/decky_task_paths.mjs';import {load} from './fixtures/decky_game_memory_store.mjs';
const power=load(fs.readFileSync(path.join(root,'decky-plugin/src/powerChoiceDebounce.ts'),'utf8'));
const monitor=load(fs.readFileSync(path.join(root,'src/bridge/deckyMonitorActions.ts'),'utf8'));
const steam=load(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8'));
const relayModule=load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':steam});
const display=load(fs.readFileSync(path.join(root,'src/bridge/deckyFanDisplay.ts'),'utf8'));
const deferred=()=>{let resolve;const promise=new Promise(done=>resolve=done);return {promise,resolve};};
const flush=async()=>{for(let i=0;i<100;i++)await Promise.resolve();};const results=[];
async function check(name,action){try{await action();results.push({name,status:'passed'});}catch(error){results.push({name,status:'failed',error:String(error.message)});}}
await check('unrelated revision during power debounce keeps the original deadline and sends the newest authoritative snapshot',()=>{
 let now=0,id=0;const timers=new Map(),sent=[];
 const choices=['balanced','performance'].map(data=>({data,label:data}));const state={connected:true,busy:false,snapshot:{runId:'r',generation:1,revision:1,ready:true,powerSource:'ac',actions:{game:true},game:{identity:'same-game',fields:{acMode:{value:'balanced',supported:true,choices}}}}};
 const queue=new power.PowerChoiceDebounce({state:()=>state,send:choice=>{sent.push(structuredClone(choice));return Promise.resolve();},schedule:(action,delay)=>{timers.set(++id,{action,due:now+delay});return id;},cancel:handle=>timers.delete(handle)});
 queue.choose('acMode','performance',state.snapshot);now=2000;state.snapshot={...structuredClone(state.snapshot),revision:2,notice:'Fan ready'};queue.observe(state);
 assert.equal(timers.size,1);now=2999;assert.equal(sent.length,0);now=3000;for(const [handle,timer]of [...timers])if(timer.due<=now){timers.delete(handle);timer.action();}
 assert.equal(sent.length,1);assert.equal(sent[0].snapshot.revision,2);assert.equal(sent[0].value,'performance');queue.dispose();
});
await check('monitor switch never overwrites appearance saved during its power admission wait',async()=>{
 let current={enabled:true,output:'ymcc',ymcc:{opacity:.86,scale:1}},calls=0,applies=0,savedArgument;const gate=deferred();
 const action=monitor.createMonitorMirrorActions({allowed:()=>true,power:async()=>{if(++calls===2)await gate.promise;return {generation:1,phase:'ready',hardwareWritesAllowed:true};},read:async()=>structuredClone(current),save:async value=>{savedArgument=value;current=typeof value==='boolean'?{...current,enabled:value}:structuredClone(value);return structuredClone(current);},apply:async prefs=>{applies++;assert.equal(prefs.ymcc.opacity,.31);return {ok:true,applied:'off',phase:'done',reason:'inert ACK'};}});
 const request=action.execute({enabled:false},{generation:1,revision:1,controlAdmission:{monitor:JSON.stringify(current)},checkpoint(){}});await flush();current.ymcc.opacity=.31;gate.resolve();const result=await request;
 assert.equal(current.ymcc.opacity,.31);assert.equal(savedArgument,false);assert.equal(result.applied,true);assert.equal(applies,1);
});
await check('closing QAM after monitor commit does not strand accepted display intent',async()=>{
 let current={enabled:true,output:'ymcc',ymcc:{opacity:.86}},live=true,applies=0;
 const action=monitor.createMonitorMirrorActions({allowed:()=>true,power:async()=>({generation:1,phase:'ready',hardwareWritesAllowed:true}),read:async()=>structuredClone(current),save:async value=>{current=typeof value==='boolean'?{...current,enabled:value}:structuredClone(value);live=false;return structuredClone(current);},apply:async()=>{applies++;return {ok:true,applied:'off',phase:'done',reason:'inert ACK'};}});
 const result=await action.execute({enabled:false},{generation:1,revision:1,controlAdmission:{monitor:JSON.stringify(current)},checkpoint(){if(!live)throw Error('QAM disconnected');}});
 assert.equal(result.saved,true);assert.equal(result.applied,true);assert.equal(applies,1);
});
await check('monitor display exception acknowledges saved intent as pending',async()=>{
 let current={enabled:true,output:'ymcc',ymcc:{opacity:.86}};
 const action=monitor.createMonitorMirrorActions({allowed:()=>true,power:async()=>({generation:1,phase:'ready',hardwareWritesAllowed:true}),read:async()=>structuredClone(current),save:async value=>{current=typeof value==='boolean'?{...current,enabled:value}:structuredClone(value);return structuredClone(current);},apply:async()=>{throw Error('Inert display unavailable');}});
 const result=await action.execute({enabled:false},{generation:1,revision:1,controlAdmission:{monitor:JSON.stringify(current)},checkpoint(){}});
 assert.equal(result.saved,true);assert.equal(result.applied,false);assert.equal(result.pending,true);assert.match(result.notice,/Inert display unavailable/);
});
await check('concurrent snapshot demand shares one source read and one delivery rather than duplicate frames',async()=>{
 const gate=deferred(),runId='a'.repeat(32),peer={connection:'1',runId};let reads=0;const replies=[];
 const relay=new relayModule.ReadOnlyMirrorRelay({read:async()=>{reads++;await gate.promise;return {generation:1,ready:true,game:null,fan:{supported:false,enabled:false,preset:'',choices:[]}};},observe:()=>()=>{},reply:async(_connection,message)=>{replies.push(message);return true;}});
 const demand=Array.from({length:10},()=>relay.request({...peer,request:{command:'snapshot',runId}}));await flush();assert.equal(reads,1);gate.resolve();await Promise.all(demand);assert.equal(replies.length,1);
 await relay.request({...peer,request:{command:'snapshot',runId}});assert.equal(replies.length,2);assert.equal(reads,1);relay.dispose();
});
await check('identical fan display notifications do not refresh the entire sidebar',()=>{
 let deliveries=0;const stop=display.onFanMirrorDisplay(()=>deliveries++),value={preset:'balanced',active:true,pending:true,notice:'Waiting for owner'};
 display.observeFanMirrorDisplay(value);for(let i=0;i<20;i++)display.observeFanMirrorDisplay({...value});assert.equal(deliveries,1);
 display.observeFanMirrorDisplay({...value,pending:false});assert.equal(deliveries,2);stop();
});
const baseline=process.argv.includes('--baseline');fs.writeFileSync(path.join(task,'validation',baseline?'AUDIT-REGRESSION-BASELINE.json':'AUDIT-REGRESSION-FIXED.json'),JSON.stringify({results,cases:results.length,actualSteam:false,realConfigWrites:0,hardwareWrites:0},null,2));
for(const result of results)console.log(result.status+' '+result.name+(result.error?' | '+result.error:''));if(results.some(result=>result.status==='failed'))process.exitCode=1;
