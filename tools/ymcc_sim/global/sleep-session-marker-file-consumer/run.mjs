import {openSharedLeafReview} from '../sleep-native-recovery-consumer/shared-leaf-successor.mjs';
import {verifyCompilerSourceReview,verifyWorkerEsmCoverage} from '../sleep-exit-inflight-consumer/compiler-source-review.mjs';
import fs from'node:fs';import path from'node:path';import{spawnSync}from'node:child_process';import{createHash}from'node:crypto';import{fileURLToPath}from'node:url';
import{REPO,MODULE,CASE_ID,inputFiles,toolchainSpec}from'./adapter.mjs';
import{allocateScratch,verifyScratch,archiveScratch,cleanupScratch}from'../sleep-native-recovery-consumer/scratch.mjs';import{evidencePath}from'../sleep-native-recovery-consumer/run.mjs';export{evidencePath};
const hash=f=>createHash('sha256').update(fs.readFileSync(f)).digest('hex'),read=f=>JSON.parse(fs.readFileSync(f,'utf8').replace(/^\uFEFF/,'')),write=(f,j)=>fs.writeFileSync(f,JSON.stringify(j,null,2)+'\n'),need=(v,m)=>{if(!v)throw Error(m);};
function walk(dir){const rows=[];for(const entry of fs.readdirSync(dir,{withFileTypes:true})){need(!entry.isSymbolicLink(),'SESSION_MARKER_FILES_OUTPUT_REPARSE');const p=path.join(dir,entry.name);if(entry.isDirectory())rows.push(...walk(p));else{need(entry.isFile(),'SESSION_MARKER_FILES_OUTPUT_TYPE');rows.push(p);}need(rows.length<12000,'SESSION_MARKER_FILES_OUTPUT_LIMIT');}return rows.sort();}
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
// Pin the full successor and reverse only its reviewed literal change.
const adapterSuccessors=new Map([
 ['tools/ymcc_sim/global/marker-ownership-consumer/adapter.mjs',{
  predecessor:'59767af40f1e0b3f8a62a7cf90a96c81a40d905a702d8b60eeff4e673dd7ba2c',current:'77c4ec12a61b306753abb3e97faa093d9e3c5486d1d46c56c6958cb992e334a7',offset:1690,before:'9',after:'10',
 }],
 ['tools/ymcc_sim/global/sleep-single-pid-freeze-consumer/adapter.mjs',{
  predecessor:'5c6ed8482558262b81ac7d19625e52cdc0c13a81aa871f37c68b21f893f30f58',current:'05ee0c8addc70b4bfc2f877afda245d214ef02fb18a5c41763b20a353c5682a9',offset:1601,
  before:'plus3 marker parsers.19 direct callouts (16 memory ports and3 exact pure parsers using memory sgReadFile). PID/session/marker/process handles and owned counted NT function pointer.26 positive groups and5 private production source faults must compile/start then fail preserved fixture assertions. Portable import guard refuses real process/power/network ports; no real NT/user process/Host/Steam/device acceptance',
  after:'and three marker parsers. Process, marker, startup inventory and file lease ports are memory-only. The original 26 positive groups and five private source faults must compile and run with preserved assertions. The portable import guard refuses real process, power and network ports',
 }],
]);
export function verifySessionMarkerPinnedSources(specPath,specBytes,readBytes){
 const spec=JSON.parse(specBytes.toString('utf8').replace(/^\uFEFF/,'')),captureIdentities=new Map([[specPath,digest(specBytes)]]),exactAdapterSuccessors=[];
 const sharedReview=openSharedLeafReview(spec,readBytes);
 const fixture=spec.fixture;
 need(digest(readBytes(fixture.path))===fixture.sha256,'SESSION_MARKER_FILES_FIXTURE_NEEDS_REVIEW');
 captureIdentities.set(fixture.path,fixture.sha256);
 for(const row of spec.reviewedSharedLeafIdentities){
  const bytes=readBytes(row.path),sha=digest(bytes);
  if(sha!==row.sha256){
   const review=adapterSuccessors.get(row.path),message='SESSION_MARKER_FILES_SHARED_LEAF_CHANGED:'+row.path;
   if(!review){sharedReview.verify(row,bytes,message);captureIdentities.set(row.path,sha);continue;}
   need(review.predecessor===row.sha256&&review.current===sha,message);
   const after=Buffer.from(review.after),offset=review.offset??bytes.indexOf(after),end=offset+after.length;
   need(offset>=0&&bytes.subarray(offset,end).equals(after),message);
   if(review.offset===undefined)need(bytes.indexOf(after,offset+1)===-1,message);
   const predecessor=Buffer.concat([bytes.subarray(0,offset),Buffer.from(review.before),bytes.subarray(end)]);
   need(digest(predecessor)===row.sha256,message);
   exactAdapterSuccessors.push({path:row.path,predecessorSha256:row.sha256,currentSha256:sha,boundaryDescriptionOnly:review.boundaryDescriptionOnly!==false});
  }
  captureIdentities.set(row.path,sha);
 }
 for(const [name,sha256] of sharedReview.pins)captureIdentities.set(name,sha256);
 return {captureIdentities,sourceReview:{status:'PASS',exactAdapterSuccessors,sharedLeafReview:sharedReview.receipt(),sourceApproval:false}};
}
export function verifySessionMarkerSourceCapture(sources,captureIdentities){
 const copied=new Map(sources.map(row=>[row.path,row.sha256]));
 for(const [name,sha] of captureIdentities)need(copied.get(name)===sha,'SESSION_MARKER_FILES_CAPTURED_REVIEW_CHANGED:'+name);
}
export function qualifySessionMarkerJob(job,receipt,startedEpochMs,timeoutMs){
 const marker=[...(job?.stdout??'').toString().matchAll(/^YMCC_SIM_OWNED_JOB childPid=([1-9][0-9]*)\r?$/gm)];
 const pid=receipt?.childPid,active=receipt?.activeProcessesBeforeCleanup;
 const bound=receipt?.schemaVersion===1&&receipt.jobAssigned===true&&receipt.childExitedBeforeReturn===true&&Number.isSafeInteger(pid)&&pid>0&&marker.length===1&&Number(marker[0][1])===pid;
 // The outer spawn timeout is unchanged; no inner guard deadline was registered.
 const onTime=bound&&receipt.deadlineEpochMs===0&&receipt.cleanupReserveMs===0&&receipt.deadlineExpired===false&&Number.isSafeInteger(receipt.finishedEpochMs)&&receipt.finishedEpochMs>=startedEpochMs&&receipt.finishedEpochMs<=startedEpochMs+timeoutMs;
 const exited=onTime&&!job?.error&&!job?.signal&&Number.isSafeInteger(job?.status)&&job.status===receipt.exitCode&&receipt.rootExitCode===receipt.exitCode&&receipt.status===(receipt.exitCode===0?'EXITED':'FAILED');
 const returned=bound&&receipt.resourceObservationVersion===1&&Number.isSafeInteger(receipt.jobQueryCount)&&receipt.jobQueryCount>=2&&Number.isSafeInteger(active)&&active>=0&&receipt.terminationRequested===(active>0)&&receipt.terminationSucceeded===(active>0)&&receipt.activeProcessesFinal===0&&receipt.zeroObservedBeforeDispose===true&&receipt.jobDisposed===true&&(receipt.cleanupError===null||receipt.cleanupError==='');
 need(exited&&returned,'SESSION_MARKER_FILES_OWNED_JOB_NOT_RETIRED');
 return job.status===0&&active===0;
}
export function runSleepSessionMarkerFiles(value,{prototype=false}={}){
 need(prototype===false,'PRIVATE_SOURCE_PROTOTYPES_NOT_ALLOWED');
 const root=evidencePath(value),specPath=MODULE+'/spec.json',readSource=name=>fs.readFileSync(path.join(REPO,name));
 const {captureIdentities,sourceReview}=verifySessionMarkerPinnedSources(specPath,readSource(specPath),readSource);
 const compilerSourceReview=verifyCompilerSourceReview(REPO);
 const tool=toolchainSpec(REPO);need(path.resolve(process.execPath).toLowerCase()===path.resolve(tool.nodeExecutable).toLowerCase(),'SESSION_MARKER_FILES_NODE_NOT_PINNED');
 for(const row of tool.files)need(hash(row.path)===row.sha256,'SESSION_MARKER_FILES_TOOL_CHANGED:'+row.path);
 fs.mkdirSync(root,{recursive:true});const sources=inputFiles(REPO).sort().map(relative=>{const saved=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.copyFileSync(path.join(REPO,relative),saved,fs.constants.COPYFILE_EXCL);return{path:relative,saved,sha256:hash(saved)};});
 verifySessionMarkerSourceCapture(sources,captureIdentities);
 const snapshot={schemaVersion:1,module:MODULE,evidenceRoot:root,createdAt:new Date().toISOString(),sources,tool,sourceReview};write(root+'/input-snapshot.json',snapshot);const owner=allocateScratch(root),scratch=verifyScratch(owner,root);write(root+'/scratch-allocation.json',owner);const executed=sources.map(row=>{const saved=path.join(scratch,'input-sources',row.path);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.copyFileSync(row.saved,saved,fs.constants.COPYFILE_EXCL);need(hash(saved)===row.sha256,'SESSION_MARKER_FILES_EXECUTION_COPY_CHANGED');return{...row,saved};});write(scratch+'/input-snapshot.json',{...snapshot,sources:executed});write(scratch+'/execution-owner.json',owner);const args=root+'/job-arguments.json';write(args,['-Node',tool.nodeExecutable,'-Worker',path.join(scratch,'input-sources',MODULE,'worker.mjs'),'-Root',scratch]);const env={...process.env};delete env.NODE_OPTIONS;delete env.NODE_PATH;env.NODE_V8_COVERAGE=path.join(scratch,'esm-coverage');fs.mkdirSync(env.NODE_V8_COVERAGE);
 const timeoutMs=360000,startedEpochMs=Date.now(),jobReceiptPath=root+'/owned-job.json';
 const job=spawnSync(tool.powershellExecutable,['-NoLogo','-NoProfile','-File',path.join(scratch,'input-sources/tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(scratch,'input-sources/tools/ymcc_sim/global/sleep-native-recovery-consumer/worker.ps1'),'-ArgumentsPath',args,'-DeadlineReceiptPath',jobReceiptPath],{env,cwd:scratch,windowsHide:true,timeout:timeoutMs,maxBuffer:8*1024*1024});fs.writeFileSync(root+'/job.stdout.log',job.stdout??'',{flag:'wx'});fs.writeFileSync(root+'/job.stderr.log',job.stderr??'',{flag:'wx'});
 let ownedJob;try{ownedJob=read(jobReceiptPath);}catch(error){throw Error('SESSION_MARKER_FILES_OWNED_JOB_RECEIPT_UNREADABLE',{cause:error});}
 const naturalJob=qualifySessionMarkerJob(job,ownedJob,startedEpochMs,timeoutMs);
 const workerFile=scratch+'/worker-result.json',worker=fs.existsSync(workerFile)?read(workerFile):{status:'FAIL',error:'SESSION_MARKER_FILES_WORKER_RESULT_MISSING'},sourceChanges=sources.filter(x=>hash(path.join(REPO,x.path))!==x.sha256).map(x=>x.path),toolChanges=tool.files.filter(x=>hash(x.path)!==x.sha256).map(x=>x.path),validationErrors=[];let workerEsmSourceEvidence=null;
 try{verifyScratch(owner,root);for(const row of sources){need(hash(row.saved)===row.sha256&&hash(path.join(scratch,'input-sources',row.path))===row.sha256,'SESSION_MARKER_FILES_CAPTURE_CHANGED');}for(const fact of Object.values(worker.facts??{})){if(fact.compilerInputs){need(hash(fact.compilerInputs.path)===fact.compilerInputs.sha256,'SESSION_MARKER_FILES_COMPILER_LEDGER_CHANGED');for(const row of read(fact.compilerInputs.path).rows)need(hash(row.path)===row.sha256,'SESSION_MARKER_FILES_COMPILER_INPUT_CHANGED:'+row.path);}for(const p of Object.values(fact.produced??{}))need(hash(p.path)===p.sha256,'SESSION_MARKER_FILES_NATIVE_OUTPUT_CHANGED');}workerEsmSourceEvidence=verifyWorkerEsmCoverage(scratch,snapshot);}catch(e){validationErrors.push(e.message);}
 // Host Job returned and disposed descendants before archive or exact-owner cleanup.
 const mapping=archiveScratch(owner,root);write(root+'/execution-archive-mapping.json',mapping);let cleanup={root:scratch,cleaned:false,retainedForFailure:true};if(naturalJob&&worker.status==='PASS'&&!sourceChanges.length&&!toolChanges.length&&!validationErrors.length)cleanup=cleanupScratch(owner,root,mapping);write(root+'/scratch-cleanup.json',cleanup);const complete=naturalJob&&worker.status==='PASS'&&!sourceChanges.length&&!toolChanges.length&&!validationErrors.length&&cleanup.cleaned&&!prototype;
 const result={schemaVersion:1,id:CASE_ID,sourceReview,compilerSourceReview,workerEsmSourceEvidence,status:complete?'PASS':'FAIL',passed:complete,prototypeOnly:prototype,prototypeWorkerPassed:worker.status==='PASS',workerError:worker.error??null,validationErrors,naturalJob,sourceChanges,toolChanges,sourceHashes:[...sources.map(({path,sha256})=>({path,sha256})),...walk(root).map(path=>({path,sha256:hash(path)}))],executionArchive:{mapping:root+'/execution-archive-mapping.json',mappingSha256:hash(root+'/execution-archive-mapping.json'),originalRoot:scratch,rawWorkerResult:root+'/execution/worker-result.json',archiveFiles:mapping.files.length,originalContentsRewritten:false,cleaned:cleanup.cleaned},inputConsumerStateFeedback:{input:'Exact current seven definitions and immutable17 scenarios with real owned marker files',consumer:'Four marker targets, two actual exit callers and one pure helper',state:'Actual create/truncate/write/flush/close/read/refusal/retry and cross-process predecessor recognition',feedback:'Nine exact compiled semantic source faults; independent parent file bytes and natural child exits'},boundary:{actualNativeDefinitions:7,newActualTargets:4,previousActualCallerSupports:2,additionalPureSupports:1,realOwnedFileIO:true,actualCrossOwnedProcessFileRoundtrip:true,pathClockPidAndLifecycleLogsMemory:true,otherResumeCancelPortsMemory:true,directConcurrentHelperAccepted:false,concurrentExitFastPathAccepted:false,realUserFiles:false,realProcessSuspension:false,realSleep:false,realHost:false,realSteam:false,physicalDevices:false,ownedWindowsJob:true,allBytesArchivedWithoutRewriting:true,fullProductAcceptance:false},compileFailureCountsAsNegative:false,remainingGaps:['Direct concurrent MarkClean and cleanup completion fast path; mid-write disk-full and power-loss durability; user data and real process/OS/Host/physical behavior remain unaccepted'],goalComplete:false};write(root+'/result.json',result);console.log(JSON.stringify({id:CASE_ID,status:result.status,prototypeOnly:prototype,workerStatus:worker.status,error:result.workerError,validationErrors,sourceChanges,toolChanges,nestedRows:result.sourceHashes.length,archived:mapping.files.length,cleaned:cleanup.cleaned,root}));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const args=process.argv.slice(2);need(args[0]==='--evidence-dir'&&typeof args[1]==='string'&&args.length===2,'SESSION_MARKER_FILES_ARGUMENTS');process.exitCode=runSleepSessionMarkerFiles(args[1]).status==='PASS'?0:2;}catch(e){console.error(e.stack);process.exitCode=2;}}
