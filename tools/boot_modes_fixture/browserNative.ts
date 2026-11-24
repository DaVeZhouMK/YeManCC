// Browser IO is a closed memory boundary. Actual Vue controls and gamepad engine stay intact.
export * from './runtimePorts';
import {calls,record,copy,settings,state,invoke as invokeRuntimePort} from './runtimePorts';
// The page begins after the separately exercised startup pass has disabled the shortcut.
state.gameBarControllerWindows=0;
export const registry={
 async read(root:string,path:string,name:string){record('common.read.registry',{root,path,name});if(root!=='HKCU'||path!=='Software\\Microsoft\\GameBar'||name!=='UseNexusForGameBarEnabled')throw Error('Unexpected browser registry target');return state.gameBarControllerWindows;},
 async write(root:string,path:string,name:string,value:number){record('common.write.registry',{root,path,name,value});if(root!=='HKCU'||path!=='Software\\Microsoft\\GameBar'||name!=='UseNexusForGameBarEnabled'||![0,1].includes(value))throw Error('Unexpected browser registry write');state.gameBarControllerWindows=value;return true;}
};
export const isNativeRuntime=true;
export const on=(event:string,fn:any)=>{const cb=(e:any)=>fn(e.detail);window.addEventListener('ipc:'+event,cb);return()=>window.removeEventListener('ipc:'+event,cb);};
export const emitNativeEvent=(event:string,value:any)=>record('event.'+event,value);
export const windowApi={async controllerEditorWidth(owner:string,enabled:boolean,factor:number){record('window.editorWidth',{owner,enabled,factor,collapsedWidth:window.__bootShellGeometry().stageWidth*window.__bootShellGeometry().scale,expandedWidth:Math.min(innerWidth,window.__bootShellGeometry().stageWidth*window.__bootShellGeometry().scale*factor)});return {ok:true};},async minimize(){record('window.minimize');},async setPerformanceEditorWidth(...args:any[]){record('window.width',args);return {ok:true};}};
export const app={exit:async()=>record('app.exit')};
export const oemKeysGet=async()=>({ok:true,available:false,keys:[],buttons:[],definitions:[],observed:[]});
export const setShortcutRecording=async()=>({ok:true});
export const summonGet=async()=>({enabled:false,button:'guide',holdMs:500});
export const summonSet=async()=>({ok:true});
export const isUiVisible=()=>true;
export const getUiVisibilityState=()=>({visible:true,phase:'active',generation:1});
export const onUiVisibilityChange=()=>()=>{};
export const residentNavigationRoutes=(routes:any)=>routes;
export const useVirtualTargetPolicy=()=>({value:null});
export const currentVirtualTargetPolicy=()=>null;
export const virtualTargetOptions=(items:any)=>items;
export const virtualTargetSelectionError=()=>null;
export const virtualTargetShortcutError=()=>null;
export const requireVirtualTargetAllowed=()=>{};
export const STEAM_DECK_SELF_OUTPUT_MESSAGE='';
export const subscribePolicyGameStatus=()=>()=>{};
export const ROUTES=[{path:'/boot',title:'开机模式'}];
export const getSettingsSnapshot=async()=>copy(settings);
export const getCachedSettings=()=>copy(settings);
export const subscribeSettings=()=>()=>{};
export const fs=new Proxy({}, {get:(_target,key)=>async()=>{throw new Error('Unexpected memory filesystem call: '+String(key));}});
export const settingsStore=new Proxy({}, {get:(_target,key)=>async()=>{throw new Error('Unexpected settings store call: '+String(key));}});
import {ref} from 'vue';
export const fanNavigationMotion=false;
export const fanNavigationSpinDuration='1s';
export const topMonitorData=ref({tdpW:15,freqMhz:2800,tempC:55,hasBattery:true,chargeW:-8,batteryPercent:75});
export const startTopMonitor=async()=>{};
export const stopTopMonitor=async()=>{};
export const readTopMonitor=async()=>topMonitorData.value;
export const setTopMonitorData=(value:any)=>topMonitorData.value=value;
export const registerScheduledTask=(_name:any,_interval:any,run:any)=>{void run();return()=>{};};
export const currentName=ref('Fixture music');
export const hasFolder=ref(false);
export const playing=ref(false);
export const initMusic=async()=>{};
export const chooseFolder=async()=>{};
export const togglePlay=async()=>{};
export const refreshGameStatus=async()=>null;
export const refreshGameStatusStrict=async()=>null;
export const subscribeGameStatus=()=>()=>{};
export const detectedGameName=(game:any)=>game?.name||'';
export const detectGame=async()=>null;
export const getCurrentGame=()=>null;
export const isCurrentGameTarget=()=>false;
export const validateGameTarget=async()=>null;
// Actual PowerView's common-startup ports: every read/write is isolated and journalled.
export const BOOT_MIRROR_CHANGED_EVENT='boot-mirror-changed-fixture';
let networkTaskPresent=1,networkTaskEnabled=1;
export async function createTask(name:string){
 if(name!=='Steamcommunity_302'||networkTaskPresent)throw Error('Unexpected network task creation');
 record('common.write.networkTask.create',name);networkTaskPresent=networkTaskEnabled=2;return true;
}
export async function invoke(command:string,args:any){
 if(command!=='updateAccel.bootTask')return invokeRuntimePort(command,args);
 if('enabled' in args){record('common.write.networkTask',args);networkTaskEnabled=args.enabled?(networkTaskPresent&1||networkTaskPresent&2):0;}
 else if('restore' in args){if(args.expectedPresent!==networkTaskPresent||args.expectedEnabled!==networkTaskEnabled)return {ok:false};record('common.write.networkTask',args);networkTaskEnabled=args.restore;}
 else record('common.read.networkTask');
 return {ok:true,present:networkTaskPresent,enabled:networkTaskEnabled};
}
export async function taskExists(name:string){record('common.read.taskExists',name);return name==='桌面模式-开机设置为桌面模式'&&state.legacyDesktop===true;}
export async function searchState(){record('common.read.searchState');return 'full';}
export async function readBootMirrorState(){record('common.read.bootMirror');return false;}
export async function readBootRtssState(){record('common.read.rtss');return false;}
export async function readHardwareGpuSchedule(){record('common.read.gpuSchedule');return false;}
export async function readSettingsSection(section:string){record('common.read.settings',section);return copy(settings[section]||{});}
export async function saveSettingsSection(section:string,value:any){record('common.write.settings',{section,value});settings[section]={...settings[section],...copy(value)};}
export async function toggleTask(name:string,enabled:boolean){record('common.write.task',{name,enabled});if(name==='桌面模式-开机设置为桌面模式')state.legacyDesktop=enabled;return enabled;}
export async function toggleBootMirror(enabled:boolean){record('common.write.bootMirror',enabled);return enabled;}
export async function toggleBootRtss(enabled:boolean){record('common.write.rtss',enabled);return enabled;}
export async function writeHardwareGpuSchedule(enabled:boolean){record('common.write.gpuSchedule',enabled);return true;}
export async function openSearchFolder(){record('common.write.searchFolder');}
export async function deleteAllYemanTasks(){record('common.write.deleteTasks');}
export async function restoreAllYemanTasks(){record('common.write.restoreTasks');}

// Same inert error shape as the production IPC module; browser IO remains isolated.
export class IpcTimeoutError extends Error {
  readonly code = 'IPC_TIMEOUT';
  constructor(readonly command:string) { super(`IPC command timed out: ${command}`); }
}
