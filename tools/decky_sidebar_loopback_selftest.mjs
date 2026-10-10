import { task } from './fixtures/decky_task_paths.mjs';
// Real Windows loopback broker + exact production TS relay + exact production client.
// Owner data/IPC boundary is a fixture; no Steam, installed config or hardware is touched.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import net from 'node:net';
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import {fixture as originalGameMemory,gameDisplayName} from './fixtures/decky_game_memory_store.mjs';
const root=path.resolve(import.meta.dirname,'..');const require=createRequire(path.join(root,'package.json'));const {transformSync}=require('esbuild');
function load(relative){const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,relative),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,queueMicrotask,require:name=>{if(name==='../../decky-plugin/src/steamRunningContext'||name==='./steamRunningContext')return load('decky-plugin/src/steamRunningContext.ts');if(name==='./gamePolicyHysteresis')return load('src/bridge/gamePolicyHysteresis.ts');throw new Error('Unexpected dependency');}});return module.exports;}
const {MirrorClient}=load('decky-plugin/src/mirrorClient.ts');const {ReadOnlyMirrorRelay}=load('src/bridge/deckyMirrorRelay.ts');
const gameMutations=process.argv.includes('--game-mutations');
const mutations=gameMutations||process.argv.includes('--fan-mutations');
const fanFixture=mutations&&!gameMutations?require(path.resolve(task,'Build/decky-sidebar-fan-fixture.cjs')).createOriginalFanFixture():null;
const gameMemory=gameMutations?originalGameMemory({version:1,entries:{},rtss:{'other.exe':{enabled:true,acFps:50,dcFps:30}}}):null;
const {createGameMirrorActions,makeGameMirrorAdmission,gameMirrorIdentity}=load('src/bridge/deckyGameActions.ts');
const game={pid:101,name:'game.exe',title:'Game',path:'C:/Games/Game.exe',processCreated:'birthA',ts:0};
const profile={cpuPreset:'balanced',coreMode:'big',tdpMax:25,fpsTarget:60,cpuTarget:'none',tdpStrategy:'none'},schedule={version:2,configured:true,enabled:true,active:{ac:'balanced',dc:'eco'},profiles:{ac:{},dc:{}}};
for(const side of ['ac','dc'])for(const mode of ['eco','balanced','medium','performance','elite','extreme'])schedule.profiles[side][mode]={...profile,tdpMax:side==='ac'?25:10};
let gameSaves=0;
const gameActions=gameMemory?createGameMirrorActions({featureAllowed:()=>true,game:()=>game,label:gameDisplayName,power:async()=>({generation:1,phase:'ready',hardwareWritesAllowed:true}),capabilities:()=>({heterogeneous:true,smtAvailable:true}),acquire:gameMemory.lock.tryAcquireQuickAction,load:gameMemory.store.loadGameCustomConfig,current:gameMemory.store.getGameCustomConfig,schedule:async()=>structuredClone(schedule),currentSchedule:()=>schedule,save:async config=>{gameSaves++;await gameMemory.store.saveGameCustomConfig(config);},closeJoyxoff:async()=>false}):null;
const file=path.resolve(task,'Build/decky-sidebar-broker-test.exe');
const child=spawn(file,['--serve-relay-fixture'],{windowsHide:true,stdio:['pipe','pipe','pipe']});
let stderr='';child.stderr.on('data',data=>{stderr+=data;});
const exit=new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',(code,signal)=>resolve({code,signal}));});
function deferred(){let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};}
function bounded(promise,label){let timer;return Promise.race([promise,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error(`Timeout: ${label}`)),5000);})]).finally(()=>clearTimeout(timer));}
const boot=deferred(),summary=deferred(),attached=deferred();const unsubscribeWaiters=[];let nativeOutput='';let reads=0,subscriptions=0,unsubscriptions=0;let ownerLabel='Fixture game A';const peers=[];
const relayDeps={
  read:async()=>{reads++;if(gameMemory){const entry=gameMemory.store.getGameCustomConfig().entries['game.exe'];const option=(value,choices)=>({value,supported:true,choices:choices.map(data=>({data,label:data}))});return {gameAdmission:makeGameMirrorAdmission(entry,schedule),generation:1,ready:true,actions:{game:true,fan:false},game:{label:ownerLabel,identity:gameMirrorIdentity(game),fields:{acMode:option(entry?.acMode??'follow',['follow','eco','balanced','medium','performance','elite','extreme']),dcMode:option(entry?.dcMode??'follow',['follow','eco','balanced','medium','performance','elite','extreme']),padPersona:option(entry?.padPersona??'follow',['follow','elite','disabled','steamdeck']),gyroOverride:option(entry?.gyroOverride??'follow',['follow','fps','racing','off'])}},fan:{supported:false,enabled:false,preset:'',choices:[]},notice:'Original EXE memory store; owner application deliberately not acknowledged'};}if(fanFixture)return fanFixture.snapshot(ownerLabel);return {generation:1,ready:false,game:{label:ownerLabel,identity:'a.exe:101:birthA',fields:{}},fan:{supported:true,enabled:false,preset:'balanced',choices:[{data:'balanced',label:'均衡转速'}]},notice:'Fixture data; production relay/transport/client'};},
  execute:gameActions?(command,args,context)=>gameActions.execute(command,args,context):fanFixture ? (command,args,context)=>{
    if(command !== 'fan.setEnabled' && command !== 'fan.setPreset')return Promise.reject(new Error('MIRROR_GAME_NOT_WIRED'));
    return fanFixture.execute(command,args,context);
  }:undefined,
  observe:()=>{subscriptions++;const stopConfig=gameMemory?.store.onGameCustomConfigChanged(()=>relay.invalidate());return()=>{stopConfig?.();unsubscriptions++;for(const waiter of [...unsubscribeWaiters])if(unsubscriptions>=waiter.count)waiter.done.resolve();};},
  reply:async(connection,message)=>{child.stdin.write(JSON.stringify({connection,message})+'\n');return true;},
};
let relay=new ReadOnlyMirrorRelay(relayDeps);
child.stdout.on('data',data=>{nativeOutput+=data.toString('utf8');for(;;){const end=nativeOutput.indexOf('\n');if(end<0)break;const line=nativeOutput.slice(0,end).trim();nativeOutput=nativeOutput.slice(end+1);if(!line)continue;
  try {const message=JSON.parse(line);if(message.endpoint)boot.resolve(message);else if(message.type==='peer'){peers.push(message);relay.connected(message);}else if(message.type==='request')void relay.request(message);else if(message.type==='attach')attached.resolve(message);else if(message.endpointReleased)summary.resolve(message);}catch(error){summary.reject(error);}
}});
function masked(payload,opcode=1){const body=Buffer.from(payload);const mask=crypto.randomBytes(4);let header;
if(body.length<126)header=Buffer.from([0x80|opcode,0x80|body.length]);else header=Buffer.from([0x80|opcode,0xfe,body.length>>8,body.length&255]);const copy=Buffer.from(body);for(let i=0;i<copy.length;i++)copy[i]^=mask[i%4];return Buffer.concat([header,mask,copy]);}
class SocketAdapter {
  listeners=new Map();buffer=Buffer.alloc(0);upgraded=false;closed=false;sent=[];
  constructor(url,{origin='https://steamloopback.host'}={}){
    const parsed=new URL(url);assert.equal(parsed.hostname,'127.0.0.1');const nonce=crypto.randomBytes(16).toString('base64');this.accept=crypto.createHash('sha1').update(nonce+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    this.raw=net.connect({host:'127.0.0.1',port:Number(parsed.port)},()=>this.raw.write(`GET ${parsed.pathname}${parsed.search} HTTP/1.1\r\nHost: 127.0.0.1:${parsed.port}\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: ${nonce}\r\nOrigin: ${origin}\r\n\r\n`));
    this.raw.on('error',()=>{});this.raw.on('close',()=>{this.closed=true;this.emit('close',{});});
    this.raw.on('data',data=>{this.buffer=Buffer.concat([this.buffer,data]);if(!this.upgraded){const end=this.buffer.indexOf('\r\n\r\n');if(end<0)return;const header=this.buffer.subarray(0,end).toString('ascii');assert.match(header,/^HTTP\/1.1 101 /);assert.ok(header.includes(this.accept));this.buffer=this.buffer.subarray(end+4);this.upgraded=true;this.emit('open',{});}
      while(this.buffer.length>=2){let size=this.buffer[1]&127,offset=2;if(size===126){if(this.buffer.length<4)return;size=this.buffer.readUInt16BE(2);offset=4;}else if(size===127){if(this.buffer.length<10)return;size=Number(this.buffer.readBigUInt64BE(2));offset=10;}assert.ok(size<=65536);assert.equal(this.buffer[1]&0x80,0);if(this.buffer.length<offset+size)return;const opcode=this.buffer[0]&15;const body=this.buffer.subarray(offset,offset+size);this.buffer=this.buffer.subarray(offset+size);if(opcode===1)this.emit('message',{data:body.toString('utf8')});else if(opcode===8)this.close();}
    });
  }
  addEventListener(event,listener){let listeners=this.listeners.get(event);if(!listeners)this.listeners.set(event,listeners=new Set());listeners.add(listener);}
  emit(event,value){for(const listener of this.listeners.get(event)??[])listener(value);}
  send(text){this.sent.push(JSON.parse(text));this.raw.write(masked(text));}
  close(){this.raw.destroy();}
}
function awaitState(client,predicate){if(predicate(client.snapshot()))return Promise.resolve(client.snapshot());const done=deferred();const stop=client.subscribe(state=>{if(predicate(state))done.resolve(state);});return bounded(done.promise,'client state').finally(stop);}
function waitUnsubscribe(count){if(unsubscriptions>=count)return Promise.resolve();const done=deferred();unsubscribeWaiters.push({count,done});return bounded(done.promise,'peer unsubscribe');}
async function rejected(url,options){const socket=new SocketAdapter(url,options);const done=deferred();socket.addEventListener('close',()=>done.resolve());await bounded(done.promise,'unauthorized close');assert.equal(socket.upgraded,false);}
const cases=[];const resources=[];let client;
try{
 const bootstrap=await bounded(boot.promise,'native bootstrap');
 await rejected(`${bootstrap.endpoint}?token=wrong`);cases.push('wrong credential rejected before owner callback');
 await rejected(`${bootstrap.endpoint}?token=${bootstrap.token}`,{origin:'https://evil.example'});cases.push('foreign Origin rejected before owner callback');
 assert.equal(reads,0);assert.equal(subscriptions,0);
 const partial=new SocketAdapter(`${bootstrap.endpoint}?token=${bootstrap.token}`);resources.push(partial);
 const partialOpen=deferred(),partialClosed=deferred();partial.addEventListener('open',()=>partialOpen.resolve());partial.addEventListener('close',()=>partialClosed.resolve());
 await bounded(partialOpen.promise,'partial-frame connection');partial.raw.write(Buffer.from([0x81,0x80]));await bounded(partialClosed.promise,'partial-frame deadline');await waitUnsubscribe(1);
 assert.equal(reads,0);cases.push('authenticated incomplete frame is closed on bounded deadline');resources.length=0;
 client=new MirrorClient({bootstrap:()=>bootstrap,socket:url=>{const socket=new SocketAdapter(url);resources.push(socket);return socket;},schedule:(fn,ms)=>setTimeout(fn,ms),cancel:clearTimeout,clientId:'loopback-client'});
 client.setVisible(true);const first=await awaitState(client,state=>state.connected);assert.equal(first.snapshot.game.label,'Fixture game A');assert.equal(first.snapshot.ready,mutations);assert.equal(reads,1);assert.equal(resources.length,1);cases.push('actual broker / production relay / production client snapshot chain works on Windows');
 ownerLabel='Fixture game B';relay.invalidate();relay.invalidate();await awaitState(client,state=>state.snapshot?.game?.label==='Fixture game B');assert.equal(reads,2);assert.equal(resources.length,1);cases.push('source-event push reuses one connection with one coalesced read');
 relay.dispose();relay=new ReadOnlyMirrorRelay(relayDeps);child.stdin.write('attach\n');const meta=await bounded(attached.promise,'renderer reattach metadata');
 assert.equal(meta.revision,2);relay.connected(meta);ownerLabel='Fixture renderer remounted';await relay.request({...meta,request:{command:'snapshot',runId:bootstrap.runId}});
 const remounted=await awaitState(client,state=>state.snapshot?.game?.label==='Fixture renderer remounted');assert.equal(remounted.snapshot.revision,3);assert.equal(resources.length,1);
 cases.push('renderer restart seeds native revision floor without reconnecting or stale UI');
 if(!mutations){
 const denied=deferred();resources[0].addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.type==='reply'&&message.id==='test-action')denied.resolve(message);});
 resources[0].send(JSON.stringify({command:'fan.setEnabled',runId:bootstrap.runId,id:'test-action',args:{enabled:true}}));const reply=await bounded(denied.promise,'read-only reply');assert.equal(reply.ok,false);assert.match(reply.error,/只读/);assert.equal(reads,3);cases.push('mutation reaches read-only gate and performs no provider/hardware action');
 }else if(gameMutations){
   const firstSave=await client.mutate('game.setField',{field:'acMode',value:'performance'});assert.equal(firstSave.saved,true);assert.equal(firstSave.applied,false);assert.equal(firstSave.pending,true);await awaitState(client,state=>state.snapshot?.game?.fields.acMode.value==='performance');
   assert.equal(gameMemory.raw().entries['game.exe'].enabled,true);assert.equal(gameSaves,1);cases.push('production client/native broker/relay/thin adapter saves and enables original EXE memory entry');
   const packet=resources[0].sent.find(packet=>packet.command==='game.setField');const replay=deferred();resources[0].addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.type==='reply'&&message.id===packet.id)replay.resolve(message);});resources[0].send(JSON.stringify(packet));assert.equal((await bounded(replay.promise,'game cached ACK')).ok,true);assert.equal(gameSaves,1);cases.push('exact game ACK replay returns cached receipt without a second original save');
   await client.mutate('game.setField',{field:'dcMode',value:'balanced'});await awaitState(client,state=>state.snapshot?.game?.fields.dcMode.value==='balanced');
   await client.mutate('game.setField',{field:'padPersona',value:'elite'});await awaitState(client,state=>state.snapshot?.game?.fields.padPersona.value==='elite');
   await client.mutate('game.setField',{field:'gyroOverride',value:'fps'});await awaitState(client,state=>state.snapshot?.game?.fields.gyroOverride.value==='fps');
   assert.equal(gameSaves,4);assert.deepEqual(gameMemory.raw().rtss,{'other.exe':{enabled:true,acFps:50,dcFps:30,acCeiling:60,dcCeiling:30,acLastFps:50,dcLastFps:30},'game.exe':{enabled:true,acFps:120,dcFps:60,acCeiling:120,dcCeiling:60,acLastFps:120,dcLastFps:60}});assert.equal(gameMemory.lock.isQuickActionBusy(),false);cases.push('DC/input settings round-trip through same EXE map, preserve the other RTSS-only record and remember the independent whole-profile pair');
 }else{
   const on=await client.mutate('fan.setEnabled',{enabled:true});assert.equal(on.applied,true);await awaitState(client,state=>state.snapshot?.fan.enabled===true);
   const firstEnablePacket=resources[0].sent.find(packet=>packet.command==='fan.setEnabled');
   assert.equal(fanFixture.stats().calls.filter(value=>value==='enable').length,1);cases.push('client switch invokes unchanged original start/apply once');
   await client.mutate('fan.setPreset',{preset:'aggressive'});await awaitState(client,state=>state.snapshot?.fan.preset==='aggressive');
   assert.equal(fanFixture.stats().calls.filter(value=>value==='preset').length,1);cases.push('client dropdown persists global preset through original applyPreset/lease');
   const before=fanFixture.stats().calls.filter(call=>call!=='heartbeat').length;const duplicate=deferred();resources[0].addEventListener('message',event=>{const message=JSON.parse(event.data);if(message.type==='reply'&&message.id===firstEnablePacket.id)duplicate.resolve(message);});
   resources[0].send(JSON.stringify(firstEnablePacket));const cached=await bounded(duplicate.promise,'cached action reply');assert.equal(cached.ok,true);assert.equal(fanFixture.stats().calls.filter(call=>call!=='heartbeat').length,before);cases.push('lost ACK replay gets cached outcome, no repeated owner/hardware write call');
   await client.mutate('fan.setEnabled',{enabled:false});await awaitState(client,state=>state.snapshot?.fan.enabled===false);
   assert.equal(fanFixture.stats().lease,null);assert.ok(fanFixture.stats().calls.includes('restore')&&fanFixture.stats().calls.includes('release'));cases.push('client off invokes original OEM restore/release path');
   const ownerCalls=fanFixture.stats().calls.length;const save=await client.mutate('fan.setPreset',{preset:'soft'});await awaitState(client,state=>state.snapshot?.fan.preset==='soft');
   assert.equal(save.saved,true);assert.equal(save.applied,false);assert.equal(fanFixture.stats().calls.length,ownerCalls);cases.push('off-state dropdown saves only, never starts hardware owner');
   await assert.rejects(client.mutate('game.setField',{field:'acMode',value:'eco'}),/尚不可用/);cases.push('unfinished game actions explicitly stay disabled');
 }
 client.setVisible(false);await waitUnsubscribe(3);assert.equal(subscriptions,3);assert.equal(unsubscriptions,3);cases.push('QAM hide releases socket and source observers');
 client.dispose();relay.dispose();await fanFixture?.close();child.stdin.end('stop\n');const result=await bounded(summary.promise,'native release summary');const terminal=await bounded(exit,'native process exit');assert.equal(terminal.code,0);assert.equal(result.activeAfterStop,false);assert.equal(result.mutationRequests,mutations?5:1);cases.push('native stop and restart release handles and rotate credentials');
 const report={project:'YMCC Decky 侧边栏',actualOriginalFanLifecycle:mutations&&!gameMutations,actualGameMirrorAdapter:gameMutations,gameSaves,gameMemoryStats:gameMemory?.stats(),gameHardwareOwnerApplied:false,fanStats:fanFixture?.stats(),cases:cases.length,results:cases,status:'passed',platform:process.platform,nativeBroker:true,productionRelay:true,productionClient:true,businessOwnerFixture:true,actualWebView:false,actualSteam:false,hardwareActions:0,userConfigWrites:0,newDependencies:0,sourceReads:reads,activeMirrorSocketsAfterTest:0,nativeSummary:result};
 fs.writeFileSync(path.resolve(task,gameMutations?'validation/MIRROR-GAME-LOOPBACK-IMPLEMENTATION09.json':mutations?'validation/MIRROR-FAN-LOOPBACK-INTEGRATION.json':'validation/MIRROR-LOOPBACK-INTEGRATION.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}catch(error){client?.dispose();relay.dispose();await fanFixture?.close().catch(()=>{});for(const socket of resources)socket.close();child.stdin.end('stop\n');await bounded(exit,'failure cleanup').catch(()=>{child.kill();});throw new Error(`${error.stack}; fanStats=${JSON.stringify(fanFixture?.stats())}; stderr=${stderr}`);}
