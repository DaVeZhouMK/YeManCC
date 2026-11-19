import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createRequire} from 'node:module';
const req=createRequire(path.resolve('package.json'));
export async function runPreparationCases({test,out,runtimeBundle}) {
 await test('platform preparation records explicit readbacks for all four modes enabled and disabled without runtime writes',async()=>{
  const a=await runtimeBundle(out),rows=[];
  for(const mode of a.model.BOOT_MODE_IDS)for(const enabled of [false,true]){
   a.ports.reset();
   // Start from ON so modes 1/2 must actively retire the old advanced route.
   a.ports.state.steam=true;a.ports.state.earlyCommand='"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
   const document=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());
   a.ports.settings.extensions.bootModes=structuredClone(document);
   const input=structuredClone(a.ports.settings.input),games=structuredClone(a.ports.settings.gameProfiles);
   const before={device:a.ports.state.deviceForm,home:a.ports.state.homeApp,flag:a.ports.state.xbox};
   const result=await a.boot.chooseBootMode(mode,document,enabled),receipt=result.preparation;
   assert.equal(receipt.schema,1);assert.equal(receipt.mode,mode);assert.equal(receipt.enabled,enabled);
   assert.equal(receipt.revision,result.revision);assert.equal(receipt.observedBoot,'fixture-windows-boot-1');
   assert.equal(receipt.status,'prepared');assert.equal(result.bootNotice,undefined);
   assert.deepEqual(a.model.normalizeBootPreparation(receipt,result.revision),receipt);
   assert.deepEqual(a.ports.settings.input,input);assert.deepEqual(a.ports.settings.gameProfiles,games);
   assert(!a.ports.calls.some(row=>row.op==='platform.set'||row.op.startsWith('steam.')||row.op.startsWith('mouse.')||row.op==='input.cas'));
   if(mode==='global'){
    for(const id of ['legacyMouse','windows','audioStartup','frameStartup'])assert.equal(receipt.steps[id].state,'skipped');
    assert.equal(a.ports.state.deviceForm,before.device);assert.equal(a.ports.state.homeApp,before.home);assert.equal(a.ports.state.xbox,before.flag);
   }else{
    assert(Object.values(receipt.steps).every(row=>row.state==='confirmed'));
    const xbox=enabled&&(mode==='xbox'||mode==='xbox-steam');
    assert.deepEqual(receipt.steps.windows.observed,{deviceForm:enabled?46:before.device,startupValue:xbox?1:0,homeApp:xbox?'Microsoft.GamingApp_8wekyb3d8bbwe!Microsoft.Xbox.App':''});
   }
   const steam=enabled&&(mode==='steam'||mode==='xbox-steam');
   assert.equal(receipt.steps.steam.state,'confirmed');assert.equal(receipt.steps.steam.observed.enabled,steam);
   assert.equal(receipt.steps.steam.observed.command,steam?'recognized':'empty-or-absent');
   assert.equal(receipt.steps.steam.observed.file.exists,steam);
   assert.deepEqual(a.ports.calls.filter(row=>row.op==='startup.steam').map(row=>row.value),[steam]);
   const reopened=await a.boot.loadBootModes({overviewOnly:true});assert.deepEqual(reopened.document.preparation,receipt);
   rows.push({mode,enabled,previousAdvancedStartup:true,advancedStartupAfter:steam,preparedFromReadback:true,currentInputUnchanged:true});
  }
  return rows;
 });
 await test('platform preparation retains every failed or unconfirmed readback and explicit retry owns a new receipt',async()=>{
  const a=await runtimeBundle(out),rows=[];
  for(const fault of ['windows-write','windows-readback','steam-write','steam-read','steam-service','steam-noncanonical','steam-missing','global-steam-off-leftover','xbox-steam-off-leftover','audio-read','audio-leftover','frame-leftover']){
   a.ports.reset();const document=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());a.ports.settings.extensions.bootModes=structuredClone(document);
   let step,state;
   if(fault==='windows-write'){a.ports.faults['startup.home']=1;step='windows';state='failed';}
   if(fault==='windows-readback'){a.ports.hooks.gamingHomeRead=()=>{a.ports.state.homeApp='concurrent-home';};step='windows';state='waiting';}
   if(fault==='steam-write'){a.ports.faults['startup.steam']=1;step='steam';state='failed';}
   if(fault==='steam-read'){a.ports.faults['preparation.steam.read']=1;step='steam';state='waiting';}
   if(fault==='steam-service'){a.ports.state.earlyService={known:true,installed:false,automatic:false,running:false};step='steam';state='waiting';}
   if(fault==='steam-noncanonical'){a.ports.state.earlyFile={known:true,exists:true,bytes:51,canonical:false,encoding:'utf-8'};step='steam';state='waiting';}
   if(fault==='steam-missing'){a.ports.state.earlyFile={known:true,exists:false,bytes:0,canonical:false,encoding:'unknown'};step='steam';state='waiting';}
   const mode=fault==='global-steam-off-leftover'?'global':fault==='xbox-steam-off-leftover'?'xbox':'xbox-steam';
   if(fault.endsWith('steam-off-leftover')){a.ports.state.steam=true;a.ports.state.earlyFile={known:true,exists:true,bytes:51,canonical:true,encoding:'utf-8'};step='steam';state='waiting';}
   if(fault==='audio-read'){a.ports.hooks.audioStartupRead=()=>{throw Error('fixture access denied');};step='audioStartup';state='waiting';}
   if(fault==='audio-leftover'){a.ports.hooks.audioStartupRead=()=>{a.ports.state.fx=true;};step='audioStartup';state='waiting';}
   if(fault==='frame-leftover'){a.ports.hooks.frameStartupRead=()=>{a.ports.state.addon=true;};step='frameStartup';state='waiting';}
   const failed=await a.boot.chooseBootMode(mode,document),original=structuredClone(failed.preparation);
   assert.equal(original.status,'partial');assert.equal(original.steps[step].state,state);assert(failed.bootNotice);
   const reopened=await a.boot.loadBootModes({overviewOnly:true});assert.deepEqual(reopened.document.preparation,original);
   for(const hook of ['gamingHomeRead','audioStartupRead','frameStartupRead'])delete a.ports.hooks[hook];delete a.ports.state.earlyService;delete a.ports.state.earlyFile;
   const retry=await a.boot.chooseBootMode(mode,failed);assert.equal(retry.preparation.status,'prepared');assert.equal(retry.bootNotice,undefined);
   assert.equal(retry.preparation.revision,original.revision+1);assert.equal(original.status,'partial');
   rows.push({fault,step,state,partialPreserved:true,explicitRetry:true});
  }
  for(const mode of a.model.BOOT_MODE_IDS){
   a.ports.reset();const document=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());a.ports.settings.extensions.bootModes=structuredClone(document);
   const step=mode==='global'?'steam':'frameStartup';
   let replacement;
   a.ports.hooks[mode==='global'?'steamStartupRead':'frameStartupRead']=()=>{
    if(replacement)return;
    replacement=structuredClone(a.ports.settings.extensions.bootModes);
    replacement.preparation.status='partial';
    replacement.preparation.steps[step]={state:'failed',detail:'replacement readback'};
    a.ports.settings.extensions.bootModes=structuredClone(replacement);
   };
   const result=await a.boot.chooseBootMode(mode,document).then(value=>({value}),error=>({error}));
   assert(replacement,`${mode}: final readback not observed`);
   assert.deepEqual(a.ports.settings.extensions.bootModes,replacement,`${mode}: late completion overwrote a replacement receipt with the same revision`);
   assert.match(String(result.error),/变化|过期|失效/);
   rows.push({mode,step,replacementPreserved:true});
  }
  return rows;
 });
 await test('platform preparation persists preparing before a dispatch and preserves newer ordinary edits without rewriting its receipt',async()=>{
  const a=await runtimeBundle(out);a.ports.reset();const document=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());a.ports.settings.extensions.bootModes=structuredClone(document);
  let enter,release;const entered=new Promise(resolve=>{enter=resolve;}),hold=new Promise(resolve=>{release=resolve;});
  a.ports.hooks.steamStartupWrite=async()=>{enter();await hold;};
  const task=a.boot.chooseBootMode('steam',document);await entered;
  const during=await a.boot.loadBootModes({overviewOnly:true});assert.equal(during.document.preparation.status,'preparing');assert(during.document.bootNotice);
  assert.equal(during.document.preparation.steps.windows.state,'confirmed');assert.equal(during.document.preparation.steps.steam.state,'waiting');
  const newer=structuredClone(a.ports.settings.extensions.bootModes);newer.revision++;newer.global.background={ui:{theme:'red-black'}};a.ports.settings.extensions.bootModes=newer;
  release();const selected=await task;assert.equal(selected.revision,newer.revision);assert.equal(selected.preparation.revision,newer.revision-1);
  assert.deepEqual(selected.global.background,newer.global.background);assert.equal(selected.preparation.status,'prepared');
  const draft=structuredClone(selected);draft.global.background={ui:{theme:'yellow-black'}};draft.preparation.steps.steam.observed.enabled=false;
  // The ordinary editor cannot manufacture preparation proof from its draft.
  draft.preparation=structuredClone(selected.preparation);draft.preparation.steps.steam.detail='draft only';
  const saved=await a.boot.saveBootModes(draft);assert.deepEqual(saved.preparation,selected.preparation);assert.equal(saved.revision,selected.revision+1);
  return {preparingDurable:true,ordinaryEditRetained:true,receiptRevisionStable:true,editorCannotReplaceProof:true};
 });
 await test('platform preparation validates mode revision step semantics and missing boot identity never becomes boot proof',async()=>{
  const a=await runtimeBundle(out);a.ports.reset();const document=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());a.ports.settings.extensions.bootModes=structuredClone(document);
  a.ports.hooks.bootIdentity=()=>'';const selected=await a.boot.chooseBootMode('steam',document);assert.equal(selected.preparation.observedBoot,null);assert.equal(selected.preparation.status,'prepared');
  const mutations=[r=>r.schema=2,r=>r.revision=Number.MAX_SAFE_INTEGER,r=>r.mode='unknown',r=>r.enabled='yes',r=>delete r.steps.windows,
   r=>r.steps.unexpected={state:'confirmed',observed:{anything:true}},r=>r.steps.windows.state='skipped',r=>r.steps.windows.observed.startupValue=1,
   r=>r.steps.windows.observed.deviceForm=3,r=>r.steps.steam.observed.enabled=false,r=>r.steps.steam.observed.service.running=false,
   r=>r.steps.steam.observed={},r=>r.steps.frameStartup.observed.enabled=true,r=>r.steps.audioStartup.observed.verification='not-read',r=>r.steps.steam.state='waiting',
   r=>r.steps.steam.observed.file.canonical=false,r=>r.steps.steam.observed.file.bytes=-1,r=>delete r.steps.steam.observed.file];
  for(const mutate of mutations){const broken=structuredClone(selected.preparation);mutate(broken);assert.equal(a.model.normalizeBootPreparation(broken,selected.revision),undefined);}
  const wrong=structuredClone(selected);wrong.preparation.mode='xbox';assert.throws(()=>a.model.normalizeBootModeDocument(wrong,selected.global),/准备回执无效/);
  return {malformedReceipts:mutations.length,unknownBootPreserved:true,notStartupConsumptionProof:true};
 });
 await test('platform preparation actual native reader and strict audio startup reader reject unknown observation',async()=>{
  const directory=path.join(out,'actual-preparation-platform');fs.mkdirSync(directory,{recursive:true});
  const a=await runtimeBundle(directory,{actualDisplayPlatform:true});a.ports.reset();
  for(const reply of [null,{}, {known:false,enabled:false,conflict:false},{known:true,enabled:0,conflict:false},{known:true,enabled:false,conflict:'no'}]){
   a.ports.state.nativeStartupReply=reply;await assert.rejects(a.platform.readBootSteamStartup(),/未确认/);
  }
  a.ports.state.nativeStartupReply={known:true,enabled:true,conflict:false};const missing=await a.platform.readBootSteamStartup();assert.equal(missing.service.known,false);
  a.ports.state.nativeStartupReply={known:true,enabled:false,conflict:false,service:{known:true,installed:true,automatic:true,running:true}};
  const off=await a.platform.readBootSteamStartup();assert.equal(off.command,'empty-or-absent');assert.equal(off.file.known,false);
  a.ports.state.nativeStartupReply.file={known:true,exists:false,bytes:0,canonical:false,encoding:'unknown'};
  assert.deepEqual((await a.platform.readBootSteamStartup()).file,a.ports.state.nativeStartupReply.file);
  assert(a.ports.calls.every(row=>row.op==='ipc.steam.startup.get'));
  const {createSourceFile,ScriptTarget,ScriptKind,isFunctionDeclaration}=req('typescript'),text=fs.readFileSync('src/bridge/yeman.ts','utf8');
  const tree=createSourceFile('yeman.ts',text,ScriptTarget.Latest,true,ScriptKind.TS),fn=tree.statements.find(row=>isFunctionDeclaration(row)&&row.name?.text==='fxExists');assert(fn);
  const {transformSync}=req('esbuild'),module={exports:{}};let directoryValue='',failed=false;
  vm.runInNewContext(transformSync(fn.getText(tree),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,commonStartupDir:async()=>{if(failed)throw Error('read denied');return directoryValue;},fs:{exists:async()=>false}});
  assert.equal(await module.exports.fxExists(),false);await assert.rejects(module.exports.fxExists({strict:true}),/未确认/);
  failed=true;assert.equal(await module.exports.fxExists(),false);await assert.rejects(module.exports.fxExists({strict:true}),/read denied/);
  failed=false;directoryValue='fixture-startup';assert.equal(await module.exports.fxExists({strict:true}),false);
  const dirFn=tree.statements.find(row=>isFunctionDeclaration(row)&&row.name?.text==='commonStartupDir');assert(dirFn);
  const complete={exports:{}};let fallback={exitCode:1,stdout:'C:/fixture/not-a-confirmed-directory'},existsCalls=0;
  vm.runInNewContext(transformSync(dirFn.getText(tree)+'\n'+fn.getText(tree),{loader:'ts',format:'cjs'}).code,
   {module:complete,exports:complete.exports,nativeSysInfo:async()=>null,shell:{run:async()=>fallback},fs:{exists:async()=>{existsCalls++;return false;}}});
  await assert.rejects(complete.exports.fxExists({strict:true}),/未确认|读取失败/);assert.equal(existsCalls,0,'failed fallback was used as an observed directory');
  assert.equal(await complete.exports.fxExists(),false,'ordinary compatibility changed');
  fallback={exitCode:0,stdout:'C:/fixture/startup'};assert.equal(await complete.exports.fxExists({strict:true}),false);
  fallback={exitCode:0,stdout:''};await assert.rejects(complete.exports.fxExists({strict:true}),/未确认/);
  return {actualNativeReader:true,invalidNativeReplies:5,strictUnknownIsNotAbsent:true,ordinaryCompatibility:true};
 });
}

/** Same disabled-fallback case, with the real Native-to-renderer identity handoff. */
export async function runBootIdentityHandoff(out,{native,seed,model}){
 assert(native,'Owned settings-file consumer required');
 const {repo}=await import('./harness.mjs');
 const {createDiskRuntime}=await import('./diskSettings.mjs');
 const defaults=JSON.parse(fs.readFileSync(path.join(repo,'PowerControl/settings-defaults-v1.json'),'utf8'));
 const oldBoot='windows-loader-v1|{16605190-1234-4000-8000-111111111111}|1';
 const boot='windows-volatile-v2|{16605190-1234-4000-8000-222222222222}';
 const nextBoot='windows-volatile-v2|{16605190-1234-4000-8000-333333333333}';
 const rows=[];
 for(const id of model.BOOT_MODE_IDS){
  const controls=['stale-record','invalid-record','malformed-source','source-overflow','mismatched-claim'];
  const kinds=['native-v1-upgrade','native-legacy-anchor','prior-v1-record',...(id==='global'?controls:[])];
  for(const kind of kinds){
   const name=id+'-'+kind,disk=await createDiskRuntime(path.join(out,name),native,{moduleDirectory:path.join(out,'identity-handoff-runtime')});
   const doc=model.createBootModeDocument(seed);
   doc.boot=id;doc.bootEnabled=false;doc.current=id==='global'?'xbox':id;
   doc.global.input={...doc.global.input,persona:'elite',outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:false}};
   doc.global.background.ui.backgroundOpacity=.46;
   const raw=structuredClone(defaults);raw.input.startupAppliedSession='legacy-session';
   delete raw.input.startupAppliedBoot;delete raw.input.startupObservedBoot;
   raw.extensions={bootModes:doc,peer:{keep:true},...(kind==='native-legacy-anchor'?{}:{bootModesAppliedBoot:oldBoot})};
   disk.writeRaw(raw);
   assert.equal((await disk.startup(boot,'native-upgrade')).ok,true);
   const anchored=disk.read();
   assert.equal(anchored.extensions.bootModesAppliedBoot,boot);
   assert.equal(anchored.input.startupObservedBoot,boot);
   assert.equal(anchored.input.outputTarget.persona,'disabled','Native already adopted a new preset at an unproven boundary');
   if(kind!=='native-legacy-anchor')assert.deepEqual(anchored.extensions.bootModesIdentityMigration,{from:oldBoot,to:boot,policy:'preserve-current-until-next-boot'});
   const expectedDefer=!controls.includes(kind);
   if(kind==='prior-v1-record'||!expectedDefer){
    delete anchored.extensions.bootModesDeferredBoot;
    if(kind==='stale-record')anchored.extensions.bootModesIdentityMigration.to=nextBoot;
    if(kind==='invalid-record')anchored.extensions.bootModesIdentityMigration.policy='ignored';
    if(kind==='malformed-source')anchored.extensions.bootModesIdentityMigration.from='windows-loader-v1|not-an-identity';
    if(kind==='source-overflow')anchored.extensions.bootModesIdentityMigration.from=oldBoot.slice(0,-1)+'4294967296';
    if(kind==='mismatched-claim')anchored.extensions.bootModesAppliedBoot=nextBoot;
    disk.writeRaw(anchored);
   }
   const same=await disk.moduleAt('same');same.ports.hooks.bootIdentity=()=>boot;
   Object.assign(same.ports.settings,await same.repository.loadSettings());
   await same.boot.restoreSelectedBootMode();
   const reopened=disk.read();
   const preserved=req('node:util').isDeepStrictEqual(reopened,anchored);
   const applied=reopened.extensions.bootModesStartupResult?.boot===boot;
   const evidence={name,id,kind,preserved,applied,expectedDefer,nativeDeferral:anchored.extensions.bootModesDeferredBoot===boot};
   if(expectedDefer){
    // The next-boot check starts with the real upgraded file, independent of
    // whether the same-boot negative already exposed a premature apply.
    if(!preserved)disk.writeRaw(anchored);
    assert.equal((await disk.startup(nextBoot,'native-next-boot')).ok,true);
    assert.equal(disk.read().input.outputTarget.persona,'elite');
    const later=await disk.moduleAt('later');later.ports.hooks.bootIdentity=()=>nextBoot;
    Object.assign(later.ports.settings,await later.repository.loadSettings());
    await later.boot.restoreSelectedBootMode();
    const appliedLater=disk.read();
    assert.equal(appliedLater.extensions.bootModes.current,'global');
    assert.equal(appliedLater.extensions.bootModes.activeSnapshot.background.ui.backgroundOpacity,.46);
    assert.equal(appliedLater.extensions.bootModesStartupResult.boot,nextBoot);
    assert.deepEqual(appliedLater.extensions.bootModes.scenes,doc.scenes);
    assert.deepEqual(appliedLater.extensions.peer,{keep:true});
    evidence.nextBootInheritedGlobal=true;
   }
   rows.push(evidence);
   disk.flush();
  }
 }
 fs.writeFileSync(path.join(out,'identity-handoff-raw.json'),JSON.stringify(rows,null,2)+'\n',{flag:'wx'});
 assert.equal(rows.length,17);
 const unexpected=rows.filter(row=>row.preserved!==row.expectedDefer||row.applied===row.expectedDefer||
  row.nativeDeferral!==['native-v1-upgrade','native-legacy-anchor'].includes(row.kind));
 assert.deepEqual(unexpected.map(row=>row.name),[],'Native migration and renderer same-boot restore disagreed');
 return rows;
}

export async function runInputOnlyBootIdentityHandoff(out,{native,seed,model}){
 assert(native,'Owned settings-file consumer required');
 const {repo}=await import('./harness.mjs');
 const {createDiskRuntime}=await import('./diskSettings.mjs');
 const defaults=JSON.parse(fs.readFileSync(path.join(repo,'PowerControl/settings-defaults-v1.json'),'utf8'));
 const guid='{16605190-1234-4000-8000-111111111111}';
 const oldBoot=`windows-loader-v1|${guid}|1`,priorBoot=`windows-volatile-v2|${guid}`;
 const boot='windows-volatile-v2|{16605190-1234-4000-8000-222222222222}';
 const nextBoot='windows-volatile-v2|{16605190-1234-4000-8000-333333333333}';
 const rows=[];
 for(const id of model.BOOT_MODE_IDS){
  const kinds=['enabled','disabled','unknown-first',...(id==='global'?['known-v2-choice','invalid-v1-input']:[])];
  for(const kind of kinds){
   const name=`${id}-${kind}`, disk=await createDiskRuntime(path.join(out,name),native,{moduleDirectory:path.join(out,'input-identity-runtime')});
   const expectedDefer=kind!=='known-v2-choice'&&kind!=='invalid-v1-input';
   const doc=model.createBootModeDocument(seed);
   doc.boot=id;doc.bootEnabled=kind!=='disabled';
   doc.current=id==='global'||!doc.bootEnabled?'xbox':'global';
   doc.global.input={...doc.global.input,persona:'elite',outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:false}};
   if(id!=='global')doc.scenes[id].overrides.input={...doc.global.input,persona:'dualsense-edge',outputTarget:{...doc.global.input.outputTarget,persona:'dualsense-edge'}};
   const expectedMode=doc.bootEnabled?id:'global', expectedPersona=expectedMode==='global'?'elite':'dualsense-edge';
   const raw=structuredClone(defaults);
   raw.input.startupAppliedSession='loader-v1-process';raw.input.startupAppliedBoot=kind==='invalid-v1-input'?oldBoot.slice(0,-1)+'4294967296':oldBoot;
   raw.input.startupObservedBoot=raw.input.startupAppliedBoot;
   raw.extensions={bootModes:doc,peer:{keep:true},...(kind==='known-v2-choice'?{bootModesAppliedBoot:priorBoot,bootModesDeferredBoot:priorBoot}:{})};
   disk.writeRaw(raw);
   if(kind==='unknown-first'){
    assert.equal((await disk.startup('','native-unknown')).ok,true);
    const unknown=disk.read();
    assert.equal(unknown.input.startupObservedBoot,null);assert.equal(unknown.input.startupAppliedBoot,oldBoot);
    assert.equal(unknown.input.outputTarget.persona,'disabled');assert.equal(unknown.extensions.bootModesAppliedBoot,undefined);
   }
   assert.equal((await disk.startup(boot,'native-upgrade')).ok,true);
   const anchored=disk.read();
   const restore=async observation=>{
    const renderer=await disk.moduleAt('renderer');renderer.ports.hooks.bootIdentity=()=>observation;
    Object.assign(renderer.ports.settings,await renderer.repository.loadSettings());
    await renderer.boot.restoreSelectedBootMode();return disk.read();
   };
   const same=await restore(boot);
   const account=(value,identity)=>value.extensions.bootModes.current===expectedMode&&
    value.extensions.bootModesStartupResult?.boot===identity&&value.input.outputTarget.persona===expectedPersona;
   const migration=anchored.extensions.bootModesIdentityMigration;
   const row={name,expectedDefer,nativePersona:anchored.input.outputTarget.persona,expectedPersona,
    anchored:anchored.extensions.bootModesAppliedBoot===boot&&anchored.extensions.bootModesDeferredBoot===boot&&
     migration?.from===oldBoot&&migration?.to===boot&&migration?.policy==='preserve-current-until-next-boot',
    sameBootUnchanged:req('node:util').isDeepStrictEqual(same,anchored),sameBootApplied:account(same,boot)};
   if(expectedDefer){
    // Start the independent next-boot control from the pre-renderer native result.
    disk.writeRaw(anchored);
    assert.equal((await disk.startup(nextBoot,'native-next')).ok,true);
    row.nextBootApplied=account(await restore(nextBoot),nextBoot);
   }
   const final=disk.read();
   assert.deepEqual(final.extensions.bootModes.scenes,doc.scenes);assert.deepEqual(final.extensions.peer,{keep:true});
   rows.push(row);disk.flush();
  }
 }
 fs.writeFileSync(path.join(out,'input-only-identity-raw.json'),JSON.stringify(rows,null,2)+'\n',{flag:'wx'});
 assert.equal(rows.length,14);
 const failures=rows.filter(row=>row.expectedDefer
  ? !row.anchored||row.nativePersona!=='disabled'||!row.sameBootUnchanged||row.sameBootApplied||!row.nextBootApplied
  : row.anchored||row.nativePersona!==row.expectedPersona||!row.sameBootApplied);
 assert.deepEqual(failures.map(row=>row.name),[],'Input-only migration must preserve the live configuration until a proven new boot');
 return rows;
}

export async function runStartupClaimLockRace(out,{native,seed,model,waitSignal}){
 assert(native,'Owned settings-file consumer required');
 const {repo}=await import('./harness.mjs');
 const {createDiskRuntime}=await import('./diskSettings.mjs');
 const defaults=JSON.parse(fs.readFileSync(path.join(repo,'PowerControl/settings-defaults-v1.json'),'utf8'));
 const boot='fixture-windows-boot-1',nextBoot='fixture-windows-boot-2',rows=[];
 for(const mode of model.BOOT_MODE_IDS)for(const kind of ['input','new-boot','peer']){
  const name=`${mode}-${kind}`, claims=[];
  const port={...native,async invoke(file,request){
   const result=await native.invoke(file,request);
   if(request.operation==='write'){
    const candidate=JSON.parse(request.content), receipt=candidate.extensions?.bootModesStartupResult;
    if(receipt?.pending?.[0]?.includes('已领取')&&!receipt.error&&
       req('node:util').isDeepStrictEqual(request.baseline?.extensions?.bootModesStartupResult,receipt)===false)
     claims.push({ok:result.ok,reason:result.reason,expectedInputRevision:request.expectedInputRevision});
   }
   return result;
  }};
  const disk=await createDiskRuntime(path.join(out,name),port,{moduleDirectory:path.join(out,'claim-lock-runtime')});
  const doc=model.createBootModeDocument(seed);doc.boot=mode;doc.bootEnabled=true;doc.current=mode==='global'?'xbox':'global';
  doc.global.background.ui.backgroundOpacity=.62;
  const raw=structuredClone(defaults);raw.input.startupObservedBoot=boot;raw.input.startupAppliedBoot=boot;
  raw.extensions={bootModes:doc,peer:{keep:true}};disk.writeRaw(raw);
  const renderer=await disk.moduleAt('contender');renderer.ports.hooks.bootIdentity=()=>boot;
  const prepare=async a=>{
   Object.assign(a.ports.settings,await a.repository.loadSettings());
   const steam=mode!=='steam'&&mode!=='xbox-steam';a.ports.state.steam=steam;
   a.ports.state.earlyCommand=steam?'"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"':null;
  };
  await prepare(renderer);
  const expectedRevision=disk.read().input.revision;
  disk.faults.holdWrite=true;
  const contending=renderer.boot.restoreSelectedBootMode().then(()=>null,error=>String(error));
  let peerWrites=0, waiting=true;
  try {
   await waitSignal(new Promise(resolve=>{const poll=()=>{
    if(disk.faults.releaseWrite)resolve();else if(waiting)setImmediate(poll);
   };poll();}),'stale claim write');
   const held=disk.read();assert.equal(held.extensions.bootModesStartupResult,undefined);
   if(kind==='input'){
    const value=structuredClone(held);value.input.revision++;
    const changed=await native.invoke(disk.file,{operation:'write',content:JSON.stringify(value),baseline:held,expectedInputRevision:held.input.revision});
    assert.equal(changed.ok,true);assert.equal(disk.read().input.revision,expectedRevision+1);
   }else if(kind==='new-boot'){
    assert.equal((await disk.startup(nextBoot,'new-native-process')).ok,true);
    assert.equal(disk.read().input.startupObservedBoot,nextBoot);
   }else{
    const peer=await disk.moduleAt('winner');peer.ports.hooks.bootIdentity=()=>boot;await prepare(peer);
    await peer.boot.restoreSelectedBootMode();
    peerWrites=peer.ports.calls.filter(c=>c.op==='startup.steam').length;
    assert.equal(disk.read().extensions.bootModesActiveBoot,boot);
   }
  }finally{waiting=false;disk.faults.holdWrite=false;disk.faults.releaseWrite?.();await contending;}
  const error=await contending, applied=disk.read();
  const writes=renderer.ports.calls.filter(c=>c.op==='startup.steam').length;
  rows.push({name,kind,error,claims,writes,peerWrites,active:applied.extensions.bootModesActiveBoot===boot,
   claimed:applied.extensions.bootModesAppliedBoot===boot,current:applied.extensions.bootModes.current,
   expectedMode:mode,receipt:applied.extensions.bootModesStartupResult?.status,
   revision:applied.extensions.bootModes.revision,expectedRevision:doc.revision+1});
  assert.deepEqual(applied.extensions.peer,{keep:true});disk.flush();
 }
 fs.writeFileSync(path.join(out,'claim-lock-race-raw.json'),JSON.stringify(rows,null,2)+'\n',{flag:'wx'});
 assert.equal(rows.length,12);
 const expectedOrder={input:[false,true],'new-boot':[false],peer:[true,false]};
 const failures=rows.filter(r=>r.kind==='new-boot'
  ? r.claimed||r.active||r.writes!==0||r.receipt!==undefined
  : !!r.error||!r.active||!r.claimed||r.current!==r.expectedMode||r.revision!==r.expectedRevision||
    (r.kind==='input'?r.writes!==1:r.writes!==0||r.peerWrites!==1));
 assert.deepEqual(failures.map(r=>r.name),[],'Claim ownership must be decided with current input and boot under the settings lock');
 for(const row of rows){
  assert.deepEqual(row.claims.map(c=>c.ok),expectedOrder[row.kind],row.name);
  for(const claim of row.claims){
   assert(Number.isSafeInteger(claim.expectedInputRevision),row.name);
   if(!claim.ok)assert.equal(claim.reason,'revision-conflict',row.name);
  }
 }
 return rows;
}

export async function runStartupClaimAcknowledgement(out,{native,seed,model,waitSignal,late=false}){
 assert(native,'Owned settings-file consumer required');
 const {repo}=await import('./harness.mjs');
 const {createDiskRuntime}=await import('./diskSettings.mjs');
 const defaults=JSON.parse(fs.readFileSync(path.join(repo,'PowerControl/settings-defaults-v1.json'),'utf8'));
 const boot='fixture-windows-boot-1',nextBoot='fixture-windows-boot-2',rows=[];
 const kinds=late?['late-before-read','late-before-cas']:['before-commit','after-commit','peer','next-boot','readback-error'];
 for(const mode of model.BOOT_MODE_IDS)for(const kind of kinds){
  const name=`${mode}-${kind}`,claims=[];
  let disk,delayed,settled=false,recoveryReads=0;
  const port={...native,async invoke(file,request){
   const isClaim=request.operation==='write'&&JSON.parse(request.content).extensions?.bootModesStartupResult?.pending?.[0]?.includes('已领取')&&
    !request.baseline?.extensions?.bootModesStartupResult;
   if(late&&isClaim&&!settled&&!delayed){
    delayed={file,request};throw Error('isolated acknowledgement timeout before native claim completion');
   }
   if(delayed&&(kind==='late-before-cas'?isClaim:request.operation==='read'&&++recoveryReads===2)){
    const commit=await native.invoke(delayed.file,delayed.request);
    assert.equal(commit.ok,true,'The unacknowledged native write must complete');
    claims.push({ok:commit.ok,reason:commit.reason,guarded:delayed.request.guardBootMode===true});
    delayed=undefined;settled=true;
   }
   const result=await native.invoke(file,request);
   if(request.operation==='write'){
    const receipt=JSON.parse(request.content).extensions?.bootModesStartupResult;
    if(isClaim&&!receipt.error){
     claims.push({ok:result.ok,reason:result.reason,guarded:request.guardBootMode===true});
     if(result.ok&&claims.length===1){
      if(kind==='next-boot')assert.equal((await disk.startup(nextBoot,'new-native-process')).ok,true);
      if(kind==='readback-error')disk.faults.failRead=true;
     }
    }
   }
   return result;
  }};
  disk=await createDiskRuntime(path.join(out,name),port,{moduleDirectory:path.join(out,'claim-ack-runtime')});
  const doc=model.createBootModeDocument(seed);doc.boot=mode;doc.bootEnabled=true;doc.current=mode==='global'?'xbox':'global';
  doc.global.background.ui.backgroundOpacity=.62;
  const raw=structuredClone(defaults);raw.input.startupObservedBoot=boot;raw.input.startupAppliedBoot=boot;
  raw.extensions={bootModes:doc,peer:{keep:true}};disk.writeRaw(raw);
  const steam=mode==='steam'||mode==='xbox-steam';
  const prepare=async label=>{
   const a=await disk.moduleAt(label);a.ports.hooks.bootIdentity=()=>boot;
   Object.assign(a.ports.settings,await a.repository.loadSettings());
   a.ports.state.steam=!steam;a.ports.state.earlyCommand=steam?null:'"C:\SOFT\YeMan\PowerControl\YeManSteam.bat"';
   return a;
  };
  const restore=a=>a.boot.restoreSelectedBootMode().then(()=>null,error=>String(error));
  const writes=a=>a.ports.calls.filter(c=>c.op==='startup.steam').map(c=>c.value);
  const renderer=await prepare('requester');
  let winner,waiting=true;
  if(kind==='before-commit')disk.faults.failWrite=true;
  else if(kind==='peer')disk.faults.holdWrite=true;
  else if(!late)disk.faults.dropWriteAck=true;
  const pending=restore(renderer);
  try{
   if(kind==='peer'){
    await waitSignal(new Promise(resolve=>{const poll=()=>{
     if(disk.faults.releaseWrite)resolve();else if(waiting)setImmediate(poll);
    };poll();}),'claim awaiting native file write');
    winner=await prepare('winner');assert.equal(await restore(winner),null);
    assert.equal(disk.read().extensions.bootModesActiveBoot,boot);
    disk.faults.dropWriteAck=true;
   }
  }finally{waiting=false;disk.faults.holdWrite=false;disk.faults.releaseWrite?.();await pending;}
  const error=await pending;
  const applied=disk.read(),extensions=applied.extensions;
  disk.faults.failRead=false;
  const reopened=await prepare('reopened'),reopenError=await restore(reopened);
  assert.deepEqual(disk.read().extensions.peer,{keep:true});
  rows.push({name,mode,kind,steam,error,reopenError,claims,writes:writes(renderer),peerWrites:winner?writes(winner):[],reopenWrites:writes(reopened),
   injectConsumed:late?settled:!disk.faults.dropWriteAck&&!disk.faults.failWrite,observed:applied.input.startupObservedBoot,
   active:extensions.bootModesActiveBoot===boot,claimed:extensions.bootModesAppliedBoot===boot,
   current:extensions.bootModes.current,revision:extensions.bootModes.revision,expectedRevision:doc.revision+1,
   preservedAfterReopen:req('node:util').isDeepStrictEqual(applied,disk.read())});
  disk.flush();
 }
 fs.writeFileSync(path.join(out,'claim-ack-raw.json'),JSON.stringify(rows,null,2)+'\n',{flag:'wx'});
 assert.equal(rows.length,model.BOOT_MODE_IDS.length*kinds.length);
 const same=req('node:util').isDeepStrictEqual;
 const failures=rows.filter(r=>{
  if(!r.injectConsumed||r.reopenError||r.reopenWrites.length||!r.claimed||!r.preservedAfterReopen||r.claims.some(c=>!c.guarded))return true;
  if(r.kind==='next-boot'||r.kind==='readback-error')return r.active||r.writes.length||r.current===r.mode||
   r.observed!==(r.kind==='next-boot'?nextBoot:boot)||!same(r.claims.map(c=>c.ok),[true]);
  if(r.error||!r.active||r.current!==r.mode||r.revision!==r.expectedRevision||r.observed!==boot)return true;
  return r.kind==='peer'? !same(r.claims.map(c=>c.ok),[true,false])||r.writes.length||!same(r.peerWrites,[r.steam])
   : !same(r.claims.map(c=>c.ok),r.kind==='late-before-cas'?[true,false]:[true])||!same(r.writes,[r.steam]);
 });
 assert.deepEqual(failures.map(r=>r.name),[],'Only a verified owner may complete startup after a write acknowledgement failure');
 return rows;
}

export async function runStartupClaimSparseStorage(out,{native,seed,model}){
 assert(native,'Owned settings-file consumer required');
 const {repo}=await import('./harness.mjs');
 const {createDiskRuntime}=await import('./diskSettings.mjs');
 const defaults=JSON.parse(fs.readFileSync(path.join(repo,'PowerControl/settings-defaults-v1.json'),'utf8'));
 const boot='fixture-windows-boot-1', rows=[];
 const cases=['missing','legacy-global','healthy-backup','future-schema',...model.BOOT_MODE_IDS.map(mode=>'disabled-'+mode)];
 for(const kind of cases){
  const claims=[];
  const port={...native,async invoke(file,request){
   const result=await native.invoke(file,request);
   if(request.operation==='write'){
    const candidate=JSON.parse(request.content),receipt=candidate.extensions?.bootModesStartupResult;
    if(receipt?.pending?.[0]?.includes('已领取')&&!receipt.error&&!request.baseline?.extensions?.bootModesStartupResult)
     claims.push({ok:result.ok,reason:result.reason,revision:request.expectedInputRevision,guarded:request.guardBootMode===true});
   }
   return result;
  }};
  const disk=await createDiskRuntime(path.join(out,kind),port,{moduleDirectory:path.join(out,'claim-sparse-runtime')});
  const mode=kind.startsWith('disabled-')?kind.slice('disabled-'.length):undefined;
  const legacy={schemaVersion:1,ui:{backgroundOpacity:.48},startupDesired:{virtualGamepadPersona:'elite',gyroPreset:'off',fanControl:false},extensions:{peer:{keep:true}}};
  let preparedScene;
  if(mode){
   preparedScene=model.createBootModeDocument(seed);preparedScene.boot=mode;preparedScene.bootEnabled=false;preparedScene.current='xbox';
   preparedScene.global.background.ui.backgroundOpacity=.48;
   preparedScene.global.input={...preparedScene.global.input,persona:'elite',mouse:'off',outputTarget:{persona:'elite',buttonMappingEnabled:true,gyroEnabled:false}};
   if(mode!=='global')preparedScene.scenes[mode].overrides.input={...preparedScene.global.input,persona:'steamdeck',outputTarget:{...preparedScene.global.input.outputTarget,persona:'steamdeck'}};
   legacy.extensions.bootModes=preparedScene;
  }
  if(kind==='healthy-backup'){
   // Keep the original native startup observation in the healthy copy.
   fs.writeFileSync(disk.file+'.bak',JSON.stringify({...defaults,...legacy,input:{...defaults.input,startupObservedBoot:boot,startupAppliedBoot:boot}}));
   disk.writeRaw('{corrupt-primary');
  }else if(kind==='future-schema'){
   disk.writeRaw({...legacy,schemaVersion:99});
  }else{
   if(kind!=='missing')disk.writeRaw(legacy);
   assert.equal((await disk.startup(boot,'fresh-native-process')).ok,true,kind);
  }
  const renderer=await disk.moduleAt('renderer');renderer.ports.hooks.bootIdentity=()=>boot;
  const before=fs.readFileSync(disk.file,'utf8');
  const settings=await renderer.repository.loadSettings();Object.assign(renderer.ports.settings,settings);
  renderer.ports.state.steam=true;renderer.ports.state.earlyCommand='"C:\\SOFT\\YeMan\\PowerControl\\YeManSteam.bat"';
  const error=await renderer.boot.restoreSelectedBootMode().then(()=>null,error=>String(error));
  const after=disk.read(),final=after.extensions?.bootModes;
  const row={kind,error,claims,advancedWrites:renderer.ports.calls.filter(c=>c.op==='startup.steam').map(c=>c.value),
   active:after.extensions?.bootModesActiveBoot===boot,current:final?.current,boot:final?.boot,enabled:final?.bootEnabled,
   modePreserved:!preparedScene||req('node:util').isDeepStrictEqual(final?.scenes,preparedScene.scenes),
   persona:after.input?.outputTarget?.persona,opacity:final?.activeSnapshot?.background?.ui?.backgroundOpacity,
   preservedFuture:kind==='future-schema'&&fs.readFileSync(disk.file,'utf8')===before};
  if(kind!=='missing')assert.deepEqual(after.extensions?.peer,{keep:true},kind);
  rows.push(row);disk.flush();
 }
 fs.writeFileSync(path.join(out,'sparse-claim-raw.json'),JSON.stringify(rows,null,2)+'\n',{flag:'wx'});
 assert.equal(rows.length,8);
 for(const row of rows){
  if(row.kind==='future-schema'){
   assert(row.error&&row.preservedFuture&&!row.active,row.kind);assert.equal(row.claims.length,0);assert.deepEqual(row.advancedWrites,[]);
  }else{
   assert.equal(row.error,null,row.kind);assert(row.active&&row.current==='global'&&row.enabled===false&&row.modePreserved,row.kind);
   assert.equal(row.boot,row.kind.startsWith('disabled-')?row.kind.slice('disabled-'.length):'global');
   assert.deepEqual(row.advancedWrites,[false],row.kind);
   assert.equal(row.claims.length,1,row.kind);assert(row.claims[0].ok&&row.claims[0].guarded&&Number.isSafeInteger(row.claims[0].revision),row.kind);
   if(row.kind!=='missing'){assert.equal(row.persona,'elite',row.kind);assert.equal(row.opacity,.48,row.kind);}
  }
 }
 return rows;
}

/** Exact original real-file backend, without reopening the unchanged full UI group. */
export async function runPreparationFileEntry(out,options={}){
 const {repo,runtimeBundle,fingerprints}=await import('./harness.mjs');
 const {prepareDiskSettings,createDiskRuntime,nativeInputs}=await import('./diskSettings.mjs');
 const sources=new Set([...nativeInputs,'tools/boot_modes_fixture/preparation.mjs','tools/boot_modes_fixture/diskSettings.mjs','tools/boot_modes_fixture/diskApiPort.ts','tools/boot_modes_fixture/harness.mjs','tools/boot_modes_file_ui_selftest.mjs','PowerControl/settings-defaults-v1.json']);
 const sourceHashes=fingerprints([...sources]),rows=[],started=performance.now();
 const parent=path.resolve(repo,'../../../Testing/YMCC/Workspace/BootModePreparation');
 if(!options.workspace)fs.mkdirSync(parent,{recursive:true});
 const workspace=options.workspace?path.resolve(options.workspace):fs.mkdtempSync(path.join(parent,'run-'));
 fs.mkdirSync(workspace,{recursive:true});
 if(options.native)assert.equal(path.resolve(options.native.root),workspace,'Owned native root must match the preparation workspace');
 let native;
 const finalReplacement=options.finalReplacementOnly===true;
 assert(!finalReplacement||!options.ownershipCases,'Choose one exact preparation file scope');
 const report={caseId:'settings.boot-modes-file-ui',selection:finalReplacement?'preparation-final-replacement-only':'preparation-only',wholeSuite:'NOT_RUN',pass:false,assertions:rows,sourceHashes,workspace,
  scope:'Actual boot bridge/model/settingsRepository/API with original production Win32 atomic file consumer; platform/service/device terminals remain memory. No browser geometry, installed app, hardware, services or real boot.',
  notRun:['original full UI and all720/800 layouts','touch profile group','physical boot/input/game consumption','native platform/service mutation','logs.exportAll actual archive dispatch']};
 try{
  native=options.native??prepareDiskSettings(out+'/native',{storageRoot:workspace});
  fs.mkdirSync(out+'/seed',{recursive:true});
  const seed=await runtimeBundle(out+'/seed');
  for(const file of seed.inputs)sources.add(file);
  const baseline=await seed.boot.captureBootModeSnapshot();
  for(const from of seed.model.BOOT_MODE_IDS)for(const to of finalReplacement?[from]:seed.model.BOOT_MODE_IDS){
   const name=finalReplacement?'owner-'+to+'-same-revision-final':from+'-to-'+to,disk=await createDiskRuntime(out+'/'+name,native,{storageDirectory:path.join(workspace,name),moduleDirectory:out+'/runtime-module'});
   for(const file of disk.a.inputs)sources.add(file);
   const initial=JSON.parse(fs.readFileSync(repo+'/PowerControl/settings-defaults-v1.json','utf8'));
   initial.extensions={...initial.extensions,bootModes:seed.model.createBootModeDocument(baseline),preparationPeer:{keep:true}};
   disk.writeRaw(initial);const loaded=await disk.a.boot.loadBootModes({overviewOnly:true});const input=structuredClone(disk.read().input);
   if(finalReplacement){
    const other=await disk.moduleAt('other-writer'),step=to==='global'?'steam':'frameStartup';
    let replacementBytes;
    disk.a.ports.hooks[to==='global'?'steamStartupRead':'frameStartupRead']=async()=>{
     await other.repository.updateSettings(settings=>{
      const next=structuredClone(settings.extensions.bootModes);
      assert.equal(next.preparation.status,'preparing');
      next.preparation.status='partial';next.preparation.steps[step]={state:'failed',detail:'replacement readback'};
      settings.extensions.bootModes=next;
     });
     replacementBytes=fs.readFileSync(disk.file,'utf8');
    };
    await assert.rejects(disk.a.boot.chooseBootMode(to,loaded.document,true),/变化|过期|失效/);
    assert(replacementBytes,'Replacement must reach the original settings file');
    assert.equal(fs.readFileSync(disk.file,'utf8'),replacementBytes,'Obsolete completion changed the settings file');
    const reopened=await disk.moduleAt('reopened'),view=await reopened.boot.loadBootModes({overviewOnly:true});
    assert.equal(view.document.preparation.status,'partial');assert.equal(view.document.preparation.steps[step].detail,'replacement readback');
    assert.deepEqual(disk.read().input,input);
    rows.push({name,pass:true,step,actualFile:true,freshRepository:true,replacementBytesPreserved:true});
    disk.flush();continue;
   }
   const first=await disk.a.boot.chooseBootMode(from,loaded.document,true),selected=await disk.a.boot.chooseBootMode(to,first,true);
   assert.equal(selected.preparation.status,'prepared');assert.equal(selected.preparation.mode,to);assert.equal(selected.preparation.revision,selected.revision);
   const onDisk=disk.read();assert.deepEqual(onDisk.extensions.bootModes.preparation,selected.preparation);assert.deepEqual(onDisk.input,input);
   assert.equal(onDisk.extensions.preparationPeer.keep,true);assert(fs.existsSync(disk.file+'.bak'));
   const reopened=await disk.moduleAt('reopened'),view=await reopened.boot.loadBootModes({overviewOnly:true});
   assert.deepEqual(view.document.preparation,selected.preparation);assert.equal(view.document.boot,to);assert.equal(view.document.current,'global');
   rows.push({name,pass:true,actualFile:true,freshRepository:true,preparationRevision: selected.preparation.revision,currentInputUnchanged:true});
   disk.flush();
  }

  if(options.ownershipCases)for(const mode of seed.model.BOOT_MODE_IDS)for(const kind of ['mode','ordinary','input']){
   const name='owner-'+mode+'-'+kind,disk=await createDiskRuntime(out+'/'+name,native,{storageDirectory:path.join(workspace,name),moduleDirectory:out+'/runtime-module'});
   for(const file of disk.a.inputs)sources.add(file);
   const initial=JSON.parse(fs.readFileSync(repo+'/PowerControl/settings-defaults-v1.json','utf8'));
   initial.extensions={...initial.extensions,bootModes:seed.model.createBootModeDocument(baseline),preparationPeer:{keep:true}};
   initial.gameProfiles={'C:/games/config-owner.exe':{name:'配置归属专属游戏',keep:true}};
   disk.writeRaw(initial);const loaded=await disk.a.boot.loadBootModes({overviewOnly:true});
   let enter,release;const entered=new Promise(resolve=>enter=resolve),held=new Promise(resolve=>release=resolve);
   disk.a.ports.hooks.steamStartupWrite=async()=>{enter();await held;};
   const pending=disk.a.boot.chooseBootMode(mode,loaded.document,true),settled=pending.then(value=>({value}),error=>({error:String(error)}));
   let timer;try{await Promise.race([entered,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Original preparation ownership boundary not reached')),2500);})]);}finally{clearTimeout(timer);}
   assert.equal(disk.read().extensions.bootModes.preparation.status,'preparing');
   const other=await disk.moduleAt('other-writer');
   if(kind==='mode')await other.repository.updateSettings(settings=>{const next=structuredClone(settings.extensions.bootModes);next.revision++;next.boot=mode==='global'?'xbox':'global';delete next.preparation;settings.extensions.bootModes=next;});
   if(kind==='ordinary')await other.repository.saveSettingsSection('ui',{backgroundOpacity:41});
   if(kind==='input'){
    const settings=await other.repository.loadSettings();
    const result=await other.repository.compareAndSwapInputSettings(settings.input.revision,{diagnostics:{...settings.input.diagnostics,inputLoggingEnabled:!settings.input.diagnostics.inputLoggingEnabled}});assert.equal(result.ok,true);
   }
   const changed=disk.read(),changedBytes=fs.readFileSync(disk.file,'utf8'),input=structuredClone(changed.input),games=structuredClone(changed.gameProfiles),beforeSteam=disk.a.ports.state.steam;
   release();const result=await settled,after=disk.read();
   assert.deepEqual(after.input,input,'Late preparation rewrote newer input configuration');assert.deepEqual(after.gameProfiles,games);assert.equal(after.extensions.preparationPeer.keep,true);
   if(kind==='mode'){
    assert(result.error,'A replaced preparation request returned success');assert.equal(fs.readFileSync(disk.file,'utf8'),changedBytes,'Old preparation committed after a new mode owner');
    assert.equal(disk.a.ports.state.steam,beforeSteam);
   }else if(kind==='ordinary'){
    assert.equal(result.value?.preparation.status,'prepared');assert.equal(after.ui.backgroundOpacity,41);assert.equal(after.extensions.bootModes.global.background.ui.backgroundOpacity,41);
    assert.equal(after.extensions.bootModes.revision,changed.extensions.bootModes.revision);assert.equal(after.extensions.bootModes.preparation.revision,changed.extensions.bootModes.preparation.revision);
   }else{
    assert(result.error||result.value?.preparation.status==='partial','Changed input allowed obsolete Steam preparation to confirm');assert.equal(disk.a.ports.state.steam,beforeSteam);
   }
   const reopened=await disk.moduleAt('reopened-owner'),view=await reopened.boot.loadBootModes({overviewOnly:true});
   assert.equal(view.document.boot,after.extensions.bootModes.boot);assert.deepEqual(view.document.preparation,after.extensions.bootModes.preparation);
   rows.push({name,pass:true,mode,kind,actualFile:true,freshRepository:true,newInputAndDedicatedPreserved:true,oldModeOwnerNoCommit:kind==='mode',ordinaryEditContinued:kind==='ordinary',inputChangeDidNotConfirmOldPreparation:kind==='input'});
   disk.a.ports.hooks.steamStartupWrite=undefined;disk.flush();
  }

  report.sourceHashes=fingerprints([...sources]);
  report.pass=rows.length===(finalReplacement?4:options.ownershipCases?28:16)&&rows.every(row=>row.pass);report.durationMs=performance.now()-started;
 }catch(error){report.error=error.stack||String(error);throw error;}
 finally{
  if(native)await native.close();report.ownNativeClosed=!!native;
  report.sourceChanges=Object.entries(sourceHashes).filter(([file,expected])=>fingerprints([file])[file]!==expected).map(([file])=>file);
  if(report.sourceChanges.length)report.pass=false;
  fs.writeFileSync(path.join(out,'boot-modes-file-ui.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});
 }
 console.log(JSON.stringify({pass:report.pass,selection:report.selection,cases:rows.length,durationMs:report.durationMs}));
 if(!report.pass)process.exitCode=1;
}
