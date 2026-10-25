import path from 'node:path';
import {SOURCE_MEMBERS,toolDeclaration} from '../../../fixtures/radial_private/scope.mjs';

export const MODULE='tools/ymcc_sim/global/monitor-notice-lifecycle';
export const NATIVE_ID='lifecycle.monitor-notice-native-lease';
// Quote closure of the original C++ fixture, excluding its generated handler include.
// Angle-included SDK/WebView2 headers and libraries belong to toolDeclaration.
export const NATIVE_QUOTE_SOURCES=Object.freeze([
 'deps/json/json.hpp','native/monitor_notice_log.h','native/monitor_desktop_hud.h','native/monitor_hud_drag.h',
 'native/monitor_hud_input_policy.h','native/monitor_hud_panel_input.h',
 'native/monitor_hud_placement.h','native/monitor_hud_recovery.h',
 'native/monitor_hud_serialization.h','native/monitor_hud_visibility.h',
 'native/screen_touchpad_input_surface.h','native/touch_haptic_dispatch.h',
 MODULE+'/native-lease-selftest.cpp',
]);
export const NATIVE_INPUTS=Object.freeze([...new Set([
 ...SOURCE_MEMBERS,...NATIVE_QUOTE_SOURCES,'native/main.cpp','tsconfig.json',
 'tools/ymcc_sim/global/capability-lifecycle/index.mjs',
 MODULE+'/native-lease-selftest.mjs',MODULE+'/native-adapter.mjs',
 MODULE+'/native-global.mjs',MODULE+'/README.md',
 'tools/ymcc_sim/global/extra-adapters.mjs','tools/ymcc_sim/global/extra-adapter-index.json',
])]);
export function getMonitorNoticeNativeLeaseAdapter(repoRoot=path.resolve(import.meta.dirname,'../../../..')){
 return {
  id:NATIVE_ID,domain:'lifecycle',kind:'node',
  title:'Persistent monitor notice host: original native lease exit component, 10 cases and exact single-guard negatives',
  path:MODULE+'/native-global.mjs',args:['--evidence-dir','{outDir}/monitor-notice-native-lease'],
  timeoutMs:600000,profiles:['desktop','full'],scope:'behavioral-mock',
  resultFiles:['monitor-notice-native-lease/result.json'],
  evidenceInputs:[...NATIVE_INPUTS],...toolDeclaration(repoRoot),
  lifecycleComponent:{
  "featureId": "monitor.notice-host",
  "persistence": "persistent",
  "role": "native-exit-component",
  "parentAdapterId": "lifecycle.monitor-notice-host",
  "fullPersistentAcceptance": false,
  "rationale": "The existing persistent notice host owns resident windows and timers. This adapter supplements its native lease admission/retirement and resource destruction; it does not create a separate one-shot feature or prove the complete persistent recovery contract.",
  "sourceInputs": [
    "native/main.cpp",
    "native/monitor_desktop_hud.h",
    "native/monitor_notice_log.h"
  ],
  "resources": [
    "resident-windows",
    "periodic-timers",
    "com-references"
  ],
  "evidenceFile": "monitor-notice-native-lease/result.json"
},
  nativeHandlerSource:{path:'native/main.cpp',start:'    ipc_on("monitorNotice.reserve"',endExclusive:'    ipc_on("monitorNotice.frame"',
   meaning:'The original selftest extracts reserve/create/retire handlers verbatim, records offsets and source/generated hashes, and executes that slice; full main is not compiled or started.'},
  boundary:'Reuses the existing native-lease-selftest.mjs/cpp with the exact production low-frequency diagnostic helper and radial_private/scope.mjs compiler/tool/source contracts. All original positive10 assertions and two single-guard mutations remain required. Only newly owned hidden HWND/timers and ABI TestController COM; null WebView2 environment makes both create paths reject before real creation. Original negative native-result.json files stay FAIL; only the separate exact-mask aggregate is PASS/FAIL. No installed YMCC/Steam, real WebView2/renderer, device, OS sleep or complete persistent recovery acceptance.',
 };
}