// Development-only integration probe. No hardware API, settings write or polling.
export const PROBE_SIGNATURE = 'ymcc-steam-hud-probe-v1';
export const MAX_PROBE_MS = 180000;
export type ProbeScope = 'overlay-only' | 'main-window-test';
export interface ProbeConfig { kind:'integration-test'; runId:string; expiresAt:number; scope:ProbeScope; }
export interface ProbeData {
  runId:string; seq:number; collectedAt:number; source:'fixture'|'ymcc-uniform';
  fps:number|null; cpuPowerW:number|null; cpuTempC:number|null; gpuUsage:number|null;
}
export interface ProbeAppearance { backgroundAlpha:number; textAlpha:number; layout?:'bar'|'corner'; }
export const DEFAULT_PROBE_APPEARANCE:Readonly<ProbeAppearance> = /* @__PURE__ */ Object.freeze({backgroundAlpha:0.92,textAlpha:1});
export function validProbeAppearance(value:unknown):value is ProbeAppearance {
  const v=value as ProbeAppearance;
  return !!v && (v.layout===undefined||v.layout==='bar'||v.layout==='corner') && ['backgroundAlpha','textAlpha'].every(key=>{
    const n=v[key as keyof ProbeAppearance];return typeof n==='number'&&Number.isFinite(n)&&n>=0&&n<=1;
  });
}
// Alpha is applied independently to panel paint and glyph paint, never to the container.
export function probePaint(v:ProbeAppearance) {
  return {background:`rgba(16,22,31,${v.backgroundAlpha})`,
    text:`rgba(241,245,250,${v.textAlpha})`,accent:`rgba(133,203,231,${v.textAlpha})`,
    secondary:`rgba(176,190,205,${v.textAlpha})`,warning:`rgba(229,197,138,${v.textAlpha})`};
}
export interface ProbeTrace { event:string; at:number; detail?:Record<string,unknown>; }
export function validProbeConfig(value:unknown,now:number):value is ProbeConfig {
  const v=value as ProbeConfig;
  return !!v && v.kind==='integration-test' && /^hud-probe-[a-zA-Z0-9-]{1,55}$/.test(v.runId) &&
    Number.isSafeInteger(v.expiresAt) && v.expiresAt>now && v.expiresAt-now<=MAX_PROBE_MS &&
    (v.scope==='overlay-only'||v.scope==='main-window-test');
}
export function validProbeData(value:unknown,config:ProbeConfig,now:number,lastSeq:number):value is ProbeData {
  const v=value as ProbeData;
  if(!v||v.runId!==config.runId||!Number.isSafeInteger(v.seq)||v.seq<=lastSeq||
    !Number.isFinite(v.collectedAt)||v.collectedAt>now+1000||now-v.collectedAt>6000||
    !['fixture','ymcc-uniform'].includes(v.source))return false;
  return ['fps','cpuPowerW','cpuTempC','gpuUsage'].every(key=>{
    const n=v[key as keyof ProbeData]; return n===null || typeof n==='number'&&Number.isFinite(n)&&n>=0&&n<=10000;
  }) && (v.gpuUsage===null||v.gpuUsage<=100);
}
export const HOOK_MARKERS = ['AddMinimumCompositionStateRequest','ChangeMinimumCompositionStateRequest','RemoveMinimumCompositionStateRequest'] as const;
// Same export signature used by MagicBlack 2.0.0, commit 6f61a516f64b65d88e5b30248821a5a3b80fcb28 (GPL-3.0).
export function isCompositionHook(fn:unknown):boolean {
  if(typeof fn!=='function')return false;
  const s=Function.prototype.toString.call(fn);
  return HOOK_MARKERS.every(key=>s.includes(key)) && !s.includes('m_mapCompositionStateRequests');
}
export function uniqueExport(modules:unknown,predicate:(fn:Function)=>boolean):{fn:Function;module:string;key:string;exports:Record<string,unknown>}|null {
  const result:{fn:Function;module:string;key:string;exports:Record<string,unknown>}[]=[];
  const seen=new Set<Function>();
  const entries=modules instanceof Map?modules.entries():Object.entries((modules??{}) as Record<string,unknown>);
  for(const [id,raw] of entries){
    const mod=(raw as any)?.exports??raw;
    if(!mod||typeof mod!=='object')continue;
    for(const key of Object.keys(mod)){
      let fn;try{fn=mod[key];}catch{continue;}
      if(typeof fn==='function'&&!seen.has(fn)&&predicate(fn)){
        seen.add(fn);result.push({fn,module:String(id),key,exports:mod});
      }
    }
  }
  return result.length===1?result[0]:null;
}
export function makeProbeState(now=()=>Date.now()) {
  let config:ProbeConfig|null=null,data:ProbeData|null=null,appearance:ProbeAppearance={...DEFAULT_PROBE_APPEARANCE};
  const listeners=new Set<()=>void>(),traces:ProbeTrace[]=[];
  const trace=(event:string,detail?:Record<string,unknown>)=>{if(traces.length===160)traces.shift();traces.push({event,at:now(),detail});};
  const publish=()=>{for(const f of listeners)try{f();}catch{}};
  return {
    snapshot:()=>({config:config?{...config}:null,data:data?{...data}:null,appearance:{...appearance}}), signal:publish,
    traces:()=>structuredClone(traces),trace,
    subscribe(f:()=>void){listeners.add(f);return()=>{listeners.delete(f);};},
    start(value:unknown){if(config||!validProbeConfig(value,now()))return false;config={...value};data=null;appearance={...DEFAULT_PROBE_APPEARANCE};trace('start',{runId:config.runId,scope:config.scope});publish();return true;},
    update(value:unknown){if(!config||config.expiresAt<=now()||!validProbeData(value,config,now(),data?.seq??-1))return false;data={...value};trace('data',{seq:data.seq,source:data.source});publish();return true;},
    setAppearance(runId:string,value:unknown){
      if(!config||config.runId!==runId||config.expiresAt<=now()||!validProbeAppearance(value))return false;
      appearance={backgroundAlpha:value.backgroundAlpha,textAlpha:value.textAlpha,...(value.layout?{layout:value.layout}:{})};
      trace('appearance',{...appearance});publish();return true;
    },
    stop(runId:string,reason='stop'){if(!config||runId!==config.runId)return false;trace('stop',{runId,reason});config=null;data=null;appearance={...DEFAULT_PROBE_APPEARANCE};publish();return true;},
    counts:()=>({listeners:listeners.size,traces:traces.length}),
  };
}
