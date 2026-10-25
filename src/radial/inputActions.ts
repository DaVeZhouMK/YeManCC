import { createGlobalMirrorActions, type GlobalMirrorRead } from '@/bridge/deckyGlobalActions';
import { createGameMirrorActions, gameMirrorIdentity, makeGameMirrorAdmission, readGameMirrorGyro, type GameMirrorDeps, type GameMirrorGyroDefaults } from '@/bridge/deckyGameActions';
import type { GameCustomProfile, PerformanceScheduleConfig } from '@/bridge/performanceSchedule';
import type { DetectedGame } from '@/bridge/gamedetect';
import type { MirrorMutationContext } from '@/bridge/deckyFanActions';
import { virtualTargetOptions, type VirtualTargetPolicy } from '@/bridge/virtualTargetPolicy';
import { RADIAL_INPUT_OPTIONS, emptyRadialInput, inputFieldsFor, type RadialInputFrame, type RadialInputGroup, type RadialInputIntent, type RadialInputField } from './input';
export type RadialInputRead={policy:VirtualTargetPolicy|null;locked:boolean;busy:boolean}&(
 {scope:'global';global:GlobalMirrorRead}|{scope:'game';game:DetectedGame;entry:GameCustomProfile|undefined;schedule:PerformanceScheduleConfig;gyro:GameMirrorGyroDefaults;label:string});
export interface RadialInputDeps {read:()=>Promise<RadialInputRead>;global:Parameters<typeof createGlobalMirrorActions>[0];game:GameMirrorDeps;nextRevision:()=>string;}
export function createRadialInputActions(deps:RadialInputDeps){
 const global=createGlobalMirrorActions(deps.global),game=createGameMirrorActions(deps.game);
 let stopped=false,applying=false,fingerprint='',frame=emptyRadialInput();
 async function read(checkpoint:()=>void=()=>{}){
  checkpoint();if(stopped)throw Error('轮盘输入会话已结束');const raw=await deps.read();checkpoint();
  let fields:GlobalMirrorRead['fields'],identity='',admission:ReturnType<typeof makeGameMirrorAdmission>|undefined;
  if(raw.scope==='global')fields=raw.global.fields;
  else{
   identity=gameMirrorIdentity(raw.game);admission=makeGameMirrorAdmission(raw.entry,raw.schedule,undefined,raw.gyro.source);
   const persona=raw.entry?.padPersona&&raw.entry.padPersona!=='follow'?raw.entry.padPersona:raw.gyro.padPersona??'disabled';
   const gyro=readGameMirrorGyro(raw.entry,raw.gyro),supported=persona!=='disabled'&&RADIAL_INPUT_OPTIONS.padPersona.some(choice=>choice.data===persona);
   const choices=virtualTargetOptions<{data:string;label:string;disabled?:boolean}>(RADIAL_INPUT_OPTIONS.padPersona,raw.policy);
   const blocked=choices.find(choice=>choice.data===persona)?.disabled===true;
   fields={padPersona:{value:persona,choices,supported:true},gyroEnabled:{value:gyro.enabled?'on':'off',choices:RADIAL_INPUT_OPTIONS.gyroEnabled.map(choice=>({...choice,disabled:choice.data==='on'&&blocked})),supported},gyroPreset:{value:gyro.preset,choices:[...RADIAL_INPUT_OPTIONS.gyroPreset],supported:supported&&!blocked}};
  }
  const identityReady=raw.scope==='global'||Number.isSafeInteger(raw.game.pid)&&raw.game.pid>0&&!!raw.game.processCreated;
  const available=identityReady&&!(raw.scope==='global'&&raw.locked),busy=raw.busy;
  const controls=(['padPersona','gyroEnabled','gyroPreset'] as RadialInputField[]).map(key=>{
   const source=fields[key];return {key,value:String(source?.value??''),enabled:available&&!busy&&source?.supported===true&&(key!=='gyroPreset'||fields.gyroEnabled?.value==='on'),
    choices:RADIAL_INPUT_OPTIONS[key].map(option=>{const actual=source?.choices.find(choice=>choice.data===option.data);return {value:option.data,label:actual?.label??option.label,enabled:!!actual&&!actual.disabled}})};
  });
  const pending=raw.scope==='game'||raw.global.input.applyStatus!=='active';
  const value:RadialInputFrame={sourceRevision:'',scope:raw.scope,label:raw.scope==='global'?'全局输入':raw.label.slice(0,90)+' · 游戏专属',available,busy,pending,controls,
   notice:!identityReady?'游戏进程身份尚未确认':!available?'当前输入由游戏联动控制，请等待范围同步':busy?'请先完成当前输入操作':raw.scope==='game'?(raw.entry?.enabled===true?'按当前游戏记忆输入；由原游戏联动应用':'首次选择会启用当前游戏的原专属配置'):pending?'全局输入已保存；等待输入服务确认':'全局输入状态已由 YMCC 输入服务确认'};
  const source=raw.scope==='global'?raw.global.source:JSON.stringify([identity,admission]);
  const next=JSON.stringify([source,raw.policy,raw.locked,value]);if(next!==fingerprint){fingerprint=next;frame={...value,sourceRevision:deps.nextRevision()};}
  return {frame:{...frame,controls:frame.controls.map(control=>({...control,choices:control.choices.map(choice=>({...choice}))}))},raw,identity,admission};
 }
 return {
  async read(checkpoint?:()=>void){return (await read(checkpoint)).frame;},
  async apply(group:RadialInputGroup,intent:RadialInputIntent,context:MirrorMutationContext){
   if(stopped||applying)throw Error('输入操作正在处理，请稍后重试');
   if(!intent||!inputFieldsFor(group).includes(intent.field)||typeof intent.value!=='string'||typeof intent.sourceRevision!=='string')throw Error('输入选项请求无效');
   applying=true;
   try{
    const current=await read(context.checkpoint),control=current.frame.controls.find(control=>control.key===intent.field);
    if(!current.frame.available||current.frame.busy)throw Error(current.frame.notice);
    if(!intent.sourceRevision||intent.sourceRevision!==current.frame.sourceRevision)throw Error('游戏、设备或输入设置已变化，请重新选择');
    if(!control?.enabled||!control.choices.some(choice=>choice.value===intent.value&&choice.enabled))throw Error('当前输入选项不可用');
    const relayContext={...context,revision:0,gameIdentity:current.identity,...(current.admission?{gameAdmission:current.admission}:{}),...(current.raw.scope==='global'?{controlAdmission:{global:current.raw.global.source}}:{})};
    const args={field:intent.field,value:intent.value};
    const outcome=current.raw.scope==='game'?await game.execute('game.setField',args,relayContext):await global.execute(args,relayContext);
    const refreshed=await read(context.checkpoint);return {...outcome,frame:refreshed.frame};
   }finally{applying=false;}
  },
  stop(){stopped=true;},
 };
}
