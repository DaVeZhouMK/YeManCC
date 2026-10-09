// FPS is independent from performance combinations; 0 disables only RTSS capping.
export type FrameRateSide = 'ac' | 'dc';
export const FRAME_RATE_DEFAULTS: Record<FrameRateSide, number> = { ac: 120, dc: 60 };
export const FRAME_RATE_CEILINGS = [0,30,60,90,120,200,300];
export interface FrameRateSetting { fps:number;ceiling:number;lastFps:number; }
export type FrameRatePair = Record<FrameRateSide,FrameRateSetting>;
export function frameRateSetting(value:unknown,fallback:unknown=0):FrameRateSetting {
  const raw=value && typeof value==='object'?value as Partial<FrameRateSetting>:{fps:typeof value==='number'?value:undefined};
  const prior=fallback && typeof fallback==='object'?fallback as Partial<FrameRateSetting>:{fps:typeof fallback==='number'?fallback:0};
  const limit=(v:unknown,d:number)=>typeof v==='number' && Number.isFinite(v)?Math.max(0,Math.min(300,Math.round(v))):d;
  const fps=limit(raw.fps,limit(prior.fps,0));
  const lastFps=Math.max(20,limit(fps>0?fps:raw.lastFps,limit(prior.lastFps,90)) || 90);
  const ceiling=fps===0?0:FRAME_RATE_CEILINGS.find(c=>c>=fps && c>0) ?? 300;
  return {fps,lastFps,ceiling:fps>0 && FRAME_RATE_CEILINGS.includes(Number(raw.ceiling)) && Number(raw.ceiling)>=fps?Number(raw.ceiling):ceiling};
}
export function frameRatePair(value:unknown,ac:unknown=FRAME_RATE_DEFAULTS.ac,dc:unknown=FRAME_RATE_DEFAULTS.dc):FrameRatePair {
  const raw=value && typeof value==='object'?value as Partial<FrameRatePair>:{};
  return {ac:frameRateSetting(raw.ac,ac),dc:frameRateSetting(raw.dc,dc)};
}
export interface DedicatedFrameRate {enabled:boolean;acFps:number;dcFps:number;acCeiling?:number;dcCeiling?:number;acLastFps?:number;dcLastFps?:number;}
export function dedicatedFrameRatePair(value:DedicatedFrameRate|undefined,fallback:FrameRatePair):FrameRatePair {
  if(!value || value.enabled===false)return frameRatePair(fallback);
  return {ac:frameRateSetting({fps:value.acFps,ceiling:value.acCeiling,lastFps:value.acLastFps},fallback.ac),dc:frameRateSetting({fps:value.dcFps,ceiling:value.dcCeiling,lastFps:value.dcLastFps},fallback.dc)};
}
export function dedicatedFrameRateRecord(pair:FrameRatePair):DedicatedFrameRate {
  return {enabled:true,acFps:pair.ac.fps,dcFps:pair.dc.fps,acCeiling:pair.ac.ceiling,dcCeiling:pair.dc.ceiling,acLastFps:pair.ac.lastFps,dcLastFps:pair.dc.lastFps};
}
