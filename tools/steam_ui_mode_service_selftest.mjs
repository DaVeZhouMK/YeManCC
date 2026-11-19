import assert from 'node:assert/strict';import fs from 'node:fs';import vm from 'node:vm';import {transformSync} from 'esbuild';
import {createHash} from 'node:crypto';

async function legacyStopCases(control,output) {
 const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
 const sources=['src/bridge/yeman.ts','src/bridge/steamUi.ts','tools/steam_ui_mode_service_selftest.mjs'];
 const before=new Map(sources.map(file=>[file,fs.readFileSync(file)]));
 const bridge=before.get(sources[0]).toString('utf8');
 const marker='export async function steamStop(',start=bridge.indexOf(marker),end=bridge.indexOf('\nlet steamLaunchInFlight',start);
 assert(start>=0&&end>start&&bridge.indexOf(marker,start+1)<0,'one complete original legacy declaration');
 const declaration=bridge.slice(start,end),legacyCode=transformSync(declaration,{loader:'ts',format:'cjs',supported:{'dynamic-import':false}}).code;
 const serviceCode=transformSync(before.get(sources[1]).toString('utf8'),{loader:'ts',format:'cjs'}).code;
 const desktop={known:true,running:true,mode:'desktop',pid:123,birth:'456',reason:''};
 const fixture=()=>{
  const calls=[],shells=[];let answer={ok:true},failure=null;
  const invoke=async(command,args,options)=>{calls.push({command,args:JSON.parse(JSON.stringify(args)),options:JSON.parse(JSON.stringify(options))});if(failure)throw failure;return answer;};
  const service={exports:{}};
  vm.runInNewContext(serviceCode,{module:service,exports:service.exports,Error,Number,require:name=>{
   if(name==='./ipc')return{invoke,on:()=>{throw Error('unexpected subscription')}};
   if(name==='./yeman')return{steamRunning:()=>{throw Error('unexpected independent running query')}};
   throw Error('unexpected service dependency '+name);
  }});
  const legacy={exports:{}};
  vm.runInNewContext(legacyCode,{module:legacy,exports:legacy.exports,require:name=>{
   assert.equal(name,'./steamUi');return service.exports;
  },shell:{run:async(...args)=>{shells.push(args);return{exitCode:0}}}});
  return{calls,shells,stop:legacy.exports.steamStop,setAnswer(value){answer=value;},setFailure(value){failure=value;}};
 };
 const cases=[];let failure=null;
 const check=async(name,fn)=>{await fn();cases.push(name);};
 try {
  await check('legacy stop delegates the exact displayed client to the shared owner',async()=>{
   const f=fixture();await f.stop(desktop,{userInitiated:true});
   assert.equal(f.calls.length,1,'one shared exact Steam stop outlet');assert.equal(f.shells.length,0,'no name-based process kill');
   assert.deepEqual(f.calls[0],{command:'steam.ui.stop',args:{pid:123,birth:'456',userInitiated:true},options:{timeoutMs:10000}});
  });
  if(!control) {
   await check('legacy stop refuses missing or malformed manual intent before native IPC',async()=>{
    for(const intent of [undefined,null,{}, {userInitiated:false},{userInitiated:'true'},{userInitiated:1}]){
     const f=fixture();await assert.rejects(f.stop(desktop,intent),/手动确认/);assert.equal(f.calls.length,0);assert.equal(f.shells.length,0);
    }
   });
   await check('legacy stop refuses unbound unknown and malformed state before native IPC',async()=>{
    for(const state of [undefined,null,{}, {...desktop,pid:0,birth:'0',mode:'unknown'}, {...desktop,pid:-1}, {...desktop,pid:2**32}, {...desktop,birth:'00'}, {...desktop,known:false,mode:'unknown'}, {...desktop,running:false,mode:'stopped'}]){
     const f=fixture();await assert.rejects(f.stop(state,{userInitiated:true}),/实例身份/);assert.equal(f.calls.length,0);assert.equal(f.shells.length,0);
    }
   });
   await check('legacy stop preserves shared negative and malformed acknowledgements without fallback',async()=>{
    for(const value of [{ok:false,reason:'steam-session-changed'},null,{}, {ok:'true'},false]){
     const f=fixture();f.setAnswer(value);await assert.rejects(f.stop(desktop,{userInitiated:true}),/会话已改变|退出请求未成功/);assert.equal(f.calls.length,1);assert.equal(f.shells.length,0);
    }
   });
   await check('legacy uncertain stop never retries and a later explicit request remains possible',async()=>{
    const f=fixture(),error=Error('native stop timeout');f.setFailure(error);
    await assert.rejects(f.stop(desktop,{userInitiated:true}),e=>e===error);assert.equal(f.calls.length,1);assert.equal(f.shells.length,0);
    f.setFailure(null);await f.stop(desktop,{userInitiated:true});assert.equal(f.calls.length,2);assert.equal(f.shells.length,0);
   });
   await check('legacy stop shares exact identity admission when mode is unknown and does no extra reads',async()=>{
    const f=fixture();await f.stop({...desktop,mode:'unknown'},{userInitiated:true});assert.equal(f.calls.length,1);assert.equal(f.calls[0].command,'steam.ui.stop');assert.equal(f.shells.length,0);
   });
  }
 } catch(error) {failure={name:error.name,message:error.message};process.exitCode=1;}
 const sourceHashes=Object.fromEntries([...before].map(([file,b])=>{assert.deepEqual(fs.readFileSync(file),b,'source remained fixed');return[file,hash(b)];}));
 const record={status:failure?'FAIL_RETAINED':'PASS_LEGACY_STOP_SHARED_OWNER',selection:control?'legacy-stop-control':'legacy-stop-only',cases,passed:cases.length,failure,sourceHashes,actualDeclaration:declaration,actualFullSharedModule:true,ipcAndShellLeaves:'CONTROLLED_MEMORY',realSteamOperations:0,defaultModeAndLiveGroupsExecuted:false};
 if(output)fs.writeFileSync(output,JSON.stringify(record,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(record));
}
async function notificationReadCases(output) {
 const files=['src/bridge/ipc.ts','src/bridge/steamUi.ts','tools/steam_ui_mode_service_selftest.mjs'];
 const sources=new Map(files.map(file=>[file,fs.readFileSync(file)]));
 const script=file=>transformSync(sources.get(file).toString('utf8'),{loader:'ts',format:'cjs'}).code;
 const ipcCode=script(files[0]),serviceCode=script(files[1]);
 const state={known:true,running:true,mode:'desktop',pid:123,birth:'456',reason:'fixture'};
 const auto={automatic:true};
 function fixture(){
  const host=new EventTarget(),window=new EventTarget(),requests=[];
  let legacy=0;
  const receive=data=>host.dispatchEvent(new MessageEvent('message',{data}));
  window.chrome={webview:{addEventListener:host.addEventListener.bind(host),postMessage:req=>requests.push(JSON.parse(JSON.stringify(req)))}};
  const ipc={exports:{}},service={exports:{}};
  vm.runInNewContext(ipcCode,{module:ipc,exports:ipc.exports,window,CustomEvent,setTimeout,clearTimeout,Error,queueMicrotask});
  vm.runInNewContext(serviceCode,{module:service,exports:service.exports,Error,Number,require:name=>{
   if(name==='./ipc')return ipc.exports;
   if(name==='./yeman')return {steamRunning:async()=>{legacy++;return true;}};
   throw Error('Unexpected fixture dependency '+name);
  }});
  return {ui:service.exports.steamUi,requests,legacy:()=>legacy,close:ipc.exports.rejectAllPending,
   emit:()=>receive({event:'steam.uiStateChanged',data:{}}),
   reply:(request,result=state)=>receive({id:request.id,result}),fail:(request,error)=>receive({id:request.id,error})};
 }
 const cases=[];let failure=null,overlapRequests;
 const check=async(name,run)=>{
  const f=fixture(),reads=[];
  const read=options=>{const promise=f.ui.read(options);reads.push(promise);void promise.catch(()=>{});return promise;};
  try{await run(f,read,reads);cases.push(name);}finally{f.close();await Promise.allSettled(reads);}
 };
 try {
  await check('same notification shares one passive IPC with independent results',async(f,read,reads)=>{
   f.ui.subscribe(()=>read(auto));f.ui.subscribe(()=>read(auto));f.emit();
   overlapRequests=f.requests.length;
   assert.equal(overlapRequests,1,'same notification request count');
   assert.deepEqual(f.requests[0],{id:0,cmd:'steam.uiState',args:{}});f.reply(f.requests[0]);
   const [one,two]=await Promise.all(reads);assert.notEqual(one,two);
   assert.equal(one.pid,123);assert.equal(two.birth,'456');one.pid=0;assert.equal(two.pid,123);
   assert.equal(f.legacy(),0);
  });
  await check('separate notifications do not join; force and unsubscribed callbacks stay independent',async(f,read,reads)=>{
   const a=f.ui.subscribe(()=>read(auto)),b=f.ui.subscribe(()=>read(auto)),force=f.ui.subscribe(()=>read());
   f.emit();force();a();f.emit();b();f.emit();
   assert.deepEqual(f.requests.map(r=>r.args),[{}, {force:true}, {}]);
   f.reply(f.requests[2],{...state,birth:'789'});f.reply(f.requests[1]);f.reply(f.requests[0]);
   assert.deepEqual((await Promise.all(reads)).map(v=>v.birth),['456','456','456','789']);
  });
  await check('post-action confirmation and plain reads never join pending notification',async(f,read,reads)=>{
   f.ui.subscribe(()=>read(auto));f.emit();
   const action=f.ui.shutdown(state,{userInitiated:true});
   assert.equal(f.requests[1].cmd,'steam.ui.stop');f.reply(f.requests[1],{ok:true});await action;
   const confirmation=read(auto),plain=read(auto);
   assert.deepEqual(f.requests.map(r=>r.cmd),['steam.uiState','steam.ui.stop','steam.uiState','steam.uiState']);
   f.reply(f.requests[2],{...state,known:true,running:false,mode:'stopped',pid:0,birth:'0'});f.reply(f.requests[3]);f.reply(f.requests[0]);
   assert.equal((await confirmation).mode,'stopped');assert.equal((await plain).mode,'desktop');assert.equal((await reads[0]).mode,'desktop');
  });
  await check('rejects transport and malformed replies; missing handler shares one legacy read only for this dispatch',async(f,read,reads)=>{
   f.ui.subscribe(()=>read(auto));f.ui.subscribe(()=>read(auto));
   f.emit();f.fail(f.requests[0],'transport unavailable');
   await Promise.all(reads.slice(0,2).map(p=>assert.rejects(p,/transport unavailable/)));assert.equal(f.legacy(),0);
   f.emit();f.reply(f.requests[1],{});
   await Promise.all(reads.slice(2,4).map(p=>assert.rejects(p,/返回无效/)));assert.equal(f.legacy(),0);
   f.emit();f.fail(f.requests[2],'unknown: steam.uiState');
   const legacy=await Promise.all(reads.slice(4));assert.equal(legacy.length,2);assert.equal(f.legacy(),1);
   for(const value of legacy)assert.deepEqual(JSON.parse(JSON.stringify(value)),{known:true,running:true,mode:'unknown',pid:0,birth:'0',reason:'native-ui-mode-unavailable'});
   f.emit();f.reply(f.requests[3]);assert.equal((await reads[6]).pid,123);assert.equal((await reads[7]).pid,123);
   assert.equal(f.requests.length,4);assert.equal(f.legacy(),1);
  });
 } catch (error) {failure={name:error.name,message:error.message};process.exitCode=1;}
 const sourceHashes=Object.fromEntries([...sources].map(([file,b])=>{assert.deepEqual(fs.readFileSync(file),b);return [file,createHash('sha256').update(b).digest('hex')];}));
 const result={pass:failure===null,selection:'notification-read-only',cases,failure,overlapRequests,sourceHashes,actualIpcAndSteamUi:true,webviewTransport:'CONTROLLED_EVENT_TARGET',realSteamOperations:0};
 if(output)fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));
}
async function presenceFallbackCases(output) {
 const files=['src/bridge/ipc.ts','src/bridge/steamUi.ts','src/bridge/yeman.ts','tools/steam_ui_mode_service_selftest.mjs'];
 const sources=new Map(files.map(file=>[file,fs.readFileSync(file)]));
 const bridge=sources.get(files[2]).toString('utf8');
 const declaration=(startMarker,endMarker)=>{
  const start=bridge.indexOf(startMarker),end=bridge.indexOf(endMarker,start);
  assert(start>=0&&end>start&&bridge.indexOf(startMarker,start+1)<0,'one complete production declaration');
  return bridge.slice(start,end);
 };
 const compile=text=>transformSync(text,{loader:'ts',format:'cjs'}).code;
 const ipcCode=compile(sources.get(files[0]).toString('utf8'));
 const serviceCode=compile(sources.get(files[1]).toString('utf8'));
 const runningCode=compile(declaration('async function nativeProcRunning(', '// AC/DC is a passive read')+
                           declaration('export async function steamRunning(', 'export async function steamStop('));
 const replies={present:{available:true,version:1,requested:4,presence:1},absent:{available:true,version:1,requested:4,presence:0}};
 const desktop={known:true,running:true,mode:'desktop',pid:123,birth:'456',reason:'fixture'};
 const cases=[];let failure=null,observed=[];
 const check=async(name,mode,facts,legacy,run)=>{
  const host=new EventTarget(),window=new EventTarget(),ipc={exports:{}},service={exports:{}},bridge={exports:{}};
  const requests=[];observed=requests;
  const transport={
   'steam.uiState':mode,'steamInformation.get':facts,'proc.running':legacy,
  };
  window.chrome={webview:{addEventListener:host.addEventListener.bind(host),postMessage:request=>{
   requests.push(JSON.parse(JSON.stringify(request)));
   const value=transport[request.cmd];
   assert.notEqual(value,undefined,`unexpected IPC ${request.cmd}`);
   const reply=typeof value==='string'?{id:request.id,error:value}:{id:request.id,result:value};
   queueMicrotask(()=>host.dispatchEvent(new MessageEvent('message',{data:reply})));
  }}};
  vm.runInNewContext(ipcCode,{module:ipc,exports:ipc.exports,window,CustomEvent,setTimeout,clearTimeout,Error,queueMicrotask});
  vm.runInNewContext(runningCode,{module:bridge,exports:bridge.exports,invoke:ipc.exports.invoke,Error});
  vm.runInNewContext(serviceCode,{module:service,exports:service.exports,Error,require:name=>{
   if(name==='./ipc')return ipc.exports;
   if(name==='./yeman')return bridge.exports;
   throw Error(`unexpected module ${name}`);
  }});
  try{await run(service.exports.steamUi,requests);cases.push({name,commands:requests.map(r=>r.cmd)});}
  finally{ipc.exports.rejectAllPending();}
 };
 const older='unknown: steam.uiState',oldest='Unknown IPC cmd: steamInformation.get';
 const commands=(requests,names)=>assert.deepEqual(requests.map(r=>r.cmd),names);
 try {
  await check('missing mode reads the shared present fact, with no generic scan',older,replies.present,{steam:true},async(ui,req)=>{
   const s=await ui.read({automatic:true});commands(req,['steam.uiState','steamInformation.get']);
   assert.deepEqual(req[1].args,{mask:4});assert(s.running&&s.known&&s.mode==='unknown');assert.equal(s.pid,0);assert.equal(s.birth,'0');
   await assert.rejects(ui.shutdown(s,{userInitiated:true}),/实例身份/);commands(req,['steam.uiState','steamInformation.get']);
  });
  await check('explicit shared absence is stopped',older,replies.absent,undefined,async(ui,req)=>{
   const s=await ui.read();assert(s.known&&!s.running&&s.mode==='stopped');commands(req,['steam.uiState','steamInformation.get']);
  });
  await check('modern native mode stays one request',desktop,undefined,undefined,async(ui,req)=>{
   assert.equal((await ui.read()).pid,123);commands(req,['steam.uiState']);
  });
  await check('oldest native uses one generic query only on exact missing facts handler',older,oldest,{steam:false},async(ui,req)=>{
   assert.equal((await ui.read()).mode,'stopped');commands(req,['steam.uiState','steamInformation.get','proc.running']);
   assert.deepEqual(req[2].args,{names:['steam']});
  });
  for(const [label,response] of [['unknown',{...replies.present,presence:-1}],['unavailable',{available:false}],
   ['version',{...replies.present,version:2}],['wrong mask',{...replies.present,requested:2}],['bad type',{...replies.present,presence:'0'}],
   ['transport','IPC command timed out: steamInformation.get'],['other handler','unknown: proc.running']]){
   await check(`facts ${label} never falls back`,older,response,undefined,async(ui,req)=>{
    await assert.rejects(ui.read());commands(req,['steam.uiState','steamInformation.get']);
   });
  }
  await check('unrelated mode error is not a missing handler','unsupported IPC reply',undefined,undefined,async(ui,req)=>{
   await assert.rejects(ui.read(),/unsupported IPC reply/);commands(req,['steam.uiState']);
  });
  await check('oldest inventory failure never becomes stopped',older,oldest,'PROCESS_INVENTORY_UNAVAILABLE',async(ui,req)=>{
   await assert.rejects(ui.read(),/状态验证失败/);commands(req,['steam.uiState','steamInformation.get','proc.running']);
  });
 } catch(error) {failure={name:error.name,message:error.message};process.exitCode=1;}
 const sourceHashes=Object.fromEntries([...sources].map(([file,b])=>{assert.deepEqual(fs.readFileSync(file),b);return [file,createHash('sha256').update(b).digest('hex')];}));
 const result={pass:failure===null,selection:'presence-fallback-only',cases,failure,lastCommands:observed.map(r=>r.cmd),sourceHashes,realSteamOperations:0};
 if(output)fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(result));
}
const selectedFlags=process.argv.slice(2).filter(arg=>arg.startsWith('--'));
if(selectedFlags.length) {
 if(selectedFlags.length!==1||!['--legacy-stop-only','--legacy-stop-control','--notification-read-only','--presence-fallback-only'].includes(selectedFlags[0]))throw Error('Invalid Steam UI service selection');
 const outputs=process.argv.slice(2).filter(arg=>!arg.startsWith('--'));if(outputs.length>1)throw Error('Invalid Steam UI service output');
 if(selectedFlags[0]==='--presence-fallback-only')await presenceFallbackCases(outputs[0]);
 else if(selectedFlags[0]==='--notification-read-only')await notificationReadCases(outputs[0]);
 else await legacyStopCases(selectedFlags[0]==='--legacy-stop-control',outputs[0]);
} else {
const cases=[];async function check(name,fn){await fn();cases.push(name)};
const mod={exports:{}};const code=transformSync(fs.readFileSync('src/bridge/steamUi.ts','utf8'),{loader:'ts',format:'cjs'}).code;
vm.runInNewContext(code,{module:mod,exports:mod.exports,require:()=>({}),Error,Number});const {createSteamUi,validSteamUiState}=mod.exports;
const desktop={known:true,running:true,mode:'desktop',pid:123,birth:'456',reason:''};
let state={...desktop},error=null,calls=[],legacy=0,result={ok:true};
const api=createSteamUi({invoke:async(cmd,args,options)=>{calls.push({cmd,args,options});if(error)throw Error(error);return cmd==='steam.uiState'?state:result},running:async()=>{legacy++;return true}});
for(const intent of [undefined,null,{}, {userInitiated:false}, {userInitiated:'true'}, {userInitiated:1}]) await check('missing or malformed manual intent is refused before native IPC '+JSON.stringify(intent),async()=>{const n=calls.length;await assert.rejects(api.switchMode(desktop,'bigpicture',intent),/手动点击/);await assert.rejects(api.switchMode(desktop,'desktop',intent),/手动点击/);assert.equal(calls.length,n)});
await check('valid process-bound desktop state accepted',()=>assert.equal(validSteamUiState(desktop),true));
for(const patch of [{pid:-1},{pid:2**32},{birth:'x'},{birth:'0'},{birth:'00'},{mode:'unknown',known:true,running:false},{mode:'stopped',known:false,running:false},{known:false},{running:false},{mode:'fake'}])await check('malformed state rejected '+JSON.stringify(patch),()=>assert.equal(validSteamUiState({...desktop,...patch}),false));
await check('native state read does not enumerate legacy processes',async()=>{assert.equal((await api.read()).mode,'desktop');assert.equal(legacy,0)});
await check('mode request binds the displayed PID and birth, no arbitrary URL',async()=>{await api.switchMode(desktop,'bigpicture',{userInitiated:true});assert.deepEqual(JSON.parse(JSON.stringify(calls.at(-1).args)),{mode:'bigpicture',pid:123,birth:'456',userInitiated:true})});
await check('already active target is a no-op even with explicit intent',async()=>{const n=calls.length;await api.switchMode(desktop,'desktop',{userInitiated:true});await api.switchMode({...desktop,mode:'bigpicture'},'bigpicture',{userInitiated:true});assert.equal(calls.length,n)});
await check('unknown mode never invokes a switch',async()=>{const n=calls.length;await assert.rejects(api.switchMode({...desktop,mode:'unknown'},'desktop',{userInitiated:true}));assert.equal(calls.length,n)});
await check('invalid target never invokes a switch',async()=>{const n=calls.length;await assert.rejects(api.switchMode(desktop,'shutdown',{userInitiated:true}));assert.equal(calls.length,n)});
await check('session drift is retryable and not accepted',async()=>{result={ok:false,reason:'steam-session-changed'};await assert.rejects(api.switchMode(desktop,'bigpicture',{userInitiated:true}),/会话已改变/)});
await check('only missing native handler permits running-only legacy fallback',async()=>{error='Unknown IPC cmd: steam.uiState';const v=await api.read();assert.equal(v.mode,'unknown');assert.equal(v.running,true);assert.equal(legacy,1)});
await check('production unknown-handler message also permits legacy fallback',async()=>{error='unknown: steam.uiState';const v=await api.read();assert.equal(v.mode,'unknown');assert.equal(legacy,2)});
await check('ordinary transport failure does not fake a stopped/desktop state',async()=>{error='native timeout';await assert.rejects(api.read());assert.equal(legacy,2)});
await check('invalid native response is not an older-host fallback',async()=>{error=null;state={...desktop,mode:'fake'};await assert.rejects(api.read());assert.equal(legacy,2)});
await check('explicit read requests force and uses an 8s IPC deadline',async()=>{state=desktop;error=null;await api.read();const call=calls.at(-1);assert.deepEqual(JSON.parse(JSON.stringify(call.args)),{force:true});assert.equal(call.options.timeoutMs,8000)});
await check('automatic read sends no force flag',async()=>{await api.read({automatic:true});assert.deepEqual(JSON.parse(JSON.stringify(calls.at(-1).args)),{})});
let oldCalls=0;
const old=createSteamUi({invoke:async(cmd,args)=>{oldCalls++;if(args.force)throw Error('Invalid Steam UI read');return desktop},running:async()=>false});
await check('older native argument compatibility retries exactly once',async()=>{assert.equal((await old.read()).mode,'desktop');assert.equal(oldCalls,2)});
let failedCalls=0;
const failedApi=createSteamUi({invoke:async()=>{failedCalls++;throw Error('native timeout')},running:async()=>false});
await check('IO or timeout failure never becomes an empty-argument retry loop',async()=>{await assert.rejects(failedApi.read());assert.equal(failedCalls,1)});
const embedded=fs.readFileSync('native/steam_live_script.h','utf8').split('inline constexpr const char kUiModeScript[] =')[1];
assert(embedded,'compact expression must be generated');
const compact=[...embedded.matchAll(/R"YMSTEAM\(([\s\S]*?)\)YMSTEAM"/g)].map(m=>m[1]).join('');
// Shared preflight validation can grow independently. Bound the compact shared preflight to 12 KiB in one raw literal,
// and reject unrelated operation bodies instead of freezing incidental bytes.
assert(Buffer.byteLength(compact)<=12288,'mode expression must remain within one bounded raw literal');
assert(!compact.includes("request.operation === 'sleep.inspect'")&&!compact.includes('const checkAccount =')&&!compact.includes('GetControllers()'),'mode transport must not include unrelated settings/sleep bodies');
for(const [label,script] of Object.entries({full:fs.readFileSync('native/steam_live.js','utf8'),compact})) {
 const checkMode=(name,fn)=>check(label+': '+name,fn);
async function live(raw,override={}){const window={};return await vm.runInNewContext(script+'({operation:"ui.mode.get"})',{window,globalThis:{SteamClient:{UI:{GetUIMode:async()=>raw}}},SteamClient:{UI:{GetUIMode:async()=>raw}},Date,Math,Promise,Number,setTimeout,clearTimeout,...override})}
for(const [raw,mode]of [[4,'bigpicture'],[1,'bigpicture'],[7,'desktop'],[0,'desktop'],[2,'unknown'],['4','unknown'],[null,'unknown']])await checkMode('Valve UI mode '+JSON.stringify(raw)+' maps without geometry/account reads',async()=>{const v=await live(raw);assert.equal(v.mode,mode);assert.equal(v.mutated,false)});
await checkMode('missing UI API remains unavailable, not desktop',async()=>{const v=await live(4,{globalThis:{}});assert.equal(v.ok,false);assert.equal(v.mutated,false)});
await checkMode('concurrent Steam operation is not overridden',async()=>{const v=await live(4,{window:{__ymccSteamLiveBusy:'other'}});assert.equal(v.reason,'live-busy')});
await checkMode('mode event subscription remembers rapid re-entry and registers only once per renderer',async()=>{
 let raw=7,callback,registrations=0;const window={};
 const UI={GetUIMode:async()=>raw,RegisterForUIModeChanged:fn=>{registrations++;callback=fn;return{unregister(){}}}};
 const context=vm.createContext({window,SteamClient:{UI},Date,Math,Promise,Number,setTimeout,clearTimeout});
 const read=()=>vm.runInContext(script+'({operation:"ui.mode.get"})',context);
 const desktop=await read();raw=4;callback(4);const first=await read();
 assert.equal(first.mode,'bigpicture');assert.notEqual(first.modeEpoch,desktop.modeEpoch);
 callback(4);assert.equal((await read()).modeEpoch,first.modeEpoch);
 callback(7);callback(4);const second=await read();assert.equal(second.mode,'bigpicture');assert.notEqual(second.modeEpoch,first.modeEpoch);
 callback('4');callback(2);assert.equal((await read()).modeEpoch,second.modeEpoch);assert.equal(registrations,1);
});
await checkMode('a real mode event during an async read wins over a stale mode response',async()=>{
 let callback,release;const window={};const UI={GetUIMode:()=>new Promise(r=>release=r),RegisterForUIModeChanged:fn=>{callback=fn}};
 const context=vm.createContext({window,SteamClient:{UI},Date,Math,Promise,Number,setTimeout,clearTimeout});
 const pending=vm.runInContext(script+'({operation:"ui.mode.get"})',context);callback(4);release(7);
 const result=await pending;assert.equal(result.mode,'bigpicture');assert.equal(result.rawMode,4);assert.match(result.modeEpoch,/:1$/);
});
await checkMode('older Steam without the callback keeps current-mode observation and does not invent an epoch',async()=>{const value=await live(4);assert.equal(value.mode,'bigpicture');assert.equal(value.modeEpoch,'')});
}
// New graceful-stop bridge retains the same explicit/session-bound contract.
for(const intent of [undefined,null,{}, {userInitiated:false},{userInitiated:'true'}])await check('graceful stop rejects absent or malformed explicit intent '+JSON.stringify(intent),async()=>{let dispatched=0;const owner=createSteamUi({invoke:async()=>{dispatched++;return {ok:true}},running:async()=>true});await assert.rejects(owner.shutdown(desktop,intent),/手动确认/);assert.equal(dispatched,0)});
await check('graceful stop binds the exact displayed client and only reports submission',async()=>{const sent=[];const owner=createSteamUi({invoke:async(command,args)=>{sent.push({command,args});return {ok:true}},running:async()=>true});await owner.shutdown(desktop,{userInitiated:true});assert.deepEqual(JSON.parse(JSON.stringify(sent)),[{command:'steam.ui.stop',args:{pid:123,birth:'456',userInitiated:true}}])});
await check('graceful stop refuses legacy unbound running-only state',async()=>{let dispatched=0;const owner=createSteamUi({invoke:async()=>{dispatched++;return {ok:true}},running:async()=>true});await assert.rejects(owner.shutdown({...desktop,pid:0,birth:'0',mode:'unknown'},{userInitiated:true}),/实例身份/);assert.equal(dispatched,0)});
for(const intent of [undefined,{}, {userInitiated:false},{userInitiated:'true'}]) await check('show Big Picture requires fresh explicit intent '+JSON.stringify(intent),async()=>{const sent=[];const owner=createSteamUi({invoke:async(...args)=>{sent.push(args);return {ok:true}},running:async()=>true});await assert.rejects(owner.showBigPicture({...desktop,mode:'bigpicture'},intent),/手动点击/);assert.equal(sent.length,0)});
for(const mode of ['desktop','unknown','stopped']) await check('show Big Picture never changes '+mode+' mode',async()=>{let sent=0;const owner=createSteamUi({invoke:async()=>{sent++;return {ok:true}},running:async()=>true});await assert.rejects(owner.showBigPicture({...desktop,mode},{userInitiated:true}));assert.equal(sent,0)});
await check('Big Picture status show uses only the fixed current-instance native command',async()=>{const sent=[];const owner=createSteamUi({invoke:async(command,args)=>{sent.push({command,args});return {ok:true}},running:async()=>true});await owner.showBigPicture({...desktop,mode:'bigpicture'},{userInitiated:true});assert.deepEqual(JSON.parse(JSON.stringify(sent)),[{command:'steam.ui.showBigPicture',args:{pid:123,birth:'456',userInitiated:true}}])});
const output=process.argv[2];
const record={ok:true,cases,passed:cases.length,realSteamOperations:0};if(output)fs.writeFileSync(output,JSON.stringify(record,null,2));console.log(JSON.stringify(record,null,2));

}
