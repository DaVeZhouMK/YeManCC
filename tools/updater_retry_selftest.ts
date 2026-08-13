import assert from 'node:assert/strict';

const retryIntervalMs = 5_000;
const retryWindowMs = 5 * 60_000;

type Attempt = { ok: boolean; retryable?: boolean; error?: string };
type RetryEvent = { attempt: number; nextAttempt: number; atMs: number };

function simulate(attempts: Attempt[]) {
  let nowMs = 0;
  let attempt = 0;
  const retries: RetryEvent[] = [];
  let lastError = '';
  for (;;) {
    if (nowMs >= retryWindowMs) break;
    attempt++;
    const result = attempts[attempt - 1] ?? { ok: false, retryable: true, error: 'network unavailable' };
    if (result.ok) return { ok: true, attempt, elapsedMs: nowMs, retries, lastError };
    lastError = result.error || 'download failed';
    if (result.retryable === false) return { ok: false, attempt, elapsedMs: nowMs, retries, lastError };
    if (retryWindowMs - nowMs <= retryIntervalMs) break;
    retries.push({ attempt, nextAttempt: attempt + 1, atMs: nowMs });
    nowMs += retryIntervalMs;
  }
  return { ok: false, attempt, elapsedMs: nowMs, retries, lastError };
}

const fifthSucceeds = simulate([
  { ok: false, error: 'reset 1' },
  { ok: false, error: 'reset 2' },
  { ok: false, error: 'reset 3' },
  { ok: false, error: 'reset 4' },
  { ok: true },
]);
assert.equal(fifthSucceeds.ok, true);
assert.equal(fifthSucceeds.attempt, 5);
assert.equal(fifthSucceeds.elapsedMs, 20_000);
assert.deepEqual(fifthSucceeds.retries.map((event) => event.nextAttempt), [2, 3, 4, 5]);
assert.ok(fifthSucceeds.retries.every((event, index) => event.atMs === index * retryIntervalMs));

const permanentFailure = simulate([]);
assert.equal(permanentFailure.ok, false);
assert.equal(permanentFailure.attempt, 60);
assert.equal(permanentFailure.retries.length, 59);
assert.equal(permanentFailure.elapsedMs, 295_000);
assert.equal(permanentFailure.lastError, 'network unavailable');

const localFailure = simulate([{ ok: false, retryable: false, error: 'cannot write package' }]);
assert.equal(localFailure.ok, false);
assert.equal(localFailure.attempt, 1);
assert.equal(localFailure.retries.length, 0);

console.log('updater retry self-test: PASS');
