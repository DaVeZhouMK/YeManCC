// Executes the production UI lifecycle and its root-owned reader contract.
// Only the native read-only snapshot and DOM observations are inert.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
const root=path.resolve(import.meta.dirname,'..'),req=createRequire(path.join(root,'package.json'));
const {build}=req('esbuild');
const arg=process.argv.indexOf('--out-dir'),out=arg>=0?path.resolve(process.argv[arg+1]):null;
const source=fs.readFileSync(path.join(root,'src/bridge/uiLifecycle.ts'),'utf8');
const app=fs.readFileSync(path.join(root,'src/App.vue'),'utf8');
assert.match(app,/startUiPowerReconciliation\(\s*\(\) => powerLifecycle\.get\(1500\), lifecycleAtMountPromise/,'production App must attach the real bounded reader');
assert.match(app,/uiPowerReconciliation\?\.stop\(\)/,'production App must release its reader');
const compiled=(await build({entryPoints:[path.join(root,'src/bridge/uiLifecycle.ts')],bundle:true,write:false,platform:'node',format:'cjs'})).outputFiles[0].text;
const flush=async()=>{for(let i=0;i<30;i++)await Promise.resolve();};
const ready=generation=>({phase:'ready',generation,inputReady:true,hardwareWritesAllowed:true});
async function fixture(native=true){
 const window=new EventTarget(),document=new EventTarget();document.visibilityState='visible';const queue=[],calls=[];
 const port={state:ready(1),fail:false,hold:false,async read(){calls.push('power.lifecycle');if(this.fail){this.fail=false;throw Error('inert read failure');}const value=structuredClone(this.state);if(this.hold){this.hold=false;return new Promise(resolve=>queue.push(()=>resolve(value)));}return value;}};
 const context=vm.createContext({window,document,CustomEvent,console,module:{exports:{}}});vm.runInContext(compiled,context);const life=context.module.exports;
 const reader=native?life.startUiPowerReconciliation(()=>port.read()):null;
 const event=async(name,generation)=>{window.dispatchEvent(new CustomEvent(name,{detail:generation===undefined?{}:{generation}}));await flush();};
 const release=async()=>{assert.ok(queue.length,'expected a held read');queue.shift()();await flush();};await flush();return{life,window,document,port,calls,queue,event,release,reader};
}
const cases=[];async function test(name,fn){await fn();cases.push(name);console.log('PASS '+name);}
await test('root-owned native cold start reads once without control or wake',async()=>{const f=await fixture();assert.equal(f.life.isUiVisible(),true);assert.equal(f.calls.length,1);assert.equal(f.life.getUiVisibilityState().powerGeneration,1);});
await test('G3E missed final commit: shown window reads native committed Ready',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);assert.equal(f.life.isUiVisible(),false);f.port.state=ready(2);await f.event('ipc:window.shown');assert.equal(f.life.isUiVisible(),true);});
await test('late same-generation pending cannot undo a read-confirmed Ready',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state=ready(2);await f.event('ipc:power.resumed',2);const before=f.calls.length;for(const name of ['ipc:power.suspending','ipc:power.resuming'])await f.event(name,2);assert.equal(f.life.isUiVisible(),true);assert.equal(f.calls.length,before);});
await test('older resumed cannot reopen the next real sleep',async()=>{const f=await fixture();await f.event('ipc:power.suspending',3);await f.event('ipc:power.resumed',2);assert.equal(f.life.isUiVisible(),false);assert.equal(f.life.getUiVisibilityState().powerGeneration,3);});
for(const phase of ['suspending','suspended','resuming'])await test('window shown cannot invent Ready while native is '+phase,async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state={...ready(2),phase};await f.event('ipc:window.shown');assert.equal(f.life.isUiVisible(),false);});
await test('resume-ready is only a bounded native read trigger',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state={...ready(2),phase:'resuming'};await f.event('ipc:power.resume-ready',2);assert.equal(f.life.isUiVisible(),false);f.port.state=ready(2);await f.event('ipc:power.resume-ready',2);assert.equal(f.life.isUiVisible(),true);});
await test('held old Ready cannot release newer sleep',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state=ready(2);f.port.hold=true;await f.event('ipc:window.shown');await f.event('ipc:power.suspending',3);await f.release();assert.equal(f.life.isUiVisible(),false);assert.equal(f.life.getUiVisibilityState().powerGeneration,3);});
await test('held old Suspended cannot erase newer read-confirmed Ready',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state={...ready(2),phase:'suspended'};f.port.hold=true;await f.event('ipc:window.shown');f.port.state=ready(2);await f.event('ipc:power.resumed',2);await f.release();assert.equal(f.life.isUiVisible(),true);});
for(const value of [null,{}, {phase:'ready',generation:-1},{phase:'ready',generation:2.5},{phase:'bad',generation:2},{...ready(1)},{phase:'ready',generation:'2'}])await test('invalid or stale snapshot stays closed: '+JSON.stringify(value),async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state=value;await f.event('ipc:window.shown');assert.equal(f.life.isUiVisible(),false);});
for(const flag of ['inputReady','hardwareWritesAllowed'])await test('native Ready without '+flag+' cannot reopen the renderer',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state={...ready(2),[flag]:false};await f.event('ipc:power.resumed',2);assert.equal(f.life.isUiVisible(),false);f.port.state=ready(2);await f.event('ipc:window.focus');assert.equal(f.life.isUiVisible(),true);});
await test('failed native read waits for another existing edge',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.fail=true;await f.event('ipc:window.shown');assert.equal(f.life.isUiVisible(),false);const count=f.calls.length;await flush();assert.equal(f.calls.length,count);f.port.state=ready(2);await f.event('ipc:window.focus');assert.equal(f.life.isUiVisible(),true);});
await test('200 events coalesce with no resident polling',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state=ready(2);f.port.hold=true;await f.event('ipc:window.shown');for(let i=0;i<200;i++)await f.event('ipc:window.focus');assert.equal(f.calls.length,2);await f.release();assert.equal(f.life.isUiVisible(),true);assert.equal(f.calls.length,3);await flush();assert.equal(f.calls.length,3);});
await test('Ready reconciliation does not show a hidden native window',async()=>{const f=await fixture();await f.event('ipc:window.hidden');await f.event('ipc:power.suspending',2);f.port.state=ready(2);await f.event('ipc:power.resume-ready',2);assert.equal(f.life.getUiVisibilityState().powerReady,true);assert.equal(f.life.isUiVisible(),false);});
await test('visible document edge can recover missed final commit',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.document.visibilityState='hidden';f.document.dispatchEvent(new Event('visibilitychange'));f.port.state=ready(2);f.document.visibilityState='visible';f.document.dispatchEvent(new Event('visibilitychange'));await flush();assert.equal(f.life.isUiVisible(),true);});
await test('H5 legacy events work without a native reader',async()=>{const f=await fixture(false);await f.event('ipc:power.suspending');assert.equal(f.life.isUiVisible(),false);await f.event('ipc:power.resumed');assert.equal(f.life.isUiVisible(),true);assert.equal(f.calls.length,0);});
await test('managed legacy resumed cannot bypass native suspended state',async()=>{const f=await fixture();await f.event('ipc:power.suspending');f.port.state={...ready(2),phase:'suspended'};await f.event('ipc:power.resumed');assert.equal(f.life.isUiVisible(),false);});
await test('missing all transition notifications still reconciles a newer native generation',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state=ready(3);await f.event('ipc:window.shown');assert.equal(f.life.isUiVisible(),true);assert.equal(f.life.getUiVisibilityState().powerGeneration,3);});
await test('root unmount invalidates held read and removes reconciliation ownership',async()=>{const f=await fixture();await f.event('ipc:power.suspending',2);f.port.state=ready(2);f.port.hold=true;await f.event('ipc:window.shown');f.reader.stop();await f.release();assert.equal(f.life.isUiVisible(),false);const count=f.calls.length;await f.event('ipc:window.focus');assert.equal(f.calls.length,count);});
const sha=value=>createHash('sha256').update(value).digest('hex');
const result={pass:true,cases:cases.length,checks:cases,scope:'Production uiLifecycle plus actual App root-owned reader wiring; memory-only native read/DOM; no hardware or system sleep',sourceHashes:{'src/bridge/uiLifecycle.ts':sha(source),'src/App.vue':sha(app)}};
if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-power-recovery.json'),JSON.stringify(result,null,2));}console.log(JSON.stringify(result));
