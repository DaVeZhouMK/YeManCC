// Opt-in adapter only. The existing raw producer owns the entire Python worker.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {fileURLToPath} from 'node:url';
import {spawnSync} from 'node:child_process';
import {startOwnedRawProducer, verifyRawProducerBudget, rawProgramLauncherDeclaration}
  from '../../../ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs';

const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const key=p=>path.resolve(p).toLowerCase();
const pin=p=>{const b=fs.readFileSync(p);return {path:path.resolve(p),bytes:b.length,sha256:sha(b)};};
const verify=row=>{assert.deepEqual(pin(row.path),row,'DECLARED_INPUT_DRIFT:'+row.path);return row;};
const save=(p,value)=>fs.writeFileSync(p,JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const cap=300000, started=Date.now(), mode=process.argv[2], planPath=path.resolve(process.argv[3]??'');
assert(['--check-plan','--execute'].includes(mode)&&process.argv.length===4,'Use --check-plan|--execute PLAN.json');
const plan=read(planPath), directory=plan.directory;
assert.equal(plan.kind,'typed-hostframe-owned-execution-plan-v1');
assert(['correctness','fixed-cost'].includes(plan.phase));assert.equal(plan.capMs,cap);
const correctnessScope=plan.correctnessScope??'full';
assert(['full','parity'].includes(correctnessScope),'UNKNOWN_CORRECTNESS_SCOPE');
assert.equal(correctnessScope,read(plan.config.path).correctnessScope??'full','CONFIG_CORRECTNESS_SCOPE_DIFFERS');
if(plan.phase!=='correctness')assert.equal(correctnessScope,'full','PARTIAL_CORRECTNESS_CANNOT_QUALIFY_FIXED_COST');
assert(Number.isSafeInteger(plan.remainingMs)&&plan.remainingMs>5000&&plan.remainingMs<=cap);
assert.equal(key(plan.bridge),key(fileURLToPath(import.meta.url)));
assert.equal(key(plan.tools.node),key(process.execPath),'EXPLICIT_NODE_REQUIRED');
assert.equal(key(path.dirname(planPath)),key(directory));
for(const row of [...plan.sources,...plan.toolArtifacts])verify(row);
assert.deepEqual(plan.core.profiles,['Release','Checked']);assert.deepEqual(plan.cost.profiles,['Release','Checked']);
assert.deepEqual(plan.core.variants.map(row=>row.name),['Record','BadValue','BadType','BadWire','FakeJson']);
assert.deepEqual(plan.parity.variants.map(row=>row.name),['Parity','MissingRef','Collapsed','CachedFailure','Eager','SuppressMismatch']);
for(const [name,hash] of Object.entries(plan.compileInputs)){
 assert(!name.split(/[\\/]/).includes('..')&&!path.isAbsolute(name),'COMPILE_INPUT_RELATIVE_NAME_REQUIRED');
 const file=name.startsWith('Parity/')?path.join(directory,name):path.join(plan.prepared,name);
 const actual=pin(file);assert.equal(actual.sha256,hash,'COMPILE_INPUT_IDENTITY');
 assert(plan.sources.some(row=>key(row.path)===key(file)&&row.sha256===hash),'COMPILE_INPUT_UNDECLARED');
}
const ledger=read(verify(plan.ledger).path);
assert.equal(ledger.phase,plan.phase);assert.equal(ledger.capMs,cap);assert.equal(ledger.remainingMs,plan.remainingMs);
assert(Array.isArray(ledger.entries),'ACTUAL_CUMULATIVE_LEDGER_REQUIRED');
let charged=0;
for(const entry of ledger.entries){
 const prior=read(verify(entry.process).path), intake=read(verify(entry.intake).path);
 const allocation=prior.rawExecution.budget;
 verifyRawProducerBudget(prior,prior.originalBudgetMs,allocation.parentBudgetMs);
 assert.equal(intake.verificationError,null);assert.equal(intake.collectionComplete,true);
 assert.equal(intake.exitCode,prior.exitCode);
 const minimum=read(prior.rawExecution.terminal.path).finishedEpochMs-allocation.parentStartedAtEpochMs;
 assert(Number.isSafeInteger(intake.elapsedMs)&&intake.elapsedMs>=minimum,'ACTUAL_PRIOR_CHARGE_REQUIRED');
 charged+=intake.elapsedMs;
 // A prior public invocation with the same complete compile inputs is a
 // reference, not another run, whether it passed or failed.
 if(intake.compileInputs)assert.notDeepEqual(intake.compileInputs,plan.compileInputs,'SAME_INPUT_PHASE_ALREADY_EXECUTED');
 if(prior.exitCode!==0||prior.error){
  const hashes=new Set(prior.rawExecution.sourceBefore.map(row=>row.sha256));
  const prefix=plan.phase==='fixed-cost'?'Cost/':'Core/';
  const required=Object.entries(plan.compileInputs).filter(([name])=>name.startsWith(prefix)).map(([,hash])=>hash);
  assert(!required.every(hash=>hashes.has(hash)),'FAILED_CANDIDATE_REEXECUTION_FORBIDDEN');
 }
}
assert.equal(cap-charged,plan.remainingMs,'CUMULATIVE_REMAINING_BUDGET_DIFFERS');
const launcher=rawProgramLauncherDeclaration({YMCC_SIM_RAW_PROGRAM_PWSH:plan.tools.powershell});
for(const file of launcher.files)assert(plan.toolArtifacts.some(row=>key(row.path)===key(file)),'UNDECLARED_POWERSHELL_RUNTIME');
let prerequisite=null;
if(plan.phase==='fixed-cost'){
 prerequisite=read(verify(plan.correctnessReceipt).path);
 assert.equal(prerequisite.status,'OWNED_TYPED_CORRECTNESS_PASS');assert.equal(prerequisite.phase,'correctness');
 assert.deepEqual(prerequisite.compileInputs,plan.compileInputs,'CORRECTNESS_SOURCE_PAIR_DIFFERS');
 for(const row of prerequisite.artifacts)verify(row);
 const prior=read(verify(prerequisite.process).path);
 verifyRawProducerBudget(prior,cap,prior.rawExecution.budget.parentBudgetMs);
 assert.equal(prior.exitCode,0);assert.equal(prior.error,null);
 const marker=read(prior.stdout.path);
 assert.deepEqual(marker.result,prerequisite.workerResult,'CORRECTNESS_RAW_RESULT_BINDING');
 const result=read(verify(prerequisite.workerResult).path);
 assert.equal(result.pass,true);assert.equal(result.phase,'correctness');
 assert.deepEqual(result.compileInputs,plan.compileInputs);assert.equal(result.reports.length,22);
}
assert(!fs.existsSync(path.join(directory,'Execution'))&&!fs.existsSync(path.join(directory,'OwnedJob')),'OUTPUT_ALREADY_EXECUTED');
if(mode==='--check-plan'){
 console.log(JSON.stringify({status:'OWNED_EXECUTION_PLAN_CHECKED_NOT_EXECUTED',phase:plan.phase,remainingMs:plan.remainingMs,
  sources:plan.sources.length,tools:plan.toolArtifacts.length,compileExecuted:false,businessExecuted:false,runtimeExecuted:false,sourceReady:false}));
}else{
 // One local claim consumes this supplied ledger. A second output directory
 // cannot reopen its allowance; continuation requires a new actual ledger.
 const claim=plan.ledger.path+'.typed-'+plan.phase+'.claim.json';
 save(claim,{phase:plan.phase,plan:pin(planPath),startedAtEpochMs:started,remainingMs:plan.remainingMs});
 let result=null, worker=null, failure=null, verificationError=null;
 try{
  const owner=spawnSync(plan.tools.powershell,['-NoLogo','-NoProfile','-NonInteractive','-Command',
   '[Diagnostics.Process]::GetProcessById('+process.pid+').StartTime.ToUniversalTime().Ticks.ToString()'],
   {env:plan.environment,cwd:directory,encoding:'utf8',windowsHide:true,timeout:Math.min(10000,plan.remainingMs-(Date.now()-started)-5000)});
  assert.equal(owner.status,0,'OWNER_BIRTH_QUERY_FAILED');const ownerStartTicks=owner.stdout.trim();
  const allocated=Date.now(), remaining=plan.remainingMs-(allocated-started);
  assert(remaining>5000,'ORIGINAL_REMAINING_BUDGET_EXHAUSTED');
  const deadline=allocated+Math.min(cap,remaining);
  const run=startOwnedRawProducer({id:'virtual-telemetry-hostframe-current-perf',program:plan.tools.python,
   args:['-I','-S','-B',plan.worker,'--owned-worker',planPath,String(deadline)],cwd:directory,env:plan.environment,
   host:plan.host,powershell:plan.tools.powershell,ownerStartTicks,directory:path.join(directory,'OwnedJob'),
   stdoutPath:path.join(directory,'worker.stdout.log'),stderrPath:path.join(directory,'worker.stderr.log'),
   deadlineEpochMs:deadline,allocatedAtEpochMs:allocated,parentStartedAtEpochMs:started,parentBudgetMs:plan.remainingMs,
   cleanupReserveMs:5000,sourceInputs:[...plan.sources,pin(planPath)],toolArtifacts:plan.toolArtifacts,
   originalBudgetMs:cap,parentRemainingBeforeMs:remaining,toolCleanupPolicy:plan.toolCleanupPolicy});
  result=await run.wait();save(path.join(directory,'PROCESS.json'),result);
  try{verifyRawProducerBudget(result,cap,plan.remainingMs);}catch(error){verificationError=String(error.stack??error);throw error;}
  assert.equal(result.exitCode,0);assert.equal(result.error,null);
  const marker=read(result.stdout.path);verify(marker.result);
  assert.equal(key(marker.result.path),key(path.join(directory,'Execution/RESULT.json')));
  worker=read(marker.result.path);assert.equal(worker.pass,true);assert.equal(worker.phase,plan.phase);
  assert.deepEqual(worker.compileInputs,plan.compileInputs);
  assert.equal(worker.correctnessScope,correctnessScope,'WORKER_CORRECTNESS_SCOPE_DIFFERS');
  const expectedSelection=plan.phase==='correctness'?plan.core.profiles.flatMap(profile=>[
   ...(correctnessScope==='full'?plan.core.variants.map(v=>[profile,'core',v.name]):[]),
   ...plan.parity.variants.map(v=>[profile,'parity',v.name])]):plan.cost.profiles.flatMap(profile=>[0,1].map(campaign=>[profile,'cost',campaign]));
  assert.deepEqual(worker.reports.map(r=>[r.profile,r.kind,r.kind==='cost'?r.campaign:r.variant]),expectedSelection,'EXACT_ORIGINAL_SELECTION_REQUIRED');
  for(const row of [...plan.sources,...plan.toolArtifacts,...worker.artifacts])verify(row);
 }catch(error){failure=String(error.stack??error);}
 const intake={status:failure?'OWNED_TYPED_FAILED_OR_INCOMPLETE':plan.phase==='correctness'?(correctnessScope==='full'?'OWNED_TYPED_CORRECTNESS_PASS':'OWNED_TYPED_PARITY_PASS'):'OWNED_TYPED_FIXED_COST_PASS',
  phase:plan.phase,correctnessScope,verificationError:verificationError??failure,collectionComplete:result?.rawExecution?.collectionComplete===true,
  exitCode:result?.exitCode??null,elapsedMs:Date.now()-started,capMs:cap,remainingBeforeMs:plan.remainingMs,
  compileInputs:plan.compileInputs,process:result?pin(path.join(directory,'PROCESS.json')):null,
  workerResult:worker?pin(path.join(directory,'Execution/RESULT.json')):null,
  artifacts:worker?[pin(planPath),pin(path.join(directory,'PROCESS.json')),pin(path.join(directory,'Execution/RESULT.json')),...worker.artifacts]:[],
  runtimeExecuted:result!==null,sourceReady:false,wholeProcessCpuAcceptance:false};
 save(path.join(directory,'INTAKE.json'),intake);console.log(JSON.stringify({status:intake.status,intake:pin(path.join(directory,'INTAKE.json'))}));
 if(failure)process.exitCode=1;
}
