// Global intents use the original schedule queue and input CAS owner. No new
// policy, hardware writer, configuration file or background worker lives here.
import type { PerformanceScheduleConfig } from './performanceSchedule';
import type { InputSettingsSnapshot } from './settingsRepository';
import type { PowerLifecycleState } from './api';
import type { RelayMutationContext } from './deckyMirrorRelay';
import type { MirrorActionResult } from './deckyFanActions';
import type { MirrorField } from '../../decky-plugin/src/mirrorClient';
import { currentVirtualTargetPolicy } from './virtualTargetPolicyState';
import { virtualTargetOptions, virtualTargetSelectionError, requireVirtualTargetAllowed, type VirtualTargetPolicy } from './virtualTargetPolicy';
import { DECKY_PAD_CHOICES, DECKY_GYRO_CHOICES } from './deckyInputOptions';
import { gyroMotionParamsForPreset, isGyroPresetKey } from './gyroPresetModel';

export interface GlobalMirrorRead {schedule:PerformanceScheduleConfig;input:InputSettingsSnapshot;source:string;fields:Record<string,MirrorField>;}
export function globalMirrorRead(schedule:PerformanceScheduleConfig,input:InputSettingsSnapshot,policy:VirtualTargetPolicy|null=currentVirtualTargetPolicy()):GlobalMirrorRead {
 const target=input.outputTarget||{},blocked=!!virtualTargetSelectionError(target.persona,policy);
 const field=(value:string,choices:MirrorField['choices'],supported=true):MirrorField=>({value,choices:structuredClone(choices),supported});
 const motion=input.gyroMotion||{},gyroSupported=target.persona!=='disabled'&&DECKY_PAD_CHOICES.some(o=>o.data===target.persona);
 const preset=isGyroPresetKey(motion.preset)?motion.preset:'fps';
 return {schedule:structuredClone(schedule),input:structuredClone(input),source:JSON.stringify({schedule,input,...(policy?{virtualTargetPolicy:policy}:{})}),fields:{padPersona:field(String(target.persona||'disabled'),virtualTargetOptions(DECKY_PAD_CHOICES,policy)),gyroEnabled:field(target.gyroEnabled===true&&motion.enabled===true&&gyroSupported?'on':'off',[{data:'off',label:'关闭'},{data:'on',label:'开启',...(blocked?{disabled:true}:{})}],gyroSupported),gyroPreset:field(preset,DECKY_GYRO_CHOICES,gyroSupported&&!blocked)}};
}
export function createGlobalMirrorActions(deps:{allowed:()=>boolean;game:()=>unknown;locked:()=>boolean;contextCurrent?:(context:RelayMutationContext)=>boolean;targetPolicy?:()=>VirtualTargetPolicy|null;side:()=> 'ac'|'dc'|null;read:()=>Promise<GlobalMirrorRead>;power:()=>Promise<PowerLifecycleState>;acquire:(owner:string)=>(()=>void)|null;
cas:(revision:number,patch:Record<string,unknown>,isCurrent?:()=>boolean)=>Promise<{ok:boolean;value:InputSettingsSnapshot}>;closeJoyxoff:()=>Promise<boolean>;finishInput?:(saved:boolean,context:RelayMutationContext)=>Promise<MirrorActionResult>;clearShortcut:()=>Promise<unknown>}) {
 return {async execute(args:Record<string,unknown>,context:RelayMutationContext):Promise<MirrorActionResult>{
  if(Object.keys(args).length!==2||!['padPersona','gyroEnabled','gyroPreset'].includes(String(args.field))||typeof args.value!=='string')throw Error('GLOBAL_MIRROR_INVALID_REQUEST');
  // Default remains the no-game contract. YMCC may explicitly admit the same
  // confirmed game in global scope, fenced by its original profile receipt.
  const contextCurrent=()=>deps.contextCurrent?deps.contextCurrent(context):!deps.game()&&context.gameIdentity==='';
  if(!deps.allowed()||!contextCurrent()||deps.locked())throw Error('GLOBAL_MIRROR_CONTEXT_CHANGED');
  const release=deps.acquire('decky-global-mirror');if(!release)throw Error('GLOBAL_MIRROR_BUSY');
  const checkpoint=()=>{context.checkpoint();if(!deps.allowed()||!contextCurrent()||deps.locked())throw Error('GLOBAL_MIRROR_CONTEXT_CHANGED');};
  const gate=async()=>{checkpoint();const p=await deps.power();checkpoint();if(p.generation!==context.generation||p.phase!=='ready'||!p.hardwareWritesAllowed)throw Error('GLOBAL_MIRROR_POWER_NOT_READY');};
  const committed=async()=>{
   if(deps.finishInput)return deps.finishInput(true,context);
   // Standalone consumers retain their original native owner; never infer a
   // fresh runtime ACK from the saved input's previous applyStatus.
   let acknowledged=false;
   try{context.checkpoint();if(deps.allowed()&&contextCurrent()){
    const result=await deps.clearShortcut();acknowledged=result===true||!!result&&typeof result==='object'&&(result as {ok?:boolean}).ok===true;
   }}catch{/* The durable input remains saved; no automatic retry. */}
   return {saved:true,applied:false,pending:true,notice:acknowledged?'输入设置已保存；等待 YMCC 输入服务确认':'输入设置已保存；运行确认未完成，不自动重试'};
  };
  try{
   await gate();const current=await deps.read();checkpoint();if(!context.controlAdmission?.global||current.source!==context.controlAdmission.global)throw Error('GAME_MIRROR_SOURCE_CHANGED');
   const checkTarget=()=>requireVirtualTargetAllowed(args.field==='padPersona'?args.value:current.input.outputTarget?.persona,
    deps.targetPolicy?deps.targetPolicy():currentVirtualTargetPolicy(),args.field==='padPersona'?args.value!=='disabled':args.field==='gyroPreset'||args.value==='on');
   checkTarget();
   const data=current.fields[String(args.field)];if(!data?.supported||!data.choices.some(o=>o.data===args.value&&!o.disabled))throw Error('GLOBAL_MIRROR_VALUE_DENIED');
   if(args.field==='gyroEnabled'||args.field==='gyroPreset'){
    if(args.field==='gyroPreset'&&current.fields.gyroEnabled.value!=='on')throw Error('GLOBAL_MIRROR_GYRO_DISABLED');
    if(data.value===args.value)return {saved:false,applied:false,notice:'全局陀螺仪设置未改变'};
    const motion=current.input.gyroMotion||{};
    const patch:Record<string,unknown>=args.field==='gyroEnabled'
      ? {outputTarget:{gyroEnabled:args.value==='on'},gyroMotion:{enabled:args.value==='on',...(args.value==='on'?{outputMode:'virtual-stick'}:{})}}
      : {outputTarget:{gyroEnabled:true},gyroMotion:{...gyroMotionParamsForPreset(args.value as any,motion.presets),enabled:true,preset:args.value,activePreset:args.value,outputMode:'virtual-stick'}};
    await gate();checkTarget();const result=await deps.cas(current.input.revision,patch,()=>{try{checkpoint();checkTarget();return true;}catch{return false;}});if(!result.ok)throw Error('GAME_MIRROR_SOURCE_CHANGED');
    return await committed();
   }
   const target=current.input.outputTarget||{},motion=current.input.gyroMotion||{},persona=args.value;
   const patch:Record<string,any>={outputTarget:{persona,buttonMappingEnabled:persona!=='disabled'}};
   if(persona==='disabled'){patch.outputTarget.gyroEnabled=false;patch.gyroMotion={enabled:false};}
   else if(motion.virtualPadLink!==false&&!(target.buttonMappingEnabled===true&&target.persona!=='disabled')){patch.outputTarget.gyroEnabled=true;patch.gyroMotion={enabled:true,outputMode:'virtual-stick'};}
   if(persona==='steamdeck'){checkTarget();await deps.closeJoyxoff();checkpoint();}await gate();checkTarget();const result=await deps.cas(current.input.revision,patch,()=>{try{checkpoint();checkTarget();return true;}catch{return false;}});if(!result.ok)throw Error('GAME_MIRROR_SOURCE_CHANGED');
   return await committed();
  }finally{release();}
 }};
}
