// Independent AC/DC frame limits: original frame model, original EXE store and
// original RTSS owner. No performance-combination FPS and no second limiter.
import { frameRatePair, frameRateSetting, dedicatedFrameRateRecord, FRAME_RATE_CEILINGS, type FrameRatePair, type FrameRateSetting } from './frameRateModel';
import { gameMirrorIdentity, type GameMirrorGyroDefaults } from './deckyGameActions';
import type { DetectedGame } from './gamedetect';
import type { PowerLifecycleState } from './api';
import type { GameCustomConfig, PerformanceScheduleConfig } from './performanceSchedule';
import type { RelayMutationContext } from './deckyMirrorRelay';
export interface FrameMirrorRead { pair:FrameRatePair; global:FrameRatePair; config:GameCustomConfig; schedule:PerformanceScheduleConfig|null; game:DetectedGame|null; dedicated:boolean; source:string; }
export function editMirrorFrameSetting(current:FrameRateSetting,kind:'fps'|'ceiling',value:string):FrameRateSetting {
  const numeric=Number(value);
  if(!Number.isSafeInteger(numeric)||String(numeric)!==value)throw Error('FRAME_MIRROR_INVALID_VALUE');
  if(kind==='fps'){
    if(!current.fps || numeric<20 || numeric>current.ceiling)throw Error('FRAME_MIRROR_INVALID_VALUE');
    return frameRateSetting({...current,fps:numeric,lastFps:numeric});
  }
  if(!FRAME_RATE_CEILINGS.includes(numeric))throw Error('FRAME_MIRROR_INVALID_VALUE');
  if(numeric===0)return frameRateSetting({fps:0,ceiling:0,lastFps:current.fps||current.lastFps});
  const fps=Math.min(numeric,current.fps||current.lastFps||90);
  return frameRateSetting({...current,fps,lastFps:fps,ceiling:numeric});
}
export function createFrameMirrorActions(deps:{
  allowed:()=>boolean;game:()=>DetectedGame|null;side:()=> 'ac'|'dc'|null;power:()=>Promise<PowerLifecycleState>;
  acquire:(owner:string)=>(()=>void)|null;read:()=>Promise<FrameMirrorRead>;key:(game:DetectedGame)=>string;
  label:(game:DetectedGame)=>string;gyro:()=>Promise<GameMirrorGyroDefaults>;
  save:(config:GameCustomConfig)=>Promise<void>;saveGlobal:(side:'ac'|'dc',value:FrameRateSetting)=>Promise<unknown>;
  apply:(current:()=>boolean)=>Promise<boolean>;
}) { return { async execute(args:Record<string,unknown>,context:RelayMutationContext) {
  if(Object.keys(args).length!==2 || typeof args.field!=='string' || !/^(ac|dc)(Fps|Ceiling)$/.test(args.field) || typeof args.value!=='string')throw Error('FRAME_MIRROR_INVALID_REQUEST');
  const side=args.field.startsWith('ac')?'ac':'dc',kind=args.field.endsWith('Fps')?'fps':'ceiling';
  const identity=context.gameIdentity??'',source=context.controlAdmission?.frames;
  const checkpoint=()=>{context.checkpoint();if(!deps.allowed()||deps.side()!==side||gameMirrorIdentity(deps.game())!==identity)throw Error('FRAME_MIRROR_TARGET_CHANGED');};
  checkpoint();if(typeof source!=='string')throw Error('FRAME_MIRROR_SOURCE_REQUIRED');
  const release=deps.acquire('decky-independent-frame-rate');if(!release)throw Error('FRAME_MIRROR_BUSY');
  const requirePower=async()=>{checkpoint();const power=await deps.power();checkpoint();if(power.generation!==context.generation||power.phase!=='ready'||!power.hardwareWritesAllowed)throw Error('FRAME_MIRROR_POWER_NOT_READY');};
  try {
    await requirePower();const read=await deps.read();checkpoint();
    if(read.source!==source)throw Error('FRAME_MIRROR_SOURCE_CHANGED');
    const pair=frameRatePair(read.pair),next=editMirrorFrameSetting(pair[side],kind,args.value);
    const key=read.game?deps.key(read.game):'',entry=key?read.config.entries[key]:undefined;
    // Recognizing a game is not consent to create its dedicated profile.
    const dedicated=!!read.game && !!entry && entry.enabled!==false;
    const needsMemory=dedicated && (read.config.rtss[key]?.enabled===false || !read.config.rtss[key]);
    if(!needsMemory && JSON.stringify(next)===JSON.stringify(pair[side]))return {saved:false,applied:false,notice:'独立帧率未改变'};
    pair[side]=next;
    let nextConfig:GameCustomConfig|undefined;
    if(read.game && dedicated){
      if(!Number.isSafeInteger(read.game.pid)||read.game.pid<=0||!read.game.processCreated)throw Error('FRAME_MIRROR_TARGET_CHANGED');
      if(!key || !read.schedule)throw Error('FRAME_MIRROR_GAME_SOURCE_REQUIRED');
      // An FPS intent edits RTSS only. Profile creation/import and remembered
      // controller/gyro/performance values belong to their own explicit actions.
      nextConfig={...read.config,rtss:{...read.config.rtss,[key]:{...read.config.rtss[key],...dedicatedFrameRateRecord(pair)}}};
    }
    // Re-read the original sources immediately before their original save boundary.
    const final=await deps.read();checkpoint();if(final.source!==source)throw Error('FRAME_MIRROR_SOURCE_CHANGED');
    await requirePower();
    if(nextConfig)await deps.save(nextConfig);else await deps.saveGlobal(side,next);
    const current=()=>deps.allowed() && deps.side()===side && gameMirrorIdentity(deps.game())===identity;
    let applied=false;
    // The durable setting survives a later lifecycle change, but this old
    // request may not dispatch into the resumed generation. YMCC's resident
    // owner decides application of saved settings in its own current context.
    try{await requirePower();if(current())applied=await deps.apply(current);}catch{/* saved, application pending */}
    return {saved:true,applied,pending:!applied,notice:applied?'已保存并应用独立帧率；未修改性能档位':'已保存独立帧率，当前应用尚未确认'};
  } finally {release();}
} }; }
