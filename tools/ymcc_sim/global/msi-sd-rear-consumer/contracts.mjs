export const ID='controller.msi-sd-rear-consumer';
export const MODULE='tools/ymcc_sim/global/msi-sd-rear-consumer';
export const COMPLETION='MSI_SD_REAR_CONSUMER_V1_COMPLETED positive=78/67093 faults=5/5 DefaultRelease';
export const OUTER_TIMEOUT_MS=480000;
export const NODE_BUDGET_MS=450000;
export const PRODUCERS=Object.freeze([
 {path:'tools/hc_msi_rear_consumers_selftest.mjs',sha256:'179b4939c8d2c14836166854d7c5065ef05b65d866da61a84356d2ddcb051399'},
 {path:'tools/hc_msi_rear_consumers_source_fault_selftest.mjs',sha256:'b3bcd6de44fed08487231502adf8e19eade38b6fbe3a4e103d5eca8cdf43f11e'},
]);
export const FAULTS=Object.freeze([
 {id:'family-label-instead-of-exact-model',source:'capabilities/HcControllerConfigurationCapabilityCatalog.h',error:'changed model admitted'},
 {id:'drop-endpoint-size',source:'capabilities/HcControllerConfigurationCapabilityCatalog.h',error:'bad physical tuple admitted'},
 {id:'nearest-unknown-firmware',source:'capabilities/HcControllerConfigurationCapabilityCatalog.h',error:'unknown firmware admitted by total library'},
 {id:'wrong-report-route',source:'capabilities/HcControllerConfigurationCapabilityCatalog.h',error:'total factory changed packet'},
 {id:'sd-wrong-command-order',source:'native/main.cpp',error:'SD exact wire bytes differ'},
].map(Object.freeze));
export const EXCLUDED_FAULT=Object.freeze({id:'native-missing-mode-fence',reason:'experimentOnly: native keyboard remains disabled; explicitly excluded, not missing coverage'});
export const OVERLAY_FILES=Object.freeze([
 'native/main.cpp','native/msi_native_rear_keyboard_runtime.h','native/msi_native_rear_keyboard.h',
 'native/msi_rear_protocol.h','native/msi_rear_sd_preparation_receipt.h','native/msi_blocked_start_preparation.h',
 'capabilities/HcControllerConfigurationCapabilityCatalog.h','capabilities/HcControllerConfigurationCapabilityCatalog.generated.h',
 'tools/fixtures/hc_msi_rear_consumers/probe.cpp',
]);
export const BOUNDARY=Object.freeze({hardwareIo:false,physicalAcceptance:false,fullProductExecuted:false,
 nativeKeyboardExperimentExecuted:false,secondaryDisplayExecuted:false,automaticRestoreEstablished:false,
 actualDllLoadGraphEstablished:false,independentCompleteLinkTraceEstablished:false,
 toolAllowlistRole:'582 predeclared approved prerequisites; not actual links or loads',
 observedIncludesRole:'raw sourceDependencies checked against exact declared sources/tools and this-run generated/overlay files',
 jobOwnership:'global powershell descriptor uses existing host-process.ps1 gated kill-on-close Job; no independent Job-membership observation',
});
export const REMAINING_GAPS=Object.freeze([
 'Original producers have sourceDependencies but no independent complete link trace or DLL load graph.',
 'No whole Host/Main execution, physical MSI device IO, firmware/mapping acceptance, automatic restore, or CPU/power claim.',
 'Native keyboard experiment and secondary display remain disabled and unexecuted.',
 'Native exit and per-fault child exit are recorded by the pinned original producers; no new independent native PID observer.',
 'Suite success applies only to these original enabled consumer cases, not other models or untested consumers.',
]);
export function need(ok,code){if(!ok)throw Error('MSI_SD_'+code);}
export const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
export function requireNatural(call){
 need(call&&Number.isSafeInteger(call.pid)&&call.pid>0&&call.exitCode===0&&call.signal===null&&call.timedOut===false&&call.launchError===null&&call.outputTruncated===false&&call.stdoutComplete===true&&call.stderrComplete===true,'PRODUCER_NOT_NATURAL_COMPLETE_ZERO');
}
export function validateOriginal(result,fault=null){
 need(result?.schemaVersion===1&&result.status==='PASS'&&result.expectedFailure===(fault?.error??null),'ORIGINAL_RESULT');
 need(Array.isArray(result.sourceDrift)&&result.sourceDrift.length===0&&result.actualBeforeAfterConsumerBodies===true&&result.nativeKeyboardProductionLockUnchanged===true,'ORIGINAL_SOURCE_BOUNDARY');
 need(result.hardwareIo===false&&result.physicalAcceptance===false&&result.fullProductExecuted===false&&result.wholeProcessCpuReduction===null,'ORIGINAL_IO_BOUNDARY');
 need(Array.isArray(result.configurations)&&result.configurations.length===1,'ONE_CONFIGURATION_REQUIRED');
 const c=result.configurations[0];
 need(c.configuration==='DefaultRelease','DEFAULT_RELEASE_ONLY');
 need(c.compiled===true&&c.accepted===true&&c.signal===null&&c.exitCode===(fault?1:0),'COMPILED_NATURAL_NATIVE_EXIT');
 if(fault)need(c.report?.status==='FAIL'&&c.report.error===fault.error,'EXACT_SEMANTIC_ERROR');
 else need(c.report?.schemaVersion===1&&c.report.status==='PASS'&&c.report.cases===78&&c.report.checks===67093&&c.report.defaultNativeKeyboardWritesEnabled===false&&c.report.fixtureExperimentCompiled===false&&c.report.allHardwareFunctionsLinkedToMemoryFakes===true&&c.report.physicalAcceptance===false&&c.report.fullProductExecuted===false,'POSITIVE_78_67093_DEFAULT_LOCK');
 return c;
}
export function validateFaultSummary(result){
 need(result?.schemaVersion===1&&result.status==='PASS'&&result.hardwareIo===false&&result.physicalAcceptance===false&&Array.isArray(result.sourceDrift)&&result.sourceDrift.length===0,'FAULT_SUMMARY');
 need(Array.isArray(result.faults)&&result.faults.length===5,'FIVE_FAULTS_REQUIRED');
 need(new Set(result.faults.map(row=>row.id)).size===5,'DUPLICATE_FAULT_ID');
 result.faults.forEach((row,i)=>{
  const expected=FAULTS[i];need(row.id===expected.id&&row.source===expected.source&&row.expectedError===expected.error&&row.rejected===true,'EXACT_FAULT_ID_SOURCE_ERROR');
  need(Array.isArray(row.configurations)&&row.configurations.length===1,'FAULT_ONE_CONFIGURATION');
  const c=row.configurations[0];need(c.configuration==='DefaultRelease'&&c.compiled===true&&c.exitCode===1&&c.signal===null&&c.error===expected.error,'FAULT_COMPILED_SEMANTIC_REJECTION');
 });
 return result.faults;
}
/** Completeness/freshness, not a substitute for opening and hashing the pointed-to raw files. */
export function validateBundle(bundle,{runId,evidenceRoot}){
 need(bundle?.runId===runId&&bundle.evidenceRoot===evidenceRoot,'STALE_RECEIPT');
 need(bundle.positive?.validated===true&&bundle.faultSummary?.validated===true,'ORIGINAL_POSITIVE_AND_SUMMARY_REQUIRED');
 need(Array.isArray(bundle.faults)&&bundle.faults.length===5&&new Set(bundle.faults.map(x=>x.id)).size===5,'BUNDLE_FIVE_UNIQUE_FAULTS');
 need(bundle.faults.every((row,i)=>row.id===FAULTS[i].id&&row.validated===true),'BUNDLE_MISSING_FAULT');
 need(bundle.processes?.length===2,'TWO_ORIGINAL_PRODUCERS_REQUIRED');bundle.processes.forEach(requireNatural);
 need(bundle.inputsUnchanged===true&&bundle.toolsUnchanged===true,'IDENTITY_CHANGED');
}
export function validateToolSet(tools,actual){
 need(tools?.declaredOnly===true&&tools.files?.length===582&&new Set(tools.files.map(r=>r.path.toLowerCase())).size===582,'582_TOOLS_REQUIRED');
 need(actual?.length===582&&actual.every((row,i)=>row.status==='OK'&&row.sha256===tools.files[i].sha256&&row.path.toLowerCase()===tools.files[i].path.toLowerCase()),'TOOL_MISSING_OR_CHANGED');
}
