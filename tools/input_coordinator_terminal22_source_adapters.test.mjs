import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {guardCurrentSteam,observeCurrent105Fixture,repairCurrent105LifecycleFixtureInputs,currentCompileSourceDataProfile,currentFixtureSourceOverride,repairCurrent105TupleFixtureInputs} from './input_coordinator_current105_source_adapters.mjs';
import {terminal22SteamCompileSpec,terminal23CompileSourceProfile} from './input_coordinator_terminal22_source_adapters.mjs';
import {currentLifecycleLogBoundary,bindNativeEventHelloIdentity,prefixCurrentRuntimeRecoveryTransport,replaceCurrentTransportSource,prefixCurrentTargetReturnWireObserver} from './input_coordinator_current_compile_only.mjs';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {readVirtualControllerSource} from './virtual_controller_runtime_source.mjs';
import {reviewedTerminal26ProductSourceSuccessor,TERMINAL26_PRODUCT_SUCCESSOR,reviewedTerminal26MsiCleanupHeader,TERMINAL26_MSI_CLEANUP_SUCCESSOR,terminal26MsiCleanupSuccessorFiles} from './input_coordinator_terminal26_source_profile.mjs';
import {bindCurrentOrdinarySwitchFixturePorts,bindCurrentOrdinarySwitchTopology,currentCompileCoreSymbols} from './input_coordinator_current_compile_only.mjs';
import {currentLifecycleModePlan,verifyCurrentLifecycleMode} from './ymcc_sim/global/controller-coordinator-pipe/r3-current-run.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const capture=process.env.YMCC_TERMINAL22_SOURCE_ROOT||root;
const original=readVirtualControllerSource(path.join(capture,'native/main.cpp')).source;
const options={sourceRoot:capture,managedInputContract:'current105-terminal22-v1'};
test('msi return diagnostic consumer keeps both original cases distinct without granting physical return',()=>{
 const testCase=currentLifecycleModePlan().find(row=>row.role==='positive'&&row.mode==='recovery-protection');
 const diagnostic={schemaVersion:1,observationIsReturnProof:false,coherentSnapshot:false,durableDischargeProven:null,
  modeReturnOwed:true,startSeamActive:true,durableDischargeUnavailableInStartSeam:true,personaObserved:true,
  logicalConfiguredPersona:'steamdeck',operationalTargetEnabled:false,logicalOffEligibility:false,modeDebtReadStable:true,
  ownerMetadataObserved:false,storeMetadataObserved:false};
 const names=['MSI/31000/same-intent-external-return-during-pending-start','MSI/31000/same-intent-external-return-during-success-settle'];
 const cases=names.map(name=>({name,msiReturnBeforeTakeover:structuredClone(diagnostic),msiReturnAfterVerifiedReturn:structuredClone(diagnostic),msiReturnAtFixtureEnd:structuredClone(diagnostic)}));
 // Synthetic consumer input only; this never claims either native case ran.
 const raw={status:'PASS',forbiddenIo:0,fullTranslationUnit:true,normalProductMainExecuted:false,realDeviceIo:false,failures:[],checks:20,cases};
 const consume=value=>verifyCurrentLifecycleMode(testCase,value,{requireMsiReturnObservation:true});
 const result=consume(raw).msiReturnObservation;
 assert.equal(result.fullOwnershipReturnProven,false);assert.equal(result.durableDischargeProven,false);
 assert.deepEqual(result.cases.map(row=>row.observations.msiReturnAtFixtureEnd.status),['MODE_RETURN_STILL_OWED','MODE_RETURN_STILL_OWED']);
 assert(result.cases.every(row=>row.observations.msiReturnAtFixtureEnd.logicalOffEligibility===false));
 const duplicate=structuredClone(raw);duplicate.cases[1]=structuredClone(duplicate.cases[0]);
 assert.throws(()=>consume(duplicate),/TWO_DISTINCT_ORIGINAL_CASES_REQUIRED/);
 const missing=structuredClone(raw);delete missing.cases[1].msiReturnAtFixtureEnd;
 assert.throws(()=>consume(missing),/DECLARED_OBSERVATION_MISSING/);
 const forged=structuredClone(raw);forged.cases[0].msiReturnAtFixtureEnd.durableDischargeProven=true;
 assert.throws(()=>consume(forged),/CANNOT_SIGN_DURABLE/);
 const wrongOff=structuredClone(raw);wrongOff.cases[0].msiReturnAtFixtureEnd.logicalOffEligibility=true;
 assert.throws(()=>consume(wrongOff),/ACTUAL_OFF_ELIGIBILITY/);
 const legacy={...raw};delete legacy.cases;
 assert.equal(verifyCurrentLifecycleMode(testCase,legacy).originalSemanticChecksVerified,true);
 assert.equal(currentLifecycleModePlan().length,16);
});
test('msi cleanup exact private r2 header keeps all other bytes and original authority unchanged',()=>{
 const bytes=fs.readFileSync(path.join(capture,'native/msi_native_foreground_isolation_runtime.h'));
 const opts={sourceRoot:capture,managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1',productHeaderSuccessor:TERMINAL26_MSI_CLEANUP_SUCCESSOR};
 const output=reviewedTerminal26MsiCleanupHeader(bytes,opts),proof=output.productHeaderSuccessor;
 assert.equal(proof.id,TERMINAL26_MSI_CLEANUP_SUCCESSOR);assert.equal(output.bytes.length,155556);
 assert.equal(createHash('sha256').update(output.bytes).digest('hex'),'e6a9ef846f9629c664d2cd7a628be417281a13285d5f67a63cad263035055944');
 assert.equal(proof.inverseVerified,true);assert.equal(proof.mainlineImported,false);assert.equal(proof.candidateSourceReady,false);
 assert.equal(proof.productSourceSemanticChange,true);assert.equal(proof.sourceCaptureModified,false);
 assert.deepEqual(proof.inputs.map(row=>row.path),terminal26MsiCleanupSuccessorFiles(opts.productHeaderSuccessor));
 assert(fs.readFileSync(path.join(capture,'native/msi_native_foreground_isolation_runtime.h')).equals(bytes));
 assert.deepEqual(reviewedTerminal26MsiCleanupHeader(bytes),{bytes,productHeaderSuccessor:null});
});
test('msi cleanup exact private r2 header rejects drift unknown selection wrong contract and root',()=>{
 const bytes=fs.readFileSync(path.join(capture,'native/msi_native_foreground_isolation_runtime.h'));
 const opts={sourceRoot:capture,managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1',productHeaderSuccessor:TERMINAL26_MSI_CLEANUP_SUCCESSOR};
 const drift=Buffer.from(bytes);drift[100]^=1;
 assert.throws(()=>reviewedTerminal26MsiCleanupHeader(drift,opts),/ORIGINAL_CHANGED/);
 assert.throws(()=>reviewedTerminal26MsiCleanupHeader(bytes,{...opts,productHeaderSuccessor:'unknown'}),/UNKNOWN_MSI_HEADER_SUCCESSOR/);
 assert.throws(()=>reviewedTerminal26MsiCleanupHeader(bytes,{...opts,managedInputContract:'current105-terminal22-v1'}),/CONTRACT_REQUIRED/);
 assert.throws(()=>reviewedTerminal26MsiCleanupHeader(bytes,{...opts,sourceRoot:root}),/BASE_REQUIRED/);
});
test('ordinary switch topology uses the original controlled OS leaf and leaves other header code unchanged',()=>{
 const header=fs.readFileSync(path.join(capture,'native/external_controller_priority_runtime.h'),'utf8'),opts={managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1'};
 const result=bindCurrentOrdinarySwitchTopology(original,header,opts),before=extract(header,'externalPriorityUsbPortKind').code;
 const after=extract(result.source,'externalPriorityUsbPortKind').code;
 const inserted='\n if(fixtureOrdinarySwitchCliScope)return fixtureRogDeferredUsbPortKind(root);\n';
 assert.equal(after.replace(inserted,''),before);assert.equal(result.substitution.inverseVerified,true);
 const inline=header.replace(before,()=>after).replace(/^#pragma once/m,'// pragma once: private TU topology header');
 assert.equal(result.source.replace(inline,'#include "external_controller_priority_runtime.h"'),original);
 for(const symbol of currentCompileCoreSymbols)assert.equal(extract(result.source,symbol).code,extract(original,symbol).code,symbol);
});
test('ordinary switch topology rejects changed headers and duplicate includes while older contracts keep original bytes',()=>{
 const header=fs.readFileSync(path.join(capture,'native/external_controller_priority_runtime.h'),'utf8'),opts={managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1'};
 assert.throws(()=>bindCurrentOrdinarySwitchTopology(original,header+' ',opts),/HEADER_PIN_CHANGED/);
 assert.throws(()=>bindCurrentOrdinarySwitchTopology(original+'\n#include "external_controller_priority_runtime.h"',header,opts));
 for(const managedInputContract of [undefined,'current105-terminal22-v1','current105-terminal23-sd-decoder-v1'])
  assert.deepEqual(bindCurrentOrdinarySwitchTopology(original,header,{managedInputContract}),{source:original,substitution:null});
});
test('ordinary switch fixture ports reuse explicit I/O seams and preserve production core bodies',()=>{
 const opts={managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1'};
 const result=bindCurrentOrdinarySwitchFixturePorts(original,opts);
 assert.equal(result.substitutions.length,7);
 assert.equal(new Set(result.substitutions.map(row=>row.symbol)).size,7);
 for(const row of result.substitutions){
  assert.equal(createHash('sha256').update(extract(original,row.symbol).code).digest('hex'),row.beforeSha256);
  assert.equal(createHash('sha256').update(extract(result.source,row.symbol).code).digest('hex'),row.afterSha256);
  assert.equal(row.inverseVerified,true);
 }
 for(const symbol of currentCompileCoreSymbols)assert.equal(extract(result.source,symbol).code,extract(original,symbol).code,symbol);
 const hide=extract(result.source,'hidHideApplyPhysicalTransaction').code;
 assert(hide.includes('fixtureOrdinarySwitchCliScope?fixtureRogDeferredHideSeam(g_startEntrySeam.hidHideApplyResult,transactionId,powerCycle):g_startEntrySeam.hidHideApplyResult'));
 const restore=extract(result.source,'hidHideRestorePhysicalTransaction').code;
 assert(restore.includes('fixtureOrdinarySwitchCliScope && fixtureSwitchOwnedHideSeed && g_startEntrySeam.hidHideRestoreResult'));
 assert(extract(result.source,'hidHideRun').code.includes('fixtureOrdinarySwitchCliScope && fixtureOwnedCliBoundary'));
});
test('ordinary switch fixture ports preserve older contracts and refuse missing or duplicate seam anchors',()=>{
 for(const managedInputContract of [undefined,'current105-terminal22-v1','current105-terminal23-sd-decoder-v1'])
  assert.deepEqual(bindCurrentOrdinarySwitchFixturePorts(original,{managedInputContract}),{source:original,substitutions:[]});
 const opts={managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1'};
 assert.throws(()=>bindCurrentOrdinarySwitchFixturePorts(original.replace('return g_startEntrySeam.hidHideApplyResult;','return false;'),opts));
 assert.throws(()=>bindCurrentOrdinarySwitchFixturePorts(original+extract(original,'hidHideApplyPhysicalTransaction').code,opts),/NOT_UNIQUE|Expected one function definition/);
});
test('terminal26 explicit product successor binds exact two owner hunks and keeps its base immutable',()=>{
 const before=readVirtualControllerSource(path.join(capture,'native/main.cpp')).source;
 const opts={sourceRoot:capture,managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1',productSourceSuccessor:TERMINAL26_PRODUCT_SUCCESSOR};
 const changed=reviewedTerminal26ProductSourceSuccessor(before,opts),proof=changed.productSourceSuccessor;
 assert.equal(createHash('sha256').update(before).digest('hex'),proof.expandedBeforeSha256);
 assert.equal(createHash('sha256').update(changed.source).digest('hex'),proof.expandedAfterSha256);
 assert.equal(proof.hunks.length,2);assert.equal(proof.inverseVerified,true);
 assert.equal(proof.productSourceSemanticChange,true);assert.equal(proof.sourceCaptureModified,false);
 assert.equal(proof.publicRuntimeAccepted,false);assert.equal(proof.managedRebuilt,false);
 assert.equal(readVirtualControllerSource(path.join(capture,'native/main.cpp')).source,before);
 for(const productSourceSuccessor of [undefined])assert.deepEqual(reviewedTerminal26ProductSourceSuccessor(before,{productSourceSuccessor}),{source:before,productSourceSuccessor:null});
});
test('terminal26 product successor rejects unknown selection changed whole source wrong contract and root',()=>{
 const opts={sourceRoot:capture,managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1',productSourceSuccessor:TERMINAL26_PRODUCT_SUCCESSOR};
 assert.throws(()=>reviewedTerminal26ProductSourceSuccessor(original,{...opts,productSourceSuccessor:'unknown'}),/UNKNOWN_PRODUCT_SUCCESSOR/);
 assert.throws(()=>reviewedTerminal26ProductSourceSuccessor(original,{...opts,managedInputContract:'current105-terminal22-v1'}),/CONTRACT_REQUIRED/);
 assert.throws(()=>reviewedTerminal26ProductSourceSuccessor(original,{...opts,sourceRoot:root}),/BASE_ROOT_REQUIRED/);
 assert.throws(()=>reviewedTerminal26ProductSourceSuccessor(original+' ',opts),/EXPANDED_BASE_CHANGED/);
 assert.throws(()=>reviewedTerminal26ProductSourceSuccessor(original+original,opts),/EXPANDED_BASE_CHANGED/);
});
test('tuple current input repair retains all checks and corrects monitoring and exact terminal ownership criteria',()=>{
 const relative='tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc',bytes=fs.readFileSync(path.join(capture,relative));
 const opts={sourceRoot:capture,managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1'};
 const output=currentFixtureSourceOverride(relative,bytes,opts),before=bytes.toString('utf8'),after=output.toString('utf8');
 assert(output.equals(repairCurrent105TupleFixtureInputs(bytes,opts)));
 const checks=text=>[...text.matchAll(/check\([^;]+;/g)].map(row=>row[0]).sort();
 const oldCriterion='                        !g_inputHostRuntimeRecoveryPending.load() && createCount()==2 && stopCount()==1 &&';
 const currentCriterion='                        g_inputHostRuntimeRecoveryPending.load() && runtime.monitoring && createCount()==2 && stopCount()==1 &&';
 assert.equal(before.split(oldCriterion).length,2);
 let expected=before.replace(oldCriterion,currentCriterion);
 for(const [oldCheck,newCheck] of [
  ['            check(stopCount()==1 && traced("stop:runtime-transport-recovery") && createCount()==1 &&\n                runtime.stopInvoked && !runtime.spent && !runtime.recoveryRequestId && budgetView()==budgetBefore,',
   '            check(stopCount()==1 && traced(sharedTerminalCase?"stop:recovery-exhausted-physical-fallback":\n                "stop:runtime-transport-recovery") && createCount()==1 &&\n                (sharedTerminalCase?terminalStoppedForOldOwner():(runtime.stopInvoked && !runtime.spent)) &&\n                !runtime.recoveryRequestId && budgetView()==expectedBudget,'],
  ['            check(runtimeView()==waiting && stopCount()==1 && createCount()==1 && budgetView()==budgetBefore &&\n                fixtureRuntimeRecoveryCensusCalls==pnpBefore && fixtureRuntimeRecoveryWriterCalls==writerBefore,',
   '            check(runtimeView()==waiting && stopCount()==1 && createCount()==1 && budgetView()==expectedBudget &&\n                (!sharedTerminalCase || (terminalStoppedForOldOwner() && terminalFactSnapshot()==terminalWaiting)) &&\n                fixtureRuntimeRecoveryCensusCalls==pnpBefore && fixtureRuntimeRecoveryWriterCalls==writerBefore,'],
  ['                    check(!runtime.recoveryRequestId && createCount()==1 && stopCount()==1 && budgetView()==budgetBefore,',
   '                    check(!runtime.recoveryRequestId && createCount()==1 && stopCount()==1 && budgetView()==expectedBudget &&\n                        (!sharedTerminalCase || (terminalStoppedForOldOwner() && terminalFactSnapshot()==terminalWaiting)),']
 ]){assert.equal(expected.split(oldCheck).length,2);expected=expected.replace(oldCheck,newCheck);}
 assert.deepEqual(checks(after),checks(expected));
 assert(after.includes('auto expectedBudget=budgetBefore;'));
 assert(after.includes('if(sharedTerminalCase){expectedBudget["terminal"]=true;expectedBudget["fallbackState"]=3;}'));
 assert(after.includes('f.epochCounter==oldEpochCounter'));
 assert(after.includes('f.request==oldTicket && f.power==oldPower && f.demand==runtime.demandRevision'));
 assert(after.includes('terminalFactSnapshot()==terminalWaiting'));
 assert(after.includes('g_inputHostRecoveryIntent.invalidate();'));
 assert(after.includes('std::lock_guard<std::mutex> personaLock(g_inputHostPersonaMx);'));
 const start=after.indexOf('    const auto observeNextRuntimeProbe = [&]() {'),end=after.indexOf('    };',start),helper=after.slice(start,end);
 assert(start>=0&&end>start);assert(helper.includes('const auto delta=r.nextProbe-now;'));
 assert(helper.includes('if(delta>1000)throw'));
 assert(helper.includes('fixturePowerClockOffset+=delta;startProtectionClock+=delta;'));
 assert(helper.includes('readCurrent();runtimeTick();'));
 assert(!/r\.(?:nextProbe|retryAt|recoveryRequestId|spent|pending)\s*=(?!=)/.test(helper));
 assert(!/\b(?:for|while)\s*\(/.test(helper));
 assert.equal(after.split('                    observeNextRuntimeProbe();').length,2);
 assert(after.indexOf('                    observeNextRuntimeProbe();')>after.indexOf('g_shortcutRecordingActive=false;readCurrent();runtimeTick();'));
});
test('tuple current input repair rejects drift and preserves older contract bytes',()=>{
 const relative='tools/fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc',bytes=fs.readFileSync(path.join(capture,relative));
 const opts={sourceRoot:capture,managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1'};
 const drift=Buffer.from(bytes);drift[100]^=1;
 assert.throws(()=>repairCurrent105TupleFixtureInputs(drift,opts),/INPUT_PIN_REQUIRED/);
 assert.throws(()=>repairCurrent105TupleFixtureInputs(Buffer.concat([bytes,Buffer.from('x')]),opts),/INPUT_SIZE_REQUIRED/);
 for(const managedInputContract of [undefined,'current105-terminal22-v1','current105-terminal23-sd-decoder-v1'])
  assert.equal(repairCurrent105TupleFixtureInputs(bytes,{managedInputContract}),bytes);
});
test('runtime wire observer composes with fault prefix and preserves mixed source line endings',()=>{
 const raw=extract(original,'inputHostPipeIo').code,lf=String.fromCharCode(10),crlf=String.fromCharCode(13,10);
 for(const eol of [lf,crlf]){
  const before=raw.replaceAll(crlf,lf).replaceAll(lf,eol);
  const fault=prefixCurrentRuntimeRecoveryTransport(before,'inputHostPipeIo'),wire=prefixCurrentTargetReturnWireObserver(fault);
  const text='prefix'+fault+'suffix',changed=replaceCurrentTransportSource(text,fault,wire,'composed actual pipe');
  assert.equal(changed.replace(wire,fault),text);
  const observer=lf+'    if(fixtureFullPipe && write)fixtureTargetReturnWireObserve(buffer,size);'+lf;
  assert.equal(wire.replace(observer,''),fault);
 }
});
test('runtime wire observer exact replacement rejects missing duplicate and normalized mixed anchors',()=>{
 const raw=extract(original,'inputHostPipeIo').code,lf=String.fromCharCode(10),crlf=String.fromCharCode(13,10);
 const mixed=prefixCurrentRuntimeRecoveryTransport(raw.replaceAll(crlf,lf).replaceAll(lf,crlf),'inputHostPipeIo'),wire=prefixCurrentTargetReturnWireObserver(mixed);
 assert.throws(()=>replaceCurrentTransportSource(mixed,mixed.replaceAll(crlf,lf),wire,'normalized'),/EXACT_SOURCE_REQUIRED/);
 assert.throws(()=>replaceCurrentTransportSource(mixed+mixed,mixed,wire,'duplicate'),/EXACT_SOURCE_REQUIRED/);
 assert.throws(()=>replaceCurrentTransportSource(mixed,'missing',wire,'missing'),/EXACT_SOURCE_REQUIRED/);
});
test('runtime recovery transport adapter reuses exact original one-shot OS input and preserves actual function body',()=>{
 const donor=fs.readFileSync(path.join(root,'tools/input_coordinator_lifecycle_selftest.mjs'),'utf8');
 for(const name of ['inputHostWaitClientLocked','inputHostPipeIo']){
  const before=extract(original,name).code,after=prefixCurrentRuntimeRecoveryTransport(before,name);
  const marker="['"+name+"','",start=donor.indexOf(marker);assert(start>=0);
  const body=donor.slice(start+marker.length,donor.indexOf("']",start));
  const insertion=String.fromCharCode(10)+body+String.fromCharCode(10);
  assert.equal(after.split(insertion).length,2);assert.equal(after.replace(insertion,''),before);
  assert(after.includes('pipeTransport::Cancellation{'));
  assert(after.includes('if(contextRejectedOut)*contextRejectedOut=result.contextRejected;'));
 }
});
test('runtime recovery transport adapter rejects missing cancellation contract and unknown endpoints',()=>{
 const name='inputHostPipeIo',originalEndpoint=extract(original,name).code;
 for(const token of ['bool* contextRejectedOut=nullptr','if(contextRejectedOut)*contextRejectedOut=result.contextRejected;','ymcc::inputHost::pipeTransport::Cancellation{'])
  assert.throws(()=>prefixCurrentRuntimeRecoveryTransport(originalEndpoint.replaceAll(token,'missing-contract'),name),/CANCELLATION_AND_REJECT_OUTPUT_REQUIRED/);
 assert.throws(()=>prefixCurrentRuntimeRecoveryTransport(originalEndpoint,'unreviewedEndpoint'),/UNKNOWN_RUNTIME_RECOVERY_TRANSPORT_ENDPOINT/);
});
test('native event HELLO binding accepts exact LF and CRLF success tails without changing other bytes',()=>{
 const originalEndpoint=extract(original,'ymStartPipeSeamServe').code;
 const lf=String.fromCharCode(10),crlf=String.fromCharCode(13,10),normalized=originalEndpoint.replaceAll(crlf,lf);
 for(const eol of [lf,crlf]){
  const before=normalized.replaceAll(lf,eol),after=bindNativeEventHelloIdentity(before);
  const insertion='    if(cmd=="HELLO")g_inputHostHostInstanceId="fixture-native-event-"+std::to_string(g_inputHostEpoch)+"-"+g_inputHostRunId;'+eol;
  assert.equal(after.split(insertion).length,2);
  assert.equal(after.replace(insertion,''),before);
  assert(after.indexOf(insertion)>after.indexOf('pipeFailCommand'));
  assert(after.indexOf(insertion)>after.indexOf('pipeThrowCommand'));
 }
});
test('native event HELLO binding rejects missing or duplicate success tails before compilation',()=>{
 const endpoint=extract(original,'ymStartPipeSeamServe').code;
 assert.throws(()=>bindNativeEventHelloIdentity(endpoint.replace('    return true;','    return false;')),/SUCCESS_ANCHOR_REQUIRED/);
 assert.throws(()=>bindNativeEventHelloIdentity(endpoint+endpoint),/SUCCESS_ANCHOR_REQUIRED/);
});
test('terminal26 lifecycle input repair preserves every original assertion and binds actual cached isolation',()=>{
 const opts={sourceRoot:capture,managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1'};
 const observation=observeCurrent105Fixture(fs.readFileSync(path.join(capture,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8'),opts);
 const repaired=repairCurrent105LifecycleFixtureInputs(observation,opts);
 const checks=text=>text.split(String.fromCharCode(10)).flatMap(line=>[...line.matchAll(/check\([^;]+;/g)].map(row=>row[0])).sort();
 assert.deepEqual(checks(repaired.text),checks(observation.text));
 assert.equal(repaired.lifecycleFixtureInputRepair.changes.length,17);
 assert(repaired.text.includes('static bool fixtureOrdinarySwitchCliScope=false;'));
 assert(repaired.text.includes('~OrdinarySwitchCliScope(){fixtureOrdinarySwitchCliScope=previous;}'));
 assert(repaired.text.includes('fixtureOrdinarySwitchCliScope && (fixtureFullPipe || fixtureColdArchive || fixtureRuntimeRecoveryOsEnabled)'));
 assert(repaired.text.includes('fixtureOrdinarySwitchCliScope?L".native-switch-cli":L".full-pipe-cli"'));
 assert(repaired.text.includes('cases.back()["startEvents"]=fixtureEvents;cases.back()["startTrace"]=g_startEntrySeam.trace;'));
 assert.equal(repaired.lifecycleFixtureInputRepair.sourceCaptureModified,false);
 assert.equal(repaired.lifecycleFixtureInputRepair.currentMainlineFixtureImported,false);
 assert.equal(repaired.lifecycleFixtureInputRepair.assertionsRemoved,0);
 const begin=repaired.text.indexOf('// Establish the actual Host first, then acquire the existing cached isolation');
 assert(begin>=0);const scope=repaired.text.slice(begin,begin+1050);
 assert(scope.indexOf('check(inputHostStart()')<scope.indexOf('radialInputIsolationOwnership(true)'));
 assert(scope.includes('radialInputIsolationInvalidate()'));
 const guard=repaired.text.indexOf('if(hidden.empty())throw std::runtime_error("owned-readback-newer-query-targets-missing");');
 assert(guard>=0);assert(repaired.text.indexOf('hidden.erase(hidden.begin());',guard)>guard);
});
test('terminal26 lifecycle input repair rejects source or observation drift',()=>{
 const opts={sourceRoot:capture,managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1'};
 const observation=observeCurrent105Fixture(fs.readFileSync(path.join(capture,'tools/fixtures/input_coordinator_lifecycle/fixture.cpp'),'utf8'),opts);
 assert.throws(()=>repairCurrent105LifecycleFixtureInputs({...observation,text:observation.text+' // drift'},opts),/EXACT_OBSERVED_SOURCE_REQUIRED/);
 assert.throws(()=>repairCurrent105LifecycleFixtureInputs({...observation,originalSha256:'0'.repeat(64)},opts),/ORIGINAL_PIN_REQUIRED/);
});
test('terminal26 lifecycle input repair leaves older contracts untouched',()=>{
 const old={text:'unchanged historical fixture',originalSha256:'original'};
 for(const managedInputContract of [undefined,'current105-terminal22-v1','current105-terminal23-sd-decoder-v1'])
  assert.deepEqual(repairCurrent105LifecycleFixtureInputs(old,{managedInputContract}),{...old,lifecycleFixtureInputRepair:null});
});
test('explicit terminal22 keeps both original executing entries blocked',()=>{
 const guarded=guardCurrentSteam(original,options);
 assert.equal(guarded.substitutions.length,2);
 assert(guarded.substitutions.every(row=>row.rejectOnly));
 assert(guarded.inert.includes('#define steamOriginalRecoveryStart fixtureNeverCallOriginalSteamRecoveryStart'));
 assert(guarded.inert.includes('static void steamOriginalRecoveryStart(){++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-start");}'));
 assert(guarded.inert.includes('throw std::logic_error("unexpected-offline-steam-recovery-engine")'));
});
test('default historical guard still refuses the newer runtime header',()=>assert.throws(()=>guardCurrentSteam(original)));
test('an explicit legacy label cannot admit the new header',()=>assert.throws(()=>guardCurrentSteam(original,{...options,managedInputContract:'current105-exact21-v1'})));
test('unknown versions cannot borrow the newer compile guard',()=>{
 for(const managedInputContract of ['terminal22','current105-terminal22-v2',null,22])assert.throws(()=>guardCurrentSteam(original,{...options,managedInputContract}),/UNKNOWN_COMPILE_SOURCE_CONTRACT/);
});
test('terminal22 requires the captured absolute source root',()=>{
 for(const sourceRoot of [undefined,'relative-source','',null])assert.throws(()=>terminal22SteamCompileSpec(sourceRoot),/ABSOLUTE_CAPTURE_SOURCE_REQUIRED/);
});
test('missing or repeated original Start include cannot pass the wrapper',()=>{
 const include='#include "steam_original_recovery_runtime.h"';
 assert.throws(()=>guardCurrentSteam(original.replace(include,''),options));
 assert.throws(()=>guardCurrentSteam(original+'\n'+include,options));
});
test('changing the engine bridge is rejected before its replacement',()=>{
 const marker='static json steamOriginalRecoveryEngineHandle(';
 const at=original.indexOf(marker);assert(at>=0);
 const brace=original.indexOf('{',at);assert(brace>at);
 assert.throws(()=>guardCurrentSteam(original.slice(0,brace+1)+' /* unreviewed bridge */ '+original.slice(brace+1),options));
});
test('changed header bytes are refused, even when the guarded entry names still exist',()=>{
 const evidence=path.resolve(root,'../../../Testing/YMCC/Evidence/VC-Current105Terminal22-23');
 const owned=fs.mkdtempSync(path.join(evidence,'guard-header-negative-'));fs.mkdirSync(path.join(owned,'native'));
 for(const name of ['steam_original_recovery_runtime.h','steam_original_recovery_dual_route.h'])fs.copyFileSync(path.join(capture,'native',name),path.join(owned,'native',name));
 fs.appendFileSync(path.join(owned,'native/steam_original_recovery_runtime.h'),'\n// unreviewed header change\n');
 assert.throws(()=>terminal22SteamCompileSpec(owned),/REVIEWED_NATIVE_HEADER_CHANGED/);
});

test('terminal23 current compile guard: real entries fixture and original log decoder remain exact',()=>{
 const id='current105-terminal23-sd-decoder-v1',opts={sourceRoot:capture,managedInputContract:id},spec=terminal23CompileSourceProfile(capture),proof=spec.nativeGuardProvenance;
 assert.equal(spec.kind,'CURRENT105_TERMINAL23_COMPILE_SOURCE_GUARD_V1');assert.equal(spec.managedInputContract,id);
 assert.equal(proof.actualManagedInputContract,id);assert.equal(proof.nativeSpecManagedInputContract,id);assert.equal(proof.reusedNativeSpec,false);
 assert.equal(proof.compileOnly,true);assert.equal(proof.runtimeAuthorityGranted,false);assert.equal(spec.review.publicRuntimeAccepted,false);assert.equal(spec.review.actualCompilerLedgerStillRequired,true);
 for(const [name,sha] of [['r3-terminal22-native-source-profile.json','f6916a908291a9246d3638278fa6579e173330a4a1a4b1278ecff1a491ac8107'],['r3-current105-source-adapter-profile.json','8c8a5803990da252418f91be0dc5df13b89efd589b14a197f962e12ba9943674']])assert.equal(createHash('sha256').update(fs.readFileSync(new URL('./ymcc_sim/global/controller-coordinator-pipe/'+name,import.meta.url))).digest('hex'),sha);
 const guarded=guardCurrentSteam(original,opts);assert.deepEqual(guarded.nativeGuardProvenance,proof);
 assert.deepEqual(guarded.substitutions.map(row=>row.symbol),['steamOriginalRecoveryStart','steamOriginalRecoveryEngineHandle','steamOriginalRecoveryExportSnapshot']);assert(guarded.substitutions.every(row=>row.rejectOnly));
 for(const [name,suffix,alias] of [['steamOriginalRecoveryStart','start','fixtureNeverCallOriginalSteamRecoveryStart'],['steamOriginalRecoveryExportSnapshot','export','fixtureNeverCallOriginalSteamRecoveryExportSnapshot']]){
  assert(guarded.inert.includes('#define '+name+' '+alias));
  assert(guarded.inert.includes('++forbiddenIo;throw std::logic_error("unexpected-offline-steam-recovery-'+suffix+'");}'));
 }
 assert(guarded.inert.includes('throw std::logic_error("unexpected-offline-steam-recovery-engine")'));
 const fixture=fs.readFileSync(path.join(capture,spec.fixture.relative),'utf8'),observed=observeCurrent105Fixture(fixture,opts);
 assert.equal(observed.originalBusinessChecksChanged,false);assert.equal(observed.originalSha256,spec.fixture.sha256);assert(observed.text.includes('fixtureOffCheckpoint("assertion",name,good?1:0);'));
 let inverse=observed.text;for(const row of [...spec.fixtureObservationChanges].reverse()){
  const before=row.before.replaceAll('\r\n','\n'),after=row.after.replaceAll('\r\n','\n');const lf=inverse.includes(after),actual=lf?after:after.replaceAll('\n','\r\n');
  for(let i=0;i<row.count;i++){assert(inverse.includes(actual));inverse=inverse.replace(actual,()=>lf?before:before.replaceAll('\n','\r\n'));}
 }assert.equal(inverse,fixture);
 const log=currentLifecycleLogBoundary(capture,{managedInputContract:id});assert.equal(Buffer.byteLength(log),905);assert.equal(createHash('sha256').update(log).digest('hex'),spec.producerData.decodedUtf8Sha256);
 assert(log.startsWith('const auto& detail=observed;'));assert.equal(Buffer.byteLength(log.slice(28)),877);assert.equal(createHash('sha256').update(log.slice(28)).digest('hex'),'fee1e275715d037b953baa1b8471e42fd4e0ea2f3be78557e80b25b6d43ef7fb');
});
test('terminal23 current compile guard: unknown IDs and stale legacy data stay refused',()=>{
 const id='current105-terminal23-sd-decoder-v1';for(const managedInputContract of ['terminal23','current105-terminal23-v1','current105-terminal23-sd-decoder-v2',null,23,{id},new String(id)]){
  assert.throws(()=>currentCompileSourceDataProfile({sourceRoot:capture,managedInputContract}),/UNKNOWN_COMPILE_SOURCE_CONTRACT/);assert.throws(()=>guardCurrentSteam(original,{sourceRoot:capture,managedInputContract}),/UNKNOWN_COMPILE_SOURCE_CONTRACT/);
 }
 for(const sourceRoot of [undefined,'relative-source','',null])assert.throws(()=>terminal23CompileSourceProfile(sourceRoot),/TERMINAL23_ABSOLUTE_CAPTURE_SOURCE_REQUIRED/);
 const legacy=currentCompileSourceDataProfile();assert.equal(legacy.fixture.sha256,'7078261d04d0378b0b81b7132fd000244d3791fbbff614f53a119761a5062537');const fixture=fs.readFileSync(path.join(capture,legacy.fixture.relative),'utf8');
 assert.throws(()=>observeCurrent105Fixture(fixture,{sourceRoot:capture,managedInputContract:'current105-terminal22-v1'}),/Exact captured current fixture/);
 assert.throws(()=>currentLifecycleLogBoundary(capture,{managedInputContract:'current105-terminal22-v1'}),/CURRENT_LOG_DATA_VALUE_CHANGED/);
});
test('terminal23 current compile guard: drifted inputs anchors fixture and log literal reject',()=>{
 const id='current105-terminal23-sd-decoder-v1',spec=terminal23CompileSourceProfile(capture),temp=process.env.YMCC_TERMINAL22_TEST_TEMP;assert(temp&&path.isAbsolute(temp),'OWNED_TEST_TEMP_REQUIRED');
 const owned=fs.mkdtempSync(path.join(temp,'guard23-'));try{
  const inputs=[spec.runtimeHeader,...spec.extraCompileHeaders,...spec.extraCompileSources];for(const row of [...inputs,spec.fixture,spec.producerData]){const file=path.join(owned,row.relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.copyFileSync(path.join(capture,row.relative),file);}
  assert.equal(terminal23CompileSourceProfile(owned).nativeGuardProvenance.nativeSpecSha256,spec.nativeGuardProvenance.nativeSpecSha256);
  for(const row of inputs){const file=path.join(owned,row.relative);fs.appendFileSync(file,'\n// unreviewed compiler input\n');assert.throws(()=>terminal23CompileSourceProfile(owned),/TERMINAL23_REVIEWED_COMPILE_INPUT_CHANGED/);fs.copyFileSync(path.join(capture,row.relative),file);}
  const opts={sourceRoot:owned,managedInputContract:id},include='#include "steam_original_recovery_runtime.h"';assert.throws(()=>guardCurrentSteam(original.replace(include,''),opts));assert.throws(()=>guardCurrentSteam(original+'\n'+include,opts));
  const at=original.indexOf('static json steamOriginalRecoveryEngineHandle(');assert(at>=0);const brace=original.indexOf('{',at);assert(brace>at);assert.throws(()=>guardCurrentSteam(original.slice(0,brace+1)+' /* drift */ '+original.slice(brace+1),opts));
  const fixture=fs.readFileSync(path.join(owned,spec.fixture.relative),'utf8');assert.throws(()=>observeCurrent105Fixture(fixture+'\n// drift',opts),/Exact captured current fixture/);
  const file=path.join(owned,spec.producerData.relative),producer=fs.readFileSync(file,'utf8');assert(producer.includes('const auto& detail=observed;'));fs.writeFileSync(file,producer.replace('const auto& detail=observed;','const auto& detail=unexpected;'));assert.throws(()=>currentLifecycleLogBoundary(owned,{managedInputContract:id}),/CURRENT_LOG_DATA_VALUE_CHANGED/);
 }finally{assert.equal(path.dirname(path.resolve(owned)),path.resolve(temp));assert(path.basename(owned).startsWith('guard23-'));fs.rmSync(owned,{recursive:true,force:true});}assert.equal(fs.existsSync(owned),false);
});
