import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { createRevalidationPlan, checkBatchInputs, selectContributingReceipts } from './index.mjs';
import { main, parseArgs } from './cli.mjs';
import { RECEIPT_AUDIT_LIMITS } from '../receipt-audit.mjs';

const sha = value => createHash('sha256').update(value).digest('hex');
function fixture(t, ids = ['fan.one', 'fan.two', 'lighting.three']) {
  const base = fs.realpathSync(os.tmpdir());
  const root = fs.mkdtempSync(path.join(base, 'ymcc-revalidation-plan-'));
  t.after(() => {
    assert.equal(path.dirname(fs.realpathSync(root)), base);
    assert(path.basename(root).startsWith('ymcc-revalidation-plan-'));
    fs.rmSync(root, { recursive: true, force: true });
  });
  const repoRoot = path.join(root, 'repo');
  fs.mkdirSync(repoRoot);
  const adapters = ids.map(id => ({
    id, domain: id.split('.')[0], kind: 'node', path: `${id}.mjs`, args: [], timeoutMs: 1000,
    profiles: ['full', 'desktop'], scope: 'behavioral-mock', evidenceInputs: ['product.cs'], resultFiles: [],
  }));
  for (const adapter of adapters) fs.writeFileSync(path.join(repoRoot, adapter.path), "throw Error('PRODUCT_CODE_MUST_NOT_RUN');");
  fs.writeFileSync(path.join(repoRoot, 'product.cs'), 'product-v1');
  const passed = id => {
    const adapter = adapters.find(row => row.id === id);
    return { id, status: 'PASS', exitCode: 0, source: adapter.path, scope: adapter.scope,
      sourceInputs: [adapter.path, ...adapter.evidenceInputs].map(file => ({ path: file, sha256: sha(fs.readFileSync(path.join(repoRoot, file))) })),
      structuredEvidence: [],
    };
  };
  function write(name, rows = adapters.map(row => passed(row.id)), extra = {}) {
    const dir = path.join(root, name); fs.mkdirSync(dir, { recursive: true });
    const file = path.join(dir, 'result.json');
    fs.writeFileSync(file, JSON.stringify({ schemaVersion: 1, tool: 'ymcc_sim-global', mode: 'run', runId: name, results: rows, ...extra }));
    return file;
  }
  return { root, repoRoot, adapters, passed, write,
    plan(files, options = {}) { return createRevalidationPlan(files, { repoRoot, adapters, ...options }); },
  };
}

test('current live audit reuses selected cases without executing product or declaring acceptance', t => {
  const f = fixture(t), file = f.write('initial');
  const bytes = fs.readFileSync(file);
  const { plan, audit } = f.plan([file]);
  assert.equal(plan.status, 'PLAN_ONLY'); assert.equal(plan.executedTests, 0);
  assert.equal(plan.summary.reusableAtAudit, 3); assert.equal(plan.summary.needsRerun, 0);
  assert.equal(plan.fullProductAcceptance, false); assert.equal(plan.physicalHardwareAcceptance, false);
  assert.equal(audit.executedTests, 0); assert.deepEqual(fs.readFileSync(file), bytes);
});
test('changes are re-audited from original receipts rather than a historical empty dirty list', t => {
  const f = fixture(t), file = f.write('initial', undefined, { needsRerun: [] });
  assert.equal(f.plan([file]).plan.summary.needsRerun, 0);
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'product-v2');
  const { plan } = f.plan([file]);
  assert.equal(plan.summary.needsRerun, 3); assert.equal(plan.summary.reusableAtAudit, 0);
  assert.equal(plan.batches.length, 1); assert.equal(plan.changedInputHotspots[0].testIds.length, 3);
  assert.equal(plan.changedInputHotspots[0].path, 'product.cs');
});
test('explicit selection excludes unrelated cases and rejects unknown IDs', t => {
  const f = fixture(t), file = f.write('initial');
  const { plan } = f.plan([file], { tests: ['fan.one'] });
  assert.equal(plan.summary.selected, 1); assert.equal(plan.outsideSelection.length, 2);
  assert.throws(() => f.plan([file], { tests: ['fan.unknown'] }), /UNKNOWN_SELECTION/);
  assert.throws(() => f.plan([file], { domains: ['unknown'] }), /UNKNOWN_SELECTION/);
});
test('latest failures and blocked outcomes replace older pass receipts and remain in batches', t => {
  const f = fixture(t), old = f.write('old');
  const latest = f.write('latest', [{ id: 'fan.one', status: 'FAIL' }, { id: 'fan.two', status: 'BLOCKED' }]);
  const { plan } = f.plan([old, latest]);
  assert.equal(plan.summary.reusableAtAudit, 1);
  assert.deepEqual(plan.needsRerun.map(row => [row.id, row.status]), [['fan.one', 'FAIL'], ['fan.two', 'BLOCKED']]);
  assert.equal(plan.batches[0].reasonStatus, 'FAIL');
});
test('newly registered cases without receipts cannot disappear', t => {
  const f = fixture(t), file = f.write('partial', [f.passed('fan.one')]);
  const { plan } = f.plan([file]);
  assert.equal(plan.summary.reusableAtAudit, 1); assert.equal(plan.summary.needsRerun, 2);
  assert(plan.needsRerun.every(row => row.status === 'NOT_RUN'));
  assert.deepEqual(plan.batches.flatMap(row => row.testIds).sort(), ['fan.two', 'lighting.three']);
});
test('registration changes require execution of the new source closure', t => {
  const f = fixture(t), file = f.write('initial');
  fs.writeFileSync(path.join(f.repoRoot, 'new.cs'), 'new dependency');
  f.adapters[0].evidenceInputs.push('new.cs');
  const { plan } = f.plan([file]);
  assert.equal(plan.needsRerun[0].id, 'fan.one'); assert.equal(plan.needsRerun[0].status, 'STALE_REGISTRATION');
  assert(plan.needsRerun[0].requiredSourceInputs.includes('new.cs'));
});
test('assignments split batches but never erase required tests or claim an active worker', t => {
  const f = fixture(t), file = f.write('initial');
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'product-v2');
  const { plan } = f.plan([file], { assignments: [{ owner: 'peer-thread', tests: ['fan.one', 'fan.two'] }] });
  assert.equal(plan.summary.needsRerun, 3); assert.equal(plan.summary.assignedNeedsRerun, 2);
  assert.equal(plan.summary.unassignedNeedsRerun, 1); assert.equal(plan.batches.length, 2);
  assert(plan.batches.find(row => row.owner)?.assignmentMeaning.includes('NOT_LIVE_PROCESS_PROOF'));
  assert.throws(() => f.plan([file], { assignments: [{ owner: 'x', tests: ['fan.unknown'] }] }), /NOT_REGISTERED/);
  assert.throws(() => f.plan([file], { assignments: [{ owner: 'x', tests: ['fan.one'] }, { owner: 'y', tests: ['fan.one'] }] }), /DUPLICATE_TEST_ASSIGNMENT/);
});
test('batch size partitions complete obligations without duplicates', t => {
  const f = fixture(t), file = f.write('initial');
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'product-v2');
  const { plan } = f.plan([file], { maxBatchSize: 2 });
  assert.deepEqual(plan.batches.map(row => row.testIds.length), [2, 1]);
  assert.equal(new Set(plan.batches.flatMap(row => row.testIds)).size, 3);
  for (const maxBatchSize of [0, 33, 1.5, '2']) assert.throws(() => f.plan([file], { maxBatchSize }), /INPUT_INVALID/);
});
test('a shared source alone does not merge cases with different additional changes', t => {
  const f = fixture(t), file = f.write('initial');
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'product-v2');
  fs.writeFileSync(path.join(f.repoRoot, 'fan.one.mjs'), 'different entry');
  const { plan } = f.plan([file]);
  assert.equal(plan.batches.length, 2);
  assert.deepEqual(plan.batches.find(row => row.testIds.includes('fan.one')).testIds, ['fan.one']);
});
test('explicit candidate identities separate batches even with the same source drift', t => {
  const f = fixture(t);
  const candidates = ['candidate-a.dll', 'candidate-b.dll'].map(name => {
    const file = path.join(f.root, name); fs.writeFileSync(file, name); return { path: file, sha256: sha(name) };
  });
  const file = f.write('initial', [
    { ...f.passed('fan.one'), candidateArtifacts: [candidates[0]] },
    { ...f.passed('fan.two'), candidateArtifacts: [candidates[1]] },
  ]);
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'product-v2');
  const { plan } = f.plan([file], { domains: ['fan'] });
  assert.equal(plan.batches.length, 2);
  assert.notEqual(plan.batches[0].executionBinding.explicitArtifacts[0].path, plan.batches[1].executionBinding.explicitArtifacts[0].path);
});
test('replaced candidate bytes expire a receipt even without product source changes', t => {
  const f = fixture(t), candidate = path.join(f.root, 'candidate.dll'); fs.writeFileSync(candidate, 'approved');
  const file = f.write('candidate', [{ ...f.passed('fan.one'), candidateArtifacts: [{ path: candidate, sha256: sha('approved') }] }]);
  assert.equal(f.plan([file], { tests: ['fan.one'] }).plan.summary.reusableAtAudit, 1);
  fs.writeFileSync(candidate, 'changed');
  const { plan } = f.plan([file], { tests: ['fan.one'] });
  assert.equal(plan.summary.reusableAtAudit, 0);
  assert.equal(plan.needsRerun[0].reasons.changes[0].role, 'explicit-candidate-artifact');
});
test('different explicit Host payload roots never share an execution batch', t => {
  const f = fixture(t), files = [];
  for (const [index, id] of ['fan.one', 'fan.two'].entries()) {
    f.adapters.find(row => row.id === id).scope = 'host-mock';
    const payload = path.join(f.root, `host-${index}`); fs.mkdirSync(payload);
    const binaries = ['YeManFanHost.exe', 'YeManFanHost.dll'].map(name => {
      fs.writeFileSync(path.join(payload, name), 'approved'); return { name, sha256: sha('approved') };
    });
    files.push(f.write(`host-run-${index}`, [f.passed(id)], { hostPayload: { root: payload, binaries } }));
  }
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'product-v2');
  const { plan } = f.plan(files, { domains: ['fan'] });
  assert.equal(plan.batches.length, 2);
  assert.notEqual(plan.batches[0].executionBinding.host.root, plan.batches[1].executionBinding.host.root);
});
test('tampered structured evidence remains a required revalidation', t => {
  const f = fixture(t), dir = path.join(f.root, 'proof-run', 'cases', 'fan.one'); fs.mkdirSync(dir, { recursive: true });
  const proof = path.join(dir, 'proof.json'); fs.writeFileSync(proof, JSON.stringify({ passed: true, checks: 1 }));
  const row = { ...f.passed('fan.one'), structuredEvidence: [{ path: proof, sha256: sha(fs.readFileSync(proof)), failed: false }] };
  const file = f.write('proof-run', [row]);
  assert.equal(f.plan([file], { tests: ['fan.one'] }).plan.summary.reusableAtAudit, 1);
  fs.writeFileSync(proof, JSON.stringify({ passed: true, checks: 999 }));
  const { plan } = f.plan([file], { tests: ['fan.one'] });
  assert.equal(plan.needsRerun[0].status, 'STALE_OR_INVALID_EVIDENCE');
});
test('old audit snapshots and invalid receipt files cannot certify or omit selected cases', t => {
  const f = fixture(t), file = f.write('initial');
  const snapshot = path.join(f.root, 'old-audit.json'); fs.writeFileSync(snapshot, JSON.stringify(f.plan([file]).audit));
  const { plan } = f.plan([snapshot]);
  assert.equal(plan.status, 'BLOCKED'); assert.equal(plan.summary.reusableAtAudit, 0); assert.equal(plan.summary.needsRerun, 3);
  const mixed = f.plan([file, snapshot]).plan;
  assert.equal(mixed.status, 'BLOCKED'); assert.equal(mixed.summary.reusableAtAudit, 0);
  assert(mixed.needsRerun.every(row => row.status === 'NEEDS_REAUDIT'));
});
test('source changes during planning block reuse of otherwise valid cases', t => {
  const f = fixture(t), file = f.write('initial');
  const originalRead = fs.readFileSync;
  let changed = false;
  fs.readFileSync = function (target, ...args) {
    if (!changed && String(target) === file) {
      changed = true; fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'product-v2');
    }
    return originalRead.call(this, target, ...args);
  };
  try {
    const { plan } = f.plan([file]);
    assert.equal(plan.status, 'BLOCKED'); assert.equal(plan.summary.reusableAtAudit, 0);
    assert(plan.diagnostics.some(row => row.code === 'SOURCE_CHANGED_DURING_PLANNING'));
  } finally { fs.readFileSync = originalRead; }
});
test('CLI rejects duplicate, unknown and incomplete arguments and never overwrites evidence', async t => {
  assert.equal(parseArgs(['--receipt', 'one', '--receipt', 'two', '--out-dir', 'new']).receipts.length, 2);
  for (const args of [[], ['--unknown', 'x'], ['--receipt', 'a', '--out-dir', 'x', '--out-dir', 'y'],
    ['--receipt', 'a', '--out-dir', 'x', '--max-batch-size', '2.5'], ['--receipt', 'a', '--out-dir', 'x', '--test', 'fan.one,']])
    assert.throws(() => parseArgs(args));
  const f = fixture(t), marker = path.join(f.root, 'kept.txt'); fs.writeFileSync(marker, 'keep');
  await assert.rejects(main(['--receipt', 'unused', '--out-dir', f.root], { repoRoot: f.repoRoot }), /EVIDENCE_DIRECTORY_ALREADY_EXISTS/);
  assert.equal(fs.readFileSync(marker, 'utf8'), 'keep');
});

test('dynamic imports recorded by execution are protected during the audit window', t => {
  const f = fixture(t), dynamic = path.join(f.repoRoot, 'dynamic.mjs'); fs.writeFileSync(dynamic, 'dynamic-v1');
  const row = { ...f.passed('fan.one'), dependencyInputs: [{ path: 'dynamic.mjs', sha256: sha('dynamic-v1') }] };
  const file = f.write('dynamic-run', [row]);
  const originalRead = fs.readFileSync;
  let reads = 0;
  fs.readFileSync = function (target, ...args) {
    if (String(target) === dynamic && ++reads === 3) fs.writeFileSync(dynamic, 'dynamic-v2');
    return originalRead.call(this, target, ...args);
  };
  try {
    const { plan } = f.plan([file], { tests: ['fan.one'] });
    assert.equal(plan.status, 'BLOCKED'); assert.equal(plan.summary.reusableAtAudit, 0);
    assert(plan.diagnostics.some(row => row.code === 'RECORDED_INPUT_CHANGED_DURING_PLANNING'));
  } finally { fs.readFileSync = originalRead; }
});
test('a candidate changed after its audit cannot remain in the reuse list', t => {
  const f = fixture(t), candidate = path.join(f.root, 'late-candidate.dll'); fs.writeFileSync(candidate, 'approved');
  const file = f.write('candidate-run', [{ ...f.passed('fan.one'), candidateArtifacts: [{ path: candidate, sha256: sha('approved') }] }]);
  const originalRead = fs.readFileSync;
  let reads = 0;
  fs.readFileSync = function (target, ...args) {
    if (String(target) === candidate && ++reads === 3) fs.writeFileSync(candidate, 'changed-after-audit');
    return originalRead.call(this, target, ...args);
  };
  try {
    const { plan, audit } = f.plan([file], { tests: ['fan.one'] });
    assert.equal(audit.cases[0].status, 'PASS');
    assert.equal(plan.status, 'BLOCKED'); assert.equal(plan.summary.reusableAtAudit, 0);
    assert.equal(plan.needsRerun[0].status, 'NEEDS_REAUDIT');
    assert(plan.diagnostics.some(row => row.code === 'RECORDED_INPUT_CHANGED_DURING_PLANNING'));
  } finally { fs.readFileSync = originalRead; }
});

test('saved batch preflight detects changed source before starting another test', t => {
  const f = fixture(t), file = f.write('initial');
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'target-v2');
  const { plan } = f.plan([file]);
  const batch = plan.batches[0];
  const same = checkBatchInputs(plan, batch.id, { repoRoot: f.repoRoot });
  assert.equal(same.status, 'INPUTS_MATCH_PLAN'); assert.equal(same.executedTests, 0);
  assert.equal(same.fullProductAcceptance, false);
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'changed-after-plan');
  const changed = checkBatchInputs(plan, batch.id, { repoRoot: f.repoRoot });
  assert.equal(changed.status, 'PLAN_INPUTS_CHANGED'); assert.equal(changed.executedTests, 0);
  assert(changed.changes.some(row => row.path === 'product.cs' && row.kind === 'source'));
});
test('current registered binary dependencies are pinned even before their first execution', t => {
  const f = fixture(t), binary = path.join(f.root, 'required-compiler.dll'); fs.writeFileSync(binary, 'compiler-v1');
  f.adapters[0].artifactInputs = [binary];
  const file = f.write('partial', [f.passed('fan.two')]);
  const { plan } = f.plan([file], { tests: ['fan.one'] });
  const batch = plan.batches[0];
  assert.equal(checkBatchInputs(plan, batch.id, { repoRoot: f.repoRoot }).status, 'INPUTS_MATCH_PLAN');
  fs.writeFileSync(binary, 'compiler-v2');
  const result = checkBatchInputs(plan, batch.id, { repoRoot: f.repoRoot });
  assert.equal(result.status, 'PLAN_INPUTS_CHANGED'); assert(result.changes.some(row => row.kind === 'artifact'));
});
test('an unavailable required planned input blocks readiness and does not become an absent match', t => {
  const f = fixture(t), file = f.write('partial', [f.passed('fan.two')]);
  fs.rmSync(path.join(f.repoRoot, 'fan.one.mjs'));
  const { plan } = f.plan([file], { tests: ['fan.one'] });
  const result = checkBatchInputs(plan, plan.batches[0].id, { repoRoot: f.repoRoot });
  assert.equal(result.status, 'BLOCKED_INPUTS_MISSING'); assert.equal(result.executedTests, 0);
  assert(result.missing.some(row => row.path === 'fan.one.mjs'));
});
test('dynamic execution dependencies are present in batch preflight inputs', t => {
  const f = fixture(t), dynamic = path.join(f.repoRoot, 'dynamic.mjs'); fs.writeFileSync(dynamic, 'dynamic-v1');
  const file = f.write('dynamic', [{ ...f.passed('fan.one'), dependencyInputs: [{ path: 'dynamic.mjs', sha256: sha('dynamic-v1') }] }]);
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'target-v2');
  const { plan } = f.plan([file], { tests: ['fan.one'] });
  assert(plan.batches[0].inputState.sources.some(row => row.path === 'dynamic.mjs'));
  fs.writeFileSync(dynamic, 'dynamic-v2');
  assert.equal(checkBatchInputs(plan, plan.batches[0].id, { repoRoot: f.repoRoot }).status, 'PLAN_INPUTS_CHANGED');
});
test('captured missing optional Host companions stay separate from required Host binaries', t => {
  const f = fixture(t), payload = path.join(f.root, 'host'); fs.mkdirSync(payload);
  f.adapters[0].scope = 'host-mock';
  const binaries = ['YeManFanHost.exe', 'YeManFanHost.dll'].map(name => {
    fs.writeFileSync(path.join(payload, name), 'approved'); return { name, sha256: sha('approved') };
  });
  binaries.push({ name: 'YeManFanBackend.dll', sha256: null });
  const file = f.write('host-run', [f.passed('fan.one')], { hostPayload: { root: payload, binaries } });
  fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'target-v2');
  const { plan } = f.plan([file], { tests: ['fan.one'] });
  const batch = plan.batches[0];
  assert.equal(checkBatchInputs(plan, batch.id, { repoRoot: f.repoRoot }).status, 'INPUTS_MATCH_PLAN');
  fs.writeFileSync(path.join(payload, 'YeManFanBackend.dll'), 'new-backend');
  assert.equal(checkBatchInputs(plan, batch.id, { repoRoot: f.repoRoot }).status, 'PLAN_INPUTS_CHANGED');
});
test('blocked plans, unknown batches and altered batch inputs cannot produce readiness', t => {
  const f = fixture(t), file = f.write('initial'); fs.writeFileSync(path.join(f.repoRoot, 'product.cs'), 'target-v2');
  const { plan } = f.plan([file]);
  assert.throws(() => checkBatchInputs({ ...plan, status: 'BLOCKED' }, 'batch-001', { repoRoot: f.repoRoot }), /BATCH_PLAN_INPUT_INVALID/);
  assert.throws(() => checkBatchInputs(plan, 'missing', { repoRoot: f.repoRoot }), /BATCH_ID_NOT_UNIQUE_OR_MISSING/);
  plan.batches[0].inputState.sources[0].sha256 = sha('changed-json');
  assert.throws(() => checkBatchInputs(plan, 'batch-001', { repoRoot: f.repoRoot }), /BATCH_INPUT_STATE_INVALID/);
});

test('historical inputs replaced by a newer receipt cannot block the current batch plan', t => {
  const f = fixture(t), oldOnly = path.join(f.repoRoot, 'old-only.cs'); fs.writeFileSync(oldOnly, 'historical-v1');
  const old = f.write('old', [{ ...f.passed('fan.one'), dependencyInputs: [{ path: 'old-only.cs', sha256: sha('historical-v1') }] }]);
  const latest = f.write('latest', [f.passed('fan.one')]);
  const originalRead = fs.readFileSync;
  let reads = 0;
  fs.readFileSync = function (target, ...args) {
    if (String(target) === oldOnly && ++reads === 2) fs.writeFileSync(oldOnly, 'historical-unrelated-change');
    return originalRead.call(this, target, ...args);
  };
  try {
    const { plan, audit } = f.plan([old, latest], { tests: ['fan.one'] });
    assert.equal(audit.cases[0].status, 'PASS');
    assert.equal(plan.status, 'PLAN_ONLY');
    assert.equal(plan.summary.reusableAtAudit, 1);
  } finally { fs.readFileSync = originalRead; }
});

test('receipt selection retains only contributors and preserves the latest failure semantics', t => {
  const f = fixture(t), old = f.write('old'), latest = f.write('latest', [{ id: 'fan.one', status: 'FAIL' }, f.passed('fan.two'), f.passed('lighting.three')]);
  const oldBytes = fs.readFileSync(old), latestBytes = fs.readFileSync(latest);
  const selection = selectContributingReceipts([old, latest]);
  assert.equal(selection.status, 'SELECTION_ONLY'); assert.equal(selection.executedTests, 0);
  assert.deepEqual(selection.receiptFiles, [latest]); assert.equal(selection.supersededCount, 1);
  const full = f.plan([old, latest]).audit, compact = f.plan(selection.receiptFiles).audit;
  assert.deepEqual(compact.cases, full.cases); assert.deepEqual(compact.summary, full.summary);
  assert.equal(compact.cases.find(row => row.id === 'fan.one').status, 'FAIL');
  assert.deepEqual(fs.readFileSync(old), oldBytes); assert.deepEqual(fs.readFileSync(latest), latestBytes);
});
test('a receipt that still owns any latest case stays in the original order', t => {
  const f = fixture(t), old = f.write('all'), middle = f.write('middle', [f.passed('fan.one')]), latest = f.write('latest', [{ id: 'fan.one', status: 'BLOCKED' }]);
  const selection = selectContributingReceipts([old, middle, latest]);
  assert.deepEqual(selection.receiptFiles, [old, latest]);
  assert.equal(selection.omitted[0].file, middle);
  assert.equal(f.plan(selection.receiptFiles).audit.cases.find(row => row.id === 'fan.one').status, 'BLOCKED');
});
test('invalid old containers stay visible instead of being silently compacted away', t => {
  const f = fixture(t), invalid = f.write('invalid', [{ id: '../outside', status: 'PASS' }]), latest = f.write('latest');
  const selection = selectContributingReceipts([invalid, latest]);
  assert.deepEqual(selection.receiptFiles, [invalid, latest]);
  assert.equal(selection.retained[0].invalidContainerRetained, true);
  assert.equal(f.plan(selection.receiptFiles).plan.status, 'BLOCKED');
});
test('many superseded receipts can fit the formal audit limit without new synthetic results', t => {
  const f = fixture(t), files = Array.from({ length: 40 }, (_, index) => f.write(`run-${index}`, [f.passed('fan.one')]));
  const selection = selectContributingReceipts(files);
  assert.equal(selection.inputCount, 40); assert.equal(selection.retainedCount, 1);
  assert.equal(selection.supersededCount, 39); assert.equal(selection.withinFormalAuditReceiptLimit, true);
  assert.equal(f.plan(selection.receiptFiles, { tests: ['fan.one'] }).plan.summary.reusableAtAudit, 1);
});
test('receipt selection reports over-limit contributing inputs without pretending to combine them', t => {
  const f = fixture(t), files = Array.from({ length: RECEIPT_AUDIT_LIMITS.maxFiles + 1 }, (_, index) => f.write(`run-${index}`, [{ ...f.passed('fan.one'), id: `case.${index}` }]));
  const selection = selectContributingReceipts(files);
  assert.equal(selection.retainedCount, RECEIPT_AUDIT_LIMITS.maxFiles + 1); assert.equal(selection.supersededCount, 0);
  assert.equal(selection.withinFormalAuditReceiptLimit, false);
  assert.throws(() => selectContributingReceipts([]), /INPUT_INVALID/);
  assert.throws(() => selectContributingReceipts(Array(257).fill(files[0])), /INPUT_INVALID/);
});
test('more than 32 original contributors retain order and latest failure obligations', t => {
  const ids = Array.from({ length: 40 }, (_, index) => `fan.case-${index}`);
  const f = fixture(t, ids), old = f.write('older-all-pass');
  const files = ids.map((id, index) => f.write(`contributor-${index}`, [{ ...f.passed(id),
    ...(index === 38 ? { status: 'FAIL', exitCode: 2 } : index === 39 ? { status: 'BLOCKED' } : {}),
  }]));
  const selection = selectContributingReceipts([old, ...files]);
  assert.equal(selection.retainedCount, 40); assert.equal(selection.withinFormalAuditReceiptLimit, true);
  assert.deepEqual(selection.receiptFiles, files);
  const {plan, audit} = f.plan([old, ...files]);
  assert.equal(plan.status, 'PLAN_ONLY'); assert.equal(plan.executedTests, 0);
  assert.equal(plan.summary.reusableAtAudit, 38); assert.equal(plan.summary.needsRerun, 2);
  assert.deepEqual(audit.sourceRuns.map(row => row.file), [old, ...files]);
  assert.deepEqual(plan.needsRerun.map(row => [row.id, row.status]), [[ids[38], 'FAIL'], [ids[39], 'BLOCKED']]);
  assert.equal(plan.fullProductAcceptance, false); assert.equal(plan.physicalHardwareAcceptance, false);
});
test('128 contributing originals are accepted while the 129th stays visible and is rejected by planning', t => {
  const ids = Array.from({ length: RECEIPT_AUDIT_LIMITS.maxFiles + 1 }, (_, index) => `fan.boundary-${index}`);
  const f = fixture(t, ids), files = ids.map((id, index) => f.write(`boundary-${index}`, [f.passed(id)]));
  const atLimit = files.slice(0, RECEIPT_AUDIT_LIMITS.maxFiles);
  assert.equal(selectContributingReceipts(atLimit).withinFormalAuditReceiptLimit, true);
  const {plan, audit} = f.plan(atLimit, {tests: ids.slice(0, RECEIPT_AUDIT_LIMITS.maxFiles)});
  assert.equal(plan.status, 'PLAN_ONLY'); assert.equal(plan.summary.reusableAtAudit, RECEIPT_AUDIT_LIMITS.maxFiles);
  assert.deepEqual(audit.sourceRuns.map(row => row.file), atLimit);
  const over = selectContributingReceipts(files);
  assert.equal(over.withinFormalAuditReceiptLimit, false); assert.deepEqual(over.receiptFiles, files);
  assert.equal(over.omitted.length, 0);
  assert.throws(() => f.plan(files), /REVALIDATION_PLAN_INPUT_INVALID/);
  assert.equal(selectContributingReceipts(Array(256).fill(files[0])).inputCount, 256);
});
test('40 pending contributors still split into execution batches of 32 and 8', t => {
  const ids = Array.from({length: 40}, (_, index) => `fan.pending-${index}`), f = fixture(t, ids);
  const files = ids.map((id, index) => f.write(`pending-${index}`, [{...f.passed(id), status: 'FAIL', exitCode: 2}]));
  const {plan} = f.plan(files, {maxBatchSize: 32});
  assert.equal(plan.status, 'PLAN_ONLY'); assert.equal(plan.summary.needsRerun, 40);
  assert.deepEqual(plan.batches.map(row => row.testIds.length), [32, 8]);
  assert.deepEqual(plan.batches.flatMap(row => row.testIds).sort(), [...ids].sort());
  assert.throws(() => f.plan(files, {maxBatchSize: 33}), /REVALIDATION_PLAN_INPUT_INVALID/);
});
test('oversized originals are retained and blocked before any receipt payload read', t => {
  const f = fixture(t), file = f.write('oversized');
  const originalStat = fs.statSync, originalRead = fs.readFileSync;
  let payloadReads = 0;
  fs.statSync = function(target, ...args) {
    const result = originalStat.call(this, target, ...args);
    if (path.resolve(String(target)) === path.resolve(file)) result.size = RECEIPT_AUDIT_LIMITS.maxSingleBytes + 1;
    return result;
  };
  fs.readFileSync = function(target, ...args) {
    if (path.resolve(String(target)) === path.resolve(file)) payloadReads++;
    return originalRead.call(this, target, ...args);
  };
  try {
    const selection = selectContributingReceipts([file]);
    assert.deepEqual(selection.receiptFiles, [file]); assert.equal(selection.withinFormalAuditReceiptLimit, false);
    const {plan} = f.plan([file]);
    assert.equal(plan.status, 'BLOCKED'); assert.equal(plan.summary.reusableAtAudit, 0);
    assert.equal(payloadReads, 0);
  } finally {fs.statSync = originalStat; fs.readFileSync = originalRead;}
});
test('real 32 MiB receipt bytes meet the 512 MiB boundary and an extra byte blocks reuse', t => {
  const f = fixture(t), file = f.write('byte-boundary', [f.passed('fan.one')]);
  fs.appendFileSync(file, Buffer.alloc(RECEIPT_AUDIT_LIMITS.maxSingleBytes - fs.statSync(file).size, 0x20));
  const copies = RECEIPT_AUDIT_LIMITS.maxTotalBytes / RECEIPT_AUDIT_LIMITS.maxSingleBytes;
  assert(Number.isInteger(copies));
  const atLimit = Array(copies).fill(file);
  assert.equal(selectContributingReceipts(atLimit).withinFormalAuditReceiptLimit, true);
  const current = f.plan(atLimit, {tests: ['fan.one']});
  assert.equal(current.plan.status, 'PLAN_ONLY'); assert.equal(current.plan.summary.reusableAtAudit, 1);
  assert.equal(current.audit.receiptBytesRead, RECEIPT_AUDIT_LIMITS.maxTotalBytes);
  const extra = path.join(f.root, 'one-more-byte.json'); fs.writeFileSync(extra, ' ');
  const originalStat = fs.statSync;
  // Simulate growth after stat: actual read lengths must still enforce both limits.
  fs.statSync = function(target, ...args) {
    const result = originalStat.call(this, target, ...args), resolved = path.resolve(String(target));
    if (resolved === path.resolve(extra)) result.size = 0;
    if (resolved === path.resolve(file)) result.size = RECEIPT_AUDIT_LIMITS.maxSingleBytes;
    return result;
  };
  try {
    const over = [...atLimit, extra], selection = selectContributingReceipts(over);
    assert(selection.receiptFiles.includes(extra)); assert.equal(selection.withinFormalAuditReceiptLimit, false);
    assert.equal(selection.retained.find(row => row.file === extra).readError, 'RECEIPT_TOTAL_SIZE_LIMIT');
    const blocked = f.plan(over);
    assert.equal(blocked.plan.status, 'BLOCKED'); assert.equal(blocked.plan.summary.reusableAtAudit, 0);
    assert(blocked.plan.diagnostics.some(row => row.code === 'RECEIPT_TOTAL_SIZE_LIMIT'));
    assert.equal(blocked.audit.receiptBytesRead, RECEIPT_AUDIT_LIMITS.maxTotalBytes);
    fs.appendFileSync(file, ' ');
    const grown = selectContributingReceipts([file]);
    assert.deepEqual(grown.receiptFiles, [file]); assert.equal(grown.withinFormalAuditReceiptLimit, false);
    assert.equal(grown.retained[0].readError, 'RECEIPT_SIZE_LIMIT');
    const oversized = f.plan([file]);
    assert.equal(oversized.plan.status, 'BLOCKED'); assert.equal(oversized.plan.summary.reusableAtAudit, 0);
  } finally {fs.statSync = originalStat;}
});
test('receipt bytes changing during selection prevent dropping any original input', t => {
  const f = fixture(t), old = f.write('old'), latest = f.write('latest');
  const originalRead = fs.readFileSync;
  let reads = 0;
  fs.readFileSync = function (target, ...args) {
    if (String(target) === old && ++reads === 2) fs.writeFileSync(old, 'invalidated-during-selection');
    return originalRead.call(this, target, ...args);
  };
  try {
    const selection = selectContributingReceipts([old, latest]);
    assert.equal(selection.status, 'BLOCKED_INPUT_CHANGED');
    assert.deepEqual(selection.receiptFiles, [old, latest]); assert.deepEqual(selection.omitted, []);
  } finally { fs.readFileSync = originalRead; }
});


test('completion output changes during planning invalidate reuse without executing product', async t => {
  const f = fixture(t);
  const {verifyExecutionCompletion} = await import('../execution-completion/index.mjs');
  const row = f.passed('fan.one');
  const file = f.write('completion-log-drift', [row, f.passed('fan.two'), f.passed('lighting.three')]);
  const caseDir = path.join(path.dirname(file), 'cases', row.id);
  fs.mkdirSync(caseDir, {recursive: true});
  const stdout = path.join(caseDir, 'stdout.log');
  fs.writeFileSync(stdout, 'suite complete\n');
  fs.writeFileSync(path.join(caseDir, 'stderr.log'), '');
  row.completionProof = verifyExecutionCompletion({exitCode: 0, stdout: 'suite complete\n', stderr: ''}, {schemaVersion: 1, rules: [{line: 'suite complete'}]});
  const run = JSON.parse(fs.readFileSync(file, 'utf8'));
  run.results[0] = row;
  fs.writeFileSync(file, JSON.stringify(run));
  const originalRead = fs.readFileSync;
  let productReads = 0;
  fs.readFileSync = function(target, ...args) {
    const bytes = originalRead.call(this, target, ...args);
    if (path.resolve(String(target)) === path.resolve(f.repoRoot, 'product.cs') && ++productReads === 3)
      fs.writeFileSync(stdout, 'changed during the formal audit\n');
    return bytes;
  };
  try {
    const {plan} = f.plan([file]);
    assert(productReads >= 3, 'the mutation must actually occur in the audit window');
    assert.equal(plan.status, 'BLOCKED');
    assert(plan.diagnostics.some(row => row.code === 'RECORDED_INPUT_CHANGED_DURING_PLANNING'));
    assert.equal(plan.summary.reusableAtAudit, 0);
    assert.equal(plan.executedTests, 0);
  } finally { fs.readFileSync = originalRead; }
});


test('user-excluded historical failure stays visible but cannot enter the current registry rerun queue', t => {
  const excluded = 'sleep.wake-password-ui-consumer';
  const f = fixture(t, ['fan.one', excluded]);
  const historical = f.write('before-user-exclusion', [f.passed('fan.one'), { ...f.passed(excluded), status: 'FAIL', exitCode: 1 }]);
  const before = sha(fs.readFileSync(historical));
  const current = f.adapters.filter(row => row.id !== excluded);
  const { plan, audit } = f.plan([historical], { adapters: current });
  assert.equal(plan.status, 'PLAN_ONLY');
  assert.deepEqual(plan.selection.ids, ['fan.one']);
  assert.equal(plan.summary.selected, 1);
  assert.equal(plan.summary.reusableAtAudit, 1);
  assert.equal(plan.summary.needsRerun, 0);
  assert.deepEqual(plan.needsRerun, []);
  assert.deepEqual(plan.batches, []);
  assert.deepEqual(plan.outsideSelection, [{ id: excluded, status: 'FAIL' }]);
  assert.equal(audit.cases.find(row => row.id === excluded).status, 'FAIL');
  assert.equal(sha(fs.readFileSync(historical)), before);
  assert.equal(plan.executedTests, 0);
});
