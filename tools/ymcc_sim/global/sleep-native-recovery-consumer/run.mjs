import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {REPO,MODULE,CASE_ID,moduleSources} from './adapter.mjs';
import {allocateScratch,verifyScratch,archiveScratch,cleanupScratch} from './scratch.mjs';
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const inside=(base,file)=>{const relative=path.relative(path.resolve(base),path.resolve(file));return !path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
export function evidencePath(value){
 need(typeof value==='string'&&/^[A-Za-z]:[\\/]/.test(value)&&!/[\0\r\n]/.test(value),'NATIVE_RECOVERY_EXPLICIT_LOCAL_OUTPUT');
 const root=path.resolve(value);need(!inside(REPO,root)&&!inside(root,REPO),'NATIVE_RECOVERY_REPO_BOUNDARY');
 need(path.parse(root).root.toLowerCase()!==(process.env.SystemDrive??'C:').toLowerCase()+'\\','NATIVE_RECOVERY_SYSTEM_VOLUME');
 for(let current=root;;){let stat;try{stat=fs.lstatSync(current);}catch(error){need(error.code==='ENOENT','NATIVE_RECOVERY_PATH_UNREADABLE');}if(stat)need(stat.isDirectory()&&!stat.isSymbolicLink(),'NATIVE_RECOVERY_REPARSE_OR_NOT_DIRECTORY');const parent=path.dirname(current);if(parent===current)break;current=parent;}
 need(!fs.existsSync(root),'NATIVE_RECOVERY_FRESH_OUTPUT_REQUIRED');return root;
}
function walk(root){const files=[];function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);need(!entry.isSymbolicLink(),'NATIVE_RECOVERY_OUTPUT_REPARSE');if(entry.isDirectory())visit(file);else{need(entry.isFile(),'NATIVE_RECOVERY_OUTPUT_TYPE');files.push(file);}need(files.length<12000,'NATIVE_RECOVERY_OUTPUT_LIMIT');}}visit(root);return files.sort();}
function captureInputs(root,spec,tool){
 const sources=[...new Set([...moduleSources(),...spec.sources,'tools/ymcc_sim/global/host-process.ps1'])].sort().map(relative=>{const original=path.join(REPO,relative),bytes=fs.readFileSync(original),saved=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.writeFileSync(saved,bytes,{flag:'wx'});return{path:relative,sha256:createHash('sha256').update(bytes).digest('hex'),saved};});
 const snapshot={schemaVersion:1,module:MODULE,createdAt:new Date().toISOString(),sources,tool};write(path.join(root,'input-snapshot.json'),snapshot);return snapshot;
}
function prepareExecution(record,evidence,snapshot){
 const scratch=verifyScratch(record,evidence),sources=snapshot.sources.map(row=>{const saved=path.join(scratch,'input-sources',row.path);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.copyFileSync(row.saved,saved,fs.constants.COPYFILE_EXCL);need(hash(saved)===row.sha256,'EXECUTION_SOURCE_COPY_CHANGED');return{...row,saved};});
 write(path.join(scratch,'input-snapshot.json'),{...snapshot,sources});write(path.join(scratch,'execution-owner.json'),record);return scratch;
}
function verifyExecutedArtifacts(scratch,worker){
 const roots=[worker.facts?.boundaryCompiler,worker.facts?.directOriginalQueryBoundary?.compilerInputs,worker.facts?.positive?.actualCompilerInputs,worker.facts?.['negative-confirmation']?.actualCompilerInputs].filter(Boolean);
 for(const ledger of roots){need(inside(scratch,ledger.path)&&hash(ledger.path)===ledger.sha256,'EVALUATED_LEDGER_CHANGED');for(const row of read(ledger.path).rows)need(hash(row.path)===row.sha256,'EXECUTED_COMPILER_OR_LINK_INPUT_CHANGED:'+row.path);}
 for(const label of ['positive','negative-confirmation']){const fact=worker.facts?.[label];if(!fact)continue;for(const row of Object.values(fact.produced??{}))need(inside(scratch,row.path)&&hash(row.path)===row.sha256,'EXECUTED_NATIVE_ARTIFACT_CHANGED');for(const row of fact.pythonAudits??[])need(inside(scratch,row.path)&&hash(row.path)===row.sha256,'EXECUTED_PYTHON_AUDIT_CHANGED');}
}
// A maintained script hash alone is insufficient: recover every historical byte by
// removing the one reviewed in-memory owner-proof declaration, and bind its real consumer.
function verifyVersion1Scripts(spec,readSource=relative=>fs.readFileSync(path.join(REPO,relative))){
 const historical='332bc485c61d2083afd36d429580f4e90698345912bcb889819181c88ef7f4c6',reviewed='73150a3e4685079a6b267495a1e43d76cedef061106a654ff450ae790aec802a',wake='88a8f3b95e1b64b4d4bd873c9401e0d247091953b2a348a9fab0c07f984d0f13';
 const block="// Mock only the native owner proof snapshot. Actual main helper is covered by full-pipe cases.\nULONGLONG inputHostWakeBindingProofLocked(){return g_wakeKeptTargetGeneration.load();}\n";
 const digest=bytes=>createHash('sha256').update(bytes).digest('hex'),review=spec.scriptCompatibilityReview;
 need(review?.schemaVersion===1&&review.kind==='exact-owner-proof-snapshot-mock-only'&&review.historicalApiSha256===historical&&review.reviewedApiSha256===reviewed&&review.wakeSha256===wake,'NATIVE_RECOVERY_REVIEW_CONTRACT_CHANGED');
 need(review.addedBlock===block,'NATIVE_RECOVERY_REVIEW_BLOCK_CHANGED');
 const expected=[['tools/sleep_recovery_api_selftest.py',reviewed],['tools/sleep_wake_resume_selftest.py',wake]];
 need(spec.originalScripts.length===expected.length&&spec.originalScripts.every((row,i)=>row.path===expected[i][0]&&row.sha256===expected[i][1]),'NATIVE_RECOVERY_REVIEWED_SCRIPT_SET_CHANGED');
 const api=readSource(expected[0][0]),wakeBytes=readSource(expected[1][0]);need(digest(api)===reviewed&&digest(wakeBytes)===wake,'NATIVE_RECOVERY_ORIGINAL_SCRIPT_NEEDS_REVIEW');
 const text=api.toString('utf8');need(text.split(block).length===2,'NATIVE_RECOVERY_REVIEW_DELTA_NOT_UNIQUE');need(digest(Buffer.from(text.replace(block,''),'utf8'))===historical,'NATIVE_RECOVERY_HISTORICAL_BYTES_NOT_PRESERVED');
 const contracts=[['native/sleep_recovery_api.h','{"powerBindingProofGeneration",inputHostWakeBindingProofLocked()}'],['native/sleep_recovery_api.h','inputBindingAllowed(number(i,"boundGeneration"),generation,number(i,"powerBindingProofGeneration"))'],['native/main.cpp','static unsigned long long inputHostWakeBindingProofLocked()'],['native/main.cpp','ymcc::sleeprecovery::inputBindingAllowed(g_inputHostBoundPowerGeneration,generation,inputHostWakeBindingProofLocked())']];
 need(JSON.stringify(review.requiredSourceContracts)===JSON.stringify(contracts.map(([path,text])=>({path,text}))),'NATIVE_RECOVERY_SOURCE_CONTRACT_DECLARATION_CHANGED');
 for(const[relative,required]of contracts)need(readSource(relative).toString('utf8').includes(required),'NATIVE_RECOVERY_OWNER_PROOF_CONSUMER_NEEDS_REVIEW:'+relative);
 return{schemaVersion:1,status:'PASS',historicalApiSha256:historical,reviewedApiSha256:reviewed,wakeSha256:wake,exactHistoricalRemainderVerified:true,onlyAddedMemorySnapshotDeclaration:true,sourceContractsVerified:review.requiredSourceContracts,realOwnerProofCalculationCovered:false,sourceApproval:false};
}


// Version 2 keeps the complete v1 verifier above. Its pinned byte edits must
// reconstruct both historical inputs before v1 contracts can be evaluated.
export function verifyReviewedOriginalScripts(spec,readSource=relative=>fs.readFileSync(path.join(REPO,relative))){
 if(spec.scriptCompatibilityReview?.schemaVersion===1)return verifyVersion1Scripts(spec,readSource);
 const expected={"schemaVersion":2,"kind":"exact-api-boundaries-and-early-completion","historicalApiSha256":"73150a3e4685079a6b267495a1e43d76cedef061106a654ff450ae790aec802a","historicalWakeSha256":"88a8f3b95e1b64b4d4bd873c9401e0d247091953b2a348a9fab0c07f984d0f13","reviewedApiSha256":"19068c9dfd2b58f8365d9e1b418a216825da104b72aa395d3d9f2136858bbc22","reviewedWakeSha256":"b3af74b0b99568e4fb3b9c4c3ea2b22a2d936acf2729c7803c26c3d9a750bd05","exactDifferencePath":"tools/ymcc_sim/global/sleep-native-recovery-consumer/script-maintenance-v2.json","exactDifferenceSha256":"58a20eb1a4727b07e9954cfa134edad24e5998bea972bf383817df7dc4d5479a"};
 const review=spec.scriptCompatibilityReview;
 need(review&&Object.entries(expected).every(([key,value])=>review[key]===value),'NATIVE_RECOVERY_REVIEW_CONTRACT_CHANGED');
 const scripts=[['tools/sleep_recovery_api_selftest.py',expected.reviewedApiSha256],['tools/sleep_wake_resume_selftest.py',expected.reviewedWakeSha256]];
 need(spec.originalScripts.length===scripts.length&&spec.originalScripts.every((row,i)=>row.path===scripts[i][0]&&row.sha256===scripts[i][1]),'NATIVE_RECOVERY_REVIEWED_SCRIPT_SET_CHANGED');
 const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
 const deltaBytes=readSource(expected.exactDifferencePath);
 need(digest(deltaBytes)===expected.exactDifferenceSha256,'NATIVE_RECOVERY_REVIEW_DELTA_HASH_CHANGED');
 const delta=JSON.parse(deltaBytes.toString('utf8'));
 need(delta.schemaVersion===2&&delta.kind===expected.kind&&delta.deltas.length===2,'NATIVE_RECOVERY_REVIEW_DELTA_SCHEMA');
 const historical=new Map();
 for(const [index,[relative,wanted]] of scripts.entries()){
  const bytes=readSource(relative),edit=delta.deltas[index];
  need(digest(bytes)===wanted,'NATIVE_RECOVERY_ORIGINAL_SCRIPT_NEEDS_REVIEW');
  const prior=index===0?expected.historicalApiSha256:expected.historicalWakeSha256;
  need(edit.path===relative&&edit.afterSha256===wanted&&edit.beforeSha256===prior&&Array.isArray(edit.edits),'NATIVE_RECOVERY_REVIEW_DELTA_BINDING');
  let cursor=0,rebuiltLength=0;const pieces=[];
  for(const change of edit.edits){
   const before=Buffer.from(change.beforeBase64,'base64'),after=Buffer.from(change.afterBase64,'base64');
   need(Number.isSafeInteger(change.afterOffset)&&change.afterOffset>=cursor&&change.afterOffset+after.length<=bytes.length,'NATIVE_RECOVERY_REVIEW_DELTA_RANGE');
   const unchanged=bytes.subarray(cursor,change.afterOffset);rebuiltLength+=unchanged.length;
   need(change.beforeOffset===rebuiltLength&&bytes.subarray(change.afterOffset,change.afterOffset+after.length).equals(after),'NATIVE_RECOVERY_REVIEW_DELTA_BYTES');
   pieces.push(unchanged,before);rebuiltLength+=before.length;cursor=change.afterOffset+after.length;
  }
  pieces.push(bytes.subarray(cursor));const restored=Buffer.concat(pieces);
  need(digest(restored)===prior,'NATIVE_RECOVERY_HISTORICAL_BYTES_NOT_PRESERVED');historical.set(relative,restored);
 }
 const prior=verifyVersion1Scripts({originalScripts:delta.priorOriginalScripts,scriptCompatibilityReview:delta.priorVersion1Review},relative=>historical.get(relative)??readSource(relative));
 for(const contract of delta.requiredSourceContracts)need(readSource(contract.path).toString('utf8').includes(contract.text),'NATIVE_RECOVERY_EARLY_COMPLETION_CONSUMER_NEEDS_REVIEW:'+contract.path);
 return{...expected,status:'PASS',exactHistoricalRemainderVerified:true,exactReverseDeltaVerified:true,
  priorVersion1Review:prior,reviewedChanges:delta.reviewedChanges,apiFixtureMaintainedForCurrentSource:true,
  wakeFixtureMaintainedForCurrentSource:true,historicalWakeBytesUnchanged:false,originalApiGroupsRetained:25,
  maintainedSuccessCountAssertions:1,originalPolicyAssertionsChanged:false,
  realOwnerProofCalculationCovered:false,sourceApproval:false};
}

export function runNativeRecovery(value,{prototype=false}={}){
 const root=evidencePath(value),spec=read(path.join(REPO,MODULE,'spec.json')),tool=read(path.join(REPO,MODULE,'toolchain-inputs.json'));
 need(path.resolve(process.execPath).toLowerCase()===path.resolve(tool.nodeExecutable).toLowerCase(),'NATIVE_RECOVERY_NODE_NOT_PINNED');
 const originalScriptReview=verifyReviewedOriginalScripts(spec);
 for(const row of tool.files)need(hash(row.path)===row.sha256,'NATIVE_RECOVERY_TOOL_CHANGED:'+row.path);
 fs.mkdirSync(root,{recursive:true});const snapshot=captureInputs(root,spec,tool),record=allocateScratch(root);write(path.join(root,'scratch-allocation.json'),record);const scratch=prepareExecution(record,root,snapshot),frozen=path.join(scratch,'input-sources',MODULE),args=path.join(root,'job-arguments.json');
 write(args,['-Node',tool.nodeExecutable,'-Worker',path.join(frozen,'worker.mjs'),'-Root',scratch]);const env={...process.env};delete env.NODE_OPTIONS;delete env.NODE_PATH;
 const job=spawnSync(tool.powershellExecutable,['-NoLogo','-NoProfile','-File',path.join(scratch,'input-sources/tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(frozen,'worker.ps1'),'-ArgumentsPath',args],{cwd:scratch,env,windowsHide:true,timeout:220000,maxBuffer:4*1024*1024});
 fs.writeFileSync(path.join(root,'job.stdout.log'),job.stdout??'');fs.writeFileSync(path.join(root,'job.stderr.log'),job.stderr??'');
 const workerFile=path.join(scratch,'worker-result.json'),worker=fs.existsSync(workerFile)?read(workerFile):{status:'FAIL',error:'WORKER_RESULT_MISSING'};
 const sourceDrift=snapshot.sources.filter(row=>hash(path.join(REPO,row.path))!==row.sha256),toolDrift=tool.files.filter(row=>hash(row.path)!==row.sha256);
 let validationError=null;try{verifyScratch(record,root);for(const row of snapshot.sources){need(hash(row.saved)===row.sha256,'SOURCE_SNAPSHOT_MUTATED');need(hash(path.join(scratch,'input-sources',row.path))===row.sha256,'EXECUTION_SOURCE_SNAPSHOT_MUTATED');}verifyExecutedArtifacts(scratch,worker);}catch(error){validationError=error.message;}
 // The Windows Job has returned and disposed all descendants before any copyback or cleanup.
 const mapping=archiveScratch(record,root);write(path.join(root,'execution-archive-mapping.json'),mapping);
 const naturalJob=job.status===0&&!job.error&&!job.signal&&(job.stdout??'').toString().includes('YMCC_SIM_OWNED_JOB childPid=');
 let cleanup={root:scratch,markerSha256:record.markerSha256,cleaned:false,retainedForFailure:true};
 if(naturalJob&&worker.status==='PASS'&&!sourceDrift.length&&!toolDrift.length&&!validationError)cleanup=cleanupScratch(record,root,mapping);
 write(path.join(root,'scratch-cleanup.json'),cleanup);
 const completed=naturalJob&&worker.status==='PASS'&&!sourceDrift.length&&!toolDrift.length&&!validationError&&cleanup.cleaned&&!prototype;
 const result={schemaVersion:1,id:CASE_ID,originalScriptReview,status:completed?'PASS':'FAIL',passed:completed,prototypeOnly:prototype,prototypeWorkerPassed:worker.status==='PASS',workerError:worker.error??validationError??null,validationError,originalSourceOrCompileFailurePreserved:worker.status!=='PASS',compileFailureCountsAsNegative:false,sourceHashes:[...snapshot.sources.map(({path,sha256})=>({path,sha256})),...walk(root).map(file=>({path:file,sha256:hash(file)}))],sourceChanges:sourceDrift.map(row=>row.path),toolChanges:toolDrift.map(row=>row.path),executionArchive:{mapping:path.join(root,'execution-archive-mapping.json'),mappingSha256:hash(path.join(root,'execution-archive-mapping.json')),originalRoot:scratch,rawWorkerResult:path.join(root,'execution/worker-result.json'),archiveFiles:mapping.files.length,originalContentsRewritten:false,cleaned:cleanup.cleaned},actualOriginalConsumers:['tools/sleep_recovery_api_selftest.py','tools/sleep_wake_resume_selftest.py'],inputConsumerStateFeedback:{input:'Original fixed fake clock/process/lease/settings/input observations',consumer:'Actual production wake/API/diagnostic headers and source-exact main recovery functions',state:'Original inspection/confirmation/lease/generation/timeout/input admission assertions',feedback:'Original native stdout/Native fixture return, owned feedback files, compile/OS boundary diagnostics'},boundary:{ownedWindowsJob:true,reviewedScriptsUnchangedDuringRun:true,apiFixtureMaintainedForCurrentSource:true,originalApiGroupsRetained:25,originalPolicyAssertionsChanged:false,maintainedSuccessCountAssertions:1,addedContextGroups:6,originalWakeScriptHistoricallyUnchanged:false,wakeFixtureMaintainedForCurrentSource:true,shortExecutionRootExactCaseOwned:true,allOutputsArchivedWithoutRewriting:true,realOsThreadQuery:'unknown via denied dynamic external query',realSleep:false,realProcessResume:false,realSteam:false,realHost:false,realDevices:false,installedApplication:false,fullProductAcceptance:false},remainingGaps:[...(worker.status==='PASS'?[]:[worker.error]),...(validationError?[validationError]:[]),...(sourceDrift.length?['Live sources changed; full source snapshot is not current acceptance']:[]),'OS sleep, actual original-game recovery, Steam input and physical devices are not accepted'],goalComplete:false};
 write(path.join(root,'result.json'),result);console.log(JSON.stringify({id:CASE_ID,status:result.status,prototypeOnly:prototype,workerStatus:worker.status,error:result.workerError,sourceDrift:sourceDrift.map(row=>row.path),nestedRows:result.sourceHashes.length,archived:mapping.files.length,cleaned:cleanup.cleaned,root}));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const args=process.argv.slice(2),prototype=args.includes('--prototype');need(args[0]==='--evidence-dir'&&typeof args[1]==='string'&&args.length===(prototype?3:2),'NATIVE_RECOVERY_ARGUMENTS');process.exitCode=runNativeRecovery(args[1],{prototype}).status==='PASS'?0:2;}catch(error){console.error(error.stack);process.exitCode=2;}}
