import { invoke } from './ipc';
import { sharedGameMedia, type GameMediaEntry } from './sharedGameMedia';
import {gameMediaRevisions} from './gameMediaRevisionFence';
import { getDynamicBackgroundConfig, resolveSteamWallpaperIdentity, type DynamicBackgroundResult } from './steamMediaSource';
import { detectGame, validateGameTarget, type DetectedGame } from './gamedetect';
import type { DynamicBackgroundState } from './background';
export { cleanGameTitle, normalizeGameTitle, scoreSteamTitle, stripLaunchModeSuffix, getDynamicBackgroundConfig, setDynamicBackgroundEnabled } from './steamMediaSource';
export type {DynamicBackgroundConfig,DynamicBackgroundResult} from './steamMediaSource';
// Native/cache replies are runtime input: a deleted or malformed asset cannot
// authorize a renderer, even when an old reply still advertises ready=true.
function usableBackgroundEntry(entry:GameMediaEntry|null|undefined):entry is GameMediaEntry {
  return !!entry && entry.ready===true && entry.deleted!==true &&
    typeof entry.key==='string' && !!entry.key && Number.isSafeInteger(entry.bindingRevision) && entry.bindingRevision>=0 &&
    typeof entry.url==='string' && !!entry.url.trim() &&
    (entry.localUrl===undefined || typeof entry.localUrl==='string') &&
    (entry.asset?.kind===undefined || entry.asset.kind==='image' || entry.asset.kind==='video');
}
function backgroundReadAdmitted(state:DynamicBackgroundState):boolean {
  return getDynamicBackgroundConfig().enabled && !gameMediaRevisions.outdated(state);
}
const backgroundInFlight=new Map<string,Promise<DynamicBackgroundResult|null>>();
export async function refreshDynamicBackground(game:DetectedGame|null):Promise<DynamicBackgroundResult|null>{
  if(!game||!getDynamicBackgroundConfig().enabled)return null;
  const key=JSON.stringify([game.pid,game.processCreated,game.path.split('/').join(String.fromCharCode(92)).toLowerCase()]);const previous=backgroundInFlight.get(key);if(previous)return previous;
  const pending=refreshDynamicBackgroundImpl(game);backgroundInFlight.set(key,pending);
  try{return await pending;}finally{if(backgroundInFlight.get(key)===pending)backgroundInFlight.delete(key);}
}
async function refreshDynamicBackgroundImpl(game:DetectedGame):Promise<DynamicBackgroundResult|null>{
  if(!game||!getDynamicBackgroundConfig().enabled)return null;
  if(!game.processCreated)throw Error('游戏识别阀门未提供进程身份');
  const mediaReadEpoch=gameMediaRevisions.capture();
  const cached=await sharedGameMedia.lookup({executable:game.path}).catch(()=>null);
  const target={...(cached?.ready?{appId:cached.appId||0,name:cached.name||game.title,executable:game.path,verified:cached.identityVerified===true}:await resolveSteamWallpaperIdentity(game)),legacyTarget:{pid:game.pid,processCreated:game.processCreated}};
  const migrated=!cached?.ready&&target.verified?await invoke<import('./sharedGameMedia').GameMediaEntry>('gameMedia.legacy',{...target,...target.legacyTarget}).catch(()=>null):null;
  const entry=cached?.ready&&!cached.deleted?cached:migrated?.ready?migrated:await sharedGameMedia.ensure(target,'ymcc',true,true);
  // Shared and verified legacy bytes render now, independently of network work.
  if((cached?.ready||migrated?.ready)&&usableBackgroundEntry(entry))void (async()=>{
    if(cached?.ready&&!cached.dynamicReady&&cached.asset?.kind!=='video'&&target.verified)await invoke('gameMedia.legacy',{...target,...target.legacyTarget}).catch(()=>null);
    await sharedGameMedia.ensure(target,'ymcc',true,true);
  })().catch(()=>{});
  if(!usableBackgroundEntry(entry))return null;
  // Retain the exact native valve admission for this renderer; the shared asset
  // is reusable while performance/input still require the original process.
  const current=await validateGameTarget(game);
  if(!current||current.pid!==game.pid||current.processCreated!==game.processCreated)return null;
  const state:DynamicBackgroundState={enabled:true,mediaReadEpoch,kind:entry.asset?.kind||'video',url:entry.localUrl||entry.url,appId:entry.appId||target.appId,gameName:entry.name||target.name,source:'shared-cache',pid:game.pid,processCreated:game.processCreated,mediaKey:entry.key,mediaAssetId:entry.asset?.id,mediaBindingRevision:entry.bindingRevision,playback:entry.playback};
  if(!backgroundReadAdmitted(state))return null;
  window.dispatchEvent(new CustomEvent('dynamic-background:loaded',{detail:state}));
  return {state,appId:entry.appId||target.appId,gameName:entry.name||target.name,source:state.kind==='video'?'video':'background',detectedTitle:target.name,matchedBy:'cache'};
}
export async function getDynamicBackgroundState():Promise<DynamicBackgroundState>{
  if(!getDynamicBackgroundConfig().enabled)return {enabled:false,url:''};
  const mediaReadEpoch=gameMediaRevisions.capture();
  const game=await detectGame(true);if(!game?.processCreated)return {enabled:false,url:''};
  const entry=await sharedGameMedia.lookup({executable:game.path});if(!usableBackgroundEntry(entry))return {enabled:false,url:''};
  const current=await validateGameTarget(game);if(!current||current.pid!==game.pid||current.processCreated!==game.processCreated)return {enabled:false,url:''};
  const state:DynamicBackgroundState={enabled:true,mediaReadEpoch,url:entry.localUrl||entry.url,kind:entry.asset?.kind||'video',appId:entry.appId,gameName:entry.name,pid:game.pid,processCreated:game.processCreated,mediaKey:entry.key,mediaAssetId:entry.asset?.id,mediaBindingRevision:entry.bindingRevision,playback:entry.playback};
  return backgroundReadAdmitted(state)?state:{enabled:false,url:''};
}
