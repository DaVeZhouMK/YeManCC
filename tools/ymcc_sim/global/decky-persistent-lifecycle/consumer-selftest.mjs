import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createMirrorHostFixture} from '../../../fixtures/decky_mirror_host_fixture.mjs';
import {load,seed} from '../../../fixtures/decky_game_memory_store.mjs';
const root=path.resolve(import.meta.dirname,'../../../..');
const read=rel=>fs.readFileSync(path.join(root,rel),'utf8');
const steam=load(read('decky-plugin/src/steamRunningContext.ts'));
const {MirrorClient}=load(read('decky-plugin/src/mirrorClient.ts'),{'./steamRunningContext':steam});
const {ReadOnlyMirrorRelay}=load(read('src/bridge/deckyMirrorRelay.ts'),{'../../decky-plugin/src/steamRunningContext':steam});
const flush=async()=>{for(let i=0;i<240;i++)await Promise.resolve();};
const resourceTotal=s=>Object.values(s).reduce((n,v)=>n+v,0);
export const currentHealth=(state,runId,generation)=>Boolean(state.snapshot?.ready===true&&state.snapshot.runId===runId&&state.snapshot.generation===generation&&state.snapshot.provenance?.kind==='ymcc-native'&&state.snapshot.provenance.pid>0);
export async function lifecycleConsumerTests(nativeResult,options={}){
 assert(options&&typeof options==='object'&&!Array.isArray(options)&&Object.keys(options).every(key=>key==='selection'),'CONSUMER_SELECTION_OPTIONS_INVALID');
 const selection=options.selection??'all';assert(['all','read-recovery'].includes(selection),'CONSUMER_SELECTION_UNKNOWN');
 const cases=[],registeredCaseIds=[],omittedCaseIds=[];
 const check=async(id,action)=>{registeredCaseIds.push(id);if(selection==='read-recovery'&&!id.startsWith('consumer.transient-')&&!id.startsWith('consumer.exception-')&&!id.startsWith('negative.relay-source-churn-')){omittedCaseIds.push(id);return;}try{const measurements=await action();cases.push({id,name:id,passed:measurements.acceptance!==false,measurements});}catch(e){cases.push({id,name:id,passed:false,error:e.stack});}};
 async function fixture({delayedAttach=false}={}){
  const runId='a'.repeat(32),timers=new Map(),sockets=[],delivered=[],pendingAttaches=[];let now=0,nextTimer=0,f;
  const boot={endpoint:'ws://127.0.0.1:1822/mirror',token:'memory-only-token',runId};
  class Socket{
   handlers=new Map();sent=[];closed=false;
   constructor(){this.connection=String(sockets.length+1);sockets.push(this);}
   addEventListener(event,fn){const set=this.handlers.get(event)||new Set();set.add(fn);this.handlers.set(event,set);}
   emit(event,value){for(const fn of this.handlers.get(event)||[])fn({data:typeof value==='string'?value:JSON.stringify(value)});}
   send(text){const request=JSON.parse(text);this.sent.push(request);f.emit('deckySidebar.mirrorRequest',{connection:this.connection,runId,request});}
   close(){this.closed=true;this.emit('close');}
  }
  const client=new MirrorClient({bootstrap:()=>boot,socket:()=>new Socket(),schedule:(action,delay)=>{const id=++nextTimer;timers.set(id,{action,delay,due:now+delay});return id;},cancel:id=>timers.delete(id),clientId:'lifecycle-fixture',now:()=>now});
  f=await createMirrorHostFixture({initial:seed(),invoke:async(command,args)=>{
   if(command==='deckySidebar.mirrorAttach'){
    const peer={connection:sockets.at(-1)?.connection??'1',runId,connected:sockets.length>0,owner:'YMCC-native',pid:8612};
    if(delayedAttach)return new Promise(resolve=>pendingAttaches.push(()=>resolve(peer)));
    return peer;
   }
   if(command==='deckySidebar.mirrorReply'){delivered.push(structuredClone(args));sockets.find(s=>s.connection===args.connection)?.emit('message',args.message);return true;}
   return true;
  }});
  const start=async()=>{f.ready();client.setVisible(true);const socket=sockets.at(-1);f.emit('deckySidebar.mirrorPeer',{connection:socket.connection,runId,connected:true});socket.emit('open');await flush();assert(currentHealth(client.snapshot(),runId,1),'actual host snapshot received by actual client');};
  const advance=async ms=>{const end=now+ms;for(let steps=0;steps<100;steps++){const found=[...timers].filter(([,t])=>t.due<=end).sort((a,b)=>a[1].due-b[1].due)[0];if(!found)break;const [id,t]=found;now=t.due;timers.delete(id);t.action();await flush();}now=end;};
  const counts=()=>({timers:timers.size,subscriptions:resourceTotal(f.observerStats())});
  const stop=()=>{f.stop();client.dispose();};
  return{f,client,runId,timers,sockets,delivered,pendingAttaches,start,advance,counts,stop};
 }
 await check('consumer.persistent-resource-classification',async()=>{
  const x=await fixture();try{await x.start();const held=x.counts();assert(held.subscriptions>0);x.sockets[0].close();await flush();assert(x.timers.size>0);const recovering=x.counts();x.stop();assert.equal(x.counts().subscriptions,0);assert.equal(x.counts().timers,0);return{heldWhileHealthy:held,heldWhileRecovering:recovering,afterStop:x.counts(),evidence:'actual host observers and actual client recovery timers survive idle; source-built native processes measured separately'};}finally{x.stop();}
 });
 await check('consumer.sleep-rejects-write-and-automatically-publishes-current-ready',async()=>{
  const native=nativeResult.cases.find(c=>c.id==='native.committed-ready-executes-original-rebind-consumer');assert.equal(native?.passed,true);
  const x=await fixture();try{await x.start();const old=structuredClone(x.client.snapshot());const baseWrites=x.f.counts.performanceUpdates??0;x.f.state.power={...x.f.state.power,generation:2,phase:'suspended',hardwareWritesAllowed:false};x.f.powerChanged(true);await flush();const held=x.client.snapshot().snapshot;assert.equal(held.ready,false);
   await assert.rejects(x.client.mutate('performance.setControls',{patch:{tdp:25}}));
   // Also reach the owner directly, with its actual power-held snapshot.
   const beforeReplies=x.f.replies.length;x.f.emit('deckySidebar.mirrorRequest',{connection:x.sockets[0].connection,runId:x.runId,request:{command:'performance.setControls',runId:x.runId,id:'held:1',clientId:'held',sequence:1,generation:held.generation,revision:held.revision,identity:held.game?.identity??'',args:{patch:{tdp:25}}}});await flush();
   const denial=x.f.replies.slice(beforeReplies).find(r=>r.message.type==='reply');assert.equal(denial?.message.ok,false);assert.equal(x.f.counts.performanceUpdates??0,baseWrites);
   const snapshotRequests=x.sockets.flatMap(s=>s.sent).filter(r=>r.command==='snapshot').length,replyMark=x.delivered.length;
   x.f.state.power={...x.f.state.power,phase:'ready',hardwareWritesAllowed:true};x.f.emit('power.resumed',{generation:2});await flush();
   const after=x.client.snapshot(),newSnapshots=x.delivered.slice(replyMark).filter(r=>r.message.type==='snapshot');assert(currentHealth(after,x.runId,2));assert(after.snapshot.revision>old.snapshot.revision);assert(newSnapshots.length>0);assert.equal(x.sockets.flatMap(s=>s.sent).filter(r=>r.command==='snapshot').length,snapshotRequests);assert.equal(x.f.counts.performanceUpdates??0,baseWrites);
   // The same original scenario also composes repeated power intents with a
   // transport outage. These are memory S3/S0 signals, not physical sleep proof.
   const compound=[];
   for(let cycle=0;cycle<12;cycle++){
    const generation=cycle+3,kind=cycle%2?'S0':'S3',reconnect=cycle%3!==0;
    x.f.state.power={...x.f.state.power,generation,phase:kind==='S3'?'suspended':'suspending',hardwareWritesAllowed:false};
    x.f.powerChanged(true);await flush();
    assert.equal(x.client.snapshot().snapshot.ready,false);
    await assert.rejects(x.client.mutate('performance.setControls',{patch:{tdp:25}}));
    let retired,late=[];
    if(reconnect){
     retired=x.sockets.at(-1);retired.close();
     x.f.emit('deckySidebar.mirrorPeer',{connection:retired.connection,runId:x.runId,connected:false});
     late=[...x.timers.values()].map(t=>t.action);
     assert.equal(x.timers.size,1);await x.advance([...x.timers.values()][0].delay);
     const socket=x.sockets.at(-1);assert.notEqual(socket,retired);
     x.f.emit('deckySidebar.mirrorPeer',{connection:socket.connection,runId:x.runId,connected:true});
     socket.emit('open');await flush();
     assert.equal(x.client.snapshot().snapshot.ready,false);
     await assert.rejects(x.client.mutate('performance.setControls',{patch:{tdp:25}}));
    }
    x.f.state.power={...x.f.state.power,phase:'resuming',hardwareWritesAllowed:false};
    x.f.powerChanged(true);await flush();assert.equal(x.client.snapshot().snapshot.ready,false);
    const requestMark=x.sockets.flatMap(s=>s.sent).filter(r=>r.command==='snapshot').length;
    const deliveredMark=x.delivered.length;
    x.f.state.power={...x.f.state.power,phase:'ready',hardwareWritesAllowed:true};
    for(let storm=0;storm<16;storm++)x.f.emit('power.resumed',{generation});
    await flush();assert(currentHealth(x.client.snapshot(),x.runId,generation));
    for(const callback of late)callback();
    if(retired){retired.emit('open');retired.emit('message',{type:'snapshot',runId:x.runId,snapshot:old.snapshot});retired.close();}
    await flush();assert(currentHealth(x.client.snapshot(),x.runId,generation));
    assert.equal(x.sockets.flatMap(s=>s.sent).filter(r=>r.command==='snapshot').length,requestMark);
    assert.equal(x.f.counts.performanceUpdates??0,baseWrites);assert.equal(x.timers.size,0);
    assert.equal(x.delivered.slice(deliveredMark).filter(r=>r.message.type==='snapshot').length,1,'Ready event storm must coalesce the current snapshot');
    compound.push({kind,generation,reconnect,lateCallbacks:late.length,healthy:true,automaticReadySnapshots:1,writes:0});
   }
   const final=x.client.snapshot(),expectedGeneration=14;
   return{desiredEnabled:true,sleepObserved:held.ready===false,readyGranted:true,recovered:true,healthObserved:currentHealth(final,x.runId,expectedGeneration),currentGeneration:final.snapshot.generation===expectedGeneration,observedGeneration:final.snapshot.generation,expectedGeneration,runId:final.snapshot.runId,revisionBefore:old.snapshot.revision,revisionAfter:final.snapshot.revision,initialNativeCommittedGeneration:2,attempts:native.measurements.bindingCallsAfterReady-native.measurements.bindingCallsBefore,maxAttempts:1,attemptBudgetScope:'one rebind per committed generation; native duplicate event also executed',automaticSnapshots:newSnapshots.length,explicitReadRequestsAfterReady:0,writesWhileSleeping:(x.f.counts.performanceUpdates??0)-baseWrites,ownerDenial:denial.message,compoundRecovery:compound,physicalSleepPerformed:false,unconfirmedWritesReplayed:0,healthOrigin:'production host read -> production relay -> production MirrorClient; memory IPC/socket boundaries'};
  }finally{x.stop();}
 });
 await check('negative.connected-without-ready-snapshot-is-not-health',async()=>{
  const x=await fixture();try{x.client.setVisible(true);x.sockets[0].emit('open');await flush();const s=x.client.snapshot();assert.equal(s.connected,true);assert.equal(s.snapshot?.ready??false,false);assert.equal(currentHealth(s,x.runId,1),false);return{connected:s.connected,healthObserved:false,correctlyRejected:true,readySnapshotReceived:false};}finally{x.stop();}
 });
 await check('negative.recorded-pre-sleep-ready-is-not-current-generation-health',async()=>{
  const x=await fixture();try{await x.start();const actualOld=structuredClone(x.client.snapshot());assert(currentHealth(actualOld,x.runId,1));assert.equal(currentHealth(actualOld,x.runId,2),false);x.sockets[0].emit('message',{type:'snapshot',runId:x.runId,snapshot:actualOld.snapshot});await flush();assert.equal(currentHealth(x.client.snapshot(),x.runId,2),false);return{recordedSnapshotReady:actualOld.snapshot.ready,recordedGeneration:actualOld.snapshot.generation,expectedGeneration:2,healthObserved:false,correctlyRejected:true};}finally{x.stop();}
 });
 await check('consumer.exit-disposes-observers-and-delivered-late-recovery-callbacks',async()=>{
  const x=await fixture();await x.start();x.sockets[0].close();await flush();const late=[...x.timers.values()].map(t=>t.action);assert(late.length>0);const saved=x.sockets[0],before=x.sockets.length;const held=x.counts();x.stop();for(const callback of late)callback();saved.emit('open');saved.emit('close');x.f.emit('power.resumed',{generation:3});x.f.emit('deckySidebar.mirrorPeer',{connection:'late',runId:x.runId,connected:true});await flush();const remaining=x.counts();assert.equal(remaining.timers,0);assert.equal(remaining.subscriptions,0);assert.equal(x.sockets.length,before);assert.equal(x.f.counts.performanceUpdates??0,0);return{heldBeforeExit:held,remainingOwnedResources:remaining,lateCallbacksDelivered:late.length+4,restartsAfterExit:x.sockets.length-before,bounded:true,ownerRevoked:true};
 });
 await check('consumer.late-attach-resolution-cannot-recreate-stopped-host',async()=>{
  const x=await fixture({delayedAttach:true});x.f.ready();assert(x.pendingAttaches.length>0);const delivered=x.pendingAttaches.length;x.stop();for(const resolve of x.pendingAttaches)resolve();await flush();assert.equal(x.counts().subscriptions,0);assert.equal(x.f.replies.length,0);return{lateCallbacksDelivered:delivered,restartsAfterExit:0,remainingOwnedResources:x.counts(),bounded:true,ownerRevoked:true};
 });
 await check('consumer.transient-source-churn-recovers-through-original-two-retry-relay',async()=>{
  // Reuse the real host-produced healthy snapshot as the relay read boundary;
  // no synthetic ready JSON is used to establish health.
  const x=await fixture();try{await x.start();const produced=x.client.snapshot().snapshot,{runId,revision,...data}=produced;const timers=new Map(),replies=[];let reads=0,id=0,observers=0,now=0;
   const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;if(reads<=3)relay.invalidate();return structuredClone(data);},observe:()=>{observers++;return()=>observers--;},reply:async(connection,message)=>{replies.push({connection,message});return true;},schedule:(action,delay)=>{const key=++id;timers.set(key,{action,delay});return key;},cancel:key=>timers.delete(key),now:()=>now});
   try{const peer={connection:'31',runId:x.runId,connected:true};relay.connected(peer);await relay.request({...peer,request:{command:'snapshot',runId:x.runId}});await flush();assert.equal(timers.size,1);const t=[...timers.values()][0];assert.equal(t.delay,250);timers.clear();now+=t.delay;t.action();await flush();const actual=replies.filter(r=>r.message.type==='snapshot').at(-1)?.message.snapshot;assert.equal(actual?.ready,true);assert.equal(actual?.generation,1);assert.equal(actual?.runId,x.runId);assert.equal(actual?.provenance.kind,'ymcc-native');assert.equal(reads,4);assert.equal(timers.size,0);
    return{desiredEnabled:true,recovered:true,healthObserved:actual.ready===true,currentGeneration:actual.generation===produced.generation&&actual.runId===produced.runId,attempts:1,maxAttempts:2,totalReadAttempts:reads,maxTotalReadAttempts:9,attemptBudgetScope:'one fast read observation within the original two-slot fast window; current health ends observation',unconfirmedWritesReplayed:0,readySnapshotOrigin:'captured actual production host snapshot consumed by production relay after three source invalidations during the initial read'};
   }finally{relay.dispose();assert.equal(observers,0);assert.equal(timers.size,0);}
  }finally{x.stop();}
 });
 await check('consumer.transient-read-exception-does-not-self-recover',async()=>{
  const x=await fixture();try{await x.start();const actual=x.client.snapshot().snapshot,{runId,revision,...data}=actual;
  const timers=new Map(),replies=[];let reads=0,next=0,now=0;const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;if(reads<=3)throw Error('inert read exception beyond the two fast observations');return structuredClone(data);},observe:()=>()=>{},reply:async(connection,message)=>{replies.push(message);return true;},schedule:(action,delay)=>{const id=++next;timers.set(id,{action,delay});return id;},cancel:id=>timers.delete(id),now:()=>now});
  try{const peer={connection:'32',runId:x.runId,connected:true};relay.connected(peer);await relay.request({...peer,request:{command:'snapshot',runId:peer.runId}});await flush();const delays=[];for(let n=0;n<3&&timers.size;n++){const t=[...timers.values()][0];timers.clear();delays.push(t.delay);now+=t.delay;t.action();await flush();}
   assert.deepEqual(delays,[250,1000,3000]);assert.equal(reads,4);assert.equal(timers.size,0);
   const snap=replies.filter(r=>r.type==='snapshot').at(-1)?.snapshot;const healthy=snap?.ready===true&&snap.generation===actual.generation&&snap.runId===actual.runId;return{acceptance:healthy,desiredEnabled:true,recovered:healthy,healthObserved:healthy,currentGeneration:snap?.generation===actual.generation&&snap?.runId===actual.runId,attempts:delays.filter(delay=>delay<=1000).length,maxAttempts:2,attemptBudgetScope:'original fast window only; later capped read observations neither replay business writes nor renew an action budget',readObservationAttempts:delays.length,slowObservations:delays.filter(delay=>delay>1000).length,readAttempts:reads,remainingRecoveryTimers:timers.size,retryDelays:delays,unconfirmedWritesReplayed:0,observedReady:snap?.ready,observedGeneration:snap?.generation,sourceWouldBeHealthyOnNextRead:true,gap:healthy?undefined:'One actual read exception publishes ready=false generation=0 and schedules no automatic retry, although the next boundary read would return the actual healthy host snapshot'};
  }finally{relay.dispose();}
  }finally{x.stop();}
 });
 await check('negative.relay-source-churn-exhausts-original-two-retry-budget',async()=>{
  const timers=new Map(),replies=[];let reads=0,next=0,now=0;const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;relay.invalidate();return {generation:1,ready:false,game:null,fan:{supported:false,enabled:false,preset:'',choices:[]}};},observe:()=>()=>{},reply:async(_connection,message)=>{replies.push(message);return true;},schedule:(action,delay)=>{const id=++next;timers.set(id,{action,delay});return id;},cancel:id=>timers.delete(id),now:()=>now});
  try{const peer={connection:'33',runId:'b'.repeat(32),connected:true};relay.connected(peer);await relay.request({...peer,request:{command:'snapshot',runId:peer.runId}});await flush();assert.equal(reads,3);const delays=[];
   const observe=async expected=>{assert.equal(timers.size,1);const [id,t]=[...timers][0];assert.equal(t.delay,expected);timers.delete(id);now+=t.delay;const before=reads;delays.push(t.delay);t.action();await flush();assert.equal(reads-before,3,'one source-churn read batch retains its original three-attempt bound');assert.equal(timers.size,1);};
   await observe(250);await observe(1000);assert.equal(reads,9);assert.deepEqual(delays,[250,1000]);
   for(const delay of [3000,10000,30000,30000])await observe(delay);
   assert.equal([...timers.values()][0].delay,30000);const heldTimer=[...timers.keys()][0],beforeStorm=reads;
   for(let n=0;n<20;n++){relay.invalidate();await relay.request({...peer,request:{command:'snapshot',runId:peer.runId}});await flush();}
   assert.equal(reads,beforeStorm,'repeated generic source/snapshot traffic cannot bypass a pending observation');assert.equal(timers.size,1);assert.equal([...timers.keys()][0],heldTimer);const snapshots=replies.filter(row=>row.type==='snapshot');assert(snapshots.length>0);assert(snapshots.every(row=>row.snapshot.ready===false));
   const heldReadObserverTimers=timers.size;relay.dispose();assert.equal(timers.size,0);
   return{readAttempts:reads,recoveryAttempts:2,maxRecoveryAttempts:2,attemptBudgetScope:'two fast slots only; one capped read-only observer remains until owner disposal',fastWindowExhausted:true,readObservations:delays.length,slowObservations:delays.length-2,delays,nextObservationDelayMs:30000,sourceChurnAttemptsPerBatch:3,stormRequests:20,additionalReadsFromStorm:reads-beforeStorm,heldReadObserverTimers,remainingTimers:timers.size,limitReached:true,unconfirmedWritesReplayed:0};
  }finally{relay.dispose();assert.equal(timers.size,0);}
 });

 // These controls execute the same production relay after an exception. They
 // do not reset its budget or call its private recovery method.
 function exceptionRelay(){
  const timers=new Map(),replies=[];let reads=0,next=0,observers=0,now=0;
  const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;throw Error('persistent read failure');},observe:()=>{observers++;return()=>observers--;},reply:async(connection,message)=>{replies.push(message);return true;},schedule:(action,delay)=>{const id=++next;timers.set(id,{action,delay});return id;},cancel:id=>timers.delete(id),now:()=>now});
  const peer={connection:'34',runId:'c'.repeat(32),connected:true};relay.connected(peer);
  return{relay,peer,timers,replies,reads:()=>reads,observers:()=>observers,elapse:ms=>{now+=ms;},request:()=>relay.request({...peer,request:{command:'snapshot',runId:peer.runId}})};
 }
 await check('consumer.exception-exhausts-original-two-retry-budget',async()=>{
  const x=exceptionRelay();try{await x.request();await flush();const delays=[];
   const observe=async expected=>{assert.equal(x.timers.size,1);const [id,t]=[...x.timers][0];assert.equal(t.delay,expected);x.timers.delete(id);x.elapse(t.delay);const before=x.reads();delays.push(t.delay);t.action();await flush();assert.equal(x.reads()-before,1);assert.equal(x.timers.size,1);};
   await observe(250);await observe(1000);assert.equal(x.reads(),3);assert.deepEqual(delays,[250,1000]);
   for(const delay of [3000,10000,30000,30000])await observe(delay);
   assert.equal([...x.timers.values()][0].delay,30000);const heldTimer=[...x.timers.keys()][0],beforeStorm=x.reads();
   for(let n=0;n<20;n++){await x.request();await flush();}
   assert.equal(x.reads(),beforeStorm,'repeated failed snapshots cannot bypass the read observer');assert.equal(x.timers.size,1);assert.equal([...x.timers.keys()][0],heldTimer);assert(x.replies.every(r=>r.snapshot.ready===false&&r.snapshot.generation===0));assert.equal(x.observers(),1);
   const heldReadObserverTimers=x.timers.size;x.relay.dispose();assert.equal(x.observers(),0);assert.equal(x.timers.size,0);
   return{readAttempts:x.reads(),recoveryAttempts:2,maxRecoveryAttempts:2,attemptBudgetScope:'original two fast observations only; fault-only observation has a capped rate and separate owner lifetime',fastWindowExhausted:true,readObservations:delays.length,slowObservations:delays.length-2,delays,nextObservationDelayMs:30000,stormRequests:20,additionalReadsFromStorm:x.reads()-beforeStorm,heldReadObserverTimers,remainingTimers:x.timers.size,remainingSubscriptions:x.observers(),limitReached:true,unconfirmedWritesReplayed:0};
  }finally{x.relay.dispose();assert.equal(x.observers(),0);assert.equal(x.timers.size,0);}
 });
 for(const stop of ['paused-demand','detached-peer','disposed'])await check('consumer.exception-late-retry-cannot-revive-'+stop,async()=>{
  const x=exceptionRelay();try{await x.request();await flush();assert.equal(x.timers.size,1);const callback=[...x.timers.values()][0].action;
   if(stop==='paused-demand')await x.relay.request({...x.peer,request:{command:'snapshot',runId:x.peer.runId,subscribe:false}});
   else if(stop==='detached-peer')x.relay.connected({...x.peer,connected:false});else x.relay.dispose();
   const deliveries=x.replies.length;assert.equal(x.timers.size,0);x.elapse(1000);callback();await flush();assert.equal(x.reads(),1);assert.equal(x.replies.length,deliveries);assert.equal(x.timers.size,0);assert.equal(x.observers(),0);
   return{stop,lateCallbacksDelivered:1,additionalReads:0,additionalDeliveries:0,remainingTimers:0,remainingSubscriptions:0,unconfirmedWritesReplayed:0};
  }finally{x.relay.dispose();}
 });
 assert(cases.length>0,'CONSUMER_SELECTION_EMPTY');assert.equal(new Set(registeredCaseIds).size,registeredCaseIds.length,'DUPLICATE_CONSUMER_ID');
 return{schemaVersion:1,passed:cases.every(c=>c.passed),selection:{mode:selection,registeredCaseIds,actualCaseIds:cases.map(row=>row.id),omittedCaseIds},fullConsumerGroupExecuted:omittedCaseIds.length===0,nativeEvidenceProvided:nativeResult!=null,productionHostExecuted:true,productionClientExecuted:true,productionRelayExecuted:true,physicalSleepPerformed:false,hardwareWrites:0,cases};
}
