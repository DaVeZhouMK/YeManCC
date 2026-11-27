import {verifyPublicWakeSource} from './public-wake-source-review.mjs';
import {verifyCurrentSleepSourceReview} from './compiler-leaf.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {REPO,MODULE,CASE_ID,inputFiles,toolchainSpec,getJoinedSenderSelection} from './adapter.mjs';
import {pinJoinedBuildReference,verifyJoinedBuildReference} from './joined-build-reference.mjs';
import {verifyOwnedJobReceipt} from '../sleep-native-recovery-consumer/run.mjs';
import {allocateScratch,verifyScratch,archiveScratch,cleanupScratch} from '../sleep-native-recovery-consumer/scratch.mjs';
import {evidencePath} from '../sleep-native-recovery-consumer/run.mjs';
export {evidencePath};
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const inside=(base,file)=>{const relative=path.relative(path.resolve(base),path.resolve(file));return !path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};

// The original guard's structured terminal is authoritative. stdout is only a
// diagnostic log: exit0 or an empty/unassigned Job cannot prove child return.
export function inspectNonUserWakeOwnedJob(job,receipt,{deadlineEpochMs,cleanupReserveMs}){
 const reasons=[],require=(ok,reason)=>{if(!ok)reasons.push(reason);};
 require(receipt?.schemaVersion===1,'JOB_RECEIPT_MISSING_OR_SCHEMA');
 require(receipt?.jobAssigned===true,'JOB_CHILD_NOT_ASSIGNED');
 require(Number.isSafeInteger(receipt?.childPid)&&receipt.childPid>0,'JOB_CHILD_IDENTITY_MISSING');
 require(receipt?.childExitedBeforeReturn===true,'JOB_CHILD_EXIT_NOT_OBSERVED');
 require(receipt?.resourceObservationVersion===1&&Number.isSafeInteger(receipt?.jobQueryCount)&&receipt.jobQueryCount>=2,'JOB_RESOURCE_OBSERVATION_MISSING');
 const initialActive=receipt?.activeProcessesBeforeCleanup,requested=receipt?.terminationRequested,succeeded=receipt?.terminationSucceeded;
 require(Number.isSafeInteger(initialActive)&&initialActive>=0,'JOB_INITIAL_ACCOUNTING_MISSING');
 require(typeof requested==='boolean'&&typeof succeeded==='boolean','JOB_CLEANUP_POLARITY_MISSING');
 // The original guard requests termination exactly when its first query sees active members.
 require(requested===(initialActive>0)&&succeeded===requested,'JOB_CLEANUP_ACCOUNTING_CONTRADICTION');
 require(receipt?.activeProcessesFinal===0&&receipt?.zeroObservedBeforeDispose===true,'JOB_ZERO_BEFORE_DISPOSE_NOT_PROVEN');
 require(receipt?.jobDisposed===true,'JOB_DISPOSE_NOT_PROVEN');
 require(receipt?.cleanupError===null||receipt?.cleanupError==='','JOB_CLEANUP_ERROR');
 require(Number.isSafeInteger(deadlineEpochMs)&&deadlineEpochMs>0&&receipt?.deadlineEpochMs===deadlineEpochMs&&receipt?.cleanupReserveMs===cleanupReserveMs,'JOB_DEADLINE_BINDING_MISMATCH');
 require(Number.isSafeInteger(receipt?.finishedEpochMs)&&receipt.finishedEpochMs>0&&receipt.finishedEpochMs<=deadlineEpochMs,'JOB_TERMINAL_OUTSIDE_DEADLINE');
 const resourcesReturned=reasons.length===0;
 require(job?.status===0&&!job?.error&&!job?.signal,'JOB_GUARD_PROCESS_FAILED');
 require(receipt?.status==='EXITED'&&receipt?.exitCode===0&&receipt?.rootExitCode===0&&receipt?.deadlineExpired===false,'JOB_ORIGINAL_CHILD_NOT_SUCCESSFUL');
 require(receipt?.terminationRequested===false&&receipt?.terminationSucceeded===false,'JOB_FORCED_CLEANUP_IS_NOT_NATURAL_EXIT');
 return {status:reasons.length?'FAIL':'PASS',resourcesReturned,naturalExit:reasons.length===0,reasons,
  rootExitCode:receipt?.rootExitCode??null,deadlineExpired:receipt?.deadlineExpired??null,
  forcedCleanup:receipt?.terminationRequested??null,childPid:receipt?.childPid??null};
}

function walk(root){const files=[];function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);need(!entry.isSymbolicLink(),'NATIVE_RECOVERY_OUTPUT_REPARSE');if(entry.isDirectory())visit(file);else{need(entry.isFile(),'NATIVE_RECOVERY_OUTPUT_TYPE');files.push(file);}need(files.length<12000,'NATIVE_RECOVERY_OUTPUT_LIMIT');}}visit(root);return files.sort();}
function captureInputs(root,spec,tool,selection='legacy',resumeSenderCase=null,resumeSenderBuildReference=null){
 const sources=inputFiles(REPO).sort().map(relative=>{const original=path.join(REPO,relative),bytes=fs.readFileSync(original),saved=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.writeFileSync(saved,bytes,{flag:'wx'});return{path:relative,sha256:createHash('sha256').update(bytes).digest('hex'),saved};});
 const snapshot={schemaVersion:1,module:MODULE,createdAt:new Date().toISOString(),sources,tool,selection,resumeSenderCase,resumeSenderBuildReference};write(path.join(root,'input-snapshot.json'),snapshot);return snapshot;
}
function prepareExecution(record,evidence,snapshot){
 const scratch=verifyScratch(record,evidence),sources=snapshot.sources.map(row=>{const saved=path.join(scratch,'input-sources',row.path);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.copyFileSync(row.saved,saved,fs.constants.COPYFILE_EXCL);need(hash(saved)===row.sha256,'EXECUTION_SOURCE_COPY_CHANGED');return{...row,saved};});
 write(path.join(scratch,'input-snapshot.json'),{...snapshot,sources});write(path.join(scratch,'execution-owner.json'),record);return scratch;
}
function verifyExecutedArtifacts(scratch,worker,snapshot){
 const joined=worker.facts?.['resume-sender'];
 if(joined?.compileReused){
  need(snapshot.resumeSenderBuildReference&&JSON.stringify(joined.buildReference?.authority)===JSON.stringify(snapshot.resumeSenderBuildReference),'EXECUTED_BUILD_REFERENCE_AUTHORITY_CHANGED');
  need(inside(scratch,joined.buildContext.path)&&hash(joined.buildContext.path)===joined.buildContext.sha256,'EXECUTED_BUILD_CONTEXT_CHANGED');
  verifyJoinedBuildReference(snapshot.resumeSenderBuildReference,{snapshot,context:read(joined.buildContext.path)});
 }
 const roots=[worker.facts?.osBoundary?.compilerInputs,worker.facts?.positive?.actualCompilerInputs,worker.facts?.['negative-budget']?.actualCompilerInputs,worker.facts?.['negative-suspend']?.actualCompilerInputs,worker.facts?.['resume-classification']?.actualCompilerInputs,worker.facts?.['resume-sender']?.actualCompilerInputs].filter(Boolean);
 for(const ledger of roots){need(inside(scratch,ledger.path)&&hash(ledger.path)===ledger.sha256,'EVALUATED_LEDGER_CHANGED');for(const row of read(ledger.path).rows)need(hash(row.path)===row.sha256,'EXECUTED_COMPILER_OR_LINK_INPUT_CHANGED:'+row.path);}
 for(const label of ['positive','negative-budget','negative-suspend','resume-classification','resume-sender']){const fact=worker.facts?.[label];if(!fact)continue;if(label==='resume-sender'&&!fact.calibrationNode){need(fact.calibrationNodeInvoked===false,'EXECUTED_CALIBRATION_NODE_RECEIPT_CHANGED');}else for(const row of [fact.calibrationNode,fact.calibrationNode?.configuration]){need(row&&inside(scratch,row.path)&&hash(row.path)===row.sha256,'EXECUTED_CALIBRATION_NODE_RECEIPT_CHANGED');}for(const row of Object.values(fact.produced??{}))need(inside(scratch,row.path)&&hash(row.path)===row.sha256,'EXECUTED_NATIVE_ARTIFACT_CHANGED');for(const row of fact.pythonAudits??[])need(inside(scratch,row.path)&&hash(row.path)===row.sha256,'EXECUTED_PYTHON_AUDIT_CHANGED');}
}
const writeNew=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
// The original Job guard observes each phase; compiler cleanup never qualifies business.
const samePath=(a,b)=>path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();

function inspect(job,tool,phase,expectedExit,deadlineEpochMs,cleanupReserveMs){
 need(job.schemaVersion===1&&job.resourceObservationVersion===1&&job.jobAssigned===true&&job.childExitedBeforeReturn===true&&
  Number.isSafeInteger(job.childPid)&&job.childPid>0&&job.jobDisposed===true&&job.deadlineExpired===false&&
  job.deadlineEpochMs===deadlineEpochMs&&job.cleanupReserveMs===cleanupReserveMs&&job.finishedEpochMs<=deadlineEpochMs&&
  job.rootExitCode===expectedExit&&job.exitCode===expectedExit&&job.activeProcessesFinal===0&&job.zeroObservedBeforeDispose===true&&
  job.cleanupError===null&&Number.isSafeInteger(job.jobQueryCount)&&job.jobQueryCount>=2,'NONUSER_SPLIT_JOB_TERMINAL:'+phase);
 if(phase==='compiler'){
  need(job.activeProcessesBeforeCleanup===(job.terminationRequested?1:0),'NONUSER_SPLIT_COMPILER_ACCOUNTING');
  return verifyOwnedJobReceipt(job,tool).kind;
 }
 need(phase==='business'&&job.activeProcessesBeforeCleanup===0&&job.terminationRequested===false&&job.terminationSucceeded===false,'NONUSER_SPLIT_BUSINESS_NOT_NATURAL');
 return 'NATURAL';
}

export function createOwnedPhases({root,tool,execute}){
 const dir=path.join(root,'owned-phases'),budget=read(path.join(root,'owned-phase-budget.json'));
 need(budget.schemaVersion===1&&Number.isSafeInteger(budget.deadlineEpochMs)&&budget.cleanupReserveMs===10000,'NONUSER_SPLIT_PARENT_BUDGET');
 const guard=path.join(root,'input-sources/tools/ymcc_sim/global/host-process.ps1'),records=[];
 fs.mkdirSync(dir);
 function run(label,phase,program,args,{cwd,env,expectedExit=0}){
  const cleanupReserveMs=1000,deadlineEpochMs=Math.min(budget.deadlineEpochMs-budget.cleanupReserveMs-500,Date.now()+75000);
  need(Date.now()<deadlineEpochMs-cleanupReserveMs,'NONUSER_SPLIT_NO_REMAINING_BUDGET');
  const argsPath=path.join(dir,label+'.args.json'),receiptPath=path.join(dir,label+'.receipt.json');
  writeNew(argsPath,args);
  const p=execute(label,tool.powershellExecutable,['-NoLogo','-NoProfile','-File',guard,'-ProgramPath',program,'-ArgumentsPath',argsPath,'-DeadlineEpochMs',String(deadlineEpochMs),'-CleanupReserveMs',String(cleanupReserveMs),'-DeadlineReceiptPath',receiptPath],{cwd,env,timeout:deadlineEpochMs-Date.now()+100});
  need(p.exitCode===expectedExit,'NONUSER_SPLIT_PROCESS_EXIT:'+label+':'+p.exitCode);
  const job=read(receiptPath),kind=inspect(job,tool,phase,expectedExit,deadlineEpochMs,cleanupReserveMs);
  const announced=fs.readFileSync(p.stdout,'utf8').match(/^YMCC_SIM_OWNED_JOB childPid=(\d+)\r?$/m);
  need(announced&&Number(announced[1])===job.childPid,'NONUSER_SPLIT_CHILD_RECEIPT_BINDING:'+label);
  const recipe=phase==='compiler'?{path:args[2],sha256:hash(args[2])}:null;
  const record={label,phase,kind,program,programSha256:hash(program),args:argsPath,argsSha256:hash(argsPath),recipe,expectedExit,deadlineEpochMs,cleanupReserveMs,receipt:receiptPath,sha256:hash(receiptPath),stdout:p.stdout,stdoutSha256:p.stdoutSha256};
  records.push(record);writeNew(path.join(dir,label+'.qualified.json'),record);
  return p;
 }
 return {run,records};
}

export function verifyOwnedPhases(root,tool,spec,records,parentDeadlineEpochMs,parentCleanupReserveMs){
 const labels=['os-probe-compile',...['power','process','thread','snapshot','network','resume'].map(name=>'os-refusal-'+name),'positive-compile','positive-native-0','positive-native-1','positive-native-2','negative-budget-compile','negative-budget-native-0','negative-suspend-compile','negative-suspend-native-0'];
 need(Array.isArray(records)&&JSON.stringify(records.map(row=>row.label))===JSON.stringify(labels),'NONUSER_SPLIT_PHASE_SET');
 const seen=new Set();
 for(const [i,row] of records.entries()){
  const phase=row.label.endsWith('-compile')?'compiler':'business';
  const expectedExit=i>=1&&i<=6?91:row.label.startsWith('negative-')&&phase==='business'?3:0;
  need(row.phase===phase&&row.expectedExit===expectedExit&&Number.isSafeInteger(row.deadlineEpochMs)&&row.deadlineEpochMs<=parentDeadlineEpochMs-parentCleanupReserveMs&&row.cleanupReserveMs===1000,'NONUSER_SPLIT_PHASE_SCOPE:'+row.label);
  for(const [file,digest] of [[row.receipt,row.sha256],[row.args,row.argsSha256],[row.stdout,row.stdoutSha256]])need(inside(root,file)&&hash(file)===digest,'NONUSER_SPLIT_RECEIPT_CHANGED:'+row.label);
  const os=row.label.startsWith('os-'),arm=os?null:row.label.replace(/-(?:compile|native-\d+)$/,''),out=os?path.join(root,'os-port-probe'):path.join(root,arm,'original-output');
  const args=read(row.args),command=phase==='compiler'?tool.cmdExecutable:path.join(out,os?'port-probe.exe':'sleep_non_user_wake_native_selftest.exe');
  need(samePath(row.program,command)&&hash(row.program)===row.programSha256,'NONUSER_SPLIT_PROGRAM_CHANGED:'+row.label);
  if(phase==='compiler'){
   const recipe=path.join(out,os?'compile.cmd':'compile-owned.cmd');
   need(row.recipe&&samePath(row.recipe.path,recipe)&&hash(recipe)===row.recipe.sha256&&Array.isArray(args)&&args.length===3&&args[0]==='/d'&&args[1]==='/c'&&samePath(args[2],recipe),'NONUSER_SPLIT_COMPILE_RECIPE_CHANGED:'+row.label);
  }else{
   const expected=os?[row.label.slice('os-refusal-'.length)]:row.label==='positive-native-2'?[...spec.originalFixtures,...spec.additionalFixtures].map(x=>path.join(root,arm,'source-repo',x.path)):[row.label==='positive-native-1'?'--caps-unknown':arm==='negative-suspend'?'--suspend-admission-only':'--s3'];
   need(row.recipe===null&&JSON.stringify(args)===JSON.stringify(expected),'NONUSER_SPLIT_BUSINESS_ARGS_CHANGED:'+row.label);
  }
  const job=read(row.receipt),key=job.childPid+':'+job.finishedEpochMs;
  const markers=[...fs.readFileSync(row.stdout,'utf8').matchAll(/^YMCC_SIM_OWNED_JOB childPid=(\d+)\r?$/gm)];
  need(markers.length===1&&Number(markers[0][1])===job.childPid,'NONUSER_SPLIT_JOB_MARKER_CHANGED:'+row.label);
  need(!seen.has(key)&&inspect(job,tool,phase,expectedExit,row.deadlineEpochMs,row.cleanupReserveMs)===row.kind,'NONUSER_SPLIT_TERMINAL_CHANGED:'+row.label);
  seen.add(key);
 }
}

export function runNonUserWake(value,{prototype=false,resumeClassificationOnly=false,resumeClassificationEdgeOnly=false,resumeClassificationLifetimeOnly=false,resumeClassificationPublicationOnly=false,resumeClassificationSenderOnly=false,resumeSenderCase=null,resumeSenderBuildReference=null}={}){
 need([prototype,resumeClassificationOnly,resumeClassificationEdgeOnly,resumeClassificationLifetimeOnly,resumeClassificationPublicationOnly,resumeClassificationSenderOnly].filter(Boolean).length<=1,'NATIVE_RECOVERY_ARGUMENTS');
 need(resumeSenderCase===null||resumeClassificationSenderOnly,'NATIVE_RECOVERY_ARGUMENTS');
 need(resumeSenderBuildReference===null||resumeClassificationSenderOnly&&typeof resumeSenderBuildReference==='string','NATIVE_RECOVERY_ARGUMENTS');
 const executionStarted=performance.now(),executionPhases=[];let executionLast=executionStarted;
 const markExecutionPhase=phase=>{const end=performance.now();executionPhases.push({phase,startOffsetMs:executionLast-executionStarted,elapsedMs:end-executionLast});executionLast=end;};
 const senderSelection=resumeClassificationSenderOnly?getJoinedSenderSelection(REPO,resumeSenderCase):null;
 const buildReferenceAuthority=resumeSenderBuildReference===null?null:pinJoinedBuildReference(resumeSenderBuildReference);
 const root=evidencePath(value),spec=read(path.join(REPO,MODULE,'spec.json')),tool=toolchainSpec(REPO);
 markExecutionPhase('metadata_and_tool_selection');
 need(path.resolve(process.execPath).toLowerCase()===path.resolve(tool.nodeExecutable).toLowerCase(),'NATIVE_RECOVERY_NODE_NOT_PINNED');
 const publicWakeReview=verifyPublicWakeSource(REPO,spec),sourceSuccessor=publicWakeReview.source;
 for(const row of [spec.originalScript])need(sourceSuccessor.directHash(row.path)===row.sha256,'NATIVE_RECOVERY_ORIGINAL_SCRIPT_NEEDS_REVIEW');
 for(const row of [...spec.originalFixtures,...spec.additionalFixtures])need(hash(path.join(REPO,row.path))===row.sha256,'NONUSER_IMMUTABLE_REPLAY_CHANGED:'+row.path);
 const sharedLaunchReview=publicWakeReview.launch();
 for(const row of spec.reviewedSharedLeafIdentities)need(sourceSuccessor.directHash(row.path)===row.sha256,'NONUSER_REVIEWED_SHARED_LEAF_CHANGED:'+row.path);
 for(const row of tool.files)need(hash(row.path)===row.sha256,'NATIVE_RECOVERY_TOOL_CHANGED:'+row.path);
 const sourceCompatibilityReview={...sourceSuccessor.verifyOriginalSourceReview(verifyCurrentSleepSourceReview),publicWakeReview:publicWakeReview.receipt,sharedLaunchReview:sharedLaunchReview.receipt};
 markExecutionPhase('source_and_tool_preflight');
 fs.mkdirSync(root,{recursive:true});const snapshot=captureInputs(root,spec,tool,resumeClassificationSenderOnly?'resume-classification-sender-only':resumeClassificationPublicationOnly?'resume-classification-publication-only':resumeClassificationLifetimeOnly?'resume-classification-lifetime-only':resumeClassificationEdgeOnly?'resume-classification-edge-only':resumeClassificationOnly?'resume-classification-only':'legacy',resumeSenderCase,buildReferenceAuthority);if(buildReferenceAuthority)verifyJoinedBuildReference(buildReferenceAuthority,{snapshot});markExecutionPhase('capture_input_snapshot');
 publicWakeReview.verifyCaptured?.(snapshot.sources);
 const record=allocateScratch(root);write(path.join(root,'scratch-allocation.json'),record);markExecutionPhase('allocate_owned_scratch');
 const scratch=prepareExecution(record,root,snapshot);markExecutionPhase('copy_execution_sources');
 const frozen=path.join(scratch,'input-sources',MODULE),args=path.join(root,'job-arguments.json');
 write(args,['-Node',tool.nodeExecutable,'-Worker',path.join(frozen,'worker.mjs'),'-Root',scratch]);const env={...process.env,PATHEXT:'.COM;.EXE;.BAT;.CMD'};for(const key of Object.keys(env))if(/^VSLANG$/i.test(key))delete env[key];Object.assign(env,tool.compilerDiagnosticEnvironment);delete env.NODE_OPTIONS;delete env.NODE_PATH;
 markExecutionPhase('prepare_owned_worker_arguments');
 const jobDeadlineEpochMs=Date.now()+220000,jobCleanupReserveMs=10000,jobReceiptPath=path.join(root,'job-receipt.json');
 if(snapshot.selection==='legacy')write(path.join(scratch,'owned-phase-budget.json'),{schemaVersion:1,deadlineEpochMs:jobDeadlineEpochMs,cleanupReserveMs:jobCleanupReserveMs});
 const job=spawnSync(tool.powershellExecutable,['-NoLogo','-NoProfile','-File',path.join(scratch,'input-sources/tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(scratch,'input-sources/tools/ymcc_sim/global/sleep-native-recovery-consumer/worker.ps1'),'-ArgumentsPath',args,'-DeadlineEpochMs',String(jobDeadlineEpochMs),'-CleanupReserveMs',String(jobCleanupReserveMs),'-DeadlineReceiptPath',jobReceiptPath],{cwd:scratch,env,windowsHide:true,timeout:220000,maxBuffer:4*1024*1024});
 markExecutionPhase('owned_worker_guard');
 fs.writeFileSync(path.join(root,'job.stdout.log'),job.stdout??'');fs.writeFileSync(path.join(root,'job.stderr.log'),job.stderr??'');
 const workerFile=path.join(scratch,'worker-result.json'),worker=fs.existsSync(workerFile)?read(workerFile):{status:'FAIL',error:'WORKER_RESULT_MISSING'};
 const sourceDrift=snapshot.sources.filter(row=>hash(path.join(REPO,row.path))!==row.sha256),toolDrift=tool.files.filter(row=>hash(row.path)!==row.sha256);
 markExecutionPhase('read_worker_and_recheck_current_identity');
 let validationError=null;try{verifyScratch(record,root);for(const row of snapshot.sources){need(hash(row.saved)===row.sha256,'SOURCE_SNAPSHOT_MUTATED');need(hash(path.join(scratch,'input-sources',row.path))===row.sha256,'EXECUTION_SOURCE_SNAPSHOT_MUTATED');}verifyExecutedArtifacts(scratch,worker,snapshot);if(snapshot.selection==='legacy'&&worker.status==='PASS')verifyOwnedPhases(scratch,tool,spec,worker.facts.ownedPhases,jobDeadlineEpochMs,jobCleanupReserveMs);}catch(error){validationError=error.message;}
 markExecutionPhase('verify_executed_sources_and_artifacts');
 let jobReceipt=null,jobReceiptError=null;
 try{if(fs.existsSync(jobReceiptPath))jobReceipt=read(jobReceiptPath);}catch(error){jobReceiptError=error.message;}
 const ownedJob=inspectNonUserWakeOwnedJob(job,jobReceipt,{deadlineEpochMs:jobDeadlineEpochMs,cleanupReserveMs:jobCleanupReserveMs});
 if(jobReceiptError)ownedJob.receiptError=jobReceiptError;
 if(fs.existsSync(jobReceiptPath))ownedJob.receipt={path:jobReceiptPath,sha256:hash(jobReceiptPath)};
 write(path.join(root,'job-receipt-observed.json'),ownedJob);
 markExecutionPhase('consume_structured_job_receipt');
 // Archive available evidence even on failure. Scratch deletion requires the
 // actual assigned child's exit and Job zero before disposal, not root stdout.
 const mapping=ownedJob.resourcesReturned?archiveScratch(record,root):{schemaVersion:1,status:'NOT_ARCHIVED_RESOURCE_RETURN_UNPROVEN',originalRoot:scratch,files:[],originalContentsRewritten:false};write(path.join(root,'execution-archive-mapping.json'),mapping);markExecutionPhase('archive_owned_execution');
 const naturalJob=ownedJob.naturalExit;
 let cleanup={root:scratch,markerSha256:record.markerSha256,cleaned:false,retainedForFailure:true};
 if(naturalJob&&worker.status==='PASS'&&!sourceDrift.length&&!toolDrift.length&&!validationError)cleanup=cleanupScratch(record,root,mapping);
 write(path.join(root,'scratch-cleanup.json'),cleanup);
 markExecutionPhase('cleanup_owned_scratch');
 const completed=naturalJob&&worker.status==='PASS'&&!sourceDrift.length&&!toolDrift.length&&!validationError&&cleanup.cleaned&&!prototype;
 const result={schemaVersion:1,id:CASE_ID,legacyParentExecuted:!(resumeClassificationOnly||resumeClassificationEdgeOnly||resumeClassificationLifetimeOnly||resumeClassificationPublicationOnly||resumeClassificationSenderOnly),resumeSenderJoin:resumeClassificationSenderOnly?worker.facts?.['resume-sender']?.resumeSenderJoin??null:null,resumeClassification:(resumeClassificationOnly||resumeClassificationEdgeOnly||resumeClassificationLifetimeOnly||resumeClassificationPublicationOnly)?worker.facts?.['resume-classification']?.resumeClassification??null:null,sourceCompatibilityReview,status:completed?'PASS':'FAIL',passed:completed,prototypeOnly:prototype,prototypeWorkerPassed:worker.status==='PASS',workerError:worker.error??validationError??null,validationError,originalSourceOrCompileFailurePreserved:worker.status!=='PASS',compileFailureCountsAsNegative:false,sourceHashes:[...snapshot.sources.map(({path,sha256})=>({path,sha256})),...walk(root).map(file=>({path:file,sha256:hash(file)}))],sourceChanges:sourceDrift.map(row=>row.path),toolChanges:toolDrift.map(row=>row.path),executionArchive:{mapping:path.join(root,'execution-archive-mapping.json'),mappingSha256:hash(path.join(root,'execution-archive-mapping.json')),originalRoot:scratch,rawWorkerResult:ownedJob.resourcesReturned?path.join(root,'execution/worker-result.json'):null,archiveFiles:mapping.files.length,archivePerformed:ownedJob.resourcesReturned,originalContentsRewritten:false,cleaned:cleanup.cleaned},executionSelection:snapshot.selection,actualOriginalConsumers:['tools/sleep_non_user_wake_selftest.py'],inputConsumerStateFeedback:{input:`Original clock/timer/capability/PID memory terminals plus${[...spec.originalFixtures,...spec.additionalFixtures].reduce((n,row)=>n+row.events,0)} fixed JSON events`,consumer:`Actual${spec.ioReview.actualFunctions} current production non-user wake/retry functions and handlers with pure budget and fan state`,state:'Original120-second refusal,200-cycle cumulative budget,S3/capability-unknown gate and confirmed-user cancellation assertions',feedback:`Original${spec.expectedProfiles.s3} S3 groups,${spec.expectedProfiles.capsUnknown} unknown groups,current${spec.expectedProfiles.main} main/replay groups,original budget assertion failure and six external-port refusals`},boundary:{ownedWindowsJob:jobReceipt?.jobAssigned===true,reviewedScriptsUnchangedDuringRun:true,suspendAdmissionRegression:true,suspendAdmissionFaultRequired:true,nonUserWakeFixtureReviewedForCurrentSource:true,originalNonUserWakeAssertionsPreserved:true,immutableReplayFixtures:spec.originalFixtures.length,additionalPinnedReplayFixtures:spec.additionalFixtures.length,shortExecutionRootExactCaseOwned:true,allOutputsArchivedWithoutRewriting:ownedJob.resourcesReturned,systemSleepRequest:'counted original fake, real SetSuspendState refuses',realSleep:false,realProcessResume:false,realSteam:false,realHost:false,realDevices:false,installedApplication:false,fullProductAcceptance:false},remainingGaps:[...(worker.status==='PASS'?[]:[worker.error]),...(validationError?[validationError]:[]),...(sourceDrift.length?['Live sources changed; full source snapshot is not current acceptance']:[]),'OS S0/S3/S4,actual USB4 recovery,live process/game,Host/Steam and physical devices are not accepted'],goalComplete:false};
 if(resumeClassificationOnly||resumeClassificationEdgeOnly||resumeClassificationLifetimeOnly||resumeClassificationPublicationOnly){result.inputConsumerStateFeedback={input:resumeClassificationPublicationOnly?'One original softwareGate case; no lifetime, edge or legacy cases executed':'Exact field chronology, scalar clock/timer/PID leaves; no legacy JSON replays executed',consumer:resumeClassificationPublicationOnly?'Actual shared power publisher and mirror revocation through the original softwareGate':'Actual common/Begin/three wake-worker arms/sgRealWake/classifier/independent timeout',state:resumeClassificationPublicationOnly?'Only pure-software-classification-gate-and-same-generation-final-notice (1 original softwareGate)':resumeClassificationLifetimeOnly?'Only four critical/missing-Kernel/settings lifecycle controls':resumeClassificationEdgeOnly?'Only two late506 after classified507 cases':'33 S0 plus1 classic and1 unknown-capability scope',feedback:resumeClassificationPublicationOnly?'One RESUME_CLASSIFICATION case and one original native START/NATURAL_RETURN pair':'RESUME_CLASSIFICATION original assertion terminals and original native OS boundary'};result.boundary.suspendAdmissionRegression=false;result.boundary.originalLegacyParentExecuted=false;result.remainingGaps.unshift('Original legacy parent was not selected; earlier parent failures and source epochs remain');}
 if(resumeClassificationSenderOnly){result.inputConsumerStateFeedback={input:'Exact original joined sender selectors: '+senderSelection.selectors.join(', '),consumer:'Actual joined sender and shared Reader; counted privilege/caps/suspend/publisher/mirror terminals',state:'Only '+senderSelection.selectors.length+' selected original joined sender cases; no legacy or resume-classification cases executed',feedback:'RESUME_SENDER_JOIN original case bindings and assertion terminals; no full OS or actual projection acceptance'};result.boundary.suspendAdmissionRegression=false;result.boundary.suspendAdmissionFaultRequired=false;result.boundary.originalLegacyParentExecuted=false;result.remainingGaps.unshift('Original legacy parent and resume-classification scope were not selected; earlier failures and source epochs remain','Joined sender uses counted privilege/caps/suspend/publisher/mirror terminals; full OS and actual projection are not accepted');}
 result.ownedJob=ownedJob;
 if(!naturalJob){result.remainingGaps.unshift(...ownedJob.reasons);result.workerError??=ownedJob.reasons.join('; ');}
 markExecutionPhase('assemble_result_and_hash_evidence');
 result.executionTimings={schemaVersion:1,clock:'performance.now',scope:'Original runner after argument validation through immediately before result serialization; outer launcher/module import and final result write/stdout excluded. Worker and child phases overlap this parent and must not be added again.',elapsedMs:executionLast-executionStarted,phases:executionPhases,observationalOnly:true};
 write(path.join(root,'result.json'),result);console.log(JSON.stringify({id:CASE_ID,status:result.status,prototypeOnly:prototype,workerStatus:worker.status,error:result.workerError,sourceDrift:sourceDrift.map(row=>row.path),nestedRows:result.sourceHashes.length,archived:mapping.files.length,cleaned:cleanup.cleaned,root}));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{
 const args=process.argv.slice(2),selector=args[2],prototype=selector==='--prototype',resumeClassificationOnly=selector==='--resume-classification-only',resumeClassificationEdgeOnly=selector==='--resume-classification-edge-only',resumeClassificationLifetimeOnly=selector==='--resume-classification-lifetime-only',resumeClassificationPublicationOnly=selector==='--resume-classification-publication-only',resumeClassificationSenderOnly=selector==='--resume-classification-sender-only';
 const hasSelector=prototype||resumeClassificationOnly||resumeClassificationEdgeOnly||resumeClassificationLifetimeOnly||resumeClassificationPublicationOnly||resumeClassificationSenderOnly;
 need(args[0]==='--evidence-dir'&&typeof args[1]==='string'&&args[1].length>0&&!args[1].startsWith('--')&&(args.length===2||hasSelector&&(args.length===3||resumeClassificationSenderOnly&&[5,7].includes(args.length))),'NATIVE_RECOVERY_ARGUMENTS');
 let resumeSenderCase=null,resumeSenderBuildReference=null;const flags=new Set();
 for(let index=3;index<args.length;index+=2){const flag=args[index],value=args[index+1];need(['--resume-sender-case','--resume-sender-build-reference'].includes(flag)&&!flags.has(flag)&&typeof value==='string'&&value.length>0&&!value.startsWith('--'),'NATIVE_RECOVERY_ARGUMENTS');flags.add(flag);if(flag==='--resume-sender-case')resumeSenderCase=value;else resumeSenderBuildReference=value;}
 process.exitCode=runNonUserWake(args[1],{prototype,resumeClassificationOnly,resumeClassificationEdgeOnly,resumeClassificationLifetimeOnly,resumeClassificationPublicationOnly,resumeClassificationSenderOnly,resumeSenderCase,resumeSenderBuildReference}).status==='PASS'?0:2;
}catch(error){console.error(error.stack);process.exitCode=2;}}
