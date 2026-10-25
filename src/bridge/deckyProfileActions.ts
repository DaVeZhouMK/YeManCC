// Dedicated-profile intents use the existing game command, mutex and store owners.
import {gamePolicyKey} from './gamePolicyHysteresis';
import {gameMirrorIdentity,makeGameMirrorEntry,rememberGameMirrorInput,type GameMirrorGyroDefaults} from './deckyGameActions';
import {requireVirtualTargetAllowed,type VirtualTargetPolicy} from './virtualTargetPolicy';
import type {DetectedGame} from './gamedetect';
import type {ContinuousPerformanceSnapshot} from './performanceControls';
import type {GameCustomConfig,GameCustomProfile,GameCustomProfileTarget,PerformanceScheduleConfig} from './performanceSchedule';
import type {FrameRatePair} from './frameRateLimits';
import type {RelayMutationContext} from './deckyMirrorRelay';
export const gameProfileRevision=(config:GameCustomConfig,key:string)=>JSON.stringify([config.entries[key]??null,config.rtss[key]??null]);
export const gameProfileImportSource=(s:ContinuousPerformanceSnapshot)=>JSON.stringify([s.identity,s.controls,s.frameRates]);
export interface ProfileMirrorDeps {
  allowed:()=>boolean;game:()=>DetectedGame|null;label:(game:DetectedGame)=>string;
  power:()=>Promise<{phase:string;hardwareWritesAllowed:boolean;generation:number}>;
  acquire:(label:string)=>(()=>void)|null;load:()=>Promise<GameCustomConfig>;
  schedule:()=>Promise<PerformanceScheduleConfig>;currentSchedule:()=>PerformanceScheduleConfig|null;
  snapshot:()=>Promise<ContinuousPerformanceSnapshot>;gyro:()=>Promise<GameMirrorGyroDefaults>;
  targetPolicy:()=>VirtualTargetPolicy|null;
  import:(target:GameCustomProfileTarget,revision:string,entry:GameCustomProfile,frames:FrameRatePair,checkpoint:()=>void)=>Promise<void>;
  remove:(target:GameCustomProfileTarget,revision:string)=>Promise<void>;
  restore:()=>Promise<string>;
}
export function createProfileMirrorActions(deps:ProfileMirrorDeps) {
 return {async execute(args:Record<string,unknown>,context:RelayMutationContext) {
  if(Object.keys(args).length!==2||args.field!=='profile'||!['dedicated','global'].includes(args.value as string))throw Error('GAME_PROFILE_INVALID_REQUEST');
  const admission=context.gameAdmission;
  if(!admission||typeof admission.profile!=='string'||args.value==='dedicated'&&typeof admission.profileSource!=='string'||!context.gameIdentity)throw Error('GAME_PROFILE_ADMISSION_REQUIRED');
  const captured=deps.game();
  if(!captured||!gamePolicyKey(captured)||captured.pid<=0||!captured.processCreated||gameMirrorIdentity(captured)!==context.gameIdentity)throw Error('GAME_PROFILE_TARGET_CHANGED');
  const game=structuredClone(captured),key=gamePolicyKey(game),target={exe:key,pid:game.pid,processCreated:String(game.processCreated),generation:context.generation};
  const checkpoint=()=>{context.checkpoint();if(!deps.allowed()||gameMirrorIdentity(deps.game())!==context.gameIdentity)throw Error('GAME_PROFILE_TARGET_CHANGED');};
  const gate=async()=>{checkpoint();const p=await deps.power();checkpoint();if(p.phase!=='ready'||p.hardwareWritesAllowed!==true||p.generation!==context.generation)throw Error('GAME_PROFILE_POWER_NOT_READY');};
  await gate();const release=deps.acquire('decky-game-profile');if(!release)throw Error('GAME_PROFILE_BUSY');
  try {
   const config=await deps.load();checkpoint();if(gameProfileRevision(config,key)!==admission.profile)throw Error('GAME_PROFILE_SOURCE_CHANGED');
   if(args.value==='global') {
    if(!config.entries[key])throw Error('GAME_PROFILE_NOT_FOUND');
    await gate();await deps.remove(target,admission.profile);
   } else {
    if(config.entries[key]?.enabled!==false&&config.entries[key])return {saved:false,applied:false,notice:'当前游戏已使用专属配置'};
    const schedule=await deps.schedule();checkpoint();if(JSON.stringify(schedule)!==admission.schedule)throw Error('GAME_PROFILE_SOURCE_CHANGED');
    const source=await deps.snapshot();checkpoint();if(gameProfileImportSource(source)!==admission.profileSource||source.identity.scope!=='global')throw Error('GAME_PROFILE_SOURCE_CHANGED');
    const gyro=await deps.gyro();checkpoint();if(gyro.source!==admission.gyro)throw Error('GAME_PROFILE_SOURCE_CHANGED');
    requireVirtualTargetAllowed(gyro.padPersona,deps.targetPolicy(),true);
    const entry=makeGameMirrorEntry(undefined,schedule,deps.label(game));
    entry.enabled=true;entry.controls=structuredClone(source.controls);rememberGameMirrorInput(entry,gyro);
    entry.hyperThreadPolicyEnabled=source.hyperThreadMode!=='default';entry.hyperThreadPolicy=source.hyperThreadMode;
    await gate();
    const fresh=await deps.snapshot();checkpoint();if(gameProfileImportSource(fresh)!==admission.profileSource)throw Error('GAME_PROFILE_SOURCE_CHANGED');
    const freshGyro=await deps.gyro();checkpoint();if(freshGyro.source!==admission.gyro)throw Error('GAME_PROFILE_SOURCE_CHANGED');
    await gate();
    await deps.import(target,admission.profile,entry,structuredClone(source.frameRates),()=>{checkpoint();if(JSON.stringify(deps.currentSchedule())!==admission.schedule)throw Error('GAME_PROFILE_SOURCE_CHANGED');});
   }
   // The file commit has succeeded. A later apply failure must not be reported as an unsaved edit.
   const notice=args.value==='global'?'已删除游戏专属配置，恢复全局配置':'已将当前配置导入游戏专属';
   try {const owner=await deps.restore();return {saved:true,applied:owner!=='none',pending:owner==='none',notice:owner==='none'?notice+'；等待 YMCC 应用':notice};}
   catch(error) {return {saved:true,applied:false,pending:true,notice:notice+'；应用未完成：'+(error as Error).message};}
  } finally {release();}
 }};
}
