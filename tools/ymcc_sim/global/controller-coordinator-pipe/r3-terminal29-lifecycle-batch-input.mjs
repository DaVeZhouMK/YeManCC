import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';

export const terminal29BatchKind='TERMINAL29_LIFECYCLE_BATCH_COMPILED_INPUTS';
const key=file=>path.resolve(file).toLowerCase();
const same=(a,b)=>typeof a==='string'&&path.isAbsolute(a)&&key(a)===key(b);
function inside(root,file){const rel=path.relative(root,file);return rel&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);}
const exactKeys=(value,names)=>value&&Object.keys(value).sort().join('|')===names.slice().sort().join('|');
const filePin=row=>exactKeys(row,['path','bytes','sha256'])&&typeof row.path==='string'&&path.isAbsolute(row.path)&&Number.isSafeInteger(row.bytes)&&row.bytes>=0&&/^[a-f0-9]{64}$/.test(row.sha256);

export function readTerminal29LifecycleBatch({declaration,inputFile,repo,workspace,evidence,originalRoles}){
 assert(exactKeys(declaration,['schemaVersion','kind','batch','budget'])&&declaration.schemaVersion===1&&declaration.kind===terminal29BatchKind&&filePin(declaration.batch)&&filePin(declaration.budget),'explicit Terminal29 batch declaration required');
 assert(path.isAbsolute(inputFile)&&inside(workspace,inputFile),'Terminal29 batch declaration escaped Workspace');
 const pins=new Map();
 function load(file,root,expected,parse=true){
  assert(same(file,file)&&inside(root,file)&&!/[\0\r\n]/.test(file),'Terminal29 batch input escaped its owner');
  assert(same(fs.realpathSync(file),file)&&fs.lstatSync(file).isFile(),'regular unredirected Terminal29 batch input required');
  const raw=fs.readFileSync(file),sha256=createHash('sha256').update(raw).digest('hex');
  if(expected!==undefined){assert(filePin(expected)&&same(expected.path,file)&&expected.sha256===sha256&&expected.bytes===raw.byteLength,'Terminal29 batch input identity changed: '+file);}
  pins.set(key(file),{path:file,sha256,bytes:raw.byteLength});
  if(parse===false)return undefined;
  const text=raw.toString('utf8');
  return parse==='text'?text:JSON.parse(text.replace(/^\uFEFF/,''));
 }
 const batchFile=declaration.batch.path,batch=load(batchFile,workspace,declaration.batch);
 assert(batch?.schemaVersion===1&&batch.status==='TERMINAL29_NATIVE_BATCH_COMPILED_ONLY'&&batch.publicRuntimeAccepted===false&&batch.nativeExecuted===false&&
  batch.fullSourceVerificationCalls===2&&Array.isArray(batch.failures)&&batch.failures.length===0,'complete compile-only Terminal29 batch required');
 const buildRoot=path.dirname(path.dirname(batchFile)),batchRoot=path.dirname(batchFile);
 assert(path.basename(batchFile)==='NATIVE-BATCH.json'&&same(path.dirname(inputFile),buildRoot),'exact Terminal29 batch declaration owner required');
 assert(exactKeys(batch.inputs,['preparation','layout','toolchainManifest'])&&Object.values(batch.inputs).every(filePin),'exact Terminal29 batch preparation, layout and toolchain pins required');
 const preparation=load(batch.inputs.preparation.path,buildRoot,batch.inputs.preparation);
 const layout=load(batch.inputs.layout.path,buildRoot,batch.inputs.layout);
 assert(same(layout.authorizedBuildRoot,buildRoot)&&same(layout.primaryRoot,repo)&&same(batch.inputs.layout.path,preparation.layout.path)&&
  batch.inputs.layout.sha256===preparation.layout.sha256,'Terminal29 batch managed layout differs');
 // Reject stale batches before reading toolchain or role artifacts. This is not the full source proof.
 const primaryMain=path.join(repo,'native','main.cpp');
 assert.match(layout.productMainSha256??'',/^[a-f0-9]{64}$/,'Terminal29 batch requires a captured main digest');
 assert(same(fs.realpathSync(primaryMain),primaryMain)&&fs.lstatSync(primaryMain).isFile(),'Terminal29 batch requires a regular unredirected primary main');
 const currentMainSha256=createHash('sha256').update(fs.readFileSync(primaryMain)).digest('hex');
 assert(currentMainSha256===layout.productMainSha256,`CURRENT_MAIN_DRIFT_BEFORE_REGISTRATION: captured ${layout.productMainSha256}, current ${currentMainSha256}`);
 const toolchain=load(batch.inputs.toolchainManifest.path,evidence,batch.inputs.toolchainManifest);
 const sourcePhases=['before','after'];assert(exactKeys(batch.authority,sourcePhases)&&Object.values(batch.authority).every(filePin),'both Terminal29 batch authorities required');
 const [before,after]=sourcePhases.map((phase,i)=>{
  const expected=batch.authority[phase],file=path.join(batchRoot,'authority',`authority-${phase}.json`);
  const value=load(file,batchRoot,expected),source=value.source;
  assert(value.fullSourceVerificationCalls===i+1&&value.sourceApprovalGranted===false&&value.sourceVerificationMode==='parallel-primary-and-copy'&&
   source?.status==='FROZEN_SOURCE_VERIFIED'&&source.creationStatus==='PASS'&&source.snapshotIntegrityVerified===true&&source.isCurrent===true&&
   same(source.sourceRoot,repo)&&same(source.snapshotRoot,layout.sourceRoot)&&source.manifestSha256===layout.manifestSha256,'exact current Terminal29 batch source proof required');
  return value;
 });
 assert(after.status==='AUTHORITY_STABLE'&&after.currentConsumerAccepted===false&&after.memberAdmissions===originalRoles.length&&
  Array.isArray(after.actualTools)&&after.actualTools.length>0,'closed Terminal29 batch authority and original member count required');
 assert.deepEqual(before.actualTools,after.actualTools,'Terminal29 batch tool identities differ');
 assert.deepEqual(before.nodeTools,after.nodeTools,'Terminal29 batch Node binding differs');
 const budgetFile=declaration.budget.path,budget=load(budgetFile,evidence,declaration.budget),budgetRoot=path.dirname(budgetFile);
 assert(path.basename(budgetFile)==='execution-budget.json'&&same(budget.runDir,budgetRoot)&&budget.schemaVersion===1&&budget.scope==='original-domain'&&
  budget.status==='ORIGINAL_DOMAIN_EXITED'&&budget.processStarted===true&&budget.exitCode===0&&budget.processExitCode===0&&budget.timedOut===false&&
  budget.ownedJobVerified===true&&budget.resourceZeroVerified===true&&budget.sourceIdentity?.status==='MATCH'&&
  Array.isArray(budget.sourceDrift)&&budget.sourceDrift.length===0&&Array.isArray(budget.sourceReadErrors)&&budget.sourceReadErrors.length===0,'successful exact Terminal29 batch guard required');
 const job=load(path.join(budgetRoot,'owned-job.json'),budgetRoot),context=load(path.join(budgetRoot,'budget-context.json'),budgetRoot);
 assert.deepEqual(job,budget.ownedJob,'Terminal29 batch raw Job differs from envelope');
 assert(context.kind==='ymcc-fixture-wall-budget'&&context.scope==='original-domain'&&same(context.repoRoot,repo)&&same(context.runDir,budgetRoot)&&
  context.deadlineEpochMs===job.deadlineEpochMs&&context.cleanupReserveMs===job.cleanupReserveMs&&context.budgetMs===budget.budgetMs&&
  Number.isSafeInteger(context.budgetMs)&&context.budgetMs>0&&context.budgetMs<=1800000&&job.finishedEpochMs<=job.deadlineEpochMs&&
  job.status==='EXITED'&&job.exitCode===0&&job.rootExitCode===0&&job.deadlineExpired===false&&Number.isSafeInteger(batch.producerPid)&&batch.producerPid>0&&
  job.childPid===batch.producerPid&&job.jobAssigned===true&&job.childExitedBeforeReturn===true&&job.jobDisposed===true&&job.resourceObservationVersion===1&&
  job.activeProcessesFinal===0&&job.zeroObservedBeforeDispose===true&&job.cleanupError===null,'Terminal29 batch Job binding or terminal zero proof differs');
 if(job.terminationRequested){
  const members=job.membersBeforeTermination,seen=new Set();
  assert(job.terminationSucceeded===true&&job.activeProcessesBeforeCleanup>0&&job.memberObservationComplete===true&&job.memberObservationMatchesAccounting===true&&
   Array.isArray(members)&&members.length===job.activeProcessesBeforeCleanup,'incomplete Terminal29 compiler helper reap');
  for(const member of members){
   const pinned=after.actualTools.find(row=>same(member.image,row.path));
   assert(member.identityComplete&&member.handleBound&&member.ownedAtQuery&&member.handleClosed&&!member.error&&Number.isSafeInteger(member.pid)&&member.pid>0&&
    member.pid!==batch.producerPid&&!seen.has(member.pid)&&/^\d+$/.test(member.startFileTimeUtc)&&pinned&&path.basename(pinned.path).toLowerCase()==='vctip.exe'&&
    toolchain.tools.some(row=>same(row.original,pinned.path)&&row.sha256===pinned.sha256),'unexpected Terminal29 batch process survived compile');
   seen.add(member.pid);
  }
 }else assert(job.terminationRequested===false&&job.activeProcessesBeforeCleanup===0,'natural Terminal29 batch process zero required');
 const nativeEntry=path.join(repo,'tools','ymcc_sim','global','controller-coordinator-pipe','r3-terminal22-native-prepare.mjs');
 const argumentsFile=load(path.join(budgetRoot,'budget-arguments.json'),budgetRoot);
 assert(Array.isArray(argumentsFile)&&same(argumentsFile[0],nativeEntry)&&argumentsFile.length%2===1,'exact Terminal29 batch guard command required');
 const options=new Map();
 for(let i=1;i<argumentsFile.length;i+=2){assert(!options.has(argumentsFile[i]),'duplicate Terminal29 batch guard argument');options.set(argumentsFile[i],argumentsFile[i+1]);}
 assert(same(options.get('--out'),batchRoot)&&same(options.get('--preparation'),batch.inputs.preparation.path)&&same(options.get('--toolchain-manifest'),batch.inputs.toolchainManifest.path)&&
  options.get('--role')==='lifecycle-batch'&&options.get('--runtime-reuse-policy')==='owned-lifecycle-child-v1'&&(options.get('--native-optimization')??'O2')==='O2','Terminal29 guarded compile command differs');
 const output=load(path.join(budgetRoot,'budget.stdout.log'),budgetRoot,undefined,'text').trimEnd();
 const stamp=JSON.parse(output.slice(output.lastIndexOf('\n')+1));
 assert(stamp.status===batch.status&&stamp.producerPid===job.childPid&&same(stamp.resultFile,batchFile)&&stamp.resultSha256===declaration.batch.sha256&&stamp.failures===0,'Terminal29 guarded raw batch identity differs');
 assert.deepEqual(stamp.roles,originalRoles,'Terminal29 guarded role set differs');
 assert(Array.isArray(batch.roles)&&batch.roles.length===originalRoles.length,'all original Terminal29 staged role receipts required');
 assert.deepEqual(batch.roles.map(row=>row.role),originalRoles,'Terminal29 batch role order or membership differs');
 const selected=batch.roles.map(row=>{
  const roleRoot=path.join(batchRoot,row.role),file=path.join(roleRoot,'NATIVE-PREPARATION.json');
  const n=load(file,roleRoot,row.receipt);
  const guardEntry=budget.sourcePins?.find(pin=>same(pin.path,nativeEntry));
  assert(n.status==='TERMINAL29_NATIVE_ROLE_STAGED'&&n.role===row.role&&n.batchAfter===undefined&&n.preparation?.sha256===batch.inputs.preparation.sha256&&
   same(n.preparation.path,batch.inputs.preparation.path)&&n.toolchainManifest?.sha256===batch.inputs.toolchainManifest.sha256&&
   same(n.toolchainManifest.path,batch.inputs.toolchainManifest.path)&&guardEntry&&same(n.runner?.path,nativeEntry)&&n.runner.sha256===guardEntry.sha256,'Terminal29 staged role differs from guarded batch');
  const artifacts={compilerLedger:'COMPILER-INPUTS.json',compilerProcess:'native/build-process.json',compileReceipt:'native/COMPILE-ONLY.json',buildScript:'native/build.cmd',sourceManifest:'native/source-manifest.json',exe:'native/CoordinatorFixture.exe'};
  assert(exactKeys(row.artifacts,Object.keys(artifacts))&&Object.values(row.artifacts).every(filePin),'exact Terminal29 role artifact set required');
  for(const [name,relative]of Object.entries(artifacts)){
   const expected=row.artifacts[name],artifact=path.join(roleRoot,relative);
   if(name!=='compileReceipt'){
    const prior=name==='compilerLedger'?n.compilerLedger:n.native?.[name];
    assert(same(prior?.path,artifact)&&expected.sha256===prior.sha256,'Terminal29 staged artifact differs: '+name);
   }
   // Managed/artifact identity will also be checked by the original observer.
   load(artifact,roleRoot,expected,false);
  }
  return {role:row.role,path:file,sha256:row.receipt.sha256};
 });
 return {selected,after,inputs:[...pins.values()]};
}
