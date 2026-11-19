/** Actual Fan API + diagnostics in a VM; all HTTP/IPC are inert fixture ports. */
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import {createRequire} from 'node:module';
const repo=path.resolve(import.meta.dirname,'..');
const args=process.argv.slice(2);let out,apiSource,fieldState;
for(let i=0;i<args.length;i+=2){if(args[i]==='--out')out=path.resolve(args[i+1]);else if(args[i]==='--api-source')apiSource=path.resolve(args[i+1]);else if(args[i]==='--field-state')fieldState=path.resolve(args[i+1]);else throw Error('Usage: --out path [--api-source path] [--field-state path]');}
if(!out)throw Error('--out is required');fs.mkdirSync(out,{recursive:true});
const require=createRequire(path.join(repo,'package.json')),ts=require('typescript'),vue=require('vue');
const files={fanApi:apiSource??path.join(repo,'src/bridge/fanApi.ts'),fanDiagnostics:path.join(repo,'src/bridge/fanDiagnostics.ts')};
const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
const source=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,fs.readFileSync(p)]));
const bindings=Object.fromEntries(Object.entries(files).map(([k,p])=>[k,{path:p,sha256:digest(source[k])}]));
const rows=[],httpCalls=[];let reply,error,diagnosticFailure=false;
const context=vm.createContext({console,Date,Error,TypeError,RangeError,Promise,structuredClone,URL,Number,String,Object,Array,Boolean,Math,JSON}, {codeGeneration:{strings:false,wasm:false}});
const cache=new Map();
function load(name){if(cache.has(name))return cache.get(name).exports;const m={exports:{}};cache.set(name,m);
 const text=ts.transpileModule(source[name].toString('utf8'),{compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.CommonJS},fileName:files[name]}).outputText;
 const req=id=>{if(id==='vue')return vue;if(id==='./api')return {http:{request:async(url,options)=>{httpCalls.push({url,options});if(error)throw error;return reply;}}};
  if(id==='./ipc')return {isNativeRuntime:true,invoke:async(command,args)=>{assert.equal(command,'fanLog.write');if(diagnosticFailure)throw Error('fixture diagnostic IPC');rows.push(JSON.parse(JSON.stringify(args)));return {ok:true};}};
  if(id==='./fanDiagnostics')return load('fanDiagnostics');throw Error('UNREVIEWED_MODULE '+id);};
 new vm.Script('(function(require,module,exports){'+text+'\n})',{filename:files[name]}).runInContext(context)(req,m,m.exports);return m.exports;
}
const api=load('fanApi'),diag=load('fanDiagnostics');const adapter=new api.HttpFanApiAdapter('http://fixture.invalid','SECRET_SESSION_VALUE');
const checks=[],failures=[],syntheticRows=[];async function check(name,fn){try{await fn();checks.push({name,pass:true});for(const r of rows)syntheticRows.push({time:'2026-10-19T12:00:00.000',...r,fixtureCase:name,evidenceScope:'synthetic-memory-ports-not-field-observation'});}catch(e){checks.push({name,pass:false,error:e.message});failures.push(name);}}
const ready={state:'Ready',openCalled:true,openEventsCalled:true,hardwareWritesEnabled:true};
function reset(body={ok:true,state:ready},status=200){rows.length=0;httpCalls.length=0;error=null;diagnosticFailure=false;reply={status,body:JSON.stringify(body),headers:'X-YeMan-Fan-Request-Id: fixture-request-1',bodyComplete:true};diag.adoptFanDiagnosticLoggingState(true);}
function row(event){const r=rows.find(r=>r.event===event);assert(r,'Missing '+event);return r.details;}
const unavailable={ok:true,supported:false,protocolVersion:2,hostMode:'real-hc-unavailable',hardwareCapable:false,hardwareWritesEnabled:false,hardwareWritesObserved:false,mockZeroHardwareEvidence:false};
await check('unavailable-handshake-preserves-supported-false',async()=>{reset(unavailable);const r=await adapter.handshake();assert.equal(r.supported,false);const d=row('api.response');assert.equal(d.response.supported,false);assert.equal(d.receipt.supported,false);assert.equal(d.response.hardwareCapable,false);assert.equal(d.receiptValid,true);assert.equal(d.observationSchemaVersion,2);assert.equal(httpCalls.length,1);});
await check('initial-handshake-budget-covers-Host-15s-without-extending-health-or-state',async()=>{reset(unavailable);await adapter.handshake();assert.equal(httpCalls.at(-1).options.timeoutMs,16000);reset();await adapter.getState();assert.equal(httpCalls.at(-1).options.timeoutMs,5000);reset(unavailable);await adapter.handshake(300);assert.equal(httpCalls.at(-1).options.timeoutMs,300);});
await check('healthy-handshake-preserves-true-without-hardware-promotion',async()=>{reset({...unavailable,supported:true,hostMode:'real-hc-authorized',hardwareCapable:true,fanRouteWriteReady:true});await adapter.handshake();const d=row('api.response');assert.equal(d.receipt.supported,true);assert.equal(d.response.hardwareWritesObserved,false);});
await check('missing-supported-is-absent-not-false-or-true',async()=>{const body={...unavailable};delete body.supported;reset(body);await assert.rejects(()=>adapter.handshake(),/FAN_API_RESPONSE_INVALID/);const d=row('api.response');assert.equal(Object.hasOwn(d.response,'supported'),false);assert.equal(Object.hasOwn(d.receipt,'supported'),false);assert.equal(d.receiptValid,false);});
await check('invalid-supported-string-is-not-coerced',async()=>{reset({...unavailable,supported:'false'});await assert.rejects(()=>adapter.handshake(),/FAN_API_RESPONSE_INVALID/);assert.equal(row('api.response').response.supported,'false');assert.equal(Object.hasOwn(row('api.response').receipt,'supported'),false);assert.equal(row('api.response').receiptValid,false);});
await check('envelope-ok-distinct-from-nested-state',async()=>{reset({ok:false,supported:false,state:{...ready,ok:true,supported:true},error:{code:'HC_CLOSE_PENDING',message:'fixture'}});await assert.rejects(()=>adapter.open(),/HC_CLOSE_PENDING/);const d=row('api.response');assert.equal(d.receipt.ok,false);assert.equal(d.receipt.supported,false);assert.equal(d.response.ok,true);assert.equal(d.response.supported,true);assert.equal(d.ok,false);});
await check('cleanup-evidence-fields-preserved-without-stopped-inference',async()=>{reset({ok:true,state:{...ready,hcDeviceManagerStopCompleted:true,hcDeviceManagerStopInvoked:false,hcManagerCleanupEvidence:'post-close-no-active-manager-observed',hcDeviceManagerLifecycle:'Started'}});await adapter.open();const d=row('api.response').response;assert.equal(d.hcDeviceManagerStopInvoked,false);assert.equal(d.hcManagerCleanupEvidence,'post-close-no-active-manager-observed');assert.equal(d.hcDeviceManagerLifecycle,'Started');});
await check('known-refusal-keeps-response-id-status-and-code',async()=>{reset({ok:false,state:ready,error:{code:'FAN_DEVICE_GATE_DENIED',message:'fixture'}},409);await assert.rejects(()=>adapter.open(),/FAN_DEVICE_GATE_DENIED/);const d=row('api.request-failure');assert.equal(d.responseReceived,true);assert.equal(d.status,409);assert.equal(d.requestId,'fixture-request-1');assert.equal(d.errorCode,'FAN_DEVICE_GATE_DENIED');});
await check('malformed-body-keeps-status-id-without-success-row',async()=>{reset();reply.body='{unfinished';await assert.rejects(()=>adapter.open(),/FAN_HTTP_RESULT_UNKNOWN/);const d=row('api.request-failure');assert.equal(d.responseReceived,true);assert.equal(d.status,200);assert.equal(d.requestId,'fixture-request-1');assert.equal(rows.some(r=>r.event==='api.response'),false);});
await check('partial-body-is-not-promoted-to-complete-receipt',async()=>{reset();reply.bodyComplete=false;await assert.rejects(()=>adapter.open(),/FAN_HTTP_RESULT_UNKNOWN/);assert.equal(row('api.request-failure').responseReceived,true);assert.equal(row('api.request-failure').status,200);});
await check('legacy-missing-body-complete-stays-unobserved',async()=>{reset();delete reply.bodyComplete;await adapter.open();const d=row('api.response');assert.equal(d.bodyCompleteObserved,false);assert.equal(Object.hasOwn(d,'bodyComplete'),false);});
await check('complete-receipt-observation-is-explicit',async()=>{reset();await adapter.open();assert.equal(row('api.response').bodyCompleteObserved,true);assert.equal(row('api.response').bodyComplete,true);});
await check('transport-failure-has-no-invented-http-status',async()=>{reset();error=new Error('HTTP request failed (WinHTTP 12029, phase=send)');await assert.rejects(()=>adapter.open(),/FAN_HTTP_RESULT_UNKNOWN/);const d=row('api.transport-failure');assert.equal(d.responseReceived,false);assert.equal(Object.hasOwn(d,'status'),false);assert.equal(Object.hasOwn(d,'requestId'),false);assert.equal(httpCalls.length,1);});
await check('response-diagnostic-construction-throw-does-not-change-open',async()=>{reset();let reads=0;const node={get tempC(){if(++reads>1)throw Error('fixture projection getter');return 0;},dutyPercent:20};assert.equal((await adapter.enable([node])).state,'Ready');assert.equal(reads,2);assert.equal(httpCalls.length,1);assert.equal(row('api.response').request.bodyParse,'failed');});
await check('optional-request-header-projection-fault-does-not-fail-open',async()=>{reset();Object.defineProperty(reply,'headers',{get(){throw Error('fixture optional header projection');}});assert.equal((await adapter.open()).state,'Ready');assert.equal(Object.hasOwn(row('api.response'),'requestId'),false);assert.equal(httpCalls.length,1);});
await check('gate-off-skips-diagnostic-request-projection',async()=>{reset();diag.adoptFanDiagnosticLoggingState(false);let reads=0;const node={get tempC(){reads++;return 0;},dutyPercent:20};await adapter.enable([node]);assert.equal(reads,1);assert.equal(rows.length,0);assert.equal(httpCalls.length,1);});
await check('diagnostic-ipc-rejection-does-not-change-receipt',async()=>{reset();diagnosticFailure=true;assert.equal((await adapter.open()).state,'Ready');await Promise.resolve();assert.equal(httpCalls.length,1);});
await check('response-and-request-secrets-do-not-enter-diagnostic-projection',async()=>{reset({ok:true,state:{...ready,sessionToken:'SECRET_RESPONSE_VALUE',leaseId:'SECRET_RESPONSE_LEASE'}});await adapter.enable([{tempC:0,dutyPercent:20}],'SECRET_REQUEST_LEASE');const text=JSON.stringify(rows);for(const secret of ['SECRET_SESSION_VALUE','SECRET_RESPONSE_VALUE','SECRET_RESPONSE_LEASE','SECRET_REQUEST_LEASE'])assert.equal(text.includes(secret),false);assert.equal(row('api.response').request.leasePresent,true);});
await check('missing-host-ok-remains-absent',async()=>{reset({state:ready});await adapter.open();assert.equal(Object.hasOwn(row('api.response').receipt,'ok'),false);});
await check('log-enabled-does-not-add-http-or-retries',async()=>{reset();await adapter.open();assert.equal(httpCalls.length,1);assert.equal(new URL(httpCalls[0].url).pathname,'/api/open');});
const heartbeatLease={leaseId:'fixture-private-lease',generation:3,expiresAtMonoMs:14000};
const heartbeatReply={ok:true,lease:heartbeatLease};
const heartbeatRequestId=(id)=>{reply.headers='X-YeMan-Fan-Request-Id: '+id.toString(16).padStart(32,'0');};
await check('complete success keeps one compact receipt for every heartbeat',async()=>{
  reset(heartbeatReply);
  for(const id of [1,2,3]){
    heartbeatRequestId(id);
    const returned=await adapter.heartbeat(heartbeatLease.leaseId);
    assert.equal(returned.expiresAtMonoMs,heartbeatLease.expiresAtMonoMs);
  }
  assert.equal(httpCalls.length,3);assert.equal(rows.length,3);
  for(const [i,r] of rows.entries()){
    const d=r.details;
    assert.equal(r.event,'api.response');assert.equal(d.path,'/api/heartbeat');assert.equal(d.method,'POST');
    assert.equal(d.status,200);assert.equal(d.ok,true);assert.equal(d.bodyCompleteObserved,true);
    assert.equal(d.bodyComplete,true);assert.equal(d.receiptValid,true);assert.equal(d.heartbeatReceiptSchema,1);
    assert.equal(d.requestId,(i+1).toString(16).padStart(32,'0'));assert.equal(typeof d.durationMs,'number');
    for(const name of ['request','receipt','response'])assert.equal(Object.hasOwn(d,name),false);
  }
  assert(!JSON.stringify(rows).includes(heartbeatLease.leaseId));
});
await check('different observations and legacy body completeness stay inline',async()=>{
  for(const [body,complete,knownId] of [
    [{...heartbeatReply,state:{...ready}},true,true],
    [{...heartbeatReply,resumeNoop:true},true,true],
    [heartbeatReply,undefined,true],
    [heartbeatReply,true,false],
    [{lease:heartbeatLease},true,true],
  ]){
    reset(body);if(knownId)heartbeatRequestId(10);
    if(complete===undefined)delete reply.bodyComplete;
    await adapter.heartbeat(heartbeatLease.leaseId);
    const d=row('api.response');assert.equal(Object.hasOwn(d,'heartbeatReceiptSchema'),false);
    assert.equal(d.request.leasePresent,true);assert.equal(d.bodyCompleteObserved,complete!==undefined);
    if(body.state)assert.equal(d.response.state,'Ready');
    if(body.resumeNoop)assert.equal(d.receipt.resumeNoop,true);
  }
});
await check('mismatched lease and incomplete body retain failure evidence',async()=>{
  reset({...heartbeatReply,lease:{...heartbeatLease,leaseId:'fixture-other-lease'}});heartbeatRequestId(11);
  await assert.rejects(()=>adapter.heartbeat(heartbeatLease.leaseId),/FAN_HTTP_RESULT_UNKNOWN/);
  const d=row('api.response');assert.equal(d.receiptValid,false);assert.equal(d.request.leasePresent,true);
  assert.equal(Object.hasOwn(d,'heartbeatReceiptSchema'),false);assert.equal(row('api.request-failure').responseReceived,true);
  reset(heartbeatReply);heartbeatRequestId(12);reply.bodyComplete=false;
  await assert.rejects(()=>adapter.heartbeat(heartbeatLease.leaseId),/FAN_HTTP_RESULT_UNKNOWN/);
  assert.equal(rows.some(r=>r.event==='api.response'),false);assert.equal(row('api.request-failure').status,200);
});
const telemetryState={...ready,unknownState:false,hcCloseCleanupPending:false,controlAccepting:true,oemRestoreConfirmed:false,powerState:'On',leaseGeneration:3,lastTemperatureC:51,
  deviceIdentity:{fixtureDescription:'test-identity-'.repeat(125)}};
const telemetryRead=(id,state=telemetryState,source='page-telemetry')=>{
  reply.body=JSON.stringify({ok:true,state});
  reply.headers=`X-YeMan-Fan-Request-Id: ${id.toString(16).padStart(32,'0')}`;
  return adapter.getState(undefined,source);
};
await check('page-only stable state retains each receipt and refreshes the full snapshot',async()=>{
  reset();adapter.setSessionToken('fixture-first');
  for(let id=1;id<=17;id++){
    const state=await telemetryRead(id);
    assert.equal(state.deviceIdentity.fixtureDescription,telemetryState.deviceIdentity.fixtureDescription);
  }
  assert.equal(rows.length,17);assert.equal(httpCalls.length,17);
  const details=rows.map(row=>row.details);
  assert.equal(details[0].stateSnapshot.kind,'full');assert.equal(details[16].stateSnapshot.kind,'full');
  for(const detail of details.slice(1,16)){
    assert.equal(detail.stateSnapshot.kind,'unchanged');assert.equal(detail.stateSnapshot.baselineRequestId,details[0].requestId);
    assert.equal(detail.response.lastTemperatureC,51);assert.equal(detail.response.hardwareWritesEnabled,true);
    assert.equal(Object.hasOwn(detail.response,'deviceIdentity'),false);
    assert.equal(detail.bodyComplete,true);assert.equal(detail.receiptValid,true);assert.equal(typeof detail.durationMs,'number');
  }
  assert.equal(new Set(details.map(detail=>detail.requestId)).size,17);
  await telemetryRead(18,{...telemetryState,lastTemperatureC:52});
  assert.equal(rows.at(-1).details.stateSnapshot.kind,'full');assert.equal(rows.at(-1).details.response.lastTemperatureC,52);
  reply.body=JSON.stringify({ok:true,state:telemetryState});await adapter.getState();
  assert.equal(Object.hasOwn(rows.at(-1).details,'stateSnapshot'),false);
  assert.equal(rows.at(-1).details.response.deviceIdentity.fixtureDescription,telemetryState.deviceIdentity.fixtureDescription);
  await telemetryRead(19);assert.equal(rows.at(-1).details.stateSnapshot.kind,'full');
});
await check('page state rearms after gate toggles, power or token changes, and failed responses',async()=>{
  reset();adapter.setSessionToken('fixture-second');diag.setFanDiagnosticPowerGeneration(7);
  await telemetryRead(20);await telemetryRead(21);assert.equal(rows.at(-1).details.stateSnapshot.kind,'unchanged');
  diag.adoptFanDiagnosticLoggingState(false);await telemetryRead(22);assert.equal(rows.length,2);
  diag.adoptFanDiagnosticLoggingState(true);await telemetryRead(23);assert.equal(rows.at(-1).details.stateSnapshot.kind,'full');
  diag.adoptFanDiagnosticLoggingState(false);diag.adoptFanDiagnosticLoggingState(true);
  await telemetryRead(24);assert.equal(rows.at(-1).details.stateSnapshot.kind,'full');
  diag.setFanDiagnosticPowerGeneration(8);await telemetryRead(25);assert.equal(rows.at(-1).details.stateSnapshot.kind,'full');
  adapter.setSessionToken('fixture-third');await telemetryRead(26);assert.equal(rows.at(-1).details.stateSnapshot.kind,'full');
  reply.bodyComplete=false;await assert.rejects(()=>telemetryRead(27),/FAN_API_RESPONSE_INVALID/);
  assert.equal(rows.at(-1).event,'api.request-failure');
  reply.bodyComplete=true;await telemetryRead(28);assert.equal(rows.at(-1).details.stateSnapshot.kind,'full');
  const fault={...telemetryState,unknownState:true};
  await telemetryRead(29,fault);await telemetryRead(30,fault);
  assert.equal(rows.at(-1).details.stateSnapshot.kind,'full');assert.equal(rows.at(-1).details.response.unknownState,true);
  for(const [id,state] of [[31,{...telemetryState,controlAccepting:false}],[33,{...telemetryState,oemRestoreConfirmed:true}]]){
    await telemetryRead(id,state);await telemetryRead(id+1,state);assert.equal(rows.at(-1).details.stateSnapshot.kind,'full');
  }
});
if(fieldState){
  const input=JSON.parse(fs.readFileSync(fieldState,'utf8'));
  const states=input.rows;
  assert(states.length>16);
  reset();adapter.setSessionToken('fixture-field-state');
  const byRequest=new Map();let full=0,unchanged=0,originalBytes=0,projectedBytes=0;
  const measure=row=>Buffer.byteLength(JSON.stringify(row)+'\n');
  for(const original of states){
    const d=original.details;
    assert.equal(d.path,'/api/state');assert.equal(d.status,200);assert.equal(d.bodyComplete,true);
    reply.body=JSON.stringify({...d.receipt,state:d.response});
    reply.headers=`X-YeMan-Fan-Request-Id: ${d.requestId}`;
    diag.setFanDiagnosticPowerGeneration(d.generation+1000);
    const returned=await adapter.getState(undefined,'page-telemetry');
    assert.deepEqual(JSON.parse(JSON.stringify(returned)),d.response);
    const observed=rows.pop().details;
    assert.equal(observed.requestId,d.requestId);
    if(observed.stateSnapshot.kind==='unchanged'){
      unchanged++;
      assert.deepEqual(byRequest.get(observed.stateSnapshot.baselineRequestId),d.response);
      for(const [key,value] of Object.entries(observed.response))assert.deepEqual(value,d.response[key]);
    }else{
      full++;
      assert.deepEqual(observed.response,d.response);
      byRequest.set(observed.requestId,observed.response);
    }
    originalBytes+=measure(original);
    projectedBytes+=measure({...original,details:{...d,response:observed.response,diagnosticSource:observed.diagnosticSource,stateSnapshot:observed.stateSnapshot}});
  }
  assert.equal(httpCalls.length,states.length);assert(full>0&&unchanged>0);
  const report={scope:'historical logged projections replayed with assumed page source; not historical call-site, raw HTTP or installed result',
    rows:states.length,full,unchanged,originalBytes,projectedBytes,savedBytes:originalBytes-projectedBytes,input:input.source};
  fs.writeFileSync(path.join(out,'field-projection.json'),JSON.stringify(report,null,2)+'\n',{flag:'wx'});
  console.log(JSON.stringify(report));
}
for(const [name,p] of Object.entries(files))assert.equal(digest(fs.readFileSync(p)),bindings[name].sha256,'source drift '+name);
const report={status:failures.length?'FAIL':'PASS',checks,passed:checks.filter(c=>c.pass).length,failures,sourceBindings:bindings,scope:'actual TS API/diagnostics; fake HTTP/IPC; no product process, real network, setting or hardware'};
fs.writeFileSync(path.join(out,'synthetic-fan-api-rows.json'),JSON.stringify({scope:'synthetic memory HTTP/IPC; not a field log',rows:syntheticRows},null,2)+'\n');fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify({status:report.status,checks:checks.length,passed:report.passed,failures}));process.exitCode=failures.length?1:0;
