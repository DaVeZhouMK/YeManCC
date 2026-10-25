/** Real HTTP adapter + in-memory HTTP reply/Host; no IPC, network, HC or hardware. */
import assert from 'node:assert/strict';
import {http,type HttpResponse} from '../src/bridge/api';
import {HttpFanApiAdapter,FanApiError,type FanNode} from '../src/bridge/fanApi';
import {FanHostLifecycle,resolveFanHostConfig} from '../src/bridge/fanHost';
import {NoIoHost} from './fixtures/fan_no_io_host';
const fields=['hardwareWrites','hardwareWritesEnabled','hardwareWritesObserved','unknownState','openCalled','openEventsCalled','oemRestoreConfirmed','hcVirtualCloseReturned','hcDeviceManagerStopCompleted','oemPhysicalOwnershipConfirmed','hcCloseCleanupPending','controlAccepting'] as const;
const originalRequest=http.request,originalSome=Array.prototype.some,originalTest=RegExp.prototype.test;
let fieldCallbackTraversals=0,requests=0,sequence=0,live:NoIoHost|null=null;
const regexObjects=new Set<RegExp>(),results:Array<{name:string;ok:boolean;error?:string;evidence?:unknown}>=[];
let reply:unknown={ok:true,state:{state:'Ready'}},status=200;
(Array.prototype as any).some=function(callback:unknown,thisArg?:unknown){if(this.length===fields.length&&this[0]===fields[0]&&this[this.length-1]===fields[fields.length-1])fieldCallbackTraversals++;return originalSome.call(this,callback as any,thisArg);};
RegExp.prototype.test=function(value:string){if(this.source==='^[A-Za-z0-9_-]{8,80}$'&&this.flags==='')regexObjects.add(this);return originalTest.call(this,value);};
const lease=()=>({leaseId:'inert-validator-lease-001',generation:++sequence,expiresAtMonoMs:15000+sequence,owner:'fixture'});
http.request=async(url,options):Promise<HttpResponse>=>{
 requests++;let body=reply;const path=new URL(url).pathname,h=live;
 if(h){const input=options?.body?JSON.parse(options.body):{};
  switch(path){
   case'/api/handshake':body=await h.handshake();break;
   case'/api/state':body={ok:true,state:await h.getState()};break;
   case'/api/open':body={ok:true,state:await h.open()};break;
   case'/api/open-events':body={ok:true,state:await h.openEvents()};break;
   case'/api/acquire-control':{const value=await h.acquireControl();value.leaseId='inert-manual-lease-'+value.generation;h.lease=value;body={ok:true,lease:value};break;}
   case'/api/enable':body={ok:true,state:await h.enable(input.nodes,input.leaseId)};break;
   case'/api/disable':body={ok:true,state:await h.disable()};break;
   case'/api/restore':body={ok:true,state:await h.restoreOem()};break;
   case'/api/release-control':body={ok:true,state:await h.releaseControl()};break;
   case'/api/close':body={ok:true,state:await h.close()};break;
   case'/api/shutdown':body={ok:true};break;
   default:throw Error('unexpected inert endpoint '+path);
  }
 }
 return{status,headers:'Content-Type: application/json',body:JSON.stringify(body)};
};
const adapter=new HttpFanApiAdapter('http://127.0.0.1:11930','inert-validator-session');
async function check(name:string,run:()=>Promise<unknown>){try{results.push({name,ok:true,evidence:await run()});}catch(error){results.push({name,ok:false,error:String(error)});}finally{status=200;live=null;}}
async function invalid(call:()=>Promise<unknown>,code='FAN_API_RESPONSE_INVALID',expectedStatus=200){await assert.rejects(call,error=>error instanceof FanApiError&&error.errorCode===code&&error.status===expectedStatus);}
try{
 await check('each real status response stays fresh while the fixed lease matcher is reused',async()=>{
  const beforeRequests=requests,start=regexObjects.size;for(let i=0;i<4;i++){const value=lease();reply={ok:true,state:{state:'Ready',lease:value,leaseGeneration:value.generation}};assert.equal((await adapter.getState()).lease?.generation,value.generation);}
  assert.equal(requests-beforeRequests,4);assert.equal(regexObjects.size-start,1,'fixed lease matcher recreated for each status');return{freshResponses:4,matcherObjects:1};
 });
 await check('state boolean checks do not allocate a new array callback per response',async()=>{const before=fieldCallbackTraversals;for(let i=0;i<3;i++){reply={ok:true,state:{state:'Ready',hardwareWritesEnabled:i%2===0}};assert.equal((await adapter.getState()).hardwareWritesEnabled,i%2===0);}assert.equal(fieldCallbackTraversals-before,0,'state response rebuilt array traversal callback');return{requests:3,arrayCallbackTraversals:0};});
 for(const field of fields)await check('optional boolean contract remains exact: '+field,async()=>{
  for(const value of [true,false]){reply={ok:true,state:{state:'Ready',[field]:value}};assert.equal((await adapter.getState())[field],value);}
  reply={ok:true,state:{state:'Ready'}};assert.equal((await adapter.getState())[field],undefined);
  for(const value of [null,0,1,'false',{},[]]){reply={ok:true,state:{state:'Ready',[field]:value}};await invalid(()=>adapter.getState());}
  return{field,booleansAccepted:2,omittedAccepted:true,wrongWireTypesRejected:6};
 });
 await check('lease grammar and current lease fields retain their boundary checks',async()=>{
  for(const leaseId of ['A'.repeat(8),'_a-019ZX','z'.repeat(80)]){reply={ok:true,state:{state:'Ready',lease:{...lease(),leaseId}}};assert.equal((await adapter.getState()).lease?.leaseId,leaseId);}
  for(const leaseId of ['a'.repeat(7),'a'.repeat(81),'with space','中'.repeat(8),'bad@token']){reply={ok:true,state:{state:'Ready',lease:{...lease(),leaseId}}};await invalid(()=>adapter.getState());}
  for(const update of [{generation:0},{generation:-1},{generation:1.5},{expiresAtMonoMs:0},{expiresAtMonoMs:-1},{owner:1}]){reply={ok:true,state:{state:'Ready',lease:{...lease(),...update}}};await invalid(()=>adapter.getState());}
  reply={ok:true,state:{state:'Ready',lease:lease()}};assert.equal((await adapter.getState()).state,'Ready');return{validTokenBoundaries:3,invalidTokens:5,invalidLeaseFields:6,nextReadReachable:true};
 });
 await check('heartbeat still binds the reply to this request lease instead of caching validity',async()=>{
  const a=lease(),b={...lease(),leaseId:'inert-validator-lease-002'};reply={ok:true,lease:a};assert.equal((await adapter.heartbeat(a.leaseId)).generation,a.generation);reply={ok:true,lease:b};await invalid(()=>adapter.heartbeat(a.leaseId),'FAN_HTTP_RESULT_UNKNOWN');reply={ok:true,lease:b};assert.equal((await adapter.heartbeat(b.leaseId)).generation,b.generation);return{currentLeaseMatchRequired:true,newCurrentLeaseAccepted:true};
 });
 await check('authentication failures and unknown write result keep their original classifications',async()=>{status=401;reply={ok:false,error:{code:'API_SESSION_REQUIRED',message:'inert rejected'}};await invalid(()=>adapter.enable([{tempC:0,dutyPercent:100}]),'API_SESSION_REQUIRED',401);status=200;reply={ok:true,state:{state:'Ready',controlAccepting:0}};await invalid(()=>adapter.enable([{tempC:0,dutyPercent:100}]),'FAN_HTTP_RESULT_UNKNOWN');reply={ok:true,state:{state:'Ready'}};assert.equal((await adapter.open()).state,'Ready');return{authNotBypassed:true,unknownWriteNotPromoted:true,nextManualOpenReachable:true};});
 await check('real adapter/lifecycle still completes manual ON -> OFF -> fresh ON',async()=>{
  const host=new NoIoHost();live=host;const nodes:FanNode[]=[0,40,70,100].map(tempC=>({tempC,dutyPercent:75}));
  const life=new FanHostLifecycle({enabled:true,adapter,config:{...resolveFanHostConfig(),sessionToken:'c'.repeat(64)},heartbeatIntervalMs:60000,readNativeActivity:async()=>null,readNativePowerState:async()=>({generation:0,phase:'ready',resumeReady:false,hardwareWritesAllowed:true,inputReady:true,hibernateAvailable:true}),launcher:{async start(){return{pid:119301,executable:'inert-validator-host.exe'};},async stop(){}}});
  try{await life.start({manualRecovery:true});await life.apply(nodes);await life.disable();await life.apply(nodes);assert.equal(host.calls.filter(x=>x==='enable').length,2);assert.equal(life.state,'ready');return{manualEnables:2,offBetween:true,actualHttpAdapter:true};}finally{await life.close().catch(()=>{});}
 });
}finally{http.request=originalRequest;Array.prototype.some=originalSome;RegExp.prototype.test=originalTest;}
console.log(JSON.stringify({ok:results.every(x=>x.ok),checks:results.length,results,hardwareCalls:0,cpuBenchmark:false,scope:'actual HTTP adapter and public lifecycle, inert HTTP/native/Host; matcher reference and Array.some interception are structural checks, not CPU/time/allocation benchmarks'}));if(results.some(x=>!x.ok))process.exitCode=1;
