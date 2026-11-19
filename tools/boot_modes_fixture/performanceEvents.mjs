import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import assert from 'node:assert/strict';
/** Explicit bounded selection under the original boot runtime identity. Shares
 * the actual existing float fixture and its memory hardware ports. */
export async function runPerformanceEventOwners({out,buildFixture,savedPerformance,repository,source,target,yeman,freshSchedule,configureTdp,hardwareWrites,configureFloat,bootCode,bootRuntime,bootModel,models,frameModel}){
 const {floatFixture}=await import('../continuous_float_runtime_selftest.mjs');
 const kinds=['late-float','early-valid','early-republish','early-newer','stop-late','game-change'];
 const argument=process.argv.find(value=>value.startsWith('--boot-performance-event-case=')),selected=argument?.split('=')[1];assert(!argument||kinds.includes(selected),'Unknown original performance event case');
 const observations=path.join(out,'event-observations.jsonl');fs.closeSync(fs.openSync(observations,'wx'));const rows=[];
 for(const mode of ['global','xbox','steam','xbox-steam'])for(const kind of selected?[selected]:kinds){
  const policyGame={pid:101,processCreated:'birth',path:'C:\\fixture\\fixture.exe',name:'fixture.exe'},f=await buildFixture(mode,{policyGame}),boot='actual-float-boot';
  await floatFixture.reset();configureFloat(floatFixture.runtime);const service=freshSchedule(),originalTdp=yeman.setTdp,oldScheme=yeman.getActiveScheme,oldSwitch=yeman.switchScheme,oldBal=yeman.PW.WIN_BAL;
  let hold=false,entered,release;const entry=new Promise(resolve=>{entered=resolve;}),held=new Promise(resolve=>{release=resolve;});const trace=[];
  yeman.setTdp=async(side,watts,options={})=>{const row={source:options.source,side,watts};trace.push(row);if(hold&&options.source==='autofloat'){entered();await held;}const result=await originalTdp(side,watts,{...options,onAccepted:value=>{row.accepted=value;options.onAccepted?.(value);}});row.result=result;return result;};
  let scheme='test-yeman';yeman.PW.WIN_BAL='test-bal';yeman.getActiveScheme=async()=>scheme;yeman.switchScheme=async value=>{assert.equal(value,'bal');scheme='test-bal';};
  floatFixture.bind({yeman,repository:{getSettingsGeneration:f.repo.getSettingsGeneration,assertSettingsGeneration:f.repo.assertSettingsGeneration,readSettingsSection:repository.readSettingsSection,saveSettingsSection:repository.saveSettingsSection},game:target.getPolicyGame});
  const events=[],stopProbe=service.onContinuousPerformanceApplied((signature,context)=>events.push({signature,context}));let observed,bootCalls=0,stop,pending;
  const context={exports:{},structuredClone,setTimeout,clearTimeout,console,Map,WeakMap,Promise,window:new EventTarget(),CustomEvent,...bootRuntime.inputReceipts,
   normalizeBootStartupResult:bootModel.normalizeBootStartupResult,cloneBootValue:bootModel.cloneBootValue,normalizeBootConfig:bootModel.normalizeBootConfig,
   normalizeContinuousControls:models.normalizeContinuousControls,normalizeControlPowerSchemes:models.normalizeControlPowerSchemes,performanceConsumptionSignature:models.performanceConsumptionSignature,frameRatePair:frameModel.frameRatePair,
   snapshotPerformanceSchedule:service.snapshotPerformanceSchedule,continuousControlsFromSchedule:service.continuousControlsFromSchedule,
   getSettingsGeneration:f.repo.getSettingsGeneration,assertSettingsGeneration:f.repo.assertSettingsGeneration,loadSettings:repository.loadSettings,updateSettings:repository.updateSettings,updateSettingsForInputReceipt:f.repo.updateSettingsForInputReceipt,
   on:bootRuntime.ports.on,invoke:bootRuntime.ports.invoke,onMonitorOutputApplied:bootRuntime.ports.onMonitorOutputApplied,getPolicyGame:target.getPolicyGame,isGameStatusCurrent:()=>true,onContinuousPerformanceApplied:service.onContinuousPerformanceApplied,
   applyPerformanceConfiguration:async(...args)=>{bootCalls++;observed=await service.applyPerformanceConfiguration(...args);return observed;},
  };vm.runInNewContext(bootCode,context);
  const owner={boot,generation:f.repo.getSettingsGeneration(),mode,sourceRevision:2,revision:3,committed:true},receipt={mode,applied:[],skipped:[],pending:[],modules:{}};context.bindBoot(receipt,owner);
  const requested=structuredClone(savedPerformance);requested.controls.ac.tdp=24;requested.controls.ac.tdpCeiling=30;
  try{
   await f.repo.updateSettings(settings=>{settings.input.startupObservedBoot=boot;settings.extensions.bootModesAppliedBoot=boot;delete settings.extensions.bootModesActiveBoot;delete settings.extensions.bootModesStartupResult;},{origin:'boot-mode'});
   configureTdp('none');stop=context.startBootObserver();await context.bootPerformance('performance',requested,savedPerformance,receipt);assert.equal(receipt.modules.performance.state,'waiting');assert.equal(observed.applied,false);assert.equal(events.length,0);
   const publish=async()=>{await context.publishBoot(receipt,owner);await f.repo.updateSettings(settings=>{settings.extensions.bootModesActiveBoot=boot;},{origin:'runtime'});context.window.dispatchEvent(new CustomEvent('boot-mode:changed'));};
   if(['late-float','stop-late','game-change'].includes(kind)){await publish();assert.equal(await service.restorePerformanceScheduleIfConfigured(),'auto');}
   if(kind==='early-republish'){f.state.writeFailure=true;try{await context.publishBoot(receipt,owner);}finally{f.state.writeFailure=false;}assert.equal(f.document().extensions.bootModesStartupResult,undefined);await f.repo.loadSettings();}
   await floatFixture.step();assert.equal(events.length,0);configureTdp();floatFixture.advance(31000);
   const cpuBefore=hardwareWrites().filter(row=>row.kind==='cpu').length;
   let stopping;
   if(['stop-late','game-change'].includes(kind)){
    hold=true;pending=floatFixture.step();let timer;await Promise.race([entry,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Actual float did not enter its TDP writer')),1500);})]).finally(()=>clearTimeout(timer));
    if(kind==='game-change')await f.setPolicyGame({...policyGame,pid:202,processCreated:'new-birth'});
    else{stopping=service.stopPerformanceScheduleAndUseWindowsBalanced();for(let n=0;n<10;n++)await Promise.resolve();}
    hold=false;release();await pending;pending=null;if(stopping)await stopping;
   }else await floatFixture.step();
   await context.drainBootQueue();
   const beforePublication=structuredClone(f.document().extensions.bootModesStartupResult),earlyEvents=structuredClone(events);
   if(kind==='early-newer'){
    await f.repo.updateSettings(settings=>{settings.extensions.bootModes.revision++;settings.extensions.bootModesStartupResult={schema:1,boot,mode,sourceRevision:3,revision:4,status:'partial',modules:{performance:{state:'waiting',requested}},applied:[],skipped:[],pending:['性能调度：newer startup']};settings.extensions.bootModesActiveBoot=boot;},{origin:'boot-mode'});
    await context.publishBoot(receipt,owner);context.window.dispatchEvent(new CustomEvent('boot-mode:changed'));
   }else if(['early-valid','early-republish'].includes(kind))await publish();
   await context.drainBootQueue();const actual=f.document().extensions.bootModesStartupResult;
   fs.appendFileSync(observations,JSON.stringify({mode,kind,observed,events:earlyEvents,beforePublication,actual,trace,bootCalls,cpuBefore,cpuAfter:hardwareWrites().filter(row=>row.kind==='cpu').length,float:floatFixture.runtime.getFloatInfo()})+'\n');
   const active=['late-float','early-valid','early-republish'].includes(kind);
   assert.equal(actual?.modules.performance.state,active?'active':'waiting',mode+'/'+kind+': actual float completion must reconcile only its exact startup request');
   assert.equal(bootCalls,1,'event observer replayed boot performance application');assert.equal(observed.applied,false,'late completion mutated the original partial return');
   assert.equal(hardwareWrites().filter(row=>row.kind==='cpu').length,cpuBefore,'receipt reconciliation replayed the CPU bank');
   if(!['stop-late','game-change'].includes(kind)){assert.equal(events.length,1);assert.equal(events[0].context?.revision,3,'early actual completion lost its original startup identity');}
   else assert.equal(events.length,0,'obsolete float TDP ACK escaped cancellation');
   if(kind==='early-newer')assert.equal(actual.revision,4);
   if(active){const writes=f.state.writes.length;await floatFixture.runtime.__test.applyTdpLimit(24,true);await context.drainBootQueue();assert.equal(f.state.writes.length,writes,'healthy float TDP write repeated startup receipt');assert.equal(events.length,1,'healthy float repeated full-bank completion');}
   rows.push({mode,eventKind:kind,active,actualFloat:true,actualService:true,actualBootObserver:true,bootApplications:bootCalls,completionEvents:events.length,initialPartialReturnPreserved:true});
  }finally{hold=false;release();if(pending)await pending.catch(()=>{});stop?.();stop?.();stopProbe();await floatFixture.runtime.disableFloat();await context.drainBootQueue();configureFloat(null);configureTdp();yeman.setTdp=originalTdp;yeman.getActiveScheme=oldScheme;yeman.switchScheme=oldSwitch;yeman.PW.WIN_BAL=oldBal;}
 }
 const report={pass:true,selection:'events-only',default173:'NOT_RUN_EXPLICIT_COST_BOUNDARY',rows,scope:'Actual original float + performance service + boot observer + repository, virtual clock and shared memory hardware terminals; no installed app.',inputSources:['src/bridge/autofloat.ts','src/bridge/performanceSchedule.ts','src/bridge/bootModes.ts','tools/continuous_float_runtime_selftest.mjs','tools/continuous_performance_selftest.mjs','tools/boot_modes_fixture/performanceEvents.mjs','tools/boot_modes_fixture/performancePreferences.mjs']};
 fs.writeFileSync(path.join(out,'performance-preferences.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});return report;
}
