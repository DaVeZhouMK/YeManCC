import assert from 'node:assert/strict';
import path from 'node:path';

const pid = value => Number.isInteger(value) && value > 0 && value <= 0xffffffff;
const time = value => typeof value === 'string' && /^[1-9][0-9]{16,19}$/.test(value);
const key = value => path.win32.resolve(value).toLowerCase();

// Both expected-failure arms still require a clean, independently owned runtime.
export function validateBrowserOwnership(raw, launcher, runtimeRoot) {
  assert(typeof runtimeRoot === 'string' && path.win32.isAbsolute(runtimeRoot), 'EXACT_RUNTIME_ROOT_REQUIRED');
  assert.equal(launcher.workerAssignedBeforeResume, true, 'WORKER_JOB_ASSIGNMENT_REQUIRED');
  assert.equal(launcher.jobBreakawayDisabled, true, 'JOB_BREAKAWAY_MUST_BE_DISABLED');
  assert.equal(launcher.jobZeroBeforeClose, true, 'OBSERVED_JOB_ZERO_BEFORE_CLOSE_REQUIRED');
  assert.equal(launcher.activeProcessesBeforeJobClose, 0, 'OWNED_JOB_REMAINS_ACTIVE');
  assert.equal(launcher.forcedTermination, false, 'FORCED_CLOSE_IS_NOT_NATURAL_JOB_DRAIN');
  const worker = raw.workerOwnership;
  assert(worker && worker.inOwnedJob === true && pid(worker.pid) && worker.pid === launcher.workerPid, 'EXACT_WORKER_JOB_IDENTITY_REQUIRED');
  assert(time(worker.startFileTimeUtc) && worker.startFileTimeUtc === launcher.workerStartFileTimeUtc, 'EXACT_WORKER_BIRTH_REQUIRED');
  assert(typeof worker.desktopName === 'string' && worker.desktopName && worker.desktopName === launcher.desktopName, 'EXACT_WORKER_PRIVATE_DESKTOP_REQUIRED');
  assert(Array.isArray(raw.browserOwnership) && raw.browserOwnership.length > 0, 'ACTUAL_BROWSER_JOB_IDENTITIES_REQUIRED');
  assert(Number.isInteger(launcher.totalProcessesInJob) && launcher.totalProcessesInJob >= raw.browserOwnership.length + 1, 'JOB_ACCOUNTING_CANNOT_COVER_OBSERVED_PROCESSES');
  const expected = key(path.win32.join(runtimeRoot, 'msedgewebview2.exe'));
  const seen = new Set([worker.pid]);
  for (const browser of raw.browserOwnership) {
    assert(pid(browser.pid) && !seen.has(browser.pid), 'INVALID_OR_DUPLICATE_OWNED_BROWSER_PID');
    seen.add(browser.pid);
    assert(time(browser.startFileTimeUtc) && BigInt(browser.startFileTimeUtc) >= BigInt(worker.startFileTimeUtc), 'BROWSER_PREDATES_WORKER');
    assert(typeof browser.image === 'string' && key(browser.image) === expected, 'BROWSER_RUNTIME_IMAGE_MISMATCH');
    assert(browser.inOwnedJob === true && browser.beforePageMapping === true && browser.exclusiveProfile === true, 'EARLY_BROWSER_JOB_OWNERSHIP_REQUIRED');
    assert.equal(browser.controllerParentDesktop, worker.desktopName, 'ACTUAL_CONTROLLER_PARENT_DESKTOP_MISMATCH');
  }
  assert.equal(raw.browserPid, raw.browserOwnership[0].pid, 'FIRST_BROWSER_PID_MISMATCH');
  assert.equal(raw.browserObservedImage, raw.browserOwnership[0].image, 'FIRST_BROWSER_IMAGE_MISMATCH');
  return {distinctOwnedBrowsers: raw.browserOwnership.length, activeProcessesBeforeJobClose: 0, workerPid: worker.pid};
}
