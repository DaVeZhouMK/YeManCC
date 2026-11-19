import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
import {verifyOriginalExecution,parseArgs} from './run.mjs';import {createIoBoundary,executeOriginalBundle} from './vm-io.mjs';import {getTdpScheduleConsumerAdapters,CASE_ID} from './adapter.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';
const spec=JSON.parse(fs.readFileSync(new URL('./spec.json',import.meta.url),'utf8'));
const raw=()=>({exitCode:0,processExitObserved:true,thrown:null,boundary:{denied:[],processAttempts:0,networkAttempts:0,virtualizedCalls:0},logs:[...spec.expectedMarkers]});
const samePath=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
function fixture(t){
 const workspace=simulationCorePath('Workspace'),parent=fs.realpathSync.native(workspace);
 assert(samePath(parent,workspace)&&!fs.lstatSync(workspace).isSymbolicLink());
 const root=fs.mkdtempSync(path.join(parent,'ymcc-tdp-schedule-contract-')),identity=fs.lstatSync(root,{bigint:true});
 t.after(()=>{
  const actual=fs.realpathSync.native(root),current=fs.lstatSync(root,{bigint:true});
  assert(samePath(actual,root)&&samePath(path.dirname(actual),parent));
  assert(current.isDirectory()&&!current.isSymbolicLink()&&current.dev===identity.dev&&current.ino===identity.ino);
  fs.rmSync(actual,{recursive:true,force:true});
 });
 return root;
}
test('descriptor covers the original full scheduler consumer rather than only setTdp',()=>{const[a]=getTdpScheduleConsumerAdapters();assert.equal(a.id,CASE_ID);assert(a.evidenceInputs.includes('tools/performance_schedule_tdp_selftest.ts'));assert(a.evidenceInputs.includes('src/bridge/performanceSchedule.ts'));assert(a.evidenceInputs.includes('src/bridge/autofloat.ts'));assert.deepEqual(a.resultFiles,['tdp-schedule/result.json','tdp-schedule/output-evidence.json']);});
test('the original exit and all three original completion markers are mandatory',()=>{assert.equal(verifyOriginalExecution(raw(),spec).completedMarkers.length,3);for(const mode of ['exit','marker','code']){const value=raw();if(mode==='exit')value.processExitObserved=false;else if(mode==='marker')value.logs.pop();else value.exitCode=1;assert.throws(()=>verifyOriginalExecution(value,spec));}});
test('a targeted original assertion distinguishes a consumer fault from environment failure',async t=>{
 const root=fixture(t),message=spec.fault.expectedFailedCase,headline=`Error: ${message}`;
 const catchError=message=>`void Promise.resolve().then(()=>{throw new Error(${JSON.stringify(message)});}).catch(error=>{console.error(error.stack||error);process.exit(1);});`;
 const negative={negative:true},rejection=/ORIGINAL_ASSERTION_NOT_CAUGHT/;
 const cases=[
  ['target',catchError(message),headline,true],
  ['unrelated',catchError('setup failed'),'Error: setup failed'],
  ['ordinary-log',`console.log(${JSON.stringify(headline)});${catchError('setup failed')}`,'Error: setup failed'],
  ['warning',`console.warn(${JSON.stringify(headline)});process.exit(1);`,null],
  ['non-headline',catchError('setup failed\n'+headline),'Error: setup failed'],
  ['non-exact',catchError(message+' (setup failed)'),headline+' (setup failed)'],
  ['stale-error',`console.error(${JSON.stringify(headline)});console.warn('setup failed');process.exit(1);`,null],
  ['after-exit',`console.error('Error: setup failed');process.exit(1);console.error(${JSON.stringify(headline)});`,'Error: setup failed'],
 ];
 for(const [name,script,terminalErrorLine,accepted] of cases){
  const file=path.join(root,name+'.cjs');fs.writeFileSync(file,script,{flag:'wx'});
  const value=await executeOriginalBundle(file,path.join(root,name),{timeoutMs:500});
  assert.equal(value.terminalErrorLine,terminalErrorLine,name);
  if(!accepted){assert.throws(()=>verifyOriginalExecution(value,spec,negative),rejection,name);continue;}
  assert.deepEqual(verifyOriginalExecution(value,spec,negative).failedNames,[message]);
  const {terminalErrorLine:_,...legacy}=value;
  for(const rejected of [legacy,{...value,exitCode:0},{...value,logs:[...value.logs,spec.expectedMarkers[0]]}]){
   assert.throws(()=>verifyOriginalExecution(rejected,spec,negative),rejection);
  }
 }
});
test('timeout or forbidden IO invalidates successful marker text',()=>{for(const mode of ['throw','process','network','file']){const value=raw();if(mode==='throw')value.thrown='timeout';else if(mode==='process')value.boundary.processAttempts=1;else if(mode==='network')value.boundary.networkAttempts=1;else value.boundary.denied.push('outside');assert.throws(()=>verifyOriginalExecution(value,spec));}});
test('default installation namespace is served only by private scratch storage',t=>{const root=fixture(t),{safeFs,observation}=createIoBoundary(root),virtual='C:\\SOFT\\YeMan\\PowerControl\\fixture-B0.json',file=path.join(root,'default-install','fixture-B0.json');safeFs.mkdirSync('C:\\SOFT\\YeMan\\PowerControl',{recursive:true});safeFs.writeFileSync(virtual,'private fixture');assert.equal(safeFs.readFileSync(virtual,'utf8'),'private fixture');assert.equal(fs.readFileSync(file,'utf8'),'private fixture');assert.equal(observation.denied.length,0);assert(observation.virtualizedCalls>=3);});
test('private temporary cleanup is contained and the scratch root cannot be deleted',t=>{const root=fixture(t),{safeFs}=createIoBoundary(root);const temporary=safeFs.mkdtempSync(path.join(root,'test-'));safeFs.writeFileSync(path.join(temporary,'data.txt'),'fixture');safeFs.rmSync(temporary,{recursive:true,force:true});assert.equal(fs.existsSync(temporary),false);assert.throws(()=>safeFs.rmSync(root,{recursive:true,force:true}),/ROOT_DELETE_FORBIDDEN/);assert.equal(fs.existsSync(root),true);});
test('void main observes its own exit and leaves no resident fixture timers',async t=>{const root=fixture(t),file=path.join(root,'void-main.cjs');fs.writeFileSync(file,'void Promise.resolve().then(()=>process.exit(0));');const result=await executeOriginalBundle(file,path.join(root,'scratch'),{timeoutMs:100});assert.equal(result.processExitObserved,true);assert.equal(result.exitCode,0);assert.equal(result.thrown,null);assert.throws(()=>verifyOriginalExecution(result,spec),/ORIGINAL_NOT_PASS/);});
test('an isolated suite that never signals exit cannot silently complete',async t=>{const root=fixture(t),file=path.join(root,'hang.cjs');fs.writeFileSync(file,'void Promise.resolve();');const result=await executeOriginalBundle(file,path.join(root,'scratch'),{timeoutMs:50});assert.equal(result.processExitObserved,false);assert.equal(result.thrown,'TDP_ORIGINAL_TIMEOUT');});
test('CLI cannot reuse evidence or turn unknown arguments into default execution',t=>{const root=fixture(t);for(const args of [[],['--bad',root],['--evidence-dir','relative'],['--evidence-dir',root],['--evidence-dir',root,'extra']])assert.throws(()=>parseArgs(args));});
