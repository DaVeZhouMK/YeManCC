// Explicit extension of performance_entry_parity_selftest; no parent gate or hardware access.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {createPerformanceMemoryFixture as fixture,evaluatePerformanceModule as evaluate,models,frames} from '../performance_memory_fixture.mjs';
const require=createRequire(path.resolve('package.json')),ts=require('typescript');
const read=file=>fs.readFileSync(file,'utf8'),clone=x=>structuredClone(x),plain=x=>JSON.parse(JSON.stringify(x));
const schemes=['yeman','besteff','bal','bestperf'],sides=['ac','dc'];
const tracked=[
 'tools/performance_entry_parity_selftest.mjs','tools/fixtures/performance_memory_fixture.mjs','tools/fixtures/performance_power_schemes/verify.mjs',
 'src/bridge/performanceControls.ts','src/bridge/frameRateModel.ts','src/bridge/controllerPersona.ts','src/bridge/settingsSnapshot.ts',
 'src/bridge/performanceSchedule.ts','src/bridge/gamePolicyRuntime.ts','src/bridge/deckyPerformanceActions.ts','decky-plugin/src/performanceModel.ts',
 'src/App.vue','src/bridge/yeman.ts','src/bridge/quickActionLock.ts','src/bridge/bootModes.ts','src/bridge/bootModeModel.ts','src/bridge/bootModeSharedShortcuts.ts','src/bridge/screenTouchpadDefaults.ts',
];
const hash=file=>({path:path.resolve(file),bytes:fs.statSync(file).size,sha256:createHash('sha256').update(fs.readFileSync(file)).digest('hex')});
const identity=()=>tracked.map(hash);
const mirror=evaluate(read('decky-plugin/src/performanceModel.ts'));
const adapter=evaluate(read('src/bridge/deckyPerformanceActions.ts'),{'../../decky-plugin/src/performanceModel':mirror,'./performanceControls':models});
function gateway(f,onUpdate){
 const lock=evaluate(read('src/bridge/quickActionLock.ts'));
 const actual=adapter.createPerformanceMirrorActions({allowed:()=>true,read:()=>f.schedule.getContinuousPerformanceSnapshot(),
  update:(...args)=>{onUpdate?.(...args);return f.schedule.updateContinuousPerformanceControls(...args)},
  power:async()=>({phase:'ready',hardwareWritesAllowed:true,generation:1}),acquire:lock.tryAcquireQuickAction});
 return {async send(patch,snapshot){const current=snapshot??await f.schedule.getContinuousPerformanceSnapshot();return actual.execute({patch},{generation:1,controlAdmission:{performance:adapter.performanceMirrorSource(current)},checkpoint(){}})}};
}
async function edit(f,patch,scope='effective',side){const s=await f.schedule.getContinuousPerformanceSnapshot(scope,side);return f.schedule.updateContinuousPerformanceControls(s.identity,patch)}
function seedPair(f,pair){f.settings.performanceSchedule.powerSchemes=clone(pair)}
function clear(f){f.effects.length=0;f.hardwareWrites.length=0;f.schemeAttempts.length=0;f.corePolicyCalls.length=0}
function counts(f){return {tdp:f.hardwareWrites.filter(w=>w.kind==='tdp').length,cpu:f.hardwareWrites.filter(w=>w.kind==='cpu').length,core:f.hardwareWrites.filter(w=>w.kind==='core').length,
 schemeAttempts:clone(f.schemeAttempts),schemeWrites:f.hardwareWrites.filter(w=>w.kind==='selected-scheme'),floating:f.floating,ownerSide:f.ownerSide,activeScheme:f.activeScheme,effects:clone(f.effects),tasks:[...f.tasks.keys()],corePolicy:clone(f.corePolicyCalls)}}
function noCpuTdp(f){assert.equal(f.hardwareWrites.filter(w=>['cpu','tdp','core','active-scheme'].includes(w.kind)).length,0);assert(!f.effects.includes('start-float'))}
async function settle(f,retry=false){for(let i=0;i<1800;i++)await Promise.resolve();if(retry)for(const run of [...f.tasks.values()])await run();for(let i=0;i<1800;i++)await Promise.resolve()}
function bootModel(){
 const snapshot=evaluate(read('src/bridge/settingsSnapshot.ts'),{'vue':require('vue')});
 const shortcuts=evaluate(read('src/bridge/bootModeSharedShortcuts.ts'),{'./settingsSnapshot':snapshot});
 const touch=evaluate(read('src/bridge/screenTouchpadDefaults.ts'));
 return evaluate(read('src/bridge/bootModeModel.ts'),{'./bootModeSharedShortcuts':shortcuts,'./screenTouchpadDefaults':touch});
}
function bootConsumer(f,extractions){
 const file='src/bridge/bootModes.ts',source=read(file),ast=ts.createSourceFile(file,source,ts.ScriptTarget.Latest,true);
 const fn=ast.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='applyRuntimeModule');assert(fn,'actual Boot applyRuntimeModule');
 const text=fn.getText(ast),start=fn.getStart(ast),end=fn.end;
 extractions.push({file,function:'applyRuntimeModule',start,end,sha256:createHash('sha256').update(text).digest('hex'),selection:'Complete original function; only id=performance invoked. All other module branches NOT_RUN.'});
 return evaluate(text+'\nexport {applyRuntimeModule};',{}, {
  normalizeControlPowerSchemes:models.normalizeControlPowerSchemes,normalizeContinuousControls:models.normalizeContinuousControls,frameRatePair:frames.frameRatePair,
  applyPerformanceConfiguration:f.schedule.applyPerformanceConfiguration,LABELS:{performance:'performance'},
 }).applyRuntimeModule;
}

function extractedNode(file,ast,node,extractions,label,offset=0){
 const text=node.getText(ast),start=node.getStart(ast)+offset,end=node.end+offset;
 const raw=read(file);assert.equal(raw.slice(start,end),text,'exact extracted source bytes');
 extractions.push({file,function:label,text,start,end,startLine:raw.slice(0,start).split('\n').length,sha256:createHash('sha256').update(text).digest('hex'),selection:'Actual complete AST node; dependency terminals are memory only.'});return text;
}
function actualPowerReconcile(f,ports,extractions){
 const file='src/bridge/yeman.ts',ast=ts.createSourceFile(file,read(file),ts.ScriptTarget.Latest,true);
 const variables=['PW','SCHEMES','powerSchemeEnsureQueue','schemeProtectionSuspendUntil','schemeProtectionSuspendMode'];
 const names=['isGuid','readRememberedPowerScheme','rememberYemanScheme','rememberManualScheme','ensureRememberedYemanSchemeActive','reconcileRememberedPowerScheme'];
 const selected=[...variables,...names].map(name=>{const node=ast.statements.find(node=>ts.isFunctionDeclaration(node)?node.name?.text===name:ts.isVariableStatement(node)&&node.declarationList.declarations.some(d=>d.name.getText(ast)===name));assert(node,'actual power declaration '+name);return extractedNode(file,ast,node,extractions,name)});
 const terminal=f.powerTerminals;let actual;
 const realActive=()=>f.activeScheme==='test-yeman'?actual.PW.YEMAN:['test-bal','test-besteff','test-bestperf'].includes(f.activeScheme)?actual.PW.WIN_BAL:f.activeScheme;
 actual=evaluate(selected.join('\n')+'\nexport {readRememberedPowerScheme,rememberManualScheme};',{}, {
  readSettingsSection:f.settingsRepository.readSettingsSection,saveSettingsSection:f.settingsRepository.saveSettingsSection,
  ensureYemanScheme:async()=>{f.effects.push('ensure-yeman-base')},getActiveScheme:async()=>realActive(),
  setActiveScheme:async guid=>terminal.setActiveScheme(guid===actual.PW.YEMAN?'test-yeman':guid===actual.PW.WIN_BAL?'test-bal':guid),
  setOverlay:async key=>{f.effects.push('overlay-'+key)},
 });
 terminal.ensureRememberedYemanSchemeActive=actual.ensureRememberedYemanSchemeActive;
 ports.onSelectedScheme=async key=>key==='yeman'?actual.rememberYemanScheme('yeman'):actual.rememberManualScheme(key,actual.PW.WIN_BAL);
 return actual;
}
function appStartupConsumer(f,{restoreBoot,onStage,powerGate=async()=>true},extractions){
 const file='src/App.vue',raw=read(file),match=/<script setup[^>]*>([\s\S]*?)<\/script>/.exec(raw);assert(match,'actual App setup script');
 const offset=raw.indexOf(match[1]),ast=ts.createSourceFile(file,match[1],ts.ScriptTarget.Latest,true);
 const hasCall=(node,name)=>{let found=false;const visit=n=>{if(ts.isCallExpression(n)&&n.expression.getText(ast)===name)found=true;ts.forEachChild(n,visit)};visit(node);return found};
 const mount=ast.statements.find(node=>ts.isExpressionStatement(node)&&ts.isCallExpression(node.expression)&&node.expression.expression.getText(ast)==='onMounted'&&node.expression.arguments.some(arg=>ts.isArrowFunction(arg)&&hasCall(arg,'startGamePolicyRuntime')));
 assert(mount,'actual asynchronous App mounted owner');const body=mount.expression.arguments.find(ts.isArrowFunction).body;assert(ts.isBlock(body));
 const start=body.statements.find(node=>ts.isExpressionStatement(node)&&node.expression.getText(ast)==='startGamePolicyRuntime()');
 const boot=body.statements.find(node=>ts.isIfStatement(node)&&hasCall(node,'restoreSelectedBootMode'));
 const subscribe=body.statements.find(node=>ts.isExpressionStatement(node)&&node.getText(ast).startsWith('stopSchemeWatch = on('));
 const fallback=body.statements.find(node=>(ts.isIfStatement(node)||ts.isTryStatement(node))&&node.getText(ast).includes('const schemeOwnerAtBoot ='));
 assert(start&&boot&&subscribe&&fallback,'complete actual App startup performance boundaries');
 const receiptNodes=body.statements.filter(node=>node.pos<start.pos&&(ts.isVariableStatement(node)&&node.declarationList.declarations.some(d=>['startupPerformanceIntent','startupPerformanceResumeGeneration'].includes(d.name.getText(ast)))||hasCall(node,'onContinuousPerformanceApplied')));
 assert.equal(receiptNodes.length,3,'actual listener declarations and installation before runtime start');
 const rootNodes=['stopSchemeWatch','stopStartupPerformanceWatch','latestResumeGeneration'].map(name=>{const node=ast.statements.find(node=>ts.isVariableStatement(node)&&node.declarationList.declarations.some(d=>d.name.getText(ast)===name));assert(node,'actual App root variable '+name);return node});
 const unmount=ast.statements.find(node=>ts.isExpressionStatement(node)&&ts.isCallExpression(node.expression)&&node.expression.expression.getText(ast)==='onUnmounted'&&hasCall(node,'stopStartupPerformanceWatch'));
 assert(unmount,'actual App unmount fallback');const cleanup=unmount.expression.arguments.find(ts.isArrowFunction).body.statements.filter(node=>node.getText(ast).startsWith('stopStartupPerformanceWatch'));
 assert.equal(cleanup.length,2,'actual unmount unsubscribe and clear');
 const extract=(node,label)=>extractedNode(file,ast,node,extractions,label,offset);
 const roots=rootNodes.map(node=>extract(node,'App.root-lifecycle-variable')).join('\n'),receipts=receiptNodes.map(node=>extract(node,'App.startup-receipt-listener')).join('\n'),dispose=cleanup.map(node=>extract(node,'App.unmount-receipt-cleanup')).join('\n');
 const nodes=[start,boot,subscribe,fallback].map((node,index)=>extract(node,['App.runtime-start','App.boot-call','App.scheme-subscribe','App.startup-fallback'][index]));
 const events=new Map(),timers=[],warnings=[],restores=[],fallbacks=[],reconciles=[],watch={subscribed:0,disposed:0,active:0,notifications:0,invalidations:0};
 const module=evaluate(`${roots}\nexport async function run(){${receipts}\n${nodes[0]}\nawait onStage('runtime-started');\n${nodes[1]}\nawait onStage('boot-returned');\n${nodes[2]}\nawait onStage('before-app-fallback');\n${nodes[3]}\nawait onStage('app-fallback-returned');}\nexport function advanceResumeGeneration(){latestResumeGeneration++;}\nexport function unmountWatch(){${dispose}}\nexport function stop(){stopSchemeWatch?.();${dispose}}`,{}, {
  startGamePolicyRuntime:f.runtime.startGamePolicyRuntime,restoreSelectedBootMode:restoreBoot,onStage,
  onContinuousPerformanceApplied:(callback,onInvalidated)=>{watch.subscribed++;watch.active++;const stop=f.schedule.onContinuousPerformanceApplied(key=>{watch.notifications++;callback(key)},()=>{watch.invalidations++;onInvalidated?.()});let active=true;return()=>{if(active){active=false;watch.active--;watch.disposed++;stop()}}},
  aiFanMockActive:false,aiCpuIsolated:false,powerLifecycleReadyForWrites:powerGate,
  on:(name,callback)=>{events.set(name,callback);return()=>events.delete(name)},scheduleSleepPowerPlanOptimization(){},globalRefreshKey:{value:0},
  reconcileRememberedPowerScheme:async()=>{const owner=await f.actualReconcile.reconcileRememberedPowerScheme();reconciles.push(owner);return owner},
  restorePerformanceScheduleIfConfigured:async(...args)=>{const entry={options:Object.fromEntries(Object.entries(args[1]??{}).map(([key,value])=>[key,typeof value==='function'?'[live callback]':clone(value)])),intentAtCall:args[1]?.completedStartupIntent?.()??null};restores.push(entry);try{const result=await f.schedule.restorePerformanceScheduleIfConfigured(...args);entry.result=result;return result}catch(error){entry.error=String(error);throw error}},
  window:{setTimeout:(run,ms)=>{timers.push({run,ms});return timers.length}},refreshPerformanceScheduleCoreMode:async()=>{throw Error('timer NOT_RUN')},
  applyProgramControls:async()=>{fallbacks.push('legacy-no-owner-fallback')},console:{warn:(...args)=>warnings.push(args.map(String)),log:console.log},
 });
 return {...module,events,timers,warnings,restores,fallbacks,reconciles,watch};
}

async function applyBoot(f,apply,value){const receipt={pending:[],skipped:[],applied:[]};await apply('performance',value,{},receipt);assert.deepEqual(receipt.applied,['performance']);return receipt}
export async function verifyPowerSchemes({output}={}){
 const started=performance.now(),sourceBefore=identity(),cases=[],extractions=[],observations=[];
 const check=async(id,run)=>{assert(!cases.some(c=>c.id===id),'duplicate selected case ID: '+id);const start=performance.now();try{await run();cases.push({id,pass:true,executedThisRun:true,elapsedMs:performance.now()-start})}catch(error){cases.push({id,pass:false,executedThisRun:true,elapsedMs:performance.now()-start,error:String(error.stack||error)})}};
 for(const side of sides)await check('defaults/'+side,async()=>{
  const f=fixture({side}),s=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(s.powerScheme,'yeman');assert.equal('scheme' in s.bank.manual,false);
  assert.deepEqual(plain((await f.schedule.loadPerformanceSchedule()).powerSchemes),{ac:'yeman',dc:'yeman'});assert.equal(f.hardwareWrites.length,0);
  f.settings.performanceSchedule.powerSchemes={ac:'invalid',dc:'bestperf'};
  assert.deepEqual(plain((await f.schedule.loadPerformanceSchedule()).powerSchemes),{ac:'yeman',dc:'bestperf'});
 });
 for(const dedicated of [false,true])for(const side of sides)for(const scheme of schemes)await check(`standalone/${dedicated?'dedicated':'global'}/${side}/${scheme}`,async()=>{
  const f=fixture({side,dedicated});seedPair(f,{ac:'besteff',dc:'bestperf'});if(scheme===(side==='ac'?'besteff':'bestperf'))f.settings.performanceSchedule.powerSchemes[side]='bal';
  const before=await f.schedule.getContinuousPerformanceSnapshot(),custom=plain(await f.schedule.loadGameCustomConfig()),banks=clone(before.controls),pair=clone(f.settings.performanceSchedule.powerSchemes),events=[];
  const stop=f.schedule.onPerformanceScheduleChanged(c=>events.push(clone(c)));events.length=0;clear(f);
  const result=await f.schedule.updateContinuousPerformanceControls(before.identity,{powerScheme:scheme});
  assert.deepEqual(plain(f.settings.performanceSchedule.powerSchemes),{...pair,[side]:scheme});assert.equal(result.powerScheme,scheme);assert.notEqual(result.identity.revision,before.identity.revision);
  assert.deepEqual(plain(result.controls),plain(banks));assert.deepEqual(plain(await f.schedule.loadGameCustomConfig()),custom);assert(!f.effects.includes('file-save'));noCpuTdp(f);
  assert.deepEqual(f.schemeAttempts.map(x=>x.key),[scheme]);assert.equal(f.schemeAttempts[0].accepted,true);assert.equal(events.at(-1).powerSchemes[side],scheme);
  let immediate;f.schedule.onPerformanceScheduleChanged(c=>{immediate=c})();assert.equal(immediate.powerSchemes[side],scheme);stop();
  const saved=JSON.stringify(f.settings);clear(f);await edit(f,{powerScheme:scheme});assert.equal(JSON.stringify(f.settings),saved);assert.equal(f.effects.length,0);assert.equal(f.schemeAttempts.length,0);assert.equal(f.tasks.size,0);
 });
 await check('global-pair/game-change-and-fresh-module',async()=>{
  const f=fixture({dedicated:true});await edit(f,{powerScheme:'besteff'});const controls=clone((await f.schedule.getContinuousPerformanceSnapshot()).controls);
  const saved=clone(f.settings),custom=clone(await f.schedule.loadGameCustomConfig());
  f.setGame({pid:23,processCreated:'new-23',name:'other.exe',path:'C:/Games/other.exe'});
  const other=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(other.identity.scope,'global');assert.equal(other.powerScheme,'besteff');
  const fresh=fixture({dedicated:true});Object.assign(fresh.settings,clone(saved));await fresh.setEntry(custom.entries['game.exe']);clear(fresh);
  assert.equal((await fresh.schedule.getContinuousPerformanceSnapshot()).powerScheme,'besteff');assert.deepEqual(plain((await fresh.schedule.loadPerformanceSchedule()).powerSchemes),{ac:'besteff',dc:'yeman'});
  await fresh.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});noCpuTdp(fresh);assert.deepEqual(fresh.schemeAttempts.map(x=>x.key),['besteff']);
  assert.deepEqual(plain((await fresh.schedule.getContinuousPerformanceSnapshot()).controls),plain(controls));
 });
 for(const side of sides)await check('inactive-side/'+side,async()=>{
  const actual=side==='ac'?'dc':'ac',f=fixture({side:actual});await edit(f,{powerScheme:'bal'},'global',side);
  assert.equal((await f.schedule.getContinuousPerformanceSnapshot('global',side)).powerScheme,'bal');assert.equal(f.schemeAttempts.length,0);assert.equal(f.hardwareWrites.length,0);
  f.sourceMode.value=side;await f.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true});noCpuTdp(f);assert.deepEqual(f.schemeAttempts.map(x=>x.key),['bal']);
 });
 for(const dedicated of [false,true])await check('passive-runtime/ac-dc/'+dedicated,async()=>{
  const f=fixture({dedicated});seedPair(f,{ac:'besteff',dc:'bestperf'});f.runtime.startGamePolicyRuntime();try{
   await settle(f,true);noCpuTdp(f);assert.equal(f.activeScheme,'test-besteff');clear(f);
   f.sourceMode.value='dc';for(const notify of [...f.powerListeners])notify('dc','ac');await settle(f,true);
   noCpuTdp(f);assert.equal(f.activeScheme,'test-bestperf');assert.deepEqual(f.schemeAttempts.map(x=>x.key),['bestperf']);
   clear(f);await settle(f,true);assert.equal(f.schemeAttempts.length,0);assert.equal(f.tasks.size,0);
  }finally{f.runtime.stopGamePolicyRuntime()}assert.equal(f.tasks.size,0);assert.equal(f.powerListeners.size,0);
 });

 await check('observer/inactive-choice-no-consumption',async()=>{
  const f=fixture();seedPair(f,{ac:'bal',dc:'yeman'});f.runtime.startGamePolicyRuntime();try{
   await settle(f,true);clear(f);await edit(f,{powerScheme:'besteff'},'global','dc');await settle(f,true);
   assert.deepEqual(plain(f.settings.performanceSchedule.powerSchemes),{ac:'bal',dc:'besteff'});assert.equal(f.schemeAttempts.length,0);assert.equal(f.hardwareWrites.length,0);assert.equal(f.tasks.size,0);
  }finally{f.runtime.stopGamePolicyRuntime()}
 });
 for(const patch of [{powerScheme:'bal',tdp:20},{powerScheme:'bal',manual:{turbo:false}},{powerScheme:'invalid'}])await check('validation/standalone/'+JSON.stringify(patch),async()=>{
  const f=fixture(),saved=JSON.stringify(f.settings);await assert.rejects(edit(f,patch));assert.equal(JSON.stringify(f.settings),saved);assert.equal(f.hardwareWrites.length,0);
 });
 await check('validation/auto-cannot-select',async()=>{const f=fixture({mode:'auto'}),saved=JSON.stringify(f.settings);await assert.rejects(edit(f,{powerScheme:'bal'}));assert.equal(JSON.stringify(f.settings),saved);assert.equal(f.hardwareWrites.length,0)});
 for(const kind of ['side','settings-generation','game-birth','game-path','game-generation','scheme'])await check('lazy-terminal/revoke/'+kind,async()=>{
  const ports={},f=fixture({gameActive:true,ports});ports.beforeSelectedScheme=async()=>{
   if(kind==='side')f.sourceMode.value='dc';if(kind==='settings-generation')ports.generation=2;
   if(kind==='game-birth')f.setGame({...f.game,processCreated:'replacement'});if(kind==='game-path')f.setGame({...f.game,path:'C:/Other/game.exe'});
   if(kind==='game-generation')f.setGame({...f.game,identityGeneration:88});if(kind==='scheme')f.settings.performanceSchedule.powerSchemes.ac='bestperf';
  };
  await assert.rejects(edit(f,{powerScheme:'bal'}));assert.equal(f.hardwareWrites.length,0);assert.equal(f.schemeAttempts.length,1);assert.equal(f.schemeAttempts[0].accepted,false);assert(!f.effects.includes('start-float'));
 });
 for(const kind of ['side','settings-generation','birth','path','native-generation','revision'])await check('old-identity/'+kind,async()=>{
  const ports={},f=fixture({gameActive:true,ports}),s=await f.schedule.getContinuousPerformanceSnapshot(),saved=JSON.stringify(f.settings);
  if(kind==='side')f.sourceMode.value='dc';if(kind==='settings-generation')ports.generation=2;if(kind==='birth')f.setGame({...f.game,processCreated:'replacement'});
  if(kind==='path')f.setGame({...f.game,path:'C:/Other/game.exe'});if(kind==='native-generation')f.setGame({...f.game,identityGeneration:88});if(kind==='revision')s.identity.revision='stale';
  await assert.rejects(f.schedule.updateContinuousPerformanceControls(s.identity,{powerScheme:'bal'}));assert.equal(JSON.stringify(f.settings),saved);assert.equal(f.hardwareWrites.length,0);assert.equal(f.schemeAttempts.length,0);
 });
 await check('switch-failure/persist-without-followup-or-poll',async()=>{
  const f=fixture();f.setSwitchFailure(true);await assert.rejects(edit(f,{powerScheme:'bestperf'}),/switch rejected/);assert.equal(f.settings.performanceSchedule.powerSchemes.ac,'bestperf');assert.equal(f.hardwareWrites.length,0);assert.equal(f.tasks.size,0);
  f.setSwitchFailure(false);clear(f);await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});noCpuTdp(f);assert.equal(f.activeScheme,'test-bestperf');
 });
 const forbidden=[{manual:{frequencyMHz:2000}},{manual:{aggressiveness:40}},{manual:{unlimited:true}},{manual:{turbo:false}},{manual:{coreMode:'littleOnly'}},
  {auto:{bias:60}},{auto:{saving:'none'}},{auto:{coreSplit:12}},{auto:{coreLow:'bigOnly'}},{auto:{coreHigh:'littleOnly'}},{hyperThreadMode:'off'},{hyperThreadMode:'on'},{hyperThreadMode:'default'}];
 for(const dedicated of [false,true])for(const scheme of schemes.slice(1))for(const patch of forbidden)await check('windows-cpu-reject/'+(dedicated?'dedicated':'global')+'/'+scheme+'/'+JSON.stringify(patch),async()=>{
  const f=fixture({gameActive:true,dedicated});seedPair(f,{ac:scheme,dc:'yeman'});const saved=JSON.stringify(f.settings),custom=plain(await f.schedule.loadGameCustomConfig());
  await assert.rejects(edit(f,patch),/Windows/);assert.equal(JSON.stringify(f.settings),saved);assert.deepEqual(plain(await f.schedule.loadGameCustomConfig()),custom);assert.equal(f.hardwareWrites.length,0);
 });
 for(const scheme of schemes.slice(1))await check('windows-allowed/tdp-ceiling-fps/'+scheme,async()=>{
  const f=fixture();seedPair(f,{ac:scheme,dc:'yeman'});const tdp=await edit(f,{tdp:35});assert.equal(tdp.bank.tdp,35);assert.equal(tdp.tdpWriteReceipt.ok,true);
  assert.equal(f.hardwareWrites.filter(x=>x.kind==='tdp').length,1);assert.equal(f.hardwareWrites.filter(x=>['cpu','core','active-scheme'].includes(x.kind)).length,0);assert(!f.effects.includes('start-float'));
  clear(f);const ceiling=await edit(f,{tdpCeiling:40});assert.equal(ceiling.bank.tdpCeiling,40);assert.equal(f.hardwareWrites.filter(x=>['cpu','core','active-scheme'].includes(x.kind)).length,0);
  clear(f);const fps=await edit(f,{frameRate:{fps:90,lastFps:90,ceiling:120}});assert.equal(fps.frameRateApplied,true);assert.equal(fps.frameRates.ac.fps,90);assert.equal(f.effects.filter(e=>e==='fps').length,1);assert.equal(f.hardwareWrites.length,0);
 });
 for(const side of sides)for(const scheme of schemes)await check('actual-decky/four-choice/'+side+'/'+scheme,async()=>{
  const f=fixture({side});seedPair(f,{ac:scheme==='yeman'?'bal':'yeman',dc:scheme==='yeman'?'bal':'yeman'});let called=0,mapped;
  const bridge=gateway(f,(_identity,patch)=>{called++;mapped=clone(patch)}),view=adapter.performanceMirrorView(await f.schedule.getContinuousPerformanceSnapshot(),'actual');
  assert.deepEqual(plain(view.controls.find(c=>c.key==='powerScheme').choices.map(c=>c.data)),schemes);assert.equal((await bridge.send({powerScheme:scheme})).saved,true);
  assert.equal(called,1);assert.deepEqual(plain(mapped),{powerScheme:scheme});assert.equal((await f.schedule.getContinuousPerformanceSnapshot()).powerScheme,scheme);noCpuTdp(f);
 });
 for(const key of ['frequencyMHz','aggressiveness','unlimited','turbo','coreMode','hyperThreadMode','bias','saving','coreSplit','coreLow','coreHigh'])await check('actual-decky/old-request-reject/'+key,async()=>{
  const f=fixture({gameActive:true});seedPair(f,{ac:'bal',dc:'besteff'});let called=0;
  const bridge=gateway(f,()=>called++),view=adapter.performanceMirrorView(await f.schedule.getContinuousPerformanceSnapshot(),'actual');
  assert(!view.controls.some(c=>c.key===key));await assert.rejects(bridge.send({[key]:({frequencyMHz:2000,aggressiveness:40,unlimited:true,turbo:false,coreMode:'littleOnly',hyperThreadMode:'off',bias:50,saving:'none',coreSplit:10,coreLow:'bigOnly',coreHigh:'bigOnly'})[key]}));
  assert.equal(called,0);assert.equal(f.hardwareWrites.length,0);
 });
 await check('actual-decky/admission-revision-includes-scheme',async()=>{const f=fixture(),s=await f.schedule.getContinuousPerformanceSnapshot(),bridge=gateway(f);await edit(f,{powerScheme:'bal'});clear(f);await assert.rejects(bridge.send({tdp:20},s),/SOURCE_CHANGED/);assert.equal(f.hardwareWrites.length,0)});
 await check('return-yeman/explicit-cpu-and-auto',async()=>{
  const f=fixture();seedPair(f,{ac:'bal',dc:'bestperf'});await edit(f,{powerScheme:'yeman'});noCpuTdp(f);clear(f);
  await edit(f,{manual:{frequencyMHz:2400}});assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);assert.equal(f.hardwareWrites.find(w=>w.kind==='cpu').params.acFreq,2400);
  clear(f);await edit(f,{mode:'auto'});assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);assert.equal(f.floating,true);assert(f.effects.includes('ensure-scheme'));assert.deepEqual(plain(f.settings.performanceSchedule.powerSchemes),{ac:'yeman',dc:'bestperf'});
 });
 await check('automatic/remember-manual-choice',async()=>{
  const f=fixture();seedPair(f,{ac:'besteff',dc:'bestperf'});await edit(f,{mode:'auto'});assert.equal(f.floating,true);assert(f.effects.includes('ensure-scheme'));assert.equal(f.schemeAttempts.length,0);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);
  assert.deepEqual(plain(f.settings.performanceSchedule.powerSchemes),{ac:'besteff',dc:'bestperf'});clear(f);await edit(f,{mode:'manual'});assert.equal(f.floating,false);assert.equal(f.activeScheme,'test-besteff');assert.equal(f.hardwareWrites.filter(w=>['cpu','core','active-scheme'].includes(w.kind)).length,0);
 });
 await check('observer/current-scheme-event-releases-old-independent-smt',async()=>{
  const ports={smtAvailable:true},f=fixture({gameActive:true,ports});await f.setEntry({displayName:'old SMT only',enabled:false,smtIndependent:true,hyperThreadPolicyEnabled:true,hyperThreadPolicy:'off'});
  f.runtime.startGamePolicyRuntime();try{await settle(f,true);assert.equal(f.corePolicyCalls.filter(c=>c.kind==='apply').length,1);clear(f);
   await edit(f,{powerScheme:'bal'});await settle(f,true);observations.push({id:'observer-old-smt',...counts(f)});
   assert.equal(f.corePolicyCalls.filter(c=>c.kind==='apply').length,0);assert.equal(f.corePolicyCalls.filter(c=>c.kind==='clear').length,1);noCpuTdp(f);assert.equal(f.tasks.size,0);
  }finally{f.runtime.stopGamePolicyRuntime()}
 });

 for(const side of sides)await check('fresh-legacy-global/manual-default/'+side,async()=>{
  const f=fixture({side,legacy:true}),legacy=await f.schedule.loadPerformanceSchedule();
  f.settings.performanceSchedule.profiles=clone(legacy.profiles);f.settings.performanceSchedule.active=clone(legacy.active);seedPair(f,{ac:'besteff',dc:'bestperf'});
  const first=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(first.bank.mode,'manual');assert.equal(first.powerScheme,side==='ac'?'besteff':'bestperf');
  await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});noCpuTdp(f);assert.equal(f.schemeAttempts.at(-1).key,side==='ac'?'besteff':'bestperf');
 });
 await check('legacy-dedicated/automatic-without-mode',async()=>{
  const f=fixture({gameActive:true}),config=await f.schedule.loadPerformanceSchedule();seedPair(f,{ac:'bal',dc:'besteff'});
  await f.setEntry({displayName:'legacy automatic',enabled:true,ac:config.profiles.ac[config.active.ac],dc:config.profiles.dc[config.active.dc]});clear(f);
  const s=await f.schedule.getContinuousPerformanceSnapshot();assert.equal(s.bank.mode,'auto');assert.equal(s.powerScheme,'bal');
  await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});assert(f.effects.includes('ensure-scheme'));assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);assert.equal(f.schemeAttempts.length,0);
 });
 await check('standalone/admission-revoked-before-and-during-terminal',async()=>{
  const ports={},f=fixture({ports}),s=await f.schedule.getContinuousPerformanceSnapshot(),saved=JSON.stringify(f.settings);
  await assert.rejects(f.schedule.updateContinuousPerformanceControls(s.identity,{powerScheme:'bal'},()=>false));assert.equal(JSON.stringify(f.settings),saved);assert.equal(f.schemeAttempts.length,0);
  let allowed=true;ports.beforeSelectedScheme=async()=>{allowed=false};await assert.rejects(f.schedule.updateContinuousPerformanceControls(s.identity,{powerScheme:'bal'},()=>allowed));
  assert.equal(f.settings.performanceSchedule.powerSchemes.ac,'bal');assert.equal(f.hardwareWrites.length,0);assert.equal(f.schemeAttempts[0].accepted,false);
 });
 const bm=bootModel();
 for(const id of ['global','xbox','steam','xbox-steam'])for(const representation of ['root','schedule','root-precedence','legacy-default'])await check('actual-boot/normalize-reload/'+id+'/'+representation,async()=>{
  const f=fixture(),controls=clone((await f.schedule.getContinuousPerformanceSnapshot()).controls),baseline=Object.fromEntries(bm.BOOT_MODE_MODULES.map(key=>[key,{}]));baseline.performance={controls,schedule:{configured:true,enabled:true,controls},frames:clone(f.settings.rtss.frameRates)};
  const raw=bm.createBootModeDocument(baseline),pair={ac:'besteff',dc:'bestperf'},value=clone(baseline.performance);
  if(['root','root-precedence'].includes(representation))value.powerSchemes=pair;if(representation==='schedule')value.schedule.powerSchemes=pair;
  if(representation==='root-precedence')value.schedule.powerSchemes={ac:'bal',dc:'bal'};
  if(id==='global')raw.global.performance=value;else raw.scenes[id].overrides.performance=value;
  const normalized=bm.normalizeBootModeDocument(JSON.parse(JSON.stringify(raw)),baseline),reloaded=bm.normalizeBootModeDocument(JSON.parse(JSON.stringify(normalized)),baseline),resolved=bm.resolveBootMode(reloaded,id);
  const expected=representation==='legacy-default'?{ac:'yeman',dc:'yeman'}:pair;
  if(representation!=='legacy-default')assert.deepEqual(plain(representation!=='schedule'?resolved.performance.powerSchemes:resolved.performance.schedule.powerSchemes),pair);
  await applyBoot(f,bootConsumer(f,extractions),resolved.performance);assert.deepEqual(plain((await f.schedule.loadPerformanceSchedule()).powerSchemes),expected);
  assert.deepEqual(plain(f.settings.performanceSchedule.powerSchemes),expected);assert.equal(await f.schedule.getPerformanceScheduleOwnership(),'manual');
  const fresh=fixture();Object.assign(fresh.settings,JSON.parse(JSON.stringify(f.settings)));const actual=await fresh.schedule.loadPerformanceSchedule();assert.deepEqual(plain(actual.powerSchemes),expected);
  for(const side of sides){assert.equal((await fresh.schedule.getContinuousPerformanceSnapshot('global',side)).powerScheme,expected[side]);
   fresh.sourceMode.value=side;clear(fresh);await fresh.schedule.restorePerformanceScheduleIfConfigured(side,{automaticOnly:true});assert.deepEqual(fresh.schemeAttempts.map(x=>x.key),[expected[side]]);noCpuTdp(fresh);}
 });
 for(const mode of ['auto','manual'])await check('actual-boot/live-observer/single-owner/'+mode,async()=>{
  const f=fixture({mode}),apply=bootConsumer(f,extractions),completed=[];let unsubscribe=()=>{};f.runtime.startGamePolicyRuntime();try{
   await settle(f,true);clear(f);unsubscribe=f.schedule.onContinuousPerformanceApplied(key=>completed.push(key));const controls=clone((await f.schedule.getContinuousPerformanceSnapshot()).controls);controls.ac.tdp=42;controls.ac.manual.frequencyMHz=2300;
   const value={powerSchemes:{ac:mode==='manual'?'bal':'besteff',dc:'bestperf'},controls,frames:{ac:{fps:100,lastFps:100,ceiling:120},dc:{fps:60,lastFps:60,ceiling:60}}};
   let bootError;try{await applyBoot(f,apply,value)}catch(error){bootError=error}
   await settle(f,true);const observation={id:'boot-live-'+mode,completed:clone(completed),...counts(f),owner:await f.schedule.getPerformanceScheduleOwnership(),bootError:bootError?String(bootError.stack||bootError):null};observations.push(observation);
   assert.ifError(bootError);assert.equal(completed.length,1,'one successful transaction must publish exactly one completion');assert.equal(f.effects.filter(effect=>effect==='settings-save').length,1,'performance and frames commit together');
   assert.equal(observation.owner,mode);assert.equal(observation.tdp,1,'one Boot transaction must have one TDP writer');assert.equal(observation.cpu,mode==='auto'?1:0,'one actual CPU owner');assert.equal(f.floating,mode==='auto');assert.equal(f.tasks.size,0);
  }finally{unsubscribe();f.runtime.stopGamePolicyRuntime()}
 });

 await check('actual-boot/live-observer/failure-no-completion-receipt',async()=>{
  const f=fixture(),apply=bootConsumer(f,extractions),completed=[];
  f.runtime.startGamePolicyRuntime();await settle(f,true);clear(f);
  const unsubscribe=f.schedule.onContinuousPerformanceApplied(key=>completed.push(key));
  try{
   const controls=clone((await f.schedule.getContinuousPerformanceSnapshot()).controls);controls.ac.tdp=39;
   const value={powerSchemes:{ac:'bal',dc:'besteff'},controls,frames:clone(f.settings.rtss.frameRates)};
   f.setSwitchFailure(true);await assert.rejects(applyBoot(f,apply,value),/switch rejected/);await settle(f,true);
   observations.push({id:'boot-failure-no-completion',completed:clone(completed),...counts(f)});
   assert.equal(completed.length,0,'failed Boot must not emit an applied completion');assert.equal(f.settings.performanceSchedule.powerSchemes.ac,'bal');
   assert.equal(f.schemeAttempts.length>0,true);assert.equal(f.schemeAttempts.some(attempt=>attempt.accepted),false);noCpuTdp(f);
   f.setSwitchFailure(false);clear(f);await applyBoot(f,apply,value);await settle(f,true);
   assert.equal(completed.length,1,'recovery must consume once instead of acknowledging the failed attempt');
   assert.equal(f.hardwareWrites.filter(write=>write.kind==='tdp').length,1);assert.equal(f.schemeAttempts.length,1);assert.equal(f.tasks.size,0);
  }finally{unsubscribe();f.runtime.stopGamePolicyRuntime()}
 });

 await check('actual-boot/live-observer/atomic-write-failure-no-events',async()=>{
  const ports={},f=fixture({ports}),apply=bootConsumer(f,extractions),configured=[],completed=[],drafts=[];
  f.runtime.startGamePolicyRuntime();await settle(f,true);clear(f);
  const stopConfig=f.schedule.onPerformanceScheduleChanged(config=>configured.push(clone(config))),stopApplied=f.schedule.onContinuousPerformanceApplied(key=>completed.push(key));configured.length=0;
  try{
   const controls=clone((await f.schedule.getContinuousPerformanceSnapshot()).controls);controls.ac.tdp=37;
   const value={powerSchemes:{ac:'bal',dc:'bestperf'},controls,frames:{ac:{fps:95,lastFps:95,ceiling:120},dc:{fps:55,lastFps:55,ceiling:60}}},before=clone(f.settings);
   ports.beforeSettingsCommit=async draft=>{drafts.push(draft);throw Error('injected atomic settings commit failure')};
   await assert.rejects(applyBoot(f,apply,value),/injected atomic settings commit failure/);await settle(f,true);
   observations.push({id:'boot-atomic-write-failure',configuredEvents:configured.length,completed:clone(completed),drafts:clone(drafts),...counts(f)});
   assert.equal(drafts.length,1);assert.equal(drafts[0].performanceSchedule.powerSchemes.ac,'bal');assert.equal(drafts[0].rtss.frameRates.ac.fps,95);
   assert.deepEqual(plain(f.settings),plain(before),'both settings sections must remain at the previous committed value');
   assert.equal(configured.length,0,'failed persistence must not emit a configuration event');assert.equal(completed.length,0,'failed persistence must not acknowledge application');
   assert.equal(f.effects.length,0);assert.equal(f.schemeAttempts.length,0);assert.equal(f.hardwareWrites.length,0);assert.equal(f.tasks.size,0);
  }finally{stopConfig();stopApplied();f.runtime.stopGamePolicyRuntime()}
 });

 const appScenarios=[
  {id:'dedicated-auto/boot-applied',dedicated:true,mode:'auto',boot:'apply',expected:0},
  {id:'global-auto/boot-applied',mode:'auto',boot:'apply',expected:0},
  {id:'global-manual/boot-applied',mode:'manual',boot:'apply',expected:0},
  {id:'global-auto/boot-no-application',mode:'auto',boot:'noop',expected:0},
  {id:'dedicated-auto/boot-no-application',dedicated:true,mode:'auto',boot:'noop',expected:0},
  {id:'global-manual/boot-no-application',mode:'manual',boot:'noop',expected:0},
  {id:'unconfigured/boot-atomic-failed',mode:'auto',configured:false,boot:'atomic-fail',expected:0},
  {id:'global-auto/boot-driver-failed-recovery',mode:'auto',boot:'driver-fail',expected:1},
  {id:'dedicated-auto/resume-generation-mismatch',dedicated:true,mode:'auto',boot:'apply',expected:1,advanceGeneration:true},
  {id:'global-auto/resume-generation-mismatch',mode:'auto',boot:'apply',expected:1,advanceGeneration:true},
  {id:'global-auto/same-intent-failed-after-success',mode:'auto',boot:'driver-fail',expected:1,sameIntent:true},
  {id:'dedicated-auto/same-intent-failed-after-success',dedicated:true,mode:'auto',boot:'driver-fail',expected:1,sameIntent:true},
 ];
 for(const scenario of appScenarios)await check('actual-app/startup/'+scenario.id,async()=>{
  const ports={},f=fixture({mode:scenario.mode,dedicated:!!scenario.dedicated,configured:scenario.configured!==false,ports}),apply=bootConsumer(f,extractions);
  f.actualReconcile=actualPowerReconcile(f,ports,extractions);
  const completed=[],phases=[],stopApplied=f.schedule.onContinuousPerformanceApplied(key=>completed.push(key));let app;
  const stage=name=>phases.push({name,completed:clone(completed),...counts(f)});
  const value={powerSchemes:{ac:scenario.mode==='manual'?'bal':'besteff',dc:'bestperf'},controls:clone((await f.schedule.getContinuousPerformanceSnapshot('global')).controls),frames:clone(f.settings.rtss.frameRates)};
  if(scenario.sameIntent)value.powerSchemes={ac:'yeman',dc:'yeman'};else value.controls.ac.tdp=42;
  app=appStartupConsumer(f,{restoreBoot:async()=>{
   if(scenario.boot==='noop')return;
   if(scenario.boot==='atomic-fail')ports.beforeSettingsCommit=async()=>{throw Error('injected App Boot atomic failure')};
   if(scenario.boot==='driver-fail')ports.beforeCpuWrite=async()=>{throw Error('injected App Boot strict CPU failure')};
   try{await applyBoot(f,apply,value)}finally{delete ports.beforeSettingsCommit;delete ports.beforeCpuWrite}
  },onStage:async name=>{if(name==='runtime-started')await settle(f,true);if(name==='before-app-fallback'&&scenario.advanceGeneration)app.advanceResumeGeneration();if(name==='app-fallback-returned')await settle(f,true);stage(name)}},extractions);
  try{
   await app.run();const before=phases.find(phase=>phase.name==='boot-returned'),after=phases.at(-1);
   const observation={id:'app-startup/'+scenario.id,scenario,phases,reconciles:clone(app.reconciles),directRestores:clone(app.restores),caughtBootWarnings:clone(app.warnings),registeredEvents:[...app.events.keys()],deferredCoreTimerMs:app.timers.map(timer=>timer.ms),legacyFallbacks:clone(app.fallbacks),delta:{tdp:after.tdp-before.tdp,cpu:after.cpu-before.cpu,completed:after.completed.length-before.completed.length},finalOwner:await f.schedule.getPerformanceScheduleOwnership(),startupWatch:clone(app.watch)};observations.push(observation);
   assert.equal(app.watch.subscribed,1);assert.equal(app.watch.active,0);assert.equal(app.watch.disposed,1);if(scenario.mode==='auto'&&scenario.configured!==false)assert(app.watch.notifications>0,'watch precedes the original automatic observer consumption');
   if(scenario.advanceGeneration)assert.equal(app.restores.at(-1).intentAtCall,'','old resume generation must pass an empty token');
   assert.equal(app.warnings.length,scenario.boot.endsWith('fail')?1:0,'App must expose only the injected Boot failure warning');
   assert.equal(observation.delta.tdp,scenario.expected,'App final restore must not replay an already completed automatic intent; failed unconsumed intent still restores');
   assert.equal(observation.delta.cpu,scenario.expected,'App final CPU consumption count');
   if(scenario.mode==='manual')assert.equal(after.cpu,0);if(scenario.mode==='auto'&&scenario.configured!==false)assert.equal(after.floating,true,'automatic owner must remain active or recover after a failed rewrite');
   if(scenario.boot==='driver-fail')assert.equal(observation.delta.completed,1,'failed Boot must leave fallback recovery admissible');
  }finally{stopApplied();app.stop();f.runtime.stopGamePolicyRuntime()}
 });


 for(const dedicated of [false,true])await check('startup-receipt/exact-auto-no-reconsume/'+(dedicated?'dedicated':'global'),async()=>{
  const f=fixture({dedicated,mode:'auto'}),completed=[],stop=f.schedule.onContinuousPerformanceApplied(key=>completed.push(key));
  try{
   await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});assert.equal(completed.length,1);const receipt=completed[0],lease=f.ownerLease;
   clear(f);completed.length=0;
   assert.equal(await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true,completedStartupIntent:()=>receipt}),'auto');
   assert.equal(f.hardwareWrites.length,0);assert.equal(f.effects.length,0,'skip must occur before disableFloat or scheme terminals');assert.equal(completed.length,0,'skip must not fabricate a new completion');assert.equal(f.floating,true);assert.equal(f.ownerLease,lease);
   clear(f);await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});
   assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').length,1,'ordinary wake/AC-style caller without startup token must restore');assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);assert.equal(completed.length,1);
  }finally{stop()}
 });
 const invalidReceipts=['empty','different-string','side','bank','fps','scheme','pid','birth','path','native-generation','owner'];
 for(const dedicated of [false,true])for(const change of invalidReceipts)await check('startup-receipt/mismatch/'+(dedicated?'dedicated':'global')+'/'+change,async()=>{
  const f=fixture({dedicated,gameActive:true,mode:'auto'}),completed=[],stop=f.schedule.onContinuousPerformanceApplied(key=>completed.push(key));
  try{
   await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});assert.equal(completed.length,1);let receipt=completed[0];
   if(change==='empty')receipt='';if(change==='different-string')receipt+='-invalid';if(change==='side')f.sourceMode.value='dc';
   if(change==='bank'){
    if(dedicated){const config=await f.schedule.loadGameCustomConfig(),entry=clone(config.entries['game.exe']);entry.controls.ac.tdp=41;await f.setEntry(entry)}else f.settings.performanceSchedule.controls.ac.tdp=41;
   }
   if(change==='fps'){if(dedicated){const config=await f.schedule.loadGameCustomConfig();config.rtss['game.exe']={enabled:true,acFps:95,dcFps:60};await f.schedule.saveGameCustomConfig(config)}else f.settings.rtss.frameRates.ac.fps=95;assert.equal((await f.schedule.getContinuousPerformanceSnapshot()).frameRates.ac.fps,95,'changed effective FPS reaches actual snapshot');}if(change==='scheme')seedPair(f,{ac:'bestperf',dc:'yeman'});
   if(change==='pid')f.setGame({...f.game,pid:33});if(change==='birth')f.setGame({...f.game,processCreated:'new-birth'});if(change==='path')f.setGame({...f.game,path:'C:/Other/game.exe'});if(change==='native-generation')f.setGame({...f.game,identityGeneration:99});
   if(change==='owner'){
    if(dedicated){const config=await f.schedule.loadGameCustomConfig();await f.setEntry({...config.entries['game.exe'],enabled:false})}
    else{const snapshot=await f.schedule.getContinuousPerformanceSnapshot();await f.setEntry({enabled:true,displayName:'new dedicated',controls:clone(snapshot.controls)})}
   }
   clear(f);completed.length=0;await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true,completedStartupIntent:()=>receipt});
   assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').length,1,'mismatched current intent must restore TDP');assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);assert.equal(completed.length,1);
  }finally{stop()}
 });
 await check('startup-receipt/manual-still-restores-scheme',async()=>{
  const f=fixture({mode:'auto'}),completed=[],stop=f.schedule.onContinuousPerformanceApplied(key=>completed.push(key));
  try{
   await f.schedule.restorePerformanceScheduleIfConfigured();const receipt=completed.at(-1);
   f.settings.performanceSchedule.controls.ac.mode='manual';seedPair(f,{ac:'bal',dc:'yeman'});clear(f);completed.length=0;
   assert.equal(await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true,completedStartupIntent:()=>receipt}),'manual');
   assert.deepEqual(f.schemeAttempts.map(attempt=>attempt.key),['bal']);noCpuTdp(f);assert.equal(completed.length,0);assert.equal(f.floating,false);
  }finally{stop()}
 });


 for(const ending of ['normal','gate-closed','fallback-throws','unmount-before-fallback'])await check('actual-app/startup/listener-disposed/'+ending,async()=>{
  const ports={},f=fixture({mode:'auto',ports}),completed=[];f.actualReconcile=actualPowerReconcile(f,ports,extractions);
  let app;
  app=appStartupConsumer(f,{restoreBoot:async()=>{},powerGate:async()=>{
   if(app&&app.phasesBeforeFallback){if(ending==='gate-closed')return false;if(ending==='fallback-throws')throw Error('injected final startup gate failure')}
   return true;
  },onStage:async stage=>{
   if(stage==='runtime-started')await settle(f,true);
   if(stage==='before-app-fallback'){app.phasesBeforeFallback=true;if(ending==='unmount-before-fallback'){app.unmountWatch();assert.equal(app.watch.active,0);assert.equal(app.watch.disposed,1)}}
   if(stage==='app-fallback-returned')await settle(f,true);
  }},extractions);
  const stop=f.schedule.onContinuousPerformanceApplied(key=>completed.push(key));
  try{
   if(ending==='fallback-throws')await assert.rejects(app.run(),/injected final startup gate failure/);else await app.run();
   assert.equal(app.watch.active,0);assert.equal(app.watch.disposed,1);const before=app.watch.notifications;
   clear(f);await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});
   assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').length,1);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);assert.equal(app.watch.notifications,before,'disposed listener must not observe a later ordinary restore');
   app.unmountWatch();assert.equal(app.watch.disposed,1,'unmount fallback remains idempotent after finally');
   observations.push({id:'app-watch-disposal/'+ending,startupWatch:clone(app.watch),ordinaryRestore:counts(f)});
  }finally{stop();app.stop();f.runtime.stopGamePolicyRuntime()}
 });


 await check('startup-receipt/invalidation-failure-and-unsubscribe',async()=>{
  const ports={},f=fixture({mode:'auto',ports}),events=[];let token='';
  const stop=f.schedule.onContinuousPerformanceApplied(key=>{token=key;events.push('completed')},()=>{token='';events.push('invalidated')});
  try{
   await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});assert.deepEqual(events,['invalidated','completed']);const first=token;
   events.length=0;clear(f);await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true,completedStartupIntent:()=>token});assert.equal(token,first);assert.equal(events.length,0);assert.equal(f.effects.length,0);
   ports.beforeCpuWrite=async()=>{throw Error('injected strict same-bank rewrite failure')};
   await assert.rejects(f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true}),/injected strict same-bank rewrite failure/);
   assert.deepEqual(events,['invalidated'],'failed rewrite invalidates and never publishes success');assert.equal(token,'');assert.equal(f.floating,false);
   delete ports.beforeCpuWrite;events.length=0;clear(f);await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true,completedStartupIntent:()=>token});
   assert.deepEqual(events,['invalidated','completed']);assert.equal(f.hardwareWrites.filter(w=>w.kind==='tdp').length,1);assert.equal(f.hardwareWrites.filter(w=>w.kind==='cpu').length,1);assert.equal(f.floating,true);
   stop();events.length=0;await f.schedule.restorePerformanceScheduleIfConfigured(undefined,{automaticOnly:true});assert.equal(events.length,0,'one unsubscribe disposes both callbacks');
  }finally{stop()}
 });

 const sourceAfter=identity(),drift=sourceBefore.filter((v,i)=>v.sha256!==sourceAfter[i].sha256).map(v=>v.path);
 if(drift.length)cases.push({id:'identity/source-stable',pass:false,executedThisRun:true,error:'Source changed during selected regression: '+drift.join(',')});
 const dependencies=[...new Set([process.execPath,require.resolve('@esbuild/win32-x64/esbuild.exe'),...Object.keys(require.cache)])].filter(file=>fs.existsSync(file)).map(hash);
 const report={schema:1,pass:cases.every(c=>c.pass),count:cases.length,failed:cases.filter(c=>!c.pass).length,executedThisRun:cases.length,startedAt:new Date(Date.now()-(performance.now()-started)).toISOString(),finishedAt:new Date().toISOString(),elapsedMs:performance.now()-started,
  canonicalEntry:'tools/performance_entry_parity_selftest.mjs --power-schemes-only',module:'performance',parentGate:'NOT_RUN; explicit selected extension only; no registration/minimum expansion',
  reuseSearch:['Existing performance_entry_parity_selftest covers actual Decky/backend parity; extended by an explicit branch.','Existing performance_memory_fixture keeps actual schedule/runtime; only terminal ports added.','No existing performance_power_schemes verifier or registered four-choice regression found.'],
  scope:'Actual performanceSchedule, gamePolicyRuntime observers, Decky adapter/model, Boot normalize/resolve and AST-extracted complete applyRuntimeModule performance branch; actual App startup performance AST nodes and real yeman reconcile/remember helper AST with memory terminals. Terminal filesystem/settings/IPC/power/TDP/CPU/RTSS are memory only.',
  notRun:['Real hardware and installed app','Actual yeman helper implementation/hardware semantics (peer owned)','Decky TSX renderer and Vue UI','Other Boot module branches and native launch pipeline; Boot once-per-Windows-boot claim uses a controlled test adapter','Whole App mount, power.schemeChanged delivery and 3-second core timer are NOT_RUN','Default suite, parent/release gates, whole-system lifecycle'],realHardwareCalls:0,
  cases,observations,sourceBefore,sourceAfter,drift,dependencies,extractions:[...new Map(extractions.map(e=>[e.sha256,e])).values()],command:{executable:process.execPath,argv:process.argv,cwd:process.cwd()}};
 if(output){fs.mkdirSync(output,{recursive:true});fs.writeFileSync(path.join(output,'performance-power-schemes.json'),JSON.stringify(report,null,2)+'\n');
  fs.writeFileSync(path.join(output,'REPORT.md'),`# Selected backend power schemes regression\n\n${report.pass?'PASS':'FAIL'}: ${report.count-report.failed}/${report.count} actual selected cases; ${report.elapsedMs.toFixed(2)} ms.\n\n${report.scope}\n\nReal hardware calls: 0. Final source pins are supplied by the coordinating user; this report binds its own sourceBefore/sourceAfter hashes.\n\nNot run: ${report.notRun.join('; ')}.\n\nFailures:\n\n${cases.filter(c=>!c.pass).map(c=>'- '+c.id+'\n\n```\n'+c.error+'\n```').join('\n')}\n\nObservations:\n\n\`\`\`json\n${JSON.stringify(observations,null,2)}\n\`\`\`\n`);
 }
 return report;
}
