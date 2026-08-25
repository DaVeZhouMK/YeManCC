import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const native = readFileSync(resolve(process.cwd(), 'native/main.cpp'), 'utf8');

function requireNative(token: string): void {
  assert.ok(native.includes(token), `missing sleep policy: ${token}`);
}

function rejectNative(token: string): void {
  assert.ok(!native.includes(token), `obsolete sleep policy remains: ${token}`);
}

function section(start: string, end: string): string {
  const begin = native.indexOf(start);
  assert.ok(begin >= 0, `missing section start: ${start}`);
  const finish = native.indexOf(end, begin + start.length);
  assert.ok(finish > begin, `missing section end: ${end}`);
  return native.slice(begin, finish);
}

// One transaction, two evidence reasons, one retry timer.
requireNative('enum class SgRetryKind : uint8_t { None, EntryFailure, UnexpectedWake };');
requireNative('#define SG_RETRY_TIMER_ID 0xA20C');
rejectNative('SG_EXTERNAL_DEVICE_RETRY_TIMER_ID');
rejectNative('SgSleepTaskPhase');
rejectNative('SgExternalDeviceWakeState');
rejectNative('SG_ENTRY_RETRY_CONFIRM_TIMEOUT_MS');
rejectNative('SG_EXTERNAL_SUSPEND_CONFIRM_TIMEOUT_MS');
rejectNative('SG_MAX_EXTERNAL_WAKE_CYCLES');
rejectNative('completedSleepCycles');
rejectNative('awaitingSuspendConfirmation');
rejectNative('SgRetryKind::NonUserWake');
rejectNative('WM_SG_S0_REENTER');
rejectNative('sgResumeGlobalSuspendedLargeProcesses');

// Sleep pause owns one exact PID lease. Manual pause remains independent.
requireNative('static const std::wstring SG_SLEEP_LEASE_DIR = SG_DIR + L"\\\\suspended";');
requireNative('static const std::wstring SG_MANUAL_DIR = SG_DIR + L"\\\\manual-suspended";');
requireNative('target.pid, SG_SLEEP_LEASE_DIR, target.processCreated, &target,');
requireNative('reason=manual-lease');
rejectNative('target.pid, SG_MANUAL_DIR, target.processCreated, &target');

const markerWriter = section('static bool sgWriteProcessMarker(', 'struct SgSuspendResult');
assert.ok(markerWriter.includes('|generation='),
  'sleep marker must persist power generation');

// main.cpp may be checked out with either LF or CRLF. Locate the definition
// by structure rather than a newline-specific literal so this guard remains
// valid after Git/editor line-ending normalization.
const resumeDefinitionMatch = native.match(
  /static SgResumeResult sgResumeSleepTarget\(\s*unsigned long long expectedGeneration,\s*bool focusResumedGame\)\s*\{/,
);
const resumeDefinition = resumeDefinitionMatch?.index ?? -1;
assert.ok(resumeDefinition >= 0, 'missing sgResumeSleepTarget definition');
const resumeSleepTarget = native.slice(
  resumeDefinition,
  native.indexOf('// Capture and freeze the current valve lease synchronously.', resumeDefinition),
);
requireNative('target.powerGeneration != expectedGeneration');
requireNative('sgMarkerGeneration(SG_SLEEP_LEASE_DIR, target.pid) != expectedGeneration');
requireNative('actualCreated != target.processCreated');
requireNative('SG_SLEEP_RESUME_RETRY_DELAYS_MS[] = {0ULL, 100ULL, 300ULL, 600ULL}');
assert.ok(!resumeSleepTarget.includes('SG_MANUAL_DIR'));
assert.ok(!resumeSleepTarget.includes('nativeValveAcquire'));

// The one dispatcher waits for the original pause worker only on EntryFailure.
requireNative('SG_ENTRY_RETRY_DELAYS_MS[] = {500ULL, 1000ULL, 2000ULL}');
requireNative('SG_PAUSE_READY_WAIT_MAX_MS = 2000ULL');
requireNative('SG_PAUSE_READY_POLL_MS = 50U');
requireNative('SetSuspendState(FALSE, FALSE, FALSE)');
requireNative('SE_SHUTDOWN_NAME');
const retryDispatch = section(
  'static void sgDispatchSameModeRetry() {',
  'static void sgStartSleepRetry(',
);
assert.ok(
  retryDispatch.indexOf('g_sgPauseWorkCompletedGeneration.load') >= 0 &&
  retryDispatch.indexOf('g_sgPauseWorkCompletedGeneration.load') <
    retryDispatch.indexOf('sgRequestSystemSleep()'),
  'EntryFailure must wait for pause completion before requesting sleep',
);
assert.ok(
  retryDispatch.indexOf('sgMarkInternalSleepRequest(kind)') >= 0 &&
  retryDispatch.indexOf('sgMarkInternalSleepRequest(kind)') <
    retryDispatch.indexOf('sgRequestSystemSleep()'),
  'every internal retry must be tagged before SetSuspendState',
);
assert.ok(retryDispatch.includes('if (request.accepted) return;'),
  'accepted SetSuspendState must wait for real Windows evidence');
rejectNative('retry-confirm-timeout');

// Physical path 1: 506 Reason=1/3 then 507 Reason=7/8 within 2 seconds.
requireNative('SG_S0_REASON7_FAILURE_WINDOW_MS = 2000ULL');
requireNative('if (wakeReason != 7 && wakeReason != 8) return false;');
requireNative('const bool userIntent506 = sleepReason == 1 || sleepReason == 3;');
requireNative('sgStartSleepRetry(SgRetryKind::EntryFailure, "s0-kernel-entry-failure")');
requireNative('sgAdvanceSleepRetry("s0-retry-entry-failure")');
const s0Wake = section('case WM_SG_S0_WAKE:', 'case WM_SG_S4_WAKE:');
assert.ok(
  s0Wake.indexOf('if (entryFailure) {') >= 0 &&
  s0Wake.indexOf('if (entryFailure) {') <
    s0Wake.indexOf('g_sgLastS0WakeReason == 5'),
  'EntryFailure must win before Reason=5 processing',
);

// Physical path 2: 120-second user sleep intent plus code=7 or Reason=5.
requireNative('SG_USER_STANDBY_DEVICE_DELAY_MS = 120000ULL');
requireNative('g_sgTask.unexpectedWakeConsumed = true;');
requireNative('sgStartSleepRetry(SgRetryKind::UnexpectedWake, "external-device-wake")');
const externalEvaluate = section(
  'static void sgEvaluateExternalDeviceWake()',
  'static void sgNoteExternalDeviceNodeChange()',
);
assert.ok(!externalEvaluate.includes('g_sgRepairEligible'));
assert.ok(!externalEvaluate.includes('g_sgTask.mode'));
assert.ok(externalEvaluate.includes('g_sgTask.unexpectedWakeConsumed'));
const deviceChange = section('case WM_DEVICECHANGE:', 'case WM_KEYDOWN:');
assert.ok(deviceChange.includes('DBT_DEVNODES_CHANGED'));
assert.ok(deviceChange.includes('sgNoteExternalDeviceNodeChange();'));
const acdcOnly = section(
  'static void sgNoteExternalDeviceAcDcChange()',
  'static void sgNoteExternalDeviceKernel507Reason5()',
);
assert.ok(!acdcOnly.includes('sgEvaluateExternalDeviceWake();'),
  'AC/DC alone must remain diagnostic-only');

// Automatic wake never restores the lease. Explicit user/S4/query-cancel/failure does.
requireNative('sleep automatic wake held for explicit user resume');
requireNative('handlePowerResumeNotification(SgWork::WakeSuspend, "resume_suspend")');
requireNative('sgAbortSleepIntent("s0-user-power-button-wake")');
requireNative('sgAbortSleepIntent("kernel-s4-wake")');
requireNative('sgAbortSleepIntent("query-canceled")');
requireNative('static void sgFinishSleepRetryFailure(const char* reason)');
requireNative('sgResumeTrackedAll();');

// Deterministic replay of the two absolute-priority contracts.
type RetryKind = 'none' | 'entry-failure' | 'unexpected-wake';
type Model = {
  paused: boolean;
  userIntentAt: number;
  retryKind: RetryKind;
  retryAttempt: number;
  unexpectedConsumed: boolean;
  resumeCount: number;
};

const createModel = (): Model => ({
  paused: false,
  userIntentAt: -1,
  retryKind: 'none',
  retryAttempt: 0,
  unexpectedConsumed: false,
  resumeCount: 0,
});
const userSleep = (s: Model, at: number): void => {
  s.paused = true;
  s.userIntentAt = at;
  s.retryKind = 'none';
  s.retryAttempt = 0;
  s.unexpectedConsumed = false;
};
const entryExit = (s: Model, at: number, reason: number): boolean => {
  if ((reason !== 7 && reason !== 8) || at - s.userIntentAt > 2000) return false;
  s.retryKind = 'entry-failure';
  return true;
};
const unexpectedWake = (s: Model, at: number, code7: boolean, reason507: number): boolean => {
  if (s.unexpectedConsumed || s.retryKind !== 'none' ||
      at - s.userIntentAt < 120000 || (!code7 && reason507 !== 5)) return false;
  s.unexpectedConsumed = true;
  s.retryKind = 'unexpected-wake';
  return true;
};
const userWake = (s: Model): void => {
  s.retryKind = 'none';
  if (s.paused) {
    s.paused = false;
    s.resumeCount += 1;
  }
};

const sd = createModel();
userSleep(sd, 0);
assert.equal(entryExit(sd, 1007, 8), true);
assert.equal(sd.retryKind, 'entry-failure');
assert.equal(sd.paused, true, 'SD Gundam must stay paused during re-sleep');
userWake(sd);
assert.equal(sd.paused, false);
assert.equal(sd.resumeCount, 1);

const usb4 = createModel();
userSleep(usb4, 0);
assert.equal(unexpectedWake(usb4, 192042, true, -1), true);
assert.equal(usb4.retryKind, 'unexpected-wake');
assert.equal(usb4.paused, true, 'USB4 wake must not resume the game');
assert.equal(unexpectedWake(usb4, 192050, true, 5), false,
  'a burst of code=7/Reason=5 must not start a second retry');
userWake(usb4);
assert.equal(usb4.paused, false);
assert.equal(usb4.resumeCount, 1);

assert.equal(entryExit(createModel(), 2001, 7), false);
const tooEarly = createModel();
userSleep(tooEarly, 0);
assert.equal(unexpectedWake(tooEarly, 119999, true, -1), false);

console.log('sleep task policy self-test: PASS');
