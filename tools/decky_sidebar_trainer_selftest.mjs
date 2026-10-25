// Production Decky adapter/host and original ready-only trainer owner with memory IO.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {root,load,detectedGameName,seed} from './fixtures/decky_game_memory_store.mjs';
import {task} from './fixtures/decky_task_paths.mjs';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const validation=path.join(task,'validation');fs.mkdirSync(validation,{recursive:true});
const read=p=>fs.readFileSync(path.join(root,p),'utf8');
const key=load(read('src/bridge/gamePolicyHysteresis.ts'));
const gameModule=load(read('src/bridge/deckyGameActions.ts'),{'./gamePolicyHysteresis':key});
const {gameMirrorIdentity}=gameModule;
const {createTrainerMirrorActions,trainerMirrorSource}=load(read('src/bridge/deckyTrainerActions.ts'),{'./deckyGameActions':gameModule});
const cases=[];const check=async(name,fn)=>{await fn();cases.push({name,status:'passed'});};
const flush=async()=>{for(let i=0;i<120;i++)await Promise.resolve();await new Promise(resolve=>setImmediate(resolve));};
const defer=()=>{let resolve,reject;const promise=new Promise((r,j)=>{resolve=r;reject=j;});return {promise,resolve,reject};};
const installed={gameName:'Fixture Game',origin:'verified-fixture',folder:'C:\\Fixture\\Trainers\\Game',path:'C:\\Fixture\\Trainers\\Game\\Trainer.exe',sha256:'a'.repeat(64),modified:1};
const game={pid:101,processCreated:'birth-A',path:'C:/Games/Game.exe',name:'Game.exe',title:'Fixture Game',ts:0};
function adapterFixture(){
 const state={game:structuredClone(game),ready:structuredClone(installed),allowed:true,busy:false,expired:false,power:{generation:1,phase:'ready',hardwareWritesAllowed:true},powerHook:null,launchHook:null,launches:0,invalidations:0,lookups:0};
 const adapter=createTrainerMirrorActions({allowed:()=>state.allowed,game:()=>state.game,label:detectedGameName,
  ready:()=>{state.lookups++;return state.ready?{...state.ready,modified:state.lookups}:null;},
  power:async()=>state.powerHook?await state.powerHook():structuredClone(state.power),busy:()=>state.busy,invalidate:()=>state.invalidations++,
  launch:async(target,isCurrent)=>{state.launches++;assert.deepEqual(target,game);if(!await isCurrent())throw Error('Inert owner refuses changed target');return state.launchHook?state.launchHook(isCurrent):{action:'opened'};}});
 const context={generation:1,revision:1,gameIdentity:gameMirrorIdentity(game),controlAdmission:{trainer:trainerMirrorSource(installed)},checkpoint:()=>{if(state.expired)throw Error('Expired peer');}};
 return {state,adapter,context,view:()=>adapter.view(state.game,state.ready),run:(args={field:'trainer',value:'open'})=>adapter.execute(args,context)};
}
await check('stable admission ignores the owner lookup timestamp but includes filename and hash',()=>{
 assert.equal(trainerMirrorSource(installed),trainerMirrorSource({...installed,modified:Date.now()}));
 for(const field of ['gameName','origin','path','folder','sha256'])assert.notEqual(trainerMirrorSource(installed),trainerMirrorSource({...installed,[field]:installed[field]+'changed'}));
});
await check('existing launch returns pending without claiming success, then confirms only the original receipt',async()=>{
 const f=adapterFixture(),done=defer();f.state.launchHook=()=>done.promise;
 const result=await f.run();await flush();assert.deepEqual(structuredClone(result),{saved:false,applied:false,pending:true,notice:'修改器正在启动，等待启动确认'});assert.equal(f.state.launches,1);assert.equal(f.view().pending,true);assert.equal(f.view().notice,'');
 done.resolve({action:'opened'});await flush();assert.equal(f.view().pending,false);assert.equal(f.view().notice,'已启动当前游戏修改器');assert.equal(f.state.busy,false);f.adapter.stop();
});
await check('duplicate clicks are refused while the original launcher is waiting',async()=>{
 const f=adapterFixture(),done=defer();f.state.launchHook=()=>done.promise;await f.run();await flush();await assert.rejects(f.run(),/BUSY/);assert.equal(f.state.launches,1);done.resolve({});await flush();f.adapter.stop();
});
await check('receipt rejection restores only the trainer button and does not retry',async()=>{
 const f=adapterFixture(),done=defer();f.state.launchHook=()=>done.promise;await f.run();await flush();done.reject(Error('Inert original hash or receipt failure'));await flush();assert.equal(f.view().pending,false);assert.match(f.view().notice,/未确认/);assert.equal(f.state.launches,1);assert(!f.view().notice.includes(installed.path));f.adapter.stop();
});
for(const [name,patch,error] of [
 ['feature off',{allowed:false},/TARGET_CHANGED/],['no current game',{game:null},/TARGET_CHANGED/],
 ['missing birth identity',{game:{...game,processCreated:''}},/TARGET_CHANGED/],['invalid PID',{game:{...game,pid:0}},/TARGET_CHANGED/],
 ['changed PID',{game:{...game,pid:202}},/TARGET_CHANGED/],['changed executable path',{game:{...game,path:'C:/Other/Game.exe'}},/TARGET_CHANGED/],
 ['global quick action already busy',{busy:true},/BUSY/],['no verified ready entry',{ready:null},/SOURCE_CHANGED/],
 ['changed cached file',{ready:{...installed,path:installed.path+'.other'}},/SOURCE_CHANGED/],['changed cached hash',{ready:{...installed,sha256:'b'.repeat(64)}},/SOURCE_CHANGED/],
 ['expired mirror peer',{expired:true},/Expired/],['power generation changed',{power:{generation:2,phase:'ready',hardwareWritesAllowed:true}},/CONTEXT_CHANGED/],
 ['power gate closed',{power:{generation:1,phase:'ready',hardwareWritesAllowed:false}},/CONTEXT_CHANGED/],
 ['power resuming',{power:{generation:1,phase:'resuming',hardwareWritesAllowed:true}},/CONTEXT_CHANGED/],
])await check(name+' never starts preparation or a launcher',async()=>{const f=adapterFixture();Object.assign(f.state,patch);await assert.rejects(f.run(),error);await flush();assert.equal(f.state.launches,0);assert.equal(f.view().pending,false);f.adapter.stop();});
await check('missing private ready admission cannot be granted by client args',async()=>{for(const source of [undefined,'null']){const f=adapterFixture();f.context.controlAdmission.trainer=source;await assert.rejects(f.run(),/NOT_FOUND/);assert.equal(f.state.launches,0);f.adapter.stop();}});
await check('strict open intent accepts no path, title, URL, download or alternate action',async()=>{
 for(const args of [{field:'trainer',value:'download'},{field:'trainer',value:'open',path:installed.path},{field:'trainer',value:'open',url:'https://invalid.test'},{field:'wrong',value:'open'}]){const f=adapterFixture();await assert.rejects(f.run(args),/INVALID_REQUEST/);assert.equal(f.state.launches,0);f.adapter.stop();}
});
await check('a game switch while awaiting power prevents the original launcher',async()=>{const f=adapterFixture(),done=defer();f.state.powerHook=()=>done.promise;const pending=f.run();f.state.game={...game,pid:202,processCreated:'birth-B'};done.resolve(f.state.power);await assert.rejects(pending,/CONTEXT_CHANGED/);assert.equal(f.state.launches,0);f.adapter.stop();});
await check('owner callback rejects game, source, power and peer changes before file execution',async()=>{
 for(const mutate of [s=>s.game={...game,processCreated:'reused-PID'},s=>s.ready=null,s=>s.power.generation++,s=>s.expired=true]){const f=adapterFixture(),done=defer();let current;f.state.launchHook=check=>{current=check;return done.promise;};await f.run();await flush();mutate(f.state);assert.equal(await current(),false);done.reject(Error('Inert target revoked'));await flush();f.adapter.stop();}
});
await check('an old game launch outcome never appears on a new game or updated trainer source',async()=>{
 const f=adapterFixture();await f.run();await flush();assert.match(f.view().notice,/已启动/);f.state.game={...game,pid:202};assert.equal(f.view().notice,'');f.state.game=structuredClone(game);f.state.ready={...installed,sha256:'b'.repeat(64)};assert.equal(f.view().notice,'');f.adapter.stop();
});
await check('stop cancels target permission and consumes late receipts without invalidating closed host',async()=>{
 const f=adapterFixture(),done=defer();let current;f.state.launchHook=check=>{current=check;return done.promise;};await f.run();await flush();const before=f.state.invalidations;f.adapter.stop();assert.equal(await current(),false);done.resolve({});await flush();assert.equal(f.state.invalidations,before);assert.equal(f.view().notice,'');
});

// Original gameTrainer module: seed only its in-memory ready cache; every IO is inert.
function ownerFixture(){
 const state={hash:'a'.repeat(64),gameExists:true,trainerExists:true,scriptExists:true,current:true,shellCalls:[],writes:[],reads:[],targetChecks:0,externalLaunches:0};
 const api={app:{powerControlDir:async()=> 'C:\\Fixture\\PowerControl',dataDir:async()=> 'C:\\Fixture\\Data',exeDir:async()=> 'C:\\Fixture\\YMCC'},fs:{
  exists:async p=>p===game.path?state.gameExists:p===installed.path?state.trainerExists:p.endsWith('YeManTrainerLaunch.ps1')?state.scriptExists:true,
  sha256File:async p=>{assert.equal(p,installed.path);return state.hash;},mkdir:async()=>{},remove:async()=>{},
  writeTextFileAtomic:async(p,text)=>state.writes.push({p,text}),readTextFile:async(p)=>{state.reads.push(p);const sent=state.shellCalls.at(-1);assert(sent);return JSON.stringify({requestId:sent.args[sent.args.indexOf('-RequestId')+1],pending:false,ok:state.trainerExists&&state.hash===installed.sha256,error:'fixture backend rejects changed/missing file',code:'trainer-changed'});}},
  shell:{hidden:async(exe,args)=>{assert.equal(exe,'powershell.exe');assert(args[args.indexOf('-File')+1].endsWith('YeManTrainerLaunch.ps1'));assert.equal(args[args.indexOf('-Mode')+1],'Trainer');assert.equal(args[args.indexOf('-TrainerPath')+1],installed.path);state.shellCalls.push({exe,args});if(state.trainerExists&&state.hash===installed.sha256)state.externalLaunches++;return {ok:true};}}};
 const owner=load(read('src/bridge/gameTrainer.ts')+'\nexport function seedReadyForTest(name,path,state){states.set(trainerTaskKey(name,path),state);}',{'./api':api,'./gamedetect':{cleanGameTitle:name=>name.trim()},'./settingsRepository':{getSettingsGeneration:()=>1},'@/robust/frontendDiagnostics':{reportFrontendError:()=>{}}});
 const ready={ok:true,state:'ready',gameName:game.title,trainerGameName:installed.gameName,trainerPath:installed.path,trainerFolder:installed.folder,sha256:installed.sha256,origin:installed.origin,identity:{method:'steam-manifest',steamAppId:'480',manifestPath:'C:/Fixture/steamapps/appmanifest_480.acf'}};
 owner.seedReadyForTest(game.title,game.path,ready);
 const options={isTargetCurrent:async()=>{state.targetChecks++;return state.current;}};
 return {state,owner,ready,options,run:()=>owner.launchReadyGameTrainer(game.title,game.path,options)};
}
await check('original ready query is exact by title and EXE path, needs a verified SHA and has zero IO',()=>{
 const f=ownerFixture();assert(f.owner.getReadyGameTrainer(game.title,game.path));assert.equal(f.owner.getReadyGameTrainer('Other Game',game.path),null);assert.equal(f.owner.getReadyGameTrainer(game.title,'C:/Other/Game.exe'),null);assert.equal(f.state.reads.length+f.state.writes.length+f.state.shellCalls.length,0);
 f.owner.seedReadyForTest(game.title,game.path,{...f.ready,sha256:undefined});assert.equal(f.owner.getReadyGameTrainer(game.title,game.path),null);
});
await check('the original ready-only owner validates files/hash/target and calls only its established launch script',async()=>{
 const f=ownerFixture(),result=await f.run();assert.equal(result.action,'opened');assert.equal(f.state.shellCalls.length,1);assert(f.state.targetChecks>=2);assert.equal(f.state.writes.length,1);assert.equal(f.state.reads.length,1);
});
await check('original owner deduplicates concurrent ready launches and shares the in-flight menu launch',async()=>{
 const f=ownerFixture(),done=defer();f.options.isTargetCurrent=()=>done.promise;const first=f.run(),second=f.run();assert.equal(first,second);assert.equal(f.owner.openOrSearchGameTrainer(game.title,undefined,game.path),first);done.resolve(true);await first;assert.equal(f.state.shellCalls.length,1);
});
for(const [name,mutate] of [['missing game EXE',s=>s.gameExists=false],['removed trainer file',s=>s.trainerExists=false],['changed file SHA',s=>s.hash='b'.repeat(64)],['missing launcher component',s=>s.scriptExists=false],['changed target',s=>s.current=false]])await check('original ready owner refuses '+name+' without preparation, downloading or launch',async()=>{const f=ownerFixture();mutate(f.state);await assert.rejects(f.run());assert.equal(f.state.externalLaunches,0);assert.equal(f.state.shellCalls.length,name==='removed trainer file'||name==='changed file SHA'?1:0);});
await check('original owner treats downloading, unavailable and ambiguous ready states as absent',async()=>{
 for(const patch of [{state:'downloading'},{state:'unavailable'},{ok:false},{trainerGameName:''},{trainerPath:'C:\\Outside\\Trainer.exe'}]){const f=ownerFixture();f.owner.seedReadyForTest(game.title,game.path,{...f.ready,...patch});assert.equal(f.owner.getReadyGameTrainer(game.title,game.path),null);await assert.rejects(f.run());assert.equal(f.state.shellCalls.length,0);}
});

const runId='a'.repeat(32),peer={connection:'1',runId,connected:true};
async function hostFixture(initial){const f=await createMirrorHostFixture(initial?{initial}:{});f.ready();let sequence=0;
 f.requestSnapshot=async subscribe=>{f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,...(subscribe===undefined?{}:{subscribe})}});await flush();return f.replies.filter(r=>r.message.type==='snapshot').at(-1)?.message.snapshot;};
 f.edit=async(snapshot,field='trainer',value='open')=>{const n=f.replies.length;sequence++;const request={command:'game.setField',runId,id:'trainer:'+sequence,clientId:'trainer',sequence,generation:snapshot.generation,revision:snapshot.revision,identity:snapshot.game.identity,args:{field,value}};f.emit('deckySidebar.mirrorRequest',{...peer,request});await flush();return {request,reply:f.replies.slice(n).find(r=>r.message.type==='reply')?.message};};
 f.seed=()=>{f.state.trainer={targetName:detectedGameName(f.state.game),targetPath:f.state.game.path,installed:structuredClone(installed)};f.trainerChanged();};return f;}
await check('production host shows unavailable without a ready entry and never searches or writes a profile',async()=>{const f=await hostFixture();try{const snap=await f.requestSnapshot();assert.deepEqual(snap.trainer,{available:false,pending:false,notice:''});assert.equal(f.counts.trainerLaunches,0);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);}finally{f.stop();}});
await check('production host follows ready events and removes all private trainer paths and hashes from wire',async()=>{const f=await hostFixture();try{await f.requestSnapshot();f.seed();await flush();const snap=f.replies.at(-1).message.snapshot;assert.equal(snap.trainer.available,true);const wire=JSON.stringify(snap);assert(!wire.includes(installed.sha256));assert(!wire.includes('Trainers'));assert(!wire.includes('controlAdmission'));assert.equal(f.counts.trainerLaunches,0);}finally{f.stop();}});
await check('production pending launch releases relay controls while retaining trainer deduplication',async()=>{const f=await hostFixture(seed()),done=defer();try{f.seed();f.state.trainerLaunchHook=()=>done.promise;const snap=await f.requestSnapshot(),first=await f.edit(snap);assert.equal(first.reply.ok,true);assert.equal(first.reply.result.pending,true);assert.equal(first.reply.result.applied,false);assert.equal(f.counts.trainerLaunches,1);const pending=await f.requestSnapshot();assert.equal(pending.trainer.pending,true);
 const other=await f.edit(pending,'padPersona','elite');assert.equal(other.reply.ok,true);assert.equal(f.counts.saves,1);assert.equal(f.counts.trainerLaunches,1);
 // Retransmitting exactly the original intent returns the saved admission receipt.
 f.emit('deckySidebar.mirrorRequest',{...peer,request:first.request});await flush();assert.equal(f.counts.trainerLaunches,1);
 done.resolve({action:'opened'});await flush();const latest=await f.requestSnapshot();assert.equal(latest.trainer.pending,false);assert.equal(latest.trainer.notice,'已启动当前游戏修改器');assert.equal(f.counts.saves,1);
 }finally{done.resolve({});await flush();f.stop();}});
await check('production launch intent cannot fall through to a profile write',async()=>{const f=await hostFixture();try{f.seed();const snap=await f.requestSnapshot(),result=await f.edit(snap);assert.equal(result.reply.ok,true);assert.equal(f.counts.trainerLaunches,1);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.monitorSaves,0);assert.equal(f.counts.fanSaves,0);}finally{f.stop();}});
await check('production host loses trainer availability on current-game path change or no game',async()=>{const f=await hostFixture();try{f.seed();assert.equal((await f.requestSnapshot()).trainer.available,true);f.state.game={...f.state.game,path:'C:/Other/Game.exe'};f.gameChanged();await flush();assert.equal((await f.requestSnapshot()).trainer.available,false);f.state.game=null;f.gameChanged();await flush();assert.equal((await f.requestSnapshot()).trainer.available,false);}finally{f.stop();}});
await check('a stale-game revision/identity never reaches the original ready launcher',async()=>{const f=await hostFixture();try{f.seed();const snap=await f.requestSnapshot();f.state.game={...f.state.game,pid:202,processCreated:'birth-B'};f.gameChanged();await flush();const result=await f.edit(snap);assert.equal(result.reply.ok,false);assert.equal(f.counts.trainerLaunches,0);}finally{f.stop();}});
await check('hiding cancels only the trainer observer; reshow consumes latest ready state without polling',async()=>{const f=await hostFixture();try{await f.requestSnapshot();await f.requestSnapshot(false);const before={...f.counts};for(let i=0;i<1000;i++)f.trainerChanged();await flush();assert.equal(f.counts.trainerReads,before.trainerReads);f.seed();const latest=await f.requestSnapshot();assert.equal(latest.trainer.available,true);assert.equal(f.counts.trainerLaunches,0);}finally{f.stop();}assert.deepEqual(f.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});});
await check('original owner failures complete the pending state without a second launch or a profile write',async()=>{const f=await hostFixture(),done=defer();try{f.seed();f.state.trainerLaunchHook=()=>done.promise;await f.edit(await f.requestSnapshot());done.reject(Error('Inert file removed'));await flush();const snap=await f.requestSnapshot();assert.equal(snap.trainer.pending,false);assert.match(snap.trainer.notice,/未确认/);assert.equal(f.counts.trainerLaunches,1);assert.equal(f.counts.saves,0);}finally{f.stop();}});
await check('1000 download-progress events with unchanged readiness cause no extra native or full snapshot reads',async()=>{
 const f=await hostFixture();try{await f.requestSnapshot();const before={...f.counts};for(let i=0;i<1000;i++)f.trainerChanged();await flush();assert.equal(f.counts.power,before.power);assert.equal(f.counts.schedule,before.schedule);assert.equal(f.counts.monitorSaves,0);assert.equal(f.counts.trainerLaunches,0);f.seed();await flush();assert.equal(f.replies.at(-1).message.snapshot.trainer.available,true);}finally{f.stop();}
});
await check('a cached ready trainer with missing PID creation identity remains unavailable',()=>{const f=adapterFixture();f.state.game={...game,processCreated:''};assert.equal(f.view().available,false);f.adapter.stop();});
const report={status:'passed',checks:cases.length,cases,realSteam:false,realProcessLaunches:0,realDownloads:0,realSettingsWrites:0,scope:'Production trainer adapter, mirror host/relay and original ready-only trainer owner with inert memory IO. No deployment or actual Steam/device/game interaction.'};
fs.writeFileSync(path.join(validation,'DECKY-TRAINER-ENTRY.json'),JSON.stringify(report,null,2)+'\n');console.log(`Decky trainer entry PASS ${cases.length}`);
