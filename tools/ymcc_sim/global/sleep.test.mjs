import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, writeFile, readFile, unlink, rmdir, stat } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { getSleepAdapters, replaySleepLog, SLEEP_SCENARIOS } from './sleep.mjs';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const require = createRequire(path.join(repoRoot, 'package.json'));
const fact = (event, details = {}, time = '2026-10-18 12:00:00.000') => ({ time, event, details });
const pbt = (code, generation = 1, time = '2026-10-18 12:00:00.000') => fact('power-broadcast', { code, generation }, time);
const encode = rows => rows.map(row => JSON.stringify(row)).join('\n') + '\n';

async function replay(t, rows, options = {}) {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'ymcc-sleep-replay-'));
  const file = path.join(dir, 'native.jsonl');
  // Remove only this test's exact files. No recursive or name-based cleanup.
  t.after(async () => { await unlink(file); await rmdir(dir); });
  await writeFile(file, typeof rows === 'string' ? rows : encode(rows));
  return replaySleepLog(file, options);
}
const scenario = (result, id) => result.matrix.find(item => item.id === id);

let product;
async function resumeProduct() {
  if (!product) {
    // Transform the actual shipped pure transaction in memory. No substitute
    // model, installed settings, IPC host, process control, or hardware is used.
    product = (async () => {
      const source = await readFile(path.join(repoRoot, 'src/bridge/powerResume.ts'), 'utf8');
      const { code } = await require('esbuild').transform(source, { loader: 'ts', format: 'esm', target: 'node18' });
      return import(`data:text/javascript;base64,${Buffer.from(code).toString('base64')}`);
    })();
  }
  return product;
}

test('catalog retains actual source files, original tests and guarded 20-form capability', async () => {
  const adapters = getSleepAdapters(repoRoot);
  assert.equal(new Set(adapters.map(item => item.id)).size, adapters.length);
  for (const item of adapters) {
    assert.equal(item.domain, 'sleep');
    assert(item.profiles.includes('sleep'));
    assert(['node', 'typescript', 'powershell'].includes(item.kind));
    assert(['behavioral-mock', 'source-contract', 'host-mock'].includes(item.scope));
    assert(item.evidenceInputs.some(file => /^(src|native|FanLab)\//.test(file)), 'each adapter retains actual production source');
    for (const file of [item.path, ...item.evidenceInputs]) {
      assert(!path.isAbsolute(file) && !file.split(/[\\/]/).includes('..'));
      assert((await stat(path.join(repoRoot, file))).isFile(), `required evidence is not a file: ${file}`);
    }
  }
  assert.equal(adapters.find(item => item.id === 'sleep.task-policy-source').scope, 'source-contract');
  const forms = adapters.find(item => item.id === 'sleep.host-forms');
  for (const [id, mode] of [['sleep.engine-memory','memory'], ['sleep.engine-http-all','all']]) {
    const software = adapters.find(item => item.id === id);
    assert(software, id);
    assert.equal(software.requiresExclusiveHost, false);
    assert.equal(software.requiresPayloadRoot, true);
    assert.equal(software.scope, 'host-mock');
    assert.deepEqual(software.args, ['-Mode', mode, '-EvidenceDir', '{outDir}/engine']);
    assert(software.evidenceInputs.includes('tools/ymcc_sim/global/consumer/sleep-engine/Program.cs'));
  }
  assert.equal(forms.scope, 'host-mock');
  assert(!forms.evidenceInputs.includes('native/main.cpp'));
  assert.deepEqual(forms.referenceInputs, ['native/main.cpp']);
  assert(forms.evidenceInputs.includes('FanLab/real-host/Program.cs'));
  assert.equal(forms.kind, 'powershell');
  assert.equal(forms.requiresExclusiveHost, true);
  assert.equal(forms.requiresPayloadRoot, true);
  assert.deepEqual(forms.profiles, ['sleep', 'full']);
  assert(adapters.find(item => item.id === 'sleep.type-classifier').profiles.includes('desktop'));
  assert.deepEqual(forms.resultFiles, ['sleep-host-result.json']);
  assert.deepEqual(adapters.filter(item => item.requiresExclusiveHost).map(item => item.args.at(-1)), ['18874', '18875', '18876']);
  assert(adapters.filter(item => item.scope === 'host-mock').every(item => !item.args.some(arg => /live|Attach|StartApp|real-backend|Bypass/.test(arg))));
  const library = JSON.parse(await readFile(path.join(repoRoot, 'tools/ymcc_sim/domains/sleep/sleep_forms.json'), 'utf8'));
  assert.equal(library.forms.length, 20);
});

test('empty and unrelated logs are NOT_COVERED, never pass', async t => {
  const empty = await replay(t, '');
  assert.equal(empty.status, 'NOT_COVERED');
  assert.deepEqual(empty.matrix.map(item => item.id), SLEEP_SCENARIOS);
  assert(empty.matrix.every(item => item.status === 'NOT_COVERED'));
  const unrelated = await replay(t, [{ event: 'unknown', secret: 'DO_NOT_EXPORT' }]);
  assert.equal(unrelated.status, 'NOT_COVERED');
  assert.equal(unrelated.counts.ignored, 1);
  assert(!JSON.stringify(unrelated).includes('DO_NOT_EXPORT'));
});

test('native S0 joined notifications and rapid wake reuse producer format without physical claims', async t => {
  const result = await replay(t, [
    pbt(4), fact('kernel-power-506', { targetState: 0 }),
    fact('s0-intent-joined-existing-suspend', { generation: 2 }),
    pbt(18, 2, '2026-10-18 12:00:00.100'),
    pbt(7, 2, '2026-10-18 12:00:00.120'),
  ]);
  assert.equal(result.status, 'INCOMPLETE');
  assert.equal(scenario(result, 's0').status, 'OBSERVED');
  assert.equal(scenario(result, 'duplicate').status, 'OBSERVED');
  assert.equal(scenario(result, 'rapid-wake').status, 'OBSERVED');
  assert.equal(result.counts.pairedCycles, 1);
  assert.equal(result.counts.unpairedResumeEdges, 0);
  assert(Object.values(result.claims).every(value => value === false));
  assert(!['PASS', 'OK'].includes(result.status));
});

test('PBT and a requested S3 mode do not prove an actual S3 or S4 transition', async t => {
  const result = await replay(t, [
    fact('power-broadcast', { code: 4, generation: 1, taskMode: 's3' }),
    pbt(18, 2, '2026-10-18 12:00:12.000'),
  ]);
  assert.equal(scenario(result, 's3').status, 'NOT_COVERED');
  assert.equal(scenario(result, 's4').status, 'NOT_COVERED');
  assert.equal(scenario(result, 's0').status, 'NOT_COVERED');
  assert.equal(result.status, 'INCOMPLETE');
});

test('native TargetState=5 hibernate witness is distinct from an S4 request', async t => {
  const result = await replay(t, [fact('kernel-power-107', { eventId: 107, targetState: 5 })], { requiredScenarios: ['s4'] });
  assert.equal(result.status, 'OBSERVED');
  assert.equal(scenario(result, 's4').status, 'OBSERVED');
  assert.equal(scenario(result, 's3').status, 'NOT_COVERED');
  assert.equal(result.claims.fullApplicationValidated, false);
  const noProof = await replay(t, [fact('kernel-power-107', { targetState: 4 })]);
  assert.equal(scenario(noProof, 's4').status, 'NOT_COVERED');
});

test('producer TargetState=-1 sentinel is unknown, not malformed or hibernate evidence', async t => {
  const result = await replay(t, [
    fact('kernel-power', { eventId: 506, targetState: -1 }),
    fact('kernel-power', { eventId: 507, targetState: -1 }, '2026-10-18 12:00:01.000'),
    fact('kernel-power-107', { eventId: 107, targetState: -1 }),
  ]);
  assert.equal(result.counts.invalidFields, 0);
  assert.equal(result.counts.malformed, 0);
  assert.equal(result.counts.unknownTargetStates, 3);
  assert.equal(result.status, 'INCOMPLETE');
  assert.equal(scenario(result, 's0').status, 'OBSERVED');
  assert.equal(scenario(result, 's3').status, 'NOT_COVERED');
  assert.equal(scenario(result, 's4').status, 'NOT_COVERED');
  for (const value of [-2, 7, -0.5, '-1']) {
    const invalid = await replay(t, [fact('kernel-power', { eventId: 506, targetState: value })]);
    assert.equal(invalid.status, 'FAIL');
    assert.equal(invalid.counts.invalidFields, 1);
  }
});

test('screen-off uses actual flattened lifecycle source and state, not monitor guard toggles', async t => {
  const result = await replay(t, [
    { time: '2026-10-18 12:00:00.000', event: 'display-idle-state', source: 'GUID_SESSION_DISPLAY_STATUS', state: 0, pid: 999, powerGeneration: 3 },
    { time: '2026-10-18 12:00:01.000', event: 'display-idle-state', source: 'notification-registration' },
    fact('monitor-disabled'),
  ], { requiredScenarios: ['screen-off'] });
  assert.equal(result.status, 'OBSERVED');
  assert.equal(scenario(result, 'screen-off').observations, 1);
  assert.equal(scenario(result, 's0').status, 'NOT_COVERED');
  assert.equal(result.counts.ignored, 2);
});

test('query cancellation closes an observed sequence without inventing a completed sleep', async t => {
  const result = await replay(t, [pbt(4), pbt(2, 1, '2026-10-18 12:00:00.100')], { requiredScenarios: ['cancel'] });
  assert.equal(result.status, 'OBSERVED');
  assert.equal(result.counts.pairedCycles, 0);
  assert.equal(result.counts.incompleteCycles, 0);
});

test('duplicate and out-of-order records are observed without sorting into a false rapid wake', async t => {
  const first = pbt(4, 8, '2026-10-18 12:00:00.100');
  const result = await replay(t, [first, first, pbt(18, 7, '2026-10-18 12:00:00.050')]);
  assert.equal(result.counts.duplicateRecords, 1);
  assert.equal(result.counts.outOfOrderTimestamps, 1);
  assert.equal(result.counts.generationRegressions, 1);
  assert.equal(scenario(result, 'out-of-order').status, 'OBSERVED');
  assert.equal(scenario(result, 'rapid-wake').status, 'NOT_COVERED');
});

test('rapid wake threshold is inclusive, configurable and cannot use missing clocks', async t => {
  const at = await replay(t, [pbt(4), pbt(18, 2, '2026-10-18 12:00:05.000')]);
  const after = await replay(t, [pbt(4), pbt(18, 2, '2026-10-18 12:00:05.001')]);
  assert.equal(at.counts.rapidCycles, 1);
  assert.equal(after.counts.rapidCycles, 0);
  const noTime = await replay(t, [pbt(4, 1, 'invalid'), pbt(18, 2, 'invalid')]);
  assert.equal(noTime.counts.missingTimestamps, 2);
  assert.equal(noTime.counts.rapidCycles, 0);
  const invalidDate = await replay(t, [pbt(4, 1, '2026-02-30 12:00:00.000'), pbt(18, 2, '2026-02-30 12:00:00.001')]);
  assert.equal(invalidDate.counts.rapidCycles, 0);
});

test('generation resets and missing generations cannot manufacture a rapid cycle', async t => {
  for (const generations of [[9, 0], [1, 9], [null, null]]) {
    const result = await replay(t, [pbt(4, generations[0]), pbt(18, generations[1], '2026-10-18 12:00:00.100')]);
    assert.equal(result.counts.rapidCycles, 0);
    assert.equal(result.counts.pairedCycles, 0);
    assert.equal(result.status, 'INCOMPLETE');
  }
});

test('unrelated later resume notifications are not joined into an unlimited wake burst', async t => {
  const result = await replay(t, [
    pbt(18, 1), pbt(7, 1, '2026-10-18 12:00:00.100'),
    pbt(18, 1, '2026-10-18 13:00:00.000'),
    pbt(18, 2, '2026-10-18 13:00:00.100'),
  ]);
  assert.equal(result.counts.unpairedResumeEdges, 3);
});

test('incomplete EOF is explicitly incomplete, never asserted to be a crash or recovery', async t => {
  const partial = await replay(t, [pbt(4)]);
  assert.equal(partial.status, 'INCOMPLETE');
  assert.equal(partial.counts.incompleteCycles, 1);
  assert.equal(scenario(partial, 'interruption').status, 'INCOMPLETE');
  assert.equal(scenario(partial, 'interruption').observations, 0);
  const orphan = await replay(t, [fact('sleep-orphan-recovery-start')], { requiredScenarios: ['interruption'] });
  assert.equal(orphan.status, 'OBSERVED');
  assert.equal(orphan.claims.productLogicExecuted, false);
});

test('malformed input and wrong field types cannot become accepted log evidence', async t => {
  for (const rows of ['{incomplete', [pbt('4')], [pbt(4, '1')], [fact('kernel-power', { eventId: '506' })],
    [{ event: 'power-broadcast', details: [] }], [fact('kernel-power-107', { targetState: '5' })]]) {
    const result = await replay(t, rows);
    assert.equal(result.status, 'FAIL');
    assert(result.counts.malformed + result.counts.invalidFields > 0);
  }
});

test('raw secrets, arbitrary keys, diagnostics and prototype names never reach output', async t => {
  const marker = 'PRIVATE_TOKEN_SETTINGS_SESSION_DO_NOT_COPY';
  const raw = pbt(4);
  raw[marker] = marker;
  raw.details.session = marker;
  raw.details.token = marker;
  raw.details.settings = { marker };
  raw.details.reason = marker;
  raw.details.pid = 123456789;
  const result = await replay(t, [raw, { event: '__proto__', time: marker, details: { token: marker } },
    { event: 'display-idle-state', source: marker, state: marker },
    pbt(18, 2, '2026-10-18 12:00:00.100')]);
  const encoded = JSON.stringify(result);
  assert(!encoded.includes(marker));
  assert(!encoded.includes('123456789'));
  assert(!encoded.includes('2026-10-18'));
  assert.deepEqual(Object.keys(result).sort(), ['schemaVersion', 'domain', 'mode', 'scope', 'status', 'reason', 'inputFormat', 'claims', 'limits', 'counts', 'eventCounts', 'matrix', 'boundaries'].sort());
  assert.equal(result.counts.ignored, 2);
});

test('read, byte, line and per-line bounds produce fixed failure codes', async t => {
  const byte = await replay(t, [pbt(4)], { maxBytes: 1 });
  assert.equal(byte.reason, 'INPUT_BYTE_LIMIT_EXCEEDED');
  const line = await replay(t, [pbt(4), pbt(18)], { maxLines: 1 });
  assert.equal(line.reason, 'INPUT_LINE_LIMIT_EXCEEDED');
  const width = await replay(t, [pbt(4)], { maxLineBytes: 8 });
  assert.equal(width.reason, 'INPUT_LINE_BYTE_LIMIT_EXCEEDED');
  for (const result of [byte, line, width]) assert.equal(result.status, 'FAIL');
  const missing = await replaySleepLog(path.join(os.tmpdir(), 'private-never-export-this', 'missing.log'));
  assert.equal(missing.status, 'FAIL');
  assert(!JSON.stringify(missing).includes('private-never-export-this'));
  const directory = await replaySleepLog(repoRoot);
  assert.equal(directory.reason, 'INPUT_NOT_REGULAR_FILE');
});

test('BOM/CRLF supported; unrecognized producers and unsafe option values are never inferred', async t => {
  const result = await replay(t, '\uFEFF' + encode([pbt(4), pbt(18, 2, '2026-10-18 12:00:00.100')]).replaceAll('\n', '\r\n'));
  assert.equal(result.counts.pairedCycles, 1);
  for (const options of [null, [], { maxBytes: Infinity }, { maxLines: -1 }, { maxLineBytes: '3' },
    { rapidWakeMs: NaN }, { requiredScenarios: [] }, { requiredScenarios: ['unknown'] }, { raw: true }]) {
    await assert.rejects(replaySleepLog('unused.log', options), { message: 'SLEEP_REPLAY_OPTIONS_INVALID' });
  }
});

test('production commits before daemon work and retries a reasoned rejection within its bound', async () => {
  const { runPowerResumeTransaction } = await resumeProduct();
  const calls = [];
  const result = await runPowerResumeTransaction(5, true, {
    completeResume: async () => { calls.push('commit'); return { ok: false, reason: 'not_ready' }; },
    resumeDaemon: async () => { calls.push('daemon'); return true; },
    commitAttempts: 2, sleep: async () => {},
  });
  assert.equal(result.committed, false);
  assert.equal(result.commitAttempts, 2);
  assert.deepEqual(calls, ['commit', 'commit']);
});

test('production generation change during retry stops further commit and daemon work', async () => {
  const { runPowerResumeTransaction } = await resumeProduct();
  let current = true;
  const calls = [];
  const result = await runPowerResumeTransaction(6, true, {
    isGenerationCurrent: () => current,
    completeResume: async () => { calls.push('commit'); current = false; return { ok: false, reason: 'not_ready' }; },
    resumeDaemon: async () => { calls.push('daemon'); return true; },
    sleep: async () => {},
  });
  assert.equal(result.committed, false);
  assert.equal(result.reason, 'superseded');
  assert.deepEqual(calls, ['commit']);
});

test('production daemon rejection never re-closes a committed native gate', async () => {
  const { runPowerResumeTransaction } = await resumeProduct();
  const calls = [];
  const result = await runPowerResumeTransaction(7, true, {
    completeResume: async () => { calls.push('commit'); return { ok: true }; },
    resumeDaemon: async () => { calls.push('daemon'); throw new Error('mock transport rejection'); },
    daemonAttempts: 2, sleep: async () => {},
  });
  assert.equal(result.committed, true);
  assert.equal(result.daemonReady, false);
  assert.deepEqual(calls, ['commit', 'daemon', 'daemon']);
});

// Normal product regression, never todo/skip/expected-success. It exposed the
// reasonless rejection defect before the owner added commitAccepted on 2026-10-18.
// Keep the original expectation so a reintroduced defect turns the run red.
test('production must reject exhausted {ok:false} commits even when optional reason is absent', async () => {
  const { runPowerResumeTransaction } = await resumeProduct();
  let daemonCalls = 0;
  const result = await runPowerResumeTransaction(8, true, {
    completeResume: async () => ({ ok: false }),
    resumeDaemon: async () => { daemonCalls++; return true; },
    commitAttempts: 2, sleep: async () => {},
  });
  assert.deepEqual({ committed: result.committed, daemonCalls }, { committed: false, daemonCalls: 0 },
    'A rejected native commit must not be reported as committed or start daemon recovery');
});
