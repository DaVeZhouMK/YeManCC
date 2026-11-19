import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {createRequire} from 'node:module';
import {runtimeBundle as buildRuntimeBundle,outputDirectory,fingerprints} from './boot_modes_fixture/harness.mjs';
const sourceIndex=process.argv.indexOf('--boot-modes-source');
if(sourceIndex>=0&&!process.argv[sourceIndex+1])throw Error('--boot-modes-source requires an original production source file');
const startupBridgeSourceIndex=process.argv.indexOf('--startup-bridge-source');
if(startupBridgeSourceIndex>=0&&!process.argv[startupBridgeSourceIndex+1])throw Error('--startup-bridge-source requires an original bridge source file');
const startupBridgeSource=path.resolve(startupBridgeSourceIndex<0?'src/bridge/yeman.ts':process.argv[startupBridgeSourceIndex+1]);
const modelSourceIndex=process.argv.indexOf('--boot-mode-model-source');
if(modelSourceIndex>=0&&!process.argv[modelSourceIndex+1])throw Error('--boot-mode-model-source requires an original model source file');
const runtimeBundle=(directory,options={})=>buildRuntimeBundle(directory,{...options,
 ...(sourceIndex<0?{}:{bootModesSource:path.resolve(process.argv[sourceIndex+1])}),
 ...(modelSourceIndex<0?{}:{bootModeModelSource:path.resolve(process.argv[modelSourceIndex+1])})});
const out=outputDirectory(process.argv.slice(2),'runtime');
if(process.argv.includes('--shell-public-wire')&&(process.argv.includes('--shell-public-power')||process.argv.includes('--shell-power-case')||process.argv.includes('--shell-power-reuse')))throw Error('SHELL_PUBLIC_SCOPE_CONFLICT');
if(process.argv.includes('--shell-public-wire')){const {runShellPublicWire}=await import('./boot_modes_fixture/brightnessOwner.mjs');const result=await runShellPublicWire(out);fs.writeFileSync(path.join(out,'boot-modes-runtime.json'),JSON.stringify({pass:true,selection:'shell-public-wire',actual:result,counts:{passed:13,failed:0},originalCanonicalId:'settings.boot-modes-runtime',fullDefaultRun:false},null,2)+'\n',{flag:'wx'});process.exit(process.exitCode||0);}
if(process.argv.includes('--shell-public-power')||process.argv.includes('--game-detect-public-power')){const {runShellOwnerNative}=await import('./boot_modes_fixture/brightnessOwner.mjs');const result=await runShellOwnerNative(out,process.argv.slice(2));fs.writeFileSync(path.join(out,'boot-modes-runtime.json'),JSON.stringify({pass:true,selection:'shell-public-power',actual:result,counts:{passed:result.actualRows,failed:0},originalCanonicalId:'settings.boot-modes-runtime',fullDefaultRun:false},null,2)+'\n',{flag:'wx'});process.exit(process.exitCode||0);}
if(process.argv.includes('--sleep-timeouts')){const {runSleepTimeouts}=await import('./boot_modes_fixture/sleepTimeouts.mjs');await runSleepTimeouts(out,process.argv.slice(2));process.exit(process.exitCode||0);}
if(process.argv.includes('--gpu-schedule')){const {runGpuScheduleFirstRun}=await import('./boot_modes_fixture/gpuScheduleFirstRun.mjs');await runGpuScheduleFirstRun(out,process.argv.slice(2));process.exit(process.exitCode||0);}
const filterIndex=process.argv.indexOf('--filter');
const filter=filterIndex>=0?process.argv[filterIndex+1]:null;
if(filterIndex>=0&&!filter)throw Error('--filter requires an exact case-name substring');
const exactCases=[];
for(let index=2;index<process.argv.length;index++)if(process.argv[index]==='--case'){
 const name=process.argv[++index];if(!name||name.startsWith('--')||exactCases.includes(name))throw Error('--case requires unique exact original case names');exactCases.push(name);
}
if(exactCases.length&&filter)throw Error('Choose --case or --filter');
const caseFromIndex=process.argv.indexOf('--case-from'),caseFrom=caseFromIndex>=0?process.argv[caseFromIndex+1]:null;
assert(caseFromIndex<0||typeof caseFrom==='string'&&!!caseFrom&&!filter&&!exactCases.length,'Choose an exact --case-from name without another selector');
let caseFromReached=!caseFrom;
const notRun=[];
const collectFailures=process.argv.includes('--collect-failures');
let halted=false;
const report={selection:exactCases.length?'cases:'+exactCases.join('|'):filter||(caseFrom?'from:'+caseFrom:'all'),notRun,schema:1,suite:'boot-modes-runtime',pass:false,isolation:'Bundled actual bootModeModel.ts and bootModes.ts; every imported native owner is aliased to memory; no installed application.',assertions:[],sourceHashes:{},errors:[]};
const {model:m,boot:b,desktopStartup:desktop,gameBarStartup:gameBar,ports:p,shortcuts,inputs}=await runtimeBundle(out);
const suppliedControllerInput=process.argv.indexOf('--controller-input');if(suppliedControllerInput>=0){const file=path.resolve(process.argv[suppliedControllerInput+1]);if(!fs.existsSync(file))throw Error('Missing actual editor receipt');inputs.push(path.relative(process.cwd(),file));}
const nativeContractInputs=['src/bridge/performanceSchedule.ts','PowerControl/settings-defaults-v1.json','tools/fixtures/performance_memory_fixture.mjs','src/bridge/gamePolicyRuntime.ts','src/bridge/settingsSnapshot.ts','src/bridge/powerLifecycleState.ts','src/bridge/frameRateLimits.ts','tools/continuous_performance_selftest.mjs','tools/boot_modes_fixture/performancePreferences.mjs','src/components/BootModeWorkbench.vue','tools/boot_modes_fixture/legacyOrigin.mjs','src/bridge/inputApplicationReceipt.ts','src/bridge/inputConfigHash.ts','native/input_application_receipt.h','tools/boot_modes_fixture/inputReceipts.mjs','src/bridge/bootModeGlobalPreferences.ts','src/bridge/steamAppearance.ts','src/bridge/steamAppearanceDraft.ts','src/views/SleepGuardView.vue','tools/boot_modes_fixture/sleepTimeouts.mjs','native/boot_mode_preferences.h','native/screen_touchpad_profile_store.h','src/App.vue','native/main.cpp','native/boot_mode_shortcuts.h','src/bridge/gameproc.ts','src/bridge/ipc.ts','src/bridge/yeman.ts','src/bridge/steamUi.ts','src/bridge/settingsRepository.ts','tools/fixtures/settings_baseline.mjs'];
report.sourceHashes=fingerprints([...new Set([...inputs,...nativeContractInputs,'src/bridge/powerResume.ts','tools/boot_modes_selftest.mjs','tools/boot_modes_fixture/harness.mjs'])]);
if(startupBridgeSourceIndex>=0)report.startupBridgeSource=fingerprints([path.relative(process.cwd(),startupBridgeSource)]);
if(!globalThis.window)globalThis.window=new EventTarget();
const test=async(name,run)=>{if(!caseFromReached){if(name===caseFrom)caseFromReached=true;else{notRun.push(name);return;}}if(halted&&!collectFailures||filter&&!name.includes(filter)||exactCases.length&&!exactCases.includes(name)){notRun.push(name);return;}b.cancelBootModeDeferredMouse?.();p.reset();try{const evidence=await run();report.assertions.push({name,pass:true,...(evidence?{evidence:structuredClone(evidence)}:{})});}catch(error){halted=true;report.assertions.push({name,pass:false,error:error.stack||String(error)});}finally{b.cancelBootModeDeferredMouse?.();}};
const waitSignal=async(signal,label)=>{let timer;try{return await Promise.race([signal,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Timed out waiting for '+label)),1500);})]);}finally{clearTimeout(timer);}};
const startMode=(id,document)=>b.applyBootMode(id,document,{startup:true});
const {runPreparationCases}=await import('./boot_modes_fixture/preparation.mjs');
await runPreparationCases({test,out,runtimeBundle});
await test('platform preparation stops obsolete steps and completion after another writer changes next boot',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());a.ports.settings.extensions.bootModes=structuredClone(doc);
 let enter,release;const entered=new Promise(resolve=>{enter=resolve;}),hold=new Promise(resolve=>{release=resolve;});
 a.ports.hooks.steamStartupWrite=async()=>{enter();await hold;};
 const pending=a.boot.chooseBootMode('steam',doc,true),settled=pending.then(value=>({value}),error=>({error:String(error)}));
 await waitSignal(entered,'platform preparation dispatch');
 const newer=structuredClone(a.ports.settings.extensions.bootModes);newer.revision++;newer.boot='xbox';newer.bootEnabled=true;delete newer.preparation;
 a.ports.settings.extensions.bootModes=newer;
 const count=a.ports.calls.length;release();const result=await settled;
 assert(result.error,'obsolete preparation returned successful completion');
 assert.match(result.error,/变化|过期|失效/);assert.equal(a.ports.settings.extensions.bootModes.revision,newer.revision);
 assert.equal(a.ports.settings.extensions.bootModes.boot,'xbox');
 assert(!a.ports.calls.slice(count).some(row=>row.op==='fx.set'||row.op==='addon.set'),'obsolete preparation dispatched later startup owners');
 const windows=[];
  for(const id of ['xbox','steam','xbox-steam'])for(const replacement of ['mode','same-revision']) {
  const x=await runtimeBundle(out),q=x.ports,original=x.model.createBootModeDocument(await x.boot.captureBootModeSnapshot());
  q.settings.extensions.bootModes=structuredClone(original);
  q.state.deviceForm=3;q.state.xbox=id==='steam';q.state.homeApp='Previous.Home!App';
  const before={deviceForm:q.state.deviceForm,xbox:q.state.xbox,homeApp:q.state.homeApp};
  let enter,release;const reached=new Promise(r=>enter=r),held=new Promise(r=>release=r);
  q.hooks.gamingHomeBackup=()=>{enter();return held;};
  const saving=x.boot.chooseBootMode(id,original,true).then(value=>({value}),error=>({error}));
  await waitSignal(reached,'Windows startup registry backup');
   const newer=structuredClone(q.settings.extensions.bootModes);
   if(replacement==='mode'){newer.revision++;newer.boot='global';delete newer.preparation;}
   else newer.preparation.observedBoot='replacement-boot';
  q.settings.extensions.bootModes=newer;
  release();const result=await saving;
   assert.deepEqual({deviceForm:q.state.deviceForm,xbox:q.state.xbox,homeApp:q.state.homeApp},before,`${id}/${replacement}: stale Windows startup request changed registry values after the backup wait`);
  assert.match(String(result.error),/变化|过期|失效/);
  assert.deepEqual(q.settings.extensions.bootModes,newer);
   windows.push({id,replacement});
 }
 const legacy=[];
 for(const [step,key,initial] of [['legacyMouse','joyStartup','C:/SOFT/Joyxoff/Joyxoff.exe'],['audioStartup','fx',true],['frameStartup','addon',true]]){
  const x=await runtimeBundle(out),q=x.ports,original=x.model.createBootModeDocument(await x.boot.captureBootModeSnapshot());
  q.settings.extensions.bootModes=structuredClone(original);q.state[key]=initial;
  let enter,release;const reached=new Promise(r=>enter=r),held=new Promise(r=>release=r);
  q.hooks.legacyStartupMutation=current=>{if(current===step){enter();return held;}};
  const saving=x.boot.chooseBootMode('steam',original,true).then(value=>({value}),error=>({error}));
  await waitSignal(reached,step+' terminal');
  const newer=structuredClone(q.settings.extensions.bootModes);newer.revision++;newer.boot='global';delete newer.preparation;
  q.settings.extensions.bootModes=newer;
  release();assert.match(String((await saving).error),/变化|过期|失效/);
  legacy.push({step,preserved:q.state[key]===initial,newerPreserved:JSON.stringify(q.settings.extensions.bootModes)===JSON.stringify(newer)});
 }
 assert.deepEqual(legacy.map(row=>[row.step,row.preserved,row.newerPreserved]),[['legacyMouse',true,true],['audioStartup',true,true],['frameStartup',true,true]],'Stale preparation must not retire any startup entry');
 return {newerSelectionPreserved:true,noLaterBusinessWrites:true,oldCompletionRejected:true,inFlightDispatchIsNotCancellationProof:true,windowsBackupOwnership:windows,legacyStartupOwnership:legacy};
});
await test('Steam startup ownership preparation keeps ordinary edits but rejects replaced request and input owners',async()=>{
 const rows=[];
 for(const mode of ['global','xbox','steam','xbox-steam'])for(const change of ['ordinary','selection','new-request','disabled','input','observed','result']){
  const a=await runtimeBundle(out),q=a.ports;q.reset();const document=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());q.settings.extensions.bootModes=structuredClone(document);
  const enabled=['steam','xbox-steam'].includes(mode);q.state.steam=!enabled;
  q.hooks.steamStartupWrite=async()=>{const newer=structuredClone(q.settings.extensions.bootModes);newer.revision++;
   if(change==='ordinary')newer.global.background={ui:{theme:'red-black'}};
   if(change==='selection')newer.boot=mode==='global'?'xbox':'global';
   if(change==='new-request')newer.preparation.revision=newer.revision;
   if(change==='disabled')newer.bootEnabled=false;
   q.settings.extensions.bootModes=newer;
   if(change==='input')q.settings.input.revision++;
   if(change==='observed')q.settings.input.startupObservedBoot='new-boot';
   if(change==='result')q.settings.extensions.bootModesStartupResult={schema:1,revision:99};
  };
  const settled=await a.boot.chooseBootMode(mode,document).then(value=>({value}),error=>({error:String(error)}));
  if(change==='ordinary'){assert.equal(settled.value?.preparation.status,'prepared');assert.equal(q.state.steam,enabled);assert.equal(settled.value.global.background.ui.theme,'red-black');}
  else {assert.equal(q.state.steam,!enabled,'expired preparation mutated Steam startup');assert(settled.error||settled.value?.preparation.status==='partial');}
  rows.push({mode,change,accepted:change==='ordinary',ordinaryEditRetained:true,staleRequestNeverMutates:change!=='ordinary'});
 }return rows;
});
await test('Steam startup ownership rejects late advanced writes after another renderer changes any of the four modes',async()=>{
 const rows=[];
 for(const id of ['global','xbox','steam','xbox-steam']){
  const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());a.ports.settings.extensions.bootModes=structuredClone(doc);
  const next=id==='steam'||id==='xbox-steam'?'global':'steam',expected=next==='steam';let submitted,afterHook=0;
  a.ports.hooks.steamStartupWrite=async options=>{
   submitted=options;const newer=structuredClone(a.ports.settings.extensions.bootModes);newer.revision++;newer.boot=next;newer.bootEnabled=true;delete newer.preparation;
   a.ports.settings.extensions.bootModes=newer;a.ports.state.steam=expected;
   a.ports.state.earlyCommand=expected?'"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"':'';afterHook=a.ports.calls.length;
  };
  await assert.rejects(a.boot.chooseBootMode(id,doc,true),/变化|过期|失效/);
  assert.equal(a.ports.state.steam,expected,'late old request changed the newer advanced setting');
  assert.equal(!!a.ports.state.earlyCommand,expected,'late old request recreated or deleted the newer file');
  assert.equal(a.ports.settings.extensions.bootModes.boot,next);
  assert.equal(submitted.origin,'boot-mode');assert.equal(submitted.bootContext.extensions.bootModes.boot,id);
  assert(!a.ports.calls.slice(afterHook).some(row=>row.op==='fx.set'||row.op==='addon.set'));
  rows.push({id,newer:next,newerAdvancedPreserved:expected,oldWriteRejected:true});
 }
 return rows;
});
await test('Steam startup ownership carries the committed global boot into automatic legacy cleanup',async()=>{
 const a=await runtimeBundle(out);a.ports.reset();a.ports.settings.extensions={};a.ports.state.steam=true;a.ports.state.earlyCommand='"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
 a.ports.hooks.steamStartupWrite=async()=>{
  const next=structuredClone(a.ports.settings.extensions.bootModes);next.boot='steam';next.bootEnabled=true;next.revision++;delete next.preparation;
  a.ports.settings.extensions.bootModes=next;a.ports.state.steam=true;a.ports.state.earlyCommand='"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
 };
 await assert.rejects(a.boot.restoreSelectedBootMode(),/变化|过期|失效/);
 assert.equal(a.ports.state.steam,true);assert(a.ports.state.earlyCommand);
 assert.equal(a.ports.settings.extensions.bootModes.boot,'steam');
});
await test('Steam startup ownership keeps an ordinary setting unscoped and unchanged',async()=>{
 const a=await runtimeBundle(out);a.ports.reset();const settings=structuredClone(a.ports.settings);
 await a.ports.steamMasterSet(true,true);assert.equal(a.ports.state.steam,true);
 assert.equal(a.ports.state.steamStartupOptions.bootContext,undefined);assert.equal(a.ports.state.steamStartupOptions.origin,undefined);
 assert.deepEqual(a.ports.settings,settings);assert(!a.ports.calls.some(row=>row.op==='settings.update'));
});
await test('Steam startup ownership rejects a boot-mode write without its context before any change',async()=>{
 const a=await runtimeBundle(out);a.ports.reset();const settings=structuredClone(a.ports.settings),state=structuredClone(a.ports.state);
 await assert.rejects(a.ports.steamMasterSet(true,true,{origin:'boot-mode'}),/BOOT_STEAM_STARTUP_CONTEXT_REQUIRED/);
 assert.deepEqual(a.ports.settings,settings);assert.deepEqual(a.ports.state,state);assert.equal(a.ports.calls.length,0);
});
await test('Steam startup ownership actual bridge captures each request and preserves ordinary payload and service checks',async()=>{
 const require=createRequire(import.meta.url),ts=require('typescript'),esbuild=require('esbuild');
 const source=fs.readFileSync(startupBridgeSource,'utf8'),ast=ts.createSourceFile('actual-yeman.ts',source,ts.ScriptTarget.Latest,true);
 const functions=ast.statements.filter(node=>ts.isFunctionDeclaration(node)&&node.name?.text==='steamMasterSet');
 assert.equal(functions.length,1,'Exactly one production Steam startup bridge');
 const helpers={exports:{}};
 vm.runInNewContext(esbuild.buildSync({entryPoints:['src/bridge/bootModeGlobalPreferences.ts'],absWorkingDir:process.cwd(),bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text,
  {module:helpers,exports:helpers.exports,structuredClone,TextEncoder});
 const calls=[],context={captureBootPreferenceWrite:helpers.exports.captureBootPreferenceWrite,reply:{ok:true,enabled:true,service:{ok:true}},
  invoke:async(op,args,options)=>{calls.push({op,args,options});return context.reply;}};
 const code=functions[0].getText(ast).replace(/^export\s+/,'')+'\nglobalThis.actualSteamMasterSet=steamMasterSet;';
 vm.runInNewContext(ts.transpileModule(code,{compilerOptions:{target:ts.ScriptTarget.ES2022}}).outputText,context);
 const rows=[];
 for(const enabled of [false,true])for(const repair of [false,true]){
  context.reply={ok:true,enabled,service:{ok:true}};await context.actualSteamMasterSet(enabled,repair);
  const call=calls.at(-1);assert.equal(call.op,'steam.startup.set');
  assert.equal(JSON.stringify(call.args),JSON.stringify({enabled,...(repair?{repairCommand:true}:{})}));
  assert.equal(call.options.timeoutMs,6000);rows.push({ordinary:true,enabled,repair});
 }
 for(const mode of ['global','xbox','steam','xbox-steam']){
  const bootContext={input:{revision:7},extensions:{bootModes:{schema:1,revision:3,boot:mode}}};
  const options={origin:'boot-mode',bootContext},expected=structuredClone(bootContext);
  context.reply={ok:true,enabled:true,service:{ok:true}};
  const pending=context.actualSteamMasterSet(true,true,options);
  bootContext.input.revision=99;bootContext.extensions.bootModes.boot='changed-after-submission';options.origin='ordinary';
  await pending;const call=calls.at(-1);assert.equal(call.args.origin,'boot-mode');
  assert.equal(JSON.stringify(call.args.bootContext),JSON.stringify(expected));
  rows.push({mode,submittedSnapshotPreserved:true});
 }
 const before=calls.length;
 for(const options of [
  {origin:'ordinary',bootContext:{input:{revision:1},extensions:{bootModes:{schema:1}}}},
  {origin:'boot-mode',bootContext:{input:{revision:-1},extensions:{bootModes:{schema:1}}}},
  {origin:'boot-mode',bootContext:{input:{revision:1},extensions:{bootModes:{schema:1},unknown:true}}}
 ])await assert.rejects(context.actualSteamMasterSet(true,true,options),/INVALID_BOOT_PREFERENCE_CONTEXT/);
 assert.equal(calls.length,before,'Invalid context reached IPC');
 for(const reply of [{ok:false,enabled:true},{ok:true,enabled:false}]){
  context.reply=reply;await assert.rejects(context.actualSteamMasterSet(true),/设置未确认/);
 }
 for(const service of [undefined,{ok:false,reason:'service-missing'},{ok:false,win32Error:5}]){
  context.reply={ok:true,enabled:true,...(service?{service}:{})};
  await assert.rejects(context.actualSteamMasterSet(true),/EarlyStart Service/);
 }
 context.reply={ok:true,enabled:false};await context.actualSteamMasterSet(false);
 return {rows,invalidContextsRejectedBeforeIpc:3,failedReceiptsRejected:5,offNeedsNoSharedServiceStart:true,
  sourceFunction:'Actual yeman.ts steamMasterSet, actual captureBootPreferenceWrite; only IPC reply is controlled'};
});
await test('Game Bar controller missing preference actively disables the Windows shortcut once',async()=>{
 const before=structuredClone(p.settings);p.state.gameBarControllerWindows=1;
 await Promise.all(Array.from({length:4},()=>gameBar.ensureGameBarControllerStartup()));
 assert.equal(p.state.gameBarControllerWindows,0);assert.equal(p.calls.filter(c=>c.op==='registry.write').length,1);
 assert.deepEqual(p.settings,before);const count=p.calls.length;await gameBar.ensureGameBarControllerStartup();assert.equal(p.calls.length,count);
});
await test('Game Bar controller absent Windows value is explicitly written OFF',async()=>{
 await gameBar.ensureGameBarControllerStartup();assert.equal(p.state.gameBarControllerWindows,0);
 assert.deepEqual(p.calls.filter(c=>c.op==='registry.write').map(c=>c.value),[{hive:'HKCU',key:'Software\\Microsoft\\GameBar',name:'UseNexusForGameBarEnabled',value:0}]);
});
await test('Game Bar controller explicit startup ON and OFF are both applied',async()=>{
 for(const enabled of [true,false]){p.reset();p.settings.startupDesired.gameBarControllerOnBoot=enabled;p.state.gameBarControllerWindows=enabled?0:1;await gameBar.ensureGameBarControllerStartup();assert.equal(p.state.gameBarControllerWindows,enabled?1:0);assert.equal(p.settings.startupDesired.gameBarControllerOnBoot,enabled);}
});
await test('Game Bar controller matching Windows state stays static without registry or configuration writes',async()=>{
 p.state.gameBarControllerWindows=0;await gameBar.ensureGameBarControllerStartup();assert.deepEqual(p.calls.map(c=>c.op),['registry.read']);
});
await test('Game Bar controller ordinary read only reports actual Windows value',async()=>{
 for(const [value,enabled]of [[0,false],[1,true],[null,true]]){p.state.gameBarControllerWindows=value;assert.equal(await gameBar.readGameBarControllerEnabled(),enabled);}assert(!p.calls.some(c=>c.op==='registry.write'||c.op==='settings.update'));
});
await test('Game Bar controller manual choices persist globally and apply 1 then 0',async()=>{
 p.state.gameBarControllerWindows=0;const unrelated=structuredClone(p.settings.input);await gameBar.setGameBarControllerEnabled(true);assert.equal(p.settings.startupDesired.gameBarControllerOnBoot,true);assert.equal(p.state.gameBarControllerWindows,1);
 await gameBar.setGameBarControllerEnabled(false);assert.equal(p.settings.startupDesired.gameBarControllerOnBoot,false);assert.equal(p.state.gameBarControllerWindows,0);assert.deepEqual(p.settings.input,unrelated);assert.deepEqual(p.calls.filter(c=>c.op==='registry.write').map(c=>c.value.value),[1,0]);
});
await test('Game Bar controller saved ON survives a later settings generation without default OFF overriding it',async()=>{
 await gameBar.setGameBarControllerEnabled(true);p.invalidate();p.state.gameBarControllerWindows=0;await gameBar.ensureGameBarControllerStartup();assert.equal(p.state.gameBarControllerWindows,1);assert.equal(p.settings.startupDesired.gameBarControllerOnBoot,true);
});
await test('Game Bar controller bad startup preferences never authorize registry writes',async()=>{
 for(const bad of ['false',0,null,{},[]]){p.reset();p.settings.startupDesired.gameBarControllerOnBoot=bad;await assert.rejects(gameBar.ensureGameBarControllerStartup(),/偏好无效/);assert(!p.calls.some(c=>c.op==='registry.write'));}
});
await test('Game Bar controller unpersisted fallback configuration never changes Windows',async()=>{
 p.hooks.recoveryState={persisted:false};await assert.rejects(gameBar.ensureGameBarControllerStartup(),/配置尚未确认/);await assert.rejects(gameBar.setGameBarControllerEnabled(false),/配置尚未确认/);assert.equal(p.calls.length,0);
});
await test('Game Bar controller denied Windows write is reported once without background retry',async()=>{
 p.state.gameBarControllerWindows=1;p.state.gameBarWriteRejected=true;await assert.rejects(gameBar.ensureGameBarControllerStartup(),/未确认/);await assert.rejects(gameBar.ensureGameBarControllerStartup(),/未确认/);assert.equal(p.calls.filter(c=>c.op==='registry.write').length,1);assert.equal(p.state.gameBarControllerWindows,1);
});
await test('Game Bar controller divergent Windows readback cannot sign success',async()=>{
 p.state.gameBarControllerWindows=1;p.state.gameBarNoReadback=true;await assert.rejects(gameBar.ensureGameBarControllerStartup(),/回读不一致/);assert.equal(p.state.gameBarControllerWindows,1);
});
await test('Game Bar controller failed preference save prevents the explicit Windows write',async()=>{
 p.state.gameBarControllerWindows=0;p.faults['settings.update']=1;await assert.rejects(gameBar.setGameBarControllerEnabled(true),/injected:settings.update/);assert(!p.calls.some(c=>c.op==='registry.write'));assert.equal(p.state.gameBarControllerWindows,0);
});
await test('Game Bar controller malformed Windows values are rejected without writing',async()=>{
 for(const bad of ['0',2,{},undefined]){p.reset();p.state.gameBarControllerWindows=bad;await assert.rejects(gameBar.ensureGameBarControllerStartup(),/Windows 设置无效/);assert(!p.calls.some(c=>c.op==='registry.write'));}
});
await test('Game Bar controller stale configuration generation stops startup before Windows dispatch',async()=>{
 p.hooks.settingsRead=()=>{p.invalidate();};await assert.rejects(gameBar.ensureGameBarControllerStartup(),/stale generation/);assert.equal(p.calls.length,0);
});
await test('Game Bar controller explicit action revokes a delayed default startup read',async()=>{
 let enter,release;const entered=new Promise(resolve=>{enter=resolve;}),hold=new Promise(resolve=>{release=resolve;});p.state.gameBarControllerWindows=0;
 p.hooks.settingsRead=async()=>{p.hooks.settingsRead=undefined;enter();await hold;};const startup=gameBar.ensureGameBarControllerStartup();const rejected=assert.rejects(startup,/用户操作接管/);await waitSignal(entered,'Game Bar startup read');const explicit=gameBar.setGameBarControllerEnabled(true);release();await rejected;await explicit;assert.equal(p.state.gameBarControllerWindows,1);assert.deepEqual(p.calls.filter(c=>c.op==='registry.write').map(c=>c.value.value),[1]);
});
await test('Game Bar controller generation change during registry read stops the old write',async()=>{
 p.state.gameBarControllerWindows=1;p.hooks.gameBarRead=()=>{p.invalidate();};await assert.rejects(gameBar.ensureGameBarControllerStartup(),/stale generation/);assert(!p.calls.some(c=>c.op==='registry.write'));
});
await test('Game Bar controller Windows failure retains explicit intent for a later startup and leaves Steam untouched',async()=>{
 p.state.gameBarControllerWindows=1;p.state.gameBarWriteRejected=true;await assert.rejects(gameBar.setGameBarControllerEnabled(false),/未确认/);assert.equal(p.settings.startupDesired.gameBarControllerOnBoot,false);assert.equal(p.state.gameBarControllerWindows,1);p.invalidate();p.state.gameBarWriteRejected=false;await gameBar.ensureGameBarControllerStartup();assert.equal(p.state.gameBarControllerWindows,0);assert(p.calls.every(c=>['settings.update','registry.read','registry.write'].includes(c.op)));
});
await test('Game Bar controller confirmed startup broadcasts once without a registry polling subscriber',async()=>{
 const notices=[];const listener=event=>notices.push(event.detail);window.addEventListener(gameBar.GAME_BAR_CONTROLLER_CHANGED_EVENT,listener);
 try{p.state.gameBarControllerWindows=1;await gameBar.ensureGameBarControllerStartup();await gameBar.ensureGameBarControllerStartup();assert.deepEqual(notices,[{enabled:false}]);assert.equal(p.calls.filter(c=>c.op==='registry.write').length,1);}finally{window.removeEventListener(gameBar.GAME_BAR_CONTROLLER_CHANGED_EVENT,listener);}
});
await test('Game Bar controller rejected readback never broadcasts a successful change',async()=>{
 const notices=[];const listener=event=>notices.push(event.detail);window.addEventListener(gameBar.GAME_BAR_CONTROLLER_CHANGED_EVENT,listener);
 try{p.state.gameBarControllerWindows=1;p.state.gameBarNoReadback=true;await assert.rejects(gameBar.ensureGameBarControllerStartup(),/回读不一致/);assert.deepEqual(notices,[]);}finally{window.removeEventListener(gameBar.GAME_BAR_CONTROLLER_CHANGED_EVENT,listener);}
});
Object.assign(report.sourceHashes,fingerprints(['tools/boot_modes_fixture/preparation.mjs']));
const {runInputReceiptCases,confirmInputReceipt}=await import('./boot_modes_fixture/inputReceipts.mjs');
await runInputReceiptCases({test,out,runtimeBundle});
const {runLegacyOriginCases}=await import('./boot_modes_fixture/legacyOrigin.mjs');
await runLegacyOriginCases({test,out,runtimeBundle});
await test('ordinary performance and independent frame owners project exact global leaves through actual repository in four modes',async()=>{
 const directory=path.join(out,'performance-preferences-owner');fs.mkdirSync(directory);
  const started=performance.now(),execution=spawnSync(process.execPath,['tools/continuous_performance_selftest.mjs',directory,'--boot-preferences-only'],{cwd:process.cwd(),windowsHide:true,encoding:'utf8',timeout:30000});
 fs.writeFileSync(path.join(directory,'owner.stdout.log'),execution.stdout??'',{flag:'wx'});fs.writeFileSync(path.join(directory,'owner.stderr.log'),execution.stderr??'',{flag:'wx'});
 fs.writeFileSync(path.join(directory,'PROCESS.json'),JSON.stringify({status:execution.status,signal:execution.signal,error:execution.error?.message,elapsedMs:performance.now()-started},null,2),{flag:'wx'});
 assert.equal(execution.status,0,execution.stdout+execution.stderr);
 const report=JSON.parse(fs.readFileSync(path.join(directory,'performance-preferences.json'),'utf8'));
 assert.equal(report.pass,true);assert.equal(report.rows.length,173);return {actualOwnerCases:173,completePerformanceModule:true,actualRepository:true,nativeAndHardwareOwners:'memory only'};
});

const baseline=()=>b.captureBootModeSnapshot();
const fresh=async()=>m.createBootModeDocument(await baseline());
const stored=doc=>{p.settings.extensions.bootModes=structuredClone(doc);};
const opNames=()=>p.calls.map(c=>c.op);
const changes=()=>p.calls.filter(c=>c.op!=='settings.update'&&c.op!=='ipc.bootMode.startup.identity');
await test('boot sleep restoration carries explicit origin through guard and plan writers in four modes',async()=>{
 if(process.argv.includes('--sleep-static-config')){assert(!process.argv.some(arg=>arg.startsWith('--sleep-guard-owner-')),'Choose static preference or guard owner scope');const {runSleepStaticBootPreferences}=await import('./boot_modes_fixture/sleepTimeouts.mjs');return await runSleepStaticBootPreferences(out,process.argv.slice(2),{native:globalThis.__bootFirstEditorNative});}
 if(process.argv.includes('--sleep-guard-owner-mutex-only')&&!process.argv.includes('--sleep-guard-owner-native'))throw Error('Sleep guard mutex-only requires native scope');
 if(process.argv.includes('--sleep-guard-owner-before')&&!process.argv.includes('--sleep-guard-owner-native'))throw Error('Sleep guard before control requires its native scope');
 if(process.argv.includes('--sleep-guard-owner-native')){const {runSleepGuardOwnerNative}=await import('./boot_modes_fixture/sleepGuardOwner.mjs');return await runSleepGuardOwnerNative(out,process.argv.slice(2));}
 const rows=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();for(const scene of Object.values(doc.scenes))delete scene.overrides.sleep;
  doc.global.sleep.config.pauseGameOnSleep=!p.settings.sleep.pauseGameOnSleep;
  doc.global.sleep.timeouts={...doc.global.sleep.timeouts,acScreen:1};
  doc.global.sleep.acButton=3;doc.global.sleep.dcButton=3;doc.global.sleep.hibernate=true;doc.global.sleep.hibernateSize=60;
  doc.global.sleep.config.sleepPowerPlanOptimizationEnabled=!doc.global.sleep.config.sleepPowerPlanOptimizationEnabled;stored(doc);
  await startMode(id,doc);
  assert.deepEqual(p.state.sleepOrigins.map(row=>row.part).sort(),['button','button','guard','hibernate','plan','size','timeout']);
  assert(p.state.sleepOrigins.every(row=>row.origin==='boot-mode'));
  rows.push({mode:id,guardOrigin:'boot-mode',planOrigin:'boot-mode'});
 }
 return rows;
});
await test('display startup writes retain boot origin and require actual mode and brightness acceptance',async()=>{
 const old={id:'1280x800@60/0',width:1280,height:800,refresh:60,orientation:0},mode={id:'1920x1080@120/0',width:1920,height:1080,refresh:120,orientation:0},rows=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();p.state.displayModes={current:old.id,modes:[old,mode]};const doc=await fresh();for(const scene of Object.values(doc.scenes))delete scene.overrides.display;
  doc.global.display={brightness:50,resolution:'1920x1080',hz:'120'};stored(doc);const result=await startMode(id,doc);
  assert.equal(p.calls.filter(c=>c.op==='display.set').length,1);assert.equal(result.receipt.modules.display.state,'active');
  assert(p.state.preferenceOrigins.filter(row=>row.part==='display').every(row=>row.origin==='boot-mode'));
  assert.deepEqual(result.document.global.display,doc.global.display);rows.push({mode:id,origin:'boot-mode'});
 }
 for(const reply of [{...mode,ok:true,width:1280},{...mode}]){
  p.reset();p.state.displayModes={current:old.id,modes:[old,mode]};const doc=await fresh();doc.global.display={brightness:50,resolution:'1920x1080',hz:'120'};stored(doc);p.hooks.displayResult=()=>reply;
  const result=await startMode('global',doc);assert.equal(result.receipt.modules.display.state,'waiting');
 }
 p.reset();const doc=await fresh();doc.global.display.brightness=75;stored(doc);p.hooks.brightnessResult=()=>({saved:true,applied:false,reason:'fixture plan saved but inactive'});
 const result=await startMode('global',doc);assert.equal(result.receipt.modules.display.state,'waiting');assert(result.receipt.pending.some(v=>v.includes('plan saved')));
 return {fourModes:rows,matchingModeIdAloneIsNotProof:true,planSaveIsNotApplication:true};
});
await test('actual display platform rejects invented brightness and retains saved but inactive power-plan receipts',async()=>{
 assert(!process.argv.includes('--display-owner-before')||process.argv.includes('--display-boot-owner-native'),'Display before control requires its native scope');
 if(process.argv.includes('--display-boot-owner-native')){assert(!process.argv.includes('--shell-boot-owner-native')&&!process.argv.includes('--brightness-boot-owner'),'Choose one explicit display, brightness or shell owner scope');const {runDisplayOwnerNative}=await import('./boot_modes_fixture/displayOwner.mjs');return await runDisplayOwnerNative(out,process.argv.slice(2));}
 assert(!(process.argv.includes('--shell-boot-owner-native')&&process.argv.includes('--brightness-boot-owner')),'Choose one explicit brightness memory or native shell scope');
 assert(!process.argv.includes('--shell-owner-before')||process.argv.includes('--shell-boot-owner-native'),'Native before control requires its original shell scope');
 if(process.argv.includes('--shell-boot-owner-native')){
  const {runShellOwnerNative}=await import('./boot_modes_fixture/brightnessOwner.mjs');return await runShellOwnerNative(out,process.argv.slice(2));
 }
 if(process.argv.includes('--brightness-boot-owner')){
  const {runBrightnessOwner}=await import('./boot_modes_fixture/brightnessOwner.mjs');return await runBrightnessOwner(out,process.argv.slice(2));
 }
 const directory=path.join(out,'actual-display-platform');fs.mkdirSync(directory,{recursive:true});
 const q=await runtimeBundle(directory,{actualDisplayPlatform:true}),port=q.ports;port.reset();
 for(const file of q.inputs)if(!report.sourceHashes[file])Object.assign(report.sourceHashes,fingerprints([file]));
 const mode={id:'1920x1080@120/0',width:1920,height:1080,refresh:120,orientation:0};port.state.displayModes={current:mode.id,modes:[mode]};
 for(const raw of [null,'',' ',false,{},'bad',-1,101,1.5]){port.state.brightnessRaw=raw;assert.equal((await q.platform.readBootDisplay()).brightness,null);}
 for(const [raw,value]of [[0,0],['0',0],['0x64',100],[75,75]]){port.state.brightnessRaw=raw;assert.equal((await q.platform.readBootDisplay()).brightness,value);}
 port.state.activeScheme='other-plan';port.calls.length=0;assert.equal((await q.platform.readBootDisplay()).brightness,null);assert.equal(port.calls.filter(c=>c.op==='registry.read').length,0);
 port.state.activeScheme=port.PW.YEMAN;port.state.powerMode='dc';port.calls.length=0;
 assert.deepEqual(await q.platform.applyBootBrightness(75),{saved:true,applied:true});assert.equal(port.calls.find(c=>c.op==='brightness.power.write').value.mode,'dc');
 assert.equal(port.calls.filter(c=>c.op==='app.run').length,1);
 port.state.brightnessRaw=null;port.state.brightnessWriteNoReadback=true;await assert.rejects(q.platform.applyBootBrightness(0),/回读不一致/);delete port.state.brightnessWriteNoReadback;
 port.state.powerMode=null;const writes=port.calls.filter(c=>c.op==='brightness.power.write').length;await assert.rejects(q.platform.applyBootBrightness(50),/供电状态未确认/);assert.equal(port.calls.filter(c=>c.op==='brightness.power.write').length,writes);
 port.state.powerMode='ac';port.state.activeScheme='other-plan';port.calls.length=0;const inactive=await q.platform.applyBootBrightness(75);assert.equal(inactive.saved,true);assert.equal(inactive.applied,false);assert.equal(port.calls.filter(c=>c.op==='app.run').length,0);
 port.state.activeScheme=port.PW.YEMAN;port.state.shellExit=1;const rejected=await q.platform.applyBootBrightness(75);assert.equal(rejected.saved,true);assert.equal(rejected.applied,false);
 port.calls.length=0;await q.realDisplay.setMode(mode);await q.realDisplay.setMode(mode,{fields:['hz']});await q.realDisplay.setMode(mode,{origin:'boot-mode'});
 const args=port.calls.filter(c=>c.op==='ipc.display.setMode').map(c=>c.value);assert.deepEqual(args,[mode,{...mode,preferenceFields:['hz']},{...mode,origin:'boot-mode'}]);
 return {actualPlatformAndApi:true,invalidBrightnessValues:9,validBrightnessValues:4,unknownPowerRefused:true,savedVersusApplied:true,ordinaryWireCompatible:true};
});
await test('charge startup writes preserve ordinary global intent and distinguish firmware release from software ownership',async()=>{
 const rows=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();for(const scene of Object.values(doc.scenes))delete scene.overrides.charge;
  doc.global.charge={enabled:true,limit:80};stored(doc);const result=await startMode(id,doc);
  assert.equal(p.calls.filter(c=>c.op==='charge.set').length,1);assert.equal(result.receipt.modules.charge.state,'active');
  assert(p.state.preferenceOrigins.filter(row=>row.part==='charge').every(row=>row.origin==='boot-mode'));
  assert.deepEqual(result.document.global.charge,{enabled:true,limit:80});rows.push({mode:id,origin:'boot-mode',requestedLimit:80});
 }
 for(const software of [false,true]){
  p.reset();Object.assign(p.state.charge,{managed:true,requestedLimit:80,currentLimit:80,kind:software?'software-bypass-threshold':'firmware-percent'});
  const doc=await fresh();doc.global.charge={enabled:false,limit:100};stored(doc);const result=await startMode('global',doc);
  assert.equal(p.calls.filter(c=>c.op==='charge.release').length,software?1:0);
  const writes=p.calls.filter(c=>c.op==='charge.set');assert.equal(writes.length,software?0:1);if(!software)assert.equal(writes[0].value.limit,100);
  assert.equal(result.receipt.modules.charge.state,'disabled');
  assert(p.state.preferenceOrigins.filter(row=>row.part==='charge').every(row=>row.origin==='boot-mode'));
  assert.equal((await baseline()).charge.enabled,false,'100% firmware preference must not become an active charge cap');
 }
 p.reset();const doc=await fresh();doc.global.charge={enabled:true,limit:80};stored(doc);
 p.hooks.chargeResult=()=>({...p.state.charge,ok:true,requestedLimit:60,currentLimit:60,verification:'readback'});
 const pending=await startMode('global',doc);assert.equal(pending.receipt.modules.charge.state,'waiting');assert(pending.receipt.pending.some(v=>v.includes('充电限制')));
 p.reset();p.state.charge.ok=false;const unknown=await baseline();assert.equal(unknown.charge.enabled,null);assert.equal(unknown.charge.limit,null);
 const disabled=m.createBootModeDocument(unknown);disabled.global.charge={enabled:false,limit:100};stored(disabled);
 const unread=await startMode('global',disabled);assert.equal(unread.receipt.modules.charge.state,'waiting');assert.equal(p.calls.filter(c=>c.op==='charge.set'||c.op==='charge.release').length,0);
 p.reset();Object.assign(p.state.charge,{managed:false,requestedLimit:80,currentLimit:80});const untouched=await fresh();untouched.global.charge={enabled:false,limit:100};stored(untouched);
 const off=await startMode('global',untouched);assert.equal(off.receipt.modules.charge.state,'disabled');assert.equal(p.calls.filter(c=>c.op==='charge.set'||c.op==='charge.release').length,0);
 return {fourModes:rows,firmwareUsesFenced100:true,softwareUsesOwnedRelease:true,mismatchedOwnerAckNotActive:true,failedReadDoesNotInventOff:true};
});
await test('Decky and wake password startup writes retain boot origin and failed preference save stays visible',async()=>{
 const automatic=process.argv.includes('--automatic-recovery-owner-native');
 if(process.argv.includes('--automatic-recovery-owner-transport-compile-only')&&!process.argv.includes('--automatic-recovery-owner-transport'))throw Error('Compile-only requires explicit transport scope');
 if(process.argv.some(arg=>arg==='--automatic-recovery-owner-worker'||arg==='--automatic-recovery-owner-transport')&&!automatic)throw Error('Automatic recovery worker/transport requires native scope');
 if(process.argv.includes('--automatic-recovery-owner-before')&&!automatic)throw Error('Automatic recovery before requires native scope');
 if(automatic&&process.argv.some(arg=>arg.startsWith('--wake-password-owner')))throw Error('Choose one original native owner scope');
 if(automatic){const {runAutomaticRecoveryOwnerNative}=await import('./boot_modes_fixture/automaticRecoveryOwner.mjs');return await runAutomaticRecoveryOwnerNative(out,process.argv.slice(2));}
 if(process.argv.some(arg=>arg.startsWith('--wake-password-owner-failure')||arg.startsWith('--wake-password-owner-registry'))&&!process.argv.includes('--wake-password-owner-native'))throw Error('Wake-password failure modifiers require native scope');
 if(process.argv.includes('--wake-password-owner-mutex-only')&&!process.argv.includes('--wake-password-owner-native'))throw Error('Wake-password mutex-only requires native scope');
 if(process.argv.includes('--wake-password-owner-before')&&!process.argv.includes('--wake-password-owner-native'))throw Error('Wake-password before control requires its native scope');
 if(process.argv.includes('--wake-password-owner-native')){const {runWakePasswordOwnerNative}=await import('./boot_modes_fixture/wakePasswordOwner.mjs');return await runWakePasswordOwnerNative(out,process.argv.slice(2));}
 const rows=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();for(const scene of Object.values(doc.scenes)){delete scene.overrides.sleep;delete scene.overrides.steamDecky;}
  doc.global.sleep.config.wakePasswordEnabled=!doc.global.sleep.config.wakePasswordEnabled;doc.global.sleep.steamAutoRecovery=!doc.global.sleep.steamAutoRecovery;doc.global.steamDecky.enabled=true;stored(doc);
  const applied=await startMode(id,doc);
  assert.deepEqual(p.state.preferenceOrigins.map(row=>row.part).sort(),['decky','recovery','wake-password']);
  assert(p.state.preferenceOrigins.every(row=>row.origin==='boot-mode'));
  assert(!applied.receipt.pending.some(row=>row.includes('唤醒密码')));
  rows.push({mode:id,deckyOrigin:'boot-mode',wakePasswordOrigin:'boot-mode',recoveryOrigin:'boot-mode'});
 }
 p.reset();const doc=await fresh();doc.global.sleep.config.wakePasswordEnabled=!doc.global.sleep.config.wakePasswordEnabled;stored(doc);p.faults['wakePassword.save']=1;
 const failed=await startMode('global',doc);assert(failed.receipt.pending.some(row=>row.includes('唤醒密码')));assert.equal(failed.receipt.modules.sleep.state,'failed');
 return {origins:rows,hardwareAckAloneDoesNotProvePreferenceSaved:true};
});
await test('module receipts separate saved input and disabled launch selections from confirmed output',async()=>{
 if(process.argv.includes('--empty-appearance-receipt')){const {runEmptyAppearanceReceipt}=await import('./boot_modes_fixture/preferenceOwner.mjs');return await runEmptyAppearanceReceipt(out,process.argv.slice(2),{runtimeBundle});}
 const rows=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();p.state.fanIntent=true;p.state.fanReady=true;p.state.monitor={...p.state.monitor,enabled:true,output:'ymcc',ymccEnabled:true,rtssEnabled:false};
  p.hooks.deckyGet=async()=>({enabled:true,phase:'loader-started',mirrorConnected:true,powerReady:true});
  const doc=await fresh();
  for(const scene of Object.values(doc.scenes)){delete scene.overrides.fan;delete scene.overrides.monitor;delete scene.overrides.steamDecky;}
  doc.global.fan={enabled:false};doc.global.monitor.preferences={...doc.global.monitor.preferences,enabled:false};doc.global.steamDecky.enabled=false;
  for(const scene of Object.values(doc.scenes)){delete scene.overrides.input;delete scene.overrides.gyro;delete scene.overrides.audioOptimization;delete scene.overrides.frameGeneration;}
  doc.global.input={...doc.global.input,persona:'elite',mouse:'off',outputTarget:{...doc.global.input.outputTarget,persona:'elite',buttonMappingEnabled:true}};
  doc.global.gyro.motion.motionSensitivityX=2.25;doc.global.audioOptimization.enabled=false;doc.global.frameGeneration.enabled=false;
  stored(doc);const {receipt}=await startMode(id,doc);
  assert.deepEqual(Object.keys(receipt.modules).sort(),[...m.BOOT_MODE_MODULES].sort());
  assert.equal(receipt.modules.input.state,'saved');assert.equal(receipt.modules.gyro.state,'saved');
  assert.equal(receipt.modules.audioOptimization.state,'disabled');assert.equal(receipt.modules.frameGeneration.state,'disabled');
  for(const [module,label] of [['monitor','监控'],['steamDecky','Decky']]){assert.equal(receipt.modules[module].state,'disabled',module+' OFF must not become active');assert(!receipt.applied.includes(label));}
  assert(!receipt.applied.includes('全局手柄与陀螺仪'));assert(!receipt.applied.includes('掌机音质优化'));assert(!receipt.applied.includes('小黄鸭联动'));
  assert(!p.calls.some(row=>row.op==='ipc.fxsound.setEnabled'||row.op==='app.launch'));
  const result={schema:1,boot:'b',mode:id,sourceRevision:0,revision:1,status:'applied',applied:receipt.applied,skipped:[],pending:[],modules:receipt.modules};
  assert.deepEqual(m.normalizeBootStartupResult(result,'b').modules,receipt.modules);
  assert.equal(m.normalizeBootStartupResult({...result,modules:{monitor:{state:'fabricated-ready'}}},'b'),undefined);
  assert.equal(m.normalizeBootStartupResult({...result,modules:{monitor:{state:'active',requested:[]}}},'b'),undefined);
  assert.equal(receipt.modules.fan,undefined);assert.equal(p.state.fanIntent,true,'Mode disabled the common live fan session');
  rows.push({mode:id,modules:m.BOOT_MODE_MODULES.length,inputAndGyro:'saved',offLaunches:'disabled',offSoftwareLaunches:0});
 }
 return rows;
});
await test('monitor owner acknowledgement repairs matching startup evidence without replay and survives renderer restart',async()=>{
 if(process.argv.includes('--monitor-generation-native-files')){
  const {runMonitorGeneration}=await import('./boot_modes_fixture/monitorGeneration.mjs');
  return await runMonitorGeneration(out,process.argv.slice(2),{native:globalThis.__bootModeSaveNative,bootModesSource:sourceIndex<0?undefined:path.resolve(process.argv[sourceIndex+1])});
 }
 const rows=[];
 for(const id of m.BOOT_MODE_IDS)for(const enabled of [true,false]){
  const a=await runtimeBundle(out),q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
  doc.boot=id;doc.bootEnabled=true;for(const scene of Object.values(doc.scenes))delete scene.overrides.monitor;
  doc.global.monitor.preferences={...doc.global.monitor.preferences,enabled,output:'ymcc',ymccEnabled:enabled,rtssEnabled:false};
  q.settings.extensions.bootModes=structuredClone(doc);q.faults['monitor.pending']=1;
  const stop=a.boot.observeBootModeRuntimeReceipts();
  try{
   await a.boot.restoreSelectedBootMode();
   const result=q.settings.extensions.bootModesStartupResult,target=structuredClone(result.modules.monitor.requested),ack={ok:true,applied:enabled?'ymcc':'off'};
   assert.equal(result.modules.monitor.state,'waiting');assert(result.pending.some(row=>row.startsWith('监控：')));
   const writes=()=>q.calls.filter(row=>row.op==='settings.update').length,beforeWrites=writes();
   const submitted=q.state.monitorApplyOptions.bootContext.extensions.bootModesClaimWriteId;
   assert.equal(typeof submitted,'string');
   const replacement=`replacement-${id}-${enabled}`;
   q.settings.extensions.bootModesClaimWriteId=replacement;
   const replacementSettings=structuredClone(q.settings);
   for(const intent of [{startupReceiptOwner:{boot:result.boot,writeId:submitted}},
    {startupReceiptOwner:{boot:result.boot,writeId:null}},{}]){
    await q.emitMonitorOutputApplied(target,ack,intent);
    assert.deepEqual(q.settings,replacementSettings,'An ACK from an old or unknown claim must not confirm a replacement with identical mode, revisions and output');
   }
   assert.equal(writes(),beforeWrites);
   await q.emitMonitorOutputApplied(target,{ok:false,applied:'unknown'});
   await q.emitMonitorOutputApplied(target,{ok:true,applied:'rtss'});
   await q.emitMonitorOutputApplied({...target,ymccTemplate:target.ymccTemplate==='A'?'B':'A'},ack);
   const saved=structuredClone(q.settings.rtss.monitorOutput);q.settings.rtss.monitorOutput={...target,enabled:!enabled};
   await q.emitMonitorOutputApplied(target,ack);q.settings.rtss.monitorOutput=saved;
   const observedBoot=q.settings.input.startupObservedBoot;q.settings.input.startupObservedBoot='different-boot';
   await q.emitMonitorOutputApplied(target,ack);q.settings.input.startupObservedBoot=observedBoot;
   const mode=q.settings.extensions.bootModes;mode.current=id==='global'?'steam':'global';
   await q.emitMonitorOutputApplied(target,ack);mode.current=id;
   if(process.argv.includes('--future-schema-boundary')){
    mode.schema=99;const future=structuredClone(q.settings);
    await q.emitMonitorOutputApplied(target,ack);
    assert.deepEqual(q.settings,future,'monitor owner must preserve future mode schema');mode.schema=1;
   }
   assert.equal(writes(),beforeWrites,'unrelated or stale ACK must not write any receipt');
   // Next-boot draft edits do not cancel the pending evidence for this startup.
   mode.revision++;mode.boot=id==='global'?'xbox':'global';
   await q.emitMonitorOutputApplied(target,ack);
   const confirmed=q.settings.extensions.bootModesStartupResult;
   assert.equal(confirmed.modules.monitor.state,enabled?'active':'disabled');assert(!confirmed.pending.some(row=>row.startsWith('监控：')));
   assert.equal(confirmed.applied.includes('监控'),enabled);assert.equal(confirmed.revision,result.revision);assert.equal(confirmed.sourceRevision,result.sourceRevision);
   assert.equal(q.calls.filter(row=>row.op==='monitor.apply').length,1,'ACK handling must not replay monitor or the scene');
   const afterWrites=writes();await q.emitMonitorOutputApplied(target,ack);assert.equal(writes(),afterWrites);
   // A fresh renderer consumes the durable pending record, without reapplying startup.
   const durable=structuredClone(q.settings);durable.extensions.bootModesStartupResult=structuredClone(result);
   const restarted=await runtimeBundle(out);Object.assign(restarted.ports.settings,durable);
   const stopRestarted=restarted.boot.observeBootModeRuntimeReceipts();
   try{
    await restarted.boot.restoreSelectedBootMode();await restarted.ports.emitMonitorOutputApplied(target,ack);
    assert.equal(restarted.ports.settings.extensions.bootModesStartupResult.modules.monitor.state,enabled?'active':'disabled');
    assert.equal(restarted.ports.calls.filter(row=>row.op==='monitor.apply'||row.op==='input.cas'||row.op==='app.launch').length,0);
   }finally{stopRestarted();}
   rows.push({mode:id,enabled,staleOrUnrelatedRejected:9,monitorApplies:1,rendererRestartReplays:0});
  }finally{stop();assert.equal(q.ipcListenerCount('fixture.monitor-applied'),0);a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();}
 }
 return rows;
});
await test('monitor owner acknowledgement remains confirmed during a later startup publication from another renderer',async()=>{
 const rows=[];for(const enabled of [true,false]){
 const a=await runtimeBundle(out),q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
 doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.monitor={preferences:{...doc.global.monitor.preferences,enabled,output:'ymcc',ymccEnabled:enabled,rtssEnabled:false}};
 doc.scenes.steam.overrides.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'scene',delay:1};
 q.settings.extensions.bootModes=structuredClone(doc);q.faults['monitor.pending']=1;
 try{
  await a.boot.restoreSelectedBootMode();const original=structuredClone(q.settings.extensions.bootModesStartupResult);
  const other=await runtimeBundle(out);Object.assign(other.ports.settings,structuredClone(q.settings));
  const stop=other.boot.observeBootModeRuntimeReceipts();
  try{await other.ports.emitMonitorOutputApplied(original.modules.monitor.requested,{ok:true,applied:enabled?'ymcc':'off'});}
  finally{stop();}
  q.settings.extensions.bootModesStartupResult=structuredClone(other.ports.settings.extensions.bootModesStartupResult);
  assert.equal(q.settings.extensions.bootModesStartupResult.modules.monitor.state,enabled?'active':'disabled');
  await new Promise(resolve=>setTimeout(resolve,1100));
  const final=q.settings.extensions.bootModesStartupResult;
  assert.equal(final.modules.monitor.state,enabled?'active':'disabled');assert.equal(final.applied.includes('监控'),enabled);assert(!final.pending.some(row=>row.startsWith('监控：')));
  assert.equal(q.calls.filter(row=>row.op==='app.launch').length,1);assert.equal(q.calls.filter(row=>row.op==='monitor.apply').length,1);
  rows.push({enabled,otherRendererAckPreserved:true,delayedAppRequests:1,monitorReplays:0});
 }finally{a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();}
 }return rows;
});
await test('late startup failure preserves completed module proof and marks not-yet-processed intent waiting',async()=>{
 const a=await runtimeBundle(out),q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
 doc.boot='global';doc.bootEnabled=true;
 doc.global.monitor.preferences={...doc.global.monitor.preferences,enabled:true,output:'ymcc',ymccEnabled:true,rtssEnabled:false};
 doc.global.display.brightness=75;q.settings.extensions.bootModes=structuredClone(doc);
 let changed=false;q.hooks.settingsRead=()=>{if(!changed&&q.calls.some(row=>row.op==='monitor.apply')){changed=true;const newer=structuredClone(q.settings.extensions.bootModes);newer.revision++;newer.global.background.ui.backgroundBlur=37;q.settings.extensions.bootModes=newer;}};
 const previousNow=Object.getOwnPropertyDescriptor(performance,'now');let clock=0;
 Object.defineProperty(performance,'now',{configurable:true,value:()=>clock});q.hooks.deckyGet=()=>{clock+=1250;return {enabled:false,phase:'off'};};
 try{
  await assert.rejects(()=>a.boot.restoreSelectedBootMode(),/其他窗口|当前模式已/);
  const result=q.settings.extensions.bootModesStartupResult;
  assert.equal(result.status,'partial');assert.equal(result.modules.monitor.state,'active');assert(result.applied.includes('监控'));
  assert.equal(result.modules.display.state,'waiting');assert(!result.applied.includes('画面'));assert.equal(q.calls.filter(row=>row.op==='monitor.apply').length,1);
  assert.match(result.error,/其他窗口|当前模式已/);
  const diff=JSON.parse(result.error.split('boot-owner-diff:')[1]?.split(' · ')[0]??'null');assert(diff,'original rejection must explain its existing read');
  assert.equal(diff.stage,'module:display:read');assert.deepEqual(diff.fields,['mode']);assert.deepEqual(diff.modeRevision,[doc.revision+1,doc.revision+2]);assert.deepEqual(diff.globalModules,['background']);
  const timing=JSON.parse(result.error.split('boot-stages:')[1]??'null');assert(timing,'original persisted failure must retain bounded stage costs');
  assert.equal(timing.at,'module:display:read');assert.equal(timing.elapsedMs,2500);assert(timing.slow.length<=3);
  for(const name of ['capture:owners','apply-capture:owners'])assert(timing.slow.some(([stage,ms])=>stage===name&&ms===1250));
  assert(result.error.length<=1000);assert(!q.calls.some(row=>row.op==='brightness.set'));assert.equal(q.settings.extensions.bootModes.global.background.ui.backgroundBlur,37);
  return {monitorProofPreserved:true,remainingDisplayIntent:'waiting',replays:0,firstOwnerDifference:diff,boundedStageCosts:timing};
 }finally{if(previousNow)Object.defineProperty(performance,'now',previousNow);else delete performance.now;delete q.hooks.deckyGet;delete q.hooks.settingsRead;a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();}
});
await test('four-mode touch preferences commit one complete bank and a failed batch preserves every persona',async()=>{
 if(process.argv.includes('--touch-profile-owner-boundaries')){
  const rows=[];
  for(const mode of m.BOOT_MODE_IDS)for(const boundary of ['unknown-boot','input-revision','unrelated-fan']){
   p.reset();for(const persona of ['disabled','elite','steamdeck','dualsense-edge'])p.state.touch[persona]={...p.screenTouchpadDefaults(persona),transparency:80};
   const document=await fresh();for(const scene of Object.values(document.scenes))delete scene.overrides.touch;
   for(const profile of Object.values(document.global.touch.profiles))profile.transparency=65;stored(document);
   if(boundary==='unknown-boot')p.settings.input.startupObservedBoot='';
   if(boundary==='input-revision')p.hooks.touchWrite=()=>{p.settings.input.revision++;};
   if(boundary==='unrelated-fan')p.hooks.touchWrite=()=>{p.settings.fan.preset='aggressive';};
   const before=structuredClone(p.state.touch);let result,error;
   try{result=await startMode(mode,document);}catch(e){error=String(e);}finally{delete p.hooks.touchWrite;}
   const requests=p.calls.filter(row=>row.op==='touch.saveProfiles');
   if(boundary==='unknown-boot'){assert.equal(error,undefined);assert.equal(requests.length,0,'Unknown startup degraded into an ordinary touch save');assert(result.receipt.pending.some(text=>text.includes('Windows')));assert.deepEqual(p.state.touch,before);}
   else if(boundary==='input-revision'){assert(error,'Late input owner change was accepted');assert.equal(requests.length,1);assert.deepEqual(p.state.touch,before,'Late old mode changed a persona');}
   else{assert.equal(error,undefined);assert.equal(requests.length,1);for(const persona of Object.keys(before))assert.equal(p.state.touch[persona].transparency,65);assert.equal(p.settings.fan.preset,'aggressive');assert.equal(result.receipt.modules.touch.state,'saved','Settings-only proof was promoted to active');}
   rows.push({mode,boundary,requests:requests.length,error:error??null,noOrdinaryStartupFallback:boundary==='unknown-boot',lateOldWriteRejected:boundary==='input-revision',unrelatedPreferenceRetained:boundary==='unrelated-fan'});
  }
  fs.writeFileSync(path.join(out,'touch-owner-boundaries.json'),JSON.stringify({rows,actualRows:rows.length},null,2),{flag:'wx'});return {rows,scope:'Actual boot-mode producer and original memory native boundaries; final native lock independently verified by native-touch consumer'};
 }
 const rows=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();for(const persona of ['disabled','elite','steamdeck','dualsense-edge'])p.state.touch[persona]={...p.screenTouchpadDefaults(persona),transparency:80};
  const doc=await fresh();for(const scene of Object.values(doc.scenes))delete scene.overrides.touch;
  for(const profile of Object.values(doc.global.touch.profiles))profile.transparency=65;
  stored(doc);const before=structuredClone(p.state.touch);p.faults['touch.saveProfiles']=1;
  const failed=await startMode(id,doc);assert.deepEqual(p.state.touch,before);assert.equal(p.calls.filter(c=>c.op==='touch.saveProfiles').length,1);assert.equal(p.calls.filter(c=>c.op==='touch.set').length,0);assert.ok(failed.receipt.pending.some(v=>v.includes('touch.saveProfiles')));
  p.calls.length=0;const retried=await startMode(id,failed.document);assert.equal(p.calls.filter(c=>c.op==='touch.saveProfiles').length,1);
  const context=p.calls.find(c=>c.op==='touch.saveProfiles').value.bootContext;
  assert.deepEqual(Object.keys(context).sort(),['boot','extensions','input','mode','revision']);
  assert.equal(context.input.revision,p.settings.input.revision);assert.equal(context.mode,id);assert.equal(context.revision,retried.document.revision);
  assert.equal(context.extensions.bootModes.current,id);assert.equal(context.extensions.bootModes.revision,context.revision);
  for(const persona of ['disabled','elite','steamdeck','dualsense-edge'])assert.equal(p.state.touch[persona].transparency,65);
  assert.ok(!retried.receipt.pending.some(v=>v.includes('屏幕触摸')));rows.push({mode:id,failedBatchDidNotPartlySave:true,savedProfiles:4,commits:1});
 }
 if(process.argv.includes('--touch-profile-wire')){const {runTouchProfileFrontendWire}=await import('./boot_modes_fixture/touchProfilesFileCases.mjs');return {rows,wire:await runTouchProfileFrontendWire(out)};}
 return rows;
});
await test('ordinary sleep timeouts follow global preferences across four modes without capturing scene writes',async()=>{
 const {compileSettingsConsumer,createSettingsFixture}=await import('./fixtures/settings_baseline.mjs');
 const {createSleepTimeoutOwnerFixture,createSleepTimeoutUiFixture}=await import('./boot_modes_fixture/sleepTimeouts.mjs');
 const code=await compileSettingsConsumer(process.cwd()),source=fs.readFileSync('src/bridge/yeman.ts','utf8'),rows=[];
 for(const mode of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();doc.boot=mode;doc.global.sleep.timeouts={acScreen:9,dcScreen:9,acSleep:9,dcSleep:9,acHibernate:9,dcHibernate:9};
  if(mode!=='global')doc.scenes[mode].overrides.sleep={timeouts:{acScreen:29}};
  const f=createSettingsFixture(code,{extensions:{bootModes:doc}});await f.repo.loadSettings();
  const owner=createSleepTimeoutOwnerFixture(source,{updateSettings:f.repo.updateSettings});
  for(const [index,[field,ac]]of [['screen',true],['screen',false],['sleep',true],['sleep',false],['hibernate',true],['hibernate',false]].entries()){
   const key=(ac?'ac':'dc')+field[0].toUpperCase()+field.slice(1),before=structuredClone(f.document().extensions.bootModes);
   const result=await owner.api.setSleepTimeout(field,ac,index+1),saved=f.document().extensions.bootModes;
   assert.equal(saved.global.sleep.timeouts[key],index+1,'ordinary timeout success did not update global startup preference');
   assert.equal(result.saved,true);assert.equal(result.applied,true);assert.equal(result.preferenceSaved,true);assert.equal(saved.revision,before.revision+1);assert.deepEqual(saved.scenes,before.scenes);
   for(const other of Object.keys(before.global.sleep.timeouts))if(other!==key)assert.equal(saved.global.sleep.timeouts[other],before.global.sleep.timeouts[other]);
   const global=structuredClone(saved.global.sleep),revision=saved.revision;
   await owner.api.setSleepTimeout(field,ac,17,{origin:'boot-mode'});await owner.api.setSleepTimeout(field,ac,18,{origin:'runtime'});
   assert.deepEqual(f.document().extensions.bootModes.global.sleep,global);assert.equal(f.document().extensions.bootModes.revision,revision);
  }
  for(const origin of ['invalid',null]){const count=owner.state.calls.length;await assert.rejects(()=>owner.api.setSleepTimeout('screen',true,8,{origin}));assert.equal(owner.state.calls.length,count);}
  const before=structuredClone(f.document().extensions.bootModes);f.state.writeFailure=true;
  let result=await owner.api.setSleepTimeout('screen',true,7);assert.equal(result.saved,true);assert.equal(result.applied,true);assert.equal(result.preferenceSaved,false);assert.match(result.preferenceReason,/开机全局超时偏好未保存/);assert.deepEqual(f.document().extensions.bootModes,before);
  const view=createSleepTimeoutUiFixture(owner);await view.functions.onSleepTimeout('screen',true,7);assert.equal(view.sleepTimeouts.acScreen,7);assert.match(view.msg.value,/开机全局超时偏好未保存/);assert.equal(view.busy.value,false);
  f.state.writeFailure=false;await view.functions.onSleepTimeout('screen',true,7);assert.equal(view.msg.value,'');assert.equal(f.document().extensions.bootModes.global.sleep.timeouts.acScreen,7);
  owner.state.activateExit=5;await assert.rejects(()=>owner.api.setSleepTimeout('screen',true,8),/已保存.*刷新未完成/);assert.equal(f.document().extensions.bootModes.global.sleep.timeouts.acScreen,8);owner.state.activateExit=0;
  owner.state.active=owner.other;result=await owner.api.setSleepTimeout('screen',true,9);assert.equal(result.saved,true);assert.equal(result.applied,false);assert.equal(result.preferenceSaved,true);
  const stable=structuredClone(f.document().extensions.bootModes);owner.state.readbackWrong=true;await assert.rejects(()=>owner.api.setSleepTimeout('screen',true,10),/回读不一致/);owner.state.readbackWrong=false;assert.deepEqual(f.document().extensions.bootModes,stable);
  owner.state.writeExit=5;await assert.rejects(()=>owner.api.setSleepTimeout('screen',true,10),/写入失败/);owner.state.writeExit=0;assert.deepEqual(f.document().extensions.bootModes,stable);
  const origin={origin:'boot-mode'};owner.state.afterWrite=()=>{origin.origin='ordinary';};await owner.api.setSleepTimeout('screen',true,11,origin);owner.state.afterWrite=null;assert.deepEqual(f.document().extensions.bootModes,stable,'in-flight origin mutation captured runtime values as global');
  owner.state.active=owner.api.PW.YEMAN;owner.state.beforeActivate=()=>{owner.state.active=owner.other;};result=await owner.api.setSleepTimeout('screen',true,12);owner.state.beforeActivate=null;assert.equal(result.applied,false);assert.equal(f.document().extensions.bootModes.global.sleep.timeouts.acScreen,12);
  let peer;owner.state.afterWrite=()=>{peer=f.repo.updateSettings(settings=>{settings.extensions.peerFuture={keep:true};settings.extensions.bootModes.scenes.steam.overrides.sleep={timeouts:{dcScreen:43}};settings.extensions.bootModes.revision++;});};await owner.api.setSleepTimeout('screen',true,13);await peer;owner.state.afterWrite=null;assert.equal(f.document().extensions.peerFuture.keep,true);assert.equal(f.document().extensions.bootModes.scenes.steam.overrides.sleep.timeouts.dcScreen,43);
  for(const kind of ['future','revision','sleep-null','timeouts-null','absent']){
   await f.repo.updateSettings(settings=>{if(kind==='absent')delete settings.extensions.bootModes;else{settings.extensions.bootModes=structuredClone(doc);if(kind==='future')settings.extensions.bootModes.schema=99;if(kind==='revision')settings.extensions.bootModes.revision=Number.MAX_SAFE_INTEGER;if(kind==='sleep-null')settings.extensions.bootModes.global.sleep=null;if(kind==='timeouts-null')settings.extensions.bootModes.global.sleep.timeouts=null;}});
   const value=structuredClone(f.document().extensions.bootModes),writes=f.state.writes.length;result=await owner.api.setSleepTimeout('screen',true,14);assert.equal(result.preferenceSaved,kind==='absent');assert.deepEqual(f.document().extensions.bootModes,value);assert.equal(f.state.writes.length,writes);
  }
  rows.push({mode,ordinaryFields:6,bootAndRuntimeOriginsIsolated:true,partialSaveShownWithoutReplay:true,inactivePlanSavedNotApplied:true});
 }
 return rows;
});
await test('ordinary sleep power preferences follow global across four modes without invented reads or scene capture',async()=>{
 if(process.argv.includes('--sleep-power-boot-owner')){const {runSleepPowerBootOwner}=await import('./boot_modes_fixture/sleepTimeouts.mjs');return await runSleepPowerBootOwner(out,process.argv.slice(2));}
 const {compileSettingsConsumer,createSettingsFixture}=await import('./fixtures/settings_baseline.mjs');
 const {createSleepPowerOwnerFixture,createSleepPowerUiFixture}=await import('./boot_modes_fixture/sleepTimeouts.mjs');
 const code=await compileSettingsConsumer(process.cwd()),source=fs.readFileSync('src/bridge/yeman.ts','utf8'),rows=[];
 for(const mode of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();doc.boot=mode;Object.assign(doc.global.sleep,{acButton:2,dcButton:2,hibernate:true,hibernateSize:40});
  if(mode!=='global')doc.scenes[mode].overrides.sleep={acButton:1,hibernateSize:70};
  const f=createSettingsFixture(code,{extensions:{bootModes:doc}});await f.repo.loadSettings();const owner=createSleepPowerOwnerFixture(source,{updateSettings:f.repo.updateSettings});
  for(const [key,value,write]of [
   ['acButton',3,options=>owner.api.setPowerBtnIdx(true,3,options)],
   ['dcButton',0,options=>owner.api.setPowerBtnIdx(false,0,options)],
   ['hibernate',false,options=>owner.api.setHibernate(false,options)],
   ['hibernateSize',60,options=>{owner.state.enabled=true;return owner.api.setHibernateSize(60,options);}]
  ]){
   const before=structuredClone(f.document().extensions.bootModes);const result=await write();let saved=f.document().extensions.bootModes;
   assert.equal(saved.global.sleep[key],value,'ordinary sleep power save did not update global startup preference');
   assert.equal(result.saved,true);assert.equal(result.preferenceSaved,true);assert.deepEqual(saved.scenes,before.scenes);assert.equal(saved.revision,before.revision+1);
   for(const other of Object.keys(before.global.sleep))if(other!==key)assert.deepEqual(saved.global.sleep[other],before.global.sleep[other]);
   const stable=structuredClone(saved);await write({origin:'boot-mode'});await write({origin:'runtime'});assert.deepEqual(f.document().extensions.bootModes,stable);
  }
  let before=structuredClone(f.document().extensions.bootModes);f.state.writeFailure=true;let result=await owner.api.setPowerBtnIdx(true,0);assert.equal(result.saved,true);assert.equal(result.preferenceSaved,false);assert.deepEqual(f.document().extensions.bootModes,before);
  f.state.writeFailure=false;result=await owner.api.setPowerBtnIdx(true,0);assert.equal(result.preferenceSaved,true);assert.equal(f.document().extensions.bootModes.global.sleep.acButton,0);
  for(const origin of ['invalid',null])for(const write of [()=>owner.api.setPowerBtnIdx(true,1,{origin}),()=>owner.api.setHibernate(true,{origin}),()=>owner.api.setHibernateSize(55,{origin})]){const calls=owner.state.calls.length;await assert.rejects(write);assert.equal(owner.state.calls.length,calls);}
  for(const raw of [null,undefined,'',false,{},99]){owner.state.buttons.ac=raw;assert.equal(await owner.api.getPowerBtnIdx(true),null,'unknown button read became a default action');owner.state.buttons.dc=raw;assert.equal(await owner.api.getPowerBtnIdx(false),null,'unknown DC read borrowed the AC action');}
  owner.state.buttons.ac=0;owner.state.buttons.dc=null;assert.equal(await owner.api.getPowerBtnIdx(true),2);assert.equal(await owner.api.getPowerBtnIdx(false),null);
  for(const raw of [null,undefined,'',false,{},0,101,NaN]){owner.state.size=raw;assert.equal(await owner.api.readHibernateSize(),null,'unknown hibernate size became 50%');}
  owner.state.size=40;owner.state.buttons.ac=0;owner.state.buttons.dc=0;before=structuredClone(f.document().extensions.bootModes);
  owner.state.buttonMismatch=true;await assert.rejects(()=>owner.api.setPowerBtnIdx(true,3),/回读/);owner.state.buttonMismatch=false;assert.deepEqual(f.document().extensions.bootModes,before);
  owner.state.enabled=false;owner.state.hibernateMismatch=true;await assert.rejects(()=>owner.api.setHibernate(true),/回读/);owner.state.hibernateMismatch=false;assert.deepEqual(f.document().extensions.bootModes,before);
  owner.state.enabled=true;owner.state.sizeMismatch=true;await assert.rejects(()=>owner.api.setHibernateSize(65),/回读/);owner.state.sizeMismatch=false;assert.deepEqual(f.document().extensions.bootModes,before);
  owner.state.shellExit=5;for(const write of [()=>owner.api.setPowerBtnIdx(true,3),()=>owner.api.setHibernate(true),()=>owner.api.setHibernateSize(65)])await assert.rejects(write);owner.state.shellExit=0;assert.deepEqual(f.document().extensions.bootModes,before);
  owner.state.battery=false;const calls=owner.state.calls.length;await assert.rejects(()=>owner.api.setPowerBtnIdx(false,3),/电池/);assert.equal(owner.state.calls.length,calls);owner.state.battery=true;
  for(const write of [()=>owner.api.setPowerBtnIdx(true,9),()=>owner.api.setHibernate(null),()=>owner.api.setHibernateSize(NaN),()=>owner.api.setHibernateSize(Infinity)]){const calls=owner.state.calls.length;await assert.rejects(write);assert.equal(owner.state.calls.length,calls);}
  const options={origin:'boot-mode'};owner.state.afterWrite=()=>{options.origin='ordinary';};await owner.api.setPowerBtnIdx(true,3,options);owner.state.afterWrite=null;assert.deepEqual(f.document().extensions.bootModes,before);
  const ui=createSleepPowerUiFixture(owner);owner.state.buttons.ac=1;owner.state.shellExit=5;await ui.functions.onPwrBtn(true,3);assert.equal(ui.acBtnIdx.value,0,'failed UI write did not reread the actual button');assert.match(ui.msg.value,/电源按钮设置失败/);owner.state.shellExit=0;
  owner.state.buttons.ac=null;owner.state.buttonMismatch=true;await ui.functions.onPwrBtn(true,3);assert.equal(ui.acBtnIdx.value,null);owner.state.buttonMismatch=false;
  f.state.writeFailure=true;await ui.functions.onPwrBtn(true,2);assert.equal(ui.acBtnIdx.value,2);assert(ui.sleepPreferenceWarnings.acButton);f.state.writeFailure=false;
  owner.state.enabled=false;ui.hibernateOn.value=false;ui.hibSize.value=null;const start=owner.state.calls.length;await ui.functions.onHibernate(true);assert.equal(ui.hibernateOn.value,true);assert.equal(ui.hibSize.value,owner.state.size);assert.equal(owner.state.calls.slice(start).filter(c=>c.kind==='shell'&&c.args[1]==='/size').length,0,'unknown hibernate size became an automatic 50% write');assert(ui.sleepPreferenceWarnings.acButton,'unrelated successful hibernate edit erased failed button preference');
  await ui.functions.onPwrBtn(true,2);assert.equal(ui.sleepPreferenceWarnings.acButton,undefined);assert.equal(ui.msg.value,'');
  for(const kind of ['future','revision','sleep-null','absent']){
   await f.repo.updateSettings(settings=>{if(kind==='absent')delete settings.extensions.bootModes;else{settings.extensions.bootModes=structuredClone(doc);if(kind==='future')settings.extensions.bootModes.schema=99;if(kind==='revision')settings.extensions.bootModes.revision=Number.MAX_SAFE_INTEGER;if(kind==='sleep-null')settings.extensions.bootModes.global.sleep=null;}});
   const stable=structuredClone(f.document().extensions.bootModes),writes=f.state.writes.length;result=await owner.api.setPowerBtnIdx(true,3);assert.equal(result.preferenceSaved,kind==='absent');assert.deepEqual(f.document().extensions.bootModes,stable);assert.equal(f.state.writes.length,writes);
  }
  rows.push({mode,ordinaryFields:4,unknownReadsStayUnknown:true,sceneAndRuntimeOriginsIsolated:true,partialPreferenceFailureHonest:true});
 }
 return rows;
});
await test('explicit startup controller choices stay separate from ordinary toggles across four modes',async()=>{
 const {compileSettingsConsumer,createSettingsFixture}=await import('./fixtures/settings_baseline.mjs'); const nativeFiles=process.argv.includes('--ordinary-preferences-native-files');
 const fixtureFactory=nativeFiles?globalThis.__bootPreferenceFixtureFactory:createSettingsFixture;
 if(nativeFiles)assert(!process.argv.includes('--input-receipt-native-files'),'Explicit native preference scopes are mutually exclusive');
 assert.equal(typeof fixtureFactory,'function','Explicit native preferences require the owned native storage provider');
const code=await compileSettingsConsumer(process.cwd()),rows=[];
 for(const id of m.BOOT_MODE_IDS)for(const persona of ['disabled','steamdeck','dualsense-edge','elite'])for(const own of id==='global'?['follow']:['follow','off']){
  p.reset();p.settings.startupDesired={...p.settings.startupDesired,virtualGamepadPersona:persona,gyroPreset:'off'};
  p.settings.input.outputTarget={...p.settings.input.outputTarget,persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true};p.settings.input.gyroMotion.enabled=true;
  const baseline=await b.captureBootModeSnapshot(),draft=b.readStoredBootMode(p.settings,baseline);
  assert.equal(draft.global.input.persona,persona,'startup draft must not adopt the ordinary virtual toggle');assert.equal(draft.global.gyro.motion.enabled,false,'temporary gyro ON is not startup intent');
  const legacy={schema:1,global:{},scenes:{}};
  const partial=b.readStoredBootMode({...p.settings,extensions:{bootModes:legacy}},baseline);
  assert.equal(partial.global.input.persona,persona,'missing startup module must use explicit saved startup choice');
  assert.equal(partial.global.gyro.motion.enabled,false);
  const explicitOff=b.readStoredBootMode({...p.settings,extensions:{bootModes:{...legacy,global:{input:{persona:'disabled'},gyro:{motion:{enabled:false}}}}}},baseline);
  assert.equal(explicitOff.global.input.persona,'disabled','saved startup OFF must remain authoritative');
  assert.equal(draft.activeSnapshot.input.persona,baseline.input.persona,'startup draft must preserve observed active input');
  for(const scene of Object.values(draft.scenes))delete scene.overrides.input;
  if(own==='off')draft.scenes[id].overrides.input={persona:'disabled',outputTarget:{persona:'disabled',buttonMappingEnabled:false}};
  const live=JSON.stringify(p.settings.input);const saved=await b.saveBootModes(draft),chosen=await b.chooseBootMode(id,saved,true);
  const expected=own==='off'?'disabled':persona;assert.equal(p.settings.startupDesired.virtualGamepadPersona,expected);assert.equal(JSON.stringify(p.settings.input),live,'startup edit must not change current runtime input');
  const f=fixtureFactory(code,p.settings);let current=await f.repo.loadSettings(),startup=structuredClone(f.document().startupDesired),modes=structuredClone(f.document().extensions.bootModes);
  const result=await f.repo.compareAndSwapInputSettings(current.input.revision,{outputTarget:{persona:'elite',buttonMappingEnabled:true}},undefined,{origin:'ordinary'});assert.equal(result.ok,true);
  const disk=f.document();assert.deepEqual(disk.startupDesired,startup,'ordinary controller ON must not change explicit startup');assert.deepEqual(disk.extensions.bootModes,modes,'ordinary controller ON must not enter global mode memory');
  const file=path.join(out,'startup-controller-'+id+'-'+persona+'-'+own+'.json');fs.writeFileSync(file,JSON.stringify(disk),{flag:'wx'});rows.push({mode:id,persona,own,file,expectedPersona:expected,ordinaryToggleExcluded:true,liveInputUnchangedByStartupSave:true});
 }
 fs.writeFileSync(path.join(out,'EXPLICIT-STARTUP-NATIVE-WIRES.json'),JSON.stringify(rows,null,2)+'\n',{flag:'wx'});return rows;
});
await test('ordinary saved preferences follow global across four modes without adopting runtime or independent scenes',async()=>{
 if(process.argv.includes('--explicit-section-native-files')){
  const {runExplicitSectionIntent}=await import('./boot_modes_fixture/explicitSectionIntent.mjs');
  return await runExplicitSectionIntent(out,process.argv.slice(2),{native:globalThis.__bootModeSaveNative});
 }
 if(process.argv.includes('--ordinary-canonical-sources')){const {runOrdinarySourceParity}=await import('./boot_modes_fixture/preferenceOwner.mjs');return await runOrdinarySourceParity(out,process.argv.slice(2),{native:globalThis.__bootCanonicalNative,settingsSources:globalThis.__bootCanonicalSources});}
 if(process.argv.includes('--first-document-source-native')){const {runFirstDocumentSourceFileCases}=await import('./boot_modes_fixture/preferenceOwner.mjs');return await runFirstDocumentSourceFileCases(out,process.argv.slice(2));}
 const {compileSettingsConsumer,createSettingsFixture}=await import('./fixtures/settings_baseline.mjs');
  const nativeFiles=process.argv.includes('--ordinary-preferences-native-files');
 const fixtureFactory=nativeFiles?globalThis.__bootPreferenceFixtureFactory:createSettingsFixture;
 if(nativeFiles)assert(!process.argv.includes('--input-receipt-native-files'),'Explicit native preference scopes are mutually exclusive');
 assert.equal(typeof fixtureFactory,'function','Explicit native preferences require the owned native storage provider');
const code=await compileSettingsConsumer(process.cwd()),rows=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();doc.boot=id;doc.global.background.ui.backgroundOpacity=72;doc.global.steamWallpaper.enabled=false;
  if(id!=='global')doc.scenes[id].overrides.background={ui:{backgroundOpacity:89}};
  const f=fixtureFactory(code,{extensions:{bootModes:doc},ui:{backgroundOpacity:72,steamDynamicWallpaperEnabled:false}});
  await f.repo.loadSettings();await f.repo.saveSettingsSection('ui',{backgroundOpacity:41,steamDynamicWallpaperEnabled:true});
  let disk=f.document();assert.equal(disk.ui.backgroundOpacity,41);assert.equal(disk.extensions.bootModes.global.background.ui.backgroundOpacity,41);assert.equal(disk.extensions.bootModes.global.steamWallpaper.enabled,true);
  assert.equal(disk.extensions.bootModes.revision,doc.revision+1);
  if(id!=='global')assert.equal(disk.extensions.bootModes.scenes[id].overrides.background.ui.backgroundOpacity,89);
  const revision=disk.extensions.bootModes.revision;
  await f.repo.saveSettingsSection('ui',{backgroundOpacity:89},undefined,{origin:'boot-mode'});
  disk=f.document();assert.equal(disk.ui.backgroundOpacity,89);assert.equal(disk.extensions.bootModes.global.background.ui.backgroundOpacity,41);assert.equal(disk.extensions.bootModes.revision,revision);
  // A normal edit of another field cannot capture the active scene's opacity.
  await f.repo.saveSettingsSection('ui',{backgroundBlur:13});disk=f.document();assert.equal(disk.extensions.bootModes.global.background.ui.backgroundOpacity,41);assert.equal(disk.extensions.bootModes.global.background.ui.backgroundBlur,13);
  const targetBefore=structuredClone(disk.extensions.bootModes.global.input);
  let saved=await f.repo.loadSettings();const temp=await f.repo.compareAndSwapInputSettings(saved.input.revision,{outputTarget:{...saved.input.outputTarget,persona:'steamdeck',buttonMappingEnabled:true}});assert.equal(temp.ok,true);
  assert.deepEqual(f.document().extensions.bootModes.global.input,targetBefore,'temporary virtual pad request is not a boot preference');
  const modeBefore=structuredClone(f.document().extensions.bootModes);f.state.writeFailure=true;
  await assert.rejects(()=>f.repo.saveSettingsSection('ui',{backgroundOpacity:14}));assert.deepEqual(f.document().extensions.bootModes,modeBefore,'failed ordinary save does not partly change the preset');
  f.state.writeFailure=false;
  rows.push({mode:id,globalOpacity:disk.extensions.bootModes.global.background.ui.backgroundOpacity,independentOpacity:id==='global'?null:disk.extensions.bootModes.scenes[id].overrides.background.ui.backgroundOpacity,runtimeDidNotReplaceGlobal:true,temporaryInputNotPromoted:true});
 }
 return rows;
});
await test('ordinary Steam appearance preferences follow four-mode global through the actual saved owner',async()=>{
 const {compileSettingsConsumer,createSettingsFixture}=await import('./fixtures/settings_baseline.mjs');
 const code=await compileSettingsConsumer(process.cwd()),require=createRequire(path.join(process.cwd(),'package.json'));
 const {transformSync}=require('esbuild'),rows=[];
 const helper={exports:{}};
 vm.runInNewContext(require('esbuild').buildSync({entryPoints:['src/bridge/bootModeGlobalPreferences.ts'],absWorkingDir:process.cwd(),bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text,{module:helper,exports:helper.exports,structuredClone,TextEncoder});
 for(const id of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();doc.boot=id;doc.global.steamAppearance={states:{Round:true,'Art Hero':true}};
  if(id!=='global')doc.scenes[id].overrides.steamAppearance={states:{Round:true}};
  const f=createSettingsFixture(code,{extensions:{bootModes:doc}});await f.repo.loadSettings();
  let revision=1,failSave=false,mismatch=false,beforeApply=null,beforeStatus=null;const calls=[];
  const snapshot={controlProtocol:2,packAvailable:true,packPrepared:true,packOwned:true,packRepairRequired:false,backendReloadRequired:false,targetId:'a'.repeat(16),revision:String(revision).padStart(64,'0'),linked:true,backendReady:false,backendProcessCount:0,steamRunning:false,configuredMatchesBackend:false,note:'memory owner',themes:[]};
  const api={fs:{exists:async()=>true},settingsStore:{location:async()=>({directory:'Q:/ymcc-memory-fixture/PowerControl'})},shell:{hidden:async()=>{throw Error('unexpected launch');},run:async(_program,args)=>{
   const action=args[args.indexOf('-Action')+1];calls.push(action);
   if(action==='Status'){if(beforeStatus){const run=beforeStatus;beforeStatus=null;await run();}return {exitCode:0,stdout:JSON.stringify({ok:true,snapshot}),stderr:''};}
   if(beforeApply){const run=beforeApply;beforeApply=null;await run();}
   if(action==='Apply'){
    const payload=JSON.parse(Buffer.from(args[args.indexOf('-Payload')+1],'base64').toString('utf8'));
    for(const [name,value]of Object.entries(payload.states))snapshot.themes.find(row=>row.name===name).configured=mismatch?!value:value;
   }else if(action==='Reset'||action==='Restore'){for(const row of snapshot.themes)row.configured=action==='Reset';}
   else throw Error('unexpected appearance action '+action);
   snapshot.revision=String(++revision).padStart(64,'0');
   return {exitCode:failSave?1:0,stdout:JSON.stringify({ok:!failSave,saved:true,snapshot,message:failSave?'saved but backend unconfirmed':'saved'}),stderr:''};
  }}};
  const module={exports:{}};
  vm.runInNewContext(transformSync(fs.readFileSync('src/bridge/steamAppearance.ts','utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,TextEncoder,TextDecoder,setTimeout,clearTimeout,performance,btoa,require:name=>{
   if(name==='./api')return api;if(name==='./ipc')return {invoke:async command=>{assert.equal(command,'steamAppearance.idlePolicy');return {supported:true,version:3,manualObservationProtocol:1,sharedSteamReadProtocol:1,processQuery:'Local\\YMCC.CssInventory.'+'a'.repeat(32),pid:42,birth:'100'};}};if(name==='./settingsRepository')return f.repo;if(name==='./bootModeGlobalPreferences')return helper.exports;throw Error('unexpected appearance dependency '+name);
  }});
  snapshot.themes=module.exports.STEAM_APPEARANCE_THEMES.map(theme=>({name:theme.name,installed:true,configured:theme.name!=='Art Hero',backendEnabled:null,parameterCount:0,error:''}));
  const service=module.exports.steamAppearance,read=()=>service.read();
  let result=await service.apply(await read(),{Round:false});
  let saved=f.document().extensions.bootModes;
  assert.equal(saved.global.steamAppearance.states.Round,false,'ordinary appearance save did not update global startup preference');
  assert.equal(result.preferenceSaved,true);assert.equal(saved.global.steamAppearance.states['Art Hero'],true,'scene-only theme captured as global');
  if(id!=='global')assert.deepEqual(saved.scenes[id].overrides.steamAppearance,doc.scenes[id].overrides.steamAppearance);
  const global=structuredClone(saved.global.steamAppearance),savedRevision=saved.revision;
  await service.apply(await read(),{Round:true},{origin:'boot-mode'});assert.deepEqual(f.document().extensions.bootModes.global.steamAppearance,global);assert.equal(f.document().extensions.bootModes.revision,savedRevision);
  await service.apply(await read(),{Round:false},{origin:'runtime'});assert.deepEqual(f.document().extensions.bootModes.global.steamAppearance,global);
  await f.repo.updateSettings(settings=>{settings.extensions.bootModes.global.steamAppearance.states.Round=true;settings.extensions.bootModes.revision++;});
  const applies=calls.filter(c=>c==='Apply').length;result=await service.apply(await read(),{Round:false});
  assert.equal(result.preferenceSaved,true);assert.equal(f.document().extensions.bootModes.global.steamAppearance.states.Round,false);assert.equal(calls.filter(c=>c==='Apply').length,applies,'same saved choice repeated backend application to repair the preference');
  f.state.writeFailure=true;result=await service.apply(await read(),{Round:true});assert.equal(result.ok,true);assert.equal(result.saved,true);assert.equal(result.preferenceSaved,false);assert.match(result.message,/开机全局外观偏好未保存/);assert.equal(f.document().extensions.bootModes.global.steamAppearance.states.Round,false);f.state.writeFailure=false;
  result=await service.apply(await read(),{Round:true});assert.equal(result.preferenceSaved,true);assert.equal(f.document().extensions.bootModes.global.steamAppearance.states.Round,true);
  failSave=true;result=await service.apply(await read(),{Round:false});assert.equal(result.ok,false);assert.equal(result.saved,true);assert.equal(result.preferenceSaved,true);assert.equal(f.document().extensions.bootModes.global.steamAppearance.states.Round,false);failSave=false;
  const before=structuredClone(f.document().extensions.bootModes);mismatch=true;result=await service.apply(await read(),{Round:true});mismatch=false;assert.equal(result.preferenceSaved,false);assert.deepEqual(f.document().extensions.bootModes,before,'mismatched saved owner changed global preferences');
  const count=calls.length;await assert.rejects(()=>service.apply(snapshot,{Round:false},{origin:'invalid'}));assert.equal(calls.length,count,'invalid origin reached external owner');
  beforeApply=()=>f.repo.updateSettings(settings=>{settings.extensions.peerFuture={keep:true};settings.extensions.bootModes.scenes.steam.overrides.steamAppearance={states:{Round:true}};settings.extensions.bootModes.revision++;});
  await service.apply(await read(),{Round:true});saved=f.document().extensions.bootModes;assert.equal(f.document().extensions.peerFuture.keep,true);assert.equal(saved.scenes.steam.overrides.steamAppearance.states.Round,true);
  result=await service.reset('preset');assert.equal(result.preferenceSaved,true);assert(Object.values(f.document().extensions.bootModes.global.steamAppearance.states).every(v=>v===true));
  result=await service.reset('previous');assert.equal(result.preferenceSaved,true);assert(Object.values(f.document().extensions.bootModes.global.steamAppearance.states).every(v=>v===false));
  failSave=true;result=await service.reset('preset');assert.equal(result.ok,false);assert.equal(result.saved,true);assert.equal(result.preferenceSaved,true);failSave=false;
  const scriptCount=calls.length;await assert.rejects(()=>service.apply(snapshot,{Round:false},{origin:null}));assert.equal(calls.length,scriptCount);
  let releaseRead,enteredRead;const readEntered=new Promise(r=>enteredRead=r),readHeld=new Promise(r=>releaseRead=r);
  beforeStatus=async()=>{enteredRead();await readHeld;};
  const intent={Round:false},operation=service.apply(structuredClone(snapshot),intent);await readEntered;intent.Round=true;releaseRead();await operation;
  assert.equal(f.document().extensions.bootModes.global.steamAppearance.states.Round,false,'caller mutated ordinary intent during external read');
  for(const kind of ['future','revision','shape','absent']){
   await f.repo.updateSettings(settings=>{if(kind==='absent')delete settings.extensions.bootModes;else {settings.extensions.bootModes=structuredClone(doc);if(kind==='future')settings.extensions.bootModes.schema=99;if(kind==='revision')settings.extensions.bootModes.revision=Number.MAX_SAFE_INTEGER;if(kind==='shape')settings.extensions.bootModes.global.steamAppearance={states:null};}});
   const protectedValue=structuredClone(f.document().extensions.bootModes),writes=f.state.writes.length;
   result=await service.apply(await read(),{Round:!snapshot.themes.find(row=>row.name==='Round').configured});
   assert.equal(result.preferenceSaved,kind==='absent');assert.deepEqual(f.document().extensions.bootModes,protectedValue);assert.equal(f.state.writes.length,writes);
  }
  rows.push({mode:id,ordinaryProjection:true,sceneAndRuntimeIsolated:true,noOpRepairsPreferenceWithoutBackendWrite:true,partialSaveHonest:true,independentPeerPreserved:true});
 }
 return rows;
});
await test('boot Steam appearance applies carry scene origin across all four modes',async()=>{
 const nativeCss=process.argv.includes('--appearance-native-owner');
 const psTransactions=process.argv.includes('--appearance-ps-transactions');
 if(psTransactions&&process.argv.some(arg=>arg.startsWith('--appearance-native-owner')||arg==='--appearance-owner'||arg.startsWith('--appearance-owner-')))throw Error('Choose one actual PS, native CSS or JS appearance owner scope');
 if(psTransactions){const {runAppearancePsTransactions}=await import('./boot_modes_fixture/appearancePsTransactions.mjs');return await runAppearancePsTransactions(out,process.argv.slice(2));}
 if(process.argv.includes('--appearance-native-owner-settings-wake')&&!nativeCss)throw Error('Native CSS settings-wake requires its scope');
 if(process.argv.includes('--appearance-native-owner-settings-boundary')&&!process.argv.includes('--appearance-native-owner-settings-wake'))throw Error('Native CSS settings boundary requires settings-wake');
 if(process.argv.includes('--appearance-native-owner-assignment-only')&&!nativeCss)throw Error('Native CSS assignment-only requires its scope');
 if(process.argv.includes('--appearance-native-owner-negative')&&!nativeCss)throw Error('Native CSS mutation control requires its scope');
 if(process.argv.includes('--appearance-native-owner-before')&&!nativeCss)throw Error('Native CSS original control requires its scope');
 if(nativeCss&&process.argv.includes('--appearance-owner'))throw Error('Choose one appearance owner scope');
 if(nativeCss){const {runCssNativeOwner}=await import('./boot_modes_fixture/cssNativeOwner.mjs');return await runCssNativeOwner(out,process.argv.slice(2));}
 if(process.argv.includes('--appearance-owner-before')&&!process.argv.includes('--appearance-owner'))throw Error('Appearance original control requires owner scope');
 if(process.argv.includes('--appearance-owner')){const {runAppearanceOwner}=await import('./boot_modes_fixture/appearanceOwner.mjs');return await runAppearanceOwner(out,process.argv.slice(2));}
 const rows=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();doc.boot=id;doc.global.steamAppearance={states:{Round:false}};
  await startMode(id,doc);const calls=p.calls.filter(c=>c.op==='appearance.apply');
  assert.equal(calls.length,1);assert.equal(calls[0].origin,'boot-mode');rows.push({mode:id,origin:calls[0].origin});
 }
 return rows;
});
await test('external priority global memory survives every boot mode and legacy snapshots',async()=>{
 const {compileSettingsConsumer,createSettingsFixture}=await import('./fixtures/settings_baseline.mjs');
 const settingsCode=await compileSettingsConsumer(process.cwd());
 const f=createSettingsFixture(settingsCode,{input:{revision:3,ownership:{externalControllerPriority:true,otherPolicy:'keep'},gameOverride:{identity:'game-A',outputTarget:{persona:'elite'}}}});
 const first=await f.repo.loadSettings();assert.equal(first.input.ownership.externalControllerPriority,true);
 const saved=await f.repo.compareAndSwapInputSettings(first.input.revision,{ownership:{externalControllerPriority:false}});assert.equal(saved.ok,true);
 assert.deepEqual(saved.value.gameOverride,first.input.gameOverride);assert.deepEqual(saved.value.outputTarget,first.input.outputTarget);
 const reopened=createSettingsFixture(settingsCode,f.document());assert.equal((await reopened.repo.loadSettings()).input.ownership.externalControllerPriority,false);
 assert.equal((await reopened.repo.loadSettings()).input.ownership.otherPolicy,'keep');
 const rows=[];
 for(const enabled of [false,true])for(const id of m.BOOT_MODE_IDS){
  p.reset();p.settings.input.ownership={externalControllerPriority:enabled,futureOwnership:{keep:true}};
  const base=await baseline();assert.equal(base.input.ownership?.externalControllerPriority,undefined,'live boot capture must not own global external priority');
  const doc=m.createBootModeDocument(base);
  doc.global.input.ownership={externalControllerPriority:!enabled,frontendIsolationEnabled:true};
  doc.activeSnapshot.input.ownership={externalControllerPriority:!enabled};
  for(const scene of Object.values(doc.scenes)){scene.overrides.input={...scene.overrides.input,ownership:{externalControllerPriority:!enabled}};scene.rememberedOverrides={input:{ownership:{externalControllerPriority:!enabled}}};}
  doc.presets.global={legacy:{name:'legacy',overrides:{input:{ownership:{externalControllerPriority:!enabled}}}}};
  const normalized=m.normalizeBootModeDocument(doc,base);
  assert.equal(normalized.global.input.ownership?.externalControllerPriority,undefined);
  for(const scene of Object.values(normalized.scenes)){assert.equal(scene.overrides.input?.ownership?.externalControllerPriority,undefined);assert.equal(scene.rememberedOverrides?.input?.ownership?.externalControllerPriority,undefined);}
  assert.equal(normalized.presets.global.legacy.overrides.input?.ownership?.externalControllerPriority,undefined);
  stored(doc);await startMode(id,doc);
  assert.equal(p.settings.input.ownership.externalControllerPriority,enabled,'mode/startup must preserve current global external priority');
  assert.deepEqual(p.settings.input.ownership.futureOwnership,{keep:true});rows.push({enabled,id});
 }
 for(const enabled of [false,true]){
  p.reset();p.settings.input.ownership={externalControllerPriority:enabled,rollbackSentinel:'global'};
  const doc=await fresh();doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'microsoft',ownership:{externalControllerPriority:!enabled}};
  stored(doc);p.faults['mouse.backend']=1;
  await assert.rejects(startMode('steam',doc),error=>{assert.equal(error.failedStep,'mouse-start');assert(error.rollback.every(item=>item.ok));return true;});
  assert.equal(p.settings.input.ownership.externalControllerPriority,enabled);assert.equal(p.settings.input.ownership.rollbackSentinel,'global');
  for(const call of p.calls.filter(call=>call.op==='input.cas'))assert.equal(call.value.patch.ownership?.externalControllerPriority,undefined,'neither apply nor rollback may write global external priority');
 }
 return {globalRoundTrip:true,gameOverlayPreserved:true,rollbackCases:2,modeCombinations:rows};
});
await test('touch summon relative defaults and migration across four boot modes',async()=>{
 const base=await baseline();base.touch={profiles:{}};const doc=m.createBootModeDocument(base);
 for(const id of m.BOOT_MODE_IDS)for(const persona of ['disabled','steamdeck','dualsense-edge','elite']){
  const profile=m.resolveBootMode(doc,id).touch.profiles[persona];assert.equal(profile.summonPosition,'opposite');assert.equal(profile.summonEnabled,true);assert.equal(profile.autoHideSeconds,15);
 }
 const legacy=structuredClone(doc);
 for(const persona of ['disabled','steamdeck','dualsense-edge','elite']){const profile=legacy.global.touch.profiles[persona];delete profile.summonPlacementVersion;profile.summonPosition='right';profile.summonEnabled=false;profile.scale=135;}
 legacy.scenes.xbox.overrides.touch={profiles:{elite:{summonPosition:'off',summonEnabled:false,scale:125}}};
 const normalized=m.normalizeBootModeDocument(legacy,base);
 for(const id of m.BOOT_MODE_IDS)for(const persona of ['disabled','steamdeck','dualsense-edge','elite'])assert.equal(m.resolveBootMode(normalized,id).touch.profiles[persona].summonPosition,'opposite');
 assert.equal(normalized.global.touch.profiles.steamdeck.scale,135);assert.equal(normalized.scenes.xbox.overrides.touch.profiles.elite.scale,125);
 for(const persona of ['disabled','steamdeck','dualsense-edge','elite'])normalized.global.touch.profiles[persona].summonPosition='same';
 for(const id of ['xbox','steam','xbox-steam'])delete normalized.scenes[id].overrides.touch;
 const saved=m.normalizeBootModeDocument(normalized,base);for(const id of m.BOOT_MODE_IDS)for(const persona of ['disabled','steamdeck','dualsense-edge','elite'])assert.equal(m.resolveBootMode(saved,id).touch.profiles[persona].summonPosition,'same');
 saved.global.touch.profiles.disabled.summonPosition='off';saved.global.touch.profiles.disabled.summonEnabled=false;
 assert.equal(m.normalizeBootModeDocument(saved,base).global.touch.profiles.disabled.summonPosition,'off');
 const runtime=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();for(const persona of ['disabled','steamdeck','dualsense-edge','elite'])p.state.touch[persona]={...p.screenTouchpadDefaults(persona),summonPosition:'same',summonPlacementVersion:1,summonEnabled:true,autoHideSeconds:60};
  const runtimeBase=await baseline();runtimeBase.touch={profiles:{}};const template=m.createBootModeDocument(runtimeBase);stored(template);
  const applied=await startMode(id,template);
  for(const persona of ['disabled','steamdeck','dualsense-edge','elite']){
   assert.equal(p.state.touch[persona].autoHideSeconds,15);
   assert.equal(p.state.touch[persona].summonPosition,'opposite');
  }
  for(const call of p.calls.filter(call=>call.op==='touch.saveProfiles'))for(const patch of Object.values(call.value.profiles))for(const key of ['layout','leftMode','rightMode','singleMode'])assert.equal(patch[key],undefined);
  runtime.push({mode:id,touchWrites:p.calls.filter(call=>call.op==='touch.saveProfiles').length,errors:applied.receipt.pending.filter(item=>item.includes('屏幕触摸'))});
 }
 return {bootModes:4,personaProfiles:4,oldAbsoluteAndOffMigrated:true,newPlayerEditsPreserved:true,runtime};
});
await test('sleep guard timeout config preserves legacy defaults, partial saves and boot snapshots',async()=>{
 const {compileSettingsConsumer,createSettingsFixture}=await import('./fixtures/settings_baseline.mjs');
 const code=await compileSettingsConsumer(process.cwd());
 const saved={sleep:{pauseGameOnSleep:false,retryOnEntryFailure:false,retryOnNonUserWake:true}};
 const f=createSettingsFixture(code,saved);await f.repo.loadSettings();
 assert.equal(f.repo.normalizeSettings(saved).sleep.retryOnSleepTimeout,true);
 await f.repo.saveSettingsSection('sleep',{retryOnSleepTimeout:true});
 await f.repo.saveSettingsSection('sleep',{retryOnEntryFailure:true});
 assert.equal(f.document().sleep.retryOnSleepTimeout,true);assert.equal(f.document().sleep.retryOnNonUserWake,true);assert.equal(f.document().sleep.pauseGameOnSleep,false);
 const reopened=createSettingsFixture(code,f.document());assert.equal((await reopened.repo.loadSettings()).sleep.retryOnSleepTimeout,true);
 const {createRequire}=await import('node:module'),require=createRequire(path.resolve('package.json')),ts=require('typescript');
 const bridge=fs.readFileSync('src/bridge/yeman.ts','utf8'),ast=ts.createSourceFile('actual.ts',bridge,ts.ScriptTarget.Latest,true);
 const names=['sleepGuardGet','sleepGuardSetConfig'];
 const selected=ast.statements.filter(n=>ts.isFunctionDeclaration(n)&&names.includes(n.name?.text)).map(n=>n.getText(ast).replace(/^export\s+/,''));
 assert.equal(selected.length,names.length);
 const js=require('esbuild').transformSync(selected.join('\n'),{loader:'ts',format:'cjs'}).code;
 const calls=[];let reply={enabled:false,mode:'custom',pauseGameOnSleep:false,retryOnEntryFailure:false,resleepEnabled:true};
 const bootPreferences={exports:{}};
 vm.runInNewContext(require('esbuild').buildSync({entryPoints:['src/bridge/bootModeGlobalPreferences.ts'],absWorkingDir:process.cwd(),bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text,{module:bootPreferences,exports:bootPreferences.exports,structuredClone,TextEncoder});
 const api=new Function('invoke','captureBootPreferenceWrite',js+';return {sleepGuardGet,sleepGuardSetConfig};')(async(cmd,value)=>{calls.push({cmd,value});return reply;},bootPreferences.exports.captureBootPreferenceWrite);
 const compat=await api.sleepGuardGet();assert.equal(compat.retryOnSleepTimeout,true);assert.equal(compat.retryOnEntryFailure,false);assert.equal(compat.retryOnNonUserWake,true);
 reply={...reply,retryOnSleepTimeout:true,retryOnNonUserWake:false};assert.equal((await api.sleepGuardGet()).retryOnSleepTimeout,true);assert.equal((await api.sleepGuardGet()).retryOnNonUserWake,false);
 await api.sleepGuardSetConfig({retryOnSleepTimeout:true});assert.deepEqual(calls.at(-1),{cmd:'sleepGuard.setConfig',value:{retryOnSleepTimeout:true}});
 for(const enabled of [false,true]){
  p.reset();p.settings.sleep={mode:'custom',pauseGameOnSleep:false,retryOnEntryFailure:false,retryOnNonUserWake:false,retryOnSleepTimeout:enabled};
  const doc=await fresh();assert.equal(doc.global.sleep.config.retryOnSleepTimeout,enabled);
  doc.scenes.steam.overrides.sleep={...structuredClone(doc.global.sleep),config:{...doc.global.sleep.config,retryOnSleepTimeout:!enabled}};
  const initialSource=(await b.loadBootModes({editorOnly:true})).initialSource;const reopened=await b.saveBootModes(doc,doc.revision,initialSource);stored(reopened);
  const result=await startMode('steam',reopened);assert.equal(result.document.current,'steam');
  assert.equal(p.calls.find(c=>c.op==='sleep.config')?.value.retryOnSleepTimeout,!enabled);
  assert.equal(p.settings.extensions.bootModes.activeSnapshot.sleep.config.retryOnSleepTimeout,!enabled);
  const bad=structuredClone(reopened);bad.global.sleep.config.retryOnSleepTimeout='true';assert.throws(()=>m.normalizeBootModeDocument(bad,doc.global),/retryOnSleepTimeout/);
 }
 return {currentDefaultRetryOnSleepTimeout:true,explicitChoicesPreserved:true,partialSaveAndReload:true,bootCaptureSaveApply:true,malformedBooleanRejected:true,nativeExecution:'NOT_RUN'};
});
const startupOnly=c=>c.op.startsWith('startup.')||c.op==='ipc.bootMode.legacyStartup.retire'||c.op==='joy.startup'&&c.value===false||c.op==='fx.set'&&c.value===false||c.op==='addon.set'&&c.value.enabled===false||c.op==='legacy.desktop.remove'&&c.value==='桌面模式-开机设置为桌面模式'||c.op==='registry.read'&&c.value.hive==='HKCU'&&c.value.key===String.raw`Software\Microsoft\Windows\CurrentVersion\Run`&&c.value.name==='Joyxoff';

const gameState=()=>JSON.stringify({profiles:p.settings.gameProfiles,games:p.settings.games});
await test('new documents disable startup application; schema 1 legacy missing bootEnabled preserves startup',async()=>{
 const doc=await fresh();assert.equal(doc.bootEnabled,false);const legacy=structuredClone(doc);delete legacy.bootEnabled;legacy.boot='steam';assert.equal(m.normalizeBootModeDocument(legacy,doc.global).bootEnabled,true);
 assert.equal(m.normalizeBootModeDocument({...legacy,bootEnabled:false},doc.global).bootEnabled,false);
 assert.equal(m.normalizeBootModeDocument(null,doc.global).bootEnabled,false);
});
await test('audio defaults enable only Steam and combined scenes while keeping observed runtime separate',async()=>{
 const base=await baseline();base.audioOptimization={enabled:true};const doc=m.createBootModeDocument(base);
 assert.equal(doc.audioDefaultsVersion,1);for(const id of m.BOOT_MODE_IDS)assert.equal(m.resolveBootMode(doc,id).audioOptimization.enabled,id==='steam'||id==='xbox-steam');
 assert.equal(doc.activeSnapshot.audioOptimization.enabled,true);assert.equal(base.audioOptimization.enabled,true);
 return {defaults:{global:false,xbox:false,steam:true,'xbox-steam':true},observedRuntimePreserved:true};
});
await test('legacy audio defaults migrate once without overriding explicit choices or later follow-global edits',async()=>{
 const doc=await fresh(),legacy=structuredClone(doc);delete legacy.audioDefaultsVersion;
 for(const id of ['xbox','steam','xbox-steam'])delete legacy.scenes[id].overrides.audioOptimization;
 const reorder=value=>Array.isArray(value)?value.map(reorder):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).sort().map(key=>[key,reorder(value[key])])):value;const fromDisk=m.normalizeBootModeDocument(reorder(legacy),doc.global);assert.equal(fromDisk.scenes.xbox.overrides.input,undefined);assert.equal(fromDisk.scenes.xbox.overrides.touch,undefined);
 const before=JSON.stringify(legacy),next=m.normalizeBootModeDocument(legacy,doc.global);assert.equal(JSON.stringify(legacy),before);
 for(const id of m.BOOT_MODE_IDS)assert.equal(m.resolveBootMode(next,id).audioOptimization.enabled,id==='steam'||id==='xbox-steam');
 legacy.scenes.steam.overrides.audioOptimization={enabled:false};legacy.scenes.xbox.overrides.audioOptimization={enabled:true};
 const custom=m.normalizeBootModeDocument(legacy,doc.global);assert.equal(m.resolveBootMode(custom,'steam').audioOptimization.enabled,false);assert.equal(m.resolveBootMode(custom,'xbox').audioOptimization.enabled,true);
 next.scenes.steam.overrides.audioOptimization={enabled:false};m.setBootModeModuleScope(next,'xbox-steam','audioOptimization',false);
 const reopened=m.normalizeBootModeDocument(next,doc.global);assert.equal(reopened.scenes['xbox-steam'].overrides.audioOptimization,undefined);assert.equal(m.resolveBootMode(reopened,'steam').audioOptimization.enabled,false);
 return {oneTimeMigration:true,explicitEditsPreserved:true,followGlobalPreserved:true};
});
await test('only Steam scene startup requests audio ON and saving or disabled scene startup never replays or closes it',async()=>{
 const rows=[];for(const id of m.BOOT_MODE_IDS){p.reset();const doc=await fresh();stored(doc);p.calls.length=0;await startMode(id,doc);
 const writes=p.calls.filter(c=>c.op==='ipc.fxsound.setEnabled');assert.equal(writes.length,id==='steam'||id==='xbox-steam'?1:0);if(writes.length)assert.equal(writes[0].value.enabled,true);
 const count=writes.length;await b.loadBootModes({editorOnly:true});assert.equal(p.calls.filter(c=>c.op==='ipc.fxsound.setEnabled').length,count);rows.push({id,onRequests:count});
 }return {rows,noAudioWritesFromEditor:true,noForcedOffForManualInstances:true};
});
const oldSteamInput={persona:'steamdeck',mouse:'steamdeck',outputTarget:{persona:'steamdeck',buttonMappingEnabled:true}};
const oldSteamTouch={profiles:{steamdeck:{masterEnabled:true,summonPosition:'off',standaloneSpecialMode:'off',specialMask:3,rearMask:0,summonEnabled:false,specialEnabled:true,rearEnabled:false,enabled:true,layout:'dual',singleMode:'mouse',leftMode:'steamdeck',rightMode:'steamdeck',transparency:80,mouseSensitivity:100,scale:100,touchFeedback:'medium',autoHideSeconds:60}}};
await test('all four modes default virtual output OFF and Steam 3/4 enable standalone Steam special keys',async()=>{
 const base=await baseline();for(const persona of ['disabled','elite','steamdeck','dualsense-edge']){const current=structuredClone(base);current.input.persona=persona;current.input.outputTarget.persona=persona;if(persona==='steamdeck')current.input.mouse='steamdeck';const original=structuredClone(current),doc=m.createBootModeDocument(current);assert.deepEqual(current,original);assert.deepEqual(doc.activeSnapshot,current);assert.equal(doc.controllerDefaultsVersion,2);
 for(const id of m.BOOT_MODE_IDS){const next=m.resolveBootMode(doc,id);assert.equal(next.input.persona,'disabled');assert.equal(next.input.outputTarget.persona,'disabled');assert.equal(next.input.outputTarget.buttonMappingEnabled,false);if(id!=='global')assert.equal(doc.scenes[id].overrides.input,undefined);}
 for(const id of ['steam','xbox-steam']){const profile=m.resolveBootMode(doc,id).touch.profiles.disabled;assert.equal(profile.masterEnabled,true);assert.equal(profile.standaloneSpecialMode,'steamdeck');assert.equal(profile.specialMask,0);assert.equal(profile.specialEnabled,false);assert.equal(profile.enabled,false);assert.equal(profile.layout,'off');}
 doc.scenes.steam.overrides.touch.profiles.disabled.standaloneSpecialMode='off';assert.equal(doc.scenes['xbox-steam'].overrides.touch.profiles.disabled.standaloneSpecialMode,'steamdeck');doc.global.input.persona='dualsense-edge';for(const id of ['xbox','steam','xbox-steam'])assert.equal(m.resolveBootMode(doc,id).input.persona,'dualsense-edge');
 }return {order:m.BOOT_MODE_IDS,defaultPersonas:['disabled','disabled','disabled','disabled'],standaloneSteamKeys:true,globalEditsFollowed:true,independentCopies:true};
});
await test('old factory SD migrates once to global input and standalone Steam keys while explicit edits and follow-global stay intact',async()=>{
 const doc=await fresh(),rows=[];
 for(const version of [undefined,1]){const legacy=structuredClone(doc);if(version===undefined)delete legacy.controllerDefaultsVersion;else legacy.controllerDefaultsVersion=version;for(const id of ['xbox','steam','xbox-steam']){legacy.scenes[id].overrides.input=structuredClone(oldSteamInput);legacy.scenes[id].overrides.touch=structuredClone(oldSteamTouch);}
 const before=JSON.stringify(legacy),next=m.normalizeBootModeDocument(legacy,doc.global);assert.equal(JSON.stringify(legacy),before);assert.equal(next.controllerDefaultsVersion,2);for(const id of m.BOOT_MODE_IDS)assert.equal(m.resolveBootMode(next,id).input.persona,'disabled');assert.equal(next.scenes.xbox.overrides.touch,undefined);for(const id of ['steam','xbox-steam'])assert.equal(next.scenes[id].overrides.touch.profiles.disabled.standaloneSpecialMode,'steamdeck');assert.deepEqual(m.normalizeBootModeDocument(next,doc.global),next);
 for(const id of ['xbox','steam','xbox-steam']){legacy.scenes[id].overrides.input={...structuredClone(oldSteamInput),customPreserve:true};legacy.scenes[id].overrides.touch={profiles:{disabled:{standaloneSpecialMode:'ps5',scale:130}}};}const custom=m.normalizeBootModeDocument(legacy,doc.global);for(const id of ['xbox','steam','xbox-steam']){assert.equal(m.resolveBootMode(custom,id).input.persona,'steamdeck');assert.equal(custom.scenes[id].overrides.input.customPreserve,true);assert.equal(custom.scenes[id].overrides.touch.profiles.disabled.standaloneSpecialMode,'ps5');}
 for(const id of ['steam','xbox-steam']){m.setBootModeModuleScope(next,id,'input',false);m.setBootModeModuleScope(next,id,'touch',false);}const reopened=m.normalizeBootModeDocument(next,doc.global);for(const id of ['steam','xbox-steam']){assert.equal(reopened.scenes[id].overrides.input,undefined);assert.equal(reopened.scenes[id].overrides.touch,undefined);m.setBootModeModuleScope(reopened,id,'touch',true);assert.equal(reopened.scenes[id].overrides.touch.profiles.disabled.standaloneSpecialMode,'steamdeck');}
 const savedCustom=structuredClone(doc);savedCustom.scenes.steam.overrides.input=structuredClone(oldSteamInput);assert.deepEqual(m.normalizeBootModeDocument(savedCustom,doc.global).scenes.steam.overrides.input,oldSteamInput,'version 2 explicit SD must not be reset');rows.push({version:version??'unversioned',migrated:true,customPreserved:true,followGlobalPreserved:true});}
 const follow=structuredClone(doc);follow.controllerDefaultsVersion=1;follow.scenes.steam.overrides={};assert.equal(m.normalizeBootModeDocument(follow,doc.global).scenes.steam.overrides.touch,undefined,'v1 explicit follow must not gain a forced touch module');return rows;
});
await test('legacy inherited Xbox modules upgrade once while explicit edits and a later follow-global choice survive save and reload',async()=>{
 const doc=await fresh(),legacy=structuredClone(doc);delete legacy.xboxDefaultsVersion;
 for(const id of ['xbox','xbox-steam'])legacy.scenes[id].overrides={};
 const before=JSON.stringify(legacy),upgraded=m.normalizeBootModeDocument(legacy,doc.global);assert.equal(JSON.stringify(legacy),before);assert.equal(upgraded.xboxDefaultsVersion,1);
 assert.deepEqual(m.resolveBootMode(upgraded,'xbox').input,doc.global.input);assert.equal(m.resolveBootMode(upgraded,'xbox-steam').input.persona,'disabled');
 m.setBootModeModuleScope(upgraded,'xbox','input',false);m.setBootModeModuleScope(upgraded,'xbox','touch',false);
 const initialSource=(await b.loadBootModes({editorOnly:true})).initialSource;const saved=await b.saveBootModes(upgraded,upgraded.revision,initialSource),reopened=m.normalizeBootModeDocument(saved,doc.global);assert.equal(m.resolveBootMode(reopened,'xbox').input.persona,doc.global.input.persona);assert.equal(reopened.scenes.xbox.overrides.touch,undefined);
 legacy.scenes.xbox.overrides.input={persona:'dualsense-edge',mouse:'off'};legacy.scenes.xbox.overrides.touch={profiles:{steamdeck:{enabled:false,layout:'off'}}};
 const custom=m.normalizeBootModeDocument(legacy,doc.global);assert.equal(m.resolveBootMode(custom,'xbox').input.persona,'dualsense-edge');assert.equal(m.resolveBootMode(custom,'xbox').touch.profiles.steamdeck.layout,'off');assert.equal(m.resolveBootMode(custom,'xbox-steam').input.persona,'disabled');
 return {oneTimeInheritedUpgrade:true,explicitEditsPreserved:true,followGlobalRoundTrip:true};
});
await test('both Xbox next-boot choices prepare requested platforms and default OFF without hot input/touch changes, and retain actual repair failures',async()=>{
 const rows=[];for(const id of ['xbox','xbox-steam']){
  p.reset();const doc=await fresh(),input=structuredClone(p.settings.input),touch=structuredClone(p.state.touch),games=gameState();stored(doc);p.calls.length=0;
  const saved=await b.chooseBootMode(id,doc,true);assert.equal(saved.boot,id);assert.equal(p.state.xbox,true);assert.equal(p.state.steam,id==='xbox-steam');assert.equal(p.settings.startupDesired.virtualGamepadPersona,'disabled');assert.deepEqual(p.settings.input,input);assert.deepEqual(p.state.touch,touch);assert.equal(gameState(),games);assert(!p.calls.some(c=>c.op==='touch.set'||c.op==='touch.saveProfiles'||c.op==='input.cas'));
  p.faults['startup.xbox']=1;const failed=await b.chooseBootMode(id,saved,true);assert.match(failed.bootNotice,/Windows 开机配置/);assert.equal(failed.bootEnabled,true);assert.deepEqual(p.settings.input,input);rows.push({id,nextPersona:'disabled',xboxRepairAttempted:true,steamPrepared:id==='xbox-steam',hotInputWrites:0,hotTouchWrites:0,failureVisible:true});
 }return rows;
});
await test('first combined-mode selection persists OFF and standalone Steam keys without requiring an earlier preset save',async()=>{
 const before=structuredClone(p.settings.input),overview=await b.loadBootModes({overviewOnly:true});assert.equal(overview.requiresCapture,true);assert.equal(p.settings.extensions.bootModes,undefined);
 const full=await b.loadBootModes(),saved=await b.chooseBootMode('xbox-steam',full.document,true);
 assert.equal(saved.boot,'xbox-steam');assert.equal(p.settings.extensions.bootModes.bootEnabled,true);assert.equal(p.settings.startupDesired.virtualGamepadPersona,'disabled');assert.equal(m.resolveBootMode(p.settings.extensions.bootModes,'xbox-steam').input.persona,'disabled');assert.equal(p.settings.extensions.bootModesAppliedBoot,'fixture-windows-boot-1');assert.deepEqual(p.settings.input,before);
 const reopened=await b.loadBootModes({overviewOnly:true});assert.equal(m.resolveBootMode(reopened.document,'xbox-steam').input.persona,'disabled');assert.equal(m.resolveBootMode(reopened.document,'xbox-steam').touch.profiles.disabled.standaloneSpecialMode,'steamdeck');
 return {initialPresetSaveRequired:false,sceneAndNativeIntentWrittenTogether:true,currentInputUnchanged:true};
});
await test('legacy missing applied snapshots never turn default OFF input back into the currently running persona on edit',async()=>{
 const rows=[];for(const id of ['steam','xbox-steam'])for(const shape of ['missing','partial','empty-input']){
  p.reset();const doc=await fresh();delete doc.xboxDefaultsVersion;doc.current=id;doc.boot=id;doc.bootEnabled=true;delete doc.controllerDefaultsVersion;doc.scenes.steam.overrides={};doc.scenes['xbox-steam'].overrides={};
  if(shape==='missing')delete doc.activeSnapshot;else if(shape==='partial')delete doc.activeSnapshot.input;else doc.activeSnapshot.input={};stored(doc);
  const overview=await b.loadBootModes({overviewOnly:true});assert.equal(m.resolveBootMode(overview.document,id).input.persona,'disabled');
  const editor=await b.loadBootModes();assert.equal(m.resolveBootMode(editor.document,id).input.persona,'disabled');assert.equal(m.resolveBootMode(editor.document,id).touch.profiles.disabled.standaloneSpecialMode,'steamdeck');
  const saved=await b.saveBootModes(editor.document);assert.equal(m.resolveBootMode(saved,id).input.persona,'disabled');const reopened=await b.loadBootModes();assert.equal(m.resolveBootMode(reopened.document,id).input.persona,'disabled');assert.equal(p.settings.startupDesired.virtualGamepadPersona,'disabled');assert.equal(p.settings.input.outputTarget.persona,'elite');rows.push({id,shape,nextPersona:'disabled',currentPersona:'elite'});
 }return rows;
});
await test('partial scenes merge recursive defaults, replace arrays, follow latest global, and never alias the source',async()=>{
 const doc=await fresh();doc.global.input={persona:'elite',mouse:'off',residue:'global-only'};doc.scenes.steam.overrides.input={persona:'steamdeck',mouse:'steamdeck'};
 const next=m.resolveBootMode(doc,'steam');assert.equal(next.input.residue,'global-only');assert.deepEqual(m.mergeBootConfig({nested:{a:1,b:2},list:[1,2]}, {nested:{a:3},list:[9]}),{nested:{a:3,b:2},list:[9]});next.input.persona='disabled';assert.equal(doc.scenes.steam.overrides.input.persona,'steamdeck');
 m.setBootModeModuleScope(doc,'steam','input',false);doc.global.input.mouse='microsoft';assert.equal(m.resolveBootMode(doc,'steam').input.mouse,'microsoft');
 m.setBootModeModuleScope(doc,'steam','input',true);m.mutableBootModeModule(doc,'steam','input').mouse='off';assert.equal(doc.global.input.mouse,'microsoft');
 m.setBootModeModuleScope(doc,'xbox','input',false);assert.throws(()=>m.mutableBootModeModule(doc,'xbox','input'),/全局/);
 for(const module of m.BOOT_MODE_MODULES){const own=structuredClone(doc.global[module]);own.fixtureOwnMarker=module;doc.scenes.xbox.overrides[module]=own;m.setBootModeModuleScope(doc,'xbox',module,false);assert.throws(()=>m.mutableBootModeModule(doc,'xbox',module),/全局/);const restored=m.normalizeBootModeDocument(doc,doc.global);m.setBootModeModuleScope(restored,'xbox',module,true);assert.equal(restored.scenes.xbox.overrides[module].fixtureOwnMarker,module);assert.equal(doc.global[module].fixtureOwnMarker,undefined);}
 const {verifyAllBootModuleStorage}=await import('./boot_modes_fixture/legacyOrigin.mjs');
 const allModuleSelectors=process.argv.filter(value=>value.startsWith('--all-module-only='));assert(allModuleSelectors.length<=1,'Duplicate all-module storage selector');
 const nativeFiles=process.argv.includes('--all-module-native-files');
 assert(!allModuleSelectors.length||nativeFiles,'Native all-module selector requires its original native file terminal');
 if(nativeFiles)assert(globalThis.__bootModeSaveNative,'Original owned native storage required');
 return await verifyAllBootModuleStorage({model:m,boot:b,ports:p,...(nativeFiles?{native:globalThis.__bootModeSaveNative,storageRoot:path.join(out,'data'),only:allModuleSelectors[0]?.slice('--all-module-only='.length)}:{})});
});
await test('controller edits and calibration remain global across four modes; stale mode parameters are deleted',async()=>{
 const rows=[];
 for(const id of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh(),controllerInputIndex=process.argv.indexOf('--controller-input'),controllerReceipt=controllerInputIndex>=0?JSON.parse(fs.readFileSync(process.argv[controllerInputIndex+1],'utf8'))[0]:null,globalRules=controllerReceipt?.input?.buttonMapping?.rules?.shortcutRules||[{id:'global-only',origin:'custom',enabled:true,inputs:[],actionId:'keyboard.key',params:{key:'F9'}}];
  p.settings.input.buttonMapping.rules={schemaVersion:2,shortcutRulesInitialized:true,shortcutRules:structuredClone(globalRules)};
  p.settings.input.axisCalibration={deviceKey:'global-device',left:{offsetX:.12},right:{offsetY:-.05}};
  const globalBefore=structuredClone({mapping:p.settings.input.buttonMapping,calibration:p.settings.input.axisCalibration,flags:p.settings.gamepad});
  doc.global.controller={name:'obsolete global snapshot',rules:[]};doc.global.input.axisCalibration={deviceKey:'old'};doc.controllerTemplates={legacy:{rules:[]}};
  for(const mode of ['xbox','steam','xbox-steam']){doc.scenes[mode].overrides.controller={name:'old mode',rules:[]};doc.scenes[mode].rememberedOverrides={controller:{rules:[]}};doc.scenes[mode].overrides.input={...structuredClone(doc.global.input),buttonMapping:{rules:{shortcutRules:[]}},axisCalibration:{deviceKey:'mode'}};}
  doc.presets.steam={old:{name:'old',overrides:{controller:{rules:[]},input:{axisCalibration:{deviceKey:'preset'}}}}};
  p.settings.input.bootModeShortcuts={schema:1,mode:'steam',boot:'fixture-windows-boot-1',rules:[]};stored(doc);
  const before=JSON.stringify(doc),normalized=m.normalizeBootModeDocument(doc,doc.global);assert.equal(JSON.stringify(doc),before,'Normalization mutated the caller');
  assert(!('controllerTemplates' in normalized));assert(!('controller' in normalized.global));assert(!('axisCalibration' in normalized.global.input));
  for(const mode of m.BOOT_MODE_IDS){const snapshot=m.resolveBootMode(normalized,mode);assert(!('controller' in snapshot));assert(!('buttonMapping' in snapshot.input));assert(!('axisCalibration' in snapshot.input));if(mode!=='global'){assert(!('controller' in normalized.scenes[mode].overrides));assert(!('controller' in (normalized.scenes[mode].rememberedOverrides||{})));}}
  assert(!('controller' in normalized.presets.steam.old.overrides));assert(!('axisCalibration' in normalized.presets.steam.old.overrides.input));
  const saved=await b.saveBootModes(normalized),running=await startMode(id,saved);assert.equal(p.settings.input.bootModeShortcuts,null);
  assert.deepEqual(p.settings.input.buttonMapping,globalBefore.mapping);assert.deepEqual(p.settings.input.axisCalibration,globalBefore.calibration);assert.deepEqual(p.settings.gamepad,globalBefore.flags);assert(!opNames().includes('summon.set'));
  const added={...globalRules[0],id:'edited-globally'};const updated=await p.compareAndSwapInputSettings(p.settings.input.revision,{buttonMapping:{rules:{shortcutRules:[added],shortcutRulesInitialized:true}}});assert(updated.ok);
  const reopened=await b.loadBootModes({editorOnly:true});await startMode(id,reopened.document);assert.equal(p.settings.input.buttonMapping.rules.shortcutRules[0].id,added.id);assert.deepEqual(p.settings.input.axisCalibration,globalBefore.calibration);
  rows.push({mode:id,oldParametersRemoved:true,globalEditsPreserved:true,globalCalibrationPreserved:true,actualEditorReceipt:!!controllerReceipt,savedRuleCount:globalRules.length});
 }return rows;
});

await test('normalization strips unsafe keys and foreign modules without changing game records',async()=>{
 const doc=await fresh();const raw=JSON.parse(JSON.stringify(doc));raw.scenes.steam.overrides.gameProfiles={danger:true};raw.global.input=JSON.parse('{"persona":"elite","__proto__":{"polluted":true},"constructor":1,"bad":null}');raw.global.input.invalid=Infinity;
 const normalized=m.normalizeBootModeDocument(raw,doc.global);assert(!('gameProfiles' in normalized.scenes.steam.overrides));assert(!Object.hasOwn(normalized.global.input,'__proto__'));assert(!Object.hasOwn(normalized.global.input,'constructor'));assert(!('invalid' in normalized.global.input));assert.equal({}.polluted,undefined);
});
await test('partial failure rolls back entered steps in reverse order and reports failed rollback truthfully',async()=>{
 const events=[];await assert.rejects(m.executeBootModeSteps([{id:'first',apply:async()=>events.push('apply-1'),rollback:async()=>{events.push('undo-1');throw new Error('undo failed');}},{id:'partial',apply:async()=>{events.push('apply-2');throw new Error('write failed');},rollback:async()=>events.push('undo-2')},{id:'unreached',apply:async()=>events.push('bad'),rollback:async()=>events.push('bad-undo')}]),e=>{assert.equal(e.failedStep,'partial');assert.deepEqual(e.rollback.map(r=>[r.step,r.ok]),[['partial',true],['first',false]]);assert.match(e.rollback[1].error,/undo failed/);return true;});assert.deepEqual(events,['apply-1','apply-2','undo-2','undo-1']);
});
await test('generation changes suppress rollback writes into new settings and prevent the next',async()=>{
 let current=true;const events=[];await assert.rejects(m.executeBootModeSteps([{id:'write',apply:async()=>{events.push('write');current=false;},rollback:async()=>events.push('undo')},{id:'never',apply:async()=>events.push('never'),rollback:async()=>{}}],()=>current),e=>{assert.equal(e.failedStep,'write');assert.equal(e.rollback[0].ok,false);return true;});assert.deepEqual(events,['write']);
});
await test('save returns increasing revisions, rejects stale writers, and preserves games and unrelated extensions',async()=>{
 if(process.argv.includes('--editor-conflict-native-files')){
  const {verifyBootEditorConflictChain}=await import('./boot_modes_fixture/legacyOrigin.mjs');
  const index=process.argv.indexOf('--workbench-source');
  return await verifyBootEditorConflictChain(out,process.argv.slice(2),{native:globalThis.__bootModeSaveNative,workbenchSource:index<0?undefined:process.argv[index+1]});
 }
 if(process.argv.includes('--first-editor-source')){const {runFirstEditorSourceCases}=await import('./boot_modes_fixture/preferenceOwner.mjs');return await runFirstEditorSourceCases(out,process.argv.slice(2),{native:globalThis.__bootFirstEditorNative,bootModesSource:sourceIndex<0?undefined:path.resolve(process.argv[sourceIndex+1])});}
 const doc=await fresh(),games=gameState();const initial=await b.loadBootModes({editorOnly:true}),a=await b.saveBootModes(doc,doc.revision,initial.initialSource);assert.equal(a.revision,1);a.scenes.steam.name='second save';const c=await b.saveBootModes(a);assert.equal(c.revision,2);
 const before=JSON.stringify(p.settings);await assert.rejects(b.saveBootModes(a),/修改|变化|revision/i);assert.equal(JSON.stringify(p.settings),before);assert.equal(gameState(),games);assert.deepEqual(p.settings.extensions.unrelated,{keep:true});
 const {verifyBootEditorDocumentUpdates}=await import('./boot_modes_fixture/legacyOrigin.mjs');return await verifyBootEditorDocumentUpdates({model:m,boot:b,ports:p});
});
await test('Xbox and combined scenes repair and enable without probing platform, Steam connection or machine capability',async()=>{
 const originalPlatform=p.bootPlatform.read,originalSteam=p.steamUi.read;let platformReads=0,steamReads=0;
 p.bootPlatform.read=async()=>{platformReads++;throw Error('No platform admission allowed');};
 p.steamUi.read=async()=>{steamReads++;throw Error('No Steam admission allowed for combined mode');};
 try {
  for(const id of ['xbox','xbox-steam']){const result=await b.bootModeConnections(id);assert.deepEqual(result,[]);}
  assert.equal(platformReads,0);assert.equal(steamReads,0);
 }finally{p.bootPlatform.read=originalPlatform;p.steamUi.read=originalSteam;}
 return {xboxProbes:0,combinedSteamProbes:0,admissionDialogs:0};
});
await test('mode startup matrix repairs both Xbox modes, disables Xbox for Windows Steam and preserves it in global Windows',async()=>{
 const rows=[];
 for(const id of ['global','steam','xbox','xbox-steam'])for(const original of [true,false]){
  p.reset();p.state.xbox=original;const doc=await fresh();stored(doc);await b.chooseBootMode(id,doc,true);
  const writes=p.calls.filter(call=>call.op==='startup.xbox');const expected=id==='global'?original:id==='xbox'||id==='xbox-steam';assert.equal(p.state.xbox,expected);
  assert.equal(writes.length,id==='global'?0:1);assert.equal(p.state.steam,id==='steam'||id==='xbox-steam');rows.push({id,original,expected,xboxWrites:writes.length});
 }return rows;
});
await test('all four boot preparation paths immediately match Xbox or None selection, startup flag, handheld identity and original BAT without live launch',async()=>{
 const rows=[];
 for(const id of m.BOOT_MODE_IDS)for(const initialXbox of [true,false]){
  p.reset();p.state.xbox=initialXbox;p.state.deviceForm=3;p.state.steam=true;p.state.earlyCommand='previous-command';const doc=await fresh();stored(doc);const input=structuredClone(p.settings.input),home=p.state.homeApp;await b.chooseBootMode(id,doc,true);
  assert.equal(p.state.homeApp,id==='global'?home:id==='xbox'||id==='xbox-steam'?'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App':'');assert.deepEqual(p.settings.input,input);assert(!p.calls.some(c=>['app.launch','steam.launch','platform.set'].includes(c.op)));
  if(id==='global'){assert.equal(p.state.xbox,initialXbox);assert.equal(p.state.deviceForm,3);assert.equal(p.state.steam,false);assert.equal(p.state.earlyCommand,'');assert.deepEqual(changes().map(c=>c.op),['startup.steam','startup.commandRepair']);}
  else {assert.equal(p.state.xbox,id==='xbox'||id==='xbox-steam');assert.equal(p.state.deviceForm,46);assert.equal(p.state.steam,id==='steam'||id==='xbox-steam');assert.equal(p.state.earlyCommand,id==='xbox'?'':'"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"');}
  assert.deepEqual(await b.bootModeConnections(id),[]);rows.push({id,initialXbox,xbox:p.state.xbox,deviceForm:p.state.deviceForm,advanced:p.state.steam,homeApp:p.state.homeApp,homePreserved:id==='global',currentInputUnchanged:true});
 }return rows;
});
await test('all four mode selections preserve the independent common desktop startup ON or OFF choice',async()=>{
 const rows=[];for(const id of ['global','steam','xbox','xbox-steam'])for(const enabled of [true,false]){p.reset();p.state.legacyDesktop=enabled;p.settings.startupDesired.desktopModeOnBoot=enabled;const doc=await fresh();stored(doc);await b.chooseBootMode(id,doc,true);assert.equal(p.state.legacyDesktop,enabled);assert.equal(p.settings.startupDesired.desktopModeOnBoot,enabled);assert.equal(opNames().includes('legacy.desktop.remove'),false);rows.push({id,desktopRetained:p.state.legacyDesktop});}return rows;
});
await test('unrecorded desktop startup defaults ON once without running the script or altering current input',async()=>{
 const input=structuredClone(p.settings.input),games=gameState();await Promise.all(Array.from({length:4},()=>desktop.ensureDesktopModeStartupDefault()));assert.equal(p.state.legacyDesktop,true);assert.equal(p.settings.startupDesired.desktopModeOnBoot,true);assert.equal(p.calls.filter(c=>c.op==='desktop.task.set').length,1);assert.deepEqual(p.settings.input,input);assert.equal(gameState(),games);assert(p.calls.every(c=>['desktop.task.read','desktop.task.set','settings.update'].includes(c.op)));return {taskCreatedOnce:true,concurrentCalls:4,currentInputUnchanged:true};
});
await test('existing desktop task is remembered without recreating it',async()=>{
 p.state.legacyDesktop=true;await desktop.ensureDesktopModeStartupDefault();assert.equal(p.settings.startupDesired.desktopModeOnBoot,true);assert(!opNames().includes('desktop.task.set'));
});
await test('an explicit desktop startup OFF survives later app bootstrap and no default recreates it',async()=>{
 await desktop.ensureDesktopModeStartupDefault();await desktop.setDesktopModeStartup(false);assert.equal(p.state.legacyDesktop,false);assert.equal(p.settings.startupDesired.desktopModeOnBoot,false);const count=p.calls.length;await desktop.ensureDesktopModeStartupDefault();assert.equal(p.state.legacyDesktop,false);assert.equal(p.calls.length,count);await desktop.setDesktopModeStartup(true);assert.equal(p.state.legacyDesktop,true);assert.equal(p.settings.startupDesired.desktopModeOnBoot,true);
});
await test('desktop startup preference write failure restores the previous task and stays retryable',async()=>{
 p.faults['settings.update']=1;await assert.rejects(desktop.ensureDesktopModeStartupDefault(),/injected:settings.update/);assert.equal(p.state.legacyDesktop,false);assert.equal(p.settings.startupDesired.desktopModeOnBoot,undefined);assert.deepEqual(p.calls.filter(c=>c.op==='desktop.task.set').map(c=>c.value.enabled),[true,false]);await desktop.ensureDesktopModeStartupDefault();assert.equal(p.state.legacyDesktop,true);assert.equal(p.settings.startupDesired.desktopModeOnBoot,true);
});
await test('failed desktop task creation never records default success',async()=>{
 p.faults['desktop.task.set']=1;await assert.rejects(desktop.ensureDesktopModeStartupDefault(),/injected:desktop.task.set/);assert.equal(p.state.legacyDesktop,false);assert.equal(p.settings.startupDesired.desktopModeOnBoot,undefined);await desktop.ensureDesktopModeStartupDefault();assert.equal(p.state.legacyDesktop,true);
});
await test('desktop default bootstrap and immediate user OFF serialize in intent order',async()=>{
 await Promise.all([desktop.ensureDesktopModeStartupDefault(),desktop.setDesktopModeStartup(false)]);assert.equal(p.state.legacyDesktop,false);assert.equal(p.settings.startupDesired.desktopModeOnBoot,false);assert.deepEqual(p.calls.filter(c=>c.op==='desktop.task.set').map(c=>c.value.enabled),[true,false]);
});
await test('stale settings generation cannot create a desktop startup task',async()=>{
 p.hooks.settingsRead=()=>{p.invalidate();};await assert.rejects(desktop.ensureDesktopModeStartupDefault(),/stale generation/);assert.equal(p.state.legacyDesktop,false);assert.equal(p.settings.startupDesired.desktopModeOnBoot,undefined);assert(!opNames().includes('desktop.task.set'));
});
await test('saved overview does not wait for native mouse/device probes and never changes pending presets',async()=>{
 const doc=await fresh();doc.global.performance.controls.ac.tdpCeiling=49;doc.boot='xbox-steam';doc.bootEnabled=true;stored(doc);const before=structuredClone(p.settings),reads=p.state.mouseReads,calls=p.calls.length;
 p.hooks.mouseRead=()=>new Promise(()=>{});
 let timeout;try {
  const overview=await Promise.race([b.loadBootModes({overviewOnly:true}),new Promise((_,reject)=>timeout=setTimeout(()=>reject(Error('Overview blocked on device probes')),200))]);
  assert.equal(overview.document.boot,'xbox-steam');assert.equal(overview.document.global.performance.controls.ac.tdpCeiling,49);assert.deepEqual(overview.support,{});assert.equal(p.state.mouseReads,reads);assert.equal(p.calls.length,calls);assert.deepEqual(p.settings,before);
 }finally{clearTimeout(timeout);delete p.hooks.mouseRead;}
});
await test('first and partial overviews use saved settings without waiting for live devices; editing still captures fresh evidence',async()=>{
 if(process.argv.includes('--retired-fan-legacy')){
  const reference=await b.captureBootModeSnapshot(),rows=[];
  for(const mode of m.BOOT_MODE_IDS)for(const marker of [false,true])for(const editorOnly of [false,true]){
   p.reset();const document=m.createBootModeDocument(reference);
   document.boot=mode;document.current=mode;document.bootEnabled=mode!=='global';
   document.global.fan={enabled:true,preset:'aggressive'};document.activeSnapshot.fan={enabled:false,preset:'soft'};
   if(marker)document.unresolvedGlobal=['fan'];
   for(const scene of Object.values(document.scenes)){scene.overrides.fan={enabled:false};scene.rememberedOverrides={fan:{enabled:true}};}
   for(const id of m.BOOT_MODE_IDS)document.presets[id]={old:{name:'old',overrides:{fan:{enabled:false}},rememberedOverrides:{fan:{preset:'soft'}}}};
   const original=structuredClone(document);stored(document);const before=structuredClone(p.settings),calls=p.calls.length;
   const context=await b.loadBootModes(editorOnly?{editorOnly:true}:{overviewOnly:true});
   // Saving/applying may normalize a legacy document using its own old global
   // snapshot as baseline, not only a clean fresh capture.
   const normalized=m.normalizeBootModeDocument(document,document.global);
   const containers=[normalized.global,normalized.activeSnapshot,...Object.values(normalized.scenes).flatMap(scene=>[scene.overrides,scene.rememberedOverrides]),...Object.values(normalized.presets).flatMap(group=>Object.values(group).flatMap(scene=>[scene.overrides,scene.rememberedOverrides]))];
   rows.push({mode,marker,editorOnly,requiresCapture:context.requiresCapture,activeFan:normalized.activeSnapshot.fan??null,retiredEverywhere:containers.every(container=>container?.fan===undefined),sourceUnchanged:JSON.stringify(document)===JSON.stringify(original),settingsUnchanged:JSON.stringify(p.settings)===JSON.stringify(before),nativeCalls:p.calls.length-calls});
  }
  fs.writeFileSync(path.join(out,'retired-fan-legacy.json'),JSON.stringify({rows,actualRows:rows.length},null,2),{flag:'wx'});
  assert(rows.every(row=>!row.requiresCapture&&row.retiredEverywhere&&row.sourceUnchanged&&row.settingsUnchanged&&row.nativeCalls===0),'Retired fan metadata triggered a live recapture or survived a legacy baseline: '+JSON.stringify(rows.filter(row=>row.requiresCapture||!row.retiredEverywhere)));
  return {actualRows:rows.length,legacySourcesUnchanged:true,allModeFanLocationsRetired:true,noLiveCapture:true};
 }
 if(process.argv.includes('--first-document-performance-race')){
  const a=await runtimeBundle(out),q=a.ports,oldWatts=q.settings.performanceSchedule.controls.ac.tdp;
  let changed=false;q.hooks.deckyGet=()=>{
   if(!changed){changed=true;q.settings.performanceSchedule.controls.ac.tdp=88;q.settings.performanceSchedule.controls.ac.tdpCeiling=88;}
   return {enabled:false,phase:'off'};
  };
  const full=await a.boot.loadBootModes();
  assert(changed);assert.equal(full.document.global.performance.controls.ac.tdp,88,'first document must use the newly saved official performance bank');
  assert.equal(full.document.activeSnapshot.performance.controls.ac.tdp,oldWatts,'captured reference must stay separate');
  assert.equal(q.settings.extensions.bootModes,undefined);assert(!q.calls.some(row=>row.op==='settings.update'||row.op==='performance.save'));
  const stored=structuredClone(full.document);stored.global.performance.controls.ac.tdp=77;stored.global.performance.schedule.controls.ac.tdp=77;q.settings.extensions.bootModes=stored;
  q.settings.performanceSchedule.controls.ac.tdp=99;q.settings.performanceSchedule.controls.ac.tdpCeiling=99;
  const again=await a.boot.loadBootModes();assert.equal(again.document.global.performance.controls.ac.tdp,77,'existing template remains authoritative');
  return {firstDocumentNewWatts:88,capturedWatts:oldWatts,existingTemplateWatts:77,currentBankWatts:99,nativeWrites:0};
 }
 if(process.argv.includes('--durable-full-editor-seed')){
  const rows=[];
  for(const fan of [false,true])for(const changedDuringCapture of [false,true]){
   const a=await runtimeBundle(out),q=a.ports;
   q.settings.startupDesired.fanControl=fan;q.state.fanIntent=!fan;
   q.settings.ui={...q.settings.ui,theme:'blue-black',backgroundOpacity:0.42,backgroundBlur:9,dynamicBackgroundEnabled:true,videoBatteryPause:false,
    steamDynamicWallpaperEnabled:false,steamWallpaperBackgroundDownload:false,steamWallpaperDownloadCpuThreshold:24};
   q.settings.sleep={mode:'custom',pauseGameOnSleep:fan,retryOnEntryFailure:!fan,retryOnSleepTimeout:fan,retryOnNonUserWake:!fan,wakePasswordEnabled:fan,sleepPowerPlanOptimizationEnabled:true,staticWakeAutoScanEnabled:false};
   q.settings.screenTouchpads={profiles:{disabled:{...q.screenTouchpadDefaults('disabled'),masterEnabled:false,transparency:47}}};
   q.settings.rtss.monitorOutput={enabled:false,output:'off',ymccEnabled:false,rtssEnabled:false,ymccTemplate:'B'};
   const initial=structuredClone(q.settings);let changed=false;
   if(changedDuringCapture)q.hooks.deckyGet=()=>{
    if(!changed){changed=true;q.settings.ui.backgroundBlur=13;q.settings.sleep.pauseGameOnSleep=!fan;q.settings.input.gyroMotion.gyroWeight=2.2;}
    return {enabled:false,phase:'off'};
   };
   const full=await a.boot.loadBootModes();
   const same=await a.boot.loadBootModes({overviewOnly:true});
   assert.equal(full.document.global.fan,undefined,'Common fan preference entered a mode template');
   assert.equal(full.document.activeSnapshot.fan,undefined,'Common fan session entered the mode snapshot');
   assert.equal(full.document.global.background.ui.backgroundBlur,changedDuringCapture?13:9);
   assert.equal(full.document.global.background.ui.backgroundOpacity,0.42);assert.equal(full.document.global.background.ui.videoBatteryPause,false);
   assert.equal(full.document.global.steamWallpaper.enabled,false);assert.equal(full.document.global.steamWallpaper.download,false);assert.equal(full.document.global.steamWallpaper.cpuThreshold,24);
   assert.equal(full.document.global.sleep.config.sleepPowerPlanOptimizationEnabled,true);assert.equal(full.document.global.sleep.config.staticWakeAutoScanEnabled,false);
   assert.equal(full.document.global.sleep.config.pauseGameOnSleep,changedDuringCapture?!fan:fan);
   assert.equal(full.document.global.touch.profiles.disabled.transparency,47);assert.equal(full.document.global.touch.profiles.disabled.masterEnabled,false);
   if(changedDuringCapture)assert.equal(full.document.global.gyro.motion.gyroWeight,2.2);
   for(const module of ['input','gyro','background','steamWallpaper','monitor'])assert.deepEqual(full.document.global[module],same.document.global[module],module+' first-entry durable source mismatch');
   const persisted=structuredClone(full.document);persisted.global.fan={enabled:!fan};persisted.global.background.ui.backgroundBlur=6;q.settings.extensions.bootModes=persisted;
   const existing=await a.boot.loadBootModes();assert.equal(existing.document.global.fan,undefined);assert.equal(q.settings.startupDesired.fanControl,fan);assert.equal(existing.document.global.background.ui.backgroundBlur,6);
   assert(!q.calls.some(row=>row.op==='settings.update'||row.op==='fan.start'||row.op==='fan.close'||row.op==='ui.set'||row.op==='touch.set'),'opening editor must remain read only');
   const after=structuredClone(q.settings);delete after.extensions.bootModes;
   if(changedDuringCapture){initial.ui.backgroundBlur=13;initial.sleep.pauseGameOnSleep=!fan;initial.input.gyroMotion.gyroWeight=2.2;}
   assert.deepEqual(after,initial);
   rows.push({fan,changedDuringCapture,latestDurableUiAndGyroPreserved:true,runtimeFanReference:!fan,existingTemplatePreserved:true,editorWrites:0});
  }
  return rows;
 }
 if(process.argv.includes('--durable-startup-seed')){
  const rows=[];
  for(const decky of [true,false,null,'invalid'])for(const recovery of [true,false,null,'invalid']){
   const a=await runtimeBundle(out),q=a.ports;
   if(decky!==null)q.settings.extensions.deckySidebar={enabled:decky,keep:'ordinary-native-choice'};
   if(recovery!==null)q.settings.sleep.autoWakeRecovery=recovery;
   // Legacy ui data and runtime owner state are not the durable Decky source.
   q.settings.ui.deckyEnabled=decky!==true;q.hooks.deckyGet=()=>{throw Error('saved overview must not query Decky')};
   q.hooks.recoveryRead=()=>{throw Error('saved overview must not query recovery')};
   const before=structuredClone(q.settings),calls=q.calls.length;
   const expectedDecky=typeof decky==='boolean'?decky:null,expectedRecovery=typeof recovery==='boolean'?recovery:null;
   const shown=await a.boot.loadBootModes({overviewOnly:true});
   assert.equal(shown.document.global.steamDecky.enabled,expectedDecky,'saved native Decky intent must seed the first global document');
   assert.equal(shown.document.global.sleep.steamAutoRecovery,expectedRecovery,'saved recovery intent must seed the first global document');
   assert.deepEqual(q.settings,before);assert.equal(q.calls.length,calls,'saved-only seed must not perform native writes');
   if(typeof decky==='boolean'&&typeof recovery==='boolean'){
    q.hooks.deckyGet=()=>({enabled:!decky,phase:'off'});q.state.recovery={ok:true,enabled:!recovery};
    const editor=await a.boot.loadBootModes();
    assert.equal(editor.baseline.steamDecky.enabled,decky,'durable first editor intent must outrank temporary native state');
    assert.equal(editor.document.global.steamDecky.enabled,decky);assert.equal(editor.document.global.sleep.steamAutoRecovery,recovery);
    assert.deepEqual(q.settings,before,'full editor remains read only');
   }
   const stored=structuredClone(shown.document);stored.global.steamDecky.enabled=null;stored.global.sleep.steamAutoRecovery=null;q.settings.extensions.bootModes=stored;
   const existing=await a.boot.loadBootModes({overviewOnly:true});
   assert.equal(existing.document.global.steamDecky.enabled,null,'existing unknown template remains authoritative');assert.equal(existing.document.global.sleep.steamAutoRecovery,null);
   rows.push({decky,recovery,seededDecky:expectedDecky,seededRecovery:expectedRecovery,existingUnknownPreserved:true,nativeReads:0});
  }
  return rows;
 }
 const before=structuredClone(p.settings),reads=p.state.mouseReads;let timer;
 p.hooks.mouseRead=()=>new Promise(()=>{});
 try {
  const initial=await Promise.race([b.loadBootModes({overviewOnly:true}),new Promise((_,reject)=>timer=setTimeout(()=>reject(Error('First overview blocked on device probes')),200))]);
  assert.equal(p.state.mouseReads,reads);assert.deepEqual(initial.support,{});assert.equal(initial.requiresCapture,true);
  assert.equal(initial.document.global.input.persona,'disabled');assert.equal(initial.document.global.input.mouse,'joyxoff');
  assert.equal(initial.document.global.charge.enabled,null);assert.equal(initial.document.global.steamDecky.enabled,null);assert.deepEqual(p.settings,before);
  const malformed=structuredClone(initial.document);malformed.global.performance.controls.ac.tdpCeiling=49;delete malformed.global.sleep;stored(malformed);
  const partial=await b.loadBootModes({overviewOnly:true});assert.equal(partial.requiresCapture,true);assert.equal(partial.document.global.performance.controls.ac.tdpCeiling,49);assert.equal(partial.document.global.sleep.hibernate,undefined);assert.equal(partial.baseline.sleep.hibernate,null);assert(partial.document.unresolvedGlobal.includes('sleep'));assert.equal(p.state.mouseReads,reads);
 }finally{clearTimeout(timer);delete p.hooks.mouseRead;}
 const editor=await b.loadBootModes();assert(p.state.mouseReads>reads);assert(editor.support.performance);assert(editor.baseline.sleep);
 return {firstAndPartialNoNativeProbe:true,unknownsStayUnknown:true,editStillRefreshes:true};
});
await test('full editor reads device support without importing ordinary-page changes into saved presets',async()=>{
 const doc=await fresh();stored(doc);p.settings.performanceSchedule.controls.ac.tdpCeiling=44;
 for(const options of [{overviewOnly:true},{editorOnly:true},{}]){const context=await b.loadBootModes(options);assert.equal(context.document.global.performance.controls.ac.tdpCeiling,30);assert.deepEqual(context.document.global,doc.global);assert.deepEqual(context.document.scenes,doc.scenes);if(!options.overviewOnly&&!options.editorOnly)assert(context.support.performance);}
 assert.equal(p.settings.extensions.bootModes.revision,doc.revision);
});
await test('global OFF survives an actual combined-mode SD startup and returns OFF without template contamination',async()=>{
 p.settings.input.outputTarget={...p.settings.input.outputTarget,persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false};p.settings.gamepad.mouseMode='off';p.state.mouse={...p.state.mouse,on:false,mode:'off'};
 const doc=await fresh();doc.scenes['xbox-steam'].overrides.input=structuredClone(oldSteamInput);stored(doc);const savedGlobal=structuredClone(doc.global),savedScenes=structuredClone(doc.scenes);
 const combined=await b.chooseBootMode('xbox-steam',doc,true);assert.equal(p.settings.startupDesired.virtualGamepadPersona,'steamdeck');
 const {document:running,receipt}=await startMode('xbox-steam',combined);assert.equal(p.settings.input.outputTarget.persona,'steamdeck');assert.equal(running.current,'xbox-steam');assert.deepEqual(running.global,savedGlobal);assert.deepEqual(running.scenes,savedScenes);
 const live=structuredClone(p.settings.input);const context=await b.loadBootModes({editorOnly:true});assert.deepEqual(context.document.global,savedGlobal);assert.deepEqual(context.document.scenes,savedScenes);
 context.document.global.performance.controls.ac.tdpCeiling=43;const saved=await b.saveBootModes(context.document);assert.deepEqual(saved.global.input,savedGlobal.input);assert.deepEqual(saved.global.gyro,savedGlobal.gyro);
 const reopened=await b.loadBootModes();assert.deepEqual(reopened.document.global.input,savedGlobal.input);const back=await b.chooseBootMode('global',reopened.document,true);
 assert.equal(back.boot,'global');assert.equal(p.settings.startupDesired.virtualGamepadPersona,'disabled');assert.equal(p.settings.startupDesired.gyroPreset,'off');assert.deepEqual(p.settings.input,live);assert.deepEqual(back.scenes,savedScenes);
 const templates=structuredClone({global:back.global,scenes:back.scenes});const {document:returned}=await startMode('global',back);assert.equal(p.settings.input.outputTarget.persona,'disabled');assert.equal(returned.current,'global');assert.deepEqual({global:returned.global,scenes:returned.scenes},templates);
 const again=await b.loadBootModes();assert.deepEqual({global:again.document.global,scenes:again.document.scenes},templates);
 return {savedGlobalPersona:'disabled',combinedStartupPersona:'steamdeck',runningPersonaBeforeReturn:'steamdeck',returnedStartupPersona:'disabled',appliedReturnedPersona:p.settings.input.outputTarget.persona,templatesUnchanged:true,hotApply:false};
});
await test('all four applied scenes keep inherited and independent templates isolated from later runtime edits',async()=>{
 const rows=[];for(const shape of ['complete','saved-optional-fields-absent'])for(const id of m.BOOT_MODE_IDS){
  p.reset();p.settings.input.outputTarget={...p.settings.input.outputTarget,persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false};p.settings.gamepad.mouseMode='off';p.state.mouse={...p.state.mouse,on:false,mode:'off'};const doc=await fresh();
  if(shape==='saved-optional-fields-absent'){delete doc.global.input.ownership;delete doc.global.input.outputTarget.descriptorHash;delete doc.global.gyro.motion.calibration;}
  doc.scenes.xbox.overrides.gyro={motion:{...structuredClone(doc.global.gyro.motion),enabled:false,gyroWeight:2.1}};doc.scenes['xbox-steam'].overrides.input={...structuredClone(doc.global.input),persona:'elite',mouse:'off',outputTarget:{...doc.global.input.outputTarget,persona:'elite',buttonMappingEnabled:true}};
  doc.current=id;doc.activeSnapshot=m.resolveBootMode(doc,id);stored(doc);const templates=structuredClone({global:doc.global,scenes:doc.scenes});
  p.settings.input.outputTarget={...p.settings.input.outputTarget,persona:'dualsense-edge',buttonMappingEnabled:true};p.settings.input.gyroMotion={...p.settings.input.gyroMotion,enabled:true,gyroWeight:7};p.settings.performanceSchedule.controls.ac.tdpCeiling=51;p.state.ui.backgroundBlur=15;
  for(const options of [{overviewOnly:true},{editorOnly:true},{}]){const read=await b.loadBootModes(options);assert.deepEqual({global:read.document.global,scenes:read.document.scenes},templates,shape+' '+id+' must not fill saved templates from live state');if(shape==='complete')assert.deepEqual(read.document.activeSnapshot,doc.activeSnapshot);}
  const read=await b.loadBootModes({editorOnly:true});const saved=await b.saveBootModes(read.document);assert.deepEqual({global:saved.global,scenes:saved.scenes},templates);const back=await b.chooseBootMode('global',saved,true);assert.equal(p.settings.startupDesired.virtualGamepadPersona,'disabled');assert.equal(p.settings.startupDesired.gyroPreset,'off');assert.equal(back.scenes['xbox-steam'].overrides.input.persona,'elite');rows.push({id,shape,globalOff:true,independentCombined:'elite',runtimePersona:'dualsense-edge',templatesUnchanged:true});
 }return rows;
});
await test('explicitly saved edits change only the selected preset module for every mode',async()=>{
 const rows=[];for(const id of m.BOOT_MODE_IDS){
  p.reset();p.settings.input.outputTarget={...p.settings.input.outputTarget,persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false};const doc=await fresh();doc.boot='xbox-steam';doc.bootEnabled=true;doc.current='xbox-steam';doc.activeSnapshot=m.resolveBootMode(doc,'xbox-steam');stored(doc);const original=structuredClone(doc);
  p.settings.input.outputTarget.persona='steamdeck';const read=await b.loadBootModes({editorOnly:true});assert.deepEqual(read.document.global,doc.global);assert.deepEqual(read.document.scenes,doc.scenes);
  const target=id==='global'?read.document.global:read.document.scenes[id].overrides;target.input={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'off',outputTarget:{...doc.global.input.outputTarget,persona:'dualsense-edge',buttonMappingEnabled:true}};
  const saved=await b.saveBootModes(read.document);assert.equal(saved.boot,'xbox-steam');assert.equal(p.settings.startupDesired.virtualGamepadPersona,m.resolveBootMode(saved,'xbox-steam').input.persona,'saving another mode changed the selected next-boot source');assert.equal(p.settings.input.outputTarget.persona,'steamdeck');if(id==='global'){assert.deepEqual(saved.scenes,original.scenes);}else{assert.deepEqual(saved.global,original.global);for(const other of m.BOOT_MODE_IDS.filter(other=>other!=='global'&&other!==id))assert.deepEqual(saved.scenes[other],original.scenes[other]);}
  const back=await b.chooseBootMode(id,saved,true);assert.equal(p.settings.startupDesired.virtualGamepadPersona,'dualsense-edge');assert.equal(m.resolveBootMode(back,id).input.persona,'dualsense-edge');assert.equal(p.settings.input.outputTarget.persona,'steamdeck');rows.push({id,savedPersona:'dualsense-edge',otherTemplatesUnchanged:true});
 }return rows;
});
// Shorten only the production read deadline; a separate watchdog still catches
// the former never-settling behavior. No native writes are given fake timeouts.
const fastReadClock=async(run)=>{const original=globalThis.setTimeout;globalThis.setTimeout=(callback,ms,...args)=>original(callback,ms===4000?15:ms,...args);try{return await run();}finally{globalThis.setTimeout=original;}};
await test('unanswered device reads become unknown, retries share one request and a late reply cannot change the returned snapshot',async()=>{
 const before=structuredClone(p.settings);let release;const held=new Promise(resolve=>release=resolve);p.hooks.mouseRead=()=>held;const reads=p.state.mouseReads;
 try{await fastReadClock(async()=>{const first=await waitSignal(b.captureBootModeSnapshot(),'bounded device read');assert.equal(first.input.mouse,null);const second=await waitSignal(b.captureBootModeSnapshot(),'bounded shared device retry');assert.equal(second.input.mouse,null);assert.equal(p.state.mouseReads,reads+1,'Retry submitted another unanswered mouse request');assert.deepEqual(p.settings,before);release();delete p.hooks.mouseRead;await new Promise(resolve=>setImmediate(resolve));assert.equal(first.input.mouse,null);const recovered=await b.captureBootModeSnapshot();assert.equal(recovered.input.mouse,'joyxoff');assert.equal(p.state.mouseReads,reads+2);assert.deepEqual(p.settings,before);});}finally{release();delete p.hooks.mouseRead;}
 return {timeoutIsUnknown:true,concurrentReadRequests:1,automaticRetry:false,lateReplyDoesNotEditSnapshot:true,recoveryOnExplicitRead:true};
});
await test('unanswered essential settings reads show a retryable error and a new settings generation does not reuse the old pending read',async()=>{
 const doc=await fresh();stored(doc);const before=structuredClone(p.settings);let release,reads=0;const held=new Promise(resolve=>release=resolve);p.hooks.settingsRead=()=>{reads++;return held;};
 try{await fastReadClock(async()=>{await assert.rejects(waitSignal(b.loadBootModes({overviewOnly:true}),'bounded settings read'),/读取超时/);await assert.rejects(waitSignal(b.loadBootModes({overviewOnly:true}),'bounded settings retry'),/读取超时/);assert.equal(reads,1);assert.deepEqual(p.settings,before);p.invalidate();delete p.hooks.settingsRead;const current=await waitSignal(b.loadBootModes({overviewOnly:true}),'fresh settings generation');assert.equal(current.document.revision,doc.revision);release();await new Promise(resolve=>setImmediate(resolve));assert.deepEqual(p.settings,before);assert.equal(p.calls.length,0);});}finally{release();delete p.hooks.settingsRead;}
 return {readTimeouts:2,underlyingRequests:1,newGenerationIndependent:true,nativeWrites:0,automaticRetries:0};
});
await test('duplicate global application paths preserve the existing name across Windows case and slash spelling',async()=>{
 const before=structuredClone(p.settings.quickApps);await b.addBootSharedApplication('Do not rename',String.raw`c:\fixture\APP.exe`);assert.deepEqual(p.settings.quickApps,before);assert(!p.calls.some(c=>c.op==='app.launch'));return {globalNameUnchanged:true,duplicateEntries:0,launchedApplications:0};
});
await test('all sixteen scene transitions preserve the current runtime and follow the latest Xbox/Steam startup matrix',async()=>{
 const evidence=[];
 for(const from of ['global','steam','xbox','xbox-steam'])for(const to of ['global','steam','xbox','xbox-steam']){
  p.reset();p.state.xbox=true;const doc=await fresh();stored(doc);const current=structuredClone(p.settings.input),games=gameState();const first=await b.chooseBootMode(from,doc,true);const previousXbox=p.state.xbox,previousSteam=p.state.steam,beforeCalls=p.calls.length;const next=await b.chooseBootMode(to,first,true);const expectedXbox=to==='global'?previousXbox:to==='xbox'||to==='xbox-steam';assert.equal(p.state.xbox,expectedXbox);assert.equal(p.state.steam,to==='steam'||to==='xbox-steam');assert.equal(next.current,'global');assert.equal(next.boot,to);assert.deepEqual(p.settings.input,current);assert.equal(gameState(),games);assert.equal(p.calls.slice(beforeCalls).filter(row=>row.op==='startup.xbox').length,to==='global'?0:1);evidence.push({from,to,xbox:expectedXbox,steam:p.state.steam,currentUnchanged:true});
 }return evidence;
});
await test('all 16 mode transitions commit Windows Xbox or None before reboot while preserving live input',async()=>{
 const rows=[];for(const from of m.BOOT_MODE_IDS)for(const to of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();stored(doc);const oldInput=structuredClone(p.settings.input);const first=await b.chooseBootMode(from,doc,true);const beforeHome=p.state.homeApp,beforeFlag=p.state.xbox;
  const next=await b.chooseBootMode(to,first,true);const expectedHome=to==='global'?beforeHome:to==='xbox'||to==='xbox-steam'?'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App':'';
  assert.equal(p.state.homeApp,expectedHome);assert.equal(p.state.xbox,to==='global'?beforeFlag:to==='xbox'||to==='xbox-steam');assert.deepEqual(p.settings.input,oldInput);assert.equal(next.current,doc.current);
  rows.push({from,to,home:p.state.homeApp,immediate:true,liveInputUnchanged:true});
 }return rows;
});
await test('failed home selection is retained as pending, and explicit selection retry repairs it now',async()=>{
 const doc=await fresh();stored(doc);const live=structuredClone(p.settings.input);p.faults['startup.home']=1;const first=await b.chooseBootMode('xbox-steam',doc,true);assert.match(first.bootNotice,/Windows 开机配置.*startup.home/);assert.notEqual(p.state.homeApp,'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App');
 const retry=await b.chooseBootMode('xbox-steam',first,true);assert.equal(retry.bootNotice,undefined);assert.equal(p.state.homeApp,'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App');assert.equal(p.state.xbox,true);assert.deepEqual(p.settings.input,live);
});
await test('failed platform startup writes retain an honest pending notice and explicit retry clears it after success',async()=>{
 const doc=await fresh();stored(doc);p.faults['startup.xbox']=1;const first=await b.chooseBootMode('xbox-steam',doc,true);assert.match(first.bootNotice,/Windows 开机配置.*injected/);assert.match(p.settings.extensions.bootModes.bootNotice,/Windows 开机配置/);assert.equal(first.current,'global');const retry=await b.chooseBootMode('xbox-steam',first,true);assert.equal(retry.bootNotice,undefined);assert.equal(p.state.xbox,true);assert.equal(p.state.steam,true);
});
await test('choosing boot is startup selection only, independent from current runtime and applications',async()=>{
 const doc=await fresh(),games=gameState();doc.current='xbox';doc.boot='global';doc.scenes.steam.overrides.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'scene'};stored(doc);const before=JSON.stringify(p.settings.input);
 const next=await b.chooseBootMode('steam',doc,true);assert.equal(next.boot,'steam');assert.equal(next.current,'xbox');assert.equal(next.bootEnabled,true);assert.equal(JSON.stringify(p.settings.input),before);assert.equal(gameState(),games);
 assert.deepEqual(changes().map(c=>c.op).sort(),['addon.set','fx.set','registry.read','startup.commandRepair','startup.handheld','startup.home','startup.steam','startup.xbox']);assert.deepEqual(p.calls.filter(c=>c.op==='fx.set'||c.op==='addon.set').map(c=>c.op==='fx.set'?c.value:c.value.enabled),[false,false]);assert.equal(p.state.steam,true);assert.equal(p.state.xbox,false);
});
await test('non-startup mode application rejects immediately for every mode without touching settings, native IO, input, or event ownership',async()=>{
 const doc=await fresh();stored(doc);const before=structuredClone(p.settings),native=structuredClone(p.state),events=structuredClone(p.ipcEvents);const rejected=[];
 for(const id of ['global','xbox','steam','xbox-steam'])for(const options of [undefined,{}, {startup:false},{startup:'true'},{startup:1}]){await assert.rejects(()=>b.applyBootMode(id,doc,options),/开机|重启|startup/i);assert.deepEqual(p.settings,before);assert.deepEqual(p.state,native);assert.deepEqual(p.calls,[]);assert.deepEqual(p.ipcEvents,events);rejected.push({id,startup:options?.startup??null});}
 await assert.rejects(()=>b.applyBootMode('steam',null),/开机|重启|startup/i);assert.deepEqual(p.settings,before);assert.deepEqual(p.calls,[]);return {rejectedCalls:rejected.length+1,nativeWrites:0,settingsWrites:0,eventOwnershipChanges:0};
});
await test('startup engine applies every mode without manually entering Steam or Xbox, cold-launching platforms, or rewriting OS startup preferences',async()=>{
 const evidence=[];for(const id of ['global','xbox','steam','xbox-steam']){p.reset();const doc=await fresh(),games=gameState();doc.boot=id;doc.bootEnabled=true;stored(doc);p.state.steamUi.known=false;p.faults['steam.launch']=1;const platformBefore={xbox:p.state.xbox,steamUi:structuredClone(p.state.steamUi)};const {document:next,receipt}=await startMode(id,doc);assert.equal(next.current,id);assert.equal(next.boot,id);assert.equal(next.bootEnabled,true);assert.equal(gameState(),games);assert.deepEqual({xbox:p.state.xbox,steamUi:p.state.steamUi},platformBefore);assert(!p.calls.some(c=>c.op.startsWith('steam.')||c.op==='platform.set'||c.op.startsWith('startup.')));assert(!receipt.applied.some(value=>value.includes('Steam 大屏')||value.includes('Xbox 全屏')));evidence.push({mode:id,currentAfterStartup:next.current,manualPlatformCalls:0,gameProfilesUnchanged:true});}return evidence;
});
await test('bootEnabled false disables both startup platforms and invokes no runtime or mouse owners',async()=>{
 const doc=await fresh();doc.bootEnabled=true;p.state.xbox=true;p.state.steam=true;stored(doc);const next=await b.chooseBootMode('xbox-steam',doc,false);
 assert.equal(next.boot,'xbox-steam');assert.equal(next.bootEnabled,false);assert.equal(next.current,'global');assert.equal(p.state.xbox,false);assert.equal(p.state.steam,false);assert(changes().every(startupOnly));
});
await test('stale boot selection fails before external startup writes and keeps newer revision',async()=>{
 const doc=await fresh();const newer=structuredClone(doc);newer.revision=4;newer.boot='xbox';stored(newer);const before=JSON.stringify(p.settings);await assert.rejects(b.chooseBootMode('steam',doc,true));assert.deepEqual(changes(),[]);assert.equal(JSON.stringify(p.settings),before);
 for(const id of m.BOOT_MODE_IDS){
  const a=await runtimeBundle(out),q=a.ports,draft=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());q.settings.extensions.bootModes=structuredClone(draft);
  let enter,release;const entered=new Promise(r=>enter=r),held=new Promise(r=>release=r);
  q.hooks.bootIdentity=()=>{enter();return held;};
  const settled=a.boot.chooseBootMode(id,draft).then(value=>({value}),error=>({error}));
  await waitSignal(entered,'mode identity before settings reset');
  q.invalidate();const replacement=structuredClone(draft);replacement.global.background.ui.backgroundOpacity=.6;q.settings.extensions.bootModes=replacement;
  const beforeReset=structuredClone(q.settings),atReset=q.calls.length;release('fixture-windows-boot-1');
  const result=await settled;assert.match(String(result.error),/stale generation/);
  assert.deepEqual(q.settings,beforeReset,'A mode request from the old settings generation wrote into the new settings');
  assert.deepEqual(q.calls.slice(atReset),[],'Stale boot selection entered the settings transaction or dispatched startup');
 }
 for(const operation of ['selection','save']){
  const a=await runtimeBundle(out),q=a.ports,draft=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());q.settings.extensions.bootModes=structuredClone(draft);
  let enter,release;const entered=new Promise(r=>enter=r),held=new Promise(r=>release=r);
  q.hooks.bootIdentity=()=>{enter();return held;};
  const block=a.boot.saveBootModes(draft).then(value=>({value}),error=>({error}));
  await waitSignal(entered,'first save holding the boot-mode queue');
  const waiting=(operation==='selection'?a.boot.chooseBootMode('steam',draft):a.boot.saveBootModes(draft)).then(value=>({value}),error=>({error}));
  q.invalidate();const beforeReset=structuredClone(q.settings);delete q.hooks.bootIdentity;release('fixture-windows-boot-1');
  assert.match(String((await block).error),/stale generation/);assert.match(String((await waiting).error),/stale generation/);
  assert.deepEqual(q.settings,beforeReset,'An older queued settings request reached the replacement generation');
 }
 return {staleRevisionRejected:true,generationWhileReadingIdentity:4,staleQueuedRequests:2};
});
await test('startup fallback materializes unconfigured global including an old same-boot ghost marker',async()=>{
 if(process.argv.includes('--startup-claim-sparse-storage')){
  const {runStartupClaimSparseStorage}=await import('./boot_modes_fixture/preparation.mjs');
  return await runStartupClaimSparseStorage(out,{native:globalThis.__bootModeSaveNative,seed:await b.captureBootModeSnapshot(),model:m});
 }
 const evidence=[];
 for(const ghost of [false,true]){
  const a=await runtimeBundle(out);a.ports.reset();
  a.ports.settings.extensions=ghost?{bootModesAppliedBoot:'fixture-windows-boot-1'}:{};
  a.ports.settings.startupDesired.virtualGamepadPersona='elite';a.ports.settings.startupDesired.gyroPreset='off';
  a.ports.settings.startupDesired.fanControl=false;
  a.ports.settings.input.outputTarget={...a.ports.settings.input.outputTarget,persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false};
  a.ports.state.steam=true;a.ports.state.earlyCommand='"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
  const games=structuredClone(a.ports.settings.gameProfiles);
  await a.boot.restoreSelectedBootMode();
  const saved=a.ports.settings.extensions.bootModes;
  assert.equal(saved.schema,1);assert.equal(saved.current,'global');assert.equal(saved.boot,'global');
  assert.equal(saved.global.input.persona,'elite');assert.equal(saved.global.fan,undefined);assert.equal(a.ports.settings.startupDesired.fanControl,false);
  assert.equal(a.ports.settings.input.outputTarget.persona,'elite');assert.equal(a.ports.state.steam,false);
  assert.equal(a.ports.settings.extensions.bootModesStartupResult.mode,'global');
  assert.deepEqual(a.ports.settings.gameProfiles,games);
  const revision=saved.revision,operations=a.ports.calls.length;await a.boot.restoreSelectedBootMode();
  assert.equal(a.ports.calls.length,operations);assert.equal(a.ports.settings.extensions.bootModes.revision,revision);
  evidence.push({ghost,materialized:true,current:'global',startupPersona:'elite',advancedSteam:false,appliedOnce:true});
 }
 return evidence;
});
await test('startup fallback preserves hardware-aware first-install performance provenance after a ghost claim',async()=>{
 const {createPerformanceMemoryFixture}=await import('./fixtures/performance_memory_fixture.mjs');
 const a=await runtimeBundle(out),q=a.ports;
 q.settings.extensions={bootModesAppliedBoot:'fixture-windows-boot-1'};
 q.settings.performanceSchedule=structuredClone(JSON.parse(fs.readFileSync('PowerControl/settings-defaults-v1.json','utf8')).performanceSchedule);
 const performance=createPerformanceMemoryFixture({ports:{initialSettings:structuredClone(q.settings),template:'desktop'}});
 Object.assign(performance.settingsRepository,{loadSettings:q.loadSettings,getSettingsGeneration:q.getSettingsGeneration,assertSettingsGeneration:q.assertSettingsGeneration});
 q.state.performanceLoad=performance.schedule.loadPerformanceSchedule;
 q.state.performanceSnapshot=performance.schedule.snapshotPerformanceSchedule;
 q.state.performanceControls=performance.schedule.continuousControlsFromSchedule;
 const shown=await performance.schedule.loadPerformanceSchedule();
 assert.equal(performance.schedule.continuousControlsFromSchedule(shown).ac.tdp,120);
 await a.boot.restoreSelectedBootMode();
 const saved=q.settings.extensions.bootModes;
 for(const bank of [saved.global.performance.controls.ac,saved.activeSnapshot.performance.controls.ac,q.settings.performanceSchedule.controls.ac]){
  assert.equal(bank.tdp,120);assert.equal(bank.tdpCeiling,120);assert.equal(bank.auto.bias,0);
 }
 assert.equal(q.settings.extensions.bootModesStartupResult.modules.performance.requested.controls.ac.tdp,120);
 return {desktopDefault:120,cpuBias:0,originalSchedulerReader:true,ghostClaimMaterializedOnce:true,physicalOperations:false};
});
await test('startup fallback never promotes an ordinary temporary virtual-pad toggle into global startup',async()=>{
 const a=await runtimeBundle(out);a.ports.reset();a.ports.settings.extensions={};
 a.ports.settings.startupDesired.virtualGamepadPersona='disabled';a.ports.settings.startupDesired.gyroPreset='off';
 a.ports.settings.input.outputTarget={...a.ports.settings.input.outputTarget,persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true};
 await a.boot.restoreSelectedBootMode();
 assert.equal(a.ports.settings.extensions.bootModes.global.input.persona,'disabled');
 assert.equal(a.ports.settings.input.outputTarget.persona,'disabled');
 assert.equal(a.ports.settings.startupDesired.virtualGamepadPersona,'disabled');
});
await test('startup fallback preserves malformed documents instead of materializing over them',async()=>{
 for(const bad of [[],[{}],true,false,0,'steam']){
  const a=await runtimeBundle(out);a.ports.reset();a.ports.settings.extensions={bootModes:structuredClone(bad)};
  const before=structuredClone(a.ports.settings);await assert.rejects(a.boot.restoreSelectedBootMode(),/配置格式无效/);
  assert.deepEqual(a.ports.settings,before);assert(!a.ports.calls.some(row=>row.op==='settings.update'||row.op.startsWith('startup.')||row.op==='input.cas'));
 }
});
await test('startup fallback repairs an old disabled-scene ghost once but leaves an explicit same-boot selection deferred',async()=>{
 if(process.argv.includes('--boot-identity-handoff-native')){
  const {runBootIdentityHandoff,runInputOnlyBootIdentityHandoff}=await import('./boot_modes_fixture/preparation.mjs');
  const run=process.argv.includes('--identity-input-only')?runInputOnlyBootIdentityHandoff:runBootIdentityHandoff;
  return await run(out,{native:globalThis.__bootModeSaveNative,seed:await b.captureBootModeSnapshot(),model:m});
 }
 const boot='fixture-windows-boot-1', nextBoot='fixture-windows-boot-2', rows=[];
 const native=globalThis.__bootModeSaveNative;
 if(native){
  const {createDiskRuntime}=await import('./boot_modes_fixture/diskSettings.mjs');
  const initial=JSON.parse(fs.readFileSync(path.resolve('PowerControl/settings-defaults-v1.json'),'utf8'));
  for(const id of ['xbox','steam','xbox-steam'])for(const selection of [false,true]){
   const disk=await createDiskRuntime(path.join(out,`defer-${id}-${selection}`),native,{moduleDirectory:path.join(out,'deferred-runtime')});
   const doc=m.createBootModeDocument(await b.captureBootModeSnapshot());
   doc.boot=id;doc.bootEnabled=false;doc.current=id;
   doc.global.input={...doc.global.input,persona:'elite',outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:false}};
   doc.scenes[id].overrides.input={...doc.global.input,persona:'steamdeck',outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:false}};
   disk.writeRaw({...initial,extensions:{bootModes:doc,untouched:{keep:true}}});
   Object.assign(disk.a.ports.settings,await disk.a.repository.loadSettings());
   const loaded=await disk.a.boot.loadBootModes({overviewOnly:true});
   const draft=selection?await disk.a.boot.chooseBootMode(id,loaded.document,false):loaded.document;
   draft.global.background.ui.backgroundOpacity=.47;
   await disk.a.boot.saveBootModes(draft);
   assert.equal(disk.read().extensions.bootModesDeferredBoot,boot,'Explicit same-boot deferral must persist');
   assert.equal(disk.read().input.outputTarget.persona,'disabled');
   assert.equal((await disk.startup(boot,'same-windows-boot')).ok,true);
   const saved=disk.read();assert.equal(saved.input.outputTarget.persona,'disabled','Native same-boot input was replaced');
   const same=await disk.moduleAt('same');
   await same.boot.restoreSelectedBootMode();
   assert.deepEqual(disk.read(),saved,'Same-boot renderer changed persisted config');
   assert.equal((await disk.startup(nextBoot,'next-windows-boot')).ok,true);
   const initialized=disk.read();
   assert.equal(initialized.input.outputTarget.persona,'elite','Next Windows boot must take global input, not the disabled scene');
   assert.equal(initialized.extensions.bootModes.boot,id);assert.equal(initialized.extensions.bootModes.bootEnabled,false);
   assert.deepEqual(initialized.extensions.untouched,{keep:true});
   const next=await disk.moduleAt('next');next.ports.hooks.bootIdentity=()=>nextBoot;
   Object.assign(next.ports.settings,await next.repository.loadSettings());
   await next.boot.restoreSelectedBootMode();
   const actual=disk.read();
   assert.equal(actual.extensions.bootModes.current,'global');
   assert.equal(actual.extensions.bootModes.activeSnapshot.background.ui.backgroundOpacity,.47);
   assert.equal(actual.extensions.bootModesStartupResult.boot,nextBoot);
   assert.deepEqual(actual.extensions.bootModes.scenes,initialized.extensions.bootModes.scenes);
   rows.push({id,selection,sameBootUnchanged:true,nextBootGlobalInput:true,nextBootGlobalPreset:true});
   disk.flush();
  }
  return rows;
 }
 for(const scenario of ['old-ghost','disabled-choice','saved-after-choice','legacy-choice-after-save','draft-save']){
  const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
  doc.boot='steam';doc.bootEnabled=false;doc.current='xbox';
  a.ports.settings.extensions.bootModes=structuredClone(doc);
  if(scenario==='old-ghost')a.ports.settings.extensions.bootModesAppliedBoot=boot;
  const chosen=scenario.includes('choice')?await a.boot.chooseBootMode('steam',doc,false):doc;
  if(['saved-after-choice','legacy-choice-after-save','draft-save'].includes(scenario)){
   const draft=structuredClone(chosen);draft.global.background.ui.backgroundOpacity=.47;
   await a.boot.saveBootModes(draft);
  }
  if(scenario==='legacy-choice-after-save')delete a.ports.settings.extensions.bootModesDeferredBoot;
  const saved=structuredClone(a.ports.settings),same=await runtimeBundle(out);
  Object.assign(same.ports.settings,structuredClone(saved));
  await same.boot.restoreSelectedBootMode();
  const result=same.ports.settings.extensions,applied=result.bootModesStartupResult?.boot===boot;
  rows.push({scenario,applied,current:result.bootModes.current,unchanged:JSON.stringify(same.ports.settings)===JSON.stringify(saved)});
  if(scenario==='old-ghost'){
   assert.equal(result.bootModesStartupResult.mode,'global');assert.equal(result.bootModesGlobalClaimBoot,boot);
  }else{
   const later=await runtimeBundle(out);Object.assign(later.ports.settings,structuredClone(saved));
   later.ports.hooks.bootIdentity=()=>nextBoot;later.ports.settings.input.startupObservedBoot=nextBoot;
   await later.boot.restoreSelectedBootMode();
   assert.equal(later.ports.settings.extensions.bootModes.current,'global','Next Windows boot must still consume global when the scene is disabled');
   assert.equal(later.ports.settings.extensions.bootModes.boot,'steam');
   assert.equal(later.ports.settings.extensions.bootModesStartupResult.boot,nextBoot);
   assert.deepEqual(later.ports.settings.extensions.bootModes.activeSnapshot.background,saved.extensions.bootModes.global.background);
  }
  const count=same.ports.calls.length;await same.boot.restoreSelectedBootMode();assert.equal(same.ports.calls.length,count);
 }
 assert.deepEqual(rows.map(({scenario,applied,current,unchanged})=>[scenario,applied,current,unchanged]),[
  ['old-ghost',true,'global',false],['disabled-choice',false,'xbox',true],['saved-after-choice',false,'xbox',true],
  ['legacy-choice-after-save',false,'xbox',true],['draft-save',false,'xbox',true]
 ],'Intentional same-boot deferral must not be mistaken for a stale legacy claim');
 return rows;
});
await test('startup fallback restores every selected mode and inherited global leaves while repairing a drifted Steam route',async()=>{
 if(process.argv.includes('--startup-claim-acknowledgement')||process.argv.includes('--startup-claim-ack-late')){
  assert(!(process.argv.includes('--startup-claim-acknowledgement')&&process.argv.includes('--startup-claim-ack-late')),'Choose one acknowledgement scope');
  const {runStartupClaimAcknowledgement}=await import('./boot_modes_fixture/preparation.mjs');
  return await runStartupClaimAcknowledgement(out,{native:globalThis.__bootModeSaveNative,seed:await b.captureBootModeSnapshot(),model:m,waitSignal,late:process.argv.includes('--startup-claim-ack-late')});
 }
 if(process.argv.includes('--startup-claim-lock-race')){
  const {runStartupClaimLockRace}=await import('./boot_modes_fixture/preparation.mjs');
  return await runStartupClaimLockRace(out,{native:globalThis.__bootModeSaveNative,seed:await b.captureBootModeSnapshot(),model:m,waitSignal});
 }
 const rows=[];
 for(const mode of m.BOOT_MODE_IDS){
  const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
  doc.boot=mode;doc.bootEnabled=true;doc.global.input={...doc.global.input,persona:'elite',mouse:'off',outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:false}};
  doc.global.gyro={motion:{...doc.global.gyro.motion,enabled:false}};doc.global.background.ui.backgroundOpacity=.61;
  doc.scenes[mode]&&(doc.scenes[mode].overrides.background={ui:{backgroundBlur:7}});
  a.ports.settings.extensions.bootModes=structuredClone(doc);
  const expectedSteam=mode==='steam'||mode==='xbox-steam';a.ports.state.steam=!expectedSteam;
  a.ports.state.earlyCommand=expectedSteam?null:'"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
  let enter,release;const entered=new Promise(resolve=>enter=resolve),held=new Promise(resolve=>release=resolve);
  a.ports.hooks.mouseRead=()=>{enter();return held;};
  const restoring=a.boot.restoreSelectedBootMode();
  try {
   await waitSignal(entered,'startup claim before capture');
   const claimed=structuredClone(a.ports.settings),receipt=claimed.extensions.bootModesStartupResult;
   assert.equal(claimed.extensions.bootModesAppliedBoot,'fixture-windows-boot-1');
   assert(receipt,'A durable startup claim must record an unconfirmed receipt before applying configuration');
   assert.equal(receipt.mode,mode);assert.equal(receipt.boot,claimed.extensions.bootModesAppliedBoot);
   assert.equal(receipt.sourceRevision,doc.revision);assert.equal(receipt.revision,doc.revision);
   assert.equal(receipt.status,'partial');assert.equal(receipt.pending.length,1);assert.deepEqual(receipt.applied,[]);
   assert.deepEqual(receipt.skipped,[]);assert.equal(receipt.modules,undefined);assert.equal(claimed.extensions.bootModesActiveBoot,undefined);
   if(mode==='global'){
    const restart=await runtimeBundle(out);Object.assign(restart.ports.settings,claimed);
    await restart.boot.restoreSelectedBootMode();
    assert.deepEqual(restart.ports.settings.extensions.bootModesStartupResult,receipt);
    assert(!restart.ports.calls.some(call=>call.op==='input.cas'||call.op==='performance.restore'||call.op.startsWith('startup.')),'Same-boot reopen must not replay an unconfirmed claim');
   }
  } finally {delete a.ports.hooks.mouseRead;release();await restoring;}
  assert.equal(a.ports.settings.extensions.bootModes.current,mode);assert.equal(a.ports.settings.input.outputTarget.persona,'elite');
  assert.equal(a.ports.settings.extensions.bootModes.activeSnapshot.background.ui.backgroundOpacity,.61);
  if(mode!=='global')assert.equal(a.ports.settings.extensions.bootModes.activeSnapshot.background.ui.backgroundBlur,7);
  assert.equal(a.ports.state.steam,expectedSteam);assert.equal(a.ports.settings.extensions.bootModesStartupResult.mode,mode);
  assert.equal(a.ports.settings.extensions.bootModesStartupResult.revision,doc.revision+1);
  assert(!a.ports.settings.extensions.bootModesStartupResult.pending.some(value=>value.includes('已领取')),'A settled receipt must replace the claim placeholder');
  const count=a.ports.calls.length;await a.boot.restoreSelectedBootMode();assert.equal(a.ports.calls.length,count);
  rows.push({mode,inheritedPersona:'elite',inheritedOpacity:.61,sceneOverridePreserved:mode!=='global',advancedSteam:expectedSteam,appliedOnce:true});
 }
 return rows;
});
await test('startup fallback disabled scenes apply global without consuming the remembered scene overrides',async()=>{
 for(const mode of m.BOOT_MODE_IDS){
  const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot=mode;doc.bootEnabled=false;
  doc.global.input={...doc.global.input,persona:'elite',mouse:'off',outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:false}};
  if(mode!=='global')doc.scenes[mode].overrides.input={persona:'steamdeck',mouse:'steamdeck'};
  a.ports.settings.extensions.bootModes=structuredClone(doc);a.ports.state.steam=true;a.ports.state.earlyCommand='"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
  await a.boot.restoreSelectedBootMode();const saved=a.ports.settings.extensions.bootModes;
  assert.equal(saved.current,'global');assert.equal(saved.boot,mode);assert.equal(saved.bootEnabled,false);
  assert.equal(a.ports.settings.input.outputTarget.persona,'elite');assert.equal(a.ports.state.steam,false);
  if(mode!=='global')assert.deepEqual(saved.scenes[mode].overrides,doc.scenes[mode].overrides);
 }
});
await test('startup fallback cleanup failure remains pending while global application proceeds',async()=>{
 const a=await runtimeBundle(out);a.ports.reset();a.ports.settings.extensions={};a.ports.state.steam=true;
 a.ports.state.earlyCommand='"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
 a.ports.hooks.steamStartupWrite=()=>{throw Error('fixture-denied-route-cleanup');};
 await a.boot.restoreSelectedBootMode();const result=a.ports.settings.extensions.bootModesStartupResult;
 assert.equal(a.ports.settings.extensions.bootModes.current,'global');assert.equal(result.mode,'global');assert.equal(result.status,'partial');
 assert(result.pending.some(value=>value.includes('Steam 开机路径')&&value.includes('fixture-denied-route-cleanup')));
 assert.equal(a.ports.state.steam,true);
});
await test('startup fallback same-boot application restart does not replay input or route writes',async()=>{
 const first=await runtimeBundle(out);first.ports.settings.extensions={};await first.boot.restoreSelectedBootMode();
 const durable=structuredClone(first.ports.settings),next=await runtimeBundle(out);Object.assign(next.ports.settings,durable);
 await next.boot.restoreSelectedBootMode();assert(!next.ports.calls.some(row=>row.op==='input.cas'||row.op.startsWith('startup.')||row.op==='monitor.apply'));
 assert.deepEqual(next.ports.settings.extensions.bootModes,durable.extensions.bootModes);
 const boot='fixture-windows-boot-1';
 for(const boundary of ['incomplete','active','unknown-boot','future-result','explicit-selection']){
  const a=await runtimeBundle(out);Object.assign(a.ports.settings,structuredClone(durable));
  const {extensions,input}=a.ports.settings;
  if(boundary!=='active')delete extensions.bootModesActiveBoot;
  delete extensions.bootModesStartupResult;
  if(boundary==='unknown-boot')input.startupObservedBoot=null;
  if(boundary==='future-result')extensions.bootModesStartupResult={schema:2,boot};
  if(boundary==='explicit-selection')await a.boot.chooseBootMode('global',extensions.bootModes,true);
  const before=structuredClone(a.ports.settings),count=a.ports.calls.length;
  await a.boot.restoreSelectedBootMode();
  const receipt=a.ports.settings.extensions.bootModesStartupResult;
  if(boundary==='incomplete'){
   assert.equal(receipt.boot,boot);assert.equal(receipt.mode,'global');assert.equal(receipt.revision,before.extensions.bootModes.revision);
   assert.equal(receipt.status,'partial');assert.equal(receipt.pending.length,1);assert.deepEqual(receipt.applied,[]);
   before.extensions.bootModesStartupResult=receipt;
  }
  assert.deepEqual(a.ports.settings,before);
  assert(!a.ports.calls.slice(count).some(row=>row.op==='input.cas'||row.op.startsWith('startup.')||row.op==='monitor.apply'));
 }
 return {settledClaimNotReplayed:true,incompleteClaimRecordedWithoutReplay:true,preservedBoundaries:4};
});
await test('startup fallback activates a saved manual global bank once and preserves unconfigured or disabled scheduling',async()=>{
 for(const [configured,enabled] of [[true,true],[false,false],[true,false]]){
  const a=await runtimeBundle(out);a.ports.reset();a.ports.settings.extensions={};
  a.ports.settings.performanceSchedule.configured=configured;a.ports.settings.performanceSchedule.enabled=enabled;
  a.ports.settings.performanceSchedule.controls.ac.mode='manual';
  await a.boot.restoreSelectedBootMode();
  const writes=a.ports.calls.filter(row=>row.op==='performance.save');assert.equal(writes.length,1);
  assert.equal(writes[0].value.configured,configured);assert.equal(writes[0].value.enabled,enabled);
  assert.equal(writes[0].value.controls.ac.mode,'manual');
  if(!configured||!enabled)assert.equal(a.ports.settings.extensions.bootModesStartupResult.modules.performance.state,'disabled');
 }
});
await test('disabled restoration applies the inherited global rather than the disabled scene',async()=>{
 const doc=await fresh();doc.boot='steam';doc.bootEnabled=false;stored(doc);await b.restoreSelectedBootMode();assert.equal(p.settings.extensions.bootModesAppliedBoot,'fixture-windows-boot-1');assert.equal(p.settings.extensions.bootModes.current,'global');assert.equal(p.settings.extensions.bootModesStartupResult.mode,'global');assert.equal(p.settings.extensions.bootModes.boot,'steam');assert(!p.calls.some(c=>c.op==='platform.set'));
});
await test('startup application updates the applied scope while preserving next-boot selection; stops old mouse before CAS, backend and start',async()=>{
 const doc=await fresh(),games=gameState();doc.boot='xbox';doc.bootEnabled=false;doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'microsoft',outputTarget:{persona:'dualsense-edge'}};stored(doc);
 const {document:next}=await startMode('steam',doc);assert.equal(next.current,'steam');assert.equal(next.boot,'xbox');assert.equal(next.bootEnabled,false);assert.equal(next.revision,1);assert.equal(gameState(),games);assert(!opNames().some(n=>n.startsWith('startup.')));
 const significant=p.calls.filter(c=>['mouse.enable','mouse.backend','input.cas'].includes(c.op));assert.deepEqual(significant.map(c=>[c.op,c.op==='input.cas'?c.value.expected:c.value]),[['mouse.enable',false],['input.cas',7],['mouse.backend','gamebar'],['mouse.enable',true]]);
 assert.equal(p.settings.input.outputTarget.persona,'dualsense-edge');assert.equal(p.state.mouse.on,true);
});
await test('mouse OFF stops the previous owner and never starts or selects any backend',async()=>{
 const doc=await fresh();doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),mouse:'off'};stored(doc);await startMode('steam',doc);assert.equal(p.state.mouse.on,false);assert.deepEqual(p.calls.filter(c=>c.op.startsWith('mouse.')).map(c=>[c.op,c.value]),[['mouse.enable',false]]);
});
await test('global application completely removes scene residue in input, gyro, and action rules',async()=>{
 const doc=await fresh(),games=gameState();doc.current='steam';doc.global.input.outputTarget={persona:'elite',buttonMappingEnabled:true,gyroEnabled:false};doc.global.input.mouse='off';doc.global.gyro.motion={enabled:false};stored(doc);
 p.settings.input.outputTarget={persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:true,sceneOnly:'must disappear'};p.settings.input.gyroMotion={enabled:true,sceneOnly:true};p.settings.input.buttonMapping.rules={shortcutRules:[],shortcutRulesInitialized:true};p.settings.input.bootModeShortcuts={schema:1,mode:'steam',boot:'',rules:[{id:'scene-only'}]};
 await startMode('global',doc);assert(!('sceneOnly' in p.settings.input.outputTarget));assert(!('sceneOnly' in p.settings.input.gyroMotion));assert.deepEqual(p.settings.input.buttonMapping.rules.shortcutRules,[]);assert.equal(p.settings.input.bootModeShortcuts,null);assert.equal(gameState(),games);
});
await test('enabling a mode from disabled native gyro defaults repairs both CAS payload and committed global settings without editing games',async()=>{
 const doc=await fresh(),games=gameState();assert.equal(p.settings.input.gyroMotion.enabled,false);assert.equal(p.settings.input.gyroMotion.outputMode,'disabled');assert.equal(p.settings.input.gyroMotion.unit,null);
 doc.scenes.steam.overrides.gyro={motion:{...structuredClone(doc.global.gyro.motion),enabled:true}};stored(doc);
 const {document:next}=await startMode('steam',doc);const cas=p.calls.filter(c=>c.op==='input.cas');assert.equal(cas.length,1);assert.equal(cas[0].value.expected,7);
 for(const motion of [cas[0].value.patch.gyroMotion,p.settings.input.gyroMotion]){assert.equal(motion.enabled,true);assert.equal(motion.outputMode,'virtual-stick');assert.equal(motion.unit,'deg/s');assert.equal(motion.motionSensitivityX,1.5);assert.equal(motion.provider,null);}
 assert.equal(cas[0].value.patch.outputTarget.gyroEnabled,true);assert.equal(p.settings.input.outputTarget.gyroEnabled,true);assert.equal(next.current,'steam');assert.equal(next.boot,'global');assert.equal(next.bootEnabled,false);assert.equal(gameState(),games);
 return {casExpected:cas[0].value.expected,casGyro:cas[0].value.patch.gyroMotion,committedGyro:p.settings.input.gyroMotion,gameProfilesUnchanged:true};
});
for(const autoOn of [false,true])await test('failed partial target mouse start restores AUTO-'+(autoOn?'ON':'OFF')+' policy, original backend and exact input before restarting the old owner',async()=>{
 p.state.mouse.mode='auto';p.state.mouse.on=autoOn;p.state.mouseAutoOn=autoOn;p.state.mouse.backend='gamebar';p.state.mouse.provider='ymcc';
 p.settings.input.outputTarget={...p.settings.input.outputTarget,buttonMappingEnabled:false,gyroEnabled:true,rollbackSentinel:'exact-output'};p.settings.input.ownership={...p.settings.input.ownership,rollbackSentinel:'exact-ownership'};p.settings.input.buttonMapping.rollbackSentinel={preserve:true};p.settings.input.gyroMotion.rollbackSentinel='exact-gyro';
 const doc=await fresh(),old=structuredClone(p.settings.input),games=gameState();doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'joyxoff'};doc.scenes.steam.overrides.gyro={motion:{...structuredClone(doc.global.gyro.motion),enabled:true}};stored(doc);const oldDoc=structuredClone(p.settings.extensions.bootModes);p.faults['mouse.start.partial']=1;let rollback;
 await assert.rejects(startMode('steam',doc),error=>{assert.equal(error.failedStep,'mouse-start');assert.deepEqual(error.rollback.map(r=>r.step),['mouse-start','global-input','mouse-stop']);assert(error.rollback.every(r=>r.ok));rollback=error.rollback;return true;});
 assert.deepEqual(p.settings.input,{...old,revision:old.revision+2});assert.deepEqual(p.settings.extensions.bootModes,oldDoc);assert.equal(gameState(),games);assert.equal(p.state.mouse.mode,'auto');assert.equal(p.state.mouse.on,autoOn);assert.equal(p.state.mouse.backend,'gamebar');assert.equal(p.state.mouse.provider,'ymcc');
 const handoff=p.calls.filter(c=>['mouse.enable','input.cas','mouse.backend','mouse.policy'].includes(c.op));assert.deepEqual(handoff.map(c=>c.op),['mouse.enable','input.cas','mouse.backend','mouse.enable','mouse.enable','input.cas','mouse.backend','mouse.policy']);assert.deepEqual(handoff.filter(c=>c.op==='mouse.enable').map(c=>c.value),[false,true,false]);assert.equal(handoff.at(-1).value,'auto');
 const restored=handoff.filter(c=>c.op==='input.cas').at(-1).value.patch;assert.deepEqual(restored,{outputTarget:old.outputTarget,ownership:old.ownership,gyroMotion:old.gyroMotion,bootModeShortcuts:old.bootModeShortcuts});assert(!opNames().some(n=>n.startsWith('startup.')||n==='app.launch'||n==='monitor.apply'));
 return {autoOn,rollback,handoff,exactInputRestored:true,gameProfilesUnchanged:true};
});
await test('input CAS conflict aborts, restores previous mouse, and never commits current scene',async()=>{
 const doc=await fresh();doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'disabled',mouse:'off'};stored(doc);p.faults['input.stale']=1;await assert.rejects(startMode('steam',doc));assert.equal(p.settings.extensions.bootModes.current,'global');assert.equal(p.settings.extensions.bootModes.revision,0);assert.equal(p.state.mouse.on,true);assert.equal(p.settings.input.outputTarget.persona,'elite');
});
await test('optional native failure is pending, never applied, and permits committing the mode',async()=>{
 if(process.argv.includes('--optional-action-owner')){
  const {runOptionalOwner}=await import('./boot_modes_fixture/optionalOwner.mjs');return await runOptionalOwner(out,process.argv.slice(2));
 }
 const doc=await fresh(),games=gameState();doc.scenes.steam.overrides.monitor={preferences:{...doc.global.monitor.preferences,ymccEnabled:true}};stored(doc);p.faults['monitor.apply']=1;
 const {document:next,receipt}=await startMode('steam',doc);assert.equal(next.current,'steam');assert(receipt.pending.some(v=>v.includes('injected:monitor.apply')));assert(!receipt.applied.includes('监控'));assert.equal(gameState(),games);
});
await test('unsupported display is skipped and cannot also be claimed applied',async()=>{
 const doc=await fresh();doc.scenes.steam.overrides.display={brightness:null,resolution:'9999x9999',hz:999};stored(doc);const {receipt}=await startMode('steam',doc);assert(receipt.skipped.some(s=>s.includes('显示')));assert(!receipt.applied.includes('画面'),'Unsupported display was incorrectly reported applied');assert(!opNames().includes('display.set'));
});
await test('pending monitor acknowledgement is not an applied receipt',async()=>{
 if(process.argv.includes('--monitor-boot-owner')){const {runMonitorOwner}=await import('./boot_modes_fixture/monitorOwner.mjs');return await runMonitorOwner(out,process.argv.slice(2));}
 const doc=await fresh();doc.scenes.steam.overrides.monitor={preferences:{...doc.global.monitor.preferences,ymccEnabled:true}};stored(doc);p.faults['monitor.pending']=1;const {receipt}=await startMode('steam',doc);assert(receipt.pending.some(s=>s.includes('not confirmed')));assert(!receipt.applied.includes('监控'),'Unconfirmed monitor was incorrectly reported applied');assert.equal(p.state.monitorApplyOptions?.origin,'boot-mode');assert.equal(p.state.monitorApplyOptions?.bootContext?.input?.revision,p.settings.input.revision);assert.equal(p.state.monitorApplyOptions?.bootContext?.extensions?.bootModes?.current,'steam');
});
await test('post-commit launches honor the shared library; failed and deleted applications are truthful',async()=>{
 const doc=await fresh();doc.scenes.steam.overrides.softwareLaunch={paths:['C:/fixture/app.exe','C:/deleted/not-owned.exe'],timing:'scene',delay:0};stored(doc);p.faults['app.launch']=1;const {document:next,receipt}=await startMode('steam',doc);assert.equal(next.current,'steam');assert.equal(p.calls.filter(c=>c.op==='app.launch').length,1);assert(receipt.pending.some(s=>s.includes('启动')&&s.includes('injected')));assert(receipt.skipped.some(s=>s.includes('删除')));
});
await test('platform startup write failure persists selected boot mode with notice and never changes current',async()=>{
 const doc=await fresh();doc.current='xbox';stored(doc);p.faults['startup.steam']=1;const next=await b.chooseBootMode('steam',doc,true);assert.equal(next.boot,'steam');assert.equal(next.current,'xbox');assert.equal(next.bootEnabled,true);assert.match(next.bootNotice,/Steam/);assert.equal(p.settings.extensions.bootModes.bootNotice,next.bootNotice);assert(changes().every(startupOnly));
});
await test('manual scenes do not launch applications and FxSound/LS selections never write OS startup or game profiles',async()=>{
 if(process.argv.includes('--startup-software-cancel')){
  const a=await runtimeBundle(out),q=a.ports,rows=[];
  const only=process.argv.find(v=>v.startsWith('--startup-software-cancel-mode='))?.split('=')[1];assert(!only||a.model.BOOT_MODE_IDS.includes(only));
  for(const mode of a.model.BOOT_MODE_IDS){
   if(only&&only!==mode)continue;
   a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();q.reset();
   const doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
   for(const scene of Object.values(doc.scenes))for(const key of ['input','gyro','audioOptimization','frameGeneration','softwareLaunch'])delete scene.overrides[key];
   doc.global.audioOptimization={enabled:false};doc.global.frameGeneration={enabled:false};
   doc.global.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'scene',delay:1};q.settings.extensions.bootModes=structuredClone(doc);
   let enter,release;const entered=new Promise(resolve=>{enter=resolve;}),hold=new Promise(resolve=>{release=resolve;});
   await a.boot.applyBootMode(mode,doc,{startup:true});
   q.hooks.processRead=()=>{let reads=0;q.hooks.settingsRead=async()=>{if(++reads===2){enter();await hold;}};};
   await waitSignal(entered,'final linked-app owner observation');a.boot.cancelBootModeLaunches();release();
   await new Promise(resolve=>setImmediate(resolve));
   assert(!q.calls.some(row=>row.op==='app.launch'),'canceled launch crossed asynchronous final owner observation');
   rows.push({mode,canceledDuringFinalOwnerRead:true,lateLaunches:0});
  }
  a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();return rows;
 }
 if(process.argv.includes('--startup-software-bridge')){
  const {runSoftwareBridge}=await import('./boot_modes_fixture/softwareOwner.mjs');return await runSoftwareBridge(out);
 }
 if(process.argv.includes('--startup-software-native')){
  const {runSoftwareNative}=await import('./boot_modes_fixture/softwareOwner.mjs');return await runSoftwareNative(out,process.argv.slice(2));
 }
 if(process.argv.includes('--startup-software-owner-boundary')){
  const stages=['frame-file','frame-process','frame-native','audio-native','audio-complete','software-file','software-process','software-native'];
  const kinds=['mode','revision','hash','result','observed','generation','ack','unrelated','current'];
  const stageOnly=process.argv.find(v=>v.startsWith('--startup-software-stage='))?.split('=')[1];
  const kindOnly=process.argv.find(v=>v.startsWith('--startup-software-kind='))?.split('=')[1];
  const from=process.argv.find(v=>v.startsWith('--startup-software-from='))?.slice('--startup-software-from='.length);let reached=!from;
  assert(!stageOnly||stages.includes(stageOnly));assert(!kindOnly||kinds.includes(kindOnly));
  const a=await runtimeBundle(out),q=a.ports,rows=[],raw=path.join(out,'startup-software-owner.jsonl');fs.closeSync(fs.openSync(raw,'wx'));
  for(const mode of a.model.BOOT_MODE_IDS)for(const stage of stageOnly?[stageOnly]:stages)for(const kind of kindOnly?[kindOnly]:kinds){
   if(!reached){if([mode,stage,kind].join('/')===from)reached=true;else continue;}
   // Cache invalidation alone changes no native durable owner. Once submitted,
   // that artificial renderer-only event cannot recall the admitted operation.
   if(kind==='generation'&&stage.endsWith('native'))continue;
   a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();q.reset();
   const doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
   for(const scene of Object.values(doc.scenes))for(const module of ['input','gyro','audioOptimization','frameGeneration','softwareLaunch'])delete scene.overrides[module];
   doc.global.audioOptimization={enabled:stage.startsWith('audio')};
   doc.global.frameGeneration={enabled:stage.startsWith('frame')||stage==='audio-complete'};
   doc.global.softwareLaunch={paths:stage.startsWith('software')?['C:/fixture/app.exe']:[],timing:'scene',delay:0};
   q.settings.extensions.bootModes=structuredClone(doc);
   let changed=false,atEffect=0;
   const mutate=()=>{if(changed)return;changed=true;atEffect=(q.state.softwareEffects??[]).length;
    if(kind==='mode'){q.settings.extensions.bootModes.current=mode==='global'?'xbox':'global';q.settings.extensions.bootModes.revision++;}
    if(kind==='revision')q.settings.input.revision++;
    if(kind==='hash')q.settings.input.gyroMotion.motionSensitivityX+=0.5;
    if(kind==='result')q.settings.extensions.bootModesStartupResult={schema:1,mode:'xbox',boot:'new-owner',revision:999};
    if(kind==='observed')q.settings.input.startupObservedBoot='new-observed-boot';
    if(kind==='generation')q.invalidate();
    if(kind==='ack')q.settings.input.appliedRevision=100;
    if(kind==='unrelated')q.settings.extensions.unrelated.keep=false;
   };
   const pathMatches=path=>String(path).endsWith(stage.startsWith('frame')?'LosslessScaling.exe':'app.exe');
   if(stage.endsWith('file'))q.hooks.fileRead=path=>{if(pathMatches(path))mutate();};
   if(stage.endsWith('process'))q.hooks.processRead=path=>{if(pathMatches(path))mutate();};
   if(stage.endsWith('native')&&stage!=='audio-native')q.hooks.shellExecute=path=>{if(pathMatches(path))mutate();};
   if(stage==='audio-native')q.hooks.fxSet=mutate;
   if(stage==='audio-complete')q.hooks.fxCompleted=mutate;
   let error;try{await a.boot.applyBootMode(mode,doc,{startup:true});}catch(e){error=String(e);}
   const effects=q.state.softwareEffects??[],allowed=['ack','unrelated','current'].includes(kind);
   const late=effects.slice(atEffect),row={mode,stage,kind,changed,error,allowed,lateEffects:late,effects};
   fs.appendFileSync(raw,JSON.stringify(row)+'\n');assert(changed,stage+' boundary was not reached');
   if(allowed){assert(!error,JSON.stringify(row));assert(late.length>0,JSON.stringify(row));}
   else {assert.equal(late.length,0,'expired startup action reached effect: '+JSON.stringify(row));
    if(stage.endsWith('file')||stage.endsWith('process'))assert(!q.calls.some(c=>c.op==='app.launch'&&pathMatches(c.value.path)),JSON.stringify(row));}
   rows.push({mode,stage,kind,staleActionStopped:!allowed,allowedCurrentControl:allowed});
  }
  assert(reached,'Unknown software ownership resume boundary');a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();return rows;
 }
 const doc=await fresh(),games=gameState();doc.scenes.steam.overrides.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'manual',delay:0};doc.scenes.steam.overrides.audioOptimization={enabled:true};doc.scenes.steam.overrides.frameGeneration={enabled:true};stored(doc);await startMode('steam',doc);assert(!p.calls.some(c=>c.op==='app.launch'&&c.value.path==='C:/fixture/app.exe'));assert(!opNames().includes('fx.set'));assert.equal(p.calls.filter(c=>c.op==='ipc.fxsound.setEnabled'&&c.value.enabled===true).length,1);assert(!opNames().includes('addon.set'));assert(!opNames().includes('lossless.set'));assert.equal(gameState(),games);
});
await test('failure after input write restores mouse, input intent, and previous committed mode',async()=>{
 if(process.argv.includes('--rollback-generation-boundary')){
  const observations=path.join(out,'rollback-generation-observations.jsonl');fs.closeSync(fs.openSync(observations,'wx'));const rows=[];
  const selectedPolicy=process.argv.find(arg=>arg.startsWith('--rollback-mouse-policy='))?.split('=')[1];assert(!selectedPolicy||['on','auto'].includes(selectedPolicy));
  const stage=process.argv.find(arg=>arg.startsWith('--rollback-stage='))?.split('=')[1]??'restore-backend';assert(['restore-backend','forward-stop','rollback-stop'].includes(stage));
  for(const mode of m.BOOT_MODE_IDS)for(const policy of selectedPolicy?[selectedPolicy]:['on','auto']){
   const a=await runtimeBundle(out),q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
   q.state.mouse.mode=policy;q.state.mouse.on=true;q.state.mouseAutoOn=true;q.settings.gamepad.mouseMode=policy;
   const wanted={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'microsoft',outputTarget:{persona:'dualsense-edge'}};
   if(mode==='global')doc.global.input=wanted;else doc.scenes[mode].overrides.input=wanted;
   q.settings.extensions.bootModes=structuredClone(doc);q.faults['mouse.backend']=1;let cancelledAt,oldGeneration,newGeneration,error;
   const cancel=()=>{oldGeneration=q.getSettingsGeneration();q.invalidate();newGeneration=q.getSettingsGeneration();cancelledAt=q.calls.length;};
   let reads=0;if(stage==='restore-backend')q.hooks.mouseBackend=cancel;else q.hooks.mouseRead=()=>{if(++reads===(stage==='forward-stop'?2:4)){q.state.mouse.on=true;q.state.mouse.mode=policy;cancel();}};
   try{await a.boot.applyBootMode(mode,doc,{startup:true});}catch(value){error=value;}finally{delete q.hooks.mouseBackend;delete q.hooks.mouseRead;}
   const actual={mode,policy,stage,failedStep:error?.failedStep,rollback:error?.rollback,oldGeneration,newGeneration,mouse:structuredClone(q.state.mouse),afterCancellation:structuredClone(q.calls.slice(cancelledAt)),calls:structuredClone(q.calls)};
   fs.appendFileSync(observations,JSON.stringify(actual)+'\n');
   assert(error&&error.failedStep===(stage==='forward-stop'?'mouse-stop':'mouse-start'),'original apply must fail at its actual writer');assert(Number.isInteger(cancelledAt));assert.equal(newGeneration,oldGeneration+1);
   assert.equal(q.state.mouse.on,stage!=='restore-backend',stage==='restore-backend'?'cancelled rollback must not revive the old mouse mode':'cancelled mouse step must not change the new owner');
   assert(!actual.afterCancellation.some(row=>['mouse.enable','mouse.policy','mouse.backend','input.cas'].includes(row.op)),'cancelled rollback dispatched another owned write');
   assert.equal(error.rollback.find(row=>row.step==='mouse-stop').ok,false,'cancelled restoration must not be marked confirmed');
   rows.push({mode,policy,stage,rollbackCancelled:true,oldMouseRevived:false,lateWrites:0,actualOriginalTransaction:true});
   a.boot.cancelBootModeDeferredMouse();a.boot.cancelBootModeLaunches();
  }
  return rows;
 }
 const doc=await fresh(),old=structuredClone(p.settings.input);doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'microsoft',outputTarget:{persona:'dualsense-edge'}};doc.scenes.steam.overrides.monitor={preferences:{...doc.global.monitor.preferences,ymccEnabled:true}};stored(doc);p.faults['mouse.backend']=1;
 await assert.rejects(startMode('steam',doc),error=>{assert.equal(error.failedStep,'mouse-start');assert(error.rollback.every(r=>r.ok));return true;});assert.deepEqual(p.settings.input,{...old,revision:old.revision+2});assert.equal(p.state.mouse.backend,'joyxoff');assert.equal(p.state.mouse.on,true);assert.equal(p.settings.extensions.bootModes.current,'global');assert(!opNames().includes('app.launch'));assert(!opNames().includes('monitor.apply'),'Optional hardware must wait until mandatory input and mode commit');
});
await test('actual apply observes generation fences and reports suppressed rollback without reviving mouse',async()=>{
 if(process.argv.includes('--mode-save-native-files')){assert(!process.argv.includes('--mode-save-owner-boundary')&&!process.argv.includes('--mouse-owner-boundary')&&!process.argv.includes('--input-boot-owner-boundary'),'Choose one actual mode-save scope');const {runModeSaveNativeOwner}=await import('./boot_modes_fixture/modeSaveNative.mjs');return await runModeSaveNativeOwner(out,process.argv.slice(2),{native:globalThis.__bootModeSaveNative});}
 if(process.argv.includes('--mode-save-owner-boundary')){
  assert(!process.argv.includes('--mouse-owner-boundary')&&!process.argv.includes('--input-boot-owner-boundary'));
  const kinds=['revision','hash','mode','result','observed','applied','active','ack','unrelated','order','current'];
  const selectedKind=process.argv.find(value=>value.startsWith('--mode-save-kind='))?.split('=')[1];assert(!selectedKind||kinds.includes(selectedKind));
  const observations=path.join(out,'mode-save-owner-observations.jsonl');fs.closeSync(fs.openSync(observations,'wx'));const rows=[];
  for(const mode of m.BOOT_MODE_IDS)for(const kind of selectedKind?[selectedKind]:kinds){
   const a=await runtimeBundle(out),q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
   const wanted={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'off',outputTarget:{persona:'dualsense-edge'}};
   if(mode==='global')doc.global.input=wanted;else doc.scenes[mode].overrides.input=wanted;
   q.settings.extensions.bootModes=structuredClone(doc);let boundary,at,error,result;
   q.hooks.settingsWrite=draft=>{
    if(boundary||draft.extensions?.bootModes?.revision!==doc.revision+1||draft.extensions?.bootModes?.current!==mode)return;
    if(kind==='revision')q.settings.input.revision++;
    if(kind==='hash')q.settings.input.diagnostics={...(q.settings.input.diagnostics??{}),inputLoggingEnabled:true};
    if(kind==='mode'){q.settings.extensions.bootModes.revision+=5;q.settings.extensions.bootModes.boot=mode==='steam'?'xbox':'steam';}
    if(kind==='result')q.settings.extensions.bootModesStartupResult={schema:1,boot:'new-result',mode,revision:99,sourceRevision:98,status:'partial',applied:[],skipped:[],pending:[]};
    if(kind==='observed')q.settings.input.startupObservedBoot='new-observed-boot';
    if(kind==='applied')q.settings.extensions.bootModesAppliedBoot='new-applied-boot';
    if(kind==='active')q.settings.extensions.bootModesActiveBoot='new-active-boot';
    if(kind==='ack')q.settings.input.hostAcknowledgedRevision=q.settings.input.revision;
    if(kind==='unrelated')q.settings.extensions.peer={keep:true};
    if(kind==='order')q.settings.extensions.bootModes=Object.fromEntries(Object.entries(q.settings.extensions.bootModes).reverse());
    boundary=structuredClone(q.settings);at=q.calls.length;
   };
   try{result=await a.boot.applyBootMode(mode,doc,{startup:true});}catch(value){error=value;}finally{delete q.hooks.settingsWrite;}
   const rejected=!['ack','unrelated','order','current'].includes(kind),actual={mode,kind,rejected,error:error?.message,boundary,after:structuredClone(q.settings),afterBoundaryCalls:structuredClone(q.calls.slice(at))};fs.appendFileSync(observations,JSON.stringify(actual)+'\n');assert(boundary,'actual apply must reach the selected mode-save writer');
   if(rejected){assert(error,'stale mode save must stop');const detail=error.message.match(/boot-owner-diff:(\{.*\})/);assert(detail,'original failure must retain its first owner comparison');const diagnostic=JSON.parse(detail[1]);assert.equal(diagnostic.stage,'save-current-mode:write');assert(diagnostic.fields.includes({revision:'inputRevision',hash:'inputHash',mode:'mode',result:'startupResult',observed:'observedBoot',applied:'appliedBoot',active:'activeBoot'}[kind]));assert.deepEqual(diagnostic.generation,[q.getSettingsGeneration(),q.getSettingsGeneration()]);assert.deepEqual(q.settings.input,boundary.input);assert.deepEqual(q.settings.extensions,boundary.extensions,'stale mode save must not replace newer ownership');assert(!actual.afterBoundaryCalls.some(row=>['input.cas','mouse.enable','mouse.policy','mouse.backend','settings.update'].includes(row.op)),'stale mode save must not replay old rollback');}
   else{assert(!error,error?.stack);assert(result);assert.equal(q.settings.extensions.bootModes.current,mode);assert.equal(q.settings.extensions.bootModes.revision,doc.revision+1);}
   rows.push({mode,kind,rejected,actualOriginalTransaction:true});a.boot.cancelBootModeDeferredMouse();a.boot.cancelBootModeLaunches();
  }return rows;
 }
 if(process.argv.includes('--mouse-owner-boundary')){
  const stages=['stop-read','backend','start'],kinds=['mode','revision','result','observed'];
  const selectedStage=process.argv.find(value=>value.startsWith('--mouse-owner-stage='))?.split('=')[1],selectedKind=process.argv.find(value=>value.startsWith('--mouse-owner-kind='))?.split('=')[1];
  assert(!selectedStage||stages.includes(selectedStage));assert(!selectedKind||selectedKind==='current'||kinds.includes(selectedKind));
  const observations=path.join(out,'mouse-owner-observations.jsonl');fs.closeSync(fs.openSync(observations,'wx'));const rows=[];
  for(const mode of m.BOOT_MODE_IDS)for(const stage of selectedStage?[selectedStage]:stages)for(const kind of selectedKind?[selectedKind]:kinds){
   const a=await runtimeBundle(out),q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
   const wanted={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'microsoft',outputTarget:{persona:'dualsense-edge'}};
   if(mode==='global')doc.global.input=wanted;else doc.scenes[mode].overrides.input=wanted;
   q.settings.extensions.bootModes=structuredClone(doc);const generation=q.getSettingsGeneration(),inputRevision=q.settings.input.revision;let boundary,cancelledAt,error;
   const changeOwner=()=>{if(boundary)return;
    if(kind==='mode'){q.settings.extensions.bootModes.revision++;q.settings.extensions.bootModes.boot=mode==='steam'?'xbox':'steam';}
    if(kind==='revision'){q.settings.input.revision++;q.settings.input.diagnostics={...(q.settings.input.diagnostics??{}),inputLoggingEnabled:true};}
    if(kind==='result')q.settings.extensions.bootModesStartupResult={schema:1,boot:'newer-owner',mode,revision:99,sourceRevision:98,status:'partial',applied:[],skipped:[],pending:[]};
    if(kind==='observed')q.settings.input.startupObservedBoot='newer-observed-boot';
    boundary=structuredClone(q.settings);cancelledAt=q.calls.length;
   };
   let reads=0,writes=0;
   if(stage==='stop-read')q.hooks.mouseRead=()=>{if(++reads===2)changeOwner();};
   if(stage==='backend')q.hooks.mouseBackend=changeOwner;
   if(stage==='start')q.hooks.mouseWrite=()=>{if(++writes===2)changeOwner();};
   try{await a.boot.applyBootMode(mode,doc,{startup:true});}catch(value){error=value;}finally{delete q.hooks.mouseRead;delete q.hooks.mouseBackend;delete q.hooks.mouseWrite;}
   const actual={mode,stage,kind,generation,currentGeneration:q.getSettingsGeneration(),boundary,after:structuredClone(q.settings),error:error?.message,failedStep:error?.failedStep,rollback:error?.rollback,afterCancellation:structuredClone(q.calls.slice(cancelledAt)),calls:structuredClone(q.calls)};
   fs.appendFileSync(observations,JSON.stringify(actual)+'\n');assert(boundary,'original transaction must reach selected mouse boundary');assert.equal(q.getSettingsGeneration(),generation,'mode ownership changed without changing configuration directory generation');
   if(kind==='current'){
    assert(!error,error?.stack);assert.equal(q.settings.extensions.bootModes.current,mode);assert.equal(q.settings.extensions.bootModes.revision,doc.revision+1);
    assert.equal(q.settings.input.revision,inputRevision+1);assert.equal(q.settings.input.outputTarget.persona,'dualsense-edge');assert.equal(q.state.mouse.on,true);assert.equal(q.state.mouse.backend,'gamebar');
   }else{
    assert(error,'obsolete mouse owner must stop the original transaction');
    assert(!actual.afterCancellation.some(row=>['mouse.enable','mouse.policy','mouse.backend','input.cas','settings.update'].includes(row.op)),'obsolete mouse owner dispatched another mouse/input/mode write');
    assert.deepEqual(q.settings.input,boundary.input,'obsolete mouse rollback changed newer input');assert.deepEqual(q.settings.extensions,boundary.extensions,'obsolete mouse rollback changed newer mode metadata');
   }
   rows.push({mode,stage,kind,actualOriginalTransaction:true,...(kind==='current'?{sameOwnerCompleted:true}:{newerOwnerPreserved:true,lateWrites:0})});a.boot.cancelBootModeDeferredMouse();a.boot.cancelBootModeLaunches();
  }return rows;
 }
 if(process.argv.includes('--input-boot-owner-boundary')){
  const observations=path.join(out,'input-boot-owner-observations.jsonl');fs.closeSync(fs.openSync(observations,'wx'));const rows=[];
  const kind=process.argv.find(value=>value.startsWith('--input-owner-kind='))?.split('=')[1]??'mode';assert(['mode','revision'].includes(kind));
  for(const mode of m.BOOT_MODE_IDS){
   const a=await runtimeBundle(out),q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
   const wanted={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'microsoft',outputTarget:{persona:'dualsense-edge'}};
   if(mode==='global')doc.global.input=wanted;else doc.scenes[mode].overrides.input=wanted;
   q.settings.extensions.bootModes=structuredClone(doc);const before=structuredClone(q.settings.input);let boundary,cancelledAt,error;
   q.hooks.inputCas=()=>{if(boundary)return;const newer=structuredClone(q.settings.extensions.bootModes);if(kind==='mode'){newer.revision++;newer.boot=mode==='steam'?'xbox':'steam';}else{q.settings.input.revision++;q.settings.input.diagnostics??={};q.settings.input.diagnostics.inputLoggingEnabled=!q.settings.input.diagnostics.inputLoggingEnabled;Object.assign(before,structuredClone(q.settings.input));}q.settings.extensions.bootModes=newer;boundary=structuredClone(newer);cancelledAt=q.calls.length;};
   try{await a.boot.applyBootMode(mode,doc,{startup:true});}catch(value){error=value;}finally{delete q.hooks.inputCas;}
   const actual={mode,before,after:structuredClone(q.settings.input),boundary,afterCancellation:structuredClone(q.calls.slice(cancelledAt)),failedStep:error?.failedStep,rollback:error?.rollback};fs.appendFileSync(observations,JSON.stringify(actual)+'\n');
   assert(error&&boundary,'original startup must reach its input dispatch and reject obsolete mode ownership');
   assert.deepEqual(q.settings.input,before,'obsolete mode input dispatch must not change the current input bank');
   assert.deepEqual(q.settings.extensions.bootModes,boundary,'obsolete mode rollback must preserve the newer draft');
   assert(!actual.afterCancellation.some(row=>['mouse.enable','mouse.policy','mouse.backend','input.cas'].includes(row.op)),'obsolete mode input dispatch derived another old-owner write');
   rows.push({mode,kind,actualOriginalTransaction:true,olderInputRejected:true,newerDraftPreserved:true,lateWrites:0});a.boot.cancelBootModeDeferredMouse();a.boot.cancelBootModeLaunches();
  }return rows;
 }
 const doc=await fresh();doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'disabled',mouse:'off'};stored(doc);p.faults['input.invalidate']=1;await assert.rejects(startMode('steam',doc),error=>{assert.equal(error.failedStep,'global-input');assert(error.rollback.length>=2);assert(error.rollback.every(r=>r.ok===false));return true;});assert.equal(p.state.mouse.on,false);assert.equal(p.calls.filter(c=>c.op==='input.cas').length,1);assert.equal(p.settings.extensions.bootModes.current,'global');
});
await test('SteamDeck mouse delegates to Steam with an explicit pending receipt and never attempts forbidden native ON or backend writes',async()=>{
 const doc=await fresh();doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'steamdeck',mouse:'steamdeck'};stored(doc);const {document:saved,receipt}=await startMode('steam',doc);const actual=await p.getMouseModeState();assert.equal(saved.current,'steam');assert.equal(actual.provider,'steam');assert.equal(actual.on,false);assert.equal(actual.stateKnown,false);assert.equal(actual.controlAllowed,false);assert.equal(actual.reason,'steamdeck_managed');assert(!opNames().includes('mouse.backend'));const cas=p.calls.findIndex(c=>c.op==='input.cas');assert(cas>=0);assert(!p.calls.slice(cas+1).some(c=>c.op==='mouse.enable'||c.op==='mouse.policy'||c.op==='mouse.backend'),'Steam-owned target invoked a forbidden native mutator');assert(receipt.pending.some(value=>value.includes('由 Steam 管理')||value.includes('由Steam管理')));assert(!receipt.applied.includes('模拟鼠标'));return {actual,mouseRequests:p.calls.filter(c=>c.op.startsWith('mouse.')),pending:receipt.pending};
});
await test('startup result parsing rejects stale boots, malformed metadata and invalid lists while bounding displayed diagnostics',async()=>{
 const legacyInputRows=[];
 for(const mode of m.BOOT_MODE_IDS)for(const module of ['input','gyro'])for(const state of ['active','disabled'])for(const status of ['applied','failed']){
  const label=module==='input'?'虚拟手柄':'陀螺仪',other=module==='input'?'陀螺仪':'虚拟手柄';
  const raw={schema:1,boot:'proof-boot',mode,sourceRevision:2,revision:3,status,applied:[label,other,'监控'],skipped:[],pending:['保留原错误'],error:'原失败不能消失',modules:{[module]:{state,detail:'旧版本记录'},monitor:{state:'active'}}};
  const before=structuredClone(raw),normalized=m.normalizeBootStartupResult(raw,'proof-boot');
  assert(normalized,'legacy record and its failure details must remain visible');
  assert.equal(normalized.modules[module].state,'saved','unproved input terminal state must not become proof of output');
  assert.equal(normalized.status,status==='failed'?'failed':'partial');
  assert.equal(normalized.error,raw.error);assert(normalized.pending.includes('保留原错误'));
  assert(normalized.pending.some(p=>p.startsWith(module==='input'?'手柄输出：':'陀螺仪：')));
  assert(!normalized.applied.includes(label));assert(normalized.applied.includes(other));assert(normalized.applied.includes('监控'));
  assert.equal(normalized.modules.monitor.state,'active');assert.deepEqual(raw,before);
  assert.deepEqual(m.normalizeBootStartupResult(normalized,'proof-boot'),normalized,'legacy classification must be idempotent');
  legacyInputRows.push({mode,module,state,status});
 }
 assert.equal(legacyInputRows.length,32);

 const valid={schema:1,boot:'boot-current',mode:'steam',sourceRevision:2,revision:3,status:'partial',applied:[],skipped:[],pending:['monitor pending']};assert(m.normalizeBootStartupResult(valid,'boot-current'));for(const patch of [{schema:2},{mode:'unknown'},{status:'running'},{sourceRevision:-1},{revision:1},{sourceRevision:2.5},{pending:['good',3]},{skipped:null}])assert.equal(m.normalizeBootStartupResult({...valid,...patch},'boot-current'),undefined);assert.equal(m.normalizeBootStartupResult(valid,'other-boot'),undefined);assert.equal(m.normalizeBootStartupResult(valid,null),undefined);const bounded=m.normalizeBootStartupResult({...valid,pending:Array.from({length:100},()=> 'x'.repeat(2000)),error:'e'.repeat(2000)},'boot-current');assert.equal(bounded.pending.length,64);assert.equal(bounded.pending[0].length,500);assert.equal(bounded.error.length,1000);assert.match(m.bootStartupStatus(bounded),/部分完成/);assert.equal(m.bootStartupStatus(undefined),'');
 const optionalSkipped=['Decky：连接状态尚未确认','小黄鸭联动：预设选择尚未确认'];
 for(const mode of m.BOOT_MODE_IDS){
  const optional=m.normalizeBootStartupResult({...valid,mode,pending:[],skipped:optionalSkipped},'boot-current'),original=structuredClone(optional);
  assert.match(m.bootStartupStatus(optional),/Decky/,'Diagnostic formatting retains the skipped detail');
  assert.deepEqual(optional,original,'Presentation must not rewrite the diagnostic receipt');
  assert.equal(optional.status,'partial');assert.deepEqual(optional.skipped,optionalSkipped);
  assert.match(m.bootStartupStatus({...optional,pending:['monitor pending']}),/monitor pending/);
  assert.match(m.bootStartupStatus({...optional,status:'failed'}),/未完成/);
  assert.match(m.bootStartupStatus({...optional,status:'failed',error:'configuration write failed'}),/configuration write failed/);
 }
});
await test('startup and a queued next-mode choice complete in order, including unchanged current-mode names',async()=>{
 const evidence=[];
 for(const current of ['global','steam']){
  const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.current=current;doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'disabled',mouse:'off'};a.ports.settings.extensions.bootModes=structuredClone(doc);const games=structuredClone(a.ports.settings.gameProfiles);let enter,release;const entered=new Promise(resolve=>enter=resolve),held=new Promise(resolve=>release=resolve);a.ports.hooks.mouseRead=()=>{enter();return held;};
  const restoring=a.boot.restoreSelectedBootMode();await waitSignal(entered,'startup capture');let choiceDone=false,editorDone=false;const editor=a.boot.loadBootModes().then(value=>{editorDone=true;return value;});const choice=a.boot.chooseBootMode('xbox',structuredClone(doc),true).then(value=>{choiceDone=true;return value;});await new Promise(resolve=>setTimeout(resolve,20));assert.equal(choiceDone,false);assert.equal(editorDone,false);release();delete a.ports.hooks.mouseRead;await waitSignal(restoring,'serialized startup');const viewed=await waitSignal(editor,'serialized editor load'),selected=await waitSignal(choice,'queued next-mode choice');assert.equal(viewed.document.current,'steam');assert.equal(selected.current,'steam');assert.equal(selected.boot,'xbox');assert.equal(selected.revision,2);assert.equal(a.ports.settings.input.outputTarget.persona,'disabled');assert.equal(a.ports.settings.startupDesired.virtualGamepadPersona,doc.global.input.persona);assert.deepEqual(a.ports.settings.gameProfiles,games);await confirmInputReceipt(a);assert.equal(a.ports.settings.extensions.bootModesStartupResult.status,'applied');assert.equal(a.ports.settings.extensions.bootModesStartupResult.sourceRevision,0);assert.equal(a.ports.settings.extensions.bootModesStartupResult.revision,1);evidence.push({previousCurrent:current,current:'steam',next:'xbox',revision:2,gameProfilesUnchanged:true});
 }return evidence;
});
await test('saving an edited template before startup restoration waits for a new Windows boot instead of applying it now',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;a.ports.settings.extensions.bootModes=structuredClone(doc);const games=structuredClone(a.ports.settings.gameProfiles),input=structuredClone(a.ports.settings.input),draft=structuredClone(doc);draft.scenes.steam.overrides.performance={...structuredClone(doc.global.performance),controls:structuredClone(doc.global.performance.controls)};draft.scenes.steam.overrides.performance.controls.ac.tdpCeiling=47;const saved=await a.boot.saveBootModes(draft);await a.boot.restoreSelectedBootMode();assert.equal(a.ports.settings.performanceSchedule.controls.ac.tdpCeiling,30);assert.equal(a.ports.settings.extensions.bootModes.current,'global');assert.equal(a.ports.settings.extensions.bootModes.boot,'steam');assert.equal(a.ports.settings.extensions.bootModes.revision,saved.revision);assert.deepEqual(a.ports.settings.input,input);assert.deepEqual(a.ports.settings.gameProfiles,games);assert.equal(a.ports.calls.filter(c=>c.op==='performance.restore').length,0);const durable=structuredClone(a.ports.settings);const restarted=await runtimeBundle(out);Object.assign(restarted.ports.settings,durable);restarted.ports.hooks.nativeSession=()=> 'new-process-same-boot';await restarted.boot.restoreSelectedBootMode();assert.equal(restarted.ports.settings.performanceSchedule.controls.ac.tdpCeiling,30);const rebooted=await runtimeBundle(out);Object.assign(rebooted.ports.settings,durable);rebooted.ports.hooks.bootIdentity=()=> 'fixture-windows-boot-2';rebooted.ports.settings.input.startupObservedBoot='fixture-windows-boot-2';await rebooted.boot.restoreSelectedBootMode();assert.equal(rebooted.ports.settings.performanceSchedule.controls.ac.tdpCeiling,47);assert.equal(rebooted.ports.settings.extensions.bootModes.current,'steam');assert.deepEqual(rebooted.ports.settings.gameProfiles,games);return {savedTdp:47,currentTdp:30,sameBootRestartTdp:30,nextWindowsBootTdp:47,currentInputUnchanged:true};
});
await test('a boot document changed outside the startup queue cannot replace the claimed startup target',async()=>{
 if(process.argv.includes('--active-publication-native-files')){
  const {runActivePublicationOwner}=await import('./boot_modes_fixture/activePublicationOwner.mjs');
  return await runActivePublicationOwner(out,process.argv.slice(2),{native:globalThis.__bootModeSaveNative,bootModesSource:sourceIndex<0?undefined:path.resolve(process.argv[sourceIndex+1])});
 }
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'disabled',mouse:'off'};a.ports.settings.extensions.bootModes=structuredClone(doc);let enter,release;const entered=new Promise(resolve=>enter=resolve),held=new Promise(resolve=>release=resolve);a.ports.hooks.mouseRead=()=>{enter();return held;};const restoring=a.boot.restoreSelectedBootMode();await waitSignal(entered,'startup claim');const changed=structuredClone(doc);changed.boot='xbox';changed.revision=7;a.ports.settings.extensions.bootModes=changed;const before=structuredClone(a.ports.settings.input);release();delete a.ports.hooks.mouseRead;await assert.rejects(()=>restoring,/开机模式已变化/);assert.equal(a.ports.settings.extensions.bootModes.boot,'xbox');assert.equal(a.ports.settings.extensions.bootModes.current,'global');assert.deepEqual(a.ports.settings.input,before);assert.equal(a.ports.calls.filter(c=>c.op==='input.cas').length,0);assert.equal(a.ports.settings.extensions.bootModesStartupResult.status,'failed');assert.equal(a.ports.settings.extensions.bootModesStartupResult.mode,'steam');
});
await test('disabled startup applies global while keeping the next-scene draft and its disabled flag',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=false;a.ports.settings.extensions.bootModes=structuredClone(doc);await waitSignal(a.boot.restoreSelectedBootMode(),'disabled startup global restoration');assert.equal(a.ports.settings.extensions.bootModes.current,'global');assert.equal(a.ports.settings.extensions.bootModesStartupResult.mode,'global');assert.equal(a.ports.settings.extensions.bootModes.boot,'steam');assert.equal(a.ports.settings.extensions.bootModes.bootEnabled,false);assert(!a.ports.calls.some(c=>c.op==='platform.set'));
});
await test('metadata rebase cannot overwrite an edited template or a newer independent next-boot choice',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;a.ports.settings.extensions.bootModes=structuredClone(doc);await a.boot.restoreSelectedBootMode();const after=structuredClone(a.ports.settings.extensions.bootModes);after.global.performance.controls.ac.tdpCeiling=44;a.ports.settings.extensions.bootModes=after;const before=structuredClone(a.ports.settings);await assert.rejects(()=>a.boot.chooseBootMode('xbox',doc,true),/配置已变化/);assert.deepEqual(a.ports.settings,before);const current=structuredClone(after);current.revision=5;current.boot='xbox-steam';a.ports.settings.extensions.bootModes=current;await assert.rejects(()=>a.boot.chooseBootMode('xbox',doc,true),/配置已变化/);assert.equal(a.ports.settings.extensions.bootModes.boot,'xbox-steam');assert.equal(a.ports.settings.extensions.bootModes.global.performance.controls.ac.tdpCeiling,44);
 for(const id of m.BOOT_MODE_IDS){
  const s=await runtimeBundle(out),q=s.ports,draft=s.model.createBootModeDocument(await s.boot.captureBootModeSnapshot());
  draft.activeSnapshot.input.mouse=null;q.settings.extensions.bootModes=structuredClone(draft);
  // The deferred mouse acknowledgement changes only effective state, without advancing the preset revision.
  q.settings.extensions.bootModes.activeSnapshot.input.mouse='microsoft';
  const active=structuredClone(q.settings.extensions.bootModes.activeSnapshot),beforeInput=structuredClone(q.settings.input);
  const selected=await s.boot.chooseBootMode(id,draft);
  assert.equal(selected.boot,id);assert.equal(selected.revision,draft.revision+1);
  assert.deepEqual(selected.activeSnapshot,active,'Selecting next boot overwrote a same-revision runtime acknowledgement');
  assert.deepEqual(q.settings.extensions.bootModes.activeSnapshot,active);assert.deepEqual(q.settings.input,beforeInput);
 }
 const native=globalThis.__bootModeSaveNative;
 for(const id of m.BOOT_MODE_IDS){
  const disk=native?await (await import('./boot_modes_fixture/diskSettings.mjs')).createDiskRuntime(path.join(out,'next-boot-metadata-'+id),native,{moduleDirectory:path.join(out,'next-boot-metadata-runtime')}):null;
  const s=disk?disk.a:await runtimeBundle(out),q=s.ports;
  if(disk)Object.assign(q.settings,await s.repository.loadSettings());
  const persisted=s.model.createBootModeDocument(await s.boot.captureBootModeSnapshot());
  q.settings.extensions.bootModes=structuredClone(persisted);
  if(disk)disk.writeRaw({...disk.read(),extensions:structuredClone(q.settings.extensions)});
  const {document:draft}=await s.boot.loadBootModes({overviewOnly:true});
  draft.global.input.persona='elite';draft.global.background.ui.backgroundOpacity=.35;
  draft.scenes.steam.name='unsaved';draft.presets.global={unsaved:{name:'unsaved',overrides:{}}};
  const selected=await s.boot.chooseBootMode(id,draft);
  assert.deepEqual(selected.global,persisted.global,'Selection saved an unsaved global template');
  assert.deepEqual(selected.scenes,persisted.scenes,'Selection saved an unsaved scene template');
  assert.deepEqual(selected.presets,persisted.presets,'Selection saved an unsaved named preset');
  const reopened=disk?await (await disk.moduleAt('reopen')).repository.loadSettings():q.settings;
  assert.equal(reopened.extensions.bootModes.boot,id);
  assert.deepEqual(reopened.extensions.bootModes.global,persisted.global);
  assert.deepEqual(reopened.extensions.bootModes.scenes,persisted.scenes);
  assert.deepEqual(reopened.extensions.bootModes.presets,persisted.presets);
  assert.equal(reopened.startupDesired.virtualGamepadPersona,s.model.resolveBootMode(persisted,id).input.persona);
 }
 return {concurrentRevisionsRefused:true,sameRevisionActiveSnapshotPreservedFor:4,unsavedTemplatesNotSelectedFor:4,realFileReopens:native?4:0};
});
await test('failed startup remains visible on overview and same-boot app restart without replaying the next chosen mode',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'disabled',mouse:'off'};a.ports.settings.extensions.bootModes=structuredClone(doc);a.ports.faults['input.stale']=1;await assert.rejects(()=>a.boot.restoreSelectedBootMode(),/revision-conflict/);const initial=await a.boot.loadBootModes({overviewOnly:true});assert.equal(initial.startupResult.status,'failed');assert.equal(initial.startupResult.mode,'steam');assert.match(initial.startupResult.error,/revision-conflict/);assert.match(a.model.bootStartupStatus(initial.startupResult),/未完成/);assert.equal(initial.document.current,'global');const selected=await a.boot.chooseBootMode('xbox',initial.document,true);const durable=structuredClone(a.ports.settings);const restart=await runtimeBundle(out);Object.assign(restart.ports.settings,durable);restart.ports.hooks.nativeSession=()=> 'different-ymcc-process';await restart.boot.restoreSelectedBootMode();const reopened=await restart.boot.loadBootModes({overviewOnly:true});assert.equal(reopened.startupResult.status,'failed');assert.equal(reopened.startupResult.mode,'steam');assert.equal(reopened.document.boot,'xbox');assert.equal(reopened.document.current,'global');assert.equal(reopened.document.revision,selected.revision);assert(restart.ports.calls.every(c=>c.op==='ipc.bootMode.startup.identity'||c.op==='settings.update'));return {persistedFailure:true,reopenDoesNotReplay:true,nextModeIsSeparate:true};
});
await test('partial startup result survives remount and is not presented for an unknown or different Windows boot',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.monitor={preferences:{...doc.global.monitor.preferences,ymccEnabled:true}};a.ports.settings.extensions.bootModes=structuredClone(doc);a.ports.faults['monitor.pending']=1;await a.boot.restoreSelectedBootMode();const saved=await a.boot.loadBootModes({overviewOnly:true});assert.equal(saved.startupResult.status,'partial');assert(saved.startupResult.pending.some(value=>value.includes('监控')));assert.equal(saved.document.current,'steam');const next=await a.boot.chooseBootMode('xbox',saved.document,true);const reopened=await a.boot.loadBootModes({overviewOnly:true});assert.equal(reopened.startupResult.status,'partial');assert.equal(next.boot,'xbox');a.ports.settings.input.startupObservedBoot=null;assert.equal((await a.boot.loadBootModes({overviewOnly:true})).startupResult,undefined);a.ports.settings.input.startupObservedBoot='other-windows-boot';assert.equal((await a.boot.loadBootModes({overviewOnly:true})).startupResult,undefined);assert.equal(a.ports.settings.extensions.bootModesStartupResult.status,'partial');return {partialPersisted:true,nextChoiceDoesNotHideFailure:true,otherBootDoesNotShowOldResult:true};
});
await test('a failed result-file commit still publishes its uncertainty without retrying or replaying the startup scene',async()=>{
 const a=await runtimeBundle(out),q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
 doc.boot='steam';doc.bootEnabled=true;q.settings.extensions.bootModes=structuredClone(doc);
 const receipts=[],attempts=[];let recordedClaim;
 const receive=event=>receipts.push(structuredClone(event.detail));
 q.hooks.settingsWrite=draft=>{
  const pending=draft.extensions?.bootModesStartupResult;
  if(!pending?.modules)return;
  attempts.push(pending);
  recordedClaim??=structuredClone(q.settings.extensions.bootModesStartupResult);
  throw Error('injected:startup.result.write');
 };
 window.addEventListener('boot-mode:changed',receive);
 try{
  await a.boot.restoreSelectedBootMode();
  const callsAfterFirst=q.calls.length;
  await a.boot.restoreSelectedBootMode();
  assert.equal(q.calls.length,callsAfterFirst,'Same renderer must not replay the claimed scene');
 }finally{window.removeEventListener('boot-mode:changed',receive);delete q.hooks.settingsWrite;}
 assert.equal(q.settings.extensions.bootModes.current,'steam');assert.equal(q.settings.extensions.bootModes.revision,1);
 assert.equal(attempts.length,1);assert.equal(attempts[0].revision,1);
 assert.equal(recordedClaim?.status,'partial');assert.equal(recordedClaim.revision,0);assert.equal(recordedClaim.mode,'steam');
 assert.equal(recordedClaim.modules,undefined);assert(recordedClaim.pending.some(value=>value.includes('已领取')));
 assert.deepEqual(q.settings.extensions.bootModesStartupResult,recordedClaim,'An unacknowledged write must not replace the durable claim');
 assert.equal(receipts.length,1);assert(receipts[0].pending.some(value=>value.includes('开机处理结果：保存未确认')));
 assert(q.calls.filter(c=>c.op.startsWith('startup.')).every(c=>['startup.steam','startup.commandRepair'].includes(c.op)));
 return {startupAppliedOnce:true,unconfirmedClaimPreserved:true,resultSaveUncertaintyVisible:true,automaticRetries:0};
});
await test('an unanswered linked-app file observation cannot hold startup or launch late after its pending result',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'scene',delay:0};a.ports.settings.extensions.bootModes=structuredClone(doc);let release;const held=new Promise(resolve=>release=resolve);a.ports.hooks.fileRead=()=>held;
 try{await fastReadClock(async()=>{await waitSignal(a.boot.restoreSelectedBootMode(),'bounded linked application read');const result=a.ports.settings.extensions.bootModesStartupResult;assert.equal(result.status,'partial');assert(result.pending.some(value=>value.includes('应用文件')&&value.includes('读取超时')));assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,0);const chosen=await waitSignal(a.boot.chooseBootMode('xbox',a.ports.settings.extensions.bootModes,true),'queue released after app observation timeout');assert.equal(chosen.current,'steam');assert.equal(chosen.boot,'xbox');release();delete a.ports.hooks.fileRead;await new Promise(resolve=>setImmediate(resolve));assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,0);assert.equal(a.ports.calls.filter(c=>c.op==='file.exists').length,1);assert.equal(a.ports.settings.extensions.bootModesStartupResult.status,'partial');});}finally{release();delete a.ports.hooks.fileRead;a.boot.cancelBootModeLaunches();}
 return {startupReturned:true,queueReleased:true,applicationLaunches:0,lateLaunches:0};
});
await test('delayed startup applications update the durable pending result once when they complete',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'scene',delay:1};a.ports.settings.extensions.bootModes=structuredClone(doc);await a.boot.restoreSelectedBootMode();assert.equal(a.ports.settings.extensions.bootModesStartupResult.status,'partial');assert(a.ports.settings.extensions.bootModesStartupResult.pending.some(value=>value.includes('1 秒')));await new Promise(resolve=>setTimeout(resolve,1100));assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,1);await confirmInputReceipt(a);assert.equal(a.ports.settings.extensions.bootModesStartupResult.status,'applied');assert.deepEqual(a.ports.settings.extensions.bootModesStartupResult.pending,[]);a.boot.cancelBootModeLaunches();
});
await test('all four confirmed startup scenes persist their own outcome once without rewriting platform startup or dedicated games',async()=>{
 const evidence=[];for(const mode of ['global','steam','xbox','xbox-steam']){const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot=mode;doc.bootEnabled=true;a.ports.settings.extensions.bootModes=structuredClone(doc);const games=structuredClone(a.ports.settings.gameProfiles);await a.boot.restoreSelectedBootMode();await a.boot.restoreSelectedBootMode();assert.equal(a.ports.settings.extensions.bootModes.current,mode);assert.equal(a.ports.settings.extensions.bootModes.revision,1);assert.equal(a.ports.settings.extensions.bootModesStartupResult.mode,mode);await confirmInputReceipt(a);const result=a.ports.settings.extensions.bootModesStartupResult;assert.equal(result.status,'applied');assert.equal(a.ports.settings.input.outputTarget.persona,'disabled');if(mode==='steam'||mode==='xbox-steam'){assert.deepEqual(result.pending,[]);assert.deepEqual(a.ports.state.touch.disabled,a.model.resolveBootMode(doc,mode).touch.profiles.disabled);assert.equal(a.ports.state.touch.disabled.standaloneSpecialMode,'steamdeck');}assert.deepEqual(a.ports.settings.gameProfiles,games);assert(!a.ports.calls.some(c=>c.op==='platform.set'||c.op.startsWith('startup.')&&!['startup.steam','startup.commandRepair'].includes(c.op)));assert.equal(a.ports.state.steam,mode==='steam'||mode==='xbox-steam');const reopened=await a.boot.loadBootModes({overviewOnly:true});assert.equal(reopened.startupResult.mode,mode);evidence.push({mode,current:mode,appliedOnce:true,osStartupWrites:a.ports.calls.filter(c=>c.op==='startup.steam').length,windowsStartupWrites:0,gameProfilesUnchanged:true});}return evidence;
});
await test('enabled startup restoration applies once while repairing only the drifted Steam route',async()=>{
 const isolated=await runtimeBundle(out);const doc=isolated.model.createBootModeDocument(await isolated.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;isolated.ports.settings.extensions.bootModes=structuredClone(doc);await isolated.boot.restoreSelectedBootMode();await isolated.boot.restoreSelectedBootMode();assert.equal(isolated.ports.settings.extensions.bootModes.current,'steam');assert.equal(isolated.ports.settings.extensions.bootModes.revision,1);assert(!isolated.ports.calls.some(c=>c.op==='platform.set'||c.op.startsWith('startup.')&&!['startup.steam','startup.commandRepair'].includes(c.op)||c.op.startsWith('steam.')));assert.equal(isolated.ports.state.steam,true);assert.equal(isolated.ports.calls.filter(c=>c.op==='startup.steam').length,1);
});
await test('save preserves current and next-boot selections even if a draft tries to change them',async()=>{
 const doc=await fresh();doc.current='xbox';doc.boot='steam';doc.bootEnabled=true;stored(doc);const draft=structuredClone(doc);draft.current='global';draft.boot='xbox-steam';draft.bootEnabled=false;const saved=await b.saveBootModes(draft);assert.equal(saved.current,'xbox');assert.equal(saved.boot,'steam');assert.equal(saved.bootEnabled,true);
});
await test('next-boot input preferences follow the selected scene while current input and game profiles stay intact',async()=>{
 const doc=await fresh(),games=gameState(),input=JSON.stringify(p.settings.input);doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'steamdeck'};doc.scenes.steam.overrides.gyro={motion:{enabled:true,preset:'racing'}};stored(doc);await b.chooseBootMode('steam',doc,true);assert.equal(p.settings.startupDesired.virtualGamepadPersona,'steamdeck');assert.equal(p.settings.startupDesired.gyroPreset,'racing');assert.equal(JSON.stringify(p.settings.input),input);assert.equal(gameState(),games);
});
await test('fan intent without an active session marker follows next boot and delegates current resume to the common owner',async()=>{
 if(process.argv.includes('--fan-intent-native-files')){const {runFanIntentInheritance}=await import('./boot_modes_fixture/preferenceOwner.mjs');return await runFanIntentInheritance(out,process.argv.slice(2),{native:globalThis.__bootModeSaveNative,bootModesSource:sourceIndex<0?undefined:path.resolve(process.argv[sourceIndex+1])});}
 const doc=await fresh();doc.boot='steam';doc.current='xbox';doc.bootEnabled=true;doc.scenes.steam.overrides.fan={enabled:true};doc.scenes.xbox.overrides.fan={enabled:false};stored(doc);assert.equal(await b.readBootModeFanIntent('startup'),null);assert.equal(await b.readBootModeFanIntent('current'),null);doc.bootEnabled=false;stored(doc);assert.equal(await b.readBootModeFanIntent('startup'),null);assert.equal(await b.readBootModeFanIntent('current'),null);assert(p.calls.every(c=>c.op==='ipc.bootMode.startup.identity'));
});
await test('fan startup and resume use only the common preference across mode and Windows boot changes',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
 a.ports.settings.startupDesired.fanControl=true;doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.fan={enabled:false,preset:'soft'};
 a.ports.settings.extensions.bootModes=structuredClone(doc);const games=structuredClone(a.ports.settings.gameProfiles);await a.boot.restoreSelectedBootMode();
 const active=a.ports.settings.extensions.bootModesActiveBoot;assert.equal(active,'fixture-windows-boot-1');assert.equal(a.ports.state.fanIntent,false);
 const draft=structuredClone(a.ports.settings.extensions.bootModes);draft.scenes.xbox.overrides.fan={enabled:true,preset:'aggressive'};draft.global.fan={enabled:false,preset:'soft'};
 const saved=await a.boot.saveBootModes(draft),selected=await a.boot.chooseBootMode('xbox',saved,true);assert.equal(selected.current,'steam');
 assert.equal(saved.global.fan,undefined);assert.equal(saved.scenes.xbox.overrides.fan,undefined);
 for(const boot of [active,'fixture-windows-boot-2']){a.ports.hooks.bootIdentity=()=>boot;assert.equal(await a.boot.readBootModeFanIntent('current'),null);assert.equal(await a.boot.readBootModeFanIntent('startup'),null);}
 const disabled=await a.boot.chooseBootMode('xbox',selected,false);assert.equal(disabled.bootEnabled,false);assert.equal(a.ports.settings.startupDesired.fanControl,true);
 assert(!a.ports.calls.some(c=>c.op.startsWith('fan.')));assert.deepEqual(a.ports.settings.gameProfiles,games);
 return {commonPreferencePreserved:true,legacyModeFanRetired:true,fanActions:0};
});
await test('unconfirmed Windows boot returns no current or startup fan intent and never revives a stored fan snapshot',async()=>{
 const doc=await fresh();doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.fan={enabled:true,preset:'balanced'};stored(doc);p.settings.extensions.bootModesActiveBoot='fixture-windows-boot-1';p.faults['ipc.bootMode.startup.identity']=2;const before=structuredClone(p.settings);assert.equal(await b.readBootModeFanIntent('current'),null);assert.equal(await b.readBootModeFanIntent('startup'),null);assert.deepEqual(p.settings,before);assert.equal(p.state.fanIntent,false);assert(p.calls.every(c=>c.op==='ipc.bootMode.startup.identity'));return {currentFanIntent:null,startupFanIntent:null,fanWrites:0};
});
await test('legacy schema 1 missing bootEnabled retains its startup fan intent',async()=>{
 const doc=await fresh();doc.boot='steam';doc.scenes.steam.overrides.fan={enabled:true};delete doc.bootEnabled;stored(doc);assert.equal(await b.readBootModeFanIntent('startup'),null);
});
await test('durable Windows-boot claim prevents applying next-boot selection on renderer reload',async()=>{
 const a=await runtimeBundle(out);const doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;a.ports.settings.extensions.bootModes=structuredClone(doc);await a.boot.restoreSelectedBootMode();const selected=await a.boot.chooseBootMode('xbox',a.ports.settings.extensions.bootModes,true);const durable=structuredClone(a.ports.settings);
 const reload=await runtimeBundle(out);Object.assign(reload.ports.settings,durable);await reload.boot.restoreSelectedBootMode();assert.equal(reload.ports.settings.extensions.bootModes.current,'steam');assert.equal(reload.ports.settings.extensions.bootModes.boot,'xbox');assert.equal(reload.ports.settings.extensions.bootModes.revision,selected.revision);assert.deepEqual(reload.ports.calls.map(c=>c.op),['ipc.bootMode.startup.identity']);assert.deepEqual(reload.ports.settings,durable);
});
await test('restarting YMCC in the same Windows boot cannot apply a selected next scene; the next confirmed boot applies it once',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.fan={enabled:true,preset:'balanced'};doc.scenes.xbox.overrides.fan={enabled:false,preset:'soft'};doc.scenes.xbox.overrides.input={...structuredClone(doc.global.input),persona:'dualsense-edge'};a.ports.settings.extensions.bootModes=structuredClone(doc);await a.boot.restoreSelectedBootMode();const selected=await a.boot.chooseBootMode('xbox',a.ports.settings.extensions.bootModes,true),durable=structuredClone(a.ports.settings);
 const restart=await runtimeBundle(out);Object.assign(restart.ports.settings,durable);restart.ports.hooks.nativeSession=()=> 'new-process-same-boot';await restart.boot.restoreSelectedBootMode();assert.equal(restart.ports.settings.extensions.bootModes.current,'steam');assert.equal(restart.ports.settings.extensions.bootModes.boot,'xbox');assert.equal(restart.ports.settings.extensions.bootModes.revision,selected.revision);assert.deepEqual(restart.ports.calls.map(c=>c.op),['ipc.bootMode.startup.identity']);assert.equal(await restart.boot.readBootModeFanIntent('startup'),null);assert.deepEqual(restart.ports.settings,durable);
 const next=await runtimeBundle(out),nextBoot='fixture-windows-boot-2';Object.assign(next.ports.settings,structuredClone(durable));next.ports.settings.input.startupObservedBoot=nextBoot;next.ports.hooks.bootIdentity=()=>nextBoot;await next.boot.restoreSelectedBootMode();await next.boot.restoreSelectedBootMode();assert.equal(next.ports.settings.extensions.bootModes.current,'xbox');assert.equal(next.ports.settings.extensions.bootModes.revision,selected.revision+1);assert.equal(next.ports.settings.extensions.bootModesActiveBoot,nextBoot);assert.equal(next.ports.settings.input.outputTarget.persona,'dualsense-edge');assert.equal(await next.boot.readBootModeFanIntent('startup'),null);assert.deepEqual(next.ports.settings.gameProfiles,durable.gameProfiles);return {sameBootCurrent:'steam',nextBoot:'xbox',confirmedNewBootCurrent:'xbox',restartWrites:0,gameProfilesUnchanged:true};
});
await test('selection before restoration consumes this boot without applying the newly chosen scene',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());a.ports.settings.extensions.bootModes=structuredClone(doc);const selected=await a.boot.chooseBootMode('xbox',doc,true);a.ports.calls.length=0;await a.boot.restoreSelectedBootMode();assert.equal(a.ports.settings.extensions.bootModes.current,'global');assert.equal(a.ports.settings.extensions.bootModes.revision,selected.revision);assert.equal(await a.boot.readBootModeFanIntent('startup'),null);assert(a.ports.calls.every(c=>c.op==='settings.update'||c.op==='ipc.bootMode.startup.identity'));
 const x=await runtimeBundle(out),q=x.ports,original=x.model.createBootModeDocument(await x.boot.captureBootModeSnapshot());
 q.settings.extensions.bootModes=structuredClone(original);
 let enter,release;const entered=new Promise(r=>enter=r),held=new Promise(r=>release=r);
 q.hooks.bootIdentity=()=>{enter();return held;};
 const saving=x.boot.saveBootModes(original).then(value=>({value}),error=>({error}));
 await waitSignal(entered,'save holding the boot-mode queue before startup restoration');
 const restoring=x.boot.restoreSelectedBootMode().then(value=>({value}),error=>({error}));
 q.invalidate();delete q.hooks.bootIdentity;
 const replacement=structuredClone(original);replacement.boot='xbox-steam';replacement.bootEnabled=true;
 q.settings.extensions.bootModes=replacement;
 const before=structuredClone(q.settings),operations=q.calls.length;
 release('fixture-windows-boot-1');
 assert.match(String((await saving).error),/stale generation/);
 const outcome=await restoring;
 assert.deepEqual(q.settings,before,'Startup queued before reset applied a scene from the replacement configuration');
 assert.deepEqual(q.calls.slice(operations),[],'Stale queued restore dispatched native reads or writes');
 assert.match(String(outcome.error),/stale generation/);
 return {selectionDefersRestore:true,queuedRestoreCannotCrossGeneration:true};
});
await test('unknown Windows boot identity never falls back to a new process identity or applies a pending scene',async()=>{
 const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='xbox';doc.bootEnabled=true;a.ports.settings.extensions.bootModes=structuredClone(doc);a.ports.hooks.bootIdentity=()=> '';a.ports.hooks.nativeSession=()=> 'known-new-process';const before=structuredClone(a.ports.settings);await assert.rejects(()=>a.boot.restoreSelectedBootMode(),/Windows/);assert.deepEqual(a.ports.settings,before);assert.equal(await a.boot.readBootModeFanIntent('startup'),null);assert(a.ports.calls.every(c=>c.op==='ipc.bootMode.startup.identity'));
});
await test('linked application paths use the global library spelling and execute each Windows path only once',async()=>{
 const doc=await fresh();doc.scenes.steam.overrides.softwareLaunch={paths:[String.raw`c:\fixture\APP.exe`,'C:/fixture/app.exe','C:/fixture/app.exe'],timing:'scene',delay:0};stored(doc);const result=await startMode('steam',doc);const launches=p.calls.filter(c=>c.op==='app.launch');assert.equal(launches.length,1);assert.equal(launches[0].value.path,'C:/fixture/app.exe');assert(!result.receipt.skipped.some(value=>value.includes('快捷库删除')));return {configuredAliases:3,appLaunches:1,canonicalGlobalLibraryPath:true};
});
await test('removing a linked application during a held file check revokes that pending launch before execute',async()=>{
 const doc=await fresh();doc.scenes.steam.overrides.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'scene',delay:0};stored(doc);let enter,release;const entered=new Promise(resolve=>enter=resolve),held=new Promise(resolve=>release=resolve);p.hooks.fileRead=()=>{enter();return held;};
 try{const applying=startMode('steam',doc);await waitSignal(entered,'held application file check');p.settings.quickApps.apps=[];release();const result=await applying;assert.equal(p.calls.filter(c=>c.op==='app.launch').length,0);assert(result.receipt.skipped.some(value=>value.includes('快捷库删除')));assert.equal(result.document.current,'steam');return {globalLibraryRemovalRevokesLaunch:true,appLaunches:0};}finally{release();delete p.hooks.fileRead;b.cancelBootModeLaunches();}
});
const readyLinkedSteam={known:true,running:true,mode:'bigpicture',pid:4321,birth:'12345',reason:'fixture-connected'};
const linkedSteamFixture=async()=>{const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.boot='steam';doc.bootEnabled=true;doc.scenes.steam.overrides.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'steam',delay:0};a.ports.settings.extensions.bootModes=structuredClone(doc);return a;};
const linkedAppReceipt=a=>{let resolve;const promise=new Promise(done=>resolve=done),receive=event=>{if(event.detail?.mode==='steam'&&a.ports.calls.filter(c=>c.op==='app.launch').length===1)resolve(event.detail);};window.addEventListener('boot-mode:changed',receive);return {promise,stop:()=>window.removeEventListener('boot-mode:changed',receive)};};
await test('Steam connected during the first stopped snapshot is not lost between observation and subscription',async()=>{
 const a=await linkedSteamFixture(),done=linkedAppReceipt(a);let reads=0;a.ports.steamUi.read=async()=>{const before=structuredClone(a.ports.state.steamUi);if(++reads===1){a.ports.state.steamUi=structuredClone(readyLinkedSteam);await a.ports.emitMemoryIpc('steam.uiStateChanged',{});return before;}return structuredClone(a.ports.state.steamUi);};
 try{await a.boot.restoreSelectedBootMode();await waitSignal(done.promise,'in-flight Steam connected handoff');assert.equal(reads,2);assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,1);assert.equal(a.ports.ipcListenerCount('steam.uiStateChanged'),0);await confirmInputReceipt(a);assert.equal(a.ports.settings.extensions.bootModesStartupResult.status,'applied');assert.deepEqual(a.ports.settings.extensions.bootModesStartupResult.pending,[]);assert(!a.ports.settings.extensions.bootModesStartupResult.pending.some(value=>value.includes('等待 Steam')));const events=a.ports.ipcEvents;assert(events.findIndex(e=>e.op==='subscribe'&&e.event==='steam.uiStateChanged')<events.findIndex(e=>e.op==='emit'&&e.event==='steam.uiStateChanged'));return {connectedEventNotLost:true,reads,appLaunches:1,listenerRemoved:true};}finally{done.stop();a.boot.cancelBootModeLaunches();}
});
await test('a newer Steam event during a queued read gets one follow-up instead of being discarded as busy',async()=>{
 const a=await linkedSteamFixture(),done=linkedAppReceipt(a);let reads=0,enter,release;const entered=new Promise(resolve=>enter=resolve),held=new Promise(resolve=>release=resolve);a.ports.steamUi.read=async()=>{const before=structuredClone(a.ports.state.steamUi);if(++reads===2){enter();await held;return before;}return before;};
 try{await a.boot.restoreSelectedBootMode();assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,0);const firstEvent=a.ports.emitMemoryIpc('steam.uiStateChanged',{});await waitSignal(entered,'held Steam event observation');a.ports.state.steamUi=structuredClone(readyLinkedSteam);await a.ports.emitMemoryIpc('steam.uiStateChanged',{});release();await firstEvent;await waitSignal(done.promise,'latched Steam event follow-up');assert.equal(reads,3);assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,1);assert.equal(a.ports.ipcListenerCount('steam.uiStateChanged'),0);return {initialStoppedRead:1,heldRead:1,followUpReads:1,appLaunches:1};}finally{release();done.stop();a.boot.cancelBootModeLaunches();}
});
await test('Steam process existence without a confirmed instance remains pending until connection identity is available',async()=>{
 const a=await linkedSteamFixture(),done=linkedAppReceipt(a);a.ports.state.steamUi={known:true,running:true,mode:'unknown',pid:0,birth:'0',reason:'legacy-running-only'};
 try{await a.boot.restoreSelectedBootMode();assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,0);assert(a.ports.settings.extensions.bootModesStartupResult.pending.some(value=>value.includes('等待 Steam')));a.ports.state.steamUi=structuredClone(readyLinkedSteam);await a.ports.emitMemoryIpc('steam.uiStateChanged',{});await waitSignal(done.promise,'confirmed Steam instance');assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,1);return {processOnlyDoesNotLaunch:true,confirmedInstanceLaunchesOnce:true};}finally{done.stop();a.boot.cancelBootModeLaunches();}
});
await test('a retired connected Steam reply cannot dispatch a linked app',async()=>{
 const a=await linkedSteamFixture(),done=linkedAppReceipt(a);
 const stopped=structuredClone(a.ports.state.steamUi);
 let reads=0,enter,release;
 const entered=new Promise(resolve=>enter=resolve),held=new Promise(resolve=>release=resolve);
 a.ports.steamUi.read=async()=>{
  const state=structuredClone(a.ports.state.steamUi);
  if(++reads===2){enter();await held;}
  return state;
 };
 try{
  await a.boot.restoreSelectedBootMode();
  a.ports.state.steamUi=structuredClone(readyLinkedSteam);
  const observing=a.ports.emitMemoryIpc('steam.uiStateChanged',{});
  await waitSignal(entered,'held connected Steam reply');
  a.ports.state.steamUi=stopped;
  await a.ports.emitMemoryIpc('steam.uiStateChanged',{});
  release();await observing;await new Promise(resolve=>setImmediate(resolve));
  assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,0,'retired connected reply triggered launch');
  assert.equal(reads,3,'the latest notification must receive exactly one fresh read');
  assert.equal(a.ports.ipcListenerCount('steam.uiStateChanged'),1);
  assert(a.ports.settings.extensions.bootModesStartupResult.pending.some(value=>value.includes('等待 Steam')));
  a.ports.state.steamUi={...readyLinkedSteam,pid:4322,birth:'12346'};
  await a.ports.emitMemoryIpc('steam.uiStateChanged',{});
  await waitSignal(done.promise,'replacement Steam instance');
  assert.equal(reads,4);
  assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,1);
  assert.equal(a.ports.ipcListenerCount('steam.uiStateChanged'),0);
  return {retiredReplyLaunches:0,followUpReads:1,replacementLaunches:1,listenerRemoved:true};
 }finally{release();done.stop();a.boot.cancelBootModeLaunches();}
});
await test('two hundred Steam events coalesce behind one in-flight read and leave no listener or read loop after launch',async()=>{
 const a=await linkedSteamFixture(),done=linkedAppReceipt(a);let reads=0,enter,release;const entered=new Promise(resolve=>enter=resolve),held=new Promise(resolve=>release=resolve);a.ports.steamUi.read=async()=>{const before=structuredClone(a.ports.state.steamUi);if(++reads===1){enter();await held;return before;}return before;};
 try{const startup=a.boot.restoreSelectedBootMode();await waitSignal(entered,'initial Steam read');a.ports.state.steamUi=structuredClone(readyLinkedSteam);await Promise.all(Array.from({length:200},()=>a.ports.emitMemoryIpc('steam.uiStateChanged',{})));assert.equal(reads,1);release();await startup;await waitSignal(done.promise,'Steam event burst completion');assert.equal(reads,2);assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,1);assert.equal(a.ports.ipcListenerCount('steam.uiStateChanged'),0);const count=a.ports.calls.length;await a.ports.emitMemoryIpc('steam.uiStateChanged',{});await a.ports.emitMemoryIpc('steam.uiStateChanged',{});assert.equal(reads,2);assert.equal(a.ports.calls.length,count);return {events:200,reads:2,appLaunches:1,automaticPolling:false,listenersAfterLaunch:0};}finally{release();done.stop();a.boot.cancelBootModeLaunches();}
});
await test('cancelling a held Steam handoff retires its listener and makes the late connected reply inert',async()=>{
 const a=await linkedSteamFixture();let reads=0,enter,release;const entered=new Promise(resolve=>enter=resolve),held=new Promise(resolve=>release=resolve);a.ports.steamUi.read=async()=>{if(++reads===2){enter();await held;}return structuredClone(a.ports.state.steamUi);};
 try{await a.boot.restoreSelectedBootMode();const pendingEvent=a.ports.emitMemoryIpc('steam.uiStateChanged',{});await waitSignal(entered,'held Steam handoff');a.boot.cancelBootModeLaunches();assert.equal(a.ports.ipcListenerCount('steam.uiStateChanged'),0);a.ports.state.steamUi=structuredClone(readyLinkedSteam);release();await pendingEvent;await a.ports.emitMemoryIpc('steam.uiStateChanged',{});await new Promise(resolve=>setImmediate(resolve));assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,0);assert.equal(reads,2);return {cancelledListener:true,lateLaunches:0,lateReads:0};}finally{release();a.boot.cancelBootModeLaunches();}
});
await test('Steam read failure leaves a pending receipt and a later real event can recover without automatic retries',async()=>{
 const a=await linkedSteamFixture(),done=linkedAppReceipt(a);let reads=0;a.ports.steamUi.read=async()=>{if(++reads===1)throw Error('fixture disconnected');return structuredClone(a.ports.state.steamUi);};
 try{await a.boot.restoreSelectedBootMode();assert.equal(reads,1);assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,0);assert.equal(a.ports.settings.extensions.bootModesStartupResult.status,'partial');a.ports.state.steamUi=structuredClone(readyLinkedSteam);await a.ports.emitMemoryIpc('steam.uiStateChanged',{});await waitSignal(done.promise,'Steam retry after a new event');assert.equal(reads,2);assert.equal(a.ports.calls.filter(c=>c.op==='app.launch').length,1);return {automaticRetries:0,newEventRecovered:true,appLaunches:1};}finally{done.stop();a.boot.cancelBootModeLaunches();}
});
await test('starting a failed new transaction cancels previously scheduled applications',async()=>{
 const doc=await fresh();doc.scenes.steam.overrides.softwareLaunch={paths:['C:/fixture/app.exe'],timing:'scene',delay:1};stored(doc);const first=await startMode('steam',doc);assert(first.receipt.pending.some(v=>v.includes('1')));const next=first.document;next.scenes.xbox.overrides.input={...structuredClone(next.global.input),persona:'dualsense-edge',mouse:'off'};p.faults['input.stale']=1;await assert.rejects(startMode('xbox',next));await new Promise(resolve=>setTimeout(resolve,1100));assert(!opNames().includes('app.launch'));b.cancelBootModeLaunches();
});
await test('first global template save preserves edits on reopen and leaves devices unchanged until reboot',async()=>{
 const opening=await b.loadBootModes({editorOnly:true}),doc=opening.document,before=structuredClone(doc.activeSnapshot);doc.global.performance.controls.ac.tdpCeiling=47;const saved=await b.saveBootModes(doc,doc.revision,opening.initialSource);const reopened=await b.loadBootModes();assert.equal(reopened.document.global.performance.controls.ac.tdpCeiling,47,'Reopening a first saved draft erased the unapplied edit');assert.deepEqual(saved.activeSnapshot,before,'Saving a first draft must preserve the observed active snapshot');assert.equal(p.settings.performanceSchedule.controls.ac.tdpCeiling,30);assert(!opNames().includes('performance.restore'));
});
await test('existing global template save is retained on reopen without startup application',async()=>{
 const doc=await fresh();stored(doc);doc.global.performance.controls.ac.tdpCeiling=49;await b.saveBootModes(doc);const reopened=await b.loadBootModes();assert.equal(reopened.document.global.performance.controls.ac.tdpCeiling,49);assert.equal(reopened.document.activeSnapshot.performance.controls.ac.tdpCeiling,30);assert.equal(p.settings.performanceSchedule.controls.ac.tdpCeiling,30);
});
await test('unsupported requested charge preset survives save, skipped Apply, and reopen',async()=>{
 const doc=await fresh();stored(doc);doc.global.charge={enabled:true,limit:73};const saved=await b.saveBootModes(doc);const {document:applied,receipt}=await startMode('global',saved);assert(receipt.skipped.some(v=>v.includes('充电')));assert(!opNames().includes('charge.set'));assert.equal(applied.activeSnapshot.charge.enabled,false);assert.equal(applied.activeSnapshot.charge.limit,100);const reopened=await b.loadBootModes();assert.deepEqual(reopened.document.global.charge,{enabled:true,limit:73});
});
await test('ordinary-page edit remains runtime-only even when the last applied preset matched the baseline',async()=>{
 const doc=await fresh();stored(doc);p.settings.performanceSchedule.controls.ac.tdpCeiling=36;const reopened=await b.loadBootModes();assert.equal(reopened.document.global.performance.controls.ac.tdpCeiling,30);assert.equal(reopened.document.activeSnapshot.performance.controls.ac.tdpCeiling,30);assert.equal(p.settings.performanceSchedule.controls.ac.tdpCeiling,36);assert(!Object.hasOwn(reopened.document.scenes.steam.overrides,'performance'));assert.equal(p.calls.length,0);
});
await test('save cannot replace the active snapshot with edited draft values',async()=>{
 const doc=await fresh();stored(doc);const active=structuredClone(doc.activeSnapshot);doc.activeSnapshot.performance.controls.ac.tdpCeiling=99;doc.global.performance.controls.ac.tdpCeiling=44;const saved=await b.saveBootModes(doc);assert.deepEqual(saved.activeSnapshot,active);assert.equal(saved.global.performance.controls.ac.tdpCeiling,44);
});
await test('successful optional module records active snapshot; pending acknowledgement retains old active state',async()=>{
 const doc=await fresh();stored(doc);doc.scenes.steam.overrides.monitor={preferences:{...doc.global.monitor.preferences,ymccEnabled:true}};p.faults['monitor.pending']=1;const first=await startMode('steam',doc);assert.equal(first.document.activeSnapshot.monitor.preferences.ymccEnabled,false);const second=await startMode('steam',first.document);assert.equal(second.document.activeSnapshot.monitor.preferences.ymccEnabled,true);assert.equal(p.settings.extensions.bootModes.activeSnapshot.monitor.preferences.ymccEnabled,true);
});
await test('transient auto-OFF is replaced by explicit OFF policy without restarting a mouse owner',async()=>{
 p.state.mouse.on=false;p.state.mouse.mode='auto';const doc=await fresh();doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),mouse:'off'};stored(doc);await startMode('steam',doc);assert.equal(p.state.mouse.mode,'off');assert.equal(p.state.mouse.on,false);assert.deepEqual(p.calls.filter(c=>c.op==='mouse.enable').map(c=>c.value),[false]);
});
await test('capture routes a global SteamDeck persona to Steam even though native mapping state is explicitly unknown',async()=>{
 p.settings.input.outputTarget.persona='steamdeck';const snapshot=await baseline(),actual=await p.getMouseModeState();assert.equal(actual.provider,'steam');assert.equal(actual.on,false);assert.equal(actual.stateKnown,false);assert.equal(snapshot.input.mouse,'steamdeck');assert.equal(snapshot.input.persona,'steamdeck');
});
await test('capture of a non-Steam global persona under a SteamDeck game override uses saved native mouse policy and backend without editing that override',async()=>{
 p.settings.input.gameOverride={sessionId:'game-owner',outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:false},sentinel:'native-owner'};const overlay=structuredClone(p.settings.input.gameOverride);p.settings.gamepad.mouseBackend='gamebar';p.state.mouse.backend='gamebar';p.settings.gamepad.mouseMode='on';p.state.mouse.mode='on';let snapshot=await baseline();assert.equal(snapshot.input.persona,'elite');assert.equal(snapshot.input.mouse,'microsoft');assert.equal(snapshot.input.mousePolicy,'on');assert.equal(snapshot.input.mouseBackend,'gamebar');p.settings.gamepad.mouseMode='off';p.state.mouse.mode='off';snapshot=await baseline();assert.equal(snapshot.input.mouse,'off');assert.deepEqual(p.settings.input.gameOverride,overlay);assert(!p.calls.some(c=>c.op.startsWith('mouse.')));
});
await test('unknown mouse state with allowed or unspecified provider gate still rejects mode entry',async()=>{
 for(const allowed of [true,undefined]){p.reset();const doc=await fresh();doc.scenes.steam.overrides.input={...structuredClone(doc.global.input),persona:'steamdeck',mouse:'steamdeck'};stored(doc);p.state.mouse.stateKnown=false;p.state.mouse.controlAllowed=allowed;p.state.mouse.on=false;p.state.mouse.mode='off';await assert.rejects(startMode('steam',doc));assert.equal(p.settings.extensions.bootModes.current,'global');}
});
await test('unsupported S4 suppresses all hibernation writes including file size and timeout',async()=>{
 const doc=await fresh();p.state.hibernate.supported=false;doc.scenes.steam.overrides.sleep={...structuredClone(doc.global.sleep),hibernate:true,hibernateSize:80,timeouts:{...doc.global.sleep.timeouts,acHibernate:10,dcHibernate:20}};stored(doc);const {receipt}=await startMode('steam',doc);assert(receipt.skipped.some(v=>v.includes('S4')));assert(!opNames().includes('hibernate.set'));assert(!opNames().includes('hibernate.size'));assert(!p.calls.some(c=>c.op==='sleep.timeout'&&c.value.kind==='hibernate'));
});
for(const boundary of ['startup','scene entry'])for(const terminal of ['disable','close'])await test('retired fan scene cannot undo common '+terminal+' at '+boundary,async()=>{
 const doc=await fresh();doc.scenes.steam.overrides.fan={enabled:true,preset:'soft'};stored(doc);
 if(boundary==='startup')await p.fanHostLifecycle[terminal]();
 else {let once=false;p.hooks.chargeRead=async()=>{if(!once){once=true;await p.fanHostLifecycle[terminal]();p.calls.length=0;}};}
 const before=structuredClone(p.settings.fan);p.calls.length=0;
 const result=await startMode('steam',doc);assert.equal(p.state.fanIntent,false);assert(!p.calls.some(c=>c.op.startsWith('fan.')));
 assert.equal(result.receipt.modules.fan,undefined);assert.deepEqual(p.settings.fan,before);
});
await test('retired fan OFF cannot close a newer common ON',async()=>{
 p.state.fanIntent=true;const doc=await fresh();doc.scenes.steam.overrides.fan={enabled:false,preset:'soft'};stored(doc);
 await p.fanHostLifecycle.applyPreset('aggressive',[{tempC:0,dutyPercent:100},{tempC:100,dutyPercent:100}]);p.calls.length=0;
 await startMode('steam',doc);assert(!p.calls.some(c=>c.op.startsWith('fan.')));assert.equal(p.state.fanIntent,true);
});
await test('retired fan scenes preserve the complete common custom curve table',async()=>{
 const original={soft:[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:70}],balanced:[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:90}],aggressive:[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:100}]};
 p.settings.fan={preset:'balanced',nodes:structuredClone(original.balanced),presetCurves:structuredClone(original),userMetadata:{keep:'custom-owner-setting'}};
 const doc=await fresh();assert.equal(doc.global.fan,undefined);
 const stale=[{tempC:0,dutyPercent:0},{tempC:100,dutyPercent:12}];doc.scenes.steam.overrides.fan={enabled:true,preset:'soft',settings:{nodes:stale,presetCurves:{soft:stale}}};stored(doc);
 p.settings.fan.presetCurves.soft=[{tempC:0,dutyPercent:5},{tempC:53,dutyPercent:27},{tempC:100,dutyPercent:73}];const expected=structuredClone(p.settings.fan);
 const {document:saved,receipt}=await startMode('steam',doc);assert.equal(receipt.modules.fan,undefined);assert(!p.calls.some(c=>c.op.startsWith('fan.')));assert.deepEqual(p.settings.fan,expected);
 await startMode('global',saved);assert.deepEqual(p.settings.fan,expected);assert.equal(p.state.fanIntent,false);
 return {customCurveTablePreserved:true,retiredPresetNotApplied:true};
});
await test('mode switching cannot replace global native shortcut flags or resurrect retired mode rules',async()=>{
 const doc=await fresh(),defaults=shortcuts.NATIVE_DEFAULT_SHORTCUTS;
 Object.assign(p.settings.gamepad,Object.fromEntries(defaults.map((v,i)=>[v.defaultKey,i%2===0])));
 const flags=structuredClone(p.settings.gamepad);for(const id of ['xbox','steam','xbox-steam'])doc.scenes[id].overrides.controller={rules:defaults.map(v=>({...v,enabled:true}))};stored(doc);
 let current=doc;for(const mode of ['xbox-steam','global','steam','xbox','global']){current=(await startMode(mode,current)).document;assert.deepEqual(p.settings.gamepad,flags);assert.equal(p.settings.input.bootModeShortcuts,null);assert(!opNames().includes('summon.set'));}
});

await test('new boot selection preserves common desktop startup and retires JoyXoff Run without changing current input',async()=>{
 const doc=await fresh();stored(doc);p.state.legacyDesktop=true;p.state.joyStartup='"C:\\SOFT\\Joyxoff\\Joyxoff.exe"';const input=JSON.stringify(p.settings.input),mouse=structuredClone(p.state.mouse);const next=await b.chooseBootMode('xbox',doc,true);
 assert.equal(next.boot,'xbox');assert.equal(next.current,'global');assert.equal(p.state.legacyDesktop,true);assert.equal(p.state.joyStartup,null);assert.equal(JSON.stringify(p.settings.input),input);assert.deepEqual(p.state.mouse,mouse);assert(!opNames().some(name=>name.startsWith('mouse.')));assert(!opNames().includes('app.launch'));assert(!opNames().includes('steam.launch'));
});
await test('retired startup cleanup failures remain visible while preserving the requested boot mode',async()=>{
 const doc=await fresh();stored(doc);p.state.legacyDesktop=true;p.state.joyStartup='C:\\SOFT\\Joyxoff\\Joyxoff.exe';p.faults['joy.startup']=1;const next=await b.chooseBootMode('steam',doc,true);
 assert.equal(next.boot,'steam');assert.equal(next.current,'global');assert.equal(next.bootEnabled,true);assert(!next.bootNotice.includes('旧桌面启动入口'));assert.match(next.bootNotice,/旧模拟鼠标开机入口/);assert.equal(p.settings.extensions.bootModes.bootNotice,next.bootNotice);assert.equal(p.state.legacyDesktop,true);assert(p.state.joyStartup);assert(!opNames().includes('steam.launch'));
});
await test('legacy startup cleanup preserves a foreign application stored under the old Run value name',async()=>{
 const doc=await fresh();stored(doc);p.state.joyStartup='C:\\Tools\\DifferentSoftware.exe';const before=p.state.joyStartup;await b.chooseBootMode('global',doc,false);assert.equal(p.state.joyStartup,before);assert(!opNames().includes('joy.startup'));assert(changes().every(startupOnly));
});
await test('native guard and shortcut-cache fixture contracts remain anchored to the actual native and bridge API sources',async()=>{
 const native=fs.readFileSync(path.join(import.meta.dirname,'../native/main.cpp'),'utf8');const body=name=>{const start=native.indexOf(name);assert(start>=0,'Missing native API '+name);const end=native.indexOf('\n}',start);assert(end>start);return native.slice(start,end);};const guard=body('static bool mouseModeSteamDeckBlocked() {');assert(guard.includes('inputShortcutRuntimeSnapshot()'));assert(guard.includes('g_inputHostBuildPersona == "steamdeck"'));assert(guard.includes('g_inputHostBoundPersona == "steamdeck"'));const receipt=body('static json mouseModeSteamDeckState(bool operation) {');for(const token of ['{"ok", !operation}','{"provider", "steam"}','{"on", false}','{"stateKnown", false}','{"controlAllowed", false}','steamdeck_managed'])assert(receipt.includes(token),'Native Steam guard receipt changed: '+token);
 for(const name of ['static json mouseModeSetBackend(const std::string& backend','static json mousePolicySet(const std::string& mode'])assert(body(name).includes('mouseModeSteamDeckBlocked()'),'Native mutation guard changed: '+name);const summon=body('ipc_on("summon.set", [](const json& a) -> json {');for(const token of ['g_openKeyboard =','g_mouseToggle =','summonSave();','{"openKeyboard", g_openKeyboard}','{"mouseToggle", g_mouseToggle}'])assert(summon.includes(token),'Native shortcut owner contract changed: '+token);
 const steamContract=fs.readFileSync(path.join(import.meta.dirname,'../src/bridge/steamUi.ts'),'utf8');assert.match(steamContract,/subscribe\(handler: \(\) => void\): \(\) => void \{\s*return on\('steam\.uiStateChanged',/);const gameproc=fs.readFileSync(path.join(import.meta.dirname,'../src/bridge/gameproc.ts'),'utf8');for(const operation of ["'mouseMode.get'","'mouseMode.setBackend'","'mouseMode.set'","'mouseMode.setPolicy'"])assert(gameproc.includes(operation));const yeman=fs.readFileSync(path.join(import.meta.dirname,'../src/bridge/yeman.ts'),'utf8');assert(yeman.includes("'summon.set'"));const ipc=fs.readFileSync(path.join(import.meta.dirname,'../src/bridge/ipc.ts'),'utf8');assert(ipc.includes('export function on<'));return {nativeContractInputs,actualNativeHash:report.sourceHashes['native/main.cpp'],behavioralMouseAndCacheTests:'executed below against actual bootModes.ts'};
});
await test('memory native mouse owner reproduces GET and forbidden mutation receipts for effective, bound and building SteamDeck personas',async()=>{
 const evidence=[];for(const gate of ['global','game-override','bound','building']){p.reset();if(gate==='global')p.settings.input.outputTarget.persona='steamdeck';if(gate==='game-override')p.settings.input.gameOverride={sessionId:'fixture-native-game',outputTarget:{persona:'steamdeck'}};if(gate==='bound')p.state.boundPersona='steamdeck';if(gate==='building')p.state.buildPersona='steamdeck';const before=structuredClone(p.settings),mouse=structuredClone(p.state.mouse),get=await p.getMouseModeState();assert.equal(get.ok,true);for(const receipt of [get,await p.setMouseBackend('gamebar'),await p.setMouseModeEnabled(true),await p.setMouseModeEnabled(false),await p.setMouseModePolicy('auto')]){assert.equal(receipt.provider,'steam');assert.equal(receipt.on,false);assert.equal(receipt.stateKnown,false);assert.equal(receipt.controlAllowed,false);assert.equal(receipt.reason,'steamdeck_managed');if(receipt!==get)assert.equal(receipt.ok,false);}assert.deepEqual(p.settings,before);assert.deepEqual(p.state.mouse,mouse);evidence.push({gate,get,allMutationsRejected:true});}return evidence;
});
const installActiveDeckOverlay=()=>{p.settings.gameProfiles['C:/games/do-not-edit.exe'].input.persona='steamdeck';p.settings.input.gameOverride={sessionId:'fixture-game-override',gamePath:'C:/games/do-not-edit.exe',outputTarget:{persona:'steamdeck',buttonMappingEnabled:true,gyroEnabled:false},sentinel:{preserve:true}};p.state.boundPersona='steamdeck';p.state.mouse.on=false;p.state.mouse.mode='off';p.settings.gamepad.mouseMode='off';};
const pendingMicrosoft=async(mode='steam')=>{installActiveDeckOverlay();const doc=await fresh();const scope=mode==='global'?doc.global:doc.scenes[mode].overrides;scope.input={...structuredClone(doc.global.input),persona:'dualsense-edge',mouse:'microsoft',outputTarget:{persona:'dualsense-edge'}};stored(doc);const games=gameState(),overlay=structuredClone(p.settings.input.gameOverride),result=await startMode(mode,doc);await new Promise(resolve=>setImmediate(resolve));assert.equal(result.document.current,mode);assert.equal(p.settings.input.outputTarget.persona,'dualsense-edge');assert.deepEqual(p.settings.input.gameOverride,overlay,'Global CAS replaced the active game override');assert.equal(gameState(),games);assert(result.receipt.pending.some(value=>value.includes('鼠标')));assert(!result.receipt.applied.includes('模拟鼠标'));return {...result,games,overlay};};
const removeOverlay=()=>{delete p.settings.input.gameOverride;p.state.boundPersona=null;p.state.buildPersona=null;p.state.mouse.stateKnown=true;p.state.mouse.controlAllowed=true;p.state.mouse.provider='ymcc';p.state.mouse.on=false;p.state.mouse.mode='off';};
const readyMouse={ok:true,provider:'ymcc',controlAllowed:true,stateKnown:true,on:false};
await test('active game-profile SteamDeck delays global Microsoft mouse until native GET confirms the overlay and bound owner are gone, then applies exactly once',async()=>{
 if(process.argv.includes('--deferred-owner-boundary')){
  const observations=path.join(out,'deferred-owner-observations.jsonl');fs.closeSync(fs.openSync(observations,'wx'));const rows=[];
  for(const mode of m.BOOT_MODE_IDS)for(const stage of ['backend','commit'])for(const kind of ['revision','hash','mode','current']){
   b.cancelBootModeDeferredMouse();p.reset();const pending=await pendingMicrosoft(mode);removeOverlay();let boundary,at,context;
   const change=()=>{if(boundary)return;
    if(kind==='revision')p.settings.input.revision++;
    if(kind==='hash')p.settings.input.diagnostics={...(p.settings.input.diagnostics??{}),inputLoggingEnabled:true};
    if(kind==='mode'){p.settings.extensions.bootModes.revision+=5;p.settings.extensions.bootModes.boot=mode==='steam'?'xbox':'steam';}
    boundary=structuredClone(p.settings);at=p.calls.length;
   };
   p.hooks.mouseBackend=value=>{context=structuredClone(value);assert(context?.input,'deferred actual mouse request must carry native boot context');assert.equal(context.input.revision,p.settings.input.revision);if(stage==='backend')change();};
   if(stage==='commit')p.hooks.settingsWrite=draft=>{if(draft.extensions?.bootModes?.activeSnapshot?.input?.mouse==='microsoft')change();};
   try{await p.emitMemoryIpc('mouseMode.changed',readyMouse);}finally{delete p.hooks.mouseBackend;delete p.hooks.settingsWrite;}
   const actual={mode,stage,kind,boundary,after:structuredClone(p.settings),context,afterBoundaryCalls:structuredClone(p.calls.slice(at)),receipt:pending.receipt};fs.appendFileSync(observations,JSON.stringify(actual)+'\n');assert(boundary&&context);
   if(kind==='current'){assert.equal(p.state.mouse.on,true);assert.equal(p.settings.extensions.bootModes.activeSnapshot.input.mouse,'microsoft');assert(pending.receipt.applied.includes('模拟鼠标'));}
   else{assert.deepEqual(p.settings.input,boundary.input);assert.deepEqual(p.settings.extensions,boundary.extensions,'expired deferred save must preserve new owner');assert(!actual.afterBoundaryCalls.some(row=>['mouse.enable','mouse.policy','mouse.backend','input.cas','settings.update'].includes(row.op)),'expired deferred request must not replay old work');assert(!pending.receipt.applied.includes('模拟鼠标'));}
   assert.equal(gameState(),pending.games);assert.equal(p.ipcListenerCount('mouseMode.changed'),0);assert.equal(p.ipcListenerCount('gamepad.mouse-toggle'),0);rows.push({mode,stage,kind,contextForwarded:true});b.cancelBootModeDeferredMouse();
  }return rows;
 }
 const events=[],listener=event=>events.push(structuredClone(event.detail));window.addEventListener('boot-mode:changed',listener);try{const result=await pendingMicrosoft(),initialPending=structuredClone(result.receipt.pending);assert.equal(p.settings.extensions.bootModes.activeSnapshot.input.mouse,null,'Pending native handoff was marked as an applied mouse snapshot');assert.equal(p.calls.filter(c=>c.op==='input.cas').length,1);assert(!p.calls.some(c=>c.op==='mouse.backend'||c.op==='mouse.enable'&&c.value===true));const before=p.calls.length;await p.emitMemoryIpc('mouseMode.changed',readyMouse);assert(!p.calls.slice(before).some(c=>c.op.startsWith('mouse.')),'Event payload bypassed the actual GET Steam guard');delete p.settings.input.gameOverride;await p.emitMemoryIpc('gamepad.mouse-toggle',readyMouse);assert(!p.calls.slice(before).some(c=>c.op.startsWith('mouse.')),'Still-bound SteamDeck owner was ignored');
 const bootBefore={revision:p.settings.extensions.bootModes.revision,boot:p.settings.extensions.bootModes.boot,enabled:p.settings.extensions.bootModes.bootEnabled};removeOverlay();await p.emitMemoryIpc('mouseMode.changed',readyMouse);assert.deepEqual({revision:p.settings.extensions.bootModes.revision,boot:p.settings.extensions.bootModes.boot,enabled:p.settings.extensions.bootModes.bootEnabled},bootBefore);assert.equal(p.settings.extensions.bootModes.activeSnapshot.input.mouse,'microsoft');assert.equal(p.state.mouse.backend,'gamebar');assert.equal(p.state.mouse.on,true);assert.equal(p.state.mouse.provider,'ymcc');assert.equal(p.settings.input.outputTarget.persona,'dualsense-edge');assert.equal(gameState(),result.games);assert.equal(p.calls.filter(c=>c.op==='input.cas').length,1,'Reconnect rewrote the input or game owner');assert.equal(p.calls.filter(c=>c.op==='mouse.backend').length,1);assert.equal(p.calls.filter(c=>c.op==='mouse.enable'&&c.value===true||c.op==='mouse.policy'&&c.value==='on').length,1);
 const settled=p.calls.length,reads=p.state.mouseReads;await p.emitMemoryIpc('gamepad.mouse-toggle',readyMouse);await p.emitMemoryIpc('mouseMode.changed',readyMouse);assert.equal(p.calls.length,settled,'Confirmed handoff was repeated');assert.equal(p.state.mouseReads,reads,'Confirmed handoff kept reading the native provider');const last=events.at(-1);assert.equal(last.mode,'steam');assert(last.applied.includes('模拟鼠标'));assert(!last.pending.some(value=>value.includes('模拟鼠标')));assert(!p.calls.slice(before).some(c=>c.op==='app.launch'||c.op==='steam.launch'||c.op.startsWith('startup.')));return {initialPending,confirmedReceipt:last,events:p.ipcEvents,overlayPreservedUntilNativeRemoval:true,gameProfilesUnchanged:true};}finally{window.removeEventListener('boot-mode:changed',listener);}
});
await test('saving a future mouse/persona draft and choosing a different next boot preserve the original startup deferred handoff',async()=>{
 const initial=await pendingMicrosoft(),draft=structuredClone(initial.document);draft.scenes.steam.overrides.input={...draft.scenes.steam.overrides.input,persona:'elite',mouse:'joyxoff',outputTarget:{...draft.scenes.steam.overrides.input.outputTarget,persona:'elite'}};const count=p.calls.length,saved=await b.saveBootModes(draft),selected=await b.chooseBootMode('xbox',saved,true);assert.equal(selected.current,'steam');assert.equal(selected.boot,'xbox');assert.equal(p.settings.input.outputTarget.persona,'dualsense-edge');assert.equal(p.settings.extensions.bootModes.activeSnapshot.input.persona,'dualsense-edge');assert.equal(p.settings.extensions.bootModes.activeSnapshot.input.mouse,null);assert(p.ipcListenerCount('mouseMode.changed')>0);assert(!p.calls.slice(count).some(c=>c.op==='input.cas'||c.op.startsWith('mouse.')),'Saving a future draft touched current input');const revision=selected.revision;removeOverlay();await p.emitMemoryIpc('mouseMode.changed',readyMouse);
 assert.equal(p.state.mouse.backend,'gamebar');assert.equal(p.state.mouse.on,true);assert.equal(p.settings.input.outputTarget.persona,'dualsense-edge');const stored=p.settings.extensions.bootModes;assert.equal(stored.current,'steam');assert.equal(stored.boot,'xbox');assert.equal(stored.bootEnabled,true);assert.equal(stored.revision,revision);assert.equal(stored.scenes.steam.overrides.input.persona,'elite');assert.equal(stored.scenes.steam.overrides.input.mouse,'joyxoff');assert.equal(stored.activeSnapshot.input.persona,'dualsense-edge');assert.equal(stored.activeSnapshot.input.mouse,'microsoft');assert.equal(p.calls.filter(c=>c.op==='mouse.backend').length,1);assert.equal(p.calls.find(c=>c.op==='mouse.backend').value,'gamebar');assert.equal(gameState(),initial.games);assert.equal(p.ipcListenerCount('mouseMode.changed'),0);assert.equal(p.ipcListenerCount('gamepad.mouse-toggle'),0);return {currentPersona:'dualsense-edge',currentMouse:'microsoft',savedFuturePersona:'elite',savedFutureMouse:'joyxoff',nextBoot:'xbox',revision,gameProfilesUnchanged:true};
});
await test('terminal deferred mouse failure preserves requested input and unknown applied snapshot across reopen, releases listeners and never retries on late events',async()=>{
 const result=await pendingMicrosoft(),before={revision:p.settings.extensions.bootModes.revision,current:p.settings.extensions.bootModes.current,boot:p.settings.extensions.bootModes.boot,enabled:p.settings.extensions.bootModes.bootEnabled};removeOverlay();p.faults['mouse.backend']=1;await p.emitMemoryIpc('mouseMode.changed',readyMouse);assert.equal(p.state.mouse.on,false);assert.equal(p.settings.extensions.bootModes.activeSnapshot.input.mouse,null);assert.equal(p.settings.extensions.bootModes.scenes.steam.overrides.input.mouse,'microsoft');assert.deepEqual({revision:p.settings.extensions.bootModes.revision,current:p.settings.extensions.bootModes.current,boot:p.settings.extensions.bootModes.boot,enabled:p.settings.extensions.bootModes.bootEnabled},before);assert.equal(p.ipcListenerCount('mouseMode.changed'),0);assert.equal(p.ipcListenerCount('gamepad.mouse-toggle'),0);const reopened=await b.loadBootModes();assert.equal(m.resolveBootMode(reopened.document,'steam').input.mouse,'microsoft');assert.equal(reopened.document.activeSnapshot.input.mouse,null);assert.equal(gameState(),result.games);assert(!result.receipt.applied.includes('模拟鼠标'));assert(result.receipt.pending.some(value=>value.includes('injected:mouse.backend')));const count=p.calls.length,reads=p.state.mouseReads;await p.emitMemoryIpc('mouseMode.changed',readyMouse);assert.equal(p.calls.length,count);assert.equal(p.state.mouseReads,reads);return {requestedMouse:'microsoft',activeMouse:null,terminalReceipt:result.receipt,lateRetryWrites:0};
});
await test('overlapping native mouse events serialize one provider read and one confirmed backend/start transaction',async()=>{
 const result=await pendingMicrosoft();removeOverlay();let release,entered;const waiting=new Promise(resolve=>entered=resolve),gate=new Promise(resolve=>release=resolve);p.hooks.mouseRead=async()=>{delete p.hooks.mouseRead;entered();await gate;};const count=p.calls.length,first=p.emitMemoryIpc('mouseMode.changed',readyMouse);let second,third;
 try{await waitSignal(waiting,'serialized deferred native GET');const reads=p.state.mouseReads;second=p.emitMemoryIpc('gamepad.mouse-toggle',readyMouse);third=p.emitMemoryIpc('mouseMode.changed',readyMouse);await new Promise(resolve=>setImmediate(resolve));assert.equal(p.state.mouseReads,reads,'Duplicate native events started concurrent GET operations');assert.equal(p.calls.length,count,'A second event bypassed the in-flight provider gate');release();await Promise.all([first,second,third]);await new Promise(resolve=>setImmediate(resolve));assert.equal(p.state.mouse.backend,'gamebar');assert.equal(p.state.mouse.on,true);assert.equal(p.calls.filter(c=>c.op==='mouse.backend').length,1);assert.equal(p.calls.filter(c=>c.op==='mouse.enable'&&c.value===true||c.op==='mouse.policy'&&c.value==='on').length,1);assert.equal(gameState(),result.games);return {concurrentEvents:3,confirmedBackendWrites:1,confirmedStarts:1,gameProfilesUnchanged:true};}finally{release();delete p.hooks.mouseRead;await Promise.allSettled([first,second,third].filter(Boolean));}
});
await test('a new mode cancels an older deferred Microsoft handoff before provider-ready events can restart it',async()=>{
 const first=await pendingMicrosoft(),next=await startMode('global',first.document);assert.equal(next.document.current,'global');const before=p.calls.length;removeOverlay();await p.emitMemoryIpc('mouseMode.changed',readyMouse);await p.emitMemoryIpc('gamepad.mouse-toggle',readyMouse);assert.equal(p.settings.extensions.bootModes.current,'global');assert.equal(p.state.mouse.on,false);assert(!p.calls.slice(before).some(c=>c.op==='mouse.backend'||c.op==='mouse.enable'&&c.value===true||c.op==='mouse.policy'&&c.value==='on'));assert.equal(gameState(),first.games);return {newCurrent:next.document.current,lateOldHandoffWrites:0};
});
for(const fence of ['generation','current-mode','current-persona'])await test('deferred mouse reconnect rejects stale '+fence+' before any native mutation',async()=>{
 const result=await pendingMicrosoft();removeOverlay();if(fence==='generation')p.invalidate();if(fence==='current-mode')p.settings.extensions.bootModes.current='xbox';if(fence==='current-persona')p.settings.input.outputTarget.persona='disabled';const before=structuredClone(p.settings),count=p.calls.length;await p.emitMemoryIpc('mouseMode.changed',readyMouse);await p.emitMemoryIpc('gamepad.mouse-toggle',readyMouse);assert.deepEqual(p.settings,before);assert.equal(p.calls.length,count,'Stale reconnect mutated native IO or settings');assert.equal(p.state.mouse.on,false);assert.equal(gameState(),result.games);
});
await test('generation changes while deferred native GET is in flight prevent writing to the new settings path',async()=>{
 await pendingMicrosoft();removeOverlay();let release,entered;const waiting=new Promise(resolve=>entered=resolve),gate=new Promise(resolve=>release=resolve);p.hooks.mouseRead=async()=>{delete p.hooks.mouseRead;entered();await gate;};const attempt=p.emitMemoryIpc('mouseMode.changed',readyMouse);await waitSignal(waiting,'deferred native GET');const count=p.calls.length;p.invalidate();const before=structuredClone(p.settings);release();await attempt;await new Promise(resolve=>setImmediate(resolve));assert.deepEqual(p.settings,before);assert.equal(p.calls.length,count);assert.equal(p.state.mouse.on,false);
});
await test('new current mode cancels a deferred native GET already in flight before it can select the old backend',async()=>{
 const result=await pendingMicrosoft();removeOverlay();let release,entered;const waiting=new Promise(resolve=>entered=resolve),gate=new Promise(resolve=>release=resolve);p.hooks.mouseRead=async()=>{delete p.hooks.mouseRead;entered();await gate;};const attempt=p.emitMemoryIpc('mouseMode.changed',readyMouse);await waitSignal(waiting,'deferred native GET');const count=p.calls.length,nextRequest=startMode('global',result.document);release();await attempt;const next=await nextRequest;await new Promise(resolve=>setImmediate(resolve));assert.equal(next.document.current,'global');assert(!p.calls.slice(count).some(c=>c.op==='mouse.backend'&&c.value==='gamebar'||c.op==='mouse.enable'&&c.value===true||c.op==='mouse.policy'&&c.value==='on'),'Queued newer apply allowed the older deferred callback to start Microsoft mouse first');assert.equal(p.state.mouse.on,false);assert.equal(p.settings.extensions.bootModes.current,'global');
});
await test('rejected non-startup request cannot cancel a legitimate startup deferred handoff or change its saved mode',async()=>{
 const result=await pendingMicrosoft(),settings=structuredClone(p.settings),native=structuredClone(p.state),count=p.calls.length,listeners={changed:p.ipcListenerCount('mouseMode.changed'),toggle:p.ipcListenerCount('gamepad.mouse-toggle')};assert(listeners.changed>0&&listeners.toggle>0);await assert.rejects(()=>b.applyBootMode('global',result.document),/开机|重启|startup/i);assert.deepEqual(p.settings,settings);assert.deepEqual(p.state,native);assert.equal(p.calls.length,count);assert.deepEqual({changed:p.ipcListenerCount('mouseMode.changed'),toggle:p.ipcListenerCount('gamepad.mouse-toggle')},listeners);removeOverlay();await p.emitMemoryIpc('mouseMode.changed',readyMouse);assert.equal(p.state.mouse.backend,'gamebar');assert.equal(p.state.mouse.on,true);assert.equal(p.settings.extensions.bootModes.current,'steam');return {rejectedHotApply:true,startupDeferredHandoffPreserved:true};
});
await test('explicit deferred-mouse cancellation removes native event ownership and makes late events inert without writing settings',async()=>{
 assert.equal(typeof b.cancelBootModeDeferredMouse,'function');await pendingMicrosoft();assert(p.ipcListenerCount('mouseMode.changed')>0);assert(p.ipcListenerCount('gamepad.mouse-toggle')>0);const count=p.calls.length,reads=p.state.mouseReads;b.cancelBootModeDeferredMouse();b.cancelBootModeDeferredMouse();assert.equal(p.ipcListenerCount('mouseMode.changed'),0);assert.equal(p.ipcListenerCount('gamepad.mouse-toggle'),0);removeOverlay();const settings=structuredClone(p.settings);await p.emitMemoryIpc('mouseMode.changed',readyMouse);await p.emitMemoryIpc('gamepad.mouse-toggle',readyMouse);assert.deepEqual(p.settings,settings);assert.equal(p.calls.length,count);assert.equal(p.state.mouseReads,reads);assert.equal(p.state.mouse.on,false);return {repeatedCancelSafe:true,nativeListenersAfterCancel:0,lateWrites:0};
});
await test('retired mode shortcuts never call the native shortcut writer, even when its acknowledgement would fail',async()=>{
 const doc=await fresh(),games=gameState(),flags=structuredClone(p.settings.gamepad);doc.scenes.steam.overrides.controller={name:'retired',rules:[{id:'mode',enabled:true}]};stored(doc);p.faults['summon.ack-mismatch']=1;
 await startMode('steam',doc);assert(!opNames().includes('summon.set'));assert.deepEqual(p.settings.gamepad,flags);assert.equal(gameState(),games);assert(!('controller' in p.settings.extensions.bootModes.scenes.steam.overrides));
});

await test('saved-only editor opens without touching stalled CPU, charge, mouse or sleep owners',async()=>{
 const doc=await fresh();stored(doc);const before=structuredClone(p.settings);let deviceReads=0;
 for(const key of ['cpuArchitectureRead','tdpCapabilityRead','chargeRead','hibernateRead','mouseRead'])p.hooks[key]=()=>{deviceReads++;throw Error('editor must not probe '+key);};
 const context=await b.loadBootModes({editorOnly:true});assert.equal(deviceReads,0);assert.deepEqual(p.settings,before);assert.deepEqual(context.support,{});assert.equal(context.document.boot,doc.boot);
 return {deviceReads,settingsUnchanged:true};
});
await test('category support reads only that category and never changes presets',async()=>{
 let cpu=0,tdp=0,charge=0,hibernate=0;p.hooks.cpuArchitectureRead=()=>{cpu++;};p.hooks.tdpCapabilityRead=()=>{tdp++;};p.hooks.chargeRead=()=>{charge++;};p.hooks.hibernateRead=()=>{hibernate++;};const before=structuredClone(p.settings);
 const performance=await b.readBootModeSupport('performance');assert.deepEqual([cpu,tdp,charge,hibernate],[1,1,1,0]);assert('charge' in performance);assert(!('hibernate' in performance));
 const sleep=await b.readBootModeSupport('sleep');assert.deepEqual([cpu,tdp,charge,hibernate],[1,1,1,1]);assert.deepEqual(Object.keys(sleep),['hibernate']);
 const steam=await b.readBootModeSupport('steam');assert.deepEqual(steam,{});assert.deepEqual([cpu,tdp,charge,hibernate],[1,1,1,1]);assert.deepEqual(p.settings,before);
 return {cpu,tdp,charge,hibernate,unrelatedReads:0};
});
await test('one complete capture shares charge and hibernate observations across the support branch',async()=>{
 let charge=0,hibernate=0;p.hooks.chargeRead=()=>{charge++;};p.hooks.hibernateRead=()=>{hibernate++;};let release;const gate=new Promise(resolve=>release=resolve);p.hooks.settingsRead=()=>gate;
 const pending=b.loadBootModes();await new Promise(resolve=>setImmediate(resolve));release();await pending;assert.equal(charge,1);assert.equal(hibernate,1);
 return {charge,hibernate,scope:'single capture; no retained device cache'};
});
await test('saved-only editor retains unapplied default OFF and capability failure remains unknown',async()=>{
 const doc=await fresh();doc.current='xbox-steam';doc.boot='xbox-steam';doc.bootEnabled=true;stored(doc);const desired=m.resolveBootMode(doc,'xbox-steam').input.persona;
 const editor=await b.loadBootModes({editorOnly:true});assert.equal(m.resolveBootMode(editor.document,'xbox-steam').input.persona,desired);assert.equal(desired,'disabled');
 p.hooks.cpuArchitectureRead=()=>{throw Error('unavailable CPU observation');};const support=await b.readBootModeSupport('performance');assert.equal(support.hybridCores.state,'unknown');
 return {pendingPersona:desired,cpuState:support.hybridCores.state};
});

await test('malformed known module data is rejected before rendering, saving or preparing any of four modes',async()=>{
 const cases=[['performance',{controls:null}],['performance',{controls:{ac:{auto:[]}}}],['performance',{frames:{dc:42}}],['gyro',{motion:42}],['gyro',{motion:{presets:[]}}],['softwareLaunch',{paths:{bad:'path'}}],['softwareLaunch',{paths:[null,123]}],['monitor',{preferences:null}],['monitor',{preferences:{ymcc:[]}}],['touch',{profiles:{disabled:42}}],['input',{outputTarget:null}],['input',{persona:'invalid'}],['sleep',{config:[]}],['sleep',{timeouts:'bad'}],['steamAppearance',{states:['bad']}],['background',{ui:false}],['display',{brightness:'bright'}],['charge',{limit:'80'}],['audioOptimization',{enabled:{}}],['frameGeneration',{enabled:[]}],['steamWallpaper',{cpuThreshold:'fast'}],['steamDecky',{enabled:1}]];
 let checked=0;for(const id of m.BOOT_MODE_IDS)for(const [module,value]of cases){
  p.reset();const doc=await fresh();if(id==='global')doc.global[module]=structuredClone(value);else doc.scenes[id].overrides[module]=structuredClone(value);stored(doc);const before=structuredClone(p.settings);
  for(const options of [{overviewOnly:true},{editorOnly:true}])await assert.rejects(b.loadBootModes(options),/开机模式数据格式异常/);
  await assert.rejects(b.saveBootModes(doc),/开机模式数据格式异常/);await assert.rejects(b.chooseBootMode(id,doc),/开机模式数据格式异常/);
  assert.deepEqual(p.settings,before);assert.equal(changes().length,0,'invalid config caused an OS/device write');checked++;
 }return {modes:4,malformedCases:checked,operationsPerCase:4,sourcePreserved:true,externalWrites:0};
});
await test('malformed hidden remembered presets, invalid metadata and future boot schema cannot be silently reset',async()=>{
 const cases=[d=>d.scenes.steam.rememberedOverrides={gyro:{motion:[]}},d=>d.presets.xbox={saved:{name:'saved',overrides:{softwareLaunch:{paths:{}}}}},d=>d.presets.xbox={saved:{name:'saved',overrides:{},rememberedOverrides:{gyro:{motion:[]}}}},d=>d.scenes=[],d=>d.global=null,d=>d.activeSnapshot=[],d=>d.current='unknown',d=>d.boot='unknown',d=>d.bootEnabled='true',d=>d.revision=-1,d=>d.schema=99];
 for(const mutate of cases){p.reset();const doc=await fresh();mutate(doc);stored(doc);const before=structuredClone(p.settings);await assert.rejects(b.loadBootModes({overviewOnly:true}));await assert.rejects(b.saveBootModes(doc));await assert.rejects(b.chooseBootMode('steam',doc));const startup=await runtimeBundle(out);startup.ports.settings.extensions.bootModes=structuredClone(doc);const startupBefore=structuredClone(startup.ports.settings);await assert.rejects(startup.boot.restoreSelectedBootMode());assert.deepEqual(startup.ports.settings,startupBefore);assert.deepEqual(p.settings,before);assert.equal(changes().length,0);}
 return {malformedDocuments:cases.length,originalsPreserved:true,noBootClaim:true};
});
await test('runtime settings fallback cannot masquerade as durable boot configuration or claim startup',async()=>{
 for(const reason of ['unsupported','read-failed','write-failed','corrupt']){
  p.reset();const doc=await fresh();stored(doc);const before=structuredClone(p.settings);p.hooks.recoveryState={status:'runtime-fallback',persisted:reason==='read-failed',message:'injected '+reason};
  for(const options of [{overviewOnly:true},{editorOnly:true},{}])await assert.rejects(b.loadBootModes(options),/尚未安全读取/);
  await assert.rejects(b.saveBootModes(doc),/尚未安全读取/);await assert.rejects(b.chooseBootMode('steam',doc),/尚未安全读取/);const startup=await runtimeBundle(out);startup.ports.settings.extensions.bootModes=structuredClone(doc);startup.ports.hooks.recoveryState=structuredClone(p.hooks.recoveryState);const startupBefore=structuredClone(startup.ports.settings);await assert.rejects(startup.boot.restoreSelectedBootMode(),/尚未安全读取/);assert.deepEqual(startup.ports.settings,startupBefore);
  assert.deepEqual(p.settings,before);assert.equal(changes().length,0);
 }return {fallbackReasons:4,operationsPerReason:6,noWrites:true};
});
await test('revision overflow and invalid requested mode cannot commit settings or startup side effects',async()=>{
 const doc=await fresh();doc.revision=Number.MAX_SAFE_INTEGER;stored(doc);const before=structuredClone(p.settings);
 await assert.rejects(b.saveBootModes(doc),/版本号/);await assert.rejects(b.chooseBootMode('steam',doc),/版本号/);await assert.rejects(b.chooseBootMode('__proto__',doc),/选择无效/);assert.deepEqual(p.settings,before);assert.equal(changes().length,0);
});

await test('modes 1 and 2 force advanced .earlystart OFF, modes 3 and 4 force it ON across all inverse selections',async()=>{
 const rows=[];for(const from of m.BOOT_MODE_IDS)for(const to of m.BOOT_MODE_IDS){
  p.reset();const doc=await fresh();stored(doc);const before=structuredClone(p.settings.input);const first=await b.chooseBootMode(from,doc);const next=await b.chooseBootMode(to,first);const advanced=to==='steam'||to==='xbox-steam';
  assert.equal(p.state.steam,advanced);assert.equal(p.state.earlyCommand.length>0,advanced);assert.equal(next.boot,to);assert.deepEqual(p.settings.input,before);assert.equal(next.bootNotice,undefined);assert(!opNames().includes('steam.launch'));rows.push({from,to,advanced,currentInputUnchanged:true});
 }return rows;
});
await test('failed advanced close/open remains pending and explicit same-mode retry repairs all four choices',async()=>{
 const rows=[];for(const id of m.BOOT_MODE_IDS){p.reset();const doc=await fresh();stored(doc);const target=id==='steam'||id==='xbox-steam';p.state.steam=!target;p.state.earlyCommand=target?'':'stale-command';const input=structuredClone(p.settings.input);p.faults['startup.steam']=1;
  const failed=await b.chooseBootMode(id,doc);assert.equal(failed.boot,id);assert.equal(p.state.steam,!target);assert.match(failed.bootNotice,/Steam 高级启动.*injected/);
  const retried=await b.chooseBootMode(id,failed);assert.equal(retried.bootNotice,undefined);assert.equal(p.state.steam,target);assert.equal(p.state.earlyCommand.length>0,target);assert.deepEqual(p.settings.input,input);rows.push({id,failedNoticeRetained:true,retryConfirmed:true});
 }return rows;
});
await test('queued disable then enable follows latest mode and does not overlap .earlystart writes',async()=>{
 const doc=await fresh();stored(doc);let release,entered;const held=new Promise(r=>release=r),started=new Promise(r=>entered=r);let calls=0;
 p.hooks.steamStartupWrite=async()=>{if(++calls===1){entered();await held;}};
 const close=b.chooseBootMode('global',doc);await waitSignal(started,'advanced close');const next=close.then(saved=>b.chooseBootMode('steam',saved));assert.equal(calls,1);release();const saved=await next;assert.equal(saved.boot,'steam');assert.equal(p.state.steam,true);assert.deepEqual(p.calls.filter(c=>c.op==='startup.steam').map(c=>c.value),[false,true]);
 return {serialWrites:2,finalMode:'steam',restarts:0};
});
if(!caseFromReached)report.assertions.push({name:'requested exact --case-from was not registered',pass:false,error:'Unknown exact original case name: '+caseFrom});
for(const name of exactCases)if(!report.assertions.some(row=>row.name===name))report.assertions.push({name,pass:false,error:notRun.includes(name)?'Selected case not reached after failure':'Unknown exact --case name'});
report.pass=report.assertions.length>0&&report.assertions.every(a=>a.pass);report.counts={passed:report.assertions.filter(a=>a.pass).length,failed:report.assertions.filter(a=>!a.pass).length};
fs.writeFileSync(path.join(out,'boot-modes-runtime.json'),JSON.stringify(report,null,2));console.log(JSON.stringify({pass:report.pass,...report.counts,report:path.join(out,'boot-modes-runtime.json'),failed:report.assertions.filter(a=>!a.pass).map(a=>({name:a.name,error:a.error.split('\n')[0]}))},null,2));if(!report.pass)process.exitCode=1;

