import {motionConsumerInputs,MODULE_ROOT,REPO_ROOT} from './inputs.mjs';
import {ADAPTER_TIMEOUT_MS} from './contracts.mjs';
/** Explicit specialist lane: 39 compiled faults, with no Host Main or real IO. */
export function getMotionConsumerAdapters(repoRoot=REPO_ROOT) {
 const inputs=motionConsumerInputs(repoRoot);
 return [{id:'gyro.machine-motion-consumers',domain:'gyro',title:'Original motion consumers, preserved legacy assertions and all 39 compiled semantic faults in Release/Checked plus matrix equivalence/four controls and mandatory primary-owner equivalence/five dual-configuration faults',kind:'powershell',path:MODULE_ROOT+'Run-MotionConsumers.ps1',args:['-EvidenceDir','{outDir}/m'],timeoutMs:ADAPTER_TIMEOUT_MS,profiles:['desktop','full'],scope:'behavioral-mock',artifactInputs:inputs.artifacts,resultFiles:['m/result.json','m/q/result.json','m/q/p/e/result.json',...Array.from({length:4},(_,i)=>'m/q/f'+i+'/e/result.json'),'m/o/result.json','m/o/p/e/result.json',...Array.from({length:5},(_,i)=>'m/o/f'+i+'/e/result.json')],evidenceInputs:inputs.sources},
 {id:'gyro.unified-motion-bindings',domain:'gyro',title:'Unified native recipe binding six compiled faults with actual Bound and pair consumers',kind:'node',path:'tools/hc_motion_binding_negative_selftest.mjs',args:['--out','{outDir}/b'],timeoutMs:300000,profiles:['desktop','full'],scope:'behavioral-mock',artifactInputs:inputs.artifacts,resultFiles:['b/result.json'],evidenceInputs:[...inputs.sources,'tools/hc_motion_binding_negative_selftest.mjs']}];
}
