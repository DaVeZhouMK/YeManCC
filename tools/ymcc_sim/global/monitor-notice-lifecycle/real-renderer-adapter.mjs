import fs from 'node:fs';
import path from 'node:path';
import {SOURCE_MEMBERS,toolDeclaration,LIMIT,sha} from '../../../fixtures/radial_private/scope.mjs';
import {LIFECYCLE_GATE_SOURCES,normalizeLifecycleComponent} from '../capability-lifecycle/index.mjs';

export const MODULE='tools/ymcc_sim/global/monitor-notice-lifecycle';
export const REAL_RENDERER_ID='lifecycle.monitor-notice-real-renderer';
export const ORIGINAL_ENTRIES=Object.freeze([MODULE+'/real-renderer-selftest.mjs',MODULE+'/real-renderer-negative-selftest.mjs']);
export const FIXED_NODE='G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe';
export const FIXED_RUNTIME='C:/Program Files (x86)/Microsoft/EdgeWebView/Application/155.0.4283.62';
export const RUNTIME_VERSION='155.0.4283.62';
export const PRODUCT_RENDERER_INPUTS=Object.freeze([
 'src/monitorNoticeOutputEntry.ts','src/components/MonitorShortcutNotice.vue',
 'src/bridge/ipc.ts','src/bridge/shortcutActionFeedback.ts','src/bridge/frameRateModel.ts',
 'src/bridge/monitorShortcutNotice.ts','src/bridge/monitorHudLayoutLane.ts',
 'src/bridge/monitorNativeDemand.ts','src/bridge/monitorFrameTime.ts','src/bridge/monitorCoreTiles.ts',
 'src/bridge/monitorSnapshotSignals.ts','src/bridge/monitorHudFpsVisibility.ts',
 'src/bridge/monitorHudFreshness.ts','src/bridge/monitorHudModel.ts','src/bridge/monitorHudProtocol.ts',
 'src/bridge/monitorHudViewport.ts','src/bridge/monitorNoticePlacement.ts',
 'src/styles/tokens.css','src/styles/monitorHudOutput.css',
]);
export const NATIVE_OWNER_INPUTS=Object.freeze([
 'native/monitor_desktop_hud.h','native/monitor_hud_drag.h','native/monitor_hud_input_policy.h',
 'native/monitor_hud_panel_input.h','native/monitor_hud_placement.h','native/monitor_hud_recovery.h',
 'native/monitor_hud_serialization.h','native/monitor_hud_visibility.h',
 'native/screen_touchpad_input_surface.h','native/touch_haptic_dispatch.h',
]);
export const RENDERER_INPUTS=Object.freeze([...new Set([
 ...SOURCE_MEMBERS,...LIFECYCLE_GATE_SOURCES,...PRODUCT_RENDERER_INPUTS,...NATIVE_OWNER_INPUTS,
 ...ORIGINAL_ENTRIES,'notice-output.html','deps/json/json.hpp','tsconfig.json',
 'deps/webview2/build/native/include/WebView2.h','deps/webview2/build/native/include/WebView2EnvironmentOptions.h',
 MODULE+'/real-webview-selftest.cpp',MODULE+'/real-renderer-checks.h',
 MODULE+'/real-renderer-adapter.mjs',MODULE+'/real-renderer-global.mjs',
])]);
export const RENDERER_COMPONENT=Object.freeze(normalizeLifecycleComponent({
 featureId:'monitor.notice-host',parentAdapterId:'lifecycle.monitor-notice-host',
 persistence:'persistent',role:'real-product-vue-renderer-component',fullPersistentAcceptance:false,
 rationale:'The persistent notice host owns the production dedicated Vue entry, IPC event subscriptions, expiry and animation callbacks, layout lane, ResizeObserver, native windows, COM controllers and browser. This component preserves the original DOM/IPC/pagehide and nine-controller retirement tests; complete parent persistent exit and recovery remain separately required.',
 sourceInputs:[...PRODUCT_RENDERER_INPUTS,...NATIVE_OWNER_INPUTS],
 resources:['resident-windows','resident-processes','com-references','async-callbacks','event-hooks',
  'subscriptions','periodic-timers','animation-frames','resize-observers','dom-mounts','layout-lanes','ipc-pending-requests','owned-jobs','private-desktops'],
 evidenceFile:'monitor-notice-real-renderer/result.json',
},REAL_RENDERER_ID));

const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const unique=files=>[...new Map(files.map(file=>[path.resolve(file).toLowerCase(),path.resolve(file)])).values()];
const inside=(root,file)=>{const rel=path.relative(path.resolve(root),path.resolve(file));return rel!==''&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
export function rendererReviewBinding(env=process.env){
 const scoped=env.YMCC_MONITOR_RENDERER_EXECUTION_REVIEW!==undefined||env.YMCC_MONITOR_RENDERER_EXECUTION_REVIEW_SHA256!==undefined;
 return scoped?{file:env.YMCC_MONITOR_RENDERER_EXECUTION_REVIEW,pin:env.YMCC_MONITOR_RENDERER_EXECUTION_REVIEW_SHA256,scoped:true}:{file:env.YMCC_RADIAL_EXECUTION_REVIEW,pin:env.YMCC_RADIAL_EXECUTION_REVIEW_SHA256,scoped:false};
}
/** The original tool authority and all exact prepared outputs stay explicit inputs. */
export function rendererToolDeclaration(repoRoot,env=process.env){
 const base=toolDeclaration(repoRoot);if(base.blockedReason)return base;
 try{
  const root=env.YMCC_MONITOR_RENDERER_PREPARED_ROOT;
  if(!root||!path.isAbsolute(root))return {...base,blockedReason:'MONITOR_RENDERER_PREPARED_ROOT_REQUIRED'};
  const file=path.join(root,'PREPARED.json'),prepared=read(file);
  if(prepared.status!=='PREPARED_REQUIRES_INDEPENDENT_PARENT_REVIEW'||prepared.id!==REAL_RENDERER_ID)throw Error('PREPARED_RENDERER_IDENTITY_REQUIRED');
  const artifacts=[...base.artifactInputs,file];
  if(prepared.builds?.length!==2)throw Error('EXACT_ORIGINAL_BUILD_PAIR_REQUIRED');
  for(const build of prepared.builds){
   if(!inside(root,build.prepareRecord)||!inside(root,build.preparedOut))throw Error('PREPARED_INPUT_ESCAPE');
   const state=read(build.prepareRecord);artifacts.push(build.prepareRecord);
   for(const row of state.scopeReceipt.compiledArtifacts){if(!inside(build.preparedOut,row.path))throw Error('PREPARED_ARTIFACT_ESCAPE');artifacts.push(row.path);}
   const manifest=read(state.runtimeManifest.path);
   if(path.resolve(manifest.runtimeRoot).toLowerCase()!==path.resolve(FIXED_RUNTIME).toLowerCase()||manifest.runtimeVersion!==RUNTIME_VERSION||manifest.runtimeFiles.length!==916)throw Error('EXACT_FIXED_RUNTIME_REQUIRED');
   for(const row of manifest.runtimeFiles){if(typeof row.path!=='string'||path.isAbsolute(row.path)||row.path.split(/[\\/]/).includes('..'))throw Error('RUNTIME_MEMBER_ESCAPE');artifacts.push(path.join(manifest.runtimeRoot,row.path));}
  }
  const binding=rendererReviewBinding(env);
  if(binding.file&&binding.pin){if(!path.isAbsolute(binding.file)||!/^[a-f0-9]{64}$/.test(binding.pin)||sha(binding.file)!==binding.pin)throw Error('EXACT_RENDERER_PARENT_REVIEW_PIN_REQUIRED');artifacts.push(binding.file);}
  const files=unique(artifacts);if(files.length>LIMIT)throw Error('ORIGINAL_COMBINED_ARTIFACT_LIMIT');
  const reason=!binding.file||!binding.pin?'MONITOR_RENDERER_EXACT_INDEPENDENT_PARENT_REVIEW_REQUIRED':null;
  return {artifactInputs:files,...(reason?{blockedReason:reason}:{})};
 }catch(error){return {...base,blockedReason:'MONITOR_RENDERER_PREPARED_INPUT_UNAVAILABLE: '+error.message};}
}
export function getMonitorNoticeRealRendererAdapter(repoRoot=path.resolve(import.meta.dirname,'../../../..')){
 return {
  id:REAL_RENDERER_ID,domain:'lifecycle',kind:'node',scope:'behavioral-mock',
  title:'Persistent monitor notice host: original production Vue renderer36, real nine controllers and exact frame-admission negative',
  path:MODULE+'/real-renderer-global.mjs',args:['--mode','run','--evidence-dir','{outDir}/monitor-notice-real-renderer'],
  timeoutMs:600000,blockedExitCodes:[10],profiles:['desktop','full'],
  evidenceInputs:[...RENDERER_INPUTS],resultFiles:[RENDERER_COMPONENT.evidenceFile],
  lifecycleComponent:RENDERER_COMPONENT,...rendererToolDeclaration(repoRoot),
  boundary:'Reuses unchanged real-renderer-selftest and real-renderer-negative-selftest, their production dedicated Vue entry, original WebView2 fixture and radial_private scope. Requires the full positive36/nine-controller sequence and exact four-check negative prefix with only the Vue presentation guard failing. All DOM, IPC, subscriptions, expiry, pagehide and late-frame semantics remain original. Exact independent parent review binds this prepared manifest, both executables, original source/tool pins and fixed916 runtime. Original3072/compile120s/reviewed-launch150s/worker120s bounds remain. PREPARED is not execution acceptance. No installed YMCC/Steam/Loader, device, OS sleep or external network; no complete parent or full YMCC acceptance.',
 };
}
