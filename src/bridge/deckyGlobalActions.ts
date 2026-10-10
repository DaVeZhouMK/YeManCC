// Global intents use the original schedule queue and input CAS owner. No new
// policy, hardware writer, configuration file or background worker lives here.
import type { PerformanceScheduleConfig, ScheduleMode } from './performanceSchedule';
import type { InputSettingsSnapshot } from './settingsRepository';
import type { PowerLifecycleState } from './api';
import type { RelayMutationContext } from './deckyMirrorRelay';
import type { MirrorActionResult } from './deckyFanActions';
import type { MirrorField } from '../../decky-plugin/src/mirrorClient';
const modes:Record<ScheduleMode,string>={eco:'节能',balanced:'平衡',medium:'中等',performance:'高性能',elite:'精睿',extreme:'极致性能'};
const personas=[['disabled','本机手柄'],['steamdeck','SteamDeck'],['dualsense-edge','PS5'],['elite','Xbox'],['dualshock4','PS4'],['xbox360','Xbox360'],['dualsense','PS5 标准']] as const;
export interface GlobalMirrorRead {schedule:PerformanceScheduleConfig;input:InputSettingsSnapshot;source:string;fields:Record<string,MirrorField>;}
export function globalMirrorRead(schedule:PerformanceScheduleConfig,input:InputSettingsSnapshot):GlobalMirrorRead {
 const target=input.outputTarget||{};
 const field=(value:string,choices:MirrorField['choices']):MirrorField=>({value,choices,supported:true});
 const power=(side:'ac'|'dc')=>field(schedule.active[side],Object.entries(modes).map(([data,label])=>({data,label:`${label} · ${schedule.profiles[side][data as ScheduleMode].tdpMax}W`})));
 return {schedule:structuredClone(schedule),input:structuredClone(input),source:JSON.stringify({schedule,input}),fields:{acMode:power('ac'),dcMode:power('dc'),padPersona:field(String(target.persona||'disabled'),personas.map(([data,label])=>({data,label})))}};
}
export function createGlobalMirrorActions(deps:{allowed:()=>boolean;game:()=>unknown;locked:()=>boolean;side:()=> 'ac'|'dc'|null;read:()=>Promise<GlobalMirrorRead>;power:()=>Promise<PowerLifecycleState>;acquire:(owner:string)=>(()=>void)|null;
 apply:(side:'ac'|'dc',mode:ScheduleMode,config?:PerformanceScheduleConfig)=>Promise<boolean>;cas:(revision:number,patch:Record<string,unknown>,isCurrent?:()=>boolean)=>Promise<{ok:boolean;value:InputSettingsSnapshot}>;closeJoyxoff:()=>Promise<boolean>;clearShortcut:()=>Promise<unknown>}) {
 return {async execute(args:Record<string,unknown>,context:RelayMutationContext):Promise<MirrorActionResult>{
  if(Object.keys(args).length!==2||!['acMode','dcMode','padPersona'].includes(String(args.field))||typeof args.value!=='string')throw Error('GLOBAL_MIRROR_INVALID_REQUEST');
  if(!deps.allowed()||deps.game()||deps.locked()||context.gameIdentity!=='')throw Error('GLOBAL_MIRROR_CONTEXT_CHANGED');
  const release=deps.acquire('decky-global-mirror');if(!release)throw Error('GLOBAL_MIRROR_BUSY');
  const checkpoint=()=>{context.checkpoint();if(!deps.allowed()||deps.game()||deps.locked())throw Error('GLOBAL_MIRROR_CONTEXT_CHANGED');};
  const gate=async()=>{checkpoint();const p=await deps.power();checkpoint();if(p.generation!==context.generation||p.phase!=='ready'||!p.hardwareWritesAllowed)throw Error('GLOBAL_MIRROR_POWER_NOT_READY');};
  try{
   await gate();const current=await deps.read();checkpoint();if(!context.controlAdmission?.global||current.source!==context.controlAdmission.global)throw Error('GAME_MIRROR_SOURCE_CHANGED');
   const data=current.fields[String(args.field)];if(!data?.supported||!data.choices.some(o=>o.data===args.value&&!o.disabled))throw Error('GLOBAL_MIRROR_VALUE_DENIED');
   if(args.field==='acMode'||args.field==='dcMode'){
    const side=args.field==='acMode'?'ac':'dc';if(deps.side()!==side)throw Error('GLOBAL_MIRROR_POWER_SIDE_CHANGED');await gate();
    const applied=await deps.apply(side,args.value as ScheduleMode);checkpoint();return {saved:true,applied,pending:!applied,notice:applied?'全局 TDP / 电源挡位已应用':'全局挡位已保存；当前供电侧应用待确认'};
   }
   const target=current.input.outputTarget||{},motion=current.input.gyroMotion||{},persona=args.value;
   const patch:Record<string,any>={outputTarget:{persona,buttonMappingEnabled:persona!=='disabled'}};
   if(persona==='disabled'){patch.outputTarget.gyroEnabled=false;patch.gyroMotion={enabled:false};}
   else if(motion.virtualPadLink!==false&&!(target.buttonMappingEnabled===true&&target.persona!=='disabled')){patch.outputTarget.gyroEnabled=true;patch.gyroMotion={enabled:true,outputMode:'virtual-stick'};}
   if(persona==='steamdeck'){await deps.closeJoyxoff();checkpoint();}await gate();const result=await deps.cas(current.input.revision,patch,()=>{try{checkpoint();return true;}catch{return false;}});checkpoint();if(!result.ok)throw Error('GAME_MIRROR_SOURCE_CHANGED');
   await deps.clearShortcut();checkpoint();const applied=result.value.applyStatus==='active';return {saved:true,applied,pending:!applied,notice:applied?'全局虚拟手柄已应用':'全局虚拟手柄已保存；实际目标由 YMCC 输入服务确认'};
  }finally{release();}
 }};
}
