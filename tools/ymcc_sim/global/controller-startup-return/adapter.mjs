import fs from 'node:fs';import path from 'node:path';import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/controller-startup-return';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASE_ID='controller.startup-return';
export const PRODUCT_INPUTS=Object.freeze([
 'native/main.cpp','native/msi_owned_return_runtime.h','native/msi_owned_return_recovery.h','native/virtual_output_status.h',
 'native/msi_crash_recovery_journal.h','native/msi_crash_recovery_runtime.h','native/xinput_identity_refresh_policy.h','native/touch_haptic_dispatch.h','deps/json/json.hpp',
 'tools/msi_owned_return_runtime_selftest.cpp','tools/verify-msi-owned-return-runtime.mjs','tools/verify-msi-startup-owned-return-runtime.mjs',
 'tools/fixtures/msi_startup_owned_return/boundaries.inc','tools/fixtures/msi_startup_owned_return/cases.inc',
 'native/external_controller_priority_ledger.h','tools/external_controller_priority_ledger_selftest.cpp','tools/external_controller_priority_ledger_selftest.ps1',
 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs',
 'tools/msi_crash_owner_liveness_selftest.mjs','tools/msi_crash_owner_liveness_selftest.cpp',
 'tools/msi_crash_record_io_selftest.mjs','tools/msi_crash_record_io_selftest.cpp',
 MODULE+'/extend-fixture.mjs'
]);
export const REFERENCE_INPUTS=Object.freeze(['native/external_controller_priority_runtime.h','native/msi_terminal_return_plan.h']);
export function moduleSources(){return ['adapter.mjs','run.mjs','provenance.mjs','extend-fixture.mjs','load-boundaries.inc','loader-cases.inc','spec.json','toolchain-inputs.json','runner.test.mjs','README.md'].map(name=>MODULE+'/'+name);}
export function sourceFiles(){return [...new Set([...moduleSources(),...PRODUCT_INPUTS,'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])].sort();}
export function toolchain(repo=REPO){return JSON.parse(fs.readFileSync(path.join(repo,MODULE,'toolchain-inputs.json'),'utf8').replace(/^\uFEFF/,''));}
export function getControllerStartupReturnAdapters(repo=REPO){return [{id:CASE_ID,domain:'controller',title:'Actual MSI cold OFF startup load/adoption/owned return, private record persistence and original external-priority ledger interruption',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/startup-return'],timeoutMs:360000,profiles:['desktop','full'],scope:'behavioral-mock',evidenceInputs:sourceFiles(),referenceInputs:[...REFERENCE_INPUTS],referenceMeaning:'Audit references only: full external-priority runtime and terminal plan are not executed by this adapter.',artifactInputs:toolchain(repo).files.map(row=>row.path),resultFiles:['startup-return/result.json'],boundary:'Actual native loader/initializer, original startup/owned consumers and strict parser with inert stat/read/liveness/PnP/transport/store boundaries. Original external ledger uses private real filesystem and its own fixture child; a separate exact-function owner sidecar uses real Win32 for current/own hidden child identity and natural exit, with labelled error leaves. A second exact-function sidecar performs real private MSI atomic write/bounded read/decode, sharing failure/retry and interruption of only its own writer before rename, plus explicit write/flush/rename leaves. No product Main, Host, pipe, device, driver, service, registry or OS sleep.'}];}
