// Public display model for the authenticated mirror. The mainline owner supplies
// saved values and hardware bounds. This contains no persistence or TDP policy.
export type PerformanceScalar = string | number | boolean;
type Base = {key:string;label:string;enabled:boolean;group?:string};
export type PerformanceControl =
  | Base & {kind:'slider';value:number;min:number;max:number;step:number;display?:string;suffix?:string}
  | Base & {kind:'choice';value:string;choices:Array<{data:string;label:string;disabled?:boolean}>;presentation?:'buttons'}
  | Base & {kind:'toggle';value:boolean};
export interface PerformanceMirrorView {version:1;side:'ac'|'dc'|null;scope:'global'|'game';mode:'auto'|'manual';sourceRevision:string;enabled:boolean;controls:PerformanceControl[];feedback?:string;}
// Core controls use the owner's hardware capability, independently of page busy/read-only state.
const HYBRID_CORE_KEYS = new Set(['coreHigh','coreLow','coreSplit','coreMode']);
export function visiblePerformanceControls(view:PerformanceMirrorView):PerformanceControl[] {
  return view.controls.filter(c=>!HYBRID_CORE_KEYS.has(c.key)||c.enabled);
}
const text=(v:unknown,max:number):v is string=>typeof v==='string'&&v.length<=max;
export function validPerformanceMirror(v:any):v is PerformanceMirrorView {
  if(!v||v.version!==1||v.side!==null&&!['ac','dc'].includes(v.side)||!['global','game'].includes(v.scope)||!['auto','manual'].includes(v.mode)||typeof v.enabled!=='boolean'||!text(v.sourceRevision,128)||!v.sourceRevision||v.feedback!==undefined&&!text(v.feedback,512)||!Array.isArray(v.controls)||v.controls.length>24)return false;
  const keys=new Set();return v.controls.every((c:any)=>{
    if(!c||!text(c.key,64)||!/^[a-zA-Z][a-zA-Z0-9]*$/.test(c.key)||keys.has(c.key)||!text(c.label,80)||typeof c.enabled!=='boolean'||c.group!==undefined&&!text(c.group,64))return false;keys.add(c.key);
    if(c.kind==='toggle')return typeof c.value==='boolean';
    if(c.kind==='choice')return text(c.value,64)&&(c.presentation===undefined||c.presentation==='buttons')&&Array.isArray(c.choices)&&c.choices.length>0&&c.choices.length<=16&&new Set(c.choices.map((o:any)=>o?.data)).size===c.choices.length&&c.choices.every((o:any)=>o&&text(o.data,64)&&text(o.label,100)&&(o.disabled===undefined||typeof o.disabled==='boolean'))&&c.choices.some((o:any)=>o.data===c.value);
    return c.kind==='slider'&&[c.value,c.min,c.max,c.step].every(n=>typeof n==='number'&&Number.isFinite(n))&&c.min>=0&&c.max<=100000&&c.min<=c.max&&c.value>=c.min&&c.value<=c.max&&c.step>0&&c.step<=100000&&(c.display===undefined||text(c.display,100))&&(c.suffix===undefined||text(c.suffix,24));
  });
}
export function performancePatchAllowed(patch:Record<string,unknown>,view:PerformanceMirrorView):boolean {
  if(!patch||Array.isArray(patch)||typeof patch!=='object')return false;
  const entries=Object.entries(patch);return view.enabled&&entries.length>0&&entries.length<=24&&entries.every(([key,value])=>{
    const c=view.controls.find(c=>c.key===key);if(!c?.enabled)return false;if(c.kind==='toggle')return typeof value==='boolean';
    if(c.kind==='choice')return c.choices.some(o=>o.data===value&&!o.disabled);
    if(typeof value!=='number'||!Number.isFinite(value)||value<c.min||value>c.max)return false;
    const steps=(value-c.min)/c.step;return Math.abs(steps-Math.round(steps))<1e-6;
  });
}

// This is scheduler preference, not a CPU/GPU power-percentage split.
export function cpuGpuPreferenceText(value:number):string {
  if(value===0)return 'CPU 优先';
  if(value===100)return 'GPU 优先';
  if(value===50)return '均衡倾向';
  return value<50?'偏向 CPU':'偏向 GPU';
}
// Steam's slider computes floating point positions even when step is integral.
// Canonicalize once at the input boundary before strict wire/owner validation.
export function normalizeSliderValue(value:unknown,min:number,max:number,step:number):number|null {
  if(typeof value!=='number'||![value,min,max,step].every(Number.isFinite)||step<=0||max<min)return null;
  const index=Math.max(0,Math.min(Math.floor((max-min)/step+1e-9),Math.round((value-min)/step)));
  return Number((min+index*step).toFixed(10));
}
