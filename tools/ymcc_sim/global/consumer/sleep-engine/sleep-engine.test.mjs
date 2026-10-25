import assert from 'node:assert/strict';
import fs from 'node:fs';
import test from 'node:test';
import { auditSleepEngineReceipt } from './receipt-audit.mjs';
const input = process.env.YMCC_SLEEP_ENGINE_RECEIPT;
const expectedHostSha256 = process.env.YMCC_SLEEP_ENGINE_HOST_SHA256;
const expectedBackendSha256 = process.env.YMCC_SLEEP_ENGINE_BACKEND_SHA256;
assert(input && expectedHostSha256 && expectedBackendSha256, 'EXPLICIT_EXECUTED_RECEIPT_AND_PINNED_HASHES_REQUIRED');
const receipt = JSON.parse(fs.readFileSync(input, 'utf8').replace(/^\uFEFF/, ''));
// This artifact predates the final wrapper-only guards. Audit its actual DLL/HTTP
// core without pretending it was run by today's wrapper. The new guards have
// separate pure-memory/contract evidence and await the parent global all run.
const options = { expectedHostSha256, expectedBackendSha256, verifySourceHashes: false };
const copy = () => structuredClone(receipt);
const audit = value => auditSleepEngineReceipt(value, options);

test('the recorded candidate core executed all original mode groups in distinct processes', () => {
  const result = audit(receipt);
  assert.equal(result.status, 'PASS', JSON.stringify(result.findings));
  assert.equal(result.summary.scenarios, 41);
  assert.equal(receipt.summary.forms, 20);
  assert.equal(receipt.summary.typeseries, 3);
  assert.equal(receipt.summary.rapidwake, 6);
  assert.equal(receipt.summary.negative, 12, 'four negative kinds run in each independent group');
  assert.equal(result.boundary.nativeSchedulerVerified, false);
  assert.equal(result.boundary.physicalSleepVerified, false);
});
test('a different Host snapshot cannot borrow this candidate acceptance', () => {
  const result = auditSleepEngineReceipt(receipt, { ...options, expectedHostSha256: 'f'.repeat(64) });
  assert.equal(result.status, 'FAIL');
  assert(result.findings.some(row => row.code === 'ACTUAL_LOADED_HOST_IDENTITY_DIFFERS'));
});
test('a wrong Backend pair fails at the artifact input contract', () => {
  const result = auditSleepEngineReceipt(receipt, { ...options, expectedBackendSha256: '0'.repeat(64) });
  assert(result.findings.some(row => row.code === 'PINNED_BACKEND_IDENTITY_DIFFERS' && row.stage === 'input'));
});
test('missing real HTTP consumer is a concrete consumer-stage failure', () => {
  const value = copy(); value.inputConsumerStateFeedback[0].consumer.entry = 'source-reference-only';
  assert(audit(value).findings.some(row => row.code === 'REAL_HTTP_CONSUMER_MISSING' && row.stage === 'consumer'));
});
test('missing response receipt is a feedback failure', () => {
  const value = copy(); delete value.inputConsumerStateFeedback[0].feedback.httpStatus;
  assert(audit(value).findings.some(row => row.code === 'HTTP_FEEDBACK_MISSING' && row.stage === 'feedback'));
});
test('a physical write flag cannot be promoted by the software fixture', () => {
  const value = copy(); value.inputConsumerStateFeedback[0].state.after.hardwareWritesObserved = true;
  assert(audit(value).findings.some(row => row.code === 'PHYSICAL_WRITE_FLAG_NOT_FALSE_hardwareWritesObserved' && row.stage === 'state'));
});
test('a target native attempt or HC load is a hard boundary failure', () => {
  for (const mutation of [value => value.safety.nativeAttempts.push('fixture-forbidden-native'), value => { value.safety.hcLoaded = true; }]) {
    const value = copy(); mutation(value);
    assert(audit(value).findings.some(row => row.code === 'HC_OR_TARGET_NATIVE_ATTEMPT'));
  }
});
test('forced cleanup can never pass natural exit', () => {
  const value = copy(); value.process.forcedCleanup = true; value.process.naturalExitObservedByParent = false;
  assert(audit(value).findings.some(row => row.code === 'NATURAL_EXIT_NOT_CONFIRMED'));
});
test('all must preserve original independent engine histories', () => {
  const value = copy(); value.groups[1].process.pid = value.groups[0].process.pid;
  assert(audit(value).findings.some(row => row.code === 'FRESH_ENGINE_PROCESSES_NOT_DISTINCT'));
});
test('a mapping without actual events is not an executed scenario', () => {
  const value = copy(); value.scenarios[0].requestEventIds = [];
  assert(audit(value).findings.some(row => row.code.startsWith('SCENARIO_NOT_EXECUTED_')));
});
test('tool source drift invalidates the exact run', () => {
  const value = copy(); value.sourceInputs[0].sha256 = '0'.repeat(64);
  assert(auditSleepEngineReceipt(value, { ...options, verifySourceHashes: true }).findings.some(row => row.code === 'TOOL_SOURCE_CHANGED_AFTER_RUN'));
});
test('malformed four-stage entries produce an audit failure rather than being dropped', () => {
  const value = copy(); value.inputConsumerStateFeedback[0] = null;
  assert(audit(value).findings.some(row => row.code === 'FOUR_STAGE_EVIDENCE_MISSING'));
});

test('a group nonzero exit cannot borrow a PASS receipt', () => {
  const value = copy(); value.groups[0].process.exitCode = 17; value.groups[0].status = 'PASS';
  assert(audit(value).findings.some(row => row.code.startsWith('GROUP_EXECUTION_OR_EXIT_FAILED_')));
});

test('an old wrapper receipt cannot serve as final wrapper acceptance', () => {
  const value = auditSleepEngineReceipt(receipt, { ...options, verifySourceHashes: true });
  assert.equal(value.status, 'FAIL');
  assert(value.findings.some(row => row.code === 'SOURCE_BUILD_RUN_STABILITY_UNCONFIRMED'));
});
