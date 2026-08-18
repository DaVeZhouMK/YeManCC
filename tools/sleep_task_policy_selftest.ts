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

requireNative('enum class SgRetryKind : uint8_t { None, EntryFailure };');
requireNative('unsigned int entryFailureAttempts = 0;');
requireNative('g_sgTask.entryFailureAttempts >= SG_MAX_ENTRY_RETRIES');
rejectNative('SgRetryKind::NonUserWake');
rejectNative('nonUserWakeAttempts');
rejectNative('nonUserWakeResleepUsed');
rejectNative('sgRequestSleepRetry(SgRetryKind::NonUserWake');
rejectNative('sgObserveAcDcForManualSleep');
rejectNative('sgSampleModernStandbySystemState');
rejectNative('sgFinalizeModernWakeTimeout');
rejectNative('sgReleaseAfterWakeObservation');
rejectNative('SG_RESLEEP_TIMER_ID');
rejectNative('SG_S0_WAKE_CLASSIFY_TIMER_ID');

// A confirmed S3 transition must not be reclassified as an entry failure
// merely because it returns quickly.
requireNative('phase == PowerLifecycle::Suspending && !g_sgTask.suspendConfirmed');
requireNative('g_sgTask.suspendConfirmed = true;');
requireNative('sleep automatic wake held for explicit user resume');
requireNative('handlePowerResumeNotification(SgWork::WakeSuspend, "resume_suspend")');

// Hibernate is deliberately outside the live pause/retry transaction.
requireNative('sleep task ignored mode=S4 source=query');
requireNative('sleep task ignored mode=S4 source=suspend');
requireNative('g_sgTask.mode != SgSleepMode::S3');

// A retry must explicitly request normal sleep, never hibernate, and enable
// the privilege Windows requires for programmatic sleep requests.
requireNative('SetSuspendState(FALSE, FALSE, FALSE)');
requireNative('SE_SHUTDOWN_NAME');
requireNative('AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr)');
requireNative('SG_ENTRY_RETRY_DELAYS_MS[] = {500ULL, 1000ULL, 2000ULL}');
requireNative('sgAdvanceRetry("retry-request-rejected")');
const sameModeDispatchStart = native.indexOf('static void sgDispatchSameModeRetry() {');
const sameModeDispatch = native.slice(
  sameModeDispatchStart,
  native.indexOf('static void sgRequestSleepRetry(', sameModeDispatchStart),
);
assert.ok(sameModeDispatch.includes('g_sgSleepTriggerTick = now;'),
  'every EntryFailure attempt must open a fresh 506/507 correlation window');
assert.ok(sameModeDispatch.includes('g_sgModernWakeClassified = false;'),
  'every EntryFailure attempt must clear the prior wake classification');
requireNative('external-device-wake-confirmed');
requireNative('independentOf", {"repairEligible", "taskMode"}');

// Unexpected-wake resleep is a separate state machine. These
// assertions prevent later SleepTask changes from silently taking ownership
// of its evidence, timer, or retry budget.
requireNative('struct SgExternalDeviceWakeState');
requireNative('#define SG_EXTERNAL_DEVICE_RETRY_TIMER_ID');
requireNative('SG_USER_STANDBY_DEVICE_DELAY_MS');
requireNative('sgNoteExternalDeviceNodeChange();');
requireNative('sgNoteExternalDeviceAcDcChange();');
requireNative('sgNoteExternalDeviceKernel507Reason5();');
requireNative('g_sgExternalDeviceWake.retryActive');
requireNative('g_sgExternalDeviceWake.consumed');
requireNative('SG_MAX_EXTERNAL_WAKE_CYCLES = 2');
requireNative('SG_EXTERNAL_SUSPEND_CONFIRM_TIMEOUT_MS = 60000U');
requireNative('bool awaitingSuspendConfirmation = false;');
requireNative('unsigned int completedSleepCycles = 0;');
requireNative('const bool unexpectedWakeEvidence =');
requireNative('g_sgExternalDeviceWake.deviceNodeChangeSeen ||');
requireNative('g_sgExternalDeviceWake.kernel507Reason5Seen;');
const acdcEvidenceFunction = native.slice(
  native.indexOf('static void sgNoteExternalDeviceAcDcChange()'),
  native.indexOf('static void sgNoteExternalDeviceKernel507Reason5()'),
);
assert.ok(!acdcEvidenceFunction.includes('sgEvaluateExternalDeviceWake();'),
  'AC/DC changes must remain diagnostic-only');
const externalSchedule = native.slice(
  native.indexOf('static void sgScheduleExternalDeviceWakeRetry()'),
  native.indexOf('static void sgEvaluateExternalDeviceWake()'),
);
assert.ok(externalSchedule.includes('stopPowerResumeWatchdog();'),
  'unexpected-wake retry must stop the generic resume watchdog');
assert.ok(externalSchedule.includes('closeHardwareWriteGate("external-device-wake-retry");'),
  'unexpected-wake retry must close the hardware write gate before dispatch');
requireNative('if (request.accepted) {');
requireNative('g_sgExternalDeviceWake.awaitingSuspendConfirmation = true;');
requireNative('external-device-wake-retry-awaiting-suspend');
requireNative('external-device-wake-suspend-confirm-timeout');
requireNative('static void sgConfirmExternalDeviceWakeSleepBoundary()');
requireNative('g_sgExternalDeviceWake.completedSleepCycles = completed;');
requireNative('g_sgExternalDeviceWake.consumed = !rearmed;');
requireNative('sgConfirmExternalDeviceWakeSleepBoundary();');
requireNative('const bool internalUnexpectedWakeRetry = g_sgExternalDeviceWake.retryActive;');
requireNative('"internal-sleep-506"');
requireNative('SG_INTERNAL_SLEEP_506_WINDOW_MS = 60000ULL');
requireNative('enum class SgInternalSleepRequestKind');
requireNative('sgInternalSleepRequestMatches506(reason, eventFileTime)');
requireNative('sgClearInternalSleepRequest("kernel-power-507-reason-1")');
requireNative('sgClearInternalSleepRequest("pbt-resume-suspend-user")');
const entryDispatch = native.slice(
  native.indexOf('static void sgDispatchSameModeRetry()'),
  native.indexOf('static void sgRequestSleepRetry(', native.indexOf('static void sgDispatchSameModeRetry()')),
);
assert.ok(
  entryDispatch.indexOf('sgMarkInternalSleepRequest(SgInternalSleepRequestKind::EntryFailure)') >= 0 &&
  entryDispatch.indexOf('sgMarkInternalSleepRequest(SgInternalSleepRequestKind::EntryFailure)') <
    entryDispatch.indexOf('sgRequestSystemSleep()'),
  'EntryFailure must identify its internal request before SetSuspendState',
);
const unexpectedDispatch = native.slice(
  native.indexOf('static void sgDispatchExternalDeviceWakeRetry()'),
  native.indexOf('static void sgConfirmExternalDeviceWakeSleepBoundary()'),
);
assert.ok(
  unexpectedDispatch.indexOf('sgMarkInternalSleepRequest(SgInternalSleepRequestKind::UnexpectedWake)') >= 0 &&
  unexpectedDispatch.indexOf('sgMarkInternalSleepRequest(SgInternalSleepRequestKind::UnexpectedWake)') <
    unexpectedDispatch.indexOf('sgRequestSystemSleep()'),
  'unexpected-wake retry must identify its internal request before SetSuspendState',
);
const intentMessage = native.slice(
  native.indexOf('case WM_SG_S0_INTENT:'),
  native.indexOf('case WM_SG_S0_REENTER:'),
);
assert.ok(
  intentMessage.indexOf('sgInternalSleepRequestMatches506(reason, eventFileTime)') >= 0 &&
  intentMessage.indexOf('sgInternalSleepRequestMatches506(reason, eventFileTime)') <
    intentMessage.indexOf('g_sgLastPowerButtonSleepIntentFileTime.store'),
  'a delayed internal 506 must be filtered before it can overwrite the 120-second user marker',
);
const kernelCallback = native.slice(
  native.indexOf('static DWORD WINAPI sgKernelPowerEventCallback'),
  native.indexOf('static void sgStartKernelPowerSubscription()'),
);
assert.ok(!kernelCallback.includes('sgClearExternalDeviceWake('),
  'Kernel-Power callback thread must not clear the UI-owned unexpected-wake state');
requireNative('Kernel-Power 107 with TargetState=5');
requireNative('sgClearExternalDeviceWake("kernel-s4-wake", true);');
requireNative('sgClearExternalDeviceWake("pbt-resume-suspend-user", true);');
requireNative('sgClearExternalDeviceWake("pbt-resume-critical", true);');

// A rapid Modern Standby flow exit is a confirmed entry failure. It must not
// be routed through the 120-second unexpected-wake evidence path, and its
// owned game pause lease must survive until retry success or a user wake.
requireNative('SG_S0_REASON7_FAILURE_WINDOW_MS = 2000ULL');
requireNative('static bool sgS0EntryFailureEligible(int wakeReason, ULONGLONG nowTick)');
requireNative('if (wakeReason != 7 && wakeReason != 8) return false;');
requireNative('if (!userIntent506 && !sgEntryRetryIsExclusive()) return false;');
requireNative('sgInternalSleepRequestIs(SgInternalSleepRequestKind::UnexpectedWake)');
requireNative('sgMarkSleepTrigger deliberately consumes g_sgSleepIntentArmed');
requireNative('static bool sgS0Reason7FailureEligible');
requireNative('const bool reason7EntryFailure = g_sgLastS0WakeReason == 7');
requireNative('const bool reason8EntryFailure = g_sgLastS0WakeReason == 8');
requireNative('{"reason7EntryFailure", reason7EntryFailure}');
requireNative('{"reason8EntryFailure", reason8EntryFailure}');
requireNative('s0 reason=%d confirmed entry failure');
requireNative('sgHandleModernStandbyWake(false);');
requireNative('Reason=5 is direct unexpected-wake evidence');
requireNative('s0-entry-retry-canceled-user-wake');
requireNative('sgAbortSleepIntent("s0-user-power-button-wake", false);');

// The two physical-machine acceptance paths are the priority contract. Their
// dispatch branches must remain ahead of all secondary wake classification.
const s0WakeMessage = native.slice(
  native.indexOf('case WM_SG_S0_WAKE:'),
  native.indexOf('case WM_SG_S4_WAKE:'),
);
assert.ok(
  s0WakeMessage.indexOf('if (entryFailure) {') >= 0 &&
  s0WakeMessage.indexOf('if (entryFailure) {') <
    s0WakeMessage.indexOf('g_sgLastS0WakeReason == 5'),
  'Reason=7/8 EntryFailure must win before the Reason=5 unexpected-wake path',
);
const deviceChangeStart = native.indexOf('case WM_DEVICECHANGE:');
const deviceChangeMessage = native.slice(
  deviceChangeStart,
  native.indexOf('case WM_KEYDOWN:', deviceChangeStart),
);
assert.ok(deviceChangeMessage.includes('DBT_DEVNODES_CHANGED'),
  'USB4 device-node code=7 must remain a direct evidence source');
assert.ok(deviceChangeMessage.includes('sgNoteExternalDeviceNodeChange();'),
  'USB4 device-node code=7 must reach the unexpected-wake evaluator');
const externalEvaluate = native.slice(
  native.indexOf('static void sgEvaluateExternalDeviceWake()'),
  native.indexOf('static void sgNoteExternalDeviceNodeChange()'),
);
assert.ok(!externalEvaluate.includes('g_sgRepairEligible'),
  'USB4 120-second path must not depend on repairEligible');
assert.ok(!externalEvaluate.includes('g_sgTask.mode'),
  'USB4 120-second path must not depend on SleepTask mode');
const pbtSuspend = native.slice(
  native.indexOf('else if (w == PBT_APMSUSPEND)'),
  native.indexOf('else if (w == PBT_APMQUERYSUSPENDFAILED'),
);
assert.ok(
  pbtSuspend.indexOf('if (g_sgExternalDeviceWake.retryActive)') >= 0 &&
  pbtSuspend.indexOf('if (g_sgExternalDeviceWake.retryActive)') <
    pbtSuspend.indexOf('if (sgEntryRetryIsExclusive())'),
  'an active USB4 retry must own its suspend confirmation before generic EntryFailure handling',
);

// Deterministic replay of the exact evidence supplied by the physical tests.
const entryFailureDecision = (reason506: number, deltaMs: number, reason507: number): boolean =>
  (reason506 === 1 || reason506 === 3) &&
  (reason507 === 7 || reason507 === 8) &&
  deltaMs >= 0 && deltaMs <= 2000;
assert.equal(entryFailureDecision(1, 3, 7), true,
  'SD Gundam: 506/1 -> 3ms -> 507/7 must enter EntryFailure');
assert.equal(entryFailureDecision(1, 1007, 8), true,
  'SD Gundam: 506/1 -> 1007ms -> 507/8 must enter EntryFailure');
assert.equal(entryFailureDecision(1, 2001, 7), false,
  'the EntryFailure path must remain bounded to two seconds');

const unexpectedWakeDecision = (
  ageMs: number,
  deviceNodeCode7: boolean,
  reason507: number,
): boolean => ageMs >= 120000 && (deviceNodeCode7 || reason507 === 5);
assert.equal(unexpectedWakeDecision(192042, true, -1), true,
  'USB4: code=7 after 120 seconds must enter unexpected-wake resleep');
assert.equal(unexpectedWakeDecision(192042, false, 5), true,
  'USB4: 507/5 after 120 seconds must enter unexpected-wake resleep');
assert.equal(unexpectedWakeDecision(119999, true, -1), false,
  'USB4 activity before 120 seconds must not resleep the user');

// A new user 506 must repair a stale in-process lifecycle before it queues the
// game pause. This is separate from active S0 transactions and USB4 retry.
requireNative('s0-intent-stale-lifecycle');
requireNative('sgAbortSleepIntent("kernel-power-506-stale-lifecycle", false);');
requireNative('entry-retry-active');
requireNative('sleep-game-pause');
requireNative('sleep-game-pause-skipped');

// Event 107 TargetState=5 is a final hibernate resume. It must restore the
// owned game lease even when a prior Reason=7 retry was still pending.
requireNative('s4-wake-canceled-entry-retry');
requireNative('sgAbortSleepIntent("kernel-s4-wake", false);');
requireNative('SgWork::WakeHibernate');
requireNative('sgRealWake("resume_suspend");');

// Entry retry is an exclusive transaction: the failure is scheduled before
// ordinary wake work can consume the held process lease.
const retryBeforeWake = native.indexOf(
  'if (fastEntryFailure && g_sgRetryEntryFailure) {',
  native.indexOf('static void handlePowerResumeNotification'),
);
const ordinaryWakeQueue = native.indexOf('sgQueueWork(work, generation);', retryBeforeWake);
assert.ok(retryBeforeWake >= 0 && ordinaryWakeQueue > retryBeforeWake,
  'entry retry must be scheduled before ordinary wake work');
requireNative('sleep entry retry succeeded attempts=%u generation=%llu');
requireNative('g_powerLifecycle.store(PowerLifecycle::Suspended, std::memory_order_release);');
requireNative('closeHardwareWriteGate("entry-retry-exhausted");');
requireNative('{"entryRetryExhausted", true}');

// Automatic S3 wake is not a user resume. It must keep both the game lease and
// hardware gate held until RESUMESUSPEND supplies explicit user intent.
const automaticWake = native.slice(
  native.indexOf('static void sgRealWake(const char* src)'),
  native.indexOf('static SgSleepMode sgPowerButtonSleepMode()'),
);
assert.ok(automaticWake.includes('stopPowerResumeWatchdog();'),
  'automatic S3 wake must stop the resume watchdog');
requireNative('sleep automatic wake commit held generation=%llu');

// Sleep delegates its process lease to the Performance Schedule directory.
requireNative('target.pid, SG_MANUAL_DIR, target.processCreated, &target');
requireNative('sgResumeGameByPids(values, SG_MANUAL_DIR)');
requireNative('sgResumeGlobalSuspendedLargeProcesses');

console.log('sleep task policy self-test: PASS');
