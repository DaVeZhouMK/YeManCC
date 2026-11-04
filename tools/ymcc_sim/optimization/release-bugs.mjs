/**
 * Select original regression IDs from an explicit schemaVersion: 1,
 * kind: 'ymcc-release-bug-plan' plan. This module has no I/O or executor.
 *
 * Only purpose: 'bug-regression' selects tests. CPU/HC words in IDs or reasons
 * never change that decision. Every issue requires an id, reason, supported
 * purpose and a nonempty testIds array of exact lowercase dotted IDs; module,
 * wildcard and whole-suite selectors are not test IDs. Issue ids/reasons are
 * trimmed. Test IDs are not normalized or expanded.
 *
 * knownIds is an Array or Set from the existing registry, supplied by the
 * caller. Membership is required only for selected bugs. Excluded issues have
 * status NOT_SELECTED_SPECIALIZED and knownIdsChecked: false; their presence
 * does not assert that their IDs are registered. Invalid structure is always
 * rejected, including in excluded issues.
 *
 * selectedIds deduplicates in first-reference order. Each selectedIssues row
 * keeps its own test references (deduplicated within that issue), so multiple
 * issues may refer to the same selected test. Output rows/arrays are detached
 * from the input. Errors are TypeErrors with stable .code values.
 *
 * Selection is never execution or acceptance. An empty result selects no
 * fallback tests and reports emptySelection: true; no result claims PASS.
 */
import path from 'node:path';

const PURPOSES = new Set(['bug-regression', 'hc-upgrade', 'cpu-optimization']);
const TEST_ID = /^[a-z][a-z0-9]*(?:-[a-z0-9]+)*(?:\.[a-z0-9]+(?:-[a-z0-9]+)*)+$/;

function reject(code, detail) {
  const error = new TypeError(`${code}: ${detail}`);
  error.code = code;
  throw error;
}

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function requiredText(value, field) {
  if (typeof value !== 'string' || !value.trim()) {
    reject('INVALID_RELEASE_BUG_ISSUE', `${field} must be a nonempty string`);
  }
  return value.trim();
}

// These are existing consumer inputs, never a process, argument list or budget.
export const RELEASE_CANDIDATE_PATH_KEYS = new Set([
  'payloadRoot', 'runtimeRoot', 'sharedRuntimeRoot', 'lightingPayloadRoot',
  'legacyHostRoot', 'fanToolLoadPlan', 'completeHostContract',
]);
const CANDIDATE_KEYS = new Set([...RELEASE_CANDIDATE_PATH_KEYS,
  'expectedHelperSha256', 'hcProfile', 'hcVersion', 'fanToolLoadPlanSha256']);
function selectedConfiguration(plan) {
  if (plan.profile !== undefined && !['desktop', 'full', 'sleep'].includes(plan.profile)) {
    reject('INVALID_RELEASE_BUG_PROFILE', 'expected desktop, full or sleep');
  }
  const candidateInputs = {};
  if (plan.candidateInputs !== undefined) {
    if (!isObject(plan.candidateInputs)) reject('INVALID_RELEASE_BUG_CANDIDATES', 'expected an object');
    for (const [key, value] of Object.entries(plan.candidateInputs)) {
      if (!CANDIDATE_KEYS.has(key)) reject('UNSUPPORTED_RELEASE_BUG_CANDIDATE', key);
      if (typeof value !== 'string' || !value.trim() || /[\0\r\n]/.test(value)) {
        reject('INVALID_RELEASE_BUG_CANDIDATE', key);
      }
      if (RELEASE_CANDIDATE_PATH_KEYS.has(key) && !path.isAbsolute(value)) {
        reject('RELEASE_BUG_CANDIDATE_ABSOLUTE_PATH_REQUIRED', key);
      }
      candidateInputs[key] = value;
    }
  }
  return { profile: plan.profile ?? null, candidateInputs };
}

export function selectReleaseBugs(plan, { knownIds } = {}) {
  if (!isObject(plan) || plan.schemaVersion !== 1 || plan.kind !== 'ymcc-release-bug-plan') {
    reject('INVALID_RELEASE_BUG_PLAN_SCHEMA', 'expected schemaVersion 1 and kind ymcc-release-bug-plan');
  }
  if (!Array.isArray(plan.issues)) {
    reject('INVALID_RELEASE_BUG_ISSUES', 'issues must be an array');
  }

  const selectedIssues = [], excludedIssues = [], issueIds = new Set();
  for (const [index, issue] of plan.issues.entries()) {
    const field = `issues[${index}]`;
    if (!isObject(issue)) reject('INVALID_RELEASE_BUG_ISSUE', `${field} must be an object`);
    const id = requiredText(issue.id, `${field}.id`);
    const reason = requiredText(issue.reason, `${field}.reason`);
    if (issueIds.has(id)) reject('DUPLICATE_RELEASE_BUG_ISSUE_ID', `${field}.id repeats ${JSON.stringify(id)}`);
    issueIds.add(id);
    if (!PURPOSES.has(issue.purpose)) {
      reject('INVALID_RELEASE_BUG_PURPOSE', `${field}.purpose must be bug-regression, hc-upgrade or cpu-optimization`);
    }
    if (!Array.isArray(issue.testIds) || issue.testIds.length === 0) {
      reject('INVALID_RELEASE_BUG_TEST_IDS', `${field}.testIds must be a nonempty array of original test IDs`);
    }
    for (const testId of issue.testIds) {
      if (typeof testId !== 'string' || !TEST_ID.test(testId)) {
        reject('INVALID_RELEASE_BUG_TEST_ID', `${field}.testIds must contain exact lowercase dotted IDs, without wildcard or whole-suite selectors`);
      }
    }
    const row = { id, reason, purpose: issue.purpose, testIds: [...new Set(issue.testIds)] };
    if (issue.purpose === 'bug-regression') selectedIssues.push(row);
    else excludedIssues.push({ ...row, status: 'NOT_SELECTED_SPECIALIZED', knownIdsChecked: false });
  }

  const selectedIds = [], selected = new Set();
  if (selectedIssues.length) {
    if (!Array.isArray(knownIds) && !(knownIds instanceof Set)) {
      reject('RELEASE_BUG_KNOWN_IDS_REQUIRED', 'selected bugs require an Array or Set of IDs from the existing registry');
    }
    const known = knownIds instanceof Set ? knownIds : new Set(knownIds);
    for (const issue of selectedIssues) {
      for (const testId of issue.testIds) {
        if (selected.has(testId)) continue;
        if (!known.has(testId)) {
          reject('UNKNOWN_RELEASE_BUG_TEST_ID', `issue ${JSON.stringify(issue.id)} references unregistered test ${JSON.stringify(testId)}`);
        }
        selected.add(testId);
        selectedIds.push(testId);
      }
    }
  }
  return {
    selectedIds,
    selectedIssues,
    excludedIssues,
    emptySelection: selectedIds.length === 0,
    planScope: 'selected-bugs-only',
    ...(selectedIds.length ? selectedConfiguration(plan) : { profile: null, candidateInputs: {} }),
  };
}
