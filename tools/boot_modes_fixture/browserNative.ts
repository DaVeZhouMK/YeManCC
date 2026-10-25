// Browser IO is a closed memory boundary. Actual Vue controls and gamepad engine stay intact.
export * from './runtimePorts';
import {calls,record,copy,settings,state} from './runtimePorts';
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
