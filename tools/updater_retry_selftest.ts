import assert from 'node:assert/strict';

const retryIntervalMs = 5_000;

type Attempt = {
  ok: boolean;
  retryable?: boolean;
  error?: string;
  receivedBytes?: number;
  restartRequired?: boolean;
};
type RetryEvent = { attempt: number; nextAttempt: number; atMs: number; resumeOffset: number; message: string };

function simulate(attempts: Attempt[], initialBytes = 0) {
  let nowMs = 0;
  let attempt = 0;
  let downloadedBytes = initialBytes;
  const retries: RetryEvent[] = [];
  let lastError = '';
  for (;;) {
    attempt++;
    const result = attempts[attempt - 1] ?? { ok: false, retryable: true, error: 'network unavailable' };
    if (result.ok) return { ok: true, attempt, elapsedMs: nowMs, retries, lastError };
    lastError = result.error || 'download failed';
    if (result.receivedBytes !== undefined) downloadedBytes = result.receivedBytes;
    if (result.retryable === false) return { ok: false, attempt, elapsedMs: nowMs, retries, lastError };
    if (result.restartRequired) downloadedBytes = 0;
    retries.push({
      attempt,
      nextAttempt: attempt + 1,
      atMs: nowMs,
      resumeOffset: downloadedBytes,
      message: result.restartRequired
        ? `下载校验或续传状态无效，5秒后从0字节重新下载第 ${attempt + 1} 次尝试`
        : downloadedBytes > 0
          ? `下载中断，5秒后从 ${downloadedBytes} 字节继续第 ${attempt + 1} 次尝试`
          : `下载失败，5秒后重新尝试第 ${attempt + 1} 次`,
    });
    nowMs += retryIntervalMs;
  }
}

const fifthSucceeds = simulate([
  { ok: false, error: 'reset 1' },
  { ok: false, error: 'reset 2', receivedBytes: 2_000_000 },
  { ok: false, error: 'reset 3', receivedBytes: 4_000_000 },
  { ok: false, error: 'reset 4', receivedBytes: 6_000_000 },
  { ok: true },
]);
assert.equal(fifthSucceeds.ok, true);
assert.equal(fifthSucceeds.attempt, 5);
assert.equal(fifthSucceeds.elapsedMs, 20_000);
assert.deepEqual(fifthSucceeds.retries.map((event) => event.nextAttempt), [2, 3, 4, 5]);
assert.deepEqual(fifthSucceeds.retries.map((event) => event.resumeOffset), [0, 2_000_000, 4_000_000, 6_000_000]);
assert.ok(fifthSucceeds.retries.every((event, index) => event.atMs === index * retryIntervalMs));

const checksumRestart = simulate([
  { ok: false, error: 'Checksum mismatch', receivedBytes: 8_000_000, restartRequired: true },
  { ok: true },
], 8_000_000);
assert.equal(checksumRestart.ok, true);
assert.equal(checksumRestart.retries[0]?.resumeOffset, 0);
assert.match(checksumRestart.retries[0]?.message ?? '', /从0字节重新下载第 2 次尝试/);

const slowNetwork = simulate([
  ...Array.from({ length: 300 }, (_, i) => ({
    ok: false,
    error: `temporary network failure ${i + 1}`,
    receivedBytes: (i + 1) * 32_768,
  })),
  { ok: true },
]);
assert.equal(slowNetwork.ok, true);
assert.equal(slowNetwork.attempt, 301);
assert.equal(slowNetwork.elapsedMs, 25 * 60_000);
assert.equal(slowNetwork.retries.length, 300);
assert.equal(slowNetwork.retries.at(-1)?.resumeOffset, 300 * 32_768);

const localFailure = simulate([{ ok: false, retryable: false, error: 'cannot write package' }], 1234);
assert.equal(localFailure.ok, false);
assert.equal(localFailure.attempt, 1);
assert.equal(localFailure.retries.length, 0);

console.log('updater retry self-test: PASS');
