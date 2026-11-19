import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';
/** Explicit selection under settings.boot-modes-runtime. Native power/game/
 * scheduler leaves remain the original fixture's controlled terminals. */
export async function runPerformanceResidentOwners({out,buildFixture,savedPerformance,repository,source,target,yeman,freshSchedule,configureTdp,hardwareWrites,evaluate,frameService,bootCode,bootRuntime,bootModel,models,frameModel,configureFloat}){
 const kinds=['partial-auto','partial-manual','completed-manual','suspended','restart-suspended','inflight-stop','inflight-game','forged-completion','generation-reuse','sleep-boundaries'];
 const lateProofKinds=['late-tdp-frame-read-retry','late-tdp-held-proof'];
 const lateKinds=["late-tdp-once","late-tdp-no-receipt","late-tdp-false","late-tdp-stop","late-tdp-generation","late-tdp-config","late-tdp-frames","late-tdp-game","late-tdp-owner-cancel","late-tdp-cpu-unconfirmed","late-tdp-windows","late-tdp-auto-no-float","late-tdp-wake"].concat(lateProofKinds);
 const argument=process.argv.find(value=>value.startsWith('--boot-performance-resident-case=')),selected=argument?.split('=')[1];const continuationKinds=['held-read-wake','held-read-restart','held-read-sequence','held-read-finally','held-read-game'];assert(!argument||kinds.includes(selected)||continuationKinds.includes(selected)||lateKinds.includes(selected)||selected==='late-tdp-boundaries'||selected==='late-tdp-proof-boundaries'||selected==='held-read-boundaries','Unknown original resident performance case');
 const candidateArgument=process.argv.indexOf('--policy-runtime-source'),runtimeSource=candidateArgument<0?path.resolve('src/bridge/gamePolicyRuntime.ts'):process.argv[candidateArgument+1];assert(path.isAbsolute(runtimeSource),'Explicit absolute resident runtime source');
 const observations=path.join(out,'resident-observations.jsonl');fs.closeSync(fs.openSync(observations,'wx'));const rows=[];
 for(const mode of ['global','xbox','steam','xbox-steam'])for(const kind of selected==='late-tdp-proof-boundaries'?lateProofKinds:selected==='late-tdp-boundaries'?lateKinds:selected==='held-read-boundaries'?continuationKinds:selected?[selected]:kinds){
  const game={pid:1901,processCreated:'1901',path:'C:\\fixture\\resident.exe',name:'resident.exe'},f=await buildFixture(mode,{policyGame:game}),boot='resident-proof-boot';
  const actualFloat=kind.startsWith('partial-')?(await import('../continuous_float_runtime_selftest.mjs')).floatFixture:null;
  if(actualFloat){await actualFloat.reset();actualFloat.bind({yeman,repository:{getSettingsGeneration:f.repo.getSettingsGeneration,assertSettingsGeneration:f.repo.assertSettingsGeneration,readSettingsSection:repository.readSettingsSection,saveSettingsSection:repository.saveSettingsSection},game:target.getPolicyGame});configureFloat(actualFloat.runtime);}
  const service=freshSchedule();
  const requested=structuredClone(savedPerformance);requested.controls.ac.tdp=24;requested.controls.ac.tdpCeiling=30;
  if(['partial-manual','completed-manual','restart-suspended','suspended','generation-reuse'].includes(kind)||lateKinds.includes(kind))requested.controls.ac.mode='manual';
  if(kind==='late-tdp-windows')requested.powerSchemes.ac='balanced';
  if(kind==='late-tdp-auto-no-float'){requested.controls.ac.mode='auto';requested.controls.ac.auto.saving='none';}
  const owner={boot,generation:f.repo.getSettingsGeneration(),mode,sourceRevision:2,revision:3,committed:true},receipt={mode,applied:[],skipped:[],pending:[],modules:{}};
  await f.repo.updateSettings(settings=>{settings.input.startupObservedBoot=boot;settings.extensions.bootModesAppliedBoot=boot;settings.extensions.bootModesActiveBoot=boot;},{origin:'boot-mode'});
  const context={exports:{},structuredClone,setTimeout,clearTimeout,console,Map,WeakMap,Promise,window:new EventTarget(),CustomEvent,...bootRuntime.inputReceipts,
   normalizeBootStartupResult:bootModel.normalizeBootStartupResult,cloneBootValue:bootModel.cloneBootValue,normalizeBootConfig:bootModel.normalizeBootConfig,normalizeContinuousControls:models.normalizeContinuousControls,normalizeControlPowerSchemes:models.normalizeControlPowerSchemes,performanceConsumptionSignature:models.performanceConsumptionSignature,frameRatePair:frameModel.frameRatePair,
   snapshotPerformanceSchedule:service.snapshotPerformanceSchedule,continuousControlsFromSchedule:service.continuousControlsFromSchedule,getSettingsGeneration:f.repo.getSettingsGeneration,assertSettingsGeneration:f.repo.assertSettingsGeneration,loadSettings:repository.loadSettings,updateSettings:repository.updateSettings,updateSettingsForInputReceipt:f.repo.updateSettingsForInputReceipt,on:bootRuntime.ports.on,invoke:bootRuntime.ports.invoke,onMonitorOutputApplied:bootRuntime.ports.onMonitorOutputApplied,getPolicyGame:target.getPolicyGame,isGameStatusCurrent:()=>true,onContinuousPerformanceApplied:service.onContinuousPerformanceApplied,applyPerformanceConfiguration:service.applyPerformanceConfiguration};
  vm.runInNewContext(bootCode,context);context.bindBoot(receipt,owner);configureTdp(['completed-manual','restart-suspended','generation-reuse'].includes(kind)?'ack':'none');await context.bootPerformance('performance',requested,savedPerformance,receipt);await context.publishBoot(receipt,owner);const stopBoot=context.startBootObserver();
  const scheduled=new Map(),registrations=[],gameListeners=new Set(),readyListeners=new Set(),frameListeners=new Set(),sourceListeners=new Set(),restoreRows=[];let subscribers=0,coreDecisions=0;
  const subscribe=original=>(...args)=>{subscribers++;const stop=original(...args);let ended=false;return()=>{if(!ended){ended=true;subscribers--;}stop();};};
  const powerListeners=new Map(),powerTimers=new Map();let powerTimerId=0,runtimeNow=Date.now(),powerReadHook=null;
  let nativePower={phase:'ready',generation:100,known:true,hardwareWritesAllowed:true,inputReady:true,notificationPending:false,publicationRevision:1,observedNotificationSequence:1};
  const powerModule=evaluate(path.resolve('src/bridge/powerLifecycleState.ts'),{'./ipc':{invoke:()=>{throw Error('Unexpected singleton power read');},on:()=>()=>{}}},{queueMicrotask});
  const sharedPower=powerModule.createSharedPowerLifecycleSource({read:async()=>({...nativePower}),listen:(name,listener)=>{if(!powerListeners.has(name))powerListeners.set(name,new Set());powerListeners.get(name).add(listener);return()=>powerListeners.get(name).delete(listener);},clock:{setTimeout:(callback,ms)=>{const id=++powerTimerId;powerTimers.set(id,{callback:()=>{powerTimers.delete(id);callback();},ms});return id;},clearTimeout:id=>powerTimers.delete(id)}});
  const stopPowerProbe=sharedPower.onSharedPowerLifecycle(()=>{});
  const broadcastPower=state=>{nativePower={...state};for(const listener of [...(powerListeners.get('power.lifecycleChanged')||[])])listener({...state});};
  const powerTick=async()=>{for(let n=0;n<60;n++)await Promise.resolve();};
  class ResidentDate extends Date {static now(){return runtimeNow;}}
  const monitorListeners=new Set();const monitorSignals=evaluate(path.resolve('src/bridge/monitorSnapshotSignals.ts'),{'./monitorFrameTime':evaluate(path.resolve('src/bridge/monitorFrameTime.ts')),'./monitorCoreTiles':evaluate(path.resolve('src/bridge/monitorCoreTiles.ts'))});
  const runtime=evaluate(runtimeSource,{
   vue:{watch:(_source,listener)=>{sourceListeners.add(listener);return()=>sourceListeners.delete(listener);}},'./performanceControls':models,
   './gamedetect':{isGameStatusCurrent:()=>true,isCurrentGameTarget:target.isPolicyTargetCurrent,subscribeGameTargetReadiness:listener=>{readyListeners.add(listener);return()=>readyListeners.delete(listener);}},
   './gamePolicyTarget':{...target,isPolicyGameInitialized:()=>true,subscribePolicyGameStatus:listener=>{gameListeners.add(listener);return()=>gameListeners.delete(listener);}},'./powerSource':{powerSourceMode:source},'./yeman':yeman,
   './frameRateLimits':{...frameModel,loadGlobalFrameRates:frameService.ensureGlobalFrameRates,applyIndependentFrameRates:frameService.applyIndependentFrameRates,onFrameRatesChanged:listener=>{frameListeners.add(listener);return()=>frameListeners.delete(listener);}},
   './performanceSchedule':{...service,onPerformanceScheduleChanged:subscribe(service.onPerformanceScheduleChanged),onGameCustomConfigChanged:subscribe(service.onGameCustomConfigChanged),onContinuousPerformanceApplied:subscribe(service.onContinuousPerformanceApplied),restorePerformanceScheduleIfConfigured:async(...args)=>{const row={options:structuredClone(args[1])};restoreRows.push(row);row.result=await service.restorePerformanceScheduleIfConfigured(...args);return row.result;}},
   './powerLifecycleState':{...sharedPower,isSharedPowerLifecycleAdmitted:powerModule.isSharedPowerLifecycleAdmitted},
   './powerResume':evaluate(path.resolve('src/bridge/powerResume.ts')),
   './monitorData':{subscribeMonitorSnapshot:listener=>{monitorListeners.add(listener);return()=>monitorListeners.delete(listener);}},'./monitorSnapshotSignals':monitorSignals,
   './autofloat':{setFloatRtssLinked(){},getFloatInfo:()=>({enabled:false})},'./api':{powerLifecycle:{get:async()=>powerReadHook?powerReadHook():({...nativePower})}},'./quickActionLock':{isQuickActionBusy:()=>false},
   './gameCorePolicy':{detectGameCorePolicy:async()=>({smtAvailable:true}),clearGameCorePolicy:async()=>{coreDecisions++;return true;},applyGameCorePolicy:async()=>{coreDecisions++;return {ok:true,applied:true};}},
   '@/scheduler':{registerScheduledTask:(name,ms,run,options)=>{assert.equal(name,'game-policy-runtime-retry');assert.equal(ms,2000);assert.equal(options.pauseWhenHidden,false);assert(!scheduled.has(name),'duplicate original retry');const task={run,ms,options};scheduled.set(name,task);registrations.push({name,ms,options});return()=>{if(scheduled.get(name)===task)scheduled.delete(name);};}},
  },{Date:ResidentDate},'export const __residentState=()=>({active,running,dirty,appliedPerformance,tdpPending,powerHeld:typeof powerHeld==="undefined"?false:powerHeld});');
  const settle=async()=>{for(let n=0;n<4000;n++){await Promise.resolve();if(!runtime.__residentState().running){await context.drainBootQueue();if(!runtime.__residentState().running)return;}}throw Error('Original resident did not settle within bounded microtask fence');};
  const run=async()=>{const task=scheduled.get('game-policy-runtime-retry');if(task)await task.run();await settle();};
  const finishOriginalTdpChecks=async()=>{for(let n=0;n<4;n++){runtimeNow+=12000;await run();}};
  const oldTdp=yeman.setTdp,oldScheme=yeman.getActiveScheme,oldSwitch=yeman.switchScheme,oldBal=yeman.PW.WIN_BAL;let scheme='test-yeman';yeman.getActiveScheme=async()=>scheme;yeman.PW.WIN_BAL='test-bal';yeman.switchScheme=async value=>{assert.equal(value,'bal');scheme='test-bal';};let release,pending;
  try{
   if(lateKinds.includes(kind)){
    await powerTick();const completions=[],stopProbe=service.onContinuousPerformanceApplied((signature,context)=>completions.push({signature,context}));
    const oldCpu=yeman.applyManualPowerParams,oldFrameRead=frameService.loadGlobalFrameRates;
    try{
     runtime.startGamePolicyRuntime();await settle();await run();
     assert.equal(completions.length,0,'partial bank published before a TDP ACK');assert.equal(f.document().extensions.bootModesStartupResult.modules.performance.state,'waiting');
     assert.equal(scheduled.size,1);const initialCpu=hardwareWrites().filter(row=>row.kind==='cpu').length;
     let current=true,expectComplete=['late-tdp-once','late-tdp-windows','late-tdp-auto-no-float','late-tdp-wake',...lateProofKinds].includes(kind);
     if(kind==='late-tdp-config'){
      const prior=structuredClone((await service.loadPerformanceSchedule()).controls.ac),priorRevision=repository.getSettingsSectionRevision('performanceSchedule');
      await repository.updateSettings(settings=>{settings.performanceSchedule.controls.ac.manual.aggressiveness=31;});await service.loadPerformanceSchedule();const changedRevision=repository.getSettingsSectionRevision('performanceSchedule');assert.notEqual(changedRevision,priorRevision,'config boundary did not observe its changed revision');
      await repository.updateSettings(settings=>{settings.performanceSchedule.controls.ac=prior;});await service.loadPerformanceSchedule();assert.notEqual(repository.getSettingsSectionRevision('performanceSchedule'),changedRevision);
     }
     if(kind==='late-tdp-frames')await repository.updateSettings(settings=>{settings.rtss.frameRates.ac.fps=90;});
     if(kind==='late-tdp-generation'){f.repo.clearSettingsCache();await repository.loadSettings();}
     if(kind==='late-tdp-game')await f.setPolicyGame({...game,pid:1902,processCreated:'1902'});
     if(kind==='late-tdp-cpu-unconfirmed'){
      yeman.applyManualPowerParams=async()=>false;
      await service.restorePerformanceScheduleIfConfigured(undefined,{withReceipt:true,restoreRemembered:true}).catch(()=>{});
      yeman.applyManualPowerParams=oldCpu;
     }
     if(kind==='late-tdp-stop')await service.stopPerformanceScheduleAndUseWindowsBalanced();
     if(kind==='late-tdp-owner-cancel')current=false;
     if(kind==='late-tdp-frame-read-retry'){
      configureTdp();frameService.loadGlobalFrameRates=async()=>{throw Error('Owned late frame read denied');};
      await assert.rejects(service.reconcilePerformanceScheduleTdp(),/Owned late frame read denied/);await context.drainBootQueue();assert.equal(completions.length,0);
      frameService.loadGlobalFrameRates=oldFrameRead;assert.equal(await service.reconcilePerformanceScheduleTdp(),true);
     }else if(kind==='late-tdp-held-proof'){
      configureTdp();const originalPower={...nativePower};let entered,releaseProof;const entry=new Promise(resolve=>entered=resolve),held=new Promise(resolve=>releaseProof=resolve);
      frameService.loadGlobalFrameRates=async()=>{entered();await held;return oldFrameRead();};
      const admitted=()=>{const power=sharedPower.getSharedPowerLifecycle();return powerModule.isSharedPowerLifecycleAdmitted(power)&&power.generation===originalPower.generation&&power.observedNotificationSequence===originalPower.observedNotificationSequence;};
      const task=service.reconcilePerformanceScheduleTdp(admitted);let timer;try{await Promise.race([entry,new Promise((_,reject)=>timer=setTimeout(()=>reject(Error('late proof read was not entered')),1500))]);}finally{clearTimeout(timer);}
      broadcastPower({...nativePower,phase:'suspended',hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});releaseProof();assert.equal(await task,false);await context.drainBootQueue();assert.equal(completions.length,0,'retired late proof published after shared suspension');
      frameService.loadGlobalFrameRates=oldFrameRead;
      broadcastPower({...nativePower,phase:'ready',generation:101,hardwareWritesAllowed:true,publicationRevision:3});await powerTick();await settle();runtimeNow+=30001;await run();await finishOriginalTdpChecks();
     }else if(kind==='late-tdp-wake'){
      const oldReady={...nativePower};broadcastPower({...nativePower,phase:'suspended',hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});
      assert.equal(scheduled.size,0);for(const listener of powerListeners.get('power.lifecycleChanged'))listener(oldReady);assert.equal(sharedPower.getSharedPowerLifecycle().hardwareWritesAllowed,false);
      nativePower={...nativePower,phase:'ready',generation:101,hardwareWritesAllowed:true,publicationRevision:3};configureTdp();
      assert.equal(powerTimers.size,1);[...powerTimers.values()][0].callback();await powerTick();await settle();
      runtimeNow+=30001;await run();await finishOriginalTdpChecks();
     }else{
      configureTdp(kind==='late-tdp-no-receipt'?'none':kind==='late-tdp-false'?'false':'ack');
      const result=await service.reconcilePerformanceScheduleTdp(()=>current);await context.drainBootQueue();
      if(expectComplete)assert.equal(result,true);
     }
     await context.drainBootQueue();fs.appendFileSync(observations,JSON.stringify({mode,kind,phase:'late-tdp-completion',expectComplete,completions,receipt:structuredClone(f.document().extensions.bootModesStartupResult),cpuBefore:initialCpu,cpuAfter:hardwareWrites().filter(row=>row.kind==='cpu').length})+'\n');assert.equal(completions.length,expectComplete?1:0,kind+': late TDP completion consumed an ineligible CPU/configuration witness');
     assert.equal(f.document().extensions.bootModesStartupResult.modules.performance.state,expectComplete?'active':'waiting');
     assert.equal(hardwareWrites().filter(row=>row.kind==='cpu').length,initialCpu,'TDP-only completion replayed CPU');
     if(expectComplete){
      for(let duplicate=0;duplicate<5;duplicate++)assert.equal(await service.reconcilePerformanceScheduleTdp(),true);
      await context.drainBootQueue();assert.equal(completions.length,1,'healthy TDP check republished the completed bank');
     }
     rows.push({mode,residentKind:kind,actualService:true,actualResident:true,actualBootObserver:true,actualSharedPowerSource:true,expectComplete,completions,cpuWrites:initialCpu,finalReceipt:structuredClone(f.document().extensions.bootModesStartupResult),finalState:runtime.__residentState()});
    }finally{stopProbe();yeman.applyManualPowerParams=oldCpu;frameService.loadGlobalFrameRates=oldFrameRead;}
    continue;
   }
   if(continuationKinds.includes(kind)){
    await powerTick();configureTdp();
    let readCount=0,releaseOld,releaseNew;const oldHold=new Promise(resolve=>releaseOld=resolve),newHold=new Promise(resolve=>releaseNew=resolve),oldReady={...nativePower};
    release=()=>{releaseOld();releaseNew();};
    powerReadHook=async()=>{const index=++readCount;if(index===1){await oldHold;return oldReady;}if(index===2&&kind==='held-read-finally')await newHold;return {...nativePower};};
    const before=hardwareWrites().length;runtime.startGamePolicyRuntime();await powerTick();pending=scheduled.get('game-policy-runtime-retry').run();await powerTick();assert.equal(readCount,1,'original held read was not entered');
    if(kind==='held-read-restart'){runtime.stopGamePolicyRuntime();assert.equal(runtime.__residentState().running,false,'stop retained its retired running owner');runtime.startGamePolicyRuntime();await powerTick();void scheduled.get('game-policy-runtime-retry')?.run();}
    else if(kind==='held-read-sequence'){broadcastPower({...nativePower,publicationRevision:2,observedNotificationSequence:2});await powerTick();}
    else if(kind==='held-read-game'){await f.setPolicyGame({...game,pid:1902,processCreated:'1902'});for(const listener of gameListeners)listener(target.getPolicyGame());}
    else {broadcastPower({...nativePower,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2});assert.equal(hardwareWrites().length,before);broadcastPower({...nativePower,phase:'ready',generation:101,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3});}
    for(let n=0;n<2000;n++)await Promise.resolve();
    assert(readCount>=2,'later admitted resident work remained blocked behind old read');
    if(kind==='held-read-finally'){
      assert.equal(runtime.__residentState().running,true);releaseOld();await pending;pending=null;await powerTick();assert.equal(runtime.__residentState().running,true,'retired finally cleared the successor running owner');const count=readCount;void scheduled.get('game-policy-runtime-retry')?.run();await powerTick();assert.equal(readCount,count,'retired finally admitted a duplicate successor');releaseNew();
    }else{assert(hardwareWrites().length>before,'new admitted owner did not make progress before old read returned');releaseOld();await pending;pending=null;}
    await settle();const afterOld=hardwareWrites().length;await powerTick();assert.equal(hardwareWrites().length,afterOld,'retired read replayed hardware');
    for(let n=0;n<4;n++){runtimeNow+=12000;await run();}assert.equal(scheduled.size,0,'new owner stranded original bounded retry');
    const healthy=hardwareWrites().length;for(let n=0;n<10;n++){broadcastPower(kind==='held-read-sequence'?{...nativePower,publicationRevision:nativePower.publicationRevision+1,observedNotificationSequence:nativePower.observedNotificationSequence+1}:{...nativePower});await powerTick();await run();}assert.equal(hardwareWrites().length,healthy,'duplicate or metadata Ready replayed healthy owner');
    rows.push({mode,residentKind:kind,actualResident:true,actualService:true,actualRepository:true,actualSharedPowerSource:true,heldOldReadBypassed:true,oldFinallyOwnerIsolated:kind==='held-read-finally',readCount,originalRetryMs:2000,finalState:runtime.__residentState()});continue;
   }
   if(kind==='sleep-boundaries'){
    await powerTick();configureTdp();
    let entered,releaseRead;const entry=new Promise(resolve=>entered=resolve),held=new Promise(resolve=>releaseRead=resolve);const oldReady={...nativePower};
    powerReadHook=async()=>{entered();await held;return oldReady;};
    const before=hardwareWrites().length;runtime.startGamePolicyRuntime();pending=run();let entryTimer;try{await Promise.race([entry,new Promise((_,reject)=>entryTimer=setTimeout(()=>reject(Error("Original resident did not enter its held power read")),1500))]);}finally{clearTimeout(entryTimer);}
    const pendingPower={...nativePower,notificationPending:true,hardwareWritesAllowed:false,publicationRevision:2,observedNotificationSequence:2};broadcastPower(pendingPower);
    // A late old Ready is fed to the actual shared source, not a private gate.
    for(const listener of [...powerListeners.get('power.lifecycleChanged')])listener({...oldReady});
    assert.equal(sharedPower.getSharedPowerLifecycle().notificationPending,true,'old Ready reopened pending native fact');
    powerReadHook=null;releaseRead();await pending;pending=null;
    assert.equal(hardwareWrites().length,before,'sleep-pending old resident crossed its shared power boundary');
    assert.equal(scheduled.size,0,'shared power boundary failed to park the original resident retry');
    // Drop the Ready push; drive only the real shared owner's existing retry.
    nativePower={...pendingPower,notificationPending:false,hardwareWritesAllowed:true,publicationRevision:3};
    assert.equal(powerTimers.size,1);[...powerTimers.values()][0].callback();await powerTick();
    assert(scheduled.size===1||runtime.__residentState().running,'missing Ready notification stranded original resident debt');
    await settle();for(let n=0;n<4;n++){runtimeNow+=12000;await run();}
    assert.equal(scheduled.size,0,'resident failed to settle its original bounded checks');
    const healthy=hardwareWrites().length,baselineCpu=hardwareWrites().filter(r=>r.kind==='cpu').length,baselineFps=hardwareWrites().filter(r=>r.kind==='fps').length,baselineCore=coreDecisions;
    const cycles=[];
    for(let cycle=0;cycle<20;cycle++){
      const savedTask=[...scheduled.values()][0],before=hardwareWrites().length;
      const blocked={...nativePower,phase:'suspended',generation:101+cycle,notificationPending:false,hardwareWritesAllowed:false,publicationRevision:nativePower.publicationRevision+1,observedNotificationSequence:nativePower.observedNotificationSequence+1};broadcastPower(blocked);
      assert.equal(scheduled.size,0);if(savedTask)await savedTask.run();assert.equal(hardwareWrites().length,before,'suspended resident performed work');
      const ready={...blocked,phase:'ready',hardwareWritesAllowed:true,publicationRevision:blocked.publicationRevision+1};
      if(cycle%2){nativePower=ready;assert.equal(powerTimers.size,1);[...powerTimers.values()][0].callback();await powerTick();}else broadcastPower(ready);
      await powerTick();await settle();for(let n=0;n<4;n++){runtimeNow+=12000;await run();}
      assert.equal(scheduled.size,0,'continuous sleep cycle stranded resident retry');
      const delta=hardwareWrites().slice(before);assert(delta.filter(r=>r.kind==='tdp').length>0&&delta.filter(r=>r.kind==='tdp').length<=3,'wake lost or refunded bounded TDP checks');
      assert.equal(hardwareWrites().filter(r=>r.kind==='cpu').length,baselineCpu,'wake replayed unchanged CPU owner');assert.equal(hardwareWrites().filter(r=>r.kind==='fps').length,baselineFps,'wake replayed frame ownership');assert.equal(coreDecisions,baselineCore,'wake replayed process affinity');
      const settledWrites=hardwareWrites().length;for(let duplicate=0;duplicate<10;duplicate++)broadcastPower(ready);await powerTick();await run();assert.equal(hardwareWrites().length,settledWrites,'duplicate Ready replayed a settled performance task');
      cycles.push({generation:ready.generation,missingReadyPush:cycle%2===1,tdpWrites:delta.filter(r=>r.kind==='tdp').length});
    }
    assert(hardwareWrites().length>healthy);rows.push({mode,residentKind:kind,actualResident:true,actualService:true,actualRepository:true,actualSharedPowerSource:true,cycles,heldReadDiscarded:true,originalRetryMs:2000,finalState:runtime.__residentState()});continue;
   }
   if(kind==='generation-reuse'){
    const previousGeneration=f.repo.getSettingsGeneration();f.repo.clearSettingsCache();await repository.loadSettings();assert.notEqual(f.repo.getSettingsGeneration(),previousGeneration);
    const count=hardwareWrites().length,recovered=await service.restorePerformanceScheduleIfConfigured(undefined,{withReceipt:true,restoreRemembered:true});
    fs.appendFileSync(observations,JSON.stringify({mode,kind,phase:'generation',previousGeneration,generation:f.repo.getSettingsGeneration(),recovered,hardware:structuredClone(hardwareWrites().slice(count))})+'\n');
    assert.equal(recovered.reusedCompletedIntent,undefined,'completed manual intent from a previous settings generation must not be reused');assert.equal(recovered.applied,true);assert.equal(hardwareWrites().slice(count).filter(row=>row.kind==='tdp').length,1);assert.equal(hardwareWrites().slice(count).filter(row=>row.kind==='cpu').length,1);
   }
   if(kind==='forged-completion'){
    const snapshot=await service.getContinuousPerformanceSnapshot('global'),identity=JSON.stringify([game.pid,game.processCreated,game.path.toLowerCase(),game.identityGeneration??0]),signature=models.performanceConsumptionSignature(['global',true,true],identity,'ac',snapshot.frameRates.ac.fps,snapshot.bank,snapshot.powerScheme);
    const count=hardwareWrites().length,forged=await service.restorePerformanceScheduleIfConfigured(undefined,{withReceipt:true,completedStartupIntent:()=>signature});
    fs.appendFileSync(observations,JSON.stringify({mode,kind,phase:'forged',forged,hardware:structuredClone(hardwareWrites().slice(count))})+'\n');
    assert.equal(forged.applied,false,'caller-supplied intent without an actual completed service witness must not confirm application');assert.equal(forged.reusedCompletedIntent,undefined);assert.equal(hardwareWrites().slice(count).filter(row=>row.kind==='tdp').length,1);assert.equal(hardwareWrites().slice(count).filter(row=>row.kind==='cpu').length,1);
   }
   if(kind==='suspended')await service.stopPerformanceScheduleAndUseWindowsBalanced();
   await powerTick();const initialWrites=hardwareWrites().length;runtime.startGamePolicyRuntime();
   if(['inflight-stop','inflight-game'].includes(kind)){
    let entered;const entry=new Promise(resolve=>{entered=resolve;}),held=new Promise(resolve=>{release=resolve;});yeman.setTdp=async(...args)=>{entered();await held;return oldTdp(...args);};pending=run();let timer;await Promise.race([entry,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Original resident did not reach TDP writer')),1500);})]).finally(()=>clearTimeout(timer));
    let stopping;if(kind==='inflight-stop')stopping=service.stopPerformanceScheduleAndUseWindowsBalanced();else{await f.setPolicyGame({...game,pid:1902,processCreated:'1902'});for(const listener of gameListeners)listener(target.getPolicyGame());}
    release();await pending;pending=null;if(stopping)await stopping;yeman.setTdp=oldTdp;await settle();
   }else{await settle();await run();}
   const initial={state:runtime.__residentState(),scheduled:scheduled.size,restoreRows:structuredClone(restoreRows),receipt:structuredClone(f.document().extensions.bootModesStartupResult),hardware:structuredClone(hardwareWrites().slice(initialWrites)),coreDecisions};
   fs.appendFileSync(observations,JSON.stringify({mode,kind,phase:'initial',...initial})+'\n');
   if(kind.startsWith('partial-')||kind==='forged-completion'){
    assert.equal(scheduled.size,1,mode+'/'+kind+': partial resident performance must retain the original retry instead of memorizing ownership as applied');assert.equal(initial.state.appliedPerformance,'');assert.equal(initial.receipt.modules.performance.state,'waiting');
    assert.equal(initial.hardware.filter(row=>row.kind==='fps').length,1,'partial TDP must not suppress the independent frame-rate channel');
    assert.equal(initial.coreDecisions,1,'partial TDP must not suppress independent game-core ownership');
    const before=restoreRows.length,cpuBefore=hardwareWrites().filter(row=>row.kind==='cpu').length;await run();assert.equal(coreDecisions,1,'performance retry replayed already-confirmed core ownership');assert.equal(restoreRows.length,before,'TDP-only retry replayed the acknowledged CPU bank');configureTdp();runtimeNow+=30001;await run();await finishOriginalTdpChecks();assert.equal(restoreRows.length,before,'TDP confirmation replayed the completed CPU bank');assert.equal(hardwareWrites().filter(row=>row.kind==='cpu').length,cpuBefore,'TDP retry changed CPU writes');assert.equal(scheduled.size,0);assert.equal(f.document().extensions.bootModesStartupResult.modules.performance.state,'active');assert(runtime.__residentState().appliedPerformance,'actual late float completion did not confirm the resident owner');const hardware=hardwareWrites().length;for(let n=0;n<10;n++)await run();assert.equal(hardwareWrites().length,hardware,'healthy acknowledged bank kept replaying');
   }else if(kind==='completed-manual'||kind==='generation-reuse'){
    assert(restoreRows.at(-1).result.applied);assert.equal(restoreRows.at(-1).result.reusedCompletedIntent,true);assert.equal(initial.hardware.filter(row=>row.kind==='cpu'||row.kind==='tdp').length,0,'confirmed manual reuse replayed hardware');await finishOriginalTdpChecks();assert.equal(scheduled.size,0);
   }else if(kind==='suspended'){
    assert.equal(scheduled.size,0,'explicit suspension left a perpetual retry');assert.equal(initial.hardware.filter(row=>row.kind==='cpu'||row.kind==='tdp').length,0);assert.equal(initial.state.appliedPerformance,'','suspension was cached as active application');
   }else if(kind==='restart-suspended'){
    runtime.stopGamePolicyRuntime();assert.equal(runtime.__residentState().appliedPerformance,'','stopped runtime retained its completed intent memo');await service.stopPerformanceScheduleAndUseWindowsBalanced();const count=hardwareWrites().length;runtime.startGamePolicyRuntime();await settle();await run();assert.equal(scheduled.size,0);assert.equal(runtime.__residentState().appliedPerformance,'');assert.equal(hardwareWrites().slice(count).filter(row=>row.kind==='cpu'||row.kind==='tdp').length,0);
    const snapshot=await service.getContinuousPerformanceSnapshot('global');await service.updateContinuousPerformanceControls(snapshot.identity,{mode:'manual'});await settle();await run();await finishOriginalTdpChecks();assert.equal(scheduled.size,0);assert(runtime.__residentState().appliedPerformance);
   }else if(kind==='inflight-stop'){
    await run();assert.equal(scheduled.size,0);assert.equal(runtime.__residentState().appliedPerformance,'');assert(!initial.hardware.some(row=>row.kind==='cpu'),'cancelled resident result derived CPU writes');
   }else{configureTdp();await run();await finishOriginalTdpChecks();assert.equal(scheduled.size,0);assert.equal(f.document().extensions.bootModesStartupResult.modules.performance.state,'active');}
   rows.push({mode,residentKind:kind,initialState:initial.state,finalState:runtime.__residentState(),originalRetryMs:2000,restoreRows:structuredClone(restoreRows),finalReceipt:structuredClone(f.document().extensions.bootModesStartupResult),actualService:true,actualResident:true,actualBootObserver:true,registrations});
  }finally{release?.();if(pending)await pending.catch(()=>{});const late=[...scheduled.values()];runtime.stopGamePolicyRuntime();runtime.stopGamePolicyRuntime();const stoppedWrites=hardwareWrites().length;for(const task of late)await task.run();stopBoot();if(actualFloat){await actualFloat.runtime.disableFloat();configureFloat(null);}await context.drainBootQueue();assert.equal(hardwareWrites().length,stoppedWrites,'stopped resident retry revived hardware work');yeman.setTdp=oldTdp;yeman.getActiveScheme=oldScheme;yeman.switchScheme=oldSwitch;yeman.PW.WIN_BAL=oldBal;configureTdp();assert.equal(scheduled.size,0);assert.equal(subscribers,0);assert.equal(gameListeners.size+readyListeners.size+frameListeners.size+sourceListeners.size,0);assert.equal(monitorListeners.size,0);stopPowerProbe();assert.equal(sharedPower.getSharedPowerLifecycleDiagnostics().subscriberCount,0);assert.equal(powerTimers.size,0);assert.equal([...powerListeners.values()].reduce((n,listeners)=>n+listeners.size,0),0);}
 }
 const report={pass:true,selection:'resident-only',default173:'NOT_RUN_EXPLICIT_COST_BOUNDARY',rows,runtimeSource,scope:'Actual performance service, original resident runtime and boot observer/repository. Only original scheduler/game/power/hardware terminals controlled; state getter appended to the evaluated module for observation.'};fs.writeFileSync(path.join(out,'performance-preferences.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});return report;
}
