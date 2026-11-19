import {compositionContract} from '../controller-signal-evidence/owned-cleanup-composition-receipts.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {isDeepStrictEqual as same} from 'node:util';
import {contract,historicalArtifactRows} from './adapter.mjs';
import {nativeExitChildArgs,nativeExitPythonFlags} from './tool-profile.mjs';
import {fixedBuildSelection,fixedNativeExitMode,validateFixedBuildGraphs} from './fixed-build-receipts.mjs';
const need=(x,s)=>assert.ok(x,s),hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'')),norm=p=>path.win32.resolve(p).toLowerCase();
function inputs(r,{repo,sources,sourceMode},snapshot){for(const row of r.inputs){const n=path.relative(repo,row.path).replaceAll('\\','/');need(Object.hasOwn(sources,n)&&sources[n]===row.sha256&&(sourceMode==='captured-execution-only'||hash(row.path)===row.sha256),'NATIVE_EXIT_ACTUAL_INPUT:'+n);need(hash(path.join(snapshot,n))===row.sha256,'NATIVE_EXIT_SOURCE_SNAPSHOT:'+n);}}
function compiler(r,tools){need(norm(r.path)===norm(tools.native.compiler)&&hash(r.path)===r.sha256&&tools.files.some(x=>norm(x.path)===norm(r.path)&&x.sha256===r.sha256),'NATIVE_EXIT_ACTUAL_COMPILER');need(r.vcvarsSha256===hash(tools.vcvars),'NATIVE_EXIT_ACTUAL_VCVARS');if(r.windowsSDKVersion||r.sdkVersion)need((r.windowsSDKVersion??r.sdkVersion).replace(/[\\/]$/,'')===tools.sdkVersion,'NATIVE_EXIT_ACTUAL_SDK');}
function validateBuild(r,ctx,output,route,expectedLaunches){
 if(!ctx.requireFixedBuild){compiler(r.compiler,ctx.tools);return;}
 return validateFixedBuildGraphs(r,ctx,output,route,fixedBuildSelection(route,contract.gateRoutes),expectedLaunches);
}
export function validateHelper(r,ctx,output){need(r.pass===true&&r.sourceStable&&r.parentStatus==='INCOMPLETE'&&r.realHostOrSdkExecuted===false&&r.productionModified===false,'NATIVE_EXIT_HELPER_SCOPE');inputs(r,ctx,path.join(output,'source'));const actualBuilds=validateBuild(r,ctx,output,'helper',2);need(r.profiles?.length===2,'NATIVE_EXIT_HELPER_PROFILES');for(const name of ['Release','Checked']){const p=r.profiles.find(p=>p.name===name);need(p&&(actualBuilds||hash(path.join(output,name,'suite.exe'))===p.binarySha256),'NATIVE_EXIT_HELPER_BINARY');const v=p.result;need(v.failures===0&&v.permanentOsBlockStatus==='UNKNOWN'&&v.realHostSdkOrPipeExecuted===false&&same(v.rows.map(x=>x.name),contract.helperNames)&&v.rows.every(x=>x.pass),'NATIVE_EXIT_HELPER_13_CASES');const first=v.rows[0].evidence;need(first.allPrivateHandlesClosed&&first.processAndThreadJoined&&first.fixtureKillOrNaturalBackstop===false&&first.forceCalls===1&&first.processExitCode===1067&&first.receipt.exitConfirmed&&first.receipt.handleClosed,'NATIVE_EXIT_HELPER_REAL_DESTRUCTION');}return {profiles:2,casesPerProfile:13,pass:true,...(actualBuilds?{actualBuilds}:{})};}
export function validateParent(r,ctx,output){
 need(r.status==='ACTUAL_NATIVE_EXIT_LOCAL_OWNERS_RETIRED_SCOPED_PASS'&&r.localOwnerRegressionVerified===true&&r.sourceStable&&r.productPass===false&&r.parentStatus==='INCOMPLETE'&&r.realHostOrSdkExecuted===false&&r.realMainExecuted===false&&r.productionModified===false&&r.mainSha256===ctx.sources['native/main.cpp'],'NATIVE_EXIT_PARENT_SCOPE');
 inputs(r,ctx,path.join(output,'source'));
 const actualBuilds=validateBuild(r,ctx,output,'parent',2*contract.parentScenarios.length);
 return {...validateParentScenarios(r,output),...(actualBuilds?{actualBuilds}:{})};
}
function validateRetainedParentCase(name,x){
 const b=x.beforeFixtureFaultRelief,c=x.actualExitCleanup,cleanup=x.fixtureCleanup;
 need(x.fullApplicationExitAccepted===false&&Number.isSafeInteger(x.assertionsPassed)&&x.assertionsPassed>0&&cleanup.faultReliefWasConditional===true,'NATIVE_EXIT_PARENT_RETAINED_SCOPE');
 if(name==='busy-pipe-independent-force'){
  const r=x.busyReceipt;
  need(x.realRuntimeMutexHeld===true&&x.forceAttempts===1&&Number.isSafeInteger(x.retirementWallMsWhilePipeBusy)&&x.retirementWallMsWhilePipeBusy>=0&&x.retirementWallMsWhilePipeBusy<2000&&r?.pipeOwnerAvailable===false&&r.pipeReturned===false&&r.forceAttempted===true,'NATIVE_EXIT_PARENT_BUSY_PIPE_RETAINS_DEBT');
  return;
 }
 need(x.actualEnsure?.returned===true&&x.actualEnsure.actualLocalGetter===true&&x.actualEnsure.wholeMainAndWmAppReadyExecuted===false&&c?.cleanupCompleted===true&&c.readyPosts===1&&c.cleanupCode===0&&c.sameWorkerCompleted===true&&Number.isSafeInteger(c.originalTid)&&c.originalTid>0,'NATIVE_EXIT_PARENT_SAME_WORKER_COMPLETES');
 need(b?.workerStillRunning===true&&b.workerTid===c.originalTid&&b.cleanupCompleted===false&&b.readyPosts===0&&b.checkpointLogCount===1&&b.actualAwaitStageElapsedMs>=31000&&b.actualAwaitStageElapsedMs<32000&&b.steppedSleepCalls>0&&b.steppedSleepCalls<=124&&b.retainedReceipt?.observationEnded===false&&b.retainedReceipt.continuationRetained===true&&b.retainedReceipt.processExitAuthorized===false,'NATIVE_EXIT_PARENT_RETAINED_CHECKPOINT');
 const captureReady=name==='busy-host-budget'||name==='permanent-close-budget';
 const producerReady=captureReady||name==='capture-nonproducer-debt';
 const independentHost=name==='capture-nonproducer-debt',closeDebt=name==='permanent-close-budget';
 need(b.captureReturned===captureReady&&b.producerReturned===producerReady&&b.ordinaryAttempted===(independentHost||closeDebt)&&b.localReturnedFlag===independentHost&&b.processOwnerRetained===!(independentHost||closeDebt)&&b.primaryOwnerRetained===!independentHost&&b.forceBudgetSpent===(independentHost||closeDebt),'NATIVE_EXIT_PARENT_RETAINED_OWNER_BOUNDARY');
 if(closeDebt)need(b.pipeState?.stopped===false,'NATIVE_EXIT_PARENT_RETAINED_CLOSE_DEBT');
 if(name==='capture-stop-exception')need(b.exceptionLogCount===1,'NATIVE_EXIT_PARENT_RETAINED_EXCEPTION');
}
export function validateParentScenarios(r,output,{historical=false}={}){
 need(r.profiles?.length===2,'NATIVE_EXIT_PARENT_PROFILES');
 const expected=historical?contract.historicalParentScenarios:contract.parentScenarios;
 const scenarioResults=[];
 for(const name of ['Release','Checked']){
  const p=r.profiles.find(p=>p.name===name),folder=path.join(output,name);
  need(p&&hash(path.join(folder,'fixture.exe'))===p.binarySha256&&exactNames(p.scenarios.map(s=>s.name),expected),'NATIVE_EXIT_PARENT_MATRIX');
  for(const s of p.scenarios){
   const x=s.result;
   need(s.exitCode===0&&x.scopedLocalOwnerPass===true,'NATIVE_EXIT_PARENT_ACTUAL_CASE');
   need(x.fixtureCleanup?.childKillOrEventBackstop===false&&x.fixtureCleanup.productClosedOriginalOwners&&x.fixtureCleanup.privateHandlesClosed&&x.fixtureCleanup.permanentFaultDestructionAccepted===false&&x.endpointAndPhysicalReleaseProven===false,'NATIVE_EXIT_PARENT_NO_SALVAGE_OR_PHYSICAL_CLAIM');
   if(contract.historicalParentScenarios.includes(s.name)){
   const proof=x.productionResourceProof,forced=s.name!=='healthy-ordinary';
   need(x.actualEnsure.returned&&x.actualEnsure.actualLocalGetter&&x.actualEnsure.wholeMainAndWmAppReadyExecuted===false,'NATIVE_EXIT_PARENT_ACTUAL_ENSURE');
   need(x.actualExitCleanup.cleanupCompleted&&x.actualExitCleanup.readyPosts===1&&x.actualExitCleanup.stage==='finished','NATIVE_EXIT_PARENT_COMPLETION');
   need(proof.allFourLocalOwnersNull&&proof.processExitedAndJoined&&proof.primaryExitedAndJoined&&proof.forceAttempts===(forced?1:0)&&proof.forceBudgetSpent===forced&&proof.processExitCode===(forced?1067:0),'NATIVE_EXIT_PARENT_KERNEL_RETURN');
   need(proof.pipeLiveHandleOracle===0&&proof.pipeRuntime.available&&proof.pipeRuntime.stopped&&['failedCloseDebt','ownedDuplicates','ownedEvents','ownedOriginals','pending'].every(k=>proof.pipeRuntime[k]===0),'NATIVE_EXIT_PARENT_PIPE_RETURN');
   if(forced)need(x.businessDutyRetained,'NATIVE_EXIT_PARENT_TARGET_DEBT_RETAINED');
   if(s.name==='protected-primary-pipe'){
    const b=x.beforeFixtureFaultRelief;
    need(b&&b.cleanupCompleted===false&&b.cleanupWorkerStillRunning&&b.readyPosts===0&&b.localReturnedFlag===false&&b.sameTuple&&b.primarySameOwner&&b.forceSpent&&b.pipeState?.failedCloseDebt===1&&b.pipeState.stopped===false,'NATIVE_EXIT_PARENT_RETENTION_BEFORE_FAULT_RELIEF');
    // The process is already gone in this sample, but its owner still owes pipe cleanup.
    need(b.processExited===true&&b.processOwnerClosed===true&&b.primaryThreadExited===true&&b.pipeState.available===true&&Number.isSafeInteger(b.pipeState.ownedOriginals)&&b.pipeState.ownedOriginals>0,'NATIVE_EXIT_PARENT_DEAD_PROCESS_WITH_PIPE_DEBT');
   }
   }else validateRetainedParentCase(s.name,x);
   const file=path.join(folder,s.name+'-result.json');
   let bytes,scenario;
   try{bytes=fs.readFileSync(file);scenario=JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));}
   catch(error){throw new Error('NATIVE_EXIT_PARENT_SCENARIO_RAW_UNREADABLE:'+name+'/'+s.name,{cause:error});}
   need(scenario?.scenario===s.name&&same(scenario,x),'NATIVE_EXIT_PARENT_SCENARIO_RAW_MISMATCH:'+name+'/'+s.name);
   scenarioResults.push({profile:name,scenario:s.name,path:file,bytes:bytes.length,sha256:createHash('sha256').update(bytes).digest('hex')});
  }
 }
 return {profiles:2,scenariosPerProfile:expected.length,pass:true,scenarioResults};
}
const ordinaryGateRoutes=['ready','no-window-loop'];
function exactNames(actual,expected){
 if(!Array.isArray(actual)||actual.length!==expected.length)return false;
 const names=new Set(actual);
 return names.size===expected.length&&expected.every(name=>names.has(name));
}

export function validateGateProfiles(r,{sourceMode}={}){
 const original=contract.originalEvidence[2],bytes=fs.readFileSync(original.path);
 need(createHash('sha256').update(bytes).digest('hex')===original.sha256,'NATIVE_EXIT_ORIGINAL_GATES_CHANGED');
 const baseline=JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));
 const legacyGateCases=baseline.profiles.find(p=>p.profile==='Release'&&p.route==='current').cases.map(c=>c.name);
 const currentGateCases=[...legacyGateCases,...ordinaryGateRoutes.map(route=>route+':serial-not-returned'),
  ...['fast-completed-top','fast-completed-after-wait'].flatMap(route=>
   ['capture-not-returned','host-local-not-returned','serial-not-returned'].map(fault=>route+':'+fault))];
 const historical=sourceMode==='captured-execution-only'&&same(r.sourceHashes,baseline.sourceHashes);
 need(r.status==='PASS_ACTUAL_EXIT_COMPLETION_GATES'&&(historical||r.originalHostAndHistoricalControlsExecuted===true),'NATIVE_EXIT_GATES_SELECTION');
 need(r.profiles?.length===8,'NATIVE_EXIT_GATES_EIGHT_BUILDS');
 let privateThreadsJoined=0;
 for(const profile of ['Release','Checked'])for(const [route,originalFailureCount] of Object.entries(contract.gateRoutes)){
  const rows=r.profiles.filter(x=>x.profile===profile&&x.route===route);
  need(rows.length===1,'NATIVE_EXIT_GATES_ROUTE');
  const p=rows[0],current=route==='current',serialBaseline=current&&!historical;
  const cases=serialBaseline?currentGateCases:legacyGateCases;
  // The three fast-top cases deliberately have no private cleanup thread.
  const threads=cases.filter(n=>!n.startsWith('fast-completed-top:')).length;
  const checks=historical?391:serialBaseline?513:419;
  const originalMask=baseline.profiles.find(row=>row.profile===profile&&row.route===route).failedChecks;
  need(originalMask.length===originalFailureCount,'NATIVE_EXIT_ORIGINAL_NEGATIVE_MASK');
  // The current mutant deletes only the close-site check. Both completed-loop exits still check Host debt.
  const expectedMask=!historical&&route==='delete-no-window-host-gate'?
   originalMask.filter(name=>!name.endsWith(':pending-loop-must-not-return-to-final-cleanup')):originalMask;
  const failures=expectedMask.length;
  need(p.isCurrentSource===current&&p.checks===checks&&p.failures===failures&&p.exitCode===(current?0:1)&&p.pass===current,'NATIVE_EXIT_GATES_SPECIFIED_ASSERTIONS');
  need(exactNames(p.cases?.map(c=>c.name),cases)&&(historical||(p.selection===(current?'all':'legacy')&&exactNames(p.expectedCases,cases))),'NATIVE_EXIT_GATES_EXACT_CASES:'+profile+'/'+route);
  need(p.completeReadyCaseExecuted&&p.completeWWinMainMessageLoopExecuted&&p.actualHostLocalPredicateExecuted&&p.allPrivateResourcesZeroBeforeExit&&p.privateHandlesRemaining===0&&p.privateThreadsStarted===threads&&p.privateThreadsSignalledAndObserved===threads&&p.privateThreadsStillRunning===0&&p.fixtureForcedRealCleanupCount===0&&p.processExitUsedAsCleanup===false&&p.cases.every(c=>c.realPrivateResourcesZeroBeforeReturn===true&&c.processExitUsedAsCleanup===false),'NATIVE_EXIT_GATES_BEFORE_EXIT_ZERO');
  need(p.testedScopeStable&&p.wholeLifecycleAcceptance===false&&p.wholeMainAcceptance===false&&
   (historical||(p.serialOwnershipIsExplicitMemoryPort===true&&p.realSerialWorkerExecuted===false&&p.realSerialWaitOrJoinProven===false)),'NATIVE_EXIT_GATES_NO_SCOPE_INFLATION');
  need(exactNames(p.failedChecks,expectedMask)&&(current||p.specifiedNegativeObserved===true)&&
   p.cases.every(c=>c.pass===!p.failedChecks.some(check=>check.startsWith(c.name+':'))),'NATIVE_EXIT_GATES_NEGATIVE_NOT_LOST');
  privateThreadsJoined+=threads;
 }
 return {profiles:2,currentCasesPerProfile:historical?28:36,currentChecksPerProfile:historical?391:513,
  specifiedNegativeBuilds:6,privateThreadsJoined,serialDeletionControls:'NOT_RUN',pass:true};
}

export function validateGates(r,ctx,output){
 need(r.status==='PASS_ACTUAL_EXIT_COMPLETION_GATES'&&r.pass&&r.runtimeEvidenceComplete&&r.sourceStable&&r.sourceScopeRawUnchanged&&r.wholeMainAcceptance===false&&r.wholeLifecycleAcceptance===false&&r.realProductHostOrCaptureExecuted===false&&r.realWindowsOrModuleCleanupExecuted===false&&r.privateCleanupThreadHandleIsReal&&r.processExitUsedAsCleanup===false,'NATIVE_EXIT_GATES_SCOPE');
 need(r.sourceHashes['native/main.cpp']===ctx.sources['native/main.cpp'],'NATIVE_EXIT_GATES_MAIN_FREEZE');
 for(const [n,h] of Object.entries(r.sourceHashes)){
  if(n.startsWith('historical/'))continue;
  need(ctx.sources[n]===h&&(ctx.sourceMode==='captured-execution-only'?hash(path.join(r.run,'source',n))===h:hash(path.join(ctx.repo,n))===h),'NATIVE_EXIT_GATES_SOURCE:'+n);
 }
 const validation=validateGateProfiles(r,ctx);
 const actualBuilds=validateBuild(r,ctx,output,'gates',2*Object.keys(contract.gateRoutes).length);
 if(!actualBuilds)for(const p of r.profiles){
  const folder=path.join(r.run,p.profile+'-'+p.route);
  need(hash(path.join(folder,'gates.exe'))===p.executableSha256&&hash(path.join(folder,'gates.cpp'))===p.cppSha256&&hash(path.join(folder,'deps.json'))===p.sourceDependenciesSha256,'NATIVE_EXIT_GATES_EXECUTED_BINARY');
 }
 return {...validation,...(actualBuilds?{actualBuilds}:{})};
}
export function compositionHistoricalReference(repo){
 const tc=compositionContract(repo);
 return {scope:'HISTORICAL_REFERENCE_ONLY',manifestSha256:tc.contract.toolchain.manifestSha256,
  declaredFiles:tc.manifest.files.length,frozenPayloadValidation:'NOT_RUN',usedForCurrentBuild:false};
}
function validateLegacyNested(result,ctx,owner){
 const tc=compositionContract(ctx.repo);let nestedBefore;
 for(const phase of ['Before','After']){const p=result['nested'+phase];need(p&&hash(p.path)===p.sha256&&norm(p.path)===norm(path.join(owner.formalOutput,'nested-tools-'+phase.toLowerCase()+'.json')),'NATIVE_EXIT_NESTED_SEAL_PIN');const n=read(p.path);need(n.kind==='complete-frozen-toolchain-manifest'&&n.phase===phase.toLowerCase()&&n.manifestSha256===tc.contract.toolchain.manifestSha256&&same(n.sourceHashes,ctx.sources)&&n.files?.length===5772&&tc.manifest.files.length===5772,'NATIVE_EXIT_COMPLETE_5772_NESTED_SEAL');for(let i=0;i<5772;i++){const row=n.files[i],expected=tc.manifest.files[i];need(norm(row.path)===norm(expected.frozen)&&row.sha256===expected.sha256&&row.bytes===expected.sizeBytes,'NATIVE_EXIT_NESTED_MANIFEST_ROW');}if(phase==='Before')nestedBefore=n.files;else need(same(nestedBefore,n.files),'NATIVE_EXIT_NESTED_BEFORE_AFTER_CHANGED');}
}
export function validateNativeExit(result,ctx){need(result.status==='PASS'&&result.pass&&result.fullLifecycleAcceptance==='INCOMPLETE'&&result.physicalReturnVerified===false&&result.driverExecuted===false&&result.processExitUsedAsCleanup===false,'NATIVE_EXIT_AGGREGATE_SCOPE');need(same(Object.keys(result.children).sort(),['gates','helper','parent']),'NATIVE_EXIT_THREE_REAL_CHILDREN');
const fixed=result.schemaVersion===2;
need((result.schemaVersion===1&&!ctx.requireFixedBuild&&result.toolEvidenceMode===undefined)||(fixed&&result.toolEvidenceMode===fixedNativeExitMode),'NATIVE_EXIT_TOOL_EVIDENCE_MODE');
need(result.sourceStable&&result.artifactsStable&&same(result.sourceHashes,ctx.sources)&&same(result.toolsBefore,ctx.tools.files)&&same(result.toolsAfter,ctx.tools.files),'NATIVE_EXIT_COMPLETE_SOURCE_TOOL_SEALS');
const currentProfile=typeof ctx.tools.profileSha256==='string';
if(currentProfile)need(same(result.toolProfile,{sha256:ctx.tools.profileSha256,files:ctx.tools.files.length,pythonFlags:nativeExitPythonFlags}),'NATIVE_EXIT_EXECUTED_TOOL_PROFILE');
else need(!result.toolProfile,'NATIVE_EXIT_EXECUTED_TOOL_PROFILE');
need(result.workspace?.identity&&hash(result.workspace.identity.path)===result.workspace.identity.sha256,'NATIVE_EXIT_ROOT_OWNER_PIN');
const owner=read(result.workspace.identity.path);need(norm(owner.repository)===norm(ctx.repo)&&same(owner.sourceHashes,ctx.sources),'NATIVE_EXIT_ROOT_SOURCE_OWNER');
if(fixed){
 need(currentProfile&&same(result.compositionArchive,compositionHistoricalReference(ctx.repo))&&
  same(result.historicalArtifactPins,historicalArtifactRows(ctx.repo))&&!result.nestedBefore&&!result.nestedAfter,'NATIVE_EXIT_HISTORICAL_REFERENCE_SCOPE');
}else validateLegacyNested(result,ctx,owner);
const values={};for(const [name,row] of Object.entries(result.children)){
 need(row.exitCode===0&&!row.timedOut&&!row.signal&&!row.launchError&&hash(row.receipt.path)===row.receipt.sha256,'NATIVE_EXIT_ACTUAL_CHILD_COMPLETION');
 const route=contract.routes[name], mainSha256=name==='parent'?ctx.sources['native/main.cpp']:undefined;
 const validArgs=currentProfile?same(row.args,nativeExitChildArgs(ctx.repo,route,row.ownedWorkspace.buildOutput,mainSha256,{fixedBuild:fixed}))&&
  norm(row.output)===norm(row.ownedWorkspace.buildOutput)&&norm(row.receipt.path)===norm(path.join(row.output,'RESULT.json')):
  norm(row.args[1])===norm(path.join(ctx.repo,route.consumer));
 need(validArgs&&norm(row.executable)===norm(ctx.tools.python),'NATIVE_EXIT_ACTUAL_CONSUMER_COMMAND');
 if(fixed){
  const job=row.ownedWorkspace, proof=job.jobIdentity;
  need(proof&&norm(proof.path)===norm(path.join(job.directory,'job.json'))&&hash(proof.path)===proof.sha256&&
   norm(path.dirname(job.directory))===norm(owner.root)&&norm(job.buildOutput)===norm(path.join(job.directory,'o'))&&
   same(job.owner,result.workspace.identity)&&same(read(proof.path),Object.fromEntries(Object.entries(job).filter(([name])=>name!=='jobIdentity'))),
   'NATIVE_EXIT_OWNED_CHILD_PIN');
 }
 if(name==='parent')need(row.args.at(-1)===mainSha256&&row.args.at(-2)==='--expected-main-sha256','NATIVE_EXIT_PARENT_EXPLICIT_FREEZE_ARG');
 values[name]=read(row.receipt.path);
}
const childCtx={...ctx,requireFixedBuild:fixed};
return {helper:validateHelper(values.helper,childCtx,result.children.helper.output),parent:validateParent(values.parent,childCtx,result.children.parent.output),gates:validateGates(values.gates,childCtx,result.children.gates.output),fullLifecycleAcceptance:'INCOMPLETE',physicalReturnVerified:false};}
