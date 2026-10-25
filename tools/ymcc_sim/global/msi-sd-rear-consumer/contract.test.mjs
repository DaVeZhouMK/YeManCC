// Read-only contract checks against the parent's actual original receipts.
// No producer, compiler, native probe, global runner or audit is executed.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {ID,FAULTS,COMPLETION,validateOriginal,validateFaultSummary,validateBundle,validateToolSet,requireNatural} from './contracts.mjs';
import {getMsiSdRearConsumerAdapters} from './adapter.mjs';
import {inputs,REPO,sourceFiles,RUNTIME_FILES,HC_SOURCE_FILES} from './inputs.mjs';
import {readJson,snapshot,digest,freshRoot} from './evidence.mjs';
const positiveFile=process.env.MSI_SD_POSITIVE_CONTRACT_RESULT??'D:/SD27/current-enabled-positive/result.json';
const faultsFile=process.env.MSI_SD_FAULT_CONTRACT_RESULT??'D:/SD27/current-enabled-faults/result.json';
const positive=readJson(positiveFile),summary=readJson(faultsFile);
const children=FAULTS.map(f=>readJson(path.join(path.dirname(faultsFile),f.id,'execution/result.json')));
const copy=structuredClone;
test('parent originals still satisfy only the original positive and exact five semantic report shapes',()=>{
 assert.equal(validateOriginal(positive).report.checks,67093);
 assert.equal(validateFaultSummary(summary).length,5);
 children.forEach((r,i)=>assert.equal(validateOriginal(r,FAULTS[i]).exitCode,1));
});
test('public bundle requires its original positive as well as the fault summary',()=>{
 const identity={runId:'structural-test-only',evidenceRoot:'contract-only'};
 for(const key of ['positive','faultSummary']){
  const bundle={...identity,positive:{validated:true},faultSummary:{validated:true}};delete bundle[key];
  assert.throws(()=>validateBundle(bundle,identity),/ORIGINAL_POSITIVE_AND_SUMMARY_REQUIRED/);
 }
 assert.throws(()=>validateOriginal(undefined),/ORIGINAL_RESULT/);
});
test('removing any one of the five original fault receipts is refused',()=>{
 for(let i=0;i<5;i++){const bad=copy(summary);bad.faults.splice(i,1);assert.throws(()=>validateFaultSummary(bad),/FIVE_FAULTS_REQUIRED/);}
});
test('each exact error is mandatory in both original fault layers',()=>{
 FAULTS.forEach((f,i)=>{
  const bad=copy(summary);bad.faults[i].expectedError=f.error+' changed';assert.throws(()=>validateFaultSummary(bad),/EXACT_FAULT_ID_SOURCE_ERROR/);
  const child=copy(children[i]);child.configurations[0].report.error=f.error+' changed';assert.throws(()=>validateOriginal(child,f),/EXACT_SEMANTIC_ERROR/);
 });
});
test('duplicate fault ID cannot replace another allowed ID',()=>{
 const bad=copy(summary);bad.faults[4]=copy(bad.faults[0]);assert.throws(()=>validateFaultSummary(bad),/DUPLICATE_FAULT_ID/);
});
test('Experiment, additional configuration and experiment-only fault stay inadmissible',()=>{
 const bad=copy(positive);bad.configurations[0].configuration='ExperimentRelease';assert.throws(()=>validateOriginal(bad),/DEFAULT_RELEASE_ONLY/);
 const extra=copy(positive);extra.configurations.push(copy(extra.configurations[0]));assert.throws(()=>validateOriginal(extra),/ONE_CONFIGURATION_REQUIRED/);
 const compiled=copy(positive);compiled.configurations[0].report.fixtureExperimentCompiled=true;assert.throws(()=>validateOriginal(compiled),/POSITIVE_78_67093_DEFAULT_LOCK/);
 const sixth=copy(summary);sixth.faults.push({id:'native-missing-mode-fence'});assert.throws(()=>validateFaultSummary(sixth),/FIVE_FAULTS_REQUIRED/);
 const native=copy(summary);native.faults[4].id='native-missing-mode-fence';assert.throws(()=>validateFaultSummary(native),/EXACT_FAULT_ID_SOURCE_ERROR/);
});
test('compiler failure, crash/signal and nonsemantic exits never count as a semantic rejection',()=>{
 FAULTS.forEach((f,i)=>{
  for(const mutation of [{compiled:false},{exitCode:2},{signal:'SIGTERM'}]){const bad=copy(children[i]);Object.assign(bad.configurations[0],mutation);assert.throws(()=>validateOriginal(bad,f),/COMPILED_NATURAL_NATIVE_EXIT/);}
  const failed=copy(summary);failed.faults[i].configurations[0].compiled=false;assert.throws(()=>validateFaultSummary(failed),/FAULT_COMPILED_SEMANTIC_REJECTION/);
 });
});
test('positive counts, native default lock and memory IO claims remain exact',()=>{
 for(const [field,value] of [['cases',77],['checks',67092],['defaultNativeKeyboardWritesEnabled',true],['allHardwareFunctionsLinkedToMemoryFakes',false]]){
  const bad=copy(positive);bad.configurations[0].report[field]=value;assert.throws(()=>validateOriginal(bad),/POSITIVE_78_67093_DEFAULT_LOCK/);
 }
});
test('incomplete producer output, timeout and launch failure are rejected before report acceptance',()=>{
 // Terminal shapes here are rejection-only contract inputs, never execution evidence.
 const base={pid:1,exitCode:0,signal:null,timedOut:false,launchError:null,outputTruncated:false,stdoutComplete:true,stderrComplete:true};
 for(const patch of [{stdoutComplete:false},{stderrComplete:false},{outputTruncated:true},{timedOut:true},{launchError:'ENOENT'},{signal:'SIGTERM'},{exitCode:1}])assert.throws(()=>requireNatural({...base,...patch}),/PRODUCER_NOT_NATURAL_COMPLETE_ZERO/);
});
test('existing parent evidence and an old receipt identity cannot become a new public success',()=>{
 assert.throws(()=>freshRoot(path.dirname(positiveFile),REPO),/FRESH_EVIDENCE_REQUIRED/);
 assert.throws(()=>validateBundle(positive,{runId:'new-wrapper-attempt',evidenceRoot:'new-root'}),/STALE_RECEIPT/);
});
test('complete six-layer 582-file tool set is present; missing or changed tool is refused',()=>{
 const spec=inputs(),actual=snapshot(spec.tools.files.map(row=>row.path));
 assert.equal(spec.manifests.length,6);assert.equal(actual.length,582);validateToolSet(spec.tools,actual);
 assert.throws(()=>validateToolSet({...spec.tools,files:spec.tools.files.slice(1)},actual),/582_TOOLS_REQUIRED/);
 const missing=copy(actual);missing[0]={path:missing[0].path,status:'MISSING_OR_INVALID'};assert.throws(()=>validateToolSet(spec.tools,missing),/TOOL_MISSING_OR_CHANGED/);
 const changed=copy(actual);changed[0].sha256='0'.repeat(64);assert.throws(()=>validateToolSet(spec.tools,changed),/TOOL_MISSING_OR_CHANGED/);
});
test('parent adapter contract is one powershell ID with fixed completion and complete runtime/source declarations',()=>{
 const adapters=getMsiSdRearConsumerAdapters();assert.equal(adapters.length,1);const [a]=adapters;
 assert.equal(a.id,ID);assert.equal(a.kind,'powershell');assert.equal(a.timeoutMs,480000);
 assert.equal(a.artifactInputs.length,582);assert.equal(a.resultFiles.length,9);
 assert.deepEqual(a.completionContract,{schemaVersion:1,rules:[{line:COMPLETION}]});
 const sources=sourceFiles();assert.ok(RUNTIME_FILES.every(f=>sources.includes('tools/ymcc_sim/global/msi-sd-rear-consumer/'+f)));
 assert.ok(HC_SOURCE_FILES.every(f=>sources.includes('deps/handheldcompanion-runtime/source/'+f)));
 assert.ok(sources.every(f=>fs.statSync(path.join(REPO,f)).isFile()));
 const pins=readJson(path.join(REPO,'tools/fixtures/hc_msi_rear_consumers/before/BASELINE.json'));
 assert.ok(pins.files.every(r=>digest(path.join(REPO,'tools/fixtures/hc_msi_rear_consumers/before',r.file))===r.sha256));
});
