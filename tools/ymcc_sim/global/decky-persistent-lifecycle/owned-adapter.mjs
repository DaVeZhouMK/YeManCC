import {selectedOwnedTools,selectedNativeTools,HOST_ENVIRONMENT_SOURCE_INPUTS} from '../release-host-environment.mjs';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {lockBundledNodeTools,BUNDLED_AUTHORITY_SOURCES} from '../bundled-node-tool-authority.mjs';
import {getNativeLaunchNoIoToolDeclaration} from '../decky-update-recovery/native-launch-noio/index.mjs';
export const OWNED_ID='plugins.decky-owned-runtime-lifecycle';
export const OWNED_MODULE='tools/ymcc_sim/global/decky-persistent-lifecycle';
export const OWNED_SOURCES=Object.freeze([
 "native/main.cpp",
 "tools/decky_binding_runtime_selftest.cpp",
 "tools/decky_binding_client_selftest.cpp",
 "native/decky_binding_client.h",
 "native/decky_listener_proxy.h",
 "native/steam_listener_source.h",
 "native/steam_listener_executor.h",
 "native/decky_binding_worker.h",
 "native/decky_sidebar_bootstrap.h",
 "native/steam_live_cdp.h",
 "native/steam_command_coordinator.h",
 "native/steam_command_disposition.h",
 "native/steam_command_channel.h",
 "native/steam_endpoint_discovery.h",
 "native/steam_native_return_evidence.h",
 "deps/json/json.hpp",
 "native/steam_live_script.h",
 "native/decky_sidebar_async_http.h",
 "native/decky_sidebar_bootstrap_watch.h",
 "native/decky_sidebar_health.h",
 "native/decky_sidebar_frontend_update_script.h",
 "native/decky_sidebar_health_script.h",
 "native/decky_loader_identity.h",
 "native/steam_session_observer.h",
 "native/steam_session_registry_watch.h",
 "native/steam_startup_capture_diagnostic.h",
 "native/steam_ui_mode_observer.h",
 "native/steam_ui_mode_cache_miss.h",
 "native/steam_account_facts.h",
 "native/steam_window_event_budget.h",
 "native/decky_binding_process_owner.h",
 "native/decky_sidebar_runtime.h",
 "native/decky_sidebar_temp.h",
 "native/decky_steam_restart_requirement.h",
    "native/decky_sidebar_session.h",
    "native/decky_frontend_state.h",
 "native/decky_sidebar_launch.h",
 "native/process_efficiency.h",
 "native/decky_sidebar_recovery.h",
 "native/decky_context_cleanup_worker.h",
 "native/decky_resource_worker.h",
 "native/decky_bigpicture_recovery.h",
 "native/decky_sidebar_manual_repair.h",
 "native/decky_sidebar_resource_cache.h",
 "native/decky_sidebar_setup.h",
 "native/decky_update_recovery.h",
 "native/game_process_inventory.h",
 "native/process_inventory.h",
 "native/sleep_power_state.h",
 "native/steam_ui_epoch.h",
 "native/decky_sidebar_diagnostics.h",
 "native/process_handle_inheritance.h",
 "native/steam_cssloader_mode.h",
 "native/steam_failure_diagnostics.h",
 "native/steam_request_policy.h",
 "native/steam_source_owner.h",
 "native/worker_thread_name.h",
 "tools/fixtures/decky_binding_constructor_cases.inc",
 "tools/fixtures/decky_binding_constructor_probe.inc",
 "tools/fixtures/decky_initial_update_cases.inc",
 "tools/fixtures/decky_loader_native_close_cases.inc",
 "tools/fixtures/decky_loader_native_close_probe.inc",
 "tools/fixtures/decky_reader_retained_cases.inc",
 "tools/fixtures/decky_resource_preflight_cases.inc",
 "tools/fixtures/steam_session_observer_publication.mjs",
 "tools/ymcc_sim/global/decky-update-recovery/native-source-admission.mjs",
 "tools/decky_binding_runtime_selftest.mjs"
]);
export const OWNED_INPUTS=Object.freeze([...new Set([...OWNED_SOURCES,...HOST_ENVIRONMENT_SOURCE_INPUTS,...BUNDLED_AUTHORITY_SOURCES,'tools/fixtures/steam_session_observer_publication.mjs','tools/ymcc_sim/global/capability-lifecycle/index.mjs',OWNED_MODULE+'/owned-adapter.mjs',OWNED_MODULE+'/owned-run.mjs',OWNED_MODULE+'/typed-binding-gate.mjs',OWNED_MODULE+'/owned-tools.json',OWNED_MODULE+'/README.md','tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/index.mjs','tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'])]);
export function ownedToolDeclaration(repo){
 const native=getNativeLaunchNoIoToolDeclaration();if(native.blockedReason)return native;
 try{const pins=selectedOwnedTools().files;
  for(const row of pins){if(fs.realpathSync(row.path).toLowerCase()!==path.resolve(row.path).toLowerCase()||crypto.createHash('sha256').update(fs.readFileSync(row.path)).digest('hex')!==row.sha256)throw Error('OWNED_NATIVE_TOOL_CHANGED:'+row.path);}
  // The current typed UI consumer includes SDDL and pragma-links these SDK libs.
  // Resolve through the same selected include/lib search order before any child starts.
  // Keep this shared declaration complete for both direct and parent consumers.
  const selected=selectedNativeTools().native;
  const sdkInputs=[['sddl.h',selected.include],['source_location',selected.include],['iphlpapi.lib',selected.lib],['crypt32.lib',selected.lib]].map(([name,roots])=>{
   const file=roots.map(root=>path.resolve(root,name)).find(file=>fs.existsSync(file)&&fs.statSync(file).isFile());
   if(!file||fs.realpathSync(file).toLowerCase()!==path.resolve(file).toLowerCase())throw Error('OWNED_SELECTED_SDK_INPUT_MISSING_OR_REPARSED:'+name);
   return file;
  });
  const rows=[...new Set([...native.artifactInputs,...sdkInputs,...lockBundledNodeTools(repo).tools.map(r=>r.path),...pins.map(r=>r.path)])];if(rows.length>3072)throw Error('TOOL_LIMIT');return {artifactInputs:rows};
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
