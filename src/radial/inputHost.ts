/** Reuse the original global CAS and per-game atomic configuration owners. */
import { invoke } from '@/bridge/ipc';
import { powerLifecycle } from '@/bridge/api';
import { compareAndSwapInputSettings, readSettingsSection, onInputSettingsChanged } from '@/bridge/settingsRepository';
import { getPolicyGame, gamePolicyKey, subscribePolicyGameStatus } from '@/bridge/gamePolicyTarget';
import { getGameInputOverrideState } from '@/bridge/gameInputOverride';
import { powerSourceMode } from '@/bridge/powerSource';
import { loadPerformanceSchedule, loadGameCustomConfig, getGameCustomConfig, saveGameCustomConfig, getContinuousPerformanceSnapshot, onGameCustomConfigChanged, onPerformanceScheduleChanged } from '@/bridge/performanceSchedule';
import { readGameMirrorGyroDefaults } from '@/bridge/deckyGyroDefaults';
import { globalMirrorRead } from '@/bridge/deckyGlobalActions';
import { gameMirrorIdentity } from '@/bridge/deckyGameActions';
import { currentVirtualTargetPolicy } from '@/bridge/virtualTargetPolicyState';
import { tryAcquireQuickAction, isQuickActionBusy } from '@/bridge/quickActionLock';
import { closeJoyxoffIfRunning } from '@/bridge/gameproc';
import { createRadialInputActions, type RadialInputRead } from './inputActions';
export function createLocalRadialInputActions(alive:()=>boolean){
 let schedule:Awaited<ReturnType<typeof loadPerformanceSchedule>>|null=null;
 const readGlobal=async(cached?:Awaited<ReturnType<typeof loadPerformanceSchedule>>)=>globalMirrorRead(cached??await loadPerformanceSchedule(),await readSettingsSection('input') as any,currentVirtualTargetPolicy());
 const actions=createRadialInputActions({
  read:async():Promise<RadialInputRead>=>{const game=getPolicyGame(),identity=gameMirrorIdentity(game),policy=currentVirtualTargetPolicy();schedule=await loadPerformanceSchedule();
   const common={policy,locked:getGameInputOverrideState().locked,busy:isQuickActionBusy()};
   if(!game){const global=await readGlobal(schedule);if(gameMirrorIdentity(getPolicyGame())!==identity)throw Error('当前游戏已变化');return {...common,scope:'global',global};}
   const [config,gyro]=await Promise.all([loadGameCustomConfig(),readGameMirrorGyroDefaults()]);
   if(gameMirrorIdentity(getPolicyGame())!==identity)throw Error('当前游戏已变化');
   return {...common,scope:'game',game:structuredClone(game),entry:config.entries[gamePolicyKey(game)],schedule,gyro,label:game.name||gamePolicyKey(game)};
  },
  global:{allowed:alive,game:getPolicyGame,locked:()=>getGameInputOverrideState().locked,targetPolicy:currentVirtualTargetPolicy,side:()=>powerSourceMode.value,read:readGlobal,power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction,cas:compareAndSwapInputSettings,closeJoyxoff:closeJoyxoffIfRunning,clearShortcut:()=>invoke('input.shortcutRuntime.clear',{})},
  game:{featureAllowed:alive,game:getPolicyGame,label:game=>game.name||gamePolicyKey(game),power:()=>powerLifecycle.get(),acquire:tryAcquireQuickAction,load:loadGameCustomConfig,current:getGameCustomConfig,schedule:loadPerformanceSchedule,currentSchedule:()=>schedule,save:saveGameCustomConfig,closeJoyxoff:closeJoyxoffIfRunning,targetPolicy:currentVirtualTargetPolicy,rememberWholeInput:true,continuous:async()=>(await getContinuousPerformanceSnapshot()).controls,gyroDefaults:readGameMirrorGyroDefaults},
  nextRevision:()=>crypto.randomUUID(),
 });
 return {...actions,observe(listener:()=>void){const stops=[onInputSettingsChanged(listener),onGameCustomConfigChanged(listener),onPerformanceScheduleChanged(listener),subscribePolicyGameStatus(listener)];return ()=>stops.forEach(stop=>stop());}};
}
