import {verifyCurrentSleepSourceReview} from '../sleep-non-user-wake-consumer/compiler-leaf.mjs';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {REPO,MODULE,CASE_ID,inputFiles,toolchainSpec} from './adapter.mjs';
import {allocateScratch,verifyScratch,archiveScratch,cleanupScratch} from '../sleep-native-recovery-consumer/scratch.mjs';
import {evidencePath} from '../sleep-native-recovery-consumer/run.mjs';
export {evidencePath};
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const need=(ok,code)=>{if(!ok)throw Error(code);};
const inside=(base,file)=>{const relative=path.relative(path.resolve(base),path.resolve(file));return !path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};

function walk(root){const files=[];function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){const file=path.join(dir,entry.name);need(!entry.isSymbolicLink(),'GAME_PAUSE_OUTPUT_REPARSE');if(entry.isDirectory())visit(file);else{need(entry.isFile(),'GAME_PAUSE_OUTPUT_TYPE');files.push(file);}need(files.length<12000,'GAME_PAUSE_OUTPUT_LIMIT');}}visit(root);return files.sort();}
function captureInputs(root,spec,tool){
 const sources=inputFiles(REPO).sort().map(relative=>{const original=path.join(REPO,relative),bytes=fs.readFileSync(original),saved=path.join(root,'input-sources',relative);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.writeFileSync(saved,bytes,{flag:'wx'});return{path:relative,sha256:createHash('sha256').update(bytes).digest('hex'),saved};});
 const snapshot={schemaVersion:1,module:MODULE,createdAt:new Date().toISOString(),sources,tool};write(path.join(root,'input-snapshot.json'),snapshot);return snapshot;
}
function prepareExecution(record,evidence,snapshot){
 const scratch=verifyScratch(record,evidence),sources=snapshot.sources.map(row=>{const saved=path.join(scratch,'input-sources',row.path);fs.mkdirSync(path.dirname(saved),{recursive:true});fs.copyFileSync(row.saved,saved,fs.constants.COPYFILE_EXCL);need(hash(saved)===row.sha256,'EXECUTION_SOURCE_COPY_CHANGED');return{...row,saved};});
 write(path.join(scratch,'input-snapshot.json'),{...snapshot,sources});write(path.join(scratch,'execution-owner.json'),record);return scratch;
}
function verifyExecutedArtifacts(scratch,worker){
 const roots=[worker.facts?.osBoundary?.compilerInputs,worker.facts?.positive?.actualCompilerInputs,worker.facts?.['negative-identity']?.actualCompilerInputs].filter(Boolean);
 for(const ledger of roots){need(inside(scratch,ledger.path)&&hash(ledger.path)===ledger.sha256,'EVALUATED_LEDGER_CHANGED');for(const row of read(ledger.path).rows)need(hash(row.path)===row.sha256,'EXECUTED_COMPILER_OR_LINK_INPUT_CHANGED:'+row.path);}
 for(const label of ['positive','negative-identity']){const fact=worker.facts?.[label];if(!fact)continue;for(const row of Object.values(fact.produced??{}))need(inside(scratch,row.path)&&hash(row.path)===row.sha256,'EXECUTED_NATIVE_ARTIFACT_CHANGED');for(const row of fact.pythonAudits??[])need(inside(scratch,row.path)&&hash(row.path)===row.sha256,'EXECUTED_PYTHON_AUDIT_CHANGED');}
}
export function runGamePause(value,{prototype=false}={}){
 const root=evidencePath(value),spec=read(path.join(REPO,MODULE,'spec.json')),tool=toolchainSpec(REPO);
 need(path.resolve(process.execPath).toLowerCase()===path.resolve(tool.nodeExecutable).toLowerCase(),'GAME_PAUSE_NODE_NOT_PINNED');
 for(const row of [spec.originalScript])need(hash(path.join(REPO,row.path))===row.sha256,'GAME_PAUSE_ORIGINAL_SCRIPT_NEEDS_REVIEW');
 for(const row of (spec.originalFixtures??[]))need(hash(path.join(REPO,row.path))===row.sha256,'GAME_PAUSE_IMMUTABLE_REPLAY_CHANGED:'+row.path);
 for(const row of spec.reviewedSharedLeafIdentities)need(hash(path.join(REPO,row.path))===row.sha256,'GAME_PAUSE_REVIEWED_SHARED_LEAF_CHANGED:'+row.path);
 for(const row of tool.files)need(hash(row.path)===row.sha256,'GAME_PAUSE_TOOL_CHANGED:'+row.path);
 const sourceCompatibilityReview=verifyCurrentSleepSourceReview(REPO);
 fs.mkdirSync(root,{recursive:true});const snapshot=captureInputs(root,spec,tool),record=allocateScratch(root);write(path.join(root,'scratch-allocation.json'),record);const scratch=prepareExecution(record,root,snapshot),frozen=path.join(scratch,'input-sources',MODULE),args=path.join(root,'job-arguments.json');
 write(args,['-Node',tool.nodeExecutable,'-Worker',path.join(frozen,'worker.mjs'),'-Root',scratch]);const env={...process.env};delete env.NODE_OPTIONS;delete env.NODE_PATH;
 const job=spawnSync(tool.powershellExecutable,['-NoLogo','-NoProfile','-File',path.join(scratch,'input-sources/tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',path.join(scratch,'input-sources/tools/ymcc_sim/global/sleep-native-recovery-consumer/worker.ps1'),'-ArgumentsPath',args],{cwd:scratch,env,windowsHide:true,timeout:220000,maxBuffer:4*1024*1024});
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
 const result={schemaVersion:1,id:CASE_ID,sourceCompatibilityReview,status:completed?'PASS':'FAIL',passed:completed,prototypeOnly:prototype,prototypeWorkerPassed:worker.status==='PASS',workerError:worker.error??validationError??null,validationError,originalSourceOrCompileFailurePreserved:worker.status!=='PASS',compileFailureCountsAsNegative:false,sourceHashes:[...snapshot.sources.map(({path,sha256})=>({path,sha256})),...walk(root).map(file=>({path:file,sha256:hash(file)}))],sourceChanges:sourceDrift.map(row=>row.path),toolChanges:toolDrift.map(row=>row.path),executionArchive:{mapping:path.join(root,'execution-archive-mapping.json'),mappingSha256:hash(path.join(root,'execution-archive-mapping.json')),originalRoot:scratch,rawWorkerResult:path.join(root,'execution/worker-result.json'),archiveFiles:mapping.files.length,originalContentsRewritten:false,cleaned:cleanup.cleaned},actualOriginalConsumers:['tools/sleep_game_pause_selftest.py'],inputConsumerStateFeedback:{input:'Original clock/capability/PID memory terminals and one owned joined cancellation thread',consumer:'13 actual singlePID pause/resume/cancel and checked-marker functions, production worker gate,1 pure main helper and2 pure header helpers',state:'Original PID birth/manual lease/generation/cancellation race and independent input policy assertions',feedback:'Original singlePID terminal and opened-handle identity fault with own thread join,actual import and external-port diagnostics'},boundary:{ownedWindowsJob:true,reviewedScriptsUnchangedDuringRun:true,gamePauseFixtureReviewedForCurrentSource:true,originalPidIdentityAssertionsPreserved:true,ownJoinedFixtureThreadAllowed:true,shortExecutionRootExactCaseOwned:true,allOutputsArchivedWithoutRewriting:true,systemSleepRequest:'not requested by this fixture; unexpected SetSuspendState is refused',realSleep:false,realProcessResume:false,realSteam:false,realHost:false,realDevices:false,installedApplication:false,fullProductAcceptance:false},remainingGaps:[...(worker.status==='PASS'?[]:[worker.error]),...(validationError?[validationError]:[]),...(sourceDrift.length?['Live sources changed; full source snapshot is not current acceptance']:[]),'OS S0/S3/S4,actual USB4 recovery,live process/game,Host/Steam and physical devices are not accepted'],goalComplete:false};
 write(path.join(root,'result.json'),result);console.log(JSON.stringify({id:CASE_ID,status:result.status,prototypeOnly:prototype,workerStatus:worker.status,error:result.workerError,sourceDrift:sourceDrift.map(row=>row.path),nestedRows:result.sourceHashes.length,archived:mapping.files.length,cleaned:cleanup.cleaned,root}));return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){try{const args=process.argv.slice(2),prototype=args.includes('--prototype');need(args[0]==='--evidence-dir'&&typeof args[1]==='string'&&args.length===(prototype?3:2),'GAME_PAUSE_ARGUMENTS');process.exitCode=runGamePause(args[1],{prototype}).status==='PASS'?0:2;}catch(error){console.error(error.stack);process.exitCode=2;}}
