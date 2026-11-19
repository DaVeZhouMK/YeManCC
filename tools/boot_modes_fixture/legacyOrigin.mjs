import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
import {compileSettingsConsumer,createSettingsFixture} from '../fixtures/settings_baseline.mjs';

export async function runLegacyOriginCases({test,out,runtimeBundle}){
 await test('legacy global origin scene preset actions retain unresolved and explicit empty modules',async()=>{
  const ts=createRequire(import.meta.url)('typescript'),source=fs.readFileSync('src/components/BootModeWorkbench.vue','utf8');
  const script=source.slice(source.indexOf('<script setup')+source.slice(source.indexOf('<script setup')).indexOf('>')+1,source.indexOf('</script>'));
  const ast=ts.createSourceFile('BootModeWorkbench.ts',script,ts.ScriptTarget.Latest,true);
  const functions=ast.statements.filter(node=>ts.isFunctionDeclaration(node)&&['saveScene','restoreScene'].includes(node.name?.text));
  assert.equal(functions.length,2);const code=ts.transpileModule(functions.map(node=>node.getText(ast)).join('\n')+'\nglobalThis.actions={saveScene,restoreScene};',{compilerOptions:{target:ts.ScriptTarget.ES2022}}).outputText;
  const a=await runtimeBundle(out),base=await a.boot.captureBootModeSnapshot(),rows=[];
  const repositoryCode=await compileSettingsConsumer(process.cwd());
  for(const mode of ['global','xbox','steam','xbox-steam']){
   const doc=a.model.createBootModeDocument(base);delete doc.global.performance;
   const normalized=a.model.normalizeBootModeDocument(doc,base);normalized.global.display={};
   const remembered={performance:{controls:{ac:{tdpCeiling:27}}},background:{ui:{backgroundOpacity:64}}};
   if(mode!=='global')normalized.scenes[mode].rememberedOverrides=structuredClone(remembered);
   const context={draft:{value:normalized},selected:{value:mode},blocked:{value:false},sceneName:{value:'saved'},scenePreset:{value:''},notice:{value:''},
    cloneBootValue:a.model.cloneBootValue,bootGlobalModuleUnresolved:a.model.bootGlobalModuleUnresolved,adoptBootGlobalModule:a.model.adoptBootGlobalModule};
   vm.runInNewContext(code,context);context.actions.saveScene();
   const saved=normalized.presets[mode].saved;
   assert.equal(saved.overrides.performance,undefined,'saving a scene captured missing global reference');
   if(mode==='global')assert.deepEqual(saved.overrides.display,{});
   else assert.deepEqual(saved.rememberedOverrides,remembered,'scene preset lost inactive independent values');
   const persisted=a.model.normalizeBootModeDocument(normalized,base);
   if(mode!=='global')assert.deepEqual(persisted.presets[mode].saved.rememberedOverrides,remembered,'normalization lost preset memory');
   const commonOverlay=mode==='global'||mode==='steam';
   const f=createSettingsFixture(repositoryCode,{startupDesired:{steamOverlayOnBoot:commonOverlay},extensions:{bootModes:persisted,unrelated:{keep:true}}});
   await f.repo.loadSettings();
   await f.repo.updateSettings(settings=>{settings.extensions.bootModes=structuredClone(persisted);});
   const reopened=createSettingsFixture(repositoryCode,f.text()),actual=await reopened.repo.loadSettings();
   assert.equal(actual.startupDesired.steamOverlayOnBoot,commonOverlay,'common startup setting changed with mode preset');
   assert.deepEqual(actual.extensions.unrelated,{keep:true});
   context.draft.value=a.boot.readStoredBootMode(actual,base);
   context.scenePreset.value='saved';context.actions.restoreScene();
   if(mode!=='global'){
    assert.deepEqual(context.draft.value.scenes[mode].rememberedOverrides,remembered,'reload/restore lost independent memory');
    assert.equal(context.draft.value.scenes[mode].overrides.performance,undefined,'restore activated a remembered module');
    a.model.setBootModeModuleScope(context.draft.value,mode,'performance',true);
    assert.equal(context.draft.value.scenes[mode].overrides.performance.controls.ac.tdpCeiling,27,'enable independent recovered global instead of remembered value');
   }
   assert(normalized.unresolvedGlobal.includes('performance'));assert.equal(normalized.global.performance.controls,undefined);
   if(mode==='global')assert.deepEqual(normalized.global.display,{});
   rows.push({mode,unresolvedGlobalNotCaptured:true,restoreDidNotInventOrigin:true});
  }
  rows.push(...await verifyBootGlobalReset({model:a.model,boot:a.boot,ports:a.ports,base,repositoryCode}));
  return rows;
 });
 await test('legacy global origin never promotes references across all sixteen modules',async()=>{
  const a=await runtimeBundle(out),q=a.ports,reference=await a.boot.captureBootModeSnapshot();
  reference.performance.controls.ac.tdpCeiling=49;reference.background.ui.theme='red-black';
  const doc=a.model.createBootModeDocument(reference);doc.global={};q.settings.extensions.bootModes=structuredClone(doc);
  const loaded=await a.boot.loadBootModes({editorOnly:true});
  assert.deepEqual(loaded.document.unresolvedGlobal,a.model.BOOT_MODE_MODULES);
  assert.equal(loaded.document.global.performance.controls,undefined);assert.equal(loaded.document.global.background.ui,undefined);
  assert.equal(loaded.baseline.performance.controls.ac.tdpCeiling,q.settings.performanceSchedule.controls.ac.tdpCeiling);
  for(const module of a.model.BOOT_MODE_MODULES){
   assert(a.model.bootGlobalModuleUnresolved(loaded.document,module));
   const again=a.model.normalizeBootModeDocument(loaded.document,reference);
   assert.deepEqual(again.global[module],loaded.document.global[module],'reference leaked into '+module);
  }
  const saved=await a.boot.saveBootModes(loaded.document),raw=q.settings.extensions.bootModes;
  assert.equal(raw.global.performance.controls,undefined);assert.equal(raw.global.input.persona,undefined);
  assert.deepEqual(raw.unresolvedGlobal,a.model.BOOT_MODE_MODULES);assert.deepEqual(saved.unresolvedGlobal,raw.unresolvedGlobal);
  const before=structuredClone(q.settings);
  for(const invalid of [['bogus'],['sleep','sleep'],'performance']){
   const broken={...saved,unresolvedGlobal:invalid};q.settings.extensions.bootModes=broken;
   await assert.rejects(a.boot.loadBootModes({overviewOnly:true}),/来源标记/);
  }
  Object.assign(q.settings,before);
  return {modules:a.model.BOOT_MODE_MODULES.length,referenceNeverSaved:true,malformedOriginsRejected:3};
 });
 await test('legacy global origin preserves three independent scenes and leaves missing global performance unconfirmed',async()=>{
  const rows=[];
  for(const mode of ['xbox','steam','xbox-steam']){
   const a=await runtimeBundle(out),q=a.ports,base=await a.boot.captureBootModeSnapshot();
   const doc=a.model.createBootModeDocument(base),own=structuredClone(doc.global.performance);
   own.controls.ac.tdpCeiling=55;own.schedule.controls.ac.tdpCeiling=55;
   doc.current=mode;doc.boot=mode;doc.scenes[mode].overrides.performance=own;doc.activeSnapshot=a.model.resolveBootMode(doc,mode);
   delete doc.global.performance;
   q.settings.extensions.bootModes=structuredClone(doc);
   q.settings.performanceSchedule.controls.ac.tdpCeiling=49;
   const loaded=await a.boot.loadBootModes({editorOnly:true});
   assert.equal(loaded.document.global.performance.controls,undefined);assert.equal(loaded.baseline.performance.controls.ac.tdpCeiling,49);
   assert.deepEqual(loaded.document.scenes,doc.scenes);
   let saved=await a.boot.saveBootModes(loaded.document);
   assert.equal(q.settings.extensions.bootModes.global.performance.controls,undefined);
   const ownApplied=await a.boot.applyBootMode(mode,saved,{startup:true});
   assert.equal(q.settings.performanceSchedule.controls.ac.tdpCeiling,55);
   assert(!ownApplied.receipt.pending.some(value=>value.startsWith('性能调度：旧配置')),'complete independent module was blocked');
   assert.deepEqual(ownApplied.document.scenes[mode].overrides.performance,own);
   q.calls.length=0;
   const global=await a.boot.applyBootMode('global',ownApplied.document,{startup:true});
   assert.equal(q.settings.performanceSchedule.controls.ac.tdpCeiling,55,'missing global changed the actual performance');
   assert.equal(q.calls.filter(row=>row.op==='performance.apply').length,0);
   assert.equal(global.receipt.modules.performance.state,'waiting');
   assert(global.receipt.pending.some(value=>value.startsWith('性能调度：旧配置')));
   assert.equal(q.settings.extensions.bootModes.global.performance.controls,undefined);
   const reopened=await a.boot.loadBootModes({editorOnly:true});
   a.model.adoptBootGlobalModule(reopened.document,'performance',reopened.baseline.performance);
   saved=await a.boot.saveBootModes(reopened.document);
   assert.equal(saved.global.performance.controls.ac.tdpCeiling,55);
   assert.equal(saved.unresolvedGlobal?.includes('performance')??false,false);
   assert.equal(q.settings.extensions.bootModes.global.performance.controls.ac.tdpCeiling,55);
   assert.deepEqual(saved.scenes[mode].overrides.performance,own);
   rows.push({mode,independent55Applied:true,globalFallbackWrites:0,explicitAdoptionRequired:true});
   a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();
  }
  return rows;
 });
 await test('legacy global origin preserves acknowledged ordinary leaves without importing sibling runtime fields',async()=>{
  const a=await runtimeBundle(out),doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
  delete doc.global.background;
  const normalized=a.model.normalizeBootModeDocument(doc,await a.boot.captureBootModeSnapshot());
  const compiled=await compileSettingsConsumer(process.cwd()),f=createSettingsFixture(compiled,{extensions:{bootModes:normalized},ui:{theme:'red-black',backgroundOpacity:.8}});
  await f.repo.loadSettings();await f.repo.saveSettingsSection('ui',{backgroundOpacity:.42});
  const disk=f.document();assert.equal(disk.extensions.bootModes.global.background.ui.backgroundOpacity,.42);
  assert.equal(disk.extensions.bootModes.global.background.ui.theme,undefined);
  assert(disk.extensions.bootModes.unresolvedGlobal.includes('background'));
  const reference=await a.boot.captureBootModeSnapshot();reference.background.ui.theme='yellow-black';
  const reopened=a.model.normalizeBootModeDocument(disk.extensions.bootModes,reference);
  assert.equal(reopened.global.background.ui.backgroundOpacity,.42);assert.equal(reopened.global.background.ui.theme,undefined);
  assert.equal(a.model.previewBootMode(reopened,'global',reference).background.ui.theme,'yellow-black');
  assert.equal(reopened.global.background.ui.theme,undefined);
  return {ordinaryLeafRetained:true,runtimeThemeNotPromoted:true,originStillUnresolved:true};
 });
 await test('legacy global origin never launches missing inherited software or fabricates input activation',async()=>{
  if(process.argv.includes('--unknown-startup-input'))return verifyUnknownStartupInput(out,{runtimeBundle,...(process.argv.includes('--unknown-startup-native')?{native:globalThis.__bootModeSaveNative}:{})});
  const a=await runtimeBundle(out),q=a.ports,doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
  for(const module of ['input','gyro','audioOptimization','frameGeneration','softwareLaunch'])delete doc.global[module];
  doc.boot='global';doc.bootEnabled=true;q.settings.extensions.bootModes=structuredClone(doc);
  const loaded=await a.boot.loadBootModes({editorOnly:true});
  const saved=await a.boot.saveBootModes(loaded.document),started=await a.boot.applyBootMode('global',saved,{startup:true});
  for(const module of ['input','gyro','audioOptimization','frameGeneration','softwareLaunch']){
   assert.equal(started.receipt.modules[module].state,'waiting');
   assert.equal(started.receipt.modules[module].requested?.inputApplication,undefined);
  }
  assert.equal(q.calls.filter(row=>row.op==='app.launch'||row.op==='ipc.fxsound.setEnabled').length,0);
  assert.equal(q.calls.filter(row=>row.op==='input.cas').length,0,'unknown input issued a fabricated request');
  assert.equal(q.settings.extensions.bootModes.global.input.persona,undefined);
  assert.equal(q.settings.startupDesired.virtualGamepadPersona,'disabled');
  return {missingModules:5,unrequestedLaunches:0,fabricatedInputCas:0};
 });
}

/** Unknown legacy mode leaves are not explicit next-boot OFF choices. */
export async function verifyUnknownStartupInput(out,options={}){
 const {createDiskRuntime}=options.native?await import('./diskSettings.mjs'):{},disk=options.native?await createDiskRuntime(path.join(out,'unknown-input-disk'),options.native,{moduleDirectory:path.join(out,'unknown-input-runtime')}):null;
 const a=disk?disk.a:await options.runtimeBundle(out),q=a.ports,rows=[];
 const hooks={...q.hooks},seed=disk?await a.repository.loadSettings():null;
 const modes=options.firstOnly?['global']:a.model.BOOT_MODE_IDS,actions=options.firstOnly?['save']:['save','choose'];
 const kinds=disk?['unknown-on','unknown-off','explicit-off','gyro-off','disabled-scene']:['unknown-on'];
 for(const mode of modes)for(const action of actions)for(const kind of kinds){
  q.reset();Object.assign(q.hooks,hooks);
  if(disk){disk.writeRaw(structuredClone(seed));Object.assign(q.settings,await a.repository.loadSettings());}
  const document=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
  for(const module of ['input','gyro']){delete document.global[module];for(const scene of Object.values(document.scenes))delete scene.overrides[module];}
  if(kind==='explicit-off')document.global.input={persona:'disabled'};
  if(kind==='gyro-off')document.global.gyro={motion:{enabled:false}};
  if(kind==='disabled-scene'&&mode!=='global')document.scenes[mode].overrides.input={persona:'disabled'};
  document.current=mode;document.boot=mode;document.bootEnabled=true;q.settings.extensions.bootModes=structuredClone(document);
  if(kind==='disabled-scene')document.bootEnabled=false;
  q.settings.startupDesired={...q.settings.startupDesired,virtualGamepadPersona:kind==='unknown-off'?'disabled':'dualsense-edge',gyroPreset:kind==='unknown-off'?'off':'fps',unrelated:'retained'};
  if(disk){const data=disk.read();data.extensions.bootModes=structuredClone(document);data.startupDesired=structuredClone(q.settings.startupDesired);disk.writeRaw(data);Object.assign(q.settings,await a.repository.loadSettings());}
  const current=disk?await a.repository.loadSettings():q.settings,before=structuredClone(current.startupDesired),input=structuredClone(current.input),games=structuredClone(current.gameProfiles);
  const shown=await a.boot.loadBootModes({editorOnly:true}),saved=action==='save'?await a.boot.saveBootModes(shown.document):await a.boot.chooseBootMode(mode,shown.document,kind!=='disabled-scene');
  const after=disk?await a.repository.loadSettings():q.settings,expected=structuredClone(before);
  if(kind==='explicit-off')Object.assign(expected,{virtualGamepadPersona:'disabled',gyroPreset:'off'});
  if(kind==='gyro-off')expected.gyroPreset='off';
  rows.push({mode,action,kind,before,expected,after:structuredClone(after.startupDesired),globalInput:saved.global.input,globalGyro:saved.global.gyro,inputUnchanged:JSON.stringify(after.input)===JSON.stringify(input),gamesUnchanged:JSON.stringify(after.gameProfiles)===JSON.stringify(games)});
  if(disk){const fresh=await disk.moduleAt('reopen-'+rows.length),reopened=await fresh.repository.loadSettings();assert.deepEqual(reopened.startupDesired,expected);assert.deepEqual(reopened.input,input);assert.deepEqual(reopened.gameProfiles,games);disk.bind();}
 }
 fs.writeFileSync(path.join(out,'unknown-startup-input.json'),JSON.stringify({rows,actualRows:rows.length},null,2),{flag:'wx'});
 assert(rows.every(row=>JSON.stringify(row.expected)===JSON.stringify(row.after)&&row.inputUnchanged&&row.gamesUnchanged),'Unknown/OFF startup ownership mismatch: '+JSON.stringify(rows));
 return {rows,actualRows:rows.length,explicitOnOffPreserved:true,actualNativeFileAndReopen:!!disk};
}


/** The original editor's actual computed/watch/save bodies, with isolated refs.
 * Configuration reconciliation only; this does not claim browser navigation. */
export async function verifyBootEditorDocumentUpdates({model:m,boot:b,ports:p}){
 const index=process.argv.indexOf('--workbench-source');
 const file=index>=0?process.argv[index+1]:'src/components/BootModeWorkbench.vue';
 assert(typeof file==='string'&&file&&!file.startsWith('--'),'--workbench-source requires an explicit existing component');
 const source=fs.readFileSync(file,'utf8'),start=source.indexOf('<script setup'),script=source.slice(source.indexOf('>',start)+1,source.indexOf('</script>',start));
 const ts=createRequire(import.meta.url)('typescript'),ast=ts.createSourceFile('BootModeWorkbench.ts',script,ts.ScriptTarget.Latest,true);
 const declaration=name=>ast.statements.find(node=>ts.isVariableStatement(node)&&node.declarationList.declarations.some(row=>row.name.getText(ast)===name));
 const watcher=ast.statements.find(node=>ts.isExpressionStatement(node)&&ts.isCallExpression(node.expression)&&node.expression.expression.getText(ast)==='watch'&&node.expression.arguments[0]?.getText(ast)==='() => props.document');
 const save=ast.statements.find(node=>ts.isFunctionDeclaration(node)&&node.name?.text==='save');
 assert(declaration('dirty')&&declaration('saveRequested')&&watcher&&save,'Original editor state declarations');
 const code=ts.transpileModule([declaration('dirty'),declaration('saveRequested'),watcher,save].map(node=>node.getText(ast)).join('\n')+'\nglobalThis.editorSave=save;globalThis.editorDirty=()=>dirty.value;',{compilerOptions:{target:ts.ScriptTarget.ES2022}}).outputText;
 const compiled=await compileSettingsConsumer(process.cwd()),f=createSettingsFixture(compiled,{}),equal=f.baseline.equalSettingsDocuments,rows=[];
 function editor(document){let callback;const emitted=[];
  const context={props:{document:structuredClone(document),saveReceipt:0},draft:{value:structuredClone(document)},blocked:{value:false},notice:{value:''},cloneBootValue:m.cloneBootValue,equalSettingsDocuments:equal,sameBootModeEditableContent:m.sameBootModeEditableContent,
   computed:read=>Object.defineProperty({},'value',{get:read}),watch:(_source,handler)=>{assert(!callback);callback=handler;},emit:(event,value)=>emitted.push({event,value})};
  vm.runInNewContext(code,context);
  return {context,emitted,change(next){const before=context.props.document;context.props.document=next;callback(next,before);},save(){context.editorSave();},dirty(){return context.editorDirty();}};
 }
 const reorder=value=>Array.isArray(value)?value.map(reorder):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).reverse().map(key=>[key,reorder(value[key])])):value;
 const mutate={
  performance:v=>{v.controls.ac.tdpCeiling=71;},fan:v=>{v.preset=v.preset==='soft'?'aggressive':'soft';},charge:v=>{v.limit=79;},
  input:v=>{v.persona=v.persona==='elite'?'disabled':'elite';},gyro:v=>{v.motion.enabled=!v.motion.enabled;},touch:v=>{v.profiles.disabled={...(v.profiles.disabled||{}),transparency:37};},
  audioOptimization:v=>{v.enabled=!v.enabled;},frameGeneration:v=>{v.enabled=!v.enabled;},softwareLaunch:v=>{v.paths=['C:/games/new-config.exe'];},
  steamAppearance:v=>{v.states={...v.states,'editor-concurrent':true};},steamWallpaper:v=>{v.enabled=!v.enabled;},steamDecky:v=>{v.enabled=!v.enabled;},
  monitor:v=>{v.preferences.ymccEnabled=!v.preferences.ymccEnabled;},display:v=>{v.brightness=47;},background:v=>{v.ui.backgroundOpacity=83;},sleep:v=>{v.config.wakePasswordEnabled=!v.config.wakePasswordEnabled;}
 };
 for(const mode of m.BOOT_MODE_IDS){
  p.reset();const base=await b.captureBootModeSnapshot(),document=m.createBootModeDocument(base);document.current=mode;document.boot=mode;
  const clean=editor(document);clean.context.props.document=reorder(document);
  assert.equal(clean.dirty(),false,'Object order alone marked a clean draft dirty');rows.push({mode,kind:'clean-object-order',dirty:false});
  const edited=editor(document);edited.context.draft.value.global.background.ui.backgroundOpacity=41;
  const latest=reorder(document);latest.revision++;p.settings.extensions.bootModes=structuredClone(latest);
  edited.change(latest);assert.equal(edited.context.draft.value.revision,latest.revision,'Serialization-only update left a stale draft revision');
  assert.equal(edited.context.draft.value.global.background.ui.backgroundOpacity,41);
  assert.equal(edited.context.notice.value,'');edited.save();assert.equal(edited.emitted.length,1);
  const saved=await b.saveBootModes(edited.emitted[0].value);assert.equal(saved.global.background.ui.backgroundOpacity,41);
  edited.context.props.saveReceipt++;edited.change(saved);assert.equal(edited.dirty(),false,'Own acknowledged save did not settle dirty state');
  rows.push({mode,kind:'dirty-draft-reordered-receipt',savedRevision:saved.revision,ownSaveAcknowledged:true});
  for(const module of m.BOOT_MODE_MODULES)for(const partial of [false,true]){
   p.reset();const missing=m.createBootModeDocument(base),other=m.BOOT_MODE_MODULES.find(name=>name!==module);
   delete missing.global[module];if(partial)delete missing.global[other];
   const previous=m.normalizeBootModeDocument(missing,base);previous.current=mode;previous.boot=mode;
   const state=editor(previous);m.adoptBootGlobalModule(state.context.draft.value,module,base[module]);
   const adopted=structuredClone(state.context.draft.value.global[module]),remaining=structuredClone(state.context.draft.value.unresolvedGlobal);
   const next=structuredClone(previous);next.revision++;p.settings.extensions.bootModes=structuredClone(next);
   const overlay={sessionId:'configuration-editor-owned-game',outputTarget:{persona:'steamdeck'}};p.settings.input.gameOverride=structuredClone(overlay);
   const input=structuredClone(p.settings.input),scenes=structuredClone(next.scenes);state.change(next);
   assert(!m.bootGlobalModuleUnresolved(state.context.draft.value,module),'Local global confirmation marker was restored by a metadata update');
   assert.deepEqual(state.context.draft.value.unresolvedGlobal,remaining,'Other unresolved global modules were changed');
   state.save();assert.equal(state.emitted.length,1);const result=await b.saveBootModes(state.emitted[0].value);
   assert(!m.bootGlobalModuleUnresolved(result,module));assert.deepEqual(result.global[module],adopted);assert.deepEqual(result.scenes,scenes);assert.deepEqual(p.settings.input,input);
   rows.push({mode,module,kind:'global-adoption-metadata',partial,confirmationPreserved:true,dedicatedInputUnchanged:true});
  }
  for(const module of m.BOOT_MODE_MODULES){
   p.reset();const original=m.createBootModeDocument(base),state=editor(original);state.context.draft.value.global.background.ui.backgroundOpacity=41;
   const foreign=structuredClone(original);foreign.revision++;mutate[module](foreign.global[module]);p.settings.extensions.bootModes=structuredClone(foreign);
   state.change(foreign);assert.equal(state.context.draft.value.revision,original.revision,'Real concurrent configuration was incorrectly rebased');
   assert.match(state.context.notice.value,/外部更新/);state.save();assert.equal(state.emitted.length,1);
   await assert.rejects(b.saveBootModes(state.emitted[0].value),/修改|变化|revision/i);assert.deepEqual(p.settings.extensions.bootModes,foreign);
   rows.push({mode,module,kind:'foreign-configuration',rejected:true});
  }
  p.reset();const arrays=m.createBootModeDocument(base);arrays.global.softwareLaunch.paths=['C:/games/one.exe','C:/games/two.exe'];
  const state=editor(arrays);state.context.draft.value.global.background.ui.backgroundOpacity=41;
  const next=structuredClone(arrays);next.revision++;next.global.softwareLaunch.paths.reverse();p.settings.extensions.bootModes=structuredClone(next);
  state.change(next);assert.equal(state.context.draft.value.revision,arrays.revision,'Array order was wrongly treated as object serialization order');
  state.save();await assert.rejects(b.saveBootModes(state.emitted[0].value),/修改|变化|revision/i);
  rows.push({mode,kind:'ordered-array-conflict',rejected:true});
 }
 return {rows,workbenchSource:file,configurationBoundaryCount:rows.length,browserNavigationClaimed:false};
}


/** Actual global-reset initialization and confirm functions; no preview capture
 * may become stored intent through the reset action. */
export async function verifyBootGlobalReset({model:m,boot:b,ports:p,base,repositoryCode}){
 const at=process.argv.indexOf('--workbench-source'),file=at<0?'src/components/BootModeWorkbench.vue':process.argv[at+1];
 assert(typeof file==='string'&&file&&!file.startsWith('--'),'--workbench-source requires an explicit component');
 const source=fs.readFileSync(file,'utf8'),start=source.indexOf('<script setup'),script=source.slice(source.indexOf('>',start)+1,source.indexOf('</script>',start));
 const ts=createRequire(import.meta.url)('typescript'),ast=ts.createSourceFile('BootModeWorkbench.ts',script,ts.ScriptTarget.Latest,true);
 const declarations=ast.statements.filter(node=>ts.isVariableStatement(node)&&node.declarationList.declarations.some(row=>['initialGlobal','initialUnresolvedGlobal'].includes(row.name.getText(ast))));
 const functions=ast.statements.filter(node=>ts.isFunctionDeclaration(node)&&['confirm','saveScene','restoreScene'].includes(node.name?.text));
 assert(declarations.length>=1&&functions.length===3);
 const code=ts.transpileModule([...declarations,...functions].map(node=>node.getText(ast)).join('\n')+'\nglobalThis.resetGlobal=()=>{confirmation.value="baseline";confirm();};',{compilerOptions:{target:ts.ScriptTarget.ES2022}}).outputText;
 const partial={performance:{controls:{ac:{tdpCeiling:27}}},fan:{preset:'soft'},charge:{limit:83},input:{persona:'disabled'},gyro:{motion:{sensitivityX:67}},touch:{profiles:{disabled:{transparency:42}}},audioOptimization:{enabled:false},frameGeneration:{enabled:false},softwareLaunch:{paths:['C:/games/one-saved.exe']},steamAppearance:{states:{saved:false}},steamWallpaper:{download:false},steamDecky:{enabled:false},monitor:{preferences:{ymccEnabled:false}},display:{resolution:'native'},background:{ui:{backgroundOpacity:42}},sleep:{config:{enabled:false}}};
 const rows=[];
 function context(document,reference){const state={props:{document:structuredClone(document),baseline:structuredClone(reference)},draft:{value:structuredClone(document)},confirmation:{value:''},notice:{value:''},selected:{value:'global'},blocked:{value:false},sceneName:{value:''},scenePreset:{value:''},cloneBootValue:m.cloneBootValue,bootGlobalModuleUnresolved:m.bootGlobalModuleUnresolved,adoptBootGlobalModule:m.adoptBootGlobalModule,emit:()=>{}};vm.runInNewContext(code,state);return state;}
 for(const currentMode of m.BOOT_MODE_IDS){
  for(const module of m.BOOT_MODE_MODULES)for(const shape of ['missing','partial']){
   p.reset();const raw=m.createBootModeDocument(base);raw.current=currentMode;raw.boot=currentMode;
   if(shape==='missing')delete raw.global[module];else{raw.global[module]=structuredClone(partial[module]);raw.unresolvedGlobal=[module];}
   const document=m.normalizeBootModeDocument(raw,base),original=structuredClone(document),reference=m.previewBootMode(document,'global',base);
   const previewOnly=Object.keys(base[module]).find(key=>!Object.hasOwn(document.global[module],key));
   const state=context(document,reference);m.adoptBootGlobalModule(state.draft.value,module,reference[module]);
   state.draft.value.global.background.ui={...(state.draft.value.global.background.ui||{}),backgroundOpacity:13};
   state.resetGlobal();
   assert.deepEqual(state.draft.value.global,document.global,'Global reset imported preview-only runtime values');
   assert.deepEqual(Array.from(state.draft.value.unresolvedGlobal||[]),document.unresolvedGlobal||[],'Global reset failed to restore unresolved source markers');
   assert.deepEqual(state.draft.value.scenes,document.scenes);assert.deepEqual(state.draft.value.presets,document.presets);
   const overlay={sessionId:'reset-config-game',outputTarget:{persona:'steamdeck'},preserve:true};p.settings.input.gameOverride=structuredClone(overlay);
   const input=structuredClone(p.settings.input),games=structuredClone(p.settings.gameProfiles),common=structuredClone(p.settings.startupDesired);
   p.settings.extensions.bootModes=structuredClone(document);const saved=await b.saveBootModes(state.draft.value);
   assert(m.bootGlobalModuleUnresolved(saved,module));assert.deepEqual(saved.global,document.global);
   assert.deepEqual(p.settings.input,input);assert.deepEqual(p.settings.gameProfiles,games);
   assert.equal(p.settings.startupDesired.steamOverlayOnBoot,common.steamOverlayOnBoot);
   const fixture=createSettingsFixture(repositoryCode,{startupDesired:common,input,gameProfiles:games,extensions:{bootModes:saved}});
   // gameOverride is a retired session claim in the current durable schema.
   // Preserve global input and real saved game profiles, not that old owner.
   const expectedInput=structuredClone(fixture.repo.normalizeSettings({input}).input);
   await fixture.repo.loadSettings();await fixture.repo.updateSettings(settings=>{settings.extensions.bootModes=structuredClone(saved);});
   const reopened=createSettingsFixture(repositoryCode,fixture.text()),settings=await reopened.repo.loadSettings();
   const after=m.normalizeBootModeDocument(settings.extensions.bootModes,base);
   assert.deepEqual(after.global,document.global);assert.deepEqual(after.unresolvedGlobal,document.unresolvedGlobal);
   if(previewOnly)assert.equal(Object.hasOwn(after.global[module],previewOnly),false,'Reset persisted a reference-only module field');
   assert.equal(settings.input.gameOverride,null);assert.deepEqual(settings.input,expectedInput);
   assert.deepEqual(settings.gameProfiles,games);assert.deepEqual(document,original,'Reset mutated its opening document');
   state.draft.value.global.background.ui={...(state.draft.value.global.background.ui||{}),backgroundOpacity:14};state.resetGlobal();
   assert.deepEqual(state.draft.value.global,document.global,'Opening reset snapshot was aliased by an earlier reset');
   rows.push({currentMode,module,shape,resetOnlySavedValues:true,unresolvedPreserved:true,realRepositoryReopened:true,dedicatedUnchanged:true});
  }
  p.reset();const document=m.createBootModeDocument(base),state=context(document,{...structuredClone(document.global),background:{ui:{backgroundOpacity:9}}});
  state.draft.value.unresolvedGlobal=['performance'];state.draft.value.global.background.ui.backgroundOpacity=14;state.resetGlobal();
  assert.deepEqual(state.draft.value.global,document.global,'Complete saved global was replaced by reference');
  assert.equal(state.draft.value.unresolvedGlobal,undefined,'Complete global reset retained a draft-only unresolved marker');
  rows.push({currentMode,shape:'complete',openingSavedValuesRestored:true,noInventedUnresolved:true});
 }
 return rows;
}


/** Four-mode storage matrix using actual model/save and repository reload.
 * Runtime/hardware owners stay isolated by the existing memory ports. */
export async function verifyAllBootModuleStorage({model:m,boot:b,ports:p,native,storageRoot,only}){
 const all=m.BOOT_MODE_IDS.flatMap(mode=>m.BOOT_MODE_MODULES.map(module=>mode+'/'+module));
 assert(only===undefined||all.includes(only),'Unknown exact all-module storage selection');
 if(native)assert(typeof native.invoke==='function'&&typeof native.safe==='function'&&typeof storageRoot==='string','Original owned native storage required');
 const path=native?(await import('node:path')).default:null;
 const nativeStorage=native?(await import('./diskSettings.mjs')).createNativeSettingsFixtureStorage:null;
 const code=await compileSettingsConsumer(process.cwd()),base=await b.captureBootModeSnapshot(),rows=[];
 const samples={
  performance:[{controls:{ac:{tdpCeiling:31}}},{controls:{ac:{tdpCeiling:47}}}],
  fan:[{preset:'balanced'},{preset:'aggressive'}],charge:[{enabled:false,limit:80},{enabled:true,limit:90}],
  input:[{persona:'elite',outputTarget:{persona:'elite',buttonMappingEnabled:true}},{persona:'dualsense-edge',outputTarget:{persona:'dualsense-edge',buttonMappingEnabled:true}}],
  gyro:[{motion:{preset:'custom',motionSensitivityX:1.2}},{motion:{preset:'racing',motionSensitivityX:2.7}}],
  touch:[{profiles:{disabled:{transparency:62}}},{profiles:{disabled:{transparency:37}}}],
  audioOptimization:[{enabled:false},{enabled:true}],frameGeneration:[{enabled:false},{enabled:true}],
  softwareLaunch:[{paths:['C:/games/global.exe'],timing:'manual',delay:0},{paths:['C:/games/mode.exe'],timing:'manual',delay:3}],
  steamAppearance:[{states:{saved:false}},{states:{saved:true}}],steamWallpaper:[{enabled:false,download:false},{enabled:true,download:true}],
  steamDecky:[{enabled:false},{enabled:true}],monitor:[{preferences:{ymccEnabled:false,ymccTemplate:'A'}},{preferences:{ymccEnabled:true,ymccTemplate:'B'}}],
  display:[{brightness:45},{brightness:73}],background:[{ui:{backgroundOpacity:43}},{ui:{backgroundOpacity:67}}],
  sleep:[{config:{retryOnEntryFailure:false}},{config:{retryOnEntryFailure:true}}]
 };
 for(const mode of m.BOOT_MODE_IDS)for(const module of m.BOOT_MODE_MODULES){
  if(only!==undefined&&only!==mode+'/'+module)continue;
  p.reset();let document=m.createBootModeDocument(base);document.current=mode;document.boot=mode;document.bootEnabled=true;
  const global=m.mergeBootConfig(document.global[module],samples[module][0]),own=m.mergeBootConfig(document.global[module],samples[module][1]);document.global[module]=global;
  for(const id of ['xbox','steam','xbox-steam'])document.scenes[id].overrides[module]=m.cloneBootValue(own);
  document=m.normalizeBootModeDocument(document,base);
  const originalGlobal=m.cloneBootValue(document.global),originalScenes=m.cloneBootValue(document.scenes);
  p.settings.input.gameOverride={sessionId:'all-module-dedicated-game',outputTarget:{persona:'steamdeck',buttonMappingEnabled:true},keep:{owner:'game'}};
  p.settings.gameProfiles['C:/games/matrix-dedicated.exe']={name:'超长中文游戏专属配置保留用于四模式矩阵核对',input:{persona:'steamdeck'},performance:{tdp:21},gyro:{sensitivityX:123},sentinel:'preserve'};
  const canonical=createSettingsFixture(code,{}).repo.normalizeSettings({input:p.settings.input,gameProfiles:p.settings.gameProfiles});
  p.settings.input=m.cloneBootValue(canonical.input);p.settings.gameProfiles=m.cloneBootValue(canonical.gameProfiles);
  const runtimeInput=m.cloneBootValue(p.settings.input),games=m.cloneBootValue(p.settings.gameProfiles),common={...p.settings.startupDesired,steamOverlayOnBoot:mode==='global'||mode==='steam',keepCommon:mode};
  p.settings.startupDesired=structuredClone(common);p.settings.extensions.bootModes=m.cloneBootValue(document);
  const completed=[],storage=native?nativeStorage(native,path.join(storageRoot,'stores',mode,module)):undefined;
  let firstNativeCommit=true;
  const makeFixture=initial=>{
   const f=createSettingsFixture(code,initial,undefined,storage?{storage}:{});
   if(storage)for(const [file,raw] of storage.snapshot())f.state.files.set(f.key(file),raw);
   return f;
  };
  async function commitAndReopen(stage){
   const storedBefore=m.cloneBootValue(p.settings.extensions.bootModes);
   const expectedGlobal=m.cloneBootValue(document.global),expectedScenes=m.cloneBootValue(document.scenes),expectedResolved=m.resolveBootMode(document,mode),previousRevision=document.revision;
   const saved=await b.saveBootModes(document);assert.equal(saved.revision,previousRevision+1);
   assert.equal(saved.current,mode);assert.equal(saved.boot,mode);assert.deepEqual(saved.global,expectedGlobal);assert.deepEqual(saved.scenes,expectedScenes);
   assert.deepEqual(p.settings.input,runtimeInput);assert.deepEqual(p.settings.gameProfiles,games);
   assert.equal(p.settings.startupDesired.steamOverlayOnBoot,common.steamOverlayOnBoot);assert.equal(p.settings.startupDesired.keepCommon,mode);
   const initial={startupDesired:common,input:runtimeInput,gameProfiles:games,extensions:{bootModes:native?storedBefore:saved,unrelated:{keep:true}}};
   const f=makeFixture(native&&!firstNativeCommit?undefined:initial);await f.repo.loadSettings();const initialWrites=f.state.writes.length;
   const rawBefore=native?fs.readFileSync(native.safe(f.state.file),'utf8'):undefined;
   await f.repo.updateSettings(settings=>{settings.extensions.bootModes=m.cloneBootValue(saved);});assert.equal(f.state.writes.length,initialWrites+1);
   let fileEvidence;
   if(native){
    const rawAfter=fs.readFileSync(native.safe(f.state.file),'utf8'),rawBackup=fs.readFileSync(native.safe(f.state.file+'.bak'),'utf8');
    assert.notEqual(rawAfter,rawBefore,'Native storage case never changed the persisted document');
    assert.equal(rawBackup,rawBefore,'Native backup is not the actual pre-commit document');
    const beforeMode=JSON.parse(rawBefore).extensions.bootModes;
    assert(!f.baseline.equalSettingsDocuments([beforeMode.global,beforeMode.scenes],[saved.global,saved.scenes]),'Only revision changed; template delta was not exercised');
    const folder=path.join(storageRoot,'persisted',mode,module,stage);native.safe(path.join(folder,'yeman-settings.json'));fs.mkdirSync(folder,{recursive:true});
    for(const suffix of ['', '.bak'])fs.copyFileSync(native.safe(f.state.file+suffix),path.join(folder,'yeman-settings.json'+suffix),fs.constants.COPYFILE_EXCL);
    fileEvidence={main:path.join(folder,'yeman-settings.json'),backup:path.join(folder,'yeman-settings.json.bak'),actualDocumentChanged:true,backupEqualsPrecommit:true,liveStore:storage.directory};
    firstNativeCommit=false;
   }
   const reopened=makeFixture(native?undefined:f.text()),settings=await reopened.repo.loadSettings();document=b.readStoredBootMode(settings,base);
   assert.equal(document.revision,saved.revision);assert.deepEqual(document.global,expectedGlobal);assert.deepEqual(document.scenes,expectedScenes);assert.deepEqual(m.resolveBootMode(document,mode),expectedResolved);
   assert.deepEqual(settings.input,runtimeInput);assert.deepEqual(settings.gameProfiles,games);assert.deepEqual(settings.extensions.unrelated,{keep:true});assert.equal(settings.startupDesired.keepCommon,mode);assert.equal(settings.startupDesired.steamOverlayOnBoot,common.steamOverlayOnBoot);
   const before=m.cloneBootValue(document),resolved=m.resolveBootMode(document,mode);resolved[module].storageAliasProbe={changed:true};assert.deepEqual(document,before,'Resolved configuration aliased saved documents');
   p.settings.extensions.bootModes=m.cloneBootValue(document);completed.push({stage,revision:document.revision,atomicRepositoryWrites:1,dedicatedUnchanged:true,...(fileEvidence?{fileEvidence}:{})});
  }
  if(mode==='global'){
   document.global[module]=m.cloneBootValue(own);await commitAndReopen('global-edit');assert.deepEqual(document.scenes,originalScenes);
  }else{
   m.setBootModeModuleScope(document,mode,module,false);const remembered=m.cloneBootValue(document.scenes[mode].rememberedOverrides[module]);
   assert.equal(document.scenes[mode].overrides[module],undefined);assert.deepEqual(remembered,own);
   document.global[module]=m.mergeBootConfig(global,{storageVersion:2});await commitAndReopen('follow-global');
   assert.deepEqual(m.resolveBootMode(document,mode)[module],document.global[module]);assert.deepEqual(document.scenes[mode].rememberedOverrides[module],remembered);
   m.setBootModeModuleScope(document,mode,module,true);assert.deepEqual(document.scenes[mode].overrides[module],remembered,'Re-enabled own module lost its remembered configuration');
   const unchangedGlobal=m.cloneBootValue(document.global);await commitAndReopen('own-restored');assert.deepEqual(document.global,unchangedGlobal);
   for(const other of ['xbox','steam','xbox-steam'].filter(id=>id!==mode))assert.deepEqual(document.scenes[other],originalScenes[other]);
  }
  assert.equal(p.calls.filter(call=>call.op==='app.launch'||call.op==='steam.launch'||call.op==='performance.apply'||call.op==='input.cas').length,0,'Storage check executed a feature owner');
  rows.push({mode,module,completed,ordinaryGlobalOrIndependentRestored:true,dedicatedInputAndGamesUnchanged:true,commonStartupRetained:true,resolutionNeverAliases:true});
 }
 return {rows,actualRows:rows.length,repositoryWrites:rows.reduce((sum,row)=>sum+row.completed.length,0),hardwareActions:0,physicalOrNativeFileProof:!!native,...(native?{actualNativeFileTerminal:true,physicalProof:false,selection:only??null}:{})};
}

/** Original dirty-draft reconciliation, including external content followed by metadata. */
export async function verifyBootEditorConflictChain(out,args=[],options={}){
 const path=(await import('node:path')).default,{pathToFileURL}=await import('node:url'),crypto=await import('node:crypto');
const M=process.cwd(),require=createRequire(path.join(M,'package.json')),ts=require('typescript');
const component=options.workbenchSource??path.join(M,'src/components/BootModeWorkbench.vue');
const text=fs.readFileSync(component,'utf8'),begin=text.indexOf('<script setup'),script=text.slice(text.indexOf('>',begin)+1,text.indexOf('</script>',begin)),ast=ts.createSourceFile(component,script,ts.ScriptTarget.Latest,true);
const declaration=name=>ast.statements.find(n=>ts.isVariableStatement(n)&&n.declarationList.declarations.some(v=>v.name.getText(ast)===name));
const watchNode=ast.statements.find(n=>ts.isExpressionStatement(n)&&ts.isCallExpression(n.expression)&&n.expression.expression.getText(ast)==='watch'&&n.expression.arguments[0]?.getText(ast)==='() => props.document');
const saveNode=ast.statements.find(n=>ts.isFunctionDeclaration(n)&&n.name?.text==='save');assert(declaration('dirty')&&declaration('saveRequested')&&watchNode&&saveNode);
const extracted=[declaration('dirty'),declaration('saveRequested'),watchNode,saveNode].map(n=>n.getText(ast)).join('\n');
const code=ts.transpileModule(extracted+'\nglobalThis.saveDraft=save;',{compilerOptions:{target:ts.ScriptTarget.ES2022}}).outputText;
const all=['global','xbox','steam','xbox-steam'].flatMap(mode=>['global-content','scene-content','preset-content','unresolved-content','array-order','metadata-only','reverted-content','clean','own-save'].map(kind=>({mode,kind,key:mode+'/'+kind})));
const choices=args.filter(v=>v.startsWith('--editor-conflict-only=')),groups=args.filter(v=>v.startsWith('--editor-conflict-group='));
assert(choices.length+groups.length<=1,'Choose one editor conflict selector');
assert(!groups.length||groups[0]==='--editor-conflict-group=same-revision','Unknown editor conflict group');
const collisionCases=['global','xbox','steam','xbox-steam'].flatMap(mode=>['revision-reuse-read','revision-reuse-lock','revision-reuse-metadata'].map(kind=>({mode,kind,key:mode+'/'+kind})));
const selected=groups.length?collisionCases:choices.length?[...all,...collisionCases].filter(s=>s.key===choices[0].slice('--editor-conflict-only='.length)):all;assert(selected.length,'Unknown exact chain selector');
const native=options.native;assert(native&&typeof native.invoke==='function','Original owned native required');
const {createDiskRuntime}=await import(pathToFileURL(path.join(M,'tools/boot_modes_fixture/diskSettings.mjs')));
const disk=await createDiskRuntime(path.join(out,'disk'),native),a=disk.a,r=a.repository;await r.loadSettings();Object.assign(a.ports.settings,await r.loadSettings());
const initial=await a.boot.loadBootModes({editorOnly:true});await a.boot.saveBootModes(initial.document,initial.document.revision,initial.initialSource);
const seed=disk.read(),comparatorModule={exports:{}};
const comparatorCode=require('esbuild').buildSync({entryPoints:[path.join(M,'src/bridge/settingsBaseline.ts')],bundle:true,platform:'node',format:'cjs',write:false}).outputFiles[0].text;
vm.runInNewContext(comparatorCode,{module:comparatorModule,exports:comparatorModule.exports});
const equal=comparatorModule.exports.equalSettingsDocuments;
// Use the existing application comparator; no approximation of ordering rules.
assert.equal(typeof equal,'function');
const rows=[];
function editor(doc){let callback;const emitted=[];
 const c={props:{document:structuredClone(doc),saveReceipt:0},draft:{value:structuredClone(doc)},blocked:{value:false},notice:{value:''},cloneBootValue:a.model.cloneBootValue,equalSettingsDocuments:equal,sameBootModeEditableContent:a.model.sameBootModeEditableContent,
 computed:fn=>Object.defineProperty({},'value',{get:fn}),watch:(_source,fn)=>{assert(!callback);callback=fn;},emit:(event,value,origin)=>emitted.push({event,value,origin})};vm.createContext(c);vm.runInContext(code,c);
 return {c,emitted,change(next){const previous=c.props.document;c.props.document=structuredClone(next);callback(c.props.document,previous);},save(){c.saveDraft();}};
}
try{
 for(const s of selected){
  const data=structuredClone(seed);data.extensions.bootModes.current=s.mode;data.extensions.bootModes.boot=s.mode;
  data.extensions.bootModes.global.softwareLaunch.paths=['C:/fixture/one.exe','C:/fixture/two.exe'];
  disk.writeRaw(data);await r.loadSettings();const original=(await a.boot.loadBootModes({editorOnly:true})).document,ui=editor(original);
  if(s.kind!=='clean')ui.c.draft.value.global.background.ui.backgroundBlur=7;
  const draftBefore=structuredClone(ui.c.draft.value),first=structuredClone(original);first.revision++;
  if(s.kind==='global-content')first.global.background.ui.backgroundOpacity=.23;
  if(s.kind==='scene-content')first.scenes.steam.name='Another window edited this scene';
  if(s.kind==='preset-content')first.presets.steam={other:{name:'other',overrides:{fan:{preset:'soft'}}}};
  if(s.kind==='unresolved-content'){first.unresolvedGlobal=['performance'];first.global.performance={};}
  if(s.kind==='array-order')first.global.softwareLaunch.paths.reverse();
  if(s.kind==='reverted-content')first.global.background.ui.backgroundOpacity=.23;
  const savePeer=async doc=>{const before=disk.read(),next=structuredClone(before);next.extensions.bootModes=structuredClone(doc);const reply=await native.invoke(disk.file,{operation:'write',content:JSON.stringify(next),baseline:before});assert(reply.ok,'Owned peer configuration write must pass');};
  const submit=({value,origin})=>a.boot.saveBootModes(value,value.revision,undefined,origin);
  if(s.kind.startsWith('revision-reuse-')){
   const peer=structuredClone(original);peer.boot=s.mode==='steam'?'xbox':'steam';
   if(s.kind!=='revision-reuse-metadata')peer.global.background.ui.backgroundOpacity=.23;
   const bank=()=>[disk.file,disk.file+'.bak'].map(file=>fs.readFileSync(file,'utf8'));
   let outcome,expected;
   if(s.kind==='revision-reuse-lock'){
    disk.faults.holdWrite=true;ui.save();assert.equal(ui.emitted.length,1);
    const pending=submit(ui.emitted[0]).then(saved=>({saved}),error=>({error:String(error.message)}));
    try{
     for(let poll=0;!disk.faults.releaseWrite&&poll<300;poll++)await new Promise(resolve=>setTimeout(resolve,5));
     assert(disk.faults.releaseWrite,'Original save did not reach its pending file write');
     await savePeer(peer);expected=bank();
    }finally{disk.faults.releaseWrite?.();outcome=await pending;}
   }else{
    await savePeer(peer);expected=bank();ui.change(peer);ui.save();assert.equal(ui.emitted.length,1);
    outcome=await submit(ui.emitted[0]).then(saved=>({saved}),error=>({error:String(error.message)}));
   }
   const actual=disk.read().extensions.bootModes,unchanged=equal(expected,bank());
   const pass=!!ui.emitted[0].origin&&(s.kind==='revision-reuse-metadata'
    ? !!outcome.saved&&actual.boot===peer.boot&&actual.revision===original.revision+1&&actual.global.background.ui.backgroundBlur===7
    : !!outcome.error&&unchanged&&actual.revision===original.revision&&actual.global.background.ui.backgroundOpacity===.23);
   rows.push({key:s.key,pass,unchanged,originReceived:!!ui.emitted[0].origin,originalRevision:original.revision,peerRevision:peer.revision,error:outcome.error??null,actual});
   continue;
  }
  await savePeer(first);ui.change(first);const afterFirst=structuredClone(ui.c.draft.value);
  const second=structuredClone(first);second.revision++;second.boot=s.mode==='steam'?'xbox':'steam';
  if(s.kind==='reverted-content'){second.global=structuredClone(original.global);second.scenes=structuredClone(original.scenes);second.presets=structuredClone(original.presets);delete second.unresolvedGlobal;}
  if(s.kind==='own-save'){
   ui.save();assert.equal(ui.emitted.length,1);const saved=await submit(ui.emitted[0]);ui.c.props.saveReceipt++;ui.change(saved);
   assert.deepEqual(ui.c.draft.value,saved);rows.push({key:s.key,ownSaveSettles:true,revision:saved.revision});continue;
  }
  await savePeer(second);ui.change(second);ui.save();assert.equal(ui.emitted.length,s.kind==='clean'?0:1);
  const rejected=['global-content','scene-content','preset-content','unresolved-content','array-order'].includes(s.kind),rawBefore=fs.readFileSync(disk.file,'utf8');let saved,error;
  if(s.kind!=='clean'){try{saved=await submit(ui.emitted.at(-1));}catch(e){error=String(e.message);}}
  const row={key:s.key,rejected,originalRevision:original.revision,peerRevision:second.revision,draftRevision:ui.c.draft.value.revision,afterFirst,draftBefore,emitted:ui.emitted,error:error??null,after:disk.read()};rows.push(row);
  if(rejected){assert(error,'Two-step external change must reject stale draft '+s.key);assert.equal(fs.readFileSync(disk.file,'utf8'),rawBefore,'Stale draft replaced new configuration '+s.key);assert.equal(ui.c.draft.value.revision,original.revision);assert.deepEqual(ui.c.draft.value.global,draftBefore.global);}
  else if(s.kind==='clean'){assert.deepEqual(ui.c.draft.value,second);}
  else{assert.equal(error,undefined,error);assert.equal(saved.global.background.ui.backgroundBlur,7);assert.equal(saved.boot,second.boot);}
 }
 assert(rows.every(row=>row.pass!==false),'Same revision did not retain the competing configuration');
}finally{
 a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();disk.flush();
 fs.writeFileSync(path.join(out,'EDITOR-CHAIN.json'),JSON.stringify({rows,actualRows:rows.length,selectedNames:selected.map(s=>s.key),complete:rows.length===selected.length,component,componentSha256:crypto.createHash('sha256').update(text).digest('hex'),actualExtractedNodes:4,scope:'Actual workbench watcher/save bodies + original repository/native file lock; isolated refs, not browser navigation'},null,2),{flag:'wx'});
}

}
