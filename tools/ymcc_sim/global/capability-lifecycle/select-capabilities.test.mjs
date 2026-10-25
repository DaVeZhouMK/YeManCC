/** Finite selector controls only. Registration getters are read-only; adapter
 * runners and product consumers are never called. Inert source copies and
 * control evidence remain in the G project core Evidence directory.
 */
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
import test from 'node:test';
import {getAdapters} from '../adapters.mjs';
import {getExtraAdapters} from '../extra-adapters.mjs';
import {CapabilitySelectionError, SUPPORTED_CAPABILITIES, parseCapabilities,
  assertCapabilityMode, selectCapabilities} from './select-capabilities.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const data = JSON.parse(fs.readFileSync(path.join(repoRoot,
  'tools/ymcc_sim/global/capability-lifecycle/ownership-catalog.json'), 'utf8'));
const references = [...new Set(data.features.flatMap(feature => feature.testReferences))];
// Real current getters; the public index is not substituted for descriptors.
const adapters = [...getAdapters(repoRoot), ...getExtraAdapters(repoRoot, {selectedIds: references})];
const choose = options => selectCapabilities({repoRoot, adapters, capabilities: 'fan', mode: 'plan', ...options});
function rejects(code, callback, inspect = () => {}) {
  assert.throws(callback, error => {
    assert.ok(error instanceof CapabilitySelectionError);
    assert.equal(error.code, code, error.message);
    if (error.selection) {
      assert.equal(error.selection.status, 'BLOCKED');
      assert.equal(error.selection.executedTests, 0);
      assert.equal(error.selection.businessExecuted, 0);
      assert.equal(error.selection.executionAuthorized, false);
      assert.equal(error.selection.softwareAcceptance, false);
      assert.equal(error.selection.lifecycleAcceptance, 'INCOMPLETE');
    }
    inspect(error);
    return true;
  });
}
function sourceFixture() {
  const evidence = path.resolve(repoRoot, '../../../Testing/YMCC/Evidence/CapabilitySelection-20261023');
  fs.mkdirSync(evidence, {recursive: true});
  const root = fs.mkdtempSync(path.join(evidence, 'source-control-'));
  const feature = data.features.find(row => row.id === 'fan');
  for (const relative of new Set(feature.bindingIds.map(id => data.bindings[id].path))) {
    const destination = path.join(root, relative);
    fs.mkdirSync(path.dirname(destination), {recursive: true});
    fs.copyFileSync(path.join(repoRoot, relative), destination);
  }
  return root;
}

test('absent capability preserves the caller legacy path without reading registration or sources', () => {
  assert.equal(selectCapabilities(), null);
  assert.equal(selectCapabilities({repoRoot: 'not-a-source-root', adapters: null, mode: 'audit'}), null);
  assert.deepEqual(parseCapabilities(undefined), []);
});

test('comma and repeated CLI values form one bounded deterministic selection', () => {
  assert.deepEqual(parseCapabilities([' fan ,motion', 'fan', 'controller.input']), ['fan', 'motion', 'controller.input']);
  rejects('CAPABILITY_TOO_MANY', () => parseCapabilities([...SUPPORTED_CAPABILITIES, 'unknown.owner']));
  rejects('CAPABILITY_INVALID_ARGUMENT', () => parseCapabilities(Array(65).fill('fan')));
});

for (const value of ['', ' ', [], [''], ',', 'fan,', ',fan', 'fan,,motion',
  'fan;whoami', 'fan&whoami', 'fan|whoami', '$(whoami)', 'fan' + String.fromCharCode(96) + 'whoami',
  'fan\n--test controller.ui-navigation', '../fan', 'G:/fan', 'G:\\fan', '/fan',
  '*', 'fan.*', '--test', '@fan', 'FAN', ['fan', 7], null]) {
  test('rejects invalid capability syntax ' + JSON.stringify(value), () => {
    rejects('CAPABILITY_INVALID_ARGUMENT', () => choose({capabilities: value}));
  });
}

for (const mode of ['audit', 'replay', 'compare', 'corpus', 'replay-plan', 'log-check', 'signal-check', 'unregistered']) {
  test('capability refuses unsupported mode ' + mode, () => {
    rejects('CAPABILITY_UNSUPPORTED_MODE', () => choose({mode}));
    rejects('CAPABILITY_UNSUPPORTED_MODE', () => assertCapabilityMode(mode));
  });
}

for (const mode of ['inventory', 'plan', 'run']) {
  test(mode + ' selects only the exact existing test and performs zero business execution', () => {
    const before = structuredClone(adapters);
    const result = choose({mode, tests: ['fan.unified-capability-roundtrip']});
    assert.equal(result.status, 'SELECTION_ONLY_NOT_EXECUTED');
    assert.deepEqual(result.selectedIds, ['fan.unified-capability-roundtrip']);
    assert.equal(result.ownershipPlan.features[0].status, 'CLASSIFIED_SOURCE_BOUND');
    assert.equal(result.ownershipPlan.features[0].persistence, 'persistent');
    assert.equal(result.ownershipPlan.mode, 'plan');
    assert.equal(result.businessExecuted, 0);
    assert.equal(result.automaticExecution, false);
    assert.equal(result.executionAuthorized, false);
    assert.equal(result.hardwareAcceptance, false);
    assert.equal(result.cpuPowerAcceptance, false);
    assert.equal(result.softwareAcceptance, false);
    assert.ok(result.remainingAcceptanceGaps[0].requirements.includes('CATALOG_IS_NOT_EXECUTION_EVIDENCE'));
    assert.ok(result.excludedCandidates.some(row => row.reasons.includes('TEST_FILTER')));
    assert.deepEqual(adapters, before, 'descriptor identities, budgets and contracts must be unchanged');
  });
}

test('all eight enabled capabilities resolve to real current registrations with source-bound ownership and retained gaps', () => {
  const result = choose({capabilities: SUPPORTED_CAPABILITIES});
  assert.equal(result.perCapability.length, 8);
  assert.equal(result.candidateIds.length, new Set(result.candidateIds).size);
  for (const row of result.perCapability) {
    assert.ok(row.candidateIds.length > 0, row.capabilityId);
    assert.ok(row.selectedIds.length > 0, row.capabilityId);
    assert.equal(row.executionAcceptance, false);
    assert.ok(row.candidateIds.every(id => adapters.some(adapter => adapter.id === id)));
  }
  for (const feature of result.ownershipPlan.features) {
    assert.equal(feature.scope, 'confirmed-enabled');
    assert.equal(feature.status, 'CLASSIFIED_SOURCE_BOUND');
    assert.ok(feature.sourceBindings.every(binding => binding.status === 'CURRENT'));
    assert.equal(feature.executionAcceptance, false);
  }
  assert.ok(result.remainingAcceptanceGaps.every(row => row.requirements.length > 0));
});

test('shared OEM registrations are deduplicated without losing parent capability ownership', () => {
  const result = choose({capabilities: ['oem.back-buttons', 'oem.dedicated-buttons'],
    tests: ['controller.unified-oem-input-binding']});
  assert.deepEqual(result.selectedIds, ['controller.unified-oem-input-binding']);
  assert.ok(result.perCapability.every(row => row.selectedIds.length === 1));
});

test('mixed valid and unknown capabilities fail atomically', () => {
  rejects('CAPABILITY_UNKNOWN', () => choose({capabilities: ['fan', 'unclassified.owner']}), error => {
    assert.deepEqual(error.selection.candidateIds, []);
    assert.deepEqual(error.selection.selectedIds, []);
    assert.equal(error.selection.ownershipPlan.features[1].persistence, 'unknown');
  });
});

for (const capability of ['model.secondary-display', 'settings.wake-password-ui', 'controller.experimental-native-rear-keyboard-writer']) {
  test('inactive or excluded capability refuses ' + capability, () => {
    rejects('CAPABILITY_INACTIVE', () => choose({capabilities: capability}));
  });
}

test('zero real registrations and partial missing registration both refuse explicitly', () => {
  const fanIds = new Set(data.features.find(row => row.id === 'fan').testReferences);
  rejects('CAPABILITY_NO_REGISTERED_IDS', () => choose({adapters: adapters.filter(row => !fanIds.has(row.id))}));
  rejects('CAPABILITY_REGISTRATION_INCOMPLETE', () => choose({adapters: adapters.filter(row => row.id !== 'fan.unified-capability-roundtrip')}), error => {
    assert.ok(error.selection.remainingAcceptanceGaps[0].unregisteredTestIds.includes('fan.unified-capability-roundtrip'));
  });
  rejects('CAPABILITY_NO_REGISTERED_IDS', () => choose({adapters: []}));
});

test('explicit registered test outside capability candidates cannot widen selection', () => {
  rejects('CAPABILITY_TEST_OUTSIDE_CANDIDATES', () => choose({tests: ['controller.ui-navigation']}));
  rejects('CAPABILITY_TEST_OUTSIDE_CANDIDATES', () => choose({tests: ['does.not-exist']}));
});

test('profile and domain intersections cannot yield an empty successful selection', () => {
  const domain = adapters.find(row => row.id === 'fan.unified-capability-roundtrip').domain;
  assert.notEqual(domain, 'controller');
  rejects('CAPABILITY_EMPTY_SELECTION', () => choose({domains: ['controller']}), error => {
    assert.ok(error.selection.candidateIds.length > 0);
    assert.deepEqual(error.selection.selectedIds, []);
    assert.ok(error.selection.excludedCandidates.every(row => row.reasons.includes('DOMAIN_FILTER')));
  });
  rejects('CAPABILITY_EMPTY_SELECTION', () => choose({profile: 'sleep', tests: ['fan.unified-capability-roundtrip']}));
  assert.deepEqual(choose({domains: [domain], tests: ['fan.unified-capability-roundtrip']}).selectedIds,
    ['fan.unified-capability-roundtrip']);
});

test('unknown filters, malformed registration and filter injection refuse', () => {
  rejects('CAPABILITY_UNKNOWN_DOMAINS', () => choose({domains: ['unregistered-domain']}));
  rejects('CAPABILITY_INVALID_TESTS', () => choose({tests: ['fan.*']}));
  rejects('CAPABILITY_INVALID_DOMAINS', () => choose({domains: ['fan;whoami']}));
  rejects('CAPABILITY_INVALID_PROFILE', () => choose({profile: 'full;whoami'}));
  rejects('CAPABILITY_INVALID_REGISTRATIONS', () => choose({adapters: [...adapters, adapters[0]]}));
  rejects('CAPABILITY_INVALID_REGISTRATIONS', () => choose({adapters: [{id: 'fan;whoami'}]}));
});

test('actual bound source drift refuses even when the registered ID is unchanged', () => {
  const root = sourceFixture();
  assert.equal(choose({repoRoot: root}).status, 'SELECTION_ONLY_NOT_EXECUTED');
  fs.appendFileSync(path.join(root, 'native/fan_resume_owner.h'), '\n// selector source-drift control\n');
  rejects('CAPABILITY_STALE', () => choose({repoRoot: root}), error => {
    const feature = error.selection.ownershipPlan.features[0];
    assert.equal(feature.persistence, 'unknown');
    assert.deepEqual(feature.registeredTestIds, []);
    assert.ok(feature.gaps.some(gap => gap.startsWith('SOURCE_BINDING_STALE:')));
  });
});

test('enabled-scope drift refuses rather than trusting an old capability name', () => {
  const root = sourceFixture();
  const file = path.join(root, 'capabilities/ACTIVE-INTEGRATION-SCOPE.json');
  const scope = JSON.parse(fs.readFileSync(file, 'utf8'));
  scope.confirmedEnabledCapabilities = scope.confirmedEnabledCapabilities.filter(row => row.id !== 'fan');
  fs.writeFileSync(file, JSON.stringify(scope));
  rejects('CAPABILITY_STALE', () => choose({repoRoot: root}));
});

test('missing source binding refuses; caller cannot replace the ownership report', () => {
  const root = sourceFixture();
  const file = path.join(root, 'native/fan_resume_owner.h');
  // Keep the original fixture bytes as evidence; rename only inside this new,
  // resolved and verified evidence fixture, never the product source tree.
  assert.ok(path.relative(root, file) === path.join('native', 'fan_resume_owner.h'));
  fs.renameSync(file, file + '.missing-source-control');
  rejects('CAPABILITY_STALE', () => choose({repoRoot: root,
    ownershipPlan: {features: [{id: 'fan', status: 'CLASSIFIED_SOURCE_BOUND'}]}}));
});
