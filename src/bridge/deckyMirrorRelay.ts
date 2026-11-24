// Authenticated mirror relay. Original business owners implement all actions;
// private per-snapshot admission never enters the public/client wire model.
import { validSteamObservation, type SteamRunningObservation } from '../../decky-plugin/src/steamRunningContext';
import type { MirrorSnapshot } from '../../decky-plugin/src/mirrorClient';
import type { GameMirrorAdmission } from './deckyGameActions';
import type { ContinuousPerformanceReadStage, GameCustomDisplayReadStage } from './performanceSchedule';
export interface MirrorControlAdmission {audioOptimization?:string;performance?:string;frames?:string;touchpads?:string;global?:string;monitor?:string;trainer?:string;}
export type RelayReadSnapshot = Omit<MirrorSnapshot,'runId'|'revision'> & {gameAdmission?:GameMirrorAdmission;controlAdmission?:MirrorControlAdmission};
export interface MirrorPeer { connection: string; runId: string; connected?: boolean; revision?: number; connectionRevision?:number; }
export interface MirrorRequest extends MirrorPeer { request: { command: string; runId: string; id?: string; clientId?: string; [key: string]: unknown }; }
export interface RelayMutationContext { nativePeer?:{connection:string;runId:string;connectionRevision:number}; generation:number; revision:number; gameIdentity?:string; gameAdmission?:GameMirrorAdmission; controlAdmission?:MirrorControlAdmission; checkpoint:() => void; }
export type MirrorReadStage = 'audio'|'profile'|'power'|'powerSource'|'schedule'|'performance'|'topology'|'gyro'|'frames'|'touchpads'|'global'|'monitor'|'input';
const invalidationSources=["audio","monitor","input","touchpads","frames","schedule","profile","game","power","fan","controller","steam","trainer","speed","lossless","action","unspecified","steam-observation","snapshot-request","peer-revision"] as const;
export type MirrorInvalidationSource=typeof invalidationSources[number];
export interface MirrorReadTrace {
  measure<T>(stage:MirrorReadStage, read:()=>Promise<T>):Promise<T>;
  isCurrent:()=>boolean;
  performanceStage?:(stage:ContinuousPerformanceReadStage,elapsedMs:number)=>void;
  profileStage?:(stage:GameCustomDisplayReadStage,elapsedMs:number)=>void;
}
export interface RelayDeps {
  read: (trace?: MirrorReadTrace) => Promise<RelayReadSnapshot>;
  diagnostic?: (event: string, detail: Record<string, unknown>) => void;
  now?: () => number;
  snapshotDemand?:()=>void;
  schedule?:(action:()=>void,delay:number)=>ReturnType<typeof setTimeout>;
  cancel?:(timer:ReturnType<typeof setTimeout>)=>void;
  observe: (invalidate: (source?:MirrorInvalidationSource) => void) => (() => void);
  reply: (connection: string, message: unknown) => Promise<boolean>;
  // Borrowed for this synchronous call; the observer owns any retained values.
  observeSteam?: (observation:Readonly<SteamRunningObservation>) => boolean;
  execute?: (command:string,args:Record<string,unknown>,context:RelayMutationContext) => Promise<{saved:boolean;applied:boolean;pending?:boolean;notice:string}>;
}
export class ReadOnlyMirrorRelay {
  private peer: MirrorPeer | null = null;
  private epoch = 0;
  private sourceEpoch = 0;
  private powerEpoch = 0;
  // Per-read diagnostic scope only; it never grants freshness or recovery.
  private activeReadChange:{sources:number}|null=null;
  private revision = 0;
  private dirty = true;
  private requested = false;
  private snapshotDemandPaused = false;
  private demandEpoch = 0;
  private queuedDemandEpoch: number | null = null;
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
  private retireRead: (() => void) | null = null;
  private probeRead: {id:string;sourceEpoch:number;demandEpoch:number} | null = null;
  private cacheSourceEpoch = -1;
  private deliveries = new Map<string,Promise<void>>();
  private pendingDisplay: {snapshot:MirrorSnapshot;sourceEpoch:number} | null = null;
  private snapshotRetry: {snapshot:MirrorSnapshot;probeId?:string;timer:ReturnType<typeof setTimeout>} | null = null;
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
  connected(peer: MirrorPeer, refreshRevision = false): boolean {
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
        this.revision = peer.revision!; this.cache = null;
        if(refreshRevision)this.invalidate('peer-revision');
        else this.markSnapshotDirty('peer-revision');
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
  // Attach restores unknown demand after a renderer replacement. An existing
  // subscription already owns its Ready/source refresh; an explicit pause wins.
  refreshSnapshotAfterAttach(peer:MirrorPeer):Promise<void> {
    if(this.disposed || !this.matches(peer) || this.requested || this.snapshotDemandPaused)return Promise.resolve();
    return this.request({...peer,request:{command:'snapshot',runId:peer.runId}});
  }
  private startObserving(): boolean {
    if (this.stopObserve) return true;
    const demandEpoch = this.demandEpoch;
    try { this.stopObserve = this.deps.observe(source => { if (demandEpoch === this.demandEpoch) this.invalidate(source); }); return true; }
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
      this.pendingDisplay=null; this.deliveries.clear();
      this.stopObserving();
      this.diagnostic('snapshot-demand',{subscribe:false,demandEpoch:this.demandEpoch});
    }
    this.stopRecovery();
    this.stopSnapshotRetry();
  }
  // Only a verified context edge or completed explicit owner action may bypass
  // the fault read's wait. Ordinary demand/events cannot renew that budget.
  invalidateFresh(contextChanged=false,source:MirrorInvalidationSource='unspecified'):void {
    if(contextChanged){this.stopRecovery();this.recoveryAttempt=0;this.deferredSnapshot=null;}
    this.freshReadRequested=true;this.invalidate(source);
  }
  advancePowerScope():void {
    this.powerEpoch++;
    this.offeredSnapshots.clear();
    this.invalidateFresh(true,'power');
    // Old owner IO can settle independently of the next snapshot.
    this.retireRead?.();
    this.retireRead=null;
    this.readTask=null;
    this.probeRead=null;
  }
  private markSnapshotDirty(source:MirrorInvalidationSource='unspecified'):void {
    this.stopSnapshotRetry();
    const changed=this.activeReadChange;
    if(changed){
      const index=invalidationSources.indexOf(source);
      changed.sources|=1<<(index>=0?index:invalidationSources.indexOf('unspecified'));
    }
    this.dirty = true; this.sourceEpoch++; this.pendingDisplay=null;
  }
  invalidate(source:MirrorInvalidationSource='unspecified'): void {
    if (this.disposed || !this.peer) return;
    this.markSnapshotDirty(source);
    // The original save notifies before its action promise settles. Do not
    // publish an intermediate revision and then a second post-action revision.
    // Keep it dirty; the existing mutation finally invalidates once after commit.
    const demandEpoch=this.demandEpoch;
    if (!this.requested || this.queuedDemandEpoch===demandEpoch || this.mutationBusy) return;
    this.queuedDemandEpoch=demandEpoch;
    queueMicrotask(() => {
      // A retired callback cannot push to a reopened sidebar or clear its queue.
      if(this.queuedDemandEpoch!==demandEpoch)return;
      this.queuedDemandEpoch=null;
      if(demandEpoch===this.demandEpoch && this.peer && this.requested && !this.disposed && !this.mutationBusy)void this.push();
    });
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
    this.stopSnapshotRetry();
    this.epoch++; this.sourceEpoch++; this.demandEpoch++; this.stopObserving(); this.deliveries.clear(); this.offeredSnapshots.clear();
    this.pendingDisplay=null;
    this.activeReadChange=null;
    this.retireRead=null;
    this.probeRead=null;
    this.firstDemandRead=true;this.freshReadRequested=false;this.deferredSnapshot=null;this.peer = null; this.cache = null; this.readTask = null; this.dirty = true; this.requested = false; this.snapshotDemandPaused = false; this.revision = 0;
  }
  private snapshot(): Promise<MirrorSnapshot | null> {
    if (!this.peer || this.disposed) return Promise.resolve(null);
    if (!this.dirty && this.cache) return Promise.resolve(this.cache);
    if (this.readTask) return this.readTask;
    const epoch = this.epoch; const peer = this.peer;
    let unstable=false,reportRetriedRead=false,retired=false;
    const retirement=new Promise<null>(resolve=>{this.retireRead=()=>{retired=true;resolve(null);};});
    const work = (async () => {
      const inlineLimit=3;
      let lastReadDemandEpoch=this.demandEpoch;
      // Reopen shares the retiring read, but its first read must not wait on
      // backoff solely because the old demand used the last inline attempt.
      // Allow one handoff per task; continued churn still uses the retry lease.
      for (let attempt = 0; attempt < inlineLimit || (attempt === inlineLimit && lastReadDemandEpoch !== this.demandEpoch); attempt++) {
        const sourceEpoch = this.sourceEpoch;
        let data: RelayReadSnapshot;
        const readDemandEpoch=this.demandEpoch,readId=++this.readSequence,started=(this.deps.now??Date.now)(),first=this.firstDemandRead;
        lastReadDemandEpoch=readDemandEpoch;
        // This retry now reads the new source for the same pending probe.
        // Never carry that proof across a side-panel close and reopen.
        if(attempt && this.probeRead?.demandEpoch===readDemandEpoch)this.probeRead.sourceEpoch=sourceEpoch;
        this.firstDemandRead=false;
        const timings:Record<string,number>={};let failed=false,partial=false;
        let profileReadMs:Partial<Record<GameCustomDisplayReadStage,number>>|undefined;
        let failureStage:MirrorReadStage|undefined,failureKind:'source-changed'|'read-error'|undefined;
        const trace:MirrorReadTrace & {sources:number}={
          sources:0,
          measure:async(stage,read)=>{const begin=(this.deps.now??Date.now)();try{return await read();}catch(error){partial=true;failureStage??=stage;throw error;}finally{timings[stage+'Ms']=Math.max(0,(this.deps.now??Date.now)()-begin);}},
           isCurrent:()=>!retired&&!this.disposed&&epoch===this.epoch&&readDemandEpoch===this.demandEpoch&&sourceEpoch===this.sourceEpoch,
          performanceStage:(stage,elapsedMs)=>{if(Number.isFinite(elapsedMs)&&elapsedMs>=0)timings[stage+'Ms']=Math.floor(elapsedMs);},
          profileStage:(stage,elapsedMs)=>{if(Number.isFinite(elapsedMs)&&elapsedMs>=0)(profileReadMs??={})[stage]=Math.min(3600000,Math.floor(elapsedMs));},
        };
        // Reuse the existing trace object; no extra per-read scope allocation.
        const changed=trace;this.activeReadChange=changed;
        if(first)this.diagnostic('snapshot-read-start',{readId,demandEpoch:readDemandEpoch});
        try { data = await this.deps.read(trace); }
        catch(error) { failed=true;failureKind='read-error';
          // Only this exact local marker is diagnostic. Never log exception text
          // or use it to bypass the original fallback, source fence or recovery.
          try{if(error&&typeof error==='object'&&(error as {message?:unknown}).message==='MIRROR_SNAPSHOT_SOURCE_CHANGED')failureKind='source-changed';}catch{}
          data = { generation: 0, ready: false, game: null, fan: { supported: false, enabled: false, preset: '', choices: [] }, notice: 'YMCC 状态读取失败；未修改任何配置' }; }
        const finished=(this.deps.now??Date.now)(),elapsedMs=Math.max(0,finished-started);
        const fault=failed||partial||!data.ready;
        const current=trace.isCurrent();
        if(first || current&&(reportRetriedRead||fault!==this.readFault) || (failed||partial||elapsedMs>=250) && finished-this.lastReadDiagnostic>=10000){
          // Allocate the list only for a logged same-demand discard with multiple causes.
          const mask=!this.disposed&&epoch===this.epoch&&readDemandEpoch===this.demandEpoch&&sourceEpoch!==this.sourceEpoch?changed.sources:0;
          const multiple=(mask&(mask-1))!==0;
          this.lastReadDiagnostic=finished;
          this.diagnostic('snapshot-read-finished',{readId,demandEpoch:readDemandEpoch,elapsedMs,ok:!failed&&!partial,
            stage:failed?'failed':partial?'partial':'settled',ready:data.ready,discarded:this.disposed||epoch!==this.epoch||readDemandEpoch!==this.demandEpoch||sourceEpoch!==this.sourceEpoch,...timings,
            ...(profileReadMs?{profileReadMs:{...profileReadMs}}:{}),
            ...(failed||partial?{failureKind:failureKind??'owner-read',...(failureStage?{failureStage}:{})}:{}),
            ...(multiple?{invalidatedBy:'multiple',invalidatedBySources:invalidationSources.filter((_,index)=>!!(mask&(1<<index)))}:
               mask?{invalidatedBy:invalidationSources[31-Math.clz32(mask)]}:{}),
            ...(this.disposed?{discardReason:'disposed'}:epoch!==this.epoch?{discardReason:'peer-changed'}:readDemandEpoch!==this.demandEpoch?{discardReason:'demand-changed'}:sourceEpoch!==this.sourceEpoch?{discardReason:'source-changed'}:{})});
        }
        if(this.activeReadChange===changed)this.activeReadChange=null;
        // The first result may be retired by a source change while the same
        // transaction immediately rereads. Record its current follow-up once;
        // otherwise a successful delivery leaves only a discarded log record.
        // No additional reads, timers or healthy steady-state records.
        if(first&&!current)reportRetriedRead=true;
        else if(current)reportRetriedRead=false;
        if (retired || this.disposed || epoch !== this.epoch || !this.matches(peer)) return null;
        if (sourceEpoch !== this.sourceEpoch) { if (!this.requested) return null; continue; }
        this.readFault=fault;
        const {gameAdmission,controlAdmission,...publicData}=data;
        this.deferredSnapshot=null;
        this.cache = { ...structuredClone(publicData), runId: peer.runId, revision: ++this.revision };
        this.cacheSourceEpoch=sourceEpoch;
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
    })();
    const task=Promise.race([work,retirement]).finally(() => {
      if (this.readTask === task) {this.readTask=null;this.retireRead=null;this.probeRead=null;if(unstable)this.recoverRead();}
    });
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
  // This is an explicit unavailable view, never fresh native proof or an admitted cache.
  // Churning reads may not have produced this.cache at all; repeated demand
  // must still wait on the same recovery lease instead of starting 3 more reads.
  private deferredView():MirrorSnapshot|null {
    if(!this.peer)return null;
    if(!this.deferredSnapshot){
      const previous=this.cache??{generation:0,game:null,fan:{supported:false,enabled:false,preset:'',choices:[]}};
      this.deferredSnapshot={...previous,runId:this.peer.runId,revision:++this.revision,ready:false,provenance:{kind:'unconfirmed'},
        actions:{fan:false,game:false,performance:false,frames:false,touchpads:false,global:false,audioOptimization:false,monitor:false},
        fan:{...previous.fan,canToggle:false},notice:'YMCC 状态正在更新；侧栏会自动恢复'};
    }
    return this.deferredSnapshot;
  }
  private stableSnapshotCurrent(snapshot:MirrorSnapshot,sourceEpoch:number):boolean {
    return !this.disposed && !!this.peer && this.requested && this.cache===snapshot && snapshot.ready &&
      !this.dirty && !this.readFault && !this.readTask && !this.mutationBusy && this.recoveryTimer===null && sourceEpoch===this.sourceEpoch;
  }
  private stopSnapshotRetry():void {
    const retry=this.snapshotRetry;this.snapshotRetry=null;
    if(retry)(this.deps.cancel??clearTimeout)(retry.timer);
  }
  private retrySnapshotDelivery(snapshot:MirrorSnapshot,sourceEpoch:number,probeId?:string):void {
    if(!this.stableSnapshotCurrent(snapshot,sourceEpoch))return;
    const prior=this.snapshotRetry;
    if(prior?.snapshot===snapshot){if(probeId)prior.probeId=probeId;return;}
    this.stopSnapshotRetry();
    // A rejected read-only payload gets one send on the current peer. Do not
    // rerun owners, reset the client deadline or retry an uncertain receipt.
    const retry={snapshot,probeId,timer:(this.deps.schedule??setTimeout)(()=>{
      if(this.snapshotRetry!==retry)return;
      this.snapshotRetry=null;
      void this.push(retry.probeId,undefined,{snapshot,sourceEpoch});
    },250)};
    this.snapshotRetry=retry;
  }
  // The host may have assembled its view before the relay has accepted it.
  // Let display-only work cross that handoff without forcing a new read.
  afterSnapshotRead(repaint:()=>void):void {
    const task=this.readTask;
    const epoch=this.epoch,demandEpoch=this.demandEpoch,powerEpoch=this.powerEpoch;
    if(task)void task.then(()=>{if(epoch===this.epoch&&demandEpoch===this.demandEpoch&&powerEpoch===this.powerEpoch)repaint();}).catch(()=>{});
    else repaint();
  }
  // Internal display-only completion. gameSteam never enters action admission,
  // so keep the already-offered control revision and all original receipts.
  // Real context/settings invalidations still require their original fresh read.
  repaintGameSteam(identity:string,generation:number,value:MirrorSnapshot['gameSteam']):boolean {
    const snapshot=this.cache,sourceEpoch=this.sourceEpoch;
    if(!snapshot || snapshot.game?.identity!==identity || snapshot.generation!==generation || !this.stableSnapshotCurrent(snapshot,sourceEpoch))return false;
    const next=value===undefined?undefined:structuredClone(value);
    if(JSON.stringify(snapshot.gameSteam)===JSON.stringify(next))return true;
    if(next===undefined)delete snapshot.gameSteam;else snapshot.gameSteam=next;
    this.deliverDisplay(snapshot,sourceEpoch);
    return true;
  }
  repaintLosslessProgram(generation:number,value:NonNullable<MirrorSnapshot['losslessProgram']>):boolean {
    const snapshot=this.cache,sourceEpoch=this.sourceEpoch;
    if(!snapshot?.losslessProgram||snapshot.generation!==generation||!this.stableSnapshotCurrent(snapshot,sourceEpoch))return false;
    if(snapshot.losslessProgram.known===value.known&&snapshot.losslessProgram.running===value.running)return true;
    snapshot.losslessProgram={...value};
    this.deliverDisplay(snapshot,sourceEpoch);
    return true;
  }
  private deliverDisplay(snapshot:MirrorSnapshot,sourceEpoch:number):void {
    // The cache already holds the latest display. Wait for in-flight sends
    // once, using their existing completion path instead of per-change waiters.
    if(this.deliveries.size){this.pendingDisplay??={snapshot,sourceEpoch};return;}
    void this.push(undefined,{snapshot,sourceEpoch});
  }
  private async push(probeId?:string,display?:{snapshot:MirrorSnapshot;sourceEpoch:number},retry?:{snapshot:MirrorSnapshot;sourceEpoch:number}): Promise<void> {
    const peer = this.peer; const epoch = this.epoch, demandEpoch = this.demandEpoch;
    if (!peer || !this.requested) return;
    const retained=display??retry;
    const fresh=retained?false:this.freshReadRequested;if(!retained)this.freshReadRequested=false;
    let snapshot:MirrorSnapshot|null;
    if(retained){if(!this.stableSnapshotCurrent(retained.snapshot,retained.sourceEpoch))return;snapshot=retained.snapshot;}
    else if(this.recoveryTimer!==null&&!fresh){
      // Both cached and never-stable sources share the one fault observation.
      snapshot=this.cache&&!this.dirty?this.cache:this.deferredView();
    }else {
      const read=this.snapshot();
      if(probeId&&read===this.readTask&&(this.probeRead?.id!==probeId||this.probeRead.sourceEpoch!==this.sourceEpoch||this.probeRead.demandEpoch!==demandEpoch))
        this.probeRead={id:probeId,sourceEpoch:this.sourceEpoch,demandEpoch};
      snapshot=await read;
    }
    if (!snapshot && probeId && this.requested && demandEpoch===this.demandEpoch && epoch===this.epoch)
      snapshot=!this.dirty&&this.cacheSourceEpoch===this.sourceEpoch?this.cache:this.deferredView();
    if (!snapshot || !this.requested || demandEpoch !== this.demandEpoch || epoch !== this.epoch || !this.matches(peer)) return;
    const key = epoch + ':' + snapshot.revision + ':' + (probeId??'') + (display?':display':'');
    const prior = this.deliveries.get(key);
    if (prior) { await prior; return; }
    // Coalesce only concurrent deliveries; a later explicit request still gets a reply.
    let delivery:Promise<void>;
    delivery = Promise.resolve().then(async () => {
      if (this.requested && demandEpoch === this.demandEpoch && epoch === this.epoch && this.matches(peer) && (!retained||this.stableSnapshotCurrent(snapshot,retained.sourceEpoch))) {
        // A source event can retire a settled read before this delivery runs.
        // Preserve the nonce, but only a current read can prove readiness.
        let outgoing=snapshot;
        if(!display&&(snapshot!==this.cache||this.cacheSourceEpoch!==this.sourceEpoch)){
          const current=!this.dirty&&this.cacheSourceEpoch===this.sourceEpoch?this.cache:null;
          const deferred=current??this.deferredView();if(!deferred)return;
          outgoing=deferred;
          if(!current&&outgoing!==snapshot&&!this.readTask&&this.queuedDemandEpoch!==demandEpoch)this.recoverRead();
        }
        let offer=this.offeredSnapshots.get(outgoing.revision);
        // A repaint can follow only a full delivery. It never creates a
        // receipt, including when the first broker send was rejected.
        if(display){
          if(offer?.delivered&&offer.snapshot===outgoing){
            // The client merges only the display fields into this revision.
            const {runId,revision,generation,gameSteam,losslessProgram}=outgoing;
            await this.deps.reply(peer.connection,{type:'snapshot',runId,displayOnly:true,
              snapshot:{runId,revision,generation,gameSteam,losslessProgram}}).catch(()=>false);
          }
          return;
        }
        if(!offer){offer={snapshot:outgoing,pending:0,delivered:false};this.offeredSnapshots.set(outgoing.revision,offer);}
        offer.pending++;
        while(this.offeredSnapshots.size>8)this.offeredSnapshots.delete(this.offeredSnapshots.keys().next().value!);
        const sourceEpoch=this.sourceEpoch;
        const sent=await this.deps.reply(peer.connection, { type: 'snapshot', runId: peer.runId, snapshot:outgoing, ...(probeId?{probeId}:{}) }).catch(() => undefined);
        offer.pending--;offer.delivered=offer.delivered||sent===true;
        if(sent===true){
          const queued=this.snapshotRetry;
          if(queued?.snapshot===outgoing&&(!queued.probeId||queued.probeId===probeId))this.stopSnapshotRetry();
        }else if(sent===false&&!retry&&(probeId||!offer.delivered))this.retrySnapshotDelivery(outgoing,sourceEpoch,probeId);
        // Separate ordinary/probe deliveries may share this same snapshot. A
        // failed sibling cannot revoke a pending or successful delivery.
        if(!offer.delivered&&offer.pending===0&&this.offeredSnapshots.get(outgoing.revision)===offer)this.offeredSnapshots.delete(outgoing.revision);
      }
    }).finally(() => {
      if(this.deliveries.get(key)!==delivery)return;
      this.deliveries.delete(key);
      if(this.deliveries.size||!this.pendingDisplay)return;
      const display=this.pendingDisplay;this.pendingDisplay=null;
      void this.push(undefined,display);
    });
    this.deliveries.set(key,delivery);
    await delivery;
  }
  async request(event: MirrorRequest): Promise<void> {
    if (this.disposed || !this.validPeer(event) || !event.request || event.request.runId !== event.runId) return;
    const snapshotRequest=event.request.command==='snapshot';
    // Mutations retain their own request across awaits. Snapshot demand uses
    // only scalar flags and a separately owned optional Steam observation.
    if(!snapshotRequest)event = { ...event, request: structuredClone(event.request) };
    // Native generated this event only after authenticated handshake. It also rescues renderer remount.
    if (!this.peer) this.connected(event);
    if (!this.matches(event)) return;
    if (snapshotRequest) {
      const {subscribe,steam,probeId}=event.request;
      if(subscribe===false){this.pauseDemand();return;}
      if(steam!==undefined){
        if(!validSteamObservation(steam))return;
        try { if(this.deps.observeSteam?.(steam)) this.markSnapshotDirty('steam-observation'); }
        catch { return; }
      }
      if(probeId!==undefined&&(typeof probeId!=='string'||!/^probe:[A-Za-z0-9_-]{1,80}:\d{1,16}$/.test(probeId)))return;
      // HUD ready can resend the same nonce while its owner read is in flight.
      // A duplicate in the same source epoch must not retire that read.
      const sameProbe=probeId&&this.probeRead?.id===probeId&&this.probeRead.sourceEpoch===this.sourceEpoch&&this.probeRead.demandEpoch===this.demandEpoch;
      if(probeId&&!sameProbe)this.markSnapshotDirty('snapshot-request');
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
      if(this.deps.snapshotDemand && (resumed || probeId&&!sameProbe || !this.readTask)&&this.recoveryTimer===null){
        this.deps.snapshotDemand();
        // The request below already owns delivery, including its probe nonce.
        if(!this.disposed && this.peer)this.markSnapshotDirty('snapshot-request');
      }
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
    const peerRevision=this.transportRevision; const epoch=this.epoch,powerEpoch=this.powerEpoch; const snapshot = await this.snapshot();
    if (!this.matches(event) || epoch !== this.epoch) return;
    if (powerEpoch!==this.powerEpoch) { await failure('状态已变化，请重新读取后操作'); return; }
    if (!snapshot) return;
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
      } finally {
        this.mutationBusy = false;
        // The accepted owner may settle after a new peer read the old facts.
        // Refresh only the current observation; never route the retired reply
        // or resubmit its action. Hidden/disposed demand still performs no IO.
        if (!this.disposed && this.peer) this.invalidateFresh();
      }
    });
    client.receipts.set(sequence,{signature,response:receipt});
    // Keep a high-water mark after evicting old completed outcomes, so they cannot replay.
    if (client.receipts.size > 32) client.receipts.delete(client.receipts.keys().next().value!);
    const response = await receipt;
    if (this.matches(event)) await this.deps.reply(event.connection,response).catch(() => false);
  }
  dispose(): void { this.disposed = true; this.detach(); }
}
