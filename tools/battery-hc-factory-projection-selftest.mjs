import assert from 'node:assert/strict';
import test from 'node:test';
import { projectBatteryFactoryRoutes } from './lib/battery_hc_factory_projection.mjs';

const maker = 'TEST-MAKER';
function branch({field = 'ProductName', values = ['Test A'], conditions = []} = {}) {
  return { line: 30, switches: [{field: 'ManufacturerName', values: [maker]}, {field, values}], conditions };
}
function entry(deviceClass, {registered = true, effectiveStatus = 'declared-runtime-probe-required', branches = [branch()]} = {}) {
  return { deviceClass, effectiveStatus, coverage: 'fixture', identityEvidence: branches, ymccRoutes: registered ? [{}] : [] };
}
function rule(deviceClass, {field = 'boardProduct', values = ['Test A'], processorPattern} = {}) {
  return { ruleId: `rule.${deviceClass}`, deviceClass, identity: { manufacturers: [maker], field, values, processorPattern } };
}
const project = (entries, rules) => projectBatteryFactoryRoutes(entries, rules);

test('literal coverage does not silently inherit across aliases or classes', () => {
  const r = project([entry('A', {branches: [branch({values: ['Test A', 'Test B', 'Test C']})]})],
    [rule('A'), rule('B', {values: ['Test B']})]);
  assert.deepEqual(r.counts, {'same-field': 1, 'source-field-conflict': 1, 'registered-no-literal-match': 1});
  assert.equal(r.rows[1].source.ruleId, 'rule.B');
  assert.deepEqual(r.candidatesWithAllFactoryLiteralsOnSourceField, []);
  assert.deepEqual(r.rulesWithoutFactoryClass, ['rule.B']);
});

test('using the other DMI field is an explicit contrast, not source-field coverage', () => {
  const r = project([entry('A')], [rule('A', {field: 'systemProduct'})]);
  assert.equal(r.rows[0].status, 'different-field');
  assert.equal(r.rows[0].source.status, 'no-match');
  assert.equal(r.rows[0].other.ruleId, 'rule.A');
  assert.deepEqual(r.candidatesWithAllFactoryLiteralsOnSourceField, []);
});

test('system-model factories and disabled HC inheritance are independent of provider admission', () => {
  const r = project([entry('A', {branches: [branch({field: 'SystemModel'})]}),
    entry('B', {registered: false, effectiveStatus: 'source-disabled', branches: [branch({values: ['Test B']})]})],
  [rule('A', {field: 'systemProduct'})]);
  assert.equal(r.rows[0].field, 'systemProduct');
  assert.equal(r.rows[0].status, 'same-field');
  assert.equal(r.rows[1].status, 'not-registered');
  assert.equal(r.rows[1].effectiveStatus, 'source-disabled');
  assert.deepEqual(r.candidatesWithAllFactoryLiteralsOnSourceField, ['A']);
});

test('a reviewed CPU witness does not bypass the blank-processor contrast', () => {
  const r = project([entry('A')], [rule('A', {processorPattern: String.raw`\bHX\s*370\b`})]);
  assert.equal(r.rows[0].status, 'same-field-processor-gated');
  assert.equal(r.rows[0].source.status, 'no-match');
  assert.equal(r.rows[0].withProcessor.ruleId, 'rule.A');
});

test('ambiguous and conditional branches are not marked covered', () => {
  const r = project([entry('A', {branches: [branch(), branch({conditions: ['conditional']})]})], [rule('A'), rule('B')]);
  assert.equal(r.rows[0].status, 'source-field-conflict');
  assert.equal(r.rows[0].source.status, 'ambiguous');
  assert.equal(r.unresolved[0].reason, 'not-a-direct-literal-branch');
  assert.deepEqual(r.candidatesWithAllFactoryLiteralsOnSourceField, []);
});
