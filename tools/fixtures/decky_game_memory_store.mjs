// Execute the complete unchanged original GameCustom store with a memory-only fs.
// Import allowlist prevents real files, native calls, subprocesses or hardware.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'../..');
const require=createRequire(path.join(root,'package.json'));
const {transformSync}=require('esbuild');
const ts=require('typescript');
function load(text,modules={}){const module={exports:{}};vm.runInNewContext(transformSync(text,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,console,queueMicrotask,setTimeout:()=>{throw new Error('No timers/hardware retries allowed in store fixture');},require:name=>{assert.ok(Object.hasOwn(modules,name),'Unmocked import: '+name);return modules[name];}});return module.exports;}
function functions(relative,names){const source=fs.readFileSync(path.join(root,relative),'utf8'),tree=ts.createSourceFile(relative,source,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);const nodes=tree.statements.filter(node=>ts.isFunctionDeclaration(node)&&names.includes(node.name?.text));assert.equal(nodes.length,names.length);return nodes.map(node=>node.getText(tree)).join('\n');}
const {snapshotSettingsData}=load(fs.readFileSync(path.join(root,'src/bridge/settingsSnapshot.ts'),'utf8'),{vue:{toRaw:value=>value}});
const mergeSource=functions('src/bridge/settingsRepository.ts',['isObject','mergeSettings','mergeSettingsSnapshots']);
const {mergeSettings}=load("import {snapshotSettingsData} from './settingsSnapshot';\n"+mergeSource,{'./settingsSnapshot':{snapshotSettingsData}});
const storeSource=fs.readFileSync(path.join(root,'src/bridge/performanceSchedule.ts'),'utf8');
const lockSource=fs.readFileSync(path.join(root,'src/bridge/quickActionLock.ts'),'utf8');
const {gamePolicyKey}=load(fs.readFileSync(path.join(root,'src/bridge/gamePolicyHysteresis.ts'),'utf8'));
const {detectedGameName}=load(functions('src/bridge/gamedetect.ts',['stripLaunchModeSuffix','cleanGameTitle','detectedGameName']));
const gameDisplayName=game=>detectedGameName(game)||game?.name||gamePolicyKey(game)||'当前游戏';
const main='C:\\SOFT\\YeMan\\PowerControl\\game-custom.json';
const backup=main+'.bak';
const base={cpuPreset:'balanced',coreMode:'big',tdpMax:20,fpsTarget:60,cpuTarget:'none',tdpStrategy:'none'};
const profile=(tdp=20)=>({displayName:'Memory fixture',enabled:true,ac:{...base,tdpMax:tdp},dc:{...base,tdpMax:10},acMode:'balanced',dcMode:'eco',padPersona:'follow',gyroOverride:'follow'});
const seed=()=>({version:1,entries:{'game.exe':profile(),'other.exe':profile(15)},rtss:{'other.exe':{enabled:true,acFps:50,dcFps:30}}});
const blocked=name=>()=>{throw new Error('Unexpected original owner call: '+name);};
const blockedModule=name=>new Proxy({}, {get:(_,key)=>blocked(name+'.'+String(key))});
function fixture(initial=seed()){
  const documents=new Map([[main,JSON.stringify(initial)]]),calls=[];
  let failMain=0,waitMain=null,notifications=0;
  const fakeFs={
    exists:async target=>{calls.push(['exists',target]);return documents.has(target);},
    readTextFile:async target=>{calls.push(['read',target]);if(!documents.has(target))throw new Error('Memory document absent');return documents.get(target);},
    writeTextFileAtomic:async(target,text)=>{calls.push(['write',target]);if(target===main&&failMain>0){failMain--;throw new Error('injected memory-only write failure');}if(target===main&&waitMain){const waiting=waitMain;waitMain=null;await waiting;}documents.set(target,text);},
  };
  const frameModel=load(fs.readFileSync(path.join(root,'src/bridge/frameRateModel.ts'),'utf8'));
  const modules={'./frameRateLimits':{...frameModel,ensureGlobalFrameRates:async()=>frameModel.frameRatePair({})},'./api':{fs:fakeFs,powerLifecycle:blockedModule('powerLifecycle')},'./settingsRepository':{mergeSettings,readSettingsSection:blocked('legacy settings read'),replaceSettingsSection:blocked('legacy settings replace'),getSettingsGeneration:blocked('settings generation'),assertSettingsGeneration:blocked('settings assert'),saveSettingsSection:blocked('settings save')},'./gamePolicyTarget':blockedModule('policyGame'),'./gamedetect':blockedModule('gameDetect'),'./yeman':blockedModule('hardware'),'./cpuProfiles':blockedModule('cpuProfiles'),'./autofloat':blockedModule('autofloat')};
  // Test-only access to the ORIGINAL pure normalizer. No production export/edit.
  const store=load(storeSource+'\nexport function normalizeGameMemoryView(value){return normalizeGameCustom(value);}',modules);
  store.onGameCustomConfigChanged(()=>{notifications++;});
  return {store,documents,calls,view:()=>store.normalizeGameMemoryView(JSON.parse(documents.get(main))),lock:load(lockSource),raw:()=>JSON.parse(documents.get(main)),failNext:()=>{failMain++;},holdNext:promise=>{waitMain=promise;},stats:()=>({fakeReads:calls.filter(x=>x[0]==='read').length,fakeWrites:calls.filter(x=>x[0]==='write').length,notifications})};
}

export {root,load,functions,fixture,base,profile,seed,gamePolicyKey,gameDisplayName,detectedGameName,main,backup};
