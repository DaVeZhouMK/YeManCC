// Diagnostic collection only. A ready result permits a read of the already
// pinned channel, never input, reattachment, identity adoption, or a Steam write.
import { performance } from 'node:perf_hooks';

export function inspectOriginalCaptureChannel(row, pinned, nowMs, { maxSampleAgeMs = 4500, maxListenerAgeMs = 8500, requireGame = true } = {}) {
  const wait = reason => ({ status: 'wait', reason, mayRead: false, actionAuthorized: false });
  const reject = reason => ({ status: 'rejected', reason, mayRead: false, actionAuthorized: false });
  if (!row || typeof row !== 'object') return wait('OS-sample-unavailable');
  const observed = Date.parse(row.atUtc);
  if (!Number.isFinite(observed) || !Number.isFinite(nowMs) || observed > nowMs || nowMs - observed > maxSampleAgeMs) return wait('OS-sample-not-fresh');
  const keys = requireGame ? ['game', 'steam', 'webhelper'] : ['steam', 'webhelper'];
  for (const key of keys) {
    const actual = row.originals?.[key], original = pinned?.[key];
    if (!original || !Number.isInteger(original.ProcessId) || original.ProcessId <= 0 || original.ProcessId > 0xffffffff || typeof original.CreationFiletime !== 'string' || !/^[1-9][0-9]{0,19}$/.test(original.CreationFiletime) || BigInt(original.CreationFiletime) > 0xffffffffffffffffn || typeof original.ImagePath !== 'string' || !original.ImagePath || !Number.isInteger(original.SessionId) || original.SessionId < 0 || original.SessionId > 0xffffffff) return reject('pinned-original-invalid:' + key);
    if (!actual || actual.IdentityObserved !== true || actual.AliveObserved !== true || actual.WaitResult !== 258) {
      if (actual?.ExitObserved === true) return reject('original-exited:' + key);
      return wait('original-identity-unavailable:' + key);
    }
    if (actual.ProcessId !== original.ProcessId || actual.CreationFiletime !== original.CreationFiletime || actual.SessionId !== original.SessionId || typeof actual.ImagePath !== 'string' || actual.ImagePath.toLowerCase() !== original.ImagePath.toLowerCase()) return reject('original-identity-changed:' + key);
    if (row.originalMatches?.[key] !== true) return reject('original-producer-disagrees:' + key);
  }
  const listenerAt = Date.parse(row.listenerObservedUtc);
  if (!Number.isFinite(listenerAt) || listenerAt > nowMs || nowMs - listenerAt > maxListenerAgeMs) return wait('original-listener-evidence-not-fresh');
  if (!Number.isInteger(row.listenerOwnerPid) || row.listenerOwnerPid === 0) return wait('original-listener-unavailable');
  if (row.listenerOwnerPid !== pinned.webhelper.ProcessId) return reject('original-listener-owner-changed');
  if (row.originalListenerSame !== true) return wait('original-listener-not-confirmed');
  return { status: 'ready', reason: 'fresh-pinned-original-channel', mayRead: true, actionAuthorized: false, sampleAgeMs: nowMs - observed, listenerAgeMs: nowMs - listenerAt };
}

export async function waitForOriginalCaptureChannel({ readSnapshot, pinned, budgetMs = 10000, pollMs = 100, wallNow = Date.now, monotonicNow = () => performance.now(), delay = ms => new Promise(resolve => setTimeout(resolve, ms)), requireGame = true }) {
  if (typeof readSnapshot !== 'function' || !Number.isInteger(budgetMs) || budgetMs < 1 || budgetMs > 20000 || !Number.isInteger(pollMs) || pollMs < 1 || pollMs > 1000) throw new TypeError('invalid-bounded-capture-wait');
  const start = monotonicNow(); let last = { status: 'wait', reason: 'OS-sample-unavailable', mayRead: false, actionAuthorized: false }, attempts = 0;
  const maximumAttempts = Math.ceil(budgetMs / pollMs) + 2;
  while (attempts < maximumAttempts) {
    const elapsed = monotonicNow() - start;
    if (!Number.isFinite(elapsed) || elapsed < 0) return { status: 'rejected', reason: 'capture-clock-invalid', mayRead: false, actionAuthorized: false, attempts };
    if (attempts > 0 && elapsed >= budgetMs) break;
    attempts++;
    let row;
    try { row = await readSnapshot(); } catch { last = { status: 'wait', reason: 'OS-sample-read-unavailable', mayRead: false, actionAuthorized: false }; }
    if (monotonicNow() - start >= budgetMs) break; // A slow reader cannot authorize a late read.
    if (row !== undefined) last = inspectOriginalCaptureChannel(row, pinned, wallNow(), { requireGame });
    if (last.status !== 'wait') return { ...last, attempts, waitedMs: monotonicNow() - start, row };
    await delay(Math.min(pollMs, Math.max(0, budgetMs - (monotonicNow() - start))));
  }
  return { status: 'timeout', reason: last.reason, mayRead: false, actionAuthorized: false, attempts, waitedMs: monotonicNow() - start };
}

// A CSteamID JS wrapper may be replaced while its account number stays the same.
// Keep the original account fixed, tag that diagnostic discontinuity, and never
// grant an action or silently adopt another account because an object changed.
export function captureAccountObservation(original, current, sameObject) {
  const valid = n => typeof n === 'number' && Number.isInteger(n) && n > 0 && n <= 0xffffffff;
  const accountMatchesOriginal = valid(original) && valid(current) && current === original;
  return { accountMatchesOriginal, capturePayload: accountMatchesOriginal, originalAccount: valid(original) ? original : null, observedAccount: valid(current) ? current : null, accountObjectSame: sameObject === true, classification: !accountMatchesOriginal ? (valid(current) ? 'different-account' : 'account-unavailable') : sameObject === true ? 'same-account-and-wrapper' : 'same-account-new-wrapper', actionAuthorized: false };
}