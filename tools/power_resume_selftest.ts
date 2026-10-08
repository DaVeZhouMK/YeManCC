import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runPowerResumeTransaction } from '@/bridge/powerResume';

const noSleep = async () => {};

async function test(name: string, run: () => Promise<void>): Promise<void> {
  await run();
  console.log(`PASS ${name}`);
}

async function main(): Promise<void> {
await test('native commit precedes daemon recovery when no FanHost F5 is armed', async () => {
  const order: string[] = [];
  const result = await runPowerResumeTransaction(7, true, {
    completeResume: async () => {
      order.push('commit');
      return { ok: true };
    },
    resumeDaemon: async () => {
      order.push('daemon');
      return true;
    },
    sleep: noSleep,
  });
  assert.deepEqual(order, ['commit', 'daemon']);
  assert.equal(result.committed, true);
  assert.equal(result.daemonReady, true);
  assert.equal(result.commitAttempts, 1);
  assert.equal(result.daemonAttempts, 1);
});

await test('App fires FanHost F5 fire-and-forget on resume-ready, never gating the native commit', async () => {
  const appSource = readFileSync('src/App.vue', 'utf8');
  const resumeReadyStart = appSource.indexOf('function onPowerResumeReady(');
  const resumeReadyEnd = appSource.indexOf('\nfunction ', resumeReadyStart + 1);
  const resumeReadyBody = appSource.slice(resumeReadyStart, resumeReadyEnd > resumeReadyStart ? resumeReadyEnd : resumeReadyStart + 900);
  const fanReadyStart = appSource.indexOf('function onFanResumeReady(');
  const fanReadyEnd = appSource.indexOf('\nfunction ', fanReadyStart + 1);
  const fanReadyBody = appSource.slice(fanReadyStart, fanReadyEnd > fanReadyStart ? fanReadyEnd : fanReadyStart + 1800);
  const transactionStart = appSource.indexOf('const result = await runPowerResumeTransaction(targetGeneration, daemonRequired, {');
  const nativeCommit = appSource.indexOf('completeResume: (resumeGeneration, meta) => powerLifecycle.completeResume(resumeGeneration, meta)', transactionStart);
  assert(resumeReadyStart >= 0 && resumeReadyBody.includes('onFanResumeReady(resumeGeneration)') &&
    resumeReadyBody.includes('scheduleResumeTransaction(resumeGeneration);') &&
    fanReadyStart > resumeReadyStart && fanReadyBody.includes('void fanHostLifecycle.resume().catch(') &&
    transactionStart >= 0 && nativeCommit > transactionStart &&
    !appSource.slice(transactionStart, transactionStart + 1200).includes('resumeFanBeforeCommit'),
  'App must start the FanHost F5 fire-and-forget from resume-ready and never gate the native Ready commit on it');
});

await test('native commit does not wait for InputHost recovery', async () => {
  const nativeSource = readFileSync('native/main.cpp', 'utf8');
  const resumeComplete = nativeSource.indexOf('ipc_on("power.resumeComplete"');
  const readyCommit = nativeSource.indexOf('g_powerLifecycle.store(PowerLifecycle::Ready', resumeComplete);
  const inputRecover = nativeSource.indexOf('WM_POWER_INPUT_RECOVER');
  const inputRecoverCase = nativeSource.indexOf('case WM_POWER_INPUT_RECOVER:');
  assert(resumeComplete >= 0 && readyCommit > resumeComplete && inputRecover >= 0 && inputRecoverCase > readyCommit,
    'native source must commit the power gate before post-commit InputHost recovery');
  const preCommitBody = nativeSource.slice(resumeComplete, readyCommit);
  assert(!preCommitBody.includes('gamepadRecoverAfterResume()'),
    'power.resumeComplete must not synchronously wait for gamepad/InputHost recovery');
});

await test('native self-commits the wake gate on resume-ready (HC SystemReady parity, G8)', async () => {
  const nativeSource = readFileSync('native/main.cpp', 'utf8');
  const readyCase = nativeSource.indexOf('case WM_POWER_RESUME_READY:');
  const readyCaseEnd = nativeSource.indexOf('case WM_POWER_RESUME_COMMIT:', readyCase);
  const body = nativeSource.slice(readyCase, readyCaseEnd > readyCase ? readyCaseEnd : readyCase + 2400);
  assert(readyCase >= 0 && body.includes('commitPowerResume(generation, "native.resume-ready")'),
    'WM_POWER_RESUME_READY must self-commit the native Ready gate; the renderer IPC stays an idempotent second caller');
  const helper = nativeSource.indexOf('static bool commitPowerResume(unsigned long long generation, const char* source) {');
  assert(helper > nativeSource.indexOf('ipc_on("power.resumeComplete"'),
    'commitPowerResume must stay after the renderer IPC registration (source-order contract)');
});

await test('Fan manual wake is a Fan-only same-generation rescue and never opens during Suspending', async () => {
  const nativeSource = readFileSync('native/main.cpp', 'utf8');
  const safeSet = nativeSource.indexOf('"power.lifecycle", "power.activeScheme", "power.hibernateState", "power.resumeComplete", "power.fanManualWake"');
  const handler = nativeSource.indexOf('ipc_on("power.fanManualWake"');
  const handlerEnd = nativeSource.indexOf('\n    // ── proc.running', handler);
  assert(safeSet >= 0, 'power.fanManualWake must be allowed through the isolated IPC command gate');
  assert(handler >= 0 && handlerEnd > handler, 'power.fanManualWake must have one registered handler');
  const body = nativeSource.slice(handler, handlerEnd);
  assert(body.includes('phase == PowerLifecycle::Suspending') && body.includes('power_suspending'),
    'manual Fan wake must reject an active Suspending phase');
  assert(body.includes('generation != current') && body.includes('stale_generation'),
    'manual Fan wake must reject a stale generation');
  assert(body.includes('g_powerLifecycle.store(PowerLifecycle::Resuming') &&
    body.includes('g_resumeReadyGeneration.store(current'),
    'manual Fan wake must promote only the current generation to Resuming/resume-ready');
  assert(body.includes('fanHostScheduleEmergencyResume(current)') &&
    body.includes('commitPowerResume(current, "fan.manual-control")'),
    'manual Fan wake must reuse the existing FanHost recovery and single resume commit');
  assert(!body.includes('realBackend') && !body.includes('hardwareWrite'),
    'manual Fan wake must not write hardware directly or bypass Host admission');
});

await test('resumed notification does not restart FanHost F5', async () => {
  const appSource = readFileSync('src/App.vue', 'utf8');
  const resumedStart = appSource.indexOf('function onPowerResumed(');
  const resumedEnd = appSource.indexOf('\nfunction ', resumedStart + 1);
  const resumedBody = appSource.slice(resumedStart, resumedEnd > resumedStart ? resumedEnd : resumedStart + 1200);
  assert(resumedStart >= 0 && !resumedBody.includes('scheduleResumeTransaction('),
    'power.resumed must close the committed generation without re-entering FanHost F5');
});

await test('powerResume transaction no longer gates the commit on FanHost F5', async () => {
  const source = readFileSync('src/bridge/powerResume.ts', 'utf8');
  const interfaceStart = source.indexOf('export interface PowerResumeTransactionDeps {');
  const interfaceEnd = source.indexOf('\n}', interfaceStart);
  const body = source.slice(interfaceStart, interfaceEnd > interfaceStart ? interfaceEnd : interfaceStart + 900);
  assert(interfaceStart >= 0 && !body.includes('resumeFanBeforeCommit') && !source.includes('await deps.resumeFanBeforeCommit'),
    'HC parity: the FanHost F5 must never gate the native Ready commit');
});

await test('a failed background FanHost F5 cannot block the native Ready commit', async () => {
  const appSource = readFileSync('src/App.vue', 'utf8');
  const fanStart = appSource.indexOf('function onFanResumeReady(');
  // FAN-932: slice the whole Fan handler instead of a hardcoded window so
  // added comments can never silently push the F5 call outside the checked body.
  const fanEnd = appSource.indexOf('\nfunction ', fanStart + 1);
  const fanBlock = appSource.slice(fanStart, fanEnd > fanStart ? fanEnd : fanStart + 1800);
  assert(fanStart >= 0 &&
    fanBlock.includes('void fanHostLifecycle.resume().catch(') &&
    !fanBlock.includes('await fanHostLifecycle.resume()'),
  'the fan resume is fire-and-forget with error swallowing; a failure never returns into the commit path');
});

await test('daemon failure never recloses a committed gate', async () => {
  let daemonAttempts = 0;
  const result = await runPowerResumeTransaction(8, true, {
    completeResume: async () => ({ ok: true }),
    resumeDaemon: async () => {
      daemonAttempts += 1;
      return false;
    },
    daemonAttempts: 2,
    sleep: noSleep,
  });
  assert.equal(result.committed, true);
  assert.equal(result.daemonReady, false);
  assert.equal(result.reason, 'daemon_resume_failed');
  assert.equal(daemonAttempts, 2);
});

await test('native commit uses bounded retries', async () => {
  let commitAttempts = 0;
  const result = await runPowerResumeTransaction(9, false, {
    completeResume: async () => {
      commitAttempts += 1;
      return commitAttempts === 3 ? { ok: true } : { ok: false, reason: 'native_recovery_not_ready' };
    },
    resumeDaemon: async () => true,
    commitAttempts: 3,
    sleep: noSleep,
  });
  assert.equal(result.committed, true);
  assert.equal(result.commitAttempts, 3);
  assert.equal(commitAttempts, 3);
});

await test('stale generation is rejected before native commit', async () => {
  let commitCalled = false;
  const result = await runPowerResumeTransaction(10, false, {
    completeResume: async () => {
      commitCalled = true;
      return { ok: true };
    },
    resumeDaemon: async () => true,
    isGenerationCurrent: () => false,
    sleep: noSleep,
  });
  assert.equal(result.committed, false);
  assert.equal(result.reason, 'superseded');
  assert.equal(commitCalled, false);
});

await test('new generation supersedes daemon work after native commit', async () => {
  let current = true;
  let daemonCalled = false;
  const result = await runPowerResumeTransaction(11, true, {
    completeResume: async () => {
      current = false;
      return { ok: true };
    },
    resumeDaemon: async () => {
      daemonCalled = true;
      return true;
    },
    isGenerationCurrent: () => current,
    sleep: noSleep,
  });
  assert.equal(result.committed, true);
  assert.equal(result.reason, 'superseded_after_commit');
  assert.equal(daemonCalled, false);
});

console.log('power resume selftest: 10/10 passed');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
