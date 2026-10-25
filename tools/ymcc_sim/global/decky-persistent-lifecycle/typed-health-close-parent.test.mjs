/** Captured receipt shape/refusal controls; source-current acceptance is a
 * separate fresh native execution and disk verification in the parent. */
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {validateHealthCloseParentEnvelope,validateTypedHealthCloseParentReceipt} from './typed-health-close-parent.mjs';
import {parentDeclaration} from './current-parent-global.mjs';
const repo=path.resolve(import.meta.dirname,'../../../..'),core=path.resolve(repo,'../../../Testing/YMCC');
const fixture=path.join(core,'Evidence/DeckyTypedLegacyHealthClose-20261023/h1-de69/r5/result.json');
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
assert.equal(sha(fixture),'3042f7bcecd1ee3ff3776f2571fe9b046046d56ea240e9c97bdd78d33d24135e','SEALED_HEALTH_RECEIPT_CHANGED');
const original=JSON.parse(fs.readFileSync(fixture,'utf8')),clone=()=>structuredClone(original);
test('recorded health11 plus11/7 shape remains a captured-source child, never full parent acceptance',()=>{
 assert.equal(validateHealthCloseParentEnvelope(original),original);assert.equal(original.fullPersistentAcceptance,false);
 assert(original.excludedGaps.includes('AsyncRequest'));assert(original.excludedGaps.includes('installed Steam registry identity'));
});
for(const[name,edit]of [
 ['child failure',v=>v.passed=false],['missing original assertion',v=>v.summary.passedOriginalAssertions=10],
 ['missing actual negative',v=>v.summary.negativeCases=6],['missing actual consumer case',v=>v.summary.additionalCases=10],
 ['full acceptance promotion',v=>v.fullPersistentAcceptance=true],['installed identity promotion',v=>v.summary.installedSteamIdentityContract=true],
 ['hidden legacy regression result',v=>v.legacyRegression.status='PASS'],['removed AsyncRequest gap',v=>v.excludedGaps.shift()],
 ['changed current source',v=>v.sourceChanged=['native/main.cpp']],['changed tools',v=>v.toolsChanged=['cl.exe']],
 ['increased native budget',v=>v.budgets.nativeMs=16000],['increased outer budget',v=>v.budgets.outerTypedGateMs=135001],
 ['elapsed outer deadline',v=>v.elapsedMs=135001],['native timeout',v=>v.executions[1].error='ETIMEDOUT'],
 ['native crash',v=>v.executions[1].exitCode=3221225477],['missing compile stage',v=>v.executions.shift()],
])test(name+' is rejected',()=>{const value=clone();edit(value);assert.throws(()=>validateHealthCloseParentEnvelope(value));});
test('unbound source refuses before consulting current head or any previous PASS',()=>{
 assert.throws(()=>validateTypedHealthCloseParentReceipt(clone(),{repo,evidenceRoot:path.join(core,'Evidence'),workRoot:path.join(core,'Workspace'),sourcePins:new Map(),toolPins:new Map()}),/SOURCE_NOT_PREBOUND/);
});
test('formal declaration binds the current child, verifier and original independent legacy source',()=>{
 const declaration=parentDeclaration(repo);
 for(const file of ['typed-health-close-parent.mjs','typed-legacy-health-close-regression.mjs','typed-legacy-health-close-regression.cpp','typed-legacy-health-close-regression.test.mjs'])
  assert(declaration.evidenceInputs.includes('tools/ymcc_sim/global/decky-persistent-lifecycle/'+file));
 assert(declaration.evidenceInputs.includes('tools/decky_sidebar_health_close_selftest.cpp'));
 assert.equal(declaration.timeoutMs,480000);assert(declaration.artifactInputs.length<=3072);
});