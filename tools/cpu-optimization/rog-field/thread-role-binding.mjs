// Development-only association of ordinary role records with an already held cohort.
// This module reads no OS state and grants no application or device authority.
const roles = new Set([
  'ui-main', 'gamepad-serial-worker', 'native-monitor', 'sensor-sta',
  'bus-tick', 'capture-worker', 'input-host-lifecycle-owner', 'fan-lease-keepalive',
]);
const fileTimeEpoch = 116444736000000000n;
const validId = value => Number.isSafeInteger(value) && value > 0 && value <= 0xffffffff;
function birth(value) {
  if (typeof value !== 'string' || !/^[1-9][0-9]{0,19}$/.test(value)) return null;
  const number = BigInt(value);
  return number <= 0xffffffffffffffffn ? number : null;
}
function offset(value) {
  const match = /^(-?)([0-9]{2}):([0-9]{2}):00$/.exec(value ?? '');
  if (!match || +match[2] > 14 || +match[3] > 59 ||
      (+match[2] === 14 && +match[3] !== 0)) return null;
  return (match[1] || '+') + match[2] + ':' + match[3];
}
export function createRoleCohort(identity, discovery) {
  const target = identity?.target;
  if (!validId(target?.pid) || birth(target.birthFileTime) === null ||
      discovery?.identity?.ProcessId !== target.pid ||
      discovery.identity.BirthFileTime !== target.birthFileTime) {
    throw new Error('Root identity differs between held cohort and discovery');
  }
  const pid = target.pid;
  const observedAt = Date.parse(identity.atUtc);
  if (!Number.isFinite(observedAt) || !Array.isArray(identity.boundThreads) ||
      identity.boundThreads.length > 512) throw new Error('Invalid held cohort');
  const threads = new Map();
  for (const thread of identity.boundThreads) {
    const value = birth(thread.birthFileTime);
    if (!validId(thread.tid) || value === null || threads.has(thread.tid)) {
      throw new Error('Invalid or duplicate held thread identity');
    }
    threads.set(thread.tid, {birth: thread.birthFileTime, value});
  }
  const zone = offset(discovery.utcOffset);
  return {
    bind(event) {
      const rejected = reason => ({accepted: false, role: event?.role,
        tid: event?.threadId, pid: event?.pid, reason});
      if (event?.event !== 'thread-role' || event.pid !== pid ||
          !validId(event.threadId)) return rejected('PID_TID_OR_EVENT_MISMATCH');
      if (!roles.has(event.role)) return rejected('UNKNOWN_ROLE');
      const thread = threads.get(event.threadId);
      if (!thread) return rejected('THREAD_OUTSIDE_HELD_COHORT');
      const explicit = ['threadBirthFileTime100ns', 'threadBirthQuerySucceeded',
        'threadBirthQueryError'].some(key => Object.hasOwn(event, key));
      let binding;
      if (explicit) {
        if (event.threadBirthQuerySucceeded !== true || event.threadBirthQueryError !== 0 ||
            birth(event.threadBirthFileTime100ns) === null) {
          return rejected('EXPLICIT_BIRTH_UNAVAILABLE_OR_MALFORMED');
        }
        if (event.threadBirthFileTime100ns !== thread.birth) {
          return rejected('EXPLICIT_BIRTH_MISMATCH');
        }
        // Exact self GetThreadTimes birth replaces only the old timestamp proxy.
        // Failed/mismatching explicit evidence never falls back to that proxy.
        binding = 'EXACT_GET_THREAD_TIMES_BIRTH';
      } else {
        if (!zone || typeof event.time !== 'string' ||
            !/^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{1,7}$/.test(event.time)) {
          return rejected('LEGACY_TIME_OR_OFFSET_UNAVAILABLE');
        }
        const eventMs = Date.parse(event.time + zone);
        const earliest = Number((thread.value - fileTimeEpoch + 9999n) / 10000n);
        if (!Number.isFinite(eventMs) || eventMs < earliest || eventMs > observedAt) {
          return rejected('LEGACY_EVENT_OUTSIDE_CONSERVATIVE_LIFETIME');
        }
        binding = 'LEGACY_CEIL_MILLISECOND_LIFETIME';
      }
      return {accepted: true, role: event.role, tid: event.threadId,
        pid, birthFileTime: thread.birth,
        key: event.threadId + ':' + thread.birth, binding, logTime: event.time ?? null};
    },
  };
}
