import {invoke,on} from './ipc';

export interface SharedPowerLifecycleSnapshot {
  phase:'ready'|'suspending'|'suspended'|'resuming';
  generation:number;
  inputReady?:boolean;
  hardwareWritesAllowed?:boolean;
  /** False denies admission even if the retained diagnostic phase is Ready. */
  known?:boolean;
  publicationRevision?:number;
  observedNotificationSequence?:number;
  notificationPending?:boolean;
}
// Public admission is separate from diagnostic Ready and controller presence.
export function isSharedPowerLifecycleAdmitted(state:SharedPowerLifecycleSnapshot|null|undefined):boolean {
  return !!state&&state.known!==false&&state.phase==='ready'&&
    state.hardwareWritesAllowed===true&&state.notificationPending!==true;
}

type Listener=(state:SharedPowerLifecycleSnapshot)=>void;
export interface SharedPowerLifecycleDependencies {
  read:()=>Promise<unknown>;
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
  const confirmations=new Map<Listener,number>();
  const retryDelays=[1000,2000,5000,15000,30000];
  const hints=new Set<string>();
  let snapshot:SharedPowerLifecycleSnapshot|null=null;
  let stops:Array<()=>void>=[],epoch=0,observation=0,committedGeneration=0;
  let publicationRevision=-1,authoritySeen=false,requestVersion=0;
  let notificationSequence=-1,deliverySerial=0;
  let started=false,inFlight=false,queued=false,followup=false,demand=false;
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
  const copy=()=>snapshot?{...snapshot}:null;
  // Export diagnostics from cached values only; this must never renew demand.
  function getSharedPowerLifecycleDiagnostics() {
    return {schemaVersion:1,source:'frontend-shared-power-lifecycle',active:active(),lifetimeEpoch:epoch,subscriberCount:listeners.size,
      snapshot:copy(),read:{inFlight,queued,followup,demand,retryStep,retryTimerActive:retryTimer!==null,pendingReadyConfirmations:confirmations.size},
      counters:{...counters},lastObservation:{...lastObservation}};
  }
  function notify(listener:Listener,value:SharedPowerLifecycleSnapshot) {
    try{listener({...value});}catch{/* One consumer cannot block other observers. */}
  }
  function same(a:SharedPowerLifecycleSnapshot|null,b:SharedPowerLifecycleSnapshot) {
    return !!a&&a.phase===b.phase&&a.generation===b.generation&&a.known===b.known&&
      a.inputReady===b.inputReady&&a.hardwareWritesAllowed===b.hardwareWritesAllowed&&
      a.notificationPending===b.notificationPending;
  }
  function ordinal(value:unknown):value is number {
    return typeof value==='number'&&Number.isSafeInteger(value)&&value>=0;
  }
  function publish(value:SharedPowerLifecycleSnapshot) {
    if(value.known!==false&&value.phase==='ready')committedGeneration=Math.max(committedGeneration,value.generation);
    const changed=!same(snapshot,value);
    const scopeChanged=!!snapshot&&snapshot.observedNotificationSequence!==value.observedNotificationSequence;
    // Always retain ordering metadata for the getter/replay, without turning
    // duplicate publications into another round of business callbacks.
    snapshot={...value};
    if(!changed&&(!scopeChanged||!notificationListeners.size))return;
    if(changed){count('published');hints.clear();}
    // A consumer can synchronously deliver a newer native fact or restart the
    // subscription lifetime. Never finish an older Ready fanout afterward.
    // Metadata-only observations do not retire delivery to the other clients.
    const delivery=changed?++deliverySerial:deliverySerial,owner=epoch;
    for(const listener of [...listeners]){
      if(owner!==epoch||delivery!==deliverySerial||!active())break;
      if(!changed&&!notificationListeners.has(listener))continue;
      if(listeners.has(listener))notify(listener,notificationListeners.has(listener)?snapshot!:value);
    }
  }
  function validAuthorityFields(state:Partial<SharedPowerLifecycleSnapshot>):boolean {
    if('known' in state&&typeof state.known!=='boolean')return false;
    if('publicationRevision' in state&&!ordinal(state.publicationRevision))return false;
    if('observedNotificationSequence' in state&&!ordinal(state.observedNotificationSequence))return false;
    for(const field of ['inputReady','hardwareWritesAllowed','notificationPending'] as const){
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
      ...(typeof state.inputReady==='boolean'?{inputReady:state.inputReady}:{}),
      ...(typeof state.hardwareWritesAllowed==='boolean'?{hardwareWritesAllowed:state.hardwareWritesAllowed}:{}),
      ...(typeof state.known==='boolean'?{known:state.known}:{}),
      ...(state.publicationRevision!==undefined?{publicationRevision:state.publicationRevision}:{}),
      ...(ordinal(state.observedNotificationSequence)?{observedNotificationSequence:state.observedNotificationSequence}:{}),
      ...(typeof state.notificationPending==='boolean'?{notificationPending:state.notificationPending}:{}),
    };
  }
  // Publications and reads describe native current state, including same-gen
  // corrections. Terminal-generation protection applies only to legacy hints.
  function acceptAuthority(value:unknown,event:boolean,readVersion=requestVersion):'known'|'unknown'|'stale' {
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
    const current=ordered?decoded:null;
    // Native may explicitly deny with metadata alone. This valid UNKNOWN still
    // fences older receipts, without granting or inventing a phase/generation.
    const metadataUnknown=ordered&&raw?.known===false&&!('generation' in raw)&&!('phase' in raw)&&validAuthorityFields(raw);
    const envelopeAccepted=!!current||metadataUnknown;
    if(current&&snapshot&&current.generation<snapshot.generation)return 'stale';
    const unknown=!current||current.known===false||((event||authoritySeen||revision>=0)&&current.known!==true);
    authoritySeen ||= event||raw?.known!==undefined||revision>=0;
    if(envelopeAccepted&&revision>=0)publicationRevision=revision;
    if(envelopeAccepted&&sequence>=0)notificationSequence=sequence;
    if(unknown){
      const previous=current??snapshot;
      const denied=previous?{...previous,known:false,inputReady:false,hardwareWritesAllowed:false,
        ...(envelopeAccepted&&revision>=0?{publicationRevision:revision}:{}),
        ...(envelopeAccepted&&sequence>=0?{observedNotificationSequence:sequence}:{})}:null;
      if(event&&(!denied||!same(snapshot,denied)))++observation;
      if(denied)publish(denied);
      demand=true;
      return 'unknown';
    }
    const admitted=isSharedPowerLifecycleAdmitted(current);
    const changed=!same(snapshot,current!);
    const freshFact=!snapshot||snapshot.generation!==current!.generation||snapshot.phase!==current!.phase||
      (snapshot.known!==true&&current!.known===true)||(!isSharedPowerLifecycleAdmitted(snapshot)&&admitted);
    if(event&&changed)++observation;
    // Metadata-only publications cannot refund a failed-read backoff. A real
    // new native phase/generation or confirmed recovery can settle its demand.
    if(admitted&&(!event||freshFact)){
      if(event||readVersion===requestVersion)demand=confirmations.size>0;
      if(event)followup=false;
      retryStep=0;cancelRetry();
    }else if(!admitted)demand=true;
    publish(current!);
    // A known transition still needs observation if its final Ready is lost.
    if(event&&demand){if(admitted)queueRead();else armRetry();}
    return 'known';
  }
  function cancelRetry() {
    ++retrySerial;
    if(retryTimer!==null)clock.clearTimeout(retryTimer);
    retryTimer=null;
  }
  function armRetry() {
    if(!active()||!demand||retryTimer!==null)return;
    const owner=epoch,serial=++retrySerial,delay=retryDelays[Math.min(retryStep++,retryDelays.length-1)];
    retryTimer=clock.setTimeout(()=>{
      if(owner!==epoch||serial!==retrySerial||!active())return;
      retryTimer=null;queueRead();
    },delay);
  }
  function queueRead() {
    if(!active()||queued||retryTimer!==null)return;
    if(inFlight){followup=true;return;}
    queued=true;const owner=epoch;
    queueMicrotask(()=>{
      if(owner!==epoch||!active())return;
      queued=false;if(!demand)return;
      inFlight=true;const version=observation,requested=requestVersion;
      count('readStarted');record('read','started',snapshot,requested);
      let readAccepted=false;
      // Capture the observation at the actual read start, not at queue time.
      let read:Promise<unknown>;
      try{read=Promise.resolve(deps.read());}catch(error){read=Promise.reject(error);}
      void read.then(value=>{
        if(owner!==epoch||!active())return;
        if(version!==observation){record('read','superseded',value,requested);if(demand)followup=true;return;}
        const outcome=acceptAuthority(value,false,requested);
        count(outcome==='known'?'readKnown':outcome==='unknown'?'readUnknown':'readStale');record('read',outcome,value,requested);
        readAccepted=outcome==='known';
        if(readAccepted&&owner===epoch&&active()&&version===observation&&isSharedPowerLifecycleAdmitted(snapshot)){
          for(const [listener,minimum] of [...confirmations]){
            if(owner!==epoch||!active()||version!==observation)break;
            if(minimum>requested||!confirmations.delete(listener))continue;
            count('confirmationDelivered');record('confirmation','confirmed',snapshot,requested);
            notify(listener,snapshot!);
          }
          if(owner===epoch&&version===observation&&requested===requestVersion&&confirmations.size===0)demand=false;
        }
      }).catch(()=>{if(owner===epoch&&active()){count('readFailed');record('read','failed',snapshot,requested);}
        /* Retain known facts; retry only this outstanding read demand. */}).finally(()=>{
        if(owner!==epoch||!active())return;
        inFlight=false;
        const again=followup;followup=false;
        if(!demand)return;
        // Error/unknown/stale replies must back off even when show/focus/hints
        // arrived during the read. Only an accepted read may run its follow-up.
        if(!readAccepted||!isSharedPowerLifecycleAdmitted(snapshot))armRetry();
        else if(again)queueRead();
        else armRetry();
      });
    });
  }
  function requestSharedPowerLifecycleReconcile(_reason:string):void {
    if(!active())return;
    ++requestVersion;demand=true;
    // Ordinary demand must preserve the one retry that already owns a delay.
    if(retryTimer===null)queueRead();
  }
  // One-shot confirmation from a read started after this request, never cache
  // replay or an event. The caller retains its ordinary source subscription.
  function requestSharedPowerLifecycleReadyConfirmation(listener:Listener):()=>void {
    if(!active())return()=>{};
    const subscription:Listener=state=>listener(state);
    confirmations.set(subscription,requestVersion+1);
    requestSharedPowerLifecycleReconcile('ready-confirmation');
    return()=>{confirmations.delete(subscription);};
  }
  function hintOnce(key:string) {
    if(hints.has(key))return;
    if(hints.size>=32)hints.delete(hints.values().next().value!);
    hints.add(key);++observation;requestSharedPowerLifecycleReconcile(key);
  }
  function observe(name:string,phase:SharedPowerLifecycleSnapshot['phase'],value:unknown) {
    if(!active())return;
    record('compatibility-event','hint',value);
    const state=decode(value&&typeof value==='object'?{...value,phase}:null);
    if(!state){hintOnce(name+':untagged');return;}
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
      count('authorityEvents');const outcome=acceptAuthority(value,true);record('authority-event',outcome,value);
      if(outcome==='unknown')hintOnce('native-current-unknown:'+snapshot?.generation+':'+snapshot?.phase);
    }));
    for(const [name,phase] of [['power.suspending','suspending'],['power.resuming','resuming'],['power.resumed','ready']] as const){
      stops.push(deps.listen(name,value=>{if(owner===epoch&&active())observe(name,phase,value);}));
    }
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
    started=false;++epoch;++observation;cancelRetry();
    for(const release of stops.splice(0)){try{release();}catch{/* Release every listener. */}}
    snapshot=null;committedGeneration=0;publicationRevision=-1;notificationSequence=-1;authoritySeen=false;requestVersion=0;hints.clear();retryStep=0;
    confirmations.clear();inFlight=queued=followup=demand=false;
    record('lifetime','stopped');
  }
  function onSharedPowerLifecycle(listener:Listener,options:{observeNotificationSequence?:boolean}={}):()=>void {
    const subscription:Listener=state=>listener(state);
    // Optional scope cancellation shares this same source lifetime and read.
    // Ordinary business subscribers retain publication-metadata deduplication.
    if(options.observeNotificationSequence===true)notificationListeners.add(subscription);
    listeners.add(subscription);
    if(snapshot)notify(subscription,snapshot);
    if(!started){
      try{start();}catch(error){notificationListeners.delete(subscription);listeners.delete(subscription);if(!listeners.size)stop();throw error;}
    }
    let ended=false;
    return()=>{if(ended)return;ended=true;notificationListeners.delete(subscription);listeners.delete(subscription);if(!listeners.size)stop();};
  }
  return {onSharedPowerLifecycle,requestSharedPowerLifecycleReconcile,requestSharedPowerLifecycleReadyConfirmation,getSharedPowerLifecycle:copy,getSharedPowerLifecycleDiagnostics};
}

const shared=createSharedPowerLifecycleSource({
  read:()=>invoke('power.lifecycle',{}, {timeoutMs:3000}),
  listen:(name,listener)=>typeof window==='undefined'?()=>{}:on(name,listener),
});
export const onSharedPowerLifecycle=shared.onSharedPowerLifecycle;
export const requestSharedPowerLifecycleReconcile=shared.requestSharedPowerLifecycleReconcile;
export const requestSharedPowerLifecycleReadyConfirmation=shared.requestSharedPowerLifecycleReadyConfirmation;
export const getSharedPowerLifecycle=shared.getSharedPowerLifecycle;
export const getSharedPowerLifecycleDiagnostics=shared.getSharedPowerLifecycleDiagnostics;
