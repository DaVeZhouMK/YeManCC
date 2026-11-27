import vm from 'node:vm';import crypto from 'node:crypto';
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
async function runUpdaterHostPowerCases({root,out,selection='all'}){
 const installCases=['read-sequence','native-sequence','handoff-sequence','denied-return','generation-change','metadata-only','healthy','legacy','stop','read-suspend-serial','native-suspend-serial','handoff-suspend-serial','held-game-read','held-power-read','submitted-install','native-readback-first','native-denial-first'];
 const observeCases=['observe-while-suspended','observe-merged-ready','observe-unsubscribed','observe-replaced'];
 const observing=selection==='observe'||observeCases.includes(selection),names=observing?observeCases:installCases;
 assert(selection==='all'||selection==='observe'||names.includes(selection),'Unknown updater host power case');
 fs.mkdirSync(out,{recursive:true});assert(!fs.existsSync(path.join(out,'HOST-POWER-RESULT.json')),'Raw result already exists');
 const req=createRequire(path.join(root,'package.json')),{build}=req('esbuild'),ports={
  'src/bridge/ipc.ts':"export const invoke=(...args)=>globalThis.__updaterHostPower.invoke(...args);export const on=(name,fn)=>globalThis.__updaterHostPower.on(name,fn);export class IpcRemoteError extends Error {}",
  'src/bridge/gamedetect.ts':"export const refreshGameStatusStrict=()=>globalThis.__updaterHostPower.readGame();",
  'src/bridge/gamePolicyTarget.ts':"export const isPolicyGameInitialized=()=>true;export const getPolicyGame=()=>globalThis.__updaterHostPower.game;export const subscribePolicyGameStatus=fn=>{globalThis.__updaterHostPower.gameListeners.add(fn);return()=>globalThis.__updaterHostPower.gameListeners.delete(fn)};",
  'src/bridge/updateManager.ts':"export const updateSnapshot={phase:'downloaded',version:'0.0.40',sha256:'a'.repeat(64)};export const updateInfo={value:{version:'0.0.40',sha256:'a'.repeat(64)}};export const ensureUpdateManager=()=>Promise.resolve();export const checkForUpdate=()=>{throw Error('No updater check expected')};export const prepareUpdate=()=>{throw Error('No updater download expected')};export const downloadAndInstall=()=>globalThis.__updaterHostPower.install();",
  'src/bridge/deckyMirrorExtensions.ts':"export const registerDeckyMirrorExtension=value=>{const h=globalThis.__updaterHostPower;assertUnique(h);h.extension=value;return()=>{if(h.extension===value)h.extension=null}};function assertUnique(h){if(h.extension)throw Error('Duplicate fixture extension')}",
 };
 if(observing){
  delete ports['src/bridge/updateManager.ts'];
  // The real manager links the scheduler's game-policy consumers. These paths must stay inert during a status read.
  for(const [file,names] of Object.entries({gamedetect:['validateGameTarget','isCurrentGameTarget','isGameStatusCurrent'],gamePolicyTarget:['gamePolicyKey','detectPolicyGame','isPolicyTargetCurrent']})){
   ports[`src/bridge/${file}.ts`]+=names.map(name=>`export function ${name}(){globalThis.__updaterHostPower.unexpected('${name}');}`).join('');
  }
 }
 const loaded=[],built=await build({stdin:{contents:"export {startDeckyUpdaterHost} from './src/bridge/deckyUpdaterHost';export {powerLifecycle} from './src/bridge/api';export {onSharedPowerLifecycle,getSharedPowerLifecycle,getSharedPowerLifecycleDiagnostics} from './src/bridge/powerLifecycleState';",resolveDir:root,loader:'ts'},bundle:true,write:false,metafile:true,platform:'node',format:'cjs',target:'es2022',tsconfig:path.join(root,'tsconfig.json'),plugins:[{name:'explicit-updater-host-leaves',setup(b){b.onLoad({filter:/\.(?:ts|tsx|js|mjs)$/},a=>{const rel=path.relative(root,a.path).replaceAll('\\','/');const content=ports[rel]??fs.readFileSync(a.path,'utf8');loaded.push({path:a.path,relative:rel,kind:ports[rel]?'inert-leaf':'actual',sha256:crypto.createHash('sha256').update(content).digest('hex'),...(ports[rel]?{contents:content}:{bytes:Buffer.byteLength(content)})});return{contents:content,loader:a.path.endsWith('.tsx')?'tsx':a.path.endsWith('.ts')?'ts':'js',resolveDir:path.dirname(a.path)}})}}]});
 fs.writeFileSync(path.join(out,'HOST-POWER.cjs'),built.outputFiles[0].contents,{flag:'wx'});fs.writeFileSync(path.join(out,'HOST-POWER-GRAPH.json'),JSON.stringify({loaded,metafile:built.metafile},null,2)+'\n',{flag:'wx'});
 const flush=async()=>{for(let i=0;i<50;i++)await Promise.resolve();},defer=()=>{let resolve;const promise=new Promise(r=>resolve=r);return{promise,resolve}};
 const fact=(seq=10,revision=1,extra={})=>({known:true,generation:7,phase:'ready',inputReady:true,hardwareWritesAllowed:true,notificationPending:false,publicationRevision:revision,suspendSerial:0,...(seq===undefined?{}:{observedNotificationSequence:seq}),...extra});
 const results=[];
 for(const name of names.filter(n=>selection==='all'||selection==='observe'||n===selection)){
  const events=new Map(),timers=new Map(),gameListeners=new Set(),gameGate=defer(),nativeGate=defer(),installGate=defer(),messages=[],observations=[];let serial=0,powerReads=0,installPowerReads=0,gameReads=0,installs=0,nativeSettled=false,host=null,release=null,m=null,error=null,oldPowerSignal;
  const readbackFirst=name==='native-readback-first'||name==='native-denial-first';
  const heldRead=name==='held-game-read'||name==='held-power-read', splitPower=heldRead||name==='submitted-install'||readbackFirst;
  let current=fact();if(name==='legacy'){delete current.observedNotificationSequence;delete current.suspendSerial;}
  const startsSuspended=name==='observe-while-suspended'||name==='observe-unsubscribed';
  if(startsSuspended)current=fact(10,1,{phase:'suspended',inputReady:false,hardwareWritesAllowed:false,suspendSerial:1});
  const stateReads=[],unexpected=[],preflightSignals=new Set();
  const h={extension:null,game:null,gameListeners,unexpected(command){unexpected.push(command);throw Error('Unexpected call '+command)},on(event,fn){let set=events.get(event);if(!set)events.set(event,set=new Set());set.add(fn);return()=>set.delete(fn)},emit(event,value){for(const fn of [...events.get(event)??[]])fn(value)},readGame(){gameReads++;return name==='held-game-read'&&gameReads>1?Promise.resolve(null):gameGate.promise},install(){installs++;return name==='submitted-install'?installGate.promise:Promise.resolve()},invoke(command,args,options){
   if(command==='power.lifecycle'){
    powerReads++;
    if(observing)return Promise.resolve({...current});
    if(splitPower){
     if(!preflightSignals.has(options.signal))return Promise.resolve({...current});
     if(++installPowerReads===1){oldPowerSignal=options.signal;if(name!=='held-game-read')return nativeGate.promise;}
     return Promise.resolve({...current});
    }
    return powerReads===1||nativeSettled?Promise.resolve({...current}):nativeGate.promise;
   }
   if(command==='app.updateState'&&observing){const read={signal:options.signal,...defer()};stateReads.push(read);return read.promise;}
   if(command==='deckySidebar.mirrorReply'){messages.push(observing?{...structuredClone(args.message),connection:args.connection}:structuredClone(args.message));return Promise.resolve(true)}return h.unexpected(command)}
  };
  const module={exports:{}},set=(fn,ms)=>{const id=++serial;timers.set(id,{fn,ms});return id},clear=id=>timers.delete(id);
  const document=Object.assign(new EventTarget(),{visibilityState:'visible',createElement:()=>({})}),window=new EventTarget();
  const context={module,exports:module.exports,process:{env:{}},Buffer,URL,URLSearchParams,performance,crypto,atob,btoa,__updaterHostPower:h,queueMicrotask,console,Promise,AbortController,structuredClone,setTimeout:set,clearTimeout:clear,window,document,CustomEvent,TextDecoder,TextEncoder};
  vm.runInNewContext(built.outputFiles[0].text,context,{filename:'actual-updater-host-power.cjs'});m=module.exports;
  if(splitPower){
   // Both the shared source and explicit preflight now carry an abort signal.
   const get=m.powerLifecycle.get;
   m.powerLifecycle.get=(timeout,signal)=>{assert(signal);preflightSignals.add(signal);return get(timeout,signal);};
  }
  const baseline=m.getSharedPowerLifecycleDiagnostics(),baselineEvents=[...events.values()].reduce((n,s)=>n+s.size,0);
  const emit=value=>{current={...value};h.emit('power.lifecycleChanged',current)};
  const peer={connection:'1',runId:'b'.repeat(32)},clientId='aabbccdd-1122-3344-5566-778899aabbcc',send=id=>h.extension.request({...peer,request:{command:'updater.request',runId:peer.runId,clientId,id,action:'install',version:'0.0.40',sha256:'a'.repeat(64)}});
  try{
   release=m.onSharedPowerLifecycle(()=>{});await flush();assert.equal(m.getSharedPowerLifecycle().phase,startsSuspended?'suspended':'ready');
   host=m.startDeckyUpdaterHost(()=>true);h.extension.connected(peer);h.extension.request({...peer,request:{command:'updater.status',runId:peer.runId}});await flush();
   if(observing){
    const next=fact(11,2,{suspendSerial:1}),replacement={...peer,connection:'2'};
    const status=(p,subscribe)=>h.extension.request({...p,request:{command:'updater.status',runId:p.runId,subscribe}});
    const saved=(version,operationId)=>({phase:'installing',stage:'install',installerActive:true,version,operationId,sha256:'a'.repeat(64)});
    const first=startsSuspended?0:1;
    assert.equal(stateReads.length,first,'A known suspend must not start the state read');
    emit(next);
    if(name==='observe-unsubscribed')status(peer,false);
    if(name==='observe-replaced')h.extension.connected(replacement);
    await flush();
    const retired=name==='observe-unsubscribed'||name==='observe-replaced';
    if(!startsSuspended)assert.equal(stateReads[0].signal.aborted,true,'Merged Ready must retire the old manager slot');
    observations.push({stage:'after-ready',stateReads:stateReads.length,source:m.getSharedPowerLifecycleDiagnostics()});
    assert.equal(stateReads.length,first+Number(!retired),'The admitted observer must read exactly once for its current scope');
    if(name==='observe-unsubscribed'){
     const sent=messages.length;emit(fact(12,3,{suspendSerial:2}));await flush();assert.equal(stateReads.length,0);assert.equal(messages.length,sent);
    }else{
     if(name==='observe-replaced'){
      const sent=messages.length;stateReads[0].resolve(saved('100.0.0','obsolete-installer'));await flush();assert.equal(messages.length,sent,'Retired read cannot publish to an unsubscribed replacement');
      status(replacement,true);await flush();assert.equal(stateReads.length,2);
     }
     stateReads.at(-1).resolve(saved('99.0.0','current-installer'));await flush();
     const published=messages.filter(x=>x.type==='ymcc-updater').at(-1);
     assert.equal(published?.view?.targetVersion,'99.0.0');assert.equal(published.connection,name==='observe-replaced'?'2':'1');
     const sent=messages.length;
     if(!startsSuspended)stateReads[0].resolve(saved('100.0.0','obsolete-installer'));
     emit({...next,publicationRevision:3});emit({...next,publicationRevision:3});h.emit('power.lifecycleChanged',fact());await flush();
     assert.equal(stateReads.length,first+1,'Metadata, duplicate Ready and stale Ready cannot issue a new read');
     assert.equal(messages.length,sent,'Metadata and obsolete reply must not publish');
     assert(!messages.some(x=>x.view?.targetVersion==='100.0.0'),'Old installer state must never be shown');
    }
    assert.deepEqual(unexpected,[]);assert.equal(gameReads,0);assert.equal(installs,0);
   }else{
   send(1);await flush();assert.equal(gameReads,1,'Actual Host must enter game read');
    if(readbackFirst){
     const prior={...current},denied=name==='native-denial-first';
     gameGate.resolve(null);await flush();assert.equal(installPowerReads,1,'The original install must reach its Native read');
     current=fact(11,2,{suspendSerial:1,...(denied?{phase:'suspending',inputReady:false,hardwareWritesAllowed:false}: {})});
     nativeSettled=true;nativeGate.resolve({...current});await flush();
     assert.equal(installs,0,'A newer Native fact cannot authorize the old explicit request');
     observations.push({stage:'native-readback-without-broadcast',source:m.getSharedPowerLifecycleDiagnostics(),installs});
     if(denied){
      assert.equal(m.getSharedPowerLifecycle().hardwareWritesAllowed,false,'The live denial must close the shared gate without a broadcast');
      send(2);await flush();assert.equal(gameReads,1,'A denied scope must block new install preflight');
      emit(fact(12,3,{suspendSerial:1}));await flush();assert.equal(installs,0,'Ready delivery never replays an old install');
     }
     send(3);await flush();
     assert.equal(installs,1,'A fresh confirmation must use the current Native scope');
     assert.equal(gameReads,2);assert.equal(m.getSharedPowerLifecycle().suspendSerial,1);
     h.emit('power.lifecycleChanged',prior);send(1);await flush();
     assert.equal(installs,1,'A stale event and retransmitted request must not reinstall');
    }else if(splitPower){
    if(name!=='held-game-read'){gameGate.resolve(null);await flush();assert.equal(installPowerReads,1,'Install preflight must enter the original power read');}
    if(name==='submitted-install'){
     nativeGate.resolve({...current});await flush();assert.equal(installs,1,'First install must have crossed the submit boundary');
     emit(fact(11,2,{generation:8,suspendSerial:1}));send(2);await flush();
     assert.equal(installs,1,'New Ready cannot release an already submitted install');assert.equal(gameReads,1);
     assert.equal(messages.find(x=>x.type==='ymcc-updater-result'&&x.id===2)?.code,'unavailable');
     installGate.resolve();await flush();assert.equal(messages.filter(x=>x.type==='ymcc-updater').at(-1)?.view?.canInstall,true);
    }else{
     const previous={...current};
     emit(fact(11,2,{generation:8,suspendSerial:1,known:false,hardwareWritesAllowed:false}));await flush();
     assert.equal(messages.filter(x=>x.type==='ymcc-updater').at(-1)?.view?.canInstall,false);
     emit(fact(11,3,{generation:8,suspendSerial:1}));await flush();
     assert.equal(installs,0,'Ready must not replay the revoked explicit install');
     assert.equal(messages.filter(x=>x.type==='ymcc-updater').at(-1)?.view?.canInstall,true,'Revoked preflight held the install queue after current Ready');
     if(name==='held-power-read')assert.equal(oldPowerSignal?.aborted,true,'Only the old read-only power IPC must be aborted');
     nativeSettled=true;send(2);await flush();
     assert.equal(installs,1,'A fresh confirmation must install before the old read returns');assert.equal(gameReads,2);
     gameGate.resolve(null);nativeGate.resolve(previous);send(1);await flush();
     assert.equal(installs,1,'Late read or retransmission must not replay the retired install');
    }
    observations.push({stage:name,installs,gameReads,powerReads,installPowerReads});
   }else{
   if(name==='read-sequence')emit(fact(11,2));
   if(name==='read-suspend-serial')emit(fact(10,2,{suspendSerial:1}));
   if(name==='denied-return'){emit(fact(11,2,{known:false,hardwareWritesAllowed:false}));emit(fact(11,3));}
   if(name==='generation-change')emit(fact(11,2,{generation:8}));
   if(name==='metadata-only')emit(fact(10,2));
   if(name==='stop'){host.stop();host=null;}
   gameGate.resolve(null);await flush();
   if(name==='native-sequence')emit(fact(11,2));
   const handoff=name==='handoff-sequence'||name==='handoff-suspend-serial';
   if(handoff){
    nativeSettled=true;nativeGate.resolve({...current});let ticks=0;const inject=()=>{const source=m.getSharedPowerLifecycleDiagnostics();if(source.subscriberCount===2){observations.push({stage:'after-validation-release-before-service-continuation',source,ticks,installsAtNotification:installs});emit(name==='handoff-sequence'?fact(11,2):fact(10,2,{suspendSerial:1}));}else if(++ticks<16)queueMicrotask(inject);else observations.push({stage:'handoff-injection-missed',source,ticks});};queueMicrotask(inject);
   }else {nativeSettled=true;nativeGate.resolve(name==='native-suspend-serial'?fact(10,2,{suspendSerial:1}):{...current});}
   await flush();
   if(handoff){const edge=observations.find(o=>o.stage==='after-validation-release-before-service-continuation');assert(edge,'Actual validation subscription must have retired before the handoff event');assert.equal(edge.source.subscriberCount,2);assert.equal(edge.installsAtNotification,0,'Event must precede first install write');}
   const expected=['metadata-only','healthy','legacy'].includes(name)?1:0;
   observations.push({stage:'settled-old-request',installs,expected,gameReads,powerReads,source:m.getSharedPowerLifecycleDiagnostics(),message:messages.filter(x=>x.type==='ymcc-updater').at(-1)?.view?.message});
   assert.equal(installs,expected,'Old explicit install must retire when its public notification scope changes: '+name);
   if(name.endsWith('-sequence')||name.endsWith('-suspend-serial')){
    if(name==='native-suspend-serial')emit(fact(10,2,{suspendSerial:1}));
    send(2);await flush();assert.equal(installs,1,'A fresh explicit request may use the new admitted scope');assert.equal(gameReads,2);
   }
   }
   }
  }catch(e){error={message:e.message,stack:e.stack};}
  finally{
   host?.stop();for(const read of stateReads)read.resolve({phase:'idle'});release?.();await flush();
   const diagnostic=m.getSharedPowerLifecycleDiagnostics(),progressListeners=events.get('update.progress')?.size??0,activeEvents=[...events.values()].reduce((n,s)=>n+s.size,0);
   observations.push({stage:'disposed',activeTimers:timers.size,activeEvents,baselineEvents,progressListeners,gameListeners:gameListeners.size,extensionPresent:!!h.extension,source:diagnostic});
   try{assert.equal(timers.size,0);assert.equal(activeEvents,baselineEvents+progressListeners);assert(progressListeners<=(observing?1:0));assert.equal(gameListeners.size,0);assert.equal(h.extension,null);assert.equal(diagnostic.active,baseline.active);assert.equal(diagnostic.subscriberCount,baseline.subscriberCount);}catch(e){error??={message:e.message,stack:e.stack}}
  }
  results.push({name,pass:!error,error,installs,gameReads,powerReads,stateReads:stateReads.length,unexpected,observations});
 }
 const result={status:results.every(r=>r.pass)?'PASS':'FAIL',selection,results,actualHostServiceApiAndSharedSource:true,actualUpdateManager:observing,externalNativeGameManagerPortsInert:!observing,actualInstallOrSystemActions:0,newTimerThreadPoller:0};
 fs.writeFileSync(path.join(out,'HOST-POWER-RESULT.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx'});return result;
}


if(process.argv.includes('--host-power-case')){
 const at=process.argv.indexOf('--host-power-case');assert.equal(at,3,'Use OUT --host-power-case NAME');assert.equal(process.argv.length,5,'One case and output required');const selected=process.argv[4];
 const result=await runUpdaterHostPowerCases({root:path.resolve(import.meta.dirname,'..'),out:path.resolve(process.argv[2]),selection:selected});
 console.log(JSON.stringify({status:result.status,cases:result.results.map(r=>({name:r.name,pass:r.pass,error:r.error?.message}))}));process.exit(result.status==='PASS'?0:2);
}
const selectedName='replacement page owns demand and old teardown cannot disable it';let output=null,onlyCase=null,excludeCase=null,ownerVariant=null;const args=process.argv.slice(2);for(let n=0;n<args.length;n++){const arg=args[n];if(arg.startsWith('--owner-variant=')){assert(ownerVariant===null,'UPDATER_OWNER_VARIANT_DUPLICATE');ownerVariant=arg.slice('--owner-variant='.length);assert(['reopen-only','mirror-close','open-handoff'].includes(ownerVariant),'UPDATER_OWNER_VARIANT_UNKNOWN');continue;}if(arg==='--case'||arg==='--exclude-case'){assert(n+1<args.length,'UPDATER_SERVICE_SELECTION_EMPTY');const value=args[++n];assert(value&&!value.startsWith('--'),'UPDATER_SERVICE_SELECTION_EMPTY');if(arg==='--case'){assert(onlyCase===null,'UPDATER_SERVICE_SELECTION_DUPLICATE');onlyCase=value;}else{assert(excludeCase===null,'UPDATER_SERVICE_SELECTION_DUPLICATE');excludeCase=value;}}else{assert(arg&&!arg.startsWith('-')&&output===null,'UPDATER_SERVICE_ARGUMENT_UNKNOWN');output=arg;}}assert(onlyCase===null||onlyCase===selectedName,'UPDATER_SERVICE_CASE_UNKNOWN');assert(excludeCase===null||excludeCase===selectedName,'UPDATER_SERVICE_EXCLUDE_UNKNOWN');assert(onlyCase===null||excludeCase===null,'UPDATER_SERVICE_SELECTION_CONFLICT');assert(ownerVariant===null||onlyCase===selectedName,'UPDATER_OWNER_VARIANT_CASE_CONFLICT');
const root=path.resolve(import.meta.dirname,'..'),require=createRequire(path.join(root,'package.json')),{build}=require('esbuild'),out=path.resolve(output||'work/decky-updater-tests');fs.mkdirSync(out,{recursive:true});
const loadedInputs=new Map(),digest=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const bindInputs=result=>{for(const input of Object.keys(result.metafile.inputs)){const file=path.resolve(root,input),sha256=digest(file);if(loadedInputs.has(file))assert.equal(loadedInputs.get(file),sha256,'Bundled updater input changed between consumers: '+file);loadedInputs.set(file,sha256);}};
const load=async(file)=>{const result=await build({entryPoints:[path.join(root,file)],bundle:true,write:false,metafile:true,platform:'node',format:'esm',target:'es2022'});bindInputs(result);return import('data:text/javascript;base64,'+Buffer.from(result.outputFiles[0].contents).toString('base64'));};
const {createDeckyUpdaterService}=await load('src/bridge/deckyUpdaterService.ts'),{DeckyUpdaterClient}=await load('decky-plugin/src/deckyUpdaterClient.ts'),{validUpdaterView}=await load('decky-plugin/src/deckyUpdaterProtocol.ts');
const hash='a'.repeat(64),peer={connection:'1',runId:'b'.repeat(32)},clientId='aabbccdd-1122-3344-5566-778899aabbcc',rows=[];
const tick=()=>new Promise(r=>setImmediate(r)),deferred=()=>{let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return {promise,resolve,reject};};
// Same production class; only its clock is virtual in these existing cases.
const clockedUpdaterBundle=await build({entryPoints:[path.resolve(root,process.env.YMCC_DECKY_UPDATER_CLIENT_SOURCE_OVERRIDE||'decky-plugin/src/deckyUpdaterClient.ts')],bundle:true,write:false,metafile:true,platform:'node',format:'cjs',target:'es2022'});bindInputs(clockedUpdaterBundle);
function updaterClock(){
 let now=0,next=0;const timers=new Map(),module={exports:{}};
 const schedule=(action,delay)=>{const id=++next;timers.set(id,{action,delay,due:now+delay});return id;},cancel=id=>timers.delete(id);
 vm.runInNewContext(clockedUpdaterBundle.outputFiles[0].text,{module,exports:module.exports,setTimeout:schedule,clearTimeout:cancel},{filename:'production-decky-updater-client.js'});
 const advance=ms=>{const end=now+ms;for(let steps=0;steps<10000;steps++){const due=[...timers].filter(([,t])=>t.due<=end).sort((a,b)=>a[1].due-b[1].due)[0];if(!due){now=end;return;}const[id,t]=due;now=t.due;timers.delete(id);t.action();}throw Error('updater timer storm');};
 return {Client:module.exports.DeckyUpdaterClient,timers,advance,schedule,cancel,now:()=>now};
}
const recoveryMeasurements=[],ownershipObservations=[],ownershipFacts=[],omitted=[];
async function check(name,fn){if(onlyCase!==null&&name!==onlyCase||name===excludeCase){omitted.push(name);return;}try{await fn();rows.push({name,status:'passed'});}catch(e){rows.push({name,status:'failed',error:e.stack});console.error(name,e);}}
function fixture(){const data={state:{phase:'idle',message:'等待检查',percent:0},target:null},calls=[],messages=[];let ready=true,game=false,power=true,observed=()=>{},observerCount=0;const actions={check:async()=>{calls.push('check');data.target={version:'0.0.36',sha256:hash};data.state={phase:'available',message:'发现新版本'};observed();},download:async()=>{calls.push('download');data.state={phase:'downloaded',version:'0.0.36',sha256:hash,message:'更新包已校验',percent:100};observed();return true;},install:async()=>{calls.push('install');data.state.phase='installing';observed();},validateInstall:async()=>{calls.push('validate');}};const service=createDeckyUpdaterService({version:'0.0.35',read:()=>data,ready:()=>ready,gameRunning:()=>game,powerReady:()=>power,check:()=>actions.check(),download:()=>actions.download(),install:()=>actions.install(),validateInstall:()=>actions.validateInstall(),observe:fn=>{observed=fn;observerCount++;return()=>{observerCount--;observed=()=>{};};},reply:async(c,m)=>{messages.push({connection:c,...m});return true;}});service.connected(peer);const request=(id,action,extra={})=>service.request({...peer,request:{command:'updater.request',runId:peer.runId,clientId,id,action,...extra}});return {service,data,actions,calls,messages,request,notify:()=>observed(),setReady:x=>ready=x,setGame:x=>{game=x;observed();},setPower:x=>{power=x;observed();},observers:()=>observerCount,subscribe(){service.request({...peer,request:{command:'updater.status',runId:peer.runId}});}};}
await check('status is read-only and does not implicitly check or install',async()=>{const h=fixture();h.subscribe();assert.deepEqual(h.calls,[]);assert.equal(h.observers(),1);assert(validUpdaterView(h.messages.at(-1).view));h.service.dispose();assert.equal(h.observers(),0);});
await check('check -> download -> confirmed install reaches the original owner with the exact target',async()=>{const h=fixture();h.subscribe();h.request(1,'check');await tick();assert.deepEqual(h.calls,['check']);assert.equal(h.service.view().canDownload,true);h.request(2,'download',{version:'0.0.36',sha256:hash});await tick();assert.equal(h.service.view().canInstall,true);h.request(3,'install',{version:'0.0.36',sha256:hash});await tick();assert.deepEqual(h.calls,['check','download','validate','install']);assert.equal(h.service.view().phase,'installing');h.service.dispose();});
await check('unverified package cannot enable installation',async()=>{const h=fixture();h.data.target={version:'0.0.36',sha256:hash};let requestId=0;for(const state of [{phase:'available'},{phase:'downloaded'},{phase:'downloaded',version:'0.0.36',sha256:'b'.repeat(64)},{phase:'downloaded',version:'0.0.37',sha256:hash}]){h.data.state=state;assert.equal(h.service.view().canInstall,false);h.request(++requestId,'install',{version:'0.0.36',sha256:hash});await tick();}assert(!h.calls.includes('install'));h.service.dispose();});
await check('current or older manifest versions never enable a downgrade',()=>{const h=fixture();for(const version of ['0.0.35','0.0.34']){h.data.target={version,sha256:hash};h.data.state={phase:'available'};assert.equal(h.service.view().targetVersion,null);assert.equal(h.service.view().canDownload,false);}h.service.dispose();});
await check('duplicate install packet is acknowledged without a second install',async()=>{const h=fixture();h.data.target={version:'0.0.36',sha256:hash};h.data.state={phase:'downloaded',version:'0.0.36',sha256:hash};const hold=deferred();h.actions.install=async()=>{h.calls.push('install');await hold.promise;};h.request(1,'install',{version:'0.0.36',sha256:hash});await tick();h.request(1,'install',{version:'0.0.36',sha256:hash});await tick();assert.equal(h.calls.filter(x=>x==='install').length,1);assert.equal(h.messages.filter(m=>m.type==='ymcc-updater-result'&&m.ok).length,2);hold.resolve();await tick();h.service.dispose();});
await check('conflicting packet IDs cannot replace the accepted package',async()=>{const h=fixture();h.request(1,'check');await tick();h.request(1,'install',{version:'0.0.36',sha256:hash});await tick();assert.equal(h.messages.at(-1).code,'conflict');assert.deepEqual(h.calls,['check']);h.service.dispose();});
await check('old trimmed packet IDs remain rejected',async()=>{const h=fixture();for(let n=1;n<=35;n++){h.request(n,'not-an-action');await tick();}h.request(1,'check');await tick();assert.equal(h.messages.at(-1).code,'stale');assert(!h.calls.length);h.service.dispose();});
await check('wrong run and wrong peer never reach the update owner',async()=>{const h=fixture();for(const foreign of [{...peer,connection:'2'},{...peer,runId:'c'.repeat(32)}])h.service.request({...foreign,request:{command:'updater.request',runId:foreign.runId,clientId,id:1,action:'check'}});await tick();assert(!h.calls.length);h.service.dispose();});
await check('install is blocked while a game runs or power is not ready',async()=>{for(const mode of ['game','power','ready']){const h=fixture();h.data.target={version:'0.0.36',sha256:hash};h.data.state={phase:'downloaded',version:'0.0.36',sha256:hash};if(mode==='game')h.setGame(true);if(mode==='power')h.setPower(false);if(mode==='ready')h.setReady(false);h.request(1,'install',{version:'0.0.36',sha256:hash});await tick();assert(!h.calls.includes('install'));h.service.dispose();}});
await check('power/game is checked again immediately before installation',async()=>{const h=fixture(),gate=deferred();h.data.target={version:'0.0.36',sha256:hash};h.data.state={phase:'downloaded',version:'0.0.36',sha256:hash};h.actions.validateInstall=()=>gate.promise;h.request(1,'install',{version:'0.0.36',sha256:hash});await tick();h.setGame(true);gate.resolve();await tick();assert(!h.calls.includes('install'));assert(h.service.view().message.includes('退出游戏'));h.service.dispose();});
await check('package replacement during install admission requires a fresh confirmation',async()=>{const h=fixture(),gate=deferred();h.data.target={version:'0.0.36',sha256:hash};h.data.state={phase:'downloaded',version:'0.0.36',sha256:hash};h.actions.validateInstall=()=>gate.promise;h.request(1,'install',{version:'0.0.36',sha256:hash});await tick();h.data.target={version:'0.0.37',sha256:'c'.repeat(64)};gate.resolve();await tick();assert(!h.calls.includes('install'));assert(h.service.view().message.includes('更新包已改变'));h.service.dispose();});
await check('synchronous validation failure is handled before an IPC acknowledgement finishes',async()=>{const h=fixture();h.subscribe();h.data.target={version:'0.0.36',sha256:hash};h.data.state={phase:'downloaded',version:'0.0.36',sha256:hash};h.actions.validateInstall=async()=>{throw Error('read failure');};h.request(1,'install',{version:'0.0.36',sha256:hash});await tick();assert(!h.calls.includes('install'));assert.equal(h.service.view().message,'read failure');h.service.dispose();});
await check('disconnect after acceptance stops an unstarted install',async()=>{const h=fixture(),gate=deferred();h.data.target={version:'0.0.36',sha256:hash};h.data.state={phase:'downloaded',version:'0.0.36',sha256:hash};h.actions.validateInstall=()=>gate.promise;h.request(1,'install',{version:'0.0.36',sha256:hash});await tick();h.service.connected({...peer,connected:false});gate.resolve();await tick();assert(!h.calls.includes('install'));h.service.dispose();});
function portFixture({connected=true,supported=true,respond=true}={}){const listeners=new Set(),extras=new Set(),sent=[],demand=new Set();let state={connected};const port={setPassiveDemand:(k,on)=>on?demand.add(k):demand.delete(k),subscribe(fn){listeners.add(fn);fn(state);return()=>listeners.delete(fn);},subscribeExtra(fn){extras.add(fn);return()=>extras.delete(fn);},updaterSupported:()=>supported,updaterRunId:()=>peer.runId,sendUpdater(command,data){if(!demand.has('updater'))return false;sent.push({command,...data});return respond;}};return {port,sent,demand,emit:m=>{for(const fn of extras)fn(m);},connect:x=>{state={connected:x};for(const fn of listeners)fn(state);},count:()=>listeners.size+extras.size};}
const view=(extra={})=>({protocol:1,revision:1,hostVersion:'0.0.35',phase:'available',targetVersion:'0.0.36',targetHash:hash,percent:0,canCheck:true,canDownload:true,canInstall:false,gameRunning:false,message:'发现新版本',...extra});
await check('an already connected mirror sends its status after enabling the updater consumer',()=>{const h=portFixture(),c=new DeckyUpdaterClient(h.port,clientId);c.open();assert.equal(h.sent[0].command,'updater.status');c.close();assert.equal(h.count(),0);assert.equal(h.demand.size,0);
 const closingClock=updaterClock(),closingPort=portFixture(),closing=new closingClock.Client(closingPort.port,clientId,100);
 closing.open();const retiredClose=[...closingClock.timers.values()][0].action;closing.close();const closedState=closing.snapshot();
 assert.equal(closingClock.timers.size,0);retiredClose();assert.equal(closing.snapshot(),closedState,'Closed updater page cannot be changed by retired read callbacks');
 assert.equal(closingPort.count(),0);assert.equal(closingPort.demand.size,0);
 const clock=updaterClock(),q=portFixture({respond:false}),next=new clock.Client(q.port,clientId,100);
 try{next.open();assert.equal(clock.timers.size,0);assert(next.snapshot().error.includes('无法连接'));assert.equal(q.sent.length,1);}
 finally{next.close();assert.equal(clock.timers.size,0);assert.equal(q.count(),0);assert.equal(q.demand.size,0);}
});
await check('old native protocol is explicitly unavailable and never sends update commands',()=>{const h=portFixture({supported:false}),c=new DeckyUpdaterClient(h.port,clientId);c.open();assert.equal(h.sent.length,0);assert.equal(c.snapshot().supported,false);assert(c.snapshot().error.includes('新版'));c.close();});
await check('unsupported or stale views cannot enable install',()=>{const h=portFixture(),c=new DeckyUpdaterClient(h.port,clientId);c.open();h.emit({type:'ymcc-updater',view:view({revision:3})});h.emit({type:'ymcc-updater',view:view({revision:2,canInstall:true})});assert.equal(c.snapshot().view.revision,3);h.emit({type:'ymcc-updater',view:view({protocol:2})});assert.equal(c.snapshot().view,null);c.close();});
await check('client duplicate activation cannot send two mutations',async()=>{const h=portFixture(),c=new DeckyUpdaterClient(h.port,clientId);c.open();h.emit({type:'ymcc-updater',view:view()});const p=c.request('download',view());await assert.rejects(c.request('download',view()));assert.equal(h.sent.filter(x=>x.command==='updater.request').length,1);h.emit({type:'ymcc-updater-result',clientId,id:1,ok:true,message:'accepted'});await p;c.close();});
await check('a different client cannot acknowledge this request',async()=>{const h=portFixture(),c=new DeckyUpdaterClient(h.port,clientId);c.open();h.emit({type:'ymcc-updater',view:view()});const p=c.request('check');h.emit({type:'ymcc-updater-result',clientId:'other',id:1,ok:true,message:'accepted'});assert.equal(c.snapshot().pending,true);h.emit({type:'ymcc-updater-result',clientId,id:1,ok:true,message:'accepted'});await p;c.close();});
await check('timeout requests fresh status but never replays installation',async()=>{const h=portFixture(),c=new DeckyUpdaterClient(h.port,clientId,15);c.open();const v=view({phase:'downloaded',canDownload:false,canInstall:true});h.emit({type:'ymcc-updater',view:v});await assert.rejects(c.request('install',v),/不自动重放/);assert.equal(h.sent.filter(x=>x.command==='updater.request').length,1);assert.equal(c.snapshot().view,null);c.close();});
await check('stale confirmation binds both package version and hash',async()=>{const h=portFixture(),c=new DeckyUpdaterClient(h.port,clientId);c.open();const v=view({phase:'downloaded',canDownload:false,canInstall:true});h.emit({type:'ymcc-updater',view:v});await assert.rejects(c.request('install',{...v,targetHash:'c'.repeat(64)}),/更新包已改变/);assert.equal(h.sent.filter(x=>x.command==='updater.request').length,0);c.close();});
await check('missing host response finishes waiting without an idle poll',async()=>{const h=portFixture(),c=new DeckyUpdaterClient(h.port,clientId,15);c.open();await new Promise(r=>setTimeout(r,25));assert(c.snapshot().error.includes('未响应'));assert.equal(h.sent.length,1);c.close();
 const clock=updaterClock(),q=portFixture(),next=new clock.Client(q.port,clientId,100);
 try{
  next.open();const originalDeadline=[...clock.timers.values()][0].action;
  for(let n=0;n<99;n++){clock.advance(1);q.emit({type:'ymcc-updater-ready',protocol:1});}
  clock.advance(1);
  assert(next.snapshot().error.includes('未响应'),'Ready storms must not postpone the original missing-state deadline');
  assert.equal(q.sent.filter(x=>x.command==='updater.status').length,1,'An unanswered status read must coalesce repeated Ready signals');
  assert.equal(clock.timers.size,0);const sent=q.sent.length;clock.advance(86400000);assert.equal(q.sent.length,sent,'No idle recovery polling');
  next.close();originalDeadline();assert.equal(clock.timers.size,0);assert.equal(q.count(),0);assert.equal(q.demand.size,0);
  recoveryMeasurements.push({route:'ready-storm-no-response',signals:99,originalDeadlineMs:100,statusRequests:sent,remainingTimers:clock.timers.size});
 }finally{next.close();}
});
await check('disconnect rejects the outstanding request and requires a fresh status',async()=>{const h=portFixture(),c=new DeckyUpdaterClient(h.port,clientId);c.open();h.emit({type:'ymcc-updater',view:view()});const p=c.request('check');h.connect(false);await assert.rejects(p,/连接已断开/);assert.equal(c.snapshot().view,null);h.connect(true);assert.equal(h.sent.filter(x=>x.command==='updater.request').length,1);c.close();});
await check('restored YMCC renderer requests status again without replaying the interrupted operation',async()=>{const h=portFixture(),c=new DeckyUpdaterClient(h.port,clientId);c.open();h.emit({type:'ymcc-updater',view:view()});const p=c.request('check');h.emit({type:'ymcc-updater-ready',protocol:1});await assert.rejects(p,/已恢复/);assert.equal(h.sent.filter(x=>x.command==='updater.request').length,1);assert.equal(h.sent.at(-1).command,'updater.status');h.emit({type:'ymcc-updater',view:view({revision:1})});assert(c.snapshot().view);c.close();
 const clock=updaterClock(),q=portFixture(),next=new clock.Client(q.port,clientId,100);
 try{
  next.open();const retired=[...clock.timers.values()][0].action;q.emit({type:'ymcc-updater',view:view()});
  const operation=next.request('check').then(()=>({ok:true}),error=>({ok:false,error}));
  q.emit({type:'ymcc-updater-ready',protocol:1});assert.equal((await operation).ok,false);
  const live=[...clock.timers.values()][0];assert(live);retired();
  assert.equal(clock.timers.size,1);assert(!next.snapshot().error,'A retired initial-read timeout cannot overwrite a newer recovery wait');
  q.emit({type:'ymcc-updater',view:view({revision:3})});const acknowledged=next.snapshot().view;
  live.action();assert.equal(next.snapshot().view,acknowledged,'A cancelled recovered-read deadline cannot erase the current valid view');
  assert.equal(q.sent.filter(x=>x.command==='updater.request').length,1,'Ready recovery cannot replay an interrupted update action');
  const fresh=next.request('check'),request=q.sent.at(-1);q.emit({type:'ymcc-updater-result',clientId:request.clientId,id:request.id,ok:true,message:'accepted'});await fresh;
  recoveryMeasurements.push({route:'late-read-deadlines',staleCallbacks:2,unconfirmedReplayed:0,freshIntentAcknowledged:true});
 }finally{next.close();}
 assert.equal(clock.timers.size,0);assert.equal(q.count(),0);assert.equal(q.demand.size,0);
});

await check('twenty settings remounts reuse one bounded client identity with increasing receipts',async()=>{const h=portFixture();for(let n=1;n<=20;n++){const candidate=('00000000-0000-4000-8000-'+String(n).padStart(12,'0')),c=new DeckyUpdaterClient(h.port,candidate);c.open();h.emit({type:'ymcc-updater',view:view()});const p=c.request('check'),request=h.sent.at(-1);assert.equal(request.id,n);assert.equal(request.clientId,'00000000-0000-4000-8000-000000000001');h.emit({type:'ymcc-updater-result',clientId:request.clientId,id:request.id,ok:true,message:'accepted'});await p;c.close();}assert.equal(new Set(h.sent.filter(x=>x.command==='updater.request').map(x=>x.clientId)).size,1);assert.equal(h.count(),0);assert.equal(h.demand.size,0);});
await check('a receipt from the prior page cannot acknowledge a new page operation',async()=>{const h=portFixture(),old=new DeckyUpdaterClient(h.port,clientId);old.open();h.emit({type:'ymcc-updater',view:view()});const p=old.request('check');old.close();await assert.rejects(p);const next=new DeckyUpdaterClient(h.port,'ffeeddcc-bbaa-9988-7766-554433221100');next.open();h.emit({type:'ymcc-updater',view:view()});const operation=next.request('check');h.emit({type:'ymcc-updater-result',clientId,id:1,ok:true,message:'old accepted'});assert(next.snapshot().pending);h.emit({type:'ymcc-updater-result',clientId,id:2,ok:true,message:'new accepted'});await operation;next.close();});
await check('replacement page owns demand and old teardown cannot disable it',async()=>{
 if(ownerVariant==='mirror-close'||ownerVariant===null){
  const {MirrorClient}=await load('decky-plugin/src/mirrorClient.ts');
  for(const route of ['replacement','same-instance']){
   const clock=updaterClock(),sockets=[],boot={endpoint:'ws://127.0.0.1:12345/mirror',token:'OWNED-MEMORY-CLOSE',runId:peer.runId,updaterProtocol:1};
   let failClose=false,armed=false,once=false,next=null,operation=null,fresh=null;
   class Socket{
    listeners=new Map();sent=[];closed=false;readyState=0;
    constructor(url){this.url=url;sockets.push(this);}
    addEventListener(name,callback){this.listeners.set(name,callback);}
    send(text){const packet=JSON.parse(text);if(failClose&&packet.command==='updater.status'&&packet.subscribe===false){failClose=false;throw Error('controlled socket close-subscription send failure');}this.sent.push(packet);}
    close(){if(this.closed)return;this.closed=true;this.readyState=3;this.listeners.get('close')?.({});}
    emit(name,value){if(name==='open')this.readyState=1;this.listeners.get(name)?.(name==='message'?{data:JSON.stringify(value)}:{});}
   }
   const mirror=new MirrorClient({bootstrap:()=>boot,socket:url=>new Socket(url),schedule:clock.schedule,cancel:clock.cancel,now:clock.now,clientId:'close-owner-fixture'});
   const prior=new clock.Client(mirror,clientId,100),replacement=route==='same-instance'?prior:new clock.Client(mirror,'ffeeddcc-bbaa-9988-7766-554433221100',100);
   const faults=[],expect=(okay,message)=>{if(!okay)faults.push(message);};
   const snapshot=revision=>({runId:boot.runId,generation:1,revision,ready:true,game:null,fan:{enabled:false,supported:false,preset:'off',choices:[]},provenance:{kind:'ymcc-native',pid:7}});
   let stop=()=>{};
   try{
    mirror.setVisible(true);const old=sockets.at(-1);old.emit('open');old.emit('message',{type:'snapshot',runId:boot.runId,snapshot:snapshot(1)});
    mirror.setPassiveDemand('connection',true);prior.open();old.emit('message',{type:'ymcc-updater',runId:boot.runId,view:view()});
    assert(prior.snapshot().view,'before close must have current real mirror updater view');
    operation=prior.request('check').then(()=>({ok:true}),error=>({ok:false,message:error.message}));
    assert.equal(old.sent.filter(p=>p.command==='updater.request').length,1,'old page request must actually dispatch before it is closed');
    stop=mirror.subscribe(state=>{if(armed&&!once&&!state.connected){once=true;next=replacement;next.open();}},{connectionOnly:true});
    armed=true;failClose=true;prior.close();const oldResult=await operation;
    expect(once,'actual MirrorClient disconnect must synchronously reach the replacement observer');
    expect(oldResult.ok===false,'old dispatched request stays unconfirmed');
    const retainedDemand=mirror.passiveConsumers.has('updater');
    expect(retainedDemand,'old close must preserve replacement updater passive demand');
    clock.advance(250);const current=sockets.at(-1);
    expect(current!==old,'existing mirror retry must create a replacement socket');
    if(current!==old){
     current.emit('open');current.emit('message',{type:'snapshot',runId:boot.runId,snapshot:snapshot(2)});
     current.emit('message',{type:'ymcc-updater',runId:boot.runId,view:view({revision:2})});
    }
    expect(!!next?.snapshot().view,'replacement updater must receive current status through actual MirrorClient');
    let freshAcknowledged=false;
    if(next?.snapshot().view){
     fresh=next.request('check').then(()=>({ok:true}),error=>({ok:false,message:error.message}));
     const request=current.sent.filter(p=>p.command==='updater.request').at(-1);
     if(request){expect(request.id===2,'new explicit intent keeps original monotonic receipt sequence');current.emit('message',{type:'ymcc-updater-result',runId:boot.runId,clientId:request.clientId,id:request.id,ok:true,message:'accepted'});freshAcknowledged=(await fresh).ok;}
    }
    expect(freshAcknowledged,'new explicit intent must be admitted and acknowledged');
    expect(sockets.flatMap(s=>s.sent).filter(p=>p.command==='updater.request').length===(freshAcknowledged?2:1),'old uncertain request cannot be replayed');
    ownershipFacts.push({edge:'actual-mirror-close-'+route,controlledSendFailures:1,oldRequestActuallySent:1,oldResultUnconfirmed:!oldResult.ok,replacementDemandRetained:retainedDemand,replacementViewReceived:!!next?.snapshot().view,freshExplicitIntentAcknowledged:freshAcknowledged,faults});
   }catch(error){faults.push(String(error.stack??error));}
   finally{stop();prior.close();if(replacement!==prior)replacement.close();mirror.dispose();if(operation)await operation;if(fresh)await fresh;expect(clock.timers.size===0,'all owned timers must return after disposal');expect(sockets.every(s=>s.closed),'all controlled sockets must close');}
   ownershipObservations.push({name:'actual-mirror-close-'+route,status:faults.length?'failed':'passed',...(faults.length?{error:faults.join('; ')}:{})});
  }
  assert.equal(ownershipObservations.length,2);
  assert(ownershipObservations.every(x=>x.status==='passed'),'actual mirror close controls failed: '+ownershipObservations.filter(x=>x.status!=='passed').map(x=>x.name+': '+x.error).join(' | '));
  if(ownerVariant==='mirror-close')return;
 }
 if(ownerVariant==='open-handoff'||ownerVariant===null){
  const {MirrorClient}=await load('decky-plugin/src/mirrorClient.ts');
  for(const route of ['third-page','prior-page-reopen']){
   const clock=updaterClock(),sockets=[],boot={endpoint:'ws://127.0.0.1:12345/mirror',token:'OWNED-NESTED-OPEN',runId:peer.runId,updaterProtocol:1};
   class Socket{listeners=new Map();sent=[];closed=false;readyState=0;constructor(url){this.url=url;sockets.push(this);}addEventListener(k,fn){this.listeners.set(k,fn);}send(text){this.sent.push(JSON.parse(text));}close(){if(this.closed)return;this.closed=true;this.readyState=3;this.listeners.get('close')?.({});}emit(k,v){if(k==='open')this.readyState=1;this.listeners.get(k)?.(k==='message'?{data:JSON.stringify(v)}:{});}}
   const mirror=new MirrorClient({bootstrap:()=>boot,socket:url=>new Socket(url),schedule:clock.schedule,cancel:clock.cancel,now:clock.now,clientId:'nested-open-fixture'});
   const prior=new clock.Client(mirror,clientId,100),outer=new clock.Client(mirror,'00112233-4455-6677-8899-aabbccddeeff',100),latest=route==='third-page'?new clock.Client(mirror,'ffeeddcc-bbaa-9988-7766-554433221100',100):prior;
   const faults=[],expect=(okay,message)=>{if(!okay)faults.push(message);};let armed=false,once=false,oldOperation=null,fresh=null;let off=()=>{};
   try{
    mirror.setVisible(true);const socket=sockets.at(-1);socket.emit('open');socket.emit('message',{type:'snapshot',runId:boot.runId,snapshot:{runId:boot.runId,generation:1,revision:1,ready:true,game:null,fan:{enabled:false,supported:false,preset:'off',choices:[]},provenance:{kind:'ymcc-native',pid:7}}});mirror.setPassiveDemand('connection',true);
    prior.open();socket.emit('message',{type:'ymcc-updater',runId:boot.runId,view:view()});assert(prior.snapshot().view);
    oldOperation=prior.request('check').then(()=>({ok:true}),error=>({ok:false,message:error.message}));assert.equal(socket.sent.filter(p=>p.command==='updater.request').length,1);
    off=prior.subscribe(()=>{if(armed&&!once&&!prior.snapshot().connected){once=true;latest.open();}});armed=true;
    outer.open();expect(once,'prior close observer must open the latest page');
    expect((await oldOperation).ok===false,'old accepted request must remain unconfirmed');
    const listeners={extra:mirror.extraListeners.size,connection:mirror.connectionListeners.size};
    expect(listeners.extra===1&&listeners.connection===1,'one latest page alone must own mirror listeners');
    expect(outer.opened===null,'outer stale open must retire after a newer opening');
    const statusReads=socket.sent.filter(p=>p.command==='updater.status'&&p.subscribe===true).length;
    expect(statusReads===2,'old initial plus latest page status only; retired outer cannot read');
    socket.emit('message',{type:'ymcc-updater',runId:boot.runId,view:view({revision:2})});
    expect(!!latest.snapshot().view,'latest page must receive status');const before=socket.sent.filter(p=>p.command==='updater.request').length;
    fresh=latest.request('check').then(()=>({ok:true}),error=>({ok:false,message:error.message}));const request=socket.sent.filter(p=>p.command==='updater.request').at(-1);
    if(socket.sent.filter(p=>p.command==='updater.request').length===before+1){expect(request.id===2,'new request must use the original monotonic identity');socket.emit('message',{type:'ymcc-updater-result',runId:boot.runId,clientId:request.clientId,id:request.id,ok:true,message:'accepted'});}else clock.advance(100);
    const freshAccepted=(await fresh).ok;expect(freshAccepted,'latest page explicit request must be acknowledged');
    outer.close();if(prior!==latest)prior.close();expect(mirror.passiveConsumers.has('updater'),'retired outer/old cleanup must preserve latest demand');
    expect(socket.sent.filter(p=>p.command==='updater.request').length===2,'only old and new explicit requests, without replay');
    ownershipFacts.push({edge:'nested-open-'+route,oldRequestActuallySent:1,oldUnconfirmed:true,latestExplicitRequestAcknowledged:freshAccepted,listenerCounts:listeners,subscribedStatusReads:statusReads,staleOuterRetired:outer.opened===null,onlyOneControlledSocket:sockets.length===1,faults});
   }catch(error){faults.push(String(error.stack??error));}
   finally{off();prior.close();outer.close();if(latest!==prior)latest.close();mirror.dispose();if(oldOperation)await oldOperation;if(fresh)await fresh;expect(clock.timers.size===0,'all nested-open timers must return');expect(sockets.every(s=>s.closed),'all controlled sockets must return');}
   ownershipObservations.push({name:'nested-open-'+route,status:faults.length?'failed':'passed',...(faults.length?{error:faults.join('; ')}:{})});
  }
  if(ownerVariant==='open-handoff'){assert.equal(ownershipObservations.length,2);assert(ownershipObservations.every(x=>x.status==='passed'),'nested open owner controls failed: '+ownershipObservations.filter(x=>x.status!=='passed').map(x=>x.name+': '+x.error).join(' | '));return;}
 }
const h=portFixture(),old=new DeckyUpdaterClient(h.port,clientId),next=new DeckyUpdaterClient(h.port,'ffeeddcc-bbaa-9988-7766-554433221100');old.open();next.open();old.close();assert(h.demand.has('updater'));assert.equal(h.count(),2);h.emit({type:'ymcc-updater',view:view()});assert(next.snapshot().view);next.close();assert.equal(h.count(),0);
 const clock=updaterClock(),q=portFixture(),prior=new clock.Client(q.port,clientId),replacement=new clock.Client(q.port,'ffeeddcc-bbaa-9988-7766-554433221100');
 prior.open();prior.subscribe(()=>{throw Error('inert failed UI subscriber');});
 try{
  assert.doesNotThrow(()=>replacement.open(),'An old page observer exception cannot prevent ownership handoff');
  prior.close();assert.equal(q.count(),2);assert(q.demand.has('updater'));
  q.emit({type:'ymcc-updater',view:view()});assert(replacement.snapshot().view);
 }finally{prior.close();replacement.close();}
 assert.equal(q.count(),0);assert.equal(q.demand.size,0);assert.equal(clock.timers.size,0);
 const verify=async(name,fn)=>{try{await fn();ownershipObservations.push({name,status:'passed'});}catch(error){ownershipObservations.push({name,status:'failed',error:String(error.stack||error)});}};
 if(ownerVariant===null){
 for(const mode of ['close','replace'])await verify('subscribe-return-'+mode,async()=>{
  const clock=updaterClock(),h=portFixture(),c=new clock.Client(h.port,clientId,100);let replacement=null,armed=true;
  c.subscribe(()=>{if(armed&&c.snapshot().connected){armed=false;if(mode==='close')c.close();else{replacement=new clock.Client(h.port,'ffeeddcc-bbaa-9988-7766-554433221100',100);replacement.open();}}});
  try{c.open();assert.equal(h.count(),mode==='close'?0:2,'retired subscribe return must release only original connection listener');assert.equal(h.demand.has('updater'),mode==='replace');if(replacement){h.emit({type:'ymcc-updater',view:view()});assert(replacement.snapshot().view);}ownershipFacts.push({edge:'subscribe-return-'+mode,listenerCount:h.count(),timerCount:clock.timers.size,replacementActive:!!replacement});}
  finally{c.close();replacement?.close();}assert.equal(h.count(),0);assert.equal(clock.timers.size,0);assert.equal(h.demand.size,0);
 });
 for(const mode of ['disconnect','ready','replace'])await verify('pending-publication-'+mode,async()=>{
  const clock=updaterClock(),h=portFixture(),c=new clock.Client(h.port,clientId,100);let replacement=null,armed=true;c.open();h.emit({type:'ymcc-updater',view:view()});
  c.subscribe(()=>{if(armed&&c.snapshot().pending){armed=false;if(mode==='disconnect')h.connect(false);else if(mode==='ready')h.emit({type:'ymcc-updater-ready',protocol:1});else{replacement=new clock.Client(h.port,'ffeeddcc-bbaa-9988-7766-554433221100',100);replacement.open();}}});
  try{const operation=c.request('check').then(()=>({ok:true}),error=>({ok:false,error}));assert.equal((await operation).ok,false);assert.equal(h.sent.filter(x=>x.command==='updater.request').length,0,'retired unstarted operation must never reach transport');if(replacement){h.emit({type:'ymcc-updater',view:view()});const p=replacement.request('check');const request=h.sent.at(-1);assert.equal(request.id,2);h.emit({type:'ymcc-updater-result',clientId:request.clientId,id:request.id,ok:true,message:'accepted'});await p;}ownershipFacts.push({edge:'pending-publication-'+mode,retiredRequests:0,replacementRequestAcknowledged:!!replacement});}
  finally{c.close();replacement?.close();}assert.equal(h.count(),0);assert.equal(clock.timers.size,0);assert.equal(h.demand.size,0);
 });
 await verify('timeout-publication-replacement',async()=>{
  const clock=updaterClock(),h=portFixture(),c=new clock.Client(h.port,clientId,100);let replacement=null,armed=true;c.open();h.emit({type:'ymcc-updater',view:view()});
  c.subscribe(()=>{if(armed&&!c.snapshot().pending&&c.snapshot().error.includes('更新请求未确认')){armed=false;replacement=new clock.Client(h.port,'ffeeddcc-bbaa-9988-7766-554433221100',100);replacement.open();}});
  try{const p=c.request('check').then(()=>({ok:true}),error=>({ok:false,error}));const reads=h.sent.filter(x=>x.command==='updater.status'&&x.subscribe===true).length;clock.advance(100);assert.equal((await p).ok,false);assert.equal(h.sent.filter(x=>x.command==='updater.status'&&x.subscribe===true).length-reads,1,'only replacement page may request its initial state');assert.equal(h.sent.filter(x=>x.command==='updater.request').length,1);assert.equal(h.count(),2);ownershipFacts.push({edge:'timeout-publication-replacement',newStatusRequests:1,acceptedRequests:1,listenerCount:h.count()});}
  finally{c.close();replacement?.close();}assert.equal(h.count(),0);assert.equal(clock.timers.size,0);assert.equal(h.demand.size,0);
 });
 await verify('healthy-dispatch-and-same-owner-timeout',async()=>{
  const clock=updaterClock(),h=portFixture(),c=new clock.Client(h.port,clientId,100);c.open();h.emit({type:'ymcc-updater',view:view()});
  try{const first=c.request('download',view());const request=h.sent.at(-1);h.emit({type:'ymcc-updater-result',clientId:request.clientId,id:request.id,ok:true,message:'accepted'});await first;const second=c.request('check').then(()=>({ok:true}),error=>({ok:false,error}));const reads=h.sent.filter(x=>x.command==='updater.status'&&x.subscribe===true).length;clock.advance(100);assert.equal((await second).ok,false);assert.equal(h.sent.filter(x=>x.command==='updater.request').length,2);assert.equal(h.sent.filter(x=>x.command==='updater.status'&&x.subscribe===true).length-reads,1,'same owner still refreshes actual status after uncertainty');ownershipFacts.push({edge:'healthy-dispatch-and-same-owner-timeout',acceptedRequests:2,timeoutStatusRefreshes:1});}
  finally{c.close();}assert.equal(h.count(),0);assert.equal(clock.timers.size,0);assert.equal(h.demand.size,0);
 });
 }
 await verify('same-instance-reopen-during-subscribe',async()=>{
  const clock=updaterClock(),h=portFixture(),c=new clock.Client(h.port,clientId,100);let armed=true;c.subscribe(()=>{if(armed&&c.snapshot().connected){armed=false;c.close();c.open();}});
  try{c.open();assert.equal(h.count(),2,'same instance reopen must retain only new extra and new connection listeners');assert(h.demand.has('updater'));h.emit({type:'ymcc-updater',view:view()});assert(c.snapshot().view);ownershipFacts.push({edge:'same-instance-reopen-during-subscribe',listenerCount:h.count(),timerCount:clock.timers.size});}
  finally{c.close();}assert.equal(h.count(),0);assert.equal(clock.timers.size,0);assert.equal(h.demand.size,0);
 });
 await verify('same-instance-reopen-after-timeout',async()=>{
  const clock=updaterClock(),h=portFixture(),c=new clock.Client(h.port,clientId,100);let armed=true;c.open();h.emit({type:'ymcc-updater',view:view()});c.subscribe(()=>{if(armed&&!c.snapshot().pending&&c.snapshot().error.includes('更新请求未确认')){armed=false;c.close();c.open();}});
  try{const p=c.request('check').then(()=>({ok:true}),error=>({ok:false,error}));const reads=h.sent.filter(x=>x.command==='updater.status'&&x.subscribe===true).length;clock.advance(100);assert.equal((await p).ok,false);assert.equal(h.sent.filter(x=>x.command==='updater.status'&&x.subscribe===true).length-reads,1,'old opening timeout must not subscribe using the new opening');assert.equal(h.count(),2);ownershipFacts.push({edge:'same-instance-reopen-after-timeout',newStatusRequests:1,acceptedRequests:1,listenerCount:h.count()});}
  finally{c.close();}assert.equal(h.count(),0);assert.equal(clock.timers.size,0);assert.equal(h.demand.size,0);
 });
 assert.equal(ownershipObservations.length,ownerVariant==='reopen-only'?2:13);assert(ownershipObservations.every(x=>x.status==='passed'),'updater ownership controls failed: '+ownershipObservations.filter(x=>x.status!=='passed').map(x=>x.name).join(','));
});

const sourceInputs=[...loadedInputs].map(([file,sha256])=>({path:file,sha256})),sourceDrift=sourceInputs.filter(row=>digest(row.path)!==row.sha256);
const report={onlyCase,excludeCase,ownerVariant,omitted,ownershipObservations,ownershipFacts,status:rows.every(x=>x.status==='passed')&&sourceDrift.length===0?'passed':'failed',sourceInputs,sourceDrift,cases:rows.length,results:rows,productionService:true,productionClient:true,recoveryMeasurements,liveNativeInstallation:false,steamUIActions:0};fs.writeFileSync(path.join(out,'UPDATER-SERVICE.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({status:report.status,cases:rows.length,failed:rows.filter(x=>x.status==='failed').map(x=>x.name)}));if(report.status!=='passed')process.exitCode=1;
