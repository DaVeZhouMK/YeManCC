/** Execute the original product self-test and its queue interruption successor.
 * The Ready source assertion follows its new coalesced compensation contract;
 * the original transaction ordering, budgets and behavioral assertions remain.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { beginPowerResumeTools, auditedPowerResumeBuild, originalPowerResumeDataUrl, finishPowerResumeTools } from './power-resume-tool-scope.mjs';
import { appPowerResumeSource } from '../../../fixtures/power_resume_app_source.mjs';

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
let source = originalSource.replace(mainEntry, 'await main();');
let appSource = fs.readFileSync(path.join(repo, 'src/App.vue'), 'utf8');
const appFirstReadyNegative = process.argv.includes('--app-first-ready-negative');
if (appFirstReadyNegative) {
  const retainedRequest = '    else if (!first || confirmedF5) onPowerResumed(event);';
  appSource = appSource.replaceAll('\r\n','\n');
  assert.equal(appSource.split(retainedRequest).length, 2, 'APP_FIRST_READY_NEGATIVE_UNIQUE');
  appSource = appSource.replace(retainedRequest, '    else if (!first) onPowerResumed(event);');
}
const appSleepConfirmationOnly=process.argv.includes('--app-sleep-confirmation-only');
const appSleepPlanOnly=process.argv.includes('--app-sleep-plan-only')||appSleepConfirmationOnly;
assert(!(appSleepConfirmationOnly&&process.argv.includes('--app-sleep-plan-only')),'POWER_RESUME_SLEEP_SELECTION_CONFLICT');
const appPrepared = appPowerResumeSource(appSource,appSleepPlanOnly?fs.readFileSync(path.join(repo,'src/bridge/yeman.ts'),'utf8'):null);
assert.equal(source.split('/*__ACTUAL_APP_RESUME__*/').length, 2);
source = source.replace('/*__ACTUAL_APP_RESUME__*/', appPrepared.source);
const sharedBytes=fs.readFileSync(path.join(repo,'src/bridge/powerLifecycleState.ts'),'utf8');
const sharedImport="import {invoke,on} from './ipc';";
assert.equal(sharedBytes.split(sharedImport).length,2,'POWER_RESUME_SHARED_IMPORT_UNIQUE');
const singleton=sharedBytes.indexOf('const shared=createSharedPowerLifecycleSource({');
assert(singleton>sharedImport.length&&sharedBytes.indexOf('const shared=createSharedPowerLifecycleSource({',singleton+1)<0,'POWER_RESUME_SHARED_SINGLETON_UNIQUE');
const sharedBody=sharedBytes.slice(sharedImport.length,singleton);
assert.equal(source.split('/*__ACTUAL_SHARED_POWER_SOURCE__*/').length,2);
source=source.replace('/*__ACTUAL_SHARED_POWER_SOURCE__*/',sharedBody);
const sharedSourceBindings={exactBody:sharedBody,excludedImport:sharedImport,excludedInactiveSingleton:sharedBytes.slice(singleton),scope:'Actual complete public source body; only inactive singleton IPC wiring excluded; original factory, admission and current getter unchanged'};
const selectedFlag = process.argv.indexOf('--case'), selectedCase = selectedFlag < 0 ? null : process.argv[selectedFlag + 1];
const declaredNames = [...originalSource.matchAll(/await test\('([^']+)'/g)].map(match => match[1]);
if (selectedFlag >= 0) assert(declaredNames.includes(selectedCase), 'POWER_RESUME_UNKNOWN_SELECTED_CASE');
if(appSleepPlanOnly&&selectedCase)assert(selectedCase.startsWith(appSleepConfirmationOnly?'actual App sleep plan confirmation ':'actual App sleep plan '),'POWER_RESUME_SLEEP_PLAN_SELECTION_MISMATCH');
if (appFirstReadyNegative) assert.equal(selectedCase, 'actual App retains resume-ready before its first known Ready and leaves cold Ready idle');
const appSchedulerOnly = process.argv.includes('--app-scheduler-only');
assert(!(appSchedulerOnly && selectedCase), 'POWER_RESUME_SELECTION_CONFLICT');
const serialNegative = process.argv.includes('--legacy-serial-negative');
if (serialNegative) {
  assert.equal(selectedCase, 'a pending old generation cannot block its successor');
  const product = fs.readFileSync(path.join(repo, 'src/bridge/powerResume.ts'), 'utf8');
  const anchor = 'export function createPowerResumeQueue() {';
  const end = product.indexOf('\nconst defaultSleep =', product.indexOf(anchor));
  assert(product.split(anchor).length === 2 && end > product.indexOf(anchor));
  // The predecessor App serialized all generations through this Promise chain.
  // Keep the same owner checks: only the obsolete cross-generation wait changes.
  const original = product.slice(product.indexOf(anchor), end);
  const serial = original.replace('  let latestGeneration = 0;', '  let latestGeneration = 0; let predecessor = Promise.resolve();')
    .replace('next.task = Promise.resolve().then(async () => {', 'next.task = predecessor.then(async () => {')
    .replace('      return next.task;', '      predecessor = next.task.catch(() => {}); return next.task;');
  assert.notEqual(serial, original);
  const candidate = product.replace(original, serial).replaceAll('export ', '');
  const importLine = "import { createPowerResumeQueue, runPowerResumeTransaction } from '@/bridge/powerResume';";
  assert.equal(source.split(importLine).length, 2);
  source = source.replace(importLine, candidate);
}
const caseCount = declaredNames.length;
const optionalSleepCases=declaredNames.filter(name=>name.startsWith('actual App sleep plan ')).length;
const optionalSleepConfirmationCases=declaredNames.filter(name=>name.startsWith('actual App sleep plan confirmation ')).length;
const optionalContinuationCases=declaredNames.filter(name=>name.startsWith('actual App continuation ')).length;
const appContinuationOnly=process.argv.includes('--app-continuation-only');
const appSharedSourceOnly=process.argv.includes('--app-shared-source-only');
const optionalSharedCases=declaredNames.filter(name=>name.startsWith('actual App shared source ')).length;
assert.equal(caseCount-optionalContinuationCases-optionalSharedCases-optionalSleepCases, 23, 'POWER_RESUME_ORIGINAL_CASE_COUNT_CHANGED');
assert.equal(optionalSharedCases,23,'POWER_RESUME_SHARED_CASE_COUNT_CHANGED');
assert.equal(optionalContinuationCases, 5, 'POWER_RESUME_CONTINUATION_CASE_COUNT_CHANGED');
assert([appSchedulerOnly,appContinuationOnly,appSharedSourceOnly,appSleepPlanOnly].filter(Boolean).length<=1,'POWER_RESUME_SELECTION_CONFLICT');
const built = await auditedPowerResumeBuild(scope, {
  stdin: { contents: source, resolveDir: repo, sourcefile: 'tools/power_resume_selftest.ts', loader: 'ts' },
  bundle: true, write: false, platform: 'node', format: 'esm', target: 'node18',
  alias: { '@': path.join(repo, 'src') }, logLevel: 'silent',
});
const importUrl = originalPowerResumeDataUrl(scope, built);
await import(importUrl);
finishPowerResumeTools(scope, {
  schemaVersion: 1, domain: 'sleep', status: 'PASS', originalSelftestExecuted: true,
  productSourceExecuted: !serialNegative && !appFirstReadyNegative, sourceAssertionsMigrated: true,
  originalBehaviorAssertionsUnchanged: true, sourceAssertionMigration: 'Trace the current atomic Native commit, separate Fan observation and manual admission, and use the shared queue and Native-confirmed F5 for remount recovery',
  originalMainAwaited: true, declaredCases: caseCount, casesExecuted: selectedCase ? 1 : appSleepConfirmationOnly ? optionalSleepConfirmationCases : appSleepPlanOnly ? optionalSleepCases : appSharedSourceOnly ? optionalSharedCases : appContinuationOnly ? optionalContinuationCases : appSchedulerOnly ? 4 : 23, selectedCase, serialNegative,
  appContinuationOnly,optionalContinuationCases,appSharedSourceOnly,optionalSharedCases,appSleepPlanOnly,optionalSleepCases,appSleepConfirmationOnly,optionalSleepConfirmationCases,sharedSourceBindings,originalDefaultCases:23,
  appFirstReadyNegative, appSchedulerOnly, appSourceBindings: appPrepared.bindings,
  operatingSystemSleepPerformed: false, fullApplicationValidated: false,
});
