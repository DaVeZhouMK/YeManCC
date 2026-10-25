import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import test from 'node:test';import assert from 'node:assert/strict';import {fileURLToPath} from 'node:url';
import {MSI_MANUAL_PROFILE,projectMsiManualMODEL_ONLY,inspectMsiManualInputs} from './prepare-msi-manual.mjs';
const W=path.win32,dir=path.dirname(fileURLToPath(import.meta.url)),root=path.resolve(dir,'../../../../../'),read=f=>JSON.parse(fs.readFileSync(f,'utf8'));
const B=read(path.join(dir,'msi-manual-basis.json')),model=read(path.join(dir,'models/msi-manual/msi-manual-model.json')),clone=v=>structuredClone(v),sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const same=(a,b)=>W.normalize(a).toLowerCase()===W.normalize(b).toLowerCase();
const fixture=role=>{const v=clone(model);v.input.role=role;v.fault=v.faults[role]??null;return v;};
function refuse(name,fn,code,role='positive'){test('MODEL_ONLY rejects '+name,()=>{const v=fixture(role);fn(v);assert.throws(()=>projectMsiManualMODEL_ONLY(v),e=>e.message===code);});}
for(const role of B.roles){
 test('MODEL_ONLY '+role+' original44 contract and role-specific image ownership',()=>{
  const v=fixture(role),p=projectMsiManualMODEL_ONLY(v);assert.equal(p.MODEL_ONLY,true);assert.equal(p.evidenceKind,'MODEL_ONLY');assert.deepEqual(p.expectedCaseIds,B.ids);assert.deepEqual(p.expectedFailureMask,B.failureMasks[role]);assert.deepEqual(p.expectedMarkers,B.markers[role]);assert.deepEqual(p.budgets,{business:90000,parent:360000,generation:30000,compile:120000});assert.equal(p.expectedExitCode,role==='positive'?0:2);assert.equal(p.hc.sha256,role==='preservation'?v.fault.summary.target.sha256:v.base.hc.sha256);assert.equal(p.host.sha256,role==='positive'||role==='preservation'?v.host.summary.target.sha256:v.fault.summary.target.sha256);assert.equal(p.files.length,115);assert.equal(Object.keys(p.manifest).length,116);assert.equal(p.expectedMsiManualContract.caseCount,44);assert.equal(p.expectedMsiManualContract.positivePublicAttempts,52);assert.equal(p.expectedMsiManualContract.positivePublicSuccesses,21);assert.equal(p.expectedMsiManualContract.openCycles,44);assert.equal(p.expectedMsiManualContract.ownedStaLifecycles,44);assert.equal(p.expectedMsiManualContract.readonlyOsIdentityCalls,44);assert.equal(p.expectedMsiManualContract.checks,'UNKNOWN_UNTIL_ACTUAL_EXECUTION');assert.deepEqual(p.expectedMsiManualContract.types,B.types);assert.equal(p.scope.appliesTo,'canonical-reviewed-host3-before-original-fault');if(role==='positive')assert.equal(p.selectedOriginalFault,null);else {assert.equal(p.selectedOriginalFault.role,role);assert.deepEqual(p.selectedOriginalFault.changedMethods,v.fault.proof.changedMethods);assert.equal(p.selectedOriginalFault.targetKind,role==='preservation'?'hc':'host');}assert.equal(p.targetExecutions,0);assert.equal(p.executionApprovalGranted,false);assert.equal(p.metadataApprovalGranted,false);
 });
 test('MODEL_ONLY '+role+' full inverse restores every original descriptor/request/alias/manifest',()=>{
  const v=fixture(role),p=projectMsiManualMODEL_ONLY(v),mapping=new Map(p.files.map(f=>[W.normalize(f.destination).toLowerCase(),f.original.path]));
  const back=f=>mapping.get(W.normalize(f).toLowerCase())??f;
  const restore=t=>({...t,path:back(t.path),...(same(t.path,p.host.path)?{sha256:v.base.host.sha256}:{}),...(same(t.path,p.hc.path)?{sha256:v.base.hc.sha256}:{})});
  const policy=clone(p.policy);policy.ownedRoot=v.policy.ownedRoot;policy.entry=back(policy.entry);policy.contexts[0].managed=policy.contexts[0].managed.map(restore);policy.contexts[0].requests=policy.contexts[0].requests.map(t=>({...t,targetPath:back(t.targetPath)}));policy.exactAliases=policy.exactAliases.map(t=>({...t,provider:restore(t.provider)}));assert.deepEqual(policy,v.policy);
  const guard={};for(const[f,s]of Object.entries(p.manifest))guard[back(f)]=same(f,p.host.path)?v.base.host.sha256:same(f,p.hc.path)?v.base.hc.sha256:s;assert.deepEqual(guard,v.manifest);
  const changes=p.files.filter(f=>f.source.sha256!==f.original.sha256);assert.equal(changes.length,role==='preservation'?2:1);for(const f of p.files){assert.equal(same(f.destination,W.join(p.ownedRoot,W.relative(v.base.ownedRoot,f.original.path))),true);assert.equal(f.original.sha256,v.manifest[f.original.path]);}
 });
 test('MODEL_ONLY '+role+' all nine argv and framework native exclusion',()=>{
  const v=fixture(role),p=projectMsiManualMODEL_ONLY(v),r=p.ownedRoot;
  assert.deepEqual(p.args,[p.host.path,p.hc.path,W.join(r,'runtime'),W.join(r,'shared'),W.join(r,'runtime-pins.json'),W.join(r,'inputs','inventory.json'),W.join(r,'inputs','YeManFanHost.json'),W.join(r,'Build','msi-manual',role,'case','result.json'),'msi-manual']);
  for(const m of p.policy.contexts[0].managed.filter(t=>t.role==='framework'))assert.equal(Object.keys(p.manifest).some(f=>same(f,m.path)),false);
  assert.equal(p.policy.contexts.length,1);assert.equal(p.policy.contexts[0].id,'Default');assert.equal(p.policy.contexts[0].managed.length,92);assert.equal(p.policy.exactAliases.length,6);assert.equal(p.scope.originalSentryModuleInitializerPreserved,true);assert.equal(p.scope.ownedBuildPayloadWritesRemain,true);assert.equal(p.scope.freshChildManualEnabledDefaultFalse,true);assert.equal(p.scope.backgroundReplayExecuted,false);assert.equal(p.scope.nativeIoZeroClaim,false);
 });
}
const cases=[
 ['unknown profile',v=>v.input.profile='default-current','MANUAL_EXPLICIT_PROFILE_ROLE'],
 ['unknown role',v=>v.input.role='replay','MANUAL_EXPLICIT_PROFILE_ROLE'],
 ['missing qualification',v=>v.input.derivativeQualificationPin=null,'MANUAL_QUALIFICATION_REQUIRED'],
 ['caller business budget override',v=>v.input.budgets={business:120000},'MANUAL_INPUT_OVERRIDE_FORBIDDEN'],
 ['caller permissions override',v=>v.input.hardwareIo=true,'MANUAL_INPUT_OVERRIDE_FORBIDDEN'],
 ['old83 premise pin',v=>v.input.acceptedMsiLifecycle92.summaryPin.sha256='0'.repeat(64),'MANUAL_EXACT_ACCEPTED_LIFECYCLE_REQUIRED'],
 ['wrong joint pin',v=>v.input.acceptedMsiLifecycle92.jointReviewPin.sha256='0'.repeat(64),'MANUAL_EXACT_ACCEPTED_LIFECYCLE_REQUIRED'],
 ['UNC stage',v=>v.input.stageRoot='\\\\server\\share\\stage','MANUAL_LOCAL_OWNED_PATH'],
 ['ADS stage',v=>v.input.stageRoot=v.input.stageRoot+':stream','MANUAL_LOCAL_OWNED_PATH'],
 ['path traversal',v=>v.input.stageRoot=v.input.ownedParent+'/../escape','MANUAL_LOCAL_OWNED_PATH'],
 ['nested non-direct child',v=>v.input.stageRoot=v.input.ownedParent+'/nested/child','MANUAL_FRESH_DIRECT_CHILD'],
 ['parent root reuse',v=>{v.input.stageRoot=v.base.ownedRoot;v.input.ownedParent=W.dirname(v.base.ownedRoot);},'MANUAL_DISTINCT_FRESH_ROOT'],
 ['pending review qualification',v=>v.qualification.status='REVIEW_PENDING','MANUAL_REVIEWED_QUALIFICATION'],
 ['native permission addition',v=>v.qualification.nativeDevicePermissionAdded=true,'MANUAL_REVIEWED_QUALIFICATION'],
 ['different generation aggregate',v=>v.qualification.hostGenerationPin.sha256='0'.repeat(64),'MANUAL_REVIEWED_QUALIFICATION'],
 ['different inventory aggregate',v=>v.qualification.inventoryGenerationPin.sha256='0'.repeat(64),'MANUAL_REVIEWED_QUALIFICATION'],
 ['different inventory source',v=>v.qualification.inventory.resultPin.sha256='0'.repeat(64),'MANUAL_REVIEWED_QUALIFICATION'],
 ['no review evidence',v=>v.qualification.reviewEvidencePins=[],'MANUAL_REVIEW_EVIDENCE'],
 ['nonactual typed review',v=>v.review.evidenceKind='SOURCE_ONLY','MANUAL_ACTUAL_TYPED_REVIEW'],
 ['review target execution permission',v=>v.review.targetCodeExecuted=true,'MANUAL_ACTUAL_TYPED_REVIEW'],
 ['parent role is negative',v=>v.base.role='binding','MANUAL_PARENT_POSITIVE_PLAN'],
 ['wrong parent HC image',v=>v.base.hc.sha256='0'.repeat(64),'MANUAL_PARENT_POSITIVE_PLAN'],
 ['parent graphfail summary',v=>v.acceptance.status='BUSINESS_ONLY_GRAPHFAIL','MANUAL_ACTUAL_PARENT_ACCEPTANCE'],
 ['unknown managed parent',v=>v.acceptance.unknownManaged=1,'MANUAL_ACTUAL_PARENT_ACCEPTANCE'],
 ['denied native parent',v=>v.acceptance.deniedNative=1,'MANUAL_ACTUAL_PARENT_ACCEPTANCE'],
 ['parent forced cleanup',v=>v.acceptance.forcedThreadCleanup=1,'MANUAL_ACTUAL_PARENT_ACCEPTANCE'],
 ['parent declared business90000',v=>v.acceptance.originalBusinessBudgetMs=90000,'MANUAL_ACTUAL_PARENT_ACCEPTANCE'],
 ['parent process old null expiry',v=>v.parentProcess.expired=null,'MANUAL_NATURAL_TERMINAL'],
 ['parent process business90000 confusion',v=>v.parentProcess.budgetMs=90000,'MANUAL_NATURAL_TERMINAL'],
 ['parent duration over60000',v=>v.parentProcess.durationMs=60001,'MANUAL_NATURAL_TERMINAL'],
 ['parent signal',v=>v.parentProcess.signal='SIGTERM','MANUAL_NATURAL_TERMINAL'],
 ['parent nonzero',v=>v.parentProcess.code=2,'MANUAL_NATURAL_TERMINAL'],
 ['parent shell launch',v=>v.parentProcess.shell=true,'MANUAL_NATURAL_TERMINAL'],
 ['metadata original budget changed',v=>v.metadataProcess.budgetMs=90000,'MANUAL_NATURAL_TERMINAL'],
 ['preload expiry',v=>v.preloadProcess.expired=true,'MANUAL_NATURAL_TERMINAL'],
 ['parent actual cwd altered',v=>v.parentProcess.cwd='D:/wrong','MANUAL_PARENT_ACTUAL_ARGV'],
 ['parent actual runtime argv altered',v=>v.parentProcess.args[10]='D:/wrong','MANUAL_PARENT_ACTUAL_ARGV'],
 ['parent graph unknown image',v=>v.graph.unknownFiles.push({path:'D:/unknown.dll'}),'MANUAL_PARENT_EXACT_GRAPH'],
 ['graph launch mismatch',v=>v.launch.actualLoadedManaged=[],'MANUAL_PARENT_EXACT_GRAPH'],
 ['parent result reordered IDs',v=>v.parentResult.cases.reverse(),'MANUAL_REAL_PARENT_BUSINESS'],
 ['parent result lost case',v=>v.parentResult.cases.pop(),'MANUAL_REAL_PARENT_BUSINESS'],
 ['parent result failure',v=>v.parentResult.cases[0].status='FAIL','MANUAL_REAL_PARENT_BUSINESS'],
 ['parent thread leak',v=>v.parentResult.threadEvidence[0].allOwnedThreadsExited=false,'MANUAL_REAL_PARENT_BUSINESS'],
 ['parent wrong types',v=>v.parentResult.uniqueConstructedTypes[0]='Wrong.Type','MANUAL_REAL_PARENT_BUSINESS'],
 ['parent readonly call missing',v=>v.parentResult.readOnlyOsThreadIdentityCalls=22,'MANUAL_REAL_PARENT_BUSINESS'],
 ['parent hardware flag',v=>v.parentResult.hardwareIo=true,'MANUAL_REAL_PARENT_BUSINESS'],
 ['parent manager start',v=>v.parentResult.managerStartCalled=true,'MANUAL_REAL_PARENT_BUSINESS'],
 ['parent Host Main',v=>v.parentResult.hostMainStarted=true,'MANUAL_REAL_PARENT_BUSINESS'],
 ['parent BaseOpen unexecuted',v=>v.parentResult.actualOriginalMsiBaseOpenExecuted=false,'MANUAL_REAL_PARENT_BUSINESS'],
 ['parent joint not accepted',v=>v.joint.MSILifecycleSubrouteAccepted=false,'MANUAL_INDEPENDENT_PARENT_JOINT'],
 ['parent joint Sentry removed',v=>v.joint.originalSentryModuleInitializerRemains=false,'MANUAL_INDEPENDENT_PARENT_JOINT'],
 ['parent joint zero IO claim',v=>v.joint.nativeIoZeroClaim=true,'MANUAL_INDEPENDENT_PARENT_JOINT'],
 ['parent budget mutated',v=>v.base.budgets.parent=360001,'MANUAL_PARENT_BUDGET_UNKNOWN_NATIVE259'],
 ['initialize invented',v=>v.base.initializeRole='PASS','MANUAL_PARENT_BUDGET_UNKNOWN_NATIVE259'],
 ['native boundary changed',v=>v.base.nativeBoundaryLength=260,'MANUAL_PARENT_BUDGET_UNKNOWN_NATIVE259'],
 ['policy new context',v=>v.policy.contexts.push(clone(v.policy.contexts[0])),'MANUAL_EXACT_SCHEMA6_POLICY'],
 ['unreviewed System.Management provider',v=>v.policy.contexts[0].managed.push({path:'D:/unreviewed/System.Management.dll',sha256:'1'.repeat(64),role:'target',identity:'System.Management, Version=10.0.0.0, Culture=neutral, PublicKeyToken=b03f5f7f11d50a3a'}),'MANUAL_EXACT_SCHEMA6_POLICY'],
 ['framework role masquerade',v=>v.policy.contexts[0].managed.find(x=>x.role==='framework').role='target','MANUAL_EXACT_SCHEMA6_POLICY'],
 ['alias request widened',v=>v.policy.exactAliases[0].requestingContexts.push('Product'),'MANUAL_EXACT_SCHEMA6_POLICY'],
 ['alias provider hash changed',v=>v.policy.exactAliases[0].provider.sha256='1'.repeat(64),'MANUAL_EXACT_SCHEMA6_POLICY'],
 ['metadata directory substitute',v=>v.policy.metadata.push({path:'D:/all',sha256:'1'.repeat(64)}),'MANUAL_EXACT_SCHEMA6_POLICY'],
 ['device guard extra native',v=>v.manifest['D:/user32.dll']='1'.repeat(64),'MANUAL_EXACT_GUARD116'],
 ['device guard lost target',v=>delete v.manifest[v.base.hc.path],'MANUAL_EXACT_GUARD116'],
 ['artifact authority changed',v=>v.base.artifacts.authority.sha256='1'.repeat(64),'MANUAL_EXACT_ARTIFACTS'],
 ['runtime directory argv old root',v=>{v.base.args[2]='D:/wrong/runtime';v.parentProcess.args[7]='D:/wrong/runtime';},'MANUAL_PARENT_ARGV_DIRS'],
 ['argv hash mismatch',v=>v.argv.argvSha256='0'.repeat(64),'MANUAL_PARENT_ARGV_DIRS'],
 ['case oracle shortened',v=>v.oracle.ids.pop(),'MANUAL_ORIGINAL44_ORACLE_BUDGETS'],
 ['case oracle reordered',v=>v.oracle.ids.reverse(),'MANUAL_ORIGINAL44_ORACLE_BUDGETS'],
 ['mask relaxed',v=>v.oracle.failureMasks.owner=[],'MANUAL_ORIGINAL44_ORACLE_BUDGETS'],
 ['marker relaxed',v=>v.oracle.markers['axis-endpoint']=[],'MANUAL_ORIGINAL44_ORACLE_BUDGETS'],
 ['manual90000 changed to60000',v=>v.oracle.budgets.business=60000,'MANUAL_ORIGINAL44_ORACLE_BUDGETS'],
 ['manual parent budget enlarged',v=>v.oracle.budgets.parent=400000,'MANUAL_ORIGINAL44_ORACLE_BUDGETS'],
 ['host generator timeout',v=>v.host.process.expired=true,'MANUAL_NATURAL_TERMINAL'],
 ['host generator30000 changed',v=>v.host.process.budgetMs=60000,'MANUAL_NATURAL_TERMINAL'],
 ['host wrong CLI',v=>v.host.summary.cli='msi-replay-host-ports','MANUAL_ORIGINAL_HOST3_GENERATION'],
 ['host wrong producer',v=>v.host.aggregate.producer.sha256='1'.repeat(64),'MANUAL_ORIGINAL_HOST3_GENERATION'],
 ['host nonexact managed graph',v=>v.host.summary.exactManagedGraphVerified=false,'MANUAL_ORIGINAL_HOST3_GENERATION'],
 ['host wrong target',v=>v.host.summary.target.sha256='1'.repeat(64),'MANUAL_ORIGINAL_HOST3_GENERATION'],
 ['host source not lifecycle image',v=>v.host.summary.source.sha256='1'.repeat(64),'MANUAL_ORIGINAL_HOST3_GENERATION'],
 ['host fourth changed method',v=>v.host.proof.changedMethods.push('System.Void Extra::Run()'),'MANUAL_HOST3_PROOF'],
 ['host power not isolated',v=>v.host.proof.physicalPowerReadIsolated=false,'MANUAL_HOST3_PROOF'],
 ['host proof IO claim',v=>v.host.proof.hardwareIo=true,'MANUAL_HOST3_PROOF'],
 ['HC typed review wrong SHA',v=>v.review.hcPin.sha256='1'.repeat(64),'MANUAL_TYPED_REVIEW_IMAGE_BINDING'],
 ['Ports SHA changed',v=>v.review.portsSHA256='1'.repeat(64),'MANUAL_TYPED_REVIEW_IMAGE_BINDING'],
 ['review changed method order differs proof',v=>v.review.hostChangedMethods.reverse(),'MANUAL_TYPED_REVIEW_IMAGE_BINDING'],
 ['review approves replay',v=>v.review.backgroundReplayExecuted=true,'MANUAL_NO_REPLAY_OR_DECODER_ACCEPTANCE'],
 ['review approves raw decoder',v=>v.review.rawWmiDecoderAccepted=true,'MANUAL_NO_REPLAY_OR_DECODER_ACCEPTANCE'],
 ['inventory source mismatch',v=>v.inventory.sourceSha256='1'.repeat(64),'MANUAL_INVENTORY_SOURCE_AND_METHODS'],
 ['inventory executed assembly',v=>v.inventory.assembliesExecuted=true,'MANUAL_INVENTORY_SOURCE_AND_METHODS'],
 ['inventory missing consumer',v=>v.inventory.methods.pop(),'MANUAL_INVENTORY_SOURCE_AND_METHODS'],
 ['inventory process wrong CLI',v=>v.inventoryProcess.args[5]='msi-manual-replay','MANUAL_INVENTORY_ORIGINAL_CLI'],
 ['native root too long',v=>{v.input.ownedParent='D:/'+('a'.repeat(220));v.input.stageRoot=v.input.ownedParent+'/stage';},'MANUAL_NATIVE259_ROOT']
];for(const c of cases)refuse(...c);
for(const key of ['wholeMethodSetsVerified','originalBaseOpenAndStructuredReleasePreserved','originalPublicApplyCurveAndTableBitWritersPreserved','bothTimerArmsOnlyOperandChangesInverseVerified','physicalPowerIoOnlyTerminalReplaced','originalSentryModuleInitializerPreserved','ownedBuildPayloadWritesRemain','freshChildManualEnabledDefaultFalse','hostNoRegistryPnpGeneralOpenAdded'])refuse('missing exact scope '+key,v=>v.review[key]=false,'MANUAL_REVIEW_SCOPE_'+key);
for(const key of ['publicEntry','originalPublicLoadAndHandshakeExecuted','actualStaMessagePumpExecuted','actualDefaultAssemblyLoadExecuted','actualOriginalGetCurrentAndConstructorsExecuted','actualConfigureAndNativeAdmissionExecuted','actualIdentityCapabilityAndAuthorizationExecuted'])refuse('parent skipped original '+key,v=>v.parentResult[key]=false,'MANUAL_REAL_PARENT_BUSINESS');
for(const role of B.roles.filter(x=>x!=='positive')){
 refuse(role+' no raw review binding',v=>v.review.derivedFrom=[],'MANUAL_ACTUAL_TYPED_REVIEW',role);
 refuse(role+' wrong source family',v=>v.fault.summary.source.sha256='1'.repeat(64),'MANUAL_ORIGINAL_NEGATIVE_PROCESS',role);
 refuse(role+' changed two methods',v=>v.fault.proof.changedMethods.push('Extra'),'MANUAL_ORIGINAL_SINGLE_METHOD_FAULT',role);
 refuse(role+' no whole methodset proof',v=>v.review.variants.find(x=>x.role===role).wholeMethodSetVerified=false,'MANUAL_VARIANT_EXACT_IMAGE_METHODSET_MASK',role);
 refuse(role+' role-kind mismatch',v=>v.review.variants.find(x=>x.role===role).targetKind=role==='preservation'?'host':'hc','MANUAL_VARIANT_EXACT_IMAGE_METHODSET_MASK',role);
 refuse(role+' wrong original failure mask',v=>v.review.variants.find(x=>x.role===role).expectedFailureMask=[],'MANUAL_VARIANT_EXACT_IMAGE_METHODSET_MASK',role);
}
test('MODEL_ONLY obsolete preparation/report flags do not revoke exact actual parent receipts',()=>{const v=fixture('positive');v.base.sourceModelsPendingWorker=true;v.host.aggregate.publicFactoryStillUnaccepted=true;v.acceptance.sourceModelsPendingWorker=true;assert.equal(projectMsiManualMODEL_ONLY(v).expectedCaseIds.length,44);});
test('MODEL_ONLY cannot be mislabeled as actual source model API',()=>{const v=fixture('positive');delete v.MODEL_ONLY;assert.throws(()=>projectMsiManualMODEL_ONLY(v),/MANUAL_MODEL_LABEL_REQUIRED/);});
test('actual inspect rejects MODEL_ONLY options before file access',()=>{assert.throws(()=>inspectMsiManualInputs({MODEL_ONLY:true,inputsPin:{path:'D:/not-read.json',sha256:'0'.repeat(64)}}),/MANUAL_MODEL_NOT_ACTUAL/);});
for(const [name,code]of [['missing-qualification','MANUAL_QUALIFICATION_REQUIRED'],['wrong-accepted','MANUAL_EXACT_ACCEPTED_LIFECYCLE_REQUIRED'],['unreviewed-qualification','MANUAL_COMPLETE_DERIVATIVE_QUALIFICATION'],['model-qualification','MANUAL_MODEL_NOT_ACTUAL']])test('actual reader rejects '+name+' before stage writes',()=>{const f=path.join(dir,'models/msi-manual/refusal-'+name+'.json');const pin={path:f,sha256:sha(fs.readFileSync(f))};const i=read(f);assert.equal(fs.existsSync(i.stageRoot),false);assert.throws(()=>inspectMsiManualInputs({inputsPin:pin}),e=>e.message===code);assert.equal(fs.existsSync(i.stageRoot),false);});
test('source oracle is still bound to original G bytes',()=>{for(const p of B.sourcePins)assert.equal(sha(fs.readFileSync(p.path)),p.sha256);});
test('runtime helper has no producer/process execution entry',()=>{const source=fs.readFileSync(path.join(dir,'prepare-msi-manual.mjs'),'utf8');assert.doesNotMatch(source,/child_process|execSync|spawnSync|\.spawn\(|\.exec\(/);assert.equal(MSI_MANUAL_PROFILE,B.profile);});
