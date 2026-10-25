import {invoke,on} from './ipc';
import {SteamWallpaperDelivery} from './steamWallpaperDelivery';
import {registerDeckyMirrorExtension} from './deckyMirrorExtensions';
import type {MirrorPeer} from './deckyMirrorRelay';
import {sharedGameMedia,steamWallpaperEnabled,gameMediaPairComplete,type GameMediaEntry,type SharedGameMedia,wallpaperTargetKey} from './sharedGameMedia';
import type {WallpaperTarget} from './steamMediaSource';
import {getBackgroundOpacity,getBackgroundBlur} from './background';

// A shortcut must come from the current account's VDF. Unknown EXEs enter the
// shared owner's identification queue, rather than identifying on the render path.
export async function resolveWallpaperTarget(id:string,name:string,call=invoke):Promise<WallpaperTarget|null>{
  if(!/^\d{1,20}$/.test(id))return null;
  const numeric=BigInt(id);if(numeric<=0n||numeric>18446744073709551615n)return null;
  if(numeric<=2147483647n)return {appId:Number(numeric),name,verified:true};
  const local=await call<{executable?:string;name?:string;appId?:number;verified?:boolean;shortcutAppId?:string;steamAccount?:string;accountId?:string}>('gameMedia.shortcut',{gameId:id},{timeoutMs:5000});
  if(!local.executable)return null;
  return {appId:local.verified&&Number.isSafeInteger(local.appId)&&Number(local.appId)>0&&Number(local.appId)<=0x7fffffff?local.appId!:0,name:local.name||name,executable:local.executable,verified:local.verified===true,sourceTier:2,catalogSource:'steam-shortcut',shortcutAppId:local.shortcutAppId,steamAccount:local.steamAccount||local.accountId};
}
interface WallpaperHostPorts {
  invoke:typeof invoke;
  media:Pick<SharedGameMedia,'lookup'|'ensure'|'subscribe'|'playbackFailed'>&Partial<Pick<SharedGameMedia,'selectSteamTarget'|'prewarm'>>;
  enabled:()=>boolean;
  register:typeof registerDeckyMirrorExtension;
  appearance:()=>{opacity:number;blur:number};
  inventory:()=>void;
  schedule:typeof setTimeout;cancel:typeof clearTimeout;
  onResume:(handler:(event:{generation?:number})=>void)=>()=>void;
}
export function startSteamWallpaperHost(overrides:Partial<WallpaperHostPorts>={}){
  const port:WallpaperHostPorts={schedule:(...args)=>globalThis.setTimeout(...args),cancel:timer=>globalThis.clearTimeout(timer),invoke,media:sharedGameMedia,enabled:steamWallpaperEnabled,register:registerDeckyMirrorExtension,appearance:()=>({opacity:getBackgroundOpacity(),blur:getBackgroundBlur()}),inventory:()=>sharedGameMedia.changed('inventory'),onResume:handler=>typeof window==='undefined'?()=>{}:on('power.resumed',handler),...overrides};
  // Scopes local ordering only; peer/runId continue to own transport identity.
  const stateOwner=Date.now().toString(36)+'-'+Math.random().toString(36).slice(2);
  let peer:MirrorPeer|null=null,ended=false,ready=false,epoch=0,announced:boolean|undefined,lastFrame='',settingsStamp='',resumeGeneration=0,inventoryVisible=false,stateRevision=0;
  let context:{seq:number;appId:string;name:string;visible:boolean;key?:string;target?:Promise<WallpaperTarget|null>}|null=null;
  let delivered:{key:string;assetId:string;bindingRevision:number;kind?:string;seq:number}|null=null;
  const delivery=new SteamWallpaperDelivery({schedule:port.schedule,cancel:port.cancel,send:(target,message)=>port.invoke<boolean>('deckySidebar.mirrorReply',{connection:target.connection,message:{...message,runId:target.runId}},{timeoutMs:5000})});
  let selectionRetry:ReturnType<typeof setTimeout>|null=null;
  const cancelSelectionRetry=()=>{if(selectionRetry!==null)port.cancel(selectionRetry);selectionRetry=null;};
  const retrySelection=(current:NonNullable<typeof context>)=>{
    if(selectionRetry!==null||ended||current!==context||!current.visible||!port.enabled()||!peer)return;
    selectionRetry=port.schedule(()=>{selectionRetry=null;if(!ended&&current===context&&current.visible&&port.enabled()&&peer)void publish();},20000);
  };
  const reply=(message:Record<string,unknown>,force=false)=>{if(!peer||ended)return;delivery.put(message.type==='wallpaper-ready'?'ready':'frame',{...message,stateOwner,stateRevision},force);};
  const announce=(force=false,contextSeq?:number)=>{if(!peer||ended)return;const enabled=ready&&port.enabled();if(!force&&enabled===announced)return;if(enabled!==announced)++stateRevision;announced=enabled;reply({type:'wallpaper-ready',enabled,...(contextSeq!==undefined?{contextSeq}:{})},force);};
  const output=(message:Record<string,unknown>)=>{const signature=JSON.stringify(message);if(signature===lastFrame)return;lastFrame=signature;reply(message);};
  async function publish(){
    const at=++epoch,current=context;if(!ready||ended||!peer)return;
    if(!port.enabled()||!current||!current.visible){cancelSelectionRetry();inventoryVisible=false;port.media.selectSteamTarget?.(null);delivered=null;output({type:'wallpaper',enabled:port.enabled(),contextSeq:current?.seq??0,appId:current?.appId,entry:null});return;}
    if(!inventoryVisible){inventoryVisible=true;port.inventory();}
    // Keep this promise only for this selection event. Reconnects/new contexts
    // recheck the active Steam account; unrelated media updates reuse it.
    if(!current.target){
      cancelSelectionRetry();
      const pending:Promise<WallpaperTarget|null>=resolveWallpaperTarget(current.appId,current.name,port.invoke).catch(()=>{
        if(current.target===pending){current.target=undefined;retrySelection(current);}return null;
      });
      current.target=pending;
    }
    const target=await current.target;
    if(at!==epoch||ended||current!==context||!port.enabled())return;
    port.media.selectSteamTarget?.(target);
    let cacheUnavailable=false;
    const cached=target?await port.media.lookup(target).catch(()=>{cacheUnavailable=true;return null;}):null;
    if(at!==epoch||ended||current!==context||!port.enabled())return;
    if(cacheUnavailable)retrySelection(current);else if(target)cancelSelectionRetry();
    current.key=cached?.key||(target?wallpaperTargetKey(target):undefined);
    const send=(entry:GameMediaEntry|null)=>{
      if(entry?.key)current.key=entry.key;
      delivered=entry?.ready&&entry.asset?{key:entry.key,assetId:entry.asset.id,bindingRevision:entry.bindingRevision,kind:entry.asset.kind,seq:current.seq}:null;
      output({type:'wallpaper',enabled:true,contextSeq:current.seq,appId:current.appId,entry:entry?.ready?{assetId:entry.asset?.id,revision:entry.asset?.revision,url:entry.url,kind:entry.asset?.kind,playback:entry.playback,bindingRevision:entry.bindingRevision}:null,...port.appearance(),status:entry?.status??'missing'});
    };
    send(cached);
    if(!cacheUnavailable&&target&&(target.appId>0||target.executable)&&!cached?.deleted&&!(cached?.ready&&!cached.refreshRequested&&gameMediaPairComplete(cached,Date.now()))){
      const result=await port.media.ensure(target,'steam',true,true).catch(()=>null);
      if(at===epoch&&!ended&&current===context&&port.enabled())send(result);
    }
  }
  const off=port.register({prefix:'wallpaper.',connected(value){port.media.selectSteamTarget?.(null);peer=value.connected===false?null:value;delivery.reset(peer);cancelSelectionRetry();inventoryVisible=false;context=null;delivered=null;announced=undefined;lastFrame='';++epoch;if(peer)announce();},request(event){
    if(!peer||event.connection!==peer.connection||event.runId!==peer.runId)return;
    const request=event.request;
    // Old frontend neighbor messages are intentionally ignored: no speculative I/O.
    if(request.command==='wallpaper.status'){
      if(!port.enabled()||request.status!=='playback-failed'||!delivered||request.contextSeq!==delivered.seq||request.appId!==context?.appId||request.assetId!==delivered.assetId||request.bindingRevision!==delivered.bindingRevision)return;
      const failed=delivered;delivered=null;void port.media.playbackFailed(failed).catch(()=>{});return;
    }
    if(request.command!=='wallpaper.context')return;
    const seq=Number(request.contextSeq),id=String(request.appId??'');
    if(!Number.isSafeInteger(seq)||seq<0||(context&&seq<=context.seq)||!/^\d{1,20}$/.test(id)||BigInt(id)<=0n||BigInt(id)>18446744073709551615n)return;
    if(request.requestReady===true)announce(true,seq);
    cancelSelectionRetry();port.media.selectSteamTarget?.(null);
    context={seq,appId:id,name:typeof request.name==='string'?request.name.slice(0,512):'',visible:request.visible===true};void publish();
  }});
  const changed=(reason='asset',key?:string)=>{
    if(ended||reason==='retry'||reason==='inventory'||reason==='demand'||reason==='policy')return;
    if(reason==='asset'&&!context?.visible)return;
    if(reason==='asset'&&key&&context?.key&&key!==context.key)return;
    if(reason==='settings'){const stamp=JSON.stringify({ready,enabled:port.enabled(),...port.appearance()});if(stamp===settingsStamp)return;settingsStamp=stamp;}
    announce();void publish();
  };
  // Use the committed native wake edge, not a timer or sidebar snapshot health.
  // Announce a decoder reset, but wait for a fresh frontend visibility proof.
  const offResume=port.onResume(event=>{
    const generation=Number(event?.generation);
    if(ended||!Number.isSafeInteger(generation)||generation<=resumeGeneration)return;
    resumeGeneration=generation;cancelSelectionRetry();++epoch;delivered=null;lastFrame='';
    inventoryVisible=false;if(context){context.target=undefined;context.visible=false;}
    if(!ready||!peer||!port.enabled())return;
    reply({type:'wallpaper-ready',enabled:true,resumeGeneration:generation});
    void publish();
  });
  const unsubscribe=port.media.subscribe(changed);
  return {ready(){ready=true;changed('settings');},stop(){ended=true;cancelSelectionRetry();delivery.stop();++epoch;port.media.selectSteamTarget?.(null);offResume();unsubscribe();off();peer=null;context=null;delivered=null;}};
}
