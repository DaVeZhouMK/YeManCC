import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
import {REPO,readCloseToolProfile,resolveCloseToolProfile} from './adapter.mjs';
import {verifyReport,verifyExecution,verifyOriginalIdentityAssertion,mutateSource,summarizeActualCoverage} from './contract.mjs';
const spec=JSON.parse(fs.readFileSync(new URL('./spec.json',import.meta.url),'utf8'));
const pathIdentity=file=>path.win32.normalize(file).toLowerCase();
const positive=()=>({ok:true,checks:44,hardwareCalls:0,cpuBenchmark:false,scope:spec.originalScope,results:spec.expectedRows.map(({model,name})=>({model,name,ok:true,calls:['memory'],evidence:name.includes('ordinary read timeout')?{successfulReadOnlyCleanup:true,closeCalls:1,shutdownCalls:1}:{settledWithoutDelay:true,closeCalls:1,stateReads:name.startsWith('close-denied ')?0:1,manualWrites:1,maxLive:1}}))});
function negative(){const row=positive();row.ok=false;for(const failure of spec.fault.expectedFailures)row.results[failure.index]={name:failure.name,model:failure.model,ok:false,error:failure.error,calls:['close','state']};return row;}
const raw=()=>({exitCode:0,error:null,signal:null,stdout:JSON.stringify(positive()),boundary:{kind:'fan-global-runtime-boundary',exitCode:0,status:'PASS',explicitExitCalled:false,denied:[],boundaries:Object.fromEntries(['realNetwork','realChildProcess','nativeLibraryLoad','writesOutsideEvidence','realHostMain','hardwareAccess','operatingSystemSleep'].map(name=>[name,false]))},observation:{beforeExitObserved:true,exitCode:0,nodeWindowAbsentAtStart:true,nodeChromeAbsentAtStart:true,nodeWindowAbsentAtEnd:true,nodeChromeAbsentAtEnd:true,activeResources:['PipeWrap']}});
test('complete44 model/name matrix and only10 predicted no-follow-up reads accepted',()=>{assert.equal(verifyReport(positive(),spec).passedRows,44);const row=verifyReport(negative(),spec,{fault:true});assert.equal(row.failedRows,10);assert.equal(row.passedRows,34);});
test('missing model, repeated identities, incomplete or reordered rows reject',()=>{for(const alter of [row=>row.results.pop(),row=>row.results.reverse(),row=>row.results[1]=row.results[0],row=>row.results[0].model='unknown',row=>row.checks=40]){const row=positive();alter(row);assert.throws(()=>verifyReport(row,spec));}});
test('owner overlap, delayed refusal, unexpected follow-up reads and false completion reject',()=>{for(const alter of [row=>row.results[0].evidence.maxLive=2,row=>row.results[0].evidence.settledWithoutDelay=false,row=>row.results[0].evidence.stateReads=1,row=>row.results[43].evidence.shutdownCalls=0]){const row=positive();alter(row);assert.throws(()=>verifyReport(row,spec));}});
test('production denial, wrong numeric assertion or different fault case cannot count',()=>{for(const alter of [row=>row.results[0].error='Error: AUTH_REJECTED',row=>row.results[0].error=row.results[0].error.replace('1 !== 0','2 !== 0'),row=>row.results[0].model='ROG']){const row=negative();alter(row);assert.throws(()=>verifyReport(row,spec,{fault:true}));}});
test('natural no-native endpoint accepted; IO refusal/native bridge/live timers reject',()=>{assert.equal(verifyExecution(raw(),spec).checks,44);for(const alter of [row=>row.boundary.denied.push({operation:'fetch'}),row=>row.boundary.explicitExitCalled=true,row=>row.observation.nodeWindowAbsentAtStart=false,row=>row.observation.activeResources.push('Timeout')]){const row=raw();alter(row);assert.throws(()=>verifyExecution(row,spec));}});
test('incomplete stdout, compile diagnostics and timeout cannot claim behavioral fault',()=>{for(const alter of [row=>row.stdout='',row=>row.stdout='SyntaxError compile failure',row=>row.error='ETIMEDOUT']){const row=raw();alter(row);assert.throws(()=>verifyExecution(row,spec));}});
test('only unique entry guard and original minified counter21 are accepted',()=>{assert.equal(mutateSource(spec.fault.before,spec.fault),spec.fault.after);assert.throws(()=>mutateSource('',spec.fault));assert.throws(()=>mutateSource(spec.fault.before+spec.fault.before,spec.fault));const original=fs.readFileSync(new URL('../../../../'+spec.original,import.meta.url),'utf8');assert.equal(verifyOriginalIdentityAssertion(original,spec).line,21);assert.throws(()=>verifyOriginalIdentityAssertion('\n'+original,spec));const wrong=structuredClone(spec);wrong.fault.expectedOriginalAssertionLine=20;assert.throws(()=>verifyReport(negative(),wrong,{fault:true}));});
test('full no-message pinned Node assertion bytes independently generated',()=>{let rendered;try{assert.equal(1,0);}catch(error){rendered=String(error);}for(const row of spec.fault.expectedFailures)assert.equal(row.error,rendered);});
test('aggregate compiler input count requires both verified worker arms',async()=>{
 const {verifiedCompilerInputCount}=await import('./run.mjs');
 const count=spec.buildInputs.length;
 const worker={positive:{compilerInputs:count},negative:{compilerInputs:count}};
 assert.equal(verifiedCompilerInputCount(worker,spec),count);
 for(const [arm,code] of [['positive',/FAN_CLOSE_POSITIVE_COMPILER_INPUT_COUNT_MISMATCH/],['negative',/FAN_CLOSE_NEGATIVE_COMPILER_INPUT_COUNT_MISMATCH/]]){
  for(const rejected of [undefined,{}, {compilerInputs:count-1},{compilerInputs:String(count)}]){
   assert.throws(()=>verifiedCompilerInputCount({...worker,[arm]:rejected},spec),code);
  }
 }
});
test('Close substitutes just its exact PowerShell row without changing the shared profile',()=>{
 const file=new URL('../../../../'+spec.sharedToolProfile,import.meta.url);
 const bytes=fs.readFileSync(file),base=JSON.parse(bytes),preimage=structuredClone(base);
 const {previous,replacement}=spec.closePowerShell;
 const index=base.files.findIndex(row=>pathIdentity(row.path)===pathIdentity(previous.path));
 assert(index>=0);
 const expected=structuredClone(base);
 expected.powershellExecutable=replacement.path;
 expected.files[index]={...expected.files[index],...replacement};
 assert.deepEqual(resolveCloseToolProfile(base,spec.closePowerShell),expected);
 assert.deepEqual(readCloseToolProfile(REPO,spec),expected);
 assert.deepEqual(base,preimage);
 assert.deepEqual(fs.readFileSync(file),bytes);
});
test('Close refuses drift or ambiguity in the shared PowerShell preimage',()=>{
 const base=JSON.parse(fs.readFileSync(new URL('../../../../'+spec.sharedToolProfile,import.meta.url)));
 const {previous,replacement}=spec.closePowerShell;
 const previousRow=profile=>profile.files.find(row=>pathIdentity(row.path)===pathIdentity(previous.path));
 for(const perturb of [
  p=>{p.powershellExecutable=replacement.path;},
  p=>{previousRow(p).sha256='0'.repeat(64);},
  p=>{previousRow(p).path=replacement.path;},
  p=>p.files.push(previous),
  p=>p.files.push(replacement),
 ]){
  const altered=structuredClone(base);perturb(altered);
  assert.throws(()=>resolveCloseToolProfile(altered,spec.closePowerShell),/FAN_CLOSE_(SHARED_POWERSHELL|POWERSHELL_FILE)_PREIMAGE/);
 }
});
test('source review authorizes only exact and complete frozen inputs',async()=>{
 const {verifyReviewedSnapshot}=await import('./run.mjs');
 const paths=['fanHost.ts','guard.mjs'];
 const pins=paths.map((path,i)=>({path,sha256:String(i).repeat(64)}));
 assert.doesNotThrow(()=>verifyReviewedSnapshot(pins,paths,[...pins,{path:'runner.mjs',sha256:'a'.repeat(64)}]));
 for(const changed of [[pins[0]], [pins[0],pins[0]], [pins[0],{...pins[1],path:'other.mjs'}]])
  assert.throws(()=>verifyReviewedSnapshot(changed,paths,pins),/FAN_CLOSE_REVIEWED_SOURCE_MEMBERSHIP/);
 assert.throws(()=>verifyReviewedSnapshot(pins,paths,[pins[0],{...pins[1],sha256:'a'.repeat(64)}]),/FAN_CLOSE_REVIEWED_SOURCE_CHANGED:guard.mjs/);
});
