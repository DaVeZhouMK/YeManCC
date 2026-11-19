import test from 'node:test';
import assert from 'node:assert/strict';
import fsSync,{promises as fs,constants} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {runUpdateDownloadRegressions,validateUpdateDownloadReceipt,snapshotUpdateDownloadInputs,verifyUpdateDownloadInputs,getUpdateDownloadAdapters} from './index.mjs';
import {simulationCorePath} from '../release-host-environment.mjs';

const repoRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const workspace=simulationCorePath('Workspace'),liveRoot=process.env.YMCC_UPDATE_DOWNLOAD_INDEX_LIVE_ROOT;
const live={skip:liveRoot===undefined?'Explicit live evidence parent not requested':false};
const key=p=>process.platform==='win32'?path.resolve(p).toLowerCase():path.resolve(p);
const owns=(root,file)=>{const r=path.relative(root,file);return r!==''&&!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);};
async function plainDir(dir){const s=await fs.lstat(dir,{bigint:true});assert(s.isDirectory()&&!s.isSymbolicLink()&&key(fsSync.realpathSync.native(dir))===key(dir));return s;}
const same=(a,b)=>a.dev===b.dev&&a.ino===b.ino&&a.birthtimeNs===b.birthtimeNs;
let root,owner,parentOwner;
test.before(async()=>{parentOwner=await plainDir(workspace);root=await fs.mkdtemp(path.join(workspace,'update-receipt-controls-'));owner=await plainDir(root);});
test.after(async()=>{
 if(!owner)return;
 assert(same(await plainDir(workspace),parentOwner)&&same(await plainDir(root),owner));
 assert(key(path.dirname(root))===key(workspace)&&path.basename(root).startsWith('update-receipt-controls-'));
 async function check(dir){
  for(const name of await fs.readdir(dir)){
   const file=path.resolve(dir,name);assert(owns(root,file));
   const s=await fs.lstat(file);assert(!s.isSymbolicLink()&&key(fsSync.realpathSync.native(file))===key(file));
   if(s.isDirectory())await check(file);else assert(s.isFile());
  }
 }
 await check(root);
 await fs.rm(root,{recursive:true});
});
async function ownedLiveRoot(){
 assert(path.isAbsolute(liveRoot),'Explicit live evidence parent must be absolute');
 const dir=path.resolve(liveRoot);
 assert([workspace,simulationCorePath('Evidence')].some(p=>key(p)===key(dir)||owns(p,dir)),'Explicit live evidence parent must stay in the simulation core');
 await plainDir(dir);
 const target=await fs.mkdtemp(path.join(dir,'update-download-live-'));await plainDir(target);return target;
}

// Shape-only rows. They exercise the receipt reader and make no execution claim.
const cases=['scheduler.ready-once','scheduler.failure-backoff','scheduler.offline-and-resume','scheduler.concurrent-dedup','scheduler.disable-late-check','scheduler.stop-late-refresh','scheduler.initialize-retry','scheduler.bad-policy','scheduler.cancel-during-notice','scheduler.target-changed-in-notice','scheduler.fractional-backoff-policy','scheduler.schedule-write-failure','scheduler.pause-not-failure','provider.fresh-subscription-beats-old-read-error','provider.invalid-receipt','provider.replacement-late-read','provider.write-failure-feedback','service.rejected-activity-ack','service.idle-resync-preserves-transfer','manager.unknown-post-owner','manager.cancel-timeout-no-replay','manager.native-proof-recovers-unknown-post','manager.export-version-fence'];
const stability={'missing-start':2,'active-stall':2,'transport-reset':2,'slow-progress':1,'cleanup-running':2,'cleanup-probe':2,'cleanup-no-proof':1,'cleanup-error':2,'cleanup-busy':2,'cleanup-idle':2,'late-check':1,'late-initialize':1,'manual-protection':3,'scheduler-quiet':2,'game-recovery':10};
const originals={'scheduler-original':30,'integration-original':8,'service-original':8,'successor-original':11};
const compatibility=['updateManager','updatePredownloadService','updatePredownloadScheduler','updatePredownload','yeman','ipc','gamePolicyHysteresis'].map(s=>'src/bridge/'+s+'.ts');
function contractSample(){
 const terminal={
  'manager.unknown-post-owner':{downloadRequests:1,settled:false},
  'manager.cancel-timeout-no-replay':{downloadRequests:1},
  'manager.native-proof-recovers-unknown-post':{downloadRequests:1,snapshot:{phase:'downloaded'},installed:false},
  'service.rejected-activity-ack':{networkRequests:0},
 };
 const executedProductModules=[...compatibility,'src/bridge/gamePolicyTarget.ts'];
 const example={schemaVersion:1,status:'PASS',checks:cases.map(id=>({id,status:'PASS',inputConsumerStateFeedback:{source:'source-derived-fixture',state:terminal[id]??{},feedback:{calls:terminal[id]?.downloadRequests?[{cmd:'app.downloadUpdate'}]:[]}}})),
  stabilityTests:Object.entries(stability).map(([id,checks])=>({id,status:'PASS',exitCode:0,receipt:{status:'PASS',checks,records:Array.from({length:checks},()=>({}))}})),
  originalTests:Object.entries(originals).map(([id,checks])=>({id,status:'PASS',exitCode:0,checks})),
  executedProductModules,compiledInputs:executedProductModules.map(path=>({path,sha256:'a'.repeat(64)})),compatibility:{schemaVersion:1,hcRuntimeRequired:false,requiredModules:compatibility},
  ioGuards:Array.from({length:11},()=>({fetch:0,userStorage:0})),sharedPowerInputBoundary:{module:'powerLifecycleState',role:'inert snapshot/listener input',realSharedPowerSourceExecuted:false,sourceSha256:'b'.repeat(64)}};
 assert.deepEqual(validateUpdateDownloadReceipt(example),{status:'PASS',issues:[]});return example;
}
function refuses(mutate,issue){const value=contractSample();mutate(value);const actual=validateUpdateDownloadReceipt(value);assert.equal(actual.status,'FAIL');assert(actual.issues.includes(issue),JSON.stringify(actual));}

test('actual formal consumers and all original assertions pass with exact compile hashes',live,async()=>{
 const evidenceDir=path.join(await ownedLiveRoot(),'valid'),result=await runUpdateDownloadRegressions({repoRoot,evidenceDir});
 assert.equal(result.status,'PASS',JSON.stringify(result.failure));assert.equal(result.checks.length,23);assert.equal(result.originalTests.reduce((n,item)=>n+item.checks,0),57);
 assert.deepEqual(result.validation,{status:'PASS',issues:[]});assert.equal(result.sourceVerification.status,'PASS');
 assert.ok(result.actualCompiledInputs.some(item=>item.path==='src/bridge/updateManager.ts'));assert.ok(result.actualCompiledInputs.some(item=>item.path==='src/bridge/ipc.ts'));assert.equal(result.ioGuards.length,11);
 await assert.rejects(runUpdateDownloadRegressions({repoRoot,evidenceDir}),error=>error.code==='EEXIST');
});
test('missing stability or game-recovery execution cannot pass',()=>{refuses(copy=>copy.stabilityTests.pop(),'STABILITY_OR_GAME_RECOVERY_RECEIPT_MISSING');});
test('duplicated stability case or missing actual game policy cannot be accepted',()=>{
 refuses(copy=>{copy.stabilityTests[1]=copy.stabilityTests[0];},'STABILITY_OR_GAME_RECOVERY_RECEIPT_MISSING');
 refuses(copy=>{copy.compiledInputs=copy.compiledInputs.filter(item=>item.path!=='src/bridge/gamePolicyTarget.ts');},'ACTUAL_GAME_POLICY_NOT_EXECUTED:src/bridge/gamePolicyTarget.ts');
});
test('a fake PASS that replays an unknown POST or drops actual native proof is rejected',()=>{
 refuses(copy=>copy.checks.find(item=>item.id==='manager.unknown-post-owner').inputConsumerStateFeedback.feedback.calls.push({cmd:'app.downloadUpdate'}),'UNKNOWN_POST_REPLAY_OR_RECEIPT_MISSING');
 refuses(copy=>{copy.checks.find(item=>item.id==='manager.native-proof-recovers-unknown-post').inputConsumerStateFeedback.state.snapshot.phase='downloading';},'NATIVE_PROOF_OR_PENDING_STATE_MISSING');
});
test('false activity admission cannot be masked by a successful case label',()=>{refuses(copy=>copy.checks.find(item=>item.id==='service.rejected-activity-ack').inputConsumerStateFeedback.feedback.calls.push({cmd:'app.checkUpdate'}),'REJECTED_ACTIVITY_FALSE_SUCCESS');});
test('automatic install command, forbidden network and missing guard cannot pass',()=>{
 refuses(copy=>{copy.checks[0].inputConsumerStateFeedback.feedback.calls=[{cmd:'app.installUpdate'}];},'AUTO_INSTALL_COMMAND_FORBIDDEN');
 for(const mutate of [copy=>{copy.ioGuards[0].fetch=1;},copy=>{copy.ioGuards=[];}])refuses(mutate,'FORBIDDEN_IO_OR_GUARD_RECEIPT_MISSING');
});
test('missing original assertions, missing compiled input and duplicate/missing cases fail',()=>{
 for(const [mutate,issue] of [[copy=>copy.originalTests[0].checks=29,'ORIGINAL_ASSERTIONS_MISSING:scheduler-original'],[copy=>copy.compiledInputs=[],'EXACT_COMPILED_INPUTS_MISSING'],[copy=>copy.checks.pop(),'BEHAVIOR_FEEDBACK_INCOMPLETE'],[copy=>copy.checks[0].id=copy.checks[1].id,'BEHAVIOR_FEEDBACK_INCOMPLETE'],[copy=>copy.compatibility=null,'COMPATIBILITY_CONTRACT_MISSING'],[copy=>copy.status='BLOCKED','WORKER_NOT_PASS']])refuses(mutate,issue);
 assert.equal(validateUpdateDownloadReceipt(null).status,'FAIL');
});
test('actual isolated source bytes changed or removed are not accepted',async()=>{
 const fakeRepo=path.join(root,'source-copy'),relative='src/bridge/updatePredownloadScheduler.ts',target=path.join(fakeRepo,relative);
 await fs.mkdir(path.dirname(target),{recursive:true});await fs.copyFile(path.join(repoRoot,relative),target,constants.COPYFILE_EXCL);
 const hashes=await snapshotUpdateDownloadInputs(fakeRepo,[relative]);assert.equal((await verifyUpdateDownloadInputs(fakeRepo,hashes)).status,'PASS');
 await fs.appendFile(target,'\n// actual isolated source drift\n');assert.equal((await verifyUpdateDownloadInputs(fakeRepo,hashes)).status,'FAIL');
 await fs.unlink(target);assert.equal((await verifyUpdateDownloadInputs(fakeRepo,hashes)).status,'FAIL');
});
test('a pre-existing evidence directory cannot be reused',async()=>{
 const existing=path.join(root,'already-owned');await fs.mkdir(existing);const marker=path.join(existing,'sentinel');await fs.writeFile(marker,'do not replace',{flag:'wx'});
 await assert.rejects(runUpdateDownloadRegressions({repoRoot,evidenceDir:existing}),error=>error.code==='EEXIST');assert.equal(await fs.readFile(marker,'utf8'),'do not replace');
});
test('missing source is BLOCKED without fabricated success',async()=>{
 const missing=await runUpdateDownloadRegressions({repoRoot:path.join(root,'missing-repo'),evidenceDir:path.join(root,'missing-source')});
 assert.equal(missing.status,'BLOCKED');assert.equal(missing.failure.code,'REQUIRED_PRODUCT_OR_TOOL_INPUT_MISSING');assert.equal(missing.process,undefined);
});
test('missing PS7 is BLOCKED without fabricated success',live,async()=>{
 const liveDir=await ownedLiveRoot(),runtime=await runUpdateDownloadRegressions({repoRoot,evidenceDir:path.join(liveDir,'missing-pwsh'),pwshPath:path.join(liveDir,'missing.exe')});
 assert.equal(runtime.status,'BLOCKED');assert.equal(runtime.failure.code,'OWNED_JOB_OR_PS7_UNAVAILABLE');
});
test('actual child without receipt and actual owned-job timeout never PASS',live,async()=>{
 const liveDir=await ownedLiveRoot(),missing=await runUpdateDownloadRegressions({repoRoot,evidenceDir:path.join(liveDir,'no-receipt'),pwshPath:process.execPath});
 assert.equal(missing.status,'BLOCKED');assert.equal(missing.failure.code,'UPDATE_DOWNLOAD_WORKER_RECEIPT_MISSING');
 const timeout=await runUpdateDownloadRegressions({repoRoot,evidenceDir:path.join(liveDir,'timeout'),timeoutMs:500});assert.equal(timeout.status,'BLOCKED');assert.equal(timeout.failure.code,'UPDATE_DOWNLOAD_RUN_TIMEOUT');assert.equal(timeout.checks.length,0);
});
test('descriptor has bounded timeout and exposes actual product sources and reference-only inputs',()=>{
 const [descriptor]=getUpdateDownloadAdapters();assert.equal(descriptor.id,'network.update-predownload-signals');assert.equal(descriptor.kind,'node');
 assert.equal(descriptor.scope,'behavioral-mock');assert.equal(descriptor.timeoutMs,90000);assert.deepEqual(descriptor.resultFiles,['update-download/result.json','update-download/worker-result.json']);
 assert.equal(new Set(descriptor.evidenceInputs).size,descriptor.evidenceInputs.length);assert.ok(descriptor.evidenceInputs.includes('src/bridge/updatePredownloadService.ts'));
 assert.deepEqual(descriptor.referenceInputs,['src/bridge/gamePolicyTarget.ts','src/bridge/powerLifecycleState.ts']);
});
test('source tree writes and unbounded run duration are rejected before execution',async()=>{
 await assert.rejects(runUpdateDownloadRegressions({repoRoot,evidenceDir:path.join(repoRoot,'must-not-create')}),error=>error.code==='EVIDENCE_MUST_BE_OUTSIDE_REPOSITORY');
 await assert.rejects(runUpdateDownloadRegressions({repoRoot,evidenceDir:path.join(root,'no-timeout'),timeoutMs:0}),error=>error.code==='BOUNDED_TIMEOUT_REQUIRED');
});
