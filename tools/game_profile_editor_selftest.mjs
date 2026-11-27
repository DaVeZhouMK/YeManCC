import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const require = createRequire(path.resolve('package.json'));
const { transformSync } = require('esbuild');
const clone = x => structuredClone(x);
function evaluate(file, imports = {}, globals = {}, expose = []) {
  const module = {exports:{}};
  let source=fs.readFileSync(file,'utf8');
  if(file.endsWith('.vue')){
    const script=source.match(/<script setup lang="ts">([\s\S]*?)<\/script>/);
    if(!script||!expose.length)throw Error('Expected one actual Vue setup script and explicit exports');
    source=script[1]+'\nexport { '+expose.join(',')+' };';
  }
  vm.runInNewContext(transformSync(source,{loader:'ts',format:'cjs'}).code,
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
  './powerLifecycleState':evaluate('src/bridge/powerLifecycleState.ts',{'./ipc':{invoke:async()=>({phase:'ready',known:true,hardwareWritesAllowed:true,generation:1}),on:()=>()=>{}}}),
  './bootModeGlobalPreferences':evaluate('src/bridge/bootModeGlobalPreferences.ts',{'./inputConfigHash':evaluate('src/bridge/inputConfigHash.ts'),'./settingsBaseline':evaluate('src/bridge/settingsBaseline.ts',{'../../PowerControl/settings-baseline.json':JSON.parse(fs.readFileSync('PowerControl/settings-baseline.json','utf8'))})}),
  './controllerPersona':personas,
  './performanceControls':models,'./powerSource':{powerSourceMode:source},'./gameCorePolicy':{detectGameCorePolicy:async()=>({detected:true,heterogeneous:true,smtAvailable:true}),applyGameCorePolicy:async(t,m,h)=>{writes.push({kind:'smt',pid:t.pid,h});return {ok:true,applied:true}}},
  './gamePolicyTarget':target,'./api':{powerLifecycle:{get:async()=>({phase:'ready',hardwareWritesAllowed:true})},fs:{exists:async p=>files.has(p),readTextFile:async p=>files.get(p),writeTextFileAtomic:async(p,v)=>{if(failFileWrites&&p===sourcePath)throw Error("disk full");files.set(p,v)},writeTextFile:async(p,v)=>{files.set(p,v)}}},
  './frameRateLimits':frames,'./settingsRepository':repository,'./gamedetect':{detectGame:async()=>game,isGameStatusCurrent:()=>gameStatusCurrent,isCurrentGameTarget:t=>!!game&&game.pid===t.pid&&String(game.processCreated)===String(t.processCreated)&&(!t.path||t.path.toLowerCase()===game.path.toLowerCase())&&(t.identityGeneration===undefined||t.identityGeneration===game.identityGeneration),validateGameTarget:async()=>game},'./yeman':yeman,'./cpuProfiles':{loadCpuProfiles:async()=>({profiles:{}})},
  './autofloat':autofloat,
},{Date:TestDate});

if(process.argv.includes('--target-identity')){
 repository.loadSettings=async()=>clone(settings);
 const {verifyTargetIdentity,verifyInputPreflight}=await import('./fixtures/game_profile_target/verify.mjs');
 const selectedAt=process.argv.indexOf('--target-case');
 const selection=selectedAt<0?'identity':process.argv[selectedAt+1];
 if(!['identity','input-preflight','input-preflight-switch','input-preflight-controls'].includes(selection))throw Error('Invalid target case selection');
 const keyModule=evaluate('src/bridge/gamePolicyHysteresis.ts');
 const helper=evaluate('src/bridge/gameCustomProfileTarget.ts',{'./gamePolicyHysteresis':keyModule});
 const index=process.argv.indexOf('--out-dir');if(index<0||!process.argv[index+1])throw Error('--out-dir is required');
 const output=path.resolve(process.argv[index+1]);
 await (selection==='identity'?verifyTargetIdentity:verifyInputPreflight)({output,schedule,models,frames,clone,selection,vue:require('vue'),
  capture:helper.captureGameCustomProfileTarget,
  setGame:value=>{game=value},setGeneration:value=>{generation=value},setCurrent:value=>{gameStatusCurrent=value},
  state:()=>({files:[...files],settings:clone(settings),writes:clone(writes),custom:files.get(sourcePath)}),evaluate,keyModule});
 process.exit(process.exitCode||0);
}
const cases=[];
async function check(name,run){await run();cases.push(name)}
function merge(base,patch){const out=clone(base);for(const [key,value] of Object.entries(patch)){out[key]=value&&typeof value==='object'&&!Array.isArray(value)?merge(out[key]&&typeof out[key]==='object'?out[key]:{},value):clone(value)}return out}
repository.mergeSettings=merge;
settings.input={schemaVersion:1,revision:1,outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true},gyroMotion:{enabled:true,preset:'fps',activePreset:'fps',gyroWeight:1.8,virtualPadLink:true,presets:{fps:{gyroWeight:1.8}}},ownership:{},buttonMapping:{}};
repository.loadSettings=async()=>clone(settings);
let baseWrites=0;
const inputListeners=new Set();
repository.onInputSettingsChanged=cb=>{inputListeners.add(cb);return()=>inputListeners.delete(cb)};
repository.compareAndSwapInputSettings=async(revision,patch,isCurrent)=>{if(isCurrent&&!isCurrent())throw Error('cancelled');if(revision!==settings.input.revision)return {ok:false,reason:'revision-conflict',value:clone(settings.input)};if(settings.input.gameOverride&&!Object.hasOwn(patch,'gameOverride'))return {ok:false,reason:'game-override-active',value:clone(settings.input)};baseWrites++;settings.input=merge(settings.input,patch);settings.input.revision++;for(const cb of [...inputListeners])cb(clone(settings.input));return {ok:true,revision:settings.input.revision,value:clone(settings.input)}};
const policyListeners=new Set();target.subscribePolicyGameStatus=cb=>{policyListeners.add(cb);return()=>policyListeners.delete(cb)};
function setGame(value){game=value;for(const cb of [...policyListeners])cb(game)}
const gyroModel=evaluate('src/bridge/gyroPresetModel.ts',{'./hcInputUtils':{HC_MIN_GYRO_THRESHOLD_DPS:0.1}});
const overlay=evaluate('src/bridge/gameInputOverride.ts');
const editor=evaluate('src/bridge/gameInputEditor.ts',{'./gyroPresetModel':gyroModel,'./settingsRepository':repository});
const makeGame=(pid,name='a.exe')=>({pid,processCreated:String(1000+pid),name,path:'C:/Games/'+name,title:name,ts:0});
const profileTarget=()=>({exe:target.gamePolicyKey(game),pid:game.pid,processCreated:game.processCreated,path:game.path,identityGeneration:game.identityGeneration,generation});
const a=makeGame(9);setGame(a);
const config=await schedule.loadPerformanceSchedule();const controls=schedule.continuousControlsFromSchedule(config);
await schedule.saveGameCustomConfig({version:1,entries:{'a.exe':{enabled:true,displayName:'Game A',ac:config.profiles.ac.performance,dc:config.profiles.dc.performance,controls,padPersona:'elite',gyroOverride:'racing',gyroPreset:'racing',inputOverrides:{gyroMotion:{gyroWeight:9.9}}}},rtss:{}});
await check('性能页仍跟随专属，TDP编辑保留全局和DC',async()=>{
 const base=JSON.stringify(settings.performanceSchedule),s=await schedule.getContinuousPerformanceSnapshot('effective');assert.equal(s.identity.scope,'dedicated');assert.equal(s.gameName,'Game A');
 const result=await schedule.updateContinuousPerformanceControls(s.identity,{tdp:28});assert.equal(result.identity.followEffective,true);assert.equal(result.bank.tdp,28);assert.equal(JSON.stringify(settings.performanceSchedule),base);assert.equal(result.controls.dc.tdp,30);
});
await check('活动专属的旧输入字段不影响普通页，Vue代理编辑仅保存全局',async()=>{
 const before=files.get(sourcePath),current=await editor.readGameInputEditor();assert.equal(current.gameName,'');assert.equal(current.profile,undefined);assert.equal(current.input.outputTarget.persona,'steamdeck');
 const result=await editor.saveGameInputEditor(require('vue').reactive(current),{outputTarget:{persona:'elite',buttonMappingEnabled:true}});assert.equal(result.ok,true);assert.equal(settings.input.outputTarget.persona,'elite');assert.equal(files.get(sourcePath),before);assert.equal(baseWrites,1);
});
await check('陀螺仪细调和预设参数存入全局，所有专属overlay均为空',async()=>{
 const before=files.get(sourcePath),current=await editor.readGameInputEditor();await editor.saveGameInputEditor(current,{outputTarget:{gyroEnabled:true},gyroMotion:{enabled:true,activePreset:'fps',preset:'fps',gyroWeight:2.4,presets:{fps:{gyroWeight:2.4}}}});
 const reread=await editor.readGameInputEditor();assert.equal(reread.input.gyroMotion.gyroWeight,2.4);assert.equal(settings.input.gyroMotion.gyroWeight,2.4);assert.equal(files.get(sourcePath),before);
 for(const gyroOverride of ['follow','on','off','fps','racing','custom','steam'])assert.equal(overlay.buildGameInputOverlay(settings.input,{enabled:true,padPersona:'elite',gyroOverride,inputOverrides:{gyroMotion:{gyroWeight:9.9}}},'fixture-id','fixture-session'),null);
});
await check('全局本机手柄选择关闭陀螺仪但保留四个预设',async()=>{
 const current=await editor.readGameInputEditor();await editor.saveGameInputEditor(current,{outputTarget:{persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false},gyroMotion:{enabled:false}});
 const next=await editor.readGameInputEditor();assert.equal(next.input.outputTarget.persona,'disabled');assert.equal(next.input.gyroMotion.enabled,false);assert.equal(next.input.gyroMotion.presets.fps.gyroWeight,2.4);
});
await check('游戏切换和PID复用不改变全局输入所有权，配置重置和取消仍拒绝旧编辑',async()=>{
 let current=await editor.readGameInputEditor();setGame(makeGame(10,'b.exe'));assert.equal((await editor.saveGameInputEditor(current,{outputTarget:{persona:'elite'}})).ok,true);
 current=await editor.readGameInputEditor();setGame({...a,processCreated:'reused'});assert.equal((await editor.saveGameInputEditor(current,{outputTarget:{persona:'steamdeck'}})).ok,true);setGame(a);
 current=await editor.readGameInputEditor();const writesBefore=baseWrites;generation++;await assert.rejects(editor.saveGameInputEditor(current,{gyroMotion:{enabled:true}}));generation--;await assert.rejects(editor.saveGameInputEditor(current,{gyroMotion:{enabled:true}},()=>false));assert.equal(baseWrites,writesBefore);
});
await check('并发全局输入由CAS拒绝旧修订，性能编辑不影响全局输入',async()=>{
 const current=await editor.readGameInputEditor();await editor.saveGameInputEditor(current,{gyroMotion:{enabled:true}});const stale=await editor.saveGameInputEditor(current,{gyroMotion:{enabled:false}});assert.equal(stale.ok,false);assert.equal(stale.reason,'revision-conflict');
 const fresh=await editor.readGameInputEditor(),s=await schedule.getContinuousPerformanceSnapshot('effective');await schedule.updateContinuousPerformanceControls(s.identity,{tdp:27});assert.equal((await editor.saveGameInputEditor(fresh,{outputTarget:{persona:'steamdeck',buttonMappingEnabled:true}})).ok,true);assert.equal((await schedule.getContinuousPerformanceSnapshot('effective')).bank.tdp,27);
});
await check('确认期间配置变更、换游戏与配置重置仍不删除错误条目',async()=>{
 const current=profileTarget();let custom=await schedule.loadGameCustomConfig(),revision=JSON.stringify([custom.entries['a.exe'],custom.rtss['a.exe']]);custom.entries['a.exe'].displayName='Changed after confirmation';await schedule.saveGameCustomConfig(custom);await assert.rejects(schedule.removeGameCustomProfile(current,revision));
 custom=await schedule.loadGameCustomConfig();revision=JSON.stringify([custom.entries['a.exe'],custom.rtss['a.exe']]);setGame(makeGame(22,'b.exe'));await assert.rejects(schedule.removeGameCustomProfile(current,revision));setGame(a);assert.ok((await schedule.loadGameCustomConfig()).entries['a.exe']);generation++;await assert.rejects(schedule.removeGameCustomProfile(current,revision));generation--;
});
await check('停用和启用专属不使全局输入快照失效',async()=>{
 const current=await editor.readGameInputEditor(),captured=profileTarget();await schedule.patchGameCustomProfile(captured,{enabled:false});await schedule.patchGameCustomProfile(captured,{enabled:true});assert.equal((await editor.saveGameInputEditor(current,{gyroMotion:{enabled:true}})).ok,true);assert.equal((await editor.readGameInputEditor()).profile,undefined);
});
await check('解除专属写盘失败保留原配置，队列恢复后仍可重试',async()=>{
 const current=profileTarget(),custom=await schedule.loadGameCustomConfig(),before=files.get(sourcePath);failFileWrites=true;try{await assert.rejects(schedule.removeGameCustomProfile(current,JSON.stringify([custom.entries['a.exe'],custom.rtss['a.exe']])))}finally{failFileWrites=false}assert.equal(files.get(sourcePath),before);assert.ok((await schedule.loadGameCustomConfig()).entries['a.exe']);
});
await check('解除专属删除性能和RTSS，既有全局输入编辑不能重建游戏条目',async()=>{
 const current=await editor.readGameInputEditor(),custom=await schedule.loadGameCustomConfig();await schedule.removeGameCustomProfile(profileTarget(),JSON.stringify([custom.entries['a.exe'],custom.rtss['a.exe']]));const saved=await schedule.loadGameCustomConfig();assert.equal(saved.entries['a.exe'],undefined);assert.equal(saved.rtss['a.exe'],undefined);assert.equal((await schedule.getContinuousPerformanceSnapshot('effective')).identity.scope,'global');
 assert.equal((await editor.saveGameInputEditor(current,{outputTarget:{persona:'elite'}})).ok,true);assert.equal((await schedule.loadGameCustomConfig()).entries['a.exe'],undefined);assert.equal((await editor.readGameInputEditor()).input.gyroMotion.gyroWeight,2.4);
});
await check('普通页只订阅全局输入且取消后无残留，游戏识别未知也可读取',async()=>{
 const baseline=inputListeners.size;let changes=0;const stop=editor.onGameInputEditorChanged(()=>changes++);assert.equal(inputListeners.size,baseline+1);gameStatusCurrent=false;setGame(null);const current=await editor.readGameInputEditor();assert.equal(current.profile,undefined);assert.equal((await editor.saveGameInputEditor(current,{gyroMotion:{enabled:true}})).ok,true);assert.equal(changes,1);stop();assert.equal(inputListeners.size,baseline);assert.equal(policyListeners.size,0);gameStatusCurrent=true;setGame(a);
});
await check('一键专属保留性能创建竞争保护且不创建输入记忆',async()=>{
 setGame(makeGame(33,'direct.exe'));const current=profileTarget(),seed={displayName:'超长中文专属测试游戏',enabled:true,ac:config.profiles.ac.performance,dc:config.profiles.dc.performance,controls};await schedule.patchGameCustomProfile(current,{enabled:true},seed,{expectedAbsent:true});assert.equal((await schedule.getContinuousPerformanceSnapshot('effective')).identity.scope,'dedicated');await assert.rejects(schedule.patchGameCustomProfile(current,{enabled:true},seed,{expectedAbsent:true}));const saved=(await schedule.loadGameCustomConfig()).entries['direct.exe'];assert.equal(saved.displayName,seed.displayName);assert.equal(saved.padPersona??'follow','follow');assert.equal(saved.gyroOverride??'follow','follow');for(const key of ['gyroPreset','inputOverrides'])assert.equal(saved[key],undefined);
});
await check('新增专属输入字段及非法结构全部拒绝，不修改文件',async()=>{
 const current=profileTarget(),before=files.get(sourcePath);for(const patch of [{padPersona:'elite'},{gyroOverride:'fps'},{gyroPreset:'custom'},... [[],1,null,{other:{}},{gyroMotion:[]}].map(inputOverrides=>({inputOverrides}))])await assert.rejects(schedule.patchGameCustomProfile(current,patch));assert.equal(files.get(sourcePath),before);
});
await check('旧保存overlay不改变全局显示，下一次全局CAS将其退休',async()=>{
 settings.input.gameOverride={sessionId:'fixture-session',identity:'legacy',outputTarget:{persona:'dualsense-edge'},gyroMotion:{enabled:true,gyroWeight:9.9}};const current=await editor.readGameInputEditor();assert.equal(overlay.effectiveInputPersona(current.input),'elite');assert.equal(current.input.gyroMotion.gyroWeight,2.4);assert.equal((await editor.saveGameInputEditor(current,{gyroMotion:{enabled:false}})).ok,true);assert.equal(settings.input.gameOverride,null);
});
await check('旧常驻API没有输入所有权或订阅资源',()=>{
 overlay.startGameInputOverrideWatch();let calls=0;const stop=overlay.subscribeGameInputOverrideState(state=>{calls++;assert.equal(state.locked,false);assert.equal(state.identity,'')});overlay.stopGameInputOverrideWatch();stop();overlay.startGameInputOverrideWatch();overlay.stopGameInputOverrideWatch();assert.equal(calls,1);assert.equal(overlay.getGameInputOverrideState().locked,false);assert.equal(policyListeners.size,0);
});
const result={pass:true,count:cases.length,cases,boundary:'in-memory settings and mocked hardware only',baseInputWrites:baseWrites};const output=process.argv[2];if(output){fs.mkdirSync(output,{recursive:true});fs.writeFileSync(path.join(output,'game-profile-editor-tests.json'),JSON.stringify(result,null,2),{flag:'wx'})}console.log(JSON.stringify(result,null,2));
