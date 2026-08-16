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

// A retry must explicitly request sleep, never hibernate.
requireNative('SetSystemPowerState(TRUE, FALSE)');
requireNative('SG_ENTRY_RETRY_DELAYS_MS[] = {500ULL, 1000ULL, 2000ULL}');
requireNative('sgAdvanceRetry("retry-request-rejected")');
requireNative('g_sgTask.nonUserWakeAttempts >= SG_MAX_ENTRY_RETRIES');
requireNative('user-standby-device-trigger');

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
requireNative('g_powerLifecycle.store(PowerLifecycle::Suspended, std::memory_order_release);\n                    return TRUE;');

// Sleep delegates its process lease to the Performance Schedule directory.
requireNative('target.pid, SG_MANUAL_DIR, target.processCreated, &target');
requireNative('sgResumeGameByPids(values, SG_MANUAL_DIR)');
requireNative('sgResumeGlobalSuspendedLargeProcesses');

console.log('sleep task policy self-test: PASS');
