import nodeTest,{after} from 'node:test';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const filterSpecified=process.env.YMCC_BUDGET_CASE_FILTER!==undefined;
const CaseFilter=filterSpecified?JSON.parse(process.env.YMCC_BUDGET_CASE_FILTER):null,availableCases=new Set(),selectedCases=[];
if(filterSpecified&&(!Array.isArray(CaseFilter)||!CaseFilter.length||CaseFilter.some(name=>typeof name!=='string')||new Set(CaseFilter).size!==CaseFilter.length))throw Error('BUDGET_CASE_FILTER_INVALID');
const test=(name,fn,directedOnly=false)=>{
 availableCases.add(name);
 if(CaseFilter===null){if(!directedOnly)return nodeTest(name,fn);}
 else if(CaseFilter.includes(name))selectedCases.push({name,fn});
};
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {runWithWallBudget,validateWallBudget,readBudgetWorker,validateLegacyRequest} from './execution-budget.mjs';
import {runProcess} from '../global/runner.mjs';
const repo=path.resolve(import.meta.dirname,'../../..'),core=path.resolve(process.env.YMCC_SIM_CORE_ROOT??'G:/YeManCC-Work/Testing/YMCC');
const root=path.resolve(process.env.YMCC_BUDGET_TEST_ROOT??path.join(core,'Evidence/Build/execution-budget-tests'));
const inside=(parent,file)=>{const relative=path.relative(parent,file);return relative!==''&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
if(!inside(fs.realpathSync(core),root))throw Error('BUDGET_TEST_ROOT_OUTSIDE_CORE');
let rootAncestor=root;while(!fs.existsSync(rootAncestor))rootAncestor=path.dirname(rootAncestor);
const realCore=fs.realpathSync(core),realAncestor=fs.realpathSync(rootAncestor);
if(path.relative(realCore,realAncestor)!==''&&!inside(realCore,realAncestor))throw Error('BUDGET_TEST_ROOT_ESCAPE');
fs.mkdirSync(root,{recursive:true});if(!inside(realCore,fs.realpathSync(root)))throw Error('BUDGET_TEST_ROOT_ESCAPE');
const own=fs.mkdtempSync(path.join(root,'owned-')),worker=path.join(own,'synthetic-worker.mjs');
// An inert owned child and descendant, not a product or a formal acceptance result.
fs.writeFileSync(worker,`import fs from 'node:fs';import path from 'node:path';import {spawn} from 'node:child_process';
const ctx=JSON.parse(fs.readFileSync(process.argv[3],'utf8')),mode=ctx.args[0];
if(mode==='hang'){const child=spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{stdio:'ignore',windowsHide:true});fs.writeFileSync(path.join(ctx.runDir,'owned-descendant.json'),JSON.stringify({pid:child.pid}));setInterval(()=>{},1000);}
else{
 if(mode==='drift')fs.appendFileSync(process.argv[1],'\\n// changed after launch\\n');
 const result={schemaVersion:1,status:mode==='blocked'?'INCOMPLETE':'PASS_SELECTED_SCOPE',runDir:ctx.runDir,summary:{selected:1,executedThisRun:mode==='blocked'?0:1,actualPassedThisRun:mode==='blocked'?0:1}};
 const reports={'fail-zero':'FAIL','timeout-zero':'TIMEOUT','incomplete-zero':'INCOMPLETE','blocked-zero':'BLOCKED','not-run-zero':'NOT_RUN_BUSY'};
 if(reports[mode]){result.status=reports[mode];result.summary.actualPassedThisRun=0;}
 if(mode==='empty-bug-plan'){result.status='NOT_RUN_NO_RELEASE_BUGS';result.noTestsExecuted=true;result.summary={selected:0,executedThisRun:0,actualPassedThisRun:0};}
 if(mode==='missing-schema')delete result.schemaVersion;
 if(mode==='bad-schema')result.schemaVersion=2;
 if(mode==='nonstring-status')result.status=7;
 if(mode==='unknown-status')result.status='UNRECOGNIZED_SUCCESS';
 if(mode==='bad-summary')result.summary.actualPassedThisRun=-1;
 if(mode==='foreign-result')result.runDir=path.dirname(ctx.runDir);
 const planModes=['missing-with-run-plan','broken-with-plan','plan-only','selected-plan','bad-plan-schema','legacy-header-plan','legacy-selected-plan','modeless-run-plan','conflicting-plan'];
 if(mode==='cached'){result.status='COMPLETE_WITH_CACHED_REFERENCES';result.summary={selected:1,executedThisRun:0,actualPassedThisRun:0,cachedReferences:1};result.rows=[{id:'inert',status:'CACHED_REFERENCE',executedThisRun:false,actualPassedThisRun:false}];}
 if(mode.startsWith('delegated')){result.status='EXPLICIT_FULL_DELEGATED';result.exitCode=mode==='delegated-failure'?2:mode==='delegated-mismatch'?2:0;result.fullProductAcceptance=false;delete result.summary;delete result.runDir;}
 if(planModes.includes(mode))fs.writeFileSync(path.join(ctx.runDir,'plan.json'),JSON.stringify({schemaVersion:mode==='bad-plan-schema'?2:1,...(mode==='plan-only'?{status:'PLAN_ONLY',executedThisRun:0}:{mode:mode==='missing-with-run-plan'?'run':'plan',noTestsExecuted:true,tests:[]})}));
 if(mode==='legacy-header-plan')fs.writeFileSync(path.join(ctx.runDir,'plan.json'),JSON.stringify({schemaVersion:1,status:'PLAN_ONLY'}));
 if(mode==='legacy-selected-plan'||mode==='modeless-run-plan')fs.writeFileSync(path.join(ctx.runDir,'plan.json'),JSON.stringify({schemaVersion:1,noTestsExecuted:true,tests:[{id:'inert'}]}));
 if(mode==='conflicting-plan')fs.writeFileSync(path.join(ctx.runDir,'plan.json'),JSON.stringify({schemaVersion:1,status:'FAIL',mode:'plan',noTestsExecuted:true,tests:[]}));
 if(mode==='broken-with-plan')fs.writeFileSync(path.join(ctx.runDir,'result.json'),'{broken');
 else if(!['missing-result',...planModes].includes(mode))fs.writeFileSync(path.join(ctx.runDir,'result.json'),JSON.stringify(result));
 process.exitCode=mode==='blocked'?10:mode==='bad-exit'||mode==='delegated-failure'?2:0;
}`);
const run=(mode,budgetMs=10000)=>runWithWallBudget([mode,...(mode==='modeless-run-plan'?['--mode','run']:[])],{repoRoot:repo,coreRoot:core,workerEntry:worker,budgetMs,outDir:own});
test('normal owned work produces current result, one guard and a finite shared budget',async()=>{
 const {exitCode,result}=await run('normal');assert.equal(exitCode,0);assert.equal(result.status,'PASS_SELECTED_SCOPE');assert.equal(result.ownedJob.jobDisposed,true);assert.equal(result.actualPassedThisRun,1);
});
test('a stale PASS-shaped file cannot mask the actual nonzero child exit',async()=>{
 const {exitCode,result}=await run('bad-exit');assert.equal(exitCode,2);assert.equal(result.status,'FAIL');
});
test('unexecuted closure evidence stays incomplete rather than becoming a product failure or a pass',async()=>{
 const {exitCode,result}=await run('blocked');assert.equal(exitCode,10);assert.equal(result.status,'INCOMPLETE');assert.equal(result.actualPassedThisRun,0);
});
test('one deadline reclaims an owned hanging worker and its descendant and preserves timeout evidence',async()=>{
 const {exitCode,result}=await run('hang',6000);assert.equal(exitCode,124);assert.equal(result.status,'TIMEOUT');assert.equal(result.timedOut,true);assert.ok(result.wallMs<8000);
 const pidFile=path.join(result.runDir,'owned-descendant.json');assert.equal(fs.existsSync(pidFile),true,'inert grandchild must actually start before testing cleanup');
 const {pid}=JSON.parse(fs.readFileSync(pidFile,'utf8'));assert.throws(()=>process.kill(pid,0));
 assert.equal(fs.existsSync(path.join(result.runDir,'execution-budget.json')),true);assert.notEqual(result.status,'PASS_SELECTED_SCOPE');
});
test('worker context rejects changed nonce and evidence outside the selected core',async()=>{
 const {result}=await run('normal');const context=path.join(result.runDir,'budget-context.json');
 assert.throws(()=>readBudgetWorker(context,'wrong',{coreRoot:core,repoRoot:repo}),/BUDGET_CONTEXT_INVALID/);
 assert.throws(()=>readBudgetWorker(context,'wrong',{coreRoot:path.join(own,'other'),repoRoot:repo}));
 for(const n of [0,999,1800001,NaN])assert.throws(()=>validateWallBudget(n));assert.equal(validateWallBudget(1800000),1800000);
});

test('source drift after launch prevents PASS even when the child reports success',async()=>{
 const {exitCode,result}=await run('drift');assert.equal(exitCode,10);assert.equal(result.status,'INCOMPLETE_IDENTITY_DRIFT');assert.equal(result.sourceDrift.length,1);
});

test('the original PowerShell script guard remains usable without opting into a new budget',async()=>{
 const script=path.join(own,'legacy-inert.ps1'),args=path.join(own,'legacy-arguments.json');
 fs.writeFileSync(script,"param([string]$Value)\nWrite-Output ('LEGACY_OWNED_GUARD_OK:'+ $Value)\nexit 0\n");fs.writeFileSync(args,JSON.stringify(['-Value','中文-🎮']));
 const result=await runProcess('pwsh.exe',['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(repo,'tools/ymcc_sim/global/host-process.ps1'),'-ScriptPath',script,'-ArgumentsPath',args],{cwd:repo,timeoutMs:10000});
 assert.equal(result.exitCode,0);assert.match(result.stdout,/LEGACY_OWNED_GUARD_OK:中文-🎮/);assert.equal(result.timedOut,false);
});

test('original domain command preserves its plan exit without inventing an actual PASS',async()=>{
 const entry=path.join(own,'original-plan.mjs');fs.writeFileSync(entry,"console.log('ORIGINAL_DOMAIN_PLAN_ONLY:原始计划：中文-🎮');process.exitCode=12;");
 const {exitCode,result}=await runWithWallBudget([],{repoRoot:repo,coreRoot:core,budgetMs:10000,outDir:own,command:{executable:process.execPath,args:[entry],sourceInputs:[entry]}});
 assert.equal(exitCode,12);assert.equal(result.status,'ORIGINAL_DOMAIN_PLANNED');assert.equal(result.actualPassedThisRun,null);assert.equal(result.scope,'original-domain');assert.equal(result.ownedJob.jobDisposed,true);assert.match(fs.readFileSync(path.join(result.runDir,'budget.stdout.log'),'utf8'),/原始计划：中文-🎮/);
});
test('legacy requests cannot replace the original front door, forge a domain, or widen the budget',()=>{
 const request={schemaVersion:1,kind:'ymcc-legacy-domain-budget',domain:'sleep',arguments:['-Domain','sleep','-Mode','sleeptypes'],budgetMs:10000,executable:'C:/synthetic/pwsh.exe'};
 const valid=validateLegacyRequest(request,repo);assert.equal(valid.entry,path.join(repo,'tools/ymcc_sim/ymcc_sim.ps1'));assert.deepEqual(valid.command.args.slice(-4),['-Domain','sleep','-Mode','sleeptypes']);
 for(const patch of [{domain:'unknown'},{arguments:['-Domain','fan']},{executable:'C:/synthetic/cmd.exe'},{budgetMs:1800001},{arguments:['-Domain','sleep',null]}])assert.throws(()=>validateLegacyRequest({...request,...patch},repo));
});


for(const [mode,status,code] of [['fail-zero','FAIL',2],['timeout-zero','TIMEOUT',124],['incomplete-zero','INCOMPLETE',10],['blocked-zero','BLOCKED',10],['not-run-zero','NOT_RUN_BUSY',10]])test('zero-exit worker report stays non-successful: '+mode,async()=>{
 const {exitCode,result}=await run(mode);assert.equal(result.status,status);assert.equal(exitCode,code);assert.equal(result.exitCode,code);assert.equal(result.processExitCode,0);assert.equal(result.timedOut,status==='TIMEOUT');
});
for(const mode of ['missing-result','missing-with-run-plan','broken-with-plan','missing-schema','bad-schema','nonstring-status','unknown-status','bad-summary','foreign-result','bad-plan-schema'])test('invalid current worker evidence cannot authorize success: '+mode,async()=>{
 const {exitCode,result}=await run(mode);assert.equal(exitCode,10);assert.match(result.status,/^INCOMPLETE/);assert.equal(result.exitCode,10);assert.equal(result.processExitCode,0);assert.equal(result.actualPassedThisRun,null);
});
for(const mode of ['plan-only','selected-plan'])test('a schema-valid current plan remains explicitly unexecuted: '+mode,async()=>{
 const {exitCode,result}=await run(mode);assert.equal(exitCode,0);assert.equal(result.status,'PLAN_ONLY');assert.equal(result.actualPassedThisRun,null);
});
test('an empty release bug plan remains NOT_RUN_NO_RELEASE_BUGS with zero actual passes',async()=>{
 const {exitCode,result}=await run('empty-bug-plan');assert.equal(exitCode,0);assert.equal(result.status,'NOT_RUN_NO_RELEASE_BUGS');assert.equal(result.actualPassedThisRun,0);assert.equal(result.timedOut,false);
});
test('wrapper pins and owned-job evidence state their partial scope',async()=>{
 const {exitCode,result}=await run('normal');assert.equal(exitCode,0);assert.equal(result.ownedJobVerified,true);assert.equal(result.sourceClosureComplete,false);assert.match(result.sourceIdentityScope,/entry|explicit/i);
 const context=JSON.parse(fs.readFileSync(path.join(result.runDir,'budget-context.json'),'utf8'));assert.equal(result.ownedJob.deadlineEpochMs,context.deadlineEpochMs);assert.equal(result.ownedJob.cleanupReserveMs,context.cleanupReserveMs);assert.equal(result.ownedJob.schemaVersion,1);assert.equal(result.ownedJob.exitCode,result.processExitCode);assert.ok(result.ownedJob.childPid>0);
});
test('legacy domain exits remain original, including failure during source drift',async()=>{
 const entry=path.join(own,'legacy-exits.mjs');fs.writeFileSync(entry,"import fs from 'node:fs';if(process.argv[3]==='drift')fs.appendFileSync(process.argv[1],'\\n// changed after launch\\n');process.exitCode=Number(process.argv[2]);");
 for(const [code,drift,status]of [[0,false,'ORIGINAL_DOMAIN_EXITED'],[10,false,'BLOCKED'],[2,false,'FAIL'],[12,true,'ORIGINAL_DOMAIN_PLANNED'],[2,true,'FAIL']]){
  const {exitCode,result}=await runWithWallBudget([],{repoRoot:repo,coreRoot:core,budgetMs:10000,outDir:own,command:{executable:process.execPath,args:[entry,String(code),drift?'drift':'steady'],sourceInputs:[entry]}});
  assert.equal(exitCode,code);assert.equal(result.exitCode,code);assert.equal(result.status,status);assert.equal(result.actualPassedThisRun,null);assert.equal(result.sourceDrift.length,drift?1:0);
 }
});


for(const mode of ['legacy-header-plan','legacy-selected-plan'])test('original plan envelope stays compatible without new required fields: '+mode,async()=>{
 const {exitCode,result}=await run(mode);assert.equal(exitCode,0);assert.equal(result.status,'PLAN_ONLY');assert.equal(result.actualPassedThisRun,null);
});
for(const mode of ['modeless-run-plan','conflicting-plan'])test('plan envelope cannot mask missing run evidence or an explicit failure: '+mode,async()=>{
 const {exitCode,result}=await run(mode);assert.equal(exitCode,10);assert.equal(result.status,'INCOMPLETE_RESULT');
});
for(const [mode,status,code]of [['delegated','EXPLICIT_FULL_DELEGATED',0],['delegated-failure','FAIL',2],['delegated-mismatch','INCOMPLETE_RESULT',10]])test('original full delegation envelope stays compatible: '+mode,async()=>{
 const {exitCode,result}=await run(mode);assert.equal(exitCode,code);assert.equal(result.status,status);assert.equal(result.fullProductAcceptance,false);assert.equal(result.actualPassedThisRun,null);
});
test('cached references retain their original envelope without inventing actual passes',async()=>{
 const {exitCode,result}=await run('cached');assert.equal(exitCode,0);assert.equal(result.status,'COMPLETE_WITH_CACHED_REFERENCES');assert.equal(result.actualPassedThisRun,0);
});

// These two original boundary cases are explicit-only; default tests create no delay fixtures.
const identityCaseNames={pre:'source identity pre scan stops at the original deadline before worker admission',post:'source identity post scan cannot authorize PASS after the original deadline'};
const identityDeadlineCase=async stage=>{
 const caseName=identityCaseNames[stage],caseDir=fs.mkdtempSync(path.join(own,'identity-'+stage+'-')),fixtureDir=path.join(caseDir,'files');fs.mkdirSync(fixtureDir);
 const helper=path.join(repo,'tools/fixtures/execution_budget_identity_delay.cjs'),delay=createRequire(import.meta.url)(helper);
 const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'),fixtures=[];
 for(let index=0;index<90;index++){
  const file=path.join(fixtureDir,String(index).padStart(3,'0')+'.identity-fixture');
  const bytes=Buffer.from(Array.from({length:257},(_,i)=>(i*131+index*17+(stage==='post'?19:0))%256));fs.writeFileSync(file,bytes);
  fixtures.push({path:file,bytes:fs.statSync(file).size,sha256:sha(file)});
 }
 const files=fixtures.map(row=>row.path),declaredInputs=[helper,fileURLToPath(import.meta.url)],inputs=declaredInputs.map(file=>({path:file,bytes:fs.statSync(file).size,sha256:sha(file)}));
 const manifestFile=path.join(caseDir,'inputs.json'),configFile=path.join(caseDir,'delay-config.json'),traceFile=path.join(caseDir,'trace.jsonl'),rawFile=path.join(caseDir,'identity-'+stage+'.raw.json');
 fs.writeFileSync(manifestFile,JSON.stringify({schemaVersion:1,caseName,stage,fixtures,inputs},null,2)+'\n');
 fs.writeFileSync(configFile,JSON.stringify({schemaVersion:1,fixtureDir,files,phase:stage,delayMs:stage==='pre'?20:40,traceFile},null,2)+'\n');
 let outcome,runError=null;
 delay.begin(configFile);
 try{outcome=await runWithWallBudget(['normal'],{repoRoot:repo,coreRoot:core,workerEntry:worker,budgetMs:stage==='pre'?1000:3000,outDir:caseDir,additionalInputs:[...files,...declaredInputs]});}
 catch(error){runError={name:error.name,message:error.message};}
 finally{delay.end();}
 const trace=fs.readFileSync(traceFile,'utf8').trim().split('\n').filter(Boolean).map(line=>JSON.parse(line));
 const observed=rows=>rows.map(row=>{try{return {...row,actualSha256:sha(row.path),readError:null};}catch(error){return {...row,actualSha256:null,readError:error.code??error.message};}});
 const fixtureChecks=observed(fixtures),inputChecks=observed(inputs),pinChecks=observed(outcome?.result.sourcePins??[]);
 const raw={schemaVersion:1,group:'execution-budget',caseName,caseCount:1,stage,fixtureCount:fixtures.length,fixtureBytes:fixtures.reduce((n,row)=>n+row.bytes,0),manifestFile,configFile,traceFile,delayedFiles:trace.filter(row=>row.delayed).length,trace,fixtureChecks,inputChecks,pinChecks,outcome,runError};
 fs.writeFileSync(rawFile,JSON.stringify(raw,null,2)+'\n');
 console.log('IDENTITY_DEADLINE_RAW '+JSON.stringify({caseName,path:rawFile,status:outcome?.result.status??'ERROR',wallMs:outcome?.result.wallMs??null,delayedFiles:raw.delayedFiles}));
 assert.equal(runError,null);assert.ok(outcome);
 const {exitCode,result}=outcome;
 assert.equal(exitCode,124);assert.equal(result.status,'TIMEOUT');assert.equal(result.timedOut,true);
 assert.equal(result.sourceIdentity.status,'INCOMPLETE');assert.deepEqual(result.sourceDrift,[]);assert.deepEqual(result.sourceReadErrors,[]);
 assert.equal(result.actualPassedThisRun,null);assert.equal(result.fullProductAcceptance,false);
 const scan=result.sourceIdentity[stage];assert.equal(scan.complete,false);assert.equal(scan.timedOut,true);assert.ok(scan.completed<scan.requested);assert.ok(raw.delayedFiles>0&&raw.delayedFiles<files.length);
 assert.ok(trace.every(row=>files.includes(row.path)&&row.api==='readSync'&&row.requestBytes<=64*1024),'only owned fixture byte reads may be delayed');
 const delayed=trace.filter(row=>row.delayed);assert.equal(new Set(delayed.map(row=>row.path)).size,delayed.length);assert.ok(delayed.every(row=>row.delayMs>0&&row.delayMs<=40));
 for(const row of [...fixtureChecks,...inputChecks,...pinChecks]){assert.equal(row.readError,null);assert.equal(row.actualSha256,row.sha256,'raw byte SHA mismatch: '+row.path);}
 const manifest=JSON.parse(fs.readFileSync(manifestFile,'utf8'));assert.deepEqual(manifest.fixtures,fixtures);assert.deepEqual(manifest.inputs,inputs);
 assert.equal(fs.existsSync(path.join(result.runDir,'execution-budget.json')),true);
 if(stage==='pre'){
  assert.ok(result.wallMs<1300,'pre scan must stop near the original 1000ms deadline');
  assert.equal(result.processStarted,false);assert.equal(result.ownedJob,null);assert.equal(result.ownedJobVerified,false);assert.equal(result.processExitCode,null);assert.equal(result.workerStatus,null);assert.equal(result.workerActualPassedThisRun,null);assert.equal(result.sourceIdentity.post,null);
  assert.equal(Object.hasOwn(result,'childPid'),false);
  for(const name of ['owned-job.json','result.json'])assert.equal(fs.existsSync(path.join(result.runDir,name)),false);
 }else{
  assert.ok(result.wallMs<3300,'post scan must stop near the original 3000ms deadline');
  assert.equal(result.sourceIdentity.pre.complete,true);assert.equal(result.workerStatus,'PASS_SELECTED_SCOPE');assert.equal(result.workerActualPassedThisRun,1);assert.equal(result.processExitCode,0);assert.equal(result.ownedJobVerified,true);assert.equal(result.ownedJob.jobDisposed,true);assert.equal(result.ownedJob.deadlineExpired,false);assert.equal(result.ownedJob.exitCode,0);assert.ok(result.ownedJob.childPid>0);
  const context=JSON.parse(fs.readFileSync(path.join(result.runDir,'budget-context.json'),'utf8'));
  assert.equal(result.ownedJob.deadlineEpochMs,context.deadlineEpochMs);assert.equal(result.ownedJob.cleanupReserveMs,context.cleanupReserveMs);
  const business=JSON.parse(fs.readFileSync(path.join(result.runDir,'result.json'),'utf8'));assert.equal(business.status,'PASS_SELECTED_SCOPE');assert.equal(business.summary.actualPassedThisRun,1);
 }
};
test(identityCaseNames.pre,()=>identityDeadlineCase('pre'),true);
test(identityCaseNames.post,()=>identityDeadlineCase('post'),true);
if(CaseFilter!==null){
 if(CaseFilter.some(name=>!availableCases.has(name)))throw Error('BUDGET_CASE_FILTER_UNKNOWN');
 const selectedNames=selectedCases.map(row=>row.name),actualNames=[],results=[];
 assert.equal(selectedNames.length,CaseFilter.length);
 fs.writeFileSync(path.join(own,'case-selection.json'),JSON.stringify({schemaVersion:1,group:'execution-budget',CaseFilter,selectedNames,selectedCount:selectedNames.length},null,2)+'\n');
 for(const row of selectedCases)nodeTest(row.name,async context=>{
  actualNames.push(row.name);
  try{await row.fn(context);results.push({name:row.name,status:'PASS'});}
  catch(error){results.push({name:row.name,status:'FAIL',error:error.message});throw error;}
 });
 after(()=>{
  const file=path.join(own,'case-results.raw.json'),raw={schemaVersion:1,group:'execution-budget',selectedNames,actualNames,selectedCount:selectedNames.length,actualCount:actualNames.length,results};
  fs.writeFileSync(file,JSON.stringify(raw,null,2)+'\n');console.log('BUDGET_CASES_RAW '+JSON.stringify({path:file,...raw}));
  assert.deepEqual(actualNames,selectedNames);assert.equal(results.length,selectedNames.length);
 });
}
