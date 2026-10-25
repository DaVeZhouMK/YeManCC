import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { parseArgs } from '../cli.mjs';
import { evaluateMonitorFieldLogFeedback, readMonitorFieldLogFeedback,
  applyMonitorFieldFeedbackVerdict, MONITOR_FIELD_FEEDBACK_SCHEMA, MAX_MONITOR_FIELD_INPUT_BYTES } from './field-log-feedback.mjs';
const root = 'G:/YeManCC-Work/Testing/YMCC/Workspace/MonitorFieldLogConsumerIntegration-20261023';
fs.mkdirSync(root, {recursive: true});
const scratch = fs.mkdtempSync(path.join(root, 'controls-'));
const hash = value => createHash('sha256').update(value).digest('hex');
const archive = { sha256: 'a'.repeat(64), stable: true };
const tuple = { pid: 2345, buildSha256: 'b'.repeat(64), nativeGeneration: 4 };
function input() { return { schema: MONITOR_FIELD_FEEDBACK_SCHEMA, sourceArchiveSha256: archive.sha256,
  manifest: { schema: 'ymcc.monitor-field-log-cohorts.v1', fixtureScope: 'ORDINARY_EXPORT_UNVERIFIED',
    cohorts: [{sourceTuple: {...tuple}, sources: ['native-lifecycle','frontend-trace'].map(kind => ({kind, sha256: hash(''),
      sourceTuple: {...tuple}, generationBinding: 'verified-single-native-generation-slice', coverage: 'partial'}))}] },
  streams: ['native-lifecycle','frontend-trace'].map(kind => ({cohortIndex: 0, kind, jsonl: ''})) }; }
const run = (value = input(), sourceArchive = archive) => evaluateMonitorFieldLogFeedback({inputBytes: Buffer.from(JSON.stringify(value)), sourceArchive});
const codes = result => result.diagnostics.map(row => row.code);

test('ordinary handoff stays incomplete and never implies current health or acceptance', () => {
  const result = run(); assert.equal(result.status, 'INCOMPLETE'); assert.equal(result.fieldAcceptance, false);
  assert.equal(result.provenanceValidated, false); assert.equal(result.currentNativeHealth, 'UNKNOWN');
  assert.equal(result.automaticExecution, false); assert.equal(result.archiveAttribution, 'CALLER_CLAIM_ONLY');
  assert(codes(result).includes('ARCHIVE_MEMBERS_AND_SOURCE_TUPLE_NOT_INDEPENDENTLY_VERIFIED'));
  assert(result.suggestedTestIds.length > 0); assert(result.suggestedTestIds.every(id => id.startsWith('lifecycle.monitor-notice')));
});
test('different archive cannot reuse a bound pair', () => { const value = input(); value.sourceArchiveSha256 = 'c'.repeat(64);
  assert.deepEqual(codes(run(value)), ['FIELD_ARCHIVE_DIGEST_MISMATCH']); });
test('unavailable or racing archive refuses before analysis', () => {
  for (const source of [null, {stable: true}, {...archive, stable: false}]) {
    const result = run(input(), source); assert.equal(result.analysis, null); assert.equal(result.status, 'REJECTED');
    assert.deepEqual(codes(result), ['ARCHIVE_IDENTITY_UNAVAILABLE']);
  }
});
test('software controls cannot be passed as ordinary exports', () => {
  for (const scope of ['SOFTWARE_ACTUAL_R2', 'SOFTWARE_DERIVED_R2']) { const value = input(); value.manifest.fixtureScope = scope;
    assert.deepEqual(codes(run(value)), ['ORDINARY_EXPORT_SCOPE_REQUIRED']); }
});
test('source text mismatch is a difference without leaking supplied text', () => {
  const value = input(); value.streams[0].jsonl = 'PRIVATE_RAW_SENTINEL'; const result = run(value);
  assert.equal(result.status, 'DIFFERENCE'); assert(!JSON.stringify(result).includes('PRIVATE_RAW_SENTINEL'));
});
test('missing process identity remains unknown', () => {
  const value = input(); delete value.manifest.cohorts[0].sourceTuple.pid; const result = run(value);
  assert.equal(result.status, 'INCOMPLETE'); assert.equal(result.analysis.cohorts[0].identityBound, false);
  assert(result.analysis.findings.some(row => row.code === 'PID_UNBOUND'));
});
test('arbitrary command and nested path envelopes are rejected and never echoed', () => {
  for (const extra of [{command: 'PRIVATE_COMMAND_SENTINEL'}, {paths: ['PRIVATE_PATH_SENTINEL']}]) {
    const result = run({...input(), ...extra}); assert.equal(result.status, 'REJECTED');
    assert(!JSON.stringify(result).includes('PRIVATE_'));
  }
});
test('invalid JSON and oversized envelopes refuse without parser payloads', () => {
  const result = evaluateMonitorFieldLogFeedback({inputBytes: Buffer.from('{PRIVATE_JSON_SENTINEL'), sourceArchive: archive});
  assert.deepEqual(codes(result), ['FIELD_INPUT_JSON_INVALID']); assert(!JSON.stringify(result).includes('PRIVATE_JSON_SENTINEL'));
  assert.deepEqual(codes(evaluateMonitorFieldLogFeedback({inputBytes: Buffer.alloc(MAX_MONITOR_FIELD_INPUT_BYTES + 1), sourceArchive: archive})), ['FIELD_INPUT_SIZE_INVALID']);
});
test('explicit regular file records actual input bytes and both consumer source identities', () => {
  const file = path.join(scratch, 'input.json'), bytes = Buffer.from(JSON.stringify(input())); fs.writeFileSync(file, bytes);
  const result = readMonitorFieldLogFeedback({inputPath: file, sourceArchive: archive});
  assert.equal(result.status, 'INCOMPLETE'); assert.deepEqual(result.input, {sha256: hash(bytes), bytes: bytes.length, stable: true});
  assert.equal(result.sourceHashes.length, 2); assert(result.sourceHashes.every(row => /^[a-f0-9]{64}$/.test(row.sha256)));
});
test('missing file, directory, and malformed explicit path refuse', () => {
  assert.deepEqual(codes(readMonitorFieldLogFeedback({inputPath: path.join(scratch, 'missing'), sourceArchive: archive})), ['FIELD_INPUT_UNREADABLE']);
  assert.deepEqual(codes(readMonitorFieldLogFeedback({inputPath: scratch, sourceArchive: archive})), ['FIELD_INPUT_NOT_REGULAR_FILE']);
  assert.deepEqual(codes(readMonitorFieldLogFeedback({inputPath: 'bad\npath', sourceArchive: archive})), ['FIELD_INPUT_PATH_INVALID']);
});
test('existing errors and incomplete lifecycle verdicts keep their original identity', () => {
  for (const exitCode of [2,3,10,12]) { const verdict = {exitCode, status: 'ORIGINAL_STATUS'};
    assert.equal(applyMonitorFieldFeedbackVerdict(verdict, {status: 'DIFFERENCE'}), verdict); }
});
test('no association result can promote recipes to field PASS', () => {
  for (const status of ['EVIDENCE_LINKED','INCOMPLETE']) assert.deepEqual(applyMonitorFieldFeedbackVerdict({status:'PASS',exitCode:0}, {status}), {status:'RECIPES_PASS_MONITOR_FIELD_ACCEPTANCE_UNVERIFIED',exitCode:10});
  for (const status of ['DIFFERENCE','REJECTED']) assert.deepEqual(applyMonitorFieldFeedbackVerdict({status:'PASS',exitCode:0}, {status}), {status:'MONITOR_FIELD_EVIDENCE_REJECTED',exitCode:2});
});
test('CLI accepts only the two existing log modes and refuses duplicate injection', () => {
  for (const mode of ['replay-plan','log-check']) assert.equal(parseArgs(['--mode',mode,'--monitor-field-log-input','pair.json']).monitorFieldLogInput, 'pair.json');
  for (const mode of ['inventory','plan','run','audit','corpus','replay','signal-check']) assert.throws(() => parseArgs(['--mode',mode,'--monitor-field-log-input','pair.json']), /MONITOR_FIELD_INPUT_REQUIRES_LOG_MODE/);
  assert.throws(() => parseArgs(['--mode','log-check','--monitor-field-log-input','a','--monitor-field-log-input','b']), /DUPLICATE_ARGUMENT/);
});