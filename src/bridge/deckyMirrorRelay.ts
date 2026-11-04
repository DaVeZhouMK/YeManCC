// Authenticated mirror relay. Original business owners implement all actions;
// private per-snapshot admission never enters the public/client wire model.
import { validSteamObservation, type SteamRunningObservation } from '../../decky-plugin/src/steamRunningContext';
import type { MirrorSnapshot } from '../../decky-plugin/src/mirrorClient';
import type { GameMirrorAdmission } from './deckyGameActions';
export interface MirrorControlAdmission {audioOptimization?:string;performance?:string;frames?:string;touchpads?:string;global?:string;monitor?:string;trainer?:string;}
export type RelayReadSnapshot = Omit<MirrorSnapshot,'runId'|'revision'> & {gameAdmission?:GameMirrorAdmission;controlAdmission?:MirrorControlAdmission};
export interface MirrorPeer { connection: string; runId: string; connected?: boolean; revision?: number; connectionRevision?:number; }
export interface MirrorRequest extends MirrorPeer { request: { command: string; runId: string; id?: string; clientId?: string; [key: string]: unknown }; }
export interface RelayMutationContext { nativePeer?:{connection:string;runId:string;connectionRevision:number}; generation:number; revision:number; gameIdentity?:string; gameAdmission?:GameMirrorAdmission; controlAdmission?:MirrorControlAdmission; checkpoint:() => void; }
export type MirrorReadStage = 'power'|'powerSource'|'schedule'|'performance'|'topology'|'gyro'|'frames'|'touchpads'|'global'|'monitor'|'input';
export interface MirrorReadTrace { measure<T>(stage: MirrorReadStage, read: () => Promise<T>): Promise<T>; }
export interface RelayDeps {
  read: (trace?: MirrorReadTrace) => Promise<RelayReadSnapshot>;
  diagnostic?: (event: string, detail: Record<string, unknown>) => void;
  now?: () => number;
  snapshotDemand?:()=>void;
  schedule?:(action:()=>void,delay:number)=>ReturnType<typeof setTimeout>;
  cancel?:(timer:ReturnType<typeof setTimeout>)=>void;
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
  private snapshotDemandPaused = false;
  private demandEpoch = 0;
  private queued = false;
  private stopObserve: (() => void) | null = null;
  private cache: MirrorSnapshot | null = null;
  // Bounded receipts offered to this authenticated peer; never client-supplied source data.
  private offeredSnapshots = new Map<number,{snapshot:MirrorSnapshot;pending:number;delivered:boolean}>();
  private controlAdmissions = new WeakMap<MirrorSnapshot,MirrorControlAdmission>();
  private gameAdmissions = new WeakMap<MirrorSnapshot,GameMirrorAdmission>();
  private recoveryTimer:ReturnType<typeof setTimeout>|null=null;
  private recoveryLease:object|null=null;
  private recoveryAttempt=0;
  private readFault=false;
  private freshReadRequested=false;
  private deferredSnapshot:MirrorSnapshot|null=null;
  private readTask: Promise<MirrorSnapshot | null> | null = null;
  private deliveries = new Map<string,Promise<void>>();
  private disposed = false;
  private mutationBusy = false;
  private clients = new Map<string,{highest:number;receipts:Map<number,{signature:string;response:Promise<Record<string,unknown>>}>}>();
  private clientRun = '';
  private transportRevision = -1;
  private readSequence = 0;
  private firstDemandRead = true;
  private lastReadDiagnostic = -Infinity;
  private diagnostic(event: string, detail: Record<string, unknown>): void {
    try { this.deps.diagnostic?.(event,detail); } catch { /* Diagnostics never affect delivery. */ }
  }
  constructor(private deps: RelayDeps) {}
  private validPeer(peer: MirrorPeer): boolean {
    return typeof peer.connection === 'string' && /^\d{1,20}$/.test(peer.connection) && typeof peer.runId === 'string' && /^[a-f0-9]{32}$/.test(peer.runId);
  }
  private matches(peer: MirrorPeer): boolean { return !!this.peer && peer.connection === this.peer.connection && peer.runId === this.peer.runId; }
  connected(peer: MirrorPeer): boolean {
    if (this.disposed || !peer || !this.validPeer(peer)) return false;
    const serial=peer.connectionRevision,typed=serial!==undefined,same=this.matches(peer);
    // Legacy traffic may retain a typed peer, never replace or revive it.
    if(!typed&&this.transportRevision>=0&&!same)return false;
    if(typed){
      if(!Number.isSafeInteger(serial)||serial!<0||serial!<this.transportRevision || serial===this.transportRevision&&(!same||peer.connected===false))return false;
      this.transportRevision=serial!;
    }
    if (peer.connected === false) {
      // A newer native disconnect is global transport truth even if its ON
      // event was lost. Untyped legacy events keep the original identity fence.
      if(same||typed){this.detach();return true;}return false;
    }
    if (same) {
      if (Number.isSafeInteger(peer.revision) && peer.revision! > this.revision) {
        this.revision = peer.revision!; this.cache = null; this.dirty = true; this.sourceEpoch++;
      }
      return true;
    }
    if (this.clientRun !== peer.runId) { this.clients.clear(); this.clientRun = peer.runId; }
    this.detach(); this.peer = { connection: peer.connection, runId: peer.runId };
    this.revision = Number.isSafeInteger(peer.revision) && peer.revision! > 0 ? peer.revision! : 0;
    // A healthy HUD-only peer does not need any business-store observers.
    // Install them when an authenticated sidebar snapshot is requested.
    return true;
  }
  // Only the private native dispatcher may assert this receipt, after its
  // current-Broker check. It repairs a missed peer event; it grants no write.
  observeAuthenticatedRequestPeer(event:MirrorRequest):boolean {
    if(this.disposed||!event||!this.validPeer(event)||event.connected===false||!event.request||event.request.runId!==event.runId)return false;
    // Old native versions lack this envelope field. Keep an already matching
    // peer usable, but missing metadata cannot replace newer confirmed truth.
    if(event.connectionRevision===undefined&&this.transportRevision>=0&&!this.matches(event))return false;
    return this.connected(event)&&this.matches(event);
  }
  // Native attach may complete after this same peer explicitly hid its sidebar.
  // Respect that authenticated pause; only unknown/visible demand needs the
  // original renderer-restoration fallback. New peer identity clears the pause.
  refreshSnapshotAfterAttach(peer:MirrorPeer):Promise<void> {
    if(this.disposed || !this.matches(peer) || this.snapshotDemandPaused)return Promise.resolve();
    return this.request({...peer,request:{command:'snapshot',runId:peer.runId}});
  }
  private startObserving(): boolean {
    if (this.stopObserve) return true;
    const demandEpoch = this.demandEpoch;
    try { this.stopObserve = this.deps.observe(() => { if (demandEpoch === this.demandEpoch) this.invalidate(); }); return true; }
    catch { this.detach(); return false; }
  }
  private stopObserving(): void {
    const stop = this.stopObserve; this.stopObserve = null;
    try { stop?.(); } catch { /* Transport and recovery cleanup must still complete. */ }
  }
  private pauseDemand(): void {
    this.snapshotDemandPaused = true;
    if (this.requested || this.stopObserve) {
      this.requested = false; this.demandEpoch++; this.dirty = true; this.sourceEpoch++;
      this.stopObserving();
      this.diagnostic('snapshot-demand',{subscribe:false,demandEpoch:this.demandEpoch});
    }
    this.stopRecovery();
  }
  // Only a verified context edge or completed explicit owner action may bypass
  // the fault read's wait. Ordinary demand/events cannot renew that budget.
  invalidateFresh(contextChanged=false):void {
    if(contextChanged){this.stopRecovery();this.recoveryAttempt=0;this.deferredSnapshot=null;}
    this.freshReadRequested=true;this.invalidate();
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
  private stopRecovery():void {
    this.recoveryLease=null;
    if(this.recoveryTimer!==null){(this.deps.cancel??clearTimeout)(this.recoveryTimer);this.recoveryTimer=null;}
  }
  private recoverRead():void {
    if(this.disposed||!this.peer||!this.requested||this.mutationBusy||this.recoveryTimer!==null)return;
    // Same capped delays as the existing mirror transport. Only an incomplete
    // requested read owns this lease; healthy/hidden HUD has no business polling.
    // A long outage must not strand a field after just two fast observations.
    const delays=[250,1000,3000,10000,30000];
    const delay=delays[Math.min(this.recoveryAttempt,delays.length-1)];
    this.recoveryAttempt=Math.min(this.recoveryAttempt+1,delays.length);
    const epoch=this.epoch,demandEpoch=this.demandEpoch,lease={};this.recoveryLease=lease;
    this.recoveryTimer=(this.deps.schedule??setTimeout)(()=>{
      if(this.recoveryLease!==lease||epoch!==this.epoch||demandEpoch!==this.demandEpoch)return;
      this.recoveryTimer=null;this.recoveryLease=null;
      if(this.requested&&!this.disposed&&!this.mutationBusy){this.dirty=true;void this.push();}
    },delay);
  }
  private detach(): void {
    this.stopRecovery();this.recoveryAttempt=0;this.readFault=false;
    this.epoch++; this.sourceEpoch++; this.demandEpoch++; this.stopObserving(); this.deliveries.clear(); this.offeredSnapshots.clear();
    this.firstDemandRead=true;this.freshReadRequested=false;this.deferredSnapshot=null;this.peer = null; this.cache = null; this.readTask = null; this.dirty = true; this.requested = false; this.snapshotDemandPaused = false; this.revision = 0;
  }
  private snapshot(): Promise<MirrorSnapshot | null> {
    if (!this.peer || this.disposed) return Promise.resolve(null);
    if (!this.dirty && this.cache) return Promise.resolve(this.cache);
    if (this.readTask) return this.readTask;
    const epoch = this.epoch; const peer = this.peer;
    let task: Promise<MirrorSnapshot | null>;let unstable=false;
    task = (async () => {
      for (let attempt = 0; attempt < 3; attempt++) {
        const sourceEpoch = this.sourceEpoch;
        let data: RelayReadSnapshot;
        const readDemandEpoch=this.demandEpoch,readId=++this.readSequence,started=(this.deps.now??Date.now)(),first=this.firstDemandRead;
        this.firstDemandRead=false;
        const timings:Record<string,number>={};let failed=false,partial=false;
        const trace:MirrorReadTrace={measure:async(stage,read)=>{const begin=(this.deps.now??Date.now)();try{return await read();}catch(error){partial=true;throw error;}finally{timings[stage+'Ms']=Math.max(0,(this.deps.now??Date.now)()-begin);}}};
        if(first)this.diagnostic('snapshot-read-start',{readId,demandEpoch:readDemandEpoch});
        try { data = await this.deps.read(trace); }
        catch { failed=true;data = { generation: 0, ready: false, game: null, fan: { supported: false, enabled: false, preset: '', choices: [] }, notice: 'YMCC 状态读取失败；未修改任何配置' }; }
        const finished=(this.deps.now??Date.now)(),elapsedMs=Math.max(0,finished-started);
        const fault=failed||partial||!data.ready;
        const current=!this.disposed&&epoch===this.epoch&&readDemandEpoch===this.demandEpoch&&sourceEpoch===this.sourceEpoch;
        if(first || current&&fault!==this.readFault || (failed||partial||elapsedMs>=250) && finished-this.lastReadDiagnostic>=10000){
          this.lastReadDiagnostic=finished;
          this.diagnostic('snapshot-read-finished',{readId,demandEpoch:readDemandEpoch,elapsedMs,ok:!failed&&!partial,
            stage:failed?'failed':partial?'partial':'settled',ready:data.ready,discarded:this.disposed||epoch!==this.epoch||readDemandEpoch!==this.demandEpoch||sourceEpoch!==this.sourceEpoch,...timings});
        }
        if (this.disposed || epoch !== this.epoch || !this.matches(peer)) return null;
        if (sourceEpoch !== this.sourceEpoch) { if (!this.requested) return null; continue; }
        this.readFault=fault;
        const {gameAdmission,controlAdmission,...publicData}=data;
        this.deferredSnapshot=null;
        this.cache = { ...structuredClone(publicData), runId: peer.runId, revision: ++this.revision };
        if(controlAdmission)this.controlAdmissions.set(this.cache,structuredClone(controlAdmission));
        if(gameAdmission)this.gameAdmissions.set(this.cache,structuredClone(gameAdmission));
        // Keep a returned partial view usable for unaffected controls. The one
        // recovery lease, not each mutation, forces its next fresh observation.
        // An error or native-not-ready result is not recovery completion.
        this.dirty = failed;
        if(failed||partial||!publicData.ready)unstable=true;
        else {this.stopRecovery();this.recoveryAttempt=0;}
        return this.cache;
      }
      if (epoch !== this.epoch) return null;
      unstable=true;
      return this.deferredView();
    })().finally(() => { if (this.readTask === task) {this.readTask = null;if(unstable)this.recoverRead();} });
    this.readTask = task; return task;
  }
  private sameSliderSource(previous:MirrorSnapshot|undefined,current:MirrorSnapshot,command:string):boolean {
    if(!previous || !previous.ready || !current.ready || previous.runId!==current.runId || previous.generation!==current.generation ||
       previous.game?.identity!==current.game?.identity || previous.powerSource!==current.powerSource)return false;
    const same=(a:unknown,b:unknown)=>JSON.stringify(a)===JSON.stringify(b);
    if(command==='fan.setChoice')return previous.actions?.fan===true && current.actions?.fan===true &&
      previous.fan.canToggle===true && current.fan.canToggle===true && same(previous.fan,current.fan);
    const field=command==='performance.setControls'?'performance':command==='frame.setField'?'frames':null;
    if(!field || previous.actions?.[field]!==true || current.actions?.[field]!==true || !previous[field] || !current[field] || !same(previous[field],current[field]))return false;
    const source=this.controlAdmissions.get(previous)?.[field],fresh=this.controlAdmissions.get(current)?.[field];
    if(typeof source!=='string' || !source || source!==fresh)return false;
    // Each original owner still validates its exact private source before save.
    return true;
  }
  // This is an explicit unavailable view, never an admitted/healthy cache.
  // Churning reads may not have produced this.cache at all; repeated demand
  // must still wait on the same recovery lease instead of starting 3 more reads.
  private deferredView():MirrorSnapshot|null {
    if(!this.peer)return null;
    if(!this.deferredSnapshot){
      const previous=this.cache??{generation:0,game:null,fan:{supported:false,enabled:false,preset:'',choices:[]}};
      this.deferredSnapshot={...previous,runId:this.peer.runId,revision:++this.revision,ready:false,
        actions:{fan:false,game:false,performance:false,frames:false,touchpads:false,global:false,audioOptimization:false,monitor:false},
        fan:{...previous.fan,canToggle:false},notice:'YMCC 状态正在更新；侧栏会自动恢复'};
    }
    return this.deferredSnapshot;
  }
  private async push(probeId?:string): Promise<void> {
    const peer = this.peer; const epoch = this.epoch, demandEpoch = this.demandEpoch;
    if (!peer || !this.requested) return;
    const fresh=this.freshReadRequested;this.freshReadRequested=false;
    let snapshot:MirrorSnapshot|null;
    if(this.recoveryTimer!==null&&!fresh){
      // Both cached and never-stable sources share the one fault observation.
      snapshot=this.cache&&!this.dirty?this.cache:this.deferredView();
    }else snapshot=await this.snapshot();
    if (!snapshot || !this.requested || demandEpoch !== this.demandEpoch || epoch !== this.epoch || !this.matches(peer)) return;
    const key = epoch + ':' + snapshot.revision + ':' + (probeId??'');
    const prior = this.deliveries.get(key);
    if (prior) { await prior; return; }
    // Coalesce only concurrent deliveries; a later explicit request still gets a reply.
    let delivery:Promise<void>;
    delivery = Promise.resolve().then(async () => {
      if (this.requested && demandEpoch === this.demandEpoch && epoch === this.epoch && this.matches(peer)) {
        let offer=this.offeredSnapshots.get(snapshot.revision);
        if(!offer){offer={snapshot,pending:0,delivered:false};this.offeredSnapshots.set(snapshot.revision,offer);}
        offer.pending++;
        while(this.offeredSnapshots.size>8)this.offeredSnapshots.delete(this.offeredSnapshots.keys().next().value!);
        const sent=await this.deps.reply(peer.connection, { type: 'snapshot', runId: peer.runId, snapshot, ...(probeId?{probeId}:{}) }).catch(() => false);
        offer.pending--;offer.delivered=offer.delivered||sent;
        // Separate ordinary/probe deliveries may share this same snapshot. A
        // failed sibling cannot revoke a pending or successful delivery.
        if(!offer.delivered&&offer.pending===0&&this.offeredSnapshots.get(snapshot.revision)===offer)this.offeredSnapshots.delete(snapshot.revision);
      }
    }).finally(() => { if (this.deliveries.get(key) === delivery) this.deliveries.delete(key); });
    this.deliveries.set(key,delivery);
    await delivery;
  }
  async request(event: MirrorRequest): Promise<void> {
    if (this.disposed || !this.validPeer(event) || !event.request || event.request.runId !== event.runId) return;
    event = { ...event, request: structuredClone(event.request) };
    // Native generated this event only after authenticated handshake. It also rescues renderer remount.
    if (!this.peer) this.connected(event);
    if (!this.matches(event)) return;
    if (event.request.command === 'snapshot') {
      if(event.request.subscribe===false){this.pauseDemand();return;}
      if(event.request.steam!==undefined){
        if(!validSteamObservation(event.request.steam))return;
        try { if(this.deps.observeSteam?.(structuredClone(event.request.steam))) { this.dirty=true;this.sourceEpoch++; } }
        catch { return; }
      }
      const probeId=event.request.probeId;
      if(probeId!==undefined&&(typeof probeId!=='string'||!/^probe:[A-Za-z0-9_-]{1,80}:\d{1,16}$/.test(probeId)))return;
      if(probeId){this.dirty=true;this.sourceEpoch++;}
      if (!this.startObserving()) return;
      this.snapshotDemandPaused = false;
      const resumed=!this.requested;
      if(resumed){this.firstDemandRead=true;this.diagnostic('snapshot-demand',{subscribe:true,demandEpoch:this.demandEpoch});}
      this.requested = true;
      if(resumed){this.stopRecovery();this.recoveryAttempt=0;}
      // Ordinary overlapping requests share the current source read. Refreshing
      // demand again would discard it and repeat every business-owner read.
      // Reopen/probe/later explicit demand stays fresh; source events invalidate
      // independently, including a game/power change during the shared read.
      if((resumed || probeId || !this.readTask)&&this.recoveryTimer===null)this.deps.snapshotDemand?.();
      await this.push(probeId as string|undefined); return;
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
    if ((command !== 'game.setField' && command !== 'fan.setPreset' && command !== 'fan.setChoice' && command !== 'fan.setEnabled' && command !== 'frame.setField' && command !== 'touchpad.setField' && command !== 'global.setField' && command !== 'audioOptimization.setEnabled' && command !== 'monitor.setEnabled' && command !== 'performance.setControls') ||
      typeof clientId !== 'string' || !/^[a-zA-Z0-9_-]{1,64}$/.test(clientId) || !Number.isSafeInteger(sequence) || sequence <= 0 ||
      request.id !== `${clientId}:${sequence}` || !args || typeof args !== 'object' || Array.isArray(args)) {
      await failure('镜像请求格式无效，未执行操作'); return;
    }
    const allowedKeys = ['command','runId','id','clientId','sequence','generation','revision','args','identity'];
    if (Object.keys(request).some(key => !allowedKeys.includes(key)) ||
      (command === 'fan.setEnabled' || command === 'monitor.setEnabled' || command === 'audioOptimization.setEnabled') && (Object.keys(args).length !== 1 || typeof (args as any).enabled !== 'boolean') ||
      command === 'fan.setChoice' && (Object.keys(args).length !== 1 || !['off','soft','balanced','aggressive'].includes((args as any).choice)) ||
      command === 'fan.setPreset' && (Object.keys(args).length !== 1 || !['soft','balanced','aggressive'].includes((args as any).preset)) ||
      command==='performance.setControls'&&(Object.keys(args).length!==1||!(args as any).patch||typeof (args as any).patch!=='object'||Array.isArray((args as any).patch)) ||
      ['game.setField','frame.setField','touchpad.setField','global.setField'].includes(command) && (Object.keys(args).length !== 2 || typeof (args as any).field !== 'string' || typeof (args as any).value !== 'string')) {
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
    // Refresh authoritative data, then compare against this peer's bounded delivered receipts.
    // Only unchanged slider sources can survive an unrelated revision race; the original
    // owner still validates the fresh private source, identity, power and shared action lock.
    if(this.recoveryTimer!==null&&this.dirty&&!this.freshReadRequested){await failure('当前状态正在恢复，请等待新状态后操作；未重放');return;}
    const peerRevision=this.transportRevision; const epoch = this.epoch; const snapshot = await this.snapshot();
    if (!snapshot || !this.matches(event) || epoch !== this.epoch) return;
    const independentFan = (command === 'fan.setEnabled' || command === 'fan.setPreset' || command === 'fan.setChoice')
      && snapshot.actions?.fan === true && snapshot.fan.canToggle === true;
    if ((!snapshot.ready && !independentFan) || !Number.isSafeInteger(request.generation) || request.generation !== snapshot.generation ||
        !Number.isSafeInteger(request.revision) || (request.revision !== snapshot.revision && !this.sameSliderSource(this.offeredSnapshots.get(request.revision as number)?.snapshot,snapshot,command))) {
      await failure('状态已变化，请重新读取后操作'); return;
    }
    if (command === 'audioOptimization.setEnabled' ? !snapshot.actions?.audioOptimization : command === 'performance.setControls' ? !snapshot.actions?.performance : command === 'game.setField' ? !snapshot.actions?.game : command === 'frame.setField' ? !snapshot.actions?.frames : command === 'touchpad.setField' ? !snapshot.actions?.touchpads : command === 'global.setField' ? !snapshot.actions?.global : command === 'monitor.setEnabled' ? !snapshot.actions?.monitor : !snapshot.actions?.fan) {
      await failure('该镜像功能尚未接线，未修改配置'); return;
    }
    if (command === 'game.setField' && (!snapshot.game || request.identity !== snapshot.game.identity)) {
      await failure('当前游戏身份已变化，未执行操作'); return;
    }
    if(command==='global.setField'&&(snapshot.game!==null||request.identity!=='')){await failure('全局目标已变化，未执行操作');return;}
    if((command==='performance.setControls'||command==='frame.setField') && request.identity!==(snapshot.game?.identity??'')){await failure('帧率目标已变化，未执行操作');return;}
    if (command === 'fan.setPreset' && !snapshot.fan.supported && !snapshot.fan.pending || (command === 'fan.setEnabled' || command === 'fan.setChoice') && !snapshot.fan.canToggle) {
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
    const nativePeer=peerRevision>=0?{connection:event.connection,runId:event.runId,connectionRevision:peerRevision}:undefined;
    const checkpoint = () => {
      if (this.disposed || epoch !== this.epoch || peerRevision!==this.transportRevision || !this.matches(event)) throw new Error('MIRROR_CONTEXT_EXPIRED');
    };
    this.mutationBusy = true; client.highest = sequence;
    const receipt = Promise.resolve().then(async () => {
      try {
        checkpoint();
        const result = await this.deps.execute!(command,structuredClone(args) as Record<string,unknown>,
          {generation:snapshot.generation,revision:snapshot.revision,gameIdentity:command === 'performance.setControls'||command === 'game.setField'||command === 'frame.setField'||command === 'global.setField' ? snapshot.game?.identity??'' : undefined,gameAdmission:command === 'game.setField' ? this.gameAdmissions.get(snapshot) : undefined,controlAdmission:this.controlAdmissions.get(snapshot),nativePeer,checkpoint});
        return {type:'reply',runId:event.runId,id:request.id,ok:true,result,notice:result.notice};
      } catch(error) {
        const sourceChanged=['GAME_MIRROR_SOURCE_CHANGED','PERFORMANCE_MIRROR_SOURCE_CHANGED'].includes(String((error as {message?:unknown})?.message));
        return {type:'reply',runId:event.runId,id:request.id,ok:false,error:sourceChanged
          ? 'YMCC 原方案已变化；未保存，请查看更新后的侧栏再选择，不自动重试'
          : '镜像操作未确认；请查看 YMCC 当前状态，不自动重放'};
      } finally { this.mutationBusy = false; if (this.matches(event)) this.invalidateFresh(); }
    });
    client.receipts.set(sequence,{signature,response:receipt});
    // Keep a high-water mark after evicting old completed outcomes, so they cannot replay.
    if (client.receipts.size > 32) client.receipts.delete(client.receipts.keys().next().value!);
    const response = await receipt;
    if (this.matches(event)) await this.deps.reply(event.connection,response).catch(() => false);
  }
  dispose(): void { this.disposed = true; this.detach(); }
}
