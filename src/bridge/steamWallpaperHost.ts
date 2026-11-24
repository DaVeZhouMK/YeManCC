import {wallpaperPowerBlocked} from './wallpaperPowerPolicy';
import {invoke,on} from './ipc';
import {subscribeGameMediaResume} from './gameMediaPreparationPolicy';
import {onSharedPowerLifecycle,isSharedPowerLifecycleAdmitted,type SharedPowerLifecycleSnapshot} from './powerLifecycleState';
import {SteamWallpaperDelivery} from './steamWallpaperDelivery';
import {registerDeckyMirrorExtension} from './deckyMirrorExtensions';
import type {MirrorPeer} from './deckyMirrorRelay';
import {sharedGameMedia,steamWallpaperEnabled,gameMediaPairComplete,hasWallpaperBinding,type GameMediaEntry,type SharedGameMedia,type SteamMediaDemand,wallpaperTargetKey} from './sharedGameMedia';
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
  media:Pick<SharedGameMedia,'lookup'|'ensure'|'subscribe'|'playbackFailed'>&Partial<Pick<SharedGameMedia,'selectSteamTarget'|'prewarm'|'retryWhen'|'automaticPreparationEnabled'>>;
  enabled:()=>boolean;
  register:typeof registerDeckyMirrorExtension;
  appearance:()=>{opacity:number;blur:number};
  inventory:()=>void;
  now:()=>number;
  readForeground:()=>Promise<unknown>;
  onForeground:(handler:(value:unknown)=>void)=>()=>void;
  schedule:typeof setTimeout;cancel:typeof clearTimeout;
  onResume:(handler:(event:{generation?:number})=>void)=>()=>void;
  onPower:(handler:(state:SharedPowerLifecycleSnapshot)=>void)=>()=>void;
}
function selectedWallpaperRead(target:WallpaperTarget){
  if(target.catalogSource!=='steam-shortcut'||target.appId!==0||!target.executable)return target;
  // An explicit AppID (even zero) disables the native read-only EXE fallback.
  const {appId: _appId,...local}=target;
  return {...local,currentGameWallpaper:true as const};
}
export function startSteamWallpaperHost(overrides:Partial<WallpaperHostPorts>={}){
  const port:WallpaperHostPorts={now:Date.now,schedule:(...args)=>globalThis.setTimeout(...args),cancel:timer=>globalThis.clearTimeout(timer),invoke,media:sharedGameMedia,enabled:()=>steamWallpaperEnabled()&&!wallpaperPowerBlocked(),register:registerDeckyMirrorExtension,appearance:()=>({opacity:getBackgroundOpacity(),blur:getBackgroundBlur()}),inventory:()=>sharedGameMedia.changed('inventory'),readForeground:()=>invoke('gameMedia.steamForeground',{}, {timeoutMs:5000}),onForeground:handler=>typeof window==='undefined'?()=>{}:on('gameMedia.steamForegroundChanged',handler),onResume:handler=>typeof window==='undefined'?()=>{}:subscribeGameMediaResume(handler,on),onPower:handler=>onSharedPowerLifecycle(handler,{observeSuspendSerial:true}),...overrides};
  // Existing Ready-only integrations retain their callback. Production
  // consumes the public current facts, including same-generation revocation.
  const legacyResume=!!overrides.onResume&&!overrides.onPower;
  let powerAdmitted=legacyResume,powerSeen=false,powerGeneration=0,powerSuspendSerial=0;
  const playbackEnabled=()=>powerAdmitted&&port.enabled();
  // Scopes local ordering only; peer/runId continue to own transport identity.
  const stateOwner=Date.now().toString(36)+'-'+Math.random().toString(36).slice(2);
  let peer:MirrorPeer|null=null,ended=false,ready=false,epoch=0,announced:boolean|undefined,resumeGeneration=0,inventoryVisible=false,stateRevision=0;
  let lastFrame:{signature:string;message:Record<string,unknown>}|null=null;
  let settingsView:{ready:boolean;enabled:boolean;opacity:number;blur:number}|null=null;
  let context:{seq:number;appId:string;name:string;visible:boolean;key?:string;target?:Promise<WallpaperTarget|null>;fallback?:{assetId:string;bindingRevision:number}}|null=null;
  // One selected publication owns lookup and ensure until they settle. Dirty
  // notifications invalidate its result and retain one fresh trailing read.
  let publication:{selection:NonNullable<typeof context>;dirty:boolean}|null=null;
  const cancelPublication=()=>{++epoch;publication=null;};
  let delivered:{key:string;assetId:string;bindingRevision:number;kind?:string;seq:number;reducedPlayback?:true}|null=null;
  type FailureHandoff={selection:NonNullable<typeof context>;failed:NonNullable<typeof delivered>;demand:SteamMediaDemand;attempt:number};
  let failureHandoff:FailureHandoff|null=null,failureTimer:ReturnType<typeof setTimeout>|null=null,failureVersion=0;
  const cancelFailureHandoff=()=>{++failureVersion;if(failureTimer!==null)port.cancel(failureTimer);failureTimer=null;failureHandoff=null;};
  const ownsFailure=(value:FailureHandoff)=>failureHandoff===value&&!ended&&ready&&!!peer&&playbackEnabled()&&context===value.selection&&value.selection.visible&&value.demand.current?.()===true;
  const sameFailedAsset=(entry:GameMediaEntry,value:FailureHandoff)=>entry.ready===true&&entry.key===value.failed.key&&entry.bindingRevision===value.failed.bindingRevision&&entry.asset?.id===value.failed.assetId&&entry.asset?.kind===value.failed.kind;
  const delivery=new SteamWallpaperDelivery({schedule:port.schedule,cancel:port.cancel,send:(target,message)=>port.invoke<boolean>('deckySidebar.mirrorReply',{connection:target.connection,message:{...message,runId:target.runId}},{timeoutMs:5000})});
  let selectionRetry:ReturnType<typeof setTimeout>|null=null;
  const cancelSelectionRetry=()=>{if(selectionRetry!==null)port.cancel(selectionRetry);selectionRetry=null;};
  const retrySelection=(current:NonNullable<typeof context>)=>{
    if(selectionRetry!==null||ended||current!==context||!current.visible||!foregroundTicket||!playbackEnabled()||!peer)return;
    selectionRetry=port.schedule(()=>{selectionRetry=null;if(!ended&&current===context&&current.visible&&playbackEnabled()&&peer)void publish();},20000);
  };
  const wire=(message:Record<string,unknown>)=>({...message,foreground:!!foregroundTicket,stateOwner,stateRevision});
  const reply=(message:Record<string,unknown>,force=false)=>{if(!peer||ended)return;delivery.put(message.type==='wallpaper-ready'?'ready':'frame',wire(message),force);};
  const announce=(force=false,contextSeq?:number,requestContext=false)=>{if(!peer||ended)return;const enabled=ready&&playbackEnabled();if(!force&&enabled===announced)return;if(enabled!==announced)++stateRevision;announced=enabled;reply({type:'wallpaper-ready',enabled,...(contextSeq!==undefined?{contextSeq}:{}),...(requestContext?{requestContext:true}:{})},force);};
  const output=(message:Record<string,unknown>)=>{
    if(!peer||ended)return;
    const packet=wire(message),signature=JSON.stringify(packet);
    if(signature===lastFrame?.signature)return;
    lastFrame={signature,message};
    delivery.put('frame',packet,false,signature);
  };
  let foregroundTicket:SteamMediaDemand|null=null,foregroundKnown=false,foregroundVersion=0;
  let nativeEpoch='',nativeRevision=-1;
  let refusedRevision=-1,refusalRecheckUsed=false;
  const retiredNativeEpochs=new Set<string>();
  let foregroundRead:{version:number;peer:MirrorPeer}|null=null;
  let foregroundRetryTimer:ReturnType<typeof setTimeout>|null=null,foregroundRetries=0,foregroundRetryExhausted=false;
  const transportUnavailable='foreground-transport-unavailable',changedDuringRead='foreground-changed-during-read';
  let foregroundRetryStatus:typeof transportUnavailable|typeof changedDuringRead|null=null;
  const cancelForegroundRetry=()=>{if(foregroundRetryTimer!==null)port.cancel(foregroundRetryTimer);foregroundRetryTimer=null;foregroundRetries=0;foregroundRetryExhausted=false;foregroundRetryStatus=null;};
  const clearForeground=()=>{cancelForegroundRetry();const hadTicket=foregroundTicket!==null;++foregroundVersion;foregroundRead=null;foregroundKnown=false;foregroundTicket=null;cancelFailureHandoff();cancelSelectionRetry();cancelPublication();delivered=null;inventoryVisible=false;if(hadTicket)port.media.selectSteamTarget?.(null);if(context)context.target=undefined;};
  const selectedDemand=(selection:NonNullable<typeof context>):SteamMediaDemand=>{const ticket=foregroundTicket!;return {...ticket,current:()=>foregroundTicket===ticket&&!ended&&ready&&!!peer&&playbackEnabled()&&context===selection&&selection.visible};};
  const foregroundReadRaced=(proof:SteamWallpaperForegroundProof|null)=>
    !!proof&&proof.reason===changedDuringRead&&!proof.known&&!proof.foreground&&!proof.current&&proof.mode==='unknown'&&proof.pid===0&&proof.birth==='0'&&proof.window==='0';
  const rememberForegroundRevision=(proof:SteamWallpaperForegroundProof)=>{
    if(retiredNativeEpochs.has(proof.ownerEpoch)||proof.revision<nativeRevision||nativeEpoch!==''&&proof.ownerEpoch!==nativeEpoch&&proof.revision===nativeRevision)return false;
    if(nativeEpoch&&nativeEpoch!==proof.ownerEpoch){retiredNativeEpochs.add(nativeEpoch);if(retiredNativeEpochs.size>16)retiredNativeEpochs.delete(retiredNativeEpochs.values().next().value!);}
    nativeEpoch=proof.ownerEpoch;nativeRevision=proof.revision;return true;
  };
  const acceptForeground=(proof:SteamWallpaperForegroundProof|null)=>{
    if(ended||!ready||!peer||!playbackEnabled()||!context?.visible||proof&&!rememberForegroundRevision(proof))return false;
    const allowed=!!proof&&proof.revision>refusedRevision&&steamWallpaperForegroundAllowed(proof);
    if(foregroundKnown&&allowed===(foregroundTicket!==null)&&(!allowed||foregroundTicket!.demandEpoch===proof!.ownerEpoch&&foregroundTicket!.demandRevision===proof!.revision))return true;
    clearForeground();foregroundKnown=true;
    if(allowed)foregroundTicket={demandConsumer:'steam',demandEpoch:proof!.ownerEpoch,demandRevision:proof!.revision};
    ++stateRevision;lastFrame=null;announce(true,context.seq);void publish();return true;
  };
  const reportForegroundUnavailable=(selection:NonNullable<typeof context>)=>{
    if(foregroundRetryStatus)output({type:'wallpaper',enabled:true,contextSeq:selection.seq,appId:selection.appId,entry:null,status:foregroundRetryStatus});
  };
  const requestForeground=(afterRefusal=false)=>{
    if(foregroundRead||foregroundRetryTimer!==null||foregroundRetryExhausted||ended||!ready||!peer||!playbackEnabled()||!context?.visible)return;
    // The proof and its retry budget belong to this visible Steam window,
    // not an AppID. Card changes only replace the eventual media selection.
    const work={version:foregroundVersion,peer};foregroundRead=work;
    const currentSelection=()=>!ended&&ready&&foregroundVersion===work.version&&peer===work.peer&&context?.visible&&playbackEnabled()?context:null;
    const unavailable=(status:typeof foregroundRetryStatus)=>{
      const selection=currentSelection();if(foregroundRead!==work||!selection)return;foregroundRead=null;
      if(afterRefusal)return; // The native veto already acknowledged this context.
      // Neither transport loss nor a superseded read certifies a native veto.
      // Keep playback denied and use this visible demand's finite retry budget.
      foregroundKnown=false;foregroundRetryStatus=status;
      reportForegroundUnavailable(selection);
      const delay=[1000,5000,20000][foregroundRetries++];
      if(delay===undefined){foregroundRetryExhausted=true;return;}
      const timer=port.schedule(()=>{if(foregroundRetryTimer!==timer||!currentSelection())return;foregroundRetryTimer=null;requestForeground();},delay);foregroundRetryTimer=timer;
    };
    void port.readForeground().then(value=>{
      if(foregroundRead!==work||!currentSelection())return;
      const proof=steamWallpaperForegroundProof(value);
      if(foregroundReadRaced(proof)||!acceptForeground(proof)){unavailable(changedDuringRead);return;}
      if(foregroundRead===work)foregroundRead=null;
    }).catch(()=>unavailable(transportUnavailable));
  };
  const onForegroundNotice=(raw:unknown)=>{
    const proof=steamWallpaperForegroundProof(raw);
    if(!proof||!foregroundReadRaced(proof)){acceptForeground(proof);return;}
    if(ended||!ready||!peer||!playbackEnabled()||!context?.visible||proof.revision<=refusedRevision||
      foregroundKnown&&proof.revision<=nativeRevision||!rememberForegroundRevision(proof))return;
    // This notice says the observation lost a revision race, not that Steam blurred.
    // Keep an existing read or retry, but don't play a frame from a superseded proof.
    if(foregroundKnown){clearForeground();++stateRevision;lastFrame=null;}
    foregroundRetryStatus=changedDuringRead;reportForegroundUnavailable(context);requestForeground();
  };
  const refuseForeground=(entry:GameMediaEntry&{reason?:unknown},demand:SteamMediaDemand)=>{
    // Native resolve checks the ticket independently of the last pushed proof.
    // If it rejects that proof, only a newer fact can read media again. Spend
    // one direct read per visible interval to cover a missed native notice.
    const nativeRefusal=entry.reason==='media-steam-foreground-inactive';
    if(nativeRefusal)refusedRevision=Math.max(refusedRevision,demand.demandRevision);
    acceptForeground(null);
    if(nativeRefusal&&!refusalRecheckUsed){refusalRecheckUsed=true;requestForeground(true);}
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
        const cached=await port.media.lookup(selectedWallpaperRead(target),value.demand);
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
    if(!playbackEnabled()||!current||!current.visible){publication=null;cancelFailureHandoff();cancelSelectionRetry();inventoryVisible=false;port.media.selectSteamTarget?.(null);delivered=null;output({type:'wallpaper',enabled:playbackEnabled(),contextSeq:current?.seq??0,appId:current?.appId,entry:null});return;}
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
        const pending:Promise<WallpaperTarget|null>=resolveWallpaperTarget(current.appId,current.name,port.invoke).then(target=>{
          // A missing shortcut is an observation, not a permanent cache entry.
          // The next existing media notification may carry a repaired mapping.
          // Keep successful targets cached; do not add polling or title guesses.
          if(target===null&&current.target===pending)current.target=undefined;
          return target;
        }).catch(()=>{
          if(current.target===pending){current.target=undefined;retrySelection(current);}return null;
        });
        current.target=pending;
      }
      const target=await current.target;
      if(at!==epoch||ended||current!==context||!playbackEnabled()||!demand.current?.())return;
      port.media.selectSteamTarget?.(target);
      let cacheUnavailable=false;
      const readTarget=target?selectedWallpaperRead(target):null;
      const cached=readTarget?await port.media.lookup(current.fallback?{...readTarget,wallpaperPlaybackFallback:current.fallback}:readTarget,demand).catch(()=>{cacheUnavailable=true;return null;}):null;
      if(at!==epoch||ended||current!==context||!playbackEnabled()||!demand.current?.())return;
      if(cached?.status==='consumer-inactive'){refuseForeground(cached,demand);return;}
      if(cacheUnavailable)retrySelection(current);else if(target)cancelSelectionRetry();
      if(current.fallback&&cached&&cached.bindingRevision!==current.fallback.bindingRevision)current.fallback=undefined;
      current.key=cached?.key||(target?wallpaperTargetKey(target):undefined);
      const failed=failureHandoff;if(failed&&cached&&!sameFailedAsset(cached,failed))cancelFailureHandoff();
      const send=(entry:GameMediaEntry|null)=>{
        if(entry&&failureHandoff&&sameFailedAsset(entry,failureHandoff))entry={...entry,ready:false,status:'playback-failed'};
        if(entry?.key)current.key=entry.key;
        // A superseded binding can still say ready after the HTTP server has refused its URL.
        const frame=entry?.ready===true&&!entry.deleted&&entry.asset&&typeof entry.url==='string'&&entry.url.trim()
          ? {assetId:entry.asset.id,revision:entry.asset.revision,url:entry.url,kind:entry.asset.kind,playback:entry.playback,bindingRevision:entry.bindingRevision,...(entry.asset.reducedPlayback===true?{reducedPlayback:true,recoverOriginal:true} as const:{})}
          : null;
        delivered=frame&&entry?{key:entry.key,assetId:frame.assetId,bindingRevision:frame.bindingRevision,kind:frame.kind,seq:current.seq,reducedPlayback:frame.reducedPlayback}:null;
        output({type:'wallpaper',enabled:true,contextSeq:current.seq,appId:current.appId,entry:frame,...port.appearance(),status:!frame&&entry?.ready?'media-url-unavailable':entry?.status??'missing'});
      };
      send(cached);
      if(current.fallback)return;
      if(cached&&failureHandoff&&sameFailedAsset(cached,failureHandoff))return;
      if(!cacheUnavailable&&target&&(target.appId>0||target.executable)&&!cached?.deleted&&!cached?.currentGameWallpaperFallback&&!(cached&&hasWallpaperBinding(cached)&&!cached.refreshRequested&&gameMediaPairComplete(cached,Date.now()))){
        const deadline=Number(cached?.status==='pending'?cached.leaseUntil:cached?.retryAt)||0;
        if(deadline>port.now()){port.media.retryWhen?.(deadline);return;}
        // With preparation disabled, ensure would only repeat the local read.
        // An unresolved shortcut has a different display projection; keep its exact read.
        if(readTarget===target&&port.media.automaticPreparationEnabled?.()===false)return;
        const result=await port.media.ensure(target,'steam',true,true,demand).catch(()=>null);
        // A failed complement read says nothing about the cache already sent.
        // Only a resolved result can replace or explicitly revoke that frame.
        if(result&&at===epoch&&!ended&&current===context&&playbackEnabled()&&demand.current?.()){
          if(result.status==='consumer-inactive'){refuseForeground(result,demand);return;}
          send(result);
        }
      }
    }finally{
      // A revoked chain cannot clear or delay its replacement. Do not reuse
      // pending cache/preparation results as the cache after a dirty event.
      if(publication===work){
        publication=null;
        if(work.dirty&&!ended&&ready&&peer&&current===context&&current.visible&&playbackEnabled())void publish();
      }
    }
  }
  const off=port.register({prefix:'wallpaper.',connected(value){
    // Native attach can confirm the same live transport after App readiness.
    if(value.connected!==false&&peer&&value.connection===peer.connection&&value.runId===peer.runId&&value.connectionRevision===peer.connectionRevision)return;
    clearForeground();nativeEpoch='';nativeRevision=-1;refusedRevision=-1;refusalRecheckUsed=false;retiredNativeEpochs.clear();cancelFailureHandoff();port.media.selectSteamTarget?.(null);peer=value.connected===false?null:value;delivery.reset(peer);cancelSelectionRetry();inventoryVisible=false;context=null;delivered=null;announced=undefined;lastFrame=null;cancelPublication();if(peer)announce();
  },request(event){
    if(!peer||event.connection!==peer.connection||event.runId!==peer.runId)return;
    const request=event.request;
    // Old frontend neighbor messages are intentionally ignored: no speculative I/O.
    if(request.command==='wallpaper.status'){
      // Legacy renderers can report image load errors. Only our delivered video can invalidate shared media.
      const temporary=request.status==='playback-interrupted';
      if(!foregroundTicket||!playbackEnabled()||!context?.visible||(!temporary&&request.status!=='playback-failed')||!delivered||delivered.kind!=='video'||request.contextSeq!==delivered.seq||request.appId!==context.appId||request.assetId!==delivered.assetId||request.bindingRevision!==delivered.bindingRevision)return;
      if(temporary){
        if(!delivered.reducedPlayback||context.fallback)return;
        context.fallback={assetId:delivered.assetId,bindingRevision:delivered.bindingRevision};
        delivered=null;cancelPublication();void publish();return;
      }
      const failed=delivered;delivered=null;cancelFailureHandoff();
      const value:FailureHandoff={selection:context!,failed,demand:selectedDemand(context!),attempt:0};failureHandoff=value;
      output({type:'wallpaper',enabled:true,contextSeq:value.selection.seq,appId:value.selection.appId,entry:null,...port.appearance(),status:'playback-failed'});
      void reportFailure(value);return;
    }
    if(request.command!=='wallpaper.context')return;
    const seq=Number(request.contextSeq),id=String(request.appId??'');
    if(!Number.isSafeInteger(seq)||seq<0||(context&&seq<=context.seq)||!/^\d{1,20}$/.test(id)||BigInt(id)<=0n||BigInt(id)>18446744073709551615n)return;
    if(request.visible!==true||context?.visible===false){refusalRecheckUsed=false;clearForeground();}
    if(request.requestReady===true)announce(true,seq);
    cancelFailureHandoff();cancelSelectionRetry();port.media.selectSteamTarget?.(null);
    context={seq,appId:id,name:typeof request.name==='string'?request.name.slice(0,512):'',visible:request.visible===true&&powerAdmitted};
    // Settings startup, foreground proof and media reads may all outlive the
    // transport check. This receipt never admits playback or a media read.
    if(request.requestReady!==true&&request.visible===true&&
       (!ready||playbackEnabled()&&context.visible&&(foregroundTicket||!foregroundKnown&&!foregroundRetryStatus)))
      reply({type:'wallpaper-context-accepted',contextSeq:seq,appId:id});
    // Renew the response for the new selection, not the failed proof's retry
    // budget. Silence here would time out the otherwise live shared socket.
    if(ready&&playbackEnabled()&&context.visible&&!foregroundKnown)reportForegroundUnavailable(context);
    void publish();
  }});
  const changed=(reason='asset',key?:string)=>{
    if(ended||reason==='inventory'||reason==='demand'||reason==='policy')return;
    if(reason==='retry'){if(context?.visible&&foregroundTicket&&playbackEnabled())void publish();return;}
    if(reason==='asset'||reason==='wallpaper-playback'){
      if(!context?.visible||key&&context.key&&key!==context.key)return;
      if(reason==='wallpaper-playback'&&delivered?.seq===context.seq&&delivered.kind==='video')return;
    }
    if(reason==='settings'){
      const appearance=port.appearance(),enabled=playbackEnabled(),previous=settingsView;
      settingsView={ready,enabled,...appearance};
      if(previous?.ready===ready&&previous.enabled===enabled){
        if(previous.opacity===appearance.opacity&&previous.blur===appearance.blur)return;
        // Repaint only a current frame. Pending media reads will pick up the
        // latest appearance themselves; style changes never invalidate them.
        if(ready&&enabled&&foregroundTicket&&context?.visible&&delivered?.seq===context.seq&&lastFrame?.message.entry)
          output({...lastFrame.message,...appearance});
        return;
      }
    }
    if(!playbackEnabled()){refusalRecheckUsed=false;clearForeground();}
    announce();void publish();
  };
  // Use the committed native wake edge, not a timer or sidebar snapshot health.
  // Announce a decoder reset, but wait for a fresh frontend visibility proof.
  const retirePower=()=>{
    refusalRecheckUsed=false;clearForeground();delivery.reset(peer);++stateRevision;lastFrame=null;
    if(context){context.target=undefined;context.visible=false;}
  };
  const offPower=legacyResume?()=>{}:port.onPower(state=>{
    if(ended)return;
    const generation=Number(state.generation);
    const suspendSerial=state.suspendSerial??powerSuspendSerial;
    const admitted=Number.isSafeInteger(generation)&&generation>0&&isSharedPowerLifecycleAdmitted(state);
    if(Number.isSafeInteger(generation)&&generation>0&&generation<powerGeneration)return;
    if(suspendSerial<powerSuspendSerial)return;
    if(powerSeen&&powerAdmitted===admitted&&powerGeneration===generation&&powerSuspendSerial===suspendSerial)return;
    powerSeen=true;powerAdmitted=admitted;powerSuspendSerial=suspendSerial;
    if(Number.isSafeInteger(generation)&&generation>0)powerGeneration=generation;
    retirePower();
    if(!ready||!peer||!port.enabled())return;
    // Effective OFF revokes the decoder without changing the saved setting.
    // Admitted recovery restores Ready and asks for a new visible context;
    // omit resumeGeneration so same-generation recovery is not dropped.
    announce(true,context?.seq,admitted);void publish();
  });
  const offResume=!legacyResume?()=>{}:port.onResume(event=>{
    const generation=Number(event?.generation);
    if(ended||!Number.isSafeInteger(generation)||generation<=resumeGeneration)return;
    resumeGeneration=generation;retirePower();
    if(!ready||!peer||!playbackEnabled())return;
    reply({type:'wallpaper-ready',enabled:true,foreground:false,resumeGeneration:generation});
    void publish();
  });
  const offForeground=port.onForeground(onForegroundNotice);
  const unsubscribe=port.media.subscribe(changed);
  return {ready(){ready=true;changed('settings');},stop(){ended=true;clearForeground();offForeground();cancelFailureHandoff();cancelSelectionRetry();delivery.stop();cancelPublication();offPower();offResume();unsubscribe();off();peer=null;context=null;delivered=null;}};
}
