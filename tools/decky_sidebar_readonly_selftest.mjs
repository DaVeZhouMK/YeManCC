import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import {load as loadSource} from './fixtures/decky_game_memory_store.mjs';
const steamContext=loadSource(fs.readFileSync(path.join(process.cwd(),'decky-plugin/src/steamRunningContext.ts'),'utf8'),{});const ReadOnlyMirrorRelay=loadSource(fs.readFileSync(path.join(process.cwd(),'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':steamContext}).ReadOnlyMirrorRelay;
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const runId='a'.repeat(32),peer={connection:'1',runId,connected:true};const cases=[];const flush=async()=>{for(let i=0;i<100;i++)await Promise.resolve();await new Promise(resolve=>setImmediate(resolve));await new Promise(resolve=>setImmediate(resolve));};
const selected=new Set();for(let i=2;i<process.argv.length;i+=2){const name=process.argv[i+1];assert(process.argv[i]==='--case'&&name&&!selected.has(name),'Use --case with a unique exact name');selected.add(name);}
async function check(name,fn){if(!selected.size||selected.has(name)){await fn();cases.push(name);}}
function relayFixture(){let reads=0,observes=0;const replies=[];let invalidate=()=>{};const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;return {generation:1,ready:true,game:null,fan:{supported:false,enabled:false,preset:'',choices:[]},actions:{performance:false}};},observe:fn=>{observes++;invalidate=fn;return()=>{};},reply:async(connection,message)=>{replies.push({connection,message});return true;}});return {relay,replies,reads:()=>reads,observes:()=>observes,invalidate:()=>invalidate()};}
await check('relay waits for authenticated demand',async()=>{// Compilation reuse must not reuse a context, module, dependency, globals or a changed source.
const text='globalThis.loadCounter=(globalThis.loadCounter??0)+1;module.exports={marker:globalThis.loadCounter,value:fixtureValue,dep:require("fixture-dep"),count:0};';
const depA={},depB={},first=loadSource(text,{'fixture-dep':depA},{fixtureValue:'first'}),second=loadSource(text,{'fixture-dep':depB},{fixtureValue:'second'});
assert.notEqual(first,second);assert.equal(first.marker,1);assert.equal(second.marker,1);assert.equal(first.value,'first');assert.equal(second.value,'second');assert.equal(first.dep,depA);assert.equal(second.dep,depB);first.count=7;assert.equal(second.count,0);
const changed=loadSource(text.replace('count:0','count:9'),{'fixture-dep':depB},{fixtureValue:'changed'});assert.equal(changed.count,9);assert.equal(changed.marker,1);
const f=relayFixture();assert.equal(f.reads(),0);f.relay.connected(peer);assert.equal(f.reads(),0);await f.relay.request({...peer,request:{command:'snapshot',runId}});assert.equal(f.reads(),1);assert.equal(f.observes(),1);f.relay.dispose();});
await check('relay coalesces source invalidations and does not poll',async()=>{const f=relayFixture();f.relay.connected(peer);await f.relay.request({...peer,request:{command:'snapshot',runId}});f.invalidate();f.invalidate();await flush();assert.equal(f.reads(),2);f.relay.dispose();});
await check('relay rejects unknown and mutation commands in read-only provider',async()=>{const f=relayFixture();f.relay.connected(peer);for(const command of ['shell.run','performance.setControls'])await f.relay.request({...peer,request:{command,runId,id:command,clientId:'c',sequence:1,args:command==='performance.setControls'?{patch:{tdp:20}}:{}}});assert.equal(f.reads(),0);assert(f.replies.every(x=>x.message.ok===false));f.relay.dispose();});
async function hostFixture(){const f=await createMirrorHostFixture();f.ready();const read=async()=>{const before=f.replies.length;f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return f.replies.slice(before).filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;};return {f,read};}
await check('host read is side-effect free and exposes only continuous performance controls',async()=>{const {f,read}=await hostFixture();const s=await read();assert.equal(s.ready,true);assert(s.performance);assert(!Object.hasOwn(s.game.fields,'acMode'));assert(!Object.hasOwn(s.game.fields,'dcMode'));assert(!Object.hasOwn(s.game.fields,'corePolicyMode'));assert(!Object.hasOwn(s.game.fields,'hyperThreadPolicy'));assert.equal(f.counts.saves,0);f.stop();
 // Real schedule + frame service + host, with only native/storage ports in memory.
 const {createPerformanceMemoryFixture,evaluatePerformanceModule}=await import('./fixtures/performance_memory_fixture.mjs');
 const {createRequire}=await import('node:module');const require=createRequire(path.join(process.cwd(),'package.json'));
 const settingsModel=evaluatePerformanceModule(require('esbuild').buildSync({entryPoints:['src/bridge/settingsRepository.ts'],bundle:true,write:false,platform:'node',format:'cjs',external:['vue']}).outputFiles[0].text,{'vue':require('vue')},{},'js');
 const frameSource=fs.readFileSync('src/bridge/frameRateLimits.ts','utf8'),frameModel=evaluatePerformanceModule(fs.readFileSync('src/bridge/frameRateModel.ts','utf8'));
 const build=(raw,extra={})=>{
  const ports={...extra,initialSettings:settingsModel.normalizeSettings(raw)};
  const observations={sectionReads:0,frameCalls:0,customReads:0};
  ports.beforeCustomRead=async p=>{observations.customReads++;await extra.beforeCustomRead?.(p);};
  ports.frameRateService=({repository,targets,gameDetection,yeman,powerSource,loadGameCustomConfig})=>{const actual=evaluatePerformanceModule(frameSource,{
   './frameRateModel':frameModel,'./settingsRepository':{...repository,
    readSettingsSection:async key=>{observations.sectionReads++;await extra.beforeFrameRead?.(key);return repository.readSettingsSection(key);},
    saveSettingsSection:async(key,patch,generation)=>{repository.assertSettingsGeneration(generation);return repository.updateSettings(next=>{repository.assertSettingsGeneration(generation);next[key]={...next[key],...structuredClone(patch)};});}},
   './gamePolicyTarget':{...targets,isPolicyGameInitialized:()=>true},'./gamedetect':gameDetection,'./yeman':yeman,'./powerSource':{powerSourceMode:powerSource},
   './performanceSchedule':{loadGameCustomConfig},'./api':{powerLifecycle:{get:async()=>({phase:'ready',hardwareWritesAllowed:true})}},
   './autofloat':{isContinuousFloat:()=>false,getFloatInfo:()=>({enabled:false}),setFloatRtssLinked(){},setFloatTarget:async()=>{throw Error('Unexpected hardware write');}}
  });return {...actual,loadGlobalFrameRates:(...args)=>{observations.frameCalls++;return actual.loadGlobalFrameRates(...args);}};};
  const owner=createPerformanceMemoryFixture({ports,gameActive:extra.gameActive===true});
  return {owner,ports,observations};
 };
 const display=owner=>owner.schedule.getContinuousPerformanceSnapshot('effective',undefined,undefined,'read-only');
 // RETIRED_HOST_OBSERVER_BEGIN
 {
  const {models}=await import('./fixtures/performance_memory_fixture.mjs');const observerMeasurements=[];
  for(const mode of ['schedule','power','monitor','controller']){
   let configFails=false,holdMonitor=false,monitorEntered=false,releaseMonitor;const gate=new Promise(resolve=>releaseMonitor=resolve);
   const {owner}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:{version:1,ac:models.defaultContinuousBank('ac'),dc:models.defaultContinuousBank('dc')}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive:true,beforeSettingsLoad:async()=>{if(configFails)throw Error('inert-settings-root-failure');}});
   const seed=await owner.schedule.loadPerformanceSchedule(),scheduleObservers=[],eventObservers=[],events=new Map();
   const performanceOwner={...owner.schedule,onPerformanceScheduleChanged:callback=>{const row={callback,active:true};scheduleObservers.push(row);const stop=owner.schedule.onPerformanceScheduleChanged(callback);return()=>{row.active=false;stop();};}};
   const eventSource={on:(event,callback)=>{let set=events.get(event);if(!set)events.set(event,set=new Set());set.add(callback);const row={event,callback,active:true};eventObservers.push(row);return()=>{row.active=false;set.delete(callback);if(!set.size)events.delete(event);};},emit:(event,message)=>{for(const callback of [...events.get(event)??[]])callback(message);}};
   const f=await createMirrorHostFixture({performanceOwner,frameOwner:owner.frameRateService,eventSource,readHooks:{monitor:async()=>{if(holdMonitor&&!monitorEntered){monitorEntered=true;await gate;}}}});f.state.game=structuredClone(owner.game);
   const latest=()=>f.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;
   const demand=async subscribe=>{f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe}});await flush();};
   const rawController={virtualOutput:{state:'active',requestedPersona:'steamdeck',boundPersona:'steamdeck',hostAlive:true,hostEpoch:1,prepared:true,neutralized:true,frameAccepted:true,sourceLive:true,sourceCurrent:true,steamRunning:true,startProtection:{locked:true,requestId:91,phase:'pending',persona:'steamdeck',remainingMs:1000,elapsedMs:0,maxMs:30000,settleMs:10000}}};
   try{
    f.ready();await demand(true);assert.equal(latest().ready,true);const oldSchedule=scheduleObservers.at(-1),oldEvent=eventObservers.find(row=>row.event===(mode==='power'?'power.pending':mode==='controller'?'gamepad.state':'test.monitorChanged'));
    assert(oldSchedule.active);if(mode!=='schedule')assert(oldEvent?.active);await demand(false);assert.equal(oldSchedule.active,false);if(mode!=='schedule')assert.equal(oldEvent.active,false);
    if(mode==='schedule'){
     holdMonitor=true;await demand(true);assert.equal(monitorEntered,true);oldSchedule.callback(structuredClone(seed));holdMonitor=false;releaseMonitor();await flush();let view=latest();
     console.log(JSON.stringify({retiredHostObserved:{mode,ready:view?.ready,powerReads:f.counts.power,recoveryTimers:f.controllerTimerCount()}}));
     assert.equal(view?.ready,true,'Retired schedule callback cannot replace a reopened read source');assert.equal(f.controllerTimerCount(),0);
     const before=f.counts.power;scheduleObservers.at(-1).callback(structuredClone(seed));await flush();assert.equal(latest().ready,true);assert.equal(f.counts.power,before+1,'Current schedule observer still refreshes once');
     observerMeasurements.push({mode,oldCallbackIgnored:true,currentCallbackReads:1,oldSourceFailure:false});
    }else{
     configFails=mode!=='controller';await demand(true);if(configFails)for(const delay of [250,1000,3000,10000]){f.advanceController(delay);await flush();}
     assert.equal(latest().ready,true);assert.equal(f.controllerTimerCount(),configFails?1:0);const powerBefore=f.counts.power;
     const payload=mode==='monitor'?{...structuredClone(f.state.monitor),enabled:false}:mode==='controller'?rawController:{};
     oldEvent.callback(structuredClone(payload));await flush();const afterOld=f.counts.power;f.advanceController(250);await flush();
     console.log(JSON.stringify({retiredHostObserved:{mode,powerBefore,afterOld,powerAfter250:f.counts.power,recoveryTimers:f.controllerTimerCount(),ready:latest()?.ready}}));
     assert.equal(f.counts.power,powerBefore,'Retired '+mode+' callback cannot start a current read');assert.equal(f.controllerTimerCount(),configFails?1:0,'Retired callback cannot rearm current recovery or controller timers');
     for(let i=0;i<100;i++)oldEvent.callback(structuredClone(payload));await flush();assert.equal(f.counts.power,powerBefore,'Repeated retired notifications cannot bypass the new demand');
     if(mode==='controller'){
      f.state.gamepadState=structuredClone(rawController);f.emit('gamepad.state',f.state.gamepadState);await flush();assert.equal(f.counts.power,powerBefore+1);assert.equal(latest().virtualController.locked,true);assert.equal(f.controllerTimerCount(),1);f.advanceController(1000);await flush();assert.equal(latest().virtualController.locked,false);assert.equal(f.controllerTimerCount(),0);
     }else{
      f.state.monitor={...f.state.monitor,enabled:false};f.monitorChanged();await flush();assert.equal(latest().monitor.enabled,false);assert.equal(latest().actions.monitor,true);
      const view=latest(),clientId='retired-'+mode,id=clientId+':1';f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'monitor.setEnabled',runId,id,clientId,sequence:1,generation:view.generation,revision:view.revision,args:{enabled:true}}});await flush();assert.equal(f.replies.find(row=>row.message.id===id)?.message.ok,true);assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);
      const beforeDeadline=f.counts.power;f.advanceController(29749);await flush();assert.equal(f.counts.power,beforeDeadline,'Retired notifications cannot refund or shorten the original capped fault deadline');configFails=false;f.advanceController(1);await flush();assert.equal(latest().actions.performance,true);assert.equal(f.controllerTimerCount(),0);
     }
     observerMeasurements.push({mode,oldCallbackIgnored:true,retiredStorm:100,extraRetiredReads:0,currentCallbackWorks:true,originalDeadlinePreserved:configFails===false,explicitMonitorWrites:mode==='controller'?0:1});
    }
    assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
   }finally{configFails=false;holdMonitor=false;releaseMonitor();f.stop();await flush();assert.equal(events.size,0);assert(scheduleObservers.every(row=>!row.active));assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);assert(Object.values(f.observerStats()).every(value=>value===0));}
  }
  console.log(JSON.stringify({retiredHostObserverMeasurements:observerMeasurements,scope:'Actual Host/Relay/full schedule/frame owners; original retired callback replay at memory dispatch seam, no physical source/runtime actions'}));
 }
 // RETIRED_HOST_OBSERVER_END
 // AUDIO_FAULT_COMMIT_BEGIN
 {
  const {models}=await import('./fixtures/performance_memory_fixture.mjs');
  let configFails=true,audioFails=false;
  const {owner}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:{version:1,ac:models.defaultContinuousBank('ac'),dc:models.defaultContinuousBank('dc')}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive:true,beforeSettingsLoad:async()=>{if(configFails)throw Error('inert-sustained-root-read-failure');}});
  const f=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService,readHooks:{fxSound:async()=>{if(audioFails)throw Error('inert-audio-read-failure');}}});f.state.game=structuredClone(owner.game);
  const latest=()=>f.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;
  let sequence=0;const command=async(view,enabled)=>{const id='audio-fault:'+ ++sequence;f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'audioOptimization.setEnabled',runId,id,clientId:'audio-fault',sequence,generation:view.generation,revision:view.revision,args:{enabled}}});await flush();return f.replies.find(row=>row.message.id===id)?.message;};
  try{
   f.ready();f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
   for(const delay of [250,1000,3000,10000]){f.advanceController(delay);await flush();}
   const old=latest(),powerReads=f.counts.power,audioReads=f.counts.fxSoundReads,attaches=f.counts.attaches;
   assert.equal(old.ready,true);assert.equal(old.actions.audioOptimization,true);assert.equal(old.audioOptimization.enabled,false);assert.equal(f.controllerTimerCount(),1);assert.equal(f.fxTimerCount(),1);
   f.state.fxSound={...f.state.fxSound,enabled:true,running:true};f.advanceFx();await flush();let view=latest();
   console.log(JSON.stringify({audioFaultBeforeEdge:{ready:old.ready,enabled:old.audioOptimization.enabled,powerReads,audioReads},audioFaultAfterEdge:{ready:view.ready,enabled:view.audioOptimization?.enabled,audioAction:view.actions.audioOptimization,monitorAction:view.actions.monitor,powerReads:f.counts.power,audioReads:f.counts.fxSoundReads}}));
   assert.equal(view.ready,true,'An accepted audio owner change cannot gray independent controls during a capped config fault');
   assert.equal(view.audioOptimization.enabled,true);assert.equal(view.actions.audioOptimization,true);assert.equal(view.actions.monitor,true);assert.equal(view.actions.performance,false);assert.equal(view.actions.frames,false);
   assert.equal(f.counts.power,powerReads+2);assert.equal(f.counts.fxSoundReads,audioReads+1,'The notification and display must share the original native audio flight');
   const stablePower=f.counts.power,stableAudio=f.counts.fxSoundReads,stableRevision=view.revision;
   for(let i=0;i<100;i++){f.advanceFx();await flush();}
   assert.equal(f.counts.power,stablePower,'Equivalent native audio observations cannot bypass the fault backoff');assert.equal(f.counts.fxSoundReads,stableAudio+100,'Only original audio observation reads remain');assert.equal(latest().revision,stableRevision);assert.equal(f.controllerTimerCount(),1);assert.equal(f.fxTimerCount(),1);
   const known=view;audioFails=true;const beforeUnknownReads=f.counts.fxSoundReads;f.advanceFx();await flush();view=latest();
   assert.equal(view.ready,true);assert.equal(view.audioOptimization.known,false);assert.equal(view.actions.audioOptimization,false);assert.equal(view.actions.monitor,true);assert.equal(f.counts.fxSoundReads,beforeUnknownReads+1);
   assert.equal((await command(known,false))?.ok,false);assert.equal(f.counts.fxSoundWrites,0,'Unknown current audio state rejects old delivered authority');
   audioFails=false;const beforeRecoveredReads=f.counts.fxSoundReads;f.advanceFx();await flush();view=latest();assert.equal(view.ready,true);assert.equal(view.actions.audioOptimization,true);assert.equal(view.audioOptimization.enabled,true);assert.equal(f.counts.fxSoundReads,beforeRecoveredReads+1);
   const reply=await command(view,false);assert.equal(reply?.ok,true);assert.equal(f.counts.fxSoundWrites,1,'Current explicit OFF reaches original audio owner once');assert.equal(latest().audioOptimization.enabled,false);
   const beforeDeadline=f.counts.power;f.advanceController(29999);await flush();assert.equal(f.counts.power,beforeDeadline,'Independent audio changes cannot renew or shorten the original fault deadline');
   configFails=false;f.advanceController(1);await flush();assert.equal(latest().actions.performance,true);assert.equal(f.controllerTimerCount(),0);assert.equal(f.counts.attaches,attaches);assert.equal(f.counts.fxSoundWrites,1);
   assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);assert.equal(f.counts.saves,0);assert.equal(f.counts.monitorSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
   f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();const hiddenPower=f.counts.power,hiddenAudio=f.counts.fxSoundReads,replies=f.replies.length;f.state.fxSound={...f.state.fxSound,enabled:true,running:true};f.advanceFx();await flush();assert.equal(f.counts.power,hiddenPower);assert.equal(f.counts.fxSoundReads,hiddenAudio);assert.equal(f.replies.length,replies);assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);
   console.log(JSON.stringify({audioFaultNotificationMeasurements:{equivalentObservations:100,extraHostReadsFromEquivalentObservations:0,extraAudioReadsBeyondOriginalObserver:0,unknownOnlyAudioDisabled:true,oldAuthorityRejected:true,explicitOffWrites:1,deadlineMs:30000,originalDeadlinePreserved:true,transportReattachDelta:0,hiddenReads:0,criticalWrites:0}}));
  }finally{configFails=false;audioFails=false;f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);assert(Object.values(f.observerStats()).every(value=>value===0));}
 }
 // AUDIO_FAULT_COMMIT_END
 // MONITOR_FAULT_COMMIT_BEGIN
 const {models:controlModels71}=await import('./fixtures/performance_memory_fixture.mjs');
 {
  let failing=true;
  const {owner}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:{version:1,ac:controlModels71.defaultContinuousBank('ac'),dc:controlModels71.defaultContinuousBank('dc')}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive:true,beforeSettingsLoad:async()=>{if(failing)throw Error('inert-sustained-settings-root-failure');}});
  const f=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});f.state.game=structuredClone(owner.game);
  const latest=()=>f.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;
  let seq=0;
  const command=async(view,enabled)=>{const id='fault-monitor:'+ ++seq;f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'monitor.setEnabled',runId,id,clientId:'fault-monitor',sequence:seq,generation:view.generation,revision:view.revision,args:{enabled}}});await flush();return f.replies.find(row=>row.message.id===id)?.message;};
  try{
   f.ready();f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
   for(const delay of [250,1000,3000,10000]){f.advanceController(delay);await flush();}
   const old=latest(),reads=f.counts.power,attaches=f.counts.attaches;
   assert.equal(old.ready,true);assert.equal(old.actions.monitor,true);assert.equal(old.actions.performance,false);assert.equal(f.controllerTimerCount(),1);
   f.state.monitor={...f.state.monitor,enabled:false};f.monitorChanged();await flush();
   let view=latest();
   console.log(JSON.stringify({monitorFaultBeforeCommit:{ready:old.ready,enabled:old.monitor.enabled,reads},monitorFaultAfterCommit:{ready:view.ready,enabled:view.monitor?.enabled,monitorAction:view.actions.monitor,reads:f.counts.power,recoveryTimers:f.controllerTimerCount()}}));
   assert.equal(view.ready,true,'A committed monitor enable edge must preserve independent controls during a capped config fault');
   assert.equal(view.monitor.enabled,false);assert.equal(view.actions.monitor,true);assert.equal(view.actions.audioOptimization,true);assert.equal(view.actions.performance,false);assert.equal(view.actions.frames,false);assert.equal(f.counts.power,reads+2,'One edge owns one partial batch with original final native fence');
   const afterEdgeReads=f.counts.power;
   for(let i=0;i<100;i++){f.monitorChanged();await flush();}
   assert.equal(f.counts.power,afterEdgeReads,'Equivalent committed notifications cannot bypass capped config fault backoff');assert.equal(latest().ready,true);assert.equal(latest().actions.monitor,true);assert.equal(f.controllerTimerCount(),1);
   assert.equal((await command(old,true))?.ok,false,'Old display authority cannot write after a monitor edge');assert.equal(f.counts.monitorSaves,0);
   assert.equal((await command(latest(),true))?.ok,true,'Current explicit monitor intent still reaches original owner');assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);
   const afterActionReads=f.counts.power;f.advanceController(29999);await flush();assert.equal(f.counts.power,afterActionReads,'Fresh monitor edge/action cannot refund the original capped fault deadline');
   failing=false;f.advanceController(1);await flush();view=latest();assert.equal(view.ready,true);assert.equal(view.actions.performance,true);assert.equal(f.controllerTimerCount(),0);assert.equal(f.counts.attaches,attaches,'Fault recovery reuses the same transport');
   assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
   f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();const hiddenReads=f.counts.power,replies=f.replies.length;
   f.state.monitor={...f.state.monitor,enabled:false};f.monitorChanged();await flush();assert.equal(f.counts.power,hiddenReads);assert.equal(f.replies.length,replies);
   assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);
   console.log(JSON.stringify({monitorFaultCommitMeasurements:{duplicates:100,extraReadsFromDuplicates:0,staleRejected:true,explicitMonitorWrites:1,configRecoveredSameTransport:true,originalCappedDeadlinePreserved:true,hiddenReads:0,criticalWrites:0}}));
  }finally{failing=false;f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);assert(Object.values(f.observerStats()).every(value=>value===0));}
 }
 // MONITOR_FAULT_COMMIT_END
 // DISPLAY_CUSTOM_COPY_BOUNDARY_BEGIN
 {
  const {models:copyModels}=await import('./fixtures/performance_memory_fixture.mjs');
  const copyMeasurements=[];
  for(const gameActive of [false,true]){
   let copies=0;
   const {owner}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:{version:1,ac:copyModels.defaultContinuousBank('ac'),dc:copyModels.defaultContinuousBank('dc')}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive});
   const performanceOwner={...owner.schedule,getGameCustomConfig:()=>{copies++;return owner.schedule.getGameCustomConfig();}};
   const host=await createMirrorHostFixture({performanceOwner,frameOwner:owner.frameRateService});host.state.game=structuredClone(owner.game);
   const request=async()=>{const before=host.replies.length;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return host.replies.slice(before).filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;};
   try{
    host.ready();const before=copies,first=await request(),coldCopies=copies-before;assert.equal(first?.ready,true);assert.equal(first.frames.pair.ac.fps,90);assert.equal(first.actions.performance,true);
    owner.settings.rtss.frameRates=frameModel.frameRatePair(null,105,45);const nextBefore=copies,next=await request();assert.equal(next.frames.pair.ac.fps,105);
    const nextDisplayCopies=copies-nextBefore;
    // A command must read the original owner again, never the prior display data.
    owner.settings.rtss.frameRates=frameModel.frameRatePair(null,110,50);const commandStart=host.replies.length;
    host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'frame.setField',runId,id:'copy-fresh:1',clientId:'copy-fresh',sequence:1,generation:next.generation,revision:next.revision,identity:next.game?.identity??'',args:{field:'acFps',value:'120'}}});await flush();
    const rejected=host.replies.slice(commandStart).find(row=>row.message.id==='copy-fresh:1')?.message;assert.equal(rejected?.ok,false,'Default frame action must reject the display receipt after external FPS changes');assert.equal(owner.effects.length,0);assert.equal(host.counts.frameApplies,0);
    const hidden=copies;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();assert.equal(copies,hidden);assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
    copyMeasurements.push({gameActive,coldCopies,nextDisplayCopies,externalFps:105,displayWrites:0});
   }finally{host.stop();await flush();assert.equal(host.controllerTimerCount(),0);assert.equal(host.fxTimerCount(),0);assert.equal(host.observerStats().watchers,0);}
  }
  console.log(JSON.stringify({profileCopyMeasurements:copyMeasurements,scope:'Host getter calls execute actual getGameCustomConfig structuredClone; not CPU percentage or heap-allocation measurement'}));
  assert.deepEqual(copyMeasurements.map(row=>row.coldCopies),[1,2],'One display shares its already-read profile with FPS and accepted final profile source');
  assert.deepEqual(copyMeasurements.map(row=>row.nextDisplayCopies),[1,2],'The next display keeps fresh owner reads and the original two live source fences');
 }
 // DISPLAY_CUSTOM_COPY_BOUNDARY_END
 // DISPLAY_CUSTOM_COPY_SOURCE_BOUNDARY_BEGIN
 {
  const {models:sourceModels72}=await import('./fixtures/performance_memory_fixture.mjs');
  let entered=false,release;const gate=new Promise(resolve=>release=resolve);
  const {owner}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:{version:1,ac:sourceModels72.defaultContinuousBank('ac'),dc:sourceModels72.defaultContinuousBank('dc')}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive:true});
  // Missing notifications never authorize publication from an old profile copy.
  const host=await createMirrorHostFixture({performanceOwner:{...owner.schedule,onGameCustomConfigChanged:()=>()=>{}},frameOwner:owner.frameRateService,readHooks:{monitor:async()=>{if(!entered){entered=true;await gate;}}}});host.state.game=structuredClone(owner.game);
  try{
   host.ready();host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();assert.equal(entered,true);
   const banks={version:1,ac:sourceModels72.defaultContinuousBank('ac'),dc:sourceModels72.defaultContinuousBank('dc')};banks.ac.tdp=32;
   await owner.setEntry({displayName:'Fresh dedicated source',enabled:true,controls:banks,ac:{tdpMax:32,fpsTarget:90},dc:{tdpMax:10,fpsTarget:40},padPersona:'follow',gyroOverride:'follow'});const setupWrites=owner.effects.length;
   release();await flush();const offered=()=>host.replies.filter(row=>row.message.type==='snapshot').map(row=>row.message.snapshot);
   assert.equal(offered().some(view=>view.ready),false,'A silently changed profile cannot publish its old frame/performance admission');
   host.advanceController(250);await flush();const current=offered().at(-1);assert.equal(current?.ready,true);assert.equal(current.performance.scope,'game');assert.equal(current.performance.controls.find(control=>control.key==='tdp').value,32);assert.equal(owner.effects.length,setupWrites,'Only explicit test setup writes; display recovery never saves');assert.equal(owner.hardwareWrites.length,0);
   console.log(JSON.stringify({profileCopySourceFence:{lostNotification:true,oldReadyDeliveries:0,recoveredDedicatedTdp:32,extraDisplayWrites:0}}));
  }finally{release();host.stop();await flush();assert.equal(host.controllerTimerCount(),0);assert.equal(host.fxTimerCount(),0);assert.equal(host.observerStats().watchers,0);}
 }
 // DISPLAY_CUSTOM_COPY_SOURCE_BOUNDARY_END

 // CONFIG_FAULT_INDEPENDENCE_BEGIN
 {
  const {models:controlModels71}=await import('./fixtures/performance_memory_fixture.mjs');
  const faultMeasurements=[];
  for(const mode of ['schedule','profile','independent-actions','generation','hide']){
   let calls=0,failing=true,releaseMonitor,monitorEntered=false;
   const monitorGate=new Promise(resolve=>releaseMonitor=resolve);
   const {owner,ports}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:{version:1,ac:controlModels71.defaultContinuousBank('ac'),dc:controlModels71.defaultContinuousBank('dc')}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive:true,beforeSettingsLoad:async()=>{calls++;if(failing&&(mode==='profile'?calls===2:mode==='independent-actions'||calls===1))throw Error('inert-settings-root-read-failure');}});
   const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService,readHooks:{monitor:async()=>{if((mode==='generation'||mode==='hide')&&!monitorEntered){monitorEntered=true;await monitorGate;}}}});host.state.game=structuredClone(owner.game);
   let sequence=0;
   const snapshots=()=>host.replies.filter(row=>row.message.type==='snapshot').map(row=>row.message.snapshot);
   const read=async()=>{host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return snapshots().at(-1);};
   const request=async(command,args,view)=>{const id='config-fault:'+ ++sequence,before=host.replies.length;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command,runId,id,clientId:'config-fault',sequence,generation:view.generation,revision:view.revision,identity:view.game?.identity??'',args}});await flush();return host.replies.slice(before).find(row=>row.message.id===id)?.message;};
   try{
    host.ready();const first=await read(),transportAttaches=host.counts.attaches;
    if(mode==='generation'||mode==='hide'){
     assert.equal(monitorEntered,true,'Independent monitor read must start after a configuration failure');
     if(mode==='generation'){
      host.state.power={...host.state.power,generation:2};ports.generation=2;failing=false;
      releaseMonitor();await flush();assert.equal(snapshots().some(view=>view.ready&&view.generation===1),false,'Changed native/settings generation cannot publish old independent admissions');
      assert.equal(snapshots().at(-1)?.ready,false);host.advanceController(250);await flush();assert.equal(snapshots().at(-1)?.generation,2);assert.equal(snapshots().at(-1)?.actions.performance,true);
      faultMeasurements.push({mode,oldReadyDeliveries:0,recoveredGeneration:2});
     }else{
      host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();releaseMonitor();await flush();assert.equal(snapshots().length,0,'Hidden partial transaction cannot deliver or rearm');assert.equal(host.controllerTimerCount(),0);assert.equal(host.fxTimerCount(),0);assert.equal(host.observerStats().watchers,0);
      faultMeasurements.push({mode,lateDeliveries:0,readRecoveryTimers:0});
     }
    }else{
     assert.equal(first?.ready,true,'Configuration read failure must preserve independent monitor/audio controls');assert.equal(first.monitor?.enabled,true);assert.equal(first.actions.monitor,true);assert.equal(first.audioOptimization?.known,true);assert.equal(first.actions.audioOptimization,true);assert.equal(first.performance,undefined);assert.equal(first.frames,undefined);assert.equal(first.actions.performance,false);assert.equal(first.actions.frames,false);assert.equal(first.actions.global,false);assert.equal(first.actions.game,false);
     const trace=host.diagnostics.find(row=>row.event==='snapshot-read-finished');assert.equal(trace.stage,'partial');assert.equal(trace.ok,false);assert.equal(trace.failureStage,mode==='profile'?'profile':'schedule');assert.equal(host.controllerTimerCount(),1,'Configuration fault uses existing single read-recovery timer');
     if(mode==='independent-actions'){
      const monitor=await request('monitor.setEnabled',{enabled:false},first);assert.equal(monitor?.ok,true);assert.equal(host.counts.monitorSaves,1);assert.equal(host.counts.monitorApplies,1);
      const current=snapshots().at(-1),audio=await request('audioOptimization.setEnabled',{enabled:true},current);assert.equal(audio?.ok,true);assert.equal(host.counts.fxSoundWrites,1);assert.equal(snapshots().at(-1)?.audioOptimization.enabled,true);
      const stale=snapshots().at(-1),saves=host.counts.monitorSaves;host.state.monitor={...host.state.monitor,enabled:true};const refused=await request('monitor.setEnabled',{enabled:false},stale);assert.equal(refused?.ok,false);assert.equal(host.counts.monitorSaves,saves,'Stale independent receipt still rechecks fresh original monitor state');
      faultMeasurements.push({mode,monitorSaves:1,monitorApplies:1,audioWrites:1,staleWriteRejected:true,criticalWrites:0});
     }else{
      failing=false;host.advanceController(250);await flush();const recovered=snapshots().at(-1);assert.equal(recovered?.ready,true);assert.equal(recovered.actions.performance,true);assert.equal(recovered.actions.frames,true);assert.equal(host.controllerTimerCount(),0);assert.equal(host.counts.attaches,transportAttaches,'Fault healing does not reconnect transport');
      faultMeasurements.push({mode,partialReady:true,recoveredInSamePeer:true,transportAttaches,readRecoveryTimers:0});
     }
    }
    assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);assert.equal(host.counts.saves,0);assert.equal(host.counts.performanceUpdates??0,0);
   }finally{releaseMonitor();host.stop();await flush();assert.equal(host.fxTimerCount(),0);assert.equal(host.controllerTimerCount(),0);assert.equal(host.observerStats().watchers,0);}
  }
  console.log(JSON.stringify({configFaultMeasurements:faultMeasurements,scope:'original Host + actual schedule/frame owners, memory settings/native ports',nativeInputOrHardwareAcceptance:false}));
 }
 // CONFIG_FAULT_INDEPENDENCE_END
 // DISPLAY_RTSS_READ_BOUNDARY_BEGIN
 {
  const {models:rtssModels}=await import('./fixtures/performance_memory_fixture.mjs');
  const monitorModel=evaluatePerformanceModule(require('esbuild').buildSync({entryPoints:['src/bridge/monitorHudModel.ts'],bundle:true,write:false,platform:'node',format:'cjs',external:['vue']}).outputFiles[0].text,{'vue':require('vue')},{},'js');
  const rtssTs=require('typescript'),monitorFile='src/bridge/monitorOutput.ts',monitorSource=fs.readFileSync(monitorFile,'utf8'),monitorTree=rtssTs.createSourceFile(monitorFile,monitorSource,rtssTs.ScriptTarget.Latest,true,rtssTs.ScriptKind.TS);
  const monitorFunction=monitorTree.statements.find(node=>rtssTs.isFunctionDeclaration(node)&&node.name?.text==='readMonitorOutputPreferences');assert(monitorFunction);
  const rtssReadMeasurements=[];
  for(const mode of ['normal','first-read-fails','source-change']){
   let rtssReads=0,release,held=false;const gate=new Promise(resolve=>release=resolve);
   const {owner}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:{version:1,ac:rtssModels.defaultContinuousBank('ac'),dc:rtssModels.defaultContinuousBank('dc')}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40),monitorOutput:{enabled:true,output:'ymcc'}}},{beforeSettingsRead:async key=>{if(key==='rtss'){rtssReads++;if(mode==='first-read-fails'&&rtssReads===1)throw Error('inert-first-RTSS-read-failure');}}});
   const monitorOwner=evaluatePerformanceModule(monitorFunction.getText(monitorTree),{}, {readSettingsSection:owner.settingsRepository.readSettingsSection,selectableMonitorOutput:monitorModel.selectableMonitorOutput});
   const rtssReader=async()=>{const value=await owner.settingsRepository.readSettingsSection('rtss');if(mode==='source-change'&&rtssReads===1){held=true;await gate;}return value;};
   const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService,rtssReader,monitorOwner});host.state.game=null;
   const request=async()=>{const before=host.replies.length;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return host.replies.slice(before).filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;};
   try{
    host.ready();
    if(mode==='source-change'){
     host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();assert.equal(held,true);
     owner.settings.rtss.frameRates=frameModel.frameRatePair(null,105,45);owner.settings.rtss.monitorOutput.enabled=false;host.monitorChanged();await flush();
     let monitorSettled=false,frameSettled=false;
     const ordinaryMonitor=monitorOwner.readMonitorOutputPreferences().then(value=>{monitorSettled=true;return value;});
     const ordinaryFrames=owner.frameRateService.loadGlobalFrameRates().then(value=>{frameSettled=true;return value;});await flush();
     assert.equal(monitorSettled,true,'Ordinary monitor read cannot wait for display data');assert.equal(frameSettled,true,'Ordinary FPS read cannot wait for display data');assert.equal((await ordinaryMonitor).enabled,false);assert.equal((await ordinaryFrames).ac.fps,105);
     release();await flush();const offered=host.replies.filter(row=>row.message.type==='snapshot'),view=offered.at(-1)?.message.snapshot;
     assert.equal(view?.ready,true);assert.equal(view.frames.pair.ac.fps,105);assert.equal(view.monitor.enabled,false);assert.equal(offered.length,1,'Retired display facts cannot be offered');assert.equal(rtssReads,4,'Two display epochs and two independent ordinary reads');
     rtssReadMeasurements.push({mode,displayRtssReads:2,ordinaryFreshReads:2,retiredDeliveries:0,fps:105,monitor:false,settingsWrites:0});
    }else{
     const first=await request();assert.equal(first?.ready,true);
     if(mode==='first-read-fails'){
      assert.equal(first.frames,undefined);assert.equal(first.performance,undefined);assert.equal(first.monitor?.enabled,true,'Monitor must retain its independent fresh read after the shared read fails');assert.equal(first.actions.monitor,true);assert.equal(rtssReads,2);
      rtssReadMeasurements.push({mode,firstReadFailed:true,monitorIndependentRecovery:true,rtssReads:2,framesUnavailable:true,performanceUnavailable:true,settingsWrites:0});
     }else{
      assert.equal(first.frames.pair.ac.fps,90);assert.equal(first.monitor.enabled,true);assert.equal(rtssReads,1,'FPS and monitor display share one successful current section read');
      owner.settings.rtss.frameRates=frameModel.frameRatePair(null,105,45);owner.settings.rtss.monitorOutput.enabled=false;
      const before=rtssReads,next=await request();assert.equal(next.frames.pair.ac.fps,105);assert.equal(next.monitor.enabled,false);assert.equal(rtssReads-before,1);
      const ordinaryBefore=rtssReads;assert.equal((await monitorOwner.readMonitorOutputPreferences()).enabled,false);assert.equal((await owner.frameRateService.loadGlobalFrameRates()).ac.fps,105);assert.equal(rtssReads-ordinaryBefore,2);
      rtssReadMeasurements.push({mode,coldRtssReads:1,laterFreshRtssReads:1,ordinaryFreshReads:2,fps:105,monitor:false,settingsWrites:0});
     }
    }
    const hidden=rtssReads;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();assert.equal(rtssReads,hidden);assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);assert.equal(host.fxTimerCount(),0);assert.equal(host.controllerTimerCount(),0);assert.equal(host.observerStats().watchers,0);
   }finally{release();host.stop();await flush();assert.equal(host.fxTimerCount(),0);assert.equal(host.controllerTimerCount(),0);assert.equal(host.observerStats().watchers,0);}
  }
  console.log(JSON.stringify({rtssReadMeasurements}));
 }
 // DISPLAY_RTSS_READ_BOUNDARY_END

 // DISPLAY_WARM_ORDER_BOUNDARY_BEGIN
 {
  const {models:controlModels}=await import('./fixtures/performance_memory_fixture.mjs');
  const banks={version:1,ac:controlModels.defaultContinuousBank('ac'),dc:controlModels.defaultContinuousBank('dc')};
  const {owner}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:banks},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}});
  const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});host.state.game=null;
  const readHost=async()=>{const start=host.replies.length;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return host.replies.slice(start).filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;};
  let held=false,entered=false,release;const gate=new Promise(resolve=>release=resolve),load=owner.settingsRepository.loadSettings,cap=owner.powerTerminals.getTdpExecutionCapability;let loads=0,caps=0;
  owner.settingsRepository.loadSettings=async()=>{loads++;if(held){entered=true;await gate;}return load();};
  owner.powerTerminals.getTdpExecutionCapability=async(...args)=>{caps++;return cap(...args);};
  try{
   host.ready();const first=await readHost();assert.equal(first.ready,true);const priorLoads=loads,priorCaps=caps;
   held=true;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
   assert.equal(entered,true,'Original actual settings read must be held');const capabilityStartedBeforeScheduleReleased=caps>priorCaps;
   console.log(JSON.stringify({warmOrderObservation:{capabilityStartedBeforeScheduleReleased,heldSettingsLoads:loads-priorLoads,capabilityStarts:caps-priorCaps}}));
   held=false;release();await flush();const latest=host.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;assert.equal(latest?.ready,true);
   assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
   assert.equal(capabilityStartedBeforeScheduleReleased,true,'Warm capability read must overlap the pending original schedule read');
  }finally{held=false;release();host.stop();await flush();assert.equal(host.fxTimerCount(),0);assert.equal(host.controllerTimerCount(),0);assert.equal(host.observerStats().watchers,0);}
 }

 // The original FPS promise may reject while schedule IO is still pending.
 {
  const {models:controlModels}=await import('./fixtures/performance_memory_fixture.mjs');const banks={version:1,ac:controlModels.defaultContinuousBank('ac'),dc:controlModels.defaultContinuousBank('dc')};
  const {owner}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:banks},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}});
  let release,entered=false;const gate=new Promise(resolve=>release=resolve),load=owner.settingsRepository.loadSettings;owner.settingsRepository.loadSettings=async()=>{entered=true;await gate;return load();};
  const unhandled=[];const listener=error=>unhandled.push(String(error));process.on('unhandledRejection',listener);
  const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:{...owner.frameRateService,loadGlobalFrameRates:()=>Promise.reject(Error('inert-early-FPS-read-failure'))}});host.state.game=null;
  try{host.ready();host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();assert.equal(entered,true);assert.deepEqual(unhandled,[],'Early owner rejection must be observed while schedule is pending');
   release();await flush();const view=host.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;assert.equal(view?.ready,true);assert.equal(view.frames,undefined);assert.equal(view.performance,undefined);assert.deepEqual(unhandled,[]);assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
   console.log(JSON.stringify({earlyReadFailureObservation:{unhandledRejections:0,affectedControlsUnavailable:true,settingsWrites:0}}));
  }finally{release();host.stop();await flush();process.removeListener('unhandledRejection',listener);assert.equal(host.fxTimerCount(),0);assert.equal(host.observerStats().watchers,0);}
 }
 // DISPLAY_WARM_ORDER_BOUNDARY_END

 // DISPLAY_ASYNC_SOURCE_BOUNDARY_BEGIN
 const {models:asyncModels}=await import('./fixtures/performance_memory_fixture.mjs');
 const asyncRaw=()=>({performanceSchedule:{version:2,configured:true,enabled:true,controls:{version:1,ac:asyncModels.defaultContinuousBank('ac'),dc:asyncModels.defaultContinuousBank('dc')}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}});
 {
  const {owner}=build(asyncRaw());const config=await owner.schedule.loadPerformanceSchedule(),custom=await owner.schedule.loadGameCustomConfig('read-only'),watts=config.controls.ac.tdp;
  const reading=owner.schedule.getContinuousPerformanceSnapshot('effective',undefined,undefined,'read-only',{generation:1,schedule:config,custom,globalFrameRates:Promise.resolve(frameModel.frameRatePair(null,90,40))});config.controls.ac.tdp=71;
  assert.equal((await reading).bank.tdp,watts,'A plain display value keeps its original immediate-copy boundary');
  let release;const fake=new Promise(resolve=>release=resolve);let settled=false;
  const action=owner.schedule.getContinuousPerformanceSnapshot('effective',undefined,undefined,'ensure',{generation:1,schedule:fake,custom:fake,globalFrameRates:Promise.resolve(frameModel.frameRatePair(null,10,10))}).then(value=>{settled=true;return value;});
  try{await flush();assert.equal(settled,true,'An ordinary action must not wait for supplied display promises');const view=await action;assert.equal(view.bank.tdp,watts);assert.equal(view.frameRates.ac.fps,90);}
  finally{release(config);await action.catch(()=>{});}
  assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);console.log(JSON.stringify({asyncCopyAndActionObservation:{plainCopiedImmediately:true,ordinaryActionIgnoredPendingDisplay:true,settingsWrites:0}}));
 }
 {
  const {owner,ports}=build(asyncRaw());const config=await owner.schedule.loadPerformanceSchedule(),custom=await owner.schedule.loadGameCustomConfig('read-only');let release;const pending=new Promise(resolve=>release=resolve);
  const reading=owner.schedule.getContinuousPerformanceSnapshot('effective',undefined,undefined,'read-only',{generation:1,schedule:pending,custom:Promise.resolve(custom),globalFrameRates:Promise.resolve(frameModel.frameRatePair(null,90,40))});
  ports.generation=2;release(config);await assert.rejects(reading);assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);console.log(JSON.stringify({asyncGenerationObservation:{changedDuringWait:true,oldResultRejected:true,settingsWrites:0}}));
 }
 {
  const {owner}=build(asyncRaw());let held=true,entered=false,release,loads=0;const gate=new Promise(resolve=>release=resolve),load=owner.settingsRepository.loadSettings;
  owner.settingsRepository.loadSettings=async()=>{loads++;if(held){entered=true;await gate;}return load();};
  const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});host.state.game=null;
  try{host.ready();host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();assert.equal(entered,true);
   host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();assert.equal(host.fxTimerCount(),0);assert.equal(host.observerStats().watchers,0);const hiddenReplies=host.replies.length;
   held=false;release();await flush();assert.equal(host.replies.slice(hiddenReplies).filter(row=>row.message.type==='snapshot').length,0,'A retired pending read cannot publish after hiding');const before=loads;
   host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();const fresh=host.replies.slice(hiddenReplies).filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;assert.equal(fresh?.ready,true);assert.equal(loads,before+1);assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
   console.log(JSON.stringify({asyncHiddenObservation:{retiredDeliveries:0,reopenFreshReads:1,settingsWrites:0}}));
  }finally{held=false;release();host.stop();await flush();assert.equal(host.fxTimerCount(),0);assert.equal(host.controllerTimerCount(),0);assert.equal(host.observerStats().watchers,0);}
 }
 // DISPLAY_ASYNC_SOURCE_BOUNDARY_END

 // DISPLAY_SCHEDULE_READ_BOUNDARY_BEGIN
 const scheduleReadMeasurements=[];
 const {models:scheduleModels}=await import('./fixtures/performance_memory_fixture.mjs');
 for(const template of [false,true])for(const gameActive of [false,true]){
  const {owner,ports}=build({...(template?{}:{performanceSchedule:{version:2,configured:true,enabled:true,controls:{version:1,ac:scheduleModels.defaultContinuousBank('ac'),dc:scheduleModels.defaultContinuousBank('dc')}}}),rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive});
  const originalLoad=owner.settingsRepository.loadSettings;let reads=0;
  owner.settingsRepository.loadSettings=async()=>{reads++;return originalLoad();};
  const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});host.state.game=owner.game;
  const readHost=async()=>{const start=host.replies.length;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return host.replies.slice(start).filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;};
  const tdp=view=>view.performance.controls.find(row=>row.key==='tdp')?.value;
  try{
   host.ready();const first=await readHost();assert.equal(first.ready,true);assert(first.performance);
   console.log(JSON.stringify({scheduleColdObservation:{template,gameActive,reads,tdp:tdp(first)}}));
   assert.equal(reads,(template?2:1)+(gameActive?1:0),'One shared schedule plus original custom-root preparation reads');
   const changed=structuredClone((await owner.schedule.loadPerformanceSchedule()).controls);changed.ac.tdp=44;owner.settings.performanceSchedule.controls=changed;
   const before=reads;const next=await readHost();assert.equal(next.ready,true);assert.equal(reads,before+1+(gameActive?1:0),'A later display keeps one fresh schedule plus original custom-root preparation');assert.equal(tdp(next),44,'A later display sees external schedule changes');
   // A supplied display value never replaces the ordinary action reader.
   const forged=structuredClone(await owner.schedule.loadPerformanceSchedule());forged.controls.ac.tdp=77;
   const prior=reads;const action=await owner.schedule.getContinuousPerformanceSnapshot('effective',undefined,undefined,'ensure',{generation:ports.generation??1,schedule:forged,globalFrameRates:Promise.resolve(frameModel.frameRatePair(null,10,10))});
   assert.equal(action.bank.tdp,44);assert.equal(reads,prior+2,'Default action keeps original schedule plus custom-config preparation reads');
   const generation=ports.generation??1;ports.generation=generation+1;
   await assert.rejects(owner.schedule.getContinuousPerformanceSnapshot('effective',undefined,undefined,'read-only',{generation,schedule:forged,globalFrameRates:Promise.resolve(frameModel.frameRatePair(null,10,10))}));
   assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
   const hidden=reads;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();assert.equal(reads,hidden);
   scheduleReadMeasurements.push({template,gameActive,coldSettingsReads:(template?2:1)+(gameActive?1:0),laterDisplayFreshSettingsReads:1+(gameActive?1:0),externalTdp:44,defaultActionFresh:true,oldGenerationRejected:true,displayWrites:0});
  }finally{host.stop();await flush();assert.equal(host.fxTimerCount(),0);assert.equal(host.controllerTimerCount(),0);assert.equal(host.observerStats().watchers,0);}
 }
 console.log(JSON.stringify({scheduleReadMeasurements}));

 {
  const banks={version:1,ac:scheduleModels.defaultContinuousBank('ac'),dc:scheduleModels.defaultContinuousBank('dc')},dedicated=structuredClone(banks);dedicated.ac.tdp=32;
  const {owner}=build({performanceSchedule:{version:2,configured:true,enabled:true,controls:banks},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive:true,initialCustom:{version:1,entries:{'game.exe':{enabled:true,displayName:'Memory Game',controls:dedicated}},rtss:{}}});
  const load=owner.settingsRepository.loadSettings;let reads=0;owner.settingsRepository.loadSettings=async()=>{reads++;return load();};
  const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});host.state.game=owner.game;
  const readHost=async()=>{const start=host.replies.length;host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return host.replies.slice(start).filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;};
  try{host.ready();const first=await readHost();assert.equal(first.ready,true);assert.equal(first.performance.scope,'game');assert.equal(first.performance.controls.find(v=>v.key==='tdp').value,32);assert.equal(reads,2);
   owner.settings.performanceSchedule.controls.ac.tdp=43;const next=await readHost();assert.equal(next.performance.controls.find(v=>v.key==='tdp').value,32);assert.equal(reads,4);assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
   console.log(JSON.stringify({dedicatedScheduleObservation:{coldSettingsReads:2,nextSettingsReads:2,dedicatedTdp:32,externalGlobalTdp:43,displayWrites:0}}));
  }finally{host.stop();await flush();assert.equal(host.fxTimerCount(),0);assert.equal(host.observerStats().watchers,0);}
 }
 // DISPLAY_SCHEDULE_READ_BOUNDARY_END


 // DISPLAY_CPU_READ_BOUNDARY_BEGIN
 const cpuReadMeasurements=[];
 {
  let reads=0;const {owner}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{actualCpuProfiles:true,beforeSettingsRead:async key=>{if(key==='cpu')reads++;}});
  const snapshot=await display(owner);assert(snapshot.controls);assert.equal(reads,0,'A complete modern display must not read unused legacy CPU profiles');assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
  await owner.schedule.getContinuousPerformanceSnapshot();assert.equal(reads,1,'Ordinary action keeps its original fresh CPU owner read');
  cpuReadMeasurements.push({modernGlobal:true,displayCpuReads:0,actionCpuReads:1,displayWrites:0});
 }
 {
  const legacy=(await import('./fixtures/decky_game_memory_store.mjs')).seed(),model=(await import('./fixtures/performance_memory_fixture.mjs')).models;
  legacy.entries['game.exe'].controls={version:1,ac:model.defaultContinuousBank('ac'),dc:model.defaultContinuousBank('dc')};
  let reads=0;const {owner}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive:true,initialCustom:legacy,actualCpuProfiles:true,beforeSettingsRead:async key=>{if(key==='cpu')reads++;}});
  const snapshot=await display(owner);assert.equal(snapshot.identity.scope,'dedicated');assert.equal(reads,0);assert.equal(owner.effects.length,0);
  cpuReadMeasurements.push({modernDedicated:true,displayCpuReads:0,displayWrites:0});
 }
 {
  let reads=0;const {owner}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{actualCpuProfiles:true,beforeSettingsRead:async key=>{if(key==='cpu'){reads++;throw Error('fixture-legacy-cpu-unavailable');}}});
  const snapshot=await display(owner);assert(snapshot.controls);assert.equal(reads,0);assert.equal(owner.effects.length,0);
  await assert.rejects(owner.schedule.getContinuousPerformanceSnapshot(),/fixture-legacy-cpu-unavailable/);assert.equal(reads,1);
  cpuReadMeasurements.push({unusedCpuFailureIsolated:true,displayCpuReads:0,ordinaryActionFailurePreserved:true});
 }
 {
  let reads=0;const {owner}=build({performanceSchedule:{configured:true,enabled:true,active:{ac:'eco',dc:'eco'},profiles:{ac:{eco:{cpuPreset:'turbo',tdpMax:35}},dc:{eco:{cpuPreset:'turbo',tdpMax:20}}}},cpu:{active:'turbo',profiles:{turbo:{acFreq:3300,dcFreq:2200,acAggr:61,dcAggr:42,acTurbo:true,dcTurbo:false}}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{actualCpuProfiles:true,beforeSettingsRead:async key=>{if(key==='cpu')reads++;}});
  const snapshot=await display(owner);assert.equal(reads,1,'Legacy global conversion still reads actual CPU profiles');assert.equal(snapshot.controls.ac.manual.frequencyMHz,3300);assert.equal(snapshot.controls.dc.manual.frequencyMHz,2200);assert.equal(snapshot.controls.ac.manual.aggressiveness,61);assert.equal(owner.effects.length,0);
  cpuReadMeasurements.push({legacyGlobal:true,displayCpuReads:1,acFrequency:3300,dcFrequency:2200});
 }
 {
  const legacy=(await import('./fixtures/decky_game_memory_store.mjs')).seed();
  let reads=0;const {owner}=build({cpu:{active:'balanced',profiles:{balanced:{acFreq:3100,dcFreq:2100,acAggr:61,dcAggr:42,acTurbo:true,dcTurbo:false}}},rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive:true,initialCustom:legacy,actualCpuProfiles:true,beforeSettingsRead:async key=>{if(key==='cpu')reads++;}});
  let snapshot=await display(owner);assert.equal(reads,1);assert.equal(snapshot.controls.ac.manual.frequencyMHz,3100);assert.equal(snapshot.controls.dc.manual.frequencyMHz,2100);
  const global=await owner.schedule.getContinuousPerformanceSnapshot('global',undefined,undefined,'read-only');assert.equal(global.identity.scope,'global');assert.equal(reads,1,'Explicit global view ignores an unrelated legacy dedicated entry');
  owner.settings.cpu.profiles.balanced.acFreq=3200;snapshot=await owner.schedule.getContinuousPerformanceSnapshot('dedicated',undefined,undefined,'read-only');assert.equal(reads,2);assert.equal(snapshot.controls.ac.manual.frequencyMHz,3200,'A later legacy display observes the actual updated CPU owner');assert.equal(owner.effects.length,0);
  cpuReadMeasurements.push({legacyDedicated:true,firstDisplayCpuReads:1,explicitGlobalExtraReads:0,nextDedicatedTotalReads:2,nextAcFrequency:3200});
 }
 console.log(JSON.stringify({cpuReadMeasurements}));
 // DISPLAY_CPU_READ_BOUNDARY_END


 // Cold display must not create/migrate a dedicated document merely to render.
 // The same complete original scheduler and FPS owner perform action migration.
 const coldCustomMeasurements=[];
 for(const gameActive of [false,true]){
  const legacy=(await import('./fixtures/decky_game_memory_store.mjs')).seed();
  const {owner}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)},gameCustom:legacy},{gameActive,customDocuments:[]});
  const before=JSON.stringify(owner.settings);
  const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});host.state.game=owner.game;
  try{host.ready();host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
   const view=host.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;assert(view?.ready);assert(view.performance);
   console.log(JSON.stringify({coldCustomObservation:{gameActive,effects:owner.effects,scope:view.performance.scope}}));
   assert.equal(owner.effects.length,0,'Decky cold display must not create or migrate game-custom files');assert.equal(owner.hardwareWrites.length,0);assert.equal(JSON.stringify(owner.settings),before);
   const direct=await display(owner);assert.equal(owner.effects.length,0,'Direct read-only performance display must not migrate game custom');
   assert.equal(owner.schedule.getGameCustomRecoveryState().persisted,false,'A display cannot claim a missing document was persisted');
   const action=await owner.schedule.getContinuousPerformanceSnapshot();
   assert.deepEqual(structuredClone(action.controls),structuredClone(direct.controls));
   assert.equal(owner.effects.filter(x=>x==='file-save').length,1,'Default original load still creates the dedicated document exactly once');
   assert.equal(owner.effects.filter(x=>x==='settings-save').length,1,'Default original load still clears the migrated legacy settings exactly once');
   assert.equal(owner.schedule.getGameCustomRecoveryState().persisted,true);assert.equal(owner.hardwareWrites.length,0);
   coldCustomMeasurements.push({gameActive,displayWrites:0,defaultFileWrites:1,defaultLegacyWrites:1,hardwareWrites:0});
  }finally{host.stop();await flush();assert.deepEqual(host.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});}
 }

 // A read-only observation keeps original corrupt/future and backup facts;
 // it must not archive/repair or clear legacy settings while rendering.
 for(const kind of ['main','backup','corrupt','future']){
  const legacy=(await import('./fixtures/decky_game_memory_store.mjs')).seed();
  const main='C:\\SOFT\\YeMan\\PowerControl\\game-custom.json'.replace(/\\\\/g,'\\');
  const documents=kind==='backup'?[[main+'.bak',JSON.stringify(legacy)]]:[[main,kind==='corrupt'?'{broken':JSON.stringify({...legacy,...(kind==='future'?{version:99}:{})})]];
  const {owner}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)},gameCustom:legacy},{gameActive:true,customDocuments:documents});
  const before=JSON.stringify(owner.settings);await display(owner);const state=owner.schedule.getGameCustomRecoveryState();
  assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);assert.equal(JSON.stringify(owner.settings),before);
  assert.equal(state.status,kind==='corrupt'||kind==='future'?'runtime-fallback':'ready');
  assert.equal(state.persisted,kind==='main');
  if(kind==='corrupt')assert.equal(state.reason,'corrupt');if(kind==='future')assert.equal(state.reason,'unsupported');
  coldCustomMeasurements.push({document:kind,displayWrites:0,recoveryStatus:state.status,persisted:state.persisted});
 }
 console.log(JSON.stringify({coldCustomMeasurements}));
 // COLD_CUSTOM_EXPLICIT_INTENT_BEGIN
 const coldIntentMeasurements=[];
 for(const gameActive of [false,true]){
  const legacy=(await import('./fixtures/decky_game_memory_store.mjs')).seed();
  const {owner}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)},gameCustom:legacy},{gameActive,customDocuments:[]});
  const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});host.state.game=owner.game;
  try{host.ready();host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
   const view=host.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;assert(view?.ready);assert.equal(owner.effects.length,0);
   const request={command:'performance.setControls',runId,id:'cold:1',clientId:'cold',sequence:1,generation:view.generation,revision:view.revision,identity:view.game?.identity??'',args:{patch:{tdp:22}}};
   host.emit('deckySidebar.mirrorRequest',{...peer,request});
   let reply;for(let i=0;i<10&&!reply;i++){await flush();reply=host.replies.find(row=>row.message.type==='reply'&&row.message.id==='cold:1')?.message;}
   console.log(JSON.stringify({coldIntentObservation:{gameActive,reply,effects:owner.effects,hardwareWrites:owner.hardwareWrites}}));
   assert(reply);assert.equal(reply.ok,true,'An explicit TDP intent after a cold display must reach its original owner');
   assert.equal(reply.result.saved,true);assert.equal(reply.result.applied,true);
   assert.equal(owner.hardwareWrites.filter(row=>row.kind==='tdp'&&row.side==='ac'&&row.watts===22).length,1);
   const saved=gameActive?(await owner.schedule.loadGameCustomConfig()).entries['game.exe'].controls:owner.settings.performanceSchedule.controls;
   assert.equal(saved.ac.tdp,22);assert.equal(owner.schedule.getGameCustomRecoveryState().persisted,true);
   const count=owner.hardwareWrites.length;host.emit('deckySidebar.mirrorRequest',{...peer,request});await flush();assert.equal(owner.hardwareWrites.length,count,'Duplicate delivery must not repeat a submitted TDP intent');
   coldIntentMeasurements.push({gameActive,savedTdp:22,acceptedTdpWrites:1,duplicateOwnerWrites:0,defaultMigrationPreserved:true});
  }finally{host.stop();await flush();assert.deepEqual(host.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});}
 }
 console.log(JSON.stringify({coldIntentMeasurements}));
 // COLD_CUSTOM_EXPLICIT_INTENT_END


 const sharedOwnerMeasurements=[];
 {
  const main=['C:','SOFT','YeMan','PowerControl','game-custom.json'].join(String.fromCharCode(92)),doc=JSON.stringify({version:1,entries:{},rtss:{}});
  let enter,release;const entered=new Promise(resolve=>enter=resolve),gate=new Promise(resolve=>release=resolve);
  const reads=[];let profileClock=100;
  const {owner}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,120,60)}},{gameActive:true,profileReadNow:()=>profileClock,customDocuments:[[main,doc],[main+'.bak',doc]],beforeCustomRead:async p=>{
   const backup=p===main+'.bak';assert(backup||p===main);reads.push(backup?'backup':'main');
   if(reads.length===1){enter();await gate;}
   else if(reads.length>2)profileClock+=backup?5:35;
  }});
  let ordinaryTimings=0;const predecessor=owner.schedule.loadGameCustomConfig('ensure',{onTiming:()=>ordinaryTimings++});
  await Promise.race([entered,predecessor.then(()=>{throw Error('Fixture profile read did not enter the queue gate');})]);
  const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});host.state.game=owner.game;
  try{
   host.ready();host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
   assert.deepEqual(reads,['main'],'The Decky display must still wait behind the owner queue');
   assert(!host.replies.some(row=>row.message.type==='snapshot'&&row.message.snapshot.ready));
   profileClock+=155;release();await predecessor;await flush();
   const report=host.diagnostics.find(row=>row.event==='snapshot-read-finished');
   assert.deepEqual(report?.profileReadMs,{queue:155,settings:0,main:35,backup:5},'Profile phase timings must survive the actual owner, host and relay');
   assert(host.replies.some(row=>row.message.type==='snapshot'&&row.message.snapshot.ready&&!Object.hasOwn(row.message.snapshot,'profileReadMs')),'Diagnostics cannot enter the plugin snapshot');
   assert.deepEqual(reads,['main','backup','main','backup']);assert.equal(ordinaryTimings,0);
   assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
   console.log(JSON.stringify({profileReadMs:report.profileReadMs,reads,ordinaryTimings,displayWrites:0}));
  }finally{release();await predecessor;host.stop();await flush();assert.deepEqual(host.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});}
 }
 // Both consumers use the same real schedule/FPS service, rather than a
 // production performance reader joined to an unrelated modeled frame reader.
 {
  const queuedDisplayReads=[];
  for(const ending of ['visible','hide','reopen','stop','new-game-process','new-power-generation']){
   const main=['C:','SOFT','YeMan','PowerControl','game-custom.json'].join(String.fromCharCode(92));
   const doc=JSON.stringify({version:1,entries:{},rtss:{}}),reads=[];
   let enter,release;const entered=new Promise(resolve=>enter=resolve),gate=new Promise(resolve=>release=resolve);
   const {owner,observations}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,120,60)}},{gameActive:true,customDocuments:[[main,doc],[main+'.bak',doc]],beforeCustomRead:async p=>{
    const backup=p===main+'.bak';assert(backup||p===main);reads.push(backup?'backup':'main');
    if(reads.length===1){enter();await gate;}
   }});
   const predecessor=owner.schedule.loadGameCustomConfig('read-only');
   await Promise.race([entered,predecessor.then(()=>{throw Error('Fixture predecessor did not enter the queue gate');})]);
   const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService,
    ...(ending==='new-power-generation'?{readHooks:{settingsGeneration:owner.settingsRepository.getSettingsGeneration}}:{})});host.state.game=owner.game;
   let ordinary;
   const demand=subscribe=>host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe}});
   const snapshots=()=>host.replies.filter(row=>row.message.type==='snapshot');
   try{
    host.ready();demand(true);await flush();
    assert.deepEqual(reads,['main']);assert.equal(observations.frameCalls,1,'The old display started its owner work');assert.equal(snapshots().length,0);
    if(ending==='stop')host.stop();
    if(ending==='hide'||ending==='reopen')demand(false);
    if(ending==='new-game-process'){
     const next={...owner.game,pid:23,processCreated:'created-23'};
     owner.setGame(next);host.state.game=next;host.gameChanged();await flush();
     assert.deepEqual(reads,['main']);assert.equal(snapshots().length,0);
    }
    if(ending==='new-power-generation'){
     const ready=host.state.power;
     host.state.power={...ready,generation:2,phase:'suspending',hardwareWritesAllowed:false};host.powerChanged(true);await flush();
     host.state.power={...ready,generation:2};host.powerChanged();await flush();
     assert.deepEqual(reads,['main']);assert.equal(snapshots().length,0);
    }
    ordinary=owner.schedule.loadGameCustomConfig('ensure',{requireReady:true,displayCurrent:()=>false});
    if(ending==='reopen')demand(true);
    release();await predecessor;await ordinary;await flush();
    const packets=snapshots();
    assert.equal(packets.length,ending==='hide'||ending==='stop'?0:1);
    if(packets.length)assert.equal(packets[0].message.snapshot.ready,true);
    if(ending==='new-game-process'){
     assert.deepEqual(JSON.parse(packets[0].message.snapshot.game.identity).slice(2,4),[23,'created-23']);
     assert.equal(packets[0].message.snapshot.actions.performance,true);
    }
    if(ending==='new-power-generation'){
     assert.equal(packets[0].message.snapshot.generation,2);
     assert.equal(packets[0].message.snapshot.actions.performance,true);
     assert.equal(host.counts.power,2,'A retired queued display must not start the independent fallback power read');
    }
    assert.equal(host.controllerTimerCount(),0);assert.equal(owner.schedule.getGameCustomRecoveryState().status,'ready');
    assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
    assert.deepEqual(reads,Array.from({length:reads.length/2},()=>['main','backup']).flat());
    queuedDisplayReads.push({ending,reads:reads.length,packets:packets.length,retryTimers:0,displayWrites:0});
   }finally{release();await predecessor;await ordinary;host.stop();await flush();assert.deepEqual(host.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});}
  }
  console.log(JSON.stringify({queuedDisplayReads}));
  assert.deepEqual(queuedDisplayReads.map(row=>row.reads),[6,4,6,4,6,6],'Retired profile demand must not start main or backup I/O');
 }
 for(const dedicated of [false,true]){
  const initialCustom=(await import('./fixtures/decky_game_memory_store.mjs')).seed();
  if(!dedicated)initialCustom.entries={};
  initialCustom.rtss['game.exe']=frameModel.dedicatedFrameRateRecord(frameModel.frameRatePair(null,45,30));
  const {owner,observations}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,120,60)}},{gameActive:true,initialCustom});
  const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});host.state.game=owner.game;
  try{host.ready();host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
   const view=host.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;assert(view?.ready);assert(view.performance);assert.equal(view.performance.scope,dedicated?'game':'global');assert.equal(view.frames.pair.ac.fps,dedicated?45:120);
   console.log(JSON.stringify({sharedOwnerObservation:{dedicated,...observations}}));
   assert.equal(observations.customReads,1,'One display must not reread its primary game configuration');assert.equal(observations.frameCalls,1,'One display must not repeat its original FPS owner');assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
   sharedOwnerMeasurements.push({dedicated,primaryConfigReads:observations.customReads,frameOwnerCalls:observations.frameCalls,displayWrites:0});
   // A later opening must read the original owner again and see external data.
   host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();
   owner.settings.rtss.frameRates=frameModel.frameRatePair(null,100,50);owner.settings.rtss.templateDefaults=false;
   host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
   const next=host.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;assert(next?.ready);assert.equal(next.frames.pair.ac.fps,dedicated?45:100);assert.equal(observations.customReads,2);assert.equal(observations.frameCalls,2);
   sharedOwnerMeasurements.push({dedicated,reopenPrimaryReads:2,reopenFrameCalls:2,currentGlobalFps:100,displayWrites:0});
  }finally{host.stop();await flush();assert.deepEqual(host.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});}
 }
 {
  const {owner,ports,observations}=build({rtss:{templateDefaults:false,frameRates:frameModel.frameRatePair(null,90,40)}},{gameActive:true});
  const custom=await owner.schedule.loadGameCustomConfig(),frameTask=owner.frameRateService.loadGlobalFrameRates();
  const prior=ports.generation??1;ports.generation=prior+1;
  const stale={generation:prior,custom,globalFrameRates:frameTask};
  await assert.rejects(owner.schedule.getContinuousPerformanceSnapshot('effective',undefined,undefined,'read-only',stale));
  const before=observations.customReads;const actual=await owner.schedule.getContinuousPerformanceSnapshot('effective',undefined,undefined,'ensure',{...stale,globalFrameRates:Promise.resolve(frameModel.frameRatePair(null,200,200))});
  assert.equal(actual.frameRates.ac.fps,90);assert.equal(actual.frameRates.dc.fps,40);assert.equal(observations.customReads,before+1,'Action read ignores the display input and reloads the original config');assert.equal(owner.effects.length,0);assert.equal(owner.hardwareWrites.length,0);
  sharedOwnerMeasurements.push({oldGenerationRejected:true,actionReadOwnsFreshConfig:true,displayInputIgnoredByAction:true});
 }
 const measurements=[];
 for(const fps of [0,75,120]){
  const {owner}=build({tdp:{fpsLimit:fps}});assert.equal(owner.settings.rtss.frameRates,undefined);
  const before=JSON.stringify(owner.settings);const host=await createMirrorHostFixture({performanceOwner:owner.schedule,frameOwner:owner.frameRateService});host.state.game=null;
  try{host.ready();host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();
   const view=host.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;assert(view?.ready);assert(view.performance);
   assert.equal(owner.effects.length,0,'Decky display must not persist legacy FPS');assert.equal(owner.hardwareWrites.length,0);assert.equal(JSON.stringify(owner.settings),before);
   const snapshot=await display(owner);assert.equal(snapshot.frameRates.ac.fps,fps);assert.equal(snapshot.frameRates.dc.fps,fps);
   const actionRead=await owner.schedule.getContinuousPerformanceSnapshot();assert.deepEqual(structuredClone(actionRead.frameRates),structuredClone(snapshot.frameRates));assert.equal(owner.effects.filter(x=>x==='settings-save').length,1,'Default action read still freezes legacy FPS');
   measurements.push({legacy:fps,displayWrites:0,actionMigrationWrites:1});
  }finally{host.stop();await flush();assert.deepEqual(host.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});}
 }
 {
  const {owner}=build({performanceSchedule:{configured:true,enabled:true,active:{ac:'eco',dc:'eco'},profiles:{ac:{eco:{fpsTarget:35}},dc:{eco:{fpsTarget:45}}}}});
  const s=await display(owner);assert.equal(s.frameRates.ac.fps,35);assert.equal(s.frameRates.dc.fps,45);assert.equal(owner.effects.length,0);measurements.push({legacyProfiles:[35,45],displayWrites:0});
 }
 {
  const saved=frameModel.frameRatePair(null,90,40),{owner}=build({tdp:{fpsLimit:75},rtss:{frameRates:saved,templateDefaults:false}});
  const s=await display(owner);assert.deepEqual(structuredClone(s.frameRates),structuredClone(saved));assert.equal(owner.effects.length,0);measurements.push({saved:[90,40],displayWrites:0});
 }
 // Hold a real save in the original frame queue; the view must finish before
 // release, while a default action read must still wait for that same write.
 {
  let release,entered;const gate=new Promise(resolve=>{release=resolve;}),started=new Promise(resolve=>{entered=resolve;});
  const {owner}=build({tdp:{fpsLimit:75}},{beforeSettingsCommit:async()=>{entered();await gate;}});
  const writer=owner.frameRateService.saveGlobalFrameRate('ac',{fps:90,ceiling:120,lastFps:90});await started;
  let viewDone=false,actionDone=false;const view=display(owner).then(s=>{viewDone=true;return s;}),action=owner.schedule.getContinuousPerformanceSnapshot().then(s=>{actionDone=true;return s;});
  try{await flush();assert.equal(viewDone,true,'Read-only snapshot must not wait behind a pending FPS save');assert.equal(actionDone,false,'Action read keeps original FIFO ordering');assert.equal(owner.effects.length,0);const s=await view;assert.equal(s.frameRates.ac.fps,75);}
  finally{release();await writer;await action;}
  assert.equal(owner.settings.rtss.frameRates.ac.fps,90);assert.equal(owner.settings.rtss.frameRates.dc.fps,75);assert.equal(owner.effects.filter(x=>x==='settings-save').length,1);measurements.push({heldWriter:true,displayCompletedBeforeRelease:true,actionCompletedAfterRelease:true});
 }
 // Retain generation/IO rejection rather than manufacturing usable controls.
 {
  const {owner,ports}=build({tdp:{fpsLimit:75}},{beforeFrameRead:async()=>{throw Error('frame-read-denied');}});
  await assert.rejects(display(owner),/frame-read-denied/);assert.equal(owner.effects.length,0);
  ports.generation=2;const old=await owner.settingsRepository.getSettingsGeneration();assert.equal(old,2);
 }
 console.log(JSON.stringify({actualFrameOwnerMeasurements:measurements,sharedOwnerMeasurements,storage:'memory ports',defaultParent:false}));

});
await check('host rejects old Decky game mode/core commands',async()=>{const {f,read}=await hostFixture();const s=await read();let n=0;for(const [field,value] of [['acMode','performance'],['dcMode','eco'],['corePolicyMode','only-small'],['hyperThreadPolicy','off']]){n++;f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'game.setField',runId,id:'old:'+n,clientId:'old',sequence:n,generation:s.generation,revision:s.revision,identity:s.game.identity,args:{field,value}}});await flush();const r=f.replies.findLast(x=>x.message.id==='old:'+n)?.message;assert.equal(r.ok,false);}assert.equal(f.counts.saves,0);f.stop();});
await check('host stop removes subscriptions and late source changes are inert',async()=>{const {f,read}=await hostFixture();await read();f.stop();assert.equal(f.observerStats().watchers,0);f.scheduleChanged();await flush();assert.equal(f.counts.saves,0);});
assert(cases.length>0&&[...selected].every(name=>cases.includes(name)),'Unknown read-only sidebar case');
console.log(JSON.stringify({status:'passed',cases,actualHardwareWrites:0,readOnlyRelay:true},null,2));
