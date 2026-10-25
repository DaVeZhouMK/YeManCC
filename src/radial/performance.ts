import type { ContinuousPerformanceIdentity, ContinuousPerformanceSnapshot, ContinuousSidePatch } from '@/bridge/performanceControls';
import { performanceMirrorPatch, performanceMirrorView } from '@/bridge/deckyPerformanceActions';
import { validPerformanceMirror, type PerformanceControl, type PerformanceMirrorView, type PerformanceScalar } from '../../decky-plugin/src/performanceModel';

export type RadialPerformanceFrame = PerformanceMirrorView & { label:string };
export type RadialPerformanceIntent = { key:string; value:PerformanceScalar; sourceRevision:string };
export type RadialPerformanceCapability = {ready:boolean;minPL1:number;maxPL1:number;error?:string};
export const RADIAL_PERFORMANCE_KEYS = ['mode','tdp','bias','saving','frequencyMHz','aggressiveness','unlimited','turbo'] as const;
const exposed = new Set<string>(RADIAL_PERFORMANCE_KEYS);
export function emptyRadialPerformance():RadialPerformanceFrame {
  return {version:1,side:null,scope:'global',mode:'auto',sourceRevision:'not-ready',enabled:false,controls:[],label:'正在读取当前性能策略',feedback:'性能调度尚未就绪'};
}
/** Projection of the existing performance page owner; no new ranges or policy. */
export function radialPerformanceFrame(snapshot:ContinuousPerformanceSnapshot,capability:RadialPerformanceCapability,sourceRevision:string):RadialPerformanceFrame {
  const view=performanceMirrorView(snapshot,sourceRevision);
  const min=Math.max(snapshot.hardwareMinimum,capability.minPL1),max=Math.min(snapshot.hardwareMaximum,capability.maxPL1,snapshot.bank?.tdpCeiling??0);
  const available=capability.ready&&view.enabled&&[min,max].every(Number.isFinite)&&min<=max;
  const controls=view.controls.filter(control=>exposed.has(control.key)).map(control=>{
    if(control.kind==='slider'&&control.key==='tdp')return {...control,min,max,value:Math.max(min,Math.min(max,control.value)),enabled:control.enabled&&available};
    return {...control,enabled:control.enabled&&available};
  });
  const value:RadialPerformanceFrame={...view,enabled:available,controls:available?controls:[],
    label:(snapshot.identity.scope==='dedicated'?(snapshot.gameName||'游戏专属'):'全局')+' · '+(snapshot.side==='ac'?'接电':snapshot.side==='dc'?'电池':'等待供电'),
    ...(!available?{feedback:capability.error||view.feedback||'性能执行通道尚未就绪'}:{})};
  const label=value.label;
  return validPerformanceMirror(value)?value:{...emptyRadialPerformance(),sourceRevision,label,feedback:'性能范围正在同步，请稍后重新呼出'};
}
/** Only the displayed subset crosses into the lightweight output window. */
export function normalizeRadialPerformance(raw:unknown):RadialPerformanceFrame {
  if(!validPerformanceMirror(raw)||!raw.controls.every(control=>exposed.has(control.key)))return emptyRadialPerformance();
  const value=raw as RadialPerformanceFrame;
  if(typeof value.label!=='string'||value.label.length>120)return emptyRadialPerformance();
  return {...value,controls:value.controls.map(control=>control.kind==='choice'?{...control,choices:control.choices.map(option=>({...option}))}:{...control})};
}
export function radialPerformancePatch(frame:RadialPerformanceFrame,intent:RadialPerformanceIntent):ContinuousSidePatch {
  if(!frame.enabled||intent.sourceRevision!==frame.sourceRevision||!exposed.has(intent.key))throw Error('性能参数已变化，请重新选择');
  return performanceMirrorPatch({[intent.key]:intent.value},frame);
}
export function performanceControlText(control:PerformanceControl,value:PerformanceScalar=control.value):string {
  if(control.kind==='choice')return control.choices.find(option=>option.data===value)?.label??'不可用';
  if(control.kind==='toggle')return value?'开启':'关闭';
  if(control.key==='bias')return value===0?'CPU 优先':value===100?'GPU 优先':value===50?'均衡倾向':Number(value)<50?'偏向 CPU':'偏向 GPU';
  return String(value)+(control.suffix??'');
}
export function performanceControlId(parentId:string,key:string):string {return '@performance/'+encodeURIComponent(parentId)+'/'+key;}
/** Executes one explicit confirmed control against the captured effective owner.
 * The original scheduler still owns queues, AC/DC, game/PID and write admission. */
export async function applyRadialPerformance(snapshot:ContinuousPerformanceSnapshot,frame:RadialPerformanceFrame,intent:RadialPerformanceIntent,deps:{
  read:()=>Promise<ContinuousPerformanceSnapshot>;capability:()=>Promise<RadialPerformanceCapability>;
  checkpoint:()=>Promise<void>;admit:()=>boolean;
  update:(identity:ContinuousPerformanceIdentity,patch:ContinuousSidePatch,admit:()=>boolean)=>Promise<ContinuousPerformanceSnapshot>;
  nextRevision:()=>string;
}) {
  const patch=radialPerformancePatch(frame,intent);
  await deps.checkpoint();
  const capability=await deps.capability();await deps.checkpoint();
  const fresh=await deps.read();await deps.checkpoint();
  const freshFrame=radialPerformanceFrame(fresh,capability,frame.sourceRevision);
  if(JSON.stringify(fresh.identity)!==JSON.stringify(snapshot.identity)||JSON.stringify(freshFrame)!==JSON.stringify(frame))throw Error('供电、游戏或性能范围已变化，请重新呼出');
  if(!deps.admit())throw Error('性能操作已取消或过期');
  const updated=await deps.update(snapshot.identity,patch,deps.admit);
  await deps.checkpoint();
  const refreshed=radialPerformanceFrame(updated,capability,deps.nextRevision());
  const proof=updated.tdpWriteReceipt;
  const confirmed=intent.key==='tdp'&&proof?.ok===true&&proof.proof==='write-ack'&&proof.requested===intent.value&&proof.accepted===intent.value;
  const title=frame.controls.find(control=>control.key===intent.key)?.label??'性能参数';
  return {snapshot:updated,frame:refreshed,saved:true,ok:intent.key==='tdp'?confirmed:true,applied:confirmed,
    proof:confirmed?'write-ack':'unconfirmed',
    message:intent.key==='tdp'?(confirmed?`TDP 已确认写入 ${intent.value} W`:'TDP 目标已保存；本次硬件写入尚未确认。'):`${title}已保存并提交现有调度；实际效果待设备确认。`};
}