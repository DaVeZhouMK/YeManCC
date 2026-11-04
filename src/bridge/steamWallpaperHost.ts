import {invoke,on} from './ipc';
import {subscribeGameMediaResume} from './gameMediaPreparationPolicy';
import {SteamWallpaperDelivery} from './steamWallpaperDelivery';
import {registerDeckyMirrorExtension} from './deckyMirrorExtensions';
import type {MirrorPeer} from './deckyMirrorRelay';
import {sharedGameMedia,steamWallpaperEnabled,gameMediaPairComplete,type GameMediaEntry,type SharedGameMedia,type SteamMediaDemand,wallpaperTargetKey} from './sharedGameMedia';
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
export interface SteamWallpaperForegroundProof {source:string;known:boolean;foreground:boolean;current:boolean;mode:string;pid:number;birth:string;window:string;ownerEpoch:string;revision:number;reason?:string;}
export function steamWallpaperForegroundProof(raw:any):SteamWallpaperForegroundProof|null {
  if(!raw||raw.source!=='native-steam-bigpicture-foreground'||typeof raw.known!=='boolean'||typeof raw.foreground!=='boolean'||typeof raw.current!=='boolean'||!['bigpicture','desktop','unknown','stopped'].includes(raw.mode)||!Number.isSafeInteger(raw.pid)||raw.pid<0||raw.pid>0xffffffff||typeof raw.birth!=='string'||typeof raw.window!=='string'||typeof raw.ownerEpoch!=='string'||!raw.ownerEpoch||new TextEncoder().encode(raw.ownerEpoch).length>128||!Number.isSafeInteger(raw.revision)||raw.revision<0)return null;
  return raw;
}
export function steamWallpaperForegroundAllowed(raw:any){const value=steamWallpaperForegroundProof(raw);return !!value&&value.known&&value.foreground&&value.current&&value.mode==='bigpicture'&&value.pid>0&&/^[1-9][0-9]{0,19}$/.test(value.birth)&&/^[1-9][0-9]{0,19}$/.test(value.window)&&BigInt(value.window)<=18446744073709551615n&&BigInt(value.birth)<=18446744073709551615n;}
interface WallpaperHostPorts {
  invoke:typeof invoke;
  media:Pick<SharedGameMedia,'lookup'|'ensure'|'subscribe'|'playbackFailed'>&Partial<Pick<SharedGameMedia,'selectSteamTarget'|'prewarm'|'retryWhen'>>;
  enabled:()=>boolean;
  register:typeof registerDeckyMirrorExtension;
  appearance:()=>{opacity:number;blur:number};
  inventory:()=>void;
  now:()=>number;
  readForeground:()=>Promise<unknown>;
  onForeground:(handler:(value:unknown)=>void)=>()=>void;
  schedule:typeof setTimeout;cancel:typeof clearTimeout;
  onResume:(handler:(event:{generation?:number})=>void)=>()=>void;
}
export function startSteamWallpaperHost(overrides:Partial<WallpaperHostPorts>={}){
  const port:WallpaperHostPorts={now:Date.now,schedule:(...args)=>globalThis.setTimeout(...args),cancel:timer=>globalThis.clearTimeout(timer),invoke,media:sharedGameMedia,enabled:steamWallpaperEnabled,register:registerDeckyMirrorExtension,appearance:()=>({opacity:getBackgroundOpacity(),blur:getBackgroundBlur()}),inventory:()=>sharedGameMedia.changed('inventory'),readForeground:()=>invoke('gameMedia.steamForeground',{}, {timeoutMs:5000}),onForeground:handler=>typeof window==='undefined'?()=>{}:on('gameMedia.steamForegroundChanged',handler),onResume:handler=>typeof window==='undefined'?()=>{}:subscribeGameMediaResume(handler,on),...overrides};
  // Scopes local ordering only; peer/runId continue to own transport identity.
  const stateOwner=Date.now().toString(36)+'-'+Math.random().toString(36).slice(2);
  let peer:MirrorPeer|null=null,ended=false,ready=false,epoch=0,announced:boolean|undefined,lastFrame='',settingsStamp='',resumeGeneration=0,inventoryVisible=false,stateRevision=0;
  let context:{seq:number;appId:string;name:string;visible:boolean;key?:string;target?:Promise<WallpaperTarget|null>}|null=null;
  // One selected publication owns lookup and ensure until they settle. Dirty
  // notifications invalidate its result and retain one fresh trailing read.
  let publication:{selection:NonNullable<typeof context>;dirty:boolean}|null=null;
  const cancelPublication=()=>{++epoch;publication=null;};
  let delivered:{key:string;assetId:string;bindingRevision:number;kind?:string;seq:number}|null=null;
  type FailureHandoff={selection:NonNullable<typeof context>;failed:NonNullable<typeof delivered>;demand:SteamMediaDemand;attempt:number};
  let failureHandoff:FailureHandoff|null=null,failureTimer:ReturnType<typeof setTimeout>|null=null,failureVersion=0;
  const cancelFailureHandoff=()=>{++failureVersion;if(failureTimer!==null)port.cancel(failureTimer);failureTimer=null;failureHandoff=null;};
  const ownsFailure=(value:FailureHandoff)=>failureHandoff===value&&!ended&&ready&&!!peer&&port.enabled()&&context===value.selection&&value.selection.visible&&value.demand.current?.()===true;
  const sameFailedAsset=(entry:GameMediaEntry,value:FailureHandoff)=>entry.ready===true&&entry.key===value.failed.key&&entry.bindingRevision===value.failed.bindingRevision&&entry.asset?.id===value.failed.assetId&&entry.asset?.kind===value.failed.kind;
  const delivery=new SteamWallpaperDelivery({schedule:port.schedule,cancel:port.cancel,send:(target,message)=>port.invoke<boolean>('deckySidebar.mirrorReply',{connection:target.connection,message:{...message,runId:target.runId}},{timeoutMs:5000})});
  let selectionRetry:ReturnType<typeof setTimeout>|null=null;
  const cancelSelectionRetry=()=>{if(selectionRetry!==null)port.cancel(selectionRetry);selectionRetry=null;};
  const retrySelection=(current:NonNullable<typeof context>)=>{
    if(selectionRetry!==null||ended||current!==context||!current.visible||!foregroundTicket||!port.enabled()||!peer)return;
    selectionRetry=port.schedule(()=>{selectionRetry=null;if(!ended&&current===context&&current.visible&&port.enabled()&&peer)void publish();},20000);
  };
  const reply=(message:Record<string,unknown>,force=false)=>{if(!peer||ended)return;delivery.put(message.type==='wallpaper-ready'?'ready':'frame',{...message,foreground:!!foregroundTicket,stateOwner,stateRevision},force);};
  const announce=(force=false,contextSeq?:number)=>{if(!peer||ended)return;const enabled=ready&&port.enabled();if(!force&&enabled===announced)return;if(enabled!==announced)++stateRevision;announced=enabled;reply({type:'wallpaper-ready',enabled,...(contextSeq!==undefined?{contextSeq}:{})},force);};
  const output=(message:Record<string,unknown>)=>{const signature=JSON.stringify(message);if(signature===lastFrame)return;lastFrame=signature;reply(message);};
  let foregroundTicket:SteamMediaDemand|null=null,foregroundKnown=false,foregroundVersion=0;
  let nativeEpoch='',nativeRevision=-1;
  const retiredNativeEpochs=new Set<string>();
  let foregroundRead:{version:number;peer:MirrorPeer;selection:NonNullable<typeof context>}|null=null;
  let foregroundRetryTimer:ReturnType<typeof setTimeout>|null=null,foregroundRetries=0,foregroundRetryExhausted=false;
  const cancelForegroundRetry=()=>{if(foregroundRetryTimer!==null)port.cancel(foregroundRetryTimer);foregroundRetryTimer=null;foregroundRetries=0;foregroundRetryExhausted=false;};
  const clearForeground=()=>{cancelForegroundRetry();const hadTicket=foregroundTicket!==null;++foregroundVersion;foregroundRead=null;foregroundKnown=false;foregroundTicket=null;cancelFailureHandoff();cancelSelectionRetry();cancelPublication();delivered=null;inventoryVisible=false;if(hadTicket)port.media.selectSteamTarget?.(null);if(context)context.target=undefined;};
  const selectedDemand=(selection:NonNullable<typeof context>):SteamMediaDemand=>{const ticket=foregroundTicket!;return {...ticket,current:()=>foregroundTicket===ticket&&!ended&&ready&&!!peer&&port.enabled()&&context===selection&&selection.visible};};
  const acceptForeground=(raw:unknown)=>{
    if(ended||!ready||!peer||!port.enabled()||!context?.visible)return;
    const proof=steamWallpaperForegroundProof(raw);
    if(proof){
      if(retiredNativeEpochs.has(proof.ownerEpoch)||proof.revision<nativeRevision||nativeEpoch!==''&&proof.ownerEpoch!==nativeEpoch&&proof.revision===nativeRevision)return;
      if(nativeEpoch&&nativeEpoch!==proof.ownerEpoch){retiredNativeEpochs.add(nativeEpoch);if(retiredNativeEpochs.size>16)retiredNativeEpochs.delete(retiredNativeEpochs.values().next().value!);}
      nativeEpoch=proof.ownerEpoch;nativeRevision=proof.revision;
    }
    const allowed=steamWallpaperForegroundAllowed(proof);
    if(foregroundKnown&&allowed===(foregroundTicket!==null)&&(!allowed||foregroundTicket!.demandEpoch===proof!.ownerEpoch&&foregroundTicket!.demandRevision===proof!.revision))return;
    clearForeground();foregroundKnown=true;
    if(allowed)foregroundTicket={demandConsumer:'steam',demandEpoch:proof!.ownerEpoch,demandRevision:proof!.revision};
    ++stateRevision;lastFrame='';announce(true,context.seq);void publish();
  };
  const requestForeground=()=>{
    if(foregroundRead||foregroundRetryTimer!==null||foregroundRetryExhausted||ended||!ready||!peer||!port.enabled()||!context?.visible)return;
    const work={version:foregroundVersion,peer,selection:context};foregroundRead=work;
    const owns=()=>!ended&&ready&&foregroundVersion===work.version&&peer===work.peer&&context===work.selection&&work.selection.visible&&port.enabled();
    void port.readForeground().then(value=>{if(foregroundRead!==work||!owns())return;foregroundRead=null;acceptForeground(value);}).catch(()=>{
      if(foregroundRead!==work||!owns())return;foregroundRead=null;
      // Transport failure carries no native veto. Keep playback denied, but
      // repair this visible demand with a finite budget, never an idle poll.
      foregroundKnown=false;
      output({type:'wallpaper',enabled:true,contextSeq:work.selection.seq,appId:work.selection.appId,entry:null,status:'foreground-transport-unavailable'});
      const delay=[1000,5000,20000][foregroundRetries++];
      if(delay===undefined){foregroundRetryExhausted=true;return;}
      const timer=port.schedule(()=>{if(foregroundRetryTimer!==timer||!owns())return;foregroundRetryTimer=null;requestForeground();},delay);foregroundRetryTimer=timer;
    });
  };
  const retryFailure=(value:FailureHandoff)=>{
    if(!ownsFailure(value)||failureTimer!==null||value.attempt>=3)return;
    const delay=[1000,5000,20000][value.attempt++];
    const version=++failureVersion;
    failureTimer=port.schedule(()=>{if(version!==failureVersion||!ownsFailure(value))return;++failureVersion;failureTimer=null;void reportFailure(value,true);},delay);
  };
  async function reportFailure(value:FailureHandoff,recheck=false){
    if(!ownsFailure(value))return;
    try{
      if(recheck){
        const target=await value.selection.target;
        if(!ownsFailure(value))return;
        if(!target){cancelFailureHandoff();void publish();return;}
        const cached=await port.media.lookup(target,value.demand);
        if(!ownsFailure(value))return;
        // Lost ACK is not permission to repeat a write. Read the exact key,
        // asset and revision first. A committed edit has a new revision; the
        // native Store.edit CAS also rejects a late competing old write.
        if(!sameFailedAsset(cached,value)){cancelFailureHandoff();void publish();return;}
      }
      await port.media.playbackFailed(value.failed,value.demand);
      if(ownsFailure(value))cancelFailureHandoff();
    }catch{retryFailure(value);}
  }
  async function publish(){
    const at=++epoch,current=context;if(!ready||ended||!peer){publication=null;return;}
    if(!port.enabled()||!current||!current.visible){publication=null;cancelFailureHandoff();cancelSelectionRetry();inventoryVisible=false;port.media.selectSteamTarget?.(null);delivered=null;output({type:'wallpaper',enabled:port.enabled(),contextSeq:current?.seq??0,appId:current?.appId,entry:null});return;}
    if(!foregroundKnown){publication=null;requestForeground();return;}
    if(!foregroundTicket){publication=null;delivered=null;output({type:'wallpaper',enabled:true,contextSeq:current.seq,appId:current.appId,entry:null,status:'steam-not-foreground'});return;}
    const demand=selectedDemand(current);
    if(publication?.selection===current){publication.dirty=true;return;}
    const work={selection:current,dirty:false};publication=work;
    try{
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
      if(at!==epoch||ended||current!==context||!port.enabled()||!demand.current?.())return;
      port.media.selectSteamTarget?.(target);
      let cacheUnavailable=false;
      const cached=target?await port.media.lookup(target,demand).catch(()=>{cacheUnavailable=true;return null;}):null;
      if(at!==epoch||ended||current!==context||!port.enabled()||!demand.current?.())return;
      if(cached?.status==='consumer-inactive'){acceptForeground(null);return;}
      if(cacheUnavailable)retrySelection(current);else if(target)cancelSelectionRetry();
      current.key=cached?.key||(target?wallpaperTargetKey(target):undefined);
      const failed=failureHandoff;if(failed&&cached&&!sameFailedAsset(cached,failed))cancelFailureHandoff();
      const send=(entry:GameMediaEntry|null)=>{
        if(entry&&failureHandoff&&sameFailedAsset(entry,failureHandoff))entry={...entry,ready:false,status:'playback-failed'};
        if(entry?.key)current.key=entry.key;
        delivered=entry?.ready&&entry.asset?{key:entry.key,assetId:entry.asset.id,bindingRevision:entry.bindingRevision,kind:entry.asset.kind,seq:current.seq}:null;
        output({type:'wallpaper',enabled:true,contextSeq:current.seq,appId:current.appId,entry:entry?.ready?{assetId:entry.asset?.id,revision:entry.asset?.revision,url:entry.url,kind:entry.asset?.kind,playback:entry.playback,bindingRevision:entry.bindingRevision}:null,...port.appearance(),status:entry?.status??'missing'});
      };
      send(cached);
      if(cached&&failureHandoff&&sameFailedAsset(cached,failureHandoff))return;
      if(!cacheUnavailable&&target&&(target.appId>0||target.executable)&&!cached?.deleted&&!(cached?.ready&&!cached.refreshRequested&&gameMediaPairComplete(cached,Date.now()))){
        const deadline=Number(cached?.status==='pending'?cached.leaseUntil:cached?.retryAt)||0;
        if(deadline>port.now()){port.media.retryWhen?.(deadline);return;}
        const result=await port.media.ensure(target,'steam',true,true,demand).catch(()=>null);
        if(at===epoch&&!ended&&current===context&&port.enabled()&&demand.current?.())send(result);
      }
    }finally{
      // A revoked chain cannot clear or delay its replacement. Do not reuse
      // pending cache/preparation results as the cache after a dirty event.
      if(publication===work){
        publication=null;
        if(work.dirty&&!ended&&ready&&peer&&current===context&&current.visible&&port.enabled())void publish();
      }
    }
  }
  const off=port.register({prefix:'wallpaper.',connected(value){clearForeground();nativeEpoch='';nativeRevision=-1;retiredNativeEpochs.clear();cancelFailureHandoff();port.media.selectSteamTarget?.(null);peer=value.connected===false?null:value;delivery.reset(peer);cancelSelectionRetry();inventoryVisible=false;context=null;delivered=null;announced=undefined;lastFrame='';cancelPublication();if(peer)announce();},request(event){
    if(!peer||event.connection!==peer.connection||event.runId!==peer.runId)return;
    const request=event.request;
    // Old frontend neighbor messages are intentionally ignored: no speculative I/O.
    if(request.command==='wallpaper.status'){
      if(!foregroundTicket||!port.enabled()||request.status!=='playback-failed'||!delivered||request.contextSeq!==delivered.seq||request.appId!==context?.appId||request.assetId!==delivered.assetId||request.bindingRevision!==delivered.bindingRevision)return;
      const failed=delivered;delivered=null;cancelFailureHandoff();
      const value:FailureHandoff={selection:context!,failed,demand:selectedDemand(context!),attempt:0};failureHandoff=value;
      output({type:'wallpaper',enabled:true,contextSeq:value.selection.seq,appId:value.selection.appId,entry:null,...port.appearance(),status:'playback-failed'});
      void reportFailure(value);return;
    }
    if(request.command!=='wallpaper.context')return;
    const seq=Number(request.contextSeq),id=String(request.appId??'');
    if(!Number.isSafeInteger(seq)||seq<0||(context&&seq<=context.seq)||!/^\d{1,20}$/.test(id)||BigInt(id)<=0n||BigInt(id)>18446744073709551615n)return;
    if(request.visible!==true||context?.visible===false||!foregroundKnown&&(foregroundRead!==null||foregroundRetryTimer!==null||foregroundRetryExhausted))clearForeground();
    if(request.requestReady===true)announce(true,seq);
    cancelFailureHandoff();cancelSelectionRetry();port.media.selectSteamTarget?.(null);
    context={seq,appId:id,name:typeof request.name==='string'?request.name.slice(0,512):'',visible:request.visible===true};void publish();
  }});
  const changed=(reason='asset',key?:string)=>{
    if(ended||reason==='inventory'||reason==='demand'||reason==='policy')return;
    if(reason==='retry'){if(context?.visible&&foregroundTicket&&port.enabled())void publish();return;}
    if(reason==='asset'&&!context?.visible)return;
    if(reason==='asset'&&key&&context?.key&&key!==context.key)return;
    if(reason==='settings'){const stamp=JSON.stringify({ready,enabled:port.enabled(),...port.appearance()});if(stamp===settingsStamp)return;settingsStamp=stamp;}
    if(!port.enabled())clearForeground();
    announce();void publish();
  };
  // Use the committed native wake edge, not a timer or sidebar snapshot health.
  // Announce a decoder reset, but wait for a fresh frontend visibility proof.
  const offResume=port.onResume(event=>{
    const generation=Number(event?.generation);
    if(ended||!Number.isSafeInteger(generation)||generation<=resumeGeneration)return;
    resumeGeneration=generation;clearForeground();cancelFailureHandoff();cancelSelectionRetry();cancelPublication();delivered=null;lastFrame='';
    inventoryVisible=false;if(context){context.target=undefined;context.visible=false;}
    if(!ready||!peer||!port.enabled())return;
    reply({type:'wallpaper-ready',enabled:true,foreground:false,resumeGeneration:generation});
    void publish();
  });
  const offForeground=port.onForeground(acceptForeground);
  const unsubscribe=port.media.subscribe(changed);
  return {ready(){ready=true;changed('settings');},stop(){ended=true;clearForeground();offForeground();cancelFailureHandoff();cancelSelectionRetry();delivery.stop();cancelPublication();offResume();unsubscribe();off();peer=null;context=null;delivered=null;}};
}
