import path from 'node:path';
import {motionConsumerDeclarations,REPO_ROOT} from '../motion-consumer-signals/inputs.mjs';
export function getCapabilityHotPathAdapters(repo=REPO_ROOT){
 const shared=motionConsumerDeclarations(repo);
 // Generation reads this plan to authenticate the already declared processor preimage.
 const processorPlan=path.join(shared.workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/WindowsProcessorOwnership-20261018/PLAN.json');
 return [{id:'gyro.recipe-dispatch-hotpath',domain:'gyro',title:'Actual generic motion selector hot path, all identity invalidations and five compiled cache defects',kind:'powershell',path:'tools/ymcc_sim/global/capability-hotpaths/Run-MotionHotPath.ps1',args:['-EvidenceDir','{outDir}/hotpath'],timeoutMs:240000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['hotpath/micro/result.json','hotpath/negative/result.json'],evidenceInputs:[...new Set([...shared.sources,'tools/ymcc_sim/global/capability-hotpaths/adapter.mjs','tools/ymcc_sim/global/capability-hotpaths/Run-MotionHotPath.ps1','tools/hc_generic_motion_hotpath_selftest.mjs','tools/hc_generic_motion_cache_negative_selftest.mjs','tools/fixtures/hc_capability_hotpaths/generic-recipe-before.inc'])],artifactInputs:[...new Set([...shared.artifacts,processorPlan])]}];
}
