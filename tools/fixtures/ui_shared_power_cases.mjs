// Additional cases of lifecycle.ui-power-recovery, never a second test entry.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {createHash} from 'node:crypto';

export async function runSharedPowerCases({root,build,test,selection='default',powerSource=null}) {
 if(!['default','hardening','ready-admission','diagnostics','lifecycle','notification-scope'].includes(selection))throw Error('Unknown shared power selection');
 const input='src/bridge/powerLifecycleState.ts';
 const bytes=fs.readFileSync(path.join(root,input));
 const before=createHash('sha256').update(bytes).digest('hex');
 const bundle=await build({entryPoints:[path.join(root,input)],bundle:true,write:false,platform:'node',format:'cjs',
  plugins:[{name:'inert-unsubscribed-ipc-leaf',setup(b){
   if(powerSource!==null)b.onLoad({filter:/[\\/]powerLifecycleState\.ts$/},()=>({loader:'ts',contents:powerSource}));
   b.onLoad({filter:/[\\/]src[\\/]bridge[\\/]ipc\.ts$/},()=>({loader:'ts',contents:
    'export function invoke(){throw Error("The inactive singleton must not invoke IPC");} export function on(){throw Error("The inactive singleton must not subscribe");}'}));
  }}]});
 const module={exports:{}};
 vm.runInNewContext(bundle.outputFiles[0].text,{module,exports:module.exports,queueMicrotask,console,Promise,AbortController,setTimeout(){throw Error('Use the injected clock');},clearTimeout(){throw Error('Use the injected clock');}});
 const create=module.exports.createSharedPowerLifecycleSource;
 const flush=async()=>{for(let i=0;i<35;++i)await Promise.resolve();};
 const fact=(generation=2,phase='ready',revision=1,extra={})=>({known:true,generation,phase,publicationRevision:revision,inputReady:phase==='ready',hardwareWritesAllowed:phase==='ready',...extra});
 function fixture(){
  const handlers=new Map(),timers=new Map(),pending=[],seen=[],commands=[];let calls=0,serial=0;
  const source=create({read(signal){++calls;commands.push('power.lifecycle');return new Promise((resolve,reject)=>pending.push({resolve,reject,signal}));},
   listen(name,fn){let rows=handlers.get(name);if(!rows)handlers.set(name,rows=new Set());rows.add(fn);return()=>rows.delete(fn);},
   clock:{setTimeout(fn,ms){const id=++serial;timers.set(id,{fn,ms});return id;},clearTimeout(id){timers.delete(id);}}});
  const off=source.onSharedPowerLifecycle(value=>seen.push(structuredClone(value)));
  const emit=(name,value={})=>{for(const fn of [...(handlers.get(name)??[])])fn(value);};
  return {source,handlers,timers,pending,seen,commands,off,emit,get calls(){return calls;},
   async reply(value){assert(pending.length,'A real source read must be pending');pending.shift().resolve(value);await flush();},
   async fail(){assert(pending.length);pending.shift().reject(Error('inert read failure'));await flush();},
   async fire(){assert.equal(timers.size,1);const [id,timer]=[...timers][0];timers.delete(id);timer.fn();await flush();return timer.ms;},
   state:()=>structuredClone(source.getSharedPowerLifecycle()),
   stop(){off();assert.equal(timers.size,0);assert.equal([...handlers.values()].reduce((n,s)=>n+s.size,0),0);}};
 }
 async function boot(value=fact()) {const f=fixture();await flush();await f.reply(value);return f;}
 if(selection==='notification-scope'){
  let cases=0;
  const notificationCase=async(name,fn)=>{await test(name,fn);++cases;};
  const fencedReady=state=>({...state,inputReady:false,hardwareWritesAllowed:false,scopeReadbackPending:true});
  await notificationCase('notification scope: newer scoped hints revoke old write admission before matching readback',async()=>{
   const ready=fact(8,'ready',11,{observedNotificationSequence:21,suspendSerial:3});
   const next={...ready,publicationRevision:12,observedNotificationSequence:22,suspendSerial:4};
   const hint={generation:8,readbackScope:{suspendSerial:4,notificationSequence:22}};
   for(const reentrant of [false,true]){
    const initial=reentrant?fact(8,'resuming',10,{observedNotificationSequence:21,suspendSerial:3}):ready;
    const f=await boot(initial),delivered=[],late=[];
    let inject=reentrant;
    const observe=value=>[value.phase,value.inputReady,module.exports.sharedPowerWriteScope(value)?.join(':')??null];
    const first=f.source.onSharedPowerLifecycle(value=>{
     if(!inject||value.hardwareWritesAllowed!==true)return;
     inject=false;f.emit('power.resumed',hint);
    });
    const second=f.source.onSharedPowerLifecycle(value=>delivered.push(observe(value)));
    let offLate=()=>{};
    try{
     delivered.length=0;
     if(reentrant)f.emit('power.lifecycleChanged',ready);else f.emit('power.resumed',hint);
     assert.deepEqual(observe(f.state()),['ready',false,null],'a known newer scope must revoke old cached write admission');
     assert.deepEqual(delivered,[['ready',false,null]],'existing subscribers must see one revocation without any stale new admission');
     offLate=f.source.onSharedPowerLifecycle(value=>late.push(observe(value)));
     assert.deepEqual(late,[['ready',false,null]],'a new subscriber must not inherit retired write admission');
     for(let i=0;i<30;i++)f.emit('power.resumed',hint);
     await flush();assert.equal(f.calls,2);
     await f.reply(ready);
     assert.equal(module.exports.sharedPowerWriteScope(f.state()),null);
     assert.equal(await f.fire(),1000);await f.reply(next);
     assert.deepEqual(delivered,[['ready',false,null],['ready',true,'8:4:22']]);
     assert.deepEqual(late,delivered);assert.deepEqual(f.state(),next);assert.equal(f.timers.size,0);
    }finally{offLate();first();second();f.stop();}
   }
  });
  await notificationCase('notification scope: cached input and F5 are revoked independently of write admission',async()=>{
   const scope={notificationPending:false,observedNotificationSequence:21,suspendSerial:3};
   const input=fact(8,'ready',10,{...scope,hardwareWritesAllowed:false,gamepadUiRestart:true});
   const waking=fact(8,'resuming',10,{...scope,resumeReady:true,fanWakeGeneration:9});
   for(const [old,denied] of [[input,{...input,inputReady:false,gamepadUiRestart:false}],
    [waking,{...waking,resumeReady:false,fanWakeGeneration:undefined}]]){
    const f=await boot(old),seen=[],replayed=[];
    const evidence=state=>[state.phase,state.inputReady,state.gamepadUiRestart,state.resumeReady,state.fanWakeGeneration];
    const off=f.source.onSharedPowerLifecycle(state=>seen.push(evidence(state)),{observeFanWakeGeneration:true});
    let offReplay=()=>{};
    try{
     f.emit('power.resumed',{generation:8,readbackScope:{suspendSerial:4,notificationSequence:22}});
     offReplay=f.source.onSharedPowerLifecycle(state=>replayed.push(evidence(state)),{observeFanWakeGeneration:true});
     assert.deepEqual(evidence(f.state()),evidence(denied));
     assert.deepEqual(seen,[evidence(old),evidence(denied)],'already subscribed consumers must revoke independent readiness too');
     assert.deepEqual(replayed,[evidence(denied)]);
     await flush();assert.equal(f.calls,1,'a scoped hint must keep the existing non-Ready retry');
     assert.equal(await f.fire(),1000);
     await flush();assert.equal(f.calls,2);
     const current={...old,publicationRevision:11,suspendSerial:4,observedNotificationSequence:22};
     await f.reply(current);
     assert.deepEqual(replayed,[evidence(denied),evidence(current)]);
     assert.deepEqual(seen,[evidence(old),evidence(denied),evidence(current)]);
     assert.equal(module.exports.sharedPowerWriteScope(f.state()),null,'partial readiness cannot create write admission');
     f.emit('power.lifecycleChanged',fact(8,'ready',12,{suspendSerial:4,observedNotificationSequence:22}));
     assert.equal(f.timers.size,0);
    }finally{offReplay();off();f.stop();}
   }
  });
  await notificationCase('notification scope: coalesced same-generation suspend retires compatibility hint deduplication',async()=>{
   const ready=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3});
   const f=await boot(ready),seen=[];
   const off=f.source.onSharedPowerLifecycle(value=>seen.push(value.suspendSerial),{observeSuspendSerial:true});
   const ordinary=f.seen.length;
   try{
    for(const serial of [4,5]){
     const calls=f.calls;
     // The legacy wire carries the same generation for both real boundaries.
     // The full publication is deliberately dropped; only the read can advance it.
     for(let i=0;i<100;++i)f.emit('power.suspending',{generation:8});
     await flush();
     assert.equal(f.calls,calls+1,'a verified newer suspend scope must allow the next compatibility hint');
     assert.equal(f.state().suspendSerial,serial-1,'a hint cannot create authority');
     await f.reply({...ready,publicationRevision:10+serial,observedNotificationSequence:21+serial,suspendSerial:serial});
     assert.equal(f.calls,calls+1,'duplicate hints in one round must not create another read');
     assert.equal(f.timers.size,0);
    }
    assert.deepEqual(seen,[3,4,5]);assert.equal(f.seen.length,ordinary);
   }finally{off();f.stop();}
  });
  await notificationCase('notification scope: scoped hint survives a delayed legacy hint without granting authority',async()=>{
   const ready=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3});
   const f=await boot(ready),seen=[];
   const off=f.source.onSharedPowerLifecycle(value=>seen.push(value.suspendSerial),{observeSuspendSerial:true});
   const emit=(generation,suspendSerial,notificationSequence)=>f.emit('power.suspending',{
    generation,readbackScope:{suspendSerial,notificationSequence},known:true,hardwareWritesAllowed:true,
   });
   try{
    f.emit('power.suspending',{generation:8});await flush();await f.reply(ready);
    assert.equal(f.calls,2,'a delayed legacy hint may read the same scope');
    emit(8,4,22);await flush();
    assert.equal(f.calls,3,'a newer scoped hint must not be swallowed by the legacy hint');
    assert.deepEqual(f.state(),fencedReady(ready),'a hint revokes old permits without advancing the observed snapshot');
    for(let i=0;i<100;i++){
     emit(8,4,22); // identical in-flight hint
     emit(8,2,21); // old suspend scope
     emit(8,3,20); // old notification within the accepted suspend scope
     emit(7,3,21); // retired generation
    }
    const current={...ready,suspendSerial:4,observedNotificationSequence:22,publicationRevision:11};
    await f.reply(current);
    assert.deepEqual(seen,[3,3,4]);assert.deepEqual(f.state(),current);
    emit(8,3,21);await flush();
    assert.equal(f.calls,3,'retired scoped hints cannot discard the new read or restart polling');
    assert.equal(f.timers.size,0);
   }finally{off();f.stop();}
  });
  await notificationCase('notification scope: an old Ready cannot settle a newer scoped read',async()=>{
   const ready=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3});
   const current={...ready,suspendSerial:4,observedNotificationSequence:22,publicationRevision:11};
   const f=await boot(ready),confirmed=[],rechecks=[];
   const off=f.source.onSharedPowerLifecycle(()=>{},{onReadyRecheck:value=>rechecks.push(value.suspendSerial)});
   const hint=()=>f.emit('power.suspending',{generation:8,readbackScope:{suspendSerial:4,notificationSequence:22}});
   const cancel=f.source.requestSharedPowerLifecycleReadyConfirmation(value=>confirmed.push(value.suspendSerial));
   try{
    hint();await flush();await f.reply(ready);
    assert.deepEqual({confirmed,rechecks,timers:f.timers.size},{confirmed:[],rechecks:[],timers:1},
      'old Ready must leave the new boundary outstanding and cannot confirm or recheck recovery');
    assert.deepEqual(f.state(),fencedReady(ready),'the compatibility hint cannot advance cached authority');
    for(let i=0;i<100;i++)hint();
    await flush();assert.equal(f.calls,2,'duplicate hints must preserve the scheduled retry');
    assert.equal(await f.fire(),1000);await f.reply(current);
    assert.deepEqual(confirmed,[4]);assert.deepEqual(f.state(),current);assert.equal(f.timers.size,0);
   }finally{cancel();off();f.stop();}
  });
  await notificationCase('notification scope: a superseded read does not spend the newer floor retry delay',async()=>{
   const start=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3});
   const older={...start,publicationRevision:11,observedNotificationSequence:22,suspendSerial:4};
   const current={...start,publicationRevision:12,observedNotificationSequence:23,suspendSerial:5};
   for(const failOld of [false,true]){
    const f=await boot(start),confirmed=[];
    const hint=value=>f.emit('power.resumed',{generation:8,readbackScope:{suspendSerial:value.suspendSerial,notificationSequence:value.observedNotificationSequence}});
    let cancel=()=>{};
    try{
     hint(older);await flush();assert.equal(f.calls,2);
     const old=f.pending.shift();
     hint(current);cancel=f.source.requestSharedPowerLifecycleReadyConfirmation(value=>confirmed.push(value.suspendSerial));
     for(let i=0;i<100;i++){hint(older);hint(current);}
     await flush();assert.equal(f.calls,3,'a strictly newer floor must read before the old transport settles');
     assert.equal(old.signal.aborted,true,'the superseded IPC wait must be retired');
     assert.equal(f.pending[0].signal.aborted,false);
     assert.equal(f.timers.size,0);assert.deepEqual(confirmed,[]);assert.deepEqual(f.state(),fencedReady(start));
     if(failOld){
      old.reject(Error('late old read failure'));await flush();
      assert.equal(f.source.getSharedPowerLifecycleDiagnostics().read.inFlight,true,'old finally must not clear the new read');
      assert.equal(f.source.getSharedPowerLifecycleDiagnostics().counters.readFailed,0,'old failure must not count against the new scope');
     }
     await f.fail();
     for(let i=0;i<100;i++)hint(current);
     await flush();assert.equal(f.calls,3,'the newest floor must preserve its own failure backoff');
     assert.equal(await f.fire(),1000);await f.reply(current);
     if(!failOld){old.resolve(older);await flush();}
     assert.deepEqual(confirmed,[5]);assert.deepEqual(f.state(),current);assert.equal(f.timers.size,0);
     assert.equal(f.calls,4);
    }finally{cancel();f.stop();}
   }
  });
  await notificationCase('notification scope: a newer authoritative suspend replaces unresolved readback',async()=>{
   for(const wait of ['in-flight','backoff','hint-owns-current','reentrant-suspend','direct-in-flight','direct-backoff','shared-read']){
    const base=fact(8,'ready',11,{observedNotificationSequence:21,suspendSerial:3});
    const older=fact(8,'suspended',12,{observedNotificationSequence:22,suspendSerial:4});
    const newer=fact(8,'suspended',13,{observedNotificationSequence:23,suspendSerial:5});
    const latest=wait==='reentrant-suspend'?fact(9,'suspended',14,{observedNotificationSequence:24,suspendSerial:6}):newer;
    const f=await boot(base),confirmed=[];
    let cancel=()=>{},offNested=()=>{};
    try{
     f.emit('power.lifecycleChanged',older);
     cancel=f.source.requestSharedPowerLifecycleReadyConfirmation(value=>confirmed.push(value.suspendSerial));
     assert.equal(await f.fire(),1000);
     let old=f.pending[0],oldTimer;
     if(wait.endsWith('backoff')){
      for(const delay of [2000,5000,15000]){await f.fail();assert.equal(await f.fire(),delay);}
      await f.fail();oldTimer=[...f.timers.values()][0];assert.equal(oldTimer.ms,30000);old=null;
     }
     if(wait==='hint-owns-current'){
      f.emit('power.resumed',{generation:8,readbackScope:{suspendSerial:5,notificationSequence:23}});await flush();
      assert.equal(old.signal.aborted,true);
     }
     if(wait==='reentrant-suspend')offNested=f.source.onSharedPowerLifecycle(value=>{
      if(value.suspendSerial===newer.suspendSerial)f.emit('power.lifecycleChanged',latest);
     },{observeSuspendSerial:true});
     const count=f.calls;
     const readback=f.source.beginSharedPowerLifecycleReadback();
     if(wait.startsWith('direct-'))readback(newer);
     else if(wait==='shared-read'){
      await f.reply(newer);
      assert.equal(old.signal.aborted,false,'the public read must not retire itself after learning a new scope');
      assert.equal(f.calls,count,'the public read stays on its existing retry path');
      assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),false);
      assert.deepEqual(confirmed,[]);
      assert.equal(await f.fire(),2000);
      await f.reply({...newer,phase:'ready',hardwareWritesAllowed:true,inputReady:true,publicationRevision:14});
      assert.deepEqual(confirmed,[5]);assert.equal(f.timers.size,0);continue;
     }else f.emit('power.lifecycleChanged',newer);
     await flush();
     const alreadyCurrent=wait==='hint-owns-current';
     assert.equal(f.calls,count+Number(!alreadyCurrent),'current authoritative suspend must not inherit old transport or backoff: '+wait);
     assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),false);
     assert.deepEqual(confirmed,[]);
     if(old){
      assert.equal(old.signal.aborted,true);
      await f.reply({...older,phase:'ready',hardwareWritesAllowed:true});
      assert.deepEqual(confirmed,[],'an old Ready must not confirm the new request');
     }
     if(oldTimer){oldTimer.fn();await flush();assert.equal(f.calls,count+1,'late old timer must not consume the new retry');}
     const currentCount=f.calls;
     f.emit('power.lifecycleChanged',older);
     f.emit('power.lifecycleChanged',newer);
     if(wait.startsWith('direct-')){
      readback(older);
      readback(newer);
     }
     await flush();assert.equal(f.calls,currentCount);
     if(alreadyCurrent){
      // A read sent for the already-current hint must not be aborted again.
      // It still started before authoritative suspension was observed.
      assert.equal(f.pending[0].signal.aborted,false);
      await f.reply({...latest,phase:'ready',hardwareWritesAllowed:true,inputReady:true,publicationRevision:latest.publicationRevision+1});
      assert.deepEqual(confirmed,[]);
      assert.equal(await f.fire(),2000,'the same scope does not reset the retry delay already spent');
     }else{
      await f.fail();assert.equal(await f.fire(),1000,'only a fresh scope restarts the existing retry delay');
     }
     await f.reply({...latest,phase:'ready',hardwareWritesAllowed:true,inputReady:true,publicationRevision:latest.publicationRevision+1});
     assert.deepEqual(confirmed,[latest.suspendSerial],wait);assert.equal(f.timers.size,0);
     assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),true);
    }finally{offNested();cancel();f.stop();}
   }
  });
  await notificationCase('notification scope: a newer floor does not wait for the retired floor backoff',async()=>{
   const ready=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3}),f=await boot(ready);
   const hint=(suspendSerial,notificationSequence,event='power.resumed')=>
    f.emit(event,{generation:8,readbackScope:{suspendSerial,notificationSequence}});
   const confirmed=[];
   let cancel=()=>{};
   try{
    hint(4,22);await flush();
    for(const delay of [1000,2000,5000,15000]){await f.fail();assert.equal(await f.fire(),delay);}
    await f.fail();
    const [oldTimer]=f.timers.values();assert.equal(oldTimer.ms,30000);
    const calls=f.calls;
    hint(4,22,'power.resuming');hint(3,21);await flush();
    assert.equal(f.calls,calls,'duplicate and stale scopes must preserve backoff');
    assert.equal([...f.timers.values()][0],oldTimer);
    hint(5,23);cancel=f.source.requestSharedPowerLifecycleReadyConfirmation(value=>confirmed.push(value.suspendSerial));
    await flush();
    assert.equal(f.calls,calls+1,'the new floor must read without waiting for the old 30-second backoff');
    assert.equal(f.timers.size,0);assert.deepEqual(f.state(),fencedReady(ready));
    await f.fail();
    const [currentTimer]=f.timers.values();assert.equal(currentTimer.ms,1000);
    oldTimer.fn();hint(4,22);hint(5,23,'power.suspending');await flush();
    assert.equal(f.calls,calls+1,'a retired timer and same-scope hints cannot spend the new retry');
    assert.equal([...f.timers.values()][0],currentTimer);
    assert.equal(await f.fire(),1000);
    const current={...ready,publicationRevision:12,observedNotificationSequence:23,suspendSerial:5};
    await f.reply(current);
    assert.deepEqual(f.state(),current);assert.deepEqual(confirmed,[5]);assert.equal(f.timers.size,0);
   }finally{cancel();f.stop();}
  });
  await notificationCase('notification scope: old non-Ready snapshots can deny writes but cannot deliver recovery receipts',async()=>{
   const start=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3});
   for(const route of ['read','publication']){
    for(const fan of [{fanWakeGeneration:9},{fanStartupWakeGeneration:9,fanStartupWakeBoundary:'1234'}]){
     const f=await boot(start),seen=[];
     const off=f.source.onSharedPowerLifecycle(value=>seen.push(structuredClone(value)),{observeFanWakeGeneration:true,observeSuspendSerial:true});
     const hint={generation:8,readbackScope:{suspendSerial:4,notificationSequence:22}};
     const old=fact(8,'resuming',11,{observedNotificationSequence:21,suspendSerial:3,
      notificationPending:false,resumeReady:true,...fan});
     try{
      f.emit('power.resumed',hint);await flush();assert.equal(f.calls,2);
      if(route==='read')await f.reply(old);
      else f.emit('power.lifecycleChanged',old);
      const denied=f.state();
      assert.deepEqual([denied.phase,denied.hardwareWritesAllowed,denied.resumeReady,denied.fanWakeGeneration,
       denied.fanStartupWakeGeneration,denied.fanStartupWakeBoundary],['resuming',false,false,undefined,undefined,undefined],
       'an old non-Ready snapshot can retain the denial but cannot carry F5 or Fan recovery evidence');
       assert.deepEqual(seen,[start,fencedReady(start),denied]);
      if(route==='publication')await f.reply(old); // A publication retired the in-flight read.
      assert.equal(await f.fire(),1000);
      const current={...old,publicationRevision:12,observedNotificationSequence:22,suspendSerial:4};
      await f.reply(current);
      assert.deepEqual(f.state(),current,'matching non-Ready recovery evidence must still be delivered');
       assert.deepEqual(seen,[start,fencedReady(start),denied,current]);
      assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),false);
      const committed=fact(8,'ready',13,{observedNotificationSequence:22,suspendSerial:4});
      f.emit('power.lifecycleChanged',committed);
      assert.deepEqual(f.state(),committed);assert.equal(f.timers.size,0);
     }finally{off();f.stop();}
    }
   }
  });
  await notificationCase('notification scope: only explicit authority covering the pending boundary can retire it',async()=>{
   const ready=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3});
   const f=await boot(ready),scope=[];
   const off=f.source.onSharedPowerLifecycle(value=>scope.push([value.suspendSerial,value.observedNotificationSequence]),{observeNotificationSequence:true,observeSuspendSerial:true});
   const hint=(suspendSerial,notificationSequence)=>f.emit('power.resumed',{generation:8,readbackScope:{suspendSerial,notificationSequence}});
   try{
    hint(3,22);await flush();
    const {suspendSerial:omitted,...noSerial}=ready;
    await f.reply({...noSerial,publicationRevision:99,observedNotificationSequence:22});
    assert.deepEqual(f.state(),fencedReady(ready),'an inherited serial cannot satisfy an explicit boundary');
    assert.equal(f.timers.size,1);
    hint(3,23);hint(3,22);await flush();assert.equal(f.calls,2,'new and delayed hints share the original retry');
    f.emit('power.lifecycleChanged',{...ready,publicationRevision:98,observedNotificationSequence:22});
    assert.deepEqual(f.state(),fencedReady(ready),'a delayed Ready event cannot supersede the outstanding boundary');
    const current={...ready,publicationRevision:11,observedNotificationSequence:24,suspendSerial:4};
    f.emit('power.lifecycleChanged',current);await flush();
    assert.deepEqual(scope,[[3,21],[3,21],[4,24]]);assert.deepEqual(f.state(),current);
    assert.equal(f.timers.size,0,'a newer covering publication can settle the scoped demand');
    hint(3,23);await flush();assert.equal(f.calls,2);
   }finally{off();f.stop();}
  });
  await notificationCase('notification scope: a hint arriving during delivery still owns its next read',async()=>{
   const ready=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3});
   const middle={...ready,publicationRevision:11,observedNotificationSequence:22,suspendSerial:4};
   const current={...middle,publicationRevision:12,observedNotificationSequence:23,suspendSerial:5};
   const f=await boot(ready),confirmed=[],seen=[];
   const hint=(suspendSerial,notificationSequence)=>f.emit('power.resumed',{generation:8,readbackScope:{suspendSerial,notificationSequence}});
   const off=f.source.onSharedPowerLifecycle(value=>{
    seen.push(value.suspendSerial);
    if(value.suspendSerial===4)hint(5,23);
   },{observeSuspendSerial:true});
   const cancel=f.source.requestSharedPowerLifecycleReadyConfirmation(value=>confirmed.push(value.suspendSerial));
   try{
    hint(4,22);await flush();await f.reply(middle);
    assert.deepEqual(confirmed,[],'a newer reentrant hint retires the earlier confirmation');
    assert.equal(f.calls,3,'the new boundary owns exactly one successor read');
    await f.reply(middle);assert.deepEqual(confirmed,[]);assert.equal(f.timers.size,1);
    assert.equal(await f.fire(),1000);await f.reply(current);
    assert.deepEqual(seen,[3,3,4,4,5]);assert.deepEqual(confirmed,[5]);assert.equal(f.timers.size,0);
   }finally{cancel();off();f.stop();}
  });
  await notificationCase('notification scope: unsubscribe retires the scoped read before a new lifetime',async()=>{
   const ready=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3});
   for(const wait of ['backoff','read']){
    const f=await boot(ready);
    f.emit('power.resuming',{generation:8,readbackScope:{suspendSerial:4,notificationSequence:22}});
    await flush();
    let old;
    if(wait==='backoff'){await f.reply(ready);assert.equal(f.timers.size,1);}
    else {assert.equal(f.pending.length,1);old=f.pending.shift();}
    f.stop();
    if(old)assert.equal(old.signal.aborted,true,'last unsubscribe must retire the transport wait');
    const seen=[];
    const off=f.source.onSharedPowerLifecycle(value=>seen.push(value.generation));
    try{
     await flush();
     if(old){old.resolve(fact(9,'ready',11,{observedNotificationSequence:22,suspendSerial:4}));await flush();assert.deepEqual(seen,[]);}
     await f.reply(fact(2,'ready',1));
     assert.deepEqual(seen,[2]);assert.equal(f.timers.size,0,'a retired scope cannot reject a later lifetime');
    }finally{off();}
   }
  });
  await notificationCase('notification scope: opt-in observes current sequence without replaying ordinary business',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21})),seen=[],options={observeNotificationSequence:true};const ordinary=f.seen.length;
   const off=f.source.onSharedPowerLifecycle(value=>seen.push(value),options);options.observeNotificationSequence=false;seen.length=0;
   try{
    f.emit('power.lifecycleChanged',fact(8,'ready',11,{observedNotificationSequence:21}));assert.equal(seen.length,0);
    f.emit('power.lifecycleChanged',fact(8,'ready',12,{observedNotificationSequence:22}));assert.equal(seen.length,1,'optional public notification scope was not delivered');assert.equal(seen[0].observedNotificationSequence,22);assert.equal(f.seen.length,ordinary);
    for(let n=0;n<100;n++)f.emit('power.lifecycleChanged',fact(8,'ready',13,{observedNotificationSequence:22}));assert.equal(seen.length,1);assert.equal(f.seen.length,ordinary);
    f.emit('power.lifecycleChanged',fact(8,'ready',999,{observedNotificationSequence:21}));assert.equal(seen.length,1);assert.equal(f.state().publicationRevision,13);assert.equal(f.calls,1);assert.equal(f.timers.size,0);
    off();f.emit('power.lifecycleChanged',fact(8,'ready',14,{observedNotificationSequence:23}));assert.equal(seen.length,1);assert.equal(f.seen.length,ordinary);
   }finally{off();f.stop();}
  });
  await notificationCase('notification scope: reentrant observation leaves every remaining consumer on the current scope',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21})),seen=[],later=[],suspendOnly=[];let armed=false;
   const first=f.source.onSharedPowerLifecycle(value=>{if(armed&&value.observedNotificationSequence===22){armed=false;f.emit('power.lifecycleChanged',fact(9,'ready',12,{observedNotificationSequence:23}));}},{observeNotificationSequence:true});
   const second=f.source.onSharedPowerLifecycle(value=>seen.push(value),{observeNotificationSequence:true});
   const offLater=f.source.onSharedPowerLifecycle(value=>later.push([value.generation,value.observedNotificationSequence,value.publicationRevision]));
   const offSuspend=f.source.onSharedPowerLifecycle(value=>suspendOnly.push([value.generation,value.observedNotificationSequence]),{observeSuspendSerial:true});
   const ordinary=f.seen.length;
   try{
    seen.length=later.length=suspendOnly.length=0;armed=true;f.emit('power.lifecycleChanged',fact(9,'ready',11,{observedNotificationSequence:22}));
    assert(seen.length>0);assert(seen.every(value=>value.observedNotificationSequence===23),'retired notification scope was delivered after reentrant advancement');
    assert.deepEqual(seen.map(value=>value.observedNotificationSequence),[23],'same replacement Ready started the remaining opted consumer twice');
    assert.deepEqual(later,[[9,23,12]],'an ordinary subscriber receives the wake once with the current scope');
    assert.deepEqual(suspendOnly,[[9,23]],'an observer excluded from the nested dispatch still receives the current wake');
    assert.equal(f.seen.length,ordinary+1,'ordinary consumers still get the business wake once');
    assert.equal(f.state().observedNotificationSequence,23);assert.equal(f.timers.size,0);
   }
   finally{first();second();offLater();offSuspend();f.stop();}
  });
  await notificationCase('notification scope: a newer Ready notification cannot inherit the previous read confirmation',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21})),confirmed=[];
   const offFirst=f.source.requestSharedPowerLifecycleReadyConfirmation(value=>{
    confirmed.push(['first',value.observedNotificationSequence]);
    f.emit('power.lifecycleChanged',fact(8,'ready',11,{observedNotificationSequence:22}));
   });
   const offSecond=f.source.requestSharedPowerLifecycleReadyConfirmation(value=>confirmed.push(['second',value.observedNotificationSequence]));
   try{
    await flush();assert.equal(f.calls,2);
    await f.reply(fact(8,'ready',10,{observedNotificationSequence:21}));
    assert.deepEqual(confirmed,[['first',21]],'a newer notification requires a fresh read before confirming remaining consumers');
    assert.equal(f.state().observedNotificationSequence,22);assert.equal(f.calls,3);
    await f.reply(fact(8,'ready',11,{observedNotificationSequence:22}));
    assert.deepEqual(confirmed,[['first',21],['second',22]]);
    assert.equal(f.timers.size,0);
    assert.equal(f.source.getSharedPowerLifecycleDiagnostics().read.pendingReadyConfirmations,0);
   }finally{offFirst();offSecond();f.stop();}
  });
  await notificationCase('notification scope: Ready recheck requires an accepted read and returns with its subscription',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21})),ordinary=[],rechecks=[];
   const off=f.source.onSharedPowerLifecycle(value=>ordinary.push(value),{observeNotificationSequence:true,onReadyRecheck:value=>rechecks.push(value)});
   try{
    f.emit('power.lifecycleChanged',fact(8,'ready',11,{observedNotificationSequence:21}));
    assert.equal(ordinary.length,1);assert.equal(rechecks.length,0);
    f.source.requestSharedPowerLifecycleReconcile('same-scope');await flush();
    assert.equal(rechecks.length,0,'cached Ready cannot trigger recheck');
    await f.reply(fact(8,'ready',11,{observedNotificationSequence:21}));
    assert.equal(rechecks.length,1);assert.equal(ordinary.length,1);
    f.source.requestSharedPowerLifecycleReconcile('new-scope');await flush();
    await f.reply(fact(8,'ready',12,{observedNotificationSequence:22}));
    assert.equal(rechecks.length,1);assert.equal(ordinary.at(-1).observedNotificationSequence,22);
    f.source.requestSharedPowerLifecycleReconcile('unsubscribe-in-flight');await flush();off();
    await f.reply(fact(8,'ready',12,{observedNotificationSequence:22}));
    assert.equal(rechecks.length,1);assert.equal(f.calls,4);assert.equal(f.timers.size,0);
   }finally{off();f.stop();}
  });
  await notificationCase('notification scope: reentrant Ready recheck survives same-scope publication but not a new boundary',async()=>{
   for(const change of ['revision','notification','suspend']){
    const ready=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3}),f=await boot(ready);
    const rechecks=[],ordinary=[];let inject=true;
    const first=f.source.onSharedPowerLifecycle(()=>{},{observeSuspendSerial:true,observeNotificationSequence:true,onReadyRecheck:value=>{
     rechecks.push(['first',value.publicationRevision,value.observedNotificationSequence,value.suspendSerial]);
     if(!inject)return;
     inject=false;
     const next=change==='suspend'?fact(9,'suspended',11,{observedNotificationSequence:22,suspendSerial:4}):
      {...ready,publicationRevision:11,observedNotificationSequence:change==='notification'?22:21};
     f.emit('power.lifecycleChanged',next);
    }});
    const second=f.source.onSharedPowerLifecycle(value=>ordinary.push(value),{observeSuspendSerial:true,observeNotificationSequence:true,onReadyRecheck:value=>{
     rechecks.push(['second',value.publicationRevision,value.observedNotificationSequence,value.suspendSerial]);
    }});
    try{
     ordinary.length=0;
     f.source.requestSharedPowerLifecycleReconcile('original-read');await flush();await f.reply(ready);
     if(change==='revision'){
      assert.deepEqual(rechecks,[['first',10,21,3],['second',11,21,3]],'a same-scope publication must not consume the remaining Ready recheck');
      assert.equal(ordinary.length,0);assert.equal(f.calls,2);assert.equal(f.timers.size,0);
     }else{
      assert.deepEqual(rechecks,[['first',10,21,3]],'a newer boundary must stop the retired Ready recheck');
      assert.equal(ordinary.length,1);assert.equal(ordinary[0].observedNotificationSequence,22);
      const current=change==='suspend'?fact(9,'ready',12,{observedNotificationSequence:22,suspendSerial:4}):f.state();
      if(change==='suspend')f.emit('power.lifecycleChanged',current);
      f.source.requestSharedPowerLifecycleReconcile('current-read');await flush();await f.reply(current);
      assert.deepEqual(rechecks.slice(1),['first','second'].map(name=>[name,current.publicationRevision,22,current.suspendSerial]));
      assert.equal(f.timers.size,0);
     }
    }finally{first();second();f.stop();}
   }
  });
  await notificationCase('notification scope: Fan-only delivery stops at a newer public notification boundary',async()=>{
   const recent=fact(8,'ready',10,{notificationPending:false,observedNotificationSequence:21,suspendSerial:3});
   for(const advancesScope of [true,false]){
    const f=await boot(recent),seen=[];
    const wake={...recent,fanWakeOnly:true,fanWakeGeneration:9};
    let inject=true;
    const first=f.source.onSharedPowerLifecycle(value=>{
     if(!value.fanWakeGeneration)return;
     seen.push(['first',value.observedNotificationSequence]);
     if(!inject)return;
     inject=false;
     f.emit('power.lifecycleChanged',{...recent,publicationRevision:11,observedNotificationSequence:advancesScope?22:21});
    },{observeFanWakeGeneration:true});
    const second=f.source.onSharedPowerLifecycle(value=>{
     if(value.fanWakeGeneration)seen.push(['second',value.observedNotificationSequence]);
    },{observeFanWakeGeneration:true});
    try{
     f.emit('power.lifecycleChanged',wake);
     assert.deepEqual(seen,advancesScope?[['first',21]]:[['first',21],['second',21]],
      'a nested public ingress must retire old scoped Fan delivery; revision-only metadata must not');
     if(advancesScope){
      f.emit('power.lifecycleChanged',wake);
      assert.deepEqual(seen,[['first',21]],'an independently delayed replay must also be ignored');
      f.emit('power.lifecycleChanged',{...wake,publicationRevision:11,observedNotificationSequence:22});
      assert.deepEqual(seen,[['first',21],['first',22],['second',22]],'the current Fan fact must still reach both listeners');
      assert.equal(f.state().observedNotificationSequence,22);
     }
     assert.equal(f.state().fanWakeGeneration,undefined,'Fan-only delivery must not update public authority');
     assert.equal(f.calls,1);assert.equal(f.timers.size,0);
    }finally{first();second();f.stop();}
   }
  });
  await notificationCase('notification scope: a newer scoped hint retires Fan-only delivery before readback',async()=>{
   const ready=fact(8,'ready',10,{notificationPending:false,observedNotificationSequence:21,suspendSerial:3});
   const past={...ready,fanWakeOnly:true,fanWakeGeneration:9};
   const next={...ready,publicationRevision:11,observedNotificationSequence:22,suspendSerial:4};
   const hint={generation:8,readbackScope:{suspendSerial:4,notificationSequence:22}};
   for(const reentrant of [false,true]){
    const f=await boot(ready),seen=[];
    let inject=reentrant;
    const first=f.source.onSharedPowerLifecycle(value=>{
     if(!value.fanWakeGeneration)return;
     seen.push(['first',value.fanWakeGeneration]);
     if(inject){inject=false;f.emit('power.resumed',hint);}
    },{observeFanWakeGeneration:true});
    const second=f.source.onSharedPowerLifecycle(value=>{
     if(value.fanWakeGeneration)seen.push(['second',value.fanWakeGeneration]);
    },{observeFanWakeGeneration:true});
    try{
     if(!reentrant)f.emit('power.resumed',hint);
     f.emit('power.lifecycleChanged',past);
     const before=reentrant?[['first',9]]:[];
     assert.deepEqual(seen,before,'an older Fan-only wake must stop at a newer scoped readback demand');
     await flush();assert.equal(f.calls,2,'the scoped hint still requests one read');
     f.emit('power.lifecycleChanged',{...past,publicationRevision:11,suspendSerial:4});
     f.emit('power.lifecycleChanged',{...past,publicationRevision:11,observedNotificationSequence:22});
     assert.deepEqual(seen,before,'each scope component must cover the newer hint');
     f.emit('power.lifecycleChanged',{...next,fanWakeOnly:true,fanWakeGeneration:10});
     assert.deepEqual(seen,[...before,['first',10],['second',10]],'current Fan-only delivery may precede matching public readback');
     assert.deepEqual(f.state(),fencedReady(ready),'Fan-only delivery must not settle public readback');
     await f.reply(next);
     assert.deepEqual(f.state(),next);assert.equal(f.calls,2);assert.equal(f.timers.size,0);
    }finally{first();second();f.stop();}
   }
  });
  await notificationCase('notification scope: reentrant scoped hints withdraw old F5 and Fan fields from delivery and replay',async()=>{
   const ready=fact(8,'ready',10,{notificationPending:false,observedNotificationSequence:21,suspendSerial:3});
   const old=fact(8,'resuming',11,{notificationPending:false,observedNotificationSequence:21,suspendSerial:3,
    resumeReady:true,fanWakeGeneration:9});
   // Change only the ingress sequence: even ordinary subscribers which do not
   // opt in to metadata must see a newly valid F5 after its old receipt was withheld.
   const current={...old,publicationRevision:12,observedNotificationSequence:22};
   const f=await boot(ready),ordinary=[],late=[];
   let injected=false,offLate=()=>{};
   const evidence=value=>[value.phase,value.resumeReady,value.fanWakeGeneration];
   const first=f.source.onSharedPowerLifecycle(value=>{
    if(injected||value.resumeReady!==true)return;
    injected=true;
    f.emit('power.resumed',{generation:8,readbackScope:{suspendSerial:3,notificationSequence:22}});
    offLate=f.source.onSharedPowerLifecycle(value=>late.push(evidence(value)));
   });
   const second=f.source.onSharedPowerLifecycle(value=>ordinary.push(evidence(value)));
   try{
    f.emit('power.lifecycleChanged',old);await flush();
    const denied=['resuming',false,undefined];
    assert.deepEqual(ordinary.at(-1),denied,'remaining ordinary subscribers cannot receive old F5 or Fan evidence');
    assert.deepEqual(late,[denied],'a new subscriber cannot replay old evidence from the accepted snapshot');
    assert.deepEqual(evidence(f.state()),denied,'the synchronous getter follows the same recovery fence');
    assert.equal(f.calls,2);
    await f.reply(current);
    assert.deepEqual(ordinary.at(-1),evidence(current));assert.deepEqual(late,[denied,evidence(current)]);
    const committed={...ready,publicationRevision:13,observedNotificationSequence:22};
    f.emit('power.lifecycleChanged',committed);
    assert.deepEqual(f.state(),committed);assert.equal(f.timers.size,0);
   }finally{offLate();first();second();f.stop();}
  });
  await notificationCase('notification scope: a reentrant scoped hint retires an in-flight Fan republish',async()=>{
   const wake=fact(8,'ready',10,{notificationPending:false,observedNotificationSequence:21,suspendSerial:3,fanWakeGeneration:9});
   const f=await boot(wake),seen=[];let inject=true;
   const first=f.source.onSharedPowerLifecycle(()=>{},{onFanWakeRepublished:value=>{
    seen.push(['first',value.publicationRevision]);
    if(!inject)return;
    inject=false;
    f.emit('power.resumed',{generation:8,readbackScope:{suspendSerial:4,notificationSequence:22}});
   }});
   const second=f.source.onSharedPowerLifecycle(()=>{},{onFanWakeRepublished:value=>seen.push(['second',value.publicationRevision])});
   try{
    f.emit('power.lifecycleChanged',{...wake,publicationRevision:11});
    assert.deepEqual(seen,[['first',11]],'remaining Fan republish callbacks must stop at the new readback scope');
    await flush();assert.equal(f.calls,2);
    const current={...wake,publicationRevision:12,suspendSerial:4,observedNotificationSequence:22};
    await f.reply(current);
    f.emit('power.lifecycleChanged',{...current,publicationRevision:13});
    assert.deepEqual(seen,[['first',11],['first',13],['second',13]],'matching Fan republish remains available after readback');
    assert.equal(f.timers.size,0);
   }finally{first();second();f.stop();}
  });
  await notificationCase('notification scope: Fan republish cannot cross a same-revision boundary',async()=>{
   for(const change of ['notification','suspend','revision']){
    const wake=fact(8,'ready',10,{notificationPending:false,observedNotificationSequence:21,suspendSerial:3,fanWakeGeneration:9});
    const f=await boot(wake),rechecks=[],ordinary=[];
    let inject=true;
    const first=f.source.onSharedPowerLifecycle(()=>{},{onFanWakeRepublished:value=>{
     rechecks.push(['first',value.publicationRevision,value.observedNotificationSequence,value.suspendSerial]);
     if(!inject)return;
     inject=false;
     f.emit('power.lifecycleChanged',{...value,publicationRevision:change==='revision'?12:11,
      observedNotificationSequence:change==='notification'?22:21,suspendSerial:change==='suspend'?4:3});
    }});
    const second=f.source.onSharedPowerLifecycle(value=>ordinary.push(value),{observeNotificationSequence:true,observeSuspendSerial:true,
     onFanWakeRepublished:value=>rechecks.push(['second',value.publicationRevision,value.observedNotificationSequence,value.suspendSerial])});
    try{
     ordinary.length=0;
     f.emit('power.lifecycleChanged',{...wake,publicationRevision:11});
     if(change==='revision'){
      assert.deepEqual(rechecks,[['first',11,21,3],['first',12,21,3],['second',12,21,3]],
       'the newer same-scope republish replaces the in-flight republish without duplicate delivery');
      assert.equal(ordinary.length,0);
     }else{
      const seq=change==='notification'?22:21,serial=change==='suspend'?4:3;
      assert.deepEqual(ordinary.map(value=>[value.observedNotificationSequence,value.suspendSerial]),[[seq,serial]]);
      assert.deepEqual(rechecks,[['first',11,21,3]],'retired Fan republish must not run after the boundary advances');
      f.emit('power.lifecycleChanged',{...wake,publicationRevision:12});
      assert.equal(rechecks.length,1,'old-scope authority must remain retired');
      f.emit('power.lifecycleChanged',{...f.state(),publicationRevision:13});
      assert.deepEqual(rechecks,[['first',11,21,3],['first',13,seq,serial],['second',13,seq,serial]]);
     }
     assert.equal(f.calls,1);assert.equal(f.timers.size,0);
    }finally{first();second();f.stop();}
   }
  });
  await notificationCase('notification scope: cancelling the last confirmation retires only its read demand',async()=>{
   for(const when of ['queued','in-flight','backoff','shared-suspend','another-confirmation','explicit-reconcile','ordinary-read-in-flight']){
    const ready=fact(8,'ready',10,{observedNotificationSequence:21,suspendSerial:3}),f=await boot(ready);
    let confirmed=0,other=0;
    if(when==='ordinary-read-in-flight'){
     f.source.requestSharedPowerLifecycleReconcile('ordinary-owner');
     await flush();
    }
    const cancel=f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{++confirmed;});
    let stopOther=()=>{};
    try{
     if(when!=='queued')await flush();
     if(when==='backoff')await f.fail();
     if(when==='shared-suspend')f.emit('power.lifecycleChanged',fact(9,'suspended',11,{observedNotificationSequence:22,suspendSerial:4}));
     if(when==='another-confirmation')stopOther=f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{++other;});
     if(when==='explicit-reconcile')f.source.requestSharedPowerLifecycleReconcile('ordinary-owner');
     cancel();
     if(when==='in-flight'||when==='shared-suspend'||when==='explicit-reconcile')await f.fail();
     if(when==='another-confirmation'||when==='ordinary-read-in-flight')await f.reply(ready);
     await flush();
     if(when==='shared-suspend'||when==='explicit-reconcile'){
      assert.equal(f.timers.size,1,when+' must keep its independent read demand');
      await f.fire();
      await f.reply(when==='shared-suspend'?fact(9,'ready',12,{observedNotificationSequence:23,suspendSerial:4}):ready);
     }else if(when==='another-confirmation'){
      assert.equal(other,0,'the older read cannot confirm the remaining newer request');
      await f.reply(ready);
      assert.equal(other,1);
     }else{
      assert.equal(f.calls,when==='queued'?1:2,when+' must not start another cancelled read');
      assert.equal(f.source.getSharedPowerLifecycleDiagnostics().read.demand,false,when+' cancelled confirmation no longer owns read demand');
     }
     assert.equal(confirmed,0);
     assert.equal(f.timers.size,0,when+' must return the timer');
     assert.equal(f.source.getSharedPowerLifecycleDiagnostics().read.pendingReadyConfirmations,0);
    }finally{stopOther();cancel();f.stop();}
   }
  });
  return {source:input,sha256:before,actualFactory:true,selection,cases,legacyCasesExecuted:0};
 }
 if(selection==='default'||selection==='lifecycle'){
  if(selection==='lifecycle'){
  await test('lifecycle: ordered-protocol missing sequence cannot reopen ingress admission',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21,notificationPending:true,hardwareWritesAllowed:false}));
   const seen=[];const offBad=f.source.onSharedPowerLifecycle(state=>{if(state.known===false)throw Error('consumer-fault');});
   const offOther=f.source.onSharedPowerLifecycle(state=>seen.push(structuredClone(state)));
   try{
    f.emit('power.lifecycleChanged',fact(8,'ready',10));
    assert.equal(f.state().known,false,'missing ordered sequence cannot grant Ready');
    assert.equal(f.state().observedNotificationSequence,21);assert.equal(f.state().publicationRevision,10);
    assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),false);
    assert.equal(seen.at(-1).known,false,'one throwing consumer cannot block denied-state broadcast');
    f.emit('power.lifecycleChanged',fact(8,'ready',11,{observedNotificationSequence:22}));
    assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),true);assert.equal(f.timers.size,0);
   }finally{offOther();offBad();f.stop();}
  });
  await test('lifecycle: ordered-protocol malformed fields cannot poison recovery watermarks',async()=>{
   for(const extra of [
    {observedNotificationSequence:'21'},{observedNotificationSequence:-1},{observedNotificationSequence:NaN},
    {observedNotificationSequence:Number.MAX_SAFE_INTEGER+1},{observedNotificationSequence:21,notificationPending:'false'},
    {observedNotificationSequence:21,inputReady:'true'},{observedNotificationSequence:21,hardwareWritesAllowed:'true'},
    {observedNotificationSequence:21,known:'true'},
   ]){
    const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21,notificationPending:true,hardwareWritesAllowed:false}));
    try{
     f.emit('power.lifecycleChanged',fact(8,'ready',999,extra));
     assert.equal(f.state().known,false,'malformed authority is UNKNOWN');
     assert.equal(f.state().publicationRevision,10,'malformed authority cannot poison publication watermark');
     assert.equal(f.state().observedNotificationSequence,21,'malformed authority cannot poison notification watermark');
     f.emit('power.lifecycleChanged',fact(8,'ready',11,{observedNotificationSequence:22}));
     assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),true,'valid same-owner recovery remains reachable');
    }finally{f.stop();}
   }
  });
  await test('lifecycle: ordered-protocol downgraded read cannot confirm or renew retry',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21,notificationPending:true,hardwareWritesAllowed:false}));let confirmed=0;
   const cancel=f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{++confirmed;});
   try{
    assert.equal(await f.fire(),1000);await f.reply(fact(8,'ready',999));
    assert.equal(confirmed,0,'missing ordered read cannot confirm Ready');
    assert.equal(f.state().publicationRevision,10);assert.equal(f.state().observedNotificationSequence,21);
    assert.equal(await f.fire(),2000,'bad reply preserves original read backoff');
    await f.reply(fact(8,'ready',11,{observedNotificationSequence:22}));
    assert.equal(confirmed,1);assert.equal(f.timers.size,0);assert.equal(f.calls,3);
   }finally{cancel();f.stop();}
  });
  await test('lifecycle: ordered-protocol legacy upgrade and fresh subscriber lifetime stay compatible',async()=>{
   const f=await boot(fact(8,'ready',1));
   try{
    assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),true,'initial legacy source keeps existing compatibility');
    f.emit('power.lifecycleChanged',fact(8,'ready',2,{observedNotificationSequence:0}));
    f.emit('power.lifecycleChanged',fact(8,'ready',3));
    assert.equal(f.state().known,false);assert.equal(f.state().observedNotificationSequence,0);assert.equal(f.state().publicationRevision,2);
    f.emit('power.lifecycleChanged',fact(8,'ready',3,{observedNotificationSequence:1}));
    assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),true);
   }finally{f.stop();}
   const off=f.source.onSharedPowerLifecycle(()=>{});
   try{await flush();await f.reply(fact(1,'ready',1));assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),true,'new source lifetime owns fresh protocol watermarks');}
   finally{off();f.stop();}
  });

  await test('lifecycle: ordered-protocol explicit UNKNOWN advances only valid metadata',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21}));
   try{
    f.emit('power.lifecycleChanged',{known:false,publicationRevision:11,observedNotificationSequence:22});
    assert.equal(f.state().known,false);assert.equal(f.state().publicationRevision,11);assert.equal(f.state().observedNotificationSequence,22);
    f.emit('power.lifecycleChanged',fact(8,'ready',10,{observedNotificationSequence:21}));
    assert.equal(f.state().known,false,'explicit UNKNOWN fences older Ready');
    f.emit('power.lifecycleChanged',{known:false,publicationRevision:999,observedNotificationSequence:23,hardwareWritesAllowed:'true'});
    assert.equal(f.state().publicationRevision,11);assert.equal(f.state().observedNotificationSequence,22);
    f.emit('power.lifecycleChanged',{known:false,publicationRevision:999});
    assert.equal(f.state().publicationRevision,11);assert.equal(f.state().observedNotificationSequence,22);
    f.emit('power.lifecycleChanged',fact(8,'ready',12,{observedNotificationSequence:23}));
    assert.equal(module.exports.isSharedPowerLifecycleAdmitted(f.state()),true);
   }finally{f.stop();}
  });
  }
  await test('lifecycle: older receipt cannot reopen the same-publication ingress fence',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:20}));
   try{
    f.source.requestSharedPowerLifecycleReconcile('ingress');await flush();
    await f.reply(fact(8,'ready',10,{observedNotificationSequence:21,notificationPending:true,hardwareWritesAllowed:false}));
    for(const publicationRevision of [10,999])f.emit('power.lifecycleChanged',fact(8,'ready',publicationRevision,{observedNotificationSequence:20}));
    assert.equal(f.state().notificationPending,true,'older receipt must retain ingress fence');
    assert.equal(f.state().publicationRevision,10,'stale receipt cannot poison publication watermark');
    f.emit('power.lifecycleChanged',fact(8,'ready',11,{observedNotificationSequence:21}));
    assert.equal(f.state().hardwareWritesAllowed,true);assert.equal(f.timers.size,0);
   }finally{f.stop();}
  });
  await test('lifecycle: stale read cannot deliver Ready confirmation after a newer ingress observation',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21,notificationPending:true,hardwareWritesAllowed:false}));let confirmed=0;
   try{
    f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{confirmed++;});
    await f.fire();await f.reply(fact(8,'ready',10,{observedNotificationSequence:20}));
    assert.equal(confirmed,0,'stale receipt read cannot confirm Ready');assert.equal(f.state().notificationPending,true);
    assert.equal(await f.fire(),2000);await f.reply(fact(8,'ready',11,{observedNotificationSequence:21}));
    assert.equal(confirmed,1);assert.equal(f.timers.size,0);
   }finally{f.stop();}
  });
  await test('lifecycle: reentrant suspend retires the remaining older Ready fanout',async()=>{
   const f=await boot(fact(2,'ready',1,{observedNotificationSequence:1})),values=[];let armed=false;
   const first=f.source.onSharedPowerLifecycle(value=>{if(armed&&value.generation===3&&value.phase==='ready')f.emit('power.lifecycleChanged',fact(4,'suspended',3,{observedNotificationSequence:3}));});
   const second=f.source.onSharedPowerLifecycle(value=>values.push(value));
   try{
    values.length=0;armed=true;f.emit('power.lifecycleChanged',fact(3,'ready',2,{observedNotificationSequence:2}));
    assert.equal(values.at(-1).phase,'suspended','older Ready fanout must stop after reentrant suspend');
    assert.equal(values.length,1);assert.equal(f.state().generation,4);assert.equal(f.timers.size,1);
    f.emit('power.lifecycleChanged',fact(4,'ready',4,{observedNotificationSequence:3}));
    assert.equal(values.at(-1).phase,'ready');assert.equal(f.timers.size,0);
   }finally{first();second();f.stop();}
  });
  await test('lifecycle: metadata-only reentrancy still reaches every current subscriber',async()=>{
   const f=await boot(),values=[];let armed=false;
   const first=f.source.onSharedPowerLifecycle(value=>{if(armed&&value.generation===3){armed=false;f.emit('power.lifecycleChanged',{...value,publicationRevision:3});}});
   const second=f.source.onSharedPowerLifecycle(value=>values.push(value));
   try{
    values.length=0;armed=true;f.emit('power.lifecycleChanged',fact(3,'ready',2));
    assert.equal(values.length,1);assert.equal(values[0].generation,3);assert.equal(f.state().publicationRevision,3);assert.equal(f.timers.size,0);
   }finally{first();second();f.stop();}
  });
  await test('lifecycle: repeated complete and canceled cycles heal lost Ready without new power intent',async()=>{
   const f=await boot(fact(2,'ready',1,{observedNotificationSequence:0}));let revision=1,sequence=0;
   try{
    for(let cycle=0;cycle<20;cycle++){
     const generation=3+cycle;
     f.emit('power.lifecycleChanged',fact(generation,'suspending',++revision,{observedNotificationSequence:++sequence}));
     if(cycle%2===0)f.emit('power.lifecycleChanged',fact(generation,'suspended',++revision,{observedNotificationSequence:sequence}));
     f.emit('power.lifecycleChanged',fact(generation,'resuming',++revision,{observedNotificationSequence:++sequence}));
     f.emit('power.resumed',{});f.emit('power.suspending',{generation:generation-1});
     for(let i=0;i<50;i++)f.emit('window.focus');
     assert.equal(f.timers.size,1);await f.fire();
     await f.reply(fact(generation,'ready',++revision,{observedNotificationSequence:sequence,inputReady:false}));
     assert.equal(f.state().generation,generation);assert.equal(f.state().phase,'ready');assert.equal(f.timers.size,0);
    }
    assert.equal(f.calls,21);assert(f.commands.every(command=>command==='power.lifecycle'));
   }finally{f.stop();}
  });
  await test('lifecycle: truncation disposes pending read, retry, confirmation and receipt watermark',async()=>{
   const f=await boot(fact(10,'suspended',20,{observedNotificationSequence:100}));let confirmed=0;
   f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{confirmed++;});await f.fire();
   const retired=f.pending.shift();f.stop();
   const values=[],off=f.source.onSharedPowerLifecycle(value=>values.push(value));await flush();
   try{
    retired.resolve(fact(99,'ready',999,{observedNotificationSequence:999}));await flush();assert.equal(values.length,0);assert.equal(confirmed,0);
    await f.reply(fact(2,'ready',1,{observedNotificationSequence:0}));
    assert.equal(values.at(-1).generation,2);assert.equal(f.timers.size,0);assert.equal(confirmed,0);
   }finally{off();f.stop();}
  });
  if(selection==='lifecycle')return {source:input,sha256:before,actualFactory:true,selection,cases:11,legacyCasesExecuted:0};
 }
 if(selection==='diagnostics'){
  await test('diagnostics: inactive singleton and source stay inert; cached outcomes and stop remain honest',async()=>{
   const idle=module.exports.getSharedPowerLifecycleDiagnostics();assert.equal(idle.active,false);assert.equal(idle.counters.readStarted,0);
   let reads=0,listens=0,timers=0;
   const unused=create({read(){reads++;return Promise.resolve(null);},listen(){listens++;return()=>{};},clock:{setTimeout(){timers++;return 1;},clearTimeout(){timers--;}}});
   for(let i=0;i<100;i++)unused.getSharedPowerLifecycleDiagnostics();await flush();
   assert.equal(reads+listens+timers,0,'inactive getter must not acquire resources');
   const f=fixture();await flush();
   const inspect=()=>{const calls=f.calls,subscriptions=[...f.handlers.values()].reduce((n,s)=>n+s.size,0),scheduled=f.timers.size;
    const d=f.source.getSharedPowerLifecycleDiagnostics();
    assert.equal(f.calls,calls,'diagnostics getter must not trigger read');assert.equal(f.timers.size,scheduled,'diagnostics getter must not schedule timer');
    assert.equal([...f.handlers.values()].reduce((n,s)=>n+s.size,0),subscriptions,'diagnostics getter must not subscribe');return d;};
   try{
    let d=inspect();assert.equal(d.read.inFlight,true);assert.equal(d.counters.readStarted,1);assert.equal(d.lastObservation.source,'read');
    const stable=JSON.stringify(d);for(let i=0;i<100;i++)assert.equal(JSON.stringify(inspect()),stable);await flush();assert.equal(f.calls,1);
    assert.deepEqual(Object.keys(d).sort(),['schemaVersion','source','active','lifetimeEpoch','subscriberCount','snapshot','read','counters','lastObservation'].sort());
    assert.equal(d.schemaVersion,1);assert.equal(d.subscriberCount,1);assert.equal(d.snapshot,null);
    await f.reply({known:false,publicationRevision:2});d=inspect();assert.equal(d.counters.readUnknown,1);assert.equal(d.lastObservation.outcome,'unknown');assert.equal(d.read.retryTimerActive,true);
    await f.fire();await f.reply(fact(2,'ready',1));d=inspect();assert.equal(d.counters.readStale,1);assert.equal(d.lastObservation.outcome,'stale');assert.equal(d.snapshot,null);
    await f.fire();await f.fail();d=inspect();assert.equal(d.counters.readFailed,1);assert.equal(d.lastObservation.outcome,'failed');
    f.emit('power.lifecycleChanged',fact(3,'resuming',3));d=inspect();assert.equal(d.counters.authorityEvents,1);assert.equal(d.lastObservation.source,'authority-event');assert.equal(d.snapshot.phase,'resuming');
    let confirmed=0;f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{confirmed++;});assert.equal(inspect().read.pendingReadyConfirmations,1);
    await f.fire();await f.reply(fact(3,'ready',4,{game:'PRIVATE_GAME',account:'PRIVATE_ACCOUNT'}));d=inspect();
    assert.equal(confirmed,1);assert.equal(d.counters.confirmationDelivered,1);assert.equal(d.counters.readKnown,1);assert.equal(d.read.demand,false);assert.equal(d.read.retryTimerActive,false);assert.equal(d.read.pendingReadyConfirmations,0);
    assert.equal(d.lastObservation.source,'confirmation');assert.equal(d.lastObservation.outcome,'confirmed');assert.equal(d.lastObservation.generation,3);assert.equal(d.lastObservation.publicationRevision,4);
    assert(JSON.stringify(d).length<4096);assert(!JSON.stringify(d).includes('PRIVATE_'));
    d.snapshot.phase='suspended';d.counters.readKnown=999;d.read.demand=true;d.lastObservation.outcome='forged';d=inspect();assert.equal(d.snapshot.phase,'ready');assert.equal(d.counters.readKnown,1);assert.equal(d.read.demand,false);assert.equal(d.lastObservation.outcome,'confirmed');
    f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{confirmed++;});await flush();const held=f.pending.shift(),counts=JSON.stringify(inspect().counters),epoch=d.lifetimeEpoch;
    f.stop();d=inspect();assert.equal(d.active,false);assert.equal(d.lifetimeEpoch,epoch+1);assert.equal(d.snapshot,null);assert.equal(d.read.inFlight,false);assert.equal(d.read.demand,false);assert.equal(d.read.pendingReadyConfirmations,0);assert.equal(d.lastObservation.outcome,'stopped');
    held.resolve(fact(99,'ready',99));await flush();assert.equal(JSON.stringify(inspect().counters),counts);assert.equal(confirmed,1);
   }finally{f.stop();}
  });
  await test('diagnostics: actual Settings export passes cached schema through its one existing invoke',async()=>{
   const settings=fs.readFileSync(path.join(root,'src/views/SettingsView.vue'),'utf8');
   assert.match(settings,/import \{ getSharedPowerLifecycleDiagnostics \} from '@\/bridge\/powerLifecycleState'/);
   const body=settings.match(/^async function exportAllLogs\(\): Promise<void> \{[\s\S]*?^\}/m)?.[0];assert(body,'actual Settings export function');
   const compiled=await build({stdin:{contents:body+'\nexport {exportAllLogs};',loader:'ts'},write:false,platform:'node',format:'cjs'});
   const calls=[],m={exports:{}};const context={module:m,exports:m.exports,detailLogBusy:{value:false},detailLogStatus:{value:''},tdpDiagnosticDeliveryFailures:()=>0,frontendDiagnosticDeliveryFailures:()=>0,
    getSharedPowerLifecycleDiagnostics:module.exports.getSharedPowerLifecycleDiagnostics,invoke:async(...args)=>{calls.push(args);assert.equal(args[0],'logs.exportAll');return {ok:true,path:'R:/inert.zip',count:1};}};
   vm.runInNewContext(compiled.outputFiles[0].text,context);await m.exports.exportAllLogs();assert.equal(calls.length,1);assert.equal(calls[0][1].sharedPowerLifecycleDiagnostics.schemaVersion,1);assert.equal(calls[0][1].sharedPowerLifecycleDiagnostics.active,false);assert.equal(calls[0][1].sharedPowerLifecycleDiagnostics.counters.readStarted,0);assert.equal(context.detailLogBusy.value,false);
  });
  return {source:input,sha256:before,actualFactory:true,selection,cases:2,oldCasesExecuted:0};
 }
 if(selection==='ready-admission'){
  await test('ready admission: diagnostic Ready waits for both public gates and accepts inputReady=false',async()=>{
   for(const flags of [{hardwareWritesAllowed:false,notificationPending:false},{hardwareWritesAllowed:true,notificationPending:true},{hardwareWritesAllowed:false,notificationPending:true}]){
    const blocked=fact(8,'ready',10,{inputReady:false,...flags}),f=await boot(blocked);let confirmed=0;
    try{
     f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{confirmed++;});
     for(const delay of [1000,2000,5000,15000,30000,30000]){
      assert.equal(f.timers.size,1,'non-admitted Ready must retain exactly one retry');
      const calls=f.calls;for(let i=0;i<100;i++)f.emit('window.focus');await flush();assert.equal(f.calls,calls);
      assert.equal(await f.fire(),delay);const reading=f.calls;
      for(let i=0;i<100;i++)f.emit('window.focus');await flush();await f.reply(blocked);
      assert.equal(f.calls,reading);assert.equal(confirmed,0,'non-admitted Ready must not confirm');
     }
     await f.fire();await f.reply({...blocked,publicationRevision:11,hardwareWritesAllowed:true,notificationPending:false});
     assert.equal(confirmed,1);assert.equal(f.timers.size,0,'admitted Ready stops retry without a controller');
     assert.equal(f.state().inputReady,false);assert(f.commands.every(command=>command==='power.lifecycle'));
    }finally{f.stop();}
   }
  });
  return {source:input,sha256:before,actualFactory:true,selection,cases:1,oldSixExecuted:0,oldTwoNegativesExecuted:0};
 }
 if(selection==='hardening'){
  await test('hardening: known nonReady retains one capped read-only retry until lost final Ready is read',async()=>{
   for(const phase of ['suspending','suspended','resuming']){
    const f=await boot(fact(3,phase,1));
    try{
     for(const delay of [1000,2000,5000,15000,30000,30000]){
      assert.equal(f.timers.size,1,'known nonReady must retain exactly one retry');
      const calls=f.calls;for(let i=0;i<100;i++)f.emit('window.focus');await flush();
      assert.equal(f.calls,calls,'focus cannot bypass the owned delay');
      assert.equal(await f.fire(),delay);const started=f.calls;
      for(let i=0;i<100;i++)f.emit('window.focus');await flush();
      await f.reply(fact(3,phase,1));assert.equal(f.calls,started,'accepted nonReady cannot spend in-flight hints as immediate reads');
     }
     await f.fire();await f.reply(fact(3,'ready',2));
     assert.equal(f.state().phase,'ready');assert.equal(f.timers.size,0,'healthy Ready stops polling');
     assert(f.commands.every(command=>command==='power.lifecycle'));
    }finally{f.stop();}
   }
  });
  await test('hardening: confirmation excludes cache and a read started before its request',async()=>{
   const f=await boot();let confirmed=0;const seen=f.seen.length;
   try{
    f.source.requestSharedPowerLifecycleReconcile('old-read');await flush();
    f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{confirmed++;});
    assert.equal(confirmed,0);await f.reply(fact());assert.equal(confirmed,0,'old read cannot confirm a newer request');
    await f.reply(fact());assert.equal(confirmed,1);assert.equal(f.timers.size,0);
    assert.equal(f.seen.length,seen,'confirmation is not business replay');
   }finally{f.stop();}
  });
  await test('hardening: unknown, stale and superseded replies cannot confirm; cancellation owns lifetime',async()=>{
   const f=await boot();let confirmed=0;
   try{
    const cancel=f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{confirmed++;});await flush();
    await f.fail();assert.equal(confirmed,0);await f.fire();
    await f.reply({known:false,publicationRevision:2});assert.equal(confirmed,0);
    await f.fire();await f.reply(fact(2,'ready',1));assert.equal(confirmed,0);
    await f.fire();f.emit('power.lifecycleChanged',fact(3,'resuming',3));await f.reply(fact(2,'ready',2));assert.equal(confirmed,0);
    cancel();await f.fire();await f.reply(fact(3,'ready',4));assert.equal(confirmed,0);assert.equal(f.timers.size,0);
    f.source.requestSharedPowerLifecycleReadyConfirmation(()=>{confirmed++;});await flush();
    const held=f.pending.shift();f.stop();const off=f.source.onSharedPowerLifecycle(()=>{});await flush();
    held.resolve(fact(99,'ready',99));await flush();assert.equal(confirmed,0);
    await f.reply(fact(4,'ready',5));assert.equal(confirmed,0);off();
   }finally{f.stop();}
  });
  return {source:input,sha256:before,actualFactory:true,selection,cases:3,legacyCasesExecuted:0};
 }
 await test('shared power: twenty subscribers own one reader and one set of listeners',async()=>{
  const f=fixture(),extra=[];for(let i=0;i<19;++i)extra.push(f.source.onSharedPowerLifecycle(()=>{}));
  await flush();assert.equal(f.calls,1);assert([...f.handlers.values()].every(s=>s.size===1));
  await f.reply(fact(2,'ready',1,{inputReady:false}));assert.equal(f.state().phase,'ready');assert.equal(f.state().inputReady,false);
  extra.forEach(off=>off());f.stop();assert(f.commands.every(c=>c==='power.lifecycle'));
 });
 await test('shared power: late same-generation suspend cannot relock committed Ready',async()=>{
  const f=await boot();f.emit('power.suspending',{generation:2});await flush();
  assert.equal(f.state().phase,'ready');await f.reply(fact());assert.equal(f.state().phase,'ready');f.stop();
 });
 await test('shared power: publication metadata changes do not replay business observers',async()=>{
  const f=await boot(),count=f.seen.length;
  for(let revision=2;revision<202;++revision)f.emit('power.lifecycleChanged',fact(2,'ready',revision,{observedNotificationSequence:revision}));
  await flush();assert.equal(f.seen.length,count);assert.equal(f.state().publicationRevision,201);assert.equal(f.calls,1);assert.equal(f.timers.size,0);f.stop();
 });
 await test('shared power: current common Ready repairs a lost resume in the SAME generation',async()=>{
  const f=await boot(fact(3,'suspended',1));f.emit('window.focus');await flush();await f.fire();await f.reply(fact(3,'ready',2));
  assert.equal(f.state().generation,3);assert.equal(f.state().phase,'ready');f.stop();
 });
 await test('shared power: fresh native facts may correct same-generation terminal bookkeeping',async()=>{
  const f=await boot(fact(4));f.emit('window.focus');await flush();await f.reply(fact(4,'suspended',2));
  assert.equal(f.state().phase,'suspended');f.emit('power.lifecycleChanged',fact(4,'ready',3));assert.equal(f.state().phase,'ready');f.stop();
 });
 await test('shared power: a held old reply cannot release a newer accepted sleep',async()=>{
  const f=await boot();f.emit('window.focus');await flush();
  f.emit('power.lifecycleChanged',fact(3,'suspending',2));await f.reply(fact(2,'ready',1));
  assert.equal(f.state().generation,3);assert.equal(f.state().phase,'suspending');f.stop();
 });
 await test('shared power: older publication and older generation cannot poison the watermark',async()=>{
  const f=await boot(fact(4,'ready',10));f.emit('power.lifecycleChanged',fact(4,'suspended',9));
  f.emit('power.lifecycleChanged',fact(3,'ready',999));assert.equal(f.state().phase,'ready');assert.equal(f.state().publicationRevision,10);
  f.emit('power.lifecycleChanged',fact(5,'suspending',11));assert.equal(f.state().generation,5);f.stop();
 });
 await test('shared power: explicit unknown is not Ready and one retry survives focus storms',async()=>{
  const f=await boot();f.emit('window.focus');await flush();await f.reply({known:false,reason:'power-context-changing',publicationRevision:2});
  assert.equal(f.state().known,false);assert.equal(f.state().inputReady,false);assert.equal(f.timers.size,1);
  const calls=f.calls;for(let i=0;i<1000;++i)f.emit('window.focus');await flush();assert.equal(f.calls,calls);assert.equal(f.timers.size,1);
  assert.equal(await f.fire(),1000);await f.reply(fact(2,'ready',3));assert.equal(f.state().known,true);assert.equal(f.timers.size,0);f.stop();
 });
 await test('shared power: read-only failure observation continues at capped backoff, then heals',async()=>{
  const f=fixture();await flush();const delays=[];
  for(let i=0;i<7;++i){await f.fail();assert.equal(f.timers.size,1);delays.push(await f.fire());}
  assert.deepEqual(delays,[1000,2000,5000,15000,30000,30000,30000]);
  await f.reply(fact(7));assert.equal(f.state().phase,'ready');assert.equal(f.timers.size,0);f.stop();
 });
 await test('shared power: failed in-flight read cannot spend a storm as immediate retries',async()=>{
  const f=fixture();await flush();for(let i=0;i<1000;++i)f.source.requestSharedPowerLifecycleReconcile('repeat-demand');
  await f.fail();assert.equal(f.calls,1);assert.equal(f.timers.size,1);await flush();assert.equal(f.calls,1);f.stop();
 });
 await test('shared power: successful demand storms have only one coalesced successor',async()=>{
  const f=fixture();await flush();for(let i=0;i<1000;++i)f.source.requestSharedPowerLifecycleReconcile('same-read');
  await f.reply(fact());assert.equal(f.calls,2);await f.reply(fact());assert.equal(f.calls,2);assert.equal(f.timers.size,0);f.stop();
 });
 await test('shared power: untagged compatibility notifications cannot invent current Ready',async()=>{
  const f=await boot(fact(2,'suspended',1));f.emit('power.resumed',{});await flush();
  assert.equal(f.state().phase,'suspended');await f.fire();await f.reply(fact(2,'suspended',1));assert.equal(f.state().phase,'suspended');f.stop();
 });
 await test('shared power: disposal retires old reads and the next subscription has a new epoch',async()=>{
  const f=fixture();await flush();const old=f.pending.shift();f.stop();const next=[];
  const off=f.source.onSharedPowerLifecycle(s=>next.push(structuredClone(s)));await flush();old.resolve(fact(99));await flush();assert.equal(next.length,0);
  await f.reply(fact(2));assert.equal(next.at(-1).generation,2);off();assert.equal(f.timers.size,0);assert([...f.handlers.values()].every(s=>s.size===0));
 });
 await test('shared power: a throwing subscriber cannot block other clients or mutate cached facts',async()=>{
  const f=await boot();const values=[];const a=f.source.onSharedPowerLifecycle(s=>{s.phase='suspended';throw Error('inert consumer');});
  const b=f.source.onSharedPowerLifecycle(s=>values.push(s));assert.equal(f.state().phase,'ready');assert.equal(values.at(-1).phase,'ready');
  a();b();f.stop();
 });
 assert.equal(createHash('sha256').update(fs.readFileSync(path.join(root,input))).digest('hex'),before,'Shared power source changed during tests');
 return {source:input,sha256:before,actualFactory:true,osPowerOrDevices:false,injectedPorts:'read-only Native facts, event delivery, logical clock; singleton IPC inactive'};
}
