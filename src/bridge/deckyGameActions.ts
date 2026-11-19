// Legacy game input adapters remain inert for older callers. Input belongs to
// global settings and boot modes; performance/FPS keep their existing owners.
import type { ContinuousControls } from './performanceControls';
import type { VirtualTargetPolicy } from './virtualTargetPolicy';
import { gamePolicyKey } from './gamePolicyHysteresis';
import type { DetectedGame } from './gamedetect';
import type { PowerLifecycleState } from './api';
import type { GameCustomConfig, GameCustomProfile, PerformanceScheduleConfig } from './performanceSchedule';
import type { MirrorActionResult } from './deckyFanActions';
import type { RelayMutationContext } from './deckyMirrorRelay';
export interface GameMirrorDeps {
  targetPolicy?:()=>VirtualTargetPolicy|null;
  featureAllowed:()=>boolean;
  game:()=>DetectedGame|null;
  label:(game:DetectedGame)=>string;
  power:()=>Promise<PowerLifecycleState>;
  acquire:(owner:string)=>(()=>void)|null;
  load:()=>Promise<GameCustomConfig>;
  current:()=>GameCustomConfig;
  schedule:()=>Promise<PerformanceScheduleConfig>;
  currentSchedule:()=>PerformanceScheduleConfig|null;
  save:(config:GameCustomConfig)=>Promise<void>;
  closeJoyxoff:()=>Promise<boolean>;
  rememberWholeInput?:boolean;
  finishInput?:(saved:boolean,context:RelayMutationContext)=>Promise<MirrorActionResult>;
  /** Legacy caller seam; input intents no longer read or migrate performance banks. */
  continuous?:()=>Promise<ContinuousControls>;
  gyroDefaults?:()=>Promise<GameMirrorGyroDefaults>;
}
// Server-side, ephemeral admission data. Never serialized into the plugin
// snapshot, sent by the client, written to a settings file or used as a key.
export interface GameMirrorAdmission {entry:string;schedule:string;lossless?:string;gyro?:string;speed?:string;profile?:string;profileSource?:string;}
export function makeGameMirrorAdmission(entry:GameCustomProfile|undefined,schedule:PerformanceScheduleConfig,lossless?:string,gyro?:string):GameMirrorAdmission {
  return {entry:JSON.stringify(entry??null),schedule:JSON.stringify(schedule),...(lossless !== undefined ? {lossless} : {}),...(gyro !== undefined ? {gyro} : {})};
}
export interface GameMirrorGyroDefaults {virtualPad?:boolean;padPersona?:Exclude<NonNullable<GameCustomProfile['padPersona']>,'follow'>;enabled:boolean;preset:NonNullable<GameCustomProfile['gyroPreset']>;source:string;}
export function needsGameMirrorInputMemory(_entry: GameCustomProfile | undefined, _nextPad?: GameCustomProfile['padPersona']): boolean {
  return false;
}
export function rememberGameMirrorInput(_entry: GameCustomProfile, _defaults: GameMirrorGyroDefaults): void {}
export function readGameMirrorGyro(_entry: GameCustomProfile | undefined, defaults: GameMirrorGyroDefaults) {
  return { enabled: defaults.virtualPad !== false && defaults.padPersona !== 'disabled' && defaults.enabled, preset: defaults.preset };
}
export function gameMirrorIdentity(game:DetectedGame|null):string {
  if(!game)return '';
  const identity:(string|number)[]=[gamePolicyKey(game),game.path,game.pid,game.processCreated];
  // Preserve the legacy opaque tuple; native leases append only when supplied.
  if(game.identityGeneration!==undefined)identity.push(game.identityGeneration);
  return JSON.stringify(identity);
}
/** Construct performance data without copying obsolete per-game input fields. */
export function makeGameMirrorEntry(current: GameCustomProfile | undefined, schedule: PerformanceScheduleConfig,
  label: string): GameCustomProfile {
  const entry: GameCustomProfile = {
    ...(current ? structuredClone(current) : {}),
    controls: current?.controls ? structuredClone(current.controls) : undefined,
    smtIndependent: current?.smtIndependent,
    displayName: current?.displayName || label,
    enabled: current?.enabled ?? false,
    ac: { ...(current?.ac ?? schedule.profiles.ac.performance) },
    dc: { ...(current?.dc ?? schedule.profiles.dc.performance) },
  };
  delete entry.padPersona;
  delete entry.gyroOverride;
  delete entry.gyroPreset;
  delete entry.inputOverrides;
  return entry;
}
export function createGameMirrorActions(_deps: GameMirrorDeps) {
  return { async execute(command: string, args: Record<string, unknown>, _context: RelayMutationContext): Promise<MirrorActionResult> {
    if (command !== 'game.setField' || Object.keys(args).length !== 2 || typeof args.field !== 'string' || typeof args.value !== 'string') throw new Error('GAME_MIRROR_INVALID_REQUEST');
    throw new Error('GAME_MIRROR_INPUT_GLOBAL');
  } };
}
