// EXE/PID-bound live speed intent. Delegate to the existing OpenSpeedy owner;
// no speed profile, settings write, injection implementation or independent lock.
import { gameMirrorIdentity } from './deckyGameActions';
import type { DetectedGame } from './gamedetect';
import type { PowerLifecycleState } from './api';
import type { RelayMutationContext } from './deckyMirrorRelay';
import type { GameSpeedState, SpeedResult } from './speedhack';
export function createSpeedMirrorActions(deps: {
  featureAllowed: () => boolean; game: () => DetectedGame | null; power: () => Promise<PowerLifecycleState>;
  acquire: (owner: string) => (() => void) | null; presets: number[]; blocked: (game: DetectedGame) => boolean;
  state: () => GameSpeedState | null; apply: (game: DetectedGame, factor: number) => Promise<SpeedResult>;
  clear: (game: DetectedGame) => Promise<SpeedResult>;
}) {
  return { async execute(args: Record<string, unknown>, context: RelayMutationContext) {
    if (Object.keys(args).length !== 2 || args.field !== 'speedFactor' || typeof args.value !== 'string' ||
      !deps.presets.some(factor => String(factor) === args.value)) throw Error('SPEED_MIRROR_INVALID_REQUEST');
    const factor = Number(args.value), selected = deps.game();
    if (!deps.featureAllowed() || !selected || !context.gameIdentity || gameMirrorIdentity(selected) !== context.gameIdentity ||
      !Number.isSafeInteger(selected.pid) || selected.pid <= 0 || !selected.processCreated) throw Error('SPEED_MIRROR_TARGET_CHANGED');
    const target = structuredClone(selected), admittedSource = context.gameAdmission?.speed;
    if (typeof admittedSource !== 'string') throw Error('SPEED_MIRROR_SOURCE_REQUIRED');
    const checkpoint = () => { context.checkpoint(); if (!deps.featureAllowed() || gameMirrorIdentity(deps.game()) !== context.gameIdentity) throw Error('SPEED_MIRROR_TARGET_CHANGED'); };
    checkpoint();
    if (deps.blocked(target)) throw Error('SPEED_MIRROR_UNSUPPORTED');
    const release = deps.acquire('decky-game-speed');
    if (!release) throw Error('SPEED_MIRROR_BUSY');
    try {
      const power = await deps.power(); checkpoint();
      if (power.generation !== context.generation || power.phase !== 'ready' || !power.hardwareWritesAllowed) throw Error('SPEED_MIRROR_POWER_NOT_READY');
      const state = deps.state();
      if (JSON.stringify(state) !== admittedSource) throw Error('SPEED_MIRROR_SOURCE_CHANGED');
      const own = state?.pid === target.pid && state.processCreated === target.processCreated;
      if (factor === (own && state?.enabled ? state.factor : 1)) return { saved:false, applied:false, notice:'游戏变速倍率未改变' };
      // The original owner rechecks the native valve and PID creation identity.
      const result = await (factor === 1 ? deps.clear(target) : deps.apply(target, factor));
      if (!result.ok || result.safeFallback || result.skipped) return {saved:false,applied:false,notice:result.msgs.join('；') || '未应用变速，保留安全状态'};
      return {saved:false,applied:true,notice:factor === 1 ? '已恢复 1× 游戏变速' : `已应用 ${factor}× 游戏变速`};
    } finally { release(); }
  } };
}
