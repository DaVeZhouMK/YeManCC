import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {test} from 'node:test';
import {REPO,sourceInputs,signalArtifactRows,toolInputs} from './adapter.mjs';
import {CHILDREN} from './receipts.mjs';
import {runManagedTerminalGroup,runConsumers} from './consumer-runner.mjs';
import {managedContract,managedArtifactRows,validateManagedTerminal,validateManagedProfiles} from './managed-terminal-receipts.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
const c=managedContract(REPO);
const original=['source-binding.json','descriptor-decode/source-binding.json','semantic-witness/RESULT.json','timer-recorder/RESULT.json','resume-generation/RESULT.json','timer-adapter/RESULT.json','worker-retirement/RESULT.json','stop-wait/RESULT.json','startup-wait/result.json','startup-lifecycle/RESULT.json','exit-fence/RESULT.json','partial-init/RESULT.json','stop-receipt/RESULT.json','exit-continuation/RESULT.json','dsu-owner/RESULT.json','sensor-owners/RESULT.json','x2-owner/RESULT.json','gamebar-owner/RESULT.json','sensor-ensure/RESULT.json','owned-cleanup-composition/RESULT.json','primary-thread/RESULT.json'];
test('managed22 preserves every original group and requires the registered actual terminal executor',()=>{assert.deepEqual(CHILDREN.slice(0,21),original);assert.equal(CHILDREN.length,22);assert.equal(CHILDREN[21],c.child);assert.equal(c.registeredSignalGroup,true);assert.equal(c.registrationState,'REGISTERED_SIGNAL_CHILD_JOB_VALIDATOR_V2');assert.equal(typeof runManagedTerminalGroup,'function');assert.match(runConsumers.toString(),/await runManagedTerminalGroup\(\{repo,out,sourceHashes,tools,workspace,run\}\)/);});
test('managed22 declares every actual producer input and full runtime tool/artifact closure',()=>{assert(c.sourceInputs.every(n=>sourceInputs.includes(n)));const rows=signalArtifactRows(REPO),keys=new Set(rows.map(r=>path.win32.resolve(r.path).toLowerCase()));for(const row of managedArtifactRows(REPO))assert(keys.has(path.win32.resolve(row.path).toLowerCase()),row.path);const legacy=read(new URL('./consumer-schema.json',import.meta.url)).managedTerminalCurrent;for(const [key,row]of [['basisCurrent',c.basisCurrent],['basisExtractions',c.basisExtractions],['identityManifest',c.identityManifest],['legacy-delivery',legacy.delivery],['oldZeroFalse',legacy.oldZeroFalse],['legacy19',legacy.legacy]])assert(keys.has(path.win32.resolve(row.path).toLowerCase()),key);});
// A bounded registration check is not runtime acceptance. Runtime controls only
// run against an explicitly supplied fresh actual receipt, never an old default.
const explicit=process.env.YMCC_MANAGED_TERMINAL_EXECUTION;
const globalRoot=process.env.YMCC_SIGNAL_EVIDENCE_RECEIPTS;
if(explicit||globalRoot){
 const first=read(explicit??path.join(globalRoot,c.child));
 const r=first.runs?first:read(first.receipt.path),tools=toolInputs(REPO),ctx={repo:REPO,sourceHashes:r.sourceHashes,tools};
 test('fresh managed22 shared executor proves both current builds and all ten semantic negative builds',()=>{const v=validateManagedTerminal(r,ctx);assert.equal(v.currentChecks,84);assert.equal(v.specifiedNegativeBuilds,10);assert.equal(v.wholeLifecycleAcceptance,false);});
 for(const [name,change,code] of [
  ['failed aggregate',x=>x.pass=false,/MANAGED_V2_AGGREGATE_SCOPE/],
  ['fake whole lifecycle acceptance',x=>x.wholeLifecycleAcceptance=true,/MANAGED_V2_AGGREGATE_SCOPE/],
  ['process exit used as cleanup',x=>x.processExitUsedAsCleanup=true,/MANAGED_V2_AGGREGATE_SCOPE/],
  ['lost semantic negative',x=>x.runs.pop(),/MANAGED_V2_SIX_ROUTE_MATRIX/],
  ['duplicate route',x=>x.runs[5].fault=x.runs[0].fault,/MANAGED_V2_SIX_ROUTE_MATRIX/],
  ['lost runtime closure',x=>delete x.sourceHashes['InputHost/HostTerminalCleanup.cs'],/MANAGED_V2_AGGREGATE_SCOPE/],
  ['incomplete dotnet seal',x=>x.toolsAfter.pop(),/MANAGED_V2_COMPLETE_DOTNET_SEALS/],
  ['rejected child reported accepted',x=>x.runs[0].accepted=false,/MANAGED_V2_ACTUAL_RUN_ACCEPTED/],
  ['changed actual receipt',x=>x.runs[0].receipt.sha256='0'.repeat(64),/MANAGED_V2_ACTUAL_RUN_PIN|MANAGED_V2_ACTUAL_CONTEXT_PIN/],
  ['changed context receipt',x=>x.runs[0].context.sha256='0'.repeat(64),/MANAGED_V2_ACTUAL_RUN_PIN|MANAGED_V2_ACTUAL_CONTEXT_PIN/],
 ])test('fresh managed22 rejects '+name,()=>{const copy=structuredClone(r);change(copy);assert.throws(()=>validateManagedTerminal(copy,ctx),code);});
 for(const fault of c.faults.filter(f=>f!=='none'))test('fresh managed22 rejects missing specified assertion for '+fault,()=>{const actual=read(r.runs.find(run=>run.fault===fault).receipt.path),required=c.negativeContracts.find(row=>row.fault===fault);assert(required);for(const row of actual.profiles){const hit=row.result.cases.find(x=>x.name===required.requiredFailedCase);assert(hit);hit.pass=true;hit.error=null;}assert.throws(()=>validateManagedProfiles(actual,c,fault),/MANAGED_SPECIFIED_SEMANTIC_NEGATIVE/);});
}

// v1 receipts stay on their original source/identity authority; v2 is the only
// default current route. No child consumer is rerun by these artifact controls.
import {managedCurrentVersion,managedExpectedUnit} from './managed-terminal-receipts.mjs';
import {validateManagedV2Initialization,validateManagedV2Artifacts,managedV2IndependentResult,validateManagedV2IndependentExecution} from './managed-terminal-v2.mjs';
const versionSchema=read(new URL('./consumer-schema.json',import.meta.url));
const qualified=read(c.qualifiedAggregate.path),currentTools=toolInputs(REPO),qualifiedContext={repo:REPO,sourceHashes:qualified.sourceHashes,tools:currentTools};
const historic=versionSchema.managedTerminalVersions.historicalV1,historicAggregate=read(historic.aggregate.path);
const basisReceipt=read(c.basisCurrent.path),initOriginal=read(c.initializationBoundary.receipt.path);
test('v2 independent reference resolves the retained parent execution without rewriting its controls',()=>{
 const execution=managedV2IndependentResult(c),before=fs.readFileSync(c.actualIndependentResult);
 assert.equal(path.resolve(c.actualIndependentResult),path.resolve(c.evidenceRoot,'parent-formal-r1/EXECUTION.json'));
 assert.equal(execution.version,2);assert.equal(execution.validation.currentChecks,84);assert.equal(execution.validation.specifiedNegativeBuilds,10);assert.equal(execution.validation.wholeLifecycleAcceptance,false);
 assert.deepEqual(validateManagedV2IndependentExecution(structuredClone(execution),c),execution);
 assert.deepEqual(fs.readFileSync(c.actualIndependentResult),before);
});
for(const [name,value,code]of [
 ['missing field',undefined,/MANAGED_V2_INDEPENDENT_REFERENCE_LOCATION/],
 ['missing file',path.join(c.evidenceRoot,'missing-reference-control/EXECUTION.json'),/MANAGED_V2_INDEPENDENT_REFERENCE_MISSING/],
 ['relative file','parent-formal-r1/EXECUTION.json',/MANAGED_V2_INDEPENDENT_REFERENCE_LOCATION/],
 ['v1 history path',versionSchema.managedTerminalCurrent.actualIndependentResult,/MANAGED_V2_INDEPENDENT_REFERENCE_LOCATION/],
 ['aggregate used as execution',c.qualifiedAggregate.path,/MANAGED_V2_INDEPENDENT_REFERENCE_LOCATION/],
])test('v2 independent reference rejects '+name,()=>{const changed=structuredClone(c);changed.actualIndependentResult=value;assert.throws(()=>managedV2IndependentResult(changed),code);});
for(const [name,change,code]of [
 ['v1 execution',r=>r.version=1,/MANAGED_V2_INDEPENDENT_SCOPE/],
 ['foreign execution scope',r=>r.scope='whole-product-main',/MANAGED_V2_INDEPENDENT_SCOPE/],
 ['failed execution',r=>r.pass=false,/MANAGED_V2_INDEPENDENT_SCOPE/],
 ['parent lifecycle promotion',r=>r.parentLifecycleAcceptance='COMPLETE',/MANAGED_V2_INDEPENDENT_SCOPE/],
 ['whole lifecycle promotion',r=>r.validation.wholeLifecycleAcceptance=true,/MANAGED_V2_INDEPENDENT_VALIDATION_SCOPE/],
 ['missing validation',r=>delete r.validation,/MANAGED_V2_INDEPENDENT_VALIDATION_SCOPE/],
 ['process exit cleanup claim',r=>r.validation.processExitUsedAsCleanup=true,/MANAGED_V2_INDEPENDENT_VALIDATION_SCOPE/],
 ['old 80-check summary',r=>r.validation.currentChecks=80,/MANAGED_V2_INDEPENDENT_VALIDATION_COUNTS/],
 ['different product source',r=>r.sourceHashes['InputHost/Program.cs']='0'.repeat(64),/MANAGED_V2_INDEPENDENT_SOURCE_IDENTITY/],
])test('v2 independent reference rejects '+name,()=>{const changed=read(c.actualIndependentResult);change(changed);assert.throws(()=>validateManagedV2IndependentExecution(changed,c),code);});
test('current formal dispatch explicitly selects v2 while preserving v1 contract and exact old source authority',()=>{assert.equal(managedCurrentVersion(),2);assert.equal(c.groupVersion,2);assert.equal(c.casesPerProfile,42);const old=managedContract(REPO,{version:1,historicalSnapshot:true});assert.equal(old.groupVersion,1);assert.equal(old.casesPerProfile,40);assert.equal(old.sourceInputs.length,39);assert.equal(versionSchema.managedTerminalCurrent.groupVersion,1);assert.equal(versionSchema.managedTerminalCurrent.casesPerProfile,40);assert.match(runManagedTerminalGroup.toString(),/version:managedCurrentVersion\(\)/);});
test('current v2 consumes all actual42 resources, five exact negative masks,571 tools and retained initialization failure',()=>{const v=validateManagedTerminal(qualified,qualifiedContext);assert.equal(v.version,2);assert.equal(v.currentChecks,84);assert.equal(v.currentCasesPerProfile,42);assert.equal(v.specifiedNegativeBuilds,10);assert.deepEqual(v.exactNegativeFailureCounts.map(x=>x.perProfile),[18,19,3,9,24]);assert.equal(v.initializationOriginalRejectedBuilds,2);assert.equal(v.wholeLifecycleAcceptance,false);});
test('complete v1 historical aggregate remains verifiable with its original identity and generated units',()=>{const v=validateManagedTerminal(historicAggregate,{repo:REPO,sourceHashes:historicAggregate.sourceHashes,tools:currentTools,version:1,historicalSnapshot:true});assert.equal(v.currentCasesPerProfile,40);assert.equal(v.currentChecks,80);assert.equal(v.legacyCasesPerProfile,19);assert.equal(v.retainedOldZeroFalseFailures,4);});
test('old v1 cannot approve current v2 and current v2 cannot approve old v1',()=>{assert.throws(()=>validateManagedTerminal(historicAggregate,{...qualifiedContext,sourceHashes:historicAggregate.sourceHashes}),/MANAGED_AGGREGATE_VERSION/);assert.throws(()=>validateManagedTerminal(qualified,{...qualifiedContext,version:1,historicalSnapshot:true}),/MANAGED_AGGREGATE_VERSION/);assert.throws(()=>managedContract(REPO,{version:3}),/MANAGED_UNSUPPORTED_VERSION/);});
for(const [name,change,code]of [
 ['missing initialization case',r=>r.profiles[0].result.cases.splice(18,1),/MANAGED_V2_ACTUAL_42_CASES/],
 ['reordered actual cases',r=>r.profiles[0].result.cases.reverse(),/MANAGED_V2_ACTUAL_42_CASES/],
 ['initialization receipt lost',r=>r.profiles[0].result.cases.find(x=>x.name==='terminal-initialization-throws').receipts=[],/MANAGED_V2_ACTUAL_OWNER_RESOURCE_RECEIPT/],
 ['initialization source failure washed',r=>r.profiles[0].result.cases.find(x=>x.name==='terminal-initialization-throws').receipts[0].SourceExitCode=0,/MANAGED_V2_ACTUAL_OWNER_RESOURCE_RECEIPT/],
 ['diagnostic disposed before settlement',r=>r.profiles[0].result.cases.find(x=>x.name==='terminal-initialization-healthy').stageAfterSettle=false,/MANAGED_V2_ACTUAL_OWNER_RESOURCE_RECEIPT/],
 ['resource survives current initialization',r=>r.profiles[0].result.cases.find(x=>x.name==='terminal-initialization-throws').beforeFixtureCleanup.workers[0].outputAlive=true,/MANAGED_V2_ACTUAL_OWNER_RESOURCE_RECEIPT/],
 ['zero exit erases retained owner failure',r=>r.profiles[0].result.cases.find(x=>x.name==='direct-output-permanent').terminalReceipts[0].ExitCode=0,/MANAGED_V2_ACTUAL_OWNER_RESOURCE_RECEIPT/],
 ['SDK scope claim',r=>r.profiles[0].result.realSdkAssemblyLoaded=true,/MANAGED_V2_TERMINAL_SCOPE/],
 ['process exit claimed as release',r=>r.resourceAcceptance='OS_FALLBACK_PASS',/MANAGED_V2_BOUNDARY_OR_STATUS/],
 ['source instability',r=>r.sourceStable=false,/MANAGED_V2_BOUNDARY_OR_STATUS/],
 ['failed profile accepted on process0',r=>r.profiles[0].exit=1,/MANAGED_V2_PROFILE_EXIT/],
 ])test('v2 profiles reject '+name,()=>{const copy=structuredClone(basisReceipt);change(copy);assert.throws(()=>validateManagedProfiles(copy,c,'none'),code);});
for(const n of c.negativeContracts){
 const actual=read(n.receipt.path);
 test('v2 keeps actual required failure and complete mask for '+n.fault,()=>{validateManagedProfiles(actual,c,n.fault);const copy=structuredClone(actual),rt=copy.profiles[0].result;const other=rt.cases.find(x=>!x.pass&&x.name!==n.requiredFailedCase);assert(other);other.pass=true;other.error=null;assert.throws(()=>validateManagedProfiles(copy,c,n.fault),/MANAGED_V2_EXACT_NEGATIVE_MASK/);});
 test('v2 does not accept an unrelated exit1 in place of '+n.fault,()=>{const copy=structuredClone(actual),rt=copy.profiles[0].result;rt.cases.find(x=>x.name===n.requiredFailedCase).pass=true;assert.throws(()=>validateManagedProfiles(copy,c,n.fault),/MANAGED_V2_ACTUAL_42_CASES|MANAGED_SPECIFIED_SEMANTIC_NEGATIVE/);});
}
for(const [name,change,code]of [
 ['original init suddenly approved',r=>r.profiles[0].result.cases[18].pass=true,/MANAGED_V2_ACTUAL_42_CASES|MANAGED_V2_INIT_ORIGINAL_MASK/],
 ['original init falsely has settlement',r=>r.profiles[0].result.cases[18].receipts=[{}],/MANAGED_V2_INIT_ORIGINAL_BYPASSED_SETTLE/],
 ['original init resources silently salvaged',r=>r.profiles[0].result.cases[18].beforeFixtureCleanup.workers[0].outputAlive=false,/MANAGED_V2_INIT_ORIGINAL_BYPASSED_SETTLE/],
 ])test('v2 retains original41of42 rejection: '+name,()=>{const copy=structuredClone(initOriginal);change(copy);assert.throws(()=>validateManagedV2Initialization(copy,c),code);});
for(const [name,change,code]of [
 ['v1 aggregate shape',r=>r.version=1,/MANAGED_AGGREGATE_VERSION/],
 ['OS fallback claim',r=>r.wholeLifecycleAcceptance=true,/MANAGED_V2_AGGREGATE_SCOPE/],
 ['process exit cleanup claim',r=>r.processExitUsedAsCleanup=true,/MANAGED_V2_AGGREGATE_SCOPE/],
 ['incomplete571 tool set',r=>r.toolsAfter.pop(),/MANAGED_V2_COMPLETE_DOTNET_SEALS/],
 ['missing actual mutant',r=>r.runs.pop(),/MANAGED_V2_SIX_ROUTE_MATRIX/],
 ['source identity substitution',r=>r.sourceHashes['InputHost/Program.cs']='0'.repeat(64),/MANAGED_V2_AGGREGATE_SCOPE|MANAGED_V2_AGGREGATE_SOURCE_IDENTITY/],
 ['receipt pin substitution',r=>r.runs[0].receipt.sha256='0'.repeat(64),/MANAGED_V2_ACTUAL_RUN_PIN/],
 ])test('v2 aggregate rejects '+name,()=>{const copy=structuredClone(qualified);change(copy);assert.throws(()=>validateManagedTerminal(copy,qualifiedContext),code);});
const basisRun=qualified.runs.find(x=>x.fault==='none'),artifactContext=read(basisRun.context.path),artifactRoot=path.dirname(basisRun.receipt.path);
for(const [name,change,code]of [
 ['altered source hash',(r,ctx)=>r.inputs[0].sha256='0'.repeat(64),/MANAGED_V2_ACTUAL_SOURCE_SNAPSHOT/],
 ['incomplete producer571 tools',(r,ctx)=>r.toolchain.pop(),/MANAGED_V2_COMPLETE_PRODUCER_TOOL_INPUT_LOGS/],
 ['missing generated or executed binary',(r,ctx)=>ctx.integratedArtifacts.pop(),/MANAGED_V2_COMPLETE_GENERATED_AND_EXECUTED_ARTIFACTS/],
 ['changed actual build log pin',(r,ctx)=>ctx.entryArtifacts['Release-build.log'].sha256='0'.repeat(64),/MANAGED_V2_ENTRY_ARTIFACT_PIN/],
 ])test('v2 artifacts reject '+name,()=>{const r=structuredClone(basisReceipt),ctx=structuredClone(artifactContext);change(r,ctx);assert.throws(()=>validateManagedV2Artifacts(r,c,'none',artifactRoot,REPO,ctx),code);});
test('v2 requires whole generated Main unit, not just42 names or a process result',()=>{const altered=structuredClone(c);altered.generatedUnits['ExtraProduction.cs']=versionSchema.managedTerminalCurrent.generatedUnits['ExtraProduction.cs'];assert.throws(()=>validateManagedV2Artifacts(basisReceipt,altered,'none',artifactRoot,REPO,artifactContext),/MANAGED_V2_WHOLE_ACTUAL_GENERATED_UNIT/);for(const n of c.negativeContracts){const unit=n.fault==='terminal-log-fatal'?'ExtraProduction.cs':'HostTerminalCleanup.cs';assert.notEqual(managedExpectedUnit(c,unit,n.fault),managedExpectedUnit(c,unit,'none'));}});
