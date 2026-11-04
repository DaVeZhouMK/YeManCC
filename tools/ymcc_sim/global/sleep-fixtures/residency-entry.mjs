// Bounded adapter for the existing native residency consumer. No duplicated model.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
import {toolchainSpec} from '../sleep-non-user-wake-consumer/adapter.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const activitySources=['native/sleep_activity_evidence.h','native/sleep_activity_evidence_native.inc','native/sleep_notification_evidence.h','tools/fixtures/sleep_residency/sleep_activity_native_harness.inc'];
const timeoutSources=['native/sleep_timeout_policy.h','native/sleep_timeout_native.inc','tools/fixtures/sleep_residency/sleep_timeout_native_harness.inc'];
const lateSources=['tools/fixtures/sleep_residency/latewake_cases.inc','tools/fixtures/sleep_non_user_wake/amd_rog_late_spontaneous_ac.json','native/sleep_abnormal_resleep.h'];
const sources=[...new Set([...activitySources,...timeoutSources,...lateSources,'native/sleep_export_diagnostics.h','native/sleep_export_diagnostics_native.inc','tools/fixtures/sleep_residency/sleep_export_harness.inc','native/sleep_abnormal_etw_native.inc','native/sleep_static_wake_policy.h','native/sleep_static_wake_receipt.h','native/sleep_static_wake_native.inc','native/sleep_static_wake_schedule.h','native/sleep_static_wake_schedule_native.inc','tools/fixtures/sleep_residency/static_wake_harness.inc','tools/fixtures/sleep_residency/static_wake_schedule_harness.inc','native/main.cpp','native/sleep_residency_evidence.h','native/sleep_abnormal_resleep.h','native/sleep_abnormal_resleep_native.inc','native/sleep_retry_budget.h','native/sleep_request_fallback.h','deps/json/json.hpp','tools/sleep_residency_evidence_selftest.cpp','tools/sleep_residency_evidence_selftest.py','tools/fixtures/sleep_residency/abnormal_native_harness.inc','tools/fixtures/sleep_residency/cases.json','tools/fixtures/sleep_residency/README.md','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/sleep.mjs','tools/ymcc_sim/global/sleep-fixtures/residency-entry.mjs','tools/ymcc_sim/global/sleep-non-user-wake-consumer/adapter.mjs','tools/ymcc_sim/global/sleep-non-user-wake-consumer/toolchain-inputs.json','tools/ymcc_sim/global/sleep-native-recovery-consumer/toolchain-inputs.json'])];
export function getResidencyAdapter(root,{identityOnly=false,componentScope=process.env.YMCC_SLEEP_RESIDENCY_COMPONENT_SCOPE??'all'}={}){if(!['all','timeout','late-wake'].includes(componentScope))throw Error('UNKNOWN_COMPONENT_SCOPE:'+componentScope);const spec=identityOnly?{files:[]}:toolchainSpec(root);return {...(identityOnly?{registrationIdentityOnly:true,blockedReason:'RESIDENCY_EXECUTION_IDENTITY_DEFERRED'}:{}),id:'sleep.residency-evidence-consumer',domain:'sleep',title:'Conservative sleep entries, completed507 evidence and bounded static scan costs with compiled faults',kind:'node',path:'tools/ymcc_sim/global/sleep-fixtures/residency-entry.mjs',args:['--output','{outDir}/residency',...(componentScope==='all'?[]:['--component-scope',componentScope])],profiles:['sleep','desktop','full'],scope:'behavioral-mock',timeoutMs:90000,resultFiles:['residency/result.json'],evidenceInputs:sources,artifactInputs:spec.files.map(row=>row.path),boundary:'Actual conservative production entries and completed507 capture; export-only WindowsSleepStudy helper and actual native adapter with inert command ports; staged engine/ETW retained as explicit historical regression; static query failure costs, exact localized empty-list parsing and one-minute cancellation. Mocked Windows APIs, no physical CPU/DRIPS acceptance. Existing compiler declaration and budgets reused; nineteen bounded executions; activity observer and actual timeout header/native/shared sender consumers compile/execute in none only; explicit component-scope late-wake runs current nonuser507/manual callback/sender and one monitor-only negative without other groups; explicit component-scope timeout selects the independent timeout group once with original ID/budget; elapsed policy requires original506Reason1 and makes no high-power claim; unchanged export companion executes in none only; eighteen explicit shared references each; static companions run in none and their three affected faults, fifteen scoped references; no parent duplication.'};}

// Re-read completed child artifacts without re-executing them. A caller which
// only imports this verifier must report a reference, never a new business PASS.
export function verifyLateWakeReceipt({dest,inputs,exact=false}) {
 const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
 const hash=file=>digest(fs.readFileSync(file));
 const need=(ok,code)=>{if(!ok)throw Error(code);};
 const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
 const receipt=read(path.join(dest,'receipt.json')), mutation=receipt.mutation;
 const fault=mutation==='late-wake-monitor-sender';
 need(receipt.status==='passed'&&receipt.compileExit===0&&receipt.runExit===(fault?1:0)&&receipt.realSleepTriggered===false&&receipt.sourceIdentityStable===true,'LATE_WAKE_CHILD_NOT_QUALIFIED');
 need(mutation==='none'||fault,'LATE_WAKE_UNRELATED_MUTATION');
 const main=inputs.find(row=>row.path==='native/main.cpp');need(main,'LATE_WAKE_MAIN_UNDECLARED');
 const current=new Map(inputs.map(row=>[row.path,row.sha256]));
 for(const row of receipt.sourceInputs??[])need(current.get(row.path)===row.sha256,'LATE_WAKE_PRODUCER_SOURCE_MISMATCH:'+row.path);
 const native=receipt.lateWakeNative;
 need(native&&native.executedThisRun===true&&native.actualPassedThisRun===!fault&&native.status===(fault?'EXPECTED_FAULT_CAUGHT':'EXECUTED'),'LATE_WAKE_COMPONENT_NOT_EXECUTED');
 need(JSON.stringify(native.inputs)===JSON.stringify(inputs.filter(row=>lateSources.includes(row.path))),'LATE_WAKE_INPUT_CLOSURE');
 const mainText=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replace(/^\uFEFF/,'').replace(/\r\n/g,'\n');
 const bytes=Buffer.from(mainText,'utf8');
 const allowed=new Set(['production_timeout_types.inc','production_timeout_bindings.inc','production_timeout_s4_hook.inc','production_late_wake_types.inc','production_late_wake_bindings.inc','production_late_wake_message.inc','production_late_wake_manual_callback.inc']);
 const seen=new Set();
 for(const binding of [receipt.sleepTimeoutNative?.binding,native.binding]){
  need(binding?.sourcePath==='native/main.cpp'&&binding.sourceSha256===main.sha256&&binding.normalization==='utf8-sig-universal-newlines'&&Array.isArray(binding.artifacts),'LATE_WAKE_SOURCE_BINDING');
  for(const artifact of binding.artifacts){
   need(allowed.has(artifact.path)&&!seen.has(artifact.path)&&Array.isArray(artifact.slices)&&artifact.slices.length>0,'LATE_WAKE_BINDING_ARTIFACT');seen.add(artifact.path);
   const chunks=artifact.slices.map(slice=>{need(Number.isInteger(slice.begin)&&Number.isInteger(slice.end)&&slice.begin>=0&&slice.end>slice.begin&&slice.end<=bytes.length,'LATE_WAKE_SLICE_RANGE');const text=bytes.subarray(slice.begin,slice.end).toString('utf8');need(digest(text)===slice.sha256,'LATE_WAKE_SOURCE_SLICE_CHANGED:'+slice.name);return text;});
   let expected=artifact.prefix+chunks.join(artifact.separator)+artifact.suffix;
   need(digest(expected)===artifact.sha256,'LATE_WAKE_BINDING_RECONSTRUCTION');
   if(fault&&artifact.path==='production_timeout_bindings.inc'){
    const anchor='const BOOL accepted=SetSuspendState(FALSE,FALSE,FALSE);';need(expected.split(anchor).length===2,'LATE_WAKE_FAULT_ANCHOR');
    expected=expected.replace(anchor,'const BOOL accepted=SendMessageTimeoutW(g_hwnd,WM_SYSCOMMAND,SC_MONITORPOWER,2,SMTO_ABORTIFHUNG,1000,nullptr)!=0;');
   }
   const actual=path.join(dest,artifact.path);need(fs.readFileSync(actual,'utf8')===expected&&receipt.compiledInputs?.some(row=>row.path===artifact.path&&row.sha256===digest(expected)),'LATE_WAKE_ACTUAL_COMPILED_BINDING');
  }
 }
 need(seen.size===allowed.size,'LATE_WAKE_BINDING_OMISSION');
 for(const row of receipt.compiledInputs??[]){need(path.basename(row.path)===row.path,'LATE_WAKE_COMPILED_PATH');need(hash(path.join(dest,row.path))===row.sha256,'LATE_WAKE_COMPILED_INPUT_CHANGED:'+row.path);}
 for(const name of [...timeoutSources,...lateSources])need(hash(path.join(dest,path.basename(name)))===current.get(name),'LATE_WAKE_COMPILED_INPUT_UNBOUND:'+name);
 const fixture=read(path.join(repo,lateSources[1]));
 need(native.provenance?.payloadSha256===current.get(lateSources[1])&&native.provenance.factRecordsChecked===59&&native.provenance.cases.length===3,'LATE_WAKE_PROVENANCE');
 const literalNames=[...fs.readFileSync(path.join(repo,lateSources[0]),'utf8').matchAll(/lateScenario\("([^"\n]+)"/g)].map(match=>match[1]);
 const expectedNames=fault?['rog-stable-ac-20min-control']:[...fixture.cases.map(row=>row.id),...literalNames];
 const raw=read(path.join(dest,'result.json')),cases=native.scenarios;
 need(Array.isArray(cases)&&cases.length>0&&new Set(expectedNames).size===expectedNames.length&&JSON.stringify(cases.map(row=>row.name))===JSON.stringify(expectedNames),'LATE_WAKE_EXACT_CASE_SELECTION');
 const reportedCases=exact?raw.caseResults:raw.lateWakeNativeScenarios;
 need(JSON.stringify(reportedCases)===JSON.stringify(cases)&&(!exact||raw.cases===cases.length)&&cases.every(row=>Number.isInteger(row.checks)&&row.checks>0),'LATE_WAKE_RAW_CASE_RESULTS');
 need(receipt.selection?.lateWakeExecutedOnce===true&&Number.isInteger(native.checks)&&native.checks>0,'LATE_WAKE_SELECTION_NOT_CONSUMED');
 if(fault){need(receipt.expectedFaultCaught===true&&raw.status==='failed'&&raw.failure==='CHECK_FAILED: late-wake shared sender requires tripleFALSE and zero monitor calls'&&cases.length===1&&cases[0].status==='failed'&&cases[0].monitorCalls===1&&cases[0].mockSuspendCalls===0,'LATE_WAKE_MONITOR_FAULT_NOT_DETECTED');}
 else {need(raw.status==='passed'&&raw.lateWakeNativeChecks===native.checks&&JSON.stringify(raw.lateWakeNativeScenarios)===JSON.stringify(cases)&&cases.every(row=>row.status==='passed'&&row.monitorCalls===0),'LATE_WAKE_POSITIVE_NOT_COMPLETE');}
 if(exact){
  need(receipt.componentScope==='late-wake'&&raw.componentScope==='late-wake'&&receipt.selection.timeoutExecutedOnce===false&&receipt.selection.otherResidencyGroupsExecuted===false,'LATE_WAKE_EXACT_SCOPE_LEAK');
  if(!fault)for(const field of ['sleepTimeoutNativeChecks','sleepActivityNativeChecks','sleepExportDiagnosticChecks','runtimeChecks','conservativeProductionChecks','historicalStagedEngineChecks','staticWakeChecks','staticWakeScheduleChecks'])need(raw[field]===0,'LATE_WAKE_UNSELECTED_GROUP_EXECUTED:'+field);
  need(receipt.sleepActivityEvidence?.status==='NOT_RUN_LATE_WAKE_SCOPE'&&receipt.sleepExportDiagnostics?.status==='NOT_RUN_LATE_WAKE_SCOPE'&&receipt.sleepTimeoutNative?.status==='NOT_RUN_LATE_WAKE_SCOPE','LATE_WAKE_UNSELECTED_RECEIPTS');
 }
 return {status:fault?'EXPECTED_FAULT_CAUGHT':'PASS_SELECTED_LATE_WAKE_SCOPE',scenarios:cases.length,checks:native.checks,caseNames:expectedNames,mockSuspendCalls:cases.reduce((sum,row)=>sum+row.mockSuspendCalls,0),monitorCalls:cases.reduce((sum,row)=>sum+row.monitorCalls,0),actualPassedThisRun:!fault,expectedFaultCaught:fault,sourceBindings:seen.size,provenance:native.provenance,realSleepTriggered:false};
}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 const at=process.argv.indexOf('--output');if(at<0||!process.argv[at+1])throw Error('EXPLICIT_OUTPUT_REQUIRED');
 const scopeAt=process.argv.indexOf('--component-scope'),requestedScope=scopeAt<0?(process.env.YMCC_SLEEP_RESIDENCY_COMPONENT_SCOPE??'all'):process.argv[scopeAt+1];
 if(!['all','timeout','late-wake'].includes(requestedScope))throw Error('UNKNOWN_COMPONENT_SCOPE:'+requestedScope);
 const out=path.resolve(process.argv[at+1]);if(out===repo||out.startsWith(repo+path.sep)||fs.existsSync(out))throw Error('FRESH_EXTERNAL_EVIDENCE_REQUIRED');
 fs.mkdirSync(out,{recursive:true});const begin=Date.now(),hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),tool=toolchainSpec(repo);
 const inputs=sources.map(p=>({path:p,sha256:hash(path.join(repo,p))}));const rows=[];let failure=null;
 try{
  for(const row of tool.files)if(hash(row.path)!==row.sha256)throw Error('TOOL_IDENTITY_CHANGED:'+row.path);
  for(const mutation of (requestedScope==='late-wake'?['none','late-wake-monitor-sender']:requestedScope==='timeout'?['none']:['none','hardware-zero-confirmed','missing-as-zero','callback-success-ignored','accepted-s4-shutdown','terminal-cleanup-removed','stale-evidence-permitted','previous-session-health-permitted','owned-intent-veto-lost','owned-budget-refilled','etw-loss-counters-ignored','etw-post-query-veto-ignored','etw-post-query-freshness-ignored','etw-terminal-health-dropped','stale-scan-start-accepted','conservative-arm-regressed','conservative-owned-regressed','global-snapshot-failure-retried','wake-armed-locale-regressed'])){
   const remaining=85000-(Date.now()-begin);if(remaining<1000)throw Error('CASE_BUDGET_EXHAUSTED');
   // The existing abnormal scope excludes unrelated static companions. Their
   // original positive and all three affected fault controls remain real runs.
   const componentScope=requestedScope==='late-wake'?'late-wake':requestedScope==='timeout'?'timeout':['none','stale-scan-start-accepted','global-snapshot-failure-retried','wake-armed-locale-regressed'].includes(mutation)?'all':'abnormal';
   const dest=path.join(out,'c'+rows.length),run=spawnSync(tool.pythonExecutable,['tools/sleep_residency_evidence_selftest.py','--output-dir',dest,'--mutation',mutation,'--component-scope',componentScope],{cwd:repo,env:{...process.env,COMSPEC:tool.cmdExecutable,PATH:path.dirname(tool.nodeExecutable)+';'+process.env.PATH},timeout:remaining,maxBuffer:2*1024*1024});
   fs.writeFileSync(path.join(out,mutation+'.stdout.log'),run.stdout??'');fs.writeFileSync(path.join(out,mutation+'.stderr.log'),run.stderr??'');
   if(run.error||run.signal||run.status!==0)throw Error('NATIVE_CONSUMER_FAILED:'+mutation+':'+(run.error?.message??run.status));
   const receiptPath=path.join(dest,'receipt.json'),receipt=JSON.parse(fs.readFileSync(receiptPath,'utf8'));
   if(receipt.status!=='passed'||receipt.realSleepTriggered!==false||(mutation!=='none'&&!receipt.expectedFaultCaught))throw Error('INVALID_CHILD_RECEIPT:'+mutation);
   if(componentScope==='late-wake'){
    const lateWakeNative=verifyLateWakeReceipt({dest,inputs,exact:true});
    rows.push({mutation,componentScope,receipt:receiptPath,sha256:hash(receiptPath),expectedFaultCaught:receipt.expectedFaultCaught,lateWakeNative});
    continue;
   }
   const component=receipt.sleepExportDiagnostics;
   if(!component||JSON.stringify(component.inputs)!==JSON.stringify(rows.length?rows[0].exportDiagnostics.inputs:component.inputs))throw Error('EXPORT_COMPANION_IDENTITY_MISMATCH:'+mutation);
   if(mutation==='none'&&componentScope!=='timeout'&&(component.status!=='EXECUTED'||!(component.checks>0)))throw Error('EXPORT_COMPANION_NOT_EXECUTED');
   if(mutation!=='none'&&component.status!=='NOT_RUN_MUTATION_SCOPE')throw Error('EXPORT_COMPANION_DUPLICATED');
   if(componentScope==='timeout'&&component.status!=='NOT_RUN_TIMEOUT_SCOPE')throw Error('EXPORT_UNSELECTED_GROUP_EXECUTED');
   const exportDiagnostics=componentScope==='timeout'?{...component,executedThisRun:false,actualPassedThisRun:false}:mutation==='none'?{...component,executedThisRun:true}:{status:'SHARED_REFERENCE',executedThisRun:false,inputs:component.inputs,reference:rows[0].receipt,referenceSha256:rows[0].sha256};
   const activity=receipt.sleepActivityEvidence;
   const expectedActivityInputs=inputs.filter(row=>activitySources.includes(row.path));
   if(!activity||JSON.stringify(activity.inputs)!==JSON.stringify(expectedActivityInputs))throw Error('ACTIVITY_COMPANION_IDENTITY_MISMATCH:'+mutation);
   if(activity.factHook?.sourcePath!=='native/main.cpp'||activity.factHook.sourceSha256!==inputs.find(row=>row.path==='native/main.cpp')?.sha256||!/^[a-f0-9]{64}$/.test(activity.factHook.sha256)||activity.factHook.path!=='production_activity_fact_hook.inc'||activity.factHook.fieldBindings?.length!==2)throw Error('ACTIVITY_PRODUCTION_HOOK_UNBOUND:'+mutation);
   if(mutation!=='none'&&JSON.stringify(activity.factHook)!==JSON.stringify(rows[0].sleepActivityEvidence.factHook))throw Error('ACTIVITY_PRODUCTION_HOOK_DRIFT:'+mutation);
   if(mutation==='none'&&componentScope!=='timeout'&&(activity.status!=='EXECUTED'||activity.executedThisRun!==true||activity.actualPassedThisRun!==true||!Number.isInteger(activity.checks)||!(activity.checks>0)))throw Error('ACTIVITY_COMPANION_NOT_EXECUTED');
   if(mutation!=='none'&&(activity.status!=='NOT_RUN_MUTATION_SCOPE'||activity.executedThisRun!==false||activity.actualPassedThisRun!==false))throw Error('ACTIVITY_COMPANION_DUPLICATED');
   if(mutation!=='none'&&(!rows[0]||rows[0].mutation!=='none'||rows[0].sleepActivityEvidence?.status!=='EXECUTED'||hash(rows[0].receipt)!==rows[0].sha256))throw Error('ACTIVITY_COMPANION_REFERENCE_INVALID');
   if(componentScope==='timeout'&&(activity.status!=='NOT_RUN_TIMEOUT_SCOPE'||activity.executedThisRun!==false||activity.actualPassedThisRun!==false))throw Error('ACTIVITY_UNSELECTED_GROUP_EXECUTED');
   const sleepActivityEvidence=componentScope==='timeout'?activity:mutation==='none'?activity:{status:'SHARED_REFERENCE',executedThisRun:false,actualPassedThisRun:false,inputs:activity.inputs,factHook:activity.factHook,reference:rows[0].receipt,referenceSha256:rows[0].sha256};
   const timeout=receipt.sleepTimeoutNative,expectedTimeoutInputs=inputs.filter(row=>timeoutSources.includes(row.path));
   if(!timeout||JSON.stringify(timeout.inputs)!==JSON.stringify(expectedTimeoutInputs))throw Error('TIMEOUT_COMPANION_IDENTITY_MISMATCH:'+mutation);
   const binding=timeout.binding;
   if(binding?.sourcePath!=='native/main.cpp'||binding.sourceSha256!==inputs.find(row=>row.path==='native/main.cpp')?.sha256||binding.normalization!=='utf8-sig-universal-newlines'||binding.artifacts?.length!==3)throw Error('TIMEOUT_BINDINGS_UNBOUND:'+mutation);
   const mainText=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8').replace(/^\uFEFF/,'').replace(/\r\n/g,'\n'),mainBytes=Buffer.from(mainText,'utf8'),digest=text=>createHash('sha256').update(text).digest('hex');
   if(new Set(binding.artifacts.map(item=>item.path)).size!==3)throw Error('TIMEOUT_DUPLICATE_BINDING_ARTIFACT');
   for(const artifact of binding.artifacts){
    if(!['production_timeout_types.inc','production_timeout_bindings.inc','production_timeout_s4_hook.inc'].includes(artifact.path)||!Array.isArray(artifact.slices)||!artifact.slices.length)throw Error('TIMEOUT_UNEXPECTED_BINDING_ARTIFACT');
    const chunks=artifact.slices.map(slice=>{if(!Number.isInteger(slice.begin)||!Number.isInteger(slice.end)||slice.begin<0||slice.end<=slice.begin||slice.end>mainBytes.length)throw Error('TIMEOUT_SLICE_RANGE');const text=mainBytes.subarray(slice.begin,slice.end).toString('utf8');if(digest(text)!==slice.sha256)throw Error('TIMEOUT_SLICE_SOURCE_CHANGED:'+slice.name);return text;});
    const expected=artifact.prefix+chunks.join(artifact.separator)+artifact.suffix;
    if(digest(expected)!==artifact.sha256)throw Error('TIMEOUT_BINDING_REBUILD_MISMATCH');
    if(mutation==='none'&&(fs.readFileSync(path.join(dest,artifact.path),'utf8')!==expected||receipt.compiledInputs?.find(row=>row.path===artifact.path)?.sha256!==artifact.sha256))throw Error('TIMEOUT_ACTUAL_COMPILED_BINDING_MISMATCH');
   }
   if(mutation==='none'){
    if(timeout.status!=='EXECUTED'||timeout.executedThisRun!==true||timeout.actualPassedThisRun!==true||!Number.isInteger(timeout.checks)||timeout.checks<=0||!Array.isArray(timeout.scenarios)||!timeout.scenarios.length||new Set(timeout.scenarios.map(row=>row.name)).size!==timeout.scenarios.length||timeout.scenarios.some(row=>row.status!=='passed'||!Number.isInteger(row.checks)||row.checks<=0))throw Error('TIMEOUT_COMPANION_NOT_EXECUTED');
    for(const row of expectedTimeoutInputs)if(hash(path.join(dest,path.basename(row.path)))!==row.sha256)throw Error('TIMEOUT_COMPILED_SOURCE_IDENTITY_MISMATCH');
    const raw=JSON.parse(fs.readFileSync(path.join(dest,'result.json'),'utf8'));
    if(raw.sleepTimeoutNativeChecks!==timeout.checks||JSON.stringify(raw.sleepTimeoutNativeScenarios)!==JSON.stringify(timeout.scenarios)||receipt.selection?.timeoutExecutedOnce!==true)throw Error('TIMEOUT_RAW_SELECTION_MISMATCH');
    if(componentScope==='timeout'&&(raw.componentScope!=='timeout'||raw.cases!==timeout.scenarios.length||JSON.stringify(raw.caseResults)!==JSON.stringify(timeout.scenarios)||['sleepActivityNativeChecks','sleepExportDiagnosticChecks','runtimeChecks','conservativeProductionChecks','historicalStagedEngineChecks','staticWakeChecks','staticWakeScheduleChecks'].some(field=>raw[field]!==0)||receipt.selection?.otherResidencyGroupsExecuted!==false))throw Error('TIMEOUT_EXACT_SCOPE_LEAKED_OR_OMITTED_CASES');
   }else if(timeout.status!=='NOT_RUN_MUTATION_SCOPE'||timeout.executedThisRun!==false||timeout.actualPassedThisRun!==false||!rows[0]||rows[0].sleepTimeoutNative?.status!=='EXECUTED'||JSON.stringify(binding)!==JSON.stringify(rows[0].sleepTimeoutNative.binding)||hash(rows[0].receipt)!==rows[0].sha256)throw Error('TIMEOUT_COMPANION_REFERENCE_INVALID');
   const sleepTimeoutNative=mutation==='none'?timeout:{status:'SHARED_REFERENCE',executedThisRun:false,actualPassedThisRun:false,inputs:timeout.inputs,binding,reference:rows[0].receipt,referenceSha256:rows[0].sha256};
   if(receipt.componentScope!==componentScope)throw Error('COMPONENT_SCOPE_NOT_CONSUMED:'+mutation);
   const staticCompanions=componentScope==='timeout'?{status:'NOT_RUN_TIMEOUT_SCOPE',executedThisRun:false,actualPassedThisRun:false}:componentScope==='all'?{status:'EXECUTED_OR_EXPECTED_FAULT',executedThisRun:true}:{status:'SHARED_REFERENCE',executedThisRun:false,reference:rows[0].receipt,referenceSha256:rows[0].sha256};
   const lateWakeNative=componentScope==='all'&&mutation==='none'
    ? verifyLateWakeReceipt({dest,inputs})
    : {status:'NOT_RUN_SELECTED_SCOPE',executedThisRun:false,actualPassedThisRun:false};
   rows.push({mutation,componentScope,receipt:receiptPath,sha256:hash(receiptPath),expectedFaultCaught:receipt.expectedFaultCaught,exportDiagnostics,sleepActivityEvidence,sleepTimeoutNative,staticCompanions,lateWakeNative});
  }
  for(const row of inputs)if(hash(path.join(repo,row.path))!==row.sha256)throw Error('SOURCE_CHANGED_DURING_RUN:'+row.path);
  for(const row of tool.files)if(hash(row.path)!==row.sha256)throw Error('TOOL_CHANGED_DURING_RUN:'+row.path);
 }catch(error){failure=String(error.message??error);}
 const passed=!failure,result={schemaVersion:1,id:'sleep.residency-evidence-consumer',status:passed?'PASS':'FAIL',passed,executedThisRun:true,elapsedMs:Date.now()-begin,componentScope:requestedScope,children:rows,sourceInputs:inputs,failure,realSleepTriggered:false,physicalAcceptance:false,scope:'Actual timeout policy/native include, current-plan action reader and shared sender/retry slices under inert Windows boundaries; timeout group executes only in none, exact timeout scope runs no other groups. Elapsed policy requires original506Reason1, not a physical high-power classifier. Conservative production entries and completed507 capture with mocked Windows boundaries; UI event-driven activity observer with inert OS accounting ports; staged ETW engine historical-only; static query/schedule costs bounded; physical CPU and sleep benefit unverified.'};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify({status:result.status,children:rows.length,elapsedMs:result.elapsedMs,failure}));if(!passed)process.exitCode=1;
}
