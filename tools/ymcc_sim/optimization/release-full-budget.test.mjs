import nodeTest from 'node:test';
import crypto from 'node:crypto';
const CaseFilter=process.env.YMCC_FULL_BUDGET_CASE_FILTER===undefined?null:JSON.parse(process.env.YMCC_FULL_BUDGET_CASE_FILTER),availableCases=new Set();
if(CaseFilter!==null&&(!Array.isArray(CaseFilter)||!CaseFilter.length||CaseFilter.some(name=>typeof name!=='string')||new Set(CaseFilter).size!==CaseFilter.length))throw Error('FULL_BUDGET_CASE_FILTER_INVALID');
const pendingTests=[];const enqueueTest=(...args)=>pendingTests.push(args);
const test=(name,...args)=>{availableCases.add(name);if(CaseFilter===null||CaseFilter.includes(name))return enqueueTest(name,...args);};
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createFullReleaseSession,readFullReleaseSession,runFullReleaseFixtureStage,validateLightingRequest,runHostedReleaseRegression,validateHostedRegression,HOSTED_REGRESSION_STAGES} from './release-full-budget.mjs';
import {simulationCorePath} from '../global/release-host-environment.mjs';
const repo=path.resolve(import.meta.dirname,'../../..'),core=simulationCorePath();
const root=process.env.YMCC_FULL_RELEASE_CONTROL_ROOT||path.join(core,'Evidence/FixtureIntake-20261030/Continuation-20261031/FullReleaseBudget-20261031/Controls');
const relativeRoot=path.relative(core,path.resolve(root));if(!path.isAbsolute(root)||relativeRoot===''||relativeRoot==='..'||relativeRoot.startsWith('..'+path.sep)||path.isAbsolute(relativeRoot))throw Error('FULL_RELEASE_CONTROL_ROOT_INVALID');
const q=value=>value.replaceAll("'","''"),json=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
let own,gate,workspace;
function prepareControlFixture(){
fs.mkdirSync(root,{recursive:true});const realRelativeRoot=path.relative(fs.realpathSync(core),fs.realpathSync(root));if(realRelativeRoot===''||realRelativeRoot==='..'||realRelativeRoot.startsWith('..'+path.sep)||path.isAbsolute(realRelativeRoot))throw Error('FULL_RELEASE_CONTROL_ROOT_INVALID');
own=fs.mkdtempSync(path.join(root,'owned-')),gate=path.join(own,'inert-gate.ps1'),workspace=path.join(own,'workspace');fs.mkdirSync(workspace);
fs.writeFileSync(path.join(own,'inert-child.mjs'),'setInterval(()=>{},1000);\n');
fs.writeFileSync(gate,`param([string]$Stage,[string]$WorkspaceRoot,[string]$CandidatePackage='',[string]$LifecycleEvidenceRoot='')
$ErrorActionPreference='Stop'
. '${q(path.join(repo,'tools/ymcc_sim/optimization/release-full-budget.ps1'))}'
Assert-YmccFullReleaseFixtureWorker -ProjectRoot '${q(repo)}' -Stage $Stage -WorkspaceRoot $WorkspaceRoot -CandidatePackage $CandidatePackage -LifecycleEvidenceRoot $LifecycleEvidenceRoot
@{stage=$Stage;attempt=$env:YMCC_RELEASE_FIXTURE_ATTEMPT}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $WorkspaceRoot ($Stage+'.json')) -Encoding UTF8
if($env:YMCC_FULL_BUDGET_TEST_MODE -eq 'fail'){exit 7}
if($env:YMCC_FULL_BUDGET_TEST_MODE -eq 'hang'){
 $child=Start-Process -FilePath '${q(process.execPath)}' -ArgumentList @('${q(path.join(own,'inert-child.mjs'))}') -WindowStyle Hidden -PassThru
 @{pid=$child.Id}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $WorkspaceRoot 'descendant.json') -Encoding UTF8
 Start-Sleep -Seconds 90
}
if($Stage -eq 'Build'){
 $buildReceipt=Join-Path $WorkspaceRoot 'Build/Validation/DeckyUpdate/BUILD.json'
 New-Item -ItemType Directory -Force -Path (Split-Path -Parent $buildReceipt)|Out-Null
 @{passed=$true;fixtureBudgetSession=$env:YMCC_RELEASE_FIXTURE_SESSION;fixtureBudgetAttempt=$env:YMCC_RELEASE_FIXTURE_ATTEMPT}|ConvertTo-Json|Set-Content -LiteralPath $buildReceipt -Encoding UTF8
}
exit 0
`);
terminal=path.join(own,'hosted-terminal'),pnpmCommand=path.join(terminal,'pnpm.cmd');fs.mkdirSync(terminal);
terminalScript=path.join(terminal,'inert-pnpm.mjs'),callsFile=path.join(terminal,'calls.jsonl');
fs.writeFileSync(terminalScript,`import fs from 'node:fs';import path from 'node:path';import {spawn} from 'node:child_process';
const args=process.argv.slice(2),mode=process.env.YMCC_FULL_BUDGET_TEST_MODE||'normal';
if(args.length!==2||args[0]!=='run')process.exit(9);
fs.appendFileSync(${JSON.stringify(callsFile)},JSON.stringify({test:args[1],mode,pid:process.pid})+String.fromCharCode(10));
if(mode==='hang'){const child=spawn(process.execPath,['-e','setInterval(()=>{},1000)'],{stdio:'ignore',windowsHide:true});fs.writeFileSync(path.join(${JSON.stringify(terminal)},'hang.json'),JSON.stringify({pid:process.pid,childPid:child.pid}));setInterval(()=>{},1000);}else process.exit(mode==='fail'?7:0);
`);
fs.writeFileSync(pnpmCommand,'@echo off\r\n"'+process.execPath+'" "'+terminalScript+'" %*\r\nexit /b %errorlevel%\r\n');
}
const session=(budgetMs=20000)=>createFullReleaseSession({repoRoot:repo,coreRoot:core,budgetMs});
async function stage(file,name,mode='normal',extra={}){
 const old=process.env.YMCC_FULL_BUDGET_TEST_MODE;process.env.YMCC_FULL_BUDGET_TEST_MODE=mode;
 try{return await runFullReleaseFixtureStage({stage:name,workspaceRoot:workspace,sessionFile:file,repoRoot:repo,coreRoot:core,gateEntry:gate,...extra});}
 finally{if(old===undefined)delete process.env.YMCC_FULL_BUDGET_TEST_MODE;else process.env.YMCC_FULL_BUDGET_TEST_MODE=old;}
}
test('Build and Package consume one cumulative quota; a successful stage cannot silently run twice',{timeout:30000},async()=>{
 const file=session();const lighting=await stage(file,'Lighting');assert.equal(lighting.exitCode,0);const build=await stage(file,'Build');assert.equal(build.exitCode,0);assert.equal(build.result.ownedJob.jobDisposed,true);
 const before=readFullReleaseSession(file,{repoRoot:repo,coreRoot:core}).session.spentMs;
 const packaged=await stage(file,'Package');assert.equal(packaged.exitCode,0);assert.equal(packaged.session.attempts.length,3);assert.equal(packaged.session.status,'COMPLETE');
 assert.ok(packaged.result.budgetMs<20000-before);assert.ok(packaged.session.spentMs>=before+packaged.result.wallMs);
 await assert.rejects(()=>stage(file,'Build'),/ALREADY_ATTEMPTED/);
 assert.equal(readFullReleaseSession(file,{repoRoot:repo,coreRoot:core}).session.attempts.length,3);
});
test('a failed original gate stays failed and Package cannot restart its quota',{timeout:15000},async()=>{
 const file=session(),result=await stage(file,'Build','fail');assert.equal(result.exitCode,7);assert.equal(result.session.status,'FAILED');assert.equal(result.result.status,'FAIL');
 await assert.rejects(()=>stage(file,'Package'),/NOT_RESUMABLE/);assert.equal(readFullReleaseSession(file,{repoRoot:repo,coreRoot:core}).session.attempts.length,1);
});
test('the second stage times out on remaining time and reclaims its owned descendant',{timeout:18000},async()=>{
 const file=session(7000),started=performance.now();const first=await stage(file,'Build');assert.equal(first.exitCode,0);
 const next=await stage(file,'Package','hang');assert.equal(next.exitCode,124);assert.equal(next.session.status,'EXHAUSTED');assert.equal(next.result.ownedJob.jobDisposed,true);assert.ok(next.result.budgetMs<7000-first.session.spentMs);
 assert.ok(performance.now()-started<9000,'two stages must not get two seven-second quotas');
 const pid=json(path.join(workspace,'descendant.json')).pid;assert.throws(()=>process.kill(pid,0));
 assert.equal(next.result.fullProductAcceptance,false);
});
test('outside-core, changed workspace, expired and interrupted sessions fail before a new stage',{timeout:15000},async()=>{
 const file=session();const original=json(file);fs.writeFileSync(path.join(own,'outside.json'),JSON.stringify(original));
 assert.throws(()=>readFullReleaseSession(path.join(own,'outside.json'),{repoRoot:repo,coreRoot:path.join(own,'workspace')}),/SCOPE_INVALID/);
 const first=await stage(file,'Build');assert.equal(first.exitCode,0);
 await assert.rejects(()=>stage(file,'Package','normal',{workspaceRoot:own}),/WORKSPACE_CHANGED/);
 const stopped=json(file);stopped.spentMs=stopped.budgetMs-500;fs.writeFileSync(file,JSON.stringify(stopped));
 await assert.rejects(()=>stage(file,'Package'),/BUDGET_EXHAUSTED/);assert.equal(json(file).status,'EXHAUSTED');
 const interrupted=session();fs.writeFileSync(interrupted+'.lock','owned-test');await assert.rejects(()=>stage(interrupted,'Build'),/BUSY_OR_INTERRUPTED/);fs.unlinkSync(interrupted+'.lock');
 for(const budgetMs of [0,999,1800001,NaN])assert.throws(()=>session(budgetMs));
});
test('a stale inherited worker token cannot bypass the original gate budget dispatch',{timeout:15000},()=>{
 const file=session(),target=path.join(own,'never-created');
 const result=spawnSync('pwsh.exe',['-NoLogo','-NoProfile','-NonInteractive','-File',path.join(repo,'tools/decky_update_release_gate.ps1'),'-Stage','Build','-WorkspaceRoot',target],{encoding:'utf8',timeout:10000,windowsHide:true,env:{...process.env,YMCC_RELEASE_FIXTURE_SESSION:file,YMCC_RELEASE_FIXTURE_ATTEMPT:'stale'}});
 assert.notEqual(result.status,0);assert.match(result.stderr+result.stdout,/FULL_RELEASE_WORKER_CONTEXT_INVALID/);assert.equal(fs.existsSync(target),false);
});
test('actual publish control flow shares one session and restores the caller environment; default export creates none',{timeout:25000},()=>{
 const controlScope=process.env.YMCC_PUBLISH_CONTROL_SCOPE??'all';assert(['all','timeout-only'].includes(controlScope),'PUBLISH_CONTROL_SCOPE_INVALID');const timeoutOnly=controlScope==='timeout-only';let fullOuter;
 const fake=path.join(own,'publish-repo'),tools=path.join(fake,'tools'),helper=path.join(tools,'ymcc_sim/optimization');fs.mkdirSync(helper,{recursive:true});
 fs.copyFileSync(path.join(repo,'tools/publish.ps1'),path.join(tools,'publish.ps1'));
 const stagesFile=path.join(own,'publish-stages.jsonl'),sessionPath=path.join(own,'publish-session.json');
  const fakeGlobal=path.join(tools,'ymcc_sim/global');fs.mkdirSync(fakeGlobal,{recursive:true});
  for(const file of ['host-process.ps1','runner.mjs','release-host-environment.ps1'])fs.copyFileSync(path.join(repo,'tools/ymcc_sim/global',file),path.join(fakeGlobal,file));
 fs.writeFileSync(path.join(helper,'release-full-budget.mjs'),`import fs from 'node:fs';import {runFullReleasePublishInvocation as run} from ${JSON.stringify(new URL('./release-full-budget.mjs',import.meta.url).href)};const args=process.argv.slice(2);if(args.length!==4||args[0]!=='--action'||args[1]!=='publish-invocation'||args[2]!=='--request-file')throw Error('FIXED_PUBLISH_DISPATCH_ONLY');run(args[3],{repoRoot:${JSON.stringify(fake)},coreRoot:${JSON.stringify(core)}}).then(r=>process.exitCode=r.exitCode).catch(e=>{console.error(e);process.exitCode=3;});\n`);

 fs.writeFileSync(path.join(helper,'release-full-budget.ps1'),fs.readFileSync(path.join(repo,'tools/ymcc_sim/optimization/release-full-budget.ps1'),'utf8')+`\nfunction New-YmccFullReleaseFixtureSession {param([string]$ProjectRoot,[int]$BudgetSeconds) @{budget=$BudgetSeconds}|ConvertTo-Json|Set-Content -LiteralPath '${q(sessionPath)}' -Encoding UTF8; return '${q(sessionPath)}'}
`);
 for(const name of ['build-workspace','package-release'])fs.writeFileSync(path.join(tools,name+'.ps1'),`param([string]$WorkspaceRoot,[switch]$FullAcceptance,[switch]$FastRelease,[switch]$TestExport) if($env:YMCC_FULL_PUBLISH_CONTROL_MODE -eq 'fail' -and '${name}' -eq 'build-workspace'){exit 7};if($env:YMCC_FULL_PUBLISH_CONTROL_MODE -eq 'hang' -and '${name}' -eq 'build-workspace'){ $child=Start-Process -WindowStyle Hidden -FilePath '${q(process.execPath)}' -ArgumentList @('-e','setInterval(()=>{},1000)') -PassThru;@{pid=$PID;childPid=$child.Id}|ConvertTo-Json|Set-Content -LiteralPath '${q(path.join(own,'publish-hanging.json'))}' -Encoding UTF8;Start-Sleep -Seconds 90 }; @{name='${name}';session=$env:YMCC_RELEASE_FIXTURE_SESSION;workspace=$WorkspaceRoot;workspaceEnvironment=$env:YEMAN_WORKSPACE_ROOT;full=[bool]$FullAcceptance;fast=[bool]$FastRelease;test=[bool]$TestExport}|ConvertTo-Json -Compress|Add-Content -LiteralPath '${q(stagesFile)}' -Encoding UTF8;exit 0
`);
 const harness=path.join(own,'publish-harness.ps1'),outFile=path.join(own,'publish-restored.json');
 fs.writeFileSync(harness,`param([switch]$Full,[switch]$Fast,[int]$Seconds=17,[switch]$DefaultRoot)
$env:YMCC_RELEASE_FIXTURE_SESSION='caller-session'
$env:YEMAN_WORKSPACE_ROOT='caller-workspace';$env:YEMAN_TEST_EXPORT='caller-test';$env:YEMAN_FULL_ACCEPTANCE='caller-full'
if($DefaultRoot){$env:YEMAN_WORKSPACE_ROOT=''}
function pnpm { throw 'CONTROL_PNPM_PREFLIGHT_AUTO_INSTALL_FORBIDDEN' }
if($DefaultRoot){ & '${q(path.join(tools,'publish.ps1'))}' }elseif($Full){ & '${q(path.join(tools,'publish.ps1'))}' -WorkspaceRoot '${q(workspace)}' -FullAcceptance -FixtureBudgetSeconds $Seconds }elseif($Fast){ & '${q(path.join(tools,'publish.ps1'))}' -WorkspaceRoot '${q(workspace)}' -FastRelease }else{ & '${q(path.join(tools,'publish.ps1'))}' -WorkspaceRoot '${q(workspace)}' }
@{restored=$env:YMCC_RELEASE_FIXTURE_SESSION;workspace=$env:YEMAN_WORKSPACE_ROOT;test=$env:YEMAN_TEST_EXPORT;full=$env:YEMAN_FULL_ACCEPTANCE}|ConvertTo-Json|Set-Content -LiteralPath '${q(outFile)}' -Encoding UTF8
exit $LASTEXITCODE
`);
 const restored=()=>assert.deepEqual(json(outFile),{restored:'caller-session',workspace:'caller-workspace',test:'caller-test',full:'caller-full'});
 if(!timeoutOnly){
 const full=spawnSync('pwsh.exe',['-NoProfile','-File',harness,'-Full'],{encoding:'utf8',timeout:15000,windowsHide:true,env:{...process.env,YMCC_FULL_PUBLISH_WORKER:'forged-inherited-marker'}});assert.equal(full.status,0,full.stderr+full.stdout);
 fullOuter=full.stdout.split(/\r?\n/).filter(line=>line.startsWith('{')).map(line=>{try{return JSON.parse(line);}catch{return null;}}).find(row=>row?.kind==='ymcc-full-publish-whole-invocation');assert(fullOuter);assert.equal(fullOuter.originalInvocationRuns,1);assert.equal(fullOuter.ownedJobVerified,true);assert.equal(fullOuter.ownedJobDisposed,true);assert.equal(fullOuter.automaticRetries,0);
 restored();
 const records=fs.readFileSync(stagesFile,'utf8').replace(/^\uFEFF/,'').trim().split(/\r?\n/).map(JSON.parse);assert.equal(records.length,2);assert.equal(records[0].session,sessionPath);assert.equal(records[1].session,sessionPath);assert.equal(json(sessionPath).budget,17);assert.equal(json(outFile).restored,'caller-session');restored();
 fs.unlinkSync(stagesFile);fs.unlinkSync(sessionPath);
 const normal=spawnSync('pwsh.exe',['-NoProfile','-File',harness],{encoding:'utf8',timeout:15000,windowsHide:true});assert.equal(normal.status,0,normal.stderr+normal.stdout);
 const ordinary=fs.readFileSync(stagesFile,'utf8').replace(/^\uFEFF/,'').trim().split(/\r?\n/).map(JSON.parse);assert.equal(ordinary.length,2);assert.deepEqual(ordinary.map(row=>row.name),['build-workspace','package-release']);for(const row of ordinary){assert.equal(row.test,true);assert.equal(row.full,false);assert.equal(row.fast,false);assert.equal(row.workspace,workspace);assert.equal(row.workspaceEnvironment,workspace);}assert.equal(fs.existsSync(sessionPath),false);assert.equal(json(outFile).restored,'caller-session');restored();
 fs.unlinkSync(stagesFile);
 const defaultRoot=spawnSync(path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe'),['-NoProfile','-File',harness,'-DefaultRoot'],{encoding:'utf8',timeout:10000,windowsHide:true});assert.equal(defaultRoot.status,0,defaultRoot.stdout+defaultRoot.stderr);
 const defaultStages=fs.readFileSync(stagesFile,'utf8').replace(/^\uFEFF/,'').trim().split(/\r?\n/).map(JSON.parse);assert.equal(defaultStages.length,2);assert.deepEqual(defaultStages.map(row=>row.name),['build-workspace','package-release']);for(const row of defaultStages){assert.equal(row.workspace,path.resolve(fake,'../..'));assert.equal(row.workspaceEnvironment,row.workspace);assert.equal(row.test,true);assert.equal(row.full,false);assert.equal(row.fast,false);assert.equal(row.session,'caller-session');}assert.equal(fs.existsSync(sessionPath),false);assert.deepEqual(json(outFile),{restored:'caller-session',workspace:null,test:'caller-test',full:'caller-full'});
 fs.unlinkSync(stagesFile);
 const ordinaryFailed=spawnSync('pwsh.exe',['-NoProfile','-File',harness],{encoding:'utf8',timeout:10000,windowsHide:true,env:{...process.env,YMCC_FULL_PUBLISH_CONTROL_MODE:'fail'}});assert.notEqual(ordinaryFailed.status,0,ordinaryFailed.stdout+ordinaryFailed.stderr);assert.equal(fs.existsSync(stagesFile),false,'Ordinary Build failure must not run Package, invoke pnpm, or retry');assert.equal(fs.existsSync(sessionPath),false);restored();
 const fast=spawnSync('pwsh.exe',['-NoProfile','-File',harness,'-Fast'],{encoding:'utf8',timeout:15000,windowsHide:true});assert.equal(fast.status,0,fast.stderr+fast.stdout);
 const quick=fs.readFileSync(stagesFile,'utf8').replace(/^\uFEFF/,'').trim().split(/\r?\n/).map(JSON.parse);assert.equal(quick.length,2);assert.equal(quick[0].fast,true);assert.equal(quick[1].fast,true);assert.equal(quick[0].full,false);assert.equal(fs.existsSync(sessionPath),false);assert.equal(json(outFile).restored,'caller-session');restored();
  fs.unlinkSync(stagesFile);
  const failed=spawnSync('pwsh.exe',['-NoProfile','-File',harness,'-Full'],{encoding:'utf8',timeout:12000,windowsHide:true,env:{...process.env,YMCC_FULL_PUBLISH_CONTROL_MODE:'fail'}});
  assert.notEqual(failed.status,0,failed.stdout+failed.stderr);assert.equal(fs.existsSync(stagesFile),false,'Failed Build must not run Package or retry Build');restored();
 }
  const hanging=spawnSync('pwsh.exe',['-NoProfile','-File',harness,'-Full','-Seconds','6'],{encoding:'utf8',timeout:9000,windowsHide:true,env:{...process.env,YMCC_FULL_PUBLISH_CONTROL_MODE:'hang'}});
  assert.equal(hanging.status,124,hanging.stdout+hanging.stderr);assert.equal(fs.existsSync(stagesFile),false,'Timeout must not run Package or retry');restored();
  const pids=json(path.join(own,'publish-hanging.json'));const alive=spawnSync('pwsh.exe',['-NoProfile','-Command',`@(Get-Process -Id ${pids.pid},${pids.childPid} -ErrorAction SilentlyContinue).Count`],{encoding:'utf8',timeout:3000,windowsHide:true});assert.equal(alive.status,0);assert.equal(alive.stdout.trim(),'0','Owned parent and descendant must both be gone');
 if(!timeoutOnly){
 const firstRaw=fs.readFileSync(fullOuter.request.path,'utf8');
 const replay=spawnSync(process.execPath,[path.join(helper,'release-full-budget.mjs'),'--action','publish-invocation','--request-file',fullOuter.request.path],{encoding:'utf8',timeout:3000,windowsHide:true});
 assert.notEqual(replay.status,0);assert.match(replay.stderr,/EEXIST|FULL_RELEASE_BUDGET_EXHAUSTED/);assert.equal(fs.readFileSync(fullOuter.request.path,'utf8'),firstRaw);assert.equal(fs.existsSync(stagesFile),false,'Consumed request cannot start a second producer');
 }
 console.log('PUBLISH_CONTROL_SCOPE '+JSON.stringify({scope:controlScope,timeoutBudgetSeconds:6,ownedDescendantStartedAndGone:true,actualProductBuilds:0,actualFullAcceptance:false,completeCallbackCoverage:!timeoutOnly}));

});

test('original required-step function records success and failure time without accepting a failed child',{timeout:15000},()=>{
 const harness=path.join(own,'step-timing-harness.ps1'),output=path.join(own,'step-timing.json'),child=path.join(own,'step-exit.mjs');fs.writeFileSync(child,'process.exitCode=Number(process.argv[2]);\n');
 fs.writeFileSync(harness,`$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
$tokens=$null;$errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile('${q(path.join(repo,'tools/decky_update_release_gate.ps1'))}',[ref]$tokens,[ref]$errors)
$node=$ast.Find({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Invoke-Required'},$true)
. ([scriptblock]::Create($node.Extent.Text))
$gateRoot='${q(own)}'
$steps=New-Object 'System.Collections.Generic.List[object]'
Invoke-Required 'timed-positive' '${q(process.execPath)}' @('${q(child)}','0')
$caught=$false
try{Invoke-Required 'timed-failure' '${q(process.execPath)}' @('${q(child)}','9')}catch{$caught=$true}
@{steps=@($steps.ToArray());caught=$caught}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath '${q(output)}' -Encoding UTF8
`);
 const result=spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',harness],{encoding:'utf8',timeout:12000,windowsHide:true});assert.equal(result.status,0,result.stderr+result.stdout);
 const value=json(output);assert.equal(value.caught,true);assert.equal(value.steps.length,2);assert.equal(value.steps[0].passed,true);assert.equal(value.steps[1].passed,false);assert.equal(value.steps[1].exitCode,9);
 for(const row of value.steps){assert.ok(row.wallMs>=0);assert.ok(row.startedAtUtc);assert.ok(fs.existsSync(row.log));assert.match(row.sha256,/^[0-9a-f]{64}$/);}
});

test('lighting route keeps the original one consumer, payload/HC inputs and 240-second ceiling',()=>{
 const a=['-NoProfile','-File',path.join(repo,'tools/ymcc_sim/ymcc_sim.ps1'),'-Domain','global','-Mode','run','-Tests','lighting.signal-consumer','-LightingPayloadRoot',path.join(workspace,'payload'),'-ExpectedHelperSha256','a'.repeat(64),'-RuntimeRoot',path.join(repo,'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1'),'-SharedRuntimeRoot',path.join(repo,'PowerControl/ymcc-shared-runtime'),'-HcProfile','ymcc-device-core-v1','-EvidenceDir',path.join(workspace,'global')];
 const pwsh=spawnSync('where.exe',['pwsh.exe'],{encoding:'utf8',windowsHide:true}).stdout.trim().split(/\r?\n/)[0];
 const request={schemaVersion:1,kind:'ymcc-lighting-release-fixture-request',executable:pwsh,workspaceRoot:workspace,arguments:a};
 const selected=validateLightingRequest(request,{repoRoot:repo});assert.deepEqual(selected.command.args,a);assert.equal(selected.command.maxWallMs,240000);
 for(const [i,v] of [[2,gate],[8,'plugins.decky-persistent-lifecycle'],[10,own],[12,'bad-sha'],[14,workspace],[18,'hc-approved']]){const changed=[...a];changed[i]=v;assert.throws(()=>validateLightingRequest({...request,arguments:changed},{repoRoot:repo}));}
 assert.throws(()=>validateLightingRequest({...request,arguments:[...a,'-Extra','any']},{repoRoot:repo}));
});

test('standalone Package reuses the bound Build session and missing session evidence refuses before dispatch',{timeout:15000},()=>{
 const target=path.join(own,'standalone-package'),receipt=path.join(target,'Build/Validation/DeckyUpdate/BUILD.json');fs.mkdirSync(path.dirname(receipt),{recursive:true});const file=session();fs.writeFileSync(receipt,JSON.stringify({passed:true,fixtureBudgetSession:file}));
 const harness=path.join(own,'package-session-harness.ps1'),output=path.join(own,'package-session-call.json');
 fs.writeFileSync(harness,`$ErrorActionPreference='Stop'
. '${q(path.join(repo,'tools/ymcc_sim/optimization/release-full-budget.ps1'))}'
function Get-YmccFullReleaseNode { return 'Invoke-InertBudgetNode' }
function Invoke-InertBudgetNode {param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments) @($Arguments)|ConvertTo-Json|Set-Content -LiteralPath '${q(output)}' -Encoding UTF8;$global:LASTEXITCODE=0}
$env:YMCC_RELEASE_FIXTURE_SESSION=$null
Invoke-YmccFullReleaseFixtureStage -ProjectRoot '${q(repo)}' -Stage Package -WorkspaceRoot '${q(target)}' -CandidatePackage '${q(path.join(target,'candidate.zip'))}'
`);
 const run=()=>spawnSync('pwsh.exe',['-NoProfile','-File',harness],{encoding:'utf8',timeout:12000,windowsHide:true});const first=run();assert.equal(first.status,0,first.stderr+first.stdout);
 const arguments_=json(output);assert.equal(arguments_[arguments_.indexOf('--session')+1],file);assert.equal(arguments_[arguments_.indexOf('--stage')+1],'Package');
 fs.unlinkSync(output);fs.writeFileSync(receipt,JSON.stringify({passed:true}));const missing=run();assert.notEqual(missing.status,0);assert.match(missing.stdout+missing.stderr,/FULL_RELEASE_BUILD_SESSION_REQUIRED/);assert.equal(fs.existsSync(output),false);
});
test('the original lighting Run function forwards its exact command once through the shared session',{timeout:15000},()=>{
 const target=path.join(own,'lighting-workspace'),candidate=path.join(target,'candidate'),fake=path.join(own,'lighting-project'),entry=path.join(fake,'tools/ymcc_sim/optimization/release-full-budget.mjs');fs.mkdirSync(candidate,{recursive:true});fs.mkdirSync(path.dirname(entry),{recursive:true});
 fs.writeFileSync(entry,"import fs from 'node:fs';const a=process.argv.slice(2);const r=JSON.parse(fs.readFileSync(a[a.indexOf('--request-file')+1],'utf8'));console.log('RESULT '+r.arguments[20]+'/result.json');\n");
 const args=['-NoProfile','-File',path.join(fake,'tools/ymcc_sim/ymcc_sim.ps1'),'-Domain','global','-Mode','run','-Tests','lighting.signal-consumer','-LightingPayloadRoot',path.join(candidate,'payload'),'-ExpectedHelperSha256','b'.repeat(64),'-RuntimeRoot',path.join(fake,'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1'),'-SharedRuntimeRoot',path.join(fake,'PowerControl/ymcc-shared-runtime'),'-HcProfile','ymcc-device-core-v1','-EvidenceDir',path.join(candidate,'global')];
 const file=session(),input=path.join(candidate,'arguments.json'),harness=path.join(candidate,'harness.ps1'),output=path.join(candidate,'observed.json');fs.writeFileSync(input,JSON.stringify(args));
 fs.writeFileSync(harness,`$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile('${q(path.join(repo,'tools/build-lightsetter-payload.ps1'))}',[ref]$tokens,[ref]$errors)
$fn=$ast.Find({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq 'Run'},$true)
. ([scriptblock]::Create($fn.Extent.Text))
function Save([string]$File,$Value){[IO.File]::WriteAllText($File,($Value|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))}
function Get-YmccLightingFileHash([string]$File){return (Get-FileHash -LiteralPath $File).Hash.ToLowerInvariant()}
$project='${q(fake)}';$workspace='${q(target)}';$candidate='${q(candidate)}';$toolCalls=[Collections.Generic.List[object]]::new()
$env:YMCC_RELEASE_FIXTURE_SESSION='${q(file)}'
$arguments=@(Get-Content -LiteralPath '${q(input)}' -Raw|ConvertFrom-Json)
$text=Run 'global-consumers' (Join-Path $PSHOME 'pwsh.exe') $arguments
Save '${q(output)}' @{text=$text;calls=@($toolCalls);originalExecutable=(Join-Path $PSHOME 'pwsh.exe')}
`);
 const result=spawnSync('pwsh.exe',['-NoProfile','-File',harness],{encoding:'utf8',timeout:12000,windowsHide:true});assert.equal(result.status,0,result.stderr+result.stdout);
 const value=json(output),request=json(path.join(candidate,'fixture-budget-request.json'));assert.equal(value.calls.length,1);assert.equal(request.sessionFile,file);assert.deepEqual(request.arguments,args);assert.equal(request.executable,value.originalExecutable);assert.match(value.text,/^RESULT /);assert.equal(value.calls[0].exitCode,0);
});

test('direct full Build initializes once while ordinary Build never creates a fixture session',{timeout:15000},()=>{
 const fake=path.join(own,'direct-build'),tools=path.join(fake,'tools'),helper=path.join(tools,'ymcc_sim/optimization');fs.mkdirSync(helper,{recursive:true});const counter=path.join(fake,'created.json');
 fs.writeFileSync(path.join(helper,'release-full-budget.ps1'),`function New-YmccFullReleaseFixtureSession {param([string]$ProjectRoot) @{root=$ProjectRoot}|ConvertTo-Json|Set-Content -LiteralPath '${q(counter)}' -Encoding UTF8;return 'one-shared-session'}
`);
 const harness=path.join(tools,'harness.ps1'),output=path.join(fake,'output.json');
 fs.writeFileSync(harness,`$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null;$ast=[System.Management.Automation.Language.Parser]::ParseFile('${q(path.join(repo,'tools/build-workspace.ps1'))}',[ref]$tokens,[ref]$errors)
$init=$ast.Find({param($n) $n -is [System.Management.Automation.Language.IfStatementAst] -and $n.Extent.Text.StartsWith('if($FullAcceptance -and -not $env:YMCC_RELEASE_FIXTURE_SESSION)')},$true)
if(-not $init){throw 'DIRECT_BUILD_INIT_NOT_FOUND'}
$initFile=Join-Path $PSScriptRoot 'actual-init.ps1';[IO.File]::WriteAllText($initFile,$init.Extent.Text)
$env:YMCC_RELEASE_FIXTURE_SESSION=$null;$FullAcceptance=$false;. $initFile;$ordinaryCreated=Test-Path -LiteralPath '${q(counter)}'
$FullAcceptance=$true;. $initFile;$first=$env:YMCC_RELEASE_FIXTURE_SESSION
Remove-Item -LiteralPath '${q(counter)}';. $initFile;$createdTwice=Test-Path -LiteralPath '${q(counter)}'
@{ordinaryCreated=$ordinaryCreated;first=$first;createdTwice=$createdTwice}|ConvertTo-Json|Set-Content -LiteralPath '${q(output)}' -Encoding UTF8
`);
 const result=spawnSync('pwsh.exe',['-NoProfile','-File',harness],{encoding:'utf8',timeout:12000,windowsHide:true});assert.equal(result.status,0,result.stderr+result.stdout);assert.deepEqual(json(output),{ordinaryCreated:false,first:'one-shared-session',createdTwice:false});
});

test('a different fresh quota or a changed Build receipt cannot fund Package',{timeout:15000},async()=>{
 const original=session(),build=await stage(original,'Build');assert.equal(build.exitCode,0);
 const fresh=session();await assert.rejects(()=>stage(fresh,'Package'),/SUCCESSFUL_BUILD_SESSION_REQUIRED/);assert.equal(json(fresh).attempts.length,0);
 const receipt=path.join(workspace,'Build/Validation/DeckyUpdate/BUILD.json');const value=json(receipt);value.fixtureBudgetSession=fresh;fs.writeFileSync(receipt,JSON.stringify(value));
 await assert.rejects(()=>stage(original,'Package'),/BUILD_RECEIPT_CHANGED/);assert.equal(json(original).attempts.length,1);
});
test('the actual portable pwsh path also launches the owned guard without pwsh on PATH',{timeout:15000},async()=>{
 const pwsh=spawnSync('where.exe',['pwsh.exe'],{encoding:'utf8',windowsHide:true}).stdout.trim().split(/\r?\n/)[0];const file=session(),prior=process.env.PATH;
 process.env.PATH=path.join(process.env.SystemRoot,'System32');
 try{const result=await stage(file,'Build','normal',{originalCommand:{executable:pwsh,args:['-NoProfile','-ExecutionPolicy','Bypass','-File',gate,'-Stage','Build','-WorkspaceRoot',workspace],sourceInputs:[gate]}});assert.equal(result.exitCode,0);assert.equal(result.result.ownedJobVerified,true);}
 finally{process.env.PATH=prior;}
});
test('actual Build and Package callers stop immediately on a failing original gate',{timeout:15000},()=>{
 for(const [name,stageName] of [['build-workspace','Build'],['package-release','Package']]){
  const harness=path.join(own,name+'-failure-harness.ps1'),after=path.join(own,name+'-must-not-exist'),gateFile=path.join(own,'decky_update_release_gate.ps1');fs.writeFileSync(gateFile,'exit 124\n');
  fs.writeFileSync(harness,`$ErrorActionPreference='Stop'
$tokens=$null;$errors=$null;$ast=[System.Management.Automation.Language.Parser]::ParseFile('${q(path.join(repo,'tools',name+'.ps1'))}',[ref]$tokens,[ref]$errors)
$call=$ast.Find({param($n) $n -is [System.Management.Automation.Language.CommandAst] -and $n.Extent.Text.Contains("'decky_update_release_gate.ps1'") -and $n.Extent.Text.Contains('-Stage ${stageName}')},$true)
$block=$call.Parent.Parent
$script=$block.Statements|ForEach-Object{$_.Extent.Text}
$original=Join-Path $PSScriptRoot 'caller-actual-${stageName}.ps1'
[IO.File]::WriteAllText($original,('$WorkspaceRoot=$PSScriptRoot'+[Environment]::NewLine+'$packagePath="candidate.zip"'+[Environment]::NewLine+($script -join [Environment]::NewLine)+[Environment]::NewLine+"'should-not-run'|Set-Content -LiteralPath '${q(after)}'"))
& $original
`);
  const result=spawnSync('pwsh.exe',['-NoProfile','-File',harness],{encoding:'utf8',timeout:12000,windowsHide:true});assert.notEqual(result.status,0);assert.match(result.stderr+result.stdout,/FULL_RELEASE_(BUILD|PACKAGE)_FIXTURE_FAILED exit=124/);assert.equal(fs.existsSync(after),false);
 }
});


// Existing hosted pnpm regressions use an inert, owned command terminal here.
// No GitHub account provisioning, real updater, registry or device operation runs.
let terminal,pnpmCommand,terminalScript,callsFile;
const terminalCalls=()=>fs.existsSync(callsFile)?fs.readFileSync(callsFile,'utf8').trim().split(/\r?\n/).filter(Boolean).map(JSON.parse):[];
async function hosted(file,testName,mode='normal'){
 const previous=process.env.YMCC_FULL_BUDGET_TEST_MODE;process.env.YMCC_FULL_BUDGET_TEST_MODE=mode;
 try{return await runHostedReleaseRegression({test:testName,workspaceRoot:workspace,pnpmCommand,sessionFile:file,repoRoot:repo,coreRoot:core});}
 finally{if(previous===undefined)delete process.env.YMCC_FULL_BUDGET_TEST_MODE;else process.env.YMCC_FULL_BUDGET_TEST_MODE=previous;}
}
function hostedCallerFragment(which){
 const lines=fs.readFileSync(path.join(repo,'tools/invoke-hosted-release-gate.ps1'),'utf8').split(/\r?\n/);
 const index=which==='before-package'?lines.findIndex(line=>line.includes("foreach($test in @('test:updater'")):lines.findIndex(line=>line.includes('Invoke-YmccFullReleaseRegression')&&line.includes("-Test 'test:updater-install-chain'"));
 assert.ok(index>=0,'actual hosted caller fragment missing');return which==='before-package'?lines[index]:lines.slice(index,index+2).join('\n');
}
function runHostedCaller(which){
 const script=path.join(own,'hosted-caller-'+which+'.ps1');fs.writeFileSync(script,`$ErrorActionPreference='Stop'
$repo='${q(repo)}';$env:YEMAN_WORKSPACE_ROOT='${q(workspace)}';$env:YMCC_RELEASE_FIXTURE_SESSION=$null
. '${q(path.join(repo,'tools/ymcc_sim/optimization/release-full-budget.ps1'))}'
${hostedCallerFragment(which)}
'after-original-hosted-loop'|Set-Content -LiteralPath '${q(path.join(own,'after-hosted-loop.txt'))}'
`);
 return spawnSync('pwsh.exe',['-NoProfile','-File',script],{encoding:'utf8',timeout:60000,windowsHide:true,env:{...process.env,PATH:terminal+path.delimiter+process.env.PATH}});
}
test('the actual hosted caller charges all eight existing regressions to Build and Package without fresh quotas',{timeout:70000},async()=>{
 const file=session(50000);const built=await stage(file,'Build');assert.equal(built.exitCode,0);const before=built.session.spentMs,count=terminalCalls().length;
 const first=runHostedCaller('before-package');assert.equal(first.status,0,first.stderr+first.stdout);
 let current=readFullReleaseSession(file,{repoRoot:repo,coreRoot:core}).session;assert.equal(current.attempts.length,8);assert.ok(current.spentMs>before);
 await assert.rejects(()=>hosted(file,'test:updater-install-chain'),/SUCCESSFUL_PACKAGE_SESSION_REQUIRED/);
 const packaged=await stage(file,'Package');assert.equal(packaged.exitCode,0);
 const last=runHostedCaller('after-package');assert.equal(last.status,0,last.stderr+last.stdout);
 current=readFullReleaseSession(file,{repoRoot:repo,coreRoot:core}).session;
 assert.equal(current.attempts.length,10);assert.equal(current.status,'COMPLETE');
 assert.deepEqual(current.attempts.map(a=>a.stage),['Build',...Object.values(HOSTED_REGRESSION_STAGES).slice(0,7),'Package','HostedUpdaterInstallChain']);
 assert.deepEqual(terminalCalls().slice(count).map(row=>row.test),Object.keys(HOSTED_REGRESSION_STAGES));
 let charged=0;for(const attempt of current.attempts){assert.equal(attempt.exitCode,0);assert.ok(attempt.remainingAtStartMs<=Math.floor(current.budgetMs-charged));charged+=attempt.wallMs;}
 assert.equal(current.spentMs,charged);await assert.rejects(()=>hosted(file,'test:updater'),/ALREADY_ATTEMPTED/);
});
test('a hosted pnpm failure stops the original loop and blocks later packaging',{timeout:20000},async()=>{
 const file=session();assert.equal((await stage(file,'Build')).exitCode,0);const marker=path.join(own,'after-hosted-loop.txt');if(fs.existsSync(marker))fs.unlinkSync(marker);
 const previous=process.env.YMCC_FULL_BUDGET_TEST_MODE;process.env.YMCC_FULL_BUDGET_TEST_MODE='fail';let failed;
 try{failed=runHostedCaller('before-package');}finally{if(previous===undefined)delete process.env.YMCC_FULL_BUDGET_TEST_MODE;else process.env.YMCC_FULL_BUDGET_TEST_MODE=previous;}
 assert.notEqual(failed.status,0);assert.match(failed.stdout+failed.stderr,/Release regression failed: test:updater/);assert.equal(fs.existsSync(marker),false);
 const current=readFullReleaseSession(file,{repoRoot:repo,coreRoot:core}).session;assert.equal(current.status,'FAILED');assert.equal(current.attempts.length,2);assert.equal(current.attempts[1].exitCode,7);
 await assert.rejects(()=>stage(file,'Package'),/NOT_RESUMABLE/);
});
test('hosted regression timeout uses Build remaining budget and destroys the command and grandchild',{timeout:15000},async()=>{
 const file=session(6500),started=performance.now();const built=await stage(file,'Build');assert.equal(built.exitCode,0);
 const result=await hosted(file,'test:updater','hang');assert.equal(result.exitCode,124);assert.equal(result.session.status,'EXHAUSTED');assert.equal(result.result.ownedJob.jobDisposed,true);assert.ok(result.result.budgetMs<6500-built.session.spentMs);
 assert.ok(performance.now()-started<9000);const pids=json(path.join(terminal,'hang.json'));assert.throws(()=>process.kill(pids.pid,0));assert.throws(()=>process.kill(pids.childPid,0));
 await assert.rejects(()=>stage(file,'Package'),/NOT_RESUMABLE/);
});
test('fixed hosted tests need the original successful Build binding and cannot select arbitrary commands',{timeout:20000},async()=>{
 const file=session(),before=terminalCalls().length;await assert.rejects(()=>hosted(file,'test:updater'),/SUCCESSFUL_BUILD_SESSION_REQUIRED/);assert.equal(json(file).attempts.length,0);
 for(const name of ['test:unknown','__proto__','constructor','test:updater; whoami'])assert.throws(()=>validateHostedRegression(name,{repoRoot:repo,workspaceRoot:workspace,pnpmCommand}),/HOSTED_REGRESSION_INVALID/);
 assert.equal((await stage(file,'Build')).exitCode,0);const other=session();await assert.rejects(()=>hosted(other,'test:updater'),/SUCCESSFUL_BUILD_SESSION_REQUIRED/);
 const receipt=path.join(workspace,'Build/Validation/DeckyUpdate/BUILD.json');fs.appendFileSync(receipt,' ');await assert.rejects(()=>hosted(file,'test:updater'),/BUILD_RECEIPT_CHANGED/);assert.equal(terminalCalls().length,before);
});

const closingCase='shared release quota rejects closing overrun while preserving the inner successful receipt';
availableCases.add(closingCase);
if(CaseFilter?.includes(closingCase))enqueueTest(closingCase,{timeout:10000},async()=>{
 const sessionFile=createFullReleaseSession({repoRoot:repo,coreRoot:own,budgetMs:3000});
 const command=path.join(own,'closing-inert.mjs');fs.writeFileSync(command,"console.log('INERT_STAGE_EXIT_ZERO');\n");
 const originalRead=fs.readFileSync;let delayed=false;const delayMs=3000;
 fs.readFileSync=function(file,...args){
  const result=originalRead.call(this,file,...args);
  if(!delayed&&typeof file==='string'&&path.basename(file)==='execution-budget.json'&&path.relative(path.dirname(sessionFile),file).startsWith('lighting'+path.sep)){
   delayed=true;Atomics.wait(new Int32Array(new SharedArrayBuffer(4)),0,0,delayMs);
  }
  return result;
 };
 let result;try{result=await runFullReleaseFixtureStage({stage:'Lighting',workspaceRoot:workspace,sessionFile,repoRoot:repo,coreRoot:own,gateEntry:command,originalCommand:{executable:process.execPath,args:[command],sourceInputs:[command]}});}
 finally{fs.readFileSync=originalRead;}
 const attempt=result.session.attempts.at(-1),receiptBytes=fs.readFileSync(attempt.budgetReceipt),receipt=JSON.parse(receiptBytes),receiptSha=crypto.createHash('sha256').update(receiptBytes).digest('hex');
 fs.writeFileSync(path.join(own,'closing-overrun.raw.json'),JSON.stringify({delayed,delayMs,result,receipt,receiptSha},null,2)+'\n');
 assert.equal(delayed,true);assert.equal(receiptSha,attempt.budgetReceiptSha256);
 assert.equal(receipt.exitCode,0);assert.equal(receipt.timedOut,false);assert.equal(receipt.processExitCode,0);assert.equal(receipt.ownedJob.deadlineExpired,false);assert.equal(receipt.ownedJob.jobDisposed,true);
 assert.equal(result.result.exitCode,0);assert.equal(result.result.timedOut,false);
 assert.equal(result.session.status,'EXHAUSTED');assert.ok(result.session.spentMs>=result.session.budgetMs);
 assert.equal(result.exitCode,124);assert.equal(result.status,'TIMEOUT');assert.equal(result.timeoutScope,'shared-release-budget');
 assert.equal(attempt.exitCode,124);assert.equal(attempt.status,'TIMEOUT');assert.equal(attempt.innerExitCode,0);assert.equal(attempt.innerStatus,'ORIGINAL_GATE_EXITED');assert.equal(attempt.timeoutScope,'shared-release-budget');
 assert.equal(json(sessionFile).attempts.at(-1).exitCode,124);
});
test('actual full stage selects the existing raw launcher and restores environment on success timeout and failure',{timeout:7000},()=>{
 const script=path.join(own,'raw-launcher-scope.ps1'),outFile=path.join(own,'raw-launcher-scope.json');
 fs.writeFileSync(script,`$ErrorActionPreference='Stop'
. '${q(path.join(repo,'tools/ymcc_sim/optimization/release-full-budget.ps1'))}'
function Get-YmccFullReleaseNode {return 'Invoke-InertStageNode'}
$script:calls=New-Object 'System.Collections.Generic.List[object]'
$script:mode='success'
function Invoke-InertStageNode {
 param([Parameter(ValueFromRemainingArguments=$true)][object[]]$NodeArgs)
 $script:calls.Add(@{launcher=$env:YMCC_SIM_RAW_PROGRAM_PWSH;arguments=@($NodeArgs);mode=$script:mode})|Out-Null
 if($script:mode-eq'failure'){$global:LASTEXITCODE=9;throw 'INERT_NODE_STAGE_FAILURE'}
 $global:LASTEXITCODE=if($script:mode-eq'timeout'){124}else{0}
}
$actual=(Get-Command pwsh.exe -CommandType Application -ErrorAction Stop|Select-Object -First 1).Source
$rows=New-Object 'System.Collections.Generic.List[object]'
foreach($preference in @('absent','configured')){
 foreach($mode in @('success','timeout','failure')){
  $old=if($preference-eq'configured'){'C:\INERT-explicit-tool\pwsh.exe'}else{$null}
  $env:YMCC_SIM_RAW_PROGRAM_PWSH=$old;$script:mode=$mode;$caught=$false
  try{Invoke-YmccFullReleaseFixtureStage -ProjectRoot '${q(repo)}' -Stage Build -WorkspaceRoot '${q(workspace)}'}catch{if($_.Exception.Message-ne'INERT_NODE_STAGE_FAILURE'){throw};$caught=$true}
  $exit=$LASTEXITCODE;$seen=$script:calls[$script:calls.Count-1]
  $rows.Add(@{preference=$preference;mode=$mode;before=$old;after=$env:YMCC_SIM_RAW_PROGRAM_PWSH;actualTool=$actual;seen=$seen;exitCode=$exit;caught=$caught})|Out-Null
 }
}
@{rows=@($rows.ToArray());calls=$script:calls.Count;gateInvocations=0;compiles=0}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath '${q(outFile)}' -Encoding UTF8
`);
 const run=spawnSync('pwsh.exe',['-NoLogo','-NoProfile','-NonInteractive','-File',script],{cwd:repo,encoding:'utf8',timeout:5500,windowsHide:true});assert.equal(run.status,0,run.stderr+run.stdout);
 const value=json(outFile);assert.equal(value.rows.length,6);assert.equal(value.calls,6);assert.equal(value.gateInvocations,0);
 for(const row of value.rows){assert.equal(row.after??null,row.before??null,'caller environment must restore');assert.equal(row.seen.launcher,row.preference==='configured'?row.before:row.actualTool,'full stage must pass the exact selected launcher');assert.equal(row.caught,row.mode==='failure');assert.equal(row.exitCode,row.mode==='timeout'?124:row.mode==='failure'?9:0);assert.ok(row.seen.arguments.includes('--stage'));assert.ok(row.seen.arguments.includes('Build'));}
});

test('bootstrap original Check stops later selected cases after required failure and keeps explicit collection',{timeout:6000},()=>{
 const script=path.join(own,'bootstrap-check-control.ps1'),outFile=path.join(own,'bootstrap-check-control.json');
 fs.writeFileSync(script,String.raw`
param([Parameter(Mandatory=$true)][string]$Source,[Parameter(Mandatory=$true)][string]$Result)
$ErrorActionPreference='Stop';$tokens=$null;$parseErrors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($Source,[ref]$tokens,[ref]$parseErrors)
if(@($parseErrors).Count){throw 'BOOTSTRAP_SOURCE_PARSE_REQUIRED'}
$nodes=@($ast.FindAll({param($n)$n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Check'},$true))
if($nodes.Count -ne 1){throw 'ORIGINAL_CHECK_FUNCTION_REQUIRED'}
. ([scriptblock]::Create($nodes[0].Extent.Text))
$rows=[Collections.Generic.List[object]]::new()
foreach($mode in @('normal','required-failure','expected-negative','collect','filtered','no-match')){
 $cases=[Collections.Generic.List[object]]::new();$trace=[Collections.Generic.List[string]]::new();$script:bootstrapRequiredFailure=$null;$CollectAfterFailure=($mode -eq 'collect');$CaseFilter=if($mode -eq 'filtered'){'selected'}elseif($mode -eq 'no-match'){'absent'}else{'*'}
 $observation=@{scope='INERT_OBSERVATION'}
 Check 'first' {try{$trace.Add('first');if($mode -in @('required-failure','collect')){throw 'INERT_REQUIRED_FAILURE'};if($mode -eq 'expected-negative'){try{throw 'INERT_EXPECTED_REJECTION'}catch{}}}finally{$trace.Add('first-finally')}} $observation
 Check 'selected' {$trace.Add('selected')}
 Check 'last' {$trace.Add('last');if($mode -eq 'collect'){throw 'INERT_SECOND_FAILURE'}}
 $expectedStatus=@('PASS','PASS','PASS');$expectedTrace=@('first','first-finally','selected','last')
 if($mode -eq 'required-failure'){$expectedStatus=@('FAIL','NOT_RUN_AFTER_REQUIRED_FAILURE','NOT_RUN_AFTER_REQUIRED_FAILURE');$expectedTrace=@('first','first-finally')}
 elseif($mode -eq 'collect'){$expectedStatus=@('FAIL','PASS','FAIL')}
 elseif($mode -eq 'filtered'){$expectedStatus=@('PASS');$expectedTrace=@('selected')}
 elseif($mode -eq 'no-match'){$expectedStatus=@();$expectedTrace=@()}
 $actualStatus=@($cases|ForEach-Object{$_.status})
 if(($actualStatus -join '|') -cne ($expectedStatus -join '|') -or ($trace.ToArray() -join '|') -cne ($expectedTrace -join '|')){throw ('BOOTSTRAP_REQUIRED_FAILURE_DID_NOT_STOP '+$mode+' statuses='+($actualStatus -join '|')+' trace='+($trace.ToArray() -join '|'))}
 if($mode -in @('required-failure','collect')){if($cases[0].error -cne 'INERT_REQUIRED_FAILURE' -or -not $cases[0].stack -or $cases[0].observation -ne $observation){throw 'ORIGINAL_FAILURE_OR_OBSERVATION_LOST'}}
 if($mode -eq 'required-failure'){foreach($row in @($cases|Where-Object status -eq 'NOT_RUN_AFTER_REQUIRED_FAILURE')){if($row.milliseconds -ne 0 -or $row.blockedBy -cne 'first'){throw 'UNEXECUTED_CASE_NOT_IDENTIFIED'}}}
 $rows.Add(@{name=$mode;passed=$true;statuses=$actualStatus;inertBodyEvents=@($trace.ToArray());actualFailures=@($cases|Where-Object status -eq FAIL).Count;notRun=@($cases|Where-Object status -eq NOT_RUN_AFTER_REQUIRED_FAILURE).Count})
}
$params=@($ast.ParamBlock.Parameters|ForEach-Object{$_.Name.VariablePath.UserPath});if($params -cnotcontains 'CollectAfterFailure'){throw 'EXPLICIT_COLLECTION_PARAMETER_REQUIRED'}
@{schemaVersion=1;scope='ACTUAL_ORIGINAL_CHECK_OVER_INERT_BODIES_ONLY';rows=@($rows.ToArray());businessRuns=0;compiles=0;sparseFilesCreated=0;fullRuns=0}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $Result -Encoding UTF8
`);
 const pwsh=process.env.YMCC_SIM_RAW_PROGRAM_PWSH||'pwsh.exe',run=spawnSync(pwsh,['-NoLogo','-NoProfile','-NonInteractive','-File',script,'-Source',path.join(repo,'tools/updater_bootstrap_recovery_selftest.ps1'),'-Result',outFile],{cwd:repo,encoding:'utf8',timeout:5000,windowsHide:true});
 fs.writeFileSync(path.join(own,'bootstrap-check-control.stdout'),run.stdout??'');fs.writeFileSync(path.join(own,'bootstrap-check-control.stderr'),run.stderr??'');
 assert.equal(run.status,0,run.stderr+run.stdout);const value=json(outFile);assert.deepEqual(value.rows.map(row=>row.name),['normal','required-failure','expected-negative','collect','filtered','no-match']);assert(value.rows.every(row=>row.passed));assert.equal(value.businessRuns,0);assert.equal(value.compiles,0);assert.equal(value.sparseFilesCreated,0);assert.equal(value.fullRuns,0);
 console.log('YMCC_BOOTSTRAP_FAILFAST_CONTROL_RAW '+JSON.stringify(value));
});
test('below-cap closing checkpoint charges prior finalization and preserves original errors',{timeout:8000},async()=>{
 const rows=[];
 for(const mode of ['success','failure','original-error']){
  const sessionFile=createFullReleaseSession({repoRoot:repo,coreRoot:own,budgetMs:10000}),seed=json(sessionFile);seed.spentMs=123.5;fs.writeFileSync(sessionFile,JSON.stringify(seed));
  const command=path.join(own,'below-cap-'+mode+'.mjs');fs.writeFileSync(command,"console.log('INERT_CLOSING_CONTROL');process.exitCode="+(mode==='failure'?7:0)+";\n");
  const originalWrite=fs.writeFileSync,priorSession=process.env.YMCC_RELEASE_FIXTURE_SESSION,priorAttempt=process.env.YMCC_RELEASE_FIXTURE_ATTEMPT,checkpoints=[];
  let firstWriteMs=0,result,error;
  fs.writeFileSync=function(file,value,...args){
   if(typeof file==='string'&&file.startsWith(sessionFile+'.')&&file.endsWith('.tmp')){
    const checkpoint=JSON.parse(value);
    if(checkpoint.activeAttempt===null){
     assert.equal(fs.existsSync(sessionFile+'.lock'),true,'closing writes must remain locked');
     checkpoints.push(checkpoint);
     if(mode==='original-error'&&checkpoints.length>1)throw Error('CLOSING_WRITE_MUST_NOT_REPLACE_ORIGINAL_ERROR');
     const started=performance.now(),written=originalWrite.call(this,file,value,...args);if(checkpoints.length===1)firstWriteMs=performance.now()-started;return written;
    }
   }
   return originalWrite.call(this,file,value,...args);
  };
  try{result=await runFullReleaseFixtureStage({stage:mode==='original-error'?'Build':'Lighting',workspaceRoot:workspace,sessionFile,repoRoot:repo,coreRoot:own,gateEntry:command,originalCommand:{executable:process.execPath,args:[command],sourceInputs:[command]}});}catch(caught){error=caught;}
  finally{fs.writeFileSync=originalWrite;}
  const saved=json(sessionFile),attempt=saved.attempts.at(-1),receiptBytes=fs.readFileSync(attempt.budgetReceipt),receipt=JSON.parse(receiptBytes);
  const row={mode,checkpoints,firstWriteMs,saved,receipt,error:error?.message,scope:'INERT_COMMAND_ONLY'};rows.push(row);fs.writeFileSync(path.join(own,'below-cap-closing.raw.json'),JSON.stringify(rows,null,2)+'\n');
  assert.equal(fs.writeFileSync,originalWrite);assert.equal(fs.existsSync(sessionFile+'.lock'),false);assert.equal(process.env.YMCC_RELEASE_FIXTURE_SESSION,priorSession);assert.equal(process.env.YMCC_RELEASE_FIXTURE_ATTEMPT,priorAttempt);
  assert.equal(crypto.createHash('sha256').update(receiptBytes).digest('hex'),attempt.budgetReceiptSha256);assert.equal(receipt.timedOut,false);assert.equal(receipt.ownedJob.jobDisposed,true);assert.equal(saved.activeAttempt,null);
  assert.ok(saved.spentMs<saved.budgetMs);assert.equal(saved.spentMs,123.5+attempt.wallMs);
  if(mode==='original-error'){
   assert.equal(error?.code,'ENOENT');assert.match(error.message,/BUILD\.json/);assert.equal(checkpoints.length,1);assert.equal(attempt.exitCode,3);assert.equal(attempt.status,'FAILED');assert.equal(saved.status,'FAILED');assert.equal(attempt.error,error.message);
  }else{
   assert.ifError(error);assert.equal(checkpoints.length,2,'below-cap completion must persist the closing checkpoint');
   const previousWall=checkpoints[0].attempts.at(-1).wallMs;
   assert.ok(firstWriteMs>0);assert.ok(attempt.wallMs>=previousWall+firstWriteMs,'final checkpoint must include the preceding actual session write');
   assert.ok(Date.parse(attempt.finishedAtUtc)>=Date.parse(checkpoints[0].attempts.at(-1).finishedAtUtc));
   assert.deepEqual(saved,result.session);assert.equal(result.exitCode,mode==='failure'?7:0);assert.equal(attempt.exitCode,result.exitCode);assert.equal(attempt.status,mode==='failure'?'FAILED':'ORIGINAL_GATE_EXITED');assert.equal(saved.status,mode==='failure'?'FAILED':'OPEN');assert.equal(attempt.timeoutScope,undefined);
   assert.equal(receipt.exitCode,result.exitCode);assert.equal(receipt.processExitCode,result.exitCode);
  }
 }
 console.log('YMCC_BELOW_CAP_CLOSING_RAW '+JSON.stringify({modes:rows.map(row=>row.mode),rows,actualBusinessRuns:0,compiles:0,fullRuns:0,fullProductAcceptance:false}));
});
if(CaseFilter!==null&&CaseFilter.some(name=>!availableCases.has(name)))throw Error('FULL_BUDGET_CASE_FILTER_UNKNOWN');
prepareControlFixture();
for(const args of pendingTests)nodeTest(...args);
