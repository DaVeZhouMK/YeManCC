// Finite receipt/coverage controls only. No native/JS business child is started.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import vm from 'node:vm';
import {OWNED_SOURCES} from './owned-adapter.mjs';
import {validateCurrentEnvelope,validateBaselineReceipt,aggregateSeven,coverageMap,allocateBudget,sourceMap,naturalExit,verifyCurrentDisk,parentDeclaration,legacyMigration,EXACT_25,REQUIREMENTS,JS_REGRESSIONS,NATIVE_REGRESSIONS,BUDGETS,currentChildToolPins} from './current-parent-global.mjs';
import {verifyDeferredResourceRevalidation,verifyDeferredResourceExecution,verifyDeferredResourceProof} from './typed-binding-gate.mjs';
import {validateOriginalResult as validateCurrentOriginal,ORIGINAL_CASES as originalCoreCases} from './owned-run.mjs';
import {createRequiredStageGuard,raceInitialProducerFailure,collectInitialProducersAfterFailure} from './current-parent-global.mjs';
// Explicit finite factory controls only; the original history-bound suite stays in else.
const requiredStageFilterText=process.env.YMCC_PARENT_GLOBAL_CASE_FILTER;
if(requiredStageFilterText!==undefined){
 const requested=JSON.parse(requiredStageFilterText);
 if(!Array.isArray(requested)||requested.length===0||requested.some(name=>typeof name!=='string')||new Set(requested).size!==requested.length)throw Error('YMCC_PARENT_GLOBAL_CASE_FILTER_INVALID');
 // Synthetic disk receipts exercise the exported production admission, never a producer.
 let budgetControlSequence=0;
 function budgetControl({raw=true,parentRemainingMs=458684,allocatedMs=Math.min(240000,parentRemainingMs),editRequest=()=>{},editTerminal=()=>{},editResult=()=>{}}={}){
  const repo=path.resolve(import.meta.dirname,'../../../..'),coreEvidence=path.resolve(repo,'../../../Testing/YMCC/Evidence');
  assert(process.env.YMCC_PARENT_GLOBAL_CONTROL_RESULT,'DIRECTED_CONTROL_EVIDENCE_REQUIRED');
  const outputRoot=path.dirname(path.resolve(process.env.YMCC_PARENT_GLOBAL_CONTROL_RESULT));assert(outputRoot.startsWith(coreEvidence+path.sep),'DIRECTED_CONTROL_CORE_EVIDENCE_REQUIRED');
  const directory=path.join(outputRoot,'fixtures',String(++budgetControlSequence).padStart(3,'0'));fs.mkdirSync(directory,{recursive:true});
  const write=(name,value)=>{const file=path.join(directory,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,typeof value==='string'?value:JSON.stringify(value)+'\n',{flag:'wx'});return file;};
  const pin=file=>({path:file,bytes:fs.statSync(file).size,sha256:crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')});
  const source=pin(write('control/source.h','SYNTHETIC metadata-only source')),program=pin(write('program.fixture','SYNTHETIC never executed')),helper=pin(write('helper.fixture','SYNTHETIC never executed')),launcher=pin(write('launcher.fixture','SYNTHETIC never executed'));
  const sources=[{path:'control/source.h',sha256:source.sha256}],tools=[program,helper,launcher],before=new Map(sources.map(row=>[row.path,row.sha256]));
  const result={fieldRegression:'decky-update-disconnect-20261020',nativeChecks:1,passed:true,inputsUnchanged:true,sourceInputs:sources,realSteamActions:0,userSettingsWrites:0,hardwareWrites:0};editResult(result);
  const originalResult=pin(write('result.json',result)),args=[path.join(directory,'baseline.fixture'),'--evidence-dir',directory],argumentsPin=pin(write('arguments.json',args));
  const parentStartedAtEpochMs=1700000000000,allocatedAtEpochMs=parentStartedAtEpochMs+BUDGETS.parent-parentRemainingMs,launchRequestedEpochMs=allocatedAtEpochMs+6,deadlineEpochMs=allocatedAtEpochMs+allocatedMs;
  const budget={schemaVersion:1,parentStartedAtEpochMs,parentBudgetMs:BUDGETS.parent,allocatedAtEpochMs,requestCreatedEpochMs:allocatedAtEpochMs+3,originalBudgetMs:240000,allocatedBudgetMs:allocatedMs,parentRemainingBeforeMs:parentRemainingMs,deadlineEpochMs,cleanupReserveMs:5000};
  const request={schemaVersion:1,kind:'ymcc-raw-program-v1',budget,cancelNonce:'a'.repeat(64),cwd:directory,arguments:argumentsPin,environment:{},sources:[source],tools,maxOutputBytes:16*1024*1024};editRequest(request);
  const requestPin=pin(write('request.json',request)),logs=Object.fromEntries(['stdout','stderr'].map(name=>[name,pin(write(name+'.raw','SYNTHETIC metadata-only log'))]));
  const declared=[...request.sources.map(row=>({role:'sources',...row})),...request.tools.map(row=>({role:'tools',...row}))];
  const terminal={schemaVersion:1,kind:'ymcc-raw-program-terminal-v1',reason:'NATURAL',collectionComplete:true,cancelled:false,ownerLost:false,residualDescendants:false,timedOut:false,outputLimitExceeded:false,logsClosedAndFlushed:true,declaredPinsUnchanged:true,
   request:{...requestPin,bytes:fs.statSync(requestPin.path).size},argumentsPin,launcher:{pid:222,program:launcher,helper},owner:{pid:111,expectedStartTicks:'638355968000000000',handleBound:true},
   business:{pid:333,program,arguments:args,cwd:directory,atomicJobAdmission:true,originalDeadlineEpochMs:deadlineEpochMs,runCutoffEpochMs:deadlineEpochMs-5000,cleanupReserveMs:5000},businessExitCode:0,
   job:{activeProcessesFinal:0,zeroObservedBeforeDispose:true,disposed:true,terminateRequested:false,cleanupError:null},logs,declaredInputsBefore:declared,declaredInputsAfter:declared.map(({path,bytes,sha256})=>({path,bytes,sha256})),startedEpochMs:launchRequestedEpochMs+1,finishedEpochMs:launchRequestedEpochMs+123,deadlineEpochMs,runCutoffEpochMs:deadlineEpochMs-5000,cleanupReserveMs:5000,helperExitCode:0};editTerminal(terminal);
  const terminalPin=pin(write('terminal.json',terminal));
  const receipt={id:'original.plugins.decky-update-recovery',pid:333,program,arguments:args,cwd:directory,finishedAt:new Date(terminal.finishedEpochMs).toISOString(),exitCode:0,error:null,signal:null,timedOut:false,outputTruncated:false,elapsedMs:124,timeoutMs:raw?deadlineEpochMs-launchRequestedEpochMs:240000,originalBudgetMs:240000,parentRemainingBeforeMs:parentRemainingMs,budgetClipped:allocatedMs<240000,boundSourceInputs:sources,toolArtifacts:tools,originalResult,stdout:logs.stdout,stderr:logs.stderr};
  if(raw)receipt.rawExecution={budget:structuredClone(budget),launchRequestedEpochMs,terminal:terminalPin,request:requestPin,arguments:argumentsPin,helperSource:helper,launcher:terminal.launcher,helperPid:222,helperExitCode:0,helperSignal:null,actualBusiness:terminal.business,completion:{kind:'NATURAL',forcedToolFinalization:false,productResourcesWaived:false},job:terminal.job,logs:terminal.logs,sourceBefore:terminal.declaredInputsBefore,sourceAfter:terminal.declaredInputsAfter,collectionComplete:true,cancelled:false,ownerLost:false,residualDescendants:false};
  return {result,process:receipt,before,required:['control/source.h']};
 }
 const admitBudget=c=>validateBaselineReceipt(c.result,c.process,c.before,c.required);
 // Real parent race/guard/cleanup helpers with deferred proofs; no process is started.
 const flushInitialProducers=()=>new Promise(resolve=>setImmediate(resolve));
 const initialProof=(id,extra={})=>({id,exitCode:id==='current.native.entry'?2:0,elapsedMs:1,timeoutMs:100,error:null,signal:null,timedOut:false,outputTruncated:false,...extra});
 function initialProducerControl({cancelNativeError=null,validateBaseline=()=>{}}={}){
  const trace=[],proofs=[],cleanupErrors=[],cases=[],failures=[],stops=[];
  const mock=id=>{let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});return {resolve,reject,cancelCalls:0,wait:()=>promise,async cancel(){this.cancelCalls++;if(id==='current.native.entry'&&cancelNativeError)throw cancelNativeError;return await promise;}};};
  const native=mock('current.native.entry'),baseline=mock('original.plugins.decky-update-recovery');
  const persist=proof=>{if(!proofs.some(row=>row.id===proof.id))proofs.push(proof);};
  const guard=createRequiredStageGuard({cases,recordFailure:(id,error)=>{failures.push({id,error});cases.push({id,passed:false});},recordStop:row=>stops.push(row)});
  let observedCause=null;
  const done=(async()=>{
   try{
    const early=await raceInitialProducerFailure(native,baseline);
    if(early)await guard('original.plugins.decky-update-recovery',()=>{if(early.proof)persist(early.proof);throw early.cause;});
    await guard('current.native.entry',async()=>{const proof=await native.wait();persist(proof);naturalExit(proof,2);trace.push('native');cases.push({id:proof.id,passed:true});});
    await guard('original.plugins.decky-update-recovery',async()=>{const proof=await baseline.wait();persist(proof);naturalExit(proof,0);validateBaseline(proof);trace.push('baseline');cases.push({id:proof.id,passed:true});});
    await guard('later',()=>{trace.push('later');cases.push({id:'later',passed:true});});
   }catch(cause){observedCause=cause;await collectInitialProducersAfterFailure([native,baseline],persist,cleanup=>cleanupErrors.push(cleanup));}
   return observedCause;
  })();
  return {native,baseline,done,trace,proofs,cleanupErrors,cases,failures,stops,get cause(){return observedCause;}};
 }
 const directedControls=[
  ['initial producer baseline nonzero cancels and awaits unresolved native once before stopping',async()=>{
   const c=initialProducerControl(),failed=initialProof('original.plugins.decky-update-recovery',{exitCode:1}),cancelled=initialProof('current.native.entry',{exitCode:99,error:'RAW_PRODUCER_CANCELLED'});let finished=false;c.done.then(()=>{finished=true;});
   c.baseline.resolve(failed);await flushInitialProducers();assert.equal(c.native.cancelCalls,1);assert.equal(finished,false);assert.deepEqual(c.trace,[]);assert.strictEqual(c.proofs[0],failed);assert.equal(c.stops[0].id,failed.id);
   c.native.resolve(cancelled);const cause=await c.done;assert.strictEqual(cause,c.stops[0].error);assert.match(cause.message,/UNEXPECTED_CHILD_EXIT/);assert.equal(c.native.cancelCalls,1);assert.equal(c.baseline.cancelCalls,1);assert.deepEqual(c.proofs,[failed,cancelled]);assert.deepEqual(c.cleanupErrors,[]);assert.deepEqual(c.cases,[{id:failed.id,passed:false}]);assert.deepEqual(c.trace,[]);
  }],
  ['initial producer successful baseline first keeps native then baseline validation',async()=>{
   const c=initialProducerControl(),baseline=initialProof('original.plugins.decky-update-recovery');c.baseline.resolve(baseline);await flushInitialProducers();assert.equal(c.native.cancelCalls,0);assert.deepEqual(c.trace,[]);assert.deepEqual(c.cases,[]);
   c.native.resolve(initialProof('current.native.entry'));assert.equal(await c.done,null);assert.deepEqual(c.trace,['native','baseline','later']);assert.equal(c.native.cancelCalls,0);assert.equal(c.baseline.cancelCalls,0);assert.deepEqual(c.stops,[]);
  }],
  ['initial producer native succeeds first retains normal sequence',async()=>{
   const c=initialProducerControl();c.native.resolve(initialProof('current.native.entry'));await flushInitialProducers();assert.deepEqual(c.trace,['native']);assert.equal(c.baseline.cancelCalls,0);
   c.baseline.resolve(initialProof('original.plugins.decky-update-recovery'));assert.equal(await c.done,null);assert.deepEqual(c.trace,['native','baseline','later']);assert.equal(c.native.cancelCalls,0);assert.equal(c.baseline.cancelCalls,0);
  }],
  ['initial producer handled semantic negative root zero does not cancel',async()=>{
   const baseline=initialProof('original.plugins.decky-update-recovery',{handledSemanticNegative:{observedExitCode:2,rejected:true}}),c=initialProducerControl({validateBaseline:proof=>assert.equal(proof.handledSemanticNegative.rejected,true)});
   c.baseline.resolve(baseline);await flushInitialProducers();assert.equal(c.native.cancelCalls,0);assert.deepEqual(c.cases,[]);c.native.resolve(initialProof('current.native.entry'));assert.equal(await c.done,null);assert.deepEqual(c.trace,['native','baseline','later']);assert.deepEqual(c.failures,[]);assert.equal(c.native.cancelCalls,0);
  }],
  ['initial producer cancel rejection is retained without masking baseline cause',async()=>{
   const cleanup=new Error('owned cancel rejected'),c=initialProducerControl({cancelNativeError:cleanup}),failed=initialProof('original.plugins.decky-update-recovery',{exitCode:1});c.baseline.resolve(failed);const cause=await c.done;
   assert.match(cause.message,/UNEXPECTED_CHILD_EXIT/);assert.notStrictEqual(cause,cleanup);assert.strictEqual(cause,c.failures[0].error);assert.strictEqual(cause,c.stops[0].error);assert.deepEqual(c.cleanupErrors,[cleanup]);assert.deepEqual(c.proofs,[failed]);assert.equal(c.native.cancelCalls,1);assert.deepEqual(c.trace,[]);assert.deepEqual(c.cases,[{id:failed.id,passed:false}]);
  }],
  ['initial producer baseline root error stops before native validation',async()=>{
   const c=initialProducerControl(),failed=initialProof('original.plugins.decky-update-recovery',{error:'ROOT_LAUNCH_ERROR'});c.baseline.resolve(failed);await flushInitialProducers();assert.equal(c.native.cancelCalls,1);c.native.resolve(initialProof('current.native.entry',{error:'CANCELLED'}));assert.match((await c.done).message,/CHILD_PROCESS_ERROR/);assert.equal(c.stops[0].id,failed.id);assert.deepEqual(c.trace,[]);assert.strictEqual(c.proofs[0],failed);
  }],
  ['initial producer baseline timeout stops before native validation',async()=>{
   const c=initialProducerControl(),failed=initialProof('original.plugins.decky-update-recovery',{exitCode:null,timedOut:true,error:'TIMEOUT'});c.baseline.resolve(failed);await flushInitialProducers();assert.equal(c.native.cancelCalls,1);c.native.resolve(initialProof('current.native.entry',{error:'CANCELLED'}));assert.match((await c.done).message,/UNEXPECTED_CHILD_EXIT/);assert.equal(c.stops[0].id,failed.id);assert.strictEqual(c.proofs[0],failed);assert.deepEqual(c.trace,[]);
  }],
  ['initial producer native first failure still cancels and awaits baseline once',async()=>{
   const c=initialProducerControl(),failed=initialProof('current.native.entry',{exitCode:1});c.native.resolve(failed);await flushInitialProducers();assert.equal(c.baseline.cancelCalls,1);assert.equal(c.stops[0].id,failed.id);c.baseline.resolve(initialProof('original.plugins.decky-update-recovery',{error:'CANCELLED'}));const cause=await c.done;assert.strictEqual(cause,c.failures[0].error);assert.equal(c.native.cancelCalls,1);assert.equal(c.baseline.cancelCalls,1);assert.equal(c.proofs.length,2);assert.deepEqual(c.trace,[]);
  }],
  ['initial producer baseline root zero still requires full baseline validation',async()=>{
   const original=new Error('baseline full receipt rejected'),c=initialProducerControl({validateBaseline:()=>{throw original;}});c.baseline.resolve(initialProof('original.plugins.decky-update-recovery'));await flushInitialProducers();assert.equal(c.native.cancelCalls,0);assert.deepEqual(c.cases,[]);c.native.resolve(initialProof('current.native.entry'));assert.strictEqual(await c.done,original);assert.equal(c.stops[0].id,'original.plugins.decky-update-recovery');assert.deepEqual(c.trace,['native']);assert(!c.cases.some(row=>row.id==='later'));assert(!c.cases.some(row=>row.id==='original.plugins.decky-update-recovery'&&row.passed));
  }],
  ['initial producer baseline wait rejection retains original cause',async()=>{
   const original=new Error('raw proof collection rejected'),c=initialProducerControl();c.baseline.reject(original);await flushInitialProducers();assert.equal(c.native.cancelCalls,1);c.native.resolve(initialProof('current.native.entry',{error:'CANCELLED'}));assert.strictEqual(await c.done,original);assert.strictEqual(c.stops[0].error,original);assert.deepEqual(c.cleanupErrors,[original]);assert.deepEqual(c.trace,[]);
  }],
  ['raw baseline admits exact allocation with six milliseconds of request setup',()=>{const c=budgetControl();assert.equal(c.process.timeoutMs,239994);assert.equal(c.process.rawExecution.budget.allocatedBudgetMs,240000);assert.equal(c.process.budgetClipped,false);assert.strictEqual(admitBudget(c),c.result);} ],
  ['raw closing pins accept the actual roleless helper schema',()=>{const c=budgetControl();assert.strictEqual(admitBudget(c),c.result);} ],
  ['raw closing pins reject missing input',()=>{const c=budgetControl({editTerminal:t=>t.declaredInputsAfter.pop()});assert.throws(()=>admitBudget(c),/RAW_BUDGET_DECLARED_AFTER_COUNT/);} ],
  ['raw closing pins reject equal-count duplicated input',()=>{const c=budgetControl({editTerminal:t=>t.declaredInputsAfter[1]={...t.declaredInputsAfter[0]}});assert.throws(()=>admitBudget(c),/RAW_BUDGET_DECLARED_AFTER:/);} ],
  ['raw closing pins reject changed hash',()=>{const c=budgetControl({editTerminal:t=>t.declaredInputsAfter[0].sha256='f'.repeat(64)});assert.throws(()=>admitBudget(c),/RAW_BUDGET_DECLARED_AFTER:/);} ],
  ['raw closing pins reject changed bytes',()=>{const c=budgetControl({editTerminal:t=>t.declaredInputsAfter[0].bytes++});assert.throws(()=>admitBudget(c),/RAW_BUDGET_DECLARED_AFTER:/);} ],
  ['raw closing pins reject reordered same members',()=>{const c=budgetControl({editTerminal:t=>t.declaredInputsAfter.reverse()});assert.throws(()=>admitBudget(c),/RAW_BUDGET_DECLARED_AFTER:/);} ],
  ['raw closing pins reject extra unproduced role field',()=>{const c=budgetControl({editTerminal:t=>t.declaredInputsAfter[0].role='tools'});assert.throws(()=>admitBudget(c),/RAW_BUDGET_DECLARED_AFTER_SCHEMA:/);} ],
  ['raw admission pins retain source versus tool roles',()=>{const c=budgetControl({editTerminal:t=>t.declaredInputsBefore[0].role='tools'});assert.throws(()=>admitBudget(c),/RAW_BUDGET_DECLARED_BEFORE:/);} ],
  ['raw baseline rejects genuinely parent-clipped allocation',()=>{const c=budgetControl({parentRemainingMs:239000});assert.equal(c.process.budgetClipped,true);assert.throws(()=>admitBudget(c),/REUSE_REQUIRES_FULL_ORIGINAL_BUDGET/);} ],
  ['raw baseline rejects expanded allocation',()=>{const c=budgetControl({allocatedMs:240001});assert.throws(()=>admitBudget(c),/RAW_ALLOCATION_CLIPPED_OR_EXPANDED/);} ],
  ['raw baseline rejects request and result budget disagreement',()=>{const c=budgetControl();c.process.rawExecution.budget.deadlineEpochMs++;assert.throws(()=>admitBudget(c),/RAW_BUDGET_PROOF_MISMATCH/);} ],
  ['raw baseline rejects terminal deadline mismatch',()=>{const c=budgetControl({editTerminal:t=>t.deadlineEpochMs++});assert.throws(()=>admitBudget(c),/RAW_TERMINAL_DEADLINE_MISMATCH/);} ],
  ['raw baseline rejects business deadline mismatch',()=>{const c=budgetControl({editTerminal:t=>t.business.originalDeadlineEpochMs++});assert.throws(()=>admitBudget(c),/ERR_ASSERTION/);} ],
  ['raw baseline rejects effective timeout mismatch',()=>{const c=budgetControl();c.process.timeoutMs--;assert.throws(()=>admitBudget(c),/RAW_EFFECTIVE_TIMEOUT_MISMATCH/);} ],
  ['raw baseline rejects mismatched request pin',()=>{const c=budgetControl();c.process.rawExecution.request.sha256='f'.repeat(64);assert.throws(()=>admitBudget(c),/RAW_BUDGET_PIN_CHANGED/);} ],
  ['raw baseline rejects mismatched terminal pin',()=>{const c=budgetControl();c.process.rawExecution.terminal.sha256='f'.repeat(64);assert.throws(()=>admitBudget(c),/RAW_BUDGET_PIN_CHANGED/);} ],
  ['raw baseline rejects legacy request without precise allocation',()=>{const c=budgetControl({editRequest:q=>delete q.budget});assert.throws(()=>admitBudget(c),/RAW_PRECISE_BUDGET_REQUIRED/);} ],
  ['raw baseline rejects marker without pinned provenance',()=>{const c=budgetControl({raw:false});c.process.rawExecution={};assert.throws(()=>admitBudget(c),/RAW_BUDGET_PIN_REQUIRED/);} ],
  ['raw baseline rejects incorrect clipping classification',()=>{const c=budgetControl();c.process.budgetClipped=true;assert.throws(()=>admitBudget(c),/RAW_CLIPPED_SEMANTICS/);} ],
  ['raw baseline rejects incomplete Job completion',()=>{const c=budgetControl({editTerminal:t=>t.job.activeProcessesFinal=1});assert.throws(()=>admitBudget(c),/RAW_COMPLETION_ACTIVE_PROCESSES/);} ],
  ['raw baseline rejects changed bound source proof',()=>{const c=budgetControl();c.process.boundSourceInputs=[{path:'control/source.h',sha256:'f'.repeat(64)}];assert.throws(()=>admitBudget(c),/RAW_BUDGET_BOUND_SOURCES/);} ],
  ['raw baseline rejects changed bound tool proof',()=>{const c=budgetControl();c.process.toolArtifacts=[];assert.throws(()=>admitBudget(c),/RAW_BUDGET_BOUND_TOOLS/);} ],
  ['raw baseline rejects original result pin mismatch',()=>{const c=budgetControl();c.process.originalResult.sha256='f'.repeat(64);assert.throws(()=>admitBudget(c),/EVIDENCE_FILE_CHANGED/);} ],
  ['raw baseline preserves original FAIL',()=>{const c=budgetControl({editResult:r=>r.passed=false});assert.throws(()=>admitBudget(c),/BASELINE_ORIGINAL_FAIL/);} ],
  ['sequential baseline accepts only exact original timeout',()=>{const c=budgetControl({raw:false});assert.strictEqual(admitBudget(c),c.result);} ],
  ['sequential baseline rejects shortened timeout',()=>{const c=budgetControl({raw:false});c.process.timeoutMs=239994;assert.throws(()=>admitBudget(c),/REUSE_REQUIRES_FULL_ORIGINAL_BUDGET/);} ],
  ['sequential baseline rejects expanded timeout',()=>{const c=budgetControl({raw:false});c.process.timeoutMs=240001;assert.throws(()=>admitBudget(c),/REUSE_REQUIRES_FULL_ORIGINAL_BUDGET/);} ],

  ['original typed source freeze includes every declared input before parent consumption',()=>{
   const repo=path.resolve(import.meta.dirname,'../../../..'),entry=path.join(repo,'tools/decky_binding_runtime_selftest.mjs'),text=fs.readFileSync(entry,'utf8');
   const begin=text.indexOf('function freeze(file){'),boundary=text.indexOf('\nfreeze(path.join(repo,',begin),seedEnd=text.indexOf('\n// Explicit focused counterexample',boundary);
   assert(begin>=0&&boundary>begin&&seedEnd>boundary,'ORIGINAL_FREEZE_FUNCTION_AND_SEEDS_REQUIRED');
   const virtualRoot=path.join(repo,'__source_membership_readonly_memory__'),memory=new Map(),pins=[],seen=new Set();
   const memoryFs={...fs,mkdirSync:()=>undefined,writeFileSync(file,value){const relative=path.relative(virtualRoot,path.resolve(file));assert(relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'SOURCE_CONTROL_CANNOT_WRITE_OUTSIDE_MEMORY');memory.set(path.resolve(file).toLowerCase(),Buffer.isBuffer(value)?Buffer.from(value):Buffer.from(value));},
    readFileSync(file,options){const bytes=memory.get(path.resolve(file).toLowerCase());if(!bytes)return fs.readFileSync(file,options);const encoding=typeof options==='string'?options:options?.encoding;return encoding?bytes.toString(encoding):Buffer.from(bytes);}};
   const context={repo,source:virtualRoot,pins,seen,search:[path.join(repo,'native'),path.join(repo,'deps/json'),repo],fs:memoryFs,path,assert,sha:bytes=>crypto.createHash('sha256').update(bytes).digest('hex')};
   vm.runInNewContext(text.slice(begin,boundary)+'\n'+text.slice(boundary+1,seedEnd),context,{timeout:2000});
   // Production freezes main.cpp separately as attribution, without recursing its includes.
   assert(text.includes("pins.push({path:'native/main.cpp'"),'ORIGINAL_MAIN_ATTRIBUTION_REQUIRED');const main=fs.readFileSync(path.join(repo,'native/main.cpp'));pins.push({path:'native/main.cpp',bytes:main.length,sha256:context.sha(main)});
   const actual=pins.map(row=>row.path).sort(),declared=[...OWNED_SOURCES].sort();
   assert.equal(new Set(actual).size,actual.length,'ORIGINAL_FREEZE_DUPLICATE_SOURCE');
   assert.deepEqual(actual,declared,'ORIGINAL_FROZEN_SOURCE_MEMBERSHIP_CHANGED');
   for(const entry of actual){const without=declared.filter(name=>name!==entry);assert.throws(()=>assert.deepEqual(actual,without,'ORIGINAL_FROZEN_SOURCE_MEMBERSHIP_CHANGED'));}
   assert.throws(()=>assert.deepEqual(actual,[...declared,declared[0]].sort(),'ORIGINAL_FROZEN_SOURCE_MEMBERSHIP_CHANGED'));
   assert.equal(memory.size,pins.length-1,'ORIGINAL_FREEZE_CONTROL_WRITES_ARE_MEMORY_ONLY');
  }],
  ['required-stage guard preserves successful stage order and return values',async()=>{
   const cases=[],failures=[],stops=[],trace=[],firstValue={stage:'first'},lastValue={stage:'last'};
   const guard=createRequiredStageGuard({cases,recordFailure:(id,error)=>{failures.push({id,error});cases.push({id,passed:false});},recordStop:row=>stops.push(row)});
   const first=await guard('first',async()=>{trace.push('first:enter');await Promise.resolve();cases.push({id:'first.ok',passed:true});trace.push('first:done');return firstValue;});
   const second=await guard('second',()=>{trace.push('second');cases.push({id:'second.ok',passed:true});return false;});
   const third=await guard('third',()=>{trace.push('third');cases.push({id:'third.ok',passed:true});});
   const last=await guard('last',()=>{trace.push('last');cases.push({id:'last.ok',passed:true});return lastValue;});
   assert.strictEqual(first,firstValue);assert.strictEqual(second,false);assert.strictEqual(third,undefined);assert.strictEqual(last,lastValue);
   assert.deepEqual(trace,['first:enter','first:done','second','third','last']);
   assert.deepEqual(cases,[{id:'first.ok',passed:true},{id:'second.ok',passed:true},{id:'third.ok',passed:true},{id:'last.ok',passed:true}]);
   assert.deepEqual(failures,[]);assert.deepEqual(stops,[]);
  }],
  ['required-stage guard rethrows the original required exception before later stages',async()=>{
   const cases=[],failures=[],stops=[],trace=[],originalError=new Error('required action failed');
   const guard=createRequiredStageGuard({cases,recordFailure:(id,error)=>{failures.push({id,error});cases.push({id,passed:false});},recordStop:row=>stops.push(row)});
   await assert.rejects(async()=>{
    await guard('prepare',()=>{trace.push('prepare');cases.push({id:'prepare.ok',passed:true});});
    await guard('required',async()=>{trace.push('required');throw originalError;});
    await guard('later',()=>{trace.push('later');cases.push({id:'later.ok',passed:true});});
   },error=>{assert.strictEqual(error,originalError);return true;});
   assert.deepEqual(trace,['prepare','required']);assert.deepEqual(cases,[{id:'prepare.ok',passed:true},{id:'required',passed:false}]);
   assert.equal(failures.length,1);assert.equal(failures[0].id,'required');assert.strictEqual(failures[0].error,originalError);
   assert.equal(stops.length,1);assert.equal(stops[0].id,'required');assert.deepEqual(stops[0].caseIds,['required']);assert.strictEqual(stops[0].error,originalError);
  }],
  ['required-stage guard stops on a newly recorded false semantic case without an action throw',async()=>{
   const cases=[],failures=[],stops=[],trace=[];let actionReturned=false,reportedError;
   const guard=createRequiredStageGuard({cases,recordFailure:(id,error)=>{failures.push({id,error});cases.push({id,passed:false});},recordStop:row=>stops.push(row)});
   await assert.rejects(async()=>{
    await guard('semantic',()=>{trace.push('semantic');cases.push({id:'semantic.prepared',passed:true},{id:'semantic.failed',passed:false});actionReturned=true;return 'action returned normally';});
    await guard('later',()=>{trace.push('later');cases.push({id:'later.ok',passed:true});});
   },error=>{assert(error instanceof Error);assert.equal(error.message,'REQUIRED_STAGE_REPORTED_FAILURE semantic');reportedError=error;return true;});
   assert.equal(actionReturned,true);assert.deepEqual(trace,['semantic']);assert.deepEqual(failures,[]);
   assert.deepEqual(cases,[{id:'semantic.prepared',passed:true},{id:'semantic.failed',passed:false}]);
   assert.equal(stops.length,1);assert.equal(stops[0].id,'semantic');assert.deepEqual(stops[0].caseIds,['semantic.failed']);assert.strictEqual(stops[0].error,reportedError);
  }],
  ['required-stage guard explicit collection retains failures and continues later stages',async()=>{
   const cases=[],failures=[],stops=[],trace=[],originalError=new Error('collected required failure'),semanticValue={stage:'semantic'},laterValue={stage:'later'};
   const guard=createRequiredStageGuard({cases,collectAfterFailure:true,recordFailure:(id,error)=>{failures.push({id,error});cases.push({id,passed:false});},recordStop:row=>stops.push(row)});
   const thrown=await guard('throwing',()=>{trace.push('throwing');throw originalError;});
   const semantic=await guard('semantic',()=>{trace.push('semantic');cases.push({id:'semantic.failed',passed:false});return semanticValue;});
   const later=await guard('later',()=>{trace.push('later');cases.push({id:'later.ok',passed:true});return laterValue;});
   assert.strictEqual(thrown,null);assert.strictEqual(semantic,semanticValue);assert.strictEqual(later,laterValue);
   assert.deepEqual(trace,['throwing','semantic','later']);assert.deepEqual(cases,[{id:'throwing',passed:false},{id:'semantic.failed',passed:false},{id:'later.ok',passed:true}]);
   assert.equal(failures.length,1);assert.equal(failures[0].id,'throwing');assert.strictEqual(failures[0].error,originalError);assert.deepEqual(stops,[]);
  }],
  ['required-stage guard continues after an expected negative handled inside the original action',async()=>{
   const cases=[],failures=[],stops=[],trace=[],handledValue={handled:true},laterValue={stage:'later'},expectedNegative=new Error('expected negative rejection');
   const guard=createRequiredStageGuard({cases,recordFailure:(id,error)=>{failures.push({id,error});cases.push({id,passed:false});},recordStop:row=>stops.push(row)});
   const handled=await guard('expected-negative',async()=>{
    trace.push('expected-negative');
    naturalExit({exitCode:2,elapsedMs:1,timeoutMs:10,error:null,signal:null,launchError:null,timedOut:false,outputTruncated:false},2);
    await assert.rejects(async()=>{throw expectedNegative;},error=>error===expectedNegative);
    cases.push({id:'expected-negative.handled',passed:true,observedExitCode:2,negativeHandled:true});return handledValue;
   });
   const later=await guard('later',()=>{trace.push('later');cases.push({id:'later.ok',passed:true});return laterValue;});
   assert.strictEqual(handled,handledValue);assert.strictEqual(later,laterValue);assert.deepEqual(trace,['expected-negative','later']);
   assert.deepEqual(cases,[{id:'expected-negative.handled',passed:true,observedExitCode:2,negativeHandled:true},{id:'later.ok',passed:true}]);
   assert.deepEqual(failures,[]);assert.deepEqual(stops,[]);
  }]
 ];
 const availableNames=directedControls.map(([name])=>name);
 if(requested.some(name=>!availableNames.includes(name)))throw Error('YMCC_PARENT_GLOBAL_CASE_FILTER_UNKNOWN');
 const selected=directedControls.filter(([name])=>requested.includes(name)),selectedNames=selected.map(([name])=>name),actualNames=[],results=[];
 assert.equal(selected.length,requested.length);
 for(const [name,action]of selected)test(name,async()=>{
  actualNames.push(name);
  try{await action();results.push({name,passed:true});}
  catch(error){results.push({name,passed:false,error:String(error?.message??error)});throw error;}
 });
 test.after(()=>{
  console.log('YMCC_PARENT_GLOBAL_REQUIRED_STAGE_CONTROL_RAW '+JSON.stringify({schemaVersion:1,scope:'REQUIRED_STAGE_FACTORY_CONTROLS_ONLY',selectedNames,actualNames,selectedCount:selectedNames.length,actualCount:actualNames.length,controls:results,businessExecuted:false,parentExecuted:false,baselineExecuted:false}));
  assert.deepEqual(actualNames,selectedNames);assert.equal(results.length,selectedNames.length);
 });
}else{
const repo=path.resolve(import.meta.dirname,'../../../..');
const core=path.resolve(repo,'../../../Testing/YMCC');
const evidence=path.join(core,'Evidence/DeckyParentMigration-20261023/c3-e02b');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const seal=read(path.join(evidence,'HANDOFF.json'));
assert.equal(hash(seal.actualEvidence.run.path),seal.actualEvidence.run.sha256,'ORIGINAL_R5_RECEIPT_CHANGED');
const original=read(seal.actualEvidence.run.path),clone=()=>structuredClone(original);
const processReceipt=()=>({exitCode:2,elapsedMs:40000,timeoutMs:285000,error:null,signal:null,timedOut:false,outputTruncated:false});
const controls=[];
function check(name,fn){test(name,()=>{fn();controls.push({name,passed:true});});}
check('original immutable r5 25/7 INCOMPLETE envelope is accepted only as selected child',()=>{
 assert.equal(validateCurrentEnvelope(original,processReceipt()),original);assert.equal(original.passed,false);assert.equal(original.status,'INCOMPLETE');assert.equal(EXACT_25.length,25);
});
for(const [name,edit]of [
 ['child exit0 cannot masquerade as expected exit2',(v,p)=>p.exitCode=0],
 ['child exit1 FAIL is retained',(v,p)=>p.exitCode=1],
 ['native access violation exit is rejected',(v,p)=>p.exitCode=3221225477],
 ['signal is never accepted',(v,p)=>p.signal='SIGTERM'],
 ['timeout is never accepted',(v,p)=>p.timedOut=true],
 ['truncated output is never accepted',(v,p)=>p.outputTruncated=true],
 ['scope status PASS cannot replace original INCOMPLETE',(v)=>v.status='PASS'],
 ['partial24 cannot pass with aggregate counts',(v)=>v.cases.pop()],
 ['extra unexpected failed case cannot be masked',(v)=>v.cases.push({id:'extra.failure',passed:false})],
 ['original negative FAIL remains FAIL',(v)=>v.cases[8].passed=false],
 ['duplicated original case cannot replace another',(v)=>v.cases[3]=v.cases[2]],
 ['unreviewed budget expansion is rejected',(v)=>v.originalBudgets.nativeMs=46000],
 ['missing seventh requirement is rejected',(v)=>v.requirements.pop()],
 ['changed requirement mapping is rejected',(v)=>v.requirements[0].caseIds.pop()],
 ['raw observation mutation is rejected',(v)=>v.requirements[0].observations[0].measurements={fake:true}],
 ['failed native subphase is rejected',(v)=>v.executions[2].exitCode=1],
 ['source drift is rejected',(v)=>{v.sourceUnchanged=false;v.sourceChanges=['native/main.cpp'];}],
 ['unexpected top error is not swallowed',(v)=>v.error='unexpected']
])check(name,()=>{const value=clone(),p=processReceipt();edit(value,p);assert.throws(()=>validateCurrentEnvelope(value,p));});
check('seven independent measurement aggregations retain case IDs and evidence scopes',()=>{
 const result=aggregateSeven(original);assert.equal(result.cases.length,7);assert.equal(new Set(result.cases.map(c=>c.id)).size,7);
 assert.deepEqual(result.observations.map(r=>r.requirement),REQUIREMENTS.map(r=>r[0]));
 assert.equal(result.cases[1].measurements.fallbackOwnedOnly,true);assert.equal(result.cases[6].measurements.ownedInertLoaderLaunches,12);
 assert.equal(result.cases[4].measurements.healthObserved,true);assert.equal(result.cases[4].measurements.committedReady.healthObserved,false);
});
for(const [name,id,edit]of [
 ['nonzero resource cannot become measured zero','native.exit-clears-owned-process-job-worker-event',m=>m.atExit.handles=1],
 ['thread count inequality cannot be waived','native.crash-budget-cannot-renew-on-recovery-or-unknown',m=>m.parentThreadsAfter=1],
 ['live sentinel in target Job rejects owned-only fallback','native.job-stop-failure-falls-back-to-owned-job-close',m=>m.sentinelInRuntimeJob=true],
 ['killed descendant by direct terminate is rejected','native.job-stop-failure-falls-back-to-owned-job-close',m=>m.directDescendantTerminateProcessCalls=1],
 ['fake fifth crash count cannot replace real five','native.crash-budget-cannot-renew-on-recovery-or-unknown',m=>m.attempts=4],
 ['actual crash exit-code mutation rejects budget proof','native.crash-budget-cannot-renew-on-recovery-or-unknown',m=>m.ownedInertProcesses[0].exitCode=0],
 ['late process can never become zero restarts','native.late-actual-recovery-callback-after-exit',m=>m.additionalStarts=1],
 ['connected-only cannot replace current health','consumer.sleep-rejects-write-and-automatically-publishes-current-ready',m=>m.healthObserved=false]
])check(name,()=>{const value=clone();edit(value.cases.find(r=>r.id===id).measurements);assert.throws(()=>aggregateSeven(value));});
check('historical frozen raw retains its verdict but cannot authorize the current expanded producer',()=>{
 const toolMap=new Map(original.toolArtifacts.map(r=>[path.resolve(r.path).toLowerCase(),r]));const sources=sourceMap(original.sourceInputs);
 const root=path.dirname(seal.actualEvidence.run.path),work=path.dirname(path.dirname(original.currentProducer.raw.path));
 const before=hash(seal.actualEvidence.run.path);
 assert.throws(()=>verifyCurrentDisk(original,sources,toolMap,root,work),'Old frozen producer/tool receipts cannot fabricate the current mandatory boundary');
 assert.equal(hash(seal.actualEvidence.run.path),before,'Historical verdict and bytes must remain unchanged');
});
check('child before-bind cannot be replaced by missing or post-hoc source digest',()=>{
 const map=sourceMap(original.sourceInputs);map.delete('native/decky_sidebar_runtime.h');
 assert.throws(()=>verifyCurrentDisk(original,map,new Map(original.toolArtifacts.map(r=>[path.resolve(r.path).toLowerCase(),r])),path.dirname(seal.actualEvidence.run.path),path.dirname(path.dirname(original.currentProducer.raw.path))));
});
check('single 480s deadline clips remaining allowance without resetting child budget',()=>{
 assert.deepEqual(allocateBudget(1000,2000,240000),{originalMs:240000,timeoutMs:240000,parentRemainingMs:479000,clipped:false});
 assert.equal(allocateBudget(1000,470000,240000).timeoutMs,11000);assert.throws(()=>allocateBudget(1000,481000,60000));
 assert.throws(()=>naturalExit({exitCode:0,elapsedMs:1001,timeoutMs:1000},0));
});
const fakeHash='a'.repeat(64),fakePath='native/baseline-control-only.h';
function baselineControl(){return {raw:{fieldRegression:'decky-update-disconnect-20261020',nativeChecks:1,passed:true,inputsUnchanged:true,sourceInputs:[{path:fakePath,sha256:fakeHash}],realSteamActions:0,userSettingsWrites:0,hardwareWrites:0},process:{id:'original.plugins.decky-update-recovery',exitCode:0,elapsedMs:123,timeoutMs:240000,originalBudgetMs:240000,boundSourceInputs:[{path:fakePath,sha256:fakeHash}]},before:new Map([[fakePath,fakeHash]])};}
check('baseline schema control only accepts exact original successful source-bound budget',()=>{const c=baselineControl();validateBaselineReceipt(c.raw,c.process,c.before,[fakePath]);});
for(const [name,edit]of [
 ['baseline original FAIL cannot be reused',c=>c.raw.passed=false],
 ['baseline old source cannot be reused',c=>c.raw.sourceInputs[0].sha256='b'.repeat(64)],
 ['baseline missing before-binding proof is rejected',c=>c.process.boundSourceInputs=[]],
 ['baseline missing original elapsed/process result is rejected',c=>delete c.process.elapsedMs],
 ['baseline clipped or expanded 240s contract is rejected',c=>c.process.timeoutMs=240001],
 ['baseline top fields cannot hide source-change',c=>c.raw.inputsUnchanged=false],
 ['baseline nonzero exit cannot be reused',c=>c.process.exitCode=1],
 ['baseline undeclared actual source is rejected',c=>c.raw.sourceInputs.push({path:'new.h',sha256:fakeHash})]
])check(name,()=>{const c=baselineControl();edit(c);assert.throws(()=>validateBaselineReceipt(c.raw,c.process,c.before,[fakePath]));});
check('source traversal and conflicting duplicate identity rejected',()=>{
 assert.throws(()=>sourceMap([{path:'../outside',sha256:fakeHash}]));
 assert.throws(()=>sourceMap([{path:'safe.h',sha256:fakeHash},{path:'safe.h',sha256:'b'.repeat(64)}],{duplicates:true}));
});
check('all old47 rows mapped while old three failures stay immutable',()=>{
 const old=read(path.join(core,'Evidence/DeckyParentMigration-20261023/a1-2547/history/result.json'));
 const before=structuredClone(old);const aggregate=aggregateSeven(original);const mapping=coverageMap(old,[...original.cases,...aggregate.cases,{id:'current.native.entry',passed:true}]);
 assert.equal(mapping.length,47);assert.deepEqual(old,before);assert.equal(mapping.filter(r=>r.oldPassed===false).length,3);
 assert(mapping.every(r=>r.originalFailurePromoted===false));assert.equal(mapping[24].relation,'HISTORICAL_CALLBACK_FAIL_RETAINED_TYPED_COUNTERPARTS_SEPARATE');
 assert.equal(mapping[0].currentCases[0].status,'NOT_RUN');const invalid=structuredClone(old);invalid.cases[24].passed=true;assert.throws(()=>coverageMap(invalid,[]));
});
check('candidate declaration binds vendor lock, exact regressions/tools and unchanged adapter budget',()=>{
 const plan=parentDeclaration(repo);assert(plan.evidenceInputs.includes('decky-plugin/vendor/decky-loader.lock.json'));for(const source of ["tools/decky_update_release_gate.ps1", "tools/decky-loader-upgrade.ps1", "decky-plugin/plugin.json", "decky-plugin/vendor/DECKY-API-LOCK.json", "decky-plugin/vendor/decky-api-1.1.3/dist/index.js", "decky-plugin/vendor/decky-api-1.1.3/dist/types.js", "decky-plugin/vendor/decky-api-1.1.3/LICENSE", "decky-plugin/vendor/decky-api-1.1.3/package.json"])assert(plan.evidenceInputs.includes(source),'metadata and complete official SDK closure must be prebound: '+source);assert(plan.evidenceInputs.includes('tools/ymcc_sim/global/capability-lifecycle/index.mjs'));
 const workflows=fs.readdirSync(path.join(repo,'.github/workflows')).filter(name=>/\.ya?ml$/.test(name));assert(workflows.includes('release.yml'));for(const name of workflows)assert(plan.evidenceInputs.includes('.github/workflows/'+name),'actual publication workflow source must be prebound: '+name);
 assert.equal(plan.timeoutMs,480000);assert(plan.artifactInputs.length<=3072);assert.equal(JS_REGRESSIONS.length,6);assert.equal(NATIVE_REGRESSIONS.length,6);
 assert.equal(BUDGETS.baseline,240000);assert.equal(plan.requiredComponentsAreNotMutated,true);assert(plan.resultFiles.includes('decky-persistent-lifecycle/result.json'));
 const gaps=legacyMigration(repo);assert(gaps.length>0&&gaps.every(g=>g.promotionAllowed===false&&g.originalCompileOrExecutionFailureNeverMasked));
});
test.after(()=>{
 if(process.env.YMCC_PARENT_GLOBAL_CONTROL_RESULT){const output=path.resolve(process.env.YMCC_PARENT_GLOBAL_CONTROL_RESULT);assert(output.startsWith(path.join(core,'Evidence/DeckyParentGlobal-20261023')+path.sep));fs.writeFileSync(output,JSON.stringify({schemaVersion:1,scope:'FINITE_RECEIPT_REJECTION_AND_MAPPING_CONTROLS_ONLY',businessExecuted:false,parentExecuted:false,baseline240sExecuted:false,controls,completedControls:controls.length,originalR5Receipt:seal.actualEvidence.run},null,2)+'\n');}
});
check('current child exact tool scope excludes parent-only package compiler inputs',()=>{
 const a=path.join(core,'Workspace','inert-current-compiler.exe'),b=path.join(core,'Workspace','inert-parent-package-compiler.exe');
 const pin={path:a,sha256:'a'.repeat(64)},extra={path:b,sha256:'b'.repeat(64)};
 const all=new Map([[path.resolve(a).toLowerCase(),pin],[path.resolve(b).toLowerCase(),extra]]);
 const selected=currentChildToolPins({currentChildArtifactInputs:[a]},all);assert.equal(selected.size,1);assert.equal(selected.get(path.resolve(a).toLowerCase()),pin);assert(!selected.has(path.resolve(b).toLowerCase()));assert.equal(all.size,2);
});
check('current tool scope cannot admit unknown paths, duplicates or fall back to the union',()=>{
 const p=path.join(core,'Workspace','inert-one.exe'),map=new Map([[path.resolve(p).toLowerCase(),{path:p,sha256:'a'.repeat(64)}]]);
 for(const values of [[],[p,p],[p+'-missing']])assert.throws(()=>currentChildToolPins({currentChildArtifactInputs:values},map));
 assert.throws(()=>currentChildToolPins({},map));
});
check('real parent pins the package compiler while native child keeps its original explicit scope',()=>{
 const d=parentDeclaration(repo),compiler='c:/windows/microsoft.net/framework64/v4.0.30319/csc.exe';
 assert(d.artifactInputs.some(p=>path.resolve(p).replaceAll('\\','/').toLowerCase()===compiler));
 assert(!d.currentChildArtifactInputs.some(p=>path.resolve(p).replaceAll('\\','/').toLowerCase()===compiler));
 assert(d.currentChildArtifactInputs.length>0&&d.artifactInputs.length<=3072);
});
test('private mutation of the compiler manifest is refused before any compilation',async()=>{
 const root=path.join(core,'Workspace/DeckyPackageBudgetIntegration-20261023');fs.mkdirSync(root,{recursive:true});const temp=fs.mkdtempSync(path.join(root,'authority-negative-'));
 const modulePath=path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/package-fixture-tools.mjs'),manifest=path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/package-fixture-tools.json');
 fs.copyFileSync(modulePath,path.join(temp,'package-fixture-tools.mjs'));const value=JSON.parse(fs.readFileSync(manifest));value.scope+=' mutation-control';fs.writeFileSync(path.join(temp,'package-fixture-tools.json'),JSON.stringify(value));
 const {pathToFileURL}=await import('node:url');const copied=await import(pathToFileURL(path.join(temp,'package-fixture-tools.mjs')));assert.throws(()=>copied.verifyPackageFixtureTools(),/PACKAGE_FIXTURE_MANIFEST_CHANGED/);
});

// Targeted receipt controls; no additional business producer/parent is added.
const zeroDeferredResource=()=>({fullyDestroyed:true,handles:0,pendingIo:0,activeCalls:0,jobActiveProcesses:0,childExited:true,ownerHandles:0,ownerPendingIo:0,readerHandles:0,readerPendingIo:0,clientHandles:0});
const deferredResourceRow=(variant='recover')=>{
 const heals=['recover','ready'].includes(variant);
 return {id:'native.typed-history-resource-'+variant,afterDeferredBootstrap:{passed:true,variant,stage:'resource-revalidation-after-deferred-bootstrap',plansAtRejectedPreflight:2,resourcesAtRejectedPreflight:zeroDeferredResource(),atExit:zeroDeferredResource(),beforeHandles:174,afterHandles:174,cefUnaffected:true,originalStopDeadlineMs:3200,stopElapsedMs:1,recovered:heals,bindingPlans:heals?3:2,preflightReads:variant==='recover'?4:heals?3:2,loaderPid:heals?123:0,phase:heals?'loader-started':variant==='off'?'disabled':variant==='admission'?'waiting-manual-steam':'unavailable',reason:variant==='hash'?'resource-hash-mismatch':variant==='cleanup'?'resource-worker-cleanup-incomplete':'',elapsedFromFailureMs:3010}};
};
check('deferred resource receipt requires all original variant boundaries',()=>{for(const variant of ['recover','ready','off','hash','cleanup','admission'])assert.equal(verifyDeferredResourceRevalidation(deferredResourceRow(variant)).variant,variant);});
for(const [name,edit]of [
 ['missing second-preflight evidence',r=>delete r.afterDeferredBootstrap],
 ['earlier successful resource read cannot replace deferred proof',r=>r.afterDeferredBootstrap.preflightReads=2],
 ['retry-debt refill',r=>r.afterDeferredBootstrap.elapsedFromFailureMs=2000],
 ['retained binding handles',r=>r.afterDeferredBootstrap.resourcesAtRejectedPreflight.handles=1],
 ['stop deadline expansion',r=>r.afterDeferredBootstrap.originalStopDeadlineMs=3300],
 ['stop elapsed over original budget',r=>r.afterDeferredBootstrap.stopElapsedMs=3201],
 ['stale variant identity',r=>r.afterDeferredBootstrap.variant='hash'],
 ['external CEF killed',r=>r.afterDeferredBootstrap.cefUnaffected=false],
 ['owned resource remains at exit',r=>r.afterDeferredBootstrap.atExit.activeCalls=1]
])check('deferred resource receipt rejects '+name,()=>{const row=deferredResourceRow();edit(row);assert.throws(()=>verifyDeferredResourceRevalidation(row));});
for(const variant of ['off','hash','cleanup','admission'])check('deferred resource receipt refuses unsafe recovery '+variant,()=>{const row=deferredResourceRow(variant);row.afterDeferredBootstrap.recovered=true;assert.throws(()=>verifyDeferredResourceRevalidation(row));});

check('deferred resource child execution remains same-budget and fully released',()=>{
 const good={selected:true,childExecutions:1,pid:123,exitCode:0,jobActiveProcesses:0,ownedParentHandlesAfterClose:0,parentReaderThreads:0,originalSharedOuterBudgetMs:45000,cases:6,elapsedMs:10000,parentObservationElapsedMs:35000,image:'current-source-built-test.exe',raw:'owned/result.json'};
 assert.equal(verifyDeferredResourceExecution(good),good);
 for(const patch of [{selected:false},{childExecutions:2},{exitCode:1},{jobActiveProcesses:1},{ownedParentHandlesAfterClose:1},{parentReaderThreads:1},{originalSharedOuterBudgetMs:45001},{elapsedMs:45001},{parentObservationElapsedMs:45001},{cases:5},{pid:0},{raw:''}])assert.throws(()=>verifyDeferredResourceExecution({...good,...patch}));
});

// These are finite original-verifier controls, not a new business parent/ID.
const jointDeferredEnvelope=()=>{
 const nested=['recover','ready','off','hash','cleanup','admission'].map(variant=>({...deferredResourceRow(variant),passed:true}));
 const cases=[...originalCoreCases.map(row=>({id:row.id,passed:true})),...nested];
 return {schema:'ymcc.typed-binding-runtime-gate.v1',tests:cases.length,passedCount:cases.length,cases,passed:true,selection:'',deadlineMs:3200,productCallbackMode:'typed-only; legacy macro absent',realSteamOrDeviceActions:0,originalHistoricalFailurePreserved:true,fieldWakeOrS4Verified:false,hostPackagePassed:false,deferredResourceExecution:{selected:true,childExecutions:1,pid:123,exitCode:0,jobActiveProcesses:0,ownedParentHandlesAfterClose:0,parentReaderThreads:0,originalSharedOuterBudgetMs:45000,cases:6,elapsedMs:10000,parentObservationElapsedMs:35000,image:'test-current.exe',raw:'owned/result.json'}};
};
const jointDeferredRun=()=>({selection:null,runExit:0,error:null,compilerExit:0,changedOriginalInputs:[],status:'pass'});
check('joint deferred boundary complete original aggregation consumes exact current six',()=>{assert.equal(validateCurrentOriginal(jointDeferredEnvelope(),jointDeferredRun()).completeCurrentProducer,true);});
for(const [name,change]of [
 ['whole group omitted',r=>{delete r.deferredResourceExecution;for(const row of r.cases)delete row.afterDeferredBootstrap;}],
 ['one nested boundary omitted',r=>delete r.cases.find(row=>row.afterDeferredBootstrap).afterDeferredBootstrap],
 ['invalid nested resource count',r=>r.cases.find(row=>row.afterDeferredBootstrap).afterDeferredBootstrap.atExit.handles=1],
 ['child exit failure hidden by outerPASS',r=>r.deferredResourceExecution.exitCode=1],
 ['incomplete childJob cleanup',r=>r.deferredResourceExecution.jobActiveProcesses=1],
 ['a raw legacy flag cannot waive current proof',r=>{delete r.deferredResourceExecution;r.allowLegacy=true;r.sourcePredatesDeferredResource=true;}]
])check('joint deferred boundary refuses '+name,()=>{const r=jointDeferredEnvelope();change(r);assert.throws(()=>validateCurrentOriginal(r,jointDeferredRun()));});
check('joint deferred boundary retains original producerFAIL instead of forcing missing new success evidence',()=>{const r=jointDeferredEnvelope();r.cases[0].passed=false;r.passed=false;r.passedCount--;delete r.deferredResourceExecution;const summary=validateCurrentOriginal(r,{...jointDeferredRun(),runExit:1,status:'fail'});assert.equal(summary.failed,1);});
check('joint deferred boundary proof reads original owned raw and rejects identities or nested edits',()=>{
 const base=path.join(core,'Workspace/DeckyParentGlobal-20261023');fs.mkdirSync(base,{recursive:true});const dir=fs.mkdtempSync(path.join(base,'deferred-proof-control-')),file=path.join(dir,'runtime-fixtures/deferred-resource-branch/result.json');fs.mkdirSync(path.dirname(file),{recursive:true});
 const r=jointDeferredEnvelope();r.deferredResourceExecution.image=path.join(dir,'FixtureSteam/steam.exe').replaceAll('\\','/').toLowerCase();r.deferredResourceExecution.raw=file;
 const child={pid:123,sourceImage:r.deferredResourceExecution.image,elapsedMs:10000,cases:Object.fromEntries(r.cases.filter(row=>row.afterDeferredBootstrap).map(row=>[row.afterDeferredBootstrap.variant,row.afterDeferredBootstrap]))};
 fs.writeFileSync(file,JSON.stringify(child));assert.equal(verifyDeferredResourceProof(r,dir).raw.sha256,hash(file));
 for(const edit of [c=>c.pid++,c=>c.sourceImage+='foreign',c=>c.cases.recover.atExit.handles=1,c=>delete c.cases.cleanup]){const c=structuredClone(child);edit(c);fs.writeFileSync(file,JSON.stringify(c));assert.throws(()=>verifyDeferredResourceProof(r,dir));}
 fs.writeFileSync(file,JSON.stringify(child));const wrong=structuredClone(r);wrong.deferredResourceExecution.raw=path.join(dir,'not-original.json');assert.throws(()=>verifyDeferredResourceProof(wrong,dir));
});

check('joint deferred historical older raw is not promoted to current acceptance or silently patched',()=>{
 const historic=read(original.originalProducer.raw.path),run=read(original.originalProducer.result.path),before=hash(original.originalProducer.raw.path);
 assert.throws(()=>validateCurrentOriginal(historic,run));assert.equal(hash(original.originalProducer.raw.path),before);
});
}
