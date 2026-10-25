import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
export const need=(condition,code)=>{if(!condition)throw Error(code);};
export const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\ufeff/,''));
export function within(root,file){const relative=path.relative(path.resolve(root),path.resolve(file));return relative===''||!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);}
export function verifyMode({managed,boundary,node,originalOutput,execution},spec,{mode,fault=false,hostSha256}){
 const definition=spec.modes.find(row=>row.mode===mode);need(definition,'UNKNOWN_ORIGINAL_MODE');
 need(managed.count===definition.checks&&managed.checks.length===definition.checks,'ORIGINAL_MANAGED_CHECK_COUNT');
 need(JSON.stringify(managed.checks.map(row=>row.name))===JSON.stringify(definition.originalManagedCheckNames),'ORIGINAL_MANAGED_CHECK_ORDER');
 const failed=managed.checks.filter(row=>row.passed!==true);
 if(fault){need(mode==='allowed'&&managed.ok===false&&failed.length===1&&failed[0].name===spec.fault.expectedManagedFailure&&managed.count-failed.length===81,'EXACT_ORIGINAL_FAULT_CHECK');need(originalOutput.ok===false&&originalOutput.error===spec.fault.expectedTsError,'ORIGINAL_TS_FAULT_FEEDBACK');}
 else{need(managed.ok===true&&failed.length===0&&originalOutput.ok===true,'ORIGINAL_POSITIVE_CONTRACT');}
 need(originalOutput.mode===mode&&typeof originalOutput.error==='string','ORIGINAL_TS_ENDPOINT_MISSING');
 need(execution.naturalExit===true&&execution.exitCode===(fault?1:0)&&!execution.error&&!execution.signal,'OWNED_ORIGINAL_CHILD_ENDPOINT');
 need(boundary.observedProcessExit===true&&boundary.optionsChecked===17&&boundary.enginesObserved===17,'ACTUAL_ORIGINAL_MOCK_ENGINE_CONSTRUCTION');
 need(Array.isArray(boundary.denied)&&!boundary.denied.length&&Array.isArray(boundary.targetNativeAttempts)&&!boundary.targetNativeAttempts.length,'MANAGED_BOUNDARY_DENIAL');
 need(boundary.loadedTargetAssemblies.some(row=>path.basename(row.path)==='YeManFanHost.dll'&&row.sha256===hostSha256),'ACTUAL_LOADED_HOST_IDENTITY');
 need(node.exitCode===(fault?1:0)&&node.beforeExitObserved===true&&node.explicitExitCalled===false&&node.initial.windowAbsent===true&&node.initial.chromeAbsent===true&&node.windowAbsentAtExit===true&&node.chromeAbsentAtExit===true,'NODE_NATURAL_NO_WEBVIEW_ENDPOINT');
 need(Array.isArray(node.denied)&&!node.denied.length&&!node.activeResources.includes('Timeout'),'NODE_BOUNDARY_DENIAL_OR_TIMER');
 const origin=new URL(node.allowedOrigin);need(origin.protocol==='http:'&&origin.hostname==='127.0.0.1'&&origin.port,'OWNED_LOOPBACK_ORIGIN');need(node.connections.length>0&&node.connections.every(row=>row.host==='127.0.0.1'&&row.port===Number(origin.port)),'OWNED_LOOPBACK_CONNECTIONS');
 const requests=node.requests;need(requests.length>0&&requests.every((row,index)=>row.sequence===index+1&&row.status===200&&row.sessionTokenPresent&&/^[a-f0-9]{64}$/.test(row.sessionTokenSha256)&&!row.transportError),'ACTUAL_AUTHENTICATED_HTTP_FEEDBACK');
 need(requests[0].path==='/api/handshake'&&requests[1].path==='/api/cancel-control-intent'&&requests[1].response.controlIntentCancelled===true,'DISABLE_CANCEL_REAL_HTTP_SEAM');
 need(requests.at(-1).path==='/api/close','ORIGINAL_HTTP_CLOSE_NOT_CONSUMED');
 const controlPaths=['/api/open','/api/open-events','/api/acquire-control','/api/enable'];
 let controlSessionPath='refused',controlSessionEvidence=null;
 if(definition.controlWriteAllowed&&!fault){
  const acquire=requests.filter(row=>row.path==='/api/acquire-control'),enable=requests.filter(row=>row.path==='/api/enable'),opens=requests.filter(row=>row.path==='/api/open'),events=requests.filter(row=>row.path==='/api/open-events');
  need(acquire.length===1&&enable.length===1&&acquire[0].sequence<enable[0].sequence,'ORIGINAL_ACQUIRE_ENABLE_ENDPOINTS');
  if(opens.length===1&&events.length===1){need(opens[0].sequence<events[0].sequence&&events[0].sequence<acquire[0].sequence,'ORIGINAL_OPEN_EVENTS_ORDER');controlSessionPath='opened-session';controlSessionEvidence={openRequest:opens[0].sequence,openEventsRequest:events[0].sequence,acquireRequest:acquire[0].sequence,enableRequest:enable[0].sequence};}
  else{
   need(opens.length===0&&events.length===0,'UNPAIRED_OR_REPEATED_SESSION_OPEN');
   const proof=requests.filter(row=>row.path==='/api/state'&&row.sequence<acquire[0].sequence).at(-1),before=proof?.response?.state,expectedGeneration=mode.startsWith('empty-')?3:2;
   need(proof?.response?.ok===true&&proof.sessionTokenSha256===acquire[0].sessionTokenSha256&&before&&['Ready','AwaitingControl'].includes(before.state)&&before.powerState==='On'&&before.openCalled===true&&before.openEventsCalled===true&&before.controlAccepting===true&&before.unknownState===false&&before.hcCloseCleanupPending===false&&before.powerOperationGeneration===expectedGeneration&&before.resumePhaseGeneration===expectedGeneration&&before.powerOperationStatus==='completed'&&before.lease===null,'EXISTING_READY_SESSION_NOT_PROVEN');controlSessionPath='existing-ready-session';controlSessionEvidence={actualStateRequest:proof.sequence,actualStateEndpoint:proof.path,stateSource:'node-runtime-boundary.json requests[].response.state',acquireRequest:acquire[0].sequence,enableRequest:enable[0].sequence,sessionTokenSha256:proof.sessionTokenSha256,generation:before.powerOperationGeneration,loadedHostSha256:hostSha256};
  }
  need(enable[0].response.state.hardwareWritesEnabled===true,'ORIGINAL_MOCK_PROTOCOL_ENABLED');
 }
 else need(!requests.some(row=>controlPaths.includes(row.path)),'FORBIDDEN_CONTROL_AFTER_REFUSAL_OR_FAULT');
 need(managed.checks.find(row=>row.name==='cancel-http-listener-stops-within-bound')?.passed===true,'ORIGINAL_HTTP_LISTENER_DID_NOT_SETTLE');
 return{mode,fault,controlSessionPath,controlSessionEvidence,checks:managed.count,failedChecks:failed,originalOutput,httpRequests:requests.length,ownedOrigin:node.allowedOrigin,naturalManagedExit:execution.exitCode,naturalNodeExit:node.exitCode,managedEngineCount:boundary.enginesObserved,originalEngineDisposedFlags:boundary.engineDisposedFlags,allEngineDisposedClaimed:false,physicalAcceptance:false};
}
