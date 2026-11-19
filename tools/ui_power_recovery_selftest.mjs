// Executes the actual production UI consumer AND the shared native source in one VM.
// Only ipc.invoke/on, DOM and the logical clock are inert. No product reader is replaced.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import {runSharedPowerCases} from './fixtures/ui_shared_power_cases.mjs';
import {observeUiPowerBuilds} from './fixtures/ui_power_build_receipt.mjs';
const root=path.resolve(import.meta.dirname,'..'),req=createRequire(path.join(root,'package.json'));
let out=null,uiOnly=false,hardeningOnly=false,readyAdmissionOnly=false,diagnosticsOnly=false,trainerSequenceOnly=false,trainerSequenceCase=null,notificationScopeOnly=false;
for(let i=2;i<process.argv.length;i++){
 const value=process.argv[i];
 if(value==='--ui-consumers-only'){if(uiOnly)throw Error('Duplicate/combined UI selection');uiOnly=true;}
 else if(value==='--diagnostics-only'){if(diagnosticsOnly)throw Error('Duplicate diagnostics selection');diagnosticsOnly=true;}
 else if(value==='--ready-admission-only'){if(readyAdmissionOnly)throw Error('Duplicate Ready admission selection');readyAdmissionOnly=true;}
 else if(value==='--hardening-only'){if(hardeningOnly)throw Error('Duplicate hardening selection');hardeningOnly=true;}
 else if(value==='--notification-scope-only'){if(notificationScopeOnly)throw Error('Duplicate notification scope selection');notificationScopeOnly=true;}
 else if(value==='--trainer-sequence-only'){if(trainerSequenceOnly)throw Error('Duplicate trainer sequence selection');trainerSequenceOnly=true;}
 else if(value==='--trainer-sequence-case'){if(trainerSequenceCase!==null||!process.argv[i+1])throw Error('One trainer sequence case required');trainerSequenceCase=process.argv[++i];}
 else if(value==='--out-dir'){
  if(out!==null||!process.argv[i+1]||process.argv[i+1].startsWith('--'))throw Error('Exactly one --out-dir value is required');
  out=path.resolve(process.argv[++i]);
 }else throw Error('Unknown/unsupported selection or option: '+value);
}
assert(!trainerSequenceCase||trainerSequenceOnly,'Trainer sequence case requires its scope');
assert(!trainerSequenceOnly||!uiOnly&&!hardeningOnly&&!readyAdmissionOnly&&!diagnosticsOnly,'Selections are mutually exclusive');
assert(trainerSequenceCase===null||['scope','read-newer','read-missing','held-launch','held-recovery','cycles'].includes(trainerSequenceCase),'Unknown trainer notification case');
assert(!notificationScopeOnly||!trainerSequenceOnly&&!uiOnly&&!hardeningOnly&&!readyAdmissionOnly&&!diagnosticsOnly,'Selections are mutually exclusive');
const {build:sourceBuild,receipt}=observeUiPowerBuilds(root);let publicGraphSerial=0;
const build=async options=>{
 if(!process.env.YMCC_PUBLIC_POWER_GRAPH_DIR)return sourceBuild(options);
 const substitutions=[];
 const plugins=(options.plugins??[]).map(plugin=>({...plugin,setup(engine){return plugin.setup({...engine,onLoad(settings,callback){engine.onLoad(settings,async args=>{const result=await callback(args);if(result?.contents!==undefined)substitutions.push({path:args.path,namespace:args.namespace,contents:typeof result.contents==='string'?result.contents:Buffer.from(result.contents).toString('utf8')});return result;});}});}}));
 const result=await sourceBuild({...options,metafile:true,plugins});const directory=process.env.YMCC_PUBLIC_POWER_GRAPH_DIR;fs.mkdirSync(directory,{recursive:true});const stem=path.join(directory,String(++publicGraphSerial).padStart(4,'0'));
 fs.writeFileSync(stem+'.cjs',result.outputFiles[0].text,{flag:'wx'});fs.writeFileSync(stem+'.metafile.json',JSON.stringify(result.metafile,null,2),{flag:'wx'});fs.writeFileSync(stem+'.ports.json',JSON.stringify({substitutions},null,2),{flag:'wx'});return result;
};
if(notificationScopeOnly){const checks=[];const coverage=await runSharedPowerCases({root,build,selection:'notification-scope',test:async(name,fn)=>{await fn();checks.push(name);}});const result={pass:true,checks,coverage};if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-notification-scope.json'),JSON.stringify(result,null,2),{flag:'wx'});}console.log(JSON.stringify(result));process.exit(0);}
if(trainerSequenceOnly){
 const checks=[],started=performance.now();
 const {runTrainerNotificationSequenceCases}=await import('./fixtures/game_trainer_recovery_fixture.mjs');
 try{const coverage=await runTrainerNotificationSequenceCases({caseOnly:trainerSequenceCase,test:async(name,fn)=>{await fn();checks.push(name);}});const result={pass:true,selection:'trainer-sequence-only',checks,coverage,elapsedMs:performance.now()-started};if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-trainer-sequence.json'),JSON.stringify(result,null,2),{flag:'wx'});}console.log(JSON.stringify(result));}
 catch(error){if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-trainer-sequence-failure.json'),JSON.stringify({pass:false,checks,error:String(error),stack:error.stack},null,2),{flag:'wx'});}throw error;}
 process.exit(0);
}
if(diagnosticsOnly){
 assert(!uiOnly&&!hardeningOnly&&!readyAdmissionOnly,'Selections are mutually exclusive');
 const started=performance.now(),checks=[];const test=async(name,fn)=>{await fn();checks.push(name);console.log('PASS '+name);};
 try{
  const coverage=await runSharedPowerCases({root,build,test,selection:'diagnostics'});
  const source=fs.readFileSync(path.join(root,'src/bridge/powerLifecycleState.ts'),'utf8');const needle='function getSharedPowerLifecycleDiagnostics() {';assert.equal(source.split(needle).length,2);
  let rejection;try{await runSharedPowerCases({root,build,selection:'diagnostics',powerSource:source.replace(needle,needle+' if(active())deps.read();'),test:async(_name,fn)=>fn()});}catch(error){rejection=error;}
  assert.equal(rejection?.code,'ERR_ASSERTION','impure getter must be rejected by an actual observation');assert.match(rejection.message,/diagnostics getter must not trigger read/);assert.equal(checks.length,2);
  const result={pass:true,selection:'diagnostics-only',checks,negative:{rejected:true,assertion:rejection.message},coverage,old14Executed:0,old27Executed:0,old6Executed:0,elapsedMs:performance.now()-started};
  if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-power-diagnostics.json'),JSON.stringify(result,null,2),{flag:'wx'});}console.log(JSON.stringify(result));
 }catch(error){if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-power-diagnostics-failure.json'),JSON.stringify({pass:false,checks,error:String(error),stack:error.stack,elapsedMs:performance.now()-started},null,2),{flag:'wx'});}throw error;}
 process.exit(0);
}
if(hardeningOnly||readyAdmissionOnly){
 assert(!uiOnly&&!(hardeningOnly&&readyAdmissionOnly),'Selections are mutually exclusive');
 const started=performance.now(),checks=[],negatives=[];
 const inputs=['src/bridge/powerLifecycleState.ts','src/bridge/trainerLifecycle.ts','src/bridge/gameTrainerAuto.ts','src/bridge/gameTrainerRecovery.ts','src/bridge/gameTrainer.ts','tools/fixtures/ui_shared_power_cases.mjs','tools/fixtures/game_trainer_recovery_fixture.mjs','tools/fixtures/trainer_ready_tmnt_20261022.json','tools/ui_power_recovery_selftest.mjs','package.json'];
 const pin=()=>Object.fromEntries(inputs.map(file=>[file,createHash('sha256').update(fs.readFileSync(path.join(root,file))).digest('hex')]));
 const before=pin();const test=async(name,fn)=>{await fn();checks.push(name);console.log('PASS '+name);};
 const {runTrainerPowerHardeningCases}=await import('./fixtures/game_trainer_recovery_fixture.mjs');
 if(readyAdmissionOnly){
  const {runTrainerReadyAdmissionCase}=await import('./fixtures/game_trainer_recovery_fixture.mjs');
  try{
   await runSharedPowerCases({root,build,test,selection:'ready-admission'});
   await runTrainerReadyAdmissionCase({test});
   const source=fs.readFileSync(path.join(root,'src/bridge/powerLifecycleState.ts'),'utf8');
   const gate="state.hardwareWritesAllowed===true&&state.notificationPending!==true";
   assert.equal(source.split(gate).length,2,'one public admission predicate');
   let rejection;try{await runSharedPowerCases({root,build,selection:'ready-admission',powerSource:source.replace(gate,'true'),test:async(_name,fn)=>fn()});}catch(error){rejection=error;}
   assert.equal(rejection?.code,'ERR_ASSERTION','old phase-only gate must fail a behavioral assertion');
   assert.match(rejection.message,/non-admitted Ready must retain exactly one retry/);
   negatives.push({name:'old phase-only Ready admission',rejected:true,assertion:rejection.message});
   assert.equal(checks.length,2);assert.deepEqual(pin(),before,'candidate drift');
   const result={pass:true,selection:'ready-admission-only',checks,negatives,oldSixExecuted:0,oldTwoNegativesExecuted:0,legacy14Executed:0,legacy27Executed:0,sourceHashes:before,elapsedMs:performance.now()-started};
   if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-ready-admission.json'),JSON.stringify(result,null,2),{flag:'wx'});}console.log(JSON.stringify(result));
  }catch(error){if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-ready-admission-failure.json'),JSON.stringify({pass:false,checks,negatives,sourceHashes:before,error:String(error),stack:error.stack},null,2),{flag:'wx'});}throw error;}
  process.exit(0);
 }
 try{
  await runSharedPowerCases({root,build,test,selection:'hardening'});
  await runTrainerPowerHardeningCases({test});
  const expectRejected=async(name,run,expected)=>{let error;try{await run();}catch(e){error=e;}assert.equal(error?.code,'ERR_ASSERTION',name+' must fail a behavioral assertion');assert.match(error.message,expected);negatives.push({name,rejected:true,assertion:error.message});console.log('EXPECTED_REJECTION '+name);};
  const source=fs.readFileSync(path.join(root,'src/bridge/powerLifecycleState.ts'),'utf8');
  const needle="if(admitted&&(!event||freshFact)){";assert(source.includes(needle));
  await expectRejected('settle nonReady reconciliation demand as Ready',()=>runSharedPowerCases({root,build,selection:'hardening',powerSource:source.replace(needle,'if(!event||freshFact){'),test:async(_name,fn)=>fn()}),/known nonReady must retain exactly one retry/);
  const trainer=fs.readFileSync(path.join(root,'src/bridge/trainerLifecycle.ts'),'utf8');
  const guard='if (recoveryPending) return; // Only this recovery\'s fresh read may resume trainer work.';assert(trainer.includes(guard));
  await expectRejected('remove trainer fresh-read gate',()=>runTrainerPowerHardeningCases({lifecycleSource:trainer.replace(guard,''),test:async(name,fn)=>{if(name.includes('publication during recovery'))await fn();}}),/publication cannot resume Auto before authoritative read/);
  assert.deepEqual(pin(),before,'source drift during directed execution');assert.equal(checks.length,6);assert.equal(negatives.length,2);
  const result={pass:true,selection:'hardening-only',checks,negatives,legacy14Executed:0,legacy27Executed:0,sourceHashes:before,elapsedMs:performance.now()-started,scope:'Actual shared source, trainer lifecycle, Auto and Recovery; inert IPC/files/DOM/logical time; no system or product operations'};
  if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-power-hardening.json'),JSON.stringify(result,null,2),{flag:'wx'});}console.log(JSON.stringify(result));
 }catch(error){if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-power-hardening-failure.json'),JSON.stringify({pass:false,checks,negatives,sourceHashes:before,elapsedMs:performance.now()-started,error:String(error),stack:error.stack},null,2),{flag:'wx'});}throw error;}
 process.exit(0);
}
const source=fs.readFileSync(path.join(root,'src/bridge/uiLifecycle.ts'),'utf8');
const app=fs.readFileSync(path.join(root,'src/App.vue'),'utf8');
const sharedBytes=fs.readFileSync(path.join(root,'src/bridge/powerLifecycleState.ts'));
assert.match(app,/startUiPowerReconciliation\(\s*\)/,'production App attaches the shared-source UI projection');
assert.match(app,/uiPowerReconciliation\?\.stop\(\)/,'production App releases its projection');
const compiled=(await build({stdin:{contents:"export * from './src/bridge/uiLifecycle'; export {getSharedPowerLifecycle} from './src/bridge/powerLifecycleState';",resolveDir:root,loader:'ts'},bundle:true,write:false,platform:'node',format:'cjs',plugins:[{
 name:'ui-inert-ipc-leaf',setup(b){b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]ipc\.ts$/},()=>({loader:'ts',contents:'export const invoke=(...args)=>globalThis.__uiNative.invoke(...args); export const on=(...args)=>globalThis.__uiNative.on(...args);'}));}
}]})).outputFiles[0].text;
const flush=async()=>{for(let i=0;i<45;i++)await Promise.resolve();};
const ready=generation=>({known:true,phase:'ready',generation,publicationRevision:generation*10,inputReady:true,hardwareWritesAllowed:true});
let activeFixture=null;
async function fixture(native=true){
 assert.equal(activeFixture,null,'each case must return its prior fixture');
 const window=new EventTarget(),document=new EventTarget();document.visibilityState='visible';
 const queue=[],calls=[],timers=new Map(),handlers=new Set();let now=0,serial=0,legacyReaderCalls=0;
 const clock={setTimeout(fn,ms){const id=++serial;timers.set(id,{fn,due:now+ms,ms});return id;},clearTimeout(id){timers.delete(id);}};
 const port={state:ready(1),fail:false,hold:false,async read(){
  if(this.fail){if(typeof this.fail==='number')this.fail--;else this.fail=false;throw Error('inert read failure');}
  const value=structuredClone(this.state);if(this.hold){this.hold=false;return new Promise(resolve=>queue.push(()=>resolve(value)));}return value;
 }};
 const ipc={invoke(command,args,options){assert.equal(command,'power.lifecycle','UI cannot issue hardware/wake/control commands');assert.equal(options.timeoutMs,3000);calls.push(command);return port.read();},
  on(name,listener){const row={name,fn:event=>listener(event.detail)};handlers.add(row);window.addEventListener('ipc:'+name,row.fn);return()=>{handlers.delete(row);window.removeEventListener('ipc:'+name,row.fn);};}};
 const module={exports:{}};
 const context=vm.createContext({window,document,CustomEvent,console,module,exports:module.exports,queueMicrotask,Promise,structuredClone,__uiNative:ipc,setTimeout:clock.setTimeout,clearTimeout:clock.clearTimeout});
 vm.runInContext(compiled,context);const life=module.exports;
 const reader=native?life.startUiPowerReconciliation(()=>{legacyReaderCalls++;throw Error('obsolete reader must not run');}):null;
 const event=async(name,generation)=>{window.dispatchEvent(new CustomEvent(name,{detail:generation===undefined?{}:{generation}}));await flush();};
 const suspend=async(generation)=>{port.state={...ready(generation),phase:'suspending'};await event('ipc:power.suspending',generation);};
 const publish=async(state)=>{port.state=state;window.dispatchEvent(new CustomEvent('ipc:power.lifecycleChanged',{detail:state}));await flush();};
 const release=async()=>{assert.ok(queue.length,'expected a held read');queue.shift()();await flush();};
 const advance=async(ms)=>{const end=now+ms;for(;;){const row=[...timers].filter(([,t])=>t.due<=end).sort((a,b)=>a[1].due-b[1].due)[0];if(!row)break;now=row[1].due;timers.delete(row[0]);row[1].fn();await flush();}now=end;await flush();};
 const cleanup=async()=>{reader?.stop();while(queue.length)await release();await flush();assert.equal(timers.size,0,'all shared retry timers returned');assert.equal(handlers.size,0,'all shared IPC subscriptions returned');assert.equal(legacyReaderCalls,0,'old UI reader is not an authority');assert(calls.every(c=>c==='power.lifecycle'));};
 // A show/hint never refunds the shared source's existing backoff. Advance
 // the fixture clock explicitly when a case needs its next authoritative read.
 const nextRead=async()=>{const next=[...timers.values()].sort((a,b)=>a.due-b.due)[0];if(next)await advance(next.due-now);else await flush();};
 await flush();activeFixture={life,window,document,port,calls,queue,timers,handlers,event,suspend,publish,release,reader,advance,nextRead,cleanup};return activeFixture;
}
const cases=[];async function test(name,fn){try{await fn();}finally{if(activeFixture){const value=activeFixture;activeFixture=null;await value.cleanup();}}cases.push(name);console.log('PASS '+name);}

await test('root-owned native cold start reads once without control or wake',async()=>{const f=await fixture();assert.equal(f.life.isUiVisible(),true);assert.equal(f.calls.length,1);assert.equal(f.life.getUiVisibilityState().powerGeneration,1);});
await test('G3E missed final commit: shown window reads native committed Ready',async()=>{const f=await fixture();await f.suspend(2);assert.equal(f.life.isUiVisible(),false);f.port.state=ready(2);await f.event('ipc:window.shown');await f.nextRead();assert.equal(f.life.isUiVisible(),true);});
await test('late same-generation pending cannot undo a read-confirmed Ready',async()=>{const f=await fixture();await f.suspend(2);f.port.state=ready(2);await f.event('ipc:power.resumed',2);await f.nextRead();const before=f.calls.length;for(const name of ['ipc:power.suspending','ipc:power.resuming'])await f.event(name,2);assert.equal(f.life.isUiVisible(),true);const count=f.calls.length;await f.event('ipc:power.suspending',2);await f.event('ipc:power.resuming',2);assert.equal(f.calls.length,count);assert(f.calls.length<=before+2,'legacy hints remain bounded');});
await test('older resumed cannot reopen the next real sleep',async()=>{const f=await fixture();await f.suspend(3);await f.event('ipc:power.resumed',2);assert.equal(f.life.isUiVisible(),false);assert.equal(f.life.getUiVisibilityState().powerGeneration,3);});
for(const phase of ['suspending','suspended','resuming'])await test('window shown cannot invent Ready while native is '+phase,async()=>{const f=await fixture();await f.suspend(2);f.port.state={...ready(2),phase};await f.event('ipc:window.shown');assert.equal(f.life.isUiVisible(),false);});
await test('resume-ready is only a bounded native read trigger',async()=>{const f=await fixture();await f.suspend(2);f.port.state={...ready(2),phase:'resuming'};await f.event('ipc:power.resume-ready',2);assert.equal(f.life.isUiVisible(),false);await f.publish({...ready(2),known:false,publicationRevision:21});assert.equal(f.life.isUiVisible(),false);await f.publish({...ready(2),publicationRevision:22});assert.equal(f.life.isUiVisible(),true);});
await test('held old Ready cannot release newer sleep',async()=>{const f=await fixture();await f.suspend(2);f.port.state=ready(2);f.port.hold=true;await f.event('ipc:window.shown');await f.nextRead();await f.suspend(3);await f.release();assert.equal(f.life.isUiVisible(),false);assert.equal(f.life.getUiVisibilityState().powerGeneration,3);});
await test('held old Suspended cannot erase newer read-confirmed Ready',async()=>{const f=await fixture();await f.suspend(2);f.port.state={...ready(2),phase:'suspended'};f.port.hold=true;await f.event('ipc:window.shown');await f.nextRead();await f.publish({...ready(2),publicationRevision:21});await f.release();assert.equal(f.life.isUiVisible(),true);});
for(const value of [null,{}, {phase:'ready',generation:-1},{phase:'ready',generation:2.5},{phase:'bad',generation:2},{...ready(1)},{phase:'ready',generation:'2'}])await test('invalid or stale snapshot stays closed: '+JSON.stringify(value),async()=>{const f=await fixture();await f.suspend(2);f.port.state=value;await f.event('ipc:window.shown');await f.nextRead();assert.equal(f.life.isUiVisible(),false);});
// Authorized semantic correction of the original two UI-closes-on-device-false
// assertions: system Ready displays UI, but never changes the device fact or
// sends hardware commands. Original policy and names remain in the receipt.
for(const flag of ['inputReady','hardwareWritesAllowed'])await test('native Ready without '+flag+' displays UI without hardware authorization',async()=>{const f=await fixture();await f.suspend(2);f.port.state={...ready(2),[flag]:false};await f.event('ipc:power.resumed',2);await f.nextRead();assert.equal(f.life.isUiVisible(),true);assert.equal(f.life.getSharedPowerLifecycle()[flag],false);assert(f.calls.every(c=>c==='power.lifecycle'));f.port.state=ready(2);await f.event('ipc:window.focus');assert.equal(f.life.isUiVisible(),true);});
await test('failed native read observes capped backoff despite existing-edge storms',async()=>{const f=await fixture();await f.suspend(2);f.port.fail=100;await f.event('ipc:window.shown');assert.equal(f.life.isUiVisible(),false);for(const delay of [1000,2000,5000,15000,30000]){const count=f.calls.length;for(let i=0;i<200;i++)await f.event('ipc:window.focus');assert.equal(f.calls.length,count);assert.equal(f.timers.size,1);await f.advance(delay-1);assert.equal(f.calls.length,count);await f.advance(1);assert.equal(f.calls.length,count+1);}f.port.fail=false;f.port.state=ready(2);const count=f.calls.length;await f.event('ipc:window.focus');assert.equal(f.calls.length,count);await f.advance(30000);assert.equal(f.life.isUiVisible(),true);assert.equal(f.timers.size,0);});
await test('200 events coalesce with no resident polling',async()=>{const f=await fixture();await f.suspend(2);f.port.state=ready(2);f.port.hold=true;const before=f.calls.length;await f.event('ipc:window.shown');await f.nextRead();for(let i=0;i<200;i++)await f.event('ipc:window.focus');assert.equal(f.calls.length,before+1);await f.release();assert.equal(f.life.isUiVisible(),true);assert.equal(f.calls.length,before+2);const count=f.calls.length;await f.advance(60000);assert.equal(f.calls.length,count);assert.equal(f.timers.size,0);});
await test('Ready reconciliation does not show a hidden native window',async()=>{const f=await fixture();await f.event('ipc:window.hidden');await f.suspend(2);f.port.state=ready(2);await f.event('ipc:power.resume-ready',2);await f.nextRead();assert.equal(f.life.getUiVisibilityState().powerReady,true);assert.equal(f.life.isUiVisible(),false);});
await test('visible document edge can recover missed final commit',async()=>{const f=await fixture();await f.suspend(2);f.document.visibilityState='hidden';f.document.dispatchEvent(new Event('visibilitychange'));f.port.state=ready(2);f.document.visibilityState='visible';f.document.dispatchEvent(new Event('visibilitychange'));await flush();await f.nextRead();assert.equal(f.life.isUiVisible(),true);});
await test('H5 without a common subscription does not consume raw power events',async()=>{const f=await fixture(false);await f.event('ipc:power.suspending');assert.equal(f.life.isUiVisible(),true);await f.event('ipc:power.resumed');assert.equal(f.life.isUiVisible(),true);assert.equal(f.life.getUiVisibilityState().powerGeneration,0);assert.equal(f.calls.length,0);assert.equal(f.handlers.size,0);});
await test('managed legacy resumed cannot bypass native suspended state',async()=>{const f=await fixture();f.port.state={...ready(2),phase:'suspended'};await f.event('ipc:power.suspending');await f.event('ipc:power.resumed');await f.nextRead();assert.equal(f.life.isUiVisible(),false);});
await test('missing all transition notifications still reconciles a newer native generation',async()=>{const f=await fixture();await f.suspend(2);f.port.state=ready(3);await f.event('ipc:window.shown');await f.nextRead();assert.equal(f.life.isUiVisible(),true);assert.equal(f.life.getUiVisibilityState().powerGeneration,3);});
await test('root unmount invalidates held read and removes reconciliation ownership',async()=>{const f=await fixture();await f.suspend(2);f.port.state=ready(2);f.port.hold=true;await f.event('ipc:window.shown');await f.nextRead();f.reader.stop();await f.release();assert.equal(f.life.isUiVisible(),false);const count=f.calls.length;await f.event('ipc:window.focus');await f.advance(60000);assert.equal(f.calls.length,count);assert.equal(f.handlers.size,0);assert.equal(f.timers.size,0);});

assert.equal(cases.length,27,'the directed UI subset is exactly the original 27 cases');
const sharedPowerCoverage=uiOnly?{executed:false,cases:0,status:'NOT_RUN_DIRECTED_UI_SELECTION'}:await runSharedPowerCases({root,build,test});
assert.equal(cases.length,uiOnly?27:47,'default and directed selections are mutually exclusive');
const sha=value=>createHash('sha256').update(value).digest('hex');
const semanticChanges=[
 {original:'native Ready without inputReady cannot reopen the renderer',current:'native Ready without inputReady displays UI without hardware authorization',basis:'User authorized system Ready and device inputReady separation; UI presentation is not a hardware capability.'},
 {original:'native Ready without hardwareWritesAllowed cannot reopen the renderer',current:'native Ready without hardwareWritesAllowed displays UI without hardware authorization',basis:'User authorized system Ready and device readiness separation; actual false device field and read-only IPC restriction remain asserted.'},
 {original:'failed native read waits for another existing edge',current:'failed native read observes capped backoff despite existing-edge storms',basis:'Shared source owns 1/2/5/15/30-second retry; ordinary focus cannot bypass it.'},
 {original:'H5 legacy events work without a native reader',current:'H5 without a common subscription does not consume raw power events',basis:'Only the shared source consumes system power; unsubscribed H5 cannot invent system transitions.'}
];
const result={pass:true,cases:cases.length,checks:cases,selection:{mode:uiOnly?'ui-consumers-only':'default',uiCases:27,sharedCases:uiOnly?0:20},scope:'Actual UI consumer and shared source together; only ipc.invoke/on, DOM and logical clock mocked; no hardware or system sleep',semanticChanges,sharedPowerCoverage,sourceHashes:{'src/bridge/uiLifecycle.ts':sha(source),'src/App.vue':sha(app),'src/bridge/powerLifecycleState.ts':sha(sharedBytes),'tools/fixtures/ui_shared_power_cases.mjs':sha(fs.readFileSync(path.join(root,'tools/fixtures/ui_shared_power_cases.mjs'))),'tools/ui_power_recovery_selftest.mjs':sha(fs.readFileSync(import.meta.filename))}};
result.observedBuilds=receipt();
if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'ui-power-recovery.json'),JSON.stringify(result,null,2),{flag:'wx'});}console.log(JSON.stringify(result));
