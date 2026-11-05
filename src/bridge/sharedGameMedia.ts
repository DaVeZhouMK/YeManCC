import {wallpaperPowerBlocked,wallpaperPowerReason,subscribeWallpaperPowerChanged,captureWallpaperPower,wallpaperPowerCurrent} from './wallpaperPowerPolicy';
import {gameMediaDownloads,backgroundDownloadEnabled} from './gameMediaDownloads';
import {invoke,on} from './ipc';
import {subscribePolicyGameStatus,refreshPolicyGameStrict} from './gamePolicyTarget';
import {GameMediaPreparationPolicy,subscribeGameMediaResume} from './gameMediaPreparationPolicy';
import {getUiSetting,setUiSettings,uiSettingsLoaded} from './uiSettings';
import {requestSteamHttp} from './steamHttp';
import {parseHlsAsset} from './steamMediaPlaylist';
import {installedSteamWallpaperTargets,steamWallpaperCandidates,normalizeSteamMediaUrl,resolveSteamWallpaperIdentity,type WallpaperTarget} from './steamMediaSource';
export interface GameMediaAsset {id:string;revision:string;kind:'video'|'image';mime:string;extension:string;bytes:number;qualityPolicy?:number;targetHeight?:number;selectedHeight?:number;}
export interface GameMediaEntry {key:string;appId?:number;name?:string;executable?:string;bindingRevision:number;status:string;ready?:boolean;asset?:GameMediaAsset;url?:string;localUrl?:string;manual?:boolean;deleted?:boolean;refreshRequested?:boolean;retryAt?:number;leaseUntil?:number;taskId?:string;acquired?:boolean;error?:string;identityVerified?:boolean;wallpaperMode?:'static'|'dynamic';assets?:{static?:GameMediaAsset;dynamic?:GameMediaAsset};staticReady?:boolean;dynamicReady?:boolean;taskSlots?:string[];demandConsumer?:string;videoUnavailableUntil?:number;staticUnavailableUntil?:number;manualSlots?:Record<string,boolean>;deletedSlots?:Record<string,boolean>;playback?:{start:number;end:number;fit:'cover'|'contain'};}
export function gameMediaPairComplete(entry:GameMediaEntry,now=Date.now()){return ['static','dynamic'].every(slot=>entry[slot==='static'?'staticReady':'dynamicReady']||entry.deletedSlots?.[slot]||entry.manualSlots?.[slot]||Number(slot==='static'?entry.staticUnavailableUntil:entry.videoUnavailableUntil)>now);}
export function wallpaperSourcePriority(target:Partial<WallpaperTarget>){const tier=target.sourceTier??3;return tier<2?tier:target.catalogSource==='steam-shortcut'?2:tier===2?3:4;}
export function wallpaperTargetKey(target:Partial<WallpaperTarget>){return target.appId&&target.appId>0?'steam-'+target.appId:'exe-'+String(target.executable||'').replace(/\//g,'\\').toLowerCase();}
export interface SteamMediaDemand {demandConsumer:'steam';demandEpoch:string;demandRevision:number;current?:()=>boolean;}
export function steamMediaDemandCurrent(value:SteamMediaDemand|undefined){return !!value&&value.demandConsumer==='steam'&&typeof value.demandEpoch==='string'&&!!value.demandEpoch&&new TextEncoder().encode(value.demandEpoch).length<=128&&Number.isSafeInteger(value.demandRevision)&&value.demandRevision>=0&&(value.current?.()??true);}
function steamMediaDemandFields(value:SteamMediaDemand|undefined){return value?{demandConsumer:'steam' as const,demandEpoch:value.demandEpoch,demandRevision:value.demandRevision}:{};}
interface Job {demand?:SteamMediaDemand;target:WallpaperTarget;consumer:string;priority:boolean;resolve:(entry:GameMediaEntry)=>void;reject:(error:unknown)=>void;promise:Promise<GameMediaEntry>;}
export interface MediaPorts {workAllowed:()=>boolean;nativeBackground:boolean;invoke:typeof invoke;enabled:()=>boolean;now:()=>number;candidates:typeof steamWallpaperCandidates;request:typeof requestSteamHttp;installed:typeof installedSteamWallpaperTargets;backgroundEnabled:()=>boolean;schedule:typeof setTimeout;cancel:typeof clearTimeout;}
export class SharedGameMedia {
  private port:MediaPorts;
  constructor(overrides:Partial<MediaPorts>={}){this.port={workAllowed:()=>!wallpaperPowerBlocked(),invoke,nativeBackground:true,enabled:()=>getUiSetting('steamDynamicWallpaperEnabled'),now:Date.now,candidates:steamWallpaperCandidates,request:requestSteamHttp,installed:installedSteamWallpaperTargets,backgroundEnabled:backgroundDownloadEnabled,schedule:(...args:Parameters<typeof setTimeout>)=>globalThis.setTimeout(...args),cancel:(timer:ReturnType<typeof setTimeout>)=>globalThis.clearTimeout(timer),...overrides};}
  private workEpoch=0;private powerBlocked=false;
  private workCurrent(epoch:number){return !this.stopped&&this.port.workAllowed()&&epoch===this.workEpoch;}
  private jobs=new Map<string,Job>();private queue:string[]=[];private running=false;private stopped=false;
  private nativeAdmissions=new Map<string,Promise<void>>();
  private lookupFlights=new Map<string,Promise<GameMediaEntry>>();
  private preparationState={backgroundBlocked:false,storageBlocked:false,reason:''};
  setPreparationState(state:Partial<typeof this.preparationState>){const next={...this.preparationState,...state};if(next.backgroundBlocked===this.preparationState.backgroundBlocked&&next.storageBlocked===this.preparationState.storageBlocked&&next.reason===this.preparationState.reason)return;this.preparationState=next;this.changed('policy');void this.drain();}
  private listeners=new Set<(reason:string,key?:string)=>void>();private nextRetry:ReturnType<typeof setTimeout>|null=null;private retryAt=0;
  subscribe(listener:(reason:string,key?:string)=>void){this.listeners.add(listener);return()=>{this.listeners.delete(listener);};}
  invalidateLookups(){this.lookupFlights.clear();}
  changed(reason='asset',key?:string){this.invalidateLookups();for(const listener of this.listeners)try{listener(reason,key);}catch{}}
  async playbackFailed(entry:{key:string;assetId:string;bindingRevision:number;kind?:string},demand?:SteamMediaDemand){
    if(!this.port.workAllowed())throw Error('wallpaper-power-paused');
    if(demand&&!steamMediaDemandCurrent(demand))throw Error('steam-demand-withdrawn');
    if(this.stopped)throw Error('media-owner-stopped');
    const failed=await this.port.invoke<GameMediaEntry>('gameMedia.edit',{key:entry.key,assetId:entry.assetId,bindingRevision:entry.bindingRevision,slot:entry.kind==='image'?'static':'dynamic',action:'playback-failed',...steamMediaDemandFields(demand)},{timeoutMs:5000});
    if(!this.stopped&&(!demand||steamMediaDemandCurrent(demand))){if(failed.retryAt)this.armRetry(failed.retryAt);this.changed('asset',failed.key);}return failed;
  }
  // Retained as an inert compatibility entry for old callers. No IPC or I/O.
  async prewarm(_targets:WallpaperTarget[]):Promise<GameMediaEntry[]>{return [];}
  lookup(target:Partial<WallpaperTarget>&{key?:string},demand?:SteamMediaDemand):Promise<GameMediaEntry>{
    if(this.stopped)return Promise.reject(Error('media-owner-stopped'));
    if(!this.port.workAllowed())return Promise.reject(Error('wallpaper-power-paused'));
    const workEpoch=this.workEpoch;
    if(demand&&!steamMediaDemandCurrent(demand))return Promise.reject(Error('steam-demand-withdrawn'));
    const args={...target,...steamMediaDemandFields(demand)};
    const signature=JSON.stringify(Object.fromEntries(Object.keys(args).sort().map(key=>[key,args[key as keyof typeof args]])));
    let pending=this.lookupFlights.get(signature);
    if(!pending){
      let resolve!:(entry:GameMediaEntry)=>void,reject!:(error:unknown)=>void;
      pending=new Promise<GameMediaEntry>((yes,no)=>{resolve=yes;reject=no;});
      // Share only overlapping reads. No settled entry or persistent freshness cache.
      if(this.lookupFlights.size<64)this.lookupFlights.set(signature,pending);
      const owned=pending;
      const release=()=>{if(this.lookupFlights.get(signature)===owned)this.lookupFlights.delete(signature);};
      void pending.then(release,release);
      try{void Promise.resolve(this.port.invoke<GameMediaEntry>('gameMedia.resolve',args,{timeoutMs:12000})).then(resolve,reject);}catch(error){reject(error);}
    }
    return pending.then(entry=>{
      if(this.stopped)throw Error('media-owner-stopped');
      if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');
      if(demand&&!steamMediaDemandCurrent(demand))throw Error('steam-demand-withdrawn');
      // IPC returns JSON values. Each consumer retains its own mutable result.
      return JSON.parse(JSON.stringify(entry)) as GameMediaEntry;
    });
  }
  async ensure(target:WallpaperTarget,consumer:'ymcc'|'steam'|'library',priority=false,completePair=false,demand?:SteamMediaDemand):Promise<GameMediaEntry>{
    if(this.stopped||!this.port.workAllowed())return {key:wallpaperTargetKey(target),bindingRevision:0,status:'consumer-inactive',ready:false,acquired:false};
    if(consumer==='steam'&&!this.port.enabled())return {key:wallpaperTargetKey(target),bindingRevision:0,status:'consumer-inactive',ready:false,acquired:false};
    if(consumer==='steam'&&!steamMediaDemandCurrent(demand))throw Error('steam-foreground-required');
    if(consumer!=='steam')demand=undefined;
    if(target.executable&&target.appId>0&&target.verified!==true)target={...target,appId:0};
    const workEpoch=this.workEpoch;
    const cached=await this.lookup(target,demand);
    if(!this.workCurrent(workEpoch))return {...cached,status:'consumer-inactive',ready:false,acquired:false};
    if(demand&&!steamMediaDemandCurrent(demand))throw Error('steam-demand-withdrawn');
    const paired=gameMediaPairComplete(cached,this.port.now());
    if(cached.deleted||cached.manual&&!completePair&&!cached.refreshRequested||cached.ready&&!cached.refreshRequested&&(!completePair||paired))return cached;
    // All automatic network work uses the stoppable, low-priority library
    // process. Merely showing a current game cannot bypass battery/CPU/boot
    // gates through the renderer's former foreground preparation lane.
    if(this.port.nativeBackground){
      if(this.port.backgroundEnabled()&&!this.stopped)await this.admitNative(target,cached,demand);
      if(!this.workCurrent(workEpoch))return {...cached,status:'consumer-inactive',ready:false,acquired:false};
      return cached;
    }
    if(cached.retryAt&&cached.retryAt>this.port.now()){this.armRetry(cached.retryAt);return cached;}
    const key=wallpaperTargetKey(target);const existing=this.jobs.get(key);if(existing){if(consumer!=='steam'){existing.consumer=consumer;existing.demand=undefined;}else if(existing.consumer==='steam')existing.demand=demand;existing.priority ||= priority;if(target.legacyTarget)existing.target.legacyTarget=target.legacyTarget;if(priority&&this.queue.includes(key)){this.queue=this.queue.filter(id=>id!==key);this.queue.unshift(key);}void this.drain();return existing.promise;}
    let resolve!:(entry:GameMediaEntry)=>void,reject!:(reason:unknown)=>void;
    const promise=new Promise<GameMediaEntry>((yes,no)=>{resolve=yes;reject=no;});
    this.jobs.set(key,{target,consumer,priority,resolve,reject,promise,demand});if(priority)this.queue.unshift(key);else this.queue.push(key);this.changed('demand');void this.drain();return promise;
  }
  private async admitNative(target:WallpaperTarget,cached:GameMediaEntry,demand?:SteamMediaDemand){
    if(demand&&!steamMediaDemandCurrent(demand))throw Error('steam-demand-withdrawn');
    // Only identical in-flight IPC is shared. A new binding or changed target
    // provenance/play record still reaches native admission independently.
    const signature=JSON.stringify([cached.key,cached.bindingRevision,steamMediaDemandFields(demand),
      Object.fromEntries(Object.keys(target).sort().map(key=>[key,target[key as keyof WallpaperTarget]]))]);
    let pending=this.nativeAdmissions.get(signature);
    if(!pending){
      pending=this.port.invoke('gameMedia.background',{targets:[target],...steamMediaDemandFields(demand)},{timeoutMs:30000}).then(()=>{},()=>{});
      this.nativeAdmissions.set(signature,pending);
    }
    try{await pending;}finally{if(this.nativeAdmissions.get(signature)===pending)this.nativeAdmissions.delete(signature);}
  }
  selectSteamTarget(target:WallpaperTarget|null){
    const key=target?wallpaperTargetKey(target):'';
    for(const job of this.jobs.values())if(job.consumer==='steam')job.priority=!!target&&(wallpaperTargetKey(job.target)===key||!!target.executable&&!!job.target.executable&&wallpaperTargetKey({executable:target.executable})===wallpaperTargetKey({executable:job.target.executable}));
    void this.drain();
  }
  reconcilePreparation(){
    const blocked=!this.port.workAllowed();
    if(blocked!==this.powerBlocked){this.powerBlocked=blocked;++this.workEpoch;this.invalidateLookups();this.nativeAdmissions.clear();}
    if(blocked){
      if(this.nextRetry!==null)this.port.cancel(this.nextRetry);this.nextRetry=null;this.retryAt=0;
      for(const job of this.jobs.values())job.reject(Error('wallpaper-power-paused'));
      this.jobs.clear();this.queue=[];return;
    }
    void this.drain();
  }
  retryWhen(at:number){if(Number.isFinite(at)&&at>this.port.now())this.armRetry(at);}
  private armRetry(at:number){if(!this.port.workAllowed()||this.stopped)return;if(this.nextRetry!==null&&this.retryAt<=at)return;if(this.nextRetry!==null)this.port.cancel(this.nextRetry);this.retryAt=at;this.nextRetry=this.port.schedule(()=>{this.nextRetry=null;this.retryAt=0;this.changed('retry');},Math.max(1000,Math.min(3600000,at-this.port.now())));}
  private async plan(url:string,workEpoch=this.workEpoch):Promise<{source:string;segments?:string[]}>{
    if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');
    if(!normalizeSteamMediaUrl(url))throw Error('媒体来源无效');
    if(!/\.m3u8(?:\?|$)/i.test(url))return {source:url};
    let current=url;for(let depth=0;depth<4;depth++){if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');const response=await this.port.request(current);if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');const parsed=parseHlsAsset(response.body,current);if(parsed.segments){if(parsed.segments.some(segment=>!normalizeSteamMediaUrl(segment)))throw Error('媒体分片来源无效');return {source:url,segments:parsed.segments.map(segment=>normalizeSteamMediaUrl(segment)!)};}if(!parsed.variant)break;if(!normalizeSteamMediaUrl(parsed.variant))throw Error('媒体清单来源无效');current=normalizeSteamMediaUrl(parsed.variant)!;}
    throw Error('媒体播放列表层级无效');
  }
  private async prepare(job:Job):Promise<GameMediaEntry>{
    if(!this.port.workAllowed())throw Error('wallpaper-power-paused');
    const workEpoch=this.workEpoch;
    if(job.consumer==='steam'&&!steamMediaDemandCurrent(job.demand))throw Error('steam-demand-withdrawn');
    const invokeOwned=<T=unknown>(command:string,args:Record<string,unknown>,options?:{timeoutMs:number})=>{if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');if(job.consumer==='steam'&&(!this.port.enabled()||!steamMediaDemandCurrent(job.demand)))throw Error('steam-demand-withdrawn');return this.port.invoke<T>(command,{...args,...steamMediaDemandFields(job.demand)},options);};
    let lease=await invokeOwned<GameMediaEntry>('gameMedia.begin',{...job.target,demandConsumer:job.consumer,...steamMediaDemandFields(job.demand)},{timeoutMs:12000});
    if(!lease.acquired){if(lease.leaseUntil)this.armRetry(lease.leaseUntil);return this.lookup(job.target,job.demand);}
    try{
      if(job.target.appId<=0){
        if(!job.target.executable)throw Error('游戏文件身份缺失');
        const identity=await invokeOwned<{appId?:number;name?:string;verified?:boolean}>('gameMedia.identify',{executable:job.target.executable,...steamMediaDemandFields(job.demand)},{timeoutMs:100000});
        if(!identity.verified||!Number.isSafeInteger(identity.appId)||Number(identity.appId)<=0)throw Error('游戏文件身份尚未确认');
        job.target={...job.target,appId:identity.appId!,name:identity.name||job.target.name,verified:true};
        const bound=await invokeOwned<GameMediaEntry>('gameMedia.bind',{...job.target,...steamMediaDemandFields(job.demand)});
        if(bound.taskId===lease.taskId)lease=bound;
        else {lease=await invokeOwned<GameMediaEntry>('gameMedia.begin',{...job.target,demandConsumer:job.consumer,...steamMediaDemandFields(job.demand)},{timeoutMs:12000});if(!lease.acquired){if(lease.leaseUntil)this.armRetry(lease.leaseUntil);return this.lookup(job.target,job.demand);}}
      }
      if(job.consumer==='steam'&&!this.port.enabled()){await invokeOwned('gameMedia.finish',{...lease,...steamMediaDemandFields(job.demand)});return this.lookup(job.target,job.demand);}
      if(job.target.legacyTarget&&lease.taskSlots?.includes('dynamic')){
        const migrated=await invokeOwned<GameMediaEntry>('gameMedia.legacy',{...lease,...job.target,...steamMediaDemandFields(job.demand),...job.target.legacyTarget}).catch(error=>{if(/stale|conflict/.test(String(error)))throw error;return null;});
        if(migrated?.dynamicReady){lease=migrated;this.changed('asset',lease.key);if(!lease.taskSlots?.length){await invokeOwned('gameMedia.finish',{...lease,...steamMediaDemandFields(job.demand)});return this.lookup(job.target,job.demand);}}
      }
      if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');
      const media=await this.port.candidates(job.target);
      if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');
      if(job.consumer==='steam'&&!this.port.enabled()){await invokeOwned('gameMedia.finish',{...lease,...steamMediaDemandFields(job.demand)});return this.lookup(job.target,job.demand);}const needed=lease.taskSlots||['dynamic'];let last:unknown;const failures:unknown[]=[];
      if(needed.includes('static')&&media.poster)try{lease=await invokeOwned<GameMediaEntry>('gameMedia.install',{...lease,...job.target,...steamMediaDemandFields(job.demand),source:media.poster,name:media.name},{timeoutMs:135000});this.changed('asset',lease.key);}catch(error){if(/stale|conflict/.test(String(error)))throw error;failures.push(error);}
      const video=media.urls.filter(url=>/\.(?:webm|mp4|m3u8)(?:\?|$)/i.test(url));
      let published=!needed.includes('dynamic');
      if(needed.includes('dynamic')&&(job.consumer!=='steam'||this.port.enabled()))for(const source of video.slice(0,4))try{
        const plan=await this.plan(source,workEpoch);lease=await invokeOwned<GameMediaEntry>('gameMedia.install',{...lease,...job.target,...steamMediaDemandFields(job.demand),...plan,videoQualityPolicy:3,name:media.name},{timeoutMs:135000});published=true;this.changed('asset',lease.key);break;
      }catch(error){last=error;if(/stale|conflict/.test(String(error)))throw error;}
      if(!published&&video.length&&(job.consumer!=='steam'||this.port.enabled()))failures.push(last||Error('动态素材准备失败'));
      if(failures.length)throw failures[0];
      await invokeOwned('gameMedia.finish',{...lease,...steamMediaDemandFields(job.demand),videoUnavailable:video.length===0,staticUnavailable:!media.poster});
      this.changed('asset',lease.key);return this.lookup(job.target,job.demand);
    }catch(error){if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');if(job.consumer==='steam'&&!steamMediaDemandCurrent(job.demand))throw Error('steam-demand-withdrawn');const failed=await invokeOwned<GameMediaEntry>('gameMedia.fail',{...lease,...steamMediaDemandFields(job.demand),reason:String(error instanceof Error?error.message:error)}).catch(()=>lease);if(failed.retryAt)this.armRetry(failed.retryAt);else if(/stale|conflict/.test(String(error))&&!this.stopped&&(job.consumer!=='steam'||this.port.enabled()))this.armRetry(this.port.now()+1000);this.changed('asset',failed.key);return this.lookup(job.target,job.demand).catch(()=>failed);}
  }
  private async drain(){if(this.running||this.stopped||!this.port.workAllowed())return;this.running=true;try{while(this.queue.length&&!this.stopped&&this.port.workAllowed()){
    const index=this.queue.findIndex(key=>{const job=this.jobs.get(key);return !job||job.consumer==='steam'&&!this.port.enabled()||!this.preparationState.storageBlocked&&(!this.preparationState.backgroundBlocked||job.priority||job.consumer!=='steam');});
    if(index<0)break;const key=this.queue.splice(index,1)[0],job=this.jobs.get(key);if(!job)continue;
    if(job.consumer==='steam'&&!this.port.enabled()){this.jobs.delete(key);job.resolve(await this.lookup(job.target,job.demand));continue;}
    try{job.resolve(await this.prepare(job));}catch(error){job.reject(error);}finally{if(this.jobs.get(key)===job)this.jobs.delete(key);}
  }}finally{this.running=false;this.changed('demand');}}
  async supplement(includeInstalled=true,manual=false,inventoryRequestId=''){if((!this.port.backgroundEnabled()&&!manual)||this.stopped||!this.port.workAllowed())return;
    const workEpoch=this.workEpoch;
    const [installed,library]=await Promise.all([includeInstalled?this.port.installed().catch(()=>[]):Promise.resolve([]),this.port.invoke<WallpaperTarget[]>('gameMedia.inventory').catch(()=>[])]);
    const targets=new Map<string,WallpaperTarget>();
    const add=(target:WallpaperTarget)=>{const key=wallpaperTargetKey(target),prior=targets.get(key);
      if(!prior){targets.set(key,target);return;}
      const preferred=wallpaperSourcePriority(prior)<=wallpaperSourcePriority(target)?prior:target;
      // Keep the preferred native identity or shortcut's exact EXE/provenance
      // when several local records share one verified Steam store AppID.
      const merged={...target,...prior,...preferred,gameDirectory:preferred.gameDirectory||prior.gameDirectory||target.gameDirectory,lastPlayed:Math.max(Number(prior.lastPlayed)||0,Number(target.lastPlayed)||0)};
      for(const field of ['executable','steamAccount','shortcutAppId'] as const)if(preferred[field]===undefined)delete merged[field];
      targets.set(key,merged);
    };
    for(const target of installed)add({...target,sourceTier:1,catalogSource:'steam-native'});
    for(const target of library)add({...target,sourceTier:2,catalogSource:target.catalogSource==='steam-shortcut'?'steam-shortcut':'ymcc-confirmed'});

    if((!this.port.backgroundEnabled()&&!manual)||this.stopped||!this.port.workAllowed())return;
    if(!this.workCurrent(workEpoch))return;
    // The library process owns supplementation; display consumers keep only
    // explicit foreground demand. Both paths share native revision leases.
    await this.port.invoke('gameMedia.background',{targets:[...targets.values()],...(inventoryRequestId?{inventoryRequestId}:{})},{timeoutMs:30000});
  }
  preparationDescription(){const messages:Record<string,string>={
    'wallpaper-on-battery':'离电关闭已生效，播放、缓存和下载已暂停',
    'wallpaper-power-unknown':'正在确认真实电源，壁纸任务已暂停',
    'capacity-unavailable':'正在检查壁纸缓存；本地壁纸仍可使用',
    'media-power-transition':'睡眠恢复期间暂缓补齐；就绪后自动继续',
    'media-disk-space-low':'磁盘余量不足，已暂停补齐；本地壁纸仍可使用',
    'media-cache-budget-full':'壁纸缓存已达 4 GB，已暂停补齐；可在游戏库管理素材',
    'media-cache-budget-unknown':'缓存状态暂不可读，已暂停补齐；稍后重试',
    'battery-saver':'节电模式下暂停后台补齐；当前壁纸仍可使用',
    'low-battery':'低电量时暂停后台补齐；当前壁纸仍可使用',
    'game-running':'游戏运行中暂停后台补齐；当前游戏仍可优先准备',
  };return messages[this.preparationState.reason]||'';}
  inspect(){return {running:this.running,queued:this.queue.length,tasks:this.jobs.size,...this.preparationState};}
  stop(){this.stopped=true;this.invalidateLookups();this.nativeAdmissions.clear();if(this.nextRetry!==null)this.port.cancel(this.nextRetry);for(const job of this.jobs.values())job.reject(Error('素材服务已停止'));this.jobs.clear();this.queue=[];this.listeners.clear();}
}
// One deferred scan per burst; a change arriving during a read is never lost.
export class MediaInventoryScan {
  private timer:ReturnType<typeof setTimeout>|null=null;private running=false;private pending=false;private full=false;private stopped=false;
  constructor(private read:(includeInstalled:boolean)=>Promise<void>,private enabled:()=>boolean,private clock={schedule:(...args:Parameters<typeof setTimeout>)=>globalThis.setTimeout(...args),cancel:(timer:ReturnType<typeof setTimeout>)=>globalThis.clearTimeout(timer)}){}
  request(includeInstalled=false){
    if(this.stopped||!this.enabled())return;this.pending=true;this.full ||= includeInstalled;
    if(this.running)return;
    if(this.timer!==null){if(!includeInstalled)return;this.clock.cancel(this.timer);}
    this.timer=this.clock.schedule(()=>{this.timer=null;void this.run();},includeInstalled?0:250);
  }
  private async run(){
    if(this.stopped||!this.enabled()){this.pending=false;this.full=false;return;}
    const full=this.full;this.full=false;this.pending=false;this.running=true;
    try{await this.read(full);}catch{}finally{this.running=false;if(this.pending&&!this.stopped)this.request(this.full);}
  }
  stop(){this.stopped=true;this.pending=false;if(this.timer!==null)this.clock.cancel(this.timer);this.timer=null;}
}
// A newly confirmed EXE may reuse an already-cached Steam asset. Notify its
// previous selection key exactly once; unrelated progress must remain inert.
export function reconcileMediaAliases(known:Map<string,string>,aliases:Record<string,string>,notify:(previous:string,key:string)=>void){for(const [previous,key] of Object.entries(aliases))if(known.get(previous)!==key){known.set(previous,key);notify(previous,key);}}
export const sharedGameMedia=new SharedGameMedia();
export const steamWallpaperEnabled=()=>getUiSetting('steamDynamicWallpaperEnabled');
export async function setSteamWallpaperEnabled(enabled:boolean){await setUiSettings({steamDynamicWallpaperEnabled:enabled});sharedGameMedia.changed('settings');}
export function startSharedGameMedia(){
  let disposed=false,lastEnabled=false,refreshing=false,refreshAgain=false;let lastSteam:boolean|undefined;
  const revisions=new Map<string,number>();const aliases=new Map<string,string>();
  const stopDownloads=gameMediaDownloads.start();
  const manualAllowed=()=>Number(gameMediaDownloads.state.manualUntil||0)>Date.now();
  const scans=new MediaInventoryScan(full=>sharedGameMedia.supplement(full,manualAllowed(),gameMediaDownloads.state.inventoryRequestId||''),()=>!disposed&&uiSettingsLoaded()&&!wallpaperPowerBlocked()&&(backgroundDownloadEnabled()||manualAllowed()));
  const inventoryReadyAt=Date.now()+30000;let inventoryBootTimer:ReturnType<typeof setTimeout>|null=null,inventoryFull=false;
  const scan=(includeInstalled=true)=>{if(wallpaperPowerBlocked())return;if(Date.now()>=inventoryReadyAt){scans.request(includeInstalled);return;}inventoryFull ||= includeInstalled;if(inventoryBootTimer===null)inventoryBootTimer=setTimeout(()=>{inventoryBootTimer=null;if(!disposed)scans.request(inventoryFull);inventoryFull=false;},Math.max(0,inventoryReadyAt-Date.now()));};
  const policy=new GameMediaPreparationPolicy({read:()=>wallpaperPowerBlocked()?Promise.resolve({blockedReason:wallpaperPowerReason()}):invoke('gameMedia.preparationPolicy',{}, {timeoutMs:5000}),apply:state=>{const value=wallpaperPowerBlocked()?{backgroundBlocked:true,storageBlocked:true,reason:wallpaperPowerReason()}:state;sharedGameMedia.setPreparationState(value);gameMediaDownloads.policy(value.reason);},queued:()=>sharedGameMedia.inspect().queued+Number(gameMediaDownloads.state.remaining||0)+Number(gameMediaDownloads.state.catalogPending===true),enabled:()=>!wallpaperPowerBlocked()&&(backgroundDownloadEnabled()||Number(gameMediaDownloads.state.manualUntil||0)>Date.now()),recheck:async()=>{await Promise.allSettled([refreshPolicyGameStrict(),gameMediaDownloads.recheck()]);if(!disposed)await refresh();}});
  let admissionGame:Parameters<typeof resolveSteamWallpaperIdentity>[0]|null=null,admissionKey='',admissionEpoch=0,admissionSuspended=false;
  const reconcileGameAdmission=()=>{
    const game=admissionGame,enabled=!disposed&&!admissionSuspended&&uiSettingsLoaded()&&backgroundDownloadEnabled()&&!wallpaperPowerBlocked();
    const key=enabled&&game?JSON.stringify([game.pid,game.processCreated,game.path.toLowerCase(),game.identityGeneration,game.source,game.whitelistRule]):'';
    if(key===admissionKey)return;admissionKey=key;const epoch=++admissionEpoch;if(!game||!key)return;
    // Selection observation owns recency. A slow local lookup cannot make an
    // exited/replaced game newer or survive OFF, suspend, or an ABA handoff.
    const observedAt=Date.now();
    void resolveSteamWallpaperIdentity({...game}).then(target=>{
      if(disposed||admissionSuspended||epoch!==admissionEpoch||!uiSettingsLoaded()||!backgroundDownloadEnabled()||wallpaperPowerBlocked()||!target.verified)return;
      return invoke('gameMedia.background',{targets:[{...target,lastPlayed:observedAt}]});
    }).catch(()=>{});
  };
  const gamePolicy=subscribePolicyGameStatus(game=>{admissionGame=game?{...game}:null;policy.game(!!game);reconcileGameAdmission();});
  const powerPolicy=on('power.sourceChanged',()=>policy.request());
  const saverPolicy=on('gameMedia.preparationPolicyChanged',()=>policy.request());
  const suspendPolicy=on('power.suspending',()=>{admissionSuspended=true;reconcileGameAdmission();policy.suspend();});
  const resumePolicy=subscribeGameMediaResume(()=>{
    admissionSuspended=false;admissionGame=null;admissionKey='';const epoch=++admissionEpoch;policy.resume();
    // Reuse the authoritative detector's strict refresh; a retained pre-sleep
    // snapshot must not manufacture a new play observation after a missed exit.
    void refreshPolicyGameStrict().then(game=>{if(disposed||admissionSuspended||epoch!==admissionEpoch)return;admissionGame=game?{...game}:null;reconcileGameAdmission();}).catch(()=>{});
  },on);
  const demandPolicy=sharedGameMedia.subscribe(reason=>{if(reason==='demand')policy.demand();else if(reason==='retry')policy.request();});
  const completionPolicy=gameMediaDownloads.watchCompleted(()=>{void refresh();});
  const downloadPolicy=gameMediaDownloads.subscribe(()=>policy.demand());
  policy.request();
  async function refresh(){if(disposed||wallpaperPowerBlocked())return;const powerEpoch=captureWallpaperPower();if(refreshing){refreshAgain=true;return;}refreshing=true;
    try{const state=await invoke<{entries:Record<string,GameMediaEntry>;aliases?:Record<string,string>}>('gameMedia.snapshot');if(disposed||!wallpaperPowerCurrent(powerEpoch))return;
      reconcileMediaAliases(aliases,state.aliases||{},(previous,key)=>{revisions.delete(previous);sharedGameMedia.changed('asset',previous);window.dispatchEvent(new CustomEvent('game-media:changed',{detail:{key:previous,canonicalKey:key}}));});
      for(const entry of Object.values(state.entries)){
        const old=revisions.get(entry.key);revisions.set(entry.key,entry.bindingRevision);
        if(old!==entry.bindingRevision){sharedGameMedia.changed('asset',entry.key);window.dispatchEvent(new CustomEvent('game-media:changed',{detail:{key:entry.key,revision:entry.bindingRevision}}));}
        const requested=entry.refreshRequested===true;
        const consumer=requested?'library':entry.demandConsumer==='steam'?'steam':entry.demandConsumer==='ymcc'?'ymcc':entry.demandConsumer==='library'||!entry.demandConsumer?'library':null;
        const owned=!wallpaperPowerBlocked()&&(consumer==='steam'?steamWallpaperEnabled():consumer==='ymcc'?getUiSetting('dynamicBackgroundEnabled'):consumer==='library'&&(requested||backgroundDownloadEnabled()||manualAllowed()));
        const pending=entry.status==='pending',partial=entry.ready&&!gameMediaPairComplete(entry)&&Number(entry.leaseUntil||0)<=Date.now(),recoverable=pending||entry.status==='error'||partial;const deadline=Number(pending?entry.leaseUntil:entry.retryAt)||0;
        if(owned&&recoverable&&!entry.deleted)sharedGameMedia.retryWhen(deadline);
        const retry=owned&&recoverable&&deadline<=Date.now();
        if(consumer&&consumer!=='steam'&&(requested||retry)&&(entry.appId||entry.executable)&&!entry.deleted){void sharedGameMedia.ensure({appId:entry.appId||0,name:entry.name||'',executable:entry.executable,verified:entry.identityVerified===true},consumer,false,true).catch(()=>{});}
      }
    }catch{}finally{refreshing=false;if(refreshAgain){refreshAgain=false;void refresh();}}
  }
  const off=on('gameMedia.changed',()=>{sharedGameMedia.invalidateLookups();policy.request();void refresh();});
  const inventory=on('gameMedia.inventoryChanged',()=>{sharedGameMedia.invalidateLookups();scan(true);});
  let inventoryRequest='';const rescanRequest=gameMediaDownloads.subscribe(()=>{const id=gameMediaDownloads.state.inventoryRequestId;if(id&&id!==inventoryRequest){inventoryRequest=id;scan(true);}});
  const settings=()=>{const enabled=uiSettingsLoaded()&&backgroundDownloadEnabled()&&!wallpaperPowerBlocked();reconcileGameAdmission();sharedGameMedia.reconcilePreparation();sharedGameMedia.changed('settings');if(enabled&&!lastEnabled)scan();lastEnabled=enabled;};
  const powerChanged=subscribeWallpaperPowerChanged(blocked=>{
    if(blocked&&inventoryBootTimer!==null){clearTimeout(inventoryBootTimer);inventoryBootTimer=null;}
    sharedGameMedia.reconcilePreparation();reconcileGameAdmission();
    // Keep admission closed until the existing policy owner confirms game/storage state.
    gameMediaDownloads.policy(blocked?wallpaperPowerReason():'capacity-unavailable');
    sharedGameMedia.changed('settings');policy.demand();
    if(!blocked){scan();policy.request();void refresh();}
  });
  window.addEventListener('ui-settings:changed',settings);window.addEventListener('ui-settings:loaded',settings);
  const steam=on<{steamKnown?:boolean;steamRunning?:boolean}>('deckySidebar.updated',state=>{if(state.steamKnown&&state.steamRunning!==lastSteam){lastSteam=state.steamRunning;if(lastSteam)scan();}});
  const retry=sharedGameMedia.subscribe(reason=>{if(reason==='retry')void refresh();else if(reason==='inventory')scan(false);});
  void invoke('gameMedia.watch',{enabled:true}).catch(()=>{});settings();void refresh();
  return()=>{disposed=true;powerChanged();if(inventoryBootTimer!==null)clearTimeout(inventoryBootTimer);stopDownloads();policy.stop();gamePolicy();powerPolicy();saverPolicy();suspendPolicy();resumePolicy();demandPolicy();downloadPolicy();completionPolicy();rescanRequest();scans.stop();off();inventory();steam();retry();window.removeEventListener('ui-settings:changed',settings);window.removeEventListener('ui-settings:loaded',settings);void invoke('gameMedia.watch',{enabled:false}).catch(()=>{});sharedGameMedia.stop();};
}
