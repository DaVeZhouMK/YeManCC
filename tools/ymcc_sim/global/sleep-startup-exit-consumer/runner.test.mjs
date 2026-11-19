import test from'node:test';import assert from'node:assert/strict';import fs from'node:fs';import path from'node:path';import{getSleepStartupExitConsumerAdapters,REPO,MODULE,inputFiles,toolchainSpec}from'./adapter.mjs';
import{createHash}from'node:crypto';
import{requireStartupExitOwnedReturn,verifyStartupExitBuildEvidence}from'./run.mjs';
test('startup/exit requires observed Job return and keeps forced return archive-only',()=>{
 const limits={deadlineEpochMs:210000,cleanupReserveMs:8000},job={status:0,signal:null};
 const receipt={schemaVersion:1,jobAssigned:true,childPid:42,childExitedBeforeReturn:true,
  resourceObservationVersion:1,jobQueryCount:2,activeProcessesBeforeCleanup:0,activeProcessesFinal:0,
  zeroObservedBeforeDispose:true,jobDisposed:true,terminationRequested:false,terminationSucceeded:false,
  cleanupError:null,...limits,finishedEpochMs:207000,status:'EXITED',exitCode:0,rootExitCode:0,deadlineExpired:false};
 const check=(raw=receipt,process=job)=>requireStartupExitOwnedReturn(process,raw,limits);
 assert.equal(check(),true);
 assert.throws(()=>check(null),/NOT_RETIRED:RECEIPT/);
 for(const [delta,reason] of [
  [{jobAssigned:false},'ASSIGNED_CHILD_EXIT'],[{childPid:0},'ASSIGNED_CHILD_EXIT'],
  [{childExitedBeforeReturn:false},'ASSIGNED_CHILD_EXIT'],[{jobQueryCount:1},'ACCOUNTING'],
  [{activeProcessesBeforeCleanup:1},'CLEANUP_ACCOUNTING'],[{zeroObservedBeforeDispose:false},'ZERO_BEFORE_DISPOSE'],
  [{jobDisposed:false},'ZERO_BEFORE_DISPOSE'],[{cleanupError:'failed'},'CLEANUP_ERROR'],
  [{deadlineEpochMs:210001},'DEADLINE_BINDING'],[{cleanupReserveMs:1000},'DEADLINE_BINDING'],
  [{finishedEpochMs:210001},'TERMINAL_AFTER_DEADLINE'],
 ])assert.throws(()=>check({...receipt,...delta}),new RegExp('NOT_RETIRED:'+reason));
 assert.equal(check({...receipt,status:'FAILED',exitCode:23,rootExitCode:23},{status:23,signal:null}),false);
 const forced={...receipt,status:'TIMEOUT',exitCode:124,rootExitCode:null,deadlineExpired:true,
  activeProcessesBeforeCleanup:2,terminationRequested:true,terminationSucceeded:true};
 assert.equal(check(forced,{status:124,signal:null}),false);
 assert.throws(()=>check({...forced,terminationSucceeded:false},{status:124}),/NOT_RETIRED:CLEANUP_ACCOUNTING/);
 assert.equal(check({...forced,status:'EXITED',exitCode:0,rootExitCode:0,deadlineExpired:false}),false);
});
test('startup/exit build evidence checks every ledger row and keeps observations local',()=>{
 const files=new Map([['header',Buffer.from('exact header')],['native',Buffer.from('exact output')]]),reads=[];
 const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
 const pin=path=>({path,sha256:hash(files.get(path))});
 const ledger=(path,row)=>{files.set(path,Buffer.from('\uFEFF'+JSON.stringify({rows:[row]})));return pin(path);};
 const input=pin('header'),output=pin('native');
 const facts={positive:{compilerInputs:ledger('first',input),produced:{exe:output}},negative:{compilerInputs:ledger('second',input),produced:{exe:output}}};
 const verify=()=>verifyStartupExitBuildEvidence(facts,path=>{reads.push(path);return files.get(path);});
 verify();assert.deepEqual(reads,['first','header','native','second','native']);
 files.set('header',Buffer.from('new version'));
 assert.throws(verify,/STARTUP_EXIT_COMPILER_INPUT_CHANGED:header/);
 files.set('header',Buffer.from('exact header'));
 facts.negative.compilerInputs=ledger('second',{...input,sha256:'0'.repeat(64)});
 assert.throws(verify,/STARTUP_EXIT_COMPILER_INPUT_CHANGED:header/);
 facts.negative.compilerInputs=ledger('second',input);
 const ledgerBytes=files.get('second');files.set('second',Buffer.from('{}'));
 assert.throws(verify,/STARTUP_EXIT_COMPILER_LEDGER_CHANGED/);
 files.set('second',ledgerBytes);files.set('native',Buffer.from('new output'));
 assert.throws(verify,/STARTUP_EXIT_NATIVE_OUTPUT_CHANGED/);
});
test('startup/exit consumer declares 26 actual definitions, 27 cases and 9 fault expectations',()=>{const[a]=getSleepStartupExitConsumerAdapters(),s=JSON.parse(fs.readFileSync(path.join(REPO,MODULE,'spec.json'),'utf8'));assert.equal(a.id,'sleep.startup-exit-consumer');assert.equal(s.reviewedFunctions.length,26);assert.equal(s.newTargetFunctions.length,11);assert.equal(s.previousActualSupportFunctions.length,13);assert.equal(s.additionalPureSupportFunctions.length,2);assert.equal(s.additionalPureSupportFunctions[0],'sgAppendResumeResult');assert.equal(s.fixture.positiveGroups,27);assert.equal(s.fixedAssertions.length,27);assert.equal(s.faults.length,9);assert.equal(inputFiles().length,36);assert(a.evidenceInputs.includes('native/sleep_power_observation.h'));assert.equal(toolchainSpec().files.length,561);for(const p of a.evidenceInputs)assert(fs.statSync(path.join(REPO,p)).isFile(),p);for(const f of s.faults)for(const id of f.expectedFailedCases)assert(s.fixedAssertions.some(x=>x.case===id),id);});
