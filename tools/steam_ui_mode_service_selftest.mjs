import assert from 'node:assert/strict';import fs from 'node:fs';import vm from 'node:vm';import {transformSync} from 'esbuild';
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
