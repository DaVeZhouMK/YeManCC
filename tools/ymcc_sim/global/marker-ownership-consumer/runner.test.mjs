import test from'node:test';import assert from'node:assert/strict';import fs from'node:fs';import path from'node:path';import{getMarkerOwnershipConsumerAdapters,REPO,MODULE}from'./adapter.mjs';
import {verifySharedLeafPins} from '../sleep-native-recovery-consumer/shared-leaf-successor.mjs';
test('actual native helper candidate declares all immutable inputs and memory fault terminals',()=>{const[a]=getMarkerOwnershipConsumerAdapters();assert.equal(a.id,'sleep.marker-ownership-consumer');assert(a.artifactInputs.length<640);for(const p of a.evidenceInputs)assert(fs.statSync(path.join(REPO,p)).isFile(),p);const spec=JSON.parse(fs.readFileSync(path.join(REPO,MODULE,'spec.json'),'utf8'));assert.equal(spec.reviewedFunctions.length,11);assert.equal(spec.fixture.positiveGroups,32);assert.equal(spec.atomicMemoryPorts.length,10);assert.equal(spec.faults.length,4);const source=fs.readFileSync(path.join(REPO,'native/main.cpp'),'utf8').replace(/\r\n/g,'\n');for(const f of spec.faults)assert.equal(source.split(f.before.replace(/\r\n/g,'\n')).length,2,f.label);});

const unrelatedScenarios = new Set([
  'tools/sleep_game_pause_selftest.py',
  'tools/sleep_non_user_wake_selftest.py',
]);
const readSpec = () => JSON.parse(fs.readFileSync(path.join(REPO, MODULE, 'spec.json'), 'utf8'));
const readSharedLeaf = relative => {
  assert(!unrelatedScenarios.has(relative), `unrelated Python scenario read: ${relative}`);
  return fs.readFileSync(path.join(REPO, relative));
};

test('marker ownership snapshots and verifies native inputs without unrelated Python scenarios', () => {
  const [adapter] = getMarkerOwnershipConsumerAdapters();
  const spec = readSpec();
  const reads = new Set();
  const review = verifySharedLeafPins(spec, relative => {
    reads.add(relative);
    return readSharedLeaf(relative);
  }, 'MARKER_OWNERSHIP');
  assert.equal(review.receipt().status, 'PASS');
  for (const relative of unrelatedScenarios) {
    assert(!adapter.evidenceInputs.includes(relative), relative);
    assert(!spec.sharedLeaves.includes(relative), relative);
    assert(!review.pins.has(relative), relative);
  }
  for (const relative of review.pins.keys()) {
    assert(reads.has(relative), relative);
    assert(adapter.evidenceInputs.includes(relative), relative);
  }
});

test('marker ownership still refuses changed bytes in its consumed compiler module', () => {
  const target = 'tools/ymcc_sim/global/sleep-non-user-wake-consumer/compiler-leaf.mjs';
  let changed = false;
  assert.throws(() => verifySharedLeafPins(readSpec(), relative => {
    const bytes = readSharedLeaf(relative);
    if (relative !== target) return bytes;
    changed = true;
    return Buffer.concat([bytes, Buffer.from('\n// in-memory pin control\n')]);
  }, 'MARKER_OWNERSHIP'), error => error.code === 'ERR_ASSERTION'
    && error.message.split('\n', 1)[0] === 'MARKER_OWNERSHIP_SHARED_LEAF_CHANGED:' + target);
  assert(changed);
});
