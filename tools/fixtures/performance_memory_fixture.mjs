// Actual scheduler/resident modules; all filesystem, IPC and hardware boundaries are memory-only.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const require=createRequire(path.resolve('package.json'));
const {transformSync}=require('esbuild');
export function evaluatePerformanceModule(text, imports={}, globals={}, loader='ts') {
  const module={exports:{}};
  vm.runInNewContext(transformSync(text,{loader,format:'cjs',jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment'}).code,{module,exports:module.exports,
    require:name=>{ if(name in imports)return imports[name];throw Error('Unexpected import '+name); },
    console,structuredClone,TextEncoder,TextDecoder,queueMicrotask,performance,Date,setTimeout,clearTimeout,...globals});
  return module.exports;
}
export const models=evaluatePerformanceModule(fs.readFileSync('src/bridge/performanceControls.ts','utf8'));
export const frames=evaluatePerformanceModule(fs.readFileSync('src/bridge/frameRateModel.ts','utf8'));
export const persona=evaluatePerformanceModule(fs.readFileSync('src/bridge/controllerPersona.ts','utf8'));
const clone=x=>structuredClone(x);
export function createPerformanceMemoryFixture({side='ac',mode='manual',dedicated=false,float=false,configured=true,legacy=false,gameActive=false,ports={},source=file=>fs.readFileSync(file,'utf8')}={}) {
  const controls={version:1,ac:{...models.defaultContinuousBank('ac'),mode},dc:{...models.defaultContinuousBank('dc'),mode}};
  let settings={performanceSchedule:{version:2,configured,enabled:true,...(legacy?{enabled:false}:{controls})},rtss:{frameRates:frames.frameRatePair({})}};
  let game=dedicated||gameActive?{pid:22,processCreated:'created-22',name:'game.exe',path:'C:/Games/game.exe'}:null;
  const custom={version:1,entries:dedicated?{'game.exe':{displayName:'Game',enabled:true,controls:clone(controls)}}:{},rtss:{}};
  const customPath='C:\\SOFT\\YeMan\\PowerControl\\game-custom.json'.replace(/\\\\/g,'\\');
  const files=new Map([[customPath,JSON.stringify(custom)]]),effects=[],hardwareWrites=[],schemeAttempts=[],corePolicyCalls=[];
  let floating=float,ownerLease=null,ownerSide=null,ownerWatts=0,activeScheme='test-yeman',switchFailure=false;
  const gameListeners=new Set();
  const sourceMode={value:side};
  const snapshots=evaluatePerformanceModule(source('src/bridge/settingsSnapshot.ts'),{'vue':require('vue')});
  const sectionRevisions=new Map();
  const repo={SETTINGS_FILE:customPath.replace('game-custom.json','yeman-settings.json'),loadSettings:async()=>clone(settings),getSettingsGeneration:()=>ports.generation??1,
    getSettingsSectionRevision:key=>{const json=JSON.stringify(settings[key]??{}),previous=sectionRevisions.get(key);if(!previous||previous.json!==json)sectionRevisions.set(key,{json,revision:(previous?.revision||0)+1});return sectionRevisions.get(key).revision},
    assertSettingsGeneration:g=>assert.equal(g,ports.generation??1),mergeSettings:(a,b)=>({...a,...b}),
    readSettingsSection:async key=>clone(settings[key]||{}),
    saveSettingsSection:async(key,value)=>{effects.push('settings-save');settings[key]=clone(value)},
    replaceSettingsSection:async(key,value)=>{effects.push('settings-save');settings[key]=clone(value)},
    updateSettings:async mutate=>{const next=clone(settings);mutate(next);await ports.beforeSettingsCommit?.(clone(next));effects.push('settings-save');settings=next}};
  const currentGame=t=>!!game&&game.pid===t.pid&&game.processCreated===t.processCreated&&game.path.toLowerCase()===String(t.path||'').toLowerCase()&&game.identityGeneration===t.identityGeneration;
  const gameDetection={detectGame:async()=>game,isGameStatusCurrent:()=>true,isCurrentGameTarget:currentGame,validateGameTarget:async t=>currentGame(t),subscribeGameTargetReadiness:()=>()=>{}};
  const targets={getPolicyGame:()=>game,detectPolicyGame:async()=>game,gamePolicyKey:g=>g.name.toLowerCase(),isPolicyTargetCurrent:t=>!!game&&game.pid===t.pid&&game.processCreated===t.processCreated};
  const yeman={detectPerformanceTemplate:async()=>'portable',detectPowerMode:async()=>sourceMode.value,detectPowerModeReliable:async()=>sourceMode.value,
    getTdpHardwareMinimum:()=>2,getTdpHardwareMaximum:()=>150,getTdpExecutionCapability:async()=>({selected:'offline-fixture'}),
    getActiveScheme:async()=>activeScheme,ensureSelectedPowerScheme:async(key,current)=>{
      assert(['yeman','besteff','bal','bestperf'].includes(key));
      const attempt={key,accepted:false};schemeAttempts.push(attempt);
      await ports.beforeSelectedScheme?.(key);
      if(current&&!await current())throw Error('memory scheme terminal: stale owner');
      if(switchFailure)throw Error('memory scheme terminal: switch rejected');
      attempt.accepted=true;await ports.onSelectedScheme?.(key);const desired='test-'+key;
      if(activeScheme!==desired){activeScheme=desired;effects.push('selected-scheme');hardwareWrites.push({kind:'selected-scheme',key});}
    },PW:{YEMAN:'test-yeman',WIN_BAL:'test-balanced'},
    switchScheme:async key=>{assert.equal(key,'bal');effects.push('windows-balanced');if(switchFailure)throw Error('overlay rejected');activeScheme='test-balanced';hardwareWrites.push({kind:'balanced'});},ensureRememberedYemanSchemeActive:async()=>effects.push('ensure-scheme'),
    setActiveScheme:async guid=>{activeScheme=guid;effects.push('set-active');hardwareWrites.push({kind:'active-scheme',guid})},detectCoreArchitecture:async()=>({heterogeneous:true}),
    setCoreModeAc:async mode=>{effects.push('core-ac');hardwareWrites.push({kind:'core',side:'ac',mode})},setCoreModeDc:async mode=>{effects.push('core-dc');hardwareWrites.push({kind:'core',side:'dc',mode})},
    setTdp:async(s,w,opts)=>{await ports.beforeTdpWrite?.(s,w);if(opts.isCurrent&&!opts.isCurrent())return false;effects.push('tdp-'+s);hardwareWrites.push({kind:'tdp',side:s,watts:w});await ports.afterTdpWrite?.(s,w);if(opts.isCurrent&&!opts.isCurrent())return false;opts.onAccepted?.(w);ports.onTdpAccepted?.(s,w);return true},
    applyManualPowerParams:async p=>{await ports.beforeCpuWrite?.(clone(p));effects.push('cpu-'+p.sides[0]);hardwareWrites.push({kind:'cpu',params:clone(p)});return true},setRtssLimit:async()=>effects.push('rtss'),readRtssLimit:async()=>120,rebuildYemanScheme:async()=>effects.push('rebuild')};
  const schedule=evaluatePerformanceModule(source('src/bridge/performanceSchedule.ts'),{
    './controllerPersona':persona,'./performanceControls':models,'./powerSource':{powerSourceMode:sourceMode},
    './gameCorePolicy':{detectGameCorePolicy:async()=>({detected:ports.topologyDetected??true,heterogeneous:ports.heterogeneous??true,smtAvailable:ports.smtAvailable??true}),applyGameCorePolicy:async(...args)=>{corePolicyCalls.push({kind:'apply',args:clone(args)});return {ok:true,applied:true}},clearGameCorePolicy:async target=>{corePolicyCalls.push({kind:'clear',target:clone(target)});return true}},
    './gamePolicyTarget':targets,'./gamedetect':gameDetection,'./settingsSnapshot':snapshots,'./settingsRepository':repo,'./yeman':yeman,'./cpuProfiles':{loadCpuProfiles:async()=>({profiles:{}})},
    './api':{powerLifecycle:{get:async()=>({phase:'ready',hardwareWritesAllowed:true})},fs:{exists:async p=>files.has(p),readTextFile:async p=>{if(ports.customReadFailure)throw Error('fixture-custom-read-denied');return files.get(p)},writeTextFileAtomic:async(p,v)=>{effects.push('file-save');files.set(p,v)},writeTextFile:async(p,v)=>{effects.push('file-save');files.set(p,v)}}},
    './frameRateLimits':{...frames,ensureGlobalFrameRates:async()=>clone(settings.rtss.frameRates),applyIndependentFrameRates:async()=>{effects.push('fps');return true},publishFrameRatesChanged(){}},
    './autofloat':{pauseContinuousFloatForTdpEdit:async()=>{if(!ownerLease)return null;const prior=ownerLease;return {current:()=>ownerLease===prior,accept:(_requested,accepted)=>{ownerWatts=accepted??0},release(){}}},getFloatInfo:()=>({enabled:floating,tdpApplied:ownerWatts}),disableFloat:async()=>{effects.push('stop-float');floating=false;ownerLease=null},updateContinuousFloatCoreRules:()=>effects.push('update-core-rules'),holdContinuousFloatAfterCoreChange:()=>effects.push('hold-core'),enableContinuousFloat:async(_bank,side,_target,current)=>{effects.push('start-float');floating=true;ownerLease=current;ownerSide=side;ownerWatts=_bank.tdp},cancelPendingRtssSync(){},setFloatRtssLinked(){}}
  });
  const powerListeners=new Set(),tasks=new Map();
  const runtime=evaluatePerformanceModule(source('src/bridge/gamePolicyRuntime.ts'),{
    'vue':{watch:(_ref,callback)=>{powerListeners.add(callback);return()=>powerListeners.delete(callback)}},
    './performanceControls':models,'./powerSource':{powerSourceMode:sourceMode},'./yeman':yeman,'./performanceSchedule':schedule,'./gamedetect':gameDetection,
    './autofloat':{setFloatRtssLinked(){}},
    './gamePolicyTarget':{...targets,isPolicyGameInitialized:()=>true,subscribePolicyGameStatus:callback=>{gameListeners.add(callback);callback(game);return()=>gameListeners.delete(callback)}},
    './frameRateLimits':{...frames,loadGlobalFrameRates:async()=>{await ports.beforeRuntimeFrames?.();return clone(settings.rtss.frameRates)},onFrameRatesChanged:()=>()=>{},applyIndependentFrameRates:async()=>true},
    './api':{powerLifecycle:{get:async()=>({phase:'ready',hardwareWritesAllowed:true})}},
    '@/scheduler':{registerScheduledTask:(name,_ms,run)=>{tasks.set(name,run);return()=>tasks.delete(name)}},
    './quickActionLock':{isQuickActionBusy:()=>ports.isQuickActionBusy?.()??false},
    './gameCorePolicy':{detectGameCorePolicy:async()=>({detected:true,heterogeneous:false,smtAvailable:ports.smtAvailable??false}),clearGameCorePolicy:async()=>{corePolicyCalls.push({kind:'clear'});return true},applyGameCorePolicy:async(...args)=>{corePolicyCalls.push({kind:'apply',args:clone(args)});return {ok:true,applied:true}}}
  });
  return {schedule,runtime,powerTerminals:yeman,settingsRepository:repo,effects,hardwareWrites,schemeAttempts,corePolicyCalls,sourceMode,powerListeners,tasks,get activeScheme(){return activeScheme},get settings(){return settings},get floating(){return floating},get game(){return game},get ownerLease(){return ownerLease},get ownerSide(){return ownerSide},
    setSwitchFailure:value=>{switchFailure=value},setGame:next=>{game=next;for(const callback of gameListeners)callback(game)},
    setEntry:async entry=>{const value=await schedule.loadGameCustomConfig();if(entry)value.entries['game.exe']=entry;else delete value.entries['game.exe'];await schedule.saveGameCustomConfig(value)}
  };
}
