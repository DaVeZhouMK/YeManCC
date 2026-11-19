import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';import {pathToFileURL} from 'node:url';import {require} from './harness.mjs';
export async function runMonitorOwner(out,args=[]){
 const root=process.cwd(),choose=name=>args.find(v=>v.startsWith('--monitor-owner-'+name+'='))?.split('=').slice(1).join('=');
 const kinds=['revision','hash','mode','result','observed','ack','unrelated','current'],modes=['global','xbox','steam','xbox-steam'];
 const plans=[
  {name:'steam-on',output:'steam',enabled:true,global:false,stages:['steam-read','available-read','rtss-read-1','rtss-toggle','rtss-read-2','steam-hide','rtss-hide','layout-read','ymcc-hide','steam-show']},
  {name:'rtss-on',output:'rtss',enabled:true,global:false,stages:['steam-read','available-read','rtss-read-1','rtss-toggle','rtss-read-2','steam-hide','ymcc-hide','rtss-show','layout-read']},
  {name:'ymcc-game',output:'ymcc',enabled:true,global:false,stages:['route-read','steam-read','available-read','rtss-read-1','rtss-toggle','rtss-read-2','steam-hide','rtss-hide','layout-read','ymcc-hide','ymcc-show']},
  {name:'ymcc-global',output:'ymcc',enabled:true,global:true,stages:['route-read','available-read','ymcc-hide','ymcc-show','rtss-read-1','rtss-toggle','rtss-read-2','rtss-hide','layout-read']},
  {name:'global-off',output:'ymcc',enabled:false,global:true,stages:['ymcc-hide','route-read','available-read']},
  {name:'game-off',output:'rtss',enabled:false,global:false,stages:['ymcc-hide','steam-read','available-read','steam-hide','rtss-hide','layout-read']},
  {name:'save',stages:['post-save']},{name:'queued',stages:['queued-entry']},
 ];
 const selected={mode:choose('mode'),scenario:choose('scenario'),stage:choose('stage'),kind:choose('kind')};
 for(const [name,values]of Object.entries({mode:modes,scenario:plans.map(p=>p.name),stage:[...new Set(plans.flatMap(p=>p.stages))],kind:kinds}))assert(!selected[name]||values.includes(selected[name]),'Unknown monitor owner '+name);
 const memory=String.raw`
 import {assertBootPreferenceOwner,bootStartupReceiptOwner} from ACTUAL_OWNER;
 export const state={settings:null,effects:[],prefEvents:[],applyEvents:[],applying:[],backend:false,layout:'Empty.ovl',ownerReads:0,rtssReads:0,global:false,hook:async()=>{},block:null};
 export function cachedBootStartupReceiptOwner(){return state.settings?bootStartupReceiptOwner(state.settings):undefined;}
 export async function loadSettings(){state.ownerReads++;return structuredClone(state.settings);}
 export async function readSettingsSection(name){return structuredClone(state.settings[name]);}
 export async function updateSettings(mutator,options={}){if(options.bootContext)assertBootPreferenceOwner(state.settings,options);const doc=structuredClone(state.settings);mutator(doc);state.settings=doc;state.effects.push('settings-save');await state.hook('post-save');return structuredClone(doc);}
 export async function saveSettingsSection(){throw Error('Unexpected section writer');}
 export const deckySidebar={get:async()=>({steamKnown:true,steamRunning:true})};
 const steamState=()=>({ok:true,available:true,pending:false,applicationConfirmed:true,via:'live',liveConfirmed:true,position:0,detail:1,scale:1,saturation:1,opacity:1});
 export const steamSettings={get:async()=>{if(state.block){const block=state.block;state.block=null;block.enter();await block.wait;}await state.hook('steam-read');return steamState();},applyMonitor:async patch=>{const label=patch.position===0?'steam-hide':'steam-show';state.effects.push(label);await state.hook(label);return {...steamState(),...patch};}};
 export async function rtssRunning(){const label='rtss-read-'+(++state.rtssReads);await state.hook(label);return state.backend;}
 export async function toggleRtss(on){state.effects.push('rtss-toggle');state.backend=on;await state.hook('rtss-toggle');}
 export async function readOverlayLayout(){await state.hook('layout-read');return state.layout;}
 export async function readRtssZoom(){return 5;}
 export async function setOverlayLayout(template){const label=template==='off'?'rtss-hide':'rtss-show';state.effects.push('rtss:'+template);state.layout=template==='off'?'Empty.ovl':({'W':'YeManOBS-W-1.ovl','L':'YeManOBS-L-1.ovl','J':'YeManOBS-JJ-1.ovl'})[template];await state.hook(label);return {mode:'live'};}
 `.replace('ACTUAL_OWNER',JSON.stringify(root+'/src/bridge/bootModeGlobalPreferences.ts'));
 const build=await require('esbuild').build({stdin:{contents:"export * as monitor from './src/bridge/monitorOutput.ts';export {normalizeMonitorOutput} from './src/bridge/monitorHudModel.ts';export {state} from 'monitor-owned-memory';",resolveDir:root,sourcefile:'monitor-owned-entry.ts',loader:'ts'},absWorkingDir:root,bundle:true,platform:'node',format:'esm',write:false,metafile:true,plugins:[{name:'monitor-memory',setup(b){b.onResolve({filter:/^(monitor-owned-memory|\.\/settingsRepository|\.\/yeman|\.\/steamSettings|\.\/deckySidebar)$/},()=>({path:'terminal',namespace:'monitor-owned'}));b.onLoad({filter:/.*/,namespace:'monitor-owned'},()=>({contents:memory,loader:'ts',resolveDir:root}));}}]});
 const bundle=path.join(out,'monitor-owner.mjs');fs.writeFileSync(bundle,build.outputFiles[0].text,{flag:'wx'});const m=await import(pathToFileURL(bundle)),q=m.state,monitor=m.monitor;
 const defaults=JSON.parse(fs.readFileSync(root+'/PowerControl/settings-defaults-v1.json','utf8')),rows=[],ordinary=[];
 const offAdapter=monitor.registerYmccHudOutputAdapter({globalFor:async()=>{await q.hook('route-read');return q.global;},available:async()=>{await q.hook('available-read');return true;},visible:async()=>false,setVisible:async(show)=>{const label=show?'ymcc-show':'ymcc-hide';q.effects.push(label);await q.hook(label);return {confirmed:true};}});
 const offPreferences=monitor.onMonitorOutputPreferencesChanged(p=>q.prefEvents.push({output:p.output,template:p.rtssTemplate})),offApplied=monitor.onMonitorOutputApplied((p,r)=>q.applyEvents.push({output:p.output,template:p.rtssTemplate,result:r})),offApplying=monitor.onMonitorOutputApplying(value=>q.applying.push(value));
 const raw=path.join(out,'monitor-owner-observations.jsonl');fs.closeSync(fs.openSync(raw,'wx'));
 function reset(mode,plan){q.settings=structuredClone(defaults);q.settings.input.revision=7;q.settings.extensions={bootModes:{schema:1,revision:3,current:mode}};q.effects=[];q.prefEvents=[];q.applyEvents=[];q.applying=[];q.backend=false;q.layout='Empty.ovl';q.ownerReads=q.rtssReads=0;q.global=!!plan.global;q.hook=async()=>{};q.block=null;const context={input:structuredClone(q.settings.input),extensions:structuredClone(q.settings.extensions)};return {origin:'boot-mode',bootContext:context};}
 function mutate(kind){if(kind==='revision')q.settings.input.revision++;if(kind==='hash')q.settings.input.diagnostics.inputLoggingEnabled=!q.settings.input.diagnostics.inputLoggingEnabled;if(kind==='mode')q.settings.extensions.bootModes.revision++;if(kind==='result')q.settings.extensions.bootModesStartupResult={schema:1,revision:99};if(kind==='observed')q.settings.input.startupObservedBoot='other-boot';if(kind==='ack')q.settings.input.hostAcknowledgedRevision=17;if(kind==='unrelated')q.settings.extensions.peer=true;}
 const prefs=plan=>m.normalizeMonitorOutput({enabled:plan.enabled??true,output:plan.output??'rtss',rtssTemplate:'W',ymccTemplate:'A'});
 const wait=async(promise,label)=>{let timer;try{return await Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(Error('Monitor fixture wait expired '+label)),1500);})]);}finally{clearTimeout(timer);}};
 try{
 for(const mode of modes)for(const plan of plans)for(const stage of plan.stages)for(const kind of kinds){
  if(Object.entries(selected).some(([name,value])=>value&&value!==({mode,scenario:plan.name,stage,kind})[name]))continue;
  const options=reset(mode,plan),allowed=['ack','unrelated','current'].includes(kind);let changed=false,at=0,result,error='';
  if(plan.name==='queued'){
   let enter,release,queued;const entered=new Promise(r=>enter=r),hold=new Promise(r=>release=r),queuedSignal=new Promise(r=>queued=r);let applyingCount=0;
   const off=monitor.onMonitorOutputApplying(value=>{if(value&&++applyingCount===2)queued();});q.block={enter,wait:hold};
   const first=monitor.monitorOutputCoordinator.apply(m.normalizeMonitorOutput({enabled:false,output:'rtss'}));
   let second;
   try{await wait(entered,'first preflight');const value=m.normalizeMonitorOutput({enabled:true,output:'rtss',rtssTemplate:'J'});second=monitor.monitorOutputCoordinator.apply(value,options).then(value=>({value}),error=>({error:String(error)}));await wait(queuedSignal,'second enqueued');changed=true;at=q.effects.length;mutate(kind);release();await first;const settled=await second;result=settled.value;error=settled.error??'';}
   finally{release();await first.catch(()=>{});if(second)await second;off();}
  }else{
   q.hook=async label=>{if(changed||label!==stage)return;changed=true;at=q.effects.length;mutate(kind);};
   try{if(plan.name==='save')await monitor.saveMonitorOutputPreferences(prefs(plan),options);else result=await monitor.monitorOutputCoordinator.apply(prefs(plan),options);}catch(e){error=String(e);}
  }
  const late=plan.name==='queued'?q.effects.filter(value=>value==='rtss:J'):q.effects.slice(at),publications=plan.name==='save'?q.prefEvents.length:plan.name==='queued'?q.applyEvents.filter(e=>e.template==='J').length:q.applyEvents.length;
  const row={mode,scenario:plan.name,stage,kind,allowed,changed,error,result,lateEffects:late,publications,ownerReads:q.ownerReads};fs.appendFileSync(raw,JSON.stringify(row)+'\n');assert(changed,'Selected monitor boundary not reached '+JSON.stringify(row));
  if(allowed){assert(!error,JSON.stringify(row));if(plan.name!=='save')assert.equal(result?.ok,true,JSON.stringify(row));assert.equal(publications,1,JSON.stringify(row));}
  else{assert.equal(late.length,0,'Expired monitor request reached later output '+JSON.stringify(row));assert.equal(publications,0,'Expired monitor request published success/preferences '+JSON.stringify(row));assert.match(error,/BOOT_PREFERENCE_REQUEST_EXPIRED|MONITOR_OUTPUT_REQUEST_EXPIRED/,'No specific owner rejection '+JSON.stringify(row));}
  rows.push(row);
 }
 assert(rows.length,'No selected monitor scenario/stage');
 for(const plan of plans.filter(p=>p.name!=='queued')){reset('global',plan);if(plan.name==='save')await monitor.saveMonitorOutputPreferences(prefs(plan));else assert.equal((await monitor.monitorOutputCoordinator.apply(prefs(plan))).ok,true);assert.equal(q.ownerReads,0,'Unscoped monitor gained owner settings reads');ordinary.push({scenario:plan.name,ownerReads:0});}
 }finally{offPreferences();offApplied();offApplying();offAdapter();}
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex'),inputs=[...new Set(Object.keys(build.metafile.inputs).filter(file=>!file.startsWith('monitor-owned:')&&file!=='monitor-owned-entry.ts').map(file=>path.resolve(root,file)))].map(file=>({path:file,bytes:fs.statSync(file).size,sha256:hash(file)}));
 const result={status:'PASS',actualRows:rows.length,expectedAllRows:modes.length*kinds.length*plans.reduce((n,p)=>n+p.stages.length,0),selected,plans:plans.map(p=>({name:p.name,stages:p.stages})),ordinary,inputs,scope:'Actual monitor module/core queue/dependency callbacks, actual owner assertion; settings/Steam/RTSS/HUD are memory leaves. Save-publication and apply-publication observed. Internal already-dispatched native operations NOT accepted.'};fs.writeFileSync(path.join(out,'monitor-owner-result.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}
