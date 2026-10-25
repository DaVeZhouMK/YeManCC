import type {ContinuousControls} from './performanceControls';
// Thin EXE mirror intent adapter. The original store/lock/resident owners remain
// authoritative. This module creates no timers, files, presets or hardware owner.
import {currentVirtualTargetPolicy} from './virtualTargetPolicyState';
import {requireVirtualTargetAllowed,type VirtualTargetPolicy} from './virtualTargetPolicy';
import {gamePolicyKey} from './gamePolicyHysteresis';
import type {DetectedGame} from './gamedetect';
import type {PowerLifecycleState} from './api';
import type {GameCustomConfig,GameCustomProfile,PerformanceScheduleConfig} from './performanceSchedule';
import type {MirrorActionResult} from './deckyFanActions';
import type {RelayMutationContext} from './deckyMirrorRelay';
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
const pad=new Set<NonNullable<GameCustomProfile['padPersona']>>(['follow','disabled','steamdeck','dualsense-edge','elite']);
const gyro=new Set<NonNullable<GameCustomProfile['gyroOverride']>>(['follow','on','off','fps','racing','custom','steam']);
const gyroPresets=new Set<NonNullable<GameCustomProfile['gyroPreset']>>(['fps','racing','custom','steam']);
export interface GameMirrorGyroDefaults {virtualPad?:boolean;padPersona?:Exclude<NonNullable<GameCustomProfile['padPersona']>,'follow'>;enabled:boolean;preset:NonNullable<GameCustomProfile['gyroPreset']>;source:string;}
// Native-pad gyro follow is a dormant coupled reset, not active global inheritance.
export function needsGameMirrorInputMemory(entry:GameCustomProfile|undefined,nextPad:GameCustomProfile['padPersona']=entry?.padPersona):boolean {
  return !entry || !entry.padPersona || entry.padPersona==='follow' || nextPad!=='disabled' && ['follow','on'].includes(entry.gyroOverride??'follow');
}
export function rememberGameMirrorInput(entry:GameCustomProfile,defaults:GameMirrorGyroDefaults):void {
  if(!defaults.padPersona)throw new Error('GAME_MIRROR_INPUT_SOURCE_REQUIRED');
  if(entry.padPersona==='follow')entry.padPersona=defaults.padPersona;
  if(['follow','on'].includes(entry.gyroOverride??'follow')){
    entry.gyroPreset=defaults.preset;
    entry.gyroOverride=entry.padPersona==='disabled'||!defaults.enabled?'off':defaults.preset;
  }
}
export function readGameMirrorGyro(entry:GameCustomProfile|undefined,defaults:GameMirrorGyroDefaults) {
  const override=entry?.gyroOverride??'follow';
  return {enabled:entry?.padPersona==='disabled'||(entry?.padPersona??'follow')==='follow'&&defaults.virtualPad===false?false:override==='follow'?defaults.enabled:override!=='off',
    preset:gyroPresets.has(override as any)?override as NonNullable<GameCustomProfile['gyroPreset']>
      : (override==='follow'||override==='on')?defaults.preset
      : gyroPresets.has(entry?.gyroPreset as any)?entry!.gyroPreset!:defaults.preset};
}
const fields:Record<string,ReadonlySet<string>>={gyroEnabled:new Set(['on','off']),gyroPreset:gyroPresets,padPersona:pad,gyroOverride:gyro};
export function gameMirrorIdentity(game:DetectedGame|null):string {
  if(!game)return '';
  const identity:(string|number)[]=[gamePolicyKey(game),game.path,game.pid,game.processCreated];
  // Preserve the legacy opaque tuple; native leases append only when supplied.
  if(game.identityGeneration!==undefined)identity.push(game.identityGeneration);
  return JSON.stringify(identity);
}
/** Same record/default construction as the original panel's makeEntry.
 * No new interpretation: mode-less legacy sides use the original panel's
 * selected-mode fallback only when the user explicitly edits, never on read.
 */
export function makeGameMirrorEntry(current:GameCustomProfile|undefined,schedule:PerformanceScheduleConfig,
  label:string):GameCustomProfile {
  return {...(current?structuredClone(current):{}),controls:current?.controls?structuredClone(current.controls):undefined,smtIndependent:current?.smtIndependent,displayName:current?.displayName||label,enabled:current?.enabled??false,
    ac:{...(current?.ac??schedule.profiles.ac.performance)},dc:{...(current?.dc??schedule.profiles.dc.performance)},
    padPersona:pad.has(current?.padPersona as NonNullable<GameCustomProfile['padPersona']>)?current!.padPersona!:'follow',
    gyroOverride:gyro.has(current?.gyroOverride as NonNullable<GameCustomProfile['gyroOverride']>)?current!.gyroOverride!:'follow',
    gyroPreset:gyroPresets.has(current?.gyroOverride as any)?current!.gyroOverride as GameCustomProfile['gyroPreset']:current?.gyroPreset};
}
export function createGameMirrorActions(deps:GameMirrorDeps) {
  return {async execute(command:string,args:Record<string,unknown>,context:RelayMutationContext):Promise<MirrorActionResult>{
    if(command!=='game.setField'||Object.keys(args).length!==2||typeof args.field!=='string'||typeof args.value!=='string')throw new Error('GAME_MIRROR_INVALID_REQUEST');
    const field=args.field,value=args.value,allowed=Object.prototype.hasOwnProperty.call(fields,field)?fields[field]:undefined;
    if(!allowed||!allowed.has(value)||field==='padPersona'&&value==='follow'||field==='gyroOverride'&&value==='follow')throw new Error('GAME_MIRROR_INVALID_FIELD');
    if(!deps.featureAllowed())throw new Error('GAME_MIRROR_NOT_ENABLED');
    if(typeof context.gameIdentity!=='string'||!context.gameIdentity)throw new Error('GAME_MIRROR_IDENTITY_REQUIRED');
    if(!context.gameAdmission||typeof context.gameAdmission.entry!=='string'||typeof context.gameAdmission.schedule!=='string')throw new Error('GAME_MIRROR_ADMISSION_REQUIRED');
    const admittedIdentity=context.gameIdentity,admittedGeneration=context.generation;
    const admission={entry:context.gameAdmission.entry,schedule:context.gameAdmission.schedule,gyro:context.gameAdmission.gyro};
    // Freeze the identity admitted by the relay, not a newly selected game.
    const captured=deps.game();
    if(!captured||!gamePolicyKey(captured)||!Number.isSafeInteger(captured.pid)||captured.pid<=0||!captured.processCreated||gameMirrorIdentity(captured)!==admittedIdentity)throw new Error('GAME_MIRROR_TARGET_CHANGED');
    const target=structuredClone(captured),key=gamePolicyKey(target);
    const checkpoint=()=>{context.checkpoint();if(!deps.featureAllowed()||gameMirrorIdentity(deps.game())!==admittedIdentity)throw new Error('GAME_MIRROR_TARGET_CHANGED');};
    checkpoint();
    const release=deps.acquire('decky-game-mirror');
    if(!release)throw new Error('GAME_MIRROR_BUSY');
    const requirePower=async()=>{checkpoint();const state=await deps.power();checkpoint();if(state.generation!==admittedGeneration||state.phase!=='ready'||state.hardwareWritesAllowed!==true)throw new Error('GAME_MIRROR_POWER_NOT_READY');};
    try {
      await requirePower();
      // Read fresh only AFTER acquiring the existing panel's shared mutex.
      const config=await deps.load();checkpoint();
      const current=config.entries[key];
      // Only the explicit profile import owns activation. Input edits cannot create
      // or revive an EXE profile; the host routes global-scope input to input CAS.
      if(!current || current.enabled===false)throw new Error('GAME_MIRROR_PROFILE_REQUIRED');
      const policy=()=>deps.targetPolicy?deps.targetPolicy():currentVirtualTargetPolicy();
      requireVirtualTargetAllowed(field==='padPersona'?value:current?.padPersona,policy(),
        field==='padPersona'||field==='gyroPreset'||field==='gyroEnabled'&&value==='on'||field==='gyroOverride'&&value!=='off'||current?.enabled!==true);
      // The normalizer/cache can first warm during this fresh load, or an
      // external original-file edit may have no renderer event. Revision alone
      // cannot authorize an unseen remembered policy. Reject, then refresh.
      if(JSON.stringify(current??null)!==admission.entry)throw new Error('GAME_MIRROR_SOURCE_CHANGED');
      const documentVersion=JSON.stringify(config);
      const requireDocument=()=>{checkpoint();if(JSON.stringify(deps.current())!==documentVersion||JSON.stringify(deps.currentSchedule())!==admission.schedule)throw new Error('GAME_MIRROR_SOURCE_CHANGED');};
      if((field==='gyroOverride'||field==='gyroEnabled'||field==='gyroPreset')&&current?.padPersona==='disabled')throw new Error('GAME_MIRROR_GYRO_LOCKED');
      const mustRememberInput=deps.rememberWholeInput===true && needsGameMirrorInputMemory(current,field==='padPersona'?value as GameCustomProfile['padPersona']:current?.padPersona);
      let gyroDefaults:GameMirrorGyroDefaults|undefined;
      if(field==='gyroEnabled'||field==='gyroPreset'||mustRememberInput){
        if(!deps.gyroDefaults||typeof admission.gyro!=='string')throw new Error('GAME_MIRROR_GYRO_SOURCE_REQUIRED');
        gyroDefaults=await deps.gyroDefaults();checkpoint();
        if(gyroDefaults.source!==admission.gyro)throw new Error('GAME_MIRROR_SOURCE_CHANGED');
        if((field==='gyroEnabled'||field==='gyroPreset')&&(current?.padPersona??'follow')==='follow'&&gyroDefaults.virtualPad===false)throw new Error('GAME_MIRROR_GYRO_LOCKED');
        if(field==='gyroPreset'&&!readGameMirrorGyro(current,gyroDefaults).enabled)throw new Error('GAME_MIRROR_GYRO_DISABLED');
      }
      if(!mustRememberInput && field==='gyroEnabled' && current?.enabled===true){const remembered=readGameMirrorGyro(current,gyroDefaults!).preset;if(current.gyroOverride===(value==='on'?remembered:'off') && current.gyroPreset===remembered)return {saved:false,applied:false,notice:'EXE 已记忆此陀螺仪开关，未重复保存或下发'};}
      const sameDerivedFlags=(field!=='padPersona'||value!=='disabled'||current?.gyroOverride==='follow');
      if(!mustRememberInput&&current?.enabled===true&&current[field as keyof GameCustomProfile]===value&&sameDerivedFlags){
        if(field==='padPersona'&&deps.finishInput){await requirePower();requireDocument();return await deps.finishInput(false,context);}
        return {saved:false,applied:false,notice:'EXE 已记忆此设置，未重复保存或下发'};
      }
      const schedule=await deps.schedule();checkpoint();
      if(JSON.stringify(schedule)!==admission.schedule)throw new Error('GAME_MIRROR_SOURCE_CHANGED');
      requireDocument();
      const next=makeGameMirrorEntry(current,schedule,deps.label(target));
      // Input selection leaves original performance banks untouched.
      // Preserve the already active profile. Only the top profile action imports it.
      next.enabled=true;
      if(mustRememberInput)rememberGameMirrorInput(next,gyroDefaults!);
      if(field==='padPersona'){next.padPersona=value as NonNullable<GameCustomProfile['padPersona']>;if(value==='disabled')next.gyroOverride='follow';}
      if(field==='gyroOverride'){next.gyroOverride=value as NonNullable<GameCustomProfile['gyroOverride']>;if(gyroPresets.has(value as any))next.gyroPreset=value as NonNullable<GameCustomProfile['gyroPreset']>;}
      if(field==='gyroEnabled'){const remembered=readGameMirrorGyro(current,gyroDefaults!).preset;next.gyroPreset=remembered;next.gyroOverride=value==='on'?remembered:'off';}
      if(field==='gyroPreset'){next.gyroPreset=value as NonNullable<GameCustomProfile['gyroPreset']>;next.gyroOverride=next.gyroPreset;}
      const checkTarget=()=>requireVirtualTargetAllowed(next.padPersona==='follow'?gyroDefaults?.padPersona:next.padPersona,policy(),
        field==='padPersona'||field==='gyroPreset'||field==='gyroEnabled'&&value==='on'||field==='gyroOverride'&&value!=='off'||current?.enabled!==true);
      checkTarget();
      if(field==='padPersona'&&value==='steamdeck'){
        await requirePower();
        requireDocument();
        // Same original soft-failure operation as the panel, never a new kill API.
        await deps.closeJoyxoff().catch(()=>false);checkpoint();
      }
      await requirePower();
      requireDocument();
      if(gyroDefaults){const finalDefaults=await deps.gyroDefaults!();checkpoint();requireDocument();if(finalDefaults.source!==admission.gyro)throw new Error('GAME_MIRROR_SOURCE_CHANGED');await requirePower();requireDocument();}
      checkTarget();
      await deps.save({...config,entries:{...config.entries,[key]:next}});
      if(deps.finishInput)return await deps.finishInput(true,context);
      // Once the original atomic save is in flight it cannot be cancelled by
      // a later menu close. Keep the frozen EXE and report commit truthfully.
      // Config notifications and the existing resident retry perform application;
      // no extra CPU/TDP/input calls, poll, timer or parallel hardware owner.
      return {saved:true,applied:false,pending:true,notice:`已记忆到 ${key}；由 YMCC 原游戏联动应用，当前未宣称硬件已确认`};
    }finally{release();}
  }};
}
