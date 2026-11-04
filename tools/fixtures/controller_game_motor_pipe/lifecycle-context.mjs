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
 function member(text,file,name,kind='function',emitted=true){const r=extract(text,name,kind);bindings.push({...r,file,sourceSha256:sha(text),emitted});return r.code;}
 function declaration(text,file,name,pattern,emitted=true){const matches=[...text.matchAll(pattern)];assert.equal(matches.length,1,'ACTUAL_LIFECYCLE_DECLARATION:'+name);const m=matches[0],code=m[0];bindings.push({symbol:name,kind:'declaration',file,sourceSha256:sha(text),sha256:sha(code),startUtf8Byte:Buffer.byteLength(text.slice(0,m.index)),endUtf8ByteExclusive:Buffer.byteLength(text.slice(0,m.index+code.length)),emitted});return code;}
 const accepted=member(runtime,runtimeFile,'AcceptedSource','struct');
 const acceptedState=declaration(runtime,runtimeFile,'acceptedSource',/^static AcceptedSource acceptedSource;\r?$/gm);
 const forget=member(runtime,runtimeFile,'inputHostSwitchVerificationForgetAcceptedLocked');
 // Both current ordinary requests and protected starts consult this same real state.
 const deadline=declaration(main,sourceFile,'g_inputHostSwitchRecoveryStartDeadline',/^static thread_local std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0;\r?$/gm);
 const transport=declaration(main,sourceFile,'g_inputHostSwitchRecoveryTransportError',/^static DWORD g_inputHostSwitchRecoveryTransportError=ERROR_SUCCESS;[^\r\n]*/gm);
 const prepare=declaration(main,sourceFile,'g_inputHostPrepareMayHaveReachedHost',/^static bool g_inputHostPrepareMayHaveReachedHost = false;\r?$/gm);
 const protection=declaration(main,sourceFile,'g_virtualStartProtection',/^static ymcc::VirtualStartProtection g_virtualStartProtection;\r?$/gm);
 const pending=declaration(main,sourceFile,'g_inputHostSwitchVerificationPending',/^static std::atomic<std::uint64_t> g_inputHostSwitchVerificationPending\{0\};\r?$/gm);
 const startBlocked=declaration(main,sourceFile,'g_inputHostStartBlocked',/^static bool g_inputHostStartBlocked = false;\r?$/gm);
 const tokenType=member(main,sourceFile,'InputHostTupleRecoveryToken','struct');
 const tokenState=declaration(main,sourceFile,'g_inputHostTupleAccepted',/^static InputHostTupleRecoveryToken g_inputHostTupleFault,g_inputHostDeferredTuple,g_inputHostTupleAccepted;[^\r\n]*/gm);
 const clock=member(main,sourceFile,'inputHostStartProtectionNow');
 const projection=['inputHostStartProtectionJson','inputHostStartProtectionSnapshot','inputHostStartProtectionLog','inputHostStartProtectionFinish'].map(name=>member(main,sourceFile,name));
 const requestHelpers=['inputHostRequestIsLifecycleCommand','inputHostRequestHasExecutionDeadline','inputHostProcessAliveLocked'].map(name=>member(main,sourceFile,name));
 const tupleHelpers=['inputHostTupleRecoveryCurrentLocked','inputHostTupleRecoveryTokenLocked','inputHostTupleRecoveryReason'].map(name=>member(tuple,tupleFile,name));
 // A null production write owner leaves these gates inactive. If armed, the
 // actual intent, deadline and physical-source checks remain mandatory.
 const deferredType=member(main,sourceFile,'InputHostDeferredPhysicalHide','struct');
 const deferredState=[
  declaration(main,sourceFile,'t_inputHostDeferredPhysicalHideWrite',/^static thread_local const InputHostDeferredPhysicalHide\* t_inputHostDeferredPhysicalHideWrite=nullptr;\r?$/gm),
  declaration(main,sourceFile,'g_inputHostDeferredPhysicalHideMx',/^static std::mutex g_inputHostDeferredPhysicalHideMx;\r?$/gm),
  declaration(main,sourceFile,'g_inputHostDeferredPhysicalHide',/^static InputHostDeferredPhysicalHide g_inputHostDeferredPhysicalHide;\r?$/gm),
  declaration(main,sourceFile,'t_inputHostDeferredPhysicalHideFirstRefusal',/^static thread_local const char\* t_inputHostDeferredPhysicalHideFirstRefusal=nullptr;\r?$/gm),
  declaration(main,sourceFile,'g_xinputPhysicalSourceLocked',/^static bool g_xinputPhysicalSourceLocked = false;\r?$/gm),
  declaration(main,sourceFile,'g_xinputPhysicalSourceMask',/^static DWORD g_xinputPhysicalSourceMask = 0;\r?$/gm)
 ];
 // Reuse the later cold-return owner's real physicalReleaseWindowActive body.
 const releaseWindowDeclaration=declaration(main,sourceFile,'physicalReleaseWindowActive',/^static bool physicalReleaseWindowActive\(\);\r?$(?=\r?\nstatic void inputHostRecoveryPhysicalFallback\(\);)/gm);
 const deferredHelpers=['inputHostDeferredPhysicalHideCurrentReason','inputHostDeferredPhysicalHideRefuse','inputHostDeferredPhysicalHideIntentCurrent','inputHostDeferredPhysicalHideWriteCurrent'].map(name=>member(main,sourceFile,name));
 const unadoptedPipe=declaration(main,sourceFile,'g_inputHostUnadoptedPipe',/^static HANDLE g_inputHostUnadoptedPipe = nullptr;\r?$/gm);
 const retirement=member(main,sourceFile,'inputHostPipeRetirementProvenLocked');
 // These bindings record the remaining boundary without pretending the bodies
 // were compiled. Emitting only their top-level bodies would not close their
 // real source/foreground/wake owners; replacing those predicates is forbidden.
 const unclosedDefinitions=[
  {symbol:'rememberRogOffSignalLocked',code:member(runtime,runtimeFile,'rememberRogOffSignalLocked','function',false),reason:'Requires real ROG identity scope and retained ROG source-obligation owners.'},
  {symbol:'rogXboxSignalScope',code:member(runtime,runtimeFile,'rogXboxSignalScope','function',false),reason:'NativeEntry YmccFamily lacks the production AsusRogAlly and AsusRogAllyX enumerators.'},
  {symbol:'inputHostRogOffExecutionDefersSuccess',code:member(runtime,runtimeFile,'inputHostRogOffExecutionDefersSuccess','function',false),reason:'Requires rogOffExecutionReceiptReadyLocked and its real cleanup/pause/busy/wake/current-source closure.'},
  {symbol:'g_inputHostReleaseQuarantined',code:declaration(main,sourceFile,'g_inputHostReleaseQuarantined',/^static bool g_inputHostReleaseQuarantined = false;\r?$/gm,false),reason:'Already defined by coldReturnContext.prefix, currently after PIPE; move that sole owner earlier instead of defining it twice.'}
 ];
 const code=['#include "input_host_recovery_health.h"','#include "virtual_start_protection.h"','#include "input_startup_diagnostics.h"','#include "input_host_pipe_transport.h"',
  'namespace ymcc::inputHost::switchVerificationRuntime {',accepted,acceptedState,'}',forget,
  deadline,transport,prepare,protection,pending,startBlocked,tokenType,tokenState,clock,...projection,...requestHelpers,...tupleHelpers,
  deferredType,...deferredState,releaseWindowDeclaration,...deferredHelpers,unadoptedPipe,retirement].join('\n')+'\n';
 const inputs=[...new Set([sourceFile,...LIFECYCLE_INPUTS.map(resolveInput)])].map(file=>({file,sha256:sha(fs.readFileSync(file))}));
 return {code,bindings,inputs,unclosedDefinitions,boundaries:{productionDefinitionsVerbatim:true,staticDependencyClosureComplete:false,unemittedBindingsAreNotCompiled:true,acceptedSourceState:'Actual production pure cached scalar provenance; fresh fixture-owned state.',deadlineAndTransportState:'Actual production declarations; original request consumers own all reads/writes.',tupleAndProtection:'Actual default production VirtualStartProtection and tuple scalars; no fabricated successful admission or final result.',deferredHide:'Actual null write owner, default debt/source state and complete intent/write predicate bodies; cold-return owns physicalReleaseWindowActive.',pipeRetirement:'Actual null unadopted owner and real CloseHandle/runtime maintenance predicate; no alternate transport.',clock:'Actual production GetTickCount64 implementation; no lifecycle authorization is synthesized.',processLiveness:'Actual production read-only handle check; NativeEntry owns the private managed child handle.',requestedProtectionContext:startProtection,realLifecycleWorkerExecuted:false,hardwareIo:false}};
}
