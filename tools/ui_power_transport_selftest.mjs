import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createHash} from 'node:crypto';
import {observeUiPowerBuilds} from './fixtures/ui_power_build_receipt.mjs';
const root=path.resolve(import.meta.dirname,'..');
const arg=process.argv.indexOf('--out-dir'),out=arg>=0?path.resolve(process.argv[arg+1]):null;
const scopeArg=process.argv.indexOf('--scope');
if(scopeArg>=0){
 const args=process.argv.slice(2);
 assert(args.length===4&&args.filter(x=>x==='--scope').length===1&&args.filter(x=>x==='--out-dir').length===1&&out&&
  process.argv[scopeArg+1]==='monitor-notice-recovery','Expected --scope monitor-notice-recovery --out-dir <new directory>');
 const {runMonitorNoticeRecovery}=await import('./fixtures/ui_power_transport_notice.mjs');
 process.exitCode=await runMonitorNoticeRecovery(root,out);
}else{
const {build,receipt}=observeUiPowerBuilds(root);
const compiled=(await build({stdin:{contents:"import * as life from './src/bridge/uiLifecycle';import {powerLifecycle} from './src/bridge/api';import {getSharedPowerLifecycle} from './src/bridge/powerLifecycleState';export {life,powerLifecycle,getSharedPowerLifecycle};",resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'node',format:'cjs',metafile:true}));
const flush=async()=>{for(let i=0;i<35;i++)await Promise.resolve();};
const ready=generation=>({known:true,phase:'ready',generation,publicationRevision:generation*10+1,inputReady:true,hardwareWritesAllowed:true});
let activeFixture=null;
async function fixture(){
 assert.equal(activeFixture,null);
 const window=new EventTarget(),document=new EventTarget();document.visibilityState='visible';const sent=[],held=[],timers=new Map();let nextTimer=0,receiver;
 const wire={state:ready(1),hold:false,deliver:data=>{assert.ok(receiver);receiver({data});},async release(){assert.ok(held.length);held.shift()();await flush();}};
 window.chrome={webview:{addEventListener(name,fn){assert.equal(name,'message');assert.equal(receiver,undefined);receiver=fn;},postMessage(message){assert.equal(message.cmd,'power.lifecycle','UI issued a control instead of a read');sent.push(structuredClone(message));const result=structuredClone(wire.state),reply=()=>wire.deliver({id:message.id,result});if(wire.hold){wire.hold=false;held.push(reply);}else reply();}}};
 const context=vm.createContext({window,document,CustomEvent,console,queueMicrotask,structuredClone,module:{exports:{}},setTimeout:(fn,ms)=>{const id=++nextTimer;timers.set(id,{fn,ms});return id;},clearTimeout:id=>timers.delete(id)});
 vm.runInContext(compiled.outputFiles[0].text,context);const {life,powerLifecycle,getSharedPowerLifecycle}=context.module.exports;
 let legacyCalls=0;
 const reader=life.startUiPowerReconciliation(()=>{legacyCalls++;throw Error('Obsolete UI power reader must not run');});
 const event=async(name,data={})=>{wire.deliver({event:name,data});await flush();};
 const suspend=async generation=>{
  wire.state={...ready(generation),phase:'suspended',publicationRevision:generation*10,inputReady:false,hardwareWritesAllowed:false};
  await event('power.suspending',{generation});assert.equal(life.isUiVisible(),false);assert.equal(life.getUiVisibilityState().powerGeneration,generation);
 };
 const fire=async ms=>{assert.deepEqual([...timers.values()].map(t=>t.ms),[ms]);const [id,timer]=timers.entries().next().value;timers.delete(id);timer.fn();await flush();};
 const hintWithBackoff=async(name,ms)=>{const count=sent.length;await event(name);assert.equal(sent.length,count,'a hint cannot bypass the shared retry timer');await fire(ms);assert.equal(sent.length,count+1,'the armed timer starts exactly one read');};
 const cleanup=async()=>{reader.stop();while(held.length)await wire.release();await flush();assert.equal(timers.size,0,'no pending IPC timeout or shared retry survives disposal');assert.equal(legacyCalls,0);};
 activeFixture={wire,life,powerLifecycle,getSharedPowerLifecycle,reader,event,suspend,fire,hintWithBackoff,sent,held,timers,cleanup};
 await flush();return activeFixture;
}
const checks=[];
async function test(name,fn){
 const errors=[];
 try{await fn();}catch(error){errors.push(error);}
 if(activeFixture){const value=activeFixture;activeFixture=null;try{await value.cleanup();}catch(error){errors.push(error);}}
 if(errors.length===1)throw errors[0];
 if(errors.length>1)throw new AggregateError(errors,name+' failed both behavior and cleanup');
 checks.push(name);console.log('PASS '+name);
}
async function holdReady(f,generation){f.wire.state=ready(generation);f.wire.hold=true;await f.hintWithBackoff('window.shown',1000);assert.equal(f.held.length,1);assert.deepEqual([...f.timers.values()].map(t=>t.ms),[3000]);}
await test('actual api and ipc deliver native power snapshots and lifecycle events',async()=>{
 const f=await fixture();assert.equal(f.sent.length,1);assert.equal(f.life.getUiVisibilityState().powerGeneration,1);
 assert.deepEqual(await f.powerLifecycle.get(),ready(1));assert.equal(f.sent.length,2);
 await f.suspend(2);f.wire.state=ready(2);await f.hintWithBackoff('window.shown',1000);assert.equal(f.life.isUiVisible(),true);assert.equal(f.timers.size,0);
});
await test('WebView recovering rejection releases pending read; next show can recover',async()=>{
 const f=await fixture();await f.suspend(2);await holdReady(f,2);await f.event('webview.recovering',{reason:'fixture-renderer-recovery'});
 assert.equal(f.life.isUiVisible(),false);await f.hintWithBackoff('window.focus',2000);assert.equal(f.life.isUiVisible(),true);await f.wire.release();assert.equal(f.life.isUiVisible(),true);
});
await test('actual IPC timeout stays closed and next existing edge retries the read',async()=>{
 const f=await fixture();await f.suspend(2);await holdReady(f,2);await f.fire(3000);
 assert.equal(f.life.isUiVisible(),false);await f.hintWithBackoff('window.focus',2000);assert.equal(f.life.isUiVisible(),true);await f.wire.release();assert.equal(f.life.isUiVisible(),true);
});
await test('late reply through actual message dispatcher cannot release next sleep',async()=>{
 const f=await fixture();await f.suspend(2);await holdReady(f,2);await f.suspend(3);await f.wire.release();
 assert.equal(f.life.isUiVisible(),false);assert.equal(f.life.getUiVisibilityState().powerGeneration,3);f.wire.state=ready(3);await f.hintWithBackoff('window.shown',2000);assert.equal(f.life.isUiVisible(),true);assert.equal(f.life.getUiVisibilityState().powerGeneration,3);
});
await test('untagged native resumed cannot fabricate Ready or native input admission',async()=>{
 const f=await fixture();await f.suspend(2);await f.hintWithBackoff('power.resumed',1000);assert.equal(f.life.isUiVisible(),false);
 const displayOnly={...ready(2),inputReady:false,hardwareWritesAllowed:false};f.wire.state=displayOnly;
 await f.hintWithBackoff('power.resumed',2000);assert.equal(f.life.isUiVisible(),true,'confirmed system Ready can display the UI');
 assert.equal(f.getSharedPowerLifecycle().inputReady,false);assert.equal(f.getSharedPowerLifecycle().hardwareWritesAllowed,false);
 f.wire.state=ready(2);await f.hintWithBackoff('power.resumed',5000);assert.equal(f.getSharedPowerLifecycle().inputReady,true);assert.equal(f.getSharedPowerLifecycle().hardwareWritesAllowed,true);
});
await test('root disposal invalidates held IPC result and prevents subsequent native reads',async()=>{
 const f=await fixture();await f.suspend(2);await holdReady(f,2);f.reader.stop();await f.wire.release();assert.equal(f.life.isUiVisible(),false);
 const count=f.sent.length;await f.event('window.shown');await f.event('window.focus');assert.equal(f.sent.length,count);assert.equal(f.timers.size,0);
});
const hashes=Object.keys(compiled.metafile.inputs).filter(f=>f!=='<stdin>').map(f=>path.resolve(root,f));const result={pass:true,scope:'Production uiLifecycle + api + ipc, inert WebView message boundary only; no installed application or hardware',cases:checks.length,checks,sourceHashes:Object.fromEntries(hashes.map(f=>[path.relative(root,f),createHash('sha256').update(fs.readFileSync(f)).digest('hex')])),observedBuilds:receipt()};if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-power-transport.json'),JSON.stringify(result,null,2),{flag:'wx'});}console.log(JSON.stringify(result));
}
