import { powerLifecycle } from './api';
import { powerSourceMode } from './powerSource';
import { readSettingsSection,saveSettingsSection,getSettingsGeneration,assertSettingsGeneration } from './settingsRepository';
import { detectPowerModeReliable,readRtssLimit,setRtssLimit } from './yeman';
import { getPolicyGame,gamePolicyKey,isPolicyGameInitialized } from './gamePolicyTarget';
import { FRAME_RATE_DEFAULTS,frameRatePair,frameRateSetting,dedicatedFrameRatePair,type FrameRatePair,type FrameRateSide } from './frameRateModel';
import { loadGameCustomConfig } from './performanceSchedule';
import { getFloatInfo,setFloatTarget,setFloatRtssLinked } from './autofloat';
export * from './frameRateModel';
const listeners=new Set<(pair:FrameRatePair)=>void>();
let writeQueue:Promise<void>=Promise.resolve();
let fallbackCache:FrameRatePair|null=null;
let fallbackGeneration=-1;
export function onFrameRatesChanged(listener:(pair:FrameRatePair)=>void):()=>void {listeners.add(listener);return ()=>listeners.delete(listener);}
export async function loadGlobalFrameRates():Promise<FrameRatePair> {
  const current=await readSettingsSection<any>('rtss');
  if(current.frameRates && current.frameRates.ac && current.frameRates.dc)return frameRatePair(current.frameRates);
  if(fallbackGeneration!==getSettingsGeneration()){fallbackCache=null;fallbackGeneration=getSettingsGeneration();}
  if(fallbackCache)return frameRatePair(current.frameRates,fallbackCache.ac,fallbackCache.dc);
  const [tdp,schedule]=await Promise.all([readSettingsSection<any>('tdp'),readSettingsSection<any>('performanceSchedule')]);
  const legacy=(side:FrameRateSide)=>{
    const profile=schedule.profiles?.[side]?.[schedule.active?.[side]];
    if((schedule.enabled===true || (schedule.configured===true && schedule.enabled===undefined)) && typeof profile?.fpsTarget==='number')return profile.fpsTarget;
    return typeof tdp.fpsLimit==='number'?tdp.fpsLimit:FRAME_RATE_DEFAULTS[side];
  };
  fallbackCache=frameRatePair(current.frameRates,legacy('ac'),legacy('dc'));
  return frameRatePair(fallbackCache); // migration is read-only until a real save/apply boundary
}
export async function ensureGlobalFrameRates():Promise<FrameRatePair> {
  const generation=getSettingsGeneration();
  const run=writeQueue.then(async()=>{
    assertSettingsGeneration(generation);
    const current=await readSettingsSection<any>('rtss');
    const pair=await loadGlobalFrameRates();
    if(!current.frameRates?.ac || !current.frameRates?.dc)await saveSettingsSection('rtss',{frameRates:pair},generation);
    return pair;
  });writeQueue=run.then(()=>{},()=>{});return run;
}
export async function saveGlobalFrameRate(side:FrameRateSide,value:unknown):Promise<FrameRatePair> {
  const generation=getSettingsGeneration();
  // Capture submitted fields before waiting for other AC/DC writes.
  const requested=value && typeof value==='object'?{...value}:value;
  const run=writeQueue.then(async()=>{
    assertSettingsGeneration(generation);
    const pair=await loadGlobalFrameRates();pair[side]=frameRateSetting(requested,pair[side]);
    await saveSettingsSection('rtss',{frameRates:pair},generation);fallbackCache=frameRatePair(pair);
    for(const listener of [...listeners]){try{listener(frameRatePair(pair));}catch{/* observer failure cannot turn durable save into failure */}}return pair;
  });writeQueue=run.then(()=>{},()=>{});return run;
}
export async function effectiveFrameRates():Promise<{pair:FrameRatePair;identity:string;dedicated:boolean}> {
  const pair=await loadGlobalFrameRates();
  const game=getPolicyGame();
  if(!game)return {pair,identity:'',dedicated:false};
  const custom=await loadGameCustomConfig();const key=gamePolicyKey(game),entry=custom.entries[key];
  const dedicated=!!entry && entry.enabled!==false && custom.rtss[key]?.enabled!==false;
  return {pair:dedicated?dedicatedFrameRatePair(custom.rtss[key],pair):pair,identity:`${game.pid}:${game.processCreated}`,dedicated};
}
let applyQueue:Promise<void>=Promise.resolve();
// Disk Limit may already match after a failed live reload. Retain a retry so
// an equal disk read cannot acknowledge a limit that RTSS never received.
let rtssReloadPending=false;
export function applyIndependentFrameRates(isCurrent:()=>boolean=()=>true):Promise<boolean> {
  const run=applyQueue.then(async()=>{
    if(!isCurrent() || !isPolicyGameInitialized())return false;
    const lifecycle=await powerLifecycle.get().catch(()=>null);
    if(lifecycle?.phase!=='ready' || lifecycle.hardwareWritesAllowed!==true)return false;
    const side=await detectPowerModeReliable();
    if(!side)return false; // Unknown power is not permission to apply the AC cap.
    const state=await effectiveFrameRates();
    const identity=()=>{const game=getPolicyGame();return game?`${game.pid}:${game.processCreated}`:'';};
    if(!isCurrent() || identity()!==state.identity || (await detectPowerModeReliable())!==side)return false;
    setFloatRtssLinked(false);
    const value=state.pair[side];
    const current=()=>isCurrent() && identity()===state.identity && (powerSourceMode.value===null || powerSourceMode.value===side);
    if(!current())return false;
    const actual=await readRtssLimit();
    if(!current())return false;
    if(actual!==value.fps || rtssReloadPending){
      rtssReloadPending=true;
      await setRtssLimit(value.fps,current);
      if(!current())return false;
      rtssReloadPending=false;
    }
    if(!current() || (await detectPowerModeReliable())!==side)return false;
    const optimizer=getFloatInfo();
    if(optimizer.enabled && optimizer.target!==(value.fps || value.lastFps))await setFloatTarget(value.fps || value.lastFps);
    return true;
  });applyQueue=run.then(()=>{},()=>{});return run;
}
