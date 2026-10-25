import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
import {oracle} from '../../../fixtures/hc_model_motion_consumer/oracle.mjs';
import {machineModelInputs} from '../machine-capability-signals/adapter.mjs';
import {MATRIX_CHECKER,MATRIX_PROBE,MATRIX_TOOL_MANIFESTS,matrixToolchainInputs} from './matrix-evidence.mjs';
export const REPO_ROOT=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const MODULE_ROOT='tools/ymcc_sim/global/motion-consumer-signals/';
const fixedSources=[
 MATRIX_CHECKER,MATRIX_PROBE,...MATRIX_TOOL_MANIFESTS,
 'tools/ymcc_sim/global/execution-completion/index.mjs','tools/lib/hc_controller_motion_fixture.mjs',
 'tools/ymcc_sim/global/machine-capability-signals/adapter.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1',
 'tools/ymcc_sim/global/bundled-node-tool-authority.mjs','tools/ymcc_sim/global/snapshot-node-tools.mjs','tools/fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs','tools/ymcc_sim/global/source-snapshot.mjs',
 'tools/lib/hc_motion_compiler_trace.mjs','tools/ymcc_sim/global/motion-consumer-signals/original-compiler-trace.mjs','tools/ymcc_sim/global/motion-consumer-signals/original-compiler-trace-worker.mjs',
 'tools/hc_motion_primary_owner_selftest.mjs','tools/fixtures/hc_model_motion_consumer/primary-owner-contract.json','tools/fixtures/hc_model_motion_consumer/owner_binding_probe.cpp',
 'tools/hc_model_motion_consumer_selftest.mjs','tools/lib/hc_motion_mutation_pool.mjs','tools/fixtures/hc_model_motion_consumer/oracle.mjs','tools/fixtures/hc_model_motion_consumer/reviewed_resources.mjs',
 'tools/fixtures/hc_model_motion_consumer/README.md','tools/fixtures/hc_model_motion_consumer/harness.cpp','tools/fixtures/hc_model_motion_consumer/mocks.cpp','tools/fixtures/hc_model_motion_consumer/bridge.cpp',
 'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/fixtures/hc_remaining_model_capabilities/source-pins.json',
 'tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/lib/hc_motion_matrix_guard.mjs',
 'tools/fixtures/hc_legacy_motion_catalog/catalog.json','deps/handheldcompanion-runtime/HC-BASELINE.json','deps/handheldcompanion-runtime/source-files.sha256',
 'tools/fixtures/hc_model_motion_consumer/private-binding-contract.json','native/hc_oem_windows_motion_catalog.h','capabilities/hc-motion-policy.json','capabilities/HcMotionCapabilityCatalog.h','capabilities/HcMotionCapabilityCatalog.generated.h','tools/lib/hc_motion_catalog.mjs','tools/fixtures/hc_model_motion_consumer/unified-binding-contract.json','tools/fixtures/hc_model_motion_consumer/unified_binding.cpp',
 'native/main.cpp','native/hc_model_motion_catalog.h','native/hc_legacy_motion_catalog.h','native/hc_generic_motion_catalog.h','native/hc_default_motion_catalog.h','native/msi_motion_capabilities.h','deps/json/json.hpp',
 ...['adapter.mjs','contracts.mjs','inputs.mjs','validation.mjs','evidence.mjs','run.mjs','Run-MotionConsumers.ps1','matrix-evidence.mjs','matrix-runner.mjs','owner-evidence.mjs','owner-runner.mjs','owner-fault-scheduler.mjs','owner-child-trace.mjs'].map(name=>MODULE_ROOT+name),
];
/** All fixed upstream inputs are captured even when the oracle itself is missing. */
export function motionConsumerInputs(repoRoot=REPO_ROOT) {
 const repo=path.resolve(repoRoot),workspace=path.resolve(repo,'../../..'),model=machineModelInputs(repo),phase=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017');
 const generic=path.join(phase,'ControllerMotion/GenericWindowsMotion-20261018');
 const reviewed=path.join(phase,'ControllerMotion/CurrentCatalog/2026-10-17T10-53-19-524Z');
 const artifacts=[path.join(reviewed,'EFFECTIVE-HC-MATRICES.json'),path.join(reviewed,'RESULT.json'),...model.artifacts.filter(file=>file!==model.baseline),path.join(phase,'LegacyMotion/main.before.cpp'),path.join(phase,'MotionConsumer/main.before.cpp'),path.join(phase,'ControllerMotion/main.before.cpp'),path.join(phase,'pc80-identity/inputs.json'),path.join(generic,'ADAPTER-CONTRACT.json'),path.join(generic,'SOURCE-SCOPE.json'),path.join(generic,'NativeBuild-R1/YeManCC-source/YeManCC/native/main.cpp'),path.join(phase,'ControllerMotion/WindowsProcessorOwnership-20261018/before/native/main.cpp')];
 let expected=null,oracleProblem=null;try{expected=oracle(workspace,repo);}catch(error){oracleProblem={code:error.code??'ORACLE_REJECTED',message:String(error.message).slice(0,180)};}
 let tools=null,toolchainProblem=null;try{tools=matrixToolchainInputs(repo);}catch(error){toolchainProblem={code:error.message};}
 const sources=[...fixedSources];for(const row of expected?.legacy?.sourceChecks??[])if(typeof row.path==='string'&&!path.isAbsolute(row.path)&&!row.path.split(/[\\/]/).includes('..'))sources.push(row.path);
 return {repo,workspace,sources:[...new Set(sources)],artifacts:[...new Set([...artifacts,...(tools?.files??[]).map(row=>row.path)])],expected,oracleProblem,tools,toolchainProblem};
}
