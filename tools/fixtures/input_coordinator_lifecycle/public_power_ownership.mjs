import {extract as extractCurrentLifecycle} from '../legion_motion_consumer/extract_main.mjs';
// Pure source generation and original output binding for shared input power.
// It does not run or grant authority to the fixture/product.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
const dataFile=new URL('./public_power_ownership.json',import.meta.url),expectedDataSha256='e118520857b7e75cce64834b170256ab0f27aee5487fa387a7f727fe3a7c7c04',hash=b=>createHash('sha256').update(b).digest('hex');
const relativeFiles=Object.freeze(['native/input_host_shared_power.h','native/input_host_remote_power.h']);
function spec(){const bytes=fs.readFileSync(dataFile);assert.equal(hash(bytes),expectedDataSha256,'POWER_OWNER_OBSERVATION_DATA_CHANGED');const s=JSON.parse(bytes);assert.equal(s.kind,'EXACT_READ_ONLY_SHARED_INPUT_POWER_OWNER_OBSERVATION_V1');assert.equal(s.sourceApproval,false);assert.equal(s.runtimeAccepted,false);return s;}
export function sharedInputOwnerObservationSources(){return [...relativeFiles];}
export function observeSharedInputPowerOwners({fixture,headers,managedInputContract}={}){
 const s=spec();assert.equal(managedInputContract,s.managedInputContract,'POWER_OWNER_EXPLICIT29_CONTRACT_REQUIRED');assert.equal(typeof fixture,'string');assert(Array.isArray(headers));assert.deepEqual(headers.map(r=>r.relative),relativeFiles,'EXACT_TWO_POWER_OWNER_PRODUCT_HEADERS_REQUIRED');
 assert(!fixture.includes('fixtureCurrentPublicPowerReturn'),'POWER_OWNER_OBSERVATION_ALREADY_APPLIED');
 const anchor='int wmain(int argc,wchar_t**argv){try{';let text=fixture;const changes=[{before:anchor,after:s.addition+'\n'+anchor},s.branch,...(s.additionalBranches??[]),s.report];
 for(const [index,row]of changes.entries()){const count=row===s.report?2:1;assert.equal(text.split(row.before).length-1,count,'POWER_OWNER_ORIGINAL_FIXTURE_ANCHOR:'+index);text=text.split(row.before).join(row.after);}
 let inverse=text;for(const row of [...changes].reverse())inverse=inverse.split(row.after).join(row.before);assert.equal(inverse,fixture,'POWER_OWNER_FIXTURE_INVERSE');
 const generated=headers.map((row,index)=>{const rule=s.headers[index];assert.equal(row.relative,rule.relative);const bytes=Buffer.from(row.bytes);assert.equal(hash(bytes),rule.beforeSha256,'POWER_OWNER_HEADER_PREIMAGE:'+row.relative);const original=bytes.toString('utf8');assert.equal(original.split(rule.before).length,2);const changed=original.replace(rule.before,rule.after);assert.equal(changed.replace(rule.after,rule.before),original);assert.equal(hash(changed),rule.afterSha256,'POWER_OWNER_HEADER_OUTPUT:'+row.relative);return{relative:row.relative,bytes:Buffer.from(changed),originalSha256:rule.beforeSha256,generatedSha256:rule.afterSha256};});
 return {fixture:text,headers:generated,receipt:{kind:s.kind,dataSha256:expectedDataSha256,fixtureBeforeSha256:hash(fixture),fixtureAfterSha256:hash(text),headers:generated.map(({bytes,...row})=>row),inverseExact:true,originalBusinessChecksChanged:false,productApiOrStateChanged:false,resourceReturnGranted:false}};
}
// Called by the original generator after the exact source copy. Original
// preimages remain ordinary read-back files, separately bound from generated inputs.
export function materializeSharedInputPowerOwners({fixture,records,snapshotRoot,managedInputContract}={}){
 assert(path.isAbsolute(snapshotRoot));assert(Array.isArray(records));const headers=relativeFiles.map(relative=>{const rows=records.filter(r=>r.path===relative);assert.equal(rows.length,1,'UNIQUE_ORIGINAL_POWER_OWNER_SOURCE_REQUIRED');const row=rows[0];assert.equal(path.resolve(row.snapshot).toLowerCase(),path.resolve(snapshotRoot,relative).toLowerCase());const bytes=fs.readFileSync(row.snapshot);assert.equal(hash(bytes),row.sha256);return{relative,bytes};});
 const observed=observeSharedInputPowerOwners({fixture,headers,managedInputContract}),bindings=[];
 for(const output of observed.headers){const row=records.find(r=>r.path===output.relative),generated=row.snapshot,archive=path.join(snapshotRoot,'.fixture-original-power-owners',output.relative),bytes=fs.readFileSync(generated);assert.equal(hash(bytes),output.originalSha256);assert(!fs.existsSync(archive),'POWER_OWNER_PREIMAGE_ARCHIVE_EXISTS');fs.mkdirSync(path.dirname(archive),{recursive:true});fs.writeFileSync(archive,bytes,{flag:'wx'});assert.equal(hash(fs.readFileSync(archive)),output.originalSha256);fs.writeFileSync(generated,output.bytes);assert.equal(hash(fs.readFileSync(generated)),output.generatedSha256);row.snapshot=archive;bindings.push({source:output.relative,original:row.original,originalSnapshot:archive,originalSha256:output.originalSha256,generated,generatedSha256:output.generatedSha256,scope:'Two friend declarations only; exact product source preserved separately'});}
 return{fixture:observed.fixture,generatedFiles:bindings.map(r=>r.generated),receipt:{...observed.receipt,bindings}};
}

export function verifyMaterializedSharedInputPowerOwners({manifest,snapshotRoot,fixtureFile,managedInputContract}={}){
 const same=(a,b)=>path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
 const receipt=manifest?.publicPowerOwnerObservation;assert(receipt,'PUBLIC_POWER_OWNER_GENERATION_RECEIPT_REQUIRED');
 assert(Array.isArray(receipt.bindings)&&receipt.bindings.length===2,'EXACT_TWO_POWER_OWNER_BINDINGS_REQUIRED');
 const headers=relativeFiles.map((relative,index)=>{
  const binding=receipt.bindings[index],rows=manifest.records.filter(row=>row.path===relative);
  assert.equal(rows.length,1,'ORIGINAL_POWER_OWNER_PREIMAGE_REQUIRED');const row=rows[0];
  const archive=path.join(snapshotRoot,'.fixture-original-power-owners',relative),generated=path.join(snapshotRoot,relative);
  assert.equal(binding.source,relative);assert(same(row.snapshot,archive)&&same(binding.originalSnapshot,archive)&&same(binding.generated,generated)&&same(binding.original,row.original),'EXACT_POWER_OWNER_PATH_BINDING_REQUIRED');
  const bytes=fs.readFileSync(row.snapshot);assert.equal(hash(bytes),row.sha256);assert.equal(hash(fs.readFileSync(row.original)),row.sha256);assert.equal(binding.originalSha256,row.sha256);
  const declared=manifest.compiledGenerated.filter(item=>same(item.path,generated));assert.equal(declared.length,1,'GENERATED_POWER_OWNER_HEADER_NOT_DECLARED');
  assert.equal(hash(fs.readFileSync(generated)),declared[0].sha256);assert.equal(binding.generatedSha256,declared[0].sha256);
  assert(manifest.observedIncludePaths.some(file=>same(file,generated)),'GENERATED_POWER_OWNER_HEADER_NOT_OBSERVED');
  return{relative,bytes};
 });
 const fixture=fs.readFileSync(fixtureFile,'utf8'),s=spec(),changes=[{before:'int wmain(int argc,wchar_t**argv){try{',after:s.addition+'\nint wmain(int argc,wchar_t**argv){try{'},s.branch,...(s.additionalBranches??[]),s.report];let original=fixture;
 for(const [index,row]of [...changes.entries()].reverse()){assert.equal(original.split(row.after).length-1,row===s.report?2:1,'EXACT_POWER_OWNER_GENERATED_FIXTURE_ANCHOR');original=original.split(row.after).join(row.before);}
 const expected=observeSharedInputPowerOwners({fixture:original,headers,managedInputContract});assert.equal(expected.fixture,fixture,'POWER_OWNER_GENERATED_FIXTURE_CHANGED');
 const {bindings,...actual}=receipt;assert.deepEqual(actual,expected.receipt,'POWER_OWNER_GENERATION_RECEIPT_CHANGED');
 for(const row of expected.headers)assert.equal(hash(fs.readFileSync(path.join(snapshotRoot,row.relative))),row.generatedSha256);
 return{status:'EXACT_ORIGINAL_PREIMAGES_AND_OBSERVED_GENERATED_POWER_OWNER_INPUTS_VERIFIED',headers:2,fixtureSha256:hash(fixture),originalPreimagesPreserved:true,runtimeAccepted:false};
}

// The original child oracle supplies its exact, identity-checked child set.
// These counters prove public power ownership only; SDK/device failures survive.
export function verifySharedInputPowerOwners(report,{nativePid,children}={}){
 const need=(value,code)=>assert(value,'PUBLIC_POWER_OWNER_'+code);
 need(Number.isSafeInteger(nativePid)&&nativePid>0,'PARENT_REQUIRED');
 need(Array.isArray(children)&&children.length>0,'ORIGINAL_CHILD_SET_REQUIRED');
 const observation=report?.publicPowerReturn;
 need(observation?.schemaVersion===1&&observation.stage==='after-original-full-cycle-before-parent-exit','TERMINAL_OBSERVATION_REQUIRED');
 need(observation.parentPid===nativePid&&observation.afterSourceOrdinal===report.cases.length&&observation.originalChecks===report.checks,'EXACT_FULL_CYCLE_BOUNDARY_REQUIRED');
 for(const field of ['finishCalled','runtimeNotCreatedByObserver','msiDebtUnchanged','nativeCacheDiagnosticsAvailable','managedChildCountersInChildReceipt','consumerWaitSnapshotKnown','providerIdentityKnown','debtScanBounded'])need(observation[field]===true,field);
 need(Number.isSafeInteger(observation.issuedScopeIdentities)&&observation.issuedScopeIdentities>0&&observation.issuedScopeIdentities<=65536,'BOUNDED_SCOPE_SCAN_REQUIRED');
 need(observation.observedRepresentativeBorrowersStillHeld===0,'NATIVE_BORROW_STILL_HELD');
 need(Array.isArray(observation.exitedConsumerDebtRows)&&observation.exitedConsumerDebtRows.length===0,'EXITED_DEBT_NOT_RETURNED');
 need(Array.isArray(observation.originalConsumers),'CONSUMER_SNAPSHOT_REQUIRED');
 for(const stage of ['beforeStop','afterStop']){
  const owners=observation[stage]?.nativeInputOwners;
  for(const field of ['inputCache','lifecycleCache','lifecycleOffer','frameOffer','threadRequestScope','threadFrameSubmission'])need(owners?.[field]===false,stage+'_'+field);
 }
 const before=observation.beforeStop,after=observation.afterStop;
 need(before.runtimePublished===true&&after.runtimePublished===true&&after.gateClosed===true,'ORIGINAL_RUNTIME_STOP_REQUIRED');
 need(before.msiModeDebt===after.msiModeDebt&&before.msiModeDebtEpisode===after.msiModeDebtEpisode,'MSI_DEBT_CHANGED');
 need(after.provider?.open===false&&after.provider.admissionOpen===false,'PROVIDER_NOT_STOPPED');
 need(after.provider.outstanding===0&&after.provider.exitedConsumerDebt===0,'PROVIDER_OWNERSHIP_REMAINS');
 need(after.provider.changeEventOwnedUntilRuntimeDestruction===true,'PROVIDER_EVENT_LIFETIME_CHANGED');
 need(after.transport?.running===false,'TRANSPORT_NOT_STOPPED');
 for(const field of ['activeRequests','pendingIo','trackedOffers'])need(after.transport[field]===0,'TRANSPORT_'+field);
 need(Number.isSafeInteger(after.transport.servedRequests)&&after.transport.servedRequests>0,'ACTUAL_REQUESTS_REQUIRED');
 const identities=new Set();let retainedSdkCleanupFailures=0;
 for(const child of children){
  const identity=JSON.stringify([child.processId,child.startupNonce]);
  need(!identities.has(identity),'DUPLICATE_CHILD');identities.add(identity);
  need(child.parentPid===nativePid&&child.nativeObservedProcessExit===true&&child.finalCleanupAttempted===true&&typeof child.finalCleanupOk==='boolean','ORIGINAL_CHILD_EXIT_REQUIRED');
  const owner=child.powerOwnershipAfterFinalCleanup;
  need(owner?.bound===true&&owner.terminal===true,'CHILD_TERMINAL_REQUIRED');
  for(const field of ['currentSlots','retiredSlots','currentBorrowers','retiredBorrowers','pendingLocalReturns','localReturnFailures','admissionFailures','cancelFailures','settlementExceptions','settlementRejected','settlementUnknown'])need(owner[field]===0,'CHILD_'+field);
  for(const field of ['importInFlight','commandBorrower','closeRefreshBorrower','originalHasBorrowers','originalLocalReturnPending'])need(owner[field]===false,'CHILD_'+field);
  if(!child.finalCleanupOk)++retainedSdkCleanupFailures;
 }
 return{status:'PUBLIC_POWER_OWNERS_RETURNED_BEFORE_ORIGINAL_PARENT_AND_CHILD_EXIT',parentPid:nativePid,ownedChildren:children.length,providerOutstanding:0,exitedConsumerDebt:0,nativeCacheAndOffersEmpty:true,managedLocalOwnershipEmpty:true,retainedSdkCleanupFailures,retainedMsiDebt:after.msiModeDebt,retainedMsiDebtEpisode:after.msiModeDebtEpisode,sdkOrDeviceReturnGranted:false,originalSingle60Accepted:false,sourceApprovalGranted:false,wholeGoalComplete:false};
}

// Original cold mode has its own finite polarity and a dormant negative runtime.
// This oracle consumes exact original raw; it grants no compile or adapter authority.
export function originalColdPowerContract(){return structuredClone(spec().originalCold);}
export function mutateOriginalColdObservedId(original){
 const m=spec().originalCold.mutation;
 assert.equal(typeof original,'string');assert.equal(original.split(m.before).length,2,'EXACT_ORIGINAL_COLD_OBSERVED_ID');
 const altered=original.replace(m.before,m.after);assert.equal(altered.replace(m.after,m.before),original);
 return {text:altered,mutation:m.id,source:m.relative,beforeSha256:hash(original),afterSha256:hash(altered)};
}
export function verifyOriginalColdPowerOwners(raw,{nativePid,children,negative=false,recordBytes,archivedBytes}={}){
 const c=originalColdPowerContract(),p=raw?.publicPowerReturn;
 assert.equal(typeof negative,'boolean');assert(Number.isSafeInteger(nativePid)&&nativePid>0);
 assert.equal(raw.fullTranslationUnit,true);assert.equal(raw.normalProductMainExecuted,false);assert.equal(raw.realDeviceIo,false);assert.equal(raw.forbiddenIo,0);
 assert.deepEqual(raw.cases.map(r=>r.name),c.caseNames);assert.equal(raw.checks,negative?c.negative.checks:c.positive.checks);
 assert.equal(raw.assertionTrace.length,raw.checks);assert.equal(raw.status,negative?'FAIL':'PASS');
 const failed=rows=>rows.map(r=>JSON.stringify([r.case,r.assertion])).sort();
 assert.deepEqual(failed(raw.failures),negative?failed(c.negative.failures):[]);
 assert.deepEqual(failed(raw.assertionTrace.filter(r=>!r.passed)),failed(raw.failures));
 assert.equal(p.schemaVersion,1);assert.equal(p.fixtureMode,c.mode);assert.equal(p.stage,'after-original-full-cycle-before-parent-exit');
 assert.equal(p.parentPid,nativePid);assert.equal(p.afterSourceOrdinal,14);assert.equal(p.originalChecks,raw.checks);
 for(const f of ['finishCalled','runtimeNotCreatedByObserver','msiDebtUnchanged','nativeCacheDiagnosticsAvailable','managedChildCountersInChildReceipt','consumerWaitSnapshotKnown','providerIdentityKnown','debtScanBounded','coldRecordBytesUnchanged'])assert.equal(p[f],true,f);
 assert(Number.isSafeInteger(p.issuedScopeIdentities)&&p.issuedScopeIdentities>=0&&p.issuedScopeIdentities<=65536);
 assert.equal(p.observedRepresentativeBorrowersStillHeld,0);assert.deepEqual(p.exitedConsumerDebtRows,[]);assert(Array.isArray(p.originalConsumers));
 for(const stage of ['beforeStop','afterStop'])for(const f of ['inputCache','lifecycleCache','lifecycleOffer','frameOffer','threadRequestScope','threadFrameSubmission'])assert.equal(p[stage].nativeInputOwners[f],false,stage+':'+f);
 assert.equal(p.beforeStop.runtimePublished,p.afterStop.runtimePublished);assert.equal(p.afterStop.gateClosed,true);
 assert.equal(p.beforeStop.msiModeDebt,p.afterStop.msiModeDebt);assert.equal(p.beforeStop.msiModeDebtEpisode,p.afterStop.msiModeDebtEpisode);
 assert(Array.isArray(children));assert.equal(children.length,negative?0:c.positive.children);
 if(!negative){
  verifySharedInputPowerOwners(raw,{nativePid,children});
  const close=raw.assertionTrace.filter(r=>r.assertion==='owned managed Host observation handle checked closed');assert.equal(close.length,c.positive.observerCloseChecks);assert(close.every(r=>r.passed));
 }else{
  assert.equal(p.beforeStop.runtimePublished,false);assert.equal(p.issuedScopeIdentities,0);assert.deepEqual(p.originalConsumers,[]);
  for(const f of ['provider','transport'])assert.equal(Object.hasOwn(p.afterStop,f),false);
  assert.deepEqual(raw.targetReturnObservations,[]);
  const boundaries=raw.assertionTrace.filter(r=>r.assertion==='new independent lifecycle case starts with no residual owned child');assert.equal(boundaries.length,14);assert(boundaries.every(r=>r.passed));
  assert(raw.assertionTrace.some(r=>r.assertion==='MSI exhausted execution retries preserve both duties without writes or spawn'&&r.passed));
 }
 // The original loader deliberately refuses the default identity after cold mode.
 // Read-only raw record evidence supplies the historical obligation count.
 assert.deepEqual(p.coldRecordBefore,p.coldRecordAfter);
 assert.deepEqual(p.coldRecordAfter,{loadState:3,reason:'record-machine-binding-mismatch',win32Error:0});
 assert.equal(typeof recordBytes,'string');assert.equal(typeof archivedBytes,'string');
 const record=JSON.parse(recordBytes),archive=JSON.parse(archivedBytes);
 assert.equal(record.schemaVersion,1);assert.equal(record.machineBinding,archive.machineBinding);assert.equal(archive.obligations.length,2);
 assert.equal(record.obligations.length,negative?2:0);
 if(negative)assert.equal(recordBytes,archivedBytes);else {assert(record.revision>archive.revision);assert(raw.assertionTrace.some(r=>r.sourceOrdinal===14&&r.assertion==='MSI both historical origins settle durably before a new target'&&r.passed));}
 return{status:'ORIGINAL_COLD_POWER_TERMINAL_VERIFIED',negative,cases:14,checks:raw.checks,children:children.length,retainedHistoricalObligations:record.obligations.length,recordBytesSha256:hash(recordBytes),observerSummaryAcceptedAsHistory:false,originalModeBudgetMs:c.originalModeBudgetMs,fullAdapterAccepted:false,sourceApprovalGranted:false,originalSingle60Accepted:false};
}

export function verifyOriginalCurrentSwitchLifecycle(raw,{sourceManifest}={}){
 const s=spec().originalCurrentSwitch;
 assert(sourceManifest&&Array.isArray(sourceManifest.records)&&Array.isArray(sourceManifest.compiledGenerated),'CURRENT29_SWITCH_SOURCE_MANIFEST_REQUIRED');
 for(const [relative,expected]of [['tools/fixtures/input_coordinator_lifecycle/fixture.cpp',s.sourceFixtureSha256],['native/main.cpp',s.sourceMainSha256]]){
  const rows=sourceManifest.records.filter(r=>r.path===relative);assert.equal(rows.length,1);const r=rows[0];assert.equal(r.sha256,expected);assert.equal(hash(fs.readFileSync(r.original)),expected);assert.equal(hash(fs.readFileSync(r.snapshot)),expected);
 }
 const fixtures=sourceManifest.compiledGenerated.filter(r=>path.basename(r.path)==='fixture.cpp');assert.equal(fixtures.length,1);const fixture=fs.readFileSync(fixtures[0].path);assert.equal(hash(fixture),fixtures[0].sha256);assert.equal(hash(extractCurrentLifecycle(fixture.toString('utf8'),'switchRecoveryCases').code),s.compiledFunctionSha256);
 assert.equal(raw.status,'PASS');assert.equal(raw.forbiddenIo,0);assert.equal(raw.fullTranslationUnit,true);assert.equal(raw.normalProductMainExecuted,false);assert.equal(raw.realDeviceIo,false);assert.deepEqual(raw.failures,[]);
 assert.deepEqual(raw.cases.map(r=>r.name),s.caseNames);assert.equal(raw.checks,s.checks);assert.equal(raw.assertionTrace.length,s.checks);assert(raw.assertionTrace.every(r=>r.passed===true));
 const sequence=raw.assertionTrace.map(({case:c,assertion,sourceOrdinal,nameOccurrence,checkIndex})=>({case:c,assertion,sourceOrdinal,nameOccurrence,checkIndex}));assert.equal(hash(JSON.stringify(sequence)),s.traceSha256);
 const ids=new Set();for(const child of raw.nativePrivatePeers??[]){assert.equal(child.wait,0);assert.equal(child.exitCode,0);assert(child.controlEventClosed&&child.processObserverClosed&&child.exactCreatedIdentity);const id=JSON.stringify([child.pid,child.birth100ns,child.controlName]);assert(!ids.has(id),'DUPLICATE_NATIVE_PEER');ids.add(id);}
 return{status:'CURRENT29_ORIGINAL_SWITCH98_SOURCE_AND1727_TRACE_VERIFIED',cases:s.caseNames.length,checks:s.checks,nativePrivatePeers:(raw.nativePrivatePeers??[]).length,sourceManifestVerified:true,matchedOriginalRequiredNegativeTrace:true,original43LegacyAccepted:false,wholeLifecycle16Accepted:false,sourceApprovalGranted:false};
}

export function verifyOriginalCurrentRecoveryLifecycle(raw,{sourceManifest}={}){
 const s=spec().originalCurrentRecovery;assert(sourceManifest&&Array.isArray(sourceManifest.records)&&Array.isArray(sourceManifest.compiledGenerated),'CURRENT29_RECOVERY_SOURCE_REQUIRED');
 for(const [relative,expected]of [['tools/fixtures/input_coordinator_lifecycle/fixture.cpp',s.sourceFixtureSha256],['native/main.cpp',s.sourceMainSha256]]){const rows=sourceManifest.records.filter(r=>r.path===relative);assert.equal(rows.length,1);assert.equal(rows[0].sha256,expected);for(const p of [rows[0].original,rows[0].snapshot])assert.equal(hash(fs.readFileSync(p)),expected);}
 const fixtures=sourceManifest.compiledGenerated.filter(r=>path.basename(r.path)==='fixture.cpp');assert.equal(fixtures.length,1);const fixture=fs.readFileSync(fixtures[0].path);assert.equal(hash(fixture),fixtures[0].sha256);assert.equal(hash(extractCurrentLifecycle(fixture.toString('utf8'),'recoveryProtectionCases').code),s.compiledFunctionSha256);
 assert.equal(raw.status,'PASS');assert.equal(raw.forbiddenIo,0);assert.equal(raw.fullTranslationUnit,true);assert.equal(raw.normalProductMainExecuted,false);assert.equal(raw.realDeviceIo,false);assert.deepEqual(raw.failures,[]);assert.deepEqual(raw.cases.map(r=>r.name),s.caseNames);assert.equal(raw.checks,s.checks);assert.equal(raw.assertionTrace.length,s.checks);
 for(const [i,r]of raw.assertionTrace.entries()){assert.equal(r.passed,true);assert.equal(r.checkIndex,i+1);assert.equal(r.case,s.caseNames[r.sourceOrdinal-1]);}
 const starts=raw.assertionTrace.filter(r=>s.startAssertions.includes(r.assertion));assert.equal(starts.length,s.positiveStartRows);for(const name of s.caseNames.slice(0,24))assert.deepEqual(starts.filter(r=>r.case===name).map(r=>r.assertion),s.startAssertions);assert(!starts.some(r=>r.case===s.caseNames[24]));
 const base=raw.assertionTrace.filter(r=>!s.startAssertions.includes(r.assertion)).map(({case:c,assertion,sourceOrdinal,nameOccurrence})=>({case:c,assertion,sourceOrdinal,nameOccurrence}));assert.equal(base.length,s.baseChecks);assert.equal(hash(JSON.stringify(base)),s.baseTraceSha256);
 return{status:'CURRENT29_ORIGINAL_RECOVERY25_SOURCE_AND249_ASSERTIONS_VERIFIED',cases:25,checks:249,baseOriginalAssertions:201,positiveOriginalStartAssertions:48,negativeOriginalStartAssertions:12,extraPositiveOriginalAssertions:36,matchedRequiredNegativeBase:true,original16ParentAccepted:false,sourceApprovalGranted:false};
}
