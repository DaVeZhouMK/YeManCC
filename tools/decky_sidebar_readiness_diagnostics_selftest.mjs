// Readiness telemetry and independent snapshot reads: actual relay/client/host,
// memory IO/clock only. No Steam, business configuration or devices are touched.
import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
// Register original checks before loading fixtures so invalid selections cannot
// prepare an output tree or execute a valid callback before an unknown name.
let root,load,createMirrorHostFixture,createMirrorSpeedMemoryOwner,ReadOnlyMirrorRelay,MirrorClient;
const src=p=>fs.readFileSync(path.join(root,p),'utf8');
const runId='a'.repeat(32),peer={connection:'1',runId,connected:true},flush=async()=>{for(let n=0;n<180;n++)await Promise.resolve();await new Promise(resolve=>setImmediate(resolve));};
const defer=()=>{let resolve,reject;const promise=new Promise((r,j)=>{resolve=r;reject=j;});return {promise,resolve,reject};};
function updaterHostPowerFixture(install=()=>()=>{}){
 const events=new Map(),timers=new Map();let serial=0,revision=1,reads=0;
 let fact={phase:'ready',known:true,generation:1,hardwareWritesAllowed:true,inputReady:true,notificationPending:false,publicationRevision:revision,observedNotificationSequence:revision};
 const module=load(src('src/bridge/powerLifecycleState.ts'),{'./ipc':{invoke:()=>{throw Error('private native read forbidden');},on:()=>{throw Error('private native listener forbidden');}}},{AbortController});
 const source=module.createSharedPowerLifecycleSource({read:async()=>{reads++;return {...fact};},listen:(name,callback)=>{assert(!events.has(name));events.set(name,callback);return()=>events.delete(name);},clock:{setTimeout:(callback,delay)=>{const id=++serial;timers.set(id,{callback,delay});return id;},clearTimeout:id=>timers.delete(id)}});
 const ports={...source,isSharedPowerLifecycleAdmitted:module.isSharedPowerLifecycleAdmitted,onSharedPowerLifecycle:(callback,options)=>{const release=install('power',callback);let stop;try{stop=source.onSharedPowerLifecycle(callback,options);}catch(error){release();throw error;}return()=>{try{stop();}finally{release();}};}};
 return {ports,source,events,timers,get reads(){return reads;},fact:()=>({...fact}),publish:(change={},notify=true)=>{fact={...fact,...change,publicationRevision:++revision,observedNotificationSequence:revision};if(notify)events.get('power.lifecycleChanged')?.({...fact});return {...fact};},deliver:value=>events.get('power.lifecycleChanged')?.({...value}),retry:async()=>{assert.equal(timers.size,1,'one shared retry owns missing Ready');const [id,timer]=timers.entries().next().value;timers.delete(id);timer.callback();await flush();},assertClosed:()=>{assert.equal(events.size,0);assert.equal(timers.size,0);assert.equal(source.getSharedPowerLifecycleDiagnostics().subscriberCount,0);}};
}
async function exerciseUpdaterPowerBoundary(selectedMode=''){
 const store=load(src('decky-plugin/src/deckyStoreClient.ts')),protocol=load(src('decky-plugin/src/deckyUpdaterProtocol.ts'));
 const updater=load(src('src/bridge/deckyUpdaterService.ts'),{'../../decky-plugin/src/deckyStoreClient':store,'../../decky-plugin/src/deckyUpdaterProtocol':protocol});
 const allModes=['pending-admission','game-read-same-generation','game-read-new-generation','native-read-same-generation','native-read-new-generation','native-unknown','missing-ready','stop-during-validation'];
 const modes=selectedMode?[selectedMode]:allModes;
 const measurements=[];
 for(const mode of modes){
  const power=updaterHostPowerFixture(),messages=[],observers=new Map();let extension,installs=0,checks=0,downloads=0,gameReads=0,nativeReads=0,id=0;
  let gameBarrier=null,nativeBarrier=null;
  const install=(kind,callback)=>{assert(!observers.has(kind));observers.set(kind,callback);return()=>observers.delete(kind);};
  const target={version:'9.0.0',sha256:'b'.repeat(64)},state={phase:'downloaded',version:target.version,sha256:target.sha256};
  const multiplex=load(src('src/bridge/deckyMirrorExtensions.ts'));
  const {startDeckyUpdaterHost}=load(src('src/bridge/deckyUpdaterHost.ts'),{
   vue:{watch:(_source,callback)=>install('state',callback)},'@/version':{APP_VERSION:'0.0.39'},
   './ipc':{invoke:async(command,args)=>{assert.equal(command,'deckySidebar.mirrorReply');messages.push(structuredClone(args));return true;},on:()=>{throw Error('private power subscription forbidden');}},
   './powerLifecycleState':power.ports,'./api':{powerLifecycle:{get:async()=>{nativeReads++;if(nativeBarrier)return nativeBarrier.promise;return power.fact();}}},
   './gamedetect':{refreshGameStatusStrict:async()=>{gameReads++;if(gameBarrier)await gameBarrier.promise;return null;}},
   './gamePolicyTarget':{isPolicyGameInitialized:()=>true,getPolicyGame:()=>null,subscribePolicyGameStatus:callback=>install('game',callback)},
   './updateManager':{ensureUpdateManager:async()=>{},updateSnapshot:state,updateInfo:{value:target},checkForUpdate:async()=>{checks++;},prepareUpdate:async()=>{downloads++;return true;},downloadAndInstall:async()=>{installs++;}},
   './deckyMirrorExtensions':{registerDeckyMirrorExtension:value=>{extension=value;return multiplex.registerDeckyMirrorExtension(value);}},'./deckyUpdaterService':updater
  },{AbortController});
  const host=startDeckyUpdaterHost(()=>true),status=()=>extension.request({...peer,request:{command:'updater.status',runId,subscribe:true}}),request=()=>extension.request({...peer,request:{command:'updater.request',runId,clientId:'aabbccdd-1122-3344-5566-778899aabbcc',id:++id,action:'install',...target}}),view=()=>messages.filter(r=>r.message.type==='ymcc-updater').at(-1)?.message.view;
  try{
   multiplex.dispatchDeckyMirrorPeer(peer);status();await flush();assert.equal(view().canInstall,true,'initial admitted package remains installable');
   const old=power.fact();
   if(mode==='pending-admission'||mode==='missing-ready'){
    power.publish({hardwareWritesAllowed:false,notificationPending:true});await flush();
    assert.equal(view().canInstall,false,'pending shared power must deny updater installation');request();await flush();assert.equal(installs,0);assert.equal(gameReads,0);
    power.publish({hardwareWritesAllowed:true,notificationPending:false},mode!=='missing-ready');
    if(mode==='missing-ready'){await power.retry();assert.equal(power.source.getSharedPowerLifecycleDiagnostics().read.demand,false);}
    await flush();
   }else{
    if(mode.startsWith('game-read')||mode==='stop-during-validation')gameBarrier=defer();else nativeBarrier=defer();
    request();await flush();assert.equal(gameReads,1,'original install validation started');
    if(nativeBarrier)assert.equal(nativeReads,1,'original native validation started');
    if(mode==='stop-during-validation')host.stop();
    else if(mode!=='native-unknown'){
     power.publish({hardwareWritesAllowed:false,notificationPending:true});
     power.publish({phase:'ready',generation:mode.endsWith('new-generation')?2:1,hardwareWritesAllowed:true,notificationPending:false});
     power.deliver(old);await flush();
    }
    if(gameBarrier)gameBarrier.resolve();
    if(nativeBarrier)nativeBarrier.resolve(mode==='native-unknown'?{...power.fact(),hardwareWritesAllowed:false,known:false}:power.fact());
    await flush();assert.equal(installs,0,'interrupted updater validation must not install after power boundary');
    if(mode==='stop-during-validation'){power.assertClosed();assert.equal(observers.size,0);measurements.push({mode,installs,gameReads,nativeReads,stopped:true});continue;}
    gameBarrier=null;nativeBarrier=null;
   }
   assert.equal(view().canInstall,true,'interrupted validation must release the updater task');
   assert.equal(power.source.getSharedPowerLifecycleDiagnostics().subscriberCount,1,'one original host subscription remains after validation');
   const before=installs;request();await flush();assert.equal(installs,before+1,'fresh explicit install works after shared recovery');
   assert.equal(checks,0);assert.equal(downloads,0);assert.equal(power.source.getSharedPowerLifecycleDiagnostics().subscriberCount,1);
   measurements.push({mode,recoveredGeneration:power.fact().generation,installs,gameReads,nativeReads,sharedReads:power.reads,readinessReleased:true});
  }finally{gameBarrier?.resolve();nativeBarrier?.resolve(power.fact());host.stop();await flush();power.assertClosed();assert.equal(observers.size,0);}
 }
 assert.equal(measurements.length,modes.length);console.log(JSON.stringify({updaterSharedPowerMeasurements:measurements}));
}

const args=process.argv.slice(2),requestedCases=[],groups=[];
let selectionError='',speedCompletionOnly=false,updaterPowerOnly=false,updaterPowerMode='';
const speedCompletionCase='read recovery: a late old-game read after a power generation change cannot restore old admission';
const speedCompletionMeasurements=[];
for(let i=0;i<args.length;i++){
 const arg=args[i];
 if(arg==='--updater-power-only'){if(updaterPowerOnly)selectionError='Duplicate updater power selection';updaterPowerOnly=true;}
 else if(arg.startsWith('--updater-power-mode=')){if(updaterPowerMode)selectionError='Duplicate updater power mode';updaterPowerMode=arg.slice('--updater-power-mode='.length);}
 else if(arg==='--case'){
  if(!args[i+1]||args[i+1].startsWith('--')){selectionError='--case requires one exact original case name';break;}
  requestedCases.push(args[++i]);
 }else if(arg==='--speed-completion'){if(speedCompletionOnly){selectionError='Duplicate speed completion selection';break;}speedCompletionOnly=true;}
 else if(arg==='--read-recovery'||arg==='--legacy-diagnostics')groups.push(arg.slice(2));
 else{selectionError='Unknown readiness selection argument: '+arg;break;}
}
if(groups.length>1||groups.length&&requestedCases.length)selectionError='Readiness group and exact-case selections are mutually exclusive';
if(new Set(requestedCases).size!==requestedCases.length)selectionError='Duplicate exact readiness case selection';
if(speedCompletionOnly&&(groups.length||requestedCases.length!==1||requestedCases[0]!==speedCompletionCase))selectionError='--speed-completion requires only its original exact generation case';
if(updaterPowerOnly&&(groups.length||requestedCases.length!==1||requestedCases[0]!=='updater host releases every independent observation after one teardown callback fails'))selectionError='Updater power selection requires only its exact original host case';
if(updaterPowerMode&&(!updaterPowerOnly||!['pending-admission','game-read-same-generation','game-read-new-generation','native-read-same-generation','native-read-new-generation','native-unknown','missing-ready','stop-during-validation'].includes(updaterPowerMode)))selectionError='Unknown or unsupported updater power mode';
const selection=requestedCases.length?'cases':groups[0]??'all';
const readRecoveryOnly=selection==='read-recovery',legacyOnly=selection==='legacy-diagnostics';
const registry=[],cases=[],skipped=[],selectedResults=[];
const check=async(name,fn)=>{registry.push({name,fn});};
const data=()=>({generation:1,ready:true,game:null,fan:{supported:false,enabled:false,preset:'balanced',choices:[]}});
function relayFixture(read=async()=>data(),diagnosticThrows=false){const logs=[],replies=[],timers=new Map();let now=0,watcher=null,reads=0,nextTimer=0;
 const relay=new ReadOnlyMirrorRelay({read:async trace=>{reads++;return read(trace,()=>now,delta=>{now+=delta;});},now:()=>now,observe:fn=>{watcher=fn;return()=>watcher=null;},reply:async(_c,m)=>{replies.push(m);return true;},diagnostic:(event,detail)=>{if(diagnosticThrows)throw Error('Memory log offline');logs.push({event,...structuredClone(detail)});},schedule:(fn,ms)=>{const id=++nextTimer;timers.set(id,{fn,ms});return id;},cancel:id=>timers.delete(id)});
 const f={relay,logs,replies,timers,fire:async ms=>{const entry=[...timers].find(([,timer])=>timer.ms===ms);assert(entry,'Missing read retry '+ms);timers.delete(entry[0]);now+=ms;entry[1].fn();await flush();},get reads(){return reads;},now:()=>now,advance:delta=>now+=delta,open:()=>relay.request({...peer,request:{command:'snapshot',runId}}),pause:()=>relay.request({...peer,request:{command:'snapshot',runId,subscribe:false}}),invalidate:async()=>{watcher?.();await flush();},dispose:()=>relay.dispose()};return f;}
await check('first demand records one start/finish with correlated read ID and stage timings',async()=>{
 // Same original telemetry case: actual Host and fixed startup phases, no IO.
 const startupClock={now:0},startup=await createMirrorHostFixture({clock:startupClock});
 try{
  const onlyStartup=()=>startup.diagnostics.filter(row=>row.event==='startup-phase');
  assert.equal(onlyStartup().length,1);assert.equal(onlyStartup()[0].stage,'registered');
  const reads={...startup.counts};startupClock.now=17;startup.startupPhase('native-context');
  startupClock.now=129;startup.startupPhase('fan-settings');
  assert.equal(onlyStartup().at(-1).previousStage,'native-context');assert.equal(onlyStartup().at(-1).previousMs,112);
  for(let n=0;n<1000;n++){startup.startupPhase('fan-settings');startup.startupPhase('unknown');startup.startupPhase('registered');}
  assert.equal(onlyStartup().length,3);assert.deepEqual(startup.counts,reads,'startup observations must not read settings or apply business controls');
  startupClock.now=229;startup.ready();const ready=onlyStartup().at(-1);assert.equal(ready.stage,'ready');assert.equal(ready.elapsedMs,229);assert.equal(ready.previousMs,100);
  const count=onlyStartup().length;startup.startupPhase('tray');startupClock.now=NaN;startup.startupPhase('performance');assert.equal(onlyStartup().length,count);
  startup.stop();assert.equal(onlyStartup().length,count);assert.equal(startup.controllerTimerCount(),0);assert.equal(startup.fxTimerCount(),0);
 }finally{startup.stop();}
 const failedLog=await createMirrorHostFixture({readHooks:{diagnostic:async()=>{throw Error('inert diagnostic transport failure');}}});
 try{failedLog.startupPhase('native-context');failedLog.ready();await flush();assert.equal(failedLog.counts.saves,0);assert.equal(failedLog.controllerTimerCount(),0);}finally{failedLog.stop();}
const f=relayFixture(async(trace,_now,advance)=>{await trace.measure('power',async()=>advance(25));await trace.measure('frames',async()=>advance(40));trace.performanceStage?.('performanceCore',17);return data();});try{await f.open();const a=f.logs.find(x=>x.event==='snapshot-read-start'),b=f.logs.find(x=>x.event==='snapshot-read-finished');assert.equal(a.readId,b.readId);assert.equal(a.demandEpoch,b.demandEpoch);assert.equal(b.elapsedMs,65);assert.equal(b.powerMs,25);assert.equal(b.framesMs,40);assert.equal(b.performanceCoreMs,17);assert.equal(b.ok,true);assert.equal(b.discarded,false);assert.equal(f.timers.size,0);}finally{f.dispose();}});
await check('1000 healthy fast refreshes do not create 1000 telemetry records or a polling timer',async()=>{const f=relayFixture();try{await f.open();const before=f.logs.length;for(let i=0;i<1000;i++)await f.invalidate();assert.equal(f.logs.length,before);assert.equal(f.timers.size,0);}finally{f.dispose();}});
await check('slow reads are recorded at most once per ten seconds outside first demand',async()=>{const f=relayFixture(async(trace,_now,advance)=>{await trace.measure('monitor',async()=>advance(300));return data();});try{await f.open();for(let i=0;i<10;i++)await f.invalidate();assert.equal(f.logs.filter(x=>x.event==='snapshot-read-finished').length,1);f.advance(10000);await f.invalidate();assert.equal(f.logs.filter(x=>x.event==='snapshot-read-finished').length,2);assert.equal(f.logs.at(-1).monitorMs,300);}finally{f.dispose();}});
await check('pause/reopen records the real demand transition and first read even inside the slow-log cooldown',async()=>{const f=relayFixture();try{await f.open();await f.pause();await f.pause();await f.open();const demands=f.logs.filter(x=>x.event==='snapshot-demand');assert.deepEqual(demands.map(x=>x.subscribe),[true,false,true]);assert.equal(f.logs.filter(x=>x.event==='snapshot-read-start').length,2);assert.equal(f.logs.filter(x=>x.event==='snapshot-read-finished').length,2);}finally{f.dispose();}});
await check('a blocked first read has a start record until completion, while hiding discards its eventual result',async()=>{const done=defer(),f=relayFixture(async trace=>{await trace.measure('frames',()=>done.promise);return data();});try{const open=f.open();await flush();assert.equal(f.logs.filter(x=>x.event==='snapshot-read-start').length,1);assert.equal(f.logs.filter(x=>x.event==='snapshot-read-finished').length,0);await f.pause();done.resolve();await open;const end=f.logs.find(x=>x.event==='snapshot-read-finished');assert.equal(end.discarded,true);assert.equal(end.discardReason,'demand-changed');assert.equal(end.failureKind,undefined);assert.equal(f.replies.length,0);assert.equal(end.demandEpoch,f.logs.find(x=>x.event==='snapshot-read-start').demandEpoch);}finally{done.resolve();f.dispose();}});
await check('partial owner errors retain unrelated controls and produce a fixed partial stage rather than raw messages',async()=>{const f=relayFixture(async trace=>{try{await trace.measure('touchpads',async()=>{throw Error('PRIVATE/file/path');});}catch{}return data();});try{await f.open();assert.equal(f.replies.at(-1).snapshot.ready,true);const log=f.logs.find(x=>x.event==='snapshot-read-finished');assert.equal(log.ok,false);assert.equal(log.stage,'partial');assert.equal(log.failureKind,'owner-read');assert.equal(log.failureStage,'touchpads');assert.equal(log.discardReason,undefined);assert(!JSON.stringify(f.logs).includes('PRIVATE'));}finally{f.dispose();}});
await check('complete read failure records failed and preserves the original not-ready fallback',async()=>{
 // SOURCE_CHANGE_DIAGNOSTIC_BEGIN
 {
  const findings=[];
  for(const [events,expected,sources] of [[['input'],'input'],[['game','game'],'game'],[['power','monitor','monitor'],'multiple',['monitor','power']],[['C:/PRIVATE'],'unspecified'],[['power','C:/PRIVATE'],'multiple',['power','unspecified']],[['power','probe'],'multiple',['power','snapshot-request']]]){
   const gate=defer();let calls=0;const f=relayFixture(async()=>{if(++calls===1)await gate.promise;return data();});
   try{const opening=f.open();await flush();let probe;
    for(const source of events){
     if(source==='probe')probe=f.relay.request({...peer,request:{command:'snapshot',runId,probeId:'probe:win5:1'}});
     else f.relay.invalidate(source);
    }
    gate.resolve();await opening;await probe;await flush();
    const retired=f.logs.find(row=>row.event==='snapshot-read-finished'&&row.discarded);assert.equal(retired.discardReason,'source-changed');
    assert.equal(retired.invalidatedBy,expected,'A discarded read must identify its fixed notification source');
    assert.deepEqual(retired.invalidatedBySources,sources,'Only multiple distinct fixed sources are expanded');
    const current=f.logs.find(row=>row.event==='snapshot-read-finished'&&!row.discarded);assert.equal(current.invalidatedBy,undefined,'Accepted retry cannot inherit a retired read cause');assert.equal(current.invalidatedBySources,undefined);assert.equal(f.reads,2);assert.equal(f.timers.size,0);assert(!JSON.stringify(f.logs).includes('PRIVATE'));
    if(probe)assert(f.replies.some(row=>row.probeId==='probe:win5:1'&&row.snapshot.ready));
    findings.push({events:events.map(source=>source.includes('PRIVATE')?'unrecognized':source),invalidatedBy:retired.invalidatedBy,invalidatedBySources:retired.invalidatedBySources,acceptedRetryTagged:false});
   }finally{gate.resolve();f.dispose();}
  }
  {
   const gate=defer();let calls=0;const f=await createMirrorHostFixture({readHooks:{frames:async()=>{if(++calls===1)await gate.promise;}}});
   try{f.ready();const opening=request(f);await flush();assert.equal(calls,1);f.state.monitor={...f.state.monitor,enabled:false};f.monitorChanged();await flush();gate.resolve();await opening;await flush();
    const retired=f.diagnostics.find(row=>row.event==='snapshot-read-finished'&&row.discarded);assert.equal(retired.discardReason,'source-changed');assert.equal(retired.invalidatedBy,'monitor','Actual Host must pass the original monitor notification source');
    const ready=f.replies.filter(row=>row.message.type==='snapshot').at(-1).message.snapshot;assert.equal(ready.ready,true);assert.equal(ready.monitor.enabled,false);assert.equal(f.counts.saves,0);assert.equal(f.counts.monitorSaves,0);assert.equal(f.controllerTimerCount(),0);
    findings.push({actualHost:'monitor',oldReadTagged:true,currentMonitorEnabled:false,configurationWrites:0});
   }finally{gate.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);assert(Object.values(f.observerStats()).every(value=>value===0));}
  }
  {
   const oldGate=defer(),newGate=defer();let calls=0;const f=relayFixture(async()=>{const number=++calls;if(number===1)await oldGate.promise;else if(number===2)await newGate.promise;return data();});
   try{const first=f.open();await flush();f.relay.invalidate('input');const nextPeer={...peer,connection:'2',connectionRevision:1};f.relay.connected(nextPeer);const second=f.relay.request({...nextPeer,request:{command:'snapshot',runId}});await flush();f.relay.invalidate('power');oldGate.resolve();await first;await flush();newGate.resolve();await second;await flush();
    const old=f.logs.find(row=>row.event==='snapshot-read-finished'&&row.discardReason==='peer-changed');assert(old);assert.equal(old.invalidatedBy,undefined,'Peer retirement has its own higher-priority reason');
    const changed=f.logs.find(row=>row.event==='snapshot-read-finished'&&row.discardReason==='source-changed');assert(changed);assert.equal(changed.invalidatedBy,'power','New peer scope cannot inherit or be cleared by late old scope');assert.equal(f.timers.size,0);
    findings.push({lateOldPeer:'peer-changed',oldInvalidationOmitted:true,newPeerInvalidatedBy:'power'});
   }finally{oldGate.resolve();newGate.resolve();f.dispose();}
  }
  {
   const listeners=[],responses=[];let reads=0;const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;return data();},observe:callback=>{listeners.push(callback);return()=>{};},reply:async(_connection,message)=>{responses.push(message);return true;}});
   try{await relay.request({...peer,request:{command:'snapshot',runId}});await relay.request({...peer,request:{command:'snapshot',runId,subscribe:false}});await relay.request({...peer,request:{command:'snapshot',runId}});await flush();const before=reads;
    listeners[0]('input');await flush();assert.equal(reads,before,'Tagged late observer must retain original demand-epoch fence');listeners.at(-1)('input');await flush();assert.equal(reads,before+1);
    findings.push({retiredObserverTagged:true,lateReads:0,currentObserverReads:1});
   }finally{relay.dispose();}
  }
  console.log(JSON.stringify({sourceChangeDiagnosticMeasurements:findings,scope:'Fixed labels only; existing first/fault/slow log cadence, no new read or recovery decision'}));
 }
 // SOURCE_CHANGE_DIAGNOSTIC_END

 const f=relayFixture(async()=>{throw Error('PRIVATE');});
 try{await f.open();assert.equal(f.replies.at(-1).snapshot.ready,false);const end=f.logs.find(x=>x.event==='snapshot-read-finished');assert.equal(end.stage,'failed');assert.equal(end.ok,false);assert.equal(end.failureKind,'read-error');assert.equal(end.failureStage,undefined);assert(!JSON.stringify(f.logs).includes('PRIVATE'));}finally{f.dispose();}
 // Same original error boundary, with a real source invalidation while the
 // first read is blocked. The late result is not a successful current view.
 const gate=defer();let reads=0;
 const changed=relayFixture(async()=>{if(++reads===1){await gate.promise;throw Error('MIRROR_SNAPSHOT_SOURCE_CHANGED');}return data();});
 try{const open=changed.open();await flush();await changed.invalidate();gate.resolve();await open;await flush();
  const failed=changed.logs.find(x=>x.event==='snapshot-read-finished'&&x.stage==='failed');
  assert(failed);assert.equal(failed.failureKind,'source-changed');assert.equal(failed.discardReason,'source-changed');assert.equal(failed.discarded,true);
  assert.equal(changed.replies.at(-1).snapshot.ready,true);assert.equal(changed.timers.size,0);assert(!JSON.stringify(changed.logs).includes('MIRROR_SNAPSHOT_SOURCE_CHANGED'));
  const accepted=changed.logs.find(row=>row.event==='snapshot-read-finished'&&row.discarded===false&&row.ready===true);
  assert(accepted,'A current retry after the first discarded read must have its own outcome record');
  assert.equal(accepted.ok,true);assert.equal(accepted.demandEpoch,failed.demandEpoch);assert(accepted.readId>failed.readId);
 }finally{gate.resolve();changed.dispose();}
});
await check('diagnostic transport failures cannot fail an otherwise healthy snapshot',async()=>{const f=relayFixture(undefined,true);try{await f.open();assert.equal(f.replies.at(-1).snapshot.ready,true);assert.equal(f.timers.size,0);}finally{f.dispose();}});
const request=async f=>{f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;};
await check('independent control reads begin concurrently before a delayed frame owner returns',async()=>{const frames=defer(),input=defer(),monitor=defer();const f=await createMirrorHostFixture({sharedGyroInput:true,readHooks:{frames:()=>frames.promise,input:()=>input.promise,monitor:()=>monitor.promise}});try{f.ready();const pending=request(f);await flush();assert.equal(f.counts.frameReads,1);assert.equal(f.counts.inputReads,1);assert.equal(f.counts.monitorReads,1);input.resolve();await flush();assert.equal(f.counts.touchpadReads,1);assert.equal(f.replies.filter(x=>x.message.type==='snapshot').length,0);frames.resolve();monitor.resolve();await pending;await flush();const snapshot=f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;assert(snapshot?.ready);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);}finally{frames.resolve();input.resolve();monitor.resolve();f.stop();}
 // Current shared input is display-only. Reopening reads again; action defaults
 // always call the original settings owner, not a retained display promise.
 const measurements=[];
 for(const dedicated of [false,true]){
  const initial={version:1,entries:{},rtss:{}};
  if(dedicated)initial.entries['game.exe']={enabled:true,displayName:'Fixture Game',ac:{tdpMax:20},dc:{tdpMax:10},padPersona:'follow',gyroOverride:'follow'};
  const h=await createMirrorHostFixture({initial,sharedGyroInput:true});
  try{
   h.state.input.gyroMotion={enabled:true,preset:'racing'};h.state.input.outputTarget={persona:'steamdeck',gyroEnabled:true,buttonMappingEnabled:true};h.ready();
   let view=await request(h);await flush();assert(view?.ready);assert.equal(h.counts.inputReads,1,'A game display must share one original input read');assert.equal(view.game.fields.gyroPreset.value,'racing');assert.equal(view.game.fields.gyroEnabled.value,'on');
   const first=structuredClone(view);
   h.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();h.state.input.gyroMotion.preset='steam';
   view=await request(h);await flush();assert(view?.ready);assert.equal(h.counts.inputReads,2);assert.equal(view.game.fields.gyroPreset.value,'steam');
   for(const key of ['saves','inputSaves','monitorSaves','fxSoundWrites'])assert.equal(h.counts[key],0);
   measurements.push({dedicated,firstInputReads:1,reopenedInputReads:h.counts.inputReads,firstPreset:first.game.fields.gyroPreset.value,currentPreset:view.game.fields.gyroPreset.value,displayWrites:0});
  }finally{h.stop();await flush();assert.deepEqual(h.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});assert.equal(h.controllerTimerCount(),0);assert.equal(h.fxTimerCount(),0);assert.equal(h.sharedPowerTimerCount(),0);}
 }

 // A failed shared input read disables its dependent fields only. The existing
 // read-recovery timer must fetch a new input promise after the owner returns.
 {
  let failed=true;const h=await createMirrorHostFixture({sharedGyroInput:true,readHooks:{input:async()=>{if(failed)throw Error('Inert input owner unavailable');}}});
  try{h.state.input.gyroMotion={enabled:true,preset:'steam'};h.state.input.outputTarget={persona:'steamdeck',gyroEnabled:true,buttonMappingEnabled:true};h.ready();
   let view=await request(h);await flush();assert(view?.ready);assert.equal(h.counts.inputReads,1);assert.equal(view.game.fields.gyroPreset.supported,false);assert.equal(view.actions.monitor,true);assert(view.frames);assert.equal(h.controllerTimerCount(),1);
   failed=false;h.advanceController(250);await flush();view=h.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;assert(view?.ready);assert.equal(h.counts.inputReads,2);assert.equal(view.game.fields.gyroPreset.value,'steam');assert.equal(view.game.fields.gyroPreset.supported,true);assert.equal(h.controllerTimerCount(),0);
   assert.equal(h.counts.inputSaves,0);assert.equal(h.counts.saves,0);measurements.push({inputFailureRecovery:true,failedReadCount:1,recoveredReadCount:2,currentPreset:view.game.fields.gyroPreset.value,dependentFieldsOnly:true});
  }finally{h.stop();await flush();assert.deepEqual(h.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});assert.equal(h.controllerTimerCount(),0);assert.equal(h.fxTimerCount(),0);assert.equal(h.sharedPowerTimerCount(),0);}
 }
 console.log(JSON.stringify({sharedGyroInputMeasurements:measurements}));
});
let beforeCounts=null,afterCounts;
const completionMeasurements=[];
const transportMeasurements=[];
const audioGenerationMeasurements=[];
await check('one no-game snapshot reuses input and schedule rather than loading each twice',async()=>{
 const f=await createMirrorHostFixture();
 try{
  f.state.game=null;f.ready();const snapshot=await request(f);await flush();
  assert(snapshot?.ready);assert.equal(snapshot.game,null);
  afterCounts={input:f.counts.inputReads,schedule:f.counts.schedule,frames:f.counts.frameReads,monitor:f.counts.monitorReads};
  assert.deepEqual(afterCounts,{input:1,schedule:1,frames:1,monitor:1});
  assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
 }finally{f.stop();}

 // Controls completing first cannot publish an unobserved audio state.
 {
  const audio=defer();const h=await createMirrorHostFixture({readHooks:{fxSound:()=>audio.promise}});
  try{h.state.game=null;h.ready();const pending=request(h);await flush();assert.equal(h.counts.fxSoundReads,1);assert.equal(h.counts.frameReads,1);assert.equal(h.replies.filter(x=>x.message.type==='snapshot').length,0);
   audio.resolve();await pending;await flush();const view=h.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;assert(view?.ready);assert.equal(view.audioOptimization.known,true);assert.equal(h.counts.frameReads,1);assert.equal(h.counts.fxSoundReads,1);assert(h.diagnostics.some(x=>x.event==='snapshot-read-finished'&&Number.isFinite(x.audioMs)));assert.equal(h.counts.saves,0);assert.equal(h.counts.fxSoundWrites,0);
   completionMeasurements.push({timing:'audio-after-controls',controlBatches:1,audioReads:1,noEarlyPublication:true});
  }finally{audio.resolve();h.stop();await flush();assert.equal(h.fxTimerCount(),0);assert.equal(h.controllerTimerCount(),0);}
 }
 // Controlled completion order, using the original read hooks/owner clock.
 const snapshots=f=>f.replies.filter(row=>row.message.type==='snapshot').map(row=>row.message.snapshot);
 const counts=f=>({input:f.counts.inputReads,frames:f.counts.frameReads,monitor:f.counts.monitorReads});
 {
  const audio=defer(),frames=defer(),program=defer();let programReads=0;
  const f=await createMirrorHostFixture({readHooks:{fxSound:()=>audio.promise,frames:()=>frames.promise,losslessProgram:()=>{programReads++;return program.promise;}}});
  try{
   f.state.game=null;f.ready();const pending=request(f);await flush();assert.equal(f.counts.fxSoundReads,1);assert.equal(f.counts.frameReads,1,'Independent controls must start while audio is pending');assert.equal(snapshots(f).length,0,'Audio is still required for the final admitted view');
   audio.resolve();await flush();assert.equal(snapshots(f).length,0);
   assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});assert.equal(programReads,1);
   f.state.losslessProgram={known:true,running:true};program.resolve();await flush();assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});
   frames.resolve();await pending;await flush();const current=snapshots(f).at(-1);
   assert(current?.ready);assert.deepEqual(current.losslessProgram,{known:true,running:true});assert.equal(current.audioOptimization.known,true);
   assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});assert.equal(f.counts.fxSoundReads,1);assert.equal(programReads,1);
   assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.fxSoundWrites,0);
   completionMeasurements.push({timing:'program-before-controls',counts:counts(f),programReads,publishedCurrent:true});
  }finally{audio.resolve();program.resolve();frames.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
 {
  const program=defer();let programReads=0;
  const f=await createMirrorHostFixture({readHooks:{losslessProgram:()=>{programReads++;return program.promise;}}});
  try{
   f.state.game=null;f.ready();await request(f);await flush();const initial=snapshots(f).at(-1);
   assert(initial?.ready);assert.equal(initial.losslessProgram.known,false);assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});
   const boundary=snapshots(f).length;f.state.losslessProgram={known:true,running:true};program.resolve();await flush();
   const updates=snapshots(f).slice(boundary);assert.equal(updates.length,1,'Completion after a published pending view must refresh it once');
   assert.deepEqual(updates[0],{...initial,losslessProgram:{known:true,running:true}});
   assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});assert.equal(programReads,1);
   await flush();assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});assert.equal(f.counts.saves,0);assert.equal(f.counts.fxSoundWrites,0);
   completionMeasurements.push({timing:'program-after-snapshot',counts:counts(f),programReads,followingSnapshots:updates.length});
  }finally{program.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
 {
  const initialAudio=defer(),frames=defer();let audioReads=0;
  const f=await createMirrorHostFixture({readHooks:{frames:()=>frames.promise,fxSound:async()=>{audioReads++;if(audioReads===1)await initialAudio.promise;}}});
  try{
   f.state.game=null;f.ready();const pending=request(f);await flush();assert.equal(audioReads,1);assert.equal(f.counts.frameReads,1,'Audio capture must not serialize the control batch');
   initialAudio.resolve();await flush();assert.equal(f.fxTimerCount(),1);assert.equal(snapshots(f).length,0);
   f.state.fxSound={...f.state.fxSound,running:true,enabled:true};f.advanceFx();await flush();assert.equal(audioReads,2,'Actual existing audio tick must complete before the blocked frames');
   assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});frames.resolve();await pending;await flush();
   const views=snapshots(f);assert(views.length);assert(views.every(view=>view.ready&&view.audioOptimization.enabled===true),'An audio change after capture must discard every old admitted view');
   assert.deepEqual(counts(f),{input:2,frames:2,monitor:2});assert.equal(audioReads,2,'The current retry consumes the same-generation tick, not a second audio read');
   assert.equal(f.counts.saves,0);assert.equal(f.counts.fxSoundWrites,0);
   completionMeasurements.push({timing:'audio-after-capture',counts:counts(f),audioReads,noStalePublication:true});
  }finally{initialAudio.resolve();frames.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
});
await check('a late read from the old game cannot publish stale identity after a game switch',async()=>{
 for(const change of ['target','readiness']){
  const gate=defer();let calls=0;
  const f=await createMirrorHostFixture({readHooks:{frames:()=>++calls===1?gate.promise:Promise.resolve()}});
  try{
   f.ready();const pending=request(f);await flush();assert.equal(calls,1);
   if(change==='target'){
    f.state.game={...f.state.game,pid:202,processCreated:'birth-B',path:'C:/Other/Game.exe'};f.gameChanged();
   }else f.setGameReadiness(false);
   gate.resolve();await pending;await flush();
   const snapshots=f.replies.filter(x=>x.message.type==='snapshot').map(x=>x.message.snapshot);assert(snapshots.length);
   if(change==='target')assert(snapshots.every(x=>!x.game||x.game.identity.includes('202')));
   else assert(snapshots.every(x=>x.ready&&x.actions.game===false&&x.actions.monitor===true));
   const finished=f.diagnostics.filter(row=>row.event==='snapshot-read-finished');
   const retired=finished.find(row=>row.discarded);
   assert.equal(retired?.discardReason,'source-changed');
   assert.equal(retired.invalidatedBy,'game',`Missing game invalidation source: ${change}`);
   const accepted=finished.find(row=>!row.discarded);assert(accepted?.ready);assert.equal(accepted.invalidatedBy,undefined);
   assert.equal(calls,2);assert.equal(f.counts.saves,0);assert.equal(f.controllerTimerCount(),0);
  }finally{gate.resolve();f.stop();await flush();assert(Object.values(f.observerStats()).every(value=>value===0));}
 }
});
await check('settings invalidation during parallel reads retries against the latest schedule',async()=>{
 // Keep settings and target changes independent: a target fence must not mask a broken settings fence.
 for(const change of ['settings','target']){
  const gate=defer();let hold=false,frameStarts=0;const performanceStarts=[];let f;
  f=await createMirrorHostFixture({readHooks:{
   frames:async()=>{if(hold){frameStarts++;await gate.promise;}},
   performance:async()=>{performanceStarts.push({tdp:f.state.schedule.controls.ac.tdp,pid:f.state.game.pid});},
  }});
  try{
   f.state.schedule.controls={version:1,ac:{mode:'auto',tdp:21,tdpCeiling:55},dc:{mode:'auto',tdp:17,tdpCeiling:30}};
   f.ready();await request(f);await flush();
   const previous=f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;
   assert(previous?.ready);assert(previous.performance?.enabled);assert.equal(previous.performance.scope,'global');assert.equal(previous.performance.side,'ac');
   assert.equal(previous.performance.controls.find(x=>x.key==='tdp')?.value,21);
   const oldRevision=previous.performance.sourceRevision;assert(oldRevision);
   const originalGame={...f.state.game},readsBefore=performanceStarts.length;
   // Identical preferences intentionally do not invalidate a snapshot.
   hold=true;f.state.monitor.enabled=!f.state.monitor.enabled;f.monitorChanged();await flush();
   assert.equal(frameStarts,1,'The original frame owner must still be blocked');
   assert.equal(performanceStarts.length,readsBefore+1,'The parallel performance reader must have started on the old source');
   assert.deepEqual(performanceStarts.at(-1),{tdp:21,pid:originalGame.pid});
   const invalidationBoundary=f.replies.length;
   if(change==='settings'){
    f.state.schedule.controls.ac.tdp=29;f.scheduleChanged();
   }else{
    f.state.game={...originalGame,pid:202,processCreated:'birth-B',path:'C:/Other/Game.exe'};f.gameChanged();
   }
   await flush();
   hold=false;gate.resolve();await flush();
   assert(performanceStarts.length>=readsBefore+2,'Invalidated parallel reads must be retried against the current owner');
   const snapshots=f.replies.slice(invalidationBoundary).filter(x=>x.message.type==='snapshot').map(x=>x.message.snapshot);
   assert(snapshots.length,'A current snapshot must eventually be published');
   for(const snapshot of snapshots){
    // Pending snapshots may retain display values; they cannot grant current operation authority.
    if(!snapshot.ready){assert(Object.values(snapshot.actions??{}).every(value=>value===false));assert(snapshot.fan?.canToggle!==true);continue;}
    assert(snapshot.performance?.enabled);assert.equal(snapshot.performance.scope,'global');assert.equal(snapshot.performance.side,'ac');
    assert.equal(snapshot.performance.controls.find(x=>x.key==='tdp')?.value,change==='settings'?29:21);
    if(change==='settings')assert.notEqual(snapshot.performance.sourceRevision,oldRevision,'Admitted settings must refer to the new performance source');
    if(change==='target'){assert(snapshot.game?.identity.includes('202'));assert.notEqual(snapshot.game.identity,previous.game.identity);}
    else assert.equal(snapshot.game?.identity,previous.game.identity);
   }
   const final=snapshots.at(-1);assert(final?.ready);assert(final.performance?.enabled);
   assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{hold=false;gate.resolve();f.stop();}
 }

 // An old peer may finish after the new peer has acquired its read lease.
 // Keep the stable branch separate so an intentional source event cannot mask
 // an unwanted LS-completion retry caused by clearing the new peer's lease.
 for(const externalSettings of [false,true]){
  const framesA=defer(),framesB=defer(),programA=defer(),programB=defer();let frameStarts=0,programReads=0;
  const f=await createMirrorHostFixture({readHooks:{frames:()=>{frameStarts++;return frameStarts===1?framesA.promise:frameStarts===2?framesB.promise:Promise.resolve();},losslessProgram:()=>++programReads===1?programA.promise:programB.promise}});
  try{
   f.state.game=null;f.state.schedule.controls={version:1,ac:{mode:'auto',tdp:21,tdpCeiling:55},dc:{mode:'auto',tdp:17,tdpCeiling:30}};
   f.ready();const pendingA=request(f);await flush();assert.equal(frameStarts,1);assert.equal(programReads,1);
   const currentPeer={...peer,connection:'2'};f.emit('deckySidebar.mirrorPeer',currentPeer);f.emit('deckySidebar.mirrorRequest',{...currentPeer,request:{command:'snapshot',runId}});await flush();assert.equal(frameStarts,2);
   framesA.resolve();await pendingA;await flush();assert.equal(frameStarts,2);
   f.state.losslessProgram={known:true,running:false};programA.resolve();await flush();assert.equal(frameStarts,2);
   if(externalSettings){f.state.schedule.controls.ac.tdp=29;f.scheduleChanged();await flush();}
   framesB.resolve();await flush();const views=f.replies.filter(row=>row.message.type==='snapshot');assert(views.length);
   assert(views.every(row=>row.connection==='2'),'No retired-peer view may be delivered');
   assert(views.every(row=>row.message.snapshot.ready&&row.message.snapshot.performance.controls.find(control=>control.key==='tdp')?.value===(externalSettings?29:21)),'Only the current external owner source may be admitted');
   assert.deepEqual(views.at(-1).message.snapshot.losslessProgram,{known:false,running:false},'Retired peer process receipt must stay unconfirmed');
   const revision=views.at(-1).message.snapshot.revision;
   f.state.losslessProgram={known:true,running:true};programB.resolve();await flush();
   const final=f.replies.filter(row=>row.message.type==='snapshot').at(-1);
   assert.equal(final.connection,'2');assert.equal(final.message.snapshot.revision,revision);
   assert.deepEqual(final.message.snapshot.losslessProgram,{known:true,running:true});assert.equal(programReads,2);
   assert.equal(frameStarts,externalSettings?3:2,'Old finally cannot clear a newer lease; only an actual settings edge adds a retry');
   assert.equal(f.counts.inputReads,frameStarts);assert.equal(f.counts.monitorReads,frameStarts);assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.counts.fxSoundWrites,0);
   completionMeasurements.push({timing:externalSettings?'old-peer-with-settings':'old-peer-stable',controlBatches:frameStarts,programReads,currentPeerOnly:true});
  }finally{framesA.resolve();framesB.resolve();programA.resolve();programB.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
 // The original schedule owner may return after this demand has retired or
 // after a newer source event. That completion cannot replace the current cache
 // and make the next explicit, current user intent fail source validation.
 for(const boundary of ['peer-change','pause-reopen','settings-event']){
  const held=defer();let f,loads=0;
  const owner={
   getContinuousPerformanceSnapshot:async()=>null,
   updateContinuousPerformanceControls:async()=>{throw Error('No performance write in schedule read test');},
   loadPerformanceSchedule:async()=>{loads++;return loads===1?held.promise:structuredClone(f.state.schedule);},
  };
  f=await createMirrorHostFixture({performanceOwner:owner});
  try{
   f.state.game=null;
   f.state.schedule.controls={version:1,ac:{mode:'auto',tdp:21,tdpCeiling:55},dc:{mode:'auto',tdp:17,tdpCeiling:30}};
   const old=structuredClone(f.state.schedule);
   f.ready();const first=request(f);await flush();assert.equal(loads,1);
   f.state.schedule.controls.ac.tdp=29;
   const currentPeer={...peer,connection:boundary==='peer-change'?'2':'1'};
   if(boundary==='peer-change'){
    f.emit('deckySidebar.mirrorPeer',currentPeer);
    f.emit('deckySidebar.mirrorRequest',{...currentPeer,request:{command:'snapshot',runId}});
    await flush();assert.equal(loads,2);
   }else if(boundary==='pause-reopen'){
    f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});
    await flush();
    f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});
    await flush();assert.equal(loads,1,'The existing in-flight read remains single-owner');
   }else{
    f.scheduleChanged();await flush();assert.equal(loads,1);
   }
   held.resolve(old);await first;await flush();
   // An explicit post-completion read exposes cache poisoning even when the
   // stale response itself was correctly withheld from its retired peer.
   f.emit('deckySidebar.mirrorRequest',{...currentPeer,request:{command:'snapshot',runId}});
   await flush();
   const view=f.replies.filter(row=>row.connection===currentPeer.connection&&row.message.type==='snapshot').at(-1)?.message.snapshot;
   assert(view?.ready);assert(view.actions.global);assert.equal(view.game,null);
   const id='schedule-intent:1';
   f.emit('deckySidebar.mirrorRequest',{...currentPeer,request:{command:'global.setField',runId,id,clientId:'schedule-intent',sequence:1,
    generation:view.generation,revision:view.revision,identity:'',args:{field:'gyroEnabled',value:'on'}}});
   await flush();
   const reply=f.replies.filter(row=>row.connection===currentPeer.connection&&row.message.type==='reply'&&row.message.id===id).at(-1)?.message;
   const measurement={boundary,loads,currentReply:reply??null,inputSaves:f.counts.inputSaves,
    profileSaves:f.counts.saves,performanceWrites:f.counts.performanceUpdates??0,frameWrites:f.counts.frameApplies,fxWrites:f.counts.fxSoundWrites};
   console.log(JSON.stringify({retiredScheduleMeasurement:measurement}));
   assert.equal(reply?.ok,true,'current global intent must remain accepted after a retired schedule read');
   assert.equal(f.counts.inputSaves,1,'Only the explicit current user intent may save through its original input owner');
   assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.counts.frameApplies,0);assert.equal(f.counts.fxSoundWrites,0);
  }finally{held.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);assert.deepEqual(f.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});}
 }
});
await check('a control owner error disables only the related operation and retains other fresh fields',async()=>{const f=await createMirrorHostFixture({readHooks:{monitor:async()=>{throw Error('Memory monitor offline');}}});try{f.ready();const snapshot=await request(f);assert(snapshot.ready);assert.equal(snapshot.actions.monitor,false);assert(snapshot.frames);assert(snapshot.touchpads);assert.equal(snapshot.actions.game,true);assert(f.diagnostics.some(x=>x.event==='snapshot-read-finished'&&x.stage==='partial'));}finally{f.stop();}});
await check('after hiding, diagnostic and owner observers stay idle despite source event storms',async()=>{const f=await createMirrorHostFixture();try{f.ready();await request(f);f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();const reads=f.counts.power,logs=f.diagnostics.length;for(let i=0;i<1000;i++){f.trainerChanged();f.scheduleChanged();f.monitorChanged();}await flush();assert.equal(f.counts.power,reads);assert.equal(f.diagnostics.length,logs);assert.equal(f.observerStats().schedule,0);}finally{f.stop();} const cleanupMeasurements=[];
 for(const boundary of ['pause','disconnect','dispose'])for(const failUnsubscribe of [false,true]){
  let releaseCalls=0;
  const h=await createMirrorHostFixture({policyReadiness:{isGameStatusCurrent:()=>true,subscribeGameTargetReadiness:()=>()=>{releaseCalls++;if(failUnsubscribe)throw Error('inert readiness unsubscribe failure');}}});
  try{
   h.ready();await flush();
   const request=subscribe=>h.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,...(subscribe===undefined?{}:{subscribe})}});
   const cycles=boundary==='pause'?4:1;
   for(let cycle=0;cycle<cycles;cycle++){
    request();await flush();assert.equal(h.observerStats().watchers,1,'one current host observation after opening');
    if(boundary==='pause')request(false);else if(boundary==='disconnect')h.emit('deckySidebar.mirrorPeer',{...peer,connected:false});else h.stop();
    await flush();
    const stats=h.observerStats(),row={boundary,failUnsubscribe,cycle,releaseCalls,...stats,controllerTimers:h.controllerTimerCount(),audioTimers:h.fxTimerCount(),readinessSubscribers:h.readinessSubscriberCount()};
    cleanupMeasurements.push(row);console.log(JSON.stringify({observerCleanupMeasurement:row}));
    assert.equal(stats.watchers,0,'a failed readiness unsubscribe must still release later host observers');
    assert.equal(stats.game,0);assert.equal(stats.schedule,0);assert.equal(stats.fan,0);assert.equal(row.controllerTimers,0);assert.equal(row.audioTimers,0);assert.equal(row.readinessSubscribers,0);
    assert.equal(stats.ipc,boundary==='dispose'?0:2,'only native peer/request listeners remain until complete host stop');
    const reads=h.counts.power;for(let n=0;n<1000;n++){h.powerChanged();h.scheduleChanged();h.trainerChanged();}await flush();assert.equal(h.counts.power,reads);
   }
   for(const key of ['saves','fanSaves','inputSaves','monitorSaves','trainerLaunches','fxSoundWrites'])assert.equal(h.counts[key],0,'cleanup must not issue business writes: '+key);
  }finally{h.stop();}
  assert.deepEqual(h.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});
 }
 assert.equal(cleanupMeasurements.length,12);
});
function clientFixture(buildId){const sent=[],timers=new Map(),handlers=new Map();let next=0;const socket={send:s=>sent.push(JSON.parse(s)),close(){handlers.get('close')?.();},addEventListener:(n,fn)=>handlers.set(n,fn)};const client=new MirrorClient({bootstrap:()=>({endpoint:'ws://127.0.0.1:12345/mirror',runId,token:'TEST-ONLY'}),socket:()=>socket,clientId:'build',buildId,schedule:(fn,ms)=>{timers.set(++next,{fn,ms});return next;},cancel:id=>timers.delete(id)});return {client,sent,timers,handlers};}
await check('compiled fingerprint accompanies read-only requests and never adds a discovery connection',()=>{const id='a'.repeat(64),f=clientFixture(id);try{f.client.setVisible(true);f.handlers.get('open')();assert.equal(f.sent.length,1);assert.equal(f.sent[0].buildId,id);assert.equal(f.sent[0].command,'snapshot');f.client.setVisible(false);assert.equal(f.timers.size,0);}finally{f.client.dispose();}});
await check('unbuilt or malformed fingerprint is omitted for legacy clients',()=>{for(const id of [undefined,'development','C:/private/path','A'.repeat(64),'a'.repeat(65)]){const f=clientFixture(id);try{f.client.setVisible(true);f.handlers.get('open')();assert.equal('buildId' in f.sent[0],false);}finally{f.client.dispose();}}});
await check('an in-flight snapshot retains no diagnostics metadata in its public payload',async()=>{const f=relayFixture(async trace=>{await trace.measure('input',async()=>{});return data();});try{await f.open();const text=JSON.stringify(f.replies.at(-1).snapshot);assert(!text.includes('readId'));assert(!text.includes('powerMs'));assert(!text.includes('demandEpoch'));assert(!text.includes('inputMs'));}finally{f.dispose();}});

await check('read recovery: each measured owner failure is retried without treating its partial cache as settled',async()=>{
 for(const stage of ['power','powerSource','schedule','performance','topology','gyro','frames','touchpads','global','monitor','input']){
  let fail=true;const f=relayFixture(async trace=>{try{await trace.measure(stage,async()=>{if(fail)throw Error('PRIVATE transient field');});}catch{}return {...data(),actions:{monitor:!fail}};});
  try{await f.open();assert.equal(f.replies.at(-1).snapshot.ready,true);assert.equal(f.replies.at(-1).snapshot.actions.monitor,false);
   assert.equal(f.timers.size,1,stage+' must retain a single read-recovery lease');assert.equal([...f.timers.values()][0].ms,250);
   fail=false;await f.fire(250);assert.equal(f.replies.at(-1).snapshot.actions.monitor,true);assert.equal(f.timers.size,0);
   assert(!JSON.stringify(f.logs).includes('PRIVATE'));
  }finally{f.dispose();}
 }
});
for(const kind of ['exception','readiness'])await check('read recovery: '+kind+' lasting beyond two fast attempts still recovers without another event',async()=>{
 let fail=true;const f=relayFixture(async()=>{if(fail&&kind==='exception')throw Error('PRIVATE transient read');return {...data(),ready:!fail};});
 try{await f.open();await f.fire(250);await f.fire(1000);assert.equal(f.reads,3);assert.equal(f.timers.size,1);
  assert.equal([...f.timers.values()][0].ms,3000);fail=false;await f.fire(3000);assert.equal(f.reads,4);assert.equal(f.replies.at(-1).snapshot.ready,true);assert.equal(f.timers.size,0);
 }finally{f.dispose();}
});
await check('read recovery: persistent faults share one capped timer and repeated demand does not reset backoff',async()=>{
 const f=relayFixture(async()=>{throw Error('PRIVATE offline');});try{
  await f.open();for(const delay of [250,1000,3000,10000,30000]){assert.equal(f.timers.size,1);await f.fire(delay);}
  for(let n=0;n<100;n++){assert.equal(f.timers.size,1);assert.equal([...f.timers.values()][0].ms,30000);await f.fire(30000);}
  for(let n=0;n<10;n++)await f.open();assert.equal(f.timers.size,1);await f.fire(30000);assert.equal([...f.timers.values()][0].ms,30000);
 }finally{f.dispose();assert.equal(f.timers.size,0);}
});
for(const boundary of ['pause-reopen','peer-change'])await check('read recovery: dispatched callback from '+boundary+' cannot consume the current lease',async()=>{
 let fail=true;const f=relayFixture(async()=>({...data(),ready:!fail}));try{
  await f.open();const old=[...f.timers.values()][0];assert(old);
  if(boundary==='pause-reopen'){await f.pause();await f.open();}
  else{f.relay.connected({...peer,connection:'2'});await f.relay.request({...peer,connection:'2',request:{command:'snapshot',runId}});}
  const current=[...f.timers.entries()][0],reads=f.reads;assert(current);old.fn();await flush();assert.equal(f.reads,reads);
  assert.deepEqual([...f.timers.keys()],[current[0]]);fail=false;await f.fire(250);assert.equal(f.replies.at(-1).snapshot.ready,true);assert.equal(f.timers.size,0);
 }finally{f.dispose();}
 if(boundary!=='pause-reopen')return;
 for(const ending of ['reopen','hide','same-demand','reopen-again']){
  const gates=Array.from({length:4},defer);let active=0,peak=0,reads=0;
  const x=relayFixture(async()=>{
   const generation=++reads;peak=Math.max(peak,++active);
   try{await gates[generation-1]?.promise;return {...data(),generation,provenance:{kind:'ymcc-native',pid:123}};}
   finally{active--;}
  });
  const probeId='probe:reopen-last-read:1';
  const reopen=()=>x.relay.request({...peer,request:{command:'snapshot',runId,probeId}});
  const requests=[];
  try{
   requests.push(x.open());
   for(let i=0;i<2;i++){
    assert.equal(x.reads,i+1);await x.invalidate();gates[i].resolve();await flush();
   }
   assert.equal(x.reads,3);assert.equal(x.replies.length,0);
   if(ending==='same-demand')await x.invalidate();
   else{await x.pause();if(ending!=='hide')requests.push(reopen());}
   assert.equal(x.reads,3,'A reopened sidebar must not run a second owner read concurrently');
   gates[2].resolve();await flush();
   const handoff=ending==='reopen'||ending==='reopen-again';
   console.log(JSON.stringify({ending,reads:x.reads,peak,timers:[...x.timers.values()].map(timer=>timer.ms),replies:x.replies.length}));
   assert.equal(x.reads,handoff?4:3,'A newly opened demand gets its own first read before any retry timer');
   if(ending==='reopen-again'){await x.pause();requests.push(reopen());}
   gates[3].resolve();await Promise.all(requests);await flush();
   assert.equal(peak,1);
   if(ending==='reopen'){
    assert.deepEqual(x.replies.map(r=>[r.probeId,r.snapshot.generation,r.snapshot.ready]),[[probeId,4,true]]);
    assert.equal(x.timers.size,0);
   }else if(ending==='hide'){
    assert.equal(x.replies.length,0);assert.equal(x.timers.size,0);
   }else{
    assert(x.replies.length>0&&x.replies.every(r=>!r.snapshot.ready&&r.snapshot.provenance?.kind==='unconfirmed'));
    assert.deepEqual([...x.timers.values()].map(timer=>timer.ms),[250]);
    assert.equal(x.reads,handoff?4:3,'Churn cannot extend the inline read indefinitely');
    await x.fire(250);assert.equal(x.replies.at(-1).snapshot.ready,true);assert.equal(x.timers.size,0);
   }
  }finally{gates.forEach(gate=>gate.resolve());x.dispose();await Promise.all(requests);await flush();}
  assert.equal(active,0);assert.equal(x.timers.size,0);
 }
});
await check('read recovery: healthy disabled controls are not mistaken for a failed read or polled',async()=>{
 const f=relayFixture(async()=>({...data(),actions:{monitor:false,performance:false}}));try{
  await f.open();for(let n=0;n<1000;n++)await f.invalidate();assert.equal(f.timers.size,0);assert.equal(f.replies.at(-1).snapshot.ready,true);
 }finally{f.dispose();}

 const gate=defer();let hold=false,held=false;
 const host=await createMirrorHostFixture({reactiveInputs:true,readHooks:{performance:async()=>{if(hold&&!held){held=true;await gate.promise;}}}});
 const snapshots=()=>host.replies.filter(row=>row.message.type==='snapshot');
 const latest=()=>snapshots().at(-1)?.message.snapshot;
 const counts=()=>[host.counts.power,host.counts.schedule,host.counts.inputReads,host.counts.frameReads,snapshots().length];
 const delta=start=>counts().map((value,index)=>value-start[index]);
 try{
  host.state.fan.controlReady=false;
  host.ready();await flush();const idleObservers=host.observerStats();
  await request(host);assert.equal(latest().fan.supported,false);assert.equal(latest().powerSource,'ac');
  assert.equal(host.observerStats().watchers,1);
  let start=counts();
  for(let i=0;i<20;i++){host.state.fan.featureEnabled=i%2===1;await flush();}
  const unchanged=delta(start);assert.equal(latest().fan.supported,false);

  // Work already in flight must not be retried on an equivalent fan view.
  start=counts();hold=true;host.state.monitor.enabled=false;host.monitorChanged();await flush();assert(held);
  host.state.fan.featureEnabled=false;await flush();gate.resolve();await flush();
  const unchangedInFlight=delta(start);

  // Real preset and power-side changes still publish, even without fan support.
  start=counts();host.state.fan.preset='soft';await flush();assert.equal(latest().fan.preset,'soft');
  const preset=delta(start);
  start=counts();host.state.powerSource='dc';await flush();assert.equal(latest().powerSource,'dc');assert.equal(latest().actions.frames,true);
  const dc=delta(start);
  start=counts();host.state.powerSource=null;await flush();assert.equal(latest().powerSource,null);assert.equal(latest().actions.frames,false);
  const unknown=delta(start);
  start=counts();host.state.powerSource='ac';host.state.fan.preset='aggressive';await flush();
  assert.equal(latest().powerSource,'ac');assert.equal(latest().fan.preset,'aggressive');assert.equal(latest().actions.frames,true);
  const combined=delta(start);
  start=counts();host.state.fan.preset='balanced';host.state.powerSource='dc';
  host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();
  const afterUnsubscribe=delta(start);assert.deepEqual(host.observerStats(),idleObservers);
  await request(host);assert.equal(latest().powerSource,'dc');assert.equal(latest().fan.preset,'balanced');
  assert.equal(host.controllerTimerCount(),0);assert.equal(host.counts.fanSaves,0);assert.equal(host.counts.saves,0);assert.equal(host.counts.inputSaves,0);
  const measured={unchanged,unchangedInFlight,preset,dc,unknown,combined,afterUnsubscribe};
  console.log(JSON.stringify({fanPowerWatchMeasurement:measured,columns:['powerReads','scheduleReads','inputReads','frameReads','snapshotPublications']}));
  const zero=[0,0,0,0,0],one=[1,1,1,1,1];
  assert.deepEqual(measured,{unchanged:zero,unchangedInFlight:one,preset:one,dc:one,unknown:one,combined:one,afterUnsubscribe:zero},'Equivalent fan input must not make, retire, or publish a full snapshot');
 }finally{gate.resolve();host.stop();await flush();assert.deepEqual(host.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});}
});
await check('read recovery: pause and disposal release the single timer and late callbacks remain inert',async()=>{
 for(const boundary of ['pause','disconnect','dispose']){
  const f=relayFixture(async()=>({...data(),ready:false}));try{await f.open();const old=[...f.timers.values()][0];assert(old);
   if(boundary==='pause')await f.pause();else if(boundary==='disconnect')f.relay.connected({...peer,connected:false});else f.dispose();assert.equal(f.timers.size,0);const reads=f.reads;old.fn();await flush();assert.equal(f.reads,reads);assert.equal(f.timers.size,0);
  }finally{f.dispose();}
 }
});
for(const field of ['monitor','performance'])await check('read recovery: actual host restores '+field+' after its owner returns without a source event',async()=>{
 let fail=false;const f=await createMirrorHostFixture({readHooks:{[field]:async()=>{if(fail)throw Error('PRIVATE owner unavailable');}}});
 try{f.ready();await request(f);await flush();fail=true;f.monitorChanged();await flush();
  let snapshot=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(snapshot.ready,true);assert.equal(snapshot.actions[field],false);assert.equal(snapshot.actions.game,true);assert.equal(f.controllerTimerCount(),1);
  fail=false;await flush();assert.equal(f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot.actions[field],false);
  f.advanceController(250);await flush();snapshot=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(snapshot.actions[field],true);assert.equal(f.controllerTimerCount(),0);
  for(const name of ['saves','inputSaves','monitorSaves','monitorApplies','performanceUpdates','frameApplies','fanSaves'])assert.equal(f.counts[name]??0,0,name);
 }finally{f.stop();assert.equal(f.controllerTimerCount(),0);}
});
await check('read recovery: a held owner read cannot block Decky across same-generation sleep',async()=>{
 const gate=defer();let hold=false,held=false;
 const h=await createMirrorHostFixture({readHooks:{performance:async()=>{if(hold&&!held){held=true;await gate.promise;}}}});
 const snapshots=()=>h.replies.filter(row=>row.message.type==='snapshot');
 const latest=()=>snapshots().at(-1)?.message.snapshot;
 const publish=(revision,change)=>{h.state.power={...h.state.power,...change,known:true,publicationRevision:revision,observedNotificationSequence:revision};h.emit('power.lifecycleChanged',{...h.state.power});};
 try{
  h.state.power.suspendSerial=1;h.ready();await request(h);await flush();
  const old=latest();assert(old?.ready);
  hold=true;h.state.monitor.enabled=!h.state.monitor.enabled;h.monitorChanged();await flush();assert(held);
  const id='sleep:1';
  h.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'monitor.setEnabled',runId,id,clientId:'sleep',sequence:1,generation:old.generation,revision:old.revision,args:{enabled:old.monitor.enabled}}});
  await flush();assert.equal(h.replies.some(row=>row.message.id===id),false,'the pre-sleep request is waiting for read admission');
  publish(2,{suspendSerial:2,phase:'suspending',hardwareWritesAllowed:false,inputReady:false});await flush();
  assert.equal(latest()?.ready,false,'the public suspend must close the old display without waiting for the owner read');
  assert.equal(h.replies.find(row=>row.message.id===id)?.message.ok,false,'the pre-sleep request must receive a refusal before the old read returns');
  publish(3,{phase:'ready',hardwareWritesAllowed:true,inputReady:true});await flush();
  const current=latest();assert(current?.ready,'Ready must publish while the pre-sleep owner is still held');
  assert.equal(current.generation,old.generation);assert(current.revision>old.revision);assert.equal(current.actions.performance,true);
  const publications=snapshots().length,reads=h.counts.power;
  gate.resolve();await flush();
  assert.equal(snapshots().length,publications,'late owner completion cannot publish a retired snapshot');
  assert.equal(h.counts.power,reads,'late owner completion must not start another read');
  assert.equal(h.controllerTimerCount(),0);
  for(const name of ['saves','inputSaves','monitorSaves','monitorApplies','performanceUpdates','frameApplies','fanSaves'])assert.equal(h.counts[name]??0,0,name);
 }finally{gate.resolve();h.stop();await flush();assert.equal(h.sharedPowerTimerCount(),0);assert.equal(h.sharedPowerDiagnostics().subscriberCount,0);}
});
await check('read recovery: mirror follows suspend scope and ignores a delayed derived pending event',async()=>{
 const h=await createMirrorHostFixture();
 const latest=()=>h.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;
 const publish=(revision,change)=>{h.state.power={...h.state.power,...change,known:true,publicationRevision:revision,observedNotificationSequence:revision};h.emit('power.lifecycleChanged',{...h.state.power});};
 try{
  h.state.power.suspendSerial=1;h.ready();await request(h);await flush();assert(latest()?.ready);
  const reads=h.counts.power;
  h.emit('power.pending',{generation:1,suspendSerial:0,publicationRevision:0});await flush();
  assert.equal(h.counts.power,reads,'a delayed derived event cannot retire settled Ready');
  const previous=latest().revision;
  publish(2,{suspendSerial:2});await flush();
  assert(latest().ready);assert(latest().revision>previous,'same-generation suspend scope retires the old mirror read');
  const before={input:h.counts.inputReads,frames:h.counts.frameReads,monitor:h.counts.monitorReads};
  publish(3,{notificationPending:true});await flush();
  assert.equal(latest().ready,false);assert.equal(latest().actions.game,false);
  assert.deepEqual({input:h.counts.inputReads,frames:h.counts.frameReads,monitor:h.counts.monitorReads},before);
  publish(4,{notificationPending:false});await flush();assert(latest().ready);
  assert.equal(h.counts.saves,0);assert.equal(h.counts.performanceUpdates??0,0);
 }finally{h.stop();assert.equal(h.sharedPowerTimerCount(),0);assert.equal(h.sharedPowerDiagnostics().subscriberCount,0);}
});
await check('read recovery: missed Ready notification refreshes native facts without replaying a control write',async()=>{
 const f=await createMirrorHostFixture();try{f.ready();await request(f);await flush();
  f.state.power={...f.state.power,generation:2,phase:'resuming',hardwareWritesAllowed:false};f.powerChanged(true);await flush();
  assert.equal(f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot.ready,false);assert.equal(f.controllerTimerCount(),1);
  f.state.power={...f.state.power,phase:'ready',hardwareWritesAllowed:true};f.advanceController(250);await flush();
  const snapshot=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;assert.equal(snapshot.ready,true);assert.equal(snapshot.generation,2);assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.controllerTimerCount(),0);
 }finally{f.stop();assert.equal(f.sharedPowerTimerCount(),0);assert.equal(f.sharedPowerDiagnostics().subscriberCount,0);}

 // The real shared source receives a changed fact before an older native read
 // returns. That old Ready must not enter any business readers. Then native
 // Ready is observed by the existing read recovery without a Ready event.
 const gate=defer();let held=false,h;
 h=await createMirrorHostFixture({readHooks:{power:async()=>{if(!held){held=true;const old=structuredClone(h.state.power);await gate.promise;return old;}}}});
 try{
  h.ready();const pending=request(h);await flush();assert(held);
  h.state.power={...h.state.power,generation:2,phase:'resuming',hardwareWritesAllowed:false,inputReady:false};
  h.emit('power.lifecycleChanged',{...h.state.power,known:true,publicationRevision:2});await flush();
  gate.resolve();await pending;await flush();
  const waiting=h.replies.filter(r=>r.message.type==='snapshot').at(-1)?.message.snapshot;assert(waiting&&!waiting.ready);assert.equal(waiting.generation,2);
  assert.deepEqual({input:h.counts.inputReads,frames:h.counts.frameReads,monitor:h.counts.monitorReads},{input:0,frames:0,monitor:0},'A superseded Ready read must stop before the parallel business batch');
  h.state.power={...h.state.power,phase:'ready',hardwareWritesAllowed:true,inputReady:true};h.advanceController(250);await flush();
  const current=h.replies.filter(r=>r.message.type==='snapshot').at(-1)?.message.snapshot;assert(current?.ready);assert.equal(current.generation,2);
  const counts={input:h.counts.inputReads,frames:h.counts.frameReads,monitor:h.counts.monitorReads};assert.deepEqual(counts,{input:1,frames:1,monitor:1});
  const reads=h.counts.power;h.advanceSharedPower(750);await flush();
  assert.equal(h.sharedPowerDiagnostics().snapshot.phase,'ready');assert.equal(h.sharedPowerTimerCount(),0);
  assert.deepEqual({input:h.counts.inputReads,frames:h.counts.frameReads,monitor:h.counts.monitorReads},counts,'Later same-state shared confirmation must not reread all controls');assert.equal(h.counts.power,reads);
  for(let n=0;n<1000;n++)h.emit('power.lifecycleChanged',{...h.state.power,known:true,publicationRevision:3+n});await flush();
  assert.equal(h.counts.power,reads);assert.equal(h.controllerTimerCount(),0);assert.equal(h.sharedPowerTimerCount(),0);
  for(const name of ['saves','inputSaves','performanceUpdates','frameApplies','monitorSaves','monitorApplies','fxSoundWrites'])assert.equal(h.counts[name]??0,0,name);
  console.log(JSON.stringify({sharedPowerHostMeasurement:{boundary:'changed-before-native-return-then-missed-Ready',counts,nativeSnapshotReads:h.counts.power,sharedPowerReads:h.counts.sharedPowerReads,sharedDiagnostics:h.sharedPowerDiagnostics(),duplicatePublications:1000,businessWrites:0}}));
  const retired=h.diagnostics.filter(row=>row.event==='snapshot-read-finished'&&row.discarded);
  assert.equal(retired.length,1);
  assert.equal(retired[0].failureKind,'source-changed');assert.equal(retired[0].discardReason,'source-changed');
  const currentReceipt=h.diagnostics.find(row=>row.event==='snapshot-read-finished'&&!row.discarded);
  assert.ok(currentReceipt);assert.equal(currentReceipt.invalidatedBy,undefined,'The current retry must not inherit the retired read cause');
  console.log(JSON.stringify({sharedPowerDiscardMeasurement:{discardedReads:retired.length,source:retired[0].invalidatedBy,currentRetryTagged:'invalidatedBy' in currentReceipt}}));
  assert.equal(retired[0].invalidatedBy,'power','A newer shared power fact that supersedes the direct read must identify its source');
 }finally{gate.resolve();h.stop();await flush();assert.equal(h.sharedPowerTimerCount(),0);assert.equal(h.sharedPowerDiagnostics().subscriberCount,0);assert.equal(h.controllerTimerCount(),0);assert.equal(h.fxTimerCount(),0);assert.equal(h.observerStats().ipc,0);}
});

await check('read recovery: unrelated monitor intent still reaches its owner during a performance outage and is not replayed',async()=>{
 const f=await createMirrorHostFixture();try{f.ready();await request(f);await flush();f.state.performanceReadError=true;f.monitorChanged();await flush();
  const snapshot=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(snapshot.actions.performance,false);assert.equal(snapshot.actions.monitor,true);const enabled=!snapshot.monitor.enabled;
  f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'monitor.setEnabled',runId,id:'readfix:1',clientId:'readfix',sequence:1,generation:snapshot.generation,revision:snapshot.revision,args:{enabled}}});await flush();
  const reply=f.replies.find(r=>r.message.type==='reply'&&r.message.id==='readfix:1')?.message;assert.equal(reply?.ok,true);
  assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);assert.equal(f.counts.performanceUpdates??0,0);
  f.advanceController(250);await flush();assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);
  f.state.performanceReadError=false;f.advanceController(1000);await flush();assert.equal(f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot.actions.performance,true);
  assert.equal(f.counts.monitorSaves,1);assert.equal(f.controllerTimerCount(),0);
 }finally{f.stop();}

 // A real admission edge must remain observable during an unrelated long fault.
 // Reuse this original case, the same Host clock, and original owner IO seams.
 const latest=f=>f.replies.filter(r=>r.message.type==='snapshot').at(-1)?.message.snapshot;
 const capped=async f=>{f.ready();await request(f);f.state.performanceReadError=true;f.monitorChanged();await flush();for(const delay of [250,1000,3000,10000]){f.advanceController(delay);await flush();}assert.equal(f.controllerTimerCount(),1);};
 {
  const f=await createMirrorHostFixture();try{
   await capped(f);const beforeReads=f.counts.power;
   const startProtection={locked:true,requestId:11,phase:'pending',persona:'steamdeck',remainingMs:1000,elapsedMs:0,maxMs:30000,settleMs:10000};
   f.state.gamepadState={virtualOutput:{state:'active',requestedPersona:'steamdeck',boundPersona:'steamdeck',hostAlive:true,hostEpoch:1,prepared:true,neutralized:true,frameAccepted:true,sourceLive:true,sourceCurrent:true,steamRunning:true,startProtection}};
   f.emit('gamepad.state',f.state.gamepadState);await flush();let snapshot=latest(f);
   assert.equal(snapshot.virtualController?.locked,true,'Current native controller lock cannot wait for an unrelated 30-second fault read');
   assert.equal(snapshot.ready,true);assert.equal(snapshot.actions.performance,false);assert.equal(snapshot.actions.monitor,true);
   assert.equal(snapshot.game.fields.padPersona.supported,false);assert.equal(f.counts.power,beforeReads+1,'One actual edge owns one fresh batch');
   const reads=f.counts.power;
   for(let i=0;i<100;i++)f.emit('gamepad.state',{virtualOutput:{...f.state.gamepadState.virtualOutput,packetCount:i,startProtection:{...startProtection,remainingMs:1000-i,elapsedMs:i}}});
   await flush();assert.equal(f.counts.power,reads,'Equivalent telemetry cannot bypass fault backoff');assert.equal(f.controllerTimerCount(),2);
   f.advanceController(1000);await flush();snapshot=latest(f);
   assert.equal(snapshot.virtualController?.locked,false,'Original controller UI deadline must publish while the other owner remains unavailable');
   assert.equal(snapshot.ready,true);assert.equal(snapshot.actions.performance,false);assert.equal(snapshot.actions.monitor,true);assert.equal(f.controllerTimerCount(),1);
   assert.equal(f.counts.power,reads+1,'The deadline owns one refresh, not a control retry');
   const beforeOriginalDeadline=f.counts.power;f.advanceController(28999);await flush();assert.equal(f.counts.power,beforeOriginalDeadline,'Admission changes cannot refund or shorten the capped fault lease');
   f.advanceController(1);await flush();assert.equal(f.counts.power,beforeOriginalDeadline+1,'Original capped read lease still expires at its original time');
   assert.equal(f.controllerTimerCount(),1);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{f.stop();assert.equal(f.controllerTimerCount(),0);}
 }
 {
  let active=true,disableCalls=0;
  const life={controlReady:false,state:'awaiting-control',hasControlIntent:true,recoveryActive:false,controlCancellationRevision:0,
   start:async()=>{throw Error('Unexpected automatic ON');},apply:async()=>{throw Error('Unexpected curve write');},applyPreset:async()=>{throw Error('Unexpected preset');},
   disable:async()=>{disableCalls++;life.controlCancellationRevision++;life.hasControlIntent=false;life.controlReady=false;return {state:'AwaitingControl'};}};
  const feature={FAN_IMPORT_ENABLED:true,FAN_FORCE_PREVIEW:false,fanFeatureEnabled:{value:true},fanControlActive:{get value(){return active;}},getFanSelectedPreset:()=> 'balanced',getFanFeatureSettings:()=>({preset:'balanced'}),getFanPresetCurve:()=>[],saveFanCurve:async()=>{throw Error('Unexpected fan save');},setFanControlActive:value=>{active=value;},setFanNavigationDuty:()=>{},recordFanHandshake:async()=>{}};
  const f=await createMirrorHostFixture({fanOwner:{lifecycle:life,featureBindings:feature,close:async()=>{}}});try{
   await capped(f);const old=latest(f),reads=f.counts.power;life.controlReady=true;life.state='ready';f.emit('fan.resume-ready',{generation:1});await flush();const snapshot=latest(f);
   assert.equal(snapshot.fan.supported,true,'Actual fan owner readiness cannot wait for an unrelated fault');assert.equal(snapshot.actions.fan,true);assert.equal(snapshot.fan.canToggle,true);
   assert.equal(snapshot.actions.performance,false);assert.equal(f.counts.power,reads+1);
   const stable=f.counts.power;for(let i=0;i<100;i++)f.emit('fan.resume-ready',{generation:1});await flush();assert.equal(f.counts.power,stable,'Duplicate readiness signals cannot start additional batches');assert.equal(f.controllerTimerCount(),1);
   const off={command:'fan.setEnabled',runId,id:'edge:1',clientId:'edge',sequence:1,generation:old.generation,revision:old.revision,args:{enabled:false}};
   f.emit('deckySidebar.mirrorRequest',{...peer,request:off});await flush();assert.equal(f.replies.find(r=>r.message.id===off.id)?.message.ok,false,'Stale delivered authority is still rejected');assert.equal(disableCalls,0);
   const current=latest(f),fresh={...off,id:'edge:2',sequence:2,generation:current.generation,revision:current.revision};
   f.emit('deckySidebar.mirrorRequest',{...peer,request:fresh});await flush();assert.equal(f.replies.find(r=>r.message.id===fresh.id)?.message.ok,true);assert.equal(disableCalls,1,'Explicit OFF reaches the original fan owner once');
   f.emit('deckySidebar.mirrorRequest',{...peer,request:fresh});await flush();assert.equal(disableCalls,1,'Duplicate command returns its old receipt without replay');
   f.advanceController(30000);await flush();assert.equal(disableCalls,1);assert.equal(active,false);assert.equal(life.hasControlIntent,false);
   assert.equal(f.counts.fanSaves,0);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{f.stop();assert.equal(f.controllerTimerCount(),0);}
 }
});
await check(speedCompletionCase,async()=>{
 if(!speedCompletionOnly){
 const gate=defer();let hold=false,started=0;const f=await createMirrorHostFixture({readHooks:{performance:async()=>{if(hold){started++;await gate.promise;}}}});
 try{f.ready();await request(f);await flush();f.state.performanceReadError=true;f.monitorChanged();await flush();
  hold=true;f.advanceController(250);await flush();assert.equal(started,1);const old=f.state.game;
  f.state.game={...old,pid:202,processCreated:'birth-B',path:'C:/Other/GameB.exe'};f.state.power={...f.state.power,generation:2};
  f.gameChanged();f.powerChanged();f.state.performanceReadError=false;hold=false;const boundary=f.replies.length;gate.resolve();await flush();
  const snapshots=f.replies.slice(boundary).filter(r=>r.message.type==='snapshot').map(r=>r.message.snapshot);assert(snapshots.length);
  assert(snapshots.every(s=>s.generation===2&&s.game?.identity.includes('202')));assert.equal(snapshots.at(-1).actions.performance,true);
  assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.controllerTimerCount(),0);
 }finally{gate.resolve();f.stop();}

 // Global audio intent has no game identity, but its native transaction must
 // still retain the generation admitted before an asynchronous state read.
 for(const change of ['generation','native-dispatch-generation','gate','peer','source']){
  const gate=defer();let hold=false,entered=false;const calls=[];let applications=0;
  const f=await createMirrorHostFixture({readHooks:{fxSound:async()=>{if(hold){entered=true;await gate.promise;}}}});
  const A={...peer,connectionRevision:1};
  try{
   f.ready();f.emit('deckySidebar.mirrorRequest',{...A,request:{command:'snapshot',runId}});await flush();
   const original=f.replies.filter(row=>row.message.type==='snapshot').at(-1).message.snapshot;assert(original.actions.audioOptimization);
   // This is the existing native setter port. Its expected/current contract is
   // also compiled from the actual main.cpp validation prefix in native evidence.
   f.state.fxSoundSetHook=async(enabled,expectedGeneration)=>{
    if(change==='native-dispatch-generation')f.state.power={...f.state.power,generation:f.state.power.generation+1};
    const current=f.state.power;calls.push({enabled,expectedGeneration,nativeGeneration:current.generation});
    if(expectedGeneration!==undefined&&expectedGeneration!==current.generation)throw Error('FXSOUND_POWER_NOT_READY');
    const allowed=current.phase==='ready'&&current.hardwareWritesAllowed===true;
    if(allowed){applications++;f.state.fxSound={...f.state.fxSound,enabled,running:enabled};}
    return {ok:allowed,state:structuredClone(f.state.fxSound),copied:false,reused:false,repaired:false,reason:allowed?'Inert native transaction accepted':'Native power gate closed'};
   };
   const intent={...A,request:{command:'audioOptimization.setEnabled',runId,id:'audio-generation:1',clientId:'audio-generation',sequence:1,generation:original.generation,revision:original.revision,args:{enabled:!original.audioOptimization.enabled}}};
   hold=true;f.emit('deckySidebar.mirrorRequest',intent);await flush();assert(entered);assert.equal(calls.length,0);
   if(change==='generation'){f.state.power={...f.state.power,generation:original.generation+1};f.powerChanged();}
   if(change==='gate'){f.state.power={...f.state.power,phase:'resuming',hardwareWritesAllowed:false};f.powerChanged(true);}
   if(change==='peer')f.emit('deckySidebar.mirrorPeer',{...A,connection:'2',connectionRevision:3});
   if(change==='source')f.state.fxSound={...f.state.fxSound,output:'A newer output selected outside Decky'};
   await flush();hold=false;gate.resolve();await flush();
   assert.equal(applications,0,'An old or revoked audio intent must never apply');
   const reply=f.replies.find(row=>row.message.id==='audio-generation:1')?.message;
   if(change==='generation'||change==='native-dispatch-generation'){
    assert.equal(calls.length,1);assert.equal(calls[0].expectedGeneration,original.generation);assert.equal(calls[0].nativeGeneration,original.generation+1);assert.equal(reply?.ok,false);
   }else if(change==='gate'){assert.equal(reply?.result.applied,false);assert.equal(reply?.result.pending,true);}
   else assert.equal(calls.length,0,'Revoked peer/source cannot reach the native transaction');
   if(change!=='peer'){
    const requests=calls.length;f.emit('deckySidebar.mirrorRequest',intent);await flush();assert.equal(calls.length,requests);assert.equal(applications,0,'Duplicate failed intent cannot replay across a wake');
   }
   if(change==='generation'){
    const current=f.replies.filter(row=>row.message.type==='snapshot').at(-1).message.snapshot;
    assert.equal(current.generation,original.generation+1);assert.equal(current.ready,true);
    const fresh={...A,request:{...intent.request,id:'audio-generation:2',sequence:2,generation:current.generation,revision:current.revision}};
    f.emit('deckySidebar.mirrorRequest',fresh);await flush();assert.equal(f.replies.find(row=>row.message.id==='audio-generation:2')?.message.result.applied,true);
    assert.equal(applications,1);assert.equal(calls.at(-1).expectedGeneration,current.generation);
    f.emit('deckySidebar.mirrorRequest',fresh);await flush();assert.equal(applications,1,'Only the new explicit intent applies once');
   }
   audioGenerationMeasurements.push({boundary:change,nativeRequests:calls,applications,oldIntentApplied:false,automaticReplay:false});
   assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.monitorSaves,0);
  }finally{hold=false;gate.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
 {
  const sent=[];const state={known:true,installed:true,running:true,enabled:true,selected:true,preset:'inert',output:'inert',reason:''};
  const owner=load(src('src/bridge/fxSound.ts'),{'./ipc':{invoke:async(command,args)=>{sent.push({command,args});return {ok:true,state,copied:false,reused:false,repaired:false,reason:'Legacy normal YMCC owner'};}}},{clearTimeout:()=>{throw Error('Unexpected legacy owner timer');}});
  const result=await owner.fxSound.setEnabled(true);assert.equal(result.ok,true);assert.deepEqual(structuredClone(sent),[{command:'fxsound.setEnabled',args:{enabled:true}}],'Normal YMCC one-field calls keep their original native API');
  audioGenerationMeasurements.push({boundary:'legacy-normal-owner',optionalGenerationAbsent:true,nativeRequests:1});
 }
 }
 await speedCompletionBoundaries();
});

await check('read recovery: repeated snapshot and source requests cannot bypass the outstanding fault backoff',async()=>{
 const f=relayFixture(async()=>{throw Error('PRIVATE offline');});try{await f.open();const initial=f.reads;
  for(let n=0;n<100;n++){await f.open();await f.invalidate();}
  assert.equal(f.reads,initial,'All repeated demand must share the already scheduled read lease');assert.equal(f.timers.size,1);
  await f.fire(250);assert.equal(f.reads,initial+1);
 }finally{f.dispose();}
});

await check('read recovery: native not-Ready observation does not enter business readers or refresh write admission',async()=>{
 let performanceReads=0;const f=await createMirrorHostFixture({readHooks:{performance:async()=>{performanceReads++;}}});
 try{
  f.state.schedule.controls={version:1,ac:{mode:'auto',tdp:21,tdpCeiling:55},dc:{mode:'auto',tdp:17,tdpCeiling:30}};
  f.ready();await request(f);await flush();const original=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(original.performance.controls.find(row=>row.key==='tdp')?.value,21);
  const before={performanceReads,frames:f.counts.frameReads,monitor:f.counts.monitorReads,input:f.counts.inputReads,audio:f.counts.fxSoundReads};
  const counts=()=>({performanceReads,frames:f.counts.frameReads,monitor:f.counts.monitorReads,input:f.counts.inputReads,audio:f.counts.fxSoundReads});
  f.state.power={...f.state.power,generation:2,phase:'resuming',hardwareWritesAllowed:false};f.powerChanged(true);await flush();
  for(const delay of [250,1000,3000]){f.advanceController(delay);await flush();}
  assert.deepEqual(counts(),before,'Native not-Ready observation must not enter any business reader');
  const held=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(held.ready,false);assert.equal(held.generation,2);assert.equal(held.actions.performance,false);assert.equal(held.actions.game,false);
  // An external settings change while gated cannot be laundered into Ready.
  f.state.schedule.controls.ac.tdp=29;f.scheduleChanged();await flush();assert.deepEqual(counts(),before);
  f.state.power={...f.state.power,phase:'ready',hardwareWritesAllowed:true};f.advanceController(10000);await flush();
  const current=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(current.ready,true);assert.equal(current.generation,2);assert.equal(current.actions.performance,true);assert.equal(current.actions.game,true);
  assert.equal(current.game.identity,original.game.identity);assert.equal(current.performance.controls.find(row=>row.key==='tdp')?.value,29);
  assert.notEqual(current.performance.sourceRevision,original.performance.sourceRevision,'Fresh authority refers to the current owner source');
  assert.deepEqual(counts(),Object.fromEntries(Object.entries(before).map(([key,value])=>[key,value+1])),'One Ready batch consumes fast LS/audio completions without a redundant business batch');
  // Old generation authority must still fail before the original operation owner.
  f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'performance.setControls',runId,id:'ready:1',clientId:'ready',sequence:1,generation:original.generation,revision:original.revision,identity:original.game.identity,args:{patch:{tdp:33}}}});await flush();
  assert.equal(f.replies.find(r=>r.message.id==='ready:1')?.message.ok,false);assert.equal(f.counts.performanceUpdates??0,0);
  assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.controllerTimerCount(),0);
 }finally{f.stop();}
});
await check('read recovery: duplicate probe and native context events cannot create extra read batches',async()=>{
 const f=relayFixture(async()=>{throw Error('PRIVATE offline');});try{await f.open();for(let n=0;n<100;n++)await f.relay.request({...peer,request:{command:'snapshot',runId,probeId:'probe:test:'+n}});
  assert.equal(f.reads,1);assert.equal(f.timers.size,1);await f.fire(250);assert.equal(f.reads,2);
 }finally{f.dispose();}
 const h=await createMirrorHostFixture();try{h.ready();await request(h);await flush();h.state.monitorReadError=true;h.monitorChanged();await flush();h.powerChanged();await flush();
  const reads=h.counts.power;for(let n=0;n<100;n++){h.powerChanged();h.gameChanged();h.monitorChanged();}await flush();assert.equal(h.counts.power,reads);assert.equal(h.controllerTimerCount(),1);
 }finally{h.stop();}

 // The envelope serial is produced by native Broker/WM, never request payload.
 {
  const A={...peer,connectionRevision:1},B={...peer,connection:'2',connectionRevision:3};
  const oldAttach=defer();let attaches=0,current=null,extensionDeliveries=0;
  const connected=[];const f=await createMirrorHostFixture({invoke:async command=>{
   if(command!=='deckySidebar.mirrorAttach')return true;
   if(++attaches===2)await oldAttach.promise;
   return {...A,owner:'YMCC-native',pid:8612};
  }});
  const off=f.registerExtension({prefix:'wallpaper.',connected:value=>{current=value;connected.push({connection:value.connection,connected:value.connected!==false});},request:event=>{if(current?.connected!==false&&event.connection===current?.connection&&event.runId===current?.runId)extensionDeliveries++;}});
  const snapshot=()=>f.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;
  try{
   await flush();f.ready();await flush();assert.equal(snapshot()?.ready,true);assert.equal(connected.length,1);
   // Native B request arrives while its peer notification is missing.
   f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'snapshot',runId}});await flush();
   assert.equal(f.replies.filter(row=>row.message.type==='snapshot').at(-1)?.connection,'2','Current authenticated B request repairs a lost peer event');
   assert.equal(snapshot().ready,true);assert.equal(connected.length,2);
   // A missing legacy serial cannot downgrade a confirmed typed peer.
   const legacyBoundary=f.replies.length;
   f.emit('deckySidebar.mirrorPeer',{...peer,connected:true});await flush();
   f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'snapshot',runId}});await flush();
   const afterLegacy=f.replies.slice(legacyBoundary).filter(row=>row.message.type==='snapshot');
   assert(afterLegacy.some(row=>row.connection==='2'),'A serial-less old A notification must not strand confirmed B');
   assert(afterLegacy.every(row=>row.connection==='2'));assert.equal(connected.length,2,'Legacy mismatch must not reset wallpaper peer');

   const boundary=f.replies.length,reads=f.counts.power;
   oldAttach.resolve();await flush();assert.equal(connected.length,2,'Old attach must not reset current wallpaper state');assert.equal(f.counts.power,reads);
   // Already-retired private receipts cannot overtake B, even with client flags.
   f.emit('deckySidebar.mirrorPeer',A);
   f.emit('deckySidebar.mirrorRequest',{...A,request:{command:'snapshot',runId,isCurrent:true,connectionRevision:999999}});await flush();
   assert.equal(connected.length,2);assert.equal(f.counts.power,reads);assert.equal(f.replies.length,boundary);
   for(let n=0;n<100;n++)f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'snapshot',runId}});
   await flush();assert.equal(connected.length,2,'Repeated snapshots never rebroadcast wallpaper.connected');
   for(let n=0;n<100;n++)f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'wallpaper.context',runId,contextSeq:n+1,appId:'42',visible:true}});
   await flush();assert.equal(extensionDeliveries,100);assert.equal(connected.length,2,'Passive context traffic never resets extension ownership');
   const source=snapshot(),intent={...B,request:{command:'monitor.setEnabled',runId,id:'transport:1',clientId:'transport',sequence:1,generation:source.generation,revision:source.revision,args:{enabled:!source.monitor.enabled}}};
   for(let n=0;n<100;n++)f.emit('deckySidebar.mirrorRequest',intent);
   await flush();assert.equal(f.replies.find(row=>row.message.id==='transport:1')?.message.ok,true);assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);assert.equal(connected.length,2);
   // A current native disconnect is authoritative even if that peer's ON was lost.
   f.emit('deckySidebar.mirrorPeer',{...peer,connection:'3',connectionRevision:6,connected:false});await flush();assert.equal(current.connected,false);assert.equal(f.observerStats().watchers,0);assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);
   const afterClose=f.counts.power;f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'snapshot',runId,isCurrent:true}});await flush();assert.equal(f.counts.power,afterClose,'Old request cannot revive demand after current native OFF');
   const D={...peer,connection:'4',connectionRevision:7};f.emit('deckySidebar.mirrorRequest',{...D,request:{command:'snapshot',runId}});await flush();assert.equal(snapshot().ready,true);assert.equal(current.connection,'4');assert.equal(connected.length,4);
   assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
   assert(f.replies.slice(boundary).filter(row=>row.message.type==='snapshot').every(row=>row.connection!=='1'));
   transportMeasurements.push({boundary:'missed-peer-late-attach-and-duplicates',connected,extensionDeliveries,explicitMonitorSaves:f.counts.monitorSaves,explicitMonitorApplies:f.counts.monitorApplies,retiredRequestRevived:false});
  }finally{oldAttach.resolve();off();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
 {
  const framesA=defer(),framesB=defer();let frames=0;
  const f=await createMirrorHostFixture({readHooks:{frames:()=>++frames===1?framesA.promise:frames===2?framesB.promise:Promise.resolve()}});
  try{
   f.ready();f.emit('deckySidebar.mirrorRequest',{...peer,connectionRevision:1,request:{command:'snapshot',runId}});await flush();assert.equal(frames,1);
   // A passed the native gate, then B takes ownership while A awaits an owner.
   f.emit('deckySidebar.mirrorRequest',{...peer,connection:'2',connectionRevision:3,request:{command:'snapshot',runId}});await flush();assert.equal(frames,2);
   framesA.resolve();await flush();assert.equal(f.replies.filter(row=>row.message.type==='snapshot').length,0);
   framesB.resolve();await flush();const views=f.replies.filter(row=>row.message.type==='snapshot');assert(views.length);assert(views.every(row=>row.connection==='2'&&row.message.snapshot.ready));assert.equal(frames,2);
   assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
   transportMeasurements.push({boundary:'native-request-A-retires-during-async-read',controlBatches:frames,currentBOnly:true,businessWrites:0});
  }finally{framesA.resolve();framesB.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }

 {
  const f=relayFixture();try{
   const B={...peer,connection:'2',connectionRevision:3},requestB={...B,request:{command:'snapshot',runId}};
   assert.equal(f.relay.observeAuthenticatedRequestPeer(requestB),true);await f.relay.request(requestB);
   assert.equal(f.relay.connected({...peer,connection:'2'}),true,'Matching legacy peer still works');
   assert.equal(f.relay.observeAuthenticatedRequestPeer({...peer,connection:'2',request:{command:'snapshot',runId}}),true,'Matching legacy request stays compatible');
   assert.equal(f.relay.connected({...B,connected:false,connectionRevision:4}),true);
   assert.equal(f.relay.connected({...peer,connected:true}),false,'Legacy ON cannot revive a typed OFF');
   assert.equal(f.relay.connected({...peer,connection:'2',connected:true}),false,'Even the retired identity needs a new native fact after typed OFF');
   transportMeasurements.push({boundary:'typed-floor-legacy-matching-and-OFF',matchingLegacyAllowed:true,legacyRevivalRefused:true});
  }finally{f.dispose();}
  const legacy=relayFixture();try{await legacy.open();assert.equal(legacy.replies.at(-1).snapshot.ready,true);assert.equal(legacy.relay.connected({...peer,connection:'2'}),true,'A fresh legacy-only owner retains its original route');transportMeasurements.push({boundary:'fresh-legacy-only',compatible:true});}finally{legacy.dispose();}
 }
});

await check('read recovery: first fault and actual recovery are logged once even inside the normal cooldown',async()=>{
 let fail=false;const f=relayFixture(async trace=>{try{await trace.measure('performance',async()=>{if(fail)throw Error('PRIVATE owner');});}catch{}return data();});
 try{await f.open();const before=f.logs.filter(r=>r.event==='snapshot-read-finished').length;fail=true;await f.invalidate();
  assert.equal(f.logs.filter(r=>r.event==='snapshot-read-finished').length,before+1);assert.equal(f.logs.at(-1).stage,'partial');assert.equal(f.logs.at(-1).ready,true);
  await f.fire(250);assert.equal(f.logs.filter(r=>r.event==='snapshot-read-finished').length,before+1);
  fail=false;await f.fire(1000);const end=f.logs.at(-1);assert.equal(end.stage,'settled');assert.equal(end.ready,true);assert.equal(end.ok,true);
  assert.equal(f.logs.filter(r=>r.event==='snapshot-read-finished').length,before+2);assert.equal(f.timers.size,0);assert(!JSON.stringify(f.logs).includes('PRIVATE'));
 }finally{f.dispose();}
});

async function speedCompletionBoundaries(){
 for(const timing of ['native-final-wait','owner-success-log-await']){
  const gate=defer(),P={active:true,connected:true,connection:'73',runId:'c'.repeat(32),connectionRevision:73};let f=null,held=false,holdLog=true,simulatedEffectFactor=1;
  const initialPower={generation:1,phase:'ready',hardwareWritesAllowed:true},initialGame={pid:101,name:'game.exe',title:'Fixture',path:'C:/Games/Game.exe',processCreated:'123456789'};
  const speed=createMirrorSpeedMemoryOwner({game:()=>f?.state.game??initialGame,power:()=>f?.state.power??initialPower,peer:()=>P,
   afterDispatch:async()=>{simulatedEffectFactor=2;if(timing==='native-final-wait'){f.state.power={...f.state.power,generation:2};f.powerChanged();}},
   beforeLogWrite:async text=>{if(timing==='owner-success-log-await'&&holdLog&&text.includes('RAW op=')){holdLog=false;held=true;await gate.promise;}}});
  f=await createMirrorHostFixture({speedOwner:speed.owner,invoke:async command=>{if(command==='deckySidebar.mirrorAttach')return speed.nativeState();if(command==='deckySidebar.mirrorReply')return true;throw Error('Unexpected completion fixture IPC '+command);}});
  try{
   f.ready();f.emit('deckySidebar.mirrorPeer',P);f.emit('deckySidebar.mirrorRequest',{...P,request:{command:'snapshot',runId:P.runId}});await flush();
   const snapshot=f.replies.filter(row=>row.message.type==='snapshot').at(-1).message.snapshot;assert.equal(snapshot.ready,true);
   const intent={...P,request:{command:'game.setField',runId:P.runId,id:'speed-complete:1',clientId:'speed-complete',sequence:1,generation:snapshot.generation,revision:snapshot.revision,identity:snapshot.game.identity,args:{field:'speedFactor',value:'2'}}};
   f.emit('deckySidebar.mirrorRequest',intent);await flush();
   if(timing==='owner-success-log-await'){assert.equal(held,true);f.state.power={...f.state.power,generation:2};f.powerChanged();gate.resolve();await flush();}
   const reply=f.replies.find(row=>row.message.id==='speed-complete:1')?.message;assert.equal(reply?.ok,true);assert.equal(reply.result.applied,false);assert.equal(reply.result.pending,true);assert.match(reply.result.notice,/待确认.*不自动重放/);
   assert.equal(speed.stats().dispatches,1);assert.equal(simulatedEffectFactor,2,'Already-possible effect is not called never-executed');const pending=speed.owner.getGameSpeedState();assert.equal(pending?.cleanupPending,true);assert.equal(pending?.enabled,false);assert.equal(pending?.pid,initialGame.pid);assert.equal(pending?.processCreated,initialGame.processCreated);assert.equal(speed.stats().activeGameObservers,1);assert.equal(speed.stats().activeWakeObservers,1);assert.equal(f.counts.speedApply,1);assert.equal(f.counts.speedLockReleases,1);
   assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.monitorSaves,0);
   speedCompletionMeasurements.push({timing,dispatches:speed.stats().dispatches,simulatedEffectFactor,newSessionCommitted:false,cleanupOwnerRetained:true,oldSuccessReported:false,lockReleases:f.counts.speedLockReleases,automaticReplay:false,nativeWaitModeled:true});
  }finally{gate.resolve();f.stop();speed.disposeExternalPorts();await flush();assert.equal(speed.stats().activeGameObservers,0);assert.equal(speed.stats().activeWakeObservers,0);assert.equal(f.observerStats().ipc,0);assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
}

await check('updater teardown failure cannot strand the next authenticated status connection',async()=>{
 const store=load(src('decky-plugin/src/deckyStoreClient.ts'));
 const protocol=load(src('decky-plugin/src/deckyUpdaterProtocol.ts'));
 const {createDeckyUpdaterService}=load(src('src/bridge/deckyUpdaterService.ts'),{'../../decky-plugin/src/deckyStoreClient':store,'../../decky-plugin/src/deckyUpdaterProtocol':protocol});
 const measurements=[];
 for(const boundary of ['pause','disconnect','replacement','dispose'])for(const failStop of [false,true]){
  let stopCalls=0,liveObservers=0,effects=0;const notifications=new Set(),messages=[];
  const state={phase:'idle',message:'inert update status',percent:0};
  const service=createDeckyUpdaterService({version:'0.0.39',read:()=>({state,target:null}),ready:()=>true,gameRunning:()=>false,powerReady:()=>true,
   check:async()=>{effects++;},download:async()=>{effects++;return true;},install:async()=>{effects++;},validateInstall:async()=>{effects++;},
   observe:changed=>{liveObservers++;notifications.add(changed);return()=>{stopCalls++;liveObservers--;notifications.delete(changed);if(failStop)throw Error('inert updater unsubscribe failed');};},
   reply:async(connection,message)=>{messages.push({connection,...message});return true;}});
  const status=(p,subscribe=true)=>service.request({...p,request:{command:'updater.status',runId:p.runId,subscribe}});
  const current={connection:'2',runId:'b'.repeat(32)},retired={...peer};
  let thrown='';
  try{
   service.connected(retired);status(retired);await flush();assert.equal(liveObservers,1);const oldNotify=[...notifications][0];
   try{
    if(boundary==='pause')status(retired,false);
    else if(boundary==='disconnect')service.connected({...retired,connected:false});
    else if(boundary==='replacement')service.connected(current);
    else service.dispose();
   }catch(error){thrown=String(error.message??error);}
   const row={boundary,failStop,thrown,stopCalls,liveObservers};measurements.push(row);console.log(JSON.stringify({updaterStopMeasurement:row}));
   assert.equal(thrown,'','failed updater unsubscribe must not interrupt connection replacement or shutdown');
   assert.equal(stopCalls,1);assert.equal(liveObservers,0);
   if(boundary==='dispose'){
    assert.doesNotThrow(()=>service.dispose());service.connected(current);status(current);await flush();
    assert.equal(stopCalls,1);assert.equal(liveObservers,0);
   }else{
    if(boundary==='pause')status(retired,false);
    else {const count=messages.length;status(retired);await flush();assert.equal(messages.length,count,'retired peer cannot restore observation after disconnect/replacement');}
    assert.equal(stopCalls,1,'a retired failing stop callback must not be invoked twice');
    if(boundary!=='replacement')service.connected(current);
    status(current);await flush();assert.equal(liveObservers,1);assert(protocol.validUpdaterView(messages.at(-1).view));assert.equal(messages.at(-1).connection,current.connection);
    const after=messages.length;oldNotify();await flush();assert.equal(messages.length,after,'late old observer cannot publish a duplicate unchanged current status');
    assert.doesNotThrow(()=>service.dispose());assert.equal(stopCalls,2);assert.equal(liveObservers,0);
   }
   assert.equal(effects,0,'status recovery never checks/downloads/installs or validates installation');
  }finally{try{service.dispose();}catch{}}
 }
 assert.equal(measurements.length,8);
});

await check('updater host releases every independent observation after one teardown callback fails',async()=>{
 if(updaterPowerOnly){await exerciseUpdaterPowerBoundary(updaterPowerMode);return;}
 const store=load(src('decky-plugin/src/deckyStoreClient.ts')),protocol=load(src('decky-plugin/src/deckyUpdaterProtocol.ts'));
 const updater=load(src('src/bridge/deckyUpdaterService.ts'),{'../../decky-plugin/src/deckyStoreClient':store,'../../decky-plugin/src/deckyUpdaterProtocol':protocol});
 const rows=[];
 for(const boundary of ['pause','disconnect','stop'])for(const failAt of ['none','state','game','power']){
  const observers=new Map(),stopCalls=[],messages=[];let extension=null,effects=0,ensureCalls=0;
  const install=(kind,callback)=>{assert(!observers.has(kind));observers.set(kind,callback);return()=>{stopCalls.push(kind);observers.delete(kind);if(failAt===kind)throw Error('inert '+kind+' unsubscribe failure');};};
  const power=updaterHostPowerFixture(install);
  const multiplex=load(src('src/bridge/deckyMirrorExtensions.ts'));
  const {startDeckyUpdaterHost}=load(src('src/bridge/deckyUpdaterHost.ts'),{
   vue:{watch:(_source,callback)=>install('state',callback)},'@/version':{APP_VERSION:'0.0.39'},
   './ipc':{invoke:async(command,args)=>{assert.equal(command,'deckySidebar.mirrorReply');messages.push(structuredClone(args));return true;},on:(event,callback)=>install(event==='power.suspending'?'suspend':'resume',callback)},
   './powerLifecycleState':power.ports,
   './api':{powerLifecycle:{get:async()=>({phase:'ready'})}},
   './gamedetect':{refreshGameStatusStrict:async()=>{effects++;return null;}},
   './gamePolicyTarget':{isPolicyGameInitialized:()=>true,getPolicyGame:()=>null,subscribePolicyGameStatus:callback=>install('game',callback)},
   './updateManager':{ensureUpdateManager:async()=>{ensureCalls++;},updateSnapshot:{phase:'idle'},updateInfo:{value:null},checkForUpdate:async()=>{effects++;},prepareUpdate:async()=>{effects++;return true;},downloadAndInstall:async()=>{effects++;}},
   './deckyMirrorExtensions':{registerDeckyMirrorExtension:value=>{extension=value;return multiplex.registerDeckyMirrorExtension(value);}},
   './deckyUpdaterService':updater
  });
  const host=startDeckyUpdaterHost(()=>true),request=subscribe=>extension.request({...peer,request:{command:'updater.status',runId,subscribe}});
  const release=power.source.onSharedPowerLifecycle(()=>{});
  try{
   await flush();assert.equal(power.source.getSharedPowerLifecycle().phase,'ready');
   multiplex.dispatchDeckyMirrorPeer(peer);request(true);await flush();assert.equal(observers.size,3);assert.equal(ensureCalls,1);
   if(boundary==='pause')request(false);else if(boundary==='disconnect')multiplex.dispatchDeckyMirrorPeer({...peer,connected:false});else host.stop();
   await flush();const row={boundary,failAt,remaining:[...observers.keys()],stopCalls:[...stopCalls],effects};rows.push(row);console.log(JSON.stringify({updaterAdapterCleanupMeasurement:row}));
   assert.equal(observers.size,0,'one updater adapter unsubscribe must not strand later independent observations');
   assert.deepEqual(stopCalls,['state','game','power']);
   const count=messages.length;for(let n=0;n<1000;n++)for(const fn of observers.values())fn();await flush();assert.equal(messages.length,count);assert.equal(effects,0);
   if(boundary!=='stop'){if(boundary==='disconnect')multiplex.dispatchDeckyMirrorPeer(peer);request(true);await flush();assert.equal(observers.size,3);host.stop();assert.equal(observers.size,0);}
  }finally{host.stop();release();power.assertClosed();}
  assert.equal(observers.size,0);
 }
 assert.equal(rows.length,12);
 await exerciseUpdaterPowerBoundary();
});

await check('retired updater initialization cannot clear the current connection failure notice',async()=>{
 const store=load(src('decky-plugin/src/deckyStoreClient.ts')),protocol=load(src('decky-plugin/src/deckyUpdaterProtocol.ts'));
 const updater=load(src('src/bridge/deckyUpdaterService.ts'),{'../../decky-plugin/src/deckyStoreClient':store,'../../decky-plugin/src/deckyUpdaterProtocol':protocol});
 const measurements=[];
 for(const boundary of ['pause-reopen','disconnect-reopen','peer-replacement']){
  const lateInitialization=defer(),observers=new Map(),messages=[],retiredCallbacks=[];
  let extension=null,ensureCalls=0,checks=0,downloads=0,installs=0,installValidations=0,powerReads=0;
  const state={phase:'idle',message:'inert current idle',percent:0};
  const install=(kind,callback)=>{observers.set(kind,callback);return()=>{retiredCallbacks.push({kind,callback});if(observers.get(kind)===callback)observers.delete(kind);};};
  const power=updaterHostPowerFixture(install);
  const multiplex=load(src('src/bridge/deckyMirrorExtensions.ts'));
  const {startDeckyUpdaterHost}=load(src('src/bridge/deckyUpdaterHost.ts'),{
   vue:{watch:(_source,callback)=>install('state',callback)},'@/version':{APP_VERSION:'0.0.39'},
   './ipc':{invoke:async(command,args)=>{assert.equal(command,'deckySidebar.mirrorReply');messages.push(structuredClone(args));return true;},on:(event,callback)=>install(event==='power.suspending'?'suspend':'resume',callback)},
   './powerLifecycleState':power.ports,
   './api':{powerLifecycle:{get:async()=>{powerReads++;return {phase:'ready'};}}},
   './gamedetect':{refreshGameStatusStrict:async()=>{installValidations++;return null;}},
   './gamePolicyTarget':{isPolicyGameInitialized:()=>true,getPolicyGame:()=>null,subscribePolicyGameStatus:callback=>install('game',callback)},
   './updateManager':{ensureUpdateManager:()=>++ensureCalls===1?lateInitialization.promise:Promise.resolve(),updateSnapshot:state,updateInfo:{value:null},checkForUpdate:async()=>{checks++;throw Error('inert current check failure');},prepareUpdate:async()=>{downloads++;return false;},downloadAndInstall:async()=>{installs++;}},
   './deckyMirrorExtensions':{registerDeckyMirrorExtension:value=>{extension=value;return multiplex.registerDeckyMirrorExtension(value);}},'./deckyUpdaterService':updater
  });
  const host=startDeckyUpdaterHost(()=>true),status=p=>extension.request({...p,request:{command:'updater.status',runId:p.runId,subscribe:true}});
  const release=power.source.onSharedPowerLifecycle(()=>{});
  try{
   await flush();assert.equal(power.source.getSharedPowerLifecycle().phase,'ready');
   multiplex.dispatchDeckyMirrorPeer(peer);status(peer);await flush();assert.equal(observers.size,3);assert.equal(ensureCalls,1);
   const current=boundary==='peer-replacement'?{...peer,connection:'2',runId:'c'.repeat(32)}:{...peer};
   if(boundary==='pause-reopen')extension.request({...peer,request:{command:'updater.status',runId,subscribe:false}});
   else if(boundary==='disconnect-reopen')multiplex.dispatchDeckyMirrorPeer({...peer,connected:false});
   if(boundary!=='pause-reopen')multiplex.dispatchDeckyMirrorPeer(current);
   status(current);await flush();assert.equal(ensureCalls,2);assert.equal(observers.size,3);
   extension.request({...current,request:{command:'updater.request',runId:current.runId,clientId:'aabbccdd-1122-3344-5566-778899aabbcc',id:1,action:'check'}});
   await flush();const before=messages.filter(r=>r.message.type==='ymcc-updater').at(-1);assert(before);assert.notEqual(before.message.view.message,state.message);assert.equal(checks,1);
   const count=messages.length,reads=powerReads;lateInitialization.resolve();await flush();
   const after=messages.filter(r=>r.message.type==='ymcc-updater').at(-1),row={boundary,oldInitializationResolved:true,errorBefore:before.message.view.message,errorAfter:after.message.view.message,extraPackets:messages.length-count,powerReadsBefore:reads,powerReadsAfter:powerReads,checks,downloads,installs,installValidations};
   measurements.push(row);console.log(JSON.stringify({updaterObserverEpochMeasurement:row}));
   assert.equal(after.message.view.message,before.message.view.message,'retired updater initialization must preserve the current operation failure');
   assert.equal(messages.length,count,'retired async completion cannot publish to the new subscriber');
   for(let n=0;n<1000;n++)for(const old of retiredCallbacks)if(old.kind==='state'||old.kind==='game')old.callback();
   await flush();assert.equal(messages.length,count);assert.equal(powerReads,reads);
   state.phase='failed';state.error='fresh current update owner status';observers.get('state')();await flush();
   const fresh=messages.filter(r=>r.message.type==='ymcc-updater').at(-1);assert.equal(fresh.message.view.message,state.error);assert(protocol.validUpdaterView(fresh.message.view));assert.equal(fresh.connection,current.connection);
   assert.equal(checks,1);assert.equal(downloads,0);assert.equal(installs,0);assert.equal(installValidations,0);
  }finally{lateInitialization.resolve();host.stop();release();await flush();power.assertClosed();}
  assert.equal(observers.size,0);
 }
 assert.equal(measurements.length,3);
});

await check('updater observation setup rollback allows an explicit current status retry',async()=>{
 const store=load(src('decky-plugin/src/deckyStoreClient.ts')),protocol=load(src('decky-plugin/src/deckyUpdaterProtocol.ts'));
 const updater=load(src('src/bridge/deckyUpdaterService.ts'),{'../../decky-plugin/src/deckyStoreClient':store,'../../decky-plugin/src/deckyUpdaterProtocol':protocol});
 const measurements=[];
 for(const recovery of ['same-page','disconnect','page-reopen'])for(const failAt of ['state','game','power','manager']){
  const observers=new Map(),messages=[],stops=[];let failing=true,effects=0,ensureCalls=0,extension=null;
  const install=(kind,callback)=>{if(failing&&failAt===kind)throw Error('inert setup '+kind+' unavailable');assert(!observers.has(kind),'no duplicate current observation');observers.set(kind,callback);return()=>{stops.push(kind);if(observers.get(kind)===callback)observers.delete(kind);};};
  const multiplex=load(src('src/bridge/deckyMirrorExtensions.ts'));
  const {startDeckyUpdaterHost}=load(src('src/bridge/deckyUpdaterHost.ts'),{
   vue:{watch:(_source,callback)=>install('state',callback)},'@/version':{APP_VERSION:'0.0.39'},
   './ipc':{invoke:async(command,args)=>{assert.equal(command,'deckySidebar.mirrorReply');messages.push(structuredClone(args));return true;},on:(event,callback)=>install(event==='power.suspending'?'suspend':'resume',callback)},
   './powerLifecycleState':updaterHostPowerFixture(install).ports,
   './api':{powerLifecycle:{get:async()=>({phase:'ready'})}},'./gamedetect':{refreshGameStatusStrict:async()=>{effects++;return null;}},
   './gamePolicyTarget':{isPolicyGameInitialized:()=>true,getPolicyGame:()=>null,subscribePolicyGameStatus:callback=>install('game',callback)},
   './updateManager':{ensureUpdateManager:()=>{ensureCalls++;if(failing&&failAt==='manager')throw Error('inert manager setup unavailable');return Promise.resolve();},updateSnapshot:{phase:'idle'},updateInfo:{value:null},checkForUpdate:async()=>{effects++;},prepareUpdate:async()=>{effects++;return false;},downloadAndInstall:async()=>{effects++;}},
   './deckyMirrorExtensions':{registerDeckyMirrorExtension:value=>{extension=value;return multiplex.registerDeckyMirrorExtension(value);}},'./deckyUpdaterService':updater
  });
  const host=startDeckyUpdaterHost(()=>true),status=(p,subscribe=true)=>multiplex.dispatchDeckyMirrorRequest({...p,request:{command:'updater.status',runId:p.runId,subscribe}});
  try{
   multiplex.dispatchDeckyMirrorPeer(peer);status(peer);await flush();
   const failure=messages.filter(r=>r.message.type==='ymcc-updater').at(-1)?.message.view;
   const row={recovery,failAt,remainingAfterFailure:[...observers.keys()],failureReplyPresent:!!failure,failureActions:failure?[failure.canCheck,failure.canDownload,failure.canInstall]:null,ensureCalls,effects};
   measurements.push(row);console.log(JSON.stringify({updaterSubscriptionRecoveryMeasurement:row}));
   assert.equal(observers.size,0,'failed updater setup must roll back every earlier observation');
   assert(failure&&protocol.validUpdaterView(failure),'failed setup must report a bounded current unavailable view');
   assert.equal(failure.canCheck,false);assert.equal(failure.canDownload,false);assert.equal(failure.canInstall,false);
   if(recovery==='disconnect'){multiplex.dispatchDeckyMirrorPeer({...peer,connected:false});multiplex.dispatchDeckyMirrorPeer({...peer,connection:'2'});}
   else if(recovery==='page-reopen')status(peer,false);
   failing=false;const current=recovery==='disconnect'?{...peer,connection:'2'}:peer;status(current);await flush();
   assert.equal(observers.size,3,'new explicit status must establish one complete observation after failure');
   const healthy=messages.filter(r=>r.message.type==='ymcc-updater').at(-1);assert.equal(healthy.connection,current.connection);assert.equal(healthy.message.view.canCheck,true);assert(protocol.validUpdaterView(healthy.message.view));
   const priorEnsures=ensureCalls;for(let n=0;n<10;n++)status(current);await flush();assert.equal(ensureCalls,priorEnsures);assert.equal(observers.size,3);assert.equal(effects,0);
   host.stop();assert.equal(observers.size,0);
  }finally{host.stop();observers.clear();}
 }
 assert.equal(measurements.length,12);
});

// Actual shared reader/model, host and relay. Only native completion and source
// notifications are inert; no alternate identity/cache/permission model.
async function jointDisplayFixture({automatic=false,readHooks={},defaultEvents=false}={}){
 const eventListeners=new Map(),readinessListeners=new Set();let f,invokeNative;const eventSource={on:(name,callback)=>{let set=eventListeners.get(name);if(!set)eventListeners.set(name,set=new Set());set.add(callback);return()=>{set.delete(callback);if(!set.size)eventListeners.delete(name);};},emit:(name,value)=>{for(const callback of [...eventListeners.get(name)??[]])callback(value);}};
 const model=load(src('src/bridge/gameSteamContextModel.ts'));
 const readerModule=load(src('src/bridge/gameSteamContext.ts'),{'./gameSteamContextModel':model,
  './gamedetect':{getCurrentGame:()=>f?.state.game??null,isCurrentGameTarget:()=>f?.state.gameCurrent===true,subscribeGameTargetReadiness:callback=>{if(defaultEvents){readinessListeners.add(callback);callback(false);return()=>readinessListeners.delete(callback);}return()=>{};}},
  './ipc':{invoke:(...args)=>invokeNative(...args),on:defaultEvents?eventSource.on:()=>()=>{}}},{AbortController,...(defaultEvents?{window:{}}:{})});
 let uiSubscriptions=0;const slots=[],active=new Set(),sourceListeners=new Set();
 const wire=slot=>{const game=slot.target,appid=400+game.pid;return {schemaVersion:1,
  game:{...game,rulesEpoch:'1',powerGeneration:String(slot.generation),processValidated:false},
  steam:{status:'cached-association',appid,gameId:String(appid),gamePid:game.pid,gameCreated:game.processCreated,gamePath:game.path,
   gameIdentityGeneration:game.identityGeneration,steamPid:900,steamCreated:'987654321',processEpoch:'1',accountEpoch:'1',processValidated:false,currentSource:false}};};
 const injectedReader=readerModule.createGameSteamContextReader({getGame:()=>f?.state.game??null,isGameCurrent:()=>f?.state.gameCurrent===true,
  subscribeInvalidation:callback=>{sourceListeners.add(callback);return()=>sourceListeners.delete(callback);},
  invoke:invokeNative=async(command,args,options)=>{
   assert(['gameSteam.context.get','gameSteam.context.refresh'].includes(command));const deferred=defer(),slot={command,target:structuredClone(args.expectedGame),generation:f.state.power.generation,signal:options?.signal,resolve:value=>deferred.resolve(value),wire:()=>wire(slot)};
   slots.push(slot);active.add(slot);const abort=()=>deferred.reject(Error('Inert native request retired'));
   options?.signal?.addEventListener('abort',abort,{once:true});if(options?.signal?.aborted)abort();
   if(automatic||command==='gameSteam.context.refresh')queueMicrotask(()=>slot.resolve(wire(slot)));
   try{return await deferred.promise;}finally{active.delete(slot);options?.signal?.removeEventListener('abort',abort);}
  }});
 const reader=defaultEvents?readerModule.gameSteamContext:injectedReader;
 const owner={...reader,subscribeInvalidation:callback=>{uiSubscriptions++;const stop=reader.subscribeInvalidation(callback);let retired=false;return()=>{if(!retired){retired=true;uiSubscriptions--;stop();}};}};
 f=await createMirrorHostFixture({contextOwner:owner,readHooks,...(defaultEvents?{eventSource}:{})});f.state.game.identityGeneration=1;
 const latest=()=>f.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;
 const request=(subscribe=true)=>f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe}});
 const getSlots=()=>slots.filter(slot=>slot.command==='gameSteam.context.get'),warmCount=()=>slots.filter(slot=>slot.command==='gameSteam.context.refresh').length;
 const invalidate=()=>{for(const callback of [...sourceListeners])callback();};
 const finish=async()=>{f.stop();reader.dispose();injectedReader.dispose();await flush();assert.equal(eventListeners.size,0);assert.equal(readinessListeners.size,0);assert.equal(active.size,0);assert.equal(uiSubscriptions,0);assert.equal(sourceListeners.size,0);assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);assert.equal(f.sharedPowerTimerCount(),0);assert(Object.values(f.observerStats()).every(value=>value===0));};
 const gameReadiness=current=>{f.setGameReadiness(current);for(const callback of [...readinessListeners])callback(current);};
 return {f,reader,latest,request,getSlots,warmCount,invalidate,wire,active,uiSubscriptions:()=>uiSubscriptions,eventSource,gameReadiness,finish};
}
await check('optional shared Steam metadata never blocks controls or replays an explicit monitor intent',async()=>{
 const x=await jointDisplayFixture();try{
  x.f.ready();x.request();await flush();const snapshot=x.latest();
  assert(snapshot?.ready,'a missing optional shared reply must not hold the core snapshot');assert.equal(snapshot.gameSteam,undefined);
  assert(x.f.counts.inputReads>0&&x.f.counts.frameReads>0&&x.f.counts.monitorReads>0,'independent controls must start while the get is pending');
  assert.equal(x.getSlots().length,1);assert.equal(x.getSlots()[0].signal.aborted,false);assert.equal(snapshot.actions.monitor,true);
  const request={...peer,request:{command:'monitor.setEnabled',runId,id:'optional:1',clientId:'optional',sequence:1,generation:snapshot.generation,revision:snapshot.revision,args:{enabled:!snapshot.monitor.enabled}}};
  x.f.emit('deckySidebar.mirrorRequest',request);await flush();assert.equal(x.f.counts.monitorSaves,1);assert.equal(x.f.counts.monitorApplies,1);assert.equal(x.getSlots().length,1);
  const reply=x.f.replies.filter(row=>row.message.type==='reply').at(-1)?.message;assert.equal(reply?.ok,true);
  x.f.emit('deckySidebar.mirrorRequest',request);await flush();assert.equal(x.f.counts.monitorSaves,1);assert.equal(x.f.counts.monitorApplies,1);
 }finally{await x.finish();}
});
await check('optional shared metadata completes the same fast snapshot and never creates a warm loop',async()=>{
 const x=await jointDisplayFixture({automatic:true});try{
  x.f.ready();x.request();await flush();const snapshot=x.latest();assert(snapshot?.ready);assert.equal(snapshot.gameSteam?.game.pid,x.f.state.game.pid);assert.equal(snapshot.gameSteam?.steam.status,'cached-association');
  assert.equal(snapshot.gameSteam.steam.processValidated,false);assert.equal(snapshot.gameSteam.steam.currentSource,false);
  assert.equal(x.getSlots().length,1);const reads=x.f.counts.frameReads,warms=x.warmCount(),snapshots=x.f.replies.filter(row=>row.message.type==='snapshot').length;
  await flush();await flush();assert.equal(x.getSlots().length,1);assert.equal(x.warmCount(),warms);assert.equal(x.f.counts.frameReads,reads);assert.equal(x.f.replies.filter(row=>row.message.type==='snapshot').length,snapshots);
 }finally{await x.finish();}
 // Win5 wake replay: a Steam metadata notification lands while the first
 // profile read still owns the requested control snapshot.
 for(const event of ['gameSteam.context.changed','steam.uiStateChanged']){
  const profile=defer();let profileReads=0;
  const x=await jointDisplayFixture({automatic:true,defaultEvents:true,readHooks:{profileReturn:async()=>{if(++profileReads===1)await profile.promise;}}});
  try{
   x.f.state.power.generation=8400;x.f.ready();x.request();await flush();
   assert.equal(profileReads,1);assert.equal(x.latest(),undefined);
   x.eventSource.emit(event,{pid:x.f.state.game.pid,identityGeneration:1});await flush();
   profile.resolve();await flush();
   const view=x.latest();assert(view?.ready&&view.actions.monitor);assert.equal(view.generation,8400);
   const state={event,profileReads,frameReads:x.f.counts.frameReads,discarded:x.f.diagnostics.filter(row=>row.event==='snapshot-read-finished'&&row.discarded).length};
   console.log(JSON.stringify({win5WakeMetadata:state}));
   assert.deepEqual(state,{event,profileReads:1,frameReads:1,discarded:0});
   assert.equal(x.f.counts.saves,0);assert.equal(x.f.counts.inputSaves,0);assert.equal(x.f.counts.monitorSaves,0);
  }finally{profile.resolve();await x.finish();}
 }
});
await check('late optional shared metadata repaints once through the existing display without another query',async()=>{
 const x=await jointDisplayFixture(),browser=clientFixture('a'.repeat(64));try{
  x.f.ready();x.request();await flush();assert(x.latest()?.ready);const slot=x.getSlots()[0],reads=x.f.counts.frameReads,warms=x.warmCount(),initial=structuredClone(x.latest()),inputReads=x.f.counts.inputReads,monitorReads=x.f.counts.monitorReads,powerReads=x.f.counts.power;
  slot.resolve(slot.wire());await flush();const snapshot=x.latest();assert.equal(snapshot.gameSteam?.game.pid,x.f.state.game.pid);assert.equal(snapshot.gameSteam?.steam.gameId,String(400+x.f.state.game.pid));
  const snapshots=x.f.replies.filter(row=>row.message.type==='snapshot');
  assert.equal(snapshots[0].message.displayOnly,undefined,'Initial offer must be a full snapshot');
  assert.equal(snapshots.at(-1).message.displayOnly,true,'Late cosmetic fields must be sent as a repaint');
  assert.equal(snapshots.at(-1).message.probeId,undefined);
  const first=snapshots[0].message,repaint=snapshots.at(-1).message;
  browser.client.setVisible(true);browser.handlers.get('open')();
  const receive=message=>browser.handlers.get('message')({data:JSON.stringify(message)});
  receive(first);
  const control=browser.client.snapshot().snapshot;
  assert(control?.ready&&control.actions.monitor);
  const checking=browser.client.testConnection().catch(error=>error),nonce=browser.sent.at(-1).probeId;
  assert(nonce);
  // No control fields are needed to repaint the existing accepted revision.
  receive({...repaint,probeId:nonce});
  const shown=browser.client.snapshot();
  assert.deepEqual(shown.snapshot,{...control,gameSteam:structuredClone(repaint.snapshot.gameSteam),losslessProgram:structuredClone(control.losslessProgram)});
  assert.equal(shown.connectionTest.pending,true);
  receive({...repaint,snapshot:{...repaint.snapshot,gameSteam:undefined}});
  assert.equal(browser.client.snapshot().snapshot.gameSteam,undefined,'A display packet can still clear obsolete game metadata');
  assert.deepEqual(browser.client.snapshot().snapshot.actions,control.actions);
  assert.equal(browser.client.snapshot().connectionTest.pending,true);
  receive({...first,probeId:nonce,snapshot:{...first.snapshot,provenance:{kind:'ymcc-native',pid:123}}});
  assert.equal(browser.client.snapshot().connectionTest.pending,false);
  assert.equal((await checking).revision,initial.revision);
  const bytes=value=>Buffer.byteLength(JSON.stringify(value));
  console.log(JSON.stringify({displayWireBytes:{full:bytes(first),repaint:bytes(repaint),fields:Object.keys(repaint.snapshot).sort()}}));
  assert.deepEqual(Object.keys(repaint.snapshot).sort(),['gameSteam','generation','losslessProgram','revision','runId'],
    'A display repaint must not reserialize full business controls');
  assert.equal(x.getSlots().length,1);assert.equal(x.warmCount(),warms);assert.equal(x.f.counts.frameReads,reads,'Late optional metadata must not repeat business-owner reads');
  const count=x.f.replies.length;await flush();await flush();assert.equal(x.f.replies.length,count);assert.equal(x.getSlots().length,1);
  // Metadata completion retains the original rendered control receipt. An
  // explicit command from it is still validated/applied by the original owner.
  assert.equal(snapshot.revision,initial.revision);assert.equal(snapshot.generation,initial.generation);assert.deepEqual(shown.snapshot.actions,control.actions);
  assert.equal(x.f.counts.inputReads,inputReads);assert.equal(x.f.counts.monitorReads,monitorReads);assert.equal(x.f.counts.power,powerReads);
  x.f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'monitor.setEnabled',runId,id:'display:1',clientId:'display',sequence:1,generation:initial.generation,revision:initial.revision,args:{enabled:!initial.monitor.enabled}}});await flush();
  const reply=x.f.replies.filter(row=>row.message.type==='reply').at(-1)?.message;assert.equal(reply?.ok,true);assert.equal(x.f.counts.monitorSaves,1);assert.equal(x.f.counts.monitorApplies,1);

 }finally{browser.client.dispose();assert.equal(browser.timers.size,0);await x.finish();}

 // Existing full read wins over a late display completion and carries the
 // metadata itself. It must not be invalidated into a third control batch.
 {
  const hold=defer();let blocked=false;const x=await jointDisplayFixture({readHooks:{frames:async()=>{if(blocked)await hold.promise;}}});
  try{x.f.ready();x.request();await flush();const first=x.latest();assert(first?.ready);blocked=true;x.f.state.monitor={...x.f.state.monitor,enabled:!x.f.state.monitor.enabled};x.f.monitorChanged();await flush();const reads=x.f.counts.frameReads;assert.equal(reads,2);const slot=x.getSlots()[0];slot.resolve(slot.wire());await flush();assert.equal(x.f.counts.frameReads,reads);hold.resolve();await flush();assert(x.latest()?.ready);assert(x.latest().gameSteam);assert.equal(x.latest().monitor.enabled,!first.monitor.enabled);assert.equal(x.f.counts.frameReads,reads);assert.equal(x.f.counts.monitorSaves,0);}
  finally{hold.resolve();await x.finish();}
 }
 // Invalidation can arrive after the host assembled the view and before the
 // relay accepted it. Retire only metadata; the first control revision wins.
 {
  let x,fired=false;const replies=()=>x.f.replies.filter(row=>row.message.type==='snapshot'),browser=clientFixture('a'.repeat(64));
  x=await jointDisplayFixture({automatic:true,defaultEvents:true,readHooks:{diagnostic:row=>{
   if(!fired&&row.event==='snapshot-read-finished'){
    fired=true;
    x.eventSource.emit('gameSteam.context.changed',{pid:x.f.state.game.pid,identityGeneration:1});
   }
  }}});
  try{
   x.f.ready();x.request();await flush();
   assert.equal(fired,true);assert.equal(x.getSlots().length,2);
   browser.client.setVisible(true);browser.handlers.get('open')();
   for(const {message} of replies())browser.handlers.get('message')({data:JSON.stringify(message)});
   const displayed=browser.client.snapshot().snapshot;
   assert(displayed?.ready&&displayed.gameSteam?.game.pid===x.f.state.game.pid);
   assert(replies().length>0&&replies().every(row=>row.message.snapshot.revision===1));
   assert.equal(x.f.counts.power,1);assert.equal(x.f.counts.frameReads,1);assert.equal(x.f.counts.monitorSaves,0);
  }finally{browser.client.dispose();assert.equal(browser.timers.size,0);await x.finish();}
 }
 // Losing the first broker send leaves no receipt for optional display.
 {
  const messages=[];let reads=0,running=false;
  const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;return {generation:1,ready:true,game:null,fan:{supported:false,enabled:false,preset:'',choices:[]},losslessProgram:{known:running,running}};},observe:()=>()=>{},reply:async(_connection,message)=>{messages.push(structuredClone(message));return messages.length>1;}});
  try{
   await relay.request({...peer,request:{command:'snapshot',runId}});
   running=true;
   assert.equal(relay.repaintLosslessProgram(1,{known:true,running:true}),true);
   await flush();assert.equal(messages.length,1,'A repaint must not replace a rejected original delivery');assert.equal(reads,1);
   const probeId='probe:display:1';
   await relay.request({...peer,request:{command:'snapshot',runId,probeId}});
   assert.equal(messages.length,2);assert.equal(reads,2);assert.equal(messages[1].displayOnly,undefined);
   assert.equal(messages[1].probeId,probeId);assert.equal(messages[1].snapshot.losslessProgram.running,true);
  }finally{relay.dispose();}
 }
 // Slow sends retain only the latest repaint. Closing the panel retires
 // its wait without requiring the old broker acknowledgement to arrive.
 {
  const held=defer(),retired=defer(),messages=[];let reads=0,waiters=0;
  class CountingPromise extends Promise{static allSettled(values){waiters++;return super.allSettled(values);}}
  const steam=load(src('decky-plugin/src/steamRunningContext.ts'));
  const {ReadOnlyMirrorRelay:CountedRelay}=load(src('src/bridge/deckyMirrorRelay.ts'),{'../../decky-plugin/src/steamRunningContext':steam},{Promise:CountingPromise});
  const relay=new CountedRelay({read:async()=>{reads++;return {generation:1,ready:true,game:null,fan:{supported:false,enabled:false,preset:'',choices:[]},losslessProgram:{known:true,running:false}};},observe:()=>()=>{},reply:async(_connection,message)=>{messages.push(structuredClone(message));if(messages.length===2)await held.promise;else if(messages.length===4)await retired.promise;return true;}});
  const request=subscribe=>relay.request({...peer,request:{command:'snapshot',runId,subscribe}});
  const lamp=running=>relay.repaintLosslessProgram(1,{known:true,running});
  try{
   await request(true);assert.equal(lamp(true),true);await flush();assert.equal(messages.length,2);assert.equal(messages[1].snapshot.losslessProgram.running,true);
   const startingWaiters=waiters;
   for(let i=0;i<33;i++)assert.equal(lamp(i%2===1),true);
   const burstWaiters=waiters-startingWaiters;
   await flush();assert.equal(messages.length,2,'Repaints may not overtake an in-flight repaint');
   held.resolve();await flush();assert.equal(messages.length,3);assert.equal(messages[2].displayOnly,true);assert.equal(messages[2].snapshot.losslessProgram.running,false);assert.equal(reads,1);
   assert.equal(lamp(true),true);await flush();assert.equal(messages.length,4);assert.equal(lamp(false),true);
   await request(false);await request(true);assert.equal(messages.length,5);assert.equal(reads,2);
   assert.equal(lamp(true),true);await flush();const beforeRetired=messages.length;
   retired.resolve();await flush();
   const result={burstWaiters,beforeRetired,finalMessages:messages.length,reads};
   console.log(JSON.stringify({slowRepaint:result}));
   assert.equal(messages[4].displayOnly,undefined);assert.equal(messages[4].snapshot.losslessProgram.running,false);
   assert.equal(messages[5]?.displayOnly,true);assert.equal(messages[5]?.snapshot.losslessProgram.running,true);
   assert.deepEqual({beforeRetired,finalMessages:messages.length,reads},{beforeRetired:6,finalMessages:6,reads:2},'New demand must not wait for a retired send');
   assert(burstWaiters<=1,'Only one batch may wait for the same in-flight repaint');
  }finally{held.resolve();retired.resolve();relay.dispose();await flush();}
 }
 // A queued display delivery may not paint over a real source/peer boundary.
 for(const boundary of ['source','pause','peer','dispose']){
  let release;const held=new Promise(resolve=>{release=resolve;});const messages=[];let reads=0;const identity='current-game';
  const native={generation:1,ready:true,game:{label:'A',identity,fields:{}},fan:{supported:false,enabled:false,preset:'',choices:[]},losslessProgram:{known:false,running:false}};
  const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;return native;},observe:()=>()=>{},reply:async(connection,message)=>{messages.push({connection,message:structuredClone(message)});if(messages.length===1)await held;return true;}});
  try{const open=relay.request({...peer,request:{command:'snapshot',runId}});await flush();const before=reads;
   assert.equal(relay.repaintGameSteam?.(identity,1,undefined),true);
   const metadata={schemaVersion:1,game:null,steam:{status:'unavailable',reason:'pending'}};
   // Exact display content is opaque to action permissions; this private test
   // checks queue ownership only, separate from the real shared DTO above.
   assert.equal(relay.repaintGameSteam(identity,1,metadata),true);
   assert.equal(relay.repaintLosslessProgram(2,{known:true,running:true}),false,'Another power generation cannot repaint');
   assert.equal(relay.repaintLosslessProgram(1,{known:true,running:true}),true);
   if(boundary==='source')relay.invalidate();else if(boundary==='pause')await relay.request({...peer,request:{command:'snapshot',runId,subscribe:false}});else if(boundary==='peer')relay.connected({...peer,connection:'2'});else relay.dispose();
   release();await open;await flush();assert(messages.slice(1).every(row=>!row.message.snapshot.gameSteam&&!row.message.snapshot.losslessProgram.known),'Retired display patch must not be delivered');assert.equal(reads,boundary==='source'?before+1:before);
  }finally{release();relay.dispose();await flush();}
 }
});
await check('hiding optional metadata cancels only this sidebar subscriber and reopening restores it',async()=>{
 const x=await jointDisplayFixture();try{
  x.f.ready();x.request();await flush();assert(x.latest()?.ready);const slot=x.getSlots()[0],other=x.reader.read();await flush();assert.equal(x.getSlots().length,1);
  x.request(false);await flush();assert.equal(x.uiSubscriptions(),0);assert.equal(slot.signal.aborted,false,'another actual reader subscriber still owns the same IPC');const replies=x.f.replies.length;
  slot.resolve(slot.wire());const otherResult=await other;await flush();assert.equal(otherResult.steam.status,'cached-association');assert.equal(x.f.replies.length,replies,'hidden completion cannot repaint');
  const warms=x.warmCount();x.invalidate();await flush();assert.equal(x.warmCount(),warms);assert.equal(x.getSlots().length,1);
  x.request();await flush();assert(x.latest()?.ready);assert.equal(x.latest().gameSteam,undefined);assert.equal(x.getSlots().length,2);const fresh=x.getSlots()[1];fresh.resolve(fresh.wire());await flush();assert.equal(x.latest().gameSteam?.steam.status,'cached-association');
 }finally{await x.finish();}
});
await check('shared client epoch and game ABA discard optional metadata from retired requests',async()=>{
 // Default reader and real host receive the same native transport events.
 {
  const x=await jointDisplayFixture({defaultEvents:true});try{
   x.f.ready();x.request();await flush();
   const source={pid:900,processCreated:'987654321',processEpoch:'1',accountEpoch:'1',sourceAvailable:true,presence:1,windowsSessionKnown:true,windowsSession:1};
  const emit=(name,value)=>x.eventSource.emit(name,structuredClone(value));
   emit('gameSteam.context.sourceChanged',source);await flush();
   const slot=x.getSlots().at(-1),reads=x.f.counts.frameReads,gets=x.getSlots().length;
   for(let i=0;i<20;i++){emit('gameSteam.context.sourceChanged',source);await flush();}
   console.log(JSON.stringify({defaultSourceDuplicateMeasurements:{frameReadsBefore:reads,frameReadsAfter:x.f.counts.frameReads,getsBefore:gets,getsAfter:x.getSlots().length,pendingAborted:slot.signal.aborted}}));
   assert.equal(x.f.counts.frameReads,reads,'Duplicate complete source delivery must not repeat business reads');
   assert.equal(x.getSlots().length,gets);assert.equal(slot.signal.aborted,false);
   const changed=async(name,value)=>{const old=x.getSlots().at(-1);emit(name,value);await flush();assert.equal(old.signal.aborted,true,'Real or unknown source edge must retire pending metadata');};
   for(const patch of [{processEpoch:'2'},{accountEpoch:'2'},{processCreated:'987654322'},{pid:901},{windowsSession:2}]){await changed('gameSteam.context.sourceChanged',{...source,...patch});await changed('gameSteam.context.sourceChanged',source);}
   for(const patch of [{presence:-1},{presence:0},{sourceAvailable:false},{windowsSessionKnown:false,windowsSession:0},{processEpoch:'01'},{processEpoch:'18446744073709551616'},{extraAuthority:true}]){await changed('gameSteam.context.sourceChanged',{...source,...patch});await changed('gameSteam.context.sourceChanged',source);}
   await changed('gameSteam.context.sourceChanged',null);await changed('gameSteam.context.sourceChanged',source);
   const legacy={pid:900,processCreated:'987654321',processEpoch:'1',accountEpoch:'1'};await changed('gameSteam.context.sourceChanged',legacy);await changed('gameSteam.context.sourceChanged',legacy);
   const completion={pid:x.f.state.game.pid,identityGeneration:1};
   const ownerReads=()=>[x.f.counts.power,x.f.counts.frameReads,x.f.counts.inputReads,x.f.counts.monitorReads,x.f.counts.schedule];
   await changed('gameSteam.context.sourceChanged',source);
   await changed('gameSteam.context.changed',completion);
   const afterCompletion=x.getSlots().at(-1),afterCompletionGets=x.getSlots().length,afterCompletionReads=ownerReads();
   for(let i=0;i<20;i++){emit('gameSteam.context.sourceChanged',source);await flush();}
   console.log(JSON.stringify({sourceAfterMetadataCompletion:{extraGets:x.getSlots().length-afterCompletionGets,aborted:afterCompletion.signal.aborted}}));
   assert.equal(afterCompletion.signal.aborted,false,'Repeating the same verified source must not cancel the fresh metadata GET after completion');
   assert.equal(x.getSlots().length,afterCompletionGets);assert.deepEqual(ownerReads(),afterCompletionReads);
   const before=ownerReads(),revision=x.latest().revision,warmBefore=x.warmCount(),old=x.getSlots().at(-1),getsBefore=x.getSlots().length;
   for(let i=0;i<20;i++)emit('gameSteam.context.changed',completion);
   await flush();assert.equal(old.signal.aborted,true);assert.equal(x.getSlots().length,getsBefore+1);
   old.resolve(old.wire());await flush();assert.equal(x.latest().gameSteam,undefined);
   await changed('gameSteam.context.changed',completion);
   assert.deepEqual(ownerReads(),before);assert.equal(x.latest().revision,revision);
   assert.equal(x.warmCount(),warmBefore);assert.equal(x.f.counts.saves,0);assert.equal(x.f.counts.monitorSaves,0);
   const current=x.getSlots().at(-1),beforePower=x.getSlots().length;
   emit('power.resuming',{});emit('power.resuming',{});await flush();
   assert.equal(current.signal.aborted,false);assert.equal(x.getSlots().length,beforePower);
   x.gameReadiness(false);await flush();assert.equal(current.signal.aborted,true);
   x.gameReadiness(true);await flush();await changed('gameSteam.context.sourceChanged',source);
   const final=x.getSlots().at(-1);final.resolve(final.wire());await flush();assert(x.latest().gameSteam);
  }finally{await x.finish();}
 }
 const x=await jointDisplayFixture();try{
  x.f.ready();x.request();await flush();assert(x.latest()?.ready);const old=x.getSlots()[0],original={...x.f.state.game};
  x.invalidate();await flush();assert.equal(old.signal.aborted,true);assert.equal(x.getSlots().length,2);assert.equal(x.latest().gameSteam,undefined);
  old.resolve(old.wire());await flush();assert.equal(x.latest().gameSteam,undefined);
  const second=x.getSlots()[1];second.resolve(second.wire());await flush();assert.equal(x.latest().gameSteam?.game.pid,original.pid);
  x.f.state.game={...original,pid:original.pid+1,path:'C:/Games/Other.exe',processCreated:'123456790',identityGeneration:2};x.f.gameChanged();await flush();const B=x.getSlots().at(-1);assert.equal(B.target.pid,original.pid+1);
  x.f.state.game={...original};x.f.gameChanged();await flush();assert.equal(B.signal.aborted,true);const A=x.getSlots().at(-1);assert.notEqual(A,B);assert.equal(x.latest().gameSteam,undefined);
  B.resolve(B.wire());await flush();assert.equal(x.latest().gameSteam,undefined);A.resolve(A.wire());await flush();assert.equal(x.latest().gameSteam?.game.pid,original.pid);
 }finally{await x.finish();}
});
await check('optional metadata retires across suspended Ready and final host stop without a new timer',async()=>{
 const x=await jointDisplayFixture();try{
  x.f.ready();x.request();await flush();const old=x.getSlots()[0];assert(x.latest()?.ready);
  x.f.state.power={...x.f.state.power,generation:2,phase:'suspended',hardwareWritesAllowed:false,inputReady:false};
  x.f.emit('power.lifecycleChanged',{...x.f.state.power,known:true,publicationRevision:1,observedNotificationSequence:1});x.invalidate();await flush();assert.equal(old.signal.aborted,true);assert.equal(x.latest().ready,false);assert.equal(x.latest().gameSteam,undefined);
  const gets=x.getSlots().length;old.resolve(old.wire());await flush();assert.equal(x.getSlots().length,gets);
  x.f.state.power={...x.f.state.power,phase:'ready',hardwareWritesAllowed:true,inputReady:true};x.f.emit('power.lifecycleChanged',{...x.f.state.power,known:true,publicationRevision:2,observedNotificationSequence:2});x.invalidate();await flush();assert(x.latest()?.ready);
  const fresh=x.getSlots().at(-1);assert.equal(fresh.generation,2);fresh.resolve(fresh.wire());await flush();assert.equal(x.latest().gameSteam?.game.powerGeneration,'2');
  x.invalidate();await flush();const final=x.getSlots().at(-1);const replies=x.f.replies.length;x.f.stop();await flush();assert.equal(final.signal.aborted,true);assert.equal(x.uiSubscriptions(),0);final.resolve(final.wire());await flush();assert.equal(x.f.replies.length,replies);
 }finally{await x.finish();}
});


const registeredNames=registry.map(row=>row.name);
assert.equal(new Set(registeredNames).size,registeredNames.length,'Original readiness names must be unique');
const unknown=requestedCases.filter(name=>!registeredNames.includes(name));
if(unknown.length)selectionError='Unknown exact readiness case: '+unknown.join(', ');
const isReadRecoveryCase=name=>/^(?:read recovery:|updater |retired updater )/.test(name);
const selected=registry.filter(({name})=>requestedCases.length?requestedCases.includes(name):readRecoveryOnly?isReadRecoveryCase(name):legacyOnly?!isReadRecoveryCase(name):true);
if(!selected.length)selectionError=selectionError||'Empty readiness case selection';
if(selectionError){
 const report={status:'rejected',selected:'none',requestedCases,selectedNames:[],skipped:registeredNames,selectedResults:[],checks:0,preparationStarted:false,callbacksStarted:0,error:selectionError};
 console.log(JSON.stringify(report,null,2));process.exitCode=1;
}else{
 ({root,load}=await import('./fixtures/decky_game_memory_store.mjs'));
 ({createMirrorHostFixture,createMirrorSpeedMemoryOwner}=await import('./fixtures/decky_mirror_host_fixture.mjs'));
 const {validation}=await import('./fixtures/decky_task_paths.mjs');
 const steam=load(src('decky-plugin/src/steamRunningContext.ts'));
 ({ReadOnlyMirrorRelay}=load(src('src/bridge/deckyMirrorRelay.ts'),{'../../decky-plugin/src/steamRunningContext':steam}));
  const gameSteamModel=load(src('src/bridge/gameSteamContextModel.ts'));
  ({MirrorClient}=load(src('decky-plugin/src/mirrorClient.ts'),{'./steamRunningContext':steam,'../../src/bridge/gameSteamContextModel':gameSteamModel}));
 skipped.push(...registeredNames.filter(name=>!selected.some(row=>row.name===name)));
 for(const {name,fn} of selected){
  if(selection==='all'){await fn();cases.push(name);continue;}
  try{await fn();cases.push(name);selectedResults.push({name,status:'passed'});}
  catch(error){selectedResults.push({name,status:'failed',error:String(error.stack??error)});}
 }
 const report={status:selectedResults.some(row=>row.status==='failed')?'failed':'passed',selected:selection,requestedCases,selectedNames:selected.map(row=>row.name),skipped,selectedResults,checks:selection==='all'?cases.length:selectedResults.length,cases,beforeCounts,afterCounts,completionMeasurements,transportMeasurements,audioGenerationMeasurements,speedCompletionMeasurements,speedCompletionOnly,preparationStarted:true,callbacksStarted:selected.length,realSteam:false,realBusinessWrites:0,realDevices:false,healthyRuntimePollingTimers:0,recoveryUsesExistingTimerOwner:true,comparisonScope:readRecoveryOnly?'Production relay/host fault injection, capped read recovery, cancellation and current owner admission; no real Steam/devices or measured CPU claim.':legacyOnly?'Original 17 readiness diagnostics, current one-read counts and settings/target invalidation with no writes; historical baseline not rerun.':selection==='cases'?'Exact original names only; unselected legacy/read-recovery/native groups NOT_RUN. No installed, hardware or measured CPU acceptance.':'Combined original diagnostics and read recovery; no installed, hardware or measured CPU acceptance.'};
 fs.writeFileSync(path.join(validation,'READINESS-DIAGNOSTICS.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));if(report.status==='failed')process.exitCode=1;
}
