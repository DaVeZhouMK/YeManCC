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
  start(options: { manualRecovery: boolean; reason: string }): Promise<{ allowed: boolean; writeReady: boolean; reason?: string }>;
  apply(nodes: readonly FanNode[]): Promise<FanState>;
  applyPreset(preset: FanPreset, nodes: readonly FanNode[]): Promise<FanState>;
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
const presetNames = new Set<FanPreset>(['soft','balanced','aggressive']);
export function createFanMirrorActions(deps: FanMirrorDeps) {
  let executing = false;
  let requested = false, revision = 0, waiting = false;
  const acceptedContexts=new WeakSet<MirrorMutationContext>();
  let pendingPreset: {preset:FanPreset;nodes:FanNode[]}|null = null;
  const state = (notice: string) => deps.display({ preset:deps.currentPreset(),active:deps.enabled(),pending:deps.owner.recoveryActive,notice });
  const requirePower = async (context: MirrorMutationContext) => {
    context.checkpoint();
    const power = await deps.power();
    context.checkpoint();
    if (deps.uiBusy()) throw new Error('FAN_MIRROR_BUSY');
    if (power.generation !== context.generation || power.phase !== 'ready' || power.hardwareWritesAllowed !== true)
      throw new Error('FAN_MIRROR_POWER_NOT_READY');
  };
  const actions = {
    async execute(command: 'fan.setPreset' | 'fan.setEnabled', args: Record<string, unknown>, context: MirrorMutationContext): Promise<MirrorActionResult> {
      if (!deps.featureAllowed()) throw new Error('FAN_MIRROR_NOT_ENABLED');
      if (executing && !acceptedContexts.has(context) || deps.uiBusy()) throw new Error('FAN_MIRROR_BUSY');
      if (command === 'fan.setPreset') {
        if (Object.keys(args).length !== 1 || !presetNames.has(args.preset as FanPreset)) throw new Error('FAN_MIRROR_INVALID_PRESET');
      } else if (command === 'fan.setEnabled') {
        if (Object.keys(args).length !== 1 || typeof args.enabled !== 'boolean') throw new Error('FAN_MIRROR_INVALID_SWITCH');
      } else throw new Error('FAN_MIRROR_COMMAND_DENIED');
      executing = true;
      try {
        await requirePower(context);
        const desired = command === 'fan.setEnabled' ? args.enabled as boolean : null;
        if (desired === false) {
          // The original owner cancels recovery, restores OEM, and releases the existing lease.
          try {
            await deps.owner.disable();
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
          state('风扇挡位已保存，正在确认控制状态');
          context.checkpoint();
          if (!deps.enabled() && !deps.owner.hasControlIntent) {
            state('风扇挡位已保存，控制仍关闭');
            return { saved:true,applied:false,notice:'风扇挡位已保存，未启用风扇控制' };
          }
        } else if (deps.enabled() && deps.owner.hasControlIntent && deps.owner.controlReady && deps.owner.state === 'ready' && !deps.owner.recoveryActive) {
          // A cached UI switch is intent, not proof that a cold/replaced original
          // owner is controlling. Only a live local original owner can dedupe.
          state('风扇控制已开启'); return { saved:false,applied:true,notice:'风扇控制已开启，无重复启用调用' };
        }
        await requirePower(context);
        try {
          const gate = await deps.owner.start({manualRecovery:true,reason:'user-action-before-control-write'});
          if (deps.observeCapability) await deps.observeCapability(gate.allowed);
          context.checkpoint();
          if (!gate.allowed) throw new Error('FAN_MIRROR_CAPABILITY_NOT_READY');
          // writeReady may be false while the original owner is admitting a slow Host.
          // Its apply() owns this wait and recovery; the mirror must not invent a deadline.
          await requirePower(context);
          const latest=pendingPreset;pendingPreset=null;
          if (latest) await deps.owner.applyPreset(latest.preset,latest.nodes);
          else if (command === 'fan.setPreset') await deps.owner.applyPreset(preset,nodes);
          else await deps.owner.apply(nodes);
          context.checkpoint();
          // Apply only a newer explicit selection; no mirror-side retry loop.
          while(pendingPreset){const newer=pendingPreset;pendingPreset=null;await requirePower(context);await deps.owner.applyPreset(newer.preset,newer.nodes);context.checkpoint();}
          deps.updateActive(true); state('风扇设置已由 YMCC 原生命周期确认');
          return { saved,applied:true,notice:'风扇设置已由 YMCC 原生命周期确认' };
        } catch (error) {
          // Keep the same existing owner and recovery intent. No second recovery/retry loop.
          context.checkpoint(); // A late result must not undo an explicit OFF/new intent.
          const pending = deps.owner.recoveryActive || deps.owner.hasControlIntent;
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
    async request(command:'fan.setPreset'|'fan.setEnabled',args:Record<string,unknown>,context:MirrorMutationContext):Promise<MirrorActionResult> {
      if(!deps.featureAllowed())throw Error('FAN_MIRROR_NOT_ENABLED');
      if(command==='fan.setEnabled' && Object.keys(args).length===1 && typeof args.enabled==='boolean') {
        await requirePower(context);
        if(!args.enabled){
          requested=false;waiting=false;pendingPreset=null;const own=++revision;
          deps.updateActive(false);deps.display({preset:deps.currentPreset(),active:false,pending:true,notice:'开启意图已取消，等待 YMCC 确认交还风扇'});
          const owned:MirrorMutationContext={generation:context.generation,checkpoint:()=>{if(own!==revision||!deps.featureAllowed())throw Error('FAN_MIRROR_INTENT_CANCELED');}};
          acceptedContexts.add(owned);
          void actions.execute(command,args,owned).catch(()=>{if(own===revision)deps.display({preset:deps.currentPreset(),active:false,pending:false,notice:'开启意图已取消；YMCC 风扇交还尚未确认'});});
          return {saved:false,applied:false,pending:true,notice:'开启意图已取消，等待 YMCC 确认交还风扇'};
        }
        if(waiting)return {saved:false,applied:false,pending:true,notice:'已开启，等待 YMCC 风扇真实信号；不重复启动'};
        if(deps.enabled()&&deps.owner.hasControlIntent&&deps.owner.controlReady&&deps.owner.state==='ready'&&!deps.owner.recoveryActive)
          return {saved:false,applied:true,notice:'风扇控制已由 YMCC 确认开启'};
        requested=true;waiting=true;const own=++revision;
        deps.updateActive(true);deps.display({preset:deps.currentPreset(),active:true,pending:true,notice:'已开启，等待 YMCC 风扇真实信号'});
        const owned:MirrorMutationContext={generation:context.generation,checkpoint:()=>{if(own!==revision||!deps.featureAllowed())throw Error('FAN_MIRROR_INTENT_CANCELED');}};
        acceptedContexts.add(owned);
        void actions.execute(command,args,owned).then(result=>{
          if(own!==revision)return;waiting=false;
          deps.display({preset:deps.currentPreset(),active:true,pending:result.pending===true,notice:result.notice});
        }).catch(()=>{
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
        await requirePower(context);const own=revision,preset=args.preset as FanPreset,nodes=deps.curve(preset).map(n=>({...n}));
        await deps.save(nodes,preset);context.checkpoint();
        if(own!==revision||!waiting)return {saved:true,applied:false,notice:'风扇挡位已保存；较新的控制意图由 YMCC 处理'};
        pendingPreset={preset,nodes};
        deps.display({preset,active:true,pending:true,notice:'风扇挡位已保存，等待 YMCC 原生命周期；不重复启动'});
        return {saved:true,applied:false,pending:true,notice:'风扇挡位已保存，等待 YMCC 原生命周期'};
      }
      return actions.execute(command,args,context);
    },
    get pendingIntent(){return requested&&waiting;},
    stop(){++revision;waiting=false;pendingPreset=null;requested=false;},
  };
  return actions;
}
