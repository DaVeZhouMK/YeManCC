import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {compileSettingsConsumer,createSettingsFixture} from '../fixtures/settings_baseline.mjs';

/** Run the existing complete performance owner against the real repository.
 * Hardware/power/game owners remain the original fixture's inert ports. */
export async function runPerformancePreferenceOwners({out,schedule,models,frameModel,repository,evaluate,configure,currentSettings,source,target,yeman,freshSchedule,configureTdp,hardwareWrites,configureFloat,frameService}){
 fs.mkdirSync(out,{recursive:true});
 const code=await compileSettingsConsumer(process.cwd(),process.argv.includes('--boot-performance-resident-only')?{onCompiled:compiled=>{fs.writeFileSync(path.join(out,'repository.metafile.json'),JSON.stringify(compiled.metafile,null,2),{flag:'wx'});fs.writeFileSync(path.join(out,'repository.cjs'),compiled.outputFiles[0].text,{flag:'wx'});}}:{}),rows=[];
 const controls=structuredClone(models.normalizeContinuousControls(undefined));for(const side of ['ac','dc']){controls[side].tdp=15;controls[side].tdpCeiling=30;}
 const globalFrames=structuredClone(frameModel.frameRatePair({},120,60));
 const base=structuredClone(schedule.defaultPerformanceScheduleConfig());base.controls=controls;base.configured=true;base.enabled=true;base.suspended=false;
 const savedPerformance={schedule:structuredClone(base),controls:structuredClone(controls),powerSchemes:structuredClone(models.normalizeControlPowerSchemes(base.powerSchemes)),frames:globalFrames};
 const buildFixture=async(mode,{missing=false,invalid,empty=false,template='portable',policyGame=null}={})=>{
  const active=structuredClone(base);active.controls.ac.tdpCeiling=49;active.controls.dc.tdpCeiling=40;
  const doc={schema:1,revision:3,current:mode,boot:mode,bootEnabled:true,global:{performance:structuredClone(savedPerformance)},
   scenes:Object.fromEntries(['xbox','steam','xbox-steam'].map(id=>[id,{name:id,overrides:{performance:{...structuredClone(savedPerformance),controls:structuredClone(active.controls)}}}])),presets:{},peer:'keep'};
  if(missing){doc.global.performance={};doc.unresolvedGlobal=['performance'];}
  if(invalid==='schema')doc.schema=99;if(invalid==='revision')doc.revision=Number.MAX_SAFE_INTEGER;
  const f=createSettingsFixture(code,{extensions:{bootModes:doc,peerExtension:{keep:true}},performanceSchedule:empty?{configured:false,enabled:false}:active,
   rtss:{frameRates:frameModel.frameRatePair({},90,35),templateDefaults:false},futureOwner:{keep:true}});
  await f.repo.loadSettings();
  const sync=async(snapshot)=>configure(snapshot??await f.repo.loadSettings(),f.repo.getSettingsGeneration(),template,policyGame);await sync();
  Object.assign(repository,{
   loadSettings:async()=>{const value=await f.repo.loadSettings();await sync(value);return value;},
   readSettingsSection:async key=>{const value=await f.repo.readSettingsSection(key);await sync();return value;},
   getSettingsGeneration:()=>f.repo.getSettingsGeneration(),assertSettingsGeneration:value=>f.repo.assertSettingsGeneration(value),
   updateSettings:async(mutator,options)=>{await f.repo.updateSettings(mutator,options);await sync();},
   saveSettingsSection:async(section,value,generation,options)=>{await f.repo.saveSettingsSection(section,value,generation,options);await sync();},
   rememberOrdinaryPerformancePreferences:f.repo.rememberOrdinaryPerformancePreferences,
  });
  f.setPolicyGame=async next=>{policyGame=next;await sync();};
  return f;
 };
 const global=f=>f.document().extensions.bootModes.global.performance;
 const controlsOnly=process.argv.includes('--boot-performance-controls-only');
 const controlsSelectors=process.argv.filter(value=>value.startsWith('--boot-performance-controls-case='));
 assert(controlsSelectors.length<=1,'Duplicate performance control-source selector');
 const controlsCase=controlsSelectors[0]?.slice('--boot-performance-controls-case='.length);
 const invalidControls=process.argv.includes('--boot-performance-controls-invalid');
 assert(!invalidControls||controlsOnly,'Invalid control-source group requires its configuration scope');
 const controlKinds=invalidControls?['nested-invalid','legacy-invalid']:['nested-controls','legacy-profiles','top-controls'];
 const controlsFromArgs=process.argv.filter(value=>value.startsWith('--boot-performance-controls-from='));assert(controlsFromArgs.length<=1,'Duplicate performance control-source start');
 const controlsFrom=controlsFromArgs[0]?.slice('--boot-performance-controls-from='.length);
 assert(!controlsFromArgs.length||controlsOnly&&!controlsCase&&['global','xbox','steam','xbox-steam'].some(mode=>controlKinds.some(kind=>mode+'/'+kind===controlsFrom)),'Unknown or conflicting performance control-source start');
 let controlsReached=!controlsFrom;
 const controlsCases=['global','xbox','steam','xbox-steam'].flatMap(mode=>['nested-controls','legacy-profiles','top-controls','nested-invalid','legacy-invalid'].map(kind=>mode+'/'+kind));
 assert(!controlsSelectors.length||controlsOnly&&controlsCases.includes(controlsCase),'Unknown or unscoped performance control-source selector');
 const applicationOnly=controlsOnly||process.argv.includes('--boot-performance-receipts-only'),lifecycleOnly=process.argv.includes('--boot-performance-lifecycle-only'),partialOnly=process.argv.includes('--boot-performance-partial-only'),eventsOnly=process.argv.includes('--boot-performance-events-only'),residentOnly=process.argv.includes('--boot-performance-resident-only');
 assert([applicationOnly,lifecycleOnly,partialOnly,eventsOnly,residentOnly].filter(Boolean).length<=1,'Choose one performance group');
 if(!applicationOnly&&!lifecycleOnly&&!partialOnly&&!eventsOnly&&!residentOnly){
 for(const mode of ['global','xbox','steam','xbox-steam']){
  const f=await buildFixture(mode),original=structuredClone(f.document().extensions.bootModes);
  let snapshot=await schedule.getContinuousPerformanceSnapshot('global','dc');
  await schedule.updateContinuousPerformanceControls(snapshot.identity,{auto:{bias:75}});
  assert.equal(global(f).controls.dc.auto.bias,75);assert.equal(global(f).schedule.controls.dc.auto.bias,75);
  assert.equal(global(f).controls.ac.tdpCeiling,30);assert.equal(global(f).controls.dc.tdpCeiling,30);
  assert.equal(global(f).schedule.controls.ac.tdpCeiling,30);assert.equal(global(f).schedule.controls.dc.tdpCeiling,30);
  assert.deepEqual(global(f).frames,globalFrames);
  assert.equal(f.document().extensions.bootModes.revision,original.revision+1,'one ordinary action must commit one mode revision');
  assert.deepEqual(f.document().extensions.bootModes.scenes,original.scenes);
  snapshot=await schedule.getContinuousPerformanceSnapshot('global','dc');
  await schedule.updateContinuousPerformanceControls(snapshot.identity,{frameRate:{fps:45}});
  assert.equal(global(f).frames.dc.fps,45);assert.equal(global(f).frames.ac.fps,120);
  assert.equal(f.document().rtss.frameRates.ac.fps,90);
  snapshot=await schedule.getContinuousPerformanceSnapshot('global','dc');
  await schedule.updateContinuousPerformanceControls(snapshot.identity,{mode:'manual'});
  snapshot=await schedule.getContinuousPerformanceSnapshot('global','dc');
  await schedule.updateContinuousPerformanceControls(snapshot.identity,{powerScheme:'bal'});
  assert.equal(global(f).powerSchemes.dc,'bal');assert.equal(global(f).schedule.powerSchemes.dc,'bal');
  assert.equal(global(f).powerSchemes.ac,'yeman');assert.equal(global(f).controls.ac.tdpCeiling,30);
  snapshot=await schedule.getContinuousPerformanceSnapshot('global','dc');
  await schedule.updateContinuousPerformanceControls(snapshot.identity,{hyperThreadMode:'off'});
  assert.equal(global(f).schedule.hyperThreadPolicy,'off');assert.equal(global(f).controls.ac.tdpCeiling,30);
  assert.deepEqual(f.document().extensions.bootModes.scenes,original.scenes);
  const full=await schedule.loadPerformanceSchedule(),beforeSuspend=structuredClone(global(f));full.suspended=true;full.enabled=false;
  await schedule.savePerformanceSchedule(full);
  assert.equal(global(f).schedule.suspended,true);assert.equal(global(f).schedule.enabled,false);
  assert.equal(global(f).controls.ac.tdpCeiling,beforeSuspend.controls.ac.tdpCeiling);
  const saved=structuredClone(f.document().extensions.bootModes),runtime=await schedule.loadPerformanceSchedule();runtime.suspended=false;runtime.enabled=true;
  await schedule.savePerformanceSchedule(runtime,{origin:'runtime'});assert.deepEqual(f.document().extensions.bootModes,saved);
  runtime.controls.ac.tdpCeiling=70;
  const legacyOwnership=await schedule.applyPerformanceConfiguration(runtime,frameModel.frameRatePair({},200,60));
  assert.equal(legacyOwnership,runtime.controls.ac.mode,'legacy callers must retain the original ownership-string result');
  assert.deepEqual(f.document().extensions.bootModes,saved,'startup restoration contaminated ordinary global intent');
  assert.equal(f.document().performanceSchedule.controls.ac.tdpCeiling,70);
  assert.equal(f.document().futureOwner.keep,true);assert.equal(f.document().extensions.peerExtension.keep,true);
  rows.push({mode,wholeActualOwner:true,realRepository:true,changedLeavesOnly:true,powerAndSmtConnected:true,
   ordinaryFrameSiblingPreserved:true,runtimeAndBootOriginsIsolated:true,independentScenesPreserved:true});
 }
 {
  const f=await buildFixture('steam',{missing:true}),snapshot=await schedule.getContinuousPerformanceSnapshot('global','dc');
  await schedule.updateContinuousPerformanceControls(snapshot.identity,{auto:{bias:75}});
  assert.equal(global(f).controls.dc.auto.bias,75);assert.equal(global(f).controls.ac,undefined);assert.equal(global(f).frames,undefined);
  assert.equal(global(f).schedule.controls.ac,undefined);assert(f.document().extensions.bootModes.unresolvedGlobal.includes('performance'));
  rows.push({case:'unresolved origin',partialOrdinaryLeavesOnly:true,referenceNotAdopted:true});
 }
 for(const invalid of ['schema','revision']){
  const f=await buildFixture('xbox',{invalid}),snapshot=await schedule.getContinuousPerformanceSnapshot('global','dc');
  const before=f.document(),writes=f.state.writes.length;
  await assert.rejects(schedule.updateContinuousPerformanceControls(snapshot.identity,{auto:{bias:75}}),/配置无法确认|版本号无效/);
  assert.deepEqual(f.document(),before);assert.equal(f.state.writes.length,writes);rows.push({case:'protected '+invalid,atomicReject:true});
 }
 {
  const f=await buildFixture('steam'),snapshot=await schedule.getContinuousPerformanceSnapshot('global','dc'),before=f.document();
  f.state.writeFailure=true;await assert.rejects(schedule.updateContinuousPerformanceControls(snapshot.identity,{auto:{bias:75}}),/interrupted/);
  assert.deepEqual(f.document(),before);rows.push({case:'write failure',globalAndRuntimeUnchanged:true});
 }
 for(const mode of ['global','xbox','steam','xbox-steam']){
  const f=await buildFixture(mode),doc=structuredClone(f.document().extensions.bootModes);
  const limits=evaluate('src/bridge/frameRateLimits.ts',{'./settingsRepository':repository,'./frameRateModel':frameModel,
   './gamedetect':{isGameStatusCurrent:()=>true,isCurrentGameTarget:()=>true},'./gamePolicyTarget':target,'./yeman':yeman,
   './powerSource':{powerSourceMode:source},'./api':{},'./performanceSchedule':{},'./autofloat':{}});
  await limits.saveGlobalFrameRate('dc',{fps:45});assert.equal(global(f).frames.dc.fps,45);assert.equal(global(f).frames.ac.fps,120);
  assert.equal(f.document().rtss.frameRates.ac.fps,90);assert.equal(f.document().extensions.bootModes.revision,doc.revision+1);
  assert.deepEqual(global(f).controls,savedPerformance.controls);assert.deepEqual(f.document().extensions.bootModes.scenes,doc.scenes);
  rows.push({mode,actualStandaloneFrameOwner:true,otherSideAndControlsPreserved:true});
 }
 for(const mode of ['global','xbox','steam','xbox-steam']){
  for(const service of ['performance-owner','standalone-frame-owner']){
   const f=await buildFixture(mode,{template:'desktop'});
   await f.repo.updateSettings(settings=>{settings.rtss.frameRates=structuredClone(globalFrames);settings.rtss.templateDefaults=true;});
   configure(await f.repo.loadSettings(),f.repo.getSettingsGeneration(),'desktop');
   if(service==='performance-owner'){
    const snapshot=await schedule.getContinuousPerformanceSnapshot('global','dc');assert.equal(snapshot.frameRates.ac.fps,200);
    await schedule.updateContinuousPerformanceControls(snapshot.identity,{frameRate:{fps:45}});
   }else{
    const limits=evaluate('src/bridge/frameRateLimits.ts',{'./settingsRepository':repository,'./frameRateModel':frameModel,
     './gamedetect':{isGameStatusCurrent:()=>true},'./gamePolicyTarget':target,'./yeman':yeman,'./powerSource':{powerSourceMode:source},'./api':{},'./performanceSchedule':{},'./autofloat':{}});
    await limits.saveGlobalFrameRate('dc',{fps:45});
   }
   assert.equal(global(f).frames.ac.fps,120,'editing DC adopted the computed desktop AC default');assert.equal(global(f).frames.dc.fps,45);
   assert.equal(f.document().rtss.frameRates.ac.fps,200);assert.equal(f.document().rtss.templateDefaults,false);
   rows.push({mode,service,computedTemplateSiblingNotPromoted:true});
  }
 }
 {
  const f=await buildFixture('steam'),doc=structuredClone(f.document().extensions.bootModes);
  await f.repo.updateSettings(settings=>{delete settings.rtss.frameRates;});configure(await f.repo.loadSettings(),f.repo.getSettingsGeneration());
  const limits=evaluate('src/bridge/frameRateLimits.ts',{'./settingsRepository':repository,'./frameRateModel':frameModel,
   './gamedetect':{isGameStatusCurrent:()=>true},'./gamePolicyTarget':target,'./yeman':yeman,'./powerSource':{powerSourceMode:source},'./api':{},'./performanceSchedule':{},'./autofloat':{}});
  await limits.ensureGlobalFrameRates();assert.deepEqual(f.document().extensions.bootModes,doc,'passive frame materialization became ordinary intent');
  rows.push({case:'passive frame materialization',runtimeOrigin:true});
 }
 for(const mode of ['global','xbox','steam','xbox-steam'])for(const template of ['portable','desktop']){
  for(const service of ['ordinary-control','schedule-toggle','frame-only','smt-only']){
   const f=await buildFixture(mode,{empty:true,template}),doc=structuredClone(f.document().extensions.bootModes);
   const initial=await schedule.getContinuousPerformanceSnapshot('global','dc'),initialControls=structuredClone(initial.controls);
   assert.equal(initialControls.ac.tdp,template==='desktop'?120:55);
   assert.deepEqual(f.document().extensions.bootModes,doc,'reading a sparse schedule adopted defaults');
   if(service==='ordinary-control')await schedule.updateContinuousPerformanceControls(initial.identity,{auto:{bias:75}});
   else if(service==='schedule-toggle'){
    const full=await schedule.loadPerformanceSchedule();full.suspended=true;await schedule.savePerformanceSchedule(full);
   }else if(service==='frame-only')await schedule.updateContinuousPerformanceControls(initial.identity,{frameRate:{fps:45}});
   else await schedule.updateContinuousPerformanceControls(initial.identity,{hyperThreadMode:'off'});
   assert.deepEqual(global(f).controls.ac,doc.global.performance.controls.ac,`${mode}/${template}/${service}: unchanged AC bank adopted effective defaults`);
   assert.deepEqual(global(f).schedule.controls.ac,doc.global.performance.schedule.controls.ac,`${mode}/${template}/${service}: unchanged saved schedule AC bank adopted effective defaults`);
   assert.equal(global(f).controls.dc.tdpCeiling,30);assert.equal(global(f).controls.dc.tdp,15);
   assert.equal(global(f).schedule.controls.dc.tdpCeiling,30);assert.equal(global(f).schedule.controls.dc.tdp,15);
   if(service==='ordinary-control'){
    assert.equal(global(f).controls.dc.auto.bias,75);assert.equal(global(f).schedule.controls.dc.auto.bias,75);
   }else if(service==='schedule-toggle')assert.equal(global(f).schedule.suspended,true);
   else if(service==='frame-only')assert.equal(global(f).frames.dc.fps,45);
   else assert.equal(global(f).schedule.hyperThreadPolicy,'off');
   const current=await schedule.getContinuousPerformanceSnapshot('global','dc');
   assert.deepEqual(structuredClone(current.controls.ac),initialControls.ac,`${mode}/${template}/${service}: first save changed effective AC bank`);
   assert.equal(current.controls.dc.tdp,initialControls.dc.tdp);
   assert.deepEqual(f.document().extensions.bootModes.scenes,doc.scenes);
   assert.equal(f.document().extensions.bootModes.revision,doc.revision+1);
   rows.push({mode,template,service,sparseFirstInitialization:true,effectiveDefaultsNotAdopted:true,savedSiblingsPreserved:true});
  }
 }
 for(const mode of ['global','xbox','steam','xbox-steam'])for(const template of ['portable','desktop'])for(const shape of ['absent','partial','legacy']){
  const f=await buildFixture(mode,{template});
  await f.repo.updateSettings(settings=>{settings.extensions.bootModes.current=mode==='global'?'xbox':'global';},{origin:'boot-mode'});
  await schedule.loadPerformanceSchedule();
  const legacy=structuredClone(base);delete legacy.controls;
  const raw=shape==='legacy'?legacy:{configured:true,enabled:true,...(shape==='partial'?{controls:{version:1,ac:{tdp:24,tdpCeiling:31,auto:{bias:35}}}}:{})};
  await f.repo.updateSettings(settings=>{settings.performanceSchedule=structuredClone(raw);settings.extensions.bootModes.current=mode;},{origin:'boot-mode'});
  const prior=f.document().extensions.bootModes;
  configure(await f.repo.loadSettings(),f.repo.getSettingsGeneration(),template);
  const cold=await freshSchedule().loadPerformanceSchedule(),warm=await schedule.loadPerformanceSchedule();
  assert.deepEqual(structuredClone(warm),structuredClone(cold),mode+'/'+template+'/'+shape+': prior cache changed current configuration meaning');
  if(shape==='absent'){assert.equal(warm.controls.ac.tdpValid,false);assert.equal(warm.controls.ac.cpuValid,false);assert.equal(warm.controls.ac.tdp,template==='desktop'?120:55);}
  if(shape==='partial'){assert.equal(warm.controls.ac.tdp,24);assert.equal(warm.controls.ac.auto.bias,35);assert.equal(warm.controls.dc.tdpValid,false);assert.equal(warm.controls.dc.cpuValid,false);}
  if(shape==='legacy')assert.equal(warm.controls,undefined,'legacy owner was hidden by prior continuous cache');
  assert.deepEqual(f.document().extensions.bootModes,prior,'read-only cache normalization changed saved intent');
  await f.repo.updateSettings(settings=>{settings.performanceSchedule=structuredClone(base);},{origin:'boot-mode'});await schedule.loadPerformanceSchedule();
  await f.repo.updateSettings(settings=>{settings.performanceSchedule=structuredClone(raw);},{origin:'boot-mode'});
  const previousGeneration=f.repo.getSettingsGeneration();f.repo.clearSettingsCache();assert(f.repo.getSettingsGeneration()>previousGeneration);
  configure(await f.repo.loadSettings(),f.repo.getSettingsGeneration(),template);
  const freshEpoch=await freshSchedule().loadPerformanceSchedule(),warmEpoch=await schedule.loadPerformanceSchedule();
  assert.deepEqual(structuredClone(warmEpoch),structuredClone(freshEpoch),mode+'/'+template+'/'+shape+': stale settings generation cache leaked');
  assert.deepEqual(f.document().extensions.bootModes,prior);
  rows.push({mode,template,shape,coldWarmEquivalent:true,priorSceneNotAReadFallback:true,realSettingsGenerationInvalidation:true,missingFieldsStayInvalid:true});
 }
 for(const mode of ['global','xbox','steam','xbox-steam'])for(const change of ['settings-generation','performance-source','applied-mode']){
  const f=await buildFixture(mode),originalDetect=yeman.detectPerformanceTemplate;
  await schedule.loadPerformanceSchedule();
  await f.repo.updateSettings(settings=>{settings.performanceSchedule={configured:true,enabled:true};},{origin:'boot-mode'});
  let entered,release;const hold=new Promise(resolve=>{release=resolve;}),started=new Promise(resolve=>{entered=resolve;});
  yeman.detectPerformanceTemplate=async()=>{entered();await hold;return 'portable';};
  let result;
  try{
   const pending=schedule.loadPerformanceSchedule(),settled=pending.then(value=>({value}),error=>({error}));
   let timer;try{await Promise.race([started,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Actual template detection was not reached')),1500);})]);}finally{clearTimeout(timer);}
   if(change==='settings-generation'){f.repo.clearSettingsCache();configure(await f.repo.loadSettings(),f.repo.getSettingsGeneration(),'portable');}
   if(change==='performance-source')await f.repo.updateSettings(settings=>{settings.performanceSchedule=structuredClone(base);settings.performanceSchedule.controls.ac.tdp=22;},{origin:'boot-mode'});
   if(change==='applied-mode')await f.repo.updateSettings(settings=>{settings.extensions.bootModes.current=mode==='global'?'xbox':'global';},{origin:'boot-mode'});
   yeman.detectPerformanceTemplate=originalDetect;
   const latest=await schedule.loadPerformanceSchedule(),durable=f.document();
   release();result=await settled;
   assert(result.error,mode+'/'+change+': stale template lookup returned obsolete performance');
   assert.match(String(result.error),/变化|失效|切换|取消/);
   let cached;const stop=schedule.onPerformanceScheduleChanged(value=>{cached=structuredClone(value);});stop();
   assert.deepEqual(cached,structuredClone(latest),'obsolete template lookup replaced newer cache');
   assert.deepEqual(f.document(),durable,'read-only stale lookup modified durable settings');
   rows.push({mode,change,templateAwaitFenced:true,newerCacheRetained:true,durableUnchanged:true});
  }finally{release();yeman.detectPerformanceTemplate=originalDetect;}
 }
 }
 // Execute the entire actual boot module consumer against the real performance
 // service/repository. Only its unrelated branches and hardware leaves stay inert.
 const req=createRequire(path.resolve('package.json')),ts=req('typescript');
 const bootSourceArgument=process.argv.indexOf('--boot-performance-source');
 const bootSourceFile=bootSourceArgument<0?path.resolve('src/bridge/bootModes.ts'):process.argv[bootSourceArgument+1];
 assert(typeof bootSourceFile==='string'&&path.isAbsolute(bootSourceFile),'Explicit absolute boot performance source');
 const bootSource=fs.readFileSync(bootSourceFile,'utf8'),tree=ts.createSourceFile('bootModes.ts',bootSource,ts.ScriptTarget.Latest,true);
 const functionNames=['requireBootActionOwner','ownedBootAction','bootPreferenceWrite','recordModule','applyRuntimeModule','observeBootModeRuntimeReceipts','readOnce','optional','syncStartupReceipt','rememberStartupResult','publishReceipt','inputModuleConfirmed','bootPerformanceIntent','bootPerformanceIntentMatches'];
 const variableNames=['LABELS','MODULE_KEY','PERSONAS','READ_TIMEOUT_MS','pendingReads','readGeneration','queue','serialized','startupOwners','startupReceipt','inputModules','object','same','text','INPUT_PROOF_WAITING','GYRO_PROOF_WAITING','inputProofPending','inputProofLabel'];
 const declarations=tree.statements.filter(node=>ts.isFunctionDeclaration(node)&&functionNames.includes(node.name?.text)||ts.isVariableStatement(node)&&node.declarationList.declarations.some(declaration=>variableNames.includes(declaration.name.getText(tree))));
 assert.equal(declarations.filter(ts.isFunctionDeclaration).length,functionNames.length);
 const bootCode=ts.transpileModule(declarations.map(node=>node.getText(tree)).join('\n')+`\nglobalThis.bootPerformance=applyRuntimeModule;
 globalThis.startBootObserver=observeBootModeRuntimeReceipts;
 globalThis.drainBootQueue=()=>serialized(async()=>{});
 globalThis.rememberBoot=rememberStartupResult;
 globalThis.bindBoot=(receipt,owner)=>{startupOwners.set(receipt,owner);startupReceipt=receipt;};
 globalThis.publishBoot=(receipt,owner)=>{startupOwners.set(receipt,owner);startupReceipt=receipt;return publishReceipt(receipt);};`, {compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.CommonJS}}).outputText;
 const {runtimeBundle}=await import('./harness.mjs');const modelOut=path.join(out,'boot-model');fs.mkdirSync(modelOut);const bootRuntime=await runtimeBundle(modelOut),bootModel=bootRuntime.model;
 if(residentOnly){const {runPerformanceResidentOwners}=await import('./performanceResident.mjs');return await runPerformanceResidentOwners({out,buildFixture,savedPerformance,repository,source,target,yeman,freshSchedule,configureTdp,hardwareWrites,evaluate,frameService,bootCode,bootRuntime,bootModel,models,frameModel,configureFloat});}
 if(eventsOnly){const {runPerformanceEventOwners}=await import('./performanceEvents.mjs');return await runPerformanceEventOwners({out,buildFixture,base,savedPerformance,repository,source,target,yeman,freshSchedule,configureTdp,hardwareWrites,configureFloat,bootCode,bootRuntime,bootModel,models,frameModel});}
 const applicationObservations=path.join(out,'application-observations.jsonl');fs.closeSync(fs.openSync(applicationObservations,'wx'));
 if(!lifecycleOnly&&!partialOnly){
 for(const mode of ['global','xbox','steam','xbox-steam'])for(const kind of controlsOnly?(controlsCase?[controlsCase.split('/')[1]]:controlKinds):['saved-unapplied','global-auto','global-manual','dedicated']){
  if(controlsCase&&mode+'/'+kind!==controlsCase)continue;
  if(!controlsReached){if(mode+'/'+kind===controlsFrom)controlsReached=true;else continue;}
  const policyGame=kind==='dedicated'?{pid:1701,processCreated:'1701',path:'C:\\fixture\\receipt.exe',name:'receipt.exe'}:null;
  const f=await buildFixture(mode,{policyGame}),service=freshSchedule();
  const requested=structuredClone(savedPerformance);requested.schedule.suspended=kind==='saved-unapplied';
  requested.controls.ac.mode=kind==='global-manual'?'manual':'auto';requested.controls.ac.tdp=24;requested.controls.ac.tdpCeiling=30;
  let expectedControls;
  if(controlsOnly){
   requested.schedule.controls=structuredClone(savedPerformance.controls);
   requested.schedule.controls.ac.tdp=120;requested.schedule.controls.ac.tdpCeiling=120;
   requested.schedule.controls.dc.tdp=80;requested.schedule.controls.dc.tdpCeiling=100;
   if(kind.startsWith('legacy-')){
    delete requested.schedule.controls;
    for(const side of ['ac','dc'])for(const profile of Object.values(requested.schedule.profiles[side]))profile.tdpMax=side==='ac'?120:80;
   }
   if(kind.endsWith('-invalid'))for(const side of ['ac','dc']){
    if(requested.schedule.controls)requested.schedule.controls[side].tdpValid=false;
    else for(const profile of Object.values(requested.schedule.profiles[side]))profile.tdpValid=false;
   }
   if(kind!=='top-controls')delete requested.controls;
   expectedControls=kind==='top-controls'?structuredClone(requested.controls):service.continuousControlsFromSchedule(service.snapshotPerformanceSchedule(requested.schedule));
   if(kind.endsWith('-invalid'))assert.equal(expectedControls.ac.tdpValid,false,'Invalid saved control source was promoted to valid');
   else assert.equal(expectedControls.ac.tdp,kind==='top-controls'?24:120,'Control-source setup must retain a saved120W value');
  }
  if(kind==='dedicated'){
   const controls=structuredClone(models.normalizeContinuousControls(undefined));controls.ac.mode='manual';controls.ac.tdp=18;controls.ac.tdpCeiling=30;
   const profile=structuredClone(base.profiles.ac.eco);
   await service.saveGameCustomConfig({version:1,entries:{'receipt.exe':{enabled:true,displayName:'receipt',controls,ac:profile,dc:profile,padPersona:'elite',gyroOverride:'fps',future:{keep:true}}},rtss:{}});
  }
  const gameBefore=policyGame?structuredClone(await service.loadGameCustomConfig()):null;
  let observed;
  const context={exports:{},structuredClone,cloneBootValue:bootModel.cloneBootValue,normalizeBootConfig:bootModel.normalizeBootConfig,normalizeContinuousControls:models.normalizeContinuousControls,normalizeControlPowerSchemes:models.normalizeControlPowerSchemes,frameRatePair:frameModel.frameRatePair,
   snapshotPerformanceSchedule:service.snapshotPerformanceSchedule,continuousControlsFromSchedule:service.continuousControlsFromSchedule,
   applyPerformanceConfiguration:async(...args)=>{observed=await service.applyPerformanceConfiguration(...args);return observed;}};
  vm.runInNewContext(bootCode,context);
  const receipt={mode,applied:[],skipped:[],pending:[],modules:{}},hardwareCount=hardwareWrites().length;
  await context.bootPerformance('performance',requested,savedPerformance,receipt);
  const state=receipt.modules.performance.state;
  fs.appendFileSync(applicationObservations,JSON.stringify({mode,kind,observed:structuredClone(observed),receipt:structuredClone(receipt),...(controlsOnly?{controlSource:{requested,expectedControls,persisted:structuredClone(f.document())}}:{})})+'\n');
  if(controlsOnly&&kind.endsWith('-invalid')){
   assert.equal(state,'waiting','Invalid saved TDP source must not be active');
   assert.equal(observed.applied,false);assert.equal(observed.application.tdp.ok,false);
   assert.equal(hardwareWrites().slice(hardwareCount).filter(row=>row.kind==='tdp').length,0,'Invalid saved TDP source authorized a write');
  }else if(kind==='saved-unapplied'){
   assert.equal(state,'waiting',mode+': saved configuration without actual performance application must not be active');
   assert(receipt.pending.some(value=>value.includes('性能调度')));assert(!receipt.applied.includes('性能调度'));
  }else if(kind==='dedicated'){
   assert.equal(state,'saved',mode+': applying dedicated performance must not confirm the different saved global bank');
   assert.match(receipt.modules.performance.detail,/专属/);assert(!receipt.applied.includes('性能调度'));
   assert.deepEqual(structuredClone(await service.loadGameCustomConfig()),gameBefore);
  }else {assert.equal(state,'active');assert(receipt.applied.includes('性能调度'));}
  assert.equal(f.document().performanceSchedule.controls.ac.tdp,controlsOnly?expectedControls.ac.tdp:24,'Boot configuration discarded saved schedule watts');
  if(controlsOnly){
   assert.deepEqual(structuredClone(f.document().performanceSchedule.controls),structuredClone(expectedControls));
   assert.equal(context.bootPerformanceIntent(requested).schedule.controls.ac.tdp,expectedControls.ac.tdp,'Receipt intent used a different control source');
   assert(context.bootPerformanceIntentMatches(f.document(),requested),'Current receipt intent cannot confirm the actual saved controls');
  }
  assert.equal(f.document().performanceSchedule.suspended,kind==='saved-unapplied');
  assert.equal(f.document().extensions.bootModes.global.performance.controls.ac.tdp,15,'boot application adopted the scene as ordinary global intent');
  assert.deepEqual(f.document().rtss.frameRates,requested.frames);
  rows.push({mode,applicationKind:kind,actualBootModuleAndPerformanceService:true,observed:structuredClone(observed),moduleState:state,dedicatedPreserved:true,...(controlsOnly?{requested,expectedControls,persisted:structuredClone(f.document()),receiptIntentMatches:true,tdpWrites:hardwareWrites().slice(hardwareCount).filter(row=>row.kind==='tdp').length}:{})});
 }
 }
 if(!applicationOnly&&!lifecycleOnly){
  const kinds=['tdp-none','tdp-false','tdp-throw','tdp-clamped','cpu-invalid','windows-managed','dedicated-tdp-none'];
  const argument=process.argv.find(value=>value.startsWith('--boot-performance-partial-case=')),selected=argument?.split('=')[1];
  assert(!argument||partialOnly&&kinds.includes(selected),'Unknown or unscoped original partial performance case');
  const observations=path.join(out,'partial-observations.jsonl');fs.closeSync(fs.openSync(observations,'wx'));
  for(const mode of ['global','xbox','steam','xbox-steam'])for(const kind of selected?[selected]:kinds){
   const dedicated=kind==='dedicated-tdp-none',policyGame=dedicated?{pid:1801,processCreated:'1801',path:'C:\\fixture\\partial.exe',name:'partial.exe'}:null;
   const f=await buildFixture(mode,{policyGame}),service=freshSchedule(),requested=structuredClone(savedPerformance),boot='partial-performance-boot';
   requested.controls.ac.tdp=24;requested.controls.ac.tdpCeiling=30;
   if(kind==='cpu-invalid')requested.controls.ac.cpuValid=false;
   if(kind==='windows-managed'){requested.controls.ac.mode='manual';requested.powerSchemes.ac='bal';}
   if(dedicated){const controls=structuredClone(requested.controls),profile=structuredClone(base.profiles.ac.eco);controls.ac.tdp=18;
    await service.saveGameCustomConfig({version:1,entries:{'partial.exe':{enabled:true,controls,ac:profile,dc:profile,future:{keep:true}}},rtss:{}});}
   await f.repo.updateSettings(settings=>{settings.input.startupObservedBoot=boot;settings.extensions.bootModesAppliedBoot=boot;settings.extensions.bootModesActiveBoot=boot;},{origin:'boot-mode'});
   configureTdp(kind==='dedicated-tdp-none'?'none':kind.startsWith('tdp-')&&kind!=='tdp-clamped'?kind.slice(4):'ack',kind==='tdp-clamped'?20:150);
   let observed,bootCalls=0;const events=[],stopProbe=service.onContinuousPerformanceApplied((signature,proof)=>events.push({signature,proof}));
   const context={exports:{},structuredClone,setTimeout,clearTimeout,console,Map,WeakMap,Promise,...bootRuntime.inputReceipts,
    normalizeBootStartupResult:bootModel.normalizeBootStartupResult,cloneBootValue:bootModel.cloneBootValue,normalizeBootConfig:bootModel.normalizeBootConfig,
    normalizeContinuousControls:models.normalizeContinuousControls,normalizeControlPowerSchemes:models.normalizeControlPowerSchemes,performanceConsumptionSignature:models.performanceConsumptionSignature,frameRatePair:frameModel.frameRatePair,
    snapshotPerformanceSchedule:service.snapshotPerformanceSchedule,continuousControlsFromSchedule:service.continuousControlsFromSchedule,
    getSettingsGeneration:f.repo.getSettingsGeneration,assertSettingsGeneration:f.repo.assertSettingsGeneration,loadSettings:repository.loadSettings,updateSettings:repository.updateSettings,updateSettingsForInputReceipt:f.repo.updateSettingsForInputReceipt,
    on:bootRuntime.ports.on,invoke:bootRuntime.ports.invoke,onMonitorOutputApplied:bootRuntime.ports.onMonitorOutputApplied,
    getPolicyGame:target.getPolicyGame,isGameStatusCurrent:()=>true,onContinuousPerformanceApplied:service.onContinuousPerformanceApplied,
    applyPerformanceConfiguration:async(...args)=>{bootCalls++;observed=await service.applyPerformanceConfiguration(...args);return observed;},
   };vm.runInNewContext(bootCode,context);
   let stop;
   try{
    const writesBefore=hardwareWrites().length,receipt={mode,applied:[],skipped:[],pending:[],modules:{}};
    await context.bootPerformance('performance',requested,savedPerformance,receipt);
    const complete=['tdp-clamped','windows-managed'].includes(kind),initial=structuredClone(receipt),cpuWrites=hardwareWrites().slice(writesBefore).filter(row=>row.kind==='cpu').length;
    fs.appendFileSync(observations,JSON.stringify({mode,kind,observed,receipt:initial,events,cpuWrites})+'\n');
    assert.equal(receipt.modules.performance.state,dedicated?'saved':complete?'active':'waiting',mode+'/'+kind+': partial performance application must not be reported active');
    assert.equal(observed.applied,complete,'application receipt must require each owned channel');
    assert.equal(observed.application.cpu,kind==='cpu-invalid'?'unconfirmed':kind==='windows-managed'?'windows-managed':'applied');
    assert.equal(observed.application.tdp.ok,!kind.includes('tdp-none')&&!['tdp-false','tdp-throw'].includes(kind));
    assert.equal(cpuWrites,kind==='cpu-invalid'||kind==='windows-managed'?0:1,'optional TDP failure must not suppress valid CPU application');
    assert.equal(events.length,complete?1:0,'partial application emitted an all-bank completion');
    if(kind==='tdp-clamped'){assert.equal(observed.application.tdp.accepted,20);assert.equal(f.document().performanceSchedule.controls.ac.tdp,24);}
    await context.publishBoot(receipt,{boot,generation:f.repo.getSettingsGeneration(),mode,sourceRevision:2,revision:3,committed:true});stop=context.startBootObserver();
    if(!complete){
     assert.equal(await service.restorePerformanceScheduleIfConfigured(),'auto','legacy caller ownership string changed');await context.drainBootQueue();
     assert.equal(f.document().extensions.bootModesStartupResult.modules.performance.state,dedicated?'saved':'waiting','late partial service completion promoted the boot receipt');
     assert.equal(events.length,0);
     configureTdp();
     if(dedicated)await f.setPolicyGame(null);
     if(kind==='cpu-invalid'){const repaired=await service.loadPerformanceSchedule();delete repaired.controls.ac.cpuValid;await service.savePerformanceSchedule(repaired,{origin:'runtime'});}
     await service.restorePerformanceScheduleIfConfigured();await context.drainBootQueue();
     const expected=kind==='cpu-invalid'?'waiting':'active';
     assert.equal(f.document().extensions.bootModesStartupResult.modules.performance.state,expected,'genuine recovery must acknowledge only the immutable original request');
     assert.equal(bootCalls,1,'late observer replayed the boot application');
    }
    rows.push({mode,partialKind:kind,initialState:initial.modules.performance.state,finalState:f.document().extensions.bootModesStartupResult.modules.performance.state,application:structuredClone(observed.application),cpuWrites,realLegacyRestore:true,bootCalls});
   }finally{stop?.();stopProbe();configureTdp();await context.drainBootQueue();}
  }
 }
 if(!applicationOnly&&!partialOnly){
  const kinds=['game-exit','next-boot-draft','newer-intent','newer-startup','foreign-boot','game-change','stop','disposed','generation','inflight-startup'];
  const selectedArgument=process.argv.find(value=>value.startsWith('--boot-performance-lifecycle-case=')),selected=selectedArgument?.split('=')[1];
  // Explicit boundary selections keep the original default40/default173 cost.
  const schemaKinds=['future-schema-service','future-schema-observer','future-schema-publication','future-receipt-service','future-receipt-publication'];
  const claimKinds=['claim-current','claim-observer','claim-inflight','claim-before-write','claim-legacy-observer'];
  const publicationKinds=['publication-current','publication-future-mode','publication-future-receipt','publication-newer-result','publication-mode-change','publication-lost-result','publication-observed-boot','publication-failed-newer-revision','publication-inflight-generation','publication-read-failure','publication-queued-newer-revision'];
  assert(!selectedArgument||[...kinds,...schemaKinds,...publicationKinds,...claimKinds].includes(selected),'Unknown original performance lifecycle case');
  const lifecycleObservations=path.join(out,'lifecycle-observations.jsonl');fs.closeSync(fs.openSync(lifecycleObservations,'wx'));
  for(const mode of ['global','xbox','steam','xbox-steam'])for(const kind of selected?[selected]:kinds){
   const policyGame={pid:1701,processCreated:'1701',path:'C:\\fixture\\receipt.exe',name:'receipt.exe'};
   const f=await buildFixture(mode,{policyGame}),service=freshSchedule();
   const requested=structuredClone(savedPerformance);requested.controls.ac.tdp=24;requested.controls.ac.tdpCeiling=30;
   const controls=structuredClone(models.normalizeContinuousControls(undefined));controls.ac.mode='manual';controls.ac.tdp=18;controls.ac.tdpCeiling=30;
   const profile=structuredClone(base.profiles.ac.eco);
   await service.saveGameCustomConfig({version:1,entries:{'receipt.exe':{enabled:true,displayName:'receipt',controls,ac:profile,dc:profile,future:{keep:true}}},rtss:{}});
   const boot='performance-proof-boot';
   const claimKind=claimKinds.includes(kind),writeId=claimKind&&kind!=='claim-legacy-observer'?'claim-original-'+mode:undefined;
   await f.repo.updateSettings(settings=>{settings.input.startupObservedBoot=boot;settings.extensions.bootModesAppliedBoot=boot;settings.extensions.bootModesActiveBoot=boot;if(writeId)settings.extensions.bootModesClaimWriteId=writeId;},{origin:'boot-mode'});
   let holdRead=false,readEntered,releaseRead;const entered=new Promise(resolve=>{readEntered=resolve;}),held=new Promise(resolve=>{releaseRead=resolve;});
   let applyCalls=0,subscriptions=0,replacementWritesBefore;
   const context={exports:{},structuredClone,setTimeout,clearTimeout,console,Map,WeakMap,Promise,
    ...bootRuntime.inputReceipts,normalizeBootStartupResult:bootModel.normalizeBootStartupResult,cloneBootValue:bootModel.cloneBootValue,normalizeBootConfig:bootModel.normalizeBootConfig,
    normalizeContinuousControls:models.normalizeContinuousControls,normalizeControlPowerSchemes:models.normalizeControlPowerSchemes,performanceConsumptionSignature:models.performanceConsumptionSignature,frameRatePair:frameModel.frameRatePair,snapshotPerformanceSchedule:service.snapshotPerformanceSchedule,continuousControlsFromSchedule:service.continuousControlsFromSchedule,
    getSettingsGeneration:f.repo.getSettingsGeneration,assertSettingsGeneration:f.repo.assertSettingsGeneration,
    loadSettings:async()=>{if(holdRead){holdRead=false;readEntered();await held;}return repository.loadSettings();},
    updateSettings:async(mutator,options)=>{
     if(kind==='claim-before-write'&&options?.origin==='runtime'&&replacementWritesBefore===undefined){
      await f.repo.updateSettings(settings=>{settings.extensions.bootModesClaimWriteId='claim-replaced-'+mode;},{origin:'boot-mode'});
      replacementWritesBefore=f.state.writes.length;
     }
     return repository.updateSettings(mutator,options);
    },updateSettingsForInputReceipt:f.repo.updateSettingsForInputReceipt,
    on:bootRuntime.ports.on,invoke:bootRuntime.ports.invoke,onMonitorOutputApplied:bootRuntime.ports.onMonitorOutputApplied,
    getPolicyGame:target.getPolicyGame,isGameStatusCurrent:()=>true,isCurrentGameTarget:target.isPolicyTargetCurrent,
    applyPerformanceConfiguration:async(...args)=>{applyCalls++;return service.applyPerformanceConfiguration(...args);},
    onContinuousPerformanceApplied:(listener,invalidated)=>{subscriptions++;const stop=service.onContinuousPerformanceApplied(listener,invalidated);let ended=false;return()=>{if(!ended){ended=true;subscriptions--;}stop();};},
   };
   vm.runInNewContext(bootCode,context);
   if(publicationKinds.includes(kind)){
    context.window=new EventTarget();context.CustomEvent=CustomEvent;const events=[];context.window.addEventListener('boot-mode:changed',event=>events.push(structuredClone(event.detail)));
    const owner={boot,generation:f.repo.getSettingsGeneration(),mode,sourceRevision:2,revision:3,committed:true};
    const initial={mode,applied:[],skipped:[],pending:[],modules:{performance:{state:'unchanged'},softwareLaunch:{state:'disabled'}}};
    await context.publishBoot(initial,owner);assert(owner.savedResult);assert.equal(events.length,1);events.length=0;
    await f.repo.updateSettings(settings=>{
     if(kind==='publication-future-mode')settings.extensions.bootModes.schema=99;
     if(kind==='publication-future-receipt'){settings.extensions.bootModesStartupResult.schema=99;settings.extensions.bootModesStartupResult.futureReceipt={keep:true};}
     if(kind==='publication-newer-result'){settings.extensions.bootModes.revision++;settings.extensions.bootModesStartupResult.sourceRevision++;settings.extensions.bootModesStartupResult.revision++;}
     if(kind==='publication-mode-change')settings.extensions.bootModes.current=mode==='global'?'xbox':'global';
     if(kind==='publication-lost-result')delete settings.extensions.bootModesStartupResult;
     if(kind==='publication-observed-boot')settings.input.startupObservedBoot='another-startup-proof-boot';
     if(kind==='publication-failed-newer-revision'){settings.extensions.bootModes.revision++;settings.extensions.bootModesStartupResult.revision++;}
    },{origin:'runtime'});
    const before=f.document(),writes=f.state.writes.length;let accepted,boundaryDocument;
    if(kind==='publication-inflight-generation')context.loadSettings=async()=>{const snapshot=await repository.loadSettings();f.repo.clearSettingsCache();return snapshot;};
    if(kind==='publication-read-failure')context.loadSettings=async()=>{throw Error('fixture current startup publication read unavailable');};
    if(kind==='publication-queued-newer-revision'){
     context.updateSettings=async(mutator,options)=>{
      await f.repo.updateSettings(settings=>{settings.extensions.bootModes.revision++;settings.extensions.bootModesStartupResult.revision++;},{origin:'runtime'});boundaryDocument=f.document();
      return repository.updateSettings(mutator,options);
     };
     const pending={schema:1,boot,mode,sourceRevision:2,revision:3,status:'partial',applied:[],skipped:[],pending:['old queued publication']};
     accepted=await context.rememberBoot(pending,{...owner,savedResult:undefined});
    }else if(kind==='publication-failed-newer-revision'){
     const failed={schema:1,boot,mode,sourceRevision:2,revision:2,status:'failed',applied:[],skipped:[],pending:['old failed attempt'],error:'old failed attempt'};
     accepted=await context.rememberBoot(failed,{...owner,revision:2,committed:false,savedResult:undefined});
    }else await context.publishBoot(initial,owner);
    await context.drainBootQueue();const after=f.document();fs.appendFileSync(lifecycleObservations,JSON.stringify({mode,kind,before:boundaryDocument??before,after,events,writesBefore:writes,writesAfter:f.state.writes.length,accepted})+'\n');
    assert.deepEqual(after,boundaryDocument??before,'obsolete startup publisher must not overwrite newer durable evidence');
    if(['publication-failed-newer-revision','publication-queued-newer-revision'].includes(kind))assert.equal(accepted,false,'older failed or queued startup revision must be rejected');
    else assert.equal(events.length,['publication-current','publication-read-failure'].includes(kind)?1:0,'obsolete startup no-op must not publish a current completion event');
    if(kind==='publication-read-failure')assert(events[0].pending.some(row=>row.includes('开机处理结果：保存未确认')),'current-owner read failure must remain visible');
    if(kind!=='publication-queued-newer-revision')assert.equal(f.state.writes.length,writes,'unchanged or rejected publication must not perform a settings write');
    rows.push({mode,lifecycleKind:kind,actualOriginalPublisher:true,realSettingsRepository:true,completionEvents:events.length,durableWrites:f.state.writes.length-writes,hardwareApplications:0});continue;
   }
   const receipt={mode,applied:[],skipped:[],pending:[],modules:{}};
   if(kind==='future-receipt-service'||kind==='claim-current')context.bindBoot(receipt,{boot,generation:f.repo.getSettingsGeneration(),mode,sourceRevision:2,revision:3,committed:true,writeId});
   await context.bootPerformance('performance',requested,savedPerformance,receipt);assert.equal(receipt.modules.performance.state,'saved');
   await context.publishBoot(receipt,{boot,generation:f.repo.getSettingsGeneration(),mode,sourceRevision:2,revision:3,committed:true,writeId});
   const startupBefore=f.document(),stalePublication=structuredClone(receipt);
   const stop=context.startBootObserver();let completedEvent;const stopProbe=service.onContinuousPerformanceApplied((signature,proof)=>{completedEvent={signature,proof};});
   const oldScheme=yeman.getActiveScheme,oldSwitch=yeman.switchScheme,oldBalanced=yeman.PW.WIN_BAL;
   try{
    if(kind==='future-schema-publication'||kind==='future-receipt-publication'){
     await f.repo.updateSettings(settings=>{if(kind==='future-schema-publication')settings.extensions.bootModes.schema=99;else settings.extensions.bootModesStartupResult.schema=99;settings.extensions.bootModesStartupResult.futureReceipt={keep:true};},{origin:'boot-mode'});
     const before=f.document();stalePublication.pending.push('性能调度：旧发布仍未完成');
     await context.publishBoot(stalePublication,{boot,generation:f.repo.getSettingsGeneration(),mode,sourceRevision:2,revision:3,committed:true});await context.drainBootQueue();
     const after=f.document();fs.appendFileSync(lifecycleObservations,JSON.stringify({mode,kind,before,after,applyCalls})+'\n');
     assert.deepEqual(after,before,'old startup publication must not rewrite a future mode/receipt schema');
     rows.push({mode,lifecycleKind:kind,futureDocumentPreserved:true,bootApplications:applyCalls});continue;
    }
    // Config-change callbacks alone must never acknowledge an applied bank.
    await service.savePerformanceSchedule(await service.loadPerformanceSchedule(),{origin:'runtime'});await context.drainBootQueue();
    assert.equal(f.document().extensions.bootModesStartupResult.modules.performance.state,'saved');
    await f.setPolicyGame(null);
    const observerBoundary=!['game-exit','inflight-startup','future-schema-service','future-receipt-service','claim-current','claim-inflight','claim-before-write'].includes(kind);
    holdRead=observerBoundary;
    if(kind==='future-schema-service')await f.repo.updateSettings(settings=>{settings.extensions.bootModes.schema=99;},{origin:'boot-mode'});
    if(kind==='future-receipt-service')await f.repo.updateSettings(settings=>{settings.extensions.bootModesStartupResult.schema=99;},{origin:'boot-mode'});
    if(kind==='inflight-startup'||kind==='claim-inflight'){
     const originalTdp=yeman.setTdp;let tdpEntered,tdpRelease;const tdpEntry=new Promise(resolve=>{tdpEntered=resolve;}),tdpHeld=new Promise(resolve=>{tdpRelease=resolve;});
     yeman.setTdp=async(...args)=>{tdpEntered();await tdpHeld;return originalTdp(...args);};
     const restore=service.restorePerformanceScheduleIfConfigured();
     try{let timer;await Promise.race([tdpEntry,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Original restore did not reach TDP owner')),1500);})]).finally(()=>clearTimeout(timer));
      await f.repo.updateSettings(settings=>{if(kind==='claim-inflight')settings.extensions.bootModesClaimWriteId='claim-replaced-'+mode;else{settings.extensions.bootModes.revision++;settings.extensions.bootModesStartupResult.revision++;settings.extensions.bootModesStartupResult.sourceRevision++;}},{origin:'boot-mode'});
     }finally{tdpRelease();await restore;yeman.setTdp=originalTdp;}
    }else await service.restorePerformanceScheduleIfConfigured();
    assert(completedEvent?.signature);
    if(kind==='future-schema-service'||kind==='future-receipt-service'){
     fs.appendFileSync(lifecycleObservations,JSON.stringify({mode,kind,phase:'service',event:completedEvent,result:f.document().extensions.bootModesStartupResult,applyCalls})+'\n');
     assert.equal(completedEvent.proof,undefined,kind==='future-schema-service'?'future mode schema must not produce a startup ownership proof':'future receipt schema must not produce a startup ownership proof');
    }
    if(observerBoundary){
     let timer;await Promise.race([entered,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Original performance owner completion did not reach boot observer')),1500);})]).finally(()=>clearTimeout(timer));
     if(kind==='next-boot-draft')await f.repo.updateSettings(settings=>{settings.extensions.bootModes.revision++;settings.extensions.bootModes.boot=mode==='global'?'xbox':'global';},{origin:'boot-mode'});
     if(kind==='newer-intent'){const newer=await service.loadPerformanceSchedule();newer.controls.ac.tdp=25;await service.savePerformanceSchedule(newer);}
     if(kind==='newer-startup')await f.repo.updateSettings(settings=>{settings.extensions.bootModes.revision++;settings.extensions.bootModesStartupResult.revision++;settings.extensions.bootModesStartupResult.sourceRevision++;},{origin:'boot-mode'});
     if(kind==='claim-observer'||kind==='claim-legacy-observer')await f.repo.updateSettings(settings=>{settings.extensions.bootModesClaimWriteId='claim-replaced-'+mode;},{origin:'boot-mode'});
     if(kind==='foreign-boot')await f.repo.updateSettings(settings=>{settings.input.startupObservedBoot='another-proof-boot';},{origin:'boot-mode'});
     if(kind==='future-schema-observer')await f.repo.updateSettings(settings=>{settings.extensions.bootModes.schema=99;},{origin:'boot-mode'});
     if(kind==='game-change')await f.setPolicyGame({...policyGame,pid:1702,processCreated:'1702',path:'C:\\fixture\\new-game.exe'});
     if(kind==='stop'){let scheme='test-yeman';yeman.PW.WIN_BAL='test-bal';yeman.getActiveScheme=async()=>scheme;yeman.switchScheme=async value=>{assert.equal(value,'bal');scheme='test-bal';};await service.stopPerformanceScheduleAndUseWindowsBalanced();assert.equal(await service.restorePerformanceScheduleIfConfigured(),'none');}
     if(kind==='disposed')stop();
     if(kind==='generation')f.repo.clearSettingsCache();
     releaseRead();
    }
    await context.drainBootQueue();
    const actual=f.document().extensions.bootModesStartupResult;
    fs.appendFileSync(lifecycleObservations,JSON.stringify({mode,kind,event:completedEvent,result:actual,applyCalls,...(kind==='game-exit'?{startupBefore,completedDocument:f.document()}: {})})+'\n');
    const expected=['game-exit','next-boot-draft','claim-current'].includes(kind)?'active':'saved';
    assert.equal(actual.modules.performance.state,expected,mode+'/'+kind+': actual performance completion must respect its startup request owner');
    if(kind==='claim-inflight')assert.equal(completedEvent.proof,undefined,'Retired work must not publish a new claim proof');
    if(claimKind&&kind!=='claim-inflight')assert.equal(completedEvent.proof?.writeId,writeId??null,'Completion proof must keep the submission claim');
    if(kind==='claim-before-write'){assert(Number.isInteger(replacementWritesBefore));assert.equal(f.state.writes.length,replacementWritesBefore,'A retired receipt did not reach the settings store');}
    assert.equal(applyCalls,1,'performance observer replayed the boot application');
    if(expected==='active'){
     const count=f.state.writes.length;await service.restorePerformanceScheduleIfConfigured();await context.drainBootQueue();assert.equal(f.state.writes.length,count,'duplicate completion rewrote the startup receipt');
     assert(actual.applied.includes('性能调度'));
     await context.publishBoot(stalePublication,{boot,generation:f.repo.getSettingsGeneration(),mode,sourceRevision:2,revision:3,committed:true,writeId});
     assert.equal(f.document().extensions.bootModesStartupResult.modules.performance.state,'active','a stale renderer publication erased the real completion');
    }
    rows.push({mode,lifecycleKind:kind,actualServiceCompletion:true,expectedState:expected,bootApplications:applyCalls,duplicateWrites:0});
   }finally{releaseRead();stop();stop();stopProbe();yeman.getActiveScheme=oldScheme;yeman.switchScheme=oldSwitch;yeman.PW.WIN_BAL=oldBalanced;await context.drainBootQueue();assert.equal(subscriptions,0);}
  }
 }
 assert(controlsReached,'Original performance control-source start was not reached');
 const report={pass:true,...(controlsOnly?{controlsSelection:{only:controlsCase??null,from:controlsFrom??null,invalid:invalidControls}}:{}),selection:controlsOnly?'control-sources-only':partialOnly?'partial-only':lifecycleOnly?'lifecycle-only':applicationOnly?'application-receipts-only':'all',originalPreferenceGroup:applicationOnly||lifecycleOnly||partialOnly?'NOT_RUN_SELECTED_GROUP':'executed',scope:'Complete actual performance/frame service with real settingsRepository; inert hardware/target ports. No installed app or real power/device operation.',rows,bootConsumerSource:bootSourceFile,
  inputSources:['src/bridge/bootModes.ts','src/bridge/performanceSchedule.ts','src/bridge/frameRateLimits.ts','src/bridge/bootModeGlobalPreferences.ts','src/bridge/settingsRepository.ts']};
 fs.writeFileSync(path.join(out,'performance-preferences.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});return report;
}
