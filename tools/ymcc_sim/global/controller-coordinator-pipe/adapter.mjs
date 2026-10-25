import fs from 'node:fs';
import path from 'node:path';
import {REPO,WORKSPACE} from '../controller-coordinator-lifecycle/adapter.mjs';
import {runtimeTools} from '../controller-lifecycle-pipe/adapter.mjs';
import {currentPreparedProfileActive,openCurrentPreparedContext,currentPreparedSources,currentPreparedArtifacts} from './r3-current-prepared.mjs';
import {selectCoordinatorProfile} from './r3-current-routing.mjs';
import {assertR3Native,readR3Managed,LIFECYCLE_VERSION,R3_SPEC,verifyCompilerLedgers} from './r3-contract.mjs';
import {resolveR3ManifestPin} from './r3-source-equivalence.mjs';
import {b54ActualProfileActive,b54ActualSources,b54ActualArtifacts,B54_CURRENT_G_REFERENCE_INPUTS,B54_ACTUAL_TOOL_INPUTS} from './r3-b54-actual-worker.mjs';
export {REPO,WORKSPACE};
export const MODULE='tools/ymcc_sim/global/controller-coordinator-pipe',CASE_ID='controller.coordinator-native-managed-pipe',MAX_CASE_INPUTS=640;
const POSITIVE_PIN='tools/ymcc_sim/global/controller-coordinator-lifecycle/precompiled-fixtures.json';
export const CASE_SPECS=Object.freeze([
 {key:'positive',id:CASE_ID,pinFile:POSITIVE_PIN,mutation:null,prefix:'POSITIVE',directory:'coordinator-pipe',title:'Full native + managed pipe and switch verification: R3 protected PS and complete original current pipe groups'},
 {key:'cancellation',id:CASE_ID+'.cancellation-negative',pinFile:MODULE+'/cancellation-negative.json',mutation:'rollback-cancellation-guards',prefix:'CANCELLATION',directory:'coordinator-pipe-cancellation-negative',title:'Compiled cancellation rollback: R3 complete pipe groups, B46-current identity; original exact18 failures plus11 executed-pass rules retained'},
 {key:'psOwnership',id:CASE_ID+'.ps-ownership-negative',pinFile:MODULE+'/ps-ownership-negative.json',mutation:'rollback-ps-endpoint-ownership',prefix:'PS_OWNERSHIP',directory:'coordinator-pipe-ps-ownership-negative',title:'Compiled PS SDK-root ownership rollback: R3 complete pipe groups, exact original PS ownership failures'},
 {key:'psBinding',id:CASE_ID+'.ps-binding-negative',pinFile:MODULE+'/ps-binding-negative.json',mutation:'rollback-ps-binding-revision',prefix:'PS_BINDING',directory:'coordinator-pipe-ps-binding-negative',title:'Compiled PS target-binding rollback: R3 complete pipe groups, exact original target-binding failures'}
]);
export const COMPILED_NEGATIVES=Object.freeze(CASE_SPECS.filter(s=>s.mutation));
export function caseSpec(key='positive'){const spec=CASE_SPECS.find(s=>s.key===key);if(!spec)throw Error('COORDINATOR_CASE_INVALID:'+key);return spec;}
function nativeContract(spec){
 const value=JSON.parse(fs.readFileSync(path.join(REPO,spec.pinFile)));
 const pin=spec.key==='positive'?(value.pipe??value.fixtures?.[0]):value;
 if(typeof pin?.manifest!=='string'||path.isAbsolute(pin.manifest)||pin.manifest.split(/[\\/]/).includes('..')||!/^[a-f0-9]{64}$/.test(pin.sha256))throw Error('MANIFEST_PATH');
 const manifest=resolveR3ManifestPin(pin);
 const native=JSON.parse(fs.readFileSync(manifest));assertR3Native(native);return {pin,manifest,value:native};
}
export function inputs(key='positive'){const spec=caseSpec(key),native=nativeContract(spec);return {spec,native,managed:readR3Managed(native.value,REPO,WORKSPACE)};}
const INFRASTRUCTURE=[MODULE+'/r3-current-routing.mjs',...B54_ACTUAL_TOOL_INPUTS,MODULE+'/adapter.mjs',MODULE+'/run.mjs',MODULE+'/README.md',MODULE+'/r3-contract.mjs',MODULE+'/r3-reviewed-contract.mjs',MODULE+'/r3-reviewed-contract.json',MODULE+'/r3-reviewed-scenarios.json',MODULE+'/r3-reviewed-branch-evidence.json',MODULE+'/r3-reviewed-ownership-evidence.json',MODULE+'/r3-prerequisite-root.json',MODULE+'/r3-source-equivalence.mjs',MODULE+'/r3-b49-frozen-authority.mjs',MODULE+'/r3-b51-frozen-authority.mjs',MODULE+'/r3-b49-compile-batch.mjs','tools/input_coordinator_private_terminals.mjs','tools/input_coordinator_b51_return_profile.mjs','tools/input_coordinator_private_terminal_prepare.mjs','tools/input_coordinator_runtime_hardening.mjs','tools/fixtures/input_coordinator_lifecycle/runtime_hardening.json','tools/input_coordinator_passing_chain.mjs','tools/fixtures/input_coordinator_lifecycle/passing_chain_source_profile.json','tools/ymcc_sim/global/controller-coordinator-pipe/r3-passing-bounded-summary.mjs','tools/fixtures/input_coordinator_lifecycle/passing_chain_case_contract.json','tools/fixtures/input_coordinator_lifecycle/private_terminal_observer.h','tools/ymcc_sim/global/snapshot-node-tools.mjs','tools/ymcc_sim/global/source-snapshot.mjs',MODULE+'/historical/source-snapshot-v6.mjs',MODULE+'/r3-managed-builder.mjs',MODULE+'/r3-cancellation-oracle.mjs',MODULE+'/r3-cancellation-oracle.json',MODULE+'/r3-cancellation-review.json',MODULE+'/r3-cancellation-cut47-independent-review.json',MODULE+'/r3-cancellation-b39-independent-review.json',MODULE+'/r3-scenarios.json',MODULE+'/bind-r3-fixtures.mjs','tools/input_coordinator_fixture_manifest.mjs','tools/ymcc_sim/global/controller-coordinator-lifecycle/adapter.mjs','tools/ymcc_sim/global/controller-lifecycle-pipe/adapter.mjs','tools/ymcc_sim/global/controller-lifecycle-pipe/managed-fixture.json','tools/ymcc_sim/global/controller-priority-ledger/adapter.mjs','tools/ymcc_sim/global/controller-priority/adapter.mjs','tools/ymcc_sim/global/inputhost-protocol/adapter.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'];
function bounded(values,kind){const result=[...new Set(values)];if(result.length>MAX_CASE_INPUTS)throw Error('COORDINATOR_'+kind+'_LIMIT:'+result.length+'/'+MAX_CASE_INPUTS);return result;}
export function sources(key='positive'){
 const selectedProfile=selectCoordinatorProfile(REPO);
 if(selectedProfile==='current')return currentPreparedSources(openCurrentPreparedContext(key,REPO));
 if(key==='positive'&&selectedProfile==='b54')return bounded([...b54ActualSources(),MODULE+'/r3-current-routing.mjs'],'SOURCE');
 const c=inputs(key);verifyCompilerLedgers(c.native.value,c.managed.value);
 return bounded([...c.native.value.sources.map(r=>r.path),...c.managed.mappedSources.map(r=>r.relative),...INFRASTRUCTURE,c.spec.pinFile],'SOURCE');
}
export function artifacts(key='positive'){
 const selectedProfile=selectCoordinatorProfile(REPO);
 if(selectedProfile==='current')return currentPreparedArtifacts(openCurrentPreparedContext(key,REPO));
 if(key==='positive'&&selectedProfile==='b54')return bounded([...b54ActualArtifacts()],'ARTIFACT');
 const c=inputs(key),n=c.native;
 return bounded([process.execPath,n.manifest,...n.value.generated.map(r=>r.path),...n.value.sources.map(r=>r.snapshot),...c.managed.artifacts,...runtimeTools()],'ARTIFACT');
}
export function getControllerCoordinatorPipeAdapters({readSources=sources,readArtifacts=artifacts,selectedIds}={}){
 if(selectedIds!==undefined&&(!Array.isArray(selectedIds)||!selectedIds.every(id=>typeof id==='string'&&id.length>0)))throw Error('COORDINATOR_SELECTION_SHAPE');
 const requested=selectedIds===undefined?null:new Set(selectedIds);
 return CASE_SPECS.map(spec=>{
  let actualB54=false;
  let registration;
  if(requested&&!requested.has(spec.id))registration={evidenceInputs:[...INFRASTRUCTURE,spec.pinFile],artifactInputs:[],blockedReason:'COORDINATOR_INPUTS_DEFERRED_OUTSIDE_EXPLICIT_SELECTION',inputRegistrationDeferred:true};
  else try{actualB54=spec.key==='positive'&&selectCoordinatorProfile(REPO)==='b54';registration={evidenceInputs:readSources(spec.key),artifactInputs:readArtifacts(spec.key)};if(registration.evidenceInputs.length>MAX_CASE_INPUTS||registration.artifactInputs.length>MAX_CASE_INPUTS)throw Error('COORDINATOR_CASE_INPUT_LIMIT');}
  catch(error){registration={evidenceInputs:[...INFRASTRUCTURE,spec.pinFile],artifactInputs:[],blockedReason:'COORDINATOR_PREREQUISITE_UNAVAILABLE: '+String(error?.message??error).slice(0,2048)};}
  return {id:spec.id,domain:'controller',title:actualB54?'Fixed B54 actual native/R2R full-pipe105/6458 each-leaf original60s':spec.title,lifecycleVersion:LIFECYCLE_VERSION,kind:'node',path:MODULE+'/run.mjs',args:['--case',spec.key,'--evidence-dir','{outDir}/'+spec.directory],timeoutMs:120000,profiles:['desktop','full'],scope:'behavioral-mock',...registration,...(actualB54?{referenceInputs:[...B54_CURRENT_G_REFERENCE_INPUTS]}:{}),resultFiles:[spec.directory+'/result.json'],boundary:actualB54?'This positive leaf actually executes exact fixed B54 native/R2R once under original60000ms, then validates its new105/6458/110-child aggregate into a bounded output. Current G product sources are unverified referenceInputs; wrapper/source tool changes do not assert current G product behavior. No old-run fallback.':'One current precompiled native full TU and the same current SDK-free managed codec/loop/HostSession over its real owned pipe. Actual switch receipts are candidates; the common verification consumer owns final release after 10s or failure at the original 30s cap. Case '+spec.key+' executes only its own native binary; negative cases require original exit 1, exact named failures and current expected stage semantics. No other native build or unexecuted positive proof is a prerequisite. Controlled SDK/SetupAPI/source/mode/visibility leaves; no normal Main, device/driver, OS sleep, physical return or game-consumption acceptance.'};
 });
}
