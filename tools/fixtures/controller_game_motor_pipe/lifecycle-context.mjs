// Source-exact dependencies of the current feedback request; all state is fixture-owned.
import fs from 'node:fs';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {extract} from '../legion_motion_consumer/extract_main.mjs';
export const LIFECYCLE_INPUTS=Object.freeze([
 'native/virtual_switch_verification_runtime.h','native/input_host_recovery_health.h',
 'native/virtual_tuple_recovery_runtime.h','native/virtual_start_protection.h',
 'native/input_startup_diagnostics.h','native/ps_endpoint_ownership.h'
]);
export function lifecycleContext({sourceFile,resolveInput,startProtection=false}){
 const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
 const main=fs.readFileSync(sourceFile,'utf8'),runtimeFile=resolveInput(LIFECYCLE_INPUTS[0]),runtime=fs.readFileSync(runtimeFile,'utf8');
 const tupleFile=resolveInput('native/virtual_tuple_recovery_runtime.h'),tuple=fs.readFileSync(tupleFile,'utf8');
 const bindings=[];
 function member(text,file,name,kind='function'){const r=extract(text,name,kind);bindings.push({...r,file,sourceSha256:sha(text)});return r.code;}
 function declaration(text,file,name,pattern){const matches=[...text.matchAll(pattern)];assert.equal(matches.length,1,'ACTUAL_LIFECYCLE_DECLARATION:'+name);const m=matches[0],code=m[0];bindings.push({symbol:name,kind:'declaration',file,sourceSha256:sha(text),sha256:sha(code),startUtf8Byte:Buffer.byteLength(text.slice(0,m.index)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,m.index+code.length))});return code;}
 const accepted=member(runtime,runtimeFile,'AcceptedSource','struct');
 const acceptedState=declaration(runtime,runtimeFile,'acceptedSource',/^static AcceptedSource acceptedSource;\r?$/gm);
 const forget=member(runtime,runtimeFile,'inputHostSwitchVerificationForgetAcceptedLocked');
 // Both current ordinary requests and protected starts consult this same real state.
 const deadline=declaration(main,sourceFile,'g_inputHostSwitchRecoveryStartDeadline',/^static thread_local std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0;\r?$/gm);
 const transport=declaration(main,sourceFile,'g_inputHostSwitchRecoveryTransportError',/^static DWORD g_inputHostSwitchRecoveryTransportError=ERROR_SUCCESS;[^\r\n]*/gm);
 const prepare=declaration(main,sourceFile,'g_inputHostPrepareMayHaveReachedHost',/^static bool g_inputHostPrepareMayHaveReachedHost = false;\r?$/gm);
 const protection=declaration(main,sourceFile,'g_virtualStartProtection',/^static ymcc::VirtualStartProtection g_virtualStartProtection;\r?$/gm);
 const pending=declaration(main,sourceFile,'g_inputHostSwitchVerificationPending',/^static std::atomic<std::uint64_t> g_inputHostSwitchVerificationPending\{0\};\r?$/gm);
 const tokenType=member(main,sourceFile,'InputHostTupleRecoveryToken','struct');
 const tokenState=declaration(main,sourceFile,'g_inputHostTupleAccepted',/^static InputHostTupleRecoveryToken g_inputHostTupleFault,g_inputHostDeferredTuple,g_inputHostTupleAccepted;[^\r\n]*/gm);
 const clock=member(main,sourceFile,'inputHostStartProtectionNow');
 const projection=['inputHostStartProtectionSnapshot','inputHostStartProtectionLog','inputHostStartProtectionFinish'].map(name=>member(main,sourceFile,name));
 const requestHelpers=['inputHostRequestIsLifecycleCommand','inputHostRequestHasExecutionDeadline','inputHostProcessAliveLocked'].map(name=>member(main,sourceFile,name));
 const tupleHelpers=['inputHostTupleRecoveryCurrentLocked','inputHostTupleRecoveryTokenLocked','inputHostTupleRecoveryReason'].map(name=>member(tuple,tupleFile,name));
 const code=['#include "input_host_recovery_health.h"','#include "virtual_start_protection.h"','#include "input_startup_diagnostics.h"',
  'namespace ymcc::inputHost::switchVerificationRuntime {',accepted,acceptedState,'}',forget,
  deadline,transport,prepare,protection,pending,tokenType,tokenState,clock,...projection,...requestHelpers,...tupleHelpers].join('\n')+'\n';
 const inputs=[...new Set([sourceFile,...LIFECYCLE_INPUTS.map(resolveInput)])].map(file=>({file,sha256:sha(fs.readFileSync(file))}));
 return {code,bindings,inputs,boundaries:{productionDefinitionsVerbatim:true,acceptedSourceState:'Actual production pure cached scalar provenance; fresh fixture-owned state.',deadlineAndTransportState:'Actual production declarations; original request consumers own all reads/writes.',tupleAndProtection:'Actual default production VirtualStartProtection and tuple scalars; no fabricated successful admission or final result.',clock:'Actual production GetTickCount64 implementation; no lifecycle authorization is synthesized.',processLiveness:'Actual production read-only handle check; NativeEntry owns the private managed child handle.',requestedProtectionContext:startProtection,realLifecycleWorkerExecuted:false,hardwareIo:false}};
}
