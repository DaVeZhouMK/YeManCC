import test from 'node:test';
import assert from 'node:assert/strict';
import { selectReleaseBugs } from './release-bugs.mjs';

const plan = (...issues) => ({ schemaVersion: 1, kind: 'ymcc-release-bug-plan', issues });
const issue = (overrides = {}) => ({
  id: 'BUG-FAN-1',
  reason: 'A malformed curve can exceed the fan safety bounds.',
  purpose: 'bug-regression',
  testIds: ['fan.curve'],
  ...overrides,
});
const select = (input, knownIds = ['fan.curve']) => selectReleaseBugs(input, { knownIds });
function refuses(input, code, knownIds = ['fan.curve']) {
  assert.throws(() => select(input, knownIds), error => error instanceof TypeError && error.code === code);
}
function freezePlan(input) {
  for (const row of input.issues) { Object.freeze(row.testIds); Object.freeze(row); }
  Object.freeze(input.issues);
  return Object.freeze(input);
}

// These are data-only references to original fixture IDs, never a second registry
// or an executable fixture list. Only the selector and Node assertions are loaded.
test('only explicit bug issues select original tests and specialized exclusions stay visible', () => {
  const bug = issue();
  const hc = issue({ id: 'HC-1', purpose: 'hc-upgrade', reason: 'Compare the HC upgrade candidate.', testIds: ['fan.hc-init-unavailable'] });
  const cpu = issue({ id: 'CPU-1', purpose: 'cpu-optimization', reason: 'Measure the CPU cost window.', testIds: ['controller.inputhost-cpu-cost-window'] });
  assert.deepEqual(select(plan(hc, bug, cpu)), {
    selectedIds: ['fan.curve'],
    selectedIssues: [bug],
    excludedIssues: [hc, cpu].map(row => ({ ...row, status: 'NOT_SELECTED_SPECIALIZED', knownIdsChecked: false })),
    emptySelection: false,
    planScope: 'selected-bugs-only',
    profile: null, candidateInputs: {},
  });
});

test('deduplicates tests in first-reference order while retaining every issue reference', () => {
  const first = issue({ testIds: ['performance.cpu-state', 'fan.curve', 'performance.cpu-state'] });
  const second = issue({ id: 'BUG-2', testIds: ['fan.curve', 'steam.shared-mode-source', 'fan.curve'] });
  const third = issue({ id: 'BUG-3', testIds: ['performance.cpu-state'] });
  const result = select(plan(first, second, third), ['steam.shared-mode-source', 'fan.curve', 'performance.cpu-state']);
  assert.deepEqual(result.selectedIds, ['performance.cpu-state', 'fan.curve', 'steam.shared-mode-source']);
  assert.deepEqual(result.selectedIssues, [
    { ...first, testIds: ['performance.cpu-state', 'fan.curve'] },
    { ...second, testIds: ['fan.curve', 'steam.shared-mode-source'] },
    third,
  ]);
  assert.equal(result.selectedIssues.length, 3);
});

test('CPU and HC original IDs remain selected when they regress actual bugs', () => {
  const cpu = issue({ id: 'CPU-BUG', reason: 'The CPU lock consumer applies a stale setting after an ownership change.', testIds: ['performance.cpulock-consumer'] });
  const hc = issue({ id: 'HC-BUG', reason: 'An unavailable HC initialization path incorrectly reports success.', testIds: ['fan.hc-init-unavailable'] });
  const result = select(plan(cpu, hc), new Set(['performance.cpulock-consumer', 'fan.hc-init-unavailable']));
  assert.deepEqual(result.selectedIds, ['performance.cpulock-consumer', 'fan.hc-init-unavailable']);
  assert.deepEqual(result.selectedIssues, [cpu, hc]);
  assert.deepEqual(result.excludedIssues, []);
});

test('specialized purpose remains excluded even when its reason contains bug-regression words', () => {
  const result = selectReleaseBugs(plan(
    issue({ id: 'HC-1', purpose: 'hc-upgrade', reason: 'Track bug-regression coverage during HC migration.' }),
    issue({ id: 'CPU-1', purpose: 'cpu-optimization', reason: 'Compare CPU optimization bug fixes.' }),
  ));
  assert.deepEqual(result.selectedIds, []);
  assert.equal(result.emptySelection, true);
  assert.ok(result.excludedIssues.every(row => row.status === 'NOT_SELECTED_SPECIALIZED' && row.knownIdsChecked === false));
});

test('unknown specialized IDs cannot block a known bug and are not declared registered', () => {
  const result = select(plan(
    issue({ id: 'HC-UNKNOWN', purpose: 'hc-upgrade', testIds: ['fan.not-registered-hc'] }),
    issue(),
    issue({ id: 'CPU-UNKNOWN', purpose: 'cpu-optimization', testIds: ['performance.not-registered-cpu'] }),
  ));
  assert.deepEqual(result.selectedIds, ['fan.curve']);
  assert.deepEqual(result.excludedIssues.map(row => row.knownIdsChecked), [false, false]);
  assert.deepEqual(result.excludedIssues.map(row => row.testIds), [['fan.not-registered-hc'], ['performance.not-registered-cpu']]);
});

test('a specialized reference to an ID does not veto a separate real bug using that ID', () => {
  const result = select(plan(
    issue({ id: 'HC-1', purpose: 'hc-upgrade', testIds: ['fan.hc-init-unavailable'] }),
    issue({ id: 'HC-BUG', testIds: ['fan.hc-init-unavailable'] }),
  ), ['fan.hc-init-unavailable']);
  assert.deepEqual(result.selectedIds, ['fan.hc-init-unavailable']);
  assert.equal(result.selectedIssues[0].id, 'HC-BUG');
  assert.equal(result.excludedIssues[0].status, 'NOT_SELECTED_SPECIALIZED');
});

test('registry membership is queried once per selected ID, never for specialized-only IDs', () => {
  const checked = [];
  class ObservedIds extends Set {
    has(id) { checked.push(id); return super.has(id); }
  }
  const known = new ObservedIds(['fan.curve']);
  select(plan(
    issue({ id: 'HC-1', purpose: 'hc-upgrade', testIds: ['fan.unregistered-specialized'] }),
    issue(), issue({ id: 'BUG-2' }),
  ), known);
  assert.deepEqual(checked, ['fan.curve']);
  assert.deepEqual([...known], ['fan.curve']);
});

test('an unknown selected bug is explicitly rejected with its issue and test ID', () => {
  assert.throws(() => select(plan(issue({ id: 'BUG-MISSING', testIds: ['fan.not-registered'] }))), error => {
    assert.equal(error.code, 'UNKNOWN_RELEASE_BUG_TEST_ID');
    assert.match(error.message, /BUG-MISSING/);
    assert.match(error.message, /fan\.not-registered/);
    return true;
  });
});

test('known IDs and exclusions do not conceal a later unknown selected test', () => {
  refuses(plan(issue(), issue({ id: 'BUG-MISSING', testIds: ['fan.curve', 'fan.not-registered'] })), 'UNKNOWN_RELEASE_BUG_TEST_ID');
  refuses(plan(
    issue({ id: 'HC-1', purpose: 'hc-upgrade', testIds: ['fan.not-registered'] }),
    issue({ id: 'BUG-MISSING', testIds: ['fan.not-registered'] }),
  ), 'UNKNOWN_RELEASE_BUG_TEST_ID');
});

test('selected bugs require a caller-supplied registry Array or Set', () => {
  assert.throws(() => selectReleaseBugs(plan(issue())), { code: 'RELEASE_BUG_KNOWN_IDS_REQUIRED' });
  for (const known of [null, 'fan.curve', {}, { has: () => true }]) {
    refuses(plan(issue()), 'RELEASE_BUG_KNOWN_IDS_REQUIRED', known);
  }
  for (const known of [[], new Set()]) refuses(plan(issue()), 'UNKNOWN_RELEASE_BUG_TEST_ID', known);
});

test('an empty plan selects nothing, needs no registry and never claims execution or PASS', () => {
  const result = selectReleaseBugs(plan());
  assert.deepEqual(result, {
    selectedIds: [], selectedIssues: [], excludedIssues: [], emptySelection: true, planScope: 'selected-bugs-only', profile: null, candidateInputs: {},
  });
  assert.equal(Object.hasOwn(result, 'status'), false);
  assert.equal(Object.hasOwn(result, 'actualPassedThisRun'), false);
  assert.equal(Object.hasOwn(result, 'executedThisRun'), false);
});

test('a specialized-only plan selects nothing even without a usable registry', () => {
  const input = plan(
    issue({ id: 'HC-1', purpose: 'hc-upgrade', testIds: ['fan.unknown-hc'] }),
    issue({ id: 'CPU-1', purpose: 'cpu-optimization', testIds: ['performance.unknown-cpu'] }),
  );
  for (const known of [undefined, null, {}, []]) {
    const result = selectReleaseBugs(input, { knownIds: known });
    assert.equal(result.emptySelection, true);
    assert.deepEqual(result.selectedIds, []);
    assert.deepEqual(result.selectedIssues, []);
    assert.equal(result.excludedIssues.length, 2);
    assert.equal(Object.hasOwn(result, 'status'), false);
    assert.ok(result.excludedIssues.every(row => row.status === 'NOT_SELECTED_SPECIALIZED' && row.knownIdsChecked === false));
  }
});

test('missing or wrong plan schema and kind are refused', () => {
  for (const input of [undefined, null, [], 'plan', {},
    { ...plan(), schemaVersion: '1' }, { ...plan(), schemaVersion: 2 },
    { ...plan(), schemaVersion: undefined }, { ...plan(), kind: undefined },
    { ...plan(), kind: 'ymcc-release-plan' },
  ]) refuses(input, 'INVALID_RELEASE_BUG_PLAN_SCHEMA');
});

test('issues must be an array rather than an implicit empty selection', () => {
  for (const issues of [undefined, null, {}, 'all']) refuses({ ...plan(), issues }, 'INVALID_RELEASE_BUG_ISSUES');
});

test('issue rows must be objects and sparse arrays cannot hide missing issues', () => {
  for (const row of [null, undefined, [], 'BUG-1', 1]) refuses(plan(row), 'INVALID_RELEASE_BUG_ISSUE');
  refuses({ ...plan(), issues: new Array(1) }, 'INVALID_RELEASE_BUG_ISSUE');
});

test('id and reason must both be nonempty text, including for excluded issues', () => {
  for (const field of ['id', 'reason']) {
    for (const value of [undefined, null, '', ' \t\r\n', 1, false, {}]) {
      for (const purpose of ['bug-regression', 'hc-upgrade']) {
        refuses(plan(issue({ [field]: value, purpose })), 'INVALID_RELEASE_BUG_ISSUE');
      }
    }
  }
});

test('surrounding issue text is trimmed without normalizing test identity', () => {
  const result = select(plan(issue({ id: '  BUG-1\t', reason: '  曲线超出安全边界。\n' })));
  assert.equal(result.selectedIssues[0].id, 'BUG-1');
  assert.equal(result.selectedIssues[0].reason, '曲线超出安全边界。');
  assert.deepEqual(result.selectedIds, ['fan.curve']);
});

test('issue IDs must be unique across selected and excluded purposes, including trimmed duplicates', () => {
  for (const purpose of ['bug-regression', 'hc-upgrade', 'cpu-optimization']) {
    refuses(plan(issue(), issue({ purpose })), 'DUPLICATE_RELEASE_BUG_ISSUE_ID');
    refuses(plan(issue(), issue({ id: '  BUG-FAN-1\t', purpose })), 'DUPLICATE_RELEASE_BUG_ISSUE_ID');
  }
});

test('unknown, misspelled and missing purposes cannot default to bug selection', () => {
  for (const purpose of [undefined, null, '', 'bug', 'BUG-REGRESSION', 'bug-regression ', '*', ['bug-regression']]) {
    refuses(plan(issue({ purpose })), 'INVALID_RELEASE_BUG_PURPOSE');
  }
});

test('testIds must be a nonempty array for selected and excluded issues', () => {
  for (const testIds of [undefined, null, 'fan.curve', {}, new Set(['fan.curve']), []]) {
    for (const purpose of ['bug-regression', 'cpu-optimization']) {
      refuses(plan(issue({ purpose, testIds })), 'INVALID_RELEASE_BUG_TEST_IDS');
    }
  }
});

test('non-string and sparse test references are refused rather than coerced or skipped', () => {
  for (const value of [undefined, null, 1, false, {}, ['fan.curve']]) {
    refuses(plan(issue({ testIds: [value] })), 'INVALID_RELEASE_BUG_TEST_ID');
  }
  refuses(plan(issue({ testIds: new Array(1) })), 'INVALID_RELEASE_BUG_TEST_ID');
});

test('canonical original IDs cannot be padded, case-folded, paths or malformed dotted names', () => {
  for (const id of ['', ' fan.curve', 'fan.curve ', 'fan.curve\n', 'Fan.curve', 'fan..curve',
    'fan.-curve', 'fan.curve-', 'fan.curve_test', '../fan.curve', 'fan/curve', 'fan\\curve', 'fan.curve,steam.shared-mode-source',
  ]) refuses(plan(issue({ testIds: [id] })), 'INVALID_RELEASE_BUG_TEST_ID', [id]);
});

test('wildcards and whole-suite or module substitutes are refused even when supplied as known IDs', () => {
  for (const id of ['*', 'all', 'full', 'global', 'default', 'fan', 'performance', 'all-tests', 'fan.*', '*.curve',
    'fan.cur*', 'fan.?', '--all', 'module:fan', 'all,fan.curve',
  ]) {
    for (const purpose of ['bug-regression', 'hc-upgrade']) {
      refuses(plan(issue({ purpose, testIds: [id] })), 'INVALID_RELEASE_BUG_TEST_ID', [id]);
    }
  }
});

test('an invalid excluded test reference still fails structural validation', () => {
  refuses(plan(issue(), issue({ id: 'HC-1', purpose: 'hc-upgrade', testIds: ['fan.*'] })), 'INVALID_RELEASE_BUG_TEST_ID');
});

test('selection is deterministic on frozen inputs and output mutation cannot change the plan', () => {
  const input = freezePlan(plan(
    issue({ testIds: ['fan.curve', 'fan.curve'] }),
    issue({ id: 'BUG-2' }),
    issue({ id: 'HC-1', purpose: 'hc-upgrade', testIds: ['fan.unknown-hc', 'fan.unknown-hc'] }),
  ));
  const known = Object.freeze(['fan.curve']);
  const first = select(input, known), second = select(input, known);
  assert.deepEqual(first, second);
  assert.notStrictEqual(first.selectedIssues[0], input.issues[0]);
  assert.notStrictEqual(first.selectedIssues[0].testIds, first.selectedIssues[1].testIds);
  assert.notStrictEqual(first.excludedIssues[0].testIds, input.issues[2].testIds);
  first.selectedIds.push('fan.output-only');
  first.selectedIssues[0].testIds.length = 0;
  first.selectedIssues[1].reason = 'Changed output';
  first.excludedIssues[0].testIds.push('fan.output-only');
  assert.deepEqual(select(input, known), second);
  assert.deepEqual(second.excludedIssues[0].testIds, ['fan.unknown-hc']);
});


test('selected candidate configuration is detached and preserves an explicit common profile', () => {
  const payload = process.platform === 'win32' ? 'G:/YeManCC-Work/Testing/YMCC/Workspace/candidate' : '/tmp/ymcc-candidate';
  const input = {...plan(issue()), profile: 'full', candidateInputs: {payloadRoot: payload, hcVersion: '0.32.3.2'}};
  const result = select(input);
  assert.equal(result.profile, 'full');
  assert.deepEqual(result.candidateInputs, input.candidateInputs);
  result.candidateInputs.payloadRoot = 'changed-output';
  assert.equal(input.candidateInputs.payloadRoot, payload);
});
test('candidate plans cannot provide a process, arbitrary arguments, output or execution budget', () => {
  for (const key of ['executable','args','powershell','mode','tests','modules','outDir','budgetMs']) {
    refuses({...plan(issue()), candidateInputs: {[key]: 'arbitrary'}}, 'UNSUPPORTED_RELEASE_BUG_CANDIDATE');
  }
});
test('candidate paths require absolute nonempty single-line values', () => {
  for (const value of [null, 0, '', '   ', 'relative/candidate', 'x\ny', 'x\0y']) {
    assert.throws(() => select({...plan(issue()), candidateInputs: {payloadRoot: value}}), /RELEASE_BUG_CANDIDATE/);
  }
  refuses({...plan(issue()), candidateInputs: []}, 'INVALID_RELEASE_BUG_CANDIDATES');
});
test('selected profile must use the existing profile vocabulary', () => {
  for (const profile of [null, '', 'FULL', 'all', false]) {
    refuses({...plan(issue()), profile}, 'INVALID_RELEASE_BUG_PROFILE');
  }
});
test('unselected specialty candidates and profiles are not validated or loaded', () => {
  const input = {...plan(issue({purpose: 'hc-upgrade'})), profile: 'future-profile', candidateInputs: {args: ['never-executed']}};
  const result = selectReleaseBugs(input);
  assert.equal(result.emptySelection, true);
  assert.deepEqual(result.candidateInputs, {});
  assert.equal(result.profile, null);
});
