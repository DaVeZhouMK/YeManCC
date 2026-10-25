import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

export const BUSINESS_BUDGET_MS=45000;
const SHA=/^[a-f0-9]{64}$/;
const APPROVED={
  dotnet:'ab1b71fd3dd71062e074c9fab8312081a81b7f2b3e0327c48c4d249c8d1a3135',
  launcher:'55bbc787f00ccc7b3c3855023f2f96df6423baf0481883ec40a732f7b5583a0e',
  authority:'64ad04c2085e208d6e27aaf6f4c15d2a5ad35b58a4076cec73e3b6b407c1900b',
  synthetic:'6d7bff05991ee407538b4ef35e12092b21a155d15ab5ca60fe0eae21846391f6',
  rawHc:'08a386607a0e5e72c5fcaa98d3a6c0b3b256eb0fca7d6547f6f124c1f1f33583',
};
const MTA_IDENTITY='MTA, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null';
// This finite list is NOT an approval. Each member still needs an exact actual
// managed-load row in the pinned metadata-only preload receipt; netstandard alone
// uses the user-reviewed pre-existing framework-metadata PE row.
export const BASE_CLR_IDENTITIES=Object.freeze([
  'System.Private.CoreLib, Version=10.0.0.0, Culture=neutral, PublicKeyToken=7cec85d7bea7798e',
  'System.Numerics.Vectors, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
  'System.Private.Uri, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
  'System.Collections.Immutable, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a',
  'netstandard, Version=2.1.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51',
  'System.IO.Pipelines, Version=10.0.0.0, Culture=neutral, PublicKeyToken=cc7b13ffcd2ddd51',
]);
const key=p=>path.resolve(p).toLowerCase();
const same=(a,b)=>typeof a==='string'&&typeof b==='string'&&key(a)===key(b);
const inside=(root,p)=>{const r=path.relative(path.resolve(root),path.resolve(p));return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
export const fileHash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const digest=value=>createHash('sha256').update(JSON.stringify(value)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const put=(p,value)=>fs.writeFileSync(p,typeof value==='string'?value:JSON.stringify(value,null,2)+'\n',{flag:'wx'});
const fail=(code,detail)=>{throw Object.assign(new Error(code),{code,detail,firstOldStageBlocked:true});};
const need=(value,code,detail)=>{if(!value)fail(code,detail);};
const equal=(a,b,code)=>need(JSON.stringify(a)===JSON.stringify(b),code,{expected:b,actual:a});
function pinShape(p){need(p&&typeof p.path==='string'&&path.isAbsolute(p.path)&&SHA.test(p.sha256??''),'FIRST_OLD_PIN_SHAPE',p);}
function samePin(a,b,code){pinShape(a);pinShape(b);need(same(a.path,b.path)&&a.sha256===b.sha256,code,{a,b});}
function managedShape(row){pinShape(row);need(typeof row.identity==='string'&&row.identity.includes(', Version='),'FIRST_OLD_FULL_IDENTITY_REQUIRED',row);}
function triple(row){return key(row.path)+'|'+row.identity+'|'+row.sha256;}
function noRedirect(p){need(same(fs.realpathSync(p),p)&&!fs.lstatSync(p).isSymbolicLink(),'FIRST_OLD_REDIRECTED_PATH',{path:p});}
export function checkedPin(p){pinShape(p);need(fs.existsSync(p.path)&&fs.statSync(p.path).isFile(),'FIRST_OLD_PIN_MISSING',p);noRedirect(p.path);need(fileHash(p.path)===p.sha256,'FIRST_OLD_PIN_CHANGED',p);return p;}
function uniquePaths(rows,code){const seen=new Set();for(const row of rows){pinShape(row);need(!seen.has(key(row.path)),code,row);seen.add(key(row.path));}}

export function validateStageDocuments(d) {
  const {stage,invocation:p,policy:q,review:r,preload:s,preloadStep:t,preloadProcess:x,argvMetadata:a,frameworkMetadata:f}=d;
  need(stage.schemaVersion===1&&stage.kind==='onex-first-old-stage','FIRST_OLD_STAGE_PLAN_SHAPE');
  need(p.schemaVersion===1&&p.status==='ARGUMENT_BOUND_PREPARED_NOT_EXECUTED','FIRST_OLD_PREPARED_PLAN_REQUIRED');
  need(p.originalBusinessBudgetMs===BUSINESS_BUDGET_MS,'FIRST_OLD_ORIGINAL_BUDGET_REQUIRED');
  need(typeof p.ownedRoot==='string'&&path.isAbsolute(p.ownedRoot)&&same(q.ownedRoot,p.ownedRoot),'FIRST_OLD_OWNED_ROOT_BINDING');
  need(Array.isArray(p.outputs)&&p.outputs.length>0&&p.outputs.length<=3072,'FIRST_OLD_OUTPUT_PINS_REQUIRED');uniquePaths(p.outputs,'FIRST_OLD_DUPLICATE_OUTPUT_PIN');
  for(const row of p.outputs)need(inside(p.ownedRoot,row.path),'FIRST_OLD_BUILD_OUTPUT_OUTSIDE_OWNED_ROOT',row);
  for(const field of ['producer','launcher','policy'])pinShape(p[field]);
  need(p.launcher.sha256===APPROVED.launcher&&path.basename(p.launcher.path)==='MTA.dll','FIRST_OLD_REVIEWED_MTA_REQUIRED');
  need(stage.dotnet.sha256===APPROVED.dotnet&&path.basename(stage.dotnet.path).toLowerCase()==='dotnet.exe','FIRST_OLD_REVIEWED_DOTNET_REQUIRED');
  for(const pin of [p.producer,p.launcher])need(p.outputs.some(row=>same(row.path,pin.path)&&row.sha256===pin.sha256),'FIRST_OLD_ENTRY_NOT_IN_BUILD_OUTPUTS',pin);
  need(same(q.entry,p.producer.path),'FIRST_OLD_POLICY_ENTRY_MISMATCH');
  need(Array.isArray(q.contexts)&&q.contexts.length===2&&q.contexts.filter(c=>c.id==='Default'&&c.kind==='default').length===1,'FIRST_OLD_CONTEXT_CONTRACT');
  for(const c of q.contexts){need(Array.isArray(c.managed)&&Array.isArray(c.requests),'FIRST_OLD_CONTEXT_PIN_CONTRACT');uniquePaths(c.managed,'FIRST_OLD_DUPLICATE_CONTEXT_PIN');for(const row of c.managed)managedShape(row);}
  uniquePaths(q.metadata,'FIRST_OLD_DUPLICATE_METADATA_PIN');
  need(Array.isArray(p.firstOldArgs)&&p.firstOldArgs.length===5&&p.firstOldArgs[0]==='old','FIRST_OLD_EXACT_MODE_AND_ARGC');
  need(typeof p.launchReceipt==='string'&&path.isAbsolute(p.launchReceipt)&&typeof p.firstOldArgs[4]==='string'&&path.isAbsolute(p.firstOldArgs[4]),'FIRST_OLD_ABSOLUTE_OUTPUT_PATHS_REQUIRED');
  const output=p.firstOldArgs[4];
  for(const name of [p.launchReceipt,output])need(inside(p.ownedRoot,name)&&!same(p.ownedRoot,name),'FIRST_OLD_RECEIPT_OUTSIDE_OWNED_ROOT',{path:name});
  need(!same(p.launchReceipt,output),'FIRST_OLD_OUTPUT_PATH_ALIAS');
  equal(p.dotnetArguments,[p.launcher.path,'run',p.policy.path,p.policy.sha256,p.launchReceipt,...p.firstOldArgs],'FIRST_OLD_EXACT_ARGV_REQUIRED');
  equal(a.args,p.firstOldArgs,'FIRST_OLD_ARGUMENT_METADATA_MISMATCH');
  need(a.schemaVersion===1&&a.kind==='Harness'&&same(a.producer,p.producer.path)&&same(a.output,output)&&a.expectedBusinessExit===0&&a.argvSha256===digest(p.firstOldArgs),'FIRST_OLD_ARGUMENT_METADATA_CONTRACT');
  need(q.metadata.some(row=>same(row.path,p.argumentMetadata.path)&&row.sha256===p.argumentMetadata.sha256),'FIRST_OLD_ARGUMENT_METADATA_NOT_PINNED');
  need(q.metadata.some(row=>same(row.path,p.firstOldArgs[3])&&row.sha256===APPROVED.rawHc),'FIRST_OLD_RAW_HC_METADATA_PIN_REQUIRED');
  need(r.status==='REVIEWED_ONE_EXACT_FIRST_OLD_NOIO_INVOCATION'&&r.oneInvocationReviewed===true,'FIRST_OLD_ONE_INVOCATION_REVIEW_REQUIRED');
  samePin(r.plan,stage.invocation,'FIRST_OLD_REVIEW_PLAN_MISMATCH');samePin(r.policy,p.policy,'FIRST_OLD_REVIEW_POLICY_MISMATCH');
  equal(r.branchLimits?.argv,p.firstOldArgs,'FIRST_OLD_REVIEW_ARGV_MISMATCH');
  for(const n of ['hostMainInvoked','hostOptionsCtorInvoked','rawHcClrLoadPermitted','hostFactoryStartPermitted','realManagerStartPermitted','osSleepPermitted','realDeviceIoPermitted','installationPermitted'])need(r.branchLimits[n]===false,'FIRST_OLD_REVIEW_UNSAFE_BRANCH',{field:n});
  need(r.branchLimits.fixedOneXTypesOnly===true&&Array.isArray(r.remainingDirectLocalMethodEdges)&&r.remainingDirectLocalMethodEdges.length===0,'FIRST_OLD_REVIEW_OPEN_METHOD_FRONTIER');
  need(r.constraints.businessBudgetMs===BUSINESS_BUDGET_MS&&r.constraints.originalCaseAssertionsUnchanged===true&&r.constraints.originalSemanticFaultsUnchanged===true&&r.constraints.noSuccessByPreparationFailure===true,'FIRST_OLD_REVIEW_CONSTRAINTS');
  need(Array.isArray(r.selectedSourceRoute)&&r.selectedSourceRoute.length>0&&Array.isArray(r.metadataReports)&&r.metadataReports.length>0,'FIRST_OLD_REVIEW_SOURCE_AND_IL_PINS_REQUIRED');
  const producerOutput=p.outputs.find(row=>same(row.path,p.producer.path)&&row.sha256===p.producer.sha256);
  need(Array.isArray(r.images)&&r.images.some(i=>(same(i.path,p.producer.path)||same(i.path,producerOutput?.original))&&i.sha256===p.producer.sha256),'FIRST_OLD_REVIEW_PRODUCER_MISMATCH');
  for(const f of p.firstOldArgs.slice(1,3))need(q.contexts.flatMap(c=>c.managed).some(row=>same(row.path,f)&&r.images.some(i=>same(i.path,f)&&i.sha256===row.sha256)),'FIRST_OLD_HISTORICAL_ARGUMENT_NOT_REVIEWED',{path:f});
  need(s.status==='PASS_DEFAULT_PRELOAD_METADATA'&&s.exitCode===0&&s.failure===null&&s.metadataPrepared===true,'FIRST_OLD_PRELOAD_NOT_PASSED');
  for(const n of ['targetAssembliesObserved','attachExecuted','loadPinExecuted','producerExecuted'])need(s[n]===false,'FIRST_OLD_PRELOAD_EXECUTED_TARGET',{field:n});
  need(Array.isArray(s.observerErrors)&&s.observerErrors.length===0,'FIRST_OLD_PRELOAD_OBSERVER_ERROR');
  need(same(s.policyPath,p.policy.path)&&s.policySha256===p.policy.sha256,'FIRST_OLD_PRELOAD_POLICY_MISMATCH');
  samePin(t.policy,p.policy,'FIRST_OLD_PRELOAD_STEP_POLICY_MISMATCH');samePin(t.dotnet,stage.dotnet,'FIRST_OLD_PRELOAD_DOTNET_MISMATCH');
  need(t.authority.sha256===APPROVED.authority&&s.reviewedAuthoritySha256===APPROVED.authority&&same(s.reviewedAuthorityPath,t.authority.path),'FIRST_OLD_AUTHORITY_BUILD_MISMATCH');
  need(same(t.receipt,stage.preloadReceipt.path),'FIRST_OLD_PRELOAD_RECEIPT_MISMATCH');
  equal(t.argv,[t.tool.path,'check-default-metadata',p.policy.path,p.policy.sha256,t.authority.path,t.authority.sha256,t.receipt],'FIRST_OLD_PRELOAD_EXACT_ARGV');
  need(x.naturalExit===true&&x.exitCode===0&&x.signal===null&&x.error===null,'FIRST_OLD_PRELOAD_NOT_NATURAL');
  samePin(x.step,stage.preloadStep,'FIRST_OLD_PRELOAD_PROCESS_STEP_MISMATCH');samePin(x.program,stage.dotnet,'FIRST_OLD_PRELOAD_PROCESS_PROGRAM_MISMATCH');equal(x.argv,t.argv,'FIRST_OLD_PRELOAD_PROCESS_ARGV');
  const defaults=q.contexts.find(c=>c.id==='Default').managed;
  need(s.defaultPinCount===defaults.length&&Array.isArray(s.rows)&&s.rows.length===defaults.length,'FIRST_OLD_PRELOAD_PIN_SET_MISMATCH');
  const prePins=s.rows.map(row=>{need(row.admitted===true&&row.rejection===null,'FIRST_OLD_PRELOAD_PIN_REJECTED',row);return JSON.parse(row.pinJson);});
  need(new Set(prePins.map(triple)).size===defaults.length&&defaults.every(row=>prePins.some(p=>triple(p)===triple(row))),'FIRST_OLD_PRELOAD_PIN_SET_MISMATCH');
  for(const row of [...s.actualLoadedManagedBefore,...s.actualLoadedManagedAfter,...s.assemblyLoadEvents]){managedShape(row);need(row.target===false&&row.dynamic===false,'FIRST_OLD_PRELOAD_TARGET_OR_DYNAMIC',row);}
  need(f.status==='PASS'&&f.targetRuntimeLoaded===false&&Array.isArray(f.rows),'FIRST_OLD_FRAMEWORK_METADATA_REQUIRED');
  const net=f.rows.filter(row=>row.identity===BASE_CLR_IDENTITIES[4]);
  need(net.length===1&&net[0].sha256==='ce09cef0d758196f2467f320d840c03202453efe9fb1920dddd24ea8277bbbb3'&&net[0].entryPoint===null&&net[0].moduleInitializers?.length===0,'FIRST_OLD_NETSTANDARD_METADATA_ROW_REQUIRED');
  managedShape(net[0]);
  const managed=q.contexts.flatMap(c=>c.managed),hc=managed.filter(row=>row.identity.split(',')[0]==='HandheldCompanion');
  need(hc.length===1&&hc[0].sha256===APPROVED.synthetic&&inside(p.ownedRoot,hc[0].path),'FIRST_OLD_SYNTHETIC_HC_ONLY');
  need(managed.every(row=>row.sha256!==APPROVED.rawHc),'FIRST_OLD_RAW_HC_IN_RUNTIME_POLICY');
  for(const name of [p.launchReceipt,output])need(![...p.outputs,...q.metadata,...managed].some(row=>same(row.path,name)),'FIRST_OLD_OUTPUT_OVERWRITES_PIN',{path:name});
  return d;
}

export function deriveManagedAllowance({policy,preload,invocation,frameworkMetadata}) {
  const rows=policy.contexts.flatMap(c=>c.managed.map(row=>({...row,acceptedBy:'scoped-policy',context:c.id})));
  rows.push({...invocation.launcher,identity:MTA_IDENTITY,acceptedBy:'exact-reviewed-MTA'});
  const missing=[];
  for(const identity of BASE_CLR_IDENTITIES) {
    const netstandard=identity.startsWith('netstandard,');
    const found=netstandard?(frameworkMetadata?.rows??[]).filter(row=>row.identity===identity):(preload.actualLoadedManagedAfter.filter(row=>row.identity===identity&&!row.target&&!row.dynamic));
    if(found.length!==1){missing.push(identity);continue;}
    rows.push({...found[0],acceptedBy:netstandard?'exact-pinned-framework-metadata-row':'exact-existing-preload-row'});
  }
  return {rows,missingBaseClr:missing};
}
export function reviewManagedGraph(actual,allowance) {
  need(Array.isArray(actual),'FIRST_OLD_MANAGED_GRAPH_REQUIRED');
  const invalid=[],accepted=[];
  for(const row of actual) {
    if(!row||row.dynamic||typeof row.identity!=='string'||typeof row.path!=='string'||!path.isAbsolute(row.path)||!SHA.test(row.sha256??'')) {invalid.push({row,reason:'INVALID_OR_DYNAMIC'});continue;}
    const match=allowance.rows.find(pin=>triple(pin)===triple(row));
    if(!match||row.sha256===APPROVED.rawHc)invalid.push({row,reason:'NOT_EXACTLY_PINNED'});
    else accepted.push({...row,acceptedBy:match.acceptedBy,...(match.context?{declaredContext:match.context}:{})});
  }
  return {accepted,invalid,missingBaseClr:allowance.missingBaseClr,allManagedPinned:invalid.length===0&&allowance.missingBaseClr.length===0};
}
export function preparationSummary(planPin,invocation) {
  return {schemaVersion:1,status:'PREPARED_NOT_EXECUTED',passed:false,producerExecuted:false,plan:planPin,
    exactArgv:invocation.dotnetArguments,originalBudgetMs:BUSINESS_BUDGET_MS,sourceApprovalGranted:false,executionApprovalGranted:false,
    fullHostApproved:false,wholeGoalComplete:false,jointUserTestingReady:false,cpuPowerBenefit:null};
}

// Reads exact already prepared inputs only. It never compiles, relocates a policy,
// calls a metadata tool, attaches a resolver or launches any managed assembly.
export function loadStageContract(planPin) {
  const pins=new Map();
  const track=pin=>{checkedPin(pin);const previous=pins.get(key(pin.path));need(!previous||previous.sha256===pin.sha256,'FIRST_OLD_PIN_CONFLICT',pin);pins.set(key(pin.path),{path:pin.path,sha256:pin.sha256});return pin;};
  const json=pin=>read(track(pin).path);
  const stage=json(planPin);
  equal(Object.keys(stage).sort(),['schemaVersion','kind','invocation','review','preloadStep','preloadReceipt','preloadProcess','dotnet','frameworkMetadata'].sort(),'FIRST_OLD_STAGE_ENVELOPE_KEYS');
  const invocation=json(stage.invocation),policy=json(invocation.policy),review=json(stage.review),preload=json(stage.preloadReceipt),preloadStep=json(stage.preloadStep),preloadProcess=json(stage.preloadProcess),argvMetadata=json(invocation.argumentMetadata),frameworkMetadata=json(stage.frameworkMetadata);
  track(stage.dotnet);
  const d={stage,invocation,policy,review,preload,preloadStep,preloadProcess,argvMetadata,frameworkMetadata};
  validateStageDocuments(d);
  const p=invocation,q=policy;
  for(const row of [...p.outputs,...q.metadata,...q.contexts.flatMap(c=>c.managed),...review.metadataReports,...review.images,...review.selectedSourceRoute,
    review.syntheticGeneratedSource,review.ownPortsModule,preloadStep.tool,preloadStep.toolPdb,preloadStep.source,preloadStep.runtimeconfig,preloadStep.sourceRuntimeconfig,
    preloadStep.authority,preloadStep.dotnet,...preload.actualLoadedManagedBefore,...preload.actualLoadedManagedAfter,...preload.assemblyLoadEvents])track(row);
  for(const c of q.contexts)if(c.kind==='custom'){
    samePin(c.registrationAssembly,p.producer,'FIRST_OLD_CUSTOM_REGISTRATION_IMAGE');
    need(c.registrationType==='OldBinaryProbe+Context'&&c.id==='OldBinaryProbe.Context|'+path.dirname(p.firstOldArgs[1]).toUpperCase(),'FIRST_OLD_CUSTOM_CONTEXT_OWNERSHIP');
  }
  const inventory=json(p.originalInventory);need(Array.isArray(inventory.outputs)&&inventory.outputs.length===p.outputs.length,'FIRST_OLD_ORIGINAL_INVENTORY_SET');
  for(const row of p.outputs)need(inventory.outputs.some(old=>same(old.path,row.original)&&old.sha256===row.sha256),'FIRST_OLD_RELOCATED_OUTPUT_CHANGED',row);
  for(const pin of [p.originalInvocation,p.originalPolicy,p.originalArgumentMetadata,p.oldRelocationPlan])track(pin);
  const runtimePath=p.launcher.path.replace(/\.dll$/,'.runtimeconfig.json'),runtimePin=p.outputs.find(row=>same(row.path,runtimePath));
  need(runtimePin,'FIRST_OLD_MTA_RUNTIMECONFIG_NOT_PINNED');
  const runtime=json(runtimePin).runtimeOptions;
  need(runtime.tfm==='net10.0'&&runtime.rollForward==='Disable','FIRST_OLD_FIXED_RUNTIME_REQUIRED');
  equal(runtime.frameworks,[{name:'Microsoft.NETCore.App',version:'10.0.11'},{name:'Microsoft.WindowsDesktop.App',version:'10.0.11'}],'FIRST_OLD_RUNTIME_FRAMEWORK_ORDER');
  need(runtimePin.sha256===preloadStep.runtimeconfig.sha256,'FIRST_OLD_PRELOAD_MTA_RUNTIME_BYTES_DIFFER');
  const provenancePin=q.metadata.find(row=>path.basename(row.path)==='source-input-provenance.json');need(provenancePin,'FIRST_OLD_SOURCE_PROVENANCE_REQUIRED');
  const provenance=json(provenancePin),fixedBindings=json(provenance.fixedRunBindings),protectedRoots=[fixedBindings.sourceRoot,fixedBindings.snapshotRoot];
  need(protectedRoots.every(root=>typeof root==='string'&&path.isAbsolute(root)),'FIRST_OLD_PROTECTED_SOURCE_ROOTS_REQUIRED');
  need(protectedRoots.every(root=>!inside(root,p.ownedRoot)&&!inside(p.ownedRoot,root)),'FIRST_OLD_OWNED_ROOT_OVERLAPS_SOURCE');
  noRedirect(p.ownedRoot);
  const allowance=deriveManagedAllowance(d);
  for(const row of allowance.rows)track(row);
  return {...d,planPin,pins:[...pins.values()],protectedRoots,allowance,runtimePin};
}
export function assertFreshOutputs(invocation,evidenceDir) {
  need(typeof evidenceDir==='string'&&path.isAbsolute(evidenceDir),'FIRST_OLD_ABSOLUTE_EVIDENCE_DIRECTORY_REQUIRED');
  const outputs=[evidenceDir,invocation.launchReceipt,invocation.firstOldArgs[4]];
  need(new Set(outputs.map(key)).size===3,'FIRST_OLD_OUTPUT_PATH_ALIAS');
  for(const p of outputs){need(!fs.existsSync(p),'FIRST_OLD_FRESH_OUTPUT_REQUIRED',{path:p});noRedirect(path.dirname(p));}
  need(!inside(evidenceDir,invocation.ownedRoot),'FIRST_OLD_EVIDENCE_CONTAINS_STAGE');
  return outputs;
}
function recheck(ctx){for(const pin of ctx.pins)checkedPin(pin);}
function cleanEnvironment(program){const env={...process.env};for(const name of Object.keys(env))if(/^(DOTNET_|CORECLR_|COR_|COMPLUS_)/i.test(name))delete env[name];return {...env,DOTNET_ROOT:path.dirname(program.path),DOTNET_EnableDiagnostics:'0',DOTNET_CLI_TELEMETRY_OPTOUT:'1'};}

// Original parent-script business gates remain visible. Additional managed rows
// are accepted only by exact full identity/path/SHA, never by directory/name.
export function assessBusinessResult({business,launch,processReceipt,allowance,invocation}) {
  const checks=[],check=(id,passed,details)=>checks.push({id,passed,details});
  check('natural-exit',processReceipt.naturalExit===true&&!processReceipt.timeout&&processReceipt.durationMs<=BUSINESS_BUDGET_MS,processReceipt);
  check('business-exit-0',processReceipt.exitCode===0,processReceipt.exitCode);
  check('business-status',business?.status==='DEFECT_REPRODUCED_IN_MEMORY',business?.status);
  check('native-attempts-0',business?.nativeAttempts===0,business?.nativeAttempts);
  check('original-cases-at-least8',Array.isArray(business?.cases)&&business.cases.length>=8,business?.cases?.length);
  check('original-checks-at-least63',Array.isArray(business?.checks)&&business.checks.length>=63,business?.checks?.length);
  check('all-original-checks',business?.checks?.every(row=>row.passed===true)===true,business?.checks?.filter(row=>!row.passed));
  check('launch-receipt-clean',launch?.exitCode===0&&launch?.failure===null,{exitCode:launch?.exitCode,failure:launch?.failure});
  const actual=launch?.actualLoadedManaged??[],graph=reviewManagedGraph(actual,allowance),hcs=actual.filter(row=>row.identity?.split(',')[0]==='HandheldCompanion');
  const synthetic=allowance.rows.find(row=>row.identity.split(',')[0]==='HandheldCompanion');
  check('only-synthetic-HC-loaded',hcs.length===1&&synthetic&&triple(hcs[0])===triple(synthetic),hcs);
  check('raw-HC-not-runtime-loaded',actual.every(row=>row.sha256!==APPROVED.rawHc),APPROVED.rawHc);
  check('actual-managed-loads-are-pinned',graph.allManagedPinned,graph);
  check('exact-MTA-present',actual.some(row=>same(row.path,invocation.launcher.path)&&row.identity===MTA_IDENTITY&&row.sha256===invocation.launcher.sha256),invocation.launcher);
  check('exact-producer-present',actual.some(row=>same(row.path,invocation.producer.path)&&row.sha256===invocation.producer.sha256),invocation.producer);
  return {passed:checks.every(row=>row.passed),checks,graph};
}

export async function executeFirstOldStage({mode,planPin,evidenceDir}) {
  need(['prepare-only','run'].includes(mode),'FIRST_OLD_EXPLICIT_MODE_REQUIRED');
  const ctx=loadStageContract(planPin),out=path.resolve(evidenceDir);
  need(path.isAbsolute(evidenceDir),'FIRST_OLD_ABSOLUTE_EVIDENCE_DIRECTORY_REQUIRED');
  need(ctx.protectedRoots.every(root=>!inside(root,out)&&!inside(out,root)),'FIRST_OLD_EVIDENCE_OVERLAPS_SOURCE');
  need(!fs.existsSync(out),'FIRST_OLD_FRESH_EVIDENCE_DIRECTORY_REQUIRED');noRedirect(path.dirname(out));
  need(!inside(out,ctx.invocation.ownedRoot),'FIRST_OLD_EVIDENCE_CONTAINS_STAGE');
  fs.mkdirSync(out);
  let producerExecuted=false;
  try {
    // Check both independent blockers so an already consumed plan cannot be
    // relabelled as a new preparation or overwrite its immutable result files.
    const existing=[ctx.invocation.launchReceipt,ctx.invocation.firstOldArgs[4]].filter(p=>fs.existsSync(p));
    need(existing.length===0&&ctx.allowance.missingBaseClr.length===0,'FIRST_OLD_PREREQUISITES_MISSING',{
      existingOutputPaths:existing,missingPreloadFullIdentities:ctx.allowance.missingBaseClr});
    // out was just reserved; the launch/result paths must still be fresh.
    for(const p of [ctx.invocation.launchReceipt,ctx.invocation.firstOldArgs[4]]){need(!fs.existsSync(p),'FIRST_OLD_FRESH_OUTPUT_REQUIRED',{path:p});noRedirect(path.dirname(p));}
    recheck(ctx);
    put(path.join(out,'PREPARED.json'),preparationSummary(planPin,ctx.invocation));
    if(mode==='prepare-only')return {exitCode:0,...preparationSummary(planPin,ctx.invocation)};
    const command={schemaVersion:1,program:ctx.stage.dotnet,args:ctx.invocation.dotnetArguments,cwd:ctx.invocation.ownedRoot,
      budgetMs:BUSINESS_BUDGET_MS,plan:planPin,review:ctx.stage.review,policy:ctx.invocation.policy,preload:ctx.stage.preloadReceipt,
      outputs:{launch:ctx.invocation.launchReceipt,business:ctx.invocation.firstOldArgs[4]},ownedOnly:true};
    put(path.join(out,'COMMAND.json'),command);
    recheck(ctx);
    for(const p of [command.outputs.launch,command.outputs.business])need(!fs.existsSync(p),'FIRST_OLD_FRESH_OUTPUT_REQUIRED',{path:p});
    const started=Date.now();producerExecuted=true;
    const child=spawnSync(command.program.path,command.args,{cwd:command.cwd,env:cleanEnvironment(command.program),shell:false,windowsHide:true,encoding:'utf8',timeout:BUSINESS_BUDGET_MS,maxBuffer:8*1024*1024});
    put(path.join(out,'stdout.log'),child.stdout??'');put(path.join(out,'stderr.log'),child.stderr??'');
    const outputPin=p=>fs.existsSync(p)?{path:p,sha256:fileHash(p)}:null;
    const processReceipt={exitCode:child.status,signal:child.signal,error:child.error?.message??null,durationMs:Date.now()-started,
      naturalExit:child.status!==null&&!child.error&&!child.signal,timeout:child.error?.code==='ETIMEDOUT',
      launch:outputPin(command.outputs.launch),result:outputPin(command.outputs.business)};
    put(path.join(out,'PROCESS.json'),processReceipt);
    const business=processReceipt.result?read(command.outputs.business):null,launch=processReceipt.launch?read(command.outputs.launch):null;
    const assessed=assessBusinessResult({business,launch,processReceipt,allowance:ctx.allowance,invocation:ctx.invocation});
    for(const pin of ctx.pins){let unchanged=false;try{checkedPin(pin);unchanged=true;}catch{}assessed.checks.push({id:'unchanged-input:'+pin.path,passed:unchanged,expectedSha256:pin.sha256});}
    for(const row of launch?.actualLoadedManaged??[]){let exact=false;try{checkedPin(row);exact=true;}catch{}assessed.checks.push({id:'actual-managed-file-sha:'+row.path,passed:exact});}
    const passed=assessed.checks.every(row=>row.passed),summary={schemaVersion:1,status:passed?'PASS_FIRST_OLD_DEFECT_REPRODUCED_WITH_SYNTHETIC_HC':'FAIL_FIRST_OLD_STAGE_PRESERVED',
      passed,producerExecuted:true,plan:planPin,review:ctx.stage.review,process:{path:path.join(out,'PROCESS.json'),sha256:fileHash(path.join(out,'PROCESS.json'))},
      launch:processReceipt.launch,business:processReceipt.result,caseCount:business?.cases?.length??0,originalChecks:business?.checks?.length??0,
      nativeAttempts:business?.nativeAttempts??null,checks:assessed.checks,managedGraph:assessed.graph,sourceApprovalGranted:false,fullHostApproved:false,
      fullFanWorkflowPassed:false,currentHostPassed:false,physicalAcceptance:false,wholeGoalComplete:false,jointUserTestingReady:false,cpuPowerBenefit:null};
    put(path.join(out,'SUMMARY.json'),summary);return {exitCode:passed?0:2,...summary};
  } catch(error) {
    const failure={schemaVersion:1,status:producerExecuted?'FAIL_FIRST_OLD_STAGE_PRESERVED':'BLOCKED_NOT_EXECUTED',passed:false,producerExecuted,
      code:error.code??'FIRST_OLD_STAGE_ERROR',detail:error.detail??null,error:error.message,plan:planPin,sourceApprovalGranted:false,fullHostApproved:false,wholeGoalComplete:false};
    put(path.join(out,'FAILURE.json'),failure);return {exitCode:producerExecuted?2:3,...failure};
  }
}
export function parseStageArguments(args) {
  equal(args.filter((_,i)=>i%2===0),['--mode','--plan','--sha256','--evidence-dir'],'FIRST_OLD_EXACT_CLI_ARGUMENTS');
  need(args.length===8&&['prepare-only','run','rebind-review'].includes(args[1]),'FIRST_OLD_EXPLICIT_MODE_REQUIRED');
  const options={mode:args[1],planPin:{path:args[3],sha256:args[5]},evidenceDir:args[7]};pinShape(options.planPin);return options;
}
if(process.argv[1]&&same(process.argv[1],fileURLToPath(import.meta.url))) {
  try {const options=parseStageArguments(process.argv.slice(2));const result=options.mode==='rebind-review'?rebindFirstOldReview(options):await executeFirstOldStage(options);console.log(JSON.stringify(result));process.exitCode=result.exitCode;}
  catch(error){console.error(JSON.stringify({status:'BLOCKED_NOT_EXECUTED',passed:false,producerExecuted:false,code:error.code??'FIRST_OLD_STAGE_ERROR',detail:error.detail??null,error:error.message}));process.exitCode=3;}
}

function normalizedOwnedPath(root,p){return inside(root,p)?'$OWNED/'+path.relative(root,p).replaceAll('\\','/').toLowerCase():p;}
function normalizedRebindPolicy(q,p){
  const value=structuredClone(q),map=file=>normalizedOwnedPath(p.ownedRoot,file);
  value.ownedRoot='$OWNED';value.entry=map(value.entry);
  for(const c of value.contexts){
    if(c.registrationAssembly)c.registrationAssembly.path=map(c.registrationAssembly.path);
    for(const row of c.managed)row.path=map(row.path);
    for(const request of c.requests)request.targetPath=map(request.targetPath);
  }
  for(const row of value.metadata){
    if(same(row.path,p.argumentMetadata.path)){row.path='$ARGUMENT_METADATA';row.sha256='$ARGUMENT_METADATA_SHA';}
    else row.path=map(row.path);
  }
  return value;
}
export function validateReviewRebinding(oldContext,next,nextPolicy,nextArgvMetadata){
  const old=oldContext.invocation;
  need(next.schemaVersion===1&&next.status===old.status&&next.originalBusinessBudgetMs===BUSINESS_BUDGET_MS,'FIRST_OLD_REBIND_PREPARED_PLAN_REQUIRED');
  need(!same(next.ownedRoot,old.ownedRoot),'FIRST_OLD_REBIND_NEW_OWNED_ROOT_REQUIRED');
  for(const key of ['originalInventory','originalInvocation','originalPolicy','originalArgumentMetadata'])samePin(next[key],old[key],'FIRST_OLD_REBIND_ORIGINAL_INPUT_CHANGED');
  equal(next.firstOldArgs.slice(0,4),old.firstOldArgs.slice(0,4),'FIRST_OLD_REBIND_BUSINESS_ARGUMENT_CHANGED');
  equal(normalizedOwnedPath(next.ownedRoot,next.firstOldArgs[4]),normalizedOwnedPath(old.ownedRoot,old.firstOldArgs[4]),'FIRST_OLD_REBIND_RESULT_LOCATION_CHANGED');
  equal(normalizedOwnedPath(next.ownedRoot,next.launchReceipt),normalizedOwnedPath(old.ownedRoot,old.launchReceipt),'FIRST_OLD_REBIND_LAUNCH_LOCATION_CHANGED');
  const outputMap=(p)=>p.outputs.map(row=>({relative:normalizedOwnedPath(p.ownedRoot,row.path),original:row.original,sha256:row.sha256})).sort((a,b)=>a.relative.localeCompare(b.relative,'en'));
  equal(outputMap(next),outputMap(old),'FIRST_OLD_REBIND_OUTPUT_BYTES_OR_SET_CHANGED');
  for(const key of ['producer','launcher']){need(next[key].sha256===old[key].sha256,'FIRST_OLD_REBIND_ENTRY_BYTES_CHANGED');equal(normalizedOwnedPath(next.ownedRoot,next[key].path),normalizedOwnedPath(old.ownedRoot,old[key].path),'FIRST_OLD_REBIND_ENTRY_LOCATION_CHANGED');}
  equal(normalizedRebindPolicy(nextPolicy,next),normalizedRebindPolicy(oldContext.policy,old),'FIRST_OLD_REBIND_POLICY_SEMANTICS_CHANGED');
  equal(nextArgvMetadata.args,next.firstOldArgs,'FIRST_OLD_REBIND_ARGUMENT_METADATA_CHANGED');
  need(nextArgvMetadata.argvSha256===digest(next.firstOldArgs),'FIRST_OLD_REBIND_ARGUMENT_METADATA_SHA');
  const normalizeArg=(a,p)=>({...a,producer:normalizedOwnedPath(p.ownedRoot,a.producer),args:a.args.map((v,i)=>i===4?normalizedOwnedPath(p.ownedRoot,v):v),output:normalizedOwnedPath(p.ownedRoot,a.output),argvSha256:'$ARGV_SHA'});
  equal(normalizeArg(nextArgvMetadata,next),normalizeArg(oldContext.argvMetadata,old),'FIRST_OLD_REBIND_ARGUMENT_METADATA_SEMANTICS_CHANGED');
  return true;
}
export function rebindFirstOldReview({planPin,evidenceDir}){
  checkedPin(planPin);const rebind=read(planPin.path);
  equal(Object.keys(rebind).sort(),['schemaVersion','kind','fromStage','toInvocation','preloadStep','preloadReceipt','preloadProcess','dotnet'].sort(),'FIRST_OLD_REBIND_ENVELOPE_KEYS');
  need(rebind.schemaVersion===1&&rebind.kind==='onex-first-old-review-rebind','FIRST_OLD_REBIND_PLAN_SHAPE');
  const old=loadStageContract(rebind.fromStage);
  for(const pin of [rebind.toInvocation,rebind.preloadStep,rebind.preloadReceipt,rebind.preloadProcess,rebind.dotnet])checkedPin(pin);
  samePin(rebind.dotnet,old.stage.dotnet,'FIRST_OLD_REBIND_DOTNET_CHANGED');
  const next=read(rebind.toInvocation.path);checkedPin(next.policy);checkedPin(next.argumentMetadata);
  const nextPolicy=read(next.policy.path),nextArgv=read(next.argumentMetadata.path);
  validateReviewRebinding(old,next,nextPolicy,nextArgv);
  for(const row of next.outputs)checkedPin(row);
  assertFreshOutputs(next,evidenceDir);
  need(old.protectedRoots.every(root=>!inside(root,evidenceDir)&&!inside(evidenceDir,root)&&!inside(root,next.ownedRoot)&&!inside(next.ownedRoot,root)),'FIRST_OLD_REBIND_OVERLAPS_SOURCE');
  fs.mkdirSync(evidenceDir);
  const review={...old.review,plan:rebind.toInvocation,policy:next.policy,
    branchLimits:{...old.review.branchLimits,argv:next.firstOldArgs},
    rebind:{kind:'EXACT_BYTES_AND_LOCATION_ONLY',atUtc:new Date().toISOString(),fromStage:rebind.fromStage,fromReview:old.stage.review,
      originalReviewUnchanged:true,originalMethodAndTerminalReviewRetained:true,sourceApprovalGranted:false,producerExecuted:false}};
  const reviewPath=path.join(evidenceDir,'ONE-INVOCATION-REVIEW.json');put(reviewPath,review);
  const envelope={schemaVersion:1,kind:'onex-first-old-stage',invocation:rebind.toInvocation,review:{path:reviewPath,sha256:fileHash(reviewPath)},
    preloadStep:rebind.preloadStep,preloadReceipt:rebind.preloadReceipt,preloadProcess:rebind.preloadProcess,dotnet:rebind.dotnet,frameworkMetadata:old.stage.frameworkMetadata};
  const stagePath=path.join(evidenceDir,'FIRST-OLD-STAGE-PLAN.json');put(stagePath,envelope);
  // Full exact-pin validation is repeated on the generated envelope, without
  // calling MTA or target code. A failure is retained rather than relabelled PASS.
  const bound=loadStageContract({path:stagePath,sha256:fileHash(stagePath)});
  need(bound.allowance.missingBaseClr.length===0,'FIRST_OLD_REBIND_MISSING_BASE_CLR',{missing:bound.allowance.missingBaseClr});
  recheck(old);recheck(bound);
  const result={schemaVersion:1,status:'REBOUND_PREPARED_NOT_EXECUTED',passed:false,producerExecuted:false,
    sourceApprovalGranted:false,executionApprovalGranted:false,fullHostApproved:false,plan:{path:stagePath,sha256:fileHash(stagePath)},
    review:envelope.review,fromStage:rebind.fromStage,exactOutputCount:next.outputs.length,preloadDefaultPinCount:bound.preload.defaultPinCount};
  put(path.join(evidenceDir,'REBIND-RESULT.json'),result);return {exitCode:0,...result};
}
