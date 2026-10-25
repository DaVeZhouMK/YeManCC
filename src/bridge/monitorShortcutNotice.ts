import { normalizeShortcutActionFeedback, type ShortcutActionFeedback } from './shortcutActionFeedback';
import { FRAME_RATE_CEILINGS } from './frameRateModel';
export interface NoticeRange {min:number;max:number;value:number;unit:string;}
export interface NoticeAppearance {opacity:number;theme:'blue-black'|'red-black'|'cyberpunk';}
export function noticeAppearance(raw:unknown):NoticeAppearance {
 const value=raw as Partial<NoticeAppearance>|null;
 return {opacity:typeof value?.opacity==='number'&&Number.isFinite(value.opacity)?Math.max(0,Math.min(1,value.opacity)):.8,
 theme:value?.theme==='red-black'||value?.theme==='cyberpunk'?value.theme:'blue-black'};
}
export function confirmedNoticeRange(value:ShortcutActionFeedback):NoticeRange|null {
 const n=value.value?.number;if(value.state!=='applied'||typeof n!=='number'||!Number.isFinite(n))return null;
 if(value.actionId==='display.brightnessAdjust'&&value.value?.unit==='%'&&n>=0&&n<=100)return {min:0,max:100,value:n,unit:'%'};
 // Same software range as frameRateSetting(), not a claim about panel refresh.
 const max=Math.max(...FRAME_RATE_CEILINGS);
 if(value.actionId==='display.frameRateControl'&&value.value?.unit==='FPS'&&n>=0&&n<=max)return {min:0,max,value:n,unit:'FPS'};
 // TDP v1 carries no effective per-side bounds. Preserve the confirmed text.
 return null;
}
export function createMonitorNoticeState(deps:{now():number;setTimer(fn:()=>void,ms:number):unknown;clearTimer(id:unknown):void;changed(value:ShortcutActionFeedback|null):void}) {
 let current:ShortcutActionFeedback|null=null,timer:unknown=null,ended=false,generation=-1,occurred=-1;
 const seen=new Map<string,number>();
 function clear(){if(timer!==null)deps.clearTimer(timer);timer=null;current=null;deps.changed(null);}
 return {
  accept(raw:unknown){
   if(ended)return false;const now=deps.now(),value=normalizeShortcutActionFeedback(raw,now);if(!value)return false;
   if(value.powerGeneration<generation||(value.powerGeneration===generation&&value.occurredAtUnixMs<occurred)||seen.has(value.id))return false;
   for(const [id,expires] of seen)if(expires<=now)seen.delete(id);
   seen.set(value.id,value.expiresAtUnixMs);while(seen.size>128)seen.delete(seen.keys().next().value!);
   generation=value.powerGeneration;occurred=value.occurredAtUnixMs;
   if(timer!==null)deps.clearTimer(timer);current=value;deps.changed(value);
   timer=deps.setTimer(()=>{timer=null;if(current?.id===value.id)clear();},value.expiresAtUnixMs-now);return true;
  },clear,
  check(){if(current&&current.expiresAtUnixMs<=deps.now())clear();},
  dispose(){if(ended)return;ended=true;clear();seen.clear();},
  inspect:()=>({current:current?structuredClone(current):null,timerPending:timer!==null,generation,seen:seen.size,ended})
 };
}
