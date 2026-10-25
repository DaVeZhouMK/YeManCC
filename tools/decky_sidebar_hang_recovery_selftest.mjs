import {performanceModelModule} from './fixtures/decky_game_memory_store.mjs';
// Production mirror client; inert sockets, virtual clocks and no real processes.
import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import {createRequire} from 'node:module';
import {root,validation} from './fixtures/decky_task_paths.mjs';
const {transformSync}=createRequire(path.join(root,'package.json'))('esbuild');
const contextModule={exports:{}};
vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module:contextModule,exports:contextModule.exports});
const module={exports:{}};
vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'decky-plugin/src/mirrorClient.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,require:name=>{if(name==='./performanceModel')return performanceModelModule;assert.equal(name,'./steamRunningContext');return contextModule.exports;}});
const {MirrorClient}=module.exports;const cases=[];
function fixture(){
 const timers=new Map(),sockets=[];let now=0,next=0;
 let bootstrap={endpoint:'ws://127.0.0.1:23456/mirror',token:'inert-token',runId:'session1',hudProtocol:1};
 class Socket{
  listeners=new Map();sent=[];closed=false;
  constructor(){sockets.push(this);}
  addEventListener(event,fn){const list=this.listeners.get(event)||[];list.push(fn);this.listeners.set(event,list);}
  emit(event,value){for(const fn of this.listeners.get(event)||[])fn({data:typeof value==='string'?value:JSON.stringify(value)});}
  send(value){if(this.throwSend)throw Error('inert send failure');if(this.closed)throw Error('closed inert socket');this.sent.push(JSON.parse(value));}
  close(){if(this.throwClose)throw Error('inert close failure');this.closed=true;this.emit('close');}
 }
 const client=new MirrorClient({bootstrap:()=>bootstrap,socket:()=>new Socket(),schedule:(action,delay)=>{const id=++next;timers.set(id,{action,delay,due:now+delay});return id;},cancel:id=>timers.delete(id),clientId:'fault-fixture',now:()=>now});
 const snap=(revision=1)=>({runId:bootstrap.runId,generation:1,revision,ready:true,provenance:{kind:'ymcc-native',pid:123},game:{label:'inert-game',identity:'inert:pid1',fields:{acMode:{value:'balanced',supported:true,choices:[{data:'balanced',label:'Balanced'}]}}},fan:{supported:true,enabled:true,preset:'soft',choices:[{data:'soft',label:'Soft'}]}});
 const publish=(socket=sockets.at(-1),revision=1)=>socket.emit('message',{type:'snapshot',runId:bootstrap.runId,snapshot:snap(revision)});
 const connect=()=>{client.setVisible(true);sockets.at(-1).emit('open');publish();assert.equal(client.snapshot().connected,true);};
 const advance=ms=>{const end=now+ms;let steps=0;for(;;){let found=null;for(const item of timers)if(item[1].due<=end&&(!found||item[1].due<found[1].due))found=item;if(!found)break;if(++steps>500)throw Error('retry storm');now=found[1].due;timers.delete(found[0]);found[1].action();}now=end;};
 const late=ms=>{now+=ms;const ready=[...timers].filter(([,t])=>t.due<=now);for(const[id,t]of ready){if(!timers.has(id))continue;timers.delete(id);t.action();}};
 return{client,sockets,timers,snap,publish,connect,advance,late,setBootstrap:value=>{bootstrap=value;},bootstrap:()=>bootstrap};
}
async function check(name,fn){await fn();cases.push(name);}
await check('an OPEN HUD socket with a silent business backend times out its requested snapshot and reconnects',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);const old=f.sockets[0];old.emit('open');assert.equal(f.client.snapshot().connected,true);
 f.advance(15000);assert.equal(old.closed,true);assert.equal(f.client.snapshot().connected,false);f.advance(250);assert.equal(f.sockets.length,2);f.sockets[1].emit('open');f.publish();assert.equal(f.client.snapshot().snapshot.ready,true);assert.equal(f.timers.size,0);f.client.dispose();
});
await check('an unconfirmed mutation is never replayed when a silent backend later recovers',async()=>{
 const f=fixture();f.connect();const pending=f.client.mutate('fan.setPreset',{preset:'soft'});const rejected=assert.rejects(pending);f.advance(4000);await rejected;f.advance(15000);assert.equal(f.sockets[0].closed,true);
 f.advance(250);f.sockets[1].emit('open');f.publish();assert.equal(f.client.snapshot().busy,false);assert.equal(f.sockets.flatMap(s=>s.sent).filter(m=>m.command==='fan.setPreset').length,1);assert.equal(f.timers.size,0);f.client.dispose();
});
await check('a frozen renderer which later delivers a valid snapshot clears the deadline without replay',()=>{
 const f=fixture();f.connect();f.client.setVisible(false);f.client.setVisible(true);f.publish();f.late(120000);assert.equal(f.sockets.length,2);assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);f.client.dispose();
});
await check('healthy visible and passive states add no periodic liveness traffic',()=>{
 const f=fixture();f.connect();f.client.setPassiveVisible(true);const sent=f.sockets[0].sent.length;f.advance(3600000);assert.equal(f.sockets[0].sent.length,sent);assert.equal(f.timers.size,0);f.client.setVisible(false);const hidden=f.sockets[0].sent.length;f.advance(3600000);assert.equal(f.sockets[0].sent.length,hidden);assert.equal(f.timers.size,0);f.client.dispose();
});
await check('hide and dispose cancel outstanding state deadlines without closing healthy HUD on hidden demand',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);f.sockets[0].emit('open');f.client.setVisible(false);assert.equal(f.timers.size,0);f.advance(60000);assert.equal(f.sockets.length,1);assert.equal(f.sockets[0].closed,false);f.client.dispose();assert.equal(f.timers.size,0);
});
await check('stale packets and timeout callbacks cannot damage a renewed mirror session',()=>{
 const f=fixture();f.connect();const old=f.sockets[0];f.setBootstrap({...f.bootstrap(),token:'new-inert-token',runId:'session2'});f.client.bootstrapChanged();f.sockets[1].emit('open');f.publish();old.emit('message',{type:'snapshot',runId:'session1',snapshot:{...f.snap(),runId:'session1'}});f.advance(60000);assert.equal(f.client.snapshot().snapshot.runId,'session2');assert.equal(f.sockets.length,2);assert.equal(f.timers.size,0);f.client.dispose();
});
await check('repeated HUD-ready signals cannot postpone an existing missing-snapshot deadline',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);f.sockets[0].emit('open');f.advance(14000);for(let i=0;i<100;i++)f.sockets[0].emit('message',{type:'hud-ready',runId:'session1'});assert.equal(f.timers.size,1);f.advance(1000);assert.equal(f.sockets[0].closed,true);assert.equal(f.timers.size,1);f.client.dispose();
});
await check('a stale dispatched deadline cannot clear the next session deadline handle',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);f.sockets[0].emit('open');const stale=[...f.timers.values()][0].action;f.setBootstrap({...f.bootstrap(),token:'next-token',runId:'session2'});f.client.bootstrapChanged();f.sockets[1].emit('open');stale();f.publish();assert.equal(f.timers.size,0);f.client.dispose();
});
await check('throwing close does not block deadline cleanup or recovery scheduling',()=>{
 const f=fixture();f.client.setPassiveVisible(true);f.client.setVisible(true);f.sockets[0].emit('open');f.sockets[0].throwClose=true;assert.doesNotThrow(()=>f.advance(15000));assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,1);f.advance(250);f.sockets[1].emit('open');f.publish();assert.equal(f.timers.size,0);f.client.dispose();
});
await check('mutation send exceptions reconnect without replaying the unconfirmed write',async()=>{
 const f=fixture();f.connect();f.sockets[0].throwSend=true;await assert.rejects(f.client.mutate('fan.setPreset',{preset:'soft'}));assert.equal(f.client.snapshot().connected,false);assert.equal(f.client.snapshot().busy,false);f.advance(250);f.sockets[1].emit('open');f.publish();assert.equal(f.sockets[1].sent.filter(r=>r.command==='fan.setPreset').length,0);assert.equal(f.timers.size,0);f.client.dispose();
});
fs.writeFileSync(path.join(validation,'MIRROR-HANG-RECOVERY.json'),JSON.stringify({status:'passed',cases:cases.length,results:cases,realProcesses:false,realSettings:false,clock:'virtual'},null,2)+'\n');console.log(JSON.stringify({status:'passed',mirrorHangCases:cases.length}));
