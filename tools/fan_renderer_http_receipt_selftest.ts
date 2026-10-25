/** Actual Fan adapter consumes recorded native WinHTTP replies; no live Host or hardware. */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { http, type HttpResponse } from '../src/bridge/api';
import { FanApiError, HttpFanApiAdapter } from '../src/bridge/fanApi';
import { FanHostLifecycle, resolveFanHostConfig } from '../src/bridge/fanHost';
import { NoIoHost } from './fixtures/fan_no_io_host';
const root=process.env.YMCC_FAN_HTTP_RECEIPT_EVIDENCE_DIR;
assert(root,'explicit evidence root is required');
const network=JSON.parse(fs.readFileSync(root+'/network-bound/results.json','utf8'));
assert(network.ok&&network.checks===14);
const adapter=new HttpFanApiAdapter('http://127.0.0.1:8765','fixture-session');
const original=http.request;let response:HttpResponse,requests=0;
http.request=async()=>{requests++;return response;};
const curve=[{tempC:0,dutyPercent:100},{tempC:40,dutyPercent:100},{tempC:70,dutyPercent:100},{tempC:100,dutyPercent:100}];
const lease='fixture-lease-0001';
const row=(kind:string,mode='post')=>network.rows.find((r:{kind:string;mode:string})=>r.kind===kind&&r.mode===mode) as HttpResponse;
let checks=0;const failures:string[]=[];
async function check(name:string,run:()=>Promise<void>){checks++;try{await run();console.log('PASS '+name);}catch(e){failures.push(name+': '+String(e));console.log('FAIL '+name+': '+String(e));}}
async function rejects(call:()=>Promise<unknown>,code:string|undefined,status=200){let error:unknown;try{await call();}catch(e){error=e;}assert(error instanceof FanApiError);assert.equal(error.errorCode,code);assert.equal(error.status,status);}
async function main(){try{
 for(const name of ['normal','chunked','unicode'])await check('complete recorded '+name+' reply still accepted',async()=>{response=row(name);assert.equal((await adapter.enable(curve,lease)).state,'Ready');});
 for(const name of ['partial','partial-chunked','slow-body','cancel-body'])await check('recorded '+name+' POST is unknown and never promoted',async()=>{response=row(name);await rejects(()=>adapter.enable(curve,lease),'FAN_HTTP_RESULT_UNKNOWN');});
 await check('recorded incomplete GET is an invalid observation',async()=>{response=row('partial','get');await rejects(()=>adapter.getState(),'FAN_API_RESPONSE_INVALID');});
 await check('recorded incomplete Close is not a successful cleanup receipt',async()=>{response=row('partial-close','close');await rejects(()=>adapter.close(),'FAN_HTTP_RESULT_UNKNOWN');});
 await check('recorded incomplete 401 retains auth status',async()=>{response=row('partial-denied');await rejects(()=>adapter.enable(curve,lease),undefined,401);});
 await check('incomplete handshake is never a control gate',async()=>{response={status:200,headers:'',body:JSON.stringify({ok:true,supported:true,protocolVersion:2}),bodyComplete:false};await rejects(()=>adapter.handshake(),'FAN_API_RESPONSE_INVALID');});
 await check('legacy native without body marker remains compatible',async()=>{const {bodyComplete:ignored,...legacy}=row('normal');void ignored;response=legacy;assert.equal((await adapter.enable(curve,lease)).state,'Ready');});
 await check('malformed body marker cannot become proof',async()=>{response={...row('normal'),bodyComplete:'true' as unknown as boolean};await rejects(()=>adapter.enable(curve,lease),'FAN_HTTP_RESULT_UNKNOWN');});
 await check('next valid manual adapter call is still reachable',async()=>{response=row('normal');assert.equal((await adapter.open()).state,'Ready');});
 await check('actual lifecycle never automatically replays an unconfirmed POST and next explicit manual succeeds',async()=>{
  const backend=new NoIoHost();let incomplete=true;
  http.request=async(url,options={})=>{
   const path=new URL(url).pathname,body=options.body?JSON.parse(options.body):{};let result:unknown;
   if(path==='/api/handshake')result=await backend.handshake();
   else if(path==='/api/state')result={ok:true,state:await backend.getState()};
   else if(path==='/api/open')result={ok:true,state:await backend.open()};
   else if(path==='/api/open-events')result={ok:true,state:await backend.openEvents()};
   else if(path==='/api/acquire-control'){const acquired=await backend.acquireControl();acquired.leaseId='fixture-lease-'+acquired.generation;backend.lease=acquired;result={ok:true,lease:acquired};}
   else if(path==='/api/heartbeat')result={ok:true,lease:await backend.heartbeat()};
   else if(path==='/api/enable'){result={ok:true,state:await backend.enable()};if(incomplete){incomplete=false;return {status:200,headers:'',body:JSON.stringify(result),bodyComplete:false};}}
   else if(path==='/api/restore')result={ok:true,state:await backend.restoreOem()};
   else if(path==='/api/disable'||path==='/api/cancel-control-intent')result={ok:true,state:await backend.disable(),controlIntentCancelled:true};
   else if(path==='/api/release-control')result={ok:true,state:await backend.releaseControl()};
   else if(path==='/api/close')result={ok:true,state:await backend.close()};
   else if(path==='/api/shutdown'){await backend.shutdown();result={ok:true};}
   else throw new Error('unexpected fixture route '+path+' '+String(body));
   return {status:200,headers:'',body:JSON.stringify(result),bodyComplete:true};
  };
  const life=new FanHostLifecycle({enabled:true,adapter:new HttpFanApiAdapter('http://127.0.0.1:8765','a'.repeat(64)),config:{...resolveFanHostConfig(),sessionToken:'a'.repeat(64)},heartbeatIntervalMs:60000,readNativeActivity:async()=>null,readNativePowerState:async()=>({generation:0,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),launcher:{async start(){return {pid:101111,executable:'fixture-only.exe'};},async stop(){}}});
  try{await life.start();await rejects(()=>life.apply(curve),'FAN_HTTP_RESULT_UNKNOWN');assert.equal(backend.calls.filter(x=>x==='enable').length,1);await life.apply(curve);assert.equal(backend.calls.filter(x=>x==='enable').length,2);assert.equal(life.state,'ready');}
  finally{await life.close().catch(()=>{});http.request=async()=>{requests++;return response;};}
 });
 }finally{http.request=original;}
 console.log(JSON.stringify({ok:failures.length===0,checks,failures,requests,hardwareCalls:0,scope:'actual public HTTP adapter + actual lifecycle, recorded native WinHTTP packets; transport and Host for lifecycle are explicit fixtures; no real IPC/HC/device/CPU benchmark'}));if(failures.length)process.exitCode=1;
}
void main();
