import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {compileSettingsConsumer,createSettingsFixture} from '../fixtures/settings_baseline.mjs';

export const preferenceOwnerKinds=['revision','hash','mode','result','observed','applied','active','ack','unrelated','order','current','claim'];
export function preferenceOwnerSelection(args=[]){
 const modes=['global','xbox','steam','xbox-steam'],writers=['update','section'],stages=['queue','commit'];
 const one=name=>{
  const prefix='--preference-owner-'+name+'=',values=args.filter(arg=>arg.startsWith(prefix));
  assert(values.length<=1,'Duplicate preference owner '+name);
  if(!values.length)return undefined;
  const value=values[0].slice(prefix.length);assert(value,'Empty preference owner '+name);return value;
 };
 const requested={mode:one('mode'),writer:one('writer'),stage:one('stage'),kind:one('kind')};
 const availableWriters=[...writers,'replace'];
 for(const [key,choices]of Object.entries({mode:modes,writer:availableWriters,stage:stages,kind:preferenceOwnerKinds}))assert(!requested[key]||choices.includes(requested[key]),'Unknown preference owner '+key);
 const selected=modes.flatMap(mode=>(requested.writer==='replace'?['replace']:writers).flatMap(writer=>stages.flatMap(stage=>preferenceOwnerKinds.map(kind=>({mode,writer,stage,kind})))))
  .filter(row=>Object.entries(requested).every(([key,value])=>!value||row[key]===value));
 const from=args.filter(arg=>arg.startsWith('--preference-owner-from='));
 assert(from.length<=1,'Duplicate preference owner continuation');
 if(!from.length)return selected;
 const key=from[0].slice('--preference-owner-from='.length),index=selected.findIndex(row=>[row.mode,row.writer,row.stage,row.kind].join(':')===key);
 assert(key&&index>=0,'Unknown preference owner continuation');return selected.slice(index);
}
export async function runPreferenceOwnerCases(out,args=[],options={}){
 const selected=options.selected??preferenceOwnerSelection(args),rows=[],root=process.cwd(),files=[];
 const compiled=options.compiled??await compileSettingsConsumer(root,{uiSettings:options.uiSettings===true,onCompiled:result=>{
  for(const file of Object.keys(result.metafile.inputs).filter(file=>!file.startsWith('<')))files.push(path.resolve(root,file));
 }});
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const pins=files.map(file=>({path:file,sha256:hash(file)}));
 fs.writeFileSync(path.join(out,'preference-owner-consumer.cjs'),compiled,{flag:'wx'});
 const raw=path.join(out,'preference-owner-observations.jsonl');fs.closeSync(fs.openSync(raw,'wx'));
 const prototype=createSettingsFixture(compiled).repo.normalizeSettings({});
 for(const selection of selected){
  const {mode,writer,stage,kind}=selection,initial=structuredClone(prototype);
  initial.input.revision=7;initial.input.startupObservedBoot='owned-preference-boot';
  initial.extensions={bootModes:{schema:1,revision:3,current:mode,boot:mode,global:{}},
   bootModesAppliedBoot:'owned-preference-boot',bootModesActiveBoot:'owned-preference-boot',
   bootModesStartupResult:{schema:1,boot:'owned-preference-boot',mode,sourceRevision:2,revision:3,status:'applied',applied:[],skipped:[],pending:[]}};
  if(kind==='claim')initial.extensions.bootModesClaimWriteId='owned-preference-claim';
  const backup=structuredClone(initial);backup.ui.backgroundOpacity=0.23;
  const f=(options.createFixture??createSettingsFixture)(compiled,initial,backup,selection);
  await f.repo.loadSettings();
  if(writer==='ui')await f.ui.loadUiSettings();
  const loaded=await f.repo.loadSettings(),context={input:structuredClone(loaded.input),extensions:structuredClone(loaded.extensions)};
  const opts={origin:'boot-mode',bootContext:context},writes=f.state.writes.length;
  const oldBackup=f.text(f.state.file+'.bak');
  let mutations=0;
  const mutate=()=>{
   const current=f.document();++mutations;
   if(kind==='revision')current.input.revision++;
   if(kind==='hash')current.input.diagnostics.inputLoggingEnabled=!current.input.diagnostics.inputLoggingEnabled;
   if(kind==='mode')current.extensions.bootModes.revision++;
   if(kind==='result')current.extensions.bootModesStartupResult.revision++;
   if(kind==='observed')current.input.startupObservedBoot='new-boot';
   if(kind==='applied')current.extensions.bootModesAppliedBoot='new-applied';
   if(kind==='active')current.extensions.bootModesActiveBoot='new-active';
   if(kind==='claim')current.extensions.bootModesClaimWriteId='peer-preference-claim';
   if(kind==='ack')current.input.hostAcknowledgedRevision=7;
   if(kind==='unrelated')current.extensions.unrelated={keep:true};
   if(kind==='order')current.extensions=Object.fromEntries(Object.entries(current.extensions).reverse());
   f.seed(f.state.file,current);
  };
  if(stage==='queue')mutate();else f.state.beforeWrite=mutate;
  const desired=0.37;let error='';
  try{
   if(writer==='update')await f.repo.updateSettings(s=>{s.ui.backgroundOpacity=desired;},opts);
   else if(writer==='ui')await f.ui.setUiSettings({backgroundOpacity:desired},opts);
   else if(writer==='replace')await f.repo.replaceSettingsSection('ui',{backgroundOpacity:desired},opts);
   else await f.repo.saveSettingsSection('ui',{backgroundOpacity:desired},f.repo.getSettingsGeneration(),opts);
  }catch(e){error=String(e);}
  const allowed=['ack','unrelated','order','current'].includes(kind),actual=f.document();
  const row={...selection,allowed,mutations,error,finalOpacity:actual.ui.backgroundOpacity,
   committed:f.state.writes.length-writes,backupPreserved:f.text(f.state.file+'.bak')===oldBackup};
  fs.appendFileSync(raw,JSON.stringify(row)+'\n');
  if(allowed){assert(!error,JSON.stringify(row));assert.equal(actual.ui.backgroundOpacity,desired,JSON.stringify(row));assert.equal(row.committed,1);}
  else {assert(error,'stale boot preference write was accepted: '+JSON.stringify(row));assert.notEqual(actual.ui.backgroundOpacity,desired);assert.equal(row.committed,0);assert(row.backupPreserved,'rejected stale preference changed backup');}
  if(allowed){const request=f.state.writes.at(-1);assert.equal(request.guardBootMode,true);assert.equal(request.expectedInputRevision,7);}
  assert.equal(mutations,1,'Selected original queue/commit boundary did not run exactly once');
  rows.push(row);
 }
 for(const pin of pins)assert.equal(hash(pin.path),pin.sha256,'Preference source changed during execution '+pin.path);
 const result={status:'PASS',selected,rows,actualRows:rows.length,scope:options.scope??'Actual repository/API/IPC; memory native settings terminal',
  sourcePins:pins,realHardwareOperations:0,fullDefault:'NOT_RUN'};
 fs.writeFileSync(path.join(out,'preference-owner-result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}

export async function runPreferenceUiOwner(out,args=[]){
 const selected=['global','xbox','steam','xbox-steam'].flatMap(mode=>['hash','mode','current'].map(kind=>({mode,writer:'ui',stage:'commit',kind})));
 return runPreferenceOwnerCases(out,args,{uiSettings:true,selected,scope:'Actual UI setter queue, repository/API/IPC; memory native settings terminal'});
}

export async function runFirstDocumentSourceFileCases(out,args=[],options={}){
 assert(args.filter(a=>a.startsWith('--first-document-')).every(a=>a==='--first-document-source-native'||a.startsWith('--first-document-native-main=')||a.startsWith('--first-document-source-from=')||a.startsWith('--first-document-source-only=')),'Unknown first-document native option');
 const root=process.cwd(),option=name=>{const prefix=name+'=',values=args.filter(a=>a.startsWith(prefix));assert(values.length<=1,'duplicate '+name);if(values.length)assert(values[0].length>prefix.length,'Empty '+name);return values[0]?.slice(prefix.length);};
 const mainSource=option('--first-document-native-main');
 if(mainSource)assert(path.isAbsolute(mainSource)&&fs.statSync(mainSource).isFile(),'Explicit native source must exist');
 const {prepareLockedSettings}=await import('./lockedSettings.mjs');
 const {nativeInputs}=await import('./diskSettings.mjs');
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const inputFiles=[...nativeInputs.map(name=>path.join(root,name)),...(mainSource?[mainSource]:[])];
 const pins=inputFiles.map(file=>({path:file,bytes:fs.statSync(file).size,sha256:hash(file)}));
 const compiled=await compileSettingsConsumer(root),base=createSettingsFixture(compiled).repo.normalizeSettings({});
 let native;
 const rows=[],observation=path.join(out,'first-document-source-observations.jsonl');fs.closeSync(fs.openSync(observation,'wx'));
 const changed=[
  ['performance','/performanceSchedule',d=>({...d.performanceSchedule,enabled:!d.performanceSchedule.enabled})],
  ['frames','/rtss/frameRates',()=>({ac:{fps:80,lastFps:80,ceiling:120},dc:{fps:40,lastFps:40,ceiling:60}})],
  ['frame-defaults','/rtss/templateDefaults',d=>!d.rtss.templateDefaults],
  ['monitor','/rtss/monitorOutput',()=>({enabled:true,ymccEnabled:true,rtssEnabled:false,output:'ymcc'})],
  ['startup-persona','/startupDesired/virtualGamepadPersona',()=> 'steamdeck'],
  ['startup-gyro','/startupDesired/gyroPreset',()=> 'fps'],
  ['startup-fan','/startupDesired/fanControl',d=>!d.startupDesired.fanControl],
  ['mouse-policy','/gamepad/mouseMode',d=>d.gamepad.mouseMode==='off'?'on':'off'],
  ['mouse-backend','/gamepad/mouseBackend',d=>d.gamepad.mouseBackend==='gamebar'?'joyxoff':'gamebar'],
  ['fan-preset','/fan/preset',d=>d.fan.preset==='balanced'?'aggressive':'balanced'],
  ['sleep','/sleep',d=>({...d.sleep,pauseGameOnSleep:!d.sleep.pauseGameOnSleep})],
  ['touch','/screenTouchpads/profiles',()=>({disabled:{masterEnabled:false,transparency:48}})],
  ['decky','/extensions/deckySidebar/enabled',()=>true],
  ['theme','/ui/theme',d=>d.ui.theme==='blue-black'?'green-black':'blue-black'],['dynamic','/ui/dynamicBackgroundEnabled',d=>!d.ui.dynamicBackgroundEnabled],
  ['opacity','/ui/backgroundOpacity',()=>0.23],['blur','/ui/backgroundBlur',()=>9],
  ['video-power','/ui/videoBatteryPause',d=>!d.ui.videoBatteryPause],['wallpaper','/ui/steamDynamicWallpaperEnabled',d=>!d.ui.steamDynamicWallpaperEnabled],
  ['wallpaper-download','/ui/steamWallpaperBackgroundDownload',d=>!d.ui.steamWallpaperBackgroundDownload],['wallpaper-cpu','/ui/steamWallpaperDownloadCpuThreshold',()=>23],
  ['input','/input',d=>({...d.input,revision:d.input.revision+1})],
 ];
 const set=(d,p,value)=>{const parts=p.split('/').slice(1);let node=d;for(const k of parts.slice(0,-1))node=node[k]??={};node[parts.at(-1)]=value;};
 const positive=[['current',()=>{}],['extension',d=>{d.extensions.peer={keep:true};}],['window',d=>{d.ui.windowPlacement='left';}],['radial',d=>{d.ui.radialTestingEnabled=true;}],['common-startup',d=>{d.startupDesired.networkAccelerationOnBoot=!d.startupDesired.networkAccelerationOnBoot;}],['domain-log',d=>{d.domainLogs={...d.domainLogs,fixture:true};}],['ack',d=>{Object.assign(d.input,{pendingConfigHash:'current-proof',hostAcknowledgedRevision:d.input.revision,hostAcknowledgedConfigHash:'current-proof',appliedRevision:d.input.revision,appliedConfigHash:'current-proof',applyStatus:'active'});}],['order',d=>{d.ui=Object.fromEntries(Object.entries(d.ui).reverse());}]];
 const scenarios=['global','xbox','steam','xbox-steam'].flatMap(mode=>[...changed.map(([name,path,make])=>({name,mutate:d=>set(d,path,make(d)),accept:['startup-fan','fan-preset','sleep'].includes(name),existing:false})),...positive.map(([name,mutate])=>({name,mutate,accept:true,existing:false})),{name:'existing-template',mutate:d=>{d.ui.backgroundOpacity=0.23;},accept:true,existing:true}].map(scenario=>({mode,scenario})));
 const from=option('--first-document-source-from'),first=from?scenarios.findIndex(({mode,scenario})=>mode+'/'+scenario.name===from):0;
 assert(first>=0,'Unknown exact first-document continuation');
 const only=option('--first-document-source-only'),names=only?.split(',');assert(!(from&&only),'Choose exact first-document selection or continuation');
 if(names)assert(new Set(names).size===names.length&&names.every(name=>scenarios.some(s=>s.mode+'/'+s.scenario.name===name)),'Unknown or duplicate exact first-document selection');
 const selected=names?scenarios.filter(s=>names.includes(s.mode+'/'+s.scenario.name)):scenarios.slice(first);
 native=options.native??prepareLockedSettings(path.join(out,'native'),{storageRoot:out,mainSource});
 try{
  let sequence=first;
  for(const {mode,scenario} of selected){
   const before=structuredClone(base);before.ui.backgroundOpacity=0.64;delete before.extensions.bootModes;
   const template={schema:1,revision:1,current:mode,boot:mode,bootEnabled:mode!=='global',global:{background:{ui:{backgroundOpacity:0.64}}},activeSnapshot:{},scenes:{},presets:{}};
   if(scenario.existing)before.extensions.bootModes=structuredClone(template);
   const wanted=structuredClone(before);wanted.extensions.bootModes=structuredClone(template);if(scenario.existing)wanted.extensions.bootModes.revision++;
   const newer=structuredClone(before);scenario.mutate(newer);
   if(!scenario.accept)assert.notDeepEqual(newer,before,'selected source mutation must be effective: '+scenario.name);
   const dir=path.join(out,'files',String(++sequence));fs.mkdirSync(dir,{recursive:true});const file=path.join(dir,'yeman-settings.json'),raw=JSON.stringify(newer,null,2);fs.writeFileSync(file,raw,{flag:'wx'});
   const reply=await native.invoke(file,{operation:'write',content:JSON.stringify(wanted),baseline:before});
   const finalRaw=fs.readFileSync(file,'utf8'),final=JSON.parse(finalRaw),row={mode,name:scenario.name,expectedAccepted:scenario.accept,reply,before,wanted,newer,final,sourcePreserved:finalRaw===raw,backup:fs.existsSync(file+'.bak')};
   fs.appendFileSync(observation,JSON.stringify(row)+'\n');
   assert.equal(reply.ok,scenario.accept,'final first-document ownership: '+mode+'/'+scenario.name);
   if(!scenario.accept){assert.equal(finalRaw,raw,'stale creation wrote user configuration');assert(!row.backup,'rejected creation rotated backup');}
   else{assert.deepEqual(final.extensions.bootModes,wanted.extensions.bootModes);assert(row.backup);if(['startup-fan','fan-preset'].includes(scenario.name)){assert.deepEqual(final.startupDesired,newer.startupDesired);assert.deepEqual(final.fan,newer.fan);}if(scenario.name==='sleep')assert.deepEqual(final.sleep,newer.sleep);}
   rows.push({mode,name:scenario.name,accepted:reply.ok});
  }
  assert.equal(rows.length,selected.length);assert.equal(rows.filter(r=>!r.accepted).length,selected.filter(r=>!r.scenario.accept).length);
  return {status:'PASS',rows,actualRows:rows.length,scope:'Actual original public native file writer and owned Windows settings mutex; OS input terminal isolated; not installed/physical acceptance'};
 }finally{
  await native.close();
  const after=inputFiles.map(file=>({path:file,bytes:fs.statSync(file).size,sha256:hash(file)}));
  fs.writeFileSync(path.join(out,'first-document-source-result.json'),JSON.stringify({rows,actualRows:rows.length,complete:rows.length===selected.length,selectionFrom:from??null,selectedNames:selected.map(({mode,scenario})=>mode+'/'+scenario.name),sourcePins:pins,sourceAfter:after,originalLockedSource:native.sourceIntake(),nativeEvidence:native.evidence,actualHardwareOperations:0},null,2),{flag:'wx'});
  assert.deepEqual(after,pins,'Native source changed during exact first-document scope');
 }
}

export async function runFirstEditorSourceCases(out,args=[],options={}){
 const {createDiskRuntime}=await import('./diskSettings.mjs');
 const native=options.native;assert(native&&typeof native.invoke==='function','Explicit owned original native writer required');
 const rows=[],resultFile=path.join(out,'first-editor-source-result.json');
 const put=(d,p,v)=>{const keys=p.split('/');let n=d;for(const k of keys.slice(0,-1))n=n[k]??={};n[keys.at(-1)]=v;};
 const sources=[
  ['performance','performanceSchedule',d=>{const v=structuredClone(d.performanceSchedule);v.enabled=!v.enabled;return v;}],
  ['frames','rtss/frameRates',()=>({ac:{fps:80,lastFps:80,ceiling:120},dc:{fps:40,lastFps:40,ceiling:60}})],
  ['frame-defaults','rtss/templateDefaults',d=>!d.rtss.templateDefaults],
  ['monitor','rtss/monitorOutput',()=>({enabled:true,ymccEnabled:true,rtssEnabled:false,output:'ymcc'})],
  ['persona','startupDesired/virtualGamepadPersona',()=> 'steamdeck'],
  ['gyro-preset','startupDesired/gyroPreset',d=>d.startupDesired.gyroPreset==='off'?'fps':'off'],
  ['fan-enabled','startupDesired/fanControl',d=>!d.startupDesired.fanControl],
  ['mouse-mode','gamepad/mouseMode',d=>d.gamepad.mouseMode==='off'?'on':'off'],
  ['mouse-backend','gamepad/mouseBackend',d=>d.gamepad.mouseBackend==='gamebar'?'joyxoff':'gamebar'],
  ['fan-preset','fan/preset',d=>d.fan.preset==='balanced'?'aggressive':'balanced'],
  ['sleep','sleep',d=>({...d.sleep,pauseGameOnSleep:!d.sleep.pauseGameOnSleep})],
  ['touch','screenTouchpads/profiles',()=>({disabled:{masterEnabled:false,transparency:47}})],
  ['decky','extensions/deckySidebar/enabled',d=>!d.extensions.deckySidebar?.enabled],
  ['theme','ui/theme',d=>d.ui.theme==='blue-black'?'green-black':'blue-black'],
  ['dynamic','ui/dynamicBackgroundEnabled',d=>!d.ui.dynamicBackgroundEnabled],
  ['opacity','ui/backgroundOpacity',()=>.23],['blur','ui/backgroundBlur',()=>9],
  ['video-power','ui/videoBatteryPause',d=>!d.ui.videoBatteryPause],
  ['wallpaper','ui/steamDynamicWallpaperEnabled',d=>!d.ui.steamDynamicWallpaperEnabled],
  ['wallpaper-download','ui/steamWallpaperBackgroundDownload',d=>!d.ui.steamWallpaperBackgroundDownload],
  ['wallpaper-cpu','ui/steamWallpaperDownloadCpuThreshold',()=>23],
 ];
 const changes=[...sources.map(([name,p,make])=>({name,kind:'ordinary',change:async()=>{const current=await r.loadSettings(),value=make(current);await r.updateSettings(d=>put(d,p,value));}})),
  {name:'input-intent',kind:'input-cas',change:async()=>{const d=await r.loadSettings();const reply=await r.compareAndSwapInputSettings(d.input.revision,{gyroMotion:{...d.input.gyroMotion,gyroWeight:2.5}});assert(reply.ok);}},
 ];
 const positives=[
  {name:'unchanged',change:async()=>{}},
  {name:'window',change:async()=>r.saveSettingsSection('ui',{windowPlacement:'left'})},
  {name:'peer',change:async()=>r.updateSettings(d=>{d.extensions.peer={keep:true};})},
  {name:'common-startup',change:async()=>r.saveSettingsSection('startupDesired',{networkAccelerationOnBoot:true})},
  {name:'ack-only',change:async()=>{const d=disk.read();Object.assign(d.input,{appliedRevision:d.input.revision,appliedConfigHash:'proof',pendingConfigHash:'proof',hostAcknowledgedRevision:d.input.revision,hostAcknowledgedConfigHash:'proof',applyStatus:'active'});disk.writeRaw(d);await r.loadSettings();}},
  {name:'reordered',change:async()=>{const d=disk.read();d.ui=Object.fromEntries(Object.entries(d.ui).reverse());disk.writeRaw(d);await r.loadSettings();}},
 ];
 const scenarios=['global','xbox','steam','xbox-steam'].flatMap(mode=>['save','choose'].flatMap(action=>[
  ...changes.map(c=>({mode,action,...c,accept:['fan-enabled','fan-preset','sleep'].includes(c.name)})),...positives.map(c=>({mode,action,...c,accept:true})),
  {mode,action,name:'missing-token-clone',change:async()=>{},accept:false,missing:true},
  {mode,action,name:'foreign-token',change:async()=>{},accept:false,foreign:true},
  {mode,action,name:'automatic-source-reference',change:async()=>{},accept:true,direct:true},
 ]));
 for(const mode of ['global','xbox','steam','xbox-steam'])for(const action of ['save','choose']){
  scenarios.push({mode,action,name:'settings-generation',change:async()=>{r.clearSettingsCache();await r.loadSettings();},accept:false});
  scenarios.push({mode,action,name:'queued-source',change:async()=>{},queued:true,accept:false});
 }
 assert(args.filter(x=>x.startsWith('--editor-source-')).every(x=>x.startsWith('--editor-source-from=')||x.startsWith('--editor-source-only=')),'Unknown editor source option');
 const fromArgs=args.filter(x=>x.startsWith('--editor-source-from='));assert(fromArgs.length<=1,'Duplicate editor source continuation');
 const from=fromArgs.length?fromArgs[0].slice('--editor-source-from='.length):undefined;
 const key=s=>[s.mode,s.action,s.name].join('/'),start=from===undefined?0:scenarios.findIndex(s=>key(s)===from);
 assert(start>=0,'Unknown exact editor source continuation');
 const onlyArgs=args.filter(x=>x.startsWith('--editor-source-only='));assert(onlyArgs.length<=1,'Duplicate exact editor source selection');
 const only=onlyArgs[0]?.slice('--editor-source-only='.length),names=only?.split(',');
 assert(!(from!==undefined&&only!==undefined),'Choose exact editor source selection or continuation');
 if(only!==undefined)assert(names?.length&&new Set(names).size===names.length&&names.every(name=>scenarios.some(s=>key(s)===name)),'Unknown or duplicate exact editor source selection');
 const selected=names?scenarios.filter(s=>names.includes(key(s))):scenarios.slice(start);
 const disk=await createDiskRuntime(path.join(out,'editor-source'),native,{bootModesSource:options.bootModesSource});
 const a=disk.a,r=a.repository;await r.loadSettings();await r.saveSettingsSection('ui',{backgroundOpacity:.64});
 const seed=structuredClone(disk.read());
 try{
  for(const s of selected){
   a.ports.reset();const scenarioSeed=structuredClone(seed);if(s.name==='gyro-preset')scenarioSeed.startupDesired.virtualGamepadPersona='elite';disk.writeRaw(scenarioSeed);const settings=await r.loadSettings();Object.assign(a.ports.settings,settings);
   const context=await a.boot.loadBootModes({editorOnly:true});
   assert.equal(disk.read().extensions.bootModes,undefined,'Read must not materialize a document');
   const draft=s.direct?context.document:a.model.cloneBootValue(context.document);
   draft.scenes.steam.name='Keep my unsaved draft '+s.mode;
   if(s.mode!=='global')draft.scenes[s.mode].overrides.background={ui:{backgroundBlur:7}};
   const originalDraft=structuredClone(draft);
   const source=s.foreign?Object.freeze({generation:context.initialSource?.generation}):s.missing||s.direct?undefined:context.initialSource;
   const submit=()=>s.action==='save'?a.boot.saveBootModes(draft,draft.revision,source):a.boot.chooseBootMode(s.mode,draft,true,source);
   let settled;
   if(s.queued){
    let entered,release;const ready=new Promise(resolve=>entered=resolve),hold=new Promise(resolve=>release=resolve);
    const writer=r.updateSettings(async d=>{d.ui.backgroundOpacity=.23;entered();await hold;});await ready;
    const pending=submit().then(saved=>({saved}),e=>({error:String(e)}));release();await writer;settled=pending;
   }else await s.change();
   const before=disk.read(),beforeTrace=disk.trace.length,beforeCalls=a.ports.calls.length;
   if(s.kind)assert.notDeepEqual(before,settings,'Selected ordinary source change must be effective '+key(s));
   const {saved,error}=await (settled??submit().then(saved=>({saved}),e=>({error:String(e)})));
   const after=disk.read();rows.push({key:key(s),accepted:!!saved,error,expectedAccepted:s.accept,sourcePresent:context.initialSource!==undefined,
    actualNewNativeWrites:disk.trace.slice(beforeTrace).filter(x=>x.op==='write').length});
   if(!s.accept){
    assert(error&&/来源|变化|读取/.test(error),'First editor source conflict must reject '+key(s));
    assert.deepEqual(after,before,'Rejected old editor changed settings '+key(s));
    assert.equal(disk.trace.slice(beforeTrace).filter(x=>x.op==='write').length,0,'Rejected old editor entered native write '+key(s));
    assert(!a.ports.calls.slice(beforeCalls).some(x=>x.op.startsWith('startup.')||x.op==='steam.master'),'Rejected first select reached startup preparation');
   }else{
    assert.equal(error,undefined,key(s));assert(saved&&saved.revision>=1);
    assert.equal(after.startupDesired.fanControl,before.startupDesired.fanControl,'Mode save overwrote common fan startup '+key(s));
    assert.deepEqual(after.fan,before.fan,'Mode save overwrote common fan preset or curve '+key(s));
    assert.equal(after.extensions.bootModes.global.background.ui.backgroundOpacity,.64);
    assert.equal(after.extensions.bootModes.scenes.steam.name,originalDraft.scenes.steam.name);
    if(s.mode!=='global')assert.deepEqual(after.extensions.bootModes.scenes[s.mode].overrides.background,originalDraft.scenes[s.mode].overrides.background);
    if(s.name==='window')assert.equal(after.ui.windowPlacement,'left');
    if(s.name==='peer')assert.deepEqual(after.extensions.peer,{keep:true});
    if(s.name==='common-startup')assert.equal(after.startupDesired.networkAccelerationOnBoot,true);
    const serialized=JSON.stringify(after);assert(!serialized.includes('initialSource'),'Transient source leaked to configuration');
   }
   assert.deepEqual(draft,originalDraft,'Saving or rejecting mutated editable draft '+key(s));
   assert.equal(context.initialSource===undefined,false,'Original read must supply an opaque source');
  }
  assert.equal(rows.length,selected.length);
  return {rows,actualRows:rows.length,rejected:rows.filter(x=>!x.accepted).length,accepted:rows.filter(x=>x.accepted).length,
   sourceChecking:'Original first editor load and cloned draft to actual repository, original native writer; ACK/order are injected original Host/file boundary inputs',
   nativeRecompiled:false,sourceWasPersisted:false};
 }finally{
  fs.writeFileSync(resultFile,JSON.stringify({rows,actualRows:rows.length,selectionFrom:from??null,selectedNames:selected.map(key),complete:rows.length===selected.length,trace:disk.trace},null,2),{flag:'wx'});
 }
}

export async function runOrdinarySourceParity(out,args=[],options={}){
 const specs=[],modes=['global','xbox','steam','xbox-steam'],fields=['decky','recovery'];
 const add=s=>specs.push({...s,key:[s.mode,s.field,s.kind,s.on?'on':'off',s.method??'section',s.origin??'default'].join('/')});
 for(const mode of modes)for(const field of fields)for(const on of [false,true]){
  for(const method of ['section','update','replace'])for(const origin of ['default','boot-mode','runtime'])add({mode,field,on,method,origin,kind:'changed'});
  for(const method of ['section','update','replace'])add({mode,field,on,method,origin:'ordinary',kind:'same-value'});
 }
 for(const mode of modes){
  for(const field of fields)for(const kind of ['sibling','missing','atomic-failure','queued-immutable','future-mode','max-revision'])add({mode,field,on:true,kind});
  for(const on of [false,true]){add({mode,field:'decky',on,kind:'legacy-ui'});add({mode,field:'decky',on,kind:'contrary-legacy'});}
  add({mode,field:'decky',on:true,kind:'malformed-source'});
 }
 assert(args.filter(x=>x.startsWith('--ordinary-source-')).every(x=>x.startsWith('--ordinary-source-only=')),'Unknown ordinary source option');
 const onlyArgs=args.filter(x=>x.startsWith('--ordinary-source-only='));assert(onlyArgs.length<=1,'Duplicate ordinary source selection');
 const only=onlyArgs.length?onlyArgs[0].slice('--ordinary-source-only='.length):undefined;
 const selected=only===undefined?specs:specs.filter(s=>s.key===only);assert(selected.length,'Unknown exact ordinary source selection');
 const native=options.native;assert(native&&typeof native.invoke==='function','Explicit owned original native writer required');
 const {createDiskRuntime}=await import('./diskSettings.mjs');
 const disk=await createDiskRuntime(path.join(out,'canonical'),native,{settingsSources:options.settingsSources});
 const a=disk.a,r=a.repository;await r.loadSettings();Object.assign(a.ports.settings,await r.loadSettings());
 const initial=await a.boot.loadBootModes({editorOnly:true});await a.boot.saveBootModes(initial.document,initial.document.revision,initial.initialSource);
 const seed=structuredClone(disk.read()),rows=[],digest=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const inputs=a.inputs.map(file=>path.resolve(process.cwd(),file)).filter(file=>fs.existsSync(file)).map(file=>({path:file,sha256:digest(file)}));
 const at=(d,field)=>field==='decky'?d.extensions.bootModes.global.steamDecky.enabled:d.extensions.bootModes.global.sleep.steamAutoRecovery;
 const setGlobal=(d,field,value)=>{if(field==='decky')d.extensions.bootModes.global.steamDecky.enabled=value;else d.extensions.bootModes.global.sleep.steamAutoRecovery=value;};
 const root=(d,field)=>field==='decky'?d.extensions.deckySidebar?.enabled:d.sleep.autoWakeRecovery;
 const setRoot=(d,field,value)=>{if(field==='decky')d.extensions.deckySidebar={...(d.extensions.deckySidebar||{}),enabled:value};else d.sleep.autoWakeRecovery=value;};
 const section=field=>field==='decky'?'extensions':'sleep';
 const payload=(field,value)=>field==='decky'?{deckySidebar:{enabled:value}}:{autoWakeRecovery:value};
 const output=path.join(out,'ordinary-source-parity.json');
 try{
  for(const s of selected){
   const data=structuredClone(seed);data.extensions.bootModes.current=s.mode;data.extensions.bootModes.boot=s.mode;data.extensions.bootModes.bootEnabled=s.mode!=='global';
   setRoot(data,s.field,s.kind==='same-value'?s.on:!s.on);setGlobal(data,s.field,!s.on);
   data.extensions.deckySidebar={...data.extensions.deckySidebar,ownerSentinel:'retained'};
   data.extensions.peerFixture={keep:['source','future']};
   const own=s.mode==='global'?'steam':s.mode;
   data.extensions.bootModes.scenes[own].overrides.steamDecky={enabled:s.on,bankSentinel:'independent'};
   data.extensions.bootModes.scenes[own].overrides.sleep={steamAutoRecovery:s.on,bankSentinel:'independent'};
   data.extensions.bootModes.scenes[own].rememberedOverrides={steamDecky:{enabled:!s.on},sleep:{steamAutoRecovery:!s.on}};
   data.extensions.bootModes.presets[own]={kept:{name:'kept',overrides:{steamDecky:{enabled:s.on},sleep:{steamAutoRecovery:s.on}}}};
   data.extensions.bootModes.global.steamDecky.future='preserved';data.extensions.bootModes.global.sleep.future='preserved';
   if(s.kind==='future-mode')data.extensions.bootModes.schema=99;
   if(s.kind==='max-revision')data.extensions.bootModes.revision=Number.MAX_SAFE_INTEGER;
   disk.writeRaw(data);const before=await r.loadSettings(),savedMode=structuredClone(before.extensions.bootModes),rawBefore=fs.readFileSync(disk.file,'utf8');
   let expected=at(before,s.field),changed=false,revisionChange=false,expectedError=false;
   const opts=s.origin&&s.origin!=='default'?{origin:s.origin}:{};
   const write=async()=>{
    if(s.kind==='legacy-ui')return r.saveSettingsSection('ui',{deckyEnabled:s.on});
    if(s.kind==='contrary-legacy')return r.updateSettings(d=>{d.ui.deckyEnabled=!s.on;setRoot(d,s.field,s.on);});
    if(s.kind==='sibling')return r.saveSettingsSection('extensions',{deckySidebar:{ownerSentinel:'updated-only'},peerFuture:{keep:true}});
    if(s.kind==='missing')return r.updateSettings(d=>{if(s.field==='decky')delete d.extensions.deckySidebar.enabled;else delete d.sleep.autoWakeRecovery;});
    if(s.kind==='malformed-source')return r.saveSettingsSection('extensions',{deckySidebar:{enabled:'unconfirmed'}});
    if(s.method==='update')return r.updateSettings(d=>setRoot(d,s.field,s.on),opts);
    if(s.method==='replace'){const value=structuredClone(before[section(s.field)]);if(s.field==='decky')value.deckySidebar={...value.deckySidebar,enabled:s.on};else value.autoWakeRecovery=s.on;return r.replaceSettingsSection(section(s.field),value,opts);}
    return r.saveSettingsSection(section(s.field),payload(s.field,s.on),undefined,opts);
   };
   if(['changed','same-value'].includes(s.kind)){
    changed=s.origin!=='boot-mode'&&s.origin!=='runtime'&&(s.kind==='changed'||s.method!=='update');
    if(changed)expected=s.on;
   }else if(['contrary-legacy','atomic-failure','queued-immutable'].includes(s.kind)){expected=s.on;changed=true;}
   if(s.kind==='future-mode')expected=at(before,s.field);
   if(s.kind==='max-revision')expectedError=true;
   if(s.field==='recovery'){expected=at(before,s.field);changed=false;expectedError=false;}
   if(s.kind==='queued-immutable'){
    const port=globalThis.__bootDiskPort,original=port.write;let entered,release,first=true;
    const ready=new Promise(resolve=>entered=resolve),held=new Promise(resolve=>release=resolve);
    port.write=async(...values)=>{if(first){first=false;entered();await held;}return original(...values);};
    try{
     const blocker=r.saveSettingsSection('ui',{windowPlacement:'left'});await ready;
     const value=payload(s.field,s.on),options={origin:'ordinary'},pending=r.saveSettingsSection(section(s.field),value,undefined,options);
     if(s.field==='decky')value.deckySidebar.enabled=!s.on;else value.autoWakeRecovery=!s.on;options.origin='runtime';
     release();await blocker;await pending;
    }finally{release();port.write=original;}
   }else if(s.kind==='atomic-failure'){
    disk.faults.failWrite=true;await assert.rejects(write,/写|write|失败|未确认/i);assert.equal(fs.readFileSync(disk.file,'utf8'),rawBefore,'Failed projection changed file');
    await write();
   }else if(expectedError){
    await assert.rejects(write,/版本|范围|revision/i);assert.equal(fs.readFileSync(disk.file,'utf8'),rawBefore,'Invalid mode revision changed file');
   }else await write();
   const after=disk.read(),actual=at(after,s.field),mode=after.extensions.bootModes;
   rows.push({key:s.key,expected,actual,revisionBefore:savedMode.revision,revisionAfter:mode.revision,expectedError,origin:s.origin??'default',method:s.method??'section'});
   assert.equal(actual,expected,'Canonical ordinary source: '+s.key);
   assert.deepEqual(mode.scenes,savedMode.scenes,'Ordinary source captured independent banks '+s.key);assert.deepEqual(mode.presets,savedMode.presets);
   assert.deepEqual(mode.activeSnapshot,savedMode.activeSnapshot);assert.equal(mode.current,savedMode.current);assert.equal(mode.boot,savedMode.boot);
   assert.deepEqual(after.extensions.peerFixture,before.extensions.peerFixture);assert.equal(mode.global.steamDecky.future,'preserved');assert.equal(mode.global.sleep.future,'preserved');
   if(['changed','same-value','contrary-legacy','atomic-failure','queued-immutable'].includes(s.kind)){
    assert.equal(root(after,s.field),s.on,'Saved canonical root did not follow submitted immutable intent '+s.key);
    assert.equal(mode.revision,savedMode.revision+(changed?1:0),'Preference projection must advance exactly once '+s.key);
   }
   if(s.kind==='legacy-ui'||s.kind==='sibling'||s.kind==='malformed-source')assert.equal(mode.revision,savedMode.revision,'Non-authoritative source advanced mode revision '+s.key);
   if(s.kind==='changed'&&s.origin!=='default')assert.deepEqual(mode,savedMode,'Scene/runtime source captured global '+s.key);
   if(s.kind==='future-mode')assert.deepEqual(mode,savedMode,'Future document overwritten '+s.key);
   if(s.field==='recovery')assert.deepEqual(mode,savedMode,'Shared sleep save changed four-mode state '+s.key);
  }
  assert.equal(rows.length,selected.length);
  return {actualRows:rows.length,rows,scope:'Actual repository section/update/replace and original locked native file writer; ordinary canonical fields only',sources:inputs,nativeRecompiled:false};
 }finally{
  const sourceAfter=inputs.map(p=>({...p,currentSha256:digest(p.path)}));assert(sourceAfter.every(p=>p.sha256===p.currentSha256),'Source changed during canonical field scope');
  fs.writeFileSync(output,JSON.stringify({rows,actualRows:rows.length,complete:rows.length===selected.length,selectedNames:selected.map(s=>s.key),selectionOnly:only??null,sources:inputs,sourceAfter,settingsSourceOverrides:a.settingsSourceOverrides,trace:disk.trace},null,2),{flag:'wx'});
 }
}

/** Common fan preferences are independent of all legacy four-mode records. */
export async function runFanIntentInheritance(out,args=[],options={}){
 const modes=['global','xbox','steam','xbox-steam'],specs=[];
 for(const mode of modes)for(const on of [false,true])for(const kind of mode==='global'
  ?['global-only','snapshot','claimed','unknown-boot']
  :['preset-only','empty-own','explicit-own','absent-own','invalid-enabled','malformed-own','disabled-next','snapshot','claimed','unknown-boot'])
  specs.push({mode,on,kind,key:mode+'/'+kind+'/'+(on?'on':'off')});
 for(const mode of modes)specs.push({mode,on:false,kind:'missing-global',key:mode+'/missing-global/off'});
 const startupEdits=args.includes('--fan-intent-startup-edits');
 const candidates=startupEdits?modes.flatMap(mode=>[false,true].flatMap(on=>['capture-flag','capture-preset','capture-mode','capture-input'].map(kind=>({mode,on,kind,key:mode+'/'+kind+'/'+(on?'on':'off')})))):specs;
 const selections=args.filter(v=>v.startsWith('--fan-intent-only='));assert(selections.length<=1,'Duplicate fan-intent selector');
 const only=selections[0]?.slice('--fan-intent-only='.length),selected=only===undefined?candidates:candidates.filter(s=>s.key===only);assert(selected.length,'Unknown exact fan-intent selector');
 const native=options.native;assert(native&&typeof native.invoke==='function','Original owned native file terminal required');
 const {createDiskRuntime}=await import('./diskSettings.mjs');
 const disk=await createDiskRuntime(path.join(out,'data'),native,{bootModesSource:options.bootModesSource});
 const a=disk.a,r=a.repository;await r.loadSettings();Object.assign(a.ports.settings,await r.loadSettings());
 const initial=await a.boot.loadBootModes({editorOnly:true});await a.boot.saveBootModes(initial.document,initial.document.revision,initial.initialSource);
 const seed=structuredClone(disk.read()),rows=[];const boot='fixture-windows-boot-1';
 try{
  for(const s of selected){
   if(startupEdits){
    a.ports.reset();const data=structuredClone(seed),doc=data.extensions.bootModes;
    doc.current='global';doc.boot=s.mode;doc.bootEnabled=s.mode!=='global';
    doc.global.fan={enabled:!s.on,preset:'soft'};doc.activeSnapshot.fan={enabled:!s.on,preset:'soft'};
    for(const scene of Object.values(doc.scenes))scene.overrides.fan={enabled:!s.on,preset:'aggressive'};
    data.startupDesired.fanControl=!s.on;data.fan.preset='balanced';
    disk.writeRaw(data);await r.loadSettings();Object.assign(a.ports.settings,await r.loadSettings());
    let boundary,entered=false;
    a.ports.hooks.chargeRead=async()=>{
     if(entered)return;entered=true;
     if(s.kind==='capture-flag')await r.saveSettingsSection('startupDesired',{fanControl:s.on});
     if(s.kind==='capture-preset')await r.saveSettingsSection('fan',{preset:s.on?'aggressive':'soft'});
     if(s.kind==='capture-mode')await r.updateSettings(d=>{d.extensions.bootModes.revision++;});
     if(s.kind==='capture-input'){
      const now=await r.loadSettings(),cas=await r.compareAndSwapInputSettings(now.input.revision,{gyroMotion:{gyroWeight:now.input.gyroMotion.gyroWeight+.25}});
      assert.equal(cas.ok,true,'Peer input edit failed');
     }
     boundary={raw:fs.readFileSync(disk.file,'utf8'),settings:disk.read()};
    };
    let result,error;try{result=await a.boot.applyBootMode(s.mode,doc,{startup:true});}catch(e){error={message:e.message,failedStep:e.failedStep};}finally{delete a.ports.hooks.chargeRead;}
    assert(entered&&boundary,'Capture boundary was not executed');const rejected=['capture-mode','capture-input'].includes(s.kind),after=disk.read();
    if(rejected){assert(error,'Real owner change was incorrectly accepted');assert.equal(fs.readFileSync(disk.file,'utf8'),boundary.raw,'Stale startup overwrote the new owner');}
    else{
     assert.equal(error,undefined,error?.message);assert(result);assert.equal(result.document.current,s.mode);assert.equal(result.document.revision,doc.revision+1);
     assert.equal(after.startupDesired.fanControl,s.kind==='capture-flag'?s.on:!s.on);assert.deepEqual(after.fan,boundary.settings.fan);
     assert.equal(result.receipt.modules.fan,undefined);assert.equal(result.document.global.fan,undefined);
     assert.equal(a.model.normalizeBootModeDocument(after.extensions.bootModes,initial.document.global).global.fan,undefined);
    }
    assert(!a.ports.calls.some(c=>c.op.startsWith('fan.')),'Mode application dispatched a fan action');
    rows.push({key:s.key,rejected,error:error??null,modeRevisionBefore:doc.revision,modeRevisionAtCapture:boundary.settings.extensions.bootModes.revision,modeRevisionAfter:after.extensions.bootModes.revision,commonFlag:after.startupDesired.fanControl,commonPreset:after.fan.preset,fanActions:0});
    continue;
   }
   const data=structuredClone(seed),doc=data.extensions.bootModes;
   doc.current='global';doc.boot=s.mode;doc.bootEnabled=true;doc.global.fan={enabled:s.on,preset:'balanced'};
   for(const id of ['xbox','steam','xbox-steam'])delete doc.scenes[id].overrides.fan;
   delete data.extensions.bootModesAppliedBoot;delete data.extensions.bootModesActiveBoot;delete data.extensions.bootModesStartupResult;
   if(s.mode!=='global'){
    if(s.kind==='empty-own')doc.scenes[s.mode].overrides.fan={};
    else if(s.kind==='explicit-own')doc.scenes[s.mode].overrides.fan={enabled:!s.on,preset:'aggressive'};
    else if(s.kind==='invalid-enabled')doc.scenes[s.mode].overrides.fan={enabled:null,preset:'aggressive'};
    else if(s.kind==='malformed-own')doc.scenes[s.mode].overrides.fan=['invalid'];
    else if(s.kind!=='absent-own')doc.scenes[s.mode].overrides.fan={preset:'aggressive'};
   }
   if(s.kind==='missing-global'){doc.global.fan={};doc.unresolvedGlobal=['fan'];}
   if(s.kind==='disabled-next')doc.bootEnabled=false;
   if(s.kind==='snapshot'){doc.activeSnapshot.fan={enabled:!s.on,preset:'soft'};data.extensions.bootModesActiveBoot=boot;}
   if(s.kind==='claimed')data.extensions.bootModesAppliedBoot=boot;
   a.ports.hooks.bootIdentity=()=>s.kind==='unknown-boot'?'':boot;
   disk.writeRaw(data);await r.loadSettings();
   const before=fs.readFileSync(disk.file,'utf8'),backup=fs.existsSync(disk.file+'.bak')?fs.readFileSync(disk.file+'.bak','utf8'):undefined;
   const calls=a.ports.calls.length,writes=disk.trace.filter(v=>v.op==='write').length;
   const expected=null;
   const startup=await a.boot.readBootModeFanIntent('startup'),current=await a.boot.readBootModeFanIntent('current');
   rows.push({key:s.key,expected,startup,current,source:doc.global.fan,own:s.mode==='global'?null:doc.scenes[s.mode].overrides.fan??null,rawDocumentBefore:before,
    writes:disk.trace.filter(v=>v.op==='write').length-writes,calls:a.ports.calls.slice(calls),fileUnchanged:fs.readFileSync(disk.file,'utf8')===before,
    backupUnchanged:(fs.existsSync(disk.file+'.bak')?fs.readFileSync(disk.file+'.bak','utf8'):undefined)===backup});
   assert.equal(startup,expected,'Retired mode fan record overrode the common startup preference: '+s.key);
   assert.equal(current,null,'Retired active snapshot became current fan intent');
   assert(rows.at(-1).fileUnchanged&&rows.at(-1).backupUnchanged);assert.equal(rows.at(-1).writes,0,'Read-only fan intent wrote configuration');
   assert.equal(rows.at(-1).calls.length,0,'Retired mode fan lookup must not dispatch native work');
   await r.saveSettingsSection('startupDesired',{fanControl:s.on});
   await r.saveSettingsSection('fan',{preset:s.on?'aggressive':'soft'});
   const after=disk.read();
   assert.equal(after.startupDesired.fanControl,s.on,'Common startup preference was lost');
   assert.equal(after.fan.preset,s.on?'aggressive':'soft','Common fan preset was lost');
   assert.deepEqual(after.extensions.bootModes,doc,'Common fan preference changed the four-mode owner');
   rows.at(-1).commonPreferenceSaved=true;
   rows.at(-1).modeRevisionUnchanged=true;
   const normal=a.model.normalizeBootModeDocument(doc,initial.document.global);
   assert.equal(normal.global.fan,undefined);assert.equal(normal.activeSnapshot.fan,undefined);
   for(const scene of Object.values(normal.scenes))assert.equal(scene.overrides.fan,undefined);
   assert(!normal.unresolvedGlobal?.includes('fan'));
  }
  return {rows,actualRows:rows.length,actualNativeFileRead:true,featureActions:0,commonPreferenceWrites:selected.length*2,modeWrites:0};
 }finally{
  disk.flush();a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();
  fs.writeFileSync(path.join(out,'fan-intent-inheritance.json'),JSON.stringify({rows,actualRows:rows.length,selectedNames:selected.map(s=>s.key),complete:rows.length===selected.length,
   expectedCount:selected.length,selection:only??null,bootModesSource:options.bootModesSource??path.resolve('src/bridge/bootModes.ts'),trace:disk.trace},null,2),{flag:'wx'});
 }
}

export async function runEmptyAppearanceReceipt(out,args=[],options={}){
 assert(args.filter(v=>v.startsWith('--empty-appearance-')).every(v=>v==='--empty-appearance-receipt'||v.startsWith('--empty-appearance-only=')),'Unknown empty appearance option');
 const states=['global','xbox','steam','xbox-steam'].flatMap(mode=>['empty','missing','explicit','same'].flatMap(shape=>[false,true].map(observed=>({mode,shape,observed,key:[mode,shape,observed?'on':'off'].join('/')}))));
 const onlyArgs=args.filter(v=>v.startsWith('--empty-appearance-only='));assert(onlyArgs.length<=1,'Duplicate empty appearance selection');
 const only=onlyArgs.length?onlyArgs[0].slice('--empty-appearance-only='.length):undefined,selected=only===undefined?states:states.filter(v=>v.key===only);assert(selected.length,'Unknown exact empty appearance selection');
 assert(typeof options.runtimeBundle==='function','Original boot runtime bundle required');
 const a=await options.runtimeBundle(out),q=a.ports,rows=[];
 try{
  for(const s of selected){
   q.reset();q.state.appearance={themes:[{name:'Round',configured:s.observed}]};
   const baseline=await a.boot.captureBootModeSnapshot(),doc=a.model.createBootModeDocument(baseline);
   for(const scene of Object.values(doc.scenes)){delete scene.overrides.steamAppearance;delete scene.overrides.audioOptimization;delete scene.overrides.frameGeneration;}
   doc.global.audioOptimization.enabled=false;doc.global.frameGeneration.enabled=false;
   doc.global.steamAppearance=s.shape==='missing'?{}:s.shape==='empty'?{states:{}}:{states:{Round:s.shape==='same'?s.observed:!s.observed}};
   q.settings.extensions.bootModes=structuredClone(doc);
   const {document,receipt}=await a.boot.applyBootMode(s.mode,doc,{startup:true});
   const calls=q.calls.filter(v=>v.op==='appearance.apply'),row={key:s.key,actualState:receipt.modules.steamAppearance.state,appearanceWrites:calls.length,activeSnapshot:document.activeSnapshot.steamAppearance,
    expectedState:s.shape==='explicit'?'requested':'unchanged',expectedWrites:s.shape==='explicit'?1:0};rows.push(row);
   assert.equal(row.actualState,row.expectedState,'Empty appearance selection must not claim a submitted action '+s.key);
   assert.equal(calls.length,row.expectedWrites,'No-theme selection must not dispatch an appearance apply '+s.key);
   assert.deepEqual(document.global.steamAppearance,doc.global.steamAppearance,'Receipt repair changed template intent '+s.key);
   if(s.shape==='empty'||s.shape==='missing'){
    assert.deepEqual(document.activeSnapshot.steamAppearance,baseline.steamAppearance,'No-op replaced observed appearance snapshot '+s.key);
    assert(!receipt.pending.some(v=>v.startsWith('Steam 美化')));assert(!receipt.skipped.some(v=>v.startsWith('Steam 美化')));
   }
   if(s.shape==='explicit'){assert.equal(calls[0].origin,'boot-mode');assert.deepEqual(calls[0].value,doc.global.steamAppearance.states);}
   assert(!receipt.applied.includes('Steam 美化'),'Request-only appearance falsely became active');
   a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();
  }
  assert.equal(rows.length,selected.length);return {rows,actualRows:rows.length,realSteamOrVisualClaim:false};
 }finally{
  a.boot.cancelBootModeLaunches();a.boot.cancelBootModeDeferredMouse();
  fs.writeFileSync(path.join(out,'empty-appearance-receipts.json'),JSON.stringify({rows,actualRows:rows.length,complete:rows.length===selected.length,selectedNames:selected.map(v=>v.key),selectionOnly:only??null,
   scope:'Actual four-mode configuration receipts and active snapshot; original memory Steam/OS boundary',sourceOverride:a.sourceOverride},null,2),{flag:'wx'});
 }
}
