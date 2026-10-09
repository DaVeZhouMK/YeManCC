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
  const state = (notice: string) => deps.display({ preset:deps.currentPreset(),active:deps.enabled(),pending:deps.owner.recoveryActive,notice });
  const requirePower = async (context: MirrorMutationContext) => {
    context.checkpoint();
    const power = await deps.power();
    context.checkpoint();
    if (deps.uiBusy()) throw new Error('FAN_MIRROR_BUSY');
    if (power.generation !== context.generation || power.phase !== 'ready' || power.hardwareWritesAllowed !== true)
      throw new Error('FAN_MIRROR_POWER_NOT_READY');
  };
  return {
    async execute(command: 'fan.setPreset' | 'fan.setEnabled', args: Record<string, unknown>, context: MirrorMutationContext): Promise<MirrorActionResult> {
      if (!deps.featureAllowed()) throw new Error('FAN_MIRROR_NOT_ENABLED');
      if (executing || deps.uiBusy()) throw new Error('FAN_MIRROR_BUSY');
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
            deps.updateActive(false); deps.updateDuty(0); state('风扇控制已关闭');
            return { saved:false,applied:true,notice:'风扇控制已关闭（原生命周期已确认）' };
          } catch (error) {
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
          if (!gate.allowed || !gate.writeReady) throw new Error('FAN_MIRROR_CAPABILITY_NOT_READY');
          await requirePower(context);
          if (command === 'fan.setPreset') await deps.owner.applyPreset(preset,nodes);
          else await deps.owner.apply(nodes);
          deps.updateActive(true); state('风扇设置已由 YMCC 原生命周期确认');
          return { saved,applied:true,notice:'风扇设置已由 YMCC 原生命周期确认' };
        } catch (error) {
          // Keep the same existing owner and recovery intent. No second recovery/retry loop.
          const pending = deps.owner.recoveryActive || deps.owner.hasControlIntent;
          if (command === 'fan.setEnabled' && pending) {
            deps.updateActive(true); state('原风扇生命周期正在恢复');
            return { saved,applied:false,pending:true,notice:'控制意图已登记，原风扇生命周期正在恢复' };
          }
          deps.updateActive(false); deps.updateDuty(0); state('风扇下发失败，原生命周期状态保留');
          throw error;
        }
      } finally { executing = false; }
    },
  };
}
