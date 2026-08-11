import assert from 'node:assert/strict';
import { runPowerResumeTransaction } from '@/bridge/powerResume';

const noSleep = async () => {};

async function test(name: string, run: () => Promise<void>): Promise<void> {
  await run();
  console.log(`PASS ${name}`);
}

async function main(): Promise<void> {
await test('native commit precedes daemon recovery', async () => {
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

console.log('power resume selftest: 5/5 passed');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
