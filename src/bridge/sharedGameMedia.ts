import {wallpaperPowerBlocked,wallpaperPowerReason,subscribeWallpaperPowerChanged,captureWallpaperPower,wallpaperPowerCurrent} from './wallpaperPowerPolicy';
import {gameMediaDownloads,backgroundDownloadEnabled} from './gameMediaDownloads';
import {invoke,on} from './ipc';
import {steamUi,type SteamUiState} from './steamUi';
import {STEAM_AUTOMATIC_READ_GAP_MS} from './steamReadBudget';
import {getPolicyGame,isPolicyGameCurrent,subscribePolicyGameReadiness,subscribePolicyGameStatus,refreshPolicyGameStrict} from './gamePolicyTarget';
import {GameMediaPreparationPolicy} from './gameMediaPreparationPolicy';
import {onSharedPowerLifecycle,isSharedPowerLifecycleAdmitted} from './powerLifecycleState';
import {getUiSetting,setUiSettings,uiSettingsLoaded} from './uiSettings';
import {requestSteamHttp} from './steamHttp';
import {parseHlsAsset} from './steamMediaPlaylist';
import {installedSteamWallpaperTargets,steamWallpaperCandidates,normalizeSteamMediaUrl,resolveSteamWallpaperIdentity,type WallpaperTarget} from './steamMediaSource';
export interface GameMediaAsset {id:string;revision:string;kind:'video'|'image';mime:string;extension:string;bytes:number;qualityPolicy?:number;targetHeight?:number;selectedHeight?:number;reducedPlayback?:true;}
export interface GameMediaEntry {key:string;appId?:number;name?:string;executable?:string;bindingRevision:number;status:string;ready?:boolean;asset?:GameMediaAsset;url?:string;localUrl?:string;manual?:boolean;deleted?:boolean;refreshRequested?:boolean;retryAt?:number;leaseUntil?:number;taskId?:string;acquired?:boolean;error?:string;identityVerified?:boolean;wallpaperMode?:'static'|'dynamic';assets?:{static?:GameMediaAsset;dynamic?:GameMediaAsset};staticReady?:boolean;dynamicReady?:boolean;taskSlots?:string[];demandConsumer?:string;videoUnavailableUntil?:number;staticUnavailableUntil?:number;manualSlots?:Record<string,boolean>;deletedSlots?:Record<string,boolean>;playback?:{start:number;end:number;fit:'cover'|'contain';startMode?:'random'|'from-start'};currentGameWallpaperFallback?:true;}
export interface GameMediaEntry {wallpaper30Limited?:true;wallpaperPlaybackStamp?:string;}
export function gameMediaPairComplete(entry:GameMediaEntry,now=Date.now()){return ['static','dynamic'].every(slot=>entry[slot==='static'?'staticReady':'dynamicReady']||entry.deletedSlots?.[slot]||entry.manualSlots?.[slot]||Number(slot==='static'?entry.staticUnavailableUntil:entry.videoUnavailableUntil)>now);}
export function hasWallpaperBinding(entry:GameMediaEntry|null|undefined){return !!entry&&!entry.deleted&&(entry.ready===true||entry.wallpaper30Limited===true&&entry.dynamicReady===true);}
export function wallpaperSourcePriority(target:Partial<WallpaperTarget>){const tier=target.sourceTier??3;return tier<2?tier:target.catalogSource==='steam-shortcut'?2:tier===2?3:4;}
export function wallpaperTargetKey(target:Partial<WallpaperTarget>){return target.appId&&target.appId>0?'steam-'+target.appId:'exe-'+String(target.executable||'').replace(/\//g,'\\').toLowerCase();}
export interface SteamMediaDemand {demandConsumer:'steam';demandEpoch:string;demandRevision:number;current?:()=>boolean;}
export function steamMediaDemandCurrent(value:SteamMediaDemand|undefined){return !!value&&value.demandConsumer==='steam'&&typeof value.demandEpoch==='string'&&!!value.demandEpoch&&new TextEncoder().encode(value.demandEpoch).length<=128&&Number.isSafeInteger(value.demandRevision)&&value.demandRevision>=0&&(value.current?.()??true);}
function steamMediaDemandFields(value:SteamMediaDemand|undefined){return value?{demandConsumer:'steam' as const,demandEpoch:value.demandEpoch,demandRevision:value.demandRevision}:{};}
interface Job {demand?:SteamMediaDemand;target:WallpaperTarget;consumer:string;priority:boolean;resolve:(entry:GameMediaEntry)=>void;reject:(error:unknown)=>void;promise:Promise<GameMediaEntry>;}
interface SnapshotRecovery {target:WallpaperTarget;consumer:'ymcc'|'library';current:()=>boolean;}
interface NativeAdmission {target:WallpaperTarget;cached:GameMediaEntry;}
// Bound the wait for other replies after the first target is ready to submit.
const SNAPSHOT_ADMISSION_WINDOW_MS=40;
// Wire bits; keep in order with wallpaper-inventory diagnostics on the native side.
const enum InventoryOrigin {
  LibraryNotice=1<<0, PageHint=1<<1, Manual=1<<2, Enabled=1<<3, PowerRecovery=1<<4,
  SteamProcess=1<<5, SteamAccount=1<<6, SteamDocument=1<<7, SteamMode=1<<8, SteamRevalidated=1<<9,
}
export interface MediaPorts {workAllowed:()=>boolean;nativeBackground:boolean;invoke:typeof invoke;enabled:()=>boolean;now:()=>number;candidates:typeof steamWallpaperCandidates;request:typeof requestSteamHttp;installed:typeof installedSteamWallpaperTargets;backgroundEnabled:()=>boolean;schedule:typeof setTimeout;cancel:typeof clearTimeout;}
export class SharedGameMedia {
  private port:MediaPorts;
  constructor(overrides:Partial<MediaPorts>={}){this.port={workAllowed:()=>!wallpaperPowerBlocked(),invoke,nativeBackground:true,enabled:()=>getUiSetting('steamDynamicWallpaperEnabled'),now:Date.now,candidates:steamWallpaperCandidates,request:requestSteamHttp,installed:installedSteamWallpaperTargets,backgroundEnabled:backgroundDownloadEnabled,schedule:(...args:Parameters<typeof setTimeout>)=>globalThis.setTimeout(...args),cancel:(timer:ReturnType<typeof setTimeout>)=>globalThis.clearTimeout(timer),...overrides};}
  private workEpoch=0;private powerBlocked=false;private sharedPowerAdmitted=true;
  private powerWork=new AbortController();
  private workAllowed(){return this.sharedPowerAdmitted&&this.port.workAllowed();}
  private workCurrent(epoch:number){return !this.stopped&&this.workAllowed()&&epoch===this.workEpoch;}
  private jobs=new Map<string,Job>();private queue:string[]=[];private runningEpoch:number|null=null;private stopped=false;
  private nativeAdmissions=new Map<string,Promise<void>>();
  private lookupFlights=new Map<string,{promise:Promise<GameMediaEntry>;demand?:SteamMediaDemand}>();
  private preparationState={backgroundBlocked:false,storageBlocked:false,reason:''};
  setPreparationState(state:Partial<typeof this.preparationState>){const next={...this.preparationState,...state};if(next.backgroundBlocked===this.preparationState.backgroundBlocked&&next.storageBlocked===this.preparationState.storageBlocked&&next.reason===this.preparationState.reason)return;this.preparationState=next;this.changed('policy');void this.drain();}
  private listeners=new Set<(reason:string,key?:string)=>void>();private nextRetry:ReturnType<typeof setTimeout>|null=null;private retryAt=0;
  subscribe(listener:(reason:string,key?:string)=>void){this.listeners.add(listener);return()=>{this.listeners.delete(listener);};}
  invalidateLookups(){this.lookupFlights.clear();}
  changed(reason='asset',key?:string){this.invalidateLookups();for(const listener of this.listeners)try{listener(reason,key);}catch{}}
  async playbackFailed(entry:{key:string;assetId:string;bindingRevision:number;kind?:string},demand?:SteamMediaDemand){
    if(!this.workAllowed())throw Error('wallpaper-power-paused');
    if(demand&&!steamMediaDemandCurrent(demand))throw Error('steam-demand-withdrawn');
    if(this.stopped)throw Error('media-owner-stopped');
    const epoch=this.workEpoch;
    const failed=await this.port.invoke<GameMediaEntry>('gameMedia.edit',{key:entry.key,assetId:entry.assetId,bindingRevision:entry.bindingRevision,slot:entry.kind==='image'?'static':'dynamic',action:'playback-failed',...steamMediaDemandFields(demand)},{timeoutMs:5000,signal:this.powerWork.signal});
    if(!this.workCurrent(epoch))throw Error('wallpaper-power-paused');
    if(!demand||steamMediaDemandCurrent(demand)){if(failed.retryAt)this.armRetry(failed.retryAt);this.changed('asset',failed.key);}return failed;
  }
  // Retained as an inert compatibility entry for old callers. No IPC or I/O.
  async prewarm(_targets:WallpaperTarget[]):Promise<GameMediaEntry[]>{return [];}
  automaticPreparationEnabled(){return !this.port.nativeBackground||this.port.backgroundEnabled();}
  lookup(target:Partial<WallpaperTarget>&{key?:string;currentGameWallpaper?:true;localWallpaperDelivery?:true;wallpaperPlayback?:true;wallpaperPlaybackFallback?:{assetId:string;bindingRevision:number}},demand?:SteamMediaDemand):Promise<GameMediaEntry>{
    if(this.stopped)return Promise.reject(Error('media-owner-stopped'));
    if(!this.workAllowed())return Promise.reject(Error('wallpaper-power-paused'));
    const workEpoch=this.workEpoch;
    if(demand&&!steamMediaDemandCurrent(demand))return Promise.reject(Error('steam-demand-withdrawn'));
    const args={...target,...steamMediaDemandFields(demand)};
    const signature=JSON.stringify(Object.fromEntries(Object.keys(args).sort().map(key=>[key,args[key as keyof typeof args]])));
    let flight=this.lookupFlights.get(signature);
    // A reselected card can have the same wire ticket as a retired selection.
    if(flight?.demand?.current?.()===false){this.lookupFlights.delete(signature);flight=undefined;}
    if(!flight){
      let resolve!:(entry:GameMediaEntry)=>void,reject!:(error:unknown)=>void;
      const promise=new Promise<GameMediaEntry>((yes,no)=>{resolve=yes;reject=no;});
      const owned=flight={promise,demand};
      // Share only overlapping reads. No settled entry or persistent freshness cache.
      if(this.lookupFlights.size<64)this.lookupFlights.set(signature,owned);
      const release=()=>{if(this.lookupFlights.get(signature)===owned)this.lookupFlights.delete(signature);};
      void promise.then(release,release);
      try{void Promise.resolve(this.port.invoke<GameMediaEntry>('gameMedia.resolve',args,{timeoutMs:12000,signal:this.powerWork.signal})).then(resolve,reject);}catch(error){reject(error);}
    }
    return flight.promise.then(entry=>{
      if(this.stopped)throw Error('media-owner-stopped');
      if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');
      if(demand&&!steamMediaDemandCurrent(demand))throw Error('steam-demand-withdrawn');
      // IPC returns JSON values. Each consumer retains its own mutable result.
      return JSON.parse(JSON.stringify(entry)) as GameMediaEntry;
    });
  }
  async ensure(target:WallpaperTarget,consumer:'ymcc'|'steam'|'library',priority=false,completePair=false,demand?:SteamMediaDemand,collect?:(admission:NativeAdmission)=>void):Promise<GameMediaEntry>{
    if(this.stopped||!this.workAllowed())return {key:wallpaperTargetKey(target),bindingRevision:0,status:'consumer-inactive',ready:false,acquired:false};
    if(consumer==='steam'&&!this.port.enabled())return {key:wallpaperTargetKey(target),bindingRevision:0,status:'consumer-inactive',ready:false,acquired:false};
    if(consumer==='steam'&&!steamMediaDemandCurrent(demand))throw Error('steam-foreground-required');
    if(consumer!=='steam')demand=undefined;
    if(target.executable&&target.appId>0&&target.verified!==true)target={...target,appId:0};
    const workEpoch=this.workEpoch;
    const cached=await this.lookup(consumer==='ymcc'?{...target,localWallpaperDelivery:true}:target,demand);
    if(!this.workCurrent(workEpoch))return {...cached,status:'consumer-inactive',ready:false,acquired:false};
    if(demand&&!steamMediaDemandCurrent(demand))throw Error('steam-demand-withdrawn');
    const paired=gameMediaPairComplete(cached,this.port.now());
    if(cached.deleted||cached.manual&&!completePair&&!cached.refreshRequested||hasWallpaperBinding(cached)&&!cached.refreshRequested&&(!completePair||paired))return cached;
    // All automatic network work uses the stoppable, low-priority library
    // process. Merely showing a current game cannot bypass battery/CPU/boot
    // gates through the renderer's former foreground preparation lane.
    if(this.port.nativeBackground){
      if(this.automaticPreparationEnabled()&&!this.stopped){
        if(collect&&consumer!=='steam')collect({target,cached});
        else await this.admitNative([{target,cached}],demand);
      }
      if(!this.workCurrent(workEpoch))return {...cached,status:'consumer-inactive',ready:false,acquired:false};
      return cached;
    }
    if(cached.retryAt&&cached.retryAt>this.port.now()){this.armRetry(cached.retryAt);return cached;}
    const key=wallpaperTargetKey(target);const existing=this.jobs.get(key);if(existing){if(consumer!=='steam'){existing.consumer=consumer;existing.demand=undefined;}else if(existing.consumer==='steam')existing.demand=demand;existing.priority ||= priority;if(target.legacyTarget)existing.target.legacyTarget=target.legacyTarget;if(priority&&this.queue.includes(key)){this.queue=this.queue.filter(id=>id!==key);this.queue.unshift(key);}void this.drain();return existing.promise;}
    let resolve!:(entry:GameMediaEntry)=>void,reject!:(reason:unknown)=>void;
    const promise=new Promise<GameMediaEntry>((yes,no)=>{resolve=yes;reject=no;});
    this.jobs.set(key,{target,consumer,priority,resolve,reject,promise,demand});if(priority)this.queue.unshift(key);else this.queue.push(key);this.changed('demand');void this.drain();return promise;
  }
  restoreSnapshot(targets:SnapshotRecovery[],signal:AbortSignal,finished:()=>void){
    const epoch=this.workEpoch,powerSignal=this.powerWork.signal;
    const active=()=>!signal.aborted&&this.workCurrent(epoch);
    if(!targets.length||!active()){finished();return;}
    let remaining=targets.length,cancelled=false,timer:ReturnType<typeof setTimeout>|null=null;
    let ready:Array<NativeAdmission&{current:()=>boolean}>=[];
    const clear=()=>{if(timer!==null){this.port.cancel(timer);timer=null;}};
    const detach=()=>{signal.removeEventListener('abort',cancel);powerSignal.removeEventListener('abort',cancel);};
    const cancel=()=>{cancelled=true;clear();ready=[];detach();finished();};
    const flush=()=>{
      clear();const admissions=ready;ready=[];
      if(!active()||!this.automaticPreparationEnabled())return;
      const current=admissions.filter(item=>item.current());
      for(let i=0;i<current.length;i+=10000)void this.admitNative(current.slice(i,i+10000)).catch(()=>{});
    };
    const finish=()=>{if(!cancelled&&!--remaining){detach();flush();finished();}};
    signal.addEventListener('abort',cancel,{once:true});powerSignal.addEventListener('abort',cancel,{once:true});
    for(const {target,consumer,current} of targets){
      void this.ensure(target,consumer,false,true,undefined,admission=>{
        if(!active()||!current())return;
        ready.push({...admission,current});
        if(timer===null&&remaining>1)timer=this.port.schedule(flush,SNAPSHOT_ADMISSION_WINDOW_MS);
      }).then(finish,finish);
    }
  }
  private async admitNative(admissions:NativeAdmission[],demand?:SteamMediaDemand){
    if(demand&&!steamMediaDemandCurrent(demand))throw Error('steam-demand-withdrawn');
    // Only identical in-flight IPC is shared. A new binding or changed target
    // provenance/play record still reaches native admission independently.
    const fields=steamMediaDemandFields(demand),owned=new Map<string,WallpaperTarget>(),waiting=new Set<Promise<void>>();
    for(const {target,cached} of admissions){
      const signature=JSON.stringify([cached.key,cached.bindingRevision,fields,
        Object.fromEntries(Object.keys(target).sort().map(key=>[key,target[key as keyof WallpaperTarget]]))]);
      const pending=this.nativeAdmissions.get(signature);
      if(pending)waiting.add(pending);else owned.set(signature,target);
    }
    if(owned.size){
      const pending=this.port.invoke('gameMedia.background',{targets:[...owned.values()],...fields,replyStatus:false},{timeoutMs:30000,signal:this.powerWork.signal}).then(()=>{},()=>{});
      for(const signature of owned.keys())this.nativeAdmissions.set(signature,pending);
      waiting.add(pending);
      void pending.then(()=>{for(const signature of owned.keys())if(this.nativeAdmissions.get(signature)===pending)this.nativeAdmissions.delete(signature);});
    }
    await Promise.all(waiting);
  }
  selectSteamTarget(target:WallpaperTarget|null){
    const key=target?wallpaperTargetKey(target):'';
    for(const job of this.jobs.values())if(job.consumer==='steam')job.priority=!!target&&(wallpaperTargetKey(job.target)===key||!!target.executable&&!!job.target.executable&&wallpaperTargetKey({executable:target.executable})===wallpaperTargetKey({executable:job.target.executable}));
    void this.drain();
  }
  sharedPowerChanged(admitted:boolean,retire:boolean){this.sharedPowerAdmitted=admitted;this.reconcilePreparation(retire);}
  reconcilePreparation(retire=false){
    const blocked=!this.workAllowed();
    if(retire||blocked!==this.powerBlocked){
      this.powerBlocked=blocked;++this.workEpoch;
      const old=this.powerWork;this.powerWork=new AbortController();
      this.runningEpoch=null;this.invalidateLookups();this.nativeAdmissions.clear();
      // This returns the renderer's waiting slot; Native owns posted work.
      old.abort();
    }
    if(blocked||retire){
      if(this.nextRetry!==null)this.port.cancel(this.nextRetry);this.nextRetry=null;this.retryAt=0;
      for(const job of this.jobs.values())job.reject(Error('wallpaper-power-paused'));
      this.jobs.clear();this.queue=[];return;
    }
    void this.drain();
  }
  retryWhen(at:number){if(Number.isFinite(at)&&at>this.port.now())this.armRetry(at);}
  private armRetry(at:number){if(!this.workAllowed()||this.stopped)return;if(this.nextRetry!==null&&this.retryAt<=at)return;if(this.nextRetry!==null)this.port.cancel(this.nextRetry);this.retryAt=at;const epoch=this.workEpoch;const timer=this.port.schedule(()=>{if(this.nextRetry!==timer)return;this.nextRetry=null;this.retryAt=0;if(this.workCurrent(epoch))this.changed('retry');},Math.max(1000,Math.min(3600000,at-this.port.now())));this.nextRetry=timer;}
  private async plan(url:string,workEpoch=this.workEpoch):Promise<{source:string;segments?:string[]}>{
    if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');
    if(!normalizeSteamMediaUrl(url))throw Error('媒体来源无效');
    if(!/\.m3u8(?:\?|$)/i.test(url))return {source:url};
    let current=url;for(let depth=0;depth<4;depth++){if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');const response=await this.port.request(current);if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');const parsed=parseHlsAsset(response.body,current);if(parsed.segments){if(parsed.segments.some(segment=>!normalizeSteamMediaUrl(segment)))throw Error('媒体分片来源无效');return {source:url,segments:parsed.segments.map(segment=>normalizeSteamMediaUrl(segment)!)};}if(!parsed.variant)break;if(!normalizeSteamMediaUrl(parsed.variant))throw Error('媒体清单来源无效');current=normalizeSteamMediaUrl(parsed.variant)!;}
    throw Error('媒体播放列表层级无效');
  }
  private async prepare(job:Job):Promise<GameMediaEntry>{
    if(!this.workAllowed())throw Error('wallpaper-power-paused');
    const workEpoch=this.workEpoch;
    const {signal}=this.powerWork;
    if(job.consumer==='steam'&&!steamMediaDemandCurrent(job.demand))throw Error('steam-demand-withdrawn');
    const checkPower=()=>{if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');};
    const invokeOwned=<T=unknown>(command:string,args:Record<string,unknown>,options?:{timeoutMs:number})=>{checkPower();if(job.consumer==='steam'&&(!this.port.enabled()||!steamMediaDemandCurrent(job.demand)))throw Error('steam-demand-withdrawn');return this.port.invoke<T>(command,{...args,...steamMediaDemandFields(job.demand)},{...options,signal});};
    let lease=await invokeOwned<GameMediaEntry>('gameMedia.begin',{...job.target,demandConsumer:job.consumer,...steamMediaDemandFields(job.demand)},{timeoutMs:12000});
    if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');
    if(!lease.acquired){if(lease.leaseUntil)this.armRetry(lease.leaseUntil);return this.lookup(job.target,job.demand);}
    try{
      if(job.target.appId<=0){
        if(!job.target.executable)throw Error('游戏文件身份缺失');
        const identity=await invokeOwned<{appId?:number;name?:string;verified?:boolean}>('gameMedia.identify',{executable:job.target.executable,...steamMediaDemandFields(job.demand)},{timeoutMs:100000});
        if(!identity.verified||!Number.isSafeInteger(identity.appId)||Number(identity.appId)<=0)throw Error('游戏文件身份尚未确认');
        job.target={...job.target,appId:identity.appId!,name:identity.name||job.target.name,verified:true};
        const bound=await invokeOwned<GameMediaEntry>('gameMedia.bind',{...job.target,...steamMediaDemandFields(job.demand)});
        checkPower();if(bound.taskId===lease.taskId)lease=bound;
        else {lease=await invokeOwned<GameMediaEntry>('gameMedia.begin',{...job.target,demandConsumer:job.consumer,...steamMediaDemandFields(job.demand)},{timeoutMs:12000});checkPower();if(!lease.acquired){if(lease.leaseUntil)this.armRetry(lease.leaseUntil);return this.lookup(job.target,job.demand);}}
      }
      if(job.consumer==='steam'&&!this.port.enabled()){await invokeOwned('gameMedia.finish',{...lease,...steamMediaDemandFields(job.demand)});return this.lookup(job.target,job.demand);}
      if(job.target.legacyTarget&&lease.taskSlots?.includes('dynamic')){
        const migrated=await invokeOwned<GameMediaEntry>('gameMedia.legacy',{...lease,...job.target,...steamMediaDemandFields(job.demand),...job.target.legacyTarget}).catch(error=>{if(/stale|conflict/.test(String(error)))throw error;return null;});
        checkPower();if(migrated?.dynamicReady){lease=migrated;this.changed('asset',lease.key);if(!lease.taskSlots?.length){await invokeOwned('gameMedia.finish',{...lease,...steamMediaDemandFields(job.demand)});checkPower();return this.lookup(job.target,job.demand);}}
      }
      if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');
      const media=await this.port.candidates(job.target);
      if(!this.workCurrent(workEpoch))throw Error('wallpaper-power-paused');
      if(job.consumer==='steam'&&!this.port.enabled()){await invokeOwned('gameMedia.finish',{...lease,...steamMediaDemandFields(job.demand)});return this.lookup(job.target,job.demand);}const needed=lease.taskSlots||['dynamic'];let last:unknown;const failures:unknown[]=[];
      if(needed.includes('static')&&media.poster)try{lease=await invokeOwned<GameMediaEntry>('gameMedia.install',{...lease,...job.target,...steamMediaDemandFields(job.demand),source:media.poster,name:media.name},{timeoutMs:135000});checkPower();this.changed('asset',lease.key);}catch(error){if(/stale|conflict/.test(String(error)))throw error;failures.push(error);}
      const video=media.urls.filter(url=>/\.(?:webm|mp4|m3u8)(?:\?|$)/i.test(url));
      let published=!needed.includes('dynamic');
      if(needed.includes('dynamic')&&(job.consumer!=='steam'||this.port.enabled()))for(const source of video.slice(0,4))try{
        const plan=await this.plan(source,workEpoch);lease=await invokeOwned<GameMediaEntry>('gameMedia.install',{...lease,...job.target,...steamMediaDemandFields(job.demand),...plan,videoQualityPolicy:3,name:media.name},{timeoutMs:135000});checkPower();published=true;this.changed('asset',lease.key);break;
      }catch(error){last=error;if(/stale|conflict/.test(String(error)))throw error;}
      if(!published&&video.length&&(job.consumer!=='steam'||this.port.enabled()))failures.push(last||Error('动态素材准备失败'));
      if(failures.length)throw failures[0];
      await invokeOwned('gameMedia.finish',{...lease,...steamMediaDemandFields(job.demand),videoUnavailable:video.length===0,staticUnavailable:!media.poster});
      checkPower();
      this.changed('asset',lease.key);return this.lookup(job.target,job.demand);
    }catch(error){
      checkPower();if(job.consumer==='steam'&&!steamMediaDemandCurrent(job.demand))throw Error('steam-demand-withdrawn');
      const failed=await invokeOwned<GameMediaEntry>('gameMedia.fail',{...lease,...steamMediaDemandFields(job.demand),reason:String(error instanceof Error?error.message:error)}).catch(()=>lease);
      checkPower();if(failed.retryAt)this.armRetry(failed.retryAt);else if(/stale|conflict/.test(String(error))&&(job.consumer!=='steam'||this.port.enabled()))this.armRetry(this.port.now()+1000);
      this.changed('asset',failed.key);return this.lookup(job.target,job.demand).catch(reason=>{if(job.consumer==='steam')throw reason;return failed;});
    }
  }
  private async drain(){if(this.runningEpoch!==null||this.stopped||!this.workAllowed())return;const epoch=this.runningEpoch=this.workEpoch;try{while(this.queue.length&&this.workCurrent(epoch)){
    const index=this.queue.findIndex(key=>{const job=this.jobs.get(key);return !job||job.consumer==='steam'&&!this.port.enabled()||!this.preparationState.storageBlocked&&(!this.preparationState.backgroundBlocked||job.priority||job.consumer!=='steam');});
    if(index<0)break;const key=this.queue.splice(index,1)[0],job=this.jobs.get(key);if(!job)continue;
    try{job.resolve(await (job.consumer==='steam'&&!this.port.enabled()?this.lookup(job.target,job.demand):this.prepare(job)));}catch(error){job.reject(error);}finally{if(this.jobs.get(key)===job)this.jobs.delete(key);}
  }}finally{if(this.runningEpoch===epoch){this.runningEpoch=null;this.changed('demand');}}}
  async supplement(includeInstalled=true,manual=false,inventoryRequestId='',originMask=0,signal?:AbortSignal){if((!this.port.backgroundEnabled()&&!manual)||this.stopped||!this.workAllowed()||signal?.aborted)return;
    signal??=this.powerWork.signal;
    const workEpoch=this.workEpoch;
    const readTargets=async(read:()=>Promise<WallpaperTarget[]>)=>{try{const rows=await read();return Array.isArray(rows)?rows:null;}catch{return null;}};
    const [installed,library]=await Promise.all([includeInstalled?readTargets(()=>this.port.installed(signal)):Promise.resolve([]),readTargets(()=>this.port.invoke<WallpaperTarget[]>('gameMedia.inventory',originMask?{originMask}:undefined,{signal,timeoutMs:12000}))]);
    if((!this.port.backgroundEnabled()&&!manual)||!this.workCurrent(workEpoch)||signal?.aborted)return;
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
    for(const target of installed||[])add({...target,sourceTier:1,catalogSource:'steam-native'});
    for(const target of library||[])add({...target,sourceTier:2,catalogSource:target.catalogSource==='steam-shortcut'?'steam-shortcut':'ymcc-confirmed'});

    const readFailed=installed===null||library===null;
    if(readFailed&&!targets.size)return;
    // Empty successful reads may close the batch; partial or failed reads cannot confirm it.
    const confirmed=manual&&includeInstalled&&!readFailed?inventoryRequestId:'';
    // The library process owns supplementation; display consumers keep only
    // explicit foreground demand. Both paths share native revision leases.
    await this.port.invoke('gameMedia.background',{targets:[...targets.values()],...(confirmed?{inventoryRequestId:confirmed}:{})},{signal,timeoutMs:30000});
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
  inspect(){return {running:this.runningEpoch!==null,queued:this.queue.length,tasks:this.jobs.size,...this.preparationState};}
  stop(){this.stopped=true;this.runningEpoch=null;this.powerWork.abort();this.invalidateLookups();this.nativeAdmissions.clear();if(this.nextRetry!==null)this.port.cancel(this.nextRetry);for(const job of this.jobs.values())job.reject(Error('素材服务已停止'));this.jobs.clear();this.queue=[];this.listeners.clear();}
}
// One deferred scan per burst; a change arriving during a read is never lost.
const PASSIVE_INVENTORY_GAP_MS=10000;
export class MediaInventoryScan {
  private timer:ReturnType<typeof setTimeout>|null=null;private dueAt=Infinity;private lastStarted=-Infinity;
  private running:AbortController|null=null;private pending=false;private full=false;private urgent=false;private stopped=false;private originMask=0;
  constructor(private read:(includeInstalled:boolean,originMask:number,signal:AbortSignal)=>Promise<void>,private enabled:()=>boolean,private clock={schedule:(...args:Parameters<typeof setTimeout>)=>globalThis.setTimeout(...args),cancel:(timer:ReturnType<typeof setTimeout>)=>globalThis.clearTimeout(timer)}){}
  request(includeInstalled=false,passive=false,originMask=0){
    if(this.stopped||!this.enabled())return;
    this.pending=true;this.full ||= includeInstalled;this.urgent ||= !passive;this.originMask |= originMask;
    this.schedulePending();
  }
  private schedulePending(){
    if(!this.pending||this.running||this.stopped||!this.enabled())return;
    const now=performance.now();
    const delay=Math.max(this.full?0:250,this.urgent?0:this.lastStarted+PASSIVE_INVENTORY_GAP_MS-now);
    const dueAt=now+delay;
    if(this.timer!==null){if(dueAt>=this.dueAt)return;this.clock.cancel(this.timer);}
    const timer=this.clock.schedule(()=>{if(this.timer!==timer)return;this.timer=null;this.dueAt=Infinity;void this.run();},delay);
    this.timer=timer;this.dueAt=dueAt;
  }
  private async run(){
    const full=this.full,originMask=this.originMask;this.pending=this.full=this.urgent=false;this.originMask=0;
    if(this.stopped||!this.enabled())return;
    const work=this.running=new AbortController();this.lastStarted=performance.now();
    try{await this.read(full,originMask,work.signal);}catch{}finally{if(this.running===work){this.running=null;this.schedulePending();}}
  }
  retirePending(){this.pending=this.full=this.urgent=false;this.originMask=0;const work=this.running;this.running=null;work?.abort();if(this.timer!==null)this.clock.cancel(this.timer);this.timer=null;this.dueAt=Infinity;}
  stop(){this.stopped=true;this.retirePending();}
}
// A newly confirmed EXE may reuse an already-cached Steam asset. Notify its
// previous selection key exactly once; unrelated progress must remain inert.
export function reconcileMediaAliases(known:Map<string,string>,aliases:Record<string,string>,notify:(previous:string,key:string)=>void){for(const [previous,key] of Object.entries(aliases))if(known.get(previous)!==key){known.set(previous,key);notify(previous,key);}}
export const sharedGameMedia=new SharedGameMedia();
export const steamWallpaperEnabled=()=>getUiSetting('steamDynamicWallpaperEnabled');
export async function setSteamWallpaperEnabled(enabled:boolean){await setUiSettings({steamDynamicWallpaperEnabled:enabled});sharedGameMedia.changed('settings');}
export function startSharedGameMedia(){
  let disposed=false,lastEnabled=false,admissionSuspended=true,refreshAgain=false;
  let watchStarted=false,watchConfirmed=false,watchAttempts=0;
  let watchRequest:AbortController|null=null,watchRetry:ReturnType<typeof setTimeout>|null=null;
  const cancelWatchRetry=()=>{if(watchRetry!==null)clearTimeout(watchRetry);watchRetry=null;};
  const needWatch=()=>watchStarted&&!disposed&&!admissionSuspended&&!watchConfirmed&&!watchRequest&&watchAttempts<2;
  const registerWatch=()=>{
    if(!needWatch())return;
    const request=watchRequest=new AbortController();++watchAttempts;
    void invoke<boolean>('gameMedia.watch',{enabled:true},{timeoutMs:3000,signal:request.signal}).then(ack=>{
      // An acknowledged passive watcher stays owned across sleep.
      if(!disposed&&watchRequest===request&&ack===true)watchConfirmed=true;
    }).catch(()=>{}).finally(()=>{
      if(watchRequest!==request)return;
      watchRequest=null;reconcileWatch();
    });
  };
  const reconcileWatch=()=>{
    if(!needWatch()||watchRetry!==null)return;
    if(watchAttempts===0){registerWatch();return;}
    const timer=setTimeout(()=>{
      if(watchRetry!==timer)return;
      watchRetry=null;registerWatch();
    },500);watchRetry=timer;
  };
  let refreshWork:AbortController|null=null;
  let recoveryWork:AbortController|null=null;
  const retireRecovery=()=>{const work=recoveryWork;recoveryWork=null;work?.abort();};
  const retireRefresh=()=>{const work=refreshWork;refreshWork=null;refreshAgain=false;work?.abort();retireRecovery();};
  sharedGameMedia.sharedPowerChanged(false,false);
  const revisions=new Map<string,{binding:number;playback?:string}>();const aliases=new Map<string,string>();
  const stopDownloads=gameMediaDownloads.start();
  const manualAllowed=()=>Number(gameMediaDownloads.state.manualUntil||0)>Date.now();
  const scans=new MediaInventoryScan((full,origins,signal)=>sharedGameMedia.supplement(full,manualAllowed(),gameMediaDownloads.state.inventoryRequestId||'',origins,signal),()=>!disposed&&!admissionSuspended&&uiSettingsLoaded()&&!wallpaperPowerBlocked()&&(backgroundDownloadEnabled()||manualAllowed()));
  const inventoryReadyAt=Date.now()+30000;let inventoryBootTimer:ReturnType<typeof setTimeout>|null=null,inventoryFull=false,bootOrigins=0;
  const scan=(origins:number,includeInstalled=true,passive=false)=>{
    if(admissionSuspended||wallpaperPowerBlocked())return;
    if(Date.now()>=inventoryReadyAt){scans.request(includeInstalled,passive,origins);return;}
    inventoryFull ||= includeInstalled;bootOrigins |= origins;
    if(inventoryBootTimer===null)inventoryBootTimer=setTimeout(()=>{
      inventoryBootTimer=null;
      if(!disposed)scans.request(inventoryFull,false,bootOrigins);
      inventoryFull=false;bootOrigins=0;
    },Math.max(0,inventoryReadyAt-Date.now()));
  };
  const policy=new GameMediaPreparationPolicy({read:signal=>wallpaperPowerBlocked()?Promise.resolve({blockedReason:wallpaperPowerReason()}):invoke('gameMedia.preparationPolicy',{}, {timeoutMs:5000,signal}),apply:state=>{const value=wallpaperPowerBlocked()?{backgroundBlocked:true,storageBlocked:true,reason:wallpaperPowerReason(),playback30Idle:false}:state;sharedGameMedia.setPreparationState(value);gameMediaDownloads.policy(value.reason,value.playback30Idle);},queued:()=>sharedGameMedia.inspect().queued+Number(gameMediaDownloads.state.remaining||0)+Number(gameMediaDownloads.state.catalogPending===true),enabled:()=>!wallpaperPowerBlocked()&&(backgroundDownloadEnabled()||Number(gameMediaDownloads.state.manualUntil||0)>Date.now()),recheck:async()=>{await Promise.allSettled([refreshPolicyGameStrict(),gameMediaDownloads.recheck()]);if(!disposed)await refresh();}});
  type AdmissionGame=Parameters<typeof resolveSteamWallpaperIdentity>[0];
  let admissionGame:AdmissionGame|null=null,admissionKey='';
  type AdmissionObservation={key:string;observedAt:number};
  type AdmissionRefresh={observation?:AdmissionObservation;awaitingReadiness:boolean};
  let admissionRead:{controller:AbortController;observation:AdmissionObservation}|null=null;
  let admissionRefresh:AdmissionRefresh|null=null;
  const retireAdmissionRead=()=>{
    const old=admissionRead;admissionRead=null;old?.controller.abort();
  };
  let steamReading:AbortController|null=null;
  let steamPending=false,steamImmediate=false,steamReadEpoch=0;
  let steamContext:{pid:number;birth:string;account:number;document:string;mode:SteamUiState['mode']}|undefined;
  let steamContextCurrent=false,steamNeedsPowerRecovery=false;
  let steamNoticeTimer:ReturnType<typeof setTimeout>|null=null,steamLastRead=-Infinity;
  const steamInventoryAllowed=()=>!disposed&&!admissionSuspended&&uiSettingsLoaded()&&!wallpaperPowerBlocked()&&(backgroundDownloadEnabled()||manualAllowed());
  const clearSteamNoticeTimer=()=>{if(steamNoticeTimer!==null){clearTimeout(steamNoticeTimer);steamNoticeTimer=null;}};
  const retireSteamInventory=()=>{
    ++steamReadEpoch;steamPending=steamImmediate=false;clearSteamNoticeTimer();
    const work=steamReading;steamReading=null;work?.abort();
  };
  const drainSteamInventory=()=>{
    if(!steamInventoryAllowed()){retireSteamInventory();return;}
    if(steamReading||!steamPending)return;
    if(!steamImmediate&&steamNoticeTimer!==null)return;
    const delay=steamLastRead+STEAM_AUTOMATIC_READ_GAP_MS-performance.now();
    if(!steamImmediate&&delay>0){
      if(steamNoticeTimer===null)steamNoticeTimer=setTimeout(()=>{steamNoticeTimer=null;drainSteamInventory();},delay);
      return;
    }
    clearSteamNoticeTimer();void readSteamInventory();
  };
  const requestSteamInventory=(immediate=true)=>{
    if(!steamInventoryAllowed())return;
    steamPending=true;steamImmediate ||= immediate;++steamReadEpoch;
    drainSteamInventory();
  };
  const applySteamInventoryState=(state:SteamUiState)=>{
    if(!state.known||!state.running||!state.pid||state.birth==='0'||!['desktop','bigpicture'].includes(state.mode)){
      if(steamContextCurrent){steamContextCurrent=false;sharedGameMedia.invalidateLookups();}return;
    }
    const account=Number.isSafeInteger(state.observedAccount)&&Number(state.observedAccount)>0&&Number(state.observedAccount)<=0xffffffff?state.observedAccount:0;
    const document=typeof state.modeEpoch==='string'&&state.modeEpoch.length<=96?state.modeEpoch:'';
    const previous=steamContext,processChanged=!previous||state.pid!==previous.pid||state.birth!==previous.birth;
    let origins=processChanged?InventoryOrigin.SteamProcess:0;
    if(previous){
      if(account!==previous.account)origins|=InventoryOrigin.SteamAccount;
      if(document!==previous.document)origins|=InventoryOrigin.SteamDocument;
      if(state.mode!==previous.mode)origins|=InventoryOrigin.SteamMode;
      if(!steamContextCurrent)origins|=InventoryOrigin.SteamRevalidated;
    }
    if(steamNeedsPowerRecovery)origins|=InventoryOrigin.PowerRecovery;
    if(!origins)return;
    steamContext={pid:state.pid,birth:state.birth,account,document,mode:state.mode};steamContextCurrent=true;steamNeedsPowerRecovery=false;
    sharedGameMedia.invalidateLookups();scan(origins,processChanged||!!(origins&InventoryOrigin.PowerRecovery)&&manualAllowed());
  };
  async function readSteamInventory(){
    const work=new AbortController();
    steamReading=work;steamPending=steamImmediate=false;steamLastRead=performance.now();const epoch=steamReadEpoch;
    try{
      // Reuse the native cache when available. A cache miss may still probe;
      // ordinary notifications share one pending read and one deadline.
      const state=await steamUi.read({automatic:true,signal:work.signal,shareNotification:true});
      if(!steamInventoryAllowed()||epoch!==steamReadEpoch)return;
      applySteamInventoryState(state);
    }catch{
      // A temporary Unknown is not logout. Retire the retained lookup and wait
      // for the shared producer's next signal; no private recovery timer.
      if(steamInventoryAllowed()&&epoch===steamReadEpoch&&steamContextCurrent){steamContextCurrent=false;sharedGameMedia.invalidateLookups();}
    }finally{
      if(steamReading===work){steamReading=null;if(steamPending)drainSteamInventory();}
    }
  }
  policy.suspend();
  const reconcileGameAdmission=(previous?:AdmissionObservation)=>{
    const game=admissionGame,enabled=!disposed&&!admissionSuspended&&uiSettingsLoaded()&&backgroundDownloadEnabled()&&!wallpaperPowerBlocked();
    const key=enabled&&game?JSON.stringify([game.pid,game.processCreated,game.path.toLowerCase(),game.identityGeneration,game.source,game.whitelistRule]):'';
    if(key===admissionKey)return;admissionKey=key;
    retireAdmissionRead();if(!game||!key)return;
    // Selection observation owns recency. A slow local lookup cannot make an
    // exited/replaced game newer or survive OFF, suspend, or an ABA handoff.
    const observedAt=previous?.key===key?previous.observedAt:Date.now();
    // A new scope must not join the unresolved path lookup from before sleep.
    const read=admissionRead={controller:new AbortController(),observation:{key,observedAt}};
    void resolveSteamWallpaperIdentity({...game},read.controller.signal).then(target=>{
      if(admissionRead!==read)return;
      admissionRead=null;
      if(disposed||admissionSuspended||!uiSettingsLoaded()||!backgroundDownloadEnabled()||wallpaperPowerBlocked()||!target.verified)return;
      return invoke('gameMedia.background',{targets:[{...target,lastPlayed:observedAt}],replyStatus:false});
    }).catch(()=>{if(admissionRead===read)admissionRead=null;});
  };
  const confirmGameAdmission=(refresh:AdmissionRefresh,game:AdmissionGame|null)=>{
    if(disposed||admissionSuspended||admissionRefresh!==refresh)return;
    admissionRefresh=null;admissionGame=game?{...game}:null;reconcileGameAdmission(refresh.observation);
  };
  const continueGameAdmission=()=>{
    const refresh=admissionRefresh;
    if(refresh?.awaitingReadiness&&isPolicyGameCurrent())confirmGameAdmission(refresh,getPolicyGame());
  };
  const refreshGameAdmission=(firstReady:boolean,observation?:AdmissionObservation)=>{
    const refresh:AdmissionRefresh={observation,awaitingReadiness:false};
    admissionRefresh=refresh;
    retireAdmissionRead();admissionGame=null;admissionKey='';
    // The shared detector can confirm an unchanged game without publishing a selection event.
    void refreshPolicyGameStrict(firstReady?'observe':undefined).then(game=>confirmGameAdmission(refresh,game)).catch(()=>{
      if(admissionRefresh!==refresh)return;
      refresh.awaitingReadiness=true;continueGameAdmission();
    });
  };
  const gamePolicy=subscribePolicyGameStatus(game=>{
    const observation=admissionRefresh?.observation;
    admissionRefresh=null;admissionGame=game?{...game}:null;policy.game(!!game);reconcileGameAdmission(observation);
  });
  const gameReadiness=subscribePolicyGameReadiness(ready=>{if(ready)continueGameAdmission();});
  const powerPolicy=on('power.sourceChanged',()=>policy.request());
  const saverPolicy=on('gameMedia.preparationPolicyChanged',()=>policy.request());
  let powerPhase='',powerGeneration=0,powerSuspendSerial=0,hasAdmittedPower=false,powerKnown:boolean|undefined,powerAdmitted:boolean|undefined;
  let powerNotificationSequence:number|undefined;
  const lifecyclePolicy=onSharedPowerLifecycle(state=>{
    const admitted=isSharedPowerLifecycleAdmitted(state);
    const serial=state.suspendSerial??powerSuspendSerial;
    const notice=state.observedNotificationSequence??powerNotificationSequence;
    if(disposed||serial<powerSuspendSerial||(notice!==undefined&&powerNotificationSequence!==undefined&&notice<powerNotificationSequence))return;
    const noticeChanged=notice!==powerNotificationSequence;
    powerNotificationSequence=notice;
    if(powerPhase===state.phase&&powerGeneration===state.generation&&powerKnown===state.known&&powerAdmitted===admitted&&serial===powerSuspendSerial){
      // A notification can retire in-flight observations without changing Ready.
      if(admitted&&noticeChanged){
        if(admissionRead||admissionRefresh)refreshGameAdmission(false,admissionRead?.observation??admissionRefresh?.observation);
        policy.notificationAdvanced();
        if(steamReading){retireSteamInventory();requestSteamInventory(false);}
        if(refreshWork||recoveryWork){retireRefresh();void refresh();}
      }
      return;
    }
    const scopeChanged=powerGeneration>0&&(powerGeneration!==state.generation||serial!==powerSuspendSerial);
    const firstReady=admitted&&!hasAdmittedPower;
    hasAdmittedPower ||= admitted;
    powerPhase=state.phase;powerGeneration=state.generation;powerSuspendSerial=serial;powerKnown=state.known;powerAdmitted=admitted;
    admissionSuspended=!admitted;
    if(!admitted||scopeChanged)cancelWatchRetry();
    if(scopeChanged)watchAttempts=0;
    if(admitted)reconcileWatch();
    if(!admitted||scopeChanged){
      retireSteamInventory();retireRefresh();scans.retirePending();
      if(inventoryBootTimer!==null){clearTimeout(inventoryBootTimer);inventoryBootTimer=null;}
      inventoryFull=false;bootOrigins=0;steamContextCurrent=false;steamNeedsPowerRecovery=true;
    }
    sharedGameMedia.sharedPowerChanged(admitted,scopeChanged);
    if(!admitted){admissionRefresh=null;reconcileGameAdmission();policy.suspend();return;}
    if(firstReady&&lastEnabled)scan(InventoryOrigin.Enabled);
    refreshGameAdmission(firstReady);policy.resume();if(lastEnabled||manualAllowed())requestSteamInventory();void refresh();
  },{observeSuspendSerial:true,observeNotificationSequence:true});
  const demandPolicy=sharedGameMedia.subscribe(reason=>{if(reason==='demand')policy.demand();else if(reason==='retry')policy.request();});
  const completionPolicy=gameMediaDownloads.watchCompleted(()=>{void refresh();});
  const downloadPolicy=gameMediaDownloads.subscribe(()=>policy.demand());
  policy.request();
  async function refresh(){if(disposed||admissionSuspended||wallpaperPowerBlocked())return;const powerEpoch=captureWallpaperPower();if(refreshWork){refreshAgain=true;return;}const work=new AbortController();refreshWork=work;
    try{const state=await invoke<{entries:Record<string,GameMediaEntry>;aliases?:Record<string,string>}>('gameMedia.snapshot',{wallpaperPlayback:true}, {signal:work.signal,timeoutMs:5000});if(disposed||refreshWork!==work||!wallpaperPowerCurrent(powerEpoch))return;
      retireRecovery();
      const recovery:SnapshotRecovery[]=[];
      reconcileMediaAliases(aliases,state.aliases||{},(previous,key)=>{revisions.delete(previous);sharedGameMedia.changed('asset',previous);window.dispatchEvent(new CustomEvent('game-media:changed',{detail:{key:previous,canonicalKey:key}}));});
      for(const entry of Object.values(state.entries)){
        const old=revisions.get(entry.key),binding=entry.bindingRevision,playback=entry.wallpaperPlaybackStamp;
        if(old?.binding!==binding||old?.playback!==playback){
          revisions.set(entry.key,{binding,playback});
          const playbackOnly=old?.binding===binding;
          if(!playbackOnly||playback){
            const ready=entry.ready===true&&!entry.deleted;
            sharedGameMedia.changed(playbackOnly?'wallpaper-playback':'asset',entry.key);
            window.dispatchEvent(new CustomEvent('game-media:changed',{detail:{key:entry.key,revision:binding,ready,executable:ready?entry.executable:undefined,playbackOnly}}));
          }
        }
        const requested=entry.refreshRequested===true;
        const consumer=requested?'library':entry.demandConsumer==='steam'?'steam':entry.demandConsumer==='ymcc'?'ymcc':entry.demandConsumer==='library'||!entry.demandConsumer?'library':null;
        const owned=!wallpaperPowerBlocked()&&(consumer==='steam'?steamWallpaperEnabled():consumer==='ymcc'?getUiSetting('dynamicBackgroundEnabled'):consumer==='library'&&(requested||backgroundDownloadEnabled()||manualAllowed()));
        const pending=entry.status==='pending',partial=entry.ready&&!gameMediaPairComplete(entry)&&Number(entry.leaseUntil||0)<=Date.now(),recoverable=pending||entry.status==='error'||partial;const deadline=Number(pending?entry.leaseUntil:entry.retryAt)||0;
        if(owned&&recoverable&&!entry.deleted)sharedGameMedia.retryWhen(deadline);
        const retry=owned&&recoverable&&deadline<=Date.now();
        if(consumer&&consumer!=='steam'&&(requested||retry)&&(entry.appId||entry.executable)&&!entry.deleted){
          recovery.push({target:{appId:entry.appId||0,name:entry.name||'',executable:entry.executable,verified:entry.identityVerified===true},consumer,
            current:()=>!disposed&&!admissionSuspended&&wallpaperPowerCurrent(powerEpoch)&&(consumer==='ymcc'?getUiSetting('dynamicBackgroundEnabled'):requested||backgroundDownloadEnabled()||manualAllowed())});
        }
      }
      if(recovery.length){recoveryWork=work;sharedGameMedia.restoreSnapshot(recovery,work.signal,()=>{if(recoveryWork===work)recoveryWork=null;});}
    }catch{}finally{if(refreshWork===work){refreshWork=null;if(refreshAgain){refreshAgain=false;void refresh();}}}
  }
  const off=on('gameMedia.changed',()=>{retireRecovery();sharedGameMedia.invalidateLookups();policy.request();void refresh();});
  const inventory=on('gameMedia.inventoryChanged',()=>{sharedGameMedia.invalidateLookups();scan(InventoryOrigin.LibraryNotice,true,true);});
  let inventoryRequest='';const rescanRequest=gameMediaDownloads.subscribe(()=>{const id=gameMediaDownloads.state.inventoryRequestId;if(id&&id!==inventoryRequest){inventoryRequest=id;scan(InventoryOrigin.Manual);}});
  const settings=()=>{const enabled=uiSettingsLoaded()&&backgroundDownloadEnabled()&&!wallpaperPowerBlocked();reconcileGameAdmission();sharedGameMedia.reconcilePreparation();sharedGameMedia.changed('settings');if(enabled&&!lastEnabled){scan(InventoryOrigin.Enabled);requestSteamInventory();}else if(!enabled){if(steamInventoryAllowed())++steamReadEpoch;else retireSteamInventory();}lastEnabled=enabled;};
  const powerChanged=subscribeWallpaperPowerChanged(blocked=>{
    if(blocked){retireSteamInventory();retireRefresh();}
    if(blocked&&inventoryBootTimer!==null){clearTimeout(inventoryBootTimer);inventoryBootTimer=null;}
    sharedGameMedia.reconcilePreparation();reconcileGameAdmission();
    // Keep admission closed until the existing policy owner confirms game/storage state.
    gameMediaDownloads.policy(blocked?wallpaperPowerReason():'capacity-unavailable');
    sharedGameMedia.changed('settings');policy.demand();
    if(!blocked){scan(InventoryOrigin.PowerRecovery);requestSteamInventory();policy.request();void refresh();}
  });
  window.addEventListener('ui-settings:changed',settings);window.addEventListener('ui-settings:loaded',settings);
  const steam=steamUi.subscribe(published=>{
    if(!published){requestSteamInventory(false);return;}
    if(!steamInventoryAllowed())return;
    retireSteamInventory();
    applySteamInventoryState(published);
  },{publishedMediaState:true});
  const retry=sharedGameMedia.subscribe(reason=>{if(reason==='retry')void refresh();else if(reason==='inventory')scan(InventoryOrigin.PageHint,false,true);});
  watchStarted=true;reconcileWatch();settings();
  return()=>{
    if(disposed)return;disposed=true;admissionRefresh=null;gameReadiness();cancelWatchRetry();watchRequest?.abort();watchRequest=null;
    retireAdmissionRead();retireSteamInventory();retireRefresh();powerChanged();if(inventoryBootTimer!==null)clearTimeout(inventoryBootTimer);stopDownloads();policy.stop();gamePolicy();powerPolicy();saverPolicy();lifecyclePolicy();demandPolicy();downloadPolicy();completionPolicy();rescanRequest();scans.stop();off();inventory();steam();retry();window.removeEventListener('ui-settings:changed',settings);window.removeEventListener('ui-settings:loaded',settings);void invoke('gameMedia.watch',{enabled:false},{timeoutMs:3000}).catch(()=>{});sharedGameMedia.stop();
  };
}
