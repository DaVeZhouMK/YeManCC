/** Sealed actual child used only for receipt validation, not fresh parent acceptance. */
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {validateTypedRuntimeParentReceipt} from './typed-runtime-parent.mjs';
const repo=path.resolve(import.meta.dirname,'../../../..');
const core=path.resolve(repo,'../../../Testing/YMCC');
const fixtureRoot=path.join(core,'Evidence/DeckyTypedLegacyRuntime-20261023/t1-7755/r5');
const fixture=path.join(fixtureRoot,'result.json');
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
assert.equal(sha(fixture),'5bf3b7889c678c629883c5c838d047da6e9dd9b65125d870c9debd522985490e','SEALED_RUNTIME_RECEIPT_CHANGED');
const original=JSON.parse(fs.readFileSync(fixture,'utf8'));
const workRoot=path.join(core,'Workspace/DeckyTypedLegacyRuntime-20261023/t1-7755/p5');
// Historical controls use exact original source bytes, while a separate control
// keeps rejecting drift in the real current repository. No receipt pin changes.
const sourceView=path.join(core,'Workspace/DeckyRuntimeParentControls-20261023/source-view');
const provenance=[];
for(const pin of original.sourceInputs){
 const destination=path.join(sourceView,pin.path);
 const source=[destination,path.join(workRoot,'s',pin.path),path.join(repo,pin.path)].find(file=>fs.existsSync(file)&&sha(file)===pin.sha256);
 assert(source,'SEALED_RUNTIME_EXACT_SOURCE_UNAVAILABLE:'+pin.path);
 fs.mkdirSync(path.dirname(destination),{recursive:true});if(!fs.existsSync(destination))fs.copyFileSync(source,destination);
 assert.equal(sha(destination),pin.sha256);provenance.push({path:pin.path,sha256:pin.sha256,from:source});
}
const provenanceFile=path.join(core,'Workspace/DeckyRuntimeParentControls-20261023/SOURCE-PROVENANCE.json');
if(!fs.existsSync(provenanceFile))fs.writeFileSync(provenanceFile,JSON.stringify(provenance,null,2)+'\n');
const options=()=>({repo:sourceView,evidenceRoot:path.join(core,'Evidence/DeckyTypedLegacyRuntime-20261023'),workRoot,
 sourcePins:new Map(original.sourceInputs.map(row=>[row.path,row.sha256])),
 toolPins:new Map(original.toolArtifacts.map(row=>[path.resolve(row.path).toLowerCase(),row]))});
const clone=()=>structuredClone(original);
const run=(value=clone(),opts=options())=>validateTypedRuntimeParentReceipt(value,opts);
test('sealed actual38 and4 are validated as independent typed runtime evidence',()=>{
 const result=run();assert.equal(result.status,'CURRENT_TYPED_RUNTIME_INVARIANTS_EXECUTED');
 assert.equal(result.summary.passedAssertions,38);assert.equal(result.summary.negativeControls,4);
 assert.equal(result.legacyOriginalRegressionStillRequired,true);assert.equal(result.fullPersistentAcceptance,false);
});
for(const [name,edit]of [
 ['failed child',v=>v.passed=false],
 ['source drift',v=>v.sourceChanged=['native/decky_sidebar_runtime.h']],
 ['expanded native budget',v=>v.budgets.nativeMs=16000],
 ['suppressed remaining gap',v=>v.excludedGaps.pop()],
 ['outer timeout',v=>v.elapsedMs=135001],
 ['native timeout',v=>v.executions[1].error='ETIMEDOUT'],
 ['nonzero native exit',v=>v.executions[1].exitCode=1],
 ['duplicate source replaces required membership',v=>v.sourceInputs[1]=v.sourceInputs[0]],
])test(name+' is rejected',()=>{const value=clone();edit(value);assert.throws(()=>run(value));});
test('undeclared source and tool identity cannot enter parent scope',()=>{
 const sourceOptions=options();sourceOptions.sourcePins.delete(original.sourceInputs[0].path);assert.throws(()=>run(clone(),sourceOptions),/SOURCE_NOT_PREBOUND/);
 const toolOptions=options();toolOptions.toolPins.delete(path.resolve(original.toolArtifacts[0].path).toLowerCase());assert.throws(()=>run(clone(),toolOptions),/TOOL_NOT_PREBOUND/);
});
test('raw ownership data remains bound to exact original evidence bytes',()=>{
 const value=clone();value.raw.sha256='0'.repeat(64);assert.throws(()=>run(value),/FILE_CHANGED/);
});
test('ordinal mapping cannot omit or reorder an original assertion',()=>{
 const out=path.join(core,'Evidence/DeckyTypedLegacyRuntime-20261023/parent-receipt-controls');fs.mkdirSync(out,{recursive:true});
 const mapping=JSON.parse(fs.readFileSync(original.mapping.path));mapping.reverse();
 const file=path.join(out,'reordered-'+crypto.randomUUID()+'.json');fs.writeFileSync(file,JSON.stringify(mapping));
 const value=clone();value.mapping={path:file,sha256:sha(file),bytes:fs.statSync(file).size};assert.throws(()=>run(value),/ORDERED_MAPPING/);
});
test('historical source view cannot promote changed live sources',()=>{
 const changed=original.sourceInputs.filter(row=>!fs.existsSync(path.join(repo,row.path))||sha(path.join(repo,row.path))!==row.sha256);
 if(changed.length)assert.throws(()=>run(clone(),{...options(),repo}),/CURRENT_SOURCE_CHANGED/);
 else assert.equal(run(clone(),{...options(),repo}).fullPersistentAcceptance,false);
});
