import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {compilerArtifacts} from '../controller-priority/adapter.mjs';
export const MODULE='tools/ymcc_sim/global/oem-capability-binding';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASE_ID='controller.unified-oem-input-binding';
export function evidenceSources(){
 return [...new Set([
  ...['adapter.mjs','output-evidence.mjs','fixture.mjs','run.mjs','Compile-Original.ps1'].map(name=>MODULE+'/'+name),
  'capabilities/HcOemInputCapabilityBinding.h','capabilities/hc-device-catalog.generated.h','capabilities/hc-device-catalog.json','native/main.cpp',
  'tools/lib/hc_oem_input_fixture.mjs','tools/lib/hc_motion_matrix_guard.mjs',
  'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs',
  'tools/ymcc_sim/global/controller-priority/adapter.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs',
 ])].sort();
}
export function artifactInputs(){
 const windows=process.env.SystemRoot||'C:/Windows';
 return [...new Set([...compilerArtifacts(),path.resolve(process.execPath),
  'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/native/powershell/pwsh.exe',
  path.join(windows,'System32/cmd.exe'),
  'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat',
 ])].map(file=>path.resolve(file));
}
export function getOemCapabilityBindingAdapters(){return [{
 id:CASE_ID,domain:'controller',title:'Actual unified OEM identity/profile/catalog helpers, gate fragment, key-up and semantic source faults',
 kind:'node',path:MODULE+'/output-evidence.mjs',args:['--evidence-dir','{outDir}/oem-binding'],
 profiles:['desktop','full'],scope:'behavioral-mock',timeoutMs:240000,
 evidenceInputs:evidenceSources(),artifactInputs:artifactInputs(),resultFiles:['oem-binding/result.json'],blockedExitCodes:[10],
}];}
