import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';import {seed} from './fixtures/decky_game_memory_store.mjs';
export async function wakeChainTests(root,out){
 const native=JSON.parse(fs.readFileSync(path.join(out,'native-wake-commit.json'),'utf8'));assert.equal(native.passed,true);assert.equal(native.productionBlockExtracted,true);assert.equal(native.bindingCalls,2);assert.equal(native.preflights,1);
 const req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),compiled=await build({entryPoints:[path.join(root,'decky-plugin/src/mirrorClient.ts')],bundle:true,write:false,platform:'node',format:'esm'}),{MirrorClient}=await import('data:text/javascript;base64,'+Buffer.from(compiled.outputFiles[0].contents).toString('base64'));
 const source=fs.readFileSync(path.join(root,'src/bridge/deckyMirrorHost.ts'),'utf8'),flush=async()=>{for(let i=0;i<260;i++)await Promise.resolve();};
 const powerSubscription='stops.push(onSharedPowerLifecycle(snapshot=>{';assert.equal(source.split(powerSubscription).length,2,'Host must have one shared-power subscription');
 let checks=0;const check=(value,name)=>{checks++;assert(value,name);};
 async function exercise(hostSource=source){
  let connectionSequence=0,current=null,client=null,actionPort='hold',inFlight=null,holdReply=false,heldReply=null;
  const sockets=[],timers=new Map,clientSettlements=[],ownerWrites=[];let timerId=0;
  let bootstrap={endpoint:'ws://127.0.0.1:18223/mirror',token:'a'.repeat(64),runId:'b'.repeat(32)};
  const f=await createMirrorHostFixture({initial:seed(),hostSource,invoke:async(command,args)=>{
    if(command==='deckySidebar.mirrorAttach')return {connected:false,owner:'YMCC-native',pid:8612};
    if(command==='deckySidebar.mirrorReply'){
      if(holdReply&&args.message.type==='reply'){heldReply=structuredClone(args);return true;}
      if(current&&!current.closed&&args.connection===current.connection&&args.message.runId===current.runId)current.receive('message',args.message);return true;
    }
    if(command==='input.shortcutRuntime.clear')return true;throw Error('Unexpected wake-chain IO '+command);
  },readHooks:{
    performanceUpdate:(identity,patch)=>ownerWrites.push({generation:identity.generation,tdp:patch.tdp}),
    performanceReceipt:patch=>[23,27].includes(patch.tdp)?{ok:true,proof:'write-ack',requested:patch.tdp,accepted:patch.tdp}:undefined,
  }});
  f.state.power={...f.state.power,generation:176,phase:'ready',hardwareWritesAllowed:true};
  let publicationRevision=0;
  const publishPower=()=>{
    f.state.power={...f.state.power,known:true,publicationRevision:++publicationRevision,notificationPending:false};
    f.emit('power.lifecycleChanged',structuredClone(f.state.power));
  };
  class Socket{
    handlers=new Map;sent=[];closed=false;connection=String(++connectionSequence);runId=bootstrap.runId;
    constructor(url){this.url=url;sockets.push(this);current=this;}
    addEventListener(name,fn){const list=this.handlers.get(name)||[];list.push(fn);this.handlers.set(name,list);}
    receive(name,value){for(const fn of this.handlers.get(name)||[])fn({data:JSON.stringify(value)});}
    open(){f.emit('deckySidebar.mirrorPeer',{connection:this.connection,runId:this.runId,connected:true,revision:0});this.receive('open');}
    forward(request){f.emit('deckySidebar.mirrorRequest',{connection:this.connection,runId:this.runId,request});}
    send(text){
      const request=JSON.parse(text);this.sent.push(request);
      if(request.command==='snapshot'||actionPort==='forward')this.forward(request);
      else if(actionPort==='delay')inFlight={socket:this,request};
    }
    close(){if(this.closed)return;this.closed=true;f.emit('deckySidebar.mirrorPeer',{connection:this.connection,runId:this.runId,connected:false,revision:0});this.receive('close');}
  }
  client=new MirrorClient({bootstrap:()=>bootstrap,socket:url=>new Socket(url),schedule:(fn,delay)=>{const id=++timerId;timers.set(id,{fn,delay});return id;},cancel:id=>timers.delete(id),clientId:'wake-chain',buildId:'c'.repeat(64),tdpActionSettled:facts=>clientSettlements.push(facts)});
  try{
   f.ready();await flush();client.setVisible(true);sockets[0].open();await flush();
   check(client.snapshot().connected&&client.snapshot().snapshot?.ready===true,'initial production Host and Client reach ready snapshot');
   const pending=client.mutate('performance.setControls',{patch:{tdp:25}}).then(()=>null,e=>e);await flush();
   check(sockets[0].sent.filter(m=>m.command==='performance.setControls').length===1,'one uncertain pre-wake user intent exists in old transport');
   f.state.power={...f.state.power,generation:177,phase:'suspended',hardwareWritesAllowed:false};f.powerChanged(true);publishPower();await flush();
   check(client.snapshot().connected&&client.snapshot().snapshot?.ready===false,'native suspended snapshot keeps authenticated transport but closes controls');
   check(/已连接 YMCC.*等待唤醒完成/.test(client.snapshot().snapshot.notice),'actual held notice distinguishes power gate from disconnected transport');
   for(const event of ['power.resume-ready','power.resuming']){f.emit(event,{generation:177});await flush();}
   check(client.snapshot().snapshot.ready===false,'noncommitted resume events cannot open production controls');
   const nativeReady=native.events.find(e=>e.event==='power.resumed'&&e.data.generation===177);assert(nativeReady);
   // Replay the historical commit; current Native also publishes the shared
   // lifecycle state, which is the authoritative Decky Host input.
   f.state.power={...f.state.power,phase:'ready',hardwareWritesAllowed:true};f.emit(nativeReady.event,nativeReady.data);publishPower();await flush();
   check(client.snapshot().snapshot?.ready===true&&client.snapshot().snapshot.generation===177,'native committed event must release the existing held production snapshot');
   check(f.counts.performanceUpdates===undefined&&f.counts.saves===0,'uncertain user write has not executed or replayed while waking');
   const old=sockets[0];bootstrap={...bootstrap,endpoint:'ws://127.0.0.1:18224/mirror',token:'d'.repeat(64),runId:'e'.repeat(32)};client.bootstrapChanged();sockets[1].open();await flush();
   check(old.closed&&await pending instanceof Error,'new native bootstrap fences and rejects the old uncertain request');
   check(sockets[1].sent.every(m=>m.command==='snapshot'),'replacement transport requests only current state');
   check(client.snapshot().connected&&client.snapshot().snapshot.ready&&client.snapshot().snapshot.runId===bootstrap.runId,'production client completes authenticated generation-matched new snapshot');
   for(const event of native.events.slice(1)){
     if(event.data.generation>f.state.power.generation){f.state.power={...f.state.power,generation:event.data.generation};publishPower();}
     f.emit(event.event,event.data);await flush();
   }
   check(client.snapshot().snapshot.generation===178&&client.snapshot().snapshot.ready,'duplicate and old native notifications read current power generation instead of regressing it');
   check(sockets.length===2&&sockets.flatMap(s=>s.sent).filter(m=>m.command==='performance.setControls').length===1,'wake duplicates create no extra socket and never resend uncertain writes');
   check(f.counts.saves===0&&(f.counts.performanceUpdates??0)===0,'full native-to-Host-to-Client wake chain executes no business write');
   check(timers.size===0,'healthy wake completion leaves no client polling or deadline timer');

   // New intent must reach the original relay and Host, not a mocked reply.
   actionPort='forward';
   const actions=[];
   for(const [tdp,applied,outcome] of [[23,true,'write-ack'],[24,false,'saved-unconfirmed']]){
     const settled=client.mutate('performance.setControls',{patch:{tdp}}).then(result=>({result}),error=>({error}));
     await flush();
     check(!client.snapshot().busy,'new TDP receives a terminal reply without waiting for a timer');
     const {result,error}=await settled;
     const sent=sockets.at(-1).sent.filter(row=>row.command==='performance.setControls').at(-1);
     assert(!error,`TDP ${tdp} (generation ${sent?.generation}, revision ${sent?.revision}, current ${client.snapshot().snapshot?.revision}) refused: ${error?.message}; owner calls ${ownerWrites.length}`);
     check(result.saved===true&&result.applied===applied&&result.pending===!applied,`TDP ${tdp} has the correct saved and applied state`);
     check(f.memory.raw().entries['game.exe'].controls.ac.tdp===tdp&&f.memory.raw().entries['game.exe'].controls.dc.tdp===10,'only the current game AC bank changes');
     const owner=f.diagnostics.filter(row=>row.event==='tdp-owner-settled').at(-1),clientFact=clientSettlements.at(-1);
     check(owner?.generation===178&&owner.requestedW===tdp&&owner.outcome===outcome&&clientFact?.generation===178&&clientFact.outcome===outcome,'host and client distinguish write acknowledgement from saved-only receipt');
     actions.push({tdp,owner,client:clientFact});
     // A receipt can precede the refreshed view. The slider queue waits for
     // that view before another edit; do not simulate a second edit on old data.
     await new Promise(resolve=>setImmediate(resolve));
     const view=client.snapshot().snapshot;
     check(view?.revision>sent.revision&&view.performance?.controls.find(control=>control.key==='tdp')?.value===tdp,'client receives the refreshed saved value before another edit');
   }

   // Hold only delivery, after the original Host/owner completed the write.
   holdReply=true;
   const previous=client.mutate('performance.setControls',{patch:{tdp:27}});
   await flush();
   check(heldReply?.message.ok&&heldReply.message.result.applied&&client.snapshot().busy,'the old TDP owner has an acknowledgement but its client reply is still in flight');
   f.state.power={...f.state.power,generation:179,phase:'suspended',hardwareWritesAllowed:false};f.powerChanged(true);publishPower();await flush();
   const suspended=client.snapshot().snapshot;
   check(client.snapshot().connected&&suspended.generation===179&&!suspended.ready,'same socket publishes the next suspended generation before the old reply arrives');
   check(client.snapshot().notice===suspended.notice,'the suspended notice is visible even while waiting for the old reply');
   f.state.power={...f.state.power,phase:'ready',hardwareWritesAllowed:true};f.powerChanged(true);publishPower();await flush();
   const resumed=client.snapshot().snapshot;
   check(resumed.ready&&resumed.generation===179,'new generation reaches Ready before the old reply arrives');
   check(heldReply.connection===current.connection,'the held reply belongs to the still-connected socket');
   current.receive('message',heldReply.message);holdReply=false;await flush();
   const oldResult=await previous;
   check(!client.snapshot().busy&&client.snapshot().notice===(resumed.notice??''),'an old successful TDP receipt must not replace the new Ready generation notice');
   check(oldResult.applied&&clientSettlements.at(-1)?.generation===178&&clientSettlements.at(-1)?.outcome==='write-ack','the old owner acknowledgement remains in the original operation history');
   check(f.memory.raw().entries['game.exe'].controls.ac.tdp===27&&ownerWrites.length===3,'the completed write is neither rolled back nor replayed');

   actionPort='delay';
   const expired=client.mutate('performance.setControls',{patch:{tdp:26}}).then(()=>null,error=>error);
   check(inFlight?.request.generation===179,'capture the actual in-flight mutation before the next power transition');
   f.state.power={...f.state.power,generation:180,phase:'suspended',hardwareWritesAllowed:false};f.powerChanged(true);publishPower();await flush();
   check(client.snapshot().connected&&client.snapshot().snapshot?.generation===180&&!client.snapshot().snapshot.ready,'next suspension closes the gate without replacing the peer');
   inFlight.socket.forward(inFlight.request);await flush();
   check(!client.snapshot().busy,'relay returns a terminal refusal for the stale wire request');
   check(await expired instanceof Error&&clientSettlements.at(-1)?.outcome==='owner-unconfirmed','client rejects the stale-generation action');
   check(client.snapshot().notice===client.snapshot().snapshot.notice,'the old refusal must not become the new generation operation error');
   check(ownerWrites.length===3&&ownerWrites.every((write,i)=>write.generation===178&&write.tdp===[23,24,27][i])&&f.counts.performanceUpdates===3,'only the three deliberate current-generation actions reach the owner');
   check(f.diagnostics.filter(row=>row.event==='tdp-owner-settled').length===3&&f.memory.raw().entries['game.exe'].controls.ac.tdp===27,'stale delivery neither saves nor produces an owner write receipt');
   check(clientSettlements.map(row=>row.outcome).join(',')==='transport-ended,write-ack,saved-unconfirmed,write-ack,owner-unconfirmed','each sent intent settles once, including the original retired transport');
   check(sockets.length===2&&timers.size===0,'completed actions and stale refusal leave no client retry timer or replacement socket');
   return {nativeGeneration:178,authenticated:true,ready:true,userIntentsSent:1,userIntentsReplayed:0,actualWrites:0,postWakeTdp:{actions,ownerWrites,delayedReceiptGeneration:178,rejectedGeneration:179,currentGeneration:180}};
  }finally{client?.dispose();f.stop();}
 }
 const positive=await exercise();
 // Drop only generation 177 Ready at the actual Host subscription. The
 // original power source still consumes all native signals in this control.
 const negative=source.replace(powerSubscription,`${powerSubscription}\nif(snapshot.generation===177&&isSharedPowerLifecycleAdmitted(snapshot))return;`);assert.notEqual(negative,source);
 let failure='';try{await exercise(negative);}catch(e){failure=e.message;}
 assert.match(failure,/native committed event must release the existing held production snapshot/,'missing production wake event subscription must fail at original held controls');
 const result={passed:true,checks,productionNativeWakeBlock:true,productionHost:true,productionClient:true,committedEventNegativeDetected:true,negativeFailure:failure,positive,actualWindowsSleep:false,actualSteamActions:0,physicalHardwareWrites:0};fs.writeFileSync(path.join(out,'wake-chain.json'),JSON.stringify(result,null,2),{flag:'wx'});return result;
}
