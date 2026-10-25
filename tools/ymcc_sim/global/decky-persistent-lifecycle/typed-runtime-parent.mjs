/** Strict parent consumption of a freshly executed current typed runtime child.
 * Historical legacy regressions remain independent and must still execute.
 */
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import { originalAssertions, validateRuntimeResult } from './typed-legacy-runtime-regression.mjs';
const digest = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key = file => path.resolve(file).toLowerCase();
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const inside = (root, file) => key(file).startsWith(key(root) + path.sep);

export function validateTypedRuntimeParentReceipt(value, {repo, evidenceRoot, workRoot, sourcePins, toolPins}) {
  assert.equal(value.schemaVersion, 1); assert.equal(value.id, 'sidecar.decky-typed-legacy-runtime-regression');
  assert.equal(value.status, 'PASS'); assert.equal(value.passed, true); assert.equal(value.fullPersistentAcceptance, false);
  assert.deepEqual(value.sourceChanged, []); assert.deepEqual(value.toolsChanged, []);
  assert.deepEqual(value.excludedGaps, ['health-close','AsyncRequest']);
  assert.equal(value.budgets.compileMs, 60000); assert.equal(value.budgets.nativeMs, 15000);
  assert.equal(value.budgets.outerTypedGateMs, 135000); assert.equal(value.maxArtifactInputs, 3072);
  assert.equal(value.installedApplicationsStarted, false); assert.equal(value.physicalSleepPerformed, false);
  assert.equal(value.realSteamExecuted, false); assert.equal(value.officialLoaderExecuted, false);
  assert(Number.isSafeInteger(value.elapsedMs) && value.elapsedMs >= 0 && value.elapsedMs <= 135000, 'TYPED_RUNTIME_OUTER_DEADLINE');
  const verify = (row, root) => {
    assert(row && /^[a-f0-9]{64}$/.test(row.sha256), 'TYPED_RUNTIME_FILE_PIN');
    if (root) assert(inside(root, row.path) && key(fs.realpathSync(row.path)) === key(row.path), 'TYPED_RUNTIME_OWNED_PATH');
    assert.equal(digest(row.path), row.sha256, 'TYPED_RUNTIME_FILE_CHANGED:' + row.path);
    return row;
  };
  const compilePins = new Map(), seen = new Set();
  for (const row of value.sourceInputs) {
    assert(!seen.has(row.path), 'TYPED_RUNTIME_DUPLICATE_SOURCE'); seen.add(row.path);
    assert.equal(sourcePins.get(row.path), row.sha256, 'TYPED_RUNTIME_SOURCE_NOT_PREBOUND:' + row.path);
    assert.equal(digest(path.join(repo, row.path)), row.sha256, 'TYPED_RUNTIME_CURRENT_SOURCE_CHANGED');
    const frozen = path.join(workRoot, 's', row.path);
    if (fs.existsSync(frozen)) { verify({path:frozen,sha256:row.sha256}, workRoot); compilePins.set(key(frozen), row); }
  }
  for (const name of ['typed-legacy-runtime-regression.mjs','typed-legacy-runtime-regression.cpp','typed-legacy-runtime-regression.test.mjs']) {
    assert(seen.has('tools/ymcc_sim/global/decky-persistent-lifecycle/' + name), 'TYPED_RUNTIME_REQUIRED_SOURCE');
  }
  const toolSeen = new Set();
  assert(value.toolArtifacts.length > 0 && value.toolArtifacts.length <= 3072, 'TYPED_RUNTIME_TOOL_MEMBERSHIP');
  for (const row of value.toolArtifacts) {
    assert(!toolSeen.has(key(row.path)), 'TYPED_RUNTIME_DUPLICATE_TOOL'); toolSeen.add(key(row.path));
    assert.equal(toolPins.get(key(row.path))?.sha256, row.sha256, 'TYPED_RUNTIME_TOOL_NOT_PREBOUND');
    verify(row); compilePins.set(key(row.path), row);
  }
  for (const row of value.generated) {
    assert.equal(row.bytePreserved, true); assert.equal(sourcePins.get(row.origin), row.originSha256, 'TYPED_RUNTIME_GENERATED_ORIGIN');
    verify(row, workRoot); compilePins.set(key(row.path), row);
  }
  assert.deepEqual(value.executions.map(row => row.id), ['compile','native']);
  for (const [index, row] of value.executions.entries()) {
    const limit = index === 0 ? 60000 : 15000;
    assert.equal(row.exitCode, 0); assert.equal(row.error, null); assert.equal(row.signal, null);
    assert.equal(row.originalBudgetMs, limit); assert(row.timeoutMs > 0 && row.timeoutMs <= limit);
    assert(row.elapsedMs >= 0 && row.elapsedMs <= row.timeoutMs, 'TYPED_RUNTIME_PROCESS_DEADLINE');
    verify(row.program, index === 0 ? undefined : workRoot); verify(row.stdout, evidenceRoot); verify(row.stderr, evidenceRoot);
    if (index === 0) assert.equal(toolPins.get(key(row.program.path))?.sha256, row.program.sha256, 'TYPED_RUNTIME_COMPILER_NOT_PREBOUND');
  }
  verify(value.compilerGraph, workRoot);
  const graph = read(value.compilerGraph.path); assert.equal(graph.Version, '1.2');
  for (const file of [graph.Data.Source, ...graph.Data.Includes]) {
    const row = compilePins.get(key(file)); assert(row, 'TYPED_RUNTIME_UNDECLARED_COMPILER_INPUT:' + file);
    assert.equal(digest(file), row.sha256, 'TYPED_RUNTIME_COMPILER_INPUT_CHANGED');
  }
  for (const row of value.nativeArtifacts) verify(row, workRoot);
  verify(value.raw, evidenceRoot); verify(value.mapping, evidenceRoot);
  const expected = originalAssertions(fs.readFileSync(path.join(repo, 'tools/decky_sidebar_runtime_selftest.cpp'), 'utf8'));
  const raw = read(value.raw.path), summary = validateRuntimeResult(raw, expected);
  assert.deepEqual(value.summary, summary);
  const mapping = read(value.mapping.path); assert.equal(mapping.length, expected.length);
  mapping.forEach((row, i) => {
    for (const field of ['ordinal','label','legacyCall','legacyLine','group']) assert.deepEqual(row[field], expected[i][field], 'TYPED_RUNTIME_ORDERED_MAPPING');
    assert.equal(row.status, 'PASS'); assert.deepEqual(row.raw, value.raw);
    assert.equal(row.currentSourceSha256, sourcePins.get(row.currentSource), 'TYPED_RUNTIME_MAPPING_SOURCE');
  });
  return {status:'CURRENT_TYPED_RUNTIME_INVARIANTS_EXECUTED', summary, raw:value.raw, mapping:value.mapping,
    legacyOriginalRegressionStillRequired:true, fullPersistentAcceptance:false};
}