import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {assembleExtraAdapters} from './extra-adapter-assembly.mjs';

// Synthetic declarations only: no real adapters, builds or fixture execution.
let sequence = 0;
const syntheticRoot = 'G:/YeManCC-Work/Testing/YMCC/Evidence/ReleaseFixturePolicy-20261030/Implementation';
const row = (id, extra = {}) => ({id, domain: 'synthetic', profiles: ['desktop', 'full'], ...extra});
const metadata = rows => rows.map(item => ({id: item.id, domain: item.domain,
 profiles: item.profiles, lifecycleDeclared: item.lifecycleContract !== undefined}));
function fixture(groups) {
 const calls = groups.map(() => 0);
 const factories = groups.map((rows, i) => () => {calls[i]++; return structuredClone(rows);});
 const index = {groups: groups.map(rows => ({rows: metadata(rows)})), sources: []};
 const repoRoot = path.join(syntheticRoot, 'synthetic-registration-' + ++sequence);
 return {calls, factories, index, repoRoot};
}
const assemble = (scope, options = {}) => assembleExtraAdapters(scope.factories, scope.index,
 {repoRoot: scope.repoRoot, ...options});
const forbidden = () => {throw Error('UNSELECTED_FACTORY_EXECUTED');};

for (const indexCurrent of [true, false]) {
 const label = indexCurrent ? 'current index' : 'stale index';
 test(label + ': one selected ID materializes only its original group and filters siblings', () => {
  const selected = row('synthetic.selected', {sourceInputs: ['original-input'], guard: 'retained'});
  const scope = fixture([[selected, row('synthetic.sibling')], [row('synthetic.unrelated')]]);
  scope.factories[1] = forbidden;
  assert.deepEqual(assemble(scope, {indexCurrent, selectedIds: [selected.id]}), [selected]);
  assert.deepEqual(scope.calls, [1, 0]);
 });
 test(label + ': multiple selected groups retain registration order and skip an intervening throw', () => {
  const first = row('synthetic.first'), last = row('synthetic.last');
  const scope = fixture([[first], [row('synthetic.unrelated')], [last, row('synthetic.sibling')]]);
  scope.factories[1] = forbidden;
  assert.deepEqual(assemble(scope, {indexCurrent, selectedIds: [last.id, first.id]}), [first, last]);
  assert.deepEqual(scope.calls, [1, 0, 1]);
 });
 test(label + ': an unknown new ID remains unresolved without discovering an unindexed route', () => {
  const scope = fixture([[row('synthetic.original')], [row('synthetic.other')]]);
  let calls = 0;
  scope.factories = [() => {calls++; return [row('synthetic.new')];}, forbidden];
  const selectedIds = ['synthetic.new'];
  const rows = assemble(scope, {indexCurrent, selectedIds});
  assert.deepEqual(rows, []);
  assert.deepEqual(selectedIds.filter(id => !rows.some(item => item.id === id)), selectedIds);
  assert.equal(calls, 0);
 });
 test(label + ': omitted or empty selection retains every original full-registration group', () => {
  const groups = [[row('synthetic.first'), row('synthetic.second')],
   [row('synthetic.lifecycle', {lifecycleContract: {owner: 'synthetic'}})]];
  for (const selectedIds of [undefined, []]) {
   const scope = fixture(groups);
   assert.deepEqual(assemble(scope, {indexCurrent, selectedIds}), groups.flat());
   assert.deepEqual(scope.calls, [1, 1]);
  }
 });
 test(label + ': explicit selection preserves audit-receipt materialization and final selection scope', () => {
  const selected = row('synthetic.selected');
  const scope = fixture([[selected], [row('synthetic.lifecycle', {lifecycleContract: {}})],
   [row('synthetic.receipt'), row('synthetic.receipt-sibling')]]);
  scope.factories[1] = forbidden;
  assert.deepEqual(assemble(scope, {indexCurrent, selectedIds: [selected.id],
   auditReceiptIds: ['synthetic.receipt']}), [selected]);
  assert.deepEqual(scope.calls, [1, 0, 1]);
 });
 test(label + ': audit without selection retains receipt/lifecycle scope and identity-only rows', () => {
  const groups = [[row('synthetic.receipt')],
   [row('synthetic.lifecycle', {lifecycleContract: {owner: 'synthetic'}})],
   [row('synthetic.missing', {sourceInputs: ['not-loaded-during-current-index-audit']})]];
  const scope = fixture(groups);
  if (indexCurrent) scope.factories[2] = forbidden;
  const expected = indexCurrent ? [...groups[0], ...groups[1],
   {...row('synthetic.missing'), registrationIdentityOnly: true}] : groups.flat();
  assert.deepEqual(assemble(scope, {indexCurrent, auditReceiptIds: ['synthetic.receipt']}), expected);
  assert.deepEqual(scope.calls, indexCurrent ? [1, 1, 0] : [1, 1, 1]);
 });
 test(label + ': an empty audit receipt list retains the original lifecycle-only/full scope', () => {
  const groups = [[row('synthetic.ordinary')],
   [row('synthetic.lifecycle', {lifecycleContract: null})]];
  const scope = fixture(groups);
  if (indexCurrent) scope.factories[0] = forbidden;
  assert.deepEqual(assemble(scope, {indexCurrent, auditReceiptIds: []}), indexCurrent ?
   [{...groups[0][0], registrationIdentityOnly: true}, ...groups[1]] : groups.flat());
  assert.deepEqual(scope.calls, indexCurrent ? [0, 1] : [1, 1]);
 });
 const changes = {
  id: rows => {rows[0].id = 'synthetic.renamed';},
  domain: rows => {rows[0].domain = 'other';},
  profiles: rows => {rows[0].profiles = ['full'];},
  'profile order': rows => {rows[0].profiles.reverse();},
  'lifecycle addition': rows => {rows[0].lifecycleContract = null;},
  'lifecycle removal': rows => {delete rows[1].lifecycleContract;},
  'unselected sibling metadata': rows => {rows[1].domain = 'other';},
  'added ID': rows => {rows.push(row('synthetic.new'));},
  'removed ID': rows => {rows.pop();},
  'empty materialization': rows => {rows.length = 0;},
  'group order': rows => {rows.reverse();},
 };
 for (const [change, mutate] of Object.entries(changes)) {
  test(label + ': selected group rejects ' + change + ' against original index metadata', () => {
   const groups = [[row('synthetic.selected'), row('synthetic.sibling', {lifecycleContract: {}})],
    [row('synthetic.unrelated')]];
   const scope = fixture(groups), actual = structuredClone(groups[0]);
   mutate(actual);
   scope.factories[0] = () => {scope.calls[0]++; return actual;};
   scope.factories[1] = forbidden;
   assert.throws(() => assemble(scope, {indexCurrent, selectedIds: ['synthetic.selected']}),
    {message: 'EXTRA_REGISTRATION_INDEX_CHANGED:0'});
   assert.deepEqual(scope.calls, [1, 0]);
  });
 }
 test(label + ': full registration still rejects actual group topology mismatch', () => {
  const scope = fixture([[row('synthetic.original')]]);
  scope.factories[0] = () => [];
  assert.throws(() => assemble(scope, {indexCurrent}), {message: 'EXTRA_REGISTRATION_INDEX_CHANGED:0'});
 });
 test(label + ': an audit-receipt-only materialized group is also checked strictly', () => {
  const scope = fixture([[row('synthetic.selected')], [row('synthetic.receipt')]]);
  scope.factories[1] = () => [row('synthetic.changed-receipt')];
  assert.throws(() => assemble(scope, {indexCurrent, selectedIds: ['synthetic.selected'],
   auditReceiptIds: ['synthetic.receipt']}), {message: 'EXTRA_REGISTRATION_INDEX_CHANGED:1'});
 });
}

for (const primedCurrent of [true, false]) {
 test('stale selected materialization cannot hide topology drift behind a ' +
  (primedCurrent ? 'current' : 'stale') + ' cached declaration', () => {
  const scope = fixture([[row('synthetic.selected')]]), options = {selectedIds: ['synthetic.selected']};
  assemble(scope, {...options, indexCurrent: primedCurrent});
  scope.factories[0] = () => {scope.calls[0]++; return [row('synthetic.changed')];};
  assert.throws(() => assemble(scope, {...options, indexCurrent: false}),
   {message: 'EXTRA_REGISTRATION_INDEX_CHANGED:0'});
  assert.deepEqual(scope.calls, [2]);
 });
}
test('current-index registration memoization remains available for unchanged declarations', () => {
 const original = row('synthetic.selected'), scope = fixture([[original]]);
 const options = {selectedIds: [original.id], indexCurrent: true};
 const first = assemble(scope, options);
 first[0].domain = 'consumer-mutated-copy';
 assert.deepEqual(assemble(scope, options), [original]);
 assert.deepEqual(scope.calls, [1]);
});
