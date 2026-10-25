import fs from 'node:fs';
import path from 'node:path';
import {SOURCE_MEMBERS,toolDeclaration,LIMIT} from '../../../fixtures/radial_private/scope.mjs';
import {LIFECYCLE_GATE_SOURCES,normalizeLifecycleComponent} from '../capability-lifecycle/index.mjs';

export const MODULE='tools/ymcc_sim/global/monitor-notice-lifecycle';
export const REAL_WEBVIEW_ID='lifecycle.monitor-notice-real-webview';
export const ORIGINAL_ENTRIES=Object.freeze([MODULE+'/real-webview-selftest.mjs',MODULE+'/real-webview-negative-selftest.mjs']);
export const FIXED_NODE='G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe';
export const WEBVIEW_QUOTE_SOURCES=Object.freeze([
 'deps/json/json.hpp','native/monitor_desktop_hud.h','native/monitor_hud_drag.h',
 'native/monitor_hud_input_policy.h','native/monitor_hud_panel_input.h',
 'native/monitor_hud_placement.h','native/monitor_hud_recovery.h',
 'native/monitor_hud_serialization.h','native/monitor_hud_visibility.h',
 'native/screen_touchpad_input_surface.h','native/touch_haptic_dispatch.h',
 MODULE+'/real-webview-selftest.cpp',
]);
export const WEBVIEW_INPUTS=Object.freeze([...new Set([
 ...SOURCE_MEMBERS,...LIFECYCLE_GATE_SOURCES,...WEBVIEW_QUOTE_SOURCES,...ORIGINAL_ENTRIES,
 'tsconfig.json',MODULE+'/real-webview-adapter.mjs',MODULE+'/real-webview-global.mjs',
])]);
export const WEBVIEW_COMPONENT=Object.freeze(normalizeLifecycleComponent({
 featureId:'monitor.notice-host',parentAdapterId:'lifecycle.monitor-notice-host',
 persistence:'persistent',role:'real-webview-exit-component',fullPersistentAcceptance:false,
 rationale:'The persistent monitor notice host owns resident windows, callbacks and controller references. This component reuses its existing real WebView2 Carrier retirement tests; complete parent exit, stop fallback, sleep recovery and transient recovery remain independently required.',
 sourceInputs:['native/monitor_desktop_hud.h','native/monitor_hud_recovery.h'],
 resources:['resident-windows','periodic-timers','com-references','async-callbacks','resident-processes','event-hooks'],
 evidenceFile:'monitor-notice-real-webview/result.json',
},REAL_WEBVIEW_ID));

const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const unique=files=>[...new Map(files.map(p=>[path.resolve(p).toLowerCase(),path.resolve(p)])).values()];
/** Original tool authority remains authoritative; reviewed prepared inputs are explicit artifacts. */
export function webViewToolDeclaration(repoRoot,env=process.env){
 const base=toolDeclaration(repoRoot);
 if(base.blockedReason)return base;
 try{
  const root=env.YMCC_MONITOR_WEBVIEW_PREPARED_ROOT;
  if(!root||!path.isAbsolute(root))return {...base,blockedReason:'MONITOR_WEBVIEW_PREPARED_ROOT_REQUIRED'};
  const file=path.join(root,'PREPARED.json'),prepared=read(file);
  if(prepared.status!=='PREPARED_REQUIRES_INDEPENDENT_PARENT_REVIEW')throw Error('PREPARED_IDENTITY_REQUIRED');
  const manifest=read(prepared.runtimeManifest.path),runtime=path.resolve(manifest.runtimeRoot);
  const runtimeFiles=manifest.runtimeFiles.map(row=>{
   if(typeof row.path!=='string'||path.isAbsolute(row.path)||row.path.split(/[\\/]/).includes('..'))throw Error('RUNTIME_MEMBER_ESCAPE');
   return path.join(runtime,row.path);
  });
  const artifacts=unique([...base.artifactInputs,...runtimeFiles,file,prepared.runtimeManifest.path,
   ...prepared.builds.flatMap(row=>[row.prepareRecord,row.executable.path]),
  ]);
  if(artifacts.length>LIMIT)throw Error('ORIGINAL_COMBINED_ARTIFACT_LIMIT');
  const reason=!env.YMCC_RADIAL_EXECUTION_REVIEW||!env.YMCC_RADIAL_EXECUTION_REVIEW_SHA256
   ?'MONITOR_WEBVIEW_EXACT_INDEPENDENT_PARENT_REVIEW_REQUIRED':null;
  return {artifactInputs:artifacts,...(reason?{blockedReason:reason}:{})};
 }catch(error){return {...base,blockedReason:'MONITOR_WEBVIEW_PREPARED_INPUT_UNAVAILABLE: '+error.message};}
}
export function getMonitorNoticeRealWebViewAdapter(repoRoot=path.resolve(import.meta.dirname,'../../../..')){
 return {
  id:REAL_WEBVIEW_ID,domain:'lifecycle',kind:'node',scope:'behavioral-mock',
  title:'Persistent monitor notice host: original reviewed real WebView2 retirement component and exact strong-State negative',
  path:MODULE+'/real-webview-global.mjs',args:['--mode','run','--evidence-dir','{outDir}/monitor-notice-real-webview'],
  timeoutMs:600000,blockedExitCodes:[10],profiles:['desktop','full'],
  evidenceInputs:[...WEBVIEW_INPUTS],resultFiles:[WEBVIEW_COMPONENT.evidenceFile],
  lifecycleComponent:WEBVIEW_COMPONENT,...webViewToolDeclaration(repoRoot),
  boundary:'Only original prepared real-webview-selftest and its original single strong-State-capture negative may execute, under exact independently reviewed preparedOut/executable/tool/source/runtime identities. Original compile120s, reviewed launcher150s and owned-worker120s budgets remain unchanged. Real private desktop, owned browser and Job; local synthetic handshake only. Original negative raw FAIL is preserved and its complete eight-check prefix and exact one failure are required. Parent persistent exit/recovery and product Vue renderer remain separate. No installed YMCC/Steam/Loader, external network, devices or OS sleep.',
 };
}
