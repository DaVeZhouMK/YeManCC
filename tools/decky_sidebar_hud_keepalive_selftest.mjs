import {performanceModelModule} from './fixtures/decky_game_memory_store.mjs';
import { task, validation } from './fixtures/decky_task_paths.mjs';
// Exact production Decky transport and owned HUD child; all clocks, sockets and windows are inert.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json'));
const {transformSync,build}=require('esbuild');
function load(file,dependencies={}){
  const module={exports:{}};
  vm.runInNewContext(transformSync(fs.readFileSync(path.isAbsolute(file)?file:path.join(root,file),'utf8'),{loader:'ts',format:'cjs'}).code,
    {module,exports:module.exports,structuredClone,Date,require:name=>{if(name in dependencies)return dependencies[name];throw Error('Unexpected dependency '+name);}});
  return module.exports;
}
const context=load('decky-plugin/src/steamRunningContext.ts');
const mirrorFile=process.argv.includes('--baseline')?path.join(validation,'preimage/decky-plugin/src/mirrorClient.ts'):'decky-plugin/src/mirrorClient.ts';
const {MirrorClient}=load(mirrorFile,{'./steamRunningContext':context,'./performanceModel':performanceModelModule});
const b=await build({entryPoints:[path.join(root,'decky-plugin/src/steamHudNativeCarrier.ts')],bundle:true,write:false,platform:'node',format:'cjs'});
const nativePath=path.join(task,'Build/hud-owned-child-under-test.cjs');fs.writeFileSync(nativePath,b.outputFiles[0].contents);
const {ownNativeChild}=require(nativePath);
const results=[];
async function check(name,action){try{await action();results.push({name,status:'passed'});}catch(e){results.push({name,status:'failed',error:e.message});}}
function fixture({protocol=1,available=true,failConstructors=0}={}){
  let now=100000,next=0,failures=failConstructors,attempts=0,present=available;
  const sockets=[],timers=new Map(),boot={endpoint:'ws://127.0.0.1:23456/mirror',token:'TEST-ONLY',runId:'hud-test-run',hudProtocol:protocol};
  class Socket{
    listeners=new Map();sent=[];closed=false;
    addEventListener(name,fn){this.listeners.set(name,fn);}
    send(raw){assert.equal(this.closed,false);this.sent.push(JSON.parse(raw));}
    close(){if(this.closed)return;this.closed=true;this.listeners.get('close')?.();}
    emit(name,value){this.listeners.get(name)?.({data:typeof value==='string'?value:JSON.stringify(value)});}
  }
  const client=new MirrorClient({bootstrap:()=>present?boot:undefined,now:()=>now,clientId:'hud-inert-client',
    socket:()=>{attempts++;if(failures-->0)throw Error('Inert constructor unavailable');const socket=new Socket();sockets.push(socket);return socket;},
    schedule:(action,delay)=>{const id=++next;timers.set(id,{action,delay});return id;},cancel:id=>timers.delete(id)});
  const fire=(delay)=>{const pair=[...timers].find(([,timer])=>timer.delay===delay);assert.ok(pair,`No timer ${delay}`);timers.delete(pair[0]);now+=delay;pair[1].action();};
  const onlyDelay=()=>{assert.equal(timers.size,1);return [...timers.values()][0].delay;};
  const snapshot=revision=>({runId:boot.runId,generation:1,revision,ready:true,game:null,fan:{supported:false,enabled:false,preset:'balanced',choices:[]}});
  const open=()=>sockets.at(-1).emit('open');
  const snap=(revision=1)=>sockets.at(-1).emit('message',{type:'snapshot',runId:boot.runId,snapshot:snapshot(revision)});
  return {client,boot,sockets,timers,fire,onlyDelay,snapshot,open,snap,advance:ms=>{now+=ms;},available:v=>{present=v;},attempts:()=>attempts};
}
const retryDelays=[250,1000,3000,10000,30000];
await check('Healthy HUD socket survives two idle hours with zero transport timers/business reads',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);f.open();f.advance(7200000);assert.equal(f.client.snapshot().connected,true);assert.equal(f.sockets.length,1);assert.equal(f.sockets[0].closed,false);assert.equal(f.timers.size,0);assert.equal(f.sockets[0].sent.length,0);}finally{f.client.dispose();}
});
await check('Hiding the sidebar preserves healthy HUD transport and pauses business demand',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);f.open();f.client.setVisible(true);f.snap();f.client.setVisible(false);f.advance(7200000);assert.equal(f.sockets[0].closed,false);assert.equal(f.client.snapshot().snapshot,null);assert.equal(f.sockets[0].sent.at(-1).subscribe,false);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('HUD keeps reconnecting beyond three failures, capped at 30 seconds, then recovers while hidden',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);for(let i=0;i<24;i++){f.sockets.at(-1).close();const delay=retryDelays[Math.min(i,4)];assert.equal(f.onlyDelay(),delay);f.fire(delay);}f.open();assert.equal(f.client.snapshot().connected,true);assert.equal(f.client.sendHud('hud.status',{capable:true}),true);assert.equal(f.sockets.at(-1).sent.some(p=>p.command==='snapshot'),false);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('Synchronous socket constructor failures also retry and recover without a sidebar reopen',()=>{
  const f=fixture({failConstructors:7});try{f.client.setPassiveVisible(true);for(let i=0;i<7;i++){const delay=retryDelays[Math.min(i,4)];assert.equal(f.onlyDelay(),delay);f.fire(delay);}f.open();assert.equal(f.attempts(),8);assert.equal(f.sockets.length,1);assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('Connections that open then immediately close cannot restart a fast infinite retry loop',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);for(let i=0;i<18;i++){f.open();f.sockets.at(-1).close();const delay=retryDelays[Math.min(i,4)];assert.equal(f.onlyDelay(),delay);f.fire(delay);}}finally{f.client.dispose();}
});
await check('Repeated ready/snapshot packets on brief connections do not defeat HUD reconnect backoff',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);f.client.setVisible(true);for(let i=0;i<7;i++){f.open();f.sockets.at(-1).emit('message',{type:'hud-ready',runId:f.boot.runId,protocol:1});f.snap();f.sockets.at(-1).close();const delay=retryDelays[Math.min(i,4)];assert.equal(f.onlyDelay(),delay);f.fire(delay);}}finally{f.client.dispose();}
});
await check('A stable recovered connection resets backoff without creating a healthy-state timer',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);for(const delay of [250,1000,3000,10000]){f.sockets.at(-1).close();f.fire(delay);}f.open();assert.equal(f.timers.size,0);f.advance(30000);f.sockets.at(-1).close();assert.equal(f.onlyDelay(),250);}finally{f.client.dispose();}
});
await check('HUD and sidebar share readiness even while the business snapshot is delayed',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);f.client.setVisible(true);f.open();assert.equal(f.client.snapshot().connected,true);assert.equal(f.client.snapshot().snapshot,null);assert.equal(f.client.sendHud('hud.status',{capable:true}),true);assert.equal(f.onlyDelay(),15000);f.snap();assert.equal(f.timers.size,0);assert.equal(f.sockets.length,1);}finally{f.client.dispose();}
});
await check('A connection that never opens still times out, then remains eligible for HUD recovery',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);for(let i=0;i<8;i++){f.fire(10000);assert.equal(f.sockets.at(-1).closed,true);const delay=retryDelays[Math.min(i,4)];assert.equal(f.onlyDelay(),delay);f.fire(delay);}}finally{f.client.dispose();}
});
await check('Adding HUD demand joins existing sidebar recovery without duplicating its timer',()=>{
  const f=fixture();try{f.client.setVisible(true);for(const delay of [250,1000,3000]){f.sockets.at(-1).close();f.fire(delay);}f.sockets.at(-1).close();assert.equal(f.timers.size,1);f.client.setPassiveVisible(true);assert.equal(f.sockets.length,4);assert.equal(f.timers.size,1);f.fire(10000);assert.equal(f.sockets.length,5);f.open();assert.equal(f.client.sendHud('hud.status',{}),true);}finally{f.client.dispose();}
});
await check('Opening the sidebar during background recovery consumes the old retry timer once',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);f.open();f.sockets[0].close();const old=[...f.timers.values()][0].action;f.client.setVisible(true);f.open();f.snap();assert.equal(f.timers.size,0);const count=f.sockets.length;old();assert.equal(f.sockets.length,count);assert.equal(f.client.snapshot().connected,true);}finally{f.client.dispose();}
});
await check('A stale retry callback cannot erase the handle of a newer queued recovery',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);f.sockets[0].close();const old=[...f.timers.values()][0].action;f.client.setVisible(true);f.sockets[1].close();assert.equal(f.timers.size,1);old();assert.equal(f.timers.size,1);f.client.dispose();assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('A stale connection deadline cannot erase the handle of a replacement socket deadline',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);const old=[...f.timers.values()][0].action;f.boot.token='REPLACEMENT-TEST-ONLY';f.client.bootstrapChanged();assert.equal(f.sockets.length,2);assert.equal(f.timers.size,1);old();assert.equal(f.sockets[1].closed,false);f.client.dispose();assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('Removing the last HUD consumer cancels retry/deadline and ignores an already queued callback',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);f.sockets[0].close();const callback=[...f.timers.values()][0].action;f.client.setPassiveVisible(false);assert.equal(f.timers.size,0);callback();assert.equal(f.sockets.length,1);assert.equal(f.client.snapshot().connected,false);}finally{f.client.dispose();}
});
await check('Plugin unload cancels capped recovery and never revives its connection',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);for(const delay of [250,1000,3000,10000]){f.sockets.at(-1).close();f.fire(delay);}f.sockets.at(-1).close();const callback=[...f.timers.values()][0].action;f.client.dispose();assert.equal(f.timers.size,0);const count=f.sockets.length;callback();f.client.bootstrapChanged();f.client.setPassiveVisible(true);assert.equal(f.sockets.length,count);}finally{f.client.dispose();}
});
await check('Changing authenticated bootstrap cancels old recovery and rejects stale old-run HUD packets',()=>{
  const f=fixture(),packets=[];try{f.client.subscribeExtra(value=>packets.push(value));f.client.setPassiveVisible(true);f.open();const old=f.sockets[0];old.close();const callback=[...f.timers.values()][0].action;f.boot.runId='hud-new-run';f.boot.token='NEW-TEST-ONLY';f.client.bootstrapChanged();assert.equal(f.sockets.length,2);f.open();callback();old.emit('message',{type:'hud',runId:'hud-test-run',seq:100});assert.equal(packets.length,0);assert.equal(f.sockets.length,2);assert.equal(f.timers.size,0);assert.equal(f.client.sendHud('hud.status',{}),true);assert.equal(f.sockets.at(-1).sent.at(-1).runId,'hud-new-run');}finally{f.client.dispose();}
});
await check('Missing bootstrap starts no discovery; an existing session can recover a temporarily withdrawn binding',()=>{
  const f=fixture({available:false});try{f.client.setPassiveVisible(true);assert.equal(f.attempts(),0);assert.equal(f.timers.size,0);f.available(true);f.client.bootstrapChanged();f.open();f.available(false);f.client.bootstrapChanged();assert.equal(f.sockets[0].closed,true);assert.equal(f.timers.size,1);assert.equal(f.attempts(),1);f.fire(250);assert.equal(f.attempts(),1);f.available(true);f.fire(1000);f.open();assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await check('Reconnect never replays a possibly applied business command',async()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);f.client.setVisible(true);f.open();const snapshot=f.snapshot(1);snapshot.fan.supported=true;f.sockets[0].emit('message',{type:'snapshot',runId:f.boot.runId,snapshot});const pending=f.client.mutate('fan.setEnabled',{enabled:true});const rejected=assert.rejects(pending,/中断/);f.sockets[0].close();await rejected;f.fire(250);f.open();assert.equal(f.sockets.flatMap(s=>s.sent).filter(p=>p.command==='fan.setEnabled').length,1);assert.equal(f.sockets[1].sent.some(p=>p.command==='fan.setEnabled'),false);}finally{f.client.dispose();}
});
await check('Ordinary sidebar and older HUD transport also recover after long outages',()=>{
  for(const protocol of [0,1]){const f=fixture({protocol});try{if(protocol===0)f.client.setPassiveVisible(true);f.client.setVisible(true);for(const delay of [250,1000,3000,10000,30000]){f.sockets.at(-1).close();f.fire(delay);}f.open();f.snap();assert.equal(f.sockets.length,6);assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);}finally{f.client.dispose();}}
});
await check('Invalid packets stop the current session rather than being accepted by the keepalive path',()=>{
  const f=fixture();try{f.client.setPassiveVisible(true);f.open();f.sockets[0].emit('message',{type:'hud',runId:'wrong-run'});assert.equal(f.sockets[0].closed,true);assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
function eventTarget(){const events=new Map();return {events,addEventListener:(name,fn)=>{if(!events.has(name))events.set(name,new Set());events.get(name).add(fn);},removeEventListener:(name,fn)=>events.get(name)?.delete(fn),emit:name=>{for(const fn of [...events.get(name)??[]])fn();}};}
function childFixture(){
  let now=100000,next=0;const timers=new Map(),invalid=[],calls=[];
  const style=()=>({setProperty:()=>{}}),owner={...eventTarget(),document:{...eventTarget(),hidden:true}};
  const child={...eventTarget(),closed:false,setTimeout:(action,delay)=>{const id=++next;timers.set(id,{action,delay});return id;},clearTimeout:id=>timers.delete(id)};
  const element={style:style(),ownerDocument:{defaultView:child}};
  child.document={title:'YMCC-hud-idle-test',documentElement:{style:style()},body:{style:style()},hidden:false};
  child.SteamClient={Window:{ShowWindow:()=>calls.push('show'),SetKeyFocus:()=>{},Close:()=>{calls.push('close');child.closed=true;}},Overlay:{SetWindowAlpha:()=>{},SetWindowPinned:()=>{}}};
  const plan={owner,name:child.document.title,pin:true,expiresAt:now+6000,binding:{},params:{eCreationFlags:0}};
  let ownerValid=true;
  const lease=ownNativeChild({child,element,plan,checkOwner:()=>ownerValid,onInvalid:reason=>invalid.push(reason),trace:()=>{},now:()=>now});
  const advance=ms=>{now+=ms;for(const [id,timer]of [...timers])if(now>=plan.expiresAt){timers.delete(id);timer.action();}};
  return {owner,child,timers,invalid,calls,lease,advance,now:()=>now,invalidate:()=>{ownerValid=false;}};
}
await check('Native HUD keeps displaying for two simulated idle hours while its owner document is hidden',()=>{
  const f=childFixture();try{for(let i=0;i<7200;i++){f.advance(1000);assert.equal(f.lease.renew(f.now()+6000),true);}f.owner.document.emit('visibilitychange');assert.equal(f.child.closed,false);assert.equal(f.timers.size,1);assert.equal(f.invalid.length,0);assert.equal(f.calls.filter(c=>c==='show').length,1);assert.equal(f.calls.includes('close'),false);}finally{f.lease.release('test-complete',true);}
});
await check('Explicit close and actual game-owner loss still release the owned HUD child',()=>{
  const first=childFixture();first.lease.release('explicit-hide',true);assert.equal(first.child.closed,true);assert.equal(first.timers.size,0);
  const second=childFixture();second.invalidate();second.owner.document.emit('visibilitychange');assert.equal(second.child.closed,true);assert.equal(second.invalid[0],'game-owner-changed');assert.equal(second.timers.size,0);
});
await check('A genuinely stopped data source still expires stale HUD data and cleans up its child',()=>{
  const f=childFixture();f.advance(6001);assert.equal(f.child.closed,true);assert.equal(f.invalid[0],'child-deadline');assert.equal(f.timers.size,0);
});
const failed=results.filter(r=>r.status==='failed');
const report={passed:results.length-failed.length,failed:failed.length,cases:results,scope:'Production Decky client/native child with inert socket/window/clock. Two hours are simulated; no actual Steam/minimized main-WebView/hardware acceptance.'};
fs.writeFileSync(path.join(validation,process.argv.includes('--baseline')?'HUD-KEEPALIVE-BASELINE.json':'HUD-KEEPALIVE-FIXED.json'),JSON.stringify(report,null,2));
console.log(JSON.stringify({passed:report.passed,failed:report.failed,failures:failed},null,2));
if(failed.length&&!process.argv.includes('--baseline'))process.exitCode=1;
