import {selectedOwnedTools,HOST_ENVIRONMENT_SOURCE_INPUTS} from '../release-host-environment.mjs';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {lockBundledNodeTools,BUNDLED_AUTHORITY_SOURCES} from '../bundled-node-tool-authority.mjs';
import {getNativeLaunchNoIoToolDeclaration} from '../decky-update-recovery/native-launch-noio/index.mjs';
export const OWNED_ID='plugins.decky-owned-runtime-lifecycle';
export const OWNED_MODULE='tools/ymcc_sim/global/decky-persistent-lifecycle';
export const OWNED_SOURCES=Object.freeze([
 "tools/decky_binding_runtime_selftest.cpp",
 "tools/decky_binding_client_selftest.cpp",
 "native/decky_binding_client.h",
 "native/decky_binding_worker.h",
 "native/decky_sidebar_bootstrap.h",
 "native/steam_live_cdp.h",
 "deps/json/json.hpp",
 "native/steam_live_script.h",
 "native/decky_sidebar_async_http.h",
 "native/decky_sidebar_bootstrap_watch.h",
 "native/decky_sidebar_health.h",
 "native/decky_sidebar_frontend_update_script.h",
 "native/decky_sidebar_health_script.h",
 "native/decky_loader_identity.h",
 "native/steam_session_observer.h",
 "native/steam_window_event_budget.h",
 "native/decky_binding_process_owner.h",
 "native/decky_sidebar_runtime.h",
 "native/decky_sidebar_temp.h",
 "native/decky_sidebar_launch.h",
 "native/process_efficiency.h",
 "native/decky_sidebar_recovery.h",
 "tools/decky_binding_runtime_selftest.mjs"
]);
export const OWNED_INPUTS=Object.freeze([...new Set([...OWNED_SOURCES,...HOST_ENVIRONMENT_SOURCE_INPUTS,...BUNDLED_AUTHORITY_SOURCES,'tools/ymcc_sim/global/capability-lifecycle/index.mjs',OWNED_MODULE+'/owned-adapter.mjs',OWNED_MODULE+'/owned-run.mjs',OWNED_MODULE+'/owned-tools.json',OWNED_MODULE+'/README.md','tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/index.mjs','tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'])]);
export function ownedToolDeclaration(repo){
 const native=getNativeLaunchNoIoToolDeclaration();if(native.blockedReason)return native;
 try{const pins=selectedOwnedTools().files;
  for(const row of pins){if(fs.realpathSync(row.path).toLowerCase()!==path.resolve(row.path).toLowerCase()||crypto.createHash('sha256').update(fs.readFileSync(row.path)).digest('hex')!==row.sha256)throw Error('OWNED_NATIVE_TOOL_CHANGED:'+row.path);}
  const rows=[...new Set([...native.artifactInputs,...lockBundledNodeTools(repo).tools.map(r=>r.path),...pins.map(r=>r.path)])];if(rows.length>3072)throw Error('TOOL_LIMIT');return {artifactInputs:rows};
 }catch(error){return {artifactInputs:[],blockedReason:'OWNED_RUNTIME_TOOL_DECLARATION:'+error.message};}
}
export function getDeckyOwnedRuntimeLifecycleAdapter(repo=path.resolve(import.meta.dirname,'../../../..')){return {
 id:OWNED_ID,domain:'plugins',kind:'node',title:'Persistent Decky: current production Runtime/Client/Owner destruction and stalled owned binding component',
 path:OWNED_MODULE+'/owned-run.mjs',args:['--evidence-dir','{outDir}/decky-owned-runtime-lifecycle'],timeoutMs:150000,profiles:['desktop','full'],scope:'behavioral-mock',
 resultFiles:['decky-owned-runtime-lifecycle/result.json'],evidenceInputs:[...OWNED_INPUTS],...ownedToolDeclaration(repo),
 lifecycleComponent:{
  "featureId": "plugins.decky",
  "persistence": "persistent",
  "role": "current-native-owned-binding-component",
  "parentAdapterId": "plugins.decky-persistent-lifecycle",
  "fullPersistentAcceptance": false,
  "rationale": "Production Runtime and Client retain an owned Job, child processes, worker and pending IPC across binding, power and stop events; this test measures only their current native component.",
  "sourceInputs": [
    "native/decky_sidebar_runtime.h",
    "native/decky_binding_client.h",
    "native/decky_binding_process_owner.h"
  ],
  "resources": [
    "resident-processes",
    "workers",
    "handles",
    "pending-io"
  ],
  "evidenceFile": "decky-owned-runtime-lifecycle/result.json"
},
 boundary:'Runs the unchanged existing typed Runtime/Client/Owner selftest with its original 3200ms deadline, strict process handle equality, no-extra-reader-thread check and release-after-measurement. Real source-built inert Loader, CEF and protocol child only. Original FAILs remain FAIL; this component does not replace old legacy regression, TS current-health recovery or full lifecycle acceptance. No installed app, real Steam/bootstrap/Loader, device, OS sleep or package acceptance.',
};}
