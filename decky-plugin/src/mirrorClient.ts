import {validPerformanceMirror,performancePatchAllowed,type PerformanceMirrorView} from './performanceModel';
// Native injects the short-lived credential into the Steam shared context.
// No unauthenticated localhost discovery, persisted credentials or polling.
import { validSteamObservation, type SteamRunningObservation } from './steamRunningContext';
export interface Choice { data: string; label: string; disabled?: boolean; }
export interface MirrorField { displayLabel?: string; value: string; choices: Choice[]; supported: boolean; inherited?: boolean; sourceRevision?:string; }
export type MirrorCommand = 'audioOptimization.setEnabled' | 'game.setField' | 'fan.setEnabled' | 'fan.setPreset' | 'fan.setChoice' | 'frame.setField' | 'touchpad.setField' | 'global.setField' | 'monitor.setEnabled' | 'performance.setControls';
import type {LosslessProgramState} from '../../src/bridge/losslessProgramState';
export interface MirrorFrameSetting {fps:number;ceiling:number;lastFps:number;}
export interface MirrorSnapshot {
  audioOptimization?:import('../../src/bridge/fxSound').FxSoundState;
  virtualController?: {text:string;detail:string;tone:'on'|'off'|'pending'|'warning';locked:boolean};
  losslessProgram?:LosslessProgramState;
  performance?:PerformanceMirrorView;
  trainer?: {available:boolean;pending:boolean;notice:string};
  monitor?: {enabled:boolean};
  global?: {fields:Record<string,MirrorField>};
  frames?: {pair:Record<'ac'|'dc',MirrorFrameSetting>;dedicated:boolean};
  touchpads?: {persona:string;fields:Record<string,MirrorField>};
  powerSource?: 'ac' | 'dc' | null;
  provenance?: { kind: 'ymcc-native' | 'unconfirmed'; pid?: number };
  runId: string;
  generation: number;
  revision: number;
  ready: boolean;
  game: null | { label: string; identity: string; fields: Record<string, MirrorField> };
  fan: { supported: boolean; enabled: boolean; preset: string; choices: Choice[]; canToggle?:boolean; pending?:boolean; notice?:string };
  notice?: string;
  actions?: { audioOptimization?:boolean; fan:boolean; game:boolean; frames?:boolean; touchpads?:boolean; global?:boolean; monitor?:boolean; performance?:boolean };
  steam?:SteamRunningObservation;
}
export interface Bootstrap { endpoint: string; token: string; runId: string; hudProtocol?: number; wallpaperProtocol?: number; wallpaperPrefetchProtocol?: number; updaterProtocol?:number; frontendBuildId?: string; }
export interface ConnectionTestResult { pid:number;revision:number;elapsedMs:number; }
export interface ClientState { connected: boolean; snapshot: MirrorSnapshot | null; notice: string; busy: boolean; connectionTest?:{pending:boolean;notice:string;elapsedMs?:number}; }
export interface ClientDeps {
  bootstrap: () => Bootstrap | undefined;
  socket: (url: string) => WebSocket;
  schedule: (action: () => void, delay: number) => ReturnType<typeof setTimeout>;
  cancel: (handle: ReturnType<typeof setTimeout>) => void;
  clientId: string;
  now?: () => number;
  buildId?: string;
  steam?:() => SteamRunningObservation;
}
function validChoices(value: unknown): value is Choice[] {
  return Array.isArray(value) && value.length <= 32 && value.every(option => option && (option.disabled === undefined || typeof option.disabled === 'boolean') && typeof option.data === 'string' && option.data.length <= 64 && typeof option.label === 'string' && option.label.length <= 512);
}
function validSnapshot(value: any, runId: string): value is MirrorSnapshot {
  if (!value || value.runId !== runId || !Number.isSafeInteger(value.revision) || value.revision < 0 || !Number.isSafeInteger(value.generation) || value.generation < 0 || typeof value.ready !== 'boolean') return false;
  if (!value.fan || typeof value.fan.enabled !== 'boolean' || typeof value.fan.supported !== 'boolean' || typeof value.fan.preset !== 'string' || !validChoices(value.fan.choices)) return false;
  if (value.game !== null) {
    if (!value.game || typeof value.game.label !== 'string' || typeof value.game.identity !== 'string' || !value.game.fields || typeof value.game.fields !== 'object' || Array.isArray(value.game.fields)) return false;
    const fields = Object.values(value.game.fields) as any[];
    if (fields.length > 16 || !fields.every(field => field && typeof field.value === 'string' && typeof field.supported === 'boolean' && (field.inherited === undefined || typeof field.inherited === 'boolean') && (field.sourceRevision===undefined||typeof field.sourceRevision==='string'&&field.sourceRevision.length>0&&field.sourceRevision.length<=128) && validChoices(field.choices))) return false;
  }
  if(value.global!==undefined&&(!value.global||!value.global.fields||typeof value.global.fields!=='object'||Array.isArray(value.global.fields)||Object.keys(value.global.fields).length>8||!Object.values(value.global.fields).every((f:any)=>f&&typeof f.value==='string'&&typeof f.supported==='boolean'&&validChoices(f.choices))))return false;
  if(value.powerSource !== undefined && value.powerSource !== null && !['ac', 'dc'].includes(value.powerSource)) return false;
  if(value.provenance!==undefined && (!value.provenance || !['ymcc-native','unconfirmed'].includes(value.provenance.kind) || (value.provenance.kind==='ymcc-native' && (!Number.isSafeInteger(value.provenance.pid) || value.provenance.pid<=0))))return false;
  if(value.steam!==undefined&&!validSteamObservation(value.steam))return false;
  if(value.trainer!==undefined&&(!value.trainer||typeof value.trainer.available!=='boolean'||typeof value.trainer.pending!=='boolean'||typeof value.trainer.notice!=='string'||value.trainer.notice.length>300||Object.keys(value.trainer).some(key=>!['available','pending','notice'].includes(key))||value.trainer.available&&!value.game))return false;
  if(value.frames!==undefined && (!value.frames || typeof value.frames.dedicated!=='boolean' || !['ac','dc'].every(side=>{const s=value.frames.pair?.[side];return s && ['fps','ceiling','lastFps'].every(k=>Number.isSafeInteger(s[k])&&s[k]>=0&&s[k]<=300);})))return false;
  if(value.touchpads!==undefined && (!value.touchpads || typeof value.touchpads.persona!=='string' || !value.touchpads.fields || typeof value.touchpads.fields!=='object' || Array.isArray(value.touchpads.fields) || Object.keys(value.touchpads.fields).length>8 || !Object.values(value.touchpads.fields).every((f:any)=>f && typeof f.value==='string' && typeof f.supported==='boolean' && validChoices(f.choices))))return false;
  if(value.virtualController!==undefined){const v=value.virtualController;if(!v||typeof v.text!=='string'||v.text.length>512||typeof v.detail!=='string'||v.detail.length>2048||!['on','off','pending','warning'].includes(v.tone)||typeof v.locked!=='boolean')return false;}
  if(value.performance!==undefined&&!validPerformanceMirror(value.performance))return false;
  if(value.monitor!==undefined&&(!value.monitor||typeof value.monitor.enabled!=='boolean'))return false;
  if(value.audioOptimization!==undefined&&(!value.audioOptimization||['known','installed','running','enabled','selected'].some(key=>typeof value.audioOptimization[key]!=='boolean')||['preset','output','reason'].some(key=>typeof value.audioOptimization[key]!=='string')))return false;
  if(value.losslessProgram!==undefined&&(!value.losslessProgram||typeof value.losslessProgram.known!=='boolean'||typeof value.losslessProgram.running!=='boolean'||!value.losslessProgram.known&&value.losslessProgram.running||Object.keys(value.losslessProgram).some(key=>!['known','running'].includes(key))))return false;
  if (value.actions !== undefined && (!value.actions || typeof value.actions.fan !== 'boolean' || typeof value.actions.game !== 'boolean' || value.actions.audioOptimization!==undefined&&typeof value.actions.audioOptimization!=='boolean' || value.actions.frames!==undefined&&typeof value.actions.frames!=='boolean' || value.actions.touchpads!==undefined&&typeof value.actions.touchpads!=='boolean' || value.actions.global!==undefined&&typeof value.actions.global!=='boolean' || value.actions.monitor!==undefined&&typeof value.actions.monitor!=='boolean' || value.actions.performance!==undefined&&typeof value.actions.performance!=='boolean')) return false;
  if (value.fan.canToggle !== undefined && typeof value.fan.canToggle !== 'boolean') return false;
  if(value.fan.pending!==undefined&&typeof value.fan.pending!=='boolean'||value.fan.notice!==undefined&&typeof value.fan.notice!=='string')return false;
  return value.notice === undefined || typeof value.notice === 'string';
}
export class MirrorClient {
  private visible = false;
  private readonly defaultVisibilityOwner = {};
  private readonly visibleOwners = new Set<object>();
  private passive = false;
  private passiveConsumers=new Set<string>();
  private extraListeners = new Set<(message:unknown)=>void>();
  private disposed = false;
  private epoch = 0;
  private attempt = 0;
  private connectedAt: number | null = null;
  private sequence = 0;
  private binding: Bootstrap | null = null;
  private socket: WebSocket | null = null;
  private retry: ReturnType<typeof setTimeout> | null = null;
  private connectDeadline: ReturnType<typeof setTimeout> | null = null;
  private snapshotDeadline: ReturnType<typeof setTimeout> | null = null;
  private wallpaperDeadline: null | {socket:WebSocket;runId:string;seq:number;appId?:string;timer:ReturnType<typeof setTimeout>} = null;
  private pending: null | { id: string; timer: ReturnType<typeof setTimeout>; resolve: (value: unknown) => void; reject: (error: Error) => void } = null;
  private probe: null | {id:string;started:number;timer:ReturnType<typeof setTimeout>;resolve:(value:ConnectionTestResult)=>void;reject:(error:Error)=>void} = null;
  private operationError: null | { notice:string;runId:string;generation:number;identity:string } = null;
  private listeners = new Set<(state: ClientState) => void>();
  private connectionListeners = new Set<(state: ClientState) => void>();
  private state: ClientState = { connected: false, snapshot: null, notice: '等待 YMCC 镜像连接', busy: false };
  constructor(private deps: ClientDeps) {}
  // HUD consumers only need link transitions; keep complete snapshots private to control subscribers.
  subscribe(listener: (state: ClientState) => void, options: { connectionOnly?: boolean } = {}) {
    const listeners = options.connectionOnly ? this.connectionListeners : this.listeners;
    listeners.add(listener); listener(options.connectionOnly ? this.connectionState() : this.snapshot());
    return () => { listeners.delete(listener); };
  }
  private connectionState(): ClientState { return { connected: this.state.connected, snapshot: null, notice: '', busy: false }; }
  snapshot(): ClientState { return structuredClone(this.state); }
  private publish(patch: Partial<ClientState>) {
    if (!(Object.keys(patch) as (keyof ClientState)[]).some(key => !Object.is(this.state[key], patch[key]))) return;
    const connected = this.state.connected;
    this.state = { ...this.state, ...patch };
    for (const listener of this.listeners) { try { listener(this.snapshot()); } catch {} }
    if (connected !== this.state.connected) for (const listener of this.connectionListeners) { try { listener(this.connectionState()); } catch {} }
  }
  subscribeExtra(listener:(message:unknown)=>void){this.extraListeners.add(listener);return()=>{this.extraListeners.delete(listener);};}
  setPassiveVisible(value:boolean){this.setPassiveDemand('hud',value);}
  setPassiveDemand(consumer:string,value:boolean){
    if(consumer==='wallpaper'&&!value)this.clearWallpaperDeadline();
    if(value)this.passiveConsumers.add(consumer);else this.passiveConsumers.delete(consumer);
    value=this.passiveConsumers.size>0;
    if(this.disposed||value===this.passive)return;
    this.passive=value;
    if(value&&!this.socket&&this.retry===null){this.attempt=0;this.connect();}
    else if(!this.visible&&!this.passive)this.disconnect('被动显示已关闭');
  }
  // All passive consumers share the original connection owner/backoff. A send
  // failure invalidates that socket, but never queues/replays the uncertain
  // payload. The consumer reports its CURRENT context after a fresh connection.
  private sendPassive(message:Record<string,unknown>,beforeSend?:(socket:WebSocket)=>void):boolean {
    const socket=this.socket;if(!socket)return false;
    let payload:string;try{payload=JSON.stringify(message);}catch{return false;}
    try{beforeSend?.(socket);socket.send(payload);return true;}catch{
      if(this.socket===socket){this.disconnect('镜像发送失败；正在重新连接');this.recoverConnection();}
      return false;
    }
  }
  private clearWallpaperDeadline():void {
    const pending=this.wallpaperDeadline;this.wallpaperDeadline=null;
    if(pending)this.deps.cancel(pending.timer);
  }
  private waitForWallpaperResponse(socket:WebSocket,runId:string,seq:number,appId?:string):void {
    if(this.socket!==socket||this.binding?.runId!==runId||!this.passiveConsumers.has('wallpaper'))return;
    const prior=this.wallpaperDeadline;
    if(prior?.socket===socket&&prior.runId===runId){
      // Keep the original unanswered budget even as focus/events supersede the
      // selection. Only the latest sent sequence may acknowledge this request.
      if(seq>=prior.seq){prior.seq=seq;prior.appId=appId;}
      return;
    }
    this.clearWallpaperDeadline();
    const epoch=this.epoch,pending={socket,runId,seq,appId,timer:null as unknown as ReturnType<typeof setTimeout>};
    this.wallpaperDeadline=pending;
    pending.timer=this.deps.schedule(()=>{
      if(this.wallpaperDeadline!==pending||this.epoch!==epoch||this.socket!==socket||!this.passiveConsumers.has('wallpaper'))return;
      this.wallpaperDeadline=null;
      this.disconnect('壁纸状态响应超时；正在重新连接，不重放未确认操作');
      this.recoverConnection();
    },10000);
  }
  sendWallpaper(command:'wallpaper.context'|'wallpaper.status'|'wallpaper.neighbors',data:Record<string,unknown>):boolean {
    if(!this.passiveConsumers.has('wallpaper')||!this.state.connected||!this.socket||this.binding?.wallpaperProtocol!==1)return false;
    if(command==='wallpaper.neighbors'&&this.binding?.wallpaperPrefetchProtocol!==1)return false;
    const runId=this.binding.runId,seq=data.contextSeq,visible=data.visible,appId=typeof data.appId==='string'?data.appId:undefined;
    const waiting=this.wallpaperDeadline;
    if(command==='wallpaper.context'&&visible===false&&waiting&&typeof seq==='number'&&Number.isSafeInteger(seq)&&seq>=waiting.seq)
      this.clearWallpaperDeadline();
    // Serialize first, then arm before send so local JSON failures stay local
    // and an immediate correlated reply can cancel without leaving a timer.
    return this.sendPassive({...data,command,runId},socket=>{
      if(command==='wallpaper.context'&&visible===true&&typeof seq==='number'&&Number.isSafeInteger(seq)&&seq>=0)
        this.waitForWallpaperResponse(socket,runId,seq,appId);
    });
  }
  sendUpdater(command:'updater.status'|'updater.request',data:Record<string,unknown>):boolean {
    if(!this.passiveConsumers.has('updater')||!this.state.connected||!this.socket||this.binding?.updaterProtocol!==1)return false;
    return this.sendPassive({...data,command,runId:this.binding.runId});
  }
  updaterSupported(){return this.binding?.updaterProtocol===1;}
  updaterRunId(){return this.binding?.runId;}
  private wanted(){return this.visible||this.passive;}
  private passiveRecoveryWanted(){return this.passiveConsumers.has('hud')&&this.binding?.hudProtocol===1||this.passiveConsumers.has('wallpaper')&&this.binding?.wallpaperProtocol===1||this.passiveConsumers.has('updater')&&this.binding?.updaterProtocol===1;}
  private now(){return this.deps.now?.()??Date.now();}
  sendHud(command:'hud.status'|'hud.ack',status:unknown,ok?:boolean):boolean {
    if(command!=='hud.status'&&command!=='hud.ack')return false;
    if(!this.passiveConsumers.has('hud')||!this.state.connected||!this.socket||this.binding?.hudProtocol!==1)return false;
    return this.sendPassive({command,runId:this.binding.runId,status,...(ok===undefined?{}:{ok})});
  }
  setVisible(visible: boolean, owner: object = this.defaultVisibilityOwner) {
    if(this.disposed)return;
    // QAM can mount more than one Content. A hidden page must not revoke
    // another page's authenticated snapshot or in-flight mutation.
    if(visible)this.visibleOwners.add(owner);else this.visibleOwners.delete(owner);
    visible=this.visibleOwners.size>0;
    if (visible === this.visible) return;
    this.visible = visible;
    if(!visible)this.finishProbe(undefined,'侧栏已隐藏');
    if (visible) { this.attempt = 0; if(this.socket&&this.state.connected&&this.binding)this.requestSnapshot(this.socket,this.binding.runId);else this.connect(); }
    else if(!this.passive)this.disconnect('侧栏已隐藏');
    else {this.pauseSnapshotDemand();this.rejectPending('侧栏已隐藏；不重放未确认操作');this.publish({snapshot:null});}
  }
  bootstrapChanged(): void {
    if (!this.wanted() || this.disposed) return;
    const current = this.deps.bootstrap();
    const same = !!current && !!this.binding && current.endpoint === this.binding.endpoint &&
      current.runId === this.binding.runId && current.token === this.binding.token && current.hudProtocol === this.binding.hudProtocol && current.wallpaperProtocol===this.binding.wallpaperProtocol && current.updaterProtocol===this.binding.updaterProtocol;
    if (same && (this.socket || this.retry !== null)) return;
    this.disconnect('YMCC 会话连接已更新，正在重新读取状态');
    this.attempt = 0; this.connect();
  }
  private rejectPending(reason: string) {
    if (this.pending) { const pending = this.pending; this.pending = null; this.deps.cancel(pending.timer); pending.reject(new Error(reason)); }
    this.publish({ busy: false });
  }
  private endConnectionHealth():void {
    // Use the same existing stable-connection threshold for close, failed send
    // and response deadline. Brief flaps must retain their capped backoff.
    if(this.connectedAt!==null&&this.now()-this.connectedAt>=30000)this.attempt=0;
    this.connectedAt=null;
  }
  private disconnect(reason: string) {
    this.finishProbe(undefined,reason);
    this.operationError=null;this.endConnectionHealth();
    ++this.epoch;
    if (this.retry !== null) { this.deps.cancel(this.retry); this.retry = null; }
    if (this.connectDeadline !== null) { this.deps.cancel(this.connectDeadline); this.connectDeadline = null; }
    this.clearSnapshotDeadline();this.clearWallpaperDeadline();
    const old = this.socket; this.socket = null; try { old?.close(); } catch {}
    this.rejectPending('连接已结束，操作结果需重新确认');
    this.publish({ connected: false, snapshot: null, notice: reason });
  }
  private recoverConnection() {
    if(!this.wanted()||this.disposed||this.socket||this.retry!==null)return;
    // HUD demand survives a hidden sidebar and long outages. Back off without
    // creating timers or business polling while the shared socket is healthy.
    const background=this.passiveRecoveryWanted();
    const delays=[250,1000,3000,10000,30000];
    const delay=delays[Math.min(this.attempt,delays.length-1)],recoveryEpoch=this.epoch;
    this.attempt=Math.min(this.attempt+1,delays.length);
    this.retry=this.deps.schedule(()=>{if(recoveryEpoch!==this.epoch)return;this.retry=null;this.connect();},delay);
    this.publish({notice:background?'HUD 后台连接中断，正在自动重连':'侧栏连接中断，正在自动重连'});
  }
  private finishProbe(snapshot?:MirrorSnapshot,error?:string):void {
    const probe=this.probe;if(!probe)return;
    this.probe=null;this.deps.cancel(probe.timer);
    const elapsedMs=Math.max(0,this.now()-probe.started);
    if(!error&&snapshot?.ready&&snapshot.provenance?.kind==='ymcc-native') {
      const result={pid:snapshot.provenance.pid!,revision:snapshot.revision,elapsedMs};
      this.publish({connectionTest:{pending:false,elapsedMs,notice:`连接实测通过 · YMCC ${result.pid} · ${elapsedMs} ms`}});probe.resolve(result);
    }else {
      const notice=error??'YMCC 已回复，但实际状态尚未就绪';
      this.publish({connectionTest:{pending:false,elapsedMs,notice}});probe.reject(new Error(notice));
    }
  }
  // A fresh authenticated snapshot round trip; no settings write or device action.
  testConnection():Promise<ConnectionTestResult> {
    if(this.disposed||!this.visible)return Promise.reject(new Error('请先打开侧栏再实测连接'));
    if(this.pending||this.probe)return Promise.reject(new Error('请等待当前操作完成'));
    const id='probe:'+this.deps.clientId+':'+(++this.sequence),started=this.now();
    return new Promise((resolve,reject)=>{
      const timer=this.deps.schedule(()=>this.finishProbe(undefined,'连接实测 15 秒内未取得实际状态；自动重连仍会继续'),15000);
      this.probe={id,started,timer,resolve,reject};
      this.publish({connectionTest:{pending:true,notice:'正在向 YMCC 请求最新状态…'}});
      try {
        if(this.socket&&this.state.connected&&this.binding)this.requestSnapshot(this.socket,this.binding.runId);
        else if(!this.socket)this.connect();
      }catch {this.finishProbe(undefined,'连接实测请求发送失败');}
    });
  }

  private clearSnapshotDeadline(){if(this.snapshotDeadline!==null){this.deps.cancel(this.snapshotDeadline);this.snapshotDeadline=null;}}
  private pauseSnapshotDemand(){
    this.clearSnapshotDeadline();
    // A failed pause also invalidates the shared passive transport.
    // Use its existing owner/backoff; never replay a business operation.
    if(this.socket&&this.binding)this.sendPassive({command:'snapshot',runId:this.binding.runId,subscribe:false});
  }
  private requestSnapshot(socket:WebSocket,runId:string) {
    // A requested state must eventually answer even when the socket remains OPEN.
    // Arm before send so a synchronous reply can cancel it; repeated demand never
    // postpones this deadline. There is no periodic healthy-state probe.
    if(this.visible&&this.socket===socket&&this.connectDeadline===null&&this.snapshotDeadline===null){
      const epoch=this.epoch;
      this.snapshotDeadline=this.deps.schedule(()=>{
        if(epoch!==this.epoch||this.socket!==socket||!this.visible)return;
        this.snapshotDeadline=null;
        this.disconnect('YMCC 状态响应超时；正在重新连接，不重放未确认操作');
        this.recoverConnection();
      },15000);
    }

    let observation:SteamRunningObservation|undefined;
    if(this.deps.steam){try{const value=this.deps.steam();observation=validSteamObservation(value)?value:{availability:'unavailable'};}catch{observation={availability:'unavailable'};}}
    try { socket.send(JSON.stringify({command:'snapshot',runId,clientId:this.deps.clientId,...(typeof this.deps.buildId==='string'&&/^[a-f0-9]{64}$/.test(this.deps.buildId)?{buildId:this.deps.buildId}:{}),...(this.probe?{probeId:this.probe.id}:{}),...(observation?{steam:observation}:{})})); }
    catch {
      if (this.socket !== socket || this.binding?.runId !== runId) return;
      this.finishProbe(undefined,'连接实测请求发送失败');
      this.disconnect('镜像状态请求发送失败；正在重新连接');
      this.recoverConnection();
    }
  }
  private connect() {
    if (!this.wanted() || this.disposed || this.socket) return;
    if(this.retry!==null){this.deps.cancel(this.retry);this.retry=null;}
    const raw = this.deps.bootstrap();
    const boot = raw ? { endpoint:raw.endpoint,token:raw.token,runId:raw.runId,...(raw.hudProtocol===1?{hudProtocol:1}:{}),...(raw.wallpaperProtocol===1?{wallpaperProtocol:1}:{}),...(raw.updaterProtocol===1?{updaterProtocol:1}:{}) } : undefined;
    const port = typeof boot?.endpoint === 'string' ? /^ws:\/\/127\.0\.0\.1:(\d{1,5})\/mirror$/.exec(boot.endpoint)?.[1] : undefined;
    if (!boot || !port || Number(port) < 1 || Number(port) > 65535 || typeof boot.token !== 'string' || !boot.token || boot.token.length > 128 ||
      typeof boot.runId !== 'string' || !boot.runId || boot.runId.length > 64) {
      this.publish({ connected: false, snapshot: null, notice: '等待 YMCC 提供镜像连接' }); if(this.binding)this.recoverConnection();else this.finishProbe(undefined,'YMCC 尚未提供镜像连接'); return;
    }
    this.binding = boot;
    const epoch = ++this.epoch;
    let socket: WebSocket;
    try { socket = this.deps.socket(`${boot.endpoint}?token=${encodeURIComponent(boot.token)}`); }
    catch { this.publish({connected:false,snapshot:null,notice:'镜像连接创建失败'});this.recoverConnection();return; }
    this.socket = socket;
    socket.addEventListener('open', () => {
      if (epoch !== this.epoch || !this.wanted()) return;
      if(this.passive){
        if(this.connectDeadline!==null){this.deps.cancel(this.connectDeadline);this.connectDeadline=null;}
        this.connectedAt=this.now();
        // HUD readiness belongs to the authenticated transport, independently
        // of whether the sidebar has received its business snapshot yet.
        this.publish({connected:true,snapshot:null,notice:''});
      }
      if(epoch===this.epoch&&this.visible)this.requestSnapshot(socket,boot.runId);
    });
    socket.addEventListener('message', (event: MessageEvent) => {
      if (epoch !== this.epoch || !this.wanted() || typeof event.data !== 'string') return;
      if (event.data.length > 65536) { this.disconnect('镜像消息超限'); return; }
      let message: any;
      try { message = JSON.parse(event.data); } catch { this.disconnect('镜像消息格式错误'); return; }
      if (!message || typeof message !== 'object' || Array.isArray(message)) { this.disconnect('镜像消息格式错误'); return; }
      if (message.runId !== boot.runId) { this.disconnect('YMCC 会话已变更，请重新打开侧栏'); return; }
      if (message.type === 'snapshot') {
        // A restored YMCC renderer can briefly resume its prior demand. A hidden
        // sidebar explicitly pauses that business stream while the HUD stays linked.
        if(!this.visible){this.pauseSnapshotDemand();return;}
        const snap = message.snapshot as MirrorSnapshot;
        if (!validSnapshot(snap, boot.runId)) { this.disconnect('镜像身份无效'); return; }
        if (this.state.snapshot && snap.revision < this.state.snapshot.revision) return;
        if (this.connectDeadline !== null) { this.deps.cancel(this.connectDeadline); this.connectDeadline = null; }
        this.clearSnapshotDeadline();
        if(this.operationError && (this.operationError.runId!==snap.runId || this.operationError.generation!==snap.generation || this.operationError.identity!==(snap.game?.identity??'')))this.operationError=null;
        if(this.connectedAt===null)this.connectedAt=this.now();

        this.publish({ connected: true, snapshot: snap, notice: this.pending && this.state.busy ? this.state.notice : this.operationError?.notice ?? snap.notice ?? '' });
        if(message.probeId===this.probe?.id&&this.probe)this.finishProbe(snap);
      } else if((message.type==='hud'||message.type==='hud-ready')&&this.passiveConsumers.has('hud')&&boot.hudProtocol===1){
        if(message.type==='hud-ready'){if(this.visible)this.requestSnapshot(socket,boot.runId);else this.pauseSnapshotDemand();}
        for(const listener of this.extraListeners){try{listener(message);}catch{}}
      } else if((message.type==='wallpaper'||message.type==='wallpaper-ready')&&this.passiveConsumers.has('wallpaper')&&boot.wallpaperProtocol===1){
        const pending=this.wallpaperDeadline;
        if(pending?.socket===socket&&pending.runId===boot.runId&&message.contextSeq===pending.seq&&typeof message.enabled==='boolean'&&
           (message.appId===undefined||message.appId===pending.appId))this.clearWallpaperDeadline();
        for(const listener of this.extraListeners){try{listener(message);}catch{}}
      } else if((message.type==='ymcc-updater'||message.type==='ymcc-updater-result'||message.type==='ymcc-updater-ready')&&this.passiveConsumers.has('updater')&&boot.updaterProtocol===1){
        for(const listener of this.extraListeners){try{listener(message);}catch{}}
      } else if (message.type === 'reply' && this.pending && message.id === this.pending.id) {
        // JSON truthiness must not turn a malformed failure into a success or
        // place objects in the React notice. Optional null text remains absent.
        if (typeof message.ok !== 'boolean' || (message.notice != null && typeof message.notice !== 'string') ||
            (message.error != null && typeof message.error !== 'string')) { this.disconnect('镜像操作回执格式错误；结果未确认，不自动重放'); return; }
        const pending = this.pending; this.pending = null; this.deps.cancel(pending.timer);
        const error=message.ok?null:(message.error??'操作失败');
        this.operationError=error&&this.state.snapshot?{notice:error,runId:boot.runId,generation:this.state.snapshot.generation,identity:this.state.snapshot.game?.identity??''}:null;
        this.publish({ busy: false, notice: message.ok ? (message.notice ?? '') : error });
        if (message.ok) pending.resolve(message.result); else pending.reject(new Error(message.error ?? '操作失败'));
      }
    });
    let lost = false;
    const closed = () => {
      if (epoch !== this.epoch || lost) return;
      lost = true;
      if (this.connectDeadline !== null) { this.deps.cancel(this.connectDeadline); this.connectDeadline = null; }
      this.clearSnapshotDeadline();this.clearWallpaperDeadline();
      ++this.epoch;
      this.endConnectionHealth();this.operationError=null;this.socket = null; this.rejectPending('连接中断；不自动重放设置操作');
      this.publish({ connected: false, snapshot: null, notice: '连接已中断' });
      this.recoverConnection();
    };
    socket.addEventListener('close', closed);
    this.connectDeadline = this.deps.schedule(() => {
      if (epoch !== this.epoch || lost) return;
      this.connectDeadline = null;
      try { socket.close(); } catch {}
      closed();
    }, 10000);
  }
  mutate(command: MirrorCommand, args: Record<string, unknown>, expected?: MirrorSnapshot): Promise<unknown> {
    const snap = this.state.snapshot;
    const independentFan = (command === 'fan.setEnabled' || command === 'fan.setPreset' || command === 'fan.setChoice')
      && snap?.actions?.fan === true && snap.fan.canToggle === true;
    if (!this.visible || !this.state.connected || !this.socket || !snap || (!snap.ready && !independentFan) || this.pending) return Promise.reject(new Error('当前不可操作'));
    if (expected && (expected.runId !== snap.runId || expected.generation !== snap.generation || expected.revision !== snap.revision ||
      ((command === 'game.setField' || command === 'frame.setField' || command === 'performance.setControls') && expected.game?.identity !== snap.game?.identity))) {
      this.publish({ notice: '调节时状态已变化，请确认当前游戏后重试' });
      return Promise.reject(new Error('调节快照已变化'));
    }
    if (command === 'game.setField' && !snap.game) return Promise.reject(new Error('尚未确认当前游戏'));
    if (snap.actions && (command === 'audioOptimization.setEnabled' ? !snap.actions.audioOptimization : command === 'performance.setControls' ? !snap.actions.performance : command === 'game.setField' ? !snap.actions.game : command === 'frame.setField' ? !snap.actions.frames : command === 'touchpad.setField' ? !snap.actions.touchpads : command === 'global.setField' ? !snap.actions.global : command === 'monitor.setEnabled' ? !snap.actions.monitor : !snap.actions.fan)) return Promise.reject(new Error('该镜像操作尚不可用'));
    if(command==='game.setField'&&args.field==='profile'&&(Object.keys(args).length!==2||!['dedicated','global'].includes(args.value as string)||!snap.game?.fields.profile?.supported||!snap.game.fields.profile.sourceRevision))return Promise.reject(new Error('当前游戏专属配置暂不可修改'));
    if(command === 'game.setField' && args.field === 'trainer' && (Object.keys(args).length!==2 || args.value!=='open' || !snap.trainer?.available || snap.trainer.pending))return Promise.reject(new Error('未找到修改器'));
    if(command === 'audioOptimization.setEnabled'&&(!snap.actions?.audioOptimization||!snap.audioOptimization?.known||Object.keys(args).length!==1||typeof args.enabled!=='boolean'))return Promise.reject(new Error('音质优化状态待确认'));
    if(command === 'monitor.setEnabled' && (!snap.monitor || !snap.actions?.monitor || Object.keys(args).length!==1 || typeof args.enabled!=='boolean'))return Promise.reject(new Error('监控总开关当前不可用'));
    if(command==='performance.setControls'&&(!snap.performance||!snap.actions?.performance||Object.keys(args).length!==1||!performancePatchAllowed(args.patch as Record<string,unknown>,snap.performance)))return Promise.reject(new Error('性能调度当前不可修改'));
    if((command==='game.setField'||command==='global.setField')&&['padPersona','gyroEnabled','gyroPreset'].includes(String(args.field))&&snap.virtualController?.locked)return Promise.reject(new Error('手柄切换处理中，请稍候'));
    const speedRequest = command === 'game.setField' && args.field === 'speedFactor';
    this.operationError=null;
    const transport=this.socket,transportEpoch=this.epoch,transportBinding=this.binding;
    const ownsTransport=()=>!this.disposed&&this.socket===transport&&this.epoch===transportEpoch&&this.binding===transportBinding;
    const sequence = ++this.sequence;
    const id = `${this.deps.clientId}:${sequence}`;
    const request = { command, args: structuredClone(args), id, clientId:this.deps.clientId, sequence, runId: snap.runId, generation: snap.generation,
      revision: snap.revision, identity: command === 'game.setField' ? snap.game!.identity : (command === 'frame.setField'||command === 'performance.setControls') ? snap.game?.identity??'' : command === 'global.setField' ? '' : undefined };
    return new Promise((resolve, reject) => {
      const timer = this.deps.schedule(() => {
        if (this.pending?.id !== id) return;
        this.pending = null;
        const retained=command==='game.setField'&&args.field==='losslessScaling'?structuredClone(this.state.snapshot):null;
        if(retained?.game?.fields.losslessScaling)retained.game.fields.losslessScaling={...retained.game.fields.losslessScaling,value:'unknown',supported:false};
        this.publish({ busy: false, snapshot:retained, notice: speedRequest ? '游戏变速结果未确认（等待已达 2 秒）；正在重新读取状态，不自动重试' : '结果未确认；正在重新读取状态，不重发操作' });
        try { if(ownsTransport()&&this.visible&&this.pending===null)this.requestSnapshot(transport,snap.runId); } catch {}
        reject(new Error('操作结果未确认'));
      }, command==='audioOptimization.setEnabled'?40000:speedRequest ? 2000 : (command === 'game.setField' && args.field === 'losslessScaling' || command === 'monitor.setEnabled') ? (command === 'monitor.setEnabled' ? 30000 : 18000) : (command==='performance.setControls'||command==='game.setField'&&args.field==='profile')?15000:4000);
      const ownedPending={id,timer,resolve,reject}; this.pending=ownedPending; this.publish({ busy: true, notice:speedRequest?'游戏变速正在处理，等待执行结果（最多 2 秒）':command==='game.setField'&&args.field==='losslessScaling'?'LosslessScaling 正在处理；等待配置与程序状态确认，最长 15 秒，不自动重复操作':this.state.notice });
      // Busy listeners may synchronously hide Content or replace the broker.
      // Fence both the first send and later failure/timeout work to this lease.
      if(this.pending!==ownedPending||!ownsTransport()){
        if(this.pending===ownedPending)this.rejectPending('连接已变化，本次操作未发送');
        return;
      }
      try { transport.send(JSON.stringify(request)); }
      catch {
        if(this.pending!==ownedPending||!ownsTransport())return;
        this.disconnect('发送失败，结果未确认；正在重新连接'); this.recoverConnection();
      }
    });
  }
  dispose() { this.disposed = true; this.visibleOwners.clear(); this.visible = false; this.passive = false; this.passiveConsumers.clear(); this.disconnect('插件已卸载'); this.listeners.clear(); this.connectionListeners.clear(); this.extraListeners.clear(); }
}
