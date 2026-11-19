// Explicit scoped mode of boot_modes_selftest; production function slices,
// memory Windows terminals only. No real powercfg, installed UI or devices.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
const require=createRequire(path.resolve('package.json'));
const ts=require('typescript'),{transformSync}=require('esbuild');
const yemanFile='src/bridge/yeman.ts',viewFile='src/views/SleepGuardView.vue';
const names=['PW','SLEEP_TIMEOUT_POWER','SLEEP_TIMEOUT_FIELDS','readPlanPowerIndex','timeoutSecondsToMinutes','readSleepTimeouts','getActiveScheme','reactivateCurrentScheme','setSleepTimeout'];
function select(text,wanted){
 const ast=ts.createSourceFile('actual.ts',text,ts.ScriptTarget.Latest,true),found=new Map();
 for(const n of ast.statements){
  if(ts.isFunctionDeclaration(n)&&n.name&&wanted.includes(n.name.text))found.set(n.name.text,n.getText(ast).replace(/^export\s+/,''));
  if(ts.isVariableStatement(n))for(const d of n.declarationList.declarations)if(ts.isIdentifier(d.name)&&wanted.includes(d.name.text))found.set(d.name.text,'const '+d.getText(ast)+';');
 }
 for(const name of wanted)assert(found.has(name),'Actual declaration missing: '+name);
 return [...found.values()].join('\n')+'\nexport {'+wanted.join(',')+'};';
}
function evaluate(text,globals){const module={exports:{}};vm.runInNewContext(transformSync(text,{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,console,...globals},{timeout:1000});return module.exports;}
const publicPowerBodies=['captureStaticWakeAdmission','captureSleepPowerWrite','requireSleepPowerAdmission','runSleepPowerCommand'];
const readyPower=()=>({phase:'ready',known:true,hardwareWritesAllowed:true,generation:41,observedNotificationSequence:8,notificationPending:false});
function publicPowerGlobals(state){
 state.power=readyPower();
 return {getSharedPowerLifecycle:()=>state.power,isSharedPowerLifecycleAdmitted:value=>value?.known===true&&value.phase==='ready'&&value.hardwareWritesAllowed&&!value.notificationPending};
}
function checkPublicPower(state,admission){
 if(!admission)return; // Historical source controls did not carry admission.
 const current=state.power;
 if(!current?.known||current.notificationPending||current.phase!=='ready'||!current.hardwareWritesAllowed||admission.expectedPowerGeneration!==current.generation||admission.expectedPowerNotificationSequence!==current.observedNotificationSequence)throw Error('Native public power admission retired');
}
let actualOwnership;
function ownershipGlobals(){
 if(!actualOwnership){const built=require('esbuild').buildSync({stdin:{contents:"export {captureBootPreferenceWrite,assertBootPreferenceOwner} from './src/bridge/bootModeGlobalPreferences.ts';",resolveDir:process.cwd(),sourcefile:'owned-sleep-preferences.ts',loader:'ts'},absWorkingDir:process.cwd(),bundle:true,platform:'node',format:'cjs',write:false,metafile:true});
  const module={exports:{}};vm.runInNewContext(built.outputFiles[0].text,{module,exports:module.exports,structuredClone,TextEncoder,TextDecoder,console},{timeout:1000});
  actualOwnership={...module.exports,loadSettings:async()=>{throw Error('Unexpected scoped sleep settings read without owned port');},inputs:Object.keys(built.metafile.inputs).filter(name=>name!=='owned-sleep-preferences.ts')};
 }
 return actualOwnership;
}
const key=(guid,sub,item,ac)=>[guid.toLowerCase(),sub,item,ac?'ACSettingIndex':'DCSettingIndex'].join('|');
const clone=value=>structuredClone(value);
// Independent Windows setting contract: do not seed expected addresses from the product under test.
const expectedFields={screen:{subGroup:'7516b95f-f776-4464-8c53-06167f40cc99',setting:'3c0bc021-c8a8-4e07-a973-6b14cbcb2b7e'},sleep:{subGroup:'238c9fa8-0aad-41ed-83f4-97be242c8f20',setting:'29f6c1db-86da-48c5-9fdb-f2b67b1f44da'},hibernate:{subGroup:'238c9fa8-0aad-41ed-83f4-97be242c8f20',setting:'9d7815a6-7ee4-497e-8888-515a05f02364'}};
function fixture(source,preferences={updateSettings:async mutator=>mutator({extensions:{}})}){
 const state={calls:[],values:new Map(),live:new Map(),active:'',writeExit:0,activateExit:0,readbackWrong:false,activeUnknown:false,afterWrite:null,beforeActivate:null};
 const registry={read:async(_hive,regPath,name)=>{
  state.calls.push({kind:'read',path:regPath,name});const m=regPath.match(/PowerSchemes\\([^\\]+)\\([^\\]+)\\([^\\]+)$/);assert(m,'Actual registry path');
  state.onBoundary?.('registry-read');
  return state.readbackWrong?999999:state.values.get(key(m[1],m[2],m[3],name==='ACSettingIndex'))??null;
 }};
 const shell={run:async(exe,args,timeout,_observation,admission,bootContext)=>{
  assert.equal(exe,'powercfg');state.calls.push({kind:'shell',args:[...args],timeout,...(admission?{admission:clone(admission)}:{}),...(bootContext?{bootContext:clone(bootContext)}:{})});
  state.onBoundary?.('native-dispatch');if(bootContext)ownershipGlobals().assertBootPreferenceOwner(await preferences.loadSettings(),{origin:'boot-mode',bootContext});
  checkPublicPower(state,admission);
  if(['/setacvalueindex','/setdcvalueindex'].includes(args[0])){
   if(!state.writeExit)state.values.set(key(args[1],args[2],args[3],args[0]==='/setacvalueindex'),Number(args[4]));
   state.afterWrite?.();state.onBoundary?.('shell-return');return {exitCode:state.writeExit,stdout:'',stderr:state.writeExit?'write denied':''};
  }
  if(args[0]==='/setactive'){
   state.beforeActivate?.();assert.equal(args[1],'SCHEME_CURRENT','Never activate a stale captured GUID');
   if(!state.activateExit)state.live=new Map([...state.values].filter(([k])=>k.startsWith(state.active.toLowerCase()+'|')));
   state.onBoundary?.('shell-return');return {exitCode:state.activateExit,stdout:'',stderr:state.activateExit?'apply denied':''};
  }
  if(args[0]==='/getactivescheme')return {exitCode:state.activeUnknown?1:0,stdout:state.activeUnknown?'':'Power Scheme GUID: '+state.active,stderr:''};
  throw Error('Unexpected powercfg operation '+args.join(' '));
 }};
 const invoke=async command=>{assert.equal(command,'power.activeScheme');state.calls.push({kind:'active'});state.onBoundary?.('scheme-read');return state.activeUnknown?'':state.active;};
 const selected=[...names];for(const name of ['rememberSleepTimeoutPreference','rememberSleepPreference'])if(source.includes('function '+name+'('))selected.push(name);
 if(source.includes('function assertPowerWriteOwner('))selected.push('assertPowerWriteOwner');
 if(source.includes('function captureSleepPowerWrite('))selected.push(...publicPowerBodies);
 const api=evaluate(select(source,selected),{registry,shell,invoke,...publicPowerGlobals(state),...ownershipGlobals(),...preferences});state.active=api.PW.YEMAN;
 const other='d18873c2-a73f-4d8a-a174-7cdb56832798';
 for(const guid of [api.PW.YEMAN,other,api.PW.WIN_BAL])for(const spec of Object.values(expectedFields))for(const ac of [true,false])state.values.set(key(guid,spec.subGroup,spec.setting,ac),3600);
 state.live=new Map([...state.values].filter(([k])=>k.startsWith(state.active+'|')));
 const stored=(field,ac,guid=api.PW.YEMAN)=>{const s=expectedFields[field];return state.values.get(key(guid,s.subGroup,s.setting,ac));};
 const live=(field,ac,guid=api.PW.YEMAN)=>{const s=expectedFields[field];return state.live.get(key(guid,s.subGroup,s.setting,ac));};
 return {api,state,stored,live,other};
}
function ui(f){
 const script=fs.readFileSync(viewFile,'utf8').match(/<script setup[^>]*>([\s\S]*?)<\/script>/)[1];
 const busy={value:false},timeoutLoading={value:false},msg={value:''},sleepTimeouts={acScreen:60,dcScreen:60,acSleep:60,dcSleep:60,acHibernate:60,dcHibernate:60};
 const wanted=['refreshSleepTimeouts','onSleepTimeout'];if(script.includes('function showSleepPreferenceResult('))wanted.push('showSleepPreferenceResult');
 const functions=evaluate(select(script,wanted),{busy,timeoutLoading,msg,sleepTimeouts,sleepPreferenceWarnings:{},readSleepTimeouts:f.api.readSleepTimeouts,setSleepTimeout:f.api.setSleepTimeout});
 return {functions,busy,timeoutLoading,msg,sleepTimeouts};
}
export {fixture as createSleepTimeoutOwnerFixture,ui as createSleepTimeoutUiFixture};
export function createSleepPowerOwnerFixture(source,preferences={}){
 const state={calls:[],buttons:{ac:0,dc:0},size:40,enabled:true,enabledKnown:true,battery:true,shellExit:0,failVerb:'',buttonMismatch:false,sizeMismatch:false,hibernateMismatch:false,afterWrite:null};
 const registry={read:async(_hive,path,name)=>{
  state.calls.push({kind:'read',path,name});
  state.onBoundary?.('registry-read');
  if(name==='HiberFileSizePercent')return state.size;
  if(name==='ACSettingIndex'||name==='DCSettingIndex')return state.buttons[name==='ACSettingIndex'?'ac':'dc'];
  throw Error('Unexpected sleep registry read '+name);
 }};
 const shell={run:async(program,args,_timeout,_observation,admission,bootContext)=>{
  assert.equal(program,'powercfg');state.calls.push({kind:'shell',args:clone(args),...(admission?{admission:clone(admission)}:{}),...(bootContext?{bootContext:clone(bootContext)}:{})});
  state.onBoundary?.('native-dispatch');if(bootContext)ownershipGlobals().assertBootPreferenceOwner(await preferences.loadSettings(),{origin:'boot-mode',bootContext});
  checkPublicPower(state,admission);
  if(state.shellExit&&(!state.failVerb||args.join(' ').includes(state.failVerb)))return {exitCode:state.shellExit,stdout:'',stderr:'denied'};
  if(args[0]==='/setacvalueindex'||args[0]==='/setdcvalueindex'){
   assert.equal(args[2],'4f971e89-eebd-4455-a8de-9e59040e7347');assert.equal(args[3],'7648efa3-dd9c-4e3e-b566-50f929386280');
   if(!state.buttonMismatch)state.buttons[args[0]==='/setacvalueindex'?'ac':'dc']=Number(args[4]);
  }else if(args[0]==='/hibernate'){
   if(args[1]==='on'||args[1]==='off'){if(!state.hibernateMismatch)state.enabled=args[1]==='on';}
   else if(args[1]==='/size'){if(!state.sizeMismatch)state.size=Number(args[2]);}
   else assert.equal(args.join(' '),'/hibernate /type full');
  }else if(args[0]==='/setactive')assert.equal(args[1],'SCHEME_CURRENT');
  else throw Error('Unexpected sleep command '+args.join(' '));
  await state.afterWrite?.();state.onBoundary?.('shell-return');return {exitCode:0,stdout:'',stderr:''};
 }};
 const wanted=['PW','POWER_BTN_QUEUE','PW_BTN_SUB','PW_BTN_ITEM','readPlanPowerIndex','readSchemeIndex','setAcValueIndex','setDcValueIndex','setPowerValueIndex','getPowerBtnIdx','setPowerBtnIdx','readHibernateState','setHibernate','readHibernateSize','setHibernateSize'];
 for(const name of ['rememberSleepTimeoutPreference','rememberSleepPreference'])if(source.includes('function '+name+'('))wanted.push(name);
 wanted.push('reactivateCurrentScheme');if(source.includes('function assertPowerWriteOwner('))wanted.push('assertPowerWriteOwner');
 if(source.includes('function captureSleepPowerWrite('))wanted.push(...publicPowerBodies);
 const api=evaluate(select(source,wanted),{registry,shell,hasBatteryDevice:async()=>{state.onBoundary?.('battery-read');return state.battery;},systemHibernate:{get:async()=>{state.calls.push({kind:'hibernate-read'});state.onBoundary?.('hibernate-read');return {enabled:state.enabled,enabledKnown:state.enabledKnown,supported:true,supportedKnown:true,source:'registry'};}},updateSettings:async()=>{throw Error('Unexpected preference write without repository');},...publicPowerGlobals(state),...ownershipGlobals(),...preferences});
 return {api,state};
}
export function createSleepPowerUiFixture(owner){
 const script=fs.readFileSync(viewFile,'utf8').match(/<script setup[^>]*>([\s\S]*?)<\/script>/)[1];
 const ref=value=>({value}),busy=ref(false),msg=ref(''),acBtnIdx=ref(2),dcBtnIdx=ref(2),hibSize=ref(null),hibernateOn=ref(owner.state.enabled),hibernateSupported=ref(true),confirmOff=ref(false),sleepPreferenceWarnings={};
 const refreshHibernateState=async()=>{const result=await owner.api.readHibernateState();hibernateOn.value=result.enabledKnown?result.enabled:null;return hibernateOn.value;};
 const wanted=['showSleepPreferenceResult','onPwrBtn','onHibernate','confirmHibernateOff','onHibSize'];
 const functions=evaluate(select(script,wanted),{busy,msg,acBtnIdx,dcBtnIdx,hibSize,hibernateOn,hibernateSupported,confirmOff,sleepPreferenceWarnings,refreshHibernateState,
  setPowerBtnIdx:owner.api.setPowerBtnIdx,getPowerBtnIdx:owner.api.getPowerBtnIdx,setHibernate:owner.api.setHibernate,setHibernateSize:owner.api.setHibernateSize,readHibernateSize:owner.api.readHibernateSize,
  captureSleepPowerWrite:owner.api.captureSleepPowerWrite,schedulePwrActivate:()=>{},nextTick:async()=>{},document:{querySelector:()=>null},focusGamepadElement:()=>{}});
 return {functions,busy,msg,acBtnIdx,dcBtnIdx,hibSize,hibernateOn,sleepPreferenceWarnings};
}
/** Original boot-sleep case: actual producer, actual setter and repository,
 * original native file lock/writer. No scan/worker/power terminal is invoked. */
export async function runSleepStaticBootPreferences(out,args=[],options={}){
 const native=options.native;assert(native&&typeof native.invoke==='function','Original owned native file writer required');
 const modes=['global','xbox','steam','xbox-steam'],kinds=['apply','absent','stale-input','stale-mode','unrelated','ordinary'];
 const specs=modes.flatMap(mode=>[true,false].flatMap(enabled=>kinds.map(kind=>({mode,enabled,kind,key:mode+'/'+(enabled?'on':'off')+'/'+kind}))));
 for(const mode of modes.filter(mode=>mode!=='global'))for(const enabled of [true,false])specs.push({mode,enabled,kind:'follow',key:mode+'/'+(enabled?'on':'off')+'/follow'});
 for(const mode of modes)specs.push({mode,enabled:true,kind:'invalid',key:mode+'/invalid'});
 const extra=[];for(const mode of modes){for(const enabled of [true,false])extra.push({mode,enabled,kind:'capture-missing',key:mode+'/'+(enabled?'on':'off')+'/capture-missing'});for(const kind of ['stale-input-native','stale-mode-native'])extra.push({mode,enabled:true,kind,key:mode+'/on/'+kind});}
 specs.push(...extra);
 const only=args.filter(arg=>arg.startsWith('--sleep-static-only='));assert(only.length<=1,'Duplicate static sleep selector');
 const supplement=args.includes('--sleep-static-supplement');assert(!supplement||!only.length,'Choose exact static sleep case or supplement');
 const selected=supplement?extra:only.length?specs.filter(s=>s.key===only[0].slice('--sleep-static-only='.length)):specs;assert(selected.length,'Unknown static sleep selection');
 const source=fs.readFileSync(yemanFile,'utf8'),selectedSetter=select(source,['setStaticWakeAutoScanEnabled']);
 fs.writeFileSync(path.join(out,'actual-static-wake-setter.ts'),selectedSetter,{flag:'wx'});
 const {createDiskRuntime}=await import('./diskSettings.mjs');
 const disk=await createDiskRuntime(path.join(out,'sleep-static-disk'),native,{moduleDirectory:path.join(out,'sleep-static-runtime')});
 const initial=await disk.a.repository.loadSettings();Object.assign(disk.a.ports.settings,clone(initial));
 const seed=clone(initial);seed.input.startupObservedBoot='sleep-static-configuration-boot';
 const rows=[];let complete=false;
 try{
  for(const s of selected){
   disk.writeRaw(seed);const a=await disk.moduleAt('case-'+rows.length),q=a.ports,r=a.repository;
   if(s.kind==='capture-missing'){
    const data=clone(seed);data.sleep.staticWakeAutoScanEnabled=s.enabled;disk.writeRaw(data);r.clearSettingsCache();const before=await r.loadSettings();Object.assign(q.settings,clone(before));
    delete q.settings.sleep.staticWakeAutoScanEnabled; // Native guard status does not own this durable preference.
    const captured=await a.boot.captureBootModeSnapshot(),after=disk.read(),reopened=await disk.moduleAt('capture-reopen-'+rows.length),loaded=await reopened.repository.loadSettings();
    rows.push({key:s.key,before,after,reopened:clone(loaded),captured:captured.sleep.config.staticWakeAutoScanEnabled,writes:[],error:null});
    assert.equal(captured.sleep.config.staticWakeAutoScanEnabled,s.enabled,'Guard status omission must not erase durable preference '+s.key);
    assert.deepEqual(after,before);assert.equal(loaded.sleep.staticWakeAutoScanEnabled,s.enabled);continue;
   }
   const input=await r.loadSettings();Object.assign(q.settings,clone(input));
   const doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
   doc.current=s.mode;doc.boot=s.mode;doc.bootEnabled=true;
   for(const scene of Object.values(doc.scenes))delete scene.overrides.sleep;
   const rootValue=s.kind==='ordinary'?s.enabled:!s.enabled;
   doc.global.sleep.config.staticWakeAutoScanEnabled=!s.enabled;
   if(s.kind==='apply'||s.kind==='unrelated'||s.kind.startsWith('stale-')){
    if(s.mode==='global')doc.global.sleep.config.staticWakeAutoScanEnabled=s.enabled;
    else doc.scenes[s.mode].overrides.sleep={...clone(doc.global.sleep),config:{...clone(doc.global.sleep.config),staticWakeAutoScanEnabled:s.enabled}};
   }else if(s.kind==='follow')doc.global.sleep.config.staticWakeAutoScanEnabled=s.enabled;
   else if(s.kind==='absent')delete doc.global.sleep.config.staticWakeAutoScanEnabled;
   else if(s.kind==='invalid')doc.global.sleep.config.staticWakeAutoScanEnabled='true';
   const data=clone(seed);data.sleep.staticWakeAutoScanEnabled=rootValue;data.extensions.bootModes=clone(doc);
   data.gameProfiles={...data.gameProfiles,'C:/fixture/retain-static-game.exe':{sentinel:'dedicated-retained'}};
   disk.writeRaw(data);r.clearSettingsCache();const before=await r.loadSettings();Object.assign(q.settings,clone(before));
   const setter=evaluate(selectedSetter,{saveSettingsSection:r.saveSettingsSection});
   const writes=[];let boundary=null;
   q.hooks.sleepStaticWrite=async(enabled,origin)=>{
    writes.push({enabled,origin:clone(origin)});
    if(s.kind==='stale-input-native'||s.kind==='stale-mode-native'){
     disk.faults.holdWrite=true;const pending=setter.setStaticWakeAutoScanEnabled(enabled,origin).then(()=>({ok:true}),error=>({error}));
     const deadline=Date.now()+1500;while(!disk.faults.releaseWrite){assert(Date.now()<deadline,'Static preference did not reach final native boundary');await new Promise(resolve=>setImmediate(resolve));}
     const peer=await disk.moduleAt('late-peer-'+rows.length),repo=peer.repository;await repo.loadSettings();
     try{
      if(s.kind==='stale-input-native'){const current=await repo.loadSettings(),cas=await repo.compareAndSwapInputSettings(current.input.revision,{gyroMotion:{gyroWeight:current.input.gyroMotion.gyroWeight+.25}},undefined,{origin:'runtime'});assert(cas.ok);}
      else await repo.updateSettings(current=>{current.extensions.bootModes.revision++;current.extensions.bootModes.current=s.mode==='global'?'steam':'global';},{origin:'runtime'});
      boundary=disk.read();
     }finally{disk.faults.releaseWrite();}
     const reply=await pending;if(reply.error)throw reply.error;return;
    }
    if(s.kind==='stale-input'){
     const now=await r.loadSettings(),cas=await r.compareAndSwapInputSettings(now.input.revision,{gyroMotion:{gyroWeight:now.input.gyroMotion.gyroWeight+.25}},undefined,{origin:'runtime'});assert(cas.ok);
    }else if(s.kind==='stale-mode')await r.updateSettings(current=>{current.extensions.bootModes.revision++;current.extensions.bootModes.current=s.mode==='global'?'steam':'global';},{origin:'runtime'});
    else if(s.kind==='unrelated')await r.saveSettingsSection('startupDesired',{fanControl:!before.startupDesired.fanControl});
    boundary=disk.read();await setter.setStaticWakeAutoScanEnabled(enabled,origin);
    Object.assign(q.settings,clone(await r.loadSettings()));
   };
   let result,error;
   if(s.kind==='ordinary')await setter.setStaticWakeAutoScanEnabled(s.enabled);
   else if(s.kind==='invalid'){
    try{a.model.normalizeBootModeDocument(doc,doc.global);}catch(failure){error=String(failure);}
   }else{
    try{result=await a.boot.applyBootMode(s.mode,doc,{startup:true});}catch(failure){error=String(failure);}
   }
   const after=disk.read(),reopened=await disk.moduleAt('reopen-'+rows.length),loaded=await reopened.repository.loadSettings();
   const row={key:s.key,before:clone(before),after:clone(after),reopened:clone(loaded),writes,boundary,
    error:error??null,receipt:result?.receipt??null,activeSnapshot:result?.document.activeSnapshot.sleep??null};rows.push(row);
   if(s.kind==='invalid'){
    assert(error&&/staticWakeAutoScanEnabled/.test(error),'Known static sleep field must reject non-boolean values '+s.key);
    assert.deepEqual(after,before);assert.equal(writes.length,0);
   }else if(s.kind.startsWith('stale-')){
    assert(error,'Late owner must invalidate old mode publication '+s.key);assert.equal(writes.length,1);
    assert.equal(after.sleep.staticWakeAutoScanEnabled,rootValue,'Stale static preference reached disk '+s.key);
    assert.deepEqual(after,boundary,'Late owner was overwritten '+s.key);
   }else if(s.kind==='ordinary'){
    assert.equal(after.sleep.staticWakeAutoScanEnabled,s.enabled);
    assert.equal(after.extensions.bootModes.global.sleep.config.staticWakeAutoScanEnabled,s.enabled,'Explicit ordinary choice must update saved global '+s.key);
    assert.deepEqual(after.extensions.bootModes.scenes,before.extensions.bootModes.scenes);assert.deepEqual(after.extensions.bootModes.presets,before.extensions.bootModes.presets);
   }else{
    assert.equal(error,undefined,'Current static sleep configuration failed '+s.key);
    const expected=s.kind==='absent'?rootValue:s.enabled;
    assert.equal(after.sleep.staticWakeAutoScanEnabled,expected,'Saved mode static wake preference did not reach the real configuration '+s.key);
    assert.equal(writes.length,s.kind==='absent'?0:1,'Unexpected static setting writes '+s.key);
    if(writes.length){assert.equal(writes[0].origin.origin,'boot-mode');assert(writes[0].origin.bootContext);assert.equal(result.receipt.modules.sleep.state,'saved');}
    assert.deepEqual(after.extensions.bootModes.global,before.extensions.bootModes.global,'Mode restore contaminated global '+s.key);
    assert.deepEqual(after.extensions.bootModes.scenes,before.extensions.bootModes.scenes);
    if(s.kind==='unrelated')assert.equal(after.startupDesired.fanControl,!before.startupDesired.fanControl,'Common fan edit was lost');
   }
   assert.equal(loaded.sleep.staticWakeAutoScanEnabled,after.sleep.staticWakeAutoScanEnabled);
   assert.deepEqual(after.gameProfiles,before.gameProfiles);assert.deepEqual(after.games,before.games);
   a.boot.cancelBootModeDeferredMouse();a.boot.cancelBootModeLaunches();
  }
  complete=true;
 }finally{
  disk.a.boot.cancelBootModeDeferredMouse();disk.a.boot.cancelBootModeLaunches();disk.flush();
  fs.writeFileSync(path.join(out,'sleep-static-config.json'),JSON.stringify({complete,actualRows:rows.length,selectedNames:selected.map(s=>s.key),rows,
   sourceHashes:{[yemanFile]:createHash('sha256').update(source).digest('hex')},actualSetter:selectedSetter,realScans:0,realPowerCommands:0,
   scope:'Actual boot producer and actual setStaticWakeAutoScanEnabled, real repository/native file transactions and fresh reopens; no scan or sleep engine'},null,2)+'\n',{flag:'wx'});
 }
 return {actualRows:rows.length,selectedNames:selected.map(s=>s.key),realScans:0};
}

export async function runSleepPowerBootOwner(out,args=[]){
 const choice=name=>args.find(value=>value.startsWith('--sleep-power-owner-'+name+'='))?.split('=').slice(1).join('=');
 const sourceFile=choice('source')||yemanFile,source=fs.readFileSync(sourceFile,'utf8');
 const modes=['global','xbox','steam','xbox-steam'],kinds=['revision','hash','mode','result','observed','ack','unrelated','current'];
 const writes=['native-dispatch-1','shell-return-1'],operations=[];
 for(const field of ['screen','sleep','hibernate'])for(const ac of [true,false])operations.push({name:'timeout-'+field+'-'+(ac?'ac':'dc'),stages:[...writes,'registry-read-1','scheme-read-1','native-dispatch-2','shell-return-2','scheme-read-2'],timeout:true,run:(api,options)=>api.setSleepTimeout(field,ac,1,options)});
 for(const ac of [true,false])operations.push({name:'button-'+(ac?'ac':'dc'),stages:[...(ac?[]:['battery-read-1']),...writes,'registry-read-1'],run:(api,options)=>api.setPowerBtnIdx(ac,3,options)});
 operations.push({name:'hibernate-on',stages:[...writes,'native-dispatch-2','shell-return-2','hibernate-read-1'],run:(api,options)=>api.setHibernate(true,options)},
  {name:'hibernate-off',stages:[...writes,'hibernate-read-1'],run:(api,options)=>api.setHibernate(false,options)},
  {name:'hibernate-size',stages:[...writes,'native-dispatch-2','shell-return-2','hibernate-read-1','registry-read-1'],run:(api,options)=>api.setHibernateSize(60,options)},
  {name:'refresh',stages:writes,run:(api,options)=>api.reactivateCurrentScheme(options)});
 const selected={mode:choice('mode'),operation:choice('operation'),stage:choice('stage'),kind:choice('kind')};
 for(const [name,values]of Object.entries({mode:modes,operation:operations.map(o=>o.name),stage:[...new Set(operations.flatMap(o=>o.stages))],kind:kinds}))assert(!selected[name]||values.includes(selected[name]),'Unknown sleep owner '+name);
 const defaults=JSON.parse(fs.readFileSync('PowerControl/settings-defaults-v1.json','utf8')),rows=[],ordinary=[];
 const raw=path.join(out,'sleep-power-owner-observations.jsonl');fs.closeSync(fs.openSync(raw,'wx'));
 const fixtures=new Map(),make=(operation,mode)=>{
  if(!fixtures.has(operation.name)){
   const settings={},counter={reads:0},preferences={loadSettings:async()=>{counter.reads++;return clone(settings);}};
   const f=operation.timeout?fixture(source,preferences):createSleepPowerOwnerFixture(source,preferences);
   fixtures.set(operation.name,{...f,settings,counter,reads:()=>counter.reads});
  }
  const f=fixtures.get(operation.name);for(const key of Object.keys(f.settings))delete f.settings[key];Object.assign(f.settings,clone(defaults));
  f.settings.input.revision=7;f.settings.extensions={bootModes:{schema:1,revision:3,current:mode,boot:mode}};f.counter.reads=0;f.state.calls=[];delete f.state.onBoundary;
  f.context={input:clone(f.settings.input),extensions:clone(f.settings.extensions)};return f;
 };
 for(const mode of modes)for(const operation of operations)for(const stage of operation.stages)for(const kind of kinds){
  if(Object.entries(selected).some(([name,value])=>value&&value!==({mode,operation:operation.name,stage,kind})[name]))continue;
  const f=make(operation,mode),counts={},allowed=['ack','unrelated','current'].includes(kind);let changed=false,at=0;
  f.state.onBoundary=boundary=>{const label=boundary+'-'+(counts[boundary]=(counts[boundary]||0)+1);if(changed||label!==stage)return;changed=true;at=f.state.calls.length;
   if(kind==='revision')f.settings.input.revision++;
   if(kind==='hash')f.settings.input.diagnostics.inputLoggingEnabled=!f.settings.input.diagnostics.inputLoggingEnabled;
   if(kind==='mode')f.settings.extensions.bootModes.revision++;
   if(kind==='result')f.settings.extensions.bootModesStartupResult={schema:1,revision:99};
   if(kind==='observed')f.settings.input.startupObservedBoot='replacement-boot';
   if(kind==='ack')f.settings.input.hostAcknowledgedRevision=17;
   if(kind==='unrelated')f.settings.extensions.peer={keep:true};
  };
  let result,error='';try{result=await operation.run(f.api,{origin:'boot-mode',bootContext:f.context});}catch(e){error=String(e);}
  const late=f.state.calls.slice(at).filter(c=>c.kind==='shell'),row={mode,operation:operation.name,stage,kind,changed,allowed,error,result,lateCommands:late.map(c=>c.args),requests:f.state.calls.filter(c=>c.kind==='shell').map(c=>({args:c.args,bootContextPresent:!!c.bootContext})),ownerReads:f.reads()};
  fs.appendFileSync(raw,JSON.stringify(row)+'\n');assert(changed,'Sleep boundary not reached '+JSON.stringify(row));
  if(allowed){assert(!error,JSON.stringify(row));assert(row.requests.every(r=>r.bootContextPresent),'Sleep context missing on actual writer call');}
  else{assert(error,'Expired sleep request was accepted '+JSON.stringify(row));assert.match(error,/BOOT_PREFERENCE_REQUEST_EXPIRED/,'Unrelated error is not owner rejection '+JSON.stringify(row));assert.equal(late.length,0,'Expired sleep request reached later command '+JSON.stringify(row));}
  rows.push(row);
 }
 assert(rows.length,'No selected operation/stage pair');
 for(const operation of operations){const f=make(operation,'global');await operation.run(f.api,{origin:'runtime'});assert.equal(f.reads(),0);assert(f.state.calls.filter(c=>c.kind==='shell').every(c=>!c.bootContext));ordinary.push({operation:operation.name,ownerReads:0,unscopedWirePreserved:true});}
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const inputs=[...new Set([sourceFile,yemanFile,viewFile,'tools/boot_modes_selftest.mjs','tools/boot_modes_fixture/sleepTimeouts.mjs','PowerControl/settings-defaults-v1.json',...ownershipGlobals().inputs])].map(file=>({path:path.resolve(file),bytes:fs.statSync(file).size,sha256:hash(file)}));
 const result={status:'PASS',actualRows:rows.length,expectedAllRows:modes.length*kinds.length*operations.reduce((n,o)=>n+o.stages.length,0),selected,operations:operations.map(o=>({name:o.name,stages:o.stages})),ordinaryControls:ordinary,inputs,sourceFile:path.resolve(sourceFile),scope:'Original AST sleep writers and actual preference guard; inert Windows shell/registry/native-owner terminal. No real powercfg, device, sleep, installed UI or full native main execution.'};
 fs.writeFileSync(path.join(out,'sleep-power-owner-result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}
export async function runSleepTimeouts(out,args=[]){
 const beforeDir=args.includes('--before-dir')?args[args.indexOf('--before-dir')+1]:null;
 assert(beforeDir,'--before-dir is required for the pre-fix regression negative');
 const source=fs.readFileSync(yemanFile,'utf8'),old=fs.readFileSync(path.join(beforeDir,yemanFile),'utf8');
 const files=[yemanFile,viewFile,'tools/boot_modes_selftest.mjs','tools/boot_modes_fixture/sleepTimeouts.mjs'];
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex'),pins=Object.fromEntries(files.map(file=>[file,hash(file)]));
 const report={pass:false,scope:'Actual AST declaration slices of timeout bridge/current-plan refresh/readback and Vue save handler; memory Windows terminals. No installed app, hardware, real powercfg or browser. Boot-mode dispatcher and full UI engine NOT_RUN.',cases:[],sourceHashes:pins,realWindowsWrites:0,actualWindowsIdleAcceptance:'NOT_RUN'};
 const check=async(name,run)=>{try{await run();report.cases.push({name,pass:true});}catch(error){report.cases.push({name,pass:false,error:error.stack||String(error)});}};
 const shellCalls=f=>f.state.calls.filter(c=>c.kind==='shell'),applied=f=>shellCalls(f).filter(c=>c.args[0]==='/setactive');
 await check('Screen/sleep/hibernate addresses match independent Windows GUIDs including the 666-second report',async()=>{const f=fixture(source);assert.deepEqual(clone(f.api.SLEEP_TIMEOUT_FIELDS),expectedFields);const reportText=fs.readFileSync(path.join(beforeDir,'../Report.1.html'),'utf8');assert(reportText.includes(expectedFields.screen.subGroup+'\\'+expectedFields.screen.setting));assert(reportText.includes('0000029a'));assert.equal(parseInt('29a',16),666);});
 await check('Old source reproduces saved 60s but live policy remains 3600s',async()=>{const f=fixture(old);await f.api.setSleepTimeout('screen',true,1);assert.equal(f.stored('screen',true),60);assert.equal(f.live('screen',true),3600);assert.equal(applied(f).length,0);});
 for(const field of ['screen','sleep','hibernate'])for(const ac of [true,false])await check(`${field}/${ac?'AC':'DC'} one minute updates only its 60-second live policy`,async()=>{
  const f=fixture(source),before=clone(f.state.values);await f.api.setSleepTimeout(field,ac,1);assert.equal(f.stored(field,ac),60);assert.equal(f.live(field,ac),60);assert.equal(applied(f).length,1);
  const spec=expectedFields[field],edited=key(f.api.PW.YEMAN,spec.subGroup,spec.setting,ac);
  for(const [k,v]of before)if(k!==edited)assert.equal(f.state.values.get(k),v,'Other row/side/plan preserved');
  assert.deepEqual(shellCalls(f).map(c=>c.args[0]),[ac?'/setacvalueindex':'/setdcvalueindex','/setactive']);
 });
 await check('Six fields read correct AC/DC values without writes or activation',async()=>{const f=fixture(source);let n=1;for(const field of ['screen','sleep','hibernate'])for(const ac of [true,false]){const s=expectedFields[field];f.state.values.set(key(f.api.PW.YEMAN,s.subGroup,s.setting,ac),60*n++);}assert.deepEqual(clone(await f.api.readSleepTimeouts()),{acScreen:1,dcScreen:2,acSleep:3,dcSleep:4,acHibernate:5,dcHibernate:6});assert.equal(shellCalls(f).length,0);});
 await check('Never=0 stays zero in six fields; no direct screen-off/sleep commands',async()=>{for(const field of ['screen','sleep','hibernate'])for(const ac of [true,false]){const f=fixture(source);await f.api.setSleepTimeout(field,ac,0);assert.equal(f.stored(field,ac),0);assert.equal(f.live(field,ac),0);assert.equal(shellCalls(f).length,2);}});
 await check('Inactive alternative with a similar name stays untouched and inactive YeMan is only saved',async()=>{const f=fixture(source);f.state.active=f.other;const before=clone([...f.state.values].filter(([k])=>k.startsWith(f.other)));await f.api.setSleepTimeout('screen',true,1);assert.equal(f.stored('screen',true),60);assert.equal(applied(f).length,0);assert.equal(f.state.active,f.other);assert.deepEqual([...f.state.values].filter(([k])=>k.startsWith(f.other)),before);});
 await check('User scheme change while saving is preserved without switching to YeMan',async()=>{const f=fixture(source);f.state.afterWrite=()=>{f.state.active=f.api.PW.WIN_BAL;};await f.api.setSleepTimeout('screen',true,1);assert.equal(f.state.active,f.api.PW.WIN_BAL);assert.equal(applied(f).length,0);assert.equal(f.stored('screen',true,f.api.PW.WIN_BAL),3600);});
 await check('Last-moment scheme change stays active through SCHEME_CURRENT',async()=>{const f=fixture(source);f.state.beforeActivate=()=>{f.state.active=f.api.PW.WIN_BAL;};await f.api.setSleepTimeout('screen',true,1);assert.equal(f.state.active,f.api.PW.WIN_BAL);assert.equal(applied(f)[0].args[1],'SCHEME_CURRENT');assert.equal(f.stored('screen',true,f.api.PW.WIN_BAL),3600);});
 await check('Denied write fails before activation with original values preserved',async()=>{const f=fixture(source);f.state.writeExit=5;await assert.rejects(f.api.setSleepTimeout('screen',true,1),/写入失败/);assert.equal(applied(f).length,0);assert.equal(f.stored('screen',true),3600);assert.equal(f.live('screen',true),3600);});
 await check('Readback mismatch refuses an applied-success claim',async()=>{const f=fixture(source);f.state.readbackWrong=true;await assert.rejects(f.api.setSleepTimeout('screen',true,1),/回读不一致/);assert.equal(applied(f).length,0);assert.equal(f.live('screen',true),3600);});
 await check('Activation failure reports saved but incomplete and keeps old live policy',async()=>{const f=fixture(source);f.state.activateExit=5;await assert.rejects(f.api.setSleepTimeout('screen',true,1),/已保存.*刷新未完成/);assert.equal(f.stored('screen',true),60);assert.equal(f.live('screen',true),3600);});
 await check('Unknown active plan refuses to switch or falsely report completed activation',async()=>{const f=fixture(source);f.state.activeUnknown=true;await assert.rejects(f.api.setSleepTimeout('screen',true,1),/已保存.*无法确认/);assert.equal(applied(f).length,0);});
 await check('Nonfinite value and unsupported field fail before all Windows terminals',async()=>{for(const [field,minutes]of [['screen',NaN],['screen',Infinity],['other',1]]){const f=fixture(source);await assert.rejects(f.api.setSleepTimeout(field,true,minutes),/参数无效/);assert.equal(f.state.calls.length,0);}});
 await check('Actual UI retains saved 1 minute when apply fails and displays that failure',async()=>{const f=fixture(source),v=ui(f);f.state.activateExit=5;await v.functions.onSleepTimeout('screen',true,1);assert.equal(v.sleepTimeouts.acScreen,1);assert.match(v.msg.value,/已保存.*刷新未完成/);assert.equal(v.busy.value,false);assert.equal(v.sleepTimeouts.dcScreen,60);});
 await check('UI rereads denied writes then permits one explicit successful retry',async()=>{const f=fixture(source),v=ui(f);f.state.writeExit=5;await v.functions.onSleepTimeout('screen',true,1);assert.equal(v.sleepTimeouts.acScreen,60);assert.match(v.msg.value,/写入失败/);f.state.writeExit=0;await v.functions.onSleepTimeout('screen',true,1);assert.equal(v.sleepTimeouts.acScreen,1);assert.equal(v.msg.value,'');assert.equal(applied(f).length,1);});
 await check('Busy/loading UI suppresses duplicate mutations',async()=>{for(const stateKey of ['busy','timeoutLoading']){const f=fixture(source),v=ui(f);v[stateKey].value=true;await v.functions.onSleepTimeout('screen',true,1);assert.equal(f.state.calls.length,0);}});
 await check('Common current-plan activation validates nonzero exit for existing callers',async()=>{const f=fixture(source);f.state.activateExit=5;await assert.rejects(f.api.reactivateCurrentScheme(),/刷新失败/);});
 assert.deepEqual(Object.fromEntries(files.map(file=>[file,hash(file)])),pins,'Source changed during regression');
 report.passed=report.cases.filter(c=>c.pass).length;report.failed=report.cases.length-report.passed;report.pass=report.failed===0;
 fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'sleep-timeouts.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({pass:report.pass,passed:report.passed,failed:report.failed,output:out},null,2));if(!report.pass)process.exitCode=1;
}
