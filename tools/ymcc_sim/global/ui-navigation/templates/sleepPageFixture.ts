const state={enabled:true,mode:'custom',suspended:0,pauseGameOnSleep:true,retryOnEntryFailure:true,retryOnNonUserWake:true,joyXoffAutoClose:true,wakePasswordEnabled:false,plan:true,staticWakeAutoScanEnabled:true,acButton:0,dcButton:1,hibernate:true,size:50,scheme:'1cb8b882-a900-4b9f-9bac-99d151e64441',timeouts:{acScreen:10,dcScreen:5,acSleep:30,dcSleep:15,acHibernate:0,dcHibernate:60}};
(window as any).__sleepFixture=state;
const idleStaticWake={enabled:false,phase:'idle',lastScanEpoch:0,devices:0,software:0,timers:0,protectedDevices:0,failed:0,restorePending:0,diagnosticSourceReady:false,lastError:''};
type StaticWakeOperation={requestId:string;action:'scan'|'restore';state:'pending'|'completed';ok:boolean};
type FixtureStaticWakeStatus=typeof idleStaticWake & {operation?:StaticWakeOperation};
const staticWake={status:{...idleStaticWake} as FixtureStaticWakeStatus,calls:[] as string[],requests:[] as {action:'scan'|'restore';requestId:string}[],hold:false,release:null as null|(()=>void),failure:false,throwAction:false,timeoutAction:false,throwRead:false,holdRead:false,releaseRead:null as null|(()=>void),failPlanSave:false,autoCompletions:0,holdReply:false,releaseReply:null as null|(()=>void),holdPlanSave:false,releasePlanSave:null as null|(()=>void),readReleases:[] as (()=>void)[],
  preferenceReads:0,preferenceWrites:[] as boolean[],failAutoScanSave:false,holdAutoScanSave:false,releaseAutoScanSave:null as null|(()=>void),holdPreferenceRead:false,preferenceReadReleases:[] as (()=>void)[],
  refresh(){const refresh=(window as any).__sleepRefresh;if(typeof refresh!=='function')throw Error('Actual sleep refresh provider unavailable');return refresh();},
  completeAuto(patch:Partial<FixtureStaticWakeStatus>={}){staticWake.status={...staticWake.status,operation:undefined,...patch};staticWake.autoCompletions++;emitStaticWakeUpdated();},
  completeRequest(patch:Partial<FixtureStaticWakeStatus>={},ok=true){const request=staticWake.requests.at(-1);if(!request)throw Error('No static wake request');staticWake.status={...staticWake.status,...patch,operation:{...request,state:'completed',ok}};emitStaticWakeUpdated();}
};
function emitStaticWakeUpdated(){window.dispatchEvent(new CustomEvent('ipc:sleep.staticWake.updated',{detail:{...staticWake.status}}));}
(window as any).__staticWakeFixture=staticWake;
const trace=(name:string,value:any)=>{(window as any).__sleepWrites??=[];(window as any).__sleepWrites.push({name,value});};
export const PW={YEMAN:'1cb8b882-a900-4b9f-9bac-99d151e64441'};
export type PowerBtnIdx=0|1|2|3;
export type SleepGuardStatus=any;export type SleepTimeoutSettings=any;export type StaticWakeStatus=FixtureStaticWakeStatus;
export const sleepGuardGet=async()=>({...state});
export const sleepGuardSet=async(on:boolean)=>{state.enabled=on;trace('guard',on)};
export const sleepGuardSetConfig=async(patch:any)=>{Object.assign(state,patch);trace('config',patch)};
export const getPowerBtnIdx=async(ac:boolean)=>ac?state.acButton:state.dcButton;
export const setPowerBtnIdx=async(ac:boolean,index:number)=>{state[ac?'acButton':'dcButton']=index;trace(ac?'acButton':'dcButton',index)};
export const getActiveScheme=async()=>state.scheme;
export const reactivateCurrentScheme=async()=>{};
export const readHibernateState=async()=>({supportedKnown:true,supported:true,enabledKnown:true,enabled:state.hibernate});
export const setHibernate=async(value:boolean)=>{state.hibernate=value;trace('hibernate',value);return {exitCode:0}};
export const readHibernateSize=async()=>state.size;
export const setHibernateSize=async(value:number)=>{state.size=value;trace('hibernateSize',value)};
export const readTotalMemoryGB=async()=>32;
export const readSleepTimeouts=async()=>({...state.timeouts});
export const setSleepTimeout=async(field:string,ac:boolean,value:number)=>{const key=(ac?'ac':'dc')+field[0].toUpperCase()+field.slice(1);state.timeouts[key]=value;trace(key,value)};
export const getStaticWakeStatus=async()=>{staticWake.calls.push('get');const snapshot={...staticWake.status};if(staticWake.holdRead)await new Promise<void>(resolve=>{staticWake.releaseRead=resolve;staticWake.readReleases.push(resolve)});if(staticWake.throwRead)throw Error('memory status unavailable');return snapshot};
async function staticWakeAction(action:'scan'|'restore',requestId:string){
  if(!requestId)throw Error('Static wake request ID required');
  staticWake.requests.push({action,requestId});
  staticWake.calls.push(action);trace('staticWake',action);
  if(staticWake.hold)await new Promise<void>(resolve=>{staticWake.release=resolve});
  if(staticWake.timeoutAction)throw Object.assign(Error('memory action IPC timeout'),{code:'IPC_TIMEOUT'});
  if(staticWake.throwAction)throw Error('memory action transport unavailable');
  staticWake.status=action==='scan'?{...idleStaticWake,enabled:true,phase:staticWake.failure?'partial':'applied',lastScanEpoch:1,devices:2,software:3,timers:1,protectedDevices:2,failed:staticWake.failure?1:0,diagnosticSourceReady:true,lastError:staticWake.failure?'memory item refused':''}:{...idleStaticWake,phase:staticWake.failure?'restore-pending':'idle',lastScanEpoch:staticWake.status.lastScanEpoch,restorePending:staticWake.failure?1:0,lastError:staticWake.failure?'memory owner value changed':''};
  staticWake.status.operation={action,requestId,state:'completed',ok:!staticWake.failure};
  const result={ok:!staticWake.failure,status:{...staticWake.status}};
  emitStaticWakeUpdated();
  if(staticWake.holdReply)await new Promise<void>(resolve=>{staticWake.releaseReply=resolve});
  return result;
}
export const scanStaticWakeSources=(requestId:string)=>staticWakeAction('scan',requestId);
export const restoreStaticWakeSources=(requestId:string)=>staticWakeAction('restore',requestId);
export const getStaticWakeAutoScanEnabled=async()=>{staticWake.preferenceReads++;const snapshot=state.staticWakeAutoScanEnabled;if(staticWake.holdPreferenceRead)await new Promise<void>(resolve=>{staticWake.preferenceReadReleases.push(resolve)});return snapshot};
export const setStaticWakeAutoScanEnabled=async(value:boolean)=>{staticWake.preferenceWrites.push(value);if(staticWake.holdAutoScanSave)await new Promise<void>(resolve=>{staticWake.releaseAutoScanSave=resolve});if(staticWake.failAutoScanSave)throw Error('memory auto scan preference save refused');state.staticWakeAutoScanEnabled=value;trace('staticWakeAutoScanEnabled',value)};
export const getSleepPowerPlanOptimizationEnabled=async()=>state.plan;
export const setSleepPowerPlanOptimizationEnabled=async(value:boolean)=>{if(staticWake.holdPlanSave)await new Promise<void>(resolve=>{staticWake.releasePlanSave=resolve});if(staticWake.failPlanSave)throw Error('memory preference save refused');state.plan=value;trace('plan',value)};
