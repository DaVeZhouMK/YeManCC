import {simulationCorePath,simulationNodePath,selectedNativeTools} from '../release-host-environment.mjs';
// Import is read-only. Business execution requires explicit --mode run.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {pathToFileURL} from 'node:url';
import {getLegacyDeckyPersistentLifecycleAdapters} from './adapter.mjs';
import {getDeckyUpdateRecoveryAdapters} from '../decky-update-recovery/adapter.mjs';
import {lifecycleContract} from './contract.mjs';
import {ownedToolDeclaration,OWNED_INPUTS} from './owned-adapter.mjs';
import {ORIGINAL_CASES} from './owned-run.mjs';
import {resourceZero} from './typed-binding-gate.mjs';
import {validateTypedRuntimeParentReceipt} from './typed-runtime-parent.mjs';
import {validateTypedHealthCloseParentReceipt} from './typed-health-close-parent.mjs';
import {validateTypedAsyncRequestParentReceipt} from './typed-async-parent.mjs';
import {validateOwnedAsyncRequestParentReceipt} from './typed-owned-async-parent.mjs';
import {lockBundledNodeTools,verifyBundledToolScope} from '../bundled-node-tool-authority.mjs';
import {lifecycleContractFingerprint} from '../capability-lifecycle/index.mjs';
export const MODULE='tools/ymcc_sim/global/decky-persistent-lifecycle';
export const WRAPPER=MODULE+'/current-parent-global.mjs';
export const PARENT_ID='plugins.decky-persistent-lifecycle';
export const BUDGETS=Object.freeze({parent:480000,current:285000,baseline:240000,compile:60000,native:15000,crash_breaker:35000,binding_cancel:20000,javascript:60000,identity:45000});
export const CURRENT_IDS=Object.freeze([
 'native.exit-clears-owned-process-job-worker-event','native.explicit-off-and-fatal-off-veto-late-wakes','native.late-actual-recovery-callback-after-exit','native.committed-ready-executes-original-rebind-consumer','native.job-stop-failure-falls-back-to-owned-job-close','native.crash-budget-cannot-renew-on-recovery-or-unknown'
]);
export const TS_IDS=Object.freeze([
 'consumer.persistent-resource-classification','consumer.sleep-rejects-write-and-automatically-publishes-current-ready','negative.connected-without-ready-snapshot-is-not-health','negative.recorded-pre-sleep-ready-is-not-current-generation-health','consumer.exit-disposes-observers-and-delivered-late-recovery-callbacks','consumer.late-attach-resolution-cannot-recreate-stopped-host','consumer.transient-source-churn-recovers-through-original-two-retry-relay','consumer.transient-read-exception-does-not-self-recover','negative.relay-source-churn-exhausts-original-two-retry-budget','consumer.exception-exhausts-original-two-retry-budget','consumer.exception-late-retry-cannot-revive-paused-demand','consumer.exception-late-retry-cannot-revive-detached-peer','consumer.exception-late-retry-cannot-revive-disposed'
]);
export const TYPED_IDS=Object.freeze(ORIGINAL_CASES.map(row=>row.id));
export const EXACT_25=Object.freeze([...TYPED_IDS,...CURRENT_IDS,...TS_IDS]);
export const REQUIREMENTS=Object.freeze([
 ['destroyOnExit','acceptance.exit',[TYPED_IDS[0],CURRENT_IDS[0],TS_IDS[4],TS_IDS[5]]],
 ['destroyWhenStopStalls','acceptance.stalled-stop',[TYPED_IDS[1],TYPED_IDS[2],CURRENT_IDS[4],TS_IDS[4]]],
 ['preventResurrectionAfterExit','acceptance.late-after-exit',[TYPED_IDS[1],TYPED_IDS[2],CURRENT_IDS[2],TS_IDS[4],TS_IDS[5]]],
 ['explicitOffStaysOff','acceptance.explicit-and-fatal-off',[TYPED_IDS[3],CURRENT_IDS[1],TS_IDS[4]]],
 ['recoverAfterSleep','acceptance.sleep-ready-health',[TYPED_IDS[4],TYPED_IDS[5],CURRENT_IDS[3],TS_IDS[1],TS_IDS[2],TS_IDS[3]]],
 ['recoverAfterTransientFailure','acceptance.transient-failure',[TS_IDS[7],TS_IDS[6],TS_IDS[9],...TS_IDS.slice(10)]],
 ['respectRecoveryLimits','acceptance.recovery-limits',[CURRENT_IDS[5],TS_IDS[8],TS_IDS[9]]]
]);
export const JS_REGRESSIONS=Object.freeze(['decky_loader_official_selftest','decky_sidebar_safety_selftest','decky_sidebar_client_selftest','decky_sidebar_host_selftest','decky_sidebar_hang_recovery_selftest','decky_sidebar_stability_matrix_selftest']);
export const NATIVE_REGRESSIONS=Object.freeze([
 {name:'runtime',source:'tools/decky_sidebar_runtime_selftest.cpp',runMs:15000,legacy:true},
 {name:'recovery',source:'tools/decky_sidebar_recovery_selftest.cpp',runMs:15000,legacy:false},
 {name:'health',source:'tools/decky_sidebar_health_selftest.cpp',runMs:15000,legacy:false},
 {name:'health_close',source:'tools/decky_sidebar_health_close_selftest.cpp',runMs:15000,legacy:true,argument:'health-close-fixture'},
 {name:'crash_breaker',source:'tools/decky_sidebar_crash_breaker_selftest.cpp',runMs:35000,legacy:true,argument:'crash-fixture'},
 {name:'binding_cancel',source:'tools/decky_binding_cancel_selftest.cpp',runMs:20000,legacy:true,argument:'binding-fixtures'}
]);
const sha=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
const hash=file=>sha(fs.readFileSync(file));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const save=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');};
const key=file=>path.resolve(file).toLowerCase();
const within=(root,file)=>key(file).startsWith(key(root)+path.sep);
const pin=file=>({path:path.resolve(file),sha256:hash(file)});
const slash=file=>file.replaceAll('\\','/');
const digest=value=>typeof value==='string'&&/^[a-f0-9]{64}$/.test(value);
function relative(file){assert(typeof file==='string'&&!path.isAbsolute(file)&&!file.split(/[\\/]/).some(p=>!p||p==='..'),'RELATIVE_SOURCE_REQUIRED');return slash(file);}
function natural(value){return Number.isSafeInteger(value)&&value>=0;}
export function sourceMap(rows,{duplicates=false}={}){
 assert(Array.isArray(rows)&&rows.length>0,'SOURCE_ROWS_REQUIRED');const map=new Map();
 for(const row of rows){const file=relative(row.path);assert(digest(row.sha256),'SOURCE_DIGEST_REQUIRED');if(map.has(file))assert(duplicates&&map.get(file)===row.sha256,'DUPLICATE_OR_CONFLICTING_SOURCE:'+file);map.set(file,row.sha256);}return map;
}
export function naturalExit(process,expected){
 assert.equal(process.exitCode,expected,'UNEXPECTED_CHILD_EXIT');
 for(const field of ['error','signal','launchError','timedOut','outputTruncated'])assert(!process[field],'CHILD_PROCESS_'+field.toUpperCase());
 assert(natural(process.elapsedMs)&&process.elapsedMs<=process.timeoutMs,'CHILD_ELAPSED_EXCEEDS_BUDGET');
}
export function allocateBudget(startedAt,now,originalBudget){
 assert(natural(now)&&natural(startedAt)&&now>=startedAt&&natural(originalBudget)&&originalBudget>0,'INVALID_BUDGET');
 const remaining=BUDGETS.parent-(now-startedAt);assert(remaining>0,'PARENT_DEADLINE_EXHAUSTED');return {originalMs:originalBudget,timeoutMs:Math.min(originalBudget,remaining),parentRemainingMs:remaining,clipped:remaining<originalBudget};
}
export function validateCurrentEnvelope(value,process){
 naturalExit(process,2);assert(process.timeoutMs<=BUDGETS.current,'CURRENT_WRAPPER_BUDGET_EXPANDED');
 assert(!value.error,'UNEXPECTED_CURRENT_TOP_ERROR');assert(value.cases.every(c=>!c.error&&c.measurements?.acceptance!==false),'UNEXPECTED_CURRENT_CASE_ERROR');
 assert.equal(value.schemaVersion,1);assert.equal(value.id,'plugins.decky-current-parent-migration');assert.equal(value.status,'INCOMPLETE');assert.equal(value.passed,false);
 assert.equal(value.selectedCasesPassed,true);assert.equal(value.allSevenObservationsPassed,true);assert.equal(value.fullPersistentAcceptance,false);
 assert.equal(value.sourceUnchanged,true);assert.equal(value.toolsUnchanged,true);assert.deepEqual(value.sourceChanges,[]);assert.deepEqual(value.toolChanges,[]);
 assert.deepEqual(value.originalBudgets,{compileMs:60000,nativeMs:45000,typedWrapperMs:135000,fullBaselineMs:240000});assert.equal(value.maxArtifactInputs,3072);
 assert.deepEqual(value.cases.map(c=>c.id),EXACT_25,'EXACT_25_MEMBERSHIP_OR_ORDER_CHANGED');assert(value.cases.every(c=>c.passed===true),'CURRENT_CASE_FAILED');
 assert.deepEqual(value.summary,{cases:25,passed:25,failed:[],requirements:{PASS:7,FAIL:0,INCOMPLETE:0}});
 assert.equal(value.originalBaseline.status,'NOT_RUN','CURRENT_CHILD_MUST_NOT_CLAIM_BASELINE');
 assert.equal(value.requirements.length,7);for(let i=0;i<7;i++){
  const [requirement,,ids]=REQUIREMENTS[i],row=value.requirements[i];assert.equal(row.requirement,requirement);assert.equal(row.status,'PASS');assert.deepEqual(row.caseIds,ids);assert.deepEqual(row.failedOrMissingCaseIds,[]);assert.deepEqual(row.gaps,[]);
  assert.deepEqual(row.observations.map(x=>x.caseId),ids);for(const observation of row.observations){const c=value.cases.find(c=>c.id===observation.caseId);assert.deepEqual(observation.measurements,c.measurements);assert.deepEqual(observation.evidence,c.evidence);}
 }
 const phases=[['original-typed',135000],['current-native-compile',60000],['current-native',45000],['current-consumer',45000]];
 assert.deepEqual(value.executions.map(p=>p.id),phases.map(p=>p[0]));for(let i=0;i<phases.length;i++){naturalExit(value.executions[i],0);assert.equal(value.executions[i].timeoutMs,phases[i][1]);}
 assert.equal(value.physicalSleepPerformed,false);assert.equal(value.officialLoaderExecuted,false);assert.equal(value.installedApplicationsStarted,false);assert.equal(value.realSteamOrDeviceActions,0);
 return value;
}
export function validateBaselineReceipt(value,process,beforeSources,requiredSources){
 naturalExit(process,0);assert.equal(process.id,'original.plugins.decky-update-recovery');assert.equal(process.originalBudgetMs,240000);assert.equal(process.timeoutMs,240000,'REUSE_REQUIRES_FULL_ORIGINAL_BUDGET');
 assert.equal(value.fieldRegression,'decky-update-disconnect-20261020','BASELINE_PRODUCER_IDENTITY');assert(natural(value.nativeChecks)&&value.nativeChecks>0,'BASELINE_NATIVE_ASSERTIONS_REQUIRED');
 assert.equal(value.passed,true,'BASELINE_ORIGINAL_FAIL');assert.equal(value.inputsUnchanged,true);assert(!value.error,'BASELINE_ORIGINAL_ERROR');
 const rows=sourceMap(value.sourceInputs,{duplicates:true});for(const [file,digest]of rows)assert.equal(beforeSources.get(file),digest,'BASELINE_SOURCE_NOT_PREBOUND_OR_CHANGED:'+file);
 const proof=sourceMap(process.boundSourceInputs);for(const [file,digest]of proof)assert.equal(beforeSources.get(file),digest,'BASELINE_PROOF_SOURCE_CHANGED:'+file);for(const file of requiredSources)assert.equal(proof.get(file),beforeSources.get(file),'BASELINE_EXECUTION_SOURCE_PROOF_MISSING:'+file);
 assert.equal(value.realSteamActions,0);assert.equal(value.userSettingsWrites,0);assert.equal(value.hardwareWrites,0);return value;
}
export function aggregateSeven(value){
 const rows=new Map(value.cases.map(c=>[c.id,c])),m=id=>{const row=rows.get(id);assert.equal(row?.passed,true,'AGGREGATION_CASE_NOT_PASS:'+id);return row.measurements;};
 const strictParent=s=>natural(s.parentHandlesBefore)&&s.parentHandlesBefore===s.parentHandlesAfter&&natural(s.parentThreadsBefore)&&s.parentThreadsBefore===s.parentThreadsAfter;
 const exit=m(CURRENT_IDS[0]),fallback=m(CURRENT_IDS[4]),late=m(CURRENT_IDS[2]),off=m(CURRENT_IDS[1]),limits=m(CURRENT_IDS[5]),js=m(TS_IDS[4]),attach=m(TS_IDS[5]);
 for(const state of [exit.atExit,fallback.atExit,late.atExit,off.userOff.atExit,off.fatalOff.atExit,limits.atExit,limits.explicitOwnerAtExit])assert(resourceZero(state),'MEASURED_NATIVE_RESOURCES_NOT_ZERO');
 for(const measurement of [exit,fallback,late,off.userOff,off.fatalOff,limits])assert(strictParent(measurement),'STRICT_PARENT_RESOURCE_EQUALITY');
 for(const id of [TYPED_IDS[1],TYPED_IDS[2]]){const s=m(id);assert(resourceZero(s.atDeadline));for(const field of ['completedBeforeFixtureRelease','fixtureReleaseUnsentAtMeasurement','ownedLoaderExited','bindingChildExited','descendantExited','cefUnaffected','lateRefreshRejected','lateRecoveryRejected'])assert.equal(s[field],true,field);assert.equal(s.deadlineMs,3200);assert(natural(s.stopMs)&&s.stopMs<3200);assert.equal(s.parentHandlesBefore,s.parentHandlesAfter);}
 assert.equal(fallback.deniedTerminateJobObjectCalls,2);assert.equal(fallback.ownedJobHandleLeasesBefore,2);assert.equal(fallback.closedOwnedJobHandles,2);assert.equal(fallback.lastKillOnCloseHandleClosed,1);assert.equal(fallback.directDescendantTerminateProcessCalls,0);assert.equal(fallback.remainingOwnedJobHandleLeases,0);
 for(const field of ['fallbackExercised','fallbackOwnedOnly','ownedDescendantAliveBeforeLastJobClose','ownedLoaderExited','ownedDescendantExited','sentinelAliveAfterStop','sentinelInSeparateOwnedJob'])assert.equal(fallback[field],true,field);
 assert.equal(fallback.sentinelInRuntimeJob,false);assert.equal(fallback.deadlineMs,4500);assert(natural(fallback.elapsedMs)&&fallback.elapsedMs<4500);
 assert.equal(limits.attempts,5);assert.equal(limits.maxAttempts,5);assert.equal(limits.secondBudgetAttempts,5);assert.equal(limits.ownedInertLoaderLaunches,12);assert.equal(limits.ownedInertProcesses.length,12);
 for(const field of ['limitReached','verifiedNewSessionResetExecuted','explicitEnableResetExecuted','explicitOffHonored','everyOwnedChildExited','tenRealCrashExitCodesAre37'])assert.equal(limits[field],true,field);
 assert.equal(limits.additionalStarts,0);assert.equal(limits.startsAfterOff,0);
 for(const owner of ['verified-session','explicit-enable']){const own=limits.ownedInertProcesses.filter(p=>p.owner===owner);assert.deepEqual(own.map(p=>p.sequence),[1,2,3,4,5,6]);assert(own.every(p=>p.exited===true&&natural(p.pid)&&p.pid>0));assert(own.slice(0,5).every(p=>p.exitCode===37));}
 for(const j of [js,attach]){assert.equal(j.remainingOwnedResources.timers,0);assert.equal(j.remainingOwnedResources.subscriptions,0);assert.equal(j.restartsAfterExit,0);assert(j.lateCallbacksDelivered>0);}
 const resources={processes:0,handles:0,workers:0,timers:js.remainingOwnedResources.timers,subscriptions:js.remainingOwnedResources.subscriptions};
 const resourceDerivation='Actual native resourceZero and exact owned process waits; Runtime joined worker, strict parent thread/handle equality; actual TS timer and observer counters. Independent CEF sentinel excluded.';
 const merged={
  destroyOnExit:{...exit,remainingOwnedResources:resources,ownerRevoked:exit.ownerRevoked&&js.ownerRevoked,bounded:exit.bounded&&js.bounded,resourceDerivation},
  destroyWhenStopStalls:{...fallback,remainingOwnedResources:resources,resourceDerivation,currentTypedCaseIds:[TYPED_IDS[1],TYPED_IDS[2]],typedDeadlineMs:3200},
  preventResurrectionAfterExit:{...late,remainingOwnedResources:resources,lateCallbacksDelivered:late.lateCallbacksDelivered+js.lateCallbacksDelivered+attach.lateCallbacksDelivered,restartsAfterExit:late.additionalStarts+js.restartsAfterExit+attach.restartsAfterExit,resourceDerivation},
  explicitOffStaysOff:{desiredEnabled:!(off.explicitOffHonored&&off.fatalOffHonored&&off.userOff.ownerRevoked&&off.fatalOff.ownerRevoked),lateCallbacksDelivered:off.userOff.lateCallbacksDelivered+off.fatalOff.lateCallbacksDelivered+js.lateCallbacksDelivered,restartsAfterExit:off.userOff.additionalStarts+off.fatalOff.additionalStarts+js.restartsAfterExit,userOff:off.userOff,fatalOff:off.fatalOff},
  recoverAfterSleep:{...m(TS_IDS[1]),committedReady:m(CURRENT_IDS[3]),typedReady:m(TYPED_IDS[4]),retainedReady:m(TYPED_IDS[5])},
  recoverAfterTransientFailure:{...m(TS_IDS[7]),relayChurn:m(TS_IDS[6]),exceptionRetryBudget:m(TS_IDS[9])},
  respectRecoveryLimits:{...limits,relayChurnBudget:m(TS_IDS[8]),relayExceptionBudget:m(TS_IDS[9])}
 };
 for(const requirement of ['recoverAfterSleep','recoverAfterTransientFailure']){const s=merged[requirement];for(const field of ['desiredEnabled','recovered','healthObserved','currentGeneration'])assert.equal(s[field],true,'CURRENT_HEALTH_REQUIRED:'+field);assert(natural(s.attempts)&&s.attempts>0&&natural(s.maxAttempts)&&s.maxAttempts>=s.attempts,'RECOVERY_BUDGET_MEASUREMENT');assert.equal(s.unconfirmedWritesReplayed,0);}
 assert.equal(merged.recoverAfterSleep.sleepObserved,true);assert.equal(merged.recoverAfterSleep.readyGranted,true);
 for(const requirement of ['destroyOnExit','destroyWhenStopStalls','preventResurrectionAfterExit']){assert.equal(merged[requirement].bounded,true);assert.equal(merged[requirement].ownerRevoked,true);}
 assert.equal(merged.explicitOffStaysOff.desiredEnabled,false);assert.equal(merged.explicitOffStaysOff.restartsAfterExit,0);assert.equal(merged.preventResurrectionAfterExit.restartsAfterExit,0);
 const acceptance=REQUIREMENTS.map(([requirement,id,caseIds])=>({id,name:id,passed:caseIds.every(id=>rows.get(id)?.passed===true),componentCaseIds:caseIds,measurements:merged[requirement],aggregationOnly:true}));
 return {cases:acceptance,observations:REQUIREMENTS.map(([requirement,caseId],i)=>({requirement,caseId,passed:acceptance[i].passed,measurements:acceptance[i].measurements}))};
}
export function coverageMap(old,currentCases){
 assert.equal(old.cases.length,47,'EXACT_HISTORICAL_47_REQUIRED');assert.equal(old.passed,false);assert.equal(old.status,'FAIL');assert.equal(old.summary.failed,3);
 assert.deepEqual(old.cases.filter(c=>c.passed!==true).map(c=>c.id),['native-persistent-execution','native.stalled-binding-stop-requires-fixture-release','acceptance.stalled-stop']);
 const actual=new Map(currentCases.map(c=>[c.id,c]));
 return old.cases.map((row,i)=>{
  let ids=[row.id],relation='ORIGINAL_REGRESSION';
  if(row.id==='native.stalled-binding-stop-requires-fixture-release'){ids=[TYPED_IDS[1],TYPED_IDS[2]];relation='HISTORICAL_CALLBACK_FAIL_RETAINED_TYPED_COUNTERPARTS_SEPARATE';}
  else if(row.id==='build.native-persistent'){ids=['current.native.entry'];relation='LEGACY_FIXTURE_BUILD_REPLACED_BY_CURRENT_TYPED_ENTRY';}
  else if(row.id==='native-persistent-execution'){ids=['current.native.entry'];relation='HISTORICAL_PROCESS_FAIL_RETAINED_CURRENT_PROCESS_SEPARATE';}
  else if(CURRENT_IDS.includes(row.id))relation='CURRENT_TYPED_PRODUCER_SAME_BEHAVIOR_CASE_ID';
  else if(TS_IDS.includes(row.id))relation='UNCHANGED_ORIGINAL_TS_CASE_ID_AND_ASSERTIONS';
  else if(row.id.startsWith('acceptance.'))relation='FRESH_CURRENT_MEASUREMENT_AGGREGATION';
  return {oldIndex:i+1,oldCaseId:row.id,oldPassed:row.passed,oldVerdictPreserved:true,relation,currentCaseIds:ids,currentCases:ids.map(id=>({id,status:actual.has(id)?actual.get(id).passed===true?'PASS':'FAIL':'NOT_RUN'})),originalFailurePromoted:false};
 });
}
export function legacyMigration(repo){
 return ['runtime','health_close','binding_cancel'].map(name=>{
  const source=NATIVE_REGRESSIONS.find(r=>r.name===name).source,text=fs.readFileSync(path.join(repo,source),'utf8');
  return {legacySource:source,sha256:hash(path.join(repo,source)),legacyMacro:/^\s*#define\s+YMCC_DECKY_RUNTIME_LEGACY_FIXTURE\b/m.test(text),status:'INCOMPLETE_TYPED_INVARIANTS_NOT_FULLY_MIGRATED',originalRegressionWillExecuteUnchanged:true,originalCompileOrExecutionFailureNeverMasked:true,
   exactOriginalCheckLabels:[...text.matchAll(/(?:check|need)\([^\n]*?,\s*"([^"]+)"\s*\)/g)].map(m=>m[1]),
   currentExecutableCounterparts:name==='runtime'?TYPED_IDS:name==='health_close'?[CURRENT_IDS[1],TYPED_IDS[3]]:[TYPED_IDS[1],TYPED_IDS[2]],
   remaining:name==='runtime'?'Original no-launch preflight/environment and callback-generation handoff/token invariants are not all proved by the six typed cases. Legacy callbacks cannot be current Runtime acceptance.':name==='health_close'?'Current scoped fatal OFF is exercised; old complete fault-close/re-enable sequence still requires reviewed typed equivalent.':'Owned typed process cancellation does not prove every old AsyncRequest silent HTTP/WS callback assertion. Original legacy test stays independent regression.',
   executableCommand:[process.execPath,path.join(repo,MODULE,'current-parent-native.mjs'),'--evidence-dir','<fresh core DeckyParentMigration-20261023 evidence>','--workspace-dir','<owned core DeckyParentMigration-20261023 work>'],promotionAllowed:false};
 });
}
export function parentDeclaration(repo){
 assert.equal(key(process.execPath),key(path.resolve(simulationNodePath())),'FIXED_NODE_REQUIRED');
 const parent=getLegacyDeckyPersistentLifecycleAdapters(repo)[0],baseline=getDeckyUpdateRecoveryAdapters(repo)[0],tools=ownedToolDeclaration(repo);
 assert(!parent.blockedReason,parent.blockedReason);assert(!baseline.blockedReason,baseline.blockedReason);assert(!tools.blockedReason,tools.blockedReason);
 const inputs=new Set([WRAPPER,'decky-plugin/plugin.json','tools/decky_update_release_gate.ps1','tools/decky-loader-upgrade.ps1','decky-plugin/vendor/DECKY-API-LOCK.json','decky-plugin/vendor/decky-api-1.1.3/dist/index.js','decky-plugin/vendor/decky-api-1.1.3/dist/types.js','decky-plugin/vendor/decky-api-1.1.3/LICENSE','decky-plugin/vendor/decky-api-1.1.3/package.json','tools/decky_binding_steam_lease_selftest.cpp','tools/decky_binding_steam_lease_selftest.mjs',...['typed-owned-async-parent.mjs','typed-owned-async-native.mjs','typed-owned-async-request-regression.mjs','typed-owned-async-request-regression.cpp','typed-owned-async-request-regression.test.mjs','typed-owned-async-contract.json','typed-async-parent.mjs','typed-async-parent.test.mjs','typed-async-request-parent.mjs','typed-legacy-async-request-regression.mjs','typed-legacy-async-request-regression.cpp','typed-legacy-async-request-regression.test.mjs','typed-runtime-parent.mjs','typed-legacy-runtime-regression.mjs','typed-legacy-runtime-regression.cpp','typed-legacy-runtime-regression.test.mjs','typed-health-close-parent.mjs','typed-legacy-health-close-regression.mjs','typed-legacy-health-close-regression.cpp','typed-legacy-health-close-regression.test.mjs'].map(name=>MODULE+'/'+name),...parent.evidenceInputs,...baseline.evidenceInputs,baseline.path,...OWNED_INPUTS,MODULE+'/current-parent-native.mjs',MODULE+'/current-parent-native.cpp','tools/decky-loader-contract.mjs','PowerControl/decky/PluginLoader_noconsole.exe','PowerControl/decky/LICENSE.decky-loader','.gitattributes','scripts/bump-version.mjs',...JS_REGRESSIONS.map(n=>'tools/'+n+'.mjs'),...NATIVE_REGRESSIONS.map(n=>n.source)]);
 // The official consumer reads every publication workflow as source data.
 for(const name of fs.readdirSync(path.join(repo,'.github/workflows')))if(/\.ya?ml$/.test(name))inputs.add('.github/workflows/'+name);
 for(const directory of ['native','src/bridge','decky-plugin/src'])for(const name of fs.readdirSync(path.join(repo,directory)))if(/\.(h|cpp|js|tsx?)$/.test(name))inputs.add(directory+'/'+name);
 for(const directory of ['tools/ymcc_sim/global/decky-update-recovery','tools/ymcc_sim/global/decky-update-recovery/native-launch-noio'])for(const name of fs.readdirSync(path.join(repo,directory)))if(fs.statSync(path.join(repo,directory,name)).isFile())inputs.add(directory+'/'+name);
 // The baseline executes the original Trainer module, exit callback fixture and
 // Vue source bundles. Bind these data/config inputs BEFORE starting its child.
 for(const name of fs.readdirSync(path.join(repo,'PowerControl/Trainer')))if(/\.(?:ps1|psm1)$/.test(name))inputs.add('PowerControl/Trainer/'+name);
 for(const name of ['PowerControl/GameTitlePolicy.ps1','tools/steam_cssloader_exit_selftest.ps1','tools/steam_cssloader_exit_selftest.cpp','tools/steam_cssloader_exit_extract.mjs','tsconfig.json','tsconfig.node.json'])inputs.add(name);
 const resolveInput=base=>[base,...['.mjs','.js','.ts','.tsx','.vue','.json','.css','/index.mjs','/index.js','/index.ts'].map(ext=>base+ext)].find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());
 const queue=[...inputs];for(let i=0;i<queue.length;i++){
  const file=path.join(repo,queue[i]);if(!/\.(mjs|cjs|js|ts|tsx|vue|cpp|h)$/.test(file))continue;const text=fs.readFileSync(file,'utf8'),discovered=[];
  for(const m of text.matchAll(/(?:\bfrom\s*|\bimport\s*(?:\(\s*)?|\brequire\s*\(\s*|new URL\(\s*)['"]((?:\.|@\/)[^'"\r\n]+)['"]/g)){const specifier=m[1].split('?')[0],base=specifier.startsWith('@/')?path.resolve(repo,'src',specifier.slice(2)):path.resolve(path.dirname(file),specifier),target=resolveInput(base);if(target&&within(repo,target))discovered.push(slash(path.relative(repo,target)));}
  for(const m of text.matchAll(/^\s*#\s*include\s*"([^"\r\n]+)"/gm)){const found=[path.resolve(path.dirname(file),m[1]),path.join(repo,'native',m[1]),path.join(repo,'deps/json',m[1])].find(p=>fs.existsSync(p)&&fs.statSync(p).isFile());if(found&&within(repo,found))discovered.push(slash(path.relative(repo,found)));}
  for(const m of text.matchAll(/['"]((?:src\/bridge|decky-plugin\/src|PowerControl)\/[^'"\r\n]+\.(?:tsx?|js|json))['"]/g))if(fs.existsSync(path.join(repo,m[1])))discovered.push(m[1]);
  for(const found of discovered)if(!inputs.has(found)){inputs.add(found);queue.push(found);}
 }
 const authority=lockBundledNodeTools(repo);verifyBundledToolScope(authority,repo);
 const currentChildArtifactInputs=[...new Map([...tools.artifactInputs,...authority.tools.map(row=>row.path),process.execPath].map(file=>[key(file),path.resolve(file)])).values()];assert(currentChildArtifactInputs.length<=3072,'CURRENT_CHILD_TOOL_LIMIT');
 const artifactInputs=[...new Map([...tools.artifactInputs,...(baseline.artifactInputs??[]),...authority.tools.map(r=>r.path),process.execPath].map(file=>[key(file),path.resolve(file)])).values()];assert(artifactInputs.length<=3072,'TOOL_LIMIT');
 const resultFiles=['result.json','SOURCE-PINS.json','TOOL-PINS.json','REGRESSION-COVERAGE.json','LEGACY-TYPED-MIGRATION.json','RAW-EVIDENCE.json','HISTORY.json'].map(p=>'decky-persistent-lifecycle/'+p);
 return {id:PARENT_ID,kind:'node',path:WRAPPER,args:['--mode','run','--evidence-dir','{outDir}/decky-persistent-lifecycle'],timeoutMs:BUDGETS.parent,scope:'behavioral-mock',evidenceInputs:[...inputs].sort(),artifactInputs,resultFiles,lifecycleContract,requiredComponentsRecommendation:['plugins.decky-owned-runtime-lifecycle'],requiredComponentsAreNotMutated:true,baselineRequiredSources:[...new Set([baseline.path,...baseline.evidenceInputs])].sort(),currentChildArtifactInputs,authority};
}
export function currentChildToolPins(declaration,parentToolPins){
 assert(Array.isArray(declaration.currentChildArtifactInputs)&&declaration.currentChildArtifactInputs.length>0&&declaration.currentChildArtifactInputs.length<=3072,'CURRENT_CHILD_TOOL_SCOPE_REQUIRED');
 const result=new Map();for(const file of declaration.currentChildArtifactInputs){const k=key(file);assert(!result.has(k),'CURRENT_CHILD_TOOL_SCOPE_DUPLICATE');const row=parentToolPins.get(k);assert(row&&digest(row.sha256),'CURRENT_CHILD_TOOL_NOT_PREBOUND');result.set(k,row);}return result;
}
function verifyFile(row){assert(digest(row.sha256),'FILE_PIN_REQUIRED');assert.equal(hash(row.path),row.sha256,'EVIDENCE_FILE_CHANGED:'+row.path);return row;}
function exactTools(rows,map){assert.equal(rows.length,map.size,'TOOL_MEMBERSHIP_CHANGED');const seen=new Set();for(const row of rows){const k=key(row.path);assert(!seen.has(k),'DUPLICATE_TOOL');seen.add(k);assert.equal(map.get(k)?.sha256,row.sha256,'CHILD_TOOL_CHANGED');verifyFile(row);}}
function verifySourceRows(rows,map,{duplicates=false}={}){for(const [file,digest]of sourceMap(rows,{duplicates})){assert.equal(map.get(file),digest,'CHILD_SOURCE_NOT_PREBOUND:'+file);}}
function ownedFile(root,file){assert(within(root,file)&&key(fs.realpathSync(file))===key(file),'OWNED_EVIDENCE_PATH_REQUIRED:'+file);return file;}
function validateGraph(file,sourcePins,toolPins){
 const graph=read(file);assert.equal(graph.Version,'1.2');for(const p of [graph.Data.Source,...graph.Data.Includes]){const row=sourcePins.get(key(p))??toolPins.get(key(p));assert(row,'UNDECLARED_COMPILER_INPUT:'+p);assert.equal(hash(p),row.sha256,'COMPILER_INPUT_CHANGED:'+p);}return pin(file);
}
function freeze(repo,paths,destination){const rows=[];for(const p of paths){relative(p);const source=path.join(repo,p);assert.equal(key(fs.realpathSync(source)),key(source),'SOURCE_REPARSE');const bytes=fs.readFileSync(source),target=path.join(destination,p);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);assert.equal(hash(source),sha(bytes),'SOURCE_CHANGED_DURING_FREEZE');rows.push({path:p,sha256:sha(bytes),frozen:pin(target)});}return rows;}
export function verifyCurrentDisk(value,parentSources,toolPins,childRoot,work){
 verifySourceRows(value.sourceInputs,parentSources);exactTools(value.toolArtifacts,toolPins);
 for(const phase of value.executions){verifyFile(phase.program);verifyFile(phase.stdout);verifyFile(phase.stderr);ownedFile(childRoot,phase.stdout.path);ownedFile(childRoot,phase.stderr.path);}
 const original=value.originalProducer,current=value.currentProducer;
 for(const row of [original.raw,original.result,original.sourcePins,original.exe,current.raw,current.exe,current.cef]){verifyFile(row);ownedFile(work,row.path);}
 const typed=read(original.raw.path),typedRun=read(original.result.path);assert.equal(typed.schema,'ymcc.typed-binding-runtime-gate.v1');assert.equal(typed.passed,true);assert.deepEqual(typed.cases.map(c=>c.id),TYPED_IDS);assert.equal(typedRun.status,'pass');assert.equal(typedRun.runExit,0);assert.deepEqual(typedRun.changedOriginalInputs,[]);
 for(const c of typed.cases){const delivered=value.cases.find(r=>r.id===c.id);assert.deepEqual(delivered.measurements,Object.fromEntries(Object.entries(c).filter(([k])=>!['id','passed'].includes(k))));assert.deepEqual(delivered.evidence,original.raw);}
 const frozen=read(original.sourcePins.path);verifySourceRows(frozen.files,parentSources);const frozenMap=new Map();for(const row of frozen.files){const p=ownedFile(work,path.join(frozen.source,row.path));assert.equal(hash(p),row.sha256);frozenMap.set(key(p),{path:p,sha256:row.sha256});}
 const originals=path.dirname(original.raw.path);validateGraph(path.join(originals,'dependencies.json'),frozenMap,toolPins);const build=read(path.join(originals,'BUILD.json'));assert.equal(build.exit,0);assert.equal(build.error,null);assert.equal(toolPins.get(key(build.compiler))?.sha256,build.compilerSha256);
 const n=read(current.raw.path);assert.equal(n.schema,'ymcc.current-decky-parent-native.v1');assert.equal(n.passed,true);assert.equal(n.productCallbackMode,'typed-only; legacy macro absent');assert.equal(n.remainingTrackedJobHandles,0);assert.equal(n.resourceLedgerOverflow,false);assert.deepEqual(n.cases.map(c=>c.id),CURRENT_IDS);
 for(const c of n.cases){const delivered=value.cases.find(r=>r.id===c.id);assert.equal(c.passed,true);assert.deepEqual(delivered.measurements,c.measurements);assert.deepEqual(delivered.evidence,current.raw);}
 const nativeRoot=path.dirname(current.raw.path),graph=read(path.join(nativeRoot,'d.json')),actualSources=new Map();
 for(const p of [graph.Data.Source,...graph.Data.Includes])if(within(nativeRoot,p)){
  ownedFile(work,p);assert(within(path.join(nativeRoot,'s'),p),'FROZEN_NATIVE_INPUT_REQUIRED');const sourceRelative=slash(path.relative(path.join(nativeRoot,'s'),p));
  if(parentSources.has(sourceRelative))assert.equal(hash(p),parentSources.get(sourceRelative),'FROZEN_CURRENT_SOURCE_CHANGED');else assert(Object.values(value.nativeExtractions).some(x=>key(x.generated?.path??'')===key(p)&&x.generated.sha256===hash(p)),'UNBOUND_GENERATED_NATIVE_INPUT');actualSources.set(key(p),pin(p));
 }
 validateGraph(path.join(nativeRoot,'d.json'),actualSources,toolPins);
 for(const x of Object.values(value.nativeExtractions)){verifyFile(x.generated);assert.equal(parentSources.get(relative(x.path)),x.sha256,'EXTRACTION_SOURCE_CHANGED');}
 const consumer=read(path.join(childRoot,'consumer-result.json'));assert.equal(consumer.passed,true);assert.deepEqual(consumer.cases.map(c=>c.id),TS_IDS);assert.equal(consumer.productionHostExecuted,true);assert.equal(consumer.productionRelayExecuted,true);assert.equal(consumer.productionClientExecuted,true);
 for(const c of consumer.cases){assert.deepEqual(value.cases.find(r=>r.id===c.id).measurements,c.measurements);assert.equal(c.passed,true);}
 return [original.raw,original.result,original.sourcePins,current.raw,pin(path.join(childRoot,'consumer-result.json'))];
}
function history(core){
 const base=path.join(core,'Evidence/DeckyParentMigration-20261023/a1-2547/history'),files=[['result.json','2a7a3f284df641659b691f84600f31335d77947937d827253ac061013dea59a7'],['native-result.json','2850ad18914927f98744ea0c0a31ac90d8a11577e09b093ebbbdeaf7abd22bd1'],['consumer-result.json','ea76797d7142bde1e05dd0422113af3bde1bfec0e939983b37c2e6ea01ca8859']];
 const rows=files.map(([name,digest])=>{const row=pin(path.join(base,name));assert.equal(row.sha256,digest,'HISTORICAL_FAIL_IDENTITY_CHANGED');return row;});const value=read(rows[0].path);coverageMap(value,[]);return {rows,value};
}
export async function runParent({repoRoot=path.resolve(import.meta.dirname,'../../../..'),evidenceDir,baselineResult,baselineProcess,baselineProcessSha256}={}){
 const startedAt=Date.now(),repo=fs.realpathSync(repoRoot),core=path.resolve(simulationCorePath("")),out=path.resolve(evidenceDir??'');
 assert(evidenceDir&&within(path.join(core,'Evidence'),out),'CORE_EVIDENCE_REQUIRED');assert(!fs.existsSync(out),'FRESH_EVIDENCE_REQUIRED');fs.mkdirSync(out,{recursive:true});assert.equal(key(fs.realpathSync(out)),key(out),'EVIDENCE_REPARSE');
 const workBase=path.join(core,'Workspace/DeckyParentGlobal-20261023');fs.mkdirSync(workBase,{recursive:true});const work=path.join(workBase,crypto.randomUUID().slice(0,6));fs.mkdirSync(work);assert.equal(key(fs.realpathSync(work)),key(work),'WORKSPACE_REPARSE');
 // The sealed child admits only its existing dedicated core prefixes.
 const childEvidence=path.join(core,'Evidence/DeckyParentMigration-20261023','pg-'+crypto.randomUUID().slice(0,6)),childWork=path.join(core,'Workspace/DeckyParentMigration-20261023','pg-'+crypto.randomUUID().slice(0,6));fs.mkdirSync(childWork,{recursive:true});
 const report={schemaVersion:1,id:PARENT_ID,passed:false,status:'INCOMPLETE',softwareAcceptance:false,fullPersistentAcceptance:false,fullProductAcceptance:false,physicalSleepPerformed:false,officialLoaderExecuted:false,installedApplicationsStarted:false,hardwareWrites:0,startedAt:new Date(startedAt).toISOString(),work,childEvidence,childWork,cases:[],processes:[],rawEvidence:[],sourceInputs:[],sourceHashes:{},toolArtifacts:[],missingEvidence:[],lifecycleContract,lifecycleEvidence:{schemaVersion:1,contractFingerprint:lifecycleContractFingerprint(lifecycleContract),features:[{id:'plugins.decky',observations:[]}]},budgets:BUDGETS,oldFailuresRetained:true,overallDeadlineMs:startedAt+BUDGETS.parent};
 let declaration,rows=[],frozenMap=new Map(),sourcePins=new Map(),toolPins=new Map(),old;
 const checkpoint=()=>save(path.join(out,'result.json'),report);
 const add=(id,passed,extra={})=>{const row={id,name:id,passed,...extra};report.cases.push(row);checkpoint();return row;};
 function exec(id,program,args,budget,{cwd=work,env=process.env,boundSourceInputs}={}){
  const allocation=allocateBudget(startedAt,Date.now(),budget);assert(toolPins.has(key(program))||within(work,program),'UNDECLARED_EXECUTABLE');
  const begin=Date.now(),child=spawnSync(program,args,{cwd,env,windowsHide:true,timeout:allocation.timeoutMs,maxBuffer:16*1024*1024});const stdout=path.join(out,'processes',id+'.stdout.bin'),stderr=path.join(out,'processes',id+'.stderr.bin');fs.mkdirSync(path.dirname(stdout),{recursive:true});fs.writeFileSync(stdout,child.stdout??Buffer.alloc(0));fs.writeFileSync(stderr,child.stderr??Buffer.alloc(0));
  const proof={id,program:pin(program),arguments:args,cwd,originalBudgetMs:budget,timeoutMs:allocation.timeoutMs,parentRemainingBeforeMs:allocation.parentRemainingMs,budgetClipped:allocation.clipped,elapsedMs:Date.now()-begin,exitCode:child.status,signal:child.signal,error:child.error?.message??null,timedOut:child.error?.code==='ETIMEDOUT',outputTruncated:child.error?.code==='ENOBUFS',stdout:pin(stdout),stderr:pin(stderr),...(boundSourceInputs?{boundSourceInputs,toolArtifacts:report.toolArtifacts}:{})};
  save(path.join(out,'processes',id+'.json'),proof);report.processes.push(proof);checkpoint();return proof;
 }
 const guard=async(id,action)=>{try{return await action();}catch(error){add(id,false,{error:String(error.stack??error)});return null;}};
 try{
  declaration=parentDeclaration(repo);rows=freeze(repo,declaration.evidenceInputs,path.join(work,'src'));report.sourceInputs=rows.map(({path,sha256})=>({path,sha256}));report.sourceHashes=Object.fromEntries(report.sourceInputs.map(r=>[r.path,r.sha256]));sourcePins=sourceMap(report.sourceInputs);
  for(const row of rows)frozenMap.set(key(row.frozen.path),row.frozen);report.toolArtifacts=declaration.artifactInputs.map(pin);toolPins=new Map(report.toolArtifacts.map(r=>[key(r.path),r]));
  assert(sourcePins.has('decky-plugin/vendor/decky-loader.lock.json'),'VENDOR_LOCK_MUST_BE_BOUND_BEFORE_EXECUTION');save(path.join(out,'SOURCE-PINS.json'),rows);save(path.join(out,'TOOL-PINS.json'),report.toolArtifacts);save(path.join(out,'ADAPTER-HANDOFF.json'),declaration);
  old=history(core);save(path.join(out,'HISTORY.json'),{status:'HISTORICAL_FAIL_PRESERVED',files:old.rows,oldSummary:old.value.summary,oldNativeSourceHashes:old.value.sourceHashes});
  const current=await guard('current.native.entry',()=>{
   const process=exec('current.native.entry',processExec,[path.join(repo,MODULE,'current-parent-native.mjs'),'--evidence-dir',childEvidence,'--workspace-dir',childWork],BUDGETS.current,{cwd:repo});
   const rawPath=path.join(childEvidence,'result.json');assert(fs.existsSync(rawPath),'CURRENT_NATIVE_RESULT_MISSING');report.rawEvidence.push(pin(rawPath));const value=validateCurrentEnvelope(read(rawPath),process);report.rawEvidence.push(...verifyCurrentDisk(value,sourcePins,currentChildToolPins(declaration,toolPins),childEvidence,childWork));
   const aggregate=aggregateSeven(value);add('current.native.entry',true,{exitCode:2,scope:'Exact original 25/7 INCOMPLETE child, not accepted parent',raw:pin(rawPath)});report.cases.push(...value.cases);report.cases.push(...aggregate.cases);report.lifecycleEvidence.features[0].observations=aggregate.observations;return value;
  });
  await guard('original.plugins.decky-update-recovery',()=>{
   let value,process,rawPath;
   if(baselineResult){assert(baselineProcess&&digest(baselineProcessSha256),'BASELINE_REUSE_PROCESS_AND_SHA_REQUIRED');assert.equal(hash(baselineProcess),baselineProcessSha256,'BASELINE_PROCESS_PROOF_CHANGED');process=read(baselineProcess);rawPath=path.resolve(baselineResult);value=read(rawPath);assert.deepEqual(process.originalResult,pin(rawPath),'BASELINE_RESULT_NOT_BOUND_TO_PROCESS');for(const p of [process.program,process.stdout,process.stderr])verifyFile(p);exactTools(process.toolArtifacts,toolPins);assert.equal(key(process.program.path),key(processExec));assert.equal(key(process.arguments[0]),key(path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/run.mjs')));assert.equal(key(process.cwd),key(repo));}
   else{const baselineRoot=path.join(core,'Evidence/DB','b-'+crypto.randomUUID().slice(0,8));assert(!fs.existsSync(baselineRoot),'FRESH_BASELINE_ROOT_REQUIRED');fs.mkdirSync(baselineRoot,{recursive:true});assert.equal(key(fs.realpathSync(baselineRoot)),key(baselineRoot),'BASELINE_REPARSE');const baselineOut=path.join(baselineRoot,'decky-update-recovery');process=exec('original.plugins.decky-update-recovery',processExec,[path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/run.mjs'),'--evidence-dir',baselineOut],240000,{cwd:repo,boundSourceInputs:report.sourceInputs});rawPath=path.join(baselineOut,'result.json');assert(fs.existsSync(rawPath),'BASELINE_RESULT_MISSING');value=read(rawPath);process.originalResult=pin(rawPath);save(path.join(out,'processes',process.id+'.json'),process);}
   report.rawEvidence.push(pin(rawPath));validateBaselineReceipt(value,process,sourcePins,declaration.baselineRequiredSources);report.originalUpdateRecovery={raw:pin(rawPath),process,sourceVerified:true,reused:!!baselineResult};add('original.plugins.decky-update-recovery',true,{raw:pin(rawPath),reusedBoundReceipt:!!baselineResult});
  });
  await guard('official-byte-identity',()=>{const p=exec('official-byte-identity',processExec,[path.join(repo,'tools/decky-loader-contract.mjs'),'verify'],45000,{cwd:repo});naturalExit(p,0);add(p.id,true,{rawProcess:pin(path.join(out,'processes',p.id+'.json'))});});
  for(const name of JS_REGRESSIONS)await guard('original.'+name,()=>{
   const artifact=path.join(work,name),env={...process.env,YMCC_DECKY_ARTIFACT_ROOT:artifact};const p=exec('original.'+name,processExec,[path.join(repo,'tools',name+'.mjs'),artifact],60000,{cwd:repo,env});naturalExit(p,0);
   let proof;if(name==='decky_sidebar_host_selftest')proof=read(p.stdout.path);else{const file={decky_loader_official_selftest:'OFFICIAL-LOADER-CONSUMER.json',decky_sidebar_safety_selftest:'SAFETY-NOTIFICATION.json',decky_sidebar_client_selftest:'validation/MIRROR-CLIENT-SELFTEST.json',decky_sidebar_hang_recovery_selftest:'validation/MIRROR-HANG-RECOVERY.json',decky_sidebar_stability_matrix_selftest:'validation/DECKY-STABILITY-MATRIX.json'}[name];const actual=path.join(artifact,file);assert(fs.existsSync(actual),'ORIGINAL_JS_RESULT_MISSING:'+name);proof=read(actual);report.rawEvidence.push(pin(actual));}
   assert(proof.passed===true||proof.status==='passed'||(name==='decky_sidebar_client_selftest'&&proof.productionClientSource===true),'ORIGINAL_JS_NOT_PASS');const results=proof.results??(Array.isArray(proof.cases)?proof.cases:null);assert(Array.isArray(results)&&results.length>0,'ORIGINAL_JS_CASES_REQUIRED');assert(results.every(r=>r.passed!==false&&r.status!=='failed'),'ORIGINAL_JS_RAW_CASE_FAILED');add(p.id,true,{originalCases:results.length,process:pin(path.join(out,'processes',p.id+'.json'))});
  });
  const tool=selectedNativeTools().native;
  const nativeEnv=Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]));Object.assign(nativeEnv,{INCLUDE:tool.include.join(';'),LIB:tool.lib.join(';'),PATH:path.dirname(tool.compiler)+';'+path.dirname(tool.linker)+';'+process.env.SystemRoot+'/System32',CL:'',_CL_:'',LINK:'',_LINK_:'',TEMP:work,TMP:work});
  for(const native of NATIVE_REGRESSIONS)await guard('original.native-'+native.name,()=>{
   const dir=path.join(work,'n',native.name);fs.mkdirSync(dir,{recursive:true});const image=path.join(dir,'test.exe'),graph=path.join(dir,'deps.json'),source=path.join(work,'src',native.source);
   const args=['/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+path.join(work,'src/native'),'/I'+path.join(work,'src/deps/json'),source,'/sourceDependencies',graph,'/Fo'+image+'.obj','/Fe'+image,'/link','advapi32.lib','user32.lib','userenv.lib','winhttp.lib','ws2_32.lib','bcrypt.lib','shell32.lib'];
   const built=exec('build.original-native-'+native.name,tool.compiler,args,60000,{cwd:dir,env:nativeEnv});naturalExit(built,0);validateGraph(graph,frozenMap,toolPins);add('build.original-native-'+native.name,true,{source:native.source,legacyMacro:native.legacy,compilerGraph:pin(graph),exe:pin(image)});
   const p=exec('original.native-'+native.name,image,native.argument?[path.join(dir,native.argument)]:[],native.runMs,{cwd:dir,env:nativeEnv});naturalExit(p,0);add(p.id,true,{legacyRegressionOnly:native.legacy,currentTypedAcceptance:!native.legacy,exe:pin(image),process:pin(path.join(out,'processes',p.id+'.json'))});
  });
  const typedRuntime=await guard('current.typed-runtime-regression',()=>{
   const runId='pg-'+crypto.randomUUID().slice(0,8),runtimeEvidence=path.join(core,'Evidence/DeckyTypedLegacyRuntime-20261023',runId),runtimeWork=path.join(core,'Workspace/DeckyTypedLegacyRuntime-20261023',runId);
   fs.mkdirSync(runtimeWork,{recursive:true});
   const process=exec('current.typed-runtime-regression',processExec,[path.join(repo,MODULE,'typed-legacy-runtime-regression.mjs'),'--workspace-dir',runtimeWork,'--evidence-dir',runtimeEvidence],135000,{cwd:repo});
   naturalExit(process,0);
   const resultPath=path.join(runtimeEvidence,'result.json'),value=read(resultPath);
   const verified=validateTypedRuntimeParentReceipt(value,{repo,evidenceRoot:runtimeEvidence,workRoot:runtimeWork,sourcePins,toolPins});
   report.rawEvidence.push(pin(resultPath),value.raw,value.mapping,value.compilerGraph,...value.generated,...value.nativeArtifacts,...value.executions.flatMap(row=>[row.stdout,row.stderr]));
   report.currentTypedRuntime={...verified,result:pin(resultPath),process:pin(path.join(out,'processes/current.typed-runtime-regression.json'))};
   add('current.typed-runtime-regression',true,report.currentTypedRuntime);return verified;
  });
  const typedHealth=await guard('current.typed-health-close-regression',()=>{
   const runId='pg-'+crypto.randomUUID().slice(0,8),healthEvidence=path.join(core,'Evidence/DeckyTypedLegacyHealthClose-20261023',runId),healthWork=path.join(core,'Workspace/DeckyTypedLegacyHealthClose-20261023',runId);
   fs.mkdirSync(healthWork,{recursive:true});
   const process=exec('current.typed-health-close-regression',processExec,[path.join(repo,MODULE,'typed-legacy-health-close-regression.mjs'),'--workspace-dir',healthWork,'--evidence-dir',healthEvidence],135000,{cwd:repo});
   naturalExit(process,0);
   const resultPath=path.join(healthEvidence,'result.json'),value=read(resultPath);
   const verified=validateTypedHealthCloseParentReceipt(value,{repo,evidenceRoot:healthEvidence,workRoot:healthWork,sourcePins,toolPins});
   report.rawEvidence.push(pin(resultPath),value.raw,value.mapping,value.compilerGraph,...value.generated,...value.nativeArtifacts,...value.executions.flatMap(row=>[row.stdout,row.stderr]));
   report.currentTypedHealthClose={...verified,result:pin(resultPath),process:pin(path.join(out,'processes/current.typed-health-close-regression.json'))};
   add('current.typed-health-close-regression',true,report.currentTypedHealthClose);return verified;
  });
  await guard('current.steam-session-lease-regression',()=>{
   const evidence=path.join(work,'steam-lease');
   const process=exec('current.steam-session-lease-regression',processExec,[path.join(repo,'tools/decky_binding_steam_lease_selftest.mjs'),evidence],45000,{cwd:repo});
   naturalExit(process,0);const file=path.join(evidence,'result.json'),value=read(file);
   assert.equal(value.pass,true);assert.equal(value.nativeCases,14);assert.equal(value.officialLoaderExecuted,false);assert.equal(value.realSteamActions,0);assert.equal(value.installedWrites,0);
   verifySourceRows(value.sourceHashes,sourcePins);
   assert.deepEqual(value.negativeMutations.map(r=>[r.name,r.detected]),[['original-strict-pid',true],['blind-pid-bypass',true]]);
   const positiveFile=path.join(evidence,'positive/native.json'),positive=read(positiveFile);assert.equal(positive.pass,true);assert.equal(positive.cases.length,14);assert(positive.cases.every(c=>c.pass===true));assert.equal(positive.ownedChildrenAtExit.activeProcesses,0);assert.equal(positive.privateRegistryRemoved,true);
   report.rawEvidence.push(pin(file),pin(positiveFile));
   for(const row of value.negativeMutations){const negativeFile=path.join(evidence,'negative-'+row.name,'native.json'),negative=read(negativeFile);assert.equal(negative.pass,false);assert(negative.cases.some(c=>c.name===row.failure&&c.pass===false));report.rawEvidence.push(pin(negativeFile));}
   add('current.steam-session-lease-regression',true,{nativeCases:14,semanticNegatives:2,raw:pin(file),process:pin(path.join(out,'processes/current.steam-session-lease-regression.json'))});
  });
   const typedAsync=await guard('current.typed-async-cancellation-regression',()=>{
    // V1 remains an actual independently parsed diagnostic. Its same-process
    // WinHTTP library-zero failure is preserved, never relabelled as v2 PASS.
    const oldRunId='pg-'+crypto.randomUUID().slice(0,8),oldEvidence=path.join(core,'Evidence/DeckyTypedLegacyAsyncRequest-20261023',oldRunId),oldWork=path.join(core,'Workspace/DeckyTypedLegacyAsyncRequest-20261023',oldRunId);fs.mkdirSync(oldWork,{recursive:true});
    const oldProcess=exec('diagnostic.typed-async-same-process-v1',processExec,[path.join(repo,MODULE,'typed-legacy-async-request-regression.mjs'),'--workspace-dir',oldWork,'--evidence-dir',oldEvidence],135000,{cwd:repo});
    const oldPath=path.join(oldEvidence,'result.json'),oldValue=read(oldPath),oldVerified=validateTypedAsyncRequestParentReceipt(oldValue,{repo,evidenceRoot:oldEvidence,workRoot:oldWork,sourcePins,toolPins,receiptPin:pin(oldPath)});
    naturalExit(oldProcess,oldValue.passed?0:1);report.rawEvidence.push(pin(oldPath),oldValue.raw,oldValue.mapping,oldValue.compilerGraph,...oldValue.generated,...oldValue.nativeArtifacts,...oldValue.executions.flatMap(row=>[row.stdout,row.stderr]));
    report.currentTypedAsyncSameProcessV1={...oldVerified,result:pin(oldPath),scope:'same-process-library-natural-zero-v1',verdictPreserved:true,usedForOwnedSubtreeAcceptance:false,process:pin(path.join(out,'processes/diagnostic.typed-async-same-process-v1.json'))};checkpoint();
    assert.equal(oldVerified.summary.typedFailed,0,'V1_ORIGINAL_TYPED_CALLBACK_FAILURE');
    assert.equal(oldVerified.legacy.status,'PASS','V1_ORIGINAL_LEGACY_FAILURE');
    assert(oldVerified.missingPredicates.every(row=>row.rowId==='parent'&&['WinHTTP calibrationPassed','strictParentHandlesEqual'].includes(row.predicate)),'V1_UNRELATED_MISSING_PREDICATE');
    const runId='pg-'+crypto.randomUUID().slice(0,8),asyncEvidence=path.join(core,'Evidence/DeckyOwnedAsyncControl-20261023',runId),asyncWork=path.join(core,'Workspace/DeckyOwnedAsyncControl-20261023',runId);fs.mkdirSync(asyncWork,{recursive:true});
    const process=exec('current.typed-async-cancellation-regression',processExec,[path.join(repo,MODULE,'typed-owned-async-request-regression.mjs'),'--workspace-dir',asyncWork,'--evidence-dir',asyncEvidence],135000,{cwd:repo});
    const resultPath=path.join(asyncEvidence,'result.json');if(fs.existsSync(resultPath)){const failed=read(resultPath);report.rawEvidence.push(pin(resultPath));if(failed.raw)report.rawEvidence.push(failed.raw);}
    const value=read(resultPath),verified=validateOwnedAsyncRequestParentReceipt(value,{repo,evidenceRoot:asyncEvidence,workRoot:asyncWork,sourcePins,toolPins,receiptPin:pin(resultPath)});
    report.rawEvidence.push(value.mapping,value.compilerGraph,...value.generated,...value.nativeArtifacts,...value.controls.map(row=>row.raw),...value.executions.flatMap(row=>[row.stdout,row.stderr]));
    report.currentTypedAsync={...verified,result:pin(resultPath),process:pin(path.join(out,'processes/current.typed-async-cancellation-regression.json')),contract:pin(path.join(repo,MODULE,'typed-owned-async-contract.json'))};checkpoint();
    assert.equal(verified.allowAcceptance,true,'CURRENT_TYPED_OWNED_ASYNC_REQUEST_FAILED:'+JSON.stringify(verified.missingPredicates));
    naturalExit(process,0);add('current.typed-async-cancellation-regression',true,report.currentTypedAsync);return verified;
   });
  report.legacyTypedMigration=legacyMigration(repo);
  if(report.currentTypedAsync&&!typedAsync){const row=report.legacyTypedMigration.find(row=>row.legacySource==='tools/decky_binding_cancel_selftest.cpp');Object.assign(row,{status:report.currentTypedAsync.status,currentTypedEvidence:report.currentTypedAsync,remaining:'Strict current AsyncRequest acceptance missing: '+JSON.stringify(report.currentTypedAsync.missingPredicates)});}
  if(typedHealth){const row=report.legacyTypedMigration.find(row=>row.legacySource==='tools/decky_sidebar_health_close_selftest.cpp');Object.assign(row,{status:typedHealth.status,remaining:null,currentTypedEvidence:report.currentTypedHealthClose});}
  if(typedAsync){const row=report.legacyTypedMigration.find(row=>row.legacySource==='tools/decky_binding_cancel_selftest.cpp');Object.assign(row,{status:typedAsync.status,remaining:null,currentTypedEvidence:report.currentTypedAsync});}
  if(typedRuntime){const row=report.legacyTypedMigration.find(row=>row.legacySource==='tools/decky_sidebar_runtime_selftest.cpp');Object.assign(row,{status:typedRuntime.status,remaining:null,currentTypedEvidence:report.currentTypedRuntime});}
  save(path.join(out,'LEGACY-TYPED-MIGRATION.json'),report.legacyTypedMigration);
  report.missingEvidence.push(...report.legacyTypedMigration.filter(row=>row.remaining!==null).map(row=>({code:'LEGACY_COMPLETE_INVARIANTS_REQUIRE_TYPED_MIGRATION',source:row.legacySource,remaining:row.remaining,currentCounterparts:row.currentExecutableCounterparts})));
  report.selected25Accepted=current!==null;
 }catch(error){add('parent.preflight-or-execution',false,{error:String(error.stack??error)});}
 const currentHash=file=>{try{return hash(file);}catch{return null;}};
 report.sourceChanges=rows.filter(row=>currentHash(path.join(repo,row.path))!==row.sha256||currentHash(row.frozen.path)!==row.sha256).map(row=>row.path);
 report.toolChanges=report.toolArtifacts.filter(row=>currentHash(row.path)!==row.sha256).map(row=>row.path);report.sourceUnchanged=report.sourceChanges.length===0;report.toolsUnchanged=report.toolChanges.length===0;
 if(!report.sourceUnchanged||!report.toolsUnchanged)add('parent.identity-changed',false,{sources:report.sourceChanges,tools:report.toolChanges});
 report.rawEvidenceChanges=report.rawEvidence.filter(row=>currentHash(row.path)!==row.sha256);if(report.rawEvidenceChanges.length)add('parent.raw-evidence-changed',false,{changes:report.rawEvidenceChanges});
 if(Date.now()-startedAt>BUDGETS.parent)add('parent.deadline',false,{elapsedMs:Date.now()-startedAt});
 if(old)save(path.join(out,'REGRESSION-COVERAGE.json'),{historical47:47,current:coverageMap(old.value,report.cases),fullUpdaterBudget:240000,oldFailsNotPromoted:true});
 report.passed=report.cases.length>0&&report.cases.every(c=>c.passed===true)&&report.missingEvidence.length===0&&report.sourceUnchanged&&report.toolsUnchanged;
 report.status=report.cases.some(c=>c.passed!==true)?'FAIL':report.passed?'PASS':'INCOMPLETE';report.softwareAcceptance=report.passed;report.fullPersistentAcceptance=report.passed;
 report.fullProductAcceptance=false;report.finishedAt=new Date().toISOString();report.elapsedMs=Date.now()-startedAt;report.summary={cases:report.cases.length,passed:report.cases.filter(c=>c.passed).length,failed:report.cases.filter(c=>c.passed!==true).map(c=>c.id),missingEvidence:report.missingEvidence};
 save(path.join(out,'RAW-EVIDENCE.json'),{files:report.rawEvidence,processes:report.processes.map(p=>pin(path.join(out,'processes',p.id+'.json'))),childEvidence,childWork,rawVerdictsPreserved:true});checkpoint();return report;
}
const processExec=process.execPath;
if(process.argv[1]&&import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href){
 const args=process.argv.slice(2),get=name=>{const i=args.indexOf(name);return i<0?undefined:args[i+1];};const mode=get('--mode');
 assert(['plan','run'].includes(mode),'EXPLICIT_MODE_PLAN_OR_RUN_REQUIRED');
 if(mode==='plan'){
  const output=get('--evidence-dir');assert(output,'PLAN_EVIDENCE_DIR_REQUIRED');const repo=path.resolve(import.meta.dirname,'../../../..'),base=path.resolve(simulationCorePath("Evidence/DeckyParentGlobal-20261023"));assert(within(base,path.resolve(output)),'PLAN_CORE_SCOPE');assert(!fs.existsSync(output),'FRESH_PLAN_REQUIRED');const declaration=parentDeclaration(repo);save(path.join(output,'ADAPTER-HANDOFF.json'),declaration);save(path.join(output,'LEGACY-TYPED-MIGRATION.json'),legacyMigration(repo));save(path.join(output,'PLAN-SOURCE-PINS.json'),declaration.evidenceInputs.map(relative=>({path:relative,sha256:hash(path.join(repo,relative))})));save(path.join(output,'PLAN-TOOL-PINS.json'),declaration.artifactInputs.map(pin));console.log(JSON.stringify({status:'PLAN_ONLY_NO_BUSINESS_EXECUTION',output,sourceInputs:declaration.evidenceInputs.length,artifactInputs:declaration.artifactInputs.length,budgets:BUDGETS}));
 }else{
  const report=await runParent({evidenceDir:get('--evidence-dir'),baselineResult:get('--baseline-result'),baselineProcess:get('--baseline-process'),baselineProcessSha256:get('--baseline-process-sha256')});console.log(JSON.stringify({status:report.status,summary:report.summary,fullPersistentAcceptance:report.fullPersistentAcceptance}));process.exitCode=report.status==='PASS'?0:report.status==='INCOMPLETE'?2:1;
 }
}