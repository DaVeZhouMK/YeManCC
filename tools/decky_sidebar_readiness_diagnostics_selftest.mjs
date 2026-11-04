// Readiness telemetry and independent snapshot reads: actual relay/client/host,
// memory IO/clock only. No Steam, business configuration or devices are touched.
import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';
// Register original checks before loading fixtures so invalid selections cannot
// prepare an output tree or execute a valid callback before an unknown name.
let root,load,createMirrorHostFixture,createMirrorSpeedMemoryOwner,ReadOnlyMirrorRelay,MirrorClient;
const src=p=>fs.readFileSync(path.join(root,p),'utf8');
const runId='a'.repeat(32),peer={connection:'1',runId,connected:true},flush=async()=>{for(let n=0;n<180;n++)await Promise.resolve();await new Promise(resolve=>setImmediate(resolve));};
const defer=()=>{let resolve,reject;const promise=new Promise((r,j)=>{resolve=r;reject=j;});return {promise,resolve,reject};};
const args=process.argv.slice(2),requestedCases=[],groups=[];
let selectionError='',speedCompletionOnly=false;
const speedCompletionCase='read recovery: a late old-game read after a power generation change cannot restore old admission';
const speedCompletionMeasurements=[];
for(let i=0;i<args.length;i++){
 const arg=args[i];
 if(arg==='--case'){
  if(!args[i+1]||args[i+1].startsWith('--')){selectionError='--case requires one exact original case name';break;}
  requestedCases.push(args[++i]);
 }else if(arg==='--speed-completion'){if(speedCompletionOnly){selectionError='Duplicate speed completion selection';break;}speedCompletionOnly=true;}
 else if(arg==='--read-recovery'||arg==='--legacy-diagnostics')groups.push(arg.slice(2));
 else{selectionError='Unknown readiness selection argument: '+arg;break;}
}
if(groups.length>1||groups.length&&requestedCases.length)selectionError='Readiness group and exact-case selections are mutually exclusive';
if(new Set(requestedCases).size!==requestedCases.length)selectionError='Duplicate exact readiness case selection';
if(speedCompletionOnly&&(groups.length||requestedCases.length!==1||requestedCases[0]!==speedCompletionCase))selectionError='--speed-completion requires only its original exact generation case';
const selection=requestedCases.length?'cases':groups[0]??'all';
const readRecoveryOnly=selection==='read-recovery',legacyOnly=selection==='legacy-diagnostics';
const registry=[],cases=[],skipped=[],selectedResults=[];
const check=async(name,fn)=>{registry.push({name,fn});};
const data=()=>({generation:1,ready:true,game:null,fan:{supported:false,enabled:false,preset:'balanced',choices:[]}});
function relayFixture(read=async()=>data(),diagnosticThrows=false){const logs=[],replies=[],timers=new Map();let now=0,watcher=null,reads=0,nextTimer=0;
 const relay=new ReadOnlyMirrorRelay({read:async trace=>{reads++;return read(trace,()=>now,delta=>{now+=delta;});},now:()=>now,observe:fn=>{watcher=fn;return()=>watcher=null;},reply:async(_c,m)=>{replies.push(m);return true;},diagnostic:(event,detail)=>{if(diagnosticThrows)throw Error('Memory log offline');logs.push({event,...structuredClone(detail)});},schedule:(fn,ms)=>{const id=++nextTimer;timers.set(id,{fn,ms});return id;},cancel:id=>timers.delete(id)});
 const f={relay,logs,replies,timers,fire:async ms=>{const entry=[...timers].find(([,timer])=>timer.ms===ms);assert(entry,'Missing read retry '+ms);timers.delete(entry[0]);now+=ms;entry[1].fn();await flush();},get reads(){return reads;},now:()=>now,advance:delta=>now+=delta,open:()=>relay.request({...peer,request:{command:'snapshot',runId}}),pause:()=>relay.request({...peer,request:{command:'snapshot',runId,subscribe:false}}),invalidate:async()=>{watcher?.();await flush();},dispose:()=>relay.dispose()};return f;}
await check('first demand records one start/finish with correlated read ID and stage timings',async()=>{const f=relayFixture(async(trace,_now,advance)=>{await trace.measure('power',async()=>advance(25));await trace.measure('frames',async()=>advance(40));return data();});try{await f.open();const a=f.logs.find(x=>x.event==='snapshot-read-start'),b=f.logs.find(x=>x.event==='snapshot-read-finished');assert.equal(a.readId,b.readId);assert.equal(a.demandEpoch,b.demandEpoch);assert.equal(b.elapsedMs,65);assert.equal(b.powerMs,25);assert.equal(b.framesMs,40);assert.equal(b.ok,true);assert.equal(b.discarded,false);assert.equal(f.timers.size,0);}finally{f.dispose();}});
await check('1000 healthy fast refreshes do not create 1000 telemetry records or a polling timer',async()=>{const f=relayFixture();try{await f.open();const before=f.logs.length;for(let i=0;i<1000;i++)await f.invalidate();assert.equal(f.logs.length,before);assert.equal(f.timers.size,0);}finally{f.dispose();}});
await check('slow reads are recorded at most once per ten seconds outside first demand',async()=>{const f=relayFixture(async(trace,_now,advance)=>{await trace.measure('monitor',async()=>advance(300));return data();});try{await f.open();for(let i=0;i<10;i++)await f.invalidate();assert.equal(f.logs.filter(x=>x.event==='snapshot-read-finished').length,1);f.advance(10000);await f.invalidate();assert.equal(f.logs.filter(x=>x.event==='snapshot-read-finished').length,2);assert.equal(f.logs.at(-1).monitorMs,300);}finally{f.dispose();}});
await check('pause/reopen records the real demand transition and first read even inside the slow-log cooldown',async()=>{const f=relayFixture();try{await f.open();await f.pause();await f.pause();await f.open();const demands=f.logs.filter(x=>x.event==='snapshot-demand');assert.deepEqual(demands.map(x=>x.subscribe),[true,false,true]);assert.equal(f.logs.filter(x=>x.event==='snapshot-read-start').length,2);assert.equal(f.logs.filter(x=>x.event==='snapshot-read-finished').length,2);}finally{f.dispose();}});
await check('a blocked first read has a start record until completion, while hiding discards its eventual result',async()=>{const done=defer(),f=relayFixture(async trace=>{await trace.measure('frames',()=>done.promise);return data();});try{const open=f.open();await flush();assert.equal(f.logs.filter(x=>x.event==='snapshot-read-start').length,1);assert.equal(f.logs.filter(x=>x.event==='snapshot-read-finished').length,0);await f.pause();done.resolve();await open;const end=f.logs.find(x=>x.event==='snapshot-read-finished');assert.equal(end.discarded,true);assert.equal(f.replies.length,0);assert.equal(end.demandEpoch,f.logs.find(x=>x.event==='snapshot-read-start').demandEpoch);}finally{done.resolve();f.dispose();}});
await check('partial owner errors retain unrelated controls and produce a fixed partial stage rather than raw messages',async()=>{const f=relayFixture(async trace=>{try{await trace.measure('touchpads',async()=>{throw Error('PRIVATE/file/path');});}catch{}return data();});try{await f.open();assert.equal(f.replies.at(-1).snapshot.ready,true);const log=f.logs.find(x=>x.event==='snapshot-read-finished');assert.equal(log.ok,false);assert.equal(log.stage,'partial');assert(!JSON.stringify(f.logs).includes('PRIVATE'));}finally{f.dispose();}});
await check('complete read failure records failed and preserves the original not-ready fallback',async()=>{const f=relayFixture(async()=>{throw Error('PRIVATE');});try{await f.open();assert.equal(f.replies.at(-1).snapshot.ready,false);const end=f.logs.find(x=>x.event==='snapshot-read-finished');assert.equal(end.stage,'failed');assert.equal(end.ok,false);assert(!JSON.stringify(f.logs).includes('PRIVATE'));}finally{f.dispose();}});
await check('diagnostic transport failures cannot fail an otherwise healthy snapshot',async()=>{const f=relayFixture(undefined,true);try{await f.open();assert.equal(f.replies.at(-1).snapshot.ready,true);assert.equal(f.timers.size,0);}finally{f.dispose();}});
const request=async f=>{f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;};
await check('independent control reads begin concurrently before a delayed frame owner returns',async()=>{const frames=defer(),input=defer(),monitor=defer();const f=await createMirrorHostFixture({readHooks:{frames:()=>frames.promise,input:()=>input.promise,monitor:()=>monitor.promise}});try{f.ready();const pending=request(f);await flush();assert.equal(f.counts.frameReads,1);assert.equal(f.counts.inputReads,1);assert.equal(f.counts.monitorReads,1);input.resolve();await flush();assert.equal(f.counts.touchpadReads,1);assert.equal(f.replies.filter(x=>x.message.type==='snapshot').length,0);frames.resolve();monitor.resolve();await pending;await flush();const snapshot=f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;assert(snapshot?.ready);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);}finally{frames.resolve();input.resolve();monitor.resolve();f.stop();}});
let beforeCounts=null,afterCounts;
const completionMeasurements=[];
const transportMeasurements=[];
const audioGenerationMeasurements=[];
await check('one no-game snapshot reuses input and schedule rather than loading each twice',async()=>{
 const f=await createMirrorHostFixture();
 try{
  f.state.game=null;f.ready();const snapshot=await request(f);await flush();
  assert(snapshot?.ready);assert.equal(snapshot.game,null);
  afterCounts={input:f.counts.inputReads,schedule:f.counts.schedule,frames:f.counts.frameReads,monitor:f.counts.monitorReads};
  assert.deepEqual(afterCounts,{input:1,schedule:1,frames:1,monitor:1});
  assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
 }finally{f.stop();}

 // Controlled completion order, using the original read hooks/owner clock.
 const snapshots=f=>f.replies.filter(row=>row.message.type==='snapshot').map(row=>row.message.snapshot);
 const counts=f=>({input:f.counts.inputReads,frames:f.counts.frameReads,monitor:f.counts.monitorReads});
 {
  const audio=defer(),frames=defer(),program=defer();let programReads=0;
  const f=await createMirrorHostFixture({readHooks:{fxSound:()=>audio.promise,frames:()=>frames.promise,losslessProgram:()=>{programReads++;return program.promise;}}});
  try{
   f.state.game=null;f.ready();const pending=request(f);await flush();assert.equal(f.counts.fxSoundReads,1);assert.equal(f.counts.frameReads,0);
   audio.resolve();await flush();assert.equal(snapshots(f).length,0);
   assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});assert.equal(programReads,1);
   f.state.losslessProgram={known:true,running:true};program.resolve();await flush();assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});
   frames.resolve();await pending;await flush();const current=snapshots(f).at(-1);
   assert(current?.ready);assert.deepEqual(current.losslessProgram,{known:true,running:true});assert.equal(current.audioOptimization.known,true);
   assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});assert.equal(f.counts.fxSoundReads,1);assert.equal(programReads,1);
   assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.fxSoundWrites,0);
   completionMeasurements.push({timing:'program-before-controls',counts:counts(f),programReads,publishedCurrent:true});
  }finally{audio.resolve();program.resolve();frames.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
 {
  const program=defer();let programReads=0;
  const f=await createMirrorHostFixture({readHooks:{losslessProgram:()=>{programReads++;return program.promise;}}});
  try{
   f.state.game=null;f.ready();await request(f);await flush();const initial=snapshots(f).at(-1);
   assert(initial?.ready);assert.equal(initial.losslessProgram.known,false);assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});
   const boundary=snapshots(f).length;f.state.losslessProgram={known:true,running:true};program.resolve();await flush();
   const updates=snapshots(f).slice(boundary);assert.equal(updates.length,1,'Completion after a published pending view must refresh it once');
   assert.deepEqual(updates[0].losslessProgram,{known:true,running:true});assert.deepEqual(counts(f),{input:2,frames:2,monitor:2});assert.equal(programReads,1);
   await flush();assert.deepEqual(counts(f),{input:2,frames:2,monitor:2});assert.equal(f.counts.saves,0);assert.equal(f.counts.fxSoundWrites,0);
   completionMeasurements.push({timing:'program-after-snapshot',counts:counts(f),programReads,followingSnapshots:updates.length});
  }finally{program.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
 {
  const initialAudio=defer(),frames=defer();let audioReads=0;
  const f=await createMirrorHostFixture({readHooks:{frames:()=>frames.promise,fxSound:async()=>{audioReads++;if(audioReads===1)await initialAudio.promise;}}});
  try{
   f.state.game=null;f.ready();const pending=request(f);await flush();assert.equal(audioReads,1);assert.equal(f.counts.frameReads,0);
   initialAudio.resolve();await flush();assert.equal(f.fxTimerCount(),1);assert.equal(snapshots(f).length,0);
   f.state.fxSound={...f.state.fxSound,running:true,enabled:true};f.advanceFx();await flush();assert.equal(audioReads,2,'Actual existing audio tick must complete before the blocked frames');
   assert.deepEqual(counts(f),{input:1,frames:1,monitor:1});frames.resolve();await pending;await flush();
   const views=snapshots(f);assert(views.length);assert(views.every(view=>view.ready&&view.audioOptimization.enabled===true),'An audio change after capture must discard every old admitted view');
   assert.deepEqual(counts(f),{input:2,frames:2,monitor:2});assert.equal(audioReads,3,'Initial shared read, original tick and one current retry only');
   assert.equal(f.counts.saves,0);assert.equal(f.counts.fxSoundWrites,0);
   completionMeasurements.push({timing:'audio-after-capture',counts:counts(f),audioReads,noStalePublication:true});
  }finally{initialAudio.resolve();frames.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
});
await check('a late read from the old game cannot publish stale identity after a game switch',async()=>{const gate=defer();let calls=0;const f=await createMirrorHostFixture({readHooks:{frames:()=>++calls===1?gate.promise:Promise.resolve()}});try{f.ready();const pending=request(f);await flush();f.state.game={...f.state.game,pid:202,processCreated:'birth-B',path:'C:/Other/Game.exe'};f.gameChanged();gate.resolve();await pending;await flush();const snapshots=f.replies.filter(x=>x.message.type==='snapshot').map(x=>x.message.snapshot);assert(snapshots.length);assert(snapshots.every(x=>!x.game||x.game.identity.includes('202')));assert.equal(f.counts.saves,0);}finally{gate.resolve();f.stop();}});
await check('settings invalidation during parallel reads retries against the latest schedule',async()=>{
 // Keep settings and target changes independent: a target fence must not mask a broken settings fence.
 for(const change of ['settings','target']){
  const gate=defer();let hold=false,frameStarts=0;const performanceStarts=[];let f;
  f=await createMirrorHostFixture({readHooks:{
   frames:async()=>{if(hold){frameStarts++;await gate.promise;}},
   performance:async()=>{performanceStarts.push({tdp:f.state.schedule.controls.ac.tdp,pid:f.state.game.pid});},
  }});
  try{
   f.state.schedule.controls={version:1,ac:{mode:'auto',tdp:21,tdpCeiling:55},dc:{mode:'auto',tdp:17,tdpCeiling:30}};
   f.ready();await request(f);await flush();
   const previous=f.replies.filter(x=>x.message.type==='snapshot').at(-1)?.message.snapshot;
   assert(previous?.ready);assert(previous.performance?.enabled);assert.equal(previous.performance.scope,'global');assert.equal(previous.performance.side,'ac');
   assert.equal(previous.performance.controls.find(x=>x.key==='tdp')?.value,21);
   const oldRevision=previous.performance.sourceRevision;assert(oldRevision);
   const originalGame={...f.state.game},readsBefore=performanceStarts.length;
   hold=true;f.monitorChanged();await flush();
   assert.equal(frameStarts,1,'The original frame owner must still be blocked');
   assert.equal(performanceStarts.length,readsBefore+1,'The parallel performance reader must have started on the old source');
   assert.deepEqual(performanceStarts.at(-1),{tdp:21,pid:originalGame.pid});
   const invalidationBoundary=f.replies.length;
   if(change==='settings'){
    f.state.schedule.controls.ac.tdp=29;f.scheduleChanged();
   }else{
    f.state.game={...originalGame,pid:202,processCreated:'birth-B',path:'C:/Other/Game.exe'};f.gameChanged();
   }
   await flush();
   hold=false;gate.resolve();await flush();
   assert(performanceStarts.length>=readsBefore+2,'Invalidated parallel reads must be retried against the current owner');
   const snapshots=f.replies.slice(invalidationBoundary).filter(x=>x.message.type==='snapshot').map(x=>x.message.snapshot);
   assert(snapshots.length,'A current snapshot must eventually be published');
   for(const snapshot of snapshots){
    // Pending snapshots may retain display values; they cannot grant current operation authority.
    if(!snapshot.ready){assert(Object.values(snapshot.actions??{}).every(value=>value===false));assert(snapshot.fan?.canToggle!==true);continue;}
    assert(snapshot.performance?.enabled);assert.equal(snapshot.performance.scope,'global');assert.equal(snapshot.performance.side,'ac');
    assert.equal(snapshot.performance.controls.find(x=>x.key==='tdp')?.value,change==='settings'?29:21);
    if(change==='settings')assert.notEqual(snapshot.performance.sourceRevision,oldRevision,'Admitted settings must refer to the new performance source');
    if(change==='target'){assert(snapshot.game?.identity.includes('202'));assert.notEqual(snapshot.game.identity,previous.game.identity);}
    else assert.equal(snapshot.game?.identity,previous.game.identity);
   }
   const final=snapshots.at(-1);assert(final?.ready);assert(final.performance?.enabled);
   assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{hold=false;gate.resolve();f.stop();}
 }

 // An old peer may finish after the new peer has acquired its read lease.
 // Keep the stable branch separate so an intentional source event cannot mask
 // an unwanted LS-completion retry caused by clearing the new peer's lease.
 for(const externalSettings of [false,true]){
  const framesA=defer(),framesB=defer(),program=defer();let frameStarts=0,programReads=0;
  const f=await createMirrorHostFixture({readHooks:{frames:()=>{frameStarts++;return frameStarts===1?framesA.promise:frameStarts===2?framesB.promise:Promise.resolve();},losslessProgram:()=>{programReads++;return program.promise;}}});
  try{
   f.state.game=null;f.state.schedule.controls={version:1,ac:{mode:'auto',tdp:21,tdpCeiling:55},dc:{mode:'auto',tdp:17,tdpCeiling:30}};
   f.ready();const pendingA=request(f);await flush();assert.equal(frameStarts,1);assert.equal(programReads,1);
   const currentPeer={...peer,connection:'2'};f.emit('deckySidebar.mirrorPeer',currentPeer);f.emit('deckySidebar.mirrorRequest',{...currentPeer,request:{command:'snapshot',runId}});await flush();assert.equal(frameStarts,2);
   framesA.resolve();await pendingA;await flush();assert.equal(frameStarts,2);
   f.state.losslessProgram={known:true,running:true};program.resolve();await flush();assert.equal(frameStarts,2);
   if(externalSettings){f.state.schedule.controls.ac.tdp=29;f.scheduleChanged();await flush();}
   framesB.resolve();await flush();const views=f.replies.filter(row=>row.message.type==='snapshot');assert(views.length);
   assert(views.every(row=>row.connection==='2'),'No retired-peer view may be delivered');
   assert(views.every(row=>row.message.snapshot.ready&&row.message.snapshot.performance.controls.find(control=>control.key==='tdp')?.value===(externalSettings?29:21)),'Only the current external owner source may be admitted');
   assert.deepEqual(views.at(-1).message.snapshot.losslessProgram,{known:true,running:true});assert.equal(programReads,1);
   assert.equal(frameStarts,externalSettings?3:2,'Old finally cannot clear a newer lease; only an actual settings edge adds a retry');
   assert.equal(f.counts.inputReads,frameStarts);assert.equal(f.counts.monitorReads,frameStarts);assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.counts.fxSoundWrites,0);
   completionMeasurements.push({timing:externalSettings?'old-peer-with-settings':'old-peer-stable',controlBatches:frameStarts,programReads,currentPeerOnly:true});
  }finally{framesA.resolve();framesB.resolve();program.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
});
await check('a control owner error disables only the related operation and retains other fresh fields',async()=>{const f=await createMirrorHostFixture({readHooks:{monitor:async()=>{throw Error('Memory monitor offline');}}});try{f.ready();const snapshot=await request(f);assert(snapshot.ready);assert.equal(snapshot.actions.monitor,false);assert(snapshot.frames);assert(snapshot.touchpads);assert.equal(snapshot.actions.game,true);assert(f.diagnostics.some(x=>x.event==='snapshot-read-finished'&&x.stage==='partial'));}finally{f.stop();}});
await check('after hiding, diagnostic and owner observers stay idle despite source event storms',async()=>{const f=await createMirrorHostFixture();try{f.ready();await request(f);f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();const reads=f.counts.power,logs=f.diagnostics.length;for(let i=0;i<1000;i++){f.trainerChanged();f.scheduleChanged();f.monitorChanged();}await flush();assert.equal(f.counts.power,reads);assert.equal(f.diagnostics.length,logs);assert.equal(f.observerStats().schedule,0);}finally{f.stop();}});
function clientFixture(buildId){const sent=[],timers=new Map(),handlers=new Map();let next=0;const socket={send:s=>sent.push(JSON.parse(s)),close(){handlers.get('close')?.();},addEventListener:(n,fn)=>handlers.set(n,fn)};const client=new MirrorClient({bootstrap:()=>({endpoint:'ws://127.0.0.1:12345/mirror',runId,token:'TEST-ONLY'}),socket:()=>socket,clientId:'build',buildId,schedule:(fn,ms)=>{timers.set(++next,{fn,ms});return next;},cancel:id=>timers.delete(id)});return {client,sent,timers,handlers};}
await check('compiled fingerprint accompanies read-only requests and never adds a discovery connection',()=>{const id='a'.repeat(64),f=clientFixture(id);try{f.client.setVisible(true);f.handlers.get('open')();assert.equal(f.sent.length,1);assert.equal(f.sent[0].buildId,id);assert.equal(f.sent[0].command,'snapshot');f.client.setVisible(false);assert.equal(f.timers.size,0);}finally{f.client.dispose();}});
await check('unbuilt or malformed fingerprint is omitted for legacy clients',()=>{for(const id of [undefined,'development','C:/private/path','A'.repeat(64),'a'.repeat(65)]){const f=clientFixture(id);try{f.client.setVisible(true);f.handlers.get('open')();assert.equal('buildId' in f.sent[0],false);}finally{f.client.dispose();}}});
await check('an in-flight snapshot retains no diagnostics metadata in its public payload',async()=>{const f=relayFixture(async trace=>{await trace.measure('input',async()=>{});return data();});try{await f.open();const text=JSON.stringify(f.replies.at(-1).snapshot);assert(!text.includes('readId'));assert(!text.includes('powerMs'));assert(!text.includes('demandEpoch'));assert(!text.includes('inputMs'));}finally{f.dispose();}});

await check('read recovery: each measured owner failure is retried without treating its partial cache as settled',async()=>{
 for(const stage of ['power','powerSource','schedule','performance','topology','gyro','frames','touchpads','global','monitor','input']){
  let fail=true;const f=relayFixture(async trace=>{try{await trace.measure(stage,async()=>{if(fail)throw Error('PRIVATE transient field');});}catch{}return {...data(),actions:{monitor:!fail}};});
  try{await f.open();assert.equal(f.replies.at(-1).snapshot.ready,true);assert.equal(f.replies.at(-1).snapshot.actions.monitor,false);
   assert.equal(f.timers.size,1,stage+' must retain a single read-recovery lease');assert.equal([...f.timers.values()][0].ms,250);
   fail=false;await f.fire(250);assert.equal(f.replies.at(-1).snapshot.actions.monitor,true);assert.equal(f.timers.size,0);
   assert(!JSON.stringify(f.logs).includes('PRIVATE'));
  }finally{f.dispose();}
 }
});
for(const kind of ['exception','readiness'])await check('read recovery: '+kind+' lasting beyond two fast attempts still recovers without another event',async()=>{
 let fail=true;const f=relayFixture(async()=>{if(fail&&kind==='exception')throw Error('PRIVATE transient read');return {...data(),ready:!fail};});
 try{await f.open();await f.fire(250);await f.fire(1000);assert.equal(f.reads,3);assert.equal(f.timers.size,1);
  assert.equal([...f.timers.values()][0].ms,3000);fail=false;await f.fire(3000);assert.equal(f.reads,4);assert.equal(f.replies.at(-1).snapshot.ready,true);assert.equal(f.timers.size,0);
 }finally{f.dispose();}
});
await check('read recovery: persistent faults share one capped timer and repeated demand does not reset backoff',async()=>{
 const f=relayFixture(async()=>{throw Error('PRIVATE offline');});try{
  await f.open();for(const delay of [250,1000,3000,10000,30000]){assert.equal(f.timers.size,1);await f.fire(delay);}
  for(let n=0;n<100;n++){assert.equal(f.timers.size,1);assert.equal([...f.timers.values()][0].ms,30000);await f.fire(30000);}
  for(let n=0;n<10;n++)await f.open();assert.equal(f.timers.size,1);await f.fire(30000);assert.equal([...f.timers.values()][0].ms,30000);
 }finally{f.dispose();assert.equal(f.timers.size,0);}
});
for(const boundary of ['pause-reopen','peer-change'])await check('read recovery: dispatched callback from '+boundary+' cannot consume the current lease',async()=>{
 let fail=true;const f=relayFixture(async()=>({...data(),ready:!fail}));try{
  await f.open();const old=[...f.timers.values()][0];assert(old);
  if(boundary==='pause-reopen'){await f.pause();await f.open();}
  else{f.relay.connected({...peer,connection:'2'});await f.relay.request({...peer,connection:'2',request:{command:'snapshot',runId}});}
  const current=[...f.timers.entries()][0],reads=f.reads;assert(current);old.fn();await flush();assert.equal(f.reads,reads);
  assert.deepEqual([...f.timers.keys()],[current[0]]);fail=false;await f.fire(250);assert.equal(f.replies.at(-1).snapshot.ready,true);assert.equal(f.timers.size,0);
 }finally{f.dispose();}
});
await check('read recovery: healthy disabled controls are not mistaken for a failed read or polled',async()=>{
 const f=relayFixture(async()=>({...data(),actions:{monitor:false,performance:false}}));try{
  await f.open();for(let n=0;n<1000;n++)await f.invalidate();assert.equal(f.timers.size,0);assert.equal(f.replies.at(-1).snapshot.ready,true);
 }finally{f.dispose();}
});
await check('read recovery: pause and disposal release the single timer and late callbacks remain inert',async()=>{
 for(const boundary of ['pause','disconnect','dispose']){
  const f=relayFixture(async()=>({...data(),ready:false}));try{await f.open();const old=[...f.timers.values()][0];assert(old);
   if(boundary==='pause')await f.pause();else if(boundary==='disconnect')f.relay.connected({...peer,connected:false});else f.dispose();assert.equal(f.timers.size,0);const reads=f.reads;old.fn();await flush();assert.equal(f.reads,reads);assert.equal(f.timers.size,0);
  }finally{f.dispose();}
 }
});
for(const field of ['monitor','performance'])await check('read recovery: actual host restores '+field+' after its owner returns without a source event',async()=>{
 let fail=false;const f=await createMirrorHostFixture({readHooks:{[field]:async()=>{if(fail)throw Error('PRIVATE owner unavailable');}}});
 try{f.ready();await request(f);await flush();fail=true;f.monitorChanged();await flush();
  let snapshot=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(snapshot.ready,true);assert.equal(snapshot.actions[field],false);assert.equal(snapshot.actions.game,true);assert.equal(f.controllerTimerCount(),1);
  fail=false;await flush();assert.equal(f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot.actions[field],false);
  f.advanceController(250);await flush();snapshot=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(snapshot.actions[field],true);assert.equal(f.controllerTimerCount(),0);
  for(const name of ['saves','inputSaves','monitorSaves','monitorApplies','performanceUpdates','frameApplies','fanSaves'])assert.equal(f.counts[name]??0,0,name);
 }finally{f.stop();assert.equal(f.controllerTimerCount(),0);}
});
await check('read recovery: missed Ready notification refreshes native facts without replaying a control write',async()=>{
 const f=await createMirrorHostFixture();try{f.ready();await request(f);await flush();
  f.state.power={...f.state.power,generation:2,phase:'resuming',hardwareWritesAllowed:false};f.powerChanged(true);await flush();
  assert.equal(f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot.ready,false);assert.equal(f.controllerTimerCount(),1);
  f.state.power={...f.state.power,phase:'ready',hardwareWritesAllowed:true};f.advanceController(250);await flush();
  const snapshot=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;assert.equal(snapshot.ready,true);assert.equal(snapshot.generation,2);assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.controllerTimerCount(),0);
 }finally{f.stop();}
});

await check('read recovery: unrelated monitor intent still reaches its owner during a performance outage and is not replayed',async()=>{
 const f=await createMirrorHostFixture();try{f.ready();await request(f);await flush();f.state.performanceReadError=true;f.monitorChanged();await flush();
  const snapshot=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(snapshot.actions.performance,false);assert.equal(snapshot.actions.monitor,true);const enabled=!snapshot.monitor.enabled;
  f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'monitor.setEnabled',runId,id:'readfix:1',clientId:'readfix',sequence:1,generation:snapshot.generation,revision:snapshot.revision,args:{enabled}}});await flush();
  const reply=f.replies.find(r=>r.message.type==='reply'&&r.message.id==='readfix:1')?.message;assert.equal(reply?.ok,true);
  assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);assert.equal(f.counts.performanceUpdates??0,0);
  f.advanceController(250);await flush();assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);
  f.state.performanceReadError=false;f.advanceController(1000);await flush();assert.equal(f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot.actions.performance,true);
  assert.equal(f.counts.monitorSaves,1);assert.equal(f.controllerTimerCount(),0);
 }finally{f.stop();}

 // A real admission edge must remain observable during an unrelated long fault.
 // Reuse this original case, the same Host clock, and original owner IO seams.
 const latest=f=>f.replies.filter(r=>r.message.type==='snapshot').at(-1)?.message.snapshot;
 const capped=async f=>{f.ready();await request(f);f.state.performanceReadError=true;f.monitorChanged();await flush();for(const delay of [250,1000,3000,10000]){f.advanceController(delay);await flush();}assert.equal(f.controllerTimerCount(),1);};
 {
  const f=await createMirrorHostFixture();try{
   await capped(f);const beforeReads=f.counts.power;
   const startProtection={locked:true,requestId:11,phase:'pending',persona:'steamdeck',remainingMs:1000,elapsedMs:0,maxMs:30000,settleMs:10000};
   f.state.gamepadState={virtualOutput:{state:'active',requestedPersona:'steamdeck',boundPersona:'steamdeck',hostAlive:true,hostEpoch:1,prepared:true,neutralized:true,frameAccepted:true,sourceLive:true,sourceCurrent:true,steamRunning:true,startProtection}};
   f.emit('gamepad.state',f.state.gamepadState);await flush();let snapshot=latest(f);
   assert.equal(snapshot.virtualController?.locked,true,'Current native controller lock cannot wait for an unrelated 30-second fault read');
   assert.equal(snapshot.ready,true);assert.equal(snapshot.actions.performance,false);assert.equal(snapshot.actions.monitor,true);
   assert.equal(snapshot.game.fields.padPersona.supported,false);assert.equal(f.counts.power,beforeReads+1,'One actual edge owns one fresh batch');
   const reads=f.counts.power;
   for(let i=0;i<100;i++)f.emit('gamepad.state',{virtualOutput:{...f.state.gamepadState.virtualOutput,packetCount:i,startProtection:{...startProtection,remainingMs:1000-i,elapsedMs:i}}});
   await flush();assert.equal(f.counts.power,reads,'Equivalent telemetry cannot bypass fault backoff');assert.equal(f.controllerTimerCount(),2);
   f.advanceController(1000);await flush();snapshot=latest(f);
   assert.equal(snapshot.virtualController?.locked,false,'Original controller UI deadline must publish while the other owner remains unavailable');
   assert.equal(snapshot.ready,true);assert.equal(snapshot.actions.performance,false);assert.equal(snapshot.actions.monitor,true);assert.equal(f.controllerTimerCount(),1);
   assert.equal(f.counts.power,reads+1,'The deadline owns one refresh, not a control retry');
   const beforeOriginalDeadline=f.counts.power;f.advanceController(28999);await flush();assert.equal(f.counts.power,beforeOriginalDeadline,'Admission changes cannot refund or shorten the capped fault lease');
   f.advanceController(1);await flush();assert.equal(f.counts.power,beforeOriginalDeadline+1,'Original capped read lease still expires at its original time');
   assert.equal(f.controllerTimerCount(),1);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{f.stop();assert.equal(f.controllerTimerCount(),0);}
 }
 {
  let active=true,disableCalls=0;
  const life={controlReady:false,state:'awaiting-control',hasControlIntent:true,recoveryActive:false,controlCancellationRevision:0,
   start:async()=>{throw Error('Unexpected automatic ON');},apply:async()=>{throw Error('Unexpected curve write');},applyPreset:async()=>{throw Error('Unexpected preset');},
   disable:async()=>{disableCalls++;life.controlCancellationRevision++;life.hasControlIntent=false;life.controlReady=false;return {state:'AwaitingControl'};}};
  const feature={FAN_IMPORT_ENABLED:true,FAN_FORCE_PREVIEW:false,fanFeatureEnabled:{value:true},fanControlActive:{get value(){return active;}},getFanSelectedPreset:()=> 'balanced',getFanFeatureSettings:()=>({preset:'balanced'}),getFanPresetCurve:()=>[],saveFanCurve:async()=>{throw Error('Unexpected fan save');},setFanControlActive:value=>{active=value;},setFanNavigationDuty:()=>{},recordFanHandshake:async()=>{}};
  const f=await createMirrorHostFixture({fanOwner:{lifecycle:life,featureBindings:feature,close:async()=>{}}});try{
   await capped(f);const old=latest(f),reads=f.counts.power;life.controlReady=true;life.state='ready';f.emit('fan.resume-ready',{generation:1});await flush();const snapshot=latest(f);
   assert.equal(snapshot.fan.supported,true,'Actual fan owner readiness cannot wait for an unrelated fault');assert.equal(snapshot.actions.fan,true);assert.equal(snapshot.fan.canToggle,true);
   assert.equal(snapshot.actions.performance,false);assert.equal(f.counts.power,reads+1);
   const stable=f.counts.power;for(let i=0;i<100;i++)f.emit('fan.resume-ready',{generation:1});await flush();assert.equal(f.counts.power,stable,'Duplicate readiness signals cannot start additional batches');assert.equal(f.controllerTimerCount(),1);
   const off={command:'fan.setEnabled',runId,id:'edge:1',clientId:'edge',sequence:1,generation:old.generation,revision:old.revision,args:{enabled:false}};
   f.emit('deckySidebar.mirrorRequest',{...peer,request:off});await flush();assert.equal(f.replies.find(r=>r.message.id===off.id)?.message.ok,false,'Stale delivered authority is still rejected');assert.equal(disableCalls,0);
   const current=latest(f),fresh={...off,id:'edge:2',sequence:2,generation:current.generation,revision:current.revision};
   f.emit('deckySidebar.mirrorRequest',{...peer,request:fresh});await flush();assert.equal(f.replies.find(r=>r.message.id===fresh.id)?.message.ok,true);assert.equal(disableCalls,1,'Explicit OFF reaches the original fan owner once');
   f.emit('deckySidebar.mirrorRequest',{...peer,request:fresh});await flush();assert.equal(disableCalls,1,'Duplicate command returns its old receipt without replay');
   f.advanceController(30000);await flush();assert.equal(disableCalls,1);assert.equal(active,false);assert.equal(life.hasControlIntent,false);
   assert.equal(f.counts.fanSaves,0);assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.performanceUpdates??0,0);
  }finally{f.stop();assert.equal(f.controllerTimerCount(),0);}
 }
});
await check(speedCompletionCase,async()=>{
 if(!speedCompletionOnly){
 const gate=defer();let hold=false,started=0;const f=await createMirrorHostFixture({readHooks:{performance:async()=>{if(hold){started++;await gate.promise;}}}});
 try{f.ready();await request(f);await flush();f.state.performanceReadError=true;f.monitorChanged();await flush();
  hold=true;f.advanceController(250);await flush();assert.equal(started,1);const old=f.state.game;
  f.state.game={...old,pid:202,processCreated:'birth-B',path:'C:/Other/GameB.exe'};f.state.power={...f.state.power,generation:2};
  f.gameChanged();f.powerChanged();f.state.performanceReadError=false;hold=false;const boundary=f.replies.length;gate.resolve();await flush();
  const snapshots=f.replies.slice(boundary).filter(r=>r.message.type==='snapshot').map(r=>r.message.snapshot);assert(snapshots.length);
  assert(snapshots.every(s=>s.generation===2&&s.game?.identity.includes('202')));assert.equal(snapshots.at(-1).actions.performance,true);
  assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);assert.equal(f.controllerTimerCount(),0);
 }finally{gate.resolve();f.stop();}

 // Global audio intent has no game identity, but its native transaction must
 // still retain the generation admitted before an asynchronous state read.
 for(const change of ['generation','native-dispatch-generation','gate','peer','source']){
  const gate=defer();let hold=false,entered=false;const calls=[];let applications=0;
  const f=await createMirrorHostFixture({readHooks:{fxSound:async()=>{if(hold){entered=true;await gate.promise;}}}});
  const A={...peer,connectionRevision:1};
  try{
   f.ready();f.emit('deckySidebar.mirrorRequest',{...A,request:{command:'snapshot',runId}});await flush();
   const original=f.replies.filter(row=>row.message.type==='snapshot').at(-1).message.snapshot;assert(original.actions.audioOptimization);
   // This is the existing native setter port. Its expected/current contract is
   // also compiled from the actual main.cpp validation prefix in native evidence.
   f.state.fxSoundSetHook=async(enabled,expectedGeneration)=>{
    if(change==='native-dispatch-generation')f.state.power={...f.state.power,generation:f.state.power.generation+1};
    const current=f.state.power;calls.push({enabled,expectedGeneration,nativeGeneration:current.generation});
    if(expectedGeneration!==undefined&&expectedGeneration!==current.generation)throw Error('FXSOUND_POWER_NOT_READY');
    const allowed=current.phase==='ready'&&current.hardwareWritesAllowed===true;
    if(allowed){applications++;f.state.fxSound={...f.state.fxSound,enabled,running:enabled};}
    return {ok:allowed,state:structuredClone(f.state.fxSound),copied:false,reused:false,repaired:false,reason:allowed?'Inert native transaction accepted':'Native power gate closed'};
   };
   const intent={...A,request:{command:'audioOptimization.setEnabled',runId,id:'audio-generation:1',clientId:'audio-generation',sequence:1,generation:original.generation,revision:original.revision,args:{enabled:!original.audioOptimization.enabled}}};
   hold=true;f.emit('deckySidebar.mirrorRequest',intent);await flush();assert(entered);assert.equal(calls.length,0);
   if(change==='generation'){f.state.power={...f.state.power,generation:original.generation+1};f.powerChanged();}
   if(change==='gate'){f.state.power={...f.state.power,phase:'resuming',hardwareWritesAllowed:false};f.powerChanged(true);}
   if(change==='peer')f.emit('deckySidebar.mirrorPeer',{...A,connection:'2',connectionRevision:3});
   if(change==='source')f.state.fxSound={...f.state.fxSound,output:'A newer output selected outside Decky'};
   await flush();hold=false;gate.resolve();await flush();
   assert.equal(applications,0,'An old or revoked audio intent must never apply');
   const reply=f.replies.find(row=>row.message.id==='audio-generation:1')?.message;
   if(change==='generation'||change==='native-dispatch-generation'){
    assert.equal(calls.length,1);assert.equal(calls[0].expectedGeneration,original.generation);assert.equal(calls[0].nativeGeneration,original.generation+1);assert.equal(reply?.ok,false);
   }else if(change==='gate'){assert.equal(reply?.result.applied,false);assert.equal(reply?.result.pending,true);}
   else assert.equal(calls.length,0,'Revoked peer/source cannot reach the native transaction');
   if(change!=='peer'){
    const requests=calls.length;f.emit('deckySidebar.mirrorRequest',intent);await flush();assert.equal(calls.length,requests);assert.equal(applications,0,'Duplicate failed intent cannot replay across a wake');
   }
   if(change==='generation'){
    const current=f.replies.filter(row=>row.message.type==='snapshot').at(-1).message.snapshot;
    assert.equal(current.generation,original.generation+1);assert.equal(current.ready,true);
    const fresh={...A,request:{...intent.request,id:'audio-generation:2',sequence:2,generation:current.generation,revision:current.revision}};
    f.emit('deckySidebar.mirrorRequest',fresh);await flush();assert.equal(f.replies.find(row=>row.message.id==='audio-generation:2')?.message.result.applied,true);
    assert.equal(applications,1);assert.equal(calls.at(-1).expectedGeneration,current.generation);
    f.emit('deckySidebar.mirrorRequest',fresh);await flush();assert.equal(applications,1,'Only the new explicit intent applies once');
   }
   audioGenerationMeasurements.push({boundary:change,nativeRequests:calls,applications,oldIntentApplied:false,automaticReplay:false});
   assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.monitorSaves,0);
  }finally{hold=false;gate.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
 {
  const sent=[];const state={known:true,installed:true,running:true,enabled:true,selected:true,preset:'inert',output:'inert',reason:''};
  const owner=load(src('src/bridge/fxSound.ts'),{'./ipc':{invoke:async(command,args)=>{sent.push({command,args});return {ok:true,state,copied:false,reused:false,repaired:false,reason:'Legacy normal YMCC owner'};}}},{clearTimeout:()=>{throw Error('Unexpected legacy owner timer');}});
  const result=await owner.fxSound.setEnabled(true);assert.equal(result.ok,true);assert.deepEqual(structuredClone(sent),[{command:'fxsound.setEnabled',args:{enabled:true}}],'Normal YMCC one-field calls keep their original native API');
  audioGenerationMeasurements.push({boundary:'legacy-normal-owner',optionalGenerationAbsent:true,nativeRequests:1});
 }
 }
 await speedCompletionBoundaries();
});

await check('read recovery: repeated snapshot and source requests cannot bypass the outstanding fault backoff',async()=>{
 const f=relayFixture(async()=>{throw Error('PRIVATE offline');});try{await f.open();const initial=f.reads;
  for(let n=0;n<100;n++){await f.open();await f.invalidate();}
  assert.equal(f.reads,initial,'All repeated demand must share the already scheduled read lease');assert.equal(f.timers.size,1);
  await f.fire(250);assert.equal(f.reads,initial+1);
 }finally{f.dispose();}
});

await check('read recovery: native not-Ready observation does not enter business readers or refresh write admission',async()=>{
 let performanceReads=0;const f=await createMirrorHostFixture({readHooks:{performance:async()=>{performanceReads++;}}});
 try{
  f.state.schedule.controls={version:1,ac:{mode:'auto',tdp:21,tdpCeiling:55},dc:{mode:'auto',tdp:17,tdpCeiling:30}};
  f.ready();await request(f);await flush();const original=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(original.performance.controls.find(row=>row.key==='tdp')?.value,21);
  const before={performanceReads,frames:f.counts.frameReads,monitor:f.counts.monitorReads,input:f.counts.inputReads,audio:f.counts.fxSoundReads};
  const counts=()=>({performanceReads,frames:f.counts.frameReads,monitor:f.counts.monitorReads,input:f.counts.inputReads,audio:f.counts.fxSoundReads});
  f.state.power={...f.state.power,generation:2,phase:'resuming',hardwareWritesAllowed:false};f.powerChanged(true);await flush();
  for(const delay of [250,1000,3000]){f.advanceController(delay);await flush();}
  assert.deepEqual(counts(),before,'Native not-Ready observation must not enter any business reader');
  const held=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(held.ready,false);assert.equal(held.generation,2);assert.equal(held.actions.performance,false);assert.equal(held.actions.game,false);
  // An external settings change while gated cannot be laundered into Ready.
  f.state.schedule.controls.ac.tdp=29;f.scheduleChanged();await flush();assert.deepEqual(counts(),before);
  f.state.power={...f.state.power,phase:'ready',hardwareWritesAllowed:true};f.advanceController(10000);await flush();
  const current=f.replies.filter(r=>r.message.type==='snapshot').at(-1).message.snapshot;
  assert.equal(current.ready,true);assert.equal(current.generation,2);assert.equal(current.actions.performance,true);assert.equal(current.actions.game,true);
  assert.equal(current.game.identity,original.game.identity);assert.equal(current.performance.controls.find(row=>row.key==='tdp')?.value,29);
  assert.notEqual(current.performance.sourceRevision,original.performance.sourceRevision,'Fresh authority refers to the current owner source');
  assert.deepEqual(counts(),Object.fromEntries(Object.entries(before).map(([key,value])=>[key,value+1])),'One Ready batch consumes fast LS/audio completions without a redundant business batch');
  // Old generation authority must still fail before the original operation owner.
  f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'performance.setControls',runId,id:'ready:1',clientId:'ready',sequence:1,generation:original.generation,revision:original.revision,identity:original.game.identity,args:{patch:{tdp:33}}}});await flush();
  assert.equal(f.replies.find(r=>r.message.id==='ready:1')?.message.ok,false);assert.equal(f.counts.performanceUpdates??0,0);
  assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.controllerTimerCount(),0);
 }finally{f.stop();}
});
await check('read recovery: duplicate probe and native context events cannot create extra read batches',async()=>{
 const f=relayFixture(async()=>{throw Error('PRIVATE offline');});try{await f.open();for(let n=0;n<100;n++)await f.relay.request({...peer,request:{command:'snapshot',runId,probeId:'probe:test:'+n}});
  assert.equal(f.reads,1);assert.equal(f.timers.size,1);await f.fire(250);assert.equal(f.reads,2);
 }finally{f.dispose();}
 const h=await createMirrorHostFixture();try{h.ready();await request(h);await flush();h.state.monitorReadError=true;h.monitorChanged();await flush();h.powerChanged();await flush();
  const reads=h.counts.power;for(let n=0;n<100;n++){h.powerChanged();h.gameChanged();h.monitorChanged();}await flush();assert.equal(h.counts.power,reads);assert.equal(h.controllerTimerCount(),1);
 }finally{h.stop();}

 // The envelope serial is produced by native Broker/WM, never request payload.
 {
  const A={...peer,connectionRevision:1},B={...peer,connection:'2',connectionRevision:3};
  const oldAttach=defer();let attaches=0,current=null,extensionDeliveries=0;
  const connected=[];const f=await createMirrorHostFixture({invoke:async command=>{
   if(command!=='deckySidebar.mirrorAttach')return true;
   if(++attaches===2)await oldAttach.promise;
   return {...A,owner:'YMCC-native',pid:8612};
  }});
  const off=f.registerExtension({prefix:'wallpaper.',connected:value=>{current=value;connected.push({connection:value.connection,connected:value.connected!==false});},request:event=>{if(current?.connected!==false&&event.connection===current?.connection&&event.runId===current?.runId)extensionDeliveries++;}});
  const snapshot=()=>f.replies.filter(row=>row.message.type==='snapshot').at(-1)?.message.snapshot;
  try{
   await flush();f.ready();await flush();assert.equal(snapshot()?.ready,true);assert.equal(connected.length,1);
   // Native B request arrives while its peer notification is missing.
   f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'snapshot',runId}});await flush();
   assert.equal(f.replies.filter(row=>row.message.type==='snapshot').at(-1)?.connection,'2','Current authenticated B request repairs a lost peer event');
   assert.equal(snapshot().ready,true);assert.equal(connected.length,2);
   // A missing legacy serial cannot downgrade a confirmed typed peer.
   const legacyBoundary=f.replies.length;
   f.emit('deckySidebar.mirrorPeer',{...peer,connected:true});await flush();
   f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'snapshot',runId}});await flush();
   const afterLegacy=f.replies.slice(legacyBoundary).filter(row=>row.message.type==='snapshot');
   assert(afterLegacy.some(row=>row.connection==='2'),'A serial-less old A notification must not strand confirmed B');
   assert(afterLegacy.every(row=>row.connection==='2'));assert.equal(connected.length,2,'Legacy mismatch must not reset wallpaper peer');

   const boundary=f.replies.length,reads=f.counts.power;
   oldAttach.resolve();await flush();assert.equal(connected.length,2,'Old attach must not reset current wallpaper state');assert.equal(f.counts.power,reads);
   // Already-retired private receipts cannot overtake B, even with client flags.
   f.emit('deckySidebar.mirrorPeer',A);
   f.emit('deckySidebar.mirrorRequest',{...A,request:{command:'snapshot',runId,isCurrent:true,connectionRevision:999999}});await flush();
   assert.equal(connected.length,2);assert.equal(f.counts.power,reads);assert.equal(f.replies.length,boundary);
   for(let n=0;n<100;n++)f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'snapshot',runId}});
   await flush();assert.equal(connected.length,2,'Repeated snapshots never rebroadcast wallpaper.connected');
   for(let n=0;n<100;n++)f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'wallpaper.context',runId,contextSeq:n+1,appId:'42',visible:true}});
   await flush();assert.equal(extensionDeliveries,100);assert.equal(connected.length,2,'Passive context traffic never resets extension ownership');
   const source=snapshot(),intent={...B,request:{command:'monitor.setEnabled',runId,id:'transport:1',clientId:'transport',sequence:1,generation:source.generation,revision:source.revision,args:{enabled:!source.monitor.enabled}}};
   for(let n=0;n<100;n++)f.emit('deckySidebar.mirrorRequest',intent);
   await flush();assert.equal(f.replies.find(row=>row.message.id==='transport:1')?.message.ok,true);assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);assert.equal(connected.length,2);
   // A current native disconnect is authoritative even if that peer's ON was lost.
   f.emit('deckySidebar.mirrorPeer',{...peer,connection:'3',connectionRevision:6,connected:false});await flush();assert.equal(current.connected,false);assert.equal(f.observerStats().watchers,0);assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);
   const afterClose=f.counts.power;f.emit('deckySidebar.mirrorRequest',{...B,request:{command:'snapshot',runId,isCurrent:true}});await flush();assert.equal(f.counts.power,afterClose,'Old request cannot revive demand after current native OFF');
   const D={...peer,connection:'4',connectionRevision:7};f.emit('deckySidebar.mirrorRequest',{...D,request:{command:'snapshot',runId}});await flush();assert.equal(snapshot().ready,true);assert.equal(current.connection,'4');assert.equal(connected.length,4);
   assert.equal(f.counts.monitorSaves,1);assert.equal(f.counts.monitorApplies,1);assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
   assert(f.replies.slice(boundary).filter(row=>row.message.type==='snapshot').every(row=>row.connection!=='1'));
   transportMeasurements.push({boundary:'missed-peer-late-attach-and-duplicates',connected,extensionDeliveries,explicitMonitorSaves:f.counts.monitorSaves,explicitMonitorApplies:f.counts.monitorApplies,retiredRequestRevived:false});
  }finally{oldAttach.resolve();off();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
 {
  const framesA=defer(),framesB=defer();let frames=0;
  const f=await createMirrorHostFixture({readHooks:{frames:()=>++frames===1?framesA.promise:frames===2?framesB.promise:Promise.resolve()}});
  try{
   f.ready();f.emit('deckySidebar.mirrorRequest',{...peer,connectionRevision:1,request:{command:'snapshot',runId}});await flush();assert.equal(frames,1);
   // A passed the native gate, then B takes ownership while A awaits an owner.
   f.emit('deckySidebar.mirrorRequest',{...peer,connection:'2',connectionRevision:3,request:{command:'snapshot',runId}});await flush();assert.equal(frames,2);
   framesA.resolve();await flush();assert.equal(f.replies.filter(row=>row.message.type==='snapshot').length,0);
   framesB.resolve();await flush();const views=f.replies.filter(row=>row.message.type==='snapshot');assert(views.length);assert(views.every(row=>row.connection==='2'&&row.message.snapshot.ready));assert.equal(frames,2);
   assert.equal(f.counts.saves,0);assert.equal(f.counts.performanceUpdates??0,0);
   transportMeasurements.push({boundary:'native-request-A-retires-during-async-read',controlBatches:frames,currentBOnly:true,businessWrites:0});
  }finally{framesA.resolve();framesB.resolve();f.stop();await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }

 {
  const f=relayFixture();try{
   const B={...peer,connection:'2',connectionRevision:3},requestB={...B,request:{command:'snapshot',runId}};
   assert.equal(f.relay.observeAuthenticatedRequestPeer(requestB),true);await f.relay.request(requestB);
   assert.equal(f.relay.connected({...peer,connection:'2'}),true,'Matching legacy peer still works');
   assert.equal(f.relay.observeAuthenticatedRequestPeer({...peer,connection:'2',request:{command:'snapshot',runId}}),true,'Matching legacy request stays compatible');
   assert.equal(f.relay.connected({...B,connected:false,connectionRevision:4}),true);
   assert.equal(f.relay.connected({...peer,connected:true}),false,'Legacy ON cannot revive a typed OFF');
   assert.equal(f.relay.connected({...peer,connection:'2',connected:true}),false,'Even the retired identity needs a new native fact after typed OFF');
   transportMeasurements.push({boundary:'typed-floor-legacy-matching-and-OFF',matchingLegacyAllowed:true,legacyRevivalRefused:true});
  }finally{f.dispose();}
  const legacy=relayFixture();try{await legacy.open();assert.equal(legacy.replies.at(-1).snapshot.ready,true);assert.equal(legacy.relay.connected({...peer,connection:'2'}),true,'A fresh legacy-only owner retains its original route');transportMeasurements.push({boundary:'fresh-legacy-only',compatible:true});}finally{legacy.dispose();}
 }
});

await check('read recovery: first fault and actual recovery are logged once even inside the normal cooldown',async()=>{
 let fail=false;const f=relayFixture(async trace=>{try{await trace.measure('performance',async()=>{if(fail)throw Error('PRIVATE owner');});}catch{}return data();});
 try{await f.open();const before=f.logs.filter(r=>r.event==='snapshot-read-finished').length;fail=true;await f.invalidate();
  assert.equal(f.logs.filter(r=>r.event==='snapshot-read-finished').length,before+1);assert.equal(f.logs.at(-1).stage,'partial');assert.equal(f.logs.at(-1).ready,true);
  await f.fire(250);assert.equal(f.logs.filter(r=>r.event==='snapshot-read-finished').length,before+1);
  fail=false;await f.fire(1000);const end=f.logs.at(-1);assert.equal(end.stage,'settled');assert.equal(end.ready,true);assert.equal(end.ok,true);
  assert.equal(f.logs.filter(r=>r.event==='snapshot-read-finished').length,before+2);assert.equal(f.timers.size,0);assert(!JSON.stringify(f.logs).includes('PRIVATE'));
 }finally{f.dispose();}
});

async function speedCompletionBoundaries(){
 for(const timing of ['native-final-wait','owner-success-log-await']){
  const gate=defer(),P={active:true,connected:true,connection:'73',runId:'c'.repeat(32),connectionRevision:73};let f=null,held=false,holdLog=true,simulatedEffectFactor=1;
  const initialPower={generation:1,phase:'ready',hardwareWritesAllowed:true},initialGame={pid:101,name:'game.exe',title:'Fixture',path:'C:/Games/Game.exe',processCreated:'123456789'};
  const speed=createMirrorSpeedMemoryOwner({game:()=>f?.state.game??initialGame,power:()=>f?.state.power??initialPower,peer:()=>P,
   afterDispatch:async()=>{simulatedEffectFactor=2;if(timing==='native-final-wait'){f.state.power={...f.state.power,generation:2};f.powerChanged();}},
   beforeLogWrite:async text=>{if(timing==='owner-success-log-await'&&holdLog&&text.includes('RAW op=')){holdLog=false;held=true;await gate.promise;}}});
  f=await createMirrorHostFixture({speedOwner:speed.owner,invoke:async command=>{if(command==='deckySidebar.mirrorAttach')return speed.nativeState();if(command==='deckySidebar.mirrorReply')return true;throw Error('Unexpected completion fixture IPC '+command);}});
  try{
   f.ready();f.emit('deckySidebar.mirrorPeer',P);f.emit('deckySidebar.mirrorRequest',{...P,request:{command:'snapshot',runId:P.runId}});await flush();
   const snapshot=f.replies.filter(row=>row.message.type==='snapshot').at(-1).message.snapshot;assert.equal(snapshot.ready,true);
   const intent={...P,request:{command:'game.setField',runId:P.runId,id:'speed-complete:1',clientId:'speed-complete',sequence:1,generation:snapshot.generation,revision:snapshot.revision,identity:snapshot.game.identity,args:{field:'speedFactor',value:'2'}}};
   f.emit('deckySidebar.mirrorRequest',intent);await flush();
   if(timing==='owner-success-log-await'){assert.equal(held,true);f.state.power={...f.state.power,generation:2};f.powerChanged();gate.resolve();await flush();}
   const reply=f.replies.find(row=>row.message.id==='speed-complete:1')?.message;assert.equal(reply?.ok,true);assert.equal(reply.result.applied,false);assert.equal(reply.result.pending,true);assert.match(reply.result.notice,/待确认.*不自动重放/);
   assert.equal(speed.stats().dispatches,1);assert.equal(simulatedEffectFactor,2,'Already-possible effect is not called never-executed');const pending=speed.owner.getGameSpeedState();assert.equal(pending?.cleanupPending,true);assert.equal(pending?.enabled,false);assert.equal(pending?.pid,initialGame.pid);assert.equal(pending?.processCreated,initialGame.processCreated);assert.equal(speed.stats().activeGameObservers,1);assert.equal(speed.stats().activeWakeObservers,1);assert.equal(f.counts.speedApply,1);assert.equal(f.counts.speedLockReleases,1);
   assert.equal(f.counts.saves,0);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.monitorSaves,0);
   speedCompletionMeasurements.push({timing,dispatches:speed.stats().dispatches,simulatedEffectFactor,newSessionCommitted:false,cleanupOwnerRetained:true,oldSuccessReported:false,lockReleases:f.counts.speedLockReleases,automaticReplay:false,nativeWaitModeled:true});
  }finally{gate.resolve();f.stop();speed.disposeExternalPorts();await flush();assert.equal(speed.stats().activeGameObservers,0);assert.equal(speed.stats().activeWakeObservers,0);assert.equal(f.observerStats().ipc,0);assert.equal(f.controllerTimerCount(),0);assert.equal(f.fxTimerCount(),0);}
 }
}

const registeredNames=registry.map(row=>row.name);
assert.equal(new Set(registeredNames).size,registeredNames.length,'Original readiness names must be unique');
const unknown=requestedCases.filter(name=>!registeredNames.includes(name));
if(unknown.length)selectionError='Unknown exact readiness case: '+unknown.join(', ');
const selected=registry.filter(({name})=>requestedCases.length?requestedCases.includes(name):readRecoveryOnly?name.startsWith('read recovery:'):legacyOnly?!name.startsWith('read recovery:'):true);
if(!selected.length)selectionError=selectionError||'Empty readiness case selection';
if(selectionError){
 const report={status:'rejected',selected:'none',requestedCases,selectedNames:[],skipped:registeredNames,selectedResults:[],checks:0,preparationStarted:false,callbacksStarted:0,error:selectionError};
 console.log(JSON.stringify(report,null,2));process.exitCode=1;
}else{
 ({root,load}=await import('./fixtures/decky_game_memory_store.mjs'));
 ({createMirrorHostFixture,createMirrorSpeedMemoryOwner}=await import('./fixtures/decky_mirror_host_fixture.mjs'));
 const {validation}=await import('./fixtures/decky_task_paths.mjs');
 const steam=load(src('decky-plugin/src/steamRunningContext.ts'));
 ({ReadOnlyMirrorRelay}=load(src('src/bridge/deckyMirrorRelay.ts'),{'../../decky-plugin/src/steamRunningContext':steam}));
 ({MirrorClient}=load(src('decky-plugin/src/mirrorClient.ts'),{'./steamRunningContext':steam}));
 skipped.push(...registeredNames.filter(name=>!selected.some(row=>row.name===name)));
 for(const {name,fn} of selected){
  if(selection==='all'){await fn();cases.push(name);continue;}
  try{await fn();cases.push(name);selectedResults.push({name,status:'passed'});}
  catch(error){selectedResults.push({name,status:'failed',error:String(error.stack??error)});}
 }
 const report={status:selectedResults.some(row=>row.status==='failed')?'failed':'passed',selected:selection,requestedCases,selectedNames:selected.map(row=>row.name),skipped,selectedResults,checks:selection==='all'?cases.length:selectedResults.length,cases,beforeCounts,afterCounts,completionMeasurements,transportMeasurements,audioGenerationMeasurements,speedCompletionMeasurements,speedCompletionOnly,preparationStarted:true,callbacksStarted:selected.length,realSteam:false,realBusinessWrites:0,realDevices:false,healthyRuntimePollingTimers:0,recoveryUsesExistingTimerOwner:true,comparisonScope:readRecoveryOnly?'Production relay/host fault injection, capped read recovery, cancellation and current owner admission; no real Steam/devices or measured CPU claim.':legacyOnly?'Original 17 readiness diagnostics, current one-read counts and settings/target invalidation with no writes; historical baseline not rerun.':selection==='cases'?'Exact original names only; unselected legacy/read-recovery/native groups NOT_RUN. No installed, hardware or measured CPU acceptance.':'Combined original diagnostics and read recovery; no installed, hardware or measured CPU acceptance.'};
 fs.writeFileSync(path.join(validation,'READINESS-DIAGNOSTICS.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));if(report.status==='failed')process.exitCode=1;
}
