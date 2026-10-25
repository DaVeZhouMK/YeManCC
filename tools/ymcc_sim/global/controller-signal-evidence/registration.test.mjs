import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {CHILDREN} from './receipts.mjs';
import {REPO, sourceInputs, getSignalEvidenceAdapters, historicalInputs, toolInputs} from './adapter.mjs';
import {schema, PARALLELISM, JOB_TIMEOUT_MS, inspectAll} from './consumer-runner.mjs';
import {validateAdmission} from './owner-receipts.mjs';
import {consumerEnvironment} from './environment.mjs';
import {createOwnedWorkspace} from './owned-workspace.mjs';
assert.equal(CHILDREN.length,22,'REGISTRATION_GROUP_COUNT');
assert.equal(new Set(CHILDREN).size,CHILDREN.length,'REGISTRATION_DUPLICATE_GROUP');
assert.equal(sourceInputs.filter(x=>x!== 'tools/ymcc_sim/global/controller-signal-evidence/run.mjs').length,585,'REGISTRATION_SOURCE_CLOSURE_COUNT');
assert(sourceInputs.every(x=>fs.existsSync(path.join(REPO,x))),'REGISTRATION_SOURCE_CLOSURE_MISSING');
assert.equal(Object.keys(historicalInputs(REPO)).length,12,'REGISTRATION_HISTORICAL_PIN_COUNT');
assert.equal(schema.partialVariants.length,15,'REGISTRATION_PARTIAL_VARIANT_COUNT');
assert.equal(schema.partialCases.length,57,'REGISTRATION_CURRENT_CASE_COUNT');
assert.equal(schema.partialLegacyEmergencyCases.length,49,'REGISTRATION_OLD_EMERGENCY_CASE_COUNT');
const adapter=getSignalEvidenceAdapters(REPO)[0];
assert.equal(adapter.id,'controller.signal-evidence');
assert.equal(adapter.timeoutMs,1800000,'REGISTRATION_RUN_BUDGET');
assert.equal(PARALLELISM,4,'REGISTRATION_PARALLEL_BATCH_WIDTH');
assert.equal(JOB_TIMEOUT_MS,900000,'REGISTRATION_JOB_BUDGET');
assert.equal(adapter.lifecycleContract.features[0].persistence,'persistent');
assert.equal(adapter.lifecycleContract.features[0].evidenceFile,'diagnostic-result.json');
assert(adapter.resultFiles.includes('diagnostic-result.json'));
console.log(JSON.stringify({status:'PASS',groups:CHILDREN.length,sourceInputs:sourceInputs.length,evidenceInputs:adapter.evidenceInputs.length,currentCases:schema.partialCases.length,partialVariants:schema.partialVariants.length,parallelism:PARALLELISM,jobTimeoutMs:JOB_TIMEOUT_MS,runnerTimeoutMs:adapter.timeoutMs},null,2));

function stopReceiptFixture(){
  const base={pass:true,status:'PASS',evidenceComplete:true,sourceStable:true,sourceFreezeMainMatched:true,wholeLifecycleAcceptance:false,actualRetireResourcesBodyExecuted:false,actualStartWorkerBodyExecuted:false,arbitraryBlockedParentBoundedDestruction:'UNKNOWN',processExitUsedAsCleanup:false,sourceHashes:{},actualFunctions:{},lease:{stopOwnerDebt:2048,settledAfterLeaseRelease:true}};
  const row=(positive,stage)=>({stage,productionStartAndStopExecuted:true,starterJoined:true,workerJoined:true,ownedHandlesRemaining:0,contextRetained:false,processExitUsedAsCleanup:false,hostCleanupWhileWorkerLive:positive?0:1,fixturePass:positive,finalStopSettled:true,firstStopSettled:!positive,firstStopDebts:positive?1:0});
  return {...base,profiles:['Release','Checked'].flatMap(profile=>[
    {profile,route:'current',checks:59,failures:0,expectedFailureCaught:false,lease:{stopOwnerDebt:2048,settledAfterLeaseRelease:true},cases:[0,1,2,3].map(stage=>row(true,stage))},
    {profile,route:'negative-without-admission',checks:17,failures:4,expectedFailureCaught:true,lease:{stopOwnerDebt:2048,settledAfterLeaseRelease:true},cases:[row(false,0)]},
  ])};
}
const receipt=stopReceiptFixture();
validateAdmission(receipt,{source:()=>new Set(),current:{hashes:()=>{}}});
assert.throws(()=>validateAdmission({...receipt,profiles:receipt.profiles.map(p=>p.route==='current'?{...p,cases:p.cases.map(c=>({...c,firstStopDebts:0}))}:p)},{source:()=>new Set(),current:{hashes:()=>{}}}),/STOP_RECEIPT_START_RESERVATION_DEBT/);
assert.throws(()=>validateAdmission({...receipt,profiles:receipt.profiles.map(p=>p.route==='current'?{...p,cases:p.cases.map(c=>({...c,workerJoined:false}))}:p)},{source:()=>new Set(),current:{hashes:()=>{}}}),/STOP_RECEIPT_LOST_OWNER/);
const settled=await inspectAll([async()=>{throw Error('specified receipt rejection');},async()=>({status:'observed'})],2).then(()=>false).catch(error=>error.message.startsWith('CONSUMER_BATCH_FAILED:'));
assert.equal(settled,true,'REGISTRATION_ALL_SETTLED_REJECTION');
console.log(JSON.stringify({status:'PASS',receiptMutationsRejected:['lost-start-reservation-debt','lost-worker-owner'],allSettledFailureObserved:true},null,2));

// Same real declaration as receipts.test.mjs; poison variables must not select
// a tool, and missing/changed primary identities must fail before spawning.
const tools = toolInputs(REPO);
const poisoned = {SystemRoot:'C:/Windows', PATH:'C:/foreign', CL:'/FIforeign.h', INCLUDE:'C:/foreign',
  YMCC_GIT_PATH:'C:/foreign/git.exe', YMCC_NODE_PATH:'C:/foreign/node.exe', YMCC_CL_PATH:'C:/foreign/cl.exe',
  NODE_OPTIONS:'--require foreign', PYTHONPATH:'C:/foreign', TEMP:'C:/outside'};
const isolated=consumerEnvironment({ambient:poisoned,tools,out:path.join(REPO,'unused-probe-output'),nodeExecutable:process.execPath});
assert.equal(isolated.YMCC_GIT_PATH,tools.primary.git);
assert.equal(isolated.YMCC_NODE_PATH,tools.primary.node);
assert.equal(isolated.YMCC_CL_PATH,tools.primary.compiler);
for(const name of ['CL','INCLUDE','NODE_OPTIONS','PYTHONPATH'])assert(!Object.hasOwn(isolated,name));
assert(!isolated.PATH.toLowerCase().includes('foreign'));
assert.throws(()=>consumerEnvironment({ambient:{},tools:{...tools,primary:undefined},out:REPO,nodeExecutable:process.execPath}),/PRIMARY_TOOLS_REQUIRED/);
const badPins=tools.files.map(row=>path.resolve(row.path).toLowerCase()===path.resolve(tools.primary.git).toLowerCase()?{...row,sha256:'0'.repeat(64)}:row);
assert.throws(()=>consumerEnvironment({ambient:{},tools:{...tools,files:badPins},out:REPO,nodeExecutable:process.execPath}),/PRIMARY_TOOL_HASH_MISMATCH:git/);
console.log(JSON.stringify({status:'PASS',primaryIdentityNegatives:['missing-primary','wrong-git-sha256'],poisonedToolOverridesRejected:true}));

// The original nineteen groups remain ordered; wrapper composition is twentieth.
assert.equal(CHILDREN[18],'sensor-ensure/RESULT.json','REGISTRATION_ENSURE_RETAINED');
assert.equal(CHILDREN[19],schema.ownedCleanupComposition.child,'REGISTRATION_COMPOSITION_CHILD');
assert.equal(schema.ownedCleanupComposition.frontDoorIntegration.consumer,'tools/input_host_owned_cleanup_composition_selftest.py');
assert(schema.ownedCleanupComposition.frontDoorIntegration.arguments.includes('--require-wrapper'));
for(const relative of schema.ownedCleanupComposition.currentFrontDoorIntegration.currentSourceDependencies)
 assert(sourceInputs.includes(relative),'REGISTRATION_COMPOSITION_SOURCE:'+relative);
assert.equal(schema.ownedCleanupComposition.frontDoorIntegration.profiles.length*schema.ownedCleanupComposition.frontDoorIntegration.requiredVariants.length,10);
assert.equal(schema.ownedCleanupComposition.parentLifecycleAcceptance,'INCOMPLETE');
console.log(JSON.stringify({status:'PASS',compositionGroup:20,compositionBuilds:10,preservedTools:tools.files.length,parentLifecycleAcceptance:'INCOMPLETE'}));

assert.equal(CHILDREN[20],schema.primaryThread.child,'REGISTRATION_PRIMARY_CHILD');
assert.equal(schema.primaryThread.builds,14,'REGISTRATION_PRIMARY_MATRIX');
assert.equal(schema.primaryThread.sourceInputs.length,13,'REGISTRATION_PRIMARY_SOURCE13');
