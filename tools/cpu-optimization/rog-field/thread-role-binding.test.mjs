import test from 'node:test';
import assert from 'node:assert/strict';
import {createRoleCohort} from './thread-role-binding.mjs';

const pid = 66800, tid = 35744, birth = '134387754222668117';
const identity = {atUtc: '2026-11-10T09:30:48.000Z',
  target: {pid, birthFileTime: '134387754187696575'},
  boundThreads: [{tid, birthFileTime: birth}]};
const discovery = {identity: {ProcessId: pid,
  BirthFileTime: identity.target.birthFileTime}, utcOffset: '08:00:00'};
const legacy = {event: 'thread-role', pid, threadId: tid,
  role: 'native-monitor', time: '2026-11-10T17:10:22.266'};
const explicit = {...legacy, threadBirthFileTime100ns: birth,
  threadBirthQuerySucceeded: true, threadBirthQueryError: 0};
const bind = event => createRoleCohort(identity, discovery).bind(event);

test('precise birth binds despite earlier rounded legacy timestamp', () => {
  assert.equal(bind(explicit).binding, 'EXACT_GET_THREAD_TIMES_BIRTH');
  assert.equal(bind(legacy).accepted, false);
});
test('one FILETIME tick mismatch rejects, with no timestamp fallback', () => {
  const event = {...explicit, time: '2026-11-10T17:10:22.900',
    threadBirthFileTime100ns: (BigInt(birth) + 1n).toString()};
  assert.equal(bind(event).reason, 'EXPLICIT_BIRTH_MISMATCH');
});
test('numeric FILETIME rejects even if conversion appears equal', () => {
  assert.equal(bind({...explicit, threadBirthFileTime100ns: Number(birth)}).accepted, false);
});
test('failed query never inherits a valid legacy timestamp', () => {
  assert.equal(bind({...explicit, time: '2026-11-10T17:10:22.900',
    threadBirthQuerySucceeded: false, threadBirthQueryError: 5,
    threadBirthFileTime100ns: null}).accepted, false);
});
test('partial explicit evidence never silently becomes legacy', () => {
  assert.equal(bind({...legacy, time: '2026-11-10T17:10:22.900',
    threadBirthQuerySucceeded: true}).accepted, false);
});
test('malformed and out of range explicit values reject', () => {
  for (const value of ['0', '01', '-1', '1.0', '18446744073709551616', '', null]) {
    assert.equal(bind({...explicit, threadBirthFileTime100ns: value}).accepted, false);
  }
});
test('PID, TID, event and known role remain required', () => {
  for (const change of [{pid: pid + 1}, {threadId: tid + 1},
    {event: 'other'}, {role: 'other'}, {threadId: 1.5}]) {
    assert.equal(bind({...explicit, ...change}).accepted, false);
  }
});
test('legacy first full millisecond is accepted and earlier event stays rejected', () => {
  assert.equal(bind({...legacy, time: '2026-11-10T17:10:22.267'}).accepted, true);
  assert.equal(bind(legacy).reason, 'LEGACY_EVENT_OUTSIDE_CONSERVATIVE_LIFETIME');
});
test('future legacy role event is rejected', () => {
  assert.equal(bind({...legacy, time: '2026-11-10T17:30:48.001'}).accepted, false);
});
test('missing offset rejects legacy but does not invalidate exact birth evidence', () => {
  const context = createRoleCohort(identity, {...discovery, utcOffset: null});
  assert.equal(context.bind({...legacy, time: '2026-11-10T17:10:22.900'}).accepted, false);
  assert.equal(context.bind(explicit).accepted, true);
});
test('duplicate cohort TID and altered root birth reject before binding', () => {
  assert.throws(() => createRoleCohort({...identity,
    boundThreads: [...identity.boundThreads, ...identity.boundThreads]}, discovery));
  assert.throws(() => createRoleCohort(identity, {identity: {
    ...discovery.identity, BirthFileTime: (BigInt(identity.target.birthFileTime) + 1n).toString()}}));
});
test('cohort captures birth strings, independent of later caller mutation', () => {
  const mutable = structuredClone(identity), context = createRoleCohort(mutable, discovery);
  mutable.boundThreads[0].birthFileTime = (BigInt(birth) + 1n).toString();
  mutable.target.pid += 1;
  assert.equal(context.bind(explicit).accepted, true);
});
