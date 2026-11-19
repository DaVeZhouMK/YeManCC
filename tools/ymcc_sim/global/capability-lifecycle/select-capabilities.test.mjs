/** Finite selector controls only. Registration getters are read-only; adapter
 * runners and product consumers are never called. Inert source copies and
 * control evidence remain in the G project core Evidence directory.
 */
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import assert from 'node:assert/strict';
import test from 'node:test';
import {simulationCorePath} from '../release-host-environment.mjs';
import {getAdapters} from '../adapters.mjs';
import {getExtraAdapters} from '../extra-adapters.mjs';
import {CapabilitySelectionError, SUPPORTED_CAPABILITIES, parseCapabilities,
  assertCapabilityMode, selectCapabilities} from './select-capabilities.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const data = JSON.parse(fs.readFileSync(path.join(repoRoot,
  'tools/ymcc_sim/global/capability-lifecycle/ownership-catalog.json'), 'utf8'));
const registrationCache = new Map();
function registrationsFor(capabilities = ['fan']) {
  const ids = [...new Set([...data.features.filter(feature => capabilities.includes(feature.id)).flatMap(feature => feature.testReferences),
    'controller.ui-navigation'])].sort();
  const key = ids.join(',');
  if (!registrationCache.has(key)) {
    registrationCache.set(key, [...getAdapters(repoRoot, {selectedIds: ids}), ...getExtraAdapters(repoRoot, {selectedIds: ids})]);
  }
  return registrationCache.get(key);
}
const choose = ({adapters = registrationsFor(), ...options} = {}) =>
  selectCapabilities({repoRoot, adapters, capabilities: 'fan', mode: 'plan', ...options});
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
const prefix = 'ymcc-capability-selection-';
let fixture, referenceVerified = false, retain = false;
function directDirectory(dir) {
  const stat = fs.lstatSync(dir, {bigint: true});
  assert(stat.isDirectory() && !stat.isSymbolicLink() && path.relative(dir, fs.realpathSync.native(dir)) === '', 'Redirected directory: ' + dir);
  return stat;
}
function sourceFixture() {
  if (fixture) {
    assert(referenceVerified, 'The original source fixture was not verified or could not be restored');
    return fixture.root;
  }
  const parent = simulationCorePath('Workspace'), parentIdentity = directDirectory(parent);
  const root = fs.mkdtempSync(path.join(parent, prefix));
  try {
    fixture = {root, parent, parentIdentity, identity: directDirectory(root)};
    const feature = data.features.find(row => row.id === 'fan');
    for (const relative of new Set(feature.bindingIds.map(id => data.bindings[id].path))) {
      const destination = path.join(root, relative);
      fs.mkdirSync(path.dirname(destination), {recursive: true});
      fs.copyFileSync(path.join(repoRoot, relative), destination, fs.constants.COPYFILE_EXCL);
    }
    assert.equal(choose({repoRoot: root}).status, 'SELECTION_ONLY_NOT_EXECUTED');
    referenceVerified = true;
    return root;
  } catch (error) {
    retain = true;
    if (!fixture) console.error('Retained selector fixture: ' + root);
    throw error;
  }
}
// Top-level tests run serially. Every mutation is undone before another case
// may use the full, unmodified reference tree; the catalog always reads real files.
function withSourceFixture(relative, run) {
  const root = sourceFixture(), file = path.join(root, relative);
  const prior = fs.readFileSync(file), renamed = file + '.missing-source-control';
  assert(!fs.existsSync(renamed));
  try { run(root, file); }
  catch (error) { retain = true; throw error; }
  finally {
    try {
      if (fs.existsSync(renamed)) fs.renameSync(renamed, file);
      else if (!fs.readFileSync(file).equals(prior)) fs.writeFileSync(file, prior);
      assert(fs.readFileSync(file).equals(prior), 'Mutation was not restored: ' + file);
    } catch (error) { referenceVerified = false; retain = true; throw error; }
  }
}
test.after(() => {
  if (!fixture) return;
  const {root, parent, parentIdentity, identity} = fixture;
  if (retain) { console.error('Retained selector fixture: ' + root); return; }
  assert.equal(path.relative(parent, path.dirname(root)), '');
  assert(path.basename(root).startsWith(prefix));
  for (const [dir, owned] of [[parent, parentIdentity], [root, identity]]) {
    const current = directDirectory(dir);
    assert(current.dev === owned.dev && current.ino === owned.ino, 'Fixture directory identity changed: ' + dir);
  }
  fs.rmSync(root, {recursive: true});
});

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
    rejects('CAPABILITY_INVALID_ARGUMENT', () => choose({adapters: null, capabilities: value}));
  });
}

for (const mode of ['audit', 'replay', 'compare', 'corpus', 'replay-plan', 'log-check', 'signal-check', 'unregistered']) {
  test('capability refuses unsupported mode ' + mode, () => {
    rejects('CAPABILITY_UNSUPPORTED_MODE', () => choose({adapters: null, mode}));
    rejects('CAPABILITY_UNSUPPORTED_MODE', () => assertCapabilityMode(mode));
  });
}

for (const mode of ['inventory', 'plan', 'run']) {
  test(mode + ' selects only the exact existing test and performs zero business execution', () => {
    const adapters = registrationsFor();
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
  const adapters = registrationsFor(SUPPORTED_CAPABILITIES);
  const result = choose({adapters, capabilities: SUPPORTED_CAPABILITIES});
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
  const capabilities = ['oem.back-buttons', 'oem.dedicated-buttons'];
  const result = choose({adapters: registrationsFor(capabilities), capabilities,
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
  const adapters = registrationsFor();
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
  const adapters = registrationsFor();
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
  const adapters = registrationsFor();
  rejects('CAPABILITY_INVALID_REGISTRATIONS', () => choose({adapters: [...adapters, adapters[0]]}));
  rejects('CAPABILITY_INVALID_REGISTRATIONS', () => choose({adapters: [{id: 'fan;whoami'}]}));
});

test('actual bound source drift refuses even when the registered ID is unchanged', () => {
  withSourceFixture('native/fan_resume_owner.h', (root, file) => {
    fs.appendFileSync(file, '\n// selector source-drift control\n');
    rejects('CAPABILITY_STALE', () => choose({repoRoot: root}), error => {
      const feature = error.selection.ownershipPlan.features[0];
      assert.equal(feature.persistence, 'unknown');
      assert.deepEqual(feature.registeredTestIds, []);
      assert.ok(feature.gaps.some(gap => gap.startsWith('SOURCE_BINDING_STALE:')));
    });
  });
});

test('enabled-scope drift refuses rather than trusting an old capability name', () => {
  withSourceFixture('capabilities/ACTIVE-INTEGRATION-SCOPE.json', (root, file) => {
    const scope = JSON.parse(fs.readFileSync(file, 'utf8'));
    scope.confirmedEnabledCapabilities = scope.confirmedEnabledCapabilities.filter(row => row.id !== 'fan');
    fs.writeFileSync(file, JSON.stringify(scope));
    rejects('CAPABILITY_STALE', () => choose({repoRoot: root}));
  });
});

test('missing source binding refuses; caller cannot replace the ownership report', () => {
  withSourceFixture('native/fan_resume_owner.h', (root, file) => {
    assert.ok(path.relative(root, file) === path.join('native', 'fan_resume_owner.h'));
    fs.renameSync(file, file + '.missing-source-control');
    rejects('CAPABILITY_STALE', () => choose({repoRoot: root,
      ownershipPlan: {features: [{id: 'fan', status: 'CLASSIFIED_SOURCE_BOUND'}]}}));
  });
});
