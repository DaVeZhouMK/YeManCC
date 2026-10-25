import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import test from 'node:test';import assert from 'node:assert/strict';import {fileURLToPath} from 'node:url';
import {MSI_REPLAY_PROFILE,projectMsiReplayMODEL_ONLY,inspectMsiReplayInputs,inspectMsiReplayParentInputs} from './prepare-msi-replay.mjs';
const W=path.win32,dir=path.dirname(fileURLToPath(import.meta.url)),read=f=>JSON.parse(fs.readFileSync(f,'utf8')),B=read(dir+'/msi-replay-basis.json'),model=read(dir+'/models/msi-replay/msi-replay-model.json');
const clone=v=>structuredClone(v),sha=b=>crypto.createHash('sha256').update(b).digest('hex'),same=(a,b)=>W.normalize(a).toLowerCase()===W.normalize(b).toLowerCase();
const fixture=role=>{const v=clone(model);v.input.role=role;v.fault=v.faults[role]??null;return v;};
function refusal(name,mutate,error,role='positive'){test('MODEL_ONLY rejects '+name,()=>{const v=fixture(role);mutate(v);assert.throws(()=>projectMsiReplayMODEL_ONLY(v),e=>e.message===error);});}
for(const role of B.roles){
 test('MODEL_ONLY '+role+' exact26 role contract and unchangedHC',()=>{const v=fixture(role),p=projectMsiReplayMODEL_ONLY(v);assert.equal(p.MODEL_ONLY,true);assert.equal(p.evidenceKind,'MODEL_ONLY');assert.deepEqual(p.expectedCaseIds,B.ids);assert.deepEqual(p.expectedFailureMask,B.failureMasks[role]);assert.deepEqual(p.expectedMarkers,B.markers[role]);assert.equal(p.host.sha256,role==='positive'?B.replayHost.sha256:v.fault.summary.target.sha256);assert.equal(p.hc.sha256,B.parentHc.sha256);assert.deepEqual(p.budgets,{business:90000,parent:360000,generation:30000,compile:120000});assert.equal(p.expectedExitCode,role==='positive'?0:2);assert.equal(p.files.length,115);assert.equal(Object.keys(p.manifest).length,116);assert.equal(p.targetExecutions,0);assert.equal(p.executionApprovalGranted,false);assert.equal(p.metadataApprovalGranted,false);const c=p.expectedMsiReplayContract;assert.equal(c.caseCount,26);assert.equal(c.ownedStaLifecycles,26);assert.equal(c.readonlyOsIdentityCalls,26);assert.equal(c.openCycles,26);assert.equal(c.positivePublicAttempts,31);assert.equal(c.positivePublicSuccesses,31);assert.equal(c.positiveThreadPoolMtaCallbacks,63);assert.equal(c.wallClockTimerDeliveryProven,false);assert.equal(c.originalBclTimerCreationAndDisposalUsed,true);assert.equal(c.callbackContext,'ThreadPool MTA');assert.equal(c.checks,'UNKNOWN_UNTIL_ACTUAL_EXECUTION');assert.deepEqual(c.types,B.types);if(role==='positive')assert.equal(p.selectedOriginalFault,null);else assert.equal(p.selectedOriginalFault.targetKind,'host');});
 test('MODEL_ONLY '+role+' exact inverse restores policy/request/aliases/guard',()=>{const v=fixture(role),p=projectMsiReplayMODEL_ONLY(v),mapping=new Map(p.files.map(f=>[W.normalize(f.destination).toLowerCase(),f.original.path]));const back=f=>mapping.get(W.normalize(f).toLowerCase())??f;const restore=t=>({...t,path:back(t.path),...(same(t.path,p.host.path)?{sha256:v.base.host.sha256}:{})});const policy=clone(p.policy);policy.ownedRoot=v.policy.ownedRoot;policy.entry=back(policy.entry);policy.contexts[0].managed=policy.contexts[0].managed.map(restore);policy.contexts[0].requests=policy.contexts[0].requests.map(t=>({...t,targetPath:back(t.targetPath)}));policy.exactAliases=policy.exactAliases.map(t=>({...t,provider:restore(t.provider)}));assert.deepEqual(policy,v.policy);const guard={};for(const[f,s]of Object.entries(p.manifest))guard[back(f)]=same(f,p.host.path)?v.base.host.sha256:s;assert.deepEqual(guard,v.manifest);assert.equal(p.files.filter(f=>f.source.sha256!==f.original.sha256).length,1);assert.equal(p.files.find(f=>same(f.original.path,v.base.hc.path)).source.sha256,B.parentHc.sha256);});
 test('MODEL_ONLY '+role+' every argv plus preserved native guard exclusion',()=>{const v=fixture(role),p=projectMsiReplayMODEL_ONLY(v),r=p.ownedRoot;assert.deepEqual(p.args,[p.host.path,p.hc.path,W.join(r,'runtime'),W.join(r,'shared'),W.join(r,'runtime-pins.json'),W.join(r,'inputs','inventory.json'),W.join(r,'inputs','YeManFanHost.json'),W.join(r,'Build','msi-replay',role,'case','result.json'),'msi-replay']);for(const m of p.policy.contexts[0].managed.filter(t=>t.role==='framework'))assert.equal(Object.keys(p.manifest).some(f=>same(f,m.path)),false);assert.equal(p.policy.contexts.length,1);assert.equal(p.policy.contexts[0].managed.length,92);assert.equal(p.policy.contexts[0].managed.filter(t=>t.role==='framework').length,64);assert.equal(p.policy.exactAliases.length,6);assert.equal(p.scope.wallClockTimerDeliveryProven,false);assert.equal(p.scope.originalSentryModuleInitializerPreserved,true);assert.equal(p.scope.ownedBuildPayloadWritesRemain,true);assert.equal(p.scope.appliesTo,'canonical-reviewed-replay-host3-before-original-fault');});
}
const controls=[
 ['unknown profile',v=>v.input.profile='old','REPLAY_EXPLICIT_PROFILE_ROLE'],
 ['unknown role',v=>v.input.role='preservation','REPLAY_EXPLICIT_PROFILE_ROLE'],
 ['missing future manual fourpins',v=>v.input.acceptedMsiManual92=null,'REPLAY_ACTUAL_MANUAL_FOUR_PINS_REQUIRED'],
 ['partial future manual fourpins',v=>delete v.input.acceptedMsiManual92.graphPin,'REPLAY_ACTUAL_MANUAL_FOUR_PINS_REQUIRED'],
 ['malformed future hash',v=>v.input.acceptedMsiManual92.summaryPin.sha256='unknown','REPLAY_EXACT_PIN_REQUIRED'],
 ['hidden fifth parent field',v=>v.input.acceptedMsiManual92.extra={path:'D:/x',sha256:'1'.repeat(64)},'REPLAY_EXACT_PARENT_FOUR_PINS'],
 ['no derivative qualification',v=>v.input.derivativeQualificationPin=null,'REPLAY_QUALIFICATION_REQUIRED'],
 ['caller budget override',v=>v.input.budgets={business:120000},'REPLAY_INPUT_OVERRIDE_FORBIDDEN'],
 ['caller permission override',v=>v.input.hardwareIo=true,'REPLAY_INPUT_OVERRIDE_FORBIDDEN'],
 ['UNC stage',v=>v.input.stageRoot='\\\\server\\root\\stage','REPLAY_LOCAL_OWNED_PATH'],
 ['ADS stage',v=>v.input.stageRoot+=':stream','REPLAY_LOCAL_OWNED_PATH'],
 ['traversal',v=>v.input.stageRoot=v.input.ownedParent+'/../escape','REPLAY_LOCAL_OWNED_PATH'],
 ['not direct child',v=>v.input.stageRoot=v.input.ownedParent+'/two/child','REPLAY_FRESH_DIRECT_CHILD'],
 ['pending derivative review',v=>v.qualification.status='PENDING','REPLAY_REVIEWED_QUALIFICATION'],
 ['wrong host aggregate pin',v=>v.qualification.hostGenerationPin.sha256='0'.repeat(64),'REPLAY_REVIEWED_QUALIFICATION'],
 ['new native permission',v=>v.qualification.nativeDevicePermissionAdded=true,'REPLAY_REVIEWED_QUALIFICATION'],
 ['no review evidence',v=>v.qualification.reviewEvidencePins=[],'REPLAY_REVIEW_EVIDENCE'],
 ['nonactual typed review',v=>v.review.evidenceKind='SOURCE_ONLY','REPLAY_ACTUAL_TYPED_REVIEW'],
 ['typed review executes target',v=>v.review.targetCodeExecuted=true,'REPLAY_ACTUAL_TYPED_REVIEW'],
 ['parent is lifecycle stage',v=>v.base.kind='msi-original-lifecycle-prepared','REPLAY_PARENT_POSITIVE_SOURCE_PLAN'],
 ['parent negative role',v=>v.base.role='owner','REPLAY_PARENT_POSITIVE_SOURCE_PLAN'],
 ['parent HC changed',v=>v.base.hc.sha256='0'.repeat(64),'REPLAY_PARENT_POSITIVE_SOURCE_PLAN'],
 ['parent Host not4be',v=>v.base.host.sha256='0'.repeat(64),'REPLAY_PARENT_POSITIVE_SOURCE_PLAN'],
 ['unqualified parent source basis',v=>v.base.manualOracle.basisPin.sha256='0'.repeat(64),'REPLAY_PARENT_POSITIVE_SOURCE_PLAN'],
 ['parent helper source missing',v=>v.base.sourceEvidencePins=[],'REPLAY_PARENT_POSITIVE_SOURCE_PLAN'],
 ['parent graphfail summary',v=>v.acceptance.status='BUSINESS_ONLY_GRAPHFAIL','REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['summary prepared pin mismatch',v=>v.acceptance.prepared={path:'D:/another.json',sha256:'0'.repeat(64)},'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['summary graph pin mismatch',v=>v.acceptance.graph={path:'D:/another.json',sha256:'0'.repeat(64)},'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['parent cases not44',v=>v.acceptance.cases=26,'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['parent public attempts wrong',v=>v.acceptance.publicApplyCurveAttempts=31,'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['parent public success wrong',v=>v.acceptance.publicApplyCurveSuccess=31,'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['parent open cycles missing',v=>v.acceptance.manualOpenCycles=43,'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['parent unknown managed',v=>v.acceptance.unknownManaged=1,'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['parent force cleanup',v=>v.acceptance.forcedThreadCleanup=1,'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['parent denied native',v=>v.acceptance.deniedNative=1,'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['parent old60000budget',v=>v.acceptance.originalBusinessBudgetMs=60000,'REPLAY_ACTUAL_MANUAL_PARENT_ACCEPTANCE'],
 ['parent process old60000',v=>v.parentProcess.budgetMs=60000,'REPLAY_NATURAL_TERMINAL'],
 ['parent process larger120000',v=>v.parentProcess.budgetMs=120000,'REPLAY_NATURAL_TERMINAL'],
 ['parent null expiry',v=>v.parentProcess.expired=null,'REPLAY_NATURAL_TERMINAL'],
 ['parent timeout',v=>v.parentProcess.expired=true,'REPLAY_NATURAL_TERMINAL'],
 ['parent signal',v=>v.parentProcess.signal='SIGTERM','REPLAY_NATURAL_TERMINAL'],
 ['parent duration overbudget',v=>v.parentProcess.durationMs=90001,'REPLAY_NATURAL_TERMINAL'],
 ['parent nonzero',v=>v.parentProcess.code=2,'REPLAY_NATURAL_TERMINAL'],
 ['parent shell',v=>v.parentProcess.shell=true,'REPLAY_NATURAL_TERMINAL'],
 ['metadata90000',v=>v.metadataProcess.budgetMs=90000,'REPLAY_NATURAL_TERMINAL'],
 ['preload null expiry',v=>v.preloadProcess.expired=null,'REPLAY_NATURAL_TERMINAL'],
 ['parent process cwd mismatch',v=>v.parentProcess.cwd='D:/wrong','REPLAY_PARENT_ACTUAL_ARGV'],
 ['parent actual args mismatch',v=>v.parentProcess.args[7]='D:/wrong','REPLAY_PARENT_ACTUAL_ARGV'],
 ['loadedgraph unknown',v=>v.graph.unknownFiles.push({path:'D:/x.dll'}),'REPLAY_PARENT_EXACT_GRAPH'],
 ['loadedgraph empty',v=>v.graph.files=[],'REPLAY_PARENT_EXACT_GRAPH'],
 ['launch graph mismatch',v=>v.launch.actualLoadedManaged=[],'REPLAY_PARENT_EXACT_GRAPH'],
 ['parent result mode wrong',v=>v.parentResult.msiManualMode=false,'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent result replay already active',v=>v.parentResult.msiReplayMode=true,'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent reorderedIDs',v=>v.parentResult.cases.reverse(),'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent droppedID',v=>v.parentResult.cases.pop(),'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent businessFAIL',v=>v.parentResult.cases[0].status='FAIL','REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent threadleak',v=>v.parentResult.threadEvidence[0].allOwnedThreadsExited=false,'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent noBaseOpen',v=>v.parentResult.actualOriginalMsiBaseOpenExecuted=false,'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent HW flag',v=>v.parentResult.hardwareIo=true,'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent HostMain',v=>v.parentResult.hostMainStarted=true,'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent ManagersStart',v=>v.parentResult.managerStartCalled=true,'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['parent wrongcheckbinding',v=>v.parentResult.checks=100,'REPLAY_REAL_MANUAL_PARENT_BUSINESS'],
 ['joint not accepted',v=>v.joint.MSIManualSubrouteAccepted=false,'REPLAY_INDEPENDENT_MANUAL_JOINT'],
 ['joint source wrongstatus',v=>v.joint.status='SOURCE_ONLY','REPLAY_INDEPENDENT_MANUAL_JOINT'],
 ['joint maskrelaxed',v=>v.joint.requiredMasks=[0,4],'REPLAY_INDEPENDENT_MANUAL_JOINT'],
 ['joint unrelatedpositive',v=>v.joint.roles[0].summary={path:'D:/unrelated.json',sha256:'0'.repeat(64)},'REPLAY_INDEPENDENT_MANUAL_JOINT'],
 ['joint Sentryremoved',v=>v.joint.originalSentryModuleInitializerRemains=false,'REPLAY_INDEPENDENT_MANUAL_JOINT'],
 ['joint claims nativezero',v=>v.joint.nativeIoZeroClaim=true,'REPLAY_INDEPENDENT_MANUAL_JOINT'],
 ['parent budgetchanged',v=>v.base.budgets.parent=400000,'REPLAY_PARENT_BUDGET_UNKNOWN_NATIVE259'],
 ['initializeclaimed',v=>v.base.initializeRole='PASS','REPLAY_PARENT_BUDGET_UNKNOWN_NATIVE259'],
 ['nativeboundarychanged',v=>v.base.nativeBoundaryLength=260,'REPLAY_PARENT_BUDGET_UNKNOWN_NATIVE259'],
 ['reusedparentroot',v=>{v.input.ownedParent=W.dirname(v.base.ownedRoot);v.input.stageRoot=v.base.ownedRoot;},'REPLAY_DISTINCT_FRESH_ROOT'],
 ['secondcontext',v=>v.policy.contexts.push(clone(v.policy.contexts[0])),'REPLAY_EXACT_SCHEMA6_POLICY'],
 ['entrysubstitution',v=>v.policy.entry='D:/wrong.dll','REPLAY_EXACT_SCHEMA6_POLICY'],
 ['unreviewedManagement',v=>v.policy.contexts[0].managed.push({path:'D:/System.Management.dll',sha256:'0'.repeat(64),identity:'System.Management, Version=10.0.0.0',role:'framework'}),'REPLAY_EXACT_MANAGED92'],
 ['managedwronghash',v=>v.policy.contexts[0].managed[0].sha256='0'.repeat(64),'REPLAY_EXACT_MANAGED92'],
 ['managedwrongidentity',v=>v.policy.contexts[0].managed[0].identity='Wrong, Version=1.0.0.0','REPLAY_EXACT_MANAGED92'],
 ['frameworkrolemasquerade',v=>v.policy.contexts[0].managed.find(t=>t.role==='framework').role='target','REPLAY_EXACT_MANAGED92'],
 ['requestwrongprovider',v=>v.policy.contexts[0].requests[0].targetPath='D:/wrong.dll','REPLAY_ALIAS6_REQUEST_CONSERVATION'],
 ['aliascontextwidened',v=>v.policy.exactAliases[0].requestingContexts.push('Product'),'REPLAY_ALIAS6_REQUEST_CONSERVATION'],
 ['aliaschangedsha',v=>v.policy.exactAliases[0].provider.sha256='0'.repeat(64),'REPLAY_ALIAS6_REQUEST_CONSERVATION'],
 ['guardnewnative',v=>v.manifest['D:/user32.dll']='0'.repeat(64),'REPLAY_EXACT_GUARD116'],
 ['guardremovedtarget',v=>delete v.manifest[v.base.hc.path],'REPLAY_EXACT_GUARD116'],
 ['authorityartifactchanged',v=>v.base.artifacts.authority.sha256='0'.repeat(64),'REPLAY_EXACT_ARTIFACTS'],
 ['runtimeargvchangedwithactualagreement',v=>{v.base.args[2]='D:/wrong';v.parentProcess.args[7]='D:/wrong';},'REPLAY_PARENT_ARGV_DIRS'],
 ['argvhashmismatch',v=>v.argv.argvSha256='0'.repeat(64),'REPLAY_PARENT_ARGV_DIRS'],
 ['parentexpectedIDswrong',v=>v.base.expectedCaseIds=[],'REPLAY_PARENT_ARGV_DIRS'],
 ['replayIDshortened',v=>v.oracle.ids.pop(),'REPLAY_ORIGINAL26_ORACLE_BUDGETS'],
 ['replayIDsreordered',v=>v.oracle.ids.reverse(),'REPLAY_ORIGINAL26_ORACLE_BUDGETS'],
 ['replaymaskrelaxed',v=>v.oracle.failureMasks.sta=[],'REPLAY_ORIGINAL26_ORACLE_BUDGETS'],
 ['replaymarkersrelaxed',v=>v.oracle.markers.followup=[],'REPLAY_ORIGINAL26_ORACLE_BUDGETS'],
 ['replaybudget60000',v=>v.oracle.budgets.business=60000,'REPLAY_ORIGINAL26_ORACLE_BUDGETS'],
 ['replaybudget120000',v=>v.oracle.budgets.business=120000,'REPLAY_ORIGINAL26_ORACLE_BUDGETS'],
 ['replayparentenlarged',v=>v.oracle.budgets.parent=400000,'REPLAY_ORIGINAL26_ORACLE_BUDGETS'],
 ['hostgenerationtimeout',v=>v.host.process.expired=true,'REPLAY_NATURAL_TERMINAL'],
 ['hostgenerationwrongbudget',v=>v.host.process.budgetMs=60000,'REPLAY_NATURAL_TERMINAL'],
 ['hostwrongCLI',v=>v.host.summary.cli='msi-manual-host-ports','REPLAY_ORIGINAL_HOST3_GENERATION'],
 ['hostwrongproducer',v=>v.host.aggregate.producer.sha256='0'.repeat(64),'REPLAY_ORIGINAL_HOST3_GENERATION'],
 ['hostwrongsource',v=>v.host.summary.source.sha256='0'.repeat(64),'REPLAY_ORIGINAL_HOST3_GENERATION'],
 ['hostwrongtarget',v=>v.host.summary.target.sha256='0'.repeat(64),'REPLAY_ORIGINAL_HOST3_GENERATION'],
 ['hostnonexactgraph',v=>v.host.summary.exactManagedGraphVerified=false,'REPLAY_ORIGINAL_HOST3_GENERATION'],
 ['hostfourthmethodchange',v=>v.host.proof.changedMethods.push('Extra'),'REPLAY_HOST3_PROOF'],
 ['hostproofTimerbodylost',v=>v.host.proof.restoredEndpointOperandsEqualOriginalBodies=false,'REPLAY_HOST3_PROOF'],
 ['hostproofBCLtimerreplaced',v=>v.host.proof.originalBclTimerCreatedUnarmed=false,'REPLAY_HOST3_PROOF'],
 ['hostproofpowerunisolated',v=>v.host.proof.physicalPowerReadIsolated=false,'REPLAY_HOST3_PROOF'],
 ['reviewHCsubstitution',v=>v.review.hcPin.sha256='0'.repeat(64),'REPLAY_TYPED_REVIEW_IMAGE_BINDING'],
 ['reviewwrongport',v=>v.review.portsSHA256='0'.repeat(64),'REPLAY_TYPED_REVIEW_IMAGE_BINDING'],
 ['reviewwallclockclaim',v=>v.review.wallClockTimerDeliveryProven=true,'REPLAY_NO_WALLCLOCK_OR_DECODER_ACCEPTANCE'],
 ['reviewrawWMIclaim',v=>v.review.rawWmiDecoderAccepted=true,'REPLAY_NO_WALLCLOCK_OR_DECODER_ACCEPTANCE'],
 ['nativepathunbounded',v=>{v.input.ownedParent='D:/'+('a'.repeat(220));v.input.stageRoot=v.input.ownedParent+'/stage';},'REPLAY_NATIVE259_ROOT']
];for(const c of controls)refusal(...c);
for(const key of ['wholeMethodSetsVerified','originalBaseOpenAndStructuredReleasePreserved','originalPublicApplyCurveAndTableBitWritersPreserved','originalTimerCallbacksAndReplayConsumersUnchanged','restoredEndpointOperandsEqualOriginalBodies','originalBclTimerCreatedUnarmed','originalTimerDisposalPreserved','physicalPowerIoOnlyTerminalReplaced','originalSentryModuleInitializerPreserved','ownedBuildPayloadWritesRemain','hostNoRegistryPnpGeneralOpenAdded'])refusal('missing reviewed scope '+key,v=>v.review[key]=false,'REPLAY_REVIEW_SCOPE_'+key);
for(const key of ['publicEntry','originalPublicLoadAndHandshakeExecuted','actualStaMessagePumpExecuted','actualDefaultAssemblyLoadExecuted','actualOriginalGetCurrentAndConstructorsExecuted','actualConfigureAndNativeAdmissionExecuted','actualIdentityCapabilityAndAuthorizationExecuted'])refusal('skipped actual parent '+key,v=>v.parentResult[key]=false,'REPLAY_REAL_MANUAL_PARENT_BUSINESS');
for(const role of B.roles.slice(1)){
 refusal(role+' rawpinunbound',v=>v.review.derivedFrom=[v.qualification.reviewEvidencePins[0]],'REPLAY_NEGATIVE_RAW_QUALIFICATION',role);
 refusal(role+' sourcewrong',v=>v.fault.summary.source.sha256='0'.repeat(64),'REPLAY_ORIGINAL_NEGATIVE_PROCESS',role);
 refusal(role+' budgetenlarged',v=>v.fault.process.budgetMs=60000,'REPLAY_NATURAL_TERMINAL',role);
 refusal(role+' twomethodchanges',v=>v.fault.proof.changedMethods.push('Extra'),'REPLAY_ORIGINAL_SINGLE_METHOD_FAULT',role);
 refusal(role+' fullmethodsetlost',v=>v.review.variants.find(t=>t.role===role).wholeMethodSetVerified=false,'REPLAY_VARIANT_EXACT_IMAGE_METHODSET_MASK',role);
 refusal(role+' HCfaultnotallowed',v=>v.review.variants.find(t=>t.role===role).targetKind='hc','REPLAY_VARIANT_EXACT_IMAGE_METHODSET_MASK',role);
 refusal(role+' maskrelaxed',v=>v.review.variants.find(t=>t.role===role).expectedFailureMask=[],'REPLAY_VARIANT_EXACT_IMAGE_METHODSET_MASK',role);
}
test('MODEL_ONLY parent pending flags are not used as actual acceptance evidence',()=>{const v=fixture('positive');v.base.sourceModelsPendingWorker=true;v.host.aggregate.publicFactoryStillUnaccepted=true;assert.equal(projectMsiReplayMODEL_ONLY(v).expectedCaseIds.length,26);});
test('MODEL_ONLY label mandatory on model projection',()=>{const v=fixture('positive');delete v.MODEL_ONLY;assert.throws(()=>projectMsiReplayMODEL_ONLY(v),/REPLAY_MODEL_LABEL_REQUIRED/);});
test('actual inspect rejects MODEL_ONLY options without reading a target',()=>{assert.throws(()=>inspectMsiReplayInputs({MODEL_ONLY:true,inputsPin:{path:'D:/notread.json',sha256:'0'.repeat(64)}}),/REPLAY_MODEL_NOT_ACTUAL/);});
for(const [name,error]of [['missing-manual-parent','REPLAY_ACTUAL_MANUAL_FOUR_PINS_REQUIRED'],['partial-manual-parent','REPLAY_ACTUAL_MANUAL_FOUR_PINS_REQUIRED'],['missing-qualification','REPLAY_QUALIFICATION_REQUIRED'],['model-document','REPLAY_MODEL_NOT_ACTUAL'],['pending-qualification','REPLAY_COMPLETE_DERIVATIVE_QUALIFICATION']])test('actual reader '+name+' refuses before stage writes',()=>{const f=dir+'/models/msi-replay/refusal-'+name+'.json',i=read(f);assert.equal(fs.existsSync(i.stageRoot),false);assert.throws(()=>inspectMsiReplayInputs({inputsPin:{path:f,sha256:sha(fs.readFileSync(f))}}),e=>e.message===error);assert.equal(fs.existsSync(i.stageRoot),false);});
test('source basis pins actual existing manual four receipts with provenance',()=>{assert.equal(B.parentAcceptancePending,false);assert.equal(B.referenceDoesNotProveManualAcceptance,false);assert.equal(B.sourceContractReferenceOnly,false);assert.equal(B.actualParentEvidenceBound,true);assert.equal(B.acceptedMsiManual92.jointReviewPin.sha256,'705713a0dd1c5af5e2834e5f7cdb5c48e6e4fa0fd5b8f82c4b8628b3dcff14e3');});
test('source oracle input files remain exact G bytes',()=>{for(const p of B.sourcePins)assert.equal(sha(fs.readFileSync(p.path)),p.sha256);});
test('runtime helper has no CLR/process execution implementation',()=>{const s=fs.readFileSync(dir+'/prepare-msi-replay.mjs','utf8');assert.doesNotMatch(s,/child_process|spawnSync|execSync|\.spawn\(|\.exec\(/);assert.equal(MSI_REPLAY_PROFILE,B.profile);});

refusal('parent Host descriptor moved outside controlled row',v=>v.base.host.path='D:/unrelated/YeManFanHost.dll','REPLAY_PARENT_HOST_HC_DESCRIPTORS');
refusal('parent HC descriptor identity mismatch',v=>v.base.hc.identity='FakeHC, Version=1.0.0.0','REPLAY_PARENT_HOST_HC_DESCRIPTORS');
refusal('joint duplicated role',v=>v.joint.roles[1].role='positive','REPLAY_INDEPENDENT_MANUAL_JOINT');
refusal('joint wrong per-role mask',v=>v.joint.roles[1].requiredMask=0,'REPLAY_INDEPENDENT_MANUAL_JOINT');
test('MODEL_ONLY fixed actual parent refuses a different four-pin set without rewriting basis',()=>{
 const v=fixture('positive'),basisBefore=sha(fs.readFileSync(dir+'/msi-replay-basis.json'));v.input.acceptedMsiManual92.summaryPin={path:'D:/MODEL_ONLY/another-summary.json',sha256:'a'.repeat(64)};
 assert.throws(()=>projectMsiReplayMODEL_ONLY(v),/REPLAY_FIXED_ACTUAL_PARENT_PINS/);assert.equal(sha(fs.readFileSync(dir+'/msi-replay-basis.json')),basisBefore);
});
refusal('old mistaken MANUAL_ORIGINAL44 status',v=>v.joint.status='MSI_MANUAL_ORIGINAL44_SEVEN_ROLES_INDEPENDENT_JOINT_PASS_SOURCE144_DEFAULT92','REPLAY_INDEPENDENT_MANUAL_JOINT');
refusal('actual joint checks count altered',v=>v.joint.totalChecks=10793,'REPLAY_ACTUAL_MANUAL_JOINT_BUDGETS_SCOPE');
refusal('actual joint90000budget flag missing',v=>delete v.joint.allOriginal90000BusinessBudgetsPreserved,'REPLAY_ACTUAL_MANUAL_JOINT_BUDGETS_SCOPE');
refusal('actual joint metadata preload budget flag false',v=>v.joint.allMetadataPreload60000=false,'REPLAY_ACTUAL_MANUAL_JOINT_BUDGETS_SCOPE');
refusal('actual joint nativeguard expansion flag',v=>v.joint.nativeGuard116Unchanged=false,'REPLAY_ACTUAL_MANUAL_JOINT_BUDGETS_SCOPE');
refusal('actual joint unopened thread flag',v=>v.joint.allOwnedThreadsExited=false,'REPLAY_ACTUAL_MANUAL_JOINT_BUDGETS_SCOPE');
refusal('actual joint G source bytes changed',v=>v.joint.GToolSources137Unchanged=false,'REPLAY_ACTUAL_MANUAL_JOINT_BUDGETS_SCOPE');
refusal('actual joint negative duration relabeled',v=>v.joint.sixNegativeGroupDurationMs=1,'REPLAY_ACTUAL_MANUAL_JOINT_BUDGETS_SCOPE');
refusal('actual positive checks relabeled consistently',v=>{v.acceptance.checks=1625;v.parentResult.checks=1625;},'REPLAY_FIXED_ACTUAL_POSITIVE_COUNTS');
refusal('actual positive duration relabeled',v=>v.parentProcess.durationMs=1,'REPLAY_FIXED_ACTUAL_POSITIVE_COUNTS');
refusal('actual joint role evidence removed',v=>v.parentRoleRecords.pop(),'REPLAY_ACTUAL_MANUAL_ROLE_RECORDS_REQUIRED');
refusal('actual joint role summary wrongmask',v=>v.parentRoleRecords[1].summary.requiredMask=0,'REPLAY_ACTUAL_MANUAL_JOINT_ROLE');
refusal('actual joint role process wrongbudget',v=>v.parentRoleRecords[1].process.budgetMs=60000,'REPLAY_ACTUAL_MANUAL_ROLE_TERMINAL');
refusal('actual joint role process naturalzero mismatch',v=>v.parentRoleRecords[1].process.code=0,'REPLAY_ACTUAL_MANUAL_ROLE_TERMINAL');
refusal('actual joint role result IDs reordered',v=>v.parentRoleRecords[1].result.cases.reverse(),'REPLAY_ACTUAL_MANUAL_ROLE_IDS');
refusal('actual joint role cases erased',v=>v.parentRoleRecords[1].result.cases=[],'REPLAY_ACTUAL_MANUAL_ROLE_IDS');
test('parent-only actual inspector rejects MODEL options',()=>{assert.throws(()=>inspectMsiReplayParentInputs({MODEL_ONLY:true,acceptedMsiManual92:B.acceptedMsiManual92}),/REPLAY_MODEL_NOT_ACTUAL/);});
test('parent-only actual inspector rejects override fields',()=>{assert.throws(()=>inspectMsiReplayParentInputs({acceptedMsiManual92:B.acceptedMsiManual92,budgets:{business:120000}}),/REPLAY_EXACT_PARENT_OPTIONS/);});
test('parent-only actual inspector rejects wrong actual joint pin before file reads',()=>{const p=clone(B.acceptedMsiManual92);p.jointReviewPin.sha256='0'.repeat(64);assert.throws(()=>inspectMsiReplayParentInputs({acceptedMsiManual92:p}),/REPLAY_FIXED_ACTUAL_PARENT_PINS/);});
test('shared parent validator uses original actual status; does not admit mistaken status alias',()=>{assert.equal(B.prospectiveJointContract.status,'MSI_ORIGINAL44_MANUAL_SEVEN_ROLES_INDEPENDENT_JOINT_PASS_SOURCE144_DEFAULT92');assert.equal(B.prospectiveJointContract.derivedFrom.sha256,B.acceptedMsiManual92.jointReviewPin.sha256);assert.equal(projectMsiReplayMODEL_ONLY(fixture('positive')).expectedCaseIds.length,26);});
