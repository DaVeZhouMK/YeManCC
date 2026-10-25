import assert from 'node:assert/strict';
export const ORIGINAL_ASSERTION="assert.deepEqual(['minimal','standard','detailed'].map(x=>Object.values(m.hudDataPresetFields(x)).filter(Boolean).length),[3,24,40])";
export function verifyMetadata(m,policy){
 assert.deepEqual(m.HUD_METRICS,policy.metrics,'approved full metric metadata');
 assert.deepEqual(m.HUD_DATA_PRESETS,policy.order,'approved stable preset IDs and labels');
 assert.equal(m.HUD_DEFAULT_DATA_PRESET,policy.defaultPreset,'approved default');
 for(const [name,ids]of Object.entries(policy.presets)){
  const expected=Object.fromEntries(policy.metrics.map(metric=>[metric.id,ids.includes(metric.id)]));
  assert.deepEqual(m.hudDataPresetFields(name),expected,'approved full field vector: '+name);
 }
 assert.equal(policy.presets.detailed.length,39);
 assert.deepEqual(policy.metrics.filter(metric=>!policy.presets.detailed.includes(metric.id)).map(metric=>metric.id),policy.excludedDetailed);
}
export function buildNewContract(original,policy){
 assert.equal(original.split(ORIGINAL_ASSERTION).length,2,'original metadata anchor changed');
 assert.equal(original.split('const root=').length,2,'original entry anchor changed');
 const prefix='const __approved='+JSON.stringify(policy)+';const __verifyFanMetadata='+verifyMetadata.toString().replace(/\r?\n/g,'')+';\n';
 const replacement='__verifyFanMetadata(m,__approved)';
 const adapted=original.replace('const root=',prefix+'const root=').replace(ORIGINAL_ASSERTION,replacement);
 assert.equal(adapted.replace(prefix,'').replace(replacement,ORIGINAL_ASSERTION),original,'original behavior assertions changed');
 return{adapted,prefix,replacement,originalAssertion:ORIGINAL_ASSERTION,originalAssertionsRecoverable:true,compatibility:'ORIGINAL_INCOMPATIBLE_METADATA; NEW_VERSIONED_CONTRACT'};
}