/** FAN976: public HTTP adapter with fake HTTP responses only; no native IPC/Host/device. */
import assert from 'node:assert/strict';
import { http, type HttpResponse } from '../src/bridge/api';
import { FanApiError, HttpFanApiAdapter, type FanState } from '../src/bridge/fanApi';

const originalRequest = http.request;
const adapter = new HttpFanApiAdapter('http://127.0.0.1:8765', 'fixture-session-not-a-production-token');
const curve = [{tempC:0,dutyPercent:20},{tempC:40,dutyPercent:40},{tempC:70,dutyPercent:75},{tempC:100,dutyPercent:100}];
const state = { state: 'Ready', hardwareWrites: false, hardwareWritesEnabled: true, openCalled: true, openEventsCalled: true, leaseGeneration: 1 };
const lease = { leaseId: 'fixture-lease-0001', generation: 1, expiresAtMonoMs: 15000, owner: 'fixture' };
let response: HttpResponse;
let transportFailure: Error | null = null;
let requests = 0;
http.request = async () => { requests += 1; if (transportFailure) throw transportFailure; return response; };
function setReply(body: unknown, status = 200): void { response = {status, headers:'Content-Type: application/json\r\n', body:JSON.stringify(body)}; transportFailure = null; }
function setRaw(body: string, status = 200): void { response = {status, headers:'', body}; transportFailure = null; }
const failures: string[] = [];
let checks = 0;
async function check(name: string, work: () => Promise<void>): Promise<void> {
  try { await work(); checks += 1; } catch (error) { failures.push(`${name}: ${error instanceof Error ? error.message : String(error)}`); }
}
async function invalid(call: () => Promise<unknown>, code: string, status?: number): Promise<void> {
  let error: unknown;
  try { await call(); } catch (caught) { error = caught; }
  assert(error instanceof FanApiError, 'malformed response must produce structured failure instead of success/raw TypeError');
  assert.equal(error.errorCode, code);
  if (status !== undefined) assert.equal(error.status, status);
}
const routes: Array<[string, () => Promise<FanState>]> = [
  ['open',()=>adapter.open()], ['open-events',()=>adapter.openEvents()],
  ['enable',()=>adapter.enable(curve,lease.leaseId)], ['preset',()=>adapter.applyPreset('balanced',lease.leaseId,curve)],
  ['disable',()=>adapter.disable(lease.leaseId)], ['restore',()=>adapter.restoreOem(lease.leaseId)],
  ['release',()=>adapter.releaseControl(lease.leaseId)], ['suspend',()=>adapter.suspend({generation:1,source:'fixture'})],
  ['resume',()=>adapter.resume({generation:1,source:'fixture'})], ['close',()=>adapter.close()],
];
try {
  await check('handshake supported',async()=>{setReply({ok:true,supported:true,protocolVersion:2});assert.equal((await adapter.handshake()).supported,true);});
  await check('handshake honest unsupported',async()=>{setReply({ok:true,supported:false});assert.equal((await adapter.handshake()).supported,false);});
  for (const [name,call] of routes) {
    await check(`${name} valid receipt`,async()=>{setReply({ok:true,state});assert.deepEqual(await call(),state);});
    await check(`${name} missing receipt`,async()=>{setReply({ok:true});await invalid(call,'FAN_HTTP_RESULT_UNKNOWN');});
  }
  await check('state valid receipt',async()=>{setReply({ok:true,state});assert.deepEqual(await adapter.getState(),state);});
  await check('state future optional fields allowed',async()=>{setReply({ok:true,state:{state:'FutureFamilyState',lease:null,futureField:{value:true}}});assert.equal((await adapter.getState()).state,'FutureFamilyState');});
  await check('legacy root lifecycle state remains compatible',async()=>{setReply(state);assert.equal((await adapter.open()).state,'Ready');});
  await check('legacy missing ok with valid state remains compatible',async()=>{setReply({state});assert.equal((await adapter.getState()).state,'Ready');});
  for (const badState of [null, [], 'Ready', {}, {state:''}, {state:7}, {...state,hardwareWritesEnabled:'true'}, {...state,lease:{leaseId:''}}]) {
    await check(`state malformed ${JSON.stringify(badState)}`,async()=>{setReply({ok:true,state:badState});await invalid(()=>adapter.getState(),'FAN_API_RESPONSE_INVALID');});
  }
  for (const name of ['acquire','heartbeat'] as const) {
    const call = () => name==='acquire' ? adapter.acquireControl() : adapter.heartbeat(lease.leaseId);
    await check(`${name} valid lease`,async()=>{setReply({ok:true,lease});assert.deepEqual(await call(),lease);});
    await check(`${name} legacy result lease`,async()=>{setReply({ok:true,result:{lease}});assert.deepEqual(await call(),lease);});
    for (const badLease of [undefined,null,[],{}, {...lease,leaseId:''}, {...lease,leaseId:4}, {...lease,leaseId:' spaces '}, {...lease,generation:'1'}, {...lease,generation:-1}, {...lease,expiresAtMonoMs:'15000'}]) {
      await check(`${name} bad lease ${JSON.stringify(badLease)}`,async()=>{setReply({ok:true,lease:badLease});await invalid(call,'FAN_HTTP_RESULT_UNKNOWN');});
    }
  }

  await check('heartbeat cannot rotate identity',async()=>{setReply({ok:true,lease:{...lease,leaseId:'fixture-other-0002'}});await invalid(()=>adapter.heartbeat(lease.leaseId),'FAN_HTTP_RESULT_UNKNOWN');});
  for (const body of ['null','[]','"Ready"','7','{"ok":true,','']) {
    await check(`GET malformed JSON/top-level ${body}`,async()=>{setRaw(body);await invalid(()=>adapter.getState(),'FAN_API_RESPONSE_INVALID');});
    await check(`POST malformed JSON/top-level ${body}`,async()=>{setRaw(body);await invalid(()=>adapter.enable(curve,lease.leaseId),'FAN_HTTP_RESULT_UNKNOWN');});
  }
  await check('invalid ok type is not success',async()=>{setReply({ok:'true',state});await invalid(()=>adapter.enable(curve,lease.leaseId),'FAN_HTTP_RESULT_UNKNOWN');});
  await check('401 preserves refusal even non-JSON',async()=>{setRaw('not-json',401);let error:unknown;try{await adapter.open();}catch(e){error=e;}assert(error instanceof FanApiError);assert.equal(error.status,401);assert.notEqual(error.errorCode,'FAN_HTTP_RESULT_UNKNOWN');});
  await check('403 Host code preserved',async()=>{setReply({ok:false,error:{code:'API_SESSION_REQUIRED',message:'fixture refused'}},403);await invalid(()=>adapter.enable(curve),'API_SESSION_REQUIRED',403);});
  await check('409 resuming code preserved',async()=>{setReply({ok:false,error:{code:'POWER_RESUMING',message:'fixture resuming'}},409);await invalid(()=>adapter.resume({generation:1,source:'fixture'}),'POWER_RESUMING',409);});
  await check('transport timeout retains existing unknown classification',async()=>{setReply({});transportFailure=new Error('IPC command timed out: http.request');await invalid(()=>adapter.enable(curve),'FAN_HTTP_RESULT_UNKNOWN');});
  await check('cancel intent requires cancellation and state receipt',async()=>{setReply({ok:true,controlIntentCancelled:true});await invalid(()=>adapter.disable(undefined,{controlIntentOnly:true}),'FAN_HTTP_RESULT_UNKNOWN');});
  await check('cancel intent valid',async()=>{setReply({ok:true,controlIntentCancelled:true,state:{...state,state:'Suspended'}});assert.equal((await adapter.disable(undefined,{controlIntentOnly:true})).state,'Suspended');});
  for (const name of ['suspend','resume'] as const) {
    await check(`${name} invalid source never sends HTTP`,async()=>{setReply({ok:true,state});const before=requests;await assert.rejects(()=>adapter[name]({generation:1,source:null as unknown as string}),/POWER_GENERATION_REQUIRED/);assert.equal(requests,before);});
  }
  await check('next valid call still reachable after all malformed receipts',async()=>{setReply({ok:true,state});assert.equal((await adapter.open()).state,'Ready');});
} finally { http.request = originalRequest; }
console.log(JSON.stringify({ok:failures.length===0,checks,failed:failures.length,scope:'public-adapter-fake-http-no-ipc-no-hardware',failures},null,2));
if (failures.length) process.exitCode=1;

