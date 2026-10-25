import {toolchainFiles} from '../inputhost-protocol/adapter.mjs';
export function getMsiSourceWindowAdapters(repo) {
 return [{id:'controller.msi-source-window',domain:'controller',title:'Actual MSI route decision honors the sole R3 player deadline and preserves independent wake ownership',kind:'node',
 path:'tools/msi_dinput_route_lifecycle_selftest.mjs',args:['--out','{outDir}/msi-source-window'],timeoutMs:180000,profiles:['desktop','full'],scope:'behavioral-mock',
 evidenceInputs:['native/main.cpp','native/virtual_start_protection.h','native/msi_virtual_wake.h','deps/json/json.hpp','tools/msi_dinput_route_lifecycle_selftest.mjs','tools/fixtures/msi_route_lifecycle/Fixture.cpp','tools/fixtures/msi_route_lifecycle/OriginalRouteWindow.cpp.txt','tools/fixtures/msi_route_lifecycle/OriginalRouteWindow.receipt.json','tools/fixtures/msi_owned_return_retired_io/source-binding.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/controller-msi-source-window/adapter.mjs','tools/ymcc_sim/global/controller-msi-source-window/README.md'],
 artifactInputs:toolchainFiles(repo),resultFiles:['msi-source-window/RESULT.json'],
 boundary:'Actual R3 authority, MSI route wrapper and ordinary-log registry; explicit memory globals/wake facts/log sink. Baseline wrapper is byte-identical to shipped R3. No full pump, SDK, driver, product Main, physical input or game acceptance.'}];
}
