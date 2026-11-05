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
// Process-local compilation only. Callers still read source before load().
// The captured compiler and fixed ts/cjs options stay unchanged. Exact source
// text keys cannot hit a different fault input. No module, VM, globals, require,
// business instance or result is cached; every load below executes a fresh VM.
const compiledTypeScript=new Map();
function load(text,modules={},globals={}){
 let code;
 if(typeof text==='string'&&compiledTypeScript.has(text))code=compiledTypeScript.get(text);
 else{code=transformSync(text,{loader:'ts',format:'cjs'}).code;if(typeof text==='string')compiledTypeScript.set(text,code);}
 const module={exports:{}};vm.runInNewContext(code,{module,exports:module.exports,structuredClone,console,queueMicrotask,setTimeout:()=>{throw new Error('No timers/hardware retries allowed in store fixture');},...globals,require:name=>{if(name==='./performanceModel'||name==='../../decky-plugin/src/performanceModel')return performanceModelModule;if(name==='./screenTouchpadDefaults')return screenDefaultsModule;if(name==='./controllerPersona')return personaModule;if(name==='./virtualTargetPolicy')return policyModule;if(name==='./virtualTargetPolicyState'&&!Object.hasOwn(modules,name))return {currentVirtualTargetPolicy:()=>null};assert.ok(Object.hasOwn(modules,name),'Unmocked import: '+name);return modules[name];}});return module.exports;}
const screenDefaultsModule=load(fs.readFileSync(path.join(root,'src/bridge/screenTouchpadDefaults.ts'),'utf8'));
const performanceModelModule=load(fs.readFileSync(path.join(root,'decky-plugin/src/performanceModel.ts'),'utf8'));
const performanceControlsModule=load(fs.readFileSync(path.join(root,'src/bridge/performanceControls.ts'),'utf8'));
const personaModule=load(fs.readFileSync(path.join(root,'src/bridge/controllerPersona.ts'),'utf8'));
const policyModule=load(fs.readFileSync(path.join(root,'src/bridge/virtualTargetPolicy.ts'),'utf8'));
function functions(relative,names){const source=fs.readFileSync(path.join(root,relative),'utf8'),tree=ts.createSourceFile(relative,source,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);const nodes=tree.statements.filter(node=>ts.isFunctionDeclaration(node)&&names.includes(node.name?.text));assert.equal(nodes.length,names.length);return nodes.map(node=>node.getText(tree)).join('\n');}
const {snapshotSettingsData}=load(fs.readFileSync(path.join(root,'src/bridge/settingsSnapshot.ts'),'utf8'),{vue:{toRaw:value=>value}});
const mergeSource=functions('src/bridge/settingsRepository.ts',['isObject','mergeSettings','mergeSettingsSnapshots']);
const {mergeSettings}=load("import {snapshotSettingsData} from './settingsSnapshot';\n"+mergeSource,{'./settingsSnapshot':{snapshotSettingsData}});
const storeSource=fs.readFileSync(path.join(root,'src/bridge/performanceSchedule.ts'),'utf8');
const lockSource=fs.readFileSync(path.join(root,'src/bridge/quickActionLock.ts'),'utf8');
const {gamePolicyKey}=load(fs.readFileSync(path.join(root,'src/bridge/gamePolicyHysteresis.ts'),'utf8'));
// Extract the immutable title-policy closure with its production functions.
const titleSource=fs.readFileSync(path.join(root,'src/bridge/gamedetect.ts'),'utf8');
const titleTree=ts.createSourceFile('gamedetect.ts',titleSource,ts.ScriptTarget.Latest,true,ts.ScriptKind.TS);
const titleConstants=['launchModeSuffixes','copyrightSuffix'].map(name=>{
 const node=titleTree.statements.find(node=>ts.isVariableStatement(node)&&node.declarationList.declarations.some(decl=>ts.isIdentifier(decl.name)&&decl.name.text===name));
 assert(node,'Missing actual title policy declaration: '+name);return node.getText(titleTree);
}).join(String.fromCharCode(10));
const {detectedGameName}=load('const gameTitlePolicy='+fs.readFileSync(path.join(root,'PowerControl/GameTitlePolicy.json'),'utf8')+';'+String.fromCharCode(10)+titleConstants+String.fromCharCode(10)+functions('src/bridge/gamedetect.ts',['stripLaunchModeSuffix','cleanGameTitle','detectedGameName']));
const gameDisplayName=game=>detectedGameName(game)||game?.name||gamePolicyKey(game)||'当前游戏';
const main='C:\\SOFT\\YeMan\\PowerControl\\game-custom.json';
const backup=main+'.bak';
const base={cpuPreset:'balanced',coreMode:'big',tdpMax:20,fpsTarget:60,cpuTarget:'none',tdpStrategy:'none'};
const profile=(tdp=20)=>({displayName:'Memory fixture',enabled:true,ac:{...base,tdpMax:tdp},dc:{...base,tdpMax:10},acMode:'balanced',dcMode:'eco',padPersona:'follow',gyroOverride:'follow'});
const seed=()=>({version:1,entries:{'game.exe':profile(),'other.exe':profile(15)},rtss:{'other.exe':{enabled:true,acFps:50,dcFps:30}}});
const blocked=name=>()=>{throw new Error('Unexpected original owner call: '+name);};
const blockedModule=name=>new Proxy({}, {get:(_,key)=>blocked(name+'.'+String(key))});
function fixture(initial=seed(),ownerContext={}){
  const documents=new Map([[main,JSON.stringify(initial)]]),calls=[];
  let failMain=0,waitMain=null,notifications=0;
  const generation=ownerContext.generation??(()=>1),deadlineTimers=new Set();
  const deadlines={TextEncoder,setTimeout:(run,delay)=>{assert.ok([2000,8000].includes(delay),"Only original bounded config I/O deadlines belong to this store fixture");const timer=setTimeout(()=>{deadlineTimers.delete(timer);run();},delay);deadlineTimers.add(timer);return timer;},clearTimeout:timer=>{clearTimeout(timer);deadlineTimers.delete(timer);}};
  // The production profile store now rechecks observed game readiness and full identity.
  const sameTarget=load(functions('src/bridge/gamedetect.ts',['sameIdentity'])+'\nmodule.exports={sameIdentity};').sameIdentity;
  const gameCurrent=ownerContext.gameCurrent??(()=>true);
  const fakeFs={
    exists:async target=>{calls.push(['exists',target]);return documents.has(target);},
    readTextFile:async target=>{calls.push(['read',target]);if(!documents.has(target))throw new Error('Memory document absent');return documents.get(target);},
    writeTextFileAtomic:async(target,text)=>{calls.push(['write',target]);if(target===main&&failMain>0){failMain--;throw new Error('injected memory-only write failure');}if(target===main&&waitMain){const waiting=waitMain;waitMain=null;await waiting;}documents.set(target,text);if(ownerContext.afterWrite)await ownerContext.afterWrite(target,text);},
  };
  const frameModel=load(fs.readFileSync(path.join(root,'src/bridge/frameRateModel.ts'),'utf8'));
  const modules={'./settingsSnapshot':{snapshotSettingsData},'./powerSource':{powerSourceMode:{value:'ac'}},'./performanceControls':performanceControlsModule,'./gameCorePolicy':blockedModule('gameCorePolicy'),'./frameRateLimits':{...frameModel,ensureGlobalFrameRates:async()=>frameModel.frameRatePair({})},'./api':{fs:fakeFs,powerLifecycle:blockedModule('powerLifecycle')},'./settingsRepository':{SETTINGS_FILE:main.replace('game-custom.json','yeman-settings.json'),loadSettings:ownerContext.loadSettings?async()=>{await ownerContext.loadSettings();return {};}:async()=>({}),mergeSettings,readSettingsSection:blocked('legacy settings read'),replaceSettingsSection:blocked('legacy settings replace'),getSettingsGeneration:generation,assertSettingsGeneration:expected=>{if(expected!==generation())throw Error('settings generation changed');},saveSettingsSection:blocked('settings save')},'./gamePolicyTarget':ownerContext.game?{getPolicyGame:ownerContext.game,gamePolicyKey}:blockedModule('policyGame'),'./gamedetect':ownerContext.game?{isGameStatusCurrent:gameCurrent,isCurrentGameTarget:target=>gameCurrent()&&!!ownerContext.game()&&sameTarget(target,ownerContext.game())}:blockedModule('gameDetect'),'./yeman':blockedModule('hardware'),'./cpuProfiles':blockedModule('cpuProfiles'),'./autofloat':blockedModule('autofloat')};
  // Test-only access to the ORIGINAL pure normalizer. No production export/edit.
  const store=load(storeSource+'\nexport function normalizeGameMemoryView(value){return normalizeGameCustom(value);}',modules,deadlines);
  store.onGameCustomConfigChanged(()=>{notifications++;});
  return {store,documents,calls,view:()=>store.normalizeGameMemoryView(JSON.parse(documents.get(main))),lock:load(lockSource),raw:()=>JSON.parse(documents.get(main)),failNext:()=>{failMain++;},holdNext:promise=>{waitMain=promise;},stats:()=>({pendingDeadlineTimers:deadlineTimers.size,fakeReads:calls.filter(x=>x[0]==='read').length,fakeWrites:calls.filter(x=>x[0]==='write').length,notifications})};
}

export {root,load,functions,fixture,base,profile,seed,gamePolicyKey,gameDisplayName,detectedGameName,main,backup};

export {performanceModelModule,performanceControlsModule,mergeSettings};

