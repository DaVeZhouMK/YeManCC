/** Hard policy limit; configuration cannot raise it. */
export const MAX_MODULE_MINIMUM_IDS = 2;

function reject(code, detail) {
  const error = new TypeError(`${code}: ${detail}`);
  error.code = code;
  throw error;
}

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function isId(value) {
  return typeof value === 'string' && value.trim().length > 0;
}

/**
 * Validate the combined identityOnly registrations and extra index rows against
 * the existing modules.json schemaVersion 1. IDs are compared exactly, without
 * normalization. Every registration must be unique, including unselected IDs.
 * Shared minimum IDs across modules are allowed; aliases target actual modules.
 *
 * Returns counts for planning/reporting, never execution or business acceptance.
 * Reads only the supplied data, invokes no consumers and does not mutate inputs.
 * Invalid data throws a TypeError with a stable .code value.
 */
export function validateSelectionPolicy(registrations, config) {
  if (!Array.isArray(registrations)) {
    reject('INVALID_SELECTION_REGISTRATIONS', 'expected registration rows');
  }
  const knownIds = new Set();
  for (const row of registrations) {
    if (!isObject(row) || !isId(row.id)) {
      reject('INVALID_SELECTION_REGISTRATION', 'expected a nonempty id');
    }
    if (knownIds.has(row.id)) reject('DUPLICATE_REGISTERED_ID', row.id);
    knownIds.add(row.id);
  }

  if (!isObject(config) || config.schemaVersion !== 1) {
    reject('INVALID_SELECTION_POLICY_SCHEMA', 'expected schemaVersion 1');
  }
  if (!isObject(config.modules)) {
    reject('INVALID_SELECTION_MODULES', 'expected a modules object');
  }
  const modules = Object.entries(config.modules);
  const minimumIds = new Set();
  const moduleMinimumCounts = [];
  let minimumReferenceCount = 0;
  for (const [name, module] of modules) {
    if (!isObject(module) || !Array.isArray(module.minimum)) {
      reject('INVALID_MODULE_MINIMUM', `${name}: expected a minimum array`);
    }
    const minimum = module.minimum;
    if (minimum.length < 1 || minimum.length > MAX_MODULE_MINIMUM_IDS) {
      reject('MODULE_MINIMUM_ID_COUNT', `${name}: expected 1..${MAX_MODULE_MINIMUM_IDS} IDs, got ${minimum.length}`);
    }
    const localIds = new Set();
    for (const id of minimum) {
      if (!isId(id)) reject('INVALID_MODULE_MINIMUM_ID', name);
      if (localIds.has(id)) reject('DUPLICATE_MODULE_MINIMUM_ID', `${name}: ${id}`);
      if (!knownIds.has(id)) reject('UNKNOWN_MODULE_MINIMUM_ID', `${name}: ${id}`);
      localIds.add(id);
      minimumIds.add(id);
    }
    minimumReferenceCount += minimum.length;
    moduleMinimumCounts.push([name, minimum.length]);
  }

  const aliases = config.aliases === undefined ? {} : config.aliases;
  if (!isObject(aliases)) reject('INVALID_MODULE_ALIASES', 'expected an aliases object');
  for (const [alias, target] of Object.entries(aliases)) {
    if (typeof target !== 'string' || !Object.hasOwn(config.modules, target)) {
      reject('UNKNOWN_MODULE_ALIAS_TARGET', `${alias}: ${String(target)}`);
    }
  }

  return {
    schemaVersion: config.schemaVersion,
    maxModuleMinimumIds: MAX_MODULE_MINIMUM_IDS,
    knownCount: knownIds.size,
    moduleCount: modules.length,
    aliasCount: Object.keys(aliases).length,
    minimumReferenceCount,
    uniqueMinimumIdCount: minimumIds.size,
    sharedMinimumReferenceCount: minimumReferenceCount - minimumIds.size,
    moduleMinimumCounts: Object.fromEntries(moduleMinimumCounts),
    noTestsExecuted: true,
    fullProductAcceptance: false,
  };
}
