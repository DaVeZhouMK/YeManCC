import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {REPO,MODULE,inputFiles,toolchainSpec,getSleepSessionMarkerFileConsumerAdapters} from './adapter.mjs';
import {qualifySessionMarkerJob,verifySessionMarkerPinnedSources,verifySessionMarkerSourceCapture} from './run.mjs';
test('session marker owned Job requires observed zero and natural root return before cleanup',()=>{
 const job={status:0,signal:null,stdout:'YMCC_SIM_OWNED_JOB childPid=44\r\n'},started=1000,timeout=360000;
 const receipt={schemaVersion:1,resourceObservationVersion:1,status:'EXITED',exitCode:0,rootExitCode:0,childPid:44,jobAssigned:true,childExitedBeforeReturn:true,deadlineEpochMs:0,cleanupReserveMs:0,deadlineExpired:false,finishedEpochMs:1050,jobQueryCount:2,activeProcessesBeforeCleanup:0,activeProcessesFinal:0,zeroObservedBeforeDispose:true,terminationRequested:false,terminationSucceeded:false,cleanupError:null,jobDisposed:true};
 const run=(proc=job,raw=receipt)=>qualifySessionMarkerJob(proc,raw,started,timeout);
 const refused=/SESSION_MARKER_FILES_OWNED_JOB_NOT_RETIRED/;
 assert.equal(run(),true);
 assert.equal(run(job,{...receipt,activeProcessesBeforeCleanup:1,terminationRequested:true,terminationSucceeded:true}),false);
 assert.equal(run({...job,status:23},{...receipt,status:'FAILED',exitCode:23,rootExitCode:23}),false);
 for(const [field,value] of Object.entries({jobAssigned:false,childExitedBeforeReturn:false,childPid:45,jobQueryCount:1,activeProcessesBeforeCleanup:1,activeProcessesFinal:1,zeroObservedBeforeDispose:false,terminationRequested:true,terminationSucceeded:true,cleanupError:'QUERY_FAILED',jobDisposed:false,deadlineExpired:true,deadlineEpochMs:361000,cleanupReserveMs:1000,finishedEpochMs:361001,rootExitCode:23,status:'FAILED'}))assert.throws(()=>run(job,{...receipt,[field]:value}),refused,field);
 assert.throws(()=>run(job,null),refused);
 assert.throws(()=>run({...job,stdout:job.stdout.repeat(2)}),refused);
 assert.throws(()=>run({...job,stdout:'prefix '+job.stdout}),refused);
 assert.throws(()=>run({...job,signal:'SIGTERM'}),refused);
 assert.throws(()=>run({...job,error:Error('ETIMEDOUT')}),refused);
});
const read=n=>JSON.parse(fs.readFileSync(path.join(REPO,MODULE,n),'utf8'));
test('actual session file consumer preserves17 normative groups19 stages and9 exact semantic fault sets',()=>{const spec=read('spec.json'),plan=read('scenario-plan.json');assert.equal(plan.cases.length,17);assert.equal(plan.cases.reduce((n,c)=>n+c.stages.length,0),19);assert.equal(spec.reviewedFunctions.length,7);assert.equal(spec.reviewedFunctions.filter(x=>x.role==='new-target').length,4);assert.equal(spec.variants.length,10);assert.deepEqual(spec.variants.slice(1).map(x=>x.expectedFailedCases.length),[2,4,2,4,2,1,7,4,2]);for(const v of spec.variants)for(const id of v.expectedFailedCases)assert(plan.cases.some(c=>c.id===id));});
test('positive uses captured repaired current repository source and only negatives may mutate one complete function',()=>{const spec=read('spec.json');assert.equal(spec.positiveSourceCandidateInjectionAllowed,false);assert.deepEqual(spec.variants[0].sourcePatches,[]);for(const v of spec.variants.slice(1)){assert.equal(v.sourcePatches.length,1);const p=v.sourcePatches[0];assert(spec.reviewedFunctions.some(f=>f.name===p.function));assert.notEqual(p.before,p.after);}assert.equal(spec.directConcurrentHelperAccepted,false);assert.equal(spec.concurrentExitFastPathAccepted,false);});
test('source closure is 34 complete repository members and 562 pinned tools with one portable adapter',()=>{const [a]=getSleepSessionMarkerFileConsumerAdapters();assert.equal(a.id,'sleep.session-marker-file-consumer');assert.equal(inputFiles().length,34);assert.equal(toolchainSpec().files.length,562);for(const f of ["tools/ymcc_sim/global/sleep-native-recovery-consumer/shared-leaf-successor.json","tools/ymcc_sim/global/sleep-native-recovery-consumer/shared-leaf-successor.mjs"])assert(a.evidenceInputs.includes(f),f);assert.equal(new Set(a.evidenceInputs).size,34);for(const p of ['native/log_snapshot_state.h','native/sleep_recovery_record_name.h'])assert(a.evidenceInputs.includes(p),p);for(const p of a.evidenceInputs)assert(fs.statSync(path.join(REPO,p)).isFile());assert.deepEqual(a.resultFiles,['session-marker-files/result.json']);for(const n of ['adapter.mjs','run.mjs','worker.mjs','spec.json','scenario-plan.json'])assert(!/D:[/\\].*(?:Codex|private-)/i.test(fs.readFileSync(path.join(REPO,MODULE,n),'utf8')),n);});
test('reviewed adapter successors require their exact live bytes in the captured source set',()=>{
 const specPath=MODULE+'/spec.json';
 const readSource=relative=>fs.readFileSync(path.join(REPO,relative));
 const specBytes=readSource(specPath);
 const {captureIdentities,sourceReview}=verifySessionMarkerPinnedSources(specPath,specBytes,readSource);
 const nonUserWake='tools/ymcc_sim/global/sleep-non-user-wake-consumer/adapter.mjs';
 assert.equal(sourceReview.sourceApproval,false);
 assert.equal(sourceReview.exactAdapterSuccessors.length,2);
 assert(!sourceReview.exactAdapterSuccessors.some(row=>row.path===nonUserWake));
 const shared=sourceReview.sharedLeafReview;
 assert.equal(shared.sourceApproval,false);
 assert.equal(shared.compiledHistoricalBytes,false);
 const changed=shared.exactSuccessors.find(row=>row.path===nonUserWake);
 const declared=JSON.parse(specBytes).reviewedSharedLeafIdentities.find(row=>row.path===nonUserWake);
 assert.equal(changed?.predecessorSha256,declared.sha256);
 assert.notEqual(changed.currentSha256,changed.predecessorSha256);
 assert.equal(captureIdentities.get(nonUserWake),changed.currentSha256);
 const inputs=[...captureIdentities].map(([path,sha256])=>({path,sha256}));
 verifySessionMarkerSourceCapture(inputs,captureIdentities);
 for(const adapter of [...sourceReview.exactAdapterSuccessors,changed]){
  const name=adapter.path;
  const refusal='SESSION_MARKER_FILES_SHARED_LEAF_CHANGED:'+name;
  const expected=name===nonUserWake?error=>{
   assert(error instanceof assert.AssertionError);
   assert.equal(error.message.split('\n',1)[0],refusal);
   assert.equal(error.actual,0);
   assert.equal(error.expected,1);
   return true;
  }:{message:refusal};
  assert.throws(()=>verifySessionMarkerPinnedSources(specPath,specBytes,relative=>relative===name?Buffer.concat([readSource(name),Buffer.from('\n')]):readSource(relative)),expected);
  assert.throws(()=>verifySessionMarkerSourceCapture(inputs.filter(row=>row.path!==name),captureIdentities),{message:'SESSION_MARKER_FILES_CAPTURED_REVIEW_CHANGED:'+name});
  const oldInputs=inputs.map(row=>row.path===name?{...row,sha256:adapter.predecessorSha256}:row);
  assert.throws(()=>verifySessionMarkerSourceCapture(oldInputs,captureIdentities),{message:'SESSION_MARKER_FILES_CAPTURED_REVIEW_CHANGED:'+name});
 }
});
