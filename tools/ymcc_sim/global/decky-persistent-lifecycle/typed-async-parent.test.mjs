/** Actual sealed FAIL receipts only. No native execution or synthetic product PASS. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import test from 'node:test';
import {validateTypedAsyncParentEnvelope, reconstructTypedAsyncNative,
  validateTypedAsyncRequestParentReceipt, verifyTypedAsyncRequestOriginalRun} from './typed-async-parent.mjs';
const repo = path.resolve(import.meta.dirname, '../../../..');
const core = path.resolve(repo, '../../../Testing/YMCC');
const workBase = path.join(core,'Workspace/DeckyAsyncParentVerifier-20261023');
const evidence = path.join(core,'Evidence/DeckyAsyncParentVerifier-20261023');
const read = file => JSON.parse(fs.readFileSync(file,'utf8'));
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key = file => path.resolve(file).toLowerCase();
const specs = [
  {id:'global-r1',round:'global-d9301593',result:'fe19203f071a9658de63a0a1bb0cb1a981dbf86071b11b73acd95eb9be71b4be',
    raw:'4d87ac7c9833345e2eb4b3fb0ffbc7d3e0dd2e0b032ce0743e19cb8f1f80cbf4',
    sources:'3c3d2007650210536ac992bb3525b458e2240b50f68a0999d82ef78a3248b76d',typedFailed:0},
  {id:'historical-main',round:'main-f2caee83',result:'5481299b0d6425df7f1412b681534aae10c3b97c8e26c3481901abc7bc0435d7',
    raw:'dbc36b19dc6739c5198cc3f0839de0f09e49faae2b138cdfcac75d9afc30ce9c',
    sources:'87eb0fe6f89e6ea9ac28a323c320d738a71a6bcd09eadd471c47d083fb628696',typedFailed:6},
];
const toolsSha = '48c79b4c0e5d01aeca8defbc57b579d24b4a059d1c572af512baced869b17df3';
const historicalTest = path.join(core,'Evidence/DeckyAsyncRequestContinuation-20261023/owner-identity-r1/receipt-controls.before.mjs');
fs.mkdirSync(workBase,{recursive:true}); fs.mkdirSync(evidence,{recursive:true});
const fixtures = specs.map(spec => {
  const root = path.join(core,'Evidence/DeckyTypedLegacyAsyncRequest-20261023',spec.round);
  for (const [name,expected] of [['result.json',spec.result],['native-result.json',spec.raw],['SOURCE-PINS.json',spec.sources],['TOOL-PINS.json',toolsSha]])
    assert.equal(sha(path.join(root,name)),expected,'SEALED_ORIGINAL_CHANGED:'+spec.id+'/'+name);
  const result = read(path.join(root,'result.json')), raw = read(path.join(root,'native-result.json'));
  const workRoot = result.executions[2].cwd, capturedRepo = path.join(workBase,spec.id,'source-view');
  const provenance = [];
  // Copy only original frozen bytes or a matching current file. Historical
  // controls have a separately preserved before-file, never a rewritten pin.
  for (const pin of result.sourceInputs) {
    const candidates = [path.join(capturedRepo,pin.path),path.join(workRoot,'s',pin.path),path.join(repo,pin.path),historicalTest];
    const original = candidates.find(file=>fs.existsSync(file)&&sha(file)===pin.sha256);
    assert(original,'EXACT_FROZEN_SOURCE_UNAVAILABLE:'+pin.path);
    const destination = path.join(capturedRepo,pin.path); fs.mkdirSync(path.dirname(destination),{recursive:true});
    if (!fs.existsSync(destination)) fs.copyFileSync(original,destination);
    assert.equal(sha(destination),pin.sha256,'CAPTURED_SOURCE_CHANGED');
    provenance.push({path:pin.path,sha256:pin.sha256,original});
  }
  const provenanceFile=path.join(workBase,spec.id,'FROZEN-SOURCE-PROVENANCE.json');
  if(!fs.existsSync(provenanceFile))fs.writeFileSync(provenanceFile,JSON.stringify(provenance,null,2)+'\n');
  else assert.deepEqual(read(provenanceFile).map(({path,sha256})=>({path,sha256})),provenance.map(({path,sha256})=>({path,sha256})),'SEALED_SOURCE_PROVENANCE_CHANGED');
  const options = {repo:capturedRepo,currentRepo:repo,sourceMode:'frozen',evidenceRoot:root,workRoot,
    sourcePins:new Map(result.sourceInputs.map(row=>[row.path,row.sha256])),
    toolPins:new Map(result.toolArtifacts.map(row=>[key(row.path),row])),
    receiptPin:{path:path.join(root,'result.json'),sha256:spec.result}};
  return {...spec,root,result,raw,options,exe:result.executions[3].program.path};
});
for (const fixture of fixtures) test(fixture.id+' is verified FAIL evidence, with actual ordered conditions and explicit source status',()=>{
  const envelope = validateTypedAsyncRequestParentReceipt(fixture.result,fixture.options);
  assert.equal(envelope.verified,true); assert.equal(envelope.status,'CURRENT_TYPED_ASYNC_REQUEST_FAIL');
  assert.equal(envelope.acceptance,false); assert.equal(envelope.allowAcceptance,false);
  assert.equal(envelope.fullPersistentAcceptance,false);
  const drift=fixture.result.sourceInputs.filter(row=>!fs.existsSync(path.join(repo,row.path))||sha(path.join(repo,row.path))!==row.sha256).map(row=>row.path);
  assert.deepEqual(envelope.staleSources,drift);assert.equal(envelope.currentSourceStatus,drift.length?'STALE':'CURRENT');
  assert.equal(envelope.summary.typedFailed,fixture.typedFailed); assert.equal(envelope.checked.originalPredicates,72);
  assert.equal(envelope.summary.strictParentHandlesEqual,false); assert.equal(envelope.summary.calibrationPassed,false);
  assert(envelope.missingPredicates.some(row=>row.predicate==='WinHTTP calibrationPassed'));
  assert(envelope.missingPredicates.some(row=>row.predicate==='strictParentHandlesEqual'));
  assert.equal(envelope.legacy.status,'PASS'); assert.equal(envelope.legacy.independentlyExecuted,true);
  fs.writeFileSync(path.join(evidence,fixture.id+'.VERIFIED-FAIL.json'),JSON.stringify(envelope,null,2)+'\n');
});
const latest = fixtures[0], old = fixtures[1];
const nativeReject = (name,fixture,edit,pattern) => test(name,()=>{
  const copy = structuredClone(fixture.raw); edit(copy);
  assert.throws(()=>reconstructTypedAsyncNative(copy,{executablePath:fixture.exe}),pattern);
});
nativeReject('a passing six-row summary cannot promote the actual failed parent',latest,r=>r.passed=true,/NATIVE_VERDICT/);
nativeReject('falsely claiming handle equality is rejected',latest,r=>r.strictParentHandlesEqual=true,/HANDLE_EQUALITY_REPORT/);
nativeReject('falsely claiming WinHTTP calibration PASS is rejected',latest,r=>r.calibrationPassed=true,/CALIBRATION_REPORT/);
nativeReject('deleting one of the original 12 conditions is rejected',old,r=>r.originalTypedRows[0].originalAssertions.pop(),/ORDERED_12/);
nativeReject('reordering original conditions is rejected',latest,r=>r.originalTypedRows[0].originalAssertions.reverse(),/ORDERED_12/);
nativeReject('turning historical closed failure into PASS without actual callback release is rejected',old,r=>{
  const condition=r.originalTypedRows[0].originalAssertions.find(x=>x.id==='closed()');condition.passed=true;condition.status='PASS';
},/RECONSTRUCTED_CONDITION/);
nativeReject('changing callback observation but hiding its changed actual is rejected',old,r=>r.originalTypedRows[1].afterStopAsync.bindingReturned=true);
nativeReject('removing an entire failed lifecycle row is rejected',old,r=>r.originalTypedRows.pop(),/ROW_MEMBERSHIP/);
nativeReject('callback1500ms cannot expand',latest,r=>r.originalTypedRows[0].callbackCloseDeadlineMs=1501);
nativeReject('original20s native budget cannot expand',latest,r=>r.originalNativeBudgetMs=20001);
nativeReject('owned process exit cannot conceal actual owner handle residue',latest,r=>r.originalTypedRows[0].atExit.ownerHandles=1,/RESOURCE_REPORT/);
for (const [name,edit] of [
  ['outer135s cannot expand',r=>r.budgets.outerTypedGateMs=135001],
  ['compile60s cannot expand',r=>r.executions[2].originalBudgetMs=60001],
  ['original FAIL cannot be relabeled as PASS',r=>{r.passed=true;r.status='PASS';}],
]) test(name,()=>{const copy=structuredClone(latest.result);edit(copy);assert.throws(()=>validateTypedAsyncParentEnvelope(copy));});
test('wrong source bytes cannot be repinned as the executed frozen source',()=>{
  const file=path.join(latest.options.repo,'native/decky_sidebar_runtime.h'),original=fs.readFileSync(file);
  try {
    fs.writeFileSync(file,Buffer.concat([original,Buffer.from('\n// test-owned wrong source\n')]));
    assert.throws(()=>validateTypedAsyncRequestParentReceipt(latest.result,latest.options),/FILE_CHANGED/);
  } finally { fs.writeFileSync(file,original); }
});
test('source prebinding cannot be substituted by a fresh hash',()=>{
  const sourcePins = new Map(latest.options.sourcePins);sourcePins.set('native/decky_sidebar_runtime.h','0'.repeat(64));
  assert.throws(()=>validateTypedAsyncRequestParentReceipt(latest.result,{...latest.options,sourcePins}),/SOURCE_NOT_PREBOUND/);
});
test('compiler tool must match the external prebinding',()=>{
  const toolPins = new Map(latest.options.toolPins),program=latest.result.executions[2].program;
  toolPins.set(key(program.path),{...program,sha256:'0'.repeat(64)});
  assert.throws(()=>validateTypedAsyncRequestParentReceipt(latest.result,{...latest.options,toolPins}),/TOOL_PREBINDING_CHANGED/);
});
test('changed result object cannot replace pinned original bytes',()=>{
  const copy=structuredClone(latest.result);copy.summary.typedFailed=6;
  assert.throws(()=>validateTypedAsyncRequestParentReceipt(copy,latest.options),/ORIGINAL_RESULT_MISMATCH/);
});
test('wrong raw result pin rejects before any previous verdict can be reused',()=>{
  assert.throws(()=>validateTypedAsyncRequestParentReceipt(latest.result,{...latest.options,
    receiptPin:{...latest.options.receiptPin,sha256:'0'.repeat(64)}}),/FILE_CHANGED/);
});
test('registered public wrapper consumes the same originals and preserves FAIL',()=>{
  const pointer=path.join(core,'Evidence/Build/DeckyAsyncRequestContinuation-20261023/global-r1/2026-10-23T05-04-17-797Z-e691c48f/cases/plugins.decky-typed-async-request/typed-async/ORIGINAL-RUN.json');
  const expected='c26ff2656b2d784386d6c8b2dfdfd8720b38427b53a63521df10537150876dd5';assert.equal(sha(pointer),expected);
  const envelope=verifyTypedAsyncRequestOriginalRun({path:pointer,sha256:expected},latest.options);
  assert.equal(envelope.verified,true);assert.equal(envelope.acceptance,false);assert.equal(envelope.publicCopiesVerified,4);
  assert.equal(envelope.summary.typedFailed,0);assert.equal(envelope.status,'CURRENT_TYPED_ASYNC_REQUEST_FAIL');
  fs.writeFileSync(path.join(evidence,'PUBLIC-VERIFIED-FAIL.json'),JSON.stringify(envelope,null,2)+'\n');
});