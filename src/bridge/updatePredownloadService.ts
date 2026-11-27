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
  let ended=false,suspending=!isSharedPowerLifecycleAdmitted(initialPower),admission=false;
  let powerRevision=0;
  let schedulerPolicyRead:AbortController|null=null;
  let probeTimer:ReturnType<typeof setTimeout>|null=null,probeRunning=false,resumeProbe=false;
  let adoptedRead: { controller: AbortController; current(): boolean; operationId: string | undefined;
    phase: string | undefined; installerActive: boolean | undefined; task: Promise<void | 'retired'> } | null = null;
  function capturePowerScope(){
    const ticket=powerRevision,captured=getSharedPowerLifecycle();
    return ()=>{
      const power=getSharedPowerLifecycle();
      return !ended&&ticket===powerRevision&&!!captured&&!!power&&isSharedPowerLifecycleAdmitted(captured)&&
        isSharedPowerLifecycleAdmitted(power)&&power.generation===captured.generation&&
        power.suspendSerial===captured.suspendSerial&&power.observedNotificationSequence===captured.observedNotificationSequence;
    };
  }
  const idleEvidence=()=>isSharedPowerLifecycleAdmitted(getSharedPowerLifecycle())&&isPolicyGameCurrent()&&!getPolicyGame();
  const canRun=()=>!updateCacheState.busy&&admission&&idleEvidence();
  const canProbeSuccessor=()=>canRun()&&updatePredownloadState.enabled&&navigator.onLine!==false&&
    ['downloading','validating'].includes(updateSnapshot.phase || '');
  function refreshAdopted(): Promise<void | 'retired'> {
    const { operationId, phase, installerActive } = updateSnapshot;
    if (adoptedRead?.current() && adoptedRead.operationId === operationId &&
      adoptedRead.phase === phase && adoptedRead.installerActive === installerActive) return adoptedRead.task;
    adoptedRead?.controller.abort();
    const controller = new AbortController(), scopeCurrent = capturePowerScope();
    const current = () => !controller.signal.aborted && scopeCurrent();
    const task = (async () => {
      try { await refreshAdoptedUpdate(current, controller.signal); }
      catch (error) { if (current()) throw error; }
      if (!current()) return 'retired' as const;
    })();
    adoptedRead = { controller, current, operationId, phase, installerActive, task };
    void task.finally(() => { if (adoptedRead?.task === task) adoptedRead = null; }).catch(() => {});
    return task;
  }
  setBackgroundGamePaused(!canRun());
  const restoreHint=async(saved:PredownloadPolicy,current:()=>boolean)=>{
    if(!saved?.targetVersion||!saved?.targetSha256||updateCacheState.busy)return;
    try{
      const hint=validateUpdateManifest({version:saved.targetVersion,sha256:saved.targetSha256});
      await ensureUpdateManager();
      if(current())await restoreBackgroundTarget(hint);
    }catch{/* Corrupt/obsolete hints never authorize a download. */}
  };
  const scheduler = createUpdatePredownloadScheduler({
    now: Date.now, timer: (fn, ms) => setTimeout(fn, ms), clearTimer: timer => clearTimeout(timer as ReturnType<typeof setTimeout>),
    online: () => navigator.onLine !== false, canRun,
    // Preferences are readable while held, but a completed read belongs to
    // the observation in which it started, including manager initialization.
    capturePolicyScope: () => {
      const revision=powerRevision;
      return () => !ended&&revision===powerRevision;
    },
    readPolicy: async () => {
      const reader=new AbortController();schedulerPolicyRead=reader;
      try{return await invoke<PredownloadPolicy>('app.updatePredownloadRead', {}, {timeoutMs:5000,signal:reader.signal});}
      finally{if(schedulerPolicyRead===reader)schedulerPolicyRead=null;}
    },
    setEnabled: enabled => invoke<PredownloadPolicy>('app.updatePredownloadSet', { enabled }, { timeoutMs: 5000 }),
    saveSchedule: (failureCount, nextCheckAt) => invoke('app.updatePredownloadSchedule', { failureCount, nextCheckAt }, { timeoutMs: 5000 }),
    initialize: ensureUpdateManager,
    refresh: refreshAdopted,
    current: () => ({ info: updateInfo.value, progress: updateSnapshot }),
    check: async () => {
      const current=capturePowerScope();
      const checked=await checkForUpdate(APP_VERSION,true,current);
      return current()?checked:'retired';
    },
    prepare: cacheOnly => {
      if (!cacheOnly) return prepareUpdate({ background: true });
      const current = capturePowerScope();
      if (!current()) return Promise.resolve('retired');
      // Keep the manager's serial owner until Native settles this cache check.
      return prepareUpdate({ background: true, cacheOnly }).then(prepared => current() ? prepared : 'retired');
    },
    cancel: cancelBackgroundUpdate, notify: presentUpdateReady,
    notified: (policy, info) => invoke<boolean>('app.updatePredownloadNotified', { bootId: policy.bootId, version: info.version, sha256: info.sha256 }, { timeoutMs: 5000 }),
  });
  const unregister = registerUpdatePredownloadProvider(scheduler);
  const stopProgress = watch(updateSnapshot, () => scheduler.progress());
  const stopMaintenance = watch(() => updateCacheState.revision, () => {
    scheduler.invalidateCache();
    if (!updateCacheState.busy) scheduler.wake();
  }, { flush: 'sync' });
  const stopProbeState = watch(
    () => [updateSnapshot.installerActive, updateSnapshot.phase, updateCacheState.busy, updatePredownloadState.enabled],
    reconcileProbe, { flush: 'sync' },
  );
  let activityRevision=0;
  let activityPending=false,activityPolicyRead:AbortController|null=null;
  let lastGameKnown:boolean|undefined;
  const syncActivity=async()=>{
    const ticket=++activityRevision,known=isPolicyGameCurrent(),allowed=idleEvidence();
    const scopeCurrent=capturePowerScope();
    const current=()=>!ended&&ticket===activityRevision&&(!allowed||scopeCurrent()&&idleEvidence());
    activityPolicyRead?.abort();activityPending=true;
    lastGameKnown=known;admission=false;setBackgroundGamePaused(!allowed);
    try{const receipt=await invoke<unknown>('app.updatePredownloadActivity',{known,idle:allowed},{timeoutMs:5000});
      if(receipt!==true)throw Error('后台更新活动状态未获确认');
      if(!current())return;
      admission=allowed;setBackgroundGamePaused(!admission);
      if(allowed){
        const reader=new AbortController();activityPolicyRead=reader;
        let saved:PredownloadPolicy;
        try{saved=await invoke<PredownloadPolicy>('app.updatePredownloadRead',{}, {timeoutMs:5000,signal:reader.signal});}
        finally{if(activityPolicyRead===reader)activityPolicyRead=null;}
        if(!current())return;
        await restoreHint(saved,current);
        if(current())scheduler.wake(true);
      }else scheduler.progress();
    }catch{if(current()){admission=false;setBackgroundGamePaused(true);scheduler.wake();}}
    finally{if(ticket===activityRevision)activityPending=false;reconcileProbe();}
  };
  const recover = () => { if(!ended)void syncActivity(); };
  let projectedPowerGeneration=initialPower?.generation??null,projectedSuspendSerial=initialPower?.suspendSerial;
  let projectedNotificationSequence=initialPower?.observedNotificationSequence;
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
    const observationChanged=snapshot.observedNotificationSequence!==projectedNotificationSequence;
    suspending=next;projectedPowerGeneration=snapshot.generation;
    projectedSuspendSerial=snapshot.suspendSerial;projectedNotificationSequence=snapshot.observedNotificationSequence;
    if(changed||observationChanged){
      ++powerRevision;
      if(probeRunning||adoptedRead&&updateSnapshot.installerActive)resumeProbe=true;
      // Only the wait on the read-only state query is revocable here. Keep
      // manifest and target-save ownership until their normal IPC terminal.
      adoptedRead?.controller.abort();
      const replacingPolicyRead=schedulerPolicyRead!==null;
      schedulerPolicyRead?.abort();
      // A newer observation retires an unresolved activity read even when the
      // public phase stays Ready. Leave a healthy, settled transfer alone.
      if(changed||observationChanged&&activityPending&&!next)recover();
      else if(replacingPolicyRead&&canRun())scheduler.wake();
      else if(adoptedRead&&!updateSnapshot.installerActive&&canRun())scheduler.wake();
      reconcileProbe();
    }
  },{observeSuspendSerial:true,observeNotificationSequence:true});
  requestSharedPowerLifecycleReconcile('update-predownload');
  window.addEventListener('online', recover);
  const offline=()=>{reconcileProbe();scheduler.wake();};
  window.addEventListener('offline', offline);
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
        if(probeTimer!==null)clearTimeout(probeTimer);
        probeTimer=setTimeout(probe,0);
      }else if(probeTimer===null)probeTimer=setTimeout(probe,60000);
    }
  }
  async function probe(){
    probeTimer=null;
    if(!needsProbe())return;
    if(updateSnapshot.installerActive||canProbeSuccessor())resumeProbe=false;
    probeRunning=true;
    const current=capturePowerScope();
    const power=getSharedPowerLifecycle();
    try{
      if(updateSnapshot.installerActive)await refreshAdopted();
      if(!current())return;
      if(!admission&&idleEvidence())await syncActivity();
      if(current()&&canProbeSuccessor()&&power?.suspendSerial!==undefined&&power.observedNotificationSequence!==undefined){
        const {generation,suspendSerial,observedNotificationSequence}=power;
        await probeBackgroundSuccessor({generation,suspendSerial,observedNotificationSequence},current);
      }
    }catch{/* Failed probe cannot cancel a healthy transfer. */}
    finally{probeRunning=false;reconcileProbe();}
  }
  reconcileProbe();
  scheduler.start();
  const owner = { stop() {
    if (active !== owner) return;
    ended=true;activityRevision++;if(probeTimer!==null)clearTimeout(probeTimer);probeTimer=null;
    adoptedRead?.controller.abort();
    activityPolicyRead?.abort();
    schedulerPolicyRead?.abort();
    setBackgroundGamePaused(true);void invoke('app.updatePredownloadActivity',{known:false,idle:false},{timeoutMs:5000}).catch(()=>{});
    active = null; stopProgress(); stopMaintenance(); stopProbeState(); offGame(); offGameReadiness(); offPower(); window.removeEventListener('online', recover); window.removeEventListener('offline', offline);
    scheduler.stop(); unregister();
  } };
  active = owner;
  return owner;
}

