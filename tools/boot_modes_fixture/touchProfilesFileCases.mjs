import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {repo,runtimeBundle,hash,require,evidenceInputs,fingerprints} from './harness.mjs';
import {pathToFileURL} from 'node:url';
import {prepareDiskSettings,nativeInputs} from './diskSettings.mjs';
import * as touchProfilePreparation from './touchProfilesNative.mjs';
export async function runTouchProfilesFileEntry(out,args=[]){
 const selectors=args.filter(arg=>arg.startsWith('--touch-profiles-')&&arg!=='--touch-profiles-only'&&arg!=='--touch-profiles-producer');
 assert(selectors.every(arg=>arg.startsWith('--touch-profiles-from=')||arg.startsWith('--touch-profiles-cases=')),'Unknown touch-profile selection');
 const from=selectors.filter(arg=>arg.startsWith('--touch-profiles-from=')),only=selectors.filter(arg=>arg.startsWith('--touch-profiles-cases='));
 assert(from.length<=1&&only.length<=1&&!(from.length&&only.length),'Choose one exact touch-profile selector');
 const selection={from:from[0]?.split('=').slice(1).join('='),only:only[0]?.split('=').slice(1).join('=')};
 assert(!from.length||selection.from,'Empty touch-profile continuation');assert(!only.length||selection.only,'Empty touch-profile selection');
 const report={schema:1,caseId:'settings.boot-modes-file-ui',scope:'native-touch-profiles',wholeSuite:'NOT_RUN',pass:false,assertions:[],physicalReboot:false,installedApplication:false};
 const sourceFiles=[...nativeInputs,'tools/boot_modes_file_ui_selftest.mjs','tools/boot_modes_fixture/diskSettings.mjs','tools/boot_modes_fixture/touchProfilesNative.mjs','tools/boot_modes_fixture/touchProfilesFileCases.mjs','tools/boot_modes_fixture/harness.mjs','PowerControl/settings-defaults-v1.json'];
 const pins=Object.fromEntries(sourceFiles.map(file=>[file,hash(path.join(repo,file))]));let native;
 try{
  native=prepareDiskSettings(path.join(out,'native'),{storageRoot:out,touchProfiles:true,touchProfilePreparation});
  report.native=native.evidence;
  const frozen=JSON.parse(fs.readFileSync(path.join(out,'native/touch-source.json'),'utf8'));
  Object.assign(pins,Object.fromEntries(frozen.headers.map(row=>[row.path,row.sha256])));
  report.coverage=args.includes('--touch-profiles-producer')?await runTouchProfileProducerCases(native,out,report.assertions):await runTouchProfilesFileCases(native,out,report.assertions,selection);report.pass=true;
 }catch(error){report.error=error.stack||String(error);}
 finally{
  await native?.close();report.sourceHashes=pins;
  report.sourceDrift=Object.entries(pins).filter(([file,sha])=>!fs.existsSync(path.join(repo,file))||hash(path.join(repo,file))!==sha).map(([file])=>file);
  report.pass=report.pass&&report.sourceDrift.length===0;report.counts={passed:report.assertions.filter(row=>row.pass).length,failed:report.assertions.filter(row=>!row.pass).length};
  fs.writeFileSync(path.join(out,'boot-modes-file-ui.json'),JSON.stringify(report,null,2));
 }
 console.log(JSON.stringify({pass:report.pass,scope:report.scope,wholeSuite:report.wholeSuite,counts:report.counts,error:report.error?.split('\n')[0],sourceDrift:report.sourceDrift,report:path.join(out,'boot-modes-file-ui.json')},null,2));
 if(!report.pass)process.exitCode=1;return report;
}
export async function runTouchProfilesFileCases(native,out,assertions,selection={}){
 const seedOut=path.join(out,'touch-seed');fs.mkdirSync(seedOut,{recursive:true});
 const runtime=await runtimeBundle(seedOut),model=runtime.model;
 const defaults=JSON.parse(fs.readFileSync(path.join(repo,'PowerControl/settings-defaults-v1.json'),'utf8'));
 const first=path.join(native.root,'touch-profiles','defaults','yeman-settings.json');fs.mkdirSync(path.dirname(first),{recursive:true});fs.writeFileSync(first,JSON.stringify(defaults));
 const bank=await native.invoke(first,{operation:'touchDefaults'});
 const seed=await runtime.boot.captureBootModeSnapshot();seed.touch.profiles=structuredClone(bank.profiles);
 let serial=0;
 const scenarios=[],one=async(name,mode,mutate,verify)=>{scenarios.push({name,mode,mutate,verify,key:mode+'/'+name});};
 const execute=async(name,mode,mutate,verify)=>{
  const directory=path.join(native.root,'touch-profiles',String(++serial));fs.mkdirSync(directory,{recursive:true});
  const file=path.join(directory,'yeman-settings.json'),document=model.createBootModeDocument(seed);
  document.boot=mode;document.current=mode;document.revision=7;
  const value=structuredClone(defaults);value.screenTouchpads=structuredClone(bank);value.input.startupObservedBoot='file-fixture-boot-1';
  value.extensions={...value.extensions,peerExtension:{retained:true},bootModes:document};
  const args={profiles:Object.fromEntries(Object.keys(bank.profiles).map(persona=>[persona,{transparency:65}])),expectedProfiles:structuredClone(bank.profiles),origin:'boot-mode',bootContext:{boot:'file-fixture-boot-1',mode,revision:7,input:structuredClone(value.input),extensions:{bootModes:structuredClone(document)}}};
  const controls={active:-1};mutate?.({value,args,controls});
  const before=JSON.stringify(value,null,2);fs.writeFileSync(file,before);
  const receipt=await native.invoke(file,{operation:'touchProfiles',args,controls});
  const after=fs.readFileSync(file,'utf8'),saved=JSON.parse(after);
  const row={name,mode,pass:false,receipt};
  fs.writeFileSync(path.join(directory,'before.json'),before);fs.writeFileSync(path.join(directory,'after.json'),after);fs.writeFileSync(path.join(directory,'receipt.json'),JSON.stringify(receipt,null,2));
  try{
   assert.equal(receipt.runtime.initialized,controls.ownerInitialized??(controls.active>=0));assert.equal(receipt.runtime.presentationSuspended,true);
   assert.equal(receipt.runtime.touchWindows,false);assert.equal(receipt.runtime.windowCreateAttempts,0);assert.equal(receipt.runtime.injectedInputs,0);
   if(receipt.result.settingsSaved){
    assert.ok(['not-requested','pending','applied'].includes(receipt.result.activeApplication));
    assert.equal(receipt.result.applicationConfirmed,receipt.result.activeApplication==='applied');
    for(const {event,data} of receipt.events){
     assert.equal(event,'screenTouchpads.updated');assert.equal(data.settingsSaved,true);
     assert.equal(data.activeApplication,receipt.result.activeApplication);assert.equal(data.applicationConfirmed,receipt.result.applicationConfirmed);
     if(receipt.result.activeApplication!=='not-requested'){
      assert.equal(data.ok,receipt.result.ok);assert.equal(data.reason,receipt.result.reason);
      assert.deepEqual(receipt.result.activeResult,data,'return and event must contain the same complete owner observation');
     }
    }
   }
   await verify({receipt,before,after,value,saved,args,file});row.pass=true;
  }catch(error){row.error=error.stack||String(error);throw error;}
  finally{assertions.push(row);}
 };
 const committed=({receipt,saved,value,args})=>{
  assert.equal(receipt.result.settingsSaved,true);assert.equal(receipt.mainCommits,1);assert.equal(receipt.settingsLocks,1);
  if(args.origin)assert.deepEqual(saved.extensions,value.extensions,'boot/runtime batch must preserve global and independent preferences');
  for(const [persona,patch]of Object.entries(args.profiles))if(patch.transparency!==undefined)assert.equal(saved.screenTouchpads.profiles[persona].transparency,patch.transparency);
 };
 const rejected=({receipt,before,after})=>{assert.equal(receipt.result.settingsSaved,false);assert.equal(receipt.result.ok,false);assert.equal(receipt.mainCommits,0);assert.equal(after,before,'rejected batch partly changed real main bytes');assert.deepEqual(receipt.events,[]);};
 for(const mode of model.BOOT_MODE_IDS){
  await one('all four inactive persona patches commit one bank without live output devices',mode,null,context=>{committed(context);assert.equal(context.receipt.result.ok,true);assert.equal(context.receipt.runtime.appliedProfile,-1);assert.deepEqual(context.receipt.events,[]);});
  for(const kind of ['introduced','replaced','cleared','unchanged'])await one('boot claim '+kind,mode,({value,args})=>{
   if(kind!=='introduced')args.bootContext.extensions.bootModesClaimWriteId='fixture-claim-before';
   if(kind!=='cleared')value.extensions.bootModesClaimWriteId=kind==='unchanged'?'fixture-claim-before':'fixture-claim-after';
  },context=>{
   if(kind==='unchanged')committed(context);
   else {rejected(context);assert.equal(context.receipt.result.reason,'boot-mode-context-changed');}
  });
  await one('invalid final persona leaves every earlier persona and actual main file untouched',mode,({args})=>{args.profiles.steamdeck.transparency=-1;},rejected);
  await one('conflicting expected field rejects the entire bank',mode,({value})=>{value.screenTouchpads.profiles.elite.transparency=77;},context=>{rejected(context);assert.equal(context.receipt.result.reason,'profile-settings-conflict');});
  await one('unrelated profile edit survives a batch built from an older baseline',mode,({value})=>{value.screenTouchpads.profiles.disabled.scale=175;},context=>{committed(context);assert.equal(context.saved.screenTouchpads.profiles.disabled.scale,175);});
 }
 for(const field of ['boot','mode','revision'])await one('changed boot context '+field+' refuses every profile', 'global',({args})=>{args.bootContext[field]=field==='revision'?8:'wrong';},context=>{rejected(context);assert.equal(context.receipt.result.reason,'boot-mode-context-changed');});
 for(const field of ['origin','bootContext'])await one('incomplete runtime origin '+field+' cannot bypass its ownership check','steam',({args})=>{delete args[field];},context=>{rejected(context);assert.equal(context.receipt.result.reason,'invalid-profile-batch');});
 await one('new native Windows boot rejects the old batch identity','steam',({controls})=>{controls.observedBoot='file-fixture-boot-2';},rejected);
 await one('denied actual replacement commits neither the first nor last persona','xbox-steam',({controls})=>{controls.denyReplace=true;},context=>{rejected(context);assert.equal(context.receipt.settingsLocks,1);});
 for(const field of ['revision','hash','observed-boot'])await one('changed input '+field+' after request capture refuses the whole bank','steam',({value})=>{
  if(field==='revision')value.input.revision++;
  else if(field==='hash')value.input.outputTarget.persona='elite';
  else value.input.startupObservedBoot='file-fixture-boot-2';
 },rejected);
 for(const field of ['bootModesAppliedBoot','bootModesActiveBoot','bootModesStartupResult'])await one('changed ownership '+field+' after request capture refuses the whole bank','xbox',({value})=>{
  value.extensions[field]=field==='bootModesStartupResult'?{schema:1,boot:'new-result',mode:'xbox',sourceRevision:7,revision:8,status:'partial',applied:[],skipped:[],pending:[]}: 'new-owner-boot';
 },rejected);
 await one('same input ACK progress does not cancel the owned touch bank','steam',({value})=>{Object.assign(value.input,{appliedRevision:value.input.revision,appliedConfigHash:'fixture-proof',pendingConfigHash:'fixture-proof',hostAcknowledgedRevision:value.input.revision,hostAcknowledgedConfigHash:'fixture-proof',applyStatus:'active'});},context=>{committed(context);assert.equal(context.saved.input.hostAcknowledgedConfigHash,'fixture-proof');});
 await one('unrelated settings change remains outside touch ownership','global',({value})=>{value.extensions.peerExtension.current='retained-new-value';},context=>{committed(context);assert.equal(context.saved.extensions.peerExtension.current,'retained-new-value');});
 await one('reordered current mode object remains the same touch owner','global',({value})=>{value.extensions.bootModes=Object.fromEntries(Object.entries(value.extensions.bootModes).reverse());},committed);
 await one('uninitialized active owner saves but cannot confirm the configuration','global',({controls})=>{controls.active=0;controls.ownerInitialized=false;},context=>{committed(context);assert.equal(context.receipt.result.ok,false);assert.equal(context.receipt.result.activeApplication,'pending');assert.equal(context.receipt.result.applicationConfirmed,false);assert.equal(context.receipt.runtime.appliedProfile,-1);});
 await one('active profile uses the original configure path and emits its complete state','global',({controls})=>{controls.active=0;},context=>{committed(context);assert.equal(context.receipt.result.ok,true);assert.equal(context.receipt.runtime.appliedProfile,0);assert.equal(context.receipt.runtime.config.transparency,65);assert.equal(context.receipt.events.length,1);const event=context.receipt.events[0];assert.equal(event.event,'screenTouchpads.updated');assert.equal(event.data.persona,'disabled');assert.equal(event.data.transparency,65);assert.equal(typeof event.data.masterEnabled,'boolean');assert.equal(typeof event.data.layout,'string');});
 await one('unavailable active SteamDeck output saves the bank but never publishes a successful application','steam',({args,controls})=>{args.profiles.steamdeck={transparency:65,masterEnabled:true,enabled:true,layout:'dual',leftMode:'steamdeck',rightMode:'steamdeck'};controls.active=1;},context=>{committed(context);assert.equal(context.receipt.result.ok,false);assert.equal(context.receipt.result.reason,'active-profile-output-not-ready');assert.equal(context.receipt.runtime.appliedProfile,-1);assert.equal(context.receipt.events.length,1);assert.equal(context.receipt.events[0].data.ok,false,'event contradicted the failed active output result');});
 await one('retiring input owner keeps the saved bank pending instead of reporting applied','global',({controls})=>{controls.active=0;controls.teardown=true;},context=>{committed(context);assert.equal(context.receipt.result.ok,false,'retiring owner falsely acknowledged configuration');assert.equal(context.receipt.runtime.appliedProfile,-1);assert.equal(context.receipt.events[0].data.ok,false);});
 await one('native application failure preserves durable settings and an honest event','global',({controls})=>{controls.active=0;controls.windowError=5;},context=>{committed(context);assert.equal(context.receipt.result.ok,false);assert.equal(context.receipt.result.reason,'native-apply-failed');assert.equal(context.receipt.events[0].data.ok,false);});
 await one('native input failure is not hidden by a successful settings commit','global',({controls})=>{controls.active=0;controls.inputError=5;},context=>{committed(context);assert.equal(context.receipt.result.ok,false);assert.equal(context.receipt.result.activeResult.error,5);});
 for(const available of [false,true])await one('active PS5 mapping checks its actual output capability '+available,'xbox-steam',({args,controls})=>{args.profiles['dualsense-edge']={transparency:65,masterEnabled:true,enabled:true,layout:'single',singleMode:'dualsense'};controls.active=2;controls.psAvailable=available;},context=>{committed(context);assert.equal(context.receipt.result.ok,available);assert.equal(context.receipt.result.activeApplication,available?'applied':'pending');});
 await one('saving only inactive personas neither reapplies nor confirms the unrelated active profile','global',({args,controls})=>{args.profiles={steamdeck:{transparency:65}};controls.active=0;},context=>{committed(context);assert.equal(context.receipt.result.ok,true);assert.equal(context.receipt.result.activeApplication,'not-requested');assert.equal(context.receipt.runtime.appliedProfile,-1);});
 for(const change of ['active-profile','teardown','configuration','deck-output'])await one('owner observation changed during configure '+change+' cannot acknowledge the old request','steam',({args,controls})=>{controls.active=change==='deck-output'?1:0;controls.afterConfigure=change;if(change==='deck-output'){controls.steamDeckAvailable=true;args.profiles.steamdeck={transparency:65,masterEnabled:true,enabled:true,layout:'dual',leftMode:'steamdeck',rightMode:'steamdeck'};}},context=>{committed(context);assert.equal(context.receipt.result.ok,false);assert.equal(context.receipt.result.activeApplication,'pending');});
 for(const mode of model.BOOT_MODE_IDS)await one('ordinary whole-bank save projects only user fields into global in the same commit',mode,({args,value})=>{delete args.origin;delete args.bootContext;value.extensions.bootModesClaimWriteId='fixture-claim-current';value.extensions.bootModes.global.touch.profiles.disabled.scale=135;},context=>{committed(context);const before=context.value.extensions.bootModes,after=context.saved.extensions.bootModes;for(const persona of Object.keys(bank.profiles))assert.equal(after.global.touch.profiles[persona].transparency,65);assert.equal(after.global.touch.profiles.disabled.scale,135);assert.deepEqual(after.scenes,before.scenes);assert.deepEqual(context.saved.extensions.peerExtension,context.value.extensions.peerExtension);assert.equal(context.saved.extensions.bootModesClaimWriteId,context.value.extensions.bootModesClaimWriteId);});
 for(const [persona,patch]of [['disabled',{leftMode:'steamdeck'}],['elite',{singleMode:'dualsense'}],['disabled',{specialMask:3}],['elite',{rearMask:1}]])await one('inactive '+persona+' still rejects unsupported static persona mapping '+JSON.stringify(patch),'global',({args})=>{Object.assign(args.profiles[persona],patch);},context=>{rejected(context);assert.equal(context.receipt.result.reason,'persona-mapping-not-supported');});
 assert(!(selection.from!==undefined&&selection.only!==undefined),'Choose touch-profile exact cases or continuation');
 const names=scenarios.map(row=>row.key);assert.equal(new Set(names).size,names.length,'Duplicate original touch-profile key');
 const start=selection.from===undefined?0:names.indexOf(selection.from);assert(start>=0,'Unknown exact touch-profile continuation');
 const requested=selection.only?.split(',');if(requested)assert(requested.length&&new Set(requested).size===requested.length&&requested.every(key=>names.includes(key)),'Unknown or duplicate touch-profile case');
 const selected=requested?scenarios.filter(row=>requested.includes(row.key)):scenarios.slice(start);assert(selected.length,'Empty touch-profile execution');
 fs.writeFileSync(path.join(out,'touch-profile-selection.json'),JSON.stringify({declaredNames:names,selectedNames:selected.map(row=>row.key),selection},null,2),{flag:'wx'});
 for(const row of selected)await execute(row.name,row.mode,row.mutate,row.verify);
 return {nativeIpcExecuted:true,actualFileGuard:true,atomicPrimaryCommitMeasured:true,scenarios:serial,declaredScenarios:scenarios.length,selectedNames:selected.map(row=>row.key),deviceEnumeration:false,physicalController:false};
}

/** Production frontend serialization only. Native final-lock behavior is the
 * existing native-touch scope, never substituted by this IPC transport port. */
export async function productionTouchBridge(out){
 const esbuild=require('esbuild'),file=path.join(out,'touch-production-wire.mjs');
 const result=await esbuild.build({entryPoints:[path.join(repo,'src/bridge/screenTouchpads.ts')],bundle:true,platform:'node',format:'esm',write:false,metafile:true,plugins:[{name:'touch-owned-ipc-port',setup(build){build.onResolve({filter:/^\.\/ipc$/},args=>args.importer.endsWith('screenTouchpads.ts')?{path:'ipc',namespace:'touch-wire'}:undefined);build.onLoad({filter:/.*/,namespace:'touch-wire'},()=>({contents:'export const isNativeRuntime=true;export const invoke=(op,args)=>globalThis.__bootTouchWire.invoke(op,args);',loader:'js'}));}}]});
 fs.writeFileSync(file,result.outputFiles[0].text,{flag:'wx'});const bridge=await import(pathToFileURL(file));
 const inputs=evidenceInputs(result.metafile);assert(inputs.includes('touch-wire:ipc'),'Missing explicit IPC transport port');
 const sourceHashes=fingerprints([...inputs.filter(name=>name!=='touch-wire:ipc'),'tools/boot_modes_fixture/touchProfilesFileCases.mjs']);
 return {bridge,sourceHashes};
}
export async function runTouchProfileFrontendWire(out){
 const esbuild=require('esbuild'),{bridge,sourceHashes}=await productionTouchBridge(out),seedDirectory=path.join(out,'wire-seed');fs.mkdirSync(seedDirectory);
 const runtime=await runtimeBundle(seedDirectory),rows=[];
 const before=globalThis.__bootTouchWire;let call,release,count=0;
 globalThis.__bootTouchWire={invoke(op,args){count++;call={op,args:structuredClone(args)};return new Promise(resolve=>release=resolve);}};
 try{
  for(const mode of runtime.model.BOOT_MODE_IDS){
   const document=runtime.model.createBootModeDocument(await runtime.boot.captureBootModeSnapshot());document.current=mode;document.revision=7;
   const context={boot:'fixture-windows-boot-1',mode,revision:7,input:structuredClone(runtime.ports.settings.input),extensions:{bootModes:document}};
   const patch={disabled:{transparency:65}},expected={disabled:{transparency:80}},original=structuredClone(context),start=count;
   const pending=bridge.screenTouchpadsSaveProfiles(patch,expected,context);
   assert.equal(count,start+1);assert.equal(call.op,'screenTouchpads.saveProfiles');assert.equal(call.args.origin,'boot-mode');assert.deepEqual(call.args.bootContext,original);
   context.input.revision++;context.extensions.bootModes.revision++;patch.disabled.transparency=1;expected.disabled.transparency=2;
   assert.deepEqual(call.args.bootContext,original);assert.equal(call.args.profiles.disabled.transparency,65);assert.equal(call.args.expectedProfiles.disabled.transparency,80);
   release({ok:true,settingsSaved:true,reason:'saved',activeApplication:'not-requested',applicationConfirmed:false});assert.equal((await pending).settingsSaved,true);
   rows.push({mode,fullContextForwarded:true,submittedInputAndModeDetached:true,patchAndBaselineDetached:true});
  }
  const pending=bridge.screenTouchpadsSaveProfiles({disabled:{transparency:65}},{disabled:{transparency:80}});assert.equal('origin' in call.args,false);assert.equal('bootContext' in call.args,false);release({ok:true,settingsSaved:true,reason:'saved'});await pending;
  const beforeInvalid=count;await assert.rejects(()=>bridge.screenTouchpadsSaveProfiles({disabled:{transparency:65}},{disabled:{transparency:80}},{boot:'fixture',mode:'global',revision:7,input:{revision:-1},extensions:{bootModes:{schema:1}}}),/INVALID_BOOT_PREFERENCE_CONTEXT/);assert.equal(count,beforeInvalid);
  const current=fingerprints(Object.keys(sourceHashes));assert.deepEqual(current,sourceHashes);
  const summary={rows,ordinaryPayloadUnchanged:true,invalidOwnerRejectedBeforeIpc:true,actualBridge:true,nativeRequests:0,sourceHashes};
  fs.writeFileSync(path.join(out,'touch-production-wire.json'),JSON.stringify(summary,null,2),{flag:'wx'});return summary;
 }finally{globalThis.__bootTouchWire=before;esbuild.stop();}
}

/** Original boot producer -> actual serializer -> native configuration IPC.
 * Other feature owners remain the existing isolated fixture endpoints. */
export async function runTouchProfileProducerCases(native,out,assertions){
 const {createDiskRuntime}=await import('./diskSettings.mjs'),{bridge,sourceHashes}=await productionTouchBridge(out),rows=[];
 const prior=globalThis.__bootTouchWire;
 try{
  for(const mode of ['global','xbox','steam','xbox-steam'])for(const kind of ['current','late-input','late-claim']){
   const dir=path.join(out,'producer',mode,kind),disk=await createDiskRuntime(dir,native),a=disk.a,r=a.repository;
   await r.loadSettings();Object.assign(a.ports.settings,await r.loadSettings());
   const bank=await native.invoke(disk.file,{operation:'touchDefaults'}),seed=disk.read();
   seed.screenTouchpads=structuredClone(bank);seed.input.startupObservedBoot='file-fixture-boot-1';disk.writeRaw(seed);
   Object.assign(a.ports.settings,await r.loadSettings());a.ports.state.touch=structuredClone(bank.profiles);
   const doc=a.model.createBootModeDocument(await a.boot.captureBootModeSnapshot());doc.current=mode;doc.boot=mode;doc.revision=7;
   for(const profile of Object.values(doc.global.touch.profiles))profile.transparency=65;
   for(const scene of Object.values(doc.scenes))delete scene.overrides.touch;
   const start=disk.read();start.extensions.bootModes=structuredClone(doc);start.extensions.bootModesClaimWriteId='fixture-current-producer-claim';disk.writeRaw(start);await r.loadSettings();
   let calls=0,reply,beforeNative,wireArgs;
   globalThis.__bootTouchWire={async invoke(op,args){
    assert.equal(op,'screenTouchpads.saveProfiles');calls++;wireArgs=structuredClone(args);
    if(kind==='late-input'){
     const current=await r.loadSettings(),cas=await r.compareAndSwapInputSettings(current.input.revision,{gyroMotion:{gyroWeight:current.input.gyroMotion.gyroWeight+.25}},undefined,{origin:'runtime'});assert(cas.ok,'Peer input save must precede stale touch request');
    }
    if(kind==='late-claim'){
     const peer=disk.read();peer.extensions.bootModesClaimWriteId='fixture-peer-producer-claim';disk.writeRaw(peer);
    }
    beforeNative=disk.read();reply=await native.invoke(disk.file,{operation:'touchProfiles',args,controls:{active:-1}});
    Object.assign(a.ports.settings,await r.loadSettings());return reply.result;
   }};
   a.ports.hooks.touchWrite=async context=>{const request=a.ports.calls.filter(row=>row.op==='touch.saveProfiles').at(-1).value;return bridge.screenTouchpadsSaveProfiles(request.profiles,request.expectedProfiles,context);};
   let result,error;try{result=await a.boot.applyBootMode(mode,doc,{startup:true});}catch(e){error=String(e);}
   assert.equal(calls,1,'Actual producer did not reach original native touch handler');
   assert.equal(wireArgs.bootContext.extensions.bootModesClaimWriteId,start.extensions.bootModesClaimWriteId);
   const after=disk.read();assert.equal(reply.runtime.windowCreateAttempts,0);assert.equal(reply.runtime.touchWindows,false);assert.equal(reply.runtime.injectedInputs,0);
   if(kind==='current'){
    assert.equal(error,undefined,error);assert.equal(reply.result.settingsSaved,true);assert.equal(reply.mainCommits,1);assert.equal(result.receipt.modules.touch.state,'saved');
    for(const profile of Object.values(after.screenTouchpads.profiles))assert.equal(profile.transparency,65);
   }else{
    assert(error,'Stale producer was accepted');assert.equal(reply.result.settingsSaved,false);assert.equal(reply.result.reason,'boot-mode-context-changed');assert.equal(reply.mainCommits,0);assert.deepEqual(after,beforeNative,'Old producer changed files after ownership changed');
    for(const [persona,profile]of Object.entries(after.screenTouchpads.profiles))assert.equal(profile.transparency,bank.profiles[persona].transparency);
   }
   assert.equal(after.input.revision,beforeNative.input.revision);assert.deepEqual(after.gameProfiles,start.gameProfiles);
   const reopened=await disk.moduleAt('reopened'),loaded=await reopened.repository.loadSettings();assert.deepEqual(loaded.screenTouchpads,after.screenTouchpads);
   const row={name:mode+'/'+kind,mode,kind,pass:true,actualNativeRequests:calls,error:error??null,reply,wireArgs,after};rows.push(row);assertions.push(row);disk.flush();
   a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();
  }
  assert.deepEqual(fingerprints(Object.keys(sourceHashes)),sourceHashes);
  fs.writeFileSync(path.join(out,'touch-production-integration.json'),JSON.stringify({rows,actualRows:rows.length,sourceHashes},null,2),{flag:'wx'});
  return {actualProducerRows:rows.length,actualSerializedNativeIpc:true,actualRepositoryAndFiles:true,physicalInput:false};
 }finally{globalThis.__bootTouchWire=prior;}
}
