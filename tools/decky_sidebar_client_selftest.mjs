// Actual shipped client, browser IO/clock replaced with fakes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import { createRequire } from 'node:module';
const originalMissingBootstrapCase='missing bootstrap uses one capped local retry and recovers after a missed native event';
const tdpReceiptCase='TDP terminal diagnostic only follows the sent current operation';
const connectingPauseCase='hiding before OPEN does not restart the shared transport';
const restoredPauseFailureCase='hidden restored snapshot pause failure creates one recovery despite repeated stale packets';
const actionTimeoutCase='timeout never resends a possibly applied action';
const outageBackoffCase='visible sidebar recovers beyond three failures with capped outage backoff';
const lateBootstrapCase='late native bootstrap event replaces the cold retry immediately without duplicate work';
const manualConnectionCase='connection test reconnects existing binding without replaying writes';
const selectionArgs=process.argv.slice(2);
let exactCase=null,commandLifetimeOnly=false,legacyArtifactArgument=null,restartVariant=null;
for(let i=0;i<selectionArgs.length;i++){
 const argument=selectionArgs[i];
 if(argument.startsWith('--restart-variant=')){assert(restartVariant===null,'CLIENT_VARIANT_DUPLICATE');restartVariant=argument.slice('--restart-variant='.length);assert(['visible-after-opening-timeout','hidden-proof-lifetimes','native-reply-proof','disconnect-diagnostics','open-before-first-reply','plugin-disposal-sources','entry-factory-activation','official-import-lifetime','official-import-no-replay','client-diagnostic','snapshot-delivery-refusal'].includes(restartVariant),'CLIENT_VARIANT_UNKNOWN');continue;}
 if(argument==='--command-lifetime'){
  assert(!commandLifetimeOnly,'CLIENT_CASE_SELECTION_DUPLICATE');commandLifetimeOnly=true;continue;
 }
 if(argument==='--case'||argument.startsWith('--case=')){
  assert(exactCase===null,'CLIENT_CASE_SELECTION_DUPLICATE');
  const value=argument==='--case'?selectionArgs[++i]:argument.slice('--case='.length);
  assert(typeof value==='string'&&value.length>0&&!value.startsWith('--'),'CLIENT_CASE_SELECTION_EMPTY');
  assert([originalMissingBootstrapCase,tdpReceiptCase,connectingPauseCase,restoredPauseFailureCase,actionTimeoutCase,outageBackoffCase,lateBootstrapCase,manualConnectionCase,'non-loopback bootstrap rejected','restart run ID disconnects and no mutation replay','port zero and out-of-range bootstrap are rejected',"24 mixed restart cycles fence silent connections old replies deadlines and duplicate edges","OPEN wallpaper context without response times out and recovers without replay","wallpaper timeout never replays an uncertain business write",'malformed snapshot rejects instead of crashing React','request freezes identity/generation/revision and args','slider rendered revision change rejects before sending any mutation'].includes(value),'CLIENT_CASE_SELECTION_UNKNOWN');
  exactCase=value;continue;
 }
 // Original parent/release callers pass one absolute artifact directory as a
 // redundant argument. Output ownership still comes from the original env.
 assert(typeof argument==='string'&&argument.length>0&&!argument.startsWith('-')&&path.isAbsolute(argument)&&legacyArtifactArgument===null,'CLIENT_ARGUMENT_UNKNOWN:'+argument);
 legacyArtifactArgument=argument;
}
assert(!(exactCase!==null&&commandLifetimeOnly),'CLIENT_CASE_SELECTION_CONFLICT');
assert(restartVariant===null||exactCase==='restart run ID disconnects and no mutation replay','CLIENT_VARIANT_CASE_CONFLICT');
const skipped=[];
// Admission precedes local fixture imports, output preparation and client VM.
const {performanceModelModule}=await import('./fixtures/decky_game_memory_store.mjs');
const {task}=await import('./fixtures/decky_task_paths.mjs');
const rawPath=path.resolve(task,'validation/MIRROR-CLIENT-SELFTEST.json');
if(fs.lstatSync(rawPath,{throwIfNoEntry:false}))throw new Error('Refusing to overwrite existing mirror client selftest raw: '+rawPath);
const root = path.resolve(import.meta.dirname, '..');
const require = createRequire(path.join(root,'package.json'));
const { transformSync } = require('esbuild');
const contextModule={exports:{}};
vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'decky-plugin/src/steamRunningContext.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,{module:contextModule,exports:contextModule.exports});
// Execute the same shared display DTO validator that the shipped client imports.
const gameContextModule={exports:{}};
let steamDisplayIdReads=0;
vm.runInNewContext(transformSync(fs.readFileSync(path.join(root,'src/bridge/gameSteamContextModel.ts'),'utf8'),{loader:'ts',format:'cjs'}).code,
 {module:gameContextModule,exports:gameContextModule.exports,BigInt:value=>{steamDisplayIdReads++;return BigInt(value);}});
const openingDeadlineObservations=[],optionalContextObservations=[];
const module = { exports: {} };
vm.runInNewContext(transformSync(fs.readFileSync(process.env.YMCC_MIRROR_CLIENT_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/mirrorClient.ts'),'utf8'), { loader:'ts',format:'cjs' }).code,
  { module,exports:module.exports,structuredClone,require: name => { if(name==='./performanceModel')return performanceModelModule;if(name==='./steamRunningContext')return contextModule.exports;if(name==='../../src/bridge/gameSteamContextModel')return gameContextModule.exports; throw new Error('Unexpected external dependency'); } });
const { MirrorClient } = module.exports;
// Consume the actual entry-point provider expression. It must reread the
// current native window property, including a binding supplied after startup.
const ts=require('typescript'),entryText=fs.readFileSync(process.env.YMCC_FAN_UI_SOURCE_OVERRIDE||path.join(root,'decky-plugin/src/index.tsx'),'utf8');
const entryTree=ts.createSourceFile('index.tsx',entryText,ts.ScriptTarget.Latest,true,ts.ScriptKind.TSX);
const entryFactory=entryTree.statements.find(node=>ts.isFunctionDeclaration(node)&&node.name?.text==='createYmccPlugin'&&node.modifiers?.some(m=>m.kind===ts.SyntaxKind.ExportKeyword)&&node.modifiers?.some(m=>m.kind===ts.SyntaxKind.DefaultKeyword));
const entryStatements=entryFactory?.body?.statements??entryTree.statements;
const clientDeclaration=entryStatements.filter(ts.isVariableStatement).flatMap(node=>[...node.declarationList.declarations]).find(node=>node.name.getText(entryTree)==='client');
assert(clientDeclaration&&ts.isNewExpression(clientDeclaration.initializer)&&clientDeclaration.initializer.expression.getText(entryTree)==='MirrorClient','Actual entry-point MirrorClient owner missing');
const provider=clientDeclaration.initializer.arguments[0].properties.find(node=>ts.isPropertyAssignment(node)&&node.name.getText(entryTree)==='bootstrap');
assert(provider&&ts.isArrowFunction(provider.initializer),'Actual entry-point bootstrap provider missing');
const readBootstrap=host=>vm.runInNewContext('('+provider.initializer.getText(entryTree)+')',{window:host});
// Scoped execution reuses the original consumers; inherited full-suite failures
// remain failures, and skipped names are never counted as executed.
const lifetimeCases=new Set([
 'hidden/unloaded releases socket and timers','old view packets ignored after hide',
 'request freezes identity/generation/revision and args','second mutation cannot overlap first',
 actionTimeoutCase,'close rejects inflight mutation and reconnect does not replay it',
 'power not ready rejects changes without send','fan requests do not include game identity',
 'new native run cancels old mutation, never replays into new host',
 'slider accepts only the exact rendered snapshot','slider rendered revision change rejects before sending any mutation',
 'slider game identity cannot migrate to a different foreground game',
 'monitor accepts one boolean intent without requiring a game or fan permission',
 'hiding during busy admission preserves one accepted write until its reply',
 'a timeout observer hiding the last page cannot restart paused passive snapshot demand',
 ...['hud','wallpaper','updater'].map(c=>'failed hidden pause recovers '+c+' transport without replaying an uncertain action')
]);
const cases=[],visibleReconnectRows=[],openingReadMeasurements=[],startupDeadlineRows=[],disconnectObservations=[];
const tdpReceiptObservations=[];
function writeRaw(failure=null) {
 const results=failure?[...cases,{name:failure.name,status:'failed',error:failure.error?.stack||String(failure.error)}]:cases;
 const raw={disconnectObservations,optionalContextObservations,openingDeadlineObservations,restartVariant,visibleReconnectRows,openingReadMeasurements,startupDeadlineRows,project:'YMCC Decky 侧边栏',cases:results.length,selection:exactCase!==null?'exact-case':commandLifetimeOnly?'command-lifetime':'all',...(exactCase!==null?{selectedCases:[exactCase]}:{}),skipped,results,realSockets:false,hardware:false,productionClientSource:true};
 if(failure)Object.assign(raw,{status:'failed',executed:results.length,passed:cases.length,failed:1,stoppedEarly:true,remaining:{status:'NOT_RUN',afterCase:failure.name,scope:'All subsequent check calls were not reached; their selection was not evaluated and they are excluded from passed and skipped.'}});
 if(tdpReceiptObservations.length)raw.tdpReceiptObservations=tdpReceiptObservations;
 const fd=fs.openSync(rawPath,'wx');
 try{fs.writeFileSync(fd,JSON.stringify(raw,null,2));fs.fsyncSync(fd);}finally{fs.closeSync(fd);}
}
async function check(name,action) { if((exactCase!==null&&name!==exactCase)||(commandLifetimeOnly&&!lifetimeCases.has(name))){skipped.push(name);return;}try{await action();cases.push({name,status:'passed'});}catch(error){try{writeRaw({name,error});}catch(rawError){console.error('Failed to preserve mirror client selftest failure raw at '+rawPath+':',rawError);}throw new Error(name+': '+error.message,{cause:error});} }
function fixture({boot=true,steam,now}={}) {
  const timers=new Map(); const sockets=[];let next=0;
  const bootstrap={endpoint:'ws://127.0.0.1:12345/mirror',token:'TEST-ONLY',runId:'run-1'};
  class Socket {
    listeners=new Map();sent=[];closed=false;
    constructor(url){this.url=url;sockets.push(this);}
    addEventListener(e,f){this.listeners.set(e,f);}
    send(value){this.sent.push(JSON.parse(value));}
    close(code){this.closeCode=code;this.closed=true;this.listeners.get('close')?.();}
    emit(e,value){this.listeners.get(e)?.({data:typeof value==='string'?value:JSON.stringify(value)});}
  }
  const nativeWindow={__YMCC_DECKY_MIRROR__:boot?bootstrap:undefined};
  const deps={bootstrap:readBootstrap(nativeWindow),socket:url=>new Socket(url),schedule:(action,delay)=>{const id=++next;timers.set(id,{action,delay});return id;},cancel:id=>timers.delete(id),clientId:'test-client',steam,now};
  const client=new MirrorClient(deps);
  const fireDelay=delay=>{const pair=[...timers].find(([,t])=>t.delay===delay);assert.ok(pair,`No timer ${delay}`);timers.delete(pair[0]);pair[1].action();};
  const snapshot=(revision=1)=>({runId:'run-1',generation:1,revision,ready:true,game:{label:'A',identity:'app:480:pid101:birthA',fields:{acMode:{value:'balanced',supported:true,choices:[{data:'balanced',label:'平衡'}]}}},fan:{supported:true,enabled:true,preset:'soft',choices:[{data:'soft',label:'轻柔'}]}});
  const connect=()=>{client.setVisible(true);const socket=sockets.at(-1);socket.emit('open');socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snapshot()});return socket;};
  return {nativeWindow,client,sockets,timers,bootstrap,fireDelay,snapshot,connect,makeBootstrapAvailable:()=>{nativeWindow.__YMCC_DECKY_MIRROR__=bootstrap;}};
}
// These lease assertions extend the one original missing-native-binding case.
// They have no standalone producer, case IDs or additional parent/release gate.
async function verifyBootstrapWaitOwnership(){
 const flush=async()=>{for(let n=0;n<80;n++)await Promise.resolve();};
 const verify=async(_detail,action)=>{await action();};
await verify('binding withdrawn before queued availability callback keeps the original observation lease and spent backoff',async()=>{
 const f=fixture({boot:false});try{let called=0;f.client.waitForBootstrap({},()=>called++);f.makeBootstrapAvailable();f.fireDelay(250);f.nativeWindow.__YMCC_DECKY_MIRROR__=undefined;await flush();assert.equal(called,0,'withdrawn native binding cannot consume one-shot notification');assert.equal(f.timers.size,1,'withdrawn binding keeps one original observation retry');assert.equal([...f.timers.values()][0].delay,1000,'withdrawn binding cannot refill spent backoff');assert.equal(f.sockets.length,0);f.makeBootstrapAvailable();f.fireDelay(1000);await flush();assert.equal(called,1);assert.equal(f.timers.size,0);assert.equal(f.sockets.length,0);}finally{f.client.dispose();}
});
await verify('local-only long wait observes actual provider but grants no socket until explicit feature demand',async()=>{
 const f=fixture({boot:false});try{
  f.bootstrap.wallpaperProtocol=1;let notices=0;const owner={};
  const stop=f.client.waitForBootstrap(owner,()=>{notices++;f.client.bootstrapChanged();f.client.setPassiveDemand('wallpaper',true);});
  assert.equal(f.timers.size,1);assert.equal(f.sockets.length,0);assert.equal(f.client.waitForBootstrap(owner,()=>assert.fail('duplicate callback')),stop);
  for(const delay of [250,1000,3000,10000,30000,30000]){assert.equal([...f.timers.values()][0].delay,delay);f.fireDelay(delay);assert.equal(f.sockets.length,0);assert.equal(f.timers.size,1);}
  f.makeBootstrapAvailable();f.fireDelay(30000);assert.equal(f.sockets.length,0);await flush();assert.equal(notices,1);assert.equal(f.sockets.length,1);
  f.sockets[0].emit('open');assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);assert.equal(f.sockets[0].sent.length,0);stop();
 }finally{f.client.dispose();}assert.equal(f.timers.size,0);
});
await verify('available binding only notifies asynchronously and never activates transport by itself',async()=>{
 const f=fixture();try{let called=0;f.client.waitForBootstrap({},()=>called++);assert.equal(called,0);assert.equal(f.sockets.length,0);await flush();assert.equal(called,1);assert.equal(f.sockets.length,0);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await verify('two waiting feature owners share one timer and releasing one keeps the original deadline',async()=>{
 const f=fixture({boot:false});try{const seen=[];const a=f.client.waitForBootstrap({},()=>seen.push('a'));f.client.waitForBootstrap({},()=>seen.push('b'));const id=[...f.timers.keys()][0];a();assert.equal(f.timers.size,1);assert(f.timers.has(id));f.makeBootstrapAvailable();f.fireDelay(250);await flush();assert.deepEqual(seen,['b']);assert.equal(f.timers.size,0);assert.equal(f.sockets.length,0);}finally{f.client.dispose();}
});
await verify('cancelled local retry delivered in the same socket epoch cannot erase a later waiter retry',async()=>{
 const f=fixture({boot:false});try{let notices=0;const stop=f.client.waitForBootstrap({},()=>notices++),late=[...f.timers.values()][0].action;stop();assert.equal(f.timers.size,0);f.client.waitForBootstrap({},()=>notices++);const id=[...f.timers.keys()][0];assert.equal([...f.timers.values()][0].delay,1000);late();assert(f.timers.has(id));assert.equal(f.timers.size,1);f.makeBootstrapAvailable();f.fireDelay(1000);await flush();assert.equal(notices,1);assert.equal(f.sockets.length,0);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await verify('local binding wait beside a healthy HUD does not close or duplicate its socket',async()=>{
 const f=fixture();try{f.bootstrap.hudProtocol=1;const socket=f.connect();f.client.setPassiveDemand('hud',true);f.client.setVisible(false);f.nativeWindow.__YMCC_DECKY_MIRROR__=undefined;let called=0;f.client.waitForBootstrap({},()=>called++);assert.equal(socket.closed,false);assert.equal(f.sockets.length,1);assert.equal(f.timers.size,1);f.makeBootstrapAvailable();f.fireDelay(250);await flush();assert.equal(called,1);assert.equal(socket.closed,false);assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);}finally{f.client.dispose();}
});
await verify('last network owner hiding cannot revoke a separate feature binding wait',async()=>{
 const f=fixture({boot:false});try{let called=0;f.client.setVisible(true);const late=[...f.timers.values()][0].action;f.client.waitForBootstrap({},()=>called++);f.client.setVisible(false);assert.equal(f.timers.size,1);assert.equal([...f.timers.values()][0].delay,1000);late();assert.equal(f.timers.size,1);f.makeBootstrapAvailable();f.fireDelay(1000);await flush();assert.equal(called,1);assert.equal(f.timers.size,0);assert.equal(f.sockets.length,0);}finally{f.client.dispose();}
});
for(const raw of [{runId:'native-revoked',revoked:true},{endpoint:'ws://example.com:12345/mirror',token:'fake',runId:'wrong',wallpaperProtocol:1},{endpoint:'ws://127.0.0.1:0/mirror',token:'fake',runId:'wrong',wallpaperProtocol:1},{endpoint:'ws://127.0.0.1:12345/mirror',runId:'missing-token',wallpaperProtocol:1}])await verify('available OFF or invalid binding ends observation without bypassing protocol/address identity '+raw.runId+String(raw.endpoint??''),async()=>{
  const f=fixture({boot:false});try{
   let called=0;f.client.waitForBootstrap({},()=>{called++;if(f.nativeWindow.__YMCC_DECKY_MIRROR__?.wallpaperProtocol===1)f.client.setPassiveDemand('wallpaper',true);});
   f.nativeWindow.__YMCC_DECKY_MIRROR__=raw;f.fireDelay(250);await flush();assert.equal(called,1);assert.equal(f.sockets.length,0);
   // The one-shot observation has ended. An explicit passive owner still owns
   // the original capped retry while this malformed binding remains unusable.
   const passiveRequested=raw.wallpaperProtocol===1;
   assert.equal(f.timers.size,passiveRequested?1:0,'Only explicit passive demand may retain its original retry');
   const late=[...f.timers.values()].map(timer=>timer.action);
   if(passiveRequested)assert.equal([...f.timers.values()][0].delay,250,'The explicit owner uses the original first backoff');
   f.client.setPassiveDemand('wallpaper',false);assert.equal(f.timers.size,0,'Released demand leaves no observation or transport timer');
   for(const action of late)action();await flush();assert.equal(called,1);assert.equal(f.sockets.length,0);assert.equal(f.timers.size,0,'A cancelled retry cannot revive released demand');
  }finally{f.client.dispose();}
});
await verify('invalid native-binding storms preserve spent retry deadlines and a valid correction connects immediately',async()=>{
 const invalidBindings=[
  {endpoint:'ws://example.com:12345/mirror',token:'fake',runId:'wrong'},
  {endpoint:'ws://127.0.0.1:0/mirror',token:'fake',runId:'wrong'},
  {endpoint:'ws://127.0.0.1:70000/mirror',token:'fake',runId:'wrong'},
  {endpoint:'ws://127.0.0.1:12345/mirror',runId:'missing-token'},
  {endpoint:'ws://127.0.0.1:12345/mirror',token:'fake'},
  {endpoint:'ws://127.0.0.1:12345/mirror',token:'x'.repeat(129),runId:'wrong'}
 ];
 for(const demand of ['visible','hud','wallpaper','updater'])for(const phase of ['cold','withdrawn'])for(const raw of invalidBindings){
  const f=fixture();try{
   Object.assign(f.bootstrap,{hudProtocol:1,wallpaperProtocol:1,updaterProtocol:1});
   if(phase==='cold')f.nativeWindow.__YMCC_DECKY_MIRROR__=raw;
   if(demand==='visible')f.client.setVisible(true);else f.client.setPassiveDemand(demand,true);
   if(phase==='withdrawn'){
    const old=f.sockets[0];old.emit('open');
    if(demand==='visible')old.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot()});
    f.nativeWindow.__YMCC_DECKY_MIRROR__=raw;f.client.bootstrapChanged();assert.equal(old.closed,true);
   }
   const socketCount=f.sockets.length;
   for(const delay of [250,1000,3000,10000,30000,30000]){
    const id=[...f.timers.keys()][0];assert.equal(f.timers.size,1);assert.equal(f.timers.get(id).delay,delay);
    for(let n=0;n<128;n++)f.client.bootstrapChanged();
    assert(f.timers.has(id),'Invalid binding notifications must preserve the existing retry handle and deadline');
    assert.equal(f.timers.size,1);assert.equal(f.sockets.length,socketCount);
    f.fireDelay(delay);assert.equal(f.sockets.length,socketCount);assert.equal(f.timers.size,1);
   }
   const late=[...f.timers.values()][0].action;
   f.makeBootstrapAvailable();f.client.bootstrapChanged();assert.equal(f.sockets.length,socketCount+1);
   const fresh=f.sockets.at(-1);fresh.emit('open');
   if(demand==='visible')fresh.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot()});
   assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
   late();assert.equal(f.sockets.length,socketCount+1);assert.equal(f.timers.size,0);
   assert(f.sockets.flatMap(socket=>socket.sent).every(message=>message.command==='snapshot'));
  }finally{f.client.dispose();}
  assert.equal(f.timers.size,0);assert(f.sockets.every(socket=>socket.closed));
 }
});
await verify('one observer throwing cannot block another one-shot observer or retain timers',async()=>{
 const f=fixture({boot:false});try{let called=0;f.client.waitForBootstrap({},()=>{throw Error('inert observer failure')});f.client.waitForBootstrap({},()=>called++);f.makeBootstrapAvailable();f.fireDelay(250);await flush();assert.equal(called,1);assert.equal(f.timers.size,0);assert.equal(f.sockets.length,0);}finally{f.client.dispose();}
});
await verify('queued availability and retry callbacks cannot revive a released or disposed owner',async()=>{
 for(const boundary of ['release','dispose']){const f=fixture({boot:false});let called=0;const stop=f.client.waitForBootstrap({},()=>called++),late=[...f.timers.values()][0].action;f.makeBootstrapAvailable();f.fireDelay(250);if(boundary==='release')stop();else f.client.dispose();await flush();late();assert.equal(called,0);assert.equal(f.timers.size,0);assert.equal(f.sockets.length,0);f.client.dispose();f.client.waitForBootstrap({},()=>called++);await flush();assert.equal(called,0);}
});
await verify('reentrant next waiting lease survives the old availability unsubscribe',async()=>{
 const f=fixture({boot:false});try{const owner={};let called=0,second=0;const old=f.client.waitForBootstrap(owner,()=>{called++;f.nativeWindow.__YMCC_DECKY_MIRROR__=undefined;f.client.waitForBootstrap(owner,()=>second++);});f.makeBootstrapAvailable();f.fireDelay(250);await flush();old();assert.equal(called,1);assert.equal(second,0);assert.equal(f.timers.size,1);f.makeBootstrapAvailable();f.fireDelay(1000);await flush();assert.equal(second,1);assert.equal(f.timers.size,0);assert.equal(f.sockets.length,0);}finally{f.client.dispose();}
});

 // The native health marker revokes authority even if a mixed/stale producer
 // accidentally leaves otherwise complete credentials on the same object.
 for(const boundary of ['initial','connected','observational-wait']){
  const f=fixture({boot:boundary!=='observational-wait'});
  try{
   f.bootstrap.wallpaperProtocol=1;
   let notified=0,old=null;
   if(boundary==='connected')old=f.connect();
   if(boundary==='observational-wait')f.client.waitForBootstrap({},()=>{notified++;f.client.setPassiveDemand('wallpaper',true);});
   f.bootstrap.revoked=true;
   if(boundary==='initial')f.client.setVisible(true);
   else if(boundary==='connected')f.client.bootstrapChanged();
   else{f.makeBootstrapAvailable();f.fireDelay(250);await flush();assert.equal(notified,1);}
   assert.equal(f.client.snapshot().connected,false);assert.equal(f.client.snapshot().snapshot,null);
   assert.equal(f.timers.size,0);assert.equal(f.sockets.length,boundary==='connected'?1:0);
   if(old){assert.equal(old.closed,true);old.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot(9999)});assert.equal(f.client.snapshot().connected,false);}
   // A later explicit native grant may start a fresh transport, never the old
   // revoked one. There is no automatic retry while revocation remains true.
   f.bootstrap.revoked=false;f.client.bootstrapChanged();
   const current=f.sockets.at(-1);assert(current);if(old)assert.notEqual(current,old);
   current.emit('open');if(boundary!=='observational-wait')current.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot()});
   assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
  }finally{f.client.dispose();}
  assert.equal(f.timers.size,0);
 }
}
await check('missing bootstrap uses one capped local retry and recovers after a missed native event',async()=>{
 // Attribute first transport time without adding any socket, retry or write.
 let clock=100;const timed=fixture({boot:false,now:()=>clock});
 try{
  timed.client.setVisible(true);
  for(const delay of [250,1000,3000]){clock+=delay;timed.fireDelay(delay);}
  timed.makeBootstrapAvailable();timed.client.bootstrapChanged();
  assert.equal(timed.sockets.length,1);const peer=timed.sockets[0];clock+=274;peer.emit('open');
  const report=peer.sent.find(p=>p.clientStartup)?.clientStartup;
  assert(report,'First snapshot must carry the one bounded client timing record');
  assert.deepEqual(JSON.parse(JSON.stringify(report)),{schema:1,elapsedMs:4524,bootstrapReadyMs:4250,socketOpenMs:274,bootstrapMisses:4,socketAttempts:1,retryCount:4,scheduledRetryMs:14250});
  peer.emit('message',{type:'snapshot',runId:timed.bootstrap.runId,snapshot:timed.snapshot()});
  for(let n=0;n<1000;n++)timed.client.bootstrapChanged();
  const proof=timed.client.testConnection(),read=peer.sent.at(-1);
  peer.emit('message',{type:'snapshot',runId:timed.bootstrap.runId,probeId:read.probeId,snapshot:{...timed.snapshot(2),provenance:{kind:'ymcc-native',pid:123}}});
  await proof;
  assert.equal(peer.sent.filter(p=>p.clientStartup).length,1,'Healthy demand changes cannot repeat the first-client report');
  peer.close();clock+=250;timed.fireDelay(250);const next=timed.sockets.at(-1);next.emit('open');
  next.emit('message',{type:'snapshot',runId:timed.bootstrap.runId,snapshot:timed.snapshot(2)});
  assert.equal(next.sent.filter(p=>p.clientStartup).length,0,'Later reconnects retain the original single diagnostic budget');
  assert.equal(timed.timers.size,0);assert(timed.sockets.flatMap(s=>s.sent).every(p=>p.command==='snapshot'));
 }finally{timed.client.dispose();}

 // The original missing-bootstrap case also owns delayed first availability:
 // waiting must not require a second page open or a notification to arrive.
 for(const demand of ['visible','hud','wallpaper','updater']){
  const f=fixture({boot:false});
  try{
   if(demand==='visible')f.client.setVisible(true);else f.client.setPassiveDemand(demand,true);
   assert.equal(f.sockets.length,0);assert.equal(f.timers.size,1);
   for(const delay of [250,1000,3000,10000,30000,30000]){
    const before=[...f.timers.keys()][0];assert.equal([...f.timers.values()][0].delay,delay);
    // A notification storm while the native owner is still absent must not
    // refill or postpone the original retry or open a connection.
    for(let n=0;n<128;n++)f.client.bootstrapChanged();
    assert.equal(f.timers.size,1);assert(f.timers.has(before));assert.equal(f.sockets.length,0);
    f.fireDelay(delay);assert.equal(f.sockets.length,0);assert.equal(f.timers.size,1);
   }
   const late=[...f.timers.values()][0].action;
   f.makeBootstrapAvailable();f.bootstrap.hudProtocol=1;f.bootstrap.wallpaperProtocol=1;f.bootstrap.updaterProtocol=1;
   f.fireDelay(30000);assert.equal(f.sockets.length,1);
   const socket=f.sockets[0];assert.ok(socket.url.startsWith(f.bootstrap.endpoint+'?'));
   socket.emit('open');
   if(demand==='visible')socket.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot()});
   assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
   late();assert.equal(f.sockets.length,1);assert.equal(f.timers.size,0);
   assert(socket.sent.every(message=>message.command==='snapshot'));
   if(demand==='visible'){
    const pending=f.client.mutate('fan.setEnabled',{enabled:false}),request=socket.sent.at(-1);
    assert.equal(request.command,'fan.setEnabled');assert.equal(request.runId,f.bootstrap.runId);
    socket.emit('message',{type:'reply',runId:f.bootstrap.runId,id:request.id,ok:true,result:{saved:true,applied:true}});
    assert.equal((await pending).applied,true);assert.equal(socket.sent.filter(message=>message.command==='fan.setEnabled').length,1);assert.equal(f.timers.size,0);
   }
  }finally{f.client.dispose();}
  assert.equal(f.timers.size,0);assert(f.sockets.every(socket=>socket.closed));
 }
 for(const boundary of ['hide','passive-off','dispose']){
  const f=fixture({boot:false});
  if(boundary==='passive-off')f.client.setPassiveDemand('wallpaper',true);else f.client.setVisible(true);
  assert.equal(f.timers.size,1);const late=[...f.timers.values()][0].action;
  if(boundary==='hide')f.client.setVisible(false);else if(boundary==='passive-off')f.client.setPassiveDemand('wallpaper',false);else f.client.dispose();
  assert.equal(f.timers.size,0);f.makeBootstrapAvailable();late();f.client.bootstrapChanged();
  assert.equal(f.sockets.length,0);assert.equal(f.timers.size,0);f.client.dispose();
 }
 await verifyBootstrapWaitOwnership();
});
function verifyRejectedVisibleBinding(endpoint){
 const f=fixture();try{
  f.bootstrap.endpoint=endpoint;f.client.setVisible(true);
  assert.equal(f.sockets.length,0,'A visible owner never bypasses the loopback endpoint restriction');
  assert.equal(f.timers.size,1,'Visible demand retains only the original binding-validation retry');
  assert.equal([...f.timers.values()][0].delay,250,'Malformed binding uses the original first backoff');
  f.fireDelay(250);assert.equal(f.sockets.length,0,'Revalidation cannot create a socket for a rejected endpoint');
  assert.equal(f.timers.size,1,'Revalidation retains one retry rather than adding a second owner');
  assert.equal([...f.timers.values()][0].delay,1000,'Rejected binding spends the original backoff without resetting it');
  const late=[...f.timers.values()].map(timer=>timer.action);
  f.client.setVisible(false);assert.equal(f.timers.size,0,'Releasing visible demand cancels the binding-validation retry');
  for(const action of late)action();assert.equal(f.sockets.length,0);assert.equal(f.timers.size,0,'A stale retry cannot revive released demand');
 }finally{f.client.dispose();}
}
await check('non-loopback bootstrap rejected',()=>verifyRejectedVisibleBinding('ws://example.com:12345/mirror'));
await check('one visible socket requests one snapshot, no idle polling',()=>{const f=fixture();const s=f.connect();assert.equal(s.sent.length,1);assert.equal(s.sent[0].command,'snapshot');assert.equal(f.timers.size,0);f.client.setVisible(true);assert.equal(f.sockets.length,1);f.client.dispose();});
await check('hidden/unloaded releases socket and timers',()=>{const f=fixture();f.connect();f.client.setVisible(false);assert.equal(f.timers.size,0);assert.equal(f.sockets[0].closed,true);assert.equal(f.client.snapshot().snapshot,null);});
await check('old view packets ignored after hide',()=>{const f=fixture();const s=f.connect();f.client.setVisible(false);s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(100)});assert.equal(f.client.snapshot().connected,false);});
await check('older revisions do not overwrite fresh state',()=>{const f=fixture();const s=f.connect();s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(10)});s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(5)});assert.equal(f.client.snapshot().snapshot.revision,10);f.client.dispose();});
// Parameterize the original malformed-snapshot case; no new canonical test ID.
async function verifyOptionalDisplayIsolation() {
 const target={pid:101,processCreated:'134000000000000001',path:'C:\\fixture\\selected.exe',identityGeneration:1};
 const identity=JSON.stringify(['selected',target.path,target.pid,target.processCreated,target.identityGeneration]);
 const metadata=()=>({schemaVersion:1,game:{...target,processValidated:false,rulesEpoch:'1',powerGeneration:'1'},steam:{status:'unknown',processValidated:false,currentSource:false,reason:'not-observed'}});
 const cached=()=>({...metadata(),steam:{status:'cached-association',appid:480,gameId:'480',gamePid:target.pid,gameCreated:target.processCreated,gamePath:target.path,gameIdentityGeneration:1,steamPid:202,steamCreated:'134000000000000002',processEpoch:'1',accountEpoch:'1',processValidated:false,currentSource:false}});
 const core=(f,revision=2)=>{const snap=f.snapshot(revision);snap.game.identity=identity;snap.actions={fan:true,game:true,performance:true};snap.performance={version:1,side:'dc',scope:'game',mode:'auto',sourceRevision:'fixture-power-1',enabled:true,controls:[{key:'tdp',kind:'slider',label:'TDP',enabled:true,value:20,min:5,max:30,step:1}]};return snap;};
 const copy=structuredClone;
 {
  const f=fixture();let checking;
  try{
   f.client.setVisible(true);
   const socket=f.sockets.at(-1);socket.readyState=1;socket.emit('open');
   const original={...core(f),provenance:{kind:'ymcc-native',pid:123}};
   socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:original});
   assert.equal(f.client.snapshot().snapshot,null,'A display cannot supply the first snapshot');
   assert.equal(f.client.connectionVerified('run-1'),false,'A display cannot supply the first native proof');
   socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:original});
   checking=f.client.testConnection().catch(error=>error);
   const nonce=socket.sent.at(-1).probeId,deadline=f.client.snapshotDeadline?.timer;
   assert(nonce&&f.timers.has(deadline));
   const display=copy(original);display.gameSteam=metadata();display.losslessProgram={known:true,running:true};
   display.performance.controls[0].value=29;
   socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,probeId:nonce,snapshot:display});
   assert(f.timers.has(deadline),'Optional display must not acknowledge a pending read');
   assert.equal(f.client.snapshot().connectionTest.pending,true,'Display cannot complete a current connection probe');
   assert.deepEqual(copy(f.client.snapshot().snapshot.performance),original.performance);
   assert.deepEqual(copy(f.client.snapshot().snapshot.gameSteam),display.gameSteam);
   assert.deepEqual(copy(f.client.snapshot().snapshot.losslessProgram),display.losslessProgram);
   let redraws=0;const stopDisplay=f.client.subscribe(()=>{redraws++;});
   try{
    const initial=redraws;
    for(let i=0;i<24;i++)socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:display});
    socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:{...display,gameSteam:{schemaVersion:0},losslessProgram:{known:false,running:true}}});
    assert.equal(redraws,initial,'Repeated or invalid display fields must not notify');
    const clear={...display,gameSteam:undefined};
    socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:clear});
    socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:clear});
    assert.equal(redraws,initial+1,'Actual removal still redraws, once');
    assert.equal(f.client.snapshot().snapshot.gameSteam,undefined);
    socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:display});
    assert.equal(redraws,initial+2,'Real metadata restoration still redraws');
    assert(f.timers.has(deadline),'None of these repaints can settle the probe');
   }finally{stopDisplay();}
   socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:{...display,generation:2,losslessProgram:{known:true,running:false}}});
   assert.equal(f.client.snapshot().snapshot.losslessProgram.running,true);
   socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:{...display,revision:3}});
   assert.equal(f.client.snapshot().snapshot.revision,2);
   const empty={...original,revision:3};
   socket.emit('message',{type:'snapshot',runId:'run-1',probeId:nonce,snapshot:empty});
   assert.equal((await checking).revision,3);assert.equal(f.timers.size,0);
   f.client.setPassiveVisible(true);f.client.setVisible(false);const before=socket.sent.length;
   socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:display});
   assert.equal(socket.sent.length,before,'A late optional display cannot restart hidden demand or send another pause');
   assert.equal(f.client.snapshot().snapshot,null);
   optionalContextObservations.push({kind:'display-only',readDeadlinePreserved:true,probeSettledByFullRevision:3,hiddenLateSends:0});
  }finally{f.client.dispose();await checking;}assert.equal(f.timers.size,0);
 }
 const badGroup=cached();badGroup.steam.processGroup={status:'incomplete',scope:'process-lifetime',processValidated:false,currentSource:false,roleValidated:false,reason:'future-schema',futureField:1};
 const variants=[
  ['null',null],['array',[]],['primitive','future-version'],
  ['future-schema',{...metadata(),schemaVersion:2}],
  ['unknown-field',{...metadata(),steam:{...metadata().steam,futureField:1}}],
  ['claimed-process-authority',{...metadata(),steam:{...metadata().steam,processValidated:true}}],
  ['claimed-current-authority',{...metadata(),steam:{...metadata().steam,currentSource:true}}],
  ['foreign-game-pid',{...metadata(),game:{...metadata().game,pid:303}}],
  ['reused-game-pid',{...metadata(),game:{...metadata().game,processCreated:'134000000000000009'}}],
  ['foreign-game-path',{...metadata(),game:{...metadata().game,path:'C:\\fixture\\other.exe'}}],
  ['retired-game-generation',{...metadata(),game:{...metadata().game,identityGeneration:2}}],
  ['retired-power-generation',{...metadata(),game:{...metadata().game,powerGeneration:'2'}}],
  ['bad-rules-epoch',{...metadata(),game:{...metadata().game,rulesEpoch:'unknown'}}],
  ['future-process-group',badGroup]
 ];
 for(const [name,value] of variants){
  const f=fixture();try{
   const socket=f.connect();socket.readyState=1;const snap=core(f);snap.gameSteam=copy(value);
   socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});
   const shown=f.client.snapshot();
   optionalContextObservations.push({kind:'invalid-display',variant:name,connected:shown.connected,socketClosed:socket.closed,sockets:f.sockets.length,timers:f.timers.size});
   assert.equal(shown.connected,true,'optional display metadata must not disconnect a valid core snapshot');
   assert.equal(socket.closed,false);assert.equal(f.sockets.length,1);assert.equal(f.timers.size,0);
   assert.equal(shown.snapshot.gameSteam,undefined,'invalid display metadata is omitted, never promoted');
   assert.equal(shown.snapshot.game.identity,identity);assert.equal(f.client.connectionReady('run-1'),true);
   const pending=f.client.mutate('performance.setControls',{patch:{tdp:21}},shown.snapshot),request=socket.sent.at(-1);
   assert.equal(request.command,'performance.setControls');assert.equal(request.runId,'run-1');
   assert.equal(request.identity,identity);assert.equal(request.generation,1);assert.equal(request.revision,2);
   assert.deepEqual(request.args,{patch:{tdp:21}});assert.equal(Object.hasOwn(request,'gameSteam'),false);
   socket.emit('message',{type:'reply',runId:'run-1',id:request.id,ok:true,result:{saved:true,applied:true}});await pending;
   assert.equal(socket.sent.filter(row=>row.command==='performance.setControls').length,1,'one intent has no replay');
  }finally{f.client.dispose();}assert.equal(f.timers.size,0);
 }
 const group=cached();group.steam.processGroup={status:'observed',scope:'process-lifetime',processValidated:false,currentSource:false,roleValidated:false,reason:'cached-only',sourceRevision:'7',selectedPid:target.pid,selectedSourceLine:11,appid:480,gameId:'480',gameIdRaw:'480',gameIdKind:'native-app',members:[{pid:target.pid,path:target.path,observedAt:'2026-11-13 10:00:00',sourceLine:11,gameIdRaw:'480'}]};
 for(let i=1;i<32;i++)group.steam.processGroup.members.push({...group.steam.processGroup.members[0],pid:target.pid+i,sourceLine:11+i});
 {
  const f=fixture();try{
   const socket=f.connect(),base=core(f);socket.readyState=1;
   const start=steamDisplayIdReads;
   socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...base,gameSteam:group}});
   const full=steamDisplayIdReads-start;
   assert.deepEqual(f.client.snapshot().snapshot.gameSteam,group);
   socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:base});
   const startDisplay=steamDisplayIdReads;
   socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:{...base,gameSteam:group}});
   const display=steamDisplayIdReads-startDisplay;
   assert.deepEqual(f.client.snapshot().snapshot.gameSteam,group);
   for(const edit of [v=>{delete v.game.rulesEpoch;},v=>{delete v.game.powerGeneration;},
      v=>{v.steam.processGroup.members[31].pid=v.game.pid;},v=>{v.steam.processGroup.members[0].sourceLine++;},
      v=>{v.steam.processGroup.members.push({...v.steam.processGroup.members[31],pid:999});}]){
    const bad=copy(group);edit(bad);
    socket.emit('message',{type:'snapshot',runId:'run-1',displayOnly:true,snapshot:{...base,gameSteam:bad}});
    assert.deepEqual(f.client.snapshot().snapshot.gameSteam,group,'Malformed display packet must preserve the last valid view');
   }
   optionalContextObservations.push({kind:'process-group-validation',members:32,bigIntConversionsFull:full,bigIntConversionsDisplay:display});
   assert(full<=72&&display<=72,`One complete group validation per packet, full=${full}, display=${display}`);
  }finally{f.client.dispose();}assert.equal(f.timers.size,0);
 }
 for(const [name,value] of [['unknown',metadata()],['pending',{...metadata(),steam:{...metadata().steam,status:'pending'}}],['cached-association',cached()],['cached-process-group',group],['no-display',undefined]]){
  const f=fixture();try{const socket=f.connect();socket.readyState=1;const snap=core(f);if(value!==undefined)snap.gameSteam=value;socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});
   assert.equal(f.client.snapshot().connected,true);assert.deepEqual(f.client.snapshot().snapshot.gameSteam,value);
   optionalContextObservations.push({kind:'valid-display',variant:name,connected:true,retained:true});
  }finally{f.client.dispose();}assert.equal(f.timers.size,0);
 }
 {
  const f=fixture();try{const socket=f.connect();socket.readyState=1;socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:core(f)});
   const pending=f.client.mutate('performance.setControls',{patch:{tdp:22}},f.client.snapshot().snapshot),request=socket.sent.at(-1);
   for(let i=0;i<16;i++){const snap=core(f,3+i);snap.gameSteam=copy(variants[i%variants.length][1]);socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});assert.equal(f.client.snapshot().connected,true);assert.equal(f.client.snapshot().busy,true);}
   assert.equal(socket.sent.filter(row=>row.command==='performance.setControls').length,1);
   socket.emit('message',{type:'reply',runId:'run-1',id:request.id,ok:true,result:{saved:true,applied:true}});await pending;
   assert.equal(f.client.snapshot().busy,false);assert.equal(socket.closed,false);assert.equal(f.sockets.length,1);
   optionalContextObservations.push({kind:'pending-intent',metadataUpdates:16,writes:1,replayed:false,replyAccepted:true});
  }finally{f.client.dispose();}assert.equal(f.timers.size,0);
 }
 for(const kind of ['run','generation','revision','game-identity','performance']){
  const f=fixture();try{const socket=f.connect(),snap=core(f);snap.gameSteam=metadata();
   if(kind==='run')snap.runId='foreign-run';else if(kind==='generation')snap.generation='1';else if(kind==='revision')snap.revision=-1;else if(kind==='game-identity')snap.game.identity={not:'identity'};else snap.performance.controls[0].step=0;
   socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});assert.equal(f.client.snapshot().connected,false);assert.equal(socket.closed,true);
   assert.equal(socket.sent.filter(row=>row.command==='performance.setControls').length,0);
   optionalContextObservations.push({kind:'core-rejected',variant:kind,connected:false,writes:0});
  }finally{f.client.dispose();}assert.equal(f.timers.size,0);
 }
 for(const kind of ['ready','permission','capability','stale-rendered-revision']){
  const f=fixture();try{const socket=f.connect();socket.readyState=1;const snap=core(f);snap.gameSteam={...metadata(),schemaVersion:2};
   if(kind==='ready')snap.ready=false;else if(kind==='permission')snap.actions.performance=false;else if(kind==='capability')snap.performance.enabled=false;
   socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});const rendered=f.client.snapshot().snapshot;
   if(kind==='stale-rendered-revision'){const next=core(f,3);next.gameSteam=metadata();socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:next});}
   await assert.rejects(f.client.mutate('performance.setControls',{patch:{tdp:21}},rendered));
   assert.equal(f.client.snapshot().connected,true);assert.equal(socket.sent.filter(row=>row.command==='performance.setControls').length,0);
   optionalContextObservations.push({kind:'intent-rejected',variant:kind,connected:true,writes:0});
  }finally{f.client.dispose();}assert.equal(f.timers.size,0);
 }
}

await check('malformed snapshot rejects instead of crashing React',async()=>{const f=fixture();try{const s=f.connect();s.emit('message',{type:'snapshot',runId:'run-1',snapshot:{runId:'run-1',revision:2,generation:1}});assert.equal(f.client.snapshot().connected,false);assert.equal(s.closed,true);}finally{f.client.dispose();}await verifyOptionalDisplayIsolation();});
async function verifyVisibleReconnectAfterOpeningTimeout(){
 for(const passiveConnection of [false,true]){
  const f=fixture();try{
   const old=f.connect();old.readyState=1;
   if(passiveConnection)f.client.setPassiveDemand('connection',true);
   const opening=f.client.checkConnectionOnOpen();
   const late=[...f.timers.values()].find(t=>t.delay===15000).action;
   f.fireDelay(15000);assert.equal(await opening,true);
   old.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...f.snapshot(2),provenance:{kind:'ymcc-native',pid:123}}});
   assert.equal(f.client.snapshot().connected,true);
   assert.equal(f.client.connectionReady('run-1'),false,'A timed-out opening remains unconfirmed before fresh recovery');
   old.close();f.fireDelay(250);
   const next=f.sockets.at(-1);assert.notEqual(next,old);next.readyState=1;next.emit('open');
   const request=next.sent.find(p=>p.command==='snapshot'&&p.subscribe!==false);assert(request);
   for(let n=0;n<1000;n++)next.emit('open');
   next.emit('message',{type:'snapshot',runId:'run-1',...(request.probeId?{probeId:request.probeId}:{}),snapshot:{...f.snapshot(3),provenance:{kind:'ymcc-native',pid:123}}});
   await new Promise(resolve=>setImmediate(resolve));
   const row={passiveConnection,ordinaryConnected:f.client.snapshot().connected,connectionReady:f.client.connectionReady('run-1'),freshNonce:typeof request.probeId==='string',snapshotRequests:next.sent.filter(p=>p.command==='snapshot'&&p.subscribe!==false).length,timers:f.timers.size};
   visibleReconnectRows.push(row);
   assert.equal(row.connectionReady,true,'Visible same-run reconnect must clear the failed opening proof using a fresh native receipt');
   assert.equal(row.freshNonce,true,'A reconnect proof must correlate its own native snapshot');
   assert.equal(row.snapshotRequests,1,'A socket opening owns one native proof despite duplicate open events');
   assert.equal(row.timers,0,'A confirmed visible recovery leaves no idle polling timer');
   late();assert.equal(f.client.connectionReady('run-1'),true,'A late timed-out callback cannot revoke a recovered connection');
   assert(f.sockets.flatMap(s=>s.sent).every(p=>p.command==='snapshot'),'Connection recovery never replays a business command');
  }finally{f.client.dispose();}
  assert.equal(f.timers.size,0);assert(f.sockets.every(s=>s.closed));
 }
}

// Join the actual client and relay through the existing inert Socket. The
// server still forces a fresh read for every correlated probe; only redundant
// client demand from the very same opening can disappear.
async function verifyOpeningProbeReadCoalescing(){
 const {load}=await import('./fixtures/decky_game_memory_store.mjs');
 const {ReadOnlyMirrorRelay}=load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':contextModule.exports});
 const f=fixture(),run='a'.repeat(32);f.bootstrap.runId=run;f.client.setPassiveDemand('connection',true);
 const socket=f.sockets.at(-1);socket.readyState=1;socket.emit('open');
 const initial=socket.sent.filter(packet=>packet.command==='snapshot'&&packet.probeId).at(-1);assert(initial);
 socket.emit('message',{type:'snapshot',runId:run,probeId:initial.probeId,snapshot:{...f.snapshot(),runId:run,provenance:{kind:'ymcc-native',pid:123}}});
 for(let n=0;n<80;n++)await Promise.resolve();await new Promise(resolve=>setImmediate(resolve));
 assert.equal(f.client.connectionChecking(run),false);assert.equal(f.timers.size,0);
 let releaseFirst,reads=0;const held=new Promise(resolve=>releaseFirst=resolve),pending=[],wire=[];
 const peer={connection:'1',runId:run,connected:true};
 const relay=new ReadOnlyMirrorRelay({read:async()=>{reads++;if(reads===1)await held;const {runId,revision,...view}=f.snapshot(reads);return {...view,provenance:{kind:'ymcc-native',pid:123}};},
  observe:()=>()=>{},reply:async(_connection,message)=>{socket.emit('message',message);return true;},
  schedule:()=>{throw Error('Healthy opening cannot schedule a server recovery timer');},cancel:()=>{}});
 relay.connected(peer);const originalSend=socket.send.bind(socket);
 socket.send=raw=>{originalSend(raw);const packet=JSON.parse(raw);wire.push(packet);pending.push(relay.request({...peer,request:packet}));};
 let opening;
 try{
  opening=f.client.checkConnectionOnOpen();for(let n=0;n<80;n++)await Promise.resolve();
  const requests=wire.filter(packet=>packet.command==='snapshot'&&packet.subscribe!==false);
  releaseFirst();await opening;for(let n=0;n<80;n++)await Promise.resolve();await Promise.all(pending);
  const row={mode:'hidden-retained-opening',snapshotRequests:requests.length,probeRequests:requests.filter(packet=>typeof packet.probeId==='string').length,actualRelayReads:reads,ready:f.client.connectionReady(run),remainingTimers:f.timers.size,businessWrites:wire.filter(packet=>packet.command!=='snapshot').length};openingReadMeasurements.push(row);
  assert.equal(row.snapshotRequests,1,'One opening must send only its correlated fresh request');assert.equal(row.probeRequests,1);
  assert.equal(row.actualRelayReads,1,'One opening must not invalidate and repeat its own full owner read');assert.equal(row.ready,true);assert.equal(row.remainingTimers,0);assert.equal(row.businessWrites,0);
 }finally{releaseFirst();f.client.dispose();relay.dispose();if(opening)await opening;await Promise.all(pending);assert.equal(f.timers.size,0);}
}


async function verifySnapshotObserverReentry(){
 for(const boundary of ['same-socket','new-run','dispose']){
  const f=fixture();let active=false,once=false,pending=null,newProbe=null,current=null,settled='pending',notifications=0;
  f.client.deps.connectionCheckCompleted=()=>notifications++;
  try{
   const old=f.connect();old.readyState=1;
   const off=f.client.subscribe(state=>{
    if(!active||once||state.snapshot?.revision!==2)return;once=true;
    if(boundary==='dispose'){f.client.dispose();return;}
    if(boundary==='new-run'){
     Object.assign(f.bootstrap,{endpoint:'ws://127.0.0.1:12346/mirror',token:'REENTRY-TEST',runId:'run-after-observer'});
     f.client.bootstrapChanged();current=f.sockets.at(-1);current.readyState=1;current.emit('open');
    }else current=old;
    pending=f.client.testConnection().then(value=>{settled='resolved';return value;},error=>{settled='rejected';return error;});
    newProbe=f.client.probe;assert(newProbe);
    current.emit('message',{type:'snapshot',runId:f.bootstrap.runId,probeId:newProbe.id,snapshot:{...f.snapshot(3),runId:f.bootstrap.runId,generation:0,ready:false}});
    assert.equal(newProbe.awaitingNative,true);
   });
   active=true;
   old.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...f.snapshot(2),provenance:{kind:'ymcc-native',pid:111}}});
   for(let n=0;n<40;n++)await Promise.resolve();
   assert.equal(once,true);
   if(boundary==='dispose'){
    assert.equal(f.timers.size,0);assert.equal(old.closed,true);assert.equal(notifications,0);
   }else{
    assert.equal(f.client.probe,newProbe,'retired snapshot must not settle observer-created native proof');
    assert.equal(settled,'pending','new observer read must wait for its own native reply');
    assert.equal(f.timers.has(newProbe.timer),true);assert.equal(notifications,0,'retired proof cannot publish completion for new demand');
    current.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:{...f.snapshot(4),runId:f.bootstrap.runId,provenance:{kind:'ymcc-native',pid:222}}});
    const result=await pending;assert.equal(result.pid,222);assert.equal(result.revision,4);assert.equal(settled,'resolved');
    assert.equal(f.timers.size,0);assert.equal(f.client.connectionVerified(f.bootstrap.runId),true);
    assert(f.sockets.flatMap(s=>s.sent).every(packet=>packet.command==='snapshot'),'observation cannot replay settings');
   }
   startupDeadlineRows.push({mode:'snapshot-observer-reentry',boundary,oldSnapshotPid:111,resolvedPid:boundary==='dispose'?null:222,oldMessageDidNotSettleNewProbe:true,timers:f.timers.size});
   off();
  }finally{f.client.dispose();if(pending)await pending;assert.equal(f.timers.size,0);assert(f.sockets.every(s=>s.closed));}
 }
}

async function verifyOpeningTeardownReentry(){
 for(const replacement of ['same-run','new-run']){
  const f=fixture();let active=false,once=false,current=null,opening=null;
  try{
   const old=f.connect();old.readyState=1;
   opening=f.client.checkConnectionOnOpen();
   const late=[...f.timers.values()].map(timer=>timer.action);
   const off=f.client.subscribe(state=>{
    if(!active||once||state.connectionTest?.pending!==false)return;once=true;
    if(replacement==='new-run')Object.assign(f.bootstrap,{endpoint:'ws://127.0.0.1:12346/mirror',token:'OPENING-RETIRE-TEST',runId:'run-after-opening-retire'});
    f.client.bootstrapChanged();current=f.sockets.at(-1);current.readyState=1;current.emit('open');
    current.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:{...f.snapshot(3),runId:f.bootstrap.runId,provenance:{kind:'ymcc-native',pid:222}}});
   });
   active=true;old.emit('message','invalid-json');await opening;
   for(let n=0;n<40;n++)await Promise.resolve();
   assert.equal(once,true);assert(current);assert.notEqual(current,old);assert.equal(current.closed,false);
   assert.equal(f.client.snapshot().connected,true);
   assert.equal(f.client.connectionVerified(f.bootstrap.runId),true,'cancelled opening must not poison a newly verified current transport');
   for(const callback of late)callback();
   assert.equal(f.client.connectionVerified(f.bootstrap.runId),true);assert.equal(f.timers.size,0);
   assert(f.sockets.flatMap(s=>s.sent).every(packet=>packet.command==='snapshot'));
   startupDeadlineRows.push({mode:'opening-teardown-reentry',replacement,currentVerified:true,oldCompletionIgnored:true,timers:f.timers.size});
   off();
  }finally{f.client.dispose();if(opening)await opening;assert.equal(f.timers.size,0);assert(f.sockets.every(s=>s.closed));}
 }
}

async function verifyHiddenOpeningTeardownReentry(){
 for(const replacement of ['same-run','new-run','async-open']){
  const f=fixture();let current=null,notifications=0,stop;
  try{
   f.client.deps.connectionCheckCompleted=()=>{notifications++;};
   f.client.setPassiveDemand('connection',true);
   const old=f.sockets.at(-1);old.readyState=1;old.emit('open');
   const original=old.sent.at(-1)?.probeId;assert(original);
   const late=[...f.timers.values()].map(timer=>timer.action);
   stop=f.client.subscribe(state=>{
    if(current||state.connected||state.connectionTest?.pending!==false)return;
    // Bootstrap notification can run within disconnect; its reply is later.
    if(replacement==='new-run')Object.assign(f.bootstrap,{endpoint:'ws://127.0.0.1:12346/mirror',token:'HIDDEN-OPENING-RETIRE',runId:'run-after-hidden-retire'});
    f.client.bootstrapChanged();current=f.sockets.at(-1);
    if(replacement!=='async-open'){current.readyState=1;current.emit('open');}
   });
   old.emit('message','invalid-json');assert(current);assert.notEqual(current,old);
   await new Promise(resolve=>setImmediate(resolve));
   if(replacement==='async-open'){current.readyState=1;current.emit('open');}
   const wire=current.sent.filter(packet=>packet.command==='snapshot'&&packet.subscribe!==false);
   const nonce=wire[0]?.probeId;
   const row={mode:'hidden-opening-teardown-reentry',replacement,reads:wire.length,correlated:typeof nonce==='string',
    pausedBeforeReply:current.sent.some(packet=>packet.subscribe===false)};
   startupDeadlineRows.push(row);
   assert.equal(wire.length,1);
   assert.equal(row.correlated,true,'Hidden replacement needs its own correlated proof before the retired owner finishes');
   assert.notEqual(nonce,original);assert.equal(row.pausedBeforeReply,false);
   const run=f.bootstrap.runId;
   for(const callback of late)callback();
   assert.equal(f.client.connectionChecking(run),true);assert.equal(notifications,0);
   current.emit('message',{type:'snapshot',runId:run,probeId:nonce,snapshot:{...f.snapshot(3),runId:run,provenance:{kind:'ymcc-native',pid:321}}});
   await new Promise(resolve=>setImmediate(resolve));
   assert.equal(f.client.connectionVerified(run),true);assert.equal(notifications,1);
   assert.equal(current.sent.filter(packet=>packet.subscribe===false).length,1);
   assert.equal(f.client.snapshot().snapshot,null);assert.equal(f.sockets.length,2);assert.equal(f.timers.size,0);
   assert(f.sockets.flatMap(socket=>socket.sent).every(packet=>packet.command==='snapshot'));
   Object.assign(row,{verified:true,notifications,timers:f.timers.size});
  }finally{stop?.();f.client.dispose();assert.equal(f.timers.size,0);assert(f.sockets.every(socket=>socket.closed));}
 }
}

async function verifySnapshotDeliveryRefusal(){
 const {load}=await import('./fixtures/decky_game_memory_store.mjs');
 const {ReadOnlyMirrorRelay}=load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':contextModule.exports});
 const flush=()=>new Promise(resolve=>setImmediate(resolve));
 for(const mode of ['ordinary','probe','persistent','hidden','source','peer','ambiguous']){
  const f=fixture(),run='b'.repeat(32),peer={connection:'1',runId:run,connected:true};f.bootstrap.runId=run;
  const timers=new Map(),sent=[];let timerId=0,reads=0,generation=1,refusals=mode==='probe'?0:mode==='persistent'?2:1,probe;
  f.client.setVisible(true);const socket=f.sockets.at(-1);socket.readyState=1;
  const relay=new ReadOnlyMirrorRelay({read:async()=>{
   ++reads;const {runId,revision,...view}=f.snapshot();return {...view,generation,provenance:{kind:'ymcc-native',pid:123}};
  },observe:()=>()=>{},schedule:(action,delay)=>{const id=++timerId;timers.set(id,{action,delay});return id;},cancel:id=>timers.delete(id),
   reply:async(connection,message)=>{
    sent.push({connection,...structuredClone(message)});
    if(refusals>0){--refusals;if(mode==='ambiguous')throw new Error('Unknown IPC result');return false;}
    if(connection===peer.connection)socket.emit('message',message);return true;
   }});
  const rawSend=socket.send.bind(socket);socket.send=raw=>{rawSend(raw);void relay.request({...peer,request:JSON.parse(raw)});};
  try{
   relay.connected(peer);socket.emit('open');await flush();
   if(mode==='probe'){assert(f.client.snapshot().snapshot?.ready);refusals=1;probe=f.client.testConnection();void probe.catch(()=>{});await flush();}
   const initialSent=sent.length,initialReads=reads,initialRequests=socket.sent.length;
   const row={mode:'delivery-refusal-'+mode,attemptsBefore:initialSent,readsBefore:initialReads,retryTimers:timers.size,originalDeadlines:f.timers.size};
   openingReadMeasurements.push(row);
   if(mode==='ambiguous'){assert.equal(timers.size,0,'Unknown IPC outcome is not a definite refusal');continue;}
   assert.equal(timers.size,1,'A definite refusal of the current snapshot must arm one local retry');
   const [id,retry]=timers.entries().next().value;assert.equal(retry.delay,250);
   const initial=sent.at(-1);
   if(mode==='hidden'){
    f.client.setPassiveDemand('hud',true);f.client.setVisible(false);await flush();
    assert.equal(socket.closed,false);assert.equal(socket.sent.at(-1).subscribe,false,'HUD retains the peer while the sidebar pauses');
   }
   if(mode==='source'){++generation;relay.invalidate('power');await flush();}
   if(mode==='peer')relay.connected({...peer,connection:'2',connectionRevision:2});
   const retired=['hidden','source','peer'].includes(mode);
   if(retired)assert.equal(timers.size,0,'The old delivery lease must be canceled at the ownership edge');
   const beforeCallback=sent.length;timers.delete(id);retry.action();await flush();
   assert.equal(timers.size,0,'One rejection may not grow into a polling loop');
   assert.equal(sent.length-beforeCallback,retired?0:1,'Dispatched old callback must not send');
   if(mode==='source')assert.equal(f.client.snapshot().snapshot.generation,2);
   if(!retired){
    assert.equal(reads,initialReads,'Reuse the resolved current snapshot');
    assert.equal(socket.sent.length,initialRequests,'Do not request another client snapshot');
    assert.deepEqual(sent.at(-1),initial,'Original revision, peer and probe nonce stay intact');
    if(mode==='persistent')assert.equal(f.timers.size,row.originalDeadlines,'Repeated refusal leaves the original client deadline intact');
    else{assert(f.client.snapshot().snapshot?.ready);assert.equal(f.timers.size,0);}
    if(probe)assert.equal((await probe).pid,123);
    assert.equal(f.sockets.length,1);assert.equal(socket.closed,false);
   }
   assert(socket.sent.every(message=>message.command==='snapshot'));
   Object.assign(row,{resends:sent.length-beforeCallback,readsAfter:reads,commands:socket.sent.length,settingsCommands:0,sockets:f.sockets.length});
  }finally{f.client.dispose();relay.dispose();if(probe)await probe.catch(()=>{});await flush();assert.equal(timers.size,0);assert.equal(f.timers.size,0);}
 }
}

async function verifyClientDiagnostic(){
 // A source notification can run after the read settles but before its reply
 // is sent. The active nonce must wait for the replacement owner's state.
 {
  const {load}=await import('./fixtures/decky_game_memory_store.mjs');
  const {ReadOnlyMirrorRelay}=load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':contextModule.exports});
  const f=fixture(),run='b'.repeat(32),peer={connection:'1',runId:run,connected:true};f.bootstrap.runId=run;
  f.client.setVisible(true);const socket=f.sockets.at(-1);socket.readyState=1;
  let reads=0,hold=false,inject=false,outcome='pending',result,release;
  const blocked=new Promise(resolve=>{release=resolve;}),outbound=[],pending=[];
  const relay=new ReadOnlyMirrorRelay({read:async()=>{
   const number=++reads;if(hold&&number===3)await blocked;
   const {runId,revision,...view}=f.snapshot();return {...view,generation:number===3?2:1,provenance:{kind:'ymcc-native',pid:123}};
  },observe:()=>()=>{},schedule:f.client.deps.schedule,cancel:f.client.deps.cancel,
   reply:async(_key,message)=>{outbound.push(structuredClone(message));socket.emit('message',message);return true;}});
  relay.connected(peer);
  const send=socket.send.bind(socket);socket.send=raw=>{send(raw);pending.push(relay.request({...peer,request:JSON.parse(raw)}));};
  const snapshot=relay.snapshot.bind(relay);
  relay.snapshot=()=>{const read=snapshot();if(inject){inject=false;void read.then(()=>relay.invalidate('power'));}return read;};
  const flush=()=>new Promise(resolve=>setImmediate(resolve));
  try{
   socket.emit('open');await Promise.all(pending);assert.equal(reads,1);
   hold=inject=true;
   result=f.client.testConnection().then(value=>{outcome='resolved';return value;},error=>{outcome='rejected';return error;});
   const timer=f.client.probe.timer,nonce=f.client.probe.id;await flush();assert.equal(reads,3);
   const reply=outbound.find(message=>message.probeId===nonce);
   const row={mode:'read-invalidated-before-reply',outcomeAtBoundary:outcome,readyAtBoundary:reply?.snapshot.ready,
    proofAtBoundary:reply?.snapshot.provenance?.kind,timerRetained:f.timers.has(timer)};openingReadMeasurements.push(row);
   assert.equal(row.outcomeAtBoundary,'pending','A settled read from the retired source must not authenticate the current probe');
   assert.equal(row.readyAtBoundary,false);assert.equal(row.proofAtBoundary,'unconfirmed');assert.equal(row.timerRetained,true);
   release();await Promise.all(pending);await flush();
   assert.equal((await result).pid,123);assert.equal(f.client.snapshot().snapshot.generation,2);
   assert.equal(reads,3,'Use the already triggered replacement read');assert.equal(f.sockets.length,1);
   assert.equal(f.timers.size,0);assert.equal(socket.sent.length,2);assert(socket.sent.every(p=>p.command==='snapshot'));
   Object.assign(row,{reads,remainingTimers:f.timers.size,settingsCommands:0,settledGeneration:f.client.snapshot().snapshot.generation});
  }finally{release();f.client.dispose();relay.dispose();if(result)await result;await Promise.all(pending);await flush();assert.equal(f.timers.size,0);}
 }
 // A temporary view derived from a previous healthy cache is not a fresh
 // native reply. Join the actual relay and client to check the wire contract.
 {
  const {load}=await import('./fixtures/decky_game_memory_store.mjs');
  const {ReadOnlyMirrorRelay}=load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':contextModule.exports});
  const f=fixture(),run='a'.repeat(32),peer={connection:'1',runId:run,connected:true};f.bootstrap.runId=run;
  f.client.setVisible(true);const socket=f.sockets.at(-1);socket.readyState=1;
  let churn=0,reads=0,outcome='pending',probeResult;
  const pending=[],delivered=[];
  const relay=new ReadOnlyMirrorRelay({read:async()=>{
   reads++;if(churn>0){churn--;relay.invalidate('power');}
   const {runId,revision,...view}=f.snapshot(reads);return {...view,provenance:{kind:'ymcc-native',pid:123}};
  },observe:()=>()=>{},schedule:f.client.deps.schedule,cancel:f.client.deps.cancel,
   reply:async(_connection,message)=>{delivered.push(structuredClone(message));socket.emit('message',message);return true;}});
  relay.connected(peer);const originalSend=socket.send.bind(socket);
  socket.send=raw=>{originalSend(raw);pending.push(relay.request({...peer,request:JSON.parse(raw)}));};
  const flush=async()=>{await Promise.all(pending);await new Promise(resolve=>setImmediate(resolve));};
  try{
   socket.emit('open');await flush();assert.equal(reads,1);
   assert.equal(f.client.snapshot().snapshot.provenance.kind,'ymcc-native');
   churn=3;probeResult=f.client.testConnection().then(result=>{outcome='success';return result;},()=>{outcome='rejected';return null;});
   const probe=f.client.probe;assert(probe);const timer=probe.timer;
   await flush();
   const temporary=delivered.find(message=>message.probeId===probe.id)?.snapshot;
   const row={mode:'cached-deferred-connection-proof',readsBeforeRecovery:reads,temporaryReady:temporary?.ready,
    temporaryProvenance:temporary?.provenance?.kind??'absent',outcomeBeforeRecovery:outcome,
    sameProbe:f.client.probe===probe&&f.client.probe.timer===timer,wireRequests:socket.sent.length};
   openingReadMeasurements.push(row);
   assert(temporary);assert.equal(temporary.ready,false);assert.equal(reads,4);
   assert.notEqual(temporary.provenance?.kind,'ymcc-native','Deferred view must not borrow proof from the previous healthy cache');
   assert.equal(outcome,'pending');assert.equal(row.sameProbe,true);assert.equal(row.wireRequests,2);
   assert.deepEqual([...f.timers.values()].map(value=>value.delay).sort((a,b)=>a-b),[250,15000]);
   f.fireDelay(250);await flush();
   assert.equal((await probeResult).pid,123);assert.equal(reads,5);assert.equal(f.timers.size,0);
   assert.equal(f.client.snapshot().snapshot.ready,true);assert.equal(socket.sent.length,2);
   assert.equal(f.sockets.length,1);assert.equal(socket.closed,false);
   assert(socket.sent.every(packet=>packet.command==='snapshot'));
   Object.assign(row,{readsAfterRecovery:reads,outcomeAfterRecovery:outcome,remainingTimers:f.timers.size});
  }finally{f.client.dispose();relay.dispose();if(probeResult)await probeResult;await flush();assert.equal(f.timers.size,0);}
 }
 for(const noise of ['previous-tag','unconfirmed-push']){
  const f=fixture(),s=f.connect();s.readyState=1;
  let pending;
  try{
   const expired=f.client.testConnection().catch(error=>error);
   const previousId=s.sent.at(-1).probeId;
   f.fireDelay(15000);assert.match((await expired).message,/15 秒/);
   let outcome='pending';
   pending=f.client.testConnection().then(result=>{outcome='success';return result;},()=>{outcome='rejected';return null;});
   const probe=f.client.probe,requests=s.sent.length;assert(probe&&probe.id!==previousId);
   s.emit('message',{type:'snapshot',runId:'run-1',probeId:probe.id,snapshot:{...f.snapshot(2),ready:false,provenance:{kind:'unconfirmed'}}});
   assert.equal(probe.awaitingNative,true);
   const stale=noise==='previous-tag';
   s.emit('message',{type:'snapshot',runId:'run-1',...(stale?{probeId:previousId}:{}),snapshot:{...f.snapshot(3),provenance:stale?{kind:'ymcc-native',pid:111}:{kind:'unconfirmed'}}});
   await Promise.resolve();
   const row={mode:'deferred-probe-reply-correlation',noise,outcomeAfterNoise:outcome,sameTimer:f.client.probe===probe&&f.timers.has(probe.timer)};
   openingReadMeasurements.push(row);
   assert.equal(outcome,'pending','Unrelated snapshot must not finish a deferred probe: '+noise);
   assert.equal(row.sameTimer,true);
   s.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...f.snapshot(4),provenance:{kind:'ymcc-native',pid:222}}});
   const result=await pending;assert.equal(result.pid,222);assert.equal(result.revision,4);
   assert.equal(f.timers.size,0);assert.equal(s.sent.length,requests);assert.equal(f.sockets.length,1);
   assert(s.sent.every(packet=>packet.command==='snapshot'));
   Object.assign(row,{resolvedPid:result.pid,requests,remainingTimers:f.timers.size});
  }finally{f.client.dispose();if(pending)await pending;assert.equal(f.timers.size,0);}
 }
 await verifyOpeningTeardownReentry();
 await verifySnapshotObserverReentry();
 for(const mode of ['legacy-unready','ready-unconfirmed'])for(const variant of ['native-arrives','silent','disposed']){
  const deferred=fixture();let tick=100;deferred.client.deps.now=()=>tick;
  const ready=mode==='ready-unconfirmed';
  const unconfirmed=revision=>({...deferred.snapshot(revision),ready,...(ready?{provenance:{kind:'unconfirmed'}}:{generation:0})});
  let opening;
  try{
   const s=deferred.connect();s.readyState=1;deferred.client.setPassiveDemand('connection',true);deferred.client.setVisible(false);
   opening=deferred.client.checkConnectionOnOpen();const probe=deferred.client.probe;assert(probe);const timer=probe.timer,scheduled=deferred.timers.get(timer),beforeRequests=s.sent.length;
   s.emit('message',{type:'snapshot',runId:'run-1',probeId:probe.id,snapshot:unconfirmed(2)});
   for(let n=0;n<40;n++)await Promise.resolve();
   const row={mode:'opening-owner-proof',variant,ready,checking:deferred.client.connectionChecking('run-1'),afterUnconfirmedPackets:s.sent.length-beforeRequests};
   openingReadMeasurements.push(row);
   assert.equal(row.checking,true,'A correlated '+mode+' reply must keep the existing probe pending');
   assert.equal(deferred.client.connectionVerified('run-1'),false,'Deferred view cannot fabricate native connection proof');
   assert.equal(deferred.client.probe,probe);assert.equal(deferred.client.probe.timer,timer);assert.equal(s.sent.length,beforeRequests,'Deferred response does not trigger another snapshot or hidden unsubscribe');
   assert.equal(scheduled.delay,15000);
   if(variant==='native-arrives'){
    for(const [revision,probeId] of [[1,undefined],[3,'probe:older:1']]){
     s.emit('message',{type:'snapshot',runId:'run-1',probeId,snapshot:{...deferred.snapshot(revision),provenance:{kind:'ymcc-native',pid:123}}});
     assert.equal(deferred.client.probe,probe,'Stale revisions and different nonces cannot finish the opening');
     assert.equal(deferred.client.connectionVerified('run-1'),false);
    }
    tick=2600;s.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...deferred.snapshot(4),provenance:{kind:'ymcc-native',pid:123}}});await opening;
    assert.equal(deferred.client.connectionVerified('run-1'),true);assert.equal(deferred.client.connectionChecking('run-1'),false);assert.equal(deferred.timers.size,0);
    assert.deepEqual(s.sent.slice(beforeRequests).map(p=>[p.command,p.subscribe]),[['snapshot',false]],'Recovery only releases its own hidden read lease');
   }else if(variant==='silent'){
    tick=15099;assert.equal(deferred.client.connectionChecking('run-1'),true);
    s.emit('message',{type:'snapshot',runId:'run-1',probeId:probe.id,snapshot:unconfirmed(3)});
    assert.equal(deferred.client.probe.timer,timer,'Repeated deferred replies never renew the original deadline');
    tick=15100;assert.equal(deferred.client.connectionChecking('run-1'),false);assert.equal(deferred.client.connectionReady('run-1'),false);
    deferred.timers.delete(timer);scheduled.action();await opening;assert.equal(deferred.client.connectionVerified('run-1'),false);
   }else{
    deferred.client.dispose();await opening;scheduled.action();assert.equal(deferred.timers.size,0);assert.equal(deferred.client.connectionVerified('run-1'),false);
   }
   assert.equal(deferred.sockets.length,1);assert(s.sent.every(p=>p.command==='snapshot'));
   Object.assign(row,{verified:deferred.client.connectionVerified('run-1'),timers:deferred.timers.size,sockets:deferred.sockets.length,settingsCommands:0});
  }finally{deferred.client.dispose();if(opening)await opening;assert.equal(deferred.timers.size,0);}
 }

 for(const owner of ['opening','direct-probe']){
  const f=fixture(),receipts=[],notifications=[];
  f.client.deps.nativeSnapshotObserved=facts=>receipts.push(facts.ready);
  f.client.deps.connectionCheckCompleted=()=>notifications.push(f.client.connectionVerified('run-1'));
  try{
   const s=f.connect();s.readyState=1;
   const expired=f.client.checkConnectionOnOpen(),oldNonce=f.client.probe.id;
   const lateDeadline=f.timers.get(f.client.probe.timer).action;
   f.fireDelay(15000);await expired;
   assert.deepEqual(notifications,[false]);
   const send=(revision,ready,probeId)=>s.emit('message',{type:'snapshot',runId:'run-1',probeId,snapshot:{...f.snapshot(revision),ready,provenance:{kind:'ymcc-native',pid:123}}});
   send(2,false);send(3,false,oldNonce);
   assert.equal(f.client.connectionVerified('run-1'),false,'Late snapshots cannot certify an expired opening');
   const lateNotifications=notifications.slice(1);
   const next=owner==='opening'?f.client.checkConnectionOnOpen():f.client.testConnection().catch(error=>error);
   const probe=f.client.probe;assert(probe&&probe.id!==oldNonce);
   send(4,false,oldNonce);
   assert.equal(f.client.probe,probe,'Previous nonce cannot finish the current probe');
   assert.equal(f.client.connectionVerified('run-1'),false);
   const requests=s.sent.length;
   send(5,false,probe.id);
   const outcome=await next;
   if(owner==='opening')assert.equal(outcome,true);else assert.match(outcome.message,/尚未就绪/);
   const row={mode:'not-ready-proof-after-opening-timeout',owner,verified:f.client.connectionVerified('run-1'),businessReady:f.client.snapshot().snapshot?.ready,receipts:[...receipts],notifications:[...notifications],lateNotifications,timers:f.timers.size};
   openingReadMeasurements.push(row);
   assert.equal(row.verified,true,'A new correlated native reply retires the old failure even when controls still wait');
   assert.deepEqual(row.receipts,[false],'Take the browser receipt at the actual native reply, not at a later push');
   assert.deepEqual(lateNotifications,[],'Uncorrelated pushes cannot announce recovery');
   assert.deepEqual(notifications,[false,true],'Only one verified recovery notification');
   assert.equal(row.businessReady,false);
   await assert.rejects(f.client.mutate('game.setField',{field:'acMode',value:'balanced'}),/当前不可操作/);
   lateDeadline();send(6,false,probe.id);send(7,true);
   assert.equal(f.client.connectionVerified('run-1'),true);
   assert.deepEqual(receipts,[false,true]);assert.deepEqual(notifications,[false,true]);
   assert.equal(f.timers.size,0);assert.equal(f.sockets.length,1);assert.equal(s.closed,false);
   assert.equal(s.sent.length,requests);assert(s.sent.every(p=>p.command==='snapshot'));
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 const f=fixture();
 try{
  assert.equal(typeof f.client.connectionDiagnostic,'function','finite client explanation is available');
  assert.equal(f.client.connectionDiagnostic('run-1'),'binding-run-missing');
  const socket=f.connect();socket.readyState=1;
  assert.equal(f.client.connectionDiagnostic(undefined),'native-run-missing');
  assert.equal(f.client.connectionDiagnostic('run-2'),'run-mismatch');
  for(const [state,reason] of [[0,'socket-connecting'],[1,'connected'],[2,'socket-closing'],[3,'socket-closed'],[99,'socket-unavailable']]){socket.readyState=state;assert.equal(f.client.connectionDiagnostic('run-1'),reason);}
  socket.readyState=1;
  const ts=require('typescript'),entry=fs.readFileSync(path.join(root,'decky-plugin/src/index.tsx'),'utf8'),tree=ts.createSourceFile('index.tsx',entry,ts.ScriptTarget.Latest,true,ts.ScriptKind.TSX);
  const factory=tree.statements.find(node=>ts.isFunctionDeclaration(node)&&node.name?.text==='createYmccPlugin'&&node.modifiers?.some(m=>m.kind===ts.SyntaxKind.ExportKeyword)&&node.modifiers?.some(m=>m.kind===ts.SyntaxKind.DefaultKeyword));
  const declarations=(factory?.body?.statements??tree.statements).filter(ts.isVariableStatement).flatMap(n=>n.declarationList.declarations);
  const receipt=declarations.find(n=>n.name.getText(tree)==='frontendReceipt');assert(receipt);
  const exposure=vm.runInNewContext('({receipt:('+receipt.initializer.getText(tree)+'),retire:()=>{dismounted=true}})',{client:f.client,window:f.nativeWindow,dismounted:false,directConsoleInstalled:false,placement:{active:true},__YMCC_DECKY_BUILD_ID__:'a'.repeat(64)});
  assert.equal(exposure.receipt.connectionReady,true);assert.equal(exposure.receipt.connectionState,'connected');
  assert.equal(f.client.connectionVerified('run-1'),false,'Bare TCP and an uncorrelated snapshot cannot suppress UI recovery');
  let proofNotifications=0;f.client.deps.connectionCheckCompleted=()=>{proofNotifications++;};
  const proof=f.client.checkConnectionOnOpen(),nonce=f.client.probe.id;
  socket.emit('message',{type:'snapshot',runId:'run-1',probeId:'wrong',snapshot:{...f.snapshot(2),provenance:{kind:'ymcc-native',pid:123}}});
  assert.equal(f.client.connectionVerified('run-1'),false,'An uncorrelated native packet is not current opening proof');
  socket.emit('message',{type:'snapshot',runId:'run-1',probeId:nonce,snapshot:{...f.snapshot(3),provenance:{kind:'ymcc-native',pid:123}}});await proof;
  assert.equal(proofNotifications,1,'A completed opening retires cached failure facts exactly once');
  assert.equal(f.client.connectionVerified('run-1'),true);assert.equal(exposure.receipt.connectionVerified,true);
  assert.equal(f.client.connectionVerified('run-2'),false);
  const clientDeclaration=declarations.find(n=>n.name.getText(tree)==='client'),options=clientDeclaration.initializer.arguments[0];
  const completion=options.properties.find(n=>n.name?.getText(tree)==='connectionCheckCompleted');assert(completion,'Actual plugin constructor wires the existing result event');
  const delivered=[],eventWindow={dispatchEvent:e=>{delivered.push(e.type);}},eventScope={window:eventWindow,dismounted:false,Event:class{constructor(type){this.type=type;}}};
  const completed=vm.runInNewContext('('+completion.initializer.getText(tree)+')',eventScope);
  const qamDeclaration=declarations.find(n=>n.name.getText(tree)==='qamConnectionListener');
  const qam=vm.runInNewContext('('+qamDeclaration.initializer.getText(tree)+')',{...eventScope,client:{checkConnectionOnOpen:async()=>{completed();return true;}}});
  qam();for(let n=0;n<20;n++)await Promise.resolve();assert.deepEqual(delivered,['ymcc-decky-connection-result'],'One actual QAM completion emits one existing result event');
  f.client.setPassiveDemand('connection',true);f.client.setVisible(false);
  assert.equal(f.client.connectionVerified('run-1'),true,'Hiding a healthy sidebar does not discard same-transport native proof');
  f.client.setVisible(true);
  socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...f.snapshot(4),ready:false,provenance:{kind:'ymcc-native',pid:123}}});
  assert.equal(f.client.connectionVerified('run-1'),true,'Business not ready does not sever a confirmed native connection');
  assert.equal(f.client.snapshot().snapshot.ready,false,'Business controls remain unavailable until their own native ready receipt');
  socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...f.snapshot(5),provenance:{kind:'ymcc-native',pid:123}}});
  assert.equal(f.client.connectionVerified('run-1'),true,'Same-run native readiness resumes without another open/probe');
  socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...f.snapshot(6),ready:false,provenance:{kind:'ymcc-native',pid:123}}});
  const sent=structuredClone(socket.sent),timers=f.timers.size;let clones=0;const snapshot=f.client.snapshot.bind(f.client);f.client.snapshot=()=>{clones++;return snapshot();};
  for(let i=0;i<1000;i++){assert.equal(f.client.connectionDiagnostic('run-1'),'connected');assert.equal(exposure.receipt.connectionState,'connected');assert.equal(exposure.receipt.connectionVerified,true);}
  assert.equal(clones,0);assert.deepEqual(socket.sent,sent);assert.equal(f.timers.size,timers);f.client.snapshot=snapshot;
  f.client.setPassiveDemand('connection',true);f.client.setVisible(false);
  const check=f.client.checkConnectionOnOpen(),timer=f.client.probe.timer,deadline=f.timers.get(timer);assert(deadline);f.timers.delete(timer);deadline.action();await check;
  assert.equal(f.client.connectionDiagnostic('run-1'),'opening-reply-missing');
  f.client.dispose();assert.equal(f.client.connectionDiagnostic('run-1'),'disposed');assert.equal(f.timers.size,0);
  exposure.retire();assert.equal(exposure.receipt.connectionState,'module-dismounted');assert.equal(exposure.receipt.connectionReady,false);
 }finally{f.client.dispose();}
 const lifetime=fixture();let passiveNotices=0;lifetime.client.deps.connectionCheckCompleted=()=>{passiveNotices++;};try{
  const s=lifetime.connect();s.readyState=1;lifetime.client.setPassiveDemand('connection',true);
  s.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...lifetime.snapshot(2),provenance:{kind:'ymcc-native',pid:123}}});
  assert.equal(lifetime.client.connectionVerified('run-1'),true,'Current authenticated native push proves usable controls without an extra read');
  assert.equal(passiveNotices,1,'The first hidden/ordinary native reply updates the existing mode observer');
  s.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...lifetime.snapshot(3),ready:false,provenance:{kind:'ymcc-native',pid:123}}});
  assert.equal(lifetime.client.connectionVerified('run-1'),true);assert.equal(lifetime.client.snapshot().snapshot.ready,false);assert.equal(passiveNotices,1);
  s.close();assert.equal(lifetime.client.connectionVerified('run-1'),false,'A socket close revokes the old native proof');
  lifetime.fireDelay(250);const next=lifetime.sockets.at(-1);next.readyState=1;next.emit('open');
  assert.equal(lifetime.client.connectionVerified('run-1'),false,'A new TCP socket cannot inherit old usable-sidebar proof');
  next.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...lifetime.snapshot(3),provenance:{kind:'ymcc-native',pid:123}}});
  assert.equal(lifetime.client.connectionVerified('run-1'),true,'Fresh same-run native state restores only the new transport');
  assert.equal(passiveNotices,2);lifetime.client.deps.connectionCheckCompleted=()=>{throw Error('display observer');};
 }finally{lifetime.client.dispose();assert.equal(lifetime.timers.size,0);}
 const waiting=fixture();try{waiting.client.setVisible(true);const s=waiting.sockets.at(-1);s.readyState=1;s.emit('open');assert.equal(waiting.client.connectionDiagnostic('run-1'),'transport-unconfirmed');waiting.client.setVisible(false);assert.equal(waiting.client.connectionDiagnostic('run-1'),'socket-missing');}finally{waiting.client.dispose();}
 const receipts=[];let now=100;
 const observed=fixture({now:()=>now});
 observed.client.deps.nativeSnapshotObserved=facts=>{receipts.push(structuredClone(facts));if(facts.ready)throw Error('diagnostic sink failure');};
 try{
  const s=observed.connect();s.readyState=1;
  observed.client.setPassiveDemand('connection',true);
  const opening=observed.client.checkConnectionOnOpen(),nonce=observed.client.probe.id;
  const send=(socket,revision,ready,probeId)=>socket.emit('message',{type:'snapshot',runId:'run-1',probeId,snapshot:{...observed.snapshot(revision),ready,provenance:{kind:'ymcc-native',pid:123}}});
  now=150;send(s,2,true,'wrong-probe');
  assert.deepEqual(receipts,[],'unrelated receipt while checking cannot certify the first native snapshot');
  now=200;send(s,3,false,nonce);await opening;
  assert.equal(observed.client.connectionVerified('run-1'),true);
  assert.deepEqual(receipts,[{generation:1,ready:false,connectedAgeMs:100}],'log current verified native receipt even before controls are ready');
  const sentBefore=s.sent.length,timersBefore=observed.timers.size;
  now=320;send(s,4,true);
  for(let n=5;n<=68;n++)send(s,n,n%2===0);
  assert.deepEqual(receipts,[{generation:1,ready:false,connectedAgeMs:100},{generation:1,ready:true,connectedAgeMs:220}],'at most one log per level on a live transport');
  assert.equal(s.sent.length,sentBefore,'diagnostics send no requests');assert.equal(observed.timers.size,timersBefore,'diagnostics arm no timer');
  s.close();observed.fireDelay(250);const next=observed.sockets.at(-1);next.readyState=1;next.emit('open');
  send(s,69,true);assert.equal(receipts.length,2,'old socket cannot write a receipt');
  now=420;send(next,2,true);
  assert.equal(receipts.length,3,'first ready proof on the replacement socket logs once');
  assert.deepEqual(receipts.at(-1),{generation:1,ready:true,connectedAgeMs:100});
  assert.equal(observed.client.connectionVerified('run-1'),true,'a broken diagnostic sink cannot change verification');
 }finally{observed.client.dispose();assert.equal(observed.timers.size,0);}
 const nativeReceipt=clientDeclaration.initializer.arguments[0].properties.find(node=>node.name?.getText(entryTree)==='nativeSnapshotObserved');
 assert(nativeReceipt&&ts.isPropertyAssignment(nativeReceipt),'actual plugin entry wires native receipt logging');
 const info=[];const wire=vm.runInNewContext('('+nativeReceipt.initializer.getText(entryTree)+')',{window:{console:{info:text=>info.push(text)}}});
 wire({generation:2,ready:true,connectedAgeMs:345});
 assert.deepEqual(info,['[YMCC:DeckyClient] {"schema":1,"stage":"mirror-native-snapshot","generation":2,"ready":true,"connectedAgeMs":345}']);
}

async function verifyOpeningDeadlineObservation(){
 const f=fixture();let now=100;f.client.deps.now=()=>now;
 try{
  const socket=f.connect();socket.readyState=1;f.client.setPassiveDemand('connection',true);f.client.setVisible(false);
  const opening=f.client.checkConnectionOnOpen();assert(f.client.probe);const timer=f.client.probe.timer;
  const deadline=f.timers.get(timer);assert.equal(deadline.delay,15000);
  const sent=structuredClone(socket.sent),timers=f.timers.size;
  now=15099;assert.equal(f.client.connectionChecking('run-1'),true);assert.equal(f.client.connectionReady('run-1'),true);
  now=15100;
  const observed={elapsedMs:now-100,checking:f.client.connectionChecking('run-1'),ready:f.client.connectionReady('run-1'),diagnostic:f.client.connectionDiagnostic('run-1'),timeoutCallbackExecuted:false};openingDeadlineObservations.push(observed);
  assert.equal(observed.checking,false,'Existing 15-second opening deadline must end pending observation even when its timeout callback has not run');
  assert.equal(observed.ready,false,'Expired unacknowledged opening cannot report a healthy current browser client');
  assert.equal(observed.diagnostic,'opening-reply-missing');
  now+=1800000;
  for(let i=0;i<1000;i++){assert.equal(f.client.connectionChecking('run-1'),false);assert.equal(f.client.connectionReady('run-1'),false);assert.equal(f.client.connectionDiagnostic('run-1'),'opening-reply-missing');}
  assert.deepEqual(socket.sent,sent);assert.equal(f.timers.size,timers);assert(f.timers.has(timer));
  // A later current correlated read can establish fresh connection, without
  // replaying a business command or adding a replacement timer.
  const probeId=f.client.probe.id;const snapshot={...f.snapshot(2),provenance:{kind:'ymcc-native',pid:1234}};
  socket.emit('message',{type:'snapshot',runId:'run-1',probeId,snapshot});assert.equal(await opening,true);
  assert.equal(f.client.connectionReady('run-1'),true);assert.equal(f.client.connectionChecking('run-1'),false);
  assert.equal(socket.sent.filter(x=>x.command!=='snapshot').length,0);
  f.client.setPassiveDemand('connection',false);assert.equal(f.timers.size,0);
 }finally{f.client.dispose();}
 const slow=fixture();let tick=100;slow.client.deps.now=()=>tick;
 try{
  const socket=slow.connect();socket.readyState=1;const setVisible=slow.client.setVisible.bind(slow.client);
  slow.client.setVisible=(value,owner)=>{setVisible(value,owner);if(value)tick+=10000;};
  const result=slow.client.checkConnectionOnOpen();const started=slow.client.probe.started,timer=slow.client.probe.timer;assert.equal(started,10100);
  tick=started+14999;assert.equal(slow.client.connectionChecking('run-1'),true);assert.equal(slow.client.connectionReady('run-1'),true);
  tick=started+15000;assert.equal(slow.client.connectionChecking('run-1'),false);assert.equal(slow.client.connectionReady('run-1'),false);
  openingDeadlineObservations.push({kind:'slow-visibility-setup',setupMs:10000,actualProbeStarted:started,fullOriginalReplyBoundMs:15000,checkingAt14999:true,checkingAt15000:false});
  const deadline=slow.timers.get(timer);slow.timers.delete(timer);deadline.action();await result;
 }finally{slow.client.dispose();}
 const ack=fixture();let time=100;ack.client.deps.now=()=>time;
 try{const socket=ack.connect();socket.readyState=1;const checking=ack.client.checkConnectionOnOpen();const probeId=ack.client.probe.id;socket.emit('message',{type:'snapshot',runId:'run-1',probeId,snapshot:{...ack.snapshot(2),ready:false,provenance:{kind:'ymcc-native',pid:1234}}});time=20000;assert.equal(ack.client.connectionReady('run-1'),true,'An already correlated native reply retains original transport proof even before the promise finally runs');await checking;assert.equal(ack.client.connectionChecking('run-1'),false);}finally{ack.client.dispose();}
}
async function verifyCancelledProbeSetupOwnership(){
 for(const replacement of ['same-run','new-run']){
  const f=fixture();let once=false,current=null,currentOpening=null,currentRequest=null,active=false;
  try{
   const old=f.connect();old.readyState=1;f.client.setPassiveDemand('connection',true);
   f.client.setVisible(false);const before=old.sent.length;
   const off=f.client.subscribe(state=>{
    if(!active||once||state.connectionTest?.pending!==true)return;
    once=true;
    Object.assign(f.bootstrap,{endpoint:'ws://127.0.0.1:12346/mirror',token:'PROBE-OWNER-REENTRY-TEST',...(replacement==='new-run'?{runId:'run-after-probe-start'}:{})});
    f.client.bootstrapChanged();current=f.sockets.at(-1);current.readyState=1;current.emit('open');
    currentOpening=f.client.checkConnectionOnOpen();
    currentRequest=current.sent.filter(packet=>typeof packet.probeId==='string').at(-1);assert(currentRequest);
   });
   active=true;const retiredOpening=f.client.checkConnectionOnOpen();
   await retiredOpening;
   assert.equal(once,true);assert(current);assert.notEqual(current,old);
   const probeRequests=current.sent.filter(packet=>typeof packet.probeId==='string');
   assert.equal(probeRequests.length,1,'cancelled probe setup must not issue a replacement read');
   assert.equal(f.client.probe.id,currentRequest.probeId);assert.equal(f.client.connectionChecking(f.bootstrap.runId),true);
   assert.equal(old.sent.length,before,'Retired opening never submits a probe on its old socket');
   current.emit('message',{type:'snapshot',runId:f.bootstrap.runId,probeId:currentRequest.probeId,snapshot:{...f.snapshot(3),runId:f.bootstrap.runId,provenance:{kind:'ymcc-native',pid:222}}});
   await currentOpening;
   assert.equal(f.client.connectionChecking(f.bootstrap.runId),false);assert.equal(f.client.connectionVerified(f.bootstrap.runId),true);
   assert.equal(f.timers.size,0);assert.equal(current.closed,false);
   assert(f.sockets.flatMap(socket=>socket.sent).every(packet=>packet.command==='snapshot'));
   startupDeadlineRows.push({mode:'cancelled-probe-setup',replacement,currentProbeRequests:probeRequests.length,oldProbeRequests:0,currentVerified:true,timers:f.timers.size});off();
  }finally{f.client.dispose();if(currentOpening)await currentOpening;assert.equal(f.timers.size,0);assert(f.sockets.every(socket=>socket.closed));}
 }
 {
  const f=fixture();let active=false,once=false;
  try{
   f.bootstrap.hudProtocol=1;f.client.setPassiveVisible(true);const socket=f.connect();socket.readyState=1;const before=socket.sent.length;
   const off=f.client.subscribe(state=>{if(active&&!once&&state.connectionTest?.pending){once=true;f.client.setVisible(false);}});
   active=true;const cancelled=f.client.testConnection().then(value=>value,error=>error);
   assert.equal(typeof (await cancelled).message,'string');
   const messages=socket.sent.slice(before);assert.equal(messages.filter(packet=>packet.command==='snapshot'&&packet.subscribe!==false).length,0,'Hidden cancelled probe cannot resume the business snapshot stream');
   assert.equal(messages.filter(packet=>packet.subscribe===false).length,1);assert.equal(socket.closed,false);assert.equal(f.timers.size,0);
   startupDeadlineRows.push({mode:'cancelled-probe-setup',replacement:'hidden-passive',currentProbeRequests:0,oldProbeRequests:0,passiveConnected:true,timers:f.timers.size});off();
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
}

async function verifySnapshotSendFailureReentry(){
 for(const replacement of ['same-run','new-run']){
  const f=fixture();let active=false,once=false,current=null,newIntent=null,secondWrite=null,oldProbe=null;
  try{
   const old=f.connect();old.readyState=1;
   const late=[...f.timers.values()].map(timer=>timer.action);
   const off=f.client.subscribe(state=>{
    if(!active||once||state.connectionTest?.pending!==false)return;
    once=true;
    Object.assign(f.bootstrap,{endpoint:'ws://127.0.0.1:12346/mirror',token:'SNAPSHOT-SEND-REENTRY-TEST',...(replacement==='new-run'?{runId:'run-after-send-failure'}:{})});
    f.client.bootstrapChanged();current=f.sockets.at(-1);current.readyState=1;current.emit('open');
    current.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:{...f.snapshot(3),runId:f.bootstrap.runId,provenance:{kind:'ymcc-native',pid:222}}});
    newIntent=f.client.mutate('fan.setEnabled',{enabled:true}).then(value=>value,error=>error);
    secondWrite=current.sent.findLast(packet=>packet.command==='fan.setEnabled');assert(secondWrite);
   });
   old.send=()=>{throw Error('Inert failed snapshot send');};active=true;
   oldProbe=f.client.testConnection().then(value=>value,error=>error);
   for(let n=0;n<40;n++)await Promise.resolve();
   assert.equal(once,true);assert(current);assert.notEqual(current,old);
   assert.equal(current.closed,false,'failed snapshot send must not close observer replacement');
   assert.equal(f.client.snapshot().connected,true);assert.equal(f.client.snapshot().busy,true);
   assert.equal(f.client.pending.id,secondWrite.id);assert.equal(old.closed,true);
   assert.equal(typeof (await oldProbe).message,'string');
   for(const callback of late)callback();
   old.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(99999)});
   assert.equal(f.client.pending.id,secondWrite.id);assert.equal(current.closed,false);
   current.emit('message',{type:'reply',runId:f.bootstrap.runId,id:secondWrite.id,ok:true,result:{applied:true}});
   assert.equal((await newIntent).applied,true);assert.equal(f.timers.size,0);
   assert.equal(f.sockets.flatMap(socket=>socket.sent).filter(packet=>packet.command==='fan.setEnabled').length,1,'only the new explicit intent is sent');
   startupDeadlineRows.push({mode:'snapshot-send-observer-reentry',replacement,newExplicitIntentApplied:true,oldIntentReplayed:false,timers:f.timers.size});off();
  }finally{f.client.dispose();if(oldProbe)await oldProbe;if(newIntent)await newIntent;assert.equal(f.timers.size,0);assert(f.sockets.every(socket=>socket.closed));}
 }
}

async function verifyDisconnectDiagnostics(){
 const plain=value=>JSON.parse(JSON.stringify(value));
 const make=()=>{const f=fixture(),facts=[];f.client.deps.transportDisconnected=row=>facts.push(plain(row));return {f,facts};};
 const row=(name,value)=>disconnectObservations.push({name,...plain(value)});
 const source=(facts,wanted)=>{assert.equal(facts.length,1,'local disconnect cause missing or duplicated');assert.equal(facts[0].source,wanted);assert.deepEqual(Object.keys(facts[0]).sort(),['connected','operationPending','probePending','readyState','readyStateKnown','snapshotPending','source'].sort());};
 {
  const {f,facts}=make();try{const socket=f.connect();socket.readyState=1;const sent=plain(socket.sent),timers=f.timers.size;
   for(let n=0;n<1000;n++)assert.equal(f.client.connectionDiagnostic('run-1'),'connected');
   assert.equal(facts.length,0,'healthy reads cannot log or acquire demand');assert.deepEqual(socket.sent,sent);assert.equal(f.timers.size,timers);
   f.client.setVisible(false);source(facts,'demand-ended');assert.equal(socket.closed,true);assert.equal(f.timers.size,0);row('healthy-quiet-and-hide',{facts,healthyReads:1000});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 for(const kind of ['bootstrap-revoked','bootstrap-changed','plugin-disposed','socket-closed','socket-error','run-changed']){
  const {f,facts}=make();try{const socket=f.connect();socket.readyState=kind==='socket-closed'?3:1;
   if(kind==='bootstrap-revoked'){f.nativeWindow.__YMCC_DECKY_MIRROR__={revoked:true};f.client.bootstrapChanged();}
   if(kind==='bootstrap-changed'){f.nativeWindow.__YMCC_DECKY_MIRROR__={...f.bootstrap,runId:'run-2',token:'new-test-token'};f.client.bootstrapChanged();}
   if(kind==='plugin-disposed')f.client.dispose();
   if(kind==='socket-closed')socket.emit('close');
   if(kind==='socket-error')socket.emit('error');
   if(kind==='run-changed')socket.emit('message',{type:'snapshot',runId:'other-native',snapshot:f.snapshot()});
   source(facts,kind);assert.equal(facts[0].connected,true);assert.equal(facts[0].readyStateKnown,true);
   const before=facts.length,timers=f.timers.size,peers=f.sockets.length;for(let n=0;n<1000;n++){socket.emit('close');socket.emit('error');}
   assert.equal(facts.length,before,'late old socket events cannot log into replacement');assert.equal(f.timers.size,timers);assert.equal(f.sockets.length,peers);
   row(kind,{facts,timersAfterOriginalAction:timers,lateEdges:2000});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 {
  const {f,facts}=make();try{f.client.setVisible(true);const socket=f.sockets.at(-1);socket.readyState=0;f.fireDelay(10000);
   source(facts,'connect-deadline');assert.equal(facts[0].connected,false);assert.equal(f.timers.size,1);assert.equal([...f.timers.values()][0].delay,250);row('connect-deadline',{facts});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 {
  const {f,facts}=make();try{const socket=f.connect();socket.readyState=1;const checking=f.client.checkConnectionOnOpen();
   const timer=f.client.snapshotDeadline?.timer,pending=f.timers.get(timer);assert(pending);f.timers.delete(timer);pending.action();await checking;
   source(facts,'snapshot-deadline');assert.equal(socket.closeCode,4001,'only snapshot deadline keeps original close4001');assert.equal(facts[0].probePending,true);assert.equal(facts[0].snapshotPending,false,'original deadline already consumed before teardown');
   assert.equal(f.timers.size,1);row('snapshot-deadline',{facts,closeCode:socket.closeCode});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 for(const kind of ['wallpaper-deadline','passive-send-failed','operation-send-failed','snapshot-send-failed']){
  const {f,facts}=make();try{f.bootstrap.wallpaperProtocol=1;const socket=f.connect();socket.readyState=1;f.client.setPassiveDemand('wallpaper',true);
   if(kind==='wallpaper-deadline'){assert.equal(f.client.sendWallpaper('wallpaper.context',{visible:true,contextSeq:1,appId:'480'}),true);f.fireDelay(10000);}
   else{
    socket.send=()=>{throw Error('credential-sentinel-must-never-be-logged');};
    if(kind==='passive-send-failed')assert.equal(f.client.sendWallpaper('wallpaper.status',{}),false);
    if(kind==='operation-send-failed')await assert.rejects(f.client.mutate('fan.setPreset',{preset:'soft'}),/连接已结束，操作结果需重新确认/);
    if(kind==='snapshot-send-failed')await f.client.checkConnectionOnOpen();
   }
   source(facts,kind==='wallpaper-deadline'?kind:'send-failed');assert.equal(socket.closeCode,kind==='wallpaper-deadline'?4002:undefined);row(kind,{facts,closeCode:socket.closeCode});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
  // WebSocket send() can discard data once closing begins, before close delivery.
 // Record every entry point on the same baseline instead of stopping at the first drop.
 const closedSendRows=[];
  for(const readyState of [3,2])for(const mode of ['snapshot','snapshot-during-observation','wallpaper','pause','mutation','mutation-during-busy']){
  const {f,facts}=make();let stop,write,writeSettled;
  try{
   f.bootstrap.wallpaperProtocol=1;
   const socket=f.connect();socket.readyState=1;f.client.setPassiveDemand('wallpaper',true);
   if(mode.startsWith('snapshot'))f.client.setVisible(false);
   let closedSendCalls=0,delayedCloseCalls=0;
   const send=socket.send.bind(socket);
    socket.send=value=>{if(socket.readyState===readyState){closedSendCalls++;return;}send(value);};
   socket.close=()=>{delayedCloseCalls++;};
    if(mode==='mutation-during-busy')stop=f.client.subscribe(state=>{if(state.busy)socket.readyState=readyState;});
    else if(mode==='snapshot-during-observation')f.client.deps.steam=()=>{socket.readyState=readyState;return {availability:'unavailable'};};
    else socket.readyState=readyState;
   let passiveAccepted;
   if(mode.startsWith('snapshot'))f.client.setVisible(true);
   else if(mode==='wallpaper')passiveAccepted=f.client.sendWallpaper('wallpaper.context',{visible:true,contextSeq:1,appId:'480'});
   else if(mode==='pause')f.client.setVisible(false);
   else write=f.client.mutate('fan.setPreset',{preset:'soft'}).then(()=>{writeSettled='resolved';},error=>{writeSettled=error.message;});
   await Promise.resolve();
   const timers=[...f.timers.values()].map(t=>t.delay),connected=f.client.snapshot().connected,busy=f.client.snapshot().busy;
   const retry=timers.length===1&&timers[0]===250;
    const observation={mode,readyState,closedSendCalls,delayedCloseCalls,timers,connected,busy,passiveAccepted,writeSettled,facts:plain(facts)};
    const label=readyState===2?'closing':'closed';
    closedSendRows.push(observation);row(label+'-send-'+mode,observation);
   if(!retry)continue;
    source(facts,'socket-'+label);assert.equal(facts[0].readyState,readyState);
   for(let n=0;n<24;n++)assert.equal(f.client.sendWallpaper('wallpaper.context',{visible:true,contextSeq:n+2,appId:'480'}),false);
   assert.deepEqual([...f.timers.values()].map(t=>t.delay),[250]);
   f.fireDelay(250);const next=f.sockets.at(-1);assert.equal(f.sockets.length,2);
   next.readyState=1;next.emit('open');
   socket.emit('close');socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(99)});
   assert.equal(f.client.snapshot().snapshot,null);
   next.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(2)});
   assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
   assert(next.sent.every(value=>value.command==='snapshot'),'neither wallpaper messages nor settings are replayed');
   if(mode.startsWith('mutation')){
    const explicit=f.client.mutate('fan.setPreset',{preset:'soft'}),request=next.sent.at(-1);
    assert.equal(request.command,'fan.setPreset');next.emit('message',{type:'reply',runId:'run-1',id:request.id,ok:true});await explicit;
   }
    row(label+'-send-recovered-'+mode,{newSocketReads:next.sent.filter(value=>value.command==='snapshot').length,newExplicitWrites:next.sent.filter(value=>value.command==='fan.setPreset').length,remainingTimers:f.timers.size});
  }finally{stop?.();f.client.dispose();if(write)await write;assert.equal(f.timers.size,0);}
 }
 for(const result of closedSendRows){
   assert.equal(result.closedSendCalls,0,`${result.mode}: must not send to a socket with readyState=${result.readyState}`);
  assert.equal(result.delayedCloseCalls,1);assert.equal(result.connected,false);assert.equal(result.busy,false);assert.deepEqual(result.timers,[250]);
  if(result.mode==='wallpaper')assert.equal(result.passiveAccepted,false);
  if(result.mode.startsWith('mutation'))assert.match(result.writeSettled??'',/本次操作未发送/);
 }
 for(const invalid of ['{','[]','x'.repeat(65537),{type:'snapshot',runId:'run-1',snapshot:{}}]){
  const {f,facts}=make();try{const socket=f.connect();socket.readyState=1;socket.emit('message',invalid);source(facts,'message-invalid');row('message-invalid-'+disconnectObservations.length,{facts});}
  finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 {
  const {f,facts}=make();try{const socket=f.connect();socket.readyState=1;const pending=f.client.mutate('fan.setPreset',{preset:'soft'});const rejected=assert.rejects(pending,/连接中断/);
   socket.emit('close');await rejected;source(facts,'socket-closed');assert.equal(facts[0].operationPending,true);
   const commands=socket.sent.filter(value=>value.command==='fan.setPreset');assert.equal(commands.length,1);f.fireDelay(250);const next=f.sockets.at(-1);next.readyState=1;next.emit('open');next.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(2)});
   assert.equal(next.sent.filter(value=>value.command==='fan.setPreset').length,0,'diagnostics cannot replay uncertain business');
   const explicit=f.client.mutate('fan.setPreset',{preset:'soft'}),request=next.sent.at(-1);next.emit('message',{type:'reply',runId:'run-1',id:request.id,ok:true});await explicit;
   row('uncertain-operation-and-explicit-next-intent',{facts:plain(facts),oldCommands:commands.length,newExplicitCommands:1});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 for(const mode of ['missing','throwing','working']){
  const f=fixture(),facts=[];if(mode!=='missing')f.client.deps.transportDisconnected=value=>{facts.push(plain(value));if(mode==='throwing')throw Error('diagnostic-only');};
  try{const socket=f.connect();socket.readyState=1;socket.emit('error');assert.equal(socket.closed,true);assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,1);assert.equal([...f.timers.values()][0].delay,250);
   row('optional-sink-'+mode,{facts,originalRetryMs:250});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 {
  const {f,facts}=make();let secretReads=0;try{const socket=f.connect();
   Object.defineProperty(socket,'url',{get(){secretReads++;throw Error('secret URL');}});Object.defineProperty(socket,'readyState',{get(){throw Error('broken optional read');}});
   socket.emit('close');source(facts,'socket-closed');assert.equal(facts[0].readyStateKnown,false);assert.equal(secretReads,0);assert.equal(JSON.stringify(facts).includes('secret'),false);row('broken-scalar-and-private-url',{facts,secretReads});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 {
  const {f,facts}=make();try{
   for(let n=0;n<128;n++){
    if(n===0)f.connect();else{f.client.bootstrapChanged();const socket=f.sockets.at(-1);socket.emit('open');socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(n+1)});}
    const socket=f.sockets.at(-1);socket.readyState=1;f.client.disconnect('fixture-private-text',undefined,undefined,'socket-closed');
   }
   assert.equal(facts.length,1,'same fixed signature cannot spam log after reconnect');row('repeated-disconnect-dedup',{records:facts.length,disconnects:128});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 {
  const {f,facts}=make();try{
   const sources=['socket-closed','socket-error','connect-deadline','snapshot-deadline','wallpaper-deadline','bootstrap-revoked','bootstrap-changed','demand-ended','plugin-disposed','send-failed','run-changed','message-invalid'];
   for(let n=0;n<48;n++){
    if(n===0)f.connect();else{f.client.bootstrapChanged();const socket=f.sockets.at(-1);socket.emit('open');socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(n+1)});}
    f.sockets.at(-1).readyState=Math.floor(n/sources.length);f.client.disconnect('fixture-private-text',undefined,undefined,sources[n%sources.length]);
   }
   assert.equal(facts.length,16,'one client owns at most16 distinct scalar signatures');assert.equal(f.client.disconnectFacts.size,16);row('client-signature-cap',{records:facts.length,attempts:48});
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 {
  const options=clientDeclaration.initializer.arguments[0],callback=options.properties.find(node=>node.name?.getText(entryTree)==='transportDisconnected');assert(callback,'Actual plugin entry must wire bounded disconnect facts');
  const delivered=[],scope={window:{console:{info:value=>delivered.push(value)}}};const log=vm.runInNewContext('('+callback.initializer.getText(entryTree)+')',scope);
  const facts={source:'socket-closed',readyStateKnown:true,readyState:3,connected:true,operationPending:false,probePending:false,snapshotPending:false};log(facts);
  assert.deepEqual(delivered,['[YMCC:DeckyClient] '+JSON.stringify({schema:1,stage:'mirror-disconnect',...facts})]);scope.window.console.info=()=>{throw Error('console unavailable');};assert.doesNotThrow(()=>log(facts));row('actual-entry-console',{records:1,consoleThrowIsolated:true});
 }
}


// Original restart-case variant: the opening can land between socket OPEN and
// its first ordinary snapshot. Correlation, readiness and ownership stay real.

// Opt-in parameter of the original restart case. Real entry and MirrorClient;
// browser sockets, feature installers, React and host events are memory ports.
// Extra parameter of the existing restart consumer. Whole official class is
// evaluated unchanged; browser import/OS endpoints are explicit VM ports.
async function verifyOfficialImportLifetime(noReplay=false){
 const plain=value=>JSON.parse(JSON.stringify(value));
 const flush=async()=>{for(let n=0;n<120;n++)await Promise.resolve();await new Promise(r=>setImmediate(r));for(let n=0;n<30;n++)await Promise.resolve();};
 const officialFile=process.env.YMCC_DECKY_OFFICIAL_LOADER_SOURCE,nativeFile=process.env.YMCC_DECKY_NATIVE_UPDATE_SOURCE;
 assert(officialFile&&path.isAbsolute(officialFile)&&nativeFile&&path.isAbsolute(nativeFile),'explicit pinned official and native source required');
 const upstream=fs.readFileSync(officialFile,'utf8'),nativeSource=fs.readFileSync(nativeFile,'utf8');
 const sockets=[],timers=new Map(),events=new Map(),clients=[],features=new Set(),backendListeners=new Map(),imports=[],trace=[];let now=0,nextTimer=0,nextModule=0;
 const schedule=(fn,delay)=>{timers.set(++nextTimer,{fn,delay,due:now+delay});return nextTimer;},cancel=id=>timers.delete(id);
 const advance=async ms=>{const target=now+ms;for(;;){const item=[...timers].filter(([,t])=>t.due<=target).sort((a,b)=>a[1].due-b[1].due)[0];if(!item)break;now=item[1].due;timers.delete(item[0]);item[1].fn();await flush();}now=target;await flush();};
 class Clock extends Date{constructor(...args){super(...(args.length?args:[now]));}static now(){return now;}}
 class Socket{listeners=new Map();readyState=0;closed=false;sent=[];constructor(url){this.url=url;sockets.push(this);}addEventListener(k,v){const s=this.listeners.get(k)||new Set();s.add(v);this.listeners.set(k,s);}emit(k,v){for(const fn of [...(this.listeners.get(k)||[])])fn(k==='message'?{data:JSON.stringify(v)}:{});}send(v){this.sent.push(JSON.parse(v));}close(){if(this.closed)return;this.closed=true;this.readyState=3;this.emit('close');}}
 class OwnedClient extends MirrorClient{constructor(deps){super(deps);clients.push(this);}}
 const host={__YMCC_DECKY_MIRROR__:{endpoint:'ws://127.0.0.1:12345/mirror',token:'TEST-OFFICIAL-IMPORT',runId:'run-1'},console:{info(){},warn(){}},setTimeout:schedule,clearTimeout:cancel,
  addEventListener(k,v){const s=events.get(k)||new Set();s.add(v);events.set(k,s);},removeEventListener(k,v){events.get(k)?.delete(v);},dispatchEvent(event){for(const fn of [...(events.get(event.type)||[])])fn(event);return true;}};
 const ownFeature=stage=>{const owner={stage,module:nextModule};features.add(owner);return()=>features.delete(owner);};
 const pendingNever=()=>new Promise(()=>{}),react={createElement:(type,props,...children)=>({type,props:props??{},children}),lazy:()=>()=>null};
 const controls={PanelSection:'section',PanelSectionRow:'row',SliderField:'slider',DropdownItem:'dropdown',ToggleField:'toggle',ButtonItem:'button',Router:{}};
 const entryImports=new Set(['../../src/bridge/deckyInputOptions','../../src/bridge/losslessProgramState','../../src/bridge/frameRateModel','./AnchoredDropdown','./fanChoiceDebounce','./performanceIntentQueue','./performanceModel','./PerformanceControls','./GameProfileControls','./steamRunningContext']);
 function evaluateEntry(build){
  const ordinal=++nextModule,runId=host.__YMCC_DECKY_MIRROR__.runId,module={exports:{}},scope={module,exports:module.exports,SP_REACT:react,DFL:controls,Event,structuredClone,WebSocket:Socket,__YMCC_DECKY_BUILD_ID__:build,crypto:{randomUUID:()=> 'official-entry-'+ordinal},window:host,Date:Clock,setTimeout:schedule,clearTimeout:cancel,require:name=>{
   if(name==='./mirrorClient')return{MirrorClient:OwnedClient};if(name==='@decky/api')return{useQuickAccessVisible:()=>false,routerHook:{}};
   if(name==='./steamHudProbe')return{installSteamHudProbe:()=>{throw Error('default must not install probe');}};
   if(name==='./steamMonitorHud')return{installSteamMonitorHud:()=>{const s=sockets.at(-1);assert(s);s.readyState=1;s.emit('open');const p=s.sent.filter(p=>p.command==='snapshot'&&p.subscribe!==false).at(-1);s.emit('message',{type:'snapshot',runId,...(p?.probeId?{probeId:p.probeId}:{}),snapshot:{runId,generation:1,revision:ordinal,ready:true,game:null,provenance:{kind:'ymcc-native',pid:123},fan:{supported:false,enabled:false,preset:'off',choices:[]}}});return ownFeature('monitor');}};
   if(name==='./steamWallpaper')return{installSteamWallpaper:()=>ownFeature('wallpaper')};if(name==='./deckyLoaderUpdate')return{installLoaderUpdateRow:()=>ownFeature('updater')};if(name==='./qamPlacement')return{installConsoleTab:()=>({active:true,dispose:ownFeature('placement')})};if(entryImports.has(name))return{};throw Error('unmocked entry '+name);
  }};
  vm.runInNewContext(transformSync(entryText,{loader:'tsx',format:'cjs',jsxFactory:'SP_REACT.createElement'}).code,scope);
  const result={ordinal,factoryCalls:0,exports:{default:()=>{result.factoryCalls++;return module.exports.default();}},rescue:()=>module.exports.default().onDismount()};return result;
 }
 const lifecycleCalls=[];
 class Logger{log(...x){lifecycleCalls.push(['log',...x.map(String)]);}warn(){}error(...x){lifecycleCalls.push(['error',...x.map(String)]);}debug(){}}
 class Boundary{deinit(){lifecycleCalls.push(['boundary-deinit']);}}
 class Tabs{tabs=[];init(){lifecycleCalls.push(['tabs-init']);}add(x){this.tabs.push(x);}deinit(){this.tabs=[];lifecycleCalls.push(['tabs-deinit']);}}
 class Router{routes=new Map();addRoute(k,v){this.routes.set(k,v);}removeRoute(k){this.routes.delete(k);}deinit(){lifecycleCalls.push(['router-deinit']);}waitForUnlock(){return Promise.resolve();}}
 class Toaster{toast(){return{dismiss(){}};}deinit(){lifecycleCalls.push(['toaster-deinit']);}}
 class State{value={plugins:[],disabledPlugins:[],versionInfo:null,activePlugin:null};publicState(){return this.value;}setPlugins(v){this.value.plugins=v;}setDisabledPlugins(v){this.value.disabledPlugins=v;}setVersionInfo(v){this.value.versionInfo=v;}}
 class Service{init(){}shouldNotify(){return false;}}
 const backend={ws:{readyState:1},addEventListener(k,v){const s=backendListeners.get(k)||new Set();s.add(v);backendListeners.set(k,s);},callable:name=>(...args)=>backend.call(name,...args),async call(name){if(name==='utilities/get_user_info')return{username:'fixture',path:'fixture'};if(name==='loader/get_plugins')return[{name:'ymcc-sidebar',version:'0.0.1',load_type:1,disabled:false}];throw Error('unexpected backend '+name);}};
 const ui={EUIMode:{GamePad:4},QuickAccessTab:{Decky:999},findSP:()=>({}),sleep:pendingNever,PanelSection:'section',PanelSectionRow:'row',Navigation:{},quickAccessMenuClasses:{},ModalRoot:'modal',showModal(){}};
 const defaultClass=c=>c;
 const requireOfficial=name=>{
  if(name==='@decky/api')return{};if(name==='@decky/ui')return ui;if(name==='react')return react;if(name==='react-icons/fa')return{FaPlug:'plug',FaExclamationCircle:'error'};
  if(name==='./logger')return defaultClass(Logger);if(name==='./errorboundary-hook')return defaultClass(Boundary);if(name==='./tabs-hook')return defaultClass(Tabs);if(name==='./router-hook')return defaultClass(Router);if(name==='./toaster')return defaultClass(Toaster);
  if(name==='./components/DeckyState')return{DeckyState:State,DeckyStateContextProvider:'provider',useDeckyState:()=>({})};
  if(name==='./frozen-plugins-service')return{FrozenPluginService:Service};if(name==='./hidden-plugins-service')return{HiddenPluginsService:Service};if(name==='./notification-service')return{NotificationService:Service};
  if(name==='./plugin')return{PluginLoadType:{ESMODULE_V1:1,LEGACY_EVAL_IIFE:0}};if(name==='./utils/pluginHelpers')return{getPluginDisplayName:(n,v)=>n+'@'+v};
  if(name==='./utils/settings')return{getSetting:async(k,d)=>d,setSetting(){}};if(name==='./updater')return{getVersionInfo:async()=>({current:'v3.3.1',supported:true})};
  if(name==='./steamfixes')return{initSteamFixes(){},deinitSteamFixes(){lifecycleCalls.push(['steamfixes-deinit']);}};
  if(name==='./components/modals/filepicker/patches')return{initFilepickerPatches(){},deinitFilepickerPatches(){lifecycleCalls.push(['filepicker-deinit']);}};
  if(name==='./components/QuickAccessVisibleState')return{useQuickAccessVisible:()=>false};
  if(name==='./utils/TranslationHelper')return{__esModule:true,default:'translation',TranslationClass:{PLUGIN_LOADER:'loader'}};
  if(name.startsWith('./components/'))return()=>null;if(name==='./store')return{checkForPluginUpdates:async()=>new Map()};throw Error('unmocked official '+name);
 };
 let uiModeRegistrations=0;const officialModule={exports:{}},ctx=vm.createContext({module:officialModule,exports:officialModule.exports,require:requireOfficial,SP_REACT:react,window:host,DeckyBackend:backend,SteamClient:{UI:{RegisterForUIModeChanged(){uiModeRegistrations++;return{unregister(){}};}}},performance:{now:()=>now},Date:Clock,setTimeout:schedule,clearTimeout:cancel,console:{warn(){}},AbortController,Promise});
 const dynamicImport=async spec=>{
  assert(String(spec).startsWith('http://127.0.0.1:1337/plugins/ymcc-sidebar/dist/index.js?t='),'only actual original plugin import path');
  let release;const done=new Promise(resolve=>release=resolve),gate={at:now,spec,release,settled:false,module:null};imports.push(gate);await done;
  gate.module=evaluateEntry(imports.indexOf(gate)===0?'1'.repeat(64):'2'.repeat(64));gate.settled=true;
  const namespace=new vm.SyntheticModule(['default'],function(){this.setExport('default',gate.module.exports.default);},{context:ctx});await namespace.link(()=>{});await namespace.evaluate();return namespace;
 };
 const sourceCode=transformSync(upstream,{loader:'tsx',format:'cjs',target:'es2022',jsxFactory:'SP_REACT.createElement',jsxFragment:'SP_REACT.Fragment'}).code;
 vm.runInContext(sourceCode,ctx,{importModuleDynamically:dynamicImport});
 const Loader=officialModule.exports.default,loader=new Loader();host.DeckyPluginLoader=loader;host.DeckyBackend=backend;await flush();
 assert.equal(uiModeRegistrations,1,'original constructor remains waiting its real UI event port, not force-completed');
 const snapshot=()=>({now,clients:clients.length,liveSockets:sockets.filter(s=>!s.closed).length,timers:timers.size,events:[...events.values()].reduce((n,s)=>n+s.size,0),features:features.size,officialPlugins:loader.plugins.length,officialQueue:loader.pluginReloadQueue.length,reloadLock:loader.reloadLock,receiptBuild:host.__YMCC_DECKY_FRONTEND__?.buildId??null});
 try{
  const seed=loader.importPlugin('ymcc-sidebar','0.0.1',1,true,10000);await flush();assert.equal(imports.length,1);imports[0].release();await seed;await flush();assert.equal(imports[0].module.factoryCalls,1);assert.equal(snapshot().liveSockets,1);trace.push({phase:'original-load',...snapshot()});
  // Distinct real URLs are required: browser module caches coalesce one specifier.
  await advance(1);
  const expected='2'.repeat(64),value={frontendBuildId:expected,runId:'run-1'};host.__YMCC_DECKY_MIRROR__={...host.__YMCC_DECKY_MIRROR__,...value};
  const update=vm.runInNewContext(nativeSource,{window:host,Error,Promise});update(value);await flush();assert.equal(imports.length,2);assert.equal(snapshot().liveSockets,0,'official unload must retire the seed before replacement import');
  await advance(10000);assert.equal(host.__YMCC_DECKY_FRONTEND_UPDATE__.phase,'failed');assert.equal(loader.reloadLock,false);assert.equal(loader.plugins.length,0);trace.push({phase:'original-import-and-ready-deadlines',...snapshot()});
  if(noReplay){
   for(let n=0;n<32;n++)update({...value,recoverFrontend:true,loaderVersion:'v3.3.1'});
   await advance(60000);
   const exhausted=host.__YMCC_DECKY_FRONTEND_UPDATE__;
   const terminal={imports:imports.length,attempts:exhausted.attempts,phase:exhausted.phase,pending:exhausted.pending,firstLoad:!!host.__YMCC_DECKY_FRONTEND_BOOT__,...snapshot()};
   trace.push({stage:'retained-grant-exhausted',...terminal});
   // Preserve the state before the assertion also on the old retrying source.
   disconnectObservations.push({name:'retained-grant-after-deadline',...terminal});
   assert.deepEqual({imports:terminal.imports,attempts:terminal.attempts,phase:terminal.phase,pending:terminal.pending,firstLoad:terminal.firstLoad,liveSockets:terminal.liveSockets,timers:terminal.timers,events:terminal.events,features:terminal.features},
    {imports:2,attempts:1,phase:'failed',pending:false,firstLoad:false,liveSockets:0,timers:0,events:0,features:0},'Same grant must not replay a dispatched import or start a fallback');
   const grant={...value,runId:'run-2',recoverFrontend:true,loaderVersion:'v3.3.1'};
   host.__YMCC_DECKY_MIRROR__={...host.__YMCC_DECKY_MIRROR__,...grant};host.dispatchEvent(new Event('ymcc-decky-bootstrap'));
   update(grant);await advance(11999);assert.equal(imports.length,2,'a new grant uses the original first-load grace');await advance(1);
   assert.equal(imports.length,3);assert.equal(host.__YMCC_DECKY_FRONTEND_BOOT__.dispatched,true);
  }else{
   await advance(5000);assert.equal(imports.length,3);assert.equal(host.__YMCC_DECKY_FRONTEND_UPDATE__.attempts,2);
  }
  assert.equal(new Set(imports.map(g=>g.spec)).size,3,'seed and replacement must use distinct original Date.now URL identities');imports[2].release();await flush();assert.equal(imports[2].module.factoryCalls,1);assert.equal(loader.plugins.length,1);assert.equal((noReplay?host.__YMCC_DECKY_FRONTEND_BOOT__:host.__YMCC_DECKY_FRONTEND_UPDATE__).phase,'current');assert.equal(snapshot().liveSockets,1);assert.equal(snapshot().timers,0);const acceptedReceipt=host.__YMCC_DECKY_FRONTEND__,acceptedClient=clients.at(-1);trace.push({phase:noReplay?'new-grant-first-load-accepted':'bounded-second-import-accepted',...snapshot()});
  imports[1].release();await flush();assert.equal(imports[1].module.factoryCalls,0,'expired original import must not call its factory');
  const late={phase:'first-import-evaluates-late',...snapshot(),lateFactoryCalls:imports[1].module.factoryCalls,receiptRetained:host.__YMCC_DECKY_FRONTEND__===acceptedReceipt,acceptedClientConnected:acceptedClient.snapshot().connected};trace.push(late);
  loader.unloadPlugin('ymcc-sidebar');await flush();trace.push({phase:'actual-official-unload',...snapshot()});loader.deinit();await flush();trace.push({phase:'actual-official-deinit',...snapshot(),remainingRoutes:loader.routerHook.routes.size});
  const terminal=snapshot(),passed=late.liveSockets===1&&late.receiptRetained&&late.timers===0&&terminal.liveSockets===0&&terminal.timers===0&&terminal.events===0&&terminal.features===0&&terminal.receiptBuild===null&&terminal.officialPlugins===0;
  disconnectObservations.push({name:'whole-official-class-late-import',passed,policy:noReplay?'no-replay-after-dispatch':'historical-bounded-retry',trace:plain(trace),officialConstructor:true,officialImportReactImportUnloadDeinit:true,rawOfficialSourceUnmodified:true,dynamicImportIsControlledVmPort:true,actualEntryAndMirrorClient:true,rootUiModePromiseStillPending:true,officialBackendCallbacksNotClaimedRetired:true,officialFactoryCounts:imports.map(g=>g.module?.factoryCalls??null),distinctImportSpecifiers:imports.map(g=>g.spec),realSteam:false});
  assert(passed,'whole official/native retry + late original evaluation must retain only accepted owner and return it on unload');
 }finally{
  // Preserve any failure observation before fixture-only rescue. Never count
  // rescuing an unadmitted import as official or product cleanup.
  for(const gate of imports)if(gate.settled&&gate.module?.factoryCalls===0&&snapshot().liveSockets)gate.module.rescue();
  loader.unloadPlugin('ymcc-sidebar');loader.deinit();await flush();
  assert.equal(sockets.filter(s=>!s.closed).length,0);assert.equal(timers.size,0);assert.equal(features.size,0);
 }
}

async function verifyPluginDisposalSources(activationOnly=false){
 const plain=value=>JSON.parse(JSON.stringify(value));
 const flush=async()=>{for(let n=0;n<80;n++)await Promise.resolve();};
 for(const source of (activationOnly?[]:['plugin-disposed','plugin-pagehide','plugin-initialization-failed']))for(const sink of ['working','throwing','missing']){
  const f=fixture(),facts=[];if(sink!=='missing')f.client.deps.transportDisconnected=value=>{facts.push(plain(value));if(sink==='throwing')throw Error('optional-dispose-log');};
  let pending,probe;let unsubscribe;
  try{
   const socket=f.connect();socket.readyState=1;f.client.setPassiveDemand('connection',true);
   unsubscribe=f.client.subscribe(()=>{});f.client.setIntentPending({},true);
   if(sink==='working'){pending=f.client.mutate('fan.setPreset',{preset:'soft'});pending=assert.rejects(pending,/连接已结束/);}
   if(sink==='throwing')probe=f.client.checkConnectionOnOpen();
   f.client.dispose(source);if(pending)await pending;if(probe)await probe;await flush();
   if(sink!=='missing'){assert.equal(facts.length,1,'one diagnostic per original transport');assert.equal(facts[0].source,source,'plugin disposal must retain its explicit entry source');}
   assert(socket.closed);assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,0);
   for(const owner of ['listeners','connectionListeners','extraListeners','visibleOwners','intentOwners','passiveConsumers','bootstrapWaiters'])assert.equal(f.client[owner].size,0,'dispose owner returned: '+owner);
   const count=f.sockets.length;for(let n=0;n<100;n++){socket.emit('close');socket.emit('error');}f.client.dispose();
   assert.equal(f.sockets.length,count);assert.equal(f.timers.size,0);if(sink!=='missing')assert.equal(facts.length,1);
   disconnectObservations.push({name:'dispose-source-'+source+'-'+sink,source,sink,facts,transportClosed:true,timers:0,lateEdges:200,settingsWrites:sink==='working'?1:0,uncertainWriteNotReplayed:true});
  }finally{unsubscribe?.();f.client.dispose();if(pending)await pending;if(probe)await probe;assert.equal(f.timers.size,0);}
 }
 for(const route of ['placement-rejected','receipt-rejected','loader','pagehide','initialization-rejected'])for(const sinkThrow of [false,true])for(const cleanupThrow of route==='initialization-rejected'?[false]:[false,true]){
  const sockets=[],timers=new Map(),events=new Map(),clients=[],logs=[],released=[];let timerId=0,placementRejected=route==='placement-rejected';
  const native={endpoint:'ws://127.0.0.1:12345/mirror',token:'TEST-ENTRY-ONLY',runId:'run-1'};
  class Socket {listeners=new Map();sent=[];closed=false;readyState=0;constructor(url){this.url=url;sockets.push(this);}addEventListener(k,v){this.listeners.set(k,v);}send(v){this.sent.push(JSON.parse(v));}close(){this.closed=true;this.readyState=3;this.listeners.get('close')?.();}emit(k,v){this.listeners.get(k)?.({data:JSON.stringify(v)});}}
  class OwnedClient extends MirrorClient {constructor(deps){super(deps);clients.push(this);}}
  const host={__YMCC_DECKY_MIRROR__:native,console:{info:value=>{logs.push(value);if(sinkThrow)throw Error('optional-console-unavailable');},warn:()=>{}},
   addEventListener:(k,v)=>events.set(k,v),removeEventListener:(k,v)=>{if(events.get(k)===v)events.delete(k);},dispatchEvent:event=>{events.get(event.type)?.(event);return true;}};
  const previousReceipt=route==='receipt-rejected'?{buildId:'other-owner'}:undefined;
  if(previousReceipt)Object.defineProperty(host,'__YMCC_DECKY_FRONTEND__',{value:previousReceipt});
  const module={exports:{}},react={createElement:(type,props,...children)=>({type,props:props??{},children})};
  const release=stage=>()=>{released.push(stage);if(cleanupThrow&&stage==='wallpaper')throw Error('old-surface-cleanup-unavailable');};
  const imports=new Set(['../../src/bridge/deckyInputOptions','../../src/bridge/losslessProgramState','../../src/bridge/frameRateModel','./AnchoredDropdown','./fanChoiceDebounce','./performanceIntentQueue','./performanceModel','./PerformanceControls','./GameProfileControls','./steamRunningContext']);
  const scope={module,exports:module.exports,SP_REACT:react,DFL:{PanelSection:'section',PanelSectionRow:'row',SliderField:'slider',DropdownItem:'dropdown',ToggleField:'toggle',ButtonItem:'button',Router:{}},Event,structuredClone,WebSocket:Socket,__YMCC_DECKY_BUILD_ID__:'entry-disposal-fixture',crypto:{randomUUID:()=> 'entry-fixture'},window:host,
   setTimeout:(action,delay)=>{timers.set(++timerId,{action,delay});return timerId;},clearTimeout:id=>timers.delete(id),require:name=>{
    if(name==='./mirrorClient')return {MirrorClient:OwnedClient};
    if(name==='@decky/api')return {useQuickAccessVisible:()=>false,routerHook:{}};
    if(name==='./steamHudProbe')return {installSteamHudProbe:()=>{throw Error('default entry cannot install probe');}};
    if(name==='./steamMonitorHud')return {installSteamMonitorHud:()=>{const s=sockets.at(-1);assert(s);s.readyState=1;s.emit('open');s.emit('message',{type:'snapshot',runId:'run-1',snapshot:{runId:'run-1',generation:1,revision:1,ready:true,game:null,fan:{supported:false,enabled:false,preset:'off',choices:[]}}});return release('monitor-hud');}};
    if(name==='./steamWallpaper')return {installSteamWallpaper:()=>{if(route==='initialization-rejected')throw Error('fixture-initialization-rejected');return release('wallpaper');}};
    if(name==='./deckyLoaderUpdate')return {installLoaderUpdateRow:()=>release('loader-update')};
    if(name==='./qamPlacement')return {installConsoleTab:()=>{if(placementRejected)throw Error('fixture-placement-rejected');return {active:true,dispose:release('placement')};}};
    if(imports.has(name))return {};throw Error('unexpected entry dependency '+name);
   }};
  const run=()=>{
   vm.runInNewContext(transformSync(entryText,{loader:'tsx',format:'cjs',jsxFactory:'SP_REACT.createElement'}).code,scope);
   if(activationOnly){
     const before={clients:clients.length,sockets:sockets.length,timers:timers.size,listeners:events.size,frontendReceipt:host.__YMCC_DECKY_FRONTEND__!==previousReceipt,features:released.length};
    const quiet=before.clients===0&&before.sockets===0&&before.timers===0&&before.listeners===0&&!before.frontendReceipt;
    if(!quiet){
     // The caller abandoned import and never constructed this plugin. Rescue
     // only after recording its leaked ownership, never as product recovery.
     module.exports.default().onDismount();
     disconnectObservations.push({name:'actual-entry-before-factory',route,sinkThrow,cleanupThrow,before,manualFixtureRescueAfterObservation:true,after:{timers:timers.size,eventListeners:events.size,liveSockets:sockets.filter(s=>!s.closed).length},realSteam:false});
    }else disconnectObservations.push({name:'actual-entry-before-factory',route,sinkThrow,cleanupThrow,before,manualFixtureRescueAfterObservation:false,realSteam:false});
    assert.deepEqual(before,{clients:0,sockets:0,timers:0,listeners:0,frontendReceipt:false,features:0},'abandoned module evaluation must acquire no runtime owners before official factory');
   }
   return module.exports.default();
  };
   if(route==='initialization-rejected')assert.throws(run,/fixture-initialization-rejected/);
   else if(route==='placement-rejected')assert.throws(run,/fixture-placement-rejected/);
   else if(route==='receipt-rejected')assert.throws(run,{name:'TypeError',message:/__YMCC_DECKY_FRONTEND__/});
  else{
   const plugin=run(),oldPagehide=events.get('pagehide');assert(oldPagehide);
   if(route==='pagehide')oldPagehide(new Event('pagehide'));else plugin.onDismount();
   plugin.onDismount();oldPagehide(new Event('pagehide'));
  }
   await flush();
   const observed={clients:clients.length,connected:clients[0]?.snapshot().connected,peers:sockets.length,liveSockets:sockets.filter(s=>!s.closed).length,timers:timers.size,eventListeners:events.size,previousReceiptUnchanged:host.__YMCC_DECKY_FRONTEND__===previousReceipt};
   disconnectObservations.push({name:'actual-entry-terminal-resources',route,sinkThrow,cleanupThrow,...observed});
   assert.deepEqual(observed,{clients:1,connected:false,peers:1,liveSockets:0,timers:0,eventListeners:0,previousReceiptUnchanged:true},'Factory exit must return acquired runtime resources');
  const records=logs.filter(value=>value.startsWith('[YMCC:DeckyClient] ')).map(value=>JSON.parse(value.slice('[YMCC:DeckyClient] '.length)));
  const wanted=route==='loader'?'plugin-disposed':route==='pagehide'?'plugin-pagehide':'plugin-initialization-failed';
  assert.equal(records.length,1);assert.equal(records[0].source,wanted,'actual module exit must identify original caller');assert.equal(records[0].connected,true);
   assert.deepEqual(released,route==='initialization-rejected'?['monitor-hud']:route==='placement-rejected'?['loader-update','wallpaper','monitor-hud']:['loader-update','wallpaper','monitor-hud','placement']);
  disconnectObservations.push({name:'actual-entry-'+route,route,sinkThrow,cleanupThrow,source:wanted,records:records.length,released,peers:sockets.length,timers:timers.size,eventListeners:events.size,actualProductionEntry:true,realSteam:false});
   if(route==='placement-rejected'){
    placementRejected=false;
    const next=module.exports.default();await flush();
    assert.equal(clients.length,2);assert.equal(clients[1].snapshot().connected,true);assert.equal(sockets.length,2);
    assert.equal(sockets.filter(s=>!s.closed).length,1);assert.equal(host.__YMCC_DECKY_FRONTEND__.active,true);
    assert.equal(sockets.flatMap(s=>s.sent).filter(p=>p.command!=='snapshot').length,0,'Initialization and retry must not replay writes');
    next.onDismount();await flush();
    assert.equal(timers.size,0);assert.equal(events.size,0);assert(sockets.every(s=>s.closed));assert.equal(host.__YMCC_DECKY_FRONTEND__,undefined);
    disconnectObservations.push({name:'actual-entry-retry-after-placement-rejection',sinkThrow,cleanupThrow,singleReplacement:true,noWrites:true,liveSockets:0,timers:0,eventListeners:0});
   }
 }
}

async function verifyPassiveOpenSnapshotHandoff(){
 const run='a'.repeat(32),flush=async()=>{for(let n=0;n<30;n++)await Promise.resolve();};
 const {load}=await import('./fixtures/decky_game_memory_store.mjs');
 const {ReadOnlyMirrorRelay}=load(fs.readFileSync(path.join(root,'src/bridge/deckyMirrorRelay.ts'),'utf8'),{'../../decky-plugin/src/steamRunningContext':contextModule.exports});
 for(const mode of ['visible','visible-and-opening-probe','probe-only','retire']){
  const f=fixture({steam:()=>({availability:'unavailable'})}),owner={};f.bootstrap.runId=run;
  f.client.setPassiveDemand('wallpaper',true);const socket=f.sockets[0];socket.readyState=1;
  const tasks=[];let readCount=0,observations=0,observationDone=false,probe;
  const peer={connection:'1',runId:run,connected:true};
  const relay=new ReadOnlyMirrorRelay({read:async()=>{readCount++;const {runId,revision,...view}=f.snapshot(readCount);return {...view,provenance:{kind:'ymcc-native',pid:123}};},
   observe:()=>()=>{},observeSteam:()=>{observations++;return false;},reply:async(_id,message)=>{socket.emit('message',message);return true;},
   schedule:()=>{throw Error('Healthy open cannot schedule a relay recovery timer');},cancel:()=>{}});
  relay.connected(peer);const send=socket.send.bind(socket);
  socket.send=raw=>{send(raw);tasks.push(relay.request({...peer,request:JSON.parse(raw)}));};
  const off=f.client.subscribe(state=>{
   if(!state.connected||observationDone)return;observationDone=true;
   if(mode==='retire'){f.client.dispose();return;}
   if(mode!=='probe-only')f.client.setVisible(true,owner);
   if(mode==='visible-and-opening-probe')probe=f.client.checkConnectionOnOpen();
   if(mode==='probe-only')probe=f.client.checkConnectionOnOpen();
  },{connectionOnly:true});
  try{
   socket.emit('open');const sent=socket.sent.filter(p=>p.command==='snapshot'&&p.subscribe!==false);
   await Promise.all(tasks);await flush();
   const row={mode:'passive-open-publication-'+mode,requests:sent.length,correlated:sent.filter(p=>p.probeId).length,relayReads:readCount,steamObservations:observations,requestSources:sent.map(p=>p.requestSource)};
   openingReadMeasurements.push(row);
   assert.equal(row.requests,mode==='retire'?0:1,'Only one opening read leaves this socket after publication');
   assert.equal(row.correlated,mode.includes('probe')?1:0,'The first open request preserves an explicit pending probe nonce');
   assert.deepEqual(row.requestSources,mode==='retire'?[]:[mode.includes('probe')?'socket-open-probe':'socket-open']);
   assert.equal(row.relayReads,mode==='retire'?0:1);assert.equal(row.steamObservations,mode==='retire'?0:1);
   if(probe)assert(await probe);
   assert.equal(f.timers.size,0);assert(socket.sent.every(p=>p.command==='snapshot'));
   if(mode==='visible'){
    f.client.setVisible(false,owner);const before=socket.sent.length;
    assert.equal(socket.sent.at(-1).requestSource,'demand-stop');assert.equal(socket.sent.at(-1).subscribe,false);
    f.client.setVisible(true,owner);assert.equal(socket.sent.length,before+1,'A later fresh demand is not coalesced with the opening');
    assert.equal(socket.sent.at(-1).requestSource,'demand-start');
    row.demandSources=socket.sent.slice(-2).map(p=>p.requestSource);
    await Promise.all(tasks);await flush();assert.equal(readCount,2);assert.equal(f.timers.size,0);
   }
  }finally{off();f.client.dispose();relay.dispose();if(probe)await probe.catch(()=>{});await Promise.allSettled(tasks);assert.equal(f.timers.size,0);}
 }
}

async function verifyHudPreferencesReady(){
 const {load}=await import('./fixtures/decky_game_memory_store.mjs');
 const source=file=>fs.readFileSync(path.join(root,'src/bridge',file),'utf8');
 const model={normalizeMonitorOutput:()=>({})}; // HUD data and output owners are unused by the hello path.
 const protocol=load(source('monitorHudProtocol.ts'),{'./monitorHudModel':model});
 const {createSteamHudRelay}=load(source('steamHudRelay.ts'),{'./monitorHudProtocol':protocol,'./monitorHudModel':model,'./monitorHudWire':load(source('monitorHudWire.ts'))});
 const {ReadOnlyMirrorRelay}=load(source('deckyMirrorRelay.ts'),{'../../decky-plugin/src/steamRunningContext':contextModule.exports});
 const run='a'.repeat(32),peer={connection:'1',runId:run,connected:true};
 for(const mode of ['visible','pending','hidden']){
  const f=fixture(),tasks=[],extras=[];let reads=0,release;
  const held=new Promise(resolve=>release=resolve);
  f.bootstrap.runId=run;f.bootstrap.hudProtocol=1;f.client.setPassiveDemand('hud',true);
  const socket=f.sockets[0];socket.readyState=1;
  const mirror=new ReadOnlyMirrorRelay({read:async()=>{
    if(++reads===1&&mode==='pending')await held;
    const {runId,revision,...snapshot}=f.snapshot(reads);return {...snapshot,provenance:{kind:'ymcc-native',pid:123}};
   },observe:()=>()=>{},snapshotDemand:()=>{},reply:async(_id,message)=>{socket.emit('message',message);return true;},schedule:()=>assert.fail('No recovery timer while healthy')});
  const unexpected=()=>assert.fail('HUD hello cannot acquire a sample or timer');
  const hud=createSteamHudRelay({now:()=>0,session:()=>'',reply:async(_id,message)=>{if(message.type==='hud-ready')socket.emit('message',message);return true;},sample:unexpected,acquire:unexpected,setTimer:unexpected,clearTimer:unexpected});
  const off=f.client.subscribeExtra(message=>extras.push(structuredClone(message)));
  const send=socket.send.bind(socket);
  socket.send=raw=>{send(raw);const request=JSON.parse(raw);assert.equal(request.command,'snapshot');tasks.push(mirror.request({...peer,request}));};
  try{
   mirror.connected(peer);if(mode!=='hidden')f.client.setVisible(true);socket.emit('open');
   if(mode!=='pending')await Promise.all(tasks);
   const initial=socket.sent.length;hud.connected(peer);
   assert.equal(socket.sent.length,initial+1,'An unmarked peer hello must still restore or explicitly pause demand');
   assert.equal(socket.sent.at(-1).requestSource,'hud-peer-ready');
   assert.deepEqual(extras.at(-1),{type:'hud-ready',protocol:1,runId:run});
   if(mode==='hidden')assert.equal(socket.sent.at(-1).subscribe,false);
   if(mode!=='pending')await Promise.all(tasks);
   const requests=socket.sent.length,readsBefore=reads,timers=[...f.timers.keys()];
   hud.preferencesReady();
   assert.deepEqual(extras.at(-1),{type:'hud-ready',protocol:1,runId:run,snapshotRefresh:false},'The original HUD subscriber gets the actual preferences hello');
   openingReadMeasurements.push({mode:'hud-preferences-'+mode,addedRequests:socket.sent.length-requests,addedReads:reads-readsBefore,hudNotices:extras.length,peerRequestSource:socket.sent.at(-1).requestSource});
   assert.equal(socket.sent.length,requests,'A preferences-only hello cannot resend a sidebar snapshot or pause');
   assert.equal(reads,readsBefore);assert.deepEqual([...f.timers.keys()],timers);
   release();await Promise.all(tasks);
   assert.equal(f.client.connectionVerified(run),mode!=='hidden');assert.equal(f.timers.size,0);
  }finally{release();off();f.client.dispose();hud.dispose();mirror.dispose();await Promise.allSettled(tasks);assert.equal(f.timers.size,0);}
 }
}

async function verifyOpenBeforeFirstReply(){
 await verifyHudPreferencesReady();
 await verifyPassiveOpenSnapshotHandoff();
 const flush=async()=>{for(let n=0;n<80;n++)await Promise.resolve();};
 for(const ready of [true,false]){
  const f=fixture();let opening;
  try{
   f.client.setVisible(true);const socket=f.sockets.at(-1);socket.readyState=1;socket.emit('open');
   assert.equal(f.client.snapshot().connected,false);assert.equal(socket.sent.length,1);
   opening=f.client.checkConnectionOnOpen();const probe=f.client.probe;assert(probe);
   const requests=socket.sent.filter(p=>p.command==='snapshot'&&p.subscribe!==false),correlated=requests.filter(p=>p.probeId===probe.id);
   assert.equal(correlated.length,1,'An OPEN socket awaiting its first reply must issue this existing probe nonce');
   assert.deepEqual(requests.map(p=>p.requestSource),['socket-open','connection-probe']);
   const originalTimer=probe.timer,deadline=f.timers.get(originalTimer);assert.equal(deadline.delay,15000);
   assert.equal(await f.client.checkConnectionOnOpen(),false);assert.equal(socket.sent.length,2);assert.equal(f.client.probe.timer,originalTimer);
   socket.emit('message',{type:'snapshot',runId:'run-1',snapshot:{...f.snapshot(2),provenance:{kind:'ymcc-native',pid:123}}});
   assert.equal(f.client.connectionVerified('run-1'),false,'An earlier uncorrelated reply cannot certify the new opening');
   socket.emit('message',{type:'snapshot',runId:'run-1',probeId:probe.id,snapshot:{...f.snapshot(3),ready,provenance:{kind:'ymcc-native',pid:123}}});
   await opening;await flush();assert.equal(f.client.connectionVerified('run-1'),true);assert.equal(f.client.snapshot().snapshot.ready,ready);
   if(!ready)await assert.rejects(f.client.mutate('performance.setControls',{patch:{tdp:25}}));
   deadline.action();for(let n=0;n<100;n++)socket.emit('open');assert.equal(socket.sent.length,2);assert.equal(f.sockets.length,1);assert.equal(f.timers.size,0);
   assert(socket.sent.every(p=>p.command==='snapshot'));openingReadMeasurements.push({mode:'open-before-first-reply',ready,ordinaryRequests:1,correlatedRequests:1,settingsCommands:0,sockets:f.sockets.length,timers:f.timers.size,verified:true,requestSources:requests.map(p=>p.requestSource)});
  }finally{f.client.dispose();if(opening)await opening;assert.equal(f.timers.size,0);assert(f.sockets.every(s=>s.closed));}
 }
 const connecting=fixture();let opening;
 try{
  connecting.client.setVisible(true);const s=connecting.sockets.at(-1);s.readyState=0;opening=connecting.client.checkConnectionOnOpen();const p=connecting.client.probe;assert(p);assert.equal(s.sent.length,0);
  s.readyState=1;s.emit('open');assert.equal(s.sent.length,1);assert.equal(s.sent[0].probeId,p.id);assert.equal(s.sent[0].requestSource,'socket-open-probe');
  s.emit('message',{type:'snapshot',runId:'run-1',probeId:p.id,snapshot:{...connecting.snapshot(2),provenance:{kind:'ymcc-native',pid:123}}});await opening;
  assert.equal(connecting.client.connectionVerified('run-1'),true);assert.equal(connecting.timers.size,0);openingReadMeasurements.push({mode:'connecting-before-first-reply',correlatedRequests:1,settingsCommands:0,verified:true});
 }finally{connecting.client.dispose();if(opening)await opening;assert.equal(connecting.timers.size,0);}
 for(const state of [2,3]){
  const f=fixture();let stopped;
  try{f.client.setVisible(true);const s=f.sockets.at(-1);s.readyState=1;s.emit('open');s.readyState=state;const before=s.sent.length;stopped=f.client.checkConnectionOnOpen();assert.equal(s.sent.length,before,'Closing or closed unconfirmed sockets do not receive the new probe');f.client.dispose();await stopped;assert.equal(f.client.connectionVerified('run-1'),false);assert(s.sent.every(p=>p.command==='snapshot'));openingReadMeasurements.push({mode:'unconfirmed-socket-not-open',readyState:state,additionalRequests:0,settingsCommands:0,verified:false});}
  finally{f.client.dispose();if(stopped)await stopped;assert.equal(f.timers.size,0);}
 }
 // The platform can expose CLOSED before its queued close dispatch runs.
 // A hidden QAM check must not send on that socket or wait for its reply.
 const stale=fixture();let staleOpening;
 try{
  const old=stale.connect(),runId='run-1';old.readyState=1;
  const send=(socket,revision,probeId)=>socket.emit('message',{type:'snapshot',runId,probeId,
    snapshot:{...stale.snapshot(revision),provenance:{kind:'ymcc-native',pid:123}}});
  send(old,2);assert.equal(stale.client.connectionVerified(runId),true);
  stale.client.setPassiveDemand('connection',true);stale.client.setVisible(false);
  old.readyState=3;
  const sent=old.sent.length;staleOpening=stale.client.checkConnectionOnOpen();
  const row={mode:'closed-before-platform-callback',additionalRequests:old.sent.length-sent,
    connected:stale.client.snapshot().connected,checking:stale.client.connectionChecking(runId),
    retryDelays:[...stale.timers.values()].map(t=>t.delay),sockets:stale.sockets.length};
  openingReadMeasurements.push(row);
  assert.equal(row.additionalRequests,0,'Opened QAM does not ask a CLOSED transport for state');
  assert.equal(row.connected,false);assert.equal(row.checking,false);
  assert.deepEqual(row.retryDelays,[250],'Only the existing shared rebind lease remains');
  await staleOpening;
  for(let n=0;n<24;n++)assert.equal(await stale.client.checkConnectionOnOpen(),false);
  assert.equal(stale.sockets.length,1);assert.deepEqual([...stale.timers.values()].map(t=>t.delay),[250]);
  stale.fireDelay(250);const next=stale.sockets.at(-1);assert.notEqual(next,old);next.readyState=1;next.emit('open');
  const requests=next.sent.filter(p=>p.command==='snapshot'&&p.subscribe!==false);assert.equal(requests.length,1);assert.match(requests[0].probeId,/^probe:/);
  const probeId=requests[0].probeId;
  send(old,100,probeId);old.emit('close');assert.equal(stale.client.connectionVerified(runId),false);
  send(next,3,probeId);await flush();
  assert.equal(stale.client.connectionVerified(runId),true);assert.equal(stale.timers.size,0);
  assert(stale.sockets.flatMap(s=>s.sent).every(p=>p.command==='snapshot'));
  Object.assign(row,{recovered:true,newSnapshotRequests:requests.length,remainingTimers:stale.timers.size,settingsCommands:0});
 }finally{stale.client.dispose();if(staleOpening)await staleOpening;assert.equal(stale.timers.size,0);}
 const retired=fixture();let unsubscribe,aborted;
 try{retired.client.setVisible(true);const s=retired.sockets.at(-1);s.readyState=1;s.emit('open');const before=s.sent.length;unsubscribe=retired.client.subscribe(state=>{if(state.connectionTest?.pending)retired.client.dispose();});aborted=retired.client.checkConnectionOnOpen();await aborted;assert.equal(s.sent.length,before,'A synchronous observer dispose still retires the original probe before send');assert.equal(retired.client.connectionVerified('run-1'),false);assert.equal(retired.timers.size,0);openingReadMeasurements.push({mode:'observer-dispose-before-probe-send',additionalRequests:0,settingsCommands:0,verified:false});}
 finally{unsubscribe?.();retired.client.dispose();if(aborted)await aborted;assert.equal(retired.timers.size,0);}
}

await check('restart run ID disconnects and no mutation replay',async()=>{
 if(restartVariant==='snapshot-delivery-refusal'){await verifySnapshotDeliveryRefusal();return;}
 if(restartVariant==='client-diagnostic'){await verifyClientDiagnostic();return;}
 if(restartVariant==='official-import-lifetime'){await verifyOfficialImportLifetime();return;}
 if(restartVariant==='official-import-no-replay'){await verifyOfficialImportLifetime(true);return;}
 if(restartVariant==='entry-factory-activation'){await verifyPluginDisposalSources(true);return;}
 if(restartVariant==='plugin-disposal-sources'){await verifyPluginDisposalSources();return;}
 if(restartVariant==='open-before-first-reply'){await verifyOpenBeforeFirstReply();return;}
 if(restartVariant==='disconnect-diagnostics'){await verifyDisconnectDiagnostics();return;}
 if(restartVariant===null)await verifyDisconnectDiagnostics();
 if(restartVariant==='native-reply-proof'){await verifyCancelledProbeSetupOwnership();await verifySnapshotSendFailureReentry();await verifyClientDiagnostic();await verifyHiddenOpeningTeardownReentry();return;}
 if(restartVariant===null)await verifyHiddenOpeningTeardownReentry();
 await verifyOpeningDeadlineObservation();
 await verifyClientDiagnostic();
 if(restartVariant!=='hidden-proof-lifetimes'){await verifyVisibleReconnectAfterOpeningTimeout();if(restartVariant!==null)return;await verifyOpeningProbeReadCoalescing();}

 // Field 2026-11-11: a hidden reconnect can restore transport without ever
 // asking for the native reply needed to complete reserved recovery history.
 for(const ending of (restartVariant==='hidden-proof-lifetimes'?['timeout','snapshot-timeout','dispose']:['reply','timeout','snapshot-timeout','dispose'])){
  const startup=fixture();startup.client.setPassiveDemand('connection',true);
  const peer=startup.sockets.at(-1);peer.readyState=1;peer.emit('open');
  const requests=peer.sent.filter(p=>p.command==='snapshot'&&p.subscribe!==false);
  assert(requests.some(p=>typeof p.probeId==='string'),'hidden connection must request its current native proof');
  const count=peer.sent.length;for(let n=0;n<1000;n++)peer.emit('open');
  assert.equal(peer.sent.length,count,'one socket opening owns one bounded native proof');
  assert.equal(startup.client.connectionChecking('run-1'),true);
  if(ending==='reply'){
   const request=requests.at(-1);
   peer.emit('message',{type:'snapshot',runId:'run-1',probeId:request.probeId,snapshot:{...startup.snapshot(2),provenance:{kind:'ymcc-native',pid:123}}});
  }else if(ending==='timeout'||ending==='snapshot-timeout'){
   // Equal-duration leases belong to different owners; insertion order is not identity.
   const probeTimer=startup.client.probe?.timer,snapshotTimer=startup.client.snapshotDeadline?.timer;
   assert.notEqual(probeTimer,snapshotTimer);assert.equal(startup.timers.get(probeTimer)?.delay,15000);assert.equal(startup.timers.get(snapshotTimer)?.delay,15000);
   const chosen=ending==='timeout'?probeTimer:snapshotTimer,deadline=startup.timers.get(chosen);assert(deadline);
   startup.timers.delete(chosen);deadline.action();
  }
  else startup.client.dispose();
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(startup.client.connectionChecking('run-1'),false);
  if(ending==='snapshot-timeout'){
   assert.equal(startup.timers.size,1,'failed transport keeps only its existing reconnect timer');
   assert.equal([...startup.timers.values()][0].delay,250);assert.equal(peer.closed,true);assert.equal(peer.closeCode,4001,'Snapshot deadline must reach the existing WS close facts as code4001');
  }else{assert.equal(startup.timers.size,0,'hidden native proof must return its finite read lease');assert.equal(peer.closeCode,undefined,'Other lifecycle endings must not claim snapshot deadline');}
  if(ending==='reply'){
   assert.equal(startup.client.connectionReady('run-1'),true);
   assert.equal(peer.sent.at(-1).subscribe,false,'hidden proof must stop business snapshot demand');
   const settled=peer.sent.length;for(let n=0;n<1000;n++)peer.emit('open');
   assert.equal(peer.sent.length,settled,'settled hidden native proof cannot restart from duplicate opens');
  }else if(ending==='timeout')assert.equal(startup.client.connectionReady('run-1'),false);
  assert(peer.sent.every(p=>p.command==='snapshot'),'recovery proof must never replay a business command');
  startupDeadlineRows.push({ending,timerRole:ending==='timeout'?'probe':ending==='snapshot-timeout'?'snapshot':'dispose',connectionChecking:startup.client.connectionChecking('run-1'),remainingTimers:startup.timers.size,retryDelay:[...startup.timers.values()][0]?.delay??null,socketClosed:peer.closed,...(peer.closeCode===undefined?{}:{closeCode:peer.closeCode})});
  startup.client.dispose();assert.equal(startup.timers.size,0);
 }

 // The actual client checks one fresh response even when its real QAM provider
 // remains hidden, then returns that temporary read lease and its deadlines.
 const hidden=fixture();hidden.client.setPassiveDemand('connection',true);const hiddenSocket=hidden.connect();hiddenSocket.readyState=1;
 hidden.client.setVisible(false);const beforeProbe=hiddenSocket.sent.length;
 const probe=hidden.client.checkConnectionOnOpen();const during=hiddenSocket.sent.slice(beforeProbe);
 assert(during.some(p=>p.command==='snapshot'&&p.subscribe!==false));assert(during.every(p=>p.command==='snapshot'));
 assert.equal(await hidden.client.checkConnectionOnOpen(),false,'overlapping opens reuse the existing check');
 assert.match(hiddenSocket.sent.at(-1).probeId,/^probe:test-client:/,'fresh response must echo the exact newly issued probe nonce');
 hiddenSocket.emit('message',{type:'snapshot',runId:'run-1',probeId:hiddenSocket.sent.at(-1).probeId,snapshot:{...hidden.snapshot(2),provenance:{kind:'ymcc-native',pid:123}}});assert.equal(await probe,true);
 assert.equal(hidden.timers.size,0);assert.equal(hiddenSocket.sent.at(-1).subscribe,false,'completed hidden check returns its borrowed snapshot demand');
 const timedOut=hidden.client.checkConnectionOnOpen();const ownedProbe=hidden.client.probe?.timer,probeDeadline=[ownedProbe,hidden.timers.get(ownedProbe)];assert(probeDeadline[1]);assert.equal(probeDeadline[1].delay,15000);hidden.timers.delete(probeDeadline[0]);probeDeadline[1].action();assert.equal(await timedOut,true);
 assert.equal(hidden.client.connectionReady('run-1'),false,'a still-OPEN peer cannot override an expired actual QAM response');assert.equal(hidden.timers.size,0);
 const restored=hidden.client.checkConnectionOnOpen();hiddenSocket.emit('message',{type:'snapshot',runId:'run-1',probeId:hiddenSocket.sent.at(-1).probeId,snapshot:{...hidden.snapshot(3),provenance:{kind:'ymcc-native',pid:123}}});await restored;
 assert.equal(hidden.client.connectionReady('run-1'),true);assert.equal(hidden.timers.size,0);
 // A current native reply can still be ready:false during YMCC startup.
 // It proves transport; it must neither enable controls nor trigger a restart.
 const notReady=hidden.client.checkConnectionOnOpen();
 hiddenSocket.emit('message',{type:'snapshot',runId:'run-1',probeId:hiddenSocket.sent.at(-1).probeId,snapshot:{...hidden.snapshot(4),ready:false,provenance:{kind:'ymcc-native',pid:123}}});await notReady;
 assert.equal(hidden.client.connectionReady('run-1'),true,'valid initializing native reply is a connected link');assert.equal(hidden.timers.size,0);
 const pendingCheck=hidden.client.checkConnectionOnOpen();
 assert.equal(hidden.client.connectionReady('run-1'),true,'legacy native reader must retain the established link during a pending opening');
 assert.equal(hidden.client.connectionChecking('run-1'),true);assert.equal(hidden.client.connectionChecking('old-run'),false);
 assert.equal(await hidden.client.checkConnectionOnOpen(),false);
 hiddenSocket.emit('message',{type:'snapshot',runId:'run-1',probeId:hiddenSocket.sent.at(-1).probeId,snapshot:{...hidden.snapshot(5),provenance:{kind:'ymcc-native',pid:123}}});await pendingCheck;
 assert.equal(hidden.client.connectionChecking('run-1'),false);assert.equal(hidden.client.connectionReady('run-1'),true);assert.equal(hidden.timers.size,0);
 const disposeCheck=hidden.client.checkConnectionOnOpen();hidden.client.dispose();await disposeCheck;
 assert.equal(hidden.client.connectionChecking('run-1'),false);assert.equal(hidden.client.connectionReady('run-1'),false);assert.equal(hidden.timers.size,0);

 const f=fixture();try{
  const socket=f.connect(),sentBefore=socket.sent.map(packet=>structuredClone(packet));
  socket.readyState=1;const timersBefore=f.timers.size;
  assert.equal(f.client.connectionReady('run-1'),true);assert.equal(f.client.connectionReady('run-2'),false);
  socket.readyState=3;assert.equal(f.client.connectionReady('run-1'),false);socket.readyState=1;
  for(let i=0;i<1000;i++)assert.equal(f.client.connectionReady('run-1'),true);
  assert.equal(f.timers.size,timersBefore);assert.deepEqual(socket.sent,sentBefore,'Native client health is read only');
  socket.emit('message',{type:'snapshot',runId:'run-2',snapshot:f.snapshot()});
  assert.equal(f.client.snapshot().connected,false);assert.equal(f.client.snapshot().snapshot,null);assert.equal(socket.closed,true);
  assert.equal(f.client.connectionReady('run-1'),false);
  assert.deepEqual(socket.sent,sentBefore,'A different run cannot replay an earlier packet or user operation');
  assert.equal(f.timers.size,1,'The still-visible owner retains one original reconnection retry');
  assert.equal([...f.timers.values()][0].delay,250,'Session rejection uses the original first backoff');
  const late=[...f.timers.values()].map(timer=>timer.action);
  f.client.setVisible(false);assert.equal(f.timers.size,0,'Releasing the visible owner cancels recovery');
  for(const action of late)action();assert.equal(f.sockets.length,1);assert.equal(f.timers.size,0);assert.deepEqual(socket.sent,sentBefore);
 }finally{f.client.dispose();}
});
await check('request freezes identity/generation/revision and args',async()=>{const f=fixture();const s=f.connect();const args={field:'acMode',value:'eco'};const p=f.client.mutate('game.setField',args);args.value='extreme';const req=s.sent.at(-1);assert.equal(req.args.value,'eco');assert.equal(req.identity,'app:480:pid101:birthA');assert.equal(req.generation,1);s.emit('message',{type:'reply',runId:'run-1',id:req.id,ok:true,result:{saved:true,applied:true}});await p;assert.equal(f.timers.size,0);f.client.dispose();});
await check('second mutation cannot overlap first',async()=>{const f=fixture();const s=f.connect();const p=f.client.mutate('fan.setPreset',{preset:'soft'});await assert.rejects(f.client.mutate('fan.setPreset',{preset:'aggressive'}),/当前不可操作/);const req=s.sent.at(-1);s.emit('message',{type:'reply',runId:'run-1',id:req.id,ok:true});await p;f.client.dispose();});
await check(actionTimeoutCase,async()=>{const f=fixture();const s=f.connect();const p=f.client.mutate('fan.setEnabled',{enabled:false});f.fireDelay(4000);await assert.rejects(p,/未确认/);assert.equal(s.sent.filter(m=>m.command==='fan.setEnabled').length,1);assert.equal(s.sent.at(-1).command,'snapshot');assert.equal(s.sent.at(-1).requestSource,'operation-timeout');assert.equal(f.client.snapshot().snapshot,null);assert.equal(f.client.snapshot().busy,false);f.client.dispose();});
await check('close rejects inflight mutation and reconnect does not replay it',async()=>{const f=fixture();const s=f.connect();const p=f.client.mutate('fan.setEnabled',{enabled:false});s.close();await assert.rejects(p,/中断/);f.fireDelay(250);const next=f.sockets.at(-1);next.emit('open');assert.equal(next.sent.length,1);assert.equal(next.sent[0].command,'snapshot');f.client.dispose();});
await check('silent first connection has finite deadline and reconnect',()=>{const f=fixture();f.client.setVisible(true);f.fireDelay(10000);assert.equal(f.sockets[0].closed,true);f.fireDelay(250);assert.equal(f.sockets.length,2);f.client.dispose();});
await check(outageBackoffCase,async()=>{const f=fixture();f.client.setVisible(true);for(const delay of [250,1000,3000,10000,30000,30000]){f.sockets.at(-1).close();f.fireDelay(delay);}assert.equal(f.sockets.length,7);const s=f.sockets.at(-1);s.emit('open');s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot()});assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);f.client.dispose();
 const rows=[];
 for(const entry of ['sidebar','qam']){
  const shared=fixture(),{client,sockets,timers,fireDelay}=shared;
  try{
   shared.bootstrap.hudProtocol=shared.bootstrap.wallpaperProtocol=1;
   const old=shared.connect();
   for(const consumer of ['connection','hud','wallpaper'])client.setPassiveDemand(consumer,true);
   const write=client.mutate('fan.setEnabled',{enabled:false}).then(()=>null,error=>error);
   old.close();assert.match((await write)?.message??'',/中断/);
   client.setVisible(false);
   for(const delay of [250,1000,3000,10000]){fireDelay(delay);sockets.at(-1).close();}
   const [retry]=timers.keys(),start=sockets.length;assert.equal(timers.get(retry)?.delay,30000);
   for(let n=0;n<8;n++){
    const previous=sockets.length;
    if(entry==='sidebar')client.setVisible(true);
    const check=entry==='qam'?client.checkConnectionOnOpen():null;
    if(sockets.length>previous)sockets.at(-1).close();
    if(check)await check;
    if(entry==='sidebar')client.setVisible(false);
   }
   const row={entry,extraSockets:sockets.length-start,retainedRetry:timers.has(retry),retryDelays:[...timers.values()].map(t=>t.delay)};
   rows.push(row);visibleReconnectRows.push(row);
   if(row.extraSockets!==0||!row.retainedRetry)continue; // Capture both old paths before asserting.
   if(entry==='sidebar')client.setVisible(true);
   fireDelay(30000);
   const next=sockets.at(-1);next.readyState=1;next.emit('open');
   const reads=next.sent.filter(p=>p.command==='snapshot'&&p.subscribe!==false);assert.equal(reads.length,1);
   if(entry==='qam')assert.equal(typeof reads[0].probeId,'string','Hidden reconnect still obtains a native proof');
   next.emit('message',{type:'snapshot',runId:'run-1',probeId:reads[0].probeId,snapshot:{...shared.snapshot(2),provenance:{kind:'ymcc-native',pid:123}}});
   await new Promise(resolve=>setImmediate(resolve));
   assert.equal(client.connectionVerified('run-1'),true);assert.equal(sockets.length,start+1);assert.equal(timers.size,0);
   assert.equal(sockets.flatMap(peer=>peer.sent).filter(p=>p.command==='fan.setEnabled').length,1);
   row.recovered=true;row.recoverySnapshotReads=reads.length;
  }finally{client.dispose();assert.equal(timers.size,0);assert.ok(sockets.every(peer=>peer.closed));}
 }
 for(const row of rows){assert.equal(row.extraSockets,0,row.entry);assert.equal(row.retainedRetry,true,row.entry);assert.deepEqual(row.retryDelays,[30000],row.entry);assert.equal(row.recovered,true,row.entry);}
});
await check('hide cancels queued reconnect',()=>{const f=fixture();const s=f.connect();s.close();f.client.setVisible(false);assert.equal(f.timers.size,0);assert.equal(f.sockets.length,1);});
await check('power not ready rejects changes without send',async()=>{const f=fixture();const s=f.connect();const snap=f.snapshot(2);snap.ready=false;s.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});await assert.rejects(f.client.mutate('fan.setEnabled',{enabled:true}),/当前不可操作/);assert.equal(s.sent.length,1);f.client.dispose();});
await check('fan requests do not include game identity',async()=>{const f=fixture();const s=f.connect();const p=f.client.mutate('fan.setPreset',{preset:'soft'});const req=s.sent.at(-1);assert.equal(req.identity,undefined);s.emit('message',{type:'reply',runId:'run-1',id:req.id,ok:true});await p;f.client.dispose();});
await check(lateBootstrapCase,async()=>{const f=fixture({boot:false});f.client.setVisible(true);assert.equal(f.sockets.length,0);assert.equal(f.timers.size,1);const late=[...f.timers.values()][0].action;f.makeBootstrapAvailable();f.client.bootstrapChanged();assert.equal(f.sockets.length,1);assert.equal(f.timers.size,1);late();assert.equal(f.sockets.length,1);const socket=f.sockets[0];socket.emit('open');socket.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot()});assert.equal(f.timers.size,0);f.client.dispose();
 for(const entry of ['sidebar','qam'])for(const edge of ['new-run','revoked']){
  const shared=fixture(),{client,sockets,timers,nativeWindow,bootstrap}=shared;
  try{
   const old=shared.connect();client.setPassiveDemand('connection',true);client.setVisible(false);old.close();
   assert.equal(timers.size,1);const [[retry,queued]]=timers;
   const current={...bootstrap,...(edge==='new-run'?{runId:'run-2',token:'REPLACEMENT-TEST',endpoint:'ws://127.0.0.1:12346/mirror'}:{revoked:true})};
   nativeWindow.__YMCC_DECKY_MIRROR__=current; // Deliberately omit the native change event.
   if(entry==='sidebar')client.setVisible(true);else await client.checkConnectionOnOpen();
   assert.equal(timers.has(retry),false,`${entry}: ${edge} must retire the old retry`);
   queued.action();
   if(edge==='new-run'){
    assert.equal(sockets.length,2);const next=sockets.at(-1);assert.ok(next.url.startsWith(current.endpoint+'?'));
    next.readyState=1;next.emit('open');const read=next.sent.at(-1);
    assert.equal(read.runId,current.runId);
    if(entry==='qam')assert.equal(typeof read.probeId,'string');
    next.emit('message',{type:'snapshot',runId:current.runId,probeId:read.probeId,snapshot:{...shared.snapshot(2),runId:current.runId,provenance:{kind:'ymcc-native',pid:123}}});
    await new Promise(resolve=>setImmediate(resolve));
    assert.equal(client.connectionVerified(current.runId),true);
   }else{assert.equal(sockets.length,1);assert.equal(client.snapshot().connected,false);}
   assert.equal(timers.size,0);
   visibleReconnectRows.push({entry,bindingEdge:edge,oldRetryRetired:true,verified:client.connectionVerified(current.runId),socketCount:sockets.length});
  }finally{client.dispose();assert.equal(timers.size,0);assert.ok(sockets.every(peer=>peer.closed));}
 }
});
await check('same native binding event cannot duplicate live connection',()=>{const f=fixture();f.connect();f.client.bootstrapChanged();assert.equal(f.sockets.length,1);assert.equal(f.timers.size,0);f.client.dispose();});
await check('new native run cancels old mutation, never replays into new host',async()=>{const f=fixture();const old=f.connect();const mutation=f.client.mutate('fan.setEnabled',{enabled:true});f.bootstrap.runId='run-2';f.bootstrap.token='NEW-TEST-TOKEN';f.client.bootstrapChanged();await assert.rejects(mutation,/连接已结束/);assert.equal(old.closed,true);assert.equal(f.sockets.length,2);f.sockets[1].emit('open');assert.equal(f.sockets[1].sent.length,1);assert.equal(f.sockets[1].sent[0].command,'snapshot');assert.equal(f.sockets[1].sent[0].runId,'run-2');f.client.dispose();});
await check('hidden bootstrap event starts zero sockets',()=>{const f=fixture();f.client.bootstrapChanged();assert.equal(f.sockets.length,0);assert.equal(f.timers.size,0);f.client.dispose();});
await check('late message from closed socket cannot restore connected state',()=>{const f=fixture();const old=f.connect();old.close();old.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(50)});assert.equal(f.client.snapshot().connected,false);assert.equal(f.client.snapshot().snapshot,null);f.client.dispose();});
await check('connection owns immutable bootstrap identity, not mutable global object',()=>{const f=fixture();const old=f.connect();f.bootstrap.runId='changed-without-event';old.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(2)});assert.equal(f.client.snapshot().snapshot.runId,'run-1');f.client.dispose();});
await check('port zero and out-of-range bootstrap are rejected',()=>{for(const port of [0,65536,99999])verifyRejectedVisibleBinding(`ws://127.0.0.1:${port}/mirror`);});
await check('Steam runtime ID getter runs once per demand, not per render or timer',()=>{let calls=0;const f=fixture({steam:()=>{calls++;return {availability:'observed',appId:'480',runningCount:1};}});const socket=f.connect();assert.equal(calls,1);assert.equal(socket.sent[0].steam.appId,'480');assert.equal(f.timers.size,0);f.client.setVisible(true);assert.equal(calls,1);f.client.dispose();});
await check('Steam runtime getter failure cannot prevent Fan snapshot connection',()=>{const f=fixture({steam:()=>{throw new Error('Steam API changed');}});const socket=f.connect();assert.equal(socket.sent[0].steam.availability,'unavailable');assert.equal(f.client.snapshot().connected,true);f.client.dispose();});
await check('slider accepts only the exact rendered snapshot',async()=>{const f=fixture(),s=f.connect(),expected=f.client.snapshot().snapshot;const p=f.client.mutate('fan.setPreset',{preset:'soft'},expected);const req=s.sent.at(-1);s.emit('message',{type:'reply',runId:'run-1',id:req.id,ok:true});await p;f.client.dispose();});
await check('slider rendered revision change rejects before sending any mutation',async()=>{const f=fixture(),s=f.connect(),expected=f.client.snapshot().snapshot;s.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(2)});await assert.rejects(f.client.mutate('fan.setPreset',{preset:'soft'},expected),/快照已变化/);assert.equal(s.sent.length,1);f.client.dispose();});
await check(tdpReceiptCase,async()=>{
 const record=[];
 for(const mode of ['write-ack','saved-unconfirmed','owner-unconfirmed','unknown-receipt','reply-timeout','transport-ended','non-tdp','before-send','sink-throws','regressed-clock']){
  let clock=100;const f=fixture({now:()=>clock}),s=f.connect(),snap=f.snapshot(2),rows=[];
  try{
   snap.actions={fan:true,game:true,performance:true};
   snap.performance={version:1,side:'ac',scope:'global',mode:'auto',sourceRevision:'source',enabled:true,controls:[
    {key:'tdp',label:'TDP',kind:'slider',enabled:true,value:20,min:5,max:30,step:1},
    {key:'bias',label:'Bias',kind:'slider',enabled:true,value:50,min:0,max:100,step:5}]};
   s.emit('message',{type:'snapshot',runId:'run-1',snapshot:snap});
   f.client.deps.tdpActionSettled=value=>{rows.push(JSON.parse(JSON.stringify(value)));if(mode==='sink-throws')throw Error('diagnostic failure');};
   const expected=f.client.snapshot().snapshot;
   if(mode==='before-send')expected.revision--;
   const sendAt=mode==='regressed-clock'?90:450;
   clock=sendAt;
   const pending=f.client.mutate('performance.setControls',{patch:mode==='non-tdp'?{bias:55}:{tdp:25}},expected);
   const request=s.sent.at(-1),sentBefore=s.sent.filter(x=>x.command==='performance.setControls').length;
   const reply=(id=request.id)=>s.emit('message',{type:'reply',runId:'run-1',id,ok:mode!=='owner-unconfirmed',error:mode==='owner-unconfirmed'?'owner state uncertain':undefined,
     result:mode==='saved-unconfirmed'?{saved:true,applied:false,pending:true}:mode==='unknown-receipt'?{saved:true,applied:'yes',pending:false}:{saved:true,applied:true,pending:false}});
   if(mode==='before-send'){await assert.rejects(pending);assert.equal(sentBefore,0);}
   else{
    assert.equal(sentBefore,1);reply('foreign-reply');assert.deepEqual(rows,[]);assert.equal(f.client.snapshot().busy,true);
    clock=sendAt+(mode==='reply-timeout'?15000:25);
    if(mode==='reply-timeout')f.fireDelay(15000);
    else if(mode==='transport-ended')s.close();else reply();
    if(['reply-timeout','owner-unconfirmed','transport-ended'].includes(mode))await assert.rejects(pending);else await pending;
    reply(); // Delayed or duplicate reply cannot create another terminal record.
   }
   const outcome=['sink-throws','regressed-clock'].includes(mode)?'write-ack':mode,expectedRows=['non-tdp','before-send'].includes(mode)?[]:[{generation:1,elapsedMs:clock-sendAt,outcome,connectedAgeAtSendMs:mode==='regressed-clock'?null:350}];
   tdpReceiptObservations.push({mode,sentBefore,rows});
   assert.deepEqual(rows,expectedRows,'one scoped TDP outcome for '+mode);
   assert.equal(s.sent.filter(x=>x.command==='performance.setControls').length,sentBefore);
   assert.equal(f.client.snapshot().busy,false);
  }finally{f.client.dispose();}
  assert.equal(f.timers.size,0);
 }
 // A snapshot received on the same transport can already settle the new
 // generation's view even though the original write result is still unknown.
 for(const [mode,generation,ready,kind] of [
   ['new-native-ready',2,true,'ymcc-native'],['same-generation',1,true,'ymcc-native'],
   ['new-unconfirmed',2,true,'unconfirmed'],['new-native-not-ready',2,false,'ymcc-native']
 ]){
  const f=fixture(),s=f.connect(),snap=f.snapshot(2),rows=[];
  try{
   snap.actions={fan:true,game:true,performance:true};
   snap.performance={version:1,side:'ac',scope:'global',mode:'auto',sourceRevision:'source',enabled:true,controls:[
    {key:'tdp',label:'TDP',kind:'slider',enabled:true,value:20,min:5,max:30,step:1}]};
   s.emit('message',{type:'snapshot',runId:snap.runId,snapshot:snap});
   f.client.deps.tdpActionSettled=value=>rows.push(JSON.parse(JSON.stringify(value)));
   const pending=f.client.mutate('performance.setControls',{patch:{tdp:25}}),id=s.sent.at(-1).id;
   const next={...snap,revision:3,generation,ready,provenance:kind==='ymcc-native'?{kind,pid:123}:{kind},notice:'current generation'};
   s.emit('message',{type:'snapshot',runId:snap.runId,snapshot:next});
   assert.equal(f.client.snapshot().snapshot?.revision,3);
   const reads=()=>s.sent.filter(x=>x.command==='snapshot').length;
   const before=reads();
   f.fireDelay(15000);
   await assert.rejects(pending,/未确认/);
   s.emit('message',{type:'reply',runId:snap.runId,id,ok:true,result:{saved:true,applied:true,pending:false}});
   const state=f.client.snapshot(),readDelta=reads()-before,sentBefore=s.sent.filter(x=>x.command==='performance.setControls').length;
   tdpReceiptObservations.push({mode,sentBefore,rows,readDelta,retainedRevision:state.snapshot?.revision??null,notice:state.notice,deadlines:f.timers.size});
   assert.equal(sentBefore,1,'timed out action is never replayed');
   assert.deepEqual(rows.map(({generation,outcome})=>({generation,outcome})),[{generation:1,outcome:'reply-timeout'}]);
   assert.equal(state.busy,false);assert.equal(s.closed,false);
   if(mode==='new-native-ready'){
    assert.equal(state.snapshot?.revision,3,'old timeout must keep the verified newer Ready snapshot');
    assert.equal(state.notice,next.notice);assert.equal(readDelta,0);assert.equal(f.timers.size,0);
   }else{
    assert.equal(state.snapshot,null);assert.equal(readDelta,1);assert.equal(s.sent.at(-1).requestSource,'operation-timeout');assert.equal(f.timers.size,1);
   }
  }finally{f.client.dispose();}
  assert.equal(f.timers.size,0);
 }
 const wire=clientDeclaration.initializer.arguments[0].properties.find(node=>node.name?.getText(entryTree)==='tdpActionSettled');
 assert(wire&&ts.isPropertyAssignment(wire)&&ts.isArrowFunction(wire.initializer),'actual plugin entry must wire TDP diagnostic');
 const consoleRows=[],log=vm.runInNewContext('('+wire.initializer.getText(entryTree)+')',{window:{console:{info:line=>consoleRows.push(line)}}});
 log({generation:1,elapsedMs:25,outcome:'write-ack',connectedAgeAtSendMs:350});
 record.push('[YMCC:DeckyClient] '+JSON.stringify({schema:1,stage:'tdp-action-settled',generation:1,elapsedMs:25,outcome:'write-ack',connectedAgeAtSendMs:350}));
 assert.deepEqual(consoleRows,record);
});
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
await check(manualConnectionCase,async()=>{const f=fixture(),s=f.connect();s.close();const pending=f.client.testConnection(),next=f.sockets.at(-1);assert.notEqual(next,s);next.emit('open');const req=next.sent.at(-1),snap=f.snapshot(2);snap.provenance={kind:'ymcc-native',pid:321};next.emit('message',{type:'snapshot',runId:'run-1',probeId:req.probeId,snapshot:snap});assert.equal((await pending).pid,321);assert.equal(f.timers.size,0);f.client.dispose();});
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
await check('hiding during busy admission preserves one accepted write until its reply',async()=>{
 const f=fixture(),socket=f.connect();let once=false;f.client.subscribe(state=>{if(state.busy&&!once){once=true;f.client.setVisible(false);}});const pending=f.client.mutate('fan.setPreset',{preset:'soft'});const writes=socket.sent.filter(m=>m.command!=='snapshot');assert.equal(writes.length,1);assert.equal(f.client.snapshot().busy,true);assert.equal(socket.closed,false);socket.emit('message',{type:'reply',runId:f.bootstrap.runId,id:writes[0].id,ok:true,result:{applied:true}});assert.equal((await pending).applied,true);assert.equal(socket.closed,true);assert.equal(f.timers.size,0);f.client.dispose();
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
  f.client.setVisible(false);assert.equal(f.client.snapshot().busy,true);assert.equal(socket.closed,false);const write=socket.sent.find(x=>x.command==='fan.setEnabled');socket.emit('message',{type:'reply',runId:f.bootstrap.runId,id:write.id,ok:true,result:{applied:true}});assert.equal(await pending,null);
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
await check(connectingPauseCase,async()=>{
 const modes=['native-proof','passive-only','reopen-before-open','handshake-timeout'];
 for(const boundary of modes){
  const f=fixture(),runId=f.bootstrap.runId;
  try{
   f.bootstrap.hudProtocol=1;f.client.setPassiveDemand(boundary==='native-proof'?'connection':'hud',true);
   const socket=f.sockets.at(-1);socket.readyState=0;
   const realSend=socket.send.bind(socket),[deadline]=f.timers.keys();let earlySends=0;
   socket.send=value=>{
    if(socket.readyState===0){earlySends++;throw new DOMException('WebSocket is still connecting','InvalidStateError');}
    realSend(value);
   };
   f.client.setVisible(true);f.client.setVisible(false);
   const row={mode:'connecting-pause',boundary,earlySends,closed:socket.closed,originalDeadline:f.timers.has(deadline),delays:[...f.timers.values()].map(t=>t.delay)};
   startupDeadlineRows.push(row);
   if(socket.closed)continue; // Preserve all before-state measurements for the same required assertion.
   assert.equal(f.sockets.length,1);
   if(boundary==='handshake-timeout'){
    f.fireDelay(10000);assert.equal(socket.closed,true);assert.deepEqual([...f.timers.values()].map(t=>t.delay),[250]);f.fireDelay(250);
    assert.equal(f.sockets.length,2);assert.equal(socket.sent.length,0);row.oneRetry=true;continue;
   }
   if(boundary==='reopen-before-open')f.client.setVisible(true);
   socket.readyState=1;socket.emit('open');
   assert.equal(f.client.snapshot().connected,true);
   const requests=socket.sent.filter(p=>p.command==='snapshot'&&p.subscribe!==false);
   assert.equal(requests.length,boundary==='passive-only'?0:1);
   if(boundary==='native-proof')assert.equal(typeof requests[0].probeId,'string');
   if(boundary!=='passive-only')socket.emit('message',{type:'snapshot',runId,probeId:requests[0].probeId,snapshot:{...f.snapshot(),provenance:{kind:'ymcc-native',pid:123}}});
   for(let i=0;i<8;i++)await Promise.resolve();
   if(boundary==='native-proof')assert.equal(f.client.connectionVerified(runId),true);
   assert.equal(f.timers.size,0);
   if(boundary!=='reopen-before-open'){
    const before=socket.sent.length;
    socket.emit('message',{type:'snapshot',runId,snapshot:f.snapshot(2)});
    assert.equal(socket.sent.length,before+1,'An OPEN hidden socket still corrects restored demand');
    assert.equal(socket.sent.at(-1).subscribe,false);
   }
   assert.equal(f.sockets.length,1);assert.equal(socket.closed,false);
   row.currentReads=requests.length;row.openedOriginal=true;
  }finally{f.client.dispose();assert.equal(f.timers.size,0);}
 }
 assert.deepEqual(startupDeadlineRows.filter(row=>row.mode==='connecting-pause').map(({earlySends,closed,originalDeadline,delays})=>({earlySends,closed,originalDeadline,delays})),modes.map(()=>({earlySends:0,closed:false,originalDeadline:true,delays:[10000]})));
});
await check(restoredPauseFailureCase,()=>{
 const f=fixture();f.bootstrap.hudProtocol=1;f.client.setPassiveVisible(true);
 const attempted=[];
 try{
  const socket=f.sockets.at(-1);socket.emit('open');socket.send=raw=>{attempted.push(JSON.parse(raw));throw Error('Inert restored pause send failed');};
  for(let i=0;i<1024;i++)socket.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot(i+1)});
  assert.deepEqual(attempted,[{command:'snapshot',runId:f.bootstrap.runId,subscribe:false,requestSource:'hidden-snapshot'}]);
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
 // A locally invalid selection has no possible correlated host receipt. It
 // cannot create or extend the live lease, or cancel one as a hidden context.
 const admitted=contextFixture();try{
  assert.equal(admitted.context(1,{appId:'0'}),false,'zero app ID cannot start shared recovery');
  assert.equal(admitted.timers.size,0);assert.equal(admitted.socket.sent.length,0);
  assert.equal(admitted.context(2),true);const timer=[...admitted.timers.keys()][0];
  for(const [seq,appId] of [[3,'00000000000000000000'],[3,'18446744073709551616'],[3,'12-3'],[3,480],[3,undefined],[-1,'480'],[1.5,'480'],[Number.MAX_SAFE_INTEGER+1,'480']])
   for(const visible of [true,false])assert.equal(admitted.context(seq,{appId,visible}),false);
  const cycle={};cycle.self=cycle;
  assert.equal(admitted.context(3,{visible:false,cycle}),false,'unsent hide must not cancel a valid lease');
  assert.equal(admitted.socket.sent.length,1);assert.deepEqual([...admitted.timers.keys()],[timer]);
  const unavailable={foreground:false,entry:null,status:'foreground-transport-unavailable'};
  admitted.ack(1,'wallpaper',{...unavailable,appId:'480'});assert.deepEqual([...admitted.timers.keys()],[timer]);
  admitted.ack(2,'wallpaper',{...unavailable,appId:'481'});assert.deepEqual([...admitted.timers.keys()],[timer]);
  admitted.ack(2,'wallpaper',{...unavailable,appId:'480'});assert.equal(admitted.timers.size,0);
  assert.equal(admitted.context(3,{appId:'18446744073709551615'}),true,'full unsigned shortcut ID');
  admitted.ack(3,'wallpaper',{appId:'18446744073709551615'});assert.equal(admitted.timers.size,0);
  assert.equal(admitted.context(4,{appId:'000001'}),true,'preserve host-admitted decimal text');
  assert.equal(admitted.context(5,{appId:'000001',visible:false}),true);
  assert.equal(admitted.timers.size,0);assert.equal(admitted.socket.closed,false);assert.equal(admitted.client.snapshot().connected,true);
 }finally{admitted.client.dispose();}
 const f=contextFixture();try{assert.equal(f.context(),true);assert.equal(f.timers.size,1);f.fireDelay(10000);assert.equal(f.client.snapshot().connected,false);assert.equal(f.socket.closed,true);assert.equal(f.socket.closeCode,4002,'wallpaper timeout must be visible as a distinct numeric cause to the native broker');assert.equal(f.timers.size,1);f.fireDelay(250);const next=f.sockets.at(-1);next.emit('open');assert.equal(f.client.snapshot().connected,true);assert.equal(next.sent.length,0);assert.equal(f.timers.size,0);f.context(2);next.emit('message',{type:'wallpaper',runId:f.bootstrap.runId,contextSeq:2,appId:'480',enabled:true,entry:null});assert.equal(f.timers.size,0);}finally{f.client.dispose();}
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

// Field 2026-10-31: Steam's document survives a deliberate YMCC exit. Recovery
// must reacquire a fresh native binding even if its notification was missed.
await check('YMCC restart while both views are hidden reacquires current state without an old write',async()=>{
 const f=fixture();f.bootstrap.wallpaperProtocol=1;
 try{
  const old=f.connect();f.client.setPassiveDemand('wallpaper',true);
  assert.equal(f.client.sendWallpaper('wallpaper.context',{contextSeq:1,appId:'480',visible:true}),true);
  const pending=f.client.mutate('fan.setEnabled',{enabled:false}).then(()=>null,error=>error);
  const write=old.sent.find(m=>m.command==='fan.setEnabled');
  const oldDeadlines=[...f.timers.values()].map(t=>t.action);
  old.close();assert.equal(typeof (await pending)?.message,'string');
  const retiredRetry=[...f.timers.values()][0].action;
  f.client.setVisible(false);f.client.setPassiveDemand('wallpaper',false);
  Object.assign(f.bootstrap,{endpoint:'ws://127.0.0.1:12346/mirror',token:'NEW-TEST-ONLY',runId:'run-restarted'});
  // No bootstrapChanged edge: both consumers were hidden while YMCC restarted.
  for(const callback of [...oldDeadlines,retiredRetry])callback();
  old.emit('message',{type:'reply',runId:'run-1',id:write.id,ok:true});
  old.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(9999)});
  assert.equal(f.sockets.length,1);assert.equal(f.timers.size,0);
  assert.equal(f.client.snapshot().snapshot,null);
  f.client.setPassiveDemand('wallpaper',true);
  const current=f.sockets.at(-1);assert.equal(f.sockets.length,2);
  assert.ok(current.url.startsWith(f.bootstrap.endpoint+'?'));current.emit('open');
  assert.equal(f.client.snapshot().connected,true);assert.equal(current.sent.length,0);
  assert.equal(f.client.sendWallpaper('wallpaper.context',{contextSeq:2,appId:'480',visible:true}),true);
  current.emit('message',{type:'wallpaper-ready',runId:f.bootstrap.runId,contextSeq:2,enabled:true});
  f.client.setVisible(true);
  const snapshot={...f.snapshot(),runId:f.bootstrap.runId,generation:2};
  current.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot});
  assert.equal(f.client.snapshot().snapshot.runId,'run-restarted');
  assert.equal(f.client.snapshot().snapshot.generation,2);assert.equal(f.timers.size,0);
  assert.equal(f.sockets.flatMap(s=>s.sent).filter(m=>m.command==='fan.setEnabled').length,1);
 }finally{f.client.dispose();}
 assert.equal(f.timers.size,0);
});
await check('background outage survives capped failures and missed native restart notification',()=>{
 const f=contextFixture();
 try{
  assert.equal(f.context(),true);f.socket.close();
  for(const delay of [250,1000,3000,10000,30000,30000]){f.fireDelay(delay);f.sockets.at(-1).close();}
  assert.equal(f.sockets.length,7);assert.equal(f.timers.size,1);
  Object.assign(f.bootstrap,{endpoint:'ws://127.0.0.1:12347/mirror',token:'REPLACED-TEST-ONLY',runId:'run-after-outage'});
  f.fireDelay(30000);const current=f.sockets.at(-1);current.emit('open');
  assert.ok(current.url.startsWith(f.bootstrap.endpoint+'?'));
  assert.equal(f.client.snapshot().connected,true);assert.equal(current.sent.length,0);
  for(const old of f.sockets.slice(0,-1)){
   old.emit('open');old.emit('message',{type:'wallpaper-ready',runId:'run-1',contextSeq:1,enabled:true});old.close();
  }
  assert.equal(f.client.sendWallpaper('wallpaper.context',{contextSeq:2,appId:'480',visible:true}),true);
  current.emit('message',{type:'wallpaper',runId:f.bootstrap.runId,contextSeq:2,appId:'480',enabled:true,entry:null});
  assert.equal(f.client.snapshot().connected,true);assert.equal(f.timers.size,0);
  assert.ok(f.sockets.flatMap(s=>s.sent).every(m=>m.command.startsWith('wallpaper.')));
 }finally{f.client.dispose();}
});
async function verifyTeardownObserverReentry(){
 for(const edge of ['close','error','protocol','probe-protocol']){
  for(const replacement of ['same-run','new-run']){
   const f=fixture();let active=false,once=false,current=null,newIntent=null,secondWrite=null;
   let oldIntent=null,oldProbe=null;
   try{
    const old=f.connect();old.readyState=1;
    if(edge==='probe-protocol')oldProbe=f.client.testConnection().then(value=>value,error=>error);
    else oldIntent=f.client.mutate('fan.setEnabled',{enabled:false}).then(value=>value,error=>error);
    const oldWrite=old.sent.find(packet=>packet.command==='fan.setEnabled');
    const late=[...f.timers.values()].map(timer=>timer.action);
    const off=f.client.subscribe(state=>{
     if(!active||once)return;
     if(oldProbe?state.connectionTest?.pending!==false:state.busy!==false)return;
     once=true;
     if(replacement==='new-run')Object.assign(f.bootstrap,{endpoint:'ws://127.0.0.1:12346/mirror',token:'CLOSE-REENTRY-TEST',runId:'run-after-close'});
     f.client.bootstrapChanged();current=f.sockets.at(-1);current.readyState=1;current.emit('open');
     current.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:{...f.snapshot(3),runId:f.bootstrap.runId,provenance:{kind:'ymcc-native',pid:222}}});
     newIntent=f.client.mutate('fan.setEnabled',{enabled:true}).then(value=>value,error=>error);
     secondWrite=current.sent.findLast(packet=>packet.command==='fan.setEnabled');assert(secondWrite);
    });
    active=true;
    if(edge==='close')old.close();else if(edge==='error')old.emit('error');else old.emit('message','invalid-json');
    for(let n=0;n<40;n++)await Promise.resolve();
    assert.equal(once,true);assert(current);assert.notEqual(current,old);
    assert.equal(current.closed,false,'retired teardown must not close observer replacement');
    assert.equal(f.client.snapshot().connected,true,'retired close must not overwrite replacement connection');
    assert.equal(f.client.snapshot().busy,true,'retired close must not clear a new explicit intent');
    assert.equal(f.client.snapshot().snapshot.runId,f.bootstrap.runId);
    assert.equal(f.client.pending.id,secondWrite.id);assert.equal(old.closed,true);
    if(oldIntent)assert.equal(typeof (await oldIntent).message,'string');
    if(oldProbe)assert.equal(typeof (await oldProbe).message,'string');
    for(const callback of late)callback();
    if(oldWrite)old.emit('message',{type:'reply',runId:'run-1',id:oldWrite.id,ok:true,result:{stale:true}});
    old.emit('message',{type:'snapshot',runId:'run-1',snapshot:f.snapshot(99999)});
    assert.equal(f.client.pending.id,secondWrite.id);assert.equal(current.closed,false);
    current.emit('message',{type:'reply',runId:f.bootstrap.runId,id:secondWrite.id,ok:true,result:{applied:true}});
    assert.equal((await newIntent).applied,true);assert.equal(f.client.snapshot().busy,false);assert.equal(f.timers.size,0);
    assert.equal(f.sockets.flatMap(socket=>socket.sent).filter(packet=>packet.command==='fan.setEnabled').length,oldProbe?1:2,'only distinct explicit intents; never replay old write');
    startupDeadlineRows.push({mode:'teardown-observer-reentry',edge,replacement,newExplicitIntentApplied:true,oldIntentReplayed:false,timers:f.timers.size});
    off();
   }finally{f.client.dispose();if(oldIntent)await oldIntent;if(oldProbe)await oldProbe;if(newIntent)await newIntent;assert.equal(f.timers.size,0);assert(f.sockets.every(socket=>socket.closed));}
  }
 }
}

await check('24 mixed restart cycles fence silent connections old replies deadlines and duplicate edges',async()=>{
 await verifyTeardownObserverReentry();
 const f=fixture();f.bootstrap.wallpaperProtocol=1;f.bootstrap.hudProtocol=1;f.client.setPassiveDemand('wallpaper',true);
 const first=f.sockets.at(-1);first.emit('open');f.client.setVisible(true);
 first.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:f.snapshot()});
 try{
  for(let cycle=0;cycle<24;cycle++){
   const old=f.sockets.at(-1),oldRun=f.bootstrap.runId;
   assert.equal(f.client.sendWallpaper('wallpaper.context',{contextSeq:cycle+1,appId:'480',visible:true}),true);
   const pending=f.client.mutate('fan.setEnabled',{enabled:cycle%2===0}).then(()=>null,error=>error);
   const write=old.sent.findLast(m=>m.command==='fan.setEnabled');
   const late=[...f.timers.values()].map(t=>t.action);
   old.close();assert.equal(typeof (await pending)?.message,'string');assert.equal(f.timers.size,1);
   if(cycle%3===0){
    f.fireDelay([...f.timers.values()][0].delay);
    // A connecting Steam-side WebSocket never reaches OPEN.
    assert.equal(f.timers.size,1);f.fireDelay(10000);
    assert.equal(f.client.snapshot().connected,false);assert.equal(f.timers.size,1);
   }
   late.push(...[...f.timers.values()].map(t=>t.action));
   Object.assign(f.bootstrap,{endpoint:`ws://127.0.0.1:${12400+cycle}/mirror`,token:`TEST-${cycle}`,runId:`run-cycle-${cycle}`});
   if(cycle%2){f.client.bootstrapChanged();f.client.bootstrapChanged();}
   else f.fireDelay([...f.timers.values()][0].delay); // Lost native event.
   const current=f.sockets.at(-1);assert.ok(current.url.startsWith(f.bootstrap.endpoint+'?'));current.emit('open');
   const fresh={...f.snapshot(),runId:f.bootstrap.runId,generation:cycle+2};
   current.emit('message',{type:'snapshot',runId:f.bootstrap.runId,snapshot:fresh});
   assert.equal(f.client.sendWallpaper('wallpaper.context',{contextSeq:cycle+2,appId:'480',visible:true}),true);
   current.emit('message',{type:'wallpaper-ready',runId:f.bootstrap.runId,contextSeq:cycle+2,enabled:true});
   for(const callback of late)callback();
   old.emit('message',{type:'reply',runId:oldRun,id:write.id,ok:true});
   old.emit('message',{type:'snapshot',runId:oldRun,snapshot:{...f.snapshot(999999),runId:oldRun}});old.close();
   assert.equal(current.closed,false);assert.equal(f.client.snapshot().connected,true);
   assert.equal(f.client.snapshot().snapshot.runId,f.bootstrap.runId);
   assert.equal(f.client.snapshot().snapshot.generation,cycle+2);assert.equal(f.timers.size,0);
   assert.equal(f.sockets.flatMap(s=>s.sent).filter(m=>m.command==='fan.setEnabled').length,cycle+1);
  }
 }finally{f.client.dispose();}
 assert.equal(f.timers.size,0);assert.ok(f.sockets.every(s=>s.closed));
});

writeRaw();console.log(`Mirror client tests passed: ${cases.length}`);
