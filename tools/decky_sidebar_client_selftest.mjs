import {performanceModelModule} from './fixtures/decky_game_memory_store.mjs';
import { task } from './fixtures/decky_task_paths.mjs';
// Actual shipped client, browser IO/clock replaced with fakes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { createRequire } from 'node:module';
const root = path.resolve(import.meta.dirname, '..');
const require = createRequire(path.join(root,'package.json'));
const { transformSync } = require('esbuild');
const contextModule={exports:{}};
vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module:contextModule,exports:contextModule.exports});
const module = { exports: {} };
vm.runInNewContext(transformSync(fs.readFileSync(process.env.YMCC_MIRROR_CLIENT_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/mirrorClient.ts'),'utf8'), { loader:'ts',format:'cjs' }).code,
  { module,exports:module.exports,structuredClone,require: name => { if(name==='./performanceModel')return performanceModelModule;if(name==='./steamRunningContext')return contextModule.exports; throw new Error('Unexpected external dependency'); } });
const { MirrorClient } = module.exports;
const cases=[];
async function check(name,action) { try{await action();cases.push({name,status:'passed'});}catch(error){throw new Error(name+': '+error.message,{cause:error});} }
function fixture({boot=true,steam}={}) {
  const timers=new Map(); const sockets=[];let next=0;
  let available=boot;
  const bootstrap={endpoint:'ws://127.0.0.1:12345/mirror',token:'TEST-ONLY',runId:'run-1'};
  class Socket {
    listeners=new Map();sent=[];closed=false;
    constructor(url){this.url=url;sockets.push(this);}
    addEventListener(e,f){this.listeners.set(e,f);}
    send(value){this.sent.push(JSON.parse(value));}
    close(){this.closed=true;this.listeners.get('close')?.();}
    emit(e,value){this.listeners.get(e)?.({data:typeof value==='string'?value:JSON.stringify(value)});}
  }
  const deps={bootstrap:()=>available?bootstrap:undefined,socket:url=>new Socket(url),schedule:(action,delay)=>{const id=++next;timers.set(id,{action,delay});return id;},cancel:id=>timers.delete(id),clientId:'test-client',steam};
  const client=new MirrorClient(deps);
  const fireDelay=delay=>{const pair=[...timers].find(([,t])=>t.delay===delay);assert.ok(pair,`No timer ${delay}`);timers.delete(pair[0]);pair[1].action();};
  const snapshot=(revision=1)=>({runId:'run-1',generation:1,revision,ready:true,game:{label:'A',identity:'app:480:pid101:birthA',fields:{acMode:{value:'balanced',supported:true,choices:[{data:'balanced',label:'平衡'}]}}},fan:{supported:true,enabled:true,preset:'soft',choices:[{data:'soft',label:'轻柔'}]}});
  const connect=()=>{client.setVisible(true);const socket=sockets.at(-1);socket.emit('open');socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snapshot()});return socket;};
  return {client,sockets,timers,bootstrap,fireDelay,snapshot,connect,makeBootstrapAvailable:()=>{available=true;}};
}
await check('missing bootstrap starts no discovery/network/timer',()=>{const f=fixture({boot:false});f.client.setVisible(true);assert.equal(f.sockets.length,0);assert.equal(f.timers.size,0);});
await check('non-loopback bootstrap rejected',()=>{const f=fixture();f.bootstrap.endpoint='ws://example.com:12345/mirror';f.client.setVisible(true);assert.equal(f.sockets.length,0);});
await check('one visible socket requests one snapshot, no idle polling',()=>{const f=fixture();const s=f.connect();assert.equal(s.sent.length,1);assert.equal(s.sent[0].command,'snapshot');assert.equal(f.timers.size,0);f.client.setVisible(true);assert.equal(f.sockets.length,1);f.client.dispose();});
await check('hidden/unloaded releases socket and timers',()=>{const f=fixture();f.connect();f.client.setVisible(false);assert.equal(f.timers.size,0);assert.equal(f.sockets[0].closed,true);assert.equal(f.client.snapshot().snapshot,null);});
await check('old view packets ignored after hide',()=>{const f=fixture();const s=f.connect();f.client.setVisible(false);s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(100)});assert.equal(f.client.snapshot().connected,false);});
await check('older revisions do not overwrite fresh state',()=>{const f=fixture();const s=f.connect();s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(10)});s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(5)});assert.equal(f.client.snapshot().snapshot.revision,10);f.client.dispose();});
await check('malformed snapshot rejects instead of crashing React',()=>{const f=fixture();const s=f.connect();s.emit('message',{type:'snapshot',runId:'run-1',snapshot:{runId:'run-1',revision:2,generation:1}});assert.equal(f.client.snapshot().connected,false);assert.equal(s.closed,true);});
await check('restart run ID disconnects and no mutation replay',()=>{const f=fixture();const s=f.connect();s.emit('message',{type:'snapshot',runId:'run-2',snapshot:f.snapshot()});assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,0);});
await check('request freezes identity/generation/revision and args',async()=>{const f=fixture();const s=f.connect();const args={field:'acMode',value:'eco'};const p=f.client.mutate('game.setField',args);args.value='extreme';const req=s.sent.at(-1);assert.equal(req.args.value,'eco');assert.equal(req.identity,'app:480:pid101:birthA');assert.equal(req.generation,1);s.emit('message',{type:'reply',runId:'run-1',id:req.id,ok:true,result:{saved:true,applied:true}});await p;assert.equal(f.timers.size,0);f.client.dispose();});
await check('second mutation cannot overlap first',async()=>{const f=fixture();const s=f.connect();const p=f.client.mutate('fan.setPreset',{preset:'soft'});await assert.rejects(f.client.mutate('fan.setPreset',{preset:'aggressive'}),/当前不可操作/);const req=s.sent.at(-1);s.emit('message',{type:'reply',runId:'run-1',id:req.id,ok:true});await p;f.client.dispose();});
await check('timeout never resends a possibly applied action',async()=>{const f=fixture();const s=f.connect();const p=f.client.mutate('fan.setEnabled',{enabled:false});f.fireDelay(4000);await assert.rejects(p,/未确认/);assert.equal(s.sent.filter(m=>m.command==='fan.setEnabled').length,1);assert.equal(s.sent.at(-1).command,'snapshot');assert.equal(f.client.snapshot().snapshot,null);assert.equal(f.client.snapshot().busy,false);f.client.dispose();});
await check('close rejects inflight mutation and reconnect does not replay it',async()=>{const f=fixture();const s=f.connect();const p=f.client.mutate('fan.setEnabled',{enabled:false});s.close();await assert.rejects(p,/中断/);f.fireDelay(250);const next=f.sockets.at(-1);next.emit('open');assert.equal(next.sent.length,1);assert.equal(next.sent[0].command,'snapshot');f.client.dispose();});
await check('silent first connection has finite deadline and reconnect',()=>{const f=fixture();f.client.setVisible(true);f.fireDelay(10000);assert.equal(f.sockets[0].closed,true);f.fireDelay(250);assert.equal(f.sockets.length,2);f.client.dispose();});
await check('visible sidebar recovers beyond three failures with capped outage backoff',()=>{const f=fixture();f.client.setVisible(true);for(const delay of [250,1000,3000,10000,30000,30000]){f.sockets.at(-1).close();f.fireDelay(delay);}assert.equal(f.sockets.length,7);const s=f.sockets.at(-1);s.emit('open');s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot()});assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);f.client.dispose();});
await check('hide cancels queued reconnect',()=>{const f=fixture();const s=f.connect();s.close();f.client.setVisible(false);assert.equal(f.timers.size,0);assert.equal(f.sockets.length,1);});
await check('power not ready rejects changes without send',async()=>{const f=fixture();const s=f.connect();const snap=f.snapshot(2);snap.ready=false;s.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});await assert.rejects(f.client.mutate('fan.setEnabled',{enabled:true}),/当前不可操作/);assert.equal(s.sent.length,1);f.client.dispose();});
await check('fan requests do not include game identity',async()=>{const f=fixture();const s=f.connect();const p=f.client.mutate('fan.setPreset',{preset:'soft'});const req=s.sent.at(-1);assert.equal(req.identity,undefined);s.emit('message',{type:'reply',runId:'run-1',id:req.id,ok:true});await p;f.client.dispose();});
await check('late native bootstrap starts cold plugin by event, no polling',()=>{const f=fixture({boot:false});f.client.setVisible(true);assert.equal(f.sockets.length,0);f.makeBootstrapAvailable();f.client.bootstrapChanged();assert.equal(f.sockets.length,1);f.client.dispose();});
await check('same native binding event cannot duplicate live connection',()=>{const f=fixture();f.connect();f.client.bootstrapChanged();assert.equal(f.sockets.length,1);assert.equal(f.timers.size,0);f.client.dispose();});
await check('new native run cancels old mutation, never replays into new host',async()=>{const f=fixture();const old=f.connect();const mutation=f.client.mutate('fan.setEnabled',{enabled:true});f.bootstrap.runId='run-2';f.bootstrap.token='NEW-TEST-TOKEN';f.client.bootstrapChanged();await assert.rejects(mutation,/连接已结束/);assert.equal(old.closed,true);assert.equal(f.sockets.length,2);f.sockets[1].emit('open');assert.equal(f.sockets[1].sent.length,1);assert.equal(f.sockets[1].sent[0].command,'snapshot');assert.equal(f.sockets[1].sent[0].runId,'run-2');f.client.dispose();});
await check('hidden bootstrap event starts zero sockets',()=>{const f=fixture();f.client.bootstrapChanged();assert.equal(f.sockets.length,0);assert.equal(f.timers.size,0);f.client.dispose();});
await check('late message from closed socket cannot restore connected state',()=>{const f=fixture();const old=f.connect();old.close();old.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(50)});assert.equal(f.client.snapshot().connected,false);assert.equal(f.client.snapshot().snapshot,null);f.client.dispose();});
await check('connection owns immutable bootstrap identity, not mutable global object',()=>{const f=fixture();const old=f.connect();f.bootstrap.runId='changed-without-event';old.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(2)});assert.equal(f.client.snapshot().snapshot.runId,'run-1');f.client.dispose();});
await check('port zero and out-of-range bootstrap are rejected',()=>{for(const port of [0,65536,99999]){const f=fixture();f.bootstrap.endpoint=`ws://127.0.0.1:${port}/mirror`;f.client.setVisible(true);assert.equal(f.sockets.length,0);f.client.dispose();}});
await check('Steam runtime ID getter runs once per demand, not per render or timer',()=>{let calls=0;const f=fixture({steam:()=>{calls++;return {availability:'observed',appId:'480',runningCount:1};}});const socket=f.connect();assert.equal(calls,1);assert.equal(socket.sent[0].steam.appId,'480');assert.equal(f.timers.size,0);f.client.setVisible(true);assert.equal(calls,1);f.client.dispose();});
await check('Steam runtime getter failure cannot prevent Fan snapshot connection',()=>{const f=fixture({steam:()=>{throw new Error('Steam API changed');}});const socket=f.connect();assert.equal(socket.sent[0].steam.availability,'unavailable');assert.equal(f.client.snapshot().connected,true);f.client.dispose();});
await check('slider accepts only the exact rendered snapshot',async()=>{const f=fixture(),s=f.connect(),expected=f.client.snapshot().snapshot;const p=f.client.mutate('fan.setPreset',{preset:'soft'},expected);const req=s.sent.at(-1);s.emit('message',{type:'reply',runId:'run-1',id:req.id,ok:true});await p;f.client.dispose();});
await check('slider rendered revision change rejects before sending any mutation',async()=>{const f=fixture(),s=f.connect(),expected=f.client.snapshot().snapshot;s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(2)});await assert.rejects(f.client.mutate('fan.setPreset',{preset:'soft'},expected),/快照已变化/);assert.equal(s.sent.length,1);f.client.dispose();});
await check('slider game identity cannot migrate to a different foreground game',async()=>{const f=fixture(),s=f.connect(),expected=f.client.snapshot().snapshot;expected.game.identity='old-foreground';await assert.rejects(f.client.mutate('game.setField',{field:'corePolicyMode',value:'only-small'},expected),/快照已变化/);assert.equal(s.sent.length,1);f.client.dispose();});
await check('failed action notice survives confirmed unchanged-state snapshot without retrying',async()=>{const f=fixture(),socket=f.connect();const pending=f.client.mutate('fan.setEnabled',{enabled:true}),id=socket.sent.at(-1).id;socket.emit('message',{type:'reply',runId:'run-1',id,ok:false,error:'owner launch denied'});await assert.rejects(pending,/denied/);socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(2)});assert.equal(f.client.snapshot().notice,'owner launch denied');assert.equal(socket.sent.filter(v=>v.command==='fan.setEnabled').length,1);const next=f.client.mutate('fan.setEnabled',{enabled:true}),nextId=socket.sent.at(-1).id;socket.emit('message',{type:'reply',runId:'run-1',id:nextId,ok:true});await next;socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(3)});assert.equal(f.client.snapshot().notice,'');f.client.dispose();});
await check('failed action notice cannot leak into another game or hidden/reopened session',async()=>{const f=fixture(),socket=f.connect();const pending=f.client.mutate('fan.setEnabled',{enabled:true}),id=socket.sent.at(-1).id;socket.emit('message',{type:'reply',runId:'run-1',id,ok:false,error:'old game error'});await assert.rejects(pending);const snap=f.snapshot(2);snap.game.identity='new-game';socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});assert.equal(f.client.snapshot().notice,'');f.client.setVisible(false);f.client.dispose();});
await check('monitor accepts one boolean intent without requiring a game or fan permission',async()=>{const f=fixture(),s=f.connect(),snap=f.snapshot(2);snap.game=null;snap.monitor={enabled:true};snap.actions={monitor:true,game:false,fan:false};s.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});const p=f.client.mutate('monitor.setEnabled',{enabled:false});const request=s.sent.at(-1);assert.equal(request.command,'monitor.setEnabled');assert.deepEqual(request.args,{enabled:false});assert.equal(request.identity,undefined);assert.ok([...f.timers.values()].some(timer=>timer.delay===30000));s.emit('message',{type:'reply',runId:'run-1',id:request.id,ok:true,result:{saved:true,applied:true}});await p;assert.equal(f.timers.size,0);f.client.dispose();});
await check('monitor never sends without state/permission or with invented parameters',async()=>{for(const mode of ['state','permission','denied','args']){const f=fixture(),s=f.connect(),snap=f.snapshot(2);snap.monitor={enabled:true};snap.actions={monitor:true,fan:true,game:true};if(mode==='state')delete snap.monitor;if(mode==='permission')delete snap.actions;if(mode==='denied')snap.actions.monitor=false;s.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});const before=s.sent.length;await assert.rejects(f.client.mutate('monitor.setEnabled',mode==='args'?{enabled:false,output:'rtss'}:{enabled:false}));assert.equal(s.sent.length,before);assert.equal(f.timers.size,0);f.client.dispose();}});
await check('malformed monitor state/permission terminates the session safely',()=>{for(const mode of ['state','permission']){const f=fixture(),s=f.connect(),snap=f.snapshot(2);if(mode==='state')snap.monitor={enabled:'on'};else snap.actions={fan:true,game:true,monitor:'yes'};s.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});assert.equal(s.closed,true);f.client.dispose();}});
await check('monitor timeout requests fresh state and never replays the intent',async()=>{const f=fixture(),s=f.connect(),snap=f.snapshot(2);snap.monitor={enabled:true};snap.actions={monitor:true,fan:false,game:false};s.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});const p=f.client.mutate('monitor.setEnabled',{enabled:false});f.fireDelay(30000);await assert.rejects(p,/未确认/);assert.equal(s.sent.filter(packet=>packet.command==='monitor.setEnabled').length,1);assert.equal(s.sent.at(-1).command,'snapshot');assert.equal(f.client.snapshot().snapshot,null);f.client.dispose();});
await check('connection test needs its own fresh native reply and never emits a mutation',async()=>{const f=fixture(),s=f.connect();const pending=f.client.testConnection();const request=s.sent.at(-1);assert.match(request.probeId,/^probe:test-client:/);assert.equal(request.command,'snapshot');const snap=f.snapshot(2);snap.provenance={kind:'ymcc-native',pid:123};s.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});assert.equal(f.client.snapshot().connectionTest.pending,true);s.emit('message',{type:'snapshot',runId:'run-1',probeId:'probe:other-client:1',snapshot:f.snapshot(3)});assert.equal(f.client.snapshot().connectionTest.pending,true);snap.revision=4;s.emit('message',{type:'snapshot',runId:'run-1',probeId:request.probeId,snapshot:snap});assert.equal((await pending).pid,123);assert.equal(f.timers.size,0);assert.ok(s.sent.every(p=>p.command==='snapshot'));f.client.dispose();});
await check('connection test cannot mistake unconfirmed or not-ready replies for success',async()=>{for(const mode of ['unknown','not-ready']){const f=fixture(),s=f.connect(),pending=f.client.testConnection(),probeId=s.sent.at(-1).probeId,snap=f.snapshot(2);if(mode==='not-ready'){snap.ready=false;snap.provenance={kind:'ymcc-native',pid:123};}s.emit('message',{type:'snapshot',runId:'run-1',probeId,snapshot:snap});await assert.rejects(pending,/尚未就绪/);assert.equal(f.client.snapshot().connectionTest.pending,false);f.client.dispose();}});
await check('connection test timeout does not replay or block business controls',async()=>{const f=fixture(),s=f.connect(),pending=f.client.testConnection();f.fireDelay(15000);await assert.rejects(pending,/15 秒/);assert.equal(f.client.snapshot().busy,false);assert.equal(s.sent.length,2);assert.equal(f.client.snapshot().connected,true);f.client.dispose();});
await check('hiding disposes the connection test and consumes late responses',async()=>{const f=fixture(),s=f.connect(),pending=f.client.testConnection();f.client.setVisible(false);await assert.rejects(pending,/隐藏/);assert.equal(f.timers.size,0);f.client.dispose();});
await check('connection test reconnects existing binding without replaying writes',async()=>{const f=fixture(),s=f.connect();s.close();const pending=f.client.testConnection(),next=f.sockets.at(-1);next.emit('open');const req=next.sent.at(-1),snap=f.snapshot(2);snap.provenance={kind:'ymcc-native',pid:321};next.emit('message',{type:'snapshot',runId:'run-1',probeId:req.probeId,snapshot:snap});assert.equal((await pending).pid,321);assert.equal(f.timers.size,0);f.client.dispose();});
await check('trainer open requires a confirmed ready entry, not a missing or pending snapshot',async()=>{
 for(const trainer of [undefined,{available:false,pending:false,notice:''},{available:true,pending:true,notice:''}]){const f=fixture(),socket=f.connect(),snap=f.snapshot(2);snap.trainer=trainer;socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});const before=socket.sent.length;await assert.rejects(f.client.mutate('game.setField',{field:'trainer',value:'open'}),/未找到/);assert.equal(socket.sent.length,before);assert.equal(f.timers.size,0);f.client.dispose();}
});
await check('trainer request sends only open intent and exact current-game admission identity',async()=>{
 const f=fixture(),socket=f.connect(),snap=f.snapshot(2);snap.trainer={available:true,pending:false,notice:''};snap.actions={game:true,fan:false};socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});const pending=f.client.mutate('game.setField',{field:'trainer',value:'open'},snap),request=socket.sent.at(-1);assert.deepEqual(request.args,{field:'trainer',value:'open'});assert.equal(request.identity,snap.game.identity);assert.equal(request.revision,snap.revision);assert.equal(JSON.stringify(request).includes('sha256'),false);socket.emit('message',{type:'reply',runId:'run-1',id:request.id,ok:true,result:{saved:false,applied:false,pending:true}});await pending;assert.equal(f.client.snapshot().busy,false);f.client.dispose();
});
await check('trainer-only pending state keeps existing game controls available and refuses a second launch',async()=>{
 const f=fixture(),socket=f.connect(),snap=f.snapshot(2);snap.trainer={available:true,pending:true,notice:''};snap.actions={game:true,fan:false};socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});await assert.rejects(f.client.mutate('game.setField',{field:'trainer',value:'open'}));const edit=f.client.mutate('game.setField',{field:'acMode',value:'eco'}),request=socket.sent.at(-1);assert.equal(request.args.field,'acMode');socket.emit('message',{type:'reply',runId:'run-1',id:request.id,ok:true,result:{saved:true,applied:true}});await edit;assert.equal(f.timers.size,0);f.client.dispose();
});
await check('malformed trainer states and private paths are rejected before UI sees them',()=>{
 for(const trainer of [{available:'yes',pending:false,notice:''},{available:true,pending:'yes',notice:''},{available:true,pending:false,notice:1},{available:true,pending:false,notice:'x'.repeat(301)},{available:true,pending:false,notice:'',path:'C:/private/file.exe'}]){const f=fixture(),socket=f.connect(),snap=f.snapshot(2);snap.trainer=trainer;socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});assert.equal(socket.closed,true);f.client.dispose();}
});
await check('available trainer without a current game is an invalid snapshot',()=>{const f=fixture(),socket=f.connect(),snap=f.snapshot(2);snap.game=null;snap.trainer={available:true,pending:false,notice:''};socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});assert.equal(socket.closed,true);f.client.dispose();});
await check('trainer open rejects alternate actions and supplied file paths without sending',async()=>{const f=fixture(),socket=f.connect(),snap=f.snapshot(2);snap.trainer={available:true,pending:false,notice:''};socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});const count=socket.sent.length;for(const args of [{field:'trainer',value:'download'},{field:'trainer',value:'open',path:'C:/anything.exe'}])await assert.rejects(f.client.mutate('game.setField',args));assert.equal(socket.sent.length,count);f.client.dispose();});
await check('trainer intent timeout refreshes state and never repeats the launch',async()=>{const f=fixture(),socket=f.connect(),snap=f.snapshot(2);snap.trainer={available:true,pending:false,notice:''};socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});const pending=f.client.mutate('game.setField',{field:'trainer',value:'open'});f.fireDelay(4000);await assert.rejects(pending,/未确认/);assert.equal(socket.sent.filter(p=>p.args?.field==='trainer').length,1);assert.equal(socket.sent.at(-1).command,'snapshot');assert.equal(f.client.snapshot().busy,false);f.client.dispose();});
await check('wallpaper passive demand does not own HUD traffic or business snapshots',()=>{const f=fixture();f.bootstrap.wallpaperProtocol=1;f.bootstrap.hudProtocol=1;f.client.setPassiveDemand('wallpaper',true);const socket=f.sockets.at(-1);socket.emit('open');assert.equal(socket.sent.length,0);assert.equal(f.client.sendHud('hud.status',{}),false);assert.equal(f.client.sendWallpaper('wallpaper.context',{contextSeq:1,appId:'570',visible:true}),true);assert.equal(socket.sent.length,1);assert.equal(f.timers.size,1);socket.emit('message',{type:'wallpaper-ready',runId:f.bootstrap.runId,contextSeq:1,enabled:true});assert.equal(f.timers.size,0);f.client.dispose();});
await check('HUD and wallpaper independently release one shared connection',()=>{const f=fixture();f.bootstrap.wallpaperProtocol=1;f.bootstrap.hudProtocol=1;f.client.setPassiveDemand('wallpaper',true);const socket=f.sockets.at(-1);socket.emit('open');f.client.setPassiveDemand('hud',true);f.client.setPassiveDemand('wallpaper',false);assert.equal(socket.closed,false);assert.equal(f.client.sendWallpaper('wallpaper.context',{}),false);assert.equal(f.client.sendHud('hud.status',{}),true);assert.equal(f.sockets.length,1);f.client.setPassiveDemand('hud',false);assert.equal(socket.closed,true);assert.equal(f.timers.size,0);f.client.dispose();});

function rebind(f){f.bootstrap.runId='run-2';f.bootstrap.token='NEW-TEST-TOKEN';f.bootstrap.endpoint='ws://127.0.0.1:23456/mirror';f.client.bootstrapChanged();const next=f.sockets.at(-1);next.emit('open');next.emit('message',{type:'snapshot',runId:'run-2',snapshot:{...f.snapshot(2),runId:'run-2'}});return next;}
await check('a busy observer rebind cannot forward an old write into the replacement broker',async()=>{
 const f=fixture(),old=f.connect();let once=false,next;f.client.subscribe(state=>{if(state.busy&&!once){once=true;next=rebind(f);}});const pending=f.client.mutate('fan.setPreset',{preset:'soft'}).then(()=>null,e=>e);assert.equal(next.sent.filter(m=>m.command!=='snapshot').length,0);assert.equal(old.sent.filter(m=>m.command!=='snapshot').length,0);assert.equal(typeof (await pending)?.message,'string');assert.equal(f.client.snapshot().connected,true);f.client.dispose();assert.equal(f.timers.size,0);
});
await check('an old send throwing after rebind cannot close the new healthy broker',async()=>{
 const f=fixture(),old=f.connect(),send=old.send.bind(old);let next;old.send=raw=>{const m=JSON.parse(raw);if(m.command==='snapshot')return send(raw);next=rebind(f);throw Error('old transport send failed after replacement');};const pending=f.client.mutate('fan.setPreset',{preset:'soft'}).then(()=>null,e=>e);assert.equal(typeof (await pending)?.message,'string');assert.equal(next.closed,false);assert.equal(f.client.snapshot().connected,true);assert.equal(next.sent.filter(m=>m.command!=='snapshot').length,0);f.client.dispose();assert.equal(f.timers.size,0);
});
await check('a timeout observer rebind cannot send the old run snapshot request through a new broker',async()=>{
 const f=fixture();f.connect();let once=false,next;f.client.subscribe(state=>{if(!once&&state.notice.startsWith('结果未确认')){once=true;next=rebind(f);}});const pending=f.client.mutate('fan.setPreset',{preset:'soft'}).then(()=>null,e=>e);f.fireDelay(4000);assert.equal(typeof (await pending)?.message,'string');assert(next.sent.every(m=>m.runId==='run-2'));assert.equal(next.sent.length,1);assert.equal(f.client.snapshot().connected,true);f.client.dispose();assert.equal(f.timers.size,0);
});
await check('a timeout observer hiding the last page cannot restart paused passive snapshot demand',async()=>{
 const f=fixture();f.bootstrap.hudProtocol=1;f.client.setPassiveVisible(true);const socket=f.connect();let once=false;f.client.subscribe(state=>{if(!once&&state.notice.startsWith('结果未确认')){once=true;f.client.setVisible(false);}});const pending=f.client.mutate('fan.setPreset',{preset:'soft'}).then(()=>null,e=>e);f.fireDelay(4000);assert.equal(typeof (await pending)?.message,'string');assert.equal(socket.sent.at(-1).command,'snapshot');assert.equal(socket.sent.at(-1).subscribe,false);assert.equal(f.client.snapshot().connected,true);f.client.dispose();assert.equal(f.timers.size,0);
});
await check('hiding during busy admission cancels an unsent write without relabeling it as send failure',async()=>{
 const f=fixture(),socket=f.connect();let once=false;f.client.subscribe(state=>{if(state.busy&&!once){once=true;f.client.setVisible(false);}});const pending=f.client.mutate('fan.setPreset',{preset:'soft'}).then(()=>null,e=>e);assert.equal(typeof (await pending)?.message,'string');assert.equal(socket.sent.filter(m=>m.command!=='snapshot').length,0);assert.equal(f.client.snapshot().notice,'侧栏已隐藏');assert.equal(f.timers.size,0);f.client.dispose();
});

// The hidden-sidebar pause is also a write to the shared transport. A send
// exception must use the same recovery owner as HUD/wallpaper/updater messages.
for(const consumer of ['hud','wallpaper','updater'])await check('failed hidden pause recovers '+consumer+' transport without replaying an uncertain action',async()=>{
 const f=fixture();f.bootstrap[consumer+'Protocol']=1;f.client.setPassiveDemand(consumer,true);
 try{
  const socket=f.connect(),send=socket.send.bind(socket);
  const pending=f.client.mutate('fan.setEnabled',{enabled:true}).then(()=>null,error=>error);
  assert.equal(socket.sent.filter(x=>x.command==='fan.setEnabled').length,1);
  socket.send=value=>{const message=JSON.parse(value);if(message.command==='snapshot'&&message.subscribe===false)throw Error('Inert hidden pause send failed');send(value);};
  f.client.setVisible(false);assert.equal(typeof (await pending)?.message,'string');
  assert.equal(f.client.snapshot().connected,false);assert.equal(socket.closed,true);
  assert.equal(f.timers.size,1);f.fireDelay(250);
  const next=f.sockets.at(-1);assert.notEqual(next,socket);next.emit('open');
  assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
  assert.equal(next.sent.some(x=>x.command==='snapshot'||x.command==='fan.setEnabled'),false,'Hidden reconnect must not replay business intent or poll state');
  f.client.setVisible(true);assert.equal(next.sent.at(-1).command,'snapshot');assert.notEqual(next.sent.at(-1).subscribe,false);
  next.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot(2)});assert.equal(f.timers.size,0);
 }finally{f.client.dispose();}
 assert.equal(f.timers.size,0);
});
await check('hidden restored snapshot pause failure creates one recovery despite repeated stale packets',()=>{
 const f=fixture();f.bootstrap.hudProtocol=1;f.client.setPassiveVisible(true);
 try{
  const socket=f.sockets.at(-1);socket.emit('open');socket.send=()=>{throw Error('Inert restored pause send failed');};
  for(let i=0;i<1024;i++)socket.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot(i+1)});
  assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,1);assert.equal(f.sockets.length,1);
  f.fireDelay(250);f.sockets.at(-1).emit('open');assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
  assert.equal(f.sockets.at(-1).sent.length,0,'Background recovery must not request a business snapshot');
 }finally{f.client.dispose();}
 assert.equal(f.timers.size,0);
});

// Context liveness belongs to this wallpaper request, independently of sidebar
// snapshot/HUD messages and of whether a valid response contains a playable asset.
function contextFixture(){const f=fixture();f.bootstrap.wallpaperProtocol=1;f.bootstrap.hudProtocol=1;f.client.setPassiveDemand('wallpaper',true);const socket=f.sockets.at(-1);socket.emit('open');return {...f,socket,context:(seq=1,extra={})=>f.client.sendWallpaper('wallpaper.context',{contextSeq:seq,appId:'480',visible:true,...extra}),ack:(seq=1,type='wallpaper-ready',extra={})=>socket.emit('message',{type,runId:f.bootstrap.runId,contextSeq:seq,enabled:true,...extra})};}
await check('OPEN wallpaper context without response times out and recovers without replay',()=>{
 const f=contextFixture();try{assert.equal(f.context(),true);assert.equal(f.timers.size,1);f.fireDelay(10000);assert.equal(f.client.snapshot().connected,false);assert.equal(f.socket.closed,true);assert.equal(f.timers.size,1);f.fireDelay(250);const next=f.sockets.at(-1);next.emit('open');assert.equal(f.client.snapshot().connected,true);assert.equal(next.sent.length,0);assert.equal(f.timers.size,0);f.context(2);next.emit('message',{type:'wallpaper',runId:f.bootstrap.runId,contextSeq:2,appId:'480',enabled:true,entry:null});assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
for(const type of ['wallpaper','wallpaper-ready'])await check('correlated '+type+' response settles context transport even when disabled or empty',()=>{
 const f=contextFixture();try{f.context();assert.equal(f.timers.size,1);f.ack(1,type,{enabled:false,entry:null});assert.equal(f.timers.size,0);for(let i=0;i<1000;i++)f.client.sendWallpaper('wallpaper.status',{contextSeq:1,status:'observed'});assert.equal(f.timers.size,0);assert.equal(f.client.snapshot().connected,true);}finally{f.client.dispose();}
});
await check('context deadline is armed before synchronous socket response',()=>{
 const f=contextFixture();try{const send=f.socket.send.bind(f.socket);f.socket.send=value=>{send(value);f.ack(JSON.parse(value).contextSeq);};assert.equal(f.context(),true);assert.equal(f.timers.size,0);assert.equal(f.client.snapshot().connected,true);}finally{f.client.dispose();}
});
await check('HUD snapshot uncorrelated ready wrong seq app and malformed status cannot cancel context deadline',()=>{
 const f=contextFixture();try{f.client.setPassiveDemand('hud',true);f.context();const timer=[...f.timers.keys()][0];for(const message of [{type:'hud-ready'},{type:'hud',contextSeq:1},{type:'snapshot',snapshot:f.snapshot()},{type:'wallpaper-ready',enabled:true},{type:'wallpaper-ready',contextSeq:0,enabled:true},{type:'wallpaper-ready',contextSeq:2,enabled:true},{type:'wallpaper',contextSeq:'1',enabled:true},{type:'wallpaper',contextSeq:1,enabled:'true'},{type:'wallpaper',contextSeq:1,enabled:true,appId:'481'}]){f.socket.emit('message',{...message,runId:f.bootstrap.runId});assert.equal(f.timers.size,1);assert(f.timers.has(timer));}f.ack();assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('newest context owns reply while repeated demand never postpones unanswered deadline',()=>{
 const f=contextFixture();try{f.context();const timer=[...f.timers.keys()][0];for(let seq=2;seq<=1024;seq++)f.context(seq);assert.equal(f.timers.size,1);assert(f.timers.has(timer));f.ack(1);assert.equal(f.timers.size,1);f.ack(1023);assert.equal(f.timers.size,1);f.ack(1024);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('already queued expired context callback cannot revoke a later acknowledged generation',()=>{
 const f=contextFixture();try{f.context();const old=[...f.timers.values()][0].action;f.ack();f.context(2);old();assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,1);f.ack(1);assert.equal(f.timers.size,1);f.ack(2);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
for(const boundary of ['hide','wallpaper-off','close','bootstrap','dispose'])await check('context deadline cleanup at '+boundary+' ignores a late callback',()=>{
 const f=contextFixture();try{f.context();const old=[...f.timers.values()][0].action;let next;
  if(boundary==='hide')f.context(2,{visible:false});
  if(boundary==='wallpaper-off'){f.client.setPassiveDemand('hud',true);f.client.setPassiveDemand('wallpaper',false);}
  if(boundary==='close'){f.socket.close();f.fireDelay(250);next=f.sockets.at(-1);next.emit('open');}
  if(boundary==='bootstrap'){f.bootstrap.runId='run-2';f.bootstrap.token='NEW-TEST';f.client.bootstrapChanged();next=f.sockets.at(-1);next.emit('open');}
  if(boundary==='dispose')f.client.dispose();
  assert.equal(f.timers.size,0);const count=f.sockets.length;old();assert.equal(f.timers.size,0);assert.equal(f.sockets.length,count);assert.equal(f.client.snapshot().connected,boundary!=='dispose');
  if(boundary==='bootstrap'){f.context(2);f.socket.emit('message',{type:'wallpaper-ready',runId:'run-1',contextSeq:2,enabled:true});assert.equal(f.timers.size,1);next.emit('message',{type:'wallpaper-ready',runId:'run-2',contextSeq:2,enabled:true});assert.equal(f.timers.size,0);}
 }finally{f.client.dispose();}
});
await check('a stale hidden context cannot cancel a newer visible context deadline',()=>{
 const f=contextFixture();try{f.context(2);f.context(1,{visible:false});assert.equal(f.timers.size,1);f.ack(1);assert.equal(f.timers.size,1);f.ack(2);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('local context serialization failure does not arm a transport deadline',()=>{
 const f=contextFixture();try{const cyclic={};cyclic.self=cyclic;assert.equal(f.context(1,{cyclic}),false);assert.equal(f.timers.size,0);assert.equal(f.client.snapshot().connected,true);}finally{f.client.dispose();}
});
await check('context send exception clears its deadline and keeps only original recovery',()=>{
 const f=contextFixture();try{f.socket.send=()=>{throw Error('Inert context send failure');};assert.equal(f.context(),false);assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,1);assert.equal([...f.timers.values()][0].delay,250);}finally{f.client.dispose();}
});
await check('wallpaper timeout never replays an uncertain business write',async()=>{
 const f=contextFixture();try{f.client.setVisible(true);f.socket.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot()});f.context();const pending=f.client.mutate('fan.setEnabled',{enabled:true}).then(()=>null,error=>error);f.fireDelay(4000);assert.equal(typeof (await pending)?.message,'string');f.fireDelay(10000);f.fireDelay(250);const next=f.sockets.at(-1);next.emit('open');assert.equal(f.sockets.flatMap(s=>s.sent).filter(m=>m.command==='fan.setEnabled').length,1);assert.equal(next.sent.some(m=>m.command==='wallpaper.context'||m.command==='fan.setEnabled'),false);next.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot(2)});assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});

fs.writeFileSync(path.resolve(task,'validation/MIRROR-CLIENT-SELFTEST.json'),JSON.stringify({project:'YMCC Decky 侧边栏',cases:cases.length,results:cases,realSockets:false,hardware:false,productionClientSource:true},null,2));console.log(`Mirror client tests passed: ${cases.length}`);
