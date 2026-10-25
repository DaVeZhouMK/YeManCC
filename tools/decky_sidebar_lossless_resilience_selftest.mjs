import {task} from './fixtures/decky_task_paths.mjs';
import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import {createRequire} from 'node:module';
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json')),{transformSync}=require('esbuild');
const performanceModel=(()=>{const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'decky-plugin/src/performanceModel.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,console});return module.exports;})();const cases=[];async function check(name,fn){await fn();cases.push(name);}const flush=async()=>{for(let i=0;i<40;i++)await Promise.resolve();};
function deferred(){let resolve,reject;const promise=new Promise((y,n)=>{resolve=y;reject=n});return {promise,resolve,reject};}
function clock(){let next=0,now=0;const timers=new Map();return {timers,Date:class extends Date{static now(){return now;}},setTimeout(fn,delay){const id=++next;timers.set(id,{fn,delay});return id;},clearTimeout(id){timers.delete(id);},fire(delay){const found=[...timers].find(([,t])=>t.delay===delay);assert.ok(found,'missing timer '+delay);timers.delete(found[0]);now+=delay;found[1].fn();}};}
function load(rel,globals={},modules={}){const module={exports:{}};vm.runInNewContext(transformSync(fs.readFileSync(rel==='src/bridge/losslessScaling.ts'&&process.env.YMCC_DECKY_LOSSLESS_SOURCE_OVERRIDE?process.env.YMCC_DECKY_LOSSLESS_SOURCE_OVERRIDE:path.join(root,rel),'utf8'),{loader:'ts',format:'cjs'}).code,{module,exports:module.exports,structuredClone,queueMicrotask,...globals,require:name=>{if(name in modules)return modules[name];if(name==='./performanceModel'||name==='../../decky-plugin/src/performanceModel')return performanceModel;if(name.endsWith('steamRunningContext'))return load('decky-plugin/src/steamRunningContext.ts');throw Error('Unexpected import '+name);}});return module.exports;}
function observation(){const c=clock(),api=load('src/bridge/deckyLosslessState.ts',c),events=[];let changes=0;return {c,api,events,cache:new api.DeckyLosslessObservation(()=>changes++,(event,detail)=>events.push({event,...detail})),changes:()=>changes};}
await check('slow LS read never delays the synchronous sidebar view',async()=>{const f=observation(),d=deferred();const view=f.cache.view('game1',()=>d.promise);assert.equal(view.pending,true);assert.equal(view.document,null);assert.equal(f.changes(),0);d.resolve({enabled:true});await flush();assert.equal(f.cache.view('game1',()=>{throw Error('repeat read')}).document.enabled,true);f.cache.dispose();});
await check('unrelated snapshots reuse confirmed LS state without new process reads',async()=>{const f=observation();let reads=0;f.cache.view('game1',async()=>{reads++;return {enabled:false}});await flush();for(let i=0;i<100;i++)f.cache.view('game1',async()=>{reads++;return {enabled:true}});await flush();assert.equal(reads,1);assert.equal(f.c.timers.size,0);f.cache.dispose();});
await check('no response produces six-second read timeout without false ON/OFF',async()=>{const f=observation(),d=deferred();f.cache.view('game1',()=>d.promise);await flush();f.c.fire(6000);const view=f.cache.view('game1',()=>d.promise);assert.equal(view.pending,false);assert.equal(view.document,null);assert.match(view.notice,/超时/);assert.equal(f.changes(),1);f.cache.dispose();});
await check('expired read permits one explicit recovery and bounds all unresolved reads',async()=>{
 const f=observation(),a=deferred(),b=deferred();let reads=0;
 const read=()=>{reads++;return reads===1?a.promise:b.promise;};
 f.cache.view('game1',read);await flush();f.c.fire(6000);
 for(let i=0;i<50;i++)f.cache.view('game1',read);await flush();assert.equal(reads,1,'snapshots never retry');
 f.cache.demand();assert.equal(f.cache.view('game1',read).pending,true);await flush();assert.equal(reads,2);
 f.c.fire(6000);for(let i=0;i<20;i++){f.cache.demand();f.cache.view('game1',read);}await flush();
 assert.equal(reads,2,'two hung IO calls are a hard bound');assert.equal(f.c.timers.size,0);
 a.resolve({enabled:true});b.resolve({enabled:true});await flush();assert.equal(f.cache.view('game1',async()=>({enabled:false})).document,null);
 await flush();assert.equal(f.cache.view('game1',read).document.enabled,false);f.cache.dispose();
});
await check('same-session late result after timeout cannot publish success',async()=>{const f=observation(),d=deferred();f.cache.view('game1',()=>d.promise);await flush();f.c.fire(6000);d.resolve({enabled:true});await flush();const v=f.cache.view('game1',()=>d.promise);assert.equal(v.document,null);assert.match(v.notice,/超时/);assert.equal(f.events.at(-1).stage,'late');assert.equal(f.events.at(-1).ok,false);f.cache.dispose();});
await check('old game late result cannot enable new game',async()=>{const f=observation(),a=deferred(),b=deferred();f.cache.view('game1',()=>a.promise);await flush();f.cache.view('game2',()=>b.promise);a.resolve({enabled:true});await flush();assert.equal(f.cache.view('game2',()=>b.promise).document,null);b.resolve({enabled:false});await flush();assert.equal(f.cache.view('game2',()=>b.promise).document.enabled,false);f.cache.dispose();});
await check('source change invalidates in-flight result and reads new source once',async()=>{const f=observation(),d=deferred();f.cache.view('game1',()=>d.promise);await flush();f.cache.refresh();d.resolve({enabled:true});await flush();const v=f.cache.view('game1',async()=>({enabled:false}));assert.equal(v.document,null);await flush();assert.equal(f.cache.view('game1',()=>d.promise).document.enabled,false);f.cache.dispose();});
await check('manual refresh preserves display while a new read is pending',async()=>{const f=observation(),d=deferred();f.cache.view('game1',async()=>({enabled:false}));await flush();f.cache.demand();const pending=f.cache.view('game1',()=>d.promise);assert.equal(pending.document.enabled,false);assert.equal(pending.pending,true);d.resolve({enabled:true});await flush();assert.equal(f.cache.view('game1',()=>d.promise).document.enabled,true);f.cache.dispose();});
await check('unload cancels read deadline and discards late completion',async()=>{const f=observation(),d=deferred();f.cache.view('game1',()=>d.promise);await flush();f.cache.dispose();assert.equal(f.c.timers.size,0);d.resolve({enabled:true});await flush();assert.equal(f.changes(),0);});
await check('read failure affects only LS observation and creates no idle timer',async()=>{const f=observation();f.cache.view('game1',async()=>{throw Error('no UI reply')});await flush();const v=f.cache.view('game1',()=>{throw Error('poll')});assert.equal(v.pending,false);assert.equal(v.document,null);assert.match(v.notice,/暂不可读/);assert.equal(f.c.timers.size,0);f.cache.dispose();});
await check('ordinary owner result preserves actual confirmation and clears timer',async()=>{const f=observation();let calls=0;const v=await f.api.waitForDeckyLosslessAction(async cp=>{cp();calls++;return {confirmed:false}},()=>{});assert.equal(v.pending,false);assert.equal(v.result.confirmed,false);assert.equal(calls,1);assert.equal(f.c.timers.size,0);});
await check('40-second recovery confirmation deadline revokes later steps without replay',async()=>{const f=observation(),d=deferred();let calls=0,later=0,releases=0;const work=f.api.waitForDeckyLosslessAction(async cp=>{try{cp();calls++;await d.promise;cp();later++;return true;}finally{releases++;}},()=>{},(event,detail)=>f.events.push({event,...detail}));await flush();f.c.fire(40000);const reply=await work;assert.equal(reply.pending,true);assert.equal(calls,1);assert.equal(releases,0,'lock belongs to unsettled owner');d.resolve();await flush();assert.equal(later,0);assert.equal(releases,1);assert.equal(f.events.at(-1).stage,'late');});
await check('failed owner is not retried and propagates failure once',async()=>{const f=observation();let calls=0;await assert.rejects(f.api.waitForDeckyLosslessAction(async()=>{calls++;throw Error('OWNER_FAILED')},()=>{}),/OWNER_FAILED/);assert.equal(calls,1);assert.equal(f.c.timers.size,0);});
await check('lossless presentation renames existing owner text without changing owner',()=>{const f=observation();assert.equal(f.api.deckyLosslessText('小黄鸭控件回读；小黄鸭未确认'),'LosslessScaling控件回读；LosslessScaling未确认');});
const runId='a'.repeat(32),peer={connection:'1',runId},snap=()=>({generation:1,ready:true,game:{label:'test',identity:'test:1',fields:{}},fan:{supported:true,enabled:false,preset:'balanced',choices:[]},actions:{fan:true,game:true}});
function relayFixture(){const c=clock(),{ReadOnlyMirrorRelay}=load('src/bridge/deckyMirrorRelay.ts',c),replies=[];let dirty,changing=false,reads=0;const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;if(changing)dirty();return snap()},observe:fn=>{dirty=fn;return()=>{}},reply:async(_,message)=>{replies.push(message);return true},schedule:c.setTimeout,cancel:c.clearTimeout});return {c,relay,replies,open:()=>relay.request({...peer,request:{command:'snapshot',runId}}),changing:v=>changing=v,dirty:()=>dirty(),reads:()=>reads};}
await check('three unstable reads preserve old fields disabled and recover automatically',async()=>{const f=relayFixture();await f.open();f.changing(true);f.dirty();await flush();let latest=f.replies.at(-1).snapshot;assert.equal(latest.ready,false);assert.equal(latest.game.label,'test');assert.match(latest.notice,/自动恢复/);f.changing(false);f.c.fire(250);await flush();assert.equal(f.replies.at(-1).snapshot.ready,true);assert.equal(f.c.timers.size,0);f.relay.dispose();});
await check('continuous source churn has two recovery deadlines and no infinite poll',async()=>{const f=relayFixture();await f.open();f.changing(true);f.dirty();await flush();f.c.fire(250);await flush();f.c.fire(1000);await flush();assert.equal(f.c.timers.size,0);assert.equal(f.replies.at(-1).snapshot.ready,false);f.relay.dispose();});
await check('hiding sidebar cancels scheduled read recovery',async()=>{const f=relayFixture();await f.open();f.changing(true);f.dirty();await flush();await f.relay.request({...peer,request:{command:'snapshot',runId,subscribe:false}});assert.equal(f.c.timers.size,0);f.relay.dispose();});
function clientFixture(){const c=clock(),{MirrorClient}=load('decky-plugin/src/mirrorClient.ts'),sockets=[];class Socket{listeners=new Map();sent=[];constructor(){sockets.push(this)}addEventListener(e,fn){this.listeners.set(e,fn)}send(x){this.sent.push(JSON.parse(x))}close(){}emit(e,value){this.listeners.get(e)?.({data:JSON.stringify(value)})}}const client=new MirrorClient({bootstrap:()=>({endpoint:'ws://127.0.0.1:1234/mirror',token:'TEST-ONLY',runId}),socket:()=>new Socket(),schedule:c.setTimeout,cancel:c.clearTimeout,clientId:'test'});client.setVisible(true);const socket=sockets[0];socket.emit('open',{});const snapshot={...snap(),runId,revision:1};snapshot.game.fields.losslessScaling={value:'off',supported:true,choices:[{data:'off',label:'off'},{data:'on',label:'on'}]};socket.emit('message',{type:'snapshot',runId,snapshot});return {c,client,socket,snapshot};}
await check('soft 15-second reply keeps bounded owner alive for one recovery',async()=>{const f=observation(),d=deferred();let later=0,releases=0;const work=f.api.waitForDeckyLosslessAction(async cp=>{try{cp();await d.promise;cp();later++;return true;}finally{releases++;}},()=>{},()=>{},15000);await flush();f.c.fire(15000);const reply=await work;assert.equal(reply.pending,true);assert.equal(reply.expired,undefined);assert.equal(releases,0);d.resolve();await flush();assert.equal(later,1);assert.equal(releases,1);});
await check('soft reply never extends hard recovery deadline',async()=>{const f=observation(),d=deferred();let later=0,releases=0;const work=f.api.waitForDeckyLosslessAction(async cp=>{try{cp();await d.promise;cp();later++;return true;}finally{releases++;}},()=>{},()=>{},15000);await flush();f.c.fire(15000);await work;f.c.fire(40000);d.resolve();await flush();assert.equal(later,0);assert.equal(releases,1);});
await check('processing notice survives intermediate snapshots',async()=>{const f=clientFixture(),operation=f.client.mutate('game.setField',{field:'losslessScaling',value:'on'});assert.match(f.client.snapshot().notice,/最长 15 秒/);f.socket.emit('message',{type:'snapshot',runId,snapshot:{...f.snapshot,revision:2}});assert.match(f.client.snapshot().notice,/正在处理/);const id=f.socket.sent.at(-1).id;f.socket.emit('message',{type:'reply',runId,id,ok:true,result:{pending:true},notice:'unconfirmed'});await operation;assert.equal(f.client.snapshot().busy,false);f.client.dispose();});
await check('configuration confirmation clears processing while actual runtime notice remains unknown',async()=>{
 const f=clientFixture(),operation=f.client.mutate('game.setField',{field:'losslessScaling',value:'on'});
 assert.match(f.client.snapshot().notice,/等待配置与程序状态确认/);const id=f.socket.sent.at(-1).id;
 f.socket.emit('message',{type:'reply',runId,id,ok:true,result:{applied:true,pending:false},notice:'配置已启用，实际插帧待确认'});
 const result=await operation;assert.equal(result.applied,true);assert.equal(result.pending,false);assert.equal(f.client.snapshot().busy,false);assert.match(f.client.snapshot().notice,/实际插帧待确认/);
 const updated=structuredClone(f.snapshot);updated.revision++;updated.game.fields.losslessScaling.value='on';updated.notice='配置已启用，实际插帧待确认';
 f.socket.emit('message',{type:'snapshot',runId,snapshot:updated});assert.equal(f.client.snapshot().snapshot.game.fields.losslessScaling.value,'on');assert.equal(f.client.snapshot().snapshot.game.fields.losslessScaling.supported,true);assert.match(f.client.snapshot().notice,/实际插帧待确认/);f.client.dispose();
});
await check('missing action reply preserves unrelated data and disables unknown LS state',async()=>{const f=clientFixture(),operation=f.client.mutate('game.setField',{field:'losslessScaling',value:'on'});const rejected=assert.rejects(operation,/未确认/);f.c.fire(18000);await rejected;const state=f.client.snapshot();assert.equal(state.busy,false);assert.equal(state.snapshot.ready,true);assert.equal(state.snapshot.game.fields.losslessScaling.supported,false);assert.equal(state.snapshot.game.fields.losslessScaling.value,'unknown');assert.equal(state.snapshot.fan.preset,'balanced');assert.equal(f.socket.sent.filter(x=>x.command==='game.setField').length,1);assert.equal(f.socket.sent.at(-1).command,'snapshot');f.client.dispose();});
function adapterFixture(){
 const c=clock(),api=load('src/bridge/deckyLosslessState.ts',c),game={pid:1,processCreated:'birth',path:'fixture.exe'};
 const document={xml:'fixture-xml',profile:{enabled:true},configuredEnabled:true,controlAvailable:true,running:false,enabled:false};
 const state={game,power:{generation:1,phase:'ready',hardwareWritesAllowed:true},document,owner:async()=>({confirmed:false,runtimeConfirmed:null,notice:'小黄鸭实际状态待确认'}),locked:false,releases:0,calls:0,ownerArgs:null,fenced:false,powerHook:null,readHook:null};
 const adapter=load('src/bridge/losslessScaling.ts',c,{'./deckyLosslessState':api,'./api':{powerLifecycle:{get:async()=>{await state.powerHook?.();return state.power;}}},'./quickapp':{getLosslessGameState:async()=>{await state.readHook?.();return await state.document;},setLosslessScalingEnabled:async(...args)=>{state.calls++;state.ownerArgs=args;return state.owner(...args)},onLosslessScalingChanged:()=>()=>{}},'./gamePolicyTarget':{getPolicyGame:()=>state.game},'./deckyGameActions':{gameMirrorIdentity:g=>g?JSON.stringify([g.pid,g.processCreated,g.path]):''},'./quickActionLock':{tryAcquireQuickAction:()=>{if(state.locked)return null;state.locked=true;return()=>{state.locked=false;state.releases++}}}});
 const context={gameIdentity:JSON.stringify([game.pid,game.processCreated,game.path]),generation:1,gameAdmission:{lossless:'fixture-xml'},checkpoint:()=>{if(state.fenced)throw Error('FENCE_REVOKED');}};
 return {c,state,adapter,context,run:(value='on')=>adapter.setLosslessFromMirror({field:'losslessScaling',value},context)};
}
await check('actual Decky adapter never fabricates confirmation from owner no-feedback',async()=>{const f=adapterFixture();const r=await f.run();assert.equal(r.pending,true);assert.equal(r.applied,false);assert.match(r.notice,/LosslessScaling/);assert.equal(f.state.calls,1);assert.equal(f.state.releases,1);});
await check('actual adapter keeps owner lock through timeout and discards late follow-up',async()=>{const f=adapterFixture(),d=deferred();f.state.owner=async(_,__,cp)=>{await d.promise;cp();return {confirmed:true,notice:'fake'}};const r=f.run();await flush();assert.equal(f.state.calls,1);f.c.fire(40000);assert.equal((await r).pending,true);assert.equal(f.state.locked,true);await assert.rejects(f.run(),/LOSSLESS_BUSY/);assert.equal(f.state.calls,1);d.resolve();await flush();assert.equal(f.state.locked,false);assert.equal(f.state.releases,1);});
await check('slow preflight expires without dispatching original LS action',async()=>{const f=adapterFixture(),d=deferred();f.state.document=d.promise;const r=f.run();await flush();f.c.fire(40000);assert.equal((await r).pending,true);assert.equal(f.state.calls,0);d.resolve({xml:'fixture-xml',profile:{},enabled:false});await flush();assert.equal(f.state.calls,0);assert.equal(f.state.locked,false);});
await check('original source and power identity gates still reject before owner action',async()=>{const f=adapterFixture();f.state.document.xml='changed';await assert.rejects(f.run(),/GAME_MIRROR_SOURCE_CHANGED/);assert.equal(f.state.calls,0);f.state.document.xml='fixture-xml';f.state.power.generation=2;await assert.rejects(f.run(),/LOSSLESS_POWER_NOT_READY/);assert.equal(f.state.calls,0);assert.equal(f.state.locked,false);});
await check('shared switch distinguishes actual scaling, unknown configured intent and process exit',()=>{
 const {adapter,state}=adapterFixture();assert.equal(adapter.losslessSwitchEnabled(null),false);
 for(const configuredEnabled of [false,true])for(const running of [undefined,false,true])for(const enabled of [null,false,true]){
  const document={...state.document,configuredEnabled,running,enabled};assert.equal(adapter.losslessSwitchEnabled(document),running===true&&(enabled===true||(enabled===null&&configuredEnabled)));
  const field=adapter.losslessField(document);assert.equal(field.value,running===true?(enabled===null?'unknown':enabled?'on':'off'):'off');assert.equal(field.supported,true);
 }
 assert.equal(adapter.losslessField(null).value,'unknown');assert.equal(adapter.losslessField(null).supported,false);
});
await check('LS process exit clears Decky ON after demand refresh while the saved profile stays enabled',async()=>{
 const f=adapterFixture(),o=observation();let reads=0;f.state.document.running=true;f.state.document.enabled=true;
 const read=async()=>{reads++;return {...f.state.document};};
 o.cache.view('same-game',read);await flush();let view=o.cache.view('same-game',read);
 assert.equal(f.adapter.losslessField(view.document).value,'on');
 f.state.document.running=false;f.state.document.enabled=false;o.cache.demand();o.cache.view('same-game',read);await flush();view=o.cache.view('same-game',read);
 assert.equal(view.document.configuredEnabled,true);assert.equal(f.adapter.losslessField(view.document).value,'off');assert.equal(f.adapter.losslessField(view.document).supported,true);
 for(let n=0;n<50;n++)o.cache.view('same-game',read);await flush();assert.equal(reads,2);assert.equal(o.c.timers.size,0);assert.equal(f.state.calls,0);o.cache.dispose();
});
await check('valid XML and executable with no Profile remains enableable',async()=>{
 const f=adapterFixture();Object.assign(f.state.document,{profile:null,configuredEnabled:false,enabled:null});
 assert.equal(f.adapter.losslessField(f.state.document).supported,true);assert.equal(f.adapter.losslessField(f.state.document).value,'off');await f.run();assert.equal(f.state.calls,1);
});
await check('unknown actual status cannot block explicit on or off delegated to original owner',async()=>{
 for(const value of ['on','off']){const f=adapterFixture();Object.assign(f.state.document,{running:true,enabled:null});await f.run(value);assert.equal(f.state.calls,1);assert.equal(f.state.ownerArgs[1],value==='on');assert.equal(f.state.ownerArgs[3],f.state.game,'owner receives original game identity without focus requests');}
});
await check('configuration lifecycle confirmation with unknown runtime remains honest',async()=>{
 const f=adapterFixture();f.state.document.enabled=null;f.state.owner=async()=>({confirmed:true,runtimeConfirmed:null,notice:'配置已启用，实际插帧待确认'});
 const result=await f.run();assert.equal(result.applied,true);assert.equal(result.pending,false);assert.equal(result.notice,'配置已启用，实际插帧待确认');
});
await check('unsafe unknown owner rejection propagates and never mutates from mirror',async()=>{
 const f=adapterFixture();f.state.document.enabled=null;f.state.owner=async()=>{throw Error('OWNER_GAME_UNCONFIRMED');};
 await assert.rejects(f.run('off'),/OWNER_GAME_UNCONFIRMED/);assert.equal(f.state.calls,1);assert.equal(f.state.locked,false);
});
await check('pending cached display is disabled without changing its value',()=>{
 const f=adapterFixture();Object.assign(f.state.document,{running:true,enabled:null});const view=f.adapter.losslessField(f.state.document,true);
 assert.equal(view.value,'unknown');assert.equal(view.supported,false);f.state.document.controlAvailable=false;assert.equal(f.adapter.losslessField(f.state.document).supported,false);
});
await check('missing true control prerequisites reject without owner dispatch',async()=>{const f=adapterFixture();f.state.document.controlAvailable=false;await assert.rejects(f.run(),/LOSSLESS_CONTROL_UNAVAILABLE/);assert.equal(f.state.calls,0);assert.equal(f.state.releases,1);});
await check('absent Profile explicit off still reaches owner for safe idempotent cleanup',async()=>{const f=adapterFixture();f.state.document.profile=null;f.state.document.enabled=null;await f.run('off');assert.equal(f.state.calls,1);});
await check('power closure after document read blocks owner',async()=>{const f=adapterFixture();f.state.readHook=async()=>{f.state.power.hardwareWritesAllowed=false;};await assert.rejects(f.run(),/LOSSLESS_POWER_NOT_READY/);assert.equal(f.state.calls,0);assert.equal(f.state.locked,false);});
await check('game identity and source fence changes during awaits still block dispatch',async()=>{
 const game=adapterFixture();game.state.readHook=async()=>{game.state.game={...game.state.game,path:'other.exe'};};await assert.rejects(game.run(),/LOSSLESS_GAME_CHANGED/);assert.equal(game.state.calls,0);
 const fence=adapterFixture();fence.state.readHook=async()=>{fence.state.fenced=true;};await assert.rejects(fence.run(),/FENCE_REVOKED/);assert.equal(fence.state.calls,0);assert.equal(fence.state.locked,false);
});
await check('owner return after fence revocation cannot report success',async()=>{const f=adapterFixture();f.state.owner=async()=>{f.state.fenced=true;return {confirmed:true,runtimeConfirmed:true,notice:'fake'};};await assert.rejects(f.run(),/FENCE_REVOKED/);assert.equal(f.state.locked,false);});
await check('same-key source refresh retains display but discards superseded in-flight data',async()=>{
 const f=observation(),d=deferred();f.cache.view('game1',async()=>({enabled:false}));await flush();f.cache.refresh();f.cache.view('game1',()=>d.promise);await flush();f.cache.refresh();
 d.resolve({enabled:true});await flush();const pending=f.cache.view('game1',async()=>({enabled:false}));assert.equal(pending.document.enabled,false);assert.equal(pending.pending,true);await flush();assert.equal(f.cache.view('game1',()=>d.promise).document.enabled,false);f.cache.dispose();
});
await check('power generation or game switch clears cached state before new read settles',async()=>{
 for(const key of ['game2:1','game1:2','game1:1:suspending']){const f=observation(),d=deferred();f.cache.view('game1:1',async()=>({enabled:true}));await flush();assert.equal(f.cache.view(key,()=>d.promise).document,null);await flush();f.cache.clear();d.resolve({enabled:true});await flush();assert.equal(f.cache.view('new',async()=>({enabled:false})).document,null);f.cache.dispose();assert.equal(f.c.timers.size,0);}
});
await check('new valid read wins over an earlier timed-out completion',async()=>{
 const f=observation(),old=deferred();f.cache.view('game1',()=>old.promise);await flush();f.c.fire(6000);f.cache.demand();f.cache.view('game1',async()=>({enabled:false}));await flush();old.resolve({enabled:true});await flush();assert.equal(f.cache.view('game1',()=>old.promise).document.enabled,false);assert.equal(f.c.timers.size,0);f.cache.dispose();
});
await check('failed refresh retains prior display but reports no successful read receipt',async()=>{
 const f=observation();f.cache.view('game1',async()=>({enabled:true}));await flush();f.cache.refresh();f.cache.view('game1',async()=>{throw Error('read failed');});await flush();
 const view=f.cache.view('game1',()=>{throw Error('unsolicited poll');});assert.equal(view.document.enabled,true);assert.equal(view.pending,false);assert.match(view.notice,/暂不可读/);assert.equal(f.events.at(-1).ok,false);assert.equal(f.c.timers.size,0);f.cache.dispose();
});
await check('disposing two pending reads releases all owned timers and prevents resurrection',async()=>{
 const f=observation(),a=deferred(),b=deferred();f.cache.view('game1',()=>a.promise);await flush();f.c.fire(6000);f.cache.demand();f.cache.view('game1',()=>b.promise);await flush();const changed=f.changes();f.cache.dispose();f.cache.dispose();assert.equal(f.c.timers.size,0);a.resolve({enabled:true});b.reject(Error('late'));await flush();assert.equal(f.changes(),changed);assert.equal(f.cache.view('game1',async()=>({enabled:true})).document,null);
});

await check('mirror client accepts process-only lamp receipts and rejects malformed liveness metadata',async()=>{
 for(const program of [{known:true,running:true},{known:true,running:false},{known:false,running:false}]){
  const f=clientFixture();f.socket.emit('message',{type:'snapshot',runId,snapshot:{...f.snapshot,revision:2,losslessProgram:program}});
  assert.equal(f.client.snapshot().snapshot.revision,2);assert.equal(f.client.snapshot().snapshot.losslessProgram.known,program.known);assert.equal(f.client.snapshot().snapshot.losslessProgram.running,program.running);f.client.dispose();
 }
 for(const program of [{known:'yes',running:true},{known:false,running:true},{known:true,running:1},{known:true,running:true,enabled:true},null]){
  const f=clientFixture();f.socket.emit('message',{type:'snapshot',runId,snapshot:{...f.snapshot,revision:2,losslessProgram:program}});
  assert.equal(f.client.snapshot().snapshot,null);assert.equal(f.client.snapshot().connected,false);f.client.dispose();
 }
});

await check('unapplied action emits partial and false ok while completed remains distinct',async()=>{
 const c=clock(),api=load('src/bridge/deckyLosslessState.ts',c),events=[];
 const result=await api.waitForDeckyLosslessAction(async()=>({applied:false,pending:true}),()=>{},(event,detail)=>events.push({event,...detail}));await flush();
 assert.equal(result.result.applied,false);const receipt=events.find(e=>e.event==='lossless-action-finished');
 assert.equal(receipt.ok,false);assert.equal(receipt.completed,true);assert.equal(receipt.applied,false);assert.equal(receipt.pending,true);assert.equal(receipt.stage,'partial');assert.equal(c.timers.size,0);
});
fs.writeFileSync(path.join(task,'validation/lossless-resilience-selftest.json'),JSON.stringify({cases:cases.length,results:cases,hardwareWrites:0,memoryOwnersOnly:true},null,2));console.log('Decky Lossless resilience PASS '+cases.length);


