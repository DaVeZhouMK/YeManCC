// Explicit private r6 pairing over the original frozen terminal26 capture.
// No compilation, runtime authority or product import is granted here.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {createRequire} from 'node:module';
import {readVirtualControllerSource,replaceUniqueSourceText} from './virtual_controller_runtime_source.mjs';
import {extract} from './fixtures/legion_motion_consumer/extract_main.mjs';
import {loadMsiJointSource,loadMsiJointOsSource,msiJointSourceFiles,pairMsiJointFixture,
 pairMsiJointDurable,pairMsiJointVisibilityGenerated,pairMsiJointOsGenerated,
 reverseMsiJointText,observeMsiJointReturn,pairMsiJointDinputBinding,pairMsiJointStateLifetime,pairMsiJointAsyncSignal,pairMsiJointCurrentModeInventory,pairMsiJointVerifierObservation,pairMsiJointLifecycleDispatch,pairMsiJointCancellationInputs}
 from './fixtures/input_coordinator_lifecycle/msi_joint_source.mjs';

export const TERMINAL26_MSI_POLICY_SUCCESSOR='msi-real-policy-return-r6-20261115';
export const TERMINAL26_MSI_POLICY_SUCCESSOR_R7='msi-real-policy-return-r7-20261115';
export const TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5='msi-real-policy-return-r8-r5-20261117';
export const TERMINAL26_MSI_POLICY_FIXTURE_INPUT='msi-policy-fixture-input-r2-20261115';
export {pairMsiJointPsWake} from './fixtures/input_coordinator_lifecycle/msi_joint_source.mjs';
const contract='current105-terminal26-sdk-lifetime-compact-v1';
const base='G:/YeManCC-Work/Testing/YMCC/Workspace/R105T26-20261114162102/Mainline/YeManCC-source/YeManCC';
const owner='G:/YeManCC-Work/Testing/YMCC/Evidence/VC-ROG-CommonSelfHeal-20261108-194834/msi-common-cleanup-review-20261115/msi-policy-return-private-r6';
const evidence='G:/YeManCC-Work/Testing/YMCC/Evidence/BacklogAssist-20261102/Runtime105CurrentMembership-20261114/native26-routing/lifecycle-runtime-layout';
const sha=b=>createHash('sha256').update(b).digest('hex');
const require=createRequire(import.meta.url);
const identity=(file,b)=>({path:path.resolve(file),bytes:b.length,sha256:sha(b)});
function pinned(row){
 assert(row&&path.isAbsolute(row.path),'MSI_POLICY_ABSOLUTE_PIN_REQUIRED');
 const st=fs.lstatSync(row.path);assert(st.isFile()&&!st.isSymbolicLink(),'MSI_POLICY_REGULAR_PIN_REQUIRED');
 const b=fs.readFileSync(row.path);assert.equal(b.length,row.bytes,'MSI_POLICY_PIN_SIZE_CHANGED:'+row.path);
 assert.equal(sha(b),row.sha256,'MSI_POLICY_PIN_CHANGED:'+row.path);return b;
}
function load(selection){
 if(selection===TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5){
  const previous=load(TERMINAL26_MSI_POLICY_SUCCESSOR_R7);
  const intakeRef={path:path.join(evidence,'real-store-original-consumer-20261116/r4-intake-20261117/R5-SOURCE-FORWARD-INTAKE.json'),bytes:16978,sha256:'8c0db329c2d3568fc1c6d50e579a91977c9515def6eb8594680eb2eb11b53ac7'};
  const intake=JSON.parse(pinned(intakeRef));
  assert.equal(intake.finalOwnerHandoffConsumed,true);assert.equal(intake.fixtureDeltaConsumed,true);assert.equal(intake.fixturePartialScaffold,true);
  assert.equal(intake.sourceReady,false);assert.equal(intake.newCompiles,0);assert.equal(intake.newNativeRuns,0);
  assert.equal(intake.candidate.sha256,'b30668c1c69b77b472168bd606667a2911ab2f99421b7aa2a9d201acb1f95b9d');
  const raw=JSON.parse(pinned(intake.candidate));assert.equal(raw.files.length,7);assert.equal(raw.edits.length,2);assert.equal(raw.sourceReady,false);
  const files=raw.files.map(row=>{const old=previous.candidate.files.find(x=>x.relative===row.relative);return {relative:row.relative,candidate:{path:row.path,bytes:row.bytes,sha256:row.sha256},before:old?.candidate??identity(path.join(base,row.relative),fs.readFileSync(path.join(base,row.relative)))};});
  const fixture=intake.fixtureRows.find(x=>path.basename(x.currentPrivateConsumer.path)==='fixture.cpp').currentPrivateConsumer;
  const driver=intake.fixtureRows.find(x=>path.basename(x.currentPrivateConsumer.path)==='msi_policy_return_driver.inc').currentPrivateConsumer;
  assert.equal(driver.bytes,6694);assert.equal(driver.sha256,'baa0a5d9867b68c51c11deab4544157f6117cef98177563c3a2df289ebc45ea0');
  const r4Root=path.join(path.dirname(path.dirname(intake.candidate.path)),'revision-r4');
  const r4PlanFile=path.join(r4Root,'PLAN-CONTINUATION.json'),r4Admission=JSON.parse(pinned(intake.previousIntake));
  const r4PlanRef=r4Admission.checked.find(x=>path.resolve(x.path)===path.resolve(r4PlanFile));assert(r4PlanRef,'R4_ORIGINAL_PLAN_PIN_REQUIRED');
  const r4Plan=JSON.parse(pinned(r4PlanRef));
  const supportReferences=['native/virtual_switch_verification_runtime.h','native/input_host_recovery_health.h'].map(relative=>{
   const row=r4Plan.sourceReferences.find(x=>path.resolve(x.copy)===path.resolve(path.join(r4Root,'before',relative)));assert(row,'R8_FROZEN_SUPPORT_REQUIRED');return {relative,path:row.copy,bytes:row.bytes,sha256:row.sha256};
  });
  const supportProducts=new Map(supportReferences.map(row=>[row.relative,pinned(row)]));
  const refs=[...previous.refs,intakeRef,intake.candidate,intake.finalHandoff,...intake.checkedHandoff,fixture,driver,intake.previousIntake,r4PlanRef,...supportReferences,...files.flatMap(row=>[row.before,row.candidate])];for(const ref of refs)pinned(ref);
  return {...previous,delivery:{...previous.delivery,candidate:intake.candidate,fixtureCandidate:fixture},deliveryRef:intake.finalHandoff,candidate:{...raw,files},refs,r5:{intake,fixture,driver,supportProducts,baselineDeriver:previous.delivery.productDeriver}};
 }
 if(selection===TERMINAL26_MSI_POLICY_SUCCESSOR_R7){
  const previous=load(TERMINAL26_MSI_POLICY_SUCCESSOR),root=path.join(path.dirname(owner),'msi-policy-return-private-r7');
  const deliveryRef={path:path.join(root,'FIXTURE-DELIVERY.json'),bytes:4135,sha256:'9ace80b5875f069720346043c01aedd6f0f8761ef298de3c1d89317ab7f94827'};
  const raw=JSON.parse(pinned(deliveryRef)),candidate=JSON.parse(pinned(raw.productCandidate));
  assert.equal(raw.productCandidate.sha256,'1d6732aa1dd3145bd90f6b4905009f26b83188d94a005bbb438798c2a607b808');
  assert.equal(raw.fixtureInputsOrAssertionsChangedSinceR6InputR2,false);assert.equal(candidate.sourceReady,false);assert.equal(candidate.mainlineImported,false);
  assert.equal(candidate.r7ExactChanges.length,2);
  for(const edit of candidate.r7ExactChanges){
   const old=previous.candidate.files.find(row=>row.relative===edit.relative),next=candidate.files.find(row=>row.relative===edit.relative);
   const before=pinned(old.candidate).toString('utf8'),after=pinned(next.candidate).toString('utf8');
   assert.equal(before.split(edit.beforeText).length,2);assert.equal(before.replace(edit.beforeText,()=>edit.afterText),after);
   assert.equal(after.replace(edit.afterText,()=>edit.beforeText),before,'R7_TWO_QUALIFICATION_CHANGES_EXACT_INVERSE');
  }
  const refs=[...previous.refs,deliveryRef,raw.productCandidate,raw.previousProductCandidate,raw.actualR6Failure,raw.productDeriver,raw.sourceControlScript,raw.fixtureInputSuccessor,raw.fixtureCandidate,...raw.fixtureSupport,raw.pureProjection,
   ...candidate.files.flatMap(row=>[...(row.before?[row.before]:[]),row.candidate])];
  for(const ref of refs)pinned(ref);
  return {delivery:{...previous.delivery,candidate:raw.productCandidate,productDeriver:raw.productDeriver,sourceControls:raw.sourceControlScript},deliveryRef,candidate,refs};
 }
 assert.equal(selection,TERMINAL26_MSI_POLICY_SUCCESSOR,'UNKNOWN_MSI_POLICY_SUCCESSOR');
 const deliveryRef={path:path.join(owner,'FIXTURE-DELIVERY.json'),bytes:5672,sha256:'7705220e840f2e75fa3ce8a50a5cc6308b23561ada229fe2268b5d7004112e12'};
 const delivery=JSON.parse(pinned(deliveryRef));
 const candidate=JSON.parse(pinned(delivery.candidate));
 assert.equal(delivery.candidate.sha256,'a1cf16f26ae94e7a1da0630b3d64e6ff6ebb505c50769c93762a8867c336ade8');
 assert.equal(candidate.sourceReady,false);assert.equal(candidate.mainlineImported,false);
 assert.equal(delivery.originalExpectedCases,25);assert.equal(delivery.originalExpectedChecks,249);
 const refs=[deliveryRef,delivery.candidate,delivery.productDeriver,delivery.sourceControls,delivery.fixtureBefore,delivery.fixtureCandidate,delivery.exactFixtureDiff,...delivery.fixtureSupport,
  ...candidate.files.flatMap(row=>[...(row.before?[row.before]:[]),row.candidate])];
 for(const ref of refs)pinned(ref);
 return {delivery,deliveryRef,candidate,refs};
}
export function terminal26MsiPolicySuccessorFiles(selection){
 if(selection===undefined)return [];
 const loaded=load(selection);
 return [...new Set([...loaded.refs.map(row=>row.path),...(selection===TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5?[require.resolve('./fixtures/input_coordinator_lifecycle/msi_policy_source_projection.mjs'),
  require.resolve('./fixtures/input_coordinator_lifecycle/msi_joint_source.mjs'),require.resolve('./fixtures/input_coordinator_lifecycle/msi_joint_visibility.inc'),require.resolve('./fixtures/input_coordinator_lifecycle/msi_joint_state_scope.inc'),
  ...msiJointSourceFiles(),...loadMsiJointOsSource().references.map(row=>row.path)]:[]),path.join(evidence,'msi_policy_port_source_adapter_20261115.mjs'),
  path.join(path.dirname(path.dirname(owner)),'family-common-audit-20261115/SOURCE_READY.json'),
  path.join(path.dirname(owner),'msi-policy-return-private-r4/fixture-ports/INTEGRATION.json')])];
}
export function pairTerminal26MsiPolicyProducts(originalExpanded,repaired,{sourceRoot,managedInputContract,productPolicySuccessor,productSourceSuccessor}={}){
 if(productPolicySuccessor===undefined)return {source:repaired.source,products:new Map(),provenance:null};
 assert.equal(managedInputContract,contract,'MSI_POLICY_CONTRACT_REQUIRED');
 assert.equal(path.resolve(sourceRoot),path.resolve(base),'MSI_POLICY_FIXED_BASE_REQUIRED');
 assert.equal(fs.realpathSync(sourceRoot),fs.realpathSync(base),'MSI_POLICY_REAL_BASE_REQUIRED');
 assert.equal(productSourceSuccessor,'terminal-cleanup-handoff-20261115','MSI_POLICY_QUALIFIED_COMMON_SUCCESSOR_REQUIRED');
 const loaded=load(productPolicySuccessor),{delivery}=loaded;
 const inputs=new Map(['native/virtual_controller_start_runtime.h','native/external_controller_priority_runtime.h','native/virtual_start_protection.h','native/main.cpp'].map(relative=>[relative,fs.readFileSync(path.join(sourceRoot,relative))]));
 // The exact pinned owner module is pure derivation. require(esm) keeps the
 // original synchronous generation API; it is loaded only for this selection.
 const deriver=require(delivery.productDeriver.path);
 let derived=deriver.derivePrivateMsiPolicyProducts(inputs);
 if(loaded.r5){
  const {projectMsiPolicyProducts}=require('./fixtures/input_coordinator_lifecycle/msi_policy_source_projection.mjs');
  const candidateProducts=new Map(loaded.candidate.files.map(row=>[row.relative,pinned(row.candidate)]));
  const projected=projectMsiPolicyProducts({baselineProducts:derived.products,candidateProducts});
  derived={...derived,products:projected.products,proofs:[...derived.proofs,{kind:'EXPLICIT_WHOLE_DEFINITION_R5_MAIN_PROJECTION',...projected.projection}]};
 }
 const entry=path.join(sourceRoot,'native/main.cpp');
 const raw=readVirtualControllerSource(entry);assert.equal(raw.source,originalExpanded,'MSI_POLICY_EXPANDED_BASE_CHANGED');
 const expanded=readVirtualControllerSource(entry,{readFile:file=>{
  const rel=path.relative(sourceRoot,file).replaceAll('\\','/');return derived.products.get(rel)??fs.readFileSync(file);
 }});
 let source=expanded.source;
 const commonProfile=JSON.parse(pinned(repaired.productSourceSuccessor.profile));
 for(const row of commonProfile.products){
  const before=Buffer.from(row.beforeBase64,'base64').toString('utf8'),after=Buffer.from(row.afterBase64,'base64').toString('utf8');
  source=replaceUniqueSourceText(source,before,after,'r6-retains-reviewed-common-'+row.relative);
 }
 let inverse=source;
 for(const row of [...commonProfile.products].reverse())inverse=replaceUniqueSourceText(inverse,Buffer.from(row.afterBase64,'base64').toString('utf8'),Buffer.from(row.beforeBase64,'base64').toString('utf8'),'r6-common-inverse');
 assert.equal(inverse,expanded.source,'MSI_POLICY_COMMON_INVERSE_REQUIRED');
 return {source,products:derived.products,supportProducts:loaded.r5?.supportProducts??new Map(),provenance:{id:productPolicySuccessor,delivery:loaded.deliveryRef,inputs:loaded.refs,
  productProofs:derived.proofs,sourceCaptureModified:false,expandedBeforeSha256:sha(originalExpanded),expandedAfterSha256:sha(source),
  exactCommonInverse:true,sourceApprovalGranted:false,publicRuntimeAccepted:false,physicalAccepted:false}};
}
export function admitTerminal26MsiPolicyTopology(header,selection){
 const {candidate}=load(selection),row=candidate.files.find(row=>row.relative==='native/external_controller_priority_runtime.h');
 assert(Buffer.from(header).equals(pinned(row.candidate)),'MSI_POLICY_EXACT_TOPOLOGY_CANDIDATE_REQUIRED');
 const old=pinned(row.before).toString('utf8');
 for(const symbol of ['externalPriorityUsbPortKind','externalPriorityUsbPortKindWithEvidence'])
  assert.equal(extract(header,symbol).code,extract(old,symbol).code,'MSI_POLICY_TOPOLOGY_IO_LEAF_CHANGED');
 return row.candidate.sha256;
}
function fixtureInput(selection){
 if(selection===undefined)return null;
 assert.equal(selection,TERMINAL26_MSI_POLICY_FIXTURE_INPUT,'UNKNOWN_MSI_POLICY_FIXTURE_INPUT');
 const ref={path:path.join(owner,'fixture-input-r2/FIXTURE-INPUT-SUCCESSOR.json'),bytes:4723,sha256:'cf116c3203cf7828a362d1d52acd9fdde2e4896644a46a555767bde75c4a268f'};
 const manifest=JSON.parse(pinned(ref));
 assert.equal(manifest.productCandidateUnchanged,true);assert.equal(manifest.productCandidate.sha256,'a1cf16f26ae94e7a1da0630b3d64e6ff6ebb505c50769c93762a8867c336ade8');
 const refs=[ref,manifest.productCandidate,manifest.originalFixtureDelivery,manifest.originalFixtureCandidate,manifest.fixtureCandidate,...manifest.fixtureSupport,manifest.exactDiff,manifest.pureProjection,manifest.oldIncompleteEvidence];
 for(const row of refs)pinned(row);
 return {manifest,ref,refs};
}
export function terminal26MsiPolicyFixtureInputFiles(selection){return fixtureInput(selection)?.refs.map(row=>row.path)??[];}
export {inspectMsiReturnObservation} from './fixtures/input_coordinator_lifecycle/msi_return_observation.mjs';
export function annotateTerminal26MsiReturnObservation(fixture,driver,{productPolicySuccessor}={}){
 assert.equal(typeof fixture,'string','MSI_RETURN_OBSERVATION_FIXTURE_TEXT_REQUIRED');
 const r5=productPolicySuccessor===TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5;
 assert(productPolicySuccessor===undefined||r5,'MSI_RETURN_UNKNOWN_OBSERVATION_DRIVER_SELECTION');
 assert(Buffer.isBuffer(driver)&&driver.length===(r5?6694:6349)&&sha(driver)===(r5?'baa0a5d9867b68c51c11deab4544157f6117cef98177563c3a2df289ebc45ea0':'d2a165cd0088412179c1bc22f7a353c200881dc2be264116281c82d503bb36b2'),'MSI_RETURN_OBSERVATION_EXACT_DRIVER_REQUIRED');
 const reference={path:require.resolve('./fixtures/input_coordinator_lifecycle/msi_return_observation.inc'),bytes:4800,sha256:'932a34ba12c875348cc5d1f9d278491368d7e275b0e5d8b28ffb8116aa2f0839'};
 pinned(reference);
 assert(!fixture.includes('fixtureCaptureMsiReturnObservation')&&!fixture.includes('msi_return_observation.inc'),'MSI_RETURN_OBSERVATION_ALREADY_INCLUDED');
 const originalFixture=fixture,originalDriver=driver.toString('utf8'),edits=[];
 const change=(text,before,after,role)=>{assert.equal(text.split(before).length,2,'MSI_RETURN_OBSERVATION_UNIQUE_ANCHOR:'+role);edits.push({role,beforeBase64:Buffer.from(before).toString('base64'),afterBase64:Buffer.from(after).toString('base64')});return text.replace(before,()=>after);};
 const beforeFunction=extract(fixture,'recoveryProtectionCases').code;let body=beforeFunction;
 const takeover='  if(actualMsiPolicy){if(!msiPolicyReturnIo::takeover())';
 body=change(body,takeover,'  if(actualMsiPolicy)fixtureCaptureMsiReturnObservation(cases.back(),"msiReturnBeforeTakeover");\n'+takeover,'fixture:before-real-takeover');
 const afterReturn='  check(g_virtualStartProtection.view(startProtectionClock).locked==beforeReturn.locked,';
 body=change(body,afterReturn,'  if(actualMsiPolicy){\n   fixtureCaptureMsiReturnObservation(cases.back(),"msiReturnAfterVerifiedReturn");\n   try {cases.back()["realPolicyProducer"]=msiPolicyReturnIo::driverReceipts;cases.back()["startEvents"]=fixtureEvents;cases.back()["startTrace"]=g_startEntrySeam.trace;}catch(...){}\n  }\n'+afterReturn,'fixture:after-original-return');
 const cleanup='check(g_virtualStartProtection.view(startProtectionClock).requestId==before.requestId,"recovery start inherits request ownership without renewing timer");cases.back()["cleanup"]=cleanup();';
 body=change(body,cleanup,'check(g_virtualStartProtection.view(startProtectionClock).requestId==before.requestId,"recovery start inherits request ownership without renewing timer");if(actualMsiPolicy)fixtureCaptureMsiReturnObservation(cases.back(),"msiReturnBeforeCleanup");cases.back()["cleanup"]=cleanup();if(actualMsiPolicy)fixtureCaptureMsiReturnObservation(cases.back(),"msiReturnAfterCleanup");','fixture:around-original-cleanup');
 assert.equal(fixture.split(beforeFunction).length,2);fixture=fixture.replace(beforeFunction,()=>body);
 fixture=change(fixture,'#include "msi_policy_return_driver.inc"','#include "msi_return_observation.inc"\n#include "msi_policy_return_driver.inc"','fixture:single-observation-include');
 let driverText=originalDriver;
 const receipt='{"maintenanceClock",fixtureMonotonicTick()}});';
 driverText=change(driverText,receipt,receipt+'\n    ::fixtureCaptureMsiReturnObservation(driverReceipts.back(),"msiReturn");','driver:after-returned-maintenance');
 const finish='        msiPolicyReturnIo::scopeEnabled=false;';
 driverText=change(driverText,finish,finish+'\n        if(!cases.empty())::fixtureCaptureMsiReturnObservation(cases.back(),"msiReturnAtFixtureEnd");','driver:before-original-unreturned-failure');
 let fixtureInverse=fixture,driverInverse=driverText;
 for(const edit of [...edits].reverse()){
  const before=Buffer.from(edit.beforeBase64,'base64').toString('utf8'),after=Buffer.from(edit.afterBase64,'base64').toString('utf8');
  if(edit.role.startsWith('fixture:')){assert.equal(fixtureInverse.split(after).length,2);fixtureInverse=fixtureInverse.replace(after,()=>before);}
  else{assert.equal(driverInverse.split(after).length,2);driverInverse=driverInverse.replace(after,()=>before);}
 }
 assert.equal(fixtureInverse,originalFixture,'MSI_RETURN_OBSERVATION_FIXTURE_INVERSE');assert.equal(driverInverse,originalDriver,'MSI_RETURN_OBSERVATION_DRIVER_INVERSE');
 const checks=value=>[...value.matchAll(/\bcheck\([^;]*?,\s*"([^"\r\n]*)"\s*\)/g)].map(row=>row[1]);
 assert.deepEqual(checks(body),checks(beforeFunction),'MSI_RETURN_OBSERVATION_ASSERTIONS_CHANGED');
 assert.equal(extract(driverText,'visibilitySnapshot').code,extract(originalDriver,'visibilitySnapshot').code,'MSI_RETURN_OBSERVATION_IO_LEAF_CHANGED');
 return {fixture,driver:Buffer.from(driverText),reference,provenance:{id:'original-msi-return-observation-v1',sampler:reference,
  beforeFixture:{bytes:Buffer.byteLength(originalFixture),sha256:sha(originalFixture)},afterFixture:{bytes:Buffer.byteLength(fixture),sha256:sha(fixture)},
  beforeDriver:{bytes:driver.length,sha256:sha(driver)},afterDriver:{bytes:Buffer.byteLength(driverText),sha256:sha(driverText)},edits,exactFixtureInverse:true,exactDriverInverse:true,
  originalAssertionsUnchanged:true,originalReturnAndCleanupCallsUnchanged:true,newProductAuthority:false,cppCompiled:false,businessExecuted:false,observationsAreReturnProof:false}};
}
export function pairTerminal26MsiPolicyFixture(text,{productPolicySuccessor,fixtureInputSuccessor,productSource}={}){
 if(fixtureInputSuccessor!==undefined)assert([TERMINAL26_MSI_POLICY_SUCCESSOR,TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor),'MSI_POLICY_FIXTURE_REQUIRES_PRODUCT');
 if([TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor))assert.equal(fixtureInputSuccessor,TERMINAL26_MSI_POLICY_FIXTURE_INPUT,'R7_EXACT_EXISTING_FIXTURE_INPUT_REQUIRED');
 if(productPolicySuccessor===undefined)return {text,provenance:null,support:[]};
 const loaded=load(productPolicySuccessor),{delivery,deliveryRef}=loaded;
 const input=fixtureInput(fixtureInputSuccessor);
 const before=extract(pinned(delivery.fixtureBefore).toString('utf8'),'recoveryProtectionCases').code;
 const after=extract(pinned(loaded.r5?.fixture??input?.manifest.fixtureCandidate??delivery.fixtureCandidate).toString('utf8'),'recoveryProtectionCases').code;
 const actual=extract(text,'recoveryProtectionCases').code;
 assert.equal(actual.replaceAll('\r\n','\n'),before.replaceAll('\r\n','\n'),'MSI_POLICY_ORIGINAL25_FUNCTION_REQUIRED');
 const checks=value=>[...value.matchAll(/\bcheck\([^;]*?,\s*"([^"\r\n]*)"\s*\)/g)].map(row=>row[1]);
 if(loaded.r5){
  const current=checks(after);let at=0;for(const original of checks(before)){const found=current.indexOf(original,at);assert(found>=at,'R5_ORIGINAL_ASSERTION_REMOVED_OR_REORDERED:'+original);at=found+1;}
 }else assert.deepEqual(checks(after),checks(before),'MSI_POLICY_ORIGINAL_ASSERTIONS_CHANGED');
 assert.equal(checks(before).length,19,'MSI_POLICY_ORIGINAL_ASSERTION_STRINGS_REQUIRED');
 assert(!text.includes('msi_policy_return_driver.inc'),'MSI_POLICY_DRIVER_ALREADY_INCLUDED');
 const inserted='#include "msi_policy_return_driver.inc"\n\n'+after;
 let result=replaceUniqueSourceText(text,actual,inserted,'r6-original25-real-producer-driver');
 const normalizedInserted=actual.includes('\r\n')?inserted.replaceAll('\r\n','\n').replaceAll('\n','\r\n'):inserted.replaceAll('\r\n','\n');
 assert.equal(result.replace(normalizedInserted,()=>actual),text,'MSI_POLICY_ORIGINAL25_EXACT_INVERSE_REQUIRED');
 if(input){
  assert.equal(typeof productSource,'string','MSI_POLICY_ACTUAL_PROJECTION_SOURCE_REQUIRED');
  const projection=pinned(input.manifest.pureProjection).toString('utf8');
  // The owner's saved slice includes adjacent pure capability gates as well.
  // Bind the entire slice once, and compare the actual function separately.
  assert.equal(productSource.split(projection).length,2,'MSI_POLICY_ACTUAL_DMI_PROJECTION_SLICE_REQUIRED');
  assert(extract(productSource,'ymccResolveFamily').code===extract(projection,'ymccResolveFamily').code,'MSI_POLICY_ACTUAL_DMI_PROJECTION_CHANGED');
  const oldDriver=delivery.fixtureSupport.find(row=>path.basename(row.path)==='msi_policy_return_driver.inc');
  const newDriver=input.manifest.fixtureSupport.find(row=>path.basename(row.path)==='msi_policy_return_driver.inc');
  const a=pinned(oldDriver).toString('utf8'),b=pinned(newDriver).toString('utf8');
  assert.equal(extract(a,'visibilitySnapshot').code,extract(b,'visibilitySnapshot').code,'MSI_POLICY_TYPED_IO_FUNCTION_CHANGED');
 }
 const support=[...(input?.manifest.fixtureSupport??delivery.fixtureSupport)].map(row=>loaded.r5&&path.basename(row.path)==='msi_policy_return_driver.inc'?loaded.r5.driver:row),supportOverrides=new Map();let returnObservation=null,jointSource=null;
 if([TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor)){
  const driver=support.find(row=>path.basename(row.path)==='msi_policy_return_driver.inc');
  const observed=annotateTerminal26MsiReturnObservation(result,pinned(driver),loaded.r5?{productPolicySuccessor}:{});result=observed.fixture;support.push(observed.reference);supportOverrides.set('msi_policy_return_driver.inc',observed.driver);returnObservation=observed.provenance;
  if(loaded.r5){
   const unobserved=reverseMsiJointText(result,observed.provenance.edits.filter(row=>row.role.startsWith('fixture:')));
   const joint=pairMsiJointFixture(unobserved),reobserved=observeMsiJointReturn(joint.text);
   const bound=pairMsiJointDinputBinding(joint.companion.toString('utf8'),productSource);
   result=reobserved.text;
   const companionRef=loadMsiJointSource().patch.newCompanion;
   support.push(companionRef);supportOverrides.set('MSI-JOINT-SOURCE-COMPANION.inc',Buffer.from(bound.companion));
   jointSource={...joint.provenance,returnObservation:reobserved.provenance,dinputBinding:bound.provenance,
    companionSourceName:'MSI-JOINT-SOURCE-COMPANION.inc',companionGeneratedName:'msi_joint_source_companion.inc',
    startupRefusalPreserved:true};
  }
 }
 return {text:result,support,supportOverrides,provenance:{delivery:deliveryRef,returnObservation,inputSuccessor:input?{id:fixtureInputSuccessor,manifest:input.ref,inputs:input.refs,originalDriverSignatureReference:delivery.fixtureSupport.find(row=>path.basename(row.path)==='msi_policy_return_driver.inc'),visibilitySnapshotBodyUnchanged:true}:null,beforeFunctionSha256:sha(actual),afterFunctionSha256:sha(extract(result,'recoveryProtectionCases').code),
  originalCases:25,originalChecks:249,originalAssertionStrings:checks(before),...(loaded.r5?{partialScaffold:false,jointSource,currentDriver:loaded.r5.driver,addedSourceCheckSites:checks(after).length-checks(before).length,currentRuntimeCheckCountNotMeasured:true}:{}),exactInverse:true,changedInputs:['MSI/31000/pending','MSI/31000/success-settle'],runtimeAccepted:false}};
}
export function pairTerminal26MsiPolicyIo(mainBuffer,fixtureBuffer,locationsBuffer,{productPolicySuccessor,jointCompanion}={}){
 const selected=load(productPolicySuccessor),{delivery,deliveryRef}=load(TERMINAL26_MSI_POLICY_SUCCESSOR);
 const file=path.join(evidence,'msi_policy_port_source_adapter_20261115.mjs');
 const bytes=fs.readFileSync(file);assert.equal(sha(bytes),'7054141b74b2421f8416260868a4abb3e84ca5b4190017b4921cb60a757e774e','MSI_POLICY_REVIEWED_ADAPTER_REQUIRED');
 const adapter=require(file);
 const profile={schemaVersion:1,name:'exact-generated-r6-real-producer',main:{bytes:mainBuffer.length,sha256:sha(mainBuffer)},fixture:{bytes:fixtureBuffer.length,sha256:sha(fixtureBuffer)},locations:{bytes:locationsBuffer.length,sha256:sha(locationsBuffer)}};
 const driver=delivery.fixtureSupport.find(row=>path.basename(row.path)==='msi_policy_return_driver.inc');
 const output=adapter.adaptMsiPolicyIoSource(mainBuffer,fixtureBuffer,{profile,sources:{'msi_foreground_isolation_runtime.h':locationsBuffer},r6Visibility:{delivery:pinned(deliveryRef),driver:pinned(driver),fixturePreimage:'generated-r6-driver-only'}});
 assert.equal(sha(fs.readFileSync(file)),sha(bytes),'MSI_POLICY_ADAPTER_CHANGED');
 if(selected.r5){
  const durable=pairMsiJointDurable({main:output.mainSource.toString('utf8'),fixture:output.fixtureSource.toString('utf8'),locations:output.sources['msi_foreground_isolation_runtime.h'].toString('utf8')});
  const visibility=pairMsiJointVisibilityGenerated(durable),os=pairMsiJointOsGenerated(visibility);
  assert(Buffer.isBuffer(jointCompanion),'MSI_JOINT_BOUND_COMPANION_REQUIRED');
  const lifetime=pairMsiJointStateLifetime(os.fixture,jointCompanion.toString('utf8'));
  const async=pairMsiJointAsyncSignal(os.main,lifetime.companion);
  const dispatch=pairMsiJointLifecycleDispatch(async.companion);
  const verification=pairMsiJointVerifierObservation(dispatch.companion);
  const cancellation=pairMsiJointCancellationInputs(lifetime.fixture,verification.companion);
  const inventory=pairMsiJointCurrentModeInventory(durable.ioSupport);
  return {...output,mainSource:Buffer.from(async.main),fixtureSource:Buffer.from(cancellation.fixture),
   sources:{...output.sources,'msi_foreground_isolation_runtime.h':Buffer.from(durable.locations)},
   jointSupport:new Map([['msi_policy_return_io_support.inc',inventory.bytes],['msi_joint_visibility.inc',visibility.model],...os.support,
    ['msi_joint_source_companion.inc',Buffer.from(cancellation.companion)],['msi_joint_state_scope.inc',lifetime.state]]),
   jointProvenance:{durable:durable.provenance,visibility:visibility.provenance,os:os.provenance,lifetime:lifetime.provenance,async:async.provenance,inventory:inventory.provenance,verificationObservation:verification.provenance,lifecycleDispatch:dispatch.provenance,cancellation:cancellation.provenance,pairedStartupPreconditions:true},
   adapter:identity(file,bytes),profile,selectedProductDelivery:selected.deliveryRef,unchangedTypedDriverSignatureReference:driver};
 }
 return {...output,adapter:identity(file,bytes),profile,selectedProductDelivery:selected.deliveryRef,unchangedTypedDriverSignatureReference:driver};
}
