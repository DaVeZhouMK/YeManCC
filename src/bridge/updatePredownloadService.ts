import { watch } from 'vue';
import { invoke, isNativeRuntime, on } from './ipc';
import { APP_VERSION } from '@/version';
import { ensureUpdateManager, refreshAdoptedUpdate, checkForUpdate, prepareUpdate, cancelBackgroundUpdate, updateInfo, updateSnapshot, probeBackgroundSuccessor, restoreBackgroundTarget, setBackgroundGamePaused, updateCacheState } from './updateManager';
import { validateUpdateManifest } from './yeman';
import { getPolicyGame, isPolicyGameInitialized, subscribePolicyGameStatus } from './gamePolicyTarget';
import { registerUpdatePredownloadProvider, updatePredownloadState } from './updatePredownload';
import { createUpdatePredownloadScheduler, type PredownloadPolicy } from './updatePredownloadScheduler';
import { presentUpdateReady } from './updateReadyNotice';
let active: { stop(): void } | null = null;
export function startUpdatePredownload(): { stop(): void } {
  if (active) return active;
  if (!isNativeRuntime) return { stop() {} };
  let ended=false,suspending=false,admission=false,policy:PredownloadPolicy|null=null;
  let probeTimer:ReturnType<typeof setTimeout>|null=null,probeRunning=false;
  const idleEvidence=()=>!suspending&&isPolicyGameInitialized()&&!getPolicyGame();
  const canRun=()=>!updateCacheState.busy&&admission&&idleEvidence();
  setBackgroundGamePaused(!canRun());
  const restoreHint=async()=>{if(updateCacheState.busy||!policy?.targetVersion||!policy?.targetSha256)return;try{await restoreBackgroundTarget(validateUpdateManifest({version:policy.targetVersion,sha256:policy.targetSha256}));}catch{/* Corrupt/obsolete hints never authorize a download. */}};
  const scheduler = createUpdatePredownloadScheduler({
    now: Date.now, timer: (fn, ms) => setTimeout(fn, ms), clearTimer: timer => clearTimeout(timer as ReturnType<typeof setTimeout>),
    online: () => navigator.onLine !== false, canRun,
    readPolicy: async () => { policy=await invoke<PredownloadPolicy>('app.updatePredownloadRead', {}, { timeoutMs: 5000 }); return policy; },
    setEnabled: enabled => invoke<PredownloadPolicy>('app.updatePredownloadSet', { enabled }, { timeoutMs: 5000 }),
    saveSchedule: (failureCount, nextCheckAt) => invoke('app.updatePredownloadSchedule', { failureCount, nextCheckAt }, { timeoutMs: 5000 }),
    initialize: async()=>{await ensureUpdateManager();await restoreHint();}, refresh: refreshAdoptedUpdate,
    current: () => ({ info: updateInfo.value, progress: updateSnapshot }),
    check: () => checkForUpdate(APP_VERSION, true),
    prepare: cacheOnly => prepareUpdate({ background: true, cacheOnly }),
    cancel: cancelBackgroundUpdate, notify: presentUpdateReady,
    notified: (policy, info) => invoke<boolean>('app.updatePredownloadNotified', { bootId: policy.bootId, version: info.version, sha256: info.sha256 }, { timeoutMs: 5000 }),
  });
  const unregister = registerUpdatePredownloadProvider(scheduler);
  const stopProgress = watch(updateSnapshot, () => scheduler.progress());
  const stopMaintenance = watch(() => updateCacheState.revision, () => {
    policy=null; scheduler.invalidateCache();
    if (!updateCacheState.busy) scheduler.wake();
  }, { flush: 'sync' });
  const stopProbeState = watch(
    () => [updateSnapshot.installerActive, updateSnapshot.phase, updateCacheState.busy, updatePredownloadState.enabled],
    reconcileProbe, { flush: 'sync' },
  );
  let activityRevision=0;
  const syncActivity=async()=>{
    const ticket=++activityRevision,allowed=idleEvidence();admission=false;setBackgroundGamePaused(!allowed);
    try{const receipt=await invoke<unknown>('app.updatePredownloadActivity',{known:isPolicyGameInitialized(),idle:allowed},{timeoutMs:5000});
      if(receipt!==true)throw Error('后台更新活动状态未获确认');
      if(!ended&&ticket===activityRevision){admission=allowed;setBackgroundGamePaused(!admission);if(allowed){policy=await invoke<PredownloadPolicy>('app.updatePredownloadRead',{}, {timeoutMs:5000});if(ended||ticket!==activityRevision)return;await restoreHint();scheduler.wake(true);}else scheduler.progress();}
    }catch{if(!ended&&ticket===activityRevision){admission=false;setBackgroundGamePaused(true);scheduler.wake();}}
    finally{reconcileProbe();}
  };
  const recover = () => { if(!ended)void syncActivity(); };
  let observedGame=false;
  const offGame=subscribePolicyGameStatus(()=>{observedGame=true;void syncActivity();});
  // The shared source immediately reports an already initialized target.
  // Do not send the same native activity transaction twice on startup.
  if(!observedGame)void syncActivity();
  const offResume = on('power.resumed', ()=>{suspending=false;recover();});
  const suspend = () => { suspending=true;void syncActivity(); };
  const offSuspend = on('power.suspending', suspend);
  window.addEventListener('online', recover);
  const offline=()=>{reconcileProbe();scheduler.wake();};
  window.addEventListener('offline', offline);
  function needsProbe(){
    return !ended&&!suspending&&!updateCacheState.busy&&(
      updateSnapshot.installerActive || (!admission&&idleEvidence()) ||
      (canRun()&&updatePredownloadState.enabled&&navigator.onLine!==false&&
        ['downloading','validating'].includes(updateSnapshot.phase || '')));
  }
  function reconcileProbe(){
    if(!needsProbe()){
      if(probeTimer!==null)clearTimeout(probeTimer);
      probeTimer=null;
    }else if(!probeRunning&&probeTimer===null)probeTimer=setTimeout(probe,60000);
  }
  async function probe(){
    probeTimer=null;
    if(!needsProbe())return;
    probeRunning=true;
    try{if(updateSnapshot.installerActive)await refreshAdoptedUpdate();if(!admission&&idleEvidence())await syncActivity();if(canRun()&&updatePredownloadState.enabled&&navigator.onLine!==false)await probeBackgroundSuccessor();}catch{/* Failed probe cannot cancel a healthy transfer. */}
    finally{probeRunning=false;reconcileProbe();}
  }
  reconcileProbe();
  scheduler.start();
  const owner = { stop() {
    if (active !== owner) return;
    ended=true;activityRevision++;if(probeTimer!==null)clearTimeout(probeTimer);probeTimer=null;
    setBackgroundGamePaused(true);void invoke('app.updatePredownloadActivity',{known:false,idle:false},{timeoutMs:5000}).catch(()=>{});
    active = null; stopProgress(); stopMaintenance(); stopProbeState(); offGame(); offResume(); offSuspend(); window.removeEventListener('online', recover); window.removeEventListener('offline', offline);
    scheduler.stop(); unregister();
  } };
  active = owner;
  return owner;
}

