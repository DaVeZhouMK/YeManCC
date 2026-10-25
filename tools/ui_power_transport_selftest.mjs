import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
const root=path.resolve(import.meta.dirname,'..');
const arg=process.argv.indexOf('--out-dir'),out=arg>=0?path.resolve(process.argv[arg+1]):null;
const {build}=createRequire(path.join(root,'package.json'))('esbuild');
const compiled=(await build({stdin:{contents:"import * as life from './src/bridge/uiLifecycle';import {powerLifecycle} from './src/bridge/api';export {life,powerLifecycle};",resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'node',format:'cjs',metafile:true}));
const flush=async()=>{for(let i=0;i<35;i++)await Promise.resolve();};
const ready=generation=>({phase:'ready',generation,inputReady:true,hardwareWritesAllowed:true});
async function fixture(){
 const window=new EventTarget(),document=new EventTarget();document.visibilityState='visible';const sent=[],held=[],timers=new Map();let nextTimer=0,receiver;
 const wire={state:ready(1),hold:false,deliver:data=>{assert.ok(receiver);receiver({data});},async release(){assert.ok(held.length);held.shift()();await flush();}};
 window.chrome={webview:{addEventListener(name,fn){assert.equal(name,'message');assert.equal(receiver,undefined);receiver=fn;},postMessage(message){assert.equal(message.cmd,'power.lifecycle','UI issued a control instead of a read');sent.push(structuredClone(message));const result=structuredClone(wire.state),reply=()=>wire.deliver({id:message.id,result});if(wire.hold){wire.hold=false;held.push(reply);}else reply();}}};
 const context=vm.createContext({window,document,CustomEvent,console,module:{exports:{}},setTimeout:(fn,ms)=>{const id=++nextTimer;timers.set(id,{fn,ms});return id;},clearTimeout:id=>timers.delete(id)});
 vm.runInContext(compiled.outputFiles[0].text,context);const {life,powerLifecycle}=context.module.exports;
 const reader=life.startUiPowerReconciliation(()=>powerLifecycle.get(1500));await flush();
 const event=async(name,data={})=>{wire.deliver({event:name,data});await flush();};
 return{wire,life,reader,event,sent,held,timers,flush};
}
const checks=[];async function test(name,fn){await fn();checks.push(name);console.log('PASS '+name);}
await test('actual api and ipc deliver native power snapshots and lifecycle events',async()=>{const f=await fixture();assert.equal(f.sent.length,1);assert.equal(f.life.getUiVisibilityState().powerGeneration,1);await f.event('power.suspending',{generation:2});assert.equal(f.life.isUiVisible(),false);f.wire.state=ready(2);await f.event('window.shown');assert.equal(f.life.isUiVisible(),true);assert.equal(f.timers.size,0);});
await test('WebView recovering rejection releases pending read; next show can recover',async()=>{const f=await fixture();await f.event('power.suspending',{generation:2});f.wire.state=ready(2);f.wire.hold=true;await f.event('window.shown');assert.equal(f.held.length,1);await f.event('webview.recovering',{reason:'fixture-renderer-recovery'});assert.equal(f.life.isUiVisible(),false);assert.equal(f.timers.size,0);await f.event('window.focus');assert.equal(f.life.isUiVisible(),true);await f.wire.release();assert.equal(f.life.isUiVisible(),true);});
await test('actual IPC timeout stays closed and next existing edge retries the read',async()=>{const f=await fixture();await f.event('power.suspending',{generation:2});f.wire.state=ready(2);f.wire.hold=true;await f.event('window.shown');assert.equal(f.timers.size,1);const [id,timer]=[...f.timers][0];assert.equal(timer.ms,1500);f.timers.delete(id);timer.fn();await f.flush();assert.equal(f.life.isUiVisible(),false);await f.event('window.focus');assert.equal(f.life.isUiVisible(),true);await f.wire.release();assert.equal(f.life.isUiVisible(),true);});
await test('late reply through actual message dispatcher cannot release next sleep',async()=>{const f=await fixture();await f.event('power.suspending',{generation:2});f.wire.state=ready(2);f.wire.hold=true;await f.event('window.shown');await f.event('power.suspending',{generation:3});await f.wire.release();assert.equal(f.life.isUiVisible(),false);assert.equal(f.life.getUiVisibilityState().powerGeneration,3);f.wire.state=ready(3);await f.event('window.shown');assert.equal(f.life.isUiVisible(),true);});
await test('untagged native resumed cannot fabricate Ready or native input admission',async()=>{const f=await fixture();await f.event('power.suspending',{generation:2});f.wire.state={...ready(2),phase:'suspended',inputReady:false,hardwareWritesAllowed:false};await f.event('power.resumed');assert.equal(f.life.isUiVisible(),false);f.wire.state={...ready(2),inputReady:false};await f.event('power.resumed');assert.equal(f.life.isUiVisible(),false);f.wire.state=ready(2);await f.event('power.resumed');assert.equal(f.life.isUiVisible(),true);});
await test('root disposal invalidates held IPC result and prevents subsequent native reads',async()=>{const f=await fixture();await f.event('power.suspending',{generation:2});f.wire.state=ready(2);f.wire.hold=true;await f.event('window.shown');f.reader.stop();await f.wire.release();assert.equal(f.life.isUiVisible(),false);const count=f.sent.length;await f.event('window.shown');assert.equal(f.sent.length,count);});
const hashes=Object.keys(compiled.metafile.inputs).filter(f=>f!=='<stdin>').map(f=>path.resolve(root,f)).filter(f=>fs.existsSync(f));const result={pass:true,scope:'Production uiLifecycle + api + ipc, inert WebView message boundary only; no installed application or hardware',cases:checks.length,checks,sourceHashes:Object.fromEntries(hashes.map(f=>[path.relative(root,f),createHash('sha256').update(fs.readFileSync(f)).digest('hex')]))};if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-power-transport.json'),JSON.stringify(result,null,2));}console.log(JSON.stringify(result));
