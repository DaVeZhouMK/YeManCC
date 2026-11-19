import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';import{webcrypto,createHash}from'node:crypto';import{build}from'esbuild';
const root=process.cwd(),selected=process.argv.includes('--session-config'),caseAt=process.argv.indexOf('--case-filter'),caseFilter=caseAt<0?null:process.argv[caseAt+1],outRoot=path.resolve(process.env.YMCC_CE_VALIDATION_ROOT||path.resolve(root,'../../Build/Validation/CeEngineBridge-selftest'));if(selected&&!process.env.YMCC_CE_VALIDATION_ROOT)throw Error('SessionConfig selection requires an explicit owned Evidence root');fs.mkdirSync(outRoot,{recursive:true});
const sourceOverride=process.env.YMCC_VALUE_SCAN_SOURCE;if(sourceOverride&&(!selected||!path.resolve(sourceOverride).startsWith(path.dirname(outRoot)+path.sep)))throw Error('Source override must be private Evidence');
const pin=p=>({path:path.resolve(p),sha256:createHash('sha256').update(fs.readFileSync(p)).digest('hex')}),inputPaths=[sourceOverride||'src/bridge/gameValueScan.ts','src/bridge/trainerLifecycle.ts','src/bridge/powerLifecycleState.ts','src/bridge/api.ts','tools/game_value_scan_bridge_selftest.mjs','PowerControl/MemoryScan/MemoryScanHost.cs','PowerControl/MemoryScan/CeScanEngine.lua','node_modules/esbuild/lib/main.js','node_modules/@esbuild/win32-x64/esbuild.exe'],inputs=inputPaths.map(pin),started=performance.now();
const producerPath=process.env.YMCC_VALUE_SCAN_HOST_STATUS;if(producerPath){inputPaths.push(producerPath);inputs.push(pin(producerPath));}
const mocks={
'frontendDiagnostics.ts':`export function reportFrontendError(where,error){(globalThis.fx.diagnostics??=[]).push({where,message:error.message});}`,
'api.ts':`const f=globalThis.fx;export const powerLifecycle={get:async()=>{if(f.powerHook)await f.powerHook();return {...f.power};}};const appCall=async(n,v)=>{if(f.appHook)await f.appHook(n);return v};export const app={powerControlDir:async()=>appCall('powerControlDir',f.pc||'R:/PowerControl'),dataDir:async()=>appCall('dataDir',f.data||'R:/Data'),pid:async()=>appCall('pid',55),exeDir:async()=>appCall('exeDir','R:/YMCC')};export const fs={exists:async p=>f.exists(p),mkdir:async p=>f.mkdir(p),readTextFile:async(p,max)=>{if(f.readHook){const value=await f.readHook(p);if(value!==undefined)return value;}if(!f.files.has(p))throw Error('missing');return f.files.get(p)},writeTextFileAtomic:async(p,s)=>f.write(p,s),remove:async p=>f.files.delete(p)};export const proc={identity:async()=>({valid:true,pid:55,path:'R:/YMCC/YeManCC.exe',processCreated:'134365380584475500'})};export const shell={hidden:async(p,a,t,r)=>f.launch(p,a,r)};`,
'gameQuickSession.ts':`export const getLockedGameTarget=()=>globalThis.fx.target;export const validateLockedGameTarget=async()=>{if(globalThis.fx.validateHook)await globalThis.fx.validateHook();return globalThis.fx.target};`,
'settingsRepository.ts':`export const getSettingsGeneration=()=>globalThis.fx.generation;`,
'ipc.ts':`export const invoke=async command=>{if(command!=='power.lifecycle')throw Error('Unexpected shared IPC '+command);return {...globalThis.fx.power};};export const on=(name,listener)=>{const f=globalThis.fx,key='ipc:'+name,cb=e=>listener(e.detail);if(!f.events.has(key))f.events.set(key,new Set());f.events.get(key).add(cb);return()=>f.events.get(key)?.delete(cb);};`,
};
const bundle=await build({entryPoints:['src/bridge/gameValueScan.ts'],bundle:true,format:'cjs',platform:'node',write:false,plugins:[{name:'scan-boundaries',setup(b){if(sourceOverride)b.onLoad({filter:/[\\/]gameValueScan\.ts$/},()=>({contents:fs.readFileSync(sourceOverride,'utf8'),loader:'ts'}));b.onLoad({filter:/[\\/](api|ipc|gameQuickSession|settingsRepository|frontendDiagnostics)\.ts$/},a=>({contents:mocks[path.basename(a.path)],loader:'ts'}))}}]});
function fixture({disk=false}={}){
 const fx={now:1792070000000,files:new Map(),generation:0,target:{pid:1234,processCreated:'134365380584475513',path:'R:/Game/Game.exe',lockGeneration:1,lockReason:'action'},intervals:new Map(),nextTimer:0,launches:[],requests:[],events:new Map(),power:{phase:'ready',generation:1,hardwareWritesAllowed:true,inputReady:true},state:null,root:'',config:null,validateHook:null,receiptHook:null,launchHook:null};
 if(disk){const own=path.join(outRoot,'owned-'+webcrypto.randomUUID()),normal=p=>path.resolve(p.replaceAll('\\',path.sep));fs.mkdirSync(own,{recursive:true});const check=p=>{const n=normal(p);if(!n.startsWith(own+path.sep))throw Error('File IO outside owned Evidence: '+n);return n;};fx.pc=path.join(own,'PowerControl');fx.data=path.join(own,'Data');fx.diskRoot=own;fx.diskIO={reads:0,writes:0};fx.files={has:p=>fs.existsSync(check(p)),get:p=>{fx.diskIO.reads++;return new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(fs.readFileSync(check(p)));},set:(p,v)=>{fx.diskIO.writes++;const n=check(p);fs.mkdirSync(path.dirname(n),{recursive:true});const temp=n+'.'+webcrypto.randomUUID()+'.tmp';fs.writeFileSync(temp,v);try{fs.renameSync(temp,n);}finally{if(fs.existsSync(temp))fs.unlinkSync(temp);}},delete:p=>{const n=check(p);if(fs.existsSync(n))fs.unlinkSync(n);return true;}};fx.mkdir=p=>{fs.mkdirSync(check(p),{recursive:true});return true;};fx.files.set(fx.pc+'\\MemoryScan\\MemoryScanHost.exe','INERT marker - never executed');}
 fx.mkdir||=()=>true;fx.exists=p=>p.endsWith('MemoryScanHost.exe')?!fx.missingWorker:fx.files.has(p);
 const state=()=>({...fx.state,rows:fx.state.rows.map(r=>({...r}))});
 fx.write=(p,s)=>{if(fx.writeHook)fx.writeHook(p,s);fx.files.set(p,s);if(p.endsWith('config.json'))fx.config=JSON.parse(s);if(p.endsWith('\\stop')&&fx.config&&!fx.deferClosed){const receipt={...fx.config,engine:'cheat-engine',stage:'closed',ceExited:true,at:fx.now};if(fx.closedHook)fx.closedHook(receipt);fx.files.set(p.slice(0,-4)+'closed.json',JSON.stringify(receipt));}if(p.includes('\\requests\\')){const q=JSON.parse(s);fx.requests.push(q);fx.state={...fx.state,at:fx.now};if(q.action==='first')fx.state={...fx.state,stage:'ready',revision:fx.state.revision+1,count:1,rows:[{address:'0000000000001000',value:q.value}],dataType:q.dataType||'int32'};const r={id:q.id,token:q.token,ok:true,error:'',state:state()};if(fx.receiptHook)fx.receiptHook(r);fx.files.set(p.replace('\\requests\\','\\responses\\'),fx.rawReceiptHook?fx.rawReceiptHook(r):JSON.stringify(r));fx.files.set(fx.root+'\\status.json',JSON.stringify(fx.state));}return true};
 fx.launch=async(p,a,creationReceipt)=>{if(a[0]==='--release'){(fx.releases??=[]).push({program:p,args:a});const config=JSON.parse(fx.files.get(a[1]+'\\config.json'));const result={...config,engine:'cheat-engine',released:fx.releaseAvailable===true,message:'exit not yet confirmed',at:fx.now};if(fx.releaseHook)await fx.releaseHook(result);if(result.released===true&&!fx.omitReleasedClosed)fx.files.set(a[1]+'\\closed.json',JSON.stringify({...config,engine:'cheat-engine',stage:'closed',ceExited:true,at:fx.now}));fx.files.set(a[2],JSON.stringify(result));return{ok:true,pid:4001};}if(a[0]==='--probe'){fx.probes=(fx.probes||0)+1;if(fx.probeHook)await fx.probeHook();if(fx.probeError)throw Error(fx.probeError);fx.files.set(a[1],JSON.stringify(fx.probeReceipt||{engine:'cheat-engine',available:fx.ceAvailable!==false}));return{ok:true,pid:4000}}fx.root=a[0];(fx.dispatches??=[]).push({program:p,args:a,creationReceipt});if(fx.beforeLaunchHook){const result=await fx.beforeLaunchHook();if(result!==undefined)return result;}fx.launches.push({program:p,args:a});fx.state={...fx.config,engine:'cheat-engine',at:fx.now,stage:'idle',message:'ready',revision:0,dataType:'int32',count:0,offset:0,pageSize:40,rows:[],selected:'',selectedValue:null,frozen:false,frozenValue:null,scannedBytes:0,skippedBlocks:0,bulkLimit:32,scanRound:0,searchValue:'',lastWriteCount:0,lastWriteValue:'',temporarilyPaused:false,pauseRestored:true};if(disk&&producerPath){const produced=JSON.parse(fs.readFileSync(producerPath,'utf8'));fx.state={...produced,...fx.config,at:fx.now};}if(fx.launchHook)fx.launchHook();if(!fx.deferStatus)fx.files.set(fx.root+'\\status.json',JSON.stringify(fx.state));return{ok:true,pid:4567}};
 class Clock extends Date{static now(){return fx.now}}
 const module={exports:{}};vm.runInNewContext(bundle.outputFiles[0].text,{module,exports:module.exports,fx,crypto:webcrypto,console,TextEncoder,TextDecoder,Date:Clock,queueMicrotask,window:{addEventListener:(name,cb)=>{if(!fx.events.has(name))fx.events.set(name,new Set());fx.events.get(name).add(cb)},removeEventListener:(name,cb)=>fx.events.get(name)?.delete(cb)},setTimeout:(fn,ms)=>setImmediate(()=>{fx.now+=ms;fn()}),clearTimeout:clearImmediate,setInterval:fn=>{const id=++fx.nextTimer;fx.intervals.set(id,fn);return id},clearInterval:id=>fx.intervals.delete(id)});
 fx.emit=(type,generation)=>{for(const fn of [...fx.events.get(type)||[]])fn({type,detail:{generation}});};fx.tick=async ms=>{fx.now+=ms;for(const fn of [...fx.intervals.values()])fn();for(let i=0;i<100;i++)await Promise.resolve()};return{fx,scan:module.exports};
}
const checks=[],skipped=[];async function check(name,body){if((selected&&!name.startsWith('session-config:'))||(caseFilter&&!name.includes(caseFilter))){skipped.push(name);return;}try{await body();checks.push({name,ok:true});console.log('PASS '+name)}catch(error){checks.push({name,ok:false,error:error.message});console.log('FAIL '+name+': '+error.stack)}}
await check('diagnostics: CE absence and a failed CE handshake preserve distinct causes without session capabilities',async()=>{
 const missing=fixture();missing.fx.ceAvailable=false;
 await assert.rejects(missing.scan.startGameValueSession(missing.fx.target),/CE/);
 assert.equal(missing.fx.launches.length,0);const discovery=missing.fx.diagnostics.map(x=>JSON.parse(x.message));
 assert(discovery.some(x=>x.phase==='discovery'&&x.code==='ce-not-found'));
 const failed=fixture();failed.fx.launchHook=()=>{failed.fx.state.stage='closed';failed.fx.state.message='owned CE exited: '+failed.fx.config.token;};
 await assert.rejects(failed.scan.startGameValueSession(failed.fx.target),/owned CE exited/);
 assert.equal(failed.fx.launches.length,1);const startup=failed.fx.diagnostics.find(x=>x.where==='game-value-scan.start');
 assert.equal(JSON.parse(startup.message).phase,'ce-handshake');assert(startup.message.includes('owned CE exited'));assert(!startup.message.includes(failed.fx.config.token));
 assert.equal(failed.fx.intervals.size,0);assert(failed.fx.files.has(failed.fx.root+'\\stop'));
});
await check('lifecycle: closing while CE detection waits cannot launch a helper later',async()=>{
 const{fx,scan}=fixture();let release;fx.probeHook=()=>new Promise(resolve=>release=resolve);
 const pending=scan.startGameValueSession(fx.target),rejected=assert.rejects(pending,/取消|改变|结束/);
 for(let i=0;i<100;i++)await Promise.resolve();assert(release);await scan.stopGameValueSession();release();await rejected;assert.equal(fx.launches.length,0);assert.equal(fx.intervals.size,0);
});
await check('lifecycle: relocking the same process retires the old startup authority',async()=>{
 const{fx,scan}=fixture();let release;fx.probeHook=()=>new Promise(resolve=>release=resolve);
 const pending=scan.startGameValueSession({...fx.target}),rejected=assert.rejects(pending,/取消|改变|结束/);
 for(let i=0;i<100;i++)await Promise.resolve();assert(release);fx.target={...fx.target,lockGeneration:2};release();await rejected;assert.equal(fx.launches.length,0);
 const active=fixture(),oldTarget={...active.fx.target},oldDraft={firstValue:'5000',nextValue:'4500',desiredValue:'999999'};
 await active.scan.startGameValueSession(oldTarget);active.scan.saveGameValueDraft(oldTarget,oldDraft);assert.deepEqual({...active.scan.getGameValueDraft(oldTarget)},oldDraft);
 active.fx.target={...oldTarget,lockGeneration:2};const newTarget={...active.fx.target};
 assert.equal(active.scan.getGameValueDraft(newTarget).firstValue,'','A new lock inherited the previous lock draft before its old helper retired');
 await active.scan.startGameValueSession(newTarget);const newDraft={firstValue:'8000',nextValue:'7000',desiredValue:'99999'};active.scan.saveGameValueDraft(newTarget,newDraft);
 active.scan.saveGameValueDraft(oldTarget,oldDraft);assert.deepEqual({...active.scan.getGameValueDraft(newTarget)},newDraft,'A delayed old component overwrote the newly locked session draft');
 assert.equal(active.scan.getGameValueDraft(oldTarget).firstValue,'','Old lock read the new lock draft');
 active.fx.generation++;assert.equal(active.scan.getGameValueDraft(newTarget).firstValue,'','A settings generation inherited the old session draft');await active.scan.stopGameValueSession();

});
await check('lifecycle: power transition cancels a pending startup without replay on wake',async()=>{
 const{fx,scan}=fixture();let release;fx.probeHook=()=>new Promise(resolve=>release=resolve);
 const pending=scan.startGameValueSession(fx.target),rejected=assert.rejects(pending,/取消|改变|结束/);
 for(let i=0;i<160;i++)await Promise.resolve();assert(release);fx.power={...fx.power,generation:2,phase:'suspending'};fx.emit('ipc:power.suspending',2);release();await rejected;
 fx.power.phase='ready';fx.emit('ipc:power.resumed',2);for(let i=0;i<100;i++)await Promise.resolve();assert.equal(fx.launches.length,0);fx.probeHook=null;
 assert(await scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,1);await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);assert([...fx.events.values()].every(s=>s.size===0));
});
await check('lifecycle: native nonready and lost wake generation prevent scans and writes',async()=>{
 const a=fixture();a.fx.power.phase='resuming';await assert.rejects(a.scan.startGameValueSession(a.fx.target),/电源/);assert.equal(a.fx.launches.length,0);assert.equal(a.fx.probes,undefined);
 const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.power.generation=2;await assert.rejects(scan.gameValueCommand('writeAll',{value:'99999'}),/电源/);assert.equal(fx.requests.length,0);assert.equal(fx.intervals.size,0);assert.equal(scan.getGameValueState(),null);
});
await check('lifecycle: queued commands are retired on sleep and never replayed for the next game',async()=>{
 const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);let release;fx.validateHook=()=>new Promise(resolve=>release=resolve);
 const first=scan.gameValueCommand('first',{value:'5000'}),write=scan.gameValueCommand('writeAll',{value:'99999'});
 const r1=assert.rejects(first,/改变|取消/),r2=assert.rejects(write,/改变|取消/);for(let i=0;i<100;i++)await Promise.resolve();assert(release);
 fx.power.generation=2;fx.emit('ipc:power.suspending',2);release();await Promise.all([r1,r2]);assert.equal(fx.requests.length,0);assert.equal(fx.intervals.size,0);
 fx.target={...fx.target,pid:2345,processCreated:'134365380584475514',lockGeneration:2,path:'R:/Next/Next.exe'};fx.validateHook=null;fx.emit('ipc:power.resumed',2);
 for(let i=0;i<100;i++)await Promise.resolve();assert.equal(fx.launches.length,1);assert(await scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,2);assert.equal(fx.requests.length,0);await scan.stopGameValueSession();
});
await check('lifecycle: a missing or mismatched CE release blocks replacement until exact confirmation',async()=>{
 // Restart captures the same authoritative game/settings/power context before
 // retiring the old owner. A later wake never turns the old click into a new one.
 {
  const {fx,scan}=fixture();const initial=await scan.startGameValueSession(fx.target);
  assert.equal((await scan.startGameValueSession(fx.target)).token,initial.token);assert.equal(fx.launches.length,1);
  const replacement=await scan.startGameValueSession(fx.target,{restart:true});assert.notEqual(replacement.token,initial.token);assert.equal(fx.launches.length,2);await scan.stopGameValueSession();
 }
 for(const transition of ['page-close','power-cycle','silent-generation','webview','next-game','settings']){
  const {fx,scan}=fixture();await scan.startGameValueSession(fx.target);let release;
  fx.readHook=p=>p.endsWith('\\closed.json')?new Promise(resolve=>{release=()=>resolve(fx.files.get(p))}):undefined;
  const restarting=scan.startGameValueSession(fx.target,{restart:true});const rejected=assert.rejects(restarting,/改变|取消|电源|未就绪/);
  assert.equal(scan.startGameValueSession(fx.target,{restart:true}),restarting,'Same restart should share its one request');
  for(let i=0;i<180&&!release;i++)await Promise.resolve();assert(release,transition+': restart did not retire old owner');
  let closing;
  if(transition==='page-close')closing=scan.stopGameValueSession().catch(()=>{});
  if(transition==='power-cycle'){fx.power.generation=2;fx.emit('ipc:power.suspending',2);fx.emit('ipc:power.resumed',2);}
  if(transition==='silent-generation')fx.power.generation=2;
  if(transition==='webview')fx.emit('ipc:webview.recovering',77);
  if(transition==='next-game')fx.target={...fx.target,pid:2345,processCreated:'134365380584475514',lockGeneration:2};
  if(transition==='settings')fx.generation++;
  fx.readHook=null;release();await rejected;if(closing)await closing;
  assert.equal(fx.launches.length,1,transition);assert.equal(fx.requests.length,0);assert.equal(scan.getGameValueState(),null);assert.equal(fx.intervals.size,0);
  assert(await scan.startGameValueSession(fx.target),'A fresh explicit click must recover after '+transition);assert.equal(fx.launches.length,2);await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);
 }

 for(const fault of ['not-exited','token','missing']){
  const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);const oldRoot=fx.root;
  if(fault==='missing')fx.deferClosed=true;else fx.closedHook=r=>{if(fault==='token')r.token='wrong';else r.ceExited=false;};
  await assert.rejects(scan.stopGameValueSession(),/退出|确认/);await assert.rejects(scan.startGameValueSession(fx.target),/退出|确认/);assert.equal(fx.launches.length,1,fault);
  fx.deferClosed=false;fx.closedHook=null;fx.files.set(oldRoot+'\\closed.json',JSON.stringify({...fx.config,engine:'cheat-engine',stage:'closed',ceExited:true,at:fx.now}));
  assert(await scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,2);await scan.stopGameValueSession();
 }
 for(const transition of ['same-target','next-game','sleep-resume']){
  const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);const oldRoot=fx.root,oldToken=fx.config.token;
  fx.deferClosed=true;await assert.rejects(scan.stopGameValueSession(),/退出|确认/);fx.releaseAvailable=true;
  if(transition==='next-game')fx.target={...fx.target,pid:2345,processCreated:'134365380584475514',lockGeneration:2};
  if(transition==='sleep-resume')fx.power={...fx.power,generation:2};
  assert(await scan.startGameValueSession(fx.target),'Explicit reopen could not reach authoritative release recovery after '+transition);
  assert.equal(fx.releases.length,1);assert.equal(fx.releases[0].args[0],'--release');assert.equal(fx.releases[0].args[1],oldRoot);
  assert.equal(fx.launches.length,2);assert.equal(fx.requests.length,0);assert.equal(JSON.parse(fx.files.get(oldRoot+'\\closed.json')).token,oldToken);
  assert.notEqual(scan.getGameValueState().token,oldToken);fx.deferClosed=false;await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);
 }
 for(const fault of ['token','target','released-type','missing-closed']){
  const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.deferClosed=true;await assert.rejects(scan.stopGameValueSession());
  fx.releaseAvailable=true;fx.omitReleasedClosed=fault==='missing-closed';fx.releaseHook=result=>{if(fault==='token')result.token='wrong';if(fault==='target')result.pid++;if(fault==='released-type')result.released='true';};
  await assert.rejects(scan.startGameValueSession(fx.target),/退出|核验|确认/);assert.equal(fx.launches.length,1);assert.equal(scan.getGameValueState(),null);
  fx.releaseHook=null;fx.omitReleasedClosed=false;fx.deferClosed=false;await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);
 }
 const interrupted=fixture();await interrupted.scan.startGameValueSession(interrupted.fx.target);interrupted.fx.deferClosed=true;await assert.rejects(interrupted.scan.stopGameValueSession());
 interrupted.fx.releaseAvailable=true;let release;interrupted.fx.releaseHook=()=>new Promise(resolve=>release=resolve);
 const reopening=interrupted.scan.startGameValueSession(interrupted.fx.target),rejected=assert.rejects(reopening,/取消|改变/);
 for(let i=0;i<180&&!release;i++)await new Promise(setImmediate);assert(release);
 const closing=interrupted.scan.stopGameValueSession().catch(()=>{});release();await rejected;await closing;
 assert.equal(interrupted.fx.launches.length,1);assert.equal(interrupted.fx.requests.length,0);assert.equal(interrupted.scan.getGameValueState(),null);
 interrupted.fx.releaseHook=null;interrupted.fx.deferClosed=false;assert(await interrupted.scan.startGameValueSession(interrupted.fx.target));await interrupted.scan.stopGameValueSession();assert.equal(interrupted.fx.intervals.size,0);
});
await check('lifecycle: dispatch refusal permits an explicit retry without a nonexistent CE release',async()=>{
 const{fx,scan}=fixture();fx.deferClosed=true;fx.beforeLaunchHook=()=>({ok:false,creationReceipt:1,processCreated:false,errorCode:'CREATE_PROCESS_FAILED',win32Error:5});
 await assert.rejects(scan.startGameValueSession(fx.target),/无法启动/);assert.equal(fx.launches.length,0);assert.equal(fx.dispatches.length,1);assert(![...fx.files.keys()].some(p=>p.endsWith('closed.json')));
 fx.beforeLaunchHook=null;assert(await scan.startGameValueSession(fx.target));fx.deferClosed=false;assert.equal(fx.dispatches.length,2);assert.equal(fx.dispatches[0].creationReceipt,true);assert.equal(fx.launches.length,1);await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);
});
await check('lifecycle: dispatch uncertainty never authorizes a second helper',async()=>{
 for(const fault of ['lost-ack','legacy-false','bad-version','bad-created','bad-error']){
  const{fx,scan}=fixture();fx.deferClosed=true;fx.beforeLaunchHook=()=>{if(fault==='lost-ack')throw Error('native acknowledgement lost');const r={ok:false,creationReceipt:1,processCreated:false,errorCode:'CREATE_PROCESS_FAILED',win32Error:5};if(fault==='legacy-false')return{ok:false};if(fault==='bad-version')r.creationReceipt=2;if(fault==='bad-created')r.processCreated='false';if(fault==='bad-error')r.win32Error='5';return r;};
  await assert.rejects(scan.startGameValueSession(fx.target));await assert.rejects(scan.startGameValueSession(fx.target),/退出|确认/);assert.equal(fx.dispatches.length,1,fault);assert.equal(fx.launches.length,0);assert.equal(scan.getGameValueState(),null);
  fx.files.set(fx.root+'\\closed.json',JSON.stringify({...fx.config,engine:'cheat-engine',stage:'closed',ceExited:true,at:fx.now}));fx.beforeLaunchHook=null;fx.deferClosed=false;assert(await scan.startGameValueSession(fx.target));await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);
 }
});
await check('lifecycle: dispatch cancelled while pending cannot revive a closed page',async()=>{
 const{fx,scan}=fixture();fx.deferClosed=true;let release;fx.beforeLaunchHook=()=>new Promise(resolve=>release=resolve);
 const pending=scan.startGameValueSession(fx.target),rejected=assert.rejects(pending,/无法启动|取消|改变/);for(let i=0;i<180;i++)await Promise.resolve();assert(release);
 const stopped=scan.stopGameValueSession();release({ok:false,creationReceipt:1,processCreated:false,errorCode:'CREATE_PROCESS_FAILED',win32Error:5});await Promise.all([stopped,rejected]);
 assert.equal(fx.dispatches.length,1);assert.equal(fx.launches.length,0);assert.equal(fx.intervals.size,0);assert.equal(scan.getGameValueState(),null);assert([...fx.events.values()].every(s=>s.size===0));
 fx.beforeLaunchHook=null;fx.deferClosed=false;fx.target={...fx.target,lockGeneration:2};assert(await scan.startGameValueSession(fx.target));await scan.stopGameValueSession();
});
await check('lifecycle: late successful dispatch cannot reopen the page or skip exact release',async()=>{
 const{fx,scan}=fixture();fx.deferClosed=true;let release;fx.beforeLaunchHook=()=>new Promise(resolve=>release=resolve);
 const pending=scan.startGameValueSession(fx.target),rejected=assert.rejects(pending,/取消|改变/);for(let i=0;i<180;i++)await Promise.resolve();assert(release);
 const stopped=scan.stopGameValueSession(),stopRejected=assert.rejects(stopped,/退出|确认/);release();await rejected;await stopRejected;assert.equal(fx.launches.length,1);assert.equal(fx.intervals.size,0);assert.equal(scan.getGameValueState(),null);
 await assert.rejects(scan.startGameValueSession(fx.target),/退出|确认/);assert.equal(fx.launches.length,1);
 fx.files.set(fx.root+'\\closed.json',JSON.stringify({...fx.config,engine:'cheat-engine',stage:'closed',ceExited:true,at:fx.now}));fx.beforeLaunchHook=null;fx.deferClosed=false;fx.target={...fx.target,pid:2345,processCreated:'134365380584475514',lockGeneration:2};assert(await scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,2);await scan.stopGameValueSession();assert([...fx.events.values()].every(s=>s.size===0));
});
await check('lifecycle: WebView recovery generations cannot share native power ordering',async()=>{
 for(const webviewGeneration of [1,1000]){
  const{fx,scan}=fixture();fx.power.generation=100;await scan.startGameValueSession(fx.target);const root=fx.root;
  fx.emit('ipc:webview.recovering',webviewGeneration);for(let i=0;i<180;i++)await Promise.resolve();
  assert.equal(scan.getGameValueState(),null,'WebView recovery must retire current CE at its own generation '+webviewGeneration);assert.equal(fx.intervals.size,0);assert.equal(fx.files.get(root+'\\stop'),'stop');assert.equal(fx.requests.length,0);
  fx.target={...fx.target,lockGeneration:2};assert(await scan.startGameValueSession(fx.target),'WebView generation cannot poison a fresh native power admission');assert.equal(fx.launches.length,2);assert.equal(fx.requests.length,0);await scan.stopGameValueSession();assert([...fx.events.values()].every(s=>s.size===0));
 }
});
await check('lifecycle: an old power event cannot close a newly admitted session',async()=>{
 const{fx,scan}=fixture();fx.power.generation=3;await scan.startGameValueSession(fx.target);const token=scan.getGameValueState().token;
 fx.emit('ipc:power.suspending',2);fx.emit('ipc:power.resumed',2);for(let i=0;i<100;i++)await Promise.resolve();assert.equal(scan.getGameValueState().token,token);assert.equal(fx.intervals.size,1);await scan.stopGameValueSession();
});
await check('lifecycle: a long renderer gap never renews the old CE lease',async()=>{
 const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);const before=fx.files.get(fx.root+'\\lease.json');await fx.tick(9000);
 assert.equal(fx.files.get(fx.root+'\\lease.json'),before);assert.equal(scan.getGameValueState().stage,'closed');assert.equal(fx.intervals.size,0);await scan.stopGameValueSession();
});
await check('missing locked game is rejected before creating a helper',async()=>{const{fx,scan}=fixture(),target=fx.target;fx.target=null;await assert.rejects(scan.startGameValueSession(target),/锁定游戏/);assert.equal(fx.launches.length,0)});
await check('concurrent openings reuse one helper and exact game identity',async()=>{const{fx,scan}=fixture();const[a,b]=await Promise.all([scan.startGameValueSession(fx.target),scan.startGameValueSession({...fx.target})]);assert.equal(a.pid,b.pid);assert.equal(fx.launches.length,1);assert.equal(fx.launches[0].args.length,1);await scan.stopGameValueSession();assert.equal(fx.intervals.size,0)});
await check('owner identity uses the native PID and executable directory without querying self through process.identity',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);assert.equal(fx.config.ownerPid,55);assert.equal(fx.config.ownerDirectory,'R:/YMCC');assert.equal(fx.config.ownerCreated,undefined);assert.equal(fx.config.ownerPath,undefined);await scan.stopGameValueSession()});
await check('first scan keeps input values as strings and publishes only authenticated results',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);const r=await scan.gameValueCommand('first',{value:'99999',dataType:'int32'});assert.equal(fx.requests[0].value,'99999');assert.equal(r.rows[0].value,'99999');r.rows[0].address='wrong';assert.notEqual(scan.getGameValueState().rows[0].address,'wrong');await scan.stopGameValueSession()});
await check('wrong response token cannot be accepted by the UI',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.receiptHook=r=>r.token='wrong';await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}),/回执不匹配/);assert.equal(scan.getGameValueState().stage,'idle');await scan.stopGameValueSession()});
await check('source switch during validation creates no queued write',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.validateHook=()=>{fx.generation++};await assert.rejects(scan.gameValueCommand('write',{value:'9999'}),/锁定游戏/);assert.equal(fx.requests.length,0);await scan.stopGameValueSession()});
await check('another PID or creation time never inherits an active scan session',async()=>{for(const field of['pid','processCreated']){const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.target={...fx.target,[field]:field==='pid'?2345:'134365380584475514'};await assert.rejects(scan.gameValueCommand('write',{value:'9999'}),/锁定游戏/);assert.equal(fx.requests.length,0);await fx.tick(2000);assert.equal(fx.intervals.size,0);assert.equal(fx.files.get(fx.root+'\\stop'),'stop')}});
await check('stale helper status stops freeze indication and requests bounded session shutdown',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.state.frozen=true;fx.files.set(fx.root+'\\status.json',JSON.stringify(fx.state));await fx.tick(10000);const r=scan.getGameValueState();assert.equal(r.stage,'closed');assert.equal(r.frozen,false);assert.equal(fx.files.get(fx.root+'\\stop'),'stop');assert.equal(fx.intervals.size,0);await scan.stopGameValueSession()});
await check('cancel writes one session marker without starting a replacement helper',async()=>{const{fx,scan}=fixture();await scan.startGameValueSession(fx.target);await scan.cancelGameValueScan();assert.equal(fx.files.get(fx.root+'\\cancel'),'cancel');assert.equal(fx.launches.length,1);await scan.stopGameValueSession()});
await check('missing CE removes capability and cannot start any scanner',async()=>{
 const {fx,scan}=fixture();fx.ceAvailable=false;assert.equal(await scan.isGameValueScanAvailable(),false);await assert.rejects(scan.startGameValueSession(fx.target),/未安装CE/);assert.equal(fx.launches.length,0);assert.equal(fx.requests.length,0);
});
await check('missing adapter never falls back to the old memory scanner',async()=>{
 const {fx,scan}=fixture();fx.missingWorker=true;assert.equal(await scan.isGameValueScanAvailable(),false);assert.equal(fx.launches.length,0);assert.equal(fx.probes,undefined);
});
await check('availability distinguishes missing adapter and config or permission failures from missing CE',async()=>{
 for(const kind of ['adapter','config','permission','malformed']){
  const {fx,scan}=fixture();
  if(kind==='adapter')fx.missingWorker=true;
  if(kind==='config')fx.probeReceipt={engine:'cheat-engine',available:false,code:'ce-config-unreadable',message:'无法读取CE路径配置：fixture locked'};
  if(kind==='permission')fx.probeError='fixture launch denied';
  if(kind==='malformed')fx.probeReceipt={engine:'cheat-engine',available:'false'};
  const probe=await scan.probeGameValueScanAvailability();assert.equal(probe.available,false);assert.doesNotMatch(probe.message,/未安装CE/);
  await assert.rejects(scan.startGameValueSession(fx.target),e=>!e.message.includes('未安装CE'));assert.equal(fx.launches.length,0);
 }
});
await check('old custom scanner receipts are rejected even with correct process and token',async()=>{
 const {fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.receiptHook=r=>{delete r.state.engine};await assert.rejects(scan.gameValueCommand('first',{value:'5000'}),/无效状态/);await scan.stopGameValueSession();
});

await check('session-config: real files reject malformed acknowledgement and permit explicit retry',async()=>{
 for(const kind of ['ok-string','future','duplicate','empty','truncated','array','oversize']){
  const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);
  if(kind==='ok-string')fx.receiptHook=r=>r.ok='false';
  if(kind==='future')fx.receiptHook=r=>r.schemaVersion=2;
  if(kind==='duplicate')fx.rawReceiptHook=r=>'{"ok":false,'+JSON.stringify(r).slice(1);
  if(['empty','truncated','array','oversize'].includes(kind))fx.rawReceiptHook=()=>({empty:'',truncated:'{',array:'[]',oversize:' '.repeat(131073)})[kind];
  const begin=fx.now;await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}),undefined,kind);assert(fx.now-begin<10000,kind+' was repeatedly treated as missing');
  assert.notEqual(scan.getGameValueState()?.stage,'ready',kind);fx.receiptHook=null;fx.rawReceiptHook=null;
  const good=await scan.gameValueCommand('first',{value:'5001',dataType:'int32'});assert.equal(good.rows[0].value,'5001');assert(fx.diskIO.reads>0&&fx.diskIO.writes>0);await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);
 }
});
await check('session-config: config write readback must precede any inert helper launch',async()=>{
 for(const fault of ['write','readback']){
  const{fx,scan}=fixture({disk:true});if(fault==='write')fx.writeHook=p=>{if(p.endsWith('config.json'))throw Error('owned replace refused');};else fx.readHook=p=>p.endsWith('config.json')?'{}':undefined;
  await assert.rejects(scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,0,fault);assert.equal(scan.getGameValueState(),null);assert.equal(fx.intervals.size,0);
  fx.writeHook=null;fx.readHook=null;assert(await scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,1);await scan.stopGameValueSession();
 }
});
await check('session-config: corrupt status stops timer file storms and an explicit new session recovers',async()=>{
 const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);const bad='{"schemaVersion":2}';fx.files.set(fx.root+'\\status.json',bad);await fx.tick(1000);
 assert.equal(fx.intervals.size,0);assert.equal(scan.getGameValueState()?.stage,'closed');assert.equal(fx.files.get(fx.root+'\\status.json'),bad);
 const reads=fx.diskIO.reads;for(let i=0;i<12;i++)await fx.tick(1000);assert.equal(fx.diskIO.reads,reads);assert(await scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,2);await scan.stopGameValueSession();
 const startup=fixture({disk:true});let releaseStartup,heldStartup=false;
 startup.fx.readHook=p=>{if(!p.endsWith('\\status.json'))return undefined;if(!heldStartup){heldStartup=true;const raw=startup.fx.files.get(p);return new Promise(resolve=>releaseStartup=()=>resolve(raw));}return 'HALF_WRITTEN_STARTUP_STATUS';};
 const starting=startup.scan.startGameValueSession(startup.fx.target);for(let i=0;i<180;i++)await Promise.resolve();assert(releaseStartup);
 await startup.fx.tick(1000);assert.equal(startup.fx.intervals.size,0);assert.equal(startup.fx.files.get(startup.fx.root+'\\stop'),'stop');
 releaseStartup();const startupResult=await Promise.allSettled([starting]);
 try{assert.equal(startupResult[0].status,'rejected','A late startup success revived a failed session before its first state');assert.equal(startup.scan.getGameValueState(),null);assert.equal(startup.fx.launches.length,1);}finally{startup.fx.readHook=null;await startup.scan.stopGameValueSession();}
 for(const terminal of ['corrupt','closed']){
  const item=fixture({disk:true}),{fx,scan}=item;await scan.startGameValueSession(fx.target);
  fx.state={...fx.state,stage:'ready',count:1,rows:[{address:'0000000000001000',value:'5000'}],scanRound:2,searchValue:'5000',lastWriteCount:1,lastWriteValue:'99999',scannedBytes:4096,totalBytes:8192,scanElapsedMs:123,temporarilyPaused:true,pauseRestored:false};
  fx.files.set(fx.root+'\\status.json',JSON.stringify(fx.state));await fx.tick(1000);
  let release,held=false;
  fx.readHook=p=>{if(p.includes('\\responses\\')&&!held){held=true;const raw=fx.files.get(p);return new Promise(resolve=>release=()=>resolve(raw));}return undefined;};
  const pending=scan.gameValueCommand('writeAll',{value:'99999'});for(let i=0;i<180;i++)await Promise.resolve();assert(release,'No exact pending receipt read to delay');
  const queued=scan.gameValueCommand('first',{value:'6000'});
  const closed={...fx.state,stage:'closed',message:'CE ended',count:0,rows:[],scanRound:0,searchValue:'',lastWriteCount:0,lastWriteValue:'',scannedBytes:0,totalBytes:0,scanElapsedMs:0};
  fx.files.set(fx.root+'\\status.json',terminal==='corrupt'?'HALF_WRITTEN_STATUS':JSON.stringify(closed));await fx.tick(1000);const observed=scan.getGameValueState();
  release();const results=await Promise.allSettled([pending,queued]);fx.readHook=null;
  try{
   assert.equal(results[0].status,'rejected',terminal+': a late successful reply revived a terminal session');assert.match(results[0].reason.message,/会话|结束|关闭|重新/);
   assert.equal(results[1].status,'rejected',terminal+': queued command obtained authority after terminal state');assert.equal(fx.requests.length,1,terminal+': terminal session dispatched another memory operation');
   assert.equal(observed.stage,'closed');assert.equal(observed.count,0);assert.equal(observed.rows.length,0);assert.equal(observed.scanRound,0);assert.equal(observed.searchValue,'');assert.equal(observed.lastWriteCount,0);assert.equal(observed.lastWriteValue,'');assert.equal(observed.scannedBytes,0);assert.equal(observed.totalBytes,0);assert.equal(observed.scanElapsedMs,0);
   assert.equal(observed.temporarilyPaused,true);assert.equal(observed.pauseRestored,false,'Frontend close must not invent CE pause cleanup');
   assert.equal(scan.getGameValueState().stage,'closed');assert.equal(fx.intervals.size,0);const reads=fx.diskIO.reads;for(let i=0;i<12;i++)await fx.tick(1000);assert.equal(fx.diskIO.reads,reads);
   assert(await scan.startGameValueSession(fx.target));assert.equal(fx.launches.length,2);assert.equal(scan.getGameValueState().lastWriteCount,0);assert.equal(fx.requests.length,1,'Explicit reopening replayed an old command');
  }finally{await scan.stopGameValueSession();}
 }

});
await check('session-config: unknown identity and stale revision cannot grant request authority',async()=>{
 const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);
 for(const patch of [{pid:'1234'},{message:42},{offset:-1},{pageSize:'40'},{rows:[null]},{frozen:'false'},{schemaVersion:2}]){
  fx.receiptHook=r=>Object.assign(r.state,patch);await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}));
 }
 fx.receiptHook=null;const good=await scan.gameValueCommand('first',{value:'5000',dataType:'int32'});fx.receiptHook=r=>r.state.revision=good.revision-1;await assert.rejects(scan.gameValueCommand('next',{value:'5001',dataType:'int32'}));
 assert.equal(scan.getGameValueState().revision,good.revision);await scan.stopGameValueSession();
});


await check('session-config: late old file completion never stops its replacement session',async()=>{
 const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);const oldRoot=fx.root;let release;
 fx.readHook=p=>p===oldRoot+'\\status.json'?new Promise(resolve=>release=resolve):undefined;
 await fx.tick(1000);assert(release);await scan.stopGameValueSession();assert(await scan.startGameValueSession(fx.target));const token=scan.getGameValueState().token;
 release('{');for(let i=0;i<100;i++)await Promise.resolve();assert.equal(scan.getGameValueState().token,token);assert.notEqual(scan.getGameValueState().stage,'closed');assert.equal(fx.intervals.size,1);await scan.stopGameValueSession();
});


await check('session-config: healthy late startup receipt keeps the original bounded wait',async()=>{
 // Preserve the original healthy-late control below. These inert transport
 // boundaries exercise never-returning native calls without hanging the test.
 const settles=async promise=>{
  let timer;const pending=Symbol('pending');
  try{const result=await Promise.race([promise.then(value=>({value}),error=>({error})),new Promise(resolve=>{timer=setTimeout(()=>resolve(pending),1000)})]);assert.notEqual(result,pending,'CE transport left the action permanently pending');return result;}
  finally{clearTimeout(timer);}
 };
 for(const site of ['powerControlDir','dataDir','pid','exeDir','exists','requests','responses','probe','target']){
  const {fx,scan}=fixture();const never=()=>new Promise(()=>{});let calls=0;
  if(['powerControlDir','dataDir','pid','exeDir'].includes(site))fx.appHook=name=>{if(name===site){calls++;return never()}};
  if(site==='exists'){const original=fx.exists;fx.exists=p=>{if(p.endsWith('MemoryScanHost.exe')){calls++;return never()}return original(p)}};
  if(site==='requests'||site==='responses')fx.mkdir=p=>{if(p.endsWith('\\'+site)){calls++;return never()}return true};
  if(site==='probe')fx.probeHook=()=>{calls++;return never()};
  if(site==='target')fx.validateHook=()=>{calls++;return never()};
  const result=await settles(scan.startGameValueSession(fx.target));assert(result.error,site);assert.match(result.error.message,/超时/);assert(calls>0,site);
  assert.equal(fx.launches.length,0,site);assert.equal(fx.intervals.size,0,site);assert.equal(scan.getGameValueState(),null,site);
  fx.appHook=null;fx.probeHook=null;fx.validateHook=null;fx.exists=p=>p.endsWith('MemoryScanHost.exe')||fx.files.has(p);fx.mkdir=()=>true;
  assert((await settles(scan.startGameValueSession(fx.target))).value,site+' could not explicitly retry');await scan.stopGameValueSession();assert.equal(fx.requests.length,0);
 }
 // A lost creation acknowledgement must time out, retain the old ownership,
 // and ignore a late acknowledgement instead of authorizing a second CE.
 {
  const {fx,scan}=fixture();fx.deferClosed=true;let late;
  fx.beforeLaunchHook=()=>new Promise(resolve=>late=resolve);
  const result=await settles(scan.startGameValueSession(fx.target));assert(result.error);assert.match(result.error.message,/超时/);assert(late);
  assert.equal(scan.getGameValueState(),null);assert.equal(fx.intervals.size,0);assert.equal(fx.dispatches.length,1);
  await assert.rejects(scan.startGameValueSession(fx.target),/退出|确认/);assert.equal(fx.dispatches.length,1);
  late({ok:true,pid:4567});for(let i=0;i<50;i++)await Promise.resolve();assert.equal(scan.getGameValueState(),null);assert.equal(fx.dispatches.length,1);
  fx.files.set(fx.root+'\\closed.json',JSON.stringify({...fx.config,engine:'cheat-engine',stage:'closed',ceExited:true,at:fx.now}));fx.beforeLaunchHook=null;fx.deferClosed=false;
  assert((await settles(scan.startGameValueSession(fx.target))).value);await scan.stopGameValueSession();assert.equal(fx.dispatches.length,2);assert.equal(fx.requests.length,0);
 }
 {
  const {fx,scan}=fixture();await scan.startGameValueSession(fx.target);fx.deferClosed=true;await assert.rejects(scan.stopGameValueSession());
  fx.releaseHook=()=>new Promise(()=>{});const result=await settles(scan.startGameValueSession(fx.target));assert(result.error);assert.match(result.error.message,/超时/);
  assert.equal(fx.launches.length,1);assert.equal(scan.getGameValueState(),null);fx.releaseHook=null;fx.releaseAvailable=true;
  assert((await settles(scan.startGameValueSession(fx.target))).value);fx.deferClosed=false;await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);assert.equal(fx.requests.length,0);
 }

 const{fx,scan}=fixture({disk:true});fx.deferStatus=true;const pending=scan.startGameValueSession(fx.target);for(let i=0;i<200;i++)await Promise.resolve();assert.equal(fx.launches.length,1);
 await fx.tick(1500);assert.equal(fx.intervals.size,1);assert.equal(fx.files.has(fx.root+'\\stop'),false);fx.state.at=fx.now;fx.files.set(fx.root+'\\status.json',JSON.stringify(fx.state));
 assert(await pending);assert.equal(fx.launches.length,1);await scan.stopGameValueSession();assert.equal(fx.intervals.size,0);
});


await check('session-config: parser-boundary UTF8 BOM reaches the actual parser and preserves unknowns',async()=>{
 const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);let sawBOM=false;
 fx.rawReceiptHook=r=>'\uFEFF'+JSON.stringify({...r,state:{...r.state,unknownParserField:{keep:'BOM'}}});
 fx.readHook=p=>{if(!p.includes('\\responses\\'))return undefined;const raw=fx.files.get(p);assert.equal(raw.charCodeAt(0),0xfeff);sawBOM=true;return raw;};
 const good=await scan.gameValueCommand('first',{value:'5000',dataType:'int32'});assert(sawBOM);assert.equal(good.unknownParserField.keep,'BOM');assert.equal(good.stage,'ready');await scan.stopGameValueSession();
});
await check('session-config: parser-boundary escaped duplicate keys reject without granting acknowledgement',async()=>{
 for(const location of ['root','state']){
  const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);let raw;
  fx.rawReceiptHook=r=>raw=location==='root'?'{"o\\u006b":false,'+JSON.stringify(r).slice(1):JSON.stringify(r).replace('"message":','"mess\\u0061ge":"spoof","message":');
  await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}));const response=fx.root+'\\responses\\'+fx.requests.at(-1).id+'.json';assert.equal(fx.files.get(response),raw);assert.equal(scan.getGameValueState().stage,'idle');
  fx.rawReceiptHook=null;assert.equal((await scan.gameValueCommand('first',{value:'5001',dataType:'int32'})).stage,'ready');await scan.stopGameValueSession();
 }
});
await check('session-config: parser-boundary containers at depth16 accept and depth17 reject',async()=>{
 for(const shape of ['array','object'])for(const levels of [15,16]){
  const{fx,scan}=fixture({disk:true});await scan.startGameValueSession(fx.target);let raw;
  fx.rawReceiptHook=r=>{let nested='keep';for(let n=0;n<levels;n++)nested=shape==='array'?[nested]:{next:nested};return raw=JSON.stringify({...r,state:{...r.state,unknownParserField:nested}});};
  if(levels===15){const good=await scan.gameValueCommand('first',{value:'5000',dataType:'int32'});let nested=good.unknownParserField;for(let n=0;n<levels;n++)nested=shape==='array'?nested[0]:nested.next;assert.equal(nested,'keep');}
  else {await assert.rejects(scan.gameValueCommand('first',{value:'5000',dataType:'int32'}));assert.equal(scan.getGameValueState().stage,'idle');assert.equal(fx.files.get(fx.root+'\\responses\\'+fx.requests.at(-1).id+'.json'),raw);fx.rawReceiptHook=null;assert.equal((await scan.gameValueCommand('first',{value:'5001',dataType:'int32'})).stage,'ready');}
  await scan.stopGameValueSession();
 }
});

const report={passed:checks.filter(x=>x.ok).length,failed:checks.filter(x=>!x.ok).length,checks,realGameMemoryWrites:0,boundary:'Production bridge; file/process IPC and time are isolated fixtures.'};report.selected=selected?'session-config':null;report.caseFilter=caseFilter;report.producerArtifact=producerPath?{...pin(producerPath),binding:'Actual published host fields retained; token/PID/birth/path rebound only to newly persisted fixture config, and at to the fixture clock; no actual scan/memory result'}:null;report.skipped=skipped;report.inputs=inputs;report.sourceDrift=inputPaths.map(pin).filter((v,i)=>v.sha256!==inputs[i].sha256);report.elapsedMs=performance.now()-started;report.node={...pin(process.execPath),version:process.version};report.fileBoundary=selected?'Real UTF8 files and atomic replacement only in owned Evidence directories; process/helper/memory ports inert':'Original Map boundary';const out=path.join(outRoot,'bridge-tests.json');fs.mkdirSync(path.dirname(out),{recursive:true});fs.writeFileSync(out,JSON.stringify(report,null,2));console.log(JSON.stringify(report,null,2));if(report.failed)process.exitCode=1;
