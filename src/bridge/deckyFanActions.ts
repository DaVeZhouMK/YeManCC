// YMCC mirror intent -> existing FanHostLifecycle APIs. No raw device/DLL/EC calls.
import type { FanNode, FanState } from './fanApi';
import type { PowerLifecycleState } from './api';
import type { FanPreset } from './fanFeature';
import type { FanMirrorDisplay } from './deckyFanDisplay';
export interface FanMirrorOwner {
  readonly hasControlIntent: boolean;
  readonly controlReady: boolean;
  readonly state: string;
  readonly recoveryActive: boolean;
  readonly recoveryNeedsAttention?: boolean;
  readonly controlCancellationRevision?: number;
  readonly controlIntentRevision?: number;
  start(options: { manualRecovery: boolean; reason: string }): Promise<{ allowed: boolean; writeReady: boolean; reason?: string }>;
  apply(nodes: readonly FanNode[]): Promise<FanState>;
  applyPreset(preset: FanPreset, nodes: readonly FanNode[]): Promise<FanState>;
  /** Retire an in-flight curve before the mirror submits the latest saved one. */
  stagePendingControlCurve?: (nodes: readonly FanNode[]) => void;
  disable(): Promise<FanState>;
}
export interface FanMirrorDeps {
  owner: FanMirrorOwner;
  enabled: () => boolean;
  currentPreset: () => FanPreset;
  curve: (preset: FanPreset) => FanNode[];
  save: (nodes: readonly FanNode[], preset: FanPreset) => Promise<void>;
  updateActive: (active: boolean) => void;
  updateDuty: (duty: number) => void;
  power: () => Promise<PowerLifecycleState>;
  featureAllowed: () => boolean;
  uiBusy: () => boolean;
  display: (state: FanMirrorDisplay) => void;
  observeCapability?: (allowed:boolean) => Promise<void>;
}
export interface MirrorMutationContext { generation: number; checkpoint: () => void; }
export interface MirrorActionResult { saved: boolean; applied: boolean; pending?: boolean; notice: string; }
type StagedFanPreset = { preset: FanPreset; nodes: FanNode[]; ready: Promise<void>; ownerRevision?: number };
const presetNames = new Set<FanPreset>(['soft','balanced','aggressive']);
export function createFanMirrorActions(deps: FanMirrorDeps) {
  let executing = false;
  let requested = false, revision = 0, waiting = false;
  const acceptedContexts=new WeakSet<MirrorMutationContext>();
  let pendingPreset: StagedFanPreset|null = null;
  let ownerCancellationRevision = deps.owner.controlCancellationRevision;
  // The original owner can be closed outside this mirror. Retire only the
  // mirror's stale waiting intent; a new explicit request still uses that owner.
  const synchronizeOwnerCancellation = () => {
    const current = deps.owner.controlCancellationRevision;
    if (current === ownerCancellationRevision) return;
    ownerCancellationRevision = current;
    ++revision; requested = false; waiting = false; pendingPreset = null;
  };
  const guardControlContext = (context: MirrorMutationContext): MirrorMutationContext => {
    const cancellation = deps.owner.controlCancellationRevision;
    return { generation: context.generation, checkpoint: () => {
      synchronizeOwnerCancellation();
      context.checkpoint();
      if (cancellation !== deps.owner.controlCancellationRevision) throw Error('FAN_MIRROR_INTENT_CANCELED');
    } };
  };
  const state = (notice: string) => deps.display({ preset:deps.currentPreset(),active:deps.enabled(),pending:deps.owner.recoveryActive,notice });
  // A retained curve can outlive a failed replacement. Allow a fresh manual ON.
  const canReuseControl = () => deps.enabled() && deps.owner.hasControlIntent && deps.owner.controlReady
    && deps.owner.state === 'ready' && !deps.owner.recoveryActive && !deps.owner.recoveryNeedsAttention;
  const requirePower = async (context: MirrorMutationContext) => {
    context.checkpoint();
    const power = await deps.power();
    context.checkpoint();
    if (deps.uiBusy()) throw new Error('FAN_MIRROR_BUSY');
    // Shared generation fences the mirror packet. The original Fan owner checks
    // power.fanLifecycle for real sleep/write admission before every device write.
    if (power.generation !== context.generation)
      throw new Error('FAN_MIRROR_POWER_NOT_READY');
  };
  const applyStagedPreset = async (initial: StagedFanPreset, context: MirrorMutationContext): Promise<void> => {
    let latest = initial;
    for (;;) {
      await latest.ready;
      context.checkpoint();
      const newer = pendingPreset;
      if (newer) { latest = newer; pendingPreset = null; continue; }
      // Saving can outlive OFF, a new power boundary or another curve owner.
      await requirePower(context);
      const newerAfterPower = pendingPreset;
      if (newerAfterPower) { latest = newerAfterPower; pendingPreset = null; continue; }
      if (latest.ownerRevision !== undefined && latest.ownerRevision !== deps.owner.controlIntentRevision) {
        ++revision; requested = false; waiting = false; pendingPreset = null;
        throw Error('FAN_MIRROR_INTENT_CANCELED');
      }
      await deps.owner.applyPreset(latest.preset, latest.nodes);
      context.checkpoint();
      return;
    }
  };
  const actions = {
    async execute(command: 'fan.setPreset' | 'fan.setEnabled', args: Record<string, unknown>, context: MirrorMutationContext): Promise<MirrorActionResult> {
      synchronizeOwnerCancellation();
      if (!deps.featureAllowed()) throw new Error('FAN_MIRROR_NOT_ENABLED');
      const acceptedRequest = acceptedContexts.has(context);
      if (executing && !acceptedRequest || deps.uiBusy()) throw new Error('FAN_MIRROR_BUSY');
      if (command === 'fan.setPreset') {
        if (Object.keys(args).length !== 1 || !presetNames.has(args.preset as FanPreset)) throw new Error('FAN_MIRROR_INVALID_PRESET');
      } else if (command === 'fan.setEnabled') {
        if (Object.keys(args).length !== 1 || typeof args.enabled !== 'boolean') throw new Error('FAN_MIRROR_INVALID_SWITCH');
      } else throw new Error('FAN_MIRROR_COMMAND_DENIED');
      if (command !== 'fan.setEnabled' || args.enabled !== false) context = guardControlContext(context);
      const presetOwnerRevision = command === 'fan.setPreset' ? deps.owner.controlIntentRevision : undefined;
      executing = true;
      try {
        await requirePower(context);
        const desired = command === 'fan.setEnabled' ? args.enabled as boolean : null;
        if (desired === false) {
          // The original owner cancels recovery, restores OEM, and releases the existing lease.
          try {
            const disabled = deps.owner.disable();
            // This synchronous increment belongs to this OFF, so its receipt
            // must not invalidate its own mirror context. Later OFFs still do.
            ownerCancellationRevision = deps.owner.controlCancellationRevision;
            await disabled;
            context.checkpoint();
            deps.updateActive(false); deps.updateDuty(0); state('风扇控制已关闭');
            return { saved:false,applied:true,notice:'风扇控制已关闭（原生命周期已确认）' };
          } catch (error) {
            context.checkpoint(); // A late OFF receipt cannot overwrite a newer ON.
            // Same intent/presentation semantics as the existing page: failure is NOT OEM release proof.
            deps.updateActive(false); deps.updateDuty(0); state('已取消控制意图，风扇交还未确认');
            throw error;
          }
        }
        const preset = command === 'fan.setPreset' ? args.preset as FanPreset : deps.currentPreset();
        // Freeze the original saved curve. Never synthesize/reset curves or read mutable view nodes later.
        const nodes = deps.curve(preset).map(node => ({ tempC:node.tempC,dutyPercent:node.dutyPercent }));
        let saved = false;
        if (command === 'fan.setPreset') {
          await deps.save(nodes,preset); saved = true;
          context.checkpoint();
          // Settings completion cannot grant an old preset a new hardware intent.
          if (presetOwnerRevision !== undefined && presetOwnerRevision !== deps.owner.controlIntentRevision) {
            const notice = '风扇挡位已保存；较新的控制意图由 YMCC 处理';
            state(notice);
            return { saved:true,applied:false,notice };
          }
          state('风扇挡位已保存，正在确认控制状态');
          if (!deps.enabled() && !deps.owner.hasControlIntent) {
            state('风扇挡位已保存，控制仍关闭');
            return { saved:true,applied:false,notice:'风扇挡位已保存，未启用风扇控制' };
          }
        } else if (!acceptedRequest && canReuseControl()) {
          // A cached UI switch is intent, not proof that a cold/replaced original
          // owner is controlling. Only a live local original owner can dedupe.
          state('风扇控制已开启'); return { saved:false,applied:true,notice:'风扇控制已开启，无重复启用调用' };
        }
        await requirePower(context);
        try {
          const gate = await deps.owner.start({manualRecovery:true,reason:'user-action-before-control-write'});
          context.checkpoint();
          if (deps.observeCapability) await deps.observeCapability(gate.allowed);
          context.checkpoint();
          if (!gate.allowed) throw new Error('FAN_MIRROR_CAPABILITY_NOT_READY');
          // writeReady may be false while the original owner is admitting a slow Host.
          // Its apply() owns this wait and recovery; the mirror must not invent a deadline.
          await requirePower(context);
          const latest = pendingPreset;
          pendingPreset = null;
          if (latest) {
            await applyStagedPreset(latest, context);
          } else if (command === 'fan.setPreset') await deps.owner.applyPreset(preset,nodes);
          else await deps.owner.apply(nodes);
          context.checkpoint();
          // A selection made after a write has started is a new explicit
          // hardware operation and remains serialized by the original owner.
          while(pendingPreset){const newer=pendingPreset;pendingPreset=null;await applyStagedPreset(newer, context);}
          deps.updateActive(true); state('风扇设置已由 YMCC 原生命周期确认');
          return { saved,applied:true,notice:'风扇设置已由 YMCC 原生命周期确认' };
        } catch (error) {
          // A newer staged mirror curve intentionally retires the first apply
          // before its writer point. Re-enter once with the latest saved curve;
          // this is the same original owner, not a second recovery worker.
          if (command === 'fan.setEnabled' && pendingPreset && requested && waiting
              && (pendingPreset.ownerRevision === undefined || pendingPreset.ownerRevision === deps.owner.controlIntentRevision)) {
            const latest = pendingPreset; pendingPreset = null;
            await applyStagedPreset(latest, context);
            deps.updateActive(true); waiting = false;
            state('风扇设置已由 YMCC 原生命周期确认');
            return { saved:false,applied:true,notice:'风扇设置已由 YMCC 原生命周期确认' };
          }
          // Keep the same existing owner and recovery intent. No second recovery/retry loop.
          context.checkpoint(); // A late result must not undo an explicit OFF/new intent.
          // Intent retention alone does not mean an automatic task is running.
          const pending = deps.owner.recoveryActive;
          if (command === 'fan.setEnabled' && pending) {
            deps.updateActive(true); state('原风扇生命周期正在恢复');
            return { saved,applied:false,pending:true,notice:'控制意图已登记，原风扇生命周期正在恢复' };
          }
          if(!requested){deps.updateActive(false); deps.updateDuty(0);}
          state(requested?'开启意图保留，等待 YMCC 原生命周期确认':'风扇下发失败，原生命周期状态保留');
          throw error;
        }
      } finally { executing = false; }
    },
    // Acknowledge the user's intent, not hardware readiness. Work remains on the
    // ONE original owner; no mirror timer, retry loop, new Host or device writer.
    async request(command:'fan.setPreset'|'fan.setEnabled'|'fan.setChoice',args:Record<string,unknown>,context:MirrorMutationContext):Promise<MirrorActionResult> {
      synchronizeOwnerCancellation();
      if (command !== 'fan.setEnabled' || args.enabled !== false) {
        if (command !== 'fan.setChoice' || args.choice !== 'off') context = guardControlContext(context);
      }
      if(!deps.featureAllowed())throw Error('FAN_MIRROR_NOT_ENABLED');
      if(command==='fan.setChoice') {
        if(Object.keys(args).length!==1||typeof args.choice!=='string'||args.choice!=='off'&&!presetNames.has(args.choice as FanPreset))throw Error('FAN_MIRROR_INVALID_CHOICE');
        if(args.choice==='off')return actions.request('fan.setEnabled',{enabled:false},context);
        // The final slider position is one atomic mirror intent. Existing owner
        // handles slow admission; selecting a preset while waiting never restarts it.
        if(waiting || deps.enabled()&&deps.owner.hasControlIntent)return actions.request('fan.setPreset',{preset:args.choice},context);
        const choiceOwnerRevision = deps.owner.controlIntentRevision;
        await requirePower(context);
        const preset=args.choice as FanPreset,nodes=deps.curve(preset).map(n=>({...n}));
        await deps.save(nodes,preset);context.checkpoint();
        // A saved choice is not a new ON after another entry accepted control.
        if (choiceOwnerRevision !== undefined && choiceOwnerRevision !== deps.owner.controlIntentRevision)
          return { saved:true,applied:false,notice:'风扇挡位已保存；较新的控制意图由 YMCC 处理' };
        const result=await actions.request('fan.setEnabled',{enabled:true},context);
        return {...result,saved:true};
      }
      if(command==='fan.setEnabled' && Object.keys(args).length===1 && typeof args.enabled==='boolean') {
        const ownerIntentAtRequest = deps.owner.controlIntentRevision;
        await requirePower(context);
        const ownerIntentChanged = ownerIntentAtRequest !== undefined
          && ownerIntentAtRequest !== deps.owner.controlIntentRevision;
        if(!args.enabled){
          if (ownerIntentChanged) throw Error('FAN_MIRROR_INTENT_CANCELED');
          requested=false;waiting=false;pendingPreset=null;const own=++revision;
          deps.updateActive(false);deps.display({preset:deps.currentPreset(),active:false,pending:true,notice:'开启意图已取消，等待 YMCC 确认交还风扇'});
          const owned:MirrorMutationContext={generation:context.generation,checkpoint:()=>{synchronizeOwnerCancellation();if(own!==revision||!deps.featureAllowed())throw Error('FAN_MIRROR_INTENT_CANCELED');}};
          acceptedContexts.add(owned);
          void actions.execute(command,args,owned).catch(()=>{if(own===revision)deps.display({preset:deps.currentPreset(),active:false,pending:false,notice:'开启意图已取消；YMCC 风扇交还尚未确认'});});
          return {saved:false,applied:false,pending:true,notice:'开启意图已取消，等待 YMCC 确认交还风扇'};
        }
        if(waiting)return {saved:false,applied:false,pending:true,notice:'已开启，等待 YMCC 风扇真实信号；不重复启动'};
        if(canReuseControl())
          return {saved:false,applied:true,notice:'风扇控制已由 YMCC 确认开启'};
        // A newer page curve may have entered while the first power read
        // waited. Retire this older unaccepted ON, never supersede that owner.
        if (ownerIntentChanged)
          throw Error('FAN_MIRROR_INTENT_CANCELED');
        // Preserve the first accepted ON across sleep during startup, as the
        // original page does. Registration is intent, never write-success proof.
        deps.owner.stagePendingControlCurve?.(deps.curve(deps.currentPreset()));
        requested=true;waiting=true;const own=++revision;
        deps.updateActive(true);deps.display({preset:deps.currentPreset(),active:true,pending:true,notice:'已开启，等待 YMCC 风扇真实信号'});
        const owned:MirrorMutationContext={generation:context.generation,checkpoint:()=>{synchronizeOwnerCancellation();if(own!==revision||!deps.featureAllowed())throw Error('FAN_MIRROR_INTENT_CANCELED');}};
        acceptedContexts.add(owned);
        void actions.execute(command,args,owned).then(result=>{
          synchronizeOwnerCancellation();
          if(own!==revision)return;waiting=false;
          deps.display({preset:deps.currentPreset(),active:true,pending:result.pending===true,notice:result.notice});
        }).catch(()=>{
          synchronizeOwnerCancellation();
          if(own!==revision)return;waiting=false;
          // Keep ON as explicit user intent. Never label it hardware-ready or
          // silently turn it OFF when the original owner reports a real fault.
          deps.updateActive(true);deps.display({preset:deps.currentPreset(),active:true,pending:deps.owner.recoveryActive,
            notice:'开启意图保留；YMCC 尚未确认风扇控制，请查看主线状态'});
        });
        return {saved:false,applied:false,pending:true,notice:'已开启，等待 YMCC 风扇真实信号'};
      }
      if(command==='fan.setPreset' && waiting) {
        if(Object.keys(args).length!==1||!presetNames.has(args.preset as FanPreset))throw Error('FAN_MIRROR_INVALID_PRESET');
        const own=revision,preset=args.preset as FanPreset,nodes=deps.curve(preset).map(n=>({...n}));
        // Register before even the power read. The original owner may finish
        // Open on the next microtask; it must see this curve before first enable.
        const staged: StagedFanPreset = {
          preset, nodes, ready: Promise.resolve().then(() => deps.save(nodes, preset)),
        };
        // Observe a failed save while native power is pending. Keep awaiting
        // the original promise below so failure still rejects this request.
        void staged.ready.catch(() => {});
        pendingPreset=staged;
        deps.owner.stagePendingControlCurve?.(nodes);
        staged.ownerRevision = deps.owner.controlIntentRevision;
        try { await requirePower(context); await staged.ready; context.checkpoint(); }
        catch (error) { if (pendingPreset===staged) pendingPreset=null; throw error; }
        if(own!==revision||!waiting)return {saved:true,applied:false,notice:'风扇挡位已保存；较新的控制意图由 YMCC 处理'};
        deps.display({preset,active:true,pending:true,notice:'风扇挡位已保存，等待 YMCC 原生命周期；不重复启动'});
        return {saved:true,applied:false,pending:true,notice:'风扇挡位已保存，等待 YMCC 原生命周期'};
      }
      return actions.execute(command,args,context);
    },
    get pendingIntent(){return requested&&waiting&&ownerCancellationRevision===deps.owner.controlCancellationRevision;},
    stop(){++revision;waiting=false;pendingPreset=null;requested=false;},
  };
  return actions;
}
