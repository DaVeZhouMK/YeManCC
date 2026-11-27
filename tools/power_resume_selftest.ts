import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createPowerResumeQueue, runPowerResumeTransaction } from '@/bridge/powerResume';

const noSleep = async () => {};
const selectedFlag = process.argv.indexOf('--case');
const selectedCase = selectedFlag < 0 ? null : process.argv[selectedFlag + 1];
let executedCases = 0;
const appSchedulerOnly = process.argv.includes('--app-scheduler-only');
const appContinuationOnly = process.argv.includes('--app-continuation-only');
const appSharedSourceOnly = process.argv.includes('--app-shared-source-only');
const appSleepConfirmationOnly = process.argv.includes('--app-sleep-confirmation-only');
const appSleepPlanOnly = process.argv.includes('--app-sleep-plan-only') || appSleepConfirmationOnly;

/*__ACTUAL_SHARED_POWER_SOURCE__*/
function createSharedAppFixture(ports: any = {}): any {
  let source: any, serial = 0, reads = 0;
  const handlers = new Map<string, Set<(value: any) => void>>(), timers = new Map<number, { fn: () => void; ms: number }>();
  const pending: any[] = [], observed: any[] = [];
  const listen = (name: string, listener: (value: any) => void) => { let row = handlers.get(name); if (!row) handlers.set(name, row = new Set()); row.add(listener); return () => row!.delete(listener); };
  const app = createActualAppResume({ ...ports, listen, confirmReady: (listener:any) => source.requestSharedPowerLifecycleReadyConfirmation(listener), sharedSnapshot: () => { const state = source?.getSharedPowerLifecycle() ?? null; ports.sharedRead?.(state); return state; } });
  source = createSharedPowerLifecycleSource({
    read: () => { reads++; return new Promise((resolve, reject) => pending.push({ resolve, reject })); },
    listen,
    clock: { setTimeout(fn, ms) { const id = ++serial; timers.set(id, { fn, ms }); return id; }, clearTimeout(id: any) { timers.delete(id); } },
  });
  const off = source.onSharedPowerLifecycle((state: any) => { observed.push(structuredClone(state)); app.observe(state); }, app.sharedOptions);
  const fact = (phase = 'ready', revision = 1, sequence = 1, extra: any = {}) => ({ generation: 2, phase, known: true,
    publicationRevision: revision, observedNotificationSequence: sequence, suspendSerial: 0, notificationPending: false,
    hardwareWritesAllowed: phase === 'ready', inputReady: phase === 'ready', ...extra });
  const wire = (name: string, value: any) => { for (const listener of [...(handlers.get(name) ?? [])]) listener(value); };
  const emit = (value: any) => wire('power.lifecycleChanged', value);
  return { app, source, observed, pending, timers, handlers, fact, emit, wire, get reads() { return reads; },
    async boot(publishReady = true) { await flushApp(); assert.equal(pending.length, 1); pending.shift().resolve(fact('resuming', 0, 1)); await flushApp(); app.ready(2); await flushApp();
      // A separate simulated Native current fact follows its successful commit.
      if (publishReady) { emit(fact('ready', 1, 1)); await flushApp(); } },
    async stop(expectedPublicPeers = 0) { const beforeHandlers = [...handlers].filter(([name]) => name !== 'fan.resume-ready').reduce((n, [, row]) => n + row.size, 0); app.stop(); off(); assert.equal(timers.size, 0, 'shared source returns every timer');
      assert.equal(handlers.get('fan.resume-ready')?.size, 0, 'App returns the private Fan listener');
      if (!expectedPublicPeers) assert.equal([...handlers.values()].reduce((n, row) => n + row.size, 0), 0, 'shared source returns every listener');
      else { assert.equal(source.getSharedPowerLifecycleDiagnostics().subscriberCount, expectedPublicPeers); assert.equal([...handlers.values()].reduce((n, row) => n + row.size, 0), beforeHandlers, 'peer retains exactly the original public listeners'); }
      while (pending.length) pending.shift().resolve(fact('ready', 999, 999)); await flushApp(); },
  };
}

function createActualAppResume(ports: any = {}): any {
  /*__ACTUAL_APP_RESUME__*/
  throw new Error('Canonical App source preparation required');
}
const flushApp = async () => { for (let i = 0; i < 80; i++) await Promise.resolve(); };
const sharedAppState = (generation: number, phase = 'ready', known = true) => ({ generation, phase, known, notificationPending: false, hardwareWritesAllowed: phase === 'ready' });

async function test(name: string, run: () => Promise<void>): Promise<void> {
  if (selectedCase && selectedCase !== name) return;
  if (appSleepPlanOnly !== name.startsWith('actual App sleep plan ')) return;
  if (appSleepConfirmationOnly && !name.startsWith('actual App sleep plan confirmation ')) return;
  if (!appSleepPlanOnly && appSharedSourceOnly !== name.startsWith('actual App shared source ')) return;
  if (!appSleepPlanOnly && !appSharedSourceOnly && appContinuationOnly !== name.startsWith('actual App continuation ')) return;
  if (appSchedulerOnly && !name.startsWith('actual App ')) return;
  await run();
  executedCases++;
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
  const transactionStart = appSource.indexOf('= runPowerResumeTransaction(targetGeneration, daemonRequired, {');
  const nativeCommit = appSource.indexOf('completeResume: (resumeGeneration, meta) => powerLifecycle.completeResume(resumeGeneration, meta)', transactionStart);
  assert(resumeReadyStart >= 0 && resumeReadyBody.includes('onFanResumeReady(resumeGeneration)') &&
    resumeReadyBody.includes('scheduleResumeTransaction(resumeGeneration);') &&
    fanReadyStart > resumeReadyStart && fanReadyBody.includes('void fanHostLifecycle.resumeAfterNativeReady(resumeGeneration, committedWake).then(') &&
    transactionStart >= 0 && nativeCommit > transactionStart &&
    !appSource.slice(transactionStart, transactionStart + 1200).includes('resumeFanBeforeCommit'),
  'App must start the FanHost F5 fire-and-forget from resume-ready and never gate the native Ready commit on it');
});

await test('native commit does not wait for InputHost recovery', async () => {
  const nativeSource = readFileSync('native/main.cpp', 'utf8');
  const resumeComplete = nativeSource.indexOf('ipc_on("power.resumeComplete"');
  const resumeCompleteEnd = nativeSource.indexOf('\n    });', resumeComplete);
  const helper = nativeSource.indexOf('static bool commitPowerResume(unsigned long long generation, const char* source, uint64_t ticket) {');
  const helperEnd = nativeSource.indexOf('\n}', helper);
  const gateStart = nativeSource.indexOf('static bool commitPowerResumeGate(');
  const gateEnd = nativeSource.indexOf('\n}', gateStart);
  const inputDispatch = nativeSource.indexOf('case WM_POWER_RESUME_COMMIT:');
  const inputRecoverCase = nativeSource.indexOf('case WM_POWER_INPUT_RECOVER:');
  assert(resumeComplete >= 0 && resumeCompleteEnd > resumeComplete && helper > resumeCompleteEnd && helperEnd > helper &&
    gateStart >= 0 && gateEnd > gateStart && inputDispatch > helperEnd && inputRecoverCase > inputDispatch,
    'actual IPC, commit helper and post-commit handlers must be present in order');
  const receiver = nativeSource.slice(resumeComplete, resumeCompleteEnd);
  const commit = nativeSource.slice(helper, helperEnd);
  const gate = nativeSource.slice(gateStart, gateEnd);
  const receipt = gate.indexOf('powerResumeBoundaryCurrent(receipt.generation,receipt.boundary)');
  const ready = gate.indexOf('g_powerLifecycle.compare_exchange_strong(expected,PowerLifecycle::Ready');
  const write = gate.indexOf('g_hardwareWriteGate.store(true');
  const open = commit.indexOf('if(!commitPowerResumeGate(receipt,notification,restartInput,fanOnly))return false;');
  const publish = commit.indexOf('publishSharedPowerLifecycle("native-ready-commit")');
  const post = commit.indexOf('sleepPowerPostResumeCommit(g_hwnd,generation)');
  assert(receiver.includes('phase != PowerLifecycle::Ready') && receiver.includes('captureSharedPowerLifecycle(ready)') &&
    receiver.includes('ready.generation!=current') && !receiver.includes('commitPowerResume('),
    'renderer IPC can acknowledge current Ready but cannot commit it');
  assert(receipt >= 0 && ready > receipt && write > ready && open >= 0 && publish > open && post > publish &&
    commit.includes('readPowerResumeReady(generation,ticket)') &&
    nativeSource.slice(inputDispatch, inputRecoverCase).includes('PostMessageW(h, WM_POWER_INPUT_RECOVER'),
    'native source must commit the power gate before posting InputHost recovery');
  assert(!/\b(?:gamepadRecoverAfterResume|inputHostRequestPowerRearm)\s*\(/.test(receiver + commit + gate),
    'power.resumeComplete and the shared commit helper must not synchronously run gamepad/InputHost recovery');
});

await test('native self-commits the wake gate on resume-ready (HC SystemReady parity, G8)', async () => {
  const nativeSource = readFileSync('native/main.cpp', 'utf8');
  const readyCase = nativeSource.indexOf('case WM_POWER_RESUME_READY:');
  const readyCaseEnd = nativeSource.indexOf('case WM_POWER_RESUME_COMMIT:', readyCase);
  const body = nativeSource.slice(readyCase, readyCaseEnd > readyCase ? readyCaseEnd : readyCase + 2400);
  const worker = body.indexOf('!readPowerResumeReady(generation,ticket).worker');
  const commit = body.indexOf('commitPowerResume(generation, "native.resume-ready",ticket)');
  assert(readyCase >= 0 && worker >= 0 && commit > worker && body.includes('sgSleepRetryActive()'),
    'WM_POWER_RESUME_READY must self-commit from the current worker receipt after the retry veto');
  const helper = nativeSource.indexOf('static bool commitPowerResume(unsigned long long generation, const char* source, uint64_t ticket) {');
  assert(helper > nativeSource.indexOf('ipc_on("power.resumeComplete"'),
    'commitPowerResume must stay after the renderer IPC registration (source-order contract)');
});

await test('Fan manual wake is a Fan-only same-generation rescue and never opens during Suspending', async () => {
  const nativeSource = readFileSync('native/main.cpp', 'utf8');
  const isolatedGateStart = nativeSource.indexOf('static bool aiIsolatedIpcAllowed(');
  const isolatedGateEnd = nativeSource.indexOf('\nstatic void ipc_on(', isolatedGateStart);
  const isolatedGate = nativeSource.slice(isolatedGateStart, isolatedGateEnd);
  // Check membership in the actual isolated gate, independent of list order
  // or additional safe commands such as power.fanLifecycle.
  const safeSet = isolatedGate.match(/static const std::set<std::string> safe\s*=\s*\{([\s\S]*?)\};\s*return safe\.count\(cmd\) != 0;/);
  const safeMembers = new Set(safeSet ? [...safeSet[1].matchAll(/"([^"\n]+)"/g)].map(match => match[1]) : []);
  const handler = nativeSource.indexOf('ipc_on("power.fanManualWake"');
  const handlerEnd = nativeSource.indexOf('\n    // ── proc.running', handler);
  assert(isolatedGateStart >= 0 && isolatedGateEnd > isolatedGateStart &&
    ['power.lifecycle', 'power.activeScheme', 'power.hibernateState', 'power.resumeComplete', 'power.fanManualWake'].every(command => safeMembers.has(command)),
    'power.fanManualWake must be allowed through the isolated IPC command gate');
  assert(handler >= 0 && handlerEnd > handler, 'power.fanManualWake must have one registered handler');
  const body = nativeSource.slice(handler, handlerEnd);
  assert(body.includes('phase == PowerLifecycle::Suspending') && body.includes('power_suspending'),
    'manual Fan wake must reject an active Suspending phase');
  assert(body.includes('generation != current') && body.includes('stale_generation'),
    'manual Fan wake must reject a stale generation');
  assert(body.includes('const auto sharedCurrent = currentPowerGeneration();') &&
    body.includes('const auto current = currentFanPowerGeneration();') &&
    body.includes('const auto generation = requested == 0 ? current : requested;'),
    'manual Fan wake must bind its requested Fan generation separately from the shared power generation');
  const observation = body.indexOf('if (a.value("observedWakeOnly", false))');
  const suspending = body.indexOf('if (phase == PowerLifecycle::Suspending || newerSleepObserved)');
  const observedRecovery = body.indexOf('fanHostScheduleFanResume(current)', observation);
  const observationGate = body.indexOf('!fanHostPowerMayWrite()', observation);
  const read = body.indexOf('capturePowerObservation(&readSharedSystemPower,power)', suspending);
  const pending = body.indexOf('kNotificationPending', read);
  const current = body.indexOf('powerObservationCurrent(&readSharedSystemPower,power)', pending);
  const waiting = body.indexOf('"awaiting_wake_evidence"', current);
  const close = body.indexOf('g_fanPhysicalWake.needsClose(current)', waiting);
  const manualRecovery = body.indexOf('fanHostScheduleFanResume(current,true)', close);
  assert(!/\b(?:commitPowerResume(?:Gate)?|recordPowerResumeReady)\s*\(/.test(body) &&
    !/\b(?:g_powerLifecycle|g_resumeReadyGeneration|g_hardwareWriteGate)\s*\.\s*(?:store|exchange|compare_exchange_\w+)\s*\(/.test(body),
    'manual Fan wake cannot create or commit a shared Ready receipt');
  assert(observation > body.indexOf('generation != current') && observationGate > observation &&
    observedRecovery > observationGate && suspending > observedRecovery &&
    !body.slice(observation, suspending).includes('commitPowerResume('),
    'observed Fan wake may join only Native admission and must not promote the shared generation');
  assert(read > suspending && pending > read && current > pending && waiting > current && close > waiting &&
    manualRecovery > close && body.slice(waiting,close).includes('"awaiting-power"') &&
    body.slice(waiting,close).includes('{"resumeScheduled",false}') && !body.includes('fanHostScheduleEmergencyResume(current)'),
    'manual Fan wake must retain a missing or pending observation without scheduling work before current Fan admission');
  assert(!body.includes('realBackend') && !body.includes('hardwareWrite'),
    'manual Fan wake must not write hardware directly or bypass Host admission');
});

await test('committed Ready compensates a missed resume-ready through the same queue without restarting FanHost F5', async () => {
  const appSource = readFileSync('src/App.vue', 'utf8');
  const resumedStart = appSource.indexOf('function onPowerResumed(');
  const resumedEnd = appSource.indexOf('\nfunction ', resumedStart + 1);
  const resumedBody = appSource.slice(resumedStart, resumedEnd > resumedStart ? resumedEnd : resumedStart + 1200);
  assert(resumedStart >= 0 && resumedEnd > resumedStart && resumedBody.includes('scheduleResumeTransaction(generation);') &&
    resumedBody.includes('requestFanCommittedResume(generation);') &&
    !resumedBody.includes('onFanResumeReady(') && !resumedBody.includes('fanHostLifecycle.resume()'),
    'shared Ready must compensate missed frontend work through the coalesced queue; Fan F5 stays independently owned');
  assert(appSource.includes('void resumeQueue.run(generation, async (ownsQueue) => {') &&
    appSource.includes('resumeWorkAllowed = state.known !== false && state.notificationPending !== true') &&
    appSource.includes('else if (!first || confirmedF5) onPowerResumed(event);'),
    'App must use the actual queue and current native admission for event and remount compensation');
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
    fanBlock.includes('void fanHostLifecycle.resumeAfterNativeReady(resumeGeneration, committedWake).then(') &&
    fanBlock.includes("fanDiagnosticLog('lifecycle.resume-fire-and-forget-failed'") &&
    !/\bawait\s+fanHostLifecycle\.(?:resume|resumeAfterNativeReady)\(/.test(fanBlock),
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

await test('a pending old generation cannot block its successor', async () => {
  const queue = createPowerResumeQueue();
  let release!: () => void;
  const held = new Promise<void>(resolve => { release = resolve; });
  const order: string[] = [];
  const old = queue.run(20, async current => { order.push('old'); await held; if (current()) order.push('stale-write'); });
  await Promise.resolve();
  const next = queue.run(21, async () => { order.push('next'); });
  await Promise.resolve(); await Promise.resolve();
  const successorStarted = order.includes('next');
  release(); await Promise.all([old, next]);
  assert.equal(successorStarted, true, 'a pending old generation must not serialize the new wake');
  assert.deepEqual(order, ['old', 'next']);
});

await test('duplicate notifications share one current task', async () => {
  const queue = createPowerResumeQueue(); let calls = 0; let release!: () => void;
  const held = new Promise<void>(resolve => { release = resolve; });
  const first = queue.run(22, async () => { calls++; await held; });
  const second = queue.run(22, async () => { calls++; });
  assert.equal(first, second); await Promise.resolve(); assert.equal(calls, 1);
  release(); await first;
});

await test('late older notifications cannot retire the current generation', async () => {
  const queue = createPowerResumeQueue(); let calls = 0; let release!: () => void;
  const held = new Promise<void>(resolve => { release = resolve; });
  const current = queue.run(24, async owns => { await held; assert.equal(owns(), true); calls++; });
  await Promise.resolve(); await queue.run(23, async () => { calls += 100; });
  release(); await current; assert.equal(calls, 1);
});

await test('old completion cannot clear the successor task', async () => {
  const queue = createPowerResumeQueue(); let releaseOld!: () => void; let releaseNew!: () => void;
  const oldGate = new Promise<void>(resolve => { releaseOld = resolve; });
  const newGate = new Promise<void>(resolve => { releaseNew = resolve; });
  const old = queue.run(25, async () => { await oldGate; }); await Promise.resolve();
  const next = queue.run(26, async () => { await newGate; }); await Promise.resolve();
  releaseOld(); await old;
  assert.equal(queue.run(26, async () => { throw Error('duplicate successor'); }), next);
  releaseNew(); await next;
});

await test('interrupted same-generation work loses consumption and Ready can recover', async () => {
  const queue = createPowerResumeQueue(); let release!: () => void; let writes = 0;
  const held = new Promise<void>(resolve => { release = resolve; });
  const old = queue.run(27, async current => { await held; if (current()) writes += 100; });
  await Promise.resolve(); queue.invalidate();
  const recovered = queue.run(27, async current => { if (current()) writes++; });
  await recovered; release(); await old; assert.equal(writes, 1);
});

await test('a failed task returns its slot without clearing a newer owner', async () => {
  const queue = createPowerResumeQueue(); let reject!: (error: Error) => void; let release!: () => void;
  const held = new Promise<void>((_, fail) => { reject = fail; });
  const old = queue.run(28, async () => { await held; }).catch(() => {}); await Promise.resolve();
  const next = queue.run(29, async () => { await new Promise<void>(resolve => { release = resolve; }); });
  await Promise.resolve(); reject(Error('old transport')); await old;
  assert.equal(queue.run(29, async () => { throw Error('duplicate'); }), next);
  release(); await next;
  await assert.rejects(queue.run(29, async () => { throw Error('current transport'); }), /current transport/);
  let recovered = false; await queue.run(29, async () => { recovered = true; }); assert.equal(recovered, true);
});

await test('twenty interrupted cycles return ownership and reject late callbacks', async () => {
  const queue = createPowerResumeQueue(); let writes = 0;
  for (let generation = 30; generation < 50; generation++) {
    let release!: () => void; const held = new Promise<void>(resolve => { release = resolve; });
    const old = queue.run(generation, async current => { await held; if (current()) writes += 100; });
    await Promise.resolve(); queue.invalidate();
    await queue.run(generation, async current => { assert(current()); writes++; });
    release(); await old;
  }
  queue.invalidate(); await queue.run(49, async current => { queue.invalidate(); assert.equal(current(), false); });
  await queue.run(48, async () => { writes += 100; }); assert.equal(writes, 20);
});

await test('actual App retains resume-ready before its first known Ready and leaves cold Ready idle', async () => {
  const cold = createActualAppResume(); cold.observe(sharedAppState(1)); await flushApp();
  assert.equal(cold.calls.filter((r: any) => r[0] === 'commit').length, 0); cold.stop();
  const f = createSharedAppFixture();
  try {
    f.wire('power.resume-ready', { generation: 2 }); f.wire('power.resume-ready', { generation: 2 }); await flushApp();
    assert.equal(f.app.calls.length, 0, 'unknown authority holds the remembered request');
    assert.equal(f.pending.length, 1);
    f.pending.shift().resolve(f.fact('ready', 1, 1, { resumeReady: true })); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]], 'first known Ready must consume the earlier resume-ready request');
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
  } finally { await f.stop(); }
});

await test('actual App successor bypasses a pending ownership read and retires its old continuation', async () => {
  let release!: (v: string) => void, reads = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const app = createActualAppResume({ ownership: () => ++reads === 1 ? held : Promise.resolve('none') });
  app.observe(sharedAppState(2, 'resuming')); app.ready(2); await flushApp(); assert.equal(reads, 1);
  app.observe(sharedAppState(3, 'suspending')); app.observe(sharedAppState(3, 'resuming')); app.ready(3); await flushApp(); app.observe(sharedAppState(3)); await flushApp();
  assert.deepEqual(app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 3]]);
  release('auto'); await flushApp();
  assert.deepEqual(app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 3]]);
  assert.deepEqual(app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 3]]); app.stop();
});

await test('actual App unknown fence cancels consumption while same-generation Ready recovers', async () => {
  let release!: (v: { ok: boolean }) => void, commits = 0;
  const held = new Promise<{ ok: boolean }>(resolve => { release = resolve; });
  const app = createActualAppResume({ commit: () => ++commits === 1 ? held : Promise.resolve({ ok: true }) });
  app.observe(sharedAppState(2, 'resuming')); app.ready(2); await flushApp(); assert.equal(commits, 1);
  app.observe(sharedAppState(2, 'ready', false)); app.ready(2); await flushApp(); assert.equal(commits, 1);
  app.observe(sharedAppState(2)); await flushApp(); release({ ok: true }); await flushApp();
  assert.equal(commits, 2, 'same native commit is idempotent; only its current renderer consumes it');
  assert.deepEqual(app.calls.filter((r: any) => r[0] === 'daemon'), [['daemon', 2]]);
  assert.deepEqual(app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
  app.stop(); app.observe(sharedAppState(3)); app.ready(3); await flushApp(); assert.equal(commits, 2);
});

await test('actual App retires a held post-commit scheme read and starts resident healing first', async () => {
  let release!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const app = createActualAppResume({ scheme: () => ++schemes === 1 ? held : Promise.resolve('auto') });
  app.observe(sharedAppState(2, 'resuming')); app.ready(2); await flushApp(); app.observe(sharedAppState(2)); await flushApp();
  assert.deepEqual(app.calls.filter((r: any) => r[0] === 'policy'), [['policy', 2]], 'resident healing must not wait for the scheme read');
  app.observe(sharedAppState(3, 'suspending')); app.observe(sharedAppState(3, 'resuming')); app.ready(3); await flushApp(); app.observe(sharedAppState(3)); await flushApp();
  release('auto'); await flushApp();
  assert.deepEqual(app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2], ['commit', 3]]);
  assert.deepEqual(app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 3]]); app.stop();
});


await test('actual App continuation same-generation Ready completes retained scheme work without recommit', async () => {
  let release!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const app = createActualAppResume({ scheme: () => { schemes++; return held; } });
  try {
    app.observe(sharedAppState(2, 'resuming')); app.ready(2); await flushApp(); app.observe(sharedAppState(2)); await flushApp();
    assert.equal(app.state().committed, 2);
    assert.equal(schemes, 1);
    app.observe(sharedAppState(2, 'ready', false));
    release('auto'); await flushApp();
    assert.equal(app.calls.filter((r: any) => r[0] === 'performance').length, 0, 'revoked work must not apply the followup');
    app.observe(sharedAppState(2)); await flushApp();
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]], 'same-generation Ready must finish the unconsumed post-commit stage');
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]], 'the confirmed native commit is retained');
    assert.equal(schemes, 1, 'reuse the exact settled scheme operation');
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'daemon'), [['daemon', 2]]);
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'policy'), [['policy', 2]]);
  } finally { release('auto'); app.stop(); await flushApp(); }
});


await test('actual App continuation Ready shares a still-running scheme across pending revocation', async () => {
  let release!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const app = createActualAppResume({ scheme: () => { schemes++; return held; } });
  try {
    app.observe(sharedAppState(2, 'resuming')); app.ready(2); await flushApp(); app.observe(sharedAppState(2)); await flushApp();
    app.observe({ ...sharedAppState(2), notificationPending: true });
    app.observe(sharedAppState(2)); app.ready(2); await flushApp();
    assert.equal(schemes, 1, 'pending-to-Ready cannot issue another scheme operation');
    assert.equal(app.calls.filter((r: any) => r[0] === 'performance').length, 0);
    release('manual'); await flushApp();
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
    assert.equal(app.state().followup.completed, true);
  } finally { release('manual'); app.stop(); await flushApp(); }
});
await test('actual App continuation shares an admitted performance operation and does not replay healthy work', async () => {
  let release!: () => void, schemes = 0, applications = 0;
  const held = new Promise<void>(resolve => { release = resolve; });
  const app = createActualAppResume({ scheme: async () => { schemes++; return 'auto'; }, performance: () => { applications++; return held; } });
  try {
    app.observe(sharedAppState(2, 'resuming')); app.ready(2); await flushApp(); app.observe(sharedAppState(2)); await flushApp();
    assert.equal(applications, 1);
    app.observe(sharedAppState(2, 'ready', false)); app.observe(sharedAppState(2)); app.ready(2); await flushApp();
    assert.equal(applications, 1, 'an in-flight performance action is never reissued');
    release(); await flushApp();
    assert.equal(app.state().followup.completed, true);
    app.observe({ ...sharedAppState(2), hardwareWritesAllowed: false });
    app.observe(sharedAppState(2)); app.ready(2); await flushApp();
    assert.equal(schemes, 1); assert.equal(applications, 1);
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'daemon'), [['daemon', 2]]);
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'policy'), [['policy', 2]]);
  } finally { release(); app.stop(); await flushApp(); }
});
await test('actual App continuation a new generation bypasses the old held stage and keeps its own completion', async () => {
  let release!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const app = createActualAppResume({ scheme: () => ++schemes === 1 ? held : Promise.resolve('auto') });
  try {
    app.observe(sharedAppState(2, 'resuming')); app.ready(2); await flushApp(); app.observe(sharedAppState(2)); await flushApp();
    app.observe(sharedAppState(3, 'suspending')); app.observe(sharedAppState(3, 'resuming')); app.ready(3); await flushApp(); app.observe(sharedAppState(3)); await flushApp();
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2], ['commit', 3]]);
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 3]]);
    assert.equal(app.state().followup.generation, 3); assert.equal(app.state().followup.completed, true);
    release('auto'); await flushApp();
    assert.deepEqual(app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 3]]);
    app.ready(2); await flushApp(); assert.equal(schemes, 2);
    assert.equal(app.state().followup.generation, 3); assert.equal(app.state().followup.completed, true);
  } finally { release('auto'); app.stop(); await flushApp(); }
});
await test('actual App continuation terminal stop rejects a retained late completion', async () => {
  let release!: (v: string) => void;
  const held = new Promise<string>(resolve => { release = resolve; });
  const app = createActualAppResume({ scheme: () => held });
  app.observe(sharedAppState(2, 'resuming')); app.ready(2); await flushApp(); app.observe(sharedAppState(2)); await flushApp();
  app.stop(); release('auto'); await flushApp();
  app.observe(sharedAppState(2)); app.ready(2); app.observe(sharedAppState(3)); app.ready(3); await flushApp(); app.observe(sharedAppState(3)); await flushApp();
  assert.equal(app.calls.filter((r: any) => r[0] === 'performance').length, 0);
  assert.deepEqual(app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
});


await test('actual App shared source Resuming cannot consume a retained post-commit stage', async () => {
  let release!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const f = createSharedAppFixture({ scheme: () => { schemes++; return held; } });
  try {
    await f.boot(); assert.equal(f.app.state().committed, 2); assert.equal(schemes, 1);
    f.emit({ known: false, publicationRevision: 2, observedNotificationSequence: 2 });
    release('auto'); await flushApp();
    assert.equal(f.app.calls.filter((r: any) => r[0] === 'performance').length, 0);
    f.emit(f.fact('resuming', 3, 2)); f.app.ready(2); await flushApp();
    assert.equal(f.source.getSharedPowerLifecycle().phase, 'resuming');
    assert.equal(isSharedPowerLifecycleAdmitted(f.source.getSharedPowerLifecycle()), false);
    assert.equal(f.app.calls.filter((r: any) => r[0] === 'performance').length, 0, 'shared Resuming cannot consume a post-commit continuation');
    f.emit(f.fact('ready', 4, 2)); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
    assert.equal(schemes, 1); assert.equal(f.app.state().followup.completed, true);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'daemon'), [['daemon', 2]]);
  } finally { release('auto'); await f.stop(); }
});
await test('actual App shared source restarts only an unsent scheme stage after merged Ready', async () => {
  let retire!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { retire = resolve; });
  const f = createSharedAppFixture({ scheme: () => ++schemes === 1 ? held : Promise.resolve('auto') });
  try {
    await f.boot(); assert.equal(schemes, 1);
    f.emit(f.fact('ready', 2, 1)); await flushApp();
    assert.equal(schemes, 1, 'republishing Ready alone does not restart the stage');
    f.emit(f.fact('ready', 3, 2)); retire('retry-on-ready'); await flushApp();
    assert.equal(schemes, 2);
    assert.deepEqual(f.app.calls.filter((r: any) => ['commit', 'daemon', 'policy', 'performance'].includes(r[0])),
      [['commit', 2], ['daemon', 2], ['policy', 2], ['performance', 2]]);
    assert.equal(f.app.state().followup.completed, true);
  } finally { retire('retry-on-ready'); await f.stop(); }
});
await test('actual App shared source an unconfirmed scheme write stays spent after restored Ready', async () => {
  let finish!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { finish = resolve; });
  const f = createSharedAppFixture({ scheme: () => { schemes++; return held; } });
  try {
    await f.boot();
    f.emit(f.fact('ready', 2, 1, { hardwareWritesAllowed: false }));
    f.emit(f.fact('ready', 3, 2)); await flushApp();
    assert.equal(schemes, 1); assert.equal(f.app.state().followup.completed, false);
    finish('unconfirmed'); await flushApp();
    assert.equal(schemes, 1); assert.equal(f.app.state().followup.completed, true);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
  } finally { finish('unconfirmed'); await f.stop(); }
});
await test('actual App shared source non-admitted Ready holds and hardware admission alone releases continuation', async () => {
  let release!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const f = createSharedAppFixture({ scheme: () => { schemes++; return held; } });
  try {
    await f.boot(); f.emit({ known: false, publicationRevision: 2, observedNotificationSequence: 2 }); release('manual'); await flushApp();
    f.emit(f.fact('ready', 3, 2, { hardwareWritesAllowed: false })); await flushApp();
    assert.equal(f.app.calls.filter((r: any) => r[0] === 'performance').length, 0, 'diagnostic Ready cannot consume denied followup');
    f.emit(f.fact('ready', 4, 2)); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]], 'admission-only correction must schedule the retained stage');
    assert.equal(f.app.state().followup.completed, true); assert.equal(schemes, 1);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
    const actions = f.app.calls.filter((r: any) => ['commit', 'daemon', 'policy', 'performance'].includes(r[0]));
    f.emit(f.fact('ready', 5, 2)); f.app.ready(2); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => ['commit', 'daemon', 'policy', 'performance'].includes(r[0])), actions, 'healthy metadata cannot replay settled work');
  } finally { release('manual'); await f.stop(); }
});
await test('actual App shared source delayed old sequence and denied read cannot reopen continuation', async () => {
  let release!: (v: string) => void;
  const held = new Promise<string>(resolve => { release = resolve; });
  const f = createSharedAppFixture({ scheme: () => held });
  try {
    await f.boot(); f.emit({ known: false, publicationRevision: 2, observedNotificationSequence: 2 }); release('auto'); await flushApp();
    if (f.pending.length) { f.pending.shift().resolve({ known: false, publicationRevision: 2, observedNotificationSequence: 2 }); await flushApp(); }
    f.emit(f.fact('ready', 999, 1)); f.app.ready(2); await flushApp();
    assert.equal(f.source.getSharedPowerLifecycle().known, false);
    assert.equal(f.source.getSharedPowerLifecycle().publicationRevision, 2);
    assert.equal(f.source.getSharedPowerLifecycle().observedNotificationSequence, 2);
    assert.equal(f.app.calls.filter((r: any) => r[0] === 'performance').length, 0);
    assert.equal(f.timers.size, 1, 'retained public demand has one original retry');
    const [id, timer] = [...f.timers][0] as any; f.timers.delete(id); timer.fn(); await flushApp();
    assert.equal(f.pending.length, 1); f.pending.shift().resolve(f.fact('ready', 2, 2, { hardwareWritesAllowed: false })); await flushApp();
    f.app.ready(2); await flushApp();
    assert.equal(f.app.calls.filter((r: any) => r[0] === 'performance').length, 0, 'denied authoritative read cannot start the continuation');
    f.emit(f.fact('ready', 3, 2)); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
    assert.equal(f.timers.size, 0);
  } finally { release('auto'); await f.stop(); }
});
await test('actual App shared source retains an admitted in-flight action through denied Ready and stop', async () => {
  let release!: () => void, applications = 0;
  const held = new Promise<void>(resolve => { release = resolve; });
  const f = createSharedAppFixture({ performance: () => { applications++; return held; } });
  try {
    await f.boot(); assert.equal(applications, 1);
    f.emit({ known: false, publicationRevision: 2, observedNotificationSequence: 2 });
    f.emit(f.fact('ready', 3, 2, { hardwareWritesAllowed: false })); f.app.ready(2); await flushApp();
    assert.equal(applications, 1, 'denied state cannot reissue an existing action');
    release(); await flushApp(); assert.equal(f.app.state().followup.completed, false, 'denied Ready cannot consume a late operation receipt');
    f.emit(f.fact('ready', 4, 2)); await flushApp();
    assert.equal(f.app.state().followup.completed, true); assert.equal(applications, 1);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
    await f.stop(); f.emit(f.fact('ready', 5, 2)); f.app.ready(2); await flushApp(); assert.equal(applications, 1);
  } finally { release(); await f.stop(); }
  for (const previous of ['retry-on-ready', 'unconfirmed'] as const) {
    let releaseResult!: (value: string) => void;
    let applications = 0, schemes = 0, firstAdmission: (() => boolean) | undefined;
    const heldResult = new Promise<string>(resolve => { releaseResult = resolve; });
    const fixture = createSharedAppFixture({
      scheme: async () => { schemes++; return 'auto'; },
      performance: (_mode: string, { ownsWake }: { ownsWake: () => boolean }) => {
        assert.equal(ownsWake(), true);
        if (++applications === 1) { firstAdmission = ownsWake; return heldResult; }
        return 'settled';
      },
    });
    try {
      await fixture.boot();
      assert.equal(applications, 1);
      fixture.emit(fixture.fact('ready', 2, 2, { hardwareWritesAllowed: false }));
      assert.equal(firstAdmission?.(), false);
      fixture.emit(fixture.fact('ready', 3, 2));
      await flushApp();
      assert.equal(applications, 1, 'successor waits for the original receipt');
      assert.equal(firstAdmission?.(), false, 'Ready cannot revive the retired attempt');
      releaseResult(previous);
      await flushApp();
      assert.equal(applications, previous === 'retry-on-ready' ? 2 : 1, previous);
      assert.equal(fixture.app.state().followup.completed, true);
      assert.equal(schemes, 1);
      assert.deepEqual(fixture.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
    } finally { releaseResult(previous); await fixture.stop(); }
  }
});


await test('actual App shared source admission correction resumes only retained stages without wake broadcast replay', async () => {
  let release!: (v: string) => void, fan = 0, plans = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const f = createSharedAppFixture({ scheme: () => held, fanCommitted: () => { fan++; }, sleepPlan: () => { plans++; } });
  try {
    await f.boot(); f.emit({ known: false, publicationRevision: 2, observedNotificationSequence: 2 }); release('auto'); await flushApp();
    f.emit(f.fact('ready', 3, 2, { hardwareWritesAllowed: false })); await flushApp();
    const before = { fan, plans, network: f.app.calls.filter((r: any) => r[0] === 'network').length };
    f.emit(f.fact('ready', 4, 2)); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
    assert.deepEqual({ fan, plans }, { fan: before.fan, plans: before.plans }, 'admission-only correction cannot replay the full wake broadcast');
    assert.equal(f.app.calls.filter((r: any) => r[0] === 'network').length, before.network + 1,
      'the independent network owner receives the restored admission');
    assert.equal(f.app.state().followup.completed, true);
    const all = structuredClone(f.app.calls);
    f.emit(f.fact('ready', 5, 2, { hardwareWritesAllowed: false })); f.emit(f.fact('ready', 6, 2)); f.app.ready(2); await flushApp();
    assert.deepEqual(f.app.calls, [...all, ['network', 2]], 'settled toggles notify only the independent network owner');
    assert.deepEqual({ fan, plans }, { fan: before.fan, plans: before.plans });
  } finally { release('auto'); await f.stop(); }
});


await test('actual App shared source first flight waits for public Ready after native commit', async () => {
  let schemes = 0;
  const f = createSharedAppFixture({ scheme: async () => { schemes++; return 'auto'; } });
  try {
    await f.boot(false); assert.equal(f.app.state().committed, 2); assert.equal(f.source.getSharedPowerLifecycle().phase, 'resuming');
    assert.equal(schemes, 0, 'first committed task cannot start stages before public Ready');
    assert.equal(f.app.calls.filter((r: any) => r[0] === 'performance').length, 0);
    f.emit(f.fact('ready', 1, 1)); await flushApp();
    assert.equal(schemes, 1); assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]); assert.equal(f.app.state().followup.completed, true);
  } finally { await f.stop(); }
});
await test('actual App shared source first flight pure hardware denial retires the scheme consumer', async () => {
  let release!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const f = createSharedAppFixture({ scheme: () => { schemes++; return held; } });
  try {
    await f.boot(); assert.equal(schemes, 1); assert.equal(isSharedPowerLifecycleAdmitted(f.source.getSharedPowerLifecycle()), true);
    f.emit(f.fact('ready', 2, 2, { hardwareWritesAllowed: false })); release('auto'); await flushApp();
    assert.equal(f.app.calls.filter((r: any) => r[0] === 'performance').length, 0, 'first in-flight task cannot consume a scheme result after pure hardware denial');
    assert.equal(f.app.state().followup.completed, false);
    f.emit(f.fact('ready', 3, 2)); await flushApp();
    assert.equal(schemes, 1); assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
  } finally { release('auto'); await f.stop(); }
});
await test('actual App shared source first flight restored admission never revives the retired queue owner', async () => {
  let release!: (v: string) => void;
  const held = new Promise<string>(resolve => { release = resolve; }), owners: Array<() => boolean> = [];
  const f = createSharedAppFixture({ scheme: () => held, queueOwner: (_g: number, current: () => boolean) => { owners.push(current); } });
  try {
    await f.boot(); const first = owners[owners.length - 1]; assert(first());
    f.emit(f.fact('ready', 2, 2, { hardwareWritesAllowed: false }));
    assert.equal(first(), false, 'pure hardware denial retires the original queue owner immediately');
    f.emit(f.fact('ready', 3, 2)); await flushApp(); assert.equal(first(), false, 'Ready must not revive retired ownership');
    assert(owners.length >= 2); release('manual'); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
    assert.equal(first(), false); assert.equal(f.app.state().followup.completed, true);
  } finally { release('manual'); await f.stop(); }
});
await test('actual App shared source first flight retains one performance action through pure denial', async () => {
  let release!: () => void, applications = 0;
  const held = new Promise<void>(resolve => { release = resolve; });
  const f = createSharedAppFixture({ performance: () => { applications++; return held; } });
  try {
    await f.boot(); assert.equal(applications, 1);
    f.emit(f.fact('ready', 2, 2, { hardwareWritesAllowed: false })); release(); await flushApp();
    assert.equal(f.app.state().followup.completed, false, 'pure denial cannot consume a late performance receipt');
    f.emit(f.fact('ready', 3, 2)); await flushApp(); assert.equal(applications, 1); assert.equal(f.app.state().followup.completed, true);
    const actions = structuredClone(f.app.calls); f.emit(f.fact('ready', 4, 2)); f.app.ready(2); await flushApp(); assert.deepEqual(f.app.calls, actions);
  } finally { release(); await f.stop(); }
});
await test('actual App shared source merged Ready retries only the zero-action performance stage', async () => {
  let retire!: (result: string) => void, applications = 0, schemes = 0;
  const held = new Promise<string>(resolve => { retire = resolve; });
  const f = createSharedAppFixture({ scheme: async () => { schemes++; return 'auto'; },
    performance: () => ++applications === 1 ? held : Promise.resolve('settled') });
  try {
    await f.boot(); assert.equal(applications, 1);
    f.emit(f.fact('ready', 2, 1)); await flushApp(); assert.equal(applications, 1);
    f.emit(f.fact('ready', 3, 2)); retire('retry-on-ready'); await flushApp();
    assert.equal(applications, 2); assert.equal(schemes, 1);
    assert.deepEqual(f.app.calls.filter((r: any) => ['commit', 'daemon', 'policy'].includes(r[0])),
      [['commit', 2], ['daemon', 2], ['policy', 2]]);
    assert.equal(f.app.state().followup.completed, true);
  } finally { retire('retry-on-ready'); await f.stop(); }
});
await test('actual App shared source first flight a new generation bypasses old stage and stop blocks late Ready', async () => {
  let release!: (v: string) => void, schemes = 0;
  const held = new Promise<string>(resolve => { release = resolve; });
  const f = createSharedAppFixture({ scheme: () => ++schemes === 1 ? held : Promise.resolve('auto') });
  try {
    await f.boot(); f.emit(f.fact('suspending', 2, 2, { generation: 3 })); f.emit(f.fact('resuming', 3, 2, { generation: 3 })); f.app.ready(3); await flushApp();
    f.emit(f.fact('ready', 4, 2, { generation: 3 })); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 3]]);
    release('auto'); await flushApp(); assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 3]]);
    await f.stop(); f.emit(f.fact('ready', 5, 2, { generation: 4 })); f.app.ready(4); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2], ['commit', 3]]);
  } finally { release('auto'); await f.stop(); }
});
await test('actual App shared source first flight Ready at worker completion still consumes the retained stage', async () => {
  let armed = true;
  const f = createSharedAppFixture({ sharedRead: (state: any) => {
    if (armed && state?.phase === 'resuming') { armed = false; queueMicrotask(() => f.emit(f.fact('ready', 1, 1))); }
  } });
  try {
    await f.boot(false); assert.equal(f.source.getSharedPowerLifecycle().phase, 'ready'); assert.equal(armed, false);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]], 'Ready delivered while the original queue finishes must not lose the retained stage');
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]); assert.equal(f.app.state().followup.completed, true);
  } finally { await f.stop(); }
});


await test('actual App shared source merged suspend retires an unfinished pre-commit read', async () => {
  for (const phase of ['resuming', 'ready']) {
    let release!: (owner: string) => void, reads = 0;
    const held = new Promise<string>(resolve => { release = resolve; });
    const f = createSharedAppFixture({ ownership: () => ++reads === 1 ? held : Promise.resolve('none') });
    try {
      await f.boot(false);
      f.emit(f.fact('resuming', 0, 1, { resumeReady: true })); await flushApp();
      assert.equal(reads, 1);
      assert.equal(f.app.calls.filter((r: any) => r[0] === 'commit').length, 0);
      f.emit(f.fact(phase, 0, 1, { resumeReady: true, suspendSerial: 1 })); await flushApp();
      assert.equal(reads, 2, `current ${phase} must not wait for the previous suspend scope`);
      assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]]);
      release('auto'); await flushApp();
      f.emit(f.fact('ready', 1, 1, { resumeReady: true, suspendSerial: 1 })); await flushApp();
      assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]], 'late ownership read must not submit an old commit');
      assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
    } finally { release('auto'); await f.stop(); }
  }
});

await test('actual App shared source newer scoped hint fences unfinished Ready work until current readback', async () => {
  for (const stage of ['mode', 'ownership'] as const) {
    let release!: (value: string) => void, attempts = 0;
    const held = new Promise<string>(resolve => { release = resolve; });
    const answer = stage === 'mode' ? 'ac' : 'none';
    const f = createSharedAppFixture({ [stage]: () => ++attempts === 1 ? held : Promise.resolve(answer) });
    const calls = (name: string) => f.app.calls.filter((row: any) => row[0] === name);
    const suspendSerial = stage === 'mode' ? 0 : 1;
    const provisional = f.fact('ready', 2, 1, { hardwareWritesAllowed: false, inputReady: false });
    try {
      await flushApp(); assert.equal(f.pending.length, 1);
      f.pending.shift().resolve(f.fact('resuming', 1, 1, { resumeReady: false })); await flushApp();
      f.emit(provisional); await flushApp();
      assert.equal(attempts, 1); assert.equal(calls('commit').length, 0);
      f.wire('power.resumed', { generation: 2, readbackScope: { suspendSerial, notificationSequence: 2 } });
      assert.equal(f.observed.at(-1).scopeReadbackPending, true);
      assert.equal(f.observed.at(-1).phase, 'ready'); assert.equal(f.observed.at(-1).suspendSerial, 0);
      // Simulate a late old Ready publication while the scoped read is pending.
      f.emit(f.fact('ready', 3, 1)); await flushApp();
      if (stage === 'mode') { release(answer); await flushApp(); }
      assert.equal(f.source.getSharedPowerLifecycle().scopeReadbackPending, true);
      assert.deepEqual(f.app.calls, [], 'an obsolete Ready must not send resumeComplete or run follow-up work');

      assert.equal(f.timers.size, 1);
      const [id, retry] = [...f.timers][0] as [number, {fn:()=>void}];
      f.timers.delete(id); retry.fn(); await flushApp();
      assert.equal(f.pending.length, 1);
      f.pending.shift().resolve(f.fact('ready', 4, 2, { suspendSerial })); await flushApp();
      assert.equal(f.source.getSharedPowerLifecycle().scopeReadbackPending, undefined);
      assert.equal(attempts, 2, 'current Ready should resume even before the obsolete read returns');
      assert.deepEqual(calls('commit'), [['commit', 2]]);
      assert.deepEqual(calls('performance'), [['performance', 2]]);
      release(answer); f.emit(f.fact('ready', 5, 2, { suspendSerial })); await flushApp();
      assert.deepEqual(calls('commit'), [['commit', 2]], 'a late result must not replay resumeComplete');
      assert.deepEqual(calls('performance'), [['performance', 2]]);
    } finally { release(answer); await f.stop(); }
  }
});

await test('actual App shared source merged suspend preserves the acknowledged commit during daemon recovery', async () => {
  let release!: (ready: boolean) => void;
  const held = new Promise<boolean>(resolve => { release = resolve; });
  const f = createSharedAppFixture({ daemon: () => held });
  const calls = (name: string) => f.app.calls.filter((row: any) => row[0] === name);
  try {
    await f.boot(false);
    assert.deepEqual(calls('commit'), [['commit', 2]]);
    assert.deepEqual(calls('daemon'), [['daemon', 2]]);
    f.emit(f.fact('ready', 1, 2, { suspendSerial: 1, resumeReady: true })); await flushApp();
    assert.deepEqual(calls('commit'), [['commit', 2]], 'merged Ready must not resend an acknowledged Native commit');
    assert.deepEqual(calls('daemon'), [['daemon', 2]], 'the current queue must retain the dispatched daemon attempt');
    assert.deepEqual(calls('performance'), [], 'remaining stages keep their order');
    release(true); await flushApp();
    assert.deepEqual(calls('policy'), [['policy', 2]]);
    assert.deepEqual(calls('performance'), [['performance', 2]]);
    assert.equal(f.app.state().followup.completed, true);
    f.emit(f.fact('ready', 2, 2, { suspendSerial: 2, resumeReady: true })); await flushApp();
    assert.deepEqual(calls('commit'), [['commit', 2]]);
    assert.deepEqual(calls('performance'), [['performance', 2]], 'a revocation watermark alone cannot replay a settled stage');
    const backgroundChecks = calls('background-refresh').length;
    f.app.holdBackground();
    f.emit(f.fact('ready', 3, 2, { suspendSerial: 2, resumeReady: true })); await flushApp();
    assert.equal(calls('background-refresh').length, backgroundChecks, 'revision-only Ready does not replace the background read');
    f.emit(f.fact('ready', 4, 3, { suspendSerial: 2, resumeReady: true })); await flushApp();
    assert.equal(calls('background-refresh').length, backgroundChecks + 1, 'the interrupted background read must get a current-scope successor');
    f.emit(f.fact('ready', 5, 4, { suspendSerial: 2, resumeReady: true })); await flushApp();
    assert.equal(calls('background-refresh').length, backgroundChecks + 1, 'an idle new observation does not queue a second refresh');
    assert.deepEqual(calls('commit'), [['commit', 2]], 'background recovery has no effect on the shared wake commit');
  } finally { release(true); await f.stop(); }
});

await test('actual App shared source keeps a zero-action game restore eligible until recognition', async () => {
  for (const boundary of ['recognition', 'early-recognition', 'power-pending', 'new-sleep', 'unmount']) {
    let gameReady = false, attempts = 0, writes = 0;
    let release!: (value: string) => void;
    const original = new Promise<string>(resolve => { release = resolve; });
    const listeners = new Set<(ready: boolean) => void>();
    const publishReadiness = (ready: boolean) => {
      gameReady = ready;
      for (const listener of [...listeners]) listener(ready);
    };
    const f = createSharedAppFixture({
      gameReady: () => gameReady,
      subscribeGameReadiness(listener: (ready: boolean) => void) {
        listeners.add(listener); listener(gameReady);
        return () => { listeners.delete(listener); };
      },
      performance: () => {
        if (++attempts === 1) return original;
        assert.equal(gameReady, true, 'a pending game observation may not send another attempt');
        writes++;
        return 'settled';
      },
    });
    try {
      await f.boot(); assert.equal(attempts, 1); assert.equal(writes, 0);
      if (boundary === 'early-recognition') publishReadiness(true);
      if (boundary === 'power-pending') f.emit(f.fact('ready', 2, 2, { hardwareWritesAllowed: false }));
      release('retry-on-game-ready'); await flushApp();
      if (boundary !== 'early-recognition') {
        assert.equal(f.app.state().followup.completed, false, 'zero actions must not seal this wake');
        if (boundary !== 'power-pending') f.emit(f.fact('ready', 2, 2));
        await flushApp();
        assert.equal(attempts, 1, 'power metadata alone cannot retry an unconfirmed game');
      }
      if (boundary === 'new-sleep') f.emit(f.fact('suspending', 3, 3, { generation: 3, suspendSerial: 1 }));
      if (boundary === 'unmount') await f.stop();
      publishReadiness(true); await flushApp();
      if (boundary === 'power-pending') {
        assert.equal(attempts, 1, 'game readiness alone cannot bypass closed public admission');
        f.emit(f.fact('ready', 3, 2)); await flushApp();
      }
      const retired = boundary === 'unmount' || boundary === 'new-sleep';
      assert.equal(attempts, retired ? 1 : 2, boundary);
      assert.equal(writes, retired ? 0 : 1, boundary);
      assert.deepEqual(f.app.calls.filter((row: any) => row[0] === 'commit'), [['commit', 2]]);
      assert.deepEqual(f.app.calls.filter((row: any) => row[0] === 'daemon'), [['daemon', 2]]);
    } finally { release('retry-on-game-ready'); await f.stop(); assert.equal(listeners.size, 0); }
  }
});

await test('actual App shared source raw F5 cannot advance the wake beyond confirmed Native state', async () => {
  const f = createSharedAppFixture();
  try {
    await flushApp(); assert.equal(f.pending.length, 1);
    f.pending.shift().resolve(f.fact('resuming', 1, 1, { resumeReady: false })); await flushApp();
    f.wire('power.resume-ready', { generation: 200 }); await flushApp();
    assert.equal(f.app.state().latest, 2, 'raw future F5 must not replace the current public generation');
    assert.equal(f.app.calls.length, 0, 'unconfirmed hints cannot run recovery work');
    f.emit(f.fact('resuming', 2, 1)); await flushApp();
    assert.equal(f.app.calls.length, 0, 'a legacy snapshot without the F5 receipt must stay idle');
    f.wire('power.resume-ready', { generation: 2 }); f.wire('power.resume-ready', { generation: 2 });
    const [id, retry] = [...f.timers][0] as [number, {fn:()=>void}];
    assert.equal(f.timers.size, 1, 'F5 hints preserve the one shared retry');
    f.timers.delete(id); retry.fn(); await flushApp(); assert.equal(f.pending.length, 1);
    f.pending.shift().resolve(f.fact('resuming', 3, 1, { resumeReady: true })); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', 2]], 'only the public F5 read can start the current transaction');
    f.emit(f.fact('ready', 4, 1, { resumeReady: true })); await flushApp();
    const calls = structuredClone(f.app.calls), reads = f.reads;
    f.wire('power.resume-ready', { generation: 2 }); f.wire('power.resume-ready', { generation: 1 }); await flushApp();
    assert.deepEqual(f.app.calls, calls); assert.equal(f.reads, reads, 'confirmed or old hints must not force another read');
    assert.deepEqual(calls.filter((r: any) => r[0] === 'performance'), [['performance', 2]]);
  } finally { await f.stop(); }
});

await test('actual App shared source private Fan Ready cannot supersede a delayed public Ready', async () => {
  const fanReady: number[] = [];
  const f = createSharedAppFixture({ fanReady: (g: number) => fanReady.push(g) });
  try {
    await flushApp(); assert.equal(f.pending.length, 1);
    f.pending.shift().resolve(f.fact('resuming', 1, 1, { resumeReady: false })); await flushApp();
    const publicGeneration = f.app.state().latest;
    assert.equal(publicGeneration, 2);
    f.wire('fan.resume-ready', { generation: 200, scope: 'fan-only', source: 'kernel-s4-wake' }); await flushApp();
    assert.deepEqual(fanReady, [200], 'the Fan Host must still receive its own Ready');
    assert.equal(f.app.state().latest, publicGeneration, 'a Fan-only generation cannot advance the public owner');
    assert.equal(f.app.calls.length, 0, 'Fan Ready must not run public recovery');
    f.emit(f.fact('resuming', 2, 1, { resumeReady: true })); await flushApp();
    f.emit(f.fact('ready', 3, 1, { resumeReady: true })); await flushApp();
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'commit'), [['commit', publicGeneration]]);
    assert.deepEqual(f.app.calls.filter((r: any) => r[0] === 'performance'), [['performance', publicGeneration]]);
  } finally { await f.stop(); }
});

await test('actual App shared source later notification sequence resumes a retained Fan Ready', async () => {
  const attempts: number[] = [];
  const f = createSharedAppFixture({ fanReady: (g: number) => { attempts.push(g); return attempts.length === 2; } });
  try {
    await flushApp(); assert.equal(f.pending.length, 1);
    f.pending.shift().resolve(f.fact('ready', 1, 10)); await flushApp();
    f.wire('fan.resume-ready', { generation: 200, scope: 'fan-only' }); await flushApp();
    assert.deepEqual(attempts, [200], 'the first Fan observation refused admission');
    f.emit(f.fact('ready', 2, 10)); await flushApp();
    assert.deepEqual(attempts, [200], 'a publication revision alone need not wake the waiting Fan');
    f.emit(f.fact('ready', 2, 11)); await flushApp();
    assert.deepEqual(attempts, [200, 200], 'the current notification sequence must let Fan read again once');
    f.emit(f.fact('ready', 2, 12)); await flushApp();
    assert.deepEqual(attempts, [200, 200], 'the installed Fan recovery owner consumes the retry');
    assert.deepEqual(f.app.calls, [], 'metadata delivery does not replay public work');
  } finally { await f.stop(); }
});

await test('actual App shared source republished Fan Ready resumes its retained request once', async () => {
  const attempts: number[] = [];
  const f = createSharedAppFixture({ fanReady: (g: number) => { attempts.push(g); return false; } });
  const ready = (revision: number) => f.fact('ready', revision, 10, { fanWakeGeneration: 200 });
  try {
    await flushApp(); assert.equal(f.pending.length, 1);
    f.pending.shift().resolve(ready(10)); await flushApp();
    f.wire('fan.resume-ready', { generation: 200, scope: 'fan-only' }); await flushApp();
    assert.deepEqual(attempts, [200]);
    f.emit(ready(10)); f.emit(ready(9)); await flushApp();
    assert.deepEqual(attempts, [200], 'duplicate or stale publication may not consume the retry');
    f.emit(ready(11)); await flushApp();
    assert.deepEqual(attempts, [200, 200], 'the matching Fan Ready republication must reach its retained request');
    f.emit(ready(12)); await flushApp();
    assert.deepEqual(attempts, [200, 200], 'even a failed retry cannot refund the budget');
    assert.deepEqual(f.app.calls, [], 'Fan republication does not replay the public transaction');
    assert.equal(f.reads, 1, 'Fan republication does not request another public read');
  } finally { await f.stop(); }
});

await test('actual App shared source first flight public queue returns slot before a later notification', async () => {
  const queue = createPowerResumeQueue(); let calls = 0, late: Promise<void> | null = null, oldCurrent: (() => boolean) | null = null;
  const first = queue.run(62, async current => {
    oldCurrent = current; calls++;
    void queue.run(62, async () => { calls += 100; });
  }, () => {
    queueMicrotask(() => { late = queue.run(62, async current => { assert(current()); calls++; }); });
    return false;
  });
  await first; await flushApp(); if (late) await late;
  assert.equal(calls, 2, 'a later notification after the completion decision must see a returned queue slot');
  assert.equal(oldCurrent!(), false); queue.invalidate();
});


function createSleepPlanFixture(options: any = {}): any {
  const active='01234567-89ab-cdef-0123-456789abcdef',values=new Map<string,number>(),writes:any[]=[],shellCalls:any[]=[],errors:any[]=[];
  let enabled=true,enabledReads=0,planReads=0,activeReads=0;
  const f=createSharedAppFixture({
    sleepEnabled:async()=>{enabledReads++;return options.enabled?await options.enabled(enabledReads):enabled;},
    sleepEnabledCurrent:()=>enabled,
    activeScheme:async()=>{activeReads++;return active;},
    policy:async(setting:string,ac:boolean)=>options.policy?await options.policy(setting,ac):null,
    plan:async(guid:string,sub:string,setting:string,ac:boolean)=>{planReads++;if(options.plan)await options.plan(planReads);return values.get([guid,sub,setting,ac].join(':'))??-1;},
    shell:async(program:string,args:string[],timeout:number,observation:any,admission:any)=>{
      shellCalls.push({program,args,timeout,admission:admission?{expectedPowerGeneration:admission.expectedPowerGeneration,expectedPowerNotificationSequence:admission.expectedPowerNotificationSequence}:null});assert.equal(program,'powercfg');
      if(args[0]==='/list')return{exitCode:0,stdout:active,stderr:''};
      assert(['/setacvalueindex','/setdcvalueindex'].includes(args[0]),'optimizer cannot activate or mutate another setting class');
      writes.push({args:[...args],admission:admission?{expectedPowerGeneration:admission.expectedPowerGeneration,expectedPowerNotificationSequence:admission.expectedPowerNotificationSequence}:null});
      if(options.write)await options.write(writes.length,{args,admission,values});
      values.set([args[1],args[2],args[3],args[0]==='/setacvalueindex'].join(':'),Number(args[4]));return{exitCode:0,stdout:'',stderr:''};
    },
  });
  return{...f,writes,shellCalls,values,errors,setEnabled(v:boolean){enabled=v;},get enabledReads(){return enabledReads;},get planReads(){return planReads;},
    async cold(extra:any={}){await flushApp();assert.equal(f.pending.length,1);f.pending.shift().resolve(f.fact('ready',1,1,extra));await flushApp();},
    schedule(){f.app.sleepSchedule();},async settle(){await flushApp();f.app.flushTimers();await flushApp();},
  };
}
await test('actual App sleep plan pending notification performs zero optimizer writes', async()=>{
 const f=createSleepPlanFixture();try{await f.cold({notificationPending:true});f.schedule();await f.settle();assert.equal(f.writes.length,0,'pending public notification must deny sleep optimizer writes');}finally{await f.stop();}
});
await test('actual App sleep plan confirmation requires a public notification sequence including zero', async()=>{
 const f=createSleepPlanFixture();
 try{
  await flushApp();
  const legacyReady=f.fact('ready',1,0);
  delete legacyReady.observedNotificationSequence;
  assert.equal(f.pending.length,1);
  f.pending.shift().resolve(legacyReady);
  await f.settle();
  assert.equal(f.source.getSharedPowerLifecycle().observedNotificationSequence,undefined);
  f.schedule();await f.settle();
  assert.equal(f.writes.length,0,'legacy Ready without the notification sequence cannot authorize a plan index write');
  assert.equal(f.pending.length,1,'only the existing public confirmation reads again');
  f.pending.shift().resolve(f.fact('ready',2,0));
  await f.settle();
  assert.equal(f.writes.length,12,'the fresh public Ready can finish with notification sequence zero');
  assert(f.writes.every((write:any)=>write.admission?.expectedPowerGeneration===2&&write.admission?.expectedPowerNotificationSequence===0));
 }finally{await f.stop();}
});
await test('actual App sleep plan held preference cannot pass a later public denial', async()=>{
 let release!:(v:boolean)=>void;const held=new Promise<boolean>(r=>release=r);const f=createSleepPlanFixture({enabled:()=>held});
 try{await f.cold();f.schedule();await flushApp();assert.equal(f.enabledReads,1);f.emit(f.fact('ready',2,2,{hardwareWritesAllowed:false}));release(true);await f.settle();assert.equal(f.writes.length,0,'held preference must not authorize writes after public denial');}finally{release(true);await f.stop();}
});
await test('actual App sleep plan held plan read loses admission before its write', async()=>{
 let release!:()=>void;const held=new Promise<void>(r=>release=r);const f=createSleepPlanFixture({plan:(n:number)=>n===1?held:undefined});
 try{await f.cold();f.schedule();await flushApp();assert.equal(f.planReads,1);f.emit(f.fact('ready',2,2,{hardwareWritesAllowed:false}));release();await f.settle();assert.equal(f.writes.length,0,'plan read result cannot cross public revocation into write');}finally{release();await f.stop();}
});
await test('actual App sleep plan disabled during a plan read submits no new write', async()=>{
 let release!:()=>void;const held=new Promise<void>(r=>release=r);const f=createSleepPlanFixture({plan:(n:number)=>n===1?held:undefined});
 try{await f.cold();f.schedule();await flushApp();assert.equal(f.planReads,1);f.setEnabled(false);release();await f.settle();assert.equal(f.writes.length,0,'saved OFF must stop the batch before its first write');assert.equal(f.app.sleepState().pending,false);assert.equal(f.source.getSharedPowerLifecycleDiagnostics().read.pendingReadyConfirmations,0);}finally{release();await f.stop();}
});
await test('actual App sleep plan disabled during a submitted write does not submit another', async()=>{
 let release!:()=>void;const held=new Promise<void>(r=>release=r);const f=createSleepPlanFixture({write:(n:number)=>n===1?held:undefined});
 try{await f.cold();f.schedule();await flushApp();assert.equal(f.writes.length,1);f.setEnabled(false);release();await f.settle();assert.equal(f.writes.length,1,'saved OFF must keep the remainder of the batch from writing');assert.equal(f.values.size,1);assert.equal(f.app.sleepState().pending,false);assert.equal(f.source.getSharedPowerLifecycleDiagnostics().read.pendingReadyConfirmations,0);}finally{release();await f.stop();}
});
await test('actual App sleep plan public Ready heals the interrupted work without replaying an applied index', async()=>{
 let release!:()=>void;const held=new Promise<void>(r=>release=r);const f=createSleepPlanFixture({plan:(n:number)=>n===3?held:undefined});
 try{await f.cold();f.schedule();await flushApp();assert.equal(f.writes.length,1);f.emit(f.fact('ready',2,2,{hardwareWritesAllowed:false}));release();await f.settle();assert.equal(f.writes.length,1);f.emit(f.fact('ready',3,2));await f.settle();assert.equal(f.writes.length,12,'public Ready completes remaining indices');assert.equal(new Set(f.writes.map((w:any)=>w.args.slice(0,5).join(':'))).size,12,'already applied index is read back and skipped');assert(f.writes.every((w:any)=>w.admission?.expectedPowerGeneration===2),'every write forwards captured Native generation');}finally{release();await f.stop();}
});
await test('actual App sleep plan successor bypasses a held predecessor preference', async()=>{
 let release!:(v:boolean)=>void;const held=new Promise<boolean>(r=>release=r);const f=createSleepPlanFixture({enabled:(n:number)=>n===1?held:true});
 try{await f.cold();f.schedule();await flushApp();f.emit(f.fact('suspending',2,2,{generation:3}));f.emit(f.fact('ready',3,2,{generation:3}));await f.settle();assert.equal(f.writes.length,12,'new public generation must bypass old held preference');release(true);await f.settle();assert.equal(f.writes.length,12,'old owner remains retired');assert(f.writes.every((w:any)=>w.admission?.expectedPowerGeneration===3));}finally{release(true);await f.stop();}
});
await test('actual App sleep plan restored admission cannot revive the old held owner', async()=>{
 let release!:()=>void;const held=new Promise<void>(r=>release=r);const f=createSleepPlanFixture({plan:(n:number)=>n===1?held:undefined});
 try{await f.cold();f.schedule();await flushApp();f.emit(f.fact('ready',2,2,{hardwareWritesAllowed:false}));f.emit(f.fact('ready',3,2));await f.settle();assert.equal(f.writes.length,12);assert(f.writes.every((w:any)=>w.admission?.expectedPowerNotificationSequence===2),'actual App and optimizer forward their captured public ingress ticket');release();await f.settle();assert.equal(f.writes.length,12,'late old owner must not write after same-generation Ready');}finally{release();await f.stop();}
});
await test('actual App sleep plan disabled preference closes pending work and later explicit request can run', async()=>{
 const f=createSleepPlanFixture();try{await f.cold();f.setEnabled(false);f.schedule();await f.settle();assert.equal(f.writes.length,0);assert.equal(f.shellCalls.length,0);f.setEnabled(true);f.schedule();await f.settle();assert.equal(f.writes.length,12);}finally{await f.stop();}
});
await test('actual App sleep plan stop rejects held preference and late public Ready', async()=>{
 let release!:(v:boolean)=>void;const held=new Promise<boolean>(r=>release=r);const f=createSleepPlanFixture({enabled:()=>held});
 try{await f.cold();f.schedule();await flushApp();await f.stop();release(true);await f.settle();f.app.observe(f.fact('ready',4,4));f.schedule();await f.settle();assert.equal(f.writes.length,0,'unmounted optimizer cannot write after held preference');}finally{release(true);await f.stop();}
});
await test('actual App sleep plan failed write returns its queue and a later request recovers', async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error('inert IPC rejection');}});try{await f.cold();f.schedule();await f.settle();assert.equal(f.writes.length,1);assert.equal(f.pending.length,1);f.pending.shift().resolve(f.fact('ready',2,1));await f.settle();f.schedule();await f.settle();assert.equal(f.writes.length,13);assert.equal(f.values.size,12);}finally{await f.stop();}
});
await test('actual App sleep plan repeated events coalesce and preserve the selected scheme', async()=>{
 let release!:(v:boolean)=>void;const held=new Promise<boolean>(r=>release=r);const f=createSleepPlanFixture({enabled:(n:number)=>n===1?held:true});
 try{await f.cold();f.schedule();await flushApp();for(let i=0;i<100;i++)f.schedule();release(true);await f.settle();assert.equal(f.writes.length,12);assert.equal(f.enabledReads,2,'only one coalesced followup');assert.equal(f.shellCalls.filter((r:any)=>r.args[0]==='/list').length,2);assert(f.shellCalls.every((r:any)=>r.args[0]!=='/setactive'));}finally{release(true);await f.stop();}
});
await test('actual App sleep plan metadata sequence change retires a held reader until another request', async()=>{
 let release!:()=>void;const held=new Promise<void>(r=>release=r);const f=createSleepPlanFixture({plan:(n:number)=>n===1?held:undefined});
 try{await f.cold();f.schedule();await flushApp();f.emit(f.fact('ready',2,2));release();await f.settle();assert.equal(f.writes.length,0,'metadata-only ingress sequence cannot authorize old prepared write');f.schedule();await f.settle();assert.equal(f.writes.length,12);}finally{release();await f.stop();}
});

await test('actual App sleep plan same sequence hardware denial still retires its old owner', async()=>{
 let release!:()=>void;const held=new Promise<void>(r=>release=r);const f=createSleepPlanFixture({plan:(n:number)=>n===1?held:undefined});
 try{await f.cold();f.schedule();await flushApp();f.emit(f.fact('ready',2,1,{hardwareWritesAllowed:false}));f.emit(f.fact('ready',3,1));await f.settle();assert.equal(f.writes.length,12,'same sequence admission recovery must start a fresh owner before the held reader returns');release();await f.settle();assert.equal(f.writes.length,12,'old same sequence owner remains retired');}finally{release();await f.stop();}
});


const nativePowerRejected = 'Process cancelled after power or user intent expired or shutdown';
const confirmationCount=(f:any)=>f.source.getSharedPowerLifecycleDiagnostics().read.pendingReadyConfirmations;
const resolvePowerRead=async(f:any,state:any)=>{assert.equal(f.pending.length,1,'exactly one original public read is owned');f.pending.shift().resolve(state);await f.settle();};
await test('actual App sleep plan confirmation Native rejection before new facts retains demand until fresh Ready',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}});
 try{await f.cold();f.schedule();await f.settle();assert.equal(f.writes.length,1);assert.equal(f.pending.length,1,'Native rejection must request one fresh shared confirmation');assert.equal(confirmationCount(f),1);assert.equal(f.app.sleepState().pending,true);
  const business=structuredClone(f.app.calls);await resolvePowerRead(f,f.fact('ready',2,2));assert.equal(f.writes.length,13,'new admitted ingress resumes the rejected index and remaining work');assert.equal(f.values.size,12);assert(f.writes.slice(1).every((w:any)=>w.admission.expectedPowerNotificationSequence===2));assert.deepEqual(f.app.calls,business,'metadata-only confirmation must not replay global wake work');assert.equal(f.app.sleepState().pending,false);assert.equal(confirmationCount(f),0);
 }finally{await f.stop();}
});
await test('actual App sleep plan confirmation metadata-only ingress change heals a held plan read',async()=>{
 let release!:()=>void;const hold=new Promise<void>(r=>release=r);const f=createSleepPlanFixture({plan:(n:number)=>n===1?hold:undefined});
 try{await f.cold();f.schedule();await flushApp();const calls=structuredClone(f.app.calls);f.emit(f.fact('ready',2,2));release();await f.settle();assert.equal(f.writes.length,0);assert.equal(f.pending.length,1,'retired metadata-only reader must request public continuation confirmation');assert.equal(confirmationCount(f),1);await resolvePowerRead(f,f.fact('ready',2,2));assert.equal(f.values.size,12);assert.equal(f.writes.length,12);assert.deepEqual(f.app.calls,calls);}
 finally{release();await f.stop();}
});
await test('actual App sleep plan confirmation a new suspend serial bypasses a held plan read',async()=>{
 let release!:()=>void;const hold=new Promise<void>(r=>release=r);
 const f=createSleepPlanFixture({plan:(n:number)=>n===1?hold:undefined,write:(_n:number,p:any)=>{
   assert.equal(p.admission.expectedPowerSuspendSerial,1,'only the current suspend serial may write');
 }});
 try {
   await f.cold();f.schedule();await flushApp();assert.equal(f.planReads,1);
   f.emit(f.fact('ready',2,1,{suspendSerial:1}));await f.settle();
   assert.equal(f.values.size,12,'current Ready must finish before the held older read returns');
   f.emit(f.fact('ready',1,1));release();await f.settle();
   assert.equal(f.writes.length,12);assert.equal(f.app.sleepState().pending,false);
 } finally {release();await f.stop();}
});
await test('actual App sleep plan confirmation unchanged Ready closes ordinary failure without automatic write retry',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error('ordinary inert IPC failure');}});
 try{await f.cold();f.schedule();await f.settle();assert.equal(f.pending.length,1,'ordinary failure also observes one fresh public state');await resolvePowerRead(f,f.fact('ready',2,1));assert.equal(f.writes.length,1);assert.equal(f.app.sleepState().pending,false);assert.equal(confirmationCount(f),0);assert.equal(f.timers.size,0);f.emit(f.fact('ready',3,1));await f.settle();assert.equal(f.writes.length,1);f.schedule();await f.settle();assert.equal(f.values.size,12);assert.equal(f.writes.length,13);}
 finally{await f.stop();}
});
await test('actual App sleep plan confirmation a coalesced new request waits for the same fresh read',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}});
 try{await f.cold();f.schedule();await f.settle();assert.equal(f.pending.length,1);for(let i=0;i<100;i++)f.schedule();await f.settle();assert.equal(f.writes.length,1,'new demand must not bypass pending read confirmation');assert.equal(confirmationCount(f),1);assert.equal(f.pending.length,1);await resolvePowerRead(f,f.fact('ready',2,1));assert.equal(f.writes.length,13);assert.equal(f.values.size,12);assert.equal(confirmationCount(f),0);}
 finally{await f.stop();}
});
await test('actual App sleep plan confirmation unknown and failed reads preserve original backoff without private polling',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}});
 try{await f.cold();f.schedule();await f.settle();assert.equal(f.pending.length,1);await resolvePowerRead(f,{known:false,publicationRevision:2,observedNotificationSequence:2});assert.equal(f.writes.length,1);assert.equal(confirmationCount(f),1);assert.deepEqual([...f.timers.values()].map((t:any)=>t.ms),[1000]);
  for(let i=0;i<100;i++)f.source.requestSharedPowerLifecycleReconcile('inert-repeat');await f.settle();assert.deepEqual([...f.timers.values()].map((t:any)=>t.ms),[1000]);const first=[...f.timers.entries()][0];f.timers.delete(first[0]);first[1].fn();await f.settle();assert.equal(f.pending.length,1);f.pending.shift().reject(Error('inert shared read failure'));await f.settle();assert.deepEqual([...f.timers.values()].map((t:any)=>t.ms),[2000]);assert.equal(confirmationCount(f),1);const second=[...f.timers.entries()][0];f.timers.delete(second[0]);second[1].fn();await f.settle();await resolvePowerRead(f,f.fact('ready',3,2));assert.equal(f.writes.length,13);assert.equal(f.values.size,12);assert.equal(f.timers.size,0);assert.equal(confirmationCount(f),0);}
 finally{await f.stop();}
});
await test('actual App sleep plan confirmation newer admitted generation bypasses a held confirmation read',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}});
 try{await f.cold();f.schedule();await f.settle();assert.equal(f.pending.length,1);f.emit(f.fact('ready',2,2,{generation:3}));await f.settle();assert.equal(f.writes.length,13,'new public generation starts without waiting for old read');assert(f.writes.slice(1).every((w:any)=>w.admission.expectedPowerGeneration===3));assert.equal(confirmationCount(f),0);const before=f.writes.length;await resolvePowerRead(f,f.fact('ready',1,1));assert.equal(f.writes.length,before);}
 finally{await f.stop();}
});
await test('actual App sleep plan confirmation stop releases confirmation and late response cannot restart work',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}});
 try{await f.cold();f.schedule();await f.settle();assert.equal(confirmationCount(f),1);await f.stop();assert.equal(f.app.sleepState().pending,false);assert.equal(f.app.sleepState().confirming,false,'stop must release its local confirmation owner');assert.equal(confirmationCount(f),0);assert.equal(f.writes.length,1);f.schedule();await f.settle();assert.equal(f.writes.length,1);}
 finally{await f.stop();}
});
await test('actual App sleep plan confirmation unknown completion rereads applied index before continuing',async()=>{
 const f=createSleepPlanFixture({write:(n:number,p:any)=>{if(n===1){p.values.set([p.args[1],p.args[2],p.args[3],p.args[0]==='/setacvalueindex'].join(':'),Number(p.args[4]));throw Error(nativePowerRejected);}}});
 try{await f.cold();f.schedule();await f.settle();assert.equal(f.writes.length,1);assert.equal(f.values.size,1);assert.equal(f.pending.length,1);await resolvePowerRead(f,f.fact('ready',2,2));assert.equal(f.writes.length,12,'already applied but uncertain first index must not be replayed');assert.equal(f.values.size,12);assert.equal(new Set(f.writes.map((w:any)=>w.args.slice(0,5).join(':'))).size,12);}
 finally{await f.stop();}
});
await test('actual App sleep plan confirmation delayed pre-error public read cannot satisfy fresh request',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}});
 try{await f.cold();f.source.requestSharedPowerLifecycleReconcile('pre-error');await f.settle();assert.equal(f.pending.length,1);f.schedule();await f.settle();assert.equal(confirmationCount(f),1);await resolvePowerRead(f,f.fact('ready',1,1));assert.equal(f.writes.length,1,'read started before rejection cannot confirm retry');assert.equal(confirmationCount(f),1);assert.equal(f.pending.length,1);await resolvePowerRead(f,f.fact('ready',2,2));assert.equal(f.writes.length,13);assert.equal(confirmationCount(f),0);}
 finally{await f.stop();}
});
await test('actual App sleep plan confirmation disabled preference at fresh recovery returns demand without writes',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}});
 try{await f.cold();f.schedule();await f.settle();assert.equal(confirmationCount(f),1);f.setEnabled(false);await resolvePowerRead(f,f.fact('ready',2,2));assert.equal(f.writes.length,1);assert.equal(f.app.sleepState().pending,false);assert.equal(confirmationCount(f),0);}
 finally{await f.stop();}
});


await test('actual App sleep plan confirmation same scope admission restoration bypasses its held confirmation after observed denial',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}});
 try{await f.cold();f.schedule();await f.settle();assert.equal(f.pending.length,1);f.emit(f.fact('ready',2,1,{hardwareWritesAllowed:false}));f.emit(f.fact('ready',3,1));await f.settle();assert.equal(f.writes.length,13,'same scope observed denial and restoration must resume retained demand without waiting for old read');assert.equal(f.values.size,12);assert.equal(confirmationCount(f),0);await resolvePowerRead(f,f.fact('ready',1,1));assert.equal(f.writes.length,13);}
 finally{await f.stop();}
});
await test('actual App sleep plan confirmation same scope unknown read recovery retains its unfinished request',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}});
 try{await f.cold();f.schedule();await f.settle();await resolvePowerRead(f,{known:false,publicationRevision:2,observedNotificationSequence:1});assert.equal(f.writes.length,1);assert.equal(confirmationCount(f),1);const retry=[...f.timers.entries()][0];assert.equal(retry[1].ms,1000);f.timers.delete(retry[0]);retry[1].fn();await f.settle();await resolvePowerRead(f,f.fact('ready',3,1));assert.equal(f.writes.length,13,'Ready following an explicitly observed denial may not clear unfinished same-scope work');assert.equal(f.values.size,12);assert.equal(confirmationCount(f),0);}
 finally{await f.stop();}
});

await test('actual App sleep plan confirmation stop releases only its waiter while a public peer remains active',async()=>{
 const f=createSleepPlanFixture({write:(n:number)=>{if(n===1)throw Error(nativePowerRejected);}}),peer:any[]=[];const off=f.source.onSharedPowerLifecycle((s:any)=>peer.push(s));
 try{await f.cold();f.schedule();await f.settle();assert.equal(confirmationCount(f),1);await f.stop(1);const stopped=f.source.getSharedPowerLifecycleDiagnostics();assert.equal(stopped.active,true);assert.equal(stopped.subscriberCount,1);assert.equal(stopped.read.pendingReadyConfirmations,0);assert.equal(f.app.sleepState().confirming,false);assert.equal(f.writes.length,1);f.source.requestSharedPowerLifecycleReconcile('remaining-inert-peer');await f.settle();await resolvePowerRead(f,f.fact('ready',1000,1000));assert.equal(f.writes.length,1);assert.equal(f.source.getSharedPowerLifecycleDiagnostics().active,true);assert(peer.length>0);assert.equal(f.timers.size,0);}
 finally{off();await f.stop();}
});

assert(executedCases > 0, 'selected case must exist');
console.log('power resume selftest: ' + executedCases + ' passed');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
