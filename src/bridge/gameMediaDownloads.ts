import {wallpaperPowerBlocked,wallpaperPowerReason,subscribeWallpaperPowerChanged} from './wallpaperPowerPolicy';
import {invoke,on} from './ipc';
import {onSharedPowerLifecycle,isSharedPowerLifecycleAdmitted} from './powerLifecycleState';
import {getUiSetting,setUiSettings,uiSettingsLoaded} from './uiSettings';
export interface MediaDownloadState {checkpointReadable?:boolean;recoveryRequired?:boolean;startupProtectionUntil?:number;autoStopped?:number;pendingReason?:string;nextDueAt?:number;inventoryAwaitUntil?:number;inventoryReadyId?:string;manualTotal?:number;manualCompleted?:number;manualUntil?:number;manualState?:string;inventoryRequestId?:string;catalogPending?:boolean;heartbeatAt?:number;updatedAt?:number;catalogStatus?:string;catalogError?:string;catalogRetryAt?:number;catalogCount?:number;sourceCounts?:{popular:number;steamNative:number;steamShortcuts?:number;identifying?:number;ymccConfirmed:number;other:number};statusRevision?:number;activeCount?:number;parallelSlots?:number;resumedBytes?:number;resourcePauseReason?:string;systemCpuPct?:number;cpuThreshold?:number;enabled?:boolean;phase?:string;pauseReason?:string;reason?:string;current?:{name?:string;appId?:number};bytes?:number;totalBytes?:number;completed?:number;remaining?:number;failed?:number;total?:number;unavailable?:number;workerPid?:number;}
function downloadReadUnavailable(state:MediaDownloadState){return state.checkpointReadable===false||state.phase==='deferred'&&['queue-unreadable','media-download-checkpoint-upgrade-required','host-unavailable'].includes(state.reason||'');}
export function downloadDescription(state:MediaDownloadState){
  const label=currentDownloadDescription(state);
  return label+(!downloadReadUnavailable(state)&&state.checkpointReadable===true&&state.recoveryRequired===true?' · 部分历史记录待确认':'');
}
function currentDownloadDescription(state:MediaDownloadState){
  if(downloadReadUnavailable(state))return downloadInterruptionReason(state.reason||'')||'下载队列暂不可读，稍后恢复';
  const manual=Number(state.manualUntil||0)>Date.now();
  if(state.enabled===false&&!manual){const reason=downloadInterruptionReason(state.reason||'');return '后台下载已关闭'+(reason?' · '+reason:'');}
  if(!manual&&state.total&&state.remaining===0&&!state.workerPid&&!state.activeCount)return `本轮已处理 · ${state.completed||0} / ${state.total}${state.unavailable?` · ${state.unavailable} 个已跳过，不影响其他游戏`:''}`;
  const pauses:Record<string,string>={'wallpaper-on-battery':'离电关闭已生效，壁纸缓存和下载已暂停','wallpaper-power-unknown':'正在确认真实电源，壁纸任务已暂停','game-running':'游戏运行中，后台下载已暂停','battery-saver':'节电模式下暂停后台下载','low-battery':'低电量，后台下载已暂停','capacity-unavailable':'正在检查下载条件','media-power-transition':'电源切换中，后台下载已暂停','media-storage-unavailable':'存储暂不可用，等待恢复后继续下载','media-disk-space-low':'磁盘余量不足，后台下载已暂停','media-cache-budget-full':'壁纸缓存已满，后台下载已暂停'};
  const resource:Record<string,string>={'startup-protection':'启动保护：前 30 秒不自动下载','on-battery':'电池模式，自动下载已停止','power-unknown':'正在确认供电，暂不自动下载','cpu-unknown':'正在检测 CPU 负载','cpu-busy':`CPU 占用超过 ${state.cpuThreshold||15}%，自动下载已暂停`,'cpu-recovering':'CPU 负载下降，稳定后继续下载'};
  const resourcePause=manual&&['cpu-busy','cpu-unknown','cpu-recovering'].includes(state.resourcePauseReason||'')?'':state.resourcePauseReason;
  if(resourcePause)return resource[resourcePause]||'后台下载等待恢复';
  if(state.pauseReason&&!(manual&&state.pauseReason==='game-running'))return (pauses[state.pauseReason]||'后台下载已暂停，条件恢复后继续')+(/video-selection-expired/.test(state.reason||'')?' · '+downloadInterruptionReason(state.reason||''):'');
  const count=manual&&(state.manualTotal||0)>0?`${state.manualCompleted||0} / ${state.manualTotal}`:`${state.completed||0} / ${state.total||0}`;
  const waiting=Number(state.nextDueAt||0)>Date.now()&&!state.workerPid&&!state.activeCount;
  if(waiting){const reason=downloadInterruptionReason(state.pendingReason||state.reason||'');const seconds=Math.max(1,Math.ceil((Number(state.nextDueAt)-Date.now())/1000));return `${manual?'手动补齐':'后台下载'}等待重试 · ${reason||'本轮剩余任务暂不可用'} · 约 ${seconds} 秒后复查 · ${count}`;}
  if(manual){const preparing=Number(state.inventoryAwaitUntil||0)>Date.now()&&state.inventoryReadyId!==state.inventoryRequestId;const detail=preparing?'正在获取游戏清单':state.workerPid?'下载进程正在准备':'正在启动下载';return `手动补齐中 · ${state.current?.name||detail}${preparing?'':` · ${count}`}`;}
  if(state.phase==='identifying')return `正在识别已加入 Steam 的非 Steam 游戏 · ${state.current?.name||''} · ${count}`;
  if(['starting','resolving','downloading','publishing'].includes(state.phase||''))return `${(state.activeCount||0)>1?`正在下载 ${state.activeCount} 个`:state.current?.name||'正在准备壁纸'}${state.resumedBytes?' · 断点续传':''} · ${count}${state.totalBytes?` · ${Math.min(100,Math.floor((state.bytes||0)*100/state.totalBytes))}%`:''}`;
  if(state.phase==='deferred'||state.phase==='paused'){const reason=downloadInterruptionReason(state.pendingReason||state.reason||'');return `${reason||'后台下载等待恢复'} · ${count}`;}
  return state.total?`本轮已处理 · ${count}${state.unavailable?` · ${state.unavailable} 个已跳过，不影响其他游戏`: ''}`:'等待补齐动态壁纸';
}
export function downloadInterruptionReason(reason:string){
  if(reason==='media-steam-facts-unavailable')return '正在等待 Steam 状态确认，稍后自动重试';
  if(reason==='download-source-cleanup-incomplete')return '正在等待后台任务退出，稍后自动重试';
  if(/automatic-.*budget-exhausted/.test(reason))return '该游戏已用尽自动尝试预算，已跳过；可手动重试';
  if(/video-selection-expired/.test(reason))return '所选视频已失效，请重新搜索；原壁纸已保留';
  if(/media-size-invalid/.test(reason))return '视频超过单文件限制，保留原壁纸';
  if(/store-unavailable/.test(reason))return '商店资料暂不可用，本轮已跳过';
  if(/not-game-or-no-video|no-dynamic-video/.test(reason))return '该条目暂无动态素材，本轮已跳过';
  if(/cache-owned-or-deferred/.test(reason))return '素材正由其他任务处理，等待归还后继续';
  if(/identity-helper-unavailable/.test(reason))return '识别组件暂不可用，组件恢复后重新识别';
  if(/identity-target-unavailable/.test(reason))return '游戏路径暂不可用，文件恢复后重新识别';
  if(/identity-worker-unavailable/.test(reason))return '识别组件或游戏路径不可用，恢复后重新识别';
  if(/identity-timeout/.test(reason))return '非 Steam 游戏识别超时，已退出并等待重试';
  if(/identity-network-unavailable/.test(reason))return 'Steam 身份查询网络暂不可用，等待自动重试';
  if(/identity-unconfirmed/.test(reason))return '非 Steam 游戏身份尚未确认，保留队列稍后重试';
  if(/shortcut-no-longer-current/.test(reason))return 'Steam 账号或快捷方式已变化，等待确认后继续';
  if(/upgrade-required/.test(reason))return '游戏库下载组件待更新';
  if(/worker-stalled/.test(reason))return '下载无进展，已退出并等待重试';
  if(/task-timeout|task-time-budget/.test(reason))return '单项下载超时，已退出并等待重试';
  if(/session-timeout/.test(reason))return '本批下载已达时限，保存队列后继续';
  if(/staging-budget-full/.test(reason))return '下载暂存空间已满，等待腾出空间后继续';
  if(/storage-unavailable|data-root-unwritable|media-write|media-publish/.test(reason))return '存储暂不可用，等待恢复后继续下载';
  if(/video-incomplete|video-structure|slot-container/.test(reason))return '素材格式异常，保留原壁纸并等待重试';
  if(/network|http|truncated|download-failed/.test(reason))return '网络下载中断，等待自动重试';
  if(/worker-exited/.test(reason))return '下载进程异常退出，等待自动恢复';
  if(/launch|job-|resume-failed/.test(reason))return '下载进程启动失败，等待自动恢复';
  if(/owner-stopped/.test(reason))return 'YMCC 已结束，队列已保留';
  if(/queue-unreadable/.test(reason))return '下载队列暂不可读，稍后恢复';
  if(/host-unavailable/.test(reason))return '下载接口暂不可用，等待恢复';
  return '';
}
interface DownloadPorts {
  workAllowed:()=>boolean;invoke:typeof invoke;enabled:()=>boolean;ready:()=>boolean;threshold:()=>number;
  listen:(listener:(state:MediaDownloadState)=>void)=>()=>void;
  schedule:typeof setTimeout;cancel:typeof clearTimeout;
}
export class MediaDownloads {
  state:MediaDownloadState={};
  private listeners=new Set<()=>void>();private pause='capacity-unavailable';private playback30Idle=false;private submittedPlayback30Idle=false;
  private configurationInterrupted=false;
  private active=false;private suspended=false;private syncing=false;private again=false;private epoch=0;
  private applied='';private submitted='';private failures=0;
  private checking:Promise<void>|null=null;private reading:Promise<void>|null=null;
  private readAbort:AbortController|null=null;private commandGeneration=0;
  private authoritativeRevision=0;private acceptedNativeStates=0;private readFailureKey='';
  private startupTimer:ReturnType<typeof setTimeout>|null=null;private startupChecked=false;private startupDeadline=0;
  private retry:ReturnType<typeof setTimeout>|null=null;private port:DownloadPorts;
  constructor(port:Partial<DownloadPorts>={}){
    this.port={workAllowed:()=>!wallpaperPowerBlocked(),invoke,enabled:()=>getUiSetting('steamWallpaperBackgroundDownload'),ready:uiSettingsLoaded,threshold:()=>getUiSetting('steamWallpaperDownloadCpuThreshold'),listen:listener=>on<MediaDownloadState>('gameMedia.downloadsChanged',listener),schedule:(...args)=>globalThis.setTimeout(...args),cancel:timer=>globalThis.clearTimeout(timer),...port};
  }
  private display(state:MediaDownloadState){
    this.state=state;this.armStartupCheck();
    // Observers must not interrupt native receipt handling or recovery scheduling.
    for(const listener of this.listeners){try{void Promise.resolve(listener()).catch(()=>{});}catch{}}
  }
  private cancelStartupCheck(){if(this.startupTimer!==null)this.port.cancel(this.startupTimer);this.startupTimer=null;}
  private armStartupCheck(){
    const wanted=this.active&&!this.suspended&&this.port.workAllowed()&&this.port.ready()&&(this.port.enabled()||Number(this.state.manualUntil||0)>Date.now())&&this.state.resourcePauseReason==='startup-protection';
    if(!wanted){this.cancelStartupCheck();this.startupChecked=false;this.startupDeadline=0;return;}
    const deadline=Number(this.state.startupProtectionUntil)||0;
    if(deadline!==this.startupDeadline){this.cancelStartupCheck();this.startupChecked=false;this.startupDeadline=deadline;}
    if(this.startupTimer!==null||this.reading)return;
    // Only a native snapshot can release admission. The deadline just repairs
    // a missed completion push, including when no queued work remains.
    const delay=this.startupChecked?20000:deadline?Math.max(250,Math.min(30000,deadline-Date.now())):30000;
    this.startupTimer=this.port.schedule(()=>{this.startupTimer=null;this.startupChecked=true;void this.refresh();},delay);
  }
  private configurationArgs(){
    return {enabled:this.port.enabled(),pauseReason:this.pause,cpuThreshold:this.port.threshold(),catalog:false,playback30Idle:this.playback30Idle};
  }
  private retireCommandScope(keepRetry=false){
    // A read can confirm the in-flight policy while a newer setting still waits behind it.
    const queuedConfiguration=this.again&&JSON.stringify(this.configurationArgs())!==this.applied;
    if((this.syncing&&(this.applied!==this.submitted||queuedConfiguration))||(!keepRetry&&this.retry!==null)){this.configurationInterrupted=true;this.applied='';}
    ++this.commandGeneration;this.checking=null;this.again=false;
    if(!keepRetry)this.cancelRetry();
  }
  private retireReadScope(){
    const old=this.readAbort;this.readAbort=null;this.reading=null;old?.abort();this.cancelStartupCheck();
  }
  private reconcileInterruptedConfiguration(state:MediaDownloadState){
    if(!this.configurationInterrupted||!this.active||this.suspended||!this.port.workAllowed()||downloadReadUnavailable(state))return;
    if(!Number.isSafeInteger(state.statusRevision)||Number(state.statusRevision)<=0||Number(state.statusRevision)<this.authoritativeRevision||typeof state.enabled!=='boolean'||typeof state.pauseReason!=='string'||typeof state.cpuThreshold!=='number')return;
    const args=this.configurationArgs();
    const signature=JSON.stringify(args);
    // Only current Native truth for the same submitted idempotent policy can
    // acknowledge an interrupted return. A different newer preference still
    // passes through the original serial sync owner after the old local wait.
    if(!this.submittedPlayback30Idle&&signature===this.submitted&&state.enabled===args.enabled&&state.pauseReason===args.pauseReason&&state.cpuThreshold===args.cpuThreshold){
      this.applied=signature;this.configurationInterrupted=false;this.failures=0;this.cancelRetry();return;
    }
    this.configurationInterrupted=false;this.cancelRetry();this.applied='';this.sync();
  }
  refresh():Promise<void>{
    if(!this.active||this.suspended||!this.port.ready()||!this.port.workAllowed())return Promise.resolve();
    if(this.reading)return this.reading;
    const read=this.readAbort=new AbortController(),generation=this.commandGeneration,observation=this.acceptedNativeStates;
    const pending=this.port.invoke<MediaDownloadState>('gameMedia.downloads',{}, {signal:read.signal,timeoutMs:5000}).then(state=>{
      if(this.readAbort!==read)return;
      this.accept(state,observation);
      if(this.readAbort===read&&generation===this.commandGeneration)this.reconcileInterruptedConfiguration(state);
    }).catch(()=>{/* A failed read must retain the last actual native reason. */}).finally(()=>{
      if(this.readAbort!==read)return;
      this.readAbort=null;this.reading=null;this.armStartupCheck();
      if(this.configurationInterrupted&&!this.suspended&&this.port.workAllowed())this.scheduleRetry(()=>{void this.refresh();});
    });
    this.reading=pending;return pending;
  }
  accept(state:MediaDownloadState,observation?:number){
    const incoming=Number(state.statusRevision||0),known=Math.max(this.authoritativeRevision,Number(this.state.statusRevision||0)),unavailable=downloadReadUnavailable(state);
    if(incoming>0&&incoming<known||incoming===0&&known>0&&!unavailable)return;
    if(unavailable){
      if(observation!==undefined&&observation!==this.acceptedNativeStates)return;
      const key=JSON.stringify(state);if(downloadReadUnavailable(this.state)&&this.state.reason===state.reason&&this.readFailureKey===key)return;
      // Unversioned failures are diagnostic projections. Keep known counts for
      // the existing recovery owner, but never turn them into active progress.
      this.authoritativeRevision=known;this.readFailureKey=key;
      this.display({...this.state,...state,statusRevision:state.statusRevision,activeCount:0,current:state.current,resourcePauseReason:state.resourcePauseReason});return;
    }
    if(incoming>0&&incoming===known&&!downloadReadUnavailable(this.state))return;
    if(incoming===0&&JSON.stringify(state)===JSON.stringify(this.state))return;
    this.authoritativeRevision=Math.max(known,incoming);++this.acceptedNativeStates;this.readFailureKey='';
    this.display(state);
  }
  subscribe(listener:()=>void){this.listeners.add(listener);return()=>{this.listeners.delete(listener);};}
  watchCompleted(refresh:()=>void){
    // The first readable status restores persisted counts. The startup media
    // snapshot already covers them; only transitions need another snapshot.
    let completed=this.acceptedNativeStates||downloadReadUnavailable(this.state)?Number(this.state.completed||0):undefined;
    return this.subscribe(()=>{
      // Keep reconciliation when the queue was unreadable at startup.
      if(downloadReadUnavailable(this.state)){completed??=0;return;}
      const next=Number(this.state.completed||0),changed=completed!==undefined&&next!==completed;
      completed=next;
      if(changed)refresh();
    });
  }
  policy(reason:string,playback30Idle=false){if(this.pause===reason&&this.playback30Idle===playback30Idle)return;this.pause=reason;this.playback30Idle=playback30Idle;if(!this.port.workAllowed()){this.cancelStartupCheck();this.cancelRetry();}this.sync();}
  private cancelRetry(){if(this.retry!==null)this.port.cancel(this.retry);this.retry=null;}
  private scheduleRetry(action:()=>void){
    if(this.retry!==null)return;
    const epoch=this.epoch;
    const delays=[1000,5000,20000],delay=delays[Math.min(this.failures++,delays.length-1)];
    const timer=this.port.schedule(()=>{
      if(this.retry!==timer)return;this.retry=null;
      if(this.active&&epoch===this.epoch)action();
    },delay);this.retry=timer;
  }
  sync(){
    if(!this.active||!this.port.ready()||this.configurationInterrupted)return;
    const args=this.configurationArgs();
    const signature=JSON.stringify(args);
    if(this.syncing){this.again=true;return;}
    if(signature===this.applied)return;
    if(this.retry!==null&&signature===this.submitted)return;
    if(signature!==this.submitted)this.failures=0;
    this.cancelRetry();this.submitted=signature;this.submittedPlayback30Idle ||= args.playback30Idle;this.syncing=true;const epoch=this.epoch,generation=this.commandGeneration,observation=this.acceptedNativeStates;
    void this.port.invoke<MediaDownloadState>('gameMedia.background',args,{timeoutMs:5000}).then(state=>{
      if(!this.active||epoch!==this.epoch||generation!==this.commandGeneration)return;
      this.applied=signature;this.configurationInterrupted=false;this.failures=0;this.accept(state,observation);
    }).catch(()=>{
      if(!this.active||epoch!==this.epoch||generation!==this.commandGeneration)return;
      this.applied='';
      if(observation===this.acceptedNativeStates)this.display({...this.state,phase:'deferred',reason:'host-unavailable'});
      if(!this.port.workAllowed())return;
      this.scheduleRetry(()=>this.sync());
    }).finally(()=>{
      if(epoch!==this.epoch)return;this.syncing=false;
      if(this.again){this.again=false;this.sync();}
    });
  }
  recheck():Promise<void>{
    if(!this.active||this.suspended||!this.port.ready()||!this.port.workAllowed()||!this.port.enabled()&&!(Number(this.state.manualUntil||0)>Date.now()))return Promise.resolve();
    if(this.checking)return this.checking;
    const epoch=this.epoch,generation=this.commandGeneration,observation=this.acceptedNativeStates;
    const pending=this.port.invoke<MediaDownloadState>('gameMedia.background',{recheck:true},{timeoutMs:5000}).then(state=>{
      if(!this.active||this.suspended||epoch!==this.epoch||generation!==this.commandGeneration)return;this.accept(state,observation);
      // A newer push may already have superseded this return. Repair against
      // the accepted native state; equal revisions still repair a real mismatch.
      const current=this.state;
      if(downloadReadUnavailable(current))return;
      if(current.enabled!==this.port.enabled()||current.pauseReason!==this.pause||current.cpuThreshold!==this.port.threshold()){this.applied='';this.sync();}
    }).catch(()=>{if(this.active&&!this.suspended&&epoch===this.epoch&&generation===this.commandGeneration&&observation===this.acceptedNativeStates)this.display({...this.state,phase:'deferred',reason:'host-unavailable'});}).finally(()=>{if(this.checking===pending)this.checking=null;});
    this.checking=pending;return pending;
  }
  private async manualCommand(args:{manual?:boolean;cancelManual?:boolean}){
    if(!this.port.workAllowed()&&!args.cancelManual)throw Error(wallpaperPowerReason()||'wallpaper-power-paused');
    const epoch=this.epoch,generation=this.commandGeneration,observation=this.acceptedNativeStates;
    const value=await this.port.invoke<MediaDownloadState>('gameMedia.background',args,{timeoutMs:8000});
    if(this.active&&!this.suspended&&epoch===this.epoch&&generation===this.commandGeneration)this.accept(value,observation);
    return value;
  }
  requestNow(){return this.manualCommand({manual:true});}
  cancelManual(){return this.manualCommand({cancelManual:true});}
  start(){
    if(this.active)return()=>{};
    this.active=true;this.configurationInterrupted=false;this.suspended=true;const epoch=++this.epoch;this.applied='';this.submitted='';this.failures=0;
    const off=this.port.listen(state=>{if(this.active&&epoch===this.epoch)this.accept(state);});
    const power=subscribeWallpaperPowerChanged(blocked=>{
      this.retireReadScope();this.retireCommandScope();
      if(blocked){this.cancelRetry();this.policy(wallpaperPowerReason());}
      else {this.policy('capacity-unavailable');void this.refresh();}
    });
    const settings=()=>{this.sync();this.armStartupCheck();};
    const visible=()=>{if(typeof document==='undefined'||document.visibilityState!=='hidden')void this.refresh();};
    let powerPhase='',powerGeneration=0,powerKnown:boolean|undefined,powerAdmitted:boolean|undefined,powerNotificationSequence:number|undefined,powerSuspendSerial:number|undefined;
    const lifecycle=onSharedPowerLifecycle(state=>{
      const admitted=isSharedPowerLifecycleAdmitted(state);
      if(!this.active||epoch!==this.epoch||powerPhase===state.phase&&powerGeneration===state.generation&&powerKnown===state.known&&powerAdmitted===admitted&&powerNotificationSequence===state.observedNotificationSequence&&powerSuspendSerial===state.suspendSerial)return;
      const readScopeChanged=powerGeneration!==state.generation||powerAdmitted!==admitted||powerSuspendSerial!==state.suspendSerial;
      powerPhase=state.phase;powerGeneration=state.generation;powerKnown=state.known;powerAdmitted=admitted;powerNotificationSequence=state.observedNotificationSequence;powerSuspendSerial=state.suspendSerial;
      this.suspended=!admitted;
      if(readScopeChanged)this.retireReadScope();
      // A notification-only advance retires replies, not the current retry deadline.
      this.retireCommandScope(admitted&&!readScopeChanged);
      if(admitted&&(readScopeChanged||this.configurationInterrupted&&!this.reading&&this.retry===null))void this.refresh();
    },{observeNotificationSequence:true,observeSuspendSerial:true});
    window.addEventListener('pageshow',visible);window.addEventListener('focus',visible);
    if(typeof document!=='undefined')document.addEventListener('visibilitychange',visible);
    window.addEventListener('ui-settings:changed',settings);window.addEventListener('ui-settings:loaded',settings);settings();
    return()=>{if(epoch!==this.epoch)return;this.active=false;this.configurationInterrupted=false;++this.epoch;this.checking=null;this.retireReadScope();this.syncing=false;this.again=false;this.cancelRetry();power();off();lifecycle();window.removeEventListener('pageshow',visible);window.removeEventListener('focus',visible);if(typeof document!=='undefined')document.removeEventListener('visibilitychange',visible);window.removeEventListener('ui-settings:changed',settings);window.removeEventListener('ui-settings:loaded',settings);};
  }
}
export const gameMediaDownloads=new MediaDownloads();
export const backgroundDownloadEnabled=()=>getUiSetting('steamWallpaperBackgroundDownload');
export async function setBackgroundDownloadEnabled(enabled:boolean){await setUiSettings({steamWallpaperBackgroundDownload:enabled});}

export async function setDownloadCpuThreshold(threshold:number){await setUiSettings({steamWallpaperDownloadCpuThreshold:threshold});}
