import { powerLifecycle } from './api';
import { beginSharedPowerLifecycleReadback, isSharedPowerLifecycleAdmitted, onSharedPowerLifecycle, type SharedPowerLifecycleSnapshot } from './powerLifecycleState';
import { isGameStatusCurrent, isCurrentGameTarget, type DetectedGame } from './gamedetect';
import { powerSourceMode } from './powerSource';
import { readSettingsSection,saveSettingsSection,updateSettings,rememberOrdinaryPerformancePreferences,getSettingsGeneration,assertSettingsGeneration, type UnifiedSettings } from './settingsRepository';
import { detectPerformanceTemplate, detectPowerModeReliable,readRtssLimit,setRtssLimit } from './yeman';
import { getPolicyGame,gamePolicyKey,isPolicyGameInitialized } from './gamePolicyTarget';
import { FRAME_RATE_DEFAULTS,frameRatePair,frameRateSetting,dedicatedFrameRatePair,type FrameRatePair,type FrameRateSide } from './frameRateModel';
import { loadGameCustomConfig } from './performanceSchedule';
import { getFloatInfo,setFloatTarget,setFloatRtssLinked,isContinuousFloat } from './autofloat';
export * from './frameRateModel';
const listeners=new Set<(pair:FrameRatePair)=>void>();
let writeQueue:Promise<void>=Promise.resolve();
let fallbackCache:FrameRatePair|null=null;
let fallbackGeneration=-1;
const frameTargetCurrent=(target:DetectedGame|null)=>isGameStatusCurrent() && (target ? isCurrentGameTarget(target) : getPolicyGame()===null);
export function onFrameRatesChanged(listener:(pair:FrameRatePair)=>void):()=>void {listeners.add(listener);return ()=>listeners.delete(listener);}
// A display transaction may supply its own current RTSS read. Ordinary callers
// keep the original fresh reader; no resolved section is retained here.
export async function loadGlobalFrameRates(readRtss:()=>Promise<UnifiedSettings['rtss']>=()=>readSettingsSection('rtss'),isCurrent:()=>boolean=()=>true,readTemplate=detectPerformanceTemplate):Promise<FrameRatePair> {
  const current=await readRtss();
  if(current.frameRates && current.frameRates.ac && current.frameRates.dc){
    if(current.templateDefaults===true && typeof readTemplate==='function')return frameRatePair(null,(await readTemplate())==='desktop'?200:120,60);
    return frameRatePair(current.frameRates);
  }
  const generation=getSettingsGeneration();
  if(fallbackGeneration===generation && fallbackCache)return frameRatePair(current.frameRates,fallbackCache.ac,fallbackCache.dc);
  const [tdp,schedule]=await Promise.all([readSettingsSection<any>('tdp'),readSettingsSection<any>('performanceSchedule')]);
  const legacy=(side:FrameRateSide)=>{
    const profile=schedule.profiles?.[side]?.[schedule.active?.[side]];
    if((schedule.enabled===true || (schedule.configured===true && schedule.enabled===undefined)) && typeof profile?.fpsTarget==='number')return profile.fpsTarget;
    return typeof tdp.fpsLimit==='number'?tdp.fpsLimit:FRAME_RATE_DEFAULTS[side];
  };
  const fallback=frameRatePair(current.frameRates,legacy('ac'),legacy('dc'));
  if(isCurrent() && getSettingsGeneration()===generation){fallbackCache=fallback;fallbackGeneration=generation;}
  return frameRatePair(fallback); // migration is read-only until a real save/apply boundary
}
export async function ensureGlobalFrameRates():Promise<FrameRatePair> {
  const generation=getSettingsGeneration(), target=getPolicyGame();
  const run=writeQueue.then(async()=>{
    assertSettingsGeneration(generation);
    const current=await readSettingsSection<any>('rtss');
    const pair=await loadGlobalFrameRates();
    if((!current.frameRates?.ac || !current.frameRates?.dc) && frameTargetCurrent(target))await saveSettingsSection('rtss',{frameRates:pair,templateDefaults:false},generation,{origin:'runtime'});
    return pair;
  });writeQueue=run.then(()=>{},()=>{});return run;
}
export async function saveGlobalFrameRate(side:FrameRateSide,value:unknown):Promise<FrameRatePair> {
  const generation=getSettingsGeneration(), target=getPolicyGame();
  const verify=()=>{assertSettingsGeneration(generation);if(!frameTargetCurrent(target))throw new Error("当前游戏状态未确认，请重新调整");};
  verify();
  // Capture submitted fields before waiting for other AC/DC writes.
  const requested=value && typeof value==='object'?{...value}:value;
  const run=writeQueue.then(async()=>{
    verify();
    const reference=await loadGlobalFrameRates();let pair!:FrameRatePair;
    verify();
    await updateSettings(settings=>{
      verify();
      const rates=settings.rtss.frameRates;
      const before=settings.rtss.templateDefaults!==true && rates?.ac && rates?.dc ? frameRatePair(rates) : frameRatePair(reference);
      pair=frameRatePair(before);pair[side]=frameRateSetting(requested,before[side]);
      settings.rtss={...settings.rtss,frameRates:pair as any,templateDefaults:false};
      if(settings.extensions?.bootModes!==undefined)rememberOrdinaryPerformancePreferences(settings,{frames:before},{frames:pair});
    });fallbackCache=frameRatePair(pair);
    for(const listener of [...listeners]){try{listener(frameRatePair(pair));}catch{/* observer failure cannot turn durable save into failure */}}return pair;
  });writeQueue=run.then(()=>{},()=>{});return run;
}
export async function effectiveFrameRates(isCurrent:()=>boolean=()=>true,access:'ensure'|'read-only'='ensure'):Promise<{pair:FrameRatePair;identity:string;dedicated:boolean;target:DetectedGame|null}> {
  const pair=await loadGlobalFrameRates(undefined,isCurrent);
  if(!isCurrent())return {pair,identity:'',dedicated:false,target:null};
  const game=getPolicyGame();
  if(!game)return {pair,identity:'',dedicated:false,target:null};
  const custom=await (access==='read-only'
    ? loadGameCustomConfig('read-only',{requireReady:true,displayCurrent:isCurrent})
    : loadGameCustomConfig());
  const key=gamePolicyKey(game),entry=custom.entries[key];
  const dedicated=!!entry && entry.enabled!==false && custom.rtss[key]?.enabled!==false;
  return {pair:dedicated?dedicatedFrameRatePair(custom.rtss[key],pair):pair,identity:`${game.pid}:${game.processCreated}`,dedicated,target:game};
}
let applyQueue:Promise<void>=Promise.resolve();
// Disk Limit may already match after a failed live reload. Retain a retry so
// an equal disk read cannot acknowledge a limit that RTSS never received.
let rtssReloadPending=false;
export function applyIndependentFrameRates(isCurrent:()=>boolean=()=>true):Promise<boolean> {
  const targetAtStart=getPolicyGame();
  let powerAtStart:SharedPowerLifecycleSnapshot|null=null, revoked=false;
  let retireRead!:()=>void;
  const retiredRead=new Promise<undefined>(resolve=>{retireRead=()=>resolve(undefined);});
  const readCurrent=<T>(work:Promise<T>)=>Promise.race([work,retiredRead]);
  const samePower=(a:SharedPowerLifecycleSnapshot,b:SharedPowerLifecycleSnapshot)=>
    a.generation===b.generation && a.observedNotificationSequence===b.observedNotificationSequence && a.suspendSerial===b.suspendSerial;
  // Capture before joining the write queue. A later Ready cannot renew an old request.
  const stopPower=onSharedPowerLifecycle(snapshot=>{
    if(!isSharedPowerLifecycleAdmitted(snapshot) || powerAtStart && !samePower(powerAtStart,snapshot)){
      revoked=true;
      retireRead();
    }else powerAtStart??=snapshot;
  },{observeNotificationSequence:true,observeSuspendSerial:true});
  const admitted=()=>!revoked && isCurrent() && frameTargetCurrent(targetAtStart);
  const run=applyQueue.then(async()=>{
    if(!isPolicyGameInitialized() || !admitted())return false;
    const acceptReadback=beginSharedPowerLifecycleReadback();
    const lifecycle=await readCurrent(powerLifecycle.get().catch(()=>null));
    if(!lifecycle || revoked)return false;
    // Native may observe the next scope before its notification reaches the UI.
    acceptReadback(lifecycle);
    if(!isSharedPowerLifecycleAdmitted(lifecycle) || !admitted() || powerAtStart && !samePower(powerAtStart,lifecycle))return false;
    powerAtStart=lifecycle;
    const side=await readCurrent(detectPowerModeReliable());
    if(!side || !admitted())return false;
    const state=await readCurrent(effectiveFrameRates(admitted));
    if(!state)return false;
    const targetCurrent=()=>admitted() && (state.target ? isCurrentGameTarget(state.target) : getPolicyGame()===null);
    if(!targetCurrent() || (await readCurrent(detectPowerModeReliable()))!==side || !targetCurrent())return false;
    setFloatRtssLinked(false);
    const value=state.pair[side];
    const current=()=>targetCurrent() && (powerSourceMode.value===null || powerSourceMode.value===side);
    if(!current())return false;
    // A stale read may still return later. Its result must not block the next
    // owner; writes below still settle in the original serial queue.
    const actual=await readCurrent(readRtssLimit(current));
    if(actual===undefined || !current())return false;
    if(actual!==value.fps || rtssReloadPending){
      rtssReloadPending=true;
      await setRtssLimit(value.fps,current,retiredRead);
      if(!current())return false;
      rtssReloadPending=false;
    }
    if(!current() || (await readCurrent(detectPowerModeReliable()))!==side)return false;
    if(!current())return false;
    const optimizer=getFloatInfo();
    const optimizerTarget = isContinuousFloat() ? value.fps : value.fps || value.lastFps;
    if(optimizer.enabled && optimizer.target!==optimizerTarget)await setFloatTarget(optimizerTarget);
    return current();
  }).finally(stopPower);applyQueue=run.then(()=>{},()=>{});return run;
}

export function publishFrameRatesChanged(pair: FrameRatePair): void {
  for (const listener of [...listeners]) { try { listener(frameRatePair(pair)); } catch { /* observer isolation */ } }
}
