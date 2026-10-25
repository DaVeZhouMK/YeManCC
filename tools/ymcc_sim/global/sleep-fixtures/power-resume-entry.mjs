/** Execute the current original product self-test, preserving every assertion.
 * Only await its original main so completion and failures reach this adapter.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { beginPowerResumeTools, auditedPowerResumeBuild, originalPowerResumeDataUrl, finishPowerResumeTools } from './power-resume-tool-scope.mjs';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../../..');
const flag = process.argv.indexOf('--output');
if (flag >= 0) assert(process.argv[flag + 1], 'OUTPUT_FILE_REQUIRED');
const scope = beginPowerResumeTools(repo, flag >= 0 ? process.argv[flag + 1] : undefined);
const originalSource = fs.readFileSync(path.join(repo, 'tools/power_resume_selftest.ts'), 'utf8');
const mainEntry = `main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});`;
assert.equal(originalSource.split(mainEntry).length, 2, 'POWER_RESUME_MAIN_ENTRY_CHANGED');
const source = originalSource.replace(mainEntry, 'await main();');
const caseCount = [...originalSource.matchAll(/await test\('/g)].length;
assert.equal(caseCount, 12, 'POWER_RESUME_ORIGINAL_CASE_COUNT_CHANGED');
const built = await auditedPowerResumeBuild(scope, {
  stdin: { contents: source, resolveDir: repo, sourcefile: 'tools/power_resume_selftest.ts', loader: 'ts' },
  bundle: true, write: false, platform: 'node', format: 'esm', target: 'node18',
  alias: { '@': path.join(repo, 'src') }, logLevel: 'silent',
});
const importUrl = originalPowerResumeDataUrl(scope, built);
await import(importUrl);
finishPowerResumeTools(scope, {
  schemaVersion: 1, domain: 'sleep', status: 'PASS', originalSelftestExecuted: true,
  productSourceExecuted: true, sourceAssertionsMigrated: false,
  originalAssertionsUnchanged: true, originalMainAwaited: true, casesExecuted: caseCount,
  operatingSystemSleepPerformed: false, fullApplicationValidated: false,
});
