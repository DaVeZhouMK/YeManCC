export interface GameMediaPlayback {start:number;end:number;fit?:'cover'|'contain';}
export const DEFAULT_GAME_MEDIA_PLAYBACK:Readonly<GameMediaPlayback>={start:8,end:38,fit:'cover'};
export function effectiveGameMediaRange(duration:number,playback:GameMediaPlayback){
  if(!Number.isFinite(duration)||duration<=0)return {start:0,end:0};
  const requested=Math.max(0,Number(playback.start)||0),length=Number(playback.end)>requested?Number(playback.end)-requested:0;
  const start=requested<duration-1?requested:0;return {start,end:length?Math.min(duration,start+length):duration};
}
export function applyGameMediaRange(video:HTMLVideoElement,playback:GameMediaPlayback){
  if(video.readyState<1)return;const range=effectiveGameMediaRange(video.duration,playback);
  if(video.currentTime<range.start||range.end>range.start&&video.currentTime>=range.end)video.currentTime=range.start;
}

// Steam samples the whole playable clip. Intro skip remains the loop boundary;
// editor segment length is not a Steam playback cap. Keep the initial seek
// separate so a loop returns to the intro boundary rather than rerolling.
export function randomGameMediaRange(duration:number,playback:GameMediaPlayback,random:()=>number=Math.random){
  const base=effectiveGameMediaRange(duration,{start:playback.start,end:0});
  if(base.end<=base.start)return {...base,initialStart:base.start};
  const last=Math.max(base.start,duration-Math.min(1,duration-base.start));
  const sampled=random(),unit=Number.isFinite(sampled)?Math.max(0,Math.min(1,sampled)):0;
  return {...base,initialStart:base.start+(last-base.start)*unit};
}
