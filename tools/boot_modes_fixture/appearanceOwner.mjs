import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createHash} from 'node:crypto';import {pathToFileURL} from 'node:url';import {require} from './harness.mjs';
export async function runAppearanceOwner(out,args=[]){
 const root=process.cwd(),before=args.includes('--appearance-owner-before'),extra=args.includes('--appearance-owner-extra-branches'),sourceIndex=args.indexOf('--appearance-owner-source');assert(!(before&&extra),'Original control and extra branch selections are mutually exclusive');assert(before===(sourceIndex>=0),'Before appearance scope requires only its explicit original source');
 const source=before?path.resolve(args[sourceIndex+1]):path.join(root,'src/bridge/steamAppearance.ts'),dir=path.join(out,'appearance-owner');fs.mkdirSync(dir);
 const hash=bytes=>createHash('sha256').update(bytes).digest('hex'),pin=file=>({path:file,bytes:fs.statSync(file).size,sha256:hash(fs.readFileSync(file))}),write=(name,value)=>fs.writeFileSync(path.join(dir,name),JSON.stringify(value,null,2)+'\n',{flag:'wx'});
 const memory=String.raw`
 export const q={document:null,calls:[],wire:[],effects:[],ownerReads:0,hook:async()=>{},snapshot:null,clock:0,begin:0,end:0,ensure:0,active:false,failEnd:false,failApply:false};
 export async function loadSettings(){q.ownerReads++;return structuredClone(q.document);}
 export async function updateSettings(){throw Error('Unexpected ordinary preference save');}
 export const fs={exists:async()=>true},shell={},settingsStore={location:async()=>({directory:'Q:/owned/PowerControl'})};
 export async function invoke(){throw Error('Unexpected production IPC');}
 `;
 const build=await require('esbuild').build({stdin:{contents:"export * from './src/bridge/steamAppearance.ts';export {q} from 'appearance-owner-memory';",resolveDir:root,sourcefile:'appearance-owner-entry.ts',loader:'ts'},absWorkingDir:root,bundle:true,platform:'node',format:'esm',write:false,metafile:true,plugins:[{name:'appearance-owner-memory',setup(b){b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]steamAppearance\.ts$/},()=>({contents:fs.readFileSync(source,'utf8'),loader:'ts',resolveDir:path.dirname(path.join(root,'src/bridge/steamAppearance.ts'))}));b.onResolve({filter:/^(appearance-owner-memory|\.\/settingsRepository|\.\/api|\.\/ipc)$/},()=>({path:'terminal',namespace:'appearance-owner-memory'}));b.onLoad({filter:/.*/,namespace:'appearance-owner-memory'},()=>({contents:memory,loader:'ts'}));}}]});
 const module=path.join(dir,'actual.mjs');fs.writeFileSync(module,build.outputFiles[0].text,{flag:'wx'});const m=await import(pathToFileURL(module)),q=m.q;
 const modes=['global','xbox','steam','xbox-steam'],kinds=['revision','hash','mode','result','observed','ack','unrelated','current'];
 const allPlans=[
  {name:'apply-ready',method:'apply',cold:false,offline:false,stages:['dispatch','location-1','exists-1','policy-1','Status-1','policy-2','begin-1','location-2','exists-2','policy-3','Apply-1','policy-4','release-1']},
  {name:'apply-offline',method:'apply',cold:false,offline:true,stages:['dispatch','location-1','exists-1','policy-1','Status-1','location-2','exists-2','policy-2','Apply-1']},
  {name:'reset-ready',method:'reset',cold:false,offline:false,stages:['dispatch','location-1','exists-1','policy-1','Status-1','location-2','exists-2','policy-2','Reset-1','policy-3','begin-1','release-1']},
  {name:'apply-cold',method:'apply',cold:true,offline:false,stages:['dispatch','Status-1','policy-2','begin-1','Apply-1','policy-4','ensure-1','delay-1','location-3','exists-3','policy-5','Status-2','release-1']},
  {name:'apply-takeover',method:'apply',cold:false,offline:false,extra:true,stages:['Prepare-1','Reload-1','release-1']},
  {name:'apply-reload',method:'apply',cold:false,offline:false,extra:true,stages:['Reload-1','release-1']},
  {name:'reset-previous',method:'reset',cold:false,offline:false,extra:true,stages:['Restore-1','release-1']},
 ];
 const plans=allPlans.filter(plan=>extra?plan.extra:!plan.extra);assert.equal(plans.length,extra?3:4);
 const defaults=JSON.parse(fs.readFileSync(root+'/PowerControl/settings-defaults-v1.json','utf8')),rows=[],controls=[];
 const raw=path.join(dir,'observations.jsonl');fs.closeSync(fs.openSync(raw,'wx'));
 function reset(mode,plan){
  q.document=structuredClone(defaults);q.document.input.revision=7;q.document.extensions={bootModes:{schema:1,revision:3,current:mode}};
  q.calls=[];q.wire=[];q.effects=[];q.ownerReads=0;q.begin=q.end=q.ensure=0;q.clock=0;q.active=false;q.failEnd=q.failApply=false;q.hook=async()=>{};
  q.snapshot={controlProtocol:2,packAvailable:true,packPrepared:plan.name!=='apply-takeover',packOwned:plan.name!=='apply-takeover',packRepairRequired:false,backendReloadRequired:plan.name==='apply-reload',targetId:'a'.repeat(16),revision:'b'.repeat(64),linked:true,backendReady:!plan.cold&&!plan.offline,backendProcessCount:plan.offline?0:1,steamRunning:!plan.offline,configuredMatchesBackend:true,note:'inert appearance owner',themes:m.STEAM_APPEARANCE_THEMES.map(theme=>({name:theme.name,installed:true,configured:true,backendEnabled:true,parameterCount:0,error:''}))};
  const counts=new Map(),hit=async name=>{const count=(counts.get(name)||0)+1;counts.set(name,count);const label=name+'-'+count;q.calls.push(label);await q.hook(label);};
  const service=m.createSteamAppearance({loadBootPreferences:async()=>{q.ownerReads++;return structuredClone(q.document);},
   location:async()=>{await hit('location');return {directory:'Q:/owned/PowerControl'};},exists:async()=>{await hit('exists');return true;},hidden:async()=>{throw Error('Detached launch forbidden');},
   idlePolicy:async()=>{await hit('policy');return {supported:true,version:3,manualObservationProtocol:1,sharedSteamReadProtocol:1,processQuery:'Local\\YMCC.CssInventory.'+'a'.repeat(32),pid:42,birth:'100'};},
   beginUse:async()=>{q.begin++;q.active=true;q.effects.push('begin');await hit('begin');return {ok:true,token:'17',pid:42,birth:'100',backendPath:'Q:\\owned\\CssLoader-Standalone-Headless.exe'.replaceAll('\\\\','\\')};},
   ensureUse:async token=>{assert.equal(token,'17');q.ensure++;q.effects.push('ensure');q.snapshot.backendReady=true;await hit('ensure');return {ok:true};},
   endUse:async token=>{assert.equal(token,'17');q.end++;q.active=false;q.effects.push('release');await hit('release');if(q.failEnd)throw Error('inert release failure');return {ok:true};},
   delay:async()=>{await hit('delay');},
   run:async(program,argv,timeoutMs,options)=>{assert.equal(program,'powershell.exe');assert(timeoutMs>0);const action=argv[argv.indexOf('-Action')+1],previousRevision=q.snapshot.revision;q.wire.push({action,options:options?structuredClone(options):null});
    if(action!=='Status'){q.effects.push(action);if(action==='Apply'){if(q.failApply)throw Error('inert Apply failure');const payload=JSON.parse(Buffer.from(argv[argv.indexOf('-Payload')+1],'base64').toString('utf8'));for(const [name,value]of Object.entries(payload.states))q.snapshot.themes.find(row=>row.name===name).configured=value;}if(action==='Reset')for(const row of q.snapshot.themes)row.configured=true;q.snapshot.revision='c'.repeat(64);}
    if(action==='Prepare'){q.snapshot.packPrepared=q.snapshot.packOwned=true;q.snapshot.backendReloadRequired=true;}if(action==='Reload')q.snapshot.backendReloadRequired=false;
    await hit(action);return {exitCode:0,stdout:JSON.stringify({ok:true,saved:action!=='Status',...(action==='Reload'?{reloaded:true,previousRevision}:{}),snapshot:q.snapshot}),stderr:''};},
  });
  const context={input:structuredClone(q.document.input),extensions:structuredClone(q.document.extensions)},options={origin:'boot-mode',bootContext:context};return {service,options,expected:structuredClone(context)};
 }
 function mutate(kind){if(kind==='revision')q.document.input.revision++;if(kind==='hash')q.document.input.diagnostics.inputLoggingEnabled=!q.document.input.diagnostics.inputLoggingEnabled;if(kind==='mode')q.document.extensions.bootModes.revision++;if(kind==='result')q.document.extensions.bootModesStartupResult={schema:1,revision:99};if(kind==='observed')q.document.input.startupObservedBoot='changed-boot';if(kind==='ack')q.document.input.hostAcknowledgedRevision=17;if(kind==='unrelated')q.document.extensions.peer=true;}
 const invoke=(f,plan,options=f.options)=>plan.method==='reset'?f.service.reset(plan.name==='reset-previous'?'previous':'preset',options):f.service.apply(structuredClone(q.snapshot),{Round:false},options);
 const expectedRows=before?1:modes.length*kinds.length*plans.reduce((count,p)=>count+p.stages.length,0);
 const inputs=[...new Set([source,...Object.keys(build.metafile.inputs).filter(file=>!file.startsWith('appearance-owner-memory:')&&file!=='appearance-owner-entry.ts').map(file=>path.resolve(root,file)),'tools/boot_modes_fixture/appearanceOwner.mjs'].map(file=>path.resolve(root,file)))].map(pin);
 write('TASK.json',{originalCase:'boot Steam appearance applies carry scene origin across all four modes',selection:before?'before':extra?'extra-branches':'all',plans,modes,kinds,expectedRows,inputs,bundle:pin(module),executionTargetMs:30000,hardExecutionBoundary:'Existing parent Raw Job deadline; no separate inner watchdog',scope:'Actual appearance service plus original capture/owner hash helpers; settings/script/CSS lease ends are inert memory. Native CSS/PowerShell and shell cancellation internals are NOT accepted.'});
 try{
 for(const mode of modes)for(const plan of plans)for(const stage of plan.stages)for(const kind of kinds){
  if(before&&(mode!=='global'||plan.name!=='apply-ready'||stage!=='Status-1'||kind!=='hash'))continue;
  const f=reset(mode,plan),allowed=['ack','unrelated','current'].includes(kind);let changed=false,at=0,result,error='';
  q.hook=async label=>{if(!changed&&label===stage){changed=true;at=q.effects.length;mutate(kind);}};
  try{if(stage==='dispatch'){changed=true;at=q.effects.length;mutate(kind);}result=await invoke(f,plan);}catch(e){error=String(e);}
  const late=q.effects.slice(at).filter(effect=>effect!=='release'),row={mode,scenario:plan.name,stage,kind,allowed,changed,error,result,lateEffects:late,ownerReads:q.ownerReads,begin:q.begin,end:q.end,ensure:q.ensure,active:q.active,calls:q.calls,wire:q.wire};fs.appendFileSync(raw,JSON.stringify(row)+'\n');rows.push(row);
  assert(changed,'Selected appearance boundary not reached '+JSON.stringify(row));assert.equal(q.active,false,'Exact CSS lease leaked');assert.equal(q.end,q.begin,'Acquired ticket was not released exactly once');
  if(allowed){assert(!error,JSON.stringify(row));assert.equal(result?.ok,true,JSON.stringify(row));}
  else{assert.equal(late.length,0,'Expired appearance continued later operation '+JSON.stringify(row));assert.match(error,/BOOT_APPEARANCE_REQUEST_EXPIRED/,'Expired appearance returned old result '+JSON.stringify(row));}
  if(!before)for(const wire of q.wire){assert.equal(wire.options?.origin,'boot-mode');assert.deepEqual(wire.options.bootContext,f.expected);}
 }
 if(!before){
  for(const plan of plans){const f=reset('global',plan);const result=await invoke(f,plan,{});assert.equal(result.ok,true);assert.equal(q.ownerReads,0);assert.equal(q.active,false);controls.push({name:'ordinary-'+plan.name,ownerReads:0});}
  let f=reset('global',plans[0]);const pending=invoke(f,plans[0]);f.options.bootContext.input.revision=999;f.options.bootContext.extensions.bootModes.revision=999;assert.equal((await pending).ok,true);for(const wire of q.wire)assert.deepEqual(wire.options.bootContext,f.expected);controls.push({name:'caller-context-mutation-does-not-change-captured-request'});
  f=reset('global',plans[0]);let refused=false;try{await invoke(f,plans[0],{origin:'ordinary',bootContext:f.expected});}catch(e){refused=/INVALID_BOOT_PREFERENCE_CONTEXT/.test(String(e));}assert(refused&&q.calls.length===0);controls.push({name:'invalid-context-origin-before-external-io'});
  f=reset('global',plans[0]);q.failApply=q.failEnd=true;await assert.rejects(()=>invoke(f,plans[0]),/inert Apply failure/);assert.equal(q.end,1);assert(!q.active);controls.push({name:'primary-failure-preserved-and-release-not-retried'});
  f=reset('global',plans[0]);let changedOnce=false;q.hook=async label=>{if(label==='Status-1'&&!changedOnce){changedOnce=true;mutate('hash');}};await assert.rejects(()=>invoke(f,plans[0]),/BOOT_APPEARANCE_REQUEST_EXPIRED/);q.hook=async()=>{};const next={origin:'boot-mode',bootContext:{input:structuredClone(q.document.input),extensions:structuredClone(q.document.extensions)}};assert.equal((await invoke(f,plans[0],next)).ok,true);controls.push({name:'new-explicit-request-after-stale-serialization-is-allowed'});
 }
 for(const input of inputs)assert.equal(pin(input.path).sha256,input.sha256,'Appearance source changed during software execution '+input.path);
 assert.equal(rows.length,expectedRows);const result={status:'PASS',actualRows:rows.length,controls,plans,modes,kinds,inputs,bundle:pin(module),observations:pin(raw),nativeCssAndPowerShellInternals:'NOT_RUN',realSystemActions:false};write('RESULT.json',result);return result;
 }catch(error){write('FAILURE.json',{status:'FAIL',error:String(error),actualRows:rows.length,last:rows.at(-1),inputs,observations:pin(raw)});throw error;}
}
