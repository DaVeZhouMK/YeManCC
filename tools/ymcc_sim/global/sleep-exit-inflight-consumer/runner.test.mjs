import test from 'node:test';import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {REPO,MODULE,inputFiles,toolchainSpec,getSleepExitInflightConsumerAdapters} from './adapter.mjs';const read=n=>JSON.parse(fs.readFileSync(path.join(REPO,MODULE,n),'utf8'));
test('inflight temporal consumer preserves two real-thread scenarios and exact three source faults',()=>{const spec=read('spec.json'),plan=read('scenario-plan.json');assert.equal(plan.cases.length,2);assert.equal(plan.cases.reduce((n,c)=>n+c.stages.length,0),2);assert.equal(plan.ownedThreadsPerNativeStage,2);assert.equal(spec.reviewedFunctions.length,7);assert.equal(spec.variants.length,4);assert.deepEqual(spec.variants.slice(1).map(v=>v.expectedFailedCases.length),[2,1,2]);assert.deepEqual(spec.variants[0].sourcePatches,[]);assert(spec.variants.slice(1).every(v=>v.sourcePatches.length===1&&v.sourcePatches[0].function==='sgCleanupBeforeExit'));assert.equal(spec.atomicTypeObservationIsNotSemanticPassPredicate,true);assert.equal(spec.blockingWaiterAndSessionCancelRaceAccepted,false);});
test('inflight uses one current-source adapter with 34 source members and 562 pinned tools',()=>{const [a]=getSleepExitInflightConsumerAdapters();assert.equal(a.id,'sleep.exit-inflight-consumer');assert.equal(inputFiles().length,34);assert.equal(toolchainSpec().files.length,562);for(const f of ["tools/ymcc_sim/global/sleep-native-recovery-consumer/shared-leaf-successor.json","tools/ymcc_sim/global/sleep-native-recovery-consumer/shared-leaf-successor.mjs"])assert(a.evidenceInputs.includes(f),f);assert.equal(new Set(a.evidenceInputs).size,34);for(const p of a.evidenceInputs)assert(fs.statSync(path.join(REPO,p)).isFile());for(const p of ['native/log_snapshot_state.h','native/sleep_recovery_record_name.h'])assert(a.evidenceInputs.includes(p),p);for(const n of ['adapter.mjs','run.mjs','worker.mjs','spec.json','scenario-plan.json'])assert(!/D:[/\\].*(?:Codex|private-)/i.test(fs.readFileSync(path.join(REPO,MODULE,n),'utf8')),n);});

import {createHash} from 'node:crypto';import {pathToFileURL} from 'node:url';import {verifyCompilerSourceReview,verifyInheritedCompilerRules,verifyWorkerEsmCoverage} from './compiler-source-review.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';
import {qualifyExitInflightJob} from './run.mjs';
test('inflight Job return permits archive only after observed zero',()=>{
 const started=1000,timeout=360000,proc={status:0,error:null,signal:null,stdout:'YMCC_SIM_OWNED_JOB childPid=44\n'};
 const raw={schemaVersion:1,resourceObservationVersion:1,status:'EXITED',exitCode:0,rootExitCode:0,childPid:44,jobAssigned:true,childExitedBeforeReturn:true,deadlineEpochMs:0,cleanupReserveMs:0,deadlineExpired:false,finishedEpochMs:1050,jobQueryCount:2,activeProcessesBeforeCleanup:0,activeProcessesFinal:0,zeroObservedBeforeDispose:true,terminationRequested:false,terminationSucceeded:false,cleanupError:null,jobDisposed:true};
 const run=(receipt=raw,job=proc)=>qualifyExitInflightJob(job,receipt,started,timeout);
 assert.equal(run(),true);
 assert.equal(run({...raw,activeProcessesBeforeCleanup:2,terminationRequested:true,terminationSucceeded:true}),false);
 assert.equal(run({...raw,exitCode:1,rootExitCode:1,status:'FAILED'},{...proc,status:1}),false);
 for(const receipt of [null,{...raw,childPid:45},{...raw,jobAssigned:false},{...raw,childExitedBeforeReturn:false},{...raw,jobQueryCount:1},{...raw,finishedEpochMs:999},{...raw,finishedEpochMs:361001},{...raw,activeProcessesFinal:1},{...raw,zeroObservedBeforeDispose:false},{...raw,activeProcessesBeforeCleanup:2},{...raw,rootExitCode:1},{...raw,cleanupError:'QUERY_FAILED'},{...raw,jobDisposed:false}])assert.throws(()=>run(receipt),/EXIT_INFLIGHT_OWNED_JOB_NOT_RETIRED/);
 for(const job of [{...proc,stdout:'childPid=44'},{...proc,stdout:proc.stdout+proc.stdout},{...proc,status:124},{...proc,error:{code:'ETIMEDOUT'}},{...proc,signal:'SIGTERM'}])assert.throws(()=>run(raw,job),/EXIT_INFLIGHT_OWNED_JOB_NOT_RETIRED/);
});
const reviewRelative=MODULE+'/compiler-source-review.json',review=JSON.parse(fs.readFileSync(path.join(REPO,reviewRelative),'utf8'));
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
function reviewSource(){
 const explicit=process.env.YMCC_SLEEP_EXIT_REVIEW_ROOT;
 if(explicit===undefined)return REPO;
 assert(path.isAbsolute(explicit),'Historical compiler review root must be absolute');
 const root=path.resolve(explicit);fixtureDirectoryIdentity(root);return root;
}
let reviewScratch,removeReviewScratch;
test.after(()=>removeReviewScratch?.());
function mutationReviewRoot(){
 if(!reviewScratch){
  const source=reviewSource();
  // A drifting baseline must fail before the expected-error assertion can run.
  verifyCompilerSourceReview(source);
  const parent=simulationCorePath('Workspace'),parentIdentity=fixtureDirectoryIdentity(parent),prefix='sleep-exit-review-';
  const root=fs.mkdtempSync(path.join(parent,prefix)),rootIdentity=fixtureDirectoryIdentity(root);
  removeReviewScratch=()=>{
   assert.equal(path.dirname(root),parent);assert(path.basename(root).startsWith(prefix));
   assert.deepEqual(fixtureDirectoryIdentity(parent),parentIdentity);
   assert.deepEqual(fixtureDirectoryIdentity(root),rootIdentity);
   fs.rmSync(root,{recursive:true});
  };
  for(const relative of [reviewRelative,review.leaf,review.sharedWorker.path,...review.staticDependencies.map(row=>row.path)]){
   const target=path.join(root,relative);
   fs.mkdirSync(path.dirname(target),{recursive:true});
   fs.copyFileSync(path.join(source,relative),target,fs.constants.COPYFILE_EXCL);
  }
  reviewScratch=root;
 }
 verifyCompilerSourceReview(reviewScratch);
 return reviewScratch;
}
function withReviewMutation(relative,suffix,check){
 const root=mutationReviewRoot(),file=path.join(root,relative),saved=file+'.original-test-input';
 assert(!fs.existsSync(saved));
 fs.renameSync(file,saved);
 try{
  if(suffix!==null)fs.writeFileSync(file,Buffer.concat([fs.readFileSync(saved),Buffer.from(suffix)]),{flag:'wx'});
  check(root,file);
 }finally{
  if(fs.existsSync(file))fs.unlinkSync(file);
  fs.renameSync(saved,file);
  verifyCompilerSourceReview(root);
 }
}
test('review reconstructs the exact prior compiler bytes and preserves inherited function text',()=>{const proof=verifyCompilerSourceReview(reviewSource());assert.equal(proof.beforeSha256,'55ea54805704339adf3ff6bdb31f946b6d027d77913e97534c2fce47786bc77b');assert.equal(proof.exactReverseBytes,true);assert.equal(proof.inheritedExports.length,2);assert.equal(proof.calibrationInvocationAllowed,false);});
test('changed compiler leaf refuses independently of an updated caller spec hash',()=>{
 withReviewMutation(review.leaf,'\n// unreviewed\n',root=>assert.throws(()=>verifyCompilerSourceReview(root),/COMPILER_REVIEW_SOURCE_CHANGED/));
});
test('missing mandatory top-level ESM helper refuses',()=>{
 withReviewMutation(review.staticDependencies[0].path,null,(root,file)=>assert.throws(()=>verifyCompilerSourceReview(root),{code:'ENOENT',path:file}));
});
test('modified byte-delta proof refuses',()=>{
 withReviewMutation(reviewRelative,' ',root=>assert.throws(()=>verifyCompilerSourceReview(root),/COMPILER_REVIEW_MANIFEST_CHANGED/));
});
test('changed actual inherited compiler or environment rule cannot pass source-only review',()=>{
 const root=reviewSource(),proof=verifyCompilerSourceReview(root);
 const worker=fs.readFileSync(path.join(root,proof.sharedWorker.path),'utf8').replace(/\r\n/g,'\n');
 function originalRule(name,next){
  const start=worker.indexOf('function '+name+'(out){'),end=worker.indexOf(next,start);
  assert(start>=0&&end>start);
  const exactOriginalFunctionText=worker.slice(start,end).trim();
  return {provenance:{function:name,policyCopiedOrModified:false,sourceSha256:proof.sharedWorker.sha256,
   exactFunctionSha256:sha(exactOriginalFunctionText),exactOriginalFunctionText}};
 }
 const compiler=originalRule('archiveCompilerInputs','\ntry{'),environment=originalRule('environment','\nfunction execute(');
 assert.equal(verifyInheritedCompilerRules(compiler,environment,proof),true);
 const changed=rule=>({provenance:{...rule.provenance,exactOriginalFunctionText:rule.provenance.exactOriginalFunctionText+'\n// unreviewed'}});
 assert.throws(()=>verifyInheritedCompilerRules(changed(compiler),environment,proof),{message:'COMPILER_REVIEW_ACTUAL_INHERITED_RULE_CHANGED:archiveCompilerInputs'});
 assert.throws(()=>verifyInheritedCompilerRules(compiler,changed(environment),proof),{message:'COMPILER_REVIEW_ACTUAL_INHERITED_RULE_CHANGED:environment'});
});
// These are deliberately synthetic refusal fixtures, never execution evidence.
// The production proof must come from each real worker's original V8 receipt.
function fixtureDirectoryIdentity(dir){
 const stat=fs.lstatSync(dir,{bigint:true});
 assert(stat.isDirectory()&&!stat.isSymbolicLink()&&path.relative(dir,fs.realpathSync.native(dir))==='','Redirected fixture directory: '+dir);
 return [stat.dev,stat.ino];
}
function coverageFixture(t){
 const parent=simulationCorePath('Workspace'),parentIdentity=fixtureDirectoryIdentity(parent),prefix='sleep-exit-coverage-';
 const root=fs.mkdtempSync(path.join(parent,prefix)),rootIdentity=fixtureDirectoryIdentity(root);
 t.after(()=>{
  assert.equal(path.dirname(root),parent);assert(path.basename(root).startsWith(prefix));
  assert.deepEqual(fixtureDirectoryIdentity(parent),parentIdentity);
  assert.deepEqual(fixtureDirectoryIdentity(root),rootIdentity);
  fs.rmSync(root,{recursive:true});
 });
 const sources=[MODULE+'/worker.mjs',MODULE+'/compiler-source-review.mjs','tools/ymcc_sim/global/sleep-native-recovery-consumer/scratch.mjs',review.leaf,...review.staticDependencies.map(x=>x.path)];
 const input=path.join(root,'input-sources');
 for(const relative of sources){
  assert(!path.isAbsolute(relative)&&!relative.split(/[\\/]/).includes('..'));
  const dest=path.join(input,relative);
  fs.mkdirSync(path.dirname(dest),{recursive:true});
  fs.copyFileSync(path.join(REPO,relative),dest,fs.constants.COPYFILE_EXCL);
 }
 const raw={result:sources.map(relative=>({url:pathToFileURL(path.join(input,relative)).href,functions:[{functionName:'',ranges:[{count:1}]},...(relative===review.leaf?['loadOriginalCompilerLedger','loadOriginalEnvironment',...review.forbiddenFunctionCalls].map(name=>({functionName:name,ranges:[{count:name.startsWith('loadOriginal')?1:0}]})):relative===review.staticDependencies[0].path?[{functionName:'calibrationToolchainPreamble',ranges:[{count:0}]}]:[])]}))};
 const snapshot={module:MODULE,sources:sources.map(relative=>({path:relative,sha256:sha(fs.readFileSync(path.join(input,relative)))}))};
 const directory=path.join(root,'esm-coverage');fs.mkdirSync(directory);
 const save=()=>fs.writeFileSync(path.join(directory,'coverage-fixture.json'),JSON.stringify(raw));
 save();return{root,raw,snapshot,save};
}
test('declaring helper metadata cannot replace its actual evaluated ESM receipt',t=>{const f=coverageFixture(t);f.raw.result=f.raw.result.filter(x=>!x.url.includes('hc_reviewed_calibration'));f.save();assert.throws(()=>verifyWorkerEsmCoverage(f.root,f.snapshot),/COMPILER_REVIEW_ESM_CLOSURE_INCOMPLETE/);});
test('unrelated calibration invocation is refused even with a complete declared module closure',t=>{const f=coverageFixture(t);f.raw.result.find(x=>x.url.includes('hc_reviewed_calibration')).functions[1].ranges[0].count=1;f.save();assert.throws(()=>verifyWorkerEsmCoverage(f.root,f.snapshot),/COMPILER_REVIEW_UNRELATED_CALIBRATION_CALLED/);});
test('missing original exit coverage is never inferred from a worker PASS',t=>{const f=coverageFixture(t);fs.unlinkSync(path.join(f.root,'esm-coverage/coverage-fixture.json'));assert.throws(()=>verifyWorkerEsmCoverage(f.root,f.snapshot),/COMPILER_REVIEW_ESM_COVERAGE_MISSING_OR_EXTRA/);});
