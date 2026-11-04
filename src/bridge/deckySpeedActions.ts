// EXE/PID-bound live speed intent. Delegate to the existing OpenSpeedy owner;
// no speed profile, settings write, injection implementation or independent lock.
import { gameMirrorIdentity } from './deckyGameActions';
import type { DetectedGame } from './gamedetect';
import type { PowerLifecycleState } from './api';
import type { RelayMutationContext } from './deckyMirrorRelay';
import type { GameSpeedState, SpeedResult, SpeedOperationAdmission } from './speedhack';
export interface SpeedMirrorNativeState {active:boolean;connected:boolean;connection:string;runId:string;connectionRevision:number;powerGeneration:number;hardwareWritesAllowed:boolean;}
export function createSpeedMirrorActions(deps: {
  featureAllowed: () => boolean; game: () => DetectedGame | null; power: () => Promise<PowerLifecycleState>;
  acquire: (owner: string) => (() => void) | null; presets: number[]; blocked: (game: DetectedGame) => boolean;
  state: () => GameSpeedState | null; apply: (game: DetectedGame, factor: number, admission: SpeedOperationAdmission) => Promise<SpeedResult>;
  clear: (game: DetectedGame, admission: SpeedOperationAdmission) => Promise<SpeedResult>;
  nativeState?:()=>Promise<SpeedMirrorNativeState>;
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
      if (!state?.cleanupPending && factor === (own && state?.enabled ? state.factor : 1)) return { saved:false, applied:false, notice:'游戏变速倍率未改变' };
      // The original owner rechecks the native valve and PID creation identity.
      const admission:SpeedOperationAdmission={generation:context.generation,nativePeer:context.nativePeer,checkpoint:async()=>{
        checkpoint();
        if(context.nativePeer){
          if(!deps.nativeState)throw Error('SPEED_MIRROR_NATIVE_ADMISSION_REQUIRED');
          const current=await deps.nativeState();checkpoint();
          if(current.active!==true||current.connected!==true||current.connection!==context.nativePeer.connection||current.runId!==context.nativePeer.runId||current.connectionRevision!==context.nativePeer.connectionRevision||current.powerGeneration!==context.generation||current.hardwareWritesAllowed!==true)throw Error('SPEED_MIRROR_NATIVE_ADMISSION_CHANGED');
        }else{
          const current=await deps.power();checkpoint();
          if(current.generation!==context.generation||current.phase!=='ready'||!current.hardwareWritesAllowed)throw Error('SPEED_MIRROR_POWER_NOT_READY');
        }
      }};
      const result = await (factor === 1 ? deps.clear(target,admission) : deps.apply(target, factor,admission));
      if (!result.ok || result.safeFallback || result.skipped) return {saved:false,applied:false,pending:result.reason==='unconfirmed',notice:result.msgs.join('；') || '变速结果待确认，不自动重放'};
      try{await admission.checkpoint();}catch{return {saved:false,applied:false,pending:true,notice:'变速请求完成时上下文已变化；实际效果待确认，不自动重放'};}
      return {saved:false,applied:true,notice:factor === 1 ? '已恢复 1× 游戏变速' : `已应用 ${factor}× 游戏变速`};
    } finally { release(); }
  } };
}
