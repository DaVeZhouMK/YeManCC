export const MAINTENANCE_CHILD_CASE_IDS=Object.freeze([
 'current','premature-complete','missing-consumer','missing-anchor','untracked-issue',
 'candidate-inactive-method-change','candidate-model-source-change','candidate-duplicate-device','candidate-operation-change',
 'candidate-battery-change','candidate-motion-change','candidate-private-motion-change','candidate-controller-change',
 'candidate-controller-motion-change','candidate-controller-feedback-change','candidate-tdp-policy-change',
 'missing-controller-stage','missing-controller-source','changed-controller-source-proof',
 'performance-cpu-activation','performance-gpu-activation','performance-tdp-takeover','performance-missing-model','performance-changed-source-proof',
 'performance-upstream-missing','performance-upstream-missing-model','performance-upstream-duplicate-model','performance-upstream-source-proof',
 'performance-upstream-cpu-inference','performance-upstream-gpu-inference','performance-upstream-cpu-activation','performance-upstream-tdp-takeover',
 'performance-upstream-commit','performance-upstream-facts-proof',
]);
export const MAINTENANCE_INLINE_CASES=Object.freeze({
 'scope-unknown-domain':'ACTIVE_INTEGRATION_SCOPE_UNKNOWN_DOMAIN:unreviewed',
 'scope-inactive-promotion':'ACTIVE_INTEGRATION_SCOPE_INACTIVE_DISPOSITION',
 'scope-excluded-enabled':'ACTIVE_INTEGRATION_SCOPE_ENABLED_ID:settings.wake-password-ui',
 'scope-ui-reimport':null,
 'scope-tdp-admitted':null,
 'scope-tdp-missing-decision':'ACTIVE_INTEGRATION_SCOPE_TDP_AUTHORIZATION',
 'scope-tdp-missing-contract':'ACTIVE_INTEGRATION_SCOPE_TDP_AUTHORIZATION',
 'scope-tdp-inactive-activation':'ACTIVE_INTEGRATION_SCOPE_TDP_BOUNDARIES',
 'scope-performance-not-tdp':'ACTIVE_INTEGRATION_SCOPE_PERFORMANCE_METADATA_ONLY',
 'scope-tdp-metadata-boundary':'ACTIVE_INTEGRATION_SCOPE_PERFORMANCE_METADATA_ONLY',
 'performance-pinned-file-corrupt':'PINNED_PERFORMANCE_FACTS_IDENTITY',
 'performance-pinned-file-and-self-declaration':'PINNED_PERFORMANCE_FACTS_IDENTITY',
});
export const MAINTENANCE_CASE_IDS=Object.freeze([...MAINTENANCE_CHILD_CASE_IDS,...Object.keys(MAINTENANCE_INLINE_CASES)]);
const positive=new Set(['current','candidate-inactive-method-change','candidate-model-source-change','candidate-operation-change','candidate-battery-change','candidate-motion-change','candidate-private-motion-change','candidate-controller-change','candidate-controller-motion-change','candidate-controller-feedback-change','candidate-tdp-policy-change']);
const isErrorList=value=>Array.isArray(value)&&value.every(error=>typeof error==='string'&&error.length>0);
export function validateMaintenanceResult(result){
 if(result?.status!=='PASS'||result.hardwareIo!==false||!Array.isArray(result.cases))throw Error('MAINTENANCE_ORIGINAL_RESULT_NOT_PASS');
 if(JSON.stringify(result.cases.map(x=>x?.id))!==JSON.stringify(MAINTENANCE_CASE_IDS))throw Error('MAINTENANCE_ORIGINAL_CASES_CHANGED');
 for(const row of result.cases){
  if(row.status!=='PASS')throw Error('MAINTENANCE_ORIGINAL_CASE_FAILED:'+row.id);
  if(Object.hasOwn(MAINTENANCE_INLINE_CASES,row.id)){
   if(Object.hasOwn(row,'executionExitCode')||row.error!==MAINTENANCE_INLINE_CASES[row.id])throw Error('MAINTENANCE_INLINE_VERDICT_CHANGED:'+row.id);
  }else if(row.executionExitCode!==(positive.has(row.id)?0:1)||!isErrorList(row.errors)||(row.errors.length===0)!==positive.has(row.id))throw Error('MAINTENANCE_CHILD_VERDICT_CHANGED:'+row.id);
 }
 return true;
}
export function validateMaintenanceChildResult(summary,raw){
 if(raw?.hardwareIo!==false||raw.hcAssemblyLoaded!==false||raw.sourceOrPolicyModified!==false||!Array.isArray(raw.sourceInputs)||!raw.sourceInputs.length)throw Error('MAINTENANCE_ORIGINAL_SCOPE_CHANGED:'+summary.id);
 if(raw.status!==(summary.executionExitCode===0?'PASS':'FAIL')||!isErrorList(raw.errors)||JSON.stringify(raw.errors)!==JSON.stringify(summary.errors))throw Error('MAINTENANCE_RAW_CHILD_VERDICT_CHANGED:'+summary.id);
}
