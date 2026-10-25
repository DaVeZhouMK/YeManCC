import {watch} from 'vue';
import {APP_VERSION} from '@/version';
import {invoke,on} from './ipc';
import {powerLifecycle} from './api';
import {refreshGameStatusStrict} from './gamedetect';
import {isPolicyGameInitialized,getPolicyGame,subscribePolicyGameStatus} from './gamePolicyTarget';
import {ensureUpdateManager,updateSnapshot,updateInfo,checkForUpdate,prepareUpdate,downloadAndInstall} from './updateManager';
import {registerDeckyMirrorExtension} from './deckyMirrorExtensions';
import {createDeckyUpdaterService} from './deckyUpdaterService';
// Adapter to the existing, verified full-package updater. No new package origin or installer.
export function startDeckyUpdaterHost(allowed:()=>boolean){
  let disposed=false,powerReady=false,powerEpoch=0;
  const refreshPower=async()=>{const epoch=++powerEpoch;try{const state=await powerLifecycle.get();if(!disposed&&epoch===powerEpoch){powerReady=state.phase==='ready';service.changed();}}catch{if(!disposed&&epoch===powerEpoch){powerReady=false;service.changed();}}};
  const service=createDeckyUpdaterService({version:APP_VERSION,read:()=>({state:updateSnapshot,target:updateInfo.value}),ready:()=>!disposed&&allowed()&&isPolicyGameInitialized(),gameRunning:()=>getPolicyGame()!==null,powerReady:()=>powerReady,
    check:()=>checkForUpdate(APP_VERSION),download:()=>prepareUpdate(),install:()=>downloadAndInstall(),
    validateInstall:async()=>{const game=await refreshGameStatusStrict();const power=await powerLifecycle.get();powerReady=power.phase==='ready';if(game||getPolicyGame())throw Error('游戏运行中，请退出游戏后安装。');if(!powerReady)throw Error('正在恢复电源状态，请恢复完成后安装。');if(disposed||!allowed()||!isPolicyGameInitialized())throw Error('YMCC 状态已改变，请重新确认安装。');},
    observe:changed=>{const stopState=watch(()=>[JSON.stringify(updateSnapshot),updateInfo.value?.version,updateInfo.value?.sha256],changed),stopGame=subscribePolicyGameStatus(changed);const suspend=()=>{++powerEpoch;powerReady=false;changed();};const stopSuspend=on('power.suspending',suspend),stopResume=on('power.resumed',()=>{void refreshPower();});void ensureUpdateManager().then(changed).catch(changed);void refreshPower();return()=>{stopState();stopGame();stopSuspend();stopResume();};},
    reply:(connection,message)=>invoke<boolean>('deckySidebar.mirrorReply',{connection,message},{timeoutMs:3000})});
  const unregister=registerDeckyMirrorExtension({prefix:'updater.',connected:service.connected,request:service.request});
  return {ready(){void refreshPower();service.changed();},stop(){if(disposed)return;disposed=true;++powerEpoch;unregister();service.dispose();}};
}
