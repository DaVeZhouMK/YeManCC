import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import{spawnSync}from'node:child_process';import{fileURLToPath,pathToFileURL}from'node:url';import{extract as originalExtract}from'./fixtures/legion_motion_consumer/extract_main.mjs';import{calibrationToolchainPreamble}from'./fixtures/hc_reviewed_calibration/toolchain.mjs';import{nativeCompilerInputClosure}from'./fixtures/input_coordinator_lifecycle/compiler_inputs.mjs';
// This one declaration has a legitimate default-initializer {}, which the
// inherited simple extractor mistakes for a second function body. Mask only
// those two ASCII braces in the PARSE VIEW; no compiled source is transformed.
function extract(text,symbol,kind='function'){
 // Actual [[maybe_unused]] prefix is removed in the PARSE VIEW only.
 // Restore it and prove the complete source body before returning.
 if(kind==='function'){
  if(!/^[A-Za-z_]\w*$/.test(symbol))throw Error('INVALID_EXTRACT_SYMBOL');
  const pattern=new RegExp('^(\\[\\[maybe_unused\\]\\][ \\t]+)(?=static[^\\n;{]*\\b'+symbol+'\\s*\\()','gm');
  const matches=[...text.matchAll(pattern)];
  if(matches.length>1)throw Error('NONUNIQUE_ACTUAL_FUNCTION_ATTRIBUTE:'+symbol);
  if(matches.length===1){
   const hit=matches[0],prefix=hit[1],view=text.slice(0,hit.index)+text.slice(hit.index+prefix.length);
   const result=originalExtract(view,symbol,kind),actual=prefix+result.code;
   if(text.split(actual).length!==2||view.length+prefix.length!==text.length)
    throw Error('ATTRIBUTE_PARSE_VIEW_CHANGED_ACTUAL_BODY:'+symbol);
   return {...result,code:actual,sha256:crypto.createHash('sha256').update(actual).digest('hex'),
    endUtf8ByteExclusive:result.endUtf8ByteExclusive+Buffer.byteLength(prefix),
    parseOnlyLeadingAttributeMask:true,actualAttribute:prefix,compiledSourceModified:false};
  }
 }
 if(kind==='function'&&symbol==='inputHostTargetReturnAfterPhysicalRestoreLocked'){
  const pattern=/static bool inputHostTargetReturnAfterPhysicalRestoreLocked\(\s*const HidHideVisibilitySnapshot&,bool,const std::vector<std::wstring>&,\s*const std::function<bool\(\)>& returnCurrent = \{\}\);/g;
  const rows=[...text.matchAll(pattern)];if(rows.length>1)throw Error('NONUNIQUE_RETURN_SCOPE_DEFAULT_DECLARATION');
  if(rows.length===1){const before=rows[0][0],masked=before.replace('{}','  '),view=text.slice(0,rows[0].index)+masked+text.slice(rows[0].index+before.length);
   const result=originalExtract(view,symbol,kind);if(text.split(result.code).length!==2||view.length!==text.length)throw Error('RETURN_SCOPE_PARSE_VIEW_CHANGED_ACTUAL_BODY');
   return {...result,parseOnlyDefaultInitializerMask:true,compiledSourceModified:false};
  }
 }
 return originalExtract(text,symbol,kind);
}
import {readVirtualControllerSource,replaceUniqueSourceText} from './virtual_controller_runtime_source.mjs';
import {relocatedToolchain} from './ymcc_sim/global/inputhost-protocol/adapter.mjs';
import {isolateCurrentFixtureSettingsMutex} from './fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs';
import {gpdPreHidePrefix,gpdPreHideParameterPath,readGpdPreHideSelection,inspectGpdPreHideCase,inspectGpdPreHideBatch} from './fixtures/input_coordinator_lifecycle/gpd_pre_hide_selection.mjs';
// CLI_BOUNDARY_REVIEW_FUNCTION_BEGIN
function inspectCliBoundarySelection(results){
 const expectedCases=['owned-readback-cli-boundary/host-journal','owned-readback-cli-boundary/power'];
 const report=results[0]?.result,rows=report?.cases??[],actualCases=rows.map(row=>row.name);
 const exact=results.length===1&&results[0].mode==='switch-recovery'&&results[0].exitCode===0&&report?.status==='PASS'&&report?.forbiddenIo===0&&
  JSON.stringify(expectedCases)===JSON.stringify(actualCases)&&new Set(actualCases).size===2&&rows.every((row,index)=>{
   const b=row.cliBoundary;
   return row.passed===true&&b?.callbackCount===1&&b.actualReconcile===true&&
    JSON.stringify(b.commands)===JSON.stringify(['--dev-list','--cloak-state'])&&b.writes===0&&b.physicalQueries===0&&b.fullQueries===0&&
    b.repairKeyBefore===b.repairKeyAfter&&b.repairAttemptsBefore===b.repairAttemptsAfter&&b.commandSequenceBefore===b.commandSequenceAfter&&
    (index===0?b.variant==='host-journal'&&b.newEpoch>b.oldEpoch&&b.newTransaction!==b.oldTransaction&&b.newPower===b.oldPower:
     b.variant==='power'&&b.newEpoch===b.oldEpoch&&b.newTransaction===b.oldTransaction&&b.newPower===b.oldPower+1);
  });
 return {selectedCase:'owned-readback-cli-boundary',expectedCases,actualCases,exact,sourceDeclaredParameterTable:'CliBoundaryCases: host-journal,power',scope:'actual reconcile capture/contract/publish/repair; exact observed commands and unchanged successor repair state; external OS leaves controlled'};
}
// CLI_BOUNDARY_REVIEW_FUNCTION_END
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');const rawArgs=process.argv.slice(2),planOnly=rawArgs.includes('--plan'),prepareOnly=rawArgs.includes('--prepare-only');if(rawArgs.filter(arg=>arg==='--plan').length>1)throw Error('DUPLICATE_PLAN_OPTION');if(rawArgs.filter(arg=>arg==='--prepare-only').length>1)throw Error('DUPLICATE_PREPARE_OPTION');const args=rawArgs.filter(arg=>arg!=='--plan'&&arg!=='--prepare-only');if(![2,4,6,8,10,12,14,16,18,20,22,24,26].includes(args.length)||args[0]!=="--out")throw Error("Use --out ABS_NEW_ROOT [--mutation ID] [--managed-dll SDK_FREE_DLL] [--suite all|off-lifecycle|persona-lifecycle|family-lifecycle|cold-archive|cold-msi-archive|late-hide-pending|late-hide-publication|late-hide-current|late-hide-inflight|late-hide-inflight-edges|late-hide-orphaned-inflight|recovery-cost|worker-cost|focus-recovery|switch-recovery|tuple-protection] [--product-before SAVED_ROOT] [--product-candidate ABS_ROOT (exact GPD expired-first-ON wake only)] [--family onex|aokzoe (family-lifecycle only)] [--native-optimization O2|Od (Od requires exact OneX family or input-application selection)]");
let selectedRuntimeRoot=null;let nativeWriterReturn=null;let selectedDotnet=null,inputRequests=null;let selectedCase=null;let nativeOptimization="O2",family=null,mutation=null,managedDll=null,suite=null,coldRecord=null,productBefore=null,productCandidate=null,reuseSuite=null,reuseBudgetMs=null;const seen=new Set();for(let i=2;i<args.length;i+=2){const key=args[i];if(!["--runtime-root","--dotnet","--input-requests","--case","--native-optimization","--family","--mutation","--managed-dll","--suite","--cold-record","--product-before","--product-candidate","--reuse-suite","--reuse-budget-ms","--native-writer-return"].includes(key)||seen.has(key)||!args[i+1])throw Error("Invalid or repeated option");seen.add(key);if(key==="--runtime-root"){if(!path.isAbsolute(args[i+1])||/[\0\r\n]/.test(args[i+1]))throw Error("ABSOLUTE_PRIVATE_RUNTIME_ROOT_REQUIRED");selectedRuntimeRoot=path.resolve(args[i+1]);}else if(key==="--native-writer-return"){if(args[i+1]!=="actual")throw Error("GPD_NATIVE_WRITER_RETURN_REQUIRES_ACTUAL");nativeWriterReturn=args[i+1];}else if(key==="--dotnet"){if(!path.isAbsolute(args[i+1])||path.basename(args[i+1]).toLowerCase()!=="dotnet.exe")throw Error("EXPLICIT_DOTNET_PATH_INVALID");selectedDotnet=path.resolve(args[i+1]);}else if(key==="--input-requests")inputRequests=path.resolve(args[i+1]);else if(key==="--case")selectedCase=args[i+1];else if(key==="--native-optimization"){if(!["O2","Od"].includes(args[i+1]))throw Error("INVALID_NATIVE_OPTIMIZATION");nativeOptimization=args[i+1];}else if(key==="--family")family=args[i+1];else if(key==="--mutation")mutation=args[i+1];else if(key==="--managed-dll")managedDll=path.resolve(args[i+1]);else if(key==="--cold-record")coldRecord=path.resolve(args[i+1]);else if(key==="--product-before")productBefore=path.resolve(args[i+1]);else if(key==="--product-candidate"){if(!path.isAbsolute(args[i+1]))throw Error("PRODUCT_CANDIDATE_REQUIRES_ABSOLUTE_ROOT");productCandidate=path.resolve(args[i+1]);}else if(key==="--reuse-suite")reuseSuite=args[i+1];else if(key==="--reuse-budget-ms")reuseBudgetMs=/^\d+$/.test(args[i+1])?Number(args[i+1]):NaN;else{if(!["input-application","lifecycle-directed","target-return","target-return-family","all","off-lifecycle","persona-lifecycle","family-lifecycle","cold-archive","cold-msi-archive","late-hide-pending","late-hide-publication","late-hide-current","late-hide-inflight","late-hide-inflight-edges","late-hide-orphaned-inflight","recovery-cost","worker-cost","focus-recovery","switch-recovery","tuple-protection"].includes(args[i+1]))throw Error("Unknown suite");suite=args[i+1];}}

const prepareRejectionSelected=selectedCase==='prepare-rejection-cold';
const prepareRejectionCaseName='PREPARE-REJECT/ROG/actual-cold-create-exception';
if(prepareRejectionSelected){
 const scopeOptions=['--suite','--case','--managed-dll','--dotnet'];
 const dllArg=seen.has('--managed-dll')?args[args.indexOf('--managed-dll',2)+1]:'';
 if(suite!=='target-return'||!managedDll||!selectedDotnet||!path.isAbsolute(dllArg)||!path.isAbsolute(args[1])||prepareOnly||[...seen].some(option=>!scopeOptions.includes(option)))
  throw Error('PREPARE_REJECTION_EXACT_MANAGED_SCOPE_REQUIRED');
 if(planOnly){
  console.log(JSON.stringify({status:'NOT_RUN',selectedCase,requestedModes:['target-return'],expectedCases:[prepareRejectionCaseName],expectedCount:1,requiresSdkFreeManagedFixture:true}));
  process.exit(0);
 }
}
// GPD_PRE_HIDE_SELECTION_BEGIN
const gpdPreHideSelected=selectedCase?.startsWith(gpdPreHidePrefix)===true;
if(selectedRuntimeRoot&&!gpdPreHideSelected)throw Error('RUNTIME_ROOT_REQUIRES_EXACT_GPD_PRE_HIDE_SELECTION');
const gpdPreHideRuntimeRoot=gpdPreHideSelected?(selectedRuntimeRoot??path.join(path.resolve(args[1]),'family-lifecycle')):null;
if(gpdPreHideSelected&&!planOnly&&fs.existsSync(gpdPreHideRuntimeRoot))throw Error('FRESH_GPD_PRE_HIDE_RUNTIME_ROOT_REQUIRED');
if(prepareOnly&&(!gpdPreHideSelected||planOnly))throw Error('PREPARE_ONLY_EXACT_GPD_PRE_HIDE_SCOPE_REQUIRED');
const gpdPreHideParameterSource=path.resolve(repo,gpdPreHideParameterPath);
function inspectGpdPreHideSelection(selection,results){
 const inspection=inspectGpdPreHideBatch(selection,results);
 const {notRunCases,...observed}=inspection;
 return {...selection,...observed,...(selection.selectedShapes?{notRunCases}:{}),scope:'Original GPD Start/source discovery/post-hide lifecycle with controlled native Host and existing HID I/O leaves; not real managed Host or hardware'};
}
if(gpdPreHideSelected&&(suite!=='family-lifecycle'||family||mutation||managedDll||selectedDotnet||inputRequests||coldRecord||productBefore||productCandidate||reuseSuite||reuseBudgetMs!==null||nativeWriterReturn||nativeOptimization!=='O2'||!path.isAbsolute(args[1])))throw Error('GPD_PRE_HIDE_EXACT_NATIVE_SCOPE_REQUIRED');
const gpdPreHideParameterBytes=gpdPreHideSelected?fs.readFileSync(gpdPreHideParameterSource):null;
const gpdPreHideSelection=gpdPreHideSelected?readGpdPreHideSelection(gpdPreHideParameterBytes.toString('utf8'),selectedCase):null;
const gpdPreHideBatch=gpdPreHideSelection?.selectedShapes?.map(shape=>gpdPreHidePrefix+shape);
if(gpdPreHideBatch&&prepareOnly)throw Error('GPD_PRE_HIDE_BATCH_REQUIRES_OWNED_RUN');
// GPD_PRE_HIDE_SELECTION_END

// This private candidate is an explicit input to one original native-only case.
// It never changes the meaning of the historical --product-before input.
const oneXParameterizedSelected=selectedCase==='onex-parameterized';
if(oneXParameterizedSelected&&(suite!=='family-lifecycle'||family!=='onex'||mutation||coldRecord||productBefore||productCandidate||reuseSuite||reuseBudgetMs!==null||inputRequests||!managedDll||!selectedDotnet||!path.isAbsolute(args[1])))throw Error('ONEX_PARAMETERS_EXACT_CURRENT_MANAGED_SCOPE_REQUIRED');
const terminalFallbackSelected=process.argv.some(arg=>/^runtime-after-final:(rog:steamdeck|gpd:elite|msi:steamdeck):terminal-exhausted-physical-return$/.test(arg));
const delayedSuccessorSelected=process.argv.some(arg=>/^runtime-after-final:rog:steamdeck:(delayed-same-period-successor|wall-gap-5s|wall-gap-10s)$/.test(arg));
const sourceReconfirmSelected=selectedCase==='source-reconfirm-collection'||selectedCase?.startsWith('source-reconfirm-collection:')===true,wakeKeptSourceSelected=selectedCase==='wake-kept-source'||selectedCase?.startsWith('wake-kept-source:')===true;
const sourceWakeSelected=sourceReconfirmSelected||wakeKeptSourceSelected;
const sdOffSourceLostSelected=selectedCase==='sd-off-source-lost'||selectedCase?.startsWith('sd-off-source-lost:')===true;
if(sdOffSourceLostSelected&&(suite!=='off-lifecycle'||family||mutation||inputRequests||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null||nativeOptimization!=='O2'||!path.isAbsolute(args[1])||!managedDll||!selectedDotnet))throw Error('SD_OFF_EXACT_MANAGED_DLL_AND_DOTNET_SCOPE_REQUIRED');
const sdOffFiles=Object.freeze(['native/main.cpp','native/virtual_target_return_runtime.h']);

const sourceWakeFiles=Object.freeze(['native/main.cpp','native/virtual_wake_execution_runtime.h']);
if(sourceWakeSelected&&(suite!==(sourceReconfirmSelected?'switch-recovery':'lifecycle-directed')||family||mutation||inputRequests||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null||nativeOptimization!=='O2'||!path.isAbsolute(args[1])||(sourceReconfirmSelected?(managedDll||selectedDotnet):(!planOnly&&(!managedDll||!selectedDotnet)))))throw Error('SOURCE_WAKE_REQUIRES_EXACT_EXPLICIT_SCOPE');
if(productCandidate&&!terminalFallbackSelected&&!delayedSuccessorSelected&&!sourceWakeSelected&&!sdOffSourceLostSelected&&(suite!=='switch-recovery'||selectedCase!=='gpd-first-on-expired-wake'||planOnly||family||mutation||managedDll||selectedDotnet||inputRequests||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null||nativeOptimization!=='O2'||!path.isAbsolute(args[1])))throw Error('PRODUCT_CANDIDATE_REQUIRES_EXACT_GPD_NATIVE_WAKE_SCOPE');
const wakeKeptSelected=selectedCase==='wake-kept-visibility';
if(wakeKeptSelected&&(suite!=='lifecycle-directed'||family||mutation||inputRequests||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null||(!planOnly&&(!managedDll||!selectedDotnet))))throw Error('WAKE_KEPT_REQUIRES_EXACT_REAL_PIPE_SCOPE');
const gpdLiveSdTransferSelected=selectedCase==='gpd-live-sd-transfer';
const oemAfterOffSelected=selectedCase==='oem-after-off';
if(oemAfterOffSelected&&(suite!=='persona-lifecycle'||family||mutation||inputRequests||coldRecord||productBefore||productCandidate||reuseSuite||reuseBudgetMs!==null||nativeOptimization!=='O2'||!managedDll||!selectedDotnet||!path.isAbsolute(args[1])))throw Error('OEM_AFTER_OFF_EXACT_MANAGED_SCOPE_REQUIRED');
const gpdPersonaMutexSelected=selectedCase==='gpd-persona-mutex-race';
const gpdExpiredFirstOnFullCycleSelected=selectedCase==='gpd-expired-first-on-full-cycle';
const deniedTouchpadInputSelected=prepareRejectionSelected||gpdExpiredFirstOnFullCycleSelected||selectedCase==='wake-kept-source:GPD:expired-then-current';
if(nativeWriterReturn&&!gpdExpiredFirstOnFullCycleSelected)throw Error('GPD_NATIVE_WRITER_RETURN_EXACT_ORIGINAL_CASE_REQUIRED');
if(gpdExpiredFirstOnFullCycleSelected&&(suite!=='lifecycle-directed'||family||mutation||inputRequests||coldRecord||productBefore||productCandidate||reuseSuite||reuseBudgetMs!==null||nativeOptimization!=='O2'||!path.isAbsolute(args[1])||(!planOnly&&(!managedDll||!selectedDotnet))))throw Error('GPD_EXPIRED_FIRST_ON_FULL_CYCLE_EXACT_MANAGED_SCOPE_REQUIRED');
const originalConsumerSource=path.resolve(repo,'tools/input_coordinator_lifecycle_selftest.mjs');
const originalFixtureCppSource=path.resolve(repo,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp');
const originalFixturePipeSource=path.resolve(repo,'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc');
const executingConsumer=(gpdPreHideSelected||gpdPersonaMutexSelected||gpdExpiredFirstOnFullCycleSelected)?(()=>{const file=fileURLToPath(import.meta.url),bytes=fs.readFileSync(file);return {path:file,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex'),originalConsumerSource,originalFixtureCppSource,originalFixturePipeSource,meaning:'Observed executing program; private immutable launch additionally binds exact path/import adaptations and the unmodified original source.'};})():null;
if(gpdPersonaMutexSelected&&(suite!=='switch-recovery'||family||mutation||managedDll||selectedDotnet||inputRequests||coldRecord||productBefore||productCandidate||reuseSuite||reuseBudgetMs!==null||nativeOptimization!=='O2'||!path.isAbsolute(args[1])))throw Error('GPD_PERSONA_MUTEX_EXACT_NATIVE_SCOPE_REQUIRED');
function readGpdPersonaMutexSelection(source){
 const table=/gpdPersonaMutexVariants\[\]\s*=\s*\{([^}]+)\}/.exec(source),variants=table?[...table[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]):[];
 if(variants.length!==2||new Set(variants).size!==2)throw Error('GPD_PERSONA_MUTEX_TWO_UNIQUE_CONTROLS_REQUIRED');
 return {selectedCase:'gpd-persona-mutex-race',expectedCases:variants.map(name=>'gpd-persona-mutex-race/'+name)};
}
if(planOnly&&!gpdPreHideSelected&&!oneXParameterizedSelected&&!sourceWakeSelected&&!sdOffSourceLostSelected&&!gpdLiveSdTransferSelected&&!wakeKeptSelected&&!gpdPersonaMutexSelected&&!gpdExpiredFirstOnFullCycleSelected)throw Error('PLAN_REQUIRES_EXACT_GPD_LIVE_SD_TRANSFER');
if(gpdLiveSdTransferSelected&&(suite!=='persona-lifecycle'||family||mutation||inputRequests||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null||nativeOptimization!=='O2'||!path.isAbsolute(args[1])||(!planOnly&&(!managedDll||!selectedDotnet))))throw Error('GPD_LIVE_SD_TRANSFER_REQUIRES_EXACT_PERSONA_SCOPE_AND_OWNED_MANAGED_DOTNET');
if(inputRequests!==null&&suite!=="input-application"||suite==="input-application"&&(!inputRequests||!managedDll||!selectedDotnet||mutation||family||selectedCase||productBefore||coldRecord||reuseSuite))throw Error("INPUT_APPLICATION_REQUIRES_EXACT_REQUESTS_MANAGED_AND_DOTNET");
if(selectedDotnet&&!managedDll)throw Error("EXPLICIT_DOTNET_REQUIRES_MANAGED_CONSUMER");
const selectedToolchain=selectedDotnet&&!(planOnly&&(gpdExpiredFirstOnFullCycleSelected||sourceWakeSelected||sdOffSourceLostSelected))?relocatedToolchain(repo,path.dirname(selectedDotnet)):null;
const runtimeFaultSelection=/^runtime-after-final:(msi|rog|gpd|onex|aya|lenovo):(steamdeck|dualsense-edge|elite)(?::(off-before-stop|off-after-stop|power-before-stop|power-after-stop|exit-before-stop|exit-after-stop|persistent-budget|persistent-off|persistent-start-failure|persistent-prepared-failure|persistent-repeat-before-observer|persistent-repeat-after-observer|delayed-same-period-successor|wall-gap-5s|wall-gap-10s|terminal-exhausted-physical-return))?$/.exec(selectedCase??'');
if(runtimeFaultSelection?.[3]?.startsWith('persistent-')&&(runtimeFaultSelection[1]!=='gpd'||runtimeFaultSelection[2]!=='elite'))throw Error('PERSISTENT_REQUIRES_EXISTING_GPD_ELITE_CONSUMER');
if(runtimeFaultSelection?.[3]&&!terminalFallbackSelected&&!((runtimeFaultSelection[1]==='rog'&&runtimeFaultSelection[2]==='steamdeck')||(runtimeFaultSelection[1]==='gpd'&&runtimeFaultSelection[2]==='elite')))throw Error('RUNTIME_CANCEL_REPRESENTATIVE_SCOPE_REQUIRED');
if(selectedCase?.startsWith('runtime-after-final:')&&(!runtimeFaultSelection||suite!=='family-lifecycle'||!managedDll||!selectedDotnet||family||mutation||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null))throw Error('RUNTIME_FAULT_EXACT_MANAGED_SELECTION_REQUIRED');
if(['classified-fault-cleanup-elite','classified-fault-cleanup-ps'].includes(selectedCase)&&(suite!=='family-lifecycle'||!managedDll||!selectedDotnet||family||mutation||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null))throw Error("CLASSIFIED_CLEANUP_EXACT_MANAGED_SCOPE_REQUIRED");
if(selectedCase!==null&&!prepareRejectionSelected&&!gpdPreHideSelected&&!oneXParameterizedSelected&&!sourceWakeSelected&&!sdOffSourceLostSelected&&!wakeKeptSelected&&!gpdLiveSdTransferSelected&&!oemAfterOffSelected&&!gpdExpiredFirstOnFullCycleSelected&&!runtimeFaultSelection&&!["ps-return-root","ps-return-proof","ps-nopin-proof","ps-return-all","classified-fault-cleanup-elite","classified-fault-cleanup-ps"].includes(selectedCase)&&(!["gpd-persona-mutex-race","gpd-first-on-expired-wake","owned-readback-cli-current","owned-readback-cli-boundary:host-journal","owned-readback-cli-boundary","partial-start-cleanup-owner","partial-start-cleanup-preflight","startup-exception-owner","serial-thread-start","serial-entry-diagnostic","serial-exit-owner","serial-power-action","serial-config-action"].includes(selectedCase)||suite!=="switch-recovery"||family||mutation||managedDll||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null))throw Error("INVALID_EXPLICIT_QUEUED_EXCEPTION_SELECTION");
if(family!==null&&(!["onex","aokzoe"].includes(family)||suite!=="family-lifecycle"||mutation||!managedDll||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null))throw Error("ONEX_REQUIRES_EXACT_FAMILY_LIFECYCLE_AND_CURRENT_MANAGED_DLL_NO_MUTATION_OR_REUSE");
// Functional OneX verification may disable optimization; retain the full original cases,
// source gates and actual managed integration. Defaults and product builds remain O2.
if(nativeOptimization!=="O2"&&!["onex","aokzoe"].includes(family)&&suite!=="input-application")throw Error("OD_REQUIRES_EXACT_ONEX_OR_INPUT_APPLICATION_SELECTION");
if(reuseSuite && (suite!=="switch-recovery" || reuseSuite!=="late-hide-pending" || mutation || managedDll || coldRecord || productBefore))throw Error("REUSE_REQUIRES_EXACT_SWITCH_AND_PENDING_NATIVE_PAIR");
if((reuseSuite && reuseBudgetMs===null) || (reuseBudgetMs!==null && (!reuseSuite || !Number.isSafeInteger(reuseBudgetMs) || reuseBudgetMs<1000 || reuseBudgetMs>45000)))throw Error("REUSE_REQUIRES_EXPLICIT_BOUNDED_1000_TO_45000_MS");
if(!planOnly&&!gpdPreHideSelected&&(!(suite==="target-return"&&selectedCase==="ps-nopin-proof"&&mutation==="rollback-ps-return-family")&&suite&&family!=="onex"&&!["switch-recovery","tuple-protection","late-hide-pending","late-hide-publication","late-hide-current","late-hide-inflight","late-hide-inflight-edges","late-hide-orphaned-inflight","recovery-cost","worker-cost"].includes(suite)&&(!managedDll||(mutation&&!((suite==="cold-msi-archive"&&mutation==="rollback-cold-observed-id")||(suite==="focus-recovery"&&mutation==="rollback-focus-neutral"))))))throw Error("All suites require an owned managed DLL and no directed mutation");
if(["switch-recovery","tuple-protection","late-hide-pending","late-hide-publication","late-hide-current","late-hide-inflight","late-hide-inflight-edges","late-hide-orphaned-inflight","recovery-cost","worker-cost"].includes(suite)&&(managedDll||mutation||coldRecord))throw Error("late-hide-pending is bounded native-only; no managed process or other mutation");
if(productBefore&&!["late-hide-pending","late-hide-publication","late-hide-inflight","late-hide-orphaned-inflight"].includes(suite))throw Error("--product-before is only for late-hide-pending");
if(mutation&&!["rollback-focus-neutral","rollback-cold-observed-id","bypass-admission","rollback-recovery-inheritance","rollback-cancellation-guards","rollback-request-ownership","rollback-ps-endpoint-ownership","rollback-ps-binding-revision","rollback-switch-return-rearm","rollback-switch-fault-owner","rollback-pipe-stage-budget","rollback-initial-start-budget","rollback-wake-return-continuation","rollback-wake-retirement-identity","rollback-wake-start-budget","rollback-queued-player-binding","rollback-stale-off-direction","rollback-off-return-debt","rollback-off-start-blocked","rollback-prepare-claim","rollback-unsent-prepare-claim","rollback-live-explicit-restore","rollback-ps-return-xinput","rollback-ps-return-pin","rollback-ps-return-physical","rollback-ps-return-family","rollback-exit-final-sweep","rollback-target-reservation","rollback-target-pin-store","rollback-target-settlement"].includes(mutation))throw Error("Unknown mutation");
if(["rollback-focus-neutral","rollback-cold-observed-id","rollback-cancellation-guards","rollback-request-ownership","rollback-ps-endpoint-ownership","rollback-ps-binding-revision","rollback-pipe-stage-budget","rollback-initial-start-budget","rollback-wake-return-continuation","rollback-wake-retirement-identity","rollback-wake-start-budget","rollback-queued-player-binding","rollback-stale-off-direction","rollback-off-return-debt","rollback-off-start-blocked","rollback-prepare-claim","rollback-unsent-prepare-claim","rollback-live-explicit-restore","rollback-ps-return-xinput","rollback-ps-return-pin","rollback-ps-return-physical","rollback-ps-return-family","rollback-exit-final-sweep","rollback-target-reservation","rollback-target-pin-store","rollback-target-settlement"].includes(mutation)&&!managedDll||managedDll&&mutation&&!["rollback-focus-neutral","rollback-cold-observed-id","rollback-cancellation-guards","rollback-request-ownership","rollback-ps-endpoint-ownership","rollback-ps-binding-revision","rollback-pipe-stage-budget","rollback-initial-start-budget","rollback-wake-return-continuation","rollback-wake-retirement-identity","rollback-wake-start-budget","rollback-queued-player-binding","rollback-stale-off-direction","rollback-off-return-debt","rollback-off-start-blocked","rollback-prepare-claim","rollback-unsent-prepare-claim","rollback-live-explicit-restore","rollback-ps-return-xinput","rollback-ps-return-pin","rollback-ps-return-physical","rollback-ps-return-family","rollback-exit-final-sweep","rollback-target-reservation","rollback-target-pin-store","rollback-target-settlement"].includes(mutation))throw Error("Cancellation negative requires managed DLL; other negatives remain native-only");
if(mutation==="rollback-cold-observed-id"&&suite!=="cold-msi-archive")throw Error("cold ID rollback requires its exact archive suite");
if(mutation==="rollback-focus-neutral"&&suite!=="focus-recovery")throw Error("focus neutral rollback requires --suite focus-recovery");
const expectedMutationAssertion=({"rollback-focus-neutral":"focus recovery all focused replacement wire reports are fully neutral","rollback-cold-observed-id":"MSI opaque OS ID permits durable two-duty cleanup before saved ON creation","bypass-admission":"actual durable request locks before consumer runs","rollback-recovery-inheritance":"single real return refresh restores requested operational target","rollback-cancellation-guards":"actual RECREATE primitive refuses changed admission at send boundary","rollback-request-ownership":"actual RECREATE primitive refuses changed admission at send boundary","rollback-ps-endpoint-ownership":"same-profile peer or retired endpoint cannot prove current owned PS readiness","rollback-ps-binding-revision":"new target generation invalidates a slow observation even when instance names are reused","rollback-switch-return-rearm":"verified late cleanup queues only the continuation of the already charged recovery","rollback-switch-fault-owner":"one current frame fault has exactly one serialized Stop owner","rollback-pipe-stage-budget":"every actual connect write and read receives only the original player's remaining deadline budget","rollback-initial-start-budget":"normal first Start binds every actual lifecycle pipe stage to the original player execution period","rollback-wake-return-continuation":"dead-Host wake consumes proven old return then creates exactly one new owned Host","rollback-wake-retirement-identity":"rejected dead-Host return retirement logs exact original wakeSerial requestId powerGeneration and hostEpoch","rollback-wake-start-budget":"PC dead-Host wake permits at most three actual no-Host Start attempts after proven return","rollback-queued-player-binding":"expired queued first Start cannot create a Host or target after original thirty-second budget","rollback-stale-off-direction":"expired old OFF cannot take generic Start through an also-expired newer ON","rollback-off-return-debt":"unproven MSI return debt cannot finalize OFF success after eleven seconds","rollback-off-start-blocked":"fully returned ROG OFF succeeds with StartBlocked retained and no automatic Start","rollback-prepare-claim":"lost PREPARE ACK must not restore physical visibility while actual target cleanup is still unproven","rollback-unsent-prepare-claim":"unissued PREPARE cannot create a cleanup claim or target ownership","rollback-live-explicit-restore":"explicit OFF cannot override live unknown target return and restore physical visibility","rollback-ps-return-xinput":"old PS owned HID present or unknown must veto XInput-only physical restoration and tuple retirement","rollback-ps-return-pin":"complete owned-PS absence allows the actual deferred restore and exact old tuple retirement","rollback-ps-return-physical":"failed physical restore retains the exact PS endpoint return identity for the next probe","rollback-ps-return-family":"unknown old PS root cannot be discharged by profile presence or incomplete family census","rollback-exit-final-sweep":"final app-exit sweep cannot bypass live Host or unproven target ownership","rollback-target-reservation":"actual PREPARE or RECREATE wire is preceded by persisted targetMayExist for exact current owner","rollback-target-pin-store":"accepted real PS State pin is persisted after current acceptance","rollback-target-settlement":"complete Stop consumes exactly the settled old current target-only origins"})[mutation];
if(!sdOffSourceLostSelected&&["off-lifecycle","cold-archive","cold-msi-archive"].includes(suite)&&(!coldRecord||!managedDll))throw Error("cold-archive requires --cold-record and --managed-dll");

function readSdOffSelection(text){
 const entry='static std::vector<std::string> fixtureSdOffSourceLostVariantNames(){',start=text.indexOf(entry);
 if(start<0||text.indexOf(entry,start+1)>=0)throw Error('SD_OFF_CANONICAL_FUNCTION_REQUIRED');
 const body=text.slice(start),table=/std::vector<std::string> variants=\{([^}]+)\};/.exec(body);
 if(!table)throw Error('SD_OFF_CANONICAL_TABLE_REQUIRED');
 const variants=[...table[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]);
 const cas=/for\(unsigned path=1;path<=4;\+\+path\)for\(const auto\* event:\{([^}]+)\}\)\s*variants.push_back\("CAS-"\+std::to_string\(path\)\+"-"\+event\);/.exec(body);
 const post=/for\(const auto\* event:\{([^}]+)\}\)variants.push_back\(std::string\("post-log-"\)\+event\);/.exec(body);
 const tokens=match=>match?[...match[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]):[];
 const edges=tokens(cas),postEdges=tokens(post);
 if(variants.length!==6||JSON.stringify(edges)!==JSON.stringify(['notification','generation','intent'])||JSON.stringify(edges)!==JSON.stringify(postEdges)||!body.includes('fullPipeSdOffSourceLostCase(selected);if(!failures.empty())return;'))throw Error('SD_OFF_CANONICAL_LOOPS_REQUIRED');
 for(let path=1;path<=4;++path)for(const edge of edges)variants.push('CAS-'+path+'-'+edge);
 variants.push(...postEdges.map(edge=>'post-log-'+edge));
 if(variants.length!==21||new Set(variants).size!==21||!variants.includes('absent'))throw Error('SD_OFF_21_UNIQUE_ORIGINAL_VARIANTS_REQUIRED');
 const only=selectedCase==='sd-off-source-lost'?'absent':selectedCase.slice('sd-off-source-lost:'.length);
 if(!variants.includes(only))throw Error('UNKNOWN_SD_OFF_VARIANT');
 return {suite,selectedCase,variants,selectedVariants:only===null?variants:[only],expectedCases:(only===null?variants:[only]).map(value=>'OFF-LIFECYCLE/SD-source-lost/'+value),requestedModes:['off-lifecycle'],nativeOnly:false,managed:true,defaultSelectionChanged:false};
}
function readSdOffOverlay(root){
 const actual=candidatePathIdentity(root,true),metadataPath=candidatePathIdentity(path.join(actual,'SOURCE-OVERLAY.json')),raw=fs.readFileSync(metadataPath),metadata=JSON.parse(raw.toString('utf8'));
 if(metadata.schemaVersion!==1||metadata.kind!=='reviewed-sd-off-source-lost-fixture-input'||!Array.isArray(metadata.files)||metadata.files.length!==2||new Set(metadata.files.map(row=>row?.path)).size!==2||metadata.files.some(row=>!sdOffFiles.includes(row?.path)))throw Error('SD_OFF_EXACT_TWO_FILE_OVERLAY_REQUIRED');
 const records=sdOffFiles.map(relative=>{const row=metadata.files.find(row=>row.path===relative),original=candidatePathIdentity(path.join(actual,relative)),inside=path.relative(actual,original),bytes=fs.readFileSync(original),hash=crypto.createHash('sha256').update(bytes).digest('hex');if(path.isAbsolute(inside)||inside==='..'||inside.startsWith('..'+path.sep)||!Number.isSafeInteger(row.bytes)||row.bytes<=0||bytes.length!==row.bytes||typeof row.sha256!=='string'||!/^[0-9a-f]{64}$/.test(row.sha256)||hash!==row.sha256)throw Error('SD_OFF_OVERLAY_PATH_OR_PIN:'+relative);return {path:relative,original,bytes:bytes.length,sha256:hash};});
 return {kind:'reviewed-sd-off-source-lost-fixture-input',root:actual,metadataPath,metadataBytes:raw.length,metadataSha256:crypto.createHash('sha256').update(raw).digest('hex'),metadata,records,compiledProductOverrides:[...sdOffFiles],selection:{suite,selectedCase,nativeOnly:false,managed:true,mutation:false},inputSelectionOnly:true,sourceApproval:false,productAcceptance:false};
}
const sdOffParameterSource=path.join(repo,'tools/fixtures/input_coordinator_lifecycle/off_lifecycle_cases.inc');
const sdOffSelection=sdOffSourceLostSelected?readSdOffSelection(fs.readFileSync(sdOffParameterSource,'utf8')):null;
const sdOffCandidateBinding=sdOffSourceLostSelected&&productCandidate?readSdOffOverlay(productCandidate):null;
if(sdOffSourceLostSelected){
 const dll=candidatePathIdentity(managedDll),dotnet=candidatePathIdentity(selectedDotnet),manifest=candidatePathIdentity(path.join(path.dirname(dll),'fixture-manifest.json')),m=JSON.parse(fs.readFileSync(manifest,'utf8'));
 if(m.realSdkLoaded!==false||m.hardwareIo!==false||m.status!=='BUILT'||path.resolve(m.fixtureDll)!==dll)throw Error('SD_OFF_REAL_SDK_FREE_HOST_REQUIRED');
 if(planOnly){console.log(JSON.stringify({...sdOffSelection,status:'PLANNED_ONLY_NOT_EXECUTED',parameterSource:sdOffParameterSource,parameterSourceSha256:crypto.createHash('sha256').update(fs.readFileSync(sdOffParameterSource)).digest('hex'),productCandidateBinding:sdOffCandidateBinding,managedDll:dll,selectedDotnet:dotnet,compilerInvocations:0,businessExecutions:0,evidenceWrites:0,originalCompileTimeoutMs:300000,originalModeTimeoutMs:45000},null,2));process.exit(0);}
}

function readSourceWakeSelection(source){
 const read=name=>{
  const tables=[...source.matchAll(new RegExp('\\b'+name+'\\s*\\[\\s*\\]\\s*=\\s*\\{([^}]+)\\}\\s*;','g'))];
  if(tables.length===0)throw Error('SOURCE_WAKE_PARAMETER_TABLE_NOT_READY:'+name);
  const body=tables.length===1?tables[0][1]:'',values=[...body.matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]);
  if(tables.length!==1||!values.length||new Set(values).size!==values.length||body.replace(/"[^"\r\n]+"/g,'').replace(/[\s,]/g,'')!=='')throw Error('SOURCE_WAKE_CANONICAL_TABLE_REQUIRED:'+name);
  return values;
 };
 const variants=read(sourceReconfirmSelected?'sourceReconfirmCollectionVariants':'wakeKeptSourceVariants');
 const families=sourceReconfirmSelected?[]:read('wakeKeptSourceFamilies');
 if(!sourceReconfirmSelected&&(families.length!==1||families[0]!=='ROG'))throw Error('WAKE_KEPT_SOURCE_ROG_FAMILY_REQUIRED');
 const baseCase=wakeKeptSourceSelected?'wake-kept-source':selectedCase;
 const expectedCases=sourceReconfirmSelected?variants.map(variant=>'source-reconfirm-collection/'+variant):families.flatMap(family=>variants.map(variant=>family+'/'+baseCase+'/'+variant));
 const familyControls=[];
  const directedWakeCases=[];
 if(wakeKeptSourceSelected){
  const tables=[...source.matchAll(/wakeKeptSourceFamilyControls\[\]\s*=\s*\{([\s\S]*?)\n\};/g)];
  if(tables.length!==1)throw Error('WAKE_SOURCE_FAMILY_CONTROL_TABLE_REQUIRED');
   const body=tables[0][1],entry=/\{\s*"(GPD|MSI|PC)"\s*,\s*"(current|pending-then-current|source-free|expired-then-current)"\s*,\s*(0|2|3)\s*(,\s*true\s*)?\}/g;
   const entries=[...body.matchAll(entry)];
   if(entries.length!==6||body.replace(entry,'').replace(/[\s,]/g,'')!=='')throw Error('WAKE_SOURCE_EXACT_FAMILY_CONTROLS_REQUIRED');
   for(const [,family,variant,index,directedOnly] of entries){
    const name=family+'/'+baseCase+'/'+variant;
    if(directedOnly){
     if(family!=='GPD'||variant!=='expired-then-current'||index!=='2')throw Error('WAKE_SOURCE_DIRECTED_CONTROL_BINDING');
     directedWakeCases.push(name);continue;
    }
    if((family==='GPD'?2:family==='PC'?3:0)!==Number(index)||(family==='PC'?variant!=='source-free':!variants.includes(variant)))throw Error('WAKE_SOURCE_FAMILY_CONTROL_BINDING');
    familyControls.push({family,variant,index:Number(index)});expectedCases.push(name);
   }
   if(directedWakeCases.length!==1||new Set([...expectedCases,...directedWakeCases]).size!==expectedCases.length+1)throw Error('WAKE_SOURCE_DUPLICATE_OR_MISSING_PARAMETER');
 }

  if(wakeKeptSourceSelected&&selectedCase!=='wake-kept-source'){const parts=selectedCase.split(':');if(parts.length!==3||!parts[1]||!parts[2])throw Error('WAKE_SOURCE_EXACT_VARIANT_REQUIRED');const name=parts[1]+'/'+baseCase+'/'+parts[2];if(!expectedCases.includes(name)&&!directedWakeCases.includes(name))throw Error('UNKNOWN_WAKE_SOURCE_VARIANT');expectedCases.splice(0,expectedCases.length,name);}
 if(sourceReconfirmSelected&&selectedCase!=='source-reconfirm-collection'){const parts=selectedCase.split(':');const known=[...variants,...read('sourceReconfirmAdoptionVariants')];if(parts.length!==2||!parts[1]||(!known.includes(parts[1])&&parts[1]!=='adoption-owner'))throw Error('UNKNOWN_SOURCE_RECONFIRM_EXPLICIT_VARIANT');const selected=parts[1]==='adoption-owner'?[...read('sourceReconfirmAdoptionVariants'),...variants.filter(value=>value==='exact-identity'||value==='msi-family-peer')]:[parts[1]];expectedCases.splice(0,expectedCases.length,...selected.map(value=>'source-reconfirm-collection/'+value));}
 return {suite,selectedCase,expectedCases,expectedCount:expectedCases.length,variants,families,...(wakeKeptSourceSelected?{familyControls}:{}),nativeOnly:sourceReconfirmSelected,managed:wakeKeptSourceSelected,requestedModes:[sourceReconfirmSelected?'switch-recovery':'full-pipe-core'],parameterTables:sourceReconfirmSelected?['sourceReconfirmCollectionVariants',...(selectedCase==='source-reconfirm-collection'?[]:['sourceReconfirmAdoptionVariants'])]:['wakeKeptSourceVariants','wakeKeptSourceFamilies','wakeKeptSourceFamilyControls'],defaultSelectionChanged:false};
}
const sourceWakeParameterSource=sourceWakeSelected?path.join(repo,'tools/fixtures/input_coordinator_lifecycle',sourceReconfirmSelected?'ps_wake_cases.inc':'pipe_cases.inc'):null;
const sourceWakeSelection=sourceWakeSelected?readSourceWakeSelection(fs.readFileSync(sourceWakeParameterSource,'utf8')):null;
const sourceWakeCandidateBinding=sourceWakeSelected&&productCandidate?readSourceWakeOverlay(productCandidate):null;
if(planOnly&&sourceWakeSelected){
 const parameterBytes=fs.readFileSync(sourceWakeParameterSource);
 if(JSON.stringify(readSourceWakeSelection(parameterBytes.toString('utf8')))!==JSON.stringify(sourceWakeSelection))throw Error('SOURCE_WAKE_PLAN_TABLE_DRIFT');
 if(sourceWakeCandidateBinding&&JSON.stringify(readSourceWakeOverlay(productCandidate))!==JSON.stringify(sourceWakeCandidateBinding))throw Error('SOURCE_WAKE_PLAN_OVERLAY_DRIFT');
 console.log(JSON.stringify({...sourceWakeSelection,status:'PLANNED_ONLY_NOT_EXECUTED',parameterSource:sourceWakeParameterSource,parameterSourceSha256:crypto.createHash('sha256').update(parameterBytes).digest('hex'),productCandidateBinding:sourceWakeCandidateBinding,compilerInvocations:0,businessExecutions:0,evidenceWrites:0,originalCompileTimeoutMs:300000,originalModeTimeoutMs:sourceReconfirmSelected?45000:90000},null,2));process.exit(0);
}
function readGpdLiveSdTransferSelection(text){
 const table=/static constexpr const char\* gpdLiveSdTransferPersonas\[\]=\{([^}]+)\};/.exec(text);
 const personas=table?[...table[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]):[];
 if(personas.length!==2||new Set(personas).size!==2||personas.some(persona=>!['dualsense-edge','elite'].includes(persona)))throw Error('GPD_LIVE_SD_CANONICAL_PARAMETERS_REQUIRED');
 return {suite:'persona-lifecycle',selectedCase:'gpd-live-sd-transfer',expectedCases:personas.map(persona=>'GPD/persona-transfer/steamdeck-to-'+persona),expectedCount:personas.length,
  source:'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc',parameterTable:'gpdLiveSdTransferPersonas',defaultSelectionChanged:false,
  boundary:'actual foreground/current cold admission and serialized native/SDK-free managed transfer; physical effects remain the existing controlled ends; no nativeUI hide-to-virtual handoff or real-device acceptance'};
}
function wakeKeptScope(source){
 const read=name=>{const m=new RegExp(name+'\\[\\]\\s*=\\s*\\{([^}]+)\\}').exec(source);if(!m)throw Error('WAKE_KEPT_PARAMETER_TABLE_MISSING:'+name);return [...m[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]);};
 const families=read('wakeKeptVisibilityFamilies'),variants=read('wakeKeptVisibilityVariants');
 if(families.length!==3||variants.length!==7||new Set(families).size!==3||new Set(variants).size!==7)throw Error('WAKE_KEPT_CANONICAL_TABLE_REQUIRED');
 return {selectedCase,expectedCases:families.flatMap(family=>variants.map(variant=>family+'/wake-kept-visibility/'+variant)),suite:'lifecycle-directed',requestedModes:['full-pipe-core'],defaultSelectionChanged:false};
}
function readGpdExpiredFirstOnFullCycleSelection(source){
 const tables=[...source.matchAll(/\bgpdExpiredFirstOnFullCycleTerminals\s*\[\s*\]\s*=\s*\{([^}]+)\}\s*;/g)];
 const body=tables.length===1?tables[0][1]:'',terminals=[...body.matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]);
 if(tables.length!==1||terminals.length!==2||new Set(terminals).size!==2||terminals[0]!=='off'||terminals[1]!=='exit'||body.replace(/"[^"\r\n]+"/g,'').replace(/[\s,]/g,'')!=='')throw Error('GPD_EXPIRED_FIRST_ON_FULL_CYCLE_CANONICAL_TERMINALS_REQUIRED');
 return {selectedCase:'gpd-expired-first-on-full-cycle',nativeWriterReturn:nativeWriterReturn==='actual',nativeResourceCombinationRequired:nativeWriterReturn==='actual',suite:'lifecycle-directed',expectedCases:terminals.map(terminal=>'GPD/first-on-expired-full-cycle/'+terminal),expectedCount:terminals.length,parameterTable:'gpdExpiredFirstOnFullCycleTerminals',requestedModes:['full-pipe-core'],defaultSelectionChanged:false};
}
const gpdExpiredFirstOnFullCycleSelection=gpdExpiredFirstOnFullCycleSelected?readGpdExpiredFirstOnFullCycleSelection(fs.readFileSync(originalFixturePipeSource,'utf8')):null;
if(planOnly&&gpdExpiredFirstOnFullCycleSelected){console.log(JSON.stringify({...gpdExpiredFirstOnFullCycleSelection,status:'PLANNED_ONLY_NOT_EXECUTED',originalConsumerSource,originalFixtureCppSource,originalFixturePipeSource,executingConsumer,compilerInvocations:0,businessExecutions:0,evidenceWrites:0,originalCompileTimeoutMs:300000,originalModeTimeoutMs:90000},null,2));process.exit(0);}
if(planOnly&&wakeKeptSelected){console.log(JSON.stringify({...wakeKeptScope(fs.readFileSync(originalFixturePipeSource,'utf8')),status:'PLANNED_ONLY_NOT_EXECUTED',compilerInvocations:0,businessExecutions:0,evidenceWrites:0},null,2));process.exit(0);}
if(planOnly&&gpdPersonaMutexSelected){
 const source=fs.readFileSync(originalFixtureCppSource,'utf8');
 console.log(JSON.stringify({...readGpdPersonaMutexSelection(source),status:'PLANNED_ONLY_NOT_EXECUTED',compilerInvocations:0,businessExecutions:0,evidenceWrites:0,originalCompileTimeoutMs:300000,originalModeTimeoutMs:45000},null,2));process.exit(0);
}
if(planOnly&&gpdPreHideSelected){
 const fixture=fs.readFileSync(originalFixtureCppSource,'utf8');
 if(!fixture.includes('case:gpd-pending-before-hide:')||!fixture.includes('pre_hide_cases.inc'))throw Error('GPD_PRE_HIDE_CPP_DISPATCH_REQUIRED');
 console.log(JSON.stringify({...gpdPreHideSelection,runtimeRoot:gpdPreHideRuntimeRoot,status:'PLANNED_ONLY_NOT_EXECUTED',parameterSource:gpdPreHideParameterSource,sourceSha256:crypto.createHash('sha256').update(gpdPreHideParameterBytes).digest('hex'),compilerInvocations:0,businessExecutions:0,evidenceWrites:0,originalCompileTimeoutMs:300000,originalModeTimeoutMs:90000},null,2));process.exit(0);
}
if(planOnly&&oneXParameterizedSelected){
  const source=path.join(repo,'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc'),bytes=fs.readFileSync(source),selection=oneXParameterizedScope(bytes.toString('utf8'));
  const fixture=fs.readFileSync(path.join(repo,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8');
  if(!fixture.includes('case:onex-parameterized'))throw Error('ONEX_PARAMETER_CPP_DISPATCH_REQUIRED');
  console.log(JSON.stringify({...selection,status:'PLANNED_ONLY_NOT_EXECUTED',parameterSource:source,sourceSha256:crypto.createHash('sha256').update(bytes).digest('hex'),requestedModes:['family-lifecycle'],compilerInvocations:0,businessExecutions:0,evidenceWrites:0,originalCompileTimeoutMs:300000,originalModeTimeoutMs:90000},null,2));process.exit(0);
}
if(planOnly){

 const source=path.join(repo,'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc'),bytes=fs.readFileSync(source);
 const fixture=fs.readFileSync(path.join(repo,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8');
 if(!fixture.includes('if(selectedCase==L"case:gpd-live-sd-transfer")personaLifecycle140::gpdLiveSdTransferCases();else personaLifecycleCases140();'))throw Error('GPD_LIVE_SD_CPP_DISPATCH_REQUIRED');
 console.log(JSON.stringify({...readGpdLiveSdTransferSelection(bytes.toString('utf8')),status:'PLANNED_ONLY_NOT_EXECUTED',requestedModes:['persona-lifecycle'],
  sourceSha256:crypto.createHash('sha256').update(bytes).digest('hex'),output:path.resolve(args[1]),compilerInvocations:0,businessExecutions:0,evidenceWrites:0,
  originalCompileTimeoutMs:300000,originalModeTimeoutMs:90000,knownHistoricalFullCompileMs:98643,currentCompileAndRuntimeMeasured:false,
  remainingOwnerBudgetRequired:true},null,2));
 process.exit(0);
}
let applicationRequestsBefore=null,motionLibraryBefore=null,applicationMatrixRows=null;
if(inputRequests){
 const bytes=fs.readFileSync(inputRequests),request=JSON.parse(bytes),ids=['global','xbox','steam','xbox-steam'];
 if(!Array.isArray(request.rows)||request.rows.some(row=>!row.settings?.input||!row.request))throw Error('EXACT_INPUT_APPLICATION_REQUEST_SCOPE_REQUIRED');
 if(request.schemaVersion===1){
  if(JSON.stringify(request.rows.map(row=>row.mode))!==JSON.stringify(ids))throw Error('EXACT_INPUT_APPLICATION_REQUEST_SCOPE_REQUIRED');
 }else if(request.schemaVersion===2){
  const pairs=ids.flatMap(from=>ids.map(mode=>({from,mode})));
  if(request.rows.length!==16||request.rows.some((row,index)=>row.from!==pairs[index].from||row.mode!==pairs[index].mode||row.boot!=='fixture-private-boot|'+row.from+'-to-'+row.mode||row.sourceBoot!=='fixture-private-boot'||row.settings.input.startupObservedBoot!==row.boot||row.settings.extensions?.bootModesStartupResult?.boot!==row.boot||row.settings.extensions?.bootModes?.current!==row.mode))throw Error('EXACT_SIXTEEN_CROSS_BOOT_REQUESTS_REQUIRED');
  applicationMatrixRows=request.rows.map((row,index)=>({index,from:row.from,mode:row.mode,boot:row.boot,runtimeMode:'input-application-'+row.from+'-to-'+row.mode}));
 }else throw Error('EXACT_INPUT_APPLICATION_REQUEST_SCOPE_REQUIRED');
 applicationRequestsBefore={path:inputRequests,sha256:crypto.createHash('sha256').update(bytes).digest('hex'),bytes:bytes.length,request};
}
// Optional active-gyro requests use the existing reviewed pure numeric library.
// This never admits a controller SDK or an arbitrary path supplied by a request.
if(applicationRequestsBefore){
 const request=applicationRequestsBefore.request,active=request.rows.some(row=>row.request.gyro===true);
 if(active){
  if(!request.rows.every(row=>row.request.gyro===true&&row.request.target===true))throw Error(applicationMatrixRows?'EXACT_MATRIX_ACTIVE_GYRO_REQUESTS_REQUIRED':'EXACT_FOUR_ACTIVE_GYRO_REQUESTS_REQUIRED');
  const library=path.join(repo,'deps/hc-runtime-upstream/HC-CANDIDATE-0.32.4.0-06c0b954-20260902/GamepadMotion.dll');
  const index=path.join(repo,'deps/handheldcompanion-runtime/source-files.sha256'),indexBytes=fs.readFileSync(index);
  const pin=/^([0-9a-f]{64})\s+GamepadMotion\.dll$/im.exec(indexBytes.toString('utf8'))?.[1].toLowerCase();
  const declared=request.motionLibrary;
  if(!pin||!declared||typeof declared.path!=='string'||!path.isAbsolute(declared.path)||path.resolve(declared.path)!==path.resolve(library)||declared.sha256!==pin||crypto.createHash('sha256').update(fs.readFileSync(library)).digest('hex')!==pin)throw Error('REVIEWED_NUMERIC_MOTION_LIBRARY_REQUIRED');
  motionLibraryBefore={path:library,sha256:pin,index,indexSha256:crypto.createHash('sha256').update(indexBytes).digest('hex')};
 }else if(request.motionLibrary!==undefined)throw Error('UNREQUESTED_MOTION_LIBRARY');
}
if(["ps-return-root","ps-return-proof","ps-nopin-proof","ps-return-all"].includes(selectedCase)&&(suite!=="target-return"||!managedDll||family||(mutation&&!(selectedCase==="ps-nopin-proof"&&mutation==="rollback-ps-return-family"))||coldRecord||productBefore||reuseSuite||reuseBudgetMs!==null))throw Error("INVALID_PS_RETURN_SELECTION");
// PRODUCT_CANDIDATE_BINDING_BEGIN
const productCandidateFiles=Object.freeze(['native/main.cpp','native/virtual_wake_execution_runtime.h','native/virtual_controller_start_runtime.h','native/msi_native_foreground_isolation_runtime.h']);
function candidatePathIdentity(file,directory=false){
 if(typeof file!=='string'||!path.isAbsolute(file))throw Error('PRODUCT_CANDIDATE_ABSOLUTE_IDENTITY_REQUIRED');
 const absolute=path.resolve(file),root=path.parse(absolute).root;let current=root;
 for(const part of path.relative(root,absolute).split(path.sep).filter(Boolean)){
  current=path.join(current,part);if(fs.lstatSync(current).isSymbolicLink())throw Error('PRODUCT_CANDIDATE_LINK:'+current);
 }
 const stat=fs.lstatSync(absolute),real=fs.realpathSync.native(absolute);
 if((directory?!stat.isDirectory():!stat.isFile())||path.resolve(real).toLowerCase()!==absolute.toLowerCase())throw Error('PRODUCT_CANDIDATE_PATH_IDENTITY:'+absolute);
 return real;
}
function readProductCandidate(root){
 const expected=path.resolve(repo,'../../..','Testing/YMCC/Evidence/VC-Win5-Intermittent-20261110-111926/field-213009-triage-20261113/wake-first-on-product-r1/candidate-r4');
 const actual=candidatePathIdentity(root,true),repository=candidatePathIdentity(repo,true);
 if(actual.toLowerCase()!==expected.toLowerCase())throw Error('PRODUCT_CANDIDATE_FINAL_R4_ROOT_REQUIRED');
 const metadataPath=candidatePathIdentity(path.join(actual,'../CANDIDATE-R4.json')),metadataBytes=fs.readFileSync(metadataPath),metadata=JSON.parse(metadataBytes.toString('utf8'));
 if(candidatePathIdentity(metadata.repo,true).toLowerCase()!==repository.toLowerCase()||candidatePathIdentity(metadata.candidateRoot,true).toLowerCase()!==actual.toLowerCase()||metadata.status!=='PRIVATE_CANDIDATE_PREPARED_NOT_TESTED_NOT_IMPORTED')throw Error('PRODUCT_CANDIDATE_METADATA_IDENTITY');
 if(!Array.isArray(metadata.files)||metadata.files.length!==productCandidateFiles.length||new Set(metadata.files.map(row=>row?.path)).size!==productCandidateFiles.length||metadata.files.some(row=>!productCandidateFiles.includes(row?.path)))throw Error('PRODUCT_CANDIDATE_EXACT_FOUR_ALLOWLIST_REQUIRED');
 const records=productCandidateFiles.map(relative=>{
  const row=metadata.files.find(row=>row.path===relative),declared=row.candidate;
  if(!Number.isSafeInteger(declared?.bytes)||declared.bytes<=0||!/^[0-9a-f]{64}$/.test(declared.sha256))throw Error('PRODUCT_CANDIDATE_INVALID_PIN:'+relative);
  const original=candidatePathIdentity(path.join(actual,relative)),inside=path.relative(actual,original);
  if(path.isAbsolute(inside)||inside==='..'||inside.startsWith('..'+path.sep))throw Error('PRODUCT_CANDIDATE_PATH_ESCAPE:'+relative);
  const bytes=fs.readFileSync(original);
  if(bytes.length!==declared.bytes||crypto.createHash('sha256').update(bytes).digest('hex')!==declared.sha256)throw Error('PRODUCT_CANDIDATE_BYTES_OR_SHA:'+relative);
  return {path:relative,original,bytes:bytes.length,sha256:declared.sha256};
 });
 return {root:actual,repository,metadataPath,metadataBytes:metadataBytes.length,metadataSha256:crypto.createHash('sha256').update(metadataBytes).digest('hex'),metadata,records,compiledProductOverrides:[...productCandidateFiles],selection:{suite,selectedCase,nativeOnly:true,managed:false,mutation:false},meaning:'Four exact r4 product inputs; all other inputs use the single mainline snapshot. Static identity is not execution or product acceptance.'};
}
function readSourceWakeOverlay(root){
 const actual=candidatePathIdentity(root,true),repository=candidatePathIdentity(repo,true);
 const metadataPath=candidatePathIdentity(path.join(actual,'SOURCE-OVERLAY.json')),metadataBytes=fs.readFileSync(metadataPath),metadata=JSON.parse(metadataBytes.toString('utf8'));
 if(metadata?.schemaVersion!==1||metadata.kind!=='reviewed-source-wake-fixture-input')throw Error('SOURCE_WAKE_OVERLAY_SCHEMA_REQUIRED');
 if(!Array.isArray(metadata.files)||metadata.files.length!==2||new Set(metadata.files.map(row=>row?.path)).size!==2||metadata.files.some(row=>!sourceWakeFiles.includes(row?.path)))throw Error('SOURCE_WAKE_OVERLAY_EXACT_TWO_FILES_REQUIRED');
 const records=sourceWakeFiles.map(relative=>{
  const row=metadata.files.find(row=>row.path===relative);
  if(!Number.isSafeInteger(row.bytes)||row.bytes<=0||typeof row.sha256!=='string'||!/^[0-9a-f]{64}$/i.test(row.sha256))throw Error('SOURCE_WAKE_OVERLAY_INVALID_PIN:'+relative);
  const original=candidatePathIdentity(path.join(actual,relative)),inside=path.relative(actual,original),bytes=fs.readFileSync(original),hash=crypto.createHash('sha256').update(bytes).digest('hex');
  if(path.isAbsolute(inside)||inside==='..'||inside.startsWith('..'+path.sep))throw Error('SOURCE_WAKE_OVERLAY_PATH_ESCAPE:'+relative);
  if(bytes.length!==row.bytes||hash!==row.sha256.toLowerCase())throw Error('SOURCE_WAKE_OVERLAY_BYTES_OR_SHA:'+relative);
  return {path:relative,original,bytes:bytes.length,sha256:hash};
 });
 return {kind:'reviewed-source-wake-fixture-input',root:actual,repository,metadataPath,metadataBytes:metadataBytes.length,metadataSha256:crypto.createHash('sha256').update(metadataBytes).digest('hex'),metadata,records,compiledProductOverrides:[...sourceWakeFiles],selection:{suite,selectedCase,nativeOnly:sourceReconfirmSelected,managed:wakeKeptSourceSelected,mutation:false},inputSelectionOnly:true,sourceApproval:false,productAcceptance:false,meaning:'Two explicit compilation inputs over the original mainline snapshot; this is not a complete product root or an approval. Manifest metadata grants no acceptance.'};
}
function readRuntimeSuccessorOverlay(root){
 if(!delayedSuccessorSelected||suite!=='family-lifecycle'||!managedDll||!selectedDotnet)throw Error('EXACT_MANUAL_RUNTIME_SUCCESSOR_SCOPE_REQUIRED');
 const actual=candidatePathIdentity(root,true),metadataPath=candidatePathIdentity(path.join(actual,'SOURCE-OVERLAY.json'));
 const raw=fs.readFileSync(metadataPath),metadata=JSON.parse(raw.toString('utf8'));
 const allowed=['native/main.cpp','native/input_host_pipe_transport.h','native/virtual_controller_start_runtime.h','native/virtual_runtime_recovery_runtime.h'];
 if(metadata.schemaVersion!==1||metadata.kind!=='reviewed-runtime-successor-fixture-input'||metadata.files?.length!==4||new Set(metadata.files.map(x=>x.path)).size!==4||metadata.files.some(x=>!allowed.includes(x.path)))throw Error('EXACT_RUNTIME_SUCCESSOR_OVERLAY_REQUIRED');
 const records=allowed.map(relative=>{const row=metadata.files.find(x=>x.path===relative),original=candidatePathIdentity(path.join(actual,relative)),bytes=fs.readFileSync(original),sha256=crypto.createHash('sha256').update(bytes).digest('hex');if(bytes.length!==row.bytes||sha256!==row.sha256)throw Error('RUNTIME_SUCCESSOR_OVERLAY_DRIFT:'+relative);return {path:relative,original,bytes:bytes.length,sha256};});
 return {kind:metadata.kind,root:actual,metadataPath,metadataBytes:raw.length,metadataSha256:crypto.createHash('sha256').update(raw).digest('hex'),metadata,records,compiledProductOverrides:allowed,selection:{suite,selectedCase,nativeOnly:false,managed:true,mutation:false},inputSelectionOnly:true,sourceApproval:false,productAcceptance:false};
}


function readRuntimeTerminalOverlay(root){
 if(!terminalFallbackSelected||suite!=='family-lifecycle'||!managedDll||!selectedDotnet)throw Error('EXACT_MANUAL_RUNTIME_TERMINAL_SCOPE_REQUIRED');
 const actual=candidatePathIdentity(root,true),metadataPath=candidatePathIdentity(path.join(actual,'SOURCE-OVERLAY.json'));
 const raw=fs.readFileSync(metadataPath),metadata=JSON.parse(raw.toString('utf8'));
 const allowed=['native/main.cpp','native/input_host_pipe_transport.h','native/virtual_controller_start_runtime.h','native/virtual_runtime_recovery_runtime.h','native/virtual_recovery_fallback_runtime.h','native/msi_native_foreground_isolation_runtime.h'];
 if(metadata.schemaVersion!==1||metadata.kind!=='reviewed-runtime-terminal-fixture-input'||metadata.files?.length!==6||new Set(metadata.files.map(x=>x.path)).size!==6||metadata.files.some(x=>!allowed.includes(x.path)))throw Error('EXACT_RUNTIME_TERMINAL_OVERLAY_REQUIRED');
 const records=allowed.map(relative=>{const row=metadata.files.find(x=>x.path===relative),original=candidatePathIdentity(path.join(actual,relative)),bytes=fs.readFileSync(original),sha256=crypto.createHash('sha256').update(bytes).digest('hex');if(bytes.length!==row.bytes||sha256!==row.sha256)throw Error('RUNTIME_TERMINAL_OVERLAY_DRIFT:'+relative);return {path:relative,original,bytes:bytes.length,sha256};});
 return {kind:metadata.kind,root:actual,metadataPath,metadataBytes:raw.length,metadataSha256:crypto.createHash('sha256').update(raw).digest('hex'),metadata,records,compiledProductOverrides:allowed,selection:{suite,selectedCase,nativeOnly:false,managed:true,mutation:false},inputSelectionOnly:true,sourceApproval:false,productAcceptance:false};
}

const runtimeTerminalBinding=terminalFallbackSelected?readRuntimeTerminalOverlay(productCandidate):null;
const runtimeSuccessorBinding=delayedSuccessorSelected?readRuntimeSuccessorOverlay(productCandidate):null;
const productCandidateBinding=runtimeTerminalBinding??runtimeSuccessorBinding??sdOffCandidateBinding??sourceWakeCandidateBinding??(productCandidate?readProductCandidate(productCandidate):null);
const gpdR4CandidateBinding=productCandidateBinding&&!['reviewed-runtime-terminal-fixture-input','reviewed-runtime-successor-fixture-input','reviewed-source-wake-fixture-input','reviewed-sd-off-source-lost-fixture-input'].includes(productCandidateBinding.kind)?productCandidateBinding:null;
const selectedCandidateFiles=productCandidateBinding?.compiledProductOverrides??[];
// An explicit candidate may retain one already compiled product epoch instead
// of mixing its old main with unrelated moving headers. Tools and case sources
// stay current; every product input is an exact member of the pinned old graph.
const fixedProductEpoch=(()=>{
 const declaration=productCandidateBinding?.metadata?.fixedProductEpoch;
 if(!declaration)return null;
 if(!sourceWakeSelected&&!sdOffSourceLostSelected)throw Error('FIXED_PRODUCT_EPOCH_EXPLICIT_SCOPE_REQUIRED');
 const readPin=(row,label)=>{if(typeof row?.path!=='string'||!path.isAbsolute(row.path)||!/^[a-f0-9]{64}$/.test(row.sha256??''))throw Error('FIXED_PRODUCT_EPOCH_INVALID_PIN:'+label);const file=candidatePathIdentity(row.path),bytes=fs.readFileSync(file);if(crypto.createHash('sha256').update(bytes).digest('hex')!==row.sha256)throw Error('FIXED_PRODUCT_EPOCH_PIN_CHANGED:'+label);return {path:file,sha256:row.sha256,bytes:bytes.length,value:JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''))};};
 const manifest=readPin(declaration.manifest,'manifest'),result=readPin(declaration.result,'result');
 const directory=path.dirname(manifest.path);
 if(path.dirname(result.path)!==directory||path.basename(manifest.path)!=='source-manifest.json'||path.basename(result.path)!=='RESULT.json')throw Error('FIXED_PRODUCT_EPOCH_ORIGINAL_PAIR_REQUIRED');
 const buildPath=candidatePathIdentity(path.join(directory,'build-process.json')),buildBytes=fs.readFileSync(buildPath),build=JSON.parse(buildBytes.toString('utf8'));
 if(build.exitCode!==0||build.error||build.signal||build.timedOut||!Array.isArray(manifest.value.records)||!Array.isArray(manifest.value.compiledGenerated)||!Array.isArray(result.value.results)||!result.value.results.some(row=>row.exitCode===0&&row.result?.status==='PASS'))throw Error('FIXED_PRODUCT_EPOCH_COMPILED_BUSINESS_EVIDENCE_REQUIRED');
 const sourceRoot=candidatePathIdentity(path.join(directory,'source'),true);
 const allowed=relative=>typeof relative==='string'&&!relative.includes('\\')&&!relative.split('/').some(part=>!part||part==='.'||part==='..')&&['native/','capabilities/','deps/json/','deps/adlx-sdk/','deps/webview2/build/native/include/'].some(prefix=>relative.startsWith(prefix));
 const oldCompiled=manifest.value.records,oldUncompiled=manifest.value.copiedButUncompiled;
 if(!Array.isArray(oldUncompiled)||!Array.isArray(manifest.value.snapshotCopies))throw Error('FIXED_PRODUCT_EPOCH_GENERATOR_INPUTS_REQUIRED');
 const allInputs=[...oldCompiled,...oldUncompiled],canonical=rows=>rows.map(row=>JSON.stringify([row.path,row.snapshot,row.sha256])).sort();
 if(JSON.stringify(canonical(allInputs))!==JSON.stringify(canonical(manifest.value.snapshotCopies)))throw Error('FIXED_PRODUCT_EPOCH_SOURCE_GRAPH_PARTITION_CHANGED');
 const rows=allInputs.filter(row=>allowed(row.path));
 if(!rows.length||new Set(rows.map(row=>row.path)).size!==rows.length||rows.filter(row=>row.path==='native/main.cpp').length!==1)throw Error('FIXED_PRODUCT_EPOCH_EXACT_PRODUCT_GRAPH_REQUIRED');
 const records=rows.map(row=>{const expected=path.resolve(sourceRoot,...row.path.split('/')),file=candidatePathIdentity(row.snapshot);if(file!==expected||crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')!==row.sha256)throw Error('FIXED_PRODUCT_EPOCH_MEMBER_CHANGED:'+row.path);return {path:row.path,original:file,sha256:row.sha256};});
 for(const row of manifest.value.compiledGenerated){const file=candidatePathIdentity(row.path);if(crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex')!==row.sha256)throw Error('FIXED_PRODUCT_EPOCH_COMPILED_INPUT_CHANGED');}
 return {manifest:{path:manifest.path,sha256:manifest.sha256,bytes:manifest.bytes},result:{path:result.path,sha256:result.sha256,bytes:result.bytes,originalStatus:result.value.status},build:{path:buildPath,sha256:crypto.createHash('sha256').update(buildBytes).digest('hex'),bytes:buildBytes.length},records,scope:'Exact prior product graph plus explicit two-file candidate overrides; current case/tool sources. Prior business result is input provenance, not this invocation acceptance.'};
})();
function copySourceOriginal(relative,mainline){
 const row=productCandidateBinding?.records.find(row=>row.path===relative);
 return row?candidatePathIdentity(row.original):mainline;
}
function verifyCandidateCopy(relative,original,bytes){
 if(!productCandidateBinding||!selectedCandidateFiles.includes(relative))return;
 const row=productCandidateBinding.records.find(row=>row.path===relative);
 if(original!==row.original||bytes.length!==row.bytes||crypto.createHash('sha256').update(bytes).digest('hex')!==row.sha256)throw Error('PRODUCT_CANDIDATE_COPY_DRIFT:'+relative);
}
function productCandidateDrift(){
 if(!productCandidateBinding)return [];
 try{
  const current=terminalFallbackSelected?readRuntimeTerminalOverlay(productCandidate):delayedSuccessorSelected?readRuntimeSuccessorOverlay(productCandidate):sdOffSourceLostSelected?readSdOffOverlay(productCandidate):sourceWakeSelected?readSourceWakeOverlay(productCandidate):readProductCandidate(productCandidate);
  return JSON.stringify(current)===JSON.stringify(productCandidateBinding)?[]:['product-candidate-identity'];
 }catch(error){return ['product-candidate:'+error.message];}
}
// PRODUCT_CANDIDATE_BINDING_END
const out=path.resolve(args[1]);if(fs.existsSync(out))throw Error('Fresh out required');fs.mkdirSync(out,{recursive:true});const snapshot=path.join(out,'source'),sha=b=>crypto.createHash('sha256').update(b).digest('hex'),records=[];let managedProof=managedDll?{dll:{path:managedDll,sha256:sha(fs.readFileSync(managedDll))},manifest:path.join(path.dirname(managedDll),'fixture-manifest.json')}:null;if(managedProof){managedProof.manifestSha256=sha(fs.readFileSync(managedProof.manifest));const m=JSON.parse(fs.readFileSync(managedProof.manifest));if(m.realSdkLoaded!==false||m.hardwareIo!==false||m.status!=='BUILT'||path.resolve(m.fixtureDll)!==managedDll)throw Error('Not an owned SDK-free compiled fixture');
 try{const b=m.inputBindings;if(!Array.isArray(b?.sources)||b.sources.length<1||b.sources.length>64||!Array.isArray(b.generatedInputs)||b.generatedInputs.length>64||!Array.isArray(m.runtimeArtifacts)||m.runtimeArtifacts.length>32)throw Error('MANAGED_PREREQUISITE_BINDINGS_INVALID');for(const row of b.sources){if(sha(fs.readFileSync(row.path))!==row.sha256||sha(fs.readFileSync(row.snapshot))!==row.sha256)throw Error('MANAGED_PREREQUISITE_STALE_SOURCE:'+row.relative);}for(const row of [...b.generatedInputs,...m.runtimeArtifacts])if(sha(fs.readFileSync(row.path))!==row.sha256)throw Error('MANAGED_PREREQUISITE_STALE_ARTIFACT:'+row.path);managedProof.preflight={sourceBindings:b.sources.length,generatedBindings:b.generatedInputs.length,runtimeArtifacts:m.runtimeArtifacts.length,status:'VERIFIED-BEFORE-NATIVE-COMPILE'};}catch(error){fs.writeFileSync(path.join(out,'managed-prerequisite-failure.json'),JSON.stringify({status:'REJECTED',stage:'before-native-compilation',reason:error.message,managedProof,normalHostMainExecuted:false,realDeviceIo:false},null,2));throw error;}
}
let applicationRequestBinding=null,motionLibraryBinding=null;
if(applicationRequestsBefore){const frozen=path.join(out,'input-application-requests.json');fs.copyFileSync(inputRequests,frozen,fs.constants.COPYFILE_EXCL);applicationRequestBinding={path:inputRequests,snapshot:frozen,sha256:applicationRequestsBefore.sha256,bytes:applicationRequestsBefore.bytes};}
if(motionLibraryBefore){
 const snapshot=path.join(out,'GamepadMotion.dll');fs.copyFileSync(motionLibraryBefore.path,snapshot,fs.constants.COPYFILE_EXCL);
 motionLibraryBinding={...motionLibraryBefore,snapshot,privateRuntimeCopy:path.join(out,'input-application/handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/GamepadMotion.dll'),meaning:'Reviewed existing pure numeric DLL only; no physical SDK or sensors'};
 if(applicationMatrixRows)motionLibraryBinding.privateRuntimeCopies=applicationMatrixRows.map(row=>path.join(out,row.runtimeMode,'handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/GamepadMotion.dll'));
 if(sha(fs.readFileSync(snapshot))!==motionLibraryBefore.sha256)throw Error('MOTION_LIBRARY_COPY_CHANGED');
}
// Reuse the existing managed builder when its already-admitted fake HidHide
// method still emits the old generic ACK. Only this generated terminal changes;
// HostSession, decoder, owner, final outcomes and native replies stay real.
let managedPreparation=null;
if(['onex','aokzoe'].includes(family)){
 const provided=managedProof,manifest=JSON.parse(fs.readFileSync(provided.manifest,'utf8'));
 const fakeInput=manifest.inputBindings.generatedInputs.find(row=>path.basename(row.path)==='ExtractedHost.cs');
 if(!fakeInput)throw Error('ONEX_MANAGED_EXTRACTED_HOST_BINDING_MISSING');
 const previous='private string HandleHidHideCommand(CommandEnvelope e,out bool shutdown){shutdown=false;Fixture.Events.Add(e.Command);return Ack(e,"fake-hidhide");}';
 const fakeText=fs.readFileSync(fakeInput.path,'utf8');
 if(fakeText.includes(previous)){
  const originalBuilder=path.join(repo,'tools/input_lifecycle_managed_fixture.mjs'),raw=fs.readFileSync(originalBuilder,'utf8');
  const directory=path.join(out,'onex-managed-preparation');fs.mkdirSync(directory);
  const preparedBuilder=path.join(directory,'original-builder.onex.mjs');
  const fakeOsFields="private readonly System.Collections.Generic.HashSet<string> _oneXFixtureHidden = new(System.StringComparer.OrdinalIgnoreCase){@\"USB\\VID_045E&PID_028E\\ONEX-INTERNAL\",@\"HID\\VID_045E&PID_028E&IG_00\\ONEX-GAME\"};\nprivate readonly System.Collections.Generic.HashSet<string> _oneXFixtureApplications = new(System.StringComparer.OrdinalIgnoreCase){System.Environment.GetEnvironmentVariable(\"YMCC_FIXTURE_ONEX_APPLICATION\")??\"\"};\nprivate bool _oneXFixtureCloak=true;\n";
  const handler="private string HandleHidHideCommand(CommandEnvelope e,out bool shutdown){shutdown=false;Fixture.Events.Add(e.Command);switch(e.Command){case \"HIDHIDE_CYCLE\":return e.HidHidePath==@\"USB\\VID_045E&PID_028E\\ONEX-INTERNAL\"?Ack(e,\"hidhide-cycle\",\"true\"):Error(e,\"fixture-root-not-inventory\",\"unknown-fake-OS-root\");case \"HIDHIDE_HIDE\":_oneXFixtureHidden.Add(e.HidHidePath);return Ack(e,\"hidhide-hidden\",\"true\");case \"HIDHIDE_UNHIDE\":_oneXFixtureHidden.Remove(e.HidHidePath);return Ack(e,\"hidhide-unhidden\",\"true\");case \"HIDHIDE_APP_REG\":_oneXFixtureApplications.Add(e.HidHidePath);return Ack(e,\"hidhide-app-registered\",\"true\");case \"HIDHIDE_APP_UNREG\":_oneXFixtureApplications.Remove(e.HidHidePath);return Ack(e,\"hidhide-app-unregistered\",\"true\");case \"HIDHIDE_CLOAK\":_oneXFixtureCloak=e.HidHideCloak;return Ack(e,_oneXFixtureCloak?\"hidhide-cloak-on\":\"hidhide-cloak-off\",\"true\");case \"HIDHIDE_CLOAK_OFF\":_oneXFixtureCloak=false;return Ack(e,\"hidhide-cloak-off\",\"true\");case \"HIDHIDE_QUERY\":case \"HIDHIDE_QUERY_STATE\":return Ack(e,e.Command==\"HIDHIDE_QUERY\"?\"hidhide-query\":\"hidhide-query-state\",System.Text.Json.JsonSerializer.Serialize(new{ok=true,hidden=_oneXFixtureHidden,applications=_oneXFixtureApplications,cloakOn=_oneXFixtureCloak}));default:return Error(e,\"fixture-hidhide-unsupported\",\"unsupported-fake-OS-leaf\");}}";
  let generated=replaceUniqueSourceText(raw,"let fake=generated.fake;","let fake=generated.fake;\n fake=replaceOnce(fake,"+JSON.stringify(previous)+","+JSON.stringify(handler)+");\n fake=replaceOnce(fake,\'private sealed partial class HostSession {\',\'private sealed partial class HostSession {\'+"+JSON.stringify(fakeOsFields)+");",'existing managed HidHide fake terminal');
  generated=replaceUniqueSourceText(generated,"const self=fileURLToPath(import.meta.url),repo=path.resolve(path.dirname(self),'..');",'const self='+JSON.stringify(originalBuilder)+',repo='+JSON.stringify(repo)+';','original managed source root');
  generated=generated.replace(/from\s+(['"])(\.[^'"]+)\1/g,(_match,_quote,specifier)=>'from '+JSON.stringify(pathToFileURL(path.resolve(path.dirname(originalBuilder),specifier)).href));
  generated=replaceUniqueSourceText(generated,'const bindings={schemaVersion:1,','generatedInputs.push({path:fileURLToPath(import.meta.url),sha256:fileHash(fileURLToPath(import.meta.url)),bytes:fs.statSync(fileURLToPath(import.meta.url)).size});\n const bindings={schemaVersion:1,','pin preparation transform in managed input identity');
  fs.writeFileSync(preparedBuilder,generated,{flag:'wx'});
  const builder=await import(pathToFileURL(preparedBuilder).href);
  const started=performance.now();const built=builder.buildManagedFixture(path.join(directory,'managed'),{evidenceRoot:out,...(selectedDotnet?{toolchainRoot:path.dirname(selectedDotnet)}:{})});
  managedDll=path.resolve(built.fixtureDll);
  managedProof={dll:{path:managedDll,sha256:sha(fs.readFileSync(managedDll))},manifest:built.manifestPath,manifestSha256:sha(fs.readFileSync(built.manifestPath))};
  const prepared=JSON.parse(fs.readFileSync(managedProof.manifest,'utf8'));
  if(prepared.status!=='BUILT'||prepared.hardwareIo!==false||prepared.realSdkLoaded!==false)throw Error('ONEX_PREPARED_MANAGED_NOT_ADMITTED');
  for(const row of prepared.inputBindings.sources)if(sha(fs.readFileSync(row.path))!==row.sha256||sha(fs.readFileSync(row.snapshot))!==row.sha256)throw Error('ONEX_PREPARED_SOURCE_DRIFT:'+row.relative);
  for(const row of [...prepared.inputBindings.generatedInputs,...prepared.runtimeArtifacts])if(sha(fs.readFileSync(row.path))!==row.sha256)throw Error('ONEX_PREPARED_ARTIFACT_DRIFT:'+row.path);
  managedPreparation={provided,originalBuilder,originalBuilderSha256:sha(raw),preparedBuilder,preparedBuilderSha256:sha(generated),wallMs:performance.now()-started,built,scope:'Existing SDK-free managed builder; only its generated fake HidHide method adapted. No native ACK rewrite; no productionIO duplicate.'};
  fs.writeFileSync(path.join(out,'onex-managed-preparation.json'),JSON.stringify(managedPreparation,null,2),{flag:'wx'});
 }else if(!fakeText.includes('"hidhide-cycle"'))throw Error('ONEX_MANAGED_CYCLE_TERMINAL_UNSUPPORTED');
}
// Historical product inputs are verified original bytes, with no rollback flag.
let productBeforeBinding=null;
if(productBefore){
 const manifest=path.join(productBefore,'MANIFEST.json'),bytes=fs.readFileSync(manifest),rows=JSON.parse(bytes);
 for(const row of rows)if(sha(fs.readFileSync(path.join(productBefore,row.relative)))!==row.sha256)throw Error('SAVED_PRODUCT_SHA_MISMATCH:'+row.relative);
 for(const relative of ['native/main.cpp','native/msi_late_hide_pending.h'])if(rows.filter(r=>r.relative===relative).length!==1)throw Error('SAVED_PRODUCT_INPUT_MISSING:'+relative);
 productBeforeBinding={root:productBefore,manifest,manifestSha256:sha(bytes),records:rows,compiledProductOverrides:['native/main.cpp','native/msi_late_hide_pending.h'],historicalTestHeaderExecuted:false};
 const policyHeader=fs.readFileSync(path.join(productBefore,'native/msi_late_hide_pending.h'),'utf8');
 productBeforeBinding.defaultPendingEnabled=!policyHeader.includes('inline constexpr bool buildEnabled=false;');
 const productMain=fs.readFileSync(path.join(productBefore,'native/main.cpp'),'utf8');
 productBeforeBinding.scopedEmptyCleanupGuard=/if\s*\(ymcc::msi::latehide::mayComplete\(lateHideMsiPendingRequired,\s*lateHideHasPending\)\)\s*inputHostLateHidePendingClearIfToken\(lateHideToken\)/.test(productMain);
 if(suite==='late-hide-pending'&&productBeforeBinding.defaultPendingEnabled&&productBeforeBinding.scopedEmptyCleanupGuard)throw Error('PRODUCT_BEFORE_ALREADY_HAS_BOTH_GUARDS');
}
function copy(dir,rel){for(const e of fs.readdirSync(dir,{withFileTypes:true})){if(e.isSymbolicLink())throw Error('source link');const file=path.join(dir,e.name),next=path.join(rel,e.name);if(e.isDirectory())copy(file,next);else if(/\.(?:h|hpp|hxx|c|cc|cpp|inc|inl)$/i.test(e.name)){const target=path.join(snapshot,next);fs.mkdirSync(path.dirname(target),{recursive:true});const relative=next.replaceAll('\\','/'),original=productBefore&&['native/main.cpp','native/msi_late_hide_pending.h'].includes(relative)?path.join(productBefore,relative):copySourceOriginal(relative,file);const bytes=fs.readFileSync(original);verifyCandidateCopy(relative,original,bytes);fs.writeFileSync(target,bytes);records.push({path:relative,original,snapshot:target,sha256:sha(bytes),...(productCandidateBinding&&selectedCandidateFiles.includes(relative)?{bytes:bytes.length,productCandidate:true}:{})});}}}
if(fixedProductEpoch){
 for(const row of fixedProductEpoch.records){const original=copySourceOriginal(row.path,row.original),bytes=fs.readFileSync(original),target=path.join(snapshot,...row.path.split('/'));verifyCandidateCopy(row.path,original,bytes);if(!selectedCandidateFiles.includes(row.path)&&sha(bytes)!==row.sha256)throw Error('FIXED_PRODUCT_EPOCH_COPY_CHANGED:'+row.path);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);records.push({path:row.path,original,snapshot:target,sha256:sha(bytes),bytes:bytes.length,...(selectedCandidateFiles.includes(row.path)?{productCandidate:true}:{fixedProductEpoch:true})});}
}else for(const rel of ['native','capabilities','deps/json','deps/adlx-sdk','deps/webview2/build/native/include'])copy(path.join(repo,rel),rel);
if(productCandidateBinding){
 if(terminalFallbackSelected){
  const relative='native/virtual_recovery_fallback_runtime.h';
  if(!records.some(row=>row.path===relative)){
   const row=productCandidateBinding.records.find(row=>row.path===relative),original=candidatePathIdentity(row.original),bytes=fs.readFileSync(original),target=path.join(snapshot,relative);
   verifyCandidateCopy(relative,original,bytes);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});
   records.push({path:relative,original,snapshot:target,sha256:sha(bytes),bytes:bytes.length,productCandidate:true});
  }
 }
 for(const relative of selectedCandidateFiles){const rows=records.filter(row=>row.path===relative);if(rows.length!==1||rows[0].original!==productCandidateBinding.records.find(row=>row.path===relative).original)throw Error('PRODUCT_CANDIDATE_COPY_GRAPH_INCOMPLETE:'+relative);}
}
for(const rel of [...(prepareRejectionSelected?['tools/fixtures/input_coordinator_lifecycle/prepare_rejection_cases.inc']:[]),...(deniedTouchpadInputSelected?['tools/input_coordinator_current_compile_only.mjs','tools/input_coordinator_terminal26_source_profile.mjs']:[]),...(gpdPreHideSelected?['tools/fixtures/input_coordinator_lifecycle/pre_hide_cases.inc']:[]),...(nativeWriterReturn?['tools/fixtures/input_coordinator_lifecycle/gpd_native_writer_return.inc']:[]),'tools/msi_late_hide_pending_regression.h','tools/msi_late_hide_publication_regression.h','tools/msi_late_hide_inflight_regression.h','tools/fixtures/input_coordinator_lifecycle/fixture.cpp','tools/fixtures/input_coordinator_lifecycle/focus_recovery_cases.inc','tools/fixtures/input_coordinator_lifecycle/cold_archive_cases.inc','tools/fixtures/input_coordinator_lifecycle/cold_msi_archive_cases.inc','tools/fixtures/input_coordinator_lifecycle/recovery_cost_cases.inc','tools/fixtures/input_coordinator_lifecycle/worker_cost_cases.inc','tools/fixtures/input_coordinator_lifecycle/target_return_cases.inc','tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc','tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc','tools/fixtures/input_coordinator_lifecycle/off_lifecycle_cases.inc','tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc','tools/fixtures/input_coordinator_lifecycle/ps_return_cases.inc','tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc','tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc','tools/input_coordinator_lifecycle_selftest.mjs','tools/ymcc_sim/global/inputhost-protocol/adapter.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json','tools/virtual_controller_runtime_source.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/input_coordinator_lifecycle/compiler_inputs.mjs','tools/fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs']){const target=path.join(snapshot,rel);fs.mkdirSync(path.dirname(target),{recursive:true});const original=rel==='tools/fixtures/input_coordinator_lifecycle/pre_hide_cases.inc'?gpdPreHideParameterSource:rel==='tools/input_coordinator_lifecycle_selftest.mjs'?originalConsumerSource:rel==='tools/fixtures/input_coordinator_lifecycle/fixture.cpp'?originalFixtureCppSource:rel==='tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc'?originalFixturePipeSource:path.join(repo,rel);const bytes=fs.readFileSync(original);fs.writeFileSync(target,bytes);records.push({path:rel,original,snapshot:target,sha256:sha(bytes)});}
// Bind the exact selected Stop receipt checker without changing the native recipe.
const gpdStopReceiptSource='tools/fixtures/input_coordinator_lifecycle/gpd_stop_receipt.mjs';
let inspectGpdStopReceipt;
if(gpdExpiredFirstOnFullCycleSelected){
 const original=path.join(repo,gpdStopReceiptSource),target=path.join(snapshot,gpdStopReceiptSource),bytes=fs.readFileSync(original);
 fs.writeFileSync(target,bytes);records.push({path:gpdStopReceiptSource,original,snapshot:target,sha256:sha(bytes)});
 ({inspectGpdStopReceipt}=await import(pathToFileURL(target).href));
}
// Bind the shared selector used before and after the native fixture runs.
if(gpdPreHideSelected){
 const original=fileURLToPath(new URL('./fixtures/input_coordinator_lifecycle/gpd_pre_hide_selection.mjs',import.meta.url));
 const relative=path.relative(repo,original).replaceAll('\\','/'),target=path.join(snapshot,relative),bytes=fs.readFileSync(original);
 fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);records.push({path:relative,original,snapshot:target,sha256:sha(bytes)});
}
// Bind descriptive DMI bytes only for the explicit existing-consumer variants.
if(suite==='lifecycle-directed'||suite==='family-lifecycle'||suite==='target-return-family'){
 const rel='capabilities/CONTROLLER-REPAIR-SUPPORT.json',original=path.join(repo,rel),target=path.join(snapshot,rel),bytes=fs.readFileSync(original);
 fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes);records.push({path:rel,original,snapshot:target,sha256:sha(bytes)});
}
// Parameter names come from the same arrays that drive the original C++ cases.
// This records expected coverage without a second permanent case registry.
function readOneXSubvariantSelection(text, expectedCases, family) {
 const prefix=family,firstModel=family==='aokzoe'?'A1':'Apex';
  const ids={oneXUnboundVariants:'onex/helper/unbound-slot',oneXDeadlineVariants:'onex/lifecycle/deadline-read-stage',oneXFreshVariants:'onex/lifecycle/Apex/steamdeck',oneXSelectedVariants:'onex/lifecycle/Apex/elite',oneXSelectedInventoryVariants:'onex/helper/fresh-positive'};
 const found=[...text.matchAll(/static\s+constexpr\s+const\s+char\s*\*\s*(oneXUnboundVariants|oneXDeadlineVariants|oneXFreshVariants|oneXSelectedVariants|oneXSelectedInventoryVariants)\s*\[\s*\]\s*=\s*\{([^}]+)\}\s*;/g)];
 if(found.length!==Object.keys(ids).length||new Set(found.map(row=>row[1])).size!==Object.keys(ids).length)throw Error('ONEX_CANONICAL_VARIANTS_MISSING_OR_DUPLICATE');
 const result={};
 for(const row of found){
  ids[row[1]]=ids[row[1]].replace(/^onex/,prefix).replace('/Apex/','/'+firstModel+'/');
  const names=[...row[2].matchAll(/"([^"\r\n]+)"/g)].map(match=>match[1]);
  if(!names.length||new Set(names).size!==names.length||!expectedCases.includes(ids[row[1]]))throw Error('ONEX_CANONICAL_VARIANT_SCOPE_INVALID');
  result[ids[row[1]]]=names;
 }
 return result;
}
function oneXParameterizedScope(text){
  const table=/static constexpr OneXHelperCase oneXHelperCases\[\]=\{([\s\S]*?)\n\};/.exec(text);
  const models=/static constexpr OneXLifecycleModel oneXLifecycleModels\[\]=\{([\s\S]*?)\n\};/.exec(text);
  const personas=/static constexpr const char\* oneXLifecyclePersonas\[\]=\{([^}]+)\};/.exec(text);
  if(!table||!models||!personas)throw Error('ONEX_CANONICAL_PARAMETERS_REQUIRED');
  const modelNames=[...models[1].matchAll(/\{"([^"\r\n]+)",/g)].map(row=>row[1]);
  const personaNames=[...personas[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]);
  const allCases=[...table[1].matchAll(/\{"(onex\/helper\/[^"\r\n]+)",/g)].map(row=>row[1]);
  allCases.push(...modelNames.flatMap(model=>personaNames.map(persona=>'onex/lifecycle/'+model+'/'+persona)),...modelNames.flatMap(model=>personaNames.filter(persona=>persona!=='elite').map(persona=>'onex/lifecycle/pending/'+model+'/'+persona)),'onex/lifecycle/deadline-read-stage');
  const expectedSubvariants=readOneXSubvariantSelection(text,allCases,'onex');
  const wanted=new Set(Object.keys(expectedSubvariants)),expectedCases=allCases.filter(name=>wanted.has(name));
  if(expectedCases.length!==5||new Set(expectedCases).size!==5||Object.values(expectedSubvariants).flat().length!==13)throw Error('ONEX_ORIGINAL_FIVE_THIRTEEN_REQUIRED');
  return {suite:'family-lifecycle',family:'onex',scope:'original-five-parameterized-cases',selectedCase:'onex-parameterized',expectedCases,expectedSubvariants,expectedCount:expectedCases.length,unselectedCanonicalCasesNotRun:allCases.filter(name=>!wanted.has(name)),managedCommandPath:true};
}
let gpdLiveSdTransferSelection=null;
if(gpdLiveSdTransferSelected){
 gpdLiveSdTransferSelection=readGpdLiveSdTransferSelection(fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc'),'utf8'));
 fs.writeFileSync(path.join(out,'gpd-live-sd-transfer-selection-request.json'),JSON.stringify(gpdLiveSdTransferSelection,null,2),{flag:'wx'});
}
let familySelection=null;
if(suite==='family-lifecycle'&&!gpdPreHideSelected){
 const text=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc'),'utf8');
 const table=/static constexpr OneXHelperCase oneXHelperCases\[\]=\{([\s\S]*?)\n\};/.exec(text);
 if(!table)throw Error('ONEX_CANONICAL_CASE_TABLE_MISSING');
 let expectedCases=null,expectedSubvariants=null;
 if(['onex','aokzoe'].includes(family)){
  const models=new RegExp('static constexpr OneXLifecycleModel '+(family==='aokzoe'?'aokzoeLifecycleModels':'oneXLifecycleModels')+'\\[\\]=\\{([\\s\\S]*?)\\n\\};').exec(text);
  const personas=/static constexpr const char\* oneXLifecyclePersonas\[\]=\{([^}]+)\};/.exec(text);
  if(!models||!personas)throw Error('ONEX_REAL_LIFECYCLE_PARAMETERS_MISSING');
  const modelNames=[...models[1].matchAll(/\{"([^"\r\n]+)",/g)].map(m=>m[1]);
  const personaNames=[...personas[1].matchAll(/"([^"\r\n]+)"/g)].map(m=>m[1]);
  expectedCases=[...table[1].matchAll(/\{"(onex\/helper\/[^"\r\n]+)",/g)].map(m=>m[1].replace(/^onex/,family));
  expectedCases.push(...modelNames.flatMap(model=>personaNames.map(persona=>family+'/lifecycle/'+model+'/'+persona)),...modelNames.flatMap(model=>personaNames.filter(persona=>persona!=='elite').map(persona=>family+'/lifecycle/pending/'+model+'/'+persona)),family+'/lifecycle/deadline-read-stage');
  expectedSubvariants=readOneXSubvariantSelection(text,expectedCases,family);
   if(oneXParameterizedSelected){const exact=oneXParameterizedScope(text);expectedCases=exact.expectedCases;expectedSubvariants=exact.expectedSubvariants;}
 }
 if(['classified-fault-cleanup-elite','classified-fault-cleanup-ps'].includes(selectedCase)){
  const faultTable=/classifiedFaultCleanupPersonas\[\]\s*=\s*\{([^}]+)\}/.exec(text);
  const personas=faultTable?[...faultTable[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]):[];
  const wanted=selectedCase.endsWith('-elite')?'elite':'dualsense-edge';
  if(new Set(personas).size!==personas.length||personas.filter(name=>name===wanted).length!==1)throw Error('CLASSIFIED_FAULT_CANONICAL_TABLE_MISSING');
  expectedCases=['classified-fault-cleanup/'+wanted];
 }
 if(runtimeFaultSelection){
  const readTokens=name=>{const match=new RegExp(name+'\\[\\]\\s*=\\s*\\{([^}]+)\\}').exec(text);return match?[...match[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]):[];};
  const families=readTokens('runtimeFamilyTokens140'),personas=readTokens('runtimePersonaTokens140');
  if(new Set(families).size!==families.length||new Set(personas).size!==personas.length||families.filter(x=>x===runtimeFaultSelection[1]).length!==1||personas.filter(x=>x===runtimeFaultSelection[2]).length!==1)throw Error('RUNTIME_FAULT_SOURCE_DECLARED_CASE_MISSING');
  expectedCases=['runtime-after-final/'+runtimeFaultSelection[1]+'/'+runtimeFaultSelection[2]+(runtimeFaultSelection[3]?'/'+runtimeFaultSelection[3]:'')];
 }
 if(expectedCases&&(!expectedCases.length||new Set(expectedCases).size!==expectedCases.length))throw Error('ONEX_CASE_SELECTION_EMPTY_OR_DUPLICATE');
 familySelection={suite,family:family??'default',scope:oneXParameterizedSelected?'original-five-parameterized-cases':runtimeFaultSelection?'runtime-after-final-exact':['classified-fault-cleanup-elite','classified-fault-cleanup-ps'].includes(selectedCase)?'classified-fault-exact':['onex','aokzoe'].includes(family)?'production-helper+actual-managed-start/final/OFF':'original-31',expectedCases,expectedSubvariants,expectedCount:expectedCases?.length??31,managedCommandPath:true};
 fs.writeFileSync(path.join(out,'family-selection-request.json'),JSON.stringify(familySelection,null,2),{flag:'wx'});
}
const expanded=readVirtualControllerSource(path.join(snapshot,'native/main.cpp'));
// Runtime incidents must use the same production include expansion as main.
// A copied but unexpanded header is not an executable-source contract.
const runtimeRecoveryInput=expanded.inputs.find(input=>path.resolve(input.file)===path.resolve(snapshot,'native/virtual_runtime_recovery_runtime.h'));
if(!runtimeRecoveryInput)throw Error('RUNTIME_RECOVERY_PRODUCTION_HEADER_NOT_EXPANDED');
if(!expanded.inputs.some(input=>path.basename(input.file)==='virtual_controller_onex_start_runtime.h'))throw Error('ONEX_PRODUCTION_RUNTIME_NOT_EXPANDED');
const expansionInputs=expanded.inputs.map(input=>{const row=records.find(r=>path.resolve(r.snapshot)===input.file);if(!row||row.sha256!==input.sha256)throw Error('UNBOUND_RUNTIME_EXPANSION_INPUT:'+input.file);return row.path;});
const sourceExpansion=expanded.manifest;
const expandedFile=path.join(snapshot,'native/main.expanded.cpp');fs.writeFileSync(expandedFile,expanded.source,{flag:'wx'});sourceExpansion.expandedSnapshot=expandedFile;
const source=expanded.source;let inert=source;const substitutions=[];
if(wakeKeptSourceSelected){
 const symbol='inputHostRecoveryHealthLatestSourceLocked',original=extract(inert,symbol).code;
 const tail=/return (inputHostRecoveryHealthSource\(&g_inputCaptureGamepadSnapshot, eligible\));(\s*\})$/.exec(original);
 if(!tail||!original.includes('std::lock_guard<std::mutex> sourceLock(g_inputCaptureGamepadSnapshotMx);'))throw Error('WAKE_SOURCE_ACTUAL_PROJECTION_RETURN_REQUIRED');
 const before=tail[0],after='const auto observed='+tail[1]+'; auto observer=std::move(fixtureWakeAfterSourceQuery); fixtureWakeAfterSourceQuery={}; if(observer)observer(); return observed;'+tail[2];
 const adapted=replaceUniqueSourceText(original,before,after,'wake source query one-shot observer');
 if(replaceUniqueSourceText(adapted,after,before,'wake source query reversible observer')!==original)throw Error('WAKE_SOURCE_OBSERVER_NOT_REVERSIBLE');
 inert=replaceUniqueSourceText(inert,original,adapted,'actual source projection observation');
 substitutions.push({symbol,observationOnly:true,selectedCase,sourceLockPreserved:true,actualProjectionPreserved:true,beforeSha256:sha(original),afterSha256:sha(adapted),before:original,after:adapted,reverse:{from:after,to:before},scope:'One-shot callback after the original source projection while retaining the original sourceLock; the exact observed Source is returned. Only wake-kept-source activates this adapter.'});
}
let gpdPersonaMutexObservation=null;
if(gpdPersonaMutexSelected){
 const include='#include "virtual_wake_execution_runtime.h"',getter=extract(source,'inputHostConfiguredPersona').code;
 if(!getter.includes('std::lock_guard<std::mutex> lock(g_inputHostPersonaMx);')||source.indexOf(getter)>=source.indexOf(include))throw Error('GPD_PERSONA_ACTUAL_MUTEX_GETTER_ORDER_REQUIRED');
 const wrapper='static std::string fixtureGpdObservedConfiguredPersona(){\n if(fixtureGpdPersonaMutexObserver)fixtureGpdPersonaMutexObserver(false,fixtureGpdPersonaMutexObserverContext);\n const auto value=inputHostConfiguredPersona();\n if(fixtureGpdPersonaMutexObserver)fixtureGpdPersonaMutexObserver(true,fixtureGpdPersonaMutexObserverContext);\n return value;\n}';
 const transformed=wrapper+'\n#define inputHostConfiguredPersona fixtureGpdObservedConfiguredPersona\n'+include+'\n#undef inputHostConfiguredPersona';
 inert=replaceUniqueSourceText(inert,include,transformed,'GPD exact wake-header configured-persona read observation');
 gpdPersonaMutexObservation={originalGetterSha256:sha(getter),wrapperSha256:sha(wrapper),include,transformedIncludeSha256:sha(transformed),observationOnly:true,originalMutexAndReturnValuePreserved:true,sourceBodyUnchanged:true,callGraphObservationPort:true};
 substitutions.push({symbol:'GPD-wake-configured-persona-read-observation',scope:'Only the original wake header routes its getter through a before/after observer. The original std::mutex getter executes once and returns its exact value; no persona/lock/guard replacement.',...gpdPersonaMutexObservation});
}
// Existing lifecycle fixture controls OS leaves only; it never observes or
// creates a real Windows boot registry session. Keep the real application
// receipt and startup-policy consumers, with one fixed private boot observation.
{
 const original=extract(inert,'bootModeStartupObservation').code;
 const body=applicationMatrixRows?String.raw`
 static const ymcc::bootModeIdentity::Observation value=[](){
  char boot[1024]{};const auto count=GetEnvironmentVariableA("YMCC_FIXTURE_INPUT_BOOT",boot,1024);
  if(!count||count>=1024||std::string(boot).rfind("fixture-private-boot|",0)!=0)throw std::runtime_error("private-matrix-boot-observation-required");
  return ymcc::bootModeIdentity::Observation{std::string(boot),ERROR_SUCCESS,false};
 }();return value;
}`:'\n static const ymcc::bootModeIdentity::Observation value{"fixture-private-boot",ERROR_SUCCESS,false};return value;\n}';
 const controlled=original.slice(0,original.indexOf('{')+1)+body;
 inert=replaceUniqueSourceText(inert,original,controlled,'private boot observation OS boundary');
 substitutions.push({symbol:'bootModeStartupObservation',beforeSha256:sha(original),afterSha256:sha(controlled),scope:'Only kernel boot identity read is controlled; real settings/apply receipt policy is unchanged; no registry or installed global mutex access.'});
}

if(selectedCase==='serial-thread-start'){
 const original=extract(inert,'gamepadSerialStartLocked').code;
 const controlled=replaceUniqueSourceText(original,'std::thread([] {','fixtureSerialCreateThread([] {','serial constructor OS leaf only');
 inert=replaceUniqueSourceText(inert,original,controlled,'actual serial Start constructor boundary');
 substitutions.push({symbol:'gamepadSerialStartLocked::thread-constructor',scope:'Only std::thread constructor creation leaf controlled; flags, loop, callers and actual subsequent std::thread/Stop remain production',originalSha256:sha(original),controlledSha256:sha(controlled)});
}

for(const[name,body]of [
 ['inputHostStartProtectionNow','return startProtectionClock;'],
 // The ordinary100 suite stays on its explicit legacy CLI boundary.
 // Any entry to real writer file/process/job leaves is forbidden I/O.
 ['hidHideCliWriterRecordPath','if(fixtureRuntimeRecoveryOsEnabled){fixtureRuntimeRecoveryWriterPathBoundary();return fixtureTargetReturnRecordPath()+L".runtime-cli";}if(fixtureOneXEnabled)return fixtureTargetReturnRecordPath()+L".onex-cli";if(fixtureRogPartialEnabled)return fixtureTargetReturnRecordPath()+L".rog-cli";if(fixtureColdArchive)return fixtureTargetReturnRecordPath()+L".cli";if(fixtureFullPipe)return fixtureFullPipeCliRecordPath();++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-record-path");'],
 ['hidHideCliWriterMachineBinding','if(fixtureRuntimeRecoveryOsEnabled)return std::string("fixture-machine");if(fixtureOneXEnabled)return std::string("fixture-machine");if(fixtureRogPartialEnabled)return std::string("fixture-machine");if(fixtureColdArchive)return fixtureColdMachine;if(fixtureFullPipe)return std::string("fixture-machine");++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-machine-binding");'],
 ['hidHideCliWriterCapture','++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-capture");'],
 ['hidHideCliWriterProbe','++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-process-probe");'],
 ['hidHideCliWriterProbeJob','++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-job-probe");'],
 ['inputHostTargetReturnRecordPath','return fixtureTargetReturnRecordPath();'],
 ['inputHostTargetReturnMachineBinding','return fixtureColdArchive?fixtureColdMachine:"fixture-machine";'],
 ['inputHostTargetReturnCaptureOwner','return fixtureTargetReturnCaptureOwner(process);'],
 // A new CM ancestry OS leaf, separate from the unchanged three environment leaves.
 // Preserve BeforeHide/AfterRestore algorithms; fail on any unexpected real probe.
 ['inputHostTargetReturnVerifyPhysicalPath','if(fixtureOneXEnabled)return fixtureOneXVerifyPhysicalPath(root,target);if(fixtureColdMsiArchive)return fixtureColdMsiPath(root,target);++forbiddenIo;throw std::logic_error("unexpected-physical-return-CM-verification");'],
 // Never probe a non-private recorded writer PID from the full-TU fixture.
 ['inputHostTargetReturnProbePhysicalWriter','if(fixtureRuntimeRecoveryOsEnabled)return fixtureRuntimeRecoveryWriter(owner);if(fixtureColdArchive)return fixtureColdWriter(owner);if(fixtureFullPipe)return fixtureFullPipeOwnedWriter(owner);++forbiddenIo;throw std::logic_error("unexpected-physical-return-process-probe");'],
 // The census reader and real CLI --dev-list stay behind these explicit leaves.
 ['inputHostTargetReturnColdAbsence','if(fixtureRuntimeRecoveryOsEnabled)return fixtureRuntimeRecoveryAbsence(knownIds);if(fixtureColdArchive)return fixtureColdAbsence(knownIds);if(fixtureFullPipe)return fixtureFullPipeTargetAbsence(knownIds);++forbiddenIo;throw std::logic_error("unexpected-cold-return-PnP-absence");'],
 ['inputHostTargetReturnColdHidden','if(fixtureColdMsiArchive)return fixtureColdMsiHidden(hidden);++forbiddenIo;throw std::logic_error("unexpected-cold-return-hidden-read");'],
 ['observeTdpRuntimeIdentity','/* private offline input fixture: no TDP observation */'],
 ['inputCaptureAssetsPresent','return true;'],
 ['oemBackMappingRefreshGate','/* Separate OEM hook/collector setup excluded; no physical keyboard hook in this fixture. */'],
 ['reg_desktop_monitor_hud','++forbiddenIo;throw std::logic_error("unexpected-desktop-HUD-entry");'],
 ['msiDinputOpen','++forbiddenIo;throw std::logic_error("unexpected-physical-DInput-open");'],
 ['msiClawOpenCommandHid','++forbiddenIo;throw std::logic_error("unexpected-physical-command-HID-open");'],
 ['deviceInterfaceUsbInstance','return fixtureMsiExactUsbRoot(ifacePath,out);'],
 ['ymccXInputGetState14','return fixtureControlledXinputState(userIndex,state);'],
 ['ymccXInputGetStateSecret14','++inertXinputReads;return ERROR_DEVICE_NOT_CONNECTED;'],
 ['ymccXInputSetState14','++forbiddenIo;return ERROR_DEVICE_NOT_CONNECTED;'],
 ['ymccOpenXInputGetDevicePath','return fixtureControlledXinputPath(userIndex);'],
 ['ymccOpenXInputGetUserIndex','return fixtureControlledXinputIndex(devicePath);'],
 ['ymccOpenXInputAvailable','return fixtureXinputSourceLive;'],
 ['hidHideServiceRunning','if(queriedOk)*queriedOk=true;ymStartSeamTrace("health:service-query");fixtureHealthInventoryRead=true;return true;'],
 ['hidHideCycleInstance','if(fixtureOneXEnabled)return fixtureOneXCycle(instanceId,phase,cycleTimeoutMs);return fixtureRogDeferredCycle(instanceId,phase,cycleTimeoutMs);'],
 ['hidHideRun','if(fixtureOwnedCliBoundary)return fixtureOwnedCliRead(arguments,output,exitCodeOut);if(fixtureColdMsiArchive)return fixtureColdMsiCli(arguments,output,exitCodeOut);if(fixtureExitSweep){if(arguments==L"--dev-list"){++fixtureExitSweepLists;if(output)*output=std::string("--dev-hide ")+char(34)+"HID_TEST_OWNED"+char(34);}else if(arguments.rfind(L"--dev-unhide ",0)==0){++fixtureExitSweepUnhides;}else if(arguments==L"--cloak-off"){++fixtureExitSweepCloakOffs;}else{++forbiddenIo;throw std::logic_error("unadmitted-exit-sweep-operation");}if(exitCodeOut)*exitCodeOut=0;return true;}if(fixtureHealthInventoryRead && arguments==L"--dev-all"){fixtureHealthInventoryRead=false;ymStartSeamTrace("health:readonly-dev-all");if(output)*output="offline-readonly-health-observation";if(exitCodeOut)*exitCodeOut=0;return true;}++forbiddenIo;throw std::logic_error("unexpected-HidHide-CLI-entry:"+W2U(arguments));'],
 ['appendNativeLifecycleLog','const auto& detail=observed;if(fixtureSourceReconfirmAfterReconfirmedLog&&event&&std::strcmp(event,"gamepad-physical-source-reconfirmed")==0){auto callback=std::move(fixtureSourceReconfirmAfterReconfirmedLog);fixtureSourceReconfirmAfterReconfirmedLog={};callback();}if(throwLogEvent&&event&&std::strcmp(event,throwLogEvent)==0){throwLogEvent=nullptr;throw std::runtime_error("offline-final-log-exception");}if(fixtureFullPipe)fixtureTargetReturnLifecycleObserve(event,detail);fixtureEvents.emplace_back(json{{"event",event?event:"unknown"},{"detail",std::move(detail)}});if(fixtureMsiRuntimeProofEdges&&event&&std::strcmp(event,"input-host-runtime-recovery")==0&&fixtureEvents.back()["detail"].value("stage","")=="msi-return-proven"){auto cb=std::move(fixtureMsiRuntimeProofEdges);fixtureMsiRuntimeProofEdges={};cb();}if(fixtureLateHideDuringDecision&&event&&std::strcmp(event,"late-hide-exec-decision")==0&&fixtureEvents.back()["detail"].value("decision","")=="apply"){auto callback=std::move(fixtureLateHideDuringDecision);fixtureLateHideDuringDecision={};callback();}if(fixtureLateHideAfterEmptyDecision&&event&&std::strcmp(event,"late-hide-exec-decision")==0&&fixtureEvents.back()["detail"].value("queueToken",0ull)==0&&fixtureEvents.back()["detail"].value("runId","").empty()){auto callback=std::move(fixtureLateHideAfterEmptyDecision);fixtureLateHideAfterEmptyDecision={};callback();}']]){
 if(oemAfterOffSelected&&name==='oemBackMappingRefreshGate')continue;
 let observedBody=body.replaceAll('++forbiddenIo;', '++forbiddenIo;std::cerr<<"fixture-forbidden-io: '+name+'\\n";');
 if(oemAfterOffSelected&&name==='appendNativeLifecycleLog')observedBody='std::lock_guard<std::recursive_mutex> fixtureLogLock(fixtureOemLogMx);'+observedBody;
 const old=extract(source,name).code,start=old.indexOf('{'),replacement=old.slice(0,start+1)+'\n'+observedBody+'\n}';if(inert.split(old).length!==2)throw Error('nonunique boundary '+name);inert=inert.replace(old,replacement);substitutions.push({symbol:name,beforeSha256:sha(old),afterSha256:sha(replacement),scope:'Explicit external device/discovery/process/log boundary only'});
}
if(oemAfterOffSelected){
 for(const [name,body]of [['openTouchKeyboard','++fixtureOemKeyboardActions;fixtureOemActionTid=GetCurrentThreadId();'],['returnToDesktop','++fixtureOemDesktopActions;fixtureOemActionTid=GetCurrentThreadId();return true;']]){
  const original=extract(inert,name).code,controlled=original.slice(0,original.indexOf('{')+1)+'\n'+body+'\n}';
  inert=replaceUniqueSourceText(inert,original,controlled,'OEM after OFF OS action leaf');substitutions.push({symbol:name,beforeSha256:sha(original),afterSha256:sha(controlled),scope:'Final desktop/keyboard OS effect only; no physical input injection or shell launch'});
 }
 for(const [name,changes]of [['oemBackMappingReconcileOnUi',[['SetWindowsHookExW(','fixtureOemSetHook('],['UnhookWindowsHookEx(','fixtureOemUnhook(']]],['oemBackHookProc',[['CallNextHookEx(','fixtureOemCallNextHook(']]]]){
  const original=extract(inert,name).code;let controlled=original;
  for(const [a,b]of changes){if(!controlled.includes(a))throw Error('OEM_OS_LEAF_MISSING:'+name+'/'+a);controlled=controlled.split(a).join(b);}
  inert=replaceUniqueSourceText(inert,original,controlled,'OEM private hook OS ends');substitutions.push({symbol:name,beforeSha256:sha(original),afterSha256:sha(controlled),scope:'Only SetHook/Unhook/CallNext API names replaced; complete capture/consume/release body preserved'});
 }
}
// Explicit PS-only complete root census OS leaf. The real parser remains in the controlled reader.
if(source.includes('static ymcc::targetReturn::pnpAbsence::Result inputHostTargetReturnPsProfileAbsence(')){
 const original=extract(source,'inputHostTargetReturnPsProfileAbsence').code;
 const replacement=original.slice(0,original.indexOf('{')+1)+'\n return fixturePsProfileAbsence();\n}';
 inert=replaceUniqueSourceText(inert,original,replacement,'PS profile complete census OS leaf');
 substitutions.push({symbol:'inputHostTargetReturnPsProfileAbsence',beforeSha256:sha(original),afterSha256:sha(replacement),scope:'Only broad-present OS leaf is controlled; actual PS profile parser, no-pin consumer, lifecycle and durable settlement remain production.'});
}
// Explicit OneX read-only OS leaves, including the distinct current-child inventory. Original policy bodies
// and the actual final write guard below remain byte-identical to production.
for(const [name,body] of [
 ['inputHostOneXLocations','return fixtureOneXLocations(root);'],
 ['inputHostOneXPortKind','return fixtureOneXPortKind(root);'],
 ['inputHostOneXSourceSlot','return fixtureOneXSourceSlot(source,slot);'],
 ['inputHostOneXRootStarted','return fixtureOneXRootStarted(root);'],
 ['inputHostOneXResolveSources','return fixtureOneXResolveSources(sources,complete);'],
 ['inputHostOneXResolveSelectedChildren','return fixtureOneXResolveSelectedChildren(children,complete);'],
 ['inputHostOneXVisibility','return fixtureOneXVisibility(snapshot);']
]){
 const original=extract(source,name).code;
 const replacement=original.slice(0,original.indexOf('{')+1)+'\n if(!fixtureOneXEnabled){++forbiddenIo;throw std::logic_error("unadmitted-OneX-OS-leaf");}\n '+body+'\n}';
 inert=replaceUniqueSourceText(inert,original,replacement,name+' explicit OS leaf');
 substitutions.push({symbol:name,beforeSha256:sha(original),afterSha256:sha(replacement),scope:'Only explicit OneX OS observation leaf; disabled by default. No source-ready or final outcome substitution.'});
}
// settings.write also invokes a separate Steam-wallpaper media queue owner.
// It is not part of controller admission or recovery. Keep settings commit,
// controller refresh and all assertions real; isolate only this unrelated
// post-commit external notification, never admit the media/global mutex.
{
 const original=extract(source,'steamWallpaperForegroundPreference').code;
 const altered=original.slice(0,original.indexOf('{')+1)+'\n fixtureObserveWallpaperNotification();\n}';
 inert=replaceUniqueSourceText(inert,original,altered,'separate-wallpaper-postcommit-notification');
 substitutions.push({symbol:'steamWallpaperForegroundPreference',beforeSha256:sha(original),afterSha256:sha(altered),scope:'Separate Steam wallpaper media queue notification only; not controller settings commit/admission/refresh/verification/recovery. Original named media mutex remains forbidden. Counter records excluded notifications; no media files/downloads/workers/Steam operations.'});
}
// The existing topology authority lives in a non-expanded compiler header.
// Inline its frozen bytes only in the private TU, replacing that one read-only
// OS leaf. Every other header body is preserved; no developer-machine query.
{
 const relative='native/external_controller_priority_runtime.h';
 const originalHeader=fs.readFileSync(path.join(snapshot,relative),'utf8');
 const original=extract(originalHeader,'externalPriorityUsbPortKind').code;
 const changed=original.slice(0,original.indexOf('{')+1)+'\n return fixtureRogDeferredUsbPortKind(root);\n}';
 const adapted=replaceUniqueSourceText(originalHeader,original,changed,'explicit USB topology OS leaf');
 inert=replaceUniqueSourceText(inert,'#include "external_controller_priority_runtime.h"',adapted.replace(/^#pragma once/m,'// pragma once: private TU inline header'),'frozen topology header in private TU');
 substitutions.push({symbol:'externalPriorityUsbPortKind',source:relative,sourceSha256:sha(originalHeader),beforeSha256:sha(original),afterSha256:sha(changed),scope:'Only existing read-only OS topology leaf is inert, default forbidden and opt-in portKind=0. Remainder of frozen header inlined unchanged.'});
}
// Conditional adapters are fixture-only process/transport/hide/diagnostic endpoints. Actual
// inputHostStart/Stop/queue/clear/request/Win32 pipe implementations stay unchanged.
for(const [name,body]of [['ymStartSeamTrace','if(fixtureSwitchOwnedHideSeed && entry=="ensure:main"){auto callback=fixtureSwitchSourceAtEnsure;if(callback)callback();}if(entry=="ensure:main"&&fixtureGpdTransferSourceAtEnsure){auto callback=std::move(fixtureGpdTransferSourceAtEnsure);callback();}'],['ymStartPipeSeamServe','if(fixtureFullPipe)return false;if(fixtureRuntimeRecoveryOsEnabled && command && std::strcmp(command,"HELLO")==0)g_inputHostHostInstanceId="fixture-native-event-"+std::to_string(g_inputHostEpoch)+"-"+g_inputHostRunId;if(fixtureFrameFirstAck && command && std::string(command)=="SUBMIT_FRAME"){if(responseOut)*responseOut=json{{"status",expectedStatus?expectedStatus:""},{"detail",""},{"firstFrame",true}};return true;}if(fixtureStartupTransportError && command && std::string(command)==g_startEntrySeam.pipeFailCommand)g_inputHostSwitchRecoveryTransportError=fixtureStartupTransportError;'],['ymStartHostExitWait','if(fixtureFullPipe)return WaitForSingleObject(process,5000);'],['ymStartCreateProcessW','if(fixtureFullPipe)return fixtureCreateManagedProcess(si,pi);'],['hidHideResolvePhysicalIdentities','if(fixtureRogInitialIdentityEnabled){if(fixtureFullPipe&&g_machineIdentity.family==YmccFamily::Gpd){out={fixtureGpdPostHideSource()};if(complete)*complete=true;return true;}return fixtureRogInitialDiscovery(out,complete);}'],['hidHideRefreshPhysicalHide','if(fixtureWakeScenario){++fixtureWakeHideCalls;auto beforeReturn=std::move(fixtureWakeBeforeHideReturn);fixtureWakeBeforeHideReturn={};if(beforeReturn)beforeReturn();return fixtureWakeHideResult;}'],['hidHideApplyPhysicalTransaction','if(fixtureFullPipe && g_machineIdentity.family==YmccFamily::Gpd)return fixtureGpdPostHideApply(transactionId,powerCycle);'],['hidHideRestorePhysicalTransaction','if(fixtureOneXEnabled && ymStartSeamActive() && g_startEntrySeam.hidHideRestoreResult)g_hidHideVisibilityJournal=HidHideVisibilityJournal{};if(fixtureSwitchOwnedHideSeed && ymStartSeamActive() && g_startEntrySeam.hidHideRestoreResult)g_hidHideVisibilityJournal=HidHideVisibilityJournal{};if(fixtureFullPipe && fixtureGpdPostHideApplied){fixtureGpdPostHideApplied=false;g_hidHideVisibilityJournal=HidHideVisibilityJournal{};g_xinputPhysicalSourceLocked=false;g_xinputPhysicalSourceMask=0;g_xinputPreTargetSlots=0;g_xinputSourceReconfirmMask.store(0,std::memory_order_release);g_xinputReconfirmIdentity.clear();g_xinputReconfirmMsiGeneration=0;g_xinputReconfirmRejectLogged=false;g_xinputPreTargetBoundaryScanned.store(false,std::memory_order_relaxed);g_xinputSlotFullScanOnce.store(false,std::memory_order_relaxed);g_xinputSlotFullScanEpochStart.store(0,std::memory_order_release);g_xinputBoundaryScanEpoch.store(0,std::memory_order_release);g_xinputDiscoveryDirty=true;g_xinputAdmittedMask=0;g_xinputLoggedAdmittedMask=0;return g_startEntrySeam.hidHideRestoreResult;}'],['hidHideCaptureVisibilitySnapshot','if(fixtureOwnedCliBoundary){++fixtureOwnedCliFullQueries;snapshot={};snapshot.valid=true;snapshot.cloaking=true;snapshot.hidden=fixtureOwnedCliHidden;return true;}if(fixtureOneXEnabled)return inputHostOneXVisibility(snapshot);if(fixtureRogPartialEnabled || fixtureRogDeferredJournalSeed)return fixtureRogPartialSnapshot(snapshot);if(fixtureFullPipe && fixtureGpdPostHideApplied)return fixtureGpdPostHideSnapshot(snapshot);'],['hidHideGetPhysicalIdentities','if(fixtureOwnedCliBoundary){++fixtureOwnedCliPhysicalQueries;out=fixtureOwnedCliPhysical;return true;}if(fixtureOneXEnabled)return fixtureOneXReadPhysical(out);if(fixtureFullPipe && fixtureGpdPostHideApplied)return fixtureGpdPostHidePhysicalIdentities(out);'],['inputCaptureLogVisibilitySnapshotGated','if(fixtureWakeScenario){++fixtureWakeVisibilityExclusions;return;}']]){const original=extract(source,name).code;const brace=original.indexOf('{');const changed=original.slice(0,brace+1)+'\n'+body+'\n'+original.slice(brace+1);if(inert.split(original).length!==2)throw Error('adapter source not unique');inert=inert.replace(original,changed);substitutions.push({symbol:name,scope:'Explicit conditional fixture process/pipe/hide/diagnostic ends; existing behavior unchanged when scenario flags are off',beforeSha256:sha(original),afterSha256:sha(changed)});}

// GPD_PRE_HIDE_ENDPOINTS_BEGIN
function applyGpdPreHideEndpoints(text,extractFunction,_normalizedReplace,hash,changes){
 // Complete extracted bodies already contain their exact mixed LF/CRLF bytes.
 // Preserve those bytes instead of normalizing the full-function anchor.
 const replaceUnique=(source,before,after,label)=>{
  if(!before||source.split(before).length!==2)throw Error('GPD_PRE_HIDE_EXACT_ANCHOR:'+label);
  return source.replace(before,()=>after);
 };
 for(const [name,observer]of [['inputHostConfiguredPersona',' struct PreHideGetterObserver {~PreHideGetterObserver(){fixturePreHideGetterReturn();}} preHideGetterObserver;'],['ymStartPipeSeamServe',' fixturePreHidePipe(command);'],['hidHideApplyPhysicalTransaction',' if(fixturePreHideApply(transactionId,powerCycle))return true;']]){
  const original=extractFunction(text,name).code,brace=original.indexOf('{');
  const changed=original.slice(0,brace+1)+'\n'+observer+'\n'+original.slice(brace+1);
  text=replaceUnique(text,original,changed,'GPD pre-hide test observer '+name);
  changes.push({symbol:name+'/pre-hide-observer',beforeSha256:hash(original),afterSha256:hash(changed),scope:'Selected fixture-only boundary observer; original function body retained'});
 }
 for(const name of ['hidHideCaptureVisibilitySnapshot','hidHideGetPhysicalIdentities','hidHideRestorePhysicalTransaction']){
  const original=extractFunction(text,name).code;
  const changed=replaceUnique(original,'fixtureFullPipe && fixtureGpdPostHideApplied','(fixtureFullPipe || fixturePreHideActive()) && fixtureGpdPostHideApplied','GPD selected existing journal I/O leaf '+name);
  text=replaceUnique(text,original,changed,'GPD existing I/O leaf '+name);
  changes.push({symbol:name+'/pre-hide-journal-port',beforeSha256:hash(original),afterSha256:hash(changed),scope:'Only selected case can expose original already-seeded GPD journal facts'});
 }
 return text;
}
if(gpdPreHideSelected)inert=applyGpdPreHideEndpoints(inert,extract,replaceUniqueSourceText,sha,substitutions);
// GPD_PRE_HIDE_ENDPOINTS_END

if(gpdLiveSdTransferSelected){
 // The historical test result port returns before the real in-memory carry
 // and journal retirement. For these two actual serial transfers execute that
 // original production body; it performs no device unhide/cycle or OS query.
 const original=extract(inert,'hidHideHoldPhysicalTransaction').code;
 const before='if (ymStartSeamActive()) { ymStartSeamTrace("hidhide-hold"); return g_startEntrySeam.hidHideHoldResult; }';
 const after='if (ymStartSeamActive() && !(fixtureFullPipe && g_machineIdentity.family==YmccFamily::Gpd)) { ymStartSeamTrace("hidhide-hold"); return g_startEntrySeam.hidHideHoldResult; }';
 const changed=replaceUniqueSourceText(original,before,after,'GPD serial transfer executes actual held-visibility carry');
 inert=replaceUniqueSourceText(inert,original,changed,'exact GPD carry consumer');
 substitutions.push({symbol:'hidHideHoldPhysicalTransaction/test-result-port',beforeSha256:sha(original),afterSha256:sha(changed),scope:'Exact directed GPD transfer only bypasses historical early fake result; actual carry/retire production body executes unchanged. No device IO or successful cleanup result is supplied.'});
}

if(sdOffSourceLostSelected){
 for(const [name,body]of [
  ['hidHideRun','if(fixtureSdOffSourceLostActive())return fixtureSdOffSourceLostCli(arguments,output,exitCodeOut);'],
  ['hidHideCaptureVisibilitySnapshot','if(fixtureSdOffSourceLostActive())return fixtureSdOffSourceLostSnapshot(snapshot);'],
  ['inputHostTargetReturnVerifyPhysicalPath','if(fixtureSdOffSourceLostActive())return fixtureSdOffSourceLostPath(root,target);'],
  ['appendNativeLifecycleLog','if(fixtureSdOffSourceLostActive())fixtureSdOffSourceLostObserve(event,observed);']
 ]){
  const original=extract(inert,name).code,brace=original.indexOf('{'),changed=original.slice(0,brace+1)+'\n'+body+'\n'+original.slice(brace+1);
  if(inert.split(original).length!==2)throw Error('OFF_ACTIVE_OS_LEAF_EXACT_EXTRACTED_BYTES_REQUIRED:'+name);
  const prior=inert;inert=inert.replace(original,()=>changed);
  if(inert.replace(changed,()=>original)!==prior)throw Error('OFF_ACTIVE_OS_LEAF_NOT_BYTE_REVERSIBLE:'+name);
  substitutions.push({symbol:name,scope:'OFF active-only OS leaf or pre-move log observation; original complete PnP/Session/return/CAS predicates unchanged',before:original,after:changed,beforeSha256:sha(original),afterSha256:sha(changed)});
 }
 // Product overlay carries the default callback on its FIRST declaration.
 // Preserve the historical three-argument source and default call sites.
 const actual=extract(source,'inputHostTargetReturnAfterPhysicalRestoreLocked').code;
 if(actual.includes('std::function<bool()>')){
  const forward=/static bool inputHostTargetReturnAfterPhysicalRestoreLocked\(\s*const HidHideVisibilitySnapshot&,bool,const std::vector<std::wstring>&,\s*const std::function<bool\(\)>& returnCurrent = \{\}\);/;
  if(!forward.test(inert))throw Error('SD_OFF_FOURTH_OPTIONAL_CALLBACK_FORWARD_REQUIRED');
 }
}

// Exact extraction retains mixed LF/CRLF from the actual transport source.
// Do not normalize a complete production function or replace its Runtime.
function replaceActualTransportSource(text,before,after,label){
 if(!before||text.split(before).length!==2)throw Error('ACTUAL_TRANSPORT_EXACT_SOURCE_REQUIRED:'+label);
 const changed=text.replace(before,()=>after);
 if(changed.replace(after,()=>before)!==text)throw Error('ACTUAL_TRANSPORT_SOURCE_NOT_REVERSIBLE:'+label);
 return changed;
}
// One classified error at the existing transport OS boundary. The original
// request producer, classification, runtime offer and lifecycle dispatch execute.
for(const [name,body]of [
 ['inputHostWaitClientLocked','if(fixtureRuntimeRecoveryFaultEnabled && !fixtureRuntimeRecoveryFaultConsumed && (!fixtureRuntimeRecoveryFirstHelloOnly || g_inputHostSwitchRecoveryStartRequestId!=0)){fixtureRuntimeRecoveryFaultConsumed=true;++fixtureRuntimeRecoveryFaultCalls;if(contextRejectedOut)*contextRejectedOut=false;if(errorOut)*errorOut=fixtureStartupTransportError;return false;}'],
 ['inputHostPipeIo','if(fixtureRuntimeRecoveryFaultEnabled && !fixtureRuntimeRecoveryFaultConsumed && !fixtureRuntimeRecoveryFirstHelloOnly){fixtureRuntimeRecoveryFaultConsumed=true;++fixtureRuntimeRecoveryFaultCalls;if(transferredOut)*transferredOut=0;if(contextRejectedOut)*contextRejectedOut=false;if(errorOut)*errorOut=fixtureStartupTransportError;return false;}']
]){
 const original=extract(inert,name).code,brace=original.indexOf('{');
 const changed=original.slice(0,brace+1)+'\n'+body+'\n'+original.slice(brace+1);
 if(!original.includes('bool* contextRejectedOut=nullptr')||!original.includes('if(contextRejectedOut)*contextRejectedOut=result.contextRejected;')||!original.includes('ymcc::inputHost::pipeTransport::processRuntime().run(')||!original.includes('ymcc::inputHost::pipeTransport::Cancellation{'))throw Error('ACTUAL_TRANSPORT_CANCELLATION_AND_REJECT_OUTPUT_REQUIRED:'+name);
 inert=replaceActualTransportSource(inert,original,changed,'runtime transport OS error leaf '+name);
 substitutions.push({symbol:name,scope:'Opt-in one-shot existing transport OS failure only; complete request classifier and runtime consumer unchanged',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// Read-only write-ahead evidence is observed immediately before the real OS
// pipe primitive. No create/accept/settlement logic is replaced by this adapter.
{
 const original=extract(inert,'inputHostPipeIo').code;const brace=original.indexOf('{');
 const body='\n    if(fixtureFullPipe && write)fixtureTargetReturnWireObserve(buffer,size);\n';
 const changed=original.slice(0,brace+1)+body+original.slice(brace+1);
 inert=replaceActualTransportSource(inert,original,changed,'target-return-wire-observer');
 substitutions.push({symbol:'inputHostPipeIo/write-entry-observer',scope:'Read-only real Session/store evidence before actual owned pipe write; no command/receipt/decision substitution',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// The original overlapped owned-child pipe IO still executes. For a directed
// receipt test only, replay one previously observed actual report ACK, or corrupt
// one identity/asset field at this explicit wire-read endpoint. No HostSession,
// SDK ownership, envelope parser, tuple classifier or lifecycle branch is replaced.
{
 const original=extract(inert,'inputHostPipeIo').code;
 const anchor='        return result.ok;';if(original.split(anchor).length!==2)throw Error('wire IO return anchor');
 const body=String.raw`
        fixtureFocusRecoveryPort(pipe,write,buffer,size,result.ok,result.transferred,result.error);
        ${prepareRejectionSelected?String.raw`if(fixturePrepareRejectionCapture && result.ok && buffer){
            const auto bytes=write?size:result.transferred;
            if(bytes){auto row=json::parse(std::string(static_cast<const char*>(buffer),bytes),nullptr,false);if(row.is_object())fixturePrepareRejectionWire.push_back({{"write",write},{"message",std::move(row)}});}
        }`:''}
        if(fixturePipeBudgetEnabled)fixturePipeBudgetPort(write?"write":"read",result.ok,timeoutMs,buffer,size);
        if(fixtureFullPipe && fixturePrepareAckLoss && result.ok && !write && transferredOut && *transferredOut){
            const auto received=json::parse(std::string(static_cast<char*>(buffer),*transferredOut),nullptr,false);
            if(received.is_object() && received.value("command","")=="PREPARE_TARGET" && received.value("status","")=="target-prepared"){
                fixturePrepareAckOriginal=received;fixturePrepareAckLossConsumed=true;fixturePrepareAckLoss=false;
                fixtureTargetReturnAckLostObserve(received);
                if(errorOut)*errorOut=ERROR_NO_DATA;if(transferredOut)*transferredOut=0;return false;
            }
        }
        if(fixtureFullPipe && fixtureWireCapture && result.ok && !write && transferredOut && *transferredOut){
            const std::string received(static_cast<char*>(buffer),*transferredOut);
            const auto reply=json::parse(received,nullptr,false);
            if(reply.is_object() && reply.value("command","")=="SUBMIT_FRAME" && reply.value("status","")=="frame-accepted"){
                if(!fixtureWireFault.empty()){
                    std::string replaced;fixtureWireOriginal=reply;
                    if(fixtureWireFault=="replay")replaced=fixtureWirePrevious;
                    else{auto corrupted=reply;if(fixtureWireFault=="asset")corrupted["assetVerified"]=false;else corrupted["hostInstanceId"]="offline-wrong-owned-instance";replaced=corrupted.dump();}
                    if(replaced.empty() || replaced.size()>size)throw std::logic_error("wire-fault-outside-read-buffer");
                    std::memcpy(buffer,replaced.data(),replaced.size());*transferredOut=static_cast<DWORD>(replaced.size());
                    fixtureWireInjected=json::parse(replaced);fixtureWireFaultConsumed=true;fixtureWireFault.clear();
                }else fixtureWirePrevious=received;
            }
        }
`;
 const changed=original.replace(anchor,()=>body+anchor);inert=replaceActualTransportSource(inert,original,changed,'actual-pipe-wire-observer');
 substitutions.push({symbol:'inputHostPipeIo',scope:'Original real owned-child pipe read/write, optional one captured ACK replay or one wire field corruption only. Production parser/classifier/HostSession untouched.',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// Adapt only the already-explicit successful hide seam. No journal is seeded
// on failure or without the opt-in flag and actual TLS deferred debt.
{
 const original=extract(inert,'hidHideApplyPhysicalTransaction').code;
 const from='return g_startEntrySeam.hidHideApplyResult;';
 const to='return fixtureOneXEnabled?fixtureOneXHideSeam(g_startEntrySeam.hidHideApplyResult,transactionId,powerCycle):fixtureRogDeferredHideSeam(g_startEntrySeam.hidHideApplyResult,transactionId,powerCycle);';
 const changed=replaceUniqueSourceText(original,from,to,'ROG deferred successful hide I/O port');
 if(inert.split(original).length!==2)throw Error('ROG deferred hide seam not unique');inert=inert.replace(original,changed);
 substitutions.push({symbol:'hidHideApplyPhysicalTransaction/deferred-journal-port',beforeSha256:sha(original),afterSha256:sha(changed),scope:'Existing explicit hide result port only; opt-in TLS debt plus successful hide seeds fake exact-owned 0B05 journal. No worker/helper replacement.'});
}
// Original ConnectNamedPipe owner executes. Supply elapsed time only at the
// successful external connect return, exactly like the existing wire observer.
{
 const original=extract(inert,'inputHostWaitClientLocked').code;
 const anchor='        return result.ok;';if(original.split(anchor).length!==2)throw Error('connect budget port anchor');
 const changed=original.replace(anchor,()=>'        if(fixturePipeBudgetEnabled)fixturePipeBudgetPort("connect",result.ok,timeoutMs);\n'+anchor);
 inert=replaceActualTransportSource(inert,original,changed,'actual-connect-result-observer');substitutions.push({symbol:'inputHostWaitClientLocked',scope:'Original actual owned Connect IO; optional elapsed-clock observation after kernel return only.',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// Exact reader control flow, with only four SetupAPI leaf names rebound to
// owned deterministic endpoints. No read/decision branch is replaced.
{
 const original=extract(inert,'psWakeProbeOwnEndpoint').code;
 let adapted=original;for(const [api,endpoint]of [['SetupDiGetClassDevsW','fixturePsGetClassDevs'],['SetupDiEnumDeviceInterfaces','fixturePsEnum'],['SetupDiGetDeviceInterfaceDetailW','fixturePsDetail'],['SetupDiDestroyDeviceInfoList','fixturePsDestroy'],['CM_Get_Device_IDW','fixturePsDeviceId'],['CM_Get_Parent','fixturePsParent']])adapted=adapted.replaceAll(api+'(',endpoint+'(');
 if(adapted===original||inert.split(original).length!==2)throw Error('PS SetupAPI boundary anchor');inert=inert.replace(original,adapted);substitutions.push({symbol:'psWakeProbeOwnEndpoint',beforeSha256:sha(original),afterSha256:sha(adapted),scope:'Only read-only SetupAPI/CM leaf calls rebound; loop/error/classification/ancestry/decision logic unchanged'});
}
if(sourceReconfirmSelected){
 const symbol='ymccXboxReanchorRouteMatchesSource',original=extract(inert,symbol).code;
 const leaves=[['SetupDiCreateDeviceInfoList','fixtureXboxCreate'],['SetupDiOpenDeviceInterfaceW','fixtureXboxOpen'],['SetupDiGetDeviceInterfaceDetailW','fixtureXboxDetail'],['SetupDiDestroyDeviceInfoList','fixtureXboxDestroy'],['CM_Get_Device_IDW','fixtureXboxDeviceId'],['CM_Get_Parent','fixturePsParent']];
 let adapted=original;const leafCounts=[];
 for(const [api,endpoint]of leaves){const count=adapted.split(api+'(').length-1;if(count<1||count>2)throw Error('SOURCE_ROUTE_EXACT_OS_LEAF_REQUIRED:'+api);adapted=adapted.replaceAll(api+'(',endpoint+'(');leafCounts.push({api,endpoint,count});}
 let restored=adapted;for(const [api,endpoint]of leaves)restored=restored.replaceAll(endpoint+'(',api+'(');
 if(restored!==original||leaves.some(([api])=>adapted.includes(api+'(')))throw Error('SOURCE_ROUTE_OS_ADAPTATION_NOT_EXACTLY_REVERSIBLE');
 inert=replaceUniqueSourceText(inert,original,adapted,'source collection actual route predicate');
 substitutions.push({symbol,beforeSha256:sha(original),afterSha256:sha(adapted),before:original,after:adapted,leafCounts,reverse:leaves.map(([api,endpoint])=>({from:endpoint+'(',to:api+'('})),scope:'All six read-only SetupAPI/CM leaves are rebound to the declared private graph. Exact inverse restores the entire original route predicate; its decisions, index checks, loop and return are unchanged.'});
}
for(const [symbol,leaves]of [
 ['inputHostXboxInterfaceIsTarget',[['SetupDiCreateDeviceInfoList','fixtureXboxCreate'],['SetupDiOpenDeviceInterfaceW','fixtureXboxOpen'],['SetupDiGetDeviceInterfaceDetailW','fixtureXboxDetail'],['SetupDiDestroyDeviceInfoList','fixtureXboxDestroy'],['CM_Get_Device_IDW','fixtureXboxDeviceId']]],
 ['inputHostXboxConfirmStartupLocked',[['CM_Locate_DevNodeW','fixtureXboxLocate'],['CM_Get_DevNode_Status','fixtureXboxStatus']]]]){
 const original=extract(inert,symbol).code;let adapted=original;for(const[from,to]of leaves)adapted=adapted.replaceAll(from+'(',to+'(');if(adapted===original||inert.split(original).length!==2)throw Error('Elite identity leaf adapter anchor:'+symbol);inert=inert.replace(original,adapted);substitutions.push({symbol,beforeSha256:sha(original),afterSha256:sha(adapted),scope:'Only exact read-only SetupAPI/ConfigManager leaf calls rebound to owned identity fixture; Xbox native parsing, matching, poll/state/path/index/readback and decision flow unchanged.'});
}
if(mutation==='bypass-admission'){const anchor='if (!targetRequest.admit(oldValid ? old : json::object(), parsed))';if(inert.split(anchor).length!==2)throw Error('Admission mutation anchor changed');inert=inert.replace(anchor,'if (false && !targetRequest.admit(oldValid ? old : json::object(), parsed))');substitutions.push({symbol:'ymSettingsWriteDocumentUnlocked',scope:'Explicit semantic negative: remove request admission; assertions unchanged',negativeControl:true});}
if(mutation==='rollback-recovery-inheritance'){
 const replace=(a,b)=>{inert=replaceUniqueSourceText(inert,a,b,'Recovery mutation anchor changed');};
 replace('        const auto inherited = g_virtualStartProtection.existingTicket(configuredPersona,inputHostStartProtectionNow());\n        const auto admitted = inherited ? ymcc::VirtualStartProtection::Admission{true,inherited,false}\n            : inputHostStartProtectionConsume(configuredPersona);','        const auto admitted = inputHostStartProtectionConsume(configuredPersona);');
 replace('    } else if (virtualTargetChanged && !targetEnabled &&\n            !(targetRequested && personaAdmitted && !targetMachineRejection)) {','    } else if (virtualTargetChanged && !targetEnabled) {');
 replace('    auto requestTicket = boundMsiPower?entryView.requestId:\n        g_virtualStartProtection.existingTicket(requestedPersona,entryClock);','    auto requestTicket = boundMsiPower?entryView.requestId:\n        g_virtualStartProtection.pendingTicket(requestedPersona,entryClock);');
 substitutions.push({symbol:'inputCaptureRefreshRealStickSettings/inputHostStart',scope:'Explicit semantic negative: reproduce old operational-pause cancellation and loss of inherited recovery ownership; assertions unchanged',negativeControl:true});
}
if(mutation==='rollback-cancellation-guards'){
 const removeRange=(symbol,from,to)=>{const original=extract(inert,symbol).code;const a=original.indexOf(from),b=original.indexOf(to,a);if(a<0||b<=a)throw Error('Cancellation negative anchor drift:'+symbol);const altered=original.slice(0,a)+original.slice(b);if(inert.split(original).length!==2)throw Error('Cancellation symbol not unique');inert=inert.replace(original,altered);};
 removeRange('inputHostPerformPsWakeRearm','    // A complete read is still only an observation.','    const bool rebuiltAlready =');
 const original=extract(inert,'inputHostRecreatePreparedTarget').code,a=original.indexOf('        // The intent/power/exit may change'),b=original.indexOf('            earlyOut = true;',a);if(a<0||b<=a)throw Error('Cancellation recreate anchor');const altered=original.slice(0,a)+'        if (!g_inputHostProcess || !g_inputHostHello || g_inputHostTransportFault) {'+(original.includes('\r\n')?'\r\n':'\n')+original.slice(b);inert=inert.replace(original,altered);
 removeRange('inputHostSubmitPad','    // OFF revokes normal BUS publication immediately,','    const bool powerAdmitted =');
 const actual=extract(inert,'inputHostFrameSubmissionCurrent').code;const newline=actual.includes('\r\n')?'\r\n':'\n';const old='    const bool ready=g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled &&'+newline+'        g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire) &&'+newline+'        g_inputReady.load';if(actual.split(old).length!==2)throw Error('Cancellation frame-ready anchor');inert=inert.replace(actual,actual.replace(old,'    const bool ready=g_inputReady.load'));
 substitutions.push({symbol:'inputHostPerformPsWakeRearm/inputHostRecreatePreparedTarget/inputHostSubmitPad/inputHostFrameSubmissionCurrent',negativeControl:true,scope:'Only revert the four new cancellation admission guards; actual full native worker/transport and managed session remain executed. No device or normal Main.'});
}
if(mutation==='rollback-request-ownership'){
 const newline='\n';
 const replace=(symbol,from,to)=>{const original=extract(inert,symbol).code;inert=inert.replace(original,replaceUniqueSourceText(original,from,to,'Request-ownership negative anchor:'+symbol));};
 replace('inputHostFrameSubmissionCurrent','        g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire) &&'+newline,'');
 replace('inputHostRecreatePreparedTarget','        else if (!g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire))'+newline+'            admissionRefusal = "requested-persona-not-bound";'+newline,'');
 const old=extract(inert,'inputHostSubmitPad').code;const a=old.indexOf('    // A fresh frame for the new request must not be relabeled'),b=old.indexOf('    const bool powerAdmitted =',a);if(a<0||b<=a)throw Error('Request-ownership frame-entry anchor');inert=inert.replace(old,old.slice(0,a)+old.slice(b));
 replace('inputHostPerformPsWakeRearm','            g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId != queryRequestId ||'+newline+'            inputHostEpochToken() != queryHostEpoch ||'+newline,'');
 substitutions.push({symbol:'inputHostPerformPsWakeRearm/inputHostRecreatePreparedTarget/inputHostSubmitPad/inputHostFrameSubmissionCurrent',negativeControl:true,scope:'Only bypass new requested-vs-bound target and query request/Host ownership checks. Prior OFF/exit/power fences remain active; actual settings, queue, native pipe and managed child execute.'});
}

if(mutation==='rollback-ps-endpoint-ownership'){
 const original=extract(inert,'psWakeProbeOwnEndpoint').code,a=original.indexOf('        // A marker names a driver/profile'),b=original.indexOf('        if(owned){',a);if(a<0||b<=a)throw Error('PS root-ownership rollback anchor');
 const legacy='        std::wstring legacy=path;for(auto& ch:legacy)ch=(wchar_t)towupper(ch);\n        const bool marker=legacy.find(L"HIDMAESTRO")!=std::wstring::npos||legacy.find(L"HM-CTL")!=std::wstring::npos||legacy.find(L"VIGEM")!=std::wstring::npos;\n        const bool owned=marker&&legacy.find(L"VID_054C")!=std::wstring::npos&&legacy.find(L"PID_0DF2")!=std::wstring::npos;\n        std::wstring instanceId;\n';
 inert=inert.replace(original,original.slice(0,a)+legacy+original.slice(b));substitutions.push({symbol:'psWakeProbeOwnEndpoint',negativeControl:true,scope:'Only replace SDK-root CM ancestry verdict with original profile-marker matching; completeness, session, request, target generation and managed pipe remain unchanged.'});
}
if(mutation==='rollback-ps-binding-revision'){
 const original=extract(inert,'inputHostPerformPsWakeRearm').code,a=original.indexOf('    // A replacement of the same intended target'),b=original.indexOf('    const bool rebuiltAlready =',a);if(a<0||b<=a)throw Error('PS target revision rollback anchor');inert=inert.replace(original,original.slice(0,a)+original.slice(b));substitutions.push({symbol:'inputHostPerformPsWakeRearm',negativeControl:true,scope:'Only bypass per-target binding revision and same-intent bounded fresh-read after slow query; request/Host/power checks and exact endpoint ownership remain.'});
}
if(mutation==='rollback-switch-return-rearm'){
 const anchor='const bool switchRecoveryRearmPreserved=inputHostSwitchRecoveryReturnedLocked(retiredHostEpoch,retiredRunId);';
 if(inert.split(anchor).length!==2)throw Error('Switch return rearm mutation anchor');
 inert=inert.replace(anchor,'const bool switchRecoveryRearmPreserved=false;');
 substitutions.push({symbol:'inputHostReconcileQuarantineOnLifecycleThread',negativeControl:true,scope:'Only omit exact same-intent rearm after actual proven quarantine return; original assertions unchanged.'});
}
if(mutation==='rollback-switch-fault-owner'){
 const anchor='} else if (reason.rfind("fault-stop:frame-receipt-fault:",0)==0 &&';
 if(inert.split(anchor).length!==2)throw Error('Switch fault owner mutation anchor');
 inert=inert.replace(anchor,'} else if (false && reason.rfind("fault-stop:frame-receipt-fault:",0)==0 &&');
 substitutions.push({symbol:'inputHostRunQueuedLifecycleWork',negativeControl:true,scope:'Only bypass fault-cleanup coalescing and transfer to admitted recovery; original assertions unchanged.'});
}
if(mutation==='rollback-pipe-stage-budget'){
 const original=extract(inert,'inputHostRequestPreparedLocked').code;
 const anchor='const auto executionDeadline=inputHostRequestHasExecutionDeadline(command)';
 if(original.split(anchor).length!==2)throw Error('Pipe deadline rollback anchor');
 const altered=original.replace(anchor,'const auto executionDeadline=false&&inputHostRequestHasExecutionDeadline(command)');
 inert=inert.replace(original,altered);substitutions.push({symbol:'inputHostRequestPreparedLocked',negativeControl:true,scope:'Only remove per-stage original-period budget; existing entry clamp, actual native pipe/Host, ownership, parser and all cases unchanged.'});
}
if(mutation==='rollback-initial-start-budget'){
 const original=extract(inert,'inputHostStart').code;
 const anchor='if(ticket&&view.requestId==ticket&&view.locked){';
 if(original.split(anchor).length!==2)throw Error('Initial Start period rollback anchor');
 inert=inert.replace(original,original.replace(anchor,'if(false&&ticket&&view.requestId==ticket&&view.locked){'));
 substitutions.push({symbol:'inputHostStart',negativeControl:true,scope:'Only omit normal Start attachment to existing player deadline; per-pipe-stage budget, inherited wake/fallback scopes, actual native/managed IO and original cases remain.'});
}
if(mutation==='rollback-wake-return-continuation'){
 const anchor='const bool wakeExecutionReturnPreserved=inputHostWakeReturnedLocked(retiredHostEpoch,retiredRunId);';
 if(inert.split(anchor).length!==2)throw Error('Wake return rollback anchor');
 inert=inert.replace(anchor,'const bool wakeExecutionReturnPreserved=false;');
 substitutions.push({symbol:'inputHostReconcileQuarantineOnLifecycleThread',negativeControl:true,scope:'Only omit original wake continuation after exact common quarantine return; actual Stop/restore/clear, original wake/source cases and real child sessions unchanged.'});
}
// Restore exactly one proven OFF defect in actual production bodies.
if(mutation==='rollback-stale-off-direction'){
 const original=extract(inert,'inputHostRunQueuedLifecycleWork').code;
 const altered=replaceUniqueSourceText(original,'if(reason=="virtual-target-disabled" && g_realStickTest.virtualTargetEnabled &&','if(false && reason=="virtual-target-disabled" && g_realStickTest.virtualTargetEnabled &&',mutation);
 inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostRunQueuedLifecycleWork',negativeControl:true,scope:'Only restore stale OFF promotion to later ON; same real dispatcher, queued work, player periods and actual managed Host.'});
}
if(mutation==='rollback-off-return-debt'||mutation==='rollback-off-start-blocked'){
 const original=extract(inert,'inputHostDisabledCleanupProvenLocked').code;
 const altered=mutation==='rollback-off-return-debt'
  ?replaceUniqueSourceText(original,'&& msiReturnProven;','&& true;',mutation)
  :replaceUniqueSourceText(original,'return !g_realStickTest.virtualTargetEnabled','return !g_inputHostStartBlocked && !g_realStickTest.virtualTargetEnabled',mutation);
 inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostDisabledCleanupProvenLocked',negativeControl:true,scope:mutation==='rollback-off-return-debt'?'Only omit MSI physical return debt from shared Stop/final proof; no synthetic discharge.':'Only restore future Start prohibition as an OFF ownership debt; original quarantine/restore owner and retained flag unchanged.'});
}
if(mutation==='rollback-prepare-claim'){
 const original=extract(inert,'inputHostStopLocked').code;const altered=replaceUniqueSourceText(original,'const bool targetMayExist = hadPreparedTarget || msiSdPinnedOwnership || prepareCreationPossible;','const bool targetMayExist = hadPreparedTarget || msiSdPinnedOwnership;',mutation);
 inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostStopLocked',negativeControl:true,scope:'Only omit uncertain PREPARE creation from actual Stop target ownership; actual held managed target, original real pipe and assertions unchanged.'});
}
if(mutation==='rollback-unsent-prepare-claim'){
 const original=extract(inert,'inputHostStart').code;const anchor='    const bool prepareSent = prepareAttempted && inputHostRequestLocked(';const altered=replaceUniqueSourceText(original,anchor,'    if(prepareAttempted)g_inputHostPrepareMayHaveReachedHost=true;\n'+anchor,mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostStart',negativeControl:true,scope:'Only restore pre-request creation claim before the write boundary; original actual pipe deadline denial and assertions unchanged.'});
}
if(mutation==='rollback-live-explicit-restore'){
 const original=extract(inert,'inputHostStopLocked').code;const altered=replaceUniqueSourceText(original,'const bool explicitRestoreAfterExit = hostExitConfirmed && inputHostStopRestoresVisibilityWithoutProof(reason);','const bool explicitRestoreAfterExit = inputHostStopRestoresVisibilityWithoutProof(reason);',mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostStopLocked',negativeControl:true,scope:'Only restore explicit OFF/exit override while exact old child is still alive; actual held managed target and original OFF/return assertions unchanged.'});
}
if(mutation==='rollback-ps-return-xinput'){
 const original=extract(inert,'inputHostReconcileQuarantineOnLifecycleThread').code;const altered=replaceUniqueSourceText(original,': psReturnRequired ? psRetiredReturnProvenLocked()',': false ? psRetiredReturnProvenLocked()',mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostReconcileQuarantineOnLifecycleThread',negativeControl:true,scope:'Only restore XInput-plane substitution for retired PS PnP proof; actual accepted old pin, managed disposal and controlled Present/Unknown/Absent reader remain.'});
}
if(mutation==='rollback-exit-final-sweep'){
 const original=extract(inert,'hidHideSweepAllHiddenDevices').code;const altered=replaceUniqueSourceText(original,'const bool guardedExitSweep = reason && std::strcmp(reason,"app-exit")==0;','const bool guardedExitSweep = false && reason && std::strcmp(reason,"app-exit")==0;',mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'hidHideSweepAllHiddenDevices',negativeControl:true,scope:'Only bypass app-exit ownership admission; real held child, PS return state and existing sweep API consumer stay executed with controlled driver leaves.'});
}
// Directed semantic controls remove exactly one actual runtime duty hook.
// Real Session/store, pipe consumer and all original assertions stay executed.
if(['rollback-target-reservation','rollback-target-pin-store','rollback-target-settlement'].includes(mutation)){
 const symbol=mutation==='rollback-target-reservation'?'inputHostTargetReturnBeforeCreateLocked':mutation==='rollback-target-pin-store'?'inputHostTargetReturnAcceptPsLocked':'inputHostTargetReturnTargetExitedLocked';
 const original=extract(inert,symbol).code,brace=original.indexOf('{');
 const altered=original.slice(0,brace+1)+'\n    '+(mutation==='rollback-target-reservation'?'return true;':'return;')+' // explicit compiled semantic negative\n'+original.slice(brace+1);
 inert=replaceUniqueSourceText(inert,original,altered,mutation);
 substitutions.push({symbol,negativeControl:true,scope:'Only bypass this selected target duty hook in private compiled TU; actual IO/receipts/ownership and assertions unchanged',beforeSha256:sha(original),afterSha256:sha(altered)});
}
// Directed semantic negative: alter only the private compiled TU after a real
// charged recovery. Raw product source and caller/UI/transport logic stay pinned.
if(mutation==='rollback-focus-neutral'){
 const original=extract(inert,'inputHostSubmitPad').code;
 const altered=replaceUniqueSourceText(original,
  'semanticNeutral ? focusIsolationNeutralContent() : frameContent;',
  '(semanticNeutral && !g_inputHostRecoveryEpisodeAttempts.load(std::memory_order_acquire)) ? focusIsolationNeutralContent() : frameContent;',mutation);
 inert=replaceUniqueSourceText(inert,original,altered,mutation+'-submit');
 substitutions.push({symbol:'inputHostSubmitPad/final-semantic-neutral-after-recovery',negativeControl:true,
  beforeSha256:sha(original),afterSha256:sha(altered),scope:'Only final report content neutralization is removed after the actual recovery charge in temporary main.inert.cpp; original tick, wire observation, request, budget and assertions unchanged.'});
}
if(source.includes('static bool gamepadSerialExitOwnershipReleased() noexcept'))inert='#define YMCC_FIXTURE_HAS_SERIAL_EXIT_OWNER 1\n'+inert;
if(selectedCase==='serial-exit-owner' && source.includes('static bool gamepadSerialExitOwnershipReleased() noexcept')){
 inert='#define YMCC_FIXTURE_SERIAL_EXIT_PROOF 1\n'+inert;
 inert=replaceUniqueSourceText(inert,'const bool closing=g_gamepadSerialStopping||g_exitRequested.load(std::memory_order_acquire);','const bool closing=g_gamepadSerialStopping||g_exitRequested.load(std::memory_order_acquire);fixtureSerialAfterClosingRead();','controlled preemption immediately after cached exit read');
 substitutions.push({symbol:'serial-after-closing-read-preemption',scope:'Private scheduling leaf only; actual exit CAS, queue pop, worker, Stop and kernel wait remain production'});
}
if(['serial-power-action','serial-config-action'].includes(selectedCase)){
 if(source.includes('static bool gamepadSerialExitOwnershipReleased() noexcept'))inert='#define YMCC_FIXTURE_SERIAL_EXIT_PROOF 1\n'+inert;
 if(source.includes('unsigned long long actionRevision=0;'))inert='#define YMCC_FIXTURE_ACTION_CONFIGURATION 1\n'+inert;
 if(source.includes('static bool gamepadSerialSubmitCurrentPower('))inert='#define YMCC_FIXTURE_CURRENT_POWER_ACTION 1\n'+inert;
 const shell=extract(inert,'runKillBat').code;
 inert=replaceUniqueSourceText(inert,shell,'static void runKillBat() { ++fixtureSerialKillCommands; }','owned shell launch leaf is a counter only');
 substitutions.push({symbol:'runKillBat',scope:'Only shell/process external endpoint replaced; actual enqueue/worker/current-power guard remain production',originalSha256:sha(shell)});
}
if(selectedCase==='serial-entry-diagnostic'){
 const endpoint='throwLogEvent=nullptr;throw std::runtime_error("offline-final-log-exception");';
 inert=replaceUniqueSourceText(inert,endpoint,'throwLogEvent=nullptr;fixtureSerialEntryInstallTerminateObserver();throw std::runtime_error("offline-final-log-exception");','worker-local terminate observation at the existing diagnostic failure leaf');
 substitutions.push({symbol:'worker-local-terminate-observer',scope:'Only install the private observer on the actual throwing thread before the same diagnostic exception. No business guard or worker catch changed.'});
}
// Original Native/public permission consumers remain actual bodies in the
// same coordinator TU. The parent pairs Request/hardware guards separately;
// this list prevents a default or a rollback from quietly substituting them.
const sharedPowerCoreSymbols=Object.freeze([
 'readSharedSystemPower','captureSharedPowerLifecycle','sharedPowerLifecycleCurrent',
 'observeSharedPowerRevocation','revokeSharedPowerMirrors','closeSharedPowerMirrors',
 'inputHostFrameSharedPowerCurrent','inputHostReceiptPassedSequenceGate',
 'inputHostRequestNeedsPowerScope','inputHostRequestNeedsHardwarePower','inputHostRequestPowerReceiptCurrent'
]);
const sharedPowerHeaderInputs=Object.freeze([
 'native/input_host_shared_power.h','native/sleep_power_state.h',
 'native/sleep_power_observation.h','native/sleep_power_revocation.h',
 'native/msi_power_rearm_work_runtime.h'
]).map(relative=>{
 const file=path.join(snapshot,relative),bytes=fs.readFileSync(file);
 const recorded=records.filter(row=>row.path===relative);
 if(recorded.length!==1||recorded[0].sha256!==sha(bytes))throw Error('SHARED_POWER_PAIRED_HEADER_INPUT_REQUIRED:'+relative);
 return {path:relative,file,bytes:bytes.length,sha256:sha(bytes),meaning:'Frozen actual header input, not runtime or compiler-observation acceptance'};
});
fs.writeFileSync(path.join(out,'shared-power-paired-inputs.json'),JSON.stringify({schemaVersion:1,
 source:'native/main.cpp',sourceSha256:sha(fs.readFileSync(path.join(snapshot,'native/main.cpp'))),
 expandedSourceSha256:sha(source),coreSymbols:sharedPowerCoreSymbols,
 headers:sharedPowerHeaderInputs,secondPowerAuthority:false,permissionReplacementAdded:false},null,2),{flag:'wx'});
const core=[...sharedPowerCoreSymbols,...(['serial-power-action','serial-config-action'].includes(selectedCase)?['gamepadSerialSubmit','nativeYmccShortcutSubmit',...(source.includes('static bool gamepadSerialSubmitCurrentPower(')?['gamepadSerialSubmitCurrentPower']:[])]:[]),...(gpdPersonaMutexSelected?['inputHostConfiguredPersona']:[]),'inputHostOneXAwaitPreHideSource','inputHostOneXStartCurrent','inputHostOneXStartMayIssue','inputHostOneXPostHideBeforeTarget','inputHostOneXGameSource','hidHidePeriodicStateViaCli','hidHideCapturePeriodicVisibilityState','hidHideParseCommandPaths','inputHostReconcilePhysicalSourceOnLifecycleThread','hidHideReapplyReconnectedSource','hidHideMaintainMsiAuxiliaryVisibility','hidHideAppendOwnedReconnectTargets','inputHostCaptureOwnedVisibilityReadbackLocked','inputHostOwnedVisibilityContractCurrentLocked','inputHostPublishOwnedVisibilityReadback','inputHostDeferredPhysicalHideCurrentReason','inputHostDeferredPhysicalHideRefuse','inputHostDeferredPhysicalHideArm','inputHostDeferredPhysicalHideCurrent','inputHostDeferredPhysicalHideIntentCurrent','inputHostDeferredPhysicalHideWriteCurrent','inputHostDeferredPhysicalHideCliCurrent','inputHostDeferredPhysicalHideSourceReason','inputHostDeferredPhysicalHideSame','inputHostDeferredPhysicalHideWaitSource','inputHostDeferredPhysicalHideResumeSourceReady','inputHostDeferredPhysicalHideSourceResumed','inputHostDeferredPhysicalHideConfigurationLocked','inputHostDeferredPhysicalHideRogRefreshPolicyApplies','inputHostDeferredPhysicalHideReserveRefreshIssue','inputHostDeferredPhysicalHideRetireForRecoveryLocked','ymStartRecoverRogPendingIdentity','inputHostCompleteRogPartialHideLocked','inputHostRogPartialHideSourcesCurrent','inputHostRogPartialHideTargetsCurrent','inputHostRogPartialHideConfigurationCurrent','inputHostDeferredPhysicalHideRefreshLocked','inputHostFrameSubmissionRequestCurrent','inputHostFrameSubmissionMayIssue','inputHostLateHideEnqueueEpisode','inputHostLateHidePendingRead','inputHostLateHidePendingCommit','inputHostLateHidePendingClearIfToken','inputHostLateHideMarkEpisodeCompleted','inputHostLateHideEpisodeAlreadyCompleted','inputHostLateHideManagedMsiRouteType','inputHostLateHideExecDecisionFor','inputHostLateHideInFlightSet','inputHostLateHideStateSnapshotForTest','inputHostLateHideStateRestoreForTest','inputHostQueueLifecycleWorkInternal','inputHostTargetReturnAcceptedRecreateLocked','inputHostTargetReturnAfterPsQuarantineLocked','hidHideCliReapPendingLocked','inputHostTargetReturnBeforeCreateLocked','inputHostTargetReturnAcceptPsLocked','inputHostTargetReturnTargetExitedLocked','inputHostTargetReturnBeforeHideLocked','inputHostTargetReturnAfterPhysicalRestoreLocked','inputHostApplyHelloCapabilities','inputHostRequestPreparedLocked','inputHostRequestHasExecutionDeadline','psWakeCaptureEndpointOwnership','inputHostRefreshPersonaMatchLocked','inputHostSetBoundPersona','inputHostClearBoundPersona','inputHostXboxPreparedHid','inputHostXboxPreparedRoot','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked','inputHostRequestPsWakeRearm','inputHostConsumePsWakeRearm','inputHostPerformPsWakeRearm','psWakeProbeOwnEndpoint','psWakeDecide','inputHostRecreatePreparedTarget','inputHostRequestWakeRescan','inputHostConsumeWakeRescan','inputHostPerformWakeHotReplace','inputHostWakePhysicalHandshakeReady','sgShouldKeepInputDuringStandby','inputHostRequestPowerRelease','inputHostRequestPowerRearm','inputHostConsumePowerRequests','inputCaptureSuspendSensorsForPower','inputCaptureResumeSensorsForPower','inputHostSubmitPad','inputHostFrameSubmissionCurrent','inputHostPhysicalPublicationSourceAdmitted','inputHostFreshNeutralReady','inputHostStart','inputHostStopLocked','inputHostStop','inputHostClearLocked','inputHostRunQueuedLifecycleWork','inputHostReconcileQuarantine','inputHostReconcileQuarantineOnLifecycleThread','msiSteamDeckRecheckOwnedRemovalDebt','ymStartPipeSeamServe','ymStartEnsureRoute','msiClawOpenModeCommand','msiDinputSubmitModeWriteIntent','msiStartOrderEntrySelfTest','inputHostTargetAdmission','inputHostStartProtectionFinish','nativeInputShortcutToggle','clearInputShortcutRuntime','inputCaptureRefreshRealStickSettings','inputHostTargetReturnHistorySettledLocked','inputHostTargetReturnColdPassLocked','inputHostTargetReturnQueueColdStartup'].map(name=>{const actual=extract(source,name).code;const same=extract(inert,name).code===actual;if(!same&&!['ymStartPipeSeamServe','psWakeProbeOwnEndpoint','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked'].includes(name)&&!(mutation==='rollback-recovery-inheritance'&&['inputHostStart','inputCaptureRefreshRealStickSettings'].includes(name))&&!(['rollback-cancellation-guards','rollback-request-ownership'].includes(mutation)&&['inputHostPerformPsWakeRearm','inputHostRecreatePreparedTarget','inputHostSubmitPad','inputHostFrameSubmissionCurrent'].includes(name))&&!(mutation==='rollback-ps-binding-revision'&&name==='inputHostPerformPsWakeRearm')&&!(mutation==='rollback-switch-return-rearm'&&name==='inputHostReconcileQuarantineOnLifecycleThread')&&!(mutation==='rollback-switch-fault-owner'&&name==='inputHostRunQueuedLifecycleWork')&&!(mutation==='rollback-stale-off-direction'&&name==='inputHostRunQueuedLifecycleWork')&&!(mutation==='rollback-prepare-claim'&&name==='inputHostStopLocked')&&!(mutation==='rollback-live-explicit-restore'&&name==='inputHostStopLocked')&&!(mutation==='rollback-ps-return-xinput'&&name==='inputHostReconcileQuarantineOnLifecycleThread')&&!(mutation==='rollback-unsent-prepare-claim'&&name==='inputHostStart')&&!(mutation==='rollback-pipe-stage-budget'&&name==='inputHostRequestPreparedLocked')&&!(mutation==='rollback-initial-start-budget'&&name==='inputHostStart')&&!(mutation==='rollback-wake-return-continuation'&&name==='inputHostReconcileQuarantineOnLifecycleThread')&&!(mutation==='rollback-target-reservation'&&name==='inputHostTargetReturnBeforeCreateLocked')&&!(mutation==='rollback-target-pin-store'&&name==='inputHostTargetReturnAcceptPsLocked')&&!(mutation==='rollback-target-settlement'&&name==='inputHostTargetReturnTargetExitedLocked')&&!(mutation==='rollback-focus-neutral'&&name==='inputHostSubmitPad'))throw Error('core modified '+name);return {symbol:name,sha256:sha(actual),executedSha256:sha(extract(inert,name).code),unchanged:same,fixtureEndpointAdapter:['ymStartPipeSeamServe','psWakeProbeOwnEndpoint','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked'].includes(name),expectedSemanticNegative:!!mutation&&!same&&(mutation==='rollback-ps-endpoint-ownership'?name==='psWakeProbeOwnEndpoint':!['ymStartPipeSeamServe','psWakeProbeOwnEndpoint','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked'].includes(name))};});

// All runtime incident/return decisions are the actual expanded production
// bodies. Neither parent return helper is an OS adapter or a test substitute.
for(const [relative,names]of [
 ['native/virtual_runtime_recovery_runtime.h',['inputHostRuntimeRecoveryOfferLocked','inputHostRuntimeRecoveryMaintenanceLocked','inputHostPerformRuntimeRecovery','inputHostConsumeRuntimeFaultCleanup']],
 ['native/virtual_target_return_runtime.h',['inputHostTargetReturnAfterRuntimeQuarantineLocked','inputHostTargetReturnRuntimeSettledLocked',...(source.includes('static bool inputHostTargetReturnRuntimeQuarantineProofLocked(')?['inputHostTargetReturnRuntimeQuarantineProofLocked','inputHostTargetReturnBeforeRuntimeRestoreLocked']:[]),...(source.includes('static bool inputHostTargetReturnPsLifetimeProvenLocked(')?['inputHostTargetReturnPsLifetimeProvenLocked']:[])]]
]){
 const raw=fs.readFileSync(path.join(snapshot,relative),'utf8');
 for(const name of names){
  const actual=extract(raw,name).code,expandedBody=extract(source,name).code,executed=extract(inert,name).code;
  if(actual!==expandedBody || actual!==executed)throw Error('RUNTIME_RECOVERY_REAL_CONSUMER_BODY_MODIFIED:'+name);
  if(core.some(row=>row.symbol===name))throw Error('duplicate core '+name);
  core.push({symbol:name,source:relative,sha256:sha(actual),executedSha256:sha(executed),unchanged:true,sourceExpanded:true,fixtureEndpointAdapter:false,expectedSemanticNegative:false});
 }
}
// PRODUCT_CANDIDATE_CORE_AUDIT_BEGIN
if(sourceWakeSelected){
 const name='xinputReconfirmSelectedCollectionSource';
 if(source.includes('static bool '+name+'(')){
  const actual=extract(source,name).code,executed=extract(inert,name).code;
  if(actual!==executed)throw Error('SOURCE_WAKE_COLLECTION_HELPER_MODIFIED');
  if(core.some(row=>row.symbol===name))throw Error('SOURCE_WAKE_DUPLICATE_HELPER');
  core.push({symbol:name,source:'native/main.cpp',sha256:sha(actual),executedSha256:sha(executed),unchanged:true,fixtureEndpointAdapter:false,expectedSemanticNegative:false});
 }
}
const candidateCoreAudit=(gpdR4CandidateBinding||gpdPersonaMutexSelected)?[
 ['native/virtual_wake_execution_runtime.h',['inputHostGpdStartRemaining','inputHostGpdPostHideWakeRemaining','gpdRemainingLocked']],
 ['native/msi_native_foreground_isolation_runtime.h',['msiPostHideContextCurrentLocked','msiPostHideExecutionCurrentLocked']]
].map(([relative,names])=>{
 const file=path.join(snapshot,relative),raw=fs.readFileSync(file,'utf8'),expandedHeader=expanded.inputs.some(row=>path.resolve(row.file)===path.resolve(file));
 const include='#include "'+path.basename(relative)+'"';
 if(!expandedHeader&&(source.split(include).length!==2||inert.split(include).length!==2))throw Error('PRODUCT_CANDIDATE_REAL_INCLUDE_REQUIRED:'+relative);
 const functions=names.map(name=>{
  const actual=extract(raw,name).code,executed=expandedHeader?extract(inert,name).code:actual;
  if(expandedHeader&&extract(source,name).code!==actual||actual!==executed)throw Error('PRODUCT_CANDIDATE_CORE_BODY_MODIFIED:'+name);
  if(core.some(row=>row.symbol===name))throw Error('PRODUCT_CANDIDATE_DUPLICATE_CORE:'+name);
  const row={symbol:name,source:relative,sha256:sha(actual),executedSha256:sha(executed),unchanged:true,sourceExpanded:expandedHeader,compilerObservedHeader:!expandedHeader,fixtureEndpointAdapter:false,expectedSemanticNegative:false};core.push(row);return row;
 });
 return {source:relative,sha256:sha(fs.readFileSync(file)),expandedHeader,include:expandedHeader?null:include,functions};
}):null;
function verifyCandidateExecutionSource(executed){
 if(!candidateCoreAudit)return;
 for(const binding of candidateCoreAudit){
  const raw=fs.readFileSync(path.join(snapshot,binding.source),'utf8');
  if(sha(fs.readFileSync(path.join(snapshot,binding.source)))!==binding.sha256)throw Error('PRODUCT_CANDIDATE_EXECUTION_HEADER_DRIFT:'+binding.source);
  if(!binding.expandedHeader&&executed.split(binding.include).length!==2)throw Error('PRODUCT_CANDIDATE_EXECUTION_INCLUDE_CHANGED:'+binding.source);
  for(const row of binding.functions)if(sha(extract(binding.expandedHeader?executed:raw,row.symbol).code)!==row.executedSha256)throw Error('PRODUCT_CANDIDATE_EXECUTION_BODY_CHANGED:'+row.symbol);
 }
}
// PRODUCT_CANDIDATE_CORE_AUDIT_END
if(gpdLiveSdTransferSelected){
 // Original compiled includes: no replacement cold algorithm or successful fake.
 for(const [relative,names]of [
  ['native/msi_native_foreground_isolation_runtime.h',['msiNativeForegroundFacts','msiNativeForegroundHandleWork','msiNativeForegroundBeforeVirtualWork']],
  ['native/native_visibility_cold_runtime.h',['inputHostNativeVisibilityColdReturnLocked','inputHostNativeVisibilityHistoryPending']]
 ]){
  const raw=fs.readFileSync(path.join(snapshot,relative),'utf8');
  for(const name of names){
   if(core.some(row=>row.symbol===name))throw Error('DUPLICATE_GPD_LIVE_SD_CORE:'+name);
   const actual=extract(raw,name).code;
   core.push({symbol:name,source:relative,sha256:sha(actual),executedSha256:sha(actual),unchanged:true,sourceExpanded:false,compiledOriginalHeader:true,fixtureEndpointAdapter:false,expectedSemanticNegative:false});
  }
 }
}
// The continuation header's actual functions (including the native bridge) are
// bound once by the original continuationSource loop below for every selection.
// Bind the real environment as a struct, separate from the function count.
if(oemAfterOffSelected){
 for(const name of ['oemBackMappingRefreshGate','oemKeyboardConsumeEvent','nativeKeyboardShortcutHandleOemEdge','nativeKeyboardShortcutsRefresh','nativeKeyboardShortcutsEvaluate','nativeKeyboardShortcutDispatch','nativeYmccShortcutEmit','nativeYmccShortcutSubmit','gamepadSerialSubmit','gamepadSerialSubmitCurrentPower','gamepadSerialStop']){
  const actual=extract(source,name).code,executed=extract(inert,name).code;if(actual!==executed)throw Error('OEM_AFTER_OFF_CORE_MODIFIED:'+name);
  if(!core.some(row=>row.symbol===name))core.push({symbol:name,sha256:sha(actual),executedSha256:sha(executed),unchanged:true,fixtureEndpointAdapter:false});
 }
 for(const name of ['oemBackMappingReconcileOnUi','oemBackHookProc']){
  const actual=extract(source,name).code,executed=extract(inert,name).code;
  const restored=executed.replaceAll('fixtureOemSetHook(','SetWindowsHookExW(').replaceAll('fixtureOemUnhook(','UnhookWindowsHookEx(').replaceAll('fixtureOemCallNextHook(','CallNextHookEx(');
  if(actual!==restored)throw Error('OEM_AFTER_OFF_HOOK_BODY_MODIFIED:'+name);
  core.push({symbol:name,sha256:sha(actual),executedSha256:sha(executed),unchangedAfterExplicitOsLeafRestoration:true,fixtureEndpointAdapter:true});
 }
}
const coldEnvironment=extract(source,'ColdEnvironment','struct').code;
if(extract(inert,'ColdEnvironment','struct').code!==coldEnvironment)throw Error('core modified ColdEnvironment');
const oneXRuntime=fs.readFileSync(path.join(snapshot,'native/virtual_controller_onex_start_runtime.h'),'utf8');
const oneXOsLeaves=new Set(['inputHostOneXLocations','inputHostOneXPortKind','inputHostOneXSourceSlot','inputHostOneXRootStarted','inputHostOneXResolveSources','inputHostOneXResolveSelectedChildren','inputHostOneXVisibility']);
const oneXRealFunctions=[...oneXRuntime.matchAll(/^static\s+(?:bool|uint64_t|int|std::vector<std::wstring>)\s+(inputHostOneX\w+)\s*\(/gm)].map(m=>m[1]).filter(name=>!oneXOsLeaves.has(name));
for(const name of [...oneXRealFunctions,'inputHostAdmitSelectedPhysicalSource','inputHostHidHideApplyViaPipeImpl','inputHostHidHideApplyViaPipeLocked','gamepadReadStateUnlocked']){
 const actual=extract(source,name).code,executed=extract(inert,name).code;
 if(actual!==executed)throw Error('ONEX_REAL_CONSUMER_BODY_MODIFIED:'+name);
 if(!core.some(row=>row.symbol===name))core.push({symbol:name,sha256:sha(actual),executedSha256:sha(executed),unchanged:true,fixtureEndpointAdapter:false,expectedSemanticNegative:false});
}
if(!oneXRealFunctions.includes('inputHostOneXReturnedSourceCurrent'))throw Error('ONEX_REVIEWED_FRESH_CONSUMER_MISSING');
if(!oneXRealFunctions.includes('inputHostOneXAlignSelectedBeforeCycle'))throw Error('ONEX_REVIEWED_SELECTED_ALIGNMENT_CONSUMER_MISSING');
const structCore=[{symbol:'ColdEnvironment',kind:'struct',source:'native/virtual_target_return_cold_runtime.h',sha256:sha(coldEnvironment),executedSha256:sha(coldEnvironment),unchanged:true}];
// These headers are deliberately not expanded or faked. The full runner must
// observe every one as an actual compiler include, then bind its frozen bytes.
if(gpdR4CandidateBinding||gpdPersonaMutexSelected){
 const relative='native/virtual_wake_execution_runtime.h',raw=fs.readFileSync(path.join(snapshot,relative),'utf8'),scope=extract(raw,'Scope','struct');
 if(!raw.includes('thread_local const Work* active')||!inert.includes('#include "virtual_wake_execution_runtime.h"'))throw Error('PRODUCT_CANDIDATE_ACTUAL_TLS_SCOPE_REQUIRED');
 structCore.push({symbol:'ymcc::inputHost::wakeExecutionRuntime::Scope',kind:'struct',source:relative,sha256:sha(scope.code),executedSha256:sha(scope.code),unchanged:true,compilerObservedHeader:true,startLine:scope.startLine,endLine:scope.endLine});
}
const compilerHeaderBindings=[...(sourceWakeSelected?['native/virtual_wake_execution_runtime.h']:[]),...((gpdR4CandidateBinding||gpdPersonaMutexSelected)?candidateCoreAudit.map(row=>row.source).filter(relative=>!expanded.inputs.some(row=>path.resolve(row.file)===path.resolve(snapshot,relative))):[]),'native/msi_late_hide_pending.h','native/virtual_target_return_cold_execution.h','native/virtual_target_return_pnp_absence.h','native/virtual_target_return_record.h','native/virtual_target_return_session.h','native/virtual_target_return_store.h','native/virtual_switch_execution_return_runtime.h','native/virtual_switch_recovery_runtime.h','native/virtual_switch_verification_runtime.h'].map(source=>({source,sha256:sha(fs.readFileSync(path.join(snapshot,source))),compilerObservationRequired:true}));
const localFaultSource='native/virtual_switch_verification_runtime.h';
const localFaultHeader=fs.readFileSync(path.join(snapshot,localFaultSource),'utf8');
for(const name of ['inputHostSwitchLocalPhysicalSourceCurrentLocked','inputHostSwitchRootRefreshSnapshotLocked','inputHostSwitchRootRefreshOfferCurrentLocked','inputHostSwitchLocalFaultCurrentLocked','inputHostSwitchLocalFaultResolvedLocked']){
 const actual=extract(localFaultHeader,name).code;
 if(core.some(row=>row.symbol===name))throw Error('duplicate core '+name);
 core.push({symbol:name,source:localFaultSource,sha256:sha(actual),executedSha256:sha(actual),unchanged:true,compilerObservedHeader:true,fixtureEndpointAdapter:false,expectedSemanticNegative:false});
}
const rootRecoverySource='native/virtual_switch_recovery_runtime.h';
const rootRecoveryHeader=fs.readFileSync(path.join(snapshot,rootRecoverySource),'utf8');
for(const name of ['inputHostSwitchRecoveryOfferLocked','inputHostSwitchRecoveryMaintenanceLocked','inputHostPerformSwitchRecovery']){
 const actual=extract(rootRecoveryHeader,name).code;
 if(core.some(row=>row.symbol===name))throw Error('duplicate core '+name);
 core.push({symbol:name,source:rootRecoverySource,sha256:sha(actual),executedSha256:sha(actual),unchanged:true,compilerObservedHeader:true,fixtureEndpointAdapter:false,expectedSemanticNegative:false});
}
const continuationSource='native/virtual_switch_execution_return_runtime.h';
const continuation=fs.readFileSync(path.join(snapshot,continuationSource),'utf8');
for(const match of continuation.matchAll(/^static[^\n;{]*?\b([A-Za-z_]\w*)\s*\(/gm)){
 const name=match[1],actual=extract(continuation,name).code;
 if(core.some(row=>row.symbol===name))throw Error('duplicate core '+name);
 core.push({symbol:name,source:continuationSource,sha256:sha(actual),executedSha256:sha(actual),unchanged:true,compilerObservedHeader:true,fixtureEndpointAdapter:false,expectedSemanticNegative:false});
}
const readSource=extract(source,'gamepadReadStateUnlocked').code,discoveryStart=readSource.indexOf('    const bool boundaryFullScan ='),discoveryEnd=readSource.indexOf('    // L3（需求单）',discoveryStart);if(discoveryStart<0||discoveryEnd<=discoveryStart)throw Error('Exact XInput discovery slice anchor');const discoveryCode=readSource.slice(discoveryStart,discoveryEnd),maskStart=readSource.indexOf('    const DWORD physicalMask = xinputPhysicalConnectedMask();',discoveryEnd),maskEnd=readSource.indexOf('    // R10-R2 M3',maskStart);if(maskStart<0||maskEnd<=maskStart)throw Error('Exact XInput selected-source mask slice anchor');const maskCode=readSource.slice(maskStart,maskEnd),discoveryFile=path.join(out,'xinput_discovery.inc');let discoveryBytes=sourceReconfirmSelected?readSource.slice(discoveryStart,maskEnd):discoveryCode+'\n'+maskCode;const discoveryOriginalBytes=discoveryBytes;
if(sourceReconfirmSelected&&source.includes('static bool xinputReconfirmSelectedCollectionSource(')){
 const matches=[...discoveryBytes.matchAll(/\bxinputReconfirmSelectedCollectionSource\(reappeared,\s*currentPhysical(?:,\s*collectionOwner,\s*collectionSelected,\s*collectionPower)?\)/g)];
 if(matches.length!==1)throw Error('SOURCE_ADOPTION_ONE_ACTUAL_HELPER_CALL_REQUIRED');
 const before=matches[0][0],after='fixtureSourceReconfirmObserveProof('+before+')';
 discoveryBytes=discoveryBytes.replace(before,after);
 if(discoveryBytes.replace(after,before)!==discoveryOriginalBytes)throw Error('SOURCE_ADOPTION_OBSERVER_NOT_EXACT_INVERSE');
 substitutions.push({symbol:'gamepadReadStateUnlocked/source-helper-return-observer',observationOnly:true,fixtureSliceOnly:true,actualHelperResultPreserved:true,borrowedGuardsPreserved:true,before,after,reverse:{from:after,to:before},beforeSliceSha256:sha(discoveryOriginalBytes),afterSliceSha256:sha(discoveryBytes)});
}
fs.writeFileSync(discoveryFile,discoveryBytes);const fragments=[{symbol:'gamepadReadStateUnlocked/complete-four-slot-discovery',source:'native/main.cpp',sha256:sha(discoveryCode),file:discoveryFile,fileSha256:sha(discoveryBytes),exact:true,scope:'Actual discovery loop, per-slot identity classification and completed-boundary publication. Only XInput/identity API leaves stay inert; no source-admission flag is forged.'},{symbol:'gamepadReadStateUnlocked/selected-source-publication-mask',source:'native/main.cpp',sha256:sha(maskCode),file:discoveryFile,fileSha256:sha(discoveryBytes),exact:true,scope:'Actual physical/external/selected-source mask decision and publication. Drop/reconfirm remainder and whole BUS are not executed or claimed.'}];
if(sourceReconfirmSelected){
 const dropReconfirmCode=readSource.slice(discoveryEnd,maskStart);
 if(!dropReconfirmCode.includes('g_xinputPrevSampleMask = g_xinputConnectedMask;')||!dropReconfirmCode.includes('gamepad-physical-source-reconfirmed')||!dropReconfirmCode.includes('g_xinputSourceReconfirmMask.store(stillPending'))throw Error('SOURCE_RECONFIRM_COMPLETE_ORIGINAL_FRAGMENT_REQUIRED');
 fragments[1].scope='Actual physical/external/selected-source mask decision and publication following the contiguous original drop/reconfirm segment.';
 fragments.push({symbol:'gamepadReadStateUnlocked/physical-drop-prev-sample-reconfirm',source:'native/main.cpp',sha256:sha(dropReconfirmCode),file:discoveryFile,fileSha256:sha(discoveryBytes),exact:false,reversibleHelperReturnObserverOnly:true,originalContiguousSha256:sha(discoveryOriginalBytes),sourceRange:{start:discoveryEnd,end:maskStart},contiguousRange:{start:discoveryStart,end:maskEnd},scope:'Exact original L3 drop, previous-sample update and reconfirm segment; original predicates retained. Only the explicit source-reconfirm-collection selection includes this segment.'});
}
if(suite==='worker-cost'){
 const guard='#if defined(YMCC_MSI_USB_NAME_ORIGIN_DIAGNOSTIC) && defined(YMCC_MSI_VISIBILITY_DIAGNOSTIC) && !defined(YMCC_FAULT_INJECT) && !defined(YMCC_MSI_FOREGROUND_ISOLATION_TEST)';
 const selected='#if defined(YMCC_MSI_USB_NAME_ORIGIN_DIAGNOSTIC) && defined(YMCC_MSI_VISIBILITY_DIAGNOSTIC) && (defined(YMCC_WORKER_COST_PRODUCTION_METADATA) || (!defined(YMCC_FAULT_INJECT) && !defined(YMCC_MSI_FOREGROUND_ISOLATION_TEST)))';
 inert=replaceUniqueSourceText(inert,guard,selected,'worker-cost exact production metadata selection');
 substitutions.push({symbol:'msiUsbNameContext production branch selection',scope:'Test-only preprocessor selection; original body and real ContextPublisher stay unchanged. Own-process private mapping allowed, original device/SDK/helper-process isolation preserved.',beforeSha256:sha(guard),afterSha256:sha(selected)});
}
if(deniedTouchpadInputSelected){
 const {bindCurrentTouchpadInputFixturePort}=await import('./input_coordinator_current_compile_only.mjs');
 const denied=bindCurrentTouchpadInputFixturePort(inert,{nativeSourceProfile:'current105-terminal26-current-capture-20261117a'});
 inert=denied.source;substitutions.push(...denied.substitutions);
}
if(prepareRejectionSelected){
 const capture=extract(source,'inputCaptureOnGamepad').code;
 const begin='    // Explicit user intent: a fresh virtual-target ON edge lifts the backend';
 const end='    preambleFrameSetupCost.finish();';
 if(capture.split(begin).length!==2||capture.split(end).length!==2||capture.indexOf(begin)>=capture.indexOf(end))throw Error('PREPARE_REJECTION_ON_EDGE_SOURCE_CHANGED');
 const edge=capture.slice(capture.indexOf(begin),capture.indexOf(end));
 if(!edge.includes('lastVirtualTargetWanted = wanted;')||inert.split(edge).length!==2)throw Error('PREPARE_REJECTION_ON_EDGE_NOT_EXACT');
 fs.writeFileSync(path.join(out,'prepare_rejection_on_edge.inc'),'static void fixturePrepareOriginalOnEdge(){\n'+edge+'}\n',{flag:'wx'});
 const scenario=path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/prepare_rejection_cases.inc');
 fs.copyFileSync(scenario,path.join(out,'prepare_rejection_cases.inc'),fs.constants.COPYFILE_EXCL);
 substitutions.push({symbol:'inputCaptureOnGamepad/explicit OFF-ON callback',sha256:sha(edge),copiedUnchanged:true});
}
fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);const settingsMutexIsolation=isolateCurrentFixtureSettingsMutex(fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8'),suite==='input-application'?{runtimeRoot:path.join(out,'input-application'),moduleMaintenance:true}:(wakeKeptSourceSelected||gpdExpiredFirstOnFullCycleSelected)?{runtimeRoot:path.join(out,'full-pipe-core'),moduleMaintenance:true}:(suite==='lifecycle-directed'||suite==='target-return')?{runtimeRoot:path.join(out,'target-return'),moduleMaintenance:true}:suite==='target-return-family'?{runtimeRoot:path.join(out,'target-return-family'),moduleMaintenance:true}:suite==='family-lifecycle'?{runtimeRoot:gpdPreHideSelected?gpdPreHideRuntimeRoot:path.join(out,'family-lifecycle'),moduleMaintenance:true,gpdPreHideBatchCount:gpdPreHideBatch?.length}:suite==='persona-lifecycle'?{runtimeRoot:path.join(out,'persona-lifecycle'),moduleMaintenance:true}:suite==='switch-recovery'?{runtimeRoot:path.join(out,'switch-recovery'),moduleMaintenance:true}:suite==='tuple-protection'?{runtimeRoot:path.join(out,'tuple-protection'),moduleMaintenance:true}:suite==='off-lifecycle'?{runtimeRoot:sdOffSourceLostSelected?path.join(out,'off-lifecycle',sdOffSelection.selectedVariants[0]):path.join(out,'off-lifecycle'),moduleMaintenance:true}:['late-hide-pending','late-hide-current'].includes(suite)?{runtimeRoot:path.join(out,'late-hide-pending'),moduleMaintenance:true}:managedDll&&!suite?{runtimeRoot:path.join(out,'full-pipe'),moduleMaintenance:true}:undefined);
if(oemAfterOffSelected){
 const before='static HANDLE fixturePrivateSettingsMutex(LPSECURITY_ATTRIBUTES attributes,BOOL initialOwner,LPCWSTR requestedName) {';
 const after=before+String.raw`
    if(!attributes && !initialOwner && requestedName && std::wstring(requestedName)==L"Local\\YeManCC.ModelAdaptationLog.v1") {
        ++fixtureOemJournalMutexRequests;
        const auto journalName=fixturePrivateSettingsMutexName()+L"-ModelAdaptation";
        return ::CreateMutexW(nullptr,FALSE,journalName.c_str());
    }
`;
 settingsMutexIsolation.text=replaceUniqueSourceText(settingsMutexIsolation.text,before,after,'OEM actual journal private named mutex only');
 settingsMutexIsolation.edits.push({before,after});settingsMutexIsolation.isolatedSha256=sha(settingsMutexIsolation.text);
 substitutions.push({symbol:'OEM-model-adaptation-journal-mutex',scope:'Only exact existing log mutex maps to same verified private directory identity; real Journal and OS mutex execute; unknown names still rejected',beforeSha256:sha(before),afterSha256:sha(after)});
}
if(reuseSuite){
 // Both original consumers share this exact compiler output. Admit only their
 // two explicitly selected private roots, each checked by the original isolator.
 const originalFixture=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8');
 const extraRoot=path.join(out,reuseSuite),extra=isolateCurrentFixtureSettingsMutex(originalFixture,{runtimeRoot:extraRoot,moduleMaintenance:true});
 const oneCondition=text=>{const matches=text.match(/root!=normalizeWindowsPathForCompare\(L"(?:\\.|[^"\\])*"\)/g)??[];
  if(matches.length!==1)throw Error('EXACT_ONE_PRIVATE_RUNTIME_ROOT_REQUIRED');return matches[0];};
 const first=oneCondition(settingsMutexIsolation.text),second=oneCondition(extra.text),combined='('+first+' && '+second+')';
 const merged=replaceUniqueSourceText(settingsMutexIsolation.text,first,combined,'two-exact-selected-private-roots');
 settingsMutexIsolation.edits.push({before:first,after:combined});
 let restored=merged;for(const edit of [...settingsMutexIsolation.edits].reverse())restored=replaceUniqueSourceText(restored,edit.after,edit.before,'restore-original-private-boundary');
 if(restored!==originalFixture)throw Error('PRIVATE_PAIR_RESTORATION_FAILED');
 settingsMutexIsolation.text=merged;settingsMutexIsolation.isolatedSha256=sha(merged);
 settingsMutexIsolation.additionalRuntimeRoots=[{root:extraRoot,source:'existing-explicit-reuse-suite',originalIsolatedSha256:extra.isolatedSha256}];
 settingsMutexIsolation.scope+=' Explicit native pair only: both selected roots are independently validated; all other directories remain refused.';
}


if(sdOffSourceLostSelected){
 const originalFixture=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8');
 const parameterSource=path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/off_lifecycle_cases.inc'),parameterBytes=fs.readFileSync(parameterSource);
 const declared=readSdOffSelection(parameterBytes.toString('utf8'));
 if(JSON.stringify(declared)!==JSON.stringify(sdOffSelection)||sdOffSelection.selectedVariants.length!==1||sdOffSelection.variants.length!==21||new Set(sdOffSelection.variants).size!==21)throw Error('SD_OFF_EXACT_SOURCE_DECLARED_ROOTS_REQUIRED');
 const oneCondition=text=>{const matches=text.match(/root!=normalizeWindowsPathForCompare\(L"(?:\\.|[^"\\])*"\)/g)??[];if(matches.length!==1)throw Error('EXACT_ONE_PRIVATE_RUNTIME_ROOT_REQUIRED');return matches[0];};
 const first=oneCondition(settingsMutexIsolation.text),roots=sdOffSelection.variants.map(variant=>{
  if(!/^[A-Za-z0-9][A-Za-z0-9-]*$/.test(variant))throw Error('SD_OFF_EXACT_VARIANT_DIRECTORY_REQUIRED');
  const root=path.join(out,'off-lifecycle',variant),bound=isolateCurrentFixtureSettingsMutex(originalFixture,{runtimeRoot:root,moduleMaintenance:true});
  return {variant,root,condition:oneCondition(bound.text),originalIsolatedSha256:bound.isolatedSha256};
 });
 if(new Set(roots.map(row=>path.resolve(row.root).toLowerCase())).size!==21||new Set(roots.map(row=>row.condition)).size!==21||roots.filter(row=>row.variant===sdOffSelection.selectedVariants[0]&&row.condition===first).length!==1)throw Error('SD_OFF_UNIQUE_EXACT_PRIVATE_ROOTS_REQUIRED');
 const combined='('+roots.map(row=>row.condition).join(' && ')+')';
 const merged=replaceUniqueSourceText(settingsMutexIsolation.text,first,combined,'sd-off-twenty-one-exact-source-roots');
 settingsMutexIsolation.edits.push({before:first,after:combined});let restored=merged;
 for(const edit of [...settingsMutexIsolation.edits].reverse())restored=replaceUniqueSourceText(restored,edit.after,edit.before,'restore-sd-off-original-private-boundary');
 if(restored!==originalFixture)throw Error('SD_OFF_PRIVATE_ROOT_RESTORATION_FAILED');
 settingsMutexIsolation.text=merged;settingsMutexIsolation.isolatedSha256=sha(merged);settingsMutexIsolation.runtimeRoots=roots;
 const isolatorSource=path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs');
 settingsMutexIsolation.runtimeRootSources={parameterSource,parameterSourceSha256:sha(parameterBytes),isolatorSource,isolatorSourceSha256:sha(fs.readFileSync(isolatorSource))};
 settingsMutexIsolation.scope+=' SD OFF only: twenty-one source-declared, independently validated exact variant roots; parent, siblings and prefixes remain refused.';
}

if(runtimeFaultSelection){
 const originalFixture=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8');
 const declarations=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc'),'utf8');
 const tokens=name=>{const start=declarations.indexOf(name+'[]');if(start<0)throw Error('RUNTIME_CASE_DECLARATION_MISSING');const begin=declarations.indexOf('{',start),end=declarations.indexOf('}',begin);return JSON.parse('['+declarations.slice(begin+1,end)+']');};
 const families=tokens('runtimeFamilyTokens140'),personas=tokens('runtimePersonaTokens140');
 const one=text=>{const matches=text.match(/root!=normalizeWindowsPathForCompare\(L"(?:\\.|[^"\\])*"\)/g)??[];if(matches.length!==1)throw Error('EXACT_ONE_PRIVATE_RUNTIME_ROOT_REQUIRED');return matches[0];};
 const first=one(settingsMutexIsolation.text);
 const runtimeParameters=families.flatMap(family=>personas.map(persona=>({family,persona,boundary:''})));const edgeTable=/runtimeCancellationEdges140\[\]\s*=\s*\{([^}]+)\}/.exec(fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc'),'utf8'));const boundaryTokens=[...(edgeTable?.[1]??'').matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]);if(boundaryTokens.length!==6||new Set(boundaryTokens).size!==6)throw Error('RUNTIME_CANCEL_EDGE_TABLE_INVALID');for(const [family,persona] of [['rog','steamdeck'],['gpd','elite']])for(const boundary of boundaryTokens)runtimeParameters.push({family,persona,boundary});for(const boundary of ['persistent-budget','persistent-off','persistent-start-failure','persistent-prepared-failure','persistent-repeat-before-observer','persistent-repeat-after-observer'])runtimeParameters.push({family:'gpd',persona:'elite',boundary});const roots=runtimeParameters.map(({family,persona,boundary})=>{const root=path.join(out,'runtime-'+family+'-'+persona+(boundary?'-'+boundary:'')),bound=isolateCurrentFixtureSettingsMutex(originalFixture,{runtimeRoot:root,moduleMaintenance:true});return {family,persona,root,condition:one(bound.text),isolatedSha256:bound.isolatedSha256};});
 if(new Set(roots.map(row=>row.root)).size!==roots.length)throw Error('RUNTIME_PRIVATE_ROOT_DUPLICATED');
 const combined='('+[first,...roots.map(row=>row.condition)].join(' && ')+')';
 const merged=replaceUniqueSourceText(settingsMutexIsolation.text,first,combined,'exact-source-declared-independent-runtime-roots');
 settingsMutexIsolation.edits.push({before:first,after:combined});let restored=merged;
 for(const edit of [...settingsMutexIsolation.edits].reverse())restored=replaceUniqueSourceText(restored,edit.after,edit.before,'restore-exact-runtime-roots');
 if(restored!==originalFixture)throw Error('RUNTIME_PRIVATE_ROOT_RESTORATION_FAILED');
 settingsMutexIsolation.text=merged;settingsMutexIsolation.isolatedSha256=sha(merged);settingsMutexIsolation.matrixRuntimeRoots=roots;
 settingsMutexIsolation.scope+=' Source-declared exact runtime roots only; siblings/parent/prefix remain refused.';
}

if(applicationMatrixRows){
 // Reuse the original exact-root OS boundary for each independent process.
 // The unused parent and every undeclared sibling remain refused.
 const originalFixture=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8');
 const one=text=>{const matches=text.match(/root!=normalizeWindowsPathForCompare\(L"(?:\\.|[^"\\])*"\)/g)??[];if(matches.length!==1)throw Error('EXACT_ONE_PRIVATE_RUNTIME_ROOT_REQUIRED');return matches[0];};
 const first=one(settingsMutexIsolation.text),roots=applicationMatrixRows.map(row=>{const root=path.join(out,row.runtimeMode),bound=isolateCurrentFixtureSettingsMutex(originalFixture,{runtimeRoot:root,moduleMaintenance:true});return {root,condition:one(bound.text),sha256:bound.isolatedSha256};});
 const combined='('+roots.map(row=>row.condition).join(' && ')+')';
 const merged=replaceUniqueSourceText(settingsMutexIsolation.text,first,combined,'exact-sixteen-independent-startup-roots');
 settingsMutexIsolation.edits.push({before:first,after:combined});let restored=merged;
 for(const edit of [...settingsMutexIsolation.edits].reverse())restored=replaceUniqueSourceText(restored,edit.after,edit.before,'restore-sixteen-root-boundary');
 if(restored!==originalFixture)throw Error('PRIVATE_MATRIX_RESTORATION_FAILED');
 settingsMutexIsolation.text=merged;settingsMutexIsolation.isolatedSha256=sha(merged);settingsMutexIsolation.matrixRuntimeRoots=roots;
 settingsMutexIsolation.scope+=' Sixteen independently validated exact private roots only; no parent/prefix admission.';
}

if(suite==='lifecycle-directed'&&!gpdExpiredFirstOnFullCycleSelected){
 const originalFixture=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8');
 const oneCondition=text=>{const rows=text.match(/root!=normalizeWindowsPathForCompare\(L"(?:\\.|[^"\\])*"\)/g)??[];if(rows.length!==1)throw Error('EXACT_ONE_PRIVATE_RUNTIME_ROOT_REQUIRED');return rows[0];};
 const first=oneCondition(settingsMutexIsolation.text),others=['family-lifecycle','full-pipe-core'].map(mode=>{const root=path.join(out,mode),extra=isolateCurrentFixtureSettingsMutex(originalFixture,{runtimeRoot:root,moduleMaintenance:true});return {root,condition:oneCondition(extra.text),sha256:extra.isolatedSha256};});
 const combined='('+[first,...others.map(r=>r.condition)].join(' && ')+')';
 const merged=replaceUniqueSourceText(settingsMutexIsolation.text,first,combined,'three-exact-selected-runtime-roots');
 settingsMutexIsolation.edits.push({before:first,after:combined});let restored=merged;
 for(const edit of [...settingsMutexIsolation.edits].reverse())restored=replaceUniqueSourceText(restored,edit.after,edit.before,'restore-original-private-boundary');
 if(restored!==originalFixture)throw Error('PRIVATE_MATRIX_RESTORATION_FAILED');
 settingsMutexIsolation.text=merged;settingsMutexIsolation.isolatedSha256=sha(merged);settingsMutexIsolation.additionalRuntimeRoots=others;
}

let fixture=settingsMutexIsolation.text;if(['serial-power-action','serial-config-action'].includes(selectedCase)){
 const legacy=source.match(/^\s*gamepadSerialSubmit(?:CurrentPower)?\(\[\] \{ runKillBat\(\); \}\);\s*$/m);
 if(!legacy)throw Error('EXACT_LEGACY_USER_ACTION_ENQUEUE_MISSING');
 fixture=replaceUniqueSourceText(fixture,'/*ACTUAL_LEGACY_KILL_QUEUE*/',legacy[0].trim(),'exact actual legacy action enqueue');
 substitutions.push({symbol:'actual-legacy-kill-enqueue',scope:'Full original single queue statement, no replacement guard',originalSha256:sha(legacy[0].trim()),statement:legacy[0].trim()});
}if(gpdPreHideSelected)fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/pre_hide_cases.inc'),path.join(out,'pre_hide_cases.inc'),fs.constants.COPYFILE_EXCL);
fs.writeFileSync(path.join(out,'fixture.cpp'),fixture);fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/target_return_cases.inc'),path.join(out,'target_return_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/cold_archive_cases.inc'),path.join(out,'cold_archive_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/cold_msi_archive_cases.inc'),path.join(out,'cold_msi_archive_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/recovery_cost_cases.inc'),path.join(out,'recovery_cost_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/worker_cost_cases.inc'),path.join(out,'worker_cost_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc'),path.join(out,'persona_lifecycle_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc'),path.join(out,'pipe_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/off_lifecycle_cases.inc'),path.join(out,'off_lifecycle_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc'),path.join(out,'prepare_ack_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/ps_return_cases.inc'),path.join(out,'ps_return_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc'),path.join(out,'tuple_recovery_cases.inc'));fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc'),path.join(out,'ps_wake_cases.inc'));fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify({...(sourceWakeSelected?{nativeOnly:sourceReconfirmSelected,managed:wakeKeptSourceSelected}:{}),records,fixedProductEpoch,sourceExpansion,core,structCore,compilerHeaderBindings,productBeforeBinding,productCandidateBinding,sourceWakeSelection,candidateCoreAudit,gpdPersonaMutexObservation,executingConsumer,substitutions,fragments,settingsMutexIsolation},null,2));
fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/focus_recovery_cases.inc'),path.join(out,'focus_recovery_cases.inc'));
const mutatedHeaderFiles=[],mutatedHeaderInputs=[];
// The hardware seam remains closed in product. Only this private compiled
// metadata adapter admits the real durable consumer for a controlled MSI owner.
// No matched token, store result or target-absence proof is supplied here.
{
 const rel='native/msi_crash_recovery_runtime.h',raw=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const before='if(!msiClawKnownBoardAuthority() || ymStartSeamActive())return false;';
 const after='if(!msiClawKnownBoardAuthority() || (ymStartSeamActive() && !fixtureMsiOwnedMetadata))return false;';
 const adapted=replaceUniqueSourceText(raw,before,after,'MSI controlled durable metadata hardware seam');
 const name='msi_crash_recovery_runtime.fixture.h',generated=path.join(snapshot,'native',name);
 fs.writeFileSync(generated,adapted,{flag:'wx'});
 inert=replaceUniqueSourceText(inert,'#include "msi_crash_recovery_runtime.h"','#include "'+name+'"','MSI private metadata adapter include');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);
 mutatedHeaderFiles.push('source/native/'+name);mutatedHeaderInputs.push(rel);
 substitutions.push({symbol:rel,metadataOnlyHardwareSeamAdapter:true,productionHeaderSha256:sha(raw),generatedHeaderSha256:sha(adapted),
  onlyChange:{before,after},scope:'Private fixture only: original same-owned durable commit body and callback unchanged; hardware guard admits the explicit controlled metadata flag. Raw production snapshot remains unchanged.'});
}

if(mutation==='rollback-cold-observed-id'){
 const rel='native/virtual_target_return_pnp_absence.h',file=path.join(snapshot,rel),original=fs.readFileSync(file,'utf8');
 const altered=replaceUniqueSourceText(original,'if(!leafDetail::observedInstance(observed)){','if(!ymcc::targetReturn::detail::instance(leafDetail::instanceUtf8(observed))){',mutation);
 const generated=path.join(snapshot,'native/virtual_target_return_pnp_absence.mutant.h');fs.writeFileSync(generated,altered,{flag:'wx'});
 inert=replaceUniqueSourceText(inert,'#include "virtual_target_return_pnp_absence.h"','#include "virtual_target_return_pnp_absence.mutant.h"',mutation+'-leaf');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/virtual_target_return_pnp_absence.mutant.h');mutatedHeaderInputs.push(rel);
 substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Only OS census ID validator rolled back; record authority and all original lifecycle consumers/assertions unchanged.'});
}
// Directed header negatives compile a changed private header. Raw production
// snapshots stay intact, with their hash bound as transformation inputs.

if(mutation==='rollback-wake-retirement-identity'||mutation==='rollback-wake-start-budget'){
 const rel='native/virtual_wake_execution_runtime.h',original=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const from=mutation==='rollback-wake-retirement-identity'?'static void retireLocked(Work w,const char* reason) {':'if(episode.startAttempts>=kWakeHotReplaceMaxAttempts)';
 const to=mutation==='rollback-wake-retirement-identity'?'static void retireLocked(const Work& w,const char* reason) {':'if(false&&episode.startAttempts>=kWakeHotReplaceMaxAttempts)';
 const altered=replaceUniqueSourceText(original,from,to,mutation),generated=path.join(snapshot,'native/virtual_wake_execution_runtime.mutant.h');
 fs.writeFileSync(generated,altered,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "virtual_wake_execution_runtime.h"','#include "virtual_wake_execution_runtime.mutant.h"',mutation+'-include');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/virtual_wake_execution_runtime.mutant.h');mutatedHeaderInputs.push(rel);
 substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Actual changed private production header; restore only the selected proven bug. Raw source, original assertions and actual owned Host path stay unchanged.'});
}
if(mutation==='rollback-queued-player-binding'){
 const rel='native/virtual_controller_queued_start_runtime.h',original=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const altered=replaceUniqueSourceText(original,'if(!startReason(reason))return reason;','if(true||!startReason(reason))return reason;',mutation);
 const generated=path.join(snapshot,'native/virtual_controller_queued_start_runtime.mutant.h');fs.writeFileSync(generated,altered,{flag:'wx'});
 inert=replaceUniqueSourceText(inert,'#include "virtual_controller_queued_start_runtime.h"','#include "virtual_controller_queued_start_runtime.mutant.h"',mutation+'-include');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/virtual_controller_queued_start_runtime.mutant.h');mutatedHeaderInputs.push(rel);
 substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Only remove admitted player provenance from actual queued initial/persona Start; same real managed pipe and original cases.'});
}
if(['rollback-ps-return-pin','rollback-ps-return-physical','rollback-ps-return-family'].includes(mutation)){
 const rel=mutation==='rollback-ps-return-pin'?'native/ps_endpoint_ownership.h':'native/ps_endpoint_return_runtime.h';const original=fs.readFileSync(path.join(snapshot,rel),'utf8');let altered=original;
 if(mutation==='rollback-ps-return-pin')altered=replaceUniqueSourceText(altered,'retirement_ = {true, expected_, state_};','retirement_ = {true, expected_, Snapshot{}};',mutation);
 if(mutation==='rollback-ps-return-physical')altered=replaceUniqueSourceText(altered,'return absent; // keep the exact debt until physical restore/hold also succeeds','return absent && g_psEndpointOwnership.confirmRetirement(debt.owner.runId,debt.owner.epoch);',mutation);
 if(mutation==='rollback-ps-return-family'){if(altered.includes(' && psRetiredProfileRootsAbsent()'))altered=replaceUniqueSourceText(altered,' && psRetiredProfileRootsAbsent()','','no-pin whole family negative-proof omission');altered=replaceUniqueSourceText(altered,'if(family(detail->DevicePath))return false;','if(false&&family(detail->DevicePath))return false;',mutation+'-path');altered=replaceUniqueSourceText(altered,'if(family(id))return false;','if(false&&family(id))return false;',mutation+'-ancestor');}
 const name=path.basename(rel,'.h')+'.mutant.h',generated=path.join(snapshot,'native',name);fs.writeFileSync(generated,altered,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "'+path.basename(rel)+'"','#include "'+name+'"',mutation+'-include');fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/'+name);mutatedHeaderInputs.push(rel);substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Only selected retired PS ownership/proof omission in actual compiled production header; raw sources and actual child/reader cases unchanged.'});
}
// Header-defined return readers must use the SAME admitted SetupAPI/CM ends
// as the existing main.cpp reader. Main-text substitution cannot reach includes.
{
 const rel='native/ps_endpoint_return_runtime.h',raw=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const ports={SetupDiGetClassDevsW:'fixturePsGetClassDevs',SetupDiEnumDeviceInterfaces:'fixturePsEnum',SetupDiGetDeviceInterfaceDetailW:'fixturePsDetail',SetupDiDestroyDeviceInfoList:'fixturePsDestroy',CM_Get_Device_IDW:'fixturePsDeviceId',CM_Get_Parent:'fixturePsParent',CM_Locate_DevNodeW:'fixturePsLocate'};
 const called=[...new Set([...raw.matchAll(/\b(SetupDi\w+|CM_\w+)\s*\(/g)].map(m=>m[1]))];if(called.some(name=>!Object.hasOwn(ports,name)))throw Error('UNADMITTED_PS_RETURN_DEVICE_API:'+called.join(','));
 const target=['rollback-ps-return-physical','rollback-ps-return-family'].includes(mutation)?'ps_endpoint_return_runtime.mutant.h':'ps_endpoint_return_runtime.h';
 const wrapper=Object.entries(ports).map(([api,port])=>'#define '+api+' '+port).join('\n')+'\n#include "'+target+'"\n'+Object.keys(ports).map(api=>'#undef '+api).join('\n')+'\n';
 const generated=path.join(snapshot,'native/ps_endpoint_return_runtime.fixture.h');fs.writeFileSync(generated,wrapper,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "'+target+'"','#include "ps_endpoint_return_runtime.fixture.h"','ps-return-explicit-device-ends');fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/ps_endpoint_return_runtime.fixture.h');
 substitutions.push({symbol:rel,scope:'Original production return algorithm compiled verbatim; all six SetupAPI/CM reads redirected only within this include to pre-existing controlled leaf functions. Unknown device API rejects before compile.',originalApiCalls:called,ports,wrapperSha256:sha(wrapper),productionHeaderSha256:sha(raw),controlAlgorithmChanged:false});
}
fs.copyFileSync(path.join(snapshot,'tools/msi_late_hide_pending_regression.h'),path.join(out,'msi_late_hide_pending_regression.h'));
fs.copyFileSync(path.join(snapshot,'tools/msi_late_hide_publication_regression.h'),path.join(out,'msi_late_hide_publication_regression.h'));
fs.copyFileSync(path.join(snapshot,'tools/msi_late_hide_inflight_regression.h'),path.join(out,'msi_late_hide_inflight_regression.h'));
verifyCandidateExecutionSource(fs.readFileSync(path.join(snapshot,'native/main.inert.cpp'),'utf8'));
if(productCandidateDrift().length)throw Error('PRODUCT_CANDIDATE_CHANGED_BEFORE_COMPILE');
if(nativeWriterReturn)fs.copyFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/gpd_native_writer_return.inc'),path.join(out,'gpd_native_writer_return.inc'));
const compiledGenerated=[...(prepareRejectionSelected?['prepare_rejection_cases.inc','prepare_rejection_on_edge.inc']:[]),...(gpdPreHideSelected?['pre_hide_cases.inc']:[]),...(nativeWriterReturn?['gpd_native_writer_return.inc']:[]),'msi_late_hide_pending_regression.h','msi_late_hide_publication_regression.h','msi_late_hide_inflight_regression.h','fixture.cpp','worker_cost_cases.inc','recovery_cost_cases.inc','cold_archive_cases.inc','cold_msi_archive_cases.inc','target_return_cases.inc','persona_lifecycle_cases.inc','pipe_cases.inc','off_lifecycle_cases.inc','prepare_ack_cases.inc','ps_return_cases.inc','tuple_recovery_cases.inc','ps_wake_cases.inc','focus_recovery_cases.inc','xinput_discovery.inc','source/native/main.inert.cpp',...mutatedHeaderFiles].map(rel=>{const p=path.join(out,rel);return{path:p,sha256:sha(fs.readFileSync(p))};});
const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';const native=path.join(snapshot,'native'),json=path.join(snapshot,'deps/json'),webview=path.join(snapshot,'deps/webview2/build/native/include');
const runtimeSuccessorDefines=(gpdPreHideSelected||delayedSuccessorSelected||terminalFallbackSelected)?' /DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1':'';
const workerCostDefines=suite==='worker-cost'?' /DYMCC_WORKER_COST_PRODUCTION_METADATA /DYMCC_MSI_HOST_STAGE_DIAGNOSTIC /DYMCC_MSI_VISIBILITY_DIAGNOSTIC /DYMCC_MSI_USB_NAME_ORIGIN_DIAGNOSTIC /DYMCC_MSI_VISIBILITY_RAW_DIAGNOSTIC /DYMCC_MSI_VISIBILITY_WAKE_DIAGNOSTIC':'';
const sourceReconfirmCollectionCompiled=sourceReconfirmSelected&&source.includes('static bool xinputReconfirmSelectedCollectionSource(');
const sourceReconfirmCollectionDefines=sourceReconfirmCollectionCompiled?' /DYMCC_FIXTURE_SOURCE_RECONFIRM_COLLECTION=1':'';
const sdOffDefines=(gpdPreHideSelected?' /DYMCC_FIXTURE_GPD_PENDING_BEFORE_HIDE=1':'')+(sdOffSourceLostSelected?' /DYMCC_FIXTURE_SD_OFF_SOURCE_LOST=1':'')+(oemAfterOffSelected?' /DYMCC_FIXTURE_OEM_AFTER_OFF=1':'')+(nativeWriterReturn?' /DYMCC_FIXTURE_GPD_NATIVE_WRITER_RETURN=1':'');
const gpdWakeBoundaryDefines=(gpdR4CandidateBinding?' /DYMCC_GPD_WAKE_R4_BOUNDARY_PROBES=1':gpdPersonaMutexSelected?' /DYMCC_GPD_PERSONA_MUTEX_PROBES=1':'')+sourceReconfirmCollectionDefines;
const nativeCompilerConfiguration={nativeWriterReturn:nativeWriterReturn==='actual',nativeWriterReturnDefine:nativeWriterReturn?'YMCC_FIXTURE_GPD_NATIVE_WRITER_RETURN=1':null,sdOffSelection,sdOffDefine:sdOffSourceLostSelected?'YMCC_FIXTURE_SD_OFF_SOURCE_LOST=1':null,...(sourceWakeSelected?{nativeOnly:sourceReconfirmSelected,managed:wakeKeptSourceSelected,sourceReconfirmCollectionCompiled,sourceReconfirmCollectionDefine:sourceReconfirmCollectionCompiled?'YMCC_FIXTURE_SOURCE_RECONFIRM_COLLECTION=1':null}:{}),gpdPersonaMutexProbes:gpdPersonaMutexSelected,gpdWakeR4BoundaryProbes:!!gpdR4CandidateBinding,optimization:nativeOptimization,fullOriginalCaseSelection:!selectedCase,performanceEvidence:false,optimizedProductEquivalence:nativeOptimization==='O2'};fs.writeFileSync(path.join(out,'NATIVE-COMPILER-CONFIGURATION.json'),JSON.stringify(nativeCompilerConfiguration,null,2)+'\n');
const buildStarted=performance.now();
const build=calibrationToolchainPreamble(vc)+'chcp 65001 >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /'+nativeOptimization+' /showIncludes /bigobj /DNDEBUG /DUNICODE /D_UNICODE'+workerCostDefines+runtimeSuccessorDefines+gpdWakeBoundaryDefines+sdOffDefines+(prepareRejectionSelected?' /DYMCC_PREPARE_REJECTION_COLD_CASE=1':'')+' /c "'+path.join(out,'fixture.cpp')+'" /I"'+native+'" /I"'+json+'" /I"'+webview+'" /Fo"'+path.join(out,'fixture.obj')+'"\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /'+nativeOptimization+' /showIncludes /DNDEBUG /DUNICODE /D_UNICODE /c "'+path.join(native,'dsu_server.cpp')+'" /Fo"'+path.join(out,'dsu.obj')+'"\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /'+nativeOptimization+' /showIncludes /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c "'+path.join(native,'steam_original_recovery_engine.cpp')+'" /I"'+json+'" /Fo"'+path.join(out,'steam-original-recovery.obj')+'"\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo "'+path.join(out,'fixture.obj')+'" "'+path.join(out,'dsu.obj')+'" "'+path.join(out,'steam-original-recovery.obj')+'" /Fe"'+path.join(out,'CoordinatorFixture.exe')+'" /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "'+path.join(repo,'deps/webview2/build/native/x64/WebView2LoaderStatic.lib')+'" "%WindowsSdkDir%Lib\\%WindowsSDKVersion%um\\x64\\WindowsApp.lib" user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(path.join(out,'build.cmd'),build);
if(prepareOnly){
 const pin=file=>{const bytes=fs.readFileSync(file);return {path:path.resolve(file),bytes:bytes.length,sha256:sha(bytes)};};
 const sourceChanges=records.filter(row=>sha(fs.readFileSync(row.original))!==row.sha256||sha(fs.readFileSync(row.snapshot))!==row.sha256).map(row=>row.path);
 const generated=compiledGenerated.map(row=>{const actual=pin(row.path);if(actual.sha256!==row.sha256)throw Error('PREPARED_GENERATED_INPUT_CHANGED:'+row.path);return actual;});
 const prepare={schemaVersion:1,status:sourceChanges.length?'REJECTED_PREPARATION_SOURCE_DRIFT':'PREPARED_NOT_COMPILED',out,runtimeRoot:gpdPreHideRuntimeRoot,selectedCase,selection:gpdPreHideSelection,
  snapshotRoot:snapshot,sourceCopies:records,sourceExpansion,expandedSource:pin(expandedFile),generated,buildRecipe:pin(path.join(out,'build.cmd')),
  core,structCore,compilerHeaderBindings,substitutions,fragments,settingsMutexIsolation,executingConsumer,
  sourceManifest:pin(path.join(out,'source-manifest.json')),sourceChanges,sourceFullTranslationUnit:true,
  compilerInvocations:0,businessExecutions:0,compilerObservedClosureEstablished:false,originalCompileTimeoutMs:300000,originalModeTimeoutMs:90000,
  meaning:'Original preparation and exact build recipe only. Actual compiler-observed closure, Native result, current whole parent and hardware remain unaccepted.'};
 const prepareFile=path.join(out,'PREPARE.json');fs.writeFileSync(prepareFile,JSON.stringify(prepare,null,2)+'\n',{flag:'wx'});
 const result={schemaVersion:1,status:prepare.status,preparation:pin(prepareFile),selectedCase,sourceCopies:records.length,generated:generated.length,
  compilerInvocations:0,businessExecutions:0,sourceChanges,preparationMs:performance.now()-buildStarted,compileTimeoutMs:300000,modeTimeoutMs:90000};
 fs.writeFileSync(path.join(out,'PREPARE-ONLY-RESULT.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));process.exit(sourceChanges.length?1:0);
}

const p=spawnSync('cmd.exe',['/d','/c',path.join(out,'build.cmd')],{cwd:out,windowsHide:true,encoding:'utf8',timeout:300000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(out,'build.log'),(p.stdout??'')+(p.stderr??''));fs.writeFileSync(path.join(out,'build-process.json'),JSON.stringify({elapsedMs:performance.now()-buildStarted,exitCode:p.status,signal:p.signal,error:p.error?{code:p.error.code,message:p.error.message}:null,timedOut:p.error?.code==='ETIMEDOUT',timeoutMs:300000},null,2),{flag:'wx'});if(p.status!==0)throw Error('full native fixture compile failed '+p.status+' '+path.join(out,'build.log'));
const closure=nativeCompilerInputClosure({records,buildLog:(p.stdout??'')+(p.stderr??''),snapshotRoot:snapshot,generatedFiles:compiledGenerated.map(r=>r.path),requiredSources:[...(gpdLiveSdTransferSelected?['native/msi_native_foreground_isolation_runtime.h','native/native_visibility_cold_runtime.h','native/virtual_switch_execution_return_runtime.h']:[]),...expansionInputs,...mutatedHeaderInputs,'native/dsu_server.cpp','native/steam_original_recovery_engine.cpp',...records.filter(r=>r.path.startsWith('tools/')).map(r=>r.path)]});
for(const r of compiledGenerated)if(sha(fs.readFileSync(r.path))!==r.sha256)throw Error('GENERATED_INPUT_CHANGED_DURING_COMPILE:'+r.path);

for(const binding of compilerHeaderBindings){
 const file=path.resolve(path.join(snapshot,binding.source)).toLowerCase();
 // Only this directed control compiles a changed leaf. Preserve the raw source
 // binding and separately require compiler observation of its generated mutant.
 const coldIdControl=mutation==='rollback-cold-observed-id'&&binding.source==='native/virtual_target_return_pnp_absence.h';
 const executed=coldIdControl?path.resolve(snapshot,'native/virtual_target_return_pnp_absence.mutant.h').toLowerCase():file;
 if(!closure.observedIncludePaths.includes(executed))throw Error('REQUIRED_REAL_COMPILER_HEADER_NOT_OBSERVED:'+binding.source);
 if(sha(fs.readFileSync(file))!==binding.sha256)throw Error('REQUIRED_REAL_COMPILER_HEADER_CHANGED:'+binding.source);
 if(coldIdControl){const generated=compiledGenerated.find(row=>path.resolve(row.path).toLowerCase()===executed);if(!generated||sha(fs.readFileSync(executed))!==generated.sha256)throw Error('COLD_CONTROL_GENERATED_HEADER_NOT_BOUND');binding.executedControl={file:executed,sha256:generated.sha256,rawHeaderCompiled:false};}
}
const snapshotCopies=[...records];records.splice(0,records.length,...closure.records);fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify({...(sourceWakeSelected?{nativeOnly:sourceReconfirmSelected,managed:wakeKeptSourceSelected}:{}),records,fixedProductEpoch,sourceExpansion,core,structCore,compilerHeaderBindings,productBeforeBinding,productCandidateBinding,sourceWakeSelection,candidateCoreAudit,gpdPersonaMutexObservation,executingConsumer,substitutions,fragments,settingsMutexIsolation,compiledGenerated,snapshotCopies,copiedButUncompiled:closure.copiedButUncompiled,observedIncludePaths:closure.observedIncludePaths,sourceClosureMeaning:closure.meaning+' Expanded raw runtime sources are pinned primary transformation inputs; sourceExpansion binds them to main.expanded.cpp and compiledGenerated binds the executed main.inert.cpp.'},null,2));
// One immutable compiled candidate runs native and pipe modes in separate owned processes.
// Reusing this exact EXE saves the former second full-TU build; no stale executable cache.
const nativeModes=['entry','mutation1','mutation2','rollback','queued-exception','queued-exception-common','healthy-log-exception','start-protection','recovery-protection','record-read','switch-recovery','tuple-protection'];
const modes=(wakeKeptSourceSelected||wakeKeptSelected||gpdExpiredFirstOnFullCycleSelected)?['full-pipe-core']:suite==='input-application'?(applicationMatrixRows?applicationMatrixRows.map(row=>row.runtimeMode):['input-application']):suite==='lifecycle-directed'?['target-return','family-lifecycle','full-pipe-core']:suite==='target-return'?['target-return']:suite==='target-return-family'?['target-return-family']:suite==='switch-recovery'?['switch-recovery']:suite==='tuple-protection'?['tuple-protection']:suite==='off-lifecycle'?['off-lifecycle']:suite==='focus-recovery'?['focus-recovery']:suite==='worker-cost'?[...nativeModes,'late-hide-pending','late-hide-publication','late-hide-inflight','late-hide-inflight-edges','late-hide-orphaned-inflight','worker-cost']:suite==='recovery-cost'?[...nativeModes,'late-hide-pending','late-hide-publication','late-hide-inflight','late-hide-inflight-edges','late-hide-orphaned-inflight','recovery-cost']:suite==='late-hide-orphaned-inflight'?['late-hide-orphaned-inflight']:suite==='late-hide-inflight-edges'?['late-hide-inflight-edges']:suite==='late-hide-inflight'?['late-hide-inflight']:suite==='late-hide-current'?['late-hide-pending','late-hide-publication','late-hide-inflight','late-hide-inflight-edges','late-hide-orphaned-inflight']:suite==='late-hide-publication'?['late-hide-publication']:suite==='late-hide-pending'?['late-hide-pending']:suite==='cold-msi-archive'?['cold-msi-archive']:suite==='cold-archive'?['cold-archive']:suite==='family-lifecycle'?['family-lifecycle']:suite==='persona-lifecycle'?['persona-lifecycle']:managedDll?(suite==='all'?[...nativeModes,'full-pipe','persona-lifecycle','focus-recovery',...(coldRecord?['cold-archive']:[])]:['full-pipe']):mutation?(mutation==='bypass-admission'?['start-protection']:mutation.startsWith('rollback-switch-')?['switch-recovery']:['recovery-protection']):nativeModes;
if(reuseSuite)modes.push(reuseSuite);
if(suite==='late-hide-pending'||reuseSuite==='late-hide-pending'){
 const header=fs.readFileSync(path.join(snapshot,'native/msi_late_hide_pending.h'),'utf8');
 const expectedEnabled=productBeforeBinding?productBeforeBinding.defaultPendingEnabled:true;
 if(expectedEnabled?(!header.includes('inline constexpr bool buildEnabled=true;')||header.includes('inline constexpr bool buildEnabled=false;')):!header.includes('inline constexpr bool buildEnabled=false;'))throw Error('UNEXPECTED_PRODUCT_POLICY_INPUT');
 if(/YMCC_MSI_LATE_HIDE_PENDING_FIX/.test(fs.readFileSync(path.join(out,'build.cmd'),'utf8')))throw Error('UNREQUESTED_PRODUCT_FLAG');
}
let coldRecordBinding=null;if(coldRecord){const frozen=path.join(out,'archived-target-return.json');fs.copyFileSync(coldRecord,frozen);coldRecordBinding={path:coldRecord,snapshot:frozen,sha256:sha(fs.readFileSync(frozen))};}
const results=[];let applicationMatrixStarted=null;
const modeRuns=gpdPreHideBatch?gpdPreHideBatch.map((caseName,index)=>({mode:'family-lifecycle',caseName,index})):
 modes.map(mode=>({mode,caseName:selectedCase,index:null}));
for(const {mode,caseName,index} of modeRuns){const matrixRow=applicationMatrixRows?.find(row=>row.runtimeMode===mode),nativeMode=matrixRow?'input-application':mode;const dir=gpdPreHideSelected?(index===null?gpdPreHideRuntimeRoot:path.join(gpdPreHideRuntimeRoot,'b'+index)):sdOffSourceLostSelected?path.join(out,'off-lifecycle',sdOffSelection.selectedVariants[0]):path.join(out,mode);if(gpdPreHideSelected||sdOffSourceLostSelected){fs.mkdirSync(path.dirname(dir),{recursive:true});fs.mkdirSync(dir);}else fs.mkdirSync(dir);const originalModeTimeoutMs=(mode==='full-pipe-core'||mode==='full-pipe'||mode==='persona-lifecycle'||mode==='family-lifecycle'||mode==='cold-archive'||mode==='cold-msi-archive')?90000:45000;let effectiveModeTimeoutMs=mode===reuseSuite?Math.min(originalModeTimeoutMs,reuseBudgetMs):originalModeTimeoutMs;if(matrixRow){applicationMatrixStarted??=performance.now();const remaining=Math.floor(45000-(performance.now()-applicationMatrixStarted));if(remaining<=0)throw Error('ORIGINAL_MATRIX_RUNTIME_BUDGET_EXHAUSTED');effectiveModeTimeoutMs=Math.min(effectiveModeTimeoutMs,remaining);}const modeStarted=performance.now();const r=spawnSync(path.join(out,'CoordinatorFixture.exe'),[dir,nativeMode,...(caseName?["case:"+caseName+(nativeWriterReturn?":native-writer":""),...(managedDll?[managedDll]:[])]:managedDll?[managedDll]:[])],{cwd:dir,env:{...process.env,YEMAN_POWER_CONTROL_DIR:dir,YMCC_FIXTURE_FAMILY:family??'',YMCC_FIXTURE_DOTNET:selectedDotnet??'',YMCC_FIXTURE_MOTION_DLL:motionLibraryBinding?.snapshot??'',YMCC_FIXTURE_INPUT_BOOT:matrixRow?.boot??'',YMCC_FIXTURE_INPUT_INDEX:matrixRow?String(matrixRow.index):'',...(selectedDotnet?{YMCC_FIXTURE_DOTNET:selectedDotnet,DOTNET_ROOT:path.dirname(selectedDotnet),DOTNET_MULTILEVEL_LOOKUP:'0',DOTNET_CLI_HOME:dir,DOTNET_CLI_TELEMETRY_OPTOUT:'1'}:{}),...(applicationRequestBinding?{YMCC_FIXTURE_INPUT_REQUESTS:applicationRequestBinding.snapshot}:{}),...(['onex','aokzoe'].includes(family)?{YMCC_FIXTURE_ONEX_APPLICATION:path.join(out,'CoordinatorFixture.exe')}:{}),...(coldRecordBinding?{YMCC_FIXTURE_COLD_RECORD:coldRecordBinding.snapshot}:{})},windowsHide:true,encoding:'utf8',timeout:effectiveModeTimeoutMs,maxBuffer:2*1024*1024});const modeProcessWallMs=performance.now()-modeStarted;fs.writeFileSync(path.join(dir,'stdout.log'),r.stdout??'');fs.writeFileSync(path.join(dir,'stderr.log'),r.stderr??'');let result;try{result=JSON.parse(fs.readFileSync(path.join(dir,'fixture-result.json'),'utf8'));}catch{}if(['entry','mutation1','mutation2'].includes(mode)){const mutation=mode==='entry'?0:mode==='mutation1'?1:2;const original=fs.readFileSync(path.join(dir,'msi-start-order-entry-selftest-m'+mutation+'.txt'),'utf8');const rows=[...original.matchAll(/^(PASS|FAIL) (.+)$/gm)].map(m=>({ok:m[1]==='PASS',assertion:m[2].trim()}));if(rows.length!==56||!original.includes('cases=56')||!original.includes('RESULT='+(mutation?'FAIL':'PASS')))throw Error('Original order fixture report incomplete:'+mode);if(mutation&&!rows.some(x=>!x.ok&&x.assertion===(mutation===1?'Entry-B-ensure-after-stop':'Entry-A-armed-at-admit')))throw Error('Wrong original semantic negative:'+mode);result.originalOrderAssertions=rows;result.originalOrderChecks=rows.length;}results.push({mode,nativeMode,matrixRow,...(index===null?{}:{gpdPreHideCase:caseName,runtimeRoot:dir}),modeProcessWallMs,originalModeTimeoutMs,effectiveModeTimeoutMs,modeWallTimeoutMs:effectiveModeTimeoutMs,timedOut:r.error?.code==='ETIMEDOUT',exitCode:r.status,signal:r.signal,error:r.error?{code:r.error.code,message:r.error.message}:null,result});console.log(mode+': '+r.status+' '+(result?.status??'missing-result'));if(index!==null&&!inspectGpdPreHideCase(results.at(-1),gpdPreHideSelection.expectedCases[index]).semanticPassed)break;if((suite==='lifecycle-directed'||!!matrixRow)&&(r.error||r.signal||r.status!==0||result?.status!=='PASS'))break;}
// Receipt validation only. Preserve raw process outcomes and reject incomplete
// canonical scopes; independent cases may complete in any legal order.
function inspectFamilyCaseSelection(selection, results) {
 const selected=results.filter(row=>row?.mode==='family-lifecycle');
 const rows=Array.isArray(selected[0]?.result?.cases)?selected[0].result.cases:[];
 const names=rows.map(row=>row?.name);
 const issues=[];
 if(selected.length!==1||results.length!==1)issues.push('producer-mode-scope');
 if(rows.length!==selection.expectedCount)issues.push('case-count');
 if(names.some(name=>typeof name!=='string'||!name)||new Set(names).size!==names.length)issues.push('case-name-missing-or-duplicate');
 const expected=selection.expectedCases;
 if(expected){
  const expectedNames=new Set(expected);
  if(expected.length!==selection.expectedCount||expectedNames.size!==expected.length||names.some(name=>!expectedNames.has(name)))issues.push('canonical-case-set');
  if(selection.scope==='runtime-after-final-exact'){if(rows.some(row=>row?.realManagedPipe!==true))issues.push('case-producer-scope');}
  else if(selection.scope==='classified-fault-exact'){if(rows.some(row=>row?.controlledFamily!=='ROG'||row?.realManagedPipe!==true))issues.push('case-producer-scope');}
  else if(rows.some(row=>row?.familySelection!==selection.family||row?.suite!=='family-lifecycle'||row?.managedCommandPath!==(typeof row?.name==='string'&&row.name.startsWith(selection.family+'/lifecycle/'))))issues.push('case-producer-scope');
  if(rows.some(row=>row?.passed!==true))issues.push('case-not-passed');
 }else if(names.some(name=>typeof name==='string'&&name.startsWith('onex/helper/')))issues.push('unexpected-directed-helper');
 const actualSubvariants=[];
 for(const row of rows){
  if(!row||!Object.hasOwn(row,'subvariants'))continue;
  const variants=Array.isArray(row.subvariants)?row.subvariants:[];
  const variantNames=variants.map(variant=>variant?.variant);
  if(!variants.length||variantNames.some(name=>typeof name!=='string'||!name)||new Set(variantNames).size!==variants.length||variants.some(variant=>variant?.passed!==true))issues.push('subvariant-incomplete:'+row.name);
  actualSubvariants.push({case:row.name,variants:variantNames,passed:variants.every(variant=>variant?.passed===true)});
 }
 // Omitting a whole required parameter group must not masquerade as coverage.
 for(const [caseName,expectedVariants] of Object.entries(selection.expectedSubvariants??{})){
  const row=rows.find(row=>row?.name===caseName);
  const variants=Array.isArray(row?.subvariants)?row.subvariants:[];
  const names=variants.map(variant=>variant?.variant);
  const expectedNames=new Set(expectedVariants);
  if(!expectedVariants.length||expectedNames.size!==expectedVariants.length||names.length!==expectedVariants.length||new Set(names).size!==names.length||names.some(name=>!expectedNames.has(name)))issues.push('canonical-subvariant-set:'+caseName);
 }
 const exact=issues.length===0;
 return {...selection,actualCases:names,actualCount:names.length,actualSubvariants,
  expectedOrderPreserved:expected?names.every((name,index)=>name===expected[index]):null,
  comparisonPolicy:'canonical-name-set;source-declared-subvariant-set;unique-producer;semantic-pass',
  exactScope:exact,issues,status:exact?'SCOPE_MATCH':'SCOPE_MISMATCH'};
}
if(familySelection){
 const observation=inspectFamilyCaseSelection(familySelection,results);
 fs.writeFileSync(path.join(out,'family-selection-observed.json'),JSON.stringify(observation,null,2),{flag:'wx'});
 familySelection=observation;
 if(!observation.exactScope){const selected=results.find(row=>row.mode==='family-lifecycle');if(selected)selected.scopeMismatch=true;}
}
let selectedCaseScope=null;
if(oneXParameterizedSelected){
 selectedCaseScope={selectedCase,...familySelection,exact:familySelection?.exactScope===true};
 if(!selectedCaseScope.exact)throw Error('ONEX_PARAMETERIZED_EXACT_SCOPE_MISMATCH');
}

if(oemAfterOffSelected){
 const expectedCases=['OEM-AFTER-OFF/AYA-AIRPro','OEM-AFTER-OFF/GPD-Win5'],producer=results[0],rows=producer?.result?.cases??[],actualCases=rows.map(row=>row.name);
 selectedCaseScope={selectedCase,expectedCases,actualCases,exact:results.length===1&&producer.mode==='persona-lifecycle'&&JSON.stringify(expectedCases)===JSON.stringify(actualCases),scope:'actual private SDK-free Host Start/OFF plus fresh OEM hook/retained message/serial executor; final desktop OS effects controlled'};
 fs.writeFileSync(path.join(out,'oem-after-off-selection.json'),JSON.stringify(selectedCaseScope,null,2),{flag:'wx'});
 if(!selectedCaseScope.exact)throw Error('OEM_AFTER_OFF_CASE_SCOPE_MISMATCH');
}
if(gpdPreHideSelected){
 const expected=readGpdPreHideSelection(fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/pre_hide_cases.inc'),'utf8'),selectedCase);
 selectedCaseScope=inspectGpdPreHideSelection(expected,results);
 fs.writeFileSync(path.join(out,'gpd-pre-hide-selection-observed.json'),JSON.stringify(selectedCaseScope,null,2),{flag:'wx'});
 if(JSON.stringify(expected)!==JSON.stringify(gpdPreHideSelection)||(!gpdPreHideBatch&&!selectedCaseScope.exact))throw Error('GPD_PRE_HIDE_EXACT_SCOPE_MISMATCH');
}else if(sdOffSourceLostSelected){
 const expected=readSdOffSelection(fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/off_lifecycle_cases.inc'),'utf8')),producer=results[0],rows=producer?.result?.cases??[],actualCases=rows.map(row=>row.name);
 const exact=JSON.stringify(expected)===JSON.stringify(sdOffSelection)&&results.length===1&&producer.mode==='off-lifecycle'&&!producer.error&&!producer.signal&&!producer.timedOut&&JSON.stringify(actualCases)===JSON.stringify(expected.expectedCases);
 selectedCaseScope={...expected,actualCases,exact,semanticPassed:producer?.exitCode===0&&producer?.result?.status==='PASS'&&producer?.result?.forbiddenIo===0};
 fs.writeFileSync(path.join(out,'sd-off-selection-observed.json'),JSON.stringify(selectedCaseScope,null,2),{flag:'wx'});
 if(!exact)throw Error('SD_OFF_EXACT_ORIGINAL_SCOPE_MISMATCH');
}else if(sourceWakeSelected){
 const relative=sourceReconfirmSelected?'tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc':path.relative(repo,sourceWakeParameterSource),expected=readSourceWakeSelection(fs.readFileSync(path.join(snapshot,relative),'utf8'));
 const producer=results[0],report=producer?.result,rows=Array.isArray(report?.cases)?report.cases:[],actualCases=rows.map(row=>row?.name);
 const expectedNames=new Set(expected.expectedCases);
 const exact=JSON.stringify(expected)===JSON.stringify(sourceWakeSelection)&&results.length===1&&producer.mode===expected.requestedModes[0]&&!producer.error&&!producer.signal&&!producer.timedOut&&actualCases.length===expected.expectedCount&&new Set(actualCases).size===actualCases.length&&actualCases.every(name=>expectedNames.has(name));
 const semanticPassed=producer.exitCode===0&&report?.status==='PASS'&&report.forbiddenIo===0&&rows.every(row=>row?.passed===true);
 selectedCaseScope={...expected,actualCases,exact,semanticPassed,status:exact&&semanticPassed?'PASS':'FAIL',comparisonPolicy:'exact unique source-declared canonical name set and one requested mode; outer source identity remains independently authoritative'};
 fs.writeFileSync(path.join(out,'source-wake-selection-observed.json'),JSON.stringify(selectedCaseScope,null,2),{flag:'wx'});
 if(!exact)throw Error('SOURCE_WAKE_EXPLICIT_SCOPE_MISMATCH');
}else if(gpdLiveSdTransferSelection){
 const rows=results[0]?.result?.cases??[],actualCases=rows.map(row=>row.name),expectedCases=gpdLiveSdTransferSelection.expectedCases;
 const exact=results.length===1&&results[0].mode==='persona-lifecycle'&&rows.length===2&&new Set(actualCases).size===2&&
  expectedCases.every(name=>actualCases.includes(name))&&rows.every(row=>{
   const ui=row.nativeUiObservation,serial=row.serializedTransfer;
   return row.passed===true&&row.controlledFamily==='GPD'&&row.realManagedPipe===true&&ui?.handleWorkCalls===1&&
    ui.nativeVisibilityPersistence===true&&ui.coldReturnOverride===false&&ui.effectsRestoredBeforeDispatch===true&&
    ui.deviceReads===0&&ui.deviceWrites===0&&ui.refreshes===0&&ui.queues===0&&ui.nativePending===false&&ui.coldPending===false&&
    serial?.queueReason==='persona-transition'&&serial.actualDispatcher===true&&serial.beforeVirtualWorkOnProductionPath===true&&
    serial.stopBeforeCreate===true&&serial.oldExited===true&&serial.replacementHosts===1;
  });
 selectedCaseScope={...gpdLiveSdTransferSelection,actualCases,exact,comparisonPolicy:'exact unique canonical name set plus actual foreground/serial receipt facts'};
 if(!exact)throw Error('GPD_LIVE_SD_TRANSFER_EXPLICIT_SCOPE_MISMATCH');
}else if(gpdExpiredFirstOnFullCycleSelected){
 const expected=readGpdExpiredFirstOnFullCycleSelection(fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc'),'utf8'));
 const report=results[0]?.result,rows=Array.isArray(report?.cases)?report.cases:[],actualCases=rows.map(row=>row?.name);
 const stopReceipts=rows.map(inspectGpdStopReceipt);
 const successorTupleRequired=!!nativeWriterReturn&&fs.readFileSync(path.join(snapshot,'native/virtual_runtime_recovery_runtime.h'),'utf8').includes('static void inputHostRuntimeRecoveryBindStartTupleLocked(');
 const currentSuccessorTuple=row=>{
  if(!successorTupleRequired)return true;
  const host=row?.hostStart;if(!host||!Number.isInteger(host.epoch)||host.epoch<=0||typeof host.runId!=='string'||!host.runId||typeof host.hostInstanceId!=='string'||!host.hostInstanceId||!Number.isInteger(host.pid)||host.pid<=0||host.pipeClientPid!==host.pid||row.managedReceipt?.processId!==host.pid||row.currentAck?.persona!=='elite')return false;
  const bindings=(Array.isArray(row?.nativeEvents)?row.nativeEvents:[]).filter(event=>event?.event==='input-host-runtime-recovery'&&event.detail?.stage==='successor-bound').map(event=>event.detail);
  const created=bindings.findIndex(binding=>binding.reason==='same-recovery-period-actual-start-created');
  const hello=bindings.findIndex((binding,index)=>index>created&&binding.reason==='same-created-tuple-validated-hello');
  return created>=0&&hello>created&&bindings.every(binding=>binding.origin==='pre-host-native-writer-wait'&&binding.hostEpoch===row.hostStart?.epoch&&binding.runId===row.hostStart?.runId&&binding.persona===row.currentAck?.persona&&binding.recoveryRequestId===row.nativeWriterReturn?.newRecoveryRequest&&binding.requestId===row.nativeWriterReturn?.currentRequest&&binding.nativeWriterAttempt===row.nativeWriterReturn?.attempt&&binding.monitoring===true&&binding.spent===true&&binding.pending===false&&binding.queued===false&&binding.episodeAttempts===1&&binding.powerGeneration>0&&binding.powerGeneration===bindings[created].powerGeneration&&(binding.reason==='same-recovery-period-actual-start-created'?binding.hostInstanceId==='':binding.reason==='same-created-tuple-validated-hello'&&binding.hostInstanceId===row.hostStart?.hostInstanceId));
 };
 const exact=results.length===1&&results[0].mode==='full-pipe-core'&&results[0].exitCode===0&&!results[0].error&&!results[0].signal&&!results[0].timedOut&&report?.status==='PASS'&&report.forbiddenIo===0&&
  JSON.stringify(expected.expectedCases)===JSON.stringify(gpdExpiredFirstOnFullCycleSelection.expectedCases)&&JSON.stringify(expected.expectedCases)===JSON.stringify(actualCases)&&rows.length===2&&new Set(actualCases).size===2&&rows.every(row=>{
   const child=row?.managedReceipt;
   return currentSuccessorTuple(row)&&(!nativeWriterReturn||(row?.nativeWriterReturn?.realWriter===true&&row.nativeWriterReturn.syntheticCycle===false&&row.nativeWriterReturn.writerSessionReturned===true&&row.nativeWriterReturn.rootProcessCloseVerified===true&&row.nativeWriterReturn.observerAndJobCloseVerified===true&&row.nativeWriterReturn.newRecoveryRequest>0&&row.nativeWriterReturn.newRecoveryRequest!==row.nativeWriterReturn.currentRequest))&&row?.passed===true&&row.realManagedPipe===true&&row.completeCycleProven===true&&child?.nativeObservedProcessExit===true&&child.nativeObservedExitCode===0&&child.finalCleanupOk===true&&child.hasController===false&&child.hasContext===false;
  });
 if(!exact)throw Error('GPD_EXPIRED_FIRST_ON_FULL_CYCLE_EXPLICIT_SCOPE_MISMATCH');
 if(!stopReceipts.every(receipt=>receipt.accepted))throw Error('GPD_OWNED_STOP_RECEIPT_REJECTED:'+JSON.stringify(stopReceipts));
 selectedCaseScope={...expected,actualCases,exact,successorTupleRequired,stopReceipts,comparisonPolicy:'exact ordered canonical terminals plus complete real managed cycle, natural child resource return and owned local-only Stop receipts'};
}else if(wakeKeptSelected){
 const expected=wakeKeptScope(fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc'),'utf8')),actualCases=results[0]?.result?.cases?.map(row=>row.name);
 selectedCaseScope={...expected,actualCases,exact:results.length===1&&results[0].mode==='full-pipe-core'&&JSON.stringify(expected.expectedCases)===JSON.stringify(actualCases)};
 if(!selectedCaseScope.exact)throw Error('WAKE_KEPT_EXPLICIT_SCOPE_MISMATCH');
}else if(runtimeFaultSelection){
 const expectedCases=['runtime-after-final/'+runtimeFaultSelection[1]+'/'+runtimeFaultSelection[2]+(runtimeFaultSelection[3]?'/'+runtimeFaultSelection[3]:'')],actualCases=results[0]?.result?.cases?.map(row=>row.name);
 selectedCaseScope={selectedCase,expectedCases,actualCases,exact:results.length===1&&results[0].mode==='family-lifecycle'&&JSON.stringify(expectedCases)===JSON.stringify(actualCases)};
 if(!selectedCaseScope.exact)throw Error('RUNTIME_FAULT_EXPLICIT_SCOPE_MISMATCH');
}else if(['classified-fault-cleanup-elite','classified-fault-cleanup-ps'].includes(selectedCase)){
 const source=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc'),'utf8');
 const table=/classifiedFaultCleanupPersonas\[\]\s*=\s*\{([^}]+)\}/.exec(source);
 const variants=(table?[...table[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]):[]).filter(name=>name===(selectedCase.endsWith('-elite')?'elite':'dualsense-edge'));
 const expectedCases=variants.map(name=>'classified-fault-cleanup/'+name),actualCases=results[0]?.result?.cases?.map(row=>row.name);
 selectedCaseScope={selectedCase,expectedCases,actualCases,exact:variants.length===1&&new Set(variants).size===variants.length&&results.length===1&&results[0].mode==='family-lifecycle'&&JSON.stringify(expectedCases)===JSON.stringify(actualCases)};
 if(!selectedCaseScope.exact)throw Error('CLASSIFIED_CLEANUP_SCOPE_MISMATCH');
}else if(selectedCase==='owned-readback-cli-current'||selectedCase==='owned-readback-cli-boundary:host-journal'){
 const current=selectedCase==='owned-readback-cli-current',expectedCases=[current?'owned-readback-cli-current/current-owner':'owned-readback-cli-boundary/host-journal'];
 const rows=results[0]?.result?.cases??[],actualCases=rows.map(r=>r.name);
 const exact=results.length===1&&results[0].mode==='switch-recovery'&&JSON.stringify(actualCases)===JSON.stringify(expectedCases)&&rows.every(row=>{
  const b=row.cliBoundary;return b?.actualReconcile===true&&b.callbackCount===1&&JSON.stringify(b.commands)===JSON.stringify(['--dev-list','--cloak-state'])&&
   (!current||(row.passed===true&&b.oldEpoch===b.newEpoch&&b.oldPower===b.newPower&&b.oldTransaction===b.newTransaction&&b.repairAttemptsBefore===2&&b.repairAttemptsAfter===0&&b.physicalQueries===0&&b.fullQueries===0&&b.writes===0&&b.commandSequenceBefore===b.commandSequenceAfter));
 });
 selectedCaseScope={selectedCase,expectedCases,actualCases,exact,currentOwnerHealthControl:current,originalStalePairStillAvailable:true};
 fs.writeFileSync(path.join(out,'cli-single-selection-observed.json'),JSON.stringify(selectedCaseScope,null,2),{flag:'wx'});
 if(!exact)throw Error('CLI_SINGLE_EXACT_SCOPE_MISMATCH');
}else if(selectedCase==='owned-readback-cli-boundary'){
 selectedCaseScope=inspectCliBoundarySelection(results);
 fs.writeFileSync(path.join(out,'cli-boundary-selection-observed.json'),JSON.stringify(selectedCaseScope,null,2),{flag:'wx'});
 if(!selectedCaseScope.exact)throw Error('CLI_BOUNDARY_EXACT_SCOPE_MISMATCH');
}else if(prepareRejectionSelected){
 const producer=results[0],rows=producer?.result?.cases;
 const actualCases=Array.isArray(rows)?rows.map(row=>row?.name):[];
 const exact=results.length===1&&producer?.mode==='target-return'&&actualCases.length===1&&actualCases[0]===prepareRejectionCaseName;
 selectedCaseScope={selectedCase,expectedCases:[prepareRejectionCaseName],actualCases,exact,
  semanticPassed:exact&&producer.exitCode===0&&producer.result.status==='PASS'&&producer.result.forbiddenIo===0&&rows[0]?.passed===true};
 fs.writeFileSync(path.join(out,'prepare-rejection-selection-observed.json'),JSON.stringify(selectedCaseScope,null,2)+'\n',{flag:'wx'});
}else if(selectedCase&&!oneXParameterizedSelected&&!oemAfterOffSelected&&!["ps-return-root","ps-return-proof","ps-nopin-proof","ps-return-all"].includes(selectedCase)){
 const source=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8');
 const tableName=gpdPersonaMutexSelected?"gpdPersonaMutexVariants":selectedCase==="gpd-first-on-expired-wake"?"gpdFirstOnExpiredWakeVariants":selectedCase==="serial-config-action"?"serialConfigActionVariants":selectedCase==="serial-power-action"?"serialPowerActionVariants":selectedCase==="serial-exit-owner"?"serialExitOwnerVariants":selectedCase==="serial-entry-diagnostic"?"serialEntryDiagnosticVariants":selectedCase==="serial-thread-start"?"serialThreadStartVariants":selectedCase==="startup-exception-owner"?"startupExceptionOwnerVariants":selectedCase==="partial-start-cleanup-preflight"?"partialStartPreflightVariants":"partialStartOwnerVariants";
 const table=new RegExp(tableName+"\\[\\]\\s*=\\s*\\{([^}]+)\\}").exec(source);
 const variants=table?[...table[1].matchAll(/"([^"\r\n]+)"/g)].map(row=>row[1]):[];
 const expectedCases=variants.map(variant=>selectedCase+'/'+variant),actualCases=results[0]?.result?.cases?.map(row=>row.name);
 selectedCaseScope={selectedCase,expectedCases,actualCases,exact:variants.length>0&&new Set(variants).size===variants.length&&results.length===1&&results[0].mode==='switch-recovery'&&JSON.stringify(expectedCases)===JSON.stringify(actualCases)};
 if(!selectedCaseScope.exact)throw Error('EXPLICIT_QUEUED_EXCEPTION_SCOPE_MISMATCH');
 if(gpdPersonaMutexSelected){
  const expected=readGpdPersonaMutexSelection(source),rows=results[0]?.result?.cases??[];
  if(JSON.stringify(expected.expectedCases)!==JSON.stringify(actualCases)||rows.some(row=>!row.mutexProbe))throw Error('GPD_PERSONA_MUTEX_ACTUAL_PROBES_MISSING');
  selectedCaseScope.actualMutexReadObservation=true;
 }
 if(gpdR4CandidateBinding){
  const rows=results[0]?.result?.cases??[],helperCases=rows.filter(row=>row.helperProbe).map(row=>row.name),fakeTlsCases=rows.filter(row=>row.fakeTlsProbe).map(row=>row.name);
  const expectedHelpers=['pending-control','expired-first-on'].map(variant=>selectedCase+'/'+variant),expectedFakeTls=[selectedCase+'/expired-first-on'];
  const probeCoverage={compileDefinition:'YMCC_GPD_WAKE_R4_BOUNDARY_PROBES=1',actualHelperCases:helperCases,actualFakeTlsCases:fakeTlsCases,
   everySelectedRowCompiled:rows.every(row=>row.r4BoundaryProbesCompiled===true),exact:rows.every(row=>row.r4BoundaryProbesCompiled===true)&&JSON.stringify(helperCases)===JSON.stringify(expectedHelpers)&&JSON.stringify(fakeTlsCases)===JSON.stringify(expectedFakeTls)};
  selectedCaseScope.r4BoundaryProbeCoverage=probeCoverage;
  if(!probeCoverage.exact)throw Error('PRODUCT_CANDIDATE_ACTUAL_BOUNDARY_PROBES_MISSING');
 }
}
const lateHideBefore=suite==='late-hide-pending'&&!!productBefore;
const publicationBefore=suite==='late-hide-publication'&&!!productBefore;
const inflightBefore=suite==='late-hide-inflight'&&!!productBefore;
const orphanBefore=suite==='late-hide-orphaned-inflight'&&!!productBefore;
const expectedOrphanFailures=['retained late-hide marker without a running owner permits a fresh BUS notification','a new actual worker completes the episode after the diagnostic exception'];
const expectedInflightFailures=['same in-flight MSI episode reuses the worker token without enqueueing another hide','one MSI source episode has exactly one actual successful hide after in-flight BUS reentry'];
const expectedPublicationFailures=['accepted MSI late-hide work retains a drainable notification or completed receipt','accepted MSI notification executes without a second producer'];
const expectedToken0Failures=['empty-token0 worker preserves newly enqueued token1 pending','new token1 episode is consumed and completed exactly once'];
const expectedLateHideFailures=[...(productBeforeBinding&&!productBeforeBinding.defaultPendingEnabled?['empty leftover invokes no second hide','empty leftover preserves the completed real episode','completed real episode enqueue is refused','same real episode is never hidden twice']:[]),...expectedToken0Failures];
if(selectedToolchain)relocatedToolchain(repo,path.dirname(selectedDotnet));
if(applicationRequestBinding&&(sha(fs.readFileSync(applicationRequestBinding.path))!==applicationRequestBinding.sha256||sha(fs.readFileSync(applicationRequestBinding.snapshot))!==applicationRequestBinding.sha256))throw Error('APPLICATION_REQUEST_BYTES_CHANGED');
if(motionLibraryBinding){motionLibraryBinding.executedRuntimeCopies=applicationMatrixRows?results.map(row=>path.join(out,row.mode,'handheldcompanion-runtime/YMCC-DEVICECORE-20261018-v1/GamepadMotion.dll')):[motionLibraryBinding.privateRuntimeCopy];if([motionLibraryBinding.path,motionLibraryBinding.snapshot,...motionLibraryBinding.executedRuntimeCopies].some(file=>!fs.existsSync(file)||sha(fs.readFileSync(file))!==motionLibraryBinding.sha256)||sha(fs.readFileSync(motionLibraryBinding.index))!==motionLibraryBinding.indexSha256)throw Error('MOTION_LIBRARY_BYTES_CHANGED');}
if(["ps-return-root","ps-return-proof","ps-nopin-proof","ps-return-all"].includes(selectedCase)){
 const text=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/ps_return_cases.inc'),'utf8');
 const table=/psReturnParameters\[\]=\{([\s\S]*?)\n\};/.exec(text);if(!table)throw Error('PS_RETURN_PARAMETER_TABLE_MISSING');
 const proofCases=[...table[1].matchAll(/\{"(PS-RETURN\/[^"\r\n]+)"/g)].map(x=>x[1]);
 const ackText=fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc'),'utf8');
 const ack=/psNoPinCanonicalCaseName="([^"\r\n]+)"/.exec(ackText);if(!ack)throw Error('NO_PIN_CANONICAL_CASE_MISSING');
 const expectedCases=(selectedCase==='ps-nopin-proof'?[ack[1]]:selectedCase==='ps-return-all'?[ack[1],...proofCases]:proofCases.filter(x=>selectedCase!=='ps-return-root'||x.includes('/root-'))).sort();
 const actualCases=(results[0]?.result?.cases??[]).map(x=>x.name).sort();
 selectedCaseScope={selectedCase,expectedCases,actualCases,exact:expectedCases.length>0&&new Set(expectedCases).size===expectedCases.length&&results.length===1&&results[0].mode==='target-return'&&JSON.stringify(expectedCases)===JSON.stringify(actualCases)};
 if(!selectedCaseScope.exact)throw Error('EXPLICIT_PS_RETURN_SCOPE_MISMATCH');
}
const applicationMatrixSelection=applicationMatrixRows?{
 expected:applicationMatrixRows,actual:results.map(row=>({mode:row.mode,nativeMode:row.nativeMode,cases:row.result?.cases?.map(c=>({from:c.from,mode:c.mode,boot:c.boot}))})),
 notRun:applicationMatrixRows.slice(results.length),sharedRuntimeBudgetMs:45000,elapsedMs:performance.now()-applicationMatrixStarted,
 exactScope:results.length===16&&results.every((row,index)=>row.mode===applicationMatrixRows[index].runtimeMode&&row.nativeMode==='input-application'&&row.result?.cases?.length===1&&row.result.cases[0].from===applicationMatrixRows[index].from&&row.result.cases[0].mode===applicationMatrixRows[index].mode&&row.result.cases[0].boot===applicationMatrixRows[index].boot)
}:null;
const candidateChanged=productCandidateDrift();const candidateSourceGraph=productCandidateBinding?{manifest:path.join(out,'source-manifest.json'),manifestSha256:sha(fs.readFileSync(path.join(out,'source-manifest.json'))),records:records.length,observedIncludePaths:closure.observedIncludePaths.length,candidateRecords:records.filter(row=>selectedCandidateFiles.includes(row.path))}:null;const changed=records.filter(r=>sha(fs.readFileSync(r.original))!==r.sha256||sha(fs.readFileSync(r.snapshot))!==r.sha256).map(r=>r.path);if(managedProof&&(sha(fs.readFileSync(managedDll))!==managedProof.dll.sha256||sha(fs.readFileSync(managedProof.manifest))!==managedProof.manifestSha256))changed.push('managed-prerequisite');changed.push(...candidateChanged);const result={...(sourceWakeSelected?{nativeOnly:sourceReconfirmSelected,managed:wakeKeptSourceSelected}:{}),selectedCaseScope,gpdPersonaMutexObservation,executingConsumer,productCandidateBinding,sourceWakeSelection,candidateCoreAudit,candidateSourceGraph,status:sha(fs.readFileSync(expandedFile))===sourceExpansion.expandedSha256&&compiledGenerated.every(r=>sha(fs.readFileSync(r.path))===r.sha256)&&(!(prepareRejectionSelected||gpdPreHideSelected)||selectedCaseScope?.semanticPassed===true)&&(!familySelection||familySelection.exactScope)&&(!applicationMatrixSelection||applicationMatrixSelection.exactScope)&&results.every(r=>r.result&&r.result.forbiddenIo===0&&(orphanBefore?r.exitCode===1&&r.result.status==='FAIL'&&r.result.failures.length===expectedOrphanFailures.length&&expectedOrphanFailures.every(a=>r.result.failures.some(f=>f.assertion===a)):inflightBefore?r.exitCode===1&&r.result.status==='FAIL'&&r.result.failures.length===expectedInflightFailures.length&&expectedInflightFailures.every(a=>r.result.failures.some(f=>f.assertion===a)):publicationBefore?r.exitCode===1&&r.result.status==='FAIL'&&r.result.failures.length===expectedPublicationFailures.length&&expectedPublicationFailures.every(a=>r.result.failures.some(f=>f.assertion===a)):lateHideBefore?r.exitCode===1&&r.result.status==='FAIL'&&r.result.failures.length===expectedLateHideFailures.length&&expectedLateHideFailures.every(a=>r.result.failures.some(f=>f.assertion===a)):mutation?r.exitCode===1&&r.result.status==='FAIL'&&r.result.failures.some(f=>f.assertion===expectedMutationAssertion):(r.mode.startsWith('mutation')?r.exitCode===1&&r.result.status==='FAIL':r.exitCode===0&&r.result.status==='PASS')))&&changed.length===0?'PASS':'FAIL',results,changed,productionEdited:false,nativeCompilerConfiguration,mutation,suite,family,familySelection,reuseSuite,reuseBudgetMs,requestedModes:[...modes],selectedDotnet,applicationRequestBinding,motionLibraryBinding,applicationMatrixSelection,productBeforeBinding,expectedLateHideFailures:lateHideBefore?expectedLateHideFailures:[],expectedPublicationFailures:publicationBefore?expectedPublicationFailures:[],expectedInflightFailures:inflightBefore?expectedInflightFailures:[],expectedOrphanFailures:orphanBefore?expectedOrphanFailures:[],coldRecordBinding,oneCompiledCandidate:true,managedProof,managedPreparation,expandedSourceChanged:sha(fs.readFileSync(expandedFile))!==sourceExpansion.expandedSha256,compiledGeneratedChanged:compiledGenerated.filter(r=>sha(fs.readFileSync(r.path))!==r.sha256).map(r=>r.path),copiedButUncompiledChanged:closure.copiedButUncompiled.filter(r=>sha(fs.readFileSync(r.original))!==r.sha256).map(r=>r.path),sourceFullTranslationUnit:true,normalProductMainExecuted:false,realDeviceIo:false};fs.writeFileSync(path.join(out,'RESULT.json'),JSON.stringify(result,null,2));process.exitCode=result.status==='PASS'?0:1;
