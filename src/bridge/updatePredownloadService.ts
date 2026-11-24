import { watch } from 'vue';
import { invoke, isNativeRuntime } from './ipc';
import { getSharedPowerLifecycle, isSharedPowerLifecycleAdmitted, onSharedPowerLifecycle, requestSharedPowerLifecycleReconcile } from './powerLifecycleState';
import { APP_VERSION } from '@/version';
import { ensureUpdateManager, refreshAdoptedUpdate, checkForUpdate, prepareUpdate, cancelBackgroundUpdate, updateInfo, updateSnapshot, probeBackgroundSuccessor, restoreBackgroundTarget, setBackgroundGamePaused, updateCacheState } from './updateManager';
import { validateUpdateManifest } from './yeman';
import { getPolicyGame, isPolicyGameCurrent, subscribePolicyGameReadiness, subscribePolicyGameStatus } from './gamePolicyTarget';
import { registerUpdatePredownloadProvider, updatePredownloadState } from './updatePredownload';
import { createUpdatePredownloadScheduler, type PredownloadPolicy } from './updatePredownloadScheduler';
import { presentUpdateReady } from './updateReadyNotice';
let active: { stop(): void } | null = null;
export function startUpdatePredownload(): { stop(): void } {
  if (active) return active;
  if (!isNativeRuntime) return { stop() {} };
  const initialPower=getSharedPowerLifecycle();
  let ended=false,suspending=!isSharedPowerLifecycleAdmitted(initialPower),admission=false,policy:PredownloadPolicy|null=null;
  let powerRevision=0;
  let probeTimer:ReturnType<typeof setTimeout>|null=null,probeRunning=false,resumeProbe=false;
  function capturePowerScope(){
    const ticket=powerRevision,suspendSerial=getSharedPowerLifecycle()?.suspendSerial;
    return ()=>{
      const power=getSharedPowerLifecycle();
      return !ended&&ticket===powerRevision&&power?.suspendSerial===suspendSerial&&isSharedPowerLifecycleAdmitted(power);
    };
  }
  const idleEvidence=()=>isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle())&&isPolicyGameCurrent()&&!getPolicyGame();
  const canRun=()=>!updateCacheState.busy&&admission&&idleEvidence();
  setBackgroundGamePaused(!canRun());
  const restoreHint=async()=>{if(updateCacheState.busy||!policy?.targetVersion||!policy?.targetSha256)return;try{await restoreBackgroundTarget(validateUpdateManifest({version:policy.targetVersion,sha256:policy.targetSha256}));}catch{/* Corrupt/obsolete hints never authorize a download. */}};
  const scheduler = createUpdatePredownloadScheduler({
    now: Date.now, timer: (fn, ms) => setTimeout(fn, ms), clearTimer: timer => clearTimeout(timer as ReturnType<typeof setTimeout>),
    online: () => navigator.onLine !== false, canRun,
    readPolicy: async () => { policy=await invoke<PredownloadPolicy>('app.updatePredownloadRead', {}, { timeoutMs: 5000 }); return policy; },
    setEnabled: enabled => invoke<PredownloadPolicy>('app.updatePredownloadSet', { enabled }, { timeoutMs: 5000 }),
    saveSchedule: (failureCount, nextCheckAt) => invoke('app.updatePredownloadSchedule', { failureCount, nextCheckAt }, { timeoutMs: 5000 }),
    initialize: async()=>{await ensureUpdateManager();await restoreHint();},
    refresh: async()=>{
      const current=capturePowerScope();
      try { await refreshAdoptedUpdate(current); }
      catch (error) { if (current()) throw error; }
      if(!current())return 'retired';
    },
    current: () => ({ info: updateInfo.value, progress: updateSnapshot }),
    check: async () => {
      const current=capturePowerScope();
      const checked=await checkForUpdate(APP_VERSION,true,current);
      return current()?checked:'retired';
    },
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
  let lastGameKnown:boolean|undefined;
  const syncActivity=async()=>{
    const ticket=++activityRevision,known=isPolicyGameCurrent(),allowed=idleEvidence();
    lastGameKnown=known;admission=false;setBackgroundGamePaused(!allowed);
    try{const receipt=await invoke<unknown>('app.updatePredownloadActivity',{known,idle:allowed},{timeoutMs:5000});
      if(receipt!==true)throw Error('后台更新活动状态未获确认');
      if(ended||ticket!==activityRevision)return;
      admission=allowed;setBackgroundGamePaused(!admission);
      if(allowed){
        const saved=await invoke<PredownloadPolicy>('app.updatePredownloadRead',{}, {timeoutMs:5000});
        if(ended||ticket!==activityRevision)return;
        policy=saved;await restoreHint();
        if(!ended&&ticket===activityRevision)scheduler.wake(true);
      }else scheduler.progress();
    }catch{if(!ended&&ticket===activityRevision){admission=false;setBackgroundGamePaused(true);scheduler.wake();}}
    finally{reconcileProbe();}
  };
  const recover = () => { if(!ended)void syncActivity(); };
  let projectedPowerGeneration=initialPower?.generation??null,projectedSuspendSerial=initialPower?.suspendSerial;
  let observedGame=false;
  const offGame=subscribePolicyGameStatus(()=>{observedGame=true;void syncActivity();});
  // The shared source immediately reports an already initialized target.
  // Do not send the same native activity transaction twice on startup.
  if(!observedGame)void syncActivity();
  // An unchanged empty target becomes idle again only after a new observation.
  const offGameReadiness=subscribePolicyGameReadiness(()=>{
    if(!getPolicyGame()&&lastGameKnown!==isPolicyGameCurrent())recover();
  });
  const offPower = onSharedPowerLifecycle(snapshot=>{
    if(ended)return;
    const next=!isSharedPowerLifecycleAdmitted(snapshot);
    const changed=next!==suspending||snapshot.generation!==projectedPowerGeneration||snapshot.suspendSerial!==projectedSuspendSerial;
    suspending=next;projectedPowerGeneration=snapshot.generation;
    projectedSuspendSerial=snapshot.suspendSerial;
    if(changed){++powerRevision;if(probeRunning)resumeProbe=true;recover();}
  },{observeSuspendSerial:true});
  requestSharedPowerLifecycleReconcile('update-predownload');
  window.addEventListener('online', recover);
  const offline=()=>{reconcileProbe();scheduler.wake();};
  window.addEventListener('offline', offline);
  const canProbeSuccessor=()=>canRun()&&updatePredownloadState.enabled&&navigator.onLine!==false&&
    ['downloading','validating'].includes(updateSnapshot.phase || '');
  function needsProbe(){
    return !ended&&!suspending&&!updateCacheState.busy&&(
      updateSnapshot.installerActive || (!admission&&idleEvidence()) ||
      canProbeSuccessor());
  }
  function reconcileProbe(){
    if(!needsProbe()){
      if(probeTimer!==null)clearTimeout(probeTimer);
      probeTimer=null;
    }else if(!probeRunning){
      const resume=resumeProbe&&(updateSnapshot.installerActive||canProbeSuccessor());
      if(resume){
        resumeProbe=false;
        if(probeTimer!==null)clearTimeout(probeTimer);
        probeTimer=setTimeout(probe,0);
      }else if(probeTimer===null)probeTimer=setTimeout(probe,60000);
    }
  }
  async function probe(){
    probeTimer=null;
    if(!needsProbe())return;
    probeRunning=true;
    const current=capturePowerScope();
    try{
      if(updateSnapshot.installerActive)await refreshAdoptedUpdate(current);
      if(!current())return;
      if(!admission&&idleEvidence())await syncActivity();
      if(current()&&canProbeSuccessor())await probeBackgroundSuccessor(current);
    }catch{/* Failed probe cannot cancel a healthy transfer. */}
    finally{probeRunning=false;reconcileProbe();}
  }
  reconcileProbe();
  scheduler.start();
  const owner = { stop() {
    if (active !== owner) return;
    ended=true;activityRevision++;if(probeTimer!==null)clearTimeout(probeTimer);probeTimer=null;
    setBackgroundGamePaused(true);void invoke('app.updatePredownloadActivity',{known:false,idle:false},{timeoutMs:5000}).catch(()=>{});
    active = null; stopProgress(); stopMaintenance(); stopProbeState(); offGame(); offGameReadiness(); offPower(); window.removeEventListener('online', recover); window.removeEventListener('offline', offline);
    scheduler.stop(); unregister();
  } };
  active = owner;
  return owner;
}

