import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const require = createRequire(path.resolve('package.json'));
const { transformSync } = require('esbuild');
const serviceSourceIndex=process.argv.indexOf('--performance-service-source');
const serviceSource=serviceSourceIndex<0?'src/bridge/performanceSchedule.ts':process.argv[serviceSourceIndex+1];
assert(serviceSourceIndex<0||typeof serviceSource==='string'&&path.isAbsolute(serviceSource)&&process.argv.includes('--boot-preferences-only')&&(process.argv.includes('--boot-performance-partial-only')||process.argv.includes('--boot-performance-events-only')||process.argv.includes('--boot-performance-resident-only')||process.argv.includes('--boot-performance-lifecycle-only')&&process.argv.some(value=>value.startsWith('--boot-performance-lifecycle-case='))),'Explicit service candidate requires a selected partial/event/resident group or exact lifecycle case');
const clone = x => structuredClone(x);
function evaluate(file, imports = {}, globals = {}, suffix = '') {
  const module = {exports:{}};
  const exactInput=fs.readFileSync(file,'utf8')+'\n'+suffix,transformed=transformSync(exactInput,{loader:'ts',format:'cjs'}).code;
  if(process.argv.includes('--boot-performance-resident-only')){const directory=path.join(process.argv[2],'evaluated-inputs');fs.mkdirSync(directory,{recursive:true});const serial=String(fs.readdirSync(directory).length/3).padStart(4,'0'),stem=path.join(directory,serial);fs.writeFileSync(stem+'.input.ts',exactInput,{flag:'wx'});fs.writeFileSync(stem+'.output.cjs',transformed,{flag:'wx'});fs.writeFileSync(stem+'.mapping.json',JSON.stringify({file:path.resolve(file),suffix,loader:'ts',format:'cjs'}),{flag:'wx'});}
  vm.runInNewContext(transformed,
    {module,exports:module.exports,require:n=>{if(n in imports)return imports[n];throw Error('Unexpected import '+n)},console,structuredClone,performance,Date,setTimeout,clearTimeout,TextEncoder,TextDecoder,...globals});
  return module.exports;
}
function profileTargetHelpers(){
 return evaluate('src/bridge/gameCustomProfileTarget.ts',{'./gamePolicyHysteresis':evaluate('src/bridge/gamePolicyHysteresis.ts')});
}
const models = evaluate('src/bridge/performanceControls.ts');
const frameModel = evaluate('src/bridge/frameRateModel.ts');
let afterSettingsCommit=null;
let beforeTdpSnapshot=null,afterTdpDispatch=null;
let tdpWriteMode='ack';let residentOwner=null;const ownerReads={settings:0,files:0,source:0};let sectionJson='',sectionRevision=0;
let side='ac', game=null, generation=1, flipOnTdp=false, failSave=false, acceptedLimit=150, clockMs=Date.now(), deviceTemplate='portable';
class TestDate extends Date { static now(){return clockMs} }
let settings={input:{},rtss:{frameRates:frameModel.frameRatePair({}),future:'KEEP'},performanceSchedule:{configured:false,enabled:false},other:{keep:42}};
const source={value:'ac'}, files=new Map(), writes=[], listeners=new Set();
const sourcePath='C:\\SOFT\\YeMan\\PowerControl\\game-custom.json';
files.set(sourcePath,JSON.stringify({version:1,entries:{},rtss:{}}));
const frames={...frameModel,loadGlobalFrameRates:async()=>settings.rtss.templateDefaults===true?frameModel.frameRatePair(null,deviceTemplate==='desktop'?200:120,60):frameModel.frameRatePair(settings.rtss.frameRates),ensureGlobalFrameRates:async()=>settings.rtss.templateDefaults===true?frameModel.frameRatePair(null,deviceTemplate==='desktop'?200:120,60):frameModel.frameRatePair(settings.rtss.frameRates),
  applyIndependentFrameRates:async()=>{writes.push({kind:'fps'});return true},publishFrameRatesChanged(){}};
const target={getPolicyGame:()=>game,detectPolicyGame:async()=>game,gamePolicyKey:g=>(g.path||g.name).split(/[\\/]/).pop().toLowerCase(),isPolicyTargetCurrent:t=>!!game&&game.pid===t.pid&&game.processCreated===t.processCreated};
let gameStatusCurrent=true;
let wakePowerFact={phase:'ready',known:true,generation:4,hardwareWritesAllowed:true,suspendSerial:0,observedNotificationSequence:20,publicationRevision:20};
const wakePowerHandlers=new Map();
const wakePower=evaluate('src/bridge/powerLifecycleState.ts',{'./ipc':{
 invoke:async()=>clone(wakePowerFact),on(name,listener){wakePowerHandlers.set(name,listener);return()=>wakePowerHandlers.delete(name);},
}},{window:{},queueMicrotask,AbortController});
function publishWakePower(patch){
 wakePowerFact={...wakePowerFact,publicationRevision:wakePowerFact.publicationRevision+1,...patch};
 wakePowerHandlers.get('power.lifecycleChanged')?.(clone(wakePowerFact));
}
const gameCore={detectGameCorePolicy:async()=>{if(beforeTdpSnapshot)await beforeTdpSnapshot();return {detected:true,heterogeneous:true,smtAvailable:true}},applyGameCorePolicy:async(t,m,h,powerScope)=>{writes.push({kind:'smt',pid:t.pid,h,powerScope:clone(powerScope)});return {ok:true,applied:true}}};
const repository={loadSettings:async()=>{ownerReads.settings++;return clone(settings)},SETTINGS_FILE:'C:\\SOFT\\YeMan\\PowerControl\\yeman-settings.json',getSettingsGeneration:()=>generation,assertSettingsGeneration:g=>{if(g!==generation)throw Error('generation')},
  validateSettingsRead:async()=>{},
  getSettingsSectionRevision:key=>{const json=JSON.stringify(settings[key]||{});if(json!==sectionJson){sectionJson=json;++sectionRevision}return sectionRevision},
  readSettingsSection:async key=>{ownerReads.settings++;return clone(settings[key]||{})},mergeSettings:(a,b)=>({...a,...b}),
  saveSettingsSection:async(key,value)=>{if(failSave)throw Error('disk full');settings[key]=clone(value)},replaceSettingsSection:async(key,value)=>{settings[key]=clone(value)},
  updateSettings:async mutate=>{const next=clone(settings);mutate(next);if(failSave)throw Error('disk full');settings=next;afterSettingsCommit?.()}};
const yeman={detectPerformanceTemplate:async()=>deviceTemplate,detectPowerMode:async()=>side,detectPowerModeReliable:async()=>{ownerReads.source++;return side},ensureRememberedYemanSchemeActive:async()=>{},ensureSelectedPowerScheme:async(_scheme,checkpoint)=>{checkpoint?.();},
  getTdpHardwareMinimum:()=>2,getTdpHardwareMaximum:()=>150,getTdpExecutionCapability:async()=>({selected:'fixture'}),
  detectCoreArchitecture:async()=>({heterogeneous:true}),PW:{YEMAN:'test-yeman'},getActiveScheme:async()=> 'test-yeman',setActiveScheme:async()=>{},
  setCoreModeAc:async mode=>writes.push({kind:'core',side:'ac',mode}),setCoreModeDc:async mode=>writes.push({kind:'core',side:'dc',mode}),
  setCoreModeForPowerScope:async(side,mode)=>writes.push({kind:'core',side,mode}),setActiveSchemeForPowerScope:async()=>{},
  setTdp:async(s,w,opts={})=>{if(tdpWriteMode==='throw')throw Error('TDP transport unavailable');writes.push({kind:'tdp',side:s,w});if(afterTdpDispatch)await afterTdpDispatch();if(tdpWriteMode!=='none')opts.onAccepted?.(Math.min(acceptedLimit,w));if(flipOnTdp){side='dc';source.value='dc';flipOnTdp=false}return tdpWriteMode!=='false'},
  applyManualPowerParams:async p=>{writes.push({kind:'cpu',...clone(p)});return true},applyCpuProfilePowerParams:async()=>{},rebuildYemanScheme:async()=>{},setRtssLimit:async()=>{},readRtssLimit:async()=>120};
let realFloatBackend=null;
function freshSchedule(){return evaluate(serviceSource,{
 './controllerPersona':evaluate('src/bridge/controllerPersona.ts'),
 './bootModeGlobalPreferences':evaluate('src/bridge/bootModeGlobalPreferences.ts',{'./inputConfigHash':evaluate('src/bridge/inputConfigHash.ts'),'./settingsBaseline':evaluate('src/bridge/settingsBaseline.ts',{'../../PowerControl/settings-baseline.json':JSON.parse(fs.readFileSync('PowerControl/settings-baseline.json','utf8'))})}),
  './performanceControls':models,'./powerSource':{powerSourceMode:source},'./powerLifecycleState':wakePower,'./gameCorePolicy':gameCore,
  './gamePolicyTarget':target,'./api':{powerLifecycle:{get:async()=>clone(wakePowerFact)},fs:{exists:async p=>files.has(p),readTextFile:async p=>{ownerReads.files++;return files.get(p)},writeTextFileAtomic:async(p,v)=>{files.set(p,v)},writeTextFile:async(p,v)=>{files.set(p,v)}}},
  './settingsSnapshot':evaluate('src/bridge/settingsSnapshot.ts',{'vue':require('vue')}),'./frameRateLimits':frames,'./settingsRepository':repository,'./gamedetect':{isGameStatusCurrent:()=>gameStatusCurrent,detectGame:async()=>game,validateGameTarget:async()=>true,isCurrentGameTarget:t=>!!game&&game.pid===t.pid&&game.processCreated===t.processCreated},'./yeman':yeman,'./cpuProfiles':{loadCpuProfiles:async()=>({profiles:{}})},
  './autofloat':realFloatBackend??{pauseContinuousFloatForTdpEdit:async()=>{const owner=residentOwner;if(!owner)return null;return {current:()=>residentOwner===owner,accept:(requested,accepted)=>{owner.lastReceipt={requested,accepted}},release(){}}},disableFloat:async()=>{writes.push({kind:'stop'})},enableContinuousFloat:async(bank,s,t,current,applyCore,receipt)=>{residentOwner={current,applyCore};writes.push({kind:'float',side:s,tdp:bank.tdp,target:t,receipt})},setFloatRtssLinked(){},getFloatInfo:()=>({enabled:false}),updateContinuousFloatCoreRules(){},holdContinuousFloatAfterCoreChange(){},cancelPendingRtssSync(){},notifyTdpMaxChanged:async()=>{},applyFloatSettings:async()=>{},enableFloat:async()=>{},setFloatTarget:async()=>{}},
},{Date:TestDate,AbortController});}
const schedule=freshSchedule();
function gamepadTdpApp(schedule, handleIpc) {
 const ts=require('typescript'),script=fs.readFileSync('src/App.vue','utf8').match(/<script setup[^>]*>([\s\S]*?)<\/script>/)[1];
 const tree=ts.createSourceFile('app.ts',script,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
 const functions=tree.statements.filter(n=>ts.isFunctionDeclaration(n)&&['applyGamepadTdp','onGamepadTdpDelta'].includes(n.name?.text)).map(n=>n.getText(tree)).join('\n');
 const serial=evaluate('src/gamepad/serial.ts'),module={exports:{}},feedback=[],events=[],refresh={value:0};
 const invoke=async(name,args,options)=>{feedback.push({name,args});return handleIpc ? handleIpc(name,args,options) : name==='power.lifecycle' ? clone(wakePowerFact) : {ok:true}};
 vm.runInNewContext(transformSync(functions+'\nmodule.exports={onGamepadTdpDelta,applyGamepadTdp}',{loader:'ts',format:'cjs'}).code,{
  module,exports:module.exports,...wakePower,AbortController,SkipApplyError:schedule.SkipApplyError,invoke,
  adjustContinuousPerformanceTdp:schedule.adjustContinuousPerformanceTdp,enqueueGamepadTaskDetached:serial.enqueueGamepadTaskDetached,globalRefreshKey:refresh,
  window:{dispatchEvent:e=>events.push(e)},CustomEvent:class{constructor(type,options){this.type=type;this.detail=options?.detail}},console:{error(...args){if(process.env.YMCC_TDP_TEST_TRACE)console.error(...args)}},
 });
 const scope=()=>[wakePowerFact.generation,wakePowerFact.suspendSerial,wakePowerFact.observedNotificationSequence];
 return {feedback,events,refresh,apply:(delta,id)=>module.exports.applyGamepadTdp(delta,id??'direct',scope()),
  press:(delta,feedbackId,powerScope=scope())=>module.exports.onGamepadTdpDelta({detail:{delta,feedbackId,powerScope}}),
  idle:()=>serial.enqueueGamepadTask(async()=>{}),pending:serial.gamepadSerialPending};
}
if(process.argv.includes('--gamepad-tdp-power-scope-only')) {
 const out=process.argv[2];assert(out&&!out.startsWith('--'),'Expected an evidence directory');
 const results=[];
 const defer=()=>{let release;const pending=new Promise(resolve=>{release=resolve});return {pending,release}};
 const within=async promise=>{let timer;try{return await Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('serial task did not settle')),600)})])}finally{clearTimeout(timer)}};
 for(const scenario of ['claim-reply','held-snapshot','held-validation-snapshot','native-ingress-before-save','save-settled','silent-native-after-save','held-write','completion-refused','metadata','cache-behind','unbound-event','steps-and-bounds','burst-banks']) {
  const schedule=freshSchedule();game=null;side='ac';source.value='ac';residentOwner=null;
  settings.performanceSchedule={controls:models.normalizeContinuousControls({ac:{mode:'manual',tdp:20}}),configured:true,enabled:true};
  writes.length=0;
  publishWakePower({phase:'ready',hardwareWritesAllowed:true,notificationPending:false,generation:wakePowerFact.generation+1,suspendSerial:wakePowerFact.suspendSerial+1,observedNotificationSequence:wakePowerFact.observedNotificationSequence+1});
  const off=wakePower.onSharedPowerLifecycle(()=>{},{observeSuspendSerial:true,observeNotificationSequence:true});
  await new Promise(setImmediate);
  const arrived=defer(),reply=defer(),advance=()=>publishWakePower({phase:'suspending',hardwareWritesAllowed:false,suspendSerial:wakePowerFact.suspendSerial+1,observedNotificationSequence:wakePowerFact.observedNotificationSequence+1});
  const ready=()=>publishWakePower({phase:'ready',hardwareWritesAllowed:true,generation:wakePowerFact.generation+1});
  const app=gamepadTdpApp(schedule,async(name,{id})=>{
   if(name==='input.shortcutAction.claim'&&id==='old'&&scenario==='claim-reply'){arrived.release();await reply.pending;}
   if(name==='input.shortcutAction.complete'&&scenario==='completion-refused')return {ok:false};
   return name==='power.lifecycle'?clone(wakePowerFact):{ok:true};
  });
  try {
   const before=JSON.stringify(settings), watts=()=>settings.performanceSchedule.controls.ac.tdp;
   const assertOldQuiet=()=>{assert.equal(app.events.length,0,'old UI success');assert.equal(app.feedback.filter(f=>f.name==='input.shortcutAction.complete').length,0,'old result publication');};
   if(['claim-reply','held-snapshot','held-validation-snapshot'].includes(scenario)){
    let reads=0;
    if(scenario!=='claim-reply')beforeTdpSnapshot=async()=>{if(++reads!==(scenario==='held-snapshot'?1:2))return;arrived.release();await reply.pending};
    app.press(2,'old');await within(arrived.pending);advance();ready();app.press(1,'new');await within(app.idle());
    assert.equal(watts(),21,'only the new press saves');assert.equal(writes.filter(w=>w.kind==='tdp').length,1);
    assert.equal(app.events.length,1);assert.equal(app.feedback.some(f=>f.args.id==='old'&&f.name==='input.shortcutAction.complete'),false);
    reply.release();await new Promise(setImmediate);assert.equal(watts(),21);
   } else if(scenario==='native-ingress-before-save'){
    beforeTdpSnapshot=async()=>{beforeTdpSnapshot=null;wakePowerFact={...wakePowerFact,notificationPending:true,observedNotificationSequence:wakePowerFact.observedNotificationSequence+1};};
    app.press(2,'old');await within(app.idle());assert.equal(JSON.stringify(settings),before);assert.equal(writes.length,0);assertOldQuiet();
   } else if (scenario==='save-settled'||scenario==='silent-native-after-save'){
    afterSettingsCommit=()=>{if(scenario==='save-settled')advance();else wakePowerFact={...wakePowerFact,notificationPending:true,observedNotificationSequence:wakePowerFact.observedNotificationSequence+1}};
    app.press(2,'old');await within(app.idle());assert.equal(watts(),22);assert.equal(writes.length,0,'completed save did not authorize a hardware write');assertOldQuiet();
   } else if(scenario==='held-write'){
    afterTdpDispatch=async()=>{arrived.release();await reply.pending};app.press(2,'old');await within(arrived.pending);advance();ready();
    let finished=false;const end=app.idle().then(()=>{finished=true});await new Promise(setImmediate);assert.equal(finished,false,'dispatched write still owns its receipt');
    reply.release();await within(end);assert.equal(watts(),22);assert.equal(writes.filter(w=>w.kind==='tdp').length,1);assert.equal(writes.filter(w=>w.kind==='cpu').length,0);assertOldQuiet();
   } else if(scenario==='cache-behind'){
    wakePowerFact={...wakePowerFact,generation:wakePowerFact.generation+1,suspendSerial:wakePowerFact.suspendSerial+1,observedNotificationSequence:wakePowerFact.observedNotificationSequence+1,publicationRevision:wakePowerFact.publicationRevision+1};
    app.press(1,'current');await within(app.idle());assert.equal(watts(),21);assert.equal(app.events.length,1);
   } else if(scenario==='completion-refused'){
    app.press(2,'old');await within(app.idle());assert.equal(watts(),22);assert.equal(writes.filter(w=>w.kind==='tdp').length,1);
    assert.equal(app.feedback.filter(f=>f.name==='input.shortcutAction.complete').length,1);assert.equal(app.events.length,0);assert.equal(app.refresh.value,0);
   } else if(scenario==='unbound-event'){
    app.press(2,'unbound',null);await within(app.idle());assert.equal(JSON.stringify(settings),before);assert.equal(writes.length,0);assert.equal(app.feedback.length,0);
   } else if(scenario==='burst-banks'){
    await verifyGamepadTdpRepeat(schedule);
   } else if(scenario==='steps-and-bounds'){
    await verifyGamepadTdpSteps(schedule);
   } else {
    beforeTdpSnapshot=async()=>{beforeTdpSnapshot=null;publishWakePower({})};app.press(1,'current');await within(app.idle());assert.equal(watts(),21);assert.equal(app.events.length,1);
   }
   results.push({scenario,pass:true});
  } catch(error) {results.push({scenario,pass:false,error:String(error.stack??error)});}
  finally {beforeTdpSnapshot=afterSettingsCommit=afterTdpDispatch=null;reply.release();await within(app.idle());off();}
 }
 const pass=results.every(r=>r.pass);fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'result.json'),JSON.stringify({pass,results},null,2));
 for(const r of results)console.log(`${r.pass?'PASS':'FAIL'} ${r.scenario}${r.pass?'':' '+r.error.split('\n')[0]}`);
 process.exit(pass?0:1);
}
const fpsCommitCase='FPS保存回执跨电源范围时保留设置，但旧操作不另派硬件应用';
async function verifyFpsCommitScope(schedule){
 game=null;side='ac';source.value='ac';
 const snapshot=await schedule.getContinuousPerformanceSnapshot('global');
 const before=writes.filter(w=>w.kind==='fps').length;
 let allowed=true;
 afterSettingsCommit=()=>{allowed=false};
 try{
  const saved=await schedule.updateContinuousPerformanceControls(snapshot.identity,{frameRate:{fps:45,ceiling:60,lastFps:45}},()=>allowed);
  assert.equal(saved.frameRates.ac.fps,45);
  assert.equal(settings.rtss.frameRates.ac.fps,45);
  assert.equal(writes.filter(w=>w.kind==='fps').length,before);
  assert.equal(saved.frameRateApplied,false);
 }finally{afterSettingsCommit=null}
 const fresh=await schedule.getContinuousPerformanceSnapshot('global');
 const applied=await schedule.updateContinuousPerformanceControls(fresh.identity,{frameRate:{fps:60,ceiling:60,lastFps:60}},()=>true);
 assert.equal(applied.frameRateApplied,true);
 assert.equal(writes.filter(w=>w.kind==='fps').length,before+1);
}
if(process.argv.includes('--fps-commit-scope-only')) {
 const out=process.argv[2];assert(out&&!out.startsWith('--')&&process.argv.length===4,'Expected one evidence directory and --fps-commit-scope-only');
 await verifyFpsCommitScope(freshSchedule());
 const result={pass:true,count:1,cases:[fpsCommitCase]};
 fs.writeFileSync(path.join(out,'state-tests.json'),JSON.stringify(result,null,2),{flag:'wx'});
 console.log(JSON.stringify(result));
 process.exit(0);
}
if(process.argv.includes('--wake-admission-only')) {
 const out=process.argv[2];assert(out&&!out.startsWith('--'),'Explicit wake admission evidence directory required');
 const off=wakePower.onSharedPowerLifecycle(()=>{});
 const flush=()=>new Promise(resolve=>setImmediate(resolve));
 await flush();assert(wakePower.isSharedPowerLifecycleAdmitted(wakePower.getSharedPowerLifecycle()));
 settings.input={};
 settings.performanceSchedule={configured:true,enabled:true,controls:{ac:models.defaultContinuousBank('ac'),dc:models.defaultContinuousBank('dc')}};
 const defer=()=>{let resolve;const promise=new Promise(done=>{resolve=done});return{promise,resolve};};
 let admitted=true;
 const powerEntered=defer(),powerRelease=defer(),readPower=yeman.detectPowerModeReliable;
 yeman.detectPowerModeReliable=async()=>{powerEntered.resolve();await powerRelease.promise;return 'ac';};
 const abandoned=schedule.restorePerformanceScheduleAfterWake('ac',false,()=>admitted);
 await powerEntered.promise;admitted=false;powerRelease.resolve();
 assert.equal(await abandoned,'retry-on-ready');assert.deepEqual(writes,[]);
 yeman.detectPowerModeReliable=readPower;
 admitted=true;assert.equal(await schedule.restorePerformanceScheduleAfterWake('ac',false,()=>admitted),'settled');
 assert.equal(writes.filter(row=>row.kind==='tdp').length,1);
 assert.equal(writes.filter(row=>row.kind==='cpu').length,1);

 writes.length=0;
 const tdpEntered=defer(),tdpRelease=defer(),setTdp=yeman.setTdp;
 yeman.setTdp=async(...args)=>{const result=await setTdp(...args);tdpEntered.resolve();await tdpRelease.promise;return result;};
 let partialResult;
 const partial=schedule.restorePerformanceScheduleAfterWake('ac',false,()=>admitted).then(result=>{partialResult=result;});
 try {
  await tdpEntered.promise;
  publishWakePower({observedNotificationSequence:wakePowerFact.observedNotificationSequence+1});
  await flush();
  assert.equal(partialResult,undefined,'revocation cannot release an issued TDP write before its receipt');
  assert.equal(schedule.isPerformanceScheduleBusy(),true);
  assert.equal(writes.filter(row=>row.kind==='tdp').length,1);
  assert.equal(writes.filter(row=>row.kind==='cpu').length,0);
 } finally { tdpRelease.resolve();await partial;yeman.setTdp=setTdp; }
 assert.equal(partialResult,'unconfirmed');
 assert.equal(writes.filter(row=>row.kind==='tdp').length,1);
 assert.equal(writes.filter(row=>row.kind==='cpu').length,0);
 assert.equal(schedule.isPerformanceScheduleBusy(),false);
 writes.length=0;
 admitted=true;
 game={pid:79,processCreated:'134400010000000079',name:'wakegame',path:'C:\\Games\\wakegame.exe',source:'whitelist'};
 gameStatusCurrent=false;
 const waiting=await schedule.restorePerformanceScheduleAfterWake('ac',false,()=>admitted);
 assert.deepEqual(writes,[],'unconfirmed game identity admits zero performance actions');
 assert.equal(waiting,'retry-on-game-ready');
 assert.equal(schedule.isPerformanceScheduleBusy(),false);
 gameStatusCurrent=true;
 assert.equal(await schedule.restorePerformanceScheduleAfterWake('ac',false,()=>admitted),'settled');
 assert.equal(writes.filter(row=>row.kind==='tdp').length,1);
 writes.length=0;
 const coreEntered=defer(),coreRelease=defer(),readCore=gameCore.detectGameCorePolicy;
 let reads=0,firstResult,nextResult,oldSignal;
 gameCore.detectGameCorePolicy=async signal=>{if(++reads===1){oldSignal=signal;coreEntered.resolve();await coreRelease.promise;}return readCore();};
 const old=schedule.restorePerformanceScheduleAfterWake('ac',false,()=>admitted).then(value=>{firstResult=value;});
 let next;
 try{
  await coreEntered.promise;publishWakePower({});await flush();
  assert.equal(firstResult,undefined,'publication-only Ready must leave the original read current');
  publishWakePower({observedNotificationSequence:wakePowerFact.observedNotificationSequence+1});await flush();
  assert.equal(firstResult,'retry-on-ready','retired core read must return the original queue slot before its reply');
  assert.equal(oldSignal?.aborted,true);
  assert.deepEqual(writes,[]);
  next=schedule.restorePerformanceScheduleAfterWake('ac',false,()=>admitted).then(value=>{nextResult=value;});
  await flush();assert.equal(nextResult,'settled');assert.equal(reads,2);assert.equal(writes.filter(row=>row.kind==='tdp').length,1);
  coreRelease.resolve();await old;await next;assert.equal(writes.filter(row=>row.kind==='tdp').length,1);
 }finally{coreRelease.resolve();await old;await next;gameCore.detectGameCorePolicy=readCore;}
 game=null;
 settings.performanceSchedule={configured:true,enabled:true,controls:{ac:models.defaultContinuousBank('ac'),dc:models.defaultContinuousBank('dc')}};
 assert.equal(await schedule.restorePerformanceScheduleAfterWake('ac',false,()=>admitted),'settled');
 const setCoreAc=yeman.setCoreModeAc,setScopedCore=yeman.setCoreModeForPowerScope;
 const activate=yeman.setActiveScheme,activateScoped=yeman.setActiveSchemeForPowerScope;
 const coreWrites=[],activations=[];
 let flip=true;
 const recordCore=async(side,mode,scope)=>{
  coreWrites.push({side,mode,scope:scope&&Array.from(scope)});
  if(flip){
   flip=false;
   publishWakePower({generation:5,suspendSerial:1,observedNotificationSequence:wakePowerFact.observedNotificationSequence+1});
   await flush();
  }
 };
 yeman.setCoreModeAc=mode=>recordCore('ac',mode);
 yeman.setCoreModeForPowerScope=recordCore;
 yeman.setActiveScheme=async()=>{activations.push(null);};
 yeman.setActiveSchemeForPowerScope=async(_guid,scope)=>{activations.push(Array.from(scope));};
 try {
  const before=Array.from(wakePower.sharedPowerWriteScope(wakePower.getSharedPowerLifecycle()));
  const editCore=async mode=>{
   const snapshot=await schedule.getContinuousPerformanceSnapshot('global');
   await schedule.updateContinuousPerformanceControls(snapshot.identity,{auto:{coreLow:mode,coreHigh:mode}});
  };
  await editCore('littleOnly');
  assert.equal(coreWrites.length,1,'the old rule must reach the first write');
  assert.deepEqual(activations,[],'an older Ready must not activate the scheme after the core write returns');
  assert.deepEqual(coreWrites[0].scope,before);
  await editCore('bigOnly');
  assert.equal(coreWrites.length,1,'an old owner must not take the new suspend serial');
  assert.equal(await schedule.restorePerformanceScheduleAfterWake('ac',false,()=>admitted),'settled');
  coreWrites.length=activations.length=0;
  await editCore('littleOnly');
  const current=Array.from(wakePower.sharedPowerWriteScope(wakePower.getSharedPowerLifecycle()));
  assert.deepEqual(coreWrites.map(write=>write.scope),[current]);
  assert.deepEqual(activations,[current]);
 } finally {
  admitted=false;off();yeman.setCoreModeAc=setCoreAc;yeman.setCoreModeForPowerScope=setScopedCore;
  yeman.setActiveScheme=activate;yeman.setActiveSchemeForPowerScope=activateScoped;
 }
 fs.mkdirSync(out,{recursive:true});
 const rows=['revoked before actions; current wake can reapply once','revoked after TDP dispatch; later CPU phase stopped and original TDP not replayed','unconfirmed non-Steam identity returns a zero-action retry and the confirmed target can resume','same-generation Ready retires a held pre-dispatch core read while publication-only Ready preserves it','resident core update cannot activate on a newer sleep boundary; a rebuilt owner completes once'];
 fs.writeFileSync(path.join(out,'wake-admission.json'),JSON.stringify({pass:true,rows},null,2),{flag:'wx'});
 console.log(JSON.stringify({pass:true,rows}));process.exit(0);
}
if(process.argv.includes('--boot-preferences-only')){
 const {runPerformancePreferenceOwners}=await import('./boot_modes_fixture/performancePreferences.mjs');
 const out=process.argv[2];if(!out)throw Error('Explicit preference evidence path required');
 const report=await runPerformancePreferenceOwners({out,schedule,models,frameModel,repository,evaluate,source,target,yeman,freshSchedule,
  configureTdp:(mode='ack',limit=150)=>{tdpWriteMode=mode;acceptedLimit=limit;},hardwareWrites:()=>writes,
  configureFloat:backend=>{realFloatBackend=backend;},
  frameService:frames,
  configure:(value,epoch,template,policyGame=null)=>{settings=value;generation=epoch;if(template)deviceTemplate=template;side='ac';source.value='ac';game=policyGame;residentOwner=null;},currentSettings:()=>settings});
 console.log(JSON.stringify({pass:report.pass,actualCases:report.rows.length,scope:'boot-preferences-only'}));process.exit(0);
}
const cases=[];
async function check(name,run){await run();cases.push(name)}
await check('读取新配置不启用、不写硬件；默认掌机55/30与AC120/DC60',async()=>{const before=JSON.stringify(settings);const s=await schedule.getContinuousPerformanceSnapshot();assert.equal(s.bank.mode,'auto');assert.equal(s.bank.tdp,55);assert.equal(s.frameRates.dc.fps,60);assert.equal(JSON.stringify(settings),before);assert.equal(writes.length,0);
 const baseline=clone(await schedule.getContinuousPerformanceSnapshot());const b={...ownerReads};
 const plain=clone(await schedule.getContinuousPerformanceSnapshot());const plainReads=Object.fromEntries(Object.keys(b).map(k=>[k,ownerReads[k]-b[k]]));const c={...ownerReads},timings=[];
 const traced=clone(await schedule.getContinuousPerformanceSnapshot('effective',undefined,(stage,elapsedMs)=>{timings.push({stage,elapsedMs});}));
 assert.deepEqual(traced,plain);assert.deepEqual(plain,baseline);assert.deepEqual(Object.fromEntries(Object.keys(c).map(k=>[k,ownerReads[k]-c[k]])),plainReads);
 assert.deepEqual(timings.map(x=>x.stage).sort(),['performanceSchedule','performanceGameConfig','performancePowerMode','performanceGame','performanceCore','performanceTdpCapability','performanceCpuProfiles','performanceFrameRates'].sort());
 assert(timings.every(x=>Number.isFinite(x.elapsedMs)&&x.elapsedMs>=0));let throws=0;
 const isolated=clone(await schedule.getContinuousPerformanceSnapshot('effective',undefined,()=>{throws++;throw Error('PRIVATE diagnostic unavailable');}));
 assert.equal(throws,8);assert.deepEqual(isolated,plain);assert.equal(JSON.stringify(settings),before);assert.equal(writes.length,0);
});
await check('手动/自动共用当前侧TDP；当前侧CPU写入不污染DC',async()=>{const s=await schedule.getContinuousPerformanceSnapshot();const result=await schedule.updateContinuousPerformanceControls(s.identity,{mode:'manual',tdp:28,manual:{frequencyMHz:3100,aggressiveness:63,turbo:false}});assert.equal(result.bank.tdp,28);assert.equal(result.bank.manual.frequencyMHz,3100);assert.equal(result.controls.dc.tdp,30);const cpu=writes.filter(w=>w.kind==='cpu').at(-1);assert.deepEqual(Array.from(cpu.sides),['ac']);assert.equal(cpu.acMinState,62);assert.equal(cpu.acTurbo,false);const r=await schedule.updateContinuousPerformanceControls(result.identity,{mode:'auto'});assert.equal(r.bank.tdp,28);assert.equal(r.bank.manual.frequencyMHz,3100)});
await check('无游戏/无GPU/无帧率时手动倾向照常写CPU，最右限制最左放开且不改另一侧',async()=>{
 const before=await schedule.getContinuousPerformanceSnapshot(),prior=clone(before.bank),dc=clone(before.controls.dc);
 for(const [bias,freq,aggr,turbo] of [[100,1200,10,false],[0,0,100,true],[50,2900,55,true]]){
  const s=await schedule.getContinuousPerformanceSnapshot(),count=writes.length;
  const next=await schedule.updateContinuousPerformanceControls(s.identity,{mode:'auto',auto:{bias,saving:'none'}});
  const issued=writes.slice(count),cpu=issued.filter(w=>w.kind==='cpu').at(-1);assert.ok(cpu,'Slider commit must reach the real schedule CPU path');
  assert.equal(cpu.acFreq,freq);assert.equal(cpu.acAggr,aggr);assert.equal(cpu.acTurbo,turbo);assert.deepEqual(Array.from(cpu.sides),['ac']);assert.equal(next.bank.auto.bias,bias);
  assert.deepEqual(clone(next.controls.dc),dc);assert.equal(issued.filter(w=>w.kind==='float').length,0,'No floating means no automatic owner');
 }
 const s=await schedule.getContinuousPerformanceSnapshot();await schedule.updateContinuousPerformanceControls(s.identity,{mode:prior.mode,auto:prior.auto});
});
// Explicit minimal scope for the slider regression; the normal full selftest remains unchanged.
if (process.argv.includes('--slider-only')) {
 const output=process.argv[2];assert(output && !output.startsWith('--'),'Slider scope requires an explicit evidence directory');fs.mkdirSync(output,{recursive:true});
 const result={pass:true,count:cases.length,cases,scope:'Direct production schedule commit with in-memory hardware; no game/FPS/GPU; slider scope only; later full-selftest cases NOT_RUN'};
 fs.writeFileSync(path.join(output,'slider-schedule-tests.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));process.exit(0);
}
await check('旧revision与PID复用/切游戏/切电源提交均拒绝',async()=>{let s=await schedule.getContinuousPerformanceSnapshot();await schedule.updateContinuousPerformanceControls(s.identity,{tdp:27});await assert.rejects(schedule.updateContinuousPerformanceControls(s.identity,{tdp:26}));s=await schedule.getContinuousPerformanceSnapshot();side='dc';source.value='dc';await assert.rejects(schedule.updateContinuousPerformanceControls(s.identity,{tdp:26}));side='ac';source.value='ac';s=await schedule.getContinuousPerformanceSnapshot();game={pid:9,processCreated:'1009',name:'a.exe',path:'C:/Games/a.exe'};await assert.rejects(schedule.updateContinuousPerformanceControls(s.identity,{tdp:26}))});
await check('专属从全局复制一次，后续两套TDP和CPU参数独立保存',async()=>{const global=await schedule.loadPerformanceSchedule();const controls=schedule.continuousControlsFromSchedule(global);const profile=global.profiles.ac.performance;await schedule.saveGameCustomConfig({version:1,entries:{'a.exe':{enabled:false,displayName:'A',ac:profile,dc:profile,controls,padPersona:'dualsense-edge',gyroOverride:'racing'}},rtss:{}});let dedicated=await schedule.getContinuousPerformanceSnapshot('dedicated');await schedule.updateContinuousPerformanceControls(dedicated.identity,{tdp:18,auto:{bias:75}});const shared=await schedule.getContinuousPerformanceSnapshot('global');await schedule.updateContinuousPerformanceControls(shared.identity,{tdp:32});dedicated=await schedule.getContinuousPerformanceSnapshot('dedicated');assert.equal(dedicated.bank.tdp,18);assert.equal(dedicated.bank.auto.bias,75);const entry=(await schedule.loadGameCustomConfig()).entries['a.exe'];assert.equal(entry.padPersona,'dualsense-edge');assert.equal(entry.gyroOverride,'racing')});
await check('仅帧率/SMT不启用未配置性能，不改TDP；公用SMT保存后只作用识别EXE',async()=>{
 const off=wakePower.onSharedPowerLifecycle(()=>{});await new Promise(resolve=>setImmediate(resolve));
 try{
  settings.performanceSchedule={configured:false,enabled:false};let s=await schedule.getContinuousPerformanceSnapshot('global');const before=writes.length;
  await schedule.updateContinuousPerformanceControls(s.identity,{frameRate:{fps:30,ceiling:45,lastFps:30}});assert.equal(settings.performanceSchedule.configured,false);
  assert.equal(writes.slice(before).filter(w=>w.kind==='tdp'||w.kind==='cpu').length,0);s=await schedule.getContinuousPerformanceSnapshot('global');
  await schedule.updateContinuousPerformanceControls(s.identity,{hyperThreadMode:'off'});assert.equal(settings.performanceSchedule.configured,false);
  const entry=(await schedule.loadGameCustomConfig()).entries['a.exe'];assert.equal(entry.enabled,false);assert.equal(entry.smtIndependent,false);
  assert.equal(settings.performanceSchedule.hyperThreadPolicy,'off');assert.deepEqual(writes.filter(w=>w.kind==='smt').at(-1),{kind:'smt',pid:9,h:'off',powerScope:[4,0,20]});
 }finally{off();}
});
await check('FPS/TDP同一事务保存；保存失败不先写硬件或部分提交',async()=>{let s=await schedule.getContinuousPerformanceSnapshot('global');const next=await schedule.updateContinuousPerformanceControls(s.identity,{tdp:24,frameRate:{fps:45,ceiling:60,lastFps:45}});assert.equal(next.bank.tdp,24);assert.equal(next.frameRates.ac.fps,45);const before=JSON.stringify(settings),count=writes.length;failSave=true;await assert.rejects(schedule.updateContinuousPerformanceControls(next.identity,{tdp:22,frameRate:{fps:60,ceiling:60,lastFps:60}}));failSave=false;assert.equal(JSON.stringify(settings),before);assert.equal(writes.length,count)});
await check('未改动的DC、RTSS扩展字段和其他模块保留',async()=>{assert.equal(settings.other.keep,42);assert.equal(settings.rtss.future,'KEEP');assert.equal(settings.rtss.frameRates.dc.fps,60);assert.equal(settings.performanceSchedule.controls.dc.tdp,30)});
await check('硬件写TDP期间切电源，不能继续CPU写入旧侧',async()=>{const s=await schedule.getContinuousPerformanceSnapshot('global'),before=writes.length;flipOnTdp=true;await assert.rejects(schedule.updateContinuousPerformanceControls(s.identity,{tdp:26}));assert.equal(writes.slice(before).filter(w=>w.kind==='cpu').length,0);side='ac';source.value='ac'});
await check('150W应用上限、非法数值和无效核心开关有边界',async()=>{let s=await schedule.getContinuousPerformanceSnapshot('global');const r=await schedule.updateContinuousPerformanceControls(s.identity,{tdpCeiling:200,tdp:200});assert.equal(r.bank.tdp,150);s=r;for(const patch of [{tdp:NaN},{manual:{turbo:'yes'}},{auto:{coreLow:'imaginary'}},{frameRate:{fps:10,lastFps:60,ceiling:30}}])await assert.rejects(schedule.updateContinuousPerformanceControls(s.identity,patch))});
await check('各bias/saving组合保持自动>=1.2GHz，积极性为反EPP；无浮动100/激进0最小状态',()=>{for(const bias of Array.from({length:21},(_,i)=>i*5))for(const saving of models.SAVING_LEVELS){const b=models.defaultContinuousBank('ac');b.auto={...b.auto,bias,saving};const c=models.automaticCpuControls(b);assert(c.minMHz>=1200);assert(c.maxMHz===0||c.maxMHz>=c.minMHz);assert(c.aggressiveness>=10&&c.aggressiveness<=100)}const b=models.defaultContinuousBank('ac');b.auto.saving='none';assert.equal(models.automaticCpuControls(b).minState,100);b.auto.saving='aggressive';assert.equal(models.automaticCpuControls(b).minState,0)});
await check('单项五秒验收、未知CPU/Low不授权节能；低帧先撤回TDP',()=>{
 const b=models.defaultContinuousBank('ac');b.auto.bias=0;b.auto.saving='aggressive';b.tdp=28;
 let st={watts:28,frequencyMHz:0,aggressiveness:100,stable:0,lastDown:0,recoverUntil:0};
 for(let t=1000;t<=90000;t+=1000)st=models.stepContinuousFloat(b,st,{now:t,fps:60,low:60,target:60,minimum:2,cpuBusy:0});
 assert.equal(st.watts,14);
 const recovered=models.stepContinuousFloat(b,{...st,trial:null},{now:91000,fps:40,low:29,target:60,minimum:2,cpuBusy:0});assert.equal(recovered.watts,28);assert.equal(recovered.recoverUntil,96000);
 for(const sample of [{cpuBusy:null,low:60},{cpuBusy:0,low:null}]){const held=models.stepContinuousFloat(b,{...st,trial:null},{now:91000,fps:60,target:60,minimum:2,...sample});assert.equal(held.watts,st.watts)}
 const noCap=models.stepContinuousFloat(b,st,{now:91000,fps:300,low:100,target:0,minimum:2,cpuBusy:0});assert.equal(noCap.watts,st.watts);
});
await check('允许两边相同核心模式；旧专属快照不再引用全局挡位',()=>{const global=schedule.defaultPerformanceScheduleConfig();const entry={ac:clone(global.profiles.ac.eco),dc:clone(global.profiles.dc.eco),acMode:'eco',dcMode:'eco'};entry.ac.tdpMax=11;global.profiles.ac.eco.tdpMax=99;assert.equal(schedule.resolveGameCustomProfiles(entry,global).ac.tdpMax,11);const b=models.defaultContinuousBank('ac');b.auto.coreLow=b.auto.coreHigh='bigOnly';assert.equal(models.continuousCoreAt(b,5),'bigOnly');assert.equal(models.continuousCoreAt(b,50),'bigOnly')});
await check('设置目录代数变化使旧请求失效',async()=>{const s=await schedule.getContinuousPerformanceSnapshot('global');generation++;await assert.rejects(schedule.updateContinuousPerformanceControls(s.identity,{tdp:25}))});
await check('旧专属恢复来源不接管全局bank；明确恢复已记忆配置的来源仍可执行',async()=>{
 const previous={settings:clone(settings),game,side,source:source.value,scheme:yeman.getActiveScheme};
 try{
  game=null;side='ac';source.value='ac';yeman.getActiveScheme=async()=> 'external-scheme';
  const config=await schedule.loadPerformanceSchedule();config.configured=true;config.enabled=true;config.suspended=false;
  config.controls=schedule.continuousControlsFromSchedule(config);config.controls.ac.mode='auto';
  await schedule.savePerformanceSchedule(config,{origin:'runtime'});
  const before=JSON.stringify(settings),count=writes.length;
  assert.equal(await schedule.restorePerformanceScheduleIfConfigured('ac',{dedicatedOnly:true}),'none');
  assert.equal(JSON.stringify(settings),before);assert.equal(writes.length,count,'passive dedicated-only recovery wrote the global bank');
  assert.equal(await schedule.restorePerformanceScheduleIfConfigured('ac',{dedicatedOnly:true,restoreRemembered:true}),'auto');
  assert(writes.slice(count).some(row=>row.kind==='tdp'),'explicit remembered restore did not reach the existing TDP owner');
  assert(writes.slice(count).some(row=>row.kind==='cpu'),'explicit remembered restore did not reach the existing CPU owner');
 }finally{
  settings=previous.settings;game=previous.game;side=previous.side;source.value=previous.source;yeman.getActiveScheme=previous.scheme;
  await schedule.loadPerformanceSchedule();
 }
});
await check('手柄自动/手动都写同一连续bank，不嵌套队列且遵守当前滑块上限',async()=>{
 game=null;side='ac';source.value='ac';clockMs+=3100;
 let current=await schedule.getContinuousPerformanceSnapshot('global');
 await schedule.updateContinuousPerformanceControls(current.identity,{mode:'manual',tdp:28,tdpCeiling:30});
 let timer;const result=await Promise.race([schedule.adjustContinuousPerformanceTdp(1),new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('controller transaction deadlocked')),1000)})]).finally(()=>clearTimeout(timer));
 assert.equal(result.applied,true);assert.equal(settings.performanceSchedule.controls.ac.tdp,29);
 current=await schedule.getContinuousPerformanceSnapshot('global');await schedule.updateContinuousPerformanceControls(current.identity,{mode:'auto',tdp:30});clockMs+=3100;
 const boundary=await schedule.adjustContinuousPerformanceTdp(1);assert.equal(boundary.blocked,'boundary');assert.equal(settings.performanceSchedule.controls.ac.tdp,30);
});
await check('OEM实际接受较小瓦数时，浮动从已接受基准开始而保存请求值不被改写',async()=>{
 acceptedLimit=20;let current=await schedule.getContinuousPerformanceSnapshot('global');
 await schedule.updateContinuousPerformanceControls(current.identity,{mode:'auto',tdp:30,auto:{saving:'aggressive'}});
 assert.equal(settings.performanceSchedule.controls.ac.tdp,30);const float=writes.filter(w=>w.kind==='float').at(-1);
 assert.equal(float.tdp,20);assert.deepEqual(clone(float.receipt),{requested:30,accepted:20});acceptedLimit=150;
});
await check('延迟核心刷新只改核心，不重启浮动或重放TDP和CPU',async()=>{
 const off=wakePower.onSharedPowerLifecycle(()=>{});
 try{
  await new Promise(resolve=>setImmediate(resolve));const scope=wakePower.sharedPowerWriteScope(wakePower.getSharedPowerLifecycle());assert(scope);
  const before=writes.length;assert.equal(await schedule.refreshPerformanceScheduleCoreMode(scope),true);
  assert.deepEqual(writes.slice(before).map(w=>w.kind),['core']);
 }finally{off()}
});
await check('只按电池识别两套新模板，已保存参数不被台式机模板覆盖',async()=>{
 const saved=clone(settings.performanceSchedule);settings.performanceSchedule={configured:false,enabled:false};deviceTemplate='portable';
 let s=await schedule.getContinuousPerformanceSnapshot('global');assert.deepEqual([s.controls.ac.tdp,s.controls.ac.tdpCeiling,s.controls.ac.auto.bias,s.controls.ac.auto.saving],[55,55,25,'medium']);
 assert.deepEqual([s.controls.dc.tdp,s.controls.dc.auto.bias,s.controls.dc.auto.saving,s.controls.dc.auto.coreSplit],[30,50,'large',15]);
 deviceTemplate='desktop';s=await schedule.getContinuousPerformanceSnapshot('global');assert.deepEqual([s.controls.ac.tdp,s.controls.ac.auto.bias,s.controls.ac.auto.saving],[120,0,'medium']);
 settings.performanceSchedule=saved;s=await schedule.getContinuousPerformanceSnapshot('global');assert.equal(s.controls.ac.tdp,saved.controls.ac.tdp);deviceTemplate='portable';
});
await check('编辑非当前DC侧只保存该bank，切电源后旧编辑身份被拒绝',async()=>{
 side='ac';source.value='ac';game=null;let s=await schedule.getContinuousPerformanceSnapshot('global','dc');assert.equal(s.side,'dc');assert.equal(s.actualSide,'ac');
 const before=writes.length,ac=clone(settings.performanceSchedule.controls.ac);s=await schedule.updateContinuousPerformanceControls(s.identity,{tdp:23,auto:{bias:40}});
 assert.equal(s.bank.tdp,23);assert.equal(s.bank.auto.bias,40);assert.deepEqual(clone(settings.performanceSchedule.controls.ac),ac);assert.equal(writes.length,before);
 side='dc';source.value='dc';await assert.rejects(schedule.updateContinuousPerformanceControls(s.identity,{tdp:22}));side='ac';source.value='ac';
});
await check('旧公用与专属各自迁移CPU倾向/频率/积极性/睿频/核心，连续配置优先不被旧档回写',()=>{
 const global=schedule.defaultPerformanceScheduleConfig();global.configured=true;global.enabled=true;
 const cpuProfiles={active:'balanced',profiles:{balanced:{acFreq:3000,dcFreq:2100,acAggr:64,dcAggr:32,acTurbo:false,dcTurbo:true}}};
 const profile={...global.profiles.ac.performance,cpuPreset:'balanced',cpuTarget:'none',coreMode:'only-small',tdpMax:19,tdpStrategy:'aggressive'};
 global.profiles.ac[global.active.ac]=profile;
 const expected=schedule.continuousControlsFromSchedule(global,cpuProfiles).ac;
 const entry={enabled:true,ac:clone(profile),dc:{...profile,tdpMax:12,coreMode:'only-big'}};
 const dedicated=schedule.continuousControlsForEntry(entry,global,cpuProfiles);
 assert.equal(dedicated.ac.auto.bias,expected.auto.bias);assert.equal(dedicated.ac.manual.frequencyMHz,3000);assert.equal(dedicated.ac.manual.aggressiveness,64);assert.equal(dedicated.ac.manual.turbo,false);assert.equal(dedicated.ac.manual.coreMode,'littleOnly');assert.equal(dedicated.dc.manual.coreMode,'bigOnly');assert.equal(dedicated.ac.tdp,19);assert.equal(dedicated.ac.auto.saving,'aggressive');assert.equal(dedicated.ac.auto.coreLow,'littleOnly');assert.equal(dedicated.ac.auto.coreHigh,'littleOnly');assert.equal(expected.auto.coreLow,'littleOnly');assert.equal(expected.auto.coreHigh,'littleOnly');
 entry.controls=clone(dedicated);entry.ac.cpuTarget='eco';entry.ac.coreMode='big';entry.ac.tdpMax=99;
 assert.deepEqual(clone(schedule.continuousControlsForEntry(entry,global,cpuProfiles)),clone(dedicated));
 global.controls=clone(dedicated);global.profiles.ac[global.active.ac].tdpMax=88;assert.deepEqual(clone(schedule.continuousControlsFromSchedule(global,cpuProfiles)),clone(dedicated));
});
await check('手柄TDP按已启用专属和当前AC/DC写入，专属关闭后才回到公用bank',async()=>{
 side='ac';source.value='ac';game={name:'a.exe',path:'C:/Games/a.exe',pid:77,processCreated:'100077'};
 const global=await schedule.getContinuousPerformanceSnapshot('global'),base=await schedule.loadPerformanceSchedule(),controls=clone(global.controls);controls.ac.tdp=18;controls.ac.tdpCeiling=30;controls.dc.tdp=12;controls.dc.tdpCeiling=30;
 await schedule.saveGameCustomConfig({version:1,entries:{'a.exe':{enabled:true,displayName:'A',controls,ac:clone(base.profiles.ac.performance),dc:clone(base.profiles.dc.performance),padPersona:'elite',gyroOverride:'racing',future:{keep:true}}},rtss:{'a.exe':{enabled:true,acFps:90,dcFps:45,acCeiling:120,dcCeiling:60}}});
 const beforeGlobal=JSON.stringify((await schedule.getContinuousPerformanceSnapshot('global')).controls);clockMs+=3100;let result=await schedule.adjustContinuousPerformanceTdp(1);assert.equal(result.applied,true);assert.equal(result.tdp,19);let entry=(await schedule.loadGameCustomConfig()).entries['a.exe'];assert.equal(entry.controls.ac.tdp,19);assert.equal(entry.controls.dc.tdp,12);assert.equal(JSON.stringify((await schedule.getContinuousPerformanceSnapshot('global')).controls),beforeGlobal);
 side='dc';source.value='dc';clockMs+=3100;result=await schedule.adjustContinuousPerformanceTdp(-1);assert.equal(result.tdp,11);entry=(await schedule.loadGameCustomConfig()).entries['a.exe'];assert.equal(entry.controls.ac.tdp,19);assert.equal(entry.controls.dc.tdp,11);assert.equal(entry.padPersona,'elite');assert.equal(entry.gyroOverride,'racing');assert.equal(entry.future.keep,true);
 side='ac';source.value='ac';
});
await check('顶部输入编辑与手柄TDP交错：复现旧整份快照覆盖，新字段事务保留最新瓦数和所有CPU/帧率字段',async()=>{
 const {createSourceFile,ScriptTarget,ScriptKind,isFunctionDeclaration}=require('typescript');
 const initial=clone(await schedule.loadGameCustomConfig()),globalBefore=JSON.stringify((await schedule.getContinuousPerformanceSnapshot('global')).controls),output=process.argv[2]||path.resolve('../../../Build/Tasks/ContinuousPerformance-20261017');
 async function runPanel(file){
  await schedule.saveGameCustomConfig(clone(initial));
  const cfg=await schedule.loadGameCustomConfig(),script=fs.readFileSync(file,'utf8').match(/<script setup[^>]*>([\s\S]*?)<\/script>/)[1],tree=createSourceFile('profile.ts',script,ScriptTarget.Latest,true,ScriptKind.TS),names=['clone','makeEntry','captureProfileTarget','saveInputPatch'];
  const funcs=tree.statements.filter(n=>isFunctionDeclaration(n)&&names.includes(n.name?.text)).map(n=>n.getText(tree)).join('\n');
  let expectedTdp=null,interleaved=false;const notices=[];
  const ui=evaluateSource(funcs+'\nmodule.exports={saveInputPatch}',{...profileTargetHelpers(),toRaw:x=>x,props:{game:clone(game)},key:{value:'a.exe'},busy:{value:false},entry:{value:clone(cfg.entries['a.exe'])},config:{value:clone(cfg)},schedule:{value:await schedule.loadPerformanceSchedule()},gameName:{value:'A'},isPadPersonaOverride:()=>true,isGyroOverride:()=>true,tryAcquireQuickAction:()=>()=>{},currentVirtualTargetPolicy:()=>null,requireVirtualTargetAllowed:()=>{},getSettingsGeneration:()=>generation,loadPerformanceSchedule:schedule.loadPerformanceSchedule,loadGameCustomConfig:schedule.loadGameCustomConfig,saveGameCustomConfig:schedule.saveGameCustomConfig,patchGameCustomProfile:schedule.patchGameCustomProfile,syncInputState:()=>{},announce:(...args)=>notices.push(args),emit:()=>{},getContinuousPerformanceSnapshot:async scope=>{const snapshot=await schedule.getContinuousPerformanceSnapshot(scope);if(!interleaved){interleaved=true;clockMs+=3100;const result=await schedule.adjustContinuousPerformanceTdp(1);assert.equal(result.applied,true);expectedTdp=result.tdp;}return snapshot;}});
  await ui.saveInputPatch({gyroOverride:'fps',padPersona:'dualsense-edge'},'saved');const saved=await schedule.loadGameCustomConfig();return {saved,expectedTdp,notices};
 }
 function evaluateSource(source,globals){const module={exports:{}};vm.runInNewContext(transformSync(source,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,...globals});return module.exports;}
 const prior=path.join(output,'before/src/components/GameCustomProfilePanel.vue');let before=null;
 if(fs.existsSync(prior)){const r=await runPanel(prior);before={tdpAfterHotkey:r.expectedTdp,savedTdp:r.saved.entries['a.exe'].controls.ac.tdp};assert.notEqual(before.savedTdp,before.tdpAfterHotkey);}
 const after=await runPanel('src/components/GameCustomProfilePanel.vue'),a=after.saved.entries['a.exe'];assert.equal(a.controls.ac.tdp,after.expectedTdp);assert.deepEqual(clone(a.controls.ac.manual),initial.entries['a.exe'].controls.ac.manual);assert.deepEqual(clone(a.controls.dc),initial.entries['a.exe'].controls.dc);assert.deepEqual(clone(after.saved.rtss),initial.rtss);assert.equal(a.padPersona,'dualsense-edge');assert.equal(a.gyroOverride,'fps');assert.equal(a.gyroPreset,'fps');assert.equal(a.enabled,true);assert.equal(JSON.stringify((await schedule.getContinuousPerformanceSnapshot('global')).controls),globalBefore);
 fs.mkdirSync(output,{recursive:true});fs.writeFileSync(path.join(output,'profile-input-hotkey-interleave.json'),JSON.stringify({pass:true,before,after:{tdpAfterHotkey:after.expectedTdp,savedTdp:a.controls.ac.tdp,padPersona:a.padPersona,gyroOverride:a.gyroOverride,otherPerformanceFieldsPreserved:true,framesPreserved:true}},null,2));
});
await check('专属字段事务只接收输入/启用字段，过期PID/设置代次/启用状态均拒绝，创建与回滚不覆盖性能',async()=>{
 const target=profileTargetHelpers().captureGameCustomProfileTarget(game,generation),before=await schedule.loadGameCustomConfig(),controls=clone(before.entries['a.exe'].controls);
 await assert.rejects(schedule.patchGameCustomProfile(target,{controls}));await assert.rejects(schedule.patchGameCustomProfile(target,{padPersona:'invalid'}));await assert.rejects(schedule.patchGameCustomProfile(target,{enabled:'false'}));assert.deepEqual(clone((await schedule.loadGameCustomConfig()).entries['a.exe'].controls),controls);
 game={...game,processCreated:'reused'};await assert.rejects(schedule.patchGameCustomProfile(target,{padPersona:'elite'}));game={...game,processCreated:target.processCreated};generation++;await assert.rejects(schedule.patchGameCustomProfile(target,{padPersona:'elite'}));generation--;
 await schedule.patchGameCustomProfile(target,{enabled:false},undefined,{expectedEnabled:true});await assert.rejects(schedule.patchGameCustomProfile(target,{enabled:false},undefined,{expectedEnabled:true}));
 const seed={...clone(before.entries['a.exe']),controls:{...controls,ac:{...controls.ac,tdp:3}}};await schedule.patchGameCustomProfile(target,{},seed,{createOnly:true});assert.deepEqual(clone((await schedule.loadGameCustomConfig()).entries['a.exe'].controls),controls);
 await schedule.patchGameCustomProfile(target,{padPersona:'elite'});await schedule.patchGameCustomProfile(target,{enabled:true},undefined,{expectedEnabled:false});let latest=await schedule.loadGameCustomConfig();assert.equal(latest.entries['a.exe'].padPersona,'elite');assert.deepEqual(clone(latest.entries['a.exe'].controls),controls);
 await schedule.patchGameCustomProfile(target,{enabled:false},undefined,{expectedEnabled:true});const global=await schedule.getContinuousPerformanceSnapshot('global');clockMs+=3100;const result=await schedule.adjustContinuousPerformanceTdp(-1);assert.equal(result.tdp,global.bank.tdp-1);latest=await schedule.loadGameCustomConfig();assert.deepEqual(clone(latest.entries['a.exe'].controls),controls);assert.equal(latest.entries['a.exe'].enabled,false);game=null;
});
async function verifyGamepadTdpSteps(schedule){
 const {apply,feedback,events,refresh}=gamepadTdpApp(schedule);
 game=null;side='ac';source.value='ac';
 for(const mode of ['auto','manual']){
  const snapshot=await schedule.getContinuousPerformanceSnapshot('global');await schedule.updateContinuousPerformanceControls(snapshot.identity,{mode,tdp:20,tdpCeiling:30});const dcBefore=JSON.stringify(settings.performanceSchedule.controls.dc);
  clockMs+=3100;await apply(5,'plus-five');assert.equal(settings.performanceSchedule.controls.ac.tdp,25);assert.equal(feedback.at(-1).args.outcome.value.number,25);
  clockMs+=3100;await apply(-10,'minus-ten');assert.equal(settings.performanceSchedule.controls.ac.tdp,15);
  clockMs+=3100;await apply(20);assert.equal(settings.performanceSchedule.controls.ac.tdp,30);
  clockMs+=3100;await apply(-20);assert.equal(settings.performanceSchedule.controls.ac.tdp,10);assert.equal(JSON.stringify(settings.performanceSchedule.controls.dc),dcBefore);
 }
 assert.equal(refresh.value,8);assert.equal(events.length,8);for(const delta of [0,NaN,Infinity,-Infinity,.5,21,-21])await assert.rejects(schedule.adjustContinuousPerformanceTdp(delta));
}
await check('自定义手柄TDP的真实App入口保留5W/10W步长，自动手动均遵守硬件与滑块边界，非法步长拒绝',()=>verifyGamepadTdpSteps(schedule));

async function verifyGamepadTdpRepeat(schedule){
 const {press,idle,pending,feedback,events,refresh}=gamepadTdpApp(schedule);
 for(const owner of ['global','dedicated'])for(const powerSide of ['ac','dc'])for(const mode of ['auto','manual']){
  side=powerSide;source.value=powerSide;game=owner==='dedicated'?{pid:29,processCreated:'tdp-repeat-29',name:'repeat.exe',path:'C:/Games/repeat.exe'}:null;
  if(game){const config=await schedule.loadGameCustomConfig();const global=await schedule.loadPerformanceSchedule();config.entries['repeat.exe']={enabled:true,displayName:'repeat',controls:models.normalizeContinuousControls(undefined),ac:clone(global.profiles.ac.eco),dc:clone(global.profiles.dc.eco),padPersona:'elite',gyroOverride:'racing',future:{keep:true}};await schedule.saveGameCustomConfig(config);}
  let snapshot=await schedule.getContinuousPerformanceSnapshot('effective');await schedule.updateContinuousPerformanceControls(snapshot.identity,{mode,tdp:20,tdpCeiling:45});
  const other=powerSide==='ac'?'dc':'ac',before=await schedule.getContinuousPerformanceSnapshot('effective'),untouched=JSON.stringify(before.controls[other]),framesBefore=JSON.stringify(before.frameRates),globalBefore=game?JSON.stringify((await schedule.getContinuousPerformanceSnapshot('global')).controls):'';
  const expected=[21,22,23,24,25,24,29,19];const deltas=[1,1,1,1,1,-1,5,-10];
  for(let i=0;i<deltas.length;i++){
   clockMs+=i<3?150:40;press(deltas[i],owner+'-'+powerSide+'-'+mode+'-'+i);await idle();
   snapshot=await schedule.getContinuousPerformanceSnapshot('effective');assert.equal(snapshot.bank.tdp,expected[i],owner+'/'+powerSide+'/'+mode+' repeat '+i);assert.equal(snapshot.bank.mode,mode);assert.equal(JSON.stringify(snapshot.controls[other]),untouched);assert.equal(JSON.stringify(snapshot.frameRates),framesBefore);assert.equal(feedback.at(-1).args.outcome.value.number,expected[i]);
  }
  // Multiple already-admitted repeat events still run through the existing serial lane.
  for(let i=0;i<4;i++)press(1,'burst-'+owner+powerSide+mode+i);await idle();snapshot=await schedule.getContinuousPerformanceSnapshot('effective');assert.equal(snapshot.bank.tdp,23);
  const reopened=game?(await schedule.loadGameCustomConfig()).entries['repeat.exe'].controls[powerSide]:(await schedule.loadPerformanceSchedule()).controls[powerSide];assert.equal(reopened.tdp,23);assert.equal(reopened.mode,mode);
  const persisted=game?JSON.parse(files.get(sourcePath)).entries['repeat.exe'].controls[powerSide]:settings.performanceSchedule.controls[powerSide];assert.equal(persisted.tdp,23);assert.equal(persisted.mode,mode);
  if(game){const entry=(await schedule.loadGameCustomConfig()).entries['repeat.exe'];assert.equal(entry.padPersona,'elite');assert.equal(entry.gyroOverride,'racing');assert.equal(entry.future.keep,true);assert.equal(JSON.stringify((await schedule.getContinuousPerformanceSnapshot('global')).controls),globalBefore);}
 }
 assert.equal(refresh.value,96);assert.equal(events.length,96);assert.equal(pending(),0);game=null;side='ac';source.value='ac';
}
await check('手柄连续调节移除旧调档冷却，40/150ms连发在自动手动/ACDC/全局专属均保存同一滑块值',()=>verifyGamepadTdpRepeat(schedule));
await check('连续TDP保持上下限和持久化失败保护，不因连发溢出或回退到另一供电侧',async()=>{
 game=null;side='ac';source.value='ac';let snapshot=await schedule.getContinuousPerformanceSnapshot('global');await schedule.updateContinuousPerformanceControls(snapshot.identity,{mode:'manual',tdp:29,tdpCeiling:30});const dcBefore=JSON.stringify((await schedule.getContinuousPerformanceSnapshot('global')).controls.dc);
 clockMs+=40;assert.equal((await schedule.adjustContinuousPerformanceTdp(5)).tdp,30);clockMs+=40;assert.equal((await schedule.adjustContinuousPerformanceTdp(1)).blocked,'boundary');clockMs+=40;assert.equal((await schedule.adjustContinuousPerformanceTdp(-20)).tdp,10);clockMs+=40;assert.equal((await schedule.adjustContinuousPerformanceTdp(-20)).tdp,2);clockMs+=40;assert.equal((await schedule.adjustContinuousPerformanceTdp(-1)).blocked,'boundary');assert.equal(JSON.stringify(settings.performanceSchedule.controls.dc),dcBefore);
 const before=JSON.stringify(settings);failSave=true;clockMs+=40;await assert.rejects(schedule.adjustContinuousPerformanceTdp(1));failSave=false;assert.equal(JSON.stringify(settings),before);clockMs+=40;assert.equal((await schedule.adjustContinuousPerformanceTdp(1)).tdp,3);
});


await check('轮盘 TDP 回执绑定本次原始写入，保存和 Promise 不代替写入确认',async()=>{
 game=null;side='ac';source.value='ac';acceptedLimit=150;
 for(const [index,mode] of ['ack','none','false','throw','ack'].entries()){
   const requested=18+index; // Receipts require an actual adjustment, not an unchanged resubmission.
  tdpWriteMode=mode;const before=writes.filter(w=>w.kind==='tdp').length;
  const snapshot=await schedule.getContinuousPerformanceSnapshot('effective');
  const result=await schedule.updateContinuousPerformanceControls(snapshot.identity,{tdp:requested});
  assert.ok(result.tdpWriteReceipt);assert.equal(result.tdpWriteReceipt.requested,requested);
  assert.equal(result.tdpWriteReceipt.ok,mode==='ack');assert.equal(result.tdpWriteReceipt.proof,mode==='ack'?'write-ack':'unconfirmed');
  assert.equal(result.tdpWriteReceipt.accepted,mode==='ack'?requested:null);
  if(mode==='ack')assert.equal(writes.filter(w=>w.kind==='tdp').length,before+1);
 }
 tdpWriteMode='ack';
});

await check('轮盘排队操作撤回后不能继续保存或写入',async()=>{
 const snapshot=await schedule.getContinuousPerformanceSnapshot('effective'),before=writes.length;
 await assert.rejects(schedule.updateContinuousPerformanceControls(snapshot.identity,{tdp:19},()=>false));assert.equal(writes.length,before);
});
await check('常驻浮动100次所有权检查与核心保持不重复读配置/探测电源',async()=>{
 game=null;side='ac';source.value='ac';tdpWriteMode='ack';acceptedLimit=150;
 let snapshot=await schedule.getContinuousPerformanceSnapshot('global');
 await schedule.updateContinuousPerformanceControls(snapshot.identity,{mode:'auto',tdp:27,tdpCeiling:35,auto:{saving:'aggressive'}});
 assert.ok(residentOwner);await residentOwner.current();
 ownerReads.settings=ownerReads.files=ownerReads.source=0;
 for(let i=0;i<100;i++){assert.equal(await residentOwner.current(),true);await residentOwner.applyCore(27)}
 assert.deepEqual(ownerReads,{settings:0,files:0,source:0});
 settings.input={unrelated:'changed'};assert.equal(await residentOwner.current(),true);
 assert.deepEqual(ownerReads,{settings:0,files:0,source:0});
});
await check('浮动检查不再分支ACDC，游戏出生/目录失效仍内存立即拒绝',async()=>{
 const prior={...ownerReads};source.value='dc';assert.equal(await residentOwner.current(),true);source.value='ac';
 game={pid:99,processCreated:'new-birth',name:'fixture.exe'};assert.equal(await residentOwner.current(),false);game=null;
 generation++;assert.equal(await residentOwner.current(),false);generation--;
 assert.deepEqual(ownerReads,prior);
});
await check('已观察的配置变化才重核，控制库变化拒绝旧浮动所有者',async()=>{
 settings.performanceSchedule.controls.ac.tdp=26;
 assert.equal(await residentOwner.current(),false);assert.ok(ownerReads.settings>0);
});
await check('电源事件撤销回调后，旧核心链也立即失效且不检测供电侧',async()=>{
 const before={...ownerReads};assert.equal(typeof residentOwner.current.revoke,'function');residentOwner.current.revoke();
 assert.equal(await residentOwner.current(),false);await residentOwner.applyCore(27);assert.deepEqual(ownerReads,before);
});

await check('只改TDP的连续编辑不重套CPU、不停止重启浮动或FPS资源',async()=>{
 const cost=[];game=null;side='ac';source.value='ac';tdpWriteMode='ack';acceptedLimit=150;
 for(const mode of ['manual','auto']){
  let snapshot=await schedule.getContinuousPerformanceSnapshot('effective');
  await schedule.updateContinuousPerformanceControls(snapshot.identity,{mode,tdp:20,tdpCeiling:50,auto:{saving:'aggressive'}});
  const offset=writes.length,reads={...ownerReads},started=performance.now();
  for(let n=0;n<50;n++){snapshot=await schedule.getContinuousPerformanceSnapshot('effective');const updated=await schedule.updateContinuousPerformanceControls(snapshot.identity,{tdp:21+n%5});assert.equal(updated.bank.tdp,21+n%5);assert.equal(updated.tdpWriteReceipt.accepted,21+n%5);assert.equal(updated.tdpWriteReceipt.ok,true);}
  const delta=writes.slice(offset),row={mode,edits:50,cpuWrites:delta.filter(w=>w.kind==='cpu').length,floatStops:delta.filter(w=>w.kind==='stop').length,floatStarts:delta.filter(w=>w.kind==='float').length,fpsActions:delta.filter(w=>w.kind==='fps').length,tdpWrites:delta.filter(w=>w.kind==='tdp').length,settingsReads:ownerReads.settings-reads.settings,fileReads:ownerReads.files-reads.files,sourceReads:ownerReads.source-reads.source,wallMs:performance.now()-started};cost.push(row);
 }
 console.log('TDP_ONLY_COST '+JSON.stringify(cost));
 assert.ok(cost.every(c=>c.tdpWrites===c.edits),'each changed TDP still writes exactly once');
 assert.ok(cost.every(c=>c.cpuWrites===0&&c.floatStops===0&&c.floatStarts===0&&c.fpsActions===0),'TDP-only edits must preserve the existing CPU/monitor/FPS owner');
});

await check('TDP字段提交后快捷操作撤销，旧增量不能绕过新admit写硬件',async()=>{
 game=null;side='ac';source.value='ac';tdpWriteMode='ack';acceptedLimit=150;
 let snapshot=await schedule.getContinuousPerformanceSnapshot('effective');await schedule.updateContinuousPerformanceControls(snapshot.identity,{mode:'auto',tdp:30,tdpCeiling:50,auto:{saving:'aggressive'}});
 snapshot=await schedule.getContinuousPerformanceSnapshot('effective');const before=writes.filter(w=>w.kind==='tdp').length;let allowed=true;afterSettingsCommit=()=>{allowed=false};
 try{await assert.rejects(schedule.updateContinuousPerformanceControls(snapshot.identity,{tdp:31},()=>allowed));}
 finally{afterSettingsCommit=null}
 assert.equal(writes.filter(w=>w.kind==='tdp').length,before);assert.equal(settings.performanceSchedule.controls.ac.tdp,31);
 const current=await schedule.getContinuousPerformanceSnapshot('effective');const recovered=await schedule.updateContinuousPerformanceControls(current.identity,{tdp:32},()=>true);assert.equal(recovered.tdpWriteReceipt.accepted,32);
});

await check(fpsCommitCase,()=>verifyFpsCommitScope(schedule));

const output=process.argv[2]||path.resolve('../../../Build/Tasks/ContinuousPerformance-20261017');fs.mkdirSync(output,{recursive:true});fs.writeFileSync(path.join(output,'state-tests.json'),JSON.stringify({pass:true,count:cases.length,cases},null,2));
console.log(JSON.stringify({pass:true,count:cases.length,cases},null,2));

