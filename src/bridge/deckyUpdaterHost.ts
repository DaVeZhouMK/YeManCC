import {watch} from 'vue';
import {APP_VERSION} from '@/version';
import {invoke} from './ipc';
import {powerLifecycle} from './api';
import {getSharedPowerLifecycle,isSharedPowerLifecycleAdmitted,onSharedPowerLifecycle,requestSharedPowerLifecycleReconcile} from './powerLifecycleState';
import {refreshGameStatusStrict} from './gamedetect';
import {isPolicyGameInitialized,getPolicyGame,subscribePolicyGameStatus} from './gamePolicyTarget';
import {ensureUpdateManager,updateSnapshot,updateInfo,checkForUpdate,prepareUpdate,downloadAndInstall} from './updateManager';
import {registerDeckyMirrorExtension} from './deckyMirrorExtensions';
import {createDeckyUpdaterService} from './deckyUpdaterService';
// Adapter to the existing, verified full-package updater. No new package origin or installer.
export function startDeckyUpdaterHost(allowed:()=>boolean){
  const sharedPowerReady=()=>isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle());
  let disposed=false;
  const refreshPower=()=>{if(disposed)return;requestSharedPowerLifecycleReconcile('decky-updater');service.changed();};
  const service=createDeckyUpdaterService({version:APP_VERSION,read:()=>({state:updateSnapshot,target:updateInfo.value}),ready:()=>!disposed&&allowed()&&isPolicyGameInitialized(),gameRunning:()=>getPolicyGame()!==null,powerReady:sharedPowerReady,
    check:()=>checkForUpdate(APP_VERSION),download:()=>prepareUpdate(),install:()=>downloadAndInstall(),
    validateInstall:async()=>{
      const initial=getSharedPowerLifecycle();let current=isSharedPowerLifecycleAdmitted(initial);
      // Retire this explicit request on any intervening denial, even if the
      // same generation returns Ready before an awaited read completes.
      const stop=onSharedPowerLifecycle(state=>{if(!isSharedPowerLifecycleAdmitted(state)||state.generation!==initial?.generation||state.observedNotificationSequence!==initial?.observedNotificationSequence)current=false;},{observeNotificationSequence:true});
      const assertPower=()=>{const state=getSharedPowerLifecycle();if(!current||!isSharedPowerLifecycleAdmitted(state)||state?.generation!==initial?.generation||state?.observedNotificationSequence!==initial?.observedNotificationSequence)throw Error('正在恢复电源状态，请恢复完成后重新确认安装。');};
      try{
        assertPower();const game=await refreshGameStatusStrict();assertPower();
        const power=await powerLifecycle.get(3000);assertPower();
        if(game||getPolicyGame())throw Error('游戏运行中，请退出游戏后安装。');
        if(!isSharedPowerLifecycleAdmitted(power)||power.generation!==initial?.generation||power.observedNotificationSequence!==initial?.observedNotificationSequence)throw Error('正在恢复电源状态，请恢复完成后重新确认安装。');
        if(disposed||!allowed()||!isPolicyGameInitialized())throw Error('YMCC 状态已改变，请重新确认安装。');
        // The service rechecks this captured scope after its final await.
        return assertPower;
      }finally{stop();}
    },
    observe:changed=>{
      const stops:Array<()=>void>=[];
      const stop=()=>{for(const release of stops){try{release();}catch{/* Release every independently registered observation. */}}};
      try{
        stops.push(watch(()=>[JSON.stringify(updateSnapshot),updateInfo.value?.version,updateInfo.value?.sha256],changed));
        stops.push(subscribePolicyGameStatus(changed));
        stops.push(onSharedPowerLifecycle(snapshot=>{if(disposed)return;changed();}));
        void ensureUpdateManager().then(changed).catch(changed);requestSharedPowerLifecycleReconcile('decky-updater-observe');return stop;
      }catch(error){stop();throw error;}
    },
    reply:(connection,message)=>invoke<boolean>('deckySidebar.mirrorReply',{connection,message},{timeoutMs:3000})});
  const unregister=registerDeckyMirrorExtension({prefix:'updater.',connected:service.connected,request:service.request});
  return {ready(){refreshPower();service.changed();},stop(){if(disposed)return;disposed=true;unregister();service.dispose();}};
}
