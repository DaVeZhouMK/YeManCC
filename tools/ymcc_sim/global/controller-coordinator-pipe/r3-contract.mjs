import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {R3_LAYOUT,resolveR3ManifestPin,verifyManagedSourceRows,verifyManagedBuilderAdapters} from './r3-source-equivalence.mjs';
import {REVIEWED_SPEC,assertReviewedNative,verifyReviewedShape} from './r3-reviewed-contract.mjs';
export const LIFECYCLE_VERSION='VC-LIFECYCLE-20261020-R3';
export const R3_SPEC_FILE='tools/ymcc_sim/global/controller-coordinator-pipe/r3-scenarios.json';
export const R3_MODULE='tools/ymcc_sim/global/controller-coordinator-pipe';
export const R3_SPEC=REVIEWED_SPEC;
export const need=(ok,code)=>{if(!ok)throw Error(code);};
export const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8'));
const inside=(root,file)=>{const r=path.relative(path.resolve(root),path.resolve(file));return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
export function assertR3Native(value){
 need(value?.status==='BUILT'&&value.lifecycleVersion===LIFECYCLE_VERSION&&value.r3ScenarioContract?.sha256&&value.r3ScenarioContract.sha256===hash(fileURLToPath(new URL('./r3-scenarios.json',import.meta.url))),'R3_SOURCE_BOUND_NATIVE_REQUIRED_REBUILD_OLD35');
 assertReviewedNative(value);
 need(value.sources.find(r=>r.path==='native/main.cpp')?.sha256===R3_LAYOUT.productMainSha256&&inside(R3_LAYOUT.authorizedBuildRoot,value.root),'R3_EXACT_SOURCE_AND_BUILD_ROOT_REQUIRED');
 need(value.fixtureNormalMainExecuted===false&&value.deviceIo===false&&value.compileSucceeded===true,'R3_NATIVE_COMPILE_BOUNDARY');
 need(value.sources.some(r=>r.path==='native/virtual_start_protection.h')&&value.sources.some(r=>r.path==='native/virtual_switch_execution_return_runtime.h'),'R3_PRODUCTION_OWNERS_REQUIRED');
 if(value.mode==='lifecycle')need(value.sources.some(r=>r.path==='tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc')&&value.r3ScenarioContract.tupleProtection?.cases>0,'R3_TUPLE_CONSUMER_SOURCE_AND_PROOF_REQUIRED');
}
export function readR3Managed(native,repo,workspace){
 const pin=native.managedPrerequisite,manifest=resolveR3ManifestPin(pin),value=read(manifest),buildRoot=R3_LAYOUT.authorizedBuildRoot;
 need(value.status==='BUILT'&&value.semanticMutation==null&&value.realSdkLoaded===false&&value.hardwareIo===false&&value.normalHostMainExecuted===false,'R3_MANAGED_BOUNDARY');
 const b=value.inputBindings,mappedSources=verifyManagedSourceRows(value,repo),builderAdapters=verifyManagedBuilderAdapters(value,repo);
 need(b.sources.some(r=>r.relative==='InputHost/Program.cs')&&b.sources.some(r=>r.relative==='InputHost/PsEndpointOwnership.cs'),'R3_REAL_MANAGED_CONSUMERS_REQUIRED');
 const buildArtifacts=[manifest,...value.runtimeArtifacts.map(r=>r.path),...b.sources.map(r=>r.snapshot),...b.generatedInputs.map(r=>r.path),value.generatedIdentity.path];need(buildArtifacts.every(p=>inside(buildRoot,p)),'R3_MANAGED_ARTIFACT_SCOPE');
 const originalSourceArtifacts=b.sources.map(r=>({path:r.path,sha256:r.sha256,snapshot:r.snapshot,relative:r.relative}));const artifacts=[...buildArtifacts,...builderAdapters.map(r=>r.path)];return {pin,manifest,value,buildRoot,artifacts,mappedSources,builderAdapters,originalSourceArtifacts};
}
export function verifyCompilerLedgers(native,managed){
 const rows=[...(native.compilerInputs??[]),...(native.compilerIncludes??[]),...(managed?.evaluatedCompilerInputs??[])];need(native.compilerInputs?.length>0&&native.compilerIncludes?.length>20,'R3_NATIVE_ACTUAL_COMPILER_LEDGER_REQUIRED');
 for(const r of rows)need(hash(r.path)===r.sha256,'R3_COMPILER_INPUT_CHANGED:'+r.path);
 return {rows:rows.length,meaning:'Compiler inputs for source-bound precompiled prerequisites are rehashed recursively. This wrapper executes existing native/managed children, not the compiler.'};
}
export function verifyR3Shape(result,options={}){return verifyReviewedShape(result,options);}
export function verifyR3ProtectedPs(test){
 need(test?.name==='PS-WAKE/central-protection-same-power-real-managed-pipe','R3_SAME_POWER_PS_GROUP_REQUIRED');
 const t=test.psProtectionTimeline;need(Array.isArray(t)&&t.length===7,'R3_PS_TIMELINE_REQUIRED');const phases=['normal-execution-inspection-10000','owned-absence-inspection-9900','owned-absence-inspection-1','owned-absence-inspection-complete','recreated-awaiting-current-managed-report','unique-final-success','duplicate-requests-no-rebuild-or-charge'];
 need(t.every((r,i)=>r.phase===phases[i]&&r.powerGeneration===t[0].powerGeneration&&r.boundPowerGeneration===t[0].powerGeneration&&r.hostEpoch===t[0].hostEpoch&&r.processId===t[0].processId&&r.protection.requestId===t[0].protection.requestId&&r.inspectionDeadlineElapsedMs===10000&&r.inspectionStartedElapsedMs===0),'R3_PS_ORIGINAL_REQUEST_POWER_HOST_DEADLINE');
 for(let i=0;i<3;i++)need(t[i].recreateCommands===0&&t[i].fallbackAttempts===0&&t[i].episodeAttempts===0&&t[i].protection.locked&&t[i].protection.remainingMs===30000-t[i].protection.elapsedMs,'R3_NO_EARLY_RECREATE_OR_CHARGE');
 for(let i=3;i<7;i++)need(t[i].recreateCommands===1&&t[i].fallbackAttempts===1&&t[i].episodeAttempts===1,'R3_RECREATE_EXACTLY_ONE_CENTRAL_AND_SHARED_CHARGE');
 need(t[4].protection.locked&&t[4].protection.remainingMs===20000&&t[4].protection.verificationRemainingMs===0&&!t[5].protection.locked&&t[5].protection.phase==='success'&&!t[6].protection.locked,'R3_REBUILD_ACK_NOT_FINAL_BEFORE_CURRENT_REPORT');
 const finals=test.nativeEvents.filter(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='final'&&e.detail.requestId===test.onRequestId);need(finals.length===1&&finals[0].detail.lifecycleVersion===LIFECYCLE_VERSION&&finals[0].detail.fallbackAttempts===1&&finals[0].detail.elapsedMs===10000,'R3_ONE_PS_FINAL_WITH_ORIGINAL_CLOCK');
 need(test.firstCurrentAck?.hostSubmission==='frame-accepted'&&test.repairedCurrentAck?.hostSubmission==='frame-accepted','R3_ACTUAL_MANAGED_REPORT_REQUIRED');
}
export function verifyR3NormalReturn(result){
 need(result.status==='PASS'&&result.checks===R3_SPEC.normalReturnChecks&&JSON.stringify(result.cases.map(r=>r.name))===JSON.stringify(R3_SPEC.normalReturnCaseNames)&&result.forbiddenIo===0&&result.fullTranslationUnit&&result.normalProductMainExecuted===false,'R3_ALL43_NORMAL_RETURN_921_REQUIRED');
 need(result.failures.length===0,'R3_NORMAL_RETURN_BUSINESS_FAILURE');
}
export function verifyR3LoadedChildren(result,managed){
 const children=[];for(const t of result.cases){for(const [kind,c]of [['managedReceipt',t.managedReceipt],['restartManagedReceipt',t.restartManagedReceipt],['oldManagedReceipt',t.psWakeNewIntentFollowup?.oldManagedReceipt]])if(c)children.push({case:t.name,kind,value:c});}
 for(const row of children){const c=row.value;need(c.nativeObservedProcessExit===true&&Number.isInteger(c.nativeObservedExitCode)&&c.finalCleanupAttempted===true&&typeof c.finalCleanupOk==='boolean','R3_CHILD_EXIT_OR_FINAL_CLEANUP_MISSING');
  need(c.buildReceiptSha256===hash(managed.manifestPath)&&c.inputManifestSha256===managed.inputManifestSha256,'R3_CHILD_BUILD_BINDING');
  for(const stage of ['startupBindings','finalBindings']){need(c[stage]&&Array.isArray(c[stage].loadedAssemblies),'R3_LOADED_ASSEMBLY_LEDGER_REQUIRED');let own=0;for(const r of c[stage].loadedAssemblies){need(hash(r.path)===r.sha256,'R3_LOADED_ASSEMBLY_CHANGED');if(r.ownAssembly){need(path.resolve(r.path)===path.resolve(managed.fixtureDll)&&r.sha256===hash(managed.fixtureDll),'R3_WRONG_OWN_MANAGED_ASSEMBLY');++own;}}need(own===1,'R3_OWN_LOADED_ASSEMBLY_REQUIRED');}}
 return children.map(r=>({case:r.case,kind:r.kind,pid:r.value.processId,parentPid:r.value.parentPid,exitCode:r.value.nativeObservedExitCode,loadedIdentityVerified:true}));
}
export function verifyR3BoundaryPs(test){
 const prefix='PS-WAKE/protected-boundary/',kind=test?.name?.startsWith(prefix)?test.name.slice(prefix.length):'';
 need(['natural-return','expired-queued-task','new-same-persona-ticket','unprotected-task-new-ticket'].includes(kind),'R3_EXISTING_PS_BOUNDARY_REQUIRED');
 const start=test.psProtectionTimeline?.[0];need(start?.phase==='normal-execution-inspection-10000'&&start.powerGeneration===start.boundPowerGeneration&&start.fallbackAttempts===0&&start.episodeAttempts===0&&start.recreateCommands===0&&start.protection.requestId===test.onRequestId,'R3_BOUNDARY_ORIGINAL_REQUEST_REQUIRED');
 const finals=test.nativeEvents.filter(e=>e.event==='input-host-switch-verification'&&e.detail.stage==='final').map(e=>e.detail);
 need(finals.length>0&&new Set(finals.map(f=>f.requestId)).size===finals.length&&finals.every(f=>f.lifecycleVersion===LIFECYCLE_VERSION&&f.fallbackAttempts===0),'R3_BOUNDARY_UNIQUE_FINAL_NO_BORROWED_CHARGE');
 const original=finals.find(f=>f.requestId===test.onRequestId);need(original&&(['natural-return','unprotected-task-new-ticket'].includes(kind)?original.outcome==='success':original.outcome==='failure'),'R3_BOUNDARY_OLD_REQUEST_OUTCOME');
 need(!test.nativeEvents.some(e=>e.event==='ps-wake-rearm-rebuild-once'&&e.detail?.ok),'R3_BOUNDARY_OLD_WORK_CANNOT_RECREATE');
 need(test.managedReceipt?.nativeObservedProcessExit===true&&test.managedReceipt.finalCleanupOk===true,'R3_BOUNDARY_ACTUAL_CHILD_RETURN_REQUIRED');
}
export function verifyR3Tuple(result){
 const t=R3_SPEC.tupleProtection;need(t&&result.status==='PASS'&&result.forbiddenIo===0&&result.fullTranslationUnit&&result.normalProductMainExecuted===false&&result.realDeviceIo===false&&result.checks===t.checks&&JSON.stringify(result.cases.map(r=>r.name))===JSON.stringify(t.names),'R3_EXACT_TUPLE_CONSUMER_REQUIRED');
 need(result.failures.length===0&&result.assertionTrace?.length===result.checks&&result.assertionTrace.every(r=>r.passed)&&JSON.stringify(result.assertionTrace.map(({case:c,assertion})=>({case:c,assertion})))===JSON.stringify(t.assertionSequence),'R3_TUPLE_ASSERTIONS_MISSING_OR_CHANGED');
}
