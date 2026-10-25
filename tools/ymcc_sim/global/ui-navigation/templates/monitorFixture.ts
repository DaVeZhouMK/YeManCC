import {normalizeMonitorOutput,switchMonitorHudTemplate} from '@/bridge/monitorHudModel';
import {createMonitorOutputCoordinator} from '@/bridge/monitorOutputCoordinator';
let preferences=normalizeMonitorOutput({enabled:true,output:'ymcc'});let applied='ymcc';const listeners=new Set<(p:any)=>void>();
export const readMonitorOutputPreferences=async()=>structuredClone(preferences);
export const saveMonitorOutputPreferences=async(raw:any)=>{preferences=normalizeMonitorOutput(raw);for(const fn of listeners)fn(structuredClone(preferences));(window as any).__monitorPreview={preferences:structuredClone(preferences),applied};return structuredClone(preferences);};
export const onMonitorOutputPreferencesChanged=(fn:(p:any)=>void)=>{listeners.add(fn);return()=>listeners.delete(fn);};
export const readMonitorOutputRuntime=async()=>({applied,backend:true,ymccAvailable:true,rtssScale:preferences.rtssScale,rtssTemplate:preferences.rtssTemplate,steam:{ok:true,available:true,position:0}});
export const onMonitorHudAvailability=(fn:(v:boolean)=>void)=>{fn(true);return()=>{};};
export const publishMonitorHudAvailability=()=>{};
export const onMonitorOutputApplying=(fn:(v:boolean)=>void)=>{fn(false);return()=>{};};
export const onMonitorOutputApplied=()=>()=>{};
const memoryCoordinator=createMonitorOutputCoordinator({capabilities:async()=>({steam:true,ymcc:true,ymccGlobal:true,ymccOwned:true}),ensureRtss:async()=>{},setSteam:async()=>({confirmed:true}),setRtss:async()=>({confirmed:true}),setYmcc:async()=>({confirmed:true})});
export const monitorOutputCoordinator={async apply(raw:any){const result=await memoryCoordinator.apply(raw);applied=result.applied;(window as any).__monitorPreview={preferences:structuredClone(preferences),applied};return result;}};
export const registerYmccHudOutputAdapter=()=>()=>{};
export const updateMonitorOutputEnabled=async(enabled:boolean)=>{await saveMonitorOutputPreferences({...preferences,enabled});return monitorOutputCoordinator.apply(preferences);};

(window as any).__monitorPreview={preferences:structuredClone(preferences),applied};
(window as any).__monitorSetPreferences=saveMonitorOutputPreferences;

// Keep the explicit memory boundary in sync with the production radial host.
export async function saveMonitorHudTemplate(template,revision,admit){
 if((template!=='A'&&template!=='B')||!Number.isSafeInteger(revision)||revision<1)return;
 if(admit&&!admit(preferences))throw Error('MONITOR_TEMPLATE_CONTEXT_CHANGED');
 if(revision<=(preferences.ymccTemplateRevision??0))return;
 await saveMonitorOutputPreferences(switchMonitorHudTemplate(preferences,template,revision));
}
