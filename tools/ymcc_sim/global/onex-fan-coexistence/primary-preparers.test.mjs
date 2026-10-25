import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {PRIMARY_PROFILES,ORIGINAL_BUDGETS,capturePrimarySourceBinding,projectPrimaryStageMODEL_ONLY,assertManagedCompilationComparison} from './primary-preparers-contract.mjs';
import {inspectPublicFactoryInputs,preparePublicFactory} from './prepare-public-factory.mjs';
import {inspectOneXLifecycleInputs} from './prepare-onex-lifecycle.mjs';
const json=f=>JSON.parse(fs.readFileSync(f,'utf8').replace(/^\uFEFF/,''));
const L=json(new URL('./primary-preparers-reviewed-lineage.json',import.meta.url));
const B=json(new URL('./primary-preparers-source-basis.json',import.meta.url));
const pin=f=>({path:f,sha256:crypto.createHash('sha256').update(fs.readFileSync(f)).digest('hex')});
function model(stage,role='positive'){
  const b=json(L.stages[stage].roles[role].prepared.path);
  return {stage,role,bundle:{evidenceKind:'MODEL_ONLY',b,p:json(b.policy.path),n:json(b.runtimePins.path),argv:json(b.argv.path)},input:{outDir:'D:/MODEL_ONLY/not-created'}};
}
const project=m=>projectPrimaryStageMODEL_ONLY(m);
test('MODEL_ONLY: all fourteen original role projections preserve IDs/masks/budgets and real Default/framework pair',()=>{
  for(const stage of ['public','onex'])for(const [role,a]of Object.entries(L.stages[stage].roles)){
    const m=model(stage,role),before=structuredClone(m),p=project(m);assert.deepEqual(m,before);
    assert.equal(p.evidenceKind,'MODEL_ONLY');assert.equal(p.executionApprovalGranted,false);
    assert.deepEqual(p.expectedCaseIds,L.stages[stage].ids);assert.deepEqual(p.expectedFailureMask,a.expectedFailureMask);
    assert.deepEqual(p.budgets,{business:60000,parent:360000,generation:30000,compile:120000});
    assert.equal(p.policy.contexts[0].managed.length,stage==='public'?88:92);
    assert.equal(p.policy.contexts[0].managed.filter(x=>x.role==='framework').length,stage==='public'?60:64);
    assert.equal(p.policy.exactAliases.length,6);assert.equal(p.files.length,115);assert.equal(Object.keys(p.nativePins).length,116);
    assert.equal(p.args.length,9);assert.equal(p.args[8],stage==='public'?'public':'lifecycle');
    assert.equal(p.policy.contexts[0].id,'Default');assert.equal(p.policy.contexts[0].kind,'default');
    assert.deepEqual(m.bundle.p.metadata,p.policy.metadata);
    for(const f of p.policy.contexts[0].managed.filter(x=>x.role==='framework'))assert.equal(Object.hasOwn(p.nativePins,f.path),false);
  }
});
test('MODEL_ONLY: refuses reordered IDs, changed masks, missing model label and fake success',()=>{
  for(const stage of ['public','onex']){
    let m=model(stage);m.bundle.b.expectedCaseIds.reverse();assert.throws(()=>project(m),/PRIMARY_ORIGINAL_CASES_AND_MASKS/);
    m=model(stage,Object.keys(L.stages[stage].roles)[1]);m.bundle.b.expectedFailureMask=[];assert.throws(()=>project(m),/PRIMARY_ORIGINAL_CASES_AND_MASKS/);
    m=model(stage);delete m.bundle.evidenceKind;assert.throws(()=>project(m),/PRIMARY_MODEL_LABEL_REQUIRED/);
    m=model(stage,Object.keys(L.stages[stage].roles)[1]);m.bundle.b.expectedExitCode=0;assert.throws(()=>project(m),/PRIMARY_ORIGINAL_CASES_AND_MASKS/);
  }
});
test('MODEL_ONLY: refuses a 90000ms business budget and UNKNOWN erasure',()=>{
  const m=model('public');m.bundle.b.budgets.business=90000;assert.throws(()=>project(m),/PRIMARY_ORIGINAL_BUDGET/);
  const n=model('onex');n.bundle.b.initializeRole='PASS';assert.throws(()=>project(n),/PRIMARY_UNKNOWN_AND_BOUNDARY/);
  assert.equal(ORIGINAL_BUDGETS.business,60000);
});
test('MODEL_ONLY: refuses wrong framework pair, custom context, native framework permission and missing public argument',()=>{
  let m=model('public');m.bundle.p.contexts[0].managed.pop();assert.throws(()=>project(m),/PRIMARY_MANAGED_FRAMEWORK_PAIR/);
  m=model('onex');m.bundle.p.contexts[0].kind='custom';assert.throws(()=>project(m),/PRIMARY_REAL_DEFAULT/);
  m=model('public');const f=m.bundle.p.contexts[0].managed.find(x=>x.role==='framework');delete m.bundle.n[Object.keys(m.bundle.n)[0]];m.bundle.n[f.path]=f.sha256;assert.throws(()=>project(m),/PRIMARY_NATIVE_GUARD116/);
  m=model('public');m.bundle.b.args.pop();assert.throws(()=>project(m),/PRIMARY_NINE_CONCRETE_ARGUMENTS/);
});
test('refuses old D144 as current source root, missing repo and unknown source profile',async()=>{
  await assert.rejects(capturePrimarySourceBinding({repoRoot:'D:/old-source144'}),/PRIMARY_REAL_G_ROOT_REQUIRED/);
  await assert.rejects(capturePrimarySourceBinding({}),/PRIMARY_REAL_G_ROOT_REQUIRED/);
  assert.notEqual(PRIMARY_PROFILES.public,'public-original123-fixed83-schema6');
});
test('whole managed comparison requires both compiled SHA pins, all metadata/heaps/static data and no IL differences',()=>{
  const doc=json(L.currentBuilds.hostManagedComparison.path),row=doc.results[0];
  assert.equal(assertManagedCompilationComparison(row,row.before,row.after),true);
  for(const change of [r=>r.changes.push('IL'),r=>r.heapsCompared.pop(),r=>r.allMetadataRowsExceptMethodAndFieldRvasEqual=false,r=>r.methods--,r=>r.initializedFields--,r=>r.mvidDifference.onlySingleMvidGuidHeap=false]){
    const r=structuredClone(row);change(r);assert.throws(()=>assertManagedCompilationComparison(r,row.before,row.after),/PRIMARY_MANAGED_/);
  }
  assert.throws(()=>assertManagedCompilationComparison(row,row.before,{...row.after,sha256:'a'.repeat(64)}),/PRIMARY_MANAGED_COMPARISON_PAIR/);
});
let currentInput;
function realOptions(stage){
  const parent=process.env.YMCC_PRIMARY_PREPARER_TEST_ROOT;
  assert.ok(parent&&path.isAbsolute(parent),'explicit owned test root is required');fs.mkdirSync(parent,{recursive:true});
  const file=path.join(parent,stage+'-input.json'),input={schemaVersion:1,profile:PRIMARY_PROFILES[stage],role:'positive',repoRoot:B.repoRoot,ownedParent:parent,outDir:path.join(parent,stage+'-must-not-be-created'),sourceBindingPin:currentInput};
  fs.writeFileSync(file,JSON.stringify(input,null,2)+'\n');return {inputPin:pin(file)};
}
test('actual read-only intake binds current G144 and latest CLI, rehashes r70 compiler inputs, reports new images unqualified without creating stage',async()=>{
  const parent=process.env.YMCC_PRIMARY_PREPARER_TEST_ROOT;assert.ok(parent);fs.mkdirSync(parent,{recursive:true});
  const binding=await capturePrimarySourceBinding({repoRoot:B.repoRoot}),file=path.join(parent,'SOURCE-BINDING.json');fs.writeFileSync(file,JSON.stringify(binding,null,2)+'\n');currentInput=pin(file);
  assert.ok([B.currentPrimaryBaseSourceCount,B.currentPrimaryBaseSourceCount+4].includes(binding.toolSourceCount));assert.equal(binding.sharedCLI.sha256,B.sources.find(x=>x.relative.endsWith('/global/cli.mjs')).sha256);
  for(const [stage,inspect]of [['public',inspectPublicFactoryInputs],['onex',inspectOneXLifecycleInputs]]){
    const options=realOptions(stage),result=await inspect(options);assert.equal(result.status,'BLOCKED_CURRENT_COMPILED_PINS_NOT_QUALIFIED');assert.equal(result.hostManagedSourceEquivalent,true);assert.equal(result.currentCompiledPinsQualified,false);assert.equal(result.currentBusinessAccepted,false);assert.equal(result.fullFan360Passed,false);assert.equal(result.stageCreated,false);assert.equal(result.targetExecutions,0);
    const i=json(options.inputPin.path);assert.equal(fs.existsSync(i.outDir),false);fs.writeFileSync(path.join(parent,stage+'-inspection.json'),JSON.stringify(result,null,2)+'\n');
  }
});
test('actual prepare fails closed on missing new typed/API/ported-leaf qualifications and writes no payload',async()=>{
  assert.ok(currentInput);const options=realOptions('public');await assert.rejects(preparePublicFactory(options),/PRIMARY_CURRENT_COMPILED_PINS_NOT_QUALIFIED/);assert.equal(fs.existsSync(json(options.inputPin.path).outDir),false);
});
