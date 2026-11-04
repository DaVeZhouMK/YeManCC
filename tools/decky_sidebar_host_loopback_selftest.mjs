import { task } from './fixtures/decky_task_paths.mjs';
// Windows native broker -> exact production host/relay/actions -> original
// memory-backed store. Steam/WebView/device IPC remain explicitly out of scope.
import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {spawn} from 'node:child_process';
import {root,load,main,seed} from './fixtures/decky_game_memory_store.mjs';import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';import {SocketAdapter,awaitState,deferred,bounded} from './fixtures/decky_native_transport.mjs';
const steam=load(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8'));
const {MirrorClient}=load(fs.readFileSync(path.join(root,'decky-plugin/src/mirrorClient.ts'),'utf8'),{'./steamRunningContext':steam});
// Real audio is explicit opt-in; the ordinary fixture remains memory-only.
const fxArg=process.argv.indexOf('--fxsound-native');
const fxExecutable=fxArg>=0?path.resolve(process.argv[fxArg+1]??''):null;
if(fxExecutable&&!fs.statSync(fxExecutable).isFile())throw Error('FxSound native test consumer missing');
const fxPublic=state=>Object.fromEntries(['known','installed','running','enabled','selected','preset','output','reason'].map(key=>[key,state[key]]));
async function fxCall(command){
 if(!fxExecutable)throw Error('Real FxSound consumer not opted in');
 const proc=spawn(fxExecutable,[command],{windowsHide:true,stdio:['ignore','pipe','pipe']});let output='',errors='';
 proc.stdout.on('data',chunk=>output+=String(chunk));proc.stderr.on('data',chunk=>errors+=String(chunk));
 const exit=await new Promise((resolve,reject)=>{const deadline=setTimeout(()=>{proc.kill();reject(Error('Native FxSound test consumer deadline; application is never force-killed'));},35000);proc.once('error',error=>{clearTimeout(deadline);reject(error);});proc.once('exit',code=>{clearTimeout(deadline);resolve(code);});});
 if(exit!==0)throw Error('Native FxSound test failed: '+output+' '+errors);
 return JSON.parse(output);
}
const restartStorm=process.argv.includes('--restart-storm');if(restartStorm&&fxExecutable)throw Error('Restart storm is memory-only; live audio cannot be combined');
const restartRows=[];let rotationWait=null,heldReply=null,holdReply=false,holdWait=null,deliveryWait=null;
let holdNextAttach=false,attachCaptured=null,releaseCapturedAttach=null;
const liveAudio={};
const file=path.resolve(task,'Build/decky-sidebar-broker-test.exe');
const child=spawn(file,['--serve-relay-fixture'],{windowsHide:true,stdio:['pipe','pipe','pipe']});let stderr='',buffer='',host,client,attachWait=null,requested=0;const sockets=[],cases=[];
child.stderr.on('data',data=>stderr+=String(data));const terminal=new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',(code,signal)=>resolve({code,signal}));});const boot=deferred(),summary=deferred();
child.stdout.on('data',data=>{buffer+=data;for(;;){const end=buffer.indexOf('\n');if(end<0)break;const line=buffer.slice(0,end).trim();buffer=buffer.slice(end+1);if(!line)continue;try{const event=JSON.parse(line);if(event.endpoint){if(rotationWait)rotationWait.resolve(event);else boot.resolve(event);}else if(event.type==='peer')host?.emit('deckySidebar.mirrorPeer',event);else if(event.type==='request'){requested++;host?.emit('deckySidebar.mirrorRequest',event);}else if(event.type==='attach')attachWait?.resolve(event);else if(event.type==='deliveryProbe')deliveryWait?.resolve(event);else if(event.endpointReleased)summary.resolve(event);}catch(error){summary.reject(error);}}});
const flush=async()=>{for(let i=0;i<80;i++)await Promise.resolve();};
async function waitObserverGone(){const until=Date.now()+1500;while(Date.now()<until){if(host.observerStats().watchers===0)return;await new Promise(resolve=>setTimeout(resolve,15));}throw new Error('Host observer detach deadline');}
try{
 let bootstrap=await bounded(boot.promise,'native bootstrap');
 if(fxExecutable){liveAudio.before=await fxCall('status');assert.equal(liveAudio.before.running,false,'Live audio case starts with FxSound stopped');}
 host=await createMirrorHostFixture({initial:seed(),fxSoundNative:fxExecutable?{read:async()=>fxPublic(await fxCall('status')),set:async enabled=>{const result=await fxCall(enabled?'on':'off');return {...result,state:fxPublic(result.state)};}}:null,invoke:async(command,args)=>{if(command==='deckySidebar.mirrorReply'){if(holdReply&&args.message?.type==='reply'){holdReply=false;heldReply=structuredClone(args);holdWait?.resolve(args);return true;}child.stdin.write(JSON.stringify(args)+'\n');return true;}if(command==='deckySidebar.mirrorAttach'){attachWait=deferred();child.stdin.write('attach\n');const task=attachWait;return bounded(task.promise,'native host attach').finally(()=>{if(attachWait===task)attachWait=null;}).then(async peer=>{if(holdNextAttach){holdNextAttach=false;const retired=deferred();releaseCapturedAttach=retired.resolve;attachCaptured.resolve(peer);await bounded(retired.promise,'held native attach released');}return peer;});}throw new Error('Unexpected native operation '+command);}});
 host.ready();await flush();
 client=new MirrorClient({bootstrap:()=>bootstrap,socket:url=>{const socket=new SocketAdapter(url);sockets.push(socket);return socket;},schedule:(fn,delay)=>setTimeout(fn,delay),cancel:clearTimeout,clientId:'actual-host-pipeline',steam:()=>({availability:'observed',appId:'480',runningCount:1})});
 client.setVisible(true);const first=await awaitState(client,state=>state.connected&&state.snapshot?.ready);
 if(fxExecutable){
  assert.equal(first.snapshot.actions.audioOptimization,true);assert.equal(first.snapshot.audioOptimization.enabled,false);
  const originalStore=JSON.stringify(host.memory.raw());
  cases.push('authenticated Windows broker and exact production client/host/owner read real FxSound OFF state');
  const on=await client.mutate('audioOptimization.setEnabled',{enabled:true});assert.equal(on.applied,true);
  await awaitState(client,state=>state.snapshot?.audioOptimization?.enabled===true);
  liveAudio.on=await fxCall('status');assert(liveAudio.on.running&&liveAudio.on.enabled&&liveAudio.on.selected);
  assert.equal(liveAudio.on.preset,'移动端通用音频优化');
  cases.push('authenticated ON request crosses actual broker and production owner into real native FxSound controller');
  const repeated=await client.mutate('audioOptimization.setEnabled',{enabled:true});assert.equal(repeated.applied,true);
  liveAudio.repeatedOn=await fxCall('status');assert.equal(liveAudio.repeatedOn.pid,liveAudio.on.pid);
  cases.push('a second authenticated ON reuses the same real FxSound PID');
  liveAudio.enabledSignal=await fxCall('signal');assert.equal(liveAudio.enabledSignal.ok,true);
  const off=await client.mutate('audioOptimization.setEnabled',{enabled:false});assert.equal(off.applied,true);
  await awaitState(client,state=>state.snapshot?.audioOptimization?.enabled===false);
  liveAudio.off=await fxCall('status');assert.equal(liveAudio.off.running,false);assert.match(liveAudio.off.output,/A100/);
  liveAudio.disabledSignal=await fxCall('signal');assert.equal(liveAudio.disabledSignal.ok,true);
  cases.push('authenticated OFF normally quits real FxSound and original native Recovery restores A100 with output signal');
  const repeatOff=await client.mutate('audioOptimization.setEnabled',{enabled:false});assert.equal(repeatOff.applied,true);
  assert.equal(JSON.stringify(host.memory.raw()),originalStore);assert.equal(host.counts.saves,0);assert.equal(host.counts.inputSaves,0);assert.equal(host.counts.fxSoundWrites,4);
  cases.push('repeated authenticated OFF succeeds while all game/global memory settings remain unchanged');
 }else{
 assert.equal(first.snapshot.actions.game,true);assert.equal(first.snapshot.actions.performance,true);assert.equal(first.snapshot.performance.controls.find(c=>c.key==='tdp').value,20);assert.equal(first.snapshot.steam.appId,'480');assert.equal(host.counts.saves,0);assert.equal(host.counts.capabilities,0);
 for(const [field,value] of [['acMode','performance'],['dcMode','extreme'],['corePolicyMode','only-small'],['hyperThreadPolicy','off']]){
  assert.equal(Object.hasOwn(first.snapshot.game.fields,field),false);await assert.rejects(client.mutate('game.setField',{field,value}));
 }
 assert.equal(host.counts.saves,0);assert.equal(host.counts.performanceUpdates??0,0);cases.push('real native pipeline exposes current continuous performance and rejects all four removed legacy controls without saves');
 for(const tdp of [22,24]){const result=await client.mutate('performance.setControls',{patch:{tdp}});assert.equal(result.saved,true);assert.equal(result.applied,true);await awaitState(client,state=>state.snapshot?.performance?.controls.find(c=>c.key==='tdp')?.value===tdp);}
 const edits=[['padPersona','elite'],['gyroOverride','fps']];let firstPacket;
 for(const [field,value] of edits){const result=await client.mutate('game.setField',{field,value});assert.equal(result.saved,true);assert.equal(result.applied,false);assert.equal(result.pending,true);await awaitState(client,state=>state.snapshot?.game?.fields[field].value===value);if(!firstPacket)firstPacket=sockets[0].sent.find(packet=>packet.command==='game.setField'&&packet.args.field===field);}
 assert.equal(host.counts.performanceUpdates,2);assert.equal(host.counts.saves,2);assert.equal(host.memory.stats().notifications,4);assert.equal(host.memory.raw().entries['game.exe'].enabled,true);assert.equal(host.memory.raw().entries['game.exe'].controls.ac.tdp,24);assert.equal(host.memory.raw().entries['game.exe'].controls.dc.tdp,10);assert.equal(host.counts.joyxoff,0);assert.equal(host.counts.fanSaves,0);cases.push('continuous performance and input intents cross the real native pipeline with each original save applied once');
 let priorSaves=host.counts.saves;const receipt=deferred();const packet=firstPacket;sockets[0].addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.type==='reply'&&message.id===packet.id)receipt.resolve(message);});sockets[0].send(JSON.stringify(packet));assert.equal((await bounded(receipt.promise,'cached game ACK')).ok,true);assert.equal(host.counts.saves,priorSaves);cases.push('original receipt cache prevents duplicate save after lost ACK across the complete host pipeline');
 const changed=host.memory.raw();changed.entries['game.exe'].controls.ac.tdp=26;changed.entries['game.exe'].controls.dc.tdp=13;host.memory.documents.set(main,JSON.stringify(changed));
 await assert.rejects(client.mutate('performance.setControls',{patch:{tdp:23}}),/原方案已变化/);assert.equal(host.counts.saves,priorSaves);await awaitState(client,state=>state.snapshot?.performance?.controls.find(c=>c.key==='tdp')?.value===26);
 assert.equal(host.memory.raw().entries['game.exe'].controls.ac.tdp,26);assert.equal(host.memory.lock.isQuickActionBusy(),false);cases.push('silent original-file performance change is rejected without saving and exact host refreshes before a fresh explicit intent');
 assert.ok(!Object.hasOwn(client.snapshot().snapshot,'gameAdmission'));assert.ok(sockets.every(socket=>socket.sent.every(packet=>!Object.hasOwn(packet,'gameAdmission'))));cases.push('private admission never crosses production native/plugin wire or requires a new public client field');
 const safe=await client.mutate('performance.setControls',{patch:{tdp:23}});assert.equal(safe.saved,true);await awaitState(client,state=>state.snapshot?.performance?.controls.find(c=>c.key==='tdp')?.value===23);assert.equal(host.memory.raw().entries['game.exe'].controls.dc.tdp,13);priorSaves=host.counts.saves;
 host.state.power={...host.state.power,generation:2,phase:'pending',hardwareWritesAllowed:false};host.powerChanged(true);await awaitState(client,state=>state.snapshot?.generation===2&&!state.snapshot.ready);await assert.rejects(client.mutate('performance.setControls',{patch:{tdp:25}}));assert.equal(host.counts.saves,priorSaves);cases.push('committed host pending-power snapshot disables mutations before original settings IO');
 client.setVisible(false);await waitObserverGone();const counts=structuredClone(host.counts);host.state.power={...host.state.power,phase:'ready',hardwareWritesAllowed:true};host.powerChanged();host.scheduleChanged();await flush();assert.equal(host.counts.power,counts.power);assert.equal(host.counts.schedule,counts.schedule);cases.push('hidden QAM disconnects exact host observers; power/config events add zero reads while hidden');
 client.setVisible(true);const reopened=await awaitState(client,state=>state.connected&&state.snapshot?.ready&&state.snapshot.generation===2);assert.equal(reopened.snapshot.performance.controls.find(c=>c.key==='tdp').value,23);assert.equal(host.counts.saves,priorSaves);cases.push('reopen binds existing native transport and exact host with fresh generation and remembered EXE settings');
 host.stop();host.start();host.ready();const remounted=await awaitState(client,state=>state.snapshot?.revision>reopened.snapshot.revision);assert.equal(remounted.snapshot.game.fields.gyroOverride.value,'fps');assert.equal(sockets.length,2);cases.push('host renderer remount uses actual native mirrorAttach revision without opening another socket or losing EXE memory');
 host.state.game={...host.state.game,pid:202,processCreated:'987654321',name:'Offline.exe',title:'Offline',path:'D:/GOG/Offline.exe'};host.gameChanged();const nonSteam=await awaitState(client,state=>state.snapshot?.game?.label==='Offline');assert.match(nonSteam.snapshot.game.identity,/offline\.exe/);assert.equal(nonSteam.snapshot.performance.scope,'global');assert.equal(Object.hasOwn(host.memory.raw().entries,'offline.exe'),false);
 const beforeInputSaves=host.counts.inputSaves;await client.mutate('game.setField',{field:'padPersona',value:'elite'});await awaitState(client,state=>state.snapshot?.game?.fields.padPersona.value==='elite');assert.equal(Object.hasOwn(host.memory.raw().entries,'offline.exe'),false);assert.equal(host.counts.inputSaves,beforeInputSaves+1);assert.equal(host.memory.raw().entries['game.exe'].controls.ac.tdp,23);assert.equal(client.snapshot().snapshot.performance.scope,'global');cases.push('new non-Steam EXE keeps global scope while explicit input intent follows the original global owner without inventing a dedicated profile');
 host.state.game=null;host.gameChanged();await awaitState(client,state=>state.connected&&state.snapshot?.game===null);assert.equal(client.snapshot().snapshot.actions.game,false);assert.equal(client.snapshot().snapshot.actions.fan,true);cases.push('no confirmed game closes game actions without shutting down the global fan mirror');

 }
 // Backend commit and frontend acknowledgement have different lifetimes.
 // Lost ACK across actual Broker renewal cannot replay that saved intent.
 if(restartStorm){
  for(let cycle=0;cycle<10;cycle++){
   let before=await awaitState(client,state=>state.connected&&state.snapshot?.ready&&state.snapshot.actions?.monitor===true);
   const previousBootstrap={...bootstrap},beforeSaves=host.counts.monitorSaves,beforeApplies=host.counts.monitorApplies;
   if(cycle%3===0){attachCaptured=deferred();holdNextAttach=true;host.ready();const observed=await bounded(attachCaptured.promise,'old native attach captured');assert.equal(observed.runId,bootstrap.runId);await client.testConnection();before=client.snapshot();}
   const enabled=!before.snapshot.monitor.enabled;holdWait=deferred();holdReply=true;heldReply=null;
   const pending=client.mutate('monitor.setEnabled',{enabled}).then(value=>({ok:true,value}),error=>({ok:false,error}));
   await bounded(holdWait.promise,'actual committed reply withheld');
   assert.equal(heldReply.message.ok,true);assert.equal(host.counts.monitorSaves,beforeSaves+1);assert.equal(host.counts.monitorApplies,beforeApplies+1);
   rotationWait=deferred();child.stdin.write('rotate\n');bootstrap=await bounded(rotationWait.promise,'fresh actual native broker');rotationWait=null;
   assert.notEqual(bootstrap.runId,previousBootstrap.runId);assert.notEqual(bootstrap.token,previousBootstrap.token);
   client.bootstrapChanged();client.bootstrapChanged();
   const outcome=await bounded(pending,'old unconfirmed intent rejected');assert.equal(outcome.ok,false);
   const fresh=await awaitState(client,state=>state.connected&&state.snapshot?.ready&&state.snapshot.runId===bootstrap.runId);
   assert.equal(fresh.snapshot.monitor.enabled,enabled);
   if(cycle%3===0){releaseCapturedAttach();releaseCapturedAttach=null;await flush();const probe=await client.testConnection();assert.equal(probe.pid,child.pid);assert.equal(client.snapshot().snapshot.runId,bootstrap.runId);}
   deliveryWait=deferred();child.stdin.write(JSON.stringify({...heldReply,fixtureReplyProbe:String(cycle)})+'\n');const delivery=await bounded(deliveryWait.promise,'retired reply processed by actual Broker');deliveryWait=null;assert.equal(delivery.probe,String(cycle));assert.equal(delivery.accepted,false);await flush();
   assert.equal(client.snapshot().snapshot.runId,bootstrap.runId);
   assert.equal(host.counts.monitorSaves,beforeSaves+1);assert.equal(host.counts.monitorApplies,beforeApplies+1);
   restartRows.push({cycle,savedOnce:true,appliedOnce:true,unconfirmedRejected:true,currentStateReadBack:true,oldReplyCannotReplaceNewRun:true,actualNativeLateReplyRejected:true,oldNativeAttachIgnored:cycle%3===0});
  }
  cases.push('ten actual Broker renewals recover committed state without replaying lost-ACK intents or accepting retired replies');
 }
 client.setVisible(false);await waitObserverGone();client.dispose();host.stop();child.stdin.end('stop\n');const native=await bounded(summary.promise,'native stop summary'),exit=await bounded(terminal,'native broker exit');assert.equal(exit.code,0);assert.equal(native.activeAfterStop,false);assert.equal(native.endpointReleased,true);assert.deepEqual(host.observerStats(),{ipc:0,game:0,schedule:0,fan:0,watchers:0});cases.push('all original host observers, client sockets and native broker handles released on shutdown');
 assert.equal(host.memory.stats().pendingDeadlineTimers,0);const report={restartStorm:restartRows,project:'YMCC Decky 侧边栏',stage:process.argv.includes('--stage14')?'Implementation14':process.argv.includes('--stage13')?'Implementation13':process.argv.includes('--stage12')?'Implementation12':process.argv.includes('--stage11')?'Implementation11':'Implementation10',dateHongKong:fxExecutable?'2026-10-24':'2026-10-09',cases:cases.length,results:cases,platform:process.platform,status:'passed',actualNativeBroker:true,productionClient:true,exactProductionHost:true,productionRelayAndAdapters:true,originalStoreAndLock:true,hostCounts:host.counts,memoryStoreStats:host.memory.stats(),nativeSummary:native,requested,actualWebView:false,actualSteam:false,physicalHardwareWrites:fxExecutable?null:0,realBusinessConfigWrites:fxExecutable?'FxSound settings and Windows audio defaults':0,actualJoyxoffOperations:0,realFxSoundController:!!fxExecutable,liveAudio,fxObservationTimersAfterStop:host.fxTimerCount()};assert.equal(host.fxTimerCount(),0);fs.writeFileSync(path.resolve(task,fxExecutable?'validation/FXSOUND-NATIVE-PIPELINE.json':process.argv.includes('--stage14')?'validation/MIRROR-HOST-NATIVE-PIPELINE-IMPLEMENTATION14.json':process.argv.includes('--stage13')?'validation/MIRROR-HOST-NATIVE-PIPELINE-IMPLEMENTATION13.json':process.argv.includes('--stage12')?'validation/MIRROR-HOST-NATIVE-PIPELINE-IMPLEMENTATION12.json':process.argv.includes('--stage11')?'validation/MIRROR-HOST-NATIVE-PIPELINE-IMPLEMENTATION11.json':'validation/MIRROR-HOST-NATIVE-PIPELINE-IMPLEMENTATION10.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}catch(error){releaseCapturedAttach?.();if(fxExecutable)await fxCall('off').catch(()=>{});client?.dispose();host?.stop();for(const socket of sockets)socket.close();if(!child.stdin.destroyed)child.stdin.end('stop\n');await bounded(terminal,'failure broker cleanup').catch(()=>child.kill());throw new Error(`${error.stack}; stderr=${stderr}; counts=${JSON.stringify(host?.counts)}`);}
