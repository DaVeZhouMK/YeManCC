/** Parent gate for a fresh typed health-close run. Legacy, installed Steam
 * identity and browser rendering remain separate acceptance requirements. */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {originalAssertions, validate} from './typed-legacy-health-close-regression.mjs';
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const digest = file => hash(fs.readFileSync(file));
const key = file => path.resolve(file).toLowerCase();
const read = file => JSON.parse(fs.readFileSync(file,'utf8'));
const inside = (root,file) => key(file).startsWith(key(root)+path.sep);

/** Structural validation only; this function grants no current-source PASS. */
export function validateHealthCloseParentEnvelope(value) {
  assert.equal(value.schemaVersion,1); assert.equal(value.id,'sidecar.decky-typed-legacy-health-close');
  assert.equal(value.status,'PASS'); assert.equal(value.passed,true); assert.equal(value.fullPersistentAcceptance,false);
  assert.deepEqual(value.sourceChanged,[]); assert.deepEqual(value.toolsChanged,[]);
  assert.deepEqual(value.excludedGaps,['AsyncRequest','installed Steam registry identity','real browser DOM rendering','formal parent integration']);
  assert.equal(value.legacyRegression.path,'tools/decky_sidebar_health_close_selftest.cpp');
  assert.equal(value.legacyRegression.status,'NOT_RUN'); assert.equal(value.legacyRegression.historicalResultsReclassified,false);
  assert.equal(value.budgets.compileMs,60000); assert.equal(value.budgets.nativeMs,15000); assert.equal(value.budgets.outerTypedGateMs,135000);
  assert.equal(value.maxArtifactInputs,3072); assert(Number.isSafeInteger(value.elapsedMs)&&value.elapsedMs>=0&&value.elapsedMs<=135000);
  assert.equal(value.summary.orderedOriginalAssertions,11); assert.equal(value.summary.passedOriginalAssertions,11);
  assert.equal(value.summary.additionalCases,11); assert.equal(value.summary.negativeCases,7);
  assert.equal(value.summary.fullPersistentAcceptance,false); assert.equal(value.summary.installedSteamIdentityContract,false);
  assert.deepEqual(value.executions.map(row=>row.id),['compile','native']);
  for(const [index,row]of value.executions.entries()) {
    const limit=index===0?60000:15000;
    assert.equal(row.exitCode,0); assert.equal(row.signal,null); assert.equal(row.error,null);
    assert.equal(row.originalBudgetMs,limit); assert(row.timeoutMs>0&&row.timeoutMs<=limit);
    assert(row.elapsedMs>=0&&row.elapsedMs<=row.timeoutMs,'TYPED_HEALTH_PROCESS_DEADLINE');
  }
  return value;
}

export function validateTypedHealthCloseParentReceipt(value,{repo,evidenceRoot,workRoot,sourcePins,toolPins}) {
  validateHealthCloseParentEnvelope(value);
  const verify=(row,root)=>{assert(row&&/^[a-f0-9]{64}$/.test(row.sha256),'TYPED_HEALTH_FILE_PIN');
    if(root)assert(inside(root,row.path)&&key(fs.realpathSync(row.path))===key(row.path),'TYPED_HEALTH_OWNED_PATH');
    assert.equal(digest(row.path),row.sha256,'TYPED_HEALTH_FILE_CHANGED:'+row.path);return row;};
  const compilePins=new Map(),seen=new Set();
  for(const row of value.sourceInputs) {
    assert(!seen.has(row.path),'TYPED_HEALTH_DUPLICATE_SOURCE');seen.add(row.path);
    assert.equal(sourcePins.get(row.path),row.sha256,'TYPED_HEALTH_SOURCE_NOT_PREBOUND:'+row.path);
    assert.equal(digest(path.join(repo,row.path)),row.sha256,'TYPED_HEALTH_CURRENT_SOURCE_CHANGED:'+row.path);
    const frozen=path.join(workRoot,'s',row.path);
    if(fs.existsSync(frozen)){verify({path:frozen,sha256:row.sha256},workRoot);compilePins.set(key(frozen),row);}
  }
  for(const file of ['typed-legacy-health-close-regression.mjs','typed-legacy-health-close-regression.cpp','typed-legacy-health-close-regression.test.mjs'])
    assert(seen.has('tools/ymcc_sim/global/decky-persistent-lifecycle/'+file),'TYPED_HEALTH_REQUIRED_SOURCE');
  assert(seen.has('native/main.cpp'),'TYPED_HEALTH_MAIN_SOURCE_REQUIRED');
  const toolSeen=new Set();assert(value.toolArtifacts.length>0&&value.toolArtifacts.length<=3072);
  for(const row of value.toolArtifacts){assert(!toolSeen.has(key(row.path)),'TYPED_HEALTH_DUPLICATE_TOOL');toolSeen.add(key(row.path));
    assert.equal(toolPins.get(key(row.path))?.sha256,row.sha256,'TYPED_HEALTH_TOOL_NOT_PREBOUND');verify(row);compilePins.set(key(row.path),row);}
  assert.equal(value.generated.length,5,'TYPED_HEALTH_EXTRACTION_MEMBERSHIP');
  for(const row of value.generated){assert.equal(row.bytePreserved,true);assert.equal(sourcePins.get(row.origin),row.originSha256,'TYPED_HEALTH_GENERATED_ORIGIN');
    const source=fs.readFileSync(path.join(repo,row.origin),'utf8');
    const extract=slice=>{assert(Number.isSafeInteger(slice.begin)&&Number.isSafeInteger(slice.end)&&slice.begin>=0&&slice.end>slice.begin&&slice.end<=source.length,'TYPED_HEALTH_SLICE_RANGE');return source.slice(slice.begin,slice.end);};
    const expected=Array.isArray(row.slice)?row.slice.map(extract).join('\n\n')+'\n':extract(row.slice)+(row.slice.appendNewline?'\n':'');
    assert.equal(hash(Buffer.from(expected)),row.sha256,'TYPED_HEALTH_EXTRACTED_BYTES_CHANGED');verify(row,workRoot);compilePins.set(key(row.path),row);}
  for(const [index,row]of value.executions.entries()){verify(row.program,index===0?undefined:workRoot);verify(row.stdout,evidenceRoot);verify(row.stderr,evidenceRoot);
    if(index===0)assert.equal(toolPins.get(key(row.program.path))?.sha256,row.program.sha256,'TYPED_HEALTH_COMPILER_NOT_PREBOUND');}
  // Current typed health fixture must execute the GUI image actually compiled.
  assert(value.executions[0].args.includes('/SUBSYSTEM:WINDOWS')&&value.executions[0].args.includes('/ENTRY:wmainCRTStartup'),'TYPED_HEALTH_GUI_COMPILE');
  verify(value.currentNativeImage,workRoot);assert.equal(value.currentNativeImage.path,value.executions[1].program.path);assert.equal(value.currentNativeImage.sha256,value.executions[1].program.sha256);
  const peImage=fs.readFileSync(value.currentNativeImage.path),pe=peImage.readUInt32LE(0x3c);assert.equal(peImage.readUInt32LE(pe),0x00004550);assert.equal(peImage.readUInt16LE(pe+4),0x8664);assert.equal(peImage.readUInt16LE(pe+24),0x20b);assert.equal(peImage.readUInt16LE(pe+24+68),2,'TYPED_HEALTH_ACTUAL_GUI_PE');
  verify(value.compilerGraph,workRoot);const graph=read(value.compilerGraph.path);assert.equal(graph.Version,'1.2');
  for(const file of [graph.Data.Source,...graph.Data.Includes]){const row=compilePins.get(key(file));assert(row,'TYPED_HEALTH_UNDECLARED_COMPILER_INPUT:'+file);assert.equal(digest(file),row.sha256,'TYPED_HEALTH_COMPILER_INPUT_CHANGED');}
  for(const row of value.nativeArtifacts)verify(row,workRoot);verify(value.raw,evidenceRoot);verify(value.mapping,evidenceRoot);
  const expected=originalAssertions(fs.readFileSync(path.join(repo,'tools/decky_sidebar_health_close_selftest.cpp'),'utf8'));
  const summary=validate(read(value.raw.path),expected);assert.deepEqual(value.summary,summary);
  const mapping=read(value.mapping.path);assert.equal(mapping.length,expected.length);
  mapping.forEach((row,index)=>{for(const field of ['ordinal','label','legacyCall','legacyLine','caseId'])assert.deepEqual(row[field],expected[index][field],'TYPED_HEALTH_ORDERED_MAPPING');assert.equal(row.status,'PASS');assert.deepEqual(row.raw,value.raw);assert.equal(row.literalLegacyCallbacksExecuted,false);});
  return {status:'CURRENT_TYPED_HEALTH_CLOSE_INVARIANTS_EXECUTED',summary,raw:value.raw,mapping:value.mapping,
    identityBoundary:'Task-owned PID/birth/session and loopback CDP; installed registry/browser contract remains independent',legacyOriginalRegressionStillRequired:true,fullPersistentAcceptance:false};
}