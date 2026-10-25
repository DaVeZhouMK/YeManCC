/** Actual adapter -> shared HTTP -> IPC; in-memory native replies only, no live HTTP/device. */
import assert from 'node:assert/strict';
type Headers=Record<string,string>;
type Message={id:number;cmd:string;args:{url:string;method:string;headers:Headers;body?:string;timeoutMs:number}};
const listeners:Array<(e:{data:unknown})=>void>=[],requests:Array<{origin:string;path:string;headers:Headers;wireToken:string|undefined;body?:string;timeout:number}>=[];
const servers=new Map<string,{token:string|undefined;backend:any;revoked:boolean;sequence:number}>();
let holdNext=false;const releases:Array<()=>void>=[];
function deliver(id:number,result:unknown){for(const listener of listeners)listener({data:{id,result}});}
const webview={addEventListener(_type:string,listener:(e:{data:unknown})=>void){listeners.push(listener);},postMessage(raw:unknown){
 const m=raw as Message;if(m.cmd==='fanLog.write'){queueMicrotask(()=>deliver(m.id,true));return;}
 if(m.cmd!=='http.request')throw new Error('unexpected inert IPC '+m.cmd);
 const url=new URL(m.args.url),server=servers.get(url.origin);if(!server)throw new Error('unregistered inert origin');
 const token=m.args.headers?.['X-YeMan-Fan-Session'],expected=server.token,refused=server.revoked||!expected||token!==expected;
 requests.push({origin:url.origin,path:url.pathname,headers:m.args.headers,wireToken:token,body:m.args.body,timeout:m.args.timeoutMs});
 const send=async()=>{let reply:unknown,status=200;const body=m.args.body?JSON.parse(m.args.body):{};const h=server.backend;
  if(refused){status=401;reply={ok:false,error:{code:'AUTH_REJECTED',message:'inert fixture credential refused'}};}
  else switch(url.pathname){
   case'/api/handshake':reply=await h.handshake();break;
   case'/api/state':reply={ok:true,state:{...await h.getState(),fixtureSequence:++server.sequence}};break;
   case'/api/open':reply={ok:true,state:await h.open()};break;
   case'/api/open-events':reply={ok:true,state:await h.openEvents()};break;
   case'/api/acquire-control':{const lease=await h.acquireControl();lease.leaseId='fixture-session-lease-'+lease.generation;h.lease=lease;reply={ok:true,lease};break;}
   case'/api/heartbeat':reply={ok:true,lease:{leaseId:body.leaseId,generation:1,expiresAtMonoMs:10000+(++server.sequence)}};break;
   case'/api/enable':reply={ok:true,state:await h.enable(body.nodes,body.leaseId)};break;
   case'/api/preset':reply={ok:true,state:await h.applyPreset(body.name,body.leaseId,body.nodes)};break;
   case'/api/disable':reply={ok:true,state:await h.disable()};break;
   case'/api/cancel-control-intent':reply={ok:true,controlIntentCancelled:true,state:await h.disable()};break;
   case'/api/restore':reply={ok:true,state:await h.restoreOem()};break;
   case'/api/release-control':reply={ok:true,state:await h.releaseControl()};break;
   case'/api/close':reply={ok:true,state:await h.close()};break;
   case'/api/shutdown':reply={ok:true};break;
   default:throw new Error('unexpected inert Fan endpoint '+url.pathname);
  }
  deliver(m.id,{status,headers:'Content-Type: application/json',body:JSON.stringify(reply),bodyComplete:true});};
 if(holdNext){holdNext=false;releases.push(()=>{void send();});}else queueMicrotask(()=>{void send();});
}};
Object.assign(globalThis,{window:{chrome:{webview},dispatchEvent(){return true;},addEventListener(){},removeEventListener(){}}});
const{HttpFanApiAdapter,FanApiError}=await import('../src/bridge/fanApi');
const{NoIoHost}=await import('./fixtures/fan_no_io_host');
const{rejectAllPending}=await import('../src/bridge/ipc');
let sequence=0;
function fixture(token?:string){if(arguments.length===0)token='fixture-session-A';
 const origin='http://127.0.0.1:'+(19000+(++sequence)),backend=new NoIoHost();backend.generation=0;backend.attempt=0;backend.powerState='Unknown';
 const server={token,backend,revoked:false,sequence:0};servers.set(origin,server);return{origin,server,adapter:new HttpFanApiAdapter(origin,token),rows:()=>requests.filter(r=>r.origin===origin)};
}
const last=(f:ReturnType<typeof fixture>)=>f.rows().at(-1)!;
const results:any[]=[];
async function check(name:string,work:()=>Promise<unknown>){try{results.push({name,ok:true,evidence:await work()});}catch(error){results.push({name,ok:false,error:String(error)});}finally{while(releases.length)releases.shift()!();}}
async function denied(work:()=>Promise<unknown>){let error:unknown;try{await work();}catch(e){error=e;}assert(error instanceof FanApiError);assert.equal(error.status,401);}
await check('stable GETs reuse one immutable header while every response remains fresh',async()=>{const f=fixture();const states=await Promise.all([f.adapter.getState(),f.adapter.getState(),f.adapter.getState()]);assert.equal(new Set(f.rows().map(x=>x.headers)).size,1,'stable credential requests rebuilt identical header objects');assert(Object.isFrozen(last(f).headers),'shared request header is mutable');assert.equal(new Set(states.map(s=>s.fixtureSequence)).size,3);return{requests:3,headerObjects:1,freshReplies:3};});
await check('heartbeat reuses fixed headers but still serializes and validates each lease',async()=>{const f=fixture();const a=await f.adapter.heartbeat('fixture-lease-A'),b=await f.adapter.heartbeat('fixture-lease-B');assert.equal(new Set(f.rows().map(x=>x.headers)).size,1);assert.equal(a.leaseId,'fixture-lease-A');assert.equal(b.leaseId,'fixture-lease-B');assert.notEqual(a.expiresAtMonoMs,b.expiresAtMonoMs);assert.notEqual(f.rows()[0].body,f.rows()[1].body);return{fixedHeaderObjects:1,liveBodies:2,liveLeaseReplies:2};});
await check('setting the same credential does not recreate a header',async()=>{const f=fixture();await f.adapter.getState();const before=last(f).headers;f.adapter.setSessionToken('fixture-session-A');await f.adapter.getState();assert.equal(last(f).headers,before);return{sameTokenNoRebuild:true};});
await check('credential rotation replaces the whole header and leaves the old receipt unchanged',async()=>{const f=fixture();await f.adapter.getState();const old=last(f).headers;f.server.token='fixture-session-B';f.adapter.setSessionToken(f.server.token);await f.adapter.getState();assert.notEqual(last(f).headers,old);assert.equal(old['X-YeMan-Fan-Session'],'fixture-session-A');assert.equal(last(f).wireToken,'fixture-session-B');return{newCredentialEffective:true,oldHeaderPreserved:true};});
await check('clearing a credential removes the header and cannot reuse prior authorization',async()=>{const f=fixture();await f.adapter.getState();const old=last(f).headers;f.adapter.setSessionToken('');await denied(()=>f.adapter.handshake());assert(!Object.hasOwn(last(f).headers,'X-YeMan-Fan-Session'));assert.equal(old['X-YeMan-Fan-Session'],'fixture-session-A');f.adapter.setSessionToken('fixture-session-A');assert((await f.adapter.handshake()).supported);return{clearRefused:true,correctedManualHandshake:true};});
for(const initial of [undefined,''])await check(`${initial===undefined?'missing':'empty'} constructor credential stays absent and can be supplied later`,async()=>{const f=fixture(initial);await denied(()=>f.adapter.getState());assert(!Object.hasOwn(last(f).headers,'X-YeMan-Fan-Session'));f.server.token='supplied-later';f.adapter.setSessionToken(f.server.token);assert((await f.adapter.handshake()).supported);return{initialAbsent:true,suppliedLater:true};});
for(const token of ['MiXeD_Case-012',' leading-and-trailing ','   ','凭据🌬️'])await check('credential wire text remains exact at the actual IPC boundary '+JSON.stringify(token),async()=>{const f=fixture(token);await f.adapter.handshake();assert.equal(last(f).wireToken,token);assert.equal(last(f).headers['Content-Type'],'application/json');return{exactTextPreserved:true,scope:'inert IPC serialization, not a WinHTTP Unicode-header acceptance test'};});
await check('two adapters do not share credential header storage',async()=>{const a=fixture('same-synthetic-token'),b=fixture('same-synthetic-token');await a.adapter.getState();await b.adapter.getState();assert.notEqual(last(a).headers,last(b).headers);a.server.token='only-A-rotated';a.adapter.setSessionToken(a.server.token);await a.adapter.getState();await b.adapter.getState();assert.equal(last(b).wireToken,'same-synthetic-token');return{perAdapterStorage:true,rotationIsolated:true};});
await check('held request keeps its original header after rotation and a new request uses the successor',async()=>{const f=fixture();holdNext=true;const pending=f.adapter.getState();const old=last(f).headers;assert.equal(releases.length,1);f.server.token='successor-token';f.adapter.setSessionToken(f.server.token);await f.adapter.getState();assert.notEqual(last(f).headers,old);assert.equal(old['X-YeMan-Fan-Session'],'fixture-session-A');releases.shift()!();assert((await pending).state);return{oldInFlightPreserved:true,newRequestUsesSuccessor:true};});
await check('revocation is checked for every request despite stable header identity',async()=>{const f=fixture();await f.adapter.handshake();const header=last(f).headers;f.server.revoked=true;await denied(()=>f.adapter.getState());assert.equal(last(f).headers,header);f.server.revoked=false;assert((await f.adapter.handshake()).supported);return{revocationNotCached:true,correctedHandshake:true};});
await check('consumer mutation cannot poison later request headers',async()=>{const f=fixture();await f.adapter.getState();const header=last(f).headers;assert.equal(Reflect.set(header,'X-YeMan-Fan-Session','poison'),false);assert.equal(Reflect.deleteProperty(header,'Content-Type'),false);await f.adapter.getState();assert.equal(last(f).wireToken,'fixture-session-A');assert.equal(last(f).headers['Content-Type'],'application/json');return{immutableHeader:true,laterRequestPreserved:true};});
await check('ordinary endpoint changes keep fixed header identity and live method/body/deadline',async()=>{const f=fixture();await f.adapter.handshake(300);await f.adapter.open();await f.adapter.openEvents();const lease=await f.adapter.acquireControl();await f.adapter.enable([{tempC:0,dutyPercent:100}],lease.leaseId);await f.adapter.getState(499);await f.adapter.close();assert.equal(new Set(f.rows().map(x=>x.headers)).size,1);assert.deepEqual(f.rows().map(x=>x.timeout),[300,16000,16000,5000,5000,499,45000]);assert.equal(f.rows()[0].body,undefined);assert.equal(f.rows().at(-2)?.body,undefined);return{endpoints:7,fixedHeaderObjects:1,liveDeadlinesPreserved:true};});
rejectAllPending('inert session header tests completed');
console.log(JSON.stringify({ok:results.every(x=>x.ok),checks:results.length,results,hardwareCalls:0,cpuBenchmark:false,scope:'actual HttpFanApiAdapter -> shared http.request -> IPC postMessage; native/Host replies and current token verifier are inert; no live Host, network, HC or device operations, no CPU/allocation benchmark'}));
if(results.some(x=>!x.ok))process.exitCode=1;
