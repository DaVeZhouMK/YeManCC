import { normalizeMonitorOutput, type MonitorOutputPreferences, type HudTemplateId } from '@/bridge/monitorHudModel';
import type { SteamUiState } from '@/bridge/steamUi';
import type { MirrorMutationContext } from '@/bridge/deckyFanActions';
import type { PowerLifecycleState } from '@/bridge/api';
import { UTILITY_OPTIONS, emptyRadialUtilities, type RadialUtilityGroup, type RadialUtilityState, type RadialUtilityIntent } from './utilities';
export interface RadialUtilityDeps {
 monitor:{read:()=>Promise<MonitorOutputPreferences>;save:(template:HudTemplateId,revision:number,admit:(current:MonitorOutputPreferences)=>boolean)=>Promise<void>;busy:()=>boolean};
 steam:{read:(force:boolean)=>Promise<SteamUiState>;launch:(admit:()=>boolean)=>Promise<void>;launchPending:()=>boolean;switchMode:(state:SteamUiState)=>Promise<void>;shutdown:(state:SteamUiState)=>Promise<void>};
 power:()=>Promise<PowerLifecycleState>;alive:()=>boolean;nextRevision:()=>string;now:()=>number;
}
type SteamPending={kind:'launch'|'mode'|'shutdown';target:'running'|'bigpicture'|'stopped';pid:number;birth:string};
export function createRadialUtilityActions(deps:RadialUtilityDeps){
 let stopped=false,executing=false,pending:SteamPending|null=null,steamNotice='',observedSource='',exitConfirmed=false;
 const frames=emptyRadialUtilities(),fingerprints:Record<RadialUtilityGroup,string>={'monitor.templates':'','steam.controls':''};
 const checkpoint=(context:()=>void)=>{context();if(stopped||!deps.alive())throw Error('轮盘操作会话已结束');};
 function update(group:RadialUtilityGroup,source:string,value:Omit<RadialUtilityState,'sourceRevision'>){const next=JSON.stringify([source,value]);if(next!==fingerprints[group]){fingerprints[group]=next;frames[group]={...value,sourceRevision:deps.nextRevision()};}return {...frames[group],choices:frames[group].choices.map(choice=>({...choice}))};}
 async function read(group:RadialUtilityGroup,context:()=>void=()=>{},force=false){
  checkpoint(context);
  if(group==='monitor.templates'){
   const preferences=normalizeMonitorOutput(await deps.monitor.read());checkpoint(context);const source=JSON.stringify(preferences),busy=deps.monitor.busy();
   const value=update(group,source,{available:true,busy,pending:false,selected:preferences.ymccTemplate??'B',notice:busy?'监控正在切换输出，请稍后选择':`已选择模板 ${preferences.ymccTemplate??'B'}${preferences.enabled?'':' · 监控仍关闭'}`,
    choices:UTILITY_OPTIONS[group].map(choice=>({...choice,enabled:!busy}))});return {frame:value,preferences,source};
  }
  const state=await deps.steam.read(force);checkpoint(context);
  const stateSource=JSON.stringify([state.known,state.running,state.mode,state.pid,state.birth]);
  if(stateSource!==observedSource){observedSource=stateSource;steamNotice='';exitConfirmed=false;}
  if(pending){
   const changed=state.known&&state.running&&pending.pid>0&&(state.pid!==pending.pid||state.birth!==pending.birth);
   if(changed){pending=null;steamNotice='Steam 实例已变化，请按当前状态重新选择';}
   else if(pending.kind==='shutdown'&&state.known&&!state.running){pending=null;exitConfirmed=true;steamNotice='已确认 Steam 退出';}
   else if(pending.kind==='mode'&&state.known&&state.running&&state.mode==='bigpicture'){pending=null;steamNotice='已确认 Steam 大屏';}
   else if(pending.kind==='launch'&&state.known&&state.running&&(pending.target==='running'||state.mode==='desktop'||state.mode==='bigpicture')){
    steamNotice=pending.target==='bigpicture'&&state.mode==='desktop'?'Steam 已启动为桌面模式；可再次选择大屏':'已确认 Steam 启动';pending=null;
   }else if(pending.kind==='launch'&&!deps.steam.launchPending()){pending=null;steamNotice='启动请求已提交，暂未确认 Steam 状态，请重新检测';}
  }
  const known=state.known===true,identified=known&&state.running&&Number.isSafeInteger(state.pid)&&state.pid>0&&state.birth!=='0';
  const launchWaiting=!state.running&&deps.steam.launchPending(),busy=!!pending||launchWaiting;
  const selected=known&&!state.running?'stop':known&&state.running&&state.mode==='bigpicture'?'bigpicture':known&&state.running&&state.mode==='desktop'?'start':'';
  const choices=UTILITY_OPTIONS[group].map(choice=>({...choice,enabled:!busy&&known&&(choice.value==='start'?!state.running:choice.value==='stop'?identified:!state.running||identified&&['desktop','bigpicture'].includes(state.mode))}));
  const notice=pending?pending.kind==='shutdown'?'退出请求已提交，等待 Steam 结束':pending.kind==='mode'?'大屏请求已提交，等待 Steam 确认':'启动请求已提交，等待 Steam 确认':steamNotice?(steamNotice+(launchWaiting?' · 原启动流程仍在确认或冷却':'')):launchWaiting?'原 Steam 启动流程仍在确认或冷却':(!known?'Steam 状态尚未确认':state.running?(state.mode==='bigpicture'?'Steam 大屏运行中':state.mode==='desktop'?'Steam 桌面运行中':'Steam 运行中，界面模式未确认'):'Steam 尚未运行');
  return {frame:update(group,JSON.stringify([state.known,state.running,state.mode,state.pid,state.birth]),{available:known,busy,pending:!!pending||launchWaiting&&!exitConfirmed,selected,notice,choices}),steam:state,source:JSON.stringify(state)};
 }
 return {
  async read(group:RadialUtilityGroup,context?:()=>void,force=false){return (await read(group,context,force)).frame;},
  resetSession(){pending=null;steamNotice='';observedSource='';exitConfirmed=false;},
  async apply(group:RadialUtilityGroup,intent:RadialUtilityIntent,context:MirrorMutationContext){
   if(executing)throw Error('菜单操作正在处理中');executing=true;
   try{
    if(!intent||typeof intent.choice!=='string'||typeof intent.sourceRevision!=='string')throw Error('菜单选项请求无效');
    const current=await read(group,context.checkpoint,group==='steam.controls');
    if(!current.frame.available||current.frame.busy)throw Error(current.frame.notice);
    if(!intent.sourceRevision||intent.sourceRevision!==current.frame.sourceRevision)throw Error('状态或模板已变化，请重新选择');
    if(!current.frame.choices.some(choice=>choice.value===intent.choice&&choice.enabled))throw Error('当前菜单选项不可用');
    const power=await deps.power();checkpoint(context.checkpoint);if(power.generation!==context.generation||power.phase!=='ready'||!power.inputReady)throw Error('电源或输入状态已变化');
    if(group==='monitor.templates'){
     const preferences=current.preferences!,template=intent.choice as HudTemplateId;
     if(preferences.ymccTemplate===template)return {saved:false,applied:false,pending:false,notice:`当前已选择模板 ${template}`,frame:current.frame,proof:'unchanged'};
     const revision=Math.max(deps.now(),(preferences.ymccTemplateRevision??0)+1);if(!Number.isSafeInteger(revision)||revision<1)throw Error('监控模板版本无效');
     await deps.monitor.save(template,revision,saved=>{try{checkpoint(context.checkpoint);return !deps.monitor.busy()&&JSON.stringify(normalizeMonitorOutput(saved))===current.source;}catch{return false;}});
     const refreshed=await read(group,context.checkpoint);const retained=refreshed.preferences!.ymccTemplate===template&&refreshed.preferences!.ymccTemplateRevision===revision;
     return {saved:true,applied:false,pending:false,notice:retained?`模板 ${template} 已保存${refreshed.preferences!.enabled?'':'，监控仍关闭'}`:'模板选择已提交；之后的修改由原监控流程保留',frame:refreshed.frame,proof:retained?'settings-commit':'superseded'};
    }
    const state=current.steam!;
    if(intent.choice==='bigpicture'&&state.running&&state.mode==='bigpicture')return {saved:false,applied:true,pending:false,notice:'Steam 当前已为大屏',frame:current.frame,proof:'observed-state'};
    if(intent.choice==='stop'){await deps.steam.shutdown(state);checkpoint(context.checkpoint);pending={kind:'shutdown',target:'stopped',pid:state.pid,birth:state.birth};}
    else if(!state.running){await deps.steam.launch(()=>{try{checkpoint(context.checkpoint);return true;}catch{return false;}});checkpoint(context.checkpoint);pending={kind:'launch',target:intent.choice==='bigpicture'?'bigpicture':'running',pid:0,birth:'0'};}
    else {await deps.steam.switchMode(state);checkpoint(context.checkpoint);pending={kind:'mode',target:'bigpicture',pid:state.pid,birth:state.birth};}
    checkpoint(context.checkpoint);
    let refreshed:RadialUtilityState;try{refreshed=(await read(group,context.checkpoint)).frame;}catch(error){checkpoint(context.checkpoint);refreshed={...current.frame,pending:true,busy:true,notice:'请求已提交；Steam 状态读取尚未确认',choices:current.frame.choices.map(choice=>({...choice,enabled:false}))};}
    return {saved:false,applied:!refreshed.pending&&((intent.choice==='stop'&&refreshed.selected==='stop')||(intent.choice==='bigpicture'&&refreshed.selected==='bigpicture')||(intent.choice==='start'&&refreshed.selected!==''&&refreshed.selected!=='stop')),pending:refreshed.pending,notice:refreshed.notice,frame:refreshed,proof:refreshed.pending?'submitted':'observed-state'};
   }finally{executing=false;}
  },
  stop(){stopped=true;pending=null;},
 };
}
