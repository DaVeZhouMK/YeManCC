import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/controller-recovery-health';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASE_ID='controller.recovery-health';
export const PRODUCT_INPUTS=Object.freeze([
 'native/main.cpp','native/input_host_recovery_health.h','native/sleep_recovery_diagnostics.h',
 'native/virtual_output_status.h','native/input_frame_submission.h','native/legion_motion_source.h',
 'native/windows_motion_submission.h','native/touch_haptic_dispatch.h','native/game_feedback_receipt.h','deps/json/json.hpp',
 'tools/input_host_recovery_health_selftest.mjs','tools/fixtures/input_host_recovery_health_fixture.cpp',
 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'
]);
export function moduleSources(){return ['adapter.mjs','run.mjs','provenance.mjs','spec.json','toolchain-inputs.json','runner.test.mjs','README.md'].map(name=>MODULE+'/'+name);}
export function inputFiles(){return [...new Set([...moduleSources(),...PRODUCT_INPUTS,
 'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])].sort();}
export function toolchainSpec(repo=REPO){return JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8').replace(/^\uFEFF/,''));}
export function getControllerRecoveryHealthAdapters(repo=REPO){return [{
 id:CASE_ID,domain:'controller',title:'Actual native recovery-health consumer: 47 original scenarios, diagnostics on/off and stale-source helper rejection',
 kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/recovery-health'],timeoutMs:180000,
 profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['recovery-health/result.json'],
 evidenceInputs:inputFiles(),artifactInputs:toolchainSpec(repo).files.map(row=>row.path),
 boundary:'Unchanged original fixture and source-exact native Context/Source/Window/ACK/maintenance slices. Inert power, time, snapshot and validated-receipt boundaries only; no product Main, Host, SDK, pipe, actual reader, PnP, input device, recovery action, sleep or CPU acceptance.'
}];}
