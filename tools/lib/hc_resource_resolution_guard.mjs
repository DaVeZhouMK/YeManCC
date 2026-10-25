import {validateHcMotionMatrix} from './hc_motion_matrix_guard.mjs';
// A resource-resolution gate only: exact model/source and runtime admission remain separate.
// No filename rewriting, resource loading, axis choice or device probing occurs here.
export function classifyHcResourceResolution(device,resources,loadedResource) {
 const all=[...resources],chain=[...new Set([device.name,...(device.chain||[])])];
 const candidates=all.filter(r=>chain.includes(r.config?.DeviceClass)).map(r=>({resource:r.relative,declaredClass:r.config.DeviceClass,lookupNameMatches:chain.some(n=>r.relative.endsWith('/'+n+'.json'))}));
 if(loadedResource){
  const loaded=all.find(r=>r.relative===loadedResource);
  const matrices=loaded?['GyroMatrix','AcceleroMatrix'].map(kind=>({kind,...validateHcMotionMatrix(loaded.config[kind])})):[];
  const valid=Boolean(loaded)&&matrices.every(r=>r.valid);
  return {status:'resource-loaded',loadedResource,mayImportLoadedMatrix:valid,mayPromoteDefaultAsModelMatrix:false,reviewRequired:!valid,candidates,matrices,scope:'resource-and-representability-only;not-physical-or-runtime-admission'};
 }
 return {status:candidates.length?'matching-declared-resource-not-loaded':'no-device-resource',loadedResource:null,mayImportLoadedMatrix:false,mayPromoteDefaultAsModelMatrix:false,explicitDefaultContractRequired:!candidates.length,reviewRequired:candidates.length>0,candidates,scope:'resource-and-representability-only;not-physical-or-runtime-admission'};
}
