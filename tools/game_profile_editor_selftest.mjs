import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const require = createRequire(path.resolve('package.json'));
const { transformSync } = require('esbuild');
const clone = x => structuredClone(x);
function evaluate(file, imports = {}, globals = {}) {
  const module = {exports:{}};
  vm.runInNewContext(transformSync(fs.readFileSync(file,'utf8'),{loader:'ts',format:'cjs'}).code,
    {module,exports:module.exports,require:n=>{if(n in imports)return imports[n];throw Error('Unexpected import '+n)},console,structuredClone,performance,TextEncoder,Date,setTimeout,clearTimeout,queueMicrotask,...globals});
  return module.exports;
}
const models = evaluate('src/bridge/performanceControls.ts');
const frameModel = evaluate('src/bridge/frameRateModel.ts');
const personas = evaluate('src/bridge/controllerPersona.ts');
let failFileWrites=false;
let gameStatusCurrent=true;
let side='ac', game=null, generation=1, flipOnTdp=false, failSave=false, acceptedLimit=150, clockMs=Date.now(), deviceTemplate='portable';
class TestDate extends Date { static now(){return clockMs} }
let settings={rtss:{frameRates:frameModel.frameRatePair({}),future:'KEEP'},performanceSchedule:{configured:false,enabled:false},other:{keep:42}};
const source={value:'ac'}, files=new Map(), writes=[], listeners=new Set();
const sourcePath='C:\\SOFT\\YeMan\\PowerControl\\game-custom.json';
files.set(sourcePath,JSON.stringify({version:1,entries:{},rtss:{}}));
const frames={...frameModel,ensureGlobalFrameRates:async()=>clone(settings.rtss.frameRates),
  applyIndependentFrameRates:async()=>{writes.push({kind:'fps'});return true},publishFrameRatesChanged(){}};
const target={getPolicyGame:()=>game,detectPolicyGame:async()=>game,gamePolicyKey:g=>(g.path||g.name).split(/[\\/]/).pop().toLowerCase(),isPolicyTargetCurrent:t=>!!game&&game.pid===t.pid&&game.processCreated===t.processCreated};
const sectionRevisions=new Map();
const repository={SETTINGS_FILE:sourcePath.replace(/game-custom\.json$/i,'yeman-settings.json'),
  getSettingsSectionRevision:key=>{const json=JSON.stringify(settings[key]??{}),previous=sectionRevisions.get(key);if(!previous||previous.json!==json||previous.generation!==generation)sectionRevisions.set(key,{json,generation,revision:(previous?.revision||0)+1});return sectionRevisions.get(key).revision},
  getSettingsGeneration:()=>generation,assertSettingsGeneration:g=>{if(g!==generation)throw Error('generation')},
  readSettingsSection:async key=>clone(settings[key]||{}),mergeSettings:(a,b)=>({...a,...b}),
  saveSettingsSection:async(key,value)=>{if(failSave)throw Error('disk full');settings[key]=clone(value)},replaceSettingsSection:async(key,value)=>{settings[key]=clone(value)},
  updateSettings:async mutate=>{const next=clone(settings);mutate(next);if(failSave)throw Error('disk full');settings=next}};
const yeman={detectPerformanceTemplate:async()=>deviceTemplate,detectPowerMode:async()=>side,detectPowerModeReliable:async()=>side,ensureRememberedYemanSchemeActive:async()=>{},
  getTdpHardwareMinimum:()=>2,getTdpHardwareMaximum:()=>150,getTdpExecutionCapability:async()=>({selected:'fixture'}),
  detectCoreArchitecture:async()=>({heterogeneous:true}),PW:{YEMAN:'test-yeman'},getActiveScheme:async()=> 'test-yeman',setActiveScheme:async()=>{},
  setCoreModeAc:async mode=>writes.push({kind:'core',side:'ac',mode}),setCoreModeDc:async mode=>writes.push({kind:'core',side:'dc',mode}),
  setTdp:async(s,w,opts={})=>{opts.onAccepted?.(Math.min(acceptedLimit,w));writes.push({kind:'tdp',side:s,w});if(flipOnTdp){side='dc';source.value='dc';flipOnTdp=false}return true},
  applyManualPowerParams:async p=>{writes.push({kind:'cpu',...clone(p)});return true},applyCpuProfilePowerParams:async()=>{},rebuildYemanScheme:async()=>{},setRtssLimit:async()=>{},readRtssLimit:async()=>120};
// The existing hardware port owns one in-memory float run; edits retain its lease.
let floatOwner=null;
const autofloat={
  disableFloat:async()=>{floatOwner=null;writes.push({kind:'stop'})},
  enableContinuousFloat:async(bank,s,t,current,applyCore,receipt)=>{floatOwner={bank:clone(bank),side:s,current,receipt,tdpApplied:receipt?.accepted??0,paused:false};writes.push({kind:'float',side:s,tdp:bank.tdp,target:t,receipt})},
  getFloatInfo:()=>({enabled:floatOwner!==null,tdpApplied:floatOwner?.tdpApplied??0}),
  pauseContinuousFloatForTdpEdit:async()=>{
    const owner=floatOwner;if(!owner||owner.paused)return null;
    owner.paused=true;
    const current=()=>floatOwner===owner;
    const release=()=>{owner.paused=false};
    try {
      if(!await owner.current()||!current()){release();return null}
      return {current,release,accept(requested,accepted){
        if(!current())return;
        const maximum=Math.max(yeman.getTdpHardwareMinimum(),Math.min(yeman.getTdpHardwareMaximum(),requested));
        const confirmed=accepted!==null&&Number.isFinite(accepted)&&accepted>=yeman.getTdpHardwareMinimum()&&accepted<=maximum;
        owner.bank.tdp=confirmed?accepted:maximum;owner.tdpApplied=confirmed?accepted:0;owner.receipt={requested,accepted};
      }};
    } catch(error) {release();throw error}
  },
  updateContinuousFloatCoreRules:bank=>{if(floatOwner)for(const key of ['coreSplit','coreLow','coreHigh'])floatOwner.bank.auto[key]=bank.auto[key]},
  holdContinuousFloatAfterCoreChange:()=>{if(floatOwner)floatOwner.holdUntil=clockMs+10000},
  setFloatRtssLinked(){},cancelPendingRtssSync(){},notifyTdpMaxChanged:async()=>{},applyFloatSettings:async()=>{},enableFloat:async()=>{},setFloatTarget:async()=>{},
};
const snapshots=evaluate('src/bridge/settingsSnapshot.ts',{vue:require('vue')});
const schedule=evaluate('src/bridge/performanceSchedule.ts',{
  './settingsSnapshot':snapshots,
  './controllerPersona':personas,
  './performanceControls':models,'./powerSource':{powerSourceMode:source},'./gameCorePolicy':{detectGameCorePolicy:async()=>({detected:true,heterogeneous:true,smtAvailable:true}),applyGameCorePolicy:async(t,m,h)=>{writes.push({kind:'smt',pid:t.pid,h});return {ok:true,applied:true}}},
  './gamePolicyTarget':target,'./api':{powerLifecycle:{get:async()=>({phase:'ready',hardwareWritesAllowed:true})},fs:{exists:async p=>files.has(p),readTextFile:async p=>files.get(p),writeTextFileAtomic:async(p,v)=>{if(failFileWrites&&p===sourcePath)throw Error("disk full");files.set(p,v)},writeTextFile:async(p,v)=>{files.set(p,v)}}},
  './frameRateLimits':frames,'./settingsRepository':repository,'./gamedetect':{detectGame:async()=>game,isGameStatusCurrent:()=>gameStatusCurrent,isCurrentGameTarget:t=>!!game&&game.pid===t.pid&&String(game.processCreated)===String(t.processCreated)&&(!t.path||t.path.toLowerCase()===game.path.toLowerCase())&&(t.identityGeneration===undefined||t.identityGeneration===game.identityGeneration),validateGameTarget:async()=>game},'./yeman':yeman,'./cpuProfiles':{loadCpuProfiles:async()=>({profiles:{}})},
  './autofloat':autofloat,
},{Date:TestDate});

if(process.argv.includes('--target-identity')){
 repository.loadSettings=async()=>clone(settings);
 const {verifyTargetIdentity}=await import('./fixtures/game_profile_target/verify.mjs');
 const keyModule=evaluate('src/bridge/gamePolicyHysteresis.ts');
 const helper=evaluate('src/bridge/gameCustomProfileTarget.ts',{'./gamePolicyHysteresis':keyModule});
 const index=process.argv.indexOf('--out-dir');if(index<0||!process.argv[index+1])throw Error('--out-dir is required');
 const output=path.resolve(process.argv[index+1]);
 await verifyTargetIdentity({output,schedule,models,frames,clone,
  capture:helper.captureGameCustomProfileTarget,
  setGame:value=>{game=value},setGeneration:value=>{generation=value},setCurrent:value=>{gameStatusCurrent=value},
  state:()=>({files:[...files],settings:clone(settings),writes:clone(writes),custom:files.get(sourcePath)}),evaluate,keyModule});
 process.exit(process.exitCode||0);
}
const cases=[];
async function check(name,run){await run();cases.push(name)}
const normalized=v=>JSON.parse(JSON.stringify(v));
function merge(base,patch){const out=clone(base);for(const [key,value] of Object.entries(patch)){out[key]=value&&typeof value==='object'&&!Array.isArray(value)?merge(out[key]&&typeof out[key]==='object'?out[key]:{},value):clone(value)}return out}
repository.mergeSettings=merge;
settings.input={schemaVersion:1,revision:1,outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true},gyroMotion:{enabled:true,preset:'fps',activePreset:'fps',gyroWeight:1.8,virtualPadLink:true,presets:{fps:{gyroWeight:1.8}}},ownership:{},buttonMapping:{}};
repository.loadSettings=async()=>clone(settings);
let baseWrites=0;
repository.compareAndSwapInputSettings=async(revision,patch,isCurrent)=>{if(isCurrent&&!isCurrent())throw Error('cancelled');if(revision!==settings.input.revision)return {ok:false,reason:'revision-conflict',value:clone(settings.input)};baseWrites++;settings.input=merge(settings.input,patch);settings.input.revision++;return {ok:true,revision:settings.input.revision,value:clone(settings.input)}};
const policyListeners=new Set();target.subscribePolicyGameStatus=cb=>{policyListeners.add(cb);return()=>policyListeners.delete(cb)};target.isPolicyGameInitialized=()=>true;
function setGame(value){game=value;for(const cb of [...policyListeners])cb(game)}
const gyroModel=evaluate('src/bridge/gyroPresetModel.ts',{'./hcInputUtils':{HC_MIN_GYRO_THRESHOLD_DPS:0.1}});
const overlay=evaluate('src/bridge/gameInputOverride.ts',{'./gyroPresetModel':gyroModel,'@/bridge/settingsRepository':repository,'@/bridge/gamePolicyTarget':target,'@/bridge/ipc':{invoke:async()=> 'fixture-session'},'@/scheduler':{registerScheduledTask:()=>()=>{}},'@/bridge/performanceSchedule':schedule});
const editor=evaluate('src/bridge/gameInputEditor.ts',{'./settingsSnapshot':snapshots,'./settingsRepository':repository,'./performanceSchedule':schedule,'./gamePolicyTarget':target,'./gameInputOverride':overlay,'./gamedetect':{detectedGameName:g=>g?.title||g?.name||''}});
const makeGame=(pid,name='a.exe')=>({pid,processCreated:String(1000+pid),name,path:'C:/Games/'+name,title:name,ts:0});
const a=makeGame(9);setGame(a);
const config=await schedule.loadPerformanceSchedule();const controls=schedule.continuousControlsFromSchedule(config);
await schedule.saveGameCustomConfig({version:1,entries:{'a.exe':{enabled:true,displayName:'Game A',ac:config.profiles.ac.performance,dc:config.profiles.dc.performance,controls,padPersona:'follow',gyroOverride:'follow'}},rtss:{}});
await check('性能页自动跟随启用专属，保存TDP只改专属AC并保留公用和DC',async()=>{const base=JSON.stringify(settings.performanceSchedule);const s=await schedule.getContinuousPerformanceSnapshot('effective');assert.equal(s.identity.scope,'dedicated');assert.equal(s.gameName,'Game A');const result=await schedule.updateContinuousPerformanceControls(s.identity,{tdp:28});assert.equal(result.identity.followEffective,true);assert.equal(result.bank.tdp,28);assert.equal(JSON.stringify(settings.performanceSchedule),base);assert.equal(result.controls.dc.tdp,30)});
await check('全部follow的启用专属仍可在普通手柄页编辑，Vue代理安全且不改公用',async()=>{const before=JSON.stringify(settings.input);const current=await editor.readGameInputEditor();assert.equal(current.gameName,'Game A');assert.equal(current.input.outputTarget.persona,'steamdeck');await editor.saveGameInputEditor(require('vue').reactive(current),{outputTarget:{persona:'elite',buttonMappingEnabled:true}});const saved=(await schedule.loadGameCustomConfig()).entries['a.exe'];assert.equal(saved.padPersona,'elite');assert.equal(saved.inputOverrides.outputTarget.persona,'elite');assert.equal(JSON.stringify(settings.input),before);assert.equal(baseWrites,0)});
await check('普通陀螺仪细调存入同一专属，实际overlay和回读一致，公用预设不变',async()=>{const current=await editor.readGameInputEditor();await editor.saveGameInputEditor(current,{outputTarget:{gyroEnabled:true},gyroMotion:{enabled:true,activePreset:'fps',preset:'fps',gyroWeight:2.4,presets:{fps:{gyroWeight:2.4}}}});const reread=await editor.readGameInputEditor();assert.equal(reread.input.gyroMotion.gyroWeight,2.4);assert.equal(settings.input.gyroMotion.gyroWeight,1.8);const built=overlay.buildGameInputOverlay(settings.input,reread.profile,'fixture-id','fixture-session');assert.equal(built.gyroMotion.gyroWeight,2.4);assert.equal(built.outputTarget.persona,'elite')});
await check('顶部切换预设读取专属预设参数；切本机手柄同时关闭专属陀螺仪',async()=>{let current=await editor.readGameInputEditor();await schedule.patchGameCustomProfile(current.target,{gyroOverride:'fps'});current=await editor.readGameInputEditor();assert.equal(current.input.gyroMotion.gyroWeight,2.4);await editor.saveGameInputEditor(current,{outputTarget:{persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false},gyroMotion:{enabled:false}});current=await editor.readGameInputEditor();assert.equal(current.input.outputTarget.persona,'disabled');assert.equal(current.input.gyroMotion.enabled,false)});
await check('游戏切换、PID复用、专属关闭后旧编辑快照拒绝，绝不改公用或另一游戏',async()=>{let current=await editor.readGameInputEditor();setGame(makeGame(10,'b.exe'));await assert.rejects(editor.saveGameInputEditor(current,{gyroMotion:{enabled:true}}));setGame({...a,processCreated:'reused'});await assert.rejects(editor.saveGameInputEditor(current,{outputTarget:{persona:'elite'}}));setGame(a);current=await editor.readGameInputEditor();await schedule.patchGameCustomProfile(current.target,{enabled:false});await assert.rejects(editor.saveGameInputEditor(current,{gyroMotion:{enabled:true}}));assert.equal((await editor.readGameInputEditor()).profile,undefined);assert.equal(baseWrites,0);await schedule.patchGameCustomProfile(current.target,{enabled:true})});
await check('专属输入并发修改拒绝旧细调，性能并发修改不丢输入',async()=>{const current=await editor.readGameInputEditor();await schedule.patchGameCustomProfile(current.target,{gyroOverride:'racing'});await assert.rejects(editor.saveGameInputEditor(current,{gyroMotion:{enabled:true}}));const fresh=await editor.readGameInputEditor();const s=await schedule.getContinuousPerformanceSnapshot('effective');await schedule.updateContinuousPerformanceControls(s.identity,{tdp:27});await editor.saveGameInputEditor(fresh,{outputTarget:{persona:'steamdeck',buttonMappingEnabled:true}});assert.equal((await schedule.getContinuousPerformanceSnapshot('effective')).bank.tdp,27)});
await check('确认期间配置变更、换游戏与配置重置均不删除错误条目',async()=>{const current=await editor.readGameInputEditor();let custom=await schedule.loadGameCustomConfig();let revision=JSON.stringify([custom.entries['a.exe'],custom.rtss['a.exe']]);await schedule.patchGameCustomProfile(current.target,{gyroOverride:'off'});await assert.rejects(schedule.removeGameCustomProfile(current.target,revision));custom=await schedule.loadGameCustomConfig();revision=JSON.stringify([custom.entries['a.exe'],custom.rtss['a.exe']]);setGame(makeGame(22,'b.exe'));await assert.rejects(schedule.removeGameCustomProfile(current.target,revision));setGame(a);assert.ok((await schedule.loadGameCustomConfig()).entries['a.exe']);generation++;await assert.rejects(schedule.removeGameCustomProfile(current.target,revision));generation--});
await check('遵循全局忽略已保存专属细调，关闭后重新启用使旧编辑快照失效',async()=>{let current=await editor.readGameInputEditor();await schedule.patchGameCustomProfile(current.target,{padPersona:'follow',gyroOverride:'follow'});current=await editor.readGameInputEditor();assert.equal(current.input.gyroMotion.gyroWeight,1.8);assert.equal(overlay.buildGameInputOverlay(settings.input,current.profile,'fixture-id','fixture-session'),null);await schedule.patchGameCustomProfile(current.target,{enabled:false});await schedule.patchGameCustomProfile(current.target,{enabled:true});await assert.rejects(editor.saveGameInputEditor(current,{gyroMotion:{enabled:true}}))});
await check('解除专属写盘失败保留原配置，队列恢复后仍能重试',async()=>{const current=await editor.readGameInputEditor();const custom=await schedule.loadGameCustomConfig();const before=files.get(sourcePath);failFileWrites=true;try{await assert.rejects(schedule.removeGameCustomProfile(current.target,JSON.stringify([custom.entries['a.exe'],custom.rtss['a.exe']])))}finally{failFileWrites=false}assert.equal(files.get(sourcePath),before);assert.ok((await schedule.loadGameCustomConfig()).entries['a.exe'])});
await check('解除专属删除游戏与RTSS记录，普通三页恢复公用，旧快照不能重建专属',async()=>{const current=await editor.readGameInputEditor();const custom=await schedule.loadGameCustomConfig();await schedule.removeGameCustomProfile(current.target,JSON.stringify([custom.entries['a.exe'],custom.rtss['a.exe']]));const saved=await schedule.loadGameCustomConfig();assert.equal(saved.entries['a.exe'],undefined);assert.equal(saved.rtss['a.exe'],undefined);const effective=await schedule.getContinuousPerformanceSnapshot('effective');assert.equal(effective.identity.scope,'global');const read=await editor.readGameInputEditor();assert.equal(read.gameName,'');assert.equal(read.input.outputTarget.persona,'steamdeck');assert.equal(read.input.gyroMotion.gyroWeight,1.8);await assert.rejects(editor.saveGameInputEditor(current,{outputTarget:{persona:'elite'}}));assert.equal((await schedule.loadGameCustomConfig()).entries['a.exe'],undefined)});
await check('专属未启用或游戏退出后普通页仅写公用，旧公用快照遇启用也拒绝',async()=>{let current=await editor.readGameInputEditor();await editor.saveGameInputEditor(current,{outputTarget:{persona:'elite'}});assert.equal(settings.input.outputTarget.persona,'elite');current=await editor.readGameInputEditor();await schedule.saveGameCustomConfig({version:1,entries:{'a.exe':{enabled:true,displayName:'A',ac:config.profiles.ac.performance,dc:config.profiles.dc.performance,controls}},rtss:{}});await assert.rejects(editor.saveGameInputEditor(current,{outputTarget:{persona:'disabled'}}));setGame(null);current=await editor.readGameInputEditor();assert.equal(current.profile,undefined);assert.equal((await schedule.getContinuousPerformanceSnapshot('effective')).identity.scope,'global')});
await check('一键设为专属复制公用配置并立即启用，创建竞争不覆盖其它配置',async()=>{setGame(makeGame(33,'direct.exe'));const current=await editor.readGameInputEditor();const seed={displayName:'超长中文专属测试游戏',enabled:true,ac:config.profiles.ac.performance,dc:config.profiles.dc.performance,controls,padPersona:'follow',gyroOverride:'follow'};await schedule.patchGameCustomProfile(current.target,{enabled:true},seed,{expectedAbsent:true});assert.equal((await schedule.getContinuousPerformanceSnapshot('effective')).identity.scope,'dedicated');await assert.rejects(schedule.patchGameCustomProfile(current.target,{enabled:true},seed,{expectedAbsent:true}));assert.equal((await schedule.loadGameCustomConfig()).entries['direct.exe'].displayName,seed.displayName)});
await check('非法专属输入结构拒绝保存且不更改配置',async()=>{const current=await editor.readGameInputEditor();const before=files.get(sourcePath);for(const inputOverrides of [[],1,null,{other:{}},{gyroMotion:[]}])await assert.rejects(schedule.patchGameCustomProfile(current.target,{inputOverrides}));assert.equal(files.get(sourcePath),before)});
const result={pass:true,count:cases.length,cases,boundary:'in-memory settings and mocked hardware only',baseInputWrites:baseWrites};const output=process.argv[2];if(output){fs.mkdirSync(output,{recursive:true});fs.writeFileSync(path.join(output,'game-profile-editor-tests.json'),JSON.stringify(result,null,2))}console.log(JSON.stringify(result,null,2));
