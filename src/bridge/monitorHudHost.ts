import {mouseModeState} from './mouseModeState';
import { readMonitorCpuName } from './monitorHardwareMetadata';
import { startMonitorNoticeHost } from './monitorNoticeHost';
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
  let desktopCloseSerial=0,applying=false,settling=false,settleRevision=0;
  const clock=createHudClock({nonce:()=>crypto.randomUUID(),now:Date.now,
    invoke:(command,args)=>invoke(command,args),
    setTimer:(fn,ms)=>window.setTimeout(fn,ms),clearTimer:id=>window.clearTimeout(id as number),
    pulse(snapshot){if(!ended&&source.users()>0)acceptPublishedMonitorSnapshot(snapshot);relay.advance();desktop.advance();},
    failed(reason){relay.clockUnavailable(reason);desktop.clockUnavailable(reason);},
  });
  const source=createHudSampleSource({cpuName:()=>cpuName,canReuseSnapshot:isOwnedMonitorDisplaySnapshot,subscribeGameLogo:subscribeMonitorGameLogo,subscribeSnapshot:subscribeMonitorHudSnapshot,subscribeTdp:subscribeMonitorTdpState,
    readOnce:readMonitorSnapshot,acquire(need){
      const releaseClock=clock.acquire();let release:()=>void;
      try{release=acquireMonitorDemand('ymcc-product-hud',need);}catch(error){releaseClock();throw error;}
      return()=>{release();releaseClock();};
    },now:Date.now});
  source.setClockEnabled(false);
  const relay=createSteamHudRelay({externalClock:true,acquireClock:clock.acquire,now:Date.now,session:()=>crypto.randomUUID(),sample:source.sample,acquire:source.acquire,
    reply:(connection,message)=>invoke<boolean>('deckySidebar.mirrorReply',{connection,message},{timeoutMs:3000}),
    setTimer:(fn,ms)=>window.setTimeout(fn,ms),clearTimer:id=>window.clearTimeout(id as number),statusChanged:()=>invalidate()});
  const desktop=createDesktopHudOwner({externalClock:true,acquireClock:clock.acquire,scope:'global',target:()=>null,policyGame:getPolicyGame,mouseMode:()=>mouseModeState.value,nonce:()=>crypto.randomUUID(),now:Date.now,
    sample:source.sample,acquire:source.acquire,invoke:(cmd,args)=>invoke(cmd,args as Record<string,unknown>), // owner bounds calls; retain late native descriptors
    observe(listener){const stops=['desktopHud.ready','desktopHud.painted','desktopHud.prepared','desktopHud.visibility','desktopHud.closed','desktopHud.unavailable','desktopHud.expired','desktopHud.placement','desktopHud.template'].map(name=>on<DesktopHudEvent>(name,event=>{if(name==='desktopHud.closed')++desktopCloseSerial;listener(name,event);}));return()=>{for(const stop of stops)stop();};},
    placementChanged:value=>{void saveMonitorHudPlacement(value).catch(error=>console.warn('监控位置保存失败',error));},
    templateChanged:(template,revision)=>{void saveMonitorHudTemplate(template,revision).catch(error=>console.warn('监控模板保存失败',error));},
    setTimer:(fn,ms)=>window.setTimeout(fn,ms),clearTimer:id=>window.clearTimeout(id as number),changed:()=>invalidate()});
  const router=createHudCarrierRouter({available:relay.available,visible:relay.visible,state:relay.state,setVisible:relay.setVisible},desktop,'desktop-global');
  async function refreshAvailability(){const at=++availabilityVersion;const available=ready&&!ended&&await router.available();if(at===availabilityVersion&&!ended)publishMonitorHudAvailability(available);}
  function invalidate(){
    ++version;if(!ready||ended)return;void refreshAvailability();
    if(queued)return;queued=true;queueMicrotask(()=>{queued=false;void reconcile();});
  }
  const contextKey=(route:string|null)=>route+'|'+(route==='steam'?JSON.stringify(relay.inspect().status?.game??null):'')+'|'+desktopCloseSerial;
  async function reconcile(){
    if(!ready||ended||restoring||applying||settling||!activePreferences?.enabled||activePreferences.output!=='ymcc')return;
    const route=await router.kind(activePreferences);if(!route)return;
    const key=contextKey(route); // Default desktop never needs a game identity.
    if(key===lastAttemptKey||desktop.inspect().opening)return;
    lastAttemptKey=key;restoring=true;const at=version;
    try{await monitorOutputCoordinator.apply(activePreferences);}finally{restoring=false;if(at!==version)invalidate();}
  }
  const offSteamPresence=on<{steamKnown:boolean;steamRunning:boolean}>('deckySidebar.updated',state=>{
    relay.noteSteamPresence(state.steamKnown===true?state.steamRunning:null);
  });
  const offClock=on<{nonce:string;snapshot:unknown}>('monitor.hudPulse',event=>clock.accept(event));
  const offExtension=registerDeckyMirrorExtension({prefix:'hud.',connected:peer=>relay.connected(peer),request:event=>relay.request(event)});
  const offAdapter=registerYmccHudOutputAdapter({global:true,globalFor:async prefs=>await router.kind(prefs)==='desktop',async available(prefs){return ready&&await router.available(prefs??activePreferences??undefined);},async visible(){return router.visible();},async state(){return router.active()==='desktop'&&desktop.waiting()?'armed':router.state();},setVisible:(show,prefs)=>{if(show)source.setDemand(hudNativeDemand(prefs.ymcc.fields));return router.setVisible(show,prefs);}});
  const offPreferences=onMonitorOutputPreferencesChanged(prefs=>{
    noticeHost.update(prefs);
    if(prefs.output==='ymcc')source.setDemand(hudNativeDemand(prefs.ymcc.fields));
    source.setClockEnabled(prefs.output==='ymcc' && prefs.enabled && prefs.ymcc.fields.clock === true);
    source.setGameLogoEnabled(prefs.output==='ymcc' && prefs.enabled && prefs.ymcc.underlay==='game-logo');
    clock.setFrameTime(prefs.output==='ymcc' && prefs.enabled && prefs.ymcc.fields.frametimeGraph === true);
    relay.updateAppearance(prefs);desktop.updateAppearance(prefs);
    if(activePreferences?.output==='ymcc'){
      const next=normalizeMonitorOutput(prefs);
      activePreferences={...activePreferences,mouseStatusEnabled:next.mouseStatusEnabled,ymcc:next.ymcc,ymccTemplate:next.ymccTemplate,ymccTemplateRevision:next.ymccTemplateRevision,ymccTemplates:next.ymccTemplates};
    }
  });
  const offApplying=onMonitorOutputApplying(value=>{
    applying=value;const at=++settleRevision;
    if(value){settling=false;return;}
    if(!ready||ended)return;
    // User-triggered switches own the queue too. Native close events from that
    // transaction must not enqueue stale preferences behind the user's intent.
    settling=true;
    void router.kind(activePreferences??undefined).then(route=>{
      if(at===settleRevision&&!applying&&!ended)lastAttemptKey=contextKey(route);
    }).catch(()=>{}).finally(()=>{
      if(at===settleRevision&&!ended){settling=false;invalidate();}
    });
  });
  const offApplied=onMonitorOutputApplied((prefs,result)=>{if(result.ok){activePreferences=normalizeMonitorOutput(prefs);source.setDemand(hudNativeDemand(activePreferences.ymcc.fields));source.setClockEnabled(prefs.output==='ymcc' && prefs.enabled && prefs.ymcc.fields.clock === true);source.setGameLogoEnabled(prefs.output==='ymcc' && prefs.enabled && prefs.ymcc.underlay==='game-logo');clock.setFrameTime(prefs.output==='ymcc' && prefs.enabled && prefs.ymcc.fields.frametimeGraph === true);if(result.applied!=='ymcc')lastAttemptKey='';}});
  return {
    async ready(){
      if(ended)return;activePreferences=await readMonitorOutputPreferences().catch(()=>null);if(ended)return;
      source.setDemand(hudNativeDemand(activePreferences?.ymcc.fields));
      source.setClockEnabled(activePreferences?.output==='ymcc' && activePreferences.enabled && activePreferences.ymcc.fields.clock === true);
      clock.setFrameTime(activePreferences?.output==='ymcc' && activePreferences.enabled && activePreferences.ymcc.fields.frametimeGraph === true);
      source.setGameLogoEnabled(activePreferences?.output==='ymcc' && activePreferences.enabled && activePreferences.ymcc.underlay==='game-logo');
      if(activePreferences)noticeHost.ready(activePreferences);
      ready=true;relay.hello();invalidate();
    },
    stop(){if(ended)return;ended=true;noticeHost.stop();ready=false;publishMonitorHudAvailability(false);offApplied();offApplying();offPreferences();offExtension();offSteamPresence();
      relay.dispose();void desktop.dispose().finally(()=>{offClock();clock.dispose();});offAdapter();},
    inspect:()=>({steam:relay.inspect(),desktop:desktop.inspect(),carrier:router.active(),sampleUsers:source.users(),clock:clock.inspect(),restoring}),
  };
}
