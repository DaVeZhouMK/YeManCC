import {mouseModeState} from './mouseModeState';
import {captureBootPreferenceWrite,type BootPreferenceWrite} from './bootModeGlobalPreferences';
import { readMonitorCpuName } from './monitorHardwareMetadata';
import { startMonitorNoticeHost } from './monitorNoticeHost';
import { onSharedPowerLifecycle, isSharedPowerLifecycleAdmitted } from './powerLifecycleState';
import { subscribeMonitorGameLogo } from './monitorGameLogo';
// One sample source, ONE YMCC output adapter, two native/H5 carrier ports.
import { invoke, on } from './ipc';
import { getPolicyGame } from './gamePolicyTarget';
import { registerDeckyMirrorExtension } from './deckyMirrorExtensions';
import { createSteamHudRelay } from './steamHudRelay';
import { createDesktopHudOwner, type DesktopHudEvent } from './desktopHudOwner';
import { createHudCarrierRouter } from './monitorHudCarrierRouter';
import { createHudSampleSource } from './monitorHudSampleSource';
import { createHudClock } from './monitorHudClock';
import { acquireMonitorDemand, readMonitorSnapshot, subscribeMonitorHudSnapshot, subscribeMonitorTdpState, acceptPublishedMonitorSnapshot, isOwnedMonitorDisplaySnapshot } from './monitorData';
import { hudNativeDemand, normalizeMonitorOutput, type MonitorOutputPreferences } from './monitorHudModel';
import { monitorOutputCoordinator, publishMonitorHudAvailability, onMonitorOutputPreferencesChanged, onMonitorOutputApplied,
  readMonitorOutputPreferences, saveMonitorHudPlacement, saveMonitorHudTemplate, onMonitorOutputApplying, registerYmccHudOutputAdapter } from './monitorOutput';
export function startMonitorHudHost() {
  const noticeHost=startMonitorNoticeHost();
  let cpuName='';void readMonitorCpuName().then(value=>{if(!ended)cpuName=value;});
  let ready=false,ended=false,restoring=false,queued=false,version=0,availabilityVersion=0,lastAttemptKey='';
  let activePreferences:MonitorOutputPreferences|null=null;
  let desktopCloseSerial=0,applying=false,applyRevision=0;
  let recoveryWrite:BootPreferenceWrite={},latestIntent=0,preferenceRevision=0,intentPreferenceRevision=0;
  // Only failed startup/control operations retain this temporary wake lease.
  // Native pulses drive the deadline while the main WebView is hidden; the
  // one-shot browser timeout is a fallback, never a healthy polling loop.
  const retryDelays=[1000,3000] as const,maxAttempts=3;
  let retryTimer:number|null=null,retryAt=0,retryLease:(()=>void)|null=null,retrySerial=0;
  let settingsPending=false,settingsAttempts=0,restoreAttempts=0;
  let settingsPowerRevision=0,settingsReadRevision:number|null=null;
  let requestRevision=0,lastApplyOk=false,powerHeld=true,powerGeneration=-1,committedPowerGeneration=-1;
  let powerNotificationSequence:number|undefined,powerSuspendSerial:number|undefined,committedSuspendSerial:number|undefined;
  let availabilityScope=new AbortController();
  let readyTask:Promise<void>|null=null,preferenceDraft:MonitorOutputPreferences|null=null;
  let diagnosticRows=0,visibilityLease:(()=>void)|null=null;
  function note(stage:string,phase=''){
    if(ended||diagnosticRows>=32)return;++diagnosticRows;
    void invoke('diagnostics.frontendTrace',{where:'monitor-hud-startup',message:JSON.stringify({stage,phase,
      settingsAttempts,restoreAttempts,settingsPending,powerHeld,powerGeneration,
      enabled:activePreferences?.enabled??null,output:activePreferences?.output??null})},{timeoutMs:1000}).catch(()=>{});
  }
  const clock=createHudClock({nonce:()=>crypto.randomUUID(),now:Date.now,
    invoke:(command,args,signal)=>invoke(command,args,signal?{signal}:undefined),
    setTimer:(fn,ms)=>window.setTimeout(fn,ms),clearTimer:id=>window.clearTimeout(id as number),
    pulse(snapshot){if(!ended&&source.users()>0)acceptPublishedMonitorSnapshot(snapshot);relay.advance();desktop.advance();advanceRecovery();},
    failed(reason){
      const supervisionOnly=!!visibilityLease&&!desktop.inspect().owned&&!desktop.inspect().opening&&!restoring;
      releaseVisibilityLease();relay.clockUnavailable(reason);desktop.clockUnavailable(reason);
      // A terminal receipt may arrive while no sample/window owner exists.
      // Reuse bounded control retries; never immediately reacquire in a failure loop.
      if(supervisionOnly){if(restoreAttempts<maxAttempts)++restoreAttempts;lastAttemptKey='';scheduleRetry();}
    },
  });
  const source=createHudSampleSource({cpuName:()=>cpuName,canReuseSnapshot:isOwnedMonitorDisplaySnapshot,subscribeGameLogo:subscribeMonitorGameLogo,subscribeSnapshot:subscribeMonitorHudSnapshot,subscribeTdp:subscribeMonitorTdpState,
    readOnce:readMonitorSnapshot,acquire(need){
      const releaseClock=clock.acquire();let release:()=>void;
      try{release=acquireMonitorDemand('ymcc-product-hud',need);}catch(error){releaseClock();throw error;}
      return()=>{release();releaseClock();};
    },now:Date.now});
  source.setClockEnabled(false);
  const relay=createSteamHudRelay({externalClock:true,acquireClock:clock.acquire,now:Date.now,session:()=>crypto.randomUUID(),sample:source.sample,acquire:source.acquire,
    reply:(connection,message,current)=>invoke<boolean>('deckySidebar.mirrorReply',{connection,message,...(current?.bootContext?{origin:'boot-mode',bootContext:structuredClone(current.bootContext)}:{})},{timeoutMs:3000}),
    setTimer:(fn,ms)=>window.setTimeout(fn,ms),clearTimer:id=>window.clearTimeout(id as number),statusChanged:()=>invalidate()});
  const desktop=createDesktopHudOwner({externalClock:true,acquireClock:clock.acquire,visibilityTerminal:()=>releaseVisibilityLease(),availabilityRecovered:()=>{restoreAttempts=0;lastAttemptKey='';cancelRetry();note('native-ready-again');invalidate();},visibilityProblem:(reason,repairs)=>note('visibility-repair',reason+':'+repairs),visibilityAllowed:()=>ready&&!ended&&!powerHeld&&!applying&&activePreferences?.enabled===true&&activePreferences.output==='ymcc',scope:'global',target:()=>null,policyGame:getPolicyGame,mouseMode:()=>mouseModeState.value,nonce:()=>crypto.randomUUID(),now:Date.now,
    sample:source.sample,acquire:source.acquire,invoke:(cmd,args,signal)=>invoke(cmd,args as Record<string,unknown>,signal?{signal}:undefined),
    observe(listener){const stops=['desktopHud.ready','desktopHud.painted','desktopHud.prepared','desktopHud.visibility','desktopHud.closed','desktopHud.unavailable','desktopHud.expired','desktopHud.placement','desktopHud.template'].map(name=>on<DesktopHudEvent>(name,event=>{if(name==='desktopHud.closed')++desktopCloseSerial;listener(name,event);}));return()=>{for(const stop of stops)stop();};},
    placementChanged:value=>{void saveMonitorHudPlacement(value).catch(error=>console.warn('监控位置保存失败',error));},
    templateChanged:(template,revision)=>{void saveMonitorHudTemplate(template,revision).catch(error=>console.warn('监控模板保存失败',error));},
    setTimer:(fn,ms)=>window.setTimeout(fn,ms),clearTimer:id=>window.clearTimeout(id as number),changed:()=>invalidate()});
  const router=createHudCarrierRouter({available:relay.available,visible:relay.visible,state:relay.state,setVisible:relay.setVisible},desktop,'desktop-global');
  const wantsHud=()=>activePreferences?.enabled===true&&activePreferences.output==='ymcc';
  function releaseVisibilityLease(){const release=visibilityLease;visibilityLease=null;release?.();}
  function syncVisibilityLease(){
    const expected=ready&&!ended&&!powerHeld&&wantsHud()&&!desktop.inspect().recoveryBlocked;
    desktop.setVisibilityExpected(expected);
    if(!expected){releaseVisibilityLease();return;}
    // A control-only lease of the EXISTING native pulse survives missing
    // windows/sample owners. It acquires no sensor demand and no JS timer.
    if(!visibilityLease){try{visibilityLease=clock.acquire();}catch{note('visibility-clock-unavailable');}}
  }
  function cancelRetry(){
    ++retrySerial;if(retryTimer!==null)window.clearTimeout(retryTimer);retryTimer=null;retryAt=0;
    const release=retryLease;retryLease=null;release?.();
  }
  function scheduleRetry(){
    const attempts=settingsPending?settingsAttempts:restoreAttempts;
    if(ended||!ready||powerHeld||retryTimer!==null||(!settingsPending&&!wantsHud()))return;
    if(attempts>=maxAttempts||desktop.inspect().recoveryBlocked){note('recovery-stopped');return;}
    const serial=++retrySerial,delay=retryDelays[Math.max(0,attempts-1)];
    retryAt=Date.now()+delay;
    try{retryLease=clock.acquire();}catch{/* The bounded one-shot still retires failed clock generations. */}
    retryTimer=window.setTimeout(()=>{if(serial===retrySerial)advanceRecovery();},delay);
    note('retry-scheduled');
  }
  function advanceRecovery(){
    if(!retryAt||Date.now()<retryAt)return;
    cancelRetry();if(ended||powerHeld)return;
    if(settingsPending)void loadStartupPreferences();else invalidate();
  }
  function configureSource(prefs:MonitorOutputPreferences){
    if(prefs.output==='ymcc')source.setDemand(hudNativeDemand(prefs.ymcc.fields));
    source.setClockEnabled(prefs.output==='ymcc'&&prefs.enabled&&prefs.ymcc.fields.clock===true);
    source.setGameLogoEnabled(prefs.output==='ymcc'&&prefs.enabled&&prefs.ymcc.underlay==='game-logo');
    clock.setFrameTime(prefs.output==='ymcc'&&prefs.enabled&&prefs.ymcc.fields.frametimeGraph===true);
  }
  async function loadStartupPreferences(){
    if(ended||settingsReadRevision!==null||(powerHeld&&powerGeneration!==-1))return;
    const powerAt=settingsReadRevision=settingsPowerRevision;
    const preferenceAt=preferenceRevision,intentAt=latestIntent;
    try{
      ++settingsAttempts;const prefs=await readMonitorOutputPreferences();
      if(ended||powerAt!==settingsPowerRevision)return;
      // An already-published intent owns startup, even if its apply was waiting
      // for this host. A later ordinary save supplies the current startup bank.
      if(latestIntent===intentAt&&intentAt===0)activePreferences=preferenceAt===preferenceRevision?prefs:preferenceDraft??prefs;
      settingsPending=false;settingsAttempts=0;
    }catch{
      if(ended||powerAt!==settingsPowerRevision)return;
      if(latestIntent===0&&preferenceRevision===preferenceAt){settingsPending=true;note('settings-read-failed');}
      else{if(latestIntent===0&&preferenceDraft)activePreferences=preferenceDraft;settingsPending=false;}
    }finally{
      settingsReadRevision=null;
      if(!ended){
        if(activePreferences){configureSource(activePreferences);noticeHost.ready(activePreferences);}
        const first=!ready;ready=true;if(first)relay.preferencesReady();note('preferences-loaded');
        // The repository may share or repair the document. Revalidate only
        // after that read settles; only a new Ready boundary resets attempts.
        if(settingsPending){
          if(!powerHeld&&settingsAttempts===0)await loadStartupPreferences();
          else scheduleRetry();
        }else invalidate();
      }
    }
  }
  async function refreshAvailability(){
    if(!ready||ended||powerHeld)return;
    const at=++availabilityVersion,scope=availabilityScope.signal;
    const available=await router.available(undefined,scope);
    if(at===availabilityVersion&&!ended&&!scope.aborted){syncVisibilityLease();publishMonitorHudAvailability(available);}
  }
  function invalidate(){
    ++version;syncVisibilityLease();if(!ready||ended)return;void refreshAvailability();
    if(queued)return;queued=true;queueMicrotask(()=>{queued=false;void reconcile();});
  }
  const contextKey=(route:string|null)=>route+'|'+(route==='steam'?JSON.stringify(relay.inspect().status?.game??null):'')+'|'+desktopCloseSerial+'|'+(route==='desktop'?desktop.inspect().retirementRevision:0);
  async function reconcile(){
    if(!ready||ended||powerHeld||settingsPending||restoring||applying||retryAt||!wantsHud()||restoreAttempts>=maxAttempts||desktop.inspect().recoveryBlocked)return;
    const revision=applyRevision,startedVersion=version,scope=availabilityScope.signal;
    const route=await router.kind(activePreferences!,scope);
    // Recheck intent after native IO. Same-output appearance drafts stay live,
    // but an explicit switch/OFF or a newer event invalidates this recovery.
    if(!ready||ended||powerHeld||scope.aborted||restoring||applying||!wantsHud()||revision!==applyRevision||startedVersion!==version)return;
    if(!route){++restoreAttempts;note('carrier-unavailable');scheduleRetry();return;}
    const desired=activePreferences!;
    const key=contextKey(route); // Default desktop never needs a game identity.
    if(key===lastAttemptKey||desktop.inspect().opening)return;
    restoring=true;++restoreAttempts;const at=version,requestAt=requestRevision;
    const current=()=>!ended&&!powerHeld&&requestAt===requestRevision&&wantsHud();
    try{
      const result=await monitorOutputCoordinator.apply(desired,captureBootPreferenceWrite(recoveryWrite),current);
      if(current()){
        if(result.ok){restoreAttempts=0;cancelRetry();note('restored');}
        else{note('apply-failed',result.phase);scheduleRetry();}
      }
    }catch(error){
      if(current()){
        if(error instanceof Error&&(error.message==='BOOT_PREFERENCE_REQUEST_EXPIRED'||error.message==='MONITOR_OUTPUT_REQUEST_EXPIRED')){lastAttemptKey=key;cancelRetry();note('request-expired');}
        else{note('apply-rejected');scheduleRetry();}
      }
    }finally{restoring=false;if(at!==version||requestAt!==requestRevision)invalidate();}
  }
  let steamPresence:boolean|null|undefined;
  const offSteamPresence=on<{steamKnown:boolean;steamRunning:boolean}>('deckySidebar.updated',state=>{
    const next=state.steamKnown===true?state.steamRunning:null;
    if(next===steamPresence)return;steamPresence=next;relay.noteSteamPresence(next);
  });
  const offClock=on<{nonce:string;snapshot:unknown;expired?:boolean}>('monitor.hudPulse',event=>clock.accept(event));
  const offExtension=registerDeckyMirrorExtension({prefix:'hud.',connected:peer=>relay.connected(peer),request:event=>relay.request(event)});
  const offAdapter=registerYmccHudOutputAdapter({global:true,globalFor:async prefs=>await router.kind(prefs)==='desktop',async available(prefs){return ready&&await router.available(prefs??activePreferences??undefined);},async visible(){return router.visible();},async state(){return router.active()==='desktop'&&desktop.waiting()?'armed':router.state();},setVisible:async(show,prefs,current)=>{if(current&&!await current())throw Error('MONITOR_OUTPUT_REQUEST_EXPIRED');if(show)source.setDemand(hudNativeDemand(prefs.ymcc.fields));return router.setVisible(show,prefs,current);}});
  const offPreferences=onMonitorOutputPreferencesChanged((prefs,write)=>{
    ++preferenceRevision;++requestRevision;preferenceDraft=normalizeMonitorOutput(prefs);recoveryWrite=captureBootPreferenceWrite(write??{});
    noticeHost.update(prefs);
    if(prefs.output==='ymcc')source.setDemand(hudNativeDemand(prefs.ymcc.fields));
    source.setClockEnabled(prefs.output==='ymcc' && prefs.enabled && prefs.ymcc.fields.clock === true);
    source.setGameLogoEnabled(prefs.output==='ymcc' && prefs.enabled && prefs.ymcc.underlay==='game-logo');
    clock.setFrameTime(prefs.output==='ymcc' && prefs.enabled && prefs.ymcc.fields.frametimeGraph === true);
    relay.updateAppearance(prefs);desktop.updateAppearance(prefs);
    // Retire obsolete custom recovery while the existing save/apply queue runs.
    // Saving the new target alone still does not start or migrate any output.
    if(!prefs.enabled||prefs.output!=='ymcc'){
      activePreferences=normalizeMonitorOutput(prefs);settingsPending=false;cancelRetry();syncVisibilityLease();
    }else if(activePreferences?.output==='ymcc'){
      const next=normalizeMonitorOutput(prefs);
      activePreferences={...activePreferences,mouseStatusEnabled:next.mouseStatusEnabled,ymcc:next.ymcc,ymccTemplate:next.ymccTemplate,ymccTemplateRevision:next.ymccTemplateRevision,ymccTemplates:next.ymccTemplates};
    }
  });
  const offApplying=onMonitorOutputApplying((value,intent)=>{
    if(intent){
      if(!intent.recovery){++requestRevision;restoreAttempts=0;lastAttemptKey='';cancelRetry();}
      activePreferences=normalizeMonitorOutput(intent.preferences);latestIntent=intent.id;recoveryWrite=captureBootPreferenceWrite(intent.write);intentPreferenceRevision=preferenceRevision;lastApplyOk=false;settingsPending=false;
    }
    applying=value;++applyRevision;
    if(value||!ready||ended)return;
    if(!lastApplyOk&&restoreAttempts>0)scheduleRetry();
    invalidate();
  });
  const offApplied=onMonitorOutputApplied((prefs,result,intent)=>{
    if(intent&&(intent.id!==latestIntent||intentPreferenceRevision!==preferenceRevision))return;
    if(intent)recoveryWrite=captureBootPreferenceWrite(intent.write);
    // Keep the requested output/OFF intent even when its application fails,
    // so a later event cannot revive the previously selected custom HUD.
    activePreferences=normalizeMonitorOutput(prefs);
    lastApplyOk=result.ok;
    if(result.ok){
      configureSource(activePreferences);restoreAttempts=0;cancelRetry();
      // The completed transaction already selected and confirmed a carrier.
      // A later status read must not hold or reassign that completion.
      const route=router.active();
      const confirmed=route==='desktop'?desktop.inspect().owned?.prepared:route==='steam'&&relay.state()==='shown';
      lastAttemptKey=result.applied==='ymcc'&&confirmed?contextKey(route):'';
    }
  });
  const offPower=onSharedPowerLifecycle(snapshot=>{
    const held=!isSharedPowerLifecycleAdmitted(snapshot),newGeneration=snapshot.generation!==powerGeneration;
    const suspendChanged=snapshot.suspendSerial!==powerSuspendSerial;
    const scopeChanged=snapshot.observedNotificationSequence!==powerNotificationSequence||suspendChanged;
    if(ended||held===powerHeld&&!newGeneration&&!scopeChanged)return;
    const readyAfterBoundary=!held&&(powerHeld||newGeneration||suspendChanged);
    const newCommittedGeneration=snapshot.generation!==committedPowerGeneration;
    const newCommittedBoundary=!held&&(newCommittedGeneration||(snapshot.suspendSerial!==undefined&&
      committedSuspendSerial!==undefined&&snapshot.suspendSerial>committedSuspendSerial));
    const retainedReadyAdvance=!held&&!powerHeld&&!newCommittedBoundary&&scopeChanged;
    if(held||(powerGeneration!==-1&&(newGeneration||scopeChanged)))++settingsPowerRevision;
    ++requestRevision;powerHeld=held;powerGeneration=snapshot.generation;
    powerNotificationSequence=snapshot.observedNotificationSequence;powerSuspendSerial=snapshot.suspendSerial;
    availabilityScope.abort();availabilityScope=new AbortController();
    // Notification-only advances retire old requests but keep the current
    // suspend boundary's attempts and retry delay.
    if(!retainedReadyAdvance)cancelRetry();
    syncVisibilityLease();
    desktop.reconcileVisibilityAfterPowerChange(readyAfterBoundary);
    if(readyAfterBoundary)clock.reconcileAfterWake();
    if(!held){
      committedPowerGeneration=snapshot.generation;
      if(newCommittedGeneration||snapshot.suspendSerial!==undefined)committedSuspendSerial=snapshot.suspendSerial;
      if(newCommittedBoundary){
        restoreAttempts=0;
        if(settingsPending)settingsAttempts=settingsReadRevision===settingsPowerRevision?1:0;
      }
      if(settingsPending){
        if(!retainedReadyAdvance&&settingsAttempts<maxAttempts)void loadStartupPreferences();
      }else invalidate();
    }
  },{observeNotificationSequence:true,observeSuspendSerial:true});
  return {
    async ready(){
      if(ended)return;
      if(!readyTask){if(!activePreferences&&latestIntent===0)settingsPending=true;readyTask=loadStartupPreferences();}
      await readyTask;
    },
    stop(){if(ended)return;ended=true;++requestRevision;availabilityScope.abort();cancelRetry();syncVisibilityLease();offPower();noticeHost.stop();ready=false;publishMonitorHudAvailability(false);offApplied();offApplying();offPreferences();offExtension();offSteamPresence();
      relay.dispose();void desktop.dispose().finally(()=>{offClock();clock.dispose();});offAdapter();},
    inspect:()=>({steam:relay.inspect(),desktop:desktop.inspect(),carrier:router.active(),sampleUsers:source.users(),clock:clock.inspect(),restoring,
      recovery:{settingsPending,settingsAttempts,restoreAttempts,retryAt,powerHeld,powerGeneration,visibilityLease:!!visibilityLease}}),
  };
}
