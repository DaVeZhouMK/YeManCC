// Authenticated mirror relay. Original business owners implement all actions;
// private per-snapshot admission never enters the public/client wire model.
import { validSteamObservation, type SteamRunningObservation } from '../../decky-plugin/src/steamRunningContext';
import type { MirrorSnapshot } from '../../decky-plugin/src/mirrorClient';
import type { GameMirrorAdmission } from './deckyGameActions';
export interface MirrorControlAdmission {frames?:string;touchpads?:string;}
export type RelayReadSnapshot = Omit<MirrorSnapshot,'runId'|'revision'> & {gameAdmission?:GameMirrorAdmission;controlAdmission?:MirrorControlAdmission};
export interface MirrorPeer { connection: string; runId: string; connected?: boolean; revision?: number; }
export interface MirrorRequest extends MirrorPeer { request: { command: string; runId: string; id?: string; clientId?: string; [key: string]: unknown }; }
export interface RelayMutationContext { generation:number; revision:number; gameIdentity?:string; gameAdmission?:GameMirrorAdmission; controlAdmission?:MirrorControlAdmission; checkpoint:() => void; }
export interface RelayDeps {
  read: () => Promise<RelayReadSnapshot>;
  observe: (invalidate: () => void) => (() => void);
  reply: (connection: string, message: unknown) => Promise<boolean>;
  observeSteam?: (observation:SteamRunningObservation) => boolean;
  execute?: (command:string,args:Record<string,unknown>,context:RelayMutationContext) => Promise<{saved:boolean;applied:boolean;pending?:boolean;notice:string}>;
}
export class ReadOnlyMirrorRelay {
  private peer: MirrorPeer | null = null;
  private epoch = 0;
  private sourceEpoch = 0;
  private revision = 0;
  private dirty = true;
  private requested = false;
  private queued = false;
  private stopObserve: (() => void) | null = null;
  private cache: MirrorSnapshot | null = null;
  private controlAdmissions = new WeakMap<MirrorSnapshot,MirrorControlAdmission>();
  private gameAdmissions = new WeakMap<MirrorSnapshot,GameMirrorAdmission>();
  private readTask: Promise<MirrorSnapshot | null> | null = null;
  private disposed = false;
  private mutationBusy = false;
  private clients = new Map<string,{highest:number;receipts:Map<number,{signature:string;response:Promise<Record<string,unknown>>}>}>();
  private clientRun = '';
  constructor(private deps: RelayDeps) {}
  private validPeer(peer: MirrorPeer): boolean {
    return typeof peer.connection === 'string' && /^\d{1,20}$/.test(peer.connection) && typeof peer.runId === 'string' && /^[a-f0-9]{32}$/.test(peer.runId);
  }
  private matches(peer: MirrorPeer): boolean { return !!this.peer && peer.connection === this.peer.connection && peer.runId === this.peer.runId; }
  connected(peer: MirrorPeer): void {
    if (this.disposed || !this.validPeer(peer)) return;
    if (peer.connected === false) { if (this.matches(peer)) this.detach(); return; }
    if (this.matches(peer)) {
      if (Number.isSafeInteger(peer.revision) && peer.revision! > this.revision) {
        this.revision = peer.revision!; this.cache = null; this.dirty = true; this.sourceEpoch++;
      }
      return;
    }
    if (this.clientRun !== peer.runId) { this.clients.clear(); this.clientRun = peer.runId; }
    this.detach(); this.peer = { connection: peer.connection, runId: peer.runId };
    this.revision = Number.isSafeInteger(peer.revision) && peer.revision! > 0 ? peer.revision! : 0;
    try { this.stopObserve = this.deps.observe(() => this.invalidate()); }
    catch { this.detach(); }
  }
  invalidate(): void {
    if (this.disposed || !this.peer) return;
    this.dirty = true; this.sourceEpoch++;
    // The original save notifies before its action promise settles. Do not
    // publish an intermediate revision and then a second post-action revision.
    // Keep it dirty; the existing mutation finally invalidates once after commit.
    if (!this.requested || this.queued || this.mutationBusy) return;
    this.queued = true;
    queueMicrotask(() => { this.queued = false; if (this.peer && this.requested && !this.disposed && !this.mutationBusy) void this.push(); });
  }
  private detach(): void {
    this.epoch++; this.sourceEpoch++; this.stopObserve?.(); this.stopObserve = null;
    this.peer = null; this.cache = null; this.readTask = null; this.dirty = true; this.requested = false; this.revision = 0;
  }
  private snapshot(): Promise<MirrorSnapshot | null> {
    if (!this.peer || this.disposed) return Promise.resolve(null);
    if (!this.dirty && this.cache) return Promise.resolve(this.cache);
    if (this.readTask) return this.readTask;
    const epoch = this.epoch; const peer = this.peer;
    let task: Promise<MirrorSnapshot | null>;
    task = (async () => {
      for (let attempt = 0; attempt < 3; attempt++) {
        const sourceEpoch = this.sourceEpoch;
        let data: RelayReadSnapshot;
        try { data = await this.deps.read(); }
        catch { data = { generation: 0, ready: false, game: null, fan: { supported: false, enabled: false, preset: '', choices: [] }, notice: 'YMCC 状态读取失败；未修改任何配置' }; }
        if (this.disposed || epoch !== this.epoch || !this.matches(peer)) return null;
        if (sourceEpoch !== this.sourceEpoch) continue;
        const {gameAdmission,controlAdmission,...publicData}=data;
        this.cache = { ...structuredClone(publicData), runId: peer.runId, revision: ++this.revision };
        if(controlAdmission)this.controlAdmissions.set(this.cache,structuredClone(controlAdmission));
        if(gameAdmission)this.gameAdmissions.set(this.cache,structuredClone(gameAdmission));
        this.dirty = false; return this.cache;
      }
      if (epoch !== this.epoch) return null;
      return { runId: peer.runId, revision: ++this.revision, generation: 0, ready: false, game: null,
        fan: { supported: false, enabled: false, preset: '', choices: [] }, notice: '状态正在切换；请稍后重新打开侧栏' };
    })().finally(() => { if (this.readTask === task) this.readTask = null; });
    this.readTask = task; return task;
  }
  private async push(): Promise<void> {
    const peer = this.peer; const epoch = this.epoch; if (!peer) return;
    const snapshot = await this.snapshot();
    if (snapshot && epoch === this.epoch && this.matches(peer)) await this.deps.reply(peer.connection, { type: 'snapshot', runId: peer.runId, snapshot }).catch(() => false);
  }
  async request(event: MirrorRequest): Promise<void> {
    if (this.disposed || !this.validPeer(event) || !event.request || event.request.runId !== event.runId) return;
    event = { ...event, request: structuredClone(event.request) };
    // Native generated this event only after authenticated handshake. It also rescues renderer remount.
    if (!this.peer) this.connected(event);
    if (!this.matches(event)) return;
    if (event.request.command === 'snapshot') {
      if(event.request.steam!==undefined){
        if(!validSteamObservation(event.request.steam))return;
        try { if(this.deps.observeSteam?.(structuredClone(event.request.steam))) { this.dirty=true;this.sourceEpoch++; } }
        catch { return; }
      }
      this.requested = true; await this.push(); return;
    }
    if (typeof event.request.id !== 'string' || event.request.id.length > 128) return;
    const failure = async (error:string) => {
      if (this.matches(event)) await this.deps.reply(event.connection, {type:'reply',runId:event.runId,id:event.request.id,ok:false,error}).catch(() => false);
    };
    if (!this.deps.execute) { await failure('只读镜像阶段：业务动作尚未接线，未执行设置或硬件操作'); return; }
    const request = event.request;
    const command = request.command;
    const args = request.args;
    const clientId = request.clientId;
    const sequence = request.sequence as number;
    if ((command !== 'game.setField' && command !== 'fan.setPreset' && command !== 'fan.setEnabled' && command !== 'frame.setField' && command !== 'touchpad.setField') ||
      typeof clientId !== 'string' || !/^[a-zA-Z0-9_-]{1,64}$/.test(clientId) || !Number.isSafeInteger(sequence) || sequence <= 0 ||
      request.id !== `${clientId}:${sequence}` || !args || typeof args !== 'object' || Array.isArray(args)) {
      await failure('镜像请求格式无效，未执行操作'); return;
    }
    const allowedKeys = ['command','runId','id','clientId','sequence','generation','revision','args','identity'];
    if (Object.keys(request).some(key => !allowedKeys.includes(key)) ||
      command === 'fan.setEnabled' && (Object.keys(args).length !== 1 || typeof (args as any).enabled !== 'boolean') ||
      command === 'fan.setPreset' && (Object.keys(args).length !== 1 || !['soft','balanced','aggressive'].includes((args as any).preset)) ||
      ['game.setField','frame.setField','touchpad.setField'].includes(command) && (Object.keys(args).length !== 2 || typeof (args as any).field !== 'string' || typeof (args as any).value !== 'string')) {
      await failure('镜像参数不在白名单中，未执行操作'); return;
    }
    // Keep exact packet identity; duplicate mutation returns its prior outcome, never executes twice.
    const signature = JSON.stringify(request);
    if (signature.length > 2048) { await failure('镜像修改请求过大，未执行操作'); return; }
    let client = this.clients.get(clientId);
    const prior = client?.receipts.get(sequence);
    if (prior) {
      if (signature !== prior.signature) { await failure('请求编号已用于不同操作，未重放'); return; }
      const response = await prior.response;
      if (this.matches(event)) await this.deps.reply(event.connection,response).catch(() => false);
      return;
    }
    if (client && sequence <= client.highest) { await failure('该操作已处理或过期，请重新读取状态'); return; }
    if (this.mutationBusy) { await failure('已有镜像操作正在执行，请稍候'); return; }
    if (!client && this.clients.size >= 64) { await failure('本会话客户端数量达到上限，请重连 YMCC 会话'); return; }
    // A request is accepted only against an authoritative snapshot that this peer could have seen.
    const epoch = this.epoch; const snapshot = await this.snapshot();
    if (!snapshot || !this.matches(event) || epoch !== this.epoch) return;
    if (!snapshot.ready || !Number.isSafeInteger(request.generation) || request.generation !== snapshot.generation ||
        !Number.isSafeInteger(request.revision) || request.revision !== snapshot.revision) {
      await failure('状态已变化，请重新读取后操作'); return;
    }
    if (command === 'game.setField' ? !snapshot.actions?.game : command === 'frame.setField' ? !snapshot.actions?.frames : command === 'touchpad.setField' ? !snapshot.actions?.touchpads : !snapshot.actions?.fan) {
      await failure('该镜像功能尚未接线，未修改配置'); return;
    }
    if (command === 'game.setField' && (!snapshot.game || request.identity !== snapshot.game.identity)) {
      await failure('当前游戏身份已变化，未执行操作'); return;
    }
    if(command==='frame.setField' && request.identity!==(snapshot.game?.identity??'')){await failure('帧率目标已变化，未执行操作');return;}
    if (command === 'fan.setPreset' && !snapshot.fan.supported || command === 'fan.setEnabled' && !snapshot.fan.canToggle) {
      await failure('风扇操作当前不可用'); return;
    }
    // Another request may have entered while this one was awaiting a dirty source read.
    const raced = this.clients.get(clientId)?.receipts.get(sequence);
    if (raced) {
      if (signature !== raced.signature) { await failure('请求编号已用于不同操作，未重放'); return; }
      const response = await raced.response;
      if (this.matches(event)) await this.deps.reply(event.connection,response).catch(() => false);
      return;
    }
    if (this.mutationBusy) { await failure('已有镜像操作正在执行，请稍候'); return; }
    client ??= {highest:0,receipts:new Map()}; this.clients.set(clientId,client);
    const checkpoint = () => {
      if (this.disposed || epoch !== this.epoch || !this.matches(event)) throw new Error('MIRROR_CONTEXT_EXPIRED');
    };
    this.mutationBusy = true; client.highest = sequence;
    const receipt = Promise.resolve().then(async () => {
      try {
        checkpoint();
        const result = await this.deps.execute!(command,structuredClone(args) as Record<string,unknown>,
          {generation:snapshot.generation,revision:snapshot.revision,gameIdentity:command === 'game.setField'||command === 'frame.setField' ? snapshot.game?.identity??'' : undefined,gameAdmission:command === 'game.setField'||command === 'frame.setField' ? this.gameAdmissions.get(snapshot) : undefined,controlAdmission:this.controlAdmissions.get(snapshot),checkpoint});
        return {type:'reply',runId:event.runId,id:request.id,ok:true,result,notice:result.notice};
      } catch(error) {
        const sourceChanged=(error as {message?:unknown})?.message==='GAME_MIRROR_SOURCE_CHANGED';
        return {type:'reply',runId:event.runId,id:request.id,ok:false,error:sourceChanged
          ? 'YMCC 原方案已变化；未保存，请查看更新后的侧栏再选择，不自动重试'
          : '镜像操作未确认；请查看 YMCC 当前状态，不自动重放'};
      } finally { this.mutationBusy = false; if (this.matches(event)) this.invalidate(); }
    });
    client.receipts.set(sequence,{signature,response:receipt});
    // Keep a high-water mark after evicting old completed outcomes, so they cannot replay.
    if (client.receipts.size > 32) client.receipts.delete(client.receipts.keys().next().value!);
    const response = await receipt;
    if (this.matches(event)) await this.deps.reply(event.connection,response).catch(() => false);
  }
  dispose(): void { this.disposed = true; this.detach(); }
}
