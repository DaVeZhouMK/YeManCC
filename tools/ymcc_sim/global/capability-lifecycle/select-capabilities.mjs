/** Bounded, source-bound selection only. The caller supplies current registration
 * descriptors and still owns configure/validate/run plus every lifecycle gate.
 * No adapter/product imports, command execution, path arguments or acceptance.
 */
import {getOwnershipPlan} from './ownership-catalog.mjs';

export const SUPPORTED_CAPABILITIES = Object.freeze([
  'fan', 'motion', 'controller.input', 'lighting', 'oem.back-buttons',
  'oem.dedicated-buttons', 'controller.feedback', 'battery.charge-limit',
]);
const MODES = new Set(['inventory', 'plan', 'run']);
const PROFILES = new Set(['desktop', 'full', 'sleep']);
const ID = /^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)*$/;

export class CapabilitySelectionError extends Error {
  constructor(code, details = [], selection = null) {
    super([code, ...details].join(':'));
    this.name = 'CapabilitySelectionError';
    this.code = code;
    this.details = details;
    this.selection = selection;
  }
}
const refuse = (code, details = [], selection = null) => {
  throw new CapabilitySelectionError(code, details, selection);
};
const validId = value => typeof value === 'string' && value.length <= 128 && ID.test(value);
function ids(values, kind, limit) {
  if (!Array.isArray(values) || values.length > limit || !values.every(validId)) {
    refuse('CAPABILITY_INVALID_' + kind);
  }
  return [...new Set(values)];
}
/** Feed each repeated --capability value here; comma lists are also supported.
 * undefined means absent. Empty values/segments, globs and shell syntax refuse.
 */
export function parseCapabilities(values) {
  if (values === undefined) return [];
  const entries = typeof values === 'string' ? [values] : values;
  if (!Array.isArray(entries) || !entries.length || entries.length > 64
      || !entries.every(value => typeof value === 'string' && value.length <= 1024)) {
    refuse('CAPABILITY_INVALID_ARGUMENT');
  }
  const tokens = entries.flatMap(value => value.split(',').map(part => part.trim()));
  const result = ids(tokens, 'ARGUMENT', 64);
  if (result.length > SUPPORTED_CAPABILITIES.length) refuse('CAPABILITY_TOO_MANY');
  return result;
}
export function assertCapabilityMode(mode) {
  if (!MODES.has(mode)) refuse('CAPABILITY_UNSUPPORTED_MODE', [String(mode)]);
}

/** Returns null only for an absent capability parameter (legacy caller path).
 * adapters must be the caller's complete current registration scope, before
 * profile/domain/test filters. Missing references block rather than disappear.
 * No caller-supplied ownership report or alternate catalog can bypass source
 * review. repoRoot is trusted caller context, never a capability value.
 */
export function selectCapabilities({repoRoot, capabilities, adapters, mode = 'inventory',
  profile = 'desktop', domains = [], tests = []} = {}) {
  if (capabilities === undefined) return null;
  const capabilityIds = parseCapabilities(capabilities);
  assertCapabilityMode(mode);
  if (!PROFILES.has(profile)) refuse('CAPABILITY_INVALID_PROFILE');
  const domainIds = ids(domains, 'DOMAINS', 64);
  const testIds = ids(tests, 'TESTS', 512);
  if (typeof repoRoot !== 'string' || !repoRoot.length) refuse('CAPABILITY_REPO_ROOT_REQUIRED');
  if (!Array.isArray(adapters) || adapters.length > 4096) refuse('CAPABILITY_INVALID_REGISTRATIONS');
  const byId = new Map();
  for (const row of adapters) {
    if (!row || !validId(row.id) || byId.has(row.id) || !validId(row.domain)
        || !Array.isArray(row.profiles) || !row.profiles.length
        || !row.profiles.every(value => PROFILES.has(value))) {
      refuse('CAPABILITY_INVALID_REGISTRATIONS');
    }
    byId.set(row.id, row);
  }
  const ownershipPlan = getOwnershipPlan({repoRoot, ids: capabilityIds, adapters});
  const selection = {
    schemaVersion: 1, kind: 'ymcc-capability-selection', status: 'BLOCKED', mode,
    capabilityIds, filters: {profile, domains: domainIds, tests: testIds},
    ownershipPlan, candidateIds: [], selectedIds: [], excludedCandidates: [],
    remainingAcceptanceGaps: ownershipPlan.features.map(feature => ({
      capabilityId: feature.id, persistence: feature.persistence, status: feature.status,
      requirements: [...feature.gaps], unregisteredTestIds: [...(feature.unregisteredTestIds ?? [])],
    })),
    perCapability: [], noTestsExecuted: true, businessExecuted: 0, executedTests: 0,
    automaticExecution: false, executionAuthorized: false,
    lifecycleAcceptance: 'INCOMPLETE', softwareAcceptance: false,
    hardwareAcceptance: false, cpuPowerAcceptance: false,
    meaning: 'Selection only; current registration and source review are not execution or lifecycle acceptance.',
  };
  const block = (code, details) => {
    selection.refusal = {code, details};
    refuse(code, details, selection);
  };
  // Validate all requested features before publishing a candidate set: a mixed
  // valid/unknown/stale request must never become a partial successful plan.
  for (const feature of ownershipPlan.features) {
    if (feature.status !== 'CLASSIFIED_SOURCE_BOUND') {
      const reason = ['UNKNOWN', 'INACTIVE', 'STALE'].includes(feature.status) ? feature.status : 'UNCLASSIFIED';
      block('CAPABILITY_' + reason, [feature.id]);
    }
    if (!SUPPORTED_CAPABILITIES.includes(feature.id) || feature.scope !== 'confirmed-enabled'
        || feature.active === false) block('CAPABILITY_NOT_ENABLED', [feature.id]);
    if (!['persistent', 'one-shot'].includes(feature.persistence)) block('CAPABILITY_UNCLASSIFIED', [feature.id]);
    if (!feature.registeredTestIds.length) block('CAPABILITY_NO_REGISTERED_IDS', [feature.id]);
    if (feature.unregisteredTestIds.length) {
      block('CAPABILITY_REGISTRATION_INCOMPLETE', [feature.id, ...feature.unregisteredTestIds]);
    }
    if (!feature.registeredTestIds.every(id => byId.has(id) && feature.testReferences.includes(id))) {
      block('CAPABILITY_INVALID_REGISTERED_IDS', [feature.id]);
    }
  }
  const candidates = new Set(ownershipPlan.features.flatMap(feature => feature.registeredTestIds));
  selection.candidateIds = [...candidates];
  const unknownDomains = domainIds.filter(domain => !adapters.some(row => row.domain === domain));
  if (unknownDomains.length) block('CAPABILITY_UNKNOWN_DOMAINS', unknownDomains);
  const outside = testIds.filter(id => !candidates.has(id));
  if (outside.length) block('CAPABILITY_TEST_OUTSIDE_CANDIDATES', outside);
  // Keep the existing intersection and descriptor order. Do not rewrite tests,
  // profiles, tool/source/payload/owner inputs, timeouts or lifecycle contracts.
  for (const row of adapters) {
    if (!candidates.has(row.id)) continue;
    const reasons = [];
    if (!row.profiles.includes(profile)) reasons.push('PROFILE_FILTER');
    if (domainIds.length && !domainIds.includes(row.domain)) reasons.push('DOMAIN_FILTER');
    if (testIds.length && !testIds.includes(row.id)) reasons.push('TEST_FILTER');
    if (reasons.length) selection.excludedCandidates.push({id: row.id, reasons});
    else selection.selectedIds.push(row.id);
  }
  selection.perCapability = ownershipPlan.features.map(feature => ({
    capabilityId: feature.id, candidateIds: [...feature.registeredTestIds],
    selectedIds: feature.registeredTestIds.filter(id => selection.selectedIds.includes(id)),
    executionAcceptance: false,
  }));
  if (!selection.selectedIds.length) block('CAPABILITY_EMPTY_SELECTION', capabilityIds);
  selection.status = 'SELECTION_ONLY_NOT_EXECUTED';
  return selection;
}
