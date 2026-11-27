import {invoke,on} from './ipc';

export interface SharedPowerLifecycleSnapshot {
  phase:'ready'|'suspending'|'suspended'|'resuming';
  generation:number;
  suspendSerial?:number;
  inputReady?:boolean;
  /** The current Fan wake projected by Native; the Fan gate still decides writes. */
  fanWakeGeneration?:number;
  /** Optional Fan startup candidate; never a device write grant. */
  fanStartupWakeGeneration?:number;
  /** Opaque Native boundary for a replayable candidate. Never a write grant. */
  fanStartupWakeBoundary?:string;
  hardwareWritesAllowed?:boolean;
  /** Native's F5 receipt; distinct from the public Ready commit. */
  resumeReady?:boolean;
  /** The current committed wake requires the renderer to reset input edges. */
  gamepadUiRestart?:boolean;
  /** Monotonic invalidation of the game menu's pause observation. */
  gameWakeRecoveryRevision?:number;
  /** False denies admission even if the retained diagnostic phase is Ready. */
  known?:boolean;
  publicationRevision?:number;
  observedNotificationSequence?:number;
  notificationPending?:boolean;
  /** Local projection: a newer scoped hint still needs matching Native evidence. */
  scopeReadbackPending?:true;
}
// Public admission is separate from diagnostic Ready and controller presence.
export function isSharedPowerLifecycleAdmitted(state:SharedPowerLifecycleSnapshot|null|undefined):boolean {
  return !!state&&state.known!==false&&state.phase==='ready'&&
    state.hardwareWritesAllowed===true&&state.notificationPending!==true;
}

// The write scope follows notification ingress and Suspend, independent of
// metadata-only publications in the same Ready period.
export type SharedPowerWriteScope = readonly [generation: number, suspendSerial: number, notificationSequence: number];

export function sharedPowerWriteScope(power: SharedPowerLifecycleSnapshot | null): SharedPowerWriteScope | null {
  if (!power || !isSharedPowerLifecycleAdmitted(power)) return null;
  const { generation, suspendSerial, observedNotificationSequence } = power;
  if (!Number.isSafeInteger(generation) || generation <= 0 ||
      suspendSerial === undefined || !Number.isSafeInteger(suspendSerial) || suspendSerial < 0 ||
      observedNotificationSequence === undefined || !Number.isSafeInteger(observedNotificationSequence) || observedNotificationSequence < 0) return null;
  return [generation, suspendSerial, observedNotificationSequence];
}

type Listener=(state:SharedPowerLifecycleSnapshot)=>void;
export interface SharedPowerLifecycleDependencies {
  read:(signal:AbortSignal)=>Promise<unknown>;
  listen:(name:string,listener:(value:unknown)=>void)=>()=>void;
  clock?:{setTimeout:(callback:()=>void,delay:number)=>unknown;clearTimeout:(timer:unknown)=>void};
}

// This source observes native facts only. It never commits wake, opens devices,
// or retries business operations. The first/last subscriber owns its lifetime.
export function createSharedPowerLifecycleSource(deps:SharedPowerLifecycleDependencies) {
  const clock=deps.clock??{
    setTimeout:(callback:()=>void,delay:number)=>globalThis.setTimeout(callback,delay),
    clearTimeout:(timer:unknown)=>globalThis.clearTimeout(timer as ReturnType<typeof setTimeout>),
  };
  const listeners=new Set<Listener>();
  const notificationListeners=new Set<Listener>();
  const gameWakeRecoveryListeners=new Set<Listener>();
  const fanWakeListeners=new Set<Listener>();
  const fanWakeRepublishListeners=new Map<Listener,Listener>();
  const suspendListeners=new Set<Listener>();
  const readyRecheckListeners=new Map<Listener,Listener>();
  const lastPublicationDelivery=new WeakMap<Listener,number>();
  const confirmations=new Map<Listener,number>();
  const retryDelays=[1000,2000,5000,15000,30000];
  const hints=new Set<string>();
  let snapshot:SharedPowerLifecycleSnapshot|null=null;
  let readbackFloor:{generation:number;suspendSerial:number;notificationSequence:number}|null=null;
  let stops:Array<()=>void>=[],epoch=0,observation=0,committedGeneration=0;
  let publicationRevision=-1,authoritySeen=false,requestVersion=0;
  let notificationSequence=-1,deliverySerial=0,publicationDispatch=0;
  let inFlight:AbortController|null=null;
  let started=false,queued=false,followup=false,reconcileDemand=false,reconcileVersion=0;
  const needsRead=()=>reconcileDemand||confirmations.size>0;
  let retryTimer:unknown=null,retryStep=0,retrySerial=0;
  const counters={readStarted:0,readKnown:0,readUnknown:0,readStale:0,readFailed:0,authorityEvents:0,published:0,confirmationDelivered:0};
  const count=(key:keyof typeof counters)=>{counters[key]=Math.min(1000000000,counters[key]+1);};
  const metric=(value:unknown)=>typeof value==='number'&&Number.isSafeInteger(value)&&value>=0?value:null;
  let lastObservation={source:'none',outcome:'never',generation:null as number|null,publicationRevision:null as number|null,
    observedNotificationSequence:null as number|null,requestVersion:0,lifetimeEpoch:0};
  function record(source:string,outcome:string,value:unknown=snapshot,requested=requestVersion) {
    const state=value&&typeof value==='object'?value as Partial<SharedPowerLifecycleSnapshot>:null;
    lastObservation={source,outcome,generation:metric(state?.generation),publicationRevision:metric(state?.publicationRevision),
      observedNotificationSequence:metric(state?.observedNotificationSequence),requestVersion:requested,lifetimeEpoch:epoch};
  }
  const active=()=>started&&listeners.size>0;
  const copy=()=>snapshot?{...scopedPowerView(snapshot)}:null;
  // Export diagnostics from cached values only; this must never renew demand.
  function getSharedPowerLifecycleDiagnostics() {
    return {schemaVersion:1,source:'frontend-shared-power-lifecycle',active:active(),lifetimeEpoch:epoch,subscriberCount:listeners.size,
      snapshot:copy(),read:{inFlight:inFlight!==null,queued,followup,demand:needsRead(),retryStep,retryTimerActive:retryTimer!==null,pendingReadyConfirmations:confirmations.size},
      counters:{...counters},lastObservation:{...lastObservation}};
  }
  function notify(listener:Listener,value:SharedPowerLifecycleSnapshot) {
    try{listener({...scopedPowerView(value)});}catch{/* One consumer cannot block other observers. */}
  }
  function same(a:SharedPowerLifecycleSnapshot|null,b:SharedPowerLifecycleSnapshot) {
    return !!a&&a.phase===b.phase&&a.generation===b.generation&&a.known===b.known&&
      a.inputReady===b.inputReady&&a.hardwareWritesAllowed===b.hardwareWritesAllowed&&a.resumeReady===b.resumeReady&&
      a.gamepadUiRestart===b.gamepadUiRestart&&a.notificationPending===b.notificationPending&&
      a.scopeReadbackPending===b.scopeReadbackPending;
  }
  function ordinal(value:unknown):value is number {
    return typeof value==='number'&&Number.isSafeInteger(value)&&value>=0;
  }
  function coversReadbackFloor(state:SharedPowerLifecycleSnapshot):boolean {
    return !readbackFloor||(state.generation>=readbackFloor.generation&&
      (state.suspendSerial??-1)>=readbackFloor.suspendSerial&&
      (state.observedNotificationSequence??-1)>=readbackFloor.notificationSequence);
  }
  function withoutScopedAdmission(state:SharedPowerLifecycleSnapshot):SharedPowerLifecycleSnapshot {
    const denyF5=state.resumeReady===true||(state.phase==='resuming'&&state.resumeReady!==false);
    if(state.scopeReadbackPending&&!denyF5&&state.hardwareWritesAllowed!==true&&state.inputReady!==true&&state.gamepadUiRestart!==true&&
      state.fanWakeGeneration===undefined&&state.fanStartupWakeGeneration===undefined&&
      state.fanStartupWakeBoundary===undefined)return state;
    const denied:SharedPowerLifecycleSnapshot={...state,scopeReadbackPending:true};
    if(denyF5)denied.resumeReady=false;
    if(denied.hardwareWritesAllowed===true)denied.hardwareWritesAllowed=false;
    if(denied.inputReady===true)denied.inputReady=false;
    if(denied.gamepadUiRestart===true)denied.gamepadUiRestart=false;
    delete denied.fanWakeGeneration;
    delete denied.fanStartupWakeGeneration;
    delete denied.fanStartupWakeBoundary;
    return denied;
  }
  function scopedPowerView(state:SharedPowerLifecycleSnapshot):SharedPowerLifecycleSnapshot {
    return coversReadbackFloor(state)?state:withoutScopedAdmission(state);
  }
  function publish(value:SharedPowerLifecycleSnapshot,previous=snapshot&&scopedPowerView(snapshot)) {
    value=scopedPowerView(value);
    const priorGameWake=snapshot?.gameWakeRecoveryRevision??0;
    if((value.gameWakeRecoveryRevision??0)<priorGameWake)value={...value,gameWakeRecoveryRevision:priorGameWake};
    const gameWakeChanged=(value.gameWakeRecoveryRevision??0)>priorGameWake;
    if(value.known!==false&&value.phase==='ready')committedGeneration=Math.max(committedGeneration,value.generation);
    const changed=!same(previous,value);
    const scopeChanged=!!snapshot&&snapshot.observedNotificationSequence!==value.observedNotificationSequence;
    const fanWakeChanged=previous?.fanWakeGeneration!==value.fanWakeGeneration||
      previous?.fanStartupWakeGeneration!==value.fanStartupWakeGeneration||
      previous?.fanStartupWakeBoundary!==value.fanStartupWakeBoundary;
    const suspendChanged=snapshot?.suspendSerial!==value.suspendSerial;
    const fanWakeRepublished=fanWakeRepublishListeners.size>0&&!changed&&!scopeChanged&&!fanWakeChanged&&!suspendChanged&&!gameWakeChanged&&
      value.known===true&&value.notificationPending===false&&value.fanWakeGeneration!==undefined&&
      ordinal(value.publicationRevision)&&value.publicationRevision>(snapshot?.publicationRevision??-1);
    // Always retain ordering metadata for the getter/replay, without turning
    // duplicate publications into another round of business callbacks.
    snapshot={...value};
    // A merged suspend can advance without changing the ordinary Ready fact.
    // Compatibility hints from the previous suspend scope cannot deduplicate it.
    if(changed||suspendChanged)hints.clear();
    if(!changed&&(!scopeChanged||!notificationListeners.size)&&(!fanWakeChanged||!fanWakeListeners.size)&&
      (!suspendChanged||!suspendListeners.size)&&(!gameWakeChanged||!gameWakeRecoveryListeners.size)&&!fanWakeRepublished)return;
    if(changed)count('published');
    // A consumer can synchronously deliver a newer native fact or restart the
    // subscription lifetime. Never finish an older Ready fanout afterward.
    // Metadata-only observations do not retire delivery to the other clients.
    const delivery=changed?++deliverySerial:deliverySerial,owner=epoch,dispatch=++publicationDispatch;
    for(const listener of [...listeners]){
      if(owner!==epoch||delivery!==deliverySerial||!active())break;
      const scopeListener=notificationListeners.has(listener),fanWakeListener=fanWakeListeners.has(listener);
      const suspendListener=suspendListeners.has(listener);
      if(!listeners.has(listener))continue;
      if(fanWakeRepublished){
        // Ingress and merged suspend can advance without a new publication revision.
        const current=snapshot;
        if(!current||!coversReadbackFloor(current)||current.publicationRevision!==value.publicationRevision||
          current.fanWakeGeneration!==value.fanWakeGeneration||
          current.observedNotificationSequence!==value.observedNotificationSequence||current.suspendSerial!==value.suspendSerial)break;
        const recheck=fanWakeRepublishListeners.get(listener);
        if(recheck)notify(recheck,current);
        continue;
      }
      if(!changed&&!(scopeChanged&&scopeListener)&&!(fanWakeChanged&&fanWakeListener)&&!(suspendChanged&&suspendListener)&&
        !(gameWakeChanged&&gameWakeRecoveryListeners.has(listener)))continue;
      // Reentrant delivery already notified these observers of the newer suspend scope.
      if(suspendListener&&value.suspendSerial!==snapshot?.suspendSerial)continue;
      // A nested metadata publication may have reached only some of this fanout.
      if((lastPublicationDelivery.get(listener)??0)>dispatch)continue;
      lastPublicationDelivery.set(listener,dispatch);
      // Options determine which changes trigger delivery, not which snapshot a listener receives.
      notify(listener,snapshot!);
    }
  }
  function validAuthorityFields(state:Partial<SharedPowerLifecycleSnapshot>):boolean {
    if('known' in state&&typeof state.known!=='boolean')return false;
    if('publicationRevision' in state&&!ordinal(state.publicationRevision))return false;
    if('observedNotificationSequence' in state&&!ordinal(state.observedNotificationSequence))return false;
    if('suspendSerial' in state&&!ordinal(state.suspendSerial))return false;
    for(const field of ['inputReady','hardwareWritesAllowed','resumeReady','gamepadUiRestart','notificationPending'] as const){
      if(field in state&&typeof state[field]!=='boolean')return false;
    }
    return true;
  }
  function decode(value:unknown):SharedPowerLifecycleSnapshot|null {
    if(!value||typeof value!=='object')return null;
    const state=value as Partial<SharedPowerLifecycleSnapshot>;
    if(!Number.isSafeInteger(state.generation)||Number(state.generation)<=0||
      !['ready','suspending','suspended','resuming'].includes(String(state.phase))||!validAuthorityFields(state))return null;
    return {phase:state.phase!,generation:state.generation!,
      ...(ordinal(state.suspendSerial)?{suspendSerial:state.suspendSerial}:{}),
      ...(ordinal(state.fanWakeGeneration)&&state.fanWakeGeneration>0?{fanWakeGeneration:state.fanWakeGeneration}:{}),
      ...(ordinal(state.fanStartupWakeGeneration)&&state.fanStartupWakeGeneration>0?{fanStartupWakeGeneration:state.fanStartupWakeGeneration}:{}),
      ...(ordinal(state.fanStartupWakeGeneration)&&state.fanStartupWakeGeneration>0&&
        typeof state.fanStartupWakeBoundary==='string'&&state.fanStartupWakeBoundary.length>0?{fanStartupWakeBoundary:state.fanStartupWakeBoundary}:{}),
      ...(typeof state.inputReady==='boolean'?{inputReady:state.inputReady}:{}),
      ...(typeof state.hardwareWritesAllowed==='boolean'?{hardwareWritesAllowed:state.hardwareWritesAllowed}:{}),
      ...(typeof state.resumeReady==='boolean'?{resumeReady:state.resumeReady}:{}),
      ...(typeof state.gamepadUiRestart==='boolean'?{gamepadUiRestart:state.gamepadUiRestart}:{}),
      ...(state.known===true&&ordinal(state.gameWakeRecoveryRevision)&&state.gameWakeRecoveryRevision>0?
        {gameWakeRecoveryRevision:state.gameWakeRecoveryRevision}:{}),
      ...(typeof state.known==='boolean'?{known:state.known}:{}),
      ...(state.publicationRevision!==undefined?{publicationRevision:state.publicationRevision}:{}),
      ...(ordinal(state.observedNotificationSequence)?{observedNotificationSequence:state.observedNotificationSequence}:{}),
      ...(typeof state.notificationPending==='boolean'?{notificationPending:state.notificationPending}:{}),
    };
  }
  // Publications and reads describe native current state, including same-gen
  // corrections. Terminal-generation protection applies only to legacy hints.
  function acceptAuthority(value:unknown,origin:'event'|'shared-read'|'direct-readback',readVersion=requestVersion):'known'|'unknown'|'stale' {
    const event=origin==='event';
    const raw=value&&typeof value==='object'?value as Partial<SharedPowerLifecycleSnapshot>:null;
    const revision=ordinal(raw?.publicationRevision)?raw.publicationRevision:-1;
    const sequence=ordinal(raw?.observedNotificationSequence)?raw.observedNotificationSequence:-1;
    if(revision>=0&&revision<publicationRevision)return 'stale';
    // Callback ingress advances before its UI consumer publishes a revision.
    // A delayed pre-ingress Ready must not reopen that same-revision fence or
    // confirm recovery. Reject before changing either ordering watermark.
    if(sequence>=0&&sequence<notificationSequence)return 'stale';
    const decoded=decode(value);
    // Once native ingress ordering is established, a delayed legacy envelope
    // cannot remove its fence. Invalid envelopes cannot advance either watermark.
    const ordered=notificationSequence<0||sequence>=0;
    let current=ordered?decoded:null;
    // Native may explicitly deny with metadata alone. This valid UNKNOWN still
    // fences older receipts, without granting or inventing a phase/generation.
    const metadataUnknown=ordered&&raw?.known===false&&!('generation' in raw)&&!('phase' in raw)&&validAuthorityFields(raw);
    const envelopeAccepted=!!current||metadataUnknown;
    if(current&&snapshot&&current.generation<snapshot.generation)return 'stale';
    const coversReadback=!!readbackFloor&&!!current&&coversReadbackFloor(current);
    if(current&&snapshot?.suspendSerial!==undefined){
      if(current.suspendSerial!==undefined&&current.suspendSerial<snapshot.suspendSerial)return 'stale';
      current.suspendSerial??=snapshot.suspendSerial;
    }
    const suspendChanged=!!current&&snapshot?.suspendSerial!==current.suspendSerial;
    const sequenceChanged=envelopeAccepted&&sequence>=0&&sequence!==notificationSequence;
    const unknown=!current||current.known===false||((event||authoritySeen||revision>=0)&&current.known!==true);
    const admitted=!unknown&&isSharedPowerLifecycleAdmitted(current);
    // A hint only constrains recovery evidence. Older denials can still be
    // observed; an older Ready cannot settle demand or advance a watermark.
    if(admitted&&readbackFloor&&!coversReadback)return 'stale';
    // Use the explicit receipt range, before any inherited serial, for recovery fields.
    if(current&&readbackFloor&&!coversReadback)current=withoutScopedAdmission(current);
    // Compare against what observers could see while the old scope was fenced.
    const previous=snapshot&&scopedPowerView(snapshot);
    const settlesScopedRead=admitted&&coversReadback;
    if(settlesScopedRead)readbackFloor=null;
    authoritySeen ||= event||raw?.known!==undefined||revision>=0;
    if(envelopeAccepted&&revision>=0)publicationRevision=revision;
    if(envelopeAccepted&&sequence>=0)notificationSequence=sequence;
    if(unknown){
      const previous=current??snapshot;
      const denied=previous?{...previous,known:false,inputReady:false,hardwareWritesAllowed:false,
        ...(envelopeAccepted&&revision>=0?{publicationRevision:revision}:{}),
        ...(envelopeAccepted&&sequence>=0?{observedNotificationSequence:sequence}:{})}:null;
      if(event&&(!denied||!same(snapshot,denied)||suspendChanged||sequenceChanged))++observation;
      if(denied)publish(denied);
      reconcileDemand=true;
      return 'unknown';
    }
    const changed=!same(snapshot,current!);
    const freshFact=!snapshot||snapshot.generation!==current!.generation||snapshot.phase!==current!.phase||
      (snapshot.known!==true&&current!.known===true)||(!isSharedPowerLifecycleAdmitted(snapshot)&&admitted);
    const pendingScope=readbackFloor??previous;
    const replacesPendingSuspend=origin!=='shared-read'&&!admitted&&previous&&!isSharedPowerLifecycleAdmitted(previous)&&
      (inFlight!==null||retryTimer!==null)&&pendingScope&&(!readbackFloor||coversReadback)&&
      current!.known===true&&sequence>=0&&ordinal(raw?.suspendSerial)&&
      (current!.generation>pendingScope.generation||raw.suspendSerial>(pendingScope.suspendSerial??-1));
    // A Ready confirmation belongs to the notification scope actually read,
    // even when an intervening event leaves the business snapshot unchanged.
    if(event&&(changed||suspendChanged||sequenceChanged))++observation;
    // Metadata-only publications cannot refund a failed-read backoff. A real
    // new native phase/generation or confirmed recovery can settle its demand.
    if(admitted&&(!event||freshFact||settlesScopedRead)){
      // A later one-shot confirmation does not move the ordinary read boundary.
      if(event||readVersion>=reconcileVersion)reconcileDemand=false;
      if(event)followup=false;
      retryStep=0;cancelRetry();
    }else if(!admitted)reconcileDemand=true;
    let retired:AbortController|null=null;
    if(replacesPendingSuspend){
      // An external authoritative suspend can replace the old shared read, not itself.
      retired=inFlight;inFlight=null;followup=false;
      retryStep=0;cancelRetry();
      queueRead(); // The microtask reads the latest scope after synchronous fanout.
    }
    const owner=epoch,version=observation;
    publish(current!,previous);
    retired?.abort();
    // A known transition still needs observation if its final Ready is lost.
    if(event&&owner===epoch&&version===observation&&needsRead()&&!queued){
      if(admitted)queueRead();else armRetry();
    }
    return 'known';
  }
  function cancelRetry() {
    ++retrySerial;
    if(retryTimer!==null)clock.clearTimeout(retryTimer);
    retryTimer=null;
  }
  function armRetry() {
    if(!active()||!needsRead()||retryTimer!==null)return;
    const owner=epoch,serial=++retrySerial,delay=retryDelays[Math.min(retryStep++,retryDelays.length-1)];
    retryTimer=clock.setTimeout(()=>{
      if(owner!==epoch||serial!==retrySerial||!active())return;
      retryTimer=null;queueRead();
    },delay);
  }
  function retireRead() {
    const old=inFlight;
    inFlight=null;followup=false;
    old?.abort();
  }
  function queueRead() {
    if(!active()||queued||retryTimer!==null)return;
    if(inFlight){followup=true;return;}
    queued=true;const owner=epoch;
    queueMicrotask(()=>{
      if(owner!==epoch||!active())return;
      queued=false;if(!needsRead())return;
      const attempt=new AbortController(),version=observation,requested=requestVersion;
      inFlight=attempt;
      const ownsRead=()=>owner===epoch&&active()&&inFlight===attempt;
      count('readStarted');record('read','started',snapshot,requested);
      let readAccepted=false;
      // Capture the observation at the actual read start, not at queue time.
      let read:Promise<unknown>;
      try{read=Promise.resolve(deps.read(attempt.signal));}catch(error){read=Promise.reject(error);}
      void read.then(value=>{
        if(!ownsRead())return;
        if(version!==observation){record('read','superseded',value,requested);if(needsRead())followup=true;return;}
        const prior=snapshot;
        const outcome=acceptAuthority(value,'shared-read',requested);
        if(!ownsRead())return;
        count(outcome==='known'?'readKnown':outcome==='unknown'?'readUnknown':'readStale');record('read',outcome,value,requested);
        readAccepted=outcome==='known';
        if(readAccepted&&version===observation&&isSharedPowerLifecycleAdmitted(snapshot)){
          for(const [listener,minimum] of [...confirmations]){
            if(!ownsRead()||version!==observation)break;
            if(minimum>requested||!confirmations.delete(listener))continue;
            count('confirmationDelivered');record('confirmation','confirmed',snapshot,requested);
            notify(listener,snapshot!);
          }
          // Ordinary publication already delivered a changed fact or scope.
          // Only an accepted same-scope read can wake a retained recheck here.
          const checked=snapshot;
          if(checked&&same(prior,checked)&&prior?.observedNotificationSequence===checked.observedNotificationSequence){
            for(const [subscription,listener] of [...readyRecheckListeners]){
              // A reentrant publication can replace the cached object without
              // changing the scope confirmed by this read.
              const current=snapshot;
              if(!ownsRead()||version!==observation||!current||!same(checked,current)||
                checked.observedNotificationSequence!==current.observedNotificationSequence||checked.suspendSerial!==current.suspendSerial)break;
              if(listeners.has(subscription)&&(!suspendListeners.has(subscription)||prior?.suspendSerial===checked.suspendSerial))notify(listener,current);
            }
          }
        }
      }).catch(()=>{if(ownsRead()){count('readFailed');record('read','failed',snapshot,requested);}
        /* Retain known facts; retry only this outstanding read demand. */}).finally(()=>{
        if(!ownsRead())return;
        inFlight=null;
        const again=followup;followup=false;
        if(!needsRead())return;
        if(again&&readAccepted&&isSharedPowerLifecycleAdmitted(snapshot))queueRead();
        else armRetry();
      });
    });
  }
  function requestSharedPowerLifecycleReconcile(_reason:string):void {
    if(!active())return;
    reconcileVersion=++requestVersion;reconcileDemand=true;
    // Ordinary demand must preserve the one retry that already owns a delay.
    if(retryTimer===null)queueRead();
  }
  // An existing direct power.lifecycle read may see an ingress edge before
  // the broadcast. Fold its ordered receipt into this same authority.
  function beginSharedPowerLifecycleReadback({confirmReady=false}:{confirmReady?:boolean}={}) {
    const owner=epoch,requested=requestVersion;
    return (value:unknown):void=>{
      if(owner!==epoch||!active())return;
      const current=decode(value);
      if(!current||current.known===undefined||current.publicationRevision===undefined||
         current.suspendSerial===undefined||current.observedNotificationSequence===undefined||!coversReadbackFloor(current))return;
      if(current.publicationRevision<publicationRevision||current.observedNotificationSequence<notificationSequence||
         current.generation<(snapshot?.generation??0)||current.suspendSerial<(snapshot?.suspendSerial??-1))return;
      if(confirmReady&&isSharedPowerLifecycleAdmitted(current)){
        // A preflight may revoke the previous scope, but only the common read
        // or an authoritative event can admit the replacement.
        if(current.known!==true||!snapshot)return;
        const beforeFence=advanceReadbackFloor(current.generation,current.suspendSerial,current.observedNotificationSequence);
        if(beforeFence&&publishScopedFence(beforeFence))hintOnce('direct-ready:scope:'+current.generation+':'+current.suspendSerial+':'+current.observedNotificationSequence);
        return;
      }
      if(snapshot&&current.generation===snapshot.generation&&current.suspendSerial===snapshot.suspendSerial&&
         current.observedNotificationSequence===notificationSequence&&current.publicationRevision===publicationRevision){
        // Native can finish game recovery without a new power publication. A
        // same-scope receipt may advance that fact, but cannot renew admission.
        const gameRevision=current.gameWakeRecoveryRevision??0;
        if(gameRevision>(snapshot.gameWakeRecoveryRevision??0)&&
          isSharedPowerLifecycleAdmitted(scopedPowerView(snapshot))&&isSharedPowerLifecycleAdmitted(current))
          publish({...snapshot,gameWakeRecoveryRevision:gameRevision});
        return;
      }
      ++observation;
      const outcome=acceptAuthority(value,'direct-readback',requested);
      if(owner!==epoch||!active())return;
      record('direct-readback',outcome,value,requested);
      if(needsRead()&&!queued){if(isSharedPowerLifecycleAdmitted(snapshot)&&retryTimer===null)queueRead();else armRetry();}
    };
  }
  // One-shot confirmation from a read started after this request, never cache
  // replay or an event. The caller retains its ordinary source subscription.
  function requestSharedPowerLifecycleReadyConfirmation(listener:Listener):()=>void {
    if(!active())return()=>{};
    const subscription:Listener=state=>listener(state);
    confirmations.set(subscription,++requestVersion);
    if(retryTimer===null)queueRead();
    return()=>{
      if(!confirmations.delete(subscription)||needsRead())return;
      followup=false;retryStep=0;cancelRetry();
    };
  }
  function hintOnce(key:string) {
    if(hints.has(key))return;
    if(hints.size>=32)hints.delete(hints.values().next().value!);
    hints.add(key);++observation;requestSharedPowerLifecycleReconcile(key);
  }
  function advanceReadbackFloor(generation:number,suspendSerial:number,sequence:number) {
    const advancesFloor=!readbackFloor||generation>readbackFloor.generation||
      suspendSerial>readbackFloor.suspendSerial||sequence>readbackFloor.notificationSequence;
    if(!advancesFloor||snapshot&&generation<=snapshot.generation&&
      suspendSerial<=(snapshot.suspendSerial??-1)&&sequence<=notificationSequence)return null;
    const beforeFence=snapshot&&scopedPowerView(snapshot);
    const replacesSuspend=readbackFloor!==null&&(generation>readbackFloor.generation||suspendSerial>readbackFloor.suspendSerial);
    readbackFloor={generation,suspendSerial,notificationSequence:sequence};
    // A new suspend does not inherit a pending scope's retry delay.
    if(replacesSuspend){retryStep=0;cancelRetry();}
    retireRead();
    return beforeFence;
  }
  function publishScopedFence(beforeFence:SharedPowerLifecycleSnapshot|null):boolean {
    const fenced=beforeFence&&scopedPowerView(beforeFence);
    if(!fenced||fenced===beforeFence)return true;
    const floor=readbackFloor,owner=epoch;
    publish(fenced,beforeFence);
    return active()&&owner===epoch&&floor===readbackFloor;
  }
  function observe(name:string,phase:SharedPowerLifecycleSnapshot['phase'],value:unknown) {
    if(!active())return;
    record('compatibility-event','hint',value);
    const raw=value&&typeof value==='object'?value as {readbackScope?:{suspendSerial?:unknown;notificationSequence?:unknown}}:null;
    const state=decode(raw?{...raw,phase}:null);
    if(!state){hintOnce(name+':untagged');return;}
    const scope=raw?.readbackScope;
    if(ordinal(scope?.suspendSerial)&&ordinal(scope?.notificationSequence)){
      if((snapshot&&state.generation<snapshot.generation)||scope.notificationSequence<notificationSequence||
        scope.suspendSerial<(snapshot?.suspendSerial??0))return;
      if(readbackFloor&&(state.generation<readbackFloor.generation||scope.suspendSerial<readbackFloor.suspendSerial||
        scope.notificationSequence<readbackFloor.notificationSequence))return;
      const beforeFence=advanceReadbackFloor(state.generation,scope.suspendSerial,scope.notificationSequence);
      // Scope identifies read demand only. Keep the existing immediate denial
      // for a new non-Ready phase, and never publish a stamped Ready hint.
      if(phase==='ready'||state.generation<=committedGeneration||
        (snapshot?.generation===state.generation&&snapshot.phase===phase)){
        if(!publishScopedFence(beforeFence))return;
        hintOnce(`${name}:scope:${state.generation}:${scope.suspendSerial}:${scope.notificationSequence}`);return;
      }
    }
    delete state.fanWakeGeneration;
    delete state.fanStartupWakeGeneration;
    delete state.fanStartupWakeBoundary;
    // Raw phase hints cannot advance or discard the observed suspend scope.
    delete state.suspendSerial;
    if(snapshot?.suspendSerial!==undefined)state.suspendSerial=snapshot.suspendSerial;
    // A compatibility envelope cannot attest to the Native F5 receipt.
    delete state.resumeReady;
    delete state.gamepadUiRestart;
    if(state.publicationRevision!==undefined&&state.publicationRevision<publicationRevision){hintOnce(name+':old-publication');return;}
    if(snapshot&&state.generation<snapshot.generation||phase!=='ready'&&state.generation<=committedGeneration){
      hintOnce(name+':stale:'+state.generation);return;
    }
    // Once the current-fact protocol is seen, an old Ready notification cannot
    // grant permission over an unknown or newer native observation.
    if(authoritySeen&&phase==='ready'){hintOnce(name+':compat:'+state.generation);return;}
    if(snapshot&&state.generation===snapshot.generation&&state.phase===snapshot.phase)return;
    ++observation;publish(authoritySeen?{...state,known:false}:state);requestSharedPowerLifecycleReconcile(name);
  }
  function start() {
    started=true;const owner=epoch;
    stops.push(deps.listen('power.lifecycleChanged',value=>{
      if(owner!==epoch||!active())return;
      if((value as {fanWakeOnly?:boolean}|null)?.fanWakeOnly===true){
        const fan=decode(value);
        if(fan?.known!==true||!fan.fanWakeGeneration||fan.notificationPending!==false||
          !ordinal(fan.publicationRevision)||fan.publicationRevision<publicationRevision||
          !ordinal(fan.observedNotificationSequence)||fan.observedNotificationSequence<notificationSequence||
          (snapshot&&fan.generation<snapshot.generation))return;
        // Fan rechecks its own gate. Do not advance public authority or replay its consumers.
        // A merged suspend can retire the wake without changing the other ordering fields.
        const delivery=deliverySerial;
        for(const listener of [...fanWakeListeners]){
          if(owner!==epoch||delivery!==deliverySerial||!active())break;
          // A scoped hint can arrive during fanout before Native readback advances the cached scope.
          if(!coversReadbackFloor(fan))break;
          if(fan.observedNotificationSequence<notificationSequence)break;
          const suspendSerial=snapshot?.suspendSerial;
          if(suspendSerial!==undefined&&(fan.suspendSerial===undefined||fan.suspendSerial<suspendSerial))break;
          if(fanWakeListeners.has(listener))notify(listener,fan);
        }
        return;
      }
      count('authorityEvents');const outcome=acceptAuthority(value,'event');record('authority-event',outcome,value);
      if(outcome==='unknown')hintOnce('native-current-unknown:'+snapshot?.generation+':'+snapshot?.phase);
    }));
    for(const [name,phase] of [['power.suspending','suspending'],['power.resuming','resuming'],['power.resumed','ready']] as const){
      stops.push(deps.listen(name,value=>{if(owner===epoch&&active())observe(name,phase,value);}));
    }
    stops.push(deps.listen('power.resume-ready',value=>{
      if(owner!==epoch||!active())return;
      const generation=(value as {generation?:unknown}|null)?.generation;
      if(ordinal(generation)&&generation>0){
        if(snapshot&&generation<snapshot.generation)return;
        if(snapshot?.generation===generation&&snapshot.known===true&&snapshot.notificationPending!==true&&
           (snapshot.phase==='ready'||snapshot.phase==='resuming')&&snapshot.resumeReady===true)return;
        hintOnce('power.resume-ready:'+generation);
      }else hintOnce('power.resume-ready:untagged');
    }));
    for(const name of ['window.shown','window.restored','window.maximized','window.summoned','window.focus',
      'webview.recovering','gameMedia.resume-ready']){
      stops.push(deps.listen(name,()=>{
        if(owner!==epoch||!active())return;
        if(name==='webview.recovering'||name==='gameMedia.resume-ready')++observation;
        requestSharedPowerLifecycleReconcile(name);
      }));
    }
    requestSharedPowerLifecycleReconcile('subscribe');
  }
  function stop() {
    started=false;++epoch;++observation;cancelRetry();retireRead();
    for(const release of stops.splice(0)){try{release();}catch{/* Release every listener. */}}
    snapshot=null;readbackFloor=null;committedGeneration=0;publicationRevision=-1;notificationSequence=-1;authoritySeen=false;requestVersion=reconcileVersion=0;hints.clear();retryStep=0;
    confirmations.clear();queued=reconcileDemand=false;
    record('lifetime','stopped');
  }
  function onSharedPowerLifecycle(listener:Listener,options:{observeNotificationSequence?:boolean;onReadyRecheck?:Listener;observeFanWakeGeneration?:boolean;observeSuspendSerial?:boolean;onFanWakeRepublished?:Listener;observeGameWakeRecovery?:boolean}={}):()=>void {
    const subscription:Listener=state=>listener(state);
    // Optional scope cancellation shares this same source lifetime and read.
    // Ordinary business subscribers retain publication-metadata deduplication.
    if(options.observeNotificationSequence===true)notificationListeners.add(subscription);
    if(options.observeGameWakeRecovery===true)gameWakeRecoveryListeners.add(subscription);
    if(options.observeFanWakeGeneration===true)fanWakeListeners.add(subscription);
    if(options.onFanWakeRepublished)fanWakeRepublishListeners.set(subscription,options.onFanWakeRepublished);
    if(options.observeSuspendSerial===true)suspendListeners.add(subscription);
    if(options.onReadyRecheck)readyRecheckListeners.set(subscription,options.onReadyRecheck);
    listeners.add(subscription);
    if(snapshot)notify(subscription,snapshot);
    let ended=false;
    const release=()=>{if(ended)return;ended=true;gameWakeRecoveryListeners.delete(subscription);fanWakeListeners.delete(subscription);fanWakeRepublishListeners.delete(subscription);suspendListeners.delete(subscription);readyRecheckListeners.delete(subscription);notificationListeners.delete(subscription);listeners.delete(subscription);if(!listeners.size)stop();};
    if(!started){
      try{start();}catch(error){release();throw error;}
    }
    return release;
  }
  return {onSharedPowerLifecycle,requestSharedPowerLifecycleReconcile,requestSharedPowerLifecycleReadyConfirmation,beginSharedPowerLifecycleReadback,getSharedPowerLifecycle:copy,getSharedPowerLifecycleDiagnostics,sharedPowerWriteScope};
}

const shared=createSharedPowerLifecycleSource({
  read:signal=>invoke('power.lifecycle',{}, {timeoutMs:3000,signal}),
  listen:(name,listener)=>typeof window==='undefined'?()=>{}:on(name,listener),
});
export const onSharedPowerLifecycle=shared.onSharedPowerLifecycle;
export const requestSharedPowerLifecycleReconcile=shared.requestSharedPowerLifecycleReconcile;
export const requestSharedPowerLifecycleReadyConfirmation=shared.requestSharedPowerLifecycleReadyConfirmation;
export const getSharedPowerLifecycle=shared.getSharedPowerLifecycle;
export const getSharedPowerLifecycleDiagnostics=shared.getSharedPowerLifecycleDiagnostics;
export const beginSharedPowerLifecycleReadback=shared.beginSharedPowerLifecycleReadback;
