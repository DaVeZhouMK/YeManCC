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
vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'decky-plugin/src/mirrorClient.ts'),'utf8'), { loader:'ts',format:'cjs' }).code,
  { module,exports:module.exports,structuredClone,require: name => { if(name==='./steamRunningContext')return contextModule.exports; throw new Error('Unexpected external dependency'); } });
const { MirrorClient } = module.exports;
const cases=[];
async function check(name,action) { await action(); cases.push({name,status:'passed'}); }
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
await check('failed reconnects stop after 3 delays',()=>{const f=fixture();f.client.setVisible(true);for(const delay of [250,1000,3000]){f.sockets.at(-1).close();f.fireDelay(delay);}f.sockets.at(-1).close();assert.equal(f.sockets.length,4);assert.equal(f.timers.size,0);f.client.dispose();});
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
fs.writeFileSync(path.resolve(task,'validation/MIRROR-CLIENT-SELFTEST.json'),JSON.stringify({project:'YMCC Decky 侧边栏',cases:cases.length,results:cases,realSockets:false,hardware:false,productionClientSource:true},null,2));console.log(`Mirror client tests passed: ${cases.length}`);
