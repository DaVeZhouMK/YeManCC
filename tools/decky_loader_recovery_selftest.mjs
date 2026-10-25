// Runs the pinned/patched packaged router and injection scripts in memory.
// All sockets, auth and time are inert; no Steam, Loader or real business owner.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
const dir=path.resolve(process.argv[2]);
const original=fs.readFileSync(path.join(dir,'original-router.js'),'utf8');
const patched=fs.readFileSync(path.join(dir,'patched-router.js'),'utf8');
const injection=fs.readFileSync(path.join(dir,'patched-injection.js'),'utf8');
const boot=fs.existsSync(path.join(dir,'patched-boot.js'))?fs.readFileSync(path.join(dir,'patched-boot.js'),'utf8'):'';
const cases=[];const metrics=[];const flush=async()=>{for(let n=0;n<5;n++)await new Promise(r=>setImmediate(r));};
async function check(name,fn){await fn();cases.push(name);}
function fixture(source=patched,{authOK=true}={}){
 const state={sockets:[],logs:[],timers:new Map(),now:0,next:1,timerCallbacks:0,fetches:0,authRequests:[],restarts:0,imports:0,mounts:0};
 const win={deckyHasLoaded:true,deckyAuthToken:'old-secret-token'};
 const set=(fn,ms)=>{const id=state.next++;state.timers.set(id,{fn,due:state.now+ms,ms});return id;};
 class Socket{
  static CONNECTING=0;static OPEN=1;static CLOSING=2;static CLOSED=3;
  constructor(url){if(state.failNext){state.failNext--;throw Error('inert constructor failure');}this.url=url;this.readyState=0;this.listeners={};this.sent=[];state.sockets.push(this);}
  addEventListener(name,fn,opt){(this.listeners[name]??=[]).push({fn,once:opt?.once});}
  emit(name,value={}){const listeners=[...(this.listeners[name]||[])];this.listeners[name]=(this.listeners[name]||[]).filter(x=>!x.once);for(const {fn}of listeners)fn({currentTarget:this,target:this,...value});}
  open(){this.readyState=1;this.emit('open');}
  close(code=1006){this.readyState=3;this.emit('close',{code,wasClean:false});}
  send(value){this.sent.push(JSON.parse(value));}
 }
 class Logger{constructor(){}debug(...args){state.logs.push({level:'debug',args});}error(...args){state.logs.push({level:'error',args});}warn(...args){state.logs.push({level:'warn',args});}}
 const context=vm.createContext({window:win,WebSocket:Socket,AbortController,Logger,setTimeout:set,clearTimeout:id=>state.timers.delete(id),sleep:ms=>new Promise(r=>set(r,ms)),Date:class extends Date{static now(){return state.now+(state.wallOffset??0);}},console:{error(...args){state.logs.push({level:'console-error',args});},debug(...args){state.logs.push({level:'console-debug',args});},time(){},timeEnd(){}},fetch:async(url,options)=>{state.fetches++;state.authRequests.push({signal:options?.signal});if(state.fetchImpl)return state.fetchImpl(url,options);return{ok:authOK,text:async()=> 'new-secret-token'};},__import:async url=>{
   if(url.startsWith('http')){state.imports++;state.lastImportUrl=url;if(state.importFails){state.importFails--;throw Error('inert module fetch failure');}if(state.realBoot)vm.runInContext('(()=>{'+boot.replaceAll('import(','__import(')+'})()',context);return;}
   if(url==='./chunk-BFRQAfGY.js')return {WSRouter:Router};
   if(url==='./chunk-IeLqH5QF.js'){state.mounts++;return {V:{}};}
   if(url==='./chunk-D0GKGskP.js')return {};
   throw Error('unexpected inert module '+url);
  },SteamClient:{Browser:{RestartJSContext(){state.restarts++;}}}});
 Object.defineProperty(context,'DeckyBackend',{get:()=>win.DeckyBackend,set:v=>win.DeckyBackend=v});
 Object.defineProperty(context,'deckyAuthToken',{get:()=>win.deckyAuthToken,set:v=>win.deckyAuthToken=v});
 const Router=vm.runInContext(source.replace(/^import .*;\r?\n/gm,'').replace(/^export \{[^\n]*\};/m,'')+'\nWSRouter;',context);
 const router=new Router();win.DeckyBackend=router;
 async function advance(ms){const end=state.now+ms;let steps=0;while(true){let next=null;for(const [id,t]of state.timers)if(t.due<=end&&(!next||t.due<next[1].due))next=[id,t];if(!next)break;if(++steps>500)throw Error('timer loop');state.now=next[1].due;state.timers.delete(next[0]);state.timerCallbacks++;next[1].fn();await flush();}state.now=end;await flush();}
 async function inject(){vm.runInContext(injection.replaceAll('import(','__import('),context,{timeout:1000});await flush();}
 return{state,router,Socket,win,advance,inject};
}
await check('old production router reproduces error spam and a full five-second reconnect wait',async()=>{
 const f=fixture(original);void f.router.connect();f.state.sockets[0].close();await f.advance(4999);assert.equal(f.state.sockets.length,1);assert.equal(f.state.logs.filter(x=>x.level==='error').length,1);await f.advance(1);assert.equal(f.state.sockets.length,2);
});
await check('initial connection promise survives a failed handshake and resolves after recovery',async()=>{
 const f=fixture();let ready=false;const p=f.router.connect().then(()=>ready=true);f.state.sockets[0].close();await f.advance(5000);assert.equal(ready,false);f.state.sockets[1].open();await p;assert.equal(ready,true);assert.equal(f.state.timers.size,0);
});
await check('verified backend readiness cancels backoff and reuses the original pending FULL_SYNC queue',async()=>{
 const f=fixture();void f.router.connect();const first=f.state.sockets[0];first.open();const result=f.router.call('fixture/read-only-state',7);first.close();assert.equal(f.state.sockets.length,1);await f.inject();assert.equal(f.state.sockets.length,2);const recovered=f.state.sockets[1];assert.match(recovered.url,/auth=new-secret-token$/);recovered.open();assert.equal(recovered.sent[0].type,4);assert.equal(recovered.sent[0].messages[0].route,'fixture/read-only-state');recovered.emit('message',{data:JSON.stringify({type:1,id:1,result:123})});assert.equal(await result,123);assert.equal(f.router.runningCalls.size,0);assert.equal(f.state.restarts,0);assert.equal(f.state.imports,0);
});
await check('one hundred concurrent connect calls create only one socket',async()=>{const f=fixture();const p=f.router.connect();for(let n=0;n<100;n++)assert.equal(f.router.connect(),p);assert.equal(f.state.sockets.length,1);f.state.sockets[0].open();await p;});
await check('one hundred backend-ready signals retain an authenticated live socket and do not resend calls',async()=>{const f=fixture();void f.router.connect();f.state.sockets[0].open();for(let n=0;n<100;n++)f.router.__YMCC_DECKY_RECOVERY__.backendReady();assert.equal(f.state.sockets.length,1);assert.equal(f.state.sockets[0].sent.length,1);assert.equal(f.state.timers.size,0);});
await check('duplicate close notifications coalesce to one retry and no generic error report',async()=>{const f=fixture();void f.router.connect();const s=f.state.sockets[0];s.close();s.emit('close',{code:1006});f.router.onError({target:s,code:1006});assert.equal(f.state.timers.size,1);assert.equal(f.router.__YMCC_DECKY_RECOVERY__.disconnects,1);assert.equal(f.state.logs.some(x=>x.level==='error'),false);});
await check('stale close events cannot disturb the new live connection',async()=>{const f=fixture();void f.router.connect();const old=f.state.sockets[0];old.close();await f.advance(5000);const current=f.state.sockets[1];current.open();f.router.onError({target:old,code:1006});assert.equal(f.state.timers.size,0);assert.equal(f.router.ws,current);});
await check('repeated failed connections retain one backoff timer and bound diagnostics to thirty seconds',async()=>{const f=fixture();void f.router.connect();for(let n=0;n<20;n++){f.state.sockets.at(-1).close();assert.equal(f.state.timers.size,1);await f.advance([5000,15000,30000,60000][Math.min(n,3)]);}assert.equal(f.state.logs.filter(x=>x.level==='error').length,0);assert.ok(f.state.logs.length<=20);});
await check('a stuck handshake is closed after a bounded ten seconds and remains recoverable',async()=>{const f=fixture();void f.router.connect();await f.advance(10000);assert.equal(f.state.sockets[0].readyState,3);assert.equal(f.state.timers.size,1);await f.advance(5000);assert.equal(f.state.sockets.length,2);});
await check('backend-ready credit permits only one immediate follow-up after a racing failed handshake',async()=>{const f=fixture();void f.router.connect();f.router.__YMCC_DECKY_RECOVERY__.backendReady();f.state.sockets[0].close();await f.advance(0);assert.equal(f.state.sockets.length,2);f.state.sockets[1].close();await f.advance(0);assert.equal(f.state.sockets.length,2);await f.advance(15000);assert.equal(f.state.sockets.length,3);});
await check('closing sockets are allowed to finish before readiness opens one replacement',async()=>{const f=fixture();void f.router.connect();f.state.sockets[0].readyState=2;f.router.__YMCC_DECKY_RECOVERY__.backendReady();assert.equal(f.state.sockets.length,1);f.state.sockets[0].close();await f.advance(0);assert.equal(f.state.sockets.length,2);});
await check('auth failure preserves the current token and pending retry',async()=>{const f=fixture(patched,{authOK:false});void f.router.connect();f.state.sockets[0].close();await f.inject();assert.equal(f.win.deckyAuthToken,'old-secret-token');assert.equal(f.state.sockets.length,1);assert.equal(f.state.timers.size,2);});
await check('already-loaded legacy router is upgraded without remounting and its old delayed retry is harmless',async()=>{const f=fixture(original);void f.router.connect();const first=f.state.sockets[0];first.open();first.close();const queued=f.router.call('fixture/read-only-state');await f.inject();assert.equal(f.state.sockets.length,2);const s=f.state.sockets[1];s.open();await f.advance(5000);assert.equal(f.state.sockets.length,2);assert.equal(s.sent[0].messages.length,1);assert.equal(f.state.imports,0);assert.equal(f.state.restarts,0);void queued;});
await check('diagnostics contain bounded close facts without URL, token or exception objects',async()=>{const f=fixture();void f.router.connect();f.state.sockets[0].close();const wire=JSON.stringify(f.state.logs);assert.ok(!wire.includes('secret-token'));assert.ok(!wire.includes('/ws?'));assert.ok(!wire.includes('CloseEvent'));assert.ok(wire.includes('waiting-backend'));assert.equal(typeof f.state.logs[0].args[0],'string');const jsonText=f.state.logs[0].args[0].slice('[YMCC:Decky] transport '.length);assert.equal(JSON.parse(jsonText).code,1006);});
await check('genuine Python RPC errors still reject their caller through the original router',async()=>{const f=fixture();void f.router.connect();const s=f.state.sockets[0];s.open();const p=f.router.call('fixture/failure');s.emit('message',{data:JSON.stringify({type:-1,id:1,error:{name:'Fixture',error:'failure',traceback:''}})});await assert.rejects(p,/failure/);});
await check('WebSocket constructor rejection stays on one backoff and does not strand recovery',async()=>{const f=fixture();f.state.failNext=1;let ready=false;const p=f.router.connect().then(()=>ready=true);await flush();assert.equal(f.state.sockets.length,0);assert.equal(f.state.timers.size,1);await f.advance(5000);assert.equal(f.state.sockets.length,1);f.state.sockets[0].open();await p;assert.equal(ready,true);});
await check('authentication-ready on a borrowed old socket transitions to guarded connections',async()=>{const f=fixture(original);void f.router.connect();await f.inject();f.state.sockets[0].close();await f.advance(0);assert.equal(f.state.sockets.length,2);f.state.sockets[1].open();await f.advance(5000);assert.equal(f.state.sockets.length,2);});
await check('a borrowed legacy handshake gains a deadline without duplicate timers or connections',async()=>{
 const f=fixture(original);void f.router.connect();await f.inject();const borrowed=f.state.sockets[0];
 const ready=f.router.connect();for(let n=0;n<100;n++)f.router.connect();assert.equal(f.state.sockets.length,1);
 assert.equal(f.state.timers.size,1);await f.advance(9999);assert.equal(borrowed.readyState,0);
 await f.advance(1);assert.equal(borrowed.readyState,3);assert.equal(f.state.sockets.length,2);
 f.state.sockets[1].open();await ready;await f.advance(5000);assert.equal(f.state.sockets.length,2);assert.equal(f.state.timers.size,0);
});
await check('backend readiness before initial connect reaches the canonical transport once',async()=>{
 const f=fixture();await f.inject();assert.equal(f.state.sockets.length,1);const ready=f.router.connect();for(let n=0;n<100;n++)f.router.connect();assert.equal(f.state.sockets.length,1);f.state.sockets[0].open();await ready;assert.equal(f.state.timers.size,0);
});
await check('a hung authentication request releases pending and retries by itself after bounded backoff',async()=>{
 const f=fixture();const router=f.win.DeckyBackend;f.state.fetchImpl=()=>new Promise(()=>{});await f.inject();
 assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.pending,true);await f.advance(9999);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.pending,true);
 await f.advance(1);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.pending,false);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.phase,'retry');
 assert.equal(f.state.authRequests[0].signal.aborted,true);assert.equal(f.win.deckyAuthToken,'old-secret-token');assert.equal(f.win.DeckyBackend,router);assert.equal(f.state.sockets.length,0);
 assert.equal(f.state.timers.size,1);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.retryDelayMs,5000);
 f.state.fetchImpl=null;await f.advance(5000);assert.equal(f.win.deckyAuthToken,'new-secret-token');assert.equal(f.state.sockets.length,1);f.state.sockets[0].open();assert.equal(f.state.timers.size,0);
});
await check('a stalled authentication response body has the same deadline and retains the old token',async()=>{
 const f=fixture();f.state.fetchImpl=async()=>({ok:true,text:()=>new Promise(()=>{})});await f.inject();await f.advance(10000);
 assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.pending,false);assert.equal(f.win.deckyAuthToken,'old-secret-token');assert.equal(f.state.authRequests[0].signal.aborted,true);assert.equal(f.state.timers.size,1);
});
await check('a late timed-out authentication response cannot overwrite the next successful token',async()=>{
 const f=fixture();let release;f.state.fetchImpl=()=>new Promise(resolve=>release=resolve);await f.inject();await f.advance(10000);
 f.state.fetchImpl=null;await f.inject();const current=f.win.__YMCC_DECKY_INJECTION_POLICY__;release({ok:true,text:async()=>'late-expired-secret-token'});await flush();
 assert.equal(f.win.deckyAuthToken,'new-secret-token');assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__,current);assert.equal(current.phase,'reused');assert.equal(current.pending,false);assert.equal(f.state.sockets.length,1);
});
await check('a late timed-out response body cannot create a new connection or change the current token',async()=>{
 const f=fixture();let release;f.state.fetchImpl=async()=>({ok:true,text:()=>new Promise(resolve=>release=resolve)});await f.inject();await f.advance(10000);
 f.state.fetchImpl=null;await f.inject();release('late-expired-secret-token');await flush();assert.equal(f.win.deckyAuthToken,'new-secret-token');assert.equal(f.state.sockets.length,1);
});
await check('normal authentication clears its deadline without aborting or disturbing a healthy socket',async()=>{
 const f=fixture();const ready=f.router.connect();f.state.sockets[0].open();await ready;await f.inject();assert.equal(f.state.timers.size,0);assert.equal(f.state.authRequests[0].signal.aborted,false);await f.advance(20000);assert.equal(f.state.sockets.length,1);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.phase,'reused');
});
await check('authentication outage backs off to one attempt per minute and stops all timers when healthy',async()=>{
 const f=fixture(patched,{authOK:false});await f.inject();for(const ms of [5000,15000,30000,60000,60000,60000]){assert.equal(f.state.timers.size,1);await f.advance(ms);}
 assert.equal(f.state.fetches,7);assert.equal(f.state.timers.size,1);f.state.fetchImpl=async()=>({ok:true,text:async()=>'new-secret-token'});await f.advance(60000);f.state.sockets[0].open();
 const count=f.state.fetches,ticks=f.state.timerCallbacks;assert.equal(f.state.timers.size,0);await f.advance(3600000);assert.equal(f.state.fetches,count);assert.equal(f.state.timers.size,0);assert.equal(f.state.timerCallbacks,ticks);metrics.push({scenario:'healthy after recovery',virtualMs:3600000,authFetches:0,timerCallbacks:0,remainingTimers:0});
});
await check('readiness notifications cancel a failed auth timer without leaving stale retry callbacks',async()=>{
 const f=fixture(patched,{authOK:false});await f.inject();assert.equal(f.state.timers.size,1);f.state.fetchImpl=async()=>({ok:true,text:async()=>'new-secret-token'});
 for(let n=0;n<100;n++)void f.inject();await flush();assert.equal(f.state.fetches,2);assert.equal(f.state.sockets.length,1);f.state.sockets[0].open();await f.advance(60000);assert.equal(f.state.fetches,2);assert.equal(f.state.timers.size,0);
});
await check('failed initial module fetch retries autonomously with a fresh entry URL',async()=>{
 const f=fixture();f.win.deckyHasLoaded=false;delete f.win.DeckyBackend;f.state.importFails=1;await f.inject();assert.equal(f.win.deckyHasLoaded,false);assert.equal(f.state.timers.size,1);
 await f.advance(5000);assert.equal(f.state.imports,2);assert.match(f.state.lastImportUrl,/ymccRetry=1/);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.phase,'imported');assert.equal(f.state.timers.size,0);
});
await check('real packaged async boot waits at four checks per second and retries failed auth before mounting once',async()=>{
 assert.ok(boot);const f=fixture();f.win.deckyHasLoaded=false;delete f.win.DeckyBackend;f.win.SP_REACT={};let ready=false,checks=0;f.win.App={BFinishedInitBeforeLogin:()=>{checks++;return ready;}};f.state.realBoot=true;
 f.state.fetchImpl=async()=>({ok:false,text:async()=>''});await f.inject();assert.equal(f.state.fetches,0);assert.equal(f.state.timers.size,1);await f.advance(1000);assert.equal(f.state.fetches,0);assert.equal(f.state.timers.size,1);assert.equal(checks,5);metrics.push({scenario:'SteamApp not ready',virtualMs:1000,checksIncludingInitial:checks,timerCallbacks:f.state.timerCallbacks,authFetches:f.state.fetches});
 ready=true;await f.advance(250);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.pending,false);assert.equal(f.state.sockets.length,0);assert.equal(f.state.timers.size,1);
 f.state.fetchImpl=async()=>({ok:true,text:async()=>'new-secret-token'});await f.advance(5000);assert.equal(f.state.sockets.length,1);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.pending,true);f.state.sockets[0].open();await flush();
 assert.equal(f.state.mounts,1);assert.equal(f.state.imports,2);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.phase,'imported');assert.equal(f.state.timers.size,0);await f.advance(60000);assert.equal(f.state.mounts,1);
});
await check('long websocket outage keeps one backoff timer and immediately honors fresh readiness',async()=>{
 const f=fixture();void f.router.connect();for(const ms of [5000,15000,30000,60000]){f.state.sockets.at(-1).close();await f.advance(ms);}
 f.state.sockets.at(-1).close();assert.equal([...f.state.timers.values()][0].ms,60000);await f.inject();assert.equal(f.state.sockets.length,6);f.state.sockets.at(-1).open();assert.equal(f.state.timers.size,0);await f.advance(3600000);assert.equal(f.state.sockets.length,6);
});
await check('one hundred connect calls during failure backoff cannot force extra attempts',async()=>{
 const f=fixture();void f.router.connect();f.state.sockets[0].close();for(let n=0;n<100;n++)void f.router.connect();assert.equal(f.state.sockets.length,1);assert.equal(f.state.timers.size,1);await f.advance(4999);assert.equal(f.state.sockets.length,1);await f.advance(1);assert.equal(f.state.sockets.length,2);
});
await check('initial async boot renews rotated backend auth and heals without another injection notification',async()=>{
 const f=fixture();f.win.deckyHasLoaded=false;delete f.win.DeckyBackend;f.win.SP_REACT={};f.win.App={BFinishedInitBeforeLogin:()=>true};f.state.realBoot=true;
 await f.inject();assert.equal(f.state.sockets.length,1);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.pending,true);f.state.sockets[0].close();
 f.state.fetchImpl=async()=>({ok:true,text:async()=>'rotated-backend-token'});await f.advance(5000);assert.equal(f.state.fetches,2);assert.equal(f.state.sockets.length,2);assert.match(f.state.sockets[1].url,/auth=rotated-backend-token$/);
 f.state.sockets[1].open();await flush();assert.equal(f.state.mounts,1);assert.equal(f.win.__YMCC_DECKY_INJECTION_POLICY__.pending,false);assert.equal(f.state.imports,1);assert.equal(f.state.timers.size,0);
});
await check('transport retry and one hundred injection notifications share one slow auth refresh',async()=>{
 const f=fixture();await f.inject();f.state.sockets[0].open();f.state.sockets[0].close();let release;f.state.fetchImpl=()=>new Promise(resolve=>release=resolve);await f.advance(5000);
 assert.equal(f.state.fetches,2);for(let n=0;n<100;n++)void f.inject();await flush();assert.equal(f.state.fetches,2);assert.equal(f.state.timers.size,1);
 release({ok:true,text:async()=>'new-secret-token'});await flush();assert.equal(f.state.sockets.length,2);f.state.sockets[1].open();assert.equal(f.state.timers.size,0);await f.advance(3600000);assert.equal(f.state.fetches,2);
});
await check('low CPU delayed timer service still recovers without a retry burst',async()=>{
 const f=fixture();void f.router.connect();f.state.sockets[0].close();const timer=[...f.state.timers.values()][0];f.state.timers.clear();f.state.now=120000;timer.fn();await flush();
 assert.equal(f.state.sockets.length,2);assert.equal(f.state.timers.size,1);f.state.sockets[1].open();assert.equal(f.state.timers.size,0);await f.advance(3600000);assert.equal(f.state.sockets.length,2);
});
await check('a socket stuck CLOSING cannot block recovery forever after fresh readiness',async()=>{
 const f=fixture();void f.router.connect();const old=f.state.sockets[0];old.open();old.readyState=2;f.router.__YMCC_DECKY_RECOVERY__.backendReady();
 await f.advance(10000);assert.equal(f.state.sockets.length,2);f.state.sockets[1].open();old.close();assert.equal(f.state.timers.size,0);assert.equal(f.router.ws,f.state.sockets[1]);
});
await check('late messages from a retired closing socket cannot settle current RPCs',async()=>{
 const f=fixture();void f.router.connect();const old=f.state.sockets[0];old.open();const pending=f.router.call('fixture/read-only-state');let value;pending.then(v=>value=v);old.readyState=2;f.router.__YMCC_DECKY_RECOVERY__.backendReady();await f.advance(10000);const current=f.state.sockets[1];current.open();
 old.emit('message',{data:JSON.stringify({type:1,id:1,result:'stale-old-socket'})});await flush();assert.equal(value,undefined);current.emit('message',{data:JSON.stringify({type:1,id:1,result:123})});assert.equal(await pending,123);
});
await check('a handshake whose close never acknowledges remains recoverable after closing grace',async()=>{
 const f=fixture();let ready=false;const pending=f.router.connect().then(()=>ready=true);const old=f.state.sockets[0];old.close=()=>{old.readyState=2;};await f.advance(10000);assert.equal(old.readyState,2);assert.equal(f.state.timers.size,1);await f.advance(15000);assert.equal(f.state.sockets.length,2);f.state.sockets[1].open();await pending;assert.equal(ready,true);assert.equal(f.state.timers.size,0);
});
await check('wall clock changes cannot postpone the relative retry schedule',async()=>{
 const f=fixture();void f.router.connect();f.state.sockets[0].close();f.state.wallOffset=-86400000;await f.advance(5000);assert.equal(f.state.sockets.length,2);f.state.wallOffset=86400000;f.state.sockets[1].open();assert.equal(f.state.timers.size,0);
});
await check('packaged policy contains no Steam mode, focus, reload or restart calls',async()=>{for(const text of [patched.slice(patched.indexOf('const ymccAttachRecovery')),injection])for(const word of ['RestartJSContext','SetUIMode','location.reload','SetForegroundWindow','SteamClient'])assert.ok(!text.includes(word),word);});
fs.writeFileSync(path.join(dir,'ROUTER-RECOVERY.json'),JSON.stringify({status:'passed',checks:cases.length,cases,actualPackagedRouter:true,actualPackagedInjection:true,realSockets:0,steamTouched:false},null,2)+'\n');
fs.writeFileSync(path.join(dir,'LOW-CPU-RECOVERY.json'),JSON.stringify({status:'passed',scope:'virtual clock and inert owners, no real CPU percentage claim',metrics},null,2)+'\n');
console.log(JSON.stringify({status:'passed',checks:cases.length}));
