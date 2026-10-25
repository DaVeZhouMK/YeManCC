import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {compilerArtifacts} from '../controller-priority/adapter.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export function getControllerFeatureStatusAdapters(){return [{
 id:'controller.native-and-virtual-feature-status',domain:'controller',title:'Current capability-bound OEM source status, honest native blockers and retained virtual gyro presentation',
 kind:'node',path:'tools/controller_feature_status_selftest.mjs',args:['--out','{outDir}/features'],profiles:['desktop','full'],scope:'behavioral-mock',timeoutMs:180000,
 resultFiles:['features/result.json'],evidenceInputs:['tools/ymcc_sim/global/controller-feature-status/adapter.mjs','tools/controller_feature_status_selftest.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','native/main.cpp','native/msi_native_rear_keyboard.h','deps/json/json.hpp','src/bridge/controllerFeatureStatus.ts','src/bridge/controllerShortcutRules.ts','src/components/ControllerChannelStatus.vue','src/views/ButtonMappingView.vue','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1'],
 artifactInputs:[...compilerArtifacts(),process.execPath,'C:/Windows/System32/cmd.exe','C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'].map(p=>path.resolve(repo,p)),
}];}
