import type {PerformanceScalar} from '../../decky-plugin/src/performanceModel';
import {type ContinuousPerformanceSnapshot,type ContinuousSidePatch,APPLICATION_TDP_MAX,CONTROL_CORE_OPTIONS,SAVING_RULES} from './performanceControls';
import {validPerformanceMirror,performancePatchAllowed,type PerformanceMirrorView,type PerformanceControl} from '../../decky-plugin/src/performanceModel';

// Projection only: policy calculation and persistence belong to performanceSchedule.
export function performanceMirrorSource(s:ContinuousPerformanceSnapshot):string {
  return JSON.stringify([s.identity,s.hardwareMinimum,s.hardwareMaximum,s.hybrid,s.smtAvailable,s.hyperThreadMode,s.configured]);
}
export function performanceMirrorView(s:ContinuousPerformanceSnapshot,sourceRevision:string):PerformanceMirrorView {
  const side=s.side,b=s.bank,max=Math.min(APPLICATION_TDP_MAX,s.hardwareMaximum),min=Math.max(1,s.hardwareMinimum);
  const enabled=!!s.side&&!!b&&Number.isFinite(min)&&Number.isFinite(max)&&max>=min;
  if(!b||!enabled)return {version:1,side,scope:s.identity.scope==='dedicated'?'game':'global',mode:b?.mode??'auto',sourceRevision,enabled:false,controls:[],feedback:s.side?'性能调度正在读取设备范围':'正在等待当前供电状态'};
  const controls:PerformanceControl[]=[
    {key:'tdp',label:'TDP 功耗',kind:'slider',value:b.tdp,min,max:Math.min(max,b.tdpCeiling),step:1,suffix:' W',enabled:true},
    {key:'mode',label:'调度方式',kind:'choice',value:b.mode,presentation:'buttons',choices:[{data:'auto',label:'自动模式'},{data:'manual',label:'手动模式'}],enabled:true},
  ];
  const coreChoices=CONTROL_CORE_OPTIONS.map(o=>({data:o.value,label:o.label}));
  if(b.mode==='auto')controls.push(
    {key:'bias',label:'CPU / GPU 优先倾向',kind:'slider',value:b.auto.bias,min:0,max:100,step:5,enabled:true},
    {key:'saving',label:'节能浮动',kind:'choice',value:b.auto.saving,choices:Object.entries(SAVING_RULES).map(([data,r])=>({data,label:r.label})),enabled:true},
    {key:'coreHigh',label:'高功耗核心',kind:'choice',value:b.auto.coreHigh,choices:coreChoices,enabled:s.hybrid},
    {key:'coreLow',label:'低功耗核心',kind:'choice',value:b.auto.coreLow,choices:coreChoices,enabled:s.hybrid},
    {key:'coreSplit',label:'切换低功耗核心瓦数',kind:'slider',value:Math.min(b.auto.coreSplit,b.tdp),min:0,max:Math.min(max,b.tdp),step:1,suffix:' W',enabled:s.hybrid});
  else controls.push(
    {key:'frequencyMHz',label:'CPU 频率',kind:'slider',value:b.manual.frequencyMHz,min:800,max:7200,step:100,suffix:' MHz',enabled:!b.manual.unlimited},
    {key:'aggressiveness',label:'积极性',kind:'slider',value:b.manual.aggressiveness,min:0,max:100,step:1,suffix:'%',enabled:true},
    {key:'unlimited',label:'不限频率',kind:'toggle',value:b.manual.unlimited,group:'manualToggles',enabled:true},
    {key:'turbo',label:'睿频',kind:'toggle',value:b.manual.turbo,group:'manualToggles',enabled:true},
    {key:'coreMode',label:'大小核心',kind:'choice',value:b.manual.coreMode,choices:coreChoices,group:'manualCore',enabled:s.hybrid});
  controls.push({key:'hyperThreadMode',label:'超线程',kind:'choice',value:s.hyperThreadMode,choices:[{data:'default',label:'系统默认'},{data:'on',label:'开启'},{data:'off',label:'关闭'}],group:b.mode==='manual'?'manualCore':undefined,enabled:s.smtAvailable&&!!s.identity.exe});
  const view:PerformanceMirrorView={version:1,side,scope:s.identity.scope==='dedicated'?'game':'global',mode:b.mode,sourceRevision,enabled,controls};
  return validPerformanceMirror(view as unknown)?view:{...view,enabled:false,controls:[],feedback:'性能调度参数正在同步，请稍后重试'};
}
export function performanceMirrorPatch(patch:Record<string,PerformanceScalar>,view:PerformanceMirrorView):ContinuousSidePatch {
  if(!performancePatchAllowed(patch,view))throw Error('性能参数已变化，请重新调节');
  const out:ContinuousSidePatch={};
  for(const [key,value]of Object.entries(patch)){
    if(['mode','tdp'].includes(key))(out as any)[key]=value;
    else if(['bias','saving','coreSplit','coreLow','coreHigh'].includes(key))(out.auto??={})[key as keyof NonNullable<ContinuousSidePatch['auto']>]=value as never;
    else if(['frequencyMHz','unlimited','aggressiveness','turbo','coreMode'].includes(key))(out.manual??={})[key as keyof NonNullable<ContinuousSidePatch['manual']>]=value as never;
    else if(key==='hyperThreadMode')out.hyperThreadMode=value as 'default'|'on'|'off';
    else throw Error('不支持的性能参数');
  }
  return out;
}
import type {RelayMutationContext} from './deckyMirrorRelay';
import type {ContinuousPerformanceIdentity} from './performanceControls';
export function createPerformanceMirrorActions(deps:{allowed:()=>boolean;read:()=>Promise<ContinuousPerformanceSnapshot>;update:(identity:ContinuousPerformanceIdentity,patch:ContinuousSidePatch,admit?:()=>boolean)=>Promise<ContinuousPerformanceSnapshot>;power:()=>Promise<{phase:string;hardwareWritesAllowed:boolean;generation:number}>;acquire:(label:string)=>(()=>void)|null}) {
 return {async execute(args:Record<string,unknown>,context:RelayMutationContext){
  if(Object.keys(args).length!==1||!args.patch||typeof args.patch!=='object'||Array.isArray(args.patch))throw Error('PERFORMANCE_MIRROR_INVALID_REQUEST');
  const checkpoint=()=>{context.checkpoint();if(!deps.allowed())throw Error('PERFORMANCE_MIRROR_NOT_READY');};
  const gate=async()=>{checkpoint();const p=await deps.power();checkpoint();if(p.phase!=='ready'||!p.hardwareWritesAllowed||p.generation!==context.generation)throw Error('PERFORMANCE_MIRROR_POWER_NOT_READY');};
  await gate();const release=deps.acquire('Decky 性能调度');if(!release)throw Error('PERFORMANCE_MIRROR_BUSY');
  try{const current=await deps.read();checkpoint();
   if(typeof context.controlAdmission?.performance!=='string'||performanceMirrorSource(current)!==context.controlAdmission.performance)throw Error('PERFORMANCE_MIRROR_SOURCE_CHANGED');
   const patch=performanceMirrorPatch(args.patch as Record<string,PerformanceScalar>,performanceMirrorView(current,'admitted'));
   await gate();await deps.update(current.identity,patch,()=>{checkpoint();return true;});
   return {saved:true,applied:true,notice:'性能调度已记忆并提交 YMCC'};
  }finally{release();}
 }};
}

