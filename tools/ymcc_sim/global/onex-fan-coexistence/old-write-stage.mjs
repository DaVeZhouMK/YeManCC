import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {checkedOldWritePin,validateOldWriteDerivedPlan,OLD_WRITE_GATE,OLD_WRITE_IDENTITIES} from './prepare-old-write-stage.mjs';
import {deriveManagedAllowance,reviewManagedGraph} from './first-old-stage.mjs';
import {reviewOldWriteBranch} from './old-write-branch-review.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const pin=p=>({path:p,sha256:hash(p)});
const put=(p,v)=>fs.writeFileSync(p,typeof v==='string'?v:JSON.stringify(v,null,2)+'\n',{flag:'wx'});
const same=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
const inside=(root,p)=>{const r=path.relative(root,p);return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const need=(ok,code)=>{if(!ok)throw Object.assign(new Error(code),{code});};
const CASES=['duty-cold','duty-cached','manual-profile','automatic-profile','temperature-target-false-progress','same-temperature-recovery-never-retries'];
const GOLDENS=[[0,0],[0.25,0],[0.5,1],[1,2],[25,46],[50,92],[75,138],[99,182],[100,184]];
export function assessOldWriteResult({business,launch,processReceipt,allowance,invocation}){
  const checks=[],check=(id,passed,detail)=>checks.push({id,passed:passed===true,detail});
  check('natural-exit-in-original45s',processReceipt?.naturalExit===true&&!processReceipt.timeout&&Number.isFinite(processReceipt.durationMs)&&processReceipt.durationMs<=OLD_WRITE_GATE.budgetMs,processReceipt);
  check('business-exit0',processReceipt?.exitCode===0,processReceipt?.exitCode);
  check('original-defect-status',business?.status===OLD_WRITE_GATE.status,business?.status);
  check('exact-original6-case-order',JSON.stringify(business?.cases?.map(x=>x.id))===JSON.stringify(CASES),business?.cases?.map(x=>x.id));
  check('all35-original-checks',business?.checks?.length===35&&business.checks.every(x=>x.passed===true),business?.checks?.filter(x=>!x.passed));
  check('exact-original9-goldens',JSON.stringify(business?.goldenWrites?.map(x=>[x.percent,x.raw]))===JSON.stringify(GOLDENS),business?.goldenWrites);
  check('native-attempts0',business?.nativeAttempts===0,business?.nativeAttempts);
  check('no-device-or-installed-io',business?.installed===false&&business?.hardwareIo===false&&business?.hc?.codeExecuted===false,business?.scope);
  check('exact-fixed-host-and-raw-metadata',business?.host?.sha256===invocation.host.sha256&&same(business.host.path,invocation.host.path)&&business?.hc?.sha256===invocation.rawHc.sha256&&same(business.hc.path,invocation.rawHc.path),{host:business?.host,hc:business?.hc});
  check('clean-launch-receipt',launch?.exitCode===0&&launch?.failure===null,{exitCode:launch?.exitCode,failure:launch?.failure});
  const actual=launch?.actualLoadedManaged??[],graph=reviewManagedGraph(actual,allowance);
  check('all-managed-loads-pinned',graph.allManagedPinned,graph.invalid);
  check('raw-HC-never-runtime-loaded',actual.every(x=>x.sha256!==OLD_WRITE_IDENTITIES.rawHcSha256),invocation.rawHc);
  const hc=actual.filter(x=>x.identity?.split(',')[0]==='HandheldCompanion');
  check('only-synthetic-HC',hc.length===1&&hc[0].sha256===OLD_WRITE_IDENTITIES.syntheticHcSha256,hc);
  for(const target of [invocation.producer,invocation.launcher,invocation.host])check('actual-load:'+path.basename(target.path),actual.some(x=>same(x.path,target.path)&&x.sha256===target.sha256),target);
  return {passed:checks.every(x=>x.passed),checks,managedGraph:graph};
}
export function executeOldWriteStage({mode,stagePin,evidenceDir}){
  need(['prepare-only','run'].includes(mode),'OLD_WRITE_EXPLICIT_STAGE_MODE_REQUIRED');checkedOldWritePin(stagePin);
  const stage=read(stagePin.path);need(stage.schemaVersion===1&&stage.kind==='onex-old-write-stage','OLD_WRITE_STAGE_SHAPE');
  for(const p of [stage.invocation,stage.branchInputs,stage.defaultPreload])checkedOldWritePin(p);
  const plan=read(stage.invocation.path),policy=read(checkedOldWritePin(plan.policy).path),argv=read(checkedOldWritePin(plan.argumentMetadata).path);
  validateOldWriteDerivedPlan(plan,policy,argv);need(plan.dotnet.sha256==='ab1b71fd3dd71062e074c9fab8312081a81b7f2b3e0327c48c4d249c8d1a3135'&&plan.launcher.sha256==='55bbc787f00ccc7b3c3855023f2f96df6423baf0481883ec40a732f7b5583a0e'&&plan.producer.sha256==='fe96c6a564e74a1bafcfe4161f1f23f4d84ecaab26b1e510fe332217c6dd4846','OLD_WRITE_REVIEWED_ENTRY_REQUIRED');
  const input=read(stage.branchInputs.path);need(same(input.preparedPlan.path,stage.invocation.path)&&input.preparedPlan.sha256===stage.invocation.sha256,'OLD_WRITE_STAGE_BRANCH_PLAN_MISMATCH');
  const preload=read(stage.defaultPreload.path),metadata=read(plan.metadataValidateReceipt),framework=read(checkedOldWritePin(plan.frameworkMetadata).path);
  need(preload.status==='PASS_DEFAULT_PRELOAD_METADATA'&&preload.defaultPinCount===44&&preload.rows.length===44&&preload.rows.every(x=>x.admitted)&&preload.policySha256===plan.policy.sha256&&preload.targetAssembliesObserved===false,'OLD_WRITE_EXACT_DEFAULT44_REQUIRED');
  need(metadata.exitCode===0&&metadata.failure===null&&metadata.productLoaded===false&&JSON.stringify(metadata.events)===JSON.stringify(['prepare-metadata-only']),'OLD_WRITE_EXACT_MTA_VALIDATE_REQUIRED');
  const allowance=deriveManagedAllowance({policy,preload,invocation:plan,frameworkMetadata:framework});need(allowance.missingBaseClr.length===0,'OLD_WRITE_MISSING_EXACT_CLR_BINDING');
  const pins=[stagePin,stage.invocation,stage.branchInputs,stage.defaultPreload,pin(plan.metadataValidateReceipt),plan.frameworkMetadata,plan.dotnet,plan.launcher,plan.producer,plan.host,plan.rawHc,plan.policy,plan.argumentMetadata,...plan.outputs,...policy.metadata,...allowance.rows];
  for(const p of pins)checkedOldWritePin(p);
  for(const p of [plan.launchReceipt,plan.businessResult])need(!fs.existsSync(p),'OLD_WRITE_FRESH_BUSINESS_OUTPUT_REQUIRED');
  need(typeof evidenceDir==='string'&&path.isAbsolute(evidenceDir)&&!fs.existsSync(evidenceDir),'OLD_WRITE_FRESH_EVIDENCE_REQUIRED');
  const out=path.resolve(evidenceDir),repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
  need(!inside(repo,out)&&!inside(out,repo)&&!inside(plan.ownedRoot,out)&&!inside(out,plan.ownedRoot),'OLD_WRITE_EVIDENCE_OVERLAPS_INPUT');
  need(same(fs.realpathSync(path.dirname(out)),path.dirname(out)),'OLD_WRITE_EVIDENCE_PARENT_REDIRECTED');fs.mkdirSync(out);
  let executed=false;
  try{
    const branch=reviewOldWriteBranch({inputsPin:stage.branchInputs,evidenceDir:path.join(out,'branch-review')});
    need(branch.status==='REVIEWED_ONE_EXACT_OLD_WRITE_NO_DEVICE_IO'&&branch.staticNoDeviceIoConclusion===true,'OLD_WRITE_SPECIFIC_BRANCH_REVIEW_REQUIRED');
    const proof=read(branch.path);pins.push(pin(branch.path),proof.basis,...proof.checkedPins);
    for(const p of pins)checkedOldWritePin(p);
    const prepared={schemaVersion:1,status:'PREPARED_NOT_EXECUTED',passed:false,producerExecuted:false,stage:stagePin,branch:{path:branch.path,sha256:branch.sha256},gate:OLD_WRITE_GATE};put(path.join(out,'PREPARED.json'),prepared);
    if(mode==='prepare-only')return {exitCode:0,...prepared};
    const command={program:plan.dotnet,args:plan.dotnetArguments,cwd:plan.ownedRoot,budgetMs:OLD_WRITE_GATE.budgetMs,stage:stagePin,branch:prepared.branch};put(path.join(out,'COMMAND.json'),command);
    for(const p of [plan.launchReceipt,plan.businessResult])need(!fs.existsSync(p),'OLD_WRITE_FRESH_BUSINESS_OUTPUT_REQUIRED');
    const env={...process.env};for(const k of Object.keys(env))if(/^(DOTNET_|CORECLR_|COR_|COMPLUS_)/i.test(k))delete env[k];Object.assign(env,{DOTNET_ROOT:path.dirname(plan.dotnet.path),DOTNET_EnableDiagnostics:'0',DOTNET_CLI_TELEMETRY_OPTOUT:'1'});
    const started=Date.now();executed=true;const child=spawnSync(command.program.path,command.args,{cwd:command.cwd,env,shell:false,windowsHide:true,encoding:'utf8',timeout:OLD_WRITE_GATE.budgetMs,maxBuffer:8*1024*1024});
    const processReceipt={exitCode:child.status,signal:child.signal,error:child.error?.message??null,naturalExit:child.status!==null&&!child.error&&!child.signal,timeout:child.error?.code==='ETIMEDOUT',durationMs:Date.now()-started};
    put(path.join(out,'stdout.log'),child.stdout??'');put(path.join(out,'stderr.log'),child.stderr??'');put(path.join(out,'PROCESS.json'),processReceipt);
    const business=fs.existsSync(plan.businessResult)?read(plan.businessResult):null,launch=fs.existsSync(plan.launchReceipt)?read(plan.launchReceipt):null;
    const assessment=assessOldWriteResult({business,launch,processReceipt,allowance,invocation:plan});
    for(const p of [...pins,...(launch?.actualLoadedManaged??[])]){let passed=false;try{checkedOldWritePin(p);passed=true;}catch{}assessment.checks.push({id:'unchanged:'+p.path,passed,expectedSha256:p.sha256});}
    const passed=assessment.checks.every(x=>x.passed),summary={schemaVersion:1,atUtc:new Date().toISOString(),status:passed?'PASS_FIXED_OLD_WRITE_DEFECT_REPRODUCED':'FAIL_OLD_WRITE_STAGE_PRESERVED',passed,producerExecuted:true,stage:stagePin,branch:prepared.branch,process:pin(path.join(out,'PROCESS.json')),business:fs.existsSync(plan.businessResult)?pin(plan.businessResult):null,launch:fs.existsSync(plan.launchReceipt)?pin(plan.launchReceipt):null,caseCount:business?.cases?.length??0,originalChecks:business?.checks?.length??0,goldenWrites:business?.goldenWrites?.length??0,nativeAttempts:business?.nativeAttempts??null,checks:assessment.checks,managedGraph:assessment.managedGraph,currentHostPassed:false,fullFanWorkflowPassed:false,physicalAcceptance:false,fullHostApproved:false,wholeGoalComplete:false,jointUserTestingReady:false,cpuPowerBenefit:null};
    put(path.join(out,'SUMMARY.json'),summary);return {exitCode:passed?0:2,...summary};
  }catch(e){const failure={schemaVersion:1,status:executed?'FAIL_OLD_WRITE_STAGE_PRESERVED':'BLOCKED_NOT_EXECUTED',passed:false,producerExecuted:executed,code:e.code??'OLD_WRITE_STAGE_ERROR',error:e.message};put(path.join(out,'FAILURE.json'),failure);return {exitCode:executed?2:3,...failure};}
}
if(process.argv[1]&&same(process.argv[1],fileURLToPath(import.meta.url))){try{const a=process.argv.slice(2);need(a.length===8&&JSON.stringify(a.filter((_,i)=>i%2===0))===JSON.stringify(['--mode','--stage','--sha256','--evidence-dir']),'OLD_WRITE_STAGE_EXACT_CLI');const r=executeOldWriteStage({mode:a[1],stagePin:{path:a[3],sha256:a[5]},evidenceDir:a[7]});console.log(JSON.stringify({status:r.status,passed:r.passed,producerExecuted:r.producerExecuted,caseCount:r.caseCount,originalChecks:r.originalChecks,goldenWrites:r.goldenWrites}));process.exitCode=r.exitCode;}catch(e){console.error(JSON.stringify({status:'BLOCKED_NOT_EXECUTED',code:e.code,error:e.message}));process.exitCode=3;}}
