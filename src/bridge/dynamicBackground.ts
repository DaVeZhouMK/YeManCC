import {wallpaperPowerBlocked,captureWallpaperPower,wallpaperPowerCurrent} from './wallpaperPowerPolicy';
import { invoke } from './ipc';
import { hasWallpaperBinding, sharedGameMedia, type GameMediaEntry } from './sharedGameMedia';
import {gameMediaRevisions} from './gameMediaRevisionFence';
import { getDynamicBackgroundConfig, resolveSteamWallpaperIdentity, type DynamicBackgroundResult } from './steamMediaSource';
import { detectGame, validateGameTarget, type DetectedGame } from './gamedetect';
import { getSharedPowerLifecycle, isSharedPowerLifecycleAdmitted, onSharedPowerLifecycle, type SharedPowerLifecycleSnapshot } from './powerLifecycleState';
import type { DynamicBackgroundState } from './background';
export { cleanGameTitle, normalizeGameTitle, scoreSteamTitle, stripLaunchModeSuffix, getDynamicBackgroundConfig, setDynamicBackgroundEnabled } from './steamMediaSource';
export type {DynamicBackgroundConfig,DynamicBackgroundResult} from './steamMediaSource';
export interface DynamicBackgroundFallback {key:string;assetId:string;bindingRevision:number;pid:number;processCreated:string;}
function fallbackWire(fallback?:DynamicBackgroundFallback){
  return fallback?{wallpaperPlaybackFallback:{assetId:fallback.assetId,bindingRevision:fallback.bindingRevision}}:{};
}
export function isFailedDynamicVariant(state:DynamicBackgroundState|null|undefined,fallback:DynamicBackgroundFallback|null){
  return !!fallback&&state?.mediaReducedPlayback===true&&state.pid===fallback.pid&&state.processCreated===fallback.processCreated&&
    state.mediaKey===fallback.key&&state.mediaAssetId===fallback.assetId&&state.mediaBindingRevision===fallback.bindingRevision;
}
// Native/cache replies are runtime input: a deleted or malformed asset cannot
// authorize a renderer, even when an old reply still advertises ready=true.
function backgroundEntryUrl(entry:GameMediaEntry):string {
  return entry.localUrl?.trim() || entry.url?.trim() || '';
}
function usableBackgroundEntry(entry:GameMediaEntry|null|undefined):entry is GameMediaEntry {
  return !!entry && entry.ready===true && entry.deleted!==true &&
    typeof entry.key==='string' && !!entry.key && Number.isSafeInteger(entry.bindingRevision) && entry.bindingRevision>=0 &&
    (entry.url===undefined || typeof entry.url==='string') && (entry.localUrl===undefined || typeof entry.localUrl==='string') &&
    !!backgroundEntryUrl(entry) &&
    (entry.asset?.kind===undefined || entry.asset.kind==='image' || entry.asset.kind==='video');
}
function backgroundReadAdmitted(state:DynamicBackgroundState):boolean {
  return getDynamicBackgroundConfig().enabled && !wallpaperPowerBlocked() && !gameMediaRevisions.outdated(state);
}
const backgroundInFlight=new Map<string,{pending:Promise<DynamicBackgroundResult|null>;signal:AbortSignal}>();
function sameBackgroundSleep(captured:SharedPowerLifecycleSnapshot,state:SharedPowerLifecycleSnapshot|null):boolean {
  return !!state&&isSharedPowerLifecycleAdmitted(state)&&captured.generation===state.generation&&
    captured.suspendSerial===state.suspendSerial;
}
async function withBackgroundPower<T>(sleep:SharedPowerLifecycleSnapshot,acdc:number,fallback:T,
    run:(current:()=>boolean,signal:AbortSignal)=>Promise<T>,cancellation=new AbortController()):Promise<T>{
  const current=()=>!cancellation.signal.aborted&&sameBackgroundSleep(sleep,getSharedPowerLifecycle())&&
    wallpaperPowerCurrent(acdc)&&getDynamicBackgroundConfig().enabled;
  const stop=onSharedPowerLifecycle(state=>{
    if(!sameBackgroundSleep(sleep,state))cancellation.abort();
  },{observeSuspendSerial:true});
  try{
    if(!current())return fallback;
    const retired=new Promise<T>(resolve=>cancellation.signal.addEventListener('abort',()=>resolve(fallback),{once:true}));
    // The caller can start new work even while an old media read is still pending.
    return await Promise.race([retired,run(current,cancellation.signal)]);
  }finally{stop();}
}
export async function refreshDynamicBackground(game:DetectedGame|null,fallback?:DynamicBackgroundFallback):Promise<DynamicBackgroundResult|null>{
  if(!game||(!getDynamicBackgroundConfig().enabled||wallpaperPowerBlocked()))return null;
  const sleep=getSharedPowerLifecycle();
  if(!sleep||!isSharedPowerLifecycleAdmitted(sleep))return null;
  const acdc=captureWallpaperPower();
  if(fallback&&(fallback.pid!==game.pid||fallback.processCreated!==game.processCreated))fallback=undefined;
  const key=JSON.stringify([acdc,sleep.generation,sleep.suspendSerial,game.pid,game.processCreated,game.path.split('/').join(String.fromCharCode(92)).toLowerCase(),fallback?.assetId,fallback?.bindingRevision]);
  const previous=backgroundInFlight.get(key);if(previous&&!previous.signal.aborted)return previous.pending;
  const cancellation=new AbortController();
  const pending=withBackgroundPower<DynamicBackgroundResult|null>(sleep,acdc,null,
    (current,signal)=>refreshDynamicBackgroundImpl(game,sleep,acdc,current,signal,fallback),cancellation);
  const work={pending,signal:cancellation.signal};backgroundInFlight.set(key,work);
  try{return await pending;}finally{if(backgroundInFlight.get(key)===work)backgroundInFlight.delete(key);}
}
async function refreshDynamicBackgroundImpl(game:DetectedGame,sleep:SharedPowerLifecycleSnapshot,acdc:number,
    admitted:()=>boolean,signal:AbortSignal,fallback?:DynamicBackgroundFallback):Promise<DynamicBackgroundResult|null>{
  if(!game.processCreated)throw Error('游戏识别阀门未提供进程身份');
  const mediaReadEpoch=gameMediaRevisions.capture();
  const exact={executable:game.path,currentGameWallpaper:true,localWallpaperDelivery:true,wallpaperPlayback:true,...fallbackWire(fallback)} as const;
  const cached=await sharedGameMedia.lookup(exact).catch(()=>null);
  if(!admitted())return null;
  const target={...(cached&&hasWallpaperBinding(cached)?{appId:cached.appId||0,name:cached.name||game.title,executable:game.path,verified:cached.identityVerified===true}:await resolveSteamWallpaperIdentity(game,signal)),legacyTarget:{pid:game.pid,processCreated:game.processCreated}};
  if(!admitted())return null;
  const legacyArgs={...target,...target.legacyTarget,localWallpaperDelivery:true,wallpaperPlayback:true,...fallbackWire(fallback)};
  const migrated=!hasWallpaperBinding(cached)&&target.verified?await invoke<GameMediaEntry>('gameMedia.legacy',legacyArgs,{signal}).catch(()=>null):null;
  if(!admitted())return null;
  let entry=cached&&hasWallpaperBinding(cached)?cached:migrated&&hasWallpaperBinding(migrated)?migrated:null;
  const foundBinding=!!entry;
  if(!entry){
    await sharedGameMedia.ensure(target,'ymcc',true,true);
    if(!admitted())return null;
    // The preparation reply contains editor/download URLs, not playback URLs.
    entry=await sharedGameMedia.lookup(exact);
    if(admitted()&&!hasWallpaperBinding(entry)&&target.verified&&target.appId>0)
      entry=await sharedGameMedia.lookup({...target,localWallpaperDelivery:true,wallpaperPlayback:true,...fallbackWire(fallback)});
  }
  // Shared and verified legacy bytes render now, independently of network work.
  // The authoritative exact read already checked both files in this case.
  const completeCache=cached===entry&&entry?.staticReady===true&&entry.dynamicReady===true&&!entry.refreshRequested&&usableBackgroundEntry(entry);
  if(admitted()&&foundBinding&&entry&&!entry.currentGameWallpaperFallback&&!completeCache)
    void withBackgroundPower(sleep,acdc,undefined,async (current,signal)=>{
      if(cached?.ready&&!cached.dynamicReady&&cached.asset?.kind!=='video'&&target.verified)
        await invoke('gameMedia.legacy',legacyArgs,{signal}).catch(()=>null);
      if(current())await sharedGameMedia.ensure(target,'ymcc',true,true);
    }).catch(()=>{});
  if(!admitted())return null;
  if(!usableBackgroundEntry(entry)){
    if(entry?.wallpaper30Limited&&hasWallpaperBinding(entry))window.dispatchEvent(new CustomEvent('dynamic-background:waiting-for-playback',{
      detail:{key:entry.key,pid:game.pid,processCreated:game.processCreated},
    }));
    return null;
  }
  // Retain the exact native valve admission for this renderer; the shared asset
  // is reusable while performance/input still require the original process.
  const current=await validateGameTarget(game);
  if(!current||current.pid!==game.pid||current.processCreated!==game.processCreated)return null;
  const state:DynamicBackgroundState={enabled:true,mediaReadEpoch,kind:entry.asset?.kind||'video',url:backgroundEntryUrl(entry),appId:entry.appId||target.appId,gameName:entry.name||target.name,source:'shared-cache',pid:game.pid,processCreated:game.processCreated,mediaKey:entry.key,mediaAssetId:entry.asset?.id,mediaBindingRevision:entry.bindingRevision,mediaReducedPlayback:entry.asset?.reducedPlayback,playback:entry.playback};
  if(!admitted()||!backgroundReadAdmitted(state))return null;
  window.dispatchEvent(new CustomEvent('dynamic-background:loaded',{detail:state}));
  return {state,appId:entry.appId||target.appId,gameName:entry.name||target.name,source:state.kind==='video'?'video':'background',detectedTitle:target.name,matchedBy:'cache'};
}
// Resolve only the original of the already selected binding. This path must
// not scrape, download, edit the cache or emit a separate loaded event.
export async function readDynamicBackgroundOriginal(game:DetectedGame,failed:DynamicBackgroundFallback):Promise<DynamicBackgroundState|null>{
  if(!game.processCreated||failed.pid!==game.pid||failed.processCreated!==game.processCreated||!failed.key||!failed.assetId)return null;
  const sleep=getSharedPowerLifecycle();
  if(!sleep||!isSharedPowerLifecycleAdmitted(sleep))return null;
  return withBackgroundPower<DynamicBackgroundState|null>(sleep,captureWallpaperPower(),null,async current=>{
    const mediaReadEpoch=gameMediaRevisions.capture();
    const entry=await sharedGameMedia.lookup({key:failed.key,localWallpaperDelivery:true,wallpaperPlayback:true,...fallbackWire(failed)});
    if(!current()||!usableBackgroundEntry(entry)||entry.key!==failed.key||entry.bindingRevision!==failed.bindingRevision||
      entry.asset?.kind!=='video'||!entry.asset.id||entry.asset.id===failed.assetId||entry.asset.reducedPlayback===true)return null;
    if(!await validateGameTarget(game))return null;
    const state:DynamicBackgroundState={enabled:true,url:backgroundEntryUrl(entry),kind:'video',mediaReadEpoch,appId:entry.appId,gameName:entry.name,
      pid:game.pid,processCreated:game.processCreated,mediaKey:entry.key,mediaAssetId:entry.asset.id,mediaBindingRevision:entry.bindingRevision,playback:entry.playback};
    return current()&&backgroundReadAdmitted(state)?state:null;
  });
}
export async function getDynamicBackgroundState():Promise<DynamicBackgroundState>{
  const empty={enabled:false,url:''};
  const sleep=getSharedPowerLifecycle();
  if(!sleep||!isSharedPowerLifecycleAdmitted(sleep)||!getDynamicBackgroundConfig().enabled||wallpaperPowerBlocked())return empty;
  return withBackgroundPower<DynamicBackgroundState>(sleep,captureWallpaperPower(),empty,async current=>{
    const mediaReadEpoch=gameMediaRevisions.capture();
    const game=await detectGame(true);if(!current()||!game?.processCreated)return empty;
    const entry=await sharedGameMedia.lookup({executable:game.path,currentGameWallpaper:true,localWallpaperDelivery:true,wallpaperPlayback:true});if(!current()||!usableBackgroundEntry(entry))return empty;
    const target=await validateGameTarget(game);if(!target||target.pid!==game.pid||target.processCreated!==game.processCreated)return empty;
    const state:DynamicBackgroundState={enabled:true,mediaReadEpoch,url:backgroundEntryUrl(entry),kind:entry.asset?.kind||'video',appId:entry.appId,gameName:entry.name,pid:game.pid,processCreated:game.processCreated,mediaKey:entry.key,mediaAssetId:entry.asset?.id,mediaBindingRevision:entry.bindingRevision,mediaReducedPlayback:entry.asset?.reducedPlayback,playback:entry.playback};
    return current()&&backgroundReadAdmitted(state)?state:empty;
  });
}
