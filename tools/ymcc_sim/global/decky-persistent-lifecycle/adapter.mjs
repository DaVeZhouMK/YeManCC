import {parentDeclaration as currentDeckyParentDeclaration} from './current-parent-global.mjs';
import path from 'node:path';
import {getDeckyUpdateRecoveryAdapters} from '../decky-update-recovery/adapter.mjs';
import {lifecycleContract} from './contract.mjs';
export {lifecycleContract};
export function getLegacyDeckyPersistentLifecycleAdapters(repoRoot=path.resolve(import.meta.dirname,'../../../..')){
 const existing=getDeckyUpdateRecoveryAdapters(repoRoot)[0];
 return [{
  id:'plugins.decky-persistent-lifecycle',domain:'plugins',title:'Decky persistent lifecycle: actual owners, committed Ready health, OFF/exit and bounded recovery',
  kind:'node',path:'tools/ymcc_sim/global/decky-persistent-lifecycle/run.mjs',
  args:['--evidence-dir','{outDir}/decky-persistent-lifecycle'],timeoutMs:480000,
  profiles:['desktop','full'],scope:'behavioral-mock',
  lifecycleContract,
  resultFiles:['decky-persistent-lifecycle/native/binding-cancel-results.json','decky-persistent-lifecycle/result.json','decky-persistent-lifecycle/native/historical-callback-negative-validation.json','decky-persistent-lifecycle/typed-runtime/typed-runtime-result.json','decky-persistent-lifecycle/production-binding-wiring.json','decky-persistent-lifecycle/consumer-result.json','decky-persistent-lifecycle/b/result.json'],
  evidenceInputs:[...new Set([...existing.evidenceInputs,
   'tools/ymcc_sim/global/capability-lifecycle/index.mjs',
   'tools/ymcc_sim/global/decky-persistent-lifecycle/adapter.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/contract.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/run.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/consumer-selftest.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/native-fixture.cpp','tools/ymcc_sim/global/decky-persistent-lifecycle/typed-binding-gate.mjs',
   'tools/decky_binding_runtime_selftest.cpp','tools/decky_binding_runtime_selftest.mjs','tools/decky_binding_client_selftest.cpp','tools/decky_binding_client_selftest.mjs','native/decky_binding_client.h','native/decky_binding_process_owner.h','native/decky_binding_worker.h','native/decky_binding_worker_entry.h',
   'tools/fixtures/decky_mirror_host_fixture.mjs','tools/fixtures/decky_game_memory_store.mjs',
   ...lifecycleContract.features[0].sourceInputs,
   'tools/decky_binding_cancel_selftest.cpp','tools/decky_sidebar_bootstrap_selftest.cpp','native/decky_sidebar_async_http.h','tools/decky_sidebar_runtime_selftest.cpp','tools/decky_sidebar_recovery_selftest.cpp','tools/decky_sidebar_crash_breaker_selftest.cpp','tools/decky_sidebar_health_close_selftest.cpp','tools/decky_sidebar_health_selftest.cpp',
   'tools/decky_sidebar_client_selftest.mjs','tools/decky_sidebar_host_selftest.mjs','tools/decky_sidebar_hang_recovery_selftest.mjs','tools/decky_sidebar_stability_matrix_selftest.mjs','tools/decky_loader_official_selftest.mjs','tools/decky_sidebar_safety_selftest.mjs'
  ])],
  ...(existing.artifactInputs?{artifactInputs:existing.artifactInputs}:{}),...(existing.blockedReason?{blockedReason:existing.blockedReason}:{}),
  boundary:'Standalone gate, no catalog/registry/runner edits. Executes fresh production typed Runtime/Client/Owner with source-built owned Loader/CEF/protocol peers, unchanged existing update recovery/sidebar regressions, and a byte-preserved historical callback negative. Current stalled-stop acceptance requires typed CV/descendant destruction plus original owned-Job fallback and JS exit; historical FAIL is never relabeled as product PASS. Power/IPC/socket/device boundaries are memory replays. No Steam, official Loader, installed YMCC, physical sleep or hardware execution. Product gaps remain FAIL.'
 }];
}


// Keep the original complete declaration for legacy-source review without recursing into the new wrapper.
export function getDeckyPersistentLifecycleAdapters(repoRoot=path.resolve(import.meta.dirname,'../../../..')){
 const legacy=getLegacyDeckyPersistentLifecycleAdapters(repoRoot)[0];
 const base={...legacy,path:'tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-global.mjs',args:['--mode','run','--evidence-dir','{outDir}/decky-persistent-lifecycle'],timeoutMs:480000,blockedExitCodes:[2],
  title:'Decky persistent lifecycle: current typed owners plus all original update and compatibility regressions',
  resultFiles:['result.json','SOURCE-PINS.json','TOOL-PINS.json','REGRESSION-COVERAGE.json','LEGACY-TYPED-MIGRATION.json','RAW-EVIDENCE.json','HISTORY.json'].map(file=>'decky-persistent-lifecycle/'+file),
  evidenceInputs:[...new Set([...legacy.evidenceInputs,'tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-global.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-native.mjs','tools/ymcc_sim/global/decky-persistent-lifecycle/current-parent-native.cpp','decky-plugin/vendor/decky-loader.lock.json'])],
  boundary:'Runs the actual current25 typed/native/TS lifecycle consumers and the unchanged original240s update baseline, official identity and compatibility regressions under one cumulative480s deadline. Current seven requirements never replace outstanding legacy-to-typed invariant migrations or the required owned runtime component. Historical FAILs and all raw failures remain. Memory OS/power and owned source-built inert peers only; no installed Steam/Loader, physical sleep, devices or deployment.'};
 try{const current=currentDeckyParentDeclaration(repoRoot);return [{...base,path:current.path,args:current.args,timeoutMs:current.timeoutMs,evidenceInputs:current.evidenceInputs,artifactInputs:current.artifactInputs,resultFiles:current.resultFiles,lifecycleContract:current.lifecycleContract}];}
 catch(error){return [{...base,blockedReason:'DECKY_CURRENT_PARENT_PREREQUISITE_UNAVAILABLE:'+error.message}];}
}
