import {watch} from 'vue';
import {APP_VERSION} from '@/version';
import {invoke} from './ipc';
import {powerLifecycle} from './api';
import {beginSharedPowerLifecycleReadback,getSharedPowerLifecycle,isSharedPowerLifecycleAdmitted,onSharedPowerLifecycle,requestSharedPowerLifecycleReconcile} from './powerLifecycleState';
import {refreshGameStatusStrict} from './gamedetect';
import {isPolicyGameInitialized,getPolicyGame,subscribePolicyGameStatus} from './gamePolicyTarget';
import {ensureUpdateManager,updateSnapshot,updateInfo,checkForUpdate,prepareUpdate,downloadAndInstall} from './updateManager';
import {registerDeckyMirrorExtension} from './deckyMirrorExtensions';
import {createDeckyUpdaterService} from './deckyUpdaterService';
// Adapter to the existing, verified full-package updater. No new package origin or installer.
export function startDeckyUpdaterHost(allowed:()=>boolean){
  const sharedPowerReady=()=>isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle());
  let disposed=false;
  let installPreflight:AbortController|null=null;
  const refreshPower=()=>{if(disposed)return;requestSharedPowerLifecycleReconcile('decky-updater');service.changed();};
  const service=createDeckyUpdaterService({version:APP_VERSION,read:()=>({state:updateSnapshot,target:updateInfo.value}),ready:()=>!disposed&&allowed()&&isPolicyGameInitialized(),gameRunning:()=>getPolicyGame()!==null,powerReady:sharedPowerReady,
    check:()=>checkForUpdate(APP_VERSION),download:()=>prepareUpdate(),install:()=>downloadAndInstall(),
    validateInstall:async()=>{
      const initial=getSharedPowerLifecycle(),preflight=new AbortController();
      installPreflight=preflight;
      const sameScope=(state:ReturnType<typeof getSharedPowerLifecycle>)=>isSharedPowerLifecycleAdmitted(state)&&
        state?.generation===initial?.generation&&state?.observedNotificationSequence===initial?.observedNotificationSequence&&state?.suspendSerial===initial?.suspendSerial;
      // Retire this explicit request on any intervening denial, even if the
      // same generation returns Ready before an awaited read completes.
      const stop=onSharedPowerLifecycle(state=>{if(!sameScope(state))preflight.abort();},{observeNotificationSequence:true,observeSuspendSerial:true});
      const changedBeforeInstall=()=>Error('正在恢复电源状态，请恢复完成后重新确认安装。');
      const assertPower=()=>{if(disposed||preflight.signal.aborted||!sameScope(getSharedPowerLifecycle()))throw changedBeforeInstall();};
      let onRetired:(()=>void)|undefined;
      try{
        assertPower();
        const retired=new Promise<never>((_,reject)=>{
          onRetired=()=>reject(changedBeforeInstall());
          preflight.signal.addEventListener('abort',onRetired,{once:true});
        });
        // The game observation may also have other consumers. Retire only
        // this wait; the dedicated power IPC can release its waiter as well.
        const read=async()=>{
          const game=await refreshGameStatusStrict();assertPower();
          const acceptPower=beginSharedPowerLifecycleReadback();
          const power=await powerLifecycle.get(3000,preflight.signal);assertPower();
          // A newer Native fact can retire this confirmation before its event arrives.
          acceptPower(power);assertPower();
          if(game||getPolicyGame())throw Error('游戏运行中，请退出游戏后安装。');
          if(!sameScope(power))throw changedBeforeInstall();
          if(!allowed()||!isPolicyGameInitialized())throw Error('YMCC 状态已改变，请重新确认安装。');
          // The service rechecks this captured scope after its final await.
          return assertPower;
        };
        return await Promise.race([retired,read()]);
      }finally{
        if(onRetired)preflight.signal.removeEventListener('abort',onRetired);
        if(installPreflight===preflight)installPreflight=null;
        stop();
      }
    },
    observe:changed=>{
      const stops:Array<()=>void>=[];
      let observing=false;
      let attempt:{scope:ReturnType<typeof getSharedPowerLifecycle>}|null=null;
      const sameScope=(a:ReturnType<typeof getSharedPowerLifecycle>,b:ReturnType<typeof getSharedPowerLifecycle>)=>
        a?.generation===b?.generation&&a?.suspendSerial===b?.suspendSerial&&a?.observedNotificationSequence===b?.observedNotificationSequence;
      const readStatus=(initial=false)=>{
        if(disposed||!observing)return;
        const scope=getSharedPowerLifecycle();
        if(!isSharedPowerLifecycleAdmitted(scope)&&!(initial&&scope===null)){attempt=null;return;}
        if(attempt&&sameScope(attempt.scope,scope))return;
        const owner=attempt={scope};
        const current=()=>observing&&!disposed&&attempt===owner;
        const read=()=>current()?ensureUpdateManager():undefined;
        const publish=()=>{if(current())changed();};
        // Let the manager retire its old read before trying its shared slot again.
        // Keep the original synchronous setup failure and rollback contract.
        const pending=initial?read():Promise.resolve().then(read);
        void pending?.then(publish,publish);
      };
      const stop=()=>{observing=false;attempt=null;for(const release of stops){try{release();}catch{/* Release every independently registered observation. */}}};
      try{
        stops.push(watch(()=>[JSON.stringify(updateSnapshot),updateInfo.value?.version,updateInfo.value?.sha256],changed));
        stops.push(subscribePolicyGameStatus(changed));
        stops.push(onSharedPowerLifecycle(()=>{if(disposed||!observing)return;readStatus();changed();},{observeNotificationSequence:true,observeSuspendSerial:true}));
        observing=true;
        readStatus(true);requestSharedPowerLifecycleReconcile('decky-updater-observe');return stop;
      }catch(error){stop();throw error;}
    },
    reply:(connection,message)=>invoke<boolean>('deckySidebar.mirrorReply',{connection,message},{timeoutMs:3000})});
  const unregister=registerDeckyMirrorExtension({prefix:'updater.',connected:service.connected,request:service.request});
  return {ready(){refreshPower();service.changed();},stop(){if(disposed)return;disposed=true;installPreflight?.abort();unregister();service.dispose();}};
}
