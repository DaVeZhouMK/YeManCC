import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {matrixToolchainInputs,MATRIX_TOOL_MANIFESTS} from '../motion-consumer-signals/matrix-evidence.mjs';
import {MODULE,PRODUCERS,OVERLAY_FILES,need} from './contracts.mjs';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const RUNTIME_FILES=Object.freeze(['adapter.mjs','inputs.mjs','contracts.mjs','evidence.mjs','run.mjs','Run-MsiSdRear.ps1']);
export const HC_SOURCE_FILES=Object.freeze(['Devices/MSI/ClawA1M.cs','Devices/MSI/ClawA2VM.cs','Devices/MSI/ClawBZ2EM.cs','Devices/MSI/ClawCG3EM.cs','Devices/MSI/DeviceVersion.cs','Devices/IDevice.cs']);
export const BASELINE='tools/fixtures/hc_msi_rear_consumers/before';
const fixed=[
 ...RUNTIME_FILES.map(name=>MODULE+'/'+name),...PRODUCERS.map(row=>row.path),...OVERLAY_FILES,
 BASELINE+'/BASELINE.json',BASELINE+'/native/main.cpp',BASELINE+'/native/msi_native_rear_keyboard_runtime.h',
 'deps/json/json.hpp','capabilities/hc-controller-configuration-policy.json',
 'tools/hc_capability_catalog_generate.mjs','tools/lib/hc_controller_configuration_catalog.mjs',
 'deps/handheldcompanion-runtime/HC-BASELINE.json','deps/handheldcompanion-runtime/source-files.sha256',
 ...HC_SOURCE_FILES.map(name=>'deps/handheldcompanion-runtime/source/'+name),
 'tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs',
 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs',
 'tools/ymcc_sim/global/motion-consumer-signals/matrix-evidence.mjs',
 'tools/ymcc_sim/global/motion-consumer-signals/evidence.mjs','tools/ymcc_sim/global/motion-consumer-signals/contracts.mjs',
 'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/lib/hc_motion_matrix_guard.mjs',
 ...MATRIX_TOOL_MANIFESTS,'tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs',
 'tools/ymcc_sim/global/execution-completion/index.mjs',
];
export function sourceFiles(){return [...new Set(fixed)];}
export function inputs(repo=REPO){
 const tools=matrixToolchainInputs(repo);
 const base=JSON.parse(fs.readFileSync(path.join(repo,MATRIX_TOOL_MANIFESTS.at(-1)),'utf8'));
 need(MATRIX_TOOL_MANIFESTS.length===6&&tools.files.length===582,'APPROVED_TOOL_CLOSURE');
 need(typeof base.powershellExecutable==='string','POWERSHELL_DECLARATION_REQUIRED');
 return {repo:path.resolve(repo),sources:sourceFiles(),tools,powershellExecutable:base.powershellExecutable,manifests:[...MATRIX_TOOL_MANIFESTS]};
}
