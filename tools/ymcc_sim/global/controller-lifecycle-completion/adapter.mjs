import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installedTools as compilerTools} from '../controller-priority-ledger/adapter.mjs';
export const MODULE='tools/ymcc_sim/global/controller-lifecycle-completion';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASE_ID='controller.lifecycle-completion';
export const INPUTS=['native/main.cpp','native/input_host_lifecycle_claim.h','tools/input_host_lifecycle_completion_selftest.mjs','tools/fixtures/input_host_lifecycle_completion/fixture.cpp','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/lib/hc_motion_matrix_guard.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','deps/json/json.hpp'];
export const INFRASTRUCTURE=[MODULE+'/adapter.mjs',MODULE+'/run.mjs',MODULE+'/tool-inputs.json','tools/ymcc_sim/global/controller-priority-ledger/adapter.mjs','tools/ymcc_sim/global/controller-priority-ledger/tool-inputs.json','tools/ymcc_sim/global/controller-priority/adapter.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'];
export function installedTools(){const spec=JSON.parse(fs.readFileSync(path.join(REPO,MODULE,'tool-inputs.json'),'utf8'));return [...new Set([...compilerTools(),...spec.files])];}
export function getControllerLifecycleCompletionAdapters(){return [{
 id:CASE_ID,domain:'controller',title:'Actual native lifecycle ownership, queued cleanup and exception completion with semantic fault controls',
 kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/lifecycle'],timeoutMs:180000,profiles:['desktop','full'],scope:'behavioral-mock',
 evidenceInputs:[...INPUTS,...INFRASTRUCTURE],artifactInputs:installedTools(),resultFiles:['lifecycle/result.json'],
 boundary:'Exact queue/snapshot/scope code. Device dispatch and OS endpoints are inert; no product/Host/driver start or physical return claim.'
}];}
