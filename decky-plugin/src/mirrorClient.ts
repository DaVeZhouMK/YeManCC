// Native injects the short-lived credential into the Steam shared context.
// No unauthenticated localhost discovery, persisted credentials or polling.
import { validSteamObservation, type SteamRunningObservation } from './steamRunningContext';
export interface Choice { data: string; label: string; disabled?: boolean; }
export interface MirrorField { value: string; choices: Choice[]; supported: boolean; inherited?: boolean; }
export type MirrorCommand = 'game.setField' | 'fan.setEnabled' | 'fan.setPreset' | 'frame.setField' | 'touchpad.setField' | 'global.setField';
export interface MirrorFrameSetting {fps:number;ceiling:number;lastFps:number;}
export interface MirrorSnapshot {
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
  fan: { supported: boolean; enabled: boolean; preset: string; choices: Choice[]; canToggle?:boolean };
  notice?: string;
  actions?: { fan:boolean; game:boolean; frames?:boolean; touchpads?:boolean; global?:boolean };
  steam?:SteamRunningObservation;
}
export interface Bootstrap { endpoint: string; token: string; runId: string; }
export interface ClientState { connected: boolean; snapshot: MirrorSnapshot | null; notice: string; busy: boolean; }
export interface ClientDeps {
  bootstrap: () => Bootstrap | undefined;
  socket: (url: string) => WebSocket;
  schedule: (action: () => void, delay: number) => ReturnType<typeof setTimeout>;
  cancel: (handle: ReturnType<typeof setTimeout>) => void;
  clientId: string;
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
    if (fields.length > 16 || !fields.every(field => field && typeof field.value === 'string' && typeof field.supported === 'boolean' && (field.inherited === undefined || typeof field.inherited === 'boolean') && validChoices(field.choices))) return false;
  }
  if(value.global!==undefined&&(!value.global||!value.global.fields||typeof value.global.fields!=='object'||Array.isArray(value.global.fields)||Object.keys(value.global.fields).length>8||!Object.values(value.global.fields).every((f:any)=>f&&typeof f.value==='string'&&typeof f.supported==='boolean'&&validChoices(f.choices))))return false;
  if(value.powerSource !== undefined && value.powerSource !== null && !['ac', 'dc'].includes(value.powerSource)) return false;
  if(value.provenance!==undefined && (!value.provenance || !['ymcc-native','unconfirmed'].includes(value.provenance.kind) || (value.provenance.kind==='ymcc-native' && (!Number.isSafeInteger(value.provenance.pid) || value.provenance.pid<=0))))return false;
  if(value.steam!==undefined&&!validSteamObservation(value.steam))return false;
  if(value.frames!==undefined && (!value.frames || typeof value.frames.dedicated!=='boolean' || !['ac','dc'].every(side=>{const s=value.frames.pair?.[side];return s && ['fps','ceiling','lastFps'].every(k=>Number.isSafeInteger(s[k])&&s[k]>=0&&s[k]<=300);})))return false;
  if(value.touchpads!==undefined && (!value.touchpads || typeof value.touchpads.persona!=='string' || !value.touchpads.fields || typeof value.touchpads.fields!=='object' || Array.isArray(value.touchpads.fields) || Object.keys(value.touchpads.fields).length>8 || !Object.values(value.touchpads.fields).every((f:any)=>f && typeof f.value==='string' && typeof f.supported==='boolean' && validChoices(f.choices))))return false;
  if (value.actions !== undefined && (!value.actions || typeof value.actions.fan !== 'boolean' || typeof value.actions.game !== 'boolean' || value.actions.frames!==undefined&&typeof value.actions.frames!=='boolean' || value.actions.touchpads!==undefined&&typeof value.actions.touchpads!=='boolean' || value.actions.global!==undefined&&typeof value.actions.global!=='boolean')) return false;
  if (value.fan.canToggle !== undefined && typeof value.fan.canToggle !== 'boolean') return false;
  return value.notice === undefined || typeof value.notice === 'string';
}
export class MirrorClient {
  private visible = false;
  private disposed = false;
  private epoch = 0;
  private attempt = 0;
  private sequence = 0;
  private binding: Bootstrap | null = null;
  private socket: WebSocket | null = null;
  private retry: ReturnType<typeof setTimeout> | null = null;
  private connectDeadline: ReturnType<typeof setTimeout> | null = null;
  private pending: null | { id: string; timer: ReturnType<typeof setTimeout>; resolve: (value: unknown) => void; reject: (error: Error) => void } = null;
  private operationError: null | { notice:string;runId:string;generation:number;identity:string } = null;
  private listeners = new Set<(state: ClientState) => void>();
  private state: ClientState = { connected: false, snapshot: null, notice: '等待 YMCC 镜像连接', busy: false };
  constructor(private deps: ClientDeps) {}
  subscribe(listener: (state: ClientState) => void) { this.listeners.add(listener); listener(this.snapshot()); return () => { this.listeners.delete(listener); }; }
  snapshot(): ClientState { return structuredClone(this.state); }
  private publish(patch: Partial<ClientState>) {
    this.state = { ...this.state, ...patch };
    for (const listener of this.listeners) { try { listener(this.snapshot()); } catch {} }
  }
  setVisible(visible: boolean) {
    if (this.disposed || visible === this.visible) return;
    this.visible = visible;
    if (visible) { this.attempt = 0; this.connect(); }
    else this.disconnect('侧栏已隐藏');
  }
  bootstrapChanged(): void {
    if (!this.visible || this.disposed) return;
    const current = this.deps.bootstrap();
    const same = !!current && !!this.binding && current.endpoint === this.binding.endpoint &&
      current.runId === this.binding.runId && current.token === this.binding.token;
    if (same && (this.socket || this.retry !== null)) return;
    this.disconnect('YMCC 会话连接已更新，正在重新读取状态');
    this.attempt = 0; this.connect();
  }
  private rejectPending(reason: string) {
    if (this.pending) { const pending = this.pending; this.pending = null; this.deps.cancel(pending.timer); pending.reject(new Error(reason)); }
    this.publish({ busy: false });
  }
  private disconnect(reason: string) {
    this.operationError=null;
    ++this.epoch;
    if (this.retry !== null) { this.deps.cancel(this.retry); this.retry = null; }
    if (this.connectDeadline !== null) { this.deps.cancel(this.connectDeadline); this.connectDeadline = null; }
    const old = this.socket; this.socket = null; old?.close();
    this.rejectPending('连接已结束，操作结果需重新确认');
    this.publish({ connected: false, snapshot: null, notice: reason });
  }
  private requestSnapshot(socket:WebSocket,runId:string) {
    let observation:SteamRunningObservation|undefined;
    if(this.deps.steam){try{const value=this.deps.steam();observation=validSteamObservation(value)?value:{availability:'unavailable'};}catch{observation={availability:'unavailable'};}}
    socket.send(JSON.stringify({command:'snapshot',runId,clientId:this.deps.clientId,...(observation?{steam:observation}:{})}));
  }
  private connect() {
    if (!this.visible || this.disposed || this.socket) return;
    const raw = this.deps.bootstrap();
    const boot = raw ? { endpoint:raw.endpoint,token:raw.token,runId:raw.runId } : undefined;
    const port = typeof boot?.endpoint === 'string' ? /^ws:\/\/127\.0\.0\.1:(\d{1,5})\/mirror$/.exec(boot.endpoint)?.[1] : undefined;
    if (!boot || !port || Number(port) < 1 || Number(port) > 65535 || typeof boot.token !== 'string' || !boot.token || boot.token.length > 128 ||
      typeof boot.runId !== 'string' || !boot.runId || boot.runId.length > 64) {
      this.publish({ connected: false, snapshot: null, notice: 'YMCC 尚未提供镜像连接；不会扫描端口或启动额外后台' }); return;
    }
    this.binding = boot;
    const epoch = ++this.epoch;
    let socket: WebSocket;
    try { socket = this.deps.socket(`${boot.endpoint}?token=${encodeURIComponent(boot.token)}`); }
    catch { this.publish({ notice: '镜像连接失败，请重新打开侧栏' }); return; }
    this.socket = socket;
    socket.addEventListener('open', () => {
      if (epoch !== this.epoch || !this.visible) return;
      this.requestSnapshot(socket,boot.runId);
    });
    socket.addEventListener('message', (event: MessageEvent) => {
      if (epoch !== this.epoch || !this.visible || typeof event.data !== 'string') return;
      if (event.data.length > 65536) { this.disconnect('镜像消息超限'); return; }
      let message: any;
      try { message = JSON.parse(event.data); } catch { this.disconnect('镜像消息格式错误'); return; }
      if (message.runId !== boot.runId) { this.disconnect('YMCC 会话已变更，请重新打开侧栏'); return; }
      if (message.type === 'snapshot') {
        const snap = message.snapshot as MirrorSnapshot;
        if (!validSnapshot(snap, boot.runId)) { this.disconnect('镜像身份无效'); return; }
        if (this.state.snapshot && snap.revision < this.state.snapshot.revision) return;
        if (this.connectDeadline !== null) { this.deps.cancel(this.connectDeadline); this.connectDeadline = null; }
        if(this.operationError && (this.operationError.runId!==snap.runId || this.operationError.generation!==snap.generation || this.operationError.identity!==(snap.game?.identity??'')))this.operationError=null;
        this.attempt = 0; this.publish({ connected: true, snapshot: snap, notice: this.operationError?.notice ?? snap.notice ?? '' });
      } else if (message.type === 'reply' && this.pending && message.id === this.pending.id) {
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
      ++this.epoch;
      const recoveryEpoch = this.epoch;
      this.operationError=null;this.socket = null; this.rejectPending('连接中断；不自动重放设置操作');
      this.publish({ connected: false, snapshot: null, notice: '连接已中断，正在有限重连' });
      const delays = [250, 1000, 3000];
      if (this.visible && !this.disposed && this.attempt < delays.length) {
        this.retry = this.deps.schedule(() => { this.retry = null; if (recoveryEpoch === this.epoch) this.connect(); }, delays[this.attempt++]);
      } else this.publish({ notice: '连接已中断，请重新打开侧栏' });
    };
    socket.addEventListener('close', closed);
    this.connectDeadline = this.deps.schedule(() => {
      this.connectDeadline = null;
      if (epoch !== this.epoch || lost) return;
      socket.close(); closed();
    }, 10000);
  }
  mutate(command: MirrorCommand, args: Record<string, unknown>, expected?: MirrorSnapshot): Promise<unknown> {
    const snap = this.state.snapshot;
    if (!this.visible || !this.state.connected || !this.socket || !snap?.ready || this.pending) return Promise.reject(new Error('当前不可操作'));
    if (expected && (expected.runId !== snap.runId || expected.generation !== snap.generation || expected.revision !== snap.revision ||
      ((command === 'game.setField' || command === 'frame.setField') && expected.game?.identity !== snap.game?.identity))) {
      this.publish({ notice: '调节时状态已变化，请确认当前游戏后重试' });
      return Promise.reject(new Error('调节快照已变化'));
    }
    if (command === 'game.setField' && !snap.game) return Promise.reject(new Error('尚未确认当前游戏'));
    if (snap.actions && (command === 'game.setField' ? !snap.actions.game : command === 'frame.setField' ? !snap.actions.frames : command === 'touchpad.setField' ? !snap.actions.touchpads : command === 'global.setField' ? !snap.actions.global : !snap.actions.fan)) return Promise.reject(new Error('该镜像操作尚不可用'));
    this.operationError=null;
    const sequence = ++this.sequence;
    const id = `${this.deps.clientId}:${sequence}`;
    const request = { command, args: structuredClone(args), id, clientId:this.deps.clientId, sequence, runId: snap.runId, generation: snap.generation,
      revision: snap.revision, identity: command === 'game.setField' ? snap.game!.identity : command === 'frame.setField' ? snap.game?.identity??'' : command === 'global.setField' ? '' : undefined };
    return new Promise((resolve, reject) => {
      const timer = this.deps.schedule(() => {
        if (this.pending?.id !== id) return;
        this.pending = null; this.publish({ busy: false, snapshot: null, notice: '结果未确认；正在重新读取状态，不重发操作' });
        try { if(this.socket)this.requestSnapshot(this.socket,snap.runId); } catch {}
        reject(new Error('操作结果未确认'));
      }, command === 'game.setField' && args.field === 'losslessScaling' ? 30000 : 4000);
      this.pending = { id, timer, resolve, reject }; this.publish({ busy: true });
      try { this.socket!.send(JSON.stringify(request)); }
      catch { this.rejectPending('发送失败，结果未确认'); }
    });
  }
  dispose() { this.disposed = true; this.visible = false; this.disconnect('插件已卸载'); this.listeners.clear(); }
}
