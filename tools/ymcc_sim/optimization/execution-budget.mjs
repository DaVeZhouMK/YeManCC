/** One wall budget around the original optimizer, including synchronous setup. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {performance} from 'node:perf_hooks';
import {fileURLToPath} from 'node:url';
import {runProcess,makeRunDirectory} from '../global/runner.mjs';
import {simulationCorePath} from '../global/release-host-environment.mjs';
export const MAX_WALL_BUDGET_MS=1_800_000;
const inside=(root,file)=>{const r=path.relative(root,file);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
// Raw bytes, one fixed buffer, and the original monotonic deadline in both phases.
const scanSourceIdentity=(files,deadline,expected)=>{
 const started=performance.now(),pins=[],drift=[],readErrors=[],buffer=Buffer.allocUnsafe(64*1024);
 let attempted=0,completed=0,bytesRead=0,blocksRead=0,timedOut=false;
 const expired=()=>timedOut||=(performance.now()>=deadline);
 for(let index=0;index<files.length;index++){
  if(expired())break;
  const file=files[index];let fd;
  attempted++;
  try{
   fd=fs.openSync(file,'r');if(expired())break;
   const hash=crypto.createHash('sha256');
   for(;;){
    if(expired())break;
    const size=fs.readSync(fd,buffer,0,buffer.length,null);bytesRead+=size;blocksRead++;
    if(expired())break;
    if(size===0){
     const actual=hash.digest('hex');if(expired())break;
     pins.push({path:file,sha256:actual});completed++;
     if(expected&&expected[index].sha256!==actual)drift.push({...expected[index],actual});
     break;
    }
    hash.update(buffer.subarray(0,size));
   }
  }catch(error){
   if(expired())break;
   if(!expected)throw error;
   readErrors.push({path:file,code:error.code??'SOURCE_READ_FAILED'});
  }finally{if(fd!==undefined)fs.closeSync(fd);}
  if(expired())break;
 }
 return {pins,drift,readErrors,metadata:{requested:files.length,attempted,completed,bytesRead,blocksRead,chunkBytes:buffer.length,wallMs:performance.now()-started,timedOut,complete:!timedOut&&completed===files.length}};
};
const nonPassStatus=/^(FAIL|BLOCKED|INCOMPLETE|NOT_RUN|TIMEOUT)(?:_|$)/;
const object=value=>value!==null&&typeof value==='object'&&!Array.isArray(value);
// Validate only the worker envelope; behavioral assertions remain with the original adapters.
const validWorker=(value,runDir)=>{
 if(!object(value)||value.schemaVersion!==1||typeof value.status!=='string'||(value.runDir!==undefined&&value.runDir!==runDir))return false;
 if(value.status==='EXPLICIT_FULL_DELEGATED')return Number.isInteger(value.exitCode);
 if(!['PASS_SELECTED_SCOPE','COMPLETE_WITH_CACHED_REFERENCES','NOT_RUN_NO_RELEASE_BUGS'].includes(value.status)&&!nonPassStatus.test(value.status))return false;
 const s=value.summary;if(!object(s)||['selected','executedThisRun','actualPassedThisRun'].some(key=>!Number.isSafeInteger(s[key])||s[key]<0)||s.actualPassedThisRun>s.executedThisRun||s.executedThisRun>s.selected)return false;
 if(value.status==='NOT_RUN_NO_RELEASE_BUGS')return value.noTestsExecuted===true&&s.selected===0&&s.executedThisRun===0&&s.actualPassedThisRun===0;
 if(value.status==='PASS_SELECTED_SCOPE')return s.selected>0&&s.actualPassedThisRun===s.selected;
 if(value.status==='COMPLETE_WITH_CACHED_REFERENCES')return Number.isSafeInteger(s.cachedReferences)&&s.cachedReferences>0&&s.executedThisRun===s.actualPassedThisRun&&s.actualPassedThisRun+s.cachedReferences===s.selected;
 return true;
};
const validPlan=(value,args)=>{
 const modeAt=args.lastIndexOf('--mode'),requestedMode=modeAt<0?'plan':args[modeAt+1];
 return requestedMode==='plan'&&object(value)&&value.schemaVersion===1&&(value.mode===undefined||value.mode==='plan')&&((value.status==='PLAN_ONLY'&&(value.executedThisRun===undefined||value.executedThisRun===0))||(value.status===undefined&&value.noTestsExecuted===true&&Array.isArray(value.tests)));
};
export function validateWallBudget(value){
 if(!Number.isSafeInteger(value)||value<1000||value>MAX_WALL_BUDGET_MS)throw Error('WALL_BUDGET_REQUIRES_1000_TO_1800000_MS');
 return value;
}
export function readBudgetWorker(file,nonce,{coreRoot,repoRoot}){
 const actual=fs.realpathSync(file);if(!inside(fs.realpathSync(coreRoot),actual))throw Error('BUDGET_CONTEXT_OUTSIDE_CORE');
 const context=read(actual);
 if(context.schemaVersion!==1||context.kind!=='ymcc-fixture-wall-budget'||context.nonce!==nonce||context.repoRoot!==repoRoot||path.dirname(actual)!==context.runDir||!Array.isArray(context.args)||context.args.some(a=>typeof a!=='string'))throw Error('BUDGET_CONTEXT_INVALID');
 validateWallBudget(context.budgetMs);
 if(!Number.isSafeInteger(context.deadlineEpochMs)||!Number.isSafeInteger(context.cleanupReserveMs)||context.cleanupReserveMs<=0||context.cleanupReserveMs>=context.budgetMs)throw Error('BUDGET_DEADLINE_INVALID');
 const remaining=Math.max(0,context.deadlineEpochMs-Date.now()-context.cleanupReserveMs),stop=performance.now()+remaining;
 return {...context,remainingMs:()=>Math.max(0,Math.floor(stop-performance.now()))};
}
export async function runWithWallBudget(args,{repoRoot,coreRoot,workerEntry,budgetMs=MAX_WALL_BUDGET_MS,powershell='pwsh.exe',outDir,command,additionalInputs=[]}={}){
 const started=performance.now();validateWallBudget(budgetMs);const deadline=started+budgetMs;
 const parent=path.resolve(outDir??path.join(coreRoot,'Evidence/Build/optimized'));
 if(!inside(coreRoot,parent))throw Error('OPTIMIZATION_EVIDENCE_MUST_STAY_IN_CORE');
 let cursor=parent;while(!fs.existsSync(cursor))cursor=path.dirname(cursor);
 if(!inside(fs.realpathSync(coreRoot),fs.realpathSync(cursor)))throw Error('OPTIMIZATION_EVIDENCE_ESCAPE');
 const {runDir}=makeRunDirectory(parent),cleanupReserveMs=Math.min(120000,Math.max(250,Math.floor(budgetMs/10)));
 const context={schemaVersion:1,kind:'ymcc-fixture-wall-budget',nonce:crypto.randomBytes(24).toString('hex'),repoRoot,runDir,args,budgetMs,cleanupReserveMs,deadlineEpochMs:Date.now()+Math.max(1,Math.floor(budgetMs-(performance.now()-started)))};
 const contextFile=path.join(runDir,'budget-context.json'),argumentsFile=path.join(runDir,'budget-arguments.json'),guardReceipt=path.join(runDir,'owned-job.json');
 const helper=path.join(repoRoot,'tools/ymcc_sim/global/host-process.ps1');
 const originalDomain=command!==undefined;
 if(originalDomain&&(!command||!path.isAbsolute(command.executable)||!Array.isArray(command.args)||command.args.some(a=>typeof a!=='string'||a.includes('\0'))||!Array.isArray(command.sourceInputs)||command.sourceInputs.length===0))throw Error('OWNED_COMMAND_INVALID');
 const sourceFiles=[...new Set([helper,...additionalInputs,...(originalDomain?command.sourceInputs:[workerEntry]),fileURLToPath(import.meta.url),path.join(repoRoot,'tools/ymcc_sim/global/runner.mjs')])];
 const executable=originalDomain?command.executable:process.execPath;
 write(contextFile,{...context,scope:originalDomain?'original-domain':'optimized-adapters'});write(argumentsFile,originalDomain?command.args:[workerEntry,'--budget-worker',contextFile,context.nonce]);
 const pre=scanSourceIdentity(sourceFiles,deadline),sourcePins=pre.pins;
 if(pre.metadata.timedOut||performance.now()>=deadline-cleanupReserveMs){
  const result={schemaVersion:1,status:'TIMEOUT',runDir,budgetMs,cleanupReserveMs,wallMs:performance.now()-started,exitCode:124,processExitCode:null,processStarted:false,launchError:null,timedOut:true,timeoutPhase:pre.metadata.timedOut?'source-identity-pre':'worker-admission',ownedJob:null,ownedJobVerified:false,sourcePins,sourceDrift:[],sourceReadErrors:[],sourceIdentity:{status:'INCOMPLETE',pre:pre.metadata,post:null},sourceClosureComplete:false,sourceIdentityScope:'Wrapper, guard, runner and explicitly supplied entry/additional inputs only; transitive worker/domain closure is not established here',workerStatus:null,workerEvidenceError:null,workerActualPassedThisRun:null,partialEvidencePreserved:true,actualPassedThisRun:null,scope:originalDomain?'original-domain':'optimized-adapters',fixtureScope:'Original selected adapters/domain only; process exit and deadline are not acceptance',fullProductAcceptance:false};
  write(path.join(runDir,'execution-budget.json'),result);
  console.log(JSON.stringify({status:result.status,runDir,budgetMs,wallMs:result.wallMs,deadlineEnforced:false}));
  return {exitCode:124,result};
 }
 const processResult=await runProcess(powershell,['-NoLogo','-NoProfile','-NonInteractive','-File',helper,'-ProgramPath',executable,'-ArgumentsPath',argumentsFile,'-DeadlineEpochMs',String(context.deadlineEpochMs),'-CleanupReserveMs',String(cleanupReserveMs),'-DeadlineReceiptPath',guardReceipt],{cwd:repoRoot,env:process.env,timeoutMs:Math.max(1,Math.floor(budgetMs-(performance.now()-started)))});
 fs.writeFileSync(path.join(runDir,'budget.stdout.log'),processResult.stdout??'');fs.writeFileSync(path.join(runDir,'budget.stderr.log'),processResult.stderr??'');
 let worker=null,plan=null,guard=null,workerReadError=null;
 try{worker=read(path.join(runDir,'result.json'));}catch(error){workerReadError=error.code==='ENOENT'?'MISSING':'INVALID_JSON';}
 try{plan=read(path.join(runDir,'plan.json'));}catch{}try{guard=read(guardReceipt);}catch{}
 const workerValid=validWorker(worker,runDir),workerEvidenceError=originalDomain?null:workerReadError??(workerValid?null:'INVALID_SCHEMA');
 const ownedJobVerified=object(guard)&&guard.schemaVersion===1&&guard.jobDisposed===true&&Number.isInteger(guard.childPid)&&guard.childPid>0&&guard.exitCode===processResult.exitCode&&guard.deadlineEpochMs===context.deadlineEpochMs&&guard.cleanupReserveMs===cleanupReserveMs&&typeof guard.deadlineExpired==='boolean'&&guard.status===(guard.deadlineExpired?'TIMEOUT':guard.exitCode===0?'EXITED':'FAILED');
 const processTimeout=processResult.timedOut||processResult.exitCode===124||guard?.deadlineExpired===true||(!originalDomain&&workerValid&&/^TIMEOUT(?:_|$)/.test(worker.status));
 const ended=Number.isInteger(processResult.exitCode)&&!processResult.signal&&!processResult.launchError&&ownedJobVerified;
 const nonPassWorker=workerValid&&nonPassStatus.test(worker.status)&&worker.status!=='NOT_RUN_NO_RELEASE_BUGS';
 const post=scanSourceIdentity(sourceFiles,deadline,sourcePins),sourceDrift=post.drift,sourceReadErrors=post.readErrors;
 const timeout=processTimeout||post.metadata.timedOut||performance.now()>=deadline;
 const sourceIdentity={status:!post.metadata.complete?'INCOMPLETE':sourceDrift.length?'DRIFT':'MATCH',pre:pre.metadata,post:post.metadata};
 const originalStatus=processResult.exitCode===0?'ORIGINAL_DOMAIN_EXITED':processResult.exitCode===12?'ORIGINAL_DOMAIN_PLANNED':processResult.exitCode===10?'BLOCKED':'FAIL';
 let status;
 if(timeout)status='TIMEOUT';
 else if(!ended)status='FAIL';
 else if(originalDomain)status=processResult.exitCode===0&&sourceReadErrors.length?'INCOMPLETE_IDENTITY_READ':sourceDrift.length&&processResult.exitCode===0?'INCOMPLETE_IDENTITY_DRIFT':originalStatus;
 else if(processResult.exitCode!==0)status=nonPassWorker?worker.status:'FAIL';
 else if(nonPassWorker)status=worker.status;
 else if(sourceReadErrors.length)status='INCOMPLETE_IDENTITY_READ';
 else if(sourceDrift.length)status='INCOMPLETE_IDENTITY_DRIFT';
 else if(workerValid&&worker.status==='EXPLICIT_FULL_DELEGATED'&&worker.exitCode!==processResult.exitCode)status='INCOMPLETE_RESULT';
 else if(workerValid)status=worker.status;
 else status=workerReadError==='MISSING'&&validPlan(plan,args)?'PLAN_ONLY':'INCOMPLETE_RESULT';
 const exitCode=timeout?124:!ended?2:processResult.exitCode!==0?processResult.exitCode:/^FAIL(?:_|$)/.test(status)?2:/^(BLOCKED|INCOMPLETE|NOT_RUN)(?:_|$)/.test(status)&&status!=='NOT_RUN_NO_RELEASE_BUGS'?10:0;
 const result={schemaVersion:1,status,runDir,budgetMs,cleanupReserveMs,wallMs:performance.now()-started,exitCode,processExitCode:processResult.exitCode,processStarted:true,launchError:processResult.launchError??null,timedOut:timeout,timeoutPhase:processTimeout?'owned-process':post.metadata.timedOut?'source-identity-post':timeout?'final-classification':null,ownedJob:guard,ownedJobVerified,sourcePins,sourceDrift,sourceReadErrors,sourceIdentity,sourceClosureComplete:false,sourceIdentityScope:'Wrapper, guard, runner and explicitly supplied entry/additional inputs only; transitive worker/domain closure is not established here',workerStatus:worker?.status??null,workerEvidenceError,workerActualPassedThisRun:originalDomain||!workerValid||!ended?null:worker.summary?.actualPassedThisRun??null,partialEvidencePreserved:true,actualPassedThisRun:originalDomain||!workerValid||!ended||timeout||!post.metadata.complete||sourceDrift.length?null:worker.summary?.actualPassedThisRun??null,scope:originalDomain?'original-domain':'optimized-adapters',fixtureScope:'Original selected adapters/domain only; process exit and deadline are not acceptance',fullProductAcceptance:false};
 write(path.join(runDir,'execution-budget.json'),result);
 if(processResult.stdout)process.stdout.write(processResult.stdout);if(processResult.stderr)process.stderr.write(processResult.stderr);
 console.log(JSON.stringify({status,runDir,budgetMs,wallMs:result.wallMs,deadlineEnforced:ownedJobVerified}));
 return {exitCode,result};
}

/** The sole legacy dispatcher is the existing versioned simulator front door. */
export function validateLegacyRequest(request,repoRoot){
 const domains=['sleep','gamepad','full','msi','fan','global','doctor'];
 if(!request||request.schemaVersion!==1||request.kind!=='ymcc-legacy-domain-budget'||!domains.includes(request.domain)||!Array.isArray(request.arguments)||request.arguments.length<2||request.arguments.length>1024||request.arguments.some(a=>typeof a!=='string'||a.includes('\0'))||request.arguments[0]!=='-Domain'||request.arguments[1]!==request.domain)throw Error('LEGACY_BUDGET_REQUEST_INVALID');
 if(typeof request.executable!=='string'||!path.isAbsolute(request.executable)||!['pwsh.exe','powershell.exe'].includes(path.basename(request.executable).toLowerCase()))throw Error('LEGACY_BUDGET_POWERSHELL_REQUIRED');
 const entry=path.join(repoRoot,'tools/ymcc_sim/ymcc_sim.ps1');
 return {entry,budgetMs:validateWallBudget(request.budgetMs),command:{executable:request.executable,args:['-NoLogo','-NoProfile','-NonInteractive','-File',entry,...request.arguments],sourceInputs:[entry]}};
}
export async function runLegacyRequest(request,{repoRoot=path.resolve(import.meta.dirname,'../../..'),coreRoot=simulationCorePath()}={}){
 const selected=validateLegacyRequest(request,repoRoot);
 return runWithWallBudget(request.arguments,{repoRoot,coreRoot,budgetMs:selected.budgetMs,powershell:'pwsh.exe',outDir:path.join(coreRoot,'Evidence/Build/budget-'+request.domain),command:selected.command});
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 Promise.resolve().then(async()=>{
  if(process.argv.length!==3||process.argv[2]!=='--legacy-request-stdin')throw Error('LEGACY_BUDGET_STDIN_REQUEST_REQUIRED');
  const requestText=fs.readFileSync(0,'utf8');if(Buffer.byteLength(requestText)>256*1024)throw Error('LEGACY_BUDGET_REQUEST_TOO_LARGE');
  return (await runLegacyRequest(JSON.parse(requestText.replace(/^\uFEFF/,'')))).exitCode;
 }).then(code=>process.exitCode=code).catch(error=>{console.error('YMCC_EXECUTION_BUDGET_ERROR '+error.message);process.exitCode=3;});
}
