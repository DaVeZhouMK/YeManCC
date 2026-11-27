import {verifyDirectorySourceSuccessor} from './source-successor.mjs';
import fs from'node:fs';import path from'node:path';import{spawnSync}from'node:child_process';import{createHash}from'node:crypto';import{fileURLToPath}from'node:url';
import{REPO,MODULE,CASE_ID,SOURCE_SUCCESSOR,inputFiles,toolchainSpec}from'./adapter.mjs';
import{allocateScratch,verifyScratch,archiveScratch,cleanupScratch}from'../sleep-native-recovery-consumer/scratch.mjs';import{evidencePath,verifyOwnedJobReceipt}from'../sleep-native-recovery-consumer/run.mjs';export{evidencePath};
const hash=f=>createHash('sha256').update(fs.readFileSync(f)).digest('hex'),read=f=>JSON.parse(fs.readFileSync(f,'utf8').replace(/^\uFEFF/,'')),write=(f,j)=>fs.writeFileSync(f,JSON.stringify(j,null,2)+'\n'),need=(v,m)=>{if(!v)throw Error(m);};
function walk(dir){const rows=[];for(const entry of fs.readdirSync(dir,{withFileTypes:true})){need(!entry.isSymbolicLink(),'DIRECTORY_RESUME_OUTPUT_REPARSE');const p=path.join(dir,entry.name);if(entry.isDirectory())rows.push(...walk(p));else{need(entry.isFile(),'DIRECTORY_RESUME_OUTPUT_TYPE');rows.push(p);}need(rows.length<12000,'DIRECTORY_RESUME_OUTPUT_LIMIT');}return rows.sort();}
export function inspectDirectoryOwnedJob(job,receipt,deadlineEpochMs,cleanupReserveMs){
 const markers=[...(job?.stdout??'').toString().matchAll(/^YMCC_SIM_OWNED_JOB childPid=([1-9][0-9]*)\r?$/gm)];
 const bound=receipt?.schemaVersion===1&&receipt.jobAssigned===true&&receipt.childExitedBeforeReturn===true&&Number.isSafeInteger(receipt.childPid)&&receipt.childPid>0&&receipt.deadlineEpochMs===deadlineEpochMs&&receipt.cleanupReserveMs===cleanupReserveMs&&markers.length===1&&Number(markers[0][1])===receipt.childPid;
 const resourcesReturned=bound&&receipt.resourceObservationVersion===1&&Number.isSafeInteger(receipt.jobQueryCount)&&receipt.jobQueryCount>0&&receipt.activeProcessesFinal===0&&receipt.zeroObservedBeforeDispose===true&&receipt.jobDisposed===true&&(receipt.cleanupError===null||receipt.cleanupError==='');
 const naturalExit=resourcesReturned&&job?.status===0&&!job.error&&!job.signal&&receipt.status==='EXITED'&&receipt.exitCode===0&&receipt.rootExitCode===0&&receipt.deadlineExpired===false&&Number.isSafeInteger(receipt.finishedEpochMs)&&receipt.finishedEpochMs>0&&receipt.finishedEpochMs<=deadlineEpochMs&&receipt.activeProcessesBeforeCleanup===0&&receipt.terminationRequested===false&&receipt.terminationSucceeded===false;
 return {resourcesReturned:!!resourcesReturned,naturalExit:!!naturalExit,childPid:receipt?.childPid??null,rootExitCode:receipt?.rootExitCode??null};
}
export function qualifyDirectoryCompiler(receipt,tool,{deadlineEpochMs,cleanupReserveMs,stdout}){
 const pids=[...stdout.matchAll(/^YMCC_SIM_OWNED_JOB childPid=([1-9][0-9]*)\r?$/gm)];
 need(Number.isSafeInteger(deadlineEpochMs)&&deadlineEpochMs>0&&cleanupReserveMs===1000&&
  receipt?.deadlineEpochMs===deadlineEpochMs&&receipt.cleanupReserveMs===cleanupReserveMs&&
  Number.isSafeInteger(receipt.finishedEpochMs)&&receipt.finishedEpochMs>0&&receipt.finishedEpochMs<=deadlineEpochMs&&
  Number.isSafeInteger(receipt.jobQueryCount)&&receipt.jobQueryCount>=2&&pids.length===1&&Number(pids[0][1])===receipt.childPid&&
  receipt.status==='EXITED'&&receipt.activeProcessesBeforeCleanup===(receipt.terminationRequested?1:0)&&
  receipt.terminationSucceeded===receipt.terminationRequested,'DIRECTORY_RESUME_COMPILER_JOB_BINDING');
 return verifyOwnedJobReceipt(receipt,tool).kind;
}
function verifyDirectoryCompilerPhases(scratch,tool,spec,worker,deadlineEpochMs,cleanupReserveMs){
 const labels=['os-probe',...['native-positive',...spec.nativeFaults.map(x=>x.label)].filter(x=>worker.facts[x]?.status!=='SHARED_REFERENCE'),
  'live-positive',...spec.liveFaults.map(x=>x.label)];
 const records=worker.compilerPhases;
 need(Array.isArray(records)&&JSON.stringify(records.map(x=>x.label))===JSON.stringify(labels),'DIRECTORY_RESUME_COMPILER_PHASE_SET');
 const seen=new Set();
 for(const row of records){
  const root=path.join(scratch,row.label==='os-probe'?'os-port-probe':row.label),cmd=path.join(root,'compile.cmd');
  const bind=(p,relative,sha)=>path.normalize(p)===path.join(root,relative)&&hash(p)===sha;
  need(bind(row.cmd,'compile.cmd',row.cmdSha256)&&bind(row.args,'compiler-job-args.json',row.argsSha256)&&
   bind(row.receipt,'compiler-job-receipt.json',row.receiptSha256)&&
   path.normalize(row.stdout)===path.join(scratch,row.label+'-compile.stdout.log')&&hash(row.stdout)===row.stdoutSha256&&
   path.normalize(row.stderr)===path.join(scratch,row.label+'-compile.stderr.log')&&hash(row.stderr)===row.stderrSha256&&
   row.deadlineEpochMs<=deadlineEpochMs-cleanupReserveMs&&row.exitCode===0,'DIRECTORY_RESUME_COMPILER_PHASE_ARTIFACT:'+row.label);
  const argv=read(row.args),receipt=read(row.receipt);
  need(Array.isArray(argv)&&argv.length===3&&argv[0]==='/d'&&argv[1]==='/c'&&path.normalize(argv[2])===cmd&&
   path.resolve(row.compilerProgram).toLowerCase()===path.resolve(tool.cmdExecutable).toLowerCase()&&
   hash(row.compilerProgram)===row.compilerProgramSha256,'DIRECTORY_RESUME_COMPILER_PHASE_COMMAND:'+row.label);
  const key=receipt.childPid+':'+receipt.finishedEpochMs;
  need(!seen.has(key)&&qualifyDirectoryCompiler(receipt,tool,{...row,stdout:fs.readFileSync(row.stdout,'utf8')})===row.kind,
   'DIRECTORY_RESUME_COMPILER_PHASE_TERMINAL:'+row.label);
  seen.add(key);
 }
}
export function runSleepDirectoryResume(value,{prototype=false}={}){
 const root=evidencePath(value),specPath=MODULE+'/spec.json',captured=new Map();
 const capture=relative=>{if(!captured.has(relative))captured.set(relative,fs.readFileSync(path.join(REPO,relative)));return captured.get(relative);};
 const names=inputFiles(REPO,capture).sort();for(const name of names)capture(name);
 const sourceCompatibilityReview=verifyDirectorySourceSuccessor(REPO,specPath,SOURCE_SUCCESSOR,captured);
 const spec=JSON.parse(capture(specPath)),tool=toolchainSpec(REPO);
 need(path.resolve(process.execPath).toLowerCase()===path.resolve(tool.nodeExecutable).toLowerCase(),'DIRECTORY_RESUME_NODE_NOT_PINNED');
 // Fixture, bridge and menu bytes are pinned by the current continuation checked above.
 for(const row of tool.files)need(hash(row.path)===row.sha256,'DIRECTORY_RESUME_TOOL_CHANGED:'+row.path);
 fs.mkdirSync(root,{recursive:true});const sources=names.map(relative=>{const saved=path.join(root,'input-sources',relative),bytes=capture(relative);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.writeFileSync(saved,bytes,{flag:'wx'});return{path:relative,saved,sha256:createHash('sha256').update(bytes).digest('hex')};}),snapshot={schemaVersion:1,module:MODULE,evidenceRoot:root,createdAt:new Date().toISOString(),sources,tool};if(process.env.YMCC_SLEEP_DIRECTORY_NATIVE_REFERENCE){const referencePath=path.resolve(process.env.YMCC_SLEEP_DIRECTORY_NATIVE_REFERENCE);need(!referencePath.startsWith(REPO+path.sep),'DIRECTORY_RESUME_REFERENCE_INSIDE_SOURCE');const saved=root+'/native-reference.json';fs.copyFileSync(referencePath,saved,fs.constants.COPYFILE_EXCL);snapshot.nativeReference={path:saved,sha256:hash(saved)};}write(root+'/input-snapshot.json',snapshot);const owner=allocateScratch(root),scratch=verifyScratch(owner,root);write(root+'/scratch-allocation.json',owner);const executed=sources.map(row=>{const saved=path.join(scratch,'input-sources',row.path);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.copyFileSync(row.saved,saved,fs.constants.COPYFILE_EXCL);need(hash(saved)===row.sha256,'DIRECTORY_RESUME_EXECUTION_COPY_CHANGED');return{...row,saved};});write(scratch+'/input-snapshot.json',{...snapshot,sources:executed});write(scratch+'/execution-owner.json',owner);const args=root+'/job-arguments.json';write(args,['-Node',tool.nodeExecutable,'-Worker',path.join(scratch,'input-sources',MODULE,'worker.mjs'),'-Root',scratch]);const env={...process.env};delete env.NODE_OPTIONS;delete env.NODE_PATH;
 const cleanupReserveMs=10000,deadlineEpochMs=Date.now()+480000,receiptFile=root+'/job-receipt.json';
 fs.writeFileSync(scratch+'/compiler-job-budget.json',JSON.stringify({deadlineEpochMs,cleanupReserveMs})+'\n',{flag:'wx'});
 const job=spawnSync(tool.powershellExecutable,['-NoLogo','-NoProfile','-File',path.join(scratch,'input-sources/tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(scratch,'input-sources/tools/ymcc_sim/global/sleep-native-recovery-consumer/worker.ps1'),'-ArgumentsPath',args,'-DeadlineEpochMs',String(deadlineEpochMs),'-CleanupReserveMs',String(cleanupReserveMs),'-DeadlineReceiptPath',receiptFile],{env,cwd:scratch,windowsHide:true,timeout:480000,maxBuffer:4*1024*1024});fs.writeFileSync(root+'/job.stdout.log',job.stdout??'');fs.writeFileSync(root+'/job.stderr.log',job.stderr??'');const workerFile=scratch+'/worker-result.json',worker=fs.existsSync(workerFile)?read(workerFile):{status:'FAIL',error:'DIRECTORY_RESUME_WORKER_RESULT_MISSING'},sourceChanges=sources.filter(x=>hash(path.join(REPO,x.path))!==x.sha256).map(x=>x.path),toolChanges=tool.files.filter(x=>hash(x.path)!==x.sha256).map(x=>x.path),validationErrors=[];
 let receipt=null;try{if(fs.existsSync(receiptFile))receipt=read(receiptFile);}catch(error){validationErrors.push('DIRECTORY_RESUME_JOB_RECEIPT_UNREADABLE:'+error.message);}
 const ownedJob=inspectDirectoryOwnedJob(job,receipt,deadlineEpochMs,cleanupReserveMs);
 if(worker.status==='PASS')try{verifyDirectoryCompilerPhases(scratch,tool,spec,worker,deadlineEpochMs,cleanupReserveMs);}catch(e){validationErrors.push(e.message);}
 try{
  verifyScratch(owner,root);
  for(const row of sources)need(hash(row.saved)===row.sha256&&hash(path.join(scratch,'input-sources',row.path))===row.sha256,'DIRECTORY_RESUME_CAPTURE_CHANGED');
  let sharedArchive;
  for(const fact of Object.values(worker.facts??{})){
   if(fact.status==='SHARED_REFERENCE'){
    need(snapshot.nativeReference&&hash(snapshot.nativeReference.path)===snapshot.nativeReference.sha256,'DIRECTORY_NATIVE_REFERENCE_CHANGED');
    const ref=read(snapshot.nativeReference.path),mapping=ref.pins.mapping;
    need(fact.sharedReference?.pins?.mapping?.sha256===mapping.sha256&&hash(mapping.path)===mapping.sha256,'DIRECTORY_NATIVE_REFERENCE_MAPPING_CHANGED');
    sharedArchive??=new Map(read(mapping.path).files.map(row=>[path.resolve(row.originalPath).toLowerCase(),row]));
   }
   const locate=(file,sha)=>{
    const archived=fact.status==='SHARED_REFERENCE'?sharedArchive.get(path.resolve(file).toLowerCase()):null;
    if(archived)need(archived.sha256===sha,'DIRECTORY_NATIVE_REFERENCE_ARCHIVE_CHANGED:'+file);
    return archived?.archivedPath??file;
   };
   if(fact.compilerInputs){
    const ledger=locate(fact.compilerInputs.path,fact.compilerInputs.sha256);
    need(hash(ledger)===fact.compilerInputs.sha256,'DIRECTORY_RESUME_COMPILER_LEDGER_CHANGED');
    for(const row of read(ledger).rows)need(hash(locate(row.path,row.sha256))===row.sha256,'DIRECTORY_RESUME_COMPILER_INPUT_CHANGED:'+row.path);
   }
   for(const p of Object.values(fact.produced??{}))need(hash(locate(p.path,p.sha256))===p.sha256,'DIRECTORY_RESUME_NATIVE_OUTPUT_CHANGED');
  }
 }catch(e){validationErrors.push(e.message);}
 // Archive only once the guard has proved the original Job is empty and closed.
 const mapping=ownedJob.resourcesReturned?archiveScratch(owner,root):null;if(mapping)write(root+'/execution-archive-mapping.json',mapping);const naturalJob=ownedJob.naturalExit;let cleanup={root:scratch,cleaned:false,retainedForFailure:true};if(naturalJob&&worker.status==='PASS'&&!sourceChanges.length&&!toolChanges.length&&!validationErrors.length)cleanup=cleanupScratch(owner,root,mapping);write(root+'/scratch-cleanup.json',cleanup);const complete=naturalJob&&worker.status==='PASS'&&!sourceChanges.length&&!toolChanges.length&&!validationErrors.length&&cleanup.cleaned&&!prototype;
 const result={schemaVersion:1,id:CASE_ID,sourceCompatibilityReview,status:complete?'PASS':'FAIL',passed:complete,prototypeOnly:prototype,prototypeWorkerPassed:worker.status==='PASS',workerError:worker.error??null,validationErrors,naturalJob,ownedJob,sourceChanges,toolChanges,sourceHashes:[...sources.map(({path,sha256})=>({path,sha256})),...walk(root).map(path=>({path,sha256:hash(path)}))],executionArchive:mapping?{mapping:root+'/execution-archive-mapping.json',mappingSha256:hash(root+'/execution-archive-mapping.json'),originalRoot:scratch,rawWorkerResult:root+'/execution/worker-result.json',archiveFiles:mapping.files.length,originalContentsRewritten:false,cleaned:cleanup.cleaned}:null,inputConsumerStateFeedback:{input:'Exact native main and JSON plus fixed memory PID/session/marker/admission context',consumer:'8 native targets,6 native supports,2 bridge targets and actual UI toggle function body',state:'Native70 and live26 fixed positives assert opened-handle identity,uncertainty preservation,query errors,complete feedback,durable pending state and actual second-click recovery',feedback:'13 native source faults naturally return23 on specified70 checks;5 actual live source faults trigger chain consumer23 while RPC child naturally returns0; six OS refusals,compiler inputs and exact archives'},boundary:{compiledNativeFunctions:14,nativeTargets:8,markerParsers:3,jsonResultParser:1,localHelpers:2,bridgeTargets:2,uiFunctionBodies:1,liveResponsesReplayed:false,productionWebViewTransportAccepted:false,mountedVueAccepted:false,memoryCheckedMarkerReads:true,memoryQueryAndLiveness:true,realNT:false,realUserProcess:false,realHost:false,realSteam:false,realMarkerPersistence:false,realSleep:false,physicalDevices:false,ownedWindowsJob:true,portableGuardAvoidsWindowsHandleTypeCollision:true,shortRootExactCaseOwned:true,allBytesArchivedWithoutRewriting:true,originalFixtureUnchanged:false,originalFixedAssertionsRetained:true,peerLeaseAndPublicPowerPortsMemory:true,fullProductAcceptance:false},compileFailureCountsAsNegative:false,remainingGaps:['Real process suspension,NT,marker persistence,OS sleep,Host/Steam and physical behavior are not accepted'],goalComplete:false};write(root+'/result.json',result);console.log(JSON.stringify({id:CASE_ID,status:result.status,prototypeOnly:prototype,workerStatus:worker.status,error:result.workerError,validationErrors,sourceChanges,toolChanges,nestedRows:result.sourceHashes.length,archived:mapping?.files.length??0,cleaned:cleanup.cleaned,root}));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const args=process.argv.slice(2),prototype=args.includes('--prototype');need(args[0]==='--evidence-dir'&&typeof args[1]==='string'&&args.length===(prototype?3:2),'DIRECTORY_RESUME_ARGUMENTS');process.exitCode=runSleepDirectoryResume(args[1],{prototype}).status==='PASS'?0:2;}catch(e){console.error(e.stack);process.exitCode=2;}}
