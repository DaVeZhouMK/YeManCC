import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const native = readFileSync(resolve(process.cwd(), 'native/main.cpp'), 'utf8');

function requireNative(token: string): void {
  assert.ok(native.includes(token), `missing SleepTask policy: ${token}`);
}

function rejectNative(token: string): void {
  assert.ok(!native.includes(token), `obsolete SleepTask policy remains: ${token}`);
}

function section(start: string, end: string): string {
  const begin = native.indexOf(start);
  assert.ok(begin >= 0, `missing section start: ${start}`);
  const finish = native.indexOf(end, begin + start.length);
  assert.ok(finish > begin, `missing section end: ${end}`);
  return native.slice(begin, finish);
}

// Keep only the two evidence-driven repair paths. Broad wake inference and
// global process recovery are intentionally absent.
requireNative('enum class SgRetryKind : uint8_t { None, EntryFailure };');
rejectNative('SgRetryKind::NonUserWake');
rejectNative('sgEntryFailureWindowMs');
rejectNative('fast-entry-failure');
rejectNative('WM_SG_S0_REENTER');
rejectNative('sgMarkModernStandbyReentry');
rejectNative('sgResumeGlobalSuspendedLargeProcesses');
rejectNative('SG_GLOBAL_RECOVERY_MIN_WS');

// Sleep pause has one dedicated ownership lease. Manual pause remains a
// separate user-owned feature and cannot be consumed by a sleep wake.
requireNative('static const std::wstring SG_SLEEP_LEASE_DIR = SG_DIR + L"\\\\suspended";');
requireNative('static const std::wstring SG_MANUAL_DIR = SG_DIR + L"\\\\manual-suspended";');
requireNative('target.pid, SG_SLEEP_LEASE_DIR, target.processCreated, &target,');
requireNative('false, generation);');
requireNative('reason=manual-lease');
rejectNative('target.pid, SG_MANUAL_DIR, target.processCreated, &target');

const markerWriter = section(
  'static bool sgWriteProcessMarker(',
  'struct SgSuspendResult',
);
assert.ok(markerWriter.includes('|generation='),
  'sleep marker must persist its power generation');
const suspendSignature = 'static json sgSuspendGameByPidUnlocked(\n    DWORD rootPid,';
const suspendDefinition = native.indexOf(
  suspendSignature,
  native.indexOf(suspendSignature) + suspendSignature.length,
);
assert.ok(suspendDefinition >= 0, 'missing sgSuspendGameByPidUnlocked definition');
const suspendByPid = native.slice(
  suspendDefinition,
  native.indexOf('static ULONGLONG sgMarkerCreated(', suspendDefinition),
);
assert.ok(
  suspendByPid.indexOf('sgWriteProcessMarker(markerDir, pid, "suspended", leaseGeneration)') >= 0 &&
  suspendByPid.indexOf('sgWriteProcessMarker(markerDir, pid, "suspended", leaseGeneration)') <
    suspendByPid.indexOf('fnNtSuspend(h)'),
  'the crash-recovery marker must exist before NtSuspendProcess',
);

// Resume is exact PID + creation time + generation, bounded, and never falls
// back to the currently detected game or a system-wide scan.
requireNative('SG_SLEEP_RESUME_RETRY_DELAYS_MS[] = {0ULL, 100ULL, 300ULL, 600ULL}');
const resumeSignature =
  'static SgResumeResult sgResumeSleepTarget(\n    unsigned long long expectedGeneration';
const resumeDefinition = native.indexOf(
  resumeSignature,
  native.indexOf(resumeSignature) + resumeSignature.length,
);
assert.ok(resumeDefinition >= 0, 'missing sgResumeSleepTarget definition');
const resumeSleepTarget = native.slice(
  resumeDefinition,
  native.indexOf('// Capture and freeze the current valve lease synchronously.', resumeDefinition),
);
requireNative('target.powerGeneration != expectedGeneration');
requireNative('sgMarkerGeneration(SG_SLEEP_LEASE_DIR, target.pid) != expectedGeneration');
requireNative('actualCreated != target.processCreated');
assert.ok(resumeSleepTarget.includes('for (const ULONGLONG delayMs : SG_SLEEP_RESUME_RETRY_DELAYS_MS)'),
  'sleep resume must use bounded retries');
assert.ok(!resumeSleepTarget.includes('SG_MANUAL_DIR'),
  'sleep wake must not resume manual pause markers');
assert.ok(!resumeSleepTarget.includes('nativeValveAcquire'),
  'sleep wake must not replace the leased PID with the current detector result');

// Work ordering: a queued pause or resume is never discarded. EntryFailure
// waits up to two seconds for the pause worker before SetSuspendState.
const queueSignature = 'static void sgQueueWork(';
const queueDefinition = native.indexOf(
  queueSignature,
  native.indexOf(queueSignature) + queueSignature.length,
);
assert.ok(queueDefinition >= 0, 'missing sgQueueWork definition');
const queueWork = native.slice(
  queueDefinition,
  native.indexOf('// A suspend query can be vetoed', queueDefinition),
);
assert.ok(!queueWork.includes('pop_front'), 'work queue must not discard its oldest item');
requireNative('g_sgPauseWorkCompletedGeneration.store(');
requireNative('SG_PAUSE_READY_WAIT_MAX_MS = 2000ULL');
requireNative('SG_PAUSE_READY_POLL_MS = 50U');
requireNative('SG_ENTRY_RETRY_CONFIRM_TIMEOUT_MS = 60000U');
const entryDispatch = section(
  'static void sgDispatchSameModeRetry() {',
  'static void sgRequestSleepRetry(',
);
assert.ok(
  entryDispatch.indexOf('g_sgPauseWorkCompletedGeneration.load') >= 0 &&
  entryDispatch.indexOf('g_sgPauseWorkCompletedGeneration.load') <
    entryDispatch.indexOf('sgRequestSystemSleep()'),
  'EntryFailure must wait for pause completion before requesting sleep',
);
assert.ok(
  entryDispatch.indexOf('sgMarkInternalSleepRequest(SgInternalSleepRequestKind::EntryFailure)') >= 0 &&
  entryDispatch.indexOf('sgMarkInternalSleepRequest(SgInternalSleepRequestKind::EntryFailure)') <
    entryDispatch.indexOf('sgRequestSystemSleep()'),
  'EntryFailure must tag its internal request before SetSuspendState',
);
requireNative('SetSuspendState(FALSE, FALSE, FALSE)');
requireNative('SE_SHUTDOWN_NAME');
requireNative('SG_ENTRY_RETRY_DELAYS_MS[] = {500ULL, 1000ULL, 2000ULL}');
assert.ok(entryDispatch.includes('sgAdvanceRetry("retry-confirm-timeout")'),
  'an accepted retry without a suspend/failure boundary must not wait forever');

// Physical path 1: user 506 Reason=1/3 followed within two seconds by 507
// Reason=7/8. It wins before Reason=5 and keeps the game paused during retry.
requireNative('SG_S0_REASON7_FAILURE_WINDOW_MS = 2000ULL');
requireNative('if (wakeReason != 7 && wakeReason != 8) return false;');
requireNative('const bool userIntent506 = sleepReason == 1 || sleepReason == 3;');
const s0Wake = section('case WM_SG_S0_WAKE:', 'case WM_SG_S4_WAKE:');
assert.ok(
  s0Wake.indexOf('if (entryFailure) {') >= 0 &&
  s0Wake.indexOf('if (entryFailure) {') < s0Wake.indexOf('g_sgLastS0WakeReason == 5'),
  'Reason=7/8 EntryFailure must win before unexpected-wake Reason=5',
);
requireNative('sgHandleModernStandbyWake(false);');
requireNative('sgAdvanceRetry("s0-retry-entry-failure")');
requireNative('sgQueueWork(SgWork::WakeSuspend, generation);');

// Physical path 2: after 120 seconds, AMD USB4 device-node code=7 or
// Kernel-Power 507 Reason=5 starts the independent unexpected-wake retry.
requireNative('SG_USER_STANDBY_DEVICE_DELAY_MS = 120000ULL');
requireNative('g_sgExternalDeviceWake.deviceNodeChangeSeen ||');
requireNative('g_sgExternalDeviceWake.kernel507Reason5Seen;');
const externalEvaluate = section(
  'static void sgEvaluateExternalDeviceWake()',
  'static void sgNoteExternalDeviceNodeChange()',
);
assert.ok(!externalEvaluate.includes('g_sgRepairEligible'),
  'unexpected-wake evidence must not depend on EntryFailure eligibility');
assert.ok(!externalEvaluate.includes('g_sgTask.mode'),
  'unexpected-wake evidence must not depend on SleepTask mode');
const deviceChange = section('case WM_DEVICECHANGE:', 'case WM_KEYDOWN:');
assert.ok(deviceChange.includes('DBT_DEVNODES_CHANGED'));
assert.ok(deviceChange.includes('sgNoteExternalDeviceNodeChange();'));
const acdcOnly = section(
  'static void sgNoteExternalDeviceAcDcChange()',
  'static void sgNoteExternalDeviceKernel507Reason5()',
);
assert.ok(!acdcOnly.includes('sgEvaluateExternalDeviceWake();'),
  'AC/DC alone must remain diagnostic-only');
requireNative('SG_MAX_EXTERNAL_WAKE_CYCLES = 2');
requireNative('sgConfirmExternalDeviceWakeSleepBoundary();');

// Any retry exhaustion releases the application lifecycle and queues exact
// PID recovery. It may retain a failed marker, but cannot strand the app in
// Resuming or silently resume a manual pause.
requireNative('static void sgFinishExternalDeviceWakeFailure(const char* source)');
requireNative('sgFinishExternalDeviceWakeFailure("unexpected-wake-suspend-confirm-timeout")');
requireNative('sgFinishExternalDeviceWakeFailure("unexpected-wake-requests-exhausted")');
const realWake = section(
  'static void sgRealWake(const char* src, unsigned long long expectedGeneration)',
  'static SgSleepMode sgPowerButtonSleepMode()',
);
assert.ok(realWake.includes('sgResumeSleepTarget(expectedGeneration, true)'));
assert.ok(!realWake.includes('if (stillOwned) return'),
  'failed exact resume must not block lifecycle cleanup forever');
assert.ok(!realWake.includes('SG_MANUAL_DIR'),
  'normal sleep wake must not consume manual pause');
assert.ok(realWake.includes('const bool taskMatches = g_sgTask.generation == expectedGeneration;'),
  'a stale wake must not clear the current generation task');

// Automatic wake remains paused; explicit S3 resume, 507 Reason=1, S4 wake,
// query cancellation, retry exhaustion, and process exit are final recovery.
requireNative('sleep automatic wake held for explicit user resume');
requireNative('handlePowerResumeNotification(SgWork::WakeSuspend, "resume_suspend")');
requireNative('sgAbortSleepIntent("s0-user-power-button-wake")');
requireNative('sgAbortSleepIntent("kernel-s4-wake")');
requireNative('sgAbortSleepIntent("query-canceled")');
requireNative('sgResumeTrackedAll();');

// Deterministic replay of the two physical acceptance contracts.
const entryFailureDecision = (reason506: number, deltaMs: number, reason507: number): boolean =>
  (reason506 === 1 || reason506 === 3) &&
  (reason507 === 7 || reason507 === 8) &&
  deltaMs >= 0 && deltaMs <= 2000;
assert.equal(entryFailureDecision(1, 3, 7), true);
assert.equal(entryFailureDecision(1, 1007, 8), true);
assert.equal(entryFailureDecision(3, 2000, 7), true);
assert.equal(entryFailureDecision(1, 2001, 7), false);

const unexpectedWakeDecision = (
  ageMs: number,
  deviceNodeCode7: boolean,
  reason507: number,
): boolean => ageMs >= 120000 && (deviceNodeCode7 || reason507 === 5);
assert.equal(unexpectedWakeDecision(192042, true, -1), true);
assert.equal(unexpectedWakeDecision(192042, false, 5), true);
assert.equal(unexpectedWakeDecision(119999, true, -1), false);

console.log('sleep task policy self-test: PASS');
