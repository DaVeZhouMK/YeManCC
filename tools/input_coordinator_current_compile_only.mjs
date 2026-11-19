import {assertManagedManifestBinding} from './fixtures/input_coordinator_lifecycle/managed_manifest_binding.mjs';
import {observeCurrent105Progress} from './fixtures/input_coordinator_lifecycle/check_progress.mjs';
import {observeFinalStages} from './fixtures/input_coordinator_lifecycle/final_stages.mjs';
import {isolateCurrentFixtureSettingsMutex,privateSettingsMutexDefinitionForRuntimeRoot} from './fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs';
import {readLifecycleLogData,LIFECYCLE_LOG_PRODUCER,LIFECYCLE_LOG_DATA_ROLE} from './fixtures/input_coordinator_lifecycle/lifecycle-log-data.mjs';
import {requireCurrentCompileBatch} from './ymcc_sim/global/controller-coordinator-pipe/r3-current-compile-batch.mjs';
import {managedCompileContract} from './ymcc_sim/global/controller-coordinator-pipe/r3-managed-input-contract.mjs';
import {guardCurrentSteam as guardCurrentSteam,observeCurrent105Fixture,repairCurrent105LifecycleFixtureInputs,currentCompileSourceDataProfile,currentFixtureSourceOverride} from './input_coordinator_current105_source_adapters.mjs';
import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';import{spawnSync}from'node:child_process';import{fileURLToPath}from'node:url';import{extract}from'./fixtures/legion_motion_consumer/extract_main.mjs';import{calibrationToolchainPreamble}from'./fixtures/hc_reviewed_calibration/toolchain.mjs';import{nativeCompilerInputClosure}from'./fixtures/input_coordinator_lifecycle/compiler_inputs.mjs';
import {readVirtualControllerSource,replaceUniqueSourceText} from './virtual_controller_runtime_source.mjs';
import {terminal26RuntimeLayout} from './ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-runtime-layout.mjs';
import {terminal26NativeSourceProfileFiles,reviewedTerminal26ProductSourceSuccessor,reviewedTerminal26MsiCleanupHeader,pairTerminal26MsiPolicyProducts,admitTerminal26MsiPolicyTopology,pairTerminal26MsiPolicyFixture,pairTerminal26MsiPolicyIo} from './input_coordinator_terminal26_source_profile.mjs';
import {pairMsiJointPsWake} from './fixtures/input_coordinator_lifecycle/msi_joint_source.mjs';
// Copy only bare, repository-owned tool headers named by the reviewed fixture.
// observeCurrent105Fixture still requires its exact approved SHA before compile.
export function currentFixtureToolHeaders(repo){
 const fixture=path.join(repo,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp');
 const text=fs.readFileSync(fixture,'utf8');
 if(Buffer.byteLength(text)>4*1024*1024)throw Error('CURRENT_FIXTURE_HEADER_INPUT_TOO_LARGE');
 const toolRoot=fs.realpathSync(path.join(repo,'tools')),result=[],seen=new Set();
 for(const match of text.matchAll(/^\s*#\s*include\s*"([^\"]+)"/gm)){
  const name=match[1];if(!/\.(?:h|hpp|hxx)$/i.test(name))continue;
  if(name!==path.basename(name)||!/^[-A-Za-z0-9_.]+$/.test(name))throw Error('CURRENT_FIXTURE_HEADER_PATH_REFUSED');
  const candidate=path.join(repo,'tools',name);if(!fs.existsSync(candidate))continue;
  const stat=fs.lstatSync(candidate);if(!stat.isFile()||stat.isSymbolicLink())throw Error('CURRENT_FIXTURE_HEADER_NOT_REGULAR');
  const real=fs.realpathSync(candidate),relative=path.relative(toolRoot,real);
  if(path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep))throw Error('CURRENT_FIXTURE_HEADER_ESCAPED_TOOLS');
  if(fs.existsSync(path.join(repo,'native',name)))throw Error('CURRENT_FIXTURE_HEADER_AMBIGUOUS_ORIGIN');
  const key=real.toLowerCase();if(seen.has(key))continue;seen.add(key);
  result.push('tools/'+path.basename(real));if(result.length>16)throw Error('CURRENT_FIXTURE_HEADER_COUNT_EXCEEDED');
 }
 return result;
}
// Only direct .inc files beside the already reviewed fixture are additional
// compiler inputs. Existing fixed inputs remain; fixture/profile admission is
// still checked independently before compilation.
export function currentFixtureCaseIncludes(repo){
 const fixtureRoot=path.join(repo,'tools/fixtures/input_coordinator_lifecycle');
 const root=fs.realpathSync(fixtureRoot),text=fs.readFileSync(path.join(fixtureRoot,'fixture.cpp'),'utf8');
 if(Buffer.byteLength(text)>4*1024*1024)throw Error('CURRENT_FIXTURE_CASE_INPUT_TOO_LARGE');
 const result=[],seen=new Set();
 for(const match of text.matchAll(/^\s*#\s*include\s*"([^\"]+)"/gm)){
  const name=match[1];if(!/\.inc$/i.test(name))continue;
  if(name!==path.basename(name)||!/^[-A-Za-z0-9_.]+$/.test(name))throw Error('CURRENT_FIXTURE_CASE_PATH_REFUSED');
  const candidate=path.join(fixtureRoot,name),stat=fs.lstatSync(candidate);
  if(!stat.isFile()||stat.isSymbolicLink())throw Error('CURRENT_FIXTURE_CASE_NOT_REGULAR');
  const real=fs.realpathSync(candidate),relative=path.relative(root,real);
  if(path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep))throw Error('CURRENT_FIXTURE_CASE_ESCAPED_ROOT');
  if(fs.existsSync(path.join(repo,'native',name))||fs.existsSync(path.join(repo,'tools',name)))throw Error('CURRENT_FIXTURE_CASE_AMBIGUOUS_ORIGIN');
  const key=real.toLowerCase();if(seen.has(key))continue;seen.add(key);
  result.push('tools/fixtures/input_coordinator_lifecycle/'+path.basename(real));
  if(result.length>16)throw Error('CURRENT_FIXTURE_CASE_COUNT_EXCEEDED');
 }
 return result;
}
// Derive the fixture log leaf only from the exact producer approved for this
// captured source frame. This parses one quoted data literal and executes no JS.
export function currentLifecycleLogBoundary(repo,{managedInputContract,nativeSourceProfile}={}){
 const profile=currentCompileSourceDataProfile({sourceRoot:repo,managedInputContract,nativeSourceProfile});
 const producerRelative=(managedInputContract==='current105-terminal23-sd-decoder-v1'||managedInputContract==='current105-terminal26-sdk-lifetime-compact-v1')?profile.producerData?.relative:profile.producer?.relative;
 if(producerRelative!==LIFECYCLE_LOG_PRODUCER)throw Error('CURRENT_LOG_PRODUCER_PROFILE_REQUIRED');
 return readLifecycleLogData(repo,profile.producerData).body;
}

export const currentCompileCoreSymbols=Object.freeze(['inputHostTargetReturnAcceptedRecreateLocked','inputHostTargetReturnAfterPsQuarantineLocked','hidHideCliReapPendingLocked','inputHostTargetReturnBeforeCreateLocked','inputHostTargetReturnAcceptPsLocked','inputHostTargetReturnTargetExitedLocked','inputHostTargetReturnBeforeHideLocked','inputHostTargetReturnAfterPhysicalRestoreLocked','inputHostApplyHelloCapabilities','inputHostRequestPreparedLocked','inputHostRequestHasExecutionDeadline','psWakeCaptureEndpointOwnership','inputHostRefreshPersonaMatchLocked','inputHostSetBoundPersona','inputHostClearBoundPersona','inputHostXboxPreparedHid','inputHostXboxPreparedRoot','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked','inputHostRequestPsWakeRearm','inputHostConsumePsWakeRearm','inputHostPerformPsWakeRearm','psWakeProbeOwnEndpoint','psWakeDecide','inputHostRecreatePreparedTarget','inputHostRequestWakeRescan','inputHostConsumeWakeRescan','inputHostPerformWakeHotReplace','inputHostWakePhysicalHandshakeReady','sgShouldKeepInputDuringStandby','inputHostRequestPowerRelease','inputHostRequestPowerRearm','inputHostConsumePowerRequests','inputCaptureSuspendSensorsForPower','inputCaptureResumeSensorsForPower','inputHostSubmitPad','inputHostFrameSubmissionCurrent','inputHostPhysicalPublicationSourceAdmitted','inputHostFreshNeutralReady','inputHostStart','inputHostStopLocked','inputHostStop','inputHostClearLocked','inputHostRunQueuedLifecycleWork','inputHostReconcileQuarantine','inputHostReconcileQuarantineOnLifecycleThread','msiSteamDeckRecheckOwnedRemovalDebt','ymStartPipeSeamServe','ymStartEnsureRoute','msiClawOpenModeCommand','msiDinputSubmitModeWriteIntent','msiStartOrderEntrySelfTest','inputHostTargetAdmission','inputHostStartProtectionFinish','nativeInputShortcutToggle','clearInputShortcutRuntime','inputCaptureRefreshRealStickSettings','inputHostTargetReturnHistorySettledLocked','inputHostTargetReturnColdPassLocked','inputHostTargetReturnQueueColdStartup']);
// Reuse the generic fixture's existing device I/O ports only for the explicit
// native ordinary switch scene. No ownership, return or recovery algorithm is
// replaced. Earlier contracts do not reference the new fixture scope at all.
export function bindCurrentOrdinarySwitchFixturePorts(source,{managedInputContract}={}){
 if(managedInputContract!=='current105-terminal26-sdk-lifetime-compact-v1')return {source,substitutions:[]};
 let text=source;const substitutions=[];
 const prefix=(symbol,body)=>{
  const old=extract(text,symbol).code,brace=old.indexOf('{');
  const next=old.slice(0,brace+1)+'\n'+body+'\n'+old.slice(brace+1);
  if(text.split(old).length!==2)throw Error('CURRENT_SWITCH_PORT_NOT_UNIQUE:'+symbol);
  text=text.replace(old,()=>next);substitutions.push({symbol,before:old,after:next});
 };
 const replace=(symbol,before,after)=>{
  const old=extract(text,symbol).code,next=replaceUniqueSourceText(old,before,after,'ordinary native switch explicit I/O port '+symbol);
  if(text.split(old).length!==2)throw Error('CURRENT_SWITCH_PORT_NOT_UNIQUE:'+symbol);
  text=text.replace(old,()=>next);substitutions.push({symbol,before:old,after:next});
 };
 prefix('ymStartSeamTrace','if(fixtureOrdinarySwitchCliScope && fixtureSwitchOwnedHideSeed && entry=="ensure:main"){auto callback=fixtureSwitchSourceAtEnsure;if(callback)callback();}');
 replace('hidHideApplyPhysicalTransaction','return g_startEntrySeam.hidHideApplyResult;',
  'return fixtureOrdinarySwitchCliScope?fixtureRogDeferredHideSeam(g_startEntrySeam.hidHideApplyResult,transactionId,powerCycle):g_startEntrySeam.hidHideApplyResult;');
 replace('hidHideRestorePhysicalTransaction','return g_startEntrySeam.hidHideRestoreResult;',
  'if(fixtureOrdinarySwitchCliScope && fixtureSwitchOwnedHideSeed && g_startEntrySeam.hidHideRestoreResult)g_hidHideVisibilityJournal=HidHideVisibilityJournal{};return g_startEntrySeam.hidHideRestoreResult;');
 prefix('hidHideRun','if(fixtureOrdinarySwitchCliScope && fixtureOwnedCliBoundary)return fixtureOwnedCliRead(arguments,output,exitCodeOut);');
 prefix('hidHideCaptureVisibilitySnapshot','if(fixtureOrdinarySwitchCliScope){if(fixtureOwnedCliBoundary){++fixtureOwnedCliFullQueries;snapshot={};snapshot.valid=true;snapshot.cloaking=true;snapshot.hidden=fixtureOwnedCliHidden;return true;}if(fixtureRogPartialEnabled || fixtureRogDeferredJournalSeed)return fixtureRogPartialSnapshot(snapshot);}');
 prefix('hidHideGetPhysicalIdentities','if(fixtureOrdinarySwitchCliScope && fixtureOwnedCliBoundary){++fixtureOwnedCliPhysicalQueries;out=fixtureOwnedCliPhysical;return true;}');
 prefix('hidHideCycleInstance','if(fixtureOrdinarySwitchCliScope)return fixtureRogDeferredCycle(instanceId,phase,cycleTimeoutMs);');
 let inverse=text;for(const row of [...substitutions].reverse()){
  if(inverse.split(row.after).length!==2)throw Error('CURRENT_SWITCH_PORT_INVERSE_NOT_UNIQUE:'+row.symbol);
  inverse=inverse.replace(row.after,()=>row.before);
 }
 if(inverse!==source)throw Error('CURRENT_SWITCH_PORT_INVERSE_MISMATCH');
 const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
 return {source:text,substitutions:substitutions.map(({symbol,before,after})=>({symbol,beforeSha256:hash(before),afterSha256:hash(after),
  scope:'Existing native ordinary switch fixture physical/CLI I/O port only; explicit scope required, production owner/queue/return and budget decisions unchanged',inverseVerified:true}))};
}
export function bindCurrentOrdinarySwitchTopology(source,header,{managedInputContract,productPolicySuccessor}={}){
 if(managedInputContract!=='current105-terminal26-sdk-lifetime-compact-v1')return {source,substitution:null};
 const hash=value=>crypto.createHash('sha256').update(value).digest('hex');
 if(productPolicySuccessor!==undefined)admitTerminal26MsiPolicyTopology(header,productPolicySuccessor);
 else if(hash(header)!=='bd7f452113f16779c38793f1207976e268ee370ae7c94ff4184143bbb7e1aeb5')throw Error('CURRENT_SWITCH_TOPOLOGY_HEADER_PIN_CHANGED');
 const original=extract(header,'externalPriorityUsbPortKind').code,brace=original.indexOf('{');
 const changed=original.slice(0,brace+1)+'\n if(fixtureOrdinarySwitchCliScope)return fixtureRogDeferredUsbPortKind(root);\n'+original.slice(brace+1);
 const adapted=replaceUniqueSourceText(header,original,changed,'ordinary-switch-read-only-topology-leaf');
 const inline=adapted.replace(/^#pragma once/m,'// pragma once: private TU topology header');
 const include='#include "external_controller_priority_runtime.h"';
 const output=replaceUniqueSourceText(source,include,inline,'ordinary-switch-existing-topology-header');
 if(output.replace(inline,()=>include)!==source)throw Error('CURRENT_SWITCH_TOPOLOGY_INCLUDE_INVERSE');
 if(adapted.replace(changed,()=>original)!==header)throw Error('CURRENT_SWITCH_TOPOLOGY_BODY_INVERSE');
 return {source:output,substitution:{symbol:'externalPriorityUsbPortKind',source:'native/external_controller_priority_runtime.h',sourceSha256:hash(header),beforeSha256:hash(original),afterSha256:hash(changed),
  scope:'Existing native ordinary switch read-only OS topology leaf; opt-in controlled internal-port input only, complete production header and nonselected body preserved',inverseVerified:true}};
}
// Cheap read-only generation checks precede the full source/tool batch.
// They grant neither live compilation authority nor runtime acceptance.
export const currentPsReturnDevicePorts=Object.freeze({SetupDiGetClassDevsW:'fixturePsGetClassDevs',SetupDiEnumDeviceInterfaces:'fixturePsEnum',SetupDiGetDeviceInterfaceDetailW:'fixturePsDetail',SetupDiDestroyDeviceInfoList:'fixturePsDestroy',CM_Get_Device_IDW:'fixturePsDeviceId',CM_Get_Parent:'fixturePsParent',CM_Locate_DevNodeW:'fixturePsLocate'});
// Compiler response files belong beside this invocation's outputs. A deeply
// nested evidence-directory TEMP can make cl fail only after all three TUs.
export function currentCompilePrivateEnvironment(output,inherited=process.env){
 if(typeof output!=='string'||!path.isAbsolute(output))throw Error('ABSOLUTE_PRIVATE_COMPILER_OUTPUT_REQUIRED');
 const temporary=path.join(path.resolve(output),'tmp'),environment={};
 for(const [key,value]of Object.entries(inherited))if(!['TEMP','TMP'].includes(key.toUpperCase()))environment[key]=value;
 return {temporary,environment:{...environment,TEMP:temporary,TMP:temporary}};
}
export const currentNativeSourceRoots=Object.freeze(['native','capabilities','deps/json','deps/adlx-sdk','deps/webview2/build/native/include']);
export function bindNativeEventHelloIdentity(original){
 const lf=String.fromCharCode(10),crlf=String.fromCharCode(13,10);
 const candidates=[lf,crlf].map(eol=>({eol,before:['    return true;','#else'].join(eol)}));
 const matches=candidates.flatMap(row=>Array.from({length:original.split(row.before).length-1},()=>row));
 if(matches.length!==1)throw Error('NATIVE_EVENT_HELLO_SUCCESS_ANCHOR_REQUIRED');
 const {eol,before}=matches[0];
 const after='    if(cmd=="HELLO")g_inputHostHostInstanceId="fixture-native-event-"+std::to_string(g_inputHostEpoch)+"-"+g_inputHostRunId;'+eol+before;
 return original.replace(before,after);
}
export function prefixCurrentRuntimeRecoveryTransport(original,name){
 const bodies={
 "inputHostWaitClientLocked": "if(fixtureRuntimeRecoveryFaultEnabled && !fixtureRuntimeRecoveryFaultConsumed && (!fixtureRuntimeRecoveryFirstHelloOnly || g_inputHostSwitchRecoveryStartRequestId!=0)){fixtureRuntimeRecoveryFaultConsumed=true;++fixtureRuntimeRecoveryFaultCalls;if(contextRejectedOut)*contextRejectedOut=false;if(errorOut)*errorOut=fixtureStartupTransportError;return false;}",
 "inputHostPipeIo": "if(fixtureRuntimeRecoveryFaultEnabled && !fixtureRuntimeRecoveryFaultConsumed && !fixtureRuntimeRecoveryFirstHelloOnly){fixtureRuntimeRecoveryFaultConsumed=true;++fixtureRuntimeRecoveryFaultCalls;if(transferredOut)*transferredOut=0;if(contextRejectedOut)*contextRejectedOut=false;if(errorOut)*errorOut=fixtureStartupTransportError;return false;}"
};
 if(!Object.hasOwn(bodies,name))throw Error('UNKNOWN_RUNTIME_RECOVERY_TRANSPORT_ENDPOINT');
 if(!original.includes('bool* contextRejectedOut=nullptr')||!original.includes('if(contextRejectedOut)*contextRejectedOut=result.contextRejected;')||!original.includes('ymcc::inputHost::pipeTransport::processRuntime().run(')||!original.includes('ymcc::inputHost::pipeTransport::Cancellation{'))throw Error('ACTUAL_TRANSPORT_CANCELLATION_AND_REJECT_OUTPUT_REQUIRED:'+name);
 const brace=original.indexOf('{'),lf=String.fromCharCode(10);if(brace<0)throw Error('TRANSPORT_FUNCTION_BODY_REQUIRED');
 return original.slice(0,brace+1)+lf+bodies[name]+lf+original.slice(brace+1);
}
export function replaceCurrentTransportSource(text,before,after,label){
 if(!before||text.split(before).length!==2)throw Error('ACTUAL_TRANSPORT_EXACT_SOURCE_REQUIRED:'+label);
 const changed=text.replace(before,()=>after);
 if(changed.replace(after,()=>before)!==text)throw Error('ACTUAL_TRANSPORT_SOURCE_NOT_REVERSIBLE:'+label);
 return changed;
}
export function prefixCurrentTargetReturnWireObserver(original){
 const brace=original.indexOf('{');if(brace<0)throw Error('TRANSPORT_FUNCTION_BODY_REQUIRED');
 const lf=String.fromCharCode(10),body=lf+'    if(fixtureFullPipe && write)fixtureTargetReturnWireObserve(buffer,size);'+lf;
 return original.slice(0,brace+1)+body+original.slice(brace+1);
}
// The controller fixture excludes the native touchpad input surface. Keep its
// newly referenced creation leaf denied; never synthesize a successful channel.
export function bindCurrentTouchpadInputFixturePort(source,{nativeSourceProfile}={}){
 if(nativeSourceProfile===undefined)return {source,substitutions:[]};
 terminal26NativeSourceProfileFiles(nativeSourceProfile);
 const anchor='#include "screen_touchpads.h"';
 if(source.split(anchor).length!==2)throw Error('CURRENT_TOUCHPAD_INPUT_INCLUDE_REQUIRED');
 const prefix=[
  '#ifndef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE',
  'namespace ymcc::screenpads {',
  'struct FixtureDeniedTouchpadInputRuntime {',
  ' HRESULT error=E_UNEXPECTED;',
  ' bool useFreshInputChannel(){++::forbiddenIo;throw std::logic_error("unexpected-offline-touchpad-input-channel-reset");}',
  '};',
  'inline FixtureDeniedTouchpadInputRuntime nativePadInputRuntime;',
  '}',
  '#endif'
 ].join('\n')+'\n';
 const replacement=prefix+anchor,adapted=source.replace(anchor,()=>replacement);
 if(adapted.replace(replacement,()=>anchor)!==source)throw Error('CURRENT_TOUCHPAD_INPUT_PORT_INVERSE_REQUIRED');
 const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
 return {source:adapted,substitutions:[{symbol:'ymcc::screenpads::nativePadInputRuntime',beforeSha256:hash(anchor),afterSha256:hash(replacement),
  rejectOnly:true,sourceCaptureModified:false,scope:'Native touchpad input surface is excluded in this controller fixture; any channel creation increments forbiddenIo and throws before hardware or thread work.'}]};
}
export function validateCurrentNativeSourceSelection({managedInputContract,nativeSourceProfile,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,runtimeReusePolicy}={}){
 if(runtimeReusePolicy!==undefined&&(runtimeReusePolicy!=='owned-direct-child-v1'||nativeSourceProfile!=='current105-terminal26-current-capture-20261117a'||managedInputContract!=='current105-terminal26-sdk-lifetime-compact-v1'||[productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout].some(value=>value!==undefined)))throw Error('CURRENT26_EXACT_PUBLIC_RUNTIME_POLICY_REQUIRED');
 if(nativeSourceProfile===undefined)return;
 terminal26NativeSourceProfileFiles(nativeSourceProfile);
 if(!managedCompileContract(managedInputContract).signalEvidence)throw Error('CURRENT26_NATIVE_SOURCE_PROFILE_CONTRACT_REQUIRED');
 if([productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout].some(value=>value!==undefined))throw Error('CURRENT26_NATIVE_SOURCE_PROFILE_PRIVATE_SUCCESSOR_CONFLICT');
}
export function preflightCurrentCompileSources({sourceRoot,managedInputContract,runtimeRoot,checkProgress=false,finalStages=false,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,runtimeReusePolicy,nativeSourceProfile}={}){
 validateCurrentNativeSourceSelection({managedInputContract,nativeSourceProfile,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,runtimeReusePolicy});
 if(!managedCompileContract(managedInputContract).signalEvidence)throw Error('CURRENT26_PREFLIGHT_EXPLICIT_CONTRACT_REQUIRED');
 for(const relative of currentNativeSourceRoots)if(!fs.lstatSync(path.join(sourceRoot,relative)).isDirectory())throw Error('NATIVE_SOURCE_ROOT_REQUIRED:'+relative);
 for(const relative of ['deps/adlx-sdk/ADLX.h','deps/adlx-sdk/I3DSettings3.h'])if(!fs.lstatSync(path.join(sourceRoot,relative)).isFile())throw Error('NATIVE_ADLX_DECLARED_HEADER_REQUIRED:'+relative);
 const expanded=readVirtualControllerSource(path.join(sourceRoot,'native/main.cpp'));
 const repairedProduct=reviewedTerminal26ProductSourceSuccessor(expanded.source,{sourceRoot,managedInputContract,productSourceSuccessor});
 const productSourceSuccessorProvenance=repairedProduct.productSourceSuccessor;
 const policy=pairTerminal26MsiPolicyProducts(expanded.source,repairedProduct,{sourceRoot,managedInputContract,productPolicySuccessor,productSourceSuccessor});
 const source=policy.source;
 const ordinarySwitchPorts=bindCurrentOrdinarySwitchFixturePorts(source,{managedInputContract});
 const ordinarySwitchTopology=bindCurrentOrdinarySwitchTopology(source,(policy.products.get('native/external_controller_priority_runtime.h')??fs.readFileSync(path.join(sourceRoot,'native/external_controller_priority_runtime.h'))).toString('utf8'),{managedInputContract,productPolicySuccessor});
 const msiCleanup=reviewedTerminal26MsiCleanupHeader(fs.readFileSync(path.join(sourceRoot,'native/msi_native_foreground_isolation_runtime.h')),{sourceRoot,managedInputContract,productHeaderSuccessor});
 const helloEndpoint=bindNativeEventHelloIdentity(extract(source,'ymStartPipeSeamServe').code);
 const runtimeRecoveryTransport=['inputHostWaitClientLocked','inputHostPipeIo'].map(symbol=>{
  const original=extract(source,symbol).code,prefixed=prefixCurrentRuntimeRecoveryTransport(original,symbol);
  const complete=symbol==='inputHostPipeIo'?prefixCurrentTargetReturnWireObserver(prefixed):prefixed;
  replaceCurrentTransportSource(source,original,complete,'preflight-'+symbol);
  if(symbol==='inputHostPipeIo')replaceCurrentTransportSource(prefixed,prefixed,complete,'preflight-wire-observer');
  return {symbol,sha256:crypto.createHash('sha256').update(complete).digest('hex')};
 });
 const core=currentCompileCoreSymbols.map(symbol=>{const row=extract(source,symbol);return {symbol,sha256:row.sha256};});
 extract(source,'ColdEnvironment','struct');
 const serialOwner=extract(source,'gamepadSerialExitOwnershipReleased');
 if(!serialOwner.code.startsWith('static bool gamepadSerialExitOwnershipReleased() noexcept'))throw Error('ACTUAL_SERIAL_EXIT_OWNER_SIGNATURE_REQUIRED');
 const psHeader=fs.readFileSync(path.join(sourceRoot,'native/ps_endpoint_return_runtime.h'),'utf8');
 const psReturnApis=[...new Set([...psHeader.matchAll(/\b(SetupDi\w+|CM_\w+)\s*\(/g)].map(row=>row[1]))];
 if(psReturnApis.some(name=>!Object.hasOwn(currentPsReturnDevicePorts,name)))throw Error('UNADMITTED_PS_RETURN_DEVICE_API:'+psReturnApis.join(','));
 const psLocate=extract(fs.readFileSync(path.join(sourceRoot,'tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc'),'utf8'),'fixturePsLocate');
 const guarded=guardCurrentSteam(source,{sourceRoot,managedInputContract,nativeSourceProfile});
 if(!guarded.nativeGuardProvenance||guarded.substitutions.length!==3)throw Error('CURRENT26_THREE_THROW_ONLY_GUARDS_REQUIRED');
 const touchpadInputPort=bindCurrentTouchpadInputFixturePort(guarded.inert,{nativeSourceProfile});
 const data=currentLifecycleLogBoundary(sourceRoot,{managedInputContract,nativeSourceProfile});
 const tupleRelative='tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc',tupleOriginal=fs.readFileSync(path.join(sourceRoot,tupleRelative)),tupleOutput=currentFixtureSourceOverride(tupleRelative,tupleOriginal,{sourceRoot,managedInputContract,nativeSourceProfile});
 const tupleFixtureRepair={relative:tupleRelative,inputSha256:crypto.createHash('sha256').update(tupleOriginal).digest('hex'),outputSha256:crypto.createHash('sha256').update(tupleOutput).digest('hex'),sourceCaptureModified:false,originalCaseSelectionPreserved:true};
 const observed=repairCurrent105LifecycleFixtureInputs(observeCurrent105Fixture(fs.readFileSync(path.join(sourceRoot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8'),{sourceRoot,managedInputContract,nativeSourceProfile}),{sourceRoot,managedInputContract,nativeSourceProfile});
 const isolated=isolateCurrentFixtureSettingsMutex(observed.text,{runtimeRoot,moduleMaintenance:true,managedInputContract,runtimeParameterLayout,runtimeReusePolicy});
 if(runtimeReusePolicy!==undefined&&!isolated.runtimeEntryAdmissionEnforced)throw Error('COMPLETE_CURRENT26_RUNTIME_ENTRY_ADMISSION_REQUIRED');
 const policyFixture=pairTerminal26MsiPolicyFixture(isolated.text,{productPolicySuccessor,fixtureInputSuccessor,productSource:source});
 const progress=checkProgress?observeCurrent105Progress(policyFixture.text):null;
 const stages=finalStages?observeFinalStages(progress?.text??policyFixture.text):null;
 return {status:'STATIC_GENERATION_PREFLIGHT_NOT_COMPILE_AUTHORITY',...(runtimeReusePolicy===undefined?{}:{runtimeReusePolicy}),...(nativeSourceProfile===undefined?{}:{nativeSourceProfile,nativeGuardProvenance:guarded.nativeGuardProvenance,touchpadInputPort:touchpadInputPort.substitutions}),productSourceSuccessor:productSourceSuccessorProvenance,productPolicySuccessor:policy.provenance,policyFixture:policyFixture.provenance,ordinarySwitchPorts:ordinarySwitchPorts.substitutions,ordinarySwitchTopology:ordinarySwitchTopology.substitution,productHeaderSuccessor:msiCleanup.productHeaderSuccessor,coreMembers:core.length,core,psReturnApis,psLocateControlledLeafSha256:psLocate.sha256,defaultLoggerUtf8Bytes:Buffer.byteLength(data),fixtureSha256:crypto.createHash('sha256').update(stages?.text??progress?.text??policyFixture.text).digest('hex'),nativeEventHelloEndpointSha256:crypto.createHash('sha256').update(helloEndpoint).digest('hex'),runtimeRecoveryTransport,tupleFixtureRepair,checkProgress,finalStages,lifecycleFixtureInputRepair:observed.lifecycleFixtureInputRepair,sourceApprovalGranted:false,publicRuntimeAccepted:false,compilerCalls:0};
}
export function buildCurrentCompileOnly({argv=[],batchAuthority,runtimeRoot,checkProgress=false,finalStages=false,nativeOptimization='O2',productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,runtimeReusePolicy,nativeSourceProfile}={}){
 if(typeof checkProgress!=='boolean')throw Error('BOOLEAN_CHECK_PROGRESS_REQUIRED');
 if(typeof finalStages!=='boolean')throw Error('BOOLEAN_FINAL_STAGES_REQUIRED');
 if(!['O2','Od'].includes(nativeOptimization))throw Error('CURRENT_NATIVE_OPTIMIZATION_REQUIRED');
 if(runtimeRoot!==undefined)privateSettingsMutexDefinitionForRuntimeRoot(runtimeRoot);

const rawArgs=[...argv];if(rawArgs.filter(arg=>arg==='--compile-only').length!==1)throw Error('Explicit compile-only argument required');argv=rawArgs.filter(arg=>arg!=='--compile-only');
const sourceAt=argv.indexOf('--source-root');const suppliedRoot=sourceAt<0?null:argv[sourceAt+1];if(sourceAt>=0)argv.splice(sourceAt,2);

const layout=requireCurrentCompileBatch(batchAuthority,{output:argv[1]}),managedContract=managedCompileContract(layout.managedInputContract);validateCurrentNativeSourceSelection({managedInputContract:layout.managedInputContract,nativeSourceProfile,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,runtimeReusePolicy});if(finalStages&&!managedContract.terminalCleanup)throw Error('FINAL_STAGES_TERMINAL22_ONLY');const repo=layout.sourceRoot;const args=[...argv];if(suppliedRoot&&path.resolve(suppliedRoot)!==path.resolve(repo))throw Error('Explicit current sourceRoot differs from authority');if(![2,4,6,8,10].includes(args.length)||args[0]!=="--out")throw Error("Use --out ABS_NEW_ROOT [--mutation ID] [--managed-dll SDK_FREE_DLL] [--suite all|persona-lifecycle]");
let mutation=null,managedDll=null,suite=null,coldRecord=null;const seen=new Set();for(let i=2;i<args.length;i+=2){const key=args[i];if(!["--mutation","--managed-dll","--suite","--cold-record"].includes(key)||seen.has(key)||!args[i+1])throw Error("Invalid or repeated option");seen.add(key);if(key==="--mutation")mutation=args[i+1];else if(key==="--managed-dll")managedDll=path.resolve(args[i+1]);else if(key==="--cold-record")coldRecord=path.resolve(args[i+1]);else{if(!["all","persona-lifecycle","cold-archive"].includes(args[i+1]))throw Error("Unknown suite");suite=args[i+1];}}
if(suite&&(!managedDll||mutation))throw Error("All suites require an owned managed DLL and no directed mutation");
if(mutation&&!["bypass-admission","rollback-recovery-inheritance","rollback-cancellation-guards","rollback-request-ownership","rollback-ps-endpoint-ownership","rollback-ps-binding-revision","rollback-switch-return-rearm","rollback-switch-fault-owner","rollback-pipe-stage-budget","rollback-initial-start-budget","rollback-wake-return-continuation","rollback-wake-retirement-identity","rollback-wake-start-budget","rollback-queued-player-binding","rollback-stale-off-direction","rollback-off-return-debt","rollback-off-start-blocked","rollback-prepare-claim","rollback-unsent-prepare-claim","rollback-live-explicit-restore","rollback-ps-return-xinput","rollback-ps-return-pin","rollback-ps-return-physical","rollback-ps-return-family","rollback-exit-final-sweep","rollback-target-reservation","rollback-target-pin-store","rollback-target-settlement"].includes(mutation))throw Error("Unknown mutation");
if(["rollback-cancellation-guards","rollback-request-ownership","rollback-ps-endpoint-ownership","rollback-ps-binding-revision","rollback-pipe-stage-budget","rollback-initial-start-budget","rollback-wake-return-continuation","rollback-wake-retirement-identity","rollback-wake-start-budget","rollback-queued-player-binding","rollback-stale-off-direction","rollback-off-return-debt","rollback-off-start-blocked","rollback-prepare-claim","rollback-unsent-prepare-claim","rollback-live-explicit-restore","rollback-ps-return-xinput","rollback-ps-return-pin","rollback-ps-return-physical","rollback-ps-return-family","rollback-exit-final-sweep","rollback-target-reservation","rollback-target-pin-store","rollback-target-settlement"].includes(mutation)&&!managedDll||managedDll&&mutation&&!["bypass-admission","rollback-recovery-inheritance","rollback-switch-return-rearm","rollback-switch-fault-owner"].includes(mutation)&&!["rollback-cancellation-guards","rollback-request-ownership","rollback-ps-endpoint-ownership","rollback-ps-binding-revision","rollback-pipe-stage-budget","rollback-initial-start-budget","rollback-wake-return-continuation","rollback-wake-retirement-identity","rollback-wake-start-budget","rollback-queued-player-binding","rollback-stale-off-direction","rollback-off-return-debt","rollback-off-start-blocked","rollback-prepare-claim","rollback-unsent-prepare-claim","rollback-live-explicit-restore","rollback-ps-return-xinput","rollback-ps-return-pin","rollback-ps-return-physical","rollback-ps-return-family","rollback-exit-final-sweep","rollback-target-reservation","rollback-target-pin-store","rollback-target-settlement"].includes(mutation))throw Error("Cancellation negative requires managed DLL; other negatives remain native-only");
const expectedMutationAssertion=({"bypass-admission":"actual durable request locks before consumer runs","rollback-recovery-inheritance":"single real return refresh restores requested operational target","rollback-cancellation-guards":"actual RECREATE primitive refuses changed admission at send boundary","rollback-request-ownership":"actual RECREATE primitive refuses changed admission at send boundary","rollback-ps-endpoint-ownership":"same-profile peer or retired endpoint cannot prove current owned PS readiness","rollback-ps-binding-revision":"new target generation invalidates a slow observation even when instance names are reused","rollback-switch-return-rearm":"verified late cleanup queues only the continuation of the already charged recovery","rollback-switch-fault-owner":"one current frame fault has exactly one serialized Stop owner","rollback-pipe-stage-budget":"every actual connect write and read receives only the original player's remaining deadline budget","rollback-initial-start-budget":"normal first Start binds every actual lifecycle pipe stage to the original player execution period","rollback-wake-return-continuation":"dead-Host wake consumes proven old return then creates exactly one new owned Host","rollback-wake-retirement-identity":"rejected dead-Host return retirement logs exact original wakeSerial requestId powerGeneration and hostEpoch","rollback-wake-start-budget":"PC dead-Host wake permits at most three actual no-Host Start attempts after proven return","rollback-queued-player-binding":"expired queued first Start cannot create a Host or target after original thirty-second budget","rollback-stale-off-direction":"expired old OFF cannot take generic Start through an also-expired newer ON","rollback-off-return-debt":"unproven MSI return debt cannot finalize OFF success after eleven seconds","rollback-off-start-blocked":"fully returned ROG OFF succeeds with StartBlocked retained and no automatic Start","rollback-prepare-claim":"lost PREPARE ACK must not restore physical visibility while actual target cleanup is still unproven","rollback-unsent-prepare-claim":"unissued PREPARE cannot create a cleanup claim or target ownership","rollback-live-explicit-restore":"explicit OFF cannot override live unknown target return and restore physical visibility","rollback-ps-return-xinput":"old PS owned HID present or unknown must veto XInput-only physical restoration and tuple retirement","rollback-ps-return-pin":"complete owned-PS absence allows the actual deferred restore and exact old tuple retirement","rollback-ps-return-physical":"failed physical restore retains the exact PS endpoint return identity for the next probe","rollback-ps-return-family":"unknown old PS root cannot be discharged by profile presence or incomplete family census","rollback-exit-final-sweep":"final app-exit sweep cannot bypass live Host or unproven target ownership","rollback-target-reservation":"actual PREPARE or RECREATE wire is preceded by persisted targetMayExist for exact current owner","rollback-target-pin-store":"accepted real PS State pin is persisted after current acceptance","rollback-target-settlement":"complete Stop consumes exactly the settled old current target-only origins"})[mutation];
if(suite==="cold-archive"&&(!coldRecord||!managedDll))throw Error("cold-archive requires --cold-record and --managed-dll");
const out=path.resolve(args[1]);if(fs.existsSync(out))throw Error('Fresh out required');fs.mkdirSync(out,{recursive:true});const snapshot=path.join(out,'source'),sha=b=>crypto.createHash('sha256').update(b).digest('hex'),records=[];const managedProof=managedDll?{dll:{path:managedDll,sha256:sha(fs.readFileSync(managedDll))},manifest:path.join(path.dirname(managedDll),'fixture-manifest.json')}:null;if(managedProof){managedProof.manifestSha256=sha(fs.readFileSync(managedProof.manifest));const m=JSON.parse(fs.readFileSync(managedProof.manifest));if(m.realSdkLoaded!==false||m.hardwareIo!==false||m.status!=='BUILT'||path.resolve(m.fixtureDll)!==managedDll)throw Error('Not an owned SDK-free compiled fixture');
 try{assertManagedManifestBinding(fs.readFileSync(managedProof.manifest,'utf8'));const b=m.inputBindings;if(!Array.isArray(b?.sources)||b.sources.length<1||b.sources.length>64||!Array.isArray(b.generatedInputs)||b.generatedInputs.length>64||!Array.isArray(m.runtimeArtifacts)||m.runtimeArtifacts.length>32)throw Error('MANAGED_PREREQUISITE_BINDINGS_INVALID');for(const row of b.sources){if(sha(fs.readFileSync(row.path))!==row.sha256||sha(fs.readFileSync(row.snapshot))!==row.sha256)throw Error('MANAGED_PREREQUISITE_STALE_SOURCE:'+row.relative);}for(const row of [...b.generatedInputs,...m.runtimeArtifacts])if(sha(fs.readFileSync(row.path))!==row.sha256)throw Error('MANAGED_PREREQUISITE_STALE_ARTIFACT:'+row.path);managedProof.preflight={sourceBindings:b.sources.length,generatedBindings:b.generatedInputs.length,runtimeArtifacts:m.runtimeArtifacts.length,status:'VERIFIED-BEFORE-NATIVE-COMPILE'};}catch(error){fs.writeFileSync(path.join(out,'managed-prerequisite-failure.json'),JSON.stringify({status:'REJECTED',stage:'before-native-compilation',reason:error.message,managedProof,normalHostMainExecuted:false,realDeviceIo:false},null,2));throw error;}
}
function copy(dir,rel){for(const e of fs.readdirSync(dir,{withFileTypes:true})){if(e.isSymbolicLink())throw Error('source link');const file=path.join(dir,e.name),next=path.join(rel,e.name);if(e.isDirectory())copy(file,next);else if(/\.(?:h|hpp|hxx|c|cc|cpp|inc|inl)$/i.test(e.name)){const target=path.join(snapshot,next);fs.mkdirSync(path.dirname(target),{recursive:true});const bytes=fs.readFileSync(file);fs.writeFileSync(target,bytes);records.push({path:next.replaceAll('\\','/'),original:file,snapshot:target,sha256:sha(bytes)});}}}
for(const rel of currentNativeSourceRoots)copy(path.join(repo,rel),rel);
const fixtureToolHeaders=[...currentFixtureToolHeaders(repo),...currentFixtureCaseIncludes(repo)];
for(const rel of new Set([...fixtureToolHeaders,...(finalStages?['tools/fixtures/input_coordinator_lifecycle/final_stages.mjs']:[]),'tools/fixtures/input_coordinator_lifecycle/fixture.cpp','tools/fixtures/input_coordinator_lifecycle/cold_archive_cases.inc','tools/fixtures/input_coordinator_lifecycle/target_return_cases.inc','tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc','tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc','tools/fixtures/input_coordinator_lifecycle/off_lifecycle_cases.inc','tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc','tools/fixtures/input_coordinator_lifecycle/ps_return_cases.inc','tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc','tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc','tools/input_coordinator_lifecycle_selftest.mjs','tools/virtual_controller_runtime_source.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/input_coordinator_lifecycle/compiler_inputs.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs'])){const target=path.join(snapshot,rel);fs.mkdirSync(path.dirname(target),{recursive:true});const bytes=fs.readFileSync(path.join(repo,rel));fs.writeFileSync(target,bytes);records.push({path:rel,original:path.join(repo,rel),snapshot:target,sha256:sha(bytes)});}
const expanded=readVirtualControllerSource(path.join(snapshot,'native/main.cpp'));
const expansionInputs=expanded.inputs.map(input=>{const row=records.find(r=>path.resolve(r.snapshot)===input.file);if(!row||row.sha256!==input.sha256)throw Error('UNBOUND_RUNTIME_EXPANSION_INPUT:'+input.file);return row.path;});
const sourceExpansion=expanded.manifest;
const expandedFile=path.join(snapshot,'native/main.expanded.cpp');fs.writeFileSync(expandedFile,expanded.source,{flag:'wx'});sourceExpansion.expandedSnapshot=expandedFile;
const baselineSource=expanded.source;
const repairedProduct=reviewedTerminal26ProductSourceSuccessor(baselineSource,{sourceRoot:repo,managedInputContract:managedContract.id,productSourceSuccessor});
const productSourceSuccessorProvenance=repairedProduct.productSourceSuccessor;
const policy=pairTerminal26MsiPolicyProducts(baselineSource,repairedProduct,{sourceRoot:repo,managedInputContract:managedContract.id,productPolicySuccessor,productSourceSuccessor});
const source=productPolicySuccessor===undefined?baselineSource:policy.source;
let inert=policy.source;const substitutions=[];let selectedNativeGuardProvenance;
for(const[name,body]of [
 ['inputHostStartProtectionNow','return startProtectionClock;'],
 // Explicit26 controller scope: deny unrelated boot registry/media reads before their OS leaves.
 ...(managedContract.signalEvidence?[
  ['bootModeStartupObservation',"static const ymcc::bootModeIdentity::Observation unavailable{std::string{},ERROR_NOT_SUPPORTED,false};std::cerr<<\"FIXTURE_AUXILIARY_UNAVAILABLE boot-startup-observation\\n\";return unavailable;"],
  ['sharedGameMediaStore',"std::cerr<<\"FIXTURE_AUXILIARY_UNAVAILABLE shared-game-media-store\\n\";throw std::logic_error(\"offline-controller-fixture-shared-media-unavailable\");"],
 ]:[]),
 // The ordinary100 suite stays on its explicit legacy CLI boundary.
 // Any entry to real writer file/process/job leaves is forbidden I/O.
 ['hidHideCliWriterRecordPath','if(fixtureRuntimeRecoveryOsEnabled){fixtureRuntimeRecoveryWriterPathBoundary();return fixtureTargetReturnRecordPath()+L".runtime-cli";}if(fixtureColdArchive)return fixtureTargetReturnRecordPath()+L".cli";if(fixtureFullPipe'+(managedContract.signalEvidence?' || fixtureOrdinarySwitchCliScope':'')+')return fixtureFullPipeCliRecordPath();++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-record-path");'],
 ['hidHideCliWriterMachineBinding','if(fixtureRuntimeRecoveryOsEnabled)return std::string("fixture-machine");if(fixtureColdArchive)return fixtureColdMachine;if(fixtureFullPipe'+(managedContract.signalEvidence?' || fixtureOrdinarySwitchCliScope':'')+')return std::string("fixture-machine");++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-machine-binding");'],
 ['hidHideCliWriterCapture','++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-capture");'],
 ['hidHideCliWriterProbe','++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-process-probe");'],
 ['hidHideCliWriterProbeJob','++forbiddenIo;throw std::logic_error("unexpected-CLI-writer-job-probe");'],
 ['inputHostTargetReturnRecordPath','return fixtureTargetReturnRecordPath();'],
 ['inputHostTargetReturnMachineBinding','return fixtureColdArchive?fixtureColdMachine:"fixture-machine";'],
 ['inputHostTargetReturnCaptureOwner','return fixtureTargetReturnCaptureOwner(process);'],
 // A new CM ancestry OS leaf, separate from the unchanged three environment leaves.
 // Preserve BeforeHide/AfterRestore algorithms; fail on any unexpected real probe.
 ['inputHostTargetReturnVerifyPhysicalPath','++forbiddenIo;throw std::logic_error("unexpected-physical-return-CM-verification");'],
 // Never probe a non-private recorded writer PID from the full-TU fixture.
 ['inputHostTargetReturnProbePhysicalWriter','if(fixtureRuntimeRecoveryOsEnabled)return fixtureRuntimeRecoveryWriter(owner);if(fixtureColdArchive)return fixtureColdWriter(owner);if(fixtureFullPipe)return fixtureFullPipeOwnedWriter(owner);++forbiddenIo;throw std::logic_error("unexpected-physical-return-process-probe");'],
 // The census reader and real CLI --dev-list stay behind these explicit leaves.
 ['inputHostTargetReturnColdAbsence','if(fixtureRuntimeRecoveryOsEnabled)return fixtureRuntimeRecoveryAbsence(knownIds);if(fixtureColdArchive)return fixtureColdAbsence(knownIds);if(fixtureFullPipe)return fixtureFullPipeTargetAbsence(knownIds);++forbiddenIo;throw std::logic_error("unexpected-cold-return-PnP-absence");'],
 ['inputHostTargetReturnColdHidden','++forbiddenIo;throw std::logic_error("unexpected-cold-return-hidden-read");'],
 ['inputHostTargetReturnPsProfileAbsence','return fixturePsProfileAbsence();'],
 ['observeTdpRuntimeIdentity','/* private offline input fixture: no TDP observation */'],
 ['inputCaptureAssetsPresent','return true;'],
 ['oemBackMappingRefreshGate','/* Separate OEM hook/collector setup excluded; no physical keyboard hook in this fixture. */'],
 ['reg_desktop_monitor_hud','++forbiddenIo;throw std::logic_error("unexpected-desktop-HUD-entry");'],
 ['msiDinputOpen','++forbiddenIo;throw std::logic_error("unexpected-physical-DInput-open");'],
 ['msiClawOpenCommandHid','++forbiddenIo;throw std::logic_error("unexpected-physical-command-HID-open");'],
 ['ymccXInputGetState14','return fixtureControlledXinputState(userIndex,state);'],
 ['ymccXInputGetStateSecret14','++inertXinputReads;return ERROR_DEVICE_NOT_CONNECTED;'],
 ['ymccXInputSetState14','++forbiddenIo;return ERROR_DEVICE_NOT_CONNECTED;'],
 ['ymccOpenXInputGetDevicePath','return fixtureControlledXinputPath(userIndex);'],
 ['ymccOpenXInputGetUserIndex','return fixtureControlledXinputIndex(devicePath);'],
 ['ymccOpenXInputAvailable','return fixtureXinputSourceLive;'],
 ['hidHideServiceRunning','if(queriedOk)*queriedOk=true;ymStartSeamTrace("health:service-query");fixtureHealthInventoryRead=true;return true;'],
 ['hidHideRun','if(fixtureExitSweep){if(arguments==L"--dev-list"){++fixtureExitSweepLists;if(output)*output=std::string("--dev-hide ")+char(34)+"HID_TEST_OWNED"+char(34);}else if(arguments.rfind(L"--dev-unhide ",0)==0){++fixtureExitSweepUnhides;}else if(arguments==L"--cloak-off"){++fixtureExitSweepCloakOffs;}else{++forbiddenIo;throw std::logic_error("unadmitted-exit-sweep-operation");}if(exitCodeOut)*exitCodeOut=0;return true;}if(fixtureHealthInventoryRead && arguments==L"--dev-all"){fixtureHealthInventoryRead=false;ymStartSeamTrace("health:readonly-dev-all");if(output)*output="offline-readonly-health-observation";if(exitCodeOut)*exitCodeOut=0;return true;}++forbiddenIo;throw std::logic_error("unexpected-HidHide-CLI-entry:"+W2U(arguments));'],
 ['appendNativeLifecycleLog',currentLifecycleLogBoundary(repo,{managedInputContract:managedContract.id,nativeSourceProfile})]]){
 const old=extract(source,name).code,start=old.indexOf('{'),replacement=old.slice(0,start+1)+'\n'+body+'\n}';if(inert.split(old).length!==2)throw Error('nonunique boundary '+name);inert=inert.replace(old,replacement);substitutions.push({symbol:name,beforeSha256:sha(old),afterSha256:sha(replacement),scope:'Explicit external device/discovery/process/log boundary only'});
}
{const safety=guardCurrentSteam(inert,{sourceRoot:repo,managedInputContract:layout.managedInputContract,nativeSourceProfile});inert=safety.inert;selectedNativeGuardProvenance=safety.nativeGuardProvenance;substitutions.push(...safety.substitutions);const touchpadInputPort=bindCurrentTouchpadInputFixturePort(inert,{nativeSourceProfile});inert=touchpadInputPort.source;substitutions.push(...touchpadInputPort.substitutions);}
// Conditional adapters are fixture-only process/transport/hide/diagnostic endpoints. Actual
// inputHostStart/Stop/queue/clear/request/Win32 pipe implementations stay unchanged.
for(const [name,body]of [['ymStartPipeSeamServe','if(fixtureFullPipe)return false;if(fixtureFrameFirstAck && command && std::string(command)=="SUBMIT_FRAME"){if(responseOut)*responseOut=json{{"status",expectedStatus?expectedStatus:""},{"detail",""},{"firstFrame",true}};return true;}if(fixtureStartupTransportError && command && std::string(command)==g_startEntrySeam.pipeFailCommand)g_inputHostSwitchRecoveryTransportError=fixtureStartupTransportError;'],['ymStartHostExitWait','if(fixtureFullPipe)return WaitForSingleObject(process,5000);'],['ymStartCreateProcessW','if(fixtureFullPipe)return fixtureCreateManagedProcess(si,pi);'],['hidHideRefreshPhysicalHide','if(fixtureWakeScenario){++fixtureWakeHideCalls;auto beforeReturn=std::move(fixtureWakeBeforeHideReturn);fixtureWakeBeforeHideReturn={};if(beforeReturn)beforeReturn();return fixtureWakeHideResult;}'],['inputCaptureLogVisibilitySnapshotGated','if(fixtureWakeScenario){++fixtureWakeVisibilityExclusions;return;}']]){const original=extract(source,name).code;const brace=original.indexOf('{');const changed=original.slice(0,brace+1)+'\n'+body+'\n'+original.slice(brace+1);if(inert.split(original).length!==2)throw Error('adapter source not unique');inert=inert.replace(original,changed);substitutions.push({symbol:name,scope:'Explicit conditional fixture process/pipe/hide/diagnostic ends; existing behavior unchanged when scenario flags are off',beforeSha256:sha(original),afterSha256:sha(changed)});}
// The native-only seam returns before the real HELLO tuple-binding path. Bind
// only its explicit event-backed environment identity after successful HELLO;
// throwing/failed/inactive seams and real managed-pipe runs keep their old path.
{
 const original=extract(inert,'ymStartPipeSeamServe').code;
 const changed=bindNativeEventHelloIdentity(original);
 if(inert.split(original).length!==2)throw Error('NATIVE_EVENT_HELLO_ENDPOINT_NOT_UNIQUE');
 inert=inert.replace(original,changed);
 substitutions.push({symbol:'ymStartPipeSeamServe',scope:'Native-only event-backed synthetic Host identity after successful HELLO only; not an OS process/receipt or cold-restart proof; real pipe and production owner validators unchanged',beforeSha256:sha(original),afterSha256:sha(changed)});
}
{const ordinary=bindCurrentOrdinarySwitchFixturePorts(inert,{managedInputContract:managedContract.id});inert=ordinary.source;substitutions.push(...ordinary.substitutions);}
if(managedContract.signalEvidence){
 const relative='native/external_controller_priority_runtime.h',header=(policy.products.get(relative)??fs.readFileSync(path.join(snapshot,relative))).toString('utf8');
 const ordinary=bindCurrentOrdinarySwitchTopology(inert,header,{managedInputContract:managedContract.id,productPolicySuccessor});
 inert=ordinary.source;substitutions.push(ordinary.substitution);expansionInputs.push(relative);
}
// Reuse the original lifecycle one-shot OS fault inputs; keep the full request
// classifier, cancellation, current owner, return and budget consumers intact.
for(const name of ['inputHostWaitClientLocked','inputHostPipeIo']){
 const original=extract(inert,name).code,changed=prefixCurrentRuntimeRecoveryTransport(original,name);
 inert=replaceCurrentTransportSource(inert,original,changed,'runtime transport OS error leaf '+name);
 substitutions.push({symbol:name,scope:'Opt-in one-shot existing transport OS failure only; complete request classifier and runtime consumer unchanged',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// Read-only write-ahead evidence is observed immediately before the real OS
// pipe primitive. No create/accept/settlement logic is replaced by this adapter.
{
 const original=extract(inert,'inputHostPipeIo').code,changed=prefixCurrentTargetReturnWireObserver(original);
 inert=replaceCurrentTransportSource(inert,original,changed,'target-return-wire-observer');
 substitutions.push({symbol:'inputHostPipeIo/write-entry-observer',scope:'Read-only real Session/store evidence before actual owned pipe write; no command/receipt/decision substitution',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// The original overlapped owned-child pipe IO still executes. For a directed
// receipt test only, replay one previously observed actual report ACK, or corrupt
// one identity/asset field at this explicit wire-read endpoint. No HostSession,
// SDK ownership, envelope parser, tuple classifier or lifecycle branch is replaced.
{
 const original=extract(inert,'inputHostPipeIo').code;
 const anchor='        return result.ok;';if(original.split(anchor).length!==2)throw Error('wire IO return anchor');
 const body=String.raw`
        if(fixturePipeBudgetEnabled)fixturePipeBudgetPort(write?"write":"read",result.ok,timeoutMs,buffer,size);
        if(fixtureFullPipe && fixturePrepareAckLoss && result.ok && !write && transferredOut && *transferredOut){
            const auto received=json::parse(std::string(static_cast<char*>(buffer),*transferredOut),nullptr,false);
            if(received.is_object() && received.value("command","")=="PREPARE_TARGET" && received.value("status","")=="target-prepared"){
                fixturePrepareAckOriginal=received;fixturePrepareAckLossConsumed=true;fixturePrepareAckLoss=false;
                fixtureTargetReturnAckLostObserve(received);
                if(errorOut)*errorOut=ERROR_NO_DATA;if(transferredOut)*transferredOut=0;return false;
            }
        }
        if(fixtureFullPipe && fixtureWireCapture && result.ok && !write && transferredOut && *transferredOut){
            const std::string received(static_cast<char*>(buffer),*transferredOut);
            const auto reply=json::parse(received,nullptr,false);
            if(reply.is_object() && reply.value("command","")=="SUBMIT_FRAME" && reply.value("status","")=="frame-accepted"){
                if(!fixtureWireFault.empty()){
                    std::string replaced;fixtureWireOriginal=reply;
                    if(fixtureWireFault=="replay")replaced=fixtureWirePrevious;
                    else{auto corrupted=reply;if(fixtureWireFault=="asset")corrupted["assetVerified"]=false;else corrupted["hostInstanceId"]="offline-wrong-owned-instance";replaced=corrupted.dump();}
                    if(replaced.empty() || replaced.size()>size)throw std::logic_error("wire-fault-outside-read-buffer");
                    std::memcpy(buffer,replaced.data(),replaced.size());*transferredOut=static_cast<DWORD>(replaced.size());
                    fixtureWireInjected=json::parse(replaced);fixtureWireFaultConsumed=true;fixtureWireFault.clear();
                }else fixtureWirePrevious=received;
            }
        }
`;
 const changed=original.replace(anchor,body+anchor);inert=inert.replace(original,changed);
 substitutions.push({symbol:'inputHostPipeIo',scope:'Original real owned-child pipe read/write, optional one captured ACK replay or one wire field corruption only. Production parser/classifier/HostSession untouched.',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// Original ConnectNamedPipe owner executes. Supply elapsed time only at the
// successful external connect return, exactly like the existing wire observer.
{
 const original=extract(inert,'inputHostWaitClientLocked').code;
 const anchor='        return result.ok;';if(original.split(anchor).length!==2)throw Error('connect budget port anchor');
 const changed=original.replace(anchor,'        if(fixturePipeBudgetEnabled)fixturePipeBudgetPort("connect",result.ok,timeoutMs);\n'+anchor);
 inert=inert.replace(original,changed);substitutions.push({symbol:'inputHostWaitClientLocked',scope:'Original actual owned Connect IO; optional elapsed-clock observation after kernel return only.',beforeSha256:sha(original),afterSha256:sha(changed)});
}
// Exact reader control flow, with only four SetupAPI leaf names rebound to
// owned deterministic endpoints. No read/decision branch is replaced.
{
 const original=extract(inert,'psWakeProbeOwnEndpoint').code;
 let adapted=original;for(const [api,endpoint]of [['SetupDiGetClassDevsW','fixturePsGetClassDevs'],['SetupDiEnumDeviceInterfaces','fixturePsEnum'],['SetupDiGetDeviceInterfaceDetailW','fixturePsDetail'],['SetupDiDestroyDeviceInfoList','fixturePsDestroy'],['CM_Get_Device_IDW','fixturePsDeviceId'],['CM_Get_Parent','fixturePsParent']])adapted=adapted.replaceAll(api+'(',endpoint+'(');
 if(adapted===original||inert.split(original).length!==2)throw Error('PS SetupAPI boundary anchor');inert=inert.replace(original,adapted);substitutions.push({symbol:'psWakeProbeOwnEndpoint',beforeSha256:sha(original),afterSha256:sha(adapted),scope:'Only read-only SetupAPI/CM leaf calls rebound; loop/error/classification/ancestry/decision logic unchanged'});
}
for(const [symbol,leaves]of [
 ['inputHostXboxInterfaceIsTarget',[['SetupDiCreateDeviceInfoList','fixtureXboxCreate'],['SetupDiOpenDeviceInterfaceW','fixtureXboxOpen'],['SetupDiGetDeviceInterfaceDetailW','fixtureXboxDetail'],['SetupDiDestroyDeviceInfoList','fixtureXboxDestroy'],['CM_Get_Device_IDW','fixtureXboxDeviceId']]],
 ['inputHostXboxConfirmStartupLocked',[['CM_Locate_DevNodeW','fixtureXboxLocate'],['CM_Get_DevNode_Status','fixtureXboxStatus']]]]){
 const original=extract(inert,symbol).code;let adapted=original;for(const[from,to]of leaves)adapted=adapted.replaceAll(from+'(',to+'(');if(adapted===original||inert.split(original).length!==2)throw Error('Elite identity leaf adapter anchor:'+symbol);inert=inert.replace(original,adapted);substitutions.push({symbol,beforeSha256:sha(original),afterSha256:sha(adapted),scope:'Only exact read-only SetupAPI/ConfigManager leaf calls rebound to owned identity fixture; Xbox native parsing, matching, poll/state/path/index/readback and decision flow unchanged.'});
}
if(mutation==='bypass-admission'){const anchor='if (!targetRequest.admit(oldValid ? old : json::object(), parsed))';if(inert.split(anchor).length!==2)throw Error('Admission mutation anchor changed');inert=inert.replace(anchor,'if (false && !targetRequest.admit(oldValid ? old : json::object(), parsed))');substitutions.push({symbol:'ymSettingsWriteDocumentUnlocked',scope:'Explicit semantic negative: remove request admission; assertions unchanged',negativeControl:true});}
if(mutation==='rollback-recovery-inheritance'){
 const replace=(a,b)=>{inert=replaceUniqueSourceText(inert,a,b,'Recovery mutation anchor changed');};
 replace('        const auto inherited = g_virtualStartProtection.existingTicket(configuredPersona,inputHostStartProtectionNow());\n        const auto admitted = inherited ? ymcc::VirtualStartProtection::Admission{true,inherited,false}\n            : inputHostStartProtectionConsume(configuredPersona);','        const auto admitted = inputHostStartProtectionConsume(configuredPersona);');
 replace('    } else if (virtualTargetChanged && !targetEnabled &&\n            !(targetRequested && personaAdmitted && !targetMachineRejection)) {','    } else if (virtualTargetChanged && !targetEnabled) {');
 replace('    auto requestTicket = boundMsiPower?entryView.requestId:\n        g_virtualStartProtection.existingTicket(requestedPersona,entryClock);','    auto requestTicket = boundMsiPower?entryView.requestId:\n        g_virtualStartProtection.pendingTicket(requestedPersona,entryClock);');
 substitutions.push({symbol:'inputCaptureRefreshRealStickSettings/inputHostStart',scope:'Explicit semantic negative: reproduce old operational-pause cancellation and loss of inherited recovery ownership; assertions unchanged',negativeControl:true});
}
if(mutation==='rollback-cancellation-guards'){
 const removeRange=(symbol,from,to)=>{const original=extract(inert,symbol).code;const a=original.indexOf(from),b=original.indexOf(to,a);if(a<0||b<=a)throw Error('Cancellation negative anchor drift:'+symbol);const altered=original.slice(0,a)+original.slice(b);if(inert.split(original).length!==2)throw Error('Cancellation symbol not unique');inert=inert.replace(original,altered);};
 removeRange('inputHostPerformPsWakeRearm','    // A complete read is still only an observation.','    const bool rebuiltAlready =');
 const original=extract(inert,'inputHostRecreatePreparedTarget').code,a=original.indexOf('        // The intent/power/exit may change'),b=original.indexOf('            earlyOut = true;',a);if(a<0||b<=a)throw Error('Cancellation recreate anchor');const altered=original.slice(0,a)+'        if (!g_inputHostProcess || !g_inputHostHello || g_inputHostTransportFault) {'+(original.includes('\r\n')?'\r\n':'\n')+original.slice(b);inert=inert.replace(original,altered);
 removeRange('inputHostSubmitPad','    // OFF revokes normal BUS publication immediately,','    const bool powerAdmitted =');
 const actual=extract(inert,'inputHostFrameSubmissionCurrent').code;const newline=actual.includes('\r\n')?'\r\n':'\n';const old='    const bool ready=g_realStickTest.enabled && g_realStickTest.virtualTargetEnabled &&'+newline+'        g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire) &&'+newline+'        g_inputReady.load';if(actual.split(old).length!==2)throw Error('Cancellation frame-ready anchor');inert=inert.replace(actual,actual.replace(old,'    const bool ready=g_inputReady.load'));
 substitutions.push({symbol:'inputHostPerformPsWakeRearm/inputHostRecreatePreparedTarget/inputHostSubmitPad/inputHostFrameSubmissionCurrent',negativeControl:true,scope:'Only revert the four new cancellation admission guards; actual full native worker/transport and managed session remain executed. No device or normal Main.'});
}
if(mutation==='rollback-request-ownership'){
 const newline='\n';
 const replace=(symbol,from,to)=>{const original=extract(inert,symbol).code;inert=inert.replace(original,replaceUniqueSourceText(original,from,to,'Request-ownership negative anchor:'+symbol));};
 replace('inputHostFrameSubmissionCurrent','        g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire) &&'+newline,'');
 replace('inputHostRecreatePreparedTarget','        else if (!g_inputHostRequestedPersonaMatchesBound.load(std::memory_order_acquire))'+newline+'            admissionRefusal = "requested-persona-not-bound";'+newline,'');
 const old=extract(inert,'inputHostSubmitPad').code;const a=old.indexOf('    // A fresh frame for the new request must not be relabeled'),b=old.indexOf('    const bool powerAdmitted =',a);if(a<0||b<=a)throw Error('Request-ownership frame-entry anchor');inert=inert.replace(old,old.slice(0,a)+old.slice(b));
 replace('inputHostPerformPsWakeRearm','            g_virtualStartProtection.view(inputHostStartProtectionNow()).requestId != queryRequestId ||'+newline+'            inputHostEpochToken() != queryHostEpoch ||'+newline,'');
 substitutions.push({symbol:'inputHostPerformPsWakeRearm/inputHostRecreatePreparedTarget/inputHostSubmitPad/inputHostFrameSubmissionCurrent',negativeControl:true,scope:'Only bypass new requested-vs-bound target and query request/Host ownership checks. Prior OFF/exit/power fences remain active; actual settings, queue, native pipe and managed child execute.'});
}

if(mutation==='rollback-ps-endpoint-ownership'){
 const original=extract(inert,'psWakeProbeOwnEndpoint').code,a=original.indexOf('        // A marker names a driver/profile'),b=original.indexOf('        if(owned){',a);if(a<0||b<=a)throw Error('PS root-ownership rollback anchor');
 const legacy='        std::wstring legacy=path;for(auto& ch:legacy)ch=(wchar_t)towupper(ch);\n        const bool marker=legacy.find(L"HIDMAESTRO")!=std::wstring::npos||legacy.find(L"HM-CTL")!=std::wstring::npos||legacy.find(L"VIGEM")!=std::wstring::npos;\n        const bool owned=marker&&legacy.find(L"VID_054C")!=std::wstring::npos&&legacy.find(L"PID_0DF2")!=std::wstring::npos;\n        std::wstring instanceId;\n';
 inert=inert.replace(original,original.slice(0,a)+legacy+original.slice(b));substitutions.push({symbol:'psWakeProbeOwnEndpoint',negativeControl:true,scope:'Only replace SDK-root CM ancestry verdict with original profile-marker matching; completeness, session, request, target generation and managed pipe remain unchanged.'});
}
if(mutation==='rollback-ps-binding-revision'){
 const original=extract(inert,'inputHostPerformPsWakeRearm').code,a=original.indexOf('    // A replacement of the same intended target'),b=original.indexOf('    const bool rebuiltAlready =',a);if(a<0||b<=a)throw Error('PS target revision rollback anchor');inert=inert.replace(original,original.slice(0,a)+original.slice(b));substitutions.push({symbol:'inputHostPerformPsWakeRearm',negativeControl:true,scope:'Only bypass per-target binding revision and same-intent bounded fresh-read after slow query; request/Host/power checks and exact endpoint ownership remain.'});
}
if(mutation==='rollback-switch-return-rearm'){
 const anchor='const bool switchRecoveryRearmPreserved=inputHostSwitchRecoveryReturnedLocked(retiredHostEpoch,retiredRunId);';
 if(inert.split(anchor).length!==2)throw Error('Switch return rearm mutation anchor');
 inert=inert.replace(anchor,'const bool switchRecoveryRearmPreserved=false;');
 substitutions.push({symbol:'inputHostReconcileQuarantineOnLifecycleThread',negativeControl:true,scope:'Only omit exact same-intent rearm after actual proven quarantine return; original assertions unchanged.'});
}
if(mutation==='rollback-switch-fault-owner'){
 const anchor='} else if (reason.rfind("fault-stop:frame-receipt-fault:",0)==0 &&';
 if(inert.split(anchor).length!==2)throw Error('Switch fault owner mutation anchor');
 inert=inert.replace(anchor,'} else if (false && reason.rfind("fault-stop:frame-receipt-fault:",0)==0 &&');
 substitutions.push({symbol:'inputHostRunQueuedLifecycleWork',negativeControl:true,scope:'Only bypass fault-cleanup coalescing and transfer to admitted recovery; original assertions unchanged.'});
}
if(mutation==='rollback-pipe-stage-budget'){
 const original=extract(inert,'inputHostRequestPreparedLocked').code;
 const anchor='const auto executionDeadline=inputHostRequestHasExecutionDeadline(command)';
 if(original.split(anchor).length!==2)throw Error('Pipe deadline rollback anchor');
 const altered=original.replace(anchor,'const auto executionDeadline=false&&inputHostRequestHasExecutionDeadline(command)');
 inert=inert.replace(original,altered);substitutions.push({symbol:'inputHostRequestPreparedLocked',negativeControl:true,scope:'Only remove per-stage original-period budget; existing entry clamp, actual native pipe/Host, ownership, parser and all cases unchanged.'});
}
if(mutation==='rollback-initial-start-budget'){
 const original=extract(inert,'inputHostStart').code;
 const anchor='if(ticket&&view.requestId==ticket&&view.locked){';
 if(original.split(anchor).length!==2)throw Error('Initial Start period rollback anchor');
 inert=inert.replace(original,original.replace(anchor,'if(false&&ticket&&view.requestId==ticket&&view.locked){'));
 substitutions.push({symbol:'inputHostStart',negativeControl:true,scope:'Only omit normal Start attachment to existing player deadline; per-pipe-stage budget, inherited wake/fallback scopes, actual native/managed IO and original cases remain.'});
}
if(mutation==='rollback-wake-return-continuation'){
 const anchor='const bool wakeExecutionReturnPreserved=inputHostWakeReturnedLocked(retiredHostEpoch,retiredRunId);';
 if(inert.split(anchor).length!==2)throw Error('Wake return rollback anchor');
 inert=inert.replace(anchor,'const bool wakeExecutionReturnPreserved=false;');
 substitutions.push({symbol:'inputHostReconcileQuarantineOnLifecycleThread',negativeControl:true,scope:'Only omit original wake continuation after exact common quarantine return; actual Stop/restore/clear, original wake/source cases and real child sessions unchanged.'});
}
// Restore exactly one proven OFF defect in actual production bodies.
if(mutation==='rollback-stale-off-direction'){
 const original=extract(inert,'inputHostRunQueuedLifecycleWork').code;
 const altered=replaceUniqueSourceText(original,'if(reason=="virtual-target-disabled" && g_realStickTest.virtualTargetEnabled &&','if(false && reason=="virtual-target-disabled" && g_realStickTest.virtualTargetEnabled &&',mutation);
 inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostRunQueuedLifecycleWork',negativeControl:true,scope:'Only restore stale OFF promotion to later ON; same real dispatcher, queued work, player periods and actual managed Host.'});
}
if(mutation==='rollback-off-return-debt'||mutation==='rollback-off-start-blocked'){
 const original=extract(inert,'inputHostDisabledCleanupProvenLocked').code;
 const altered=mutation==='rollback-off-return-debt'
  ?replaceUniqueSourceText(original,'&& msiReturnProven;','&& true;',mutation)
  :replaceUniqueSourceText(original,'return !g_realStickTest.virtualTargetEnabled','return !g_inputHostStartBlocked && !g_realStickTest.virtualTargetEnabled',mutation);
 inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostDisabledCleanupProvenLocked',negativeControl:true,scope:mutation==='rollback-off-return-debt'?'Only omit MSI physical return debt from shared Stop/final proof; no synthetic discharge.':'Only restore future Start prohibition as an OFF ownership debt; original quarantine/restore owner and retained flag unchanged.'});
}
if(mutation==='rollback-prepare-claim'){
 const original=extract(inert,'inputHostStopLocked').code;const altered=replaceUniqueSourceText(original,'const bool targetMayExist = hadPreparedTarget || msiSdPinnedOwnership || prepareCreationPossible;','const bool targetMayExist = hadPreparedTarget || msiSdPinnedOwnership;',mutation);
 inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostStopLocked',negativeControl:true,scope:'Only omit uncertain PREPARE creation from actual Stop target ownership; actual held managed target, original real pipe and assertions unchanged.'});
}
if(mutation==='rollback-unsent-prepare-claim'){
 const original=extract(inert,'inputHostStart').code;const anchor='    const bool prepareSent = prepareAttempted && inputHostRequestLocked(';const altered=replaceUniqueSourceText(original,anchor,'    if(prepareAttempted)g_inputHostPrepareMayHaveReachedHost=true;\n'+anchor,mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostStart',negativeControl:true,scope:'Only restore pre-request creation claim before the write boundary; original actual pipe deadline denial and assertions unchanged.'});
}
if(mutation==='rollback-live-explicit-restore'){
 const original=extract(inert,'inputHostStopLocked').code;const altered=replaceUniqueSourceText(original,'const bool explicitRestoreAfterExit = hostExitConfirmed && inputHostStopRestoresVisibilityWithoutProof(reason);','const bool explicitRestoreAfterExit = inputHostStopRestoresVisibilityWithoutProof(reason);',mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostStopLocked',negativeControl:true,scope:'Only restore explicit OFF/exit override while exact old child is still alive; actual held managed target and original OFF/return assertions unchanged.'});
}
if(mutation==='rollback-ps-return-xinput'){
 const original=extract(inert,'inputHostReconcileQuarantineOnLifecycleThread').code;const altered=replaceUniqueSourceText(original,': psReturnRequired ? psRetiredReturnProvenLocked()',': false ? psRetiredReturnProvenLocked()',mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'inputHostReconcileQuarantineOnLifecycleThread',negativeControl:true,scope:'Only restore XInput-plane substitution for retired PS PnP proof; actual accepted old pin, managed disposal and controlled Present/Unknown/Absent reader remain.'});
}
if(mutation==='rollback-exit-final-sweep'){
 const original=extract(inert,'hidHideSweepAllHiddenDevices').code;const altered=replaceUniqueSourceText(original,'const bool guardedExitSweep = reason && std::strcmp(reason,"app-exit")==0;','const bool guardedExitSweep = false && reason && std::strcmp(reason,"app-exit")==0;',mutation);inert=inert.replace(original,()=>altered);substitutions.push({symbol:'hidHideSweepAllHiddenDevices',negativeControl:true,scope:'Only bypass app-exit ownership admission; real held child, PS return state and existing sweep API consumer stay executed with controlled driver leaves.'});
}
// Directed semantic controls remove exactly one actual runtime duty hook.
// Real Session/store, pipe consumer and all original assertions stay executed.
if(['rollback-target-reservation','rollback-target-pin-store','rollback-target-settlement'].includes(mutation)){
 const symbol=mutation==='rollback-target-reservation'?'inputHostTargetReturnBeforeCreateLocked':mutation==='rollback-target-pin-store'?'inputHostTargetReturnAcceptPsLocked':'inputHostTargetReturnTargetExitedLocked';
 const original=extract(inert,symbol).code,brace=original.indexOf('{');
 const altered=original.slice(0,brace+1)+'\n    '+(mutation==='rollback-target-reservation'?'return true;':'return;')+' // explicit compiled semantic negative\n'+original.slice(brace+1);
 inert=replaceUniqueSourceText(inert,original,altered,mutation);
 substitutions.push({symbol,negativeControl:true,scope:'Only bypass this selected target duty hook in private compiled TU; actual IO/receipts/ownership and assertions unchanged',beforeSha256:sha(original),afterSha256:sha(altered)});
}
const core=currentCompileCoreSymbols.map(name=>{const actual=extract(source,name).code;const same=extract(inert,name).code===actual;if(!same&&!['ymStartPipeSeamServe','psWakeProbeOwnEndpoint','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked'].includes(name)&&!(mutation==='rollback-recovery-inheritance'&&['inputHostStart','inputCaptureRefreshRealStickSettings'].includes(name))&&!(['rollback-cancellation-guards','rollback-request-ownership'].includes(mutation)&&['inputHostPerformPsWakeRearm','inputHostRecreatePreparedTarget','inputHostSubmitPad','inputHostFrameSubmissionCurrent'].includes(name))&&!(mutation==='rollback-ps-binding-revision'&&name==='inputHostPerformPsWakeRearm')&&!(mutation==='rollback-switch-return-rearm'&&name==='inputHostReconcileQuarantineOnLifecycleThread')&&!(mutation==='rollback-switch-fault-owner'&&name==='inputHostRunQueuedLifecycleWork')&&!(mutation==='rollback-stale-off-direction'&&name==='inputHostRunQueuedLifecycleWork')&&!(mutation==='rollback-prepare-claim'&&name==='inputHostStopLocked')&&!(mutation==='rollback-live-explicit-restore'&&name==='inputHostStopLocked')&&!(mutation==='rollback-ps-return-xinput'&&name==='inputHostReconcileQuarantineOnLifecycleThread')&&!(mutation==='rollback-unsent-prepare-claim'&&name==='inputHostStart')&&!(mutation==='rollback-pipe-stage-budget'&&name==='inputHostRequestPreparedLocked')&&!(mutation==='rollback-initial-start-budget'&&name==='inputHostStart')&&!(mutation==='rollback-wake-return-continuation'&&name==='inputHostReconcileQuarantineOnLifecycleThread')&&!(mutation==='rollback-target-reservation'&&name==='inputHostTargetReturnBeforeCreateLocked')&&!(mutation==='rollback-target-pin-store'&&name==='inputHostTargetReturnAcceptPsLocked')&&!(mutation==='rollback-target-settlement'&&name==='inputHostTargetReturnTargetExitedLocked'))throw Error('core modified '+name);return {symbol:name,sha256:sha(actual),executedSha256:sha(extract(inert,name).code),unchanged:same,fixtureEndpointAdapter:['ymStartPipeSeamServe','psWakeProbeOwnEndpoint','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked'].includes(name),expectedSemanticNegative:!!mutation&&!same&&(mutation==='rollback-ps-endpoint-ownership'?name==='psWakeProbeOwnEndpoint':!['ymStartPipeSeamServe','psWakeProbeOwnEndpoint','inputHostXboxInterfaceIsTarget','inputHostXboxConfirmStartupLocked'].includes(name))};});

// Reuse the generic fixture's capability switch. The existing production
// noexcept owner must be compiled; the old-source false fallback stays disabled.
if(source.includes('static bool gamepadSerialExitOwnershipReleased() noexcept')){
 const serialSource=extract(source,'gamepadSerialExitOwnershipReleased').code;
 if(extract(inert,'gamepadSerialExitOwnershipReleased').code!==serialSource)throw Error('ACTUAL_SERIAL_EXIT_OWNER_MUST_REMAIN_VERBATIM');
 inert='#define YMCC_FIXTURE_HAS_SERIAL_EXIT_OWNER 1\n'+inert;
 substitutions.push({symbol:'YMCC_FIXTURE_HAS_SERIAL_EXIT_OWNER',actualOwnerSha256:sha(serialSource),sourceBodyChanged:false,scope:'Existing generic capability define; compiles real serial exit owner and excludes only old-source fallback'});
}
// Bind the real environment as a struct, separate from the function count.
const coldEnvironment=extract(source,'ColdEnvironment','struct').code;
if(extract(inert,'ColdEnvironment','struct').code!==coldEnvironment)throw Error('core modified ColdEnvironment');
const structCore=[{symbol:'ColdEnvironment',kind:'struct',source:'native/virtual_target_return_cold_runtime.h',sha256:sha(coldEnvironment),executedSha256:sha(coldEnvironment),unchanged:true}];
// These headers are deliberately not expanded or faked. The full runner must
// observe every one as an actual compiler include, then bind its frozen bytes.
const compilerHeaderBindings=['native/virtual_target_return_cold_execution.h','native/virtual_target_return_pnp_absence.h','native/virtual_target_return_record.h','native/virtual_target_return_session.h','native/virtual_target_return_store.h','native/virtual_switch_execution_return_runtime.h'].map(source=>({source,sha256:sha(fs.readFileSync(path.join(snapshot,source))),compilerObservationRequired:true}));
const continuationSource='native/virtual_switch_execution_return_runtime.h';
const continuation=fs.readFileSync(path.join(snapshot,continuationSource),'utf8');
for(const match of continuation.matchAll(/^static[^\n;{]*?\b([A-Za-z_]\w*)\s*\(/gm)){
 const name=match[1],actual=extract(continuation,name).code;
 if(core.some(row=>row.symbol===name))throw Error('duplicate core '+name);
 core.push({symbol:name,source:continuationSource,sha256:sha(actual),executedSha256:sha(actual),unchanged:true,compilerObservedHeader:true,fixtureEndpointAdapter:false,expectedSemanticNegative:false});
}
const readSource=extract(source,'gamepadReadStateUnlocked').code,discoveryStart=readSource.indexOf('    const bool boundaryFullScan ='),discoveryEnd=readSource.indexOf('    // L3（需求单）',discoveryStart);if(discoveryStart<0||discoveryEnd<=discoveryStart)throw Error('Exact XInput discovery slice anchor');const discoveryCode=readSource.slice(discoveryStart,discoveryEnd),maskStart=readSource.indexOf('    const DWORD physicalMask = xinputPhysicalConnectedMask();',discoveryEnd),maskEnd=readSource.indexOf('    // R10-R2 M3',maskStart);if(maskStart<0||maskEnd<=maskStart)throw Error('Exact XInput selected-source mask slice anchor');const maskCode=readSource.slice(maskStart,maskEnd),discoveryFile=path.join(out,'xinput_discovery.inc');
let discoveryBytes=discoveryCode+'\n'+maskCode,originalReadContext=null;
if(policy.supportProducts?.size){
 const contextStart=readSource.indexOf('    ymcc::sleeppower::SharedPowerV1 diagnosticReadSharedPower;');
 if(contextStart<0||contextStart>=discoveryStart)throw Error('R8_EXACT_ORIGINAL_NATIVE_READ_CONTEXT_REQUIRED');
 originalReadContext=readSource.slice(contextStart,discoveryStart);
 if(!originalReadContext.includes('DWORD successfulNativeReadMask=0;'))throw Error('R8_ORIGINAL_NATIVE_SUCCESS_MASK_CONTEXT_REQUIRED');
 // Existing legacy discovery users supply only probeNow/probeSrc/probeCostOn.
 // A local block retains all actual new read-context declarations verbatim;
 // it does not synthesize the complete producer's snapshot publication.
 discoveryBytes='{\n'+originalReadContext+discoveryCode+'\n'+maskCode+'\n}\n';
}
fs.writeFileSync(discoveryFile,discoveryBytes);const fragments=[{symbol:'gamepadReadStateUnlocked/complete-four-slot-discovery',source:'native/main.cpp',sha256:sha(discoveryCode),file:discoveryFile,fileSha256:sha(discoveryBytes),exact:true,scope:'Actual discovery loop, per-slot identity classification and completed-boundary publication. Only XInput/identity API leaves stay inert; no source-admission flag is forged.'},{symbol:'gamepadReadStateUnlocked/selected-source-publication-mask',source:'native/main.cpp',sha256:sha(maskCode),file:discoveryFile,fileSha256:sha(discoveryBytes),exact:true,scope:'Actual physical/external/selected-source mask decision and publication. Drop/reconfirm remainder and whole BUS are not executed or claimed.'},...(originalReadContext?[{symbol:'gamepadReadStateUnlocked/original-read-context',source:'native/main.cpp',sha256:sha(originalReadContext),file:discoveryFile,fileSha256:sha(discoveryBytes),exact:true,scope:'Original context declarations, block scope only; not a full physical-return snapshot publication.'}]:[])];
const settingsMutexIsolation=isolateCurrentFixtureSettingsMutex(repairCurrent105LifecycleFixtureInputs(observeCurrent105Fixture(fs.readFileSync(path.join(snapshot,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8'),{sourceRoot:repo,managedInputContract:managedContract.id,nativeSourceProfile}),{sourceRoot:repo,managedInputContract:managedContract.id,nativeSourceProfile}).text,{runtimeRoot,moduleMaintenance:managedContract.terminalCleanup,managedInputContract:managedContract.id,runtimeParameterLayout,runtimeReusePolicy});
if(runtimeReusePolicy!==undefined&&!settingsMutexIsolation.runtimeEntryAdmissionEnforced)throw Error('COMPLETE_CURRENT26_RUNTIME_ENTRY_ADMISSION_REQUIRED');
const policyFixture=pairTerminal26MsiPolicyFixture(settingsMutexIsolation.text,{productPolicySuccessor,fixtureInputSuccessor,productSource:source});
const progressObservation=checkProgress?observeCurrent105Progress(policyFixture.text):null;
const stageObservation=finalStages?observeFinalStages(progressObservation?.text??policyFixture.text):null;
let fixture=stageObservation?.text??progressObservation?.text??policyFixture.text;
const policyGeneratedFiles=[],policyGeneratedBindings=[];
let policyIoProvenance=null;
if(productPolicySuccessor!==undefined){
  if(policyFixture.provenance?.jointSource){
   const manifest=JSON.parse(fs.readFileSync(managedProof.manifest,'utf8'));
   if(manifest.inputBindings?.msiJointVisibility?.enabled!==true||manifest.inputBindings.msiJointVisibility.fields!==8)
    throw Error('MSI_JOINT_EXPLICIT_SHARED_STATE_MANAGED_BACKEND_REQUIRED');
  }
 const policyHeaders=['native/virtual_controller_policy_return_runtime.h','native/virtual_start_protection.h',...(policy.supportProducts?.size?['native/msi_owned_return_runtime.h','native/msi_crash_recovery_runtime.h',...policy.supportProducts.keys()]:[])];
 for(const relative of policyHeaders){
  const bytes=policy.products.get(relative)??policy.supportProducts?.get(relative);if(!Buffer.isBuffer(bytes))throw Error('MSI_POLICY_REQUIRED_PRODUCT_MISSING:'+relative);
  const name=path.basename(relative),generatedName=name;
  const generatedRelative='source/native/'+generatedName,generated=path.join(out,generatedRelative);
  if(name==='virtual_start_protection.h'||(policy.supportProducts?.size&&fs.existsSync(generated))){
   const record=records.find(row=>row.path===relative);if(!record||record.snapshot!==generated)throw Error('MSI_POLICY_PROTECTION_PREIMAGE_RECORD_REQUIRED');
   const raw=fs.readFileSync(generated);if(sha(raw)!==record.sha256)throw Error('MSI_POLICY_PROTECTION_PRIVATE_PREIMAGE_CHANGED');
   const predecessor=path.join(snapshot,'native/'+(name==='virtual_start_protection.h'?'virtual_start_protection.policy-r6.before.h':name+'.policy-r8.before.h'));
   fs.writeFileSync(predecessor,raw,{flag:'wx'});record.snapshot=predecessor;
   fs.writeFileSync(generated,bytes);
   if(name==='virtual_start_protection.h')policy.provenance.privateProtectionPredecessor={...record,bytes:raw.length};
   else (policy.provenance.privateHeaderPredecessors??=[]).push({...record,bytes:raw.length});
  }else fs.writeFileSync(generated,bytes,{flag:'wx'});
  policyGeneratedFiles.push(generatedRelative);
  policyGeneratedBindings.push({path:generated,sha256:sha(bytes),bytes:bytes.length,compilerObservationRequired:true});
 }
 const locationsRelative='native/msi_foreground_isolation_runtime.h',locations=fs.readFileSync(path.join(snapshot,locationsRelative));
  const io=pairTerminal26MsiPolicyIo(Buffer.from(inert),Buffer.from(fixture),locations,{productPolicySuccessor,
   jointCompanion:policyFixture.supportOverrides?.get('MSI-JOINT-SOURCE-COMPANION.inc')});
 inert=io.mainSource.toString('utf8');fixture=io.fixtureSource.toString('utf8');
 const generatedRelative='source/native/msi_foreground_isolation_runtime.policy-fixture.h',generated=path.join(out,generatedRelative);
 fs.writeFileSync(generated,io.sources['msi_foreground_isolation_runtime.h'],{flag:'wx'});
 inert=replaceUniqueSourceText(inert,'#include "msi_foreground_isolation_runtime.h"','#include "msi_foreground_isolation_runtime.policy-fixture.h"','msi-r6-location-read-port');
 policyGeneratedFiles.push(generatedRelative);
 policyGeneratedBindings.push({path:generated,sha256:sha(fs.readFileSync(generated)),compilerObservationRequired:true});
  const copiedSupport=new Set();
  for(const row of policyFixture.support){const original=fs.readFileSync(row.path);if(sha(original)!==row.sha256||original.length!==row.bytes)throw Error('MSI_POLICY_SUPPORT_CHANGED');const sourceName=path.basename(row.path),name=sourceName==='MSI-JOINT-SOURCE-COMPANION.inc'?'msi_joint_source_companion.inc':sourceName,bytes=io.jointSupport?.get(name)??policyFixture.supportOverrides?.get(sourceName)??original;fs.writeFileSync(path.join(out,name),bytes,{flag:'wx'});policyGeneratedFiles.push(name);copiedSupport.add(name);}
  for(const [name,bytes] of io.jointSupport??[]){if(copiedSupport.has(name))continue;fs.writeFileSync(path.join(out,name),bytes,{flag:'wx'});policyGeneratedFiles.push(name);}
  policyIoProvenance={adapter:io.adapter,profile:io.profile,insertionProof:io.insertionProof,reverseProof:io.reverseProof,visibilityBinding:io.visibilityBinding,...(io.jointProvenance?{joint:io.jointProvenance}:{}),sourceOnly:true};
}
fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);fs.writeFileSync(path.join(out,'fixture.cpp'),fixture);
fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify({records,sourceExpansion,...(nativeSourceProfile===undefined?{}:{nativeSourceProfile,nativeGuardProvenance:selectedNativeGuardProvenance}),productSourceSuccessor:productSourceSuccessorProvenance,productHeaderSuccessor:null,productPolicySuccessor:policy.provenance,policyFixture:policyFixture.provenance,policyIoProvenance,core,structCore,compilerHeaderBindings,substitutions,fragments},null,2));
// Directed header negatives compile a changed private header. Raw production
// snapshots stay intact, with their hash bound as transformation inputs.
const mutatedHeaderFiles=[],mutatedHeaderInputs=[];
let productHeaderSuccessorProvenance=null;
if(productHeaderSuccessor!==undefined){
 const relative='native/msi_native_foreground_isolation_runtime.h',original=fs.readFileSync(path.join(snapshot,relative));
 const reviewed=reviewedTerminal26MsiCleanupHeader(original,{sourceRoot:repo,managedInputContract:managedContract.id,productHeaderSuccessor});
 const generatedRelative='source/native/msi_native_foreground_isolation_runtime.reviewed.h',generated=path.join(out,generatedRelative);
 fs.writeFileSync(generated,reviewed.bytes,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "msi_native_foreground_isolation_runtime.h"','#include "msi_native_foreground_isolation_runtime.reviewed.h"','exact-reviewed-MSI-cleanup-product-header');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push(generatedRelative);mutatedHeaderInputs.push(relative);
 productHeaderSuccessorProvenance={...reviewed.productHeaderSuccessor,generated:{path:generated,sha256:sha(reviewed.bytes),bytes:reviewed.bytes.length}};
 substitutions.push({symbol:relative,beforeSha256:sha(original),afterSha256:sha(reviewed.bytes),productSourceSemanticChange:true,privateCandidate:true,negativeControl:false,
  scope:reviewed.productHeaderSuccessor.scope,inverseVerified:true});
}
if(mutation==='rollback-wake-retirement-identity'||mutation==='rollback-wake-start-budget'){
 const rel='native/virtual_wake_execution_runtime.h',original=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const from=mutation==='rollback-wake-retirement-identity'?'static void retireLocked(Work w,const char* reason) {':'if(episode.startAttempts>=kWakeHotReplaceMaxAttempts)';
 const to=mutation==='rollback-wake-retirement-identity'?'static void retireLocked(const Work& w,const char* reason) {':'if(false&&episode.startAttempts>=kWakeHotReplaceMaxAttempts)';
 const altered=replaceUniqueSourceText(original,from,to,mutation),generated=path.join(snapshot,'native/virtual_wake_execution_runtime.mutant.h');
 fs.writeFileSync(generated,altered,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "virtual_wake_execution_runtime.h"','#include "virtual_wake_execution_runtime.mutant.h"',mutation+'-include');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/virtual_wake_execution_runtime.mutant.h');mutatedHeaderInputs.push(rel);
 substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Actual changed private production header; restore only the selected proven bug. Raw source, original assertions and actual owned Host path stay unchanged.'});
}
if(mutation==='rollback-queued-player-binding'){
 const rel='native/virtual_controller_queued_start_runtime.h',original=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const altered=replaceUniqueSourceText(original,'if(!startReason(reason))return reason;','if(true||!startReason(reason))return reason;',mutation);
 const generated=path.join(snapshot,'native/virtual_controller_queued_start_runtime.mutant.h');fs.writeFileSync(generated,altered,{flag:'wx'});
 inert=replaceUniqueSourceText(inert,'#include "virtual_controller_queued_start_runtime.h"','#include "virtual_controller_queued_start_runtime.mutant.h"',mutation+'-include');
 fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/virtual_controller_queued_start_runtime.mutant.h');mutatedHeaderInputs.push(rel);
 substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Only remove admitted player provenance from actual queued initial/persona Start; same real managed pipe and original cases.'});
}
if(['rollback-ps-return-pin','rollback-ps-return-physical','rollback-ps-return-family'].includes(mutation)){
 const rel=mutation==='rollback-ps-return-pin'?'native/ps_endpoint_ownership.h':'native/ps_endpoint_return_runtime.h';const original=fs.readFileSync(path.join(snapshot,rel),'utf8');let altered=original;
 if(mutation==='rollback-ps-return-pin')altered=replaceUniqueSourceText(altered,'retirement_ = {true, expected_, state_};','retirement_ = {true, expected_, Snapshot{}};',mutation);
 if(mutation==='rollback-ps-return-physical')altered=replaceUniqueSourceText(altered,'return absent; // keep the exact debt until physical restore/hold also succeeds','return absent && g_psEndpointOwnership.confirmRetirement(debt.owner.runId,debt.owner.epoch);',mutation);
 if(mutation==='rollback-ps-return-family'){altered=replaceUniqueSourceText(altered,'absent=psRetiredFamilyAbsent();','absent=true;',mutation+'-complete-no-pin-proof');}
 const name=path.basename(rel,'.h')+'.mutant.h',generated=path.join(snapshot,'native',name);fs.writeFileSync(generated,altered,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "'+path.basename(rel)+'"','#include "'+name+'"',mutation+'-include');fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/'+name);mutatedHeaderInputs.push(rel);substitutions.push({symbol:rel,negativeControl:true,beforeSha256:sha(original),afterSha256:sha(altered),scope:'Only selected retired PS ownership/proof omission in actual compiled production header; raw sources and actual child/reader cases unchanged.'});
}
// Header-defined return readers must use the SAME admitted SetupAPI/CM ends
// as the existing main.cpp reader. Main-text substitution cannot reach includes.
{
 const rel='native/ps_endpoint_return_runtime.h',raw=fs.readFileSync(path.join(snapshot,rel),'utf8');
 const ports=currentPsReturnDevicePorts;
 const called=[...new Set([...raw.matchAll(/\b(SetupDi\w+|CM_\w+)\s*\(/g)].map(m=>m[1]))];if(called.some(name=>!Object.hasOwn(ports,name)))throw Error('UNADMITTED_PS_RETURN_DEVICE_API:'+called.join(','));
 const target=['rollback-ps-return-physical','rollback-ps-return-family'].includes(mutation)?'ps_endpoint_return_runtime.mutant.h':'ps_endpoint_return_runtime.h';
 const wrapper=Object.entries(ports).map(([api,port])=>'#define '+api+' '+port).join('\n')+'\n#include "'+target+'"\n'+Object.keys(ports).map(api=>'#undef '+api).join('\n')+'\n';
 const generated=path.join(snapshot,'native/ps_endpoint_return_runtime.fixture.h');fs.writeFileSync(generated,wrapper,{flag:'wx'});inert=replaceUniqueSourceText(inert,'#include "'+target+'"','#include "ps_endpoint_return_runtime.fixture.h"','ps-return-explicit-device-ends');fs.writeFileSync(path.join(snapshot,'native/main.inert.cpp'),inert);mutatedHeaderFiles.push('source/native/ps_endpoint_return_runtime.fixture.h');
 substitutions.push({symbol:rel,scope:'Original production return algorithm compiled verbatim; all six SetupAPI/CM reads redirected only within this include to pre-existing controlled leaf functions. Unknown device API rejects before compile.',originalApiCalls:called,ports,wrapperSha256:sha(wrapper),productionHeaderSha256:sha(raw),controlAlgorithmChanged:false});
}
// One final writer for each declared header/include; reviewed overlays cannot be overwritten later.
const fixtureGeneratedInputs=[...new Set([...fixtureToolHeaders,...["tools/fixtures/input_coordinator_lifecycle/target_return_cases.inc","tools/fixtures/input_coordinator_lifecycle/cold_archive_cases.inc","tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc","tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc","tools/fixtures/input_coordinator_lifecycle/off_lifecycle_cases.inc","tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc","tools/fixtures/input_coordinator_lifecycle/ps_return_cases.inc","tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc","tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc"]])];
for(const relative of fixtureGeneratedInputs){const original=fs.readFileSync(path.join(snapshot,relative));const reviewed=managedContract.signalEvidence&&['tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc','tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc','tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc'].includes(relative);let bytes=reviewed?currentFixtureSourceOverride(relative,original,{sourceRoot:repo,managedInputContract:managedContract.id,nativeSourceProfile}):original;
 if(policyFixture.provenance?.jointSource&&relative==='tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc'){
  const paired=pairMsiJointPsWake(bytes);bytes=paired.bytes;policyIoProvenance.joint.returnedPath=paired.provenance;
 }
 fs.writeFileSync(path.join(out,path.basename(relative)),bytes);}
const compiledGenerated=[...new Set([...fixtureToolHeaders.map(relative=>path.basename(relative)),'fixture.cpp','cold_archive_cases.inc','target_return_cases.inc','persona_lifecycle_cases.inc','pipe_cases.inc','off_lifecycle_cases.inc','prepare_ack_cases.inc','ps_return_cases.inc','tuple_recovery_cases.inc','ps_wake_cases.inc','xinput_discovery.inc','source/native/main.inert.cpp',...mutatedHeaderFiles,...policyGeneratedFiles])].map(rel=>{const p=path.join(out,rel);return{path:p,sha256:sha(fs.readFileSync(p))};});
const vc='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';const native=path.join(snapshot,'native'),json=path.join(snapshot,'deps/json'),webview=path.join(snapshot,'deps/webview2/build/native/include');
const build=calibrationToolchainPreamble(vc)+'chcp 65001 >nul\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /'+nativeOptimization+' /showIncludes /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c "'+path.join(out,'fixture.cpp')+'" /I"'+native+'" /I"'+json+'" /I"'+webview+'" /Fo"'+path.join(out,'fixture.obj')+'"\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /'+nativeOptimization+' /showIncludes /DNDEBUG /DUNICODE /D_UNICODE /c "'+path.join(native,'dsu_server.cpp')+'" /Fo"'+path.join(out,'dsu.obj')+'"\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /'+nativeOptimization+' /showIncludes /bigobj /DNDEBUG /DUNICODE /D_UNICODE /c "'+path.join(native,'steam_original_recovery_engine.cpp')+'" /I"'+json+'" /Fo"'+path.join(out,'steam-original-recovery.obj')+'"\r\nif errorlevel 1 exit /b %errorlevel%\r\ncl /nologo "'+path.join(out,'fixture.obj')+'" "'+path.join(out,'dsu.obj')+'" "'+path.join(out,'steam-original-recovery.obj')+'" /Fe"'+path.join(out,'CoordinatorFixture.exe')+'" /link /SUBSYSTEM:CONSOLE /MACHINE:x64 "'+path.join(repo,'deps/webview2/build/native/x64/WebView2LoaderStatic.lib')+'" "%WindowsSdkDir%Lib\\%WindowsSDKVersion%um\\x64\\WindowsApp.lib" user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib uuid.lib dwmapi.lib winhttp.lib advapi32.lib shcore.lib version.lib psapi.lib PowrProf.lib ws2_32.lib\r\nexit /b %errorlevel%\r\n';fs.writeFileSync(path.join(out,'build.cmd'),build);const compilerEnvironment=currentCompilePrivateEnvironment(out);fs.mkdirSync(compilerEnvironment.temporary,{recursive:true});const compilerStartedAtUtc=new Date().toISOString(),compilerStarted=performance.now();const p=spawnSync('cmd.exe',['/d','/c',path.join(out,'build.cmd')],{cwd:out,env:compilerEnvironment.environment,windowsHide:true,encoding:'utf8',timeout:300000,maxBuffer:4*1024*1024});fs.writeFileSync(path.join(out,'build.log'),(p.stdout??'')+(p.stderr??''));fs.writeFileSync(path.join(out,'build-process.json'),JSON.stringify({executable:'cmd.exe',args:['/d','/c',path.join(out,'build.cmd')],cwd:out,nativeOptimization,compilerTemporaryDirectory:compilerEnvironment.temporary,timeoutMs:300000,startedAtUtc:compilerStartedAtUtc,finishedAtUtc:new Date().toISOString(),elapsedMs:performance.now()-compilerStarted,exitCode:p.status,signal:p.signal??null,error:p.error?.message??null,errorCode:p.error?.code??null,timedOut:p.error?.code==='ETIMEDOUT'},null,2)+'\n',{flag:'wx'});if(p.error||p.status!==0)throw Error('full native fixture compile failed '+p.status+' '+path.join(out,'build.log'));
const closure=nativeCompilerInputClosure({records,buildLog:(p.stdout??'')+(p.stderr??''),snapshotRoot:snapshot,generatedFiles:compiledGenerated.map(r=>r.path),requiredSources:[...expansionInputs,...mutatedHeaderInputs,...(productPolicySuccessor===undefined?[]:['native/virtual_start_protection.h']),'native/dsu_server.cpp','native/steam_original_recovery_engine.cpp',...records.filter(r=>r.path.startsWith('tools/')).map(r=>r.path)]});
for(const r of compiledGenerated)if(sha(fs.readFileSync(r.path))!==r.sha256)throw Error('GENERATED_INPUT_CHANGED_DURING_COMPILE:'+r.path);

for(const binding of policyGeneratedBindings){if(!closure.observedIncludePaths.includes(path.resolve(binding.path).toLowerCase()))throw Error('MSI_POLICY_GENERATED_HEADER_NOT_OBSERVED:'+binding.path);if(sha(fs.readFileSync(binding.path))!==binding.sha256)throw Error('MSI_POLICY_GENERATED_HEADER_CHANGED');}
for(const binding of compilerHeaderBindings){
 const file=path.resolve(path.join(snapshot,binding.source)).toLowerCase();
 if(!closure.observedIncludePaths.includes(file))throw Error('REQUIRED_REAL_COMPILER_HEADER_NOT_OBSERVED:'+binding.source);
 if(sha(fs.readFileSync(file))!==binding.sha256)throw Error('REQUIRED_REAL_COMPILER_HEADER_CHANGED:'+binding.source);
}
const snapshotCopies=[...records];records.splice(0,records.length,...closure.records.map(row=>row.path===LIFECYCLE_LOG_PRODUCER?{...row,inputRole:LIFECYCLE_LOG_DATA_ROLE}:row));fs.writeFileSync(path.join(out,'source-manifest.json'),JSON.stringify({records,sourceExpansion,...(nativeSourceProfile===undefined?{}:{nativeSourceProfile,nativeGuardProvenance:selectedNativeGuardProvenance}),productSourceSuccessor:productSourceSuccessorProvenance,productHeaderSuccessor:productHeaderSuccessorProvenance,productPolicySuccessor:policy.provenance,policyFixture:policyFixture.provenance,policyIoProvenance,policyGeneratedBindings,core,structCore,compilerHeaderBindings,substitutions,fragments,settingsMutexIsolation,...(progressObservation?{progressObservation}:{}),...(stageObservation?{stageObservation}:{}),compiledGenerated,snapshotCopies,copiedButUncompiled:closure.copiedButUncompiled,observedIncludePaths:closure.observedIncludePaths,sourceClosureMeaning:closure.meaning+' Expanded raw runtime sources are pinned primary transformation inputs; sourceExpansion binds them to main.expanded.cpp and compiledGenerated binds the executed main.inert.cpp.'},null,2));

const changed=records.filter(row=>sha(fs.readFileSync(row.original))!==row.sha256||sha(fs.readFileSync(row.snapshot))!==row.sha256).map(row=>row.path);if(changed.length)throw Error('Compile inputs changed');if(managedProof&&(sha(fs.readFileSync(managedDll))!==managedProof.dll.sha256||sha(fs.readFileSync(managedProof.manifest))!==managedProof.manifestSha256))throw Error('Managed prerequisite changed');
const identity=file=>({path:path.resolve(file),sha256:sha(fs.readFileSync(file))});const result={status:'COMPILED_NOT_EXECUTED',...(runtimeReusePolicy===undefined?{}:{runtimeReusePolicy}),...(nativeSourceProfile===undefined?{}:{nativeSourceProfile,nativeGuardProvenance:selectedNativeGuardProvenance}),compileOnly:true,productSourceSuccessor:productSourceSuccessorProvenance,productHeaderSuccessor:productHeaderSuccessorProvenance,productPolicySuccessor:policy.provenance,...(managedContract.signalEvidence?{runtimeLayout:terminal26RuntimeLayout(runtimeRoot,{runtimeParameterLayout,runtimeReusePolicy})}:{}),nativeOptimization,sourceFullTranslationUnit:true,mutation,compilerExitCode:p.status,compilerSignal:p.signal??null,compilerError:p.error?.message??null,compilerProcess:identity(path.join(out,'build-process.json')),buildScript:identity(path.join(out,'build.cmd')),sourceManifest:identity(path.join(out,'source-manifest.json')),exe:identity(path.join(out,'CoordinatorFixture.exe')),managedProof,changed,fixtureExecuted:false,nativeExecuted:false,normalProductMainExecuted:false,realDeviceIo:false,results:[],runBranchReached:false};fs.writeFileSync(path.join(out,'COMPILE-ONLY.json'),JSON.stringify(result,null,2));return result;

}
