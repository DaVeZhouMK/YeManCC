import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const native = readFileSync(resolve(process.cwd(), 'native/main.cpp'), 'utf8');

function requireNative(token: string): void {
  assert.ok(native.includes(token), `missing SleepTask policy: ${token}`);
}

requireNative('enum class SgRetryKind : uint8_t { None, EntryFailure, NonUserWake };');
requireNative('unsigned int entryFailureAttempts = 0;');
requireNative('bool nonUserWakeResleepUsed = false;');
requireNative('g_sgTask.nonUserWakeResleepUsed');
requireNative('g_sgTask.entryFailureAttempts >= SG_MAX_ENTRY_RETRIES');
requireNative('case SgRetryKind::NonUserWake: return "non-user-wake";');

// A confirmed S3 transition belongs to the non-user-wake path if it returns;
// it must not be reclassified as an entry failure because it was short.
requireNative('phase == PowerLifecycle::Suspending && !g_sgTask.suspendConfirmed');
requireNative('g_sgTask.suspendConfirmed = true;');

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
requireNative('g_sgTask.nonUserWakeAttempts >= SG_MAX_ENTRY_RETRIES');
requireNative('external-device-wake-confirmed');
requireNative('independentOf", {"repairEligible", "taskMode"}');

// USB4/external-device resleep is a frozen, separate state machine. These
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
requireNative('Kernel-Power 107 with TargetState=5');

// A rapid Modern Standby flow exit is a confirmed entry failure. It must not
// be routed through USB4's 120-second external-device evidence path, and its
// owned game pause lease must survive until retry success or a user wake.
requireNative('SG_S0_REASON7_FAILURE_WINDOW_MS = 2000ULL');
requireNative('static bool sgS0EntryFailureEligible(int wakeReason, ULONGLONG nowTick)');
requireNative('if (wakeReason != 7 && wakeReason != 8) return false;');
requireNative('sgMarkSleepTrigger deliberately consumes g_sgSleepIntentArmed');
requireNative('static bool sgS0Reason7FailureEligible');
requireNative('const bool reason7EntryFailure = g_sgLastS0WakeReason == 7');
requireNative('const bool reason8EntryFailure = g_sgLastS0WakeReason == 8');
requireNative('{"reason7EntryFailure", reason7EntryFailure}');
requireNative('{"reason8EntryFailure", reason8EntryFailure}');
requireNative('s0 reason=%d confirmed entry failure');
requireNative('sgHandleModernStandbyWake(false);');
requireNative('Reason=5 remains USB4 evidence only');
requireNative('s0-entry-retry-canceled-user-wake');
requireNative('sgAbortSleepIntent("s0-user-power-button-wake", false);');

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

// Sleep delegates its process lease to the Performance Schedule directory.
requireNative('target.pid, SG_MANUAL_DIR, target.processCreated, &target');
requireNative('sgResumeGameByPids(values, SG_MANUAL_DIR)');
requireNative('sgResumeGlobalSuspendedLargeProcesses');

console.log('sleep task policy self-test: PASS');
