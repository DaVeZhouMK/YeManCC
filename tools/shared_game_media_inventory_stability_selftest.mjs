import fs from'node:fs';import path from'node:path';import{createRequire}from'node:module';
const project=path.resolve(import.meta.dirname,'..'),sharedSteam=process.argv[2]==='--shared-steam-inventory',admission=process.argv[2]==='--current-game-admission',installationFlight=process.argv[2]==='--installation-flight',directed=sharedSteam||admission||installationFlight,out=process.argv[directed?3:2],before=process.argv[directed?4:3];if(!out)throw Error('evidence directory required');fs.mkdirSync(out,{recursive:true});
const{build}=createRequire(project+'/package.json')('esbuild');const powerAdmissionFunction=fs.readFileSync(project+'/src/bridge/powerLifecycleState.ts','utf8').match(/export function isSharedPowerLifecycleAdmitted[\s\S]*?\n}/)?.[0];if(!powerAdmissionFunction)throw Error('Public admission function unavailable');let calls=0;globalThis.__inventoryFixture={entries:[],files:{}};
if(installationFlight){if(process.argv.length!==4)throw Error('exact installation-flight selection and output required');const {installationFlightCases}=await import('./fixtures/steam_installation_consumer_flight.mjs');const record=await installationFlightCases(project);fs.writeFileSync(path.join(out,'installation-flight-results.json'),JSON.stringify(record,null,2)+'\n',{flag:'wx'});createRequire(project+'/package.json')('esbuild').stop();console.log(JSON.stringify(record));if(!record.pass)process.exitCode=1;}else if(sharedSteam){await sharedSteamInventoryProbe();}else if(admission){await currentGameAdmissionProbe();}else{
const moduleFile=out+'/inventory-'+(before?'before':'after')+'.mjs';
await build({entryPoints:[project+'/src/bridge/steamMediaSource.ts'],bundle:true,platform:'node',format:'esm',outfile:moduleFile,plugins:[{name:'local-files-and-identity-only',setup(api){if(before)api.onLoad({filter:/steamMediaSource\.ts$/},()=>({contents:fs.readFileSync(before,'utf8'),loader:'ts',resolveDir:project+'/src/bridge'}));api.onResolve({filter:/^\.\/(api|ipc|steamHttp|uiSettings)$/},args=>args.importer.endsWith('steamMediaSource.ts')?{path:args.path,namespace:'inventory-ports'}:null);api.onLoad({filter:/.*/,namespace:'inventory-ports'},args=>({loader:'js',resolveDir:project+'/src/bridge',contents:args.path==='./api'?`import {createSteamInstallationReader} from './steamInstallationFacts.ts';export const steamInformation={installationRoots:createSteamInstallationReader((...args)=>globalThis.__inventoryInvoke(...args))};export const registry={read:async()=>{globalThis.__inventoryPrivateReads++;throw Error('Private registry forbidden');}};export const fs={readDir:async()=>globalThis.__inventoryFixture.entries,readTextFile:async path=>path.endsWith('libraryfolders.vdf')?'':globalThis.__inventoryFixture.files[path.split('\\\\').at(-1)]};`:args.path==='./ipc'?`export const invoke=(...args)=>globalThis.__inventoryInvoke(...args);`:args.path==='./uiSettings'?`export const getUiSetting=()=>false,setUiSettings=async()=>{};`:`export const requestSteamHttp=()=>{throw Error('network forbidden in local inventory')},requestSteamJson=requestSteamHttp;`}));}}]});
const api=await import('file:///'+moduleFile.replaceAll('\\','/'));globalThis.__inventoryPrivateReads=0;const sharedCalls=[];globalThis.__inventoryInvoke=async(command,args)=>{sharedCalls.push({command,args});if(command==='steamInformation.get')return globalThis.__inventoryFixture.sourceFacts??{available:true,version:1,requested:1,installations:[{path:'C:/FixtureSteam',source:'hkcu-steam-path',error:0}]};calls++;return{}};
let checks=0;const results=[];
const cases=[
 ['valid','570','570','Dota 2',true],['filename-mismatch','999','570','Game',false],['overflow','4294967296','4294967296','Game',false],['scientific-id','570','5.7e2','Game',false],['missing-folder','570','570','',false],['parent-folder','570','570','../Other',false],['nested-parent','570','570','Game/../Other',false],['absolute-folder','570','570','C:/Other',false],['rooted-folder','570','570','/Other',false],['dot-folder','570','570','./Other',false],['unicode-folder','570','570','游戏 文件夹',true],['nested-folder','570','570','Group/Game',true],['dots-in-name','570','570','Game..2',true],['infinite-priority','570','570','Game',true,'Infinity'],['negative-priority','570','570','Game',true,'-5'],['future-priority','570','570','Game',true,'999999999999'],
];
for(const[name,fileId,id,dir,expected,played='100']of cases){
 const file=`appmanifest_${fileId}.acf`;globalThis.__inventoryFixture={entries:[{name:file,isFile:true}],files:{[file]:`"AppState" { "appid" "${id}" "name" "Fixture" "installdir" "${dir}" "LastPlayed" "${played}" }`}};
 try{const values=await api.installedSteamWallpaperTargets();checks++;if((values.length===1)!==expected)throw Error('manifest identity/path admission differs from expected');if(expected){checks++;if(!Number.isFinite(values[0].lastPlayed)||values[0].lastPlayed<0||values[0].lastPlayed>Date.now())throw Error('priority timestamp is not bounded and finite');}results.push({name,ok:true});}catch(error){results.push({name,ok:false,error:error.message});}
}
for(const [name,sourceFacts,expected]of [
 ['unavailable',{available:false},false],['wrong-version',{available:true,version:2,requested:1,installations:[{path:'C:/FixtureSteam',error:0}]},false],
 ['wrong-mask',{available:true,version:1,requested:7,installations:[{path:'C:/FixtureSteam',error:0}]},false],
 ['null-installations',{available:true,version:1,requested:1,installations:null},false],
 ['denied-row',{available:true,version:1,requested:1,installations:[{path:'C:/FixtureSteam',error:5}]},false],
 ['nonabsolute-row',{available:true,version:1,requested:1,installations:[{path:'../FixtureSteam',error:0}]},false],
 ['zero-error-string',{available:true,version:1,requested:1,installations:[{path:'C:/FixtureSteam',error:'0'}]},false],
 ['same-installation-case-only',{available:true,version:1,requested:1,installations:[{path:'C:/FixtureSteam',error:0},{path:'c:/fixturesteam/',error:0}]},true],
 ]){
  globalThis.__inventoryFixture={sourceFacts,entries:[{name:'appmanifest_570.acf',isFile:true}],files:{'appmanifest_570.acf':'"AppState" { "appid" "570" "name" "Fixture" "installdir" "Game" }'}};
  const values=await api.installedSteamWallpaperTargets();checks++;results.push({name:'shared-installation-'+name,ok:(values.length===1)===expected});
 }
 checks++;results.push({name:'shared-installation-never-reads-private-registry',ok:globalThis.__inventoryPrivateReads===0});
 checks++;results.push({name:'shared-installation-never-requests-presence',ok:sharedCalls.length>0&&sharedCalls.every(call=>call.command==='steamInformation.get'&&call.args.mask===1)});
 const timeout=Object.assign(Error('fixture directory response timed out'),{code:'IPC_TIMEOUT'});
 globalThis.__inventoryFixture={get entries(){throw timeout;},files:{}};
 let incomplete=false;try{await api.installedSteamWallpaperTargets(new AbortController().signal);}catch(error){incomplete=error===timeout;}
 checks++;results.push({name:'inventory-timeout-is-not-an-empty-complete-scan',ok:incomplete});
 for(const id of[570,2147483648,4294967296,Number.MAX_SAFE_INTEGER]){globalThis.__inventoryInvoke=async()=>({verified:true,appId:id,name:'cached game'});try{const target=await api.resolveSteamWallpaperIdentity({path:'C:/Game/game.exe',name:'same title'});checks++;if((target.appId>0)!==(id<=0x7fffffff))throw Error('cached identity outside Steam AppID range');results.push({name:'cache-id-'+id,ok:true});}catch(error){results.push({name:'cache-id-'+id,ok:false,error:error.message});}}
checks++;results.push({name:'inventory-never-uses-remote-worker',ok:calls===0});
const report={ok:results.every(v=>v.ok),cases:results.length,checks,results,scope:'production inventory and local identity, injected local files/registry/IPC only; network forbidden'};fs.writeFileSync(out+'/inventory-results.json',JSON.stringify(report,null,2));console.log(JSON.stringify(report));if(!report.ok)process.exitCode=1;

}

async function currentGameAdmissionProbe(){
 const {strict:assert}=await import('node:assert');const fixtureSymbol='__currentGameMediaAdmissionFixture';
 const moduleFile=path.resolve(out,'current-game-admission.mjs');
 await build({entryPoints:[project+'/src/bridge/sharedGameMedia.ts'],bundle:true,platform:'node',format:'esm',outfile:moduleFile,plugins:[{name:'current-game-owned-ports',setup(api){
  if(before)api.onLoad({filter:/sharedGameMedia\.ts$/},()=>({contents:fs.readFileSync(before,'utf8'),loader:'ts',resolveDir:project+'/src/bridge'}));
  api.onResolve({filter:/^\.\/(gameMediaDownloads|ipc|gamePolicyTarget|gameMediaPreparationPolicy|uiSettings|requestSteamHttp|steamHttp|steamMediaPlaylist|steamMediaSource|steamUi|powerLifecycleState)$/},args=>args.importer.replaceAll('\\','/').endsWith('/sharedGameMedia.ts')?{path:args.path,namespace:'owned-admission-ports'}:null);
  api.onLoad({filter:/.*/,namespace:'owned-admission-ports'},args=>{const ref=`globalThis.${fixtureSymbol}`;let code='';
   if(args.path==='./gameMediaDownloads')code=`export const backgroundDownloadEnabled=()=>${ref}.enabled;export const gameMediaDownloads={state:{},start:()=>()=>{},policy:()=>{},subscribe:()=>()=>{},watchCompleted:()=>()=>{},recheck:async()=>{}};`;
   else if(args.path==='./ipc')code=`export const invoke=(...args)=>${ref}.invoke(...args),on=(name,fn)=>{const entries=${ref}.events.get(name)||new Set();${ref}.events.set(name,entries);entries.add(fn);return()=>entries.delete(fn);};`;
   else if(args.path==='./gamePolicyTarget')code=`export const subscribePolicyGameStatus=fn=>{${ref}.game=value=>{${ref}.currentGame=value;fn(value);};return()=>{${ref}.game=null;}},refreshPolicyGameStrict=async()=>{const f=${ref};if(f.confirmations)return new Promise((resolve,reject)=>f.confirmations.push(Object.assign(resolve,{reject})));return f.currentGame;};export const isPolicyGameCurrent=()=>${ref}.gameCurrent===true,getPolicyGame=()=>${ref}.currentGame,subscribePolicyGameReadiness=fn=>{${ref}.readiness=fn;fn(isPolicyGameCurrent());return()=>{${ref}.readiness=null;}};`;
   else if(args.path==='./gameMediaPreparationPolicy')code=`export class GameMediaPreparationPolicy{constructor(){}game(value){${ref}.gameGates.push(value)}request(){}demand(){}suspend(){}resume(){}notificationAdvanced(){}stop(){}}export const subscribeGameMediaResume=(fn,on)=>on('power.resumed',fn);`;
   else if(args.path==='./uiSettings')code=`export const getUiSetting=()=>false,setUiSettings=async()=>{},uiSettingsLoaded=()=>${ref}.loaded;`;
   else if(args.path==='./steamMediaSource')code=`export const resolveSteamWallpaperIdentity=(game,signal)=>${ref}.resolve(game,signal),installedSteamWallpaperTargets=async()=>[],steamWallpaperCandidates=async()=>{throw Error('network forbidden')},normalizeSteamMediaUrl=value=>value;`;
   else if(args.path==='./steamHttp')code=`export const requestSteamHttp=async()=>{throw Error('network forbidden')};`;
   else if(args.path==='./steamUi')code=`export const steamUi={read:async()=>({known:false,running:false,mode:'unknown',pid:0,birth:'0',reason:'not-selected'}),subscribe:()=>()=>{}};`;
   else if(args.path==='./powerLifecycleState')code=powerAdmissionFunction+`;export const onSharedPowerLifecycle=fn=>{${ref}.lifecycle=fn;fn({phase:'ready',generation:1,suspendSerial:0,observedNotificationSequence:1,known:true,hardwareWritesAllowed:true,notificationPending:false});return()=>{${ref}.lifecycle=null;}};`;
   else code=`export const parseHlsAsset=()=>{throw Error('network forbidden')};`;
   return{loader:'ts',contents:code};
  });
 }}]});
 const module=await import('file:///'+moduleFile.replaceAll('\\','/'));let checks=0;const cases=[];const check=(ok,label)=>{checks++;assert.ok(ok,label);};const flush=async()=>{for(let i=0;i<8;i++)await Promise.resolve();};
 const game=(id=1,extra={})=>({pid:id,processCreated:'birth-'+id,path:'G:/Games/'+id+'.exe',name:'Game '+id,title:'Game '+id,identityGeneration:id,source:'memory',...extra});
 const saved={window:globalThis.window,setTimeout:globalThis.setTimeout,clearTimeout:globalThis.clearTimeout,now:Date.now};
 let clock=100000,timerId=0;const timers=new Map();globalThis.window=new EventTarget();globalThis.setTimeout=(fn,delay)=>{timers.set(++timerId,{fn,delay});return timerId;};globalThis.clearTimeout=id=>timers.delete(id);Date.now=()=>clock;
 function fixture(){
  const f={enabled:true,loaded:true,events:new Map(),gameGates:[],queries:[],submissions:[],game:null,logicalReads:0,coalesce:false,resolve(value,signal){f.logicalReads++;if(f.coalesce&&!signal){const prior=f.queries.find(q=>!q.signal&&q.game.path.toLowerCase()===value.path.toLowerCase());if(prior)return prior.promise;}let resolve,reject;const promise=new Promise((yes,no)=>{resolve=yes;reject=no;});f.queries.push({promise,signal,game:{...value},resolve:target=>resolve(target||{appId:value.pid,name:value.name,executable:value.path,verified:true}),reject});return promise;},invoke:async(command,args)=>{if(command==='gameMedia.watch')return true;if(command==='gameMedia.snapshot')return {entries:{},aliases:{}};if(command==='gameMedia.background'){f.submissions.push(structuredClone(args));return {};}throw Error('unexpected '+command);},emit(name){if(name==='power.suspending')f.lifecycle?.({phase:'suspending',generation:2,known:true,hardwareWritesAllowed:false,notificationPending:true});else if(name==='power.resumed')f.lifecycle?.({phase:'ready',generation:2,known:true,hardwareWritesAllowed:true,notificationPending:false});for(const fn of f.events.get(name)||[])fn({});},settings(){window.dispatchEvent(new Event('ui-settings:changed'));}};
  globalThis[fixtureSymbol]=f;f.stop=module.startSharedGameMedia();return f;
 }
 async function run(name,fn){let f;const start=checks;try{clock=100000;f=fixture();await flush();await fn(f);cases.push({name,ok:true,checks:checks-start,work:{logicalReads:f.logicalReads,localRequests:f.queries.length,nativeEnrollments:f.submissions.length}});}catch(error){cases.push({name,ok:false,error:String(error?.message||error),checks:checks-start,work:f?{logicalReads:f.logicalReads,localRequests:f.queries.length,nativeEnrollments:f.submissions.length}:null});}finally{f?.stop();await flush();timers.clear();}}
 try{
  await run('game-switch-rejects-late-old-identification',async f=>{f.game(game(1));f.game(game(2));f.queries[0].resolve();await flush();check(f.submissions.length===0,'old target cannot enter queue after current target changed');f.queries[1].resolve();await flush();check(f.submissions.length===1&&f.submissions[0].targets[0].appId===2,'current target enters exactly once');});
  await run('game-exit-rejects-late-identification',async f=>{f.game(game());f.game(null);f.queries[0].resolve();await flush();check(f.submissions.length===0,'exited game never re-enters by old callback');check(f.gameGates.at(-1)===false,'exit policy still updates synchronously');});
  await run('saved-off-rejects-late-identification',async f=>{f.game(game());f.enabled=false;f.settings();f.queries[0].resolve();await flush();check(f.submissions.length===0,'saved OFF owns late callback');});
  await run('off-on-handoff-does-not-revive-old-request',async f=>{f.game(game());f.enabled=false;f.settings();f.enabled=true;f.settings();check(f.queries.length===2,'ON makes a fresh local read for the current game');f.queries[0].resolve();await flush();check(f.submissions.length===0,'prior enabled epoch stays retired');f.queries[1].resolve();await flush();check(f.submissions.length===1,'new enabled epoch may submit current target');});
  await run('aba-selection-owns-only-new-a',async f=>{f.game(game(1));f.game(game(2));f.game(game(1));f.queries[0].resolve();f.queries[1].resolve();await flush();check(f.submissions.length===0,'old A and B cannot steal new A selection');f.queries[2].resolve();await flush();check(f.submissions.length===1&&f.submissions[0].targets[0].appId===1,'latest A remains eligible');});
  await run('repeated-title-updates-do-not-repeat-background-io',async f=>{f.coalesce=true;f.game(game());for(let n=0;n<50;n++)f.game(game(1,{title:'Frame '+n,name:'Window '+n}));f.queries[0].resolve();await flush();check(f.submissions.length===1,'same owned identity has one native enrollment even when resolver shares its pending read');check(f.logicalReads===1,'stable identity invokes resolver only once');for(let n=0;n<30;n++)f.game(game(1,{title:'More '+n}));await flush();check(f.queries.length===1,'healthy stable selection does not reread on title changes');});
  await run('pid-reuse-and-identity-generation-trigger-fresh-read',async f=>{f.game(game());f.queries[0].resolve();await flush();f.game(game(1,{processCreated:'new-birth'}));check(f.queries.length===2,'new birth is a distinct identity');f.queries[1].resolve();await flush();f.game(game(1,{processCreated:'new-birth',identityGeneration:99}));check(f.queries.length===3,'new authority identity revision stays distinct');f.queries[2].resolve();await flush();check(f.submissions.length===3,'each actual identity transition is preserved');});
  await run('played-at-is-observation-time',async f=>{clock=123000;f.game(game());clock=456000;f.queries[0].resolve();await flush();check(f.submissions[0].targets[0].lastPlayed===123000,'slow resolution must not fabricate later play recency');});
  await run('suspend-retires-old-admission-and-resume-rereads',async f=>{f.game(game());f.emit('power.suspending');f.queries[0].resolve();await flush();check(f.submissions.length===0,'suspended old identity cannot admit');f.emit('power.resumed');await flush();check(f.queries.length===2,'resume requests one current fresh identity after authoritative refresh');f.queries[1].resolve();await flush();check(f.submissions.length===1,'resumed owner may enroll');});
  for(const merged of [false,true])await run(merged?'coalesced-ready-does-not-join-old-executable-read':'resume-does-not-join-old-executable-read',async f=>{
   f.coalesce=true;f.game(game());const old=f.queries[0];
   const ready={phase:'ready',generation:merged?1:2,suspendSerial:1,observedNotificationSequence:2,known:true,hardwareWritesAllowed:true,notificationPending:false};
   if(!merged){f.lifecycle({...ready,phase:'suspending',hardwareWritesAllowed:false,notificationPending:true});await flush();check(f.submissions.length===0,'suspending cannot enroll the old executable');}
   f.lifecycle(ready);await flush();
   check(f.queries.length===2,'a new Ready must start an independent identity read before the old executable read returns');
   check(old.signal?.aborted,'the previous scope releases its identity IPC wait');
   f.queries[1].resolve();await flush();check(f.submissions.length===1,'current Ready enrolls the game before the old reply');
   old.resolve({appId:99,name:'Stale',executable:old.game.path,verified:true});f.lifecycle(ready);await flush();
   check(f.submissions.length===1&&f.submissions[0].targets[0].appId===1&&f.queries.length===2,'the late old reply and repeated Ready cannot enroll or retry');
  });
  const admittedNotice=observedNotificationSequence=>({phase:'ready',generation:1,suspendSerial:0,observedNotificationSequence,known:true,hardwareWritesAllowed:true,notificationPending:false});
  await run('new-sequence-reconfirms-held-game-before-enrollment',async f=>{
   clock=123000;f.game(game());const old=f.queries[0];f.confirmations=[];
   clock=456000;f.lifecycle(admittedNotice(2));await flush();
   old.resolve({appId:99,name:'Stale',executable:old.game.path,verified:true});await flush();
   check(f.submissions.length===0,'the previous sequence cannot enroll while game authority for the new sequence is pending');
   check(old.signal?.aborted&&f.confirmations.length===1&&f.queries.length===1,'one current game confirmation replaces the obsolete identity wait');
   f.confirmations[0](game());await flush();check(f.queries.length===2,'confirmed game may request its current identity');
   f.queries[1].resolve();await flush();
   check(f.submissions.length===1&&f.submissions[0].targets[0].appId===1&&f.submissions[0].targets[0].lastPlayed===123000,'continuing the same game preserves the initial play observation time');
   f.lifecycle(admittedNotice(2));f.lifecycle(admittedNotice(1));f.lifecycle(admittedNotice(3));await flush();
   check(f.confirmations.length===1&&f.queries.length===2&&f.submissions.length===1,'duplicate, older and healthy newer notices do not request or enroll again');
  });
  for(const terminal of ['exited','off'])await run(`new-sequence-respects-${terminal}-while-confirming-game`,async f=>{
   f.game(game());const old=f.queries[0];f.confirmations=[];
   f.lifecycle(admittedNotice(2));f.lifecycle(admittedNotice(3));await flush();
   if(terminal==='off'){f.enabled=false;f.settings();}
   old.resolve();await flush();check(f.submissions.length===0,'obsolete identity cannot enroll during the newer confirmation');
   check(f.confirmations.length===2,'consecutive notifications replace only the pending game confirmation');
   f.confirmations[0](game());await flush();check(f.queries.length===1,'late game confirmation cannot start a new identity lookup');
   f.confirmations[1](terminal==='exited'?null:game());await flush();
   check(f.queries.length===1&&f.submissions.length===0,'no current game or OFF remains idle');
  });
  await run('failed-confirmation-resumes-on-same-game-readiness',async f=>{
   clock=123000;f.game(game());const old=f.queries[0];f.confirmations=[];
   clock=234000;f.lifecycle(admittedNotice(2));await flush();
   f.confirmations[0].reject(Error('current game read temporarily unavailable'));old.resolve();await flush();
   check(f.submissions.length===0&&f.queries.length===1,'failed confirmation cannot release the previous identity');
   clock=345000;f.gameCurrent=true;f.readiness?.(true);await flush();
   check(f.queries.length===2,'public game readiness must continue the same game without a selection change or another wake');
   f.queries[1].resolve();f.readiness?.(true);await flush();
   check(f.submissions.length===1&&f.submissions[0].targets[0].lastPlayed===123000&&f.queries.length===2,'readiness continues the same observation only once');
  });
  await run('resume-does-not-reenroll-a-game-that-exited-during-sleep',async f=>{f.game(game());f.emit('power.suspending');f.queries[0].resolve();await flush();f.currentGame=null;f.emit('power.resumed');await flush();check(f.queries.length===1&&f.submissions.length===0,'fresh authoritative empty target wins over retained pre-sleep game');});
  await run('unloaded-settings-do-not-admit',async f=>{f.loaded=false;f.settings();f.game(game());check(f.queries.length===0,'unloaded intent cannot start identification');f.loaded=true;window.dispatchEvent(new Event('ui-settings:loaded'));check(f.queries.length===1,'loaded enabled intent reconciles current game');});
  await run('stop-ignores-already-issued-read',async f=>{f.game(game());f.stop();f.queries[0].resolve();await flush();check(f.submissions.length===0,'stop never revives accepted read callback');});
  await run('unverified-and-rejected-local-reads-never-start-network',async f=>{f.game(game());f.queries[0].resolve({appId:1,verified:false});await flush();check(f.submissions.length===0,'unverified result remains nonadmitted');f.game(game(2));f.queries[1].reject(Error('local unavailable'));await flush();check(f.submissions.length===0,'read failure never submits guessed target');});
  await run('case-only-path-and-setting-noise-remain-idle',async f=>{f.game(game());f.queries[0].resolve();await flush();f.game(game(1,{path:'g:/games/1.EXE'}));for(let n=0;n<50;n++)f.settings();await flush();check(f.queries.length===1&&f.submissions.length===1,'same identity and unchanged enabled intent cause no additional IPC');check([...timers.values()].every(t=>t.delay>=30000),'no extra background polling introduced');});
 }finally{globalThis.window=saved.window;globalThis.setTimeout=saved.setTimeout;globalThis.clearTimeout=saved.clearTimeout;Date.now=saved.now;delete globalThis[fixtureSymbol];}
 const result={ok:cases.every(c=>c.ok),cases:cases.length,checks,results:cases,scope:'actual bundled startSharedGameMedia ownership, injected event/settings/identity/native ports; no installed app or network'};fs.writeFileSync(path.join(out,'current-game-admission-results.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;
}

async function sharedSteamInventoryProbe(){
 const {strict:assert}=await import('node:assert');
 const selected=process.argv[5],symbol='__sharedSteamMediaInventoryFixture',moduleFile=path.resolve(out,'shared-steam-inventory.mjs');
 if(process.argv.length>6||selected!==undefined&&!selected.trim())throw Error('Invalid shared Steam inventory selection');
 await build({stdin:{contents:"export * from './src/bridge/sharedGameMedia.ts';export {steamUi} from './src/bridge/steamUi.ts';",resolveDir:project,loader:'ts'},bundle:true,platform:'node',format:'esm',outfile:moduleFile,metafile:true,plugins:[{name:'shared-steam-inventory-owned-ports',setup(api){
  if(before)api.onLoad({filter:/sharedGameMedia\.ts$/},()=>({contents:fs.readFileSync(before,'utf8'),loader:'ts',resolveDir:project+'/src/bridge'}));
  api.onResolve({filter:/^\.\/(gameMediaDownloads|ipc|gamePolicyTarget|gameMediaPreparationPolicy|uiSettings|steamHttp|steamMediaPlaylist|steamMediaSource|powerLifecycleState|wallpaperPowerPolicy|yeman)$/},args=>{
   const source=args.importer.replaceAll('\\','/');return source.endsWith('/sharedGameMedia.ts')||source.endsWith('/steamUi.ts')?{path:args.path,namespace:'shared-steam-inventory-ports'}:null;
  });
  api.onLoad({filter:/.*/,namespace:'shared-steam-inventory-ports'},args=>{
   const ref=`globalThis.${symbol}`;let contents;
   if(args.path==='./ipc')contents=`export const invoke=(...a)=>${ref}.invoke(...a),on=(name,fn,includeDispatch)=>${ref}.on(name,fn,includeDispatch);`;
   else if(args.path==='./gameMediaDownloads')contents=`export const backgroundDownloadEnabled=()=>${ref}.enabled;export const gameMediaDownloads={get state(){return ${ref}.downloadState;},start:()=>()=>{},policy:reason=>${ref}.preparationReasons?.push(reason),subscribe:fn=>{${ref}.downloadListeners.add(fn);return()=>${ref}.downloadListeners.delete(fn);},watchCompleted:()=>()=>{},recheck:async()=>{}};`;
   else if(args.path==='./gamePolicyTarget')contents='export const subscribePolicyGameStatus=()=>()=>{},refreshPolicyGameStrict=async()=>null,isPolicyGameCurrent=()=>false,getPolicyGame=()=>null,subscribePolicyGameReadiness=()=>()=>{};';
   else if(args.path==='./gameMediaPreparationPolicy')contents=`import {GameMediaPreparationPolicy as ActualPolicy} from './src/bridge/gameMediaPreparationPolicy.ts';export class GameMediaPreparationPolicy{constructor(ports){if(${ref}.preparationReads)return new ActualPolicy(ports);}game(){}request(){}demand(){}suspend(){}resume(){}notificationAdvanced(){}stop(){}}`;
   else if(args.path==='./uiSettings')contents=`export const getUiSetting=()=>true,setUiSettings=async()=>{},uiSettingsLoaded=()=>${ref}.loaded;`;
   else if(args.path==='./steamMediaSource')contents=`export const installedSteamWallpaperTargets=async()=>{${ref}.installed++;return[];},resolveSteamWallpaperIdentity=async()=>{throw Error('not selected');},steamWallpaperCandidates=async()=>{throw Error('network forbidden');},normalizeSteamMediaUrl=v=>v;`;
   else if(args.path==='./powerLifecycleState')contents=powerAdmissionFunction+`;export const onSharedPowerLifecycle=(fn,options)=>{const initial={phase:'ready',generation:1,suspendSerial:0,observedNotificationSequence:21,known:true,hardwareWritesAllowed:true,notificationPending:false};${ref}.lifecycle=fn;${ref}.observeSuspendSerial=options?.observeSuspendSerial===true;${ref}.publishNotice=state=>{if(options?.observeNotificationSequence===true)fn(state);};fn(initial);return()=>{${ref}.lifecycle=${ref}.publishNotice=null;}};`;
   else if(args.path==='./wallpaperPowerPolicy')contents=`export const wallpaperPowerBlocked=()=>${ref}.blocked,wallpaperPowerReason=()=>'wallpaper-on-battery',subscribeWallpaperPowerChanged=fn=>{${ref}.powerChanged=fn;return()=>{${ref}.powerChanged=null;}},captureWallpaperPower=()=>${ref}.powerEpoch,wallpaperPowerCurrent=epoch=>epoch===${ref}.powerEpoch;`;
   else if(args.path==='./yeman')contents='export const steamRunning=async()=>{throw Error("Private/legacy running read forbidden");};';
   else contents='export const requestSteamHttp=async()=>{throw Error("network forbidden");},parseHlsAsset=()=>{throw Error("network forbidden");};';
   return{contents,loader:'ts',resolveDir:project};
  });
 }}]}).then(result=>fs.writeFileSync(path.join(out,'bundle-inputs.json'),JSON.stringify(result.metafile,null,2),{flag:'wx'}));
 const saved={window:globalThis.window,setTimeout:globalThis.setTimeout,clearTimeout:globalThis.clearTimeout,now:Date.now,performance:globalThis.performance};
 let clock=100000,timerId=0,checks=0,fixtureId=0;const timers=new Map(),rows=[],fallbackGap=5000;
 globalThis.window=new EventTarget();globalThis.setTimeout=(fn,delay)=>{const id=++timerId;timers.set(id,{fn,at:clock+delay,delay});return id;};globalThis.clearTimeout=id=>timers.delete(id);Date.now=()=>clock;
 globalThis.performance={now:()=>clock};
 const flush=async()=>{for(let i=0;i<24;i++)await Promise.resolve();};
 const advance=async delta=>{clock+=delta;for(let n=0;n<16;n++){await flush();const due=[...timers].filter(([,t])=>t.at<=clock);if(!due.length)return;for(const[id,t]of due)if(timers.delete(id))t.fn();}throw Error('Unbounded timer replay');};
 const state=(extra={})=>({known:true,running:true,mode:'bigpicture',pid:400,birth:'12345',reason:'shared',observedAccount:17,modeEpoch:'document-a:1',...extra});
 const check=(ok,label)=>{checks++;assert.ok(ok,label);};
 const origins=(f,bits)=>{checks++;assert.deepEqual(f.inventoryArgs.at(-1),{originMask:bits},'inventory request includes only the expected bounded origin mask');};
 async function fixture(beforeBoot,setup){
  const f={enabled:true,loaded:true,blocked:false,powerEpoch:1,installed:0,inventories:0,inventoryArgs:[],holdInventory:false,inventoryFlights:[],holdSnapshot:false,snapshots:[],submissions:[],events:new Map(),reads:[],hold:false,state:state(),lifecycle:null,powerChanged:null,downloadState:{},downloadListeners:new Set(),
   on(name,fn,includeDispatch){const list=f.events.get(name)||new Set(),listener=event=>includeDispatch?fn(event.detail,event):fn(event.detail);f.events.set(name,list);list.add(listener);return()=>list.delete(listener);},
   emit(){const event=new CustomEvent('ipc:steam.uiStateChanged',{detail:{}});for(const fn of f.events.get('steam.uiStateChanged')||[])fn(event);},
   inventoryNotice(){const event=new CustomEvent('ipc:gameMedia.inventoryChanged',{detail:{}});for(const fn of f.events.get('gameMedia.inventoryChanged')||[])fn(event);},
   snapshotNotice(){const event=new CustomEvent('ipc:gameMedia.changed',{detail:{}});for(const fn of f.events.get('gameMedia.changed')||[])fn(event);},
   manualRequest(id){f.downloadState={manualUntil:clock+30000,inventoryRequestId:id};for(const fn of f.downloadListeners)fn();},
   invoke:async(command,args,options)=>{
    if(command==='steam.uiState'){check(JSON.stringify(args)==='{}','shared UI reads must remain automatic/cache-only');const r={args,options,live:false};f.reads.push(r);if(f.hold)return new Promise((resolve,reject)=>{
     const signal=options?.signal;
     const clear=()=>{r.live=false;signal?.removeEventListener('abort',abort);};
     const abort=()=>{clear();reject(Error('IPC request retired: steam.uiState'));};
     r.live=true;r.resolve=value=>{clear();resolve(value);};r.reject=error=>{clear();reject(error);};
     if(signal?.aborted)abort();else signal?.addEventListener('abort',abort,{once:true});
    });return structuredClone(f.state);}
    if(command==='gameMedia.inventory'){f.inventories++;f.inventoryArgs.push(structuredClone(args));if(f.holdInventory)return new Promise(resolve=>{f.inventoryFlights.push({resolve,signal:options?.signal});f.releaseInventory=()=>resolve([]);});return[];}
    if(command==='gameMedia.snapshot'){const r={signal:options?.signal};f.snapshots.push(r);if(f.holdSnapshot)return new Promise(resolve=>{r.resolve=resolve;});return{entries:{},aliases:{}};}
    if(command==='gameMedia.preparationPolicy'&&f.preparationReads)return new Promise((resolve,reject)=>f.preparationReads.push({resolve,reject,signal:options?.signal}));
    if(command==='gameMedia.background'){f.submissions.push(structuredClone(args));return{};}
    if(command==='gameMedia.watch'){
     f.watchCalls.push(args.enabled);
     if(args.enabled&&f.watchFailures>0){f.watchFailures--;throw Error('media watch request or reply lost');}
     if(args.enabled&&f.holdWatch)return new Promise((resolve,reject)=>{
      const abort=()=>reject(Error('media watch request retired'));
      options?.signal?.addEventListener('abort',abort,{once:true});
      f.watchReply=value=>{options?.signal?.removeEventListener('abort',abort);resolve(value);};
      f.watchRequestOptions=options;
     });
     return true;
    }
    throw Error('Forbidden native command '+command);
   }};
  f.watchCalls=[];f.watchFailures=0;setup?.(f);
  globalThis[symbol]=f;const module=await import('file:///'+moduleFile.replaceAll('\\','/')+'?fixture='+(++fixtureId));f.stop=module.startSharedGameMedia();f.displayHint=()=>module.sharedGameMedia.changed('inventory');f.steamUi=module.steamUi;await flush();beforeBoot?.(f);await advance(30000);await advance(250);return f;
 }
 const definitions=[
  ['shared-source-without-decky',async f=>{check(f.events.get('steam.uiStateChanged')?.size===1,'wallpaper inventory must subscribe to common Steam source');check(!f.events.has('deckySidebar.updated'),'Steam inventory must not depend on Decky status');check(f.reads.length===1&&f.installed===1&&f.inventories===1,'initial shared snapshot and original startup scan are coalesced');}],
  ['boot-retains-all-inventory-origins',async f=>{check(f.inventories===1&&f.installed===1,'startup and pre-boot hints issue exactly one full read');origins(f,1|2|8|32);},f=>{f.inventoryNotice();f.displayHint();}],
  ['passive-notices-retain-one-full-trailing-scan',async f=>{const count=f.inventories,installed=f.installed;
   for(const gap of [2000,3000,4000]){await advance(gap);f.inventoryNotice();f.displayHint();await advance(0);}
   check(f.inventories===count&&f.installed===installed,'spaced passive notices must not each rescan');check(timers.size===1,'one pending passive scan');
   await advance(749);check(f.inventories===count,'deadline is not early');await advance(1);
   check(f.inventories===count+1&&f.installed===installed+1,'one trailing scan retains the full request');
   origins(f,1|2);
   await advance(60000);check(f.inventories===count+1&&timers.size===0,'no idle polling');}],
  ['manual-inventory-bypasses-passive-delay',async f=>{const count=f.inventories,installed=f.installed;
   f.inventoryNotice();await advance(0);check(f.inventories===count,'passive request waits');f.manualRequest('manual-fixture');await flush();await advance(0);
   check(f.inventories===count+1&&f.installed===installed+1,'a manual request lifts the gap and keeps the full scan');
   origins(f,1|4);
   check(f.submissions.at(-1)?.inventoryRequestId==='manual-fixture','manual readiness uses exact native ID');
   await advance(60000);check(f.inventories===count+1&&timers.size===0,'manual scan consumes the pending hint');}],
  ['pending-passive-inventory-does-not-run-on-dc',async f=>{const count=f.inventories;f.displayHint();await advance(0);check(timers.size===1,'one passive timer');
   f.blocked=true;f.powerEpoch++;f.powerChanged(true);await advance(10000);
   check(f.inventories===count&&timers.size===0,'pending passive work cannot read during power withdrawal');
   f.blocked=false;f.powerEpoch++;f.powerChanged(false);await advance(0);
   check(f.inventories===count+1,'power recovery performs the existing read');origins(f,16);}],
  ['origins-arriving-during-read-stay-with-successor',async f=>{const count=f.inventories,installed=f.installed;
   f.holdInventory=true;f.state=state({mode:'desktop'});f.emit();await flush();await advance(250);
   check(f.inventories===count+1&&f.installed===installed,'mode change starts the existing local-only read');origins(f,256);
   f.inventoryNotice();f.displayHint();await advance(500);check(f.inventories===count+1,'no parallel read for new origins');origins(f,256);
   f.holdInventory=false;f.releaseInventory();await flush();await advance(9500);
   check(f.inventories===count+2&&f.installed===installed+1,'one trailing full read retains hints');origins(f,1|2);}],
  ['stable-source-coalesces-event-burst',async f=>{const count=f.inventories,installed=f.installed,start=f.reads.length;f.hold=true;for(let n=0;n<1000;n++)f.emit();check(f.reads.length===start+1,'only one shared read is in flight per burst');f.reads.at(-1).resolve(f.state);await flush();await advance(fallbackGap-1);check(f.reads.length===start+1,'successor waits for the original shared read deadline');await advance(1);check(f.reads.length===start+2,'one bounded successor covers events during read');f.reads.at(-1).resolve(f.state);await flush();await advance(500);check(f.inventories===count&&f.installed===installed,'stable shared facts must not rescan media or native manifests');}],
  ['account-change-without-process-change',async f=>{const count=f.inventories,installed=f.installed;f.state=state({observedAccount:18});f.emit();await flush();await advance(250);check(f.inventories===count+1&&f.installed===installed,'current UI account change refreshes only account media inventory');origins(f,64);}],
  ['same-pid-new-birth-refreshes-installed',async f=>{const count=f.inventories,installed=f.installed;f.state=state({birth:'67890'});f.emit();await flush();await advance(0);check(f.inventories===count+1&&f.installed===installed+1,'new Steam process identity refreshes native inventory');origins(f,32);}],
  ['late-source-read-cannot-scan-old-account',async f=>{const count=f.inventories;f.hold=true;f.emit();const old=f.reads.at(-1);f.emit();old.resolve(state({observedAccount:99}));await flush();await advance(fallbackGap);check(f.inventories===count,'superseded shared response must not scan inventory');f.reads.at(-1).resolve(state({observedAccount:18}));await flush();await advance(250);check(f.inventories===count+1,'latest shared response alone refreshes inventory');}],
  ['unknown-recovers-from-next-shared-signal',async f=>{const count=f.inventories,installed=f.installed;f.state=state({known:true,mode:'unknown',reason:'shared-mode-pending'});f.emit();await flush();await advance(250);check(f.inventories===count,'Unknown cannot authorize a replacement account inventory');f.state=state();f.emit();await flush();await advance(fallbackGap-250);await advance(250);check(f.inventories===count+1&&f.installed===installed,'same identity recovers on public source event without native rescan');}],
  ['document-reentry-refreshes-local-only',async f=>{const count=f.inventories,installed=f.installed;f.state=state({modeEpoch:'document-b:1'});f.emit();await flush();await advance(250);check(f.inventories===count+1&&f.installed===installed,'new verified UI document refreshes local inventory only');origins(f,128);}],
  ['coalesced-ready-retires-inventory-and-snapshot',async f=>{
   const ready={phase:'ready',generation:1,suspendSerial:1,known:true,hardwareWritesAllowed:true,notificationPending:false};
   f.holdInventory=f.holdSnapshot=true;f.state=state({mode:'desktop'});f.emit();f.snapshotNotice();await flush();await advance(250);
   const oldInventory=f.inventoryFlights.at(-1),oldSnapshot=f.snapshots.at(-1),beforeReads=f.reads.length,beforeSubmissions=f.submissions.length;
   const changes=[];const record=e=>changes.push(e.detail);window.addEventListener('game-media:changed',record);
   try{
    f.lifecycle(ready);await flush();await advance(250);
    check(f.reads.length===beforeReads+1,'Ready with a new suspend serial revalidates Steam once');
    check(f.observeSuspendSerial&&oldInventory.signal?.aborted&&oldSnapshot.signal?.aborted,'public scope retires both old requests');
    const currentInventory=f.inventoryFlights.at(-1),currentSnapshot=f.snapshots.at(-1);
    check(currentInventory!==oldInventory&&currentSnapshot!==oldSnapshot,'fresh inventory and snapshot start while old reads are still held');
    oldInventory.resolve([{appId:999,name:'stale',verified:true}]);oldSnapshot.resolve({entries:{stale:{key:'old',bindingRevision:1}},aliases:{}});
    await flush();check(f.submissions.length===beforeSubmissions&&changes.length===0,'late old results cannot submit or publish');
    currentInventory.resolve([{appId:570,name:'current',verified:true}]);currentSnapshot.resolve({entries:{fresh:{key:'fresh',bindingRevision:1}},aliases:{}});
    await flush();check(f.submissions.length===beforeSubmissions+1&&f.submissions.at(-1).targets[0].appId===570&&changes.length===1&&changes[0].key==='fresh','fresh results complete exactly once');
    const stable=[f.reads.length,f.inventories,f.snapshots.length];f.lifecycle(ready);f.lifecycle({...ready,suspendSerial:0});await advance(60000);
    check(stable.join() === [f.reads.length,f.inventories,f.snapshots.length].join(),'duplicate or old serial cannot replay work');
   }finally{window.removeEventListener('game-media:changed',record);}
  }],
  ['notification-advance-retires-held-media-snapshot',async f=>{
   const ready={phase:'ready',generation:1,suspendSerial:0,observedNotificationSequence:22,known:true,hardwareWritesAllowed:true,notificationPending:false};
   f.holdSnapshot=true;f.snapshotNotice();await flush();
   const held=f.snapshots.at(-1),reads=f.snapshots.length,steam=f.reads.length,watches=f.watchCalls.length;
   const changes=[];const record=e=>changes.push(e.detail.key);window.addEventListener('game-media:changed',record);
   try{
    f.publishNotice(ready);await flush();
    held.resolve({entries:{old:{key:'retired',bindingRevision:1}},aliases:{}});await flush();
    check(changes.length===0,'a pre-notification snapshot may not publish a stale asset change');
    check(held.signal?.aborted&&f.snapshots.length===reads+1,'the current scope owns one replacement without waiting for the old read');
    const replacement=f.snapshots.at(-1);
    f.publishNotice(ready);f.publishNotice({...ready,observedNotificationSequence:21});await flush();
    check(!replacement.signal.aborted&&f.snapshots.length===reads+1,'duplicate and older notices cannot replace the new read');
    replacement.resolve({entries:{current:{key:'current',bindingRevision:2}},aliases:{}});await flush();
    check(changes.join() === 'current','only the new scope may publish its asset change');
    f.publishNotice({...ready,observedNotificationSequence:23});await advance(60000);
    check(f.snapshots.length===reads+1&&f.reads.length===steam&&f.watchCalls.length===watches,'a healthy notice schedules no snapshot, Steam scan or watcher registration');
   }finally{window.removeEventListener('game-media:changed',record);}
  }],
  ['notification-advance-retires-held-preparation-read',async f=>{
   const ready={phase:'ready',generation:1,suspendSerial:0,observedNotificationSequence:22,known:true,hardwareWritesAllowed:true,notificationPending:false};
   const reads=f.preparationReads,reasons=f.preparationReasons;
   check(reads.length===1&&reasons.at(-1)==='capacity-unavailable','original wake started one unresolved capacity read');
   const old=reads[0],before=reasons.length;
   f.publishNotice(ready);await advance(250);
   old.resolve({});await flush();
   check(reasons.length===before,'a pre-notification capacity reply must not release the current policy');
   check(old.signal?.aborted&&reads.length===2,'the current scope must replace the read without waiting');
   const current=reads[1];
   f.publishNotice(ready);f.publishNotice({...ready,observedNotificationSequence:21});await advance(250);
   check(!current.signal.aborted&&reads.length===2,'duplicate or older notices cannot retire the replacement');
   current.reject(Error('current wake read failed'));await flush();await advance(250);
   check(reads.length===3,'retirement must preserve the original wake-read retry');
   reads[2].resolve({blockedReason:'media-disk-space-low'});await flush();
   check(reasons.at(-1)==='media-disk-space-low','only current capacity may change admission');
   f.publishNotice({...ready,observedNotificationSequence:23});await advance(60000);
   check(reads.length===3&&timers.size===0,'a healthy notification must not start a capacity read or polling');
   for(const fn of f.events.get('gameMedia.preparationPolicyChanged')||[])fn(new CustomEvent('policy'));
   await advance(250);check(reads.length===4,'the original event still starts a capacity read');
   f.stop();check(reads[3].signal.aborted&&timers.size===0,'stop returns the owned capacity wait and timers');
   const stopped=reasons.length;reads[3].resolve({});await flush();
   check(reasons.length===stopped&&timers.size===0,'late completion after stop cannot apply policy');
  },undefined,f=>{f.preparationReads=[];f.preparationReasons=[];}],
  ['notification-advance-retires-held-steam-account-read',async f=>{
   const ready={phase:'ready',generation:1,suspendSerial:0,observedNotificationSequence:22,known:true,hardwareWritesAllowed:true,notificationPending:false};
   const start=f.reads.length,scans=f.inventories,snapshots=f.snapshots.length;
   f.hold=true;f.emit();const held=f.reads.at(-1);
   try{
    f.publishNotice(ready);await flush();
    held.resolve(state({observedAccount:99}));await flush();await advance(250);
    check(f.inventories===scans,'the retired Steam account may not schedule an inventory scan');
    check(held.options.signal?.aborted&&f.reads.length===start+1,'retirement releases the old wait and does not bypass the read gap');
    await advance(fallbackGap-250);
    check(f.reads.length===start+2,'the current public scope replaces only the held Steam read');
    const current=f.reads.at(-1);
    f.publishNotice(ready);f.publishNotice({...ready,observedNotificationSequence:21});await flush();
    check(current.live&&f.reads.length===start+2,'duplicate and late public notices leave the replacement read alone');
    current.resolve(state({observedAccount:18}));await flush();await advance(250);
    check(f.inventories===scans+1,'the current confirmed account may scan once');origins(f,64);
    f.publishNotice({...ready,observedNotificationSequence:23});await advance(60000);
    check(f.reads.length===start+2&&f.inventories===scans+1&&f.snapshots.length===snapshots,'a newer healthy notice does not replay account, inventory, or snapshot work');
   }finally{held.resolve(state());for(const r of f.reads.slice(start+1))r.resolve?.(state());}
  }],
  ['coalesced-ready-replaces-held-steam-read',async f=>{
   const count=f.inventories,start=f.reads.length;
   const ready={phase:'ready',generation:1,suspendSerial:1,known:true,hardwareWritesAllowed:true,notificationPending:false};
   f.hold=true;f.emit();const old=f.reads.at(-1);
   try{
    check(f.reads.length===start+1,'first Steam read is held');
    f.lifecycle(ready);await flush();
    check(f.reads.length===start+2,'current Ready must obtain a new Steam read before the old reply or timeout');
    const current=f.reads.at(-1);check(current!==old,'current Steam read must own a distinct request');
    check(old.options.signal?.aborted&&old.live===false,'last media reader must release the original transport wait');
    old.resolve(state({observedAccount:99}));await flush();await advance(250);
    check(f.inventories===count,'old Steam account cannot schedule a scan');
    current.resolve(state());await flush();await advance(250);
    check(f.inventories===count+1,'current Steam context alone restores inventory');origins(f,16|512);
    f.lifecycle(ready);await advance(60000);
    check(f.reads.length===start+2&&f.inventories===count+1&&timers.size===0,'duplicate Ready leaves no extra read, scan, or timer');
   }finally{old.resolve(state());for(const r of f.reads.slice(start+1))r.resolve?.(state());}
  }],
  ['retired-media-reader-does-not-abort-peer-steam-read',async f=>{
   const count=f.inventories,start=f.reads.length,peer=[];
   const off=f.steamUi.subscribe(()=>{void f.steamUi.read({automatic:true}).then(value=>peer.push(value.observedAccount),()=>peer.push('aborted'));});
   f.hold=true;f.emit();const old=f.reads.at(-1);
   try{
    check(f.reads.length===start+1,'two observers of one Steam notice share one read');
    const paused={phase:'suspending',generation:2,suspendSerial:1,known:true,hardwareWritesAllowed:false,notificationPending:true};
    f.lifecycle(paused);await flush();
    check(old.live&&!old.options.signal.aborted&&f.reads.length===start+1,'media retirement preserves the shared transport while the peer still waits');
    f.lifecycle({...paused,phase:'ready',hardwareWritesAllowed:true,notificationPending:false});await flush();
    check(f.reads.length===start+2,'current media read cannot wait for another observer to finish');
    f.reads.at(-1).resolve(state());await flush();await advance(250);
    check(f.inventories===count+1&&peer.length===0,'current scan settles independently of the shared old request');
    old.resolve(state({observedAccount:99}));await flush();await advance(5000);
    check(peer.length===1&&peer[0]===99&&!old.live&&f.inventories===count+1,'only the original peer receives its old reply');
   }finally{off();old.resolve(state());for(const r of f.reads.slice(start+1))r.resolve?.(state());}
  }],
  ['sleep-retires-pending-shared-response',async f=>{const count=f.inventories;f.hold=true;f.emit();const old=f.reads.at(-1);f.lifecycle({phase:'suspending',generation:2,known:true});old.resolve(state({observedAccount:99}));await flush();f.emit();check(f.reads.at(-1)===old,'suspend performs no new shared reads');await advance(1000);check(f.inventories===count,'suspended pending response cannot scan');f.hold=false;f.lifecycle({phase:'ready',generation:2,known:true,hardwareWritesAllowed:true,notificationPending:false});await flush();await advance(250);check(f.inventories===count+1,'committed Ready revalidates same Steam identity');const stable=f.inventories;f.hold=true;f.emit();await advance(fallbackGap-250);const waiting=f.reads.at(-1);f.lifecycle({phase:'ready',generation:2,known:true,hardwareWritesAllowed:true,notificationPending:true});waiting.resolve(state({observedAccount:99}));await flush();await advance(250);check(f.inventories===stable,'same-generation pending authority rejects old source reply');f.hold=false;f.lifecycle({phase:'ready',generation:2,known:true,hardwareWritesAllowed:true,notificationPending:false});await flush();await advance(250);check(f.inventories===stable+1,'same-generation admitted correction restores shared source read');}],
  ['off-and-dc-reject-pending-shared-response',async f=>{const count=f.inventories;f.hold=true;f.emit();const old=f.reads.at(-1);f.enabled=false;window.dispatchEvent(new Event('ui-settings:changed'));old.resolve(state({observedAccount:99}));await flush();const readCount=f.reads.length;for(let n=0;n<20;n++)f.emit();await advance(1000);check(f.reads.length===readCount&&f.inventories===count,'OFF performs no source reads or scans');f.enabled=true;f.blocked=true;f.powerEpoch++;f.powerChanged(true);window.dispatchEvent(new Event('ui-settings:changed'));f.emit();await flush();check(f.reads.length===readCount&&f.inventories===count,'physical DC pause performs no source reads or scans');f.hold=false;f.blocked=false;f.powerEpoch++;f.powerChanged(false);await flush();await advance(250);check(f.reads.length===readCount+1&&f.inventories===count+1,'power recovery reconciles through shared source');}],
  ['media-watch-recovers-only-after-current-ready',async f=>{
   check(f.watchCalls.join(',')==='true','no replacement registration while suspended');
   const ready={phase:'ready',generation:1,suspendSerial:1,known:true,hardwareWritesAllowed:true,notificationPending:false};
   f.lifecycle(ready);await advance(1000);
   check(f.watchCalls.join(',')==='true,true','current Ready replaces exactly one lost registration');
   f.lifecycle({...ready,suspendSerial:0});f.lifecycle(ready);f.lifecycle({...ready,suspendSerial:2});await advance(60000);
   check(f.watchCalls.join(',')==='true,true','an acknowledged passive watcher survives duplicate, old and new Ready');
   f.stop();f.stop();await flush();check(f.watchCalls.join(',')==='true,true,false','stop sends exactly one release');
  },f=>f.lifecycle({phase:'suspending',generation:1,suspendSerial:1,known:true,hardwareWritesAllowed:false,notificationPending:true}),f=>{f.watchFailures=1;}],
  ['media-watch-retry-is-bounded-without-a-second-fact',async f=>{
   check(f.watchCalls.join(',')==='true,true','two failures exhaust the current initial registration and its retry');
   const ready={phase:'ready',generation:1,suspendSerial:0,known:true,hardwareWritesAllowed:true,notificationPending:false};
   f.lifecycle(ready);await advance(60000);check(f.watchCalls.length===2,'no retry loop or same-scope budget refund');
   f.lifecycle({...ready,suspendSerial:1});await advance(1000);
   check(f.watchCalls.join(',')==='true,true,true','a newer coalesced Ready can repair the observation');
   await advance(60000);check(timers.size===0,'healthy idle retains no timer');
  },undefined,f=>{f.watchFailures=2;}],
  ['media-watch-stop-retires-inflight-ack',async f=>{
   check(f.watchCalls.join(',')==='true'&&f.watchRequestOptions?.timeoutMs===3000,'only one bounded registration while the acknowledgement is held');
   const signal=f.watchRequestOptions.signal;f.stop();f.watchReply(true);await advance(60000);
   check(signal.aborted&&f.watchCalls.join(',')==='true,false','released waiter and ignored the late acknowledgement');
   check(timers.size===0,'stop leaves no watcher retry timer');
  },undefined,f=>{f.holdWatch=true;}],
  ['stop-unsubscribes-and-stays-idle',async f=>{const count=f.inventories;f.hold=true;f.emit();const pending=f.reads.at(-1);f.stop();pending.resolve(state({observedAccount:99}));await flush();await advance(60000);check(f.events.get('steam.uiStateChanged')?.size===0,'stop releases common source subscription');check(f.inventories===count&&timers.size===0,'stopped consumer leaves no scan or timer');}]
 ];
 try{
  if(selected!==undefined&&!definitions.some(([id])=>id===selected))throw Error('Unknown shared Steam inventory case');
  for(const[name,body,beforeBoot,setup]of definitions){if(selected!==undefined&&selected!==name)continue;let f;const start=checks;
   try{clock=100000;f=await fixture(beforeBoot,setup);await body(f);rows.push({name,ok:true,checks:checks-start,sharedReads:f.reads.length,inventories:f.inventories,installedScans:f.installed,inventoryArgs:f.inventoryArgs,watchCalls:f.watchCalls.slice()});}
   catch(error){rows.push({name,ok:false,checks:checks-start,error:String(error?.message||error)});}
   finally{f?.stop();await flush();timers.clear();}
  }
 }finally{globalThis.window=saved.window;globalThis.setTimeout=saved.setTimeout;globalThis.clearTimeout=saved.clearTimeout;globalThis.performance=saved.performance;Date.now=saved.now;delete globalThis[symbol];}
 const result={ok:rows.length>0&&rows.every(row=>row.ok),selection:selected??'shared-steam-inventory',cases:rows.length,checks,results:rows,scope:'actual sharedGameMedia coordinator and public steamUi facade; deterministic native facts, power/lifecycle/event/timer ports; no real Steam, downloads or CPU sampling'};
 fs.writeFileSync(path.join(out,'shared-steam-inventory-results.json'),JSON.stringify(result,null,2),{flag:'wx'});console.log(JSON.stringify(result));if(!result.ok)process.exitCode=1;
}
