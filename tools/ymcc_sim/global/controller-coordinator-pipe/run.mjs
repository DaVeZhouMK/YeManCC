import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import{fileURLToPath}from'node:url';import{inputs,sources,artifacts,REPO,caseSpec}from'./adapter.mjs';import{runProcess}from'../runner.mjs';
import {LIFECYCLE_VERSION,R3_SPEC,assertR3Native,verifyCompilerLedgers,verifyR3Shape,verifyR3ProtectedPs,verifyR3BoundaryPs,verifyR3LoadedChildren} from './r3-contract.mjs';
import {LEGACY_SPEC,verifyReviewedExecution,reviewedExpectedFailures} from './r3-reviewed-contract.mjs';
import {currentPreparedProfileActive} from './r3-current-prepared.mjs';
import {selectCoordinatorProfile} from './r3-current-routing.mjs';
import {executeCurrentPreparedPipe} from './r3-current-run.mjs';
import {b54ActualProfileActive,executeB54ActualPositive} from './r3-b54-actual-worker.mjs';
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex'),need=(v,m)=>{if(!v)throw Error(m);},read=p=>JSON.parse(fs.readFileSync(p)),write=(p,v)=>fs.writeFileSync(p,JSON.stringify(v,null,2));
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'OUTPUT_LINK');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});}
function verify(c){
 const n=c.native,spec=c.spec,tag=spec.prefix;assertR3Native(n.value);need(n.value.mode==='pipe','R3_PIPE_MANIFEST_REQUIRED');
 need(hash(n.manifest)===n.pin.sha256,'NATIVE_PIN_STALE');
 need(n.value.status==='BUILT'&&n.value.expectedNegative===!!spec.mutation&&(n.value.mutation??null)===spec.mutation&&n.value.fixtureNormalMainExecuted===false&&n.value.deviceIo===false,tag+'_NATIVE_PREREQUISITE');
 need(Array.isArray(n.value.sources)&&n.value.sources.length>0&&new Set(n.value.sources.map(r=>r.path)).size===n.value.sources.length&&n.value.sources.some(r=>r.path==='native/main.cpp'),tag+'_SOURCE_MEMBERSHIP');
 for(const r of n.value.sources)need(hash(path.join(REPO,r.path))===r.sha256&&hash(r.snapshot)===r.sha256,'NATIVE_SOURCE_STALE:'+r.path);
 for(const r of n.value.generated)need(hash(r.path)===r.sha256,'NATIVE_ARTIFACT_STALE:'+r.path);
 need(n.value.generated.some(r=>r.path===n.value.exe),tag+'_EXECUTABLE_UNBOUND');
 const sourceManifest=read(n.value.generated.find(r=>path.basename(r.path)==='source-manifest.json').path);
 need(JSON.stringify(sourceManifest.records)===JSON.stringify(n.value.sources),tag+'_SOURCE_MANIFEST_MEMBERSHIP');
 const proof=read(n.value.generated.find(r=>path.basename(r.path)==='RESULT.json').path);
 need(['PASS','FAIL'].includes(proof.status)&&Array.isArray(proof.changed)&&!proof.changed.length&&(proof.mutation??null)===spec.mutation&&proof.results?.length===1&&proof.results[0].mode==='full-pipe'&&[0,1].includes(proof.results[0].exitCode),tag+'_FULL_START_STOP_COMPILED_PROOF_REQUIRED');
 need(proof.managedProof?.dll.path===c.managed.value.fixtureDll&&proof.managedProof.dll.sha256===hash(c.managed.value.fixtureDll)&&proof.managedProof.manifestSha256===c.managed.pin.sha256,'NATIVE_MANAGED_PREREQUISITE_MISMATCH');
 need(n.value.fragments?.length===2&&n.value.fragments.every(f=>f.source==='native/main.cpp'&&f.exact&&hash(f.file)===f.fileSha256&&n.value.generated.some(g=>g.path===f.file)),'ACTUAL_SOURCE_DISCOVERY_FRAGMENT_UNBOUND');
 need(c.managed.value.semanticMutation==null&&c.managed.value.inputBindings.semanticMutation==null,'NEGATIVE_MANAGED_IS_NOT_POSITIVE_ACCEPTANCE');
 need(hash(c.managed.manifest)===c.managed.pin.sha256,'MANAGED_PIN_STALE');
 for(const r of c.managed.value.inputBindings.sources)need(hash(r.path)===r.sha256&&hash(r.snapshot)===r.sha256,'MANAGED_SOURCE_STALE:'+r.path);
 for(const r of c.managed.value.inputBindings.generatedInputs)need(hash(r.path)===r.sha256,'MANAGED_GENERATED_STALE');
 for(const r of c.managed.value.runtimeArtifacts)need(hash(r.path)===r.sha256,'MANAGED_OUTPUT_STALE');
 verifyCompilerLedgers(n.value,c.managed.value);
 verifyR3Shape(proof.results[0].result,{positive:false,exactChecks:false});
 // A compiled original business FAIL may be replayed; only actual public execution can pass.
 verifyReviewedExecution(c,proof.results[0].result,{exitCode:proof.results[0].exitCode});
}
const FULL_CHECKS=R3_SPEC.expectedPipeChecks,FULL_GROUPS=R3_SPEC.pipeCaseNames.length,PS_CASE='ROG/full-coordinator-real-managed-pipe/';
const TARGET_FAILURES=[
 'new target generation invalidates a slow observation even when instance names are reused',
 'new target requires its own fresh ownership query and no second recreate'
];
const PEER_FAILURES=[
 'same-profile peer or retired endpoint cannot prove current owned PS readiness',
 'same-profile unowned observation defers without clearing recovery or rebuilding beside a peer'
];
const failureRows=(name,assertions)=>assertions.map(assertion=>({case:name,assertion}));
function cancellationFailures(){
 const recreate='cancellation during actual endpoint query cannot send RECREATE_TARGET',retired='cancelled query cannot mark rebuilt or rearm its retired request',count='actual coordinated target count matches single prepare or explicit in-process replacement',direct='actual RECREATE primitive refuses changed admission at send boundary',frame='fresh normal input after cancellation cannot reach Host frame terminal',unbound='normal input without capture TLS cannot bypass actual user OFF or changed persona';
 return Object.entries({
  off:[frame,unbound,direct,recreate,retired,count],exit:[direct,recreate,retired,count],power:[direct,recreate,retired,count],
  generation:[recreate,'older endpoint query cannot clear or rebuild a newer pending PS request','newer PS request requires its own fresh query before reusing existing target',count],
  persona:[frame,unbound,direct,recreate,retired,'old Edge Host closes naturally without stale-query recreation before requested persona replacement starts'],
  reentry:[recreate,retired,count],target:TARGET_FAILURES
 }).flatMap(([kind,assertions])=>failureRows(PS_CASE+kind+'-during-ps-probe/dualsense-edge',assertions));
}
function ownershipFailures(){return[
 ...['none','ps-late-ownership-read'].flatMap(kind=>[...failureRows(PS_CASE+kind+'/dualsense-edge',PEER_FAILURES),...failureRows(PS_CASE+kind+'/dualsense-edge',PEER_FAILURES)]),
 ...failureRows('PS-WAKE/read-only-endpoint-enumeration',Array(6).fill('actual PS endpoint reader does not turn incomplete observation into present/absent authority'))
];}
function verifyFailureRows(result,expected,prefix){
 const keys=rows=>rows.map(f=>JSON.stringify([f.case,f.assertion])).sort();
 need(Array.isArray(result.failures)&&result.failures.length===expected.length&&JSON.stringify(keys(result.failures))===JSON.stringify(keys(expected)),prefix+'_ORIGINAL_ASSERTIONS');
}
const FULL_CASE_NAMES=R3_SPEC.pipeCaseNames;
function expectedFailures(key){return reviewedExpectedFailures(key);}
function verifyFullResult(result,status,prefix){
 verifyR3Shape(result,{positive:status==='PASS'});
 need(result.status===status&&result.checks===FULL_CHECKS&&result.cases?.length===FULL_GROUPS&&result.cases.every((t,i)=>t.name===FULL_CASE_NAMES[i])&&result.forbiddenIo===0&&result.fullTranslationUnit===true&&result.normalProductMainExecuted===false&&result.realDeviceIo===false,prefix+'_FULL_CHAIN');
}
// Validate both clocks and the single real verification exit, including on
// the rollback builds whose original failure pairs must remain unchanged.
export function verifySwitchVerification(test){
 const waits=test.playerLockWaits,stops=test.stopProtectionReceipts,rejected=test.lockedPlayerRequests;
 need(Array.isArray(waits)&&waits.length>0&&Array.isArray(stops)&&stops.length>0&&Array.isArray(rejected)&&rejected.length>0,'SWITCH_VERIFICATION_RECEIPTS_MISSING');
 const locked=v=>v?.locked===true&&Number.isSafeInteger(v.requestId)&&v.requestId>0&&Number.isSafeInteger(v.elapsedMs)&&v.elapsedMs>=0&&v.elapsedMs<30000&&v.remainingMs===30000-v.elapsedMs;
 for(const row of rejected)need(locked(row.protection)&&row.sideEffectsUnchanged===true&&row.result?.ok===false&&row.result.reason==='virtual-start-protection-active'&&row.result.startProtection?.locked===true&&row.result.startProtection.requestId===row.protection.requestId,'LOCKED_PLAYER_REQUEST_CHANGED_SETTINGS_QUEUE_OR_PROCESS');
 for(const stop of stops)need(locked(stop)&&stop.persona==='disabled'&&stop.phase==='verifying'&&stop.verificationPending===true&&['success','failure'].includes(stop.candidateOutcome)&&stop.verificationRemainingMs===10000,'ACTUAL_STOP_CANDIDATE_WAS_FINAL_OR_RENEWED');
 const verification=test.nativeEvents.filter(e=>e.event==='input-host-switch-verification').map(e=>e.detail);
 need(verification.every(v=>v.stage==='inspection'||v.stage==='final'),'SWITCH_VERIFICATION_STAGE_UNKNOWN');
 const finals=verification.filter(v=>v.stage==='final'),inspections=verification.filter(v=>v.stage==='inspection');
 for(const v of inspections)need(Number.isSafeInteger(v.requestId)&&v.requestId>0&&waits.some(w=>w.before.requestId===v.requestId)&&Number.isSafeInteger(v.checks)&&v.checks>0&&v.candidateIsFinal===false&&v.replay===false&&typeof v.evidence==='string'&&v.evidence.length>0&&(['expected-paused','classified-transport-fault','current-local-chain-healthy','evidence-unknown'].includes(v.state)||(v.state==='classified-owned-endpoint-fault'&&v.evidence==='complete-current-owned-ps-endpoint-absence')),'SWITCH_INSPECTION_INVALID');
 for(const v of finals){need(typeof v.inspectionFactsAvailable==='boolean','SWITCH_INSPECTION_SCOPE_MISSING');need(v.lifecycleVersion==='VC-LIFECYCLE-20261020-R3'&&Number.isSafeInteger(v.fallbackAttempts)&&v.fallbackAttempts>=0&&v.fallbackAttempts<=1,'SWITCH_LIFECYCLE_CONTRACT_MISSING');}
 for(const v of finals)if(v.inspectionFactsAvailable){
  need(Number.isSafeInteger(v.inspectionChecks)&&v.inspectionChecks>=0&&['healthyChecks','excludedChecks','knownFaultChecks'].every(k=>Number.isSafeInteger(v[k])&&v[k]>=0&&v[k]<=v.inspectionChecks),'SWITCH_INSPECTION_COUNTERS_INVALID');
  if(v.inspectionChecks>0)need(inspections.some(i=>i.requestId===v.requestId&&i.checks<=v.inspectionChecks),'SWITCH_INSPECTION_MISSING');
 }
 need(finals.length===waits.length&&new Set(finals.map(v=>v.requestId)).size===finals.length&&new Set(waits.map(w=>w.before.requestId)).size===waits.length,'SWITCH_FINAL_MISSING_OR_DUPLICATED');
 const candidates=test.nativeEvents.filter(e=>e.event==='virtual-start-protection'&&e.detail.action==='verification-pending').map(e=>e.detail);
 need(candidates.some(v=>v.requestId===1&&v.origin==='current-source-frame-accepted'&&v.phase==='verifying'&&v.candidateOutcome==='success'&&v.verificationPending===true&&v.verificationRemainingMs===10000),'CURRENT_FRAME_SUCCESS_CANDIDATE_MISSING');
 for(const {before,afterVerification,after}of waits){
  need(locked(before)&&after?.requestId===before.requestId&&after.persona===before.persona&&after.candidateOutcome===before.candidateOutcome&&!after.locked&&!after.verificationPending&&after.remainingMs===0&&after.verificationRemainingMs===0,'SWITCH_RELEASE_CHANGED_INTENT_OR_LEFT_LOCKED');
  if(before.verificationPending){
   need(before.phase==='verifying'&&['success','failure'].includes(before.candidateOutcome)&&Number.isSafeInteger(before.verificationRemainingMs)&&before.verificationRemainingMs>=0&&before.verificationRemainingMs<=10000,'SWITCH_CANDIDATE_CLOCK_INVALID');
   if(before.verificationRemainingMs<before.remainingMs){
    need(afterVerification?.requestId===before.requestId&&afterVerification.persona===before.persona&&afterVerification.candidateOutcome===before.candidateOutcome&&afterVerification.elapsedMs===before.elapsedMs+before.verificationRemainingMs&&afterVerification.verificationRemainingMs===0,'FIRST_TEN_SECOND_VERIFICATION_CHECK_MISSING');
    if(afterVerification.locked)need(locked(afterVerification)&&afterVerification.phase==='verifying'&&afterVerification.verificationPending===true&&after.phase==='expired','MISSING_PROOF_UNLOCKED_OR_RENEWED_SWITCH');
    else need(JSON.stringify(afterVerification)===JSON.stringify(after)&&['success','failure'].includes(after.phase),'VERIFIED_FINAL_CHANGED_AFTER_RELEASE');
   }else need(afterVerification===null&&after.phase==='expired','LATE_CANDIDATE_EXTENDED_ORIGINAL_CAP');
  }else need(before.phase==='pending'&&before.candidateOutcome===''&&before.verificationRemainingMs===0&&afterVerification===null&&after.phase==='expired','INITIAL_PENDING_COMPLETED_WITHOUT_CANDIDATE');
  const final=finals.find(v=>v.requestId===before.requestId),expired=after.phase==='expired';
  need(expired?after.elapsedMs===30000:['success','failure'].includes(after.phase)&&after.elapsedMs<30000&&after.elapsedMs>=before.elapsedMs+before.verificationRemainingMs,'SWITCH_RELEASE_BEFORE_VERIFICATION_OR_AFTER_CAP');
  const reason=expired?'original-admission-deadline-expired':after.persona==='disabled'?(after.phase==='success'?'disabled-local-cleanup-verified':'disabled-local-cleanup-unproven'):after.phase==='success'?'current-intent-host-and-fresh-source-verified':'candidate-failure';
  need(final?.stage==='final'&&final.persona===after.persona&&final.candidateOutcome===after.candidateOutcome&&final.candidateIsFinal===false&&final.outcome===(expired?'failure':after.phase)&&final.finalReason===reason&&final.elapsedMs===after.elapsedMs&&final.gameInputVerified===false&&final.resultScope==='local-switch-verification','SWITCH_FINAL_NOT_BOUND_TO_ACTUAL_VERIFICATION');
 }
 if(test.controlledFamily==='GPD'||test.name==='full-coordinator-real-managed-pipe/context-dispose-once')need(stops[0].elapsedMs===27000&&stops[0].remainingMs===3000&&stops[0].verificationRemainingMs===10000,'LATE_STOP_DID_NOT_RETAIN_THIRTY_SECOND_CAP');
}
function verifyCleanPsChild(test,creates){const child=test.managedReceipt;
 need(test.controlledFamily==='ROG'&&test.requestedPersona==='dualsense-edge'&&child?.status==='OBSERVED'&&child.failure===null&&child.creates===creates&&child.nativeObservedProcessExit&&child.nativeObservedExitCode===0&&child.finalCleanupOk&&!child.hasController&&!child.hasContext&&child.pipeLoopReturned&&!test.afterStop.quarantined&&!test.afterStop.prepared,'PS_OWNED_CHILD_NOT_RELEASED');
 need(child.startupBindings&&child.finalBindings&&child.actualProductionPipeLoop&&child.actualProductionMessageCodec&&!child.hardwareIo&&!child.realSdkLoaded&&!child.productMainExecuted,'PS_CHILD_SOURCE_BINDING_MISSING');
}
function verifyPsRecovery(test,{ownershipRollback=false}={}){const stages=test.psWakeRecovery,child=test.managedReceipt;
 need(test.requestedPersona==='dualsense-edge'&&test.psWakeQueuedBeforeOff===true&&stages?.length===5,'PS_CURRENT_WORKER_CHAIN_MISSING');
 need(stages[0].phase==='unknown-budget-exhausted'&&stages[0].queryCalls===5&&stages[0].recreateCommands===0,'PS_UNKNOWN_READ_EXCEEDED_BUDGET_OR_REBUILT');
 for(const [i,suffix]of [[1,'HIDMAESTRO-OTHER-PROCESS-B'],[2,'HIDMAESTRO-OLD-TARGET-GENERATION']]){const stage=stages[i];need(stage.phase==='same-profile-not-current-target'&&stage.injectedPath==='HID#VID_054C&PID_0DF2#'+suffix&&stage.profileOnlyTruth==='controlled-terminal-not-native-ownership-proof'&&stage.actualReady===ownershipRollback&&stage.rearmRequested===!ownershipRollback&&stage.recreateCommands===0&&stage.hostEpochUnchanged===true&&stage.requestIdUnchanged===true,'PS_SAME_PROFILE_PEER_OR_RETIRED_WAS_OWNED');}
 need(stages.every((s,i)=>Number.isSafeInteger(s.generation)&&(i===0||s.generation>stages[i-1].generation)),'PS_RECOVERY_STAGES_NOT_DISTINCT');
 need(stages[3].phase==='foreign-then-own-reused'&&stages[3].recreateCommands===0,'PS_FOREIGN_OR_PRESENT_REBUILT');
 need(stages[4].phase==='complete-absence-rebuilt'&&stages[4].recreateCommands===1&&stages[4].resumedFrame.hostSubmission==='frame-accepted'&&stages[4].resumedFrame.persona==='dualsense-edge','PS_REPLACEMENT_DID_NOT_RESUME_CURRENT_EDGE_INPUT');
 const unknown=test.nativeEvents?.filter(e=>e.event==='ps-wake-rearm-deferred'&&e.detail.reason==='probe-unknown')??[];
 need(unknown.length===5&&unknown.every(e=>e.detail.probeComplete===false&&e.detail.probeReadError===5&&!e.detail.probeLimitReached),'PS_DIAGNOSTIC_READ_FAILURE_EVIDENCE_MISSING');
 need(child.creates===2&&child.observations.filter(v=>v.kind==='command-decoded'&&v.command==='RECREATE_TARGET').length===1,'PS_REAL_PIPE_RECREATE_MISSING');
}
function verifyTargetBinding(test,{bindingRollback=false}={}){const q=test.psWakeCancellation,b=test.psWakeBindingGeneration;
 need(test.name===PS_CASE+'target-during-ps-probe/dualsense-edge'&&q?.kind==='target'&&q.callbackExecuted&&q.probeCalls===(bindingRollback?1:2)&&q.directRecreateReturned&&q.recreateCommandsAdded===1&&q.processesAdded===0&&q.frameCommandsAdded===0&&!q.markedRebuilt&&!q.rearmRequested&&q.requestIdBefore===q.requestIdAfter,'PS_TARGET_BINDING_QUERY_CHAIN_MISSING');
 need(b?.sameRootInstanceId===true&&b.beforeGeneration===1&&b.afterGeneration===2&&b.retiredQueryDeferred===!bindingRollback&&b.oldQueryMarkedReady===bindingRollback,'PS_SAME_NAME_OLD_QUERY_NOT_RETIRED');
 need(b.currentFrame?.hostSubmission==='frame-accepted'&&b.currentFrame.persona==='dualsense-edge'&&test.frameInputChain?.length===9&&test.managedReceipt.events.filter(e=>e==='frame').length===7,'PS_NEW_BINDING_FRESH_QUERY_DID_NOT_RECOVER');
 const pins=test.nativeEvents.filter(e=>e.event==='ps-endpoint-ownership').map(e=>e.detail);
 need(pins.length===2&&pins[0].phase==='prepare'&&pins[1].phase==='recreate'&&pins.every(p=>p.complete)&&pins[0].bindingGeneration===1&&pins[1].bindingGeneration===2&&pins[0].sdkRootInstanceId&&pins[0].sdkRootInstanceId===pins[1].sdkRootInstanceId,'PS_SAME_INSTANCE_GENERATION_PROOF_MISSING');
 verifyCleanPsChild(test,2);
}
function verifyLateOwnership(test,options={}){const late=test.lateOwnershipRecovery,child=test.managedReceipt;
 need(test.name===PS_CASE+'ps-late-ownership-read/dualsense-edge'&&late?.beforeComplete===false&&late.afterComplete===true&&late.bindingGeneration===1&&late.queriesAdded===1&&late.recreatesAdded===0&&late.processesAdded===0,'PS_LATE_OWNERSHIP_REBUILT_OR_SPAWNED');
 need([late.beforeFrame,late.afterFrame].every(f=>f?.hostSubmission==='frame-accepted'&&f.persona==='dualsense-edge'),'PS_LATE_OWNERSHIP_INTERRUPTED_INPUT');
 const decoded=child.observations.filter(o=>o.kind==='command-decoded'),queries=decoded.filter(o=>o.command==='QUERY_TARGET_OWNERSHIP');
 need(queries.length===1&&child.observations.filter(o=>o.kind==='handle-end'&&o.command==='QUERY_TARGET_OWNERSHIP').length===1&&decoded.some(o=>o.command==='SUBMIT_FRAME'&&o.index<queries[0].index)&&decoded.some(o=>o.command==='SUBMIT_FRAME'&&o.index>queries[0].index),'PS_LATE_OWNERSHIP_READ_ONLY_COMMAND_MISSING');
 need(test.frameInputChain?.length===9&&child.events.filter(e=>e==='frame').length===9,'PS_LATE_OWNERSHIP_FRAME_CHAIN_MISSING');
 verifyCleanPsChild(test,2);verifyPsRecovery(test,options);
}
const READER_EXPECTATIONS=[
 ['open-unavailable','unknown'],['complete-empty','absent'],['complete-own-present','present'],['complete-foreign-present','foreign'],['complete-physical-only','absent'],['enumeration-denied-before-first','unknown'],['enumeration-failed-after-own','unknown'],['detail-size-unavailable','unknown'],['detail-read-failed','unknown'],['interface-bound-without-complete-end','unknown'],['partial-detail-after-foreign','unknown'],
 ['complete-same-profile-peer','foreign'],['complete-same-profile-retired','foreign'],['new-HID-same-SDK-root','present'],['parent-query-denied','unknown'],['instance-query-unavailable','unknown'],['parent-cycle','unknown'],['parent-depth-truncated','unknown'],['profile-present-without-owned-pin','unknown']
];
const OWNERSHIP_ROLLBACK_PRESENT=new Set(['complete-same-profile-peer','complete-same-profile-retired','parent-query-denied','instance-query-unavailable','parent-cycle','parent-depth-truncated']);
function verifyReader(probe,{ownershipRollback=false}={}){
 need(probe.name==='PS-WAKE/read-only-endpoint-enumeration'&&probe.actualProductionReaderAndDecision===true&&probe.controlledSetupApiLeaves===true&&probe.endpointReads?.length===19,'PS_READ_BOUNDARY_GROUP_MISSING');
 for(const [i,row]of probe.endpointReads.entries()){
  const expectedActual=ownershipRollback&&OWNERSHIP_ROLLBACK_PRESENT.has(row.scenario)?'present':READER_EXPECTATIONS[i][1];
  need(row.scenario===READER_EXPECTATIONS[i][0]&&row.expected===READER_EXPECTATIONS[i][1]&&row.actual===expectedActual&&row.rebuild===(expectedActual==='absent')&&row.enumerationCalls>=0&&row.enumerationCalls<=256,'PS_READ_FAILURE_BECAME_REBUILD_AUTHORITY');
  need(row.destroyCalls===(['open-unavailable','profile-present-without-owned-pin'].includes(row.scenario)?0:1),'PS_READ_HANDLE_NOT_RELEASED');
  need(row.action===(expectedActual==='absent'?'rebuild':expectedActual==='present'?'reuse':'defer'),'PS_READ_DECISION_MISSING');
  if(['complete-same-profile-peer','complete-same-profile-retired'].includes(row.scenario))need(row.ownMatches===(ownershipRollback?1:0)&&row.foreignVirtual===(ownershipRollback?0:1)&&row.terminalEnumerationObserved===true,'PS_READER_PEER_OR_RETIRED_WAS_OWNED');
  if(row.scenario==='new-HID-same-SDK-root')need(row.ownMatches===1&&row.foreignVirtual===0&&row.terminalEnumerationObserved===true,'PS_READER_NEW_HID_LOST_SDK_ROOT');
 }
}
export function verifyLineageReceipt(test){
 need(test?.name==='VERIFICATION/current-source-lineage-real-managed-pipe'&&test.controlledFamily==='ROG'&&test.requestedPersona==='steamdeck','SOURCE_LINEAGE_GROUP_MISSING');
 const p=test.lineageProof,c=p?.candidate,w=p?.withoutAck,v=p?.verified,child=test.managedReceipt;
 need(c?.locked&&c.phase==='verifying'&&c.candidateOutcome==='success'&&c.verificationRemainingMs===10000,'SOURCE_LINEAGE_CANDIDATE_MISSING');
 need(w?.requestId===c.requestId&&w.locked&&w.phase==='verifying'&&w.verificationRemainingMs===0&&w.elapsedMs===10000&&w.remainingMs===20000,'SOURCE_LINEAGE_UNLOCKED_BEFORE_CURRENT_ACK');
 need(Number.isSafeInteger(p.oldContinuity)&&p.oldContinuity>0&&Number.isSafeInteger(p.newContinuity)&&p.newContinuity>p.oldContinuity,'SOURCE_LINEAGE_DID_NOT_ADVANCE');
 need(p.firstAck?.hostSubmission==='frame-accepted'&&p.newAck?.hostSubmission==='frame-accepted'&&p.newAck.focusIsolationSemanticNeutral===false&&p.processes===1&&p.hostEpoch>0,'SOURCE_LINEAGE_CURRENT_ACK_MISSING');
 need(v?.requestId===c.requestId&&v.phase==='success'&&!v.locked&&v.elapsedMs===10000&&v.remainingMs===0,'SOURCE_LINEAGE_ON_NOT_VERIFIED');
 const final=test.nativeEvents.find(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='final'&&e.detail.requestId===v.requestId)?.detail;
 need(final?.outcome==='success'&&final.acceptedSourceRequestId===v.requestId&&final.acceptedSourceHostEpoch===p.hostEpoch&&final.acceptedSourceContinuity===p.newContinuity&&Number.isSafeInteger(final.acceptedSourceSequence)&&final.acceptedSourceSequence>0,'SOURCE_LINEAGE_FINAL_USED_OLD_OR_FOREIGN_RECEIPT');
 need(final.fallbackAttempts===0&&!test.nativeEvents.some(e=>e.event==='input-host-recovery-start'||e.event==='input-host-switch-recovery'&&e.detail.stage==='started'),'SOURCE_LINEAGE_NEEDED_FALLBACK');
 need(child?.status==='OBSERVED'&&child.failure===null&&child.nativeObservedProcessExit&&child.nativeObservedExitCode===0&&child.creates===1&&child.finalCleanupOk&&child.pipeLoopReturned&&child.events.filter(e=>e==='frame').length>=2&&!test.afterStop?.prepared&&!test.afterStop?.quarantined,'SOURCE_LINEAGE_CHILD_NOT_CLEAN');
}
function verifyGroups(r,options={}){
for(const name of LEGACY_SPEC.pipeCaseNames.slice(0,19))verifySwitchVerification(r.cases.find(test=>test.name===name));
const names=['none','controller-dispose-once','context-dispose-once','none','none','recreate-owned-ids-rotate','recreate-owned-ids-foreign','recreate-owned-ids-unbound-ack','none'],families=['MSI','MSI','MSI','ROG','GPD','MSI','MSI','MSI','ROG'];for(let i=0;i<9;++i){const test=r.cases[i],child=test.managedReceipt;need(test.name===(i===3||i===8?'ROG/':i===4?'GPD/':'')+'full-coordinator-real-managed-pipe/'+names[i]+(i===8?'/dualsense-edge':'')&&test.controlledFamily===families[i],'CASE_MISSING');need(child.status==='OBSERVED'&&child.failure===null&&child.nativeObservedProcessExit&&child.creates===(i>=5?2:1)&&child.pipeLoopReturned,'ACTUAL_CHILD_LIFECYCLE_MISSING');need(child.startupBindings&&child.finalBindings&&child.actualProductionPipeLoop&&child.actualProductionMessageCodec,'CHILD_SOURCE_BINDING_MISSING');
const flow=test.frameInputChain;need(flow?.length===9&&child.events.filter(v=>v==='frame').length===(i===0||i===3||i===4?10:i===5||i===8?7:6),'FRAME_CHAIN_OBSERVATIONS_MISSING');
need(flow[0].phase==='previous-power-generation'&&flow[0].result.focusIsolationSemanticNeutral&&flow[0].result.sourceContextReason==='canonical-read-context-retired'&&flow[0].protectionPhase==='pending','OLD_SOURCE_COMPLETED_START');
need(flow[1].phase==='source-disconnected'&&flow[1].result.focusIsolationSemanticNeutral&&flow[1].protectionPhase==='pending','ABSENT_SOURCE_COMPLETED_START');
need(flow[2].phase==='explicit-semantic-neutral'&&flow[2].protectionPhase==='pending'&&(families[i]==='MSI'?flow[2].result.hostSubmission==='frame-accepted':flow[2].result.publicationReason==='opening-fresh-neutral'),'FAMILY_NEUTRAL_GATE_CHANGED');
need(flow[3].phase==='power-not-admitted'&&flow[3].result.hostSubmission==='not-admitted-or-failed'&&flow[3].hostSequence===flow[2].hostSequence&&flow[3].protectionPhase==='pending','POWER_HOLD_WROTE_FRAME');
need(flow[6].phase==='fresh-neutral-admission'&&flow[6].result.hostSubmission==='frame-accepted'&&!flow[6].result.focusIsolationSemanticNeutral&&flow[6].protectionPhase==='verifying','CURRENT_SOURCE_DID_NOT_CREATE_CANDIDATE');
need(flow[7].phase==='live-buttons-and-axis'&&flow[7].result.hostSubmission==='frame-accepted'&&!flow[7].result.focusIsolationSemanticNeutral&&flow[7].protectionPhase==='verifying','LIVE_FRAME_NOT_ACCEPTED');
need(flow[8].phase==='post-admission-semantic-neutral'&&flow[8].result.hostSubmission==='frame-accepted'&&flow[8].result.focusIsolationSemanticNeutral&&flow[8].protectionPhase==='verifying','RELEASE_NEUTRAL_FAILED');
if(i===0||i===3||i===4){const bursts=test.duplicateStartBursts;need(bursts?.length===2&&bursts[0].phase==='pending-before-first-frame'&&bursts[1].phase==='current-frame-success-candidate','START_STORM_PHASES_MISSING');need(bursts.every(b=>b.requests===128&&b.commandsAdded===0&&b.processesAdded===0&&b.requestIdBefore===b.requestIdAfter&&b.remainingBeforeMs===b.remainingAfterMs),'DUPLICATE_START_REBUILT_OR_RENEWED_PROTECTION');need(bursts[0].remainingBeforeMs===30000&&bursts[1].remainingBeforeMs===29900,'START_STORM_PROTECTION_BOUNDARY_MISSING');}
if(i===0||i===3||i===4){need(test.powerCycles?.length===2&&test.powerRearmQueuedBeforeOff===true,'POWER_CYCLES_MISSING');let previous=0;for(const cycle of test.powerCycles){need(cycle.generation>previous&&cycle.staleReleaseIgnored&&cycle.staleRearmIgnored,'STALE_POWER_GENERATION_ACCEPTED');previous=cycle.generation;need(cycle.suspended.hostSubmission==='not-admitted-or-failed'&&cycle.retainedProcessId===child.processId&&cycle.afterRearm.hostHandle&&cycle.afterRearm.prepared,'SUSPEND_LOST_TARGET_OR_WROTE');need(cycle.retiredRead.hostSubmission==='frame-accepted'&&cycle.retiredRead.focusIsolationSemanticNeutral&&cycle.retiredRead.sourceContextReason==='canonical-read-context-retired','OLD_POWER_READ_REPLAYED');need(cycle.resumed.hostSubmission==='frame-accepted'&&!cycle.resumed.focusIsolationSemanticNeutral&&cycle.protocolPowerGeneration<cycle.generation,'RETAINED_HOST_INPUT_DID_NOT_RESUME');const wake=cycle.wake;need(wake?.generation===cycle.generation&&wake.keptGeneration===cycle.generation&&wake.hideCalls===1&&wake.diagnosticReadbackExcluded===true,'WAKE_POLICY_OBSERVATION_MISSING');need(wake.keptReceipt?.action==='kept-target'&&wake.keptReceipt.hidRefreshed===wake.controlledHideResult&&wake.targetAfterWake.hostHandle&&wake.targetAfterWake.epoch===test.afterStart.epoch&&wake.targetAfterWake.runId===test.afterStart.runId,'WAKE_DID_NOT_RETAIN_CURRENT_TARGET');need(wake.controlledHideResult===(cycle===test.powerCycles[0]),'WAKE_HIDE_FAILURE_CASE_MISSING');if(i===0)need(wake.msiNoSlotHandshake===true,'MSI_DINPUT_WRONGLY_REQUIRES_XINPUT_SLOT');}need(test.wakeQueuedBeforeOff===true,'LATE_WAKE_OFF_CASE_MISSING');need(!test.afterLateRearmOff.hostHandle&&!test.afterLateRearmOff.prepared&&!test.afterLateRearmOff.quarantined,'LATE_POWER_REARM_RESURRECTED_OFF');}

if(i===0||i===3||i===4||i===5||i===8)need(child.nativeObservedExitCode===0&&child.finalCleanupOk&&!child.hasController&&!child.hasContext&&!test.afterStop.quarantined,'ORDERED_RELEASE_FAILED');else if(i===1||i===2)need(child.nativeObservedExitCode===5&&!child.finalCleanupOk&&child.hasContext&&test.afterStop.quarantined&&test.afterStop.startBlocked,'UNPROVEN_RELEASE_WAS_CLEARED');else need((i===6||i===7)&&child.nativeObservedExitCode===5&&child.finalCleanupOk&&!child.hasController&&!child.hasContext&&test.afterStop.quarantined&&test.afterStop.startBlocked,'INVALID_IDENTITY_QUARANTINE_NOT_RETAINED');
if(i>=5&&i<=7){const pin=test.recreateOwnedPin;need(pin&&pin.before.includes('USB\\VID_28DE&PID_1205\\OWNED-1'),'INITIAL_RECREATE_OWNED_PIN_MISSING');if(i!==7)need(pin.wireEvidence?.required&&pin.wireEvidence.ownedTargetCount===3,'RECREATE_CURRENT_PIN_EVIDENCE_MISSING');need(child.events.filter(v=>v==='sdk-create').length===2&&child.observations.some(v=>v.kind==='command-decoded'&&v.command==='RECREATE_TARGET'),'ACTUAL_RECREATE_NOT_EXECUTED');need(pin.afterRecreate.epoch===test.afterStart.epoch&&pin.afterRecreate.runId===test.afterStart.runId,'RECREATE_HOST_TUPLE_CHANGED');if(i===5){need(pin.returned&&pin.pinnedAfter&&pin.after.includes('USB\\VID_28DE&PID_1205\\OWNED-2')&&!pin.after.includes('USB\\VID_28DE&PID_1205\\OWNED-1'),'RECREATE_RETAINED_OLD_PIN');need(JSON.stringify([...pin.after].sort())===JSON.stringify([...pin.wireEvidence.ownedTargets].sort())&&!test.afterStop.prepared&&!test.afterStop.quarantined,'RECREATE_RETURN_DID_NOT_FINISH');}else if(i===7){need(pin.unboundAck&&!pin.returned&&!pin.pinnedAfter&&!pin.oldIdsAbsentWhileNewTargetPresentWouldPass&&pin.afterRecreate.prepared&&!pin.afterRecreate.neutralized&&pin.sequenceAfter===pin.sequenceBefore&&test.afterStop.quarantined,'UNBOUND_RECREATE_RETAINED_OLD_AUTHORITY');const handled=child.observations.find(v=>v.kind==='handle-end'&&v.command==='RECREATE_TARGET');need(handled?.state.recreatedOwnedPin?.ownedTargets.includes('USB\\VID_28DE&PID_1205\\OWNED-2'),'UNBOUND_CONTROL_DID_NOT_CREATE_NEW_ENDPOINTS');}else need(pin.foreign&&!pin.returned&&!pin.pinnedAfter&&pin.afterRecreate.prepared&&!pin.afterRecreate.neutralized&&!pin.afterRecreate.active&&pin.sequenceAfter===pin.sequenceBefore+1&&test.afterStop.quarantined&&test.afterStop.startBlocked&&child.finalCleanupOk,'INVALID_RECREATE_IDENTITY_WAS_ADMITTED');}
if(i===8)verifyPsRecovery(test,options);
if(i===1||i===2){const phases=['unknown-PnP','incomplete-PnP','owned-endpoint-present','absent-wake-hold','physical-restore-failed','verified-restore'];need(test.recoveryProbes?.length===phases.length,'RECOVERY_OBSERVATIONS_MISSING');for(let k=0;k<phases.length;++k){const probe=test.recoveryProbes[k];need(probe.phase===phases[k]&&probe.state.quarantined===(k<5),'RECOVERY_GATE_BYPASSED');}need(test.recoveryProbes[5].state.startBlocked&&!test.recoveryProbes[5].state.prepared&&!test.recoveryProbes[5].state.runId,'DEAD_SESSION_NOT_RETIRED');need(test.afterExplicitRestart?.prepared&&!test.afterExplicitRestart.quarantined&&!test.afterExplicitRestart.startBlocked,'EXPLICIT_RESTART_FAILED');const restarted=test.restartManagedReceipt;need(restarted?.nativeObservedProcessExit&&restarted.nativeObservedExitCode===0&&restarted.finalCleanupOk&&!restarted.hasController&&!restarted.hasContext&&restarted.pipeLoopReturned,'RESTARTED_HOST_CLEANUP_FAILED');}}

for(let i=9;i<13;++i){if(options.cancellationRollback)continue;const test=r.cases[i],child=test.managedReceipt,c=test.psWakeCancellation,kind=['off','exit','power','generation'][i-9];
need(test.name==='ROG/full-coordinator-real-managed-pipe/'+kind+'-during-ps-probe/dualsense-edge'&&test.controlledFamily==='ROG'&&test.requestedPersona==='dualsense-edge','CANCELLATION_CASE_MISSING');
need(c?.kind===kind&&c.callbackExecuted&&!c.directRecreateReturned&&c.recreateCommandsAdded===0&&c.processesAdded===0&&!c.markedRebuilt&&!c.rearmRequested,'CANCELLED_QUERY_REBUILT_OR_REARMED');
need(child.status==='OBSERVED'&&child.failure===null&&child.nativeObservedProcessExit&&child.nativeObservedExitCode===0&&child.creates===1&&child.finalCleanupOk&&!child.hasController&&!child.hasContext&&child.pipeLoopReturned,'CANCELLED_QUERY_DID_NOT_RELEASE_INITIAL_TARGET');
need(test.frameInputChain?.length===9&&child.events.filter(v=>v==='frame').length===6&&!test.afterStop.quarantined&&!test.afterStop.prepared,'CANCELLATION_SENT_EXTRA_INPUT_OR_RETAINED_TARGET');
if(kind==='generation')need(c.probeCalls===2&&c.newerRequestPreserved&&c.newerRequestCompleted,'OLD_QUERY_CLEARED_NEWER_REQUEST');else need(c.probeCalls===1&&c.frameCommandsAdded===0&&c.normalFrameResult?.hostSubmission!=='frame-accepted','CANCELLED_FRAME_REACHED_HOST');
if(kind==='off')need(c.actualSettingsOffDuringQuery&&!c.effectiveTargetEnabled&&c.unboundCommandsAdded===0&&c.unboundFrameAfterOff?.sourceContextReason==='virtual-target-disabled'&&c.normalFrameResult?.sourceContextReason==='virtual-target-disabled','ACTUAL_OFF_WAS_BYPASSED');
need(test.nativeEvents.some(e=>e.event==='ps-wake-rearm-deferred'&&e.detail.reason==='admission-changed-during-probe'&&e.detail.probeComplete&&e.detail.retryDelayMs===0),'QUERY_CANCEL_EVIDENCE_MISSING');
if(kind!=='generation')need(test.nativeEvents.some(e=>e.event==='recreate-target-result'&&e.detail.phase==='not-attempted'&&e.detail.failKind===(kind==='off'?'virtual-target-disabled':kind==='exit'?'exit-fence':'power-not-admitted')),'DIRECT_CANCEL_EVIDENCE_MISSING');
}
for(let i=13;i<16;++i){if(options.cancellationRollback&&i!==15)continue;const test=r.cases[i],child=test.managedReceipt,q=test.psWakeCancellation,kind=['persona','reentry','refresh'][i-13];
need(q?.kind===kind&&q.callbackExecuted&&test.controlledFamily==='ROG'&&test.requestedPersona==='dualsense-edge','REQUEST_OWNERSHIP_GROUP_MISSING');
need(q.probeCalls===1&&q.processesAdded===0&&!q.rearmRequested&&!q.directRecreateReturned,'REQUEST_PROBE_CREATED_EXTRA_PROCESS_OR_REARM');
need(child.status==='OBSERVED'&&child.failure===null&&child.nativeObservedProcessExit&&child.nativeObservedExitCode===0&&child.finalCleanupOk&&!child.hasController&&!child.hasContext&&child.pipeLoopReturned&&!test.afterStop.quarantined&&!test.afterStop.prepared,'NEW_REQUEST_DID_NOT_CLOSE_OWNED_RESOURCES');
if(kind==='refresh'){
 need(q.requestIdAfter===q.requestIdBefore&&q.intentRevisionAfter!==q.publicationRevisionBefore&&q.requestedMatchesBound&&q.recreateCommandsAdded===1&&q.markedRebuilt&&child.creates===2&&child.events.filter(e=>e==='frame').length===7,'ROUTINE_REFRESH_CANCELLED_VALID_RECOVERY');
 need(test.nativeEvents.some(e=>e.event==='ps-wake-rearm-rebuild-once'&&e.detail.ok),'REFRESH_DID_NOT_REACH_ACTUAL_RECREATE');
}else{
 need(q.requestIdAfter!==q.requestIdBefore&&q.recreateCommandsAdded===0&&!q.markedRebuilt,'OLD_QUERY_USED_NEW_REQUEST');
 const deferred=test.nativeEvents.find(e=>e.event==='ps-wake-rearm-deferred'&&e.detail.reason==='admission-changed-during-probe');need(deferred?.detail.queryRequestId===q.requestIdBefore&&deferred.detail.currentRequestId===q.requestIdAfter&&deferred.detail.queryHostEpoch===deferred.detail.currentHostEpoch,'QUERY_REQUEST_OWNERSHIP_LOG_MISSING');
 const follow=test.psWakeNewIntentFollowup;need(follow?.kind===kind&&follow.phase==='verifying'&&follow.frame.hostSubmission==='frame-accepted'&&test.nativeEvents.some(e=>e.event==='virtual-start-protection'&&e.detail.action==='verification-pending'&&e.detail.requestId===q.requestIdAfter&&e.detail.phase==='verifying'&&e.detail.candidateOutcome==='success'&&e.detail.verificationPending===true&&e.detail.verificationRemainingMs===10000),'NEW_ADMITTED_INTENT_DID_NOT_CREATE_CANDIDATE');
 if(kind==='persona'){
  need(q.requestedPersonaAfter==='elite'&&q.boundPersonaAfter==='dualsense-edge'&&!q.requestedMatchesBound&&q.frameCommandsAdded===0&&q.unboundCommandsAdded===0&&q.normalFrameResult.sourceContextReason==='requested-persona-not-bound'&&q.unboundFrameAfterOff.sourceContextReason==='requested-persona-not-bound','DIFFERENT_REQUESTED_TYPE_REACHED_OLD_TARGET');
  const physical=follow.physicalSource;need(physical?.actualDiscoveryLoop&&physical.actualPhysicalAdmissionConsumer&&physical.physicalMask===1&&physical.selectedSourceMask===1&&physical.publishedSourceMask===1&&physical.completedBoundary&&physical.boundaryEpoch===physical.topologyEpoch&&JSON.stringify(physical.slotReads)==='[2,2,2,2]','ELITE_SOURCE_WAS_NOT_DISCOVERED_AND_ADMITTED_BY_ACTUAL_CONSUMER');
  const old=follow.oldManagedReceipt;need(old?.creates===1&&old.finalCleanupOk&&old.nativeObservedProcessExit&&old.nativeObservedExitCode===0&&old.events.filter(e=>e==='frame').length===6,'OLD_EDGE_RELEASE_BEFORE_NEW_ELITE_NOT_PROVEN');
  need(follow.newProcesses===1&&follow.boundPersona==='elite'&&follow.frames.length===3&&child.creates===1&&child.events.filter(e=>e==='frame').length===2,'NEW_ELITE_TARGET_OR_INPUT_NOT_VERIFIED');
  need(test.nativeEvents.some(e=>e.event==='input-host-xbox-startup'&&e.detail.phase==='xinput-ready'&&e.detail.slot===1&&e.detail.targetHid==='HID\\HIDMAESTRO_VID_045E_PID_0B00&IG_00\\OFFLINE_ELITE'),'ACTUAL_XBOX_STATE_PATH_ID_INDEX_READBACK_MISSING');
 }else need(q.requestedPersonaAfter==='dualsense-edge'&&q.requestedMatchesBound&&follow.newProcesses===0&&child.creates===1&&child.events.filter(e=>e==='frame').length===7&&follow.requestId===q.requestIdAfter,'SAME_PERSONA_REENTRY_DID_NOT_COMPLETE_OWN_NEW_REQUEST');
}
}
verifyTargetBinding(r.cases[16],options);verifyLateOwnership(r.cases[17],options);verifyLineageReceipt(r.cases[18]);verifyR3ProtectedPs(r.cases[19]);for(const row of r.cases.filter(c=>c.name.startsWith('PS-WAKE/protected-boundary/')))verifyR3BoundaryPs(row);verifyReader(r.cases.find(row=>row.name==='PS-WAKE/read-only-endpoint-enumeration'),options);
}
function verifyCancellationNegative(counter){
for(const kind of ['off','exit','power']){const row=counter.cases.find(c=>c.psWakeCancellation?.kind===kind);need(row&&row.psWakeCancellation.recreateCommandsAdded===2&&row.managedReceipt.creates===3&&counter.failures.some(f=>f.case===row.name&&f.assertion==='actual RECREATE primitive refuses changed admission at send boundary'),'CANCELLATION_NEGATIVE_DID_NOT_REPRODUCE:'+kind);}const off=counter.cases.find(c=>c.psWakeCancellation?.kind==='off');need(off.psWakeCancellation.normalFrameResult.hostSubmission==='frame-accepted'&&off.psWakeCancellation.unboundFrameAfterOff.hostSubmission==='frame-accepted','CANCELLATION_NEGATIVE_DID_NOT_REPRODUCE_OFF_INPUT');
const typeChanged=counter.cases.find(c=>c.psWakeCancellation?.kind==='persona'),sameType=counter.cases.find(c=>c.psWakeCancellation?.kind==='reentry');need(typeChanged?.psWakeCancellation.normalFrameResult.hostSubmission==='frame-accepted'&&typeChanged.psWakeCancellation.unboundFrameAfterOff.hostSubmission==='frame-accepted'&&typeChanged.psWakeCancellation.recreateCommandsAdded===2&&typeChanged.psWakeNewIntentFollowup.oldManagedReceipt.creates===3,'ROLLBACK_DID_NOT_REPRODUCE_DIFFERENT_TYPE_OUTPUT');need(sameType?.psWakeCancellation.requestIdBefore!==sameType?.psWakeCancellation.requestIdAfter&&sameType.psWakeCancellation.recreateCommandsAdded===1&&sameType.managedReceipt.creates===2,'ROLLBACK_DID_NOT_REPRODUCE_OLD_QUERY_SAME_TYPE_REENTRY');
}

export async function execute(root,key='positive'){
 const selectedProfile=selectCoordinatorProfile(REPO);
 if(selectedProfile==='current')return executeCurrentPreparedPipe(root,key,REPO);
 if(key==='positive'&&selectedProfile==='b54')return executeB54ActualPositive(path.resolve(root),REPO);
 root=path.resolve(root);need(!fs.existsSync(root),'FRESH_EVIDENCE');fs.mkdirSync(root,{recursive:true});
 let failureStage='prerequisite',actualProcess=null;try{
 const c=inputs(key);verify(c);
 const sourceHashes=sources(key).map(p=>({path:p,sha256:hash(path.join(REPO,p))})),artifactHashes=artifacts(key).map(p=>({path:p,sha256:hash(p)}));
 const run=path.join(root,'actual');fs.mkdirSync(run);failureStage='process';
 const proc=await runProcess(c.native.value.exe,[run,'full-pipe',c.managed.value.fixtureDll],{cwd:run,env:{...process.env,YEMAN_POWER_CONTROL_DIR:run},timeoutMs:60000});
 actualProcess=proc;fs.writeFileSync(path.join(root,'stdout.log'),proc.stdout);fs.writeFileSync(path.join(root,'stderr.log'),proc.stderr);const {stdout,stderr,...terminal}=proc;write(path.join(root,'process-ledger.json'),{steps:[{exe:c.native.value.exe,args:[run,'full-pipe',c.managed.value.fixtureDll],...terminal}],lifecycleVersion:LIFECYCLE_VERSION});failureStage='business';
 const expectedExitCode=c.spec.mutation?1:0;
 need(!proc.timedOut&&!proc.launchError&&!proc.signal&&!proc.outputTruncated&&Number.isInteger(proc.exitCode),c.spec.prefix+'_NATIVE_PROCESS_INCOMPLETE');
 if(proc.exitCode!==expectedExitCode){const actual=read(path.join(run,'fixture-result.json'));need(false,actual.status==='FAIL'&&actual.failures?.length?c.spec.prefix+'_ACTUAL_BUSINESS_FAILURE':c.spec.prefix+'_UNEXPECTED_NATIVE_EXIT');}
 const r=read(path.join(run,'fixture-result.json'));
 verifyFullResult(r,c.spec.mutation?'FAIL':'PASS',c.spec.prefix);const reviewedContract=verifyReviewedExecution(c,r,proc),cancellationOracle=key==='cancellation'?reviewedContract:null;verifyFailureRows(r,expectedFailures(key),c.spec.prefix);
 const options={ownershipRollback:key==='psOwnership',bindingRollback:key==='psBinding'||key==='cancellation',cancellationRollback:key==='cancellation'};
 verifyGroups(r,options);
 verify(c);
 need(sourceHashes.every(s=>hash(path.join(REPO,s.path))===s.sha256)&&artifactHashes.every(s=>hash(s.path)===s.sha256),'INPUT_CHANGED_DURING_EXECUTION');
 const actualCompilerInputs=[...c.native.value.compilerInputs,...c.native.value.compilerIncludes,...c.managed.value.evaluatedCompilerInputs];
 const actualLoadedAssemblies=r.cases.flatMap(t=>[t.managedReceipt,t.restartManagedReceipt,t.psWakeNewIntentFollowup?.oldManagedReceipt].filter(Boolean)).flatMap(child=>['startupBindings','finalBindings'].flatMap(stage=>child[stage]?.loadedAssemblies??[]));
 const result={schemaVersion:1,actualManagedSourceEquivalence:c.managed.mappedSources,actualOriginalManagedSourceArtifacts:c.managed.originalSourceArtifacts,reviewedContract,cancellationOracle,actualCompilerInputs,actualLoadedAssemblies,lifecycleVersion:LIFECYCLE_VERSION,loadedChildren:verifyR3LoadedChildren(r,c.managed.value),compilerInputsVerified:verifyCompilerLedgers(c.native.value,c.managed.value),id:c.spec.id,case:key,status:'PASS',passed:true,checks:r.checks,nativeResult:{status:r.status,originalExitCode:proc.exitCode,expectedSemanticFailure:!!c.spec.mutation,mutation:c.spec.mutation,failures:r.failures,report:path.join(run,'fixture-result.json')},
 cases:r.cases.map(t=>({name:t.name,requestedPersona:t.requestedPersona,controlledFamily:t.controlledFamily,lineageProof:t.lineageProof,psWakeCancellation:t.psWakeCancellation,psWakeNewIntentFollowup:t.psWakeNewIntentFollowup?{kind:t.psWakeNewIntentFollowup.kind,phase:t.psWakeNewIntentFollowup.phase,newProcesses:t.psWakeNewIntentFollowup.newProcesses,boundPersona:t.psWakeNewIntentFollowup.boundPersona,physicalSource:t.psWakeNewIntentFollowup.physicalSource,frame:t.psWakeNewIntentFollowup.frame,oldManagedExit:t.psWakeNewIntentFollowup.oldManagedReceipt?.nativeObservedExitCode,oldManagedCreates:t.psWakeNewIntentFollowup.oldManagedReceipt?.creates}:null,psWakeRecovery:t.psWakeRecovery,psWakeBindingGeneration:t.psWakeBindingGeneration,lateOwnershipRecovery:t.lateOwnershipRecovery,endpointReads:t.endpointReads,frameInputChain:t.frameInputChain,lockedPlayerRequests:t.lockedPlayerRequests,playerLockWaits:t.playerLockWaits,stopProtectionReceipts:t.stopProtectionReceipts,switchVerificationFinals:t.nativeEvents?.filter(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='final').map(e=>e.detail),switchVerificationInspections:t.nativeEvents?.filter(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='inspection').map(e=>e.detail),recreateOwnedPin:t.recreateOwnedPin,duplicateStartBursts:t.duplicateStartBursts,powerCycles:t.powerCycles,powerRearmQueuedBeforeOff:t.powerRearmQueuedBeforeOff,wakeQueuedBeforeOff:t.wakeQueuedBeforeOff,afterLateRearmOff:t.afterLateRearmOff,afterStart:t.afterStart,afterStop:t.afterStop,childExitConfirmed:t.managedReceipt?.nativeObservedProcessExit,childExitCode:t.managedReceipt?.nativeObservedExitCode,childFinalCleanupOk:t.managedReceipt?.finalCleanupOk,recoveryProbes:t.recoveryProbes,afterExplicitRestart:t.afterExplicitRestart,restartChildExitCode:t.restartManagedReceipt?.nativeObservedExitCode})),sourceHashes:[...sourceHashes,...walk(root).map(p=>({path:p,sha256:hash(p)}))],prerequisiteArtifacts:artifactHashes,boundary:{fullNativeStartStopExecuted:true,actualNativeRequestAndTransport:true,actualSwitchVerificationMaintenance:true,switchCandidateIsFinal:false,switchVerificationMs:10000,switchAdmissionMaxMs:30000,actualManagedLoopAndSession:true,realOwnedChildProcesses:true,nativeBuildsExecuted:1,independentCase:true,unexecutedPositivePrerequisite:false,deviceIo:false,realSdkLoaded:false,physicalReturnAccepted:false,normalProductMainExecuted:false,compilerExecutedInThisAdapter:false,wakeKeptTargetPolicyExecuted:true,physicalHideReadbackAccepted:false,actualInProcessTargetRecreateExecuted:true,physicalReplacementEndpointsVerified:false,psWakeActualRequestConsumerWorker:true,setupApiReadErrorCasesControlled:true,psSameNameBindingGenerationExecuted:true,psLateReadOnlyOwnershipExecuted:true,psPeerAndRetiredOwnershipRejected:key!=='psOwnership'}};
 write(path.join(root,'result.json'),result);return result;
 }catch(error){let raw=null;const file=path.join(root,'actual','fixture-result.json');if(fs.existsSync(file))raw=read(file);write(path.join(root,'result.json'),{schemaVersion:1,id:caseSpec(key).id,status:'FAIL',passed:false,lifecycleVersion:LIFECYCLE_VERSION,failureStage,error:error.message,nativeExitCode:actualProcess?.exitCode??null,checks:raw?.checks??null,cases:raw?.cases?.length??null,failures:raw?.failures??[],rawResult:fs.existsSync(file)?file:null,forbiddenIo:raw?.forbiddenIo??null,compilationFailureCountedAsNegative:false,normalProductMainExecuted:false,hardwareIo:false});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{
  const args=process.argv.slice(2),options=new Map();
  need(args.length===2||args.length===4,'EVIDENCE_REQUIRED');
  for(let i=0;i<args.length;i+=2){need(['--case','--evidence-dir'].includes(args[i])&&!options.has(args[i])&&args[i+1],'ARGUMENT_INVALID');options.set(args[i],args[i+1]);}
  need(options.has('--evidence-dir'),'EVIDENCE_REQUIRED');const key=options.get('--case')??'positive';caseSpec(key);
  const r=await execute(options.get('--evidence-dir'),key);console.log(JSON.stringify({status:r.status,id:r.id,case:r.case,checks:r.checks,cases:r.cases.length,originalExitCode:r.nativeResult.originalExitCode,namedFailures:r.nativeResult.failures.length}));
 }catch(e){console.error(e.stack??e.message);process.exitCode=1;}
}
