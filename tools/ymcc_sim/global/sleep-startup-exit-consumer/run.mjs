import {verifyCompilerSourceReview,verifyWorkerEsmCoverage} from '../sleep-exit-inflight-consumer/compiler-source-review.mjs';
import fs from'node:fs';import path from'node:path';import{spawnSync}from'node:child_process';import{createHash}from'node:crypto';import{fileURLToPath}from'node:url';
import{REPO,MODULE,CASE_ID,inputFiles,toolchainSpec}from'./adapter.mjs';
import{allocateScratch,verifyScratch,archiveScratch,cleanupScratch}from'../sleep-native-recovery-consumer/scratch.mjs';import{evidencePath}from'../sleep-native-recovery-consumer/run.mjs';export{evidencePath};
const hash=f=>createHash('sha256').update(fs.readFileSync(f)).digest('hex'),read=f=>JSON.parse(fs.readFileSync(f,'utf8').replace(/^\uFEFF/,'')),write=(f,j)=>fs.writeFileSync(f,JSON.stringify(j,null,2)+'\n'),need=(v,m)=>{if(!v)throw Error(m);};
export function requireStartupExitOwnedReturn(job,receipt,{deadlineEpochMs,cleanupReserveMs}){
 const requireProof=(ok,reason)=>need(ok,'STARTUP_EXIT_OWNED_JOB_NOT_RETIRED:'+reason);
 requireProof(receipt?.schemaVersion===1,'RECEIPT');
 requireProof(receipt.jobAssigned===true&&Number.isSafeInteger(receipt.childPid)&&receipt.childPid>0&&receipt.childExitedBeforeReturn===true,'ASSIGNED_CHILD_EXIT');
 requireProof(receipt.resourceObservationVersion===1&&Number.isSafeInteger(receipt.jobQueryCount)&&receipt.jobQueryCount>=2,'ACCOUNTING');
 const active=receipt.activeProcessesBeforeCleanup,forced=receipt.terminationRequested;
 requireProof(Number.isSafeInteger(active)&&active>=0&&forced===(active>0)&&receipt.terminationSucceeded===forced,'CLEANUP_ACCOUNTING');
 requireProof(receipt.zeroObservedBeforeDispose===true&&receipt.activeProcessesFinal===0&&receipt.jobDisposed===true,'ZERO_BEFORE_DISPOSE');
 requireProof(receipt.cleanupError===null||receipt.cleanupError==='','CLEANUP_ERROR');
 requireProof(Number.isSafeInteger(deadlineEpochMs)&&deadlineEpochMs>0&&receipt.deadlineEpochMs===deadlineEpochMs&&receipt.cleanupReserveMs===cleanupReserveMs,'DEADLINE_BINDING');
 requireProof(Number.isSafeInteger(receipt.finishedEpochMs)&&receipt.finishedEpochMs>0&&receipt.finishedEpochMs<=deadlineEpochMs,'TERMINAL_AFTER_DEADLINE');
 return !forced&&job.status===0&&!job.signal&&!job.error&&receipt.status==='EXITED'&&receipt.exitCode===0&&receipt.rootExitCode===0&&receipt.deadlineExpired===false;
}
export function verifyStartupExitBuildEvidence(facts,readBytes=fs.readFileSync){
 const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
 // Reuse observations only within this post-Job compiler-input checkpoint.
 const checkedInputs=new Map();
 for(const fact of Object.values(facts??{})){
  if(fact.compilerInputs){
   const {path,sha256}=fact.compilerInputs,bytes=readBytes(path);
   need(digest(bytes)===sha256,'STARTUP_EXIT_COMPILER_LEDGER_CHANGED');
   for(const row of JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,'')).rows){
    if(!checkedInputs.has(row.path))checkedInputs.set(row.path,digest(readBytes(row.path)));
    need(checkedInputs.get(row.path)===row.sha256,'STARTUP_EXIT_COMPILER_INPUT_CHANGED:'+row.path);
   }
  }
  for(const p of Object.values(fact.produced??{}))need(digest(readBytes(p.path))===p.sha256,'STARTUP_EXIT_NATIVE_OUTPUT_CHANGED');
 }
}
function walk(dir){const rows=[];for(const entry of fs.readdirSync(dir,{withFileTypes:true})){need(!entry.isSymbolicLink(),'STARTUP_EXIT_OUTPUT_REPARSE');const p=path.join(dir,entry.name);if(entry.isDirectory())rows.push(...walk(p));else{need(entry.isFile(),'STARTUP_EXIT_OUTPUT_TYPE');rows.push(p);}need(rows.length<12000,'STARTUP_EXIT_OUTPUT_LIMIT');}return rows.sort();}
export function runSleepStartupExit(value,{prototype=false}={}){
 const root=evidencePath(value),spec=read(path.join(REPO,MODULE,'spec.json'));
 need(hash(path.join(REPO,spec.fixture.path))===spec.fixture.sha256,'STARTUP_EXIT_FIXTURE_NEEDS_REVIEW');
 for(const row of spec.reviewedSharedLeafIdentities)need(hash(path.join(REPO,row.path))===row.sha256,'STARTUP_EXIT_SHARED_LEAF_CHANGED:'+row.path);
 const compilerSourceReview=verifyCompilerSourceReview(REPO);
 const tool=toolchainSpec(REPO);
 need(path.resolve(process.execPath).toLowerCase()===path.resolve(tool.nodeExecutable).toLowerCase(),'STARTUP_EXIT_NODE_NOT_PINNED');
 for(const row of tool.files)need(hash(row.path)===row.sha256,'STARTUP_EXIT_TOOL_CHANGED:'+row.path);
 fs.mkdirSync(root,{recursive:true});const sources=inputFiles(REPO).sort().map(relative=>{const saved=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.copyFileSync(path.join(REPO,relative),saved,fs.constants.COPYFILE_EXCL);return{path:relative,saved,sha256:hash(saved)};}),snapshot={schemaVersion:1,module:MODULE,evidenceRoot:root,createdAt:new Date().toISOString(),sources,tool};write(root+'/input-snapshot.json',snapshot);
 const captured=new Map(sources.map(x=>[x.path,x.sha256]));for(const row of [...spec.reviewedSharedLeafIdentities,spec.fixture])need(captured.get(row.path)===row.sha256,'STARTUP_EXIT_CAPTURED_REVIEW_CHANGED:'+row.path);
 const owner=allocateScratch(root),scratch=verifyScratch(owner,root);write(root+'/scratch-allocation.json',owner);const executed=sources.map(row=>{const saved=path.join(scratch,'input-sources',row.path);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.copyFileSync(row.saved,saved,fs.constants.COPYFILE_EXCL);need(hash(saved)===row.sha256,'STARTUP_EXIT_EXECUTION_COPY_CHANGED');return{...row,saved};});write(scratch+'/input-snapshot.json',{...snapshot,sources:executed});write(scratch+'/execution-owner.json',owner);
 const args=root+'/job-arguments.json',jobReceiptPath=root+'/owned-job.json';write(args,[path.join(scratch,'input-sources',MODULE,'worker.mjs'),scratch]);const env={...process.env};delete env.NODE_OPTIONS;delete env.NODE_PATH;env.NODE_V8_COVERAGE=path.join(scratch,'esm-coverage');fs.mkdirSync(env.NODE_V8_COVERAGE);
 const jobDeadlineEpochMs=Date.now()+200000,jobCleanupReserveMs=8000;
 const job=spawnSync(tool.powershellExecutable,['-NoLogo','-NoProfile','-File',path.join(scratch,'input-sources/tools/ymcc_sim/global/host-process.ps1'),'-ProgramPath',tool.nodeExecutable,'-ArgumentsPath',args,'-DeadlineEpochMs',String(jobDeadlineEpochMs),'-CleanupReserveMs',String(jobCleanupReserveMs),'-DeadlineReceiptPath',jobReceiptPath],{env,cwd:scratch,windowsHide:true,timeout:220000,maxBuffer:4*1024*1024});fs.writeFileSync(root+'/job.stdout.log',job.stdout??'');fs.writeFileSync(root+'/job.stderr.log',job.stderr??'');
 const ownedJob=read(jobReceiptPath),naturalJob=requireStartupExitOwnedReturn(job,ownedJob,{deadlineEpochMs:jobDeadlineEpochMs,cleanupReserveMs:jobCleanupReserveMs});
 const workerFile=scratch+'/worker-result.json',worker=fs.existsSync(workerFile)?read(workerFile):{status:'FAIL',error:'STARTUP_EXIT_WORKER_RESULT_MISSING'},sourceChanges=sources.filter(x=>hash(path.join(REPO,x.path))!==x.sha256).map(x=>x.path),toolChanges=tool.files.filter(x=>hash(x.path)!==x.sha256).map(x=>x.path),validationErrors=[];let workerEsmSourceEvidence=null;
 try{verifyScratch(owner,root);for(const row of sources){need(hash(row.saved)===row.sha256&&hash(path.join(scratch,'input-sources',row.path))===row.sha256,'STARTUP_EXIT_CAPTURE_CHANGED');}verifyStartupExitBuildEvidence(worker.facts);workerEsmSourceEvidence=verifyWorkerEsmCoverage(scratch,snapshot);}catch(e){validationErrors.push(e.message);}
 // Proven forced return permits archive, never natural cleanup.
 const mapping=archiveScratch(owner,root);write(root+'/execution-archive-mapping.json',mapping);let cleanup={root:scratch,cleaned:false,retainedForFailure:true};if(naturalJob&&worker.status==='PASS'&&!sourceChanges.length&&!toolChanges.length&&!validationErrors.length)cleanup=cleanupScratch(owner,root,mapping);write(root+'/scratch-cleanup.json',cleanup);const complete=naturalJob&&worker.status==='PASS'&&!sourceChanges.length&&!toolChanges.length&&!validationErrors.length&&cleanup.cleaned&&!prototype;
 const result={schemaVersion:1,id:CASE_ID,compilerSourceReview,workerEsmSourceEvidence,status:complete?'PASS':'FAIL',passed:complete,prototypeOnly:prototype,prototypeWorkerPassed:worker.status==='PASS',workerError:worker.error??null,validationErrors,naturalJob,ownedJob,sourceChanges,toolChanges,sourceHashes:[...sources.map(({path,sha256})=>({path,sha256})),...walk(root).map(path=>({path,sha256:hash(path)}))],executionArchive:{mapping:root+'/execution-archive-mapping.json',mappingSha256:hash(root+'/execution-archive-mapping.json'),originalRoot:scratch,rawWorkerResult:root+'/execution/worker-result.json',archiveFiles:mapping.files.length,originalContentsRewritten:false,cleaned:cleanup.cleaned},inputConsumerStateFeedback:{input:'Exact actual repaired native definitions and fixed memory marker/error/process context',consumer:'Actual SleepWork wait and startup consumption plus original marker and exit cleanup controls',state:'27 fixed checks on unknown directory/query/type/increment errors,clean flag ownership,idempotence,actual retry and owned threads',feedback:'9 complete native source faults compile/start then specified checks naturally return23; six actual OS refusal probes,compiler/link and exact archives'},boundary:{actualNativeDefinitions:spec.totalFullDefinitions,newActualTargets:spec.newTargetFunctions.length,previousActualSupports:spec.previousActualSupportFunctions.length,additionalPureSupports:spec.additionalPureSupportFunctions.length,realOwnedFixtureThreads:true,startupWaitAndConsumptionActual:true,wakeCancelAndSessionMarkerPortsMemory:true,realNT:false,realUserProcesses:false,realPersistence:false,realSleep:false,realHost:false,realSteam:false,physicalDevices:false,ownedWindowsJob:true,shortRootExactCaseOwned:true,allBytesArchivedWithoutRewriting:true,fullProductAcceptance:false},compileFailureCountsAsNegative:false,remainingGaps:['Real process suspension,NT,marker persistence,OS sleep,Host/Steam and physical behavior are not accepted'],goalComplete:false};write(root+'/result.json',result);console.log(JSON.stringify({id:CASE_ID,status:result.status,prototypeOnly:prototype,workerStatus:worker.status,error:result.workerError,validationErrors,sourceChanges,toolChanges,nestedRows:result.sourceHashes.length,archived:mapping.files.length,cleaned:cleanup.cleaned,root}));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const args=process.argv.slice(2),prototype=args.includes('--prototype');need(args[0]==='--evidence-dir'&&typeof args[1]==='string'&&args.length===(prototype?3:2),'STARTUP_EXIT_ARGUMENTS');process.exitCode=runSleepStartupExit(args[1],{prototype}).status==='PASS'?0:2;}catch(e){console.error(e.stack);process.exitCode=2;}}
