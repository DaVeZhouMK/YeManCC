/** Validate the executed software-engine receipt. No Host or hardware is started. */
import fs from 'node:fs';
import crypto from 'node:crypto';
import path from 'node:path';
import {auditSleepEngineReceipt} from './consumer/sleep-engine/receipt-audit.mjs';
const [receiptFile, expectedHostSha256, expectedBackendSha256, expectedMode, output] = process.argv.slice(2);
if (!output) throw new Error('SLEEP_ENGINE_AUDIT_ARGUMENTS_REQUIRED');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
let report;
try {
  if (fs.statSync(receiptFile).size > 16 * 1024 * 1024) throw new Error('RECEIPT_SIZE_LIMIT');
  report = auditSleepEngineReceipt(JSON.parse(fs.readFileSync(receiptFile, 'utf8').replace(/^\uFEFF/, '')), {
    expectedHostSha256, expectedBackendSha256, expectedMode, verifySourceHashes: true,
  });
  report.inputReceipt = {path: path.resolve(receiptFile), sha256: hash(receiptFile)};
  const repo = path.resolve(import.meta.dirname, '../../..');
  report.sourceHashes = Object.fromEntries([
    'tools/ymcc_sim/global/sleep-engine-verify.mjs',
    'tools/ymcc_sim/global/consumer/sleep-engine/receipt-audit.mjs',
  ].map(name => [name, hash(path.join(repo, name))]));
} catch {
  report = {schemaVersion: 1, kind: 'sleep-engine-receipt-audit', status: 'FAIL', findings: [{code: 'EXECUTED_RECEIPT_OR_SOURCE_UNREADABLE', stage: 'feedback'}]};
}
fs.writeFileSync(output, JSON.stringify(report, null, 2));
console.log('SLEEP_ENGINE_RECEIPT_AUDIT ' + report.status);
process.exitCode = report.status === 'PASS' ? 0 : 2;