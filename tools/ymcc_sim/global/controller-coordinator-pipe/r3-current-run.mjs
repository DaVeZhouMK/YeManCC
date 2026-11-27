// Current prepared consumers for the original four pipe IDs and lifecycle ID.
// Actual process calls retain the original60/45/90 budgets; no compiler runs here.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {runProcess} from '../runner.mjs';
import {LIFECYCLE_VERSION,verifyR3NormalReturn,verifyR3Tuple} from './r3-contract.mjs';
import {verifyTerminal26Switch} from './r3-terminal26-switch-contract.mjs';
import {inspectMsiReturnCases,inspectMsiCancellationCases} from '../../../fixtures/input_coordinator_lifecycle/msi_return_observation.mjs';
import {verifyCurrent29CandidateLifecycle} from '../../../fixtures/input_coordinator_lifecycle/current29_lifecycle_receipt.mjs';
import {TERMINAL_SHARED_INPUT_POWER_MANAGED_CONTRACT} from './r3-managed-input-contract.mjs';
export {inspectMsiReturnCases};
export {verifyTerminal26Switch};
import {openCurrentPreparedContext,requireCurrentPreparedContext,currentPreparedSources,currentPreparedArtifacts,recheckCurrentPrepared,recheckCurrentPreparedContexts,verifyCurrentPreparedExecution} from './r3-current-prepared.mjs';
const need=(ok,code)=>{if(!ok)throw Error('R3_CURRENT_RUN_'+code);};
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,value)=>fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');
const pipeIds=Object.freeze({positive:'controller.coordinator-native-managed-pipe',cancellation:'controller.coordinator-native-managed-pipe.cancellation-negative',psOwnership:'controller.coordinator-native-managed-pipe.ps-ownership-negative',psBinding:'controller.coordinator-native-managed-pipe.ps-binding-negative'});
const lifecycleRoles=Object.freeze(['positive','bypassAdmission','recoveryInheritance','switchReturn','switchOwner']);
const lifecycleModes=Object.freeze(['entry','mutation1','mutation2','rollback','queued-exception','queued-exception-common','healthy-log-exception','start-protection','recovery-protection','record-read','switch-recovery','tuple-protection']);
const lifecycleMutation=Object.freeze({bypassAdmission:'bypass-admission',recoveryInheritance:'rollback-recovery-inheritance',switchReturn:'rollback-switch-return-rearm',switchOwner:'rollback-switch-fault-owner'});
export function currentLifecycleModePlan(){
const plan=lifecycleModes.map(mode=>({role:'positive',mode,mutation:null,expectedExit:mode.startsWith('mutation')?1:0}));for(const role of lifecycleRoles.slice(1)){const mutation=lifecycleMutation[role],mode=mutation.startsWith('rollback-switch-')?'switch-recovery':role==='recoveryInheritance'?'recovery-protection':'start-protection';plan.push({role,mode,mutation,expectedExit:1});}need(plan.length===16,'ORIGINAL16_MODES_REQUIRED');
 return structuredClone(plan);
}
export function currentLifecycleModeLabel(test){
 need(currentLifecycleModePlan().some(row=>row.role===test?.role&&row.mode===test.mode&&row.mutation===test.mutation&&row.expectedExit===test.expectedExit),'ORIGINAL_LIFECYCLE_PLAN_MEMBER_REQUIRED');
 return test.mutation?'negative-'+test.mutation:'positive-'+test.mode;
}
export function currentLifecycleRemainingBudget(started,now=Date.now()){
 const groupWallMs=now-started;need(Number.isFinite(groupWallMs)&&groupWallMs>=0&&groupWallMs<90000,'ORIGINAL_GROUP90_BUDGET_EXCEEDED');
 return {originalModeBudgetMs:45000,originalGroupBudgetMs:90000,groupWallMs,remainingGroupMs:90000-groupWallMs,timeoutMs:Math.min(45000,90000-groupWallMs)};
}

const tuple26File=new URL('./r3-terminal26-tuple-expectations.json',import.meta.url);
const tuple26=JSON.parse(fs.readFileSync(tuple26File,'utf8'));
const tuple26Id='current105-terminal26-sdk-lifetime-compact-v1';
const tupleInteger=x=>Number.isSafeInteger(x)&&x>=0;
const tupleSameOwner=(a,b)=>a&&b&&a.epoch===b.epoch&&a.runId===b.runId;
function tupleOwner(d){
 need(d&&tupleInteger(d.epoch)&&d.epoch>0&&typeof d.runId==='string'&&d.runId.length>0,'TUPLE_OWNER_REQUIRED');
 return {epoch:d.epoch,runId:d.runId};
}
function tupleHostWitness(c){
 need(Array.isArray(c.events),'TUPLE_NATIVE_EVENTS_REQUIRED');
 let present=false,owner=null;
 for(const e of c.events){
  need(e&&typeof e.event==='string'&&e.detail&&typeof e.detail==='object','TUPLE_NATIVE_EVENT_REQUIRED');
  const d=e.detail;
  need(!(e.event==='input-host-stop-request'&&String(d.reason).startsWith('offline-owned-cleanup')),'TUPLE_PRE_FINISH_EVENTS_REQUIRED');
  if(e.event==='input-host-started'){
   const next=tupleOwner(d);
   need(!present&&(!owner||(next.epoch>owner.epoch&&next.runId!==owner.runId))&&d.transportUsable===true,'TUPLE_START_OWNER_CONFLICT');
   owner=next;present=true;
  }else if(e.event==='input-host-process-retirement'){
   need(present&&tupleSameOwner(owner,tupleOwner(d)),'TUPLE_FOREIGN_RETIREMENT');
   need(typeof d.handleClosed==='boolean'&&typeof d.hostHandleRetained==='boolean','TUPLE_RETIREMENT_FLAGS_REQUIRED');
   if(d.handleClosed)need(d.closeAttempted===true&&d.hostExitConfirmed===true&&d.waitState===0&&d.closeError===0&&d.hostHandleRetained===false,'TUPLE_RETIREMENT_CONFLICT');
   else need(d.hostHandleRetained===true,'TUPLE_RETIREMENT_CONFLICT');
   present=d.hostHandleRetained;
  }else if(e.event==='input-host-session-retained'){
   need(tupleSameOwner(owner,tupleOwner(d))&&typeof d.hostHandleRetained==='boolean'&&d.hostHandleRetained===present,'TUPLE_RETAINED_OWNER_CONFLICT');
  }else if(e.event==='input-host-quarantine-child-exited'){
   need(present&&tupleSameOwner(owner,tupleOwner(d))&&d.processHandleClosed===true,'TUPLE_QUARANTINE_OWNER_CONFLICT');present=false;
  }else if(e.event==='input-host-quarantine-cleared'){
   need(tupleSameOwner(owner,tupleOwner({epoch:d.retiredHostEpoch,runId:d.retiredRunId}))&&!present&&d.sessionClearedBeforeRearm===true,'TUPLE_CLEARED_OWNER_CONFLICT');
  }
 }
 need(owner,'TUPLE_POSITIVE_OWNER_WITNESS_REQUIRED');
 if(present&&c.observedRuntime)need(tupleSameOwner(owner,{epoch:c.observedRuntime.hostEpoch,runId:c.observedRuntime.runId}),'TUPLE_FINAL_OWNER_CONFLICT');
 return present;
}
function tupleProbeWitness(c,expected){
 const marker=c.nextScheduledProbe,old=c.offeredRuntime,final=c.observedRuntime;
 if(!expected.hasProbe){need(!Object.hasOwn(c,'nextScheduledProbe'),'TUPLE_FOREIGN_PROBE');return false;}
 need(old&&final&&tupleInteger(old.serial)&&old.serial>0&&old.serial===final.serial&&tupleInteger(c.oldRequestId)&&c.oldRequestId>0,'TUPLE_INCIDENT_WITNESS_REQUIRED');
 if(expected.parameters.condition==='cooldown'){
  need(!Object.hasOwn(c,'nextScheduledProbe'),'TUPLE_COOLDOWN_PROBE_CONFLICT');
  need(old.pending===true&&old.spent===false&&old.recoveryRequestId===0&&old.oldRequestId===c.oldRequestId&&
   final.pending===false&&final.spent===true&&tupleInteger(final.recoveryRequestId)&&final.recoveryRequestId>0&&final.recoveryRequestId!==c.oldRequestId,'TUPLE_COOLDOWN_SUCCESSOR_REQUIRED');
  const rows=c.events.map((e,index)=>({e,index})).filter(x=>x.e.event==='input-host-runtime-recovery'&&x.e.detail.serial===final.serial);
  const restart=rows.filter(x=>x.e.detail.stage==='restart'),returned=rows.filter(x=>x.e.detail.stage==='start-returned');
  need(restart.length===1&&returned.length===1&&restart[0].index<returned[0].index,'TUPLE_COOLDOWN_RESTART_REQUIRED');
  for(const x of [restart[0],returned[0]])need(x.e.detail.recoveryRequestId===final.recoveryRequestId&&x.e.detail.spent===true&&x.e.detail.pending===false&&x.e.detail.nextProbeTick===0,'TUPLE_COOLDOWN_RESTART_CONFLICT');
  return false;
 }
 need(marker&&typeof marker==='object'&&!Array.isArray(marker),'TUPLE_POSITIVE_PROBE_REQUIRED');
 for(const key of ['beforeTick','originalNextProbe','advanceMs','oldRequestId','recoveryRequestBefore','recoveryRequestAfter'])need(tupleInteger(marker[key]),'TUPLE_TYPED_PROBE_REQUIRED');
 need(marker.advanceMs>0&&marker.advanceMs<=1000&&marker.originalNextProbe-marker.beforeTick===marker.advanceMs,'TUPLE_PROBE_DELTA_CONFLICT');
 need(marker.oldRequestId===c.oldRequestId&&marker.oldRequestId===old.oldRequestId&&marker.recoveryRequestBefore===0,'TUPLE_PROBE_REQUEST_CONFLICT');
 need(marker.pendingAfter===false&&marker.recoveryRequestAfter>0&&marker.recoveryRequestAfter!==marker.oldRequestId&&marker.recoveryRequestAfter===final.recoveryRequestId,'TUPLE_PROBE_SUCCESSOR_CONFLICT');
 return true;
}
export function verifyTerminal26Tuple(result,{tupleSource}={}){
 need(tuple26.kind==='EXPLICIT26_SOURCE_DERIVED_TUPLE_ASSERTION_GRAMMAR_V1'&&tuple26.managedInputContract===tuple26Id,'TUPLE_CONTRACT_REQUIRED');
 need(tupleSource&&typeof tupleSource.path==='string'&&path.isAbsolute(tupleSource.path)&&path.basename(tupleSource.path)==='tuple_recovery_cases.inc'&&Buffer.isBuffer(tupleSource.bytes)&&
  tupleSource.bytes.length===tuple26.sourceBindings.compiledTupleBytes&&tupleSource.sha256===tuple26.sourceBindings.compiledTupleSha256&&
  createHash('sha256').update(tupleSource.bytes).digest('hex')===tupleSource.sha256,'TUPLE_COMPILED_SOURCE_PIN_REQUIRED');
 need(result?.status==='PASS'&&result.forbiddenIo===0&&result.fullTranslationUnit===true&&result.normalProductMainExecuted===false&&result.realDeviceIo===false&&Array.isArray(result.failures)&&result.failures.length===0,'TUPLE_POSITIVE_RESULT_REQUIRED');
 need(Array.isArray(result.cases)&&result.cases.length===45&&tuple26.cases.length===45,'TUPLE_EXACT45_REQUIRED');
 const expected=[],witnesses=[];
 function expand(nodes,facts,row){
  for(const n of nodes){
   if(Number.isInteger(n)){need(typeof tuple26.labels[n]==='string','TUPLE_GRAMMAR_LABEL_REQUIRED');expected.push({case:row.name,assertion:tuple26.labels[n],sourceOrdinal:row.sourceOrdinal,nameOccurrence:1});}
   else{need(n&&['host','probe'].includes(n.fact),'TUPLE_GRAMMAR_FACT_REQUIRED');expand(facts[n.fact]?n.then:n.else,facts,row);}
  }
 }
 result.cases.forEach((c,i)=>{
  const row=tuple26.cases[i];
  need(c?.name===row.name&&c.sourceOrdinal===i+1&&row.sourceOrdinal===i+1&&c.nameOccurrence===1,'TUPLE_EXACT_CASE_ORDER_REQUIRED');
  const host=tupleHostWitness(c),probe=tupleProbeWitness(c,row);
  witnesses.push({sourceOrdinal:i+1,hostPresent:host,probeReached:probe});expand(row.grammar,{host,probe},row);
 });
 need(tupleInteger(result.checks)&&result.checks===expected.length&&Array.isArray(result.assertionTrace)&&result.assertionTrace.length===expected.length,'TUPLE_TRACE_LENGTH_REQUIRED');
 expected.forEach((e,i)=>{const a=result.assertionTrace[i];need(a&&a.checkIndex===i+1&&a.passed===true&&Object.keys(e).every(k=>a[k]===e[k]),'TUPLE_EXACT_ASSERTION_SEQUENCE_REQUIRED');});
 return {kind:tuple26.kind,cases:45,checks:expected.length,witnesses,compiledTupleSha256:tupleSource.sha256,runtimeAuthorityGranted:false};
}

// Validate the actual terminal26 check() report format; this is not a failure-mask oracle.
function verifyTerminal26SwitchNegativeReport(raw){
 need(Array.isArray(raw.cases)&&raw.cases.length>0,'SWITCH_NEGATIVE_CASES_REQUIRED');
 const occurrences=new Map();
 raw.cases.forEach((row,index)=>{need(row&&typeof row.name==='string'&&row.name.length>0,'SWITCH_NEGATIVE_CASE_NAME_REQUIRED');const occurrence=(occurrences.get(row.name)??0)+1;occurrences.set(row.name,occurrence);need(row.sourceOrdinal===index+1&&row.nameOccurrence===occurrence,'SWITCH_NEGATIVE_CASE_OCCURRENCE_REQUIRED');});
 need(tupleInteger(raw.checks)&&raw.checks>0&&Array.isArray(raw.assertionTrace)&&raw.assertionTrace.length===raw.checks,'SWITCH_NEGATIVE_TRACE_REQUIRED');
 const expectedFailures=[];
 raw.assertionTrace.forEach((row,index)=>{need(row&&row.checkIndex===index+1&&typeof row.passed==='boolean'&&typeof row.assertion==='string'&&row.assertion.length>0&&Number.isSafeInteger(row.sourceOrdinal)&&row.sourceOrdinal>0,'SWITCH_NEGATIVE_TRACE_ROW_REQUIRED');const owner=raw.cases[row.sourceOrdinal-1];need(owner&&row.case===owner.name&&row.nameOccurrence===owner.nameOccurrence,'SWITCH_NEGATIVE_TRACE_OWNER_REQUIRED');if(!row.passed)expectedFailures.push(row);});
 const signature=row=>row&&[row.case,row.assertion,row.sourceOrdinal,row.nameOccurrence,row.checkIndex];
 need(Array.isArray(raw.failures)&&raw.failures.length===expectedFailures.length&&raw.failures.every((row,index)=>JSON.stringify(signature(row))===JSON.stringify(signature(expectedFailures[index]))),'SWITCH_NEGATIVE_FAILURE_TRACE_CONFLICT');
 return {scope:'ORIGINAL_NECESSARY_ASSERTION_AND_REPORT_CONSISTENCY_ONLY',reportConsistencyVerified:true,cases:raw.cases.length,checks:raw.checks,failures:expectedFailures.length,completeFailureMaskVerified:false};
}

export function verifyCurrentLifecycleMode(test,raw,{entryText,managedInputContract,tupleSource,switchSource,terminal29Source,requireMsiReturnObservation=false,msiCancellation}={}){
 need(managedInputContract===undefined||['current105-exact21-v1','current105-terminal22-v1','current105-terminal23-sd-decoder-v1',tuple26Id,TERMINAL_SHARED_INPUT_POWER_MANAGED_CONTRACT].includes(managedInputContract),'UNKNOWN_MANAGED_CONTRACT');
 currentLifecycleModeLabel(test);
 if(msiCancellation!==undefined){
  need(managedInputContract===tuple26Id&&test.role==='positive'&&test.mode==='recovery-protection'&&!test.mutation,
   'EXPLICIT26_POSITIVE_RECOVERY_PARAMETER_ONLY');
  need(msiCancellation&&typeof msiCancellation==='object'&&!Array.isArray(msiCancellation)&&
   Object.keys(msiCancellation).every(key=>key==='boundary'||key==='phase'),'EXACT_MSI_PARAMETER_OPTIONS_REQUIRED');
  return {originalSemanticChecksVerified:false,selectedParameterSemanticChecksVerified:true,
   preparedContextGranted:false,runtimeAuthorityGranted:false,
   msiCancellation:inspectMsiCancellationCases(raw,msiCancellation)};
 }
  need(typeof requireMsiReturnObservation==='boolean','MSI_RETURN_OBSERVATION_REQUIREMENT_BOOLEAN');
  const msiReturnObservation=test.mode==='recovery-protection'&&(requireMsiReturnObservation||Array.isArray(raw.cases))?inspectMsiReturnCases(raw.cases,{required:requireMsiReturnObservation}):null;
 if(!test.mutation&&(test.mode==='entry'||test.mode.startsWith('mutation')))need(typeof entryText==='string','ORIGINAL_ENTRY_TEXT_REQUIRED');
   need(raw.forbiddenIo===0&&raw.fullTranslationUnit===true&&raw.normalProductMainExecuted===false&&raw.realDeviceIo===false,'OFFLINE_COMPLETE_NATIVE_MODE_REQUIRED');
   const negativeSemanticEvidence=managedInputContract===tuple26Id&&test.mutation&&test.mode==='switch-recovery'?verifyTerminal26SwitchNegativeReport(raw):null;
   if(test.mutation){const expected=test.role==='switchReturn'?'verified late cleanup queues only the continuation of the already charged recovery':test.role==='switchOwner'?'one current frame fault has exactly one serialized Stop owner':test.role==='recoveryInheritance'?'single real return refresh restores requested operational target':'actual durable request locks before consumer runs';need(raw.status==='FAIL'&&raw.failures.some(row=>row.assertion===expected),'ORIGINAL_LIFECYCLE_SEMANTIC_NEGATIVE_REQUIRED');}
   else if(test.mode==='entry'||test.mode.startsWith('mutation')){const n=test.mode==='entry'?0:test.mode==='mutation1'?1:2,text=entryText,rows=[...text.matchAll(/^(PASS|FAIL) (.+)$/gm)];need(rows.length===56,'ORIGINAL56_ENTRY_CASES_REQUIRED');need(n?raw.status==='FAIL'&&rows.some(row=>row[1]==='FAIL'&&row[2].trim()===(n===1?'Entry-B-ensure-after-stop':'Entry-A-armed-at-admit')):raw.status==='PASS'&&rows.every(row=>row[1]==='PASS'),'ORIGINAL_ENTRY_POLARITY_REQUIRED');}
   else need(raw.status==='PASS'&&raw.failures.length===0&&raw.checks>0,'ORIGINAL_POSITIVE_NATIVE_MODE_FAILED');
   const verifyTerminal29 = managedInputContract === TERMINAL_SHARED_INPUT_POWER_MANAGED_CONTRACT &&
     test.role === 'positive' && !test.mutation && ['recovery-protection', 'switch-recovery', 'tuple-protection'].includes(test.mode);
   if (verifyTerminal29) {
     verifyCurrent29CandidateLifecycle(test.mode, raw, terminal29Source?.proof, terminal29Source?.source);
   } else {
     if(test.mode==='switch-recovery'&&!test.mutation){if(managedInputContract===tuple26Id)verifyTerminal26Switch(raw,{switchSource});else verifyR3NormalReturn(raw);}
     if(test.mode==='tuple-protection'&&!test.mutation){if(managedInputContract===tuple26Id)verifyTerminal26Tuple(raw,{tupleSource});else verifyR3Tuple(raw);}
   }

  return {originalSemanticChecksVerified:true,preparedContextGranted:false,runtimeAuthorityGranted:false,...(negativeSemanticEvidence?{negativeSemanticEvidence}:{}),...(msiReturnObservation?.present||msiReturnObservation?.required?{msiReturnObservation}:{})};
}
const unique=items=>[...new Set(items)];
function files(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(entry=>{need(!entry.isSymbolicLink(),'OUTPUT_REPARSE_POINT');const file=path.join(root,entry.name);return entry.isDirectory()?files(file):[file];});}
function recordInputs(repo,contexts){const sources=unique(contexts.flatMap(currentPreparedSources)),artifacts=unique(contexts.flatMap(currentPreparedArtifacts));need(artifacts.length<=3072,'ORIGINAL_GLOBAL_ARTIFACT_LIMIT3072');return {sources:sources.map(relative=>({path:relative,sha256:hash(path.join(repo,relative))})),artifacts:artifacts.map(file=>({path:file,sha256:hash(file)}))};}
function recheckInputs(repo,record){for(const row of record.sources)need(hash(path.join(repo,row.path))===row.sha256,'SOURCE_CHANGED:'+row.path);for(const row of record.artifacts)need(hash(row.path)===row.sha256,'ARTIFACT_CHANGED:'+row.path);}
function compilerInputs(contexts){const result=new Map();for(const context of contexts){const state=requireCurrentPreparedContext(context);for(const row of [...state.compiler.inputs,...state.managed.evaluatedCompilerInputs]){const key=path.resolve(row.path).toLowerCase(),old=result.get(key);need(!old||old.sha256===row.sha256,'COMPILER_PIN_CONFLICT');result.set(key,row);}}return [...result.values()];}
function loadedAssemblies(raw){const rows=[];function visit(value){if(!value||typeof value!=='object')return;if(value.buildReceiptSha256&&value.inputManifestSha256&&value.processId){for(const stage of ['startupBindings','finalBindings'])rows.push(...(value[stage]?.loadedAssemblies??[]));return;}for(const child of Object.values(value))if(child&&typeof child==='object')visit(child);}visit(raw.cases);return rows;}
export function currentLifecyclePreparedSources(repo){return unique(lifecycleRoles.flatMap(role=>currentPreparedSources(openCurrentPreparedContext(role,repo))));}
export function currentLifecyclePreparedArtifacts(repo){const result=unique(lifecycleRoles.flatMap(role=>currentPreparedArtifacts(openCurrentPreparedContext(role,repo))));need(result.length<=3072,'ORIGINAL_GLOBAL_ARTIFACT_LIMIT3072');return result;}
function readLifecycleRoleInputs(role,repo){
 const context=openCurrentPreparedContext(role,repo);
 return {sources:currentPreparedSources(context),artifacts:currentPreparedArtifacts(context)};
}
export function currentLifecyclePreparedRegistration(repo,{readRole=readLifecycleRoleInputs}={}){
 const roles=lifecycleRoles.map(role=>readRole(role,repo));
 const evidenceInputs=unique(roles.flatMap(row=>row.sources));
 const artifactInputs=unique(roles.flatMap(row=>row.artifacts));
 need(artifactInputs.length<=3072,'ORIGINAL_GLOBAL_ARTIFACT_LIMIT3072');
 return {evidenceInputs,artifactInputs};
}
export async function executeCurrentPreparedPipe(root,role,repo){
 root=path.resolve(root);need(Object.hasOwn(pipeIds,role),'PIPE_ROLE_REQUIRED');need(!fs.existsSync(root),'FRESH_EVIDENCE_REQUIRED');fs.mkdirSync(root,{recursive:true});let failureStage='prerequisite',actualProcess=null,launchContext=null;
 try{
  const context=openCurrentPreparedContext(role,repo),state=requireCurrentPreparedContext(context),inputs=recordInputs(repo,[context]);recheckCurrentPrepared(context);
  const run=path.join(root,'actual');fs.mkdirSync(run);failureStage='process';
  launchContext={exe:state.canonical.nativeExe.path,exeExists:fs.existsSync(state.canonical.nativeExe.path),cwd:run,cwdExists:fs.existsSync(run),cwdCharacters:run.length};
  const args=[run,'full-pipe',state.managed.fixtureDll],processResult=await runProcess(state.canonical.nativeExe.path,args,{cwd:run,env:{...process.env,YEMAN_POWER_CONTROL_DIR:run},timeoutMs:60000});actualProcess=processResult;
  fs.writeFileSync(path.join(root,'stdout.log'),processResult.stdout);fs.writeFileSync(path.join(root,'stderr.log'),processResult.stderr);const {stdout,stderr,...terminal}=processResult;
  write(path.join(root,'process-ledger.json'),{steps:[{exe:state.canonical.nativeExe.path,args,...terminal}],lifecycleVersion:LIFECYCLE_VERSION});failureStage='process-launch';
  need(!terminal.launchError,'NATIVE_PROCESS_LAUNCH_FAILED:'+terminal.launchError);failureStage='business';
  need(fs.existsSync(path.join(run,'fixture-result.json')),'COMPLETE_NATIVE_REPORT_REQUIRED');const raw=read(path.join(run,'fixture-result.json')),verified=verifyCurrentPreparedExecution(context,raw,terminal);recheckInputs(repo,inputs);
  const generated=files(run).map(file=>({path:file,sha256:hash(file)})),result={schemaVersion:1,status:'PASS',passed:true,id:pipeIds[role],case:role,lifecycleVersion:LIFECYCLE_VERSION,checks:raw.checks,cases:raw.cases.map(row=>({name:row.name,sourceOrdinal:row.sourceOrdinal,nameOccurrence:row.nameOccurrence,assertionCount:raw.assertionTrace.filter(check=>check.sourceOrdinal===row.sourceOrdinal).length,passed:true})),rawNativeReport:{path:path.join(run,'fixture-result.json'),sha256:hash(path.join(run,'fixture-result.json'))},currentContract:{id:state.profile.kind,sourceVersion:state.profile.sourceVersion,sourceMainSha256:state.profile.sourceMainSha256,qualificationReplayedAsRuntime:false,sourceBoundCaseConsumerExecuted:true,...verified},nativeResult:{status:raw.status,originalExitCode:terminal.exitCode,expectedSemanticFailure:role!=='positive',failures:raw.failures},actualCompilerInputs:compilerInputs([context]),actualLoadedAssemblies:loadedAssemblies(raw),loadedChildren:verified.children.children,sourceHashes:[...inputs.sources,...generated],prerequisiteArtifacts:inputs.artifacts,boundary:{sourceBoundPrecompiledFixture:true,compilerExecutedInAdapter:false,fullNativeCoordinatorExecuted:true,actualManagedHostSessionExecuted:true,sdkRealIo:false,normalProductMainExecuted:false,physicalReturnAccepted:false,osSleep:false}};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'result.json'),{schemaVersion:1,status:'FAIL',passed:false,id:pipeIds[role],case:role,error:error.message,failureStage,actualProcess:actualProcess?{pid:actualProcess.pid,exitCode:actualProcess.exitCode,signal:actualProcess.signal,timedOut:actualProcess.timedOut,launchError:actualProcess.launchError,outputTruncated:actualProcess.outputTruncated,durationMs:actualProcess.durationMs}:null,launchContext,normalProductMainExecuted:false,hardwareIo:false,compilationFailureCountedAsNegative:false});throw error;}
}
export async function executeCurrentPreparedLifecycle(root,repo){
 root=path.resolve(root);need(!fs.existsSync(root),'FRESH_EVIDENCE_REQUIRED');fs.mkdirSync(root,{recursive:true});const started=performance.now(),processLedger=[],steps=[];
 try{
  const contexts=Object.fromEntries(lifecycleRoles.map(role=>[role,openCurrentPreparedContext(role,repo)])),allContexts=Object.values(contexts),inputs=recordInputs(repo,allContexts);
  recheckCurrentPreparedContexts(allContexts);
  const plan=currentLifecycleModePlan();
  for(const test of plan){
   const state=requireCurrentPreparedContext(contexts[test.role]),label=currentLifecycleModeLabel(test),dir=path.join(root,label);fs.mkdirSync(dir);
   const timeoutMs=Math.floor(currentLifecycleRemainingBudget(started,performance.now()).timeoutMs);
   need(timeoutMs>=1,'ORIGINAL_GROUP90_BUDGET_EXCEEDED');
   const args=[dir,test.mode],processResult=await runProcess(state.canonical.nativeExe.path,args,{cwd:dir,env:{...process.env,YEMAN_POWER_CONTROL_DIR:dir},timeoutMs});
   fs.writeFileSync(path.join(dir,'stdout.log'),processResult.stdout);fs.writeFileSync(path.join(dir,'stderr.log'),processResult.stderr);const {stdout,stderr,...terminal}=processResult;processLedger.push({label,exe:state.canonical.nativeExe.path,args,timeoutMs,...terminal});write(path.join(root,'process-ledger.json'),{steps:processLedger});
   need(!terminal.timedOut&&!terminal.launchError&&!terminal.signal&&!terminal.outputTruncated&&terminal.durationMs<=timeoutMs&&terminal.exitCode===test.expectedExit,'ORIGINAL_NATURAL_MODE45_EXIT_REQUIRED:'+label);need(fs.existsSync(path.join(dir,'fixture-result.json')),'NATIVE_MODE_REPORT_REQUIRED:'+label);const raw=read(path.join(dir,'fixture-result.json'));
   const entryIndex=test.mode==='entry'?0:test.mode==='mutation1'?1:2,entryText=!test.mutation&&(test.mode==='entry'||test.mode.startsWith('mutation'))?fs.readFileSync(path.join(dir,'msi-start-order-entry-selftest-m'+entryIndex+'.txt'),'utf8'):undefined;
   verifyCurrentLifecycleMode(test,raw,{entryText});
   steps.push({mode:test.mode,role:test.role,expectedNegative:!!test.mutation||test.mode.startsWith('mutation'),mutation:test.mutation,exitCode:terminal.exitCode,checks:raw.checks,originalOrderChecks:['entry','mutation1','mutation2'].includes(test.mode)?56:undefined,result:{status:raw.status,checks:raw.checks,failures:raw.failures,cases:raw.cases.map(row=>({name:row.name}))},rawReport:{path:path.join(dir,'fixture-result.json'),sha256:hash(path.join(dir,'fixture-result.json'))},path:dir});
  }
  recheckCurrentPreparedContexts(allContexts);recheckInputs(repo,inputs);
  const generated=steps.flatMap(step=>files(step.path).map(file=>({path:file,sha256:hash(file)})));
  const actualCompilerInputs=compilerInputs(allContexts),groupWallMs=performance.now()-started;
  need(groupWallMs<=90000,'ORIGINAL_GROUP90_BUDGET_EXCEEDED');
  const result={schemaVersion:1,status:'PASS',passed:true,lifecycleVersion:LIFECYCLE_VERSION,currentContract:'VC-CURRENT-105-6457-38835033-20261021',actualCompilerInputs,sourceHashes:[...inputs.sources,...generated],prerequisiteArtifacts:inputs.artifacts,steps,originalModeBudgetMs:45000,originalGroupBudgetMs:90000,groupWallMs,boundary:{fullNativeCoordinatorExecuted:true,requestIpcExecuted:true,sourceBoundPrecompiledFixture:true,compilerExecutedInAdapter:false,sdkRealIo:false,normalProductMainExecuted:false,physicalReturnAccepted:false,osSleep:false}};write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'result.json'),{schemaVersion:1,status:'FAIL',passed:false,lifecycleVersion:LIFECYCLE_VERSION,error:error.message,steps,processLedger,compilationFailureCountedAsNegative:false,normalProductMainExecuted:false,hardwareIo:false});throw error;}
}
