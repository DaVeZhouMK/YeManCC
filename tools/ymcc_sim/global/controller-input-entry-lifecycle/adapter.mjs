import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {entryToolchainFiles,entryProductSources} from '../../../fixtures/controller_input_entry_toolchain.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export function getControllerInputEntryAdapters(repo=root){return [{
 id:'controller.input-entry-lifecycle',domain:'controller',title:'Actual mirror and game-overlay callers through repository/api/ipc: protected replies, conflict and cancellation',
 kind:'node',path:'tools/controller_input_entry_lifecycle_selftest.mjs',args:['--out','{outDir}/input-entry'],timeoutMs:45000,profiles:['desktop','full'],scope:'behavioral-mock',
 evidenceInputs:['tools/controller_input_entry_lifecycle_selftest.mjs','tools/fixtures/controller_input_entry_lifecycle_fixture.mjs','tools/fixtures/controller_input_entry_toolchain.mjs','tools/ymcc_sim/global/controller-input-entry-lifecycle/adapter.mjs','tools/ymcc_sim/global/controller-input-entry-lifecycle/README.md','tools/ymcc_sim/global/controller-input-entry-lifecycle/toolchain-inputs.json',
 ...entryProductSources],
 artifactInputs:entryToolchainFiles(repo).map(r=>r.path),resultFiles:['input-entry/RESULT.json'],
 boundary:'13 actual frontend consumer cases; real repository/API/IPC parsing through a declared memory WebView native-reply endpoint. Game source/config read and scheduler are explicit seams. Native admission is not modeled or executed; its full-TU regression is separate. Three compiled production controls must fail named cases. No live app/device/driver/files, OS sleep or physical/game-consumption acceptance.'
}];}
