// Full-release stages reuse the original fixture guard and one cumulative budget.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {performance} from 'node:perf_hooks';
import {fileURLToPath} from 'node:url';
import {runWithWallBudget,validateWallBudget,MAX_WALL_BUDGET_MS} from './execution-budget.mjs';
import {simulationCorePath} from '../global/release-host-environment.mjs';
const DEFAULT_REPO=path.resolve(import.meta.dirname,'../../..');
export const HOSTED_REGRESSION_STAGES=Object.freeze({
 'test:updater':'HostedUpdater',
 'test:updater-manager-resume':'HostedUpdaterManagerResume',
 'test:updater-resume':'HostedUpdaterResume',
 'test:updater-retry':'HostedUpdaterRetry',
 'test:updater-rollback':'HostedUpdaterRollback',
 'test:fan-host-paths':'HostedFanHostPaths',
 'test:fan-payload':'HostedFanPayload',
 'test:updater-install-chain':'HostedUpdaterInstallChain',
});
const HOSTED_STAGES=Object.values(HOSTED_REGRESSION_STAGES);
const STAGES=['Lighting','Build','Package','Verify',...HOSTED_STAGES];
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>{const tmp=file+'.'+crypto.randomUUID()+'.tmp';fs.writeFileSync(tmp,JSON.stringify(value,null,2)+'\n');fs.renameSync(tmp,file);};
const key=file=>path.resolve(file).toLowerCase();
const inside=(root,file)=>{const r=path.relative(root,file);return r!==''&&!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep);};
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function boundedSessionFile(file,coreRoot){
 const core=fs.realpathSync(coreRoot),actual=fs.realpathSync(file);
 if(!inside(core,actual)||fs.statSync(actual).size>1024*1024)throw Error('FULL_RELEASE_SESSION_SCOPE_INVALID');
 return actual;
}
export function readFullReleaseSession(file,{repoRoot=DEFAULT_REPO,coreRoot=simulationCorePath()}={}){
 const actual=boundedSessionFile(file,coreRoot),session=read(actual);
 if(session.schemaVersion!==1||session.kind!=='ymcc-full-release-fixture-budget'||key(session.repoRoot)!==key(repoRoot)||key(session.coreRoot)!==key(fs.realpathSync(coreRoot))||!Number.isFinite(session.spentMs)||session.spentMs<0||!Array.isArray(session.attempts)||session.attempts.length>STAGES.length||!['OPEN','COMPLETE','RUNNING','FAILED','EXHAUSTED'].includes(session.status))throw Error('FULL_RELEASE_SESSION_INVALID');
 validateWallBudget(session.budgetMs);
 return {file:actual,session};
}
export function createFullReleaseSession({repoRoot=DEFAULT_REPO,coreRoot=simulationCorePath(),budgetMs=MAX_WALL_BUDGET_MS}={}){
 validateWallBudget(budgetMs);const core=fs.realpathSync(coreRoot),parent=path.join(core,'Evidence/Build/full-release-budget');
 fs.mkdirSync(parent,{recursive:true});if(!inside(core,fs.realpathSync(parent)))throw Error('FULL_RELEASE_SESSION_SCOPE_INVALID');
 const root=fs.mkdtempSync(path.join(parent,'session-')),file=path.join(root,'SESSION.json');
 write(file,{schemaVersion:1,kind:'ymcc-full-release-fixture-budget',sessionId:crypto.randomUUID(),repoRoot:path.resolve(repoRoot),coreRoot:core,budgetMs,spentMs:0,status:'OPEN',createdAtUtc:new Date().toISOString(),workspaceRoot:null,attempts:[],fullProductAcceptance:false});
 return file;
}
function buildReceiptFile(workspaceRoot){return path.join(workspaceRoot,'Build/Validation/DeckyUpdate/BUILD.json');}
function validateBuildSessionBinding(session,file,workspaceRoot){
 const built=session.attempts.find(a=>a.stage==='Build'&&a.status==='ORIGINAL_GATE_EXITED'&&a.exitCode===0);
 if(!built?.originalBuildReceipt)throw Error('FULL_RELEASE_SUCCESSFUL_BUILD_SESSION_REQUIRED');
 const receiptFile=buildReceiptFile(workspaceRoot),binding=built.originalBuildReceipt;
 if(key(binding.path)!==key(receiptFile)||hash(receiptFile)!==binding.sha256)throw Error('FULL_RELEASE_BUILD_RECEIPT_CHANGED');
 const receipt=read(receiptFile);
 if(receipt.passed!==true||key(receipt.fixtureBudgetSession)!==key(file)||receipt.fixtureBudgetAttempt!==built.id)throw Error('FULL_RELEASE_BUILD_SESSION_CHANGED');
 return built;
}
export async function runFullReleaseFixtureStage({stage,workspaceRoot,candidatePackage='',lifecycleEvidenceRoot='',sessionFile,repoRoot=DEFAULT_REPO,coreRoot=simulationCorePath(),gateEntry=path.join(repoRoot,'tools/decky_update_release_gate.ps1'),originalCommand}={}){
 const started=performance.now();if(!STAGES.includes(stage)||!workspaceRoot||!path.isAbsolute(workspaceRoot))throw Error('FULL_RELEASE_STAGE_INPUT_INVALID');
 const bound=readFullReleaseSession(sessionFile,{repoRoot,coreRoot}),lock=bound.file+'.lock';let fd;
 try{fd=fs.openSync(lock,'wx');}catch{throw Error('FULL_RELEASE_SESSION_BUSY_OR_INTERRUPTED');}
 let completedStage;
 const session=bound.session,priorSession=process.env.YMCC_RELEASE_FIXTURE_SESSION,priorAttempt=process.env.YMCC_RELEASE_FIXTURE_ATTEMPT;
 try{
  // Read again after locking; a previous stage may have completed before this lock.
  const fresh=readFullReleaseSession(bound.file,{repoRoot,coreRoot}).session;Object.assign(session,fresh);
  if(!['OPEN','COMPLETE'].includes(session.status)||session.activeAttempt)throw Error('FULL_RELEASE_SESSION_NOT_RESUMABLE');
  if(session.attempts.some(a=>a.stage===stage))throw Error('FULL_RELEASE_STAGE_ALREADY_ATTEMPTED');
  if(session.workspaceRoot&&key(session.workspaceRoot)!==key(workspaceRoot))throw Error('FULL_RELEASE_WORKSPACE_CHANGED');
  if(stage==='Package'||stage==='Verify'||HOSTED_STAGES.includes(stage))validateBuildSessionBinding(session,bound.file,workspaceRoot);
  if(HOSTED_STAGES.includes(stage)&&!originalCommand)throw Error('FULL_RELEASE_FIXED_REGRESSION_COMMAND_REQUIRED');
  if(stage==='HostedUpdaterInstallChain'&&!session.attempts.some(a=>a.stage==='Package'&&a.status==='ORIGINAL_GATE_EXITED'&&a.exitCode===0))throw Error('FULL_RELEASE_SUCCESSFUL_PACKAGE_SESSION_REQUIRED');
  let remaining=Math.floor(session.budgetMs-session.spentMs-(performance.now()-started));
  if(remaining<1000){session.status='EXHAUSTED';write(bound.file,session);throw Error('FULL_RELEASE_BUDGET_EXHAUSTED');}
  const attempt={id:crypto.randomUUID(),stage,status:'RUNNING',startedAtUtc:new Date().toISOString(),remainingAtStartMs:remaining,workspaceRoot:path.resolve(workspaceRoot),candidatePackage:candidatePackage?path.resolve(candidatePackage):'',lifecycleEvidenceRoot:lifecycleEvidenceRoot?path.resolve(lifecycleEvidenceRoot):'',gateEntry:path.resolve(gateEntry),gateSha256:hash(gateEntry)};
  session.workspaceRoot=attempt.workspaceRoot;session.status='RUNNING';session.activeAttempt=attempt;session.attempts.push(attempt);write(bound.file,session);
  process.env.YMCC_RELEASE_FIXTURE_SESSION=bound.file;process.env.YMCC_RELEASE_FIXTURE_ATTEMPT=attempt.id;
  const executable=path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe');
  const args=['-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',gateEntry,'-Stage',stage,'-WorkspaceRoot',attempt.workspaceRoot];
  if(candidatePackage)args.push('-CandidatePackage',attempt.candidatePackage);if(lifecycleEvidenceRoot)args.push('-LifecycleEvidenceRoot',attempt.lifecycleEvidenceRoot);
  let guarded;
  try{
   remaining=Math.floor(session.budgetMs-session.spentMs-(performance.now()-started));if(originalCommand?.maxWallMs)remaining=Math.min(remaining,validateWallBudget(originalCommand.maxWallMs));validateWallBudget(remaining);
   guarded=await runWithWallBudget([],{repoRoot,coreRoot,budgetMs:remaining,outDir:path.join(path.dirname(bound.file),stage.toLowerCase()),powershell:originalCommand?.executable&&path.basename(originalCommand.executable).toLowerCase()==='pwsh.exe'?originalCommand.executable:'pwsh.exe',command:originalCommand??{executable,args,sourceInputs:[gateEntry]},additionalInputs:[fileURLToPath(import.meta.url),path.join(repoRoot,'tools/ymcc_sim/optimization/release-full-budget.ps1'),(originalCommand?.executable??executable),process.execPath]});
   attempt.status=guarded.result.timedOut?'TIMEOUT':guarded.exitCode===0?'ORIGINAL_GATE_EXITED':'FAILED';attempt.exitCode=guarded.exitCode;attempt.budgetReceipt=path.join(guarded.result.runDir,'execution-budget.json');attempt.budgetReceiptSha256=hash(attempt.budgetReceipt);
   if(stage==='Build'&&guarded.exitCode===0){const file=buildReceiptFile(attempt.workspaceRoot),receipt=read(file);if(receipt.passed!==true||key(receipt.fixtureBudgetSession)!==key(bound.file)||receipt.fixtureBudgetAttempt!==attempt.id)throw Error('FULL_RELEASE_BUILD_WORKER_RECEIPT_REQUIRED');attempt.originalBuildReceipt={path:file,sha256:hash(file)};}
  }catch(error){attempt.status='FAILED';attempt.exitCode=3;attempt.error=error.message;throw error;}
  finally{
   attempt.wallMs=performance.now()-started;attempt.finishedAtUtc=new Date().toISOString();session.spentMs+=attempt.wallMs;session.activeAttempt=null;
   session.status=attempt.status==='TIMEOUT'||session.spentMs>=session.budgetMs?'EXHAUSTED':attempt.exitCode!==0?'FAILED':session.attempts.some(a=>a.stage==='Package')?'COMPLETE':'OPEN';
   write(bound.file,session);
  }
  completedStage={...guarded,sessionFile:bound.file,session};
 }finally{
  if(priorSession===undefined)delete process.env.YMCC_RELEASE_FIXTURE_SESSION;else process.env.YMCC_RELEASE_FIXTURE_SESSION=priorSession;
  if(priorAttempt===undefined)delete process.env.YMCC_RELEASE_FIXTURE_ATTEMPT;else process.env.YMCC_RELEASE_FIXTURE_ATTEMPT=priorAttempt;
  // Keep the session locked while classifying closing work against its shared quota.
  try{if(completedStage){
   const attempt=session.attempts.at(-1),finalWallMs=performance.now()-started;
   const finalSpentMs=session.spentMs-attempt.wallMs+finalWallMs;
   if(finalSpentMs>=session.budgetMs){
    attempt.innerExitCode=attempt.exitCode;attempt.innerStatus=attempt.status;
    attempt.status='TIMEOUT';attempt.exitCode=124;attempt.timeoutScope='shared-release-budget';
    session.status='EXHAUSTED';
    completedStage={...completedStage,exitCode:124,status:'TIMEOUT',timeoutScope:attempt.timeoutScope};
   }
   attempt.wallMs=finalWallMs;attempt.finishedAtUtc=new Date().toISOString();
   session.spentMs=finalSpentMs;write(bound.file,session);
  }
  }finally{fs.closeSync(fd);fs.unlinkSync(lock);}
 }
 return completedStage;
}
// Hosted preparation preserves its existing fixed pnpm scripts, charged to the
// successful Build's session. This is not an arbitrary command or test selector.
export function validateHostedRegression(test,{repoRoot=DEFAULT_REPO,workspaceRoot,pnpmCommand}={}){
 if(typeof test!=='string'||!Object.hasOwn(HOSTED_REGRESSION_STAGES,test)||!workspaceRoot||!path.isAbsolute(workspaceRoot))throw Error('FULL_RELEASE_HOSTED_REGRESSION_INVALID');
 const packageFile=path.join(repoRoot,'package.json'),script=read(packageFile).scripts?.[test];
 if(typeof script!=='string'||!script.trim())throw Error('FULL_RELEASE_HOSTED_SCRIPT_MISSING');
 if(typeof pnpmCommand!=='string'||!path.isAbsolute(pnpmCommand)||path.basename(pnpmCommand).toLowerCase()!=='pnpm.cmd'||!fs.lstatSync(pnpmCommand).isFile()||fs.lstatSync(pnpmCommand).isSymbolicLink())throw Error('FULL_RELEASE_HOSTED_PNPM_INVALID');
 const entry=path.join(repoRoot,'tools/ymcc_sim/optimization/release-hosted-regression.ps1');
 const executable=path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe');
 return {stage:HOSTED_REGRESSION_STAGES[test],entry,command:{executable,args:['-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',entry,'-ProjectRoot',path.resolve(repoRoot),'-WorkspaceRoot',path.resolve(workspaceRoot),'-Test',test,'-PnpmCommand',path.resolve(pnpmCommand)],sourceInputs:[entry,packageFile,path.resolve(pnpmCommand)]}};
}
export async function runHostedReleaseRegression({test,workspaceRoot,pnpmCommand,sessionFile,repoRoot=DEFAULT_REPO,coreRoot=simulationCorePath()}={}){
 if(!sessionFile)throw Error('FULL_RELEASE_SUCCESSFUL_BUILD_SESSION_REQUIRED');
 const selected=validateHostedRegression(test,{repoRoot,workspaceRoot,pnpmCommand});
 return runFullReleaseFixtureStage({stage:selected.stage,workspaceRoot,sessionFile,repoRoot,coreRoot,gateEntry:selected.entry,originalCommand:selected.command});
}
// Only the existing lighting consumer command is accepted; no arbitrary command route.
export function validateLightingRequest(request,{repoRoot=DEFAULT_REPO}={}){
 const entry=path.join(repoRoot,'tools/ymcc_sim/ymcc_sim.ps1'),a=request?.arguments;
 if(request?.schemaVersion!==1||request.kind!=='ymcc-lighting-release-fixture-request'||typeof request.workspaceRoot!=='string'||!path.isAbsolute(request.workspaceRoot)||!Array.isArray(a)||a.length!==21||a.some(v=>typeof v!=='string'||v.includes('\0')))throw Error('FULL_RELEASE_LIGHTING_REQUEST_INVALID');
 const fixed=new Map([[0,'-NoProfile'],[1,'-File'],[3,'-Domain'],[4,'global'],[5,'-Mode'],[6,'run'],[7,'-Tests'],[8,'lighting.signal-consumer'],[9,'-LightingPayloadRoot'],[11,'-ExpectedHelperSha256'],[13,'-RuntimeRoot'],[15,'-SharedRuntimeRoot'],[17,'-HcProfile'],[18,'ymcc-device-core-v1'],[19,'-EvidenceDir']]);
 if([...fixed].some(([i,v])=>a[i]!==v)||key(a[2])!==key(entry)||!path.isAbsolute(a[2])||!/^[0-9a-f]{64}$/i.test(a[12]))throw Error('FULL_RELEASE_LIGHTING_COMMAND_INVALID');
 for(const i of [10,14,16,20])if(!path.isAbsolute(a[i]))throw Error('FULL_RELEASE_LIGHTING_PATH_REQUIRED');
 if(!inside(request.workspaceRoot,a[10])||!inside(request.workspaceRoot,a[20])||key(a[14])!==key(path.join(repoRoot,'PowerControl/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1'))||key(a[16])!==key(path.join(repoRoot,'PowerControl/ymcc-shared-runtime')))throw Error('FULL_RELEASE_LIGHTING_SCOPE_INVALID');
 const executable=request.executable;
 if(typeof executable!=='string'||!path.isAbsolute(executable)||path.basename(executable).toLowerCase()!=='pwsh.exe'||!fs.existsSync(executable))throw Error('FULL_RELEASE_LIGHTING_PWSH_REQUIRED');
 return {entry,workspaceRoot:request.workspaceRoot,sessionFile:request.sessionFile,command:{executable,args:a,sourceInputs:[entry,path.join(repoRoot,'tools/build-lightsetter-payload.ps1')],maxWallMs:240000}};
}
// One bounded original publish call, including product preparation/build/package.
// The generated script only imports the pinned original function and fixed args;
// it adds no public worker flag, inherited bypass token, or second session creator.
export async function runFullReleasePublishInvocation(requestFile,{repoRoot=DEFAULT_REPO,coreRoot=simulationCorePath(),powershell='pwsh.exe'}={}){
 const began=performance.now(),file=path.resolve(requestFile),core=fs.realpathSync(coreRoot);
 if(!inside(core,file)||key(fs.realpathSync(file))!==key(file)||fs.statSync(file).size>65536||path.basename(file)!=='REQUEST.json')throw Error('FULL_PUBLISH_REQUEST_SCOPE_INVALID');
 let cursor=path.dirname(file);while(inside(core,cursor)){if(key(fs.realpathSync(cursor))!==key(cursor))throw Error('FULL_PUBLISH_REQUEST_REPARSE');cursor=path.dirname(cursor);}
 const request=read(file),expected=['schemaVersion','kind','repoRoot','coreRoot','workspaceRoot','deployInstalled','budgetSeconds','startedAtUnixMs','ownerPid','ownerStartTicks'];
 if(Object.keys(request).sort().join('|')!==expected.sort().join('|')||request.schemaVersion!==1||request.kind!=='ymcc-full-publish-original-body-v1'||key(request.repoRoot)!==key(repoRoot)||key(request.coreRoot)!==key(core)||typeof request.workspaceRoot!=='string'||request.workspaceRoot&&!path.isAbsolute(request.workspaceRoot)||typeof request.deployInstalled!=='boolean'||!Number.isSafeInteger(request.budgetSeconds)||request.budgetSeconds<1||request.budgetSeconds>1800||!Number.isSafeInteger(request.startedAtUnixMs)||!Number.isSafeInteger(request.ownerPid)||request.ownerPid<=0||typeof request.ownerStartTicks!=='string'||!/^\d{10,20}$/.test(request.ownerStartTicks))throw Error('FULL_PUBLISH_REQUEST_INVALID');
 const deadline=request.startedAtUnixMs+request.budgetSeconds*1000;
 if(request.startedAtUnixMs>Date.now()||deadline<=Date.now())throw Error('FULL_RELEASE_BUDGET_EXHAUSTED');
 const usedFile=file+'.started';const claim=fs.openSync(usedFile,'wx');fs.closeSync(claim);
 const root=path.dirname(file),publish=path.join(repoRoot,'tools/publish.ps1'),publishSha=hash(publish),requestSha=hash(file),worker=path.join(root,'Invoke-OriginalPublish.ps1');
 const quote=value=>"'"+value.replaceAll("'","''")+"'";
 const body=`param([string]$RequestFile,[string]$RequestSha256)\n$ErrorActionPreference='Stop'\n[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)\n$OutputEncoding=[Text.UTF8Encoding]::new($false)\nif((Get-FileHash -LiteralPath $RequestFile -Algorithm SHA256).Hash.ToLowerInvariant() -cne $RequestSha256){throw 'FULL_PUBLISH_REQUEST_CHANGED'}\n$request=Get-Content -LiteralPath $RequestFile -Raw -Encoding UTF8|ConvertFrom-Json\n$owner=[Diagnostics.Process]::GetProcessById($request.ownerPid)\ntry{if($owner.StartTime.ToUniversalTime().Ticks.ToString() -cne $request.ownerStartTicks -or $owner.HasExited){throw 'FULL_PUBLISH_OWNER_CHANGED'}\nif([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() -ge ($request.startedAtUnixMs+$request.budgetSeconds*1000)){exit 124}\nif((Get-FileHash -LiteralPath ${quote(publish)} -Algorithm SHA256).Hash.ToLowerInvariant() -cne ${quote(publishSha)}){throw 'FULL_PUBLISH_SOURCE_CHANGED'}\n. ${quote(publish)}\n$arguments=@{WorkspaceRoot=[string]$request.workspaceRoot;FullAcceptance=$true;FixtureBudgetSeconds=[int]$request.budgetSeconds;DeployInstalled=[bool]$request.deployInstalled}\n$global:LASTEXITCODE=0\nInvoke-YmccPublishBody @arguments\nexit $LASTEXITCODE\n}finally{$owner.Dispose()}\n`;
 fs.writeFileSync(worker,'\uFEFF'+body,{flag:'wx'});
 const remaining=Math.floor(deadline-Date.now());if(remaining<1000)throw Error('FULL_RELEASE_BUDGET_EXHAUSTED');
 const executable=path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/powershell.exe');
 const result=await runWithWallBudget([],{repoRoot,coreRoot:core,budgetMs:remaining,powershell,outDir:path.join(root,'OwnedCall'),command:{executable,args:['-NoLogo','-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',worker,'-RequestFile',file,'-RequestSha256',requestSha],sourceInputs:[publish,worker,file,path.join(repoRoot,'tools/ymcc_sim/optimization/release-full-budget.ps1'),path.join(repoRoot,'tools/ymcc_sim/optimization/release-full-budget.mjs')]}});
 const wholeWallMs=Date.now()-request.startedAtUnixMs,timeout=result.exitCode===124||Date.now()>=deadline;
 const receipt={schemaVersion:1,kind:'ymcc-full-publish-whole-invocation',scope:'Original Build/fixture/Package/optional explicit deploy entry only; raw child/component receipts still required',status:timeout?'TIMEOUT':result.exitCode===0?'ORIGINAL_PUBLISH_BODY_EXITED':'FAILED',exitCode:timeout?124:result.exitCode,budgetMs:request.budgetSeconds*1000,wholeWallMs,dispatchAndClosingMs:performance.now()-began,deadlineEpochMs:deadline,request:{path:file,sha256:requestSha},publish:{path:publish,sha256:publishSha},innerFixtureAccounting:'Separate inherited session; nested wall is not charged twice',originalInvocationRuns:1,automaticRetries:0,ownedExecution:path.join(result.result.runDir,'execution-budget.json'),ownedJobVerified:result.result.ownedJobVerified,ownedJobDisposed:result.result.ownedJob?.jobDisposed===true,actualFullAcceptance:false};
 write(path.join(root,'WHOLE-PUBLISH.json'),receipt);console.log(JSON.stringify(receipt));return {...result,exitCode:receipt.exitCode,receipt};
}
export async function main(args){
 const options={};for(let i=0;i<args.length;i+=2){if(!['--action','--budget-ms','--stage','--workspace-root','--candidate-package','--lifecycle-evidence-root','--session','--request-file','--test','--pnpm-command'].includes(args[i])||!args[i+1]||options[args[i]]!==undefined)throw Error('FULL_RELEASE_ARGUMENT_INVALID');options[args[i]]=args[i+1];}
 if(options['--action']==='publish-invocation'){
  if(Object.keys(options).some(k=>!['--action','--request-file'].includes(k))||!options['--request-file'])throw Error('FULL_PUBLISH_ARGUMENT_INVALID');
  return (await runFullReleasePublishInvocation(options['--request-file'])).exitCode;
 }
 if(options['--action']==='hosted-regression'){
  if(Object.keys(options).some(k=>!['--action','--test','--workspace-root','--session','--pnpm-command'].includes(k))||!options['--session'])throw Error('FULL_RELEASE_HOSTED_ARGUMENT_INVALID');
  const result=await runHostedReleaseRegression({test:options['--test'],workspaceRoot:options['--workspace-root'],pnpmCommand:options['--pnpm-command'],sessionFile:options['--session']});
  console.log(JSON.stringify({status:result.status??result.result.status,timeoutScope:result.timeoutScope??null,sessionFile:result.sessionFile,spentMs:result.session.spentMs,budgetMs:result.session.budgetMs,fixedTest:options['--test'],fullProductAcceptance:false}));return result.exitCode;
 }
 if(options['--action']==='lighting-request'){
  if(Object.keys(options).some(k=>!['--action','--request-file'].includes(k))||!options['--request-file'])throw Error('FULL_RELEASE_LIGHTING_ARGUMENT_INVALID');
  const requestFile=path.resolve(options['--request-file']);if(fs.statSync(requestFile).size>65536)throw Error('FULL_RELEASE_LIGHTING_REQUEST_TOO_LARGE');
  const selected=validateLightingRequest(read(requestFile));selected.command.sourceInputs.push(requestFile);
  const sessionFile=selected.sessionFile||createFullReleaseSession();
  const result=await runFullReleaseFixtureStage({stage:'Lighting',workspaceRoot:selected.workspaceRoot,sessionFile,gateEntry:selected.entry,originalCommand:selected.command});
  console.log(JSON.stringify({status:result.status??result.result.status,timeoutScope:result.timeoutScope??null,sessionFile,spentMs:result.session.spentMs,budgetMs:result.session.budgetMs,fullProductAcceptance:false}));return result.exitCode;
 }
 if(options['--action']==='create'){
  if(Object.keys(options).some(k=>!['--action','--budget-ms'].includes(k)))throw Error('FULL_RELEASE_CREATE_ARGUMENT_INVALID');
  console.log(JSON.stringify({sessionFile:createFullReleaseSession({budgetMs:options['--budget-ms']===undefined?MAX_WALL_BUDGET_MS:Number(options['--budget-ms'])})}));return 0;
 }
 if(options['--action']!=='stage'||options['--budget-ms']!==undefined||options['--request-file']!==undefined||options['--test']!==undefined||options['--pnpm-command']!==undefined||!['Build','Package','Verify'].includes(options['--stage']))throw Error('FULL_RELEASE_STAGE_ARGUMENT_INVALID');
 const sessionFile=options['--session']||createFullReleaseSession();
 const result=await runFullReleaseFixtureStage({stage:options['--stage'],workspaceRoot:options['--workspace-root'],candidatePackage:options['--candidate-package'],lifecycleEvidenceRoot:options['--lifecycle-evidence-root'],sessionFile});
 console.log(JSON.stringify({status:result.status??result.result.status,timeoutScope:result.timeoutScope??null,sessionFile,spentMs:result.session.spentMs,budgetMs:result.session.budgetMs,fullProductAcceptance:false}));return result.exitCode;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))main(process.argv.slice(2)).then(c=>process.exitCode=c).catch(e=>{console.error('FULL_RELEASE_FIXTURE_BUDGET_ERROR '+e.message);process.exitCode=e.message==='FULL_RELEASE_BUDGET_EXHAUSTED'?124:3;});
