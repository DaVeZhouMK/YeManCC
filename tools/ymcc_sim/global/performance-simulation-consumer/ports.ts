import {ref} from 'vue';
import {frameRatePair,frameRateSetting} from '@/bridge/frameRateModel';
const clone=(x:any)=>structuredClone(x);
const f=()=> (window as any).__memory;
export const powerSourceMode=ref<'ac'|'dc'|null>('ac');
export const isNativeRuntime=false;
const gameListeners=new Set<()=>void>(),frameListeners=new Set<()=>void>();
export const getPolicyGame=()=>clone(f().game);
export const detectPolicyGame=async()=>getPolicyGame();
export const detectGame=detectPolicyGame;
export const gamePolicyKey=(g:any)=>g.name.toLowerCase();
export const isPolicyTargetCurrent=(g:any)=>{const n=f().game;return !!n&&g.pid===n.pid&&String(g.processCreated)===String(n.processCreated)&&g.exe===gamePolicyKey(n)};
export const subscribePolicyGameStatus=(cb:()=>void)=>{gameListeners.add(cb);return()=>gameListeners.delete(cb)};
export function notifyGame(){for(const cb of gameListeners)cb()}
export const getSettingsGeneration=()=>f().generation;
export const assertSettingsGeneration=(g:number)=>{if(g!==f().generation)throw Error('MEMORY_SETTINGS_GENERATION_CHANGED')};
export const readSettingsSection=async(name:string)=>clone(f().settings[name]??{});
export async function updateSettings(fn:any){const x=f();x.attempts++;x.events.push({kind:'memory-settings-attempt',n:x.attempts});if(x.holdWrite)await new Promise<void>(r=>x.releaseWrite=r);if(x.failWrite){x.failWrite=false;throw Error('OWNED_MEMORY_WRITE_FAILED')}const next=clone(x.settings);await fn(next);x.settings=next;x.writes.push(clone(next));x.events.push({kind:'memory-settings-committed',count:x.writes.length});return clone(next)}
export function mergeSettings(base:any,patch:any):any{if(!patch||typeof patch!=='object'||Array.isArray(patch))return clone(patch);const out={...(base??{})};for(const k of Object.keys(patch))out[k]=patch[k]&&typeof patch[k]==='object'&&!Array.isArray(patch[k])?mergeSettings(out[k],patch[k]):clone(patch[k]);return out}
export const replaceSettingsSection=async(name:string,value:any)=>updateSettings((s:any)=>s[name]=value);
export const saveSettingsSection=replaceSettingsSection;
export const ensureGlobalFrameRates=async()=>frameRatePair(f().settings.rtss.frameRates);
export const loadGlobalFrameRates=ensureGlobalFrameRates;
export {frameRatePair,frameRateSetting};
export {dedicatedFrameRatePair,dedicatedFrameRateRecord} from '@/bridge/frameRateModel';
export const publishFrameRatesChanged=()=>{for(const cb of frameListeners)cb()};
export const onFrameRatesChanged=(cb:()=>void)=>{frameListeners.add(cb);return()=>frameListeners.delete(cb)};
const memoryCall=(name:string,...args:any[])=>{f().systemCalls.push({name,args:clone(args)})};
export const applyIndependentFrameRates=async()=>{memoryCall('frameRate.apply');return true};
export const fs={exists:async(p:string)=>p.endsWith('game-custom.json')||Object.hasOwn(f().files,p),readTextFile:async(p:string)=>f().files[p]??JSON.stringify({version:1,entries:{},rtss:{}}),writeTextFileAtomic:async(p:string,text:string)=>{f().files[p]=text;f().fileWrites.push({p,text})}};
export const powerLifecycle={get:async()=>({phase:'ready',writesAllowed:true})};
export const windowApi={controllerEditorWidth:async(...args:any[])=>{memoryCall('width',...args);return {ok:true}}};
export const invoke=async()=>{throw Error('NATIVE_IPC_FORBIDDEN')};
export const detectGameCorePolicy=async()=>({detected:true,heterogeneous:true,smtAvailable:true,groups:[{label:'big',physical:4,logical:8},{label:'small',physical:4,logical:4}]});
export const applyGameCorePolicy=async(...args:any[])=>{memoryCall('gameCore.apply',...args);return {ok:true,applied:true}};
export const detectPowerMode=async()=>f().side;
export const detectPowerModeReliable=async()=>{if(f().holdPowerRead){f().pendingPowerReads??=[];await new Promise<void>(r=>f().pendingPowerReads.push(r))}return f().side};
export const detectPerformanceTemplate=async()=>'portable';
export const getTdpHardwareMinimum=()=>2;
export const getTdpHardwareMaximum=()=>150;
export const getTdpExecutionCapability=async()=>({supported:false,transport:'owned-memory'});
export const PW={YEMAN:'owned-memory-scheme'};
export const getActiveScheme=async()=>PW.YEMAN;
export const setActiveScheme=async(...args:any[])=>memoryCall('scheme.set',...args);
export const ensureRememberedYemanSchemeActive=async()=>memoryCall('scheme.ensure');
export const applyManualPowerParams=async(...args:any[])=>memoryCall('cpu.params',...args);
export const setCoreModeAc=async(...args:any[])=>memoryCall('core.ac',...args);
export const setCoreModeDc=async(...args:any[])=>memoryCall('core.dc',...args);
export const detectCoreArchitecture=detectGameCorePolicy;
export const rebuildYemanScheme=async()=>memoryCall('scheme.rebuild');
export const setTdp=async(side:any,watts:any,options:any)=>{memoryCall('tdp.memory-only',side,watts);options?.onAccepted?.(watts);return true};
export const setRtssLimit=async(...args:any[])=>memoryCall('rtss.memory-only',...args);
export const readRtssLimit=async()=>60;
export const loadCpuProfiles=async()=>({profiles:{},active:'balanced'});
export const enableContinuousFloat=async()=>memoryCall('float.enable');
export const disableFloat=async()=>memoryCall('float.disable');
export const getFloatInfo=async()=>({enabled:false});
export const cancelPendingRtssSync=()=>{};
export const setFloatRtssLinked=()=>{};
export function listenerCounts(){return {game:gameListeners.size,frames:frameListeners.size}}
export {FRAME_RATE_CEILINGS} from '@/bridge/frameRateModel';


