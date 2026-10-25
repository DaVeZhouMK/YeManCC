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
    console,structuredClone,performance,Date,setTimeout,clearTimeout,...globals});
  return module.exports;
}
export const models=evaluatePerformanceModule(fs.readFileSync('src/bridge/performanceControls.ts','utf8'));
export const frames=evaluatePerformanceModule(fs.readFileSync('src/bridge/frameRateModel.ts','utf8'));
export const persona=evaluatePerformanceModule(fs.readFileSync('src/bridge/controllerPersona.ts','utf8'));
const clone=x=>structuredClone(x);
export function createPerformanceMemoryFixture({side='ac',mode='manual',dedicated=false,float=false,configured=true,legacy=false,gameActive=false,source=file=>fs.readFileSync(file,'utf8')}={}) {
  const controls={version:1,ac:{...models.defaultContinuousBank('ac'),mode},dc:{...models.defaultContinuousBank('dc'),mode}};
  let settings={performanceSchedule:{version:2,configured,enabled:true,...(legacy?{enabled:false}:{controls})},rtss:{frameRates:frames.frameRatePair({})}};
  let game=dedicated||gameActive?{pid:22,processCreated:'created-22',name:'game.exe',path:'C:/Games/game.exe'}:null;
  const custom={version:1,entries:dedicated?{'game.exe':{displayName:'Game',enabled:true,controls:clone(controls)}}:{},rtss:{}};
  const customPath='C:\\SOFT\\YeMan\\PowerControl\\game-custom.json'.replace(/\\\\/g,'\\');
  const files=new Map([[customPath,JSON.stringify(custom)]]),effects=[],hardwareWrites=[];
  let floating=float,ownerLease=null,ownerSide=null,ownerWatts=0,activeScheme='test-yeman',switchFailure=false;
  const gameListeners=new Set();
  const sourceMode={value:side};
  const repo={getSettingsGeneration:()=>1,assertSettingsGeneration:g=>assert.equal(g,1),mergeSettings:(a,b)=>({...a,...b}),
    readSettingsSection:async key=>clone(settings[key]||{}),
    saveSettingsSection:async(key,value)=>{effects.push('settings-save');settings[key]=clone(value)},
    replaceSettingsSection:async(key,value)=>{effects.push('settings-save');settings[key]=clone(value)},
    updateSettings:async mutate=>{const next=clone(settings);mutate(next);effects.push('settings-save');settings=next}};
  const targets={getPolicyGame:()=>game,detectPolicyGame:async()=>game,gamePolicyKey:g=>g.name.toLowerCase(),isPolicyTargetCurrent:t=>!!game&&game.pid===t.pid&&game.processCreated===t.processCreated};
  const yeman={detectPerformanceTemplate:async()=>'portable',detectPowerMode:async()=>sourceMode.value,detectPowerModeReliable:async()=>sourceMode.value,
    getTdpHardwareMinimum:()=>2,getTdpHardwareMaximum:()=>150,getTdpExecutionCapability:async()=>({selected:'offline-fixture'}),
    getActiveScheme:async()=>activeScheme,PW:{YEMAN:'test-yeman',WIN_BAL:'test-balanced'},
    switchScheme:async key=>{assert.equal(key,'bal');effects.push('windows-balanced');if(switchFailure)throw Error('overlay rejected');activeScheme='test-balanced';hardwareWrites.push({kind:'balanced'});},ensureRememberedYemanSchemeActive:async()=>effects.push('ensure-scheme'),
    setActiveScheme:async guid=>{effects.push('set-active');hardwareWrites.push({kind:'active-scheme',guid})},detectCoreArchitecture:async()=>({heterogeneous:true}),
    setCoreModeAc:async mode=>{effects.push('core-ac');hardwareWrites.push({kind:'core',side:'ac',mode})},setCoreModeDc:async mode=>{effects.push('core-dc');hardwareWrites.push({kind:'core',side:'dc',mode})},
    setTdp:async(s,w,opts)=>{effects.push('tdp-'+s);hardwareWrites.push({kind:'tdp',side:s,watts:w});opts.onAccepted?.(w);return true},
    applyManualPowerParams:async p=>{effects.push('cpu-'+p.sides[0]);hardwareWrites.push({kind:'cpu',params:clone(p)});return true},setRtssLimit:async()=>effects.push('rtss'),readRtssLimit:async()=>120,rebuildYemanScheme:async()=>effects.push('rebuild')};
  const schedule=evaluatePerformanceModule(source('src/bridge/performanceSchedule.ts'),{
    './controllerPersona':persona,'./performanceControls':models,'./powerSource':{powerSourceMode:sourceMode},
    './gameCorePolicy':{detectGameCorePolicy:async()=>({detected:true,heterogeneous:true,smtAvailable:true}),applyGameCorePolicy:async()=>({ok:true,applied:true})},
    './gamePolicyTarget':targets,'./gamedetect':{detectGame:async()=>game},'./settingsRepository':repo,'./yeman':yeman,'./cpuProfiles':{loadCpuProfiles:async()=>({profiles:{}})},
    './api':{powerLifecycle:{get:async()=>({phase:'ready',hardwareWritesAllowed:true})},fs:{exists:async p=>files.has(p),readTextFile:async p=>files.get(p),writeTextFileAtomic:async(p,v)=>{effects.push('file-save');files.set(p,v)},writeTextFile:async(p,v)=>{effects.push('file-save');files.set(p,v)}}},
    './frameRateLimits':{...frames,ensureGlobalFrameRates:async()=>clone(settings.rtss.frameRates),applyIndependentFrameRates:async()=>{effects.push('fps');return true},publishFrameRatesChanged(){}},
    './autofloat':{getFloatInfo:()=>({enabled:floating,tdpApplied:ownerWatts}),disableFloat:async()=>{effects.push('stop-float');floating=false;ownerLease=null},updateContinuousFloatCoreRules:()=>effects.push('update-core-rules'),holdContinuousFloatAfterCoreChange:()=>effects.push('hold-core'),enableContinuousFloat:async(_bank,side,_target,current)=>{effects.push('start-float');floating=true;ownerLease=current;ownerSide=side;ownerWatts=_bank.tdp},cancelPendingRtssSync(){},setFloatRtssLinked(){}}
  });
  const powerListeners=new Set(),tasks=new Map();
  const runtime=evaluatePerformanceModule(source('src/bridge/gamePolicyRuntime.ts'),{
    'vue':{watch:(_ref,callback)=>{powerListeners.add(callback);return()=>powerListeners.delete(callback)}},
    './powerSource':{powerSourceMode:sourceMode},'./yeman':yeman,'./performanceSchedule':schedule,
    './autofloat':{setFloatRtssLinked(){}},
    './gamePolicyTarget':{...targets,isPolicyGameInitialized:()=>true,subscribePolicyGameStatus:callback=>{gameListeners.add(callback);callback(game);return()=>gameListeners.delete(callback)}},
    './frameRateLimits':{...frames,loadGlobalFrameRates:async()=>clone(settings.rtss.frameRates),onFrameRatesChanged:()=>()=>{},applyIndependentFrameRates:async()=>true},
    './api':{powerLifecycle:{get:async()=>({phase:'ready',hardwareWritesAllowed:true})}},
    '@/scheduler':{registerScheduledTask:(name,_ms,run)=>{tasks.set(name,run);return()=>tasks.delete(name)}},
    './quickActionLock':{isQuickActionBusy:()=>false},
    './gameCorePolicy':{detectGameCorePolicy:async()=>({detected:true,heterogeneous:false,smtAvailable:false}),clearGameCorePolicy:async()=>true,applyGameCorePolicy:async()=>({ok:true,applied:true})}
  });
  return {schedule,runtime,effects,hardwareWrites,sourceMode,powerListeners,tasks,get settings(){return settings},get floating(){return floating},get game(){return game},get ownerLease(){return ownerLease},get ownerSide(){return ownerSide},
    setSwitchFailure:value=>{switchFailure=value},setGame:next=>{game=next;for(const callback of gameListeners)callback(game)},
    setEntry:async entry=>{const value=await schedule.loadGameCustomConfig();if(entry)value.entries['game.exe']=entry;else delete value.entries['game.exe'];await schedule.saveGameCustomConfig(value)}
  };
}
