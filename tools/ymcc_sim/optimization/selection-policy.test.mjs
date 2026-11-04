import test from 'node:test';
import assert from 'node:assert/strict';
import { MAX_MODULE_MINIMUM_IDS, validateSelectionPolicy } from './selection-policy.mjs';

// Small in-memory samples of the existing row/config shapes; no registry loading.
function fixture() {
  const identityOnly = [
    { id: 'controller.persona', domain: 'controller', registrationIdentityOnly: true },
    { id: 'controller.virtual-output-status', domain: 'controller', registrationIdentityOnly: true },
    { id: 'fan.curve', domain: 'fan', registrationIdentityOnly: true },
    { id: 'settings.cas', domain: 'settings', registrationIdentityOnly: true },
  ];
  const extraIndex = { groups: [{ rows: [
    { id: 'sensor.extended-metrics', domain: 'sensor', profiles: ['desktop', 'full'], lifecycleDeclared: false },
    { id: 'plugins.decky-owned-runtime-lifecycle', domain: 'plugins', profiles: ['desktop', 'full'], lifecycleDeclared: true },
  ] }] };
  return {
    registrations: [...identityOnly, ...extraIndex.groups.flatMap(group => group.rows)],
    config: {
      schemaVersion: 1,
      meaning: 'Reuse existing registered test IDs; minimum is partial coverage',
      aliases: { gamepad: 'controller', input: 'controller' },
      modules: {
        controller: { minimum: ['controller.persona', 'controller.virtual-output-status'], description: 'Input model' },
        fan: { minimum: ['fan.curve'], description: 'Software curve' },
        monitor: { minimum: ['sensor.extended-metrics'], domains: ['sensor'] },
        decky: { minimum: ['plugins.decky-owned-runtime-lifecycle'], domains: ['plugins'] },
      },
    },
  };
}

function freeze(value) {
  if (value && typeof value === 'object') {
    for (const child of Object.values(value)) freeze(child);
    Object.freeze(value);
  }
  return value;
}

function rejects(registrations, config, code) {
  assert.throws(() => validateSelectionPolicy(registrations, config), error =>
    error instanceof TypeError && error.code === code && error.message.startsWith(`${code}:`));
}

test('current configuration style accepts combined original rows and returns actual counts without execution', () => {
  const { registrations, config } = fixture();
  const mustNotExecute = () => assert.fail('selection validation invoked a consumer');
  registrations[0].execute = mustNotExecute;
  registrations[0].factory = mustNotExecute;
  freeze(registrations);
  freeze(config);
  assert.equal(MAX_MODULE_MINIMUM_IDS, 2);
  assert.deepEqual(validateSelectionPolicy(registrations, config), {
    schemaVersion: 1,
    maxModuleMinimumIds: 2,
    knownCount: 6,
    moduleCount: 4,
    aliasCount: 2,
    minimumReferenceCount: 5,
    uniqueMinimumIdCount: 5,
    sharedMinimumReferenceCount: 0,
    moduleMinimumCounts: { controller: 2, fan: 1, monitor: 1, decky: 1 },
    noTestsExecuted: true,
    fullProductAcceptance: false,
  });
});

test('global duplicates reject even the same registration object repeated', () => {
  const { registrations, config } = fixture();
  registrations.push(registrations[0]);
  rejects(registrations, config, 'DUPLICATE_REGISTERED_ID');
});

test('duplicates between identityOnly and extra rows reject even for unselected IDs', () => {
  const { registrations, config } = fixture();
  registrations.push({ id: 'settings.cas', domain: 'different', profiles: ['full'] });
  rejects(registrations, config, 'DUPLICATE_REGISTERED_ID');
});

test('global uniqueness is checked even when the module configuration is empty', () => {
  rejects([{ id: 'original.id' }, { id: 'original.id' }],
    { schemaVersion: 1, modules: {} }, 'DUPLICATE_REGISTERED_ID');
});

test('a duplicate inside a module minimum rejects', () => {
  const { registrations, config } = fixture();
  config.modules.controller.minimum = ['controller.persona', 'controller.persona'];
  rejects(registrations, config, 'DUPLICATE_MODULE_MINIMUM_ID');
});

test('an unknown original ID rejects without normalization or domain substitution', () => {
  const { registrations, config } = fixture();
  for (const id of ['missing.original', 'controller', ' controller.persona ']) {
    config.modules.controller.minimum = [id];
    rejects(registrations, config, 'UNKNOWN_MODULE_MINIMUM_ID');
  }
});

test('three registered IDs exceed the module minimum hard limit', () => {
  const { registrations, config } = fixture();
  config.modules.controller.minimum.push('fan.curve');
  rejects(registrations, config, 'MODULE_MINIMUM_ID_COUNT');
});

test('zero IDs cannot be a module minimum', () => {
  const { registrations, config } = fixture();
  config.modules.fan.minimum = [];
  rejects(registrations, config, 'MODULE_MINIMUM_ID_COUNT');
});

test('modules may share original IDs while reference and distinct counts remain separate', () => {
  const registrations = [{ id: 'lifecycle.ui-power-transport' }, { id: 'lifecycle.ui-power-recovery' }];
  const minimum = registrations.map(row => row.id);
  const config = freeze({ schemaVersion: 1, modules: {
    sleep: { minimum }, lifecycle: { minimum },
  } });
  const result = validateSelectionPolicy(freeze(registrations), config);
  assert.equal(result.knownCount, 2);
  assert.equal(result.moduleCount, 2);
  assert.equal(result.aliasCount, 0);
  assert.equal(result.minimumReferenceCount, 4);
  assert.equal(result.uniqueMinimumIdCount, 2);
  assert.equal(result.sharedMinimumReferenceCount, 2);
  assert.deepEqual(result.moduleMinimumCounts, { sleep: 2, lifecycle: 2 });
});

test('aliases must directly target own module entries, never missing names or alias chains', () => {
  for (const target of ['missing', 'gamepad', 'toString', '__proto__', null, 2, ['controller']]) {
    const { registrations, config } = fixture();
    config.aliases.input = target;
    rejects(registrations, config, 'UNKNOWN_MODULE_ALIAS_TARGET');
  }
});

test('configuration fields cannot raise the fixed two-ID ceiling', () => {
  const { registrations, config } = fixture();
  config.maxModuleMinimumIds = 240;
  config.MAX_MODULE_MINIMUM_IDS = 240;
  config.limits = { maxModuleMinimumIds: 240 };
  config.modules.controller.maxModuleMinimumIds = 240;
  config.modules.controller.maximum = 240;
  assert.equal(validateSelectionPolicy(registrations, config).maxModuleMinimumIds, 2);
  config.modules.controller.minimum.push('fan.curve');
  rejects(registrations, config, 'MODULE_MINIMUM_ID_COUNT');
});

test('invalid registration containers and ID rows reject instead of becoming registry keys', () => {
  const { config } = fixture();
  for (const registrations of [null, {}, new Map(), 'original.id']) {
    rejects(registrations, config, 'INVALID_SELECTION_REGISTRATIONS');
  }
  for (const row of [null, [], {}, { id: '' }, { id: ' ' }, { id: 1 }]) {
    rejects([row], config, 'INVALID_SELECTION_REGISTRATION');
  }
});

test('invalid existing schema, module, minimum and alias shapes reject', () => {
  const { registrations } = fixture();
  for (const config of [null, [], {}, { schemaVersion: 2, modules: {} }]) {
    rejects(registrations, config, 'INVALID_SELECTION_POLICY_SCHEMA');
  }
  for (const modules of [null, [], 'controller']) {
    rejects(registrations, { schemaVersion: 1, modules }, 'INVALID_SELECTION_MODULES');
  }
  for (const module of [null, [], {}, { minimum: 'fan.curve' }]) {
    rejects(registrations, { schemaVersion: 1, modules: { fan: module } }, 'INVALID_MODULE_MINIMUM');
  }
  for (const id of [null, '', ' ', 1]) {
    rejects(registrations, { schemaVersion: 1, modules: { fan: { minimum: [id] } } }, 'INVALID_MODULE_MINIMUM_ID');
  }
  for (const aliases of [null, [], 'controller']) {
    rejects(registrations, { schemaVersion: 1, modules: {}, aliases }, 'INVALID_MODULE_ALIASES');
  }
});
