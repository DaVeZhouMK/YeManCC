import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { execFile } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import test from 'node:test';
import { auditFanTestResult, FAN_GLOBAL_TESTS } from './run.mjs';
const receiptFile = process.env.YMCC_FAN_GLOBAL_RESULT;
const unitRoot = process.env.YMCC_FAN_GLOBAL_UNIT_ROOT;
assert(receiptFile && unitRoot && path.isAbsolute(unitRoot), 'EXPLICIT_ORIGINAL_RESULT_AND_OWNED_UNIT_ROOT_REQUIRED');
const receipt = JSON.parse(fs.readFileSync(receiptFile, 'utf8'));
fs.mkdirSync(unitRoot, { recursive: true });
const original = receipt.cases[0];
const clone = () => structuredClone(original);
const directory = path.dirname(fileURLToPath(import.meta.url));

test('all registered original tests executed actual bundled product logic', () => {
  assert.equal(receipt.status, 'PASS');
  assert.deepEqual(receipt.cases.map(row => row.id), FAN_GLOBAL_TESTS.map(row => row.id));
  assert.equal(receipt.summary.executed, FAN_GLOBAL_TESTS.length);
  assert.equal(receipt.summary.originalChecks, receipt.cases.reduce((sum, row) => sum + row.originalReport.checks, 0));
  for (const row of receipt.cases) {
    assert.deepEqual(auditFanTestResult(row), []);
    assert(row.bundleInputs.some(input => input.path === 'src/bridge/fanHost.ts' && input.hashCapturedFromActualLoad));
    assert.equal(row.runtimeBoundary.denied.length, 0);
    assert.equal(row.originalReport.hardwareCalls, 0);
  }
});
test('a nonzero child exit cannot borrow its PASS JSON', () => {
  const row = clone(); row.process.exitCode = 17;
  assert(auditFanTestResult(row).some(item => item.code === 'TEST_PROCESS_DID_NOT_EXIT_ZERO' && item.stage === 'feedback'));
});
test('an empty or missing original structured report is not a completed test', () => {
  const row = clone(); row.originalReport = null;
  assert(auditFanTestResult(row).some(item => item.code === 'ORIGINAL_STRUCTURED_REPORT_MISSING_OR_FAILED'));
});
test('an original assertion failure stays a failure', () => {
  const row = clone(); row.originalReport.results = [{ ok: false, name: 'negative-original-assertion' }];
  assert(auditFanTestResult(row).some(item => item.code === 'ORIGINAL_ASSERTION_FAILED' && item.stage === 'state'));
});
test('missing runtime boundary receipt stops acceptance', () => {
  const row = clone(); row.runtimeBoundary = null;
  assert(auditFanTestResult(row).some(item => item.code === 'FORBIDDEN_IO_OR_BOUNDARY_RECEIPT_MISSING'));
});
test('source drift is distinct from missing behavior', () => {
  const row = clone(); row.sourceStable = false;
  assert(auditFanTestResult(row).some(item => item.code === 'SOURCE_CHANGED_DURING_TEST' && item.stage === 'consumer'));
});
test('a source string reference is not an actual compiler input hash', () => {
  const row = clone(); row.bundleInputs[0].hashCapturedFromActualLoad = false;
  assert(auditFanTestResult(row).some(item => item.code === 'ACTUAL_BUNDLE_INPUT_HASH_MISSING'));
});
test('a bundle without the real lifecycle cannot stand in for execution', () => {
  const row = clone(); row.bundleInputs = row.bundleInputs.filter(input => input.path !== 'src/bridge/fanHost.ts');
  assert(auditFanTestResult(row).some(item => item.code === 'REAL_PRODUCT_LIFECYCLE_NOT_BUNDLED'));
});
async function guardProbe(source) {
  const caseRoot = fs.mkdtempSync(path.join(unitRoot, 'boundary-'));
  const receiptPath = path.join(caseRoot, 'runtime-boundary.json');
  const configuration = path.join(caseRoot, 'configuration.json');
  const entry = path.join(caseRoot, 'entry.mjs');
  fs.writeFileSync(configuration, JSON.stringify({ caseRoot, receiptPath, readFiles: [] }));
  fs.writeFileSync(entry, source);
  const result = await new Promise(resolve => execFile(process.execPath,
    ['--import', pathToFileURL(path.join(directory, 'runtime-guard.mjs')).href, entry],
    { env: { ...process.env, NODE_OPTIONS: '', YMCC_FAN_GLOBAL_BOUNDARY: configuration }, cwd: caseRoot, windowsHide: true, timeout: 5000 },
    (error, stdout, stderr) => resolve({ error, stdout, stderr })));
  assert.equal(result.error, null, result.stderr);
  return { caseRoot, report: JSON.parse(fs.readFileSync(receiptPath, 'utf8')) };
}
test('the child can write and read only its own fixture evidence', async () => {
  const result = await guardProbe("import fs from 'node:fs';fs.writeFileSync('own.json','{}');fs.readFileSync('own.json','utf8');");
  assert.equal(result.report.status, 'PASS');
  assert(result.report.allowedWriteCount > 0);
});
test('caught outside-write attempts cannot manufacture a green boundary', async () => {
  const result = await guardProbe("import fs from 'node:fs';import path from 'node:path';try{fs.writeFileSync(path.resolve('..','forbidden-outside.json'),'{}');}catch{}console.log('caller-caught');");
  assert.equal(result.report.status, 'FAIL');
  assert(result.report.denied.some(row => row.operation === 'fs.writeFileSync'));
  assert.equal(fs.existsSync(path.join(path.dirname(result.caseRoot), 'forbidden-outside.json')), false);
});
test('network requests are rejected before any socket is opened', async () => {
  const result = await guardProbe("try{await fetch('http://127.0.0.1:9/no-connection');}catch{}");
  assert.equal(result.report.status, 'FAIL');
  assert(result.report.denied.some(row => row.operation === 'fetch'));
});
test('child process calls never reach a real executable', async () => {
  const result = await guardProbe("import{spawn}from'node:child_process';import path from'node:path';try{spawn(path.resolve('never-created-guard-probe.exe'));}catch{}");
  assert.equal(result.report.status, 'FAIL');
  assert(result.report.denied.some(row => row.operation === 'child_process.spawn'));
});
test('native library calls are rejected even if the caller catches the error', async () => {
  const result = await guardProbe("try{process.dlopen({},'never-created-guard-probe.node');}catch{}");
  assert.equal(result.report.status, 'FAIL');
  assert(result.report.denied.some(row => row.operation === 'process.dlopen'));
});
