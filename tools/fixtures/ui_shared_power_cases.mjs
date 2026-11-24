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
 vm.runInNewContext(bundle.outputFiles[0].text,{module,exports:module.exports,queueMicrotask,console,Promise,setTimeout(){throw Error('Use the injected clock');},clearTimeout(){throw Error('Use the injected clock');}});
 const create=module.exports.createSharedPowerLifecycleSource;
 const flush=async()=>{for(let i=0;i<35;++i)await Promise.resolve();};
 const fact=(generation=2,phase='ready',revision=1,extra={})=>({known:true,generation,phase,publicationRevision:revision,inputReady:phase==='ready',hardwareWritesAllowed:phase==='ready',...extra});
 function fixture(){
  const handlers=new Map(),timers=new Map(),pending=[],seen=[],commands=[];let calls=0,serial=0;
  const source=create({read(){++calls;commands.push('power.lifecycle');return new Promise((resolve,reject)=>pending.push({resolve,reject}));},
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
  await test('notification scope: opt-in observes current sequence without replaying ordinary business',async()=>{
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
  await test('notification scope: reentrant observation cannot deliver an older scope to remaining opted consumers',async()=>{
   const f=await boot(fact(8,'ready',10,{observedNotificationSequence:21})),seen=[];let armed=false;
   const first=f.source.onSharedPowerLifecycle(value=>{if(armed&&value.observedNotificationSequence===22){armed=false;f.emit('power.lifecycleChanged',fact(9,'ready',12,{observedNotificationSequence:23}));}},{observeNotificationSequence:true});
   const second=f.source.onSharedPowerLifecycle(value=>seen.push(value),{observeNotificationSequence:true});
   try{seen.length=0;armed=true;f.emit('power.lifecycleChanged',fact(9,'ready',11,{observedNotificationSequence:22}));assert(seen.length>0);assert(seen.every(value=>value.observedNotificationSequence===23),'retired notification scope was delivered after reentrant advancement');assert.equal(f.state().observedNotificationSequence,23);assert.equal(f.timers.size,0);}
   finally{first();second();f.stop();}
  });
  await test('notification scope: a newer Ready notification cannot inherit the previous read confirmation',async()=>{
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
  await test('notification scope: Ready recheck requires an accepted read and returns with its subscription',async()=>{
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
  return {source:input,sha256:before,actualFactory:true,selection,cases:4,legacyCasesExecuted:0};
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
