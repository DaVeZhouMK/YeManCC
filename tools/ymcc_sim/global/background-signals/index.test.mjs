import test from 'node:test';
import assert from 'node:assert/strict';
import {promises as fs} from 'node:fs';
import path from 'node:path';
import {realpathSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import {runBackgroundRegressions,validateBackgroundReceipt,snapshotBackgroundInputs,verifyBackgroundInputs,getBackgroundAdapters} from './index.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';
const repoRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const prefix='ymcc-background-signals-';
let scratch,failed=false;
const samePath=(a,b)=>process.platform==='win32'?a.toLowerCase()===b.toLowerCase():a===b;
async function directDir(dir){
 const stat=await fs.lstat(dir,{bigint:true});
 assert(stat.isDirectory()&&!stat.isSymbolicLink()&&samePath(realpathSync.native(dir),dir),'Not a direct directory: '+dir);
 return stat;
}
async function createScratch(){
 const parent=simulationCorePath('Workspace'),parentIdentity=await directDir(parent);
 const root=await fs.mkdtemp(path.join(parent,prefix));
 try{return {parent,parentIdentity,root,identity:await directDir(root)};}catch(error){console.error('Retained background fixture: '+root);throw error;}
}
function backgroundTest(name,run){
 test(name,async t=>{
  let passed=false;
  try{const {root}=await (scratch??=createScratch());await run(root);passed=true;}
  finally{if(!passed||t.signal.aborted)failed=true;}
 });
}
// A validator input only: it has no artifact or execution authority.
function receiptSample(){
 const modules=['src/bridge/background.ts','src/bridge/backgroundPolicy.ts','src/bridge/dynamicBackground.ts','src/bridge/sharedGameMedia.ts','src/bridge/gameMediaRevisionFence.ts','src/bridge/steamMediaSource.ts'];
 const ids=['policy.hysteresis','policy.invalid-samples','policy.video-reconciliation','bridge.fixed-and-dynamic-requests','bridge.visual-settings-feedback','bridge.settings-io-failure','bridge.native-io-failure','dynamic.cached-media','dynamic.disabled-no-io','dynamic.duplicate-inflight','dynamic.pid-reuse','dynamic.missing-process-identity','dynamic.disabled-during-request','dynamic.read-disabled-during-request','dynamic.deleted-entry','dynamic.invalid-entry','dynamic.revision-invalidated','dynamic.native-io-failure','dynamic.missing-ack','dynamic.pending-without-ack'];
 const sha256='0'.repeat(64);
 return {schemaVersion:1,status:'PASS',compatibility:{schemaVersion:1,status:'PASS',hcRuntimeRequired:false,requiredExports:Array(16).fill('sample')},
  checks:ids.map(id=>({id,status:'PASS',inputConsumerStateFeedback:{source:'source-derived-fixture',feedback:{events:['dynamic.cached-media','dynamic.duplicate-inflight'].includes(id)?[{name:'dynamic-background:loaded'}]:[]}}})),
  originalDynamic:{status:'PASS'},originalNative:{status:'PASS',checks:12},compiledInputs:modules.map(path=>({path,sha256})),executedProductModules:modules,
  behavior:{exitCode:0,timedOut:false,receipt:{safety:{realNativeCalls:0,userStorageReads:0,wallpaperChanged:false,hostStarted:false,appStarted:false,subscriptions:0,forbiddenCalls:[]}}},
  nativeSourceInputs:[{path:'native/main.cpp',sha256}],nativeArtifacts:[{path:'native/fixture.exe'}],executedNativeRegions:[{},{}]};
}
const receiptControls=[
 ['a marked PASS without actual event acknowledgment fails validation',worker=>{
  const missing=structuredClone(worker);
  missing.checks.find(item=>item.id==='dynamic.cached-media').inputConsumerStateFeedback.feedback.events=[];
  assert.ok(validateBackgroundReceipt(missing).issues.includes('EXPECTED_DYNAMIC_EVENT_RECEIPT_MISSING:dynamic.cached-media'));
 }],
 ['bad input falsely publishing success cannot pass evidence validation',worker=>{
  const corrupt=structuredClone(worker);
  corrupt.checks.find(item=>item.id==='dynamic.invalid-entry').inputConsumerStateFeedback.feedback.events.push({name:'dynamic-background:loaded'});
  assert.ok(validateBackgroundReceipt(corrupt).issues.includes('UNEXPECTED_DYNAMIC_SUCCESS_EVENT:dynamic.invalid-entry'));
 }],
 ['missing cases, duplicate IDs, missing compiled inputs and absent native receipt fail',worker=>{
  for(const [mutate,expected] of [
   [copy=>copy.checks.pop(),'BEHAVIOR_RECEIPTS_INCOMPLETE'],
   [copy=>copy.checks[0].id=copy.checks[1].id,'BEHAVIOR_RECEIPTS_INCOMPLETE'],
   [copy=>copy.compiledInputs=[],'ACTUAL_COMPILED_INPUT_HASHES_MISSING'],
   [copy=>copy.nativeSourceInputs=[],'ACTUAL_NATIVE_SOURCE_OR_ARTIFACT_HASHES_MISSING'],
   [copy=>copy.originalNative=null,'ORIGINAL_TEST_RECEIPTS_MISSING'],
   [copy=>copy.status='BLOCKED','WORKER_NOT_PASS'],
  ]){
   const copy=structuredClone(worker);mutate(copy);
   const result=validateBackgroundReceipt(copy);
   assert.equal(result.status,'FAIL');assert.ok(result.issues.includes(expected),expected);
  }
  assert.equal(validateBackgroundReceipt(null).status,'FAIL');
 }],
];
async function requireIdentity(dir,identity){
 const current=await directDir(dir);
 assert(current.dev===identity.dev&&current.ino===identity.ino,'Fixture directory identity changed: '+dir);
}
test.after(async()=>{
 if(!scratch)return;
 const {root,parent,identity,parentIdentity}=await scratch.catch(()=>({}));
 if(!root)return;
 if(failed){console.error('Retained background fixture: '+root);return;}
 assert(samePath(path.dirname(path.resolve(root)),parent)&&path.basename(root).startsWith(prefix),'Invalid owned fixture root: '+root);
 await requireIdentity(parent,parentIdentity);
 await requireIdentity(root,identity);
 await fs.rm(root,{recursive:true});
});
backgroundTest('actual original/derived native tests and all formal consumer feedback pass',async root=>{
 const evidenceDir=path.join(root,'valid');
 const passed=await runBackgroundRegressions({repoRoot,evidenceDir});
 assert.equal(passed.status,'PASS',JSON.stringify(passed.failure));
 const worker=JSON.parse(await fs.readFile(path.join(evidenceDir,'worker-result.json'),'utf8'));
 assert.equal(passed.checks.length,21);assert.equal(passed.originalDynamic.status,'PASS');assert.equal(passed.originalNative.checks,12);assert.deepEqual(validateBackgroundReceipt(worker),{status:'PASS',issues:[]});assert.equal(passed.sourceVerification.status,'PASS');assert.ok(passed.actualCompiledInputs.some(item=>item.path==='src/bridge/sharedGameMedia.ts'));assert.ok(passed.nativeSourceInputs.some(item=>item.path==='native/main.cpp'));
 // Keep the original mutations against the real receipt when the real producer runs.
 for(const [,check] of receiptControls)check(worker);
});
for(const [name,check] of receiptControls)test(name,()=>{
 const sample=receiptSample();
 assert.deepEqual(validateBackgroundReceipt(sample),{status:'PASS',issues:[]});
 check(sample);
});
backgroundTest('actual changed or missing source bytes are rejected',async root=>{const fakeRepo=path.join(root,'source-copy');const file='src/bridge/backgroundPolicy.ts';await fs.mkdir(path.dirname(path.join(fakeRepo,file)),{recursive:true});await fs.copyFile(path.join(repoRoot,file),path.join(fakeRepo,file));const before=await snapshotBackgroundInputs(fakeRepo,[file]);assert.equal((await verifyBackgroundInputs(fakeRepo,before)).status,'PASS');await fs.appendFile(path.join(fakeRepo,file),'\n// isolated actual source-drift injection\n');const drift=await verifyBackgroundInputs(fakeRepo,before);assert.equal(drift.status,'FAIL');assert.equal(drift.changed[0].path,file);await fs.unlink(path.join(fakeRepo,file));assert.equal((await verifyBackgroundInputs(fakeRepo,before)).status,'FAIL');});
backgroundTest('fresh directory contract prevents an old successful receipt from passing again',async root=>{
 const evidenceDir=path.join(root,'existing'),oldResult=path.join(evidenceDir,'result.json');
 const sentinel=JSON.stringify({status:'PASS',runId:'stale-receipt-sentinel'});
 await fs.mkdir(evidenceDir);
 await fs.writeFile(oldResult,sentinel,{flag:'wx'});
 await assert.rejects(runBackgroundRegressions({repoRoot,evidenceDir}),error=>error.code==='EEXIST');
 assert.equal(await fs.readFile(oldResult,'utf8'),sentinel);
 assert.deepEqual(await fs.readdir(evidenceDir),['result.json']);
});
backgroundTest('missing source and missing runtime yield BLOCKED without any product-success receipt',async root=>{const sourceMissing=await runBackgroundRegressions({repoRoot:path.join(root,'absent-repo'),evidenceDir:path.join(root,'missing-source')});assert.equal(sourceMissing.status,'BLOCKED');assert.equal(sourceMissing.failure.code,'REQUIRED_PRODUCT_OR_TOOL_INPUT_MISSING');const runtimeMissing=await runBackgroundRegressions({repoRoot,evidenceDir:path.join(root,'missing-runtime'),pwshPath:path.join(root,'missing-pwsh.exe')});assert.equal(runtimeMissing.status,'BLOCKED');assert.equal(runtimeMissing.failure.code,'OWNED_JOB_OR_PS7_UNAVAILABLE');});
backgroundTest('real owned-job timeout has no successful consumer receipt',async root=>{const result=await runBackgroundRegressions({repoRoot,evidenceDir:path.join(root,'timeout'),timeoutMs:500});assert.equal(result.status,'BLOCKED');assert.equal(result.failure.code,'BACKGROUND_RUN_TIMEOUT');assert.equal(result.checks.length,0);});
backgroundTest('a process exit without a worker receipt is BLOCKED',async root=>{const result=await runBackgroundRegressions({repoRoot,evidenceDir:path.join(root,'no-ack'),pwshPath:process.execPath});assert.equal(result.status,'BLOCKED');assert.equal(result.failure.code,'BACKGROUND_WORKER_RECEIPT_MISSING');});
test('descriptor is static, unique and explicitly marks UI rendering as reference-only',()=>{const [adapter]=getBackgroundAdapters();assert.equal(adapter.id,'display.background-signals');assert.equal(adapter.scope,'behavioral-mock');assert.deepEqual(adapter.resultFiles,['background/result.json','background/worker-result.json']);assert.deepEqual(adapter.referenceInputs,['src/App.vue']);assert.ok(!adapter.evidenceInputs.includes('src/App.vue'));assert.equal(new Set(adapter.evidenceInputs).size,adapter.evidenceInputs.length);});
